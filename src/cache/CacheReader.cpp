#include "CacheReader.h"

#include "Achievements.h"
#include "Constants.h"
#include "CachePath.h"   // Jagex / Steam / moved-cache resolution
#include "ItemType.h"
#include "JagexContainer.h"   // Decompress (NXT "ZL" wrapper) for the raw audio archives
#include "LocationType.h"
#include "MapLocations.h"
#include "MapTiles.h"
#include "NpcType.h"
#include "Sprite.h"
#include "Store.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Windows.h>
#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")

namespace rtx::cache {

namespace {

std::mutex                            g_mu;
std::unique_ptr<Store>                g_store;
std::unordered_map<int, ItemInfo>     g_item_cache;
std::unordered_map<int, std::string>  g_sprite_cache;   // id -> data URL
std::unordered_map<long long, std::string> g_sprite_scaled_cache;   // (id<<16|px) -> data URL
std::unordered_map<int, NpcMeta>      g_npc_cache;
std::unordered_map<int, LocMeta>      g_loc_cache;
std::unordered_map<int, std::vector<LocPlacement>> g_region_cache;  // key = rx<<8 | ry
std::unordered_map<int, std::string>  g_perk_names;     // DBRows perk id -> name
std::unordered_map<int, std::string>  g_perk_descs;     // DBRows perk id -> effect description (col 4)
bool                                  g_perks_loaded   = false;
std::unordered_map<int, std::string>  g_buff_names;     // buff-bar sprite/item id -> name
std::unordered_map<int, std::string>  g_debuff_names;   // debuff-bar sprite/item id -> name
std::unordered_map<int, int>          g_buff_kind;      // buff id -> kind bits (timer/count/pct/custom)
std::unordered_map<int, bool>         g_buff_icon_item; // buff-bar icon id -> true if it's an item id (else a sprite id)
bool                                  g_buffs_loaded   = false;
bool                                  g_init_attempted = false;

// Collision: loc clip info + per-region unwalkable grids, both memoized.
struct LocClip { bool no_clip = false; int dim_x = 1, dim_y = 1; };
std::unordered_map<int, LocClip>      g_locclip_cache;
std::unordered_map<int, std::vector<std::uint8_t>> g_blocked_cache;  // key = rx<<8 | ry
std::unordered_map<int, MapTileData>  g_tiles_cache;                 // key = rx<<8 | ry

// World-map scene icons: a loc with a mapscene (loc opcode 102) draws a map icon instead of a wall.
// mapscene id -> sprite via the MAPSCENES config (index 2, archive 34, opcode 1 = sprite_id). Memoized.
struct MapsceneIcon { int w = 0, h = 0; bool tried = false; std::vector<std::uint8_t> rgba; };
std::unordered_map<int, int>          g_mapscene_sprite;   // mapscene id -> sprite id
std::unordered_map<int, MapsceneIcon> g_mapscene_px;       // mapscene id -> decoded RGBA icon
std::unordered_map<int, int>          g_loc_mapscene;      // loc id -> mapscene id (-1 = none)
bool                                  g_mapscenes_loaded = false;

// World-map labels: a loc with a mapFunction (loc opcode 0x6B/107) draws a worldmap label icon
// (bank/altar/teleport/...). mapFunction id -> sprite via the MAPLABELS config (index 2, archive 36,
// opcode 1 = sprite). Memoized.
struct MaplabelDef { int sprite = -1; int category = -1; std::string text; };
std::unordered_map<int, MaplabelDef>  g_maplabel_def;       // maplabel id -> {sprite, category, text}
std::unordered_map<int, MapsceneIcon> g_maplabel_px;        // maplabel id -> decoded RGBA icon
std::unordered_map<int, int>          g_loc_mapfunc;        // loc id -> mapFunction (maplabel) id (-1 = none)
bool                                  g_maplabels_loaded = false;

void EnsureInit() {
    if (g_init_attempted) return;
    g_init_attempted = true;
    g_store = std::make_unique<Store>(ResolveCacheRoot());
    g_store->Add(kIndexItems,     kDefaultFilesPerArchive_Items);
    g_store->Add(kIndexNpcs,      kDefaultFilesPerArchive_Npcs);
    g_store->Add(kIndexLocations, kDefaultFilesPerArchive_Locations);
    g_store->Add(kIndexMaps,      0);   // region archives; file count from ref table
    g_store->Add(kIndexSprites,   0);   // one file per archive
    g_store->Add(kIndexConfigs,   0);   // DBRows (archive 41) for augment perk names
    g_store->Add(kIndexStructs,   0);   // structs (buff/debuff names)
    g_store->Add(kIndexEnums,     0);   // enums (id->name rosters: slayer creatures, reaper bosses)
    g_store->Add(kIndexAchievements, 0); // achievement defs (varbit-driven completion)
    g_store->Add(kIndexInterfaces, 0);  // static interface-component defs
    g_store->Add(kIndexClientScript, 0); // CS2 scripts (script 6506 = ability cooldown varc pairs)
    g_store->Add(kIndexSoundEffects, 0); // JAGA-wrapped Ogg Vorbis, archive id = sound id
    g_store->Add(kIndexMusic,        0); // ditto, music streams
}

// ---------------------------------------------------------------------------
// Audio (js5-14 sound effects, js5-40 music).
//
// Archives are JAGA containers wrapping ordinary Ogg Vorbis. Header, big-endian:
//     +0   'JAGA'
//     +4   u32  0
//     +8   u32  total samples
//     +12  u32  sample rate        22050 (effects) / 22050-44100 (music)
//     +16  u32  channels           1 = effects, 2 = music
//     +20  u32  chunk count N
//     +24  N x (u32 length, u32 offset)
//     +24+8N  the Ogg stream(s)
// Verified against archive length on index 14 (header + chunk length == archive size exactly)
// and by the Ogg being valid Vorbis once the header is skipped.
// ---------------------------------------------------------------------------
namespace {

struct JagaInfo {
    bool ok = false;
    std::uint32_t samples = 0, rate = 0, channels = 0, chunks = 0;
    std::size_t   ogg_off = 0;               // where the Ogg data starts
    std::vector<std::uint32_t> lens;         // per-chunk byte length, in order
};

std::uint32_t be32(const std::uint8_t* p) {
    return ((std::uint32_t)p[0] << 24) | ((std::uint32_t)p[1] << 16) |
           ((std::uint32_t)p[2] << 8) | p[3];
}

JagaInfo ParseJaga(const std::vector<std::uint8_t>& raw) {
    JagaInfo j;
    if (raw.size() < 24 || raw[0] != 'J' || raw[1] != 'A' || raw[2] != 'G' || raw[3] != 'A')
        return j;
    j.samples  = be32(&raw[8]);
    j.rate     = be32(&raw[12]);
    j.channels = be32(&raw[16]);
    j.chunks   = be32(&raw[20]);
    if (j.chunks > 4096) return j;                       // implausible: treat as corrupt
    j.ogg_off = 24 + (std::size_t)j.chunks * 8;
    if (j.ogg_off + 4 > raw.size()) return j;
    j.lens.reserve(j.chunks);
    for (std::uint32_t c = 0; c < j.chunks; ++c)
        j.lens.push_back(be32(&raw[24 + (std::size_t)c * 8]));   // (length, offset) pairs
    if (std::memcmp(&raw[j.ogg_off], "OggS", 4) != 0) return j;
    j.ok = true;
    return j;
}

}  // namespace


std::string base64(const std::vector<std::uint8_t>& bytes) {
    DWORD n = 0;
    if (!CryptBinaryToStringA(bytes.data(), (DWORD)bytes.size(),
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &n)) return {};
    std::string out((std::size_t)n, '\0');
    if (!CryptBinaryToStringA(bytes.data(), (DWORD)bytes.size(),
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, out.data(), &n)) return {};
    if (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

ItemInfo ResolveLocked(int item_id) {
    auto hit = g_item_cache.find(item_id);
    if (hit != g_item_cache.end()) return hit->second;

    ItemInfo info;
    auto* index = g_store ? g_store->Get(kIndexItems) : nullptr;
    if (index) {
        auto bytes = index->ReadFile(item_id >> 8, item_id & 0xff);
        if (!bytes.empty()) {
            ItemDef def = DecodeItem(item_id, std::move(bytes));
            info.name      = def.name;
            info.ge_limit  = def.ge_limit;
            info.value     = def.value;
            info.augmented = def.augmented;

            if (def.noted && def.noted_unnoted > 0) {
                auto pb = index->ReadFile(def.noted_unnoted >> 8,
                                          def.noted_unnoted & 0xff);
                if (!pb.empty()) {
                    ItemDef parent = DecodeItem(def.noted_unnoted, std::move(pb));
                    if (!parent.name.empty()) info.name = parent.name + " (noted)";
                    if (info.ge_limit < 0) info.ge_limit = parent.ge_limit;
                    if (info.value    < 0) info.value    = parent.value;
                }
            }
        }
    }
    g_item_cache[item_id] = info;
    return info;
}

// Loc clip (no_clip + footprint). Assumes g_mu held.
LocClip LocClipLocked(int loc_id) {
    auto hit = g_locclip_cache.find(loc_id);
    if (hit != g_locclip_cache.end()) return hit->second;
    LocClip clip;
    auto* index = g_store ? g_store->Get(kIndexLocations) : nullptr;
    if (index) {
        auto bytes = index->ReadFile(loc_id >> 8, loc_id & 0xff);
        if (!bytes.empty()) {
            LocDef def = DecodeLoc(loc_id, std::move(bytes));
            clip.no_clip = def.no_clip;
            clip.dim_x = def.dim_x;
            clip.dim_y = def.dim_y;
        }
    }
    g_locclip_cache[loc_id] = clip;
    return clip;
}

// Load the MAPSCENES config (index 2, archive 34): mapscene id -> sprite id. Assumes g_mu held.
void EnsureMapscenesLocked() {
    if (g_mapscenes_loaded) return;
    auto* cfg = g_store ? g_store->Get(kIndexConfigs) : nullptr;
    if (!cfg || !cfg->ready()) return;                 // cache not up yet -> retry on the next render
    g_mapscenes_loaded = true;
    constexpr int kMapscenesArchive = 34;
    const auto& entries = cfg->ref().entries();
    if ((int)entries.size() <= kMapscenesArchive) return;
    for (int fid : entries[kMapscenesArchive].valid_file_ids) {
        auto bytes = cfg->ReadFile(kMapscenesArchive, fid);
        if (bytes.empty()) continue;
        InputStream s(std::move(bytes));
        int sprite = -1;
        while (s.remaining() > 0) {
            int op = s.ReadUnsignedByte();
            if (op == 0) break;
            if (op == 1) sprite = (int)s.ReadBigSmart();   // sprite_id (variable unsigned int)
            else if (op == 2) s.ReadInt();
            else if (op >= 3 && op <= 5) { /* bool flag, no payload */ }
            else break;                                    // unknown opcode -> stop (alignment lost)
        }
        if (sprite >= 0) g_mapscene_sprite[fid] = sprite;
    }
}

// Decoded RGBA icon for a mapscene id (lazy; failures cached as empty). Assumes g_mu held.
const MapsceneIcon& MapsceneIconLocked(int mapscene_id) {
    auto& ic = g_mapscene_px[mapscene_id];
    if (ic.tried) return ic;
    ic.tried = true;
    EnsureMapscenesLocked();
    auto it = g_mapscene_sprite.find(mapscene_id);
    if (it == g_mapscene_sprite.end()) return ic;
    auto* sidx = g_store ? g_store->Get(kIndexSprites) : nullptr;
    if (!sidx) return ic;
    int w = 0, h = 0;
    auto rgba = SpriteRawRgba(*sidx, it->second, w, h);
    if (!rgba.empty() && w > 0 && h > 0) { ic.w = w; ic.h = h; ic.rgba = std::move(rgba); }
    return ic;
}

// loc id -> mapscene id (-1 = none); memoized. ALSO caches the loc's mapFunction in one decode. Assumes g_mu held.
int LocMapsceneLocked(int loc_id) {
    auto hit = g_loc_mapscene.find(loc_id);
    if (hit != g_loc_mapscene.end()) return hit->second;
    int ms = -1, mf = -1;
    auto* index = g_store ? g_store->Get(kIndexLocations) : nullptr;
    if (index) {
        auto bytes = index->ReadFile(loc_id >> 8, loc_id & 0xff);
        if (!bytes.empty()) { LocDef def = DecodeLoc(loc_id, std::move(bytes)); ms = def.mapscene; mf = def.mapFunction; }
    }
    g_loc_mapscene[loc_id] = ms;
    g_loc_mapfunc[loc_id] = mf;
    return ms;
}
// loc id -> mapFunction (worldmap maplabel) id (-1 = none); shares LocMapsceneLocked's decode. Assumes g_mu held.
int LocMapFunctionLocked(int loc_id) {
    auto hit = g_loc_mapfunc.find(loc_id);
    if (hit != g_loc_mapfunc.end()) return hit->second;
    LocMapsceneLocked(loc_id);                               // populates both g_loc_mapscene + g_loc_mapfunc
    auto it = g_loc_mapfunc.find(loc_id);
    return it != g_loc_mapfunc.end() ? it->second : -1;
}
// MAPLABELS config (index 2 / archive 36): maplabel id -> {sprite, category, text}. Opcode table
// per the game. Assumes g_mu held.
void EnsureMaplabelsLocked() {
    if (g_maplabels_loaded) return;
    auto* cfg = g_store ? g_store->Get(kIndexConfigs) : nullptr;
    if (!cfg || !cfg->ready()) return;                       // cache not up yet -> retry next render
    g_maplabels_loaded = true;
    constexpr int kMaplabelsArchive = 36;
    const auto& entries = cfg->ref().entries();
    if ((int)entries.size() <= kMaplabelsArchive) return;
    for (int fid : entries[kMaplabelsArchive].valid_file_ids) {
        auto bytes = cfg->ReadFile(kMaplabelsArchive, fid);
        if (bytes.empty()) continue;
        InputStream s(std::move(bytes));
        MaplabelDef def;
        bool stop = false;
        while (!stop && s.remaining() > 0) {
            int op = s.ReadUnsignedByte();
            if (op == 0) break;
            switch (op) {
                case 0x01: def.sprite = s.ReadBigSmart(); break;             // icon sprite
                case 0x02: s.ReadBigSmart(); break;                          // sprite_hover
                case 0x03: def.text = s.ReadString(); break;                 // label text
                case 0x04: case 0x05: s.skip(3); break;                      // colour tuples
                case 0x06: case 0x07: case 0x08: case 0x1c: case 0x1e: s.skip(1); break;
                case 0x09: case 0x14: s.skip(12); break;                     // toggle struct (ushort+ushort+uint+uint)
                case 0x0a: case 0x0b: case 0x11: s.ReadString(); break;      // rightclick / unktext
                case 0x0f: { int pc = s.ReadUnsignedByte(); s.skip(pc * 4); s.skip(4); s.skip(1); s.skip(4); s.skip(pc); break; }  // polygon
                case 0x13: def.category = s.ReadUnsignedShort(); break;      // category
                case 0x15: case 0x16: s.skip(4); break;
                case 0x19: s.ReadBigSmart(); break;                          // background_sprite
                case 0x1a: s.skip(9); break;                                 // legacy_switch
                case 0xF9: { int n = s.ReadUnsignedByte(); for (int i = 0; i < n; ++i) { bool str = s.ReadUnsignedByte() == 1; s.Read24BitInt(); if (str) s.ReadString(); else s.ReadInt(); } break; }
                default: stop = true; break;                                 // unknown -> stop (alignment lost)
            }
        }
        if (def.sprite >= 0 || !def.text.empty()) g_maplabel_def[fid] = std::move(def);
    }
}
// Decoded RGBA icon for a maplabel id (lazy; failures cached empty). Assumes g_mu held.
const MapsceneIcon& MaplabelIconLocked(int maplabel_id) {
    auto& ic = g_maplabel_px[maplabel_id];
    if (ic.tried) return ic;
    ic.tried = true;
    EnsureMaplabelsLocked();
    auto it = g_maplabel_def.find(maplabel_id);
    if (it == g_maplabel_def.end() || it->second.sprite < 0) return ic;
    auto* sidx = g_store ? g_store->Get(kIndexSprites) : nullptr;
    if (!sidx) return ic;
    int w = 0, h = 0;
    auto rgba = SpriteRawRgba(*sidx, it->second.sprite, w, h);
    if (!rgba.empty() && w > 0 && h > 0) { ic.w = w; ic.h = h; ic.rgba = std::move(rgba); }
    return ic;
}

// Decoded land tiles (file 3) for a region. Assumes g_mu held.
const MapTileData& RegionTilesLocked(int rx, int ry) {
    int key = (rx << 8) | ry;
    auto hit = g_tiles_cache.find(key);
    if (hit != g_tiles_cache.end()) return hit->second;
    auto* index = g_store ? g_store->Get(kIndexMaps) : nullptr;
    // DecodeMapTiles always returns fully-sized arrays (sentinel heights for
    // absent tiles), so an empty input still yields a valid all-default grid.
    MapTileData td = DecodeMapTiles(index ? index->ReadFile(rx | (ry << 7), 3)
                                          : std::vector<std::uint8_t>{});
    g_tiles_cache[key] = std::move(td);
    return g_tiles_cache[key];
}

// Per-region unwalkable grid (4*64*64, 1 = blocked). Assumes g_mu held.
const std::vector<std::uint8_t>& RegionBlockedGridLocked(int rx, int ry) {
    int key = (rx << 8) | ry;
    auto hit = g_blocked_cache.find(key);
    if (hit != g_blocked_cache.end()) return hit->second;

    std::vector<std::uint8_t> blk(4 * 64 * 64, 0);
    auto* index = g_store ? g_store->Get(kIndexMaps) : nullptr;
    if (!index) { g_blocked_cache[key] = std::move(blk); return g_blocked_cache[key]; }

    int archive = rx | (ry << 7);
    auto orFlag = [&](int plane, int x, int y, std::uint8_t bits) {
        if (x >= 0 && x < 64 && y >= 0 && y < 64 && plane >= 0 && plane < 4)
            blk[(plane * 64 + x) * 64 + y] |= bits;
    };

    // Bridge tiles: settings bit 0x2 on PLANE 1 marks the column as a bridge.
    // Plane z content collides at plane z-1, and the void flag on the plane
    // below (under the deck) is dropped.
    const auto& settings = RegionTilesLocked(rx, ry).settings;
    auto isBridge = [&](int x, int y) {
        return (settings[(std::size_t)(64 + x) * 64 + y] & 0x2) != 0;
    };
    // Void tiles (file 3): full-blocked at the plane the flag effectively targets.
    for (int z = 0; z < 4; ++z)
        for (int x = 0; x < 64; ++x)
            for (int y = 0; y < 64; ++y) {
                if (!(settings[(std::size_t)(z * 64 + x) * 64 + y] & 0x1)) continue;
                orFlag(isBridge(x, y) ? z - 1 : z, x, y, kTileBlockFull);
            }
    // Object placements (land file 0 + water file 1), plane-shifted on bridges.
    for (int file : {0, 1}) {
        auto bytes = index->ReadFile(archive, file);
        if (bytes.empty()) continue;
        for (const auto& p : DecodeMapLocations(std::move(bytes))) {
            const int pl = isBridge(p.x, p.y) ? p.plane - 1 : p.plane;
            if (pl < 0) continue;
            if (p.type == 9) {                                // diagonal wall: full block
                if (!LocClipLocked(p.id).no_clip) orFlag(pl, p.x, p.y, kTileBlockFull);
            } else if (p.type == 0) {                         // straight wall: one blocked edge
                if (LocClipLocked(p.id).no_clip) continue;
                std::uint8_t e = p.rotation == 0 ? kTileBlockW : p.rotation == 1 ? kTileBlockN
                               : p.rotation == 2 ? kTileBlockE : kTileBlockS;
                orFlag(pl, p.x, p.y, e | kTileHasWall);
            } else if (p.type == 2) {                         // corner wall: two blocked edges
                if (LocClipLocked(p.id).no_clip) continue;
                std::uint8_t e = p.rotation == 0 ? (kTileBlockN | kTileBlockW)
                               : p.rotation == 1 ? (kTileBlockN | kTileBlockE)
                               : p.rotation == 2 ? (kTileBlockS | kTileBlockE)
                                                 : (kTileBlockS | kTileBlockW);
                orFlag(pl, p.x, p.y, e | kTileHasWall);
            } else if (p.type == 10 || p.type == 11) {        // scenery: full-block footprint if it clips
                LocClip c = LocClipLocked(p.id);
                if (c.no_clip) continue;
                int dx = c.dim_x, dy = c.dim_y;
                if (p.rotation == 1 || p.rotation == 3) { int t = dx; dx = dy; dy = t; }
                for (int ox = 0; ox < dx; ++ox)
                    for (int oy = 0; oy < dy; ++oy)
                        orFlag(pl, p.x + ox, p.y + oy, kTileBlockFull);
            }
        }
    }
    g_blocked_cache[key] = std::move(blk);
    return g_blocked_cache[key];
}

}  // namespace

// Accessors so sibling decoders (Achievements.cpp) reuse the one Store + init + lock instead
// of opening their own handles. EnsureCacheInit must be called with AchievementsMutex held.
Store* CacheStore() { return g_store.get(); }
std::mutex& AchievementsMutex() { return g_mu; }
void EnsureCacheInit() { EnsureInit(); }

ItemInfo GetItem(int item_id) {
    if (item_id < 0) return {};
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    return ResolveLocked(item_id);
}

std::string ItemName(int item_id) {
    return GetItem(item_id).name;
}

bool ItemIsAugmented(int item_id) {
    if (item_id < 0) return false;
    return GetItem(item_id).augmented;
}

std::string ItemInfoJson(int item_id) {
    ItemInfo info = GetItem(item_id);
    std::string esc;
    esc.reserve(info.name.size() + 8);
    for (char c : info.name) {
        if (c == '"' || c == '\\') esc.push_back('\\');
        esc.push_back(c);
    }
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "{\"name\":\"%s\",\"ge_limit\":%d,\"value\":%lld}",
        esc.c_str(), info.ge_limit, info.value);
    return buf;
}

// NPC display-name overrides for defs whose cache name is an internal dev name
// (e.g. "combatv2_necromancy_..."). Applied in GetNpc.
static const std::unordered_map<int, std::string> g_npc_name_overrides = {
    // Necromancy combat conjures (cache def name is the internal "combatv2_necromancy_...").
    { 30265, "Skeleton Warrior" },
    { 30266, "Putrid Zombie" },
    { 30267, "Vengeful Ghost" },
    { 31142, "Phantom Guardian" },
};

// A name "looks internal" when it has underscores and no spaces (e.g.
// "combatv2_necromancy_conjure") -- never a real display name.
static bool name_looks_internal(const std::string& s) {
    return !s.empty() && s.find('_') != std::string::npos && s.find(' ') == std::string::npos;
}

// Interim readability for internal names until an explicit override is set:
// drop a leading "combatv2" marker, '_' -> ' ', capitalise each word.
static std::string prettify_internal_name(std::string s) {
    for (char& ch : s) if (ch == '_') ch = ' ';
    const std::string mk = "combatv2 ";
    if (s.rfind(mk, 0) == 0) s.erase(0, mk.size());
    bool start = true;
    for (char& ch : s) {
        if (ch == ' ') start = true;
        else if (start) { ch = (char)std::toupper((unsigned char)ch); start = false; }
    }
    return s;
}

NpcMeta GetNpc(int npc_id) {
    if (npc_id < 0) return {};
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    auto hit = g_npc_cache.find(npc_id);
    if (hit != g_npc_cache.end()) return hit->second;

    NpcMeta meta;
    auto* index = g_store ? g_store->Get(kIndexNpcs) : nullptr;
    if (index) {
        // Follow morph children when the def is a nameless/actionless morph BASE:
        // the real name/actions live on a varbit-selected child that can't be
        // resolved here, but children almost always share them, so the first
        // valid child is a safe approximation. 128 files per archive.
        int cur = npc_id;
        meta.id = npc_id;                    // resolved model id (updated as we follow morph children)
        for (int guard = 0; guard < 6; ++guard) {
            auto bytes = index->ReadFile(cur >> 7, cur & 0x7f);
            if (bytes.empty()) break;
            NpcDef def = DecodeNpc(cur, std::move(bytes));
            if (meta.name.empty() && !def.name.empty()) meta.id = cur;   // this def supplies the name/model
            if (meta.name.empty())     meta.name = def.name;
            if (meta.combat_level < 0) meta.combat_level = def.combat_level;
            if (meta.size <= 1 && def.size > 1) meta.size = def.size;   // tile footprint
            if (meta.actions.empty()) {
                // Per-slot, members option (150..154) overrides base option
                // (30..34) -- many NPCs store real actions only in 150..154.
                for (std::size_t i = 0; i < def.options.size(); ++i) {
                    const std::string& opt = !def.members_options[i].empty()
                                                 ? def.members_options[i] : def.options[i];
                    if (opt.empty() || opt == "Hidden" || opt == "null") continue;
                    meta.actions.push_back(opt);
                }
            }
            if ((!meta.actions.empty() && !meta.name.empty()) || def.transform_to.empty()) break;
            int next = -1;
            for (int c : def.transform_to) if (c >= 0 && c != cur) { next = c; break; }
            if (next < 0) break;
            cur = next;   // follow the morph
        }
    }
    // Explicit override wins; otherwise prettify an internal cache name.
    auto ov = g_npc_name_overrides.find(npc_id);
    if (ov != g_npc_name_overrides.end()) meta.name = ov->second;
    else if (name_looks_internal(meta.name)) meta.name = prettify_internal_name(meta.name);

    g_npc_cache[npc_id] = meta;
    return meta;
}

// ---- menu-rule authoring ------------------------------------------------------------------
namespace {

void JsonEscTo(std::string& out, const std::string& in) {
    for (char c : in) {
        if (c == '"' || c == '\\') out.push_back('\\');
        if ((unsigned char)c < 0x20) { out.push_back(' '); continue; }
        out.push_back(c);
    }
}

// name -> ids, one map per kind, decoded on first use. Guarded by g_mu like every other cache.
std::map<std::string, std::vector<int>> g_name_index[3];
bool g_name_index_built[3] = { false, false, false };

void BuildNameIndexLocked(int kind) {
    if (kind < 0 || kind > 2 || g_name_index_built[kind]) return;
    g_name_index_built[kind] = true;                 // one attempt: a failed sweep must not loop
    const int idx = kind == 0 ? kIndexItems : kind == 1 ? kIndexLocations : kIndexNpcs;
    auto* index = g_store ? g_store->Get(idx) : nullptr;
    if (!index) return;
    // Archives are 256 files; walk until a run of empty archives says the index is exhausted.
    int emptyRun = 0;
    for (int archive = 0; archive < 512 && emptyRun < 8; ++archive) {
        bool any = false;
        for (int file = 0; file < 256; ++file) {
            auto bytes = index->ReadFile(archive, file);
            if (bytes.empty()) continue;
            any = true;
            const int id = (archive << 8) | file;
            std::string nm;
            if (kind == 0)      nm = DecodeItem(id, std::move(bytes)).name;
            else if (kind == 1) nm = DecodeLoc(id, std::move(bytes)).name;
            else                nm = DecodeNpc(id, std::move(bytes)).name;
            if (!nm.empty()) g_name_index[kind][nm].push_back(id);
        }
        emptyRun = any ? 0 : emptyRun + 1;
    }
}

}  // namespace

std::string MenuDefJson(int kind, int id) {
    if (id < 0 || kind < 0 || kind > 2) return "{}";
    std::string name;
    std::vector<std::string> opts;
    if (kind == 1) {
        LocMeta m = GetLoc(id);
        name = m.name;
        opts = m.actions;
    } else if (kind == 2) {
        NpcMeta m = GetNpc(id);
        name = m.name;
        opts = m.actions;
    } else {
        std::lock_guard<std::mutex> lk(g_mu);
        EnsureInit();
        auto* index = g_store ? g_store->Get(kIndexItems) : nullptr;
        if (index) {
            auto bytes = index->ReadFile(id >> 8, id & 0xff);
            if (!bytes.empty()) {
                ItemDef d = DecodeItem(id, std::move(bytes));
                name = d.name;
                // The carried set is what a backpack menu draws; fall back to the ground set
                // for items that only exist on the floor.
                for (const auto& o : d.worn_options) if (!o.empty()) opts.push_back(o);
                if (opts.empty())
                    for (const auto& o : d.options) if (!o.empty()) opts.push_back(o);
            }
        }
    }
    // Options the CLIENT appends rather than the def carrying them - without these the authored
    // list disagreed with the real menu (Pharaoh's sceptre offered Teleport/Wield/Examine here
    // while the game drew Teleport/Wield/Use/Drop/Examine). Live menus put Use and Drop after the
    // def's own options and Examine last, for every kind; skipped if the def already names one.
    {
        auto addTail = [&opts](const char* v) {
            for (const auto& o : opts) if (o == v) return;
            opts.push_back(v);
        };
        if (kind == 0) {
            addTail("Use");
            // DESTROY REPLACES DROP: an item that can be destroyed cannot be dropped, so the
            // client appends no Drop for it. Live: TokKul-Zo (Charged) draws Teleport / Wear /
            // Check-charge / Destroy / Use / Examine - Destroy comes from the def and there is
            // no Drop at all.
            bool destroyable = false;
            for (const auto& o : opts) if (o == "Destroy") destroyable = true;
            if (!destroyable) addTail("Drop");
        }
        addTail("Examine");
    }
    if (name.empty()) return "{}";
    std::string out = "{\"id\":" + std::to_string(id) + ",\"name\":\"";
    JsonEscTo(out, name);
    out += "\",\"options\":[";
    for (std::size_t i = 0; i < opts.size(); ++i) {
        if (i) out += ',';
        out += '"';
        JsonEscTo(out, opts[i]);
        out += '"';
    }
    out += "]}";
    return out;
}

std::string MenuFindJson(int kind, const std::string& name) {
    if (kind < 0 || kind > 2 || name.empty()) return "{\"ids\":[]}";
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    BuildNameIndexLocked(kind);
    std::string out = "{\"ids\":[";
    auto hit = g_name_index[kind].find(name);
    if (hit != g_name_index[kind].end())
        for (std::size_t i = 0; i < hit->second.size(); ++i) {
            if (i) out += ',';
            out += std::to_string(hit->second[i]);
        }
    out += "]}";
    return out;
}

std::string MenuSearchJson(int kind, const std::string& query, int limit) {
    if (kind < 0 || kind > 2 || query.empty()) return "{\"hits\":[]}";
    if (limit <= 0 || limit > 200) limit = 60;
    std::string needle;
    for (char c : query) needle.push_back((char)std::tolower((unsigned char)c));

    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    BuildNameIndexLocked(kind);

    // Exact and prefix matches first: searching "bank" should lead with "Bank booth", not with
    // whatever alphabetically-first def merely contains the word.
    struct Hit { int id; const std::string* name; int rank; };
    std::vector<Hit> hits;
    for (const auto& kv : g_name_index[kind]) {
        std::string low;
        low.reserve(kv.first.size());
        for (char c : kv.first) low.push_back((char)std::tolower((unsigned char)c));
        const std::size_t at = low.find(needle);
        if (at == std::string::npos) continue;
        const int rank = (low == needle) ? 0 : (at == 0 ? 1 : 2);
        for (int id : kv.second) hits.push_back({ id, &kv.first, rank });
        if ((int)hits.size() > limit * 4) break;      // enough to rank from; the cap applies below
    }
    std::stable_sort(hits.begin(), hits.end(),
                     [](const Hit& a, const Hit& b) { return a.rank < b.rank; });

    std::string out = "{\"hits\":[";
    int n = 0;
    for (const Hit& h : hits) {
        if (n >= limit) break;
        if (n) out += ',';
        out += "{\"id\":" + std::to_string(h.id) + ",\"name\":\"";
        JsonEscTo(out, *h.name);
        out += "\"}";
        ++n;
    }
    out += "]}";
    return out;
}

LocMeta GetLoc(int loc_id) {
    if (loc_id < 0) return {};
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    auto hit = g_loc_cache.find(loc_id);
    if (hit != g_loc_cache.end()) return hit->second;

    LocMeta meta;
    auto* index = g_store ? g_store->Get(kIndexLocations) : nullptr;
    if (index) {
        // Follow morph children (opcodes 77/92) when the placed loc is a nameless
        // morph BASE: the name lives on a varbit-selected child; the first named
        // child is a safe label. 256 files per archive (archive = id >> 8).
        int cur = loc_id;
        for (int guard = 0; guard < 6; ++guard) {
            auto bytes = index->ReadFile(cur >> 8, cur & 0xff);
            if (bytes.empty()) break;
            LocDef def = DecodeLoc(cur, std::move(bytes));
            if (guard == 0) { meta.dim_x = def.dim_x; meta.dim_y = def.dim_y; }  // placed loc's footprint
            if (meta.name.empty()) meta.name = def.name;
            // Members option (150..154) overrides the base option (30..34) per slot.
            if (meta.actions.empty()) {
                for (std::size_t i = 0; i < def.options.size(); ++i) {
                    const std::string& opt = !def.members_options[i].empty()
                                                 ? def.members_options[i]
                                                 : def.options[i];
                    if (opt.empty() || opt == "Hidden" || opt == "null") continue;
                    meta.actions.push_back(opt);
                }
            }
            if (!meta.name.empty() || def.morph_children.empty()) break;
            int next = -1;
            for (int c : def.morph_children) if (c >= 0 && c != cur) { next = c; break; }
            if (next < 0) break;
            cur = next;   // follow the morph to a named variant
        }
    }
    g_loc_cache[loc_id] = meta;
    return meta;
}

namespace {
struct LocMorphInfo { bool has = false; int varbit = -1, varp = -1, def_child = -1; std::vector<int> variants; };
std::unordered_map<int, LocMorphInfo> g_loc_morph_cache;
std::unordered_map<int, LocMorphInfo> g_npc_morph_cache;   // same shape, NPC index
}  // namespace

bool GetLocMorph(int loc_id, int& varbit, int& varp, int& def_child, std::vector<int>& variants) {
    if (loc_id < 0) return false;
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    auto hit = g_loc_morph_cache.find(loc_id);
    if (hit == g_loc_morph_cache.end()) {
        LocMorphInfo info;
        auto* index = g_store ? g_store->Get(kIndexLocations) : nullptr;
        if (index) {
            auto bytes = index->ReadFile(loc_id >> 8, loc_id & 0xff);
            if (!bytes.empty()) {
                LocDef def = DecodeLoc(loc_id, std::move(bytes));
                if (!def.morph_variants.empty() && (def.morph_varbit >= 0 || def.morph_varp >= 0)) {
                    info.has = true;
                    info.varbit = def.morph_varbit;
                    info.varp = def.morph_varp;
                    info.def_child = def.morph_default;
                    info.variants = std::move(def.morph_variants);
                }
            }
        }
        hit = g_loc_morph_cache.emplace(loc_id, std::move(info)).first;
    }
    const LocMorphInfo& info = hit->second;
    if (!info.has) return false;
    varbit = info.varbit; varp = info.varp; def_child = info.def_child; variants = info.variants;
    return true;
}

bool GetNpcMorph(int npc_id, int& varbit, int& varp, int& def_child, std::vector<int>& variants) {
    if (npc_id < 0) return false;
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    auto hit = g_npc_morph_cache.find(npc_id);
    if (hit == g_npc_morph_cache.end()) {
        LocMorphInfo info;
        auto* index = g_store ? g_store->Get(kIndexNpcs) : nullptr;
        if (index) {
            auto bytes = index->ReadFile(npc_id >> 7, npc_id & 0x7f);   // 128 NPC files per archive
            if (!bytes.empty()) {
                NpcDef def = DecodeNpc(npc_id, std::move(bytes));
                // op106/118 store the value-indexed variants followed by a trailing default. A
                // varbit/varp selector + >=2 entries = a real morph; split off the last as the default.
                if ((def.varbit >= 0 || def.varp >= 0) && def.transform_to.size() >= 2) {
                    info.has = true;
                    info.varbit = def.varbit;
                    info.varp = def.varp;
                    info.def_child = def.transform_to.back();
                    info.variants.assign(def.transform_to.begin(), def.transform_to.end() - 1);
                }
            }
        }
        hit = g_npc_morph_cache.emplace(npc_id, std::move(info)).first;
    }
    const LocMorphInfo& info = hit->second;
    if (!info.has) return false;
    varbit = info.varbit; varp = info.varp; def_child = info.def_child; variants = info.variants;
    return true;
}

namespace {
// Lazily decode every DBRows perk row into g_perk_names. Caller holds g_mu.
// Faithful to the DBRowsType decode: op4 = tableId; op3 = a column block
// (totalCols, then per group: id byte (col = b&0x3F, 0xFF ends), sub count,
// per-sub unsigned-smart type [0x24 = string], row count as an UNSIGNED SMART,
// then the values as ROW-MAJOR tuples: row0's subcolumns, then row1's, ...
// (validated 18394/18394 on build 949; column-major misparsed every mixed
// string+int multi-sub column, and a u8 rowcount broke rows with >127 entries).
// A perk row has tableId 8; its id = first int column's first-row value, name =
// first string column's first-row value (both by ascending column id).
void LoadPerkNamesLocked() {
    if (g_perks_loaded) return;
    g_perks_loaded = true;
    auto* index = g_store ? g_store->Get(kIndexConfigs) : nullptr;
    if (!index || !index->ready()) return;
    constexpr int kDbRowsArchive = 41;
    const auto& entries = index->ref().entries();
    if ((int)entries.size() <= kDbRowsArchive) return;
    for (int fid : entries[kDbRowsArchive].valid_file_ids) {
        auto bytes = index->ReadFile(kDbRowsArchive, fid);
        if (bytes.size() < 2) continue;
        InputStream s(std::move(bytes));
        int tableId = -1;
        int idCol = 1 << 30, idVal = 0;            bool haveId = false;
        int nameCol = 1 << 30; std::string nameVal; bool haveName = false;
        int descCol = 1 << 30; std::string descVal;   // 2nd string col = effect description (col 4)
        bool bad = false;
        while (s.remaining() > 0 && !bad) {
            int op = s.ReadUnsignedByte();
            if (op == 0) break;
            if (op == 4) { tableId = s.ReadUnsignedSmart(); continue; }
            if (op != 3) break;                    // unknown opcode -> stop this row
            s.ReadUnsignedByte();                  // totalCols (unused)
            while (s.remaining() > 0) {
                int b = s.ReadUnsignedByte();
                if (b == 0xFF) break;
                int columnId = b & 0x3F;
                int subN = s.ReadUnsignedByte();
                if (subN <= 0) continue;
                std::vector<int> types((std::size_t)subN);
                for (int i = 0; i < subN; ++i) types[i] = s.ReadUnsignedSmart();
                int rowCount = s.ReadUnsignedSmart();
                for (int r = 0; r < rowCount && !bad; ++r) {
                    for (int sub = 0; sub < subN; ++sub) {
                        if (s.remaining() <= 0) { bad = true; break; }
                        int col = columnId + sub;
                        if (types[sub] == 0x24) {
                            std::string v = s.ReadString();
                            if (r == 0 && col < nameCol) {
                                if (haveName && nameCol < descCol) { descCol = nameCol; descVal = nameVal; }
                                nameCol = col; nameVal = v; haveName = true;
                            } else if (r == 0 && col < descCol) { descCol = col; descVal = v; }
                        } else {
                            int v = s.ReadInt();
                            if (r == 0 && col < idCol) { idCol = col; idVal = v; haveId = true; }
                        }
                    }
                }
            }
        }
        if (tableId == 8 && haveId && haveName && idVal >= 0 && !nameVal.empty()) {
            g_perk_names[idVal] = nameVal;
            if (!descVal.empty()) g_perk_descs[idVal] = descVal;
        }
    }
}

// Archaeology mystery -> collectible lists, straight from the DBRows archive (the same
// linkage script14584 counts via DB_GETFIELDCOUNT). One sweep of archive 41: table-92
// rows (op4 master 92; mysteries) keep their name + column-4/5 row-id lists; referenced
// rows resolve to (col0, col3) = (bit index, item id) -- table 81 = journal pages
// (script13039 varp banks), table 31 = collectibles (varp 11733 bits, script18966).
// Row decode = the LoadPerkNamesLocked shape (validated 18394/18394 on build 949).
std::string g_myst_pages_json;

void LoadMystPagesLocked() {
    if (!g_myst_pages_json.empty()) return;
    auto* index = g_store ? g_store->Get(kIndexConfigs) : nullptr;
    if (!index || !index->ready()) return;
    constexpr int kDbRowsArchive = 41;
    const auto& entries = index->ref().entries();
    if ((int)entries.size() <= kDbRowsArchive) return;
    struct Row {
        int master = -1;
        std::string name;                    // first string column (mystery name on table 92)
        std::vector<int> c0, c3, c4, c5;     // the only columns this mapping needs
    };
    std::unordered_map<int, Row> rows;
    for (int fid : entries[kDbRowsArchive].valid_file_ids) {
        auto bytes = index->ReadFile(kDbRowsArchive, fid);
        if (bytes.size() < 2) continue;
        InputStream s(std::move(bytes));
        Row row; int nameCol = 1 << 30; bool bad = false;
        while (s.remaining() > 0 && !bad) {
            int op = s.ReadUnsignedByte();
            if (op == 0) break;
            if (op == 4) {                       // table tag: (master<<8)|subtable when >= 256
                int t = s.ReadUnsignedSmart();
                row.master = t >= 256 ? (t >> 8) : t;
                continue;
            }
            if (op != 3) break;
            s.ReadUnsignedByte();                // totalCols (unused)
            while (s.remaining() > 0) {
                int b = s.ReadUnsignedByte();
                if (b == 0xFF) break;
                int columnId = b & 0x3F;
                int subN = s.ReadUnsignedByte();
                if (subN <= 0) continue;
                std::vector<int> types((std::size_t)subN);
                for (int i = 0; i < subN; ++i) types[i] = s.ReadUnsignedSmart();
                int rowCount = s.ReadUnsignedSmart();
                for (int r = 0; r < rowCount && !bad; ++r) {
                    for (int sub = 0; sub < subN; ++sub) {
                        if (s.remaining() <= 0) { bad = true; break; }
                        int col = columnId + sub;
                        if (types[sub] == 0x24) {
                            std::string v = s.ReadString();
                            if (col < nameCol) { nameCol = col; row.name = std::move(v); }
                        } else {
                            int v = s.ReadInt();
                            if      (col == 0) row.c0.push_back(v);
                            else if (col == 3) row.c3.push_back(v);
                            else if (col == 4) row.c4.push_back(v);
                            else if (col == 5) row.c5.push_back(v);
                        }
                    }
                }
            }
        }
        if (row.master == 92 || row.master == 81 || row.master == 31)
            rows[fid] = std::move(row);
    }
    if (rows.empty()) return;                    // cache not fully readable yet -> retry later
    // Referenced-row lists -> "[[bitIndex,itemId],..]", keeping only rows of the wanted table.
    auto emit_refs = [&rows](const std::vector<int>& refs, int wantMaster) {
        std::string s2 = "["; bool f2 = true;
        for (int rid : refs) {
            auto it = rows.find(rid);
            if (it == rows.end() || it->second.master != wantMaster || it->second.c0.empty()) continue;
            s2 += f2 ? "" : ","; f2 = false;
            s2 += "[" + std::to_string(it->second.c0[0]) + "," +
                  std::to_string(it->second.c3.empty() ? -1 : it->second.c3[0]) + "]";
        }
        return s2 + "]";
    };
    std::string out = "{\"myst\":{";
    bool first = true;
    for (auto& [fid, row] : rows) {
        if (row.master != 92 || row.name.empty()) continue;
        if (row.c4.empty() && row.c5.empty()) continue;
        // Other subtables share master 92; a real mystery is one whose refs resolve to
        // page/collectible rows, so skip entries where both lists come out empty.
        std::string pg = emit_refs(row.c4, 81), c31 = emit_refs(row.c5, 31);
        if (pg == "[]" && c31 == "[]") continue;
        out += first ? "\"" : ",\""; first = false;
        for (char c : row.name) { if (c == '"' || c == '\\') out += '\\'; if ((unsigned char)c >= 0x20) out += c; }
        out += "\":{\"pg\":" + pg + ",\"c31\":" + c31 + "}";
    }
    out += "}}";
    g_myst_pages_json = std::move(out);
}

}  // namespace

std::string PerkName(int perk_id) {
    if (perk_id <= 0) return {};
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    LoadPerkNamesLocked();
    auto it = g_perk_names.find(perk_id);
    return it != g_perk_names.end() ? it->second : std::string();
}

// Perk effect description, from the same DBRows perk row (2nd string column, col 4) --
// e.g. Precise: "Increases your minimum damage by 1.5% per rank of your maximum damage."
// May carry Jagex markup (<col=..>); the UI renders it via the tip-html path.
std::string PerkDesc(int perk_id) {
    if (perk_id <= 0) return {};
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    LoadPerkNamesLocked();
    auto it = g_perk_descs.find(perk_id);
    return it != g_perk_descs.end() ? it->second : std::string();
}

// ---- Archaeology research (DBTable 90) -------------------------------------
// One row per research topic. CS2 script14843 renders a row as name (col 3) plus
// "Field Study: <col 6>" and "Report: <col 7>"; script14629/14630 gate those on a
// BIT (col 0) in varp 9297/9298/11740 and 9299/9300/11741 respectively. We publish
// the static half here and let the panel test the live bits.
namespace {
std::string g_arch_research_json;

void LoadArchResearchLocked() {
    if (!g_arch_research_json.empty()) return;
    auto* index = g_store ? g_store->Get(kIndexConfigs) : nullptr;
    if (!index || !index->ready()) return;
    constexpr int kDbRowsArchive = 41;
    const auto& entries = index->ref().entries();
    if ((int)entries.size() <= kDbRowsArchive) return;

    // `row` is the DBRow file id -- the per-culture book enums (14083/14084/14085/10575/3016/
    // 14086/14087/14088, indexed by enum 14082) list research BY ROW ID, so the panel needs it
    // to group the list into Armadylean / Bandosian / Dragonkin / ... pages. `kind` is column 1:
    // 1 = the site-wide "your research team excavates materials here" entry, 2 = a named discovery.
    struct Res { int bit = -1, row = -1, kind = 0; std::string name, field, report; };
    std::vector<Res> out;
    for (int fid : entries[kDbRowsArchive].valid_file_ids) {
        auto bytes = index->ReadFile(kDbRowsArchive, fid);
        if (bytes.size() < 2) continue;
        InputStream s(std::move(bytes));
        int master = -1;
        std::map<int, std::string> strs;      // column -> first string value
        std::map<int, int> ints;              // column -> first int value
        bool bad = false;
        while (s.remaining() > 0 && !bad) {
            int op = s.ReadUnsignedByte();
            if (op == 0) break;
            if (op == 4) {                      // table tag: (master<<8)|subtable when >= 256
                int t = s.ReadUnsignedSmart();
                master = t >= 256 ? (t >> 8) : t;
                continue;
            }
            if (op != 3) break;
            s.ReadUnsignedByte();               // total column count (unused)
            while (s.remaining() > 0) {
                int b = s.ReadUnsignedByte();
                if (b == 0xFF) break;
                int columnId = b & 0x3F;
                int subN = s.ReadUnsignedByte();
                if (subN <= 0) continue;
                std::vector<int> types((std::size_t)subN);
                for (int i = 0; i < subN; ++i) types[i] = s.ReadUnsignedSmart();
                int rowCount = s.ReadUnsignedSmart();
                for (int r = 0; r < rowCount && !bad; ++r) {
                    for (int sub = 0; sub < subN; ++sub) {
                        if (s.remaining() <= 0) { bad = true; break; }
                        int col = columnId + sub;
                        if (types[sub] == 0x24) {
                            std::string v = s.ReadString();
                            if (!strs.count(col)) strs[col] = std::move(v);
                        } else {
                            int v = s.ReadInt();
                            if (!ints.count(col)) ints[col] = v;
                        }
                    }
                }
            }
        }
        if (bad || master != 90) continue;
        Res e;
        e.bit    = ints.count(0) ? ints[0] : -1;
        e.kind   = ints.count(1) ? ints[1] : 0;
        e.row    = fid;
        e.name   = strs.count(3) ? strs[3] : std::string();
        e.field  = strs.count(6) ? strs[6] : std::string();
        e.report = strs.count(7) ? strs[7] : std::string();
        // A row with no name is a placeholder, not research the player can ever see -- keeping
        // them only padded the total with unnameable "Research #N" rows.
        if (e.name.empty()) continue;
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const Res& a, const Res& b) { return a.bit < b.bit; });

