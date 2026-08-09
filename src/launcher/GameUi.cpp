// In-game window UI host (see GameUi.h). Pipeline per client:
//
//   Ultralight off-screen View (CPU, transparent, device-scaled)
//     -> Surface dirty rows copied into FrameShare v2 under its seqlock (Tick)
//     -> companion uploads the dirty sub-rect + draws one quad in the present hook.
//
//   Game-window input the companion consumed over our UI rects
//     -> InputShare v2 SPSC ring -> named wake event -> waiter thread
//     -> PostMessage(host, kMsgUiInput) -> DrainInput on the main thread
//     -> View::FireMouse/Scroll/KeyEvent.
//
// Perf contract: idle publishes nothing (dirty-bounds gate), a drag costs one
// dirty-rect row copy per painted frame, and the 16 ms pump timer only runs
// while something recently painted or received input.

#include "GameUi.h"
#include "Bridge.h"
#include "Dock.h"
#include "../shared/Log.h"
#include "../../companion/FrameShare.h"
#include "../../companion/InputShare.h"

#include <AppCore/AppCore.h>
#include <Ultralight/Ultralight.h>
#include <JavaScriptCore/JavaScript.h>
#include <Windows.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rtx::launcher::gameui {

using namespace ultralight;

namespace {

App* g_app = nullptr;

// These views hang off the Renderer directly -- no AppCore Window or Overlay --
// so nothing drives their frame clock: AppCore only refreshes the display ids of
// its own MONITORS (AppCore/Monitor.h), and the documented per-frame contract is
// RefreshDisplay-then-Render (Ultralight/Renderer.h). Left on the default id 0
// they get neither, so animations/transitions/rAF never advance, the views never
// need painting, and the surface dirty bounds stay empty forever. We own an id no
// monitor will ever be assigned and refresh it ourselves from Tick.
constexpr std::uint32_t kUiDisplayId = 1000;

// Waiter-thread context: owned by the THREAD (freed on exit), so Destroy never
// races it. The event handle is a duplicate; the thread closes its own copy.
struct WaiterCtx {
    HWND          host = nullptr;
    std::uint32_t pid = 0;
    HANDLE        evt = nullptr;        // duplicated wake-event handle
    volatile LONG stop = 0;
};

struct Ui : public LoadListener, public ViewListener {
    std::uint32_t pid = 0;
    HWND          host = nullptr;
    RefPtr<View>  view;

    rtx::frame::Share* frame = nullptr;
    HANDLE             frameMap = nullptr;
    rtx::input::Share* input = nullptr;
    HANDLE             inputMap = nullptr;
    HANDLE             inputEvt = nullptr;   // original wake event (waiter holds a dup)
    WaiterCtx*         waiter = nullptr;     // owned by the waiter thread once started

    double        scale = 1.0;
    bool          kbCapture = false;
    bool          visible = false;           // JS-published layer visibility
    std::uint32_t frameId = 0;
    unsigned      viewButtons = 0;           // buttons the VIEW believes are held (self-heal)
    bool          publishedOnce = false;     // diagnostics: first pixel publish logged
    bool          moduleWarned = false;      // diagnostics: stale-companion warning logged
    bool          paintWarned = false;       // diagnostics: never-painted warning logged
    ULONGLONG     boundMs = 0;
    std::string   pendingRects;              // consume rects published before Bind
    bool          pendingVisible = false;
    bool          hasPendingRects = false;
    ULONGLONG     lastActivityMs = 0;        // last publish or input (pump pacing)
    bool          pumpTimerOn = false;
    std::uint32_t lastModSeq = 0;
    ULONGLONG     lastModChangeMs = 0;       // companion liveness

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
        rtx::log::Client(pid, std::string("[ui js ") + lvl + " @" +
                              std::to_string(msg.line_number()) + "] " +
                              msg.message().utf8().data());
    }

