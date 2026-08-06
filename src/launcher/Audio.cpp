// In-process Ogg Vorbis playback for cache audio.
//
// Two halves:
//   1. DECODE - stb_vorbis (public domain) compiled into this translation unit and nowhere else.
//      Windows has no Vorbis codec, so this is the only way to hear cache audio in the client.
//   2. MIX - waveOut. PlaySound was the first attempt and cannot pause, seek or say where it is,
//      which a player needs; waveOut gives all three for one prepared buffer.
//
// Nothing here runs on the caller's thread except bookkeeping: decoding a music track is tens of
// milliseconds of MDCT work, and the JS bridge calls in on the UI thread, which shares the host
// with the game's presentation - doing it inline froze the game while a sound loaded.

#include "Audio.h"

#include <Windows.h>
#include <mmsystem.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

#pragma comment(lib, "winmm.lib")

// stb_vorbis is upstream code that will not be edited here; silence its MSVC noise for this TU.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244 4245 4456 4457 4701 4702 4703 4996)
#endif
#define STB_VORBIS_NO_STDIO          // decode from memory only; no path ever reaches the decoder
#define STB_VORBIS_NO_PUSHDATA_API   // the pull API is all this needs
#include "../../ThirdParty/stb/stb_vorbis.c"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace rtx::audio {
namespace {

std::mutex        g_mu;                  // guards everything below
HWAVEOUT          g_dev   = nullptr;
WAVEHDR           g_hdr{};
std::vector<short> g_pcm;                // interleaved 16-bit, the whole clip
int               g_rate = 0, g_ch = 0;
bool              g_paused = false;
std::atomic<int>  g_vol{100};
std::atomic<int>  g_loading{0};          // a decode is in flight
std::atomic<unsigned> g_gen{0};          // bumped per request; a stale decode drops itself

// Close the device and release the prepared buffer. Caller holds g_mu.
void CloseDeviceLocked() {
    if (!g_dev) return;
    waveOutReset(g_dev);                              // stops playback, returns the buffer
    if (g_hdr.lpData) {
        waveOutUnprepareHeader(g_dev, &g_hdr, sizeof(g_hdr));
        g_hdr = WAVEHDR{};
    }
    waveOutClose(g_dev);
    g_dev = nullptr;
    g_paused = false;
}

// Start (or restart) playback at `from_frame`. Caller holds g_mu.
bool StartAtLocked(std::size_t from_frame) {
    CloseDeviceLocked();
    if (g_pcm.empty() || g_ch <= 0 || g_rate <= 0) return false;
    if (from_frame >= g_pcm.size() / (std::size_t)g_ch) from_frame = 0;

    WAVEFORMATEX fmt{};
    fmt.wFormatTag      = WAVE_FORMAT_PCM;
    fmt.nChannels       = (WORD)g_ch;
    fmt.nSamplesPerSec  = (DWORD)g_rate;
    fmt.wBitsPerSample  = 16;
    fmt.nBlockAlign     = (WORD)(g_ch * 2);
    fmt.nAvgBytesPerSec = (DWORD)(g_rate * g_ch * 2);
    if (waveOutOpen(&g_dev, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        g_dev = nullptr;
        return false;
    }
    const int v = g_vol.load();
    const WORD lvl = (WORD)(v <= 0 ? 0 : (v >= 100 ? 0xFFFF : (v * 0xFFFF) / 100));
    waveOutSetVolume(g_dev, (DWORD)lvl | ((DWORD)lvl << 16));

    g_hdr = WAVEHDR{};
    g_hdr.lpData         = reinterpret_cast<LPSTR>(g_pcm.data() + from_frame * (std::size_t)g_ch);
    g_hdr.dwBufferLength = (DWORD)((g_pcm.size() - from_frame * (std::size_t)g_ch) * sizeof(short));
    if (waveOutPrepareHeader(g_dev, &g_hdr, sizeof(g_hdr)) != MMSYSERR_NOERROR) {
        CloseDeviceLocked();
        return false;
    }
    if (waveOutWrite(g_dev, &g_hdr, sizeof(g_hdr)) != MMSYSERR_NOERROR) {
        CloseDeviceLocked();
        return false;
    }
    g_paused = false;
    return true;
}

// Frames played of the CURRENT write. Caller holds g_mu.
std::size_t PositionFramesLocked() {
    if (!g_dev) return 0;
    MMTIME t{};
    t.wType = TIME_SAMPLES;
    if (waveOutGetPosition(g_dev, &t, sizeof(t)) != MMSYSERR_NOERROR) return 0;
    return t.wType == TIME_SAMPLES ? (std::size_t)t.u.sample : 0;
}

std::size_t g_start_frame = 0;           // where the current write began, for absolute position

}  // namespace

bool Play(const std::vector<std::vector<std::uint8_t>>& chunks, int volume_pct) {
    if (chunks.empty()) return false;
    SetVolume(volume_pct);
    const unsigned gen = ++g_gen;
    g_loading.store(1);
    auto copy = chunks;
    std::thread([copy = std::move(copy), gen]() mutable {
        std::vector<short> all;
        int rate = 0, channels = 0;
        for (const auto& ogg : copy) {
            if (gen != g_gen.load()) { g_loading.store(0); return; }   // superseded mid-decode
            if (ogg.size() < 4) continue;
            int ch = 0, hz = 0;
            short* pcm = nullptr;
            const int frames = stb_vorbis_decode_memory(ogg.data(), (int)ogg.size(), &ch, &hz, &pcm);
            if (frames <= 0 || !pcm) { if (pcm) free(pcm); continue; }
            // Every chunk of one sound shares a format; a mismatch would splice noise, so stop.
            if (!rate) { rate = hz; channels = ch; }
            if (hz != rate || ch != channels) { free(pcm); break; }
            all.insert(all.end(), pcm, pcm + (std::size_t)frames * ch);
            free(pcm);
        }
        if (gen != g_gen.load()) { g_loading.store(0); return; }
        if (all.empty() || channels < 1 || channels > 2 || rate < 1000 || rate > 192000) {
            g_loading.store(0);
            return;
        }
        {
            std::lock_guard<std::mutex> lk(g_mu);
            // Re-check under the lock: two decodes can both pass the check above and then queue
            // here, and the older one must not overwrite the newer one's audio.
            if (gen != g_gen.load()) { g_loading.store(0); return; }
            // CLOSE THE DEVICE BEFORE TOUCHING g_pcm. waveOut plays directly out of that buffer,
            // so replacing it while a previous clip is still running hands the audio driver freed
            // memory - that crashed the client when a second sound was started over a first.
            // waveOutReset + waveOutUnprepareHeader inside here are what make the buffer safe to
            // release.
            CloseDeviceLocked();
            g_pcm = std::move(all);
            g_rate = rate;
            g_ch   = channels;
            g_start_frame = 0;
            StartAtLocked(0);
        }
        g_loading.store(0);
    }).detach();
    return true;
}

void Pause() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_dev && !g_paused && waveOutPause(g_dev) == MMSYSERR_NOERROR) g_paused = true;
}

