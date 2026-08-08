#include "Dock.h"
#include "Bridge.h"
#include "Companion.h"
#include "Overlay.h"            // QuiesceMarkers (stop the in-frame layer before teardown)
#include "Markers.h"            // configurable mark/remove-tile keybinds
#include "../reader/Reader.h"   // RenderToggle (keep-focused / embed flag channel)
#include "../../companion/RenderShare.h"   // kMsgGameClicked (companion -> host click routing)
#include "../shared/Log.h"

#include <Ultralight/Ultralight.h>
#include <AppCore/AppCore.h>
#include <JavaScriptCore/JavaScript.h>
#include <Windows.h>
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM
#include <dwmapi.h>     // DwmSetWindowAttribute (disable the host's activation transition)
#pragma comment(lib, "dwmapi.lib")
#include <shobjidl.h>   // SHGetPropertyStoreForWindow (per-window AppUserModelID)
#pragma comment(lib, "shell32.lib")

#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

// Unified-window model: one host window per client, with the Ultralight sidebar as a child on the
// right and the game filling the rest. Two modes, chosen by kTrueEmbed:
//
//  - TRUE EMBED (kTrueEmbed=1): the game becomes a WS_CHILD of the host. INVARIANT: cross-process
//    SetParent silently attaches the two threads' input queues -- that kernel-side activation
//    handshake is the window-switch stall -- so it is severed right after SetParent
//    (AttachThreadInput FALSE). The game's queue then never holds real keyboard focus: the
//    companion's keep-focused layer keeps the client believing it is focused, the HOST owns
//    keyboard focus and forwards keys to the game. Mouse follows the window under the cursor.
//
//  - GLUED (kTrueEmbed=0, fallback): the game stays its own top-level window (frame stripped, no
//    parent/owner), positioned flush over the host and z-grouped. No queue attachment, but the
//    shell sees two windows.
//
// Everything here runs on the AppCore main thread (Bridge JS callbacks, OnUpdate, host WndProc).
// The host WndProc must not block.

namespace rtx::launcher { void HudClose(std::uint32_t pid); }   // Bridge.cpp: releases a client's HUD section on teardown

