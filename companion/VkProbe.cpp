// Draw-stream hooks (see VkProbe.h). Recording threads keep their own pass state; the pass table
// rotates through three frame slots so timestamp results can be read two presents later.

#include "VkProbe.h"
#include "GpuTimeShare.h"

#include <windows.h>
#include <tlhelp32.h>
#include <detours.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rtx::vkprobe {
namespace {

using LogFn = void (*)(const char*, ...);
LogFn g_log = nullptr;
void (*g_imageDestroyed)(VkImage) = nullptr;
bool  g_attached = false;
VkDevice g_dev = VK_NULL_HANDLE;
float g_tsPeriod = 1.0f;
std::atomic<bool>     g_hide{ false };
std::atomic<bool>     g_timing{ true };
std::atomic<bool>     g_probeNext{ false };
std::atomic<unsigned> g_frame{ 0 };
std::atomic<unsigned> g_dispatchOutside{ 0 };
std::atomic<unsigned> g_targetW{ 0 }, g_targetH{ 0 };

PFN_vkCmdBeginRenderPass         rBeginRP = nullptr;
PFN_vkCmdBeginRenderPass2        rBeginRP2 = nullptr;
PFN_vkCmdBeginRendering          rBeginRendering = nullptr;
PFN_vkCmdEndRenderPass           rEndRP = nullptr;
PFN_vkCmdEndRenderPass2          rEndRP2 = nullptr;
PFN_vkCmdEndRendering            rEndRendering = nullptr;
PFN_vkCmdDraw                    rDraw = nullptr;
PFN_vkCmdDrawIndexed             rDrawIndexed = nullptr;
PFN_vkCmdDrawIndirect            rDrawIndirect = nullptr;
PFN_vkCmdDrawIndexedIndirect     rDrawIndexedIndirect = nullptr;
PFN_vkCmdDrawIndirectCount       rDrawIndirectCount = nullptr;
PFN_vkCmdDrawIndexedIndirectCount rDrawIndexedIndirectCount = nullptr;
PFN_vkCmdDispatch                rDispatch = nullptr;
PFN_vkCmdDispatchIndirect        rDispatchIndirect = nullptr;
PFN_vkCmdBindPipeline            rBindPipeline = nullptr;
PFN_vkCreateGraphicsPipelines    rCreateGraphicsPipelines = nullptr;
PFN_vkDestroyPipeline            rDestroyPipeline = nullptr;
PFN_vkCreateRenderPass           rCreateRenderPass = nullptr;
PFN_vkCreateRenderPass2          rCreateRenderPass2 = nullptr;
PFN_vkCreateFramebuffer          rCreateFramebuffer = nullptr;
PFN_vkCreateImageView            rCreateImageView = nullptr;
PFN_vkCreateImage                rCreateImage = nullptr;
PFN_vkDestroyImage               rDestroyImage = nullptr;
PFN_vkCreateBuffer               rCreateBuffer = nullptr;
PFN_vkCreateQueryPool            fCreateQueryPool = nullptr;
PFN_vkDestroyQueryPool           fDestroyQueryPool = nullptr;
PFN_vkGetQueryPoolResults        fGetQueryPoolResults = nullptr;
PFN_vkCmdResetQueryPool          fCmdResetQueryPool = nullptr;
PFN_vkCmdWriteTimestamp          fCmdWriteTimestamp = nullptr;
PFN_vkResetQueryPool             fResetQueryPool = nullptr;

struct ViewInfo { VkImage image; VkFormat fmt; VkImageAspectFlags aspect; };
struct RpInfo   { std::vector<VkFormat> fmts; std::vector<VkAttachmentLoadOp> loads; std::vector<VkImageLayout> finals; };
struct FbInfo   { VkRenderPass rp; std::uint32_t w, h; std::vector<VkImageView> views; };

std::mutex g_mapMu;
std::unordered_map<VkImageView, ViewInfo>   g_views;
std::unordered_map<VkRenderPass, RpInfo>    g_rps;
std::unordered_map<VkFramebuffer, FbInfo>   g_fbs;
std::unordered_set<VkPipeline>              g_depthPipes;
std::unordered_set<VkPipeline>              g_dynDepthPipes;

struct Pass {
    char desc[rtx::gputime::kDescMax];
    unsigned draws, indirect, dispatch, skipped;
    VkImage depthImg; VkFormat depthFmt; VkImageLayout depthFinal;
    std::uint32_t w, h;
    int query;      // pair index in the slot's pool, -1 = none
};
constexpr int kSlots = 3;
constexpr unsigned kMaxPairs = 256;
std::mutex        g_passMu;
std::vector<Pass> g_slotPasses[kSlots];
VkQueryPool       g_pools[kSlots] = {};
std::atomic<int>  g_slot{ 0 };
std::atomic<unsigned> g_pairs[kSlots] = {};
std::atomic<bool>     g_poolReady[kSlots] = {};   // reset on the GPU at least once
ULONGLONG         g_lastPresentMs = 0;
unsigned          g_frameUs = 0;

VkImage       g_sceneImg = VK_NULL_HANDLE;
VkFormat      g_sceneFmt = VK_FORMAT_UNDEFINED;
VkImageLayout g_sceneLayout = VK_IMAGE_LAYOUT_UNDEFINED;

struct ResStat { std::uint32_t key[4]; unsigned count; };
std::vector<ResStat> g_images, g_buffers;
std::atomic<unsigned> g_imageTotal{ 0 }, g_bufferTotal{ 0 };

rtx::gputime::Share* g_share = nullptr;
HANDLE               g_shareMap = nullptr;

struct ThreadState {
    bool inPass = false;
    int  pass = -1;
    int  slot = 0;
    bool skip = false;
    unsigned draws = 0, indirect = 0, dispatch = 0, skipped = 0;
};
thread_local ThreadState t;

const char* FmtName(VkFormat f) {
    switch ((int)f) {
        case 9: return "R8_UNORM"; case 16: return "R8G8_UNORM"; case 37: return "R8G8B8A8_UNORM"; case 43: return "R8G8B8A8_SRGB";
        case 44: return "B8G8R8A8_UNORM"; case 50: return "B8G8R8A8_SRGB"; case 64: return "A2B10G10R10_UNORM";
        case 76: return "R16_SFLOAT"; case 83: return "R16G16_SFLOAT"; case 91: return "R16G16B16A16_UNORM"; case 97: return "R16G16B16A16_SFLOAT";
        case 100: return "R32G32_SFLOAT"; case 103: return "R32_SFLOAT"; case 109: return "R32G32B32A32_SFLOAT"; case 122: return "B10G11R11_UFLOAT";
        case 124: return "D16_UNORM"; case 126: return "D32_SFLOAT"; case 127: return "S8_UINT"; case 129: return "D24_UNORM_S8_UINT"; case 130: return "D32_SFLOAT_S8_UINT";
        case 98: return "R32_UINT"; case 74: return "R16_UINT"; case 13: return "R8_UINT"; case 4: return "R4G4B4A4";
        case 131: return "BC1_RGB_UNORM"; case 133: return "BC1_RGBA_UNORM"; case 137: return "BC3_UNORM"; case 139: return "BC4_UNORM"; case 141: return "BC5_UNORM"; case 145: return "BC7_UNORM"; case 146: return "BC7_SRGB";
        default: return nullptr;
    }
}
void AppendFmt(char* buf, size_t cap, VkFormat f) {
    size_t n = std::strlen(buf);
    const char* nm = FmtName(f);
    if (nm) std::snprintf(buf + n, cap - n, "%s", nm);
    else    std::snprintf(buf + n, cap - n, "fmt%d", (int)f);
}
void Cat(char* buf, size_t cap, const char* s) { std::strncat(buf, s, cap - std::strlen(buf) - 1); }

inline bool IsDepth(VkFormat f) { return (int)f >= 124 && (int)f <= 130; }

int BeginPass(VkCommandBuffer cmd, const char* kind, std::uint32_t w, std::uint32_t h,
              const VkFormat* colors, const VkAttachmentLoadOp* loads, std::uint32_t ncolor,
              VkFormat depth, VkAttachmentLoadOp depthLoad, VkImage depthImg, VkImageLayout depthFinal) {
    Pass p{};
    p.depthImg = depthImg; p.depthFmt = depth; p.depthFinal = depthFinal; p.w = w; p.h = h; p.query = -1;
    std::snprintf(p.desc, sizeof(p.desc), "%s %ux%u [", kind, w, h);
    for (std::uint32_t i = 0; i < ncolor; ++i) {
        if (i) Cat(p.desc, sizeof(p.desc), ",");
        AppendFmt(p.desc, sizeof(p.desc), colors[i]);
        if (loads && loads[i] == VK_ATTACHMENT_LOAD_OP_CLEAR) Cat(p.desc, sizeof(p.desc), "*");
    }
    Cat(p.desc, sizeof(p.desc), "]");
    if (depth != VK_FORMAT_UNDEFINED) {
        Cat(p.desc, sizeof(p.desc), " D[");
        AppendFmt(p.desc, sizeof(p.desc), depth);
        if (depthLoad == VK_ATTACHMENT_LOAD_OP_CLEAR) Cat(p.desc, sizeof(p.desc), "*");
        Cat(p.desc, sizeof(p.desc), "]");
    }
    const int slot = g_slot.load(std::memory_order_relaxed);
    t.slot = slot;
    if (g_pools[slot] && fCmdWriteTimestamp && g_timing.load(std::memory_order_relaxed) && g_poolReady[slot].load(std::memory_order_relaxed)) {
        unsigned pair = g_pairs[slot].fetch_add(1, std::memory_order_relaxed);
        if (pair < kMaxPairs) {
            p.query = (int)pair;
            fCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_pools[slot], pair * 2);
        }
    }
    std::lock_guard<std::mutex> lk(g_passMu);
    auto& passes = g_slotPasses[slot];
    if (passes.size() >= 256) return -1;
    passes.push_back(p);
    return (int)passes.size() - 1;
}

