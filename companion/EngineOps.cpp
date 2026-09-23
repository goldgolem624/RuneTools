#include "EngineOps.h"

#include "MarkerShare.h"
#include "FrameShare.h"
#include "Present.h"

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <cstdarg>
#include <map>
#include <atomic>
#include <mutex>
#include <thread>
#include <string>
#include <vector>

namespace rtx::engineops {
namespace {

// The script state as the handlers read it (the same layout the component code uses).
constexpr std::size_t kStateSize = 0x20000;
constexpr std::size_t kIntStack  = 0x100;    // 1000 ints
constexpr std::size_t kIntSp     = 0x10A0;
constexpr std::size_t kStrStack  = 0x10A8;   // 1000 entries of 0x20
constexpr std::size_t kStrSp     = 0x8DA8;
constexpr std::size_t kEntityRef = 0xC3B0;   // the character the state holds: reference, then the character
constexpr std::size_t kEntityObj = 0xC3B8;
constexpr std::size_t kOffPlayers = 0x19950;
constexpr std::size_t kOffStatusByte = 0x19FA0;   // 30 = in the world
// Status byte off the client's root: 30 is in the world. Calling the engine's own operations while
// the client is still loading is not safe, and there is nothing to answer for anyway.
bool InTheWorld(std::uint8_t* root) {
    __try { return *reinterpret_cast<std::int8_t*>(root + kOffStatusByte) == 30; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

using OpFn = void* (*)(void* root, std::uint8_t* state);

std::mutex g_mu;
bool g_tried = false;
std::atomic<bool> g_ready{false};   // read every frame, written once by the worker
std::map<std::uint32_t, OpFn> g_byNumber;          // this build's number -> handler
std::map<std::string, std::uint32_t> g_byName;     // name -> number
std::uint8_t* g_state = nullptr;
std::mutex g_logMu; char g_log[400] = {};

void Say(const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_logMu);
    va_list ap; va_start(ap, fmt); std::vsnprintf(g_log, sizeof(g_log), fmt, ap); va_end(ap);
}

struct Section { const std::uint8_t* begin; std::size_t size; };
bool FindText(const std::uint8_t* base, Section& out) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec)
        if (std::strncmp(reinterpret_cast<const char*>(sec->Name), ".text", 8) == 0) { out = { base + sec->VirtualAddress, sec->Misc.VirtualSize }; return true; }
    return false;
}

// mov r8w, imm16 ; lea rdx, [rip+rel32]: the registrar's shape, once per operation
bool Handlers(const Section& text) {
    const std::uint8_t* p = text.begin; const std::uint8_t* end = text.begin + text.size - 12;
    for (; p < end; ++p) {
        if (p[0] != 0x66 || p[1] != 0x41 || p[2] != 0xB8 || p[5] != 0x48 || p[6] != 0x8D || p[7] != 0x15) continue;
        std::uint16_t op; std::memcpy(&op, p + 3, 2);
        std::int32_t rel; std::memcpy(&rel, p + 8, 4);
        const std::uint8_t* h = p + 12 + rel;
        if (h < text.begin || h >= text.begin + text.size) continue;
        auto it = g_byNumber.find(op);
        if (it != g_byNumber.end() && it->second != reinterpret_cast<OpFn>(const_cast<std::uint8_t*>(h))) return false;
        g_byNumber[op] = reinterpret_cast<OpFn>(const_cast<std::uint8_t*>(h));
    }
    if (g_byNumber.size() < 1500) return false;
    const std::uint32_t maxOp = g_byNumber.rbegin()->first;
    return g_byNumber.size() * 10 >= (std::size_t)maxOp * 9;
}

bool Names() {
    wchar_t up[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH)) return false;
    std::wstring path = std::wstring(up) + L"\\RuneToolsX\\cs2\\opcodes.json";
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{}; if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (16 << 20)) { CloseHandle(h); return false; }
    std::string s((size_t)sz.QuadPart, 0); DWORD got = 0; size_t at = 0;
    while (at < s.size()) { if (!ReadFile(h, s.data() + at, (DWORD)(s.size() - at), &got, nullptr) || !got) break; at += got; }
    CloseHandle(h);
    at = 0;
    while ((at = s.find("\"op\":", at)) != std::string::npos) {
        at += 5;
        const std::uint32_t op = (std::uint32_t)std::strtoul(s.c_str() + at, nullptr, 10);
        const size_t end = s.find('}', at); if (end == std::string::npos) break;
        const size_t nm = s.find("\"name\":\"", at);
        if (nm != std::string::npos && nm < end) { const size_t q = s.find('"', nm + 8); if (q != std::string::npos && q < end) g_byName[s.substr(nm + 8, q - nm - 8)] = op; }
        at = end;
    }
    return g_byName.size() > 500;
}

