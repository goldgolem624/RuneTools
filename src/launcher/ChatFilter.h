#pragma once
// Launcher side of the NPC chat mute channel (companion/ChatShare.h). The companion owns the
// section; every call no-ops when it is absent.

#include <cstdint>
#include <string>
#include <vector>

namespace rtx::launcher::chatfilter {

// Replace the muted senders and the mode (0 count only, 1 unlisted, 2 removed from the store). Names are lower-cased here.
// An empty list turns the hook into a pass-through.
bool SetMuted(std::uint32_t pid, std::uint32_t mode, std::vector<std::string> names);

// {"ok":bool,"hooked":bool,"enabled":bool,"mode":n,"muted":n,"recent":["name",...],"diag":[...]}
// `recent` is oldest-first.
std::string StatusJson(std::uint32_t pid);

}  // namespace rtx::launcher::chatfilter
