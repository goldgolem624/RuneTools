#include "Music.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <xaudio2.h>
#include <wrl/client.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include "../shared/Log.h"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace rtx::launcher::music {
namespace {

using Microsoft::WRL::ComPtr;

std::mutex g_mu;
ComPtr<IXAudio2> g_xa;
IXAudio2MasteringVoice* g_master = nullptr;   // owned by g_xa
IXAudio2SourceVoice* g_voice = nullptr;       // owned by g_xa
std::vector<BYTE> g_pcm;                      // the decoded track, kept for the life of the process
std::string g_file;
float g_volume = 0.0f;
std::atomic<bool> g_playing{ false };
std::atomic<bool> g_starting{ false };
bool g_min = false;                           // the window is minimised: silenced without stopping the loop
bool g_mf = false;

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

// Full path next to the running exe, so "sounds/spooky-loop.mp3" works wherever the launcher is installed.
std::wstring beside_exe(const std::string& rel) {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    const auto slash = p.find_last_of(L"\\/");
    if (slash != std::wstring::npos) p.resize(slash + 1);
    std::wstring r = widen(rel);
    for (auto& c : r) if (c == L'/') c = L'\\';
    return p + r;
}

// Decodes any format Media Foundation can read (mp3, wav, ogg through the platform decoders) into 16-bit stereo
// PCM at its own sample rate, and reports that format back through `fmt`.
bool decode(const std::wstring& path, std::vector<BYTE>& out, WAVEFORMATEX& fmt) {
    ComPtr<IMFSourceReader> reader;
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader))) return false;
    reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);

    ComPtr<IMFMediaType> want;
    if (FAILED(MFCreateMediaType(&want))) return false;
    want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    want->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    want->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    if (FAILED(reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, want.Get()))) return false;

    ComPtr<IMFMediaType> got;
    if (FAILED(reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &got))) return false;
    WAVEFORMATEX* wf = nullptr; UINT32 wfSize = 0;
    if (FAILED(MFCreateWaveFormatExFromMFMediaType(got.Get(), &wf, &wfSize))) return false;
    fmt = *wf;
    CoTaskMemFree(wf);

    for (;;) {
        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        if (FAILED(reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &sample))) return false;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (!sample) continue;
        ComPtr<IMFMediaBuffer> buf;
        if (FAILED(sample->ConvertToContiguousBuffer(&buf))) return false;
        BYTE* p = nullptr; DWORD len = 0;
        if (FAILED(buf->Lock(&p, nullptr, &len))) return false;
        out.insert(out.end(), p, p + len);
        buf->Unlock();
        if (out.size() > 200u * 1024u * 1024u) break;   // a runaway file never eats the machine
    }
    return !out.empty();
}

void start_locked(const std::string& file, float volume) {
    if (!g_mf) {
        if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) { rtx::log::Launcher("music: Media Foundation unavailable"); return; }
        g_mf = true;
    }
    WAVEFORMATEX fmt{};
    if (g_pcm.empty() || g_file != file) {
        std::vector<BYTE> pcm;
        if (!decode(beside_exe(file), pcm, fmt)) { rtx::log::Launcher("music: could not decode " + file); return; }
        g_pcm.swap(pcm);
        g_file = file;
    } else {
        // already decoded: the voice keeps the format, so nothing to re-read
    }
    if (!g_xa) {
        if (FAILED(XAudio2Create(&g_xa, 0, XAUDIO2_DEFAULT_PROCESSOR))) { rtx::log::Launcher("music: XAudio2 unavailable"); return; }
        if (FAILED(g_xa->CreateMasteringVoice(&g_master))) { g_xa.Reset(); rtx::log::Launcher("music: no mastering voice"); return; }
    }
    if (!g_voice) {
        if (fmt.nChannels == 0) {   // decoded earlier in this process: re-read the format from the file header cheaply
            std::vector<BYTE> tmp;
            if (!decode(beside_exe(file), tmp, fmt)) return;
        }
        if (FAILED(g_xa->CreateSourceVoice(&g_voice, &fmt))) { rtx::log::Launcher("music: no source voice"); return; }
    }
    XAUDIO2_BUFFER xb{};
    xb.AudioBytes = (UINT32)g_pcm.size();
    xb.pAudioData = g_pcm.data();
    xb.Flags = XAUDIO2_END_OF_STREAM;
    xb.LoopCount = XAUDIO2_LOOP_INFINITE;   // sample-exact repeat: no gap, no re-decode
    g_voice->Stop(0);
    g_voice->FlushSourceBuffers();
    if (FAILED(g_voice->SubmitSourceBuffer(&xb))) { rtx::log::Launcher("music: could not queue the track"); return; }
    g_voice->SetVolume(g_min ? 0.0f : volume);
    if (FAILED(g_voice->Start(0))) { rtx::log::Launcher("music: could not start"); return; }
    g_playing = true;
    rtx::log::Launcher("music: playing " + file);
}

}  // namespace

bool Play(const std::string& file, float volume) {
    volume = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_volume = volume;
        if (g_playing && g_file == file) { if (g_voice) g_voice->SetVolume(g_min ? 0.0f : volume); return true; }
        if (g_starting) return true;
        g_starting = true;
    }
    // decoding a few minutes of audio takes a moment: never on the UI callback
    std::thread([file, volume] {
        std::lock_guard<std::mutex> lk(g_mu);
        start_locked(file, volume);
        g_starting = false;
    }).detach();
    return true;
}

void SetVolume(float volume) {
    volume = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
    std::lock_guard<std::mutex> lk(g_mu);
    g_volume = volume;
    if (g_voice) g_voice->SetVolume(g_min ? 0.0f : volume);
}

void Stop() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_voice) { g_voice->Stop(0); g_voice->FlushSourceBuffers(); }
    g_playing = false;
}

void Minimised(bool on) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_min == on) return;
    g_min = on;
    if (g_voice) g_voice->SetVolume(on ? 0.0f : g_volume);
}

bool Playing() { return g_playing.load(); }

}  // namespace rtx::launcher::music