    // ReadString already emits UTF-8, so only JSON escaping is needed here (same
    // shape as the quest journal strings).
    std::string j = "[";
    auto jstr = [&j](const char* k, const std::string& v) {
        j += ",\""; j += k; j += "\":\"";
        for (char c : v) { if (c == '"' || c == '\\') j += '\\'; if ((unsigned char)c >= 0x20) j += c; }
        j += '"';
    };
    bool first = true;
    for (const auto& e : out) {
        if (!first) j += ',';
        first = false;
        j += "{\"b\":" + std::to_string(e.bit);
        j += ",\"d\":" + std::to_string(e.row);
        j += ",\"t\":" + std::to_string(e.kind);
        jstr("n", e.name);
        jstr("f", e.field);
        jstr("r", e.report);
        j += "}";
    }
    j += "]";
    g_arch_research_json = std::move(j);
}
}  // namespace

std::string ArchResearchJson() {
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    LoadArchResearchLocked();
    return g_arch_research_json.empty() ? std::string("[]") : g_arch_research_json;
}

std::string MystPagesJson() {
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    LoadMystPagesLocked();
    return g_myst_pages_json.empty() ? std::string("{\"myst\":{}}") : g_myst_pages_json;
}

namespace {
// DBTABLE schemas: CONFIGS index 2, archive 40 -- the declared column types
// (and defaults) that DBRows carry inline. The schema is the ground truth the
// row decode is checked against in the health sweep, so the next DBRows format
// change shows up as a red row instead of silently wrong perk names.
//
// Linkage (derived and verified on build 949: the inline column types of all
// 18394 live rows match the schema found this way, 0 mismatches): a row's op4
// tag packs (master << 8) | subtable (plain master id when < 256), and that
// table's schema is FILE subtable*128 + master. Live subtables are 0/1/2.
//
// File format: every non-empty live file uses op 2 (op 1 = the older variant
// per the game, kept but unobserved on 949). op 2: u32 unknown, u8 column count,
// then per column: id byte (0xFF ends the list; id = b & 0x3F), u8 unknown,
// u8 subcolumn count, per-sub usmart type, u8 flags; flags & 2 -> default
// values (u8, first value, u8, then the remaining values; string when type
// 0x24, else i32). op 1: the id byte's 0x80 bit marks defaults (single u8
// before the values). 121 of 365 live files are a bare 0x00 footer (a table
// with no declared columns; a third of the live rows reference one).
constexpr int kDbTablesArchive = 40;
std::unordered_map<int, std::map<int, std::vector<int>>> g_dbtable_cols;  // file -> col -> sub types
bool g_dbtables_loaded = false;

// Decode one table file into `cols` (null = validate only). Returns 0 on a
// clean end, the opcode that broke the read otherwise (256 = stream ran out).
int DecodeDbTableFile(std::vector<std::uint8_t> bytes,
                      std::map<int, std::vector<int>>* cols) {
    InputStream s(std::move(bytes));
    while (s.remaining() > 0) {
        int op = s.ReadUnsignedByte();
        if (op == 0) break;
        if (op != 1 && op != 2) return op;
        if (op == 2) s.ReadInt();                     // unknown u32 (op 2 only)
        s.ReadUnsignedByte();                         // total column count
        while (true) {
            if (s.remaining() <= 0) return 256;
            int cb = s.ReadUnsignedByte();
            if (cb == 0xFF) break;
            int colid = cb & 0x3F;
            if (op == 2) s.ReadUnsignedByte();        // unknown u8 (op 2 only)
            int subn = s.ReadUnsignedByte();
            std::vector<int> types((std::size_t)subn);
            for (int i = 0; i < subn; ++i) types[i] = s.ReadUnsignedSmart();
            bool defs = (op == 2) ? (s.ReadUnsignedByte() & 0x02) != 0
                                  : (cb & 0x80) != 0;
            if (defs) {
                for (int i = 0; i < subn; ++i) {
                    if (s.remaining() <= 0) return 256;
                    if (i == 0) s.ReadUnsignedByte();
                    if (types[i] == 0x24) s.ReadString(); else s.ReadInt();
                    if (i == 0 && op == 2) s.ReadUnsignedByte();
                }
            }
            if (cols) (*cols)[colid] = std::move(types);
        }
    }
    return 0;
}

void LoadDbTablesLocked() {
    if (g_dbtables_loaded) return;
    auto* index = g_store ? g_store->Get(kIndexConfigs) : nullptr;
    if (!index || !index->ready()) return;            // cache not open yet -> retry next call
    g_dbtables_loaded = true;
    const auto& entries = index->ref().entries();
    if ((int)entries.size() <= kDbTablesArchive) return;
    for (int fid : entries[kDbTablesArchive].valid_file_ids) {
        std::map<int, std::vector<int>> cols;
        if (DecodeDbTableFile(index->ReadFile(kDbTablesArchive, fid), &cols) == 0)
            g_dbtable_cols[fid] = std::move(cols);
    }
}

// Decode one DBRow (tag + inline column types; row layout as LoadPerkNamesLocked)
// and check it against the loaded schemas. Returns 0 when every column's types
// match (or the table declares no columns), 257 on a linkage/type mismatch, the
// breaking opcode on a structural break (256 = overrun). Caller holds g_mu with
// dbtables loaded.
int DbRowSchemaCheckLocked(std::vector<std::uint8_t> bytes) {
    InputStream s(std::move(bytes));
    int tag = -1;
    std::vector<std::pair<int, std::vector<int>>> rcols;
    while (s.remaining() > 0) {
        int op = s.ReadUnsignedByte();
        if (op == 0) break;
        if (op == 4) { tag = s.ReadUnsignedSmart(); continue; }
        if (op != 3) return op;
        s.ReadUnsignedByte();                         // total cols
        while (s.remaining() > 0) {
            int cb = s.ReadUnsignedByte();
            if (cb == 0xFF) break;
            int colid = cb & 0x3F;
            int subN = s.ReadUnsignedByte();
            if (subN <= 0) continue;
            std::vector<int> types((std::size_t)subN);
            for (int i = 0; i < subN; ++i) types[i] = s.ReadUnsignedSmart();
            int rowCount = s.ReadUnsignedSmart();
            for (int r = 0; r < rowCount; ++r)
                for (int sub = 0; sub < subN; ++sub) {
                    if (s.remaining() <= 0) return 256;
                    if (types[sub] == 0x24) s.ReadString(); else s.ReadInt();
                }
            rcols.emplace_back(colid, std::move(types));
        }
    }
    if (tag < 0) return 257;                          // no table tag -> unverifiable
    const int master = tag < 256 ? tag : (tag >> 8);
    const int sub    = tag < 256 ? 0   : (tag & 0xFF);
    auto it = g_dbtable_cols.find(sub * 128 + master);
    if (it == g_dbtable_cols.end()) return 257;       // schema file missing
    if (it->second.empty()) return 0;                 // declaration-less table
    for (const auto& [colid, types] : rcols) {
        auto ct = it->second.find(colid);
        if (ct == it->second.end() || ct->second != types) return 257;
    }
    return 0;
}

// PARAM definitions: CONFIGS index 2, archive 11 (file = param id). op1/op101 =
// vartype byte, op2 = default int, op5 = default string, op4/131/207/209 =
// payload-less flags (131/207/209 added build 949). The whole archive decodes
// once (9420/9420 clean on 949) so param defaults come from one shared table
// instead of per-param point reads.
constexpr int kParamsArchive = 11;
struct ParamDef {
    int  type = 0;                    // vartype byte from op1/101 (0 when absent)
    int  def_int = 0;   bool has_int = false;
    std::string def_str; bool has_str = false;
};
std::unordered_map<int, ParamDef> g_param_defs;
bool g_params_loaded = false;

// Decode one param file (out = null to validate only). Returns 0 on a clean
// end, else the stopping opcode. Owns the opcode table for both the loader
// and the health sweep.
int DecodeParamFile(std::vector<std::uint8_t> bytes, ParamDef* out) {
    InputStream s(std::move(bytes));
    while (s.remaining() > 0) {
        int op = s.ReadUnsignedByte();
        if (op == 0) break;
        else if (op == 1 || op == 101) { int t = s.ReadUnsignedByte(); if (out) out->type = t; }
        else if (op == 2) { int v = s.ReadInt(); if (out) { out->def_int = v; out->has_int = true; } }
        else if (op == 4) { /* flag, no payload */ }
        else if (op == 5) { std::string v = s.ReadString(); if (out) { out->def_str = std::move(v); out->has_str = true; } }
        else if (op == 131 || op == 207 || op == 209) { /* flags, added build 949 */ }
        else return op;                               // unknown opcode -> length unknown, stop
    }
    return 0;
}

void LoadParamsLocked() {
    if (g_params_loaded) return;
    auto* index = g_store ? g_store->Get(kIndexConfigs) : nullptr;
    if (!index || !index->ready()) return;            // cache not open yet -> retry next call
    g_params_loaded = true;
    const auto& entries = index->ref().entries();
    if ((int)entries.size() <= kParamsArchive) return;
    for (int fid : entries[kParamsArchive].valid_file_ids) {
        auto bytes = index->ReadFile(kParamsArchive, fid);
        if (bytes.empty()) continue;
        ParamDef p;
        if (DecodeParamFile(std::move(bytes), &p) == 0) g_param_defs[fid] = std::move(p);
    }
}
}  // namespace

std::string ParamDefJson(int param_id) {
    if (param_id < 0) return "{}";
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    LoadParamsLocked();
    auto it = g_param_defs.find(param_id);
    if (it == g_param_defs.end()) return "{}";
    const ParamDef& p = it->second;
    std::string out = "{\"type\":" + std::to_string(p.type);
    if (p.has_int) out += ",\"int\":" + std::to_string(p.def_int);
    if (p.has_str) {
        out += ",\"str\":\"";
        for (char c : p.def_str) {
            if (c == '"' || c == '\\') out += '\\';
            if ((unsigned char)c >= 0x20) out += c;
        }
        out += "\"";
    }
    out += "}";
    return out;
}

namespace {
// Varbit definitions: CONFIGS index 2, archive 69 (file_id = varbit id). Each
// varbit is the bit-slice [lsb,msb] of a var. We decode the whole archive once
// into a varp-id -> [[varbitId,lsb,msb],..] map so the watcher can name which
// varbit owns a changed bit. Per the varbit config decode:
//   opcode 1 -> u8 var DOMAIN + u16 var index
//   opcode 2 -> u8 lsb + u8 msb
//   opcode 16 -> boolean flag, no payload ; opcode 0 ends.
// Only domain 0 (player varp) entries go in the map: other domains (npc/client/
// world/clan/..) are separate id-spaces, and keying them by index here would
// misfile them under same-numbered player varps (every domain's var 0 showed
// up on varp 0, and VarbitsJson read them from the wrong varp).
// Read from the live game cache so the mapping matches the running build.
// Cached as JSON.
constexpr int kVarbitArchive = 69;
std::string g_varbit_map_json;
bool        g_varbit_map_loaded = false;
std::unordered_map<int, std::array<int, 3>> g_varbit_defs;   // varbit id -> {varp,lsb,msb}

void LoadVarbitMapLocked() {
    if (g_varbit_map_loaded) return;
    auto* index = g_store ? g_store->Get(kIndexConfigs) : nullptr;
    if (!index || !index->ready()) return;          // cache not open yet -> retry next call
    const auto& entries = index->ref().entries();
    if ((int)entries.size() <= kVarbitArchive) return;
    g_varbit_map_loaded = true;

    std::unordered_map<int, std::string> per_varp;  // varp id -> "[id,lsb,msb],.."
    char buf[64];
    for (int fid : entries[kVarbitArchive].valid_file_ids) {
        auto bytes = index->ReadFile(kVarbitArchive, fid);
        if (bytes.empty()) continue;
        InputStream s(std::move(bytes));
        int domain = -1, varp = -1, lsb = -1, msb = -1;
        while (s.remaining() > 0) {
            int op = s.ReadUnsignedByte();
            if (op == 0) break;
            if (op == 1)      { domain = s.ReadUnsignedByte(); varp = s.ReadUnsignedShort(); }
            else if (op == 2) { lsb = s.ReadUnsignedByte(); msb = s.ReadUnsignedByte(); }
            else if (op == 16) { /* boolean flag, no data */ }
            else break;                             // unknown opcode -> length unknown, stop
        }
        if (domain != 0 || varp < 0 || lsb < 0 || msb < 0) continue;
        g_varbit_defs[fid] = { varp, lsb, msb };
        std::snprintf(buf, sizeof(buf), "%s[%d,%d,%d]",
                      per_varp[varp].empty() ? "" : ",", fid, lsb, msb);
        per_varp[varp] += buf;
    }
    std::string out = "{"; bool first = true;
    for (auto& kv : per_varp) {
        std::snprintf(buf, sizeof(buf), "%s\"%d\":[", first ? "" : ",", kv.first);
        out += buf; out += kv.second; out += "]";
        first = false;
    }
    out += "}";
    g_varbit_map_json = std::move(out);
}
}  // namespace

std::string VarbitMapJson() {
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    LoadVarbitMapLocked();
    return g_varbit_map_json.empty() ? std::string("{}") : g_varbit_map_json;
}

bool GetVarbit(int varbit_id, int& varp, int& lsb, int& msb) {
    if (varbit_id < 0) return false;
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    LoadVarbitMapLocked();
    auto it = g_varbit_defs.find(varbit_id);
    if (it == g_varbit_defs.end()) return false;
    varp = it->second[0]; lsb = it->second[1]; msb = it->second[2];
    return true;
}

namespace {
// Quest configs: CONFIGS index 2, archive 35 (file_id = quest config id). Opcode
// table for the RS3 QuestType decode (all live files parse):
//   1 name / 2 list name (version byte + cstring), 3 progress varps / 4 progress
//   varbits (u8 n x {u16 var, i32 start, i32 end}), 5 parent quest, 6 category,
//   7 difficulty, 8 members flag, 9 QP reward, 10 start path (n x i32), 12 i32,
//   13 required quests (n x u16), 14 required skills (n x {u8 skill, u8 level}),
//   15 QP required (u16), 17 graphic (big smart), 18/19 varp/varbit requirement
//   blocks (n x {i32,i32,i32,cstring}; empty in the live cache), 249 params.
// Re-released quests leave stub configs behind (same name, no progress vars, case
// or spelling variants); stubs are dropped and every questreq/parent link is
// remapped to the canonical entry so the UI never sees a duplicate or dead link.
constexpr int kQuestArchive = 35;
std::string g_quests_json;
bool        g_quests_loaded = false;

struct QuestDef {
    int id = -1;
    std::string name;
    bool members = false, has_diff = false;
    int difficulty = 0, points = 0, pointsreq = 0, parent = -1, graphic = -1;
    int vp = -1, vp_start = 0, vp_end = 0;          // first progress varp triple
    int vb = -1, vb_start = 0, vb_end = 0;          // first progress varbit triple
    std::vector<int> questreqs;
    std::vector<std::pair<int, int>> statreqs;      // {skill id, level}
    int icon = -1;                                   // param 7829 (journal icon sprite)
    int year = 0;                                    // param 7834 (release year)
    int journal = -1;                                // param 1345 (quest-journal id; absent = not in the in-game list)
    // Journal text + classification surfaces. Label enums are the game's own (CS2 ground
    // truth: script4011 renders 7855 via enum 13354 and 7831 via enum 13275; script20412
    // names 9393 via enum 9686) -- the panel fetches those enums itself (bridge enumInfo).
    std::string desc;                                // param 5968 (journal description)
    std::string start;                               // param 7814 (start point; 9392 = seasonal phrasing)
    std::string items;                               // param 7815 (required items)
    std::string combat;                              // param 7816 (combat advisory)
    std::string rewards;                             // param 7823 (non-XP rewards, <br>-separated)
    std::string replaced;                            // param 7888 ("In <year> this quest replaced ...")
    std::vector<std::pair<int, std::string>> xp;     // params 7818-7822 (XP reward lines)
    int length = -1;                                 // param 7855 (label enum 13354)
    int age = -1;                                    // param 7831 (label enum 13275)
    int area = -1;                                   // param 9393 (start-location area, names enum 9686)
    std::vector<int> startxy;                        // op10 packed start coords (plane<<28 | x<<14 | y)
};

bool DecodeQuestFile(std::vector<std::uint8_t> bytes, QuestDef& q, int* stop_op = nullptr) {
    if (stop_op) *stop_op = 0;
    if (bytes.empty()) return false;
    InputStream s(std::move(bytes));
    while (s.remaining() > 0) {
        int op = s.ReadUnsignedByte();
        if (op == 0) break;
        switch (op) {
        case 1:  s.ReadUnsignedByte(); q.name = s.ReadString(); break;
        case 2:  s.ReadUnsignedByte(); s.ReadString(); break;   // list name (op1 is canonical)
        case 3:  { int n = s.ReadUnsignedByte();
                   for (int i = 0; i < n; ++i) { int v = s.ReadUnsignedShort();
                       int a = s.ReadInt(), b = s.ReadInt();
                       if (i == 0) { q.vp = v; q.vp_start = a; q.vp_end = b; } } } break;
        case 4:  { int n = s.ReadUnsignedByte();
                   for (int i = 0; i < n; ++i) { int v = s.ReadUnsignedShort();
                       int a = s.ReadInt(), b = s.ReadInt();
                       if (i == 0) { q.vb = v; q.vb_start = a; q.vb_end = b; } } } break;
        case 5:  q.parent = s.ReadUnsignedShort(); break;
        case 6:  s.ReadUnsignedByte(); break;
        case 7:  q.difficulty = s.ReadUnsignedByte(); q.has_diff = true; break;
        case 8:  q.members = true; break;
        case 9:  q.points = s.ReadUnsignedByte(); break;
        case 10: { int n = s.ReadUnsignedByte();
                   for (int i = 0; i < n; ++i) q.startxy.push_back(s.ReadInt()); } break;
        case 12: s.ReadInt(); break;
        case 13: { int n = s.ReadUnsignedByte();
                   for (int i = 0; i < n; ++i) q.questreqs.push_back(s.ReadUnsignedShort()); } break;
        case 14: { int n = s.ReadUnsignedByte();
                   for (int i = 0; i < n; ++i) { int sk = s.ReadUnsignedByte();
                       int lv = s.ReadUnsignedByte(); q.statreqs.push_back({sk, lv}); } } break;
        case 15: q.pointsreq = s.ReadUnsignedShort(); break;
        case 17: q.graphic = s.ReadBigSmart(); break;
        case 18: case 19:
                 { int n = s.ReadUnsignedByte();
                   for (int i = 0; i < n; ++i) { s.ReadInt(); s.ReadInt(); s.ReadInt(); s.ReadString(); } } break;
        case 249: { int n = s.ReadUnsignedByte();
                    for (int i = 0; i < n; ++i) {
                        bool is_str = s.ReadUnsignedByte() == 1;
                        int key = s.Read24BitInt();
                        if (is_str) {
                            std::string v = s.ReadString();
                            if      (key == 5968) q.desc = std::move(v);
                            else if (key == 7814) q.start = std::move(v);
                            else if (key == 9392) { if (q.start.empty()) q.start = std::move(v); }
                            else if (key == 7815) q.items = std::move(v);
                            else if (key == 7816) q.combat = std::move(v);
                            else if (key == 7823) q.rewards = std::move(v);
                            else if (key == 7888) q.replaced = std::move(v);
                            else if (key >= 7818 && key <= 7822) q.xp.push_back({key, std::move(v)});
                        }
                        else { int v = s.ReadInt();
                               if (key == 7829) q.icon = v;
                               else if (key == 7834) q.year = v;
                               else if (key == 1345) q.journal = v;
                               else if (key == 7855) q.length = v;
                               else if (key == 7831) q.age = v;
                               else if (key == 9393) q.area = v; } } } break;
        default:
            if (stop_op) *stop_op = op;
            return !q.name.empty();        // unknown opcode -> keep what decoded
        }
    }
    return !q.name.empty();
}

// Dedupe key: lower-cased alphanumerics, with the known legacy stub spellings
// folded onto their canonical names.
std::string QuestNameKey(const std::string& name) {
    static const std::unordered_map<std::string, const char*> kAlias = {
        { "Mournings Ends Part 1",      "Mourning's End Part I" },
        { "Mournings Ends Part II",     "Mourning's End Part II" },
        { "The Great Brain Roberry",    "The Great Brain Robbery" },
        { "The Quiet Before the Swarm", "Quiet Before the Swarm" },
    };
    auto it = kAlias.find(name);
    const std::string src = it != kAlias.end() ? std::string(it->second) : name;
    std::string out;
    for (char c : src) {
        if (c >= 'A' && c <= 'Z') out += (char)(c | 0x20);
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += c;
    }
    return out;
}

void LoadQuestsLocked() {
    if (g_quests_loaded) return;
    auto* index = g_store ? g_store->Get(kIndexConfigs) : nullptr;
    if (!index || !index->ready()) return;          // cache not open yet -> retry next call
    const auto& entries = index->ref().entries();
    if ((int)entries.size() <= kQuestArchive) return;
    LoadVarbitMapLocked();                           // varbit trackers resolve below
    g_quests_loaded = true;

    std::map<int, QuestDef> defs;
    for (int fid : entries[kQuestArchive].valid_file_ids) {
        QuestDef q; q.id = fid;
        if (DecodeQuestFile(index->ReadFile(kQuestArchive, fid), q)) defs[fid] = std::move(q);
    }
    // canonical entry per name = the one WITH a progress tracker, then highest id
    auto tracked = [](const QuestDef& q) { return q.vp >= 0 || q.vb >= 0; };
    std::unordered_map<std::string, int> canon;      // name key -> config id
    for (auto& [fid, q] : defs) {
        auto key = QuestNameKey(q.name);
        auto it = canon.find(key);
        if (it == canon.end()) { canon[key] = fid; continue; }
        QuestDef& cur = defs[it->second];
        if (tracked(q) != tracked(cur) ? tracked(q) : fid > it->second) it->second = fid;
    }
    std::unordered_map<int, int> remap;              // stub id -> canonical id
    std::set<int> drop;
    for (auto& [fid, q] : defs) {
        int c = canon[QuestNameKey(q.name)];
        if (c != fid) { remap[fid] = c; drop.insert(fid); }
    }
    auto canon_id = [&](int id) { auto it = remap.find(id); return it != remap.end() ? it->second : id; };
    // The journal's generic quest icon = param 7829's own DEFAULT value (its
    // param config in archive 11). Quests without the param render this in the
    // game's quest list, so mirror it.
    int default_icon = -1;
    {
        LoadParamsLocked();
        auto it = g_param_defs.find(7829);
        if (it != g_param_defs.end() && it->second.has_int) default_icon = it->second.def_int;
    }
    // journal icon: param 7829, else op17 graphic, inherited through the parent
    // chain, else the journal's generic default.
    auto icon_of = [&](const QuestDef& q) {
        const QuestDef* cur = &q;
        for (int hop = 0; cur && hop < 8; ++hop) {
            if (cur->icon > 0) return cur->icon;
            if (cur->graphic > 0) return cur->graphic;
            auto it = defs.find(canon_id(cur->parent));
            cur = (it != defs.end() && it->second.id != cur->id) ? &it->second : nullptr;
        }
        return default_icon;
    };

    std::string out = "{\"vb\":{";
    {   // the varbits CS2 script2193's special-case status rules read (UI-side port)
        bool first = true;
        for (int vbid : { 12894, 10848, 13366, 12349, 12334 }) {
            auto it = g_varbit_defs.find(vbid);
            if (it == g_varbit_defs.end()) continue;
            out += first ? "" : ","; first = false;
            out += "\"" + std::to_string(vbid) + "\":[" + std::to_string(it->second[0]) + "," +
                   std::to_string(it->second[1]) + "," + std::to_string(it->second[2]) + "]";
        }
    }
    out += "},\"quests\":[";
    bool first = true;
    auto lower_has = [](const std::string& s, const char* needle) {
        std::string t; for (char c : s) t += (char)((c >= 'A' && c <= 'Z') ? (c | 0x20) : c);
        return t.find(needle) != std::string::npos;
    };
    // Current-quest filter: param 1345 is the game's own quest-journal id. Every entry the
    // in-game quest list shows carries one (quests, miniquests, sagas, permanent seasonal
    // quests; 361 in the live cache, ids unique), while everything the game no longer lists
    // has had it stripped even though most keep an old progress varp: replaced quests
    // (Romeo and Juliet, Black Knights' Fortress, Prince Ali Rescue, Doric's Quest),
    // discontinued holiday events (Holly, A Towering Feast, ...), retired tutorials
    // (Tutorial, Unstable Foundations) and internal stubs (Achievement System, Philipe's
    // Big Adventure). Fail-safe: if a cache update ever strips the param wholesale, drop
    // nothing rather than empty the list.
    int journalN = 0;
    for (auto& [fid, q] : defs) if (q.journal >= 0) ++journalN;
    int droppedLegacy = 0;
    if (journalN >= 100) {
        for (auto& [fid, q] : defs) {
            if (q.journal < 0 && !drop.count(fid)) { drop.insert(fid); ++droppedLegacy; }
        }
    }
    for (auto& [fid, q] : defs) {
        if (drop.count(fid)) continue;
        out += first ? "{" : ",{"; first = false;
        out += "\"id\":" + std::to_string(fid) + ",\"n\":\"";
        for (char c : q.name) { if (c == '"' || c == '\\') out += '\\'; if ((unsigned char)c >= 0x20) out += c; }
        out += "\"";
        if (lower_has(q.name, "(miniquest)")) out += ",\"mq\":1";
        if (q.members) out += ",\"m\":1";
        if (q.has_diff) out += ",\"d\":" + std::to_string(q.difficulty);
        out += ",\"p\":" + std::to_string(q.points);
        if (q.vp >= 0) {
            out += ",\"v\":[" + std::to_string(q.vp) + "," + std::to_string(q.vp_start) + "," +
                   std::to_string(q.vp_end) + "]";
        } else if (q.vb >= 0) {
            auto it = g_varbit_defs.find(q.vb);
            if (it != g_varbit_defs.end())
                out += ",\"b\":[" + std::to_string(it->second[0]) + "," + std::to_string(it->second[1]) + "," +
                       std::to_string(it->second[2]) + "," + std::to_string(q.vb_start) + "," +
                       std::to_string(q.vb_end) + "]";
        }
        if (!q.questreqs.empty()) {
            std::set<int> rq;
            for (int r : q.questreqs) { int c = canon_id(r); if (c != fid && defs.count(c) && !drop.count(c)) rq.insert(c); }
            if (!rq.empty()) {
                out += ",\"rq\":["; bool f2 = true;
                for (int r : rq) { out += f2 ? "" : ","; f2 = false; out += std::to_string(r); }
                out += "]";
            }
        }
        if (!q.statreqs.empty()) {
            out += ",\"rs\":["; bool f2 = true;
            for (auto& [sk, lv] : q.statreqs) {
                out += f2 ? "" : ","; f2 = false;
                out += "[" + std::to_string(sk) + "," + std::to_string(lv) + "]";
            }
            out += "]";
        }
        if (q.pointsreq > 0) out += ",\"rqp\":" + std::to_string(q.pointsreq);
        int par = canon_id(q.parent);
        if (par >= 0 && par != fid && defs.count(par) && !drop.count(par))
            out += ",\"par\":" + std::to_string(par);
        int ic = icon_of(q);
        if (ic > 0) out += ",\"ic\":" + std::to_string(ic);
        if (q.year > 0) out += ",\"yr\":" + std::to_string(q.year);
        // Journal text surfaces (ReadString already emits UTF-8, so only JSON escaping here).
        auto jstr = [&out](const char* k, const std::string& v) {
            if (v.empty()) return;
            out += ",\""; out += k; out += "\":\"";
            for (char c : v) { if (c == '"' || c == '\\') out += '\\'; if ((unsigned char)c >= 0x20) out += c; }
            out += '"';
        };
        jstr("ds", q.desc);
        jstr("sp", q.start);
        jstr("it", q.items);
        jstr("cb", q.combat);
        {   // XP reward lines joined in param order (7818 main, 7819-7822 extra skill lamps)
            std::string all;
            for (int k = 7818; k <= 7822; ++k)
                for (auto& [pk, pv] : q.xp)
                    if (pk == k) { if (!all.empty()) all += "<br>"; all += pv; }
            jstr("rx", all);
        }
        jstr("rw", q.rewards);
        jstr("rp", q.replaced);
        if (q.length >= 0) out += ",\"ln\":" + std::to_string(q.length);
        if (q.age >= 0)    out += ",\"ag\":" + std::to_string(q.age);
        if (q.area >= 0)   out += ",\"ar\":" + std::to_string(q.area);
        if (!q.startxy.empty()) {
            out += ",\"xy\":[";
            bool f2 = true;
            for (int v : q.startxy) { out += f2 ? "" : ","; f2 = false; out += std::to_string(v); }
            out += "]";
        }
        out += "}";
    }
    // journalN = configs carrying the journal id (the in-game list size); dropped = delisted configs removed.
    out += "],\"journalN\":" + std::to_string(journalN) +
           ",\"dropped\":" + std::to_string(droppedLegacy) + "}";
    g_quests_json = std::move(out);
}
}  // namespace

std::string QuestsJson() {
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    LoadQuestsLocked();
    return g_quests_json.empty() ? std::string("{\"vb\":{},\"quests\":[]}") : g_quests_json;
}

std::string EnumJson(int enum_id) {
    if (enum_id < 0) return "{}";
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    auto* index = g_store ? g_store->Get(kIndexEnums) : nullptr;
    if (!index || !index->ready()) return "{}";          // cache not open yet
    auto bytes = index->ReadFile(enum_id >> 8, enum_id & 0xff);
    if (bytes.empty()) return "{}";
    InputStream s(std::move(bytes));
    std::string out = "{"; bool first = true;
    auto sep = [&] { if (!first) out += ","; first = false; };
    auto key = [&](int k) { out += '"'; out += std::to_string(k); out += "\":"; };
    auto str = [&](int k, const std::string& v) {
        sep(); key(k); out += '"';
        for (char c : v) { if (c == '"' || c == '\\') out += '\\';
                           if ((unsigned char)c >= 0x20) out += c; }
        out += '"';
    };
    auto num = [&](int k, int v) { sep(); key(k); out += std::to_string(v); };
    while (s.remaining() > 0) {
        int op = s.ReadUnsignedByte();
        if (op == 0) break;
        if      (op == 1 || op == 101) s.ReadUnsignedByte(); // key type (101 = extended)
        else if (op == 2 || op == 102) s.ReadUnsignedByte(); // value type (102 = extended)
        else if (op == 3) s.ReadString();                    // default (string)
        else if (op == 4) s.ReadInt();                       // default (int)
        else if (op == 5) { int n = s.ReadUnsignedShort();   // string map, i32 keys
                            for (int i = 0; i < n; ++i) { int k = s.ReadInt(); str(k, s.ReadString()); } }
        else if (op == 6) { int n = s.ReadUnsignedShort();   // int map, i32 keys
                            for (int i = 0; i < n; ++i) { int k = s.ReadInt(); num(k, s.ReadInt()); } }
        else if (op == 7) { s.ReadUnsignedShort();           // string map, u16 keys
                            int n = s.ReadUnsignedShort();
                            for (int i = 0; i < n; ++i) { int k = s.ReadUnsignedShort(); str(k, s.ReadString()); } }
        else if (op == 8) { s.ReadUnsignedShort();           // int map, u16 keys
                            int n = s.ReadUnsignedShort();
                            for (int i = 0; i < n; ++i) { int k = s.ReadUnsignedShort(); num(k, s.ReadInt()); } }
        else if (op == 131 || op == 207 || op == 209) { }    // flags, added build 949
        else break;                                          // unknown opcode -> length unknown
    }
    out += "}";
    return out;
}

// {"id":N,"name":".."} for an NPC id. GetNpc does its own locking + caching and follows morph
// children / applies name overrides, so this just formats its result.
std::string NpcJson(int npc_id) {
    NpcMeta m = GetNpc(npc_id);
    std::string out = "{\"id\":" + std::to_string(m.id) + ",\"name\":\"";
    for (char c : m.name) {
        if (c == '"' || c == '\\') out += '\\';
        if ((unsigned char)c >= 0x20) out += c;
    }
    out += "\"}";
    return out;
}

// ---- Static interface-component defs (js5-3) --------------------------------------------------
namespace {
// One component (js5-3: archive = interface group, file = component id). The
// serialized component is FIXED-LAYOUT (no opcodes): a header (version, type,
// content-type, geometry, parent, hidden) then a type-specific block; the tail
// after that block (option strings, script hooks) is never entered. Layout per
// interface config, live-validated on build 949: all 104103 components in
// 1871 groups decode (version bytes -1..11) and every parent id resolves within
// its group (17 dangling refs exist in the cache data itself). Types 10-16
// carry undocumented variable-size payloads; their headers still decode and the
// payload is left as the (unread) tail.
struct IfaceCompDef {
    int  type = -1, contenttype = 0, parent = -1;
    int  x = 0, y = 0, w = 0, h = 0;
    bool hidden = false;
    std::string text;                 // type 4: default text
    int  sprite = -1;                 // type 5
    int  model  = -1;                 // type 6
};

// Decode the component prefix. Returns 0 on success, the type value for an
// unknown type-specific block (header fields are still valid), 256 when the
// stream ran out mid-prefix (the option/script tail always follows a clean
// prefix -- >= 36 bytes on every live component).
int DecodeIfaceComp(std::vector<std::uint8_t> bytes, IfaceCompDef& c) {
    InputStream s(std::move(bytes));
    int ver = s.ReadUnsignedByte(); if (ver >= 0x80) ver -= 0x100;   // signed; -1 in old groups
    int tb  = s.ReadUnsignedByte();
    c.type = tb & 0x7F;
    if (tb & 0x80) s.ReadString();                    // optional dev name (unused live)
    c.contenttype = s.ReadUnsignedShort();
    c.x = s.ReadShort(); c.y = s.ReadShort();
    c.w = s.ReadShort(); c.h = s.ReadShort();
    int aw = s.ReadUnsignedByte(); if (aw >= 0x80) aw -= 0x100;      // aspect type bytes
    int ah = s.ReadUnsignedByte(); if (ah >= 0x80) ah -= 0x100;
    s.ReadByte(); s.ReadByte();
    int par = s.ReadUnsignedShort();
    c.parent = (par == 0xFFFF) ? -1 : par;
    c.hidden = s.ReadUnsignedByte() != 0;
    switch (c.type) {
    case 0:                                           // container
        s.ReadUnsignedShort();
        if (s.ReadUnsignedShort() & 0x8000) s.ReadInt();
        if (ver == -1) s.ReadUnsignedByte();          // disable-hover bool
        if (ver >= 6)  s.ReadInt();
        if (ver == 6)  s.ReadInt();
        break;
    case 3:                                           // filled figure
        s.ReadInt(); s.ReadUnsignedByte(); s.ReadByte();
        break;
    case 4:                                           // text
        s.ReadBigSmart();                             // font id
        if (ver >= 0) s.ReadUnsignedByte();
        c.text = s.ReadString();
        s.ReadUnsignedByte();                                        // unknown u8
        s.ReadUnsignedByte(); s.ReadUnsignedByte();                  // align h/v
        s.ReadUnsignedByte();                                        // shadow
        s.ReadInt();                                                 // colour
        s.ReadUnsignedByte();                                        // transparency
        if (ver >= 0) s.ReadUnsignedByte();                          // multiline
        break;
    case 5:                                           // sprite
        c.sprite = s.ReadInt();
        s.ReadUnsignedShort(); s.ReadUnsignedByte();                 // rotation, tiling
        if (aw == 4) s.ReadInt();
        if (ah == 4) s.ReadInt();
        s.ReadUnsignedByte(); s.ReadUnsignedByte();                  // transparency, border
        s.ReadInt(); s.ReadUnsignedByte(); s.ReadUnsignedByte();     // unknown, vflip, hflip
        s.ReadInt();                                                 // colour
        if (ver >= 0) s.ReadUnsignedByte();                          // clickmask
        if (ver >= 6) s.ReadInt();
        break;
    case 6: {                                         // model
        c.model = s.ReadBigSmart();
        int mode = s.ReadUnsignedByte();
        if (mode != 0) {
            s.ReadShort(); s.ReadShort();                            // translate x/y
            if (mode == 2) s.ReadShort();
            s.ReadUnsignedShort(); s.ReadUnsignedShort();            // rotate x/y
            s.ReadUnsignedShort(); s.ReadUnsignedShort();            // rotate z, zoom
        }
        s.ReadBigSmart();                                            // anim id
        if (aw != 0) s.ReadUnsignedShort();
        if (ah != 0) s.ReadUnsignedShort();
        break;
    }
    case 9:                                           // line
        s.ReadUnsignedByte(); s.ReadInt(); s.ReadUnsignedByte();
        break;
    case 10: case 11: case 12: case 13: case 15: case 16:
        break;                                        // opaque payload stays in the tail
    default:
        return s.remaining() > 0 ? c.type : 256;      // unknown type -> flag it
    }
    return s.remaining() > 0 ? 0 : 256;
}

std::unordered_map<int, std::string> g_iface_defs_json;   // group id -> JSON (memoized)
}  // namespace

std::string IfaceGroupDefsJson(int group_id) {
    if (group_id < 0) return "{}";
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    auto hit = g_iface_defs_json.find(group_id);
    if (hit != g_iface_defs_json.end()) return hit->second;
    auto* index = g_store ? g_store->Get(kIndexInterfaces) : nullptr;
    if (!index || !index->ready()) return "{}";       // cache not open yet -> retry next call
    const auto& entries = index->ref().entries();
    std::string out = "{\"group\":" + std::to_string(group_id) + ",\"comps\":[";
    bool first = true;
    if (group_id < (int)entries.size()) {
        for (int fid : entries[group_id].valid_file_ids) {
            auto bytes = index->ReadFile(group_id, fid);
            if (bytes.empty()) continue;
            IfaceCompDef c;
            if (DecodeIfaceComp(std::move(bytes), c) == 256) continue;   // undecodable header
            out += first ? "{" : ",{"; first = false;
            out += "\"id\":" + std::to_string(fid) + ",\"t\":" + std::to_string(c.type) +
                   ",\"par\":" + std::to_string(c.parent) +
                   ",\"x\":" + std::to_string(c.x) + ",\"y\":" + std::to_string(c.y) +
                   ",\"w\":" + std::to_string(c.w) + ",\"h\":" + std::to_string(c.h);
            if (c.contenttype) out += ",\"ct\":" + std::to_string(c.contenttype);
            if (c.hidden) out += ",\"hid\":1";
            if (!c.text.empty()) {
                out += ",\"text\":\"";
                for (char ch : c.text) {
                    if (ch == '"' || ch == '\\') out += '\\';
                    if ((unsigned char)ch >= 0x20) out += ch;
                }
                out += "\"";
            }
            if (c.sprite >= 0) out += ",\"sprite\":" + std::to_string(c.sprite);
            if (c.model  >= 0) out += ",\"model\":"  + std::to_string(c.model);
            out += "}";
        }
    }
    out += "]}";
    g_iface_defs_json[group_id] = out;
    return out;
}

#include "OverlayTexColours.h"

// ---- Map terrain colours + flat region render (cache-native) ---------------------------------
// Resolve an underlay (archive 1) / overlay (archive 4) tile colour from the CONFIG index (2)
// -> 0xRRGGBB, or -1 (none). opcode 1 = primary RGB, 7 = secondary, 13 = ternary. Many textured
// overlays carry no primary colour, so fall through primary -> secondary -> ternary.
// Caller holds g_mu. Cached across calls.
static int ConfigColourLocked(int archive, int id) {
    if (id < 0) return -1;
    static std::unordered_map<int, int> cache;
    int key = (archive << 24) | id;
    auto hit = cache.find(key);
    if (hit != cache.end()) return hit->second;
    int col = -1, col2 = -1, col3 = -1;
    int material = -1;                                                            // overlay op 3 = material id
    auto* index = g_store ? g_store->Get(kIndexConfigs) : nullptr;
    if (index && index->ready()) {
        auto bytes = index->ReadFile(archive, id);
        if (!bytes.empty()) {
            InputStream s(std::move(bytes));
            while (s.remaining() > 0) {
                int op = s.ReadUnsignedByte();
                if (op == 0) break;
                else if (op == 1)  { int r = s.ReadUnsignedByte(), g = s.ReadUnsignedByte(), b = s.ReadUnsignedByte(); col  = (r << 16) | (g << 8) | b; }
                else if (op == 7)  { int r = s.ReadUnsignedByte(), g = s.ReadUnsignedByte(), b = s.ReadUnsignedByte(); col2 = (r << 16) | (g << 8) | b; }   // secondary RGB
                else if (op == 13) { int r = s.ReadUnsignedByte(), g = s.ReadUnsignedByte(), b = s.ReadUnsignedByte(); col3 = (r << 16) | (g << 8) | b; }   // ternary RGB
                else if (op == 3)  { material = s.ReadUnsignedShort(); }
                else if (op == 2 || op == 9) s.ReadUnsignedShort();               // u16 fields
                else if (op == 11 || op == 14 || op == 16) s.ReadUnsignedByte();  // u8 fields
                else if (op == 4 || op == 5 || op == 8 || op == 10 || op == 12) { /* bool flag, no payload */ }
                else break;                                                       // unknown opcode -> length unknown, stop
            }
        }
    }
    // Textured overlays are painted from their texture in game; their flat RGB is only a
    // neutral tint, so prefer the material's average texture colour.
    if (archive == 4 && material >= 0) {
        int flat = (col2 >= 0) ? col2 : (col >= 0) ? col : col3;
        if (flat == 0xFF00FF) flat = -1;
        // Only where the flat value cannot be the intended look: absent, or a neutral grey
        // tint. Overlays carrying a real colour keep rendering as they are.
        bool neutral = false;
        if (flat >= 0) {
            int r = (flat >> 16) & 0xff, g = (flat >> 8) & 0xff, b = flat & 0xff;
            int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
            int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
            neutral = (mx - mn) <= 12;
        }
        if (flat < 0 || neutral) {
            int lo = 0, hi = kOverlayMatCount - 1, found = -1;
            while (lo <= hi) { int mid = (lo + hi) / 2;
                if (kOverlayMatIds[mid] == material) { found = mid; break; }
                if (kOverlayMatIds[mid] < material) lo = mid + 1; else hi = mid - 1; }
            if (found >= 0) { cache[key] = kOverlayMatCols[found]; return kOverlayMatCols[found]; }
        }
    }
    // Overlays (archive 4) take SECONDARY colour first (per the game map); everything
    // else takes the primary.
    int out = (archive == 4) ? ((col2 >= 0) ? col2 : (col >= 0) ? col : col3)
                             : ((col >= 0) ? col : (col2 >= 0) ? col2 : col3);
    cache[key] = out;
    return out;
}

static std::string Base64Std(const std::vector<unsigned char>& d) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out; out.reserve((d.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 3 <= d.size(); i += 3) {
        unsigned v = (d[i] << 16) | (d[i + 1] << 8) | d[i + 2];
        out += T[(v >> 18) & 63]; out += T[(v >> 12) & 63]; out += T[(v >> 6) & 63]; out += T[v & 63];
    }
    if (i < d.size()) {
        bool two = (i + 1 < d.size());
        unsigned v = (d[i] << 16) | (two ? (d[i + 1] << 8) : 0);
        out += T[(v >> 18) & 63]; out += T[(v >> 12) & 63];
        out += two ? T[(v >> 6) & 63] : '='; out += '=';
    }
    return out;
}

// Overlay pixel mask for a tile shape: m[a*size+b]=1 where the OVERLAY covers (underlay = the rest).
// Faithful port of the overlay tile-shape draw (size even). a = east, b = top-down within the tile.
static void OverlayMaskLocal(int shape, int size, std::vector<unsigned char>& m) {
    for (auto& v : m) v = 0;
    auto set = [&](int x, int y0, int y1) {
        if (x < 0 || x >= size) return;
        if (y0 < 0) y0 = 0; if (y1 > size) y1 = size;
        for (int y = y0; y < y1; ++y) m[x * size + y] = 1;
    };
    int h = size / 2;
    switch (shape) {
        case 0: for (int x=0;x<size;++x) set(x,0,size); break;
        case 4: case 39: case 41: for (int x=0;x<size;++x) set(x,x,size); break;
        case 5: case 36: case 42: for (int x=0;x<size;++x) set(x,0,size-x); break;
        case 6: case 37: case 43: for (int x=0;x<size;++x) set(x,0,x); break;
        case 7: case 38: case 40: for (int x=0;x<size;++x) set(x,size-x,size); break;
        case 8:  for (int x=0;x<h;++x) set(x,0,size-2*x); break;
        case 9:  for (int x=0;x<size;++x) set(x,0,x/2); break;
        case 10: for (int x=h;x<size;++x) set(x,size-2*(x-h),size); break;
        case 11: for (int x=0;x<size;++x) set(x,(x+size)/2,size); break;
        case 12: for (int x=h;x<size;++x) set(x,0,2*x-size); break;
        case 13: for (int x=0;x<size;++x) set(x,size-1-(x/2),size); break;
        case 14: for (int x=0;x<h;++x) set(x,2*x,size); break;
        case 15: for (int x=0;x<size;++x) set(x,0,(size-x)/2); break;
        case 16: for (int x=0;x<size;++x) set(x,size-2*x,size); break;
        case 17: for (int x=0;x<size;++x) set(x,x/2,size); break;
        case 18: for (int x=0;x<size;++x) set(x,0,2*(size-x)); break;
        case 19: for (int x=0;x<size;++x) set(x,0,(x+size)/2); break;
        case 20: for (int x=0;x<size;++x) set(x,2*x-size,size); break;
        case 21: for (int x=0;x<size;++x) set(x,0,size-1-(x/2)); break;
        case 22: for (int x=0;x<size;++x) set(x,0,2*x); break;
        case 23: for (int x=0;x<size;++x) set(x,(size-x)/2,size); break;
        case 24: for (int x=0;x<h;++x) set(x,0,size); break;
        case 25: for (int x=0;x<size;++x) set(x,0,h); break;
        case 26: for (int x=h;x<size;++x) set(x,0,size); break;
        case 27: for (int x=0;x<size;++x) set(x,h,size); break;
        case 28: for (int x=0;x<h;++x) set(x,h+x,size); break;
        case 29: for (int x=0;x<h;++x) set(x,0,h-x); break;
        case 30: for (int x=h;x<size;++x) set(x,0,x-h); break;
        case 31: for (int x=h;x<size;++x) set(x,size+h-x,size); break;
        case 32: case 45: for (int x=0;x<size;++x) set(x,0,h+x); break;
        case 33: case 46: for (int x=0;x<size;++x) set(x,h-x,size); break;
        case 34: case 47: for (int x=0;x<size;++x) set(x,x-h,size); break;
        case 35: case 44: for (int x=0;x<size;++x) set(x,0,size+h-x); break;
        default: for (int x=0;x<size;++x) set(x,0,size); break;   // unknown -> full overlay
    }
}

// Region object placements, assuming g_mu is already held (defined with RegionLocations below).
static const std::vector<LocPlacement>& RegionLocationsLocked(int region_x, int region_y);

// Wall line pixels for a location of type 0/2/9 at a rotation: a strip along one tile edge (type 0),
// an L of two edges (type 2), or a diagonal band (type 9). Mirrors the wall line draw; widths
// floor at 1 px so walls still show at small tile sizes. Emits tile-local (a,b) into out.
static void WallLine(int ty, int rot, int size, std::vector<std::pair<int, int>>& out) {
    out.clear();
    int q = size / 4; if (q < 1) q = 1;
    int q3 = size - q;
    int e = size / 8; if (e < 1) e = 1;
    auto add = [&](int x, int y0, int y1) {
        if (x < 0 || x >= size) return;
        if (y0 < 0) y0 = 0; if (y1 > size) y1 = size;
        for (int y = y0; y < y1; ++y) out.push_back({ x, y });
    };
    if (ty == 0) {
        if (rot == 0)      for (int x = 0;  x < q;    ++x) add(x, 0, size);
        else if (rot == 1) for (int x = 0;  x < size; ++x) add(x, 0, q);
        else if (rot == 2) for (int x = q3; x < size; ++x) add(x, 0, size);
        else               for (int x = 0;  x < size; ++x) add(x, q3, size);
    } else if (ty == 2) {
        if (rot == 0)      for (int x = 0; x < size; ++x) add(x, 0, x < q  ? size : q);
        else if (rot == 1) for (int x = 0; x < size; ++x) add(x, 0, x < q3 ? q : size);
        else if (rot == 2) for (int x = 0; x < size; ++x) add(x, x < q3 ? q3 : 0, size);
        else               for (int x = 0; x < size; ++x) add(x, x < q  ? 0 : q3, size);
    } else if (ty == 9) {
        if (rot == 0 || rot == 2) for (int x = 0; x < size; ++x) { int base = size - x; add(x, base - e, base + e); }
        else                      for (int x = 0; x < size; ++x) {                       add(x, x - e,    x + e); }
    }
}

// Every chunk of a sound, in order. Each chunk is a COMPLETE Ogg stream carrying its own vorbis
// headers, so concatenating them and handing the result to a decoder yields only the first chunk -
// which is how a 32.7s effect played as 3.5s. Decode each separately and join the PCM.
std::vector<std::vector<std::uint8_t>> SoundOggChunks(int index_id, int sound_id) {
    SqliteIndexFile* idx = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        EnsureInit();
        idx = g_store ? g_store->Get(index_id) : nullptr;
    }
    if (!idx) return {};
    auto raw = idx->ReadRawArchive(sound_id);
    if (raw.empty()) return {};
    auto bytes = Decompress(raw);
    if (bytes.empty()) return {};
    JagaInfo j = ParseJaga(bytes);
    if (!j.ok) return {};

