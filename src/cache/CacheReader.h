#pragma once

#include "MapLocations.h"

#include <string>
#include <utility>
#include <vector>

// Thin, lazy facade over the cache lib. Every entry point holds the internal cache mutex.

namespace rtx::cache {

struct ItemInfo {
    std::string name;
    int         ge_limit  = -1;  // 4-hour buy limit; -1 = no limit / unknown
    long long   value     = -1;  // store value in gp
    bool        augmented = false; // Invention-augmented (holds item XP + gizmos)
};

// Noted items resolve to their unnoted parent (name suffixed "(noted)"). Empty name on failure. Memoized.
ItemInfo GetItem(int item_id);

std::string ItemName(int item_id);

// From the cache config ("Disassemble" worn option / "gizmos" destroy message); the name alone is unreliable.
bool ItemIsAugmented(int item_id);

// JSON `{"name":...,"ge_limit":N,"value":N}` for the JS bridge.
std::string ItemInfoJson(int item_id);

// Parse-health row. stop_op: most frequent breaking opcode; 256 = stream overrun, 257 = schema mismatch, -1 = none.
struct CacheParseRow {
    std::string name;              // surface label, e.g. "items"
    int  ok = 0, total = 0;        // clean / attempted
    bool sampled = false;          // total is an every-Nth sample, not the full index
    int  stop_op = -1;
    int  stop_n  = 0;              // records that stopped on stop_op
};
// Sweeps every parsed cache surface plus the DBRows-vs-dbtables cross-check. Holds the cache mutex ~1-2 s.
std::vector<CacheParseRow> CacheParseHealth();
// Unknown-opcode probe report (see src/cache/Probe.h). Holds the cache mutex for seconds.
std::string CacheProbeUnknownOps();

// {"items":N,"withIcon":M,"missing":[ids...]} over every named js5-19 item. Slow (~63k decodes); call off the UI thread.
std::string ItemIconCoverageJson(bool (*has)(int item_id));

// World-map areas (js5-23): {"<areaId>":{"n","dn","zoom","bg","x0","y0","x1","y1" (display mapsquares),
// "w","h" (px), "z":[[planes,srcX,srcY,dstPlane,dstX,dstY],..], "r":[[sx0,sy0,sx1,sy1,dx0,dy0,dx1,dy1],..] (tiles, inclusive)}}.
std::string MapAreasJson();
std::string MapAreaImageDataUrl(int areaId, bool thumb);
// `data:image/png;base64,...` or "" on failure. Memoized.
std::string SpriteDataUrl(int sprite_id);
// Capped to `px` (8..256) on the longest side; px<=0 = default cap.
std::string SpriteDataUrlScaled(int sprite_id, int px, int frame = 0);
// Archive id of the sprite group named `name` (reference-table name hash), -1 if unknown.
int SpriteIdByName(const std::string& name);

std::vector<std::uint8_t> SpriteRgba(int sprite_id, int& w, int& h);   // raw RGBA pixels (for the HUD overlay)

struct NpcMeta {
    std::string              name;
    std::vector<std::string> actions;       // non-empty right-click options, in order
    int                      combat_level = -1;
    int                      size = 1;       // tile footprint from opcode 12; 2 = a 2x2 npc
    int                      id = -1;        // def that provided the name/model (resolved morph child for a morph base)
};

// NPC def (index 18). Empty on failure. Memoized.
NpcMeta GetNpc(int npc_id);

// NPC morph table (opcodes 106/118). `variants` is value-indexed; `def_child` is the out-of-range fallback
// (-1 = hidden); the unused selector of varbit/varp is -1. False when the NPC has no morph. Memoized.
bool GetNpcMorph(int npc_id, int& varbit, int& varp, int& def_child, std::vector<int>& variants);

struct LocMeta {
    std::string              name;
    std::vector<std::string> actions;   // non-empty right-click options, in order
    int                      dim_x = 1;  // footprint (opcode 14)
    int                      dim_y = 1;  // footprint (opcode 15)
};

// Loc def (index 16). Empty name when unnamed. Memoized.
LocMeta GetLoc(int loc_id);

// ---- menu-rule authoring: `kind` 0 = item (js5-19), 1 = loc (js5-16), 2 = npc (js5-18) ----
// `{"id":N,"name":"..","options":["..",..]}`, "{}" when the id has no def or no name.
std::string MenuDefJson(int kind, int id);

// Exact-name reverse lookup, `{"ids":[..]}`. The name index is decoded once per kind (first call is slow).
std::string MenuFindJson(int kind, const std::string& name);

// Case-insensitive substring search over the same index: `{"hits":[{"id":N,"name":".."},..]}`, capped at `limit`.
std::string MenuSearchJson(int kind, const std::string& query, int limit);

// Loc morph table (opcodes 77/92). `variants` is value-indexed (-1 = no change); `def_child` is op92's
// out-of-range fallback. False when the loc has no morph. Memoized.
bool GetLocMorph(int loc_id, int& varbit, int& varp, int& def_child, std::vector<int>& variants);

// Varbit def (CONFIGS index 2 / archive 69): backing varp + [lsb,msb]. value = (varp >> lsb) & ((1<<(msb-lsb+1))-1).
bool GetVarbit(int varbit_id, int& varp, int& lsb, int& msb);
// Domain-5 (item instance) varbit: `var` is the instance key (INV_GETVAR). Perks use 30212 (item XP), 30215..30222 (gizmo perks).
bool GetObjVarbit(int varbit_id, int& var, int& lsb, int& msb);

// Perk name from DBRows (CONFIGS archive 41, tableId 8). Empty if unknown.
std::string PerkName(int perk_id);
// Perk effect description (same DBRows row, 2nd string column); may carry <col=..> markup.
std::string PerkDesc(int perk_id);
// Rank count (column 7 rows). 1 for single-rank perks, which the game never prints a rank for (CS2 12079). 0 if unknown.
int PerkRankCount(int perk_id);

// Archaeology mysteries: `{"myst":{"<name>":{"pg":[[globalPageIdx,itemId],..],"c31":[[varp11733Bit,itemId],..]},..}}`.
// Table 92 col 4 -> journal pages (table 81: col0 bit, col3 item); col 5 -> table-31 collectibles (col0 varp 11733 bit, col3 item).
std::string MystPagesJson();

// Archaeology research: `[{"b":bit,"n":name,"f":"field study","r":"report"},..]` from table 90 (cols 0/3/6/7).
// Bit is over varps 9297/9298/11740 (field study) and 9299/9300/11741 (report), 32 bits each; the panel tests it.
std::string ArchResearchJson();

// Player varbit map (CONFIGS archive 69): `{"<varpId>":[[varbitId,lsb,msb],..],..}`. "{}" until the cache opens.
std::string VarbitMapJson();

// Quest defs (CONFIGS archive 35): `{"vb":{"<varbitId>":[varp,lsb,msb]},"quests":[{..}]}`; progress as v:[varp,start,end]
// or b:[varp,lsb,msb,start,end]. Status per CS2 script2158: < start not started, < end in progress, else complete.
std::string QuestsJson();

// Param def (CONFIGS archive 11, file = id): `{"type":N[,"int":N][,"str":".."]}`; int/str only when op2/op5 present.
std::string ParamDefJson(int param_id);

// ---- Audio (js5-14 sound effects, js5-40 music): JAGA containers wrapping Ogg Vorbis; archive id = sound id ----
// Complete .ogg (JAGA header stripped). Neither the client nor Windows ships a Vorbis decoder.
std::vector<std::uint8_t> SoundOgg(int index_id, int sound_id);

// Every chunk in order: a JAGA container splits long audio into separate complete Ogg streams. Decode each, then concatenate PCM.
std::vector<std::vector<std::uint8_t>> SoundOggChunks(int index_id, int sound_id);

// [{id, rate, ch, ms, bytes}, ...] from start_id upward. Paged: a full index-14 walk decompresses hundreds of MB.
std::string SoundListJson(int index_id, int start_id, int limit);

// Static component defs of one group (js5-3, archive = group id): `{"group":N,"comps":[{"id","t","par","x","y","w","h"
// [,"ct"][,"hid"][,"text"][,"sprite"][,"model"]},..]}`. par -1 = root; text/sprite/model for types 4/5/6.
std::string IfaceGroupDefsJson(int group_id);

// Enum (index 17, archive = id>>8, file = id&0xff) as `{"<key>":<value>,..}`. "{}" if unknown.
std::string EnumJson(int enum_id);

// Ability structs keyed by name: {"Overpower":{"t":4,"d":"..","u":".."},..}; t = tier (1 basic, 2 threshold,
// 3 defensive, 4 ultimate, 5 special, 7 utility), d = description, u = unlock text.
std::string AbilityConfigsJson();
// {"buffs":[[name,id,isItem],...],"debuffs":[...]}; id is a sprite, or an item when isItem = 1.
std::string BuffCatalogJson();

// {"id":N,"name":".."} via GetNpc. "{}" if unknown.
std::string NpcJson(int npc_id);

// Top-down terrain window centred on (cx,cy): half = tiles each side, ts = px/tile (0 = default 40 / 6).
// {"w":px,"t":tilepx,"h":halfTiles,"b64":"<RGBA>"} or "{}". `want` bits: 1 terrain, 2 icons, 4 collision grids, 8 objs.
std::string MapWindowJson(int cx, int cy, int plane, int half = 0, int ts = 0, int want = 15);

// StructType params (js5-22; id = archive*32 + file): {"ints":{key:v},"strs":{key:"v"}}.
std::string StructParamsJson(int structId);

// Every world-map element: {"<id>":{"s":sprite,"c":category,"t":"text","n":"body"}}; MapWindowJson's `icons` index into it.
std::string MapLabelsJson();

// Loc id -> loc name for mapFunction-bearing locs: {"<id>":"Gate"}. Tooltip fallback for unnamed elements.
std::string MapLocNamesJson();

// Element categories: {"<id>":{"n":"Bank","g":group}}. Enum 8586 category -> struct; params 596 = name, 597 = legend group.
std::string MapCategoriesJson();

// Every map-symbol placement: {"n":count,"b":"<base64>"}, 7 bytes LE each (element u16, x u16, y u16, plane u8). Full region walk, memoized.
std::string MapSymbolsJson();

// Mount comp SUB under group 1477 for a content-slot id (enum 7716 slot -> struct, param 3503 = packed mount comp). -1 if none.
int PanelMountComp(int group_id);

// All DBRows of one master table (archive 41): [{"f":fid,"i":{col:[ints]},"s":{col:[strs]}}].
std::string DbRowsJson(int masterTable);

// Raw op-249 params of one ITEM (js5-19): {"ints":{key:v},"strs":{key:"v"}}, {} when none.
std::string ItemParamsJson(int item_id);

// Varobj list (item op 132) in slot order: instance key N belongs to varobjs[N]. Empty when none. Cached.
std::vector<int> ItemVarobjs(int item_id);

// Raw item config bytes as lowercase hex (cq "hex <id>"). Empty when absent.
std::string ItemFileHex(int item_id);
// Same for one CONFIGS (index 2) file: cq "cfg <archive> <file>". Archive 65 = varobject defs.
std::string ConfigFileHex(int archive, int file);
// File count / largest id / first ids of one CONFIGS archive: cq "cfgls <archive>".
std::string ConfigArchiveInfo(int archive);
// {"<domain>":{"n":..,"var":[min,max],"vb":[min,max],"sample":[..]}}. Domains: 0 player, 1 npc, 2 client, 3 world,
// 4 region, 5 object (item instance), 6 clan, 7 clan settings, 8 campaign, 9 player group.
std::string VarbitDomainsJson();
// Varbit defs of every non-player domain: {"<domain>":{"<var>":[[varbit,lsb,msb],..]}}.
std::string VarbitDomainMapJson();
// Var defs of one var archive (60 player, 61 npc, 62 client, 63 world, 64 region, 65 object, 66 clan,
// 67 clan settings, 68 campaign, 75 player group). See the .cpp for the shape.
std::string VarDefsJson(int archive);

// Buff name for a bar icon id (sprite, or item id for item-backed buffs). Buffs are StructTypes (index 22);
// param keys get renumbered by updates, so they are discovered from probe strings at runtime. Memoized.
std::string GetBuffName(int id);

// Debuff side first: one sprite id can label differently per bar (21239 = "Dismember" buff / "Bleeding" debuff).
std::string GetDebuffName(int id);

// Kind bitfield from struct params 8110-8113: bit0 countdown, bit1 count, bit2 percentage, bit3 custom string. 0 = unknown.
int GetBuffKind(int id);

// True when the bar icon id is an item rather than a sprite (the widget stores both at one offset). Unknown -> false.
bool GetBuffIconIsItem(int id);

// Augmented-item level (1..20) for a stored item XP value (RS3 item-XP curve).
int ItemLevelFromXp(int item_xp);

// All placements in one MAPSV2 region (index 5): file 0 land + file 1 water, local 0..63 coords. Memoized per region.
std::vector<LocPlacement> RegionLocations(int region_x, int region_y);

// Placement covering (x,y,plane) with a Search or Open option (Open = locked container):
// {"id":n,"name":"Crate","action":"Search","dx":n,"dy":n}, "{}" when none.
std::string ClueSearchTargetJson(int x, int y, int plane);

// RegionBlockedFill flags. Walls block one edge; scenery/void/diagonal block the whole tile.
constexpr std::uint8_t kTileBlockN   = 0x01;  // north edge (toward +Y)
constexpr std::uint8_t kTileBlockS   = 0x02;
constexpr std::uint8_t kTileBlockE   = 0x04;  // east edge (toward +X)
constexpr std::uint8_t kTileBlockW   = 0x08;
constexpr std::uint8_t kTileBlockFull = 0x10; // whole tile impassable
constexpr std::uint8_t kTileHasWall  = 0x20;  // a wall placement exists (render hint)

// Walkability grid, (2*radius+1)^2 flag bytes: out[gx*W+gy] = tile (player_x-radius+gx, player_y-radius+gy).
// Blocked by void tiles (MAPSV2 file 3 settings 0x1, non-bridge), type 10/11 scenery without no-clip, or type 9 walls.
void RegionBlockedFill(int player_x, int player_y, int plane, int radius,
                       std::vector<std::uint8_t>& out);

// Corner-lattice heights, (2*radius+2)^2: out[gx*W+gy] = height at tile (player_x-radius+gx, player_y-radius+gy).
// Cache units; -32768 = unknown.
void RegionHeightsFill(int player_x, int player_y, int plane, int radius,
                       std::vector<std::int16_t>& out);

// Absolute surface height at one tile (live fine-z = 32 * height), cumulative across planes, bridge-aware. -32768 unknown.
std::int16_t TileHeight(int wx, int wy, int plane);

// Four corner heights (SW,SE,NE,NW) sampled at this column's effective plane, so quads do not morph at bridge seams.
void TileCornerHeights(int wx, int wy, int plane, std::int16_t out[4]);

// Bridge-aware effective plane of a column, and the absolute height summed through a given plane (no bridge adjust).
int TileEffPlane(int wx, int wy, int plane);
std::int16_t TileHeightAtPlane(int wx, int wy, int eff_plane);

// Per-tile corner heights, (2*radius+1)^2 tiles x 4 (SW,SE,NE,NW), index ((tx*T)+ty)*4+corner; each tile picks its
// effective plane once. A shared lattice cannot represent a bridge deck edge, hence per-tile.
void RegionCornerHeightsFill(int player_x, int player_y, int plane, int radius,
                             std::vector<std::int16_t>& out);

}  // namespace rtx::cache