bool ResolveGuarded(const Section& text) {
    __try { return Handlers(text); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void Resolve() {
    const std::uint8_t* base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
    Section text;
    if (!base || !FindText(base, text)) return;
    if (!ResolveGuarded(text)) { Say("engine ops: registrar not recognised"); return; }
    if (!Names()) { Say("engine ops: operation table not readable"); return; }
    g_state = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, kStateSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    g_ready = g_state != nullptr;
    Say("engine ops: %zu handlers, %zu names", g_byNumber.size(), g_byName.size());
}

void* CallGuarded(OpFn fn, std::uint8_t* root) {
    __try { return fn(root, g_state); } __except (EXCEPTION_EXECUTE_HANDLER) { return reinterpret_cast<void*>(~0ull); }
}

}  // namespace

// Finding the handlers means walking the whole code section and reading the operation table off
// disk. That never happens on the game thread: the first caller starts a worker and is told "not
// yet", so a frame is never held up by it.
bool Ready() {
    if (g_ready) return true;
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_ready) return true;
    if (!g_tried) {
        g_tried = true;
        std::thread([] { std::lock_guard<std::mutex> lk(g_mu); Resolve(); }).detach();
    }
    return false;
}

const void* Handler(const char* name) {
    if (!Ready()) return nullptr;
    std::lock_guard<std::mutex> lk(g_mu);
    auto n = g_byName.find(name); if (n == g_byName.end()) return nullptr;
    auto h = g_byNumber.find(n->second); return h == g_byNumber.end() ? nullptr : reinterpret_cast<const void*>(h->second);
}

int Call(std::uint8_t* root, const char* name, const std::int32_t* ints, int nInts, const char* const* strs, int nStrs, std::int32_t* out, int cap) {
    const OpFn fn = reinterpret_cast<OpFn>(const_cast<void*>(Handler(name)));
    if (!fn || !root) return -1;
    std::lock_guard<std::mutex> lk(g_mu);
    std::uint32_t& isp = *reinterpret_cast<std::uint32_t*>(g_state + kIntSp);
    std::uint32_t& ssp = *reinterpret_cast<std::uint32_t*>(g_state + kStrSp);
    isp = 0; ssp = 0; g_state[0x20] = 0;
    for (int i = 0; i < nInts && i < 1000; ++i) *reinterpret_cast<std::int32_t*>(g_state + kIntStack + 4 * i) = ints[i], isp = i + 1;
    for (int i = 0; i < nStrs && i < 1000; ++i) {
        std::uint8_t* e = g_state + kStrStack + (std::size_t)i * 0x20;
        std::memset(e, 0, 0x20);
        const std::size_t n = std::strlen(strs[i]);
        if (n < 0x17) std::memcpy(e, strs[i], n + 1); else { std::memcpy(e, &strs[i], 8); e[0x17] = 0x80; }
        e[0x18] = 2; ssp = i + 1;
    }
    void* r = CallGuarded(fn, root);
    if (r == reinterpret_cast<void*>(~0ull)) { Say("engine ops: %s faulted", name); return -1; }
    int n = 0;
    for (std::uint32_t i = 0; i < isp && n < cap && i < 1000; ++i) out[n++] = *reinterpret_cast<std::int32_t*>(g_state + kIntStack + 4 * i);
    return n;
}