// Returns the query pair to close, or -1. The end timestamp is written by the caller after the
// pass instance has ended: inside a pass executing secondary command buffers it would be illegal.
int EndPass() {
    int query = -1;
    if (t.pass >= 0) {
        std::lock_guard<std::mutex> lk(g_passMu);
        auto& passes = g_slotPasses[t.slot];
        if ((size_t)t.pass < passes.size()) {
            Pass& p = passes[(size_t)t.pass];
            p.draws += t.draws; p.indirect += t.indirect; p.dispatch += t.dispatch; p.skipped += t.skipped;
            query = p.query;
        }
    }
    t.inPass = false; t.pass = -1; t.draws = t.indirect = t.dispatch = t.skipped = 0;
    return query;
}
void CloseQuery(VkCommandBuffer cmd, int slot, int query) {
    if (query >= 0 && g_pools[slot] && fCmdWriteTimestamp)
        fCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_pools[slot], (std::uint32_t)query * 2 + 1);
}

void FromRenderPass(const VkRenderPassBeginInfo* info, VkFormat* colors, VkAttachmentLoadOp* loads, std::uint32_t& nc,
                    VkFormat& depth, VkAttachmentLoadOp& dload, VkImage& depthImg, VkImageLayout& depthFinal,
                    std::uint32_t& w, std::uint32_t& h) {
    nc = 0; depth = VK_FORMAT_UNDEFINED; dload = VK_ATTACHMENT_LOAD_OP_DONT_CARE; depthImg = VK_NULL_HANDLE; depthFinal = VK_IMAGE_LAYOUT_UNDEFINED;
    w = info->renderArea.extent.width; h = info->renderArea.extent.height;
    std::lock_guard<std::mutex> lk(g_mapMu);
    auto rp = g_rps.find(info->renderPass);
    auto fb = g_fbs.find(info->framebuffer);
    if (rp == g_rps.end()) return;
    for (size_t i = 0; i < rp->second.fmts.size(); ++i) {
        VkFormat f = rp->second.fmts[i];
        if (IsDepth(f)) {
            depth = f; dload = rp->second.loads[i]; depthFinal = rp->second.finals[i];
            if (fb != g_fbs.end() && i < fb->second.views.size()) {
                auto v = g_views.find(fb->second.views[i]);
                if (v != g_views.end()) depthImg = v->second.image;
            }
        } else if (nc < 8) { colors[nc] = f; loads[nc] = rp->second.loads[i]; ++nc; }
    }
    if (fb != g_fbs.end() && w == 0) { w = fb->second.w; h = fb->second.h; }
}