namespace rtx::launcher::dock {

using namespace ultralight;

namespace {

App*        g_app = nullptr;
std::string g_client_html_path;
HBRUSH      g_darkBrush = nullptr;   // dark erase brush (#0b0d12)

// Dev UI hot-reload (RTX_UI_DIR, resolved in main.cpp): Tick polls the ui dir's newest write time
// and reloads every open panel once it settles (two equal polls, so a half-written file is never
// spliced in).
bool      g_uiWatch = false;
long long g_uiMtime = 0, g_uiPending = 0;
ULONGLONG g_uiPollMs = 0;

constexpr int     kPanelWidth = 560;   // expanded sidebar width CAP (+ View/host initial size)
constexpr int     kPanelMin   = 300;   // expanded sidebar width FLOOR (keeps content usable)
constexpr int     kRailWidth  = 56;    // collapsed icon-rail width (matches client.html)
constexpr wchar_t kHostClass[] = L"RuneToolsXHost";
constexpr UINT    kAnimTimerId = 1;    // collapse/expand slide timer
constexpr double  kAnimMs = 150.0;     // slide duration

// TRUE EMBED vs GLUED (see the model comment at the top). false falls back to the glued pair.
constexpr bool kTrueEmbed = true;

// Posted to the host by the embed worker when the hierarchy surgery finished. The surgery's
// calls (style strip, SetParent) are synchronous sends into the GAME's window thread, which can
// park for MINUTES during world preload, so they must never run on the UI thread.
// wParam = the game HWND the surgery ran against.
constexpr UINT kMsgEmbedDone = 0x8000 + 0x53;   // WM_APP range, beside kMsgGameClicked

// The expanded sidebar scales with the window (~30%), clamped to a usable range. The clamp bounds
// are CSS design pixels; the panel content renders at device_scale `sc` (the monitor scale, kept in
// in sync by SyncPanelDpi), so the PHYSICAL width reserved is the design width x sc.
int PanelFull(int cw, double sc) {
    int lo = (int)(kPanelMin * sc + 0.5), hi = (int)(kPanelWidth * sc + 0.5);
    int w = cw * 3 / 10;
    if (w < lo) w = lo;
    if (w > hi) w = hi;
    return w;
}

// Host CLIENT width that makes the embedded game render at EXACTLY `gameW`, with the panel added on
// on the right in its current state. Single source of truth for the game<->panel split, so the
// host size and Layout's split can't disagree (RS3 persists its window size, so any mismatch
// compounds across relaunches). Collapsed -> game + rail. Expanded -> the client width cw where
// cw - PanelFull(cw) == gameW; PanelFull is non-decreasing, so walk up from the floor.
int HostClientForGame(int gameW, bool collapsed, double sc) {
    if (gameW < 0) gameW = 0;
    int rail = (int)(kRailWidth * sc + 0.5);
    if (collapsed) return gameW + rail;
    int cw = gameW + (int)(kPanelMin * sc + 0.5);
    while (cw - PanelFull(cw, sc) < gameW) ++cw;
    return cw;
}

// Current-monitor DPI of a window (0 if unavailable). Defined further down; forward-declared so the
// panel-DPI sync below it can use it. Unlike AppCore's cached Window::scale(), this is always live.
unsigned DpiForWindow(HWND h);

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

// Splice sibling .js files into the page as inline <script> blocks ahead of the main inline
// script: LoadHTML has no base URL, so a relative <script src> can never resolve. Each file
// keeps its own <script> block so JS errors retain per-file line numbers in OnAddConsoleMessage.
// The panel files are bare classic scripts sharing one global scope with client.html (no IIFE).
void inject_panel_scripts(std::string& html, const std::string& html_path) {
    static const char* kFiles[] = {
        // quest_guides.js is DELIBERATELY not spliced: at ~1.3MB of one line it was half of
        // everything the page parsed at startup, for data only the quest guides need. It is
        // pulled through bridge().uiAsset() the first time a guide is shown.
        "panel_stopwatch.js",
        "panel_notes.js",
        "panel_counter.js",
        "panel_familiar.js",
        "panel_dungeoneering.js",  // Daemonheim floor status + explored map
        "panel_archresearch.js",   // Archaeology Field Study / Report status
        "panel_reputation.js",
        "panel_bossinfo.js",
        "panel_groupbank.js",
        "panel_pets.js",
        "panel_bosses.js",
        // panel_bgh.js / panel_havenbgh.js moved out to marketplace plugins (2026-08-03)
        "panel_compass.js",    // Clues: compass solver
        "panel_puzzle.js",     // Clues: puzzle-box solver
        "panel_celtic.js",     // Clues: celtic-knot solver
        "panel_lockbox.js",    // Clues: lockbox solver
        "panel_towers.js",     // Clues: towers (skyscrapers) solver
        "panel_globetrotter.js",// Clues: globetrotter outfit guide
        "panel_visions.js",    // Visions of Havenhythe quest guide
        "panel_amberfell.js",  // Secrets of Amberfell quest guide
        "panel_wizkid.js",     // Wiz Kid quest guide
        "panel_necromancy.js", // Necromancy! quest guide
        "panel_restless.js",   // The Restless Ghost quest guide
        "panel_makinghistory.js", // Making History quest guide
        "panel_newfoundations.js", // New Foundations quest guide
        "panel_noplacelikehome.js", // There's No Place Like Home... quest guide
        "panel_murderborder.js", // Murder on the Border quest guide
        "panel_interfaces.js", // Interfaces inspector
        "panel_invention.js",  // Invention components
        "panel_farming.js",    // Farming patch tracker + tool leprechaun
        "panel_lodestones.js", // Hidey-holes + Lodestones status
        "panel_clueguide.js",  // Clue scrolls map + emote/cryptic guide
        "panel_metalbank.js",  // Metal bank
        "panel_mysteries.js",  // Archaeology mysteries (requirements + focused mystery)
        "panel_chatlog.js",
        "panel_achievements.js",
        "panel_areatasks.js",   // shared task scaffold (also used by panel_gimtasks.js)
        "panel_gimtasks.js",
        "panel_perks.js",
        "panel_buffs.js",
        "panel_tasks.js",
        "panel_dailies.js",    // Dailies & Weeklies reset tracker
        "panel_scene.js",
        "panel_overlay.js",
        "panel_markers.js",
        "panel_worldmap.js",   // World Map (full-world terrain browser: pan/zoom/search/layers)
        "panel_zygomites.js",  // Anachronia base camp guide (zygomite tracker moved to a plugin 2026-08-03)
        // panel_agility.js moved out to a marketplace plugin (2026-08-03)
        "panel_rendering.js",
        "panel_storage.js",
        "panel_containers.js",
        "panel_pof.js",
        "panel_abilities.js",
        "panel_alerts.js",
        "panel_inventory.js",
        "panel_quests.js",
        "panel_questguides.js",
        "panel_xptracker.js",
        "panel_varswatcher.js",
        "panel_netprobe.js",   // Server Packets (server->client protocol + live inbound feed)
        "panel_cs2.js",        // CS2 Scripts browser (extraction + search/view)
        "panel_skillbonus.js", // Bonus XP for the Skills tab
        "panel_bank.js",
        "panel_screenshot.js", // Screenshot capture (user keybind + game-window PNG)
        "panel_scarabs.js",    // Corrupted Scarabs community world tracker
        "panel_obelisks.js",   // Soul Obelisk community world+district tracker
        // panel_ports.js moved out to a marketplace plugin (2026-08-03)
        "panel_portsinfo.js",  // Ports state reference panel + the state.ports broker decode
        "panel_kingdom.js",    // Miscellania kingdom management (approval/coffer/workers)
        "panel_rituals.js",    // Necromancy ritual site HUD + City of Um talents
        "panel_toolbelt.js",   // Toolbelt contents + tool tiers (missing tools first)
        "panel_shopcaps.js",   // Capped shop purchases with reset countdowns
        "panel_currencies.js", // Currency pouch ledger (balances vs caps, DBTable 66)
        "panel_farmcol.js",    // Player-Owned Farm breed collections (needs panel_bosses.js loader)
        "panel_archcol.js",    // Archaeology faction artefact collections (needs panel_bosses.js loader)
        "panel_leagues.js",    // Leagues tiers/relics/tasks from the live cache DBTables
        "panel_sounds.js",     // Sounds: cache audio browser (js5-14 effects / js5-40 music)
        "panel_cachex.js",     // Cache Explorer (Developer): enums/structs/dbtables/vars
        "panel_health.js",     // Reader health check (Developer; moved out of Player State)
        "panel_menuswap.js",   // Right-click menu inspector + reorder (Developer)
    };
    auto slash = html_path.find_last_of("\\/");
    std::string dir = (slash == std::string::npos) ? std::string() : html_path.substr(0, slash + 1);
    auto pos = html.find("<script>");
    if (pos == std::string::npos) return;
    // quest_guides.js used to be the FIRST spliced file, so window.QUEST_GUIDES existed before
    // any panel ran. It is lazy now, but a few panels still register their own guide into that
    // object at load time - create it up front so those assignments have something to write to.
    std::string blob = "<script>window.QUEST_GUIDES = window.QUEST_GUIDES || {};</script>\n";
    for (const char* f : kFiles) {
        std::string js = read_file(dir + f);
        if (js.empty()) continue;
        blob += "<script>\n" + js + "\n</script>\n";
    }
    html.insert(pos, blob);
}

// Sibling UI asset by bare name, for lazily-loaded page data. Rejects anything with a path
// separator or a dot segment so the page can only ever reach its own asset directory.
std::string ReadUiAssetImpl(const std::string& name) {
    if (name.empty() || name.size() > 64) return {};
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return {};
    if (name.find("..") != std::string::npos) return {};
    for (char c : name)
        if (!(std::isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-')) return {};
    auto slash = g_client_html_path.find_last_of("\\/");
    std::string dir = (slash == std::string::npos) ? std::string() : g_client_html_path.substr(0, slash + 1);
    return read_file(dir + name);
}

// Newest write time (raw tick count) across client.html and its sibling .js files.
long long ui_dir_mtime() {
    namespace fs = std::filesystem;
    std::error_code ec;
    long long m = 0;
    auto acc = [&](const fs::path& p) {
        auto t = fs::last_write_time(p, ec);
        if (!ec) m = (std::max)(m, (long long)t.time_since_epoch().count());
    };
    fs::path html(g_client_html_path);
    acc(html);
    fs::directory_iterator it(html.parent_path(), ec), end;
    for (; !ec && it != end; it.increment(ec))
        if (it->path().extension() == L".js") acc(it->path());
    return m;
}

struct Dock : public WindowListener, public LoadListener, public ViewListener {
    std::uint32_t   pid = 0;
    RefPtr<Window>  panel;       // Ultralight sidebar (child of the host)
    RefPtr<Overlay> overlay;

    // Panel JS console output (incl. uncaught exceptions) -> per-client log.
    void OnAddConsoleMessage(View*, const ConsoleMessage& msg) override {
        const char* lvl = "log";
        switch (msg.level()) {
            case kMessageLevel_Warning: lvl = "warn"; break;
            case kMessageLevel_Error:   lvl = "ERROR"; break;
            case kMessageLevel_Debug:   lvl = "debug"; break;
            case kMessageLevel_Info:    lvl = "info"; break;
            default: break;
        }
        rtx::log::Client(pid, std::string("[js ") + lvl + " @" +
                              std::to_string(msg.line_number()) + "] " +
                              msg.message().utf8().data());
    }
    HWND host = nullptr;         // the top-level host window (the unified frame)
    HWND game = nullptr;         // the game's own top-level window, glued over the host

    // Saved game-window state, to restore on detach.
    LONG_PTR gameStyle = 0, gameExStyle = 0;
    RECT     gameOrigRect{0, 0, 0, 0};

    bool      embedded = false;
    bool      embedPending = false;       // hierarchy surgery running on the embed worker
    bool      gameIsChild = false;        // TRUE EMBED took effect: game is a WS_CHILD of host,
                                          // input queues detached (vs glued top-level pair)
    DWORD     gameThread = 0;             // game window's thread id (for the queue re-detach)
    HWND      gameInput = nullptr;        // the render-target child the game takes keyboard on
                                          // (companion-published; the frame `game` ignores keys)
    bool      embedFlagApplied = false;   // companion told (RenderToggle 4); Tick retries
    bool      kbToGame = true;            // host keyboard routing: true -> forward to the game,
                                          // false -> the panel child holds focus
    bool      keyHeld[256] = {};  // keys the host has relayed down to the game (true embed). On
                                          // focus loss the physical key-up lands in the window tabbed to, so
                                          // the game never gets it; the matching key-ups are synthesized
    bool      keepFocused = true;  // keep the client rendering when unfocused. Always on: true embed
                                          // REQUIRES it, the glued fallback wants it for smooth switching
    bool      keepFocusedApplied = false;  // push once even when off, so the companion's render section EXISTS
                                          // (it then maps it once instead of retrying OpenFileMapping per message)
    int       panelW = kPanelWidth;       // current visible sidebar width (full or rail)
    int       embedGameW = 0;             // game's render width at embed: the size the game is pinned
                                          // back to so the panel never inflates it (see HostClientForGame)
    std::uint32_t ovW = 0, ovH = 0;       // last size the Ultralight overlay was resized to (must == panel window)
    bool      collapsed = false;          // user's collapse choice
    bool      animating = false;          // collapse/expand slide in progress
    int       animStartW = 0, animTargetW = 0;
    bool      animOutward = false;        // slide resizes the WINDOW, game keeps its exact size
    RECT      animHostRect{0, 0, 0, 0};   // host window rect at slide start (outward basis)
    ULONGLONG animStartMs = 0;
    HWND      lastGroupFg = nullptr;      // last foreground z-grouped for
    ULONGLONG lastDetachMs = 0;           // last queue-detach re-assert (throttled in Tick)
    bool      inRaise = false;            // re-entrancy guard: RaiseGame's own SetWindowPos sends
                                          // WM_WINDOWPOSCHANGED back into HostProc

    void OnResize(Window*, std::uint32_t w, std::uint32_t h) override {
        if (overlay) {
            overlay->Resize(w, h);  // re-applies the window's scale() to the View's device_scale
            ovW = w; ovH = h;
        }
    }
    void OnClose(Window*) override {}

    void OnWindowObjectReady(View* v, std::uint64_t, bool is_main, const String&) override {
        if (is_main) attach(v);
    }
    void OnDOMReady(View* v, std::uint64_t, bool is_main, const String&) override {
        if (is_main) attach(v);
    }
    void attach(View* v) {
        rtx::launcher::AttachBridge(v);
        auto scoped = v->LockJSContext();
        JSContextRef ctx = scoped->ctx();
        JSObjectRef global = JSContextGetGlobalObject(ctx);
        JSStringRef key = JSStringCreateWithUTF8CString("__rtx_pid");
        JSValueRef  val = JSValueMakeNumber(ctx, (double)pid);
        JSObjectSetProperty(ctx, global, key, val,
                            kJSPropertyAttributeDontDelete |
                            kJSPropertyAttributeDontEnum |
                            kJSPropertyAttributeReadOnly, nullptr);
        JSStringRelease(key);
    }
};

std::unordered_map<std::uint32_t, Dock*> g_docks;

// Cross-thread published handle: the overlay/marker render thread needs the game window, but
// g_docks is main-thread-only. Mirror just pid->game HWND under its own small lock.
std::mutex                              g_embedded_mu;
std::unordered_map<std::uint32_t, HWND> g_embedded_games;

void SetEmbeddedGame(std::uint32_t pid, HWND game) {   // main thread only
    std::lock_guard<std::mutex> lk(g_embedded_mu);
    if (game) g_embedded_games[pid] = game;
    else      g_embedded_games.erase(pid);
}

// ---- game-window discovery -------------------------------------------------
struct FindCtx { DWORD pid; HWND best; long area; };
BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lp) {
    auto* c = reinterpret_cast<FindCtx*>(lp);
    DWORD wpid = 0;
    GetWindowThreadProcessId(hwnd, &wpid);
    if (wpid != c->pid || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
    RECT r;
    if (!GetClientRect(hwnd, &r)) return TRUE;
    long area = (long)(r.right - r.left) * (r.bottom - r.top);
    if (area > c->area && (r.right - r.left) > 200 && (r.bottom - r.top) > 200) {
        c->area = area; c->best = hwnd;
    }
    return TRUE;
}
HWND FindGameWindow(DWORD pid) {
    FindCtx c{pid, nullptr, 0};
    EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&c));
    return c.best;
}

// ---- glued layout ----------------------------------------------------------
// Position the standalone game flush over the host's client area (screen coords; the game is a
// top-level window). SWP_NOACTIVATE so moving it never enters the activation path.
void PositionGame(Dock* d, int w, int h) {
    if (!d || !d->host || !d->game || !IsWindow(d->game)) return;
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    // SWP_ASYNCWINDOWPOS everywhere the GAME is positioned: it belongs to another thread, and a
    // synchronous SetWindowPos blocks the caller until that thread pumps -- which it often doesn't
    // during loading. Posting the placement keeps the UI thread responsive no matter what.
    if (d->gameIsChild) {   // real child: client coords, moves with the host on its own
        SetWindowPos(d->game, nullptr, 0, 0, w, h,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
        return;
    }
    POINT tl{0, 0};
    ClientToScreen(d->host, &tl);                 // host client (0,0) -> screen
    SetWindowPos(d->game, nullptr, tl.x, tl.y, w, h,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
}

// Bring this client's pair to the front as a unit and keep the game just above its host: RAISE
// the game to the top, then drop the host directly beneath it. Raising the game first matters for
// multi-client -- activating a background host must lift its whole pair above the other clients,
// not sink the host down to its stranded game. z-order only (SWP_NOACTIVATE) -> no activation
// handshake, no input-queue attachment.
void RaiseGame(Dock* d) {
    if (!d || !d->host || !d->game || !IsWindow(d->game) || d->inRaise) return;
    if (d->gameIsChild) return;   // true embed: one window, the shell handles z-order
    d->inRaise = true;
    SetWindowPos(d->game, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    SetWindowPos(d->host, d->game, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    d->inRaise = false;
}

// The panel's device_scale == its current monitor scale (SyncPanelDpi keeps them equal). Layout
// constants are CSS design pixels, so multiply by this for the PHYSICAL space to reserve. Sourced
// from the host's monitor (or the game's before the host exists, at first embed).
double DockScale(Dock* d) {
    HWND ref = d ? (d->host ? d->host : d->game) : nullptr;
    unsigned dpi = ref ? DpiForWindow(ref) : 0;
    return dpi ? (dpi / 96.0) : 1.0;
}

void Layout(Dock* d) {
    if (!d || !d->host) return;
    RECT rc;
    if (!GetClientRect(d->host, &rc)) return;
    int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
    if (cw <= 0 || ch <= 0) return;
    double sc = DockScale(d);
    int rail = (int)(kRailWidth * sc + 0.5);
    int full = PanelFull(cw, sc);                  // dynamic expanded width (~30% of the window)
    // Game wins when space is tight: cap the panel to whatever's left once the game keeps its launch
    // width, so the panel COMPRESSES (responsive content) instead of squeezing the game. Roomy
    // windows are unaffected (avail >= full); never let the panel fall below the rail.
    // ONLY in outward mode: there the host can't grow past the work area, so the panel must yield.
    // Maximized the panel slides INWARD over the game on purpose (SetCollapsed picks that mode by the
    // same IsZoomed test), so this clamp there would re-collapse the panel the instant it expands.
    if (!d->collapsed && d->embedGameW > 0 && !IsZoomed(d->host)) {
        int avail = cw - d->embedGameW;
        int minPanel = (int)(kPanelMin * sc + 0.5);   // usable content floor (rail shows only icons)
        if (minPanel > cw) minPanel = cw;
        // Prefer keeping the game at its launch width; but when the window can't grow enough to fit both
        // (near-fullscreen launch, or the OS clamped the outward grow), never starve the open panel below
        // its usable minimum -- let the GAME yield instead, so the expanded panel stays visible.
        if (avail < full) full = (avail >= minPanel) ? avail : minPanel;
    }
    int vis = d->collapsed ? rail : full;          // current visible width
    if (vis > cw) vis = cw;
    if (vis < 0) vis = 0;
    d->panelW = vis;
    PositionGame(d, cw - vis, ch);
    HWND ph = d->panel ? static_cast<HWND>(d->panel->native_handle()) : nullptr;
    if (ph) {
        // The panel window is sized to `full` (it scales with the window); a region clips it to its LEFT
        // `vis` px so collapse/expand is pure clipping (no Ultralight re-layout). The panel's left edge
        // -- the icon rail -- stays flush against the game in both states.
        SetWindowPos(ph, HWND_TOP, cw - vis, 0, full, ch, SWP_NOACTIVATE);
        // Pin the Ultralight overlay to EXACTLY the panel window's pixel size: a stale overlay HEIGHT
        // makes AppCore's OverlayManager::HitTest drop mouse events below it (a dead lower band), which
        // offsets hover/clicks vertically. Source the size from GetClientRect(ph) -- the same coordinate
        // space as the incoming WM_MOUSEMOVE Y, and the same source the per-tick self-heal uses.
        RECT pcr{};
        if (d->overlay && GetClientRect(ph, &pcr)) {
            std::uint32_t pw = (std::uint32_t)(pcr.right - pcr.left), pht = (std::uint32_t)(pcr.bottom - pcr.top);
            if (pw && pht && (d->ovW != pw || d->ovH != pht)) {
                d->overlay->Resize(pw, pht);
                d->ovW = pw; d->ovH = pht;
            }
        }
        HRGN rgn = (vis >= full) ? nullptr : CreateRectRgn(0, 0, vis, ch);
        SetWindowRgn(ph, rgn, TRUE);
    }
}

// Re-pin the host AFTER embed so the game renders at exactly embedGameW for the panel's CURRENT
// collapse state: the collapse choice can arrive from the UI either before or after the first
// Layout. Sizing the host CLIENT to HostClientForGame(embedGameW, collapsed) makes Layout's split
// resolve back to embedGameW, so RS3 always persists the same window size. Clamped to the work
// area; a near-fullscreen launch that can't fit may settle a touch smaller (self-limiting).
void RepinGameWidth(Dock* d) {
    if (!d || !d->host || !d->embedGameW) return;
    RECT hc{}; if (!GetClientRect(d->host, &hc)) return;
    int curClient = hc.right - hc.left;
    int wantClient = HostClientForGame(d->embedGameW, d->collapsed, DockScale(d));
    RECT wr{}; if (!GetWindowRect(d->host, &wr)) return;
    int outerW = wr.right - wr.left, outerH = wr.bottom - wr.top;
    int nonClient = outerW - curClient;                 // frame width (caption/borders)
    int newOuter = wantClient + nonClient;
    HMONITOR mon = MonitorFromWindow(d->host, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    if (GetMonitorInfoW(mon, &mi)) {
        int waW = mi.rcWork.right - mi.rcWork.left;
        if (newOuter > waW) newOuter = waW;
    }
    if (newOuter == outerW) return;                     // already correct
    SetWindowPos(d->host, nullptr, wr.left, wr.top, newOuter, outerH,
                 SWP_NOZORDER | SWP_NOACTIVATE);          // WM_SIZE -> Layout re-splits to embedGameW
    Layout(d);                                           // deterministic re-split (don't rely on WM_SIZE timing)
}

// Make AppCore's cached scale() for the PANEL match the monitor it is on. AppCore only updates
// scale() in OnChangeDPI (driven by WM_DPICHANGED), which the OS never sends to a reparented
// CHILD window, so the panel's scale() stays frozen at its creation monitor. scale() drives BOTH
// mouse mapping (PixelsToScreen + overlay hit-test) AND the View's device_scale, so a stale value
// desyncs input from rendering. Forwarding a WM_DPICHANGED with the host monitor's real DPI
// drives OnChangeDPI to the true value; re-Layout right after so AppCore's reposition can't
// displace the panel.
void SyncPanelDpi(Dock* d) {
    if (!d || !d->panel || !d->host) return;
    HWND ph = static_cast<HWND>(d->panel->native_handle());
    if (!ph) return;
    unsigned dpi = DpiForWindow(d->host);   // the unified window's true current-monitor DPI
    if (!dpi) return;
    // Idempotence guard: when AppCore's cached scale() already matches this DPI, the
    // synthetic WM_DPICHANGED only forces a same-scale re-render - and every caller
    // (embed, expand, OS dpi-change) funnels here, so unconditional sends made panel
    // content visibly flip between two sizes whenever callers alternated inputs
    // (owner-reported). When it DOES change, the log line names both values: a panel
    // that still flip-flops means the host's reported DPI itself is alternating.
    double want = dpi / 96.0, have = d->panel->scale(), diff = have - want;
    if (diff < 0.01 && diff > -0.01) return;
    rtx::log::Launcher("panel dpi sync: scale " + std::to_string(have) + " -> " +
                       std::to_string(want) + " (host dpi " + std::to_string(dpi) + ")");
    RECT pr{}; GetWindowRect(ph, &pr);      // pass the panel's current rect as the "suggested" rect
    SendMessageW(ph, 0x02E0 /*WM_DPICHANGED*/, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&pr));
    Layout(d);                              // re-place the panel + resize the overlay at the new scale
}

// One frame of the collapse/expand slide. OUTWARD (normal): the WINDOW's right edge glides and
// the game keeps its exact size, so opening/closing the panel never resizes the client. INWARD
// (maximized fallback, no room to grow): the game is resized each frame to meet the panel's
// clipped edge. The panel itself only clips (no re-layout) in both modes.
void AnimStep(Dock* d) {
    if (!d || !d->animating || !d->host) return;
    ULONGLONG now = GetTickCount64();
    double t = (double)(now - d->animStartMs) / kAnimMs;
    bool settle = (t >= 1.0);
    double e = settle ? 1.0 : 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);   // ease-out cubic
    int w = d->animStartW + (int)((d->animTargetW - d->animStartW) * e);
    d->panelW = w;

    if (d->animOutward) {
        // Window width tracks the panel width; x/y/height stay put (grows to the right).
        int outerW = (d->animHostRect.right - d->animHostRect.left) + (w - d->animStartW);
        int outerH = d->animHostRect.bottom - d->animHostRect.top;
        SetWindowPos(d->host, nullptr, d->animHostRect.left, d->animHostRect.top,
                     outerW, outerH, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    RECT rc;
    if (!GetClientRect(d->host, &rc)) { d->animating = false; KillTimer(d->host, kAnimTimerId); return; }
    int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
    if (settle || ch <= 0) {
        d->panelW = d->animTargetW;
        d->animating = false;
        KillTimer(d->host, kAnimTimerId);
        Layout(d);                          // settle: widths/clip -> final
        return;
    }
    // Panel window stays pre-sized to the slide's widest extent; its left edge (the rail) tracks the
    // game's right edge -- outward that edge never moves -- and the region reveals its LEFT `w` px.
    int full = (d->animTargetW > d->animStartW) ? d->animTargetW : d->animStartW;
    HWND ph = d->panel ? static_cast<HWND>(d->panel->native_handle()) : nullptr;
    if (ph) SetWindowPos(ph, HWND_TOP, cw - w, 0, full, ch, SWP_NOACTIVATE);
    HRGN rgn = (ph && w < full) ? CreateRectRgn(0, 0, w, ch) : nullptr;   // clip ONLY; only create when ph exists (else SetWindowRgn never takes ownership -> leak)
    // Cover-before-uncover so the sliding seam never flashes the host background: expanding -> widen
    // the panel clip BEFORE the game edge passes that strip; collapsing -> grow the game over the
    // strip BEFORE the clip narrows. (Outward, the game position calls are no-ops.)
    if (d->animTargetW > d->animStartW) {
        if (ph) SetWindowRgn(ph, rgn, TRUE);
        PositionGame(d, cw - w, ch);
    } else {
        PositionGame(d, cw - w, ch);
        if (ph) SetWindowRgn(ph, rgn, TRUE);
    }
}

void Detach(Dock* d, bool closeGame = false);   // fwd
void FinishEmbed(Dock* d);                      // fwd (kMsgEmbedDone handler)

// INDEPENDENT lifetimes: closing the last RS3 instance leaves the launcher OPEN. The launcher
// only exits when the USER closes it (main.cpp OnClose -> app_->Quit()).
void QuitIfNoClients(const char* why) {
    (void)why;   // intentionally a no-op: the launcher persists past the last client
}

// True embed: when the host loses focus while keys are held, the real key-ups land in the window
// tabbed to, so the game never gets them and a key sticks down. Synthesize the matching key-ups
// and post them to the input window the game listens on (so the companion's GetKeyState mirror
// clears too). Alt/F10 are SYSKEYs; everything else is a plain key.
void ReleaseHeldKeysToGame(Dock* d) {
    if (!d || !d->gameIsChild) return;
    HWND kb = (d->gameInput && IsWindow(d->gameInput)) ? d->gameInput : d->game;
    if (!kb || !IsWindow(kb)) return;
    for (int vk = 0; vk < 256; ++vk) {
        if (!d->keyHeld[vk]) continue;
        d->keyHeld[vk] = false;
        UINT sc = MapVirtualKeyW((UINT)vk, MAPVK_VK_TO_VSC);
        LPARAM lp = (LPARAM)(((sc & 0xff) << 16) | 0xC0000001u);   // transition + prev-down = a key-up
        UINT m = (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU || vk == VK_F10) ? WM_SYSKEYUP : WM_KEYUP;
        PostMessageW(kb, m, (WPARAM)vk, lp);
    }
}

// ---- host window proc ------------------------------------------------------
LRESULT CALLBACK HostProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Dock* d = reinterpret_cast<Dock*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_TIMER:
            if (wp == kAnimTimerId && d) AnimStep(d);
            return 0;
        case WM_SIZE:
            if (d) {
                if (d->gameIsChild) {
                    // Real child: minimizes/restores with the host natively; just track size.
                    if (wp != SIZE_MINIMIZED && !d->animating) Layout(d);
                    return 0;
                }
                // The standalone game doesn't minimize with the host (no owner), so do it
                // here: hide it when the host minimizes, show + re-glue when it restores.
                if (wp == SIZE_MINIMIZED) {
                    if (d->game && IsWindow(d->game)) ShowWindow(d->game, SW_HIDE);
                } else {
                    if (d->game && IsWindow(d->game) && !IsWindowVisible(d->game))
                        ShowWindow(d->game, SW_SHOWNOACTIVATE);
                    if (!d->animating) Layout(d);   // don't fight the slide
                    RaiseGame(d);   // maximize/resize can drop the host above the game -> re-raise
                }
            }
            return 0;
        case WM_MOVE:
            // Glued only: the game is its own top-level window, so it doesn't move with the host
            // like a child would -- reposition it to stay glued over the host's client area.
            if (d && !d->gameIsChild && !d->animating) Layout(d);
            return 0;
        case WM_EXITSIZEMOVE:
            // The user finished dragging/resizing the unified window: remember WHERE they put it, per
            // account, so it reopens there next launch (programmatic moves don't raise this).
            if (d && d->host) {
                RECT r; if (GetWindowRect(d->host, &r)) rtx::launcher::SaveWindowPos(d->pid, r.left, r.top);
                // A drag-resize is also the user CHOOSING a new game width, so re-baseline the pin to the
                // split they ended on: Layout's outward clamp re-inflates the game back to embedGameW
                // whenever the window has room, so a stale pin ratchets the window wider on every toggle.
                if (d->embedded && !d->animating && d->embedGameW > 0) {
                    RECT rc;
                    if (GetClientRect(d->host, &rc)) {
                        int gw = (rc.right - rc.left) - d->panelW;
                        if (gw > 0) d->embedGameW = gw;
                    }
                }
            }
            break;
        case WM_DPICHANGED:
            // The unified window moved to a monitor with a different DPI. Let DefWindowProc resize the host
            // to the suggested rect first, then re-sync the panel child's AppCore scale (the child never
            // gets its own WM_DPICHANGED -- see SyncPanelDpi).
            if (d && d->gameIsChild) {
                LRESULT r = DefWindowProcW(hwnd, msg, wp, lp);
                SyncPanelDpi(d);
                return r;
            }
            break;
        case WM_PARENTNOTIFY: {
            // The panel child doesn't take keyboard focus on click the way top-level windows do. Hand it
            // the focus when clicked so text fields receive keystrokes, and stop routing keys to the game.
            // (The game child opts out of WM_PARENTNOTIFY -- it would be a synchronous cross-process send
            // from its thread; the companion POSTS kMsgGameClicked instead.)
            UINT ev = LOWORD(wp);
            if (d && (ev == WM_LBUTTONDOWN || ev == WM_RBUTTONDOWN || ev == WM_MBUTTONDOWN)) {
                HWND ph = d->panel ? static_cast<HWND>(d->panel->native_handle()) : nullptr;
                POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                if (ph && ChildWindowFromPoint(hwnd, pt) == ph) {
                    d->kbToGame = false;
                    SetFocus(ph);
                }
            }
            return 0;
        }
        case rtx::render::kMsgGameClicked:
            // Companion: the user pressed a mouse button in the embedded game area. Take real keyboard
            // focus on the host and route keys to the game from here on. Only call SetFocus when the host
            // doesn't ALREADY hold focus: a redundant transition erases + repaints the panel for a frame.
            if (d && d->gameIsChild) {
                d->kbToGame = true;
                if (GetFocus() != hwnd) SetFocus(hwnd);
            }
            return 0;
        case kMsgEmbedDone:
            // The embed worker finished the hierarchy surgery (possibly much later, if the
            // client's window thread was parked) -- finalize on the main thread.
            if (d) FinishEmbed(d);
            return 0;
        case WM_NCACTIVATE:
            // TRUE EMBED detaches the game child's input queue, so each in-game click briefly flips the
            // host's perceived active state and Windows repaints the title bar inactive->active. Keep the
            // caption painted ACTIVE so it never toggles. Appearance only: real activation, focus and
            // keyboard relay are unaffected. Glued mode keeps default behaviour.
            if (d && d->gameIsChild)
                return DefWindowProcW(hwnd, WM_NCACTIVATE, TRUE, lp);
            break;
        case WM_ACTIVATE:
            if (d && LOWORD(wp) != WA_INACTIVE) {
                if (d->gameIsChild) {
                    // Coming back (alt-tab/title click): resume the chosen keyboard route.
                    if (d->kbToGame) SetFocus(hwnd);
                    else if (HWND ph = d->panel ? static_cast<HWND>(d->panel->native_handle())
                                                : nullptr) SetFocus(ph);
                } else {
                    // Glued: lift the pair so the game shows over the frame. z-only +
                    // SWP_NOACTIVATE: no activation handshake toward the game.
                    RaiseGame(d);
                }
            } else if (d) {
                // Losing focus (Alt+Tab away): release any relayed keys so a held modifier
                // doesn't stick down -- the game's detached queue won't get the real key-ups.
                ReleaseHeldKeysToGame(d);
            }
            return 0;
        case WM_ACTIVATEAPP:
            // App-level deactivation (switching to another process entirely). Same stuck-key risk.
            if (d && wp == FALSE) ReleaseHeldKeysToGame(d);
            break;
        case WM_INPUTLANGCHANGE:
            // Keyboard layout is PER-THREAD on Windows. With the game's input queue detached it
            // never sees the host's layout switch, so its own key-NAME lookups (the keybind
            // config list) resolve against whatever layout its thread started with, and can show
            // a different key than the one being pressed. Ask it to follow the host's layout.
            if (d && d->gameIsChild && d->kbToGame) {
                HWND kbl = (d->gameInput && IsWindow(d->gameInput)) ? d->gameInput : d->game;
                if (kbl && IsWindow(kbl)) PostMessageW(kbl, WM_INPUTLANGCHANGEREQUEST, 0, lp);
            }
            break;
        case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR: case WM_DEADCHAR:
        case WM_SYSKEYDOWN: case WM_SYSKEYUP: case WM_SYSCHAR:
            // True embed: the game's input queue is detached, so it can never hold real keyboard
            // focus -- the host does, and relays the messages. Relay to the companion-published INPUT
            // window (the render-target child the game listens on); the reparented frame window
            // discards key messages.
            // The companion suppresses the game's own keyboard translation (the WM_CHARs relayed from here
            // are already translated with the REAL keyboard state) and mirrors key state for GetKeyState.
            if (d && d->gameIsChild && d->kbToGame) {
                // Tile-marker keybinds: the mark/delete key adds/removes a marker on the tile UNDER THE
                // CURSOR, but only while the Markers panel is open (MarkAtCursor no-ops when disarmed, so
                // with default keys A/D the key falls through to the game). Down-edge only.
                if (msg == WM_KEYDOWN && !(lp & 0x40000000) && d->pid) {
                    // Screenshot keybind: user-defined, no default. On a match, capture the game window and
                    // CONSUME the key so it doesn't also act in-game. 0 = unbound -> skipped.
                    int ssVk = rtx::launcher::ScreenshotVk();
                    if (ssVk && (int)wp == ssVk) {
                        rtx::launcher::CaptureScreenshotForPid(d->pid);
                        return 0;
                    }
                    auto kb2 = rtx::markers::GetKeybinds();
                    // Consume the key ONLY when it actually marks/deletes a tile under the cursor; otherwise
                    // MarkAtCursor returns false and the key falls through so it still reaches the game.
                    if (kb2.markVk && (int)wp == kb2.markVk) {
                        if (rtx::markers::MarkAtCursor(d->pid, 1)) { rtx::overlay::EnableMarkers(d->pid); return 0; }
                    } else if (kb2.removeVk && (int)wp == kb2.removeVk) {
                        if (rtx::markers::MarkAtCursor(d->pid, -1)) return 0;
                    }
                }
                HWND kb = (d->gameInput && IsWindow(d->gameInput)) ? d->gameInput : d->game;
                if (kb && IsWindow(kb)) {
                    if (msg == WM_SYSKEYDOWN && wp == VK_F4) break;   // keep Alt+F4 = close
                    // Mirror what the game now believes is held, so a focus-loss can release it.
                    if (wp < 256) {
                        if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) d->keyHeld[wp] = true;
                        else if (msg == WM_KEYUP || msg == WM_SYSKEYUP) d->keyHeld[wp] = false;
                    }
                    PostMessageW(kb, msg, wp, lp);
                    return 0;
                }
            }
            break;
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
            // Wheel is delivered to the focus window (the host), not the window under the
            // cursor -- route it to whichever child the cursor is actually over (for the game,
            // its inner input window).
            if (d && d->gameIsChild) {
                POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };   // screen coords
                ScreenToClient(hwnd, &pt);
                HWND target = ChildWindowFromPointEx(hwnd, pt,
                                  CWP_SKIPINVISIBLE | CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT);
                if (target == d->game && d->gameInput && IsWindow(d->gameInput))
                    target = d->gameInput;
                if (target && target != hwnd) { PostMessageW(target, msg, wp, lp); return 0; }
            }
            break;
        case WM_WINDOWPOSCHANGED:
            // The SYSTEM also raises the host on its own -- clicking a background client's title bar raises
            // it AFTER the WM_ACTIVATE handler ran, leaving the host's dark frame above the game. Re-glue on
            // any z-order change not issued here (inRaise guards recursion: RaiseGame's SetWindowPos sends
            // this message back synchronously). Fall through to DefWindowProc, which synthesizes WM_SIZE/MOVE.
            if (d && d->embedded && !d->inRaise) {
                auto* wpos = reinterpret_cast<WINDOWPOS*>(lp);
                if (!(wpos->flags & SWP_NOZORDER)) RaiseGame(d);
            }
            break;
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            double sc = DockScale(d);   // panel min is CSS px -> physical; 320 game min stays physical
            mmi->ptMinTrackSize.x = (int)(kPanelMin * sc + 0.5) + 320;   // room for game + a usable panel
            mmi->ptMinTrackSize.y = 240;
            // Let the outward panel slide grow the window to the FULL width of its current monitor: the OS
            // default max track size is ~the primary monitor, so on a wider or secondary monitor SetWindowPos
            // is clamped. Cap at the host monitor's work area so growth stays on-screen.
            HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi{ sizeof(mi) };
            if (GetMonitorInfoW(mon, &mi)) {
                int waW = mi.rcWork.right - mi.rcWork.left;
                int waH = mi.rcWork.bottom - mi.rcWork.top;
                if (waW > mmi->ptMaxTrackSize.x) mmi->ptMaxTrackSize.x = waW;
                if (waH > mmi->ptMaxTrackSize.y) mmi->ptMaxTrackSize.y = waH;
            }
            return 0;
        }
        case WM_CLOSE:
            // Closing the unified window TERMINATES the game immediately (Detach kills the pid first,
            // then tears down). If that was the last client, the launcher exits too.
            if (d) {
                std::uint32_t pid = d->pid;
                Detach(d, /*closeGame=*/true);
                g_docks.erase(pid);
                QuitIfNoClients("last client window closed");
            }
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void EnsureHostClass() {
    static bool reg = false;
    if (reg) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    // No CS_HREDRAW/CS_VREDRAW: a resize must not invalidate + erase the whole host client, or
    // the dark background flashes over the game/panel region before they repaint.
    wc.style = 0;
    wc.lpfnWndProc = HostProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!g_darkBrush) g_darkBrush = CreateSolidBrush(RGB(11, 13, 18));
    wc.hbrBackground = g_darkBrush;
    wc.lpszClassName = kHostClass;
    wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);
    reg = true;
}

// Reparent the PANEL into the host as a child. Same process -> no cross-process cost.
void ReparentAsChild(HWND child, HWND parent) {
    LONG_PTR st = GetWindowLongPtrW(child, GWL_STYLE);
    st &= ~(WS_POPUP | WS_OVERLAPPED | WS_CAPTION | WS_THICKFRAME |
            WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_BORDER | WS_DLGFRAME);
    st |= WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS;
    SetWindowLongPtrW(child, GWL_STYLE, st);
    // Keep WM_PARENTNOTIFY firing for this child so the host can route click-to-focus.
    LONG_PTR ex = GetWindowLongPtrW(child, GWL_EXSTYLE);
    ex &= ~static_cast<LONG_PTR>(WS_EX_NOPARENTNOTIFY);
    SetWindowLongPtrW(child, GWL_EXSTYLE, ex);
    SetParent(child, parent);
    SetWindowPos(child, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOSIZE | SWP_NOMOVE | SWP_SHOWWINDOW);
}

// ---- DPI diagnostics -------------------------------------------------------------------------
// Current-monitor DPI helpers, dynamically resolved so the build never depends on the SDK
// declaring them (GetDpiForWindow: Win10 1607+; the awareness-context pair: 1607/1803+).
unsigned DpiForWindow(HWND h) {
    using Fn = UINT(WINAPI*)(HWND);
    static Fn fn = reinterpret_cast<Fn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    return (fn && h) ? fn(h) : 0;
}
const char* DpiAwarenessStr(HWND h) {
    using CtxFn = HANDLE(WINAPI*)(HWND);   // GetWindowDpiAwarenessContext -> DPI_AWARENESS_CONTEXT
    using AwFn  = int(WINAPI*)(HANDLE);     // GetAwarenessFromDpiAwarenessContext -> DPI_AWARENESS
    static CtxFn ctxFn = reinterpret_cast<CtxFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetWindowDpiAwarenessContext"));
    static AwFn awFn = reinterpret_cast<AwFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetAwarenessFromDpiAwarenessContext"));
    if (!ctxFn || !awFn || !h) return "?";
    switch (awFn(ctxFn(h))) {                // 0=unaware, 1=system, 2=per-monitor
        case 0:  return "unaware";
        case 1:  return "system";
        case 2:  return "per-monitor";
        default: return "invalid";
    }
}

// Strip the game's frame so it reads as the host's content -- but it stays a fully independent
// top-level window: NO parent, NO owner, so its input queue and activation stay single-process
// (attaching them costs input lag + window-switch stutter).
void MakeIndependentTopLevel(HWND game) {
    LONG_PTR st = GetWindowLongPtrW(game, GWL_STYLE);
    st &= ~(WS_CHILD | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX |
            WS_MAXIMIZEBOX | WS_SYSMENU | WS_BORDER | WS_DLGFRAME);
    st |= WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS;
    SetWindowLongPtrW(game, GWL_STYLE, st);
    // Glued mode keeps the game a top-level window, so strip its taskbar / Alt-Tab identity (the
    // host is the single shell entry). The shell only re-reads tool-window status when a window is
    // shown, so toggle visibility once for the change to register; Layout + RaiseGame follow.
    LONG_PTR ex   = GetWindowLongPtrW(game, GWL_EXSTYLE);
    LONG_PTR want = (ex & ~static_cast<LONG_PTR>(WS_EX_APPWINDOW)) | WS_EX_TOOLWINDOW;
    if (want != ex) {
        ShowWindow(game, SW_HIDE);
        SetWindowLongPtrW(game, GWL_EXSTYLE, want);
        ShowWindow(game, SW_SHOWNOACTIVATE);
    }
    SetWindowPos(game, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOSIZE | SWP_NOMOVE | SWP_SHOWWINDOW);
}

// TRUE EMBED: make the game a WS_CHILD of the host, then immediately sever the input-queue
// attachment cross-process SetParent silently creates (that handshake is the window-switch
// stall). Runs on the EMBED WORKER, never the UI thread: the style strip and SetParent are
// synchronous sends into the game's window thread, which can park for MINUTES mid-preload.
// Completion is posted to the host (kMsgEmbedDone) and finalized there, on the main thread.
void EmbedSurgery(HWND game, HWND host, DWORD hostThread) {
    LONG_PTR st = GetWindowLongPtrW(game, GWL_STYLE);
    st &= ~(WS_POPUP | WS_OVERLAPPED | WS_CAPTION | WS_THICKFRAME |
            WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_BORDER | WS_DLGFRAME);
    st |= WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS;
    SetWindowLongPtrW(game, GWL_STYLE, st);
    // Drop the frame's app-window (taskbar / Alt-Tab) identity and mark it a tool window so the
    // shell stops tracking it as a standalone app once it is a host child; without this Explorer's
    // labeled-taskbar path re-surfaces it as a separate window beside the panel. This GWL_EXSTYLE
    // write is another synchronous send into the game's window thread, safe only because this runs
    // on the embed worker. HIDE first: Explorer only drops/re-reads a window's taskbar button on a
    // visibility cycle, so restyling + SetParent alone leaves a dead button behind. The
    // SetWindowPos below (SWP_SHOWWINDOW) re-shows the window after SetParent, as a child the
    // shell ignores. The host is still hidden here (shown in FinishEmbed), so no extra flicker.
    ShowWindow(game, SW_HIDE);
    LONG_PTR ex = GetWindowLongPtrW(game, GWL_EXSTYLE);
    SetWindowLongPtrW(game, GWL_EXSTYLE,
                      (ex & ~static_cast<LONG_PTR>(WS_EX_APPWINDOW)) | WS_EX_TOOLWINDOW);
    // (WM_PARENTNOTIFY suppression on the inner input window is the COMPANION's job, applied on the
    // game's own window thread -- that inner window isn't known here.)
    SetLastError(0);
    SetParent(game, host);
    DWORD spErr = GetLastError();
    bool reparented = (GetParent(game) == host);   // did the child relationship actually take?
    // Sever the implicit queue attachment SetParent just made, both directions (detaching a
    // pair that isn't attached just fails harmlessly). Tick re-asserts this: the system can
    // quietly re-attach related windows' queues on later hierarchy/activation events.
    DWORD gameThread = GetWindowThreadProcessId(game, nullptr);
    AttachThreadInput(gameThread, hostThread, FALSE);
    AttachThreadInput(hostThread, gameThread, FALSE);
    SetWindowPos(game, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOSIZE | SWP_NOMOVE | SWP_SHOWWINDOW |
                 SWP_ASYNCWINDOWPOS);
    DWORD gamePid = 0; GetWindowThreadProcessId(game, &gamePid);
    if (gamePid)
        rtx::log::Client(gamePid, std::string("embed surgery: SetParent ") +
                         (reparented ? "ok" : "FAILED") + " (err=" + std::to_string(spErr) + ")");
    PostMessageW(host, kMsgEmbedDone, reinterpret_cast<WPARAM>(game), reparented ? 1 : 0);
}

// Taskbar identity for the unified host. Explorer groups taskbar buttons by AppUserModelID,
// which DEFAULTS to the owning process's exe, so without an explicit one every host groups
// under the launcher's button. One shared ID gives the hosts their own "RuneScape" group.
// id=nullptr clears the property store, which the shell asks for before the window is
// destroyed. PROPERTYKEY spelled out (System.AppUserModel.ID) so no INITGUID/propkey.h dance.
const PROPERTYKEY kPkeyAppUserModelId =
    { {0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5 };
constexpr wchar_t kHostAppId[] = L"RuneTools.RuneScape";

void SetHostAppId(HWND host, const wchar_t* id) {
    // Nothing here initializes COM; scope an apartment for this call. S_FALSE/RPC_E_CHANGED_MODE
    // both mean COM is already up, and the property store works either way.
    HRESULT ci = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IPropertyStore* ps = nullptr;
    if (SUCCEEDED(SHGetPropertyStoreForWindow(host, __uuidof(IPropertyStore),
                                              reinterpret_cast<void**>(&ps))) && ps) {
        PROPVARIANT pv;
        PropVariantInit(&pv);                                  // VT_EMPTY = clear
        if (id) { pv.vt = VT_LPWSTR; pv.pwszVal = const_cast<wchar_t*>(id); }
        ps->SetValue(kPkeyAppUserModelId, pv);                 // copies; no Clear (static string)
        ps->Release();
    }
    if (SUCCEEDED(ci)) CoUninitialize();
}

// Give the host the game's title-bar icon so the unified window reads as RuneScape.
void WearGameIcon(HWND host, HWND game) {
    auto pull = [&](WPARAM which, int classIdx) -> HICON {
        // Timeout-guarded: WM_GETICON is a synchronous send into the game's thread.
        DWORD_PTR r = 0;
        SendMessageTimeoutW(game, WM_GETICON, which, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &r);
        HICON ic = reinterpret_cast<HICON>(r);
        if (!ic) ic = reinterpret_cast<HICON>(GetClassLongPtrW(game, classIdx));
        return ic;
    };
    if (HICON big = pull(ICON_BIG, GCLP_HICON))     SendMessageW(host, WM_SETICON, ICON_BIG,   (LPARAM)big);
    if (HICON sm  = pull(ICON_SMALL, GCLP_HICONSM)) SendMessageW(host, WM_SETICON, ICON_SMALL, (LPARAM)sm);
}

// Build the host + glue the standalone game beside the panel. Returns false if game not ready.
bool Embed(Dock* d) {
    if (d->embedded) return true;
    if (d->embedPending) return false;   // surgery in flight; kMsgEmbedDone finalizes
    HWND game = FindGameWindow(d->pid);
    if (!game) return false;
    // Never start the embed against a client that isn't pumping messages (its window thread stalls
    // during login/world preload): SetParent and the follow-up window ops are SYNCHRONOUS sends into
    // that thread, so embedding mid-stall hangs the UI thread too. Tick retries.
    if (IsHungAppWindow(game)) return false;
    DWORD_PTR probe = 0;
    if (!SendMessageTimeoutW(game, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 250, &probe))
        return false;

    // Save the game's state for restore on detach.
    d->game = game;
    d->gameStyle = GetWindowLongPtrW(game, GWL_STYLE);
    d->gameExStyle = GetWindowLongPtrW(game, GWL_EXSTYLE);
    GetWindowRect(game, &d->gameOrigRect);

    // Host sized so the game keeps its current RENDER size and the panel adds on the right (see
    // HostClientForGame). Size from the game's CLIENT rect, NOT its window rect: the embed strips
    // the game's frame, so sizing the host client to the WINDOW rect inflates the now-borderless
    // game's render area by that frame. gameOrigRect is kept for restore + the host's screen pos.
    RECT gw = d->gameOrigRect;
    RECT gcr{};
    int gameW, gameH;
    if (GetClientRect(game, &gcr) && gcr.right > 0 && gcr.bottom > 0) {
        gameW = gcr.right - gcr.left; gameH = gcr.bottom - gcr.top;   // render area (frame excluded)
    } else {
        gameW = gw.right - gw.left;  gameH = gw.bottom - gw.top;      // fallback: window size
    }
    // Pin the game to the width it launched at; the panel adds on the right (HostClientForGame is the
    // SAME split Layout uses, so the game can't be inflated by a reserved-but-collapsed panel).
    d->embedGameW = gameW;
    int wantClient = HostClientForGame(gameW, d->collapsed, DockScale(d));
    RECT want{0, 0, wantClient, gameH};
    AdjustWindowRectEx(&want, WS_OVERLAPPEDWINDOW, FALSE, 0);
    int hostW = want.right - want.left, hostH = want.bottom - want.top;

    // Reopen the unified window where the user last left it (per account); fall back to the game's
    // own position. RS3 owns the game child and can't persist the host's screen position.
    int x = gw.left, y = gw.top;
    { int sx, sy; if (rtx::launcher::LoadWindowPos(d->pid, sx, sy)) { x = sx; y = sy; } }

    // Keep the unified window on-screen: clamp to the monitor that now contains (x,y) -- handles a
    // saved position on a monitor that has since changed, and a near-fullscreen game.
    HMONITOR mon = MonitorFromPoint(POINT{ x, y }, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    if (GetMonitorInfoW(mon, &mi)) {
        int waW = mi.rcWork.right - mi.rcWork.left, waH = mi.rcWork.bottom - mi.rcWork.top;
        if (hostW > waW) hostW = waW;
        if (hostH > waH) hostH = waH;
        if (x + hostW > mi.rcWork.right)  x = mi.rcWork.right - hostW;
        if (y + hostH > mi.rcWork.bottom) y = mi.rcWork.bottom - hostH;
        if (x < mi.rcWork.left) x = mi.rcWork.left;
        if (y < mi.rcWork.top)  y = mi.rcWork.top;
    }

    EnsureHostClass();
    // WS_EX_APPWINDOW: the unified window is the ONE shell entry for this client. Explicit so it
    // owns the taskbar button even after the game frame's own app-window identity is stripped at embed.
    d->host = CreateWindowExW(WS_EX_APPWINDOW, kHostClass, L"RuneScape",
                              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                              x, y, hostW, hostH,
                              nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!d->host) return false;

    // Diagnostic: DPI + size inputs.
    rtx::log::Client(d->pid,
        "[dpi] game{aware=" + std::string(DpiAwarenessStr(game)) +
        " dpi=" + std::to_string(DpiForWindow(game)) +
        " client=" + std::to_string(gameW) + "x" + std::to_string(gameH) +
        " win=" + std::to_string(gw.right - gw.left) + "x" + std::to_string(gw.bottom - gw.top) +
        "} host{aware=" + std::string(DpiAwarenessStr(d->host)) +
        " dpi=" + std::to_string(DpiForWindow(d->host)) +
        "} monWork=" + std::to_string(mi.rcWork.right - mi.rcWork.left) + "x" +
        std::to_string(mi.rcWork.bottom - mi.rcWork.top) +
        " wantClient=" + std::to_string(wantClient) +
        " collapsed=" + std::to_string(d->collapsed ? 1 : 0) +
        " host=" + std::to_string(hostW) + "x" + std::to_string(hostH) +
        " at(" + std::to_string(x) + "," + std::to_string(y) + ")");
    SetWindowLongPtrW(d->host, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(d));
    SetHostAppId(d->host, kHostAppId);   // own taskbar group, BEFORE the host is ever shown
    WearGameIcon(d->host, game);
    // Kill the DWM activation transition on the host: focus switches to/from the unified window
    // then schedule no compositor fade (trims the switch cost).
    BOOL noTransitions = TRUE;
    DwmSetWindowAttribute(d->host, DWMWA_TRANSITIONS_FORCEDISABLED, &noTransitions, sizeof(noTransitions));

    // Panel becomes a host child (same process); the game either becomes a real child too
    // (true embed) or stays its OWN top-level glued over the host (fallback).
    HWND ph = d->panel ? static_cast<HWND>(d->panel->native_handle()) : nullptr;
    if (ph) ReparentAsChild(ph, d->host);
    if (kTrueEmbed) {
        // Hand the hierarchy surgery to a worker; the host stays hidden until kMsgEmbedDone
        // arrives in HostProc (FinishEmbed). If the game's window thread parks mid-surgery,
        // only the worker waits; the UI thread stays live.
        d->embedPending = true;
        HWND host = d->host;
        DWORD hostThread = GetCurrentThreadId();
        std::thread([game, host, hostThread]() {
            EmbedSurgery(game, host, hostThread);
        }).detach();
        return false;   // not embedded yet; completion finalizes the state
    }

    MakeIndependentTopLevel(game);
    d->embedded = true;
    SetEmbeddedGame(d->pid, d->game);   // publish for the overlay/marker thread
    ShowWindow(d->host, SW_SHOW);
    Layout(d);                          // position the game over the host client area
    RepinGameWidth(d);                  // pin the game to its launch width (defeats the per-load growth)
    SyncPanelDpi(d);                    // make the panel's AppCore scale match the host's real monitor
    SetForegroundWindow(d->host);       // activate the unified window (raises the host)...
    RaiseGame(d);                       // glued: lift the game above the host -- LAST, so the
                                        //    game (not the host's frame) shows on open
    rtx::log::Client(d->pid, "client glued to host window");
    return true;
}

// kMsgEmbedDone: the embed worker finished the hierarchy surgery -- finalize the dock state
// on the main thread and reveal the unified window.
void FinishEmbed(Dock* d) {
    if (!d || !d->host || d->embedded) return;
    d->embedPending = false;
    if (!d->game || !IsWindow(d->game)) return;   // client died mid-surgery; Tick cleans up
    // Confirm the child relationship actually holds at finalize (SetParent may have failed, or the
    // shell reverted it). Proceed regardless so the panel host stays usable; the log records it.
    if (GetParent(d->game) != d->host)
        rtx::log::Client(d->pid,
            "warning: game is not our child at finalize -- a separate game window may appear; "
            "reparent failed or was reverted by the shell");
    d->gameThread = GetWindowThreadProcessId(d->game, nullptr);
    d->gameIsChild = true;
    d->kbToGame = true;                 // the game area is the natural first keyboard target
    // keep-focused is REQUIRED here, not opt-in: the game's detached queue never gets real
    // focus again, so the client must be kept believing it has it. Tick pushes it.
    d->keepFocusedApplied = false;
    d->embedded = true;
    SetEmbeddedGame(d->pid, d->game);   // publish for the overlay/marker thread
    ShowWindow(d->host, SW_SHOW);
    Layout(d);                          // position the game over the host client area
    RepinGameWidth(d);                  // pin the game to its launch width (defeats the per-load growth)
    SyncPanelDpi(d);                    // make the panel's AppCore scale match the host's real monitor
    SetForegroundWindow(d->host);       // activate the unified window
    rtx::log::Client(d->pid, "client embedded into host window");
    // Diagnostic: post-embed sizes.
    {
        RECT hc{}, gc{}, pc{};
        GetClientRect(d->host, &hc);
        if (d->game && IsWindow(d->game)) GetClientRect(d->game, &gc);
        HWND ph = d->panel ? static_cast<HWND>(d->panel->native_handle()) : nullptr;
        if (ph) GetClientRect(ph, &pc);
        rtx::log::Client(d->pid,
            "[dpi] post-embed hostClient=" + std::to_string(hc.right - hc.left) + "x" +
            std::to_string(hc.bottom - hc.top) +
            " gameClient=" + std::to_string(gc.right - gc.left) + "x" +
            std::to_string(gc.bottom - gc.top) +
            " panelClient=" + std::to_string(pc.right - pc.left) + "x" +
            std::to_string(pc.bottom - pc.top) +
            " panelW=" + std::to_string(d->panelW) +
            " hostDpi=" + std::to_string(DpiForWindow(d->host)) +
            " gameDpi=" + std::to_string(DpiForWindow(d->game)) +
            " panelDpi=" + std::to_string(DpiForWindow(ph)));
    }
}

// ---- Panel mouse: pure passthrough + DPI verification -----------------------------------------
// No coordinate transform. AppCore's input pipeline (PixelsToScreen + overlay hit-test + the
// View's device_scale) is consistent ONLY when the window's scale() equals the real monitor
// scale, and a coordinate fixup can't restore that (the hit-test re-multiplies by scale()).
// SyncPanelDpi fixes scale() at the source; this subclass only emits a one-shot diagnostic.
struct PanelHook { WNDPROC orig; Dock* dock; };
std::unordered_map<HWND, PanelHook> g_panelHooks;   // main-thread only, like g_docks

LRESULT CALLBACK PanelMouseProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto it = g_panelHooks.find(h);
    if (it == g_panelHooks.end()) return DefWindowProcW(h, msg, wp, lp);   // unreachable while subclassed
    // Control characters must never reach the view as TEXT. Ctrl+<key> combinations translate to
    // WM_CHAR 0x01-0x1A, and Ctrl+Backspace to 0x7F (DEL); the view has no editing command bound
    // to those, so it inserted them literally and the field filled up with .notdef boxes
    // (user-reported: "a square appears when I press Ctrl+Backspace"). Nothing is lost by
    // dropping them - the matching WM_KEYDOWN still arrives, so the page can act on the combo.
    // Backspace, Tab and Return are deliberately kept: those ARE the editing keys.
    if (msg == WM_CHAR || msg == WM_SYSCHAR) {
        const wchar_t c = static_cast<wchar_t>(wp);
        const bool keep = (c == L'\b' || c == L'\t' || c == L'\r' || c == L'\n');
        if (!keep && (c < 0x20 || c == 0x7F)) return 0;
    }
    if (msg == WM_LBUTTONDOWN) {
        Dock* d = it->second.dock;
        if (d) {
            double s = d->panel ? d->panel->scale() : 1.0;
            double ds = (d->overlay && d->overlay->view()) ? d->overlay->view()->device_scale() : 1.0;
            unsigned mdpi = DpiForWindow(h);
            RECT pcr{}; GetClientRect(h, &pcr);
            rtx::log::Client(d->pid,
                "[mouse] raw=(" + std::to_string(GET_X_LPARAM(lp)) + "," + std::to_string(GET_Y_LPARAM(lp)) +
                ") scale()=" + std::to_string(s) + " device_scale=" + std::to_string(ds) +
                " monScale=" + std::to_string((mdpi > 0) ? (mdpi / 96.0) : 1.0) +
                " panelClient=" + std::to_string(pcr.right - pcr.left) + "x" +
                std::to_string(pcr.bottom - pcr.top) +
                " overlay=" + std::to_string(d->ovW) + "x" + std::to_string(d->ovH));
        }
    }
    return CallWindowProcW(it->second.orig, h, msg, wp, lp);   // pass through unchanged
}

void InstallPanelMouseHook(Dock* d) {
    if (!d || !d->panel) return;
    HWND h = static_cast<HWND>(d->panel->native_handle());
    if (!h || g_panelHooks.count(h)) return;
    WNDPROC orig = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(PanelMouseProc)));
    if (!orig) return;   // subclass failed -> leave AppCore's proc as-is
    g_panelHooks[h] = PanelHook{ orig, d };
    rtx::log::Client(d->pid, std::string("panel mouse hook installed; panel scale=") +
                             std::to_string(d->panel->scale()));
}

void UninstallPanelMouseHook(Dock* d) {
    if (!d || !d->panel) return;
    HWND h = static_cast<HWND>(d->panel->native_handle());
    auto it = g_panelHooks.find(h);
    if (it == g_panelHooks.end()) return;
    SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(it->second.orig));
    g_panelHooks.erase(it);
}

