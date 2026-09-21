#pragma once
// The game's own hover outline. The engine outlines what is under the cursor, animated and
// exact, but a definition can opt a loc out of it, and most scenery does (trees among them).
// One byte in the client lifts that opt-out for everything; this finds it and sets it.

#include <cstdint>

namespace rtx::enginehl {

enum Status : std::uint32_t {
    kUnknown  = 0,   // not looked for yet
    kActive   = 1,   // found; the byte follows Set()
    kNotFound = 2,   // the code it is read from was not recognised in this build
};

// Turns the engine's outline on for every hoverable entity, or back to the game's default.
// `rgb` holds one colour (0xRRGGBB) per highlight category that replaces the game's while on;
// 0 keeps the game's own for that category. `thickness` holds one outline width per category the
// same way (the game uses 4); 0 keeps the game's. Cheap when nothing changes; safe to call every
// frame. Returns the status.
inline constexpr int kCategories = 8;
inline constexpr int kCatOwnGroup = 1, kCatPlayers = 2, kCatNpcs = 3, kCatAttackable = 4, kCatScenery = 5;
inline constexpr std::uint32_t kMaxThickness = 16;
Status Set(bool on, const std::uint32_t* rgb, const std::uint32_t* thickness);

// Puts the byte and the colours back the way the game had them. Called on unload.
void Restore();

}  // namespace rtx::enginehl
