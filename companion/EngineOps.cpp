#include "EngineOps.h"
#include "Signatures.h"
#include "MainDataOffsets.h"

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
#include <set>
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
constexpr std::size_t kOffPlayers = rtx::md::kPlayers;
constexpr std::size_t kOffStatusByte = rtx::md::kStatus;   // 30 = in the world
// The MainData shift the scene module adopted, applied to the two root fields read here.
std::atomic<std::int64_t> g_rootShift{ 0 };
// Status byte off the client's root: 30 is in the world. Calling the engine's own operations while
// the client is still loading is not safe, and there is nothing to answer for anyway.
bool InTheWorld(std::uint8_t* root) {
    __try { return *reinterpret_cast<std::int8_t*>(root + kOffStatusByte + g_rootShift.load(std::memory_order_relaxed)) == 30; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

using OpFn = void* (*)(void* root, std::uint8_t* state);

std::mutex g_mu;
bool g_tried = false;
std::atomic<bool> g_ready{false};   // read every frame, written once by the worker
std::map<std::uint32_t, OpFn> g_byNumber;          // this build's number -> handler
std::map<std::string, std::uint32_t> g_byName;     // name -> number
std::set<std::string> g_faulted;                   // named operations that faulted (guarded by g_mu)
std::uint8_t* g_state = nullptr;
// Messages for the log, oldest first; a few can arrive between two takes (the resolver says three
// things in a row), so they queue rather than overwrite.
std::mutex g_logMu; char g_log[6][400] = {}; int g_logHead = 0, g_logCount = 0;

void Say(const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_logMu);
    constexpr int kSlots = sizeof(g_log) / sizeof(g_log[0]);
    if (g_logCount == kSlots) { g_logHead = (g_logHead + 1) % kSlots; --g_logCount; }   // drop the oldest
    char* at = g_log[(g_logHead + g_logCount) % kSlots];
    va_list ap; va_start(ap, fmt); std::vsnprintf(at, sizeof(g_log[0]), fmt, ap); va_end(ap);
    ++g_logCount;
}
// Boot record check line (grammar in Signatures.h).
void Check(const char* status, const char* kind, const char* exp, const char* got, const char* detail) {
    Say("check: engine-ops %s kind=%s exp=%s got=%s features=%s need=- ; %s", status, kind && kind[0] ? kind : "-",
        exp && exp[0] ? exp : "-", got && got[0] ? got : "-",
        "Asks: achievements and quests|Sounds, camera zoom and FOV|In-frame panels and text|In-frame labels|Overhead anchors", detail);
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

// The running game's build from its own version resource ("950.1.0.0"), the same form the launcher
// writes beside the operation table when it extracts it.
std::string RunningBuild() {
    HMODULE m = GetModuleHandleW(nullptr);
    HRSRC r = FindResourceW(m, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(16) /*RT_VERSION*/);
    if (!r) return {};
    const DWORD n = SizeofResource(m, r);
    const auto* p = static_cast<const std::uint8_t*>(LockResource(LoadResource(m, r)));
    if (!p) return {};
    for (DWORD i = 0; i + sizeof(VS_FIXEDFILEINFO) <= n; i += 4) {
        VS_FIXEDFILEINFO f; std::memcpy(&f, p + i, sizeof(f));
        if (f.dwSignature != 0xFEEF04BD) continue;
        char v[48];
        std::snprintf(v, sizeof(v), "%u.%u.%u.%u", (unsigned)HIWORD(f.dwFileVersionMS), (unsigned)LOWORD(f.dwFileVersionMS),
                      (unsigned)HIWORD(f.dwFileVersionLS), (unsigned)LOWORD(f.dwFileVersionLS));
        return v;
    }
    return {};
}

// The running exe's PE time stamp: builds that share a version string (the live and beta clients are
// both 950.1.0.0) differ in it.
std::uint32_t RunningStamp() {
    const auto* base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
    if (!base) return 0;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE ? nt->FileHeader.TimeDateStamp : 0;
}

// Handlers that name themselves in their error text, by the name the operation table gives them.
struct Spot { const char* text; const char* op; };
constexpr Spot kSpots[] = {
    { "db_getfield",                    "dbrow_getfield" },
    { "array_sort",                     "ARRAY_SORT" },
    { "_minimenuopen",                  "MINIMENUOPEN" },
    { "_deeplink_get",                  "DEEPLINK_GET" },
    { "_shop_applypendingtransactions", "SHOP_APPLYPENDINGTRANSACTIONS" },
};

// The one string in .rdata whose whole text ends in `text`; null when absent or not unique.
const std::uint8_t* FindNamedString(const std::uint8_t* base, const char* text) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    const std::size_t tl = std::strlen(text);
    const std::uint8_t* found = nullptr;
    for (WORD k = 0; k < nt->FileHeader.NumberOfSections; ++k, ++sec) {
        if (std::memcmp(sec->Name, ".rdata", 6) != 0) continue;
        const std::uint8_t* b = base + sec->VirtualAddress;
        const std::size_t n = sec->Misc.VirtualSize;
        for (std::size_t i = 0; i + tl + 1 <= n; ++i) {
            if (b[i] != (std::uint8_t)text[0] || std::memcmp(b + i, text, tl) != 0 || b[i + tl] != 0) continue;
            std::size_t st = i;
            while (st > 0 && b[st - 1] != 0) --st;
            if (found) return nullptr;
            found = b + st;
        }
    }
    return found;
}

// True when the handler's first 0x400 bytes load `str` with a rip-relative lea.
bool HandlerLoads(const std::uint8_t* h, const std::uint8_t* str) {
    __try {
        for (std::size_t i = 0; i + 7 <= 0x400; ++i) {
            if ((h[i] == 0x48 || h[i] == 0x4C) && h[i + 1] == 0x8D && (h[i + 2] & 0xC7) == 0x05) {
                std::int32_t rel; std::memcpy(&rel, h + i + 3, 4);
                if (h + i + 7 + rel == str) return true;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

// How many self-naming handlers sit under another number than the table gives them; `checked` counts
// the ones that could be compared. A table from another exe of the same version disagrees here.
int SpotDisagreements(int& checked) {
    checked = 0;
    const auto* base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
    if (!base) return 0;
    int bad = 0;
    for (const Spot& sp : kSpots) {
        const auto named = g_byName.find(sp.op);
        const std::uint8_t* str = FindNamedString(base, sp.text);
        if (named == g_byName.end() || !str) continue;
        bool any = false, agrees = false;
        for (const auto& kv : g_byNumber) {
            if (!HandlerLoads(reinterpret_cast<const std::uint8_t*>(kv.second), str)) continue;
            any = true;
            if (kv.first == named->second) agrees = true;
        }
        if (!any) continue;
        ++checked;
        if (!agrees) ++bad;
    }
    return bad;
}

// Operation numbers move between game builds, so names from another build's table would call the wrong
// handlers. Only a table extracted from this build names anything: its version must be this exe's, and
// the self-naming handlers must sit under the table's numbers (or, when too few can be compared, its
// label must carry this exe's time stamp). Operations recognised by their own code (projection,
// positions) do not depend on it.
// `running` and `table` take the two build labels; `why` the short reason when they do not match.
bool NamesMatchBuild(std::string& running, std::string& table, std::string& why) {
    wchar_t up[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH)) { why = "no profile folder"; return false; }
    const std::wstring path = std::wstring(up) + L"\\RuneToolsX\\cs2\\client_version.txt";
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    char buf[64] = {}; DWORD got = 0;
    if (h != INVALID_HANDLE_VALUE) { ReadFile(h, buf, sizeof(buf) - 1, &got, nullptr); CloseHandle(h); }
    std::string label(buf, got);
    while (!label.empty() && (label.back() == '\r' || label.back() == '\n' || label.back() == ' ')) label.pop_back();
    const std::size_t sp = label.find(' ');      // "950.1.0.0 6a9986f8": the build, then the exe's time stamp
    table = label.substr(0, sp);
    const std::uint32_t tableStamp = sp == std::string::npos ? 0 : (std::uint32_t)std::strtoul(label.c_str() + sp + 1, nullptr, 16);
    running = RunningBuild();
    const std::uint32_t runningStamp = RunningStamp();
    if (!running.empty() && table == running) {
        int checked = 0;
        const int bad = SpotDisagreements(checked);
        if (bad > 0 || (checked < 3 && (tableStamp == 0 || runningStamp == 0 || tableStamp != runningStamp))) {
            Say("engine ops: the operation table is from another exe of build %s (%d of %d self-naming handlers disagree, stamp %08x, this is %08x); named operations are off until the tables are extracted again",
                running.c_str(), bad, checked, tableStamp, runningStamp);
            char w[160];
            std::snprintf(w, sizeof(w), "op names are from another exe of build %s (%d of %d self-naming handlers disagree, stamp %08x vs %08x); named ops off", running.c_str(), bad, checked, tableStamp, runningStamp);
            why = w;
            return false;
        }
    }
    if (!running.empty() && table == running) {
        // The launcher gives the label its table's write time. A label newer than the table was
        // written for an extraction that never rewrote it (earlier launchers wrote it when the run
        // started), so the table is still an earlier build's.
        const std::wstring ops = std::wstring(up) + L"\\RuneToolsX\\cs2\\opcodes.json";
        WIN32_FILE_ATTRIBUTE_DATA t{}, v{};
        if (GetFileAttributesExW(ops.c_str(), GetFileExInfoStandard, &t) &&
            GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &v) &&
            CompareFileTime(&t.ftLastWriteTime, &v.ftLastWriteTime) >= 0) return true;
        Say("engine ops: the operation table was not extracted from game build %s; named operations are off until the tables are extracted again",
            running.c_str());
        why = "the operation table was not extracted from build " + running + "; named ops off";
        return false;
    }
    Say("engine ops: operation names are from game build %s, this is %s; named operations are off until the tables are extracted again",
        table.empty() ? "(unknown)" : table.c_str(), running.empty() ? "(unknown)" : running.c_str());
    why = "op names are from build " + (table.empty() ? std::string("(unknown)") : table) + ", this is " + (running.empty() ? std::string("(unknown)") : running) + "; named ops off";
    return false;
}

bool ResolveGuarded(const Section& text) {
    __try { return Handlers(text); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void Resolve() {
    const std::uint8_t* base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
    Section text;
    if (!base || !FindText(base, text)) { Check("FAIL", "gone", "handlers", "-", "GONE: no code section to read the op registrar from; every engine op off"); return; }
    if (!ResolveGuarded(text)) { Say("engine ops: registrar not recognised"); Check("FAIL", "gone", "handlers", "0", "GONE: the op registrar was not recognised in this exe; every engine op off"); return; }
    if (!Names()) { Say("engine ops: operation table not readable"); Check("FAIL", "gone", "names", "0", "GONE: the operation table (cs2/opcodes.json) is not readable; every engine op off until the tables are extracted"); return; }
    std::string running, table, why;
    const bool match = NamesMatchBuild(running, table, why);
    if (!match) g_byName.clear();
    g_state = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, kStateSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    g_ready = g_state != nullptr;
    Say("engine ops: %zu handlers, %zu names", g_byNumber.size(), g_byName.size());
    const std::string exp = running.empty() ? std::string("(unknown)") : running, got = table.empty() ? std::string("(unknown)") : table;
    if (!match) Check("FAIL", "gone", exp.c_str(), got.c_str(), ("GONE: " + why).c_str());
    else {
        char d[160];
        std::snprintf(d, sizeof(d), "%zu handlers from the registrar, %zu names from this build's table", g_byNumber.size(), g_byName.size());
        Check("OK", "", exp.c_str(), got.c_str(), d);
    }
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

// Where the state keeps the component the interface operations act on, read straight out of the
// routine that resolves it: the slot is chosen by the state's own form byte.
constexpr std::size_t kCurCompIf = 0xBFC0;   // form byte at +0x20 is 0
constexpr std::size_t kCurCompCc = 0xBFE0;   // form byte is anything else
const void* CurrentComponent() {
    if (!g_state) return nullptr;
    std::lock_guard<std::mutex> lk(g_mu);
    const std::size_t at = g_state[0x20] ? kCurCompCc : kCurCompIf;
    return *reinterpret_cast<void**>(g_state + at);
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
    if (g_faulted.count(name)) return -1;   // faulted once: not called again, rather than faulting every frame
    std::uint32_t& isp = *reinterpret_cast<std::uint32_t*>(g_state + kIntSp);
    std::uint32_t& ssp = *reinterpret_cast<std::uint32_t*>(g_state + kStrSp);
    isp = 0; ssp = 0; g_state[0x20] = 0;
    for (int i = 0; i < nInts && i < 1000; ++i) *reinterpret_cast<std::int32_t*>(g_state + kIntStack + 4 * i) = ints[i], isp = i + 1;
    // String entries as the engine keeps them: up to 23 characters inline with byte +0x17 = 23 -
    // length, longer text out of line as {pointer, length +8, capacity +0x10 with bit 63 set}
    // (that top byte is the +0x17 flag). The caller's text outlives the call; the game never
    // releases an entry this module wrote.
    for (int i = 0; i < nStrs && i < 1000; ++i) {
        std::uint8_t* e = g_state + kStrStack + (std::size_t)i * 0x20;
        std::memset(e, 0, 0x20);
        const std::size_t n = std::strlen(strs[i]);
        if (n <= 0x17) { std::memcpy(e, strs[i], n); e[0x17] = (std::uint8_t)(0x17 - n); }
        else {
            const std::uint64_t len = n, cap = n | (1ull << 63);
            std::memcpy(e, &strs[i], 8); std::memcpy(e + 8, &len, 8); std::memcpy(e + 0x10, &cap, 8);
        }
        e[0x18] = 2; ssp = i + 1;
    }
    void* r = CallGuarded(fn, root);
    if (r == reinterpret_cast<void*>(~0ull)) { g_faulted.insert(name); Say("engine ops: %s faulted, not called again", name); return -1; }
    int n = 0;
    for (std::uint32_t i = 0; i < isp && n < cap && i < 1000; ++i) out[n++] = *reinterpret_cast<std::int32_t*>(g_state + kIntStack + 4 * i);
    return n;
}

namespace {

// The projection operation, recognised by its own shape rather than trusted by number: it pops the
// terrain flag, takes a position value from the string stack (kind 3) and reads the game view. A
// build that changes that shape loses the feature instead of calling something else by accident.
constexpr std::uint32_t kOpProject = rtx::sig::kOpProject;
constexpr const auto& kProjHead = rtx::sig::kOpProjectHead;
constexpr const auto& kProjKind3 = rtx::sig::kOpProjectKind3;          // cmp byte [rsi+0x18], 3
constexpr const auto& kProjView = rtx::sig::kOpProjectView;   // mov rcx, [rcx+0x199d0]

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

// The operation that puts the local player's position on the stack, recognised the same way: it
// reads the account block, then the player registry, and leaves one position value.
constexpr std::uint32_t kOpSelfPos = rtx::sig::kOpSelfPos;
constexpr const auto& kSelfHead = rtx::sig::kOpSelfPosHead;

// The three operations that answer for a character: put the one with this index in the state's
// hands, ask how high the game hangs its own overheads on it, and project its position lifted by
// that much. Each is recognised by the first bytes of its own handler, the same in both clients and
// shared with no other operation.
constexpr std::uint32_t kOpBindEntity = rtx::sig::kOpBindEntity, kOpOverlayHeight = rtx::sig::kOpOverlayHeight, kOpEntityScreen = rtx::sig::kOpEntityScreen;
constexpr const auto& kBindHead = rtx::sig::kOpBindHead;
constexpr const auto& kHeightHead = rtx::sig::kOpHeightHead;
constexpr const auto& kEntScrHead = rtx::sig::kOpEntityScreenHead;

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
// thread, so the character put in is only borrowed. What the slot held before is not: the binding
// operation leaves a counted reference there, which the next binding gives back. So that pair is
// set aside for the call and put back after it, rather than lost with its count still held.
bool HeightHeld(std::uint8_t* root, std::uint8_t* entity, std::int32_t& lift) {
    const OpFn high = Verified(kOpOverlayHeight, kHeightHead, sizeof(kHeightHead));
    if (!high || !entity) return false;
    auto* slotObj = reinterpret_cast<std::uint8_t**>(g_state + kEntityObj);
    auto* slotRef = reinterpret_cast<std::uint8_t**>(g_state + kEntityRef);
    std::uint8_t* const heldRef = *slotRef;
    std::uint8_t* const heldObj = *slotObj;
    std::uint32_t& isp = *reinterpret_cast<std::uint32_t*>(g_state + kIntSp);
    auto* st = reinterpret_cast<std::int32_t*>(g_state + kIntStack);
    bool ok = false;
    __try {
        // what the operation reaches through: the character's own position
        if (!*reinterpret_cast<void**>(entity + 0x18)) return false;
        *slotRef = nullptr; *slotObj = entity;
        isp = 0;
        if (high(root, g_state) != reinterpret_cast<void*>(~0ull) && isp >= 1) { lift = st[isp - 1]; ok = true; }
        *slotRef = heldRef; *slotObj = heldObj;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *slotRef = heldRef; *slotObj = heldObj;
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
        std::uint8_t* reg = *reinterpret_cast<std::uint8_t**>(root + kOffPlayers + g_rootShift.load(std::memory_order_relaxed));
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
    if (!n || g_poisoned || !root) {
        rtx::present::PublishAnchors(nullptr, 0);
        return;
    }
    if (!Ready()) return;
    rtx::frame::Share::AnchorPoint out[rtx::marker::kMaxAnchors];
    for (int i = 0; i < n; ++i) {
        // A character named alongside a point means: keep the point where it is, and hang the answer
        // at the height the game hangs its own overheads at on that character. A positive value is
        // the game's NPC index, a negative one a player as -(index + 1).
        std::int32_t lift = want[i].lift;
        bool gameHeight = false;
        if (want[i].entity) {
            std::int32_t over = 0;
            const bool got = want[i].entity > 0 ? NpcOverheadHeight(root, want[i].entity, over)
                                                : PlayerOverheadHeight(root, -want[i].entity - 1, over);
            if (got) lift = over;   // otherwise the lift given stays: the old placement
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
    rtx::present::PublishAnchors(out, n);
}

namespace {
std::mutex g_askMu;
rtx::marker::Ask g_ask[rtx::marker::kMaxAsks];
int g_askCount = 0;
std::uint32_t g_askSeq = 0;
// Where the sweep has got to, and the answers so far. A list is answered once: until the launcher
// changes it, nothing here runs at all.
rtx::frame::Share::AskAnswer g_answer[rtx::marker::kMaxAsks];
int g_askAt = 0;
bool g_askDone = true;
}   // namespace

void WantAsks(const rtx::marker::Ask* a, int n, std::uint32_t seq) {
    if (n < 0) n = 0;
    if (n > rtx::marker::kMaxAsks) n = rtx::marker::kMaxAsks;
    std::lock_guard<std::mutex> lk(g_askMu);
    if (seq == g_askSeq && n == g_askCount) return;     // the same list again: leave the answers alone
    if (n) std::memcpy(g_ask, a, sizeof(rtx::marker::Ask) * (std::size_t)n);
    g_askCount = n; g_askSeq = seq; g_askAt = 0; g_askDone = n == 0;
    std::memset(g_answer, 0, sizeof(g_answer));
}

void PumpAsks(std::uint8_t* root) {
    if (!root || g_poisoned || !InTheWorld(root)) return;
    {
        std::lock_guard<std::mutex> lk(g_askMu);
        if (g_askDone || g_askCount <= 0) return;
    }
    if (!Ready()) return;                       // still resolving: the list waits rather than being lost
    // A few per frame, and a time budget on top. The game weighs a requirement by walking
    // everything it depends on, and how deep that goes is the account's business, not ours: one
    // question can cost far more than another. So a long list is spread out, and the sweep stops
    // early if this frame has already spent its share, however few questions that turned out to be.
    constexpr int kPerFrame = 4;
    constexpr double kBudgetMs = 2.0;
    rtx::marker::Ask todo[kPerFrame];
    int n = 0, at = 0, count = 0;
    std::uint32_t seq = 0;
    {
        std::lock_guard<std::mutex> lk(g_askMu);
        at = g_askAt; count = g_askCount; seq = g_askSeq;
        for (; n < kPerFrame && at + n < count; ++n) todo[n] = g_ask[at + n];
    }
    // Which operation answers each kind, and whether it needs the index as well as the id. The
    // three that take nothing are the account-wide ones.
    struct OpFor { const char* name; int args; };
    auto opFor = [](std::uint16_t kind) -> OpFor {
        switch (kind) {
            case rtx::marker::kAskAchievementState:    return { "ACHIEVEMENT_REQSTATE", 1 };
            case rtx::marker::kAskAchievementPrereqs:  return { "ACHIEVEMENT_ALLPREREQMET", 1 };
            case rtx::marker::kAskQuestFinished:       return { "QUEST_FINISHED", 1 };
            case rtx::marker::kAskQuestStarted:        return { "QUEST_STARTED", 1 };
            case rtx::marker::kAskQuestStatReqCount:   return { "QUEST_STATREQ_COUNT", 1 };
            case rtx::marker::kAskQuestStatReqStat:    return { "QUEST_STATREQ_STAT", 2 };
            case rtx::marker::kAskQuestStatReqLevel:   return { "QUEST_STATREQ_LEVEL", 2 };
            case rtx::marker::kAskQuestReqCount:       return { "QUEST_QUESTREQ_COUNT", 1 };
            case rtx::marker::kAskQuestReq:            return { "QUEST_QUESTREQ", 2 };
            case rtx::marker::kAskQuestPointsReq:      return { "QUEST_POINTSREQ", 1 };
            case rtx::marker::kAskQuestDifficulty:     return { "QUEST_GETDIFFICULTY", 1 };
            case rtx::marker::kAskAchievementReqCount: return { "ACHIEVEMENT_ACHIEVEMENT_REQ_COUNT", 1 };
            case rtx::marker::kAskRunescore:           return { "ACHIEVEMENT_TOTAL_RUNESCORE", 0 };
            case rtx::marker::kAskGracedCount:         return { "ACHIEVEMENT_FINDGRACED", 0 };
            case rtx::marker::kAskGracedNext:          return { "ACHIEVEMENT_FINDNEXT", 0 };
            default:                                   return { nullptr, 0 };
        }
    };
    rtx::frame::Share::AskAnswer got[kPerFrame];
    LARGE_INTEGER freq{}, t0{}; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&t0);
    int done_n = 0;
    for (int i = 0; i < n; ++i) {
        const OpFor use = opFor(todo[i].kind);
        std::int32_t out[4] = {};
        const std::int32_t in[2] = { todo[i].id, todo[i].arg };
        const int r = use.name ? Call(root, use.name, in, use.args, nullptr, 0, out, 4) : -1;
        got[i].value = r > 0 ? out[r - 1] : 0;
        got[i].ok = r > 0 ? 1 : 0;
        got[i].tag = todo[i].tag;
        done_n = i + 1;
        LARGE_INTEGER t1{}; QueryPerformanceCounter(&t1);
        if (freq.QuadPart &&
            (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart >= kBudgetMs) break;
    }
    n = done_n;
    if (n <= 0) return;
    bool done = false;
    {
        std::lock_guard<std::mutex> lk(g_askMu);
        // The launcher may have replaced the list while we were answering; those answers are dropped.
        if (seq != g_askSeq || at != g_askAt) return;
        if (at + n > g_askCount) return;
        for (int i = 0; i < n; ++i) g_answer[at + i] = got[i];
        g_askAt = at + n;
        done = g_askAt >= g_askCount;
        g_askDone = done;
        rtx::present::PublishAnswers(g_answer, g_askCount, g_askSeq, done);
    }
}

bool TakeLog(char* out, std::size_t cap) {
    std::lock_guard<std::mutex> lk(g_logMu);
    if (!g_logCount || cap == 0) return false;
    constexpr int kSlots = sizeof(g_log) / sizeof(g_log[0]);
    std::snprintf(out, cap, "%s", g_log[g_logHead]);
    g_logHead = (g_logHead + 1) % kSlots; --g_logCount;
    return true;
}

void SetRootShift(std::int64_t delta) { g_rootShift.store(delta, std::memory_order_relaxed); }

}  // namespace rtx::engineops
