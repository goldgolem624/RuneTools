#include "EngineIface.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "EngineOps.h"

namespace rtx::engineiface {
namespace {

std::mutex g_logMu;
char       g_log[1024] = {0};

void Say(const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_logMu);
    std::size_t used = std::strlen(g_log);
    if (used && used + 4 < sizeof(g_log)) { std::memcpy(g_log + used, " | ", 4); used += 3; }
    va_list ap; va_start(ap, fmt);
    std::vsnprintf(g_log + used, sizeof(g_log) - used, fmt, ap);
    va_end(ap);
}

// The one int every operation takes for "which component": group above, index below.
std::int32_t Packed(int group, int comp) {
    return (std::int32_t)(((std::uint32_t)group << 16) | ((std::uint32_t)comp & 0xFFFFu));
}

// The operations taking a component take it last, because the shared front of every one of them
// pops it before the operation's own arguments are read.
bool CallWith(std::uint8_t* root, const char* op, int group, int comp,
              const std::int32_t* args, int nArgs, const char* text) {
    std::int32_t ints[8];
    if (nArgs < 0 || nArgs > 7) return false;
    for (int i = 0; i < nArgs; ++i) ints[i] = args[i];
    ints[nArgs] = Packed(group, comp);
    const char* strs[1] = { text };
    std::int32_t out[4];
    const int n = rtx::engineops::Call(root, op, ints, nArgs + 1,
                                       text ? strs : nullptr, text ? 1 : 0, out, 4);
    if (n < 0) { Say("%s: not recognised or faulted", op); return false; }
    return true;
}

}  // namespace

int IndexOf(int category, int id) {
    if (category < 0 || category > kMaxCategory) return -1;
    if (id < 0) return -1;
    if (id >= (category == 0 ? 0x1000 : 0x100)) return -1;
    return category > 0 ? id + (category + 15) * 0x100 : id;
}

// Making a child leaves it as the engine's current component: the client writes it into the script
// state as the last thing it does. So a new component is styled with the operations that act on the
// current one, and there is no index to work out. The state the calls run through is one buffer that
// is kept between them, so that choice stands until the next thing replaces it.
int Create(std::uint8_t* root, int group, int parentComp, int type, int category, int id) {
    const int index = IndexOf(category, id);
    if (index < 0) { Say("create: category %d id %d is out of range", category, id); return -1; }
    std::int32_t ints[4] = { (std::int32_t)type, (std::int32_t)category, (std::int32_t)id,
                             Packed(group, parentComp) };
    std::int32_t out[4];
    const int n = rtx::engineops::Call(root, "IF_CREATECHILD", ints, 4, nullptr, 0, out, 4);
    if (n < 0) { Say("create: IF_CREATECHILD not recognised or faulted"); return -1; }
    return index;
}

// The current-component forms, for the child just made.
namespace {
bool CallCur(std::uint8_t* root, const char* op, const std::int32_t* args, int nArgs, const char* text) {
    std::int32_t out[4];
    const char* strs[1] = { text };
    const int n = rtx::engineops::Call(root, op, args, nArgs, text ? strs : nullptr, text ? 1 : 0, out, 4);
    if (n < 0) { Say("%s: not recognised or faulted", op); return false; }
    return true;
}
}  // namespace

bool CurPosition(std::uint8_t* root, int x, int y, int xMode, int yMode) {
    const std::int32_t a[4] = { x, y, xMode, yMode };
    return CallCur(root, "CC_SETPOSITION", a, 4, nullptr);
}
bool CurSize(std::uint8_t* root, int w, int h, int wMode, int hMode) {
    const std::int32_t a[4] = { w, h, wMode, hMode };
    return CallCur(root, "CC_SETSIZE", a, 4, nullptr);
}
bool CurText(std::uint8_t* root, const char* text) {
    return CallCur(root, "CC_SETTEXT", nullptr, 0, text ? text : "");
}
bool CurTextFont(std::uint8_t* root, int font) {
    const std::int32_t a[1] = { font };
    return CallCur(root, "CC_SETTEXTFONT", a, 1, nullptr);
}
bool CurColour(std::uint8_t* root, int rgb) {
    const std::int32_t a[1] = { rgb & 0xFFFFFF };
    return CallCur(root, "CC_SETCOLOUR", a, 1, nullptr);
}
bool CurHide(std::uint8_t* root, bool hide) {
    const std::int32_t a[1] = { hide ? 1 : 0 };
    return CallCur(root, "CC_SETHIDE", a, 1, nullptr);
}

