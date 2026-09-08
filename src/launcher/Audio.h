#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rtx::audio {

// In-process Ogg Vorbis playback for cache audio (stb_vorbis decode, waveOut mix).

// Decode on a worker and start playing; never blocks the caller. `chunks` are the sound's
// Ogg streams in order (CacheReader::SoundOggChunks), each decoded separately and joined.
bool Play(const std::vector<std::vector<std::uint8_t>>& chunks, int volume_pct);

void Pause();
void Resume();
void Stop();
void Seek(int ms);                       // clamped to the clip
void SetVolume(int volume_pct);          // 0..100, applies live

// {"state":"idle|loading|playing|paused","pos":ms,"dur":ms,"vol":pct,"rate":hz,"ch":n}
std::string StatusJson();

}  // namespace rtx::audio
