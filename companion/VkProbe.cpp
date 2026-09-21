// Draw-stream hooks (see VkProbe.h). Recording threads keep their own pass state; the pass table
// rotates through three frame slots so timestamp results can be read two presents later.

#include "VkProbe.h"
#include "GpuTimeShare.h"

#include <windows.h>
#include <tlhelp32.h>
#include <detours.h>
#include <algorithm>
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
std::atomic<bool>     g_inFrameTrial{ false };
std::atomic<bool>     g_timing{ true };
std::atomic<bool>     g_probeNext{ false };
std::atomic<unsigned> g_frame{ 0 };
std::atomic<unsigned> g_worldProbeAt{ 0 };   // frame of the one probe taken with the game world on screen; 0 = not planned yet
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
PFN_vkCmdClearAttachments        fCmdClearAttachments = nullptr;
PFN_vkCmdBindDescriptorSets      rBindDescriptorSets = nullptr;
PFN_vkCmdBindVertexBuffers       rBindVertexBuffers = nullptr;
PFN_vkCmdBindIndexBuffer         rBindIndexBuffer = nullptr;
PFN_vkCmdSetViewport             rSetViewport = nullptr;
PFN_vkCmdSetScissor              rSetScissor = nullptr;
PFN_vkBeginCommandBuffer         rBeginCommandBuffer = nullptr;
bool (*g_sceneRecorder)(VkCommandBuffer, VkRenderPass, VkFramebuffer, std::uint32_t, std::uint32_t, unsigned) = nullptr;
PassRecorder g_passRecorder{};
PFN_vkCmdPushConstants           rPushConstants = nullptr;
// Where in the frame the markers go: 1 inside the interface pass, 0 into the finished scene ahead of it.
// `look` is handed to the recorder.
std::atomic<int>      g_where{ 1 };
std::atomic<unsigned> g_look{ 0 };
std::atomic<unsigned> g_statArmed{ 0 }, g_statDrew{ 0 };   // interface passes found, and those our markers went into
PFN_vkDestroyFramebuffer         fDestroyFramebuffer = nullptr;
PFN_vkDestroyRenderPass          fDestroyRenderPass = nullptr;
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
// Depth state of every pipeline, for the frame probe: bit 0 test, bit 1 write, bits 2..4 the compare op.
std::unordered_map<VkPipeline, std::uint8_t> g_pipeDepth;
std::unordered_map<VkImage, ImageInfo>      g_imageInfo;

struct Pass {
    char desc[rtx::gputime::kDescMax];
    unsigned draws, indirect, dispatch, skipped;
    unsigned depthTest, depthWrite, depthOff, depthOps;   // probe frames only: draws by depth state, compare ops seen (bit per op)
    unsigned first[8], firstCount;                       // probe frames only: vertex or index count of the pass's first draws
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
    std::uint8_t depth = 0;             // depth state of the bound pipeline, while a probe frame is near
    unsigned depthTest = 0, depthWrite = 0, depthOff = 0, depthOps = 0;
    unsigned first[8] = {}, firstCount = 0;
    // the pass being recorded, and the one that ended before it on this thread
    VkFramebuffer curFb = VK_NULL_HANDLE; VkRenderPass curRp = VK_NULL_HANDLE;
    VkFormat curColour = VK_FORMAT_UNDEFINED; std::uint32_t curColours = 0, curW = 0, curH = 0; bool curDepth = false;
    // the interface pass while it is open and nothing of ours is in it yet; the scene depth while it is lent out
    bool inject = false, borrowed = false; VkCommandBuffer lentIn = VK_NULL_HANDLE;
    VkFramebuffer prevFb = VK_NULL_HANDLE; VkRenderPass prevRp = VK_NULL_HANDLE;
    VkFormat prevColour = VK_FORMAT_UNDEFINED; std::uint32_t prevColours = 0, prevW = 0, prevH = 0, prevDraws = 0; bool prevDepth = false;
};
thread_local ThreadState t;

