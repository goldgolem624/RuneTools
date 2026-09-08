#pragma once
// Launcher side of the cache-sound channel (companion/SoundShare.h). The companion owns the
// section; every call no-ops when it is absent, so the panel can poll unconditionally.

#include <cstdint>
#include <string>
#include <vector>

namespace rtx::launcher::soundfilter {

// Observation on/off; the panel enables it only while open. False when the channel is absent.
bool SetEnabled(std::uint32_t pid, bool on);

// Replace the mute list. Keys are (js5 index << 24) | id; sorted and de-duplicated here
// because the audio path binary-searches them.
bool SetMuted(std::uint32_t pid, std::vector<int> ids);

// {"ok":bool,"hooked":bool,"enabled":bool,"playRva":n,"muted":n,"seq":n,
//  "recent":[{"n":n,"id":n,"idx":n,"muted":n,"ms":n},...],"diag":[...]}
// `recent` is oldest-first; `seq` is the total-ever count; `idx` is the js5 index (14 effects / 40 music).
std::string StatusJson(std::uint32_t pid);

}  // namespace rtx::launcher::soundfilter