void VKAPI_CALL HookBeginRP(VkCommandBuffer cmd, const VkRenderPassBeginInfo* info, VkSubpassContents contents) {
    if (info) {
        VkFormat colors[8]; VkAttachmentLoadOp loads[8]; std::uint32_t nc, w, h;
        VkFormat depth; VkAttachmentLoadOp dload; VkImage dimg; VkImageLayout dfinal;
        FromRenderPass(info, colors, loads, nc, depth, dload, dimg, dfinal, w, h);
        t.pass = BeginPass(cmd, "renderpass", w, h, colors, loads, nc, depth, dload, dimg, dfinal);
        t.inPass = true;
    }
    rBeginRP(cmd, info, contents);
}
void VKAPI_CALL HookBeginRP2(VkCommandBuffer cmd, const VkRenderPassBeginInfo* info, const VkSubpassBeginInfo* sb) {
    if (info) {
        VkFormat colors[8]; VkAttachmentLoadOp loads[8]; std::uint32_t nc, w, h;
        VkFormat depth; VkAttachmentLoadOp dload; VkImage dimg; VkImageLayout dfinal;
        FromRenderPass(info, colors, loads, nc, depth, dload, dimg, dfinal, w, h);
        t.pass = BeginPass(cmd, "renderpass2", w, h, colors, loads, nc, depth, dload, dimg, dfinal);
        t.inPass = true;
    }
    rBeginRP2(cmd, info, sb);
}
void VKAPI_CALL HookBeginRendering(VkCommandBuffer cmd, const VkRenderingInfo* info) {
    if (info) {
        VkFormat colors[8]; VkAttachmentLoadOp loads[8]; std::uint32_t nc = 0;
        VkFormat depth = VK_FORMAT_UNDEFINED; VkAttachmentLoadOp dload = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        VkImage dimg = VK_NULL_HANDLE; VkImageLayout dfinal = VK_IMAGE_LAYOUT_UNDEFINED;
        {
            std::lock_guard<std::mutex> lk(g_mapMu);
            for (std::uint32_t i = 0; i < info->colorAttachmentCount && nc < 8; ++i) {
                auto v = g_views.find(info->pColorAttachments[i].imageView);
                colors[nc] = v != g_views.end() ? v->second.fmt : VK_FORMAT_UNDEFINED;
                loads[nc] = info->pColorAttachments[i].loadOp; ++nc;
            }
            if (info->pDepthAttachment && info->pDepthAttachment->imageView) {
                auto v = g_views.find(info->pDepthAttachment->imageView);
                depth = v != g_views.end() ? v->second.fmt : VK_FORMAT_D32_SFLOAT;
                if (v != g_views.end()) dimg = v->second.image;
                dload = info->pDepthAttachment->loadOp;
                dfinal = info->pDepthAttachment->imageLayout;
            }
        }
        t.pass = BeginPass(cmd, "rendering", info->renderArea.extent.width, info->renderArea.extent.height, colors, loads, nc, depth, dload, dimg, dfinal);
        t.inPass = true;
    }
    rBeginRendering(cmd, info);
}
void VKAPI_CALL HookEndRP(VkCommandBuffer cmd) { int slot = t.slot; int q = EndPass(); rEndRP(cmd); CloseQuery(cmd, slot, q); }
void VKAPI_CALL HookEndRP2(VkCommandBuffer cmd, const VkSubpassEndInfo* e) { int slot = t.slot; int q = EndPass(); rEndRP2(cmd, e); CloseQuery(cmd, slot, q); }
void VKAPI_CALL HookEndRendering(VkCommandBuffer cmd) { int slot = t.slot; int q = EndPass(); rEndRendering(cmd); CloseQuery(cmd, slot, q); }

