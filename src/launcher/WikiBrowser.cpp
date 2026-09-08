#include "WikiBrowser.h"
#include "Dock.h"
#include "../shared/Log.h"

#include <Ultralight/Ultralight.h>
#include <AppCore/AppCore.h>
#include <Windows.h>
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

#include <fstream>
#include <map>
#include <mutex>

// One window per game client (pid). The AppCore window must stay top-level (WS_CHILD ate
// every click), so it is owned by the game host instead: z-follows, hides with it, no taskbar.
// Every URL goes through allow_url(). "rtx:" is a private scheme used only by the injected toolbar.

namespace rtx::launcher::wiki {
namespace {

using namespace ultralight;

constexpr const char* kHome = "https://runescape.wiki/";
constexpr int kChrome = 26;        // native caption strip height (drag handle)

// Win11 draws a 1px system border around top-level windows; square corners + DWMWA_COLOR_NONE remove it.
static void quiet_frame(HWND h) {
    DWORD pref = 1 /* DWMWCP_DONOTROUND */;
    DwmSetWindowAttribute(h, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &pref, sizeof(pref));
    COLORREF border = 0xFFFFFFFE /* DWMWA_COLOR_NONE */;
    DwmSetWindowAttribute(h, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof(border));
}
constexpr int kEdge = 6;           // resize border thickness

// ---- URL policy ------------------------------------------------------------------------
bool host_allowed(const std::string& host) {
    if (host == "runescape.wiki") return true;
    const std::string suf = ".runescape.wiki";
    return host.size() > suf.size() &&
           host.compare(host.size() - suf.size(), suf.size(), suf) == 0;
}
bool allow_url(const std::string& url) {
    auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return false;
    std::string scheme = url.substr(0, scheme_end);
    for (auto& c : scheme) c = (char)tolower(c);
    if (scheme != "https") return false;
    std::size_t hs = scheme_end + 3;
    std::size_t he = url.find_first_of("/?#:", hs);
    std::string host = url.substr(hs, he == std::string::npos ? std::string::npos : he - hs);
    for (auto& c : host) c = (char)tolower(c);
    return host_allowed(host);
}

std::string ul_to_std(const String& s) {
    String8 u8 = s.utf8();
    return std::string(u8.data(), u8.length());
}

// Guard + theme script injected into every committed wiki page (page world, no rtx bridge).
constexpr const char* kGuardJs = R"JS(
(function () {
  // ---- the wiki's OWN dark theme, guaranteed -------------------------------------
  // Weird Gloop themes: the `theme` cookie picks the theme at page load; without it
  // the wiki follows prefers-color-scheme (the source of the intermittent light
  // pages). Per their docs, the theme stylesheet loads on the next refresh once the
  // cookie exists -- so a page that arrived without the dark indicator is refreshed
  // ONCE (session-guarded), after which the server serves its native dark theme.
  try {
    document.cookie = 'theme=dark;path=/;max-age=31536000';
    document.cookie = 'theme=dark;domain=.runescape.wiki;path=/;max-age=31536000';
    // MediaWiki core's OWN night mode (separate from the Weird Gloop theme): Minerva
    // styles some surfaces from skin-theme-clientpref-{day|night|os} on <html>, driven
    // by the mwclientpreferences cookie. Seen live: wgl dark active while this sat on
    // "day". Set the pref cookie and swap the class exactly as core's inline script does.
    document.cookie = 'mwclientpreferences=skin-theme-clientpref-night;path=/;max-age=31536000';
    var hEl = document.documentElement;
    if (hEl && hEl.className.indexOf('skin-theme-clientpref-') >= 0)
      hEl.className = hEl.className
        .replace('skin-theme-clientpref-day', 'skin-theme-clientpref-night')
        .replace('skin-theme-clientpref-os', 'skin-theme-clientpref-night');
    var isDark = function () {
      var h = document.documentElement, b2 = document.body;
      return (h && h.className.indexOf('wgl-theme-dark') >= 0) ||
             (b2 && b2.className.indexOf('wgl-theme-dark') >= 0);
    };
    var maybeReload = function () {
      if (isDark()) return;
      var k = 'rtxDarkReload:' + location.pathname;
      try {
        if (sessionStorage.getItem(k)) return;   // one shot: never loop on a page
        sessionStorage.setItem(k, '1');
      } catch (e) { return; }
      location.reload();
    };
    if (document.body) maybeReload();
    else document.addEventListener('DOMContentLoaded', maybeReload);
  } catch (e) {}
  if (window.__rtxWikiGuard) return; window.__rtxWikiGuard = 1;
  // ---- the wiki's OWN icons ------------------------------------------------------
  // Skin icons are a solid fill clipped by a CSS mask whose URL IS the icon SVG. The
  // renderer parses mask properties but never applies them (solid white squares).
  // Conversion: read every mask declaration out of the LIVE CSSOM -- ResourceLoader
  // injects most styles as inline <style> blobs via JS, so fetching <link> hrefs saw
  // nothing -- and emit background-image rules of the same SVG for the same selector,
  // inverted to read on the dark theme. Styles land late and async, so rescan on a
  // timer until the page settles.
  try {
    var doneSel = {};
    var Q = String.fromCharCode(34);
    var emit = [];
    var takeRule = function (rule) {
      if (rule.cssRules) {                        // @media / @supports: recurse
        for (var k = 0; k < rule.cssRules.length; k++) takeRule(rule.cssRules[k]);
        return;
      }
      var st = rule.style;
      if (!st || !rule.selectorText) return;
      var mi = st.getPropertyValue('mask-image') || st.getPropertyValue('-webkit-mask-image')
            || st.getPropertyValue('mask') || st.getPropertyValue('-webkit-mask');
      if (!mi || mi.indexOf('url(') < 0) return;
      if (doneSel[rule.selectorText]) return;
      doneSel[rule.selectorText] = 1;
      var u = mi.indexOf('url('), e = mi.indexOf(')', u);
      var url = mi.slice(u + 4, e).split(Q).join('').split(String.fromCharCode(39)).join('');
      emit.push(rule.selectorText + '{background-color:transparent !important;'
        + 'background-image:url(' + Q + url + Q + ') !important;'
        + 'background-repeat:no-repeat !important;background-position:center !important;'
        + 'background-size:contain !important;filter:invert(1) !important;}');
    };
    var sweep = function () {
      emit.length = 0;
      for (var i = 0; i < document.styleSheets.length; i++) {
        try {
          var rules = document.styleSheets[i].cssRules;
          if (!rules) continue;
          for (var j = 0; j < rules.length; j++) takeRule(rules[j]);
        } catch (e) {}
      }
      window.__rtxIconStats = 'sheets=' + document.styleSheets.length
        + ' converted=' + Object.keys(doneSel).length;
      if (!emit.length) return;
      var st2 = document.createElement('style');
      st2.textContent = emit.join('');
      document.head.appendChild(st2);
    };
    document.addEventListener('DOMContentLoaded', sweep);
    window.addEventListener('load', sweep);
    setTimeout(sweep, 800); setTimeout(sweep, 2000); setTimeout(sweep, 4000);
  } catch (e) {}
  // ---- reader-pane cleanup (login-gated controls have no purpose here) -----------
  try {
    var st = document.createElement('style');
    st.textContent = '.page-actions-menu,#page-actions,.language-selector,.mw-editsection,'
      + '#mw-mf-main-menu-button,.minerva-user-navigation,.watch-this-article,'
      + '#ca-edit,.mw-mf-watch,.menu__item--language { display:none !important; }';
    (document.head || document.documentElement).appendChild(st);
  } catch (e) {}
  // ---- domain lock (layer 2) -----------------------------------------------------
  var ALLOW = /(^|[.])runescape[.]wiki$/i;
  function hostOf(u) { try { return new URL(u, location.href).hostname; } catch (e) { return ''; } }
  function okUrl(u)  { var h = hostOf(u); return h && ALLOW.test(h); }
  document.addEventListener('click', function (e) {
    var a = e.target && e.target.closest && e.target.closest('a[href]');
    if (!a) return;
    if (!okUrl(a.href)) { e.preventDefault(); e.stopPropagation(); return; }
    if (a.target && a.target !== '_self') { e.preventDefault(); location.href = a.href; }
  }, true);
  window.open = function (u) { if (u && okUrl(u)) location.href = u; return null; };
})();
)JS";

// ---- persisted geometry (relative to the host client origin) ---------------------------
struct SavedRect { int dx = -1, dy = -1, w = 0, h = 0; };
std::wstring pane_path() {
    wchar_t up[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH)) return {};
    std::wstring p = up; p += L"\\RuneToolsX";
    CreateDirectoryW(p.c_str(), nullptr);
    return p + L"\\wikipane.txt";
}
SavedRect load_rect() {
    SavedRect r;
    auto p = pane_path(); if (p.empty()) return r;
    std::ifstream f(p.c_str());
    if (f) f >> r.dx >> r.dy >> r.w >> r.h;
    return r;
}
void save_rect(const SavedRect& r) {
    auto p = pane_path(); if (p.empty()) return;
    std::ofstream f(p.c_str(), std::ios::trunc);
    if (f) f << r.dx << ' ' << r.dy << ' ' << r.w << ' ' << r.h;
}

