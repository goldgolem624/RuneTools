#pragma once

#include <cstdint>
#include <string>

// Live item-icon capture: the client renders item icons into a 1536x1536 GPU atlas and
// never keeps CPU copies, so the launcher asks the companion (companion/IconAtlasShare.h)
// to glGetTexImage it, maps atlas cells to item ids through the reader
// (rtx::reader::ReadIconAtlasMap) and slices PNGs into
//   %USERPROFILE%\RuneToolsX\icons\<clientVersion>\<itemId>.png        (flavour 0x02, 36x32)
//   %USERPROFILE%\RuneToolsX\icons\<clientVersion>\<itemId>_f<xx>.png  (other flavours)
// IconCache consults that directory before items.pack. See docs/icon-capture.md.

namespace rtx::launcher::iconcapture {

// One synchronous capture for `pid` (<= ~2.5 s: reader walk + hook round trip + PNG
// writes). Returns the number of PNGs written (0 on any failure; see StatusJson).
int CaptureIcons(std::uint32_t pid);

// Start a capture on a background thread unless one is running. Returns false when busy.
bool CaptureAsync(std::uint32_t pid);

// Called by IconCache when an icon request missed the pack and the captured set. Rate
// limited: at most one background capture every 10 s, only while a client is attached.
void NoteMiss(int item_id);

// Path of a captured PNG for the id, or "" (memoized per id; captures invalidate their ids).
std::wstring FindCapturedPng(int item_id);

// {"lastAt":epochMs,"written":N,"cells":N,"status":"..","sessionWritten":N,"busy":0|1,"dir":".."}
std::string StatusJson();

}  // namespace rtx::launcher::iconcapture
