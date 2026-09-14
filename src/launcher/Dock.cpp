#include "Dock.h"
#include "Bridge.h"
#include "Companion.h"
#include "GameUi.h"             // in-game window UI layer (off-screen view + shares)
#include "Overlay.h"            // QuiesceMarkers (stop the in-frame layer before teardown)
#include "WikiBrowser.h"        // in-client wiki pane (docked child window, wiki-locked)
#include "Markers.h"            // configurable mark/remove-tile keybinds
#include "../reader/Reader.h"   // RenderToggle (keep-focused / embed flag channel)
#include "../../companion/RenderShare.h"   // kMsgGameClicked (companion -> host click routing)
#include "../shared/Log.h"
#include "../cache/CacheReader.h"

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

// Unified-window model: one host window per client; kTrueEmbed=1 makes the game a WS_CHILD, and cross-process SetParent attaches both threads' input queues, so it is severed right after. Main thread only.

namespace rtx::launcher { void HudClose(std::uint32_t pid); }   // Bridge.cpp: releases a client's HUD section on teardown

namespace rtx::launcher::dock {

using namespace ultralight;

namespace {

App*        g_app = nullptr;
std::string g_client_html_path;
HBRUSH      g_darkBrush = nullptr;   // dark erase brush (#0b0d12)

bool      g_uiWatch = false;
long long g_uiMtime = 0, g_uiPending = 0;
ULONGLONG g_uiPollMs = 0;

constexpr wchar_t kHostClass[] = L"RuneToolsXHost";

constexpr bool kTrueEmbed = true;

// Posted by the embed worker when the hierarchy surgery finished; wParam = game HWND.
constexpr UINT kMsgEmbedDone = 0x8000 + 0x53;   // WM_APP range, beside kMsgGameClicked

unsigned DpiForWindow(HWND h);

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

void inject_panel_scripts(std::string& html, const std::string& html_path) {
    static const char* kCoreFiles[] = {
        "core/rtx-shim.js",       // window.onerror first, then the localStorage shim
        "core/rtx-prefs.js",      // durable prefs: prefGet/prefSet/prefsInit
        "core/rtx-ui.js",         // rtxUi preferences blob, uiApply, number format, bar chips
        "core/rtx-registry.js",   // TABS, RTX + registerTab, CAT_META, TAB_GROUPS, XP tables
        "core/rtx-skillbars.js",  // in-game Skills XP bars + sprite icons
        "core/rtx-bridge.js",     // pane roots + $, bridge/bridgeJson, myPid, paneEmpty
        "core/rtx-icons.js",      // item icon/info caches, tooltips, attachIcon/attachInfo
        "core/rtx-storage.js",    // bank / metal bank / guild shop / bait box / workbench
        "core/rtx-player.js",     // Player State data, skill goals, metronome + XP overlay cfg
        "core/rtx-wm.js",         // window manager
        "core/rtx-notify.js",     // toasts / uiNotify
        "core/rtx-console.js",    // rtxConsole log bus (page console capture, plugin lines, launcher tail)
        "core/rtx-settings.js",   // Preferences page
        "core/rtx-input.js",      // input rects + keyboard capture, wiki palette
        "core/rtx-layout.js",     // layout persistence, openTab, tabEntryKicks
        "core/rtx-menubar.js",    // menu bar, wiki pane, fullscreen, tab search
        "core/rtx-pane.js",       // renderPane / renderHeader
        "core/rtx-plugin-api.js", // plugin SDK method table, clamps, rate limiter, SDK shim
        "core/rtx-plugin-hud.js", // host-rendered plugin ability HUD strip
        "core/rtx-plugins.js",    // plugin SDK host broker: mounts, grants, sandbox, windows
        "core/rtx-plugin-lua.js", // Lua plugin host: mount, widget tree renderer, __rtxLuaCall shim over PLUGIN_API
        "core/rtx-plugin-market.js", // in-client marketplace (browse / install)
        "core/rtx-data.js",       // rtxData: panels' data path over PLUGIN_API (per-tick coalescer)
    };
    static const char* kBootFiles[] = {
        "core/rtx-boot.js",       // refresh loop + attachBridge
    };
    static const char* kFiles[] = {
        // quest_guides.js (~1.3MB) is not spliced; it is loaded via bridge().uiAsset() on demand.
        "rtx_vars.js",          // shared var-id table (VB/VP), before every panel
        "panel_stopwatch.js",
        "panel_console.js",        // Developer: the client-wide console (rtxConsole)
        "panel_notes.js",
        "panel_counter.js",
        "panel_auras.js",          // HUD window: watched buff/debuff icons + time sweep
        "panel_metronome.js",      // HUD window: tick dial (was drawn by the companion)
        "panel_familiar.js",
        "panel_dungeoneering.js",  // Daemonheim floor status + explored map
        "panel_archresearch.js",   // Archaeology Field Study / Report status
        "panel_reputation.js",
        "panel_wardrobe.js",     // cosmetic override ownership (dbtable 163)
        "panel_bossinfo.js",
        "panel_groupbank.js",
        "panel_pets.js",
        "panel_bosses.js",
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
        "panel_geprices.js",   // GE Prices (server-relayed real-time prices)
        "panel_mysteries.js",  // Archaeology mysteries (requirements + focused mystery)
        "panel_chatlog.js",
        "panel_combatlog.js",  // Combat Log (hitsplat stream from the reader's combat log)
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
        "panel_rendering.js",
        "panel_storage.js",
        "panel_containers.js",
        "panel_pof.js",
        "panel_resdungeons.js",
        "panel_fairyrings.js",
        "panel_abilitytips.js",   // AB_TIPS: per-ability tooltip bullets extracted from the CS2 builders
        "panel_abilities.js",
        "panel_alerts.js",
        "panel_inventory.js",
        "panel_quests.js",
        "panel_questguides.js",
        "panel_xptracker.js",
        "panel_xpmeter.js",        // HUD XP readout; must follow panel_xptracker.js (reads its state)
        "panel_varswatcher.js",
        "panel_netprobe.js",   // Server Packets (server->client protocol + live inbound feed)
        "panel_cs2.js",        // CS2 Scripts browser (extraction + search/view)
        "panel_skillbonus.js", // Bonus XP for the Skills tab
        "panel_bank.js",
        "panel_screenshot.js", // Screenshot capture (user keybind + game-window PNG)
        "panel_scarabs.js",    // Corrupted Scarabs community world tracker
        "panel_obelisks.js",   // Soul Obelisk community world+district tracker
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
        "panel_events.js",     // Event channel stream + tick meter (Developer; the rtxEvents instrument)
        "panel_menuswap.js",   // Right-click menu inspector + reorder (Developer)
    };
    auto slash = html_path.find_last_of("\\/");
    std::string dir = (slash == std::string::npos) ? std::string() : html_path.substr(0, slash + 1);
    static const char kCssMarker[] = "<!-- rtx:css -->";
    auto cpos = html.find(kCssMarker);
    if (cpos != std::string::npos) {
        std::string css = read_file(dir + "core/rtx.css");
        html.replace(cpos, sizeof(kCssMarker) - 1, "<style>\n" + css + "\n</style>");
    }
    auto pos = html.find("<script>");
    if (pos == std::string::npos) return;
    std::string blob;
    auto splice = [&](const char* f) {
        std::string js = read_file(dir + f);
        if (js.empty()) { rtx::log::Launcher(std::string("ui: missing or empty ") + f); return; }
        blob += "<script>\n" + js + "\n</script>\n";
    };
    for (const char* f : kCoreFiles) splice(f);
    blob += "<script>window.QUEST_GUIDES = window.QUEST_GUIDES || {};</script>\n";
    for (const char* f : kFiles) splice(f);
    for (const char* f : kBootFiles) splice(f);
    html.insert(pos, blob);
}

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
    for (const fs::path& d : { html.parent_path(), html.parent_path() / "core" }) {
        fs::directory_iterator it(d, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            auto ext = it->path().extension();
            if (ext == L".js" || ext == L".css") acc(it->path());
        }
        ec.clear();
    }
    return m;
}

struct Dock {
    std::uint32_t pid = 0;
    double uiScaleMul = 1.0;     // Preferences "UI scale": multiplies the DPI-derived view scale
    HWND host = nullptr;         // the top-level host window (the unified frame)
    HWND game = nullptr;         // the game's own top-level window, glued over the host

