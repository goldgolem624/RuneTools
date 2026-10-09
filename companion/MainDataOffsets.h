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
inline constexpr std::uint32_t kConfigs      = 0x18D68;   // config manager: state i32 +0x3C, 4 once the config lists are loaded
inline constexpr std::uint32_t kDisplay      = 0x18D80;   // display: width +0x68, height +0x6C, window at +0x6238 (insets)
inline constexpr std::uint32_t kTracker      = 0x19850;   // skill, combat and loot tracker feed
inline constexpr std::uint32_t kChatStore    = 0x19880;   // chat store: next uid +0x18, index tree +0x20, records map +0x878
inline constexpr std::uint32_t kClanSettings = 0x19888;   // clan manager: listened settings handle +0x60 (store object +0x68, its var map +0x98)
inline constexpr std::uint32_t kMiniMap      = 0x19890;   // minimap: walk destination
inline constexpr std::uint32_t kMapMgr       = 0x19898;   // loaded map: base tile, load progress
inline constexpr std::uint32_t kInputReport  = 0x198B0;   // input reporter: idle stamps
inline constexpr std::uint32_t kConnection   = 0x198B8;   // server connection: root back pointer +0x8, opcode +0x2C, length +0x30, logout reason +0x50
inline constexpr std::uint32_t kVarWatch     = 0x198C8;   // var change watch: change counter u32 +0x10, changed flag +0x14
inline constexpr std::uint32_t kWalkMgr      = 0x198D0;   // walk marker appearance
inline constexpr std::uint32_t kFriendsChat  = 0x198E0;   // friends chat channel: name +0x8, owner +0x20, rank +0x38, min kick +0x3C, users vector +0x40 (0x50 each)
inline constexpr std::uint32_t kArrowMgr     = 0x198F0;   // hint arrows, clue scan registry
inline constexpr std::uint32_t kTrailMgr     = 0x198F8;   // tile trail
inline constexpr std::uint32_t kIfaceOwner   = 0x19900;   // open interface groups
inline constexpr std::uint32_t kLoginMgr     = 0x19910;   // login manager: state +0x10, root back pointer +0x18, login reply +0x16C, lobby reply +0x1B8
inline constexpr std::uint32_t kVarcStore    = 0x19920;   // input state: modifier keys +0x4F0, mouse +0x46D8, skills +0x7618, client var store +0x7620 (map +0x7630), clan var store +0x77B0
inline constexpr std::uint32_t kInputProc    = 0x19928;   // minimenu: open byte +0x68, entries +0x90, hover record slots +0x13E0..
inline constexpr std::uint32_t kPlayerGroup  = 0x19948;   // player group vars
inline constexpr std::uint32_t kPlayers      = 0x19950;   // player registry by world index
inline constexpr std::uint32_t kProjectiles  = 0x19960;
inline constexpr std::uint32_t kFriends      = 0x19970;
inline constexpr std::uint32_t kGrandExchange = 0x19990;
inline constexpr std::uint32_t kWorld        = 0x199B0;
inline constexpr std::uint32_t kContainers   = 0x199C8;
inline constexpr std::uint32_t kSceneViews   = 0x199D0;   // game world list: worlds vector +0x58, current index +0x70
inline constexpr std::uint32_t kWorldMapMgr  = 0x199E8;   // world map manager: the open world map at +0x10 (null while closed)
inline constexpr std::uint32_t kCutscene     = 0x19A18;
inline constexpr std::uint32_t kSoundCtx     = 0x19A30;
inline constexpr std::uint32_t kServerTimeOffset = 0x19A48;   // i64 server time offset in ms (the runeday ops divide it)
inline constexpr std::uint32_t kCountry      = 0x19AC4;   // i32 country code
inline constexpr std::uint32_t kLanguage     = 0x19B10;
inline constexpr std::uint32_t kStatus       = 0x19FA0;   // i32 game state, 30 = in the world; the engine compares the whole dword
                                                          // (20 lobby, 23, 40 menu), readers take its low byte and the upper three are zero