    // The page's requested cursor, forwarded to the game-side filter (it answers
    // WM_SETCURSOR while the pointer is over our consume rects).
    void OnChangeCursor(View*, Cursor cursor) override {
        if (input) input->cursor_id = (std::uint32_t)cursor;
        if (frame) frame->cursor = (std::uint32_t)cursor;
    }

    void OnWindowObjectReady(View* v, std::uint64_t, bool is_main, const String&) override {
        if (is_main) attach(v);
    }
    void OnDOMReady(View* v, std::uint64_t, bool is_main, const String&) override {
        if (is_main) attach(v);
    }
    // Same contract as the old dock panel: bridge + read-only __rtx_pid global,
    // re-run on both load events (idempotent property overwrite).
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

std::unordered_map<std::uint32_t, Ui*> g_uis;   // main thread only

Ui* find(std::uint32_t pid) {
    auto it = g_uis.find(pid);
    return it == g_uis.end() ? nullptr : it->second;
}

// Physical client size of the game render area: prefer the companion's live
// per-present feedback; fall back to GetClientRect while the module is absent.
bool ClientSize(Ui* u, int& w, int& h) {
    if (u->frame && u->frame->client_w > 0 && u->frame->client_h > 0) {
        w = u->frame->client_w;
        h = u->frame->client_h;
        return true;
    }
    HWND game = reinterpret_cast<HWND>(dock::GameWindowHandle(u->pid));
    RECT rc;
    if (game && GetClientRect(game, &rc) && rc.right > 0 && rc.bottom > 0) {
        w = rc.right; h = rc.bottom;
        return true;
    }
    return false;
}

// Copy the surface's dirty region into the share under its seqlock. Row-by-row:
// the share is tightly packed (stride = width*4) while the surface row_bytes may
// include padding.
void Publish(Ui* u) {
    if (!u->frame || !u->view) return;
    Surface* s = u->view->surface();
    if (!s) return;
    IntRect db = s->dirty_bounds();
    if (db.IsEmpty()) {
        // Nothing painted since the last publish. Normal while idle -- but a view
        // that has NEVER painted means the render pass below never reached it, so
        // say it once instead of leaving an invisible UI unexplained.
        if (!u->publishedOnce) {
            u->frame->diag = 3;              // "surface not painted yet"
            if (!u->paintWarned && u->boundMs && GetTickCount64() - u->boundMs > 5000) {
                u->paintWarned = true;
                rtx::log::Client(u->pid,
                    std::string("in-game ui: the view has NEVER painted (5s after bind) needs_paint=") +
                    (u->view->needs_paint() ? "1" : "0") +
                    " display=" + std::to_string(u->view->display_id()) +
                    " surface=" + std::to_string(s->width()) + "x" + std::to_string(s->height()));
            }
        }
        return;
    }
    std::uint32_t w = s->width(), h = s->height();
    if (w == 0 || h == 0 || w > rtx::frame::kMaxWidth || h > rtx::frame::kMaxHeight) {
        u->frame->diag = 2;                  // "surface/size issue"
        s->ClearDirtyBounds();
        return;
    }
    std::uint8_t* src = static_cast<std::uint8_t*>(s->LockPixels());
    if (!src) { s->UnlockPixels(); s->ClearDirtyBounds(); return; }   // never leave it locked
    std::uint32_t rb = s->row_bytes();
    rtx::frame::Share* f = u->frame;

    bool sized = (f->width != w || f->height != h);
    int L = db.left, T = db.top, R = db.right, B = db.bottom;
    if (sized) { L = 0; T = 0; R = (int)w; B = (int)h; }
    if (L < 0) L = 0;
    if (T < 0) T = 0;
    if (R > (int)w) R = (int)w;
    if (B > (int)h) B = (int)h;
    if (R > L && B > T) {
        f->seq = f->seq + 1;            // odd: mid-write
        MemoryBarrier();
        f->width = w;
        f->height = h;
        f->stride = w * 4;
        for (int y = T; y < B; ++y)
            std::memcpy(f->pixels + (std::size_t)y * f->stride + (std::size_t)L * 4,
                        src + (std::size_t)y * rb + (std::size_t)L * 4,
                        (std::size_t)(R - L) * 4);
        f->dirty_x = L;
        f->dirty_y = T;
        f->dirty_w = R - L;
        f->dirty_h = B - T;
        f->origin_x = 0;
        f->origin_y = 0;
        f->frame_id = ++u->frameId;
        f->diag = 4;
        MemoryBarrier();
        f->seq = f->seq + 1;            // even: published
        // Pixels exist -> the layer composites. Visibility is deliberately NOT tied
        // to the page publishing input rects: the layer is transparent where nothing
        // is drawn, so compositing it always costs one quad, while gating it on a JS
        // call meant any hiccup in that call left the whole UI invisible.
        f->visible = 1;
        if (!u->publishedOnce) {
            u->publishedOnce = true;
            rtx::log::Client(u->pid, "in-game ui: first frame published " +
                                     std::to_string(w) + "x" + std::to_string(h));
        }
    }
    s->UnlockPixels();
    s->ClearDirtyBounds();
    u->lastActivityMs = GetTickCount64();
}

void WaiterThread(WaiterCtx* ctx) {
    for (;;) {
        DWORD r = WaitForSingleObject(ctx->evt, 250);
        if (InterlockedCompareExchange(&ctx->stop, 0, 0)) break;
        if (r == WAIT_OBJECT_0 && ctx->host)
            PostMessageW(ctx->host, kMsgUiInput, (WPARAM)ctx->pid, 0);
    }
    CloseHandle(ctx->evt);
    delete ctx;
}

MouseEvent::Button ButtonFor(UINT msg) {
    switch (msg) {
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK: return MouseEvent::kButton_Left;
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK: return MouseEvent::kButton_Right;
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK: return MouseEvent::kButton_Middle;
        default: return MouseEvent::kButton_None;
    }
}

unsigned ViewButtonBit(MouseEvent::Button b) {
    switch (b) {
        case MouseEvent::kButton_Left:   return 1u;
        case MouseEvent::kButton_Right:  return 2u;
        case MouseEvent::kButton_Middle: return 4u;
        default: return 0u;
    }
}

void FireOne(Ui* u, const rtx::input::Event& e) {
    if (!u->view) return;
    double sc = (u->scale > 0.01) ? u->scale : 1.0;
    switch (e.msg) {
        case WM_MOUSEMOVE: {
            // Self-heal lost releases: a button-up delivered outside the game
            // window (or dropped by a full ring) never reaches the view, leaving
            // its drag stuck to the cursor. The forwarded wparam carries the REAL
            // held state -- synthesize the missing ups first.
            unsigned held = ((e.wparam & MK_LBUTTON) ? 1u : 0u) |
                            ((e.wparam & MK_RBUTTON) ? 2u : 0u) |
                            ((e.wparam & MK_MBUTTON) ? 4u : 0u);
            unsigned stale = u->viewButtons & ~held;
            if (stale) {
                const MouseEvent::Button all[3] = { MouseEvent::kButton_Left,
                                                    MouseEvent::kButton_Right,
                                                    MouseEvent::kButton_Middle };
                for (MouseEvent::Button b : all) {
                    if (!(stale & ViewButtonBit(b))) continue;
                    MouseEvent up{ MouseEvent::kType_MouseUp,
                                   (int)(e.x / sc), (int)(e.y / sc), b };
                    u->view->FireMouseEvent(up);
                }
                u->viewButtons &= held;
            }
            MouseEvent me{ MouseEvent::kType_MouseMoved,
                           (int)(e.x / sc), (int)(e.y / sc), MouseEvent::kButton_None };
            u->view->FireMouseEvent(me);
            break;
        }
        case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
        case WM_LBUTTONDBLCLK: case WM_RBUTTONDBLCLK: case WM_MBUTTONDBLCLK: {
            // DBLCLK replaces the second button-down on Windows; WebKit detects
            // double-clicks by timing, so replay it as a plain down.
            u->viewButtons |= ViewButtonBit(ButtonFor(e.msg));
            MouseEvent me{ MouseEvent::kType_MouseDown,
                           (int)(e.x / sc), (int)(e.y / sc), ButtonFor(e.msg) };
            u->view->FireMouseEvent(me);
            break;
        }
        case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP: {
            u->viewButtons &= ~ViewButtonBit(ButtonFor(e.msg));
            MouseEvent me{ MouseEvent::kType_MouseUp,
                           (int)(e.x / sc), (int)(e.y / sc), ButtonFor(e.msg) };
            u->view->FireMouseEvent(me);
            break;
        }
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: {
            int delta = (int)(short)HIWORD((DWORD)(std::uint64_t)e.wparam);
            int px = delta * 100 / 120;   // ~100 CSS px per notch
            ScrollEvent se{ ScrollEvent::kType_ScrollByPixel,
                            (e.msg == WM_MOUSEHWHEEL) ? -px : 0,
                            (e.msg == WM_MOUSEWHEEL) ? px : 0 };
            u->view->FireScrollEvent(se);
            break;
        }
        case WM_KEYDOWN:
            u->view->FireKeyEvent(KeyEvent(KeyEvent::kType_RawKeyDown,
                                           (uintptr_t)e.wparam, (intptr_t)e.lparam, false));
            break;
        case WM_SYSKEYDOWN:
            u->view->FireKeyEvent(KeyEvent(KeyEvent::kType_RawKeyDown,
                                           (uintptr_t)e.wparam, (intptr_t)e.lparam, true));
            break;
        case WM_KEYUP: case WM_SYSKEYUP:
            u->view->FireKeyEvent(KeyEvent(KeyEvent::kType_KeyUp,
                                           (uintptr_t)e.wparam, (intptr_t)e.lparam,
                                           e.msg == WM_SYSKEYUP));
            break;
        case WM_CHAR: case WM_SYSCHAR: {
            // Control characters are NOT text. Windows sends WM_CHAR alongside the key event for
            // Ctrl combinations -- 0x7F for Ctrl+Backspace, 0x01..0x1A for Ctrl+letter -- and
            // forwarding those verbatim inserted them into whatever field had focus, which is why
            // Ctrl+Backspace typed a box glyph instead of deleting a word. A browser never inserts
            // these; the editing behaviour itself comes from the RawKeyDown we already sent above.
            // Tab, LF and CR are real text and stay, as does backspace, which works today.
            const unsigned ch = (unsigned)e.wparam;
            if (ch == 0x7F || (ch < 0x20 && ch != 0x08 && ch != 0x09 && ch != 0x0A && ch != 0x0D))
                break;
            u->view->FireKeyEvent(KeyEvent(KeyEvent::kType_Char,
                                           (uintptr_t)e.wparam, (intptr_t)e.lparam,
                                           e.msg == WM_SYSCHAR));
            break;
        }
        case WM_KILLFOCUS:
            // Companion sentinel: a click went to the game while the UI held
            // keyboard capture -- have the page blur its field.
            u->view->EvaluateScript("window.__rtxGameClick && window.__rtxGameClick()");
            break;
        default:
            break;
    }
}

}  // namespace