void Detach(Dock* d, bool closeGame) {
    if (!d) return;

    // 0) Closing the unified window KILLS the client FIRST, immediately (no graceful WM_CLOSE): a
    //    windowless zombie client poisoning the next launch is worse than losing its shutdown path.
    //    HIDE the host before the kill: TerminateProcess returns while the kernel is still reaping
    //    the client (seconds), and a visible window mid-teardown ghosts as "Not Responding".
    if (closeGame) {
        if (d->host) ShowWindow(d->host, SW_HIDE);   // host window, own thread: instant
        if (HANDLE hp = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)d->pid)) {
            TerminateProcess(hp, 0);
            CloseHandle(hp);
        }
        rtx::log::Client(d->pid, "client terminated with its window");
    }

    // 1) Stand the companion down COMPLETELY before the game is touched: markers off, every render
    //    toggle off, and -- critically -- keep-focused OFF. Keep-focused swallows the game's
    //    focus-loss messages and fakes its foreground state; if still active while the game shuts
    //    down, RS3's close path waits on activation state it can never observe and hangs.
    rtx::overlay::QuiesceMarkers(d->pid);
    SetEmbeddedGame(d->pid, nullptr);
    for (int which = 0; which <= 4; ++which) rtx::reader::RenderToggle(d->pid, which, false);
    rtx::reader::VarsWatch(d->pid, false);
    rtx::launcher::HudClose(d->pid);   // release the HUD section mapping (else one handle + view leaks per client pid)

    // 2) Restore the game to a normal top-level window (detach-without-close only). True
    //    embed: un-parent FIRST (style restore clears WS_CHILD), so destroying the host below
    //    cannot take the game with it.
    if (!closeGame && d->game && IsWindow(d->game) && d->embedded) {
        if (d->gameIsChild) {
            SetParent(d->game, nullptr);
            d->gameIsChild = false;
        }
        SetWindowLongPtrW(d->game, GWL_STYLE, d->gameStyle);
        SetWindowLongPtrW(d->game, GWL_EXSTYLE, d->gameExStyle);
        RECT& r = d->gameOrigRect;
        SetWindowPos(d->game, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    }

    d->embedded = false;
    UninstallPanelMouseHook(d);   // restore AppCore's panel WndProc before teardown
    if (d->panel) d->panel->set_listener(nullptr);
    if (d->overlay && d->overlay->view()) {
        d->overlay->view()->set_load_listener(nullptr);
        d->overlay->view()->set_view_listener(nullptr);
    }
    // Close the panel (a host child) BEFORE destroying the host: destroying a parent destroys its
    // children, which would invalidate Ultralight's window underneath.
    if (d->panel) d->panel->Close();
    if (d->host) {
        SetWindowLongPtrW(d->host, GWLP_USERDATA, 0);
        SetHostAppId(d->host, nullptr);   // clear the window property store before destroy
        DestroyWindow(d->host);
        d->host = nullptr;
    }
    // Release the heavy Ultralight objects NOW (listeners cleared + panel Closed above, so safe
    // on the main thread): the RefPtrs otherwise keep the Window + Overlay + CPU render surface +
    // WebKit heap alive. The Dock struct itself stays intentionally leaked (a few hundred bytes)
    // to avoid deleting it under a teardown/WndProc stack.
    d->overlay = nullptr;
    d->panel   = nullptr;
}