    std::vector<std::vector<std::uint8_t>> out;
    std::size_t pos = j.ogg_off;
    for (std::uint32_t c = 0; c < j.chunks && c < j.lens.size() && pos < bytes.size(); ++c) {
        const std::size_t len = j.lens[c];
        if (!len || pos + len > bytes.size()) break;
        out.emplace_back(bytes.begin() + (std::ptrdiff_t)pos,
                         bytes.begin() + (std::ptrdiff_t)(pos + len));
        pos += len;
    }
    // If the table looks wrong, fall back to "everything after the header" so the sound still
    // plays as far as its first stream rather than not at all.
    if (out.empty() && j.ogg_off < bytes.size())
        out.emplace_back(bytes.begin() + (std::ptrdiff_t)j.ogg_off, bytes.end());
    return out;
}

// One sound's Ogg bytes, header stripped -- a complete, playable .ogg file.
std::vector<std::uint8_t> SoundOgg(int index_id, int sound_id) {
    SqliteIndexFile* idx = nullptr;          // same reasoning as SoundListJson: do not hold
    {                                        // g_mu across the read + inflate
        std::lock_guard<std::mutex> lk(g_mu);
        EnsureInit();
        idx = g_store ? g_store->Get(index_id) : nullptr;
    }
    if (!idx) return {};
    auto raw = idx->ReadRawArchive(sound_id);
    if (raw.empty()) return {};
    auto bytes = Decompress(raw);   // audio indexes use the NXT "ZL" wrapper, not the
                                        // standard container (that is the sprite index)
    if (bytes.empty()) return {};
    JagaInfo j = ParseJaga(bytes);
    if (!j.ok) return {};
    return std::vector<std::uint8_t>(bytes.begin() + (std::ptrdiff_t)j.ogg_off, bytes.end());
}

