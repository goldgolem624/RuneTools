#pragma once
// Cache-sound observation and muting via the engine's sound-play function (contract in SoundShare.h).

namespace rtx::soundfilter {

bool Install();

void Uninstall();

}  // namespace rtx::soundfilter
