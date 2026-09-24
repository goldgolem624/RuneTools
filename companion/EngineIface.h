#pragma once
// Components of the client's own interfaces, created and driven through the scripting engine.
//
// The client addresses a component as one int: the interface group in the high half, the component
// index in the low half. Every styling operation comes in two forms, one taking that int and one
// acting on the engine's current component; only the first form is used here, so nothing depends on
// a context we did not set.
//
// A component we make is a child of a layer, keyed by a category and an id within it. The client
// refuses to delete a component it owns, so nothing here can remove or damage the game's own
// interfaces: the only components these calls can take away are the ones they made.
#include <cstddef>
#include <cstdint>

namespace rtx::engineiface {

constexpr int kTypeLayer   = 0;
constexpr int kTypeText    = 4;
constexpr int kTypeGraphic = 5;

// Fonts are small ids, not the cache keys they look like: 1, 2 and 5 through 12 all draw,
// checked by laying a line out in each and reading the screen. An id that names no font leaves
// the component with nothing to draw and reports nothing.
constexpr int kFontDefault = 1;

// How far the table sweep in the check goes, a slice per second so no frame carries it all.
constexpr int kDbSweepMax = 1024;

// Category 0 holds 4096 ids; every other category holds 256. The client composes the two into the
// component index the group then knows the child by.
constexpr int kMaxCategory = 0xEA;
int IndexOf(int category, int id);

// Makes a child of `parentComp` in `group`. Returns its component index, or -1.
int Create(std::uint8_t* root, int group, int parentComp, int type, int category, int id);

// Making a child leaves it as the engine's current component, so these style what was just made
// without naming it. The script state is kept between calls, so that choice holds until something
// else replaces it.
bool CurPosition(std::uint8_t* root, int x, int y, int xMode, int yMode);
bool CurSize(std::uint8_t* root, int w, int h, int wMode, int hMode);
bool CurText(std::uint8_t* root, const char* text);
bool CurTextFont(std::uint8_t* root, int font);
bool CurColour(std::uint8_t* root, int rgb);
bool CurHide(std::uint8_t* root, bool hide);

bool SetPosition(std::uint8_t* root, int group, int comp, int x, int y, int xMode, int yMode);
bool SetSize(std::uint8_t* root, int group, int comp, int w, int h, int wMode, int hMode);
bool SetText(std::uint8_t* root, int group, int comp, const char* text);
bool SetColour(std::uint8_t* root, int group, int comp, int rgb);
bool SetHide(std::uint8_t* root, int group, int comp, bool hide);
// Takes away every component we made under `parentComp`; the client's own children are untouched.
bool DeleteAll(std::uint8_t* root, int group, int parentComp);

// The client's own indexed queries over its cache tables. A count is the whole contract for one
// table with no filter: the filter handle is the engine's own list of built filters, and -1 asks
// for every row. Returns -1 when the operation is not recognised or the call faulted.
int DbRowCount(std::uint8_t* root, int table, int take, int skip, int* why = nullptr);

bool TakeLog(char* out, std::size_t cap);

}  // namespace rtx::engineiface