// ---- rail tooltip ----------------------------------------------------------
// A single shared top-most window painted over the game, to the LEFT of the hovered rail icon
// (the panel child can't draw its own tooltips out over the game beside it).
HWND         g_tipWnd = nullptr;
HFONT        g_tipFont = nullptr;
std::wstring g_tipText;

std::wstring Utf8ToW(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

LRESULT CALLBACK TipProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_PAINT) {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(11, 13, 18));
        FillRect(dc, &rc, bg); DeleteObject(bg);
        HBRUSH brd = CreateSolidBrush(RGB(45, 52, 71));
        FrameRect(dc, &rc, brd); DeleteObject(brd);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(238, 240, 245));
        HFONT old = (HFONT)SelectObject(dc, g_tipFont);
        RECT tr = rc; tr.left += 9;
        // DT_NOPREFIX: tip text is data, not a menu label -- without it GDI eats '&' as an
        // accelerator marker and underlines the next char ("Slayer & Reaper" -> "Slayer _Reaper").
        DrawTextW(dc, g_tipText.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
        SelectObject(dc, old);
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void EnsureTipWindow() {
    if (g_tipWnd) return;
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = TipProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"RuneToolsXTip";
        RegisterClassExW(&wc);
        reg = true;
    }
    g_tipWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                               L"RuneToolsXTip", L"", WS_POPUP, 0, 0, 10, 10,
                               nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    g_tipFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

}  // namespace