void Init(App* app) { g_app = app; }

void Prepare(std::uint32_t pid, const std::string& html, double initialScale) {
    if (!g_app || !pid || g_uis.count(pid)) return;
    auto* u = new Ui();
    u->pid = pid;
    u->scale = (initialScale > 0.01) ? initialScale : 1.0;

    ViewConfig cfg;
    cfg.is_accelerated = false;          // CPU surface: the pixel-transport contract
    cfg.is_transparent = true;
    cfg.initial_device_scale = u->scale;
    cfg.display_id = kUiDisplayId;       // our own frame clock (see kUiDisplayId)
    // Start at a sane size; the resize handshake snaps it to the real client
    // area on the first Tick after the companion reports in.
    u->view = g_app->renderer()->CreateView(1280, 720, cfg, nullptr);
    if (!u->view) { delete u; return; }
    u->view->set_load_listener(u);
    u->view->set_view_listener(u);
    u->view->LoadHTML(String(html.c_str()));
    u->view->Focus();
    u->view->set_needs_paint(true);      // force a first frame even before content commits
    g_uis[pid] = u;
    rtx::log::Client(pid, "in-game ui: view prepared (scale " + std::to_string(u->scale) +
                          ", display " + std::to_string(kUiDisplayId) + ")");
}

void Bind(std::uint32_t pid, void* hostHwnd) {
    Ui* u = find(pid);
    if (!u || !hostHwnd || u->host) return;
    u->host = static_cast<HWND>(hostHwnd);

    wchar_t name[64];

    // FrameShare v2 (pixel layer). CreateFileMapping returns the EXISTING object
    // when the companion still holds a mapping from a previous session for this
    // pid, so both sides always converge on one section per name.
    rtx::frame::MakeSectionName(pid, name);
    u->frameMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                     0, (DWORD)sizeof(rtx::frame::Share), name);
    if (u->frameMap) {
        u->frame = reinterpret_cast<rtx::frame::Share*>(
            MapViewOfFile(u->frameMap, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0,
                          sizeof(rtx::frame::Share)));
    }
    if (u->frame) {
        rtx::frame::Share* f = u->frame;
        f->seq = f->seq | 1;             // odd: header re-init in progress
        MemoryBarrier();
        f->pid = pid;
        f->width = f->height = f->stride = 0;
        // frame_id continues MONOTONICALLY from whatever the (possibly still-mapped)
        // companion last consumed: its upload latch persists across our re-binds, and
        // restarting at 0 would make the first frame look already-seen (skipped) or
        // non-contiguous forever. Bind zeroed width/height, so the first publish is a
        // full-surface dirty rect either way.
        u->frameId = f->frame_id;
        f->dirty_x = f->dirty_y = f->dirty_w = f->dirty_h = 0;
        f->origin_x = f->origin_y = 0;
        // module -> launcher feedback. A section reused from a previous session
        // (the companion still maps it) arrives carrying a stale non-zero
        // module_seq, which makes the very first Tick see it "change" and disarms
        // the stale-companion warning for the whole session; the stale client size
        // would likewise answer the resize handshake for a dead session. A live
        // companion rewrites both on its next present.
        f->module_seq = 0;
        f->client_w = f->client_h = 0;
        f->visible = 0;
        f->cursor = 0;
        f->flags = 0;
        f->diag = 1;
        f->version = rtx::frame::kVersion;
        f->magic = rtx::frame::kMagic;   // valid only once everything above is set
        MemoryBarrier();
        f->seq = (f->seq + 1) & ~1u;     // even
    }

    // InputShare v2 (consume rects + event ring + wake event). The EVENT is created
    // BEFORE the section: the companion opens the event only while the section maps,
    // so this order guarantees it can never observe section-without-event.
    rtx::input::MakeEventName(pid, name);
    u->inputEvt = CreateEventW(nullptr, FALSE /*auto-reset*/, FALSE, name);
    rtx::input::MakeSectionName(pid, name);
    u->inputMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                     0, (DWORD)sizeof(rtx::input::Share), name);
    if (u->inputMap) {
        u->input = reinterpret_cast<rtx::input::Share*>(
            MapViewOfFile(u->inputMap, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0,
                          sizeof(rtx::input::Share)));
    }
    if (u->input) {
        rtx::input::Share* in = u->input;
        in->pid = pid;
        in->rect_seq = 0;
        in->rect_count = 0;
        in->capture_keyboard = u->kbCapture ? 1u : 0u;   // honor a pre-Bind capture
        in->cursor_id = 0;
        in->active = 0;
        in->tail = in->head;             // drop any stale events from a prior run
        in->version = rtx::input::kVersion;
        in->magic = rtx::input::kMagic;
    }

    // Input waiter: sleeps on the wake event, marshals to the main thread.
    if (u->inputEvt) {
        auto* ctx = new WaiterCtx();
        ctx->host = u->host;
        ctx->pid = pid;
        ctx->stop = 0;
        if (DuplicateHandle(GetCurrentProcess(), u->inputEvt, GetCurrentProcess(),
                            &ctx->evt, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
            u->waiter = ctx;
            std::thread(WaiterThread, ctx).detach();
        } else {
            delete ctx;
        }
    }
    u->boundMs = GetTickCount64();
    rtx::log::Client(pid, std::string("in-game ui: bound to host (frame ") +
                          (u->frame ? "ok" : "FAILED") + ", input " +
                          (u->input ? "ok" : "FAILED") + ")");
    // Replay any consume-rect publish that arrived before the shares existed.
    if (u->hasPendingRects && u->input) {
        u->hasPendingRects = false;
        std::string csv = u->pendingRects;
        u->pendingRects.clear();
        SetConsumeRects(pid, csv, u->pendingVisible);
    }
}

