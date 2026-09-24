#include "EngineMarkers.h"
#include "EngineComponents.h"
#include "EngineOps.h"
#include "EngineIface.h"
#include "ScenePlayer.h"

#include <windows.h>
#include <detours.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace rtx::enginemark {
namespace {

constexpr int kAny = -1;

// The routine that carries out the server's "arrow" message. It starts by splitting the first
// byte into a slot (top three bits) and a kind, and then names the arrow manager:
//   movzx ebx, byte ptr [r8+rax] ; mov rax,[rcx] ; mov r14d,ebx ; shr r14d,5 ; and ebx,1Fh
//   mov rdi, [rax+disp]                                   <- the manager, inside the client's root
constexpr int kArrowSig[] = {
    0x48, 0x89, 0x5C, 0x24, 0x20, 0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56,
    0x48, 0x81, 0xEC, kAny, kAny, 0x00, 0x00,
    0x4C, 0x8B, 0x42, 0x18, 0x4C, 0x8B, 0xE1, 0x33, 0xED, 0x48, 0x8B, 0xF2,
    0x49, 0x8D, 0x40, 0x01, 0x48, 0x89, 0x42, 0x18, 0x48, 0x8B, 0x42, 0x10,
    0x41, 0x0F, 0xB6, 0x1C, 0x00, 0x48, 0x8B, 0x01,
    0x44, 0x8B, 0xF3, 0x41, 0xC1, 0xEE, 0x05, 0x83, 0xE3, 0x1F,
    0x48, 0x8B, 0xB8, kAny, kAny, kAny, kAny,
};
constexpr std::size_t kArrowLen = sizeof(kArrowSig) / sizeof(kArrowSig[0]);
constexpr std::size_t kArrowDispAt = kArrowLen - 4;

// The routine for the server's "trail" message: a slot byte, then a model that is either two
// bytes or, with the top bit set, four.
constexpr int kTrailSig[] = {
    0x48, 0x83, 0xEC, 0x28, 0x4C, 0x8B, 0x42, 0x18, 0x4C, 0x8B, 0xD1, 0x49, 0x8D, 0x48, 0x01,
    0x48, 0x89, 0x4A, 0x18, 0x48, 0x8B, 0x42, 0x10, 0x80, 0x3C, 0x08, 0x7F,
    0x46, 0x0F, 0xB6, 0x1C, 0x00,
};
constexpr std::size_t kTrailLen = sizeof(kTrailSig) / sizeof(kTrailSig[0]);
// further in: add rcx, disp ; mov rcx,[rcx] ; call   <- the trail manager, inside the root
constexpr int kTrailMgrSig[] = { 0x48, 0x81, 0xC1, kAny, kAny, kAny, kAny, 0x48, 0x8B, 0x09, 0xE8 };
constexpr std::size_t kTrailMgrLen = sizeof(kTrailMgrSig) / sizeof(kTrailMgrSig[0]);

// The arrow manager's turn in every frame of the game's own thread. This is what gets hooked:
// the two routines above touch the scene, and this is a moment the game itself touches it.
constexpr int kFrameSig[] = {
    0x48, 0x89, 0x5C, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8D, 0x79, 0x50,
    0x48, 0x89, 0x6C, 0x24, 0x50, 0x48, 0x8D, 0x6F, 0x40, 0x4C, 0x89, 0x74, 0x24, 0x60,
    0x4C, 0x8B, 0xF1, 0x48, 0x8B, 0xDF, 0x48, 0x3B, 0xFD,
};
constexpr std::size_t kFrameLen = sizeof(kFrameSig) / sizeof(kFrameSig[0]);

// Both managers: the root at +8, eight object slots from +0x10. The arrow manager also keeps one
// request per slot from +0x90 whose first field is the slot, or -1 when there is none waiting.
constexpr std::size_t kMgrRoot = 0x08, kMgrSlots = 0x10;
constexpr std::size_t kArrowRequests = 0x90, kArrowRequestSize = 0x2C;
constexpr int kSlot = 7;                       // the last one; the server fills from the first
constexpr int kPathSlot = 6;                   // a second trail, for the way to the target
constexpr std::int32_t kPathReach = 120;       // a trail's points travel as signed bytes from its first
constexpr std::int32_t kTileReach = 48;        // tiles from the player within which anything is asked of the game
constexpr std::size_t kPointerSlots = 0x50;    // the arrows at the player's feet, same eight slots
// The arrow styles the game has loaded: root -> settings (the field before the arrow manager)
// -> a table whose count sits at +8. Null until the first arrow of a session.
constexpr std::size_t kStyleTable = 0x1A8 + 0x10, kStyleCount = 0x08;
constexpr int kMaxTries = 5;

// What each marker draws with. A render object carries the game's outline: colour and alpha as
// four floats, the width, and a flag that is 1 when the width is 0.
constexpr std::size_t kPointerRender = 0x80;
constexpr std::size_t kTrailTiles = 0x80, kTrailTileSize = 0x28, kTrailTileEntity = 0x20;
constexpr std::size_t kTrailMaxTiles = 640;
// Each tile of a trail is an entity of its own: what it draws with at +0x70, and at +0x80 where
// to read its height above the ground from. All of them read one number in the trail manager
// that the game swings up and down, which takes a flat model under the ground half the time.
constexpr std::size_t kTileRender = 0x70, kTileLift = 0x80, kMgrSwing = 0x50;
constexpr std::size_t kEntityNode = 0x18, kNodePosition = 0xD0, kNodeTurn = 0xE0, kNodeScale = 0x100;
constexpr std::uint32_t kMaxReach = 1024;
constexpr std::uint32_t kMaxRange = 90;        // the game squares range * 512 in 32 bits
constexpr std::size_t kOutlineColour = 0x100, kOutlineWidth = 0x110, kOutlineThin = 0x114, kOutlineOn = 0x134;
constexpr float kOutlineAlpha = 0.85f;
constexpr std::uint32_t kMaxWidth = 4;          // the game spreads an outline to one side: a wider one reads as a second copy beside the shape
constexpr unsigned kKindNone = 0, kKindNpc = 1, kKindTile = 2;
constexpr std::uint16_t kTrailNone = 0x7FFF;

// A trail tile's own draw method. It hands its render object to the renderer only in the pass
// that carries bit 0x20, where characters and the arrow at the player's feet take part in every
// pass but the ones marked 1, 2 and 4. The game's outline is made in one of those other passes,
// so a tile never gets one:
//   call [rax+110h] ; test al,al ; je       (is it shown at all)
//   mov r9,[rsp+60h] ; mov eax,[r9+flags]   (the pass)
//   test al,3 ; jne ; test al,4 ; jne ; test al,20h ; je
constexpr int kTileDrawSig[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30,
    0x48, 0x8B, 0x01, 0x49, 0x8B, 0xF8, 0x48, 0x8B, 0xF2, 0x48, 0x8B, 0xD9,
    0xFF, 0x90, 0x10, 0x01, 0x00, 0x00, 0x84, 0xC0, 0x74, kAny,
    0x4C, 0x8B, 0x4C, 0x24, 0x60, 0x41, 0x8B, 0x81, kAny, kAny, 0x00, 0x00,
    0xA8, 0x03, 0x75, kAny, 0xA8, 0x04, 0x75, kAny, 0xA8, 0x20, 0x74, kAny,
};
constexpr std::size_t kTileDrawLen = sizeof(kTileDrawSig) / sizeof(kTileDrawSig[0]);
constexpr std::size_t kTileDrawFlagsAt = 45;
// and, a little further, the hand-over itself: mov [rsp+20h],rax ; call rel32
constexpr int kTileSubmitSig[] = { 0x48, 0x89, 0x44, 0x24, 0x20, 0xE8 };
constexpr std::size_t kTileSubmitLen = sizeof(kTileSubmitSig) / sizeof(kTileSubmitSig[0]);
constexpr std::size_t kShownMethod = 0x110;
constexpr std::uint32_t kPassSkipped = 0x07, kPassTile = 0x20;

// Inside the frame routine the game turns the feet arrow and then has the node take the change in:
//   mov r8d, 45h ; movss [rbx+..],xmm0 ; mov rdx,rbx ; mov rcx,rbx ; call rel32
// That call is how a moved node is made to show it.
constexpr int kRefreshSig[] = { 0x41, 0xB8, 0x45, 0x00, 0x00, 0x00 };
constexpr std::size_t kRefreshLen = sizeof(kRefreshSig) / sizeof(kRefreshSig[0]);
constexpr std::uint32_t kRefreshMoved = 0x07;

using Message = void* (*)(void* self, void* stream);
using Refresh = void (*)(void* from, void* node, std::uint32_t what);
using TileDraw = void (*)(void* self, void* a2, void* a3, void* a4, void* pass, void* a6);
using Submit = void (*)(void* render, void* a2, void* a3, void* pass, void* a6, void* none);
using Shown = bool (*)(void* self);
using Frame = void (*)(void* manager);

Message g_arrow = nullptr, g_trail = nullptr;
Frame   g_frame = nullptr;
TileDraw g_tileDraw = nullptr;
Submit  g_submit = nullptr;
Refresh g_refresh = nullptr;
std::uint32_t g_passFlags = 0;
bool    g_tileDrawHooked = false;
std::uint32_t g_arrowDisp = 0, g_trailDisp = 0;
bool    g_installed = false;

std::mutex g_mu;
Want    g_want;                                // under g_mu
std::atomic<std::uint32_t> g_wantSeq{ 0 };

// game thread only
std::uint32_t g_seenSeq = 0;
void*   g_manager = nullptr;
Want    g_done;
bool    g_ownArrow = false, g_ownTrail = false, g_ownPath = false;
ULONGLONG g_retryAt = 0;
int     g_tries = 0;
float   g_lift = 24.0f;                        // a steady height for our tiles; nearer the ground its outline is cut by it
const std::uint8_t* g_imageLo = nullptr;
const std::uint8_t* g_imageHi = nullptr;

std::mutex g_logMu;
char    g_log[400] = {};

void Say(const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_logMu);
    std::size_t used = std::strlen(g_log);
    if (used && used + 4 < sizeof(g_log)) { std::memcpy(g_log + used, " | ", 4); used += 3; }
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(g_log + used, sizeof(g_log) - used, fmt, ap);
    va_end(ap);
}

