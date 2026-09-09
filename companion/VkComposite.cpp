// Vulkan frame composite (see VkComposite.h). One batched draw list per frame, submitted on the
// game's present queue between its last submit and the present.

#include "VkComposite.h"
#include "VkShaders.h"
#include "MarkerShare.h"

#include <windows.h>
#undef CreateSemaphore
#include <cmath>
#include <cstring>
#include <vector>

#pragma comment(lib, "gdi32.lib")

namespace rtx::vkcomposite {
namespace {

struct Vertex { float x, y, u, v, r, g, b, a; };
struct Batch  { int tex; int mode; std::uint32_t first, count; };
struct PushConst { float sx, sy, tx, ty; std::int32_t mode; };

enum Tex { kTexWhite = 0, kTexAtlas = 1, kTexUi = 2, kTexHud = 3, kTexCount = 4 };

struct Buffer {
    VkBuffer       buf  = VK_NULL_HANDLE;
    VkDeviceMemory mem  = VK_NULL_HANDLE;
    void*          map  = nullptr;
    VkDeviceSize   size = 0;
};

struct Texture {
    VkImage         img    = VK_NULL_HANDLE;
    VkDeviceMemory  mem    = VK_NULL_HANDLE;
    VkImageView     view   = VK_NULL_HANDLE;
    VkDescriptorSet set    = VK_NULL_HANDLE;
    VkFormat        fmt    = VK_FORMAT_UNDEFINED;
    std::uint32_t   w = 0, h = 0;
    VkImageLayout   layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct Upload { int tex; std::uint32_t x, y, w, h; VkDeviceSize off; };

struct Image {
    VkImage         img   = VK_NULL_HANDLE;
    VkImageView     view  = VK_NULL_HANDLE;
    VkFramebuffer   fb    = VK_NULL_HANDLE;
    VkCommandBuffer cmd   = VK_NULL_HANDLE;
    VkFence         fence = VK_NULL_HANDLE;
    VkSemaphore     sem   = VK_NULL_HANDLE;
    bool            pending = false;
    Buffer          vbuf, sbuf;
};

struct FormatRes { VkFormat fmt; VkRenderPass rp; VkPipeline pipe; };

struct Swapchain {
    VkSwapchainKHR     sc = VK_NULL_HANDLE;
    VkFormat           fmt = VK_FORMAT_UNDEFINED;
    std::uint32_t      w = 0, h = 0;
    std::size_t        res = 0;
    std::vector<Image> images;
};

VkDevice                         g_dev = VK_NULL_HANDLE;
DeviceFns                        g_fn{};
VkPhysicalDeviceMemoryProperties g_mem{};
std::uint32_t                    g_family = 0;
bool                             g_ready = false;

VkCommandPool         g_pool      = VK_NULL_HANDLE;
VkDescriptorSetLayout g_setLayout = VK_NULL_HANDLE;
VkPipelineLayout      g_pipeLayout = VK_NULL_HANDLE;
VkDescriptorPool      g_descPool  = VK_NULL_HANDLE;
VkSampler             g_sampler   = VK_NULL_HANDLE;
VkShaderModule        g_vert = VK_NULL_HANDLE, g_frag = VK_NULL_HANDLE;
std::vector<FormatRes> g_formats;
std::vector<Swapchain> g_chains;
Texture               g_tex[kTexCount];

std::vector<unsigned char> g_atlasCov;
int  g_atlas_w = 0, g_atlas_h = 0;
int  g_glyph_adv[rtx::marker::kGlyphLast - rtx::marker::kGlyphFirst + 1] = { 0 };
bool g_staticUploaded = false;

// Frame state (present thread).
Swapchain*          g_curChain = nullptr;
Image*              g_cur      = nullptr;
bool                g_active   = false;
std::vector<Vertex> g_verts;
std::vector<Batch>  g_batches;
std::vector<Upload> g_uploads;
VkDeviceSize        g_stageUsed = 0;

std::uint32_t FindMemType(std::uint32_t bits, VkMemoryPropertyFlags want) {
    for (std::uint32_t i = 0; i < g_mem.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (g_mem.memoryTypes[i].propertyFlags & want) == want) return i;
    return UINT32_MAX;
}

void DestroyBuffer(Buffer& b) {
    if (b.map) { g_fn.UnmapMemory(g_dev, b.mem); b.map = nullptr; }
    if (b.buf) g_fn.DestroyBuffer(g_dev, b.buf, nullptr);
    if (b.mem) g_fn.FreeMemory(g_dev, b.mem, nullptr);
    b = Buffer{};
}

bool CreateHostBuffer(Buffer& b, VkDeviceSize size, VkBufferUsageFlags usage) {
    DestroyBuffer(b);
    VkBufferCreateInfo ci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    ci.size = size; ci.usage = usage; ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (g_fn.CreateBuffer(g_dev, &ci, nullptr, &b.buf) != VK_SUCCESS) return false;
    VkMemoryRequirements req{}; g_fn.GetBufferMemoryRequirements(g_dev, b.buf, &req);
    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = FindMemType(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX ||
        g_fn.AllocateMemory(g_dev, &ai, nullptr, &b.mem) != VK_SUCCESS ||
        g_fn.BindBufferMemory(g_dev, b.buf, b.mem, 0) != VK_SUCCESS ||
        g_fn.MapMemory(g_dev, b.mem, 0, VK_WHOLE_SIZE, 0, &b.map) != VK_SUCCESS) {
        DestroyBuffer(b); return false;
    }
    b.size = size;
    return true;
}

// Growth keeps the first `live` bytes: staging data queued earlier in the frame still refers to them.
bool EnsureBuffer(Buffer& b, VkDeviceSize need, VkBufferUsageFlags usage, VkDeviceSize live = 0) {
    if (b.buf && b.size >= need) return true;
    VkDeviceSize sz = b.size ? b.size : (VkDeviceSize)(1u << 20);
    while (sz < need) sz *= 2;
    Buffer fresh;
    if (!CreateHostBuffer(fresh, sz, usage)) return false;
    if (live && b.map) std::memcpy(fresh.map, b.map, (size_t)(live < b.size ? live : b.size));
    DestroyBuffer(b);
    b = fresh;
    return true;
}

void DestroyTexture(Texture& t) {
    if (t.view) g_fn.DestroyImageView(g_dev, t.view, nullptr);
    if (t.img)  g_fn.DestroyImage(g_dev, t.img, nullptr);
    if (t.mem)  g_fn.FreeMemory(g_dev, t.mem, nullptr);
    VkDescriptorSet keep = t.set;
    t = Texture{};
    t.set = keep;
}

bool CreateTexture(Texture& t, VkFormat fmt, std::uint32_t w, std::uint32_t h, bool coverage) {
    DestroyTexture(t);
    VkImageCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ci.imageType = VK_IMAGE_TYPE_2D; ci.format = fmt;
    ci.extent = { w, h, 1 }; ci.mipLevels = 1; ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT; ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE; ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (g_fn.CreateImage(g_dev, &ci, nullptr, &t.img) != VK_SUCCESS) return false;
    VkMemoryRequirements req{}; g_fn.GetImageMemoryRequirements(g_dev, t.img, &req);
    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = FindMemType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX) ai.memoryTypeIndex = FindMemType(req.memoryTypeBits, 0);
    if (ai.memoryTypeIndex == UINT32_MAX ||
        g_fn.AllocateMemory(g_dev, &ai, nullptr, &t.mem) != VK_SUCCESS ||
        g_fn.BindImageMemory(g_dev, t.img, t.mem, 0) != VK_SUCCESS) { DestroyTexture(t); return false; }
    VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vi.image = t.img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = fmt;
    if (coverage) vi.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R };
    vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (g_fn.CreateImageView(g_dev, &vi, nullptr, &t.view) != VK_SUCCESS) { DestroyTexture(t); return false; }
    t.fmt = fmt; t.w = w; t.h = h; t.layout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkDescriptorImageInfo di{ g_sampler, t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet wr{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    wr.dstSet = t.set; wr.dstBinding = 0; wr.descriptorCount = 1;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; wr.pImageInfo = &di;
    g_fn.UpdateDescriptorSets(g_dev, 1, &wr, 0, nullptr);
    return true;
}

void WaitImage(Image& im) {
    if (!im.pending) return;
    g_fn.WaitForFences(g_dev, 1, &im.fence, VK_TRUE, 2000000000ull);
    g_fn.ResetFences(g_dev, 1, &im.fence);
    im.pending = false;
}

void WaitAll() {
    for (auto& c : g_chains) for (auto& im : c.images) WaitImage(im);
}

std::size_t ResForFormat(VkFormat fmt) {
    for (std::size_t i = 0; i < g_formats.size(); ++i) if (g_formats[i].fmt == fmt) return i;

    VkAttachmentDescription att{};
    att.format = fmt; att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sp.colorAttachmentCount = 1; sp.pColorAttachments = &ref;
    VkSubpassDependency dep[2]{};
    dep[0].srcSubpass = VK_SUBPASS_EXTERNAL; dep[0].dstSubpass = 0;
    dep[0].srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT; dep[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    dep[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep[1].srcSubpass = 0; dep[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dep[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT; dep[1].dstAccessMask = 0;
    VkRenderPassCreateInfo rpi{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpi.attachmentCount = 1; rpi.pAttachments = &att; rpi.subpassCount = 1; rpi.pSubpasses = &sp;
    rpi.dependencyCount = 2; rpi.pDependencies = dep;
    VkRenderPass rp = VK_NULL_HANDLE;
    if (g_fn.CreateRenderPass(g_dev, &rpi, nullptr, &rp) != VK_SUCCESS) return SIZE_MAX;

    VkPipelineShaderStageCreateInfo st[2]{};
    st[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT; st[0].module = g_vert; st[0].pName = "main";
    st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module = g_frag; st[1].pName = "main";
    VkVertexInputBindingDescription bind{ 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription attr[3] = {
        { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 },
        { 1, 0, VK_FORMAT_R32G32_SFLOAT, 8 },
        { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16 },
    };
    VkPipelineVertexInputStateCreateInfo vin{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vin.vertexBindingDescriptionCount = 1; vin.pVertexBindingDescriptions = &bind;
    vin.vertexAttributeDescriptionCount = 3; vin.pVertexAttributeDescriptions = attr;
    VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState ba{};
    ba.blendEnable = VK_TRUE;
    ba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    ba.colorBlendOp = VK_BLEND_OP_ADD;
    ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    ba.alphaBlendOp = VK_BLEND_OP_ADD;
    ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1; cb.pAttachments = &ba;
    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    ds.dynamicStateCount = 2; ds.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo pi{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pi.stageCount = 2; pi.pStages = st; pi.pVertexInputState = &vin; pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp; pi.pRasterizationState = &rs; pi.pMultisampleState = &ms;
    pi.pColorBlendState = &cb; pi.pDynamicState = &ds; pi.layout = g_pipeLayout;
    pi.renderPass = rp; pi.subpass = 0;
    VkPipeline pipe = VK_NULL_HANDLE;
    if (g_fn.CreateGraphicsPipelines(g_dev, VK_NULL_HANDLE, 1, &pi, nullptr, &pipe) != VK_SUCCESS) {
        g_fn.DestroyRenderPass(g_dev, rp, nullptr); return SIZE_MAX;
    }
    g_formats.push_back({ fmt, rp, pipe });
    return g_formats.size() - 1;
}

void BuildGlyphAtlas() {
    const int first = rtx::marker::kGlyphFirst, last = rtx::marker::kGlyphLast;
    const int cols  = rtx::marker::kGlyphCols;
    const int cw    = rtx::marker::kGlyphCellW, chh = rtx::marker::kGlyphCellH;
    const int count = last - first + 1;
    const int rows  = (count + cols - 1) / cols;
    const int aw = cols * cw, ah = rows * chh;

    HDC memDC = CreateCompatibleDC(nullptr);
    if (!memDC) return;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = aw; bi.bmiHeader.biHeight = -ah;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) { DeleteDC(memDC); return; }
    HGDIOBJ oldBmp = SelectObject(memDC, dib);
    std::memset(bits, 0, (size_t)aw * ah * 4);
    HFONT font = CreateFontW(-(chh - 13), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HGDIOBJ oldFont = font ? SelectObject(memDC, font) : nullptr;
    SetTextColor(memDC, RGB(255, 255, 255));
    SetBkMode(memDC, TRANSPARENT);
    for (int i = 0; i < count; ++i) {
        wchar_t wc = (wchar_t)(first + i);
        int col = i % cols, row = i / cols;
        SIZE sz{};
        GetTextExtentPoint32W(memDC, &wc, 1, &sz);
        int adv = sz.cx; if (adv < 1) adv = 1; if (adv > cw) adv = cw;
        g_glyph_adv[i] = adv;
        TextOutW(memDC, col * cw, row * chh + (chh - sz.cy) / 2, &wc, 1);
    }
    GdiFlush();
    g_atlasCov.resize((size_t)aw * ah);
    const unsigned char* src = static_cast<const unsigned char*>(bits);
    for (size_t i = 0; i < g_atlasCov.size(); ++i) g_atlasCov[i] = src[i * 4];
    if (oldFont) SelectObject(memDC, oldFont);
    if (font) DeleteObject(font);
    SelectObject(memDC, oldBmp);
    DeleteObject(dib);
    DeleteDC(memDC);
    g_atlas_w = aw; g_atlas_h = ah;
}

// Reserve staging bytes in the current image; returns the mapped pointer or null.
void* Stage(VkDeviceSize bytes, VkDeviceSize* off) {
    if (!g_cur) return nullptr;
    VkDeviceSize start = (g_stageUsed + 15) & ~(VkDeviceSize)15;
    if (!EnsureBuffer(g_cur->sbuf, start + bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, g_stageUsed)) return nullptr;
    *off = start;
    g_stageUsed = start + bytes;
    return static_cast<std::uint8_t*>(g_cur->sbuf.map) + start;
}

void QueueUpload(int tex, const void* px, std::uint32_t x, std::uint32_t y,
                 std::uint32_t w, std::uint32_t h, std::uint32_t bpp, std::uint32_t srcStride) {
    VkDeviceSize off = 0;
    std::uint8_t* dst = static_cast<std::uint8_t*>(Stage((VkDeviceSize)w * h * bpp, &off));
    if (!dst) return;
    const std::uint8_t* s = static_cast<const std::uint8_t*>(px);
    const std::uint32_t row = w * bpp;
    if (srcStride == row) std::memcpy(dst, s, (size_t)row * h);
    else for (std::uint32_t r = 0; r < h; ++r) std::memcpy(dst + (size_t)r * row, s + (size_t)r * srcStride, row);
    g_uploads.push_back({ tex, x, y, w, h, off });
}

void Barrier(VkCommandBuffer cmd, Texture& t, VkImageLayout to,
             VkPipelineStageFlags srcStage, VkAccessFlags srcAccess,
             VkPipelineStageFlags dstStage, VkAccessFlags dstAccess) {
    VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.srcAccessMask = srcAccess; b.dstAccessMask = dstAccess;
    b.oldLayout = t.layout; b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = t.img; b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    g_fn.CmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
    t.layout = to;
}

inline bool Drawing() { return g_active && g_cur != nullptr; }

void Push(int tex, int mode, const Vertex* v, std::uint32_t n) {
    if (g_batches.empty() || g_batches.back().tex != tex || g_batches.back().mode != mode)
        g_batches.push_back({ tex, mode, (std::uint32_t)g_verts.size(), 0 });
    g_verts.insert(g_verts.end(), v, v + n);
    g_batches.back().count += n;
}

void PushQuad(int tex, int mode, const float* px, const float* py, const float* u, const float* v,
              float r, float g, float b, float a) {
    // Corners TL, TR, BL, BR.
    Vertex q[6] = {
        { px[0], py[0], u[0], v[0], r, g, b, a }, { px[1], py[1], u[1], v[1], r, g, b, a }, { px[2], py[2], u[2], v[2], r, g, b, a },
        { px[1], py[1], u[1], v[1], r, g, b, a }, { px[3], py[3], u[3], v[3], r, g, b, a }, { px[2], py[2], u[2], v[2], r, g, b, a },
    };
    Push(tex, mode, q, 6);
}

void PushRect(int tex, int mode, float x0, float y0, float x1, float y1,
              float u0, float v0, float u1, float v1, float r, float g, float b, float a) {
    const float px[4] = { x0, x1, x0, x1 }, py[4] = { y0, y0, y1, y1 };
    const float u[4] = { u0, u1, u0, u1 }, v[4] = { v0, v0, v1, v1 };
    PushQuad(tex, mode, px, py, u, v, r, g, b, a);
}

void RoundFill(float x, float y, float w, float h, float rad, float r, float g, float b, float a) {
    if (w <= 0.f || h <= 0.f) return;
    float maxr = (w < h ? w : h) * 0.5f;
    if (rad > maxr) rad = maxr;
    if (rad < 0.f)  rad = 0.f;
    const int SEG = 5;
    const float ccx[4] = { x + rad, x + w - rad, x + w - rad, x + rad     };
    const float ccy[4] = { y + rad, y + rad,     y + h - rad, y + h - rad };
    const float a0[4]  = { 3.14159265f, 4.71238898f, 0.0f, 1.57079633f };
    const float cenx = x + w * 0.5f, ceny = y + h * 0.5f;
    float ring[2 * 4 * (SEG + 1)]; int n = 0;
    for (int c = 0; c < 4; ++c)
        for (int s = 0; s <= SEG; ++s) {
            float ang = a0[c] + 1.57079633f * (float)s / (float)SEG;
            ring[n * 2] = ccx[c] + rad * (float)std::cos(ang);
            ring[n * 2 + 1] = ccy[c] + rad * (float)std::sin(ang);
            ++n;
        }
    Vertex tri[3 * 4 * (SEG + 1)];
    int t = 0;
    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        tri[t++] = { cenx, ceny, 0.5f, 0.5f, r, g, b, a };
        tri[t++] = { ring[i * 2], ring[i * 2 + 1], 0.5f, 0.5f, r, g, b, a };
        tri[t++] = { ring[j * 2], ring[j * 2 + 1], 0.5f, 0.5f, r, g, b, a };
    }
    Push(kTexWhite, 0, tri, (std::uint32_t)t);
}

void GlyphRun(const char* s, int len, float penX, float gTop, float scale, float cellH, float track,
              float r, float g, float b, float a) {
    const int first = rtx::marker::kGlyphFirst, last = rtx::marker::kGlyphLast;
    const int cols  = rtx::marker::kGlyphCols;
    const int cw = rtx::marker::kGlyphCellW, chh = rtx::marker::kGlyphCellH;
    for (int k = 0; k < len; ++k) {
        unsigned char ch = (unsigned char)s[k];
        if (ch < first || ch > last) continue;
        int cell = ch - first;
        int adv  = g_glyph_adv[cell];
        int col  = cell % cols, row = cell / cols;
        float u0 = (float)(col * cw)        / (float)g_atlas_w;
        float u1 = (float)(col * cw + adv)  / (float)g_atlas_w;
        float v0 = (float)(row * chh)       / (float)g_atlas_h;
        float v1 = (float)(row * chh + chh) / (float)g_atlas_h;
        float gw = adv * scale;
        PushRect(kTexAtlas, 0, penX, gTop, penX + gw, gTop + cellH, u0, v0, u1, v1, r, g, b, a);
        penX += gw + track;
    }
}

float RunWidth(const char* s, int len, float scale, float track) {
    const int first = rtx::marker::kGlyphFirst, last = rtx::marker::kGlyphLast;
    float tw = 0.0f; int glyphs = 0;
    for (int k = 0; k < len; ++k) {
        unsigned char ch = (unsigned char)s[k];
        if (ch < first || ch > last) continue;
        tw += g_glyph_adv[ch - first] * scale + track;
        ++glyphs;
    }
    if (glyphs && tw > track) tw -= track;
    return glyphs ? tw : -1.0f;
}

}  // namespace

bool Init(VkDevice dev, const DeviceFns& fns,
          const VkPhysicalDeviceMemoryProperties& mem, std::uint32_t queueFamily) {
    if (g_ready) return true;
    g_dev = dev; g_fn = fns; g_mem = mem; g_family = queueFamily;

    VkCommandPoolCreateInfo pci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pci.queueFamilyIndex = queueFamily;
    if (g_fn.CreateCommandPool(g_dev, &pci, nullptr, &g_pool) != VK_SUCCESS) return false;

    VkDescriptorSetLayoutBinding lb{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo li{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    li.bindingCount = 1; li.pBindings = &lb;
    if (g_fn.CreateDescriptorSetLayout(g_dev, &li, nullptr, &g_setLayout) != VK_SUCCESS) { Shutdown(); return false; }

    VkPushConstantRange pcr{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConst) };
    VkPipelineLayoutCreateInfo pli{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pli.setLayoutCount = 1; pli.pSetLayouts = &g_setLayout; pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcr;
    if (g_fn.CreatePipelineLayout(g_dev, &pli, nullptr, &g_pipeLayout) != VK_SUCCESS) { Shutdown(); return false; }

    VkDescriptorPoolSize ps{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kTexCount };
    VkDescriptorPoolCreateInfo dpi{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dpi.maxSets = kTexCount; dpi.poolSizeCount = 1; dpi.pPoolSizes = &ps;
    if (g_fn.CreateDescriptorPool(g_dev, &dpi, nullptr, &g_descPool) != VK_SUCCESS) { Shutdown(); return false; }
    VkDescriptorSetLayout layouts[kTexCount];
    for (auto& l : layouts) l = g_setLayout;
    VkDescriptorSet sets[kTexCount];
    VkDescriptorSetAllocateInfo dai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    dai.descriptorPool = g_descPool; dai.descriptorSetCount = kTexCount; dai.pSetLayouts = layouts;
    if (g_fn.AllocateDescriptorSets(g_dev, &dai, sets) != VK_SUCCESS) { Shutdown(); return false; }
    for (int i = 0; i < kTexCount; ++i) g_tex[i].set = sets[i];

    VkSamplerCreateInfo si{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    si.magFilter = VK_FILTER_LINEAR; si.minFilter = VK_FILTER_LINEAR; si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; si.maxLod = 0.0f;
    if (g_fn.CreateSampler(g_dev, &si, nullptr, &g_sampler) != VK_SUCCESS) { Shutdown(); return false; }

    VkShaderModuleCreateInfo smi{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    smi.codeSize = sizeof(rtx::vkshaders::kVert); smi.pCode = rtx::vkshaders::kVert;
    if (g_fn.CreateShaderModule(g_dev, &smi, nullptr, &g_vert) != VK_SUCCESS) { Shutdown(); return false; }
    smi.codeSize = sizeof(rtx::vkshaders::kFrag); smi.pCode = rtx::vkshaders::kFrag;
    if (g_fn.CreateShaderModule(g_dev, &smi, nullptr, &g_frag) != VK_SUCCESS) { Shutdown(); return false; }

    BuildGlyphAtlas();
    if (!CreateTexture(g_tex[kTexWhite], VK_FORMAT_R8G8B8A8_UNORM, 1, 1, false)) { Shutdown(); return false; }
    if (g_atlas_w > 0 && !CreateTexture(g_tex[kTexAtlas], VK_FORMAT_R8_UNORM, g_atlas_w, g_atlas_h, true)) { Shutdown(); return false; }
    g_staticUploaded = false;
    g_ready = true;
    return true;
}

bool Ready() { return g_ready; }

void UnregisterSwapchain(VkSwapchainKHR sc) {
    for (size_t i = 0; i < g_chains.size(); ++i) {
        if (g_chains[i].sc != sc) continue;
        Swapchain& c = g_chains[i];
        for (auto& im : c.images) {
            WaitImage(im);
            if (im.fb)    g_fn.DestroyFramebuffer(g_dev, im.fb, nullptr);
            if (im.view)  g_fn.DestroyImageView(g_dev, im.view, nullptr);
            if (im.sem)   g_fn.DestroySemaphore(g_dev, im.sem, nullptr);
            if (im.fence) g_fn.DestroyFence(g_dev, im.fence, nullptr);
            if (im.cmd)   g_fn.FreeCommandBuffers(g_dev, g_pool, 1, &im.cmd);
            DestroyBuffer(im.vbuf);
            DestroyBuffer(im.sbuf);
        }
        if (g_curChain == &c) { g_curChain = nullptr; g_cur = nullptr; }
        g_chains.erase(g_chains.begin() + (std::ptrdiff_t)i);
        g_curChain = nullptr; g_cur = nullptr;
        return;
    }
}

bool RegisterSwapchain(VkSwapchainKHR sc, VkFormat fmt, std::uint32_t w, std::uint32_t h) {
    if (!g_ready || !sc || w == 0 || h == 0) return false;
    UnregisterSwapchain(sc);
    std::size_t res = ResForFormat(fmt);
    if (res == SIZE_MAX) return false;

    std::uint32_t n = 0;
    if (g_fn.GetSwapchainImagesKHR(g_dev, sc, &n, nullptr) != VK_SUCCESS || n == 0 || n > 16) return false;
    VkImage imgs[16];
    if (g_fn.GetSwapchainImagesKHR(g_dev, sc, &n, imgs) != VK_SUCCESS) return false;

    Swapchain c; c.sc = sc; c.fmt = fmt; c.w = w; c.h = h; c.res = res;
    c.images.resize(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        Image& im = c.images[i];
        im.img = imgs[i];
        VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vi.image = im.img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = fmt;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkFramebufferCreateInfo fi{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        VkCommandBufferAllocateInfo ci{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        ci.commandPool = g_pool; ci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ci.commandBufferCount = 1;
        VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        VkSemaphoreCreateInfo sci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        bool ok = g_fn.CreateImageView(g_dev, &vi, nullptr, &im.view) == VK_SUCCESS;
        if (ok) {
            fi.renderPass = g_formats[res].rp; fi.attachmentCount = 1; fi.pAttachments = &im.view;
            fi.width = w; fi.height = h; fi.layers = 1;
            ok = g_fn.CreateFramebuffer(g_dev, &fi, nullptr, &im.fb) == VK_SUCCESS;
        }
        ok = ok && g_fn.AllocateCommandBuffers(g_dev, &ci, &im.cmd) == VK_SUCCESS;
        ok = ok && g_fn.CreateFence(g_dev, &fci, nullptr, &im.fence) == VK_SUCCESS;
        ok = ok && g_fn.CreateSemaphore(g_dev, &sci, nullptr, &im.sem) == VK_SUCCESS;
        if (!ok) {
            g_chains.push_back(c);
            UnregisterSwapchain(sc);
            return false;
        }
    }
    g_chains.push_back(c);
    return true;
}

int SwapchainCount() { return (int)g_chains.size(); }

bool BeginTarget(VkSwapchainKHR sc, std::uint32_t imageIndex, std::uint32_t* w, std::uint32_t* h) {
    g_curChain = nullptr; g_cur = nullptr;
    g_verts.clear(); g_batches.clear(); g_uploads.clear(); g_stageUsed = 0; g_active = false;
    if (!g_ready) return false;
    for (auto& c : g_chains) {
        if (c.sc != sc) continue;
        if (imageIndex >= c.images.size()) return false;
        Image& im = c.images[imageIndex];
        if (im.pending) {
            if (g_fn.WaitForFences(g_dev, 1, &im.fence, VK_TRUE, 500000000ull) != VK_SUCCESS) return false;
            g_fn.ResetFences(g_dev, 1, &im.fence);
            im.pending = false;
        }
        g_curChain = &c; g_cur = &im;
        if (w) *w = c.w;
        if (h) *h = c.h;
        return true;
    }
    return false;
}

void Begin() {
    if (!g_cur) return;
    g_active = true;
    if (!g_staticUploaded) {
        static const std::uint8_t white[4] = { 255, 255, 255, 255 };
        QueueUpload(kTexWhite, white, 0, 0, 1, 1, 4, 4);
        if (g_tex[kTexAtlas].img && !g_atlasCov.empty())
            QueueUpload(kTexAtlas, g_atlasCov.data(), 0, 0, (std::uint32_t)g_atlas_w, (std::uint32_t)g_atlas_h, 1, (std::uint32_t)g_atlas_w);
        g_staticUploaded = true;
    }
}

void End() { g_active = false; }

VkSemaphore Submit(VkQueue queue, std::uint32_t waitCount, const VkSemaphore* waits) {
    Image* im = g_cur; Swapchain* c = g_curChain;
    g_cur = nullptr; g_curChain = nullptr; g_active = false;
    if (!im || !c || (g_verts.empty() && g_uploads.empty())) return VK_NULL_HANDLE;

    const VkDeviceSize vbytes = (VkDeviceSize)g_verts.size() * sizeof(Vertex);
    if (vbytes && !EnsureBuffer(im->vbuf, vbytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) return VK_NULL_HANDLE;
    if (vbytes) std::memcpy(im->vbuf.map, g_verts.data(), (size_t)vbytes);

    VkCommandBuffer cmd = im->cmd;
    g_fn.ResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (g_fn.BeginCommandBuffer(cmd, &bi) != VK_SUCCESS) return VK_NULL_HANDLE;

    for (const auto& u : g_uploads) {
        Texture& t = g_tex[u.tex];
        if (!t.img) continue;
        Barrier(cmd, t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        VkBufferImageCopy rg{};
        rg.bufferOffset = u.off; rg.bufferRowLength = u.w; rg.bufferImageHeight = u.h;
        rg.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        rg.imageOffset = { (std::int32_t)u.x, (std::int32_t)u.y, 0 };
        rg.imageExtent = { u.w, u.h, 1 };
        g_fn.CmdCopyBufferToImage(cmd, im->sbuf.buf, t.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
    }
    for (int i = 0; i < kTexCount; ++i) {
        Texture& t = g_tex[i];
        if (t.img && t.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && t.layout != VK_IMAGE_LAYOUT_UNDEFINED)
            Barrier(cmd, t, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    if (!g_batches.empty()) {
        VkRenderPassBeginInfo rp{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        rp.renderPass = g_formats[c->res].rp; rp.framebuffer = im->fb;
        rp.renderArea = { { 0, 0 }, { c->w, c->h } };
        g_fn.CmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        g_fn.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_formats[c->res].pipe);
        VkViewport vp{ 0.f, 0.f, (float)c->w, (float)c->h, 0.f, 1.f };
        VkRect2D sc{ { 0, 0 }, { c->w, c->h } };
        g_fn.CmdSetViewport(cmd, 0, 1, &vp);
        g_fn.CmdSetScissor(cmd, 0, 1, &sc);
        VkDeviceSize zero = 0;
        g_fn.CmdBindVertexBuffers(cmd, 0, 1, &im->vbuf.buf, &zero);
        PushConst pc{ 2.0f / (float)c->w, 2.0f / (float)c->h, -1.0f, -1.0f, -1 };
        for (const auto& b : g_batches) {
            Texture& t = g_tex[b.tex];
            if (!t.img || t.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) continue;
            if (pc.mode != b.mode) {
                pc.mode = b.mode;
                g_fn.CmdPushConstants(cmd, g_pipeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
            }
            g_fn.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipeLayout, 0, 1, &t.set, 0, nullptr);
            g_fn.CmdDraw(cmd, b.count, 1, b.first, 0);
        }
        g_fn.CmdEndRenderPass(cmd);
    }
    if (g_fn.EndCommandBuffer(cmd) != VK_SUCCESS) return VK_NULL_HANDLE;

    VkPipelineStageFlags stages[32];
    if (waitCount > 32) waitCount = 32;
    for (std::uint32_t i = 0; i < waitCount; ++i) stages[i] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.waitSemaphoreCount = waitCount; si.pWaitSemaphores = waits; si.pWaitDstStageMask = stages;
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1; si.pSignalSemaphores = &im->sem;
    if (g_fn.QueueSubmit(queue, 1, &si, im->fence) != VK_SUCCESS) return VK_NULL_HANDLE;
    im->pending = true;
    return im->sem;
}

void Shutdown() {
    if (g_dev && g_fn.DeviceWaitIdle) g_fn.DeviceWaitIdle(g_dev);
    while (!g_chains.empty()) UnregisterSwapchain(g_chains.back().sc);
    for (auto& t : g_tex) DestroyTexture(t);
    for (auto& r : g_formats) {
        if (r.pipe) g_fn.DestroyPipeline(g_dev, r.pipe, nullptr);
        if (r.rp)   g_fn.DestroyRenderPass(g_dev, r.rp, nullptr);
    }
    g_formats.clear();
    if (g_vert)       g_fn.DestroyShaderModule(g_dev, g_vert, nullptr);
    if (g_frag)       g_fn.DestroyShaderModule(g_dev, g_frag, nullptr);
    if (g_sampler)    g_fn.DestroySampler(g_dev, g_sampler, nullptr);
    if (g_descPool)   g_fn.DestroyDescriptorPool(g_dev, g_descPool, nullptr);
    if (g_pipeLayout) g_fn.DestroyPipelineLayout(g_dev, g_pipeLayout, nullptr);
    if (g_setLayout)  g_fn.DestroyDescriptorSetLayout(g_dev, g_setLayout, nullptr);
    if (g_pool)       g_fn.DestroyCommandPool(g_dev, g_pool, nullptr);
    g_vert = g_frag = VK_NULL_HANDLE; g_sampler = VK_NULL_HANDLE; g_descPool = VK_NULL_HANDLE;
    g_pipeLayout = VK_NULL_HANDLE; g_setLayout = VK_NULL_HANDLE; g_pool = VK_NULL_HANDLE;
    for (auto& t : g_tex) t.set = VK_NULL_HANDLE;
    g_dev = VK_NULL_HANDLE; g_ready = false; g_staticUploaded = false;
    g_cur = nullptr; g_curChain = nullptr; g_active = false;
}

void DrawSolidRect(int x, int y, int w, int h, float r, float g, float b, float a, int fb_w, int fb_h) {
    if (!Drawing() || fb_w <= 0 || fb_h <= 0 || w <= 0 || h <= 0) return;
    PushRect(kTexWhite, 0, (float)x, (float)y, (float)(x + w), (float)(y + h), 0.5f, 0.5f, 0.5f, 0.5f, r, g, b, a);
}

void DrawLine(float x0, float y0, float x1, float y1, float thickness,
              float r, float g, float b, float a, int fb_w, int fb_h) {
    if (!Drawing() || fb_w <= 0 || fb_h <= 0) return;
    float dx = x1 - x0, dy = y1 - y0;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;
    if (thickness < 1.0f) thickness = 1.0f;
    float hx = (-dy / len) * thickness * 0.5f;
    float hy = ( dx / len) * thickness * 0.5f;
    const float px[4] = { x0 + hx, x1 + hx, x0 - hx, x1 - hx };
    const float py[4] = { y0 + hy, y1 + hy, y0 - hy, y1 - hy };
    const float uv[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
    PushQuad(kTexWhite, 0, px, py, uv, uv, r, g, b, a);
}

void DrawFillQuad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3,
                  float r, float g, float b, float a, int fb_w, int fb_h) {
    if (!Drawing() || fb_w <= 0 || fb_h <= 0) return;
    const float px[4] = { x0, x1, x3, x2 };
    const float py[4] = { y0, y1, y3, y2 };
    const float uv[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
    PushQuad(kTexWhite, 0, px, py, uv, uv, r, g, b, a);
}

void DrawGlyph(int cell, float x, float y, float w, float h,
               float r, float g, float b, float a, int fb_w, int fb_h) {
    if (!Drawing() || fb_w <= 0 || fb_h <= 0 || w <= 0.0f || h <= 0.0f || g_atlas_w <= 0) return;
    const int count = rtx::marker::kGlyphLast - rtx::marker::kGlyphFirst + 1;
    if (cell < 0 || cell >= count) return;
    const int cols = rtx::marker::kGlyphCols;
    const int cw = rtx::marker::kGlyphCellW, chh = rtx::marker::kGlyphCellH;
    int col = cell % cols, row = cell / cols;
    float u0 = (float)(col * cw)        / (float)g_atlas_w;
    float v0 = (float)(row * chh)       / (float)g_atlas_h;
    float u1 = (float)(col * cw + cw)   / (float)g_atlas_w;
    float v1 = (float)(row * chh + chh) / (float)g_atlas_h;
    PushRect(kTexAtlas, 0, x, y, x + w, y + h, u0, v0, u1, v1, r, g, b, a);
}

void DrawLabel(const char* s, float cx, float cy, float text_px,
               float ar, float ag, float ab, float aa, int fb_w, int fb_h) {
    if (!Drawing() || !s || !*s || fb_w <= 0 || fb_h <= 0 || g_atlas_w <= 0) return;
    const int chh = rtx::marker::kGlyphCellH;
    if (text_px < 6.0f)  text_px = 6.0f;
    if (text_px > 40.0f) text_px = 40.0f;
    const float fontPx = (float)(chh - 13);
    const float scale  = text_px / fontPx;
    const float cellH  = (float)chh * scale;
    const float track  = 0.6f * scale;

    const char* lstart[4]; int llen[4]; int nl = 0;
    const char* st = s;
    for (const char* p = s;; ++p) {
        if (*p == '\n' || *p == '\0') {
            if (p > st && nl < 4) { lstart[nl] = st; llen[nl] = (int)(p - st); ++nl; }
            if (*p == '\0') break;
            st = p + 1;
        }
    }
    if (!nl) return;
    float lw[4] = {}; float maxW = 0.0f; bool any = false;
    for (int li = 0; li < nl; ++li) {
        float tw = RunWidth(lstart[li], llen[li], scale, track);
        if (tw >= 0.0f) any = true; else tw = 0.0f;
        lw[li] = tw;
        if (tw > maxW) maxW = tw;
    }
    if (!any) return;

    const float padX = 8.0f, padY = 5.0f;
    const float lineH = cellH * 0.92f;
    const float pillW = maxW + padX * 2.0f;
    const float pillH = cellH * 0.78f + lineH * (float)(nl - 1) + padY * 2.0f;
    const float pillX = cx - pillW * 0.5f;
    const float pillY = cy - pillH * 0.5f;
    const float rad   = 5.0f;
    RoundFill(pillX, pillY + 2.0f, pillW, pillH, rad, 0.0f, 0.0f, 0.0f, 0.45f);
    RoundFill(pillX - 1.0f, pillY - 1.0f, pillW + 2.0f, pillH + 2.0f, rad + 1.0f, ar, ag, ab, aa);
    RoundFill(pillX, pillY, pillW, pillH, rad, 0.043f, 0.051f, 0.071f, 0.90f);

    float yc = pillY + padY + cellH * 0.39f;
    for (int li = 0; li < nl; ++li) {
        GlyphRun(lstart[li], llen[li], cx - lw[li] * 0.5f, yc - cellH * 0.5f, scale, cellH, track,
                 0.910f, 0.929f, 0.957f, 1.0f);
        yc += lineH;
    }
}

void DrawPlainText(const char* s, float x, float y, float text_px, int align,
                   float r, float g, float b, float a, int fb_w, int fb_h) {
    if (!Drawing() || !s || !*s || fb_w <= 0 || fb_h <= 0 || g_atlas_w <= 0) return;
    const int chh = rtx::marker::kGlyphCellH;
    if (text_px < 6.0f)  text_px = 6.0f;
    if (text_px > 40.0f) text_px = 40.0f;
    const float fontPx = (float)(chh - 13);
    const float scale  = text_px / fontPx;
    const float cellH  = (float)chh * scale;
    const float track  = 0.6f * scale;
    int len = 0;
    while (s[len] && s[len] != '\n') ++len;
    float tw = RunWidth(s, len, scale, track);
    if (tw < 0.0f) return;
    float penX = (align == 1) ? x - tw * 0.5f : (align == 2) ? x - tw : x;
    GlyphRun(s, len, penX, y - cellH * 0.5f, scale, cellH, track, r, g, b, a);
}

void DrawRoundRect(float x, float y, float w, float h, float rad,
                   float r, float g, float b, float a, int fb_w, int fb_h) {
    if (!Drawing() || fb_w <= 0 || fb_h <= 0) return;
    RoundFill(x, y, w, h, rad, r, g, b, a);
}

void UploadUiLayer(const void* bgra, int w, int h, int stride, int dx, int dy, int dw, int dh) {
    if (!Drawing() || !bgra || w <= 0 || h <= 0) return;
    if (stride <= 0) stride = w * 4;
    Texture& t = g_tex[kTexUi];
    if (!t.img || t.w != (std::uint32_t)w || t.h != (std::uint32_t)h) {
        WaitAll();
        if (!CreateTexture(t, VK_FORMAT_B8G8R8A8_UNORM, (std::uint32_t)w, (std::uint32_t)h, false)) return;
        dx = 0; dy = 0; dw = w; dh = h;
    }
    if (dx < 0) { dw += dx; dx = 0; }
    if (dy < 0) { dh += dy; dy = 0; }
    if (dx + dw > w) dw = w - dx;
    if (dy + dh > h) dh = h - dy;
    if (dw <= 0 || dh <= 0) return;
    const std::uint8_t* p = static_cast<const std::uint8_t*>(bgra) + (std::size_t)dy * (std::size_t)stride + (std::size_t)dx * 4u;
    QueueUpload(kTexUi, p, (std::uint32_t)dx, (std::uint32_t)dy, (std::uint32_t)dw, (std::uint32_t)dh, 4, (std::uint32_t)stride);
}

void DrawUiLayer(int dst_x, int dst_y, int fb_w, int fb_h) {
    if (!Drawing() || fb_w <= 0 || fb_h <= 0) return;
    Texture& t = g_tex[kTexUi];
    if (!t.img) return;
    PushRect(kTexUi, 0, (float)dst_x, (float)dst_y, (float)(dst_x + (int)t.w), (float)(dst_y + (int)t.h),
             0.f, 0.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f);
}

void UploadHud(const void* rgba, int w, int h) {
    if (!Drawing() || !rgba || w <= 0 || h <= 0) return;
    Texture& t = g_tex[kTexHud];
    if (!t.img || t.w != (std::uint32_t)w || t.h != (std::uint32_t)h) {
        WaitAll();
        if (!CreateTexture(t, VK_FORMAT_R8G8B8A8_UNORM, (std::uint32_t)w, (std::uint32_t)h, false)) return;
    }
    QueueUpload(kTexHud, rgba, 0, 0, (std::uint32_t)w, (std::uint32_t)h, 4, (std::uint32_t)w * 4u);
}

void DrawHud(int dst_x, int dst_y, int dst_w, int dst_h, int fb_w, int fb_h) {
    if (!Drawing() || dst_w <= 0 || dst_h <= 0 || fb_w <= 0 || fb_h <= 0) return;
    if (!g_tex[kTexHud].img) return;
    PushRect(kTexHud, 1, (float)dst_x, (float)dst_y, (float)(dst_x + dst_w), (float)(dst_y + dst_h),
             0.f, 0.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f);
}

}  // namespace rtx::vkcomposite