struct Instance;
std::map<std::uint32_t, Instance*> g_wins;   // main thread only
ultralight::App* g_app = nullptr;

// Narrow panes load the wiki's mobile format, wide ones desktop; crossing the threshold reloads.
bool want_mobile(int paneW) { return paneW < 1000; }
std::string with_format(std::string url, bool mobile) {
    auto p = url.find("useformat=");
    if (p != std::string::npos) {                       // strip any existing format param
        auto e = url.find('&', p);
        std::size_t cut = (p > 0 && (url[p - 1] == '&' || url[p - 1] == '?')) ? p - 1 : p;
        url.erase(cut, (e == std::string::npos ? url.size() : e) - cut);
    }
    url += (url.find('?') == std::string::npos ? '?' : '&');
    url += mobile ? "useformat=mobile" : "useformat=desktop";
    return url;
}

LRESULT CALLBACK PaneProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

struct Instance : public LoadListener, public NetworkListener, public WindowListener,
                  public ViewListener {
    std::uint32_t pid = 0;
    RefPtr<Window>  win;
    RefPtr<Overlay> ov;
    HWND hwnd = nullptr;
    HWND host = nullptr;
    std::string lastGood = kHome;
    long long openedMs = 0;        // for the one-shot icon-stats log (see Tick)
    bool statsLogged = false;
    bool closing = false;
    bool userSizing = false;   // inside a native drag/resize: Tick must not fight it
    SavedRect rel;             // host-relative geometry Tick holds the pane to (cached, not re-read)
    bool mobileFmt = true;     // format currently loaded; flips when a resize crosses the threshold
    // Last requested geometry, host relative. GetWindowRect reports the DWM frame, so Tick compares against this.
    bool  applied = false;
    int   appDx = 0, appDy = 0, appW = 0, appH = 0;
    long long netDenied = 0;

    // ---- enforcement layer 1: main-frame navigation lock -------------------------------
    void OnBeginLoading(View* v, uint64_t, bool is_main, const String& url) override {
        std::string u = ul_to_std(url);
        if (u.rfind("rtx://", 0) == 0) {
            v->Stop();
            if (u == "rtx://close") closing = true;
            else v->LoadURL(kHome);
            return;
        }
        if (allow_url(u) || u == "about:blank") { if (is_main) lastGood = u; return; }
        v->Stop();
        rtx::log::Client(pid, "[wiki] BLOCKED navigation: " + u);
        if (is_main) v->LoadURL(String(lastGood.c_str()));
    }
    // ---- enforcement layer 2 + night theme, re-injected on every page -------------------
    void OnWindowObjectReady(View* v, uint64_t, bool is_main, const String&) override {
        if (is_main) v->EvaluateScript(kGuardJs);
    }
    void OnDOMReady(View* v, uint64_t, bool is_main, const String& url) override {
        if (!is_main) return;
        v->EvaluateScript(kGuardJs);   // idempotent (guard flag)
        // Per-page theme fingerprint for diagnosing light-mode reports.
        String cls = v->EvaluateScript(
            "'html[' + document.documentElement.className + '] body[' + "
            "(document.body?document.body.className:'') + '] cookie=' + "
            "(document.cookie.indexOf('theme=dark')>=0?'dark':'MISSING')");
        rtx::log::Client(pid, "[wiki] page " + ul_to_std(url) + " " + ul_to_std(cls));
    }
    // ---- enforcement layer 3: subresource allowlist -------------------------------------
    bool OnNetworkRequest(View*, NetworkRequest& req) override {
        std::string host2 = ul_to_std(req.urlHost());
        for (auto& c : host2) c = (char)tolower(c);
        std::string proto = ul_to_std(req.urlProtocol());
        const bool ok = (proto == "https" || proto == "http") ? host_allowed(host2) : false;
        if (!ok && ++netDenied <= 40)
            rtx::log::Client(pid, "[wiki] blocked request: " + ul_to_std(req.url()));
        return ok;
    }
    void OnChangeCursor(View*, Cursor cursor) override { if (win) win->SetCursor(cursor); }
    void OnResize(Window*, uint32_t w, uint32_t h) override { fit_overlay((int)w, (int)h); }
    void OnClose(Window*) override { closing = true; }

    void fit_overlay(int, int) {
        if (!ov || !IsWindow(hwnd)) return;
        // Use the real client rect: with the frameless trick it is larger than what AppCore reports.
        RECT rc{}; GetClientRect(hwnd, &rc);
        int oh = rc.bottom - kChrome; if (oh < 40) oh = 40;
        ov->MoveTo(0, kChrome);
        ov->Resize((uint32_t)rc.right, (uint32_t)oh);
    }
    void persist() {
        if (!IsWindow(hwnd) || !IsWindow(host)) return;
        RECT wr{}; GetWindowRect(hwnd, &wr);
        POINT o{ 0, 0 }; ClientToScreen(host, &o);
        rel = SavedRect{ wr.left - o.x, wr.top - o.y, wr.right - wr.left, wr.bottom - wr.top };
        save_rect(rel);
    }
};