inline constexpr std::uint32_t kAccount      = 0x19FA8;   // account and local player data
inline constexpr std::uint32_t kVarpMgr      = 0x19FB8;   // player var store, embedded in the root (its vtable pointer sits here): values map +0x1C0C8,
                                                          // per-var set stamps +0x38170 (var id -> engine clock + 500 ms when a script sets it)
inline constexpr std::uint32_t kVarpHashFromMgr = 0x1C0C8;   // player var store -> its second fixed hash map (the varp values)
inline constexpr std::uint32_t kVarpHash     = kVarpMgr + kVarpHashFromMgr;   // 0x36080: derived, never pinned apart from the store
static_assert(kVarpHash == 0x36080, "the varp hash follows the player var store");
inline constexpr std::uint32_t kWorldInfo    = 0x535C8;   // world info: quick chat u8 +0x8, system update countdown ticks i32 +0xC, members world u8 +0x10
inline constexpr std::uint32_t kOptions      = 0x535D0;   // client options

struct Entry { const char* name; std::uint32_t off; const char* rule; const char* features; };
inline constexpr Entry kTable[] = {
    { "ClientClock",  kClientClock,  "kOffClientClock", "Tick timers, buff timers" },
    { "Fps",          kFps,          nullptr,           "Player State FPS" },
    { "Configs",      kConfigs,      "md::kConfigs",      "Update check" },
    { "Display",      kDisplay,      "md::kDisplay",      "Update check" },
    { "Tracker",      kTracker,      "md::kTracker",      "Trackers (unsurfaced)" },
    { "ChatStore",    kChatStore,    "md::kChatStore",    "Chat log" },
    { "ClanSettings", kClanSettings, "md::kClanSettings", "Clan vars" },
    { "MiniMap",      kMiniMap,      nullptr,           "Walk destination" },
    { "MapMgr",       kMapMgr,       "md::kMapMgr",       "Map base, loading, zone events" },
    { "InputReport",  kInputReport,  "md::kInputReport",  "Idle timer" },
    { "Connection",   kConnection,   "md::kConnection",   "Packet events" },
    { "VarWatch",     kVarWatch,     nullptr,           "Update check" },   // the var ops read it past the rule window
    { "WalkMgr",      kWalkMgr,      nullptr,           "Walk marker" },
    { "FriendsChat",  kFriendsChat,  "md::kFriendsChat",  "Update check" },
    { "ArrowMgr",     kArrowMgr,     nullptr,           "Engine markers, clue scan" },
    { "TrailMgr",     kTrailMgr,     nullptr,           "Engine markers" },
    { "IfaceOwner",   kIfaceOwner,   "md::kIfaceOwner",   "Interfaces, panel positions" },
    { "LoginMgr",     kLoginMgr,     "md::kLoginMgr",     "Update check" },
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
    { "WorldMapMgr",  kWorldMapMgr,  "md::kWorldMapMgr",  "Update check" },
    { "Cutscene",     kCutscene,     "md::kCutscene",     "Cutscene state" },
    { "SoundCtx",     kSoundCtx,     "md::kSoundCtx",     "Sounds" },
    { "ServerTimeOffset", kServerTimeOffset, "md::kServerTimeOffset", "Update check" },
    { "Country",      kCountry,      "md::kCountry",      "Update check" },
    { "Language",     kLanguage,     "md::kLanguage",     "Menu swaps" },
    { "Status",       kStatus,       "kOffStatus",      "Login state" },
    { "Account",      kAccount,      "kOffAccount",     "Account, local player id" },
    { "VarpMgr",      kVarpMgr,      "md::kVarpMgr",      "Player variables, var set stamps" },
    { "VarpHash",     kVarpHash,     nullptr,           "Player variables" },
    { "WorldInfo",    kWorldInfo,    "md::kWorldInfo",    "Update check" },
    { "Options",      kOptions,      "md::kOptions",      "Client options" },
};

}  // namespace rtx::md