void Destroy(std::uint32_t pid) {
    Ui* u = find(pid);
    if (!u) return;
    // Quiesce FIRST: the present hook must stop touching the layer before the
    // launcher-side view goes away (same ordering rule as QuiesceMarkers).
    if (u->input) {
        u->input->active = 0;
        u->input->capture_keyboard = 0;
    }
    if (u->frame) {
        u->frame->visible = 0;
    }
    if (u->waiter) {
        InterlockedExchange(&u->waiter->stop, 1);
        if (u->inputEvt) SetEvent(u->inputEvt);   // wake it so it sees the flag
        u->waiter = nullptr;                      // thread frees the ctx
    }
    if (u->host && u->pumpTimerOn) KillTimer(u->host, kUiPumpTimerId);
    if (u->view) {
        u->view->set_load_listener(nullptr);
        u->view->set_view_listener(nullptr);
        u->view = nullptr;
    }
    if (u->frame)    { UnmapViewOfFile(const_cast<rtx::frame::Share*>(u->frame)); u->frame = nullptr; }
    if (u->frameMap) { CloseHandle(u->frameMap); u->frameMap = nullptr; }
    if (u->input)    { UnmapViewOfFile(const_cast<rtx::input::Share*>(u->input)); u->input = nullptr; }
    if (u->inputMap) { CloseHandle(u->inputMap); u->inputMap = nullptr; }
    if (u->inputEvt) { CloseHandle(u->inputEvt); u->inputEvt = nullptr; }
    g_uis.erase(pid);
    delete u;
}