// Metadata for a page of sounds: [{id, rate, ch, ms, bytes}, ...]. Reading every archive of
// index 14 would decompress ~250MB, so the caller pages through it.
std::string SoundListJson(int index_id, int start_id, int limit) {
    if (limit < 1) limit = 1;
    if (limit > 512) limit = 512;
    // Take the cache mutex ONLY to resolve the index. The scan below reads and inflates up to
    // `limit` archives, and g_mu is the same lock the overlay's per-frame terrain lookups use -
    // holding it across the scan stalled the game's render thread for the whole listing.
    // SqliteIndexFile opens its own transient connection per call, so the reads are safe here.
    SqliteIndexFile* idx = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        EnsureInit();
        idx = g_store ? g_store->Get(index_id) : nullptr;
    }
    if (!idx) return "[]";
    // Ids come from the SQLite table, not the reference table: the audio indexes list fine this
    // way whether or not their ref table parsed.
    const auto ids = idx->ArchiveIdsFrom(start_id, limit);
    std::string out = "[";
    int emitted = 0;
    for (int id : ids) {
        auto raw = idx->ReadRawArchive(id);
        if (raw.empty()) continue;
        auto bytes = Decompress(raw);   // audio indexes use the NXT "ZL" wrapper, not the
                                        // standard container (that is the sprite index)
        JagaInfo j2 = ParseJaga(bytes);
        if (!j2.ok) continue;
        char buf[192];
        std::snprintf(buf, sizeof(buf),
                      "%s{\"id\":%d,\"rate\":%u,\"ch\":%u,\"ms\":%u,\"bytes\":%u}",
                      emitted ? "," : "", id, j2.rate, j2.channels,
                      j2.rate ? (unsigned)((std::uint64_t)j2.samples * 1000ull / j2.rate) : 0u,
                      (unsigned)(bytes.size() - j2.ogg_off));
        out += buf;
        ++emitted;
    }
    out += "]";
    return out;
}