    LONG_PTR gameStyle = 0, gameExStyle = 0;
    RECT     gameOrigRect{0, 0, 0, 0};

    bool      embedded = false;
    bool      embedPending = false;       // hierarchy surgery running on the embed worker
    bool      pendingDetach = false;      // Detach requested mid-surgery; FinishEmbed re-runs it
    bool      pendingDetachClose = false; // ...with closeGame
    bool      gameIsChild = false;        // true embed took effect: game is a WS_CHILD, queues detached
    DWORD     gameThread = 0;             // game window's thread id (for the queue re-detach)
    HWND      gameInput = nullptr;        // companion-published child the game takes keyboard on
    bool      embedFlagApplied = false;   // companion told (RenderToggle 4); Tick retries
    bool      keyHeld[256] = {};  // keys relayed down to the game; released synthetically on focus loss
    bool             fullscreen = false;
    LONG_PTR         savedStyle = 0;
    WINDOWPLACEMENT  savedPlace{};
    bool      keepFocused = true;  // always on: true embed requires it
    bool      keepFocusedApplied = false;  // push once even when off so the companion's render section exists
    HWND      lastGroupFg = nullptr;      // last foreground z-grouped for
    ULONGLONG lastDetachMs = 0;           // last queue-detach re-assert (throttled in Tick)
    ULONGLONG lastDpiSyncMs = 0;          // last SyncUiDpi re-run (throttled in Tick)
    bool      inRaise = false;            // re-entrancy guard (RaiseGame's SetWindowPos re-enters HostProc)
};

std::unordered_map<std::uint32_t, Dock*> g_docks;

// pid -> game HWND mirror for the render thread (g_docks is main-thread-only); own lock.
std::mutex                              g_embedded_mu;
std::unordered_map<std::uint32_t, HWND> g_embedded_games;

std::mutex                                                g_gamesize_mu;
std::unordered_map<std::uint32_t, std::pair<int, int>>    g_game_sizes;

void SetEmbeddedGame(std::uint32_t pid, HWND game) {   // main thread only
    std::lock_guard<std::mutex> lk(g_embedded_mu);
    if (game) g_embedded_games[pid] = game;
    else      g_embedded_games.erase(pid);
}

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

void PositionGame(Dock* d, int w, int h) {
    if (!d || !d->host || !d->game || !IsWindow(d->game)) return;
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    // SWP_ASYNCWINDOWPOS: the game's thread may not pump during loading.
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

void SetFullscreen(Dock* d, bool on) {
    if (!d || !d->host || !IsWindow(d->host) || d->fullscreen == on) return;
    if (on) {
        d->savedPlace.length = sizeof(d->savedPlace);
        if (!GetWindowPlacement(d->host, &d->savedPlace)) d->savedPlace.length = 0;
        d->savedStyle = GetWindowLongPtrW(d->host, GWL_STYLE);
        HMONITOR mon = MonitorFromWindow(d->host, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{ sizeof(mi) };
        if (!GetMonitorInfoW(mon, &mi)) return;
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
        SetWindowPos(d->host, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        if (d->savedPlace.length) SetWindowPlacement(d->host, &d->savedPlace);
        d->fullscreen = false;
    }
    Layout(d);        // refit the game child to the new client area
    SyncUiDpi(d);     // the new monitor/size may carry a different scale
}

void SyncUiDpi(Dock* d) {
    if (!d || !d->host) return;
    unsigned dpi = DpiForWindow(d->host);
    if (!dpi) return;
    double gsf = (d->game && IsWindow(d->game)) ? GameSpaceFactor(d->game, d->pid) : 1.0;
    if (gsf <= 0.0) gsf = 1.0;
    double mul = (d->uiScaleMul > 0.25 && d->uiScaleMul < 4.0) ? d->uiScaleMul : 1.0;
    gameui::SetDeviceScale(d->pid, (dpi / 96.0) * gsf * mul);
}

void Detach(Dock* d, bool closeGame = false);   // fwd
void FinishEmbed(Dock* d);                      // fwd (kMsgEmbedDone handler)

void QuitIfNoClients(const char* why) {
    (void)why;   // intentionally a no-op: the launcher persists past the last client
}

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

LRESULT CALLBACK HostProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Dock* d = reinterpret_cast<Dock*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_TIMER:
            return 0;
        case gameui::kMsgUiInput:
            if (d) gameui::DrainInput(d->pid);
            return 0;
        case WM_SIZE:
            if (d) {
                if (d->gameIsChild) {
                    if (wp != SIZE_MINIMIZED) Layout(d);
                    return 0;
                }
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
            if (d && !d->gameIsChild) Layout(d);
            return 0;
        case WM_EXITSIZEMOVE:
            if (d && d->host) {
                RECT r; if (GetWindowRect(d->host, &r)) rtx::launcher::SaveWindowPos(d->pid, r.left, r.top);
            }
            break;
        case WM_DPICHANGED:
            if (d && !d->fullscreen) {
                const RECT* pr = reinterpret_cast<const RECT*>(lp);
                if (pr)
                    SetWindowPos(hwnd, nullptr, pr->left, pr->top,
                                 pr->right - pr->left, pr->bottom - pr->top,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
                Layout(d);        // WM_SIZE does this too, but a same-size suggestion sends none
                SyncUiDpi(d);
                return 0;
            }
            if (d) { SyncUiDpi(d); return 0; }   // fullscreen: monitor-sized; scale only
            break;
        case rtx::render::kMsgGameClicked:
            if (d && d->gameIsChild) {
                if (GetFocus() != hwnd) SetFocus(hwnd);
            }
            return 0;
        case kMsgEmbedDone:
            if (d) FinishEmbed(d);
            return 0;
        case WM_NCACTIVATE:
            if (d && d->gameIsChild)
                return DefWindowProcW(hwnd, WM_NCACTIVATE, TRUE, lp);
            break;
        case WM_ACTIVATE:
            if (d && LOWORD(wp) != WA_INACTIVE) {
                if (d->gameIsChild) {
                    SetFocus(hwnd);
                } else {
                    RaiseGame(d);
                }
            } else if (d) {
                ReleaseHeldKeysToGame(d);
            }
            return 0;
        case WM_ACTIVATEAPP:
            if (d && wp == FALSE) ReleaseHeldKeysToGame(d);
            break;
        case WM_INPUTLANGCHANGE:
            if (d && d->gameIsChild) {
                HWND kbl = (d->gameInput && IsWindow(d->gameInput)) ? d->gameInput : d->game;
                if (kbl && IsWindow(kbl)) PostMessageW(kbl, WM_INPUTLANGCHANGEREQUEST, 0, lp);
            }
            break;
        case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR: case WM_DEADCHAR:
        case WM_SYSKEYDOWN: case WM_SYSKEYUP: case WM_SYSCHAR:
            if (d && d->gameIsChild) {
                if (msg == WM_SYSKEYDOWN && wp == VK_F4) break;   // keep Alt+F4 = close
                bool releasedToGame = false;
                if ((msg == WM_KEYUP || msg == WM_SYSKEYUP) && wp < 256 && d->keyHeld[wp]) {
                    d->keyHeld[wp] = false;
                    HWND kbUp = (d->gameInput && IsWindow(d->gameInput)) ? d->gameInput : d->game;
                    if (kbUp && IsWindow(kbUp)) {
                        PostMessageW(kbUp, msg, wp, lp);
                        releasedToGame = true;
                    }
                }
                if (gameui::FireHostKey(d->pid, msg, (std::uintptr_t)wp, (std::intptr_t)lp))
                    return 0;
                if (releasedToGame) return 0;   // already delivered above
                if (msg == WM_KEYDOWN && !(lp & 0x40000000) && d->pid) {
                    // Screenshot keybind: consume the key on match. 0 = unbound.
                    int ssVk = rtx::launcher::ScreenshotVk();
                    if (ssVk && (int)wp == ssVk) {
                        rtx::launcher::CaptureScreenshotForPid(d->pid);
                        return 0;
                    }
                    int wkVk = rtx::launcher::wiki::KeybindVk();
                    if (wkVk && (int)wp == wkVk) {
                        gameui::OpenWikiPalette(d->pid);
                        return 0;
                    }
                    int hpVk = rtx::launcher::HidePanelsVk();
                    if (hpVk && (int)wp == hpVk) {
                        gameui::TogglePanels(d->pid);
                        return 0;
                    }
                    auto kb2 = rtx::markers::GetKeybinds();
                    if (kb2.markVk && (int)wp == kb2.markVk) {
                        if (rtx::markers::MarkAtCursor(d->pid, 1)) { rtx::overlay::EnableMarkers(d->pid); return 0; }
                    } else if (kb2.removeVk && (int)wp == kb2.removeVk) {
                        if (rtx::markers::MarkAtCursor(d->pid, -1)) return 0;
                    }
                }
                HWND kb = (d->gameInput && IsWindow(d->gameInput)) ? d->gameInput : d->game;
                if (kb && IsWindow(kb)) {
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
            if (d && d->embedded && !d->inRaise) {
                auto* wpos = reinterpret_cast<WINDOWPOS*>(lp);
                if (!(wpos->flags & SWP_NOZORDER)) RaiseGame(d);
            }
            break;
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = 480;   // a usable minimum game viewport
            mmi->ptMinTrackSize.y = 320;
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
    // No CS_HREDRAW/CS_VREDRAW: a resize must not erase the whole host client (dark flash over the game).
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

// Dynamically resolved: GetDpiForWindow is Win10 1607+, the awareness-context pair 1607/1803+.
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

void MakeIndependentTopLevel(HWND game) {
    LONG_PTR st = GetWindowLongPtrW(game, GWL_STYLE);
    st &= ~(WS_CHILD | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX |
            WS_MAXIMIZEBOX | WS_SYSMENU | WS_BORDER | WS_DLGFRAME);
    st |= WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS;
    SetWindowLongPtrW(game, GWL_STYLE, st);
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

void EmbedSurgery(HWND game, HWND host, DWORD hostThread) {
    LONG_PTR st = GetWindowLongPtrW(game, GWL_STYLE);
    st &= ~(WS_POPUP | WS_OVERLAPPED | WS_CAPTION | WS_THICKFRAME |
            WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_BORDER | WS_DLGFRAME);
    st |= WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS;
    SetWindowLongPtrW(game, GWL_STYLE, st);
    ShowWindow(game, SW_HIDE);
    LONG_PTR ex = GetWindowLongPtrW(game, GWL_EXSTYLE);
    SetWindowLongPtrW(game, GWL_EXSTYLE,
                      (ex & ~static_cast<LONG_PTR>(WS_EX_APPWINDOW)) | WS_EX_TOOLWINDOW);
    SetLastError(0);
    SetParent(game, host);
    DWORD spErr = GetLastError();
    bool reparented = (GetParent(game) == host);   // did the child relationship actually take?
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

// id=nullptr clears the store. PROPERTYKEY spelled out to avoid propkey.h.
const PROPERTYKEY kPkeyAppUserModelId =
    { {0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5 };
constexpr wchar_t kHostAppId[] = L"RuneTools.RuneScape";

void SetHostAppId(HWND host, const wchar_t* id) {
    // Scope a COM apartment; S_FALSE/RPC_E_CHANGED_MODE both mean COM is already up.
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

bool Embed(Dock* d) {
    if (d->embedded) return true;
    if (d->embedPending) return false;   // surgery in flight; kMsgEmbedDone finalizes
    if (d->host) {
        SetWindowLongPtrW(d->host, GWLP_USERDATA, 0);
        SetHostAppId(d->host, nullptr);
        DestroyWindow(d->host);
        d->host = nullptr;
    }
    HWND game = FindGameWindow(d->pid);
    if (!game) return false;
    // Never embed a client that isn't pumping: SetParent is a synchronous send into its thread. Tick retries.
    if (IsHungAppWindow(game)) return false;
    DWORD_PTR probe = 0;
    if (!SendMessageTimeoutW(game, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 250, &probe))
        return false;

    d->game = game;
    d->gameStyle = GetWindowLongPtrW(game, GWL_STYLE);
    d->gameExStyle = GetWindowLongPtrW(game, GWL_EXSTYLE);
    GetWindowRect(game, &d->gameOrigRect);

    RECT gw = d->gameOrigRect;
    RECT gcr{};
    int gameW, gameH;
    if (GetClientRect(game, &gcr) && gcr.right > 0 && gcr.bottom > 0) {
        gameW = gcr.right - gcr.left; gameH = gcr.bottom - gcr.top;   // render area (frame excluded)
    } else {
        gameW = gw.right - gw.left;  gameH = gw.bottom - gw.top;      // fallback: window size
    }
    int wantClient = gameW;
    RECT want{0, 0, wantClient, gameH};
    AdjustWindowRectEx(&want, WS_OVERLAPPEDWINDOW, FALSE, 0);
    int hostW = want.right - want.left, hostH = want.bottom - want.top;

    int x = gw.left, y = gw.top;
    { int sx, sy; if (rtx::launcher::LoadWindowPos(d->pid, sx, sy)) { x = sx; y = sy; } }

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
    d->host = CreateWindowExW(WS_EX_APPWINDOW, kHostClass, L"RuneScape",
                              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                              x, y, hostW, hostH,
                              nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!d->host) return false;

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
    BOOL noTransitions = TRUE;
    DwmSetWindowAttribute(d->host, DWMWA_TRANSITIONS_FORCEDISABLED, &noTransitions, sizeof(noTransitions));

    if (kTrueEmbed) {
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
    RaiseGame(d);                       // glued: lift the game above the host, last so the game shows on open
    rtx::log::Client(d->pid, "client glued to host window");
    return true;
}

void FinishEmbed(Dock* d) {
    if (!d || !d->host || d->embedded) return;
    d->embedPending = false;
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
    if (GetParent(d->game) != d->host)
        rtx::log::Client(d->pid,
            "warning: game is not our child at finalize -- a separate game window may appear; "
            "reparent failed or was reverted by the shell");
    d->gameThread = GetWindowThreadProcessId(d->game, nullptr);
    d->gameIsChild = true;
    d->keepFocusedApplied = false;
    d->embedded = true;
    SetEmbeddedGame(d->pid, d->game);   // publish for the overlay/marker thread
    ShowWindow(d->host, SW_SHOW);
    Layout(d);                          // position the game over the host client area
    gameui::Bind(d->pid, d->host);      // attach the in-game UI layer (shares + input waiter)
    SyncUiDpi(d);                       // drive the UI view's device scale to the host monitor
    SetForegroundWindow(d->host);       // activate the unified window
    rtx::log::Client(d->pid, "client embedded into host window");
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

    if (closeGame) {
        if (d->host) ShowWindow(d->host, SW_HIDE);   // host window, own thread: instant
        if (HANDLE hp = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)d->pid)) {
            TerminateProcess(hp, 0);
            CloseHandle(hp);
        }
        rtx::log::Client(d->pid, "client terminated with its window");
    }

    rtx::launcher::wiki::Close(d->pid);
    gameui::Destroy(d->pid);
    rtx::overlay::QuiesceMarkers(d->pid);
    SetEmbeddedGame(d->pid, nullptr);
    for (int which = 0; which <= 4; ++which) rtx::reader::RenderToggle(d->pid, which, false);
    rtx::reader::VarsWatch(d->pid, false);
    rtx::launcher::HudClose(d->pid);   // release the HUD section mapping (else one handle + view leaks per client pid)

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
    // The Dock struct is intentionally leaked to avoid deleting it under a WndProc stack.
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
    rtx::launcher::wiki::Init(app);
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

void SetUiScaleMultiplier(std::uint32_t pid, double mul) {
    auto it = g_docks.find(pid);
    if (it == g_docks.end() || !it->second) return;
    if (!(mul > 0.25 && mul < 4.0)) mul = 1.0;
    it->second->uiScaleMul = mul;
    SyncUiDpi(it->second);
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

bool GameFocused(std::uint32_t pid) {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    HWND root = GetAncestor(fg, GA_ROOT);
    auto it = g_docks.find(pid);
    if (it != g_docks.end() && it->second) {
        Dock* d = it->second;
        if (d->host && (fg == d->host || root == d->host)) return true;
        if (d->game && IsWindow(d->game) && (fg == d->game || root == d->game)) return true;
        if (d->gameInput && IsWindow(d->gameInput) && fg == d->gameInput) return true;
    }
    DWORD fgPid = 0;
    GetWindowThreadProcessId(fg, &fgPid);
    if (fgPid == pid) return true;
    if (it != g_docks.end() && fgPid == GetCurrentProcessId()) return true;
    return false;
}

void Tick() {
    {   // Game cache updated under us: rebuild the cache view, then reload the in-game UI layers so
        // panels drop what they derived from the old data. Checked every few seconds; the check
        // itself is throttled and never waits on the cache lock.
        static ULONGLONG s_cacheCheckMs = 0;
        static std::uint64_t s_cacheGen = 0;
        ULONGLONG now = GetTickCount64();
        if (now - s_cacheCheckMs >= 3000) {
            s_cacheCheckMs = now;
            if (rtx::cache::CheckCacheUpdate())
                rtx::log::Launcher("cache: the game updated its cache while running; cache view rebuilt (generation " +
                                   std::to_string(rtx::cache::CacheGeneration()) + ")");
            const std::uint64_t gen = rtx::cache::CacheGeneration();
            if (s_cacheGen == 0) s_cacheGen = gen;
            else if (gen != s_cacheGen) {
                s_cacheGen = gen;
                if (!g_docks.empty()) {
                    std::string h = BuildClientHtml();
                    auto sp = h.find("<script>");
                    if (sp != std::string::npos) h.insert(sp, "<script>window.__rtxDevReload=1;</script>\n");
                    for (auto& kv : g_docks) gameui::ReloadHtml(kv.first, h);
                    rtx::log::Launcher("cache: " + std::to_string(g_docks.size()) + " ui layer(s) reloaded for the new cache");
                }
            }
        }
    }
    if (g_docks.empty()) return;
    gameui::Tick();   // resize handshake + dirty-surface publish + pump pacing
    rtx::launcher::wiki::Tick();   // wiki pane follows the host; reaps closed/dead windows
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
            if (!Embed(d) && !d->embedPending && !processAlive(kv.first))
                dead.push_back(kv.first);
            continue;
        }
        if (!d->game || !IsWindow(d->game)) { dead.push_back(kv.first); continue; }
        {
            ULONGLONG nowMs = GetTickCount64();
            if (nowMs - d->lastDpiSyncMs >= 500) { d->lastDpiSyncMs = nowMs; SyncUiDpi(d); }
        }
        if (!d->keepFocusedApplied &&
            rtx::reader::RenderToggle(kv.first, 3, d->keepFocused || d->gameIsChild))
            d->keepFocusedApplied = true;
        if (d->gameIsChild && !d->embedFlagApplied &&
            rtx::reader::RenderToggle(kv.first, 4, true))
            d->embedFlagApplied = true;
        if (d->gameIsChild && (!d->gameInput || !IsWindow(d->gameInput))) {
            if (std::uint64_t w = rtx::reader::RenderInputWindow(kv.first))
                d->gameInput = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(w));
        }
        if (d->gameIsChild && d->gameThread &&
            GetTickCount64() - d->lastDetachMs > 1000 && !IsHungAppWindow(d->game)) {
            d->lastDetachMs = GetTickCount64();
            AttachThreadInput(d->gameThread, GetCurrentThreadId(), FALSE);
            AttachThreadInput(GetCurrentThreadId(), d->gameThread, FALSE);
        }
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
    rtx::launcher::wiki::Shutdown();
    std::vector<Dock*> ds;
    for (auto& kv : g_docks) ds.push_back(kv.second);
    g_docks.clear();
    for (Dock* d : ds) Detach(d, /*closeGame=*/true);
}

}  // namespace rtx::launcher::dock
