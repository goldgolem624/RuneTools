// Vulkan present path (see VkPresent.h).
//
// rs2client imports only vkGetDeviceProcAddr, vkGetInstanceProcAddr and the memory/buffer/image
// functions from vulkan-1.dll; everything else is fetched by name and called through driver
// pointers, so the export detours only serve to capture handles. Once a VkDevice is seen, the real
// vkGetDeviceProcAddr hands back the exact function pointers the game holds and those get detoured.

#include "VkPresent.h"
#include "VkComposite.h"
#include "VkProbe.h"
#include "Present.h"
#include "CaptureShare.h"

#include <windows.h>
#include <tlhelp32.h>
#include <detours.h>
#undef CreateSemaphore
#include <atomic>
#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace rtx::vkpresent {
namespace {

HMODULE g_loader = nullptr;
bool    g_installed = false;

PFN_vkGetDeviceProcAddr   g_realGDPA = nullptr;
PFN_vkGetInstanceProcAddr g_realGIPA = nullptr;
PFN_vkAllocateMemory      g_realAllocateMemory = nullptr;
PFN_vkCreateBuffer        g_realCreateBuffer = nullptr;
PFN_vkCreateImage         g_realCreateImage = nullptr;
PFN_vkMapMemory           g_realMapMemory = nullptr;
PFN_vkGetPhysicalDeviceMemoryProperties  g_realGetPDMem = nullptr;
PFN_vkGetPhysicalDeviceMemoryProperties2 g_realGetPDMem2 = nullptr;
PFN_vkGetPhysicalDeviceProperties        g_realGetPDProps = nullptr;
PFN_vkGetPhysicalDeviceQueueFamilyProperties g_getQueueFamilies = nullptr;
PFN_vkEnumeratePhysicalDevices g_enumPhys = nullptr;
PFN_vkCreateInstance      g_createInstance = nullptr;

PFN_vkQueuePresentKHR     g_realQueuePresent = nullptr;
PFN_vkCreateSwapchainKHR  g_realCreateSwapchain = nullptr;
PFN_vkDestroySwapchainKHR g_realDestroySwapchain = nullptr;
PFN_vkDestroyDevice       g_realDestroyDevice = nullptr;
PFN_vkGetDeviceQueue      g_realGetDeviceQueue = nullptr;
bool                      g_deviceHooks = false;

std::atomic<VkDevice>         g_seenDevice{ VK_NULL_HANDLE };
std::atomic<VkPhysicalDevice> g_seenPhys{ VK_NULL_HANDLE };
VkInstance        g_ownInstance = VK_NULL_HANDLE;
VkDevice          g_dev = VK_NULL_HANDLE;
std::atomic<bool> g_armed{ false };
std::uint32_t     g_family = 0, g_familyQueues = 0;
int               g_queueCheck = 0;        // 0 unknown, 1 present queue is in the graphics family, -1 not
HWND              g_hwnd = nullptr;
ULONGLONG         g_hwndMs = 0;
ULONGLONG         g_armedMs = 0;
bool              g_nudged = false;
ULONGLONG         g_nudgeRestoreMs = 0;
int               g_nudgeW = 0, g_nudgeH = 0;
bool              g_warnedNoChain = false;
std::atomic<bool> g_everRegistered{ false };
std::atomic<unsigned> g_frames{ 0 };
rtx::capture::Share* g_cap = nullptr;
HANDLE g_capMap = nullptr;
ULONGLONG g_capTryMs = 0;

void EnsureCaptureMapped() {
    if (g_cap) return;
    ULONGLONG now = GetTickCount64();
    if (now - g_capTryMs < 1000) return;
    g_capTryMs = now;
    wchar_t name[64];
    rtx::capture::MakeSectionName(GetCurrentProcessId(), name);
    g_capMap = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
    if (!g_capMap) return;
    g_cap = reinterpret_cast<rtx::capture::Share*>(MapViewOfFile(g_capMap, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(rtx::capture::Share)));
    if (!g_cap) { CloseHandle(g_capMap); g_capMap = nullptr; return; }
    rtx::vkcomposite::SetCaptureShare(g_cap);
}

void OnImageDestroyedCb(VkImage img) {
    __try { rtx::vkcomposite::OnImageDestroyed(img); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void Log(const char* fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt);
    int n = std::vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    OutputDebugStringA(buf);
    wchar_t up[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH)) return;
    wchar_t path[MAX_PATH + 64];
    _snwprintf_s(path, _TRUNCATE, L"%s\\RuneToolsX\\logs\\companion-%lu.log", up, (unsigned long)GetCurrentProcessId());
    HANDLE f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st; GetLocalTime(&st);
    char line[600];
    int m = std::snprintf(line, sizeof(line), "[%02u:%02u:%02u.%03u] vk: %s\r\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf);
    DWORD w = 0;
    if (m > 0) WriteFile(f, line, (DWORD)m, &w, nullptr);
    CloseHandle(f);
}

// Every thread but ours gets suspended across the transaction so no thread is mid-prologue.
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

// ---- export hooks: handle capture only ----

inline void NoteDevice(VkDevice d) {
    if (d && d != g_dev) g_seenDevice.store(d, std::memory_order_relaxed);
}
inline void NotePhys(VkPhysicalDevice p) {
    if (p) g_seenPhys.store(p, std::memory_order_relaxed);
}

PFN_vkVoidFunction VKAPI_CALL HookGDPA(VkDevice dev, const char* name) {
    NoteDevice(dev);
    return g_realGDPA(dev, name);
}
VkResult VKAPI_CALL HookAllocateMemory(VkDevice dev, const VkMemoryAllocateInfo* i, const VkAllocationCallbacks* a, VkDeviceMemory* m) {
    NoteDevice(dev); return g_realAllocateMemory(dev, i, a, m);
}
VkResult VKAPI_CALL HookCreateBuffer(VkDevice dev, const VkBufferCreateInfo* i, const VkAllocationCallbacks* a, VkBuffer* b) {
    NoteDevice(dev); return g_realCreateBuffer(dev, i, a, b);
}
VkResult VKAPI_CALL HookCreateImage(VkDevice dev, const VkImageCreateInfo* i, const VkAllocationCallbacks* a, VkImage* im) {
    NoteDevice(dev); return g_realCreateImage(dev, i, a, im);
}
VkResult VKAPI_CALL HookMapMemory(VkDevice dev, VkDeviceMemory m, VkDeviceSize off, VkDeviceSize sz, VkMemoryMapFlags f, void** pp) {
    NoteDevice(dev); return g_realMapMemory(dev, m, off, sz, f, pp);
}
void VKAPI_CALL HookGetPDMem(VkPhysicalDevice pd, VkPhysicalDeviceMemoryProperties* p) {
    NotePhys(pd); g_realGetPDMem(pd, p);
}
void VKAPI_CALL HookGetPDMem2(VkPhysicalDevice pd, VkPhysicalDeviceMemoryProperties2* p) {
    NotePhys(pd); g_realGetPDMem2(pd, p);
}
void VKAPI_CALL HookGetPDProps(VkPhysicalDevice pd, VkPhysicalDeviceProperties* p) {
    NotePhys(pd); g_realGetPDProps(pd, p);
}

// ---- window lookup: the launcher reparents the game window, so children count too ----

struct FindCtx { DWORD pid; HWND best; long area; };
BOOL CALLBACK ConsiderWindow(HWND hwnd, LPARAM lp) {
    auto* c = reinterpret_cast<FindCtx*>(lp);
    DWORD wpid = 0;
    GetWindowThreadProcessId(hwnd, &wpid);
    if (wpid == c->pid && IsWindowVisible(hwnd)) {
        RECT r;
        if (GetClientRect(hwnd, &r)) {
            long area = (long)(r.right - r.left) * (r.bottom - r.top);
            if (area > c->area && (r.right - r.left) > 200 && (r.bottom - r.top) > 200) { c->area = area; c->best = hwnd; }
        }
    }
    return TRUE;
}
BOOL CALLBACK TopLevel(HWND hwnd, LPARAM lp) {
    ConsiderWindow(hwnd, lp);
    EnumChildWindows(hwnd, ConsiderWindow, lp);
    return TRUE;
}
// Input lands on the deepest window under the cursor: the client draws through a child
// (JagRenderView) that covers its main window, so descend while a visible child covers the parent.
BOOL CALLBACK CoveringChild(HWND hwnd, LPARAM lp) {
    auto* c = reinterpret_cast<FindCtx*>(lp);
    if (!IsWindowVisible(hwnd) || GetParent(hwnd) != c->best) return TRUE;
    RECT r;
    if (!GetClientRect(hwnd, &r)) return TRUE;
    long area = (long)(r.right - r.left) * (r.bottom - r.top);
    if (area * 10 >= c->area * 9 && area > 0) { c->area = area; c->best = hwnd; return FALSE; }
    return TRUE;
}
HWND FindGameWindow() {
    FindCtx c{ GetCurrentProcessId(), nullptr, 0 };
    EnumWindows(TopLevel, reinterpret_cast<LPARAM>(&c));
    for (int depth = 0; c.best && depth < 4; ++depth) {
        HWND before = c.best;
        EnumChildWindows(before, CoveringChild, reinterpret_cast<LPARAM>(&c));
        if (c.best == before) break;
    }
    return c.best;
}

void RefreshWindow() {
    ULONGLONG now = GetTickCount64();
    if (g_hwnd && IsWindow(g_hwnd) && now - g_hwndMs < 2000) return;
    if (now - g_hwndMs < 250) return;
    g_hwndMs = now;
    HWND h = FindGameWindow();
    if (h != g_hwnd) { g_hwnd = h; Log("game window %p", (void*)h); }
}

// ---- device-level hooks ----

VkResult VKAPI_CALL HookCreateSwapchain(VkDevice dev, const VkSwapchainCreateInfoKHR* ci, const VkAllocationCallbacks* a, VkSwapchainKHR* out) {
    VkResult r = g_realCreateSwapchain(dev, ci, a, out);
    if (r == VK_SUCCESS && ci && out && g_armed.load() && dev == g_dev) {
        bool ok = false;
        __try {
            ok = rtx::vkcomposite::RegisterSwapchain(*out, ci->imageFormat, ci->imageExtent.width, ci->imageExtent.height);
        } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
        if (ok) g_everRegistered.store(true);
        Log("swapchain %p format %d %ux%u minImages %u -> %s", (void*)*out, (int)ci->imageFormat,
            ci->imageExtent.width, ci->imageExtent.height, ci->minImageCount, ok ? "registered" : "NOT registered");
    }
    return r;
}

void VKAPI_CALL HookDestroySwapchain(VkDevice dev, VkSwapchainKHR sc, const VkAllocationCallbacks* a) {
    if (g_armed.load() && dev == g_dev) {
        __try { rtx::vkcomposite::UnregisterSwapchain(sc); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    g_realDestroySwapchain(dev, sc, a);
}

VkResult VKAPI_CALL HookQueuePresent(VkQueue queue, const VkPresentInfoKHR* info);
void VKAPI_CALL HookDestroyDevice(VkDevice dev, const VkAllocationCallbacks* a);

bool AttachDeviceHooks() {
    ThreadSet ts;
    DetourTransactionBegin();
    UpdateAllThreads(ts);
    DetourAttach(&(PVOID&)g_realQueuePresent,     (PVOID)HookQueuePresent);
    DetourAttach(&(PVOID&)g_realCreateSwapchain,  (PVOID)HookCreateSwapchain);
    DetourAttach(&(PVOID&)g_realDestroySwapchain, (PVOID)HookDestroySwapchain);
    DetourAttach(&(PVOID&)g_realDestroyDevice,    (PVOID)HookDestroyDevice);
    LONG rc = DetourTransactionCommit();
    CloseThreads(ts);
    if (rc != NO_ERROR) { Log("device hook attach failed (%ld)", rc); return false; }
    g_deviceHooks = true;
    return true;
}

void DetachDeviceHooks() {
    if (!g_deviceHooks) return;
    ThreadSet ts;
    DetourTransactionBegin();
    UpdateAllThreads(ts);
    DetourDetach(&(PVOID&)g_realQueuePresent,     (PVOID)HookQueuePresent);
    DetourDetach(&(PVOID&)g_realCreateSwapchain,  (PVOID)HookCreateSwapchain);
    DetourDetach(&(PVOID&)g_realDestroySwapchain, (PVOID)HookDestroySwapchain);
    DetourDetach(&(PVOID&)g_realDestroyDevice,    (PVOID)HookDestroyDevice);
    DetourTransactionCommit();
    CloseThreads(ts);
    g_deviceHooks = false;
}

void Disarm() {
    g_armed.store(false);
    __try { rtx::vkcomposite::Shutdown(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    rtx::vkprobe::Detach();
    DetachDeviceHooks();
    g_dev = VK_NULL_HANDLE;
    g_queueCheck = 0;
    Log("disarmed");
}

void VKAPI_CALL HookDestroyDevice(VkDevice dev, const VkAllocationCallbacks* a) {
    if (g_armed.load() && dev == g_dev) Disarm();
    g_realDestroyDevice(dev, a);
}

void CheckQueue(VkQueue queue) {
    for (std::uint32_t i = 0; i < g_familyQueues; ++i) {
        VkQueue q = VK_NULL_HANDLE;
        g_realGetDeviceQueue(g_dev, g_family, i, &q);
        if (q == queue) { g_queueCheck = 1; Log("present queue = family %u index %u", g_family, i); return; }
    }
    g_queueCheck = -1;
    Log("present queue %p is not in graphics family %u; overlay off", (void*)queue, g_family);
}

// Plain data only: runs under the caller's SEH frame.
int PresentInner(VkQueue queue, const VkPresentInfoKHR* info, VkPresentInfoKHR* local, VkSemaphore* chain) {
    if (g_queueCheck == 0) CheckQueue(queue);
    if (g_queueCheck < 0) return 0;
    RefreshWindow();
    EnsureCaptureMapped();
    rtx::vkprobe::FrameBegin();
    const VkSemaphore* waits = info->pWaitSemaphores;
    std::uint32_t wc = info->waitSemaphoreCount;
    int n = 0;
    std::uint32_t count = info->swapchainCount < 8 ? info->swapchainCount : 8;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint32_t w = 0, h = 0;
        if (!rtx::vkcomposite::BeginTarget(info->pSwapchains[i], info->pImageIndices[i], &w, &h)) continue;
        rtx::vkprobe::SetTargetExtent(w, h);
        VkImage dimg = VK_NULL_HANDLE; VkFormat dfmt = VK_FORMAT_UNDEFINED; VkImageLayout dlay = VK_IMAGE_LAYOUT_UNDEFINED;
        if (rtx::vkprobe::SceneDepth(&dimg, &dfmt, &dlay)) rtx::vkcomposite::SetSceneDepth(dimg, dfmt, dlay);
        else rtx::vkcomposite::SetSceneDepth(VK_NULL_HANDLE, VK_FORMAT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED);
        rtx::present::RenderOverlay(rtx::present::VkBackend(), g_hwnd, (int)w, (int)h);
        VkSemaphore s = rtx::vkcomposite::Submit(queue, wc, waits);
        if (s) { chain[n++] = s; waits = &chain[n - 1]; wc = 1; }
    }
    if (n) { local->waitSemaphoreCount = wc; local->pWaitSemaphores = waits; }
    g_frames.fetch_add(1, std::memory_order_relaxed);
    return n;
}

VkResult VKAPI_CALL HookQueuePresent(VkQueue queue, const VkPresentInfoKHR* info) {
    if (!g_armed.load(std::memory_order_relaxed) || !info) return g_realQueuePresent(queue, info);
    VkPresentInfoKHR local = *info;
    VkSemaphore chain[8];
    int n = 0;
    __try {
        n = PresentInner(queue, info, &local, chain);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        n = 0;
        static bool s_logged = false;
        if (!s_logged) { s_logged = true; Log("present frame fault (overlay skipped)"); }
    }
    return g_realQueuePresent(queue, n ? &local : info);
}

// ---- arming ----

std::uint32_t PreferredDeviceId() {
    wchar_t exe[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return 0;
    wchar_t* slash = wcsrchr(exe, L'\\');
    if (!slash) return 0;
    *(slash + 1) = 0;
    wchar_t path[MAX_PATH + 32];
    _snwprintf_s(path, _TRUNCATE, L"%spreferences.cfg", exe);
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return 0;
    char buf[4096]; DWORD got = 0;
    BOOL ok = ReadFile(f, buf, sizeof(buf) - 1, &got, nullptr);
    CloseHandle(f);
    if (!ok) return 0;
    buf[got] = 0;
    const char* p = std::strstr(buf, "graphics_device=");
    if (!p) return 0;
    return (std::uint32_t)std::strtoul(p + 16, nullptr, 16);
}

VkPhysicalDevice PickPhysicalDevice() {
    if (!g_createInstance || !g_enumPhys) return VK_NULL_HANDLE;
    if (!g_ownInstance) {
        VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
        app.pApplicationName = "RuneToolsX"; app.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo ci{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        ci.pApplicationInfo = &app;
        if (g_createInstance(&ci, nullptr, &g_ownInstance) != VK_SUCCESS) { Log("own instance failed"); return VK_NULL_HANDLE; }
    }
    std::uint32_t n = 0;
    if (g_enumPhys(g_ownInstance, &n, nullptr) != VK_SUCCESS || n == 0) return VK_NULL_HANDLE;
    VkPhysicalDevice devs[16];
    if (n > 16) n = 16;
    if (g_enumPhys(g_ownInstance, &n, devs) < VK_SUCCESS) return VK_NULL_HANDLE;
    const std::uint32_t want = PreferredDeviceId();
    VkPhysicalDevice discrete = VK_NULL_HANDLE;
    for (std::uint32_t i = 0; i < n; ++i) {
        VkPhysicalDeviceProperties p{};
        g_realGetPDProps(devs[i], &p);
        Log("gpu %u: %s id %04x type %d", i, p.deviceName, p.deviceID, (int)p.deviceType);
        if (want && p.deviceID == want) return devs[i];
        if (!discrete && p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) discrete = devs[i];
    }
    return discrete ? discrete : devs[0];
}

// Device-level entry points resolve only when their extension (or core version) is enabled.
void LogExtensions(VkDevice dev) {
    static const char* const names[] = {
        "vkCmdBeginRendering", "vkCmdBeginRenderingKHR", "vkCmdBeginRenderPass2", "vkCmdPushDescriptorSetKHR",
        "vkCmdDrawIndexedIndirectCount", "vkCmdDrawMeshTasksEXT", "vkCmdTraceRaysKHR", "vkCreateRayTracingPipelinesKHR",
        "vkCmdBuildAccelerationStructuresKHR", "vkGetBufferDeviceAddress", "vkCmdSetDepthTestEnable",
        "vkCmdBindDescriptorBuffersEXT", "vkCmdSetFragmentShadingRateKHR", "vkGetCalibratedTimestampsEXT",
        "vkCmdWriteTimestamp", "vkCmdBeginQuery", "vkCmdBindShadersEXT", "vkCmdPipelineBarrier2",
        "vkQueueSubmit2", "vkCreateShadersEXT", "vkCmdBindIndexBuffer2KHR", "vkCmdDrawIndirectByteCountEXT",
        "vkCmdSetPolygonModeEXT", "vkCmdCopyImage2", "vkCmdBlitImage2", "vkWaitSemaphores",
    };
    char line[1024] = {};
    for (const char* n : names) {
        if (!g_realGDPA(dev, n)) continue;
        if (line[0]) strncat_s(line, ", ", _TRUNCATE);
        strncat_s(line, n + 2, _TRUNCATE);
    }
    Log("device entry points available: %s", line);
}

template <typename T>
bool Dev(VkDevice dev, T& fp, const char* name) {
    fp = reinterpret_cast<T>(g_realGDPA(dev, name));
    if (!fp) Log("missing device entry %s", name);
    return fp != nullptr;
}

bool Arm(VkDevice dev) {
    rtx::vkcomposite::DeviceFns f{};
    bool ok = true;
#define RTX_DEV(name) ok = Dev(dev, f.name, "vk" #name) && ok
    RTX_DEV(CreateRenderPass); RTX_DEV(DestroyRenderPass); RTX_DEV(CreateImageView); RTX_DEV(DestroyImageView);
    RTX_DEV(CreateFramebuffer); RTX_DEV(DestroyFramebuffer); RTX_DEV(CreateShaderModule); RTX_DEV(DestroyShaderModule);
    RTX_DEV(CreatePipelineLayout); RTX_DEV(DestroyPipelineLayout); RTX_DEV(CreateGraphicsPipelines); RTX_DEV(DestroyPipeline);
    RTX_DEV(CreateDescriptorSetLayout); RTX_DEV(DestroyDescriptorSetLayout); RTX_DEV(CreateDescriptorPool); RTX_DEV(DestroyDescriptorPool);
    RTX_DEV(AllocateDescriptorSets); RTX_DEV(UpdateDescriptorSets); RTX_DEV(CreateSampler); RTX_DEV(DestroySampler);
    RTX_DEV(CreateImage); RTX_DEV(DestroyImage); RTX_DEV(CreateBuffer); RTX_DEV(DestroyBuffer);
    RTX_DEV(GetImageMemoryRequirements); RTX_DEV(GetBufferMemoryRequirements); RTX_DEV(AllocateMemory); RTX_DEV(FreeMemory);
    RTX_DEV(BindImageMemory); RTX_DEV(BindBufferMemory); RTX_DEV(MapMemory); RTX_DEV(UnmapMemory);
    RTX_DEV(CreateCommandPool); RTX_DEV(DestroyCommandPool); RTX_DEV(AllocateCommandBuffers); RTX_DEV(FreeCommandBuffers);
    RTX_DEV(BeginCommandBuffer); RTX_DEV(EndCommandBuffer); RTX_DEV(ResetCommandBuffer); RTX_DEV(CmdPipelineBarrier);
    RTX_DEV(CmdCopyBufferToImage); RTX_DEV(CmdCopyImageToBuffer); RTX_DEV(CmdBeginRenderPass); RTX_DEV(CmdEndRenderPass); RTX_DEV(CmdBindPipeline);
    RTX_DEV(CmdBindDescriptorSets); RTX_DEV(CmdBindVertexBuffers); RTX_DEV(CmdPushConstants); RTX_DEV(CmdSetViewport);
    RTX_DEV(CmdSetScissor); RTX_DEV(CmdDraw); RTX_DEV(CreateFence); RTX_DEV(DestroyFence); RTX_DEV(WaitForFences);
    RTX_DEV(ResetFences); RTX_DEV(CreateSemaphore); RTX_DEV(DestroySemaphore); RTX_DEV(QueueSubmit);
    RTX_DEV(GetSwapchainImagesKHR); RTX_DEV(DeviceWaitIdle);
#undef RTX_DEV
    ok = Dev(dev, g_realQueuePresent, "vkQueuePresentKHR") && ok;
    ok = Dev(dev, g_realCreateSwapchain, "vkCreateSwapchainKHR") && ok;
    ok = Dev(dev, g_realDestroySwapchain, "vkDestroySwapchainKHR") && ok;
    ok = Dev(dev, g_realDestroyDevice, "vkDestroyDevice") && ok;
    ok = Dev(dev, g_realGetDeviceQueue, "vkGetDeviceQueue") && ok;
    if (!ok) return false;

    VkPhysicalDevice pd = g_seenPhys.load();
    if (!pd) pd = PickPhysicalDevice();
    if (!pd) { Log("no physical device"); return false; }
    VkPhysicalDeviceMemoryProperties mem{};
    g_realGetPDMem(pd, &mem);
    VkPhysicalDeviceProperties pdp{};
    g_realGetPDProps(pd, &pdp);
    std::uint32_t nq = 0;
    g_getQueueFamilies(pd, &nq, nullptr);
    VkQueueFamilyProperties qf[32];
    if (nq > 32) nq = 32;
    g_getQueueFamilies(pd, &nq, qf);
    bool found = false;
    for (std::uint32_t i = 0; i < nq; ++i)
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { g_family = i; g_familyQueues = qf[i].queueCount; found = true; break; }
    if (!found) { Log("no graphics queue family"); return false; }

    if (!AttachDeviceHooks()) return false;
    bool init = false;
    __try { init = rtx::vkcomposite::Init(dev, f, mem, g_family); } __except (EXCEPTION_EXECUTE_HANDLER) { init = false; }
    if (!init) { Log("composite init failed"); DetachDeviceHooks(); return false; }
    g_dev = dev;
    g_queueCheck = 0;
    g_armedMs = GetTickCount64();
    g_nudged = false; g_warnedNoChain = false; g_everRegistered.store(false);
    g_armed.store(true);
    Log("armed on device %p, graphics family %u (%u queues), present %p", (void*)dev, g_family, g_familyQueues, (void*)g_realQueuePresent);
    LogExtensions(dev);
    rtx::vkcomposite::SetLog(Log);
    rtx::vkcomposite::SetCmdHook(rtx::vkprobe::OnOverlayCmd);
    if (rtx::vkprobe::Attach(dev, g_realGDPA, pdp.limits.timestampPeriod, Log, OnImageDestroyedCb))
        rtx::vkcomposite::SetAlwaysRecord(true);
    return true;
}

// A swapchain that already existed at injection has no known format. One 1px resize makes the
// game recreate it through the hook.
void Bootstrap() {
    ULONGLONG now = GetTickCount64();
    if (g_nudgeRestoreMs && now >= g_nudgeRestoreMs) {
        g_nudgeRestoreMs = 0;
        if (g_hwnd && IsWindow(g_hwnd))
            SetWindowPos(g_hwnd, nullptr, 0, 0, g_nudgeW, g_nudgeH, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
    if (g_everRegistered.load()) return;
    if (!g_nudged && now - g_armedMs > 1500) {
        g_nudged = true;
        RefreshWindow();
        RECT r{};
        if (g_hwnd && GetWindowRect(g_hwnd, &r)) {
            g_nudgeW = r.right - r.left; g_nudgeH = r.bottom - r.top;
            Log("nudging window %p (%dx%d) to learn the swapchain", (void*)g_hwnd, g_nudgeW, g_nudgeH);
            SetWindowPos(g_hwnd, nullptr, 0, 0, g_nudgeW + 1, g_nudgeH, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            g_nudgeRestoreMs = now + 200;
        } else {
            Log("no game window to nudge");
        }
    } else if (g_nudged && !g_warnedNoChain && now - g_armedMs > 6000) {
        g_warnedNoChain = true;
        Log("swapchain still unknown after nudge (frames seen %u)", g_frames.load());
    }
}

}  // namespace

bool Active() { return g_armed.load(std::memory_order_relaxed); }
void SetHideScene(bool on) { rtx::vkprobe::SetHideScene(on); }
bool HideSceneAvailable() { return g_armed.load(std::memory_order_relaxed) && rtx::vkprobe::HideSceneAvailable(); }

// %USERPROFILE%\\RuneToolsX\\vk-features.txt: lines depth=0/1, timing=0/1, capture=0/1 (default all on).
void PollFeatures() {
    static ULONGLONG s_next = 0;
    ULONGLONG now = GetTickCount64();
    if (now < s_next) return;
    s_next = now + 2000;
    wchar_t up[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH)) return;
    wchar_t path[MAX_PATH + 40];
    _snwprintf_s(path, _TRUNCATE, L"%s\\RuneToolsX\\vk-features.txt", up);
    bool depth = true, timing = true, capture = true;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        char buf[512]; DWORD got = 0;
        if (ReadFile(f, buf, sizeof(buf) - 1, &got, nullptr)) {
            buf[got] = 0;
            if (std::strstr(buf, "depth=0")) depth = false;
            if (std::strstr(buf, "timing=0")) timing = false;
            if (std::strstr(buf, "capture=0")) capture = false;
        }
        CloseHandle(f);
    }
    static int s_last = -1;
    int cur = (depth ? 1 : 0) | (timing ? 2 : 0) | (capture ? 4 : 0);
    if (cur == s_last) return;
    s_last = cur;
    rtx::vkcomposite::SetFeatures(depth, capture);
    rtx::vkprobe::SetTimingEnabled(timing);
    rtx::vkcomposite::SetAlwaysRecord(timing);
    Log("features: depth %d timing %d capture %d", depth ? 1 : 0, timing ? 1 : 0, capture ? 1 : 0);
}

void Poll() {
    if (!g_installed) return;
    PollFeatures();
    VkDevice dev = g_seenDevice.load(std::memory_order_relaxed);
    if (dev && dev != g_dev) {
        if (g_armed.load()) Disarm();
        g_seenDevice.store(VK_NULL_HANDLE);
        Arm(dev);
    }
    if (g_armed.load()) Bootstrap();
}

bool Install() {
    if (g_installed) return true;
    g_loader = GetModuleHandleW(L"vulkan-1.dll");
    if (!g_loader) return false;
    auto exp = [&](const char* n) { return GetProcAddress(g_loader, n); };
    g_realGDPA           = (PFN_vkGetDeviceProcAddr)exp("vkGetDeviceProcAddr");
    g_realGIPA           = (PFN_vkGetInstanceProcAddr)exp("vkGetInstanceProcAddr");
    g_realAllocateMemory = (PFN_vkAllocateMemory)exp("vkAllocateMemory");
    g_realCreateBuffer   = (PFN_vkCreateBuffer)exp("vkCreateBuffer");
    g_realCreateImage    = (PFN_vkCreateImage)exp("vkCreateImage");
    g_realMapMemory      = (PFN_vkMapMemory)exp("vkMapMemory");
    g_realGetPDMem       = (PFN_vkGetPhysicalDeviceMemoryProperties)exp("vkGetPhysicalDeviceMemoryProperties");
    g_realGetPDMem2      = (PFN_vkGetPhysicalDeviceMemoryProperties2)exp("vkGetPhysicalDeviceMemoryProperties2");
    g_realGetPDProps     = (PFN_vkGetPhysicalDeviceProperties)exp("vkGetPhysicalDeviceProperties");
    g_getQueueFamilies   = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)exp("vkGetPhysicalDeviceQueueFamilyProperties");
    g_enumPhys           = (PFN_vkEnumeratePhysicalDevices)exp("vkEnumeratePhysicalDevices");
    g_createInstance     = (PFN_vkCreateInstance)exp("vkCreateInstance");
    if (!g_realGDPA || !g_realGIPA || !g_realAllocateMemory || !g_realCreateBuffer || !g_realCreateImage ||
        !g_realMapMemory || !g_realGetPDMem || !g_realGetPDProps || !g_getQueueFamilies) {
        Log("loader exports missing");
        return false;
    }

    ThreadSet ts;
    DetourTransactionBegin();
    UpdateAllThreads(ts);
    DetourAttach(&(PVOID&)g_realGDPA,           (PVOID)HookGDPA);
    DetourAttach(&(PVOID&)g_realAllocateMemory, (PVOID)HookAllocateMemory);
    DetourAttach(&(PVOID&)g_realCreateBuffer,   (PVOID)HookCreateBuffer);
    DetourAttach(&(PVOID&)g_realCreateImage,    (PVOID)HookCreateImage);
    DetourAttach(&(PVOID&)g_realMapMemory,      (PVOID)HookMapMemory);
    DetourAttach(&(PVOID&)g_realGetPDMem,       (PVOID)HookGetPDMem);
    if (g_realGetPDMem2) DetourAttach(&(PVOID&)g_realGetPDMem2, (PVOID)HookGetPDMem2);
    DetourAttach(&(PVOID&)g_realGetPDProps,     (PVOID)HookGetPDProps);
    LONG rc = DetourTransactionCommit();
    CloseThreads(ts);
    if (rc != NO_ERROR) { Log("export hook attach failed (%ld)", rc); return false; }
    g_installed = true;
    Log("loader hooks installed (%p)", (void*)g_loader);
    return true;
}

}  // namespace rtx::vkpresent
