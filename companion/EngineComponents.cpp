#include "EngineComponents.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <mutex>
#include <cstdlib>
#include <vector>

namespace rtx::enginecc {
namespace {

// The script state as the operations read it. Only the parts they touch are laid out; the rest
// of the block stays zero. Sizes are generous: the game's own state is larger than the last
// field used here.
constexpr std::size_t kStateSize   = 0x20000;
constexpr std::size_t kFlag        = 0x20;      // byte: 1 = work on the second component slot
constexpr std::size_t kIntStack    = 0x100;     // 1000 ints
constexpr std::size_t kIntSp       = 0x10A0;
constexpr std::size_t kStrStack    = 0x10A8;    // 1000 entries of 0x20
constexpr std::size_t kStrSp       = 0x8DA8;
constexpr std::size_t kActive      = 0xBFC0;    // {control block, component} of the component being worked on
constexpr std::size_t kActiveAlt   = 0xBFE0;

// The operation routines are found, not fixed. The client registers every operation in one
// routine, each with the same two instructions (the operation number, then the address of its
// handler); that gives the full set of handlers. Two of the handlers name themselves in the
// text of their error messages. The setters are all one thin shape around a shared dispatcher,
// differing only in the routine they hand it, and each of those routines is told apart by a
// short run of bytes it alone contains. Every match must be unique or the whole set is refused.
// Routines take the root in rcx and the state in rdx and return the "no error" marker or an error.
using OpFn = void* (*)(void* root, std::uint8_t* state);
struct Op { const char* name; const char* text; const char* fingerprint; OpFn fn; const char* fingerprint2 = nullptr; };   // both fingerprints must match when two are given
Op g_ops[] = {
    { "cc_create",      "_cc_create", nullptr,                          nullptr },
    { "cc_delete",      "_cc_delete", nullptr,                          nullptr },
    { "cc_setposition", nullptr,      "33 C9 49 83 C3 40",              nullptr },   // four ints
    { "cc_setsize",     nullptr,      "41 B9 04 00 00 00 44 39 4A 08",  nullptr },   // four ints, modes clamped to 4
    { "cc_setcolour",   nullptr,      "44 89 88 88 00 00 00",           nullptr },   // one int into the component at +0x88
    { "cc_setfill",     nullptr,      "40 88 B8 88 01 00 00",           nullptr },   // one int, as a byte at +0x188
    { "cc_settrans",    nullptr,      "F6 D1 88 8A 8C 00 00 00",        nullptr },   // one int, inverted, as a byte at +0x8C
    { "cc_sethide",     nullptr,      "41 0F 94 C1 E8",                 nullptr },   // one int, compared with 1, handed on
    { "cc_find",        nullptr,      "B8 C0 BF 00 00 45 8B 82 00 01 00 00 48 83 C2 38", nullptr },   // (component, slot) -> the slot made active; pushes found
    { "cc_settext",     nullptr,      "41 FF 89 A8 8D 00 00 49 81 C1 A8 10 00 00", nullptr },   // one string, from the string stack
    { "cc_settextfont", nullptr,      "89 70 20 41 B9 FF FF 00 00",      nullptr },   // one int into the text object at +0x20
    { "cc_settextshadow", nullptr,    "41 FF 89 A0 10 00 00 33 D2 41 8B 81 A0 10 00 00 41 8B 9C 81 00 01 00 00", nullptr, "0F BA E9 01 88 48 28" },   // one int: sets bit 1 of the text object at +0x28
};
enum { kCreate, kDelete, kSetPosition, kSetSize, kSetColour, kSetFill, kSetTrans, kSetHide, kFind, kSetText, kSetTextFont, kSetTextShadow };

std::uint8_t* g_state = nullptr;
std::uint8_t* g_base = nullptr;
bool g_resolved = false, g_usable = false;
std::mutex g_logMu;
char g_log[400] = {};

void Say(const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_logMu);
    va_list ap; va_start(ap, fmt);
    std::vsnprintf(g_log, sizeof(g_log), fmt, ap);
    va_end(ap);
}

bool ReadHead(const std::uint8_t* at, std::uint8_t* out, std::size_t n) {
    __try { std::memcpy(out, at, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

struct Section { const std::uint8_t* begin; std::size_t size; };
bool FindSection(const char* name, Section& out) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(g_base + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec)
        if (std::strncmp(reinterpret_cast<const char*>(sec->Name), name, 8) == 0) { out = { g_base + sec->VirtualAddress, sec->Misc.VirtualSize }; return true; }
    return false;
}

// "AA BB ?? CC": bytes, ?? a wildcard (-1)
int ParsePattern(const char* pat, int* out, int cap) {
    int n = 0;
    for (const char* p = pat; *p && n < cap; ) {
        while (*p == ' ') ++p;
        if (!*p) break;
        if (p[0] == '?') { out[n++] = -1; p += 2; continue; }
        char two[3] = { p[0], p[1], 0 };
        out[n++] = (int)std::strtoul(two, nullptr, 16); p += 2;
    }
    return n;
}
const std::uint8_t* FindIn(const std::uint8_t* begin, std::size_t size, const int* pat, int n) {
    for (std::size_t i = 0; i + (std::size_t)n <= size; ++i) {
        std::size_t k = 0;
        for (; k < (std::size_t)n; ++k) if (pat[k] >= 0 && begin[i + k] != (std::uint8_t)pat[k]) break;
        if (k == (std::size_t)n) return begin + i;
    }
    return nullptr;
}

// Every handler the registrar names: mov r8w, imm16 ; lea rdx, [rip+rel32]
std::vector<const std::uint8_t*> AllHandlers(const Section& text) {
    std::vector<const std::uint8_t*> out;
    const std::uint8_t* p = text.begin; const std::uint8_t* end = text.begin + text.size - 12;
    for (; p < end; ++p) {
        if (p[0] == 0x66 && p[1] == 0x41 && p[2] == 0xB8 && p[5] == 0x48 && p[6] == 0x8D && p[7] == 0x15) {
            std::int32_t rel; std::memcpy(&rel, p + 8, 4);
            const std::uint8_t* h = p + 12 + rel;
            if (h >= text.begin && h < text.begin + text.size) out.push_back(h);
        }
    }
    return out;
}

// The one handler whose body takes the address of `text`, its own name in its error report.
const std::uint8_t* HandlerNaming(const std::vector<const std::uint8_t*>& handlers, const Section& rdata, const char* text) {
    const std::uint8_t* str = nullptr;
    const std::size_t len = std::strlen(text) + 1;
    for (std::size_t i = 0; i + len <= rdata.size; ++i)
        if (std::memcmp(rdata.begin + i, text, len) == 0 && (i == 0 || rdata.begin[i - 1] == 0)) { if (str) return nullptr; str = rdata.begin + i; }
    if (!str) return nullptr;
    const std::uint8_t* found = nullptr;
    for (const std::uint8_t* h : handlers) {
        for (std::size_t i = 0; i < 0x300; ++i) {
            if (h[i] == 0x48 && h[i + 1] == 0x8D && (h[i + 2] == 0x15 || h[i + 2] == 0x0D || h[i + 2] == 0x05)) {
                std::int32_t rel; std::memcpy(&rel, h + i + 3, 4);
                if (h + i + 7 + rel == str) { if (found && found != h) return nullptr; found = h; break; }
            }
        }
    }
    return found;
}

// A setter: push rbx ; sub rsp,60 ; lea rax,[vtable] ; mov [rsp+20],rax ; lea r8,[rsp+20] ; lea rax,[callback]
// ... ; call dispatcher. The same shape serves the operations that name a component by id and the
// ones that work on the component in the state's slot; they differ in the dispatcher called, and
// only the latter is wanted: it begins by choosing between the two slots, 0xBFC0 and 0xBFE0.
const std::uint8_t* WrapperCallback(const std::uint8_t* h) {
    if (h[0] == 0x40) ++h;                          // the push carries a REX prefix in this build
    static const std::uint8_t head[] = { 0x53, 0x48, 0x83, 0xEC, 0x60, 0x48, 0x8D, 0x05 };
    static const std::uint8_t mid[]  = { 0x48, 0x89, 0x44, 0x24, 0x20, 0x4C, 0x8D, 0x44, 0x24, 0x20, 0x48, 0x8D, 0x05 };
    if (std::memcmp(h, head, sizeof(head)) != 0 || std::memcmp(h + 12, mid, sizeof(mid)) != 0) return nullptr;
    // the call: mov [rsp+28],rax ; lea rax,[rsp+20] ; mov [rsp+58],rax ; call rel32
    static const std::uint8_t tail[] = { 0x48, 0x89, 0x44, 0x24, 0x28, 0x48, 0x8D, 0x44, 0x24, 0x20, 0x48, 0x89, 0x44, 0x24, 0x58, 0xE8 };
    if (std::memcmp(h + 29, tail, sizeof(tail)) != 0) return nullptr;
    std::int32_t drel; std::memcpy(&drel, h + 45, 4);
    const std::uint8_t* dispatcher = h + 49 + drel;
    static const int slots[] = { 0xB8, 0xC0, 0xBF, 0x00, 0x00, 0x41, 0xB9, 0xE0, 0xBF, 0x00, 0x00 };
    if (!FindIn(dispatcher, 0x20, slots, 11)) return nullptr;
    std::int32_t rel; std::memcpy(&rel, h + 25, 4);
    return h + 29 + rel;
}

bool ResolveGuarded(const Section& text, const Section& rdata, std::vector<const std::uint8_t*>& handlers) {
    (void)text;
    __try {
        if (handlers.size() < 1500) { Say("cc: only %zu handlers found, the registrar has changed shape", handlers.size()); return false; }
        for (auto& op : g_ops) {
            const std::uint8_t* h = nullptr;
            if (op.text) {
                h = HandlerNaming(handlers, rdata, op.text);
            } else {
                int pat[32]; const int n = ParsePattern(op.fingerprint, pat, 32);
                for (const std::uint8_t* cand : handlers) {
                    const std::uint8_t* cb = WrapperCallback(cand);
                    if (!cb) cb = cand;                          // a handler that does its own work, matched on its body
                    // only this routine's own body: it ends at its return, followed by the padding
                    // before the next routine, or the bytes of that next routine would count too
                    std::size_t len = 0x80;
                    for (std::size_t k = 0; k + 1 < 0x80; ++k) if (cb[k] == 0xC3 && cb[k + 1] == 0xCC) { len = k + 1; break; }
                    if (!FindIn(cb, len, pat, n)) continue;
                    if (op.fingerprint2) { int pat2[32]; const int n2 = ParsePattern(op.fingerprint2, pat2, 32); if (!FindIn(cb, len, pat2, n2)) continue; }
                    if (h && h != cand) { Say("cc: %s matches more than one handler", op.name); return false; }
                    h = cand;
                }
            }
            if (!h) { Say("cc: %s not found among %zu handlers", op.name, handlers.size()); return false; }
            op.fn = reinterpret_cast<OpFn>(const_cast<std::uint8_t*>(h));
        }
        Say("cc: %zu handlers, create exe+%llx, delete exe+%llx, setters found", handlers.size(),
            (unsigned long long)(reinterpret_cast<std::uint8_t*>(g_ops[kCreate].fn) - g_base), (unsigned long long)(reinterpret_cast<std::uint8_t*>(g_ops[kDelete].fn) - g_base));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void Resolve() {
    g_resolved = true;
    g_base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    Section text, rdata;
    if (!g_base || !FindSection(".text", text) || !FindSection(".rdata", rdata)) return;
    std::vector<const std::uint8_t*> handlers = AllHandlers(text);   // the image is mapped and readable throughout
    if (!ResolveGuarded(text, rdata, handlers)) return;
    g_state = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, kStateSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    g_usable = g_state != nullptr;
}

// A string on the state's string stack: a 32 byte entry, the characters inline while they fit (23
// with the terminator), else a pointer at +0 with the top bit of +0x17 set; the kind byte at +0x18
// is 2 for a string. The setter reads it as a C string.
void PushStr(const char* s) {
    std::uint32_t& sp = *reinterpret_cast<std::uint32_t*>(g_state + kStrSp);
    if (sp >= 1000) return;
    std::uint8_t* e = g_state + kStrStack + (std::size_t)sp * 0x20;
    std::memset(e, 0, 0x20);
    const std::size_t n = std::strlen(s);
    if (n < 0x17) { std::memcpy(e, s, n + 1); }
    else { std::memcpy(e, &s, 8); e[0x17] = 0x80; }
    e[0x18] = 2;
    ++sp;
}
void Push(std::int32_t v) {
    std::uint32_t& sp = *reinterpret_cast<std::uint32_t*>(g_state + kIntSp);
    if (sp < 1000) { *reinterpret_cast<std::int32_t*>(g_state + kIntStack + sp * 4) = v; ++sp; }
}
void ResetStacks() {
    *reinterpret_cast<std::uint32_t*>(g_state + kIntSp) = 0;
    *reinterpret_cast<std::uint32_t*>(g_state + kStrSp) = 0;
    g_state[kFlag] = 0;
}

// What a routine returned: the game's "no error" marker is a static address; anything else is
// an error object whose text is read for the log.
const char* Describe(void* r, char* buf, std::size_t cap) {
    if (!r) { std::snprintf(buf, cap, "null"); return buf; }
    const std::uint8_t* p = static_cast<const std::uint8_t*>(r);
    std::uint8_t peek[48];
    if (!ReadHead(p, peek, sizeof(peek))) { std::snprintf(buf, cap, "%p (unreadable)", r); return buf; }
    // static "no error" object: its first bytes are not text
    bool text = true;
    for (int i = 0; i < 8 && text; ++i) if (peek[i] == 0 || peek[i] < 0x20 || peek[i] > 0x7E) text = false;
    if (text) { std::snprintf(buf, cap, "\"%.40s\"", reinterpret_cast<const char*>(peek)); return buf; }
    std::snprintf(buf, cap, "%p (%02x %02x %02x %02x)", r, peek[0], peek[1], peek[2], peek[3]);
    return buf;
}

void* Call(int which, void* root) {
    __try { return g_ops[which].fn(root, g_state); } __except (EXCEPTION_EXECUTE_HANDLER) { return reinterpret_cast<void*>(~0ull); }
}


}  // namespace


namespace {
std::mutex g_wantMu;
std::vector<Rect> g_want;                 // from the launcher, any thread
std::vector<Rect> g_have;                 // what the game tree holds, game thread only
std::uint32_t g_wantGen = 0, g_haveGen = ~0u;

bool Find(void* root, std::int32_t parent, std::int32_t slot) {
    ResetStacks(); std::memset(g_state + kActive, 0, 0x40);
    Push(parent); Push(slot);
    Call(kFind, root);
    const std::uint32_t sp = *reinterpret_cast<std::uint32_t*>(g_state + kIntSp);
    void* comp = nullptr; std::memcpy(&comp, g_state + kActive + 8, 8);
    return sp == 1 && *reinterpret_cast<std::int32_t*>(g_state + kIntStack) == 1 && comp != nullptr;
}
char g_textKeep[96][48];   // the strings handed to the setter, alive until it has read them
void Shape(void* root, const Rect& r) {
    ResetStacks(); Push(r.x); Push(r.y); Push(0); Push(0); Call(kSetPosition, root);
    ResetStacks(); Push(r.w); Push(r.h); Push(0); Push(0); Call(kSetSize, root);
    ResetStacks(); Push((std::int32_t)(r.argb & 0xFFFFFF)); Call(kSetColour, root);
    if (r.text[0]) {
        static unsigned s_keep = 0; char* keep = g_textKeep[s_keep++ % 96];
        std::memcpy(keep, r.text, sizeof(r.text)); keep[47] = 0;
        ResetStacks(); PushStr(keep); Call(kSetText, root);
        ResetStacks(); Push(r.font > 0 ? r.font : 26); Call(kSetTextFont, root);
        ResetStacks(); Push(1); Call(kSetTextShadow, root);
    } else {
        ResetStacks(); Push(1); Call(kSetFill, root);
    }
    ResetStacks(); Push((std::int32_t)(255 - ((r.argb >> 24) & 0xFF))); Call(kSetTrans, root);
    ResetStacks(); Push(r.w > 0 && r.h > 0 ? 0 : 1); Call(kSetHide, root);
}
bool Same(const Rect& a, const Rect& b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h && a.argb == b.argb && a.font == b.font && std::strncmp(a.text, b.text, sizeof(a.text)) == 0; }
}  // namespace

void Want(const void* rects, std::uint32_t count) {
    std::lock_guard<std::mutex> lk(g_wantMu);
    const Rect* r = static_cast<const Rect*>(rects);
    if (count > 96) count = 96;
    if (g_want.size() == count && (count == 0 || std::memcmp(g_want.data(), r, count * sizeof(Rect)) == 0)) return;
    g_want.assign(r, r + count);
    ++g_wantGen;
}

bool Usable() { return g_usable; }

void Apply(std::uint8_t* root) {
    if (!root) return;
    if (!g_resolved) Resolve();
    if (!g_usable) return;
    std::vector<Rect> want;
    {
        std::lock_guard<std::mutex> lk(g_wantMu);
        if (g_wantGen == g_haveGen) {
            // nothing changed on the launcher's side: only re-create what the game dropped (a
            // panel closed and opened again), checked at a gentle rate
            static unsigned s_tick = 0;
            if (++s_tick % 15 != 0) return;
        }
        want = g_want; g_haveGen = g_wantGen;
    }
    // what is no longer wanted goes first, so a slot can be reused in the same frame
    for (const Rect& h : g_have) {
        bool still = false;
        for (const Rect& w : want) if (w.parent == h.parent && w.slot == h.slot) { still = true; break; }
        if (!still && Find(root, h.parent, h.slot)) { ResetStacks(); Call(kDelete, root); }
    }
    std::vector<Rect> have;
    for (const Rect& w : want) {
        if (w.slot < 0xE00 || w.slot >= 0xFB00) continue;              // only our own dynamic ids are ever touched
        const Rect* old = nullptr;
        for (const Rect& h : g_have) if (h.parent == w.parent && h.slot == w.slot) { old = &h; break; }
        if (Find(root, w.parent, w.slot)) {
            if (!old || !Same(*old, w)) Shape(root, w);
            have.push_back(w);
        } else {
            ResetStacks(); std::memset(g_state + kActive, 0, 0x40);
            Push(w.parent); Push(w.text[0] ? 4 : 3); Push(w.slot);   // 3 a rectangle, 4 text
            Call(kCreate, root);
            void* comp = nullptr; std::memcpy(&comp, g_state + kActive + 8, 8);
            if (!comp) continue;                                           // the interface is not open: try again later
            Shape(root, w);
            have.push_back(w);
        }
    }
    static std::size_t s_saidWant = ~(std::size_t)0, s_saidHave = ~(std::size_t)0;
    if (want.size() != s_saidWant || have.size() != s_saidHave) {
        s_saidWant = want.size(); s_saidHave = have.size();
        Say("cc: %zu rectangles wanted, %zu in the game's tree%s", want.size(), have.size(),
            want.empty() ? "" : (have.empty() ? " (create returned no component: is the interface open?)" : ""));
    }
    g_have.swap(have);
    std::memset(g_state + kActive, 0, 0x40);
}

bool TakeLog(char* out, std::size_t cap) {
    std::lock_guard<std::mutex> lk(g_logMu);
    if (!g_log[0] || cap == 0) return false;
    std::snprintf(out, cap, "%s", g_log);
    g_log[0] = 0;
    return true;
}

}  // namespace rtx::enginecc
