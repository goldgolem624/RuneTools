#pragma once
// Cache-sound observation and muting via the engine's sound-play function (contract in SoundShare.h).

namespace rtx::soundfilter {

bool Install();

// Re-create the share under the current session names. Cheap when unchanged.
void Rebind();

void Uninstall();

}  // namespace rtx::soundfilter