bool IsOpen(std::uint32_t pid) { return g_uis.count(pid) != 0; }

void Tick() {
    if (g_uis.empty()) return;
    ULONGLONG now = GetTickCount64();

    // Frame clock for our unattached views (see kUiDisplayId): drives animations,
    // transitions, smooth scroll and requestAnimationFrame, which is what marks
    // them as needing paint. ONCE per Tick, not per view -- our views share the
    // id, and refreshing it N times would advance their animations N times.
    if (g_app) g_app->renderer()->RefreshDisplay(kUiDisplayId);

    for (auto& kv : g_uis) {
        Ui* u = kv.second;
        if (!u || !u->view) continue;

        // Companion liveness (module_seq advances every present while mapped). A
        // companion that never reports is a STALE DLL: the module is only injected
        // at client launch, so an updated rtxscene.dll needs a client restart --
        // say so once instead of leaving an invisible UI unexplained.
        if (u->frame) {
            std::uint32_t ms = u->frame->module_seq;
            if (ms != u->lastModSeq) { u->lastModSeq = ms; u->lastModChangeMs = now; }
            if (!u->moduleWarned && !u->lastModChangeMs && u->boundMs &&
                now - u->boundMs > 5000) {
                u->moduleWarned = true;
                rtx::log::Client(u->pid,
                    "in-game ui: the companion is NOT compositing this layer (no v2 module "
                    "heartbeat 5s after bind). The in-game module is injected at client "
                    "launch -- restart the game client to load the updated rtxscene.dll.");
            }
        }

        // Resize handshake: track the live client size.
        int cw = 0, ch = 0;
        if (ClientSize(u, cw, ch)) {
            if (cw > (int)rtx::frame::kMaxWidth)  cw = (int)rtx::frame::kMaxWidth;
            if (ch > (int)rtx::frame::kMaxHeight) ch = (int)rtx::frame::kMaxHeight;
            if (cw > 0 && ch > 0 &&
                ((std::uint32_t)cw != u->view->width() || (std::uint32_t)ch != u->view->height()))
                u->view->Resize((std::uint32_t)cw, (std::uint32_t)ch);
        }

        // Paint into the CPU surface ourselves, after the resize and before the
        // copy, so a frame lands in the SAME tick it was painted. Tick runs from
        // AppListener::OnUpdate, which fires right before the run loop's own
        // Renderer::Update/Render -- the documented refresh-then-render point, and
        // never re-entrant (unlike DrainInput, whose Fire*Event can pump). Costs
        // nothing when idle: the renderer skips views that don't need painting.
        if (g_app) {
            View* v = u->view.get();
            g_app->renderer()->RenderOnly(&v, 1);
        }
        Publish(u);

        // Pump pacing: 16 ms wakeups only while something recently painted or
        // received input; idle clients fall back to the 100 ms keep-alive.
        bool wantPump = u->host && (now - u->lastActivityMs) < 2000;
        if (wantPump && !u->pumpTimerOn) {
            SetTimer(u->host, kUiPumpTimerId, 16, nullptr);
            u->pumpTimerOn = true;
        } else if (!wantPump && u->pumpTimerOn) {
            KillTimer(u->host, kUiPumpTimerId);
            u->pumpTimerOn = false;
        }
    }
}