// Caption-strip buttons (Back / Home / Close), painted and hit natively so they never cover page content.
constexpr int kBtnW = 34, kBtnCount = 3;
RECT strip_btn_rect(HWND h, int i) {   // i = 0 Back, 1 Home, 2 Close (rightmost)
    RECT rc{}; GetClientRect(h, &rc);
    RECT r; r.top = 2; r.bottom = kChrome - 2;
    r.right = rc.right - 4 - (kBtnCount - 1 - i) * (kBtnW + 2);
    r.left = r.right - kBtnW;
    return r;
}

RECT default_rect(HWND host) {
    RECT hc{}; GetClientRect(host, &hc);
    const int cw = hc.right, ch = hc.bottom;
    int w = (int)(cw * 0.40); if (w < 420) w = cw < 420 ? cw : 420; if (w > 860) w = 860;
    RECT r; r.left = cw - w - 8; r.top = 8; r.right = cw - 8; r.bottom = ch - 8;
    return r;
}

// Clamp a host-relative box inside the host client area. Deliberately allowed under the 360x300 minimum.
void fit_to_host(HWND host, int& dx, int& dy, int& w, int& h) {
    RECT hc{}; GetClientRect(host, &hc);
    const int cw = hc.right, ch = hc.bottom;
    if (cw <= 0 || ch <= 0) return;
    if (w > cw) w = cw;
    if (h > ch) h = ch;
    if (dx + w > cw) dx = cw - w;
    if (dy + h > ch) dy = ch - h;
    if (dx < 0) dx = 0;
    if (dy < 0) dy = 0;
}

