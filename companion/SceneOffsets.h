#pragma once
//
// RS3 client memory offsets shared by the external reader (src/reader/Reader.cpp)
// and the in-process companion (companion/SceneData.cpp); both walk the same
// root -> worldView -> scene-worker -> entity chain, so both move together when an
// offset changes here. The worker/matrix entries are defaults only: they are
// runtime-resolved and rescanned when invalid.

#include <cstdint>

namespace rtx::scn {

// root (MainData) -> worldView chain
inline constexpr std::uint64_t kContainer  = 0x19990;  // root -> worldView container
inline constexpr std::uint64_t kActiveIdx  = 0x70;     // container -> active entry index
inline constexpr std::uint64_t kEntryArr   = 0x58;     // container -> entry array (stride 0x10)
inline constexpr std::uint64_t kEntryWv    = 0x8;      // entry -> worldView object
inline constexpr std::uint64_t kPlayerData = 0x19F68;  // root -> local-player data block
inline constexpr std::uint64_t kLocalUid   = 0x48;     // data -> local world index

// worldView -> scene worker / view-projection matrix (runtime-resolved defaults)
inline constexpr std::uint32_t kWorkerOffDefault = 0x10170;
inline constexpr std::uint32_t kMatrixOffDefault = 0x13090;
inline constexpr std::int32_t  kCamPosRelDefault = 0x80;     // campos rel. to matrix (e,u,n)

// scene worker entity vector + entity/sec fields
inline constexpr std::uint64_t kVecBegin = 0x138;   // worker -> entity vector begin
inline constexpr std::uint64_t kVecEnd   = 0x140;   // worker -> entity vector end
inline constexpr std::uint64_t kSecPtr   = 0x1A0;   // entity -> object-data (sec/sub)
inline constexpr std::uint64_t kType     = 0x10;    // sec -> type byte
inline constexpr std::uint64_t kName     = 0xB8;    // sec -> live name
inline constexpr std::uint64_t kUid      = 0x88;    // sec -> world uid
inline constexpr std::uint64_t kConfig   = 0x1080;  // sec -> config/model id
inline constexpr std::uint64_t kCombat   = 0x10BC;  // sec -> player combat level
inline constexpr std::uint64_t kPlane    = 0x40;    // sec -> plane
inline constexpr std::uint64_t kPosX     = 0x270;   // sec -> fine east
inline constexpr std::uint64_t kPosZ     = 0x274;   // sec -> fine up (height)
inline constexpr std::uint64_t kPosY     = 0x278;   // sec -> fine north
// The three above are the PLAYER/NPC layout (type 1/2) only. Types 4 and 13 are much
// smaller objects and have their own position fields; reading kPos* off them runs past
// the end of the allocation into neighbouring heap. Sizes and fields, from the ctors:
inline constexpr std::uint64_t kT4Size   = 0x1B0;   // type-4 (graphic highlight) object size
inline constexpr std::uint64_t kT4Gfx    = 0x74;    // type-4 -> graphic id (int)
inline constexpr std::uint64_t kT4PosE   = 0x78;    // type-4 -> fine east  (int32)
inline constexpr std::uint64_t kT4PosU   = 0x7C;    // type-4 -> fine up    (int32)
inline constexpr std::uint64_t kT4PosN   = 0x80;    // type-4 -> fine north (int32)
inline constexpr std::uint64_t kT13Size  = 0xF8;    // type-13 (world marker) object size

// ---- walk destination + walk-marker appearance -----------------------------------------
// The click-to-walk marker is a type-13 entity, but its DESTINATION need not be hunted in
// the scene at all: the click handler at 0xE1A30 writes it straight onto ClientMiniMap
// before it sends the move packet or spawns the marker:
//     mov rax,[rcx+0x19850]        ; ClientMiniMap
//     mov [rax+0x14], edx          ; destination X
//     mov [rax+0x18], r8d          ; destination Y
//     mov byte [rax+0x1c], 0       ; 0 = world click, 1 = minimap click
// FINE units (tile = value / 512): the same ints are converted to float for the marker,
// and the type-13 position setter derives its chunk with `sar 0xf` (fine>>15 == tile>>6).
inline constexpr std::uint64_t kMiniMap  = 0x19850; // root -> ClientMiniMap (pointer)
inline constexpr std::uint64_t kDestX    = 0x14;    // minimap -> i32 destination fine X
inline constexpr std::uint64_t kDestY    = 0x18;    // minimap -> i32 destination fine Y
inline constexpr std::uint64_t kDestSrc  = 0x1C;    // minimap -> u8 0 = world click, 1 = minimap
//
// SEPARATE object, do not confuse the two: root+0x19890 is the walk-marker APPEARANCE
// manager. CS2 op SETWALKMARKER (scrambled 2222) writes it via 0x14019f2b0, and the only
// call site in the whole dump is script4175: SETWALKMARKER(6969, 140162) to set a custom
// marker, SETWALKMARKER(-1, -1) to revert. So +0x68/+0x6C are RESOURCE IDS (-1 = use the
// default at +0x70/+0x78), NOT coordinates. Byte +0x10 gates marker creation entirely,
// i.e. the in-game "walk marker" setting; the click handler skips the spawn when it is 0.
inline constexpr std::uint64_t kWalkMgr  = 0x19890; // root -> walk-marker appearance manager
inline constexpr std::uint64_t kWalkOn   = 0x10;    // manager -> u8, 0 = marker disabled
inline constexpr std::uint64_t kWalkResX = 0x68;    // manager -> i32 custom resource id (-1 = default)
inline constexpr std::uint64_t kWalkResY = 0x6C;    // manager -> i32 custom resource id (-1 = default)

}  // namespace rtx::scn
