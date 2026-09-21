#pragma once
// Extra text in the game's own mouseover tooltip.
//
// The tooltip is drawn by a client script that asks one op for the hovered entry's verb and
// target. That op reads the two as plain C strings from the hover object, so the hook hands it
// a copy of the object whose target ends in our text. Nothing of the game's is written to and
// no game memory is allocated: the script lays the longer string out itself, in its own font.

#include <cstdint>

namespace rtx::tooltip {

inline constexpr int kTextMax = 191;

bool Install();
void Uninstall();

// What to append, and to which hover. The hover object holds an item's interface slot and
// (interface id << 16 | component) in the same two fields where it holds a loc's tile x and y,
// so `slot` and `comp` key either. on = false appends nothing.
void Update(bool on, std::int32_t slot, std::uint32_t comp, const char* text);

}  // namespace rtx::tooltip