// What the game has bound in the command buffer this thread is recording. Drawing of our own in
// the middle of that command buffer replaces all of it, and the game does not bind again what it
// believes is still bound, so afterwards it is put back exactly.
struct DescCall { VkPipelineLayout layout; std::uint32_t first, count, dynCount; VkDescriptorSet sets[8]; std::uint32_t dyn[32]; };
struct PushCall { VkPipelineLayout layout; VkShaderStageFlags stages; std::uint32_t offset, size; unsigned char data[256]; };
struct Bound {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    std::vector<DescCall> sets;
    std::vector<PushCall> pushes;
    VkBuffer vb[8] = {}; VkDeviceSize vbOff[8] = {}; std::uint32_t vbMask = 0;
    VkBuffer ib = VK_NULL_HANDLE; VkDeviceSize ibOff = 0; VkIndexType ibType = VK_INDEX_TYPE_UINT16;
    VkViewport viewport{}; bool hasViewport = false;
    VkRect2D scissor{}; bool hasScissor = false;
    bool ours = false;                  // our own recording is passing through the hooks: not the game's state
};
thread_local Bound tb;
inline Bound* GameState(VkCommandBuffer cmd) {
    if (tb.ours) return nullptr;
    if (tb.cmd != cmd) { tb = Bound{}; tb.cmd = cmd; }
    return &tb;
}

// The probe reports the frames that end at 300 and 3000 and any frame asked for; the depth
// tally costs a map lookup per pipeline bind, so it only runs around those.
bool ProbeNear() {
    const unsigned f = g_frame.load(std::memory_order_relaxed);
    const unsigned world = g_worldProbeAt.load(std::memory_order_relaxed);
    return g_probeNext.load(std::memory_order_relaxed) || (f >= 297 && f <= 300) || (f >= 2997 && f <= 3000) ||
           (world && f + 3 >= world && f <= world);
}
void TallyDepth(unsigned size = 0) {
    if (!ProbeNear()) return;
    if (t.firstCount < 8) t.first[t.firstCount++] = size;
    if (t.depth & 1) ++t.depthTest;
    if (t.depth & 2) ++t.depthWrite;
    if (!(t.depth & 3)) ++t.depthOff;
    if (t.depth & 1) t.depthOps |= 1u << ((t.depth >> 2) & 7);
}

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
        if (loads && loads[i] == VK_ATTACHMENT_LOAD_OP_DONT_CARE) Cat(p.desc, sizeof(p.desc), "~");   // starts from nothing: its first draw fills it
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
            p.depthTest += t.depthTest; p.depthWrite += t.depthWrite; p.depthOff += t.depthOff; p.depthOps |= t.depthOps;
            for (unsigned i = 0; i < t.firstCount && p.firstCount < 8; ++i) p.first[p.firstCount++] = t.first[i];
            query = p.query;
        }
    }
    t.prevFb = t.curFb; t.prevRp = t.curRp; t.prevColour = t.curColour; t.prevColours = t.curColours;
    t.prevW = t.curW; t.prevH = t.curH; t.prevDepth = t.curDepth; t.prevDraws = t.draws + t.indirect;
    t.curFb = VK_NULL_HANDLE; t.curRp = VK_NULL_HANDLE;
    t.inPass = false; t.pass = -1; t.draws = t.indirect = t.dispatch = t.skipped = 0;
    t.depthTest = t.depthWrite = t.depthOff = t.depthOps = 0; t.firstCount = 0;
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

// ---- Drawing inside the game's frame -------------------------------------------------------
// The frame ends like this: a chain of full screen passes finishes the scene in a 16 bit float
// image, then a pass on an 8 bit image that keeps its contents copies that scene in and paints the
// whole interface over it. Between the two, the finished scene is complete and nothing of the
// interface exists yet: what is put into the scene image there ends up under every window,
// tooltip and menu of the game, with no part of the interface to cut around.
// It is done in a render pass of our own, between two of the game's, and with a clear command:
// no pipeline, descriptor set or buffer is bound, so nothing the game has bound is disturbed.
void RestoreGameState(VkCommandBuffer cmd) {
    if (tb.cmd != cmd) return;
    if (tb.pipeline && rBindPipeline) rBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, tb.pipeline);
    if (rBindDescriptorSets)
        for (const auto& c : tb.sets)
            rBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, c.layout, c.first, c.count, c.sets, c.dynCount, c.dynCount ? c.dyn : nullptr);
    if (rBindVertexBuffers)
        for (std::uint32_t i = 0; i < 8; ++i)
            if (tb.vbMask & (1u << i)) rBindVertexBuffers(cmd, i, 1, &tb.vb[i], &tb.vbOff[i]);
    if (tb.ib && rBindIndexBuffer) rBindIndexBuffer(cmd, tb.ib, tb.ibOff, tb.ibType);
    if (tb.hasViewport && rSetViewport) rSetViewport(cmd, 0, 1, &tb.viewport);
    if (tb.hasScissor && rSetScissor) rSetScissor(cmd, 0, 1, &tb.scissor);
    if (rPushConstants)
        for (const auto& c : tb.pushes) rPushConstants(cmd, c.layout, c.stages, c.offset, c.size, c.data);
}