void Init(App* app, std::string client_html_path, bool uiDevWatch) {
    g_app = app;
    g_client_html_path = std::move(client_html_path);
    g_uiWatch = uiDevWatch;   // resolved in main.cpp (RTX_UI_DIR env var or rtx_ui_dev.txt marker)
}

void EnsureClient(std::uint32_t pid) {
    if (!g_app || !pid || g_docks.count(pid)) return;

    auto* d = new Dock();
    d->pid = pid;
    // The sidebar: a borderless Ultralight window reparented into the host.
    d->panel = Window::Create(g_app->main_monitor(), kPanelWidth, 600, false,
                              kWindowFlags_Borderless);
    if (!d->panel) { delete d; return; }
    d->panel->set_listener(d);
    d->overlay = Overlay::Create(d->panel, d->panel->width(), d->panel->height(), 0, 0);
    if (!d->overlay) { d->panel->set_listener(nullptr); delete d; return; }
    // device_scale is left to AppCore: SyncPanelDpi keeps the panel window's scale() equal to its
    // real current monitor, and Overlay::Resize drives the View's device_scale to match.
    d->overlay->view()->set_load_listener(d);
    d->overlay->view()->set_view_listener(d);   // console messages -> client log
    InstallPanelMouseHook(d);   // mouse-mapping diagnostic (passthrough; see PanelMouseProc)
    auto html = read_file(g_client_html_path);
    if (html.empty()) html = "<html><body style='background:#11151c'></body></html>";
    inject_panel_scripts(html, g_client_html_path);
    d->overlay->view()->LoadHTML(String(html.c_str()));
    if (g_uiWatch) { g_uiMtime = ui_dir_mtime(); g_uiPending = 0; }   // this load IS the current disk state
    if (HWND ph = static_cast<HWND>(d->panel->native_handle())) {
        // Dark erase brush so panel resizes never flash white; hidden until embedded.
        if (!g_darkBrush) g_darkBrush = CreateSolidBrush(RGB(11, 13, 18));
        SetClassLongPtrW(ph, GCLP_HBRBACKGROUND, reinterpret_cast<LONG_PTR>(g_darkBrush));
        ShowWindow(ph, SW_HIDE);
    }

    g_docks[pid] = d;
    rtx::launcher::companion::EnsureLoaded(pid);   // scene/var data + in-frame markers

    Embed(d);   // embeds now if the game window is ready; Tick retries otherwise.
}

