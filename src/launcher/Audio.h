#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rtx::audio {

// Ogg Vorbis playback for cache audio, in-process.
//
// The cache stores every sound effect and music track as Ogg Vorbis (CacheReader::SoundOgg).
// Windows ships no Vorbis codec - verified: no decoder DLL, and MediaPlayer reports
// HasAudio=false for these files - and the panel's renderer has no media element, so both the
// decode (stb_vorbis) and the mixing (waveOut) happen here.
//
// waveOut rather than PlaySound because a player needs to pause, seek and report where it is;
// PlaySound can only fire and forget.

// Decode and start playing. Returns false only if the bytes are not decodable - the decode
// itself runs on a worker, so this never blocks the caller.
// `chunks` are the sound's Ogg streams in order (see CacheReader::SoundOggChunks). Each is a
// complete stream, so they are decoded separately and their PCM joined - passing the
// concatenated bytes to a decoder would yield only the first chunk.
bool Play(const std::vector<std::vector<std::uint8_t>>& chunks, int volume_pct);

void Pause();
void Resume();
void Stop();
void Seek(int ms);                       // clamped to the clip
void SetVolume(int volume_pct);          // 0..100, applies live

// {"state":"idle|loading|playing|paused","pos":ms,"dur":ms,"vol":pct,"rate":hz,"ch":n}
std::string StatusJson();

}  // namespace rtx::audio