void DrainInput(std::uint32_t pid) {
    Ui* u = find(pid);
    if (!u || !u->input) return;
    rtx::input::Share* in = u->input;
    // The SHARED cursor is the single source of truth: FireOne can re-enter this
    // drain (message pumping inside Fire*Event), and a stale local tail would
    // both double-fire events and move in->tail backwards.
    int guard = 0;
    while (in->tail != in->head && guard++ < (int)rtx::input::kRingSize) {
        std::uint32_t t = in->tail;
        rtx::input::Event e = in->events[t & (rtx::input::kRingSize - 1)];
        in->tail = t + 1;                // consume before replay: replay can re-enter
        FireOne(u, e);
    }
    u->lastActivityMs = GetTickCount64();
}

bool KeyboardCaptured(std::uint32_t pid) {
    Ui* u = find(pid);
    return u && u->kbCapture;
}

bool FireHostKey(std::uint32_t pid, unsigned msg, std::uintptr_t wparam, std::intptr_t lparam) {
    Ui* u = find(pid);
    if (!u || !u->kbCapture || !u->view) return false;
    rtx::input::Event e{};
    e.msg = msg;
    e.wparam = (std::int64_t)wparam;
    e.lparam = (std::int64_t)lparam;
    FireOne(u, e);
    u->lastActivityMs = GetTickCount64();
    return true;
}