void RemoveClient(std::uint32_t pid) {
    auto it = g_docks.find(pid);
    if (it == g_docks.end()) return;
    Dock* d = it->second;
    g_docks.erase(it);
    Detach(d);   // panel closed, game restored to a normal window (left running)
    // Drop every companion-side effect on this client so the module goes inert. No-op when the
    // companion was never loaded for this pid.
    for (int which = 0; which <= 4; ++which) rtx::reader::RenderToggle(pid, which, false);
    rtx::reader::VarsWatch(pid, false);
}

bool IsOpen(std::uint32_t pid) {
    return g_docks.count(pid) != 0;
}

void SetKeepFocused(std::uint32_t pid, bool on) {
    auto it = g_docks.find(pid);
    if (it == g_docks.end() || !it->second) return;
    Dock* d = it->second;
    d->keepFocused = on;
    d->keepFocusedApplied = false;                 // re-push to the companion (Tick retries)
    if (on) rtx::launcher::companion::EnsureLoaded(pid);   // it hosts the filter
}

void* GameWindowHandle(std::uint32_t pid) {
    HWND g = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_embedded_mu);
        auto it = g_embedded_games.find(pid);
        if (it != g_embedded_games.end()) g = it->second;
    }
    return (g && IsWindow(g)) ? g : nullptr;   // IsWindow is safe on a stale handle
}

