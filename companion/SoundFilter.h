#pragma once
//
// Cache-sound observation and muting (see SoundShare.h for the launcher contract).
//
// Sits on the engine's audio-chunk mix function: publishes the sound id behind each chunk so
// the panel can answer "what was that?", and zeroes the samples of ids the launcher has muted.

namespace rtx::soundfilter {

// Idempotent. Maps the share and attaches the mix hook. Returns false if the mix function
// could not be located, in which case observation and muting stay off and the client is
// untouched. Call from the companion worker thread.
bool Install();

void Uninstall();

}  // namespace rtx::soundfilter