void VKAPI_CALL HookPushConstants(VkCommandBuffer cmd, VkPipelineLayout layout, VkShaderStageFlags stages, std::uint32_t offset, std::uint32_t size, const void* values) {
    if (Bound* g = GameState(cmd)) {
        if (values && size && size <= 256) {
            // the same range again replaces the earlier call; the rest are replayed in the order they came
            for (size_t i = 0; i < g->pushes.size();) {
                const PushCall& o = g->pushes[i];
                if (o.stages == stages && o.offset >= offset && o.offset + o.size <= offset + size) g->pushes.erase(g->pushes.begin() + (std::ptrdiff_t)i); else ++i;
            }
            if (g->pushes.size() < 8) {
                PushCall c{}; c.layout = layout; c.stages = stages; c.offset = offset; c.size = size;
                std::memcpy(c.data, values, size);
                g->pushes.push_back(c);
            }
        } else {
            g->pushes.clear();
        }
    }
    rPushConstants(cmd, layout, stages, offset, size, values);
}

void VKAPI_CALL HookBindDescriptorSets(VkCommandBuffer cmd, VkPipelineBindPoint bp, VkPipelineLayout layout, std::uint32_t first, std::uint32_t count,
                                       const VkDescriptorSet* sets, std::uint32_t dynCount, const std::uint32_t* dyn) {
    Bound* g = bp == VK_PIPELINE_BIND_POINT_GRAPHICS ? GameState(cmd) : nullptr;
    if (g && sets && count && count <= 8 && dynCount <= 32) {
        // a call that covers an earlier one replaces it; the rest are replayed in the order they came
        for (size_t i = 0; i < g->sets.size();) {
            const DescCall& o = g->sets[i];
            if (o.first >= first && o.first + o.count <= first + count) g->sets.erase(g->sets.begin() + (std::ptrdiff_t)i); else ++i;
        }
        if (g->sets.size() < 16) {
            DescCall c{}; c.layout = layout; c.first = first; c.count = count; c.dynCount = dynCount;
            for (std::uint32_t i = 0; i < count; ++i) c.sets[i] = sets[i];
            for (std::uint32_t i = 0; i < dynCount; ++i) c.dyn[i] = dyn[i];
            g->sets.push_back(c);
        }
    } else if (g) {
        g->sets.clear();                 // more than this keeps: better to restore nothing than half
    }
    rBindDescriptorSets(cmd, bp, layout, first, count, sets, dynCount, dyn);
}
void VKAPI_CALL HookBindVertexBuffers(VkCommandBuffer cmd, std::uint32_t first, std::uint32_t count, const VkBuffer* bufs, const VkDeviceSize* offs) {
    if (Bound* g = GameState(cmd))
        for (std::uint32_t i = 0; bufs && offs && i < count && first + i < 8; ++i) { g->vb[first + i] = bufs[i]; g->vbOff[first + i] = offs[i]; g->vbMask |= 1u << (first + i); }
    rBindVertexBuffers(cmd, first, count, bufs, offs);
}
void VKAPI_CALL HookBindIndexBuffer(VkCommandBuffer cmd, VkBuffer buf, VkDeviceSize off, VkIndexType type) {
    if (Bound* g = GameState(cmd)) { g->ib = buf; g->ibOff = off; g->ibType = type; }
    rBindIndexBuffer(cmd, buf, off, type);
}
void VKAPI_CALL HookSetViewport(VkCommandBuffer cmd, std::uint32_t first, std::uint32_t count, const VkViewport* v) {
    if (Bound* g = GameState(cmd)) if (v && first == 0 && count) { g->viewport = v[0]; g->hasViewport = true; }
    rSetViewport(cmd, first, count, v);
}
void VKAPI_CALL HookSetScissor(VkCommandBuffer cmd, std::uint32_t first, std::uint32_t count, const VkRect2D* r) {
    if (Bound* g = GameState(cmd)) if (r && first == 0 && count) { g->scissor = r[0]; g->hasScissor = true; }
    rSetScissor(cmd, first, count, r);
}
VkResult VKAPI_CALL HookBeginCommandBuffer(VkCommandBuffer cmd, const VkCommandBufferBeginInfo* info) {
    if (tb.cmd == cmd && !tb.ours) tb = Bound{};   // recorded afresh: nothing is bound in it
    return rBeginCommandBuffer(cmd, info);
}