void HideRailTip() {
    if (g_tipWnd) ShowWindow(g_tipWnd, SW_HIDE);
}

std::string ReadUiAsset(const std::string& name) { return ReadUiAssetImpl(name); }

void ShowRailTip(std::uint32_t pid, const std::string& text, int clientX, int clientY) {
    if (text.empty()) { HideRailTip(); return; }
    auto it = g_docks.find(pid);
    if (it == g_docks.end() || !it->second || !it->second->panel) return;
    HWND panel = static_cast<HWND>(it->second->panel->native_handle());
    if (!panel) return;
    EnsureTipWindow();
    if (!g_tipWnd) return;
    g_tipText = Utf8ToW(text);
    HDC dc = GetDC(g_tipWnd);
    HFONT old = (HFONT)SelectObject(dc, g_tipFont);
    SIZE sz{};
    GetTextExtentPoint32W(dc, g_tipText.c_str(), (int)g_tipText.size(), &sz);
    SelectObject(dc, old);
    ReleaseDC(g_tipWnd, dc);
    int w = sz.cx + 20, h = sz.cy + 10;
    POINT p{ clientX, clientY };
    ClientToScreen(panel, &p);                    // icon position -> screen
    SetWindowPos(g_tipWnd, HWND_TOPMOST, p.x - w - 8, p.y - h / 2, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);  // to the LEFT of the icon, over the game
    InvalidateRect(g_tipWnd, nullptr, TRUE);
}

