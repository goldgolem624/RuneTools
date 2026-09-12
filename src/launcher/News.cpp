#include "News.h"
#include "BridgeUtil.h"
#include "Http.h"
#include "../shared/Log.h"

#include <Windows.h>
#include <objbase.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rtx::launcher {

namespace {

constexpr wchar_t kNewsPath[]     = L"/api/news";
constexpr int     kRefreshMs      = 30 * 60'000;        // the feed moves a few times a day
constexpr std::size_t kDocBytes   = 512u * 1024;        // the JSON document (URLs only)
constexpr std::size_t kCoverBytes = 3u * 1024 * 1024;   // one cover PNG from Jagex's CDN
constexpr int     kCoversInlined  = 7;                  // every item the server sends (hero + two rows)
constexpr int     kCoverThreads   = 4;                  // CDN fetches in flight at once
constexpr UINT    kHeroWidth       = 1200;               // the source width; the hero spans the page
constexpr UINT    kCardWidth       = 560;                // cards are ~220 css px wide, ample at 2x DPI
constexpr float   kJpegQuality     = 0.9f;
constexpr int     kCacheDays       = 30;                 // resampled covers kept on disk this long

std::mutex        g_news_mu;
std::string       g_news_json = "{}";
std::atomic<bool> g_news_started{ false };

std::string base64(const std::string& in) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out; out.reserve((in.size() + 2) / 3 * 4);
    std::size_t i = 0;
    while (i + 2 < in.size()) {
        unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8) | (unsigned char)in[i + 2];
        out += T[(v >> 18) & 63]; out += T[(v >> 12) & 63]; out += T[(v >> 6) & 63]; out += T[v & 63];
        i += 3;
    }
    if (i + 1 == in.size()) {
        unsigned v = (unsigned char)in[i] << 16;
        out += T[(v >> 18) & 63]; out += T[(v >> 12) & 63]; out += "==";
    } else if (i + 2 == in.size()) {
        unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8);
        out += T[(v >> 18) & 63]; out += T[(v >> 12) & 63]; out += T[(v >> 6) & 63]; out += '=';
    }
    return out;
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

// "https://host/path" -> host, path. Only runescape.com hosts are accepted.
bool split_cdn_url(const std::string& url, std::wstring& host, std::wstring& path) {
    if (url.rfind("https://", 0) != 0) return false;
    auto slash = url.find('/', 8);
    if (slash == std::string::npos) return false;
    std::string h = url.substr(8, slash - 8), p = url.substr(slash);
    if (h.size() < 14 || h.compare(h.size() - 14, 14, ".runescape.com") != 0) {
        if (h != "runescape.com") return false;
    }
    for (char c : h) if (!(isalnum((unsigned char)c) || c == '.' || c == '-')) return false;
    if (p.find_first_of("\"\\ <>") != std::string::npos) return false;
    host = widen(h); path = widen(p);
    return true;
}

std::string resample_cover(const std::string& bytes, UINT width) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> f;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return {};
    ComPtr<IStream> in(SHCreateMemStream((const BYTE*)bytes.data(), (UINT)bytes.size()));
    if (!in) return {};
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(f->CreateDecoderFromStream(in.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &dec))) return {};
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(dec->GetFrame(0, &frame))) return {};
    UINT w = 0, h = 0;
    if (FAILED(frame->GetSize(&w, &h)) || !w || !h) return {};
    ComPtr<IWICBitmapSource> src = frame;
    if (w > width) {
        UINT nh = (UINT)((unsigned long long)h * width / w);
        ComPtr<IWICBitmapScaler> sc;
        if (FAILED(f->CreateBitmapScaler(&sc))) return {};
        if (FAILED(sc->Initialize(frame.Get(), width, nh ? nh : 1, WICBitmapInterpolationModeHighQualityCubic))) return {};
        src = sc;
    }
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(f->CreateFormatConverter(&conv))) return {};
    if (FAILED(conv->Initialize(src.Get(), GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) return {};

    ComPtr<IStream> out(SHCreateMemStream(nullptr, 0));
    ComPtr<IWICBitmapEncoder> enc;
    if (!out || FAILED(f->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &enc))) return {};
    if (FAILED(enc->Initialize(out.Get(), WICBitmapEncoderNoCache))) return {};
    ComPtr<IWICBitmapFrameEncode> fe;
    ComPtr<IPropertyBag2> props;
    if (FAILED(enc->CreateNewFrame(&fe, &props))) return {};
    if (props) {
        PROPBAG2 opt{}; wchar_t name[] = L"ImageQuality"; opt.pstrName = name;
        VARIANT v; VariantInit(&v); v.vt = VT_R4; v.fltVal = kJpegQuality;
        props->Write(1, &opt, &v);
    }
    if (FAILED(fe->Initialize(props.Get()))) return {};
    if (FAILED(fe->WriteSource(conv.Get(), nullptr))) return {};
    if (FAILED(fe->Commit()) || FAILED(enc->Commit())) return {};

    STATSTG st{};
    if (FAILED(out->Stat(&st, STATFLAG_NONAME))) return {};
    std::string jpg((std::size_t)st.cbSize.QuadPart, char(0));
    LARGE_INTEGER zero{}; out->Seek(zero, STREAM_SEEK_SET, nullptr);
    ULONG got = 0;
    if (FAILED(out->Read(jpg.data(), (ULONG)jpg.size(), &got)) || got != jpg.size()) return {};
    return jpg;
}