namespace {

// The projection operation, recognised by its own shape rather than trusted by number: it pops the
// terrain flag, takes a position value from the string stack (kind 3) and reads the game view. A
// build that changes that shape loses the feature instead of calling something else by accident.
constexpr std::uint32_t kOpProject = 1738;
const std::uint8_t kProjHead[] = { 0x40, 0x53, 0x56, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x50,
                                   0x8B, 0x82, 0xA0, 0x10, 0x00, 0x00, 0x48, 0x8B, 0xDA, 0xFF, 0xC8 };
const std::uint8_t kProjKind3[] = { 0x80, 0x7E, 0x18, 0x03 };          // cmp byte [rsi+0x18], 3
const std::uint8_t kProjView[]  = { 0x48, 0x8B, 0x89, 0xD0, 0x99, 0x01, 0x00 };   // mov rcx, [rcx+0x199d0]

bool Contains(const std::uint8_t* p, std::size_t n, const std::uint8_t* pat, std::size_t len) {
    for (std::size_t i = 0; i + len <= n; ++i) if (std::memcmp(p + i, pat, len) == 0) return true;
    return false;
}

// Checked once and remembered: a repeated byte comparison has no place in a frame.
OpFn g_proj = nullptr; int g_projState = 0;   // 0 unchecked, 1 good, -1 not this build
OpFn Projector() {
    if (g_projState) return g_proj;
    g_projState = -1;
    auto it = g_byNumber.find(kOpProject);
    if (it == g_byNumber.end()) return nullptr;
    const auto* p = reinterpret_cast<const std::uint8_t*>(it->second);
    if (std::memcmp(p, kProjHead, sizeof(kProjHead)) != 0) return nullptr;
    if (!Contains(p, 0x90, kProjKind3, sizeof(kProjKind3))) return nullptr;
    if (!Contains(p, 0x90, kProjView, sizeof(kProjView))) return nullptr;
    g_proj = it->second; g_projState = 1;
    return g_proj;
}

// One fault is enough: everything here stays off afterwards rather than trying again every frame.
bool g_poisoned = false;
bool g_probeOn = false;      // the check file is present

// The operation that puts the local player's position on the stack, recognised the same way: it
// reads the account block, then the player registry, and leaves one position value.
constexpr std::uint32_t kOpSelfPos = 1227;
const std::uint8_t kSelfHead[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x30,
                                   0x48, 0x8B, 0x81, 0xA8, 0x9F, 0x01, 0x00, 0x48, 0x8B, 0xDA, 0x48, 0x85, 0xC0 };

// The three operations that answer for a character: put the one with this index in the state's
// hands, ask how high the game hangs its own overheads on it, and project its position lifted by
// that much. Each is recognised by the first bytes of its own handler, the same in both clients and
// shared with no other operation.
constexpr std::uint32_t kOpBindEntity = 958, kOpOverlayHeight = 1002, kOpEntityScreen = 1668;
const std::uint8_t kBindHead[]   = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57,
                                     0x48, 0x83, 0xEC, 0x20, 0x8B, 0x9A, 0xA0, 0x10, 0x00 };
const std::uint8_t kHeightHead[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x8A, 0xB8, 0xC3,
                                     0x00, 0x00, 0x48, 0x8D, 0x9A, 0x00, 0x01, 0x00, 0x00 };
const std::uint8_t kEntScrHead[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
                                     0x8B, 0xF9, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0x8A, 0xB8 };

OpFn Verified(std::uint32_t op, const std::uint8_t* head, std::size_t len) {
    auto it = g_byNumber.find(op);
    if (it == g_byNumber.end()) return nullptr;
    if (std::memcmp(reinterpret_cast<const std::uint8_t*>(it->second), head, len) != 0) return nullptr;
    return it->second;
}

// A position as the engine passes them between operations: plane, then x, height and y as floats in
// fine units. The kind byte at +0x18 marks the entry a position rather than a string.
void PushPosition(int plane, float x, float height, float y) {
    std::uint32_t& sp = *reinterpret_cast<std::uint32_t*>(g_state + kStrSp);
    if (sp >= 1000) return;
    std::uint8_t* e = g_state + kStrStack + (std::size_t)sp * 0x20;
    std::memset(e, 0, 0x20);
    std::memcpy(e + 0x0, &plane, 4);
    std::memcpy(e + 0x4, &x, 4);
    std::memcpy(e + 0x8, &height, 4);
    std::memcpy(e + 0xC, &y, 4);
    e[0x18] = 3;
    ++sp;
}

}  // namespace