// Rendering a window is expensive - an underlay box blur, a hillshade pass and a per-pixel
// bilinear sample over the whole image - and the panel redraws on a poll, asking for the SAME
// window again while the player stands still. Keep the last few results; the inputs are static
// cache data, so a hit stays valid for the life of the process.
namespace {
struct MapWinCacheEntry { int cx, cy, plane, half, ts; std::string json; };
std::deque<MapWinCacheEntry> g_mapWinCache;      // most-recent first; guarded by g_mu
constexpr std::size_t kMapWinCacheMax = 3;
}  // namespace

// See the header. A clue item states the tile but never the object, so the object is read back
// off the map. Matched by right-click option rather than by name: "Search" is the ordinary case
// and "Open" is what a LOCKED container shows, and those two together are exactly the set of
// clue-searchable things - which beats maintaining a list of names like Crate/Drawers/Chest.
std::string ClueSearchTargetJson(int x, int y, int plane) {
    if (x <= 0 || y <= 0 || plane < 0 || plane > 3) return "{}";
    // A multi-tile object is anchored at its SW corner, so the placement covering this tile can
    // be up to a few tiles away; widen to the neighbouring regions for one on a boundary.
    struct Best { int id = -1; std::string name, action; int dx = 1, dy = 1; int rank = 99; };
    Best best;
    for (int rgx = (x - 4) >> 6; rgx <= ((x + 4) >> 6); ++rgx) {
        for (int rgy = (y - 4) >> 6; rgy <= ((y + 4) >> 6); ++rgy) {
            for (const auto& p : RegionLocations(rgx, rgy)) {
                if (p.plane != plane || p.id < 0) continue;
                LocMeta m = GetLoc(p.id);
                if (m.name.empty()) continue;
                int dx = m.dim_x < 1 ? 1 : m.dim_x, dy = m.dim_y < 1 ? 1 : m.dim_y;
                if (p.rotation == 1 || p.rotation == 3) { int t = dx; dx = dy; dy = t; }
                const int gx = rgx * 64 + p.x, gy = rgy * 64 + p.y;
                if (x < gx || x >= gx + dx || y < gy || y >= gy + dy) continue;
                // Prefer Search over Open: a room can hold both a searchable object and an
                // ordinary door, and only the former is ever the clue target.
                for (std::size_t a = 0; a < m.actions.size(); ++a) {
                    const std::string& act = m.actions[a];
                    int rank = (act == "Search") ? 0 : (act == "Open") ? 1 : 99;
                    if (rank >= best.rank) continue;
                    best.rank = rank; best.id = p.id; best.name = m.name;
                    best.action = act; best.dx = dx; best.dy = dy;
                }
            }
        }
    }
    if (best.id < 0) return "{}";
    auto esc = [](const std::string& in) {          // same inline escaping the other producers use
        std::string o;
        o.reserve(in.size() + 8);
        for (char c : in) {
            if (c == '"' || c == '\\') o.push_back('\\');
            o.push_back(c);
        }
        return o;
    };
    std::string out = "{\"id\":" + std::to_string(best.id);
    out += ",\"name\":\"" + esc(best.name) + "\"";
    out += ",\"action\":\"" + esc(best.action) + "\"";
    out += ",\"dx\":" + std::to_string(best.dx) + ",\"dy\":" + std::to_string(best.dy) + "}";
    return out;
}

