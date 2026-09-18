#pragma once
// Launcher background music (the Halloween soundtrack). Ultralight has no Web Audio and no <audio>, so the page
// asks the launcher to play it: the file is decoded once with Media Foundation into one PCM buffer and played
// through XAudio2 with an infinite loop, which repeats sample-exactly with no gap at the seam.
#include <string>

namespace rtx::launcher::music {

// Starts (or keeps) the loop for `file` (a path next to the exe, e.g. "sounds/spooky-loop.mp3") at `volume`
// (0.0 to 1.0). Decoding happens once per file, off the caller's hot path. Returns false when it could not play.
bool Play(const std::string& file, float volume);
void SetVolume(float volume);
void Stop();
bool Playing();

// The launcher window was minimised (or restored). While minimised the track is silenced and, on restore, it
// comes back at the volume the settings asked for. The page's own choice is untouched.
void Minimised(bool on);

}  // namespace rtx::launcher::music
