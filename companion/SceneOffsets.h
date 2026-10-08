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
// World grid: [wv+0x140C0] owns it (the worldView itself on the static map, bounds 0,0..98,198). Rows are indexed
// by mapsquare x - min x, cells (0x18 each) by y - min y, region pointer at cell+8.
inline constexpr std::uint64_t kWvGrid     = 0x140C0;  // worldView -> grid owner
inline constexpr std::uint64_t kGridMin    = 0x14034;  // grid -> i32 min x, min y, max x, max y (mapsquares)
inline constexpr std::uint64_t kGridRows   = 0x14080;  // grid -> row array (0x18 per row, cell array begin at +0)
inline constexpr std::uint64_t kMapDef     = 0xA4190;  // worldView -> instance map definition (reads 0 on the static map)
inline constexpr std::uint64_t kRegionWv   = 0x60;     // region -> owning worldView
inline constexpr std::uint64_t kRegionRx   = 0x6C;     // region -> i32 mapsquare x
inline constexpr std::uint64_t kRegionRy   = 0x70;     // region -> i32 mapsquare y
inline constexpr std::uint64_t kWvBox      = 0xE83E8;  // worldView -> loc menu box (server packet 216); its first field == wv

inline constexpr std::uint32_t kWorkerOffDefault = 0x10170;
inline constexpr std::uint32_t kMatrixOffDefault = 0x13090;
inline constexpr std::int32_t  kCamPosRelDefault = 0x80;     // campos rel. to matrix (e,u,n)

inline constexpr std::uint64_t kVecBegin = 0x138;   // worker -> entity vector begin
inline constexpr std::uint64_t kVecEnd   = 0x140;   // worker -> entity vector end
inline constexpr std::uint64_t kSecPtr   = 0x1A0;   // entity -> object-data (sec/sub)
inline constexpr std::uint64_t kBack     = 0x18;    // sec -> entity back-pointer (was +0x8 through 949-5)
inline constexpr std::uint64_t kType     = 0x20;    // sec -> type byte (0x10 through 949-5; header grew 0x10 on 950-1, +0x88 onward unchanged)
inline constexpr std::uint64_t kName     = 0xB8;    // sec -> live name: the 64-byte buffer of the string at sec+0x90
                                                     // {ptr +0x90 (== sec+0xB8), u64 len +0x98, cap +0xA0 = 63 with bit 63
                                                     // set}; UTF-8, NBSP is C2 A0
inline constexpr std::uint64_t kLocFlags = 0xF8;    // scenery sub -> state flags; bit 16 = hidden (the tree/stump swap
                                                     // and the depleted state of every rework tree flip it; a copy sits at +0x230)
inline constexpr std::int32_t  kLocHidden = 0x10000;
inline constexpr std::uint64_t kLocTypeCur = 0xD0;  // type-0 loc sub -> current LocType* (id at +8); NULL = the game draws
                                                     // nothing (multiloc resolved to no child)
inline constexpr std::uint64_t kUid      = 0x88;    // sec -> world uid
inline constexpr std::uint64_t kConfig   = 0x1080;  // sec -> NPC base type id (== [sec+0x1098]+8); the one drawn is kNpcCur
inline constexpr std::uint64_t kNpcCur   = 0x1084;  // sec -> NPC current type id (== [sec+0x10A8]+8), -1 = drawn as nothing;
                                                     // never fall back to kConfig
inline constexpr std::uint64_t kActorSize = 0x184;  // sec -> i32 tile size (== NPC type +0x3E0). pos = SW * 512 + size * 256,
                                                     // so floor(pos / 512) = SW + size / 2: no size / 2 correction for x/y
                                                     // or the true tile
inline constexpr std::uint64_t kCombat   = 0x10BC;  // sec -> player combat level
inline constexpr std::uint64_t kPlane    = 0x50;    // sec -> plane (0x40 through 949-5; only verified on plane 0)
inline constexpr std::uint64_t kPosX     = 0x270;   // sec -> fine east
inline constexpr std::uint64_t kPosZ     = 0x274;   // sec -> fine up (height)
inline constexpr std::uint64_t kPosY     = 0x278;   // sec -> fine north
// Movement route (950-1, one class for players and NPCs): sec+0x268 -> route {vtable, begin +0x08, end +0x10
// (== +0x18), read +0x20, write +0x28, i32 count +0x30}, 21 entries of 0x18 {i32 level (-1 cleared), f32 east,
// f32 up, f32 north, i32, i32} in fine units. Steps leave from the front: read stays at begin, count == (write -
// read) / 0x18, and the queue is reset when it drains, so write == begin means stationary (not seen to wrap).
// Entries are half-tile waypoints; the newest is normally tile-centred. A newest entry with x <= 0 is cleared:
// treat as stationary.
inline constexpr std::uint64_t kMoveMgr     = 0x268;  // sec -> movement route object
inline constexpr std::uint64_t kRouteBegin  = 0x08;   // route -> entry array begin
inline constexpr std::uint64_t kRouteEnd    = 0x10;   // route -> entry array end (begin + 21 * stride)
inline constexpr std::uint64_t kRouteRead   = 0x20;   // route -> read cursor (the oldest entry)
inline constexpr std::uint64_t kRouteWrite  = 0x28;   // route -> write cursor (one past the newest entry)
inline constexpr std::uint64_t kRouteCount  = 0x30;   // route -> i32 queued entries
inline constexpr std::uint64_t kRouteStride = 0x18;   // entry size
inline constexpr std::uint64_t kRouteLevel  = 0x00;   // entry -> i32 level, -1 = cleared
inline constexpr std::uint64_t kRouteX      = 0x04;   // entry -> f32 fine east
inline constexpr std::uint64_t kRouteY      = 0x0C;   // entry -> f32 fine north
// Facing: three yaw quaternions {f32 x, y, z, w} from sec+0x1BC: current, turn start +0x1CC, turn target +0x1DC.
// NPCs that face a direction without turning keep identity values in them; the entity node has the heading.
inline constexpr std::uint64_t kFaceTargetY = 0x1E0;  // sec -> f32 y of the turn-target quaternion
inline constexpr std::uint64_t kFaceTargetW = 0x1E8;  // sec -> f32 w of the turn-target quaternion
inline constexpr std::uint64_t kTurnLen     = 0x1EC;  // sec -> f32 turn length
inline constexpr std::uint64_t kTurnDone    = 0x1F0;  // sec -> i32; turning while f32 len > i32 done
inline constexpr std::uint64_t kNodeYaw     = 0xE0;   // entity -> drawn quaternion f32 x, y, z, w (y +0xE4, w +0xEC)
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
