#pragma once
// The game's own hover outline. The engine outlines what is under the cursor, animated and
// exact, but a definition can opt a loc out of it, and most scenery does (trees among them).
// One byte in the client lifts that opt-out for everything; this finds it and sets it.

#include <cstddef>
#include <cstdint>

namespace rtx::enginehl {

enum Status : std::uint32_t {
    kUnknown  = 0,   // not looked for yet
    kActive   = 1,   // found; the byte follows Set()
    kNotFound = 2,   // the code it is read from was not recognised in this build
};

// The game's own entity highlight, driven from our side. It is one table of eight records, each a
// mode byte, a scale byte and a colour, and the game writes the same table from its own settings
// page, so everything here is put back the way the game had it when the feature is turned off.
//
//   mode   0 mouseover, 1 proximity, 2 always on, 3 off        kModeKeep leaves the game's own
//   scale  0 silhouette (fills the entity), 1..255 border size  kScaleKeep leaves the game's own
//   rgb    0xRRGGBB                                             0 leaves the game's own
//
// Cheap when nothing changes; safe to call every frame. Returns the status.
inline constexpr int kCategories = 8;
inline constexpr int kCatOwnGroup = 1, kCatPlayers = 2, kCatNpcs = 3, kCatAttackable = 4, kCatScenery = 5;
inline constexpr std::int32_t kModeKeep = -1, kScaleKeep = -1;
inline constexpr std::int32_t kModeMouseover = 0, kModeProximity = 1, kModeAlwaysOn = 2, kModeOffValue = 3;
inline constexpr std::int32_t kMaxScale = 255;
Status Set(bool on, const std::uint32_t* rgb, const std::int32_t* scale, const std::int32_t* mode);

// Puts the byte and the colours back the way the game had them. Called on unload.
void Restore();

// What this found and what it did with it, for the check. One message at a time, cleared as it is
// taken; empty when there is nothing new to say.
bool TakeLog(char* out, std::size_t cap);

}  // namespace rtx::enginehl