void Resume() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_dev && g_paused && waveOutRestart(g_dev) == MMSYSERR_NOERROR) g_paused = false;
}

void Stop() {
    ++g_gen;                                  // orphan any decode still in flight
    std::lock_guard<std::mutex> lk(g_mu);
    CloseDeviceLocked();
    g_pcm.clear();
    g_rate = g_ch = 0;
    g_start_frame = 0;
}

void Seek(int ms) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_pcm.empty() || g_rate <= 0 || g_ch <= 0) return;
    if (ms < 0) ms = 0;
    // waveOut plays one prepared buffer, so seeking means re-writing from the new offset.
    std::size_t frame = (std::size_t)((std::int64_t)ms * g_rate / 1000);
    const std::size_t total = g_pcm.size() / (std::size_t)g_ch;
    if (frame >= total) frame = total ? total - 1 : 0;
    g_start_frame = frame;
    StartAtLocked(frame);
}

void SetVolume(int volume_pct) {
    if (volume_pct < 0) volume_pct = 0;
    if (volume_pct > 100) volume_pct = 100;
    g_vol.store(volume_pct);
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_dev) return;
    const WORD lvl = (WORD)(volume_pct <= 0 ? 0
                            : (volume_pct >= 100 ? 0xFFFF : (volume_pct * 0xFFFF) / 100));
    waveOutSetVolume(g_dev, (DWORD)lvl | ((DWORD)lvl << 16));
}

std::string StatusJson() {
    std::lock_guard<std::mutex> lk(g_mu);
    const bool loading = g_loading.load() != 0;
    std::size_t total = (g_ch > 0) ? g_pcm.size() / (std::size_t)g_ch : 0;
    std::size_t pos   = g_dev ? g_start_frame + PositionFramesLocked() : 0;
    if (pos > total) pos = total;
    const bool done = g_dev && total && pos >= total;
    const char* state = loading ? "loading"
                      : (!g_dev || !total || done) ? "idle"
                      : (g_paused ? "paused" : "playing");
    char buf[200];
    std::snprintf(buf, sizeof(buf),
                  "{\"state\":\"%s\",\"pos\":%u,\"dur\":%u,\"vol\":%d,\"rate\":%d,\"ch\":%d}",
                  state,
                  (unsigned)(g_rate ? pos * 1000ull / (unsigned)g_rate : 0),
                  (unsigned)(g_rate ? total * 1000ull / (unsigned)g_rate : 0),
                  g_vol.load(), g_rate, g_ch);
    return buf;
}

}  // namespace rtx::audio
