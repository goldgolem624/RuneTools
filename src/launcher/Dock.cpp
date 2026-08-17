#include "Dock.h"
#include "Bridge.h"
#include "Companion.h"
#include "GameUi.h"             // in-game window UI layer (off-screen view + shares)
#include "Overlay.h"            // QuiesceMarkers (stop the in-frame layer before teardown)
#include "Markers.h"            // configurable mark/remove-tile keybinds
#include "../reader/Reader.h"   // RenderToggle (keep-focused / embed flag channel)
#include "../../companion/RenderShare.h"   // kMsgGameClicked (companion -> host click routing)
#include "../shared/Log.h"

#include <Ultralight/Ultralight.h>
#include <AppCore/AppCore.h>
#include <Windows.h>
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM
#include <dwmapi.h>     // DwmSetWindowAttribute (disable the host's activation transition)
#pragma comment(lib, "dwmapi.lib")
#include <shobjidl.h>   // SHGetPropertyStoreForWindow (per-window AppUserModelID)
#pragma comment(lib, "shell32.lib")

#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

// Unified-window model: one host window per client, with the game filling the ENTIRE host client
// area. The panel UI is no longer a docked sibling window: it is an off-screen Ultralight view
// composited INSIDE the game frame by the companion (see GameUi.h). Two modes, chosen by kTrueEmbed:
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

constexpr wchar_t kHostClass[] = L"RuneToolsXHost";

// TRUE EMBED vs GLUED (see the model comment at the top). false falls back to the glued pair.
constexpr bool kTrueEmbed = true;

// Posted to the host by the embed worker when the hierarchy surgery finished. The surgery's
// calls (style strip, SetParent) are synchronous sends into the GAME's window thread, which can
// park for MINUTES during world preload, so they must never run on the UI thread.
// wParam = the game HWND the surgery ran against.
constexpr UINT kMsgEmbedDone = 0x8000 + 0x53;   // WM_APP range, beside kMsgGameClicked

// Current-monitor DPI of a window (0 if unavailable). Defined further down; forward-declared so
// earlier code can use it. Unlike AppCore's cached Window::scale(), this is always live.
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

// Per-client host bookkeeping. The panel view itself lives in gameui (keyed by
// the same pid); this struct only owns the host window + embed state.
struct Dock {
    std::uint32_t pid = 0;
    HWND host = nullptr;         // the top-level host window (the unified frame)
    HWND game = nullptr;         // the game's own top-level window, glued over the host

    // Saved game-window state, to restore on detach.
    LONG_PTR gameStyle = 0, gameExStyle = 0;
    RECT     gameOrigRect{0, 0, 0, 0};

    bool      embedded = false;
    bool      embedPending = false;       // hierarchy surgery running on the embed worker
    bool      pendingDetach = false;      // Detach requested mid-surgery; FinishEmbed re-runs it
    bool      pendingDetachClose = false; // ...with closeGame
    bool      gameIsChild = false;        // TRUE EMBED took effect: game is a WS_CHILD of host,
                                          // input queues detached (vs glued top-level pair)
    DWORD     gameThread = 0;             // game window's thread id (for the queue re-detach)
    HWND      gameInput = nullptr;        // the render-target child the game takes keyboard on
                                          // (companion-published; the frame `game` ignores keys)
    bool      embedFlagApplied = false;   // companion told (RenderToggle 4); Tick retries
    bool      keyHeld[256] = {};  // keys the host has relayed down to the game (true embed). On
                                          // focus loss the physical key-up lands in the window tabbed to, so
                                          // the game never gets it; the matching key-ups are synthesized
    // Borderless fullscreen (host-side). The GAME cannot do this itself once embedded: it is a
    // WS_CHILD, so it is clipped to our client area no matter what its own Screen Sizing setting
    // says -- which is why the in-game Fullscreen button appears to do nothing. The host takes
    // the whole monitor instead and the game, which already fills the host client area, comes
    // with it. Style + placement are saved so leaving restores exactly what was there (including
    // a maximised window).
    bool             fullscreen = false;
    LONG_PTR         savedStyle = 0;
    WINDOWPLACEMENT  savedPlace{};
    bool      keepFocused = true;  // keep the client rendering when unfocused. Always on: true embed
                                          // REQUIRES it, the glued fallback wants it for smooth switching
    bool      keepFocusedApplied = false;  // push once even when off, so the companion's render section EXISTS
                                          // (it then maps it once instead of retrying OpenFileMapping per message)
    HWND      lastGroupFg = nullptr;      // last foreground z-grouped for
    ULONGLONG lastDetachMs = 0;           // last queue-detach re-assert (throttled in Tick)
    ULONGLONG lastDpiSyncMs = 0;          // last SyncUiDpi re-run (throttled in Tick)
    bool      inRaise = false;            // re-entrancy guard: RaiseGame's own SetWindowPos sends
                                          // WM_WINDOWPOSCHANGED back into HostProc
};