bool SetPosition(std::uint8_t* root, int group, int comp, int x, int y, int xMode, int yMode) {
    const std::int32_t a[4] = { x, y, xMode, yMode };
    return CallWith(root, "IF_SETPOSITION", group, comp, a, 4, nullptr);
}

bool SetSize(std::uint8_t* root, int group, int comp, int w, int h, int wMode, int hMode) {
    const std::int32_t a[4] = { w, h, wMode, hMode };
    return CallWith(root, "IF_SETSIZE", group, comp, a, 4, nullptr);
}

bool SetText(std::uint8_t* root, int group, int comp, const char* text) {
    return CallWith(root, "IF_SETTEXT", group, comp, nullptr, 0, text ? text : "");
}

bool SetColour(std::uint8_t* root, int group, int comp, int rgb) {
    const std::int32_t a[1] = { rgb & 0xFFFFFF };
    return CallWith(root, "IF_SETCOLOUR", group, comp, a, 1, nullptr);
}

bool SetHide(std::uint8_t* root, int group, int comp, bool hide) {
    const std::int32_t a[1] = { hide ? 1 : 0 };
    return CallWith(root, "IF_SETHIDE", group, comp, a, 1, nullptr);
}

bool DeleteAll(std::uint8_t* root, int group, int parentComp) {
    std::int32_t ints[1] = { Packed(group, parentComp) };
    std::int32_t out[4];
    return rtx::engineops::Call(root, "CC_DELETEALL", ints, 1, nullptr, 0, out, 4) >= 0;
}

// Arguments in the order the operation reads them off the stack: the filter to apply (-1 for every
// row), the table, whether the count is of distinct rows, how many to take and how many to skip.
int DbRowCount(std::uint8_t* root, int table, int take, int skip, int* why) {
    std::int32_t ints[5] = { -1, (std::int32_t)table, 0, (std::int32_t)take, (std::int32_t)skip };
    std::int32_t out[4] = {0};
    const int n = rtx::engineops::Call(root, "DBQUERY_EXECUTE_COUNT", ints, 5, nullptr, 0, out, 4);
    if (why) *why = n;          // -1 the call faulted, 0 it came back leaving nothing, else a count
    if (n < 1) return -1;
    return out[0];
}

bool TakeLog(char* out, std::size_t cap) {
    std::lock_guard<std::mutex> lk(g_logMu);
    if (!g_log[0] || !out || !cap) return false;
    std::snprintf(out, cap, "%s", g_log);
    g_log[0] = 0;
    return true;
}

}  // namespace rtx::engineiface

// ---------------------------------------------------------------------------------------------
// A check that can be run on the live game. With %TEMP%\rtx_iface.txt present, the line inside it
// names a layer and a component to make under it:
//
//   <group> <parent component> <type> <category> <id> <x> <y> <w> <h> <rgb> <text...>
//
// The component is made once and its text refreshed every second, so both the making and the
// driving of it are visible. Removing the file takes it away again on the next look.
#include <string>
#include <windows.h>

namespace rtx::engineiface {

void DevComponent(std::uint8_t* root) {
    static ULONGLONG s_next = 0, s_looked = 0;
    static bool s_on = false, s_made = false;
    static int  s_group = 0, s_parent = 0, s_comp = -1;
    static std::string s_spec, s_text;
    // Nothing at all runs until the engine is up. This is on the thread that draws, so a call made
    // while the client is still building itself costs a frame at best; the first minute of a launch
    // is when the client can least afford one.
    const ULONGLONG now = GetTickCount64();
    static const ULONGLONG s_first = now;
    // Says where it stopped, once for each reason, so an idle check is never silent.
    static bool s_saidRoot = false, s_saidReady = false, s_saidWait = false, s_saidGo = false;
    if (!root) { if (!s_saidRoot) { s_saidRoot = true; Say("check: no root yet"); } return; }
    if (now - s_first < 20000) {
        if (!s_saidWait) { s_saidWait = true; Say("check: holding off for the first 20 s of this run"); }
        return;
    }
    if (!rtx::engineops::Ready()) {
        if (!s_saidReady) { s_saidReady = true; Say("check: the engine operations are not resolved yet"); }
        return;
    }
    if (!s_saidGo) { s_saidGo = true; Say("check: running"); }
    if (now - s_looked > 3000) {
        s_looked = now;
        // Reading a file is not this thread's work: the path is looked at once and the answer is
        // remembered, never opened on the frame that draws.
        wchar_t tmp[MAX_PATH] = {}; GetTempPathW(MAX_PATH, tmp);
        const std::wstring path = std::wstring(tmp) + L"rtx_iface.txt";
        std::string line;
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
            if (f != INVALID_HANDLE_VALUE) {
                char buf[512] = {0}; DWORD got = 0;
                if (ReadFile(f, buf, sizeof(buf) - 1, &got, nullptr) && got) line.assign(buf, got);
                CloseHandle(f);
            }
        }
        const bool was = s_on;
        s_on = !line.empty();
        // The spec changed, or the file went away: drop what we made and start again.
        if (line != s_spec || (was && !s_on)) {
            if (s_made && s_group) {
                DeleteAll(root, s_group, s_parent);
                Say("check: removed what we made under %d:%d", s_group, s_parent);
            }
            s_made = false; s_comp = -1; s_spec = line;
        }
    }
    if (!s_on || now < s_next) return;
    s_next = now + 1000;