bool Project(std::uint8_t* root, int plane, float x, float height, float y, int heightOffset,
             bool onGround, Point& out) {
    if (g_poisoned || !Ready() || !root) return false;
    std::lock_guard<std::mutex> lk(g_mu);
    const OpFn fn = Projector();
    if (!fn) return false;
    std::uint32_t& isp = *reinterpret_cast<std::uint32_t*>(g_state + kIntSp);
    std::uint32_t& ssp = *reinterpret_cast<std::uint32_t*>(g_state + kStrSp);
    isp = 0; ssp = 0; g_state[0x20] = 0;
    // The operation takes the height from the terrain when asked to, and otherwise treats the
    // height in the position as zero: only the lift is added. So a caller's own height is passed
    // as part of the lift, and the position's height field is left for the terrain case.
    const long raise = heightOffset + (onGround ? 0L : std::lround(height));
    *reinterpret_cast<std::int32_t*>(g_state + kIntStack + 0) = (std::int32_t)raise;
    *reinterpret_cast<std::int32_t*>(g_state + kIntStack + 4) = onGround ? 1 : 0;
    isp = 2;
    PushPosition(plane, x, height, y);
    if (CallGuarded(fn, root) == reinterpret_cast<void*>(~0ull)) { g_poisoned = true; Say("engine ops: projection faulted, stopped"); return false; }
    if (isp < 3) return false;
    const auto* st = reinterpret_cast<const std::int32_t*>(g_state + kIntStack);
    out.x = st[isp - 3]; out.y = st[isp - 2]; out.depth = st[isp - 1];
    return true;
}

// The overhead-height operation only reads the character the state holds; it neither takes nor
// releases a reference. So the slot can be filled in directly with any character the game has,
// which is how a player is reached at all: the binding operation the scripts use goes through the
// NPC registry alone. The state is ours, and a character cannot go away inside a frame on this
// thread, so nothing is borrowed that has to be given back.
bool HeightHeld(std::uint8_t* root, std::uint8_t* entity, std::int32_t& lift) {
    const OpFn high = Verified(kOpOverlayHeight, kHeightHead, sizeof(kHeightHead));
    if (!high || !entity) return false;
    auto* slotObj = reinterpret_cast<std::uint8_t**>(g_state + kEntityObj);
    auto* slotRef = reinterpret_cast<std::uint8_t**>(g_state + kEntityRef);
    std::uint32_t& isp = *reinterpret_cast<std::uint32_t*>(g_state + kIntSp);
    auto* st = reinterpret_cast<std::int32_t*>(g_state + kIntStack);
    bool ok = false;
    __try {
        // what the operation reaches through: the character's own position
        if (!*reinterpret_cast<void**>(entity + 0x18)) return false;
        *slotRef = nullptr; *slotObj = entity;
        isp = 0;
        if (high(root, g_state) != reinterpret_cast<void*>(~0ull) && isp >= 1) { lift = st[isp - 1]; ok = true; }
        *slotObj = nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *slotObj = nullptr;
        g_poisoned = true;
        Say("engine ops: overhead height faulted, stopped");
        return false;
    }
    return ok;
}

