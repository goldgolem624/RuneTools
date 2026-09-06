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
constexpr UINT    kHeroWidth       = 1200;               // the source width; the hero spans the page
constexpr UINT    kCardWidth       = 560;                // cards are ~220 css px wide, ample at 2x DPI
constexpr float   kJpegQuality     = 0.9f;

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

// "https://host/path" -> host, path. Only runescape.com hosts are accepted (the server
// already filters, this is the client's own check).
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

// Resample a cover to `width` with WIC's high-quality scaler and re-encode it as JPEG. The
// engine scales images with a plain bilinear filter, so handing it a 1200 px PNG for a
// 220 px card came out soft and aliased; a properly filtered downscale looks crisp and is a
// fraction of the bytes. Returns "" on any failure (the caller then keeps the original).
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

// Fetch one cover and return it as a data URL ("" on any failure).
std::string fetch_cover(const std::string& url, UINT width) {
    std::wstring host, path;
    if (!split_cdn_url(url, host, path)) return {};
    auto r = http::Fetch(host, path, {}, kCoverBytes);
    if (!r.ok || r.status != 200 || r.body.empty()) return {};
    std::string type = r.header("Content-Type");
    auto semi = type.find(';'); if (semi != std::string::npos) type.resize(semi);
    if (type != "image/png" && type != "image/jpeg" && type != "image/webp" && type != "image/gif") return {};
    std::string jpg = resample_cover(r.body, width);
    if (!jpg.empty()) return "data:image/jpeg;base64," + base64(jpg);
    return "data:" + type + ";base64," + base64(r.body);
}

// The document carries "image":"https://..." per item. Replace the first few with data URLs
// so the page can show covers without any network access of its own. String surgery on
// purpose: the URLs are exact substrings the server emitted, no JSON parser needed.
std::string inline_covers(std::string doc) {
    const std::string key = "\"image\":\"";
    std::size_t pos = 0; int done = 0;
    while (done < kCoversInlined) {
        pos = doc.find(key, pos);
        if (pos == std::string::npos) break;
        std::size_t start = pos + key.size();
        std::size_t end = doc.find('"', start);
        if (end == std::string::npos) break;
        std::string url = doc.substr(start, end - start);
        pos = end;
        if (url.rfind("https://", 0) != 0) continue;       // null or already inlined
        std::string data = fetch_cover(url, done == 0 ? kHeroWidth : kCardWidth);
        if (data.empty()) { rtx::log::Launcher("news: cover fetch failed: " + url); continue; }
        doc.replace(start, end - start, data);
        pos = start + data.size();
        ++done;
    }
    return doc;
}

void refresh_news() {
    auto r = http::Fetch(kUpdateHost, kNewsPath, {}, kDocBytes);
    if (!r.ok || r.status != 200 || r.body.empty() || r.body.front() != '{') {
        rtx::log::Launcher("news: fetch failed (ok=" + std::to_string(r.ok) + " status=" + std::to_string(r.status) + ")");
        return;
    }
    // Publish the text first so the page has headlines within a second; covers follow.
    { std::lock_guard<std::mutex> lk(g_news_mu); g_news_json = r.body; }
    std::string with_covers = inline_covers(r.body);
    std::lock_guard<std::mutex> lk(g_news_mu);
    g_news_json = with_covers;
}

void news_loop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);   // WIC lives on this thread
    for (;;) {
        refresh_news();
        bool have = false;
        { std::lock_guard<std::mutex> lk(g_news_mu); have = g_news_json.size() > 2; }
        // A failed first fetch (offline at boot) retries quickly, then settles to the slow poll.
        const int delay = have ? kRefreshMs : 20'000;
        for (int slept = 0; slept < delay; slept += 1000)
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

}  // namespace

JSValueRef NewsCached(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t, const JSValueRef[], JSValueRef*) {
    bool expected = false;
    if (g_news_started.compare_exchange_strong(expected, true))
        std::thread([] { guarded("news", news_loop); }).detach();
    std::lock_guard<std::mutex> lk(g_news_mu);
    return utf8_to_js(ctx, g_news_json);
}

}  // namespace rtx::launcher
