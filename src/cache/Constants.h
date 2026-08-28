#pragma once

#include <string>

namespace rtx::cache {

// RS3 NXT writes its cache as `<root>/js5-<index>.jcache` (SQLite); this is
// the official launcher's default path.
constexpr const char* kDefaultCacheRoot = "C:/ProgramData/Jagex/RuneScape";

// Indexes we read.
constexpr int kIndexConfigs    = 2;
constexpr int kIndexInterfaces = 3;    // interface component defs: archive = group, file = component
constexpr int kIndexMaps       = 5;    // MAPSV2: per-region object placements + tiles
constexpr int kIndexSprites    = 8;
constexpr int kIndexClientScript = 12;
constexpr int kIndexLocations  = 16;
constexpr int kIndexEnums      = 17;
constexpr int kIndexNpcs       = 18;
constexpr int kIndexItems      = 19;   // archive = id >> 8, file = id & 0xff
constexpr int kIndexSeqs       = 20;
constexpr int kIndexStructs    = 22;
constexpr int kIndexWorldMap   = 23;   // world-map areas: archive 0 details, 1 composite zones, 2 thumb PNG, 4 full PNG (file = area id)
constexpr int kIndexAchievements = 57;  // archive = id >> 7, file = id & 0x7f
// Audio. Both hold JAGA-wrapped Ogg Vorbis, one stream per archive, and the ARCHIVE ID IS THE
// SOUND ID - the ids CS2 passes to SOUND_SYNTH / SOUND_VORBIS_VOLUME resolve directly here
// (verified: 9705, 31334, 35046, 41269, 48065, 48233 all present in index 14).
constexpr int kIndexSoundEffects = 14;  // 7427 archives, mono 22050 Hz
constexpr int kIndexMusic        = 40;  // 1095 archives, stereo 22050/44100 Hz

// Default files-per-archive when the reference table doesn't carry an
// explicit count. 256 covers items/locs; 128 covers npcs/seqs.
constexpr int kDefaultFilesPerArchive_Items = 256;
constexpr int kDefaultFilesPerArchive_Locations = 256;
constexpr int kDefaultFilesPerArchive_Npcs = 128;

}  // namespace rtx::cache