void VKAPI_CALL HookBindPipeline(VkCommandBuffer cmd, VkPipelineBindPoint bp, VkPipeline pipe) {
    if (bp == VK_PIPELINE_BIND_POINT_GRAPHICS) {
        bool depth = false;
        if (g_hide.load(std::memory_order_relaxed)) {
            std::lock_guard<std::mutex> lk(g_mapMu);
            depth = g_depthPipes.count(pipe) != 0;
        }
        t.skip = depth;
    }
    rBindPipeline(cmd, bp, pipe);
}

#define RTX_DRAW_GUARD() do { if (t.skip) { ++t.skipped; return; } ++t.draws; } while (0)
void VKAPI_CALL HookDraw(VkCommandBuffer c, std::uint32_t a, std::uint32_t b, std::uint32_t d, std::uint32_t e) { RTX_DRAW_GUARD(); rDraw(c, a, b, d, e); }
void VKAPI_CALL HookDrawIndexed(VkCommandBuffer c, std::uint32_t a, std::uint32_t b, std::uint32_t d, std::int32_t e, std::uint32_t f) { RTX_DRAW_GUARD(); rDrawIndexed(c, a, b, d, e, f); }
void VKAPI_CALL HookDrawIndirect(VkCommandBuffer c, VkBuffer b, VkDeviceSize o, std::uint32_t n, std::uint32_t s) { if (t.skip) { ++t.skipped; return; } ++t.indirect; rDrawIndirect(c, b, o, n, s); }
void VKAPI_CALL HookDrawIndexedIndirect(VkCommandBuffer c, VkBuffer b, VkDeviceSize o, std::uint32_t n, std::uint32_t s) { if (t.skip) { ++t.skipped; return; } ++t.indirect; rDrawIndexedIndirect(c, b, o, n, s); }
void VKAPI_CALL HookDrawIndirectCount(VkCommandBuffer c, VkBuffer b, VkDeviceSize o, VkBuffer cb, VkDeviceSize co, std::uint32_t m, std::uint32_t s) { if (t.skip) { ++t.skipped; return; } ++t.indirect; rDrawIndirectCount(c, b, o, cb, co, m, s); }
void VKAPI_CALL HookDrawIndexedIndirectCount(VkCommandBuffer c, VkBuffer b, VkDeviceSize o, VkBuffer cb, VkDeviceSize co, std::uint32_t m, std::uint32_t s) { if (t.skip) { ++t.skipped; return; } ++t.indirect; rDrawIndexedIndirectCount(c, b, o, cb, co, m, s); }
void VKAPI_CALL HookDispatch(VkCommandBuffer c, std::uint32_t x, std::uint32_t y, std::uint32_t z) { if (t.inPass) ++t.dispatch; else g_dispatchOutside.fetch_add(1, std::memory_order_relaxed); rDispatch(c, x, y, z); }
void VKAPI_CALL HookDispatchIndirect(VkCommandBuffer c, VkBuffer b, VkDeviceSize o) { if (t.inPass) ++t.dispatch; else g_dispatchOutside.fetch_add(1, std::memory_order_relaxed); rDispatchIndirect(c, b, o); }

