#pragma once

#include "MapLocations.h"

#include <string>
#include <utility>
#include <vector>

// Thin facade over the cache lib for launcher code. Lazy: nothing happens
// until the first lookup. Thread-safe: all entry points hold an internal
// mutex while touching the in-memory caches; SqliteIndexFile uses its
// own transient SQLite connections per query.

namespace rtx::cache {

struct ItemInfo {
    std::string name;
    int         ge_limit  = -1;  // 4-hour buy limit; -1 = no limit / unknown
    long long   value     = -1;  // store value in gp
    bool        augmented = false; // Invention-augmented (holds item XP + gizmos)
};

// Resolves item metadata for `item_id`. Noted items resolve to their
// unnoted parent (name suffixed "(noted)", limit/value inherited).
// Returns an empty-name struct on any failure. Memoized.
ItemInfo GetItem(int item_id);

// Convenience: just the display name.
std::string ItemName(int item_id);

// True when the item is Invention-augmented, per its cache config (a
// "Disassemble" worn option / a destroy message mentioning "gizmos"). The item
// name alone is unreliable (often lacks "Augmented").
bool ItemIsAugmented(int item_id);

// JSON `{"name":...,"ge_limit":N,"value":N}` for the JS bridge.
std::string ItemInfoJson(int item_id);

// One row of the cache parse-health sweep (Info-tab health check): how many
// records of a cache surface decode to a clean end, and the opcode that most
// often broke the read when they didn't. stop_op 256 = stream ran out
// mid-record (structural overrun); 257 = record parsed but disagrees with its
// schema (DBRows vs dbtables); -1 = nothing stopped.
struct CacheParseRow {
    std::string name;              // surface label, e.g. "items"
    int  ok = 0, total = 0;        // clean / attempted
    bool sampled = false;          // total is an every-Nth sample, not the full index
    int  stop_op = -1;
    int  stop_n  = 0;              // records that stopped on stop_op
};
// Sweeps every parsed cache surface (items/npcs/objects/quests/varbits/params/
// enums/structs/DBRows/dbtables/interfaces/achievements/map tiles/sprites),
// plus the DBRows-vs-dbtables schema cross-check. Holds the cache mutex for
// the sweep (~1-2 s); intended for the user-triggered health check.
std::vector<CacheParseRow> CacheParseHealth();

// `data:image/png;base64,...` for the cache sprite `sprite_id`, or "" on
// failure. Memoized. Used for skill icons (and any other cache sprite).
// World-map areas (js5-23): {"<areaId>":{"n","dn","zoom","bg","x0","y0","x1","y1" (display
// mapsquares), "w","h" (full image px), "z":[[planes,srcX,srcY,dstPlane,dstX,dstY],...],
// "r":[[sx0,sy0,sx1,sy1,dx0,dy0,dx1,dy1],...] (source rect -> display rect, tiles, inclusive)}}.
// Built once; {} until the cache opens.
std::string MapAreasJson();
// The game's own composited map image for an area as a data URL (full size, or the thumbnail).
std::string MapAreaImageDataUrl(int areaId, bool thumb);
std::string SpriteDataUrl(int sprite_id);
// Same, capped to `px` (8..256) on the longest side; px<=0 falls back to the default cap.
std::string SpriteDataUrlScaled(int sprite_id, int px, int frame = 0);
// Archive id of the sprite group named `name` (reference-table name hash), -1 if unknown.
int SpriteIdByName(const std::string& name);

std::vector<std::uint8_t> SpriteRgba(int sprite_id, int& w, int& h);   // raw RGBA pixels (for the HUD overlay)

struct NpcMeta {
    std::string              name;
    std::vector<std::string> actions;       // non-empty right-click options, in order
    int                      combat_level = -1;
    int                      size = 1;       // tile footprint from opcode 12; 2 = a 2x2 npc
    int                      id = -1;        // def that provided the name/model -- the resolved morph
                                             // child for a morph base (e.g. 26930 for base 26929)
};

// NPC definition metadata (index 18) for `npc_id`. Right-click actions come
// from the cache def, not the live entity. Empty on failure. Memoized.
NpcMeta GetNpc(int npc_id);

// Morph table for an NPC (opcodes 106/118): the base id carries a varbit/varp that
// picks the live variant. `variants` is value-indexed; `def_child` is the out-of-range
// fallback (-1 = hidden). varbit/varp are -1 when unused (the other is the selector).
// Returns false when the NPC has no varbit/varp morph. Memoized.
bool GetNpcMorph(int npc_id, int& varbit, int& varp, int& def_child, std::vector<int>& variants);

struct LocMeta {
    std::string              name;
    std::vector<std::string> actions;   // non-empty right-click options, in order
    int                      dim_x = 1;  // footprint (opcode 14) -- placed loc's size
    int                      dim_y = 1;  // footprint (opcode 15)
};

// Location ("object") definition metadata (index 16) for `loc_id`. Empty name
// when unnamed (most static scenery). Memoized.
LocMeta GetLoc(int loc_id);

// ---- menu-rule authoring ----------------------------------------------------
// `kind`: 0 = item (js5-19), 1 = loc (js5-16), 2 = npc (js5-18).
//
// Definition for one id as `{"id":N,"name":"..","options":["..",..]}` ("{}" when
// the id has no def or no name). Lets a right-click reorder rule be written from
// an id alone, without hovering the thing in game -- the options come from the
// same cache defs the client builds its menu from.
std::string MenuDefJson(int kind, int id);

// Reverse lookup: every id of `kind` whose def name matches `name` exactly, as
// `{"ids":[..]}`. A hovered NPC only exposes a runtime instance index, which is
// useless as a durable rule key (it changes on every respawn), so the panel keys
// such a rule on the config id instead -- and that needs name -> id. The index is
// decoded once per kind and memoized; the first call is slow (tens of thousands
// of defs), later ones are a map lookup.
std::string MenuFindJson(int kind, const std::string& name);

// Name search over the same index: every def of `kind` whose name CONTAINS `query`
// (case-insensitive), as `{"hits":[{"id":N,"name":".."},..]}`, capped at `limit`.
// Names are not unique - several ids share "Bank booth" - so the caller shows every
// match and picks, rather than the panel guessing which one was meant.
std::string MenuSearchJson(int kind, const std::string& query, int limit);

// Morph table for a loc (opcodes 77/92): the placed loc carries a morph BASE id
// whose on-screen variant is picked live by a varbit/varp. Returns false when the
// loc has no morph. `variants` is value-indexed (may hold -1 = "no change");
// `def_child` is op92's out-of-range fallback (-1 otherwise). varbit/varp are -1
// when unused (the other is the selector). Memoized.
bool GetLocMorph(int loc_id, int& varbit, int& varp, int& def_child, std::vector<int>& variants);

// Varbit definition (CONFIGS index 2 / archive 69): the backing varp + bit range
// [lsb,msb] for `varbit_id`. value = (varp >> lsb) & ((1<<(msb-lsb+1))-1).
// Returns false if unknown. Shares the VarbitMapJson decode (cached once).
bool GetVarbit(int varbit_id, int& varp, int& lsb, int& msb);

// Augment-perk display name for a DBRows perk id (CONFIGS index 2 / archive 41,
// rows with tableId 8). Lazily decodes the perk rows once. Empty if unknown.
std::string PerkName(int perk_id);
// Perk effect description (same DBRows row, 2nd string column); may carry <col=..> markup.
std::string PerkDesc(int perk_id);

// Archaeology mystery -> collectible mapping, decoded live from the DBRows archive:
// `{"myst":{"<name>":{"pg":[[globalPageIdx,itemId],..],"c31":[[varp11733Bit,itemId],..]},..}}`.
// A table-92 mystery row's column 4 lists its journal-page rows (table 81: col0 = the
// script13039 varp-bank bit, col3 = page item id) and column 5 its table-31 collectible
// rows (col0 = bit of varp 11733 per CS2 script18966, col3 = item id) -- the same lists
// the journal's DB_GETFIELDCOUNT(mystery, 376896/376912) counts. Decoded once; memoized.
std::string MystPagesJson();

// Archaeology RESEARCH (Field Study / Report), decoded from the DBRows archive:
// `[{"b":bit,"n":name,"f":"field study text","r":"report text"},..]` sorted by bit.
// Table 90 is the research list: column 0 is the progress BIT, column 3 the name,
// column 6 the Field Study text and column 7 the Report text (CS2 script14843 renders
// exactly these). The bit is tested against varp 9297/9298/11740 for the field study
// and 9299/9300/11741 for the report (script14629 / script14630), 32 bits per varp --
// the panel does that part live. Decoded once; memoized.
std::string ArchResearchJson();

// Varbit definition map decoded from the live cache (CONFIGS index 2 / archive 69):
// JSON `{"<varpId>":[[varbitId,lsb,msb],..],..}`. Lets the varp watcher name which
// varbit owns a changed bit. Decoded once and cached. "{}" until the cache opens.
std::string VarbitMapJson();

// All quest definitions decoded from the live cache (CONFIGS index 2 / archive 35),
// for the Quests tab. JSON `{"vb":{"<varbitId>":[varp,lsb,msb],..},"quests":[{...},..]}`:
// per quest id/name, members/difficulty/QP reward, the progress tracker as
// v:[varp,start,end] or b:[varp,lsb,msb,start,end] (varbit pre-resolved), required
// quests/skills/QP, parent quest, journal icon, release year. Status semantics are
// the game's own (CS2 script2158): value < start -> not started, < end -> in
// progress, else complete. Duplicate/stub configs (re-released quests) are dropped
// with their ids remapped in every requirement link. "vb" carries the varbits the
// UI's 8 special-case status rules (CS2 script2193) read. Decoded once; memoized.
std::string QuestsJson();

// Param definition (CONFIGS index 2 / archive 11, file = param id) as JSON
// `{"type":N[,"int":N][,"str":".."]}`: the vartype byte plus the op2/op5
// default value (emitted only when the def carries one -- op2's presence is
// what separates "default 0" from "no default"). "{}" when unknown. The whole
// archive decodes once (9420/9420 clean on build 949) and also backs the quest
// journal's default-icon lookup.
std::string ParamDefJson(int param_id);

// ---- Audio (js5-14 sound effects, js5-40 music) ----------------------------
// Archives are JAGA containers wrapping ordinary Ogg Vorbis, and the ARCHIVE ID IS THE SOUND
// ID that CS2 passes to SOUND_SYNTH / SOUND_VORBIS_VOLUME.
//
// SoundOgg returns a complete, playable .ogg (JAGA header stripped). Note the client has no
// Vorbis DECODER - Windows ships none either (verified: no codec, MediaPlayer reports
// HasAudio=false) - so these bytes are for export/preview, not for direct PCM playback.
std::vector<std::uint8_t> SoundOgg(int index_id, int sound_id);

// Every chunk of a sound, in order. A JAGA container splits longer audio into SEPARATE, complete
// Ogg streams (each with its own vorbis headers), so a decoder handed the concatenation stops at
// the end of the first one - effect 14873 played 3.5s of its 32.7s that way. Decode each and
// concatenate the PCM. Single-chunk sounds return one element.
std::vector<std::vector<std::uint8_t>> SoundOggChunks(int index_id, int sound_id);

// One page of sound metadata: [{id, rate, ch, ms, bytes}, ...] from start_id upward.
// Paged because reading every archive of index 14 would decompress hundreds of MB.
std::string SoundListJson(int index_id, int start_id, int limit);

// Static interface-component defs for one group (js5-3: archive = group id) as
// JSON `{"group":N,"comps":[{"id","t","par","x","y","w","h"[,"ct"][,"hid"]
// [,"text"][,"sprite"][,"model"]},..]}`: per component the type, parent (-1 =
// root), base geometry, content-type, and the type-specific default text
// (type 4) / sprite id (type 5) / model id (type 6). Cache DEFS, not the live
// tree: complements the reader-side interface walk with data that needs no
// attached client and never changes mid-session. Decoded once per group.
std::string IfaceGroupDefsJson(int group_id);

// Decode an ENUM (index 17, file = enum_id) to JSON `{"<key>":<value>,..}` where
// values are strings (valtype string) or numbers (valtype int). enum_id splits as
// archive=(id>>8), file=(id&0xff). Used to resolve id->name rosters, e.g. enum 1563
// (slayer creatures) and enum 9197 (reaper bosses). "{}" if unknown/cache not open.
std::string EnumJson(int enum_id);

// Ability configs from the struct cache, keyed by ability name: {"Overpower":{"t":4,"d":"..","u":".."},..}
// t = tier (1 basic/2 threshold/3 defensive/4 ultimate/5 special/7 utility), d = description,
// u = unlock text. Walked once and cached; {} until the cache is open. See AbilityConfigsJson.
std::string AbilityConfigsJson();
// Every buff/debuff name the struct cache knows with its bar icon id (sprite, or item when the
// third field is 1): {"buffs":[[name,id,isItem],...],"debuffs":[...]}. {} until the cache opens.
std::string BuffCatalogJson();

// {"id":N,"name":".."} for an NPC id (live cache, via GetNpc; morph-following + name overrides).
// Used by the Clue Scrolls tab to name "talk-to" clues. "{}" if unknown.
std::string NpcJson(int npc_id);

// Top-down terrain render of a square window CENTRED on world tile (cx,cy) on a plane: blended
// underlay colours + shape-split overlay colours (no icons), north-up. half = tiles each side
// (zoom), ts = px/tile; both clamped, 0 = default (40 / 6). Returns JSON
// {"w":px,"t":tilepx,"h":halfTiles,"b64":"<RGBA base64>"} for the panel to putImageData, or "{}".
// `want` selects the payload sections: 1 = terrain png, 2 = icons, 4 = collision grids
// (blk + nomove), 8 = objs. The world map asks for 3 (terrain + icons); the clue solver and
// the overlay walkability layer keep the collision grids. Default = everything.
std::string MapWindowJson(int cx, int cy, int plane, int half = 0, int ts = 0, int want = 15);

// Raw param map of one StructType (js5-22; id = archive*32 + file):
// {"ints":{key:v},"strs":{key:"v"}}. Generic cache-struct accessor for panels.
std::string StructParamsJson(int structId);

// Every world-map ELEMENT: {"<id>":{"s":sprite,"c":category,"t":"text","n":"body"}}. The map
// window's `icons` blob carries (tile, element id, loc id) per placement; this is the table it
// indexes into, fetched once when the World Map panel opens.
std::string MapLabelsJson();

// Loc id -> the placing loc's own name, for elements that have no name of their own: {"<id>":"Gate"}.
// The tooltip's last resort, keyed by the loc id in the `icons` blob. Only mapFunction-bearing
// locs, so a few KB; fetched once alongside MapLabelsJson.
std::string MapLocNamesJson();

// Map element CATEGORY names (the caps tooltip header): {"<id>":{"n":"Bank","g":group}}.
// Enum 8586 category -> struct, struct param 596 = name, 597 = legend group.
std::string MapCategoriesJson();

// Every map-symbol PLACEMENT in the world, for search: {"n":count,"b":"<base64>"} where the
// blob is 7 bytes each, little-endian (element u16, x u16, y u16, plane u8). One full region
// walk, memoised for the session; the panel persists it per client build.
std::string MapSymbolsJson();

// HUD panel registry lookup: mount comp SUB under group 1477 for a content-slot id
// (enum 7716 slot -> panel struct, param 3503 = packed mount comp). -1 when the slot
// isn't registered / mounts outside 1477 / the cache isn't open yet. Built once; the
// reader uses it to anchor panels with no hand-written kPanelOrigins entry.
int PanelMountComp(int group_id);

// All DBRows of one master table (archive 41): [{"f":fid,"i":{col:[ints]},"s":{col:[strs]}}].
std::string DbRowsJson(int masterTable);

// Raw op-249 params of one ITEM (js5-19): {"ints":{key:v},"strs":{key:"v"}}, {} when none.
std::string ItemParamsJson(int item_id);

// Display name for a buff-bar icon id. RS3 buffs/debuffs are StructTypes
// (index 22) whose params carry the name plus the buff-bar sprite id; the
// param keys aren't fixed (a game update renumbers them), so they're
// discovered at runtime from known probe strings and the whole struct table
// is walked once to build sprite/item id -> name. `id` is the sprite id read
// from the buff-bar icon widget, or the item id for item-backed buffs
// (overloads, flasks). Empty if unknown. Memoized.
std::string GetBuffName(int id);

// Same, resolved from the debuff side first: one sprite id can label
// differently per bar (e.g. 21239 = "Dismember" buff vs "Bleeding" debuff).
std::string GetDebuffName(int id);

// Buff "kind" bitfield for a buff-bar id, from the struct's text-builder params
// (8110-8113): bit0 = countdown timer, bit1 = has a count, bit2 = count is a
// percentage, bit3 = custom string. Lets the UI distinguish a real countdown
// from a static count/percentage (the +0x180 widget text alone is ambiguous).
// 0 = unknown. Same +/-1 adjacency as GetBuffName. Memoized.
int GetBuffKind(int id);

// True when a buff-bar icon id refers to an ITEM (render via the item pack)
// rather than a SPRITE. The buff widget stores the icon id at one offset
// regardless of type; the struct cache disambiguates. Unknown ids -> false
// (sprite). Same +/-1 adjacency as GetBuffName. Memoized.
bool GetBuffIconIsItem(int id);

// Augmented-item level (1..20) for a stored item XP value (RS3 item-XP curve).
int ItemLevelFromXp(int item_xp);

// All object placements in one MAPSV2 region (index 5): file 0 (land) + file 1
// (water), local 0..63 coords. The static map never changes during a session,
// so this is memoized per region. Empty when the region isn't in the cache.
std::vector<LocPlacement> RegionLocations(int region_x, int region_y);

// The searchable object a clue points at. Clue items carry the tile but never the object's
// name, so it is resolved from the map: the placement covering (x,y) on `plane` whose
// right-click options include Search or Open. Open (rather than Search) is what a LOCKED
// container exposes, which is why the action is reported rather than assumed.
// {"id":n,"name":"Crate","action":"Search","dx":n,"dy":n} - "{}" when nothing is found.
std::string ClueSearchTargetJson(int x, int y, int plane);

// Tile collision flags packed into the RegionBlockedFill output byte. Walls
// block a single edge (directional); scenery/void/diagonal block the whole tile.
constexpr std::uint8_t kTileBlockN   = 0x01;  // north edge (toward +Y)
constexpr std::uint8_t kTileBlockS   = 0x02;
constexpr std::uint8_t kTileBlockE   = 0x04;  // east edge (toward +X)
constexpr std::uint8_t kTileBlockW   = 0x08;
constexpr std::uint8_t kTileBlockFull = 0x10; // whole tile impassable
constexpr std::uint8_t kTileHasWall  = 0x20;  // a wall placement exists (render hint)

// Cache-driven walkability. Fills `out` (resized to (2*radius+1)^2; flags byte)
// for the tile grid centred on the player: out[gx*(2*radius+1)+gy] is world tile
// (player_x-radius+gx, player_y-radius+gy). A tile is unwalkable if it's a void
// tile (MAPSV2 file 3 settings 0x1, not a bridge) or covered by a blocking
// scenery (type 10/11 without the no-clip flag) or diagonal-wall (type 9)
// placement. Region grids are memoized.
void RegionBlockedFill(int player_x, int player_y, int plane, int radius,
                       std::vector<std::uint8_t>& out);

// Fill `out` (resized to (2*radius+2)^2) with the MAPSV2 cache tile-height for
// each corner of the grid lattice: out[gx*(2*radius+2)+gy] is the height at
// world tile (player_x-radius+gx, player_y-radius+gy). -32768 where unknown.
// Heights are in cache units; the overlay anchors them on the player's live z.
void RegionHeightsFill(int player_x, int player_y, int plane, int radius,
                       std::vector<std::int16_t>& out);

// MAPSV2 cache tile-height at a single world tile (cache units; -32768 unknown).
// Same source RegionHeightsFill uses -- for placing point markers on the terrain.
// Heights are ABSOLUTE surface heights (live fine-z = 32 * height), cumulative
// across planes and bridge-aware (see AbsHeightLocked).
std::int16_t TileHeight(int wx, int wy, int plane);

// Four corner surface heights of one tile (order SW,SE,NE,NW), all using THIS
// tile column's effective plane -- use for tile quads so they don't morph at
// bridge/deck seams. -32768 per corner = unknown.
void TileCornerHeights(int wx, int wy, int plane, std::int16_t out[4]);

// Bridge-aware effective plane of a column, and the absolute surface height of a
// column summed through a GIVEN effective plane (no bridge adjust). Together they
// let a multi-tile footprint make ONE layer decision for all of its corners.
int TileEffPlane(int wx, int wy, int plane);
std::int16_t TileHeightAtPlane(int wx, int wy, int eff_plane);

// PER-TILE corner heights for the whole grid, (2*radius+1)^2 tiles x 4 corners
// (SW,SE,NE,NW), indexed ((tx * T) + ty) * 4 + corner. Each tile decides its own
// effective plane ONCE and samples all four corners there -- the same rule as
// TileCornerHeights, in a single locked pass.
//
// This exists because a SHARED corner lattice cannot represent a bridge. A lattice
// point on a deck edge is owned by both the deck column and the ravine column beside
// it, but holds one height, so whichever column's bridge flag won dragged the deck
// quad down to the ravine floor. Per-tile corners let the seam be a clean step.
void RegionCornerHeightsFill(int player_x, int player_y, int plane, int radius,
                             std::vector<std::int16_t>& out);

}  // namespace rtx::cache
