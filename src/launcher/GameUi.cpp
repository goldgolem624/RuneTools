// In-game window UI host (see GameUi.h): off-screen View -> dirty rows into FrameShare v2 under its seqlock; input arrives over the InputShare v2 ring.

#include "GameUi.h"
#include "Bridge.h"
#include "Dock.h"
#include "../shared/Log.h"
#include "IpcGuard.h"
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
#include <mutex>
#include <unordered_map>
#include <vector>

namespace rtx::launcher::gameui {

using namespace ultralight;

namespace {

App* g_app = nullptr;

// Views hang off the Renderer with no AppCore Window, so nothing refreshes their display id;
// we own an id no monitor gets and RefreshDisplay it from Tick, or animations never advance.
constexpr std::uint32_t kUiDisplayId = 1000;

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
    std::string   lastRects;                 // last rects actually written, replayed after a rebind
    bool          lastVisible = false;
    ULONGLONG     lastActivityMs = 0;        // last publish or input (pump pacing)
    bool          pumpTimerOn = false;
    std::uint32_t lastModSeq = 0;
    ULONGLONG     lastModChangeMs = 0;       // companion liveness

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

    void OnChangeCursor(View*, Cursor cursor) override {
        if (input) input->cursor_id = (std::uint32_t)cursor;
        if (frame) frame->cursor = (std::uint32_t)cursor;
    }

