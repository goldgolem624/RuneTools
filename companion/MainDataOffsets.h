#pragma once
// Every MainData (client root) offset the reader and the companion use, in one table. 950-1 grew
// MainData by 0x40 between +0x550 and +0x18D18, so every entry from +0x18D18 up moved +0x40; the
// update check aligns the client's own handler displacements with this table to name what moved.
// `rule` is the calibration rule that re-derives the offset from the client, or null; the md:: ones
// are reported against these values (calib.refs), the others are applied in the reader.
#include <cstdint>

namespace rtx::md {

inline constexpr std::uint32_t kClientClock  = 0x528;     // u32 client cycle, 50 per second
inline constexpr std::uint32_t kFps          = 0x550;     // float frames per second
inline constexpr std::uint32_t kTracker      = 0x19850;   // skill, combat and loot tracker feed
inline constexpr std::uint32_t kChatStore    = 0x19880;   // chat history records
inline constexpr std::uint32_t kClanSettings = 0x19888;   // clan settings var slots
inline constexpr std::uint32_t kMiniMap      = 0x19890;   // minimap: walk destination
inline constexpr std::uint32_t kMapMgr       = 0x19898;   // loaded map: base tile, load progress
inline constexpr std::uint32_t kInputReport  = 0x198B0;   // input reporter: idle stamps
inline constexpr std::uint32_t kWalkMgr      = 0x198D0;   // walk marker appearance
inline constexpr std::uint32_t kArrowMgr     = 0x198F0;   // hint arrows, clue scan registry
inline constexpr std::uint32_t kTrailMgr     = 0x198F8;   // tile trail
inline constexpr std::uint32_t kIfaceOwner   = 0x19900;   // open interface groups
inline constexpr std::uint32_t kVarcStore    = 0x19920;   // varcs, skills, mouse and keys
inline constexpr std::uint32_t kInputProc    = 0x19928;   // hover slot
inline constexpr std::uint32_t kPlayerGroup  = 0x19948;   // player group vars
inline constexpr std::uint32_t kPlayers      = 0x19950;   // player registry by world index
inline constexpr std::uint32_t kProjectiles  = 0x19960;
inline constexpr std::uint32_t kFriends      = 0x19970;
inline constexpr std::uint32_t kGrandExchange = 0x19990;
inline constexpr std::uint32_t kWorld        = 0x199B0;
inline constexpr std::uint32_t kContainers   = 0x199C8;
inline constexpr std::uint32_t kSceneViews   = 0x199D0;
inline constexpr std::uint32_t kCutscene     = 0x19A18;
inline constexpr std::uint32_t kSoundCtx     = 0x19A30;
inline constexpr std::uint32_t kLanguage     = 0x19B10;
inline constexpr std::uint32_t kStatus       = 0x19FA0;   // i32 game state (read as its low byte today), 30 = in the world
inline constexpr std::uint32_t kAccount      = 0x19FA8;   // account and local player data
inline constexpr std::uint32_t kVarpMgr      = 0x19FB8;   // player var store, embedded in the root (its vtable pointer sits here); ability cooldown map
inline constexpr std::uint32_t kVarpHashFromMgr = 0x1C0C8;   // player var store -> its second fixed hash map (the varp values)
inline constexpr std::uint32_t kVarpHash     = kVarpMgr + kVarpHashFromMgr;   // 0x36080: derived, never pinned apart from the store
static_assert(kVarpHash == 0x36080, "the varp hash follows the player var store");
inline constexpr std::uint32_t kOptions      = 0x535D0;   // client options

struct Entry { const char* name; std::uint32_t off; const char* rule; const char* features; };
inline constexpr Entry kTable[] = {
    { "ClientClock",  kClientClock,  "kOffClientClock", "Tick timers, buff timers" },
    { "Fps",          kFps,          nullptr,           "Player State FPS" },
    { "Tracker",      kTracker,      "md::kTracker",      "Trackers (unsurfaced)" },
    { "ChatStore",    kChatStore,    "md::kChatStore",    "Chat log" },
    { "ClanSettings", kClanSettings, "md::kClanSettings", "Clan vars" },
    { "MiniMap",      kMiniMap,      nullptr,           "Walk destination" },
    { "MapMgr",       kMapMgr,       "md::kMapMgr",       "Map base, loading, zone events" },
    { "InputReport",  kInputReport,  "md::kInputReport",  "Idle timer" },
    { "WalkMgr",      kWalkMgr,      nullptr,           "Walk marker" },
    { "ArrowMgr",     kArrowMgr,     nullptr,           "Engine markers, clue scan" },
    { "TrailMgr",     kTrailMgr,     nullptr,           "Engine markers" },
    { "IfaceOwner",   kIfaceOwner,   "md::kIfaceOwner",   "Interfaces, panel positions" },
    { "VarcStore",    kVarcStore,    "kOffVarcStore",   "Varcs, skills, mouse, keys" },
    { "InputProc",    kInputProc,    "md::kInputProc",    "Hover" },
    { "PlayerGroup",  kPlayerGroup,  "md::kPlayerGroup",  "Group vars" },
    { "Players",      kPlayers,      "md::kPlayers",      "Local player, in-frame labels" },
    { "Projectiles",  kProjectiles,  nullptr,           "Projectiles" },
    { "Friends",      kFriends,      "md::kFriends",      "Friends" },
    { "GrandExchange", kGrandExchange, "kOffGE",        "Grand Exchange" },
    { "World",        kWorld,        "kOffWorld",       "World number" },
    { "Containers",   kContainers,   "md::kContainers",   "Backpack, bank, equipment" },
    { "SceneViews",   kSceneViews,   "md::kSceneViews",   "Scene, overlays, camera" },
    { "Cutscene",     kCutscene,     "md::kCutscene",     "Cutscene state" },
    { "SoundCtx",     kSoundCtx,     "md::kSoundCtx",     "Sounds" },
    { "Language",     kLanguage,     "md::kLanguage",     "Menu swaps" },
    { "Status",       kStatus,       "kOffStatus",      "Login state" },
    { "Account",      kAccount,      "kOffAccount",     "Account, local player id" },
    { "VarpMgr",      kVarpMgr,      "md::kVarpMgr",      "Ability cooldowns" },
    { "VarpHash",     kVarpHash,     nullptr,           "Player variables" },
    { "Options",      kOptions,      "md::kOptions",      "Client options" },
};

}  // namespace rtx::md
