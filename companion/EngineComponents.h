#pragma once
// Interface components drawn by the game itself.
//
// The client's scripts build their interfaces from components (layers, rectangles, text,
// sprites) through a set of engine operations. Those operations are plain routines that take
// the client's root and a script state: the arguments on the state's integer stack, the
// component being worked on in a slot of the state. Called from the game thread with a state of
// our own, they give us components the game lays out, clips, scrolls and layers exactly as its
// own, under its tooltips and menus.
#include <cstddef>
#include <cstdint>

namespace rtx::enginecc {

// Development probe, run once per frame on the game thread with the client's root. Reads
// %TEMP%\rtx_cc.txt ("iface comp type slot x y w h argb") once and builds that component.
void DevProbe(std::uint8_t* root);
// The rectangles wanted (any thread); Apply, on the game thread each frame, makes the game's
// component tree match: finds each by parent and slot, creates what is missing, updates what
// changed, deletes what is no longer wanted. A parent whose interface is closed is left alone.
struct Rect { std::int32_t parent, slot, x, y, w, h; std::uint32_t argb; std::int32_t font; char text[48]; };   // text[0]: a text component
void Want(const void* rects, std::uint32_t count);   // rtx::marker::CcRect, same layout as Rect
void Apply(std::uint8_t* root);
bool Usable();                                       // the routines were recognised in this client
// Lines for the ring log, one at a time.
bool TakeLog(char* out, std::size_t cap);

}  // namespace rtx::enginecc