VkResult VKAPI_CALL HookCreateGraphicsPipelines(VkDevice dev, VkPipelineCache cache, std::uint32_t n, const VkGraphicsPipelineCreateInfo* infos, const VkAllocationCallbacks* a, VkPipeline* out) {
    VkResult r = rCreateGraphicsPipelines(dev, cache, n, infos, a, out);
    if (r == VK_SUCCESS && infos && out) {
        std::lock_guard<std::mutex> lk(g_mapMu);
        for (std::uint32_t i = 0; i < n; ++i) {
            const auto& ci = infos[i];
            bool depth = ci.pDepthStencilState && (ci.pDepthStencilState->depthTestEnable || ci.pDepthStencilState->depthWriteEnable);
            bool dyn = false;
            if (ci.pDynamicState)
                for (std::uint32_t d = 0; d < ci.pDynamicState->dynamicStateCount; ++d)
                    if (ci.pDynamicState->pDynamicStates[d] == VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE) dyn = true;
            if (depth) g_depthPipes.insert(out[i]);
            if (dyn) g_dynDepthPipes.insert(out[i]);
        }
    }
    return r;
}
void VKAPI_CALL HookDestroyPipeline(VkDevice dev, VkPipeline p, const VkAllocationCallbacks* a) {
    { std::lock_guard<std::mutex> lk(g_mapMu); g_depthPipes.erase(p); g_dynDepthPipes.erase(p); }
    rDestroyPipeline(dev, p, a);
}
VkResult VKAPI_CALL HookCreateRenderPass(VkDevice dev, const VkRenderPassCreateInfo* ci, const VkAllocationCallbacks* a, VkRenderPass* out) {
    VkResult r = rCreateRenderPass(dev, ci, a, out);
    if (r == VK_SUCCESS && ci && out) {
        RpInfo info;
        for (std::uint32_t i = 0; i < ci->attachmentCount; ++i) {
            info.fmts.push_back(ci->pAttachments[i].format); info.loads.push_back(ci->pAttachments[i].loadOp);
            info.finals.push_back(ci->pAttachments[i].finalLayout);
        }
        std::lock_guard<std::mutex> lk(g_mapMu); g_rps[*out] = info;
    }
    return r;
}
VkResult VKAPI_CALL HookCreateRenderPass2(VkDevice dev, const VkRenderPassCreateInfo2* ci, const VkAllocationCallbacks* a, VkRenderPass* out) {
    VkResult r = rCreateRenderPass2(dev, ci, a, out);
    if (r == VK_SUCCESS && ci && out) {
        RpInfo info;
        for (std::uint32_t i = 0; i < ci->attachmentCount; ++i) {
            info.fmts.push_back(ci->pAttachments[i].format); info.loads.push_back(ci->pAttachments[i].loadOp);
            info.finals.push_back(ci->pAttachments[i].finalLayout);
        }
        std::lock_guard<std::mutex> lk(g_mapMu); g_rps[*out] = info;
    }
    return r;
}
VkResult VKAPI_CALL HookCreateFramebuffer(VkDevice dev, const VkFramebufferCreateInfo* ci, const VkAllocationCallbacks* a, VkFramebuffer* out) {
    VkResult r = rCreateFramebuffer(dev, ci, a, out);
    if (r == VK_SUCCESS && ci && out) {
        FbInfo info; info.rp = ci->renderPass; info.w = ci->width; info.h = ci->height;
        if (ci->pAttachments) for (std::uint32_t i = 0; i < ci->attachmentCount; ++i) info.views.push_back(ci->pAttachments[i]);
        std::lock_guard<std::mutex> lk(g_mapMu); g_fbs[*out] = info;
    }
    return r;
}
VkResult VKAPI_CALL HookCreateImageView(VkDevice dev, const VkImageViewCreateInfo* ci, const VkAllocationCallbacks* a, VkImageView* out) {
    VkResult r = rCreateImageView(dev, ci, a, out);
    if (r == VK_SUCCESS && ci && out) {
        std::lock_guard<std::mutex> lk(g_mapMu);
        g_views[*out] = { ci->image, ci->format, ci->subresourceRange.aspectMask };
        if (g_views.size() > 65536) g_views.clear();
    }
    return r;
}
void Tally(std::vector<ResStat>& v, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
    for (auto& s : v) if (s.key[0] == a && s.key[1] == b && s.key[2] == c && s.key[3] == d) { ++s.count; return; }
    if (v.size() < 64) v.push_back({ { a, b, c, d }, 1 });
}
VkResult VKAPI_CALL HookCreateImage(VkDevice dev, const VkImageCreateInfo* ci, const VkAllocationCallbacks* a, VkImage* out) {
    VkResult r = rCreateImage(dev, ci, a, out);
    if (r == VK_SUCCESS && ci) {
        g_imageTotal.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lk(g_mapMu);
        Tally(g_images, (std::uint32_t)ci->format, ci->usage, ci->extent.width, ci->extent.height);
    }
    return r;
}
void VKAPI_CALL HookDestroyImage(VkDevice dev, VkImage img, const VkAllocationCallbacks* a) {
    if (img) {
        {
            std::lock_guard<std::mutex> lk(g_passMu);
            if (g_sceneImg == img) { g_sceneImg = VK_NULL_HANDLE; g_sceneLayout = VK_IMAGE_LAYOUT_UNDEFINED; }
        }
        if (g_imageDestroyed) g_imageDestroyed(img);
    }
    rDestroyImage(dev, img, a);
}
VkResult VKAPI_CALL HookCreateBuffer(VkDevice dev, const VkBufferCreateInfo* ci, const VkAllocationCallbacks* a, VkBuffer* out) {
    VkResult r = rCreateBuffer(dev, ci, a, out);
    if (r == VK_SUCCESS && ci) {
        g_bufferTotal.fetch_add(1, std::memory_order_relaxed);
        std::uint32_t bucket = 0; VkDeviceSize s = ci->size; while (s > 1) { s >>= 1; ++bucket; }
        std::lock_guard<std::mutex> lk(g_mapMu);
        Tally(g_buffers, ci->usage, bucket, 0, 0);
    }
    return r;
}