// ---- disk cache: %LOCALAPPDATA%\RuneToolsX\newscache\<fnv1a(url)>-<width>.jpg ----

std::filesystem::path cache_dir() {
    wchar_t buf[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH) == 0) return {};
    return std::filesystem::path(buf) / L"RuneToolsX" / L"newscache";
}

std::filesystem::path cache_file(const std::string& url, UINT width) {
    const auto dir = cache_dir();
    if (dir.empty()) return {};
    unsigned long long h = 1469598103934665603ull;
    for (unsigned char c : url) { h ^= c; h *= 1099511628211ull; }
    char name[64];
    std::snprintf(name, sizeof(name), "%016llx-%u.jpg", h, width);
    return dir / name;
}

std::string cache_read(const std::filesystem::path& p) {
    if (p.empty()) return {};
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return bytes.size() > 64 ? bytes : std::string();
}

void cache_write(const std::filesystem::path& p, const std::string& bytes) {
    if (p.empty() || bytes.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    const auto tmp = p.string() + ".part";
    { std::ofstream f(tmp, std::ios::binary | std::ios::trunc); if (!f) return; f.write(bytes.data(), (std::streamsize)bytes.size()); }
    std::filesystem::rename(tmp, p, ec);
    if (ec) std::filesystem::remove(tmp, ec);
}

void cache_prune() {
    const auto dir = cache_dir();
    std::error_code ec;
    if (dir.empty() || !std::filesystem::is_directory(dir, ec)) return;
    const auto cutoff = std::filesystem::file_time_type::clock::now() - std::chrono::hours(24 * kCacheDays);
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        if (e.last_write_time(ec) < cutoff) std::filesystem::remove(e.path(), ec);
    }
}

// One cover slot in the document: where the URL sits and what replaces it.
struct Cover {
    std::size_t start = 0, end = 0;   // URL span inside the original document
    std::string url;
    UINT        width = kCardWidth;
    std::string data;                 // "data:image/jpeg;base64,..." once resolved
    bool        cached = false;
    long long   ms = 0;
    std::string note;                 // failure reason for the log
};

// Fetch one cover from the CDN and resample it; "" on any failure (note says why).
std::string fetch_cover_bytes(const Cover& c, std::string& note) {
    std::wstring host, path;
    if (!split_cdn_url(c.url, host, path)) { note = "url rejected"; return {}; }
    auto r = http::Fetch(host, path, {}, kCoverBytes);
    if (!r.ok || r.status != 200 || r.body.empty()) { note = "http " + std::to_string(r.status) + " " + r.detail; return {}; }
    std::string type = r.header("Content-Type");
    auto semi = type.find(';'); if (semi != std::string::npos) type.resize(semi);
    if (type != "image/png" && type != "image/jpeg" && type != "image/webp" && type != "image/gif") { note = "type " + type; return {}; }
    std::string jpg = resample_cover(r.body, c.width);
    if (jpg.empty()) note = "resample failed, raw " + type + " " + std::to_string(r.body.size()) + " bytes";
    return jpg.empty() ? r.body : jpg;   // the raw image still renders; it is just larger
}

// Resolve one cover: disk cache first, then the CDN. Always ends with data set or note set.
void resolve_cover(Cover& c) {
    const auto t0 = std::chrono::steady_clock::now();
    const auto file = cache_file(c.url, c.width);
    std::string bytes = cache_read(file);
    if (!bytes.empty()) {
        c.cached = true;
    } else {
        bytes = fetch_cover_bytes(c, c.note);
        if (!bytes.empty() && c.note.empty()) cache_write(file, bytes);   // only resampled JPEGs are cached
    }
    if (!bytes.empty()) {
        const bool jpeg = bytes.size() > 3 && (unsigned char)bytes[0] == 0xFF && (unsigned char)bytes[1] == 0xD8;
        const bool png  = bytes.size() > 8 && (unsigned char)bytes[0] == 0x89 && bytes[1] == 'P';
        c.data = std::string("data:image/") + (jpeg ? "jpeg" : png ? "png" : "webp") + ";base64," + base64(bytes);
    }
    c.ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
}