std::unordered_map<std::uint32_t, Dock*> g_docks;

// Cross-thread published handle: the overlay/marker render thread needs the game window, but
// g_docks is main-thread-only. Mirror just pid->game HWND under its own small lock.
std::mutex                              g_embedded_mu;
std::unordered_map<std::uint32_t, HWND> g_embedded_games;

// Cross-thread published companion client size (the game's own backbuffer size, from the
// FrameShare client_w/client_h feedback): ground truth for GameSpaceFactor's measured path.
std::mutex                                                g_gamesize_mu;
std::unordered_map<std::uint32_t, std::pair<int, int>>    g_game_sizes;

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

void Layout(Dock* d) {
    if (!d || !d->host) return;
    RECT rc;
    if (!GetClientRect(d->host, &rc)) return;
    int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
    if (cw <= 0 || ch <= 0) return;
    PositionGame(d, cw, ch);   // the game fills the entire host client area
}

void SyncUiDpi(Dock* d);   // fwd

// Borderless fullscreen on the monitor the host currently sits on. Deliberately NOT
// exclusive/mode-changing: no display-mode switch, so alt-tab stays instant, the overlay keeps
// compositing, and multi-client is unaffected.
//
// rcMonitor (not rcWork) is the target -- covering the taskbar is the point. HWND_TOP rather than
// HWND_TOPMOST: the shell drops the taskbar behind a foreground window that covers its monitor,
// and TOPMOST would also pin this client above every other window including our own other
// clients. SWP_FRAMECHANGED is required for the style change to be recalculated.
void SetFullscreen(Dock* d, bool on) {
    if (!d || !d->host || !IsWindow(d->host) || d->fullscreen == on) return;
    if (on) {
        d->savedPlace.length = sizeof(d->savedPlace);
        if (!GetWindowPlacement(d->host, &d->savedPlace)) d->savedPlace.length = 0;
        d->savedStyle = GetWindowLongPtrW(d->host, GWL_STYLE);
        HMONITOR mon = MonitorFromWindow(d->host, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{ sizeof(mi) };
        if (!GetMonitorInfoW(mon, &mi)) return;
        // Drop the whole frame (caption, thick border, min/max/sysmenu) and keep the child clip.
        LONG_PTR st = d->savedStyle;
        st &= ~(WS_OVERLAPPEDWINDOW);
        st |= WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN;
        SetWindowLongPtrW(d->host, GWL_STYLE, st);
        SetWindowPos(d->host, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOACTIVATE | SWP_FRAMECHANGED);
        d->fullscreen = true;
    } else {
        SetWindowLongPtrW(d->host, GWL_STYLE,
                          d->savedStyle ? d->savedStyle
                                        : (LONG_PTR)(WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE));
        // Frame first, then the saved placement -- restoring geometry before the style change is
        // recalculated leaves the client area short by the frame it is about to regain.
        SetWindowPos(d->host, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        if (d->savedPlace.length) SetWindowPlacement(d->host, &d->savedPlace);
        d->fullscreen = false;
    }
    Layout(d);        // refit the game child to the new client area
    SyncUiDpi(d);     // the new monitor/size may carry a different scale
}

// The unified window moved to a monitor with a different DPI (or the DPI changed): drive the
// in-game UI view's device scale so text renders crisp. The layer composites into the GAME's
// swapchain, so the scale follows the GAME window's PIXEL space -- identical to the host's except
// when the game runs DPI-virtualized: then its space is 96 DPI and DWM upscales the whole frame,
// our layer included, so scaling the layer by the monitor DPI too would double-scale it.
//
// host DPI x GameSpaceFactor IS that space's DPI: the factor is 1.0 when the game renders at
// physical resolution and gamePx/physicalPx when virtualized (measured from the companion's
// backbuffer feedback -- see GameSpaceFactor's pid overload; asking DpiForWindow(game) instead
// reads the host's context back once the game is our embedded child). Re-run from Tick: the
// measured factor only becomes available once the companion starts presenting.
void SyncUiDpi(Dock* d) {
    if (!d || !d->host) return;
    unsigned dpi = DpiForWindow(d->host);
    if (!dpi) return;
    double gsf = (d->game && IsWindow(d->game)) ? GameSpaceFactor(d->game, d->pid) : 1.0;
    if (gsf <= 0.0) gsf = 1.0;
    gameui::SetDeviceScale(d->pid, (dpi / 96.0) * gsf);
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
            // gameui's pump timer: its only job is waking the AppCore run loop so
            // CPU view paints + publishes keep pace during interaction/animation.
            return 0;
        case gameui::kMsgUiInput:
            // The companion enqueued UI events (posted by the waiter thread).
            if (d) gameui::DrainInput(d->pid);
            return 0;
        case WM_SIZE:
            if (d) {
                if (d->gameIsChild) {
                    // Real child: minimizes/restores with the host natively; just track size.
                    if (wp != SIZE_MINIMIZED) Layout(d);
                    return 0;
                }
                // The standalone game doesn't minimize with the host (no owner), so do it
                // here: hide it when the host minimizes, show + re-glue when it restores.
                if (wp == SIZE_MINIMIZED) {
                    if (d->game && IsWindow(d->game)) ShowWindow(d->game, SW_HIDE);
                } else {
                    if (d->game && IsWindow(d->game) && !IsWindowVisible(d->game))
                        ShowWindow(d->game, SW_SHOWNOACTIVATE);
                    Layout(d);
                    RaiseGame(d);   // maximize/resize can drop the host above the game -> re-raise
                }
            }
            return 0;
        case WM_MOVE:
            // Glued only: the game is its own top-level window, so it doesn't move with the host
            // like a child would -- reposition it to stay glued over the host's client area.
            if (d && !d->gameIsChild) Layout(d);
            return 0;
        case WM_EXITSIZEMOVE:
            // The user finished dragging/resizing the unified window: remember WHERE they put it, per
            // account, so it reopens there next launch (programmatic moves don't raise this).
            if (d && d->host) {
                RECT r; if (GetWindowRect(d->host, &r)) rtx::launcher::SaveWindowPos(d->pid, r.left, r.top);
            }
            break;
        case WM_DPICHANGED:
            // The unified window moved to a monitor with a different DPI. Let DefWindowProc resize the
            // host to the suggested rect first, then re-sync the in-game UI view's device scale.
            if (d && d->gameIsChild) {
                LRESULT r = DefWindowProcW(hwnd, msg, wp, lp);
                SyncUiDpi(d);
                return r;
            }
            break;
        case rtx::render::kMsgGameClicked:
            // Companion: the user pressed a mouse button in the game area (including over our
            // in-game UI). Take real keyboard focus on the host so key relay/capture works. Only
            // call SetFocus when the host doesn't ALREADY hold focus: a redundant transition
            // costs a repaint.
            if (d && d->gameIsChild) {
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
                    // Coming back (alt-tab/title click): the host owns real keyboard focus.
                    SetFocus(hwnd);
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
            if (d && d->gameIsChild) {
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
            if (d && d->gameIsChild) {
                if (msg == WM_SYSKEYDOWN && wp == VK_F4) break;   // keep Alt+F4 = close
                // A key the GAME believes held must get its release even while UI
                // keyboard capture is on (capture engaged between down and up --
                // e.g. W held to walk, then a text field clicked): otherwise the
                // key sticks down in the game forever. Forward the up, clear the
                // mirror, then still let the captured view see it below.
                bool releasedToGame = false;
                if ((msg == WM_KEYUP || msg == WM_SYSKEYUP) && wp < 256 && d->keyHeld[wp]) {
                    d->keyHeld[wp] = false;
                    HWND kbUp = (d->gameInput && IsWindow(d->gameInput)) ? d->gameInput : d->game;
                    if (kbUp && IsWindow(kbUp)) {
                        PostMessageW(kbUp, msg, wp, lp);
                        releasedToGame = true;
                    }
                }
                // In-game UI keyboard capture: while a UI text field holds focus, keys feed
                // the off-screen view directly (never relayed to the game, and keybinds
                // must not fire while typing).
                if (gameui::FireHostKey(d->pid, msg, (std::uintptr_t)wp, (std::intptr_t)lp))
                    return 0;
                if (releasedToGame) return 0;   // already delivered above
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
            mmi->ptMinTrackSize.x = 480;   // a usable minimum game viewport
            mmi->ptMinTrackSize.y = 320;
            // Allow growth to the full width of the current monitor's work area: the OS default
            // max track size is ~the primary monitor, so on a wider or secondary monitor
            // SetWindowPos is clamped otherwise.
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
    // A previous attempt's host survived (its game died mid-surgery): destroy it
    // before creating a fresh one, or the window leaks holding a stale Dock pointer.
    if (d->host) {
        SetWindowLongPtrW(d->host, GWLP_USERDATA, 0);
        SetHostAppId(d->host, nullptr);
        DestroyWindow(d->host);
        d->host = nullptr;
    }
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

    // Host client sized so the game keeps its current RENDER size. Size from the game's CLIENT
    // rect, NOT its window rect: the embed strips the game's frame, so sizing the host client to
    // the WINDOW rect inflates the now-borderless game's render area by that frame. gameOrigRect
    // is kept for restore + the host's screen pos.
    RECT gw = d->gameOrigRect;
    RECT gcr{};
    int gameW, gameH;
    if (GetClientRect(game, &gcr) && gcr.right > 0 && gcr.bottom > 0) {
        gameW = gcr.right - gcr.left; gameH = gcr.bottom - gcr.top;   // render area (frame excluded)
    } else {
        gameW = gw.right - gw.left;  gameH = gw.bottom - gw.top;      // fallback: window size
    }
    // The game fills the entire host client area (the panel UI is composited in-frame).
    int wantClient = gameW;
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
        " host=" + std::to_string(hostW) + "x" + std::to_string(hostH) +
        " at(" + std::to_string(x) + "," + std::to_string(y) + ")");
    SetWindowLongPtrW(d->host, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(d));
    SetHostAppId(d->host, kHostAppId);   // own taskbar group, BEFORE the host is ever shown
    WearGameIcon(d->host, game);
    // Kill the DWM activation transition on the host: focus switches to/from the unified window
    // then schedule no compositor fade (trims the switch cost).
    BOOL noTransitions = TRUE;
    DwmSetWindowAttribute(d->host, DWMWA_TRANSITIONS_FORCEDISABLED, &noTransitions, sizeof(noTransitions));

    // The game either becomes a real child (true embed) or stays its OWN top-level
    // glued over the host (fallback).
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
    gameui::Bind(d->pid, d->host);      // attach the in-game UI layer (shares + input waiter)
    SyncUiDpi(d);                       // drive the UI view's device scale to the host monitor
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
    // A Detach was requested mid-surgery: adopt the surgery's actual outcome so the
    // restore path sees the truth, then run the deferred teardown.
    if (d->pendingDetach) {
        bool cg = d->pendingDetachClose;
        d->pendingDetach = false;
        d->pendingDetachClose = false;
        if (d->game && IsWindow(d->game) && GetParent(d->game) == d->host) {
            d->gameThread = GetWindowThreadProcessId(d->game, nullptr);
            d->gameIsChild = true;
            d->embedded = true;
        }
        Detach(d, cg);
        return;
    }
    if (!d->game || !IsWindow(d->game)) return;   // client died mid-surgery; Tick cleans up
    // Confirm the child relationship actually holds at finalize (SetParent may have failed, or the
    // shell reverted it). Proceed regardless so the panel host stays usable; the log records it.
    if (GetParent(d->game) != d->host)
        rtx::log::Client(d->pid,
            "warning: game is not our child at finalize -- a separate game window may appear; "
            "reparent failed or was reverted by the shell");
    d->gameThread = GetWindowThreadProcessId(d->game, nullptr);
    d->gameIsChild = true;
    // keep-focused is REQUIRED here, not opt-in: the game's detached queue never gets real
    // focus again, so the client must be kept believing it has it. Tick pushes it.
    d->keepFocusedApplied = false;
    d->embedded = true;
    SetEmbeddedGame(d->pid, d->game);   // publish for the overlay/marker thread
    ShowWindow(d->host, SW_SHOW);
    Layout(d);                          // position the game over the host client area
    gameui::Bind(d->pid, d->host);      // attach the in-game UI layer (shares + input waiter)
    SyncUiDpi(d);                       // drive the UI view's device scale to the host monitor
    SetForegroundWindow(d->host);       // activate the unified window
    rtx::log::Client(d->pid, "client embedded into host window");
    // Diagnostic: post-embed sizes.
    {
        RECT hc{}, gc{};
        GetClientRect(d->host, &hc);
        if (d->game && IsWindow(d->game)) GetClientRect(d->game, &gc);
        rtx::log::Client(d->pid,
            "[dpi] post-embed hostClient=" + std::to_string(hc.right - hc.left) + "x" +
            std::to_string(hc.bottom - hc.top) +
            " gameClient=" + std::to_string(gc.right - gc.left) + "x" +
            std::to_string(gc.bottom - gc.top) +
            " hostDpi=" + std::to_string(DpiForWindow(d->host)) +
            " gameDpi=" + std::to_string(DpiForWindow(d->game)));
    }
}

void Detach(Dock* d, bool closeGame) {
    if (!d) return;

    // A hierarchy surgery is in flight on the embed worker: the game window's styles
    // and parent are mid-mutation and embedded/gameIsChild are not yet set, so a
    // teardown NOW would either destroy the game with the host (the worker's
    // SetParent lands after our restore) or leave a frameless zombie. Defer the real
    // teardown to FinishEmbed; a close request still terminates the client at once.
    if (d->embedPending) {
        d->pendingDetach = true;
        d->pendingDetachClose = d->pendingDetachClose || closeGame;
        if (d->host) ShowWindow(d->host, SW_HIDE);
        if (closeGame) {
            if (HANDLE hp = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)d->pid)) {
                TerminateProcess(hp, 0);
                CloseHandle(hp);
            }
            rtx::log::Client(d->pid, "client terminated (embed in flight; teardown deferred)");
        }
        return;
    }

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

    // 1) Stand the companion down COMPLETELY before the game is touched: the in-game UI layer
    //    quiesced (visible=0/active=0) so the present hook stops touching it, markers off, every
    //    render toggle off, and -- critically -- keep-focused OFF. Keep-focused swallows the
    //    game's focus-loss messages and fakes its foreground state; if still active while the game
    //    shuts down, RS3's close path waits on activation state it can never observe and hangs.
    gameui::Destroy(d->pid);
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
    if (d->host) {
        SetWindowLongPtrW(d->host, GWLP_USERDATA, 0);
        SetHostAppId(d->host, nullptr);   // clear the window property store before destroy
        DestroyWindow(d->host);
        d->host = nullptr;
    }
    // The Dock struct itself stays intentionally leaked (a few hundred bytes) to avoid
    // deleting it under a teardown/WndProc stack. (The heavy Ultralight view was released
    // by gameui::Destroy above.)
}

}  // namespace

double GameSpaceFactor(void* gameHwnd) {
    HWND game = reinterpret_cast<HWND>(gameHwnd);
    if (!game || !IsWindow(game)) return 1.0;
    unsigned gd = DpiForWindow(game);   // the game window's DPI context (96 when it is unaware)
    if (!gd) return 1.0;                // pre-1607 Windows: no per-window virtualization to bridge
    unsigned md = 0;
    using MonFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);   // shcore!GetDpiForMonitor (8.1+)
    static MonFn monFn = []() -> MonFn {
        HMODULE m = LoadLibraryW(L"shcore.dll");
        return m ? reinterpret_cast<MonFn>(GetProcAddress(m, "GetDpiForMonitor")) : nullptr;
    }();
    if (monFn) {
        UINT dx = 0, dy = 0;
        HMONITOR mon = MonitorFromWindow(game, MONITOR_DEFAULTTONEAREST);
        if (mon && SUCCEEDED(monFn(mon, 0 /* MDT_EFFECTIVE_DPI */, &dx, &dy)) && dx) md = dx;
    }
    if (!md) {   // fallback: the system DPI, reported truthfully to this DPI-aware process
        HDC dc = GetDC(nullptr);
        if (dc) { md = (unsigned)GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc); }
    }
    if (!md || md == gd) return 1.0;
    return (double)gd / (double)md;
}