struct ThreadSet { HANDLE h[512]; int n = 0; };
void UpdateAllThreads(ThreadSet& ts) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) { DetourUpdateThread(GetCurrentThread()); return; }
    THREADENTRY32 te{}; te.dwSize = sizeof(te);
    const DWORD pid = GetCurrentProcessId(), me = GetCurrentThreadId();
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == me || ts.n >= 512) continue;
            HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
            if (!h) continue;
            ts.h[ts.n++] = h;
            DetourUpdateThread(h);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
}
void CloseThreads(ThreadSet& ts) { for (int i = 0; i < ts.n; ++i) CloseHandle(ts.h[i]); ts.n = 0; }

struct HookDef { void** real; void* hook; const char* name; bool required; };
HookDef g_defs[] = {
    { (void**)&rBeginRP, (void*)HookBeginRP, "vkCmdBeginRenderPass", true },
    { (void**)&rBeginRP2, (void*)HookBeginRP2, "vkCmdBeginRenderPass2", false },
    { (void**)&rBeginRendering, (void*)HookBeginRendering, "vkCmdBeginRendering", false },
    { (void**)&rEndRP, (void*)HookEndRP, "vkCmdEndRenderPass", true },
    { (void**)&rEndRP2, (void*)HookEndRP2, "vkCmdEndRenderPass2", false },
    { (void**)&rEndRendering, (void*)HookEndRendering, "vkCmdEndRendering", false },
    { (void**)&rDraw, (void*)HookDraw, "vkCmdDraw", true },
    { (void**)&rDrawIndexed, (void*)HookDrawIndexed, "vkCmdDrawIndexed", true },
    { (void**)&rDrawIndirect, (void*)HookDrawIndirect, "vkCmdDrawIndirect", true },
    { (void**)&rDrawIndexedIndirect, (void*)HookDrawIndexedIndirect, "vkCmdDrawIndexedIndirect", true },
    { (void**)&rDrawIndirectCount, (void*)HookDrawIndirectCount, "vkCmdDrawIndirectCount", false },
    { (void**)&rDrawIndexedIndirectCount, (void*)HookDrawIndexedIndirectCount, "vkCmdDrawIndexedIndirectCount", false },
    { (void**)&rDispatch, (void*)HookDispatch, "vkCmdDispatch", true },
    { (void**)&rDispatchIndirect, (void*)HookDispatchIndirect, "vkCmdDispatchIndirect", true },
    { (void**)&rBindPipeline, (void*)HookBindPipeline, "vkCmdBindPipeline", true },
    { (void**)&rCreateGraphicsPipelines, (void*)HookCreateGraphicsPipelines, "vkCreateGraphicsPipelines", true },
    { (void**)&rDestroyPipeline, (void*)HookDestroyPipeline, "vkDestroyPipeline", true },
    { (void**)&rCreateRenderPass, (void*)HookCreateRenderPass, "vkCreateRenderPass", true },
    { (void**)&rCreateRenderPass2, (void*)HookCreateRenderPass2, "vkCreateRenderPass2", false },
    { (void**)&rCreateFramebuffer, (void*)HookCreateFramebuffer, "vkCreateFramebuffer", true },
    { (void**)&rCreateImageView, (void*)HookCreateImageView, "vkCreateImageView", true },
    { (void**)&rCreateImage, (void*)HookCreateImage, "vkCreateImage", true },
    { (void**)&rDestroyImage, (void*)HookDestroyImage, "vkDestroyImage", true },
    { (void**)&rCreateBuffer, (void*)HookCreateBuffer, "vkCreateBuffer", true },
};

void CreateShare() {
    if (g_share) return;
    wchar_t name[64];
    rtx::gputime::MakeSectionName(GetCurrentProcessId(), name);
    g_shareMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD)sizeof(rtx::gputime::Share), name);
    if (!g_shareMap) return;
    g_share = reinterpret_cast<rtx::gputime::Share*>(MapViewOfFile(g_shareMap, FILE_MAP_WRITE, 0, 0, sizeof(rtx::gputime::Share)));
    if (!g_share) { CloseHandle(g_shareMap); g_shareMap = nullptr; return; }
    g_share->version = rtx::gputime::kVersion; g_share->pid = GetCurrentProcessId();
    g_share->seq = 0; g_share->count = 0; g_share->frame = 0;
    g_share->magic = rtx::gputime::kMagic;
}