    int group = 0, parent = 0, type = kTypeText, cat = 0, id = 0;
    int x = 10, y = 10, w = 200, h = 20, rgb = 0xFFFFFF;
    char text[128] = {0};
    const int got = std::sscanf(s_spec.c_str(), "%d %d %d %d %d %d %d %d %d %x %127[^\r\n]",
                                &group, &parent, &type, &cat, &id, &x, &y, &w, &h, &rgb, text);
    if (got < 5 || group <= 0) { Say("check: the line needs at least group, parent, type, category, id"); return; }

    char line[160];
    std::snprintf(line, sizeof(line), "%s %llu", text[0] ? text : "RuneTools",
                  (unsigned long long)((now / 1000) % 1000));
    // Made once. Everything after it acts on the component the making left current, so the whole
    // set has to run together while that choice still stands.
    if (!s_made) {
        s_comp = Create(root, group, parent, type, cat, id);
        if (s_comp < 0) { Say("check: nothing was made"); return; }
        s_group = group; s_parent = parent; s_made = true;
        // Two ways of naming what was just made, reported separately: the operations that act on the
        // component the client left current, and the ones that take the component by its index. The
        // index is the one the client itself composes from the category and the id.
        // Whether the making left anything selected at all, which is what every operation below
        // depends on and what a silent no-op would look like.
        const void* sel = rtx::engineops::CurrentComponent();
        const bool p = CurPosition(root, x, y, 0, 0);
        const bool z = CurSize(root, w, h, 0, 0);
        const bool c = CurColour(root, rgb);
        const bool v = CurHide(root, false);
        // Text only means anything on a text component, and it draws nothing without a font.
        bool f = true, t = true;
        if (type == kTypeText) {
            f = CurTextFont(root, kFontDefault);
            t = CurText(root, line);
        }
        Say("check: made under %d:%d as index %d, selected %s, pos %d size %d colour %d shown %d font %d text %d",
            group, parent, s_comp, sel ? "yes" : "no",
            (int)p, (int)z, (int)c, (int)v, (int)f, (int)t);
        return;
    }
    // Later updates: making it again names it once more, and the client refuses the duplicate
    // without disturbing what is there, so the text lands on the one that already exists.
    if (type == kTypeText && Create(root, group, parent, type, cat, id) >= 0) CurText(root, line);

    // One table counted the same way, so the query side is checked on the same run.
    // Which ids name a real table is not settled. The count is asked for one row at a time, which
    // is the smallest amount of work the operation will do, and the ids it answers for are reported.
    // A limit of 0 is answered without touching the table at all, so it says nothing either way.
    static int s_sweep = 0;
    if (s_sweep <= kDbSweepMax) {
        std::string found;
        int s_why = 0;
        const int first = s_sweep;
        for (int n = 0; n < 64 && s_sweep <= kDbSweepMax; ++n, ++s_sweep) {
            int why = 0;
            const int rows = DbRowCount(root, s_sweep, 1, 0, &why);
            if (s_sweep == first) s_why = why;   // whether it faults or simply answers nothing
            if (rows >= 0) {
                char b[32];
                std::snprintf(b, sizeof(b), "%s%d:%d", found.empty() ? "" : " ", s_sweep, rows);
                found += b;
            }
        }
        if (!found.empty()) Say("dbcount: %d..%d answered %s", first, s_sweep - 1, found.c_str());
        else if (s_sweep > kDbSweepMax)
            Say("dbcount: nothing answered up to %d; the call %s", kDbSweepMax,
                s_why < 0 ? "faulted" : "came back leaving nothing on the stack");
    }
}

}  // namespace rtx::engineiface