// Find the first few "image":"https://..." values (string surgery, no JSON parser).
std::vector<Cover> find_covers(const std::string& doc) {
    std::vector<Cover> out;
    const std::string key = "\"image\":\"";
    std::size_t pos = 0;
    while ((int)out.size() < kCoversInlined) {
        pos = doc.find(key, pos);
        if (pos == std::string::npos) break;
        std::size_t start = pos + key.size();
        std::size_t end = doc.find('"', start);
        if (end == std::string::npos) break;
        pos = end;
        std::string url = doc.substr(start, end - start);
        if (url.rfind("https://", 0) != 0) continue;       // null or already inlined
        Cover c; c.start = start; c.end = end; c.url = url;
        c.width = out.empty() ? kHeroWidth : kCardWidth;
        out.push_back(std::move(c));
    }
    return out;
}

// The document with every resolved cover substituted; unresolved ones keep their URL.
std::string compose(const std::string& doc, const std::vector<Cover>& covers) {
    std::string out; out.reserve(doc.size() + 1024 * 1024);
    std::size_t at = 0;
    for (const auto& c : covers) {
        if (c.data.empty()) continue;
        out.append(doc, at, c.start - at);
        out += c.data;
        at = c.end;
    }
    out.append(doc, at, std::string::npos);
    return out;
}

void publish(const std::string& json) {
    std::lock_guard<std::mutex> lk(g_news_mu);
    g_news_json = json;
}

void refresh_news() {
    const auto t0 = std::chrono::steady_clock::now();
    auto r = http::Fetch(kUpdateHost, kNewsPath, {}, kDocBytes);
    if (!r.ok || r.status != 200 || r.body.empty() || r.body.front() != '{') {
        rtx::log::Launcher("news: fetch failed (ok=" + std::to_string(r.ok) + " status=" + std::to_string(r.status) + " " + r.detail + ")");
        return;
    }
    const std::string doc = r.body;
    publish(doc);                                             // headlines first; covers follow as they land
    std::vector<Cover> covers = find_covers(doc);
    rtx::log::Launcher("news: document " + std::to_string(doc.size()) + " bytes, " + std::to_string(covers.size()) + " covers to resolve");

    // Resolve covers on a few threads (each needs its own COM apartment for WIC) and republish
    // the document as each one lands, so the hero appears without waiting for the whole row.
    std::mutex mu; std::size_t next = 0; int landed = 0;
    auto worker = [&] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        for (;;) {
            std::size_t i;
            { std::lock_guard<std::mutex> lk(mu); if (next >= covers.size()) break; i = next++; }
            Cover local = covers[i];
            resolve_cover(local);
            std::lock_guard<std::mutex> lk(mu);
            covers[i] = std::move(local);
            if (!covers[i].data.empty()) { ++landed; publish(compose(doc, covers)); }
        }
        CoUninitialize();
    };
    std::vector<std::thread> pool;
    const int n = (int)std::min<std::size_t>(kCoverThreads, covers.size());
    for (int i = 0; i < n; ++i) pool.emplace_back(worker);
    for (auto& t : pool) t.join();

    int cached = 0, fetched = 0, failed = 0; std::size_t bytes = 0;
    for (const auto& c : covers) {
        if (c.data.empty()) { ++failed; rtx::log::Launcher("news: cover failed (" + c.note + ") " + c.url); continue; }
        bytes += c.data.size();
        if (c.cached) ++cached; else ++fetched;
        if (!c.note.empty()) rtx::log::Launcher("news: cover kept raw (" + c.note + ") " + c.url);
    }
    const auto total = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    rtx::log::Launcher("news: " + std::to_string(landed) + "/" + std::to_string(covers.size()) + " covers inlined (" +
                       std::to_string(cached) + " from cache, " + std::to_string(fetched) + " fetched, " +
                       std::to_string(failed) + " failed), " + std::to_string(bytes / 1024) + " KB of images, " +
                       std::to_string(total) + " ms");
}

void news_loop() {
    cache_prune();
    for (;;) {
        refresh_news();
        bool have = false;
        { std::lock_guard<std::mutex> lk(g_news_mu); have = g_news_json.size() > 2; }
        const int delay = have ? kRefreshMs : 20'000;
        for (int slept = 0; slept < delay; slept += 1000)
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

}  // namespace

JSValueRef NewsCached(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t, const JSValueRef[], JSValueRef*) {
    bool expected = false;
    if (g_news_started.compare_exchange_strong(expected, true)) {
        rtx::log::Launcher("news: first request from the page, starting the feed thread");
        std::thread([] { guarded("news", news_loop); }).detach();
    }
    std::lock_guard<std::mutex> lk(g_news_mu);
    return utf8_to_js(ctx, g_news_json);
}

}  // namespace rtx::launcher