// Native chrome: caption-strip paint, drag/resize hit-testing, min size, persist on drop.
LRESULT CALLBACK PaneProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
    auto* it = reinterpret_cast<Instance*>(ref);
    switch (msg) {
    case WM_NCHITTEST: {
        POINT pt{ (LONG)(short)LOWORD(lp), (LONG)(short)HIWORD(lp) };
        ScreenToClient(h, &pt);
        RECT rc{}; GetClientRect(h, &rc);
        const bool l = pt.x < kEdge, r = pt.x >= rc.right - kEdge;
        const bool t = pt.y < kEdge, b = pt.y >= rc.bottom - kEdge;
        if (t && l) return HTTOPLEFT;    if (t && r) return HTTOPRIGHT;
        if (b && l) return HTBOTTOMLEFT; if (b && r) return HTBOTTOMRIGHT;
        if (l) return HTLEFT; if (r) return HTRIGHT; if (t) return HTTOP; if (b) return HTBOTTOM;
        if (pt.y < kChrome) {
            for (int i = 0; i < kBtnCount; ++i) {
                RECT br = strip_btn_rect(h, i);
                if (PtInRect(&br, pt)) return HTCLIENT;   // button: clickable, not draggable
            }
            return HTCAPTION;                             // the strip drags the pane
        }
        break;
    }
    case WM_PAINT: {
        // Let AppCore blit the view first, then paint the caption strip on top.
        LRESULT lr = DefSubclassProc(h, msg, wp, lp);
        HDC dc = GetDC(h);
        if (dc) {
            RECT rc{}; GetClientRect(h, &rc); rc.bottom = kChrome;
            HBRUSH bg = CreateSolidBrush(RGB(11, 13, 18));
            FillRect(dc, &rc, bg); DeleteObject(bg);
            RECT ln = rc; ln.top = kChrome - 1;
            HBRUSH ac = CreateSolidBrush(RGB(232, 194, 106));   // brass, matches the client shell
            FillRect(dc, &ln, ac); DeleteObject(ac);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(180, 185, 200));
            HFONT f = CreateFontW(-12, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                  CLEARTYPE_QUALITY, 0, L"Segoe UI");
            HGDIOBJ of = SelectObject(dc, f);
            RECT tr = rc; tr.left += 10;
            tr.right = strip_btn_rect(h, 0).left - 8;
            DrawTextW(dc, L"RuneScape Wiki", -1,
                      &tr, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            static const wchar_t* kGlyphs[kBtnCount] = { L"\u25C0", L"\u2302", L"\u2715" };
            for (int i = 0; i < kBtnCount; ++i) {
                RECT br = strip_btn_rect(h, i);
                HBRUSH bf = CreateSolidBrush(RGB(20, 22, 32));
                FillRect(dc, &br, bf); DeleteObject(bf);
                SetTextColor(dc, i == 2 ? RGB(235, 238, 248) : RGB(190, 195, 212));
                DrawTextW(dc, kGlyphs[i], -1, &br, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            }
            SetTextColor(dc, RGB(180, 185, 200));
            SelectObject(dc, of); DeleteObject(f);
            ReleaseDC(h, dc);
        }
        return lr;
    }
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize = POINT{ 360, 300 };
        return 0;
    }
    case WM_NCCALCSIZE:
        if (wp) return 0;   // client area = whole window: WS_THICKFRAME without its frame
        break;
    case WM_NCACTIVATE:
        // Suppress the frame repaint on focus changes.
        return DefWindowProcW(h, msg, wp, (LPARAM)-1);
    case WM_ERASEBKGND: {
        // The class brush is white; paint dark to avoid flashes.
        HDC dc = (HDC)wp;
        RECT rc{}; GetClientRect(h, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(11, 13, 18));
        FillRect(dc, &rc, bg); DeleteObject(bg);
        return 1;
    }
    case WM_SIZING:
    case WM_MOVING: {
        // Live clamp: the pane can never leave the game's client area.
        if (!it || !IsWindow(it->host)) break;
        RECT* pr = reinterpret_cast<RECT*>(lp);
        RECT hc{}; GetClientRect(it->host, &hc);
        POINT o{ 0, 0 }; ClientToScreen(it->host, &o);
        const RECT b{ o.x, o.y, o.x + hc.right, o.y + hc.bottom };
        if (msg == WM_MOVING) {
            const int w2 = pr->right - pr->left, h2 = pr->bottom - pr->top;
            if (pr->left < b.left)     { pr->left = b.left;     pr->right = b.left + w2; }
            if (pr->top < b.top)       { pr->top = b.top;       pr->bottom = b.top + h2; }
            if (pr->right > b.right)   { pr->right = b.right;   pr->left = b.right - w2; }
            if (pr->bottom > b.bottom) { pr->bottom = b.bottom; pr->top = b.bottom - h2; }
        } else {
            if (pr->left < b.left) pr->left = b.left;
            if (pr->top < b.top) pr->top = b.top;
            if (pr->right > b.right) pr->right = b.right;
            if (pr->bottom > b.bottom) pr->bottom = b.bottom;
        }
        return TRUE;
    }
    case WM_LBUTTONDOWN: {
        // Strip buttons (WM_NCHITTEST maps them to HTCLIENT); below the strip belongs to the view.
        if (!it) break;
        POINT pt{ (LONG)(short)LOWORD(lp), (LONG)(short)HIWORD(lp) };
        if (pt.y < kChrome) {
            for (int i = 0; i < kBtnCount; ++i) {
                RECT br = strip_btn_rect(h, i);
                if (!PtInRect(&br, pt)) continue;
                View* v = it->ov ? it->ov->view().get() : nullptr;
                if (i == 0)      { if (v && v->CanGoBack()) v->GoBack(); }
                else if (i == 1) { if (v) v->LoadURL(String(with_format(kHome, it->mobileFmt).c_str())); }
                else               it->closing = true;
                return 0;
            }
            return 0;   // strip clicks never reach the view
        }
        break;
    }
    case WM_KEYDOWN:
        // Native Escape-to-close (page JS cannot navigate to rtx:).
        if (wp == VK_ESCAPE && it) { it->closing = true; return 0; }
        break;
    case WM_ENTERSIZEMOVE: if (it) it->userSizing = true; break;
    case WM_EXITSIZEMOVE:
        if (it) {
            it->userSizing = false; it->persist();
            it->applied = false;   // the user moved or resized it: re-apply the request once
            const bool m = want_mobile(it->rel.w);
            if (m != it->mobileFmt && it->ov) {
                it->mobileFmt = m;
                it->ov->view()->LoadURL(String(with_format(it->lastGood, m).c_str()));
            }
        }
        break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

void destroy(Instance* it) {
    if (!it) return;
    g_wins.erase(it->pid);
    if (it->hwnd && IsWindow(it->hwnd))
        RemoveWindowSubclass(it->hwnd, PaneProc, 1);
    if (it->ov)  { it->ov->view()->set_load_listener(nullptr);
                   it->ov->view()->set_network_listener(nullptr);
                   it->ov->view()->set_view_listener(nullptr); it->ov = nullptr; }
    if (it->win) { it->win->set_listener(nullptr); it->win->Close(); it->win = nullptr; }
    delete it;
}

// ---- keybind (one VK persisted in a small file) ----------------------------------------
std::mutex g_wk_mu;
int  g_wk_vk = 0;
bool g_wk_loaded = false;
std::wstring wk_path() {
    wchar_t up[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH)) return {};
    std::wstring p = up; p += L"\\RuneToolsX";
    CreateDirectoryW(p.c_str(), nullptr);
    return p + L"\\wikikey.txt";
}
void wk_load_locked() {
    if (g_wk_loaded) return;
    g_wk_loaded = true;
    auto p = wk_path(); if (p.empty()) return;
    std::ifstream f(p.c_str());
    int vk = 0;
    if (f && (f >> vk) && vk > 0 && vk < 256) g_wk_vk = vk;
}

}  // namespace