void PublishTimings(const std::vector<Pass>& passes, const std::uint64_t* results, bool haveResults, unsigned frame) {
    if (!g_share) return;
    std::uint32_t s = g_share->seq + 1;
    g_share->seq = s; MemoryBarrier();
    std::uint32_t n = 0;
    std::uint64_t first = ~0ull, last = 0;
    for (const auto& p : passes) {
        if (n >= rtx::gputime::kMaxPasses) break;
        rtx::gputime::Pass& o = g_share->passes[n];
        std::memcpy(o.desc, p.desc, sizeof(o.desc));
        o.draws = p.draws + p.indirect;
        o.us = 0;
        if (haveResults && p.query >= 0) {
            const std::uint64_t b = results[(size_t)p.query * 4 + 0], ba = results[(size_t)p.query * 4 + 1];
            const std::uint64_t e = results[(size_t)p.query * 4 + 2], ea = results[(size_t)p.query * 4 + 3];
            if (ba && ea && e >= b) {
                o.us = (std::uint32_t)((double)(e - b) * (double)g_tsPeriod / 1000.0);
                if (b < first) first = b;
                if (e > last) last = e;
            }
        }
        ++n;
    }
    g_share->count = n;
    g_share->frame = frame;
    g_share->frame_us = g_frameUs;
    g_share->total_us = (last > first) ? (std::uint32_t)((double)(last - first) * (double)g_tsPeriod / 1000.0) : 0;
    MemoryBarrier(); g_share->seq = s + 1;
}

}  // namespace

