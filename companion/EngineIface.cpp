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