std::string MapWindowJson(int cx, int cy, int plane, int half, int ts) {
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    if (plane < 0 || plane > 3) plane = 0;
    if (half < 8 || half > 384) half = 40;       // tiles each side of the centre (panel-controlled zoom)
    if (ts < 2 || ts > 32) ts = 6;               // px per tile (32 = ~3x zoom before upscaling)
    if (ts & 1) ++ts;                            // even keeps the tile-shape split exact
    // Looked up AFTER the clamps, so two requests differing only in an out-of-range argument
    // still share an entry.
    for (std::size_t i = 0; i < g_mapWinCache.size(); ++i) {
        const auto& e = g_mapWinCache[i];
        if (e.cx == cx && e.cy == cy && e.plane == plane && e.half == half && e.ts == ts) {
            if (i == 0) return e.json;
            MapWinCacheEntry tmp = e;
            g_mapWinCache.erase(g_mapWinCache.begin() + (std::ptrdiff_t)i);
            g_mapWinCache.push_front(tmp);
            return g_mapWinCache.front().json;
        }
    }
    const int HALF = half, TS = ts;
    const int WT = 2 * HALF, W = WT * TS;
    std::vector<unsigned char> rgba((size_t)W * W * 4, 0);

    // Column shift (bridge flag): settings bit 0x2 on PLANE 1 lifts the whole column one plane,
    // so the tile you SEE on plane p is stored at p+1. Same rule as EffPlaneLocked (heights).
    // This is piers and bridges, but also the whole Daemonheim castle platform - painting those
    // as flat "deck wood" erased the platform's real ground; render the shifted tile instead.
    auto effPlaneAt = [&](int gx, int gy) -> int {
        if (plane >= 3 || gx < 0 || gy < 0 || gx > 16383 || gy > 16383) return plane;
        const auto& s = RegionTilesLocked(gx >> 6, gy >> 6).settings;
        if (s.empty()) return plane;
        return (s[(std::size_t)(64 + (gx & 63)) * 64 + (gy & 63)] & 0x2) ? plane + 1 : plane;
    };
    auto tileAt = [&](int gx, int gy, int& ul, int& ov, int& sh) {
        ul = ov = sh = -1;
        if (gx < 0 || gy < 0 || gx > 16383 || gy > 16383) return;
        const MapTileData& td = RegionTilesLocked(gx >> 6, gy >> 6);
        if (td.underlay.empty()) return;
        int ep = effPlaneAt(gx, gy);
        int idx = (ep * 64 + (gx & 63)) * 64 + (gy & 63);
        ul = td.underlay[idx]; ov = td.overlay[idx]; sh = td.shape[idx];
        // A shifted column only overrides the base plane where the upper plane really has
        // ground; over open terrain the flag is set on columns whose upper plane is empty.
        if (ep != plane && ul < 1 && ov < 1) {
            int b = (plane * 64 + (gx & 63)) * 64 + (gy & 63);
            ul = td.underlay[b]; ov = td.overlay[b]; sh = td.shape[b];
        }
        // Settings bit 0x8 marks an upper-plane tile that belongs on the GROUND map: cave
        // rims, overhangs and raised structures. Without this they read as void and paint
        // black. Highest flagged plane wins, matching the order the tiles are drawn in.
        if (plane == 0 && !td.settings.empty()) {
            for (int p = 3; p >= 1; --p) {
                std::size_t i2 = (std::size_t)(p * 64 + (gx & 63)) * 64 + (gy & 63);
                if (!(td.settings[i2] & 0x8)) continue;
                if (td.underlay[i2] < 1 && td.overlay[i2] < 1) continue;
                ul = td.underlay[i2]; ov = td.overlay[i2]; sh = td.shape[i2];
                break;
            }
        }
    };
    const int kNoH = -32768;                        // INT16_MIN sentinel = tile carried no height
    auto heightAt = [&](int gx, int gy) -> int {
        if (gx < 0 || gy < 0 || gx > 16383 || gy > 16383) return kNoH;
        const MapTileData& td = RegionTilesLocked(gx >> 6, gy >> 6);
        if (td.heights.empty()) return kNoH;
        return td.heights[(plane * 64 + (gx & 63)) * 64 + (gy & 63)];
    };
    auto shade = [](int col, double f) -> int {     // multiply the tile RGB by a light factor, clamped 0..255
        if (col < 0) return col;
        auto cl = [](double v){ int i = (int)(v + 0.5); return i < 0 ? 0 : (i > 255 ? 255 : i); };
        return (cl(((col >> 16) & 0xff) * f) << 16) | (cl(((col >> 8) & 0xff) * f) << 8) | cl((col & 0xff) * f);
    };
    // Underlay blend: each tile's colour is the average underlay RGB over a (2*UBR+1)^2 box,
    // precomputed over a padded window with a separable box blur (O(1) per tile). Overlays /
    // walls / map-scene icons draw crisply ON TOP.
    // Underlay blend kernel, in tiles. The game map blends ground colour very widely, which is
    // right when a tile is a few pixels - it hides the tile grid. Held at that width while zoomed
    // IN it is why terrain looked low-resolution: every pixel was the average of a 10x10 tile
    // neighbourhood, so no local variation survived however many pixels the tile was drawn at.
    // Tighten the kernel as tiles grow, so zooming in actually reveals terrain instead of a
    // bigger smear. Tiles still blend into their neighbours - just over a smaller radius.
    const int UBR  = (TS >= 24) ? 1 : (TS >= 16) ? 2 : (TS >= 10) ? 3 : 4;
    const int UBLO = -UBR, UBHI = UBR + 1;
    const int PAD = 5, PW = WT + 2 * PAD;   // padding sized for the widest kernel; over-pad is free
    std::vector<int> rawR((size_t)PW * PW, 0), rawG((size_t)PW * PW, 0), rawB((size_t)PW * PW, 0);
    std::vector<unsigned char> rawM((size_t)PW * PW, 0);
    for (int px = 0; px < PW; ++px) for (int py = 0; py < PW; ++py) {
        int u, o, s; tileAt(cx - HALF - PAD + px, cy - HALF - PAD + py, u, o, s);
        if (u < 1) continue;
        int c = ConfigColourLocked(1, u - 1);
        if (c < 0 || c == 0xFF00FF) continue;      // magenta = not a real ground colour
        size_t i = (size_t)px * PW + py;
        rawR[i] = (c >> 16) & 0xff; rawG[i] = (c >> 8) & 0xff; rawB[i] = c & 0xff; rawM[i] = 1;
    }
    std::vector<int> hR((size_t)PW * PW, 0), hG((size_t)PW * PW, 0), hB((size_t)PW * PW, 0), hC((size_t)PW * PW, 0);
    for (int py = 0; py < PW; ++py) for (int px = 0; px < PW; ++px) {
        int sR = 0, sG = 0, sB = 0, sC = 0;
        for (int dx = UBLO; dx <= UBHI; ++dx) { int qx = px + dx; if (qx < 0 || qx >= PW) continue;
            size_t j = (size_t)qx * PW + py; if (!rawM[j]) continue; sR += rawR[j]; sG += rawG[j]; sB += rawB[j]; ++sC; }
        size_t i = (size_t)px * PW + py; hR[i] = sR; hG[i] = sG; hB[i] = sB; hC[i] = sC;
    }
    // Blend colours are kept for a 1-tile ring beyond the window (index (wx+1)*BW+(wy+1)) so the
    // per-pixel bilinear sampling below stays continuous across adjacent windows (no chunk seams).
    const int BW = WT + 2;
    std::vector<int> blendUl((size_t)BW * BW, -1);
    for (int wx = -1; wx <= WT; ++wx) for (int wy = -1; wy <= WT; ++wy) {
        int px = wx + PAD, py = wy + PAD, sR = 0, sG = 0, sB = 0, sC = 0;
        for (int dz = UBLO; dz <= UBHI; ++dz) { int qy = py + dz; if (qy < 0 || qy >= PW) continue;
            size_t j = (size_t)px * PW + qy; sR += hR[j]; sG += hG[j]; sB += hB[j]; sC += hC[j]; }
        if (sC > 0) blendUl[(size_t)(wx + 1) * BW + (wy + 1)] = ((sR / sC) << 16) | ((sG / sC) << 8) | (sB / sC);
    }
    // Height-relief hillshade factor per tile (map lighting): brighten NW-facing slopes,
    // darken SE-facing ones. Precomputed with the same 1-tile ring so it interpolates seamlessly.
    std::vector<float> shadeF((size_t)BW * BW, 1.0f);
    for (int wx = -1; wx <= WT; ++wx) for (int wy = -1; wy <= WT; ++wy) {
        int gx = cx - HALF + wx, gy = cy - HALF + wy;
        int hc = heightAt(gx, gy);
        if (hc == kNoH) continue;
        int hE = heightAt(gx + 1, gy), hW = heightAt(gx - 1, gy), hN = heightAt(gx, gy + 1), hS = heightAt(gx, gy - 1);
        int dxh = (hE != kNoH && hW != kNoH) ? (hE - hW) : 0;
        int dyh = (hN != kNoH && hS != kNoH) ? (hN - hS) : 0;
        double d = 0.014 * (double)(dxh - dyh);
        if (d < -0.18) d = -0.18; else if (d > 0.18) d = 0.18;
        shadeF[(size_t)(wx + 1) * BW + (wy + 1)] = (float)(1.0 + d);
    }
    // Bilinear underlay sample at a pixel centre: interpolates both the blended colour and the
    // hillshade between the four surrounding tile centres. This is what stops the ground looking
    // like a flat-tile mosaic at large TS; a void corner just renormalises over the rest.
    // Per-pixel shade factor, interpolated exactly like the underlay colour but CLAMPED.
    // Overlays were left flat because full relief shading bleached paths white along height
    // cliffs (Prifddinas, a whole raised platform, was the worst case). A narrow band gives
    // them form and a gradient across the tile without that failure.
    auto sampleShade = [&](int wx, int wy, int a, int bb, double lo, double hi) -> double {
        double u = wx + (a + 0.5) / (double)TS - 0.5;
        double v = wy + 1.0 - (bb + 0.5) / (double)TS - 0.5;
        int x0 = (int)std::floor(u), y0 = (int)std::floor(v);
        double fx = u - x0, fy = v - y0, acc = 0, wsum = 0;
        for (int dy2 = 0; dy2 < 2; ++dy2) for (int dx2 = 0; dx2 < 2; ++dx2) {
            double wgt = (dx2 ? fx : 1.0 - fx) * (dy2 ? fy : 1.0 - fy);
            if (wgt <= 0.0) continue;
            int tx = x0 + dx2, ty = y0 + dy2;
            if (tx < -1 || tx > WT || ty < -1 || ty > WT) continue;
            acc += wgt * shadeF[(size_t)(tx + 1) * BW + (ty + 1)];
            wsum += wgt;
        }
        if (wsum <= 0.0) return 1.0;
        double f = acc / wsum;
        return f < lo ? lo : (f > hi ? hi : f);
    };
    // Deterministic dither, keyed on the tile and the pixel within it. Ground here is ONE colour
    // per tile - there is no sub-tile detail in the cache to sample - so at high zoom a tile
    // reads as a solid block however many pixels it is given. A tiny stable jitter breaks that
    // up without inventing features: it never moves, and it is invisible below ~8 px per tile.
    auto dither = [](int tx, int ty, int a, int bb) -> int {
        unsigned h = (unsigned)tx * 73856093u ^ (unsigned)ty * 19349663u
                   ^ (unsigned)a * 83492791u ^ (unsigned)bb * 2971215073u;
        h ^= h >> 13; h *= 1274126177u; h ^= h >> 16;
        return (int)(h % 5) - 2;                       // -2..+2 per channel
    };
    auto jitter = [](int col, int d) -> int {
        if (col < 0 || !d) return col;
        auto cl = [](int q) { return q < 0 ? 0 : (q > 255 ? 255 : q); };
        return (cl(((col >> 16) & 0xff) + d) << 16) | (cl(((col >> 8) & 0xff) + d) << 8)
             | cl((col & 0xff) + d);
    };
    auto sampleUl = [&](int wx, int wy, int a, int bb) -> int {
        double u = wx + (a + 0.5) / (double)TS - 0.5;
        double v = wy + 1.0 - (bb + 0.5) / (double)TS - 0.5;   // bb runs south; v is in world-y tile units
        int x0 = (int)std::floor(u), y0 = (int)std::floor(v);
        double fx = u - x0, fy = v - y0;
        double r = 0, g = 0, b = 0, wsum = 0;
        for (int dy2 = 0; dy2 < 2; ++dy2) for (int dx2 = 0; dx2 < 2; ++dx2) {
            double wgt = (dx2 ? fx : 1.0 - fx) * (dy2 ? fy : 1.0 - fy);
            if (wgt <= 0.0) continue;
            int tx = x0 + dx2, ty = y0 + dy2;
            if (tx < -1 || tx > WT || ty < -1 || ty > WT) continue;
            int cc = blendUl[(size_t)(tx + 1) * BW + (ty + 1)];
            if (cc < 0) continue;
            double ff = shadeF[(size_t)(tx + 1) * BW + (ty + 1)];
            r += wgt * ((cc >> 16) & 0xff) * ff; g += wgt * ((cc >> 8) & 0xff) * ff; b += wgt * (cc & 0xff) * ff;
            wsum += wgt;
        }
        if (wsum <= 0.0) return -1;
        auto cl = [](double q) { int i = (int)(q + 0.5); return i < 0 ? 0 : (i > 255 ? 255 : i); };
        return (cl(r / wsum) << 16) | (cl(g / wsum) << 8) | cl(b / wsum);
    };
    // Shoreline softening: WATER pixels bilinear-sample a combined water/land colour field, so a
    // coast melts from land into water over ~1 tile (a shallow fringe) instead of snapping at hard
    // tile steps. waterCol (1-tile ring) = the shaded colour of a water tile, -1 elsewhere; water =
    // the ocean overlay 112 or a blue-dominant overlay colour (rivers/ponds; heuristic, visual-only).
    // Non-water overlays (roads, floors) keep their crisp flat edges.
    std::vector<int> waterCol((size_t)BW * BW, -1);
    if (TS >= 4) {
        for (int wx = -1; wx <= WT; ++wx) for (int wy = -1; wy <= WT; ++wy) {
            int gx = cx - HALF + wx, gy = cy - HALF + wy;
            int ul, ov, sh; tileAt(gx, gy, ul, ov, sh);
            if (ov < 1) continue;
            int oc = ConfigColourLocked(4, ov - 1);
            if (oc == 0xFF00FF) oc = -1;
            if (ov == 112 && oc == 0xFFFFFF) oc = 0x3D4E63;
            if (oc < 0) continue;
            int rr = (oc >> 16) & 0xff, gg = (oc >> 8) & 0xff, bb2 = oc & 0xff;
            if (!(ov == 112 || (bb2 > rr + 20 && bb2 > gg + 10))) continue;
            waterCol[(size_t)(wx + 1) * BW + (wy + 1)] = oc;   // flat, like every overlay
        }
    }
    auto surfAt = [&](int tx, int ty) -> int {   // water colour on water tiles, else shaded ground blend
        int wc = waterCol[(size_t)(tx + 1) * BW + (ty + 1)];
        if (wc >= 0) return wc;
        int cc = blendUl[(size_t)(tx + 1) * BW + (ty + 1)];
        if (cc < 0) return -1;
        return shade(cc, shadeF[(size_t)(tx + 1) * BW + (ty + 1)]);
    };
    auto sampleSurf = [&](int wx, int wy, int a, int bb) -> int {
        double u = wx + (a + 0.5) / (double)TS - 0.5;
        double v = wy + 1.0 - (bb + 0.5) / (double)TS - 0.5;
        int x0 = (int)std::floor(u), y0 = (int)std::floor(v);
        double fx = u - x0, fy = v - y0;
        double r = 0, g = 0, b = 0, wsum = 0;
        for (int dy2 = 0; dy2 < 2; ++dy2) for (int dx2 = 0; dx2 < 2; ++dx2) {
            double wgt = (dx2 ? fx : 1.0 - fx) * (dy2 ? fy : 1.0 - fy);
            if (wgt <= 0.0) continue;
            int tx = x0 + dx2, ty = y0 + dy2;
            if (tx < -1 || tx > WT || ty < -1 || ty > WT) continue;
            int cc = surfAt(tx, ty);
            if (cc < 0) continue;
            r += wgt * ((cc >> 16) & 0xff); g += wgt * ((cc >> 8) & 0xff); b += wgt * (cc & 0xff);
            wsum += wgt;
        }
        if (wsum <= 0.0) return -1;
        auto cl = [](double q) { int i = (int)(q + 0.5); return i < 0 ? 0 : (i > 255 ? 255 : i); };
        return (cl(r / wsum) << 16) | (cl(g / wsum) << 8) | cl(b / wsum);
    };

    std::vector<unsigned char> mask((size_t)TS * TS);
    bool any = false;
    for (int wx = 0; wx < WT; ++wx) {
        for (int wy = 0; wy < WT; ++wy) {
            int gx = cx - HALF + wx, gy = cy - HALF + wy;
            int ul, ov, sh; tileAt(gx, gy, ul, ov, sh);            // column-shifted tiles resolve to plane+1 inside tileAt
            // deck = a shifted column actually carried by the upper plane.
            bool deck = effPlaneAt(gx, gy) != plane && (ul >= 1 || ov >= 1);
            if (ul < 1 && ov < 1 && !deck) continue;               // no tile data and no shifted column -> genuine void
            int ucol = (ul >= 1) ? blendUl[(size_t)(wx + 1) * BW + (wy + 1)] : -1;
            int ocol = (ov >= 1) ? ConfigColourLocked(4, ov - 1) : -1;
            if (ocol == 0xFF00FF) ocol = -1;                       // magenta = transparent overlay
            if (ov == 112 && ocol == 0xFFFFFF) ocol = 0x3D4E63;    // ocean (white -> blue, per the game map)
            if (deck && ucol < 0 && ocol < 0) ucol = 0x6E5436;     // shifted tile with no colourable floor (plank piers) -> deck wood
            if (ucol < 0 && ocol < 0) ucol = 0x534E47;             // tile HAS data but no resolvable colour (textured city/dungeon floor) -> neutral, not black
            any = true;
            const double tf = shadeF[(size_t)(wx + 1) * BW + (wy + 1)];
            // Overlays (water/roads/floors) stay flat per tile; the underlay ground gets the
            // per-pixel gradient at TS >= 4 (at TS 2 a tile is 4 px, flat is indistinguishable).
            // Ground paints FLAT per tile at map zoom, as the game's map does; interpolating
            // between tile centres washes small terrain patches into their neighbours.
            const bool smoothUl = (TS >= 8) && (ul >= 1);
            const bool tileWater = (TS >= 4) && waterCol[(size_t)(wx + 1) * BW + (wy + 1)] >= 0;
            int ucolFlat = (ucol >= 0) ? shade(ucol, tf) : -1;
            // Overlays take a CLAMPED relief light, not the full one. Unshaded (which is what
            // the game map does) they read as solid blocks once a tile is 16-32 px; fully shaded,
            // they bleach white along height cliffs - Prifddinas, a whole raised platform, was
            // the case that forced them flat before. The narrow band gives shape without that.
            int px0 = wx * TS, py0 = ((WT - 1) - wy) * TS;          // north-up tile origin
            bool useMask = (ocol >= 0);
            if (useMask) OverlayMaskLocal(sh < 0 ? 0 : sh, TS, mask);
            // Detail only where a tile is big enough for it to read; below this a tile is a few
            // pixels and both effects are noise.
            const bool detail = (TS >= 8);
            for (int a = 0; a < TS; ++a) {
                for (int bb = 0; bb < TS; ++bb) {
                    int col;
                    if (useMask && mask[a * TS + bb]) {
                        if (tileWater) { col = sampleSurf(wx, wy, a, bb); if (col < 0) col = ocol; }
                        else if (detail) {
                            // Overlays now take a GENTLE, interpolated light instead of being
                            // flat: enough to give a path or a swamp patch shape across the
                            // tile, tight enough not to bleach at cliffs.
                            col = shade(ocol, sampleShade(wx, wy, a, bb, 0.93, 1.07));
                        }
                        else col = ocol;
                    }
                    else if (smoothUl) { col = sampleUl(wx, wy, a, bb); if (col < 0) col = ucolFlat; }
                    else col = ucolFlat;
                    if (col < 0) col = (ucolFlat >= 0 ? ucolFlat : ocol);  // no holes inside a tile
                    if (col < 0) continue;
                    if (detail) col = jitter(col, dither(cx - HALF + wx, cy - HALF + wy, a, bb));
                    size_t p = ((size_t)(py0 + bb) * W + (px0 + a)) * 4;
                    rgba[p] = (col >> 16) & 0xff; rgba[p + 1] = (col >> 8) & 0xff; rgba[p + 2] = col & 0xff; rgba[p + 3] = 255;
                }
            }
        }
    }
    // Structures built on the levels ABOVE this one (walkways, platforms, building floors)
    // belong on the map: the game's map shows them over the ground, not hidden on their own
    // level. They sit on top, so they draw over this level's overlays too.
    if (plane < 3) {
        std::vector<unsigned char> umask((size_t)TS * TS);
        for (int up = plane + 1; up <= 3; ++up) {
            for (int wx = 0; wx < WT; ++wx) {
                for (int wy = 0; wy < WT; ++wy) {
                    int gx = cx - HALF + wx, gy = cy - HALF + wy;
                    if (gx < 0 || gy < 0 || gx > 16383 || gy > 16383) continue;
                    const MapTileData& td = RegionTilesLocked(gx >> 6, gy >> 6);
                    if (td.overlay.empty()) continue;
                    int uix = (up * 64 + (gx & 63)) * 64 + (gy & 63);
                    int uo = td.overlay[uix];
                    if (uo < 1) continue;
                    int c = ConfigColourLocked(4, uo - 1);
                    if (c < 0 || c == 0xFF00FF) continue;
                    int ush = td.shape[uix];
                    OverlayMaskLocal(ush < 0 ? 0 : ush, TS, umask);
                    int px0 = wx * TS, py0 = ((WT - 1) - wy) * TS;
                    for (int a = 0; a < TS; ++a) for (int bb = 0; bb < TS; ++bb) {
                        if (!umask[a * TS + bb]) continue;
                        size_t p = ((size_t)(py0 + bb) * W + (px0 + a)) * 4;
                        rgba[p] = (c >> 16) & 0xff; rgba[p + 1] = (c >> 8) & 0xff; rgba[p + 2] = c & 0xff; rgba[p + 3] = 255;
                    }
                    any = true;
                }
            }
        }
    }
    // ---- locs: map-scene ICONS (trees/anvils/rocks/... via loc opcode 102) composited at the tile, plus
    //      light WALL lines from type-0/2/9 locs. map parity: a loc with a map-scene draws its icon INSTEAD
    //      of a wall. One pass over the regions' locs handles both. ----
    std::vector<unsigned char> objsOut;   // objects layer: scenery footprints (wtx u16, wty u16, dx u8, dy u8, id u32) for the toggleable client overlay
    {
        EnsureMapscenesLocked();
        std::vector<std::pair<int, int>> wl;
        int rloX = (cx - HALF) >> 6, rhiX = (cx + HALF - 1) >> 6;
        int rloY = (cy - HALF) >> 6, rhiY = (cy + HALF - 1) >> 6;
        for (int rgx = rloX; rgx <= rhiX; ++rgx) {
            for (int rgy = rloY; rgy <= rhiY; ++rgy) {
                const auto& locs = RegionLocationsLocked(rgx, rgy);
                for (const auto& p : locs) {
                    int gx = rgx * 64 + p.x, gy = rgy * 64 + p.y;
                    // Shifted columns also pull their plane+1 locs down (bridge rails, the walls
                    // and map icons on the Daemonheim platform).
                    if (p.plane != plane && !(p.plane == plane + 1 && effPlaneAt(gx, gy) != plane)) continue;
                    int wtx = gx - (cx - HALF), wty = gy - (cy - HALF);
                    if (wtx < 0 || wtx >= WT || wty < 0 || wty >= WT) continue;
                    int px0 = wtx * TS, py0 = ((WT - 1) - wty) * TS;
                    if (p.type == 10 || p.type == 11 || p.type == 22) {   // scenery / ground deco -> objects overlay footprint
                        LocClip lc = LocClipLocked(p.id);
                        int odx = lc.dim_x, ody = lc.dim_y;
                        if (p.rotation == 1 || p.rotation == 3) { int t = odx; odx = ody; ody = t; }
                        objsOut.push_back((unsigned char)(wtx & 0xff)); objsOut.push_back((unsigned char)((wtx >> 8) & 0xff));
                        objsOut.push_back((unsigned char)(wty & 0xff)); objsOut.push_back((unsigned char)((wty >> 8) & 0xff));
                        objsOut.push_back((unsigned char)(odx > 255 ? 255 : odx)); objsOut.push_back((unsigned char)(ody > 255 ? 255 : ody));
                        unsigned oid = (unsigned)p.id;
                        objsOut.push_back((unsigned char)(oid & 0xff)); objsOut.push_back((unsigned char)((oid >> 8) & 0xff));
                        objsOut.push_back((unsigned char)((oid >> 16) & 0xff)); objsOut.push_back((unsigned char)((oid >> 24) & 0xff));
                    }
                    int mf = LocMapFunctionLocked(p.id);
                    if (mf >= 0) {                                     // worldmap MAPLABEL icon (bank/altar/teleport/shop/...)
                        const MapsceneIcon& lic = MaplabelIconLocked(mf);
                        if (lic.w > 0 && lic.h > 0 && !lic.rgba.empty()) {
                            int dw = lic.w, dh = lic.h;                // worldmap icons draw at native sprite size, capped
                            int maxpx = 5 * TS; if (dw > maxpx) dw = maxpx; if (dh > maxpx) dh = maxpx;
                            if (dw < 6) dw = 6; if (dh < 6) dh = 6;
                            int lcx = px0 + TS / 2, lcy = py0 + TS / 2, ix0 = lcx - dw / 2, iy0 = lcy - dh / 2;
                            for (int yy = 0; yy < dh; ++yy) {
                                int iy = iy0 + yy; if (iy < 0 || iy >= W) continue;
                                int sy = yy * lic.h / dh;
                                for (int xx = 0; xx < dw; ++xx) {
                                    int ix = ix0 + xx; if (ix < 0 || ix >= W) continue;
                                    const std::uint8_t* sp = &lic.rgba[((size_t)sy * lic.w + (xx * lic.w / dw)) * 4];
                                    int a = sp[3]; if (a < 8) continue;
                                    size_t pp = ((size_t)iy * W + ix) * 4;
                                    rgba[pp]     = (std::uint8_t)((sp[0] * a + rgba[pp]     * (255 - a)) / 255);
                                    rgba[pp + 1] = (std::uint8_t)((sp[1] * a + rgba[pp + 1] * (255 - a)) / 255);
                                    rgba[pp + 2] = (std::uint8_t)((sp[2] * a + rgba[pp + 2] * (255 - a)) / 255);
                                    rgba[pp + 3] = 255;
                                }
                            }
                            any = true;
                            continue;
                        }
                    }
                    int ms = LocMapsceneLocked(p.id);
                    if (ms >= 0) {                                     // map-scene icon: composite, no wall
                        const MapsceneIcon& ic = MapsceneIconLocked(ms);
                        if (ic.w <= 0 || ic.h <= 0 || ic.rgba.empty()) continue;
                        // Native icon px scaled from the game's 4px/tile reference to our TS, clamped to a few tiles.
                        int dw = ic.w * TS / 4, dh = ic.h * TS / 4;
                        if (dw < TS) dw = TS; if (dh < TS) dh = TS;
                        int maxpx = 5 * TS; if (dw > maxpx) dw = maxpx; if (dh > maxpx) dh = maxpx;
                        int ccx = px0 + TS / 2, ccy = py0 + TS / 2;    // tile centre (north-up)
                        int ix0 = ccx - dw / 2, iy0 = ccy - dh / 2;
                        for (int yy = 0; yy < dh; ++yy) {
                            int iy = iy0 + yy; if (iy < 0 || iy >= W) continue;
                            int sy = yy * ic.h / dh;
                            for (int xx = 0; xx < dw; ++xx) {
                                int ix = ix0 + xx; if (ix < 0 || ix >= W) continue;
                                const std::uint8_t* sp = &ic.rgba[((size_t)sy * ic.w + (xx * ic.w / dw)) * 4];
                                int a = sp[3]; if (a < 8) continue;
                                size_t pp = ((size_t)iy * W + ix) * 4;
                                rgba[pp]     = (std::uint8_t)((sp[0] * a + rgba[pp]     * (255 - a)) / 255);
                                rgba[pp + 1] = (std::uint8_t)((sp[1] * a + rgba[pp + 1] * (255 - a)) / 255);
                                rgba[pp + 2] = (std::uint8_t)((sp[2] * a + rgba[pp + 2] * (255 - a)) / 255);
                                rgba[pp + 3] = 255;
                            }
                        }
                        any = true;
                        continue;
                    }
                    if (p.type != 0 && p.type != 2 && p.type != 9) continue;
                    WallLine(p.type, p.rotation, TS, wl);
                    for (const auto& ab : wl) {
                        int ix = px0 + ab.first, iy = py0 + ab.second;
                        if (ix < 0 || ix >= W || iy < 0 || iy >= W) continue;
                        size_t pp = ((size_t)iy * W + ix) * 4;
                        rgba[pp] = 0xEC; rgba[pp + 1] = 0xEC; rgba[pp + 2] = 0xEC; rgba[pp + 3] = 255;
                    }
                    if (!wl.empty()) any = true;
                }
            }
        }
    }
    if (!any) return "{}";
    // Per-tile "cannot stand here" grid for the window (1 = full-blocked: trees, scenery footprints, void,
    // diagonal walls). Edge-only walls are NOT counted (the tile is still standable). Row-major by wx
    // (index = wx*WT + wy); the panel uses it so suggested scan tiles are always walkable.
    std::vector<unsigned char> blkOut((std::size_t)WT * WT, 0);   // full-block 1/0 -- feeds the scan-tile walkability check (unchanged semantics)
    std::vector<unsigned char> nomove((std::size_t)WT * WT, 0);   // FULL collision flag byte (block + N/E/S/W wall edges) for the walkability OVERLAY (mejrs nomove layer)
    for (int wx = 0; wx < WT; ++wx)
        for (int wy = 0; wy < WT; ++wy) {
            int gx = cx - HALF + wx, gy = cy - HALF + wy;
            if (gx < 0 || gy < 0 || gx > 16383 || gy > 16383) { blkOut[(std::size_t)wx * WT + wy] = 1; nomove[(std::size_t)wx * WT + wy] = kTileBlockFull; continue; }
            const auto& grid = RegionBlockedGridLocked(gx >> 6, gy >> 6);
            std::uint8_t f = grid[(plane * 64 + (gx & 63)) * 64 + (gy & 63)];
            blkOut[(std::size_t)wx * WT + wy] = (f & kTileBlockFull) ? 1 : 0;
            nomove[(std::size_t)wx * WT + wy] = f & (kTileBlockFull | kTileBlockN | kTileBlockS | kTileBlockE | kTileBlockW);
        }
    std::string out = "{\"w\":" + std::to_string(W) + ",\"t\":" + std::to_string(TS) +
           ",\"h\":" + std::to_string(HALF) + ",\"wt\":" + std::to_string(WT) +
           ",\"cx\":" + std::to_string(cx) + ",\"cy\":" + std::to_string(cy) + ",\"p\":" + std::to_string(plane) +
           ",\"png\":\"" + Base64Std(EncodePngRgb(rgba.data(), W, W)) + "\",\"blk\":\"" + Base64Std(blkOut) +
           "\",\"nomove\":\"" + Base64Std(nomove) +
           "\",\"objs\":\"" + Base64Std(objsOut) + "\"}";
    g_mapWinCache.push_front(MapWinCacheEntry{ cx, cy, plane, half, ts, out });
    while (g_mapWinCache.size() > kMapWinCacheMax) g_mapWinCache.pop_back();
    return out;
}


// ---------------------------------------------------------------------------
// Buff / debuff name resolution.
//
// Every player-visible buff/debuff is a StructType (index 22) whose params
// carry the display name and the buff-bar icon id. The param KEYS aren't fixed
// (a game update renumbers them), so they're discovered at runtime from known
// probe strings, then every struct is walked to build sprite/item id -> name.
// ---------------------------------------------------------------------------
namespace {

struct DecodedStruct {
    std::unordered_map<int, int>         ints;
    std::unordered_map<int, std::string> strs;
};

// opcode 249 = a param block (count, then per param a type byte, a 24-bit key,
// and an int or string value). Any other opcode ends the record.
bool DecodeStructFile(std::vector<std::uint8_t> bytes, DecodedStruct& out) {
    if (bytes.empty()) return false;
    InputStream s(std::move(bytes));
    while (s.remaining() > 0) {
        int op = s.ReadUnsignedByte();
        if (op == 0) break;
        if (op != 249) break;                       // unknown opcode -> stop, keep what we have
        int len = s.ReadUnsignedByte();
        for (int i = 0; i < len && s.remaining() > 0; ++i) {
            bool isStr = s.ReadUnsignedByte() == 1;
            int  key   = s.Read24BitInt();
            if (isStr) out.strs[key] = s.ReadString();
            else       out.ints[key] = s.ReadInt();
        }
    }
    return !out.ints.empty() || !out.strs.empty();
}

// Collision rank: prefer short, punctuation-free labels over tooltip sentences.
int BuffNameScore(const std::string& s) {
    int punct = 0;
    for (char c : s) if (c == '.' || c == ',' || c == '(' || c == ')' || c == '%') ++punct;
    return (int)s.size() + punct + ((int)s.size() > 40 ? 10 : 0);
}

// First line only; strip <...> tags, trailing whitespace, and a trailing
// " Active" (redundant once it's on the buff bar).
std::string CleanBuffName(std::string name) {
    auto br = name.find("<br>");
    if (br != std::string::npos) name.resize(br);
    for (;;) {
        auto lt = name.find('<'); if (lt == std::string::npos) break;
        auto gt = name.find('>', lt); if (gt == std::string::npos) break;
        name.erase(lt, gt - lt + 1);
    }
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t' ||
                            name.back() == '\r' || name.back() == '\n'))
        name.pop_back();
    static const std::string kActive = " Active";
    if (name.size() >= kActive.size() &&
        name.compare(name.size() - kActive.size(), kActive.size(), kActive) == 0) {
        name.resize(name.size() - kActive.size());
        while (!name.empty() && name.back() == ' ') name.pop_back();
    }
    return name;
}

// Item-backed buffs (overloads, flasks) store an item id; the item's cache
// name is the right label. Strip a dose suffix "(4)" and a trailing " Active".
std::string BuffItemName(int id) {
    if (id <= 0) return {};
    std::string n = ResolveLocked(id).name;
    if (n.empty() || n == "NoName") return {};
    if (n.size() >= 4 && n.back() == ')') {
        auto open = n.rfind(" (");
        if (open != std::string::npos) {
            bool digits = open + 2 < n.size();
            for (std::size_t i = open + 2; i + 1 < n.size(); ++i)
                if (!std::isdigit((unsigned char)n[i])) { digits = false; break; }
            if (digits) n.resize(open);
        }
    }
    static const std::string kActive = " Active";
    if (n.size() >= kActive.size() &&
        n.compare(n.size() - kActive.size(), kActive.size(), kActive) == 0)
        n.resize(n.size() - kActive.size());
    return n;
}