bool Attach(VkDevice dev, PFN_vkGetDeviceProcAddr gdpa, float timestampPeriodNs,
            void (*log)(const char*, ...), void (*imageDestroyed)(VkImage)) {
    if (g_attached) return true;
    g_log = log; g_imageDestroyed = imageDestroyed; g_dev = dev;
    g_tsPeriod = timestampPeriodNs > 0.f ? timestampPeriodNs : 1.0f;
    for (auto& d : g_defs) {
        *d.real = (void*)gdpa(dev, d.name);
        if (!*d.real && d.required) { if (g_log) g_log("probe: %s missing", d.name); return false; }
    }
    fCreateQueryPool     = (PFN_vkCreateQueryPool)gdpa(dev, "vkCreateQueryPool");
    fDestroyQueryPool    = (PFN_vkDestroyQueryPool)gdpa(dev, "vkDestroyQueryPool");
    fGetQueryPoolResults = (PFN_vkGetQueryPoolResults)gdpa(dev, "vkGetQueryPoolResults");
    fCmdResetQueryPool   = (PFN_vkCmdResetQueryPool)gdpa(dev, "vkCmdResetQueryPool");
    fCmdWriteTimestamp   = (PFN_vkCmdWriteTimestamp)gdpa(dev, "vkCmdWriteTimestamp");
    fResetQueryPool      = (PFN_vkResetQueryPool)gdpa(dev, "vkResetQueryPool");
    if (!fResetQueryPool) fResetQueryPool = (PFN_vkResetQueryPool)gdpa(dev, "vkResetQueryPoolEXT");
    if (fCreateQueryPool && fGetQueryPoolResults && fCmdResetQueryPool && fCmdWriteTimestamp) {
        for (int i = 0; i < kSlots; ++i) {
            VkQueryPoolCreateInfo qi{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
            qi.queryType = VK_QUERY_TYPE_TIMESTAMP; qi.queryCount = kMaxPairs * 2;
            if (fCreateQueryPool(dev, &qi, nullptr, &g_pools[i]) != VK_SUCCESS) { g_pools[i] = VK_NULL_HANDLE; break; }
        }
    }
    CreateShare();

    ThreadSet ts;
    DetourTransactionBegin();
    UpdateAllThreads(ts);
    for (auto& d : g_defs) if (*d.real) DetourAttach(d.real, d.hook);
    LONG rc = DetourTransactionCommit();
    CloseThreads(ts);
    if (rc != NO_ERROR) { if (g_log) g_log("probe: attach failed (%ld)", rc); return false; }
    g_attached = true;
    if (g_log) g_log("probe: draw hooks attached (dynamic rendering %s, renderpass2 %s, timestamps %s, period %.3f ns)",
                     rBeginRendering ? "yes" : "no", rBeginRP2 ? "yes" : "no", g_pools[0] ? "yes" : "no", (double)g_tsPeriod);
    return true;
}

void Detach() {
    if (!g_attached) return;
    ThreadSet ts;
    DetourTransactionBegin();
    UpdateAllThreads(ts);
    for (auto& d : g_defs) if (*d.real) DetourDetach(d.real, d.hook);
    DetourTransactionCommit();
    CloseThreads(ts);
    g_attached = false;
    for (int i = 0; i < kSlots; ++i) {
        if (g_pools[i] && fDestroyQueryPool) fDestroyQueryPool(g_dev, g_pools[i], nullptr);
        g_pools[i] = VK_NULL_HANDLE; g_poolReady[i].store(false);
    }
    std::lock_guard<std::mutex> lk(g_mapMu);
    g_views.clear(); g_rps.clear(); g_fbs.clear(); g_depthPipes.clear(); g_dynDepthPipes.clear();
    g_sceneImg = VK_NULL_HANDLE;
}

void SetTargetExtent(unsigned w, unsigned h) { g_targetW.store(w); g_targetH.store(h); }
void SetHideScene(bool on) { g_hide.store(on, std::memory_order_relaxed); }
bool HideSceneAvailable() { return g_attached; }
void RequestProbe() { g_probeNext.store(true); }

bool SceneDepth(VkImage* img, VkFormat* fmt, VkImageLayout* layout) {
    std::lock_guard<std::mutex> lk(g_passMu);
    if (!g_sceneImg) return false;
    *img = g_sceneImg; *fmt = g_sceneFmt; *layout = g_sceneLayout;
    return true;
}

// FrameBegin already advanced g_slot to the slot the next frame records into; its pool was read
// there, so reset it here, ahead of that frame's submissions in queue order.
void OnOverlayCmd(VkCommandBuffer cmd) {
    if (!fCmdResetQueryPool || !g_timing.load(std::memory_order_relaxed)) return;
    const int next = g_slot.load(std::memory_order_relaxed);
    if (g_pools[next]) {
        fCmdResetQueryPool(cmd, g_pools[next], 0, kMaxPairs * 2);
        g_pairs[next].store(0, std::memory_order_relaxed);
        g_poolReady[next].store(true, std::memory_order_relaxed);
    }
}

void SetTimingEnabled(bool on) { g_timing.store(on); }

// Present boundary: the frame just recorded becomes the current slot's completed list; the slot
// presented two frames ago has its timestamps read and published; recording moves to the next slot.
void FrameBegin() {
    const unsigned f = g_frame.fetch_add(1, std::memory_order_relaxed) + 1;
    const ULONGLONG now = GetTickCount64();
    g_frameUs = g_lastPresentMs ? (unsigned)((now - g_lastPresentMs) * 1000) : 0;
    g_lastPresentMs = now;
    const int cur = g_slot.load(std::memory_order_relaxed);
    const int done = (cur + 1) % kSlots;   // recorded two presents ago; read before its pool is reset
    const bool probe = g_probeNext.exchange(false) || f == 300 || f == 3000;

    std::vector<Pass> current, old;
    {
        std::lock_guard<std::mutex> lk(g_passMu);
        current = g_slotPasses[cur];
        old.swap(g_slotPasses[done]);
        // Scene depth: the client-size depth pass with the most draws in the frame just recorded.
        const unsigned tw = g_targetW.load(), th = g_targetH.load();
        const Pass* best = nullptr;
        for (const auto& p : current)
            if (p.depthImg && p.w == tw && p.h == th && (!best || p.draws + p.skipped > best->draws + best->skipped)) best = &p;
        if (best) { g_sceneImg = best->depthImg; g_sceneFmt = best->depthFmt; g_sceneLayout = best->depthFinal; }
        else { g_sceneImg = VK_NULL_HANDLE; g_sceneLayout = VK_IMAGE_LAYOUT_UNDEFINED; }
    }

    static std::uint64_t results[kMaxPairs * 4];
    bool have = false;
    if (g_pools[done] && fGetQueryPoolResults && !old.empty()) {
        unsigned pairs = g_pairs[done].load(std::memory_order_relaxed);
        if (pairs > kMaxPairs) pairs = kMaxPairs;
        if (pairs) {
            VkResult r = fGetQueryPoolResults(g_dev, g_pools[done], 0, pairs * 2, sizeof(std::uint64_t) * 4 * pairs, results,
                                              sizeof(std::uint64_t) * 2, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
            have = (r == VK_SUCCESS || r == VK_NOT_READY);
        }
    }
    PublishTimings(old, results, have, f >= 2 ? f - 2 : 0);
    g_slot.store((cur + 1) % kSlots, std::memory_order_relaxed);

    unsigned outside = g_dispatchOutside.exchange(0);
    if (!probe || !g_log) return;
    size_t depthPipes, dynPipes, views, rps;
    std::vector<ResStat> images, buffers;
    {
        std::lock_guard<std::mutex> lk(g_mapMu);
        depthPipes = g_depthPipes.size(); dynPipes = g_dynDepthPipes.size(); views = g_views.size(); rps = g_rps.size();
        images = g_images; buffers = g_buffers;
    }
    g_log("probe frame %u: %zu passes, %u compute dispatches outside passes, %zu depth pipelines (%zu dynamic-depth), %zu render passes, %zu views, scene depth %p",
          f, current.size(), outside, depthPipes, dynPipes, rps, views, (void*)g_sceneImg);
    for (size_t i = 0; i < current.size(); ++i)
        g_log("  pass %2zu: %s draws %u indirect %u dispatch %u skipped %u", i, current[i].desc, current[i].draws, current[i].indirect, current[i].dispatch, current[i].skipped);
    g_log("  images created so far %u (distinct format/usage/size %zu):", g_imageTotal.load(), images.size());
    for (const auto& s : images) {
        char fb[48] = {}; AppendFmt(fb, sizeof(fb), (VkFormat)s.key[0]);
        g_log("    %5u x %s usage 0x%x %ux%u", s.count, fb, s.key[1], s.key[2], s.key[3]);
    }
    g_log("  buffers created so far %u (distinct usage/size class %zu)", g_bufferTotal.load(), buffers.size());
}

}  // namespace rtx::vkprobe
