#pragma once
//
// Launcher side of the cache-sound channel (companion/SoundShare.h).
//
// The companion owns the section; this only opens it. Every call is a no-op when the companion
// isn't loaded in that client, so the panel can poll unconditionally.

#include <cstdint>
#include <string>
#include <vector>

namespace rtx::launcher::soundfilter {

// Turn observation on. The companion's mix hook is a near-noop while this is off, so the panel
// sets it only while it is open. Returns false when the channel isn't there.
bool SetEnabled(std::uint32_t pid, bool on);

// Replace the mute list. Entries are PACKED KEYS - (js5 index << 24) | id - so effects and
// music never collide; the panel builds them. Need not be sorted or unique: this sorts and
// de-duplicates before publishing, because the audio path binary-searches the result.
bool SetMuted(std::uint32_t pid, std::vector<int> ids);

// {"ok":bool,"hooked":bool,"enabled":bool,"playRva":n,"muted":n,"seq":n,
//  "recent":[{"n":n,"id":n,"idx":n,"muted":n,"ms":n},...],"diag":[...]}
// `recent` is oldest-first; `seq` is the companion's total-ever count, so the panel can tell
// how many observations it missed between polls. `idx` is the js5 index the id belongs to
// (14 effects / 40 music). Never throws; "{}" when unavailable.
std::string StatusJson(std::uint32_t pid);

}  // namespace rtx::launcher::soundfilter