void SetDeviceScale(std::uint32_t pid, double scale) {
    Ui* u = find(pid);
    if (!u || !u->view || scale <= 0.01) return;
    if (u->scale > scale - 0.005 && u->scale < scale + 0.005) return;
    u->scale = scale;
    u->view->set_device_scale(scale);
    rtx::log::Client(pid, "in-game ui: device scale -> " + std::to_string(scale));
}

void SetConsumeRects(std::uint32_t pid, const std::string& rectsCsv, bool visible) {
    Ui* u = find(pid);
    if (!u) return;
    if (!u->input) {
        // Published before Bind (the page boots while the embed completes): buffer
        // and replay at Bind, or -- combined with the page's payload dedup -- the
        // layer would stay invisible and input-dead for the whole session.
        u->pendingRects = rectsCsv;
        u->pendingVisible = visible;
        u->hasPendingRects = true;
        return;
    }
    // Parse "x,y,w,h;..." (CSS px), scale to physical px.
    rtx::input::Rect rects[rtx::input::kMaxRects];
    std::uint32_t n = 0;
    double sc = (u->scale > 0.01) ? u->scale : 1.0;
    const char* p = rectsCsv.c_str();
    while (*p && n < rtx::input::kMaxRects) {
        double v[4] = {0, 0, 0, 0};
        int vi = 0;
        bool any = false;
        while (*p && *p != ';') {
            char* end = nullptr;
            double d = strtod(p, &end);
            if (end == p) { ++p; continue; }
            if (vi < 4) v[vi] = d;
            ++vi; any = true;
            p = end;
            if (*p == ',') ++p;
        }
        if (*p == ';') ++p;
        if (!any || vi < 4) continue;
        rects[n].x = (int)(v[0] * sc);
        rects[n].y = (int)(v[1] * sc);
        rects[n].w = (int)(v[2] * sc + 0.999);
        rects[n].h = (int)(v[3] * sc + 0.999);
        if (rects[n].w > 0 && rects[n].h > 0) ++n;
    }
    rtx::input::Share* in = u->input;
    in->rect_seq = in->rect_seq | 1;     // odd: mid-write
    MemoryBarrier();
    for (std::uint32_t i = 0; i < n; ++i) in->rects[i] = rects[i];
    in->rect_count = n;
    MemoryBarrier();
    in->rect_seq = (in->rect_seq + 1) & ~1u;
    // Input only: whether the module consumes messages over our regions. Pixel
    // visibility is owned by Publish (see there) so a missing rect publish can
    // never blank the whole UI.
    in->active = (visible && n > 0) ? 1u : 0u;
    u->visible = visible;
    u->lastActivityMs = GetTickCount64();
}