double GameSpaceFactor(void* gameHwnd, std::uint32_t pid) {
    HWND game = reinterpret_cast<HWND>(gameHwnd);
    if (pid && game && IsWindow(game)) {
        int cw = 0, ch = 0;
        {
            std::lock_guard<std::mutex> lk(g_gamesize_mu);
            auto it = g_game_sizes.find(pid);
            if (it != g_game_sizes.end()) { cw = it->second.first; ch = it->second.second; }
        }
        RECT rc{};
        if (cw > 0 && ch > 0 && GetClientRect(game, &rc) && rc.right > 0 && rc.bottom > 0) {
            const double fx = (double)cw / (double)rc.right;
            const double fy = (double)ch / (double)rc.bottom;
            // DPI virtualization scales both axes by the SAME factor; a non-uniform or
            // out-of-range ratio is a torn measurement (companion lagging a live resize
            // by a present) -> fall through to the DPI inference for this call.
            if (fx > 0.2 && fx < 5.0 && fy > 0.2 && fy < 5.0 && std::fabs(fx - fy) < 0.02) {
                const double f = (fx + fy) * 0.5;
                return (std::fabs(f - 1.0) < 0.005) ? 1.0 : f;   // snap rounding jitter to exact 1.0
            }
        }
    }
    return GameSpaceFactor(gameHwnd);
}

