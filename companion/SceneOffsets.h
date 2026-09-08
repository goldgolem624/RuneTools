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
inline constexpr std::uint64_t kUid      = 0x88;    // sec -> world uid
inline constexpr std::uint64_t kConfig   = 0x1080;  // sec -> config/model id
inline constexpr std::uint64_t kCombat   = 0x10BC;  // sec -> player combat level
inline constexpr std::uint64_t kPlane    = 0x50;    // sec -> plane (0x40 through 949-5; only verified on plane 0)
inline constexpr std::uint64_t kPosX     = 0x270;   // sec -> fine east
inline constexpr std::uint64_t kPosZ     = 0x274;   // sec -> fine up (height)
inline constexpr std::uint64_t kPosY     = 0x278;   // sec -> fine north
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
