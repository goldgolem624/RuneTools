#pragma once
// RS3 client memory offsets shared by Reader.cpp and SceneData.cpp. 950-1 moved every MainData-relative offset by +0x40; object-internal offsets unchanged. kDest*: written by click handler 0xE3110 (0xE1A30 on 949), fine units (tile = value / 512). kWalk*: written by CS2 SETWALKMARKER op 308 (2222 on 949).

#include <Windows.h>
#include <cstdint>

namespace rtx::scn {

inline constexpr std::uint32_t kKnownBuildStamps[] = { 0x6a998810 /* 950-1 */ };
inline bool KnownBuild(std::uint64_t base) {
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto nt  = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    for (auto st : kKnownBuildStamps) if (nt->FileHeader.TimeDateStamp == st) return true;
    return false;
}

inline constexpr std::uint64_t kContainer  = 0x199D0;  // root -> worldView container
inline constexpr std::uint64_t kActiveIdx  = 0x70;     // container -> active entry index
inline constexpr std::uint64_t kEntryArr   = 0x58;     // container -> entry array (stride 0x10)
inline constexpr std::uint64_t kEntryWv    = 0x8;      // entry -> worldView object
inline constexpr std::uint64_t kPlayerData = 0x19FA8;  // root -> local-player data block
inline constexpr std::uint64_t kLocalUid   = 0x48;     // data -> local world index

inline constexpr std::uint32_t kWorkerOffDefault = 0x10170;
inline constexpr std::uint32_t kMatrixOffDefault = 0x13090;
inline constexpr std::int32_t  kCamPosRelDefault = 0x80;     // campos rel. to matrix (e,u,n)

inline constexpr std::uint64_t kVecBegin = 0x138;   // worker -> entity vector begin
inline constexpr std::uint64_t kVecEnd   = 0x140;   // worker -> entity vector end
inline constexpr std::uint64_t kSecPtr   = 0x1A0;   // entity -> object-data (sec/sub)
inline constexpr std::uint64_t kBack     = 0x18;    // sec -> entity back-pointer (was +0x8 through 949-5)
inline constexpr std::uint64_t kType     = 0x20;    // sec -> type byte (0x10 through 949-5; header grew 0x10 on 950-1, +0x88 onward unchanged)
inline constexpr std::uint64_t kName     = 0xB8;    // sec -> live name
inline constexpr std::uint64_t kLocFlags = 0xF8;    // scenery sub -> state flags; bit 16 = hidden (the tree/stump swap
                                                     // and the depleted state of every rework tree flip it; a copy sits at +0x230)
inline constexpr std::int32_t  kLocHidden = 0x10000;
inline constexpr std::uint64_t kUid      = 0x88;    // sec -> world uid
inline constexpr std::uint64_t kConfig   = 0x1080;  // sec -> config/model id
inline constexpr std::uint64_t kCombat   = 0x10BC;  // sec -> player combat level
inline constexpr std::uint64_t kPlane    = 0x50;    // sec -> plane (0x40 through 949-5; only verified on plane 0)
inline constexpr std::uint64_t kPosX     = 0x270;   // sec -> fine east
inline constexpr std::uint64_t kPosZ     = 0x274;   // sec -> fine up (height)
inline constexpr std::uint64_t kPosY     = 0x278;   // sec -> fine north
// Movement route (950-1, live-verified 2026-09-11 on players and NPCs, one class for both):
// sec+0x268 -> route object {vtable, begin@+0x08, end@+0x10, cap@+0x18, read@+0x20, write@+0x28}, 21 entries
// of 0x18 bytes {i32, f32 east, f32 up, f32 north, i32, i32} in fine units. The entry just below the write
// cursor is the newest server tile (tile-centred); the visible position trails it by about a tick. An
// empty route (write == begin) means stationary: the visible position is the true tile.
inline constexpr std::uint64_t kMoveMgr     = 0x268;  // sec -> movement route object
inline constexpr std::uint64_t kRouteBegin  = 0x08;   // route -> entry array begin
inline constexpr std::uint64_t kRouteEnd    = 0x10;   // route -> entry array end (begin + 21 * stride)
inline constexpr std::uint64_t kRouteWrite  = 0x28;   // route -> write cursor (one past the newest entry)
inline constexpr std::uint64_t kRouteStride = 0x18;   // entry size
inline constexpr std::uint64_t kRouteX      = 0x04;   // entry -> f32 fine east
inline constexpr std::uint64_t kRouteY      = 0x0C;   // entry -> f32 fine north
// Overhead object (950-1, live-verified 2026-09-12 on a combat dummy, the local player and the
// Woodcutters' Grove tree helpers): sec+0xF08 -> {i32 active hitsplats @0, i32 capacity 6 @4,
// hitsplat ring @+0x20 (6 x 0x18: i32 hitmark, i32 value, i32 start cycle, i32 -1, i32 -1, i32 duration
// 60 cycles), head-bar slots @+0x28 .. +0x30}. The bar slots are a vector of 0x1b0 elements, four of
// them on the actors sampled; only element 0 is ever populated (cycle stamp @+0x78, fill 0..255
// @+0x7C, both matching the live node the drawing code walks). The rest hold uninitialised bytes,
// so the count must come from the vector bounds and never from reading slots until one looks blank.
inline constexpr std::uint64_t kOverhead     = 0xF08;   // sec -> overhead object
inline constexpr std::uint64_t kOvSplatCount = 0x00;
inline constexpr std::uint64_t kOvRing       = 0x20;
inline constexpr std::uint64_t kOvSlots      = 0x28;
inline constexpr std::uint64_t kSplatStride  = 0x18;
inline constexpr std::uint64_t kBarStride    = 0x1b0;   // vector element stride, from the drawing code
inline constexpr std::uint64_t kBarStamp     = 0x78;
inline constexpr std::uint64_t kBarFill      = 0x7C;
inline constexpr std::uint64_t kLpCur        = 0x114C;  // sec -> NPC current life points (local player: varp 13537)
inline constexpr std::uint64_t kLpMax        = 0x1168;  // sec -> NPC max life points
inline constexpr std::uint64_t kNpcTarget    = 0x1364;  // sec -> NPC target player index, -1 none
// Scene entity classes by sec type byte: 1 NPC, 2 player, 3 ground item, 4 world spot animation
// (gfx @+0x84, fine x/up/y @+0x88/+0x8C/+0x90), 5 projectile (fine src x/y @+0x84/+0x88, dst x/y
// @+0x8C/+0x90), 10 scenery, 13 walk marker.
inline constexpr std::uint64_t kProjSrcX     = 0x84;
inline constexpr std::uint64_t kProjSrcY     = 0x88;
inline constexpr std::uint64_t kProjDstX     = 0x8C;
inline constexpr std::uint64_t kProjDstY     = 0x90;
inline constexpr std::uint64_t kT4Size   = 0x1C0;   // type-4 (graphic highlight) object size (0x1B0 + 0x10 header growth; assumed)
inline constexpr std::uint64_t kT4Gfx    = 0x84;    // type-4 -> graphic id (int)   (0x74 through 949-5; live-verified on 950-1)
inline constexpr std::uint64_t kT4PosE   = 0x88;    // type-4 -> fine east  (int32) (0x78 through 949-5)
inline constexpr std::uint64_t kT4PosU   = 0x8C;    // type-4 -> fine up    (int32) (0x7C through 949-5)
inline constexpr std::uint64_t kT4PosN   = 0x90;    // type-4 -> fine north (int32) (0x80 through 949-5)
inline constexpr std::uint64_t kT13Size  = 0x108;   // type-13 (world marker) object size (0xF8 + 0x10 header growth; assumed)

inline constexpr std::uint64_t kMiniMap  = 0x19890; // root -> ClientMiniMap (pointer)
inline constexpr std::uint64_t kDestX    = 0x14;    // minimap -> i32 destination fine X
inline constexpr std::uint64_t kDestY    = 0x18;    // minimap -> i32 destination fine Y
inline constexpr std::uint64_t kDestSrc  = 0x1C;    // minimap -> u8 0 = world click, 1 = minimap
inline constexpr std::uint64_t kWalkMgr  = 0x198D0; // root -> walk-marker appearance manager
inline constexpr std::uint64_t kWalkOn   = 0x10;    // manager -> u8, 0 = marker disabled
inline constexpr std::uint64_t kWalkResX = 0x68;    // manager -> i32 custom resource id (-1 = default)
inline constexpr std::uint64_t kWalkResY = 0x6C;    // manager -> i32 custom resource id (-1 = default)

}  // namespace rtx::scn
