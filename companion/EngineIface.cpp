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
int DbRowCount(std::uint8_t* root, int table) {
    std::int32_t ints[5] = { -1, (std::int32_t)table, 0, -1, 0 };
    std::int32_t out[4] = {0};
    const int n = rtx::engineops::Call(root, "DBQUERY_EXECUTE_COUNT", ints, 5, nullptr, 0, out, 4);
    if (n < 1) { Say("dbcount: DBQUERY_EXECUTE_COUNT gave nothing back"); return -1; }
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
    const ULONGLONG now = GetTickCount64();
    if (now - s_looked > 3000) {
        s_looked = now;
        wchar_t tmp[MAX_PATH] = {}; GetTempPathW(MAX_PATH, tmp);
        const std::wstring path = std::wstring(tmp) + L"rtx_iface.txt";
        std::string line;
        HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            char buf[512] = {0}; DWORD got = 0;
            if (ReadFile(f, buf, sizeof(buf) - 1, &got, nullptr) && got) line.assign(buf, got);
            CloseHandle(f);
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

    if (!s_made) {
        s_comp = Create(root, group, parent, type, cat, id);
        if (s_comp < 0) { Say("check: nothing was made"); return; }
        s_group = group; s_parent = parent; s_made = true;
        SetPosition(root, group, s_comp, x, y, 0, 0);
        SetSize(root, group, s_comp, w, h, 0, 0);
        SetColour(root, group, s_comp, rgb);
        SetHide(root, group, s_comp, false);
        Say("check: made %d:%d under %d:%d", group, s_comp, group, parent);
    }
    char line[160];
    std::snprintf(line, sizeof(line), "%s %llu", text[0] ? text : "RuneTools",
                  (unsigned long long)((now / 1000) % 1000));
    SetText(root, s_group, s_comp, line);

    // One table counted the same way, so the query side is checked on the same run.
    static bool s_counted = false;
    if (!s_counted) {
        s_counted = true;
        const int rows = DbRowCount(root, 0);
        Say("check: table 0 counted %d rows", rows);
    }
}

}  // namespace rtx::engineiface