// on its own: a function with a guarded call may not also hold objects that need unwinding
bool CallRecorder(VkCommandBuffer cmd, VkRenderPass rp, VkFramebuffer fb, std::uint32_t w, std::uint32_t h) {
    __try { return g_sceneRecorder(cmd, rp, fb, w, h, g_look.load(std::memory_order_relaxed)); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallBorrow(VkCommandBuffer cmd, VkImage passDepth) {
    __try { return g_passRecorder.borrow(cmd, passDepth); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void CallGiveBack(VkCommandBuffer cmd) {
    __try { g_passRecorder.giveBack(cmd); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
bool CallPassRecord(VkCommandBuffer cmd, VkRenderPass rp, std::uint32_t w, std::uint32_t h, bool borrowed) {
    __try { return g_passRecorder.record(cmd, rp, w, h, borrowed, g_look.load(std::memory_order_relaxed)); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// The interface pass is about to begin: lend the scene depth out for its length.
void ArmInterfacePass(VkCommandBuffer cmd, VkImage passDepth) {
    if (!g_passRecorder.record || !g_passRecorder.borrow || !g_passRecorder.giveBack) return;
    tb.ours = true;
    t.borrowed = CallBorrow(cmd, passDepth);
    tb.ours = false;
    t.lentIn = cmd;
    t.inject = true;
    g_statArmed.fetch_add(1, std::memory_order_relaxed);
}
// Ahead of the first interface batch (or of the end of the pass, if there is none): our markers.
void InjectInPass(VkCommandBuffer cmd) {
    t.inject = false;
    if (cmd != t.lentIn) return;
    tb.ours = true;
    const bool drew = CallPassRecord(cmd, t.curRp, t.curW, t.curH, t.borrowed);
    tb.ours = false;
    if (drew) { RestoreGameState(cmd); g_statDrew.fetch_add(1, std::memory_order_relaxed); }
    static bool s_said = false;
    if (drew && !s_said && g_log) { s_said = true; g_log("in-frame: markers recorded inside the interface pass, %ux%u, scene depth lent %d", t.curW, t.curH, t.borrowed ? 1 : 0); }
}
void InterfacePassEnded(VkCommandBuffer cmd) {
    if (!t.borrowed || cmd != t.lentIn) { t.borrowed = false; return; }
    t.borrowed = false;
    tb.ours = true;
    CallGiveBack(cmd);
    tb.ours = false;
}

struct TrialPass { VkImageLayout layout; VkRenderPass rp; };
std::vector<TrialPass> g_trialPasses;                          // under g_mapMu
struct Retired { VkFramebuffer fb; unsigned frame; };
std::vector<Retired> g_retired;                                // under g_mapMu: framebuffers still in use by the GPU

bool SceneIsFinished(const VkFormat* colours, const VkAttachmentLoadOp* loads, std::uint32_t nc, VkFormat depth,
                     std::uint32_t w, std::uint32_t h) {
    const unsigned tw = g_targetW.load(), th = g_targetH.load();
    // about to begin: the interface pass (8 bit, keeps its contents, client size) ...
    if (nc != 1 || colours[0] != VK_FORMAT_R8G8B8A8_UNORM || loads[0] != VK_ATTACHMENT_LOAD_OP_LOAD || depth == VK_FORMAT_UNDEFINED) return false;
    if (!tw || w != tw || h != th) return false;
    // ... straight after the last full screen pass on the scene image
    return t.prevFb && t.prevColours == 1 && t.prevColour == VK_FORMAT_R16G16B16A16_SFLOAT && !t.prevDepth &&
           t.prevW == w && t.prevH == h && t.prevDraws == 1;
}

void DrawUnderInterface(VkCommandBuffer cmd, std::uint32_t w, std::uint32_t h) {
    if (!fCmdClearAttachments || !fDestroyFramebuffer || !rCreateRenderPass || !rCreateFramebuffer || !rBeginRP || !rEndRP) return;
    VkImageView view = VK_NULL_HANDLE; VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED; VkRenderPass ours = VK_NULL_HANDLE;
    {
        std::lock_guard<std::mutex> lk(g_mapMu);
        auto fb = g_fbs.find(t.prevFb); auto rp = g_rps.find(t.prevRp);
        if (fb == g_fbs.end() || rp == g_rps.end() || fb->second.views.size() != 1 || rp->second.finals.size() != 1) return;
        view = fb->second.views[0]; layout = rp->second.finals[0];
        if (!view || layout == VK_IMAGE_LAYOUT_UNDEFINED) return;
        for (const auto& p : g_trialPasses) if (p.layout == layout) ours = p.rp;
    }
    if (!ours) {
        // keeps what the game drew, and hands the image back in the layout the game left it in
        VkAttachmentDescription att{};
        att.format = VK_FORMAT_R16G16B16A16_SFLOAT; att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = layout; att.finalLayout = layout;
        VkAttachmentReference ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription sub{}; sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sub.colorAttachmentCount = 1; sub.pColorAttachments = &ref;
        VkRenderPassCreateInfo ci{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        ci.attachmentCount = 1; ci.pAttachments = &att; ci.subpassCount = 1; ci.pSubpasses = &sub;
        if (rCreateRenderPass(g_dev, &ci, nullptr, &ours) != VK_SUCCESS || !ours) return;
        std::lock_guard<std::mutex> lk(g_mapMu);
        g_trialPasses.push_back({ layout, ours });
    }
    VkFramebufferCreateInfo fi{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    fi.renderPass = ours; fi.attachmentCount = 1; fi.pAttachments = &view; fi.width = w; fi.height = h; fi.layers = 1;
    VkFramebuffer fb = VK_NULL_HANDLE;
    if (rCreateFramebuffer(g_dev, &fi, nullptr, &fb) != VK_SUCCESS || !fb) return;
    { std::lock_guard<std::mutex> lk(g_mapMu); g_retired.push_back({ fb, g_frame.load(std::memory_order_relaxed) }); }

    if (g_sceneRecorder) {
        // the markers themselves; whatever that binds goes through these hooks too and must not be taken for the game's
        tb.ours = true;
        bool drew = false;
        drew = CallRecorder(cmd, ours, fb, w, h);
        tb.ours = false;
        if (drew) RestoreGameState(cmd);
        static bool s_saidDraw = false;
        if (drew && !s_saidDraw && g_log) { s_saidDraw = true; g_log("in-frame: markers recorded ahead of the interface pass, %ux%u, image layout %d", w, h, (int)layout); }
        return;
    }
    VkRenderPassBeginInfo bi{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    bi.renderPass = ours; bi.framebuffer = fb; bi.renderArea = { { 0, 0 }, { w, h } };
    rBeginRP(cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
    // a square a fifth of the view high, in the middle, and one in the bottom left corner where the
    // game keeps part of its interface: the first must show, the second must be covered by the game
    const std::uint32_t side = h / 5;
    VkClearAttachment what{}; what.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; what.colorAttachment = 0;
    what.clearValue.color.float32[0] = 1.0f; what.clearValue.color.float32[1] = 0.0f; what.clearValue.color.float32[2] = 1.0f; what.clearValue.color.float32[3] = 1.0f;
    VkClearRect where[2]{};
    where[0].rect = { { (std::int32_t)(w / 2 - side / 2), (std::int32_t)(h / 2 - side / 2) }, { side, side } }; where[0].layerCount = 1;
    where[1].rect = { { 0, (std::int32_t)(h - side) }, { side * 2, side } }; where[1].layerCount = 1;
    fCmdClearAttachments(cmd, 1, &what, 2, where);
    rEndRP(cmd);
    static bool s_said = false;
    if (!s_said && g_log) { s_said = true; g_log("in-frame trial: drew into the finished scene %ux%u (image layout %d) ahead of the interface pass", w, h, (int)layout); }
}

void VKAPI_CALL HookBeginRP(VkCommandBuffer cmd, const VkRenderPassBeginInfo* info, VkSubpassContents contents) {
    if (info) {
        VkFormat colors[8]; VkAttachmentLoadOp loads[8]; std::uint32_t nc, w, h;
        VkFormat depth; VkAttachmentLoadOp dload; VkImage dimg; VkImageLayout dfinal;
        FromRenderPass(info, colors, loads, nc, depth, dload, dimg, dfinal, w, h);
        t.inject = false;
        if (g_inFrameTrial.load(std::memory_order_relaxed) && SceneIsFinished(colors, loads, nc, depth, w, h)) {
            const int where = g_where.load(std::memory_order_relaxed); if (where == 1) ArmInterfacePass(cmd, dimg); else if (where == 0) DrawUnderInterface(cmd, w, h);
        }
        t.curFb = info->framebuffer; t.curRp = info->renderPass; t.curColour = nc ? colors[0] : VK_FORMAT_UNDEFINED;
        t.curColours = nc; t.curW = w; t.curH = h; t.curDepth = depth != VK_FORMAT_UNDEFINED;
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
        t.inject = false;
        if (g_inFrameTrial.load(std::memory_order_relaxed) && SceneIsFinished(colors, loads, nc, depth, w, h)) {
            const int where = g_where.load(std::memory_order_relaxed); if (where == 1) ArmInterfacePass(cmd, dimg); else if (where == 0) DrawUnderInterface(cmd, w, h);
        }
        t.curFb = info->framebuffer; t.curRp = info->renderPass; t.curColour = nc ? colors[0] : VK_FORMAT_UNDEFINED;
        t.curColours = nc; t.curW = w; t.curH = h; t.curDepth = depth != VK_FORMAT_UNDEFINED;
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
void VKAPI_CALL HookEndRP(VkCommandBuffer cmd) { if (t.inject) InjectInPass(cmd); int slot = t.slot; int q = EndPass(); rEndRP(cmd); InterfacePassEnded(cmd); CloseQuery(cmd, slot, q); }
void VKAPI_CALL HookEndRP2(VkCommandBuffer cmd, const VkSubpassEndInfo* e) { if (t.inject) InjectInPass(cmd); int slot = t.slot; int q = EndPass(); rEndRP2(cmd, e); InterfacePassEnded(cmd); CloseQuery(cmd, slot, q); }
void VKAPI_CALL HookEndRendering(VkCommandBuffer cmd) { int slot = t.slot; int q = EndPass(); rEndRendering(cmd); CloseQuery(cmd, slot, q); }

void VKAPI_CALL HookBindPipeline(VkCommandBuffer cmd, VkPipelineBindPoint bp, VkPipeline pipe) {
    if (bp == VK_PIPELINE_BIND_POINT_GRAPHICS) {
        if (Bound* g = GameState(cmd)) g->pipeline = pipe;
        bool depth = false;
        if (g_hide.load(std::memory_order_relaxed)) {
            std::lock_guard<std::mutex> lk(g_mapMu);
            depth = g_depthPipes.count(pipe) != 0;
        }
        t.skip = depth;
        if (ProbeNear()) {
            std::lock_guard<std::mutex> lk(g_mapMu);
            auto it = g_pipeDepth.find(pipe);
            t.depth = it == g_pipeDepth.end() ? 0 : it->second;
        }
    }
    rBindPipeline(cmd, bp, pipe);
}

void VKAPI_CALL HookDraw(VkCommandBuffer c, std::uint32_t a, std::uint32_t b, std::uint32_t d, std::uint32_t e) { if (t.inject && a != 4) InjectInPass(c); if (t.skip) { ++t.skipped; return; } ++t.draws; TallyDepth(a); rDraw(c, a, b, d, e); }
void VKAPI_CALL HookDrawIndexed(VkCommandBuffer c, std::uint32_t a, std::uint32_t b, std::uint32_t d, std::int32_t e, std::uint32_t f) { if (t.inject && a != 4) InjectInPass(c); if (t.skip) { ++t.skipped; return; } ++t.draws; TallyDepth(a); rDrawIndexed(c, a, b, d, e, f); }
void VKAPI_CALL HookDrawIndirect(VkCommandBuffer c, VkBuffer b, VkDeviceSize o, std::uint32_t n, std::uint32_t s) { if (t.skip) { ++t.skipped; return; } ++t.indirect; TallyDepth(); rDrawIndirect(c, b, o, n, s); }
void VKAPI_CALL HookDrawIndexedIndirect(VkCommandBuffer c, VkBuffer b, VkDeviceSize o, std::uint32_t n, std::uint32_t s) { if (t.skip) { ++t.skipped; return; } ++t.indirect; TallyDepth(); rDrawIndexedIndirect(c, b, o, n, s); }
void VKAPI_CALL HookDrawIndirectCount(VkCommandBuffer c, VkBuffer b, VkDeviceSize o, VkBuffer cb, VkDeviceSize co, std::uint32_t m, std::uint32_t s) { if (t.skip) { ++t.skipped; return; } ++t.indirect; TallyDepth(); rDrawIndirectCount(c, b, o, cb, co, m, s); }
void VKAPI_CALL HookDrawIndexedIndirectCount(VkCommandBuffer c, VkBuffer b, VkDeviceSize o, VkBuffer cb, VkDeviceSize co, std::uint32_t m, std::uint32_t s) { if (t.skip) { ++t.skipped; return; } ++t.indirect; TallyDepth(); rDrawIndexedIndirectCount(c, b, o, cb, co, m, s); }
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
            if (const auto* ds = ci.pDepthStencilState)
                g_pipeDepth[out[i]] = (std::uint8_t)((ds->depthTestEnable ? 1 : 0) | (ds->depthWriteEnable ? 2 : 0) | (((unsigned)ds->depthCompareOp & 7u) << 2));
            if (dyn) g_dynDepthPipes.insert(out[i]);
        }
    }
    return r;
}
void VKAPI_CALL HookDestroyPipeline(VkDevice dev, VkPipeline p, const VkAllocationCallbacks* a) {
    { std::lock_guard<std::mutex> lk(g_mapMu); g_depthPipes.erase(p); g_dynDepthPipes.erase(p); g_pipeDepth.erase(p); }
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
        if (out && *out) {
            g_imageInfo[*out] = { ci->usage, ci->samples, ci->flags, ci->extent.width, ci->extent.height, ci->mipLevels, ci->arrayLayers };
            if (g_imageInfo.size() > 65536) g_imageInfo.clear();
        }
    }
    return r;
}
void VKAPI_CALL HookDestroyImage(VkDevice dev, VkImage img, const VkAllocationCallbacks* a) {
    if (img) {
        { std::lock_guard<std::mutex> lk(g_mapMu); g_imageInfo.erase(img); }
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

// Only this thread is handed to the transaction. Suspending every other thread for it, as was done
// here before, deadlocks: the transaction allocates, and a thread that was suspended while it held
// the heap lock never lets go of it, so the game froze for good with most of its threads asleep.
// Nothing is lost by it: these hooks go in while the device is being created or taken down, when no
// other thread is inside the functions being patched.
void UpdateThisThread() { DetourUpdateThread(GetCurrentThread()); }

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
    { (void**)&rBindDescriptorSets, (void*)HookBindDescriptorSets, "vkCmdBindDescriptorSets", false },
    { (void**)&rBindVertexBuffers, (void*)HookBindVertexBuffers, "vkCmdBindVertexBuffers", false },
    { (void**)&rBindIndexBuffer, (void*)HookBindIndexBuffer, "vkCmdBindIndexBuffer", false },
    { (void**)&rSetViewport, (void*)HookSetViewport, "vkCmdSetViewport", false },
    { (void**)&rSetScissor, (void*)HookSetScissor, "vkCmdSetScissor", false },
    { (void**)&rPushConstants, (void*)HookPushConstants, "vkCmdPushConstants", false },
    { (void**)&rBeginCommandBuffer, (void*)HookBeginCommandBuffer, "vkBeginCommandBuffer", false },
};

void CreateShare() {
    static std::uint32_t s_gen = 0;
    rtx::ipc::RebindIfStale(s_gen, g_share, g_shareMap);
    if (g_share) return;
    wchar_t name[rtx::ipc::kNameChars];
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
    // Device init is the only other caller, so without this the section would
    // never be re-created after a session change. Costs a pointer test a frame.
    CreateShare();
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
    fCmdClearAttachments = (PFN_vkCmdClearAttachments)gdpa(dev, "vkCmdClearAttachments");
    fDestroyFramebuffer = (PFN_vkDestroyFramebuffer)gdpa(dev, "vkDestroyFramebuffer");
    fDestroyRenderPass = (PFN_vkDestroyRenderPass)gdpa(dev, "vkDestroyRenderPass");
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

    DetourTransactionBegin();
    UpdateThisThread();
    for (auto& d : g_defs) if (*d.real) DetourAttach(d.real, d.hook);
    LONG rc = DetourTransactionCommit();
    if (rc != NO_ERROR) { if (g_log) g_log("probe: attach failed (%ld)", rc); return false; }
    g_attached = true;
    if (g_log) g_log("probe: draw hooks attached (dynamic rendering %s, renderpass2 %s, timestamps %s, period %.3f ns)",
                     rBeginRendering ? "yes" : "no", rBeginRP2 ? "yes" : "no", g_pools[0] ? "yes" : "no", (double)g_tsPeriod);
    return true;
}

void Detach() {
    if (!g_attached) return;
    DetourTransactionBegin();
    UpdateThisThread();
    for (auto& d : g_defs) if (*d.real) DetourDetach(d.real, d.hook);
    DetourTransactionCommit();
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
void SetInFrameTrial(bool on) { g_inFrameTrial.store(on, std::memory_order_relaxed); }
void SetSceneRecorder(bool (*recorder)(VkCommandBuffer, VkRenderPass, VkFramebuffer, std::uint32_t, std::uint32_t, unsigned)) { g_sceneRecorder = recorder; }
void SetPassRecorder(const PassRecorder& r) { g_passRecorder = r; }
bool HideSceneAvailable() { return g_attached; }
void RequestProbe() { g_probeNext.store(true); }

bool LookupImage(VkImage img, ImageInfo* out) {
    std::lock_guard<std::mutex> lk(g_mapMu);
    auto it = g_imageInfo.find(img);
    if (it == g_imageInfo.end()) return false;
    *out = it->second;
    return true;
}

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
    if (f % 1800 == 0 && g_inFrameTrial.load(std::memory_order_relaxed) && g_log)
        g_log("in-frame: last 1800 frames, interface pass found %u times, markers drawn in %u", g_statArmed.exchange(0), g_statDrew.exchange(0));
    if (fDestroyFramebuffer) {
        // the trial's framebuffers, once the GPU is sure to be done with them
        std::lock_guard<std::mutex> lk(g_mapMu);
        for (size_t i = 0; i < g_retired.size();) {
            if (f - g_retired[i].frame > 8) { fDestroyFramebuffer(g_dev, g_retired[i].fb, nullptr); g_retired[i] = g_retired.back(); g_retired.pop_back(); }
            else ++i;
        }
    }
    const ULONGLONG now = GetTickCount64();
    g_frameUs = g_lastPresentMs ? (unsigned)((now - g_lastPresentMs) * 1000) : 0;
    g_lastPresentMs = now;
    const int cur = g_slot.load(std::memory_order_relaxed);
    const int done = (cur + 1) % kSlots;   // recorded two presents ago; read before its pool is reset
    bool probe = g_probeNext.exchange(false) || f == 300 || f == 3000;

    std::vector<Pass> current, old;
    {
        std::lock_guard<std::mutex> lk(g_passMu);
        current = g_slotPasses[cur];
        old.swap(g_slotPasses[done]);
        // The fixed probe frames pass at the login screen. The world is a frame of dozens of passes:
        // once it has been up for a while, one more probe is taken of it.
        static unsigned s_worldSince = 0;
        if (current.size() >= 40) { if (!s_worldSince) s_worldSince = f; } else s_worldSince = 0;
        if (s_worldSince && !g_worldProbeAt.load() && f - s_worldSince == 600) g_worldProbeAt.store(f + 4);
        if (f == g_worldProbeAt.load() && f != 0) probe = true;
        // Scene depth: among client-size depth passes with a substantial draw count, the last one.
        // The reflection render comes first with a mirrored camera and nearly the same draw count;
        // the main scene and the water pass that follows share the depth image we want.
        const unsigned tw = g_targetW.load(), th = g_targetH.load();
        unsigned most = 0;
        for (const auto& p : current)
            if (p.depthImg && p.w == tw && p.h == th) most = std::max(most, p.draws + p.skipped);
        const Pass* best = nullptr;
        if (most >= 32)
            for (const auto& p : current)
                if (p.depthImg && p.w == tw && p.h == th && (p.draws + p.skipped) * 4 >= most) best = &p;
        if (best) {
            if (best->depthImg != g_sceneImg && g_log)
                g_log("scene depth from pass: %s (%u draws of %u max)", best->desc, best->draws + best->skipped, most);
            g_sceneImg = best->depthImg; g_sceneFmt = best->depthFmt; g_sceneLayout = best->depthFinal;
        }
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
    // compare ops as Vulkan numbers them: 0 never, 1 less, 2 equal, 3 less or equal, 4 greater, 5 not equal, 6 greater or equal, 7 always
    for (size_t i = 0; i < current.size(); ++i)
    {
        char sizes[96] = {};
        for (unsigned k = 0; k < current[i].firstCount; ++k) { char one[16]; std::snprintf(one, sizeof(one), "%s%u", k ? "," : "", current[i].first[k]); Cat(sizes, sizeof(sizes), one); }
        g_log("  pass %2zu: %s draws %u indirect %u dispatch %u skipped %u | depth test %u write %u off %u ops 0x%02x | first draws %s", i, current[i].desc, current[i].draws,
              current[i].indirect, current[i].dispatch, current[i].skipped, current[i].depthTest, current[i].depthWrite, current[i].depthOff, current[i].depthOps, sizes);
    }
    g_log("  images created so far %u (distinct format/usage/size %zu):", g_imageTotal.load(), images.size());
    for (const auto& s : images) {
        char fb[48] = {}; AppendFmt(fb, sizeof(fb), (VkFormat)s.key[0]);
        g_log("    %5u x %s usage 0x%x %ux%u", s.count, fb, s.key[1], s.key[2], s.key[3]);
    }
    g_log("  buffers created so far %u (distinct usage/size class %zu)", g_bufferTotal.load(), buffers.size());
}

}  // namespace rtx::vkprobe
