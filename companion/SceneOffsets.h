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

}  // namespace rtx::scn