// What the two routines read from: the bytes at +0x10, how far along at +0x18.
struct Stream {
    std::uint64_t head[2] = {};
    const std::uint8_t* bytes = nullptr;
    std::uint64_t at = 0;
    std::uint64_t tail[8] = {};
};

bool Send(Message fn, void* self, const std::uint8_t* bytes) {
    Stream s;
    s.bytes = bytes;
    __try {
        fn(self, &s);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SendArrow(void* root, unsigned kind, const Want& w) {
    std::uint8_t m[16] = {};
    m[0] = (std::uint8_t)((kSlot << 5) | kind);
    if (kind == kKindNpc) {
        // style, the NPC, how fast it blinks (0 = steady), four spare bytes, then the feet arrow's model
        m[1] = (std::uint8_t)w.arrow_style;
        m[2] = (std::uint8_t)(w.arrow_npc >> 8); m[3] = (std::uint8_t)w.arrow_npc;
        const std::uint32_t p = (std::uint32_t)w.arrow_pointer;
        m[10] = (std::uint8_t)(p >> 24); m[11] = (std::uint8_t)(p >> 16); m[12] = (std::uint8_t)(p >> 8); m[13] = (std::uint8_t)p;
    } else if (kind != kKindNone) {
        m[1] = (std::uint8_t)w.arrow_style;
        m[2] = (std::uint8_t)w.arrow_plane;
        m[3] = (std::uint8_t)(w.arrow_x >> 8); m[4] = (std::uint8_t)w.arrow_x;
        m[5] = (std::uint8_t)(w.arrow_y >> 8); m[6] = (std::uint8_t)w.arrow_y;
        m[7] = (std::uint8_t)w.arrow_height;
        const std::uint32_t range = w.arrow_range == 0 ? kMaxRange : w.arrow_range > kMaxRange ? kMaxRange : w.arrow_range;
        m[8] = (std::uint8_t)(range >> 8); m[9] = (std::uint8_t)range;
        const std::uint32_t p = (std::uint32_t)w.arrow_pointer;
        m[10] = (std::uint8_t)(p >> 24); m[11] = (std::uint8_t)(p >> 16); m[12] = (std::uint8_t)(p >> 8); m[13] = (std::uint8_t)p;
    }
    void* self = root;                          // the routine reads the root through its first argument
    return Send(g_arrow, &self, m);
}

// The way to the target: two points, and the game lays the model on every tile between them,
// slanting first and then straight, the way a character walks it.
bool SendPath(void* root, bool on, const Want& w) {
    std::uint8_t m[24] = {};
    std::size_t n = 0;
    m[n++] = (std::uint8_t)kPathSlot;
    if (!on) {
        m[n++] = (std::uint8_t)(kTrailNone >> 8); m[n++] = (std::uint8_t)kTrailNone;
    } else {
        const std::uint32_t id = (w.path_model & 0x7FFFFFFFu) | 0x80000000u;
        m[n++] = (std::uint8_t)(id >> 24); m[n++] = (std::uint8_t)(id >> 16); m[n++] = (std::uint8_t)(id >> 8); m[n++] = (std::uint8_t)id;
        std::int32_t dx = w.path_x1 - w.path_x0, dy = w.path_y1 - w.path_y0;
        dx = dx > kPathReach ? kPathReach : dx < -kPathReach ? -kPathReach : dx;   // further than that: as far as it goes
        dy = dy > kPathReach ? kPathReach : dy < -kPathReach ? -kPathReach : dy;
        m[n++] = (std::uint8_t)(2 + 0x40);
        m[n++] = (std::uint8_t)(w.path_x0 >> 8); m[n++] = (std::uint8_t)w.path_x0;
        m[n++] = (std::uint8_t)(w.path_y0 >> 8); m[n++] = (std::uint8_t)w.path_y0;
        m[n++] = 0; m[n++] = 0;
        m[n++] = (std::uint8_t)(std::int8_t)dx; m[n++] = (std::uint8_t)(std::int8_t)dy;
    }
    void* self[2] = { nullptr, root };
    return Send(g_trail, self, m);
}

bool SendTrail(void* root, bool on, const Want& w) {
    std::uint8_t m[24] = {};
    std::size_t n = 0;
    m[n++] = (std::uint8_t)kSlot;
    if (!on) {
        m[n++] = (std::uint8_t)(kTrailNone >> 8); m[n++] = (std::uint8_t)kTrailNone;
    } else {
        const std::uint32_t id = (w.tile_model & 0x7FFFFFFFu) | 0x80000000u;
        m[n++] = (std::uint8_t)(id >> 24); m[n++] = (std::uint8_t)(id >> 16); m[n++] = (std::uint8_t)(id >> 8); m[n++] = (std::uint8_t)id;
        // A trail marks the tiles it steps onto, never the one it starts from: start next door.
        const std::int32_t from = w.tile_x - 1;
        m[n++] = (std::uint8_t)(2 + 0x40);      // two points; counts up to 63 travel as one byte, offset by 64
        m[n++] = (std::uint8_t)(from >> 8); m[n++] = (std::uint8_t)from;
        m[n++] = (std::uint8_t)(w.tile_y >> 8); m[n++] = (std::uint8_t)w.tile_y;
        m[n++] = 0; m[n++] = 0;                 // each point as an offset from that base
        m[n++] = 1; m[n++] = 0;
    }
    void* self[2] = { nullptr, root };          // this one keeps the root in its second field
    return Send(g_trail, self, m);
}

bool SameArrow(const Want& a, const Want& b) {
    return a.arrow_on == b.arrow_on && a.arrow_x == b.arrow_x && a.arrow_y == b.arrow_y && a.arrow_plane == b.arrow_plane &&
           a.arrow_style == b.arrow_style && a.arrow_height == b.arrow_height && a.arrow_pointer == b.arrow_pointer &&
           a.arrow_range == b.arrow_range && a.arrow_npc == b.arrow_npc;
}
bool SameTile(const Want& a, const Want& b) {
    return a.tile_on == b.tile_on && a.tile_x == b.tile_x && a.tile_y == b.tile_y && a.tile_model == b.tile_model &&
           a.tile_steady == b.tile_steady;
}

bool SamePath(const Want& a, const Want& b) {
    return a.path_on == b.path_on && a.path_x0 == b.path_x0 && a.path_y0 == b.path_y0 && a.path_x1 == b.path_x1 &&
           a.path_y1 == b.path_y1 && a.path_model == b.path_model;
}

void* SlotObject(const std::uint8_t* manager, int slot = kSlot) {
    return *reinterpret_cast<void* const*>(manager + kMgrSlots + slot * sizeof(void*));
}

// Gives one render object the game's outline, or takes it away. Anything that does not look like
// a render object is left alone.
void Outline(std::uint8_t* render, std::uint32_t rgb, std::uint32_t width) {
    if (!render) return;
    const std::uint8_t* table = *reinterpret_cast<const std::uint8_t* const*>(render);
    if (table < g_imageLo || table >= g_imageHi) return;
    float was[6];
    std::memcpy(was, render + kOutlineColour, sizeof(was));
    if (!(was[3] >= 0.0f && was[3] <= 1.0f) || !(was[4] >= 0.0f && was[4] <= 64.0f) || (was[5] != 0.0f && was[5] != 1.0f)) return;
    if (width > kMaxWidth) width = kMaxWidth;
    float now[6] = {};
    if (rgb != 0 && width != 0) {
        now[0] = (float)((rgb >> 16) & 0xFF) / 255.0f;
        now[1] = (float)((rgb >> 8) & 0xFF) / 255.0f;
        now[2] = (float)(rgb & 0xFF) / 255.0f;
        now[3] = kOutlineAlpha;
        now[4] = (float)width;
    } else if (was[3] == 0.0f && was[4] == 0.0f) {
        return;                                 // none wanted, none there
    }
    if (std::memcmp(was, now, sizeof(now)) == 0) return;
    std::memcpy(render + kOutlineColour, now, sizeof(now));
    render[kOutlineOn] = 1;
}

std::uint8_t* At(const std::uint8_t* from, std::size_t offset) {
    return from ? *reinterpret_cast<std::uint8_t* const*>(from + offset) : nullptr;
}

bool InImage(const std::uint8_t* object) {
    if (!object) return false;
    const std::uint8_t* table = *reinterpret_cast<const std::uint8_t* const*>(object);
    return table >= g_imageLo && table < g_imageHi;
}

void Scale(std::uint8_t* node, std::uint32_t percent) {
    if (!node) return;
    float was[3];
    std::memcpy(was, node + kNodeScale, sizeof(was));
    for (float v : was) if (!(v > 0.01f && v < 100.0f)) return;      // not a scale: not the node this knows
    const float f = (float)(percent < 100 ? 100 : percent > 400 ? 400 : percent) / 100.0f;
    const float now[3] = { f, f, f };
    if (std::memcmp(was, now, sizeof(now)) != 0) std::memcpy(node + kNodeScale, now, sizeof(now));
}

// The arrow at the player's feet hangs from the player and the game only ever turns it, so on
// its own it spins on the spot, half under the character. Moved out along the way it points it
// circles the player instead. The turn is about the upright axis alone: (0, sin a/2, 0, cos a/2).
void Reach(std::uint8_t* node, std::uint32_t reach) {
    if (!node) return;
    float turn[4], was[3];
    std::memcpy(turn, node + kNodeTurn, sizeof(turn));
    std::memcpy(was, node + kNodePosition, sizeof(was));
    const float len = turn[1] * turn[1] + turn[3] * turn[3];
    if (!(turn[0] > -0.01f && turn[0] < 0.01f && turn[2] > -0.01f && turn[2] < 0.01f && len > 0.98f && len < 1.02f)) return;
    for (float v : was) if (!(v > -2048.0f && v < 2048.0f)) return;  // not a place next to the player
    const float r = (float)(reach > kMaxReach ? kMaxReach : reach);
    const float now[3] = { 2.0f * turn[1] * turn[3] * r, was[1], (1.0f - 2.0f * turn[1] * turn[1]) * r };
    if (std::memcmp(was, now, sizeof(now)) != 0) std::memcpy(node + kNodePosition, now, sizeof(now));
}

// An arrow over an NPC hangs from the NPC and the game puts it at the top of the NPC's model,
// on the head rather than over it. Its own place under the NPC is ours to set: straight up.
void Raise(std::uint8_t* node, std::uint32_t height) {
    if (!node || !g_refresh) return;
    float was[3];
    std::memcpy(was, node + kNodePosition, sizeof(was));
    for (float v : was) if (!(v > -4096.0f && v < 4096.0f)) return;  // not a place relative to the NPC
    const float now[3] = { 0.0f, (float)((height > 255 ? 255 : height) * 8), 0.0f };
    if (std::memcmp(was, now, sizeof(now)) == 0) return;
    std::memcpy(node + kNodePosition, now, sizeof(now));
    g_refresh(node, node, kRefreshMoved);
}

// Every frame: the game only writes the outline of what it outlines itself, so ours stays put,
// but a model arrives some frames after its marker and each new one starts without.
void TendOurs(std::uint8_t* manager, std::uint8_t* trails) {
    if (g_ownArrow && g_done.arrow_npc >= 0) {
        std::uint8_t* arrow = At(manager, kMgrSlots + kSlot * sizeof(void*));
        if (InImage(arrow)) Raise(At(arrow, kEntityNode), g_done.arrow_height);
    }
    if (g_ownArrow) {
        std::uint8_t* pointer = At(manager, kPointerSlots + kSlot * sizeof(void*));
        if (InImage(pointer)) {
            Outline(At(pointer, kPointerRender), g_done.arrow_rgb, g_done.arrow_width);
            Scale(At(pointer, kEntityNode), g_done.pointer_scale);   // the game refreshes this node every frame
            Reach(At(pointer, kEntityNode), g_done.pointer_reach);
        }
    }
    if (g_ownTrail) {
        const std::uint8_t* trail = At(trails, kMgrSlots + kSlot * sizeof(void*));
        if (!trail) return;
        const std::uint8_t* tile = At(trail, kTrailTiles);
        const std::uint8_t* end = At(trail, kTrailTiles + sizeof(void*));
        if (!tile || end < tile || (std::size_t)(end - tile) > kTrailMaxTiles * kTrailTileSize) return;
        for (; tile + kTrailTileSize <= end; tile += kTrailTileSize) {
            std::uint8_t* entity = At(tile, kTrailTileEntity);
            if (!InImage(entity)) continue;
            // only an entity that reads the manager's swinging number is one of these
            float** lift = reinterpret_cast<float**>(entity + kTileLift);
            if (g_done.tile_steady && *lift == reinterpret_cast<float*>(trails + kMgrSwing)) *lift = &g_lift;
            if (*lift != &g_lift) continue;
            Outline(At(entity, kTileRender), g_done.tile_rgb, g_done.tile_width);
        }
    }
}

void ApplyUnguarded(std::uint8_t* manager) {
    std::uint8_t* root = *reinterpret_cast<std::uint8_t**>(manager + kMgrRoot);
    if (!root || *reinterpret_cast<std::uint8_t**>(root + g_arrowDisp) != manager) return;
    std::uint8_t* trails = *reinterpret_cast<std::uint8_t**>(root + g_trailDisp);
    if (!trails || *reinterpret_cast<std::uint8_t**>(trails + kMgrRoot) != root) return;

    if (manager != g_manager) {             // a new session: nothing of ours is in it
        g_manager = manager;
        g_done = Want{};
        g_ownArrow = g_ownTrail = g_ownPath = false;
    }
    const std::uint32_t seq = g_wantSeq.load();
    const ULONGLONG now = GetTickCount64();
    // the game drops its markers when it rebuilds the scene: ask again, but not every frame
    std::int32_t waiting;
    std::memcpy(&waiting, manager + kArrowRequests + kSlot * kArrowRequestSize, sizeof(waiting));
    const bool lostArrow = g_ownArrow && !SlotObject(manager) && waiting == -1;
    const bool lostTrail = g_ownTrail && !SlotObject(trails);
    const bool lostPath = g_ownPath && !SlotObject(trails, kPathSlot);
    if (seq == g_seenSeq) {
        TendOurs(manager, trails);
        if (!(lostArrow || lostTrail || lostPath) || now < g_retryAt || g_tries >= kMaxTries) return;
        ++g_tries;
    } else {
        g_tries = 0;
    }
    g_seenSeq = seq;
    g_retryAt = now + 2000;

    Want w;
    { std::lock_guard<std::mutex> lk(g_mu); w = g_want; }
    // The game puts these on ground it has loaded and crashes on a tile it has not. Reach is measured
    // from the player's tile as read from the game itself, never from the request (whose path starts at
    // the tile the launcher believes the player is on), so a far tile is dropped whatever asked for it.
    int px = 0, py = 0;
    const bool havePlayer = rtx::sceneplayer::Tile(px, py);
    const auto inReach = [&](std::int32_t x, std::int32_t y) {
        const std::int32_t dx = x - px, dy = y - py;
        return havePlayer && x > 0 && y > 0 && dx >= -kTileReach && dx <= kTileReach && dy >= -kTileReach && dy <= kTileReach;
    };
    if (w.arrow_on && (w.arrow_npc < 0 ? !inReach(w.arrow_x, w.arrow_y) : w.arrow_npc > 0xFFFF)) w.arrow_on = false;
    if (w.tile_on && !inReach(w.tile_x, w.tile_y)) w.tile_on = false;
    if (w.path_on && (!inReach(w.path_x0, w.path_y0) || !inReach(w.path_x1, w.path_y1))) w.path_on = false;
    // a style the game does not have is refused outright: hold it to the ones there are
    if (const std::uint8_t* table = At(At(root, g_arrowDisp - sizeof(void*)), kStyleTable)) {
        std::int32_t count = 0;
        std::memcpy(&count, table + kStyleCount, sizeof(count));
        if (count > 0 && count < 256 && w.arrow_style >= (std::uint32_t)count) w.arrow_style = (std::uint32_t)count - 1;
    }

    if (!SameArrow(w, g_done) || lostArrow) {
        // a slot the game filled by itself is the game's: leave it alone
        if (g_ownArrow || (!SlotObject(manager) && waiting == -1)) {
            const bool ok = SendArrow(root, !w.arrow_on ? kKindNone : w.arrow_npc >= 0 ? kKindNpc : kKindTile, w);
            g_ownArrow = ok && w.arrow_on;
            Say(ok ? "markers: arrow %s at %d,%d style %u height %u range %u feet %d"
                   : "markers: arrow request failed (%s %d,%d style %u height %u range %u feet %d)",
                w.arrow_on ? (w.arrow_npc >= 0 ? "set on npc" : "set") : "cleared", w.arrow_x, w.arrow_y, w.arrow_style, w.arrow_height, w.arrow_range, w.arrow_pointer);
        } else {
            Say("markers: arrow slot is in use by the game");
        }
    }
    if (!SameTile(w, g_done) || lostTrail) {
        if (g_ownTrail || !SlotObject(trails)) {
            const bool ok = SendTrail(root, w.tile_on, w);
            g_ownTrail = ok && w.tile_on;
            Say(ok ? "markers: tile %s at %d,%d model %u" : "markers: tile request failed (%s %d,%d model %u)",
                w.tile_on ? "set" : "cleared", w.tile_x, w.tile_y, w.tile_model);
        } else {
            Say("markers: tile slot is in use by the game");
        }
    }
    if (!SamePath(w, g_done) || lostPath) {
        // re-laid every time the player takes a step: only a refusal is worth a line in the log
        if (g_ownPath || !SlotObject(trails, kPathSlot)) {
            const bool lay = w.path_on && w.path_model != 0;
            const bool ok = SendPath(root, lay, w);
            g_ownPath = ok && lay;
            if (!ok) Say("markers: path request failed (%d,%d to %d,%d model %u)", w.path_x0, w.path_y0, w.path_x1, w.path_y1, w.path_model);
        }
    }
    g_done = w;
}

void Apply(std::uint8_t* manager) {
    __try {
        ApplyUnguarded(manager);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// One of our tiles, in a pass its own method sat out: hand it over the way a character is.
void TileExtraPass(void* self, void* a2, void* a3, void* pass, void* a6) {
    __try {
        std::uint8_t* tile = static_cast<std::uint8_t*>(self);
        if (!tile || !pass) return;
        std::uint32_t flags;
        std::memcpy(&flags, static_cast<std::uint8_t*>(pass) + g_passFlags, sizeof(flags));
        if ((flags & kPassSkipped) || (flags & kPassTile)) return;       // not for models, or drawn already
        void* render = *reinterpret_cast<void**>(tile + kTileRender);
        if (!render) return;
        const Shown shown = *reinterpret_cast<Shown*>(*reinterpret_cast<std::uint8_t**>(tile) + kShownMethod);
        if (!shown(self)) return;
        g_submit(render, a2, a3, pass, a6, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

bool OurTile(void* self) {
    __try {
        return self && *reinterpret_cast<float**>(static_cast<std::uint8_t*>(self) + kTileLift) == &g_lift;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void TileDrawHook(void* self, void* a2, void* a3, void* a4, void* pass, void* a6) {
    g_tileDraw(self, a2, a3, a4, pass, a6);
    // only the outline needs the other passes
    if (g_done.tile_rgb != 0 && g_done.tile_width != 0 && OurTile(self)) TileExtraPass(self, a2, a3, pass, a6);
}

void FrameHook(void* manager) {
    if (manager) Apply(static_cast<std::uint8_t*>(manager));
    if (manager) {
        std::uint8_t* root = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(manager) + 8);
        rtx::enginecc::Apply(root);
        rtx::engineops::Pump(root);
        rtx::engineops::PumpAnchors(root);
        rtx::engineops::PumpAsks(root);
    }
    g_frame(manager);
}

struct Image {
    const std::uint8_t* base = nullptr;
    const IMAGE_NT_HEADERS64* nt = nullptr;
};

bool OpenImage(Image& im) {
    im.base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
    if (!im.base) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(im.base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    im.nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(im.base + dos->e_lfanew);
    return im.nt->Signature == IMAGE_NT_SIGNATURE;
}

bool Matches(const std::uint8_t* p, const int* sig, std::size_t len) {
    for (std::size_t k = 0; k < len; ++k)
        if (sig[k] != kAny && p[k] != (std::uint8_t)sig[k]) return false;
    return true;
}

// The single place in the code that matches, or null when there is none or more than one.
const std::uint8_t* FindUnique(const Image& im, const int* sig, std::size_t len) {
    const std::uint8_t* found = nullptr;
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(im.nt);
    for (unsigned i = 0; i < im.nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const std::uint8_t* p = im.base + sec->VirtualAddress;
        const std::size_t n = sec->Misc.VirtualSize;
        if (n < len) continue;
        for (std::size_t o = 0; o + len <= n; ++o) {
            if (p[o] != (std::uint8_t)sig[0] || !Matches(p + o, sig, len)) continue;
            if (found) return nullptr;
            found = p + o;
        }
    }
    return found;
}

}  // namespace

bool Install() {
    if (g_installed) return true;
    Image im;
    if (!OpenImage(im)) return false;
    const std::uint8_t* arrow = FindUnique(im, kArrowSig, kArrowLen);
    const std::uint8_t* trail = FindUnique(im, kTrailSig, kTrailLen);
    const std::uint8_t* frame = FindUnique(im, kFrameSig, kFrameLen);
    if (!arrow || !trail || !frame) return false;

    std::memcpy(&g_arrowDisp, arrow + kArrowDispAt, 4);
    const std::uint8_t* named = nullptr;
    for (std::size_t o = kTrailLen; o < 0xA0 && !named; ++o)
        if (Matches(trail + o, kTrailMgrSig, kTrailMgrLen)) named = trail + o;
    if (!named) return false;
    std::memcpy(&g_trailDisp, named + 3, 4);
    // the two managers sit side by side in the root; anything else is not the layout this knows
    if (g_arrowDisp == 0 || g_arrowDisp > 0x100000 || g_trailDisp != g_arrowDisp + 8) return false;

    g_arrow = reinterpret_cast<Message>(const_cast<std::uint8_t*>(arrow));
    g_trail = reinterpret_cast<Message>(const_cast<std::uint8_t*>(trail));
    g_frame = reinterpret_cast<Frame>(const_cast<std::uint8_t*>(frame));
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&reinterpret_cast<PVOID&>(g_frame), reinterpret_cast<PVOID>(FrameHook));
    g_installed = DetourTransactionCommit() == NO_ERROR;
    // how the game has a moved node show it: read out of the frame routine, an extra as well
    for (std::size_t o = kFrameLen; g_installed && o < 0x200 && !g_refresh; ++o) {
        if (!Matches(frame + o, kRefreshSig, kRefreshLen)) continue;
        for (std::size_t c = o + kRefreshLen; c < o + kRefreshLen + 0x20; ++c) {
            if (frame[c] != 0xE8) continue;
            std::int32_t rel;
            std::memcpy(&rel, frame + c + 1, 4);
            const std::uint8_t* target = frame + c + 5 + rel;
            const std::uint8_t* lo = im.base;
            if (target > lo && target < lo + im.nt->OptionalHeader.SizeOfImage && target[0] == 0x48 && target[1] == 0x89 && target[2] == 0x5C)
                g_refresh = reinterpret_cast<Refresh>(const_cast<std::uint8_t*>(target));
            break;
        }
    }
    // the outline of a tile is an extra: without this the tile still shows, plain
    if (g_installed) {
        const std::uint8_t* draw = FindUnique(im, kTileDrawSig, kTileDrawLen);
        const std::uint8_t* call = nullptr;
        for (std::size_t o = kTileDrawLen; draw && o < 0x90 && !call; ++o)
            if (Matches(draw + o, kTileSubmitSig, kTileSubmitLen)) call = draw + o + kTileSubmitLen;
        if (call) {
            std::int32_t rel;
            std::memcpy(&rel, call, 4);
            std::memcpy(&g_passFlags, draw + kTileDrawFlagsAt, 4);
            g_submit = reinterpret_cast<Submit>(const_cast<std::uint8_t*>(call) + 4 + rel);
            g_tileDraw = reinterpret_cast<TileDraw>(const_cast<std::uint8_t*>(draw));
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourAttach(&reinterpret_cast<PVOID&>(g_tileDraw), reinterpret_cast<PVOID>(TileDrawHook));
            g_tileDrawHooked = DetourTransactionCommit() == NO_ERROR;
        }
    }
    g_imageLo = im.base;
    g_imageHi = im.base + im.nt->OptionalHeader.SizeOfImage;
    return g_installed;
}

void Uninstall() {
    if (!g_installed) return;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&reinterpret_cast<PVOID&>(g_frame), reinterpret_cast<PVOID>(FrameHook));
    if (g_tileDrawHooked) DetourDetach(&reinterpret_cast<PVOID&>(g_tileDraw), reinterpret_cast<PVOID>(TileDrawHook));
    DetourTransactionCommit();
    g_installed = false;
}

void Update(const Want& want) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (SameArrow(want, g_want) && SameTile(want, g_want) && SamePath(want, g_want) && want.arrow_rgb == g_want.arrow_rgb &&
        want.arrow_width == g_want.arrow_width && want.tile_rgb == g_want.tile_rgb && want.tile_width == g_want.tile_width &&
        want.pointer_scale == g_want.pointer_scale && want.pointer_reach == g_want.pointer_reach) return;
    g_want = want;
    g_wantSeq.fetch_add(1);
}

bool TakeLog(char* out, std::size_t cap) {
    std::lock_guard<std::mutex> lk(g_logMu);
    if (!g_log[0] || cap == 0) return false;
    std::snprintf(out, cap, "%s", g_log);
    g_log[0] = 0;
    return true;
}

}  // namespace rtx::enginemark