// A player, by the index the game keeps it under: the registry holds one entry per index and the
// character sits at a fixed place inside it.
std::uint8_t* PlayerEntity(std::uint8_t* root, int index) {
    __try {
        std::uint8_t* reg = *reinterpret_cast<std::uint8_t**>(root + kOffPlayers);
        if (!reg) return nullptr;
        std::uint8_t* arr = *reinterpret_cast<std::uint8_t**>(reg + 0x10);
        if (!arr) return nullptr;
        std::uint8_t* entry = *reinterpret_cast<std::uint8_t**>(arr + (std::size_t)index * 8);
        if (!entry) return nullptr;
        return *reinterpret_cast<std::uint8_t**>(entry + 0x38);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

bool PlayerOverheadHeight(std::uint8_t* root, int index, std::int32_t& lift) {
    if (g_poisoned || !Ready() || !root || index < 0 || index > 0xFFFF) return false;
    std::lock_guard<std::mutex> lk(g_mu);
    return HeightHeld(root, PlayerEntity(root, index), lift);
}

bool NpcOverheadHeight(std::uint8_t* root, int index, std::int32_t& lift) {
    if (g_poisoned || !Ready() || !root) return false;
    std::lock_guard<std::mutex> lk(g_mu);
    const OpFn bind = Verified(kOpBindEntity, kBindHead, sizeof(kBindHead));
    const OpFn high = Verified(kOpOverlayHeight, kHeightHead, sizeof(kHeightHead));
    if (!bind || !high) return false;
    std::uint32_t& isp = *reinterpret_cast<std::uint32_t*>(g_state + kIntSp);
    std::uint32_t& ssp = *reinterpret_cast<std::uint32_t*>(g_state + kStrSp);
    isp = 0; ssp = 0; g_state[0x20] = 0;
    auto* st = reinterpret_cast<std::int32_t*>(g_state + kIntStack);
    st[0] = index; isp = 1;
    if (CallGuarded(bind, root) == reinterpret_cast<void*>(~0ull)) { g_poisoned = true; Say("engine ops: character lookup faulted, stopped"); return false; }
    if (isp < 1 || st[isp - 1] != 1) return false;          // no character with that index
    isp = 0;
    if (CallGuarded(high, root) == reinterpret_cast<void*>(~0ull)) { g_poisoned = true; Say("engine ops: overhead height faulted, stopped"); return false; }
    if (isp < 1) return false;
    lift = st[isp - 1];
    return true;
}

bool ProjectSelf(std::uint8_t* root, int heightOffset, Point& out) {
    if (g_poisoned || !Ready() || !root) return false;
    std::lock_guard<std::mutex> lk(g_mu);
    const OpFn proj = Projector();
    auto self = g_byNumber.find(kOpSelfPos);
    if (!proj || self == g_byNumber.end()) return false;
    if (std::memcmp(reinterpret_cast<const std::uint8_t*>(self->second), kSelfHead, sizeof(kSelfHead)) != 0) return false;
    std::uint32_t& isp = *reinterpret_cast<std::uint32_t*>(g_state + kIntSp);
    std::uint32_t& ssp = *reinterpret_cast<std::uint32_t*>(g_state + kStrSp);
    isp = 0; ssp = 0; g_state[0x20] = 0;
    // The position operation writes into the slot at the top of the stack and releases whatever it
    // finds there first. Marking the slot a position beforehand keeps it on the path that writes
    // without releasing, so it never looks at a pointer we did not put there.
    std::memset(g_state + kStrStack, 0, 0x20);
    g_state[kStrStack + 0x18] = 3;
    if (CallGuarded(self->second, root) == reinterpret_cast<void*>(~0ull)) { g_poisoned = true; Say("engine ops: own position faulted, stopped"); return false; }
    if (ssp < 1) return false;
    *reinterpret_cast<std::int32_t*>(g_state + kIntStack + 0) = heightOffset;
    *reinterpret_cast<std::int32_t*>(g_state + kIntStack + 4) = 0;   // the position already carries its height
    isp = 2;
    if (CallGuarded(proj, root) == reinterpret_cast<void*>(~0ull)) { g_poisoned = true; Say("engine ops: projection faulted, stopped"); return false; }
    if (isp < 3) return false;
    const auto* st = reinterpret_cast<const std::int32_t*>(g_state + kIntStack);
    out.x = st[isp - 3]; out.y = st[isp - 2]; out.depth = st[isp - 1];
    return true;
}

namespace { std::mutex g_qMu; std::int32_t g_qSound = 0, g_qZoom = 0, g_qFov = 0; bool g_qPending = false; }
void Queue(std::int32_t sound, std::int32_t zoom, std::int32_t fov) {
    std::lock_guard<std::mutex> lk(g_qMu);
    g_qSound = sound; g_qZoom = zoom; g_qFov = fov; g_qPending = true;
}
void Pump(std::uint8_t* root) {
    if (!root || !InTheWorld(root)) return;
    std::int32_t sound, zoom, fov;
    if (!root) return;
    { std::lock_guard<std::mutex> lk(g_qMu); if (!g_qPending) return; }
    if (!Ready()) return;          // still resolving: the request waits rather than being lost
    { std::lock_guard<std::mutex> lk(g_qMu); g_qPending = false; sound = g_qSound; zoom = g_qZoom; fov = g_qFov; }
    std::int32_t out[4];
    if (sound > 0) { const std::int32_t a[3] = { sound, 1, 0 }; const int r = Call(root, "SOUND_SYNTH", a, 3, nullptr, 0, out, 4); Say("engine ops: sound %d -> %s", sound, r < 0 ? "failed" : "played"); }
    if (zoom > 0)  { const std::int32_t a[2] = { zoom, zoom }; const int r = Call(root, "VIEWPORT_SETZOOM", a, 2, nullptr, 0, out, 4); Say("engine ops: zoom %d -> %s", zoom, r < 0 ? "failed" : "set"); }
    if (fov > 0)   { const std::int32_t a[2] = { fov, fov }; const int r = Call(root, "VIEWPORT_SETFOV", a, 2, nullptr, 0, out, 4); Say("engine ops: fov %d -> %s", fov, r < 0 ? "failed" : "set"); }
}

// A check that can be run on the live game: with %TEMP%\rtx_project.txt present, the engine's own
// screen point for the local player is reported once a second, so it can be held against where the
// overlay puts the same point before anything moves over to it.
void DevProject(std::uint8_t* root) {
    static ULONGLONG s_next = 0, s_looked = 0; static bool s_on = false; static int s_left = 20;
    if (g_poisoned || s_left <= 0) return;
    const ULONGLONG now = GetTickCount64();
    // Switchable while the game runs, so the file is looked at again every few seconds rather than
    // once; nothing at all is done unless it is there.
    if (now - s_looked > 4000) {
        s_looked = now;
        wchar_t tmp[MAX_PATH] = {}; GetTempPathW(MAX_PATH, tmp);
        const std::wstring path = std::wstring(tmp) + L"rtx_project.txt";
        s_on = GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
        g_probeOn = s_on;
    }
    if (!s_on || now < s_next) return;
    s_next = now + 1000;
    --s_left;
    Point p{};
    if (!ProjectSelf(root, 0, p)) { Say("projection: not available yet"); return; }
    // Once per run, what a call actually costs, so the number of markers that can go through it
    // each frame is a measurement rather than a guess.
    static bool s_timed = false;
    if (!s_timed) {
        s_timed = true;
        LARGE_INTEGER f{}, t0{}, t1{}; QueryPerformanceFrequency(&f);
        Point q{};
        constexpr int kRuns = 200;
        QueryPerformanceCounter(&t0);
        for (int i = 0; i < kRuns; ++i) Project(root, 0, (float)(1000 + i), 0.f, 2000.f, 0, false, q);
        QueryPerformanceCounter(&t1);
        const double us = f.QuadPart ? (double)(t1.QuadPart - t0.QuadPart) * 1e6 / (double)f.QuadPart / kRuns : 0.0;
        Say("projection: own position on screen %d, %d depth %d, one call %.2f us", p.x, p.y, p.depth, us);
        return;
    }
    Say("projection: own position on screen %d, %d depth %d", p.x, p.y, p.depth);
}

namespace {
std::mutex g_aMu;
rtx::marker::Anchor g_aWant[rtx::marker::kMaxAnchors];
int g_aCount = 0;
}   // namespace

void WantAnchors(const rtx::marker::Anchor* a, int n) {
    if (n < 0) n = 0;
    if (n > rtx::marker::kMaxAnchors) n = rtx::marker::kMaxAnchors;
    std::lock_guard<std::mutex> lk(g_aMu);
    if (n) std::memcpy(g_aWant, a, sizeof(rtx::marker::Anchor) * (std::size_t)n);
    g_aCount = n;
}

void PumpAnchors(std::uint8_t* root) {
    if (!root || !InTheWorld(root)) return;
    // ten times a second is more than the launcher can use, and keeps this off the frame path
    static ULONGLONG s_last = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - s_last < 100) return;
    s_last = now;
    rtx::marker::Anchor want[rtx::marker::kMaxAnchors];
    int n = 0;
    { std::lock_guard<std::mutex> lk(g_aMu); n = g_aCount; if (n) std::memcpy(want, g_aWant, sizeof(rtx::marker::Anchor) * (std::size_t)n); }
    // While the check file is present, say once every two seconds what came in and what went
    // back, so a silent end of the chain can be told apart from a wrong answer.
    static ULONGLONG s_say = 0;
    const ULONGLONG nowMs = GetTickCount64();
    const bool tell = g_probeOn && nowMs - s_say > 2000;
    if (!n || g_poisoned || !root) {
        if (tell) { s_say = nowMs; Say("anchors: none wanted (poisoned %d)", g_poisoned ? 1 : 0); }
        rtx::present::PublishAnchors(nullptr, 0);
        return;
    }
    if (!Ready()) { if (tell) { s_say = nowMs; Say("anchors: %d wanted, operations not resolved yet", n); } return; }
    rtx::frame::Share::AnchorPoint out[rtx::marker::kMaxAnchors];
    int named = 0, fromGame = 0, players = 0, playersOk = 0, noAnswer = 0, lastMiss = 0;
    for (int i = 0; i < n; ++i) {
        // A character named alongside a point means: keep the point where it is, and hang the answer
        // at the height the game hangs its own overheads at on that character. A positive value is
        // the game's NPC index, a negative one a player as -(index + 1).
        std::int32_t lift = want[i].lift;
        bool gameHeight = false;
        if (want[i].entity) {
            ++named;
            if (want[i].entity < 0) ++players;
            std::int32_t over = 0;
            const bool got = want[i].entity > 0 ? NpcOverheadHeight(root, want[i].entity, over)
                                                : PlayerOverheadHeight(root, -want[i].entity - 1, over);
            if (got) { lift = over; ++fromGame; if (want[i].entity < 0) ++playersOk; }
            else { ++noAnswer; lastMiss = want[i].entity; }   // the lift given stays: the old placement
            gameHeight = got;
        }
        Point p{};
        const bool ok = Project(root, want[i].plane, want[i].x, want[i].height, want[i].y,
                                lift, want[i].on_ground != 0, p);
        // ok tells the launcher how the answer was reached: 1 from the point as given, 2 with the
        // height the game hangs its own overheads at, which it places without its own lift.
        out[i].x = ok ? p.x : 0; out[i].y = ok ? p.y : 0; out[i].depth = ok ? p.depth : 0;
        out[i].ok = ok ? (gameHeight ? 2 : 1) : 0;
        out[i].tag = want[i].tag;
    }
    const bool sent = rtx::present::PublishAnchors(out, n);
    if (tell) {
        s_say = nowMs;
        if (sent) Say("anchors: %d wanted, %d named (%d players, %d answered), %d heights from the game, %d without, last miss %d",
                      n, named, players, playersOk, fromGame, noAnswer, lastMiss);
        else {
            bool mapped = false; std::uint32_t mg = 0, ver = 0, wmg = 0, wver = 0;
            rtx::present::FrameChannelState(mapped, mg, ver, wmg, wver);
            Say("anchors: %d wanted, first %d,%d ok %d, not handed over: channel %s, mark %08x wanted %08x, version %u wanted %u",
                n, out[0].x, out[0].y, out[0].ok, mapped ? "mapped" : "not mapped", mg, wmg, ver, wver);
        }
    }
}

bool TakeLog(char* out, std::size_t cap) {
    std::lock_guard<std::mutex> lk(g_logMu);
    if (!g_log[0] || cap == 0) return false;
    std::snprintf(out, cap, "%s", g_log); g_log[0] = 0; return true;
}

}  // namespace rtx::engineops