// Build g_buff_names / g_debuff_names from the struct cache. Caller holds g_mu.
void LoadBuffNamesLocked() {
    if (g_buffs_loaded) return;
    auto* index = g_store ? g_store->Get(kIndexStructs) : nullptr;
    if (!index || !index->ready()) return;          // cache not open yet -> retry next call
    g_buffs_loaded = true;

    std::vector<DecodedStruct> structs;
    const auto& entries = index->ref().entries();
    for (int a = 0; a < (int)entries.size(); ++a) {
        for (int fid : entries[a].valid_file_ids) {
            DecodedStruct ds;
            if (DecodeStructFile(index->ReadFile(a, fid), ds))
                structs.push_back(std::move(ds));
        }
    }
    if (structs.empty()) return;

    struct Probe { const char* name; bool item; int bod; };
    static const Probe PROBES[] = {
        { "Bone Shield", false, 0 }, { "Life Points Boosted", false, 0 },
        { "Perfect Build", false, 0 }, { "Reprisal", false, 0 },
        { "Haunted", false, 1 }, { "Bleeding", false, 1 },
        { "Elder Overload Active", true, 0 }, { "Overload Active", true, 0 },
        { "Supreme Overload Active", true, 0 },
    };

    // Step 1: discover the name key, the sprite/item key(s), and the
    // buff-or-debuff key from structs that contain a probe string.
    int nameKey = -1;
    std::unordered_map<int, bool> spriteKeyIsItem;          // key -> value is an item id
    std::vector<const DecodedStruct*> matchStructs;
    std::vector<const Probe*>         matchProbes;
    for (const auto& s : structs) {
        const Probe* hit = nullptr; int candName = -1;
        for (const auto& kv : s.strs) {
            for (const auto& p : PROBES)
                if (kv.second == p.name) { hit = &p; candName = kv.first; break; }
            if (hit) break;
        }
        if (!hit) continue;
        int candSprite = -1, best = 0;
        for (const auto& kv : s.ints)
            if (kv.second > best && kv.second >= 100 && kv.second <= 200000) {
                best = kv.second; candSprite = kv.first;
            }
        if (candSprite < 0) continue;
        if (nameKey < 0) nameKey = candName;
        spriteKeyIsItem[candSprite] = hit->item;
        matchStructs.push_back(&s);
        matchProbes.push_back(hit);
    }
    if (nameKey < 0 || spriteKeyIsItem.empty()) return;

    int bodKey = -1;
    {
        std::unordered_set<int> cand;
        for (const auto& kv : matchStructs.front()->ints)
            if (kv.second == 0 || kv.second == 1) cand.insert(kv.first);
        for (std::size_t i = 0; i < matchStructs.size(); ++i) {
            if (matchProbes[i]->bod < 0) continue;
            for (auto it = cand.begin(); it != cand.end();) {
                auto p = matchStructs[i]->ints.find(*it);
                if (p == matchStructs[i]->ints.end() || p->second != matchProbes[i]->bod)
                    it = cand.erase(it);
                else ++it;
            }
        }
        for (const auto& kv : spriteKeyIsItem) cand.erase(kv.first);
        cand.erase(nameKey);
        if (!cand.empty()) bodKey = *cand.begin();
    }

    // Step 2: walk every struct carrying the name key; for each discovered
    // sprite/item key present, map its value -> name into the right bar.
    for (const auto& s : structs) {
        auto nit = s.strs.find(nameKey);
        if (nit == s.strs.end() || nit->second.empty()) continue;
        std::string fallback = CleanBuffName(nit->second);
        if (fallback.empty()) continue;
        int bod = 0;
        if (bodKey >= 0) { auto b = s.ints.find(bodKey); if (b != s.ints.end()) bod = b->second; }
        auto& target = (bod == 1) ? g_debuff_names : g_buff_names;
        for (const auto& kv : spriteKeyIsItem) {
            auto sp = s.ints.find(kv.first);
            if (sp == s.ints.end() || sp->second <= 0) continue;
            int id = sp->second;
            // Remember whether this icon id is an item or a sprite, so the buff
            // bar can render it from the right source (the +0x188 widget value
            // alone is ambiguous -- e.g. Bone Shield's 30099 is an item id).
            g_buff_icon_item[id] = kv.second;
            std::string name = kv.second ? BuffItemName(id) : std::string();
            if (name.empty()) name = fallback;
            auto ex = target.find(id);
            if (ex == target.end() || BuffNameScore(name) < BuffNameScore(ex->second))
                target[id] = std::move(name);
            // Kind: text-builder params 8110-8113 say whether the slot shows a
            // countdown timer, a count, a percentage, or a custom string.
            int kind = 0;
            { auto t = s.ints.find(8112); if (t != s.ints.end() && t->second) kind |= 1; }  // timer
            { auto t = s.ints.find(8110); if (t != s.ints.end() && t->second) kind |= 2; }  // count
            { auto t = s.ints.find(8111); if (t != s.ints.end() && t->second) kind |= 4; }  // count is %
            { auto t = s.ints.find(8113); if (t != s.ints.end() && t->second) kind |= 8; }  // custom string
            if (kind) g_buff_kind[id] |= kind;
        }
    }
}

// Exact, then +/-1: RS3 stores inactive/active icon pairs at adjacent ids
// (e.g. Bloodlust struct records 23876, the active bar icon is 23875).
std::string LookupBuffNameAdj(int id, const std::unordered_map<int, std::string>& m) {
    auto it = m.find(id);     if (it != m.end()) return it->second;
    it = m.find(id - 1);      if (it != m.end()) return it->second;
    it = m.find(id + 1);      if (it != m.end()) return it->second;
    return {};
}

}  // namespace

// Ability configs from the struct cache (index 22). Each real ability is a StructType carrying
// param 2794 (name), 2799 (tier: 1 basic / 2 threshold / 3 defensive / 4 ultimate / 5 special /
// 7 utility), 2795 (description), 4650 (unlock text), 2796 (cooldown in 0.6-s game ticks) and
// 2802 (ability/sprite id == the action-bar slot id). Cosmetic ability overrides carry no 2799,
// so requiring a valid tier keeps only genuine abilities. Walked once (version-static) and cached,
// keyed by ability NAME so the abilities panel joins its live slot names to real cache data instead
// of a shipped snapshot. {"Overpower":{"t":4,"d":"Strike ...","u":""}, ...}. {} until the cache opens.
// Ability cooldown-clock varc pairs, parsed STRAIGHT from the raw bytecode of CS2
// script 6506 in the player's own cache (js5-12, archive == script id) -- no sidecar,
// no extraction, nothing shipped or hardcoded. Script 6506 is the game's cooldown-clock
// resolver: one switch over ability STRUCT ids where each case body pushes the two
// varc-int ids ([castClock, readyClock], CLIENTCLOCK 50/s cycles) and returns.
//
// CS2 opcode ids are build-shuffled (the game map's "calibration" problem), so this decodes NO
// opcodes at all. Two structural facts carry the whole parse:
//  - the FOOTER layout is stable: ...ops... [6x u16 + u32 arg/local/op counts]
//    [switch block] [u16 switch-block size]. Switch block = u8 switch count, per switch
//    a u16 case count then (value i32, jump u32) per case -- and consuming it must land
//    EXACTLY at size-2 or the parse is rejected.
//  - every case body starts with the same two 6-byte varc-push ops:
//    [opcode u16][0x02 <varc u16 BE> 0x00], 6 bytes apart, identical opcode bytes.
//    Enumerate those pairs in byte order and zip with the DISTINCT jumps ascending
//    (jump order == body order). Excess bodies at the front are the pre-switch prelude
//    (the param-2976 global-cooldown branch); more than a few extra = the script's
//    shape changed -> fail closed, emit nothing (the panel then simply shows no clock).
std::unordered_map<int, std::pair<int, int>> AbilityCooldownVarcsLocked() {
    std::unordered_map<int, std::pair<int, int>> out;
    auto* idx = g_store ? g_store->Get(kIndexClientScript) : nullptr;
    if (!idx || !idx->ready()) return out;
    std::vector<std::uint8_t> d = idx->ReadFile(6506, 0);
    const std::size_t n = d.size();
    if (n < 64) return out;
    auto u16 = [&](std::size_t o) { return (int)((d[o] << 8) | d[o + 1]); };
    auto i32 = [&](std::size_t o) {
        return (int)(((std::uint32_t)d[o] << 24) | ((std::uint32_t)d[o + 1] << 16) |
                     ((std::uint32_t)d[o + 2] << 8) | d[o + 3]);
    };
    const std::size_t tlen = (std::size_t)u16(n - 2);
    if (tlen + 2 + 16 >= n) return out;
    const std::size_t ts = n - 2 - tlen;                 // switch-block start
    std::size_t o = ts;
    const int nsw = d[o++];
    if (nsw < 1 || nsw > 8) return out;
    std::vector<std::pair<int, int>> cases;              // (struct id, jump)
    for (int s = 0; s < nsw; ++s) {
        if (o + 2 > n - 2) return out;
        const int cc = u16(o); o += 2;
        if (cc < 1 || cc > 2048 || o + (std::size_t)cc * 8 > n - 2) return out;
        for (int c = 0; c < cc; ++c) { cases.push_back({ i32(o), i32(o + 4) }); o += 8; }
    }
    if (o != n - 2) return out;                          // must consume EXACTLY
    std::vector<int> jumps;
    for (const auto& cv : cases) jumps.push_back(cv.second);
    std::sort(jumps.begin(), jumps.end());
    jumps.erase(std::unique(jumps.begin(), jumps.end()), jumps.end());
    const std::size_t opend = ts - 16;                   // ops end before the count block
    std::vector<std::pair<int, int>> bodies;             // (varcA, varcB) in byte order
    for (std::size_t i = 1; i + 12 <= opend; ++i) {
        if (d[i + 2] != 0x02 || d[i + 5] != 0x00) continue;
        if (d[i + 8] != 0x02 || d[i + 11] != 0x00) continue;
        if (d[i] != d[i + 6] || d[i + 1] != d[i + 7]) continue;   // same push opcode
        const int a = u16(i + 3), b = u16(i + 9);
        if (!a || !b) continue;
        bodies.push_back({ a, b });
        i += 11;                                         // past this pair (++ makes 12)
    }
    if (bodies.size() < jumps.size()) return out;
    const std::size_t extra = bodies.size() - jumps.size();
    if (extra > 4) return out;                           // shape changed -> fail closed
    std::unordered_map<int, std::pair<int, int>> byJump;
    for (std::size_t k = 0; k < jumps.size(); ++k) byJump[jumps[k]] = bodies[k + extra];
    for (const auto& cv : cases) {
        auto it = byJump.find(cv.second);
        if (it != byJump.end()) out[cv.first] = it->second;
    }
    return out;
}

std::string AbilityConfigsJson() {
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    static std::string cached;
    if (!cached.empty()) return cached;
    auto* index = g_store ? g_store->Get(kIndexStructs) : nullptr;
    if (!index || !index->ready()) return "{}";          // cache not open yet -> retry, don't cache
    auto jstr = [](const std::string& v) {
        std::string r = "\"";
        for (char c : v) { if (c == '"' || c == '\\') r += '\\';
                           if ((unsigned char)c >= 0x20) r += c; }
        r += '"'; return r;
    };
    std::string out = "{"; bool first = true;
    std::unordered_map<std::string, char> seen;
    // struct id -> [castVarc, readyVarc], from the player's own script 6506 (see above).
    // Empty (e.g. js5-12 absent, or a layout change) just means no "v" fields.
    const auto cdPairs = AbilityCooldownVarcsLocked();
    const auto& entries = index->ref().entries();
    for (int a = 0; a < (int)entries.size(); ++a) {
        for (int fid : entries[a].valid_file_ids) {
            DecodedStruct ds;
            if (!DecodeStructFile(index->ReadFile(a, fid), ds)) continue;
            auto itName = ds.strs.find(2794);
            auto itTier = ds.ints.find(2799);
            if (itName == ds.strs.end() || itTier == ds.ints.end()) continue;
            int tier = itTier->second;
            if (tier != 1 && tier != 2 && tier != 3 && tier != 4 && tier != 5 && tier != 7) continue;
            const std::string& name = itName->second;
            if (name.empty() || !seen.emplace(name, 1).second) continue;   // first canonical per name
            out += first ? "" : ","; first = false;
            out += jstr(name) + ":{\"t\":" + std::to_string(tier);
            auto itD = ds.strs.find(2795);
            if (itD != ds.strs.end() && !itD->second.empty()) out += ",\"d\":" + jstr(itD->second);
            auto itU = ds.strs.find(4650);
            if (itU != ds.strs.end() && !itU->second.empty()) out += ",\"u\":" + jstr(itU->second);
            // Cooldown wiring for the panel:
            //   "c" = the ability's cooldown in GAME TICKS (param 2796, 0.6 s each)
            //   "i" = the ability id the action-bar slot carries at widget+0x188 (param 2802)
            //   "s" = this struct's id -- the key CS2 script 6506 switches on to pick the
            //         cooldown-clock varc pair; the panel joins against the live CS2
            //         extraction rather than any shipped table
            //   "g" = param 2976 set: the ability uses the GLOBAL-cooldown varc pair, so
            //         script 6506's per-struct table does not apply
            auto itCd = ds.ints.find(2796);
            if (itCd != ds.ints.end() && itCd->second > 0) out += ",\"c\":" + std::to_string(itCd->second);
            auto itId = ds.ints.find(2802);
            if (itId != ds.ints.end() && itId->second > 0) out += ",\"i\":" + std::to_string(itId->second);
            out += ",\"s\":" + std::to_string(a * 32 + fid);
            auto itG = ds.ints.find(2976);
            const bool gcd = itG != ds.ints.end() && itG->second == 1;
            if (gcd) out += ",\"g\":1";
            //   "v" = this ability's cooldown-clock varc pair [castClock, readyClock]
            //         (CLIENTCLOCK 50/s cycles), from script 6506 -- skipped for
            //         global-pair abilities, whose clock is the shared GCD pair
            if (!gcd) {
                auto pv = cdPairs.find(a * 32 + fid);
                if (pv != cdPairs.end())
                    out += ",\"v\":[" + std::to_string(pv->second.first) + ","
                         + std::to_string(pv->second.second) + "]";
            }
            out += "}";
        }
    }
    out += "}";
    cached = out;
    return cached;
}

// Raw param map of ONE StructType (js5-22; struct id = archive*32 + file):
// {"ints":{"<key>":v,..},"strs":{"<key>":"v",..}}, {} when absent/unreadable. Generic
// accessor so panels can read cache struct definitions (e.g. daily-challenge structs:
// param 1266 name, 4940 name suffix, 1273 description, 2235 target, 1271 icon sprite)
// without a bespoke C++ decoder per feature.
std::string StructParamsJson(int structId) {
    if (structId < 0) return "{}";
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    auto* index = g_store ? g_store->Get(kIndexStructs) : nullptr;
    if (!index || !index->ready()) return "{}";
    const auto& entries = index->ref().entries();
    int a = structId >> 5, f = structId & 31;
    if (a < 0 || a >= (int)entries.size()) return "{}";
    DecodedStruct ds;
    if (!DecodeStructFile(index->ReadFile(a, f), ds)) return "{}";
    auto jstr = [](const std::string& v) {
        std::string r = "\"";
        for (char c : v) { if (c == '"' || c == '\\') r += '\\';
                           if ((unsigned char)c >= 0x20) r += c; }
        r += '"'; return r;
    };
    std::string out = "{\"ints\":{"; bool first = true;
    for (const auto& kv : ds.ints) {
        out += first ? "" : ","; first = false;
        out += "\"" + std::to_string(kv.first) + "\":" + std::to_string(kv.second);
    }
    out += "},\"strs\":{"; first = true;
    for (const auto& kv : ds.strs) {
        out += first ? "" : ","; first = false;
        out += "\"" + std::to_string(kv.first) + "\":" + jstr(kv.second);
    }
    out += "}}";
    return out;
}

// ---- HUD panel mount registry (enum 7716 + panel structs) -------------------------------------
namespace {
// Content INTERFACE GROUP id -> mount comp SUB under group 1477. Enum 7716 (js5-17) maps each
// HUD content-slot id to a panel StructType (js5-22); the slot ids themselves are NOT group
// ids (live-disproven 2026-08-02: keys are 0-46/1000-1053/2000-2008). The group link is params
// 3514-3517 = the panel's CONTENT interface comps packed (group<<16)|sub (Skills 320:0,
// Prayers 1457:2, Backpack 1474:0; some panels carry none). Param 3503 packs the panel's
// mount comp the same way; every registered mount sits under 1477 (the gameframe), so only
// the sub is kept (a non-1477 mount is skipped rather than guessed at). Built once the
// enum + struct indexes are ready; until then every lookup retries the build.
std::unordered_map<int, int> g_panel_mounts;
bool                         g_panel_mounts_built = false;

void BuildPanelMountsLocked() {
    if (g_panel_mounts_built) return;
    auto* enums   = g_store ? g_store->Get(kIndexEnums)   : nullptr;
    auto* structs = g_store ? g_store->Get(kIndexStructs) : nullptr;
    if (!enums || !enums->ready() || !structs || !structs->ready()) return;
    auto bytes = enums->ReadFile(7716 >> 8, 7716 & 0xff);
    if (bytes.empty()) return;
    // Same opcode walk as EnumJson, int maps only (ops 6/8) -- 7716 is slot id -> struct id.
    std::vector<std::pair<int, int>> pairs;
    InputStream s(std::move(bytes));
    while (s.remaining() > 0) {
        int op = s.ReadUnsignedByte();
        if (op == 0) break;
        if      (op == 1 || op == 101) s.ReadUnsignedByte();
        else if (op == 2 || op == 102) s.ReadUnsignedByte();
        else if (op == 3) s.ReadString();
        else if (op == 4) s.ReadInt();
        else if (op == 5) { int n = s.ReadUnsignedShort();
                            for (int i = 0; i < n; ++i) { s.ReadInt(); s.ReadString(); } }
        else if (op == 6) { int n = s.ReadUnsignedShort();
                            for (int i = 0; i < n; ++i) { int k = s.ReadInt(); pairs.emplace_back(k, s.ReadInt()); } }
        else if (op == 7) { s.ReadUnsignedShort(); int n = s.ReadUnsignedShort();
                            for (int i = 0; i < n; ++i) { s.ReadUnsignedShort(); s.ReadString(); } }
        else if (op == 8) { s.ReadUnsignedShort(); int n = s.ReadUnsignedShort();
                            for (int i = 0; i < n; ++i) { int k = s.ReadUnsignedShort(); pairs.emplace_back(k, s.ReadInt()); } }
        else if (op == 131 || op == 207 || op == 209) { }
        else break;
    }
    const auto& entries = structs->ref().entries();
    for (auto& [slot, structId] : pairs) {
        if (structId < 0) continue;
        int a = structId >> 5, f = structId & 31;
        if (a < 0 || a >= (int)entries.size()) continue;
        DecodedStruct ds;
        if (!DecodeStructFile(structs->ReadFile(a, f), ds)) continue;
        auto it = ds.ints.find(3503);
        if (it == ds.ints.end() || (it->second >> 16) != 1477) continue;
        int mount_sub = it->second & 0xFFFF;
        for (int p = 3514; p <= 3517; ++p) {                 // content comps -> hosted group ids
            auto ct = ds.ints.find(p);
            if (ct == ds.ints.end()) continue;
            int cg = ct->second >> 16;
            if (cg > 0 && cg != 1477 && !g_panel_mounts.count(cg)) g_panel_mounts[cg] = mount_sub;
        }
        (void)slot;   // slot ids are registry-internal, not group ids -- never key on them
    }
    g_panel_mounts_built = true;
}
}  // namespace

int PanelMountComp(int group_id) {
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    BuildPanelMountsLocked();
    auto it = g_panel_mounts.find(group_id);
    return it == g_panel_mounts.end() ? -1 : it->second;
}

// Raw op-249 param map of ONE item (js5-19; archive = id>>8, file = id&0xff):
// {"ints":{"<key>":v,..},"strs":{"<key>":"v",..}}, {} when absent. ItemInfoJson only
// carries name/limit/value, and ItemType.cpp parses params purely to sniff the
// augmentation marker, so this is the only way a panel can read them. Used by the
// Player-Owned Ports crew list (3080 icon sprite, 3081-3084 stats, 3093/3094 +
// 3095/3096 = cost pairs of (resource id, amount)).
std::string ItemParamsJson(int item_id) {
    if (item_id < 0) return "{}";
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    auto* index = g_store ? g_store->Get(kIndexItems) : nullptr;
    if (!index || !index->ready()) return "{}";
    auto bytes = index->ReadFile(item_id >> 8, item_id & 0xff);
    if (bytes.empty()) return "{}";
    ItemDef def = DecodeItem(item_id, std::move(bytes));   // shared decoder keeps op-249 params
    const auto& ints = def.params_i;
    const auto& strs = def.params_s;
    if (ints.empty() && strs.empty()) return "{}";
    auto jstr = [](const std::string& v) {
        std::string r = "\"";
        for (char c : v) { if (c == '"' || c == '\\') r += '\\';
                           if ((unsigned char)c >= 0x20) r += c; }
        r += '"'; return r;
    };
    std::string out = "{\"ints\":{"; bool first = true;
    for (const auto& kv : ints) {
        out += first ? "" : ","; first = false;
        out += "\"" + std::to_string(kv.first) + "\":" + std::to_string(kv.second);
    }
    out += "},\"strs\":{"; first = true;
    for (const auto& kv : strs) {
        out += first ? "" : ","; first = false;
        out += "\"" + std::to_string(kv.first) + "\":" + jstr(kv.second);
    }
    out += "}}";
    return out;
}

// All DBRows of one master table (archive 41), decoded with the validated row shape
// (see LoadPerkNamesLocked): [{"f":fileId,"i":{"<col>":[ints..]},"s":{"<col>":[strs..]}},..].
// Generic accessor for panels that join a table by column value (e.g. Meg's cases,
// DBTable 9: col 0 case number, col 2 case name, col 9 runeday). Capped at 2000 rows.
std::string DbRowsJson(int masterTable) {
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    auto* index = g_store ? g_store->Get(kIndexConfigs) : nullptr;
    if (!index || !index->ready()) return "[]";
    constexpr int kDbRowsArchive = 41;
    // Table ids are CS2-space: id = sub*128 + master, while the row's op-4 tag is
    // master*256 + sub. Ids < 128 keep the historical any-subtable match (their callers
    // predate this and their data sits at sub 0); ids >= 128 - e.g. the Leagues tables
    // 326-336 = masters 70-80 at sub 2 - REQUIRE the exact (master, sub) pair, because
    // the same master's other subtables hold unrelated rows (verified live: master 72
    // sub 0 has 871 foreign rows beside the 7 sub-2 league tiers).
    const int wantMaster = masterTable & 127, wantSub = masterTable >> 7;
    const auto& entries = index->ref().entries();
    if ((int)entries.size() <= kDbRowsArchive) return "[]";
    auto jstr = [](const std::string& v) {
        std::string r = "\"";
        for (char c : v) { if (c == '"' || c == '\\') r += '\\';
                           if ((unsigned char)c >= 0x20) r += c; }
        r += '"'; return r;
    };
    std::string out = "["; int emitted = 0;
    for (int fid : entries[kDbRowsArchive].valid_file_ids) {
        if (emitted >= 2000) break;
        auto bytes = index->ReadFile(kDbRowsArchive, fid);
        if (bytes.size() < 2) continue;
        InputStream s(std::move(bytes));
        int master = -1, sub = 0; bool bad = false;
        std::map<int, std::vector<int>>         icols;
        std::map<int, std::vector<std::string>> scols;
        while (s.remaining() > 0 && !bad) {
            int op = s.ReadUnsignedByte();
            if (op == 0) break;
            if (op == 4) {                       // table tag: (master<<8)|subtable when >= 256
                int t = s.ReadUnsignedSmart();
                master = t >= 256 ? (t >> 8) : t;
                sub    = t >= 256 ? (t & 0xff) : 0;
                continue;
            }
            if (op != 3) break;
            s.ReadUnsignedByte();                // totalCols (unused)
            while (s.remaining() > 0) {
                int b = s.ReadUnsignedByte();
                if (b == 0xFF) break;
                int columnId = b & 0x3F;
                int subN = s.ReadUnsignedByte();
                if (subN <= 0) continue;
                std::vector<int> types((std::size_t)subN);
                for (int i = 0; i < subN; ++i) types[i] = s.ReadUnsignedSmart();
                int rowCount = s.ReadUnsignedSmart();
                for (int r = 0; r < rowCount && !bad; ++r) {
                    for (int sub = 0; sub < subN; ++sub) {
                        if (s.remaining() <= 0) { bad = true; break; }
                        int col = columnId + sub;
                        if (types[sub] == 0x24) scols[col].push_back(s.ReadString());
                        else                    icols[col].push_back(s.ReadInt());
                    }
                }
            }
        }
        if (master != wantMaster || (icols.empty() && scols.empty())) continue;
        if (wantSub && sub != wantSub) continue;
        out += (emitted ? "," : ""); ++emitted;
        out += "{\"f\":" + std::to_string(fid) + ",\"i\":{";
        bool f1 = true;
        for (const auto& kv : icols) {
            out += (f1 ? "" : ","); f1 = false;
            out += "\"" + std::to_string(kv.first) + "\":[";
            for (std::size_t i = 0; i < kv.second.size(); ++i)
                out += (i ? "," : "") + std::to_string(kv.second[i]);
            out += "]";
        }
        out += "},\"s\":{"; f1 = true;
        for (const auto& kv : scols) {
            out += (f1 ? "" : ","); f1 = false;
            out += "\"" + std::to_string(kv.first) + "\":[";
            for (std::size_t i = 0; i < kv.second.size(); ++i)
                out += (i ? "," : "") + jstr(kv.second[i]);
            out += "]";
        }
        out += "}}";
    }
    out += "]";
    return out;
}