void SetCollapsed(std::uint32_t pid, bool collapsed) {
    auto it = g_docks.find(pid);
    if (it == g_docks.end() || !it->second) return;
    Dock* d = it->second;
    d->collapsed = collapsed;
    if (!d->host) { d->panelW = collapsed ? kRailWidth : kPanelWidth; return; }
    RECT rc;
    if (!GetClientRect(d->host, &rc)) { Layout(d); return; }
    int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
    if (cw <= 0 || ch <= 0) { Layout(d); return; }
    // The slide is OUTWARD whenever the window can resize: the right edge moves and the game keeps
    // its exact size. Maximized there's no room to grow -- fall back to sliding over the game.
    bool outward = !IsZoomed(d->host);
    int gameW = cw - d->panelW;
    double sc = DockScale(d);
    int target;
    if (collapsed) {
        target = (int)(kRailWidth * sc + 0.5);
    } else if (outward) {
        // Pick the expanded width so the settle Layout (which re-derives the panel as PanelFull(client
        // width)) lands on EXACTLY this game width -- a pixel of drift would resize the client and
        // reflow its UI. cw' - PanelFull(cw') is nondecreasing, so walk up to the first match.
        int cwT = gameW + (int)(kPanelMin * sc + 0.5);
        while (cwT - PanelFull(cwT, sc) < gameW) ++cwT;
        target = PanelFull(cwT, sc);
    } else {
        target = PanelFull(cw, sc);
    }
    if (d->animating ? (d->animTargetW == target) : (d->panelW == target)) return;
    HWND ph = d->panel ? static_cast<HWND>(d->panel->native_handle()) : nullptr;
    // Panel stays at the slide's widest extent (content pre-rendered); AnimStep clips its
    // LEFT edge, which stays glued to the game.
    int wide = (target > d->panelW) ? target : d->panelW;
    if (ph) {
        SetWindowPos(ph, HWND_TOP, cw - d->panelW, 0, wide, ch, SWP_NOACTIVATE);
        HRGN rgn = (d->panelW >= wide) ? nullptr
                                       : CreateRectRgn(0, 0, d->panelW, ch);
        SetWindowRgn(ph, rgn, TRUE);
    }
    d->animOutward = outward;
    GetWindowRect(d->host, &d->animHostRect);
    if (outward) {
        // If the expanded window would overflow the work area, shift it left once up front.
        // Moving the window is fine -- the client reflows on RESIZE, not on move.
        HMONITOR mon = MonitorFromWindow(d->host, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{ sizeof(mi) };
        if (GetMonitorInfoW(mon, &mi)) {
            int outerW0 = d->animHostRect.right - d->animHostRect.left;
            int finalW = outerW0 + (target - d->panelW);
            int x = d->animHostRect.left;
            if (x + finalW > mi.rcWork.right) x = mi.rcWork.right - finalW;
            if (x < mi.rcWork.left) x = mi.rcWork.left;
            if (x != d->animHostRect.left) {
                OffsetRect(&d->animHostRect, x - d->animHostRect.left, 0);
                SetWindowPos(d->host, nullptr, x, d->animHostRect.top, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
    }
    d->animStartW = d->panelW;
    d->animTargetW = target;
    d->animStartMs = GetTickCount64();
    d->animating = true;
    SetTimer(d->host, kAnimTimerId, 16, nullptr);
}

void Tick() {
    if (g_docks.empty()) return;
    // Dev UI hot-reload: with RTX_UI_DIR active, re-splice and reload every open panel when the
    // ui dir's newest mtime settles on a new value. Panel JS state resets on reload.
    if (g_uiWatch) {
        ULONGLONG now = GetTickCount64();
        if (now - g_uiPollMs >= 1500) {
            g_uiPollMs = now;
            long long m = ui_dir_mtime();
            if (g_uiMtime == 0) g_uiMtime = m;
            else if (m != g_uiMtime) {
                if (m == g_uiPending) {
                    g_uiMtime = m; g_uiPending = 0;
                    auto html = read_file(g_client_html_path);
                    if (!html.empty()) {
                        inject_panel_scripts(html, g_client_html_path);
                        // Per-dock state carry-over: the page boots collapsed by design, so a bare reload of
                        // an expanded dock would force a collapse/re-expand cycle (host resizes that drift the
                        // split). Inject the dock's real collapsed state so dockCollapse no-ops.
                        for (auto& kv : g_docks) {
                            Dock* d = kv.second;
                            if (!d || !d->overlay) continue;
                            std::string h = html;
                            auto sp = h.find("<script>");
                            if (sp != std::string::npos)
                                h.insert(sp, std::string("<script>window.__rtxDevReload={collapsed:") +
                                             (d->collapsed ? "true" : "false") + "};</script>\n");
                            d->overlay->view()->LoadHTML(String(h.c_str()));
                        }
                        rtx::log::Launcher("ui hot-reload: " + std::to_string(g_docks.size()) + " panel(s) reloaded");
                    }
                } else g_uiPending = m;
            }
        }
    }
    auto processAlive = [](std::uint32_t pid) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
        if (!h) return false;
        DWORD code = 0;
        bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
        CloseHandle(h);
        return alive;
    };
    std::vector<std::uint32_t> dead;
    HWND fg = GetForegroundWindow();
    for (auto& kv : g_docks) {
        Dock* d = kv.second;
        if (!d) continue;
        if (!d->embedded) {                          // game wasn't ready at EnsureClient
            // A client that dies BEFORE embedding (loading crash) must still tear its dock
            // down -- otherwise the launcher waits forever on a pid that will never embed.
            if (!Embed(d) && !d->embedPending && !processAlive(kv.first))
                dead.push_back(kv.first);
            continue;
        }
        // Game process exited -> its window is gone; drop the client.
        if (!d->game || !IsWindow(d->game)) { dead.push_back(kv.first); continue; }
        // Self-heal the panel overlay SIZE: if a resize is missed (any path that doesn't route through
        // Layout), the overlay's pixel size lags the panel window's and maps mouse input off on the
        // VERTICAL axis. Cheap GetClientRect compare each tick; resizes only on a real drift.
        if (d->overlay && d->panel) {
            HWND ph = static_cast<HWND>(d->panel->native_handle());
            RECT pr{};
            if (ph && GetClientRect(ph, &pr)) {
                std::uint32_t pw = (std::uint32_t)(pr.right - pr.left), pht = (std::uint32_t)(pr.bottom - pr.top);
                if (pw && pht && (d->ovW != pw || d->ovH != pht)) {
                    d->overlay->Resize(pw, pht);
                    d->ovW = pw; d->ovH = pht;
                }
            }
        }
        // Push the keep-focused flag once the companion's channel exists (creating the render section so
        // the companion maps it once instead of polling OpenFileMapping per input). True embed REQUIRES
        // it (the game's detached queue never gets real focus again), so it overrides the user toggle.
        if (!d->keepFocusedApplied &&
            rtx::reader::RenderToggle(kv.first, 3, d->keepFocused || d->gameIsChild))
            d->keepFocusedApplied = true;
        if (d->gameIsChild && !d->embedFlagApplied &&
            rtx::reader::RenderToggle(kv.first, 4, true))
            d->embedFlagApplied = true;
        // Resolve the game's real keyboard window (companion-published once a frame has
        // presented); retry until valid, re-resolve if the client recreates it.
        if (d->gameIsChild && (!d->gameInput || !IsWindow(d->gameInput))) {
            if (std::uint64_t w = rtx::reader::RenderInputWindow(kv.first))
                d->gameInput = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(w));
        }
        // Re-assert the queue detach: Windows can silently re-attach the queues of related windows on
        // later hierarchy/activation events, and one re-attached handshake is one visible frame skip.
        // Detaching an unattached pair is a cheap harmless failure. Throttled + hung-gated:
        // AttachThreadInput synchronizes with the game's queue and would block on a stalled client.
        if (d->gameIsChild && d->gameThread &&
            GetTickCount64() - d->lastDetachMs > 1000 && !IsHungAppWindow(d->game)) {
            d->lastDetachMs = GetTickCount64();
            AttachThreadInput(d->gameThread, GetCurrentThreadId(), FALSE);
            AttachThreadInput(GetCurrentThreadId(), d->gameThread, FALSE);
        }
        // Z-group (glued only): when the game becomes the foreground window, lift the host
        // (+ panel) right behind it so the frame rises with it. Edge-triggered (lastGroupFg) so
        // no SetWindowPos is issued every frame. (Host-activated direction is WM_ACTIVATE.)
        if (d->host && !d->gameIsChild && fg != d->lastGroupFg) {
            if (fg == d->game)
                SetWindowPos(d->host, d->game, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            d->lastGroupFg = fg;
        }
    }
    for (auto pid : dead) {
        auto it = g_docks.find(pid);
        if (it == g_docks.end()) continue;
        Dock* d = it->second;
        g_docks.erase(it);
        rtx::log::Client(pid, "client process gone -> tearing down its window");
        Detach(d);   // game already gone; just tears down host/panel
    }
    if (!dead.empty()) QuitIfNoClients("client process exited");
}

void Shutdown() {
    std::vector<Dock*> ds;
    for (auto& kv : g_docks) ds.push_back(kv.second);
    g_docks.clear();
    // Tied lifetimes: launcher exit kills its clients NOW (restoring them to standalone would
    // leave each game window on screen, hung, until the kill-on-close job reaped it).
    for (Dock* d : ds) Detach(d, /*closeGame=*/true);
    if (g_tipWnd)  { DestroyWindow(g_tipWnd); g_tipWnd = nullptr; }
    if (g_tipFont) { DeleteObject(g_tipFont); g_tipFont = nullptr; }
}

}  // namespace rtx::launcher::dock