    static bool local_url(const String& url) {
        String8 u8 = url.utf8();
        std::string u(u8.data(), u8.length());
        return u.empty() || u.rfind("about:", 0) == 0;
    }
    void OnBeginLoading(View* v, std::uint64_t, bool is_main, const String& url) override {
        if (is_main && !local_url(url)) {
            v->Stop();
            rtx::log::Client(pid, "[ui] BLOCKED main-frame navigation off the local page");
        }
    }
    void OnWindowObjectReady(View* v, std::uint64_t, bool is_main, const String& url) override {
        if (is_main && local_url(url)) attach(v);
    }
    void OnDOMReady(View* v, std::uint64_t, bool is_main, const String& url) override {
        if (is_main && local_url(url)) attach(v);
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

std::unordered_map<std::uint32_t, Ui*> g_uis;   // main thread only

// The module writes into the frame channel and the overlay's render thread wants to read parts of
// it (whether the game is drawing our components, and the screen points it worked out). The map
// above belongs to the main thread, so the channel pointer is kept here as well, behind a lock, and
// the render thread only ever goes through this.
std::mutex g_channelMu;
std::unordered_map<std::uint32_t, rtx::frame::Share*> g_channels;

void SetChannel(std::uint32_t pid, rtx::frame::Share* f) {
    std::lock_guard<std::mutex> lk(g_channelMu);
    if (f) g_channels[pid] = f; else g_channels.erase(pid);
}

Ui* find(std::uint32_t pid) {
    auto it = g_uis.find(pid);
    return it == g_uis.end() ? nullptr : it->second;
}

}  // namespace
int ModuleAnchors(std::uint32_t pid, ModulePoint* out, int cap) {
    if (cap <= 0) return 0;
    std::lock_guard<std::mutex> lk(g_channelMu);
    auto it = g_channels.find(pid);
    if (it == g_channels.end() || !it->second) return 0;
    const rtx::frame::Share* f = it->second;
    if (f->magic != rtx::frame::kMagic || f->version != rtx::frame::kVersion) return 0;
    int n = (int)f->anchor_count;
    if (n < 0) n = 0;
    if (n > cap) n = cap;
    if (n > 64) n = 64;
    for (int i = 0; i < n; ++i) {
        out[i].x = f->anchor[i].x; out[i].y = f->anchor[i].y;
        out[i].depth = f->anchor[i].depth; out[i].ok = f->anchor[i].ok;
        out[i].tag = f->anchor[i].tag;
    }
    return n;
}

bool ModuleGlyphWidths(std::uint32_t pid, std::uint8_t* adv, int count, int& px) {
    if (!adv || count <= 0) return false;
    std::lock_guard<std::mutex> lk(g_channelMu);
    auto it = g_channels.find(pid);
    if (it == g_channels.end() || !it->second) return false;
    const rtx::frame::Share* f = it->second;
    if (f->magic != rtx::frame::kMagic || f->version != rtx::frame::kVersion) return false;
    if (!f->glyph_ready || f->glyph_px == 0) return false;
    const int n = count < (int)sizeof(f->glyph_adv) ? count : (int)sizeof(f->glyph_adv);
    for (int i = 0; i < n; ++i) adv[i] = f->glyph_adv[i];
    px = (int)f->glyph_px;
    return true;
}

bool ModuleDrawsComponents(std::uint32_t pid) {
    std::lock_guard<std::mutex> lk(g_channelMu);
    auto it = g_channels.find(pid);
    return it != g_channels.end() && it->second && it->second->module_cc == 1;
}
namespace {
bool ClientSize(Ui* u, int& w, int& h) {
    if (u->frame && u->frame->client_w > 0 && u->frame->client_h > 0) {
        w = u->frame->client_w;
        h = u->frame->client_h;
        return true;
    }
    HWND game = reinterpret_cast<HWND>(dock::GameWindowHandle(u->pid));
    RECT rc;
    if (game && GetClientRect(game, &rc) && rc.right > 0 && rc.bottom > 0) {
        double f = dock::GameSpaceFactor(game);
        w = (int)(rc.right * f + 0.5);
        h = (int)(rc.bottom * f + 0.5);
        return true;
    }
    return false;
}

// tightly packed (stride = width*4) while the surface row_bytes may be padded.
void Publish(Ui* u) {
    if (!u->frame || !u->view) return;
    Surface* s = u->view->surface();
    if (!s) return;
    IntRect db = s->dirty_bounds();
    if (db.IsEmpty()) {
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
        if (r != WAIT_OBJECT_0 || !ctx->host) continue;
        if (InterlockedCompareExchange(&ctx->stop, 0, 0)) break;
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
        case WM_KEYDOWN: {
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool alt  = (GetKeyState(VK_MENU) & 0x8000) != 0;
            if (ctrl && !alt) {
                const char* cmd = nullptr;
                switch ((unsigned)e.wparam) {
                    case 'A': cmd = "selectAll"; break;
                    case 'C': cmd = "copy"; break;
                    case 'X': cmd = "cut"; break;
                    case 'V': cmd = "paste"; break;
                }
                if (cmd) {
                    u->view->EvaluateScript(String((std::string(
                        "(function(){if(window.__rtxEditCmd){window.__rtxEditCmd('") + cmd + "');return;}"
                        "var d=document;"
                        "while(d.activeElement&&d.activeElement.contentDocument)d=d.activeElement.contentDocument;"
                        "try{d.execCommand('" + cmd + "');}catch(e){}})()").c_str()));
                    break;
                }
            }
            u->view->FireKeyEvent(KeyEvent(KeyEvent::kType_RawKeyDown,
                                           (uintptr_t)e.wparam, (intptr_t)e.lparam, false));
            break;
        }
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
            // Control characters are not text: Windows sends WM_CHAR for Ctrl combinations (0x7F for
            // Ctrl+Backspace, 0x01..0x1A for Ctrl+letter). Tab, LF, CR and backspace stay.
            const unsigned ch = (unsigned)e.wparam;
            if (ch == 0x7F || (ch < 0x20 && ch != 0x08 && ch != 0x09 && ch != 0x0A && ch != 0x0D))
                break;
            u->view->FireKeyEvent(KeyEvent(KeyEvent::kType_Char,
                                           (uintptr_t)e.wparam, (intptr_t)e.lparam,
                                           e.msg == WM_SYSCHAR));
            break;
        }
        case WM_KILLFOCUS:
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

// Creates this client's frame and input channels and starts the input waiter.
// Split out of Bind so a session change can redo it without tearing the view
// down: see RebindChannels below.
void OpenChannels(Ui* u, std::uint32_t pid) {
    wchar_t name[rtx::ipc::kNameChars];

    rtx::frame::MakeSectionName(pid, name);
    bool framePre = false;
    u->frameMap = rtx::ipc::CreateSection(name, (std::uint32_t)sizeof(rtx::frame::Share), framePre);
    if (framePre)
        rtx::log::Launcher("ipc: frame section for pid " + std::to_string(pid) +
                           " was already present at create");
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
        u->frameId = f->frame_id;
        f->dirty_x = f->dirty_y = f->dirty_w = f->dirty_h = 0;
        f->origin_x = f->origin_y = 0;
        f->module_seq = 0; f->module_cc = 0;
        f->client_w = f->client_h = 0;
        f->visible = 0;
        f->cursor = 0;
        f->flags = 0;
        f->diag = 1;
        f->version = rtx::frame::kVersion;
        f->magic = rtx::frame::kMagic;   // valid only once everything above is set
        MemoryBarrier();
        f->seq = (f->seq + 1) & ~1u;     // even
        SetChannel(pid, f);
    }

    rtx::input::MakeEventName(pid, name);
    bool evtPre = false, inPre = false;
    u->inputEvt = rtx::ipc::CreateEvent(name, FALSE /*auto-reset*/, FALSE, evtPre);
    rtx::input::MakeSectionName(pid, name);
    u->inputMap = rtx::ipc::CreateSection(name, (std::uint32_t)sizeof(rtx::input::Share), inPre);
    if (evtPre || inPre)
        rtx::log::Launcher("ipc: input channel for pid " + std::to_string(pid) +
                           " was already present at create");
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
}

// Re-open the channels under the current session names.
//
// The previous views are dropped without unmapping and the handles are left
// open: the module writes frames through its own mapping of the old section on
// its own thread, and it has no way to tell us when it is between writes. A
// client rebinds at most once per launcher run, so this holds a single spare
// view per client until exit.
void SetConsumeRects(std::uint32_t pid, const std::string& rectsCsv, bool visible);   // fwd

void RebindChannels(Ui* u, std::uint32_t pid) {
    if (u->waiter) {
        InterlockedExchange(&u->waiter->stop, 1);
        if (u->inputEvt) SetEvent(u->inputEvt);   // wake it so it sees the flag
        u->waiter = nullptr;                      // thread frees the ctx
    }
    u->frame    = nullptr;
    u->frameMap = nullptr;
    u->input    = nullptr;
    u->inputMap = nullptr;
    u->inputEvt = nullptr;
    OpenChannels(u, pid);
    rtx::log::Client(pid, std::string("in-game ui: channels rebound (frame ") +
                          (u->frame ? "ok" : "FAILED") + ", input " +
                          (u->input ? "ok" : "FAILED") + ")");
    // The new section starts empty, so without this the companion stops
    // consuming clicks over the panels until the page next pushes rects.
    if (u->input && !u->lastRects.empty()) SetConsumeRects(pid, u->lastRects, u->lastVisible);
}

void Bind(std::uint32_t pid, void* hostHwnd) {
    Ui* u = find(pid);
    if (!u || !hostHwnd || u->host) return;
    u->host = static_cast<HWND>(hostHwnd);

    OpenChannels(u, pid);

    u->boundMs = GetTickCount64();
    rtx::log::Client(pid, std::string("in-game ui: bound to host (frame ") +
                          (u->frame ? "ok" : "FAILED") + ", input " +
                          (u->input ? "ok" : "FAILED") + ")");
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
    SetChannel(pid, nullptr);
    if (u->frame)    { UnmapViewOfFile(const_cast<rtx::frame::Share*>(u->frame)); u->frame = nullptr; }
    if (u->frameMap) { CloseHandle(u->frameMap); u->frameMap = nullptr; }
    if (u->input)    { UnmapViewOfFile(const_cast<rtx::input::Share*>(u->input)); u->input = nullptr; }
    if (u->inputMap) { CloseHandle(u->inputMap); u->inputMap = nullptr; }
    if (u->inputEvt) { CloseHandle(u->inputEvt); u->inputEvt = nullptr; }
    g_uis.erase(pid);
    delete u;
    dock::PublishGameClientSize(pid, 0, 0);   // drop the measured game-space size for this pid
}

bool IsOpen(std::uint32_t pid) { return g_uis.count(pid) != 0; }

void Tick() {
    if (g_uis.empty()) return;
    ULONGLONG now = GetTickCount64();

    // A client whose channels were opened before its session existed is still
    // on the previous names; move it across once, here, off the render path.
    {
        static std::uint32_t s_gen = 0;
        if (rtx::ipc::SessionChanged(s_gen)) {
            for (auto& kv : g_uis)
                if (kv.second && kv.second->host) RebindChannels(kv.second, kv.first);
        }
    }

    // A layer the page has hidden (every panel closed) and that saw no input for 2 s is not
    // rendered: the companion is not compositing it, so the paint would be thrown away. It is
    // rendered again on the next tick after it becomes visible or receives input.
    auto idle = [&](Ui* u) { return !u->visible && u->publishedOnce && (now - u->lastActivityMs) > 2000; };
    bool anyActive = false;
    for (auto& kv : g_uis) if (kv.second && kv.second->view && !idle(kv.second)) { anyActive = true; break; }
    if (g_app && anyActive) g_app->renderer()->RefreshDisplay(kUiDisplayId);

    for (auto& kv : g_uis) {
        Ui* u = kv.second;
        if (!u || !u->view) continue;
        const bool render = !idle(u);

        if (u->frame) {
            std::uint32_t ms = u->frame->module_seq;
            if (ms != u->lastModSeq) { u->lastModSeq = ms; u->lastModChangeMs = now; }
            const bool live = u->lastModChangeMs && now - u->lastModChangeMs < 2000;
            dock::PublishGameClientSize(u->pid,
                                        live ? (int)u->frame->client_w : 0,
                                        live ? (int)u->frame->client_h : 0);
            if (!u->moduleWarned && !u->lastModChangeMs && u->boundMs &&
                now - u->boundMs > 5000) {
                u->moduleWarned = true;
                rtx::log::Client(u->pid,
                    "in-game ui: the companion is NOT compositing this layer (no v2 module "
                    "heartbeat 5s after bind). The in-game module is injected at client "
                    "launch -- restart the game client to load the updated rtxscene.dll.");
            }
        }

        int cw = 0, ch = 0;
        if (ClientSize(u, cw, ch)) {
            if (cw > (int)rtx::frame::kMaxWidth)  cw = (int)rtx::frame::kMaxWidth;
            if (ch > (int)rtx::frame::kMaxHeight) ch = (int)rtx::frame::kMaxHeight;
            if (cw > 0 && ch > 0 &&
                ((std::uint32_t)cw != u->view->width() || (std::uint32_t)ch != u->view->height()))
                u->view->Resize((std::uint32_t)cw, (std::uint32_t)ch);
        }

        if (g_app && render) {
            View* v = u->view.get();
            g_app->renderer()->RenderOnly(&v, 1);
        }
        if (render) Publish(u);

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

bool Notify(std::uint32_t pid, const std::string& msg, int ttl_ms) {
    Ui* u = find(pid);
    if (!u || !u->view) return false;
    std::string js = "typeof uiNotify==='function'&&uiNotify(\"";
    for (unsigned char c : msg) {
        if (c == '"' || c == '\\') { js += '\\'; js += (char)c; }
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); js += b; }
        else js += (char)c;
    }
    js += "\",{ttl:" + std::to_string(ttl_ms) + (ttl_ms == 0 ? ",sticky:true" : "") + "})";
    u->view->EvaluateScript(String(js.c_str()));
    u->lastActivityMs = GetTickCount64();   // wake the pump so the card animates in promptly
    return true;
}

void OpenWikiPalette(std::uint32_t pid) {
    Ui* u = find(pid);
    if (!u || !u->view) return;
    u->view->EvaluateScript("typeof wikiPaletteOpen==='function'&&wikiPaletteOpen()");
    u->lastActivityMs = GetTickCount64();
}

void TogglePanels(std::uint32_t pid) {
    Ui* u = find(pid);
    if (!u || !u->view) return;
    u->view->EvaluateScript("typeof wmToggleAll==='function'&&wmToggleAll()");
    u->lastActivityMs = GetTickCount64();
}

void SetDeviceScale(std::uint32_t pid, double scale) {
    Ui* u = find(pid);
    if (!u || !u->view || scale <= 0.01) return;
    if (u->scale > scale - 0.005 && u->scale < scale + 0.005) return;
    u->scale = scale;
    u->view->set_device_scale(scale);
    u->view->set_needs_paint(true);
    u->view->EvaluateScript("try{window.dispatchEvent(new Event('resize'));}catch(e){}");
    rtx::log::Client(pid, "in-game ui: device scale -> " + std::to_string(scale));
}

void SetConsumeRects(std::uint32_t pid, const std::string& rectsCsv, bool visible) {
    Ui* u = find(pid);
    if (!u) return;
    if (!u->input) {
        u->pendingRects = rectsCsv;
        u->pendingVisible = visible;
        u->hasPendingRects = true;
        return;
    }
    u->lastRects   = rectsCsv;
    u->lastVisible = visible;
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