std::string GetBuffName(int id) {
    if (id <= 0) return {};
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    LoadBuffNamesLocked();
    std::string n = LookupBuffNameAdj(id, g_buff_names);
    return n.empty() ? LookupBuffNameAdj(id, g_debuff_names) : n;
}

std::string GetDebuffName(int id) {
    if (id <= 0) return {};
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    LoadBuffNamesLocked();
    std::string n = LookupBuffNameAdj(id, g_debuff_names);
    return n.empty() ? LookupBuffNameAdj(id, g_buff_names) : n;
}

int GetBuffKind(int id) {
    if (id <= 0) return 0;
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    LoadBuffNamesLocked();
    for (int d : { 0, -1, 1 }) {                 // same inactive/active adjacency as the name
        auto it = g_buff_kind.find(id + d);
        if (it != g_buff_kind.end()) return it->second;
    }
    return 0;
}

// True when a buff-bar icon id refers to an ITEM (render via the item pack)
// rather than a SPRITE (render via the sprite cache). Unknown ids -> false
// (sprite), so non-struct buffs keep their direct-sprite behaviour.
bool GetBuffIconIsItem(int id) {
    if (id <= 0) return false;
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    LoadBuffNamesLocked();
    for (int d : { 0, -1, 1 }) {                 // same inactive/active adjacency as the name
        auto it = g_buff_icon_item.find(id + d);
        if (it != g_buff_icon_item.end()) return it->second;
    }
    return false;
}


namespace { const int kItemXpTable[20] = {
    0, 1160, 2607, 5176, 8286, 11760, 15835, 21152, 28761, 40120,
    57095, 81960, 117397, 166496, 232755, 320080, 432785, 575592, 753631, 972440 }; }
int ItemLevelFromXp(int item_xp) {
    // kItemXpTable[L] = XP needed to REACH level L+1, so the highest L with
    // xp >= kItemXpTable[L-1] is the current level.
    if (item_xp <= 0) return 1;
    for (int level = 1; level < 20; ++level)
        if (item_xp < kItemXpTable[level]) return level;
    return 20;
}

// Region object placements assuming g_mu is already held (used by the in-process map render so it can
// read locations without re-locking). Archive id = rx | (ry << 7): rx must fit 7 bits; ry spans the
// full map height (cap 255 so the (rx<<8|ry) memo key stays unambiguous).
static const std::vector<LocPlacement>& RegionLocationsLocked(int region_x, int region_y) {
    static const std::vector<LocPlacement> kEmpty;
    if (region_x < 0 || region_x > 127 || region_y < 0 || region_y > 255) return kEmpty;
    int key = (region_x << 8) | region_y;
    auto hit = g_region_cache.find(key);
    if (hit != g_region_cache.end()) return hit->second;

    std::vector<LocPlacement> placements;
    auto* index = g_store ? g_store->Get(kIndexMaps) : nullptr;
    if (index) {
        int archive = region_x | (region_y << 7);
        for (int file : {0, 1}) {   // 0 = land, 1 = water
            auto bytes = index->ReadFile(archive, file);
            if (bytes.empty()) continue;
            auto part = DecodeMapLocations(std::move(bytes));
            placements.insert(placements.end(), part.begin(), part.end());
        }
    }
    g_region_cache[key] = std::move(placements);
    return g_region_cache[key];
}

std::vector<LocPlacement> RegionLocations(int region_x, int region_y) {
    if (region_x < 0 || region_x > 127 || region_y < 0 || region_y > 255) return {};
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    return RegionLocationsLocked(region_x, region_y);
}

void RegionBlockedFill(int player_x, int player_y, int plane, int radius,
                       std::vector<std::uint8_t>& out) {
    if (radius < 1) radius = 1;
    const int T = 2 * radius + 1;
    out.assign((std::size_t)T * T, 0);
    if (plane < 0 || plane > 3) plane = 0;

    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    for (int gx = 0; gx < T; ++gx) {
        for (int gy = 0; gy < T; ++gy) {
            int wx = player_x - radius + gx, wy = player_y - radius + gy;
            if (wx < 0 || wy < 0) continue;
            int rx = wx >> 6, ry = wy >> 6;
            if (rx > 127 || ry > 255) continue;
            const auto& grid = RegionBlockedGridLocked(rx, ry);
            int lx = wx & 0x3f, ly = wy & 0x3f;
            out[(std::size_t)gx * T + gy] = grid[(plane * 64 + lx) * 64 + ly];
        }
    }
}

namespace {
// Absolute surface height of a tile column at a display plane. Per-plane cache
// heights are DELTAS: the absolute surface is the sum through the effective
// plane, and a bridge column (0x2 on plane 1) shifts the surface one plane up.
// Entity fine-z = 32 * absolute height.
int EffPlaneLocked(const MapTileData& td, int lx, int ly, int plane) {
    std::size_t si = (std::size_t)(64 + lx) * 64 + ly;
    if (si < td.settings.size() && (td.settings[si] & 0x2) && plane < 3) return plane + 1;
    return plane;
}
std::int16_t AbsSumLocked(const MapTileData& td, int lx, int ly, int ep) {
    int sum = 0; bool any = false;
    for (int z = 0; z <= ep; ++z) {
        std::size_t i = (std::size_t)(z * 64 + lx) * 64 + ly;
        if (i >= td.heights.size()) break;
        std::int16_t v = td.heights[i];
        if (v == (std::int16_t)-32768) { if (z == 0) break; continue; }
        sum += v; any = true;
    }
    return any ? (std::int16_t)sum : (std::int16_t)-32768;
}
std::int16_t AbsHeightLocked(const MapTileData& td, int lx, int ly, int plane) {
    return AbsSumLocked(td, lx, ly, EffPlaneLocked(td, lx, ly, plane));
}
}  // namespace

int TileEffPlane(int wx, int wy, int plane) {
    if (wx < 0 || wy < 0) return plane;
    if (plane < 0 || plane > 3) plane = 0;
    int rx = wx >> 6, ry = wy >> 6;
    if (rx > 127 || ry > 255) return plane;
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    return EffPlaneLocked(RegionTilesLocked(rx, ry), wx & 0x3f, wy & 0x3f, plane);
}

std::int16_t TileHeightAtPlane(int wx, int wy, int eff_plane) {
    if (wx < 0 || wy < 0 || eff_plane < 0 || eff_plane > 3) return (std::int16_t)-32768;
    int rx = wx >> 6, ry = wy >> 6;
    if (rx > 127 || ry > 255) return (std::int16_t)-32768;
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    return AbsSumLocked(RegionTilesLocked(rx, ry), wx & 0x3f, wy & 0x3f, eff_plane);
}

void TileCornerHeights(int wx, int wy, int plane, std::int16_t out[4]) {
    // Four corner surface heights of one tile (SW,SE,NE,NW), all summed through
    // THIS tile column's effective plane. Per-corner bridge detection morphs
    // quads at deck/stair seams (each corner picking a different layer).
    out[0] = out[1] = out[2] = out[3] = (std::int16_t)-32768;
    if (wx < 0 || wy < 0) return;
    if (plane < 0 || plane > 3) plane = 0;
    int rx = wx >> 6, ry = wy >> 6;
    if (rx > 127 || ry > 255) return;
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    const int ep = EffPlaneLocked(RegionTilesLocked(rx, ry), wx & 0x3f, wy & 0x3f, plane);
    static const int CX[4] = { 0, 1, 1, 0 }, CY[4] = { 0, 0, 1, 1 };
    for (int c = 0; c < 4; ++c) {
        int cwx = wx + CX[c], cwy = wy + CY[c];
        int crx = cwx >> 6, cry = cwy >> 6;
        if (crx > 127 || cry > 255) continue;
        out[c] = AbsSumLocked(RegionTilesLocked(crx, cry), cwx & 0x3f, cwy & 0x3f, ep);
    }
}

std::int16_t TileHeight(int wx, int wy, int plane) {
    if (wx < 0 || wy < 0) return (std::int16_t)-32768;
    if (plane < 0 || plane > 3) plane = 0;
    int rx = wx >> 6, ry = wy >> 6;
    if (rx > 127 || ry > 255) return (std::int16_t)-32768;
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    const auto& td = RegionTilesLocked(rx, ry);
    return AbsHeightLocked(td, wx & 0x3f, wy & 0x3f, plane);
}

void RegionCornerHeightsFill(int player_x, int player_y, int plane, int radius,
                             std::vector<std::int16_t>& out) {
    if (radius < 1) radius = 1;
    const int T = 2 * radius + 1;          // tiles per axis
    out.assign((std::size_t)T * T * 4, (std::int16_t)-32768);
    if (plane < 0 || plane > 3) plane = 0;
    static const int CX[4] = { 0, 1, 1, 0 }, CY[4] = { 0, 0, 1, 1 };   // SW,SE,NE,NW

    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    for (int tx = 0; tx < T; ++tx) {
        for (int ty = 0; ty < T; ++ty) {
            const int wx = player_x - radius + tx, wy = player_y - radius + ty;
            if (wx < 0 || wy < 0) continue;
            const int rx = wx >> 6, ry = wy >> 6;
            if (rx > 127 || ry > 255) continue;
            // ONE layer decision for this tile column, then all four corners read at it.
            const int ep = EffPlaneLocked(RegionTilesLocked(rx, ry), wx & 0x3f, wy & 0x3f, plane);
            const std::size_t base = ((std::size_t)tx * T + ty) * 4;
            for (int c = 0; c < 4; ++c) {
                const int cwx = wx + CX[c], cwy = wy + CY[c];
                const int crx = cwx >> 6, cry = cwy >> 6;
                if (crx > 127 || cry > 255) continue;
                out[base + c] = AbsSumLocked(RegionTilesLocked(crx, cry), cwx & 0x3f, cwy & 0x3f, ep);
            }
        }
    }
}

void RegionHeightsFill(int player_x, int player_y, int plane, int radius,
                       std::vector<std::int16_t>& out) {
    if (radius < 1) radius = 1;
    const int N = 2 * radius + 2;   // corner-tile lattice (matches the overlay grid)
    out.assign((std::size_t)N * N, (std::int16_t)-32768);
    if (plane < 0 || plane > 3) plane = 0;

    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    for (int gx = 0; gx < N; ++gx) {
        for (int gy = 0; gy < N; ++gy) {
            int wx = player_x - radius + gx, wy = player_y - radius + gy;
            if (wx < 0 || wy < 0) continue;
            int rx = wx >> 6, ry = wy >> 6;
            if (rx > 127 || ry > 255) continue;
            const auto& td = RegionTilesLocked(rx, ry);
            out[(std::size_t)gx * N + gy] = AbsHeightLocked(td, wx & 0x3f, wy & 0x3f, plane);
        }
    }
}

std::string SpriteDataUrl(int sprite_id) {
    if (sprite_id < 0) return {};
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    auto hit = g_sprite_cache.find(sprite_id);
    if (hit != g_sprite_cache.end()) return hit->second;

    std::string url;
    auto* idx = g_store ? g_store->Get(kIndexSprites) : nullptr;
    if (idx) {
        auto png = SpriteAsPng(*idx, sprite_id);
        if (!png.empty()) {
            std::string b64 = base64(png);
            if (!b64.empty()) url = "data:image/png;base64," + b64;
        }
    }
    g_sprite_cache[sprite_id] = url;
    return url;
}

// Sprite data URL capped to `px` on the longest side (area-average filtered) -- panels
// request their display box (or 2x it) so the in-browser rescale stays small and clean.
std::string SpriteDataUrlScaled(int sprite_id, int px) {
    if (sprite_id < 0) return {};
    if (px <= 0) return SpriteDataUrl(sprite_id);
    if (px < 8) px = 8;
    if (px > 256) px = 256;
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    const long long key = ((long long)sprite_id << 16) | px;
    auto hit = g_sprite_scaled_cache.find(key);
    if (hit != g_sprite_scaled_cache.end()) return hit->second;

    std::string url;
    auto* idx = g_store ? g_store->Get(kIndexSprites) : nullptr;
    if (idx) {
        auto png = SpriteAsPngScaled(*idx, sprite_id, px);
        if (!png.empty()) {
            std::string b64 = base64(png);
            if (!b64.empty()) url = "data:image/png;base64," + b64;
        }
    }
    g_sprite_scaled_cache[key] = url;
    return url;
}

std::vector<std::uint8_t> SpriteRgba(int sprite_id, int& w, int& h) {
    w = h = 0;
    if (sprite_id < 0) return {};
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    auto* idx = g_store ? g_store->Get(kIndexSprites) : nullptr;
    if (!idx) return {};
    return SpriteRawRgba(*idx, sprite_id, w, h);
}

// ---- cache parse health -----------------------------------------------------
// One sweep per parsed cache surface: how many records decode to a clean end
// and, when they don't, the opcode that stopped the read (how the 949 item /
// achievement format changes were caught). Big indexes are SAMPLED (every Nth
// file, ~1200 records): a format change hits nearly every record, so a sample
// is as diagnostic as a full pass at a fraction of the cost. stop_op 256 means
// the stream ran out mid-record (structural overrun, no single opcode to blame);
// 257 means the record parsed but disagrees with its schema (DBRows vs dbtables).
// User-triggered from the Info-tab health check; holds g_mu for the sweep.
std::vector<CacheParseRow> CacheParseHealth() {
    std::vector<CacheParseRow> rows;
    std::lock_guard<std::mutex> lk(g_mu);
    EnsureInit();
    if (!g_store) return rows;

    auto finish = [](CacheParseRow& row, std::unordered_map<int, int>& stops) {
        for (const auto& kv : stops)
            if (kv.second > row.stop_n) { row.stop_op = kv.first; row.stop_n = kv.second; }
    };
    // Sweep one index (or a single archive of it when `archive` >= 0). `decode`
    // gets (archive, file, bytes) and returns the stopping opcode (0 = clean).
    auto sweep = [&](const char* name, int index_id, int archive, auto&& decode) {
        CacheParseRow row; row.name = name;
        auto* idx = g_store->Get(index_id);
        if (idx && idx->ready()) {
            const auto& entries = idx->ref().entries();
            int a0 = archive >= 0 ? archive : 0;
            int a1 = archive >= 0 ? archive + 1 : (int)entries.size();
            long long files = 0;
            for (int a = a0; a < a1 && a < (int)entries.size(); ++a)
                files += (long long)entries[a].valid_file_ids.size();
            long long step = files > 1200 ? files / 1200 : 1;
            row.sampled = step > 1;
            std::unordered_map<int, int> stops;
            long long k = 0;
            for (int a = a0; a < a1 && a < (int)entries.size(); ++a) {
                for (int fid : entries[a].valid_file_ids) {
                    if ((k++ % step) != 0) continue;
                    auto bytes = idx->ReadFile(a, fid);
                    if (bytes.empty()) continue;
                    ++row.total;
                    int st = decode(a, fid, std::move(bytes));
                    if (st == 0) ++row.ok; else ++stops[st];
                }
            }
            finish(row, stops);
        }
        rows.push_back(std::move(row));
    };

    sweep("items", kIndexItems, -1, [](int a, int f, std::vector<std::uint8_t> b) {
        int st = 0; DecodeItem((a << 8) | f, std::move(b), &st); return st; });
    sweep("npcs", kIndexNpcs, -1, [](int a, int f, std::vector<std::uint8_t> b) {
        int st = 0; DecodeNpc((a << 7) | f, std::move(b), &st); return st; });
    sweep("objects", kIndexLocations, -1, [](int a, int f, std::vector<std::uint8_t> b) {
        int st = 0; DecodeLoc((a << 8) | f, std::move(b), &st); return st; });
    sweep("quests", kIndexConfigs, kQuestArchive, [](int, int, std::vector<std::uint8_t> b) {
        QuestDef q; int st = 0; DecodeQuestFile(std::move(b), q, &st); return st; });
    sweep("varbits", kIndexConfigs, kVarbitArchive, [](int, int, std::vector<std::uint8_t> b) {
        InputStream s(std::move(b));
        while (s.remaining() > 0) {
            int op = s.ReadUnsignedByte();
            if (op == 0) break;
            else if (op == 1) { s.ReadUnsignedByte(); s.ReadUnsignedShort(); }
            else if (op == 2) { s.ReadUnsignedByte(); s.ReadUnsignedByte(); }
            else if (op == 16) { }
            else return op;
        }
        return 0; });
    sweep("params", kIndexConfigs, kParamsArchive, [](int, int, std::vector<std::uint8_t> b) {
        return DecodeParamFile(std::move(b), nullptr); });
    sweep("enums", kIndexEnums, -1, [](int, int, std::vector<std::uint8_t> b) {
        InputStream s(std::move(b));
        while (s.remaining() > 0) {
            int op = s.ReadUnsignedByte();
            if (op == 0) break;
            else if (op == 1 || op == 101 || op == 2 || op == 102) s.ReadUnsignedByte();
            else if (op == 3) s.ReadString();
            else if (op == 4) s.ReadInt();
            else if (op == 5) { int n = s.ReadUnsignedShort(); for (int i = 0; i < n; ++i) { s.ReadInt(); s.ReadString(); } }
            else if (op == 6) { int n = s.ReadUnsignedShort(); for (int i = 0; i < n; ++i) { s.ReadInt(); s.ReadInt(); } }
            else if (op == 7) { s.ReadUnsignedShort(); int n = s.ReadUnsignedShort(); for (int i = 0; i < n; ++i) { s.ReadUnsignedShort(); s.ReadString(); } }
            else if (op == 8) { s.ReadUnsignedShort(); int n = s.ReadUnsignedShort(); for (int i = 0; i < n; ++i) { s.ReadUnsignedShort(); s.ReadInt(); } }
            else if (op == 131 || op == 207 || op == 209) { }
            else return op;
        }
        return 0; });
    sweep("structs", kIndexStructs, -1, [](int, int, std::vector<std::uint8_t> b) {
        InputStream s(std::move(b));
        while (s.remaining() > 0) {
            int op = s.ReadUnsignedByte();
            if (op == 0) break;
            if (op != 249) return op;
            int n = s.ReadUnsignedByte();
            for (int i = 0; i < n && s.remaining() > 0; ++i) {
                bool str = s.ReadUnsignedByte() == 1;
                s.Read24BitInt();
                if (str) s.ReadString(); else s.ReadInt();
            }
        }
        return 0; });
    sweep("perks (DBRows)", kIndexConfigs, 41, [](int, int, std::vector<std::uint8_t> b) {
        InputStream s(std::move(b));
        while (s.remaining() > 0) {
            int op = s.ReadUnsignedByte();
            if (op == 0) break;
            if (op == 4) { s.ReadUnsignedSmart(); continue; }
            if (op != 3) return op;
            s.ReadUnsignedByte();
            while (s.remaining() > 0) {
                int cb = s.ReadUnsignedByte();
                if (cb == 0xFF) break;
                int subN = s.ReadUnsignedByte();
                if (subN <= 0) continue;
                std::vector<int> types((std::size_t)subN);
                for (int i = 0; i < subN; ++i) types[i] = s.ReadUnsignedSmart();
                int rowCount = s.ReadUnsignedSmart();
                for (int r = 0; r < rowCount; ++r)
                    for (int sub = 0; sub < subN; ++sub) {
                        if (s.remaining() <= 0) return 256;      // structural overrun
                        if (types[sub] == 0x24) s.ReadString(); else s.ReadInt();
                    }
            }
        }
        return 0; });
    sweep("dbtables", kIndexConfigs, kDbTablesArchive, [](int, int, std::vector<std::uint8_t> b) {
        return DecodeDbTableFile(std::move(b), nullptr); });
    // DBRows cross-checked against the dbtables schemas: linkage (op4 tag ->
    // schema file) + declared column types. stop 257 = row parses but the
    // schema disagrees -- the "silently wrong values" failure mode the
    // structural sweeps above can't see.
    // Reload the schemas for every health run instead of trusting the latch:
    // the game client rewrites the .jcache in place on updates while this
    // process keeps running, so a map latched before the update cross-checks
    // fresh row bytes against pre-update schemas and reports a false mismatch
    // until the launcher restarts (~379 small files; negligible cost).
    g_dbtable_cols.clear();
    g_dbtables_loaded = false;
    LoadDbTablesLocked();
    sweep("DBRows vs schema", kIndexConfigs, 41, [](int, int, std::vector<std::uint8_t> b) {
        return DbRowSchemaCheckLocked(std::move(b)); });
    sweep("interfaces", kIndexInterfaces, -1, [](int, int, std::vector<std::uint8_t> b) {
        IfaceCompDef c; return DecodeIfaceComp(std::move(b), c); });
    {   // achievements: full sweep via the decoder that owns the opcode table
        CacheParseRow row; row.name = "achievements";
        AchievementsParseHealth(row.ok, row.total, row.stop_op, row.stop_n);
        rows.push_back(std::move(row));
    }
    {   // map tiles: known-populated sample regions; ok = the full 4*64*64 walk
        // completed (a trailing section is normal on 949+, running SHORT is not)
        static const int kRegions[][2] = { {49,54},{50,50},{48,54},{52,53},{55,24},{58,25},{39,52} };
        CacheParseRow row; row.name = "map tiles"; row.sampled = true;
        auto* idx = g_store->Get(kIndexMaps);
        if (idx && idx->ready()) {
            for (const auto& r : kRegions) {
                auto bytes = idx->ReadFile(r[0] | (r[1] << 7), 3);
                if (bytes.empty()) continue;
                ++row.total;
                int left = 0;
                (void)DecodeMapTiles(std::move(bytes), &left);
                if (left >= 0) ++row.ok; else { row.stop_op = 256; ++row.stop_n; }
            }
        }
        rows.push_back(std::move(row));
    }
    {   // sprites: decode two UI staples (Magic skill icon + a quest journal icon)
        CacheParseRow row; row.name = "sprites"; row.sampled = true;
        auto* idx = g_store->Get(kIndexSprites);
        if (idx && idx->ready()) {
            for (int id : { 16055, 3797 }) {
                ++row.total;
                int w = 0, h = 0;
                auto px = SpriteRawRgba(*idx, id, w, h);
                if (!px.empty() && w > 0 && h > 0) ++row.ok;
            }
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

}  // namespace rtx::cache