void SetKeyboardCapture(std::uint32_t pid, bool on) {
    Ui* u = find(pid);
    if (!u) return;
    u->kbCapture = on;
    if (u->input) u->input->capture_keyboard = on ? 1u : 0u;
}

std::string ClientInfoJson(std::uint32_t pid) {
    Ui* u = find(pid);
    if (!u) return "{}";
    int pw = 0, ph = 0;
    ClientSize(u, pw, ph);
    double sc = (u->scale > 0.01) ? u->scale : 1.0;
    bool modAlive = u->frame && u->lastModChangeMs &&
                    (GetTickCount64() - u->lastModChangeMs) < 3000;
    std::string j = "{\"pw\":" + std::to_string(pw) +
                    ",\"ph\":" + std::to_string(ph) +
                    ",\"cw\":" + std::to_string((int)(pw / sc)) +
                    ",\"ch\":" + std::to_string((int)(ph / sc)) +
                    ",\"scale\":" + std::to_string(sc) +
                    ",\"mod\":" + (modAlive ? "true" : "false") + "}";
    return j;
}

void ReloadHtml(std::uint32_t pid, const std::string& html) {
    Ui* u = find(pid);
    if (!u || !u->view) return;
    u->view->LoadHTML(String(html.c_str()));
}

}  // namespace rtx::launcher::gameui