void PublishGameClientSize(std::uint32_t pid, int w, int h) {
    std::lock_guard<std::mutex> lk(g_gamesize_mu);
    if (w > 0 && h > 0) g_game_sizes[pid] = { w, h };
    else                g_game_sizes.erase(pid);
}

void Init(App* app, std::string client_html_path, bool uiDevWatch) {
    g_app = app;
    g_client_html_path = std::move(client_html_path);
    g_uiWatch = uiDevWatch;   // resolved in main.cpp (RTX_UI_DIR env var or rtx_ui_dev.txt marker)
    gameui::Init(app);
}

std::string BuildClientHtml() {
    auto html = read_file(g_client_html_path);
    if (html.empty()) html = "<html><body style='background:transparent'></body></html>";
    inject_panel_scripts(html, g_client_html_path);
    return html;
}

void EnsureClient(std::uint32_t pid) {
    if (!g_app || !pid || g_docks.count(pid)) return;

    auto* d = new Dock();
    d->pid = pid;
    // The in-game UI layer: an off-screen transparent view loading the spliced client.html.
    // Prepared now so the page is parsed/booted while the embed completes; it binds to the
    // host (shares + input) once the host window exists.
    double scale = 1.0;
    if (HWND game = FindGameWindow(pid)) {
        unsigned dpi = DpiForWindow(game);
        if (dpi) scale = dpi / 96.0;
    }
    gameui::Prepare(pid, BuildClientHtml(), scale);
    if (g_uiWatch) { g_uiMtime = ui_dir_mtime(); g_uiPending = 0; }   // this load IS the current disk state

    g_docks[pid] = d;
    rtx::launcher::companion::EnsureLoaded(pid);   // scene/var data + in-frame markers + UI compositing

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

void SetHostFullscreen(std::uint32_t pid, bool on) {
    auto it = g_docks.find(pid);
    if (it == g_docks.end() || !it->second) return;
    SetFullscreen(it->second, on);
}

bool IsHostFullscreen(std::uint32_t pid) {
    auto it = g_docks.find(pid);
    return (it != g_docks.end() && it->second) ? it->second->fullscreen : false;
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

std::string ReadUiAsset(const std::string& name) { return ReadUiAssetImpl(name); }

void Tick() {
    if (g_docks.empty()) return;
    gameui::Tick();   // resize handshake + dirty-surface publish + pump pacing
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
                    auto html = BuildClientHtml();
                    std::string h = html;
                    auto sp = h.find("<script>");
                    if (sp != std::string::npos)
                        h.insert(sp, "<script>window.__rtxDevReload=1;</script>\n");
                    for (auto& kv : g_docks)
                        gameui::ReloadHtml(kv.first, h);
                    rtx::log::Launcher("ui hot-reload: " + std::to_string(g_docks.size()) + " ui layer(s) reloaded");
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
        // Re-drive the UI layer's device scale: the MEASURED game-space factor only exists
        // once the companion presents (and can change when the game toggles its own
        // resolution handling), so the embed-time SyncUiDpi value may be provisional.
        // SetDeviceScale dedups, so a settled scale costs nothing.
        {
            ULONGLONG nowMs = GetTickCount64();
            if (nowMs - d->lastDpiSyncMs >= 500) { d->lastDpiSyncMs = nowMs; SyncUiDpi(d); }
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
}

}  // namespace rtx::launcher::dock