void Init(ultralight::App* app) { g_app = app; }

void Open(std::uint32_t pid, const std::string& term) {
    if (!g_app || !pid) return;
    HWND host = nullptr;
    if (void* g = dock::GameWindowHandle(pid)) host = GetParent((HWND)g);
    if (!host || !IsWindow(host)) { rtx::log::Client(pid, "[wiki] no host window to dock beside"); return; }

    std::string url = kHome;
    if (!term.empty()) {
        std::string enc; enc.reserve(term.size() * 3);
        for (unsigned char c : term) {
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') enc += (char)c;
            else if (c == ' ') enc += '+';
            else { char b[8]; std::snprintf(b, sizeof(b), "%%%02X", c); enc += b; }
        }
        url = std::string(kHome) + "?search=" + enc + "&title=Special%3ASearch&go=Go";
    }
    if (!allow_url(url)) return;

    auto have = g_wins.find(pid);
    if (have != g_wins.end() && have->second->win) {
        have->second->ov->view()->LoadURL(
            String(with_format(url, have->second->mobileFmt).c_str()));
        SetWindowPos(have->second->hwnd, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        have->second->ov->Focus();
        return;
    }

    // Geometry: the saved user rect (host-client-relative) when sane, else the right dock.
    RECT hc{}; GetClientRect(host, &hc);
    RECT r = default_rect(host);
    SavedRect sv = load_rect();
    if (sv.w >= 360 && sv.h >= 300 && sv.dx > -sv.w + 40 && sv.dy > -40 &&
        sv.dx < hc.right - 40 && sv.dy < hc.bottom - 40) {
        r.left = sv.dx; r.top = sv.dy; r.right = sv.dx + sv.w; r.bottom = sv.dy + sv.h;
    }
    // Fit before creating so a rect saved against a larger host does not hang off the edge.
    { int dx = r.left, dy = r.top, rw = r.right - r.left, rh = r.bottom - r.top;
      fit_to_host(host, dx, dy, rw, rh);
      r.left = dx; r.top = dy; r.right = dx + rw; r.bottom = dy + rh; }
    POINT o{ 0, 0 }; ClientToScreen(host, &o);
    const int w = r.right - r.left, h = r.bottom - r.top;
    if (w < 200 || h < 200) return;

    auto* it = new Instance();
    it->pid = pid; it->host = host;
    // Seed the geometry Tick holds the pane to.
    it->rel = SavedRect{ r.left, r.top, w, h };
    it->win = Window::Create(g_app->main_monitor(), (uint32_t)w, (uint32_t)h, false,
                             kWindowFlags_Borderless | kWindowFlags_Hidden);
    if (!it->win) { delete it; return; }
    it->win->set_listener(it);
    it->hwnd = (HWND)it->win->native_handle();
    // Owner = host (z-follows, hides with it, no taskbar entry); stays top-level for AppCore input routing.
    SetWindowLongPtrW(it->hwnd, GWLP_HWNDPARENT, (LONG_PTR)host);
    SetWindowLongPtrW(it->hwnd, GWL_EXSTYLE,
                      GetWindowLongPtrW(it->hwnd, GWL_EXSTYLE) | WS_EX_TOOLWINDOW);
    // WS_THICKFRAME makes DefWindowProc honour the edge hit-codes; WM_NCCALCSIZE erases its frame.
    SetWindowLongPtrW(it->hwnd, GWL_STYLE,
                      GetWindowLongPtrW(it->hwnd, GWL_STYLE) | WS_THICKFRAME);
    quiet_frame(it->hwnd);
    SetWindowSubclass(it->hwnd, PaneProc, 1, (DWORD_PTR)it);

    // The overlay must create its own view (a hand-built view never joined AppCore's repaint clock).
    it->ov = Overlay::Create(it->win, (uint32_t)w, (uint32_t)(h - kChrome), 0, kChrome);
    if (!it->ov) { destroy(it); return; }
    View* v = it->ov->view().get();
    v->set_load_listener(it);
    v->set_network_listener(it);
    v->set_view_listener(it);
    it->lastGood = kHome;
    it->mobileFmt = want_mobile(w);
    url = with_format(url, it->mobileFmt);
    v->LoadURL(String(url.c_str()));

    SetWindowPos(it->hwnd, HWND_TOP, o.x + r.left, o.y + r.top, w, h,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    quiet_frame(it->hwnd);   // again now that the frame exists
    it->fit_overlay(0, 0);   // real client rect is known only after the frame trick lands
    it->ov->Focus();
    it->openedMs = (long long)GetTickCount64();
    g_wins[pid] = it;
    rtx::log::Client(pid, "[wiki] opened (" + std::to_string(w) + "x" + std::to_string(h) + ") -> " + url);
}

void Close(std::uint32_t pid) {
    auto f = g_wins.find(pid);
    if (f != g_wins.end()) destroy(f->second);
}

bool IsOpen(std::uint32_t pid) { return g_wins.count(pid) != 0; }

void Tick() {
    for (auto f = g_wins.begin(); f != g_wins.end(); ) {
        Instance* it = f->second; ++f;                       // destroy() erases; advance first
        if (it->closing || !IsWindow(it->host) || !IsWindow(it->hwnd)) { destroy(it); continue; }
        if (it->userSizing) continue;                        // never fight a live drag
        // One-shot icon sweep diagnostics ~6s after open.
        if (!it->statsLogged && it->openedMs &&
            (long long)GetTickCount64() - it->openedMs > 6000 && it->ov) {
            it->statsLogged = true;
            String st = it->ov->view()->EvaluateScript(
                "window.__rtxIconStats || 'sweep never emitted'");
            rtx::log::Client(it->pid, "[wiki] icons: " + ul_to_std(st));
        }
        const bool hostUp = IsWindowVisible(it->host) && !IsIconic(it->host);
        if (!hostUp) { ShowWindow(it->hwnd, SW_HIDE); continue; }
        if (!IsWindowVisible(it->hwnd)) ShowWindow(it->hwnd, SW_SHOWNOACTIVATE);
        // Hold the pane to its host-relative offset; no saved rect yet -> default dock.
        RECT wr{}; GetWindowRect(it->hwnd, &wr);
        POINT o{ 0, 0 }; ClientToScreen(it->host, &o);
        const int curDx = wr.left - o.x, curDy = wr.top - o.y;
        const int curW = wr.right - wr.left, curH = wr.bottom - wr.top;
        int wantDx, wantDy, wantW, wantH;
        if (it->rel.w >= 360) {
            wantDx = it->rel.dx; wantDy = it->rel.dy; wantW = it->rel.w; wantH = it->rel.h;
        } else {
            RECT d = default_rect(it->host);
            wantDx = d.left; wantDy = d.top; wantW = d.right - d.left; wantH = d.bottom - d.top;
        }
        // Re-fit against the current host; `rel` keeps the user's size so growing the host restores it.
        fit_to_host(it->host, wantDx, wantDy, wantW, wantH);
        // Compare against the last request, not the measured rect, so an unchanged pane is left alone.
        const bool moved = !it->applied || it->appDx != wantDx || it->appDy != wantDy ||
                           it->appW != wantW || it->appH != wantH;
        if (moved) {
            it->applied = true;
            it->appDx = wantDx; it->appDy = wantDy; it->appW = wantW; it->appH = wantH;
            SetWindowPos(it->hwnd, nullptr, o.x + wantDx, o.y + wantDy, wantW, wantH,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        } else if (curDx != wantDx || curDy != wantDy) {
            // The host itself moved: keep the offset without touching the size.
            SetWindowPos(it->hwnd, nullptr, o.x + wantDx, o.y + wantDy, 0, 0,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);
        }
    }
}

void Shutdown() {
    while (!g_wins.empty()) destroy(g_wins.begin()->second);
}

int KeybindVk() {
    std::lock_guard<std::mutex> lk(g_wk_mu);
    wk_load_locked();
    return g_wk_vk;
}
void KeybindSet(int vk) {
    std::lock_guard<std::mutex> lk(g_wk_mu);
    g_wk_loaded = true;
    g_wk_vk = (vk > 0 && vk < 256) ? vk : 0;
    auto p = wk_path(); if (p.empty()) return;
    std::ofstream f(p.c_str(), std::ios::trunc);
    if (f) f << g_wk_vk;
}

}  // namespace rtx::launcher::wiki
