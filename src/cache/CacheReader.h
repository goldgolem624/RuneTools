#pragma once

#include "MapLocations.h"

#include <string>
#include <utility>
#include <vector>


namespace rtx::cache {

struct ItemInfo {
    std::string name;
    int         ge_limit  = -1;  // 4-hour buy limit; -1 = no limit / unknown
    long long   value     = -1;  // store value in gp
    bool        augmented = false; // Invention-augmented (holds item XP + gizmos)
};

ItemInfo GetItem(int item_id);

std::string ItemName(int item_id);

bool ItemIsAugmented(int item_id);

std::string ItemInfoJson(int item_id);

struct CacheParseRow {
    std::string name;              // surface label, e.g. "items"
    int  ok = 0, total = 0;        // clean / attempted
    bool sampled = false;          // total is an every-Nth sample, not the full index
    int  stop_op = -1;
    int  stop_n  = 0;              // records that stopped on stop_op
};
std::vector<CacheParseRow> CacheParseHealth();
std::string CacheProbeUnknownOps();

std::string ItemIconCoverageJson(bool (*has)(int item_id));

std::string MapAreasJson();
std::string MapAreaImageDataUrl(int areaId, bool thumb);
std::string SpriteDataUrl(int sprite_id);
std::string SpriteDataUrlScaled(int sprite_id, int px, int frame = 0);
int SpriteIdByName(const std::string& name);

std::vector<std::uint8_t> SpriteRgba(int sprite_id, int& w, int& h);
std::vector<std::uint8_t> SpritePng(int sprite_id, int frame);   // one frame as PNG bytes (empty when absent)   // raw RGBA pixels (for the HUD overlay)

struct NpcMeta {
    std::string              name;
    std::vector<std::string> actions;       // non-empty right-click options, in order
    int                      combat_level = -1;
    int                      size = 1;       // tile footprint from opcode 12; 2 = a 2x2 npc
    int                      id = -1;        // def that provided the name/model (resolved morph child for a morph base)
};

NpcMeta GetNpc(int npc_id);

bool GetNpcMorph(int npc_id, int& varbit, int& varp, int& def_child, std::vector<int>& variants);

struct LocMeta {
    std::string              name;
    std::vector<std::string> actions;   // non-empty right-click options, in order
    int                      dim_x = 1;  // footprint (opcode 14)
    int                      dim_y = 1;  // footprint (opcode 15)
};

LocMeta GetLoc(int loc_id);

std::string MenuDefJson(int kind, int id);

std::string MenuFindJson(int kind, const std::string& name);

std::string MenuSearchJson(int kind, const std::string& query, int limit);

bool GetLocMorph(int loc_id, int& varbit, int& varp, int& def_child, std::vector<int>& variants);

bool GetVarbit(int varbit_id, int& varp, int& lsb, int& msb);
// Domain-5 (item instance) varbit: `var` is the instance key (INV_GETVAR). Perks use 30212 (item XP), 30215..30222 (gizmo perks).
bool GetObjVarbit(int varbit_id, int& var, int& lsb, int& msb);

std::string PerkName(int perk_id);
std::string PerkDesc(int perk_id);
int PerkRankCount(int perk_id);

std::string MystPagesJson();

// Bit is over varps 9297/9298/11740 (field study) and 9299/9300/11741 (report), 32 bits each; the panel tests it.
std::string ArchResearchJson();

std::string VarbitMapJson();

std::string QuestsJson();
// Quest-tracker health: how many listed quests resolved a progress tracker, and whether the
// CONFIGS index had an archive it could not read. {"quests":n,"tracked":n,"failedArchives":n}
std::string QuestHealthJson();

std::string ParamDefJson(int param_id);

std::vector<std::uint8_t> SoundOgg(int index_id, int sound_id);

std::vector<std::vector<std::uint8_t>> SoundOggChunks(int index_id, int sound_id);

std::string SoundListJson(int index_id, int start_id, int limit);

std::string IfaceGroupDefsJson(int group_id);

std::string EnumJson(int enum_id);

std::string AbilityConfigsJson();
std::string BuffCatalogJson();

std::string NpcJson(int npc_id);

std::string MapWindowJson(int cx, int cy, int plane, int half = 0, int ts = 0, int want = 15);

std::string StructParamsJson(int structId);
// Single struct params (decoded once per struct and memoised). false when the struct or key is absent.
bool StructIntParam(int structId, int key, int& out);
bool StructStrParam(int structId, int key, std::string& out);

std::string MapLabelsJson();

std::string MapLocNamesJson();

std::string MapCategoriesJson();

std::string MapSymbolsJson();

int PanelMountComp(int group_id);

std::string DbRowsJson(int masterTable);

std::string ItemParamsJson(int item_id);

std::vector<int> ItemVarobjs(int item_id);

std::string ItemFileHex(int item_id);
std::string ConfigFileHex(int archive, int file);
std::string LocFileHex(int loc_id);
std::string ConfigArchiveInfo(int archive);
std::string VarbitDomainsJson();
std::string VarbitDomainMapJson();
std::string VarDefsJson(int archive);

std::string GetBuffName(int id);

std::string GetDebuffName(int id);

// Kind bitfield from struct params 8110-8113: bit0 countdown, bit1 count, bit2 percentage, bit3 custom string. 0 = unknown.
int GetBuffKind(int id);

bool GetBuffIconIsItem(int id);

int ItemLevelFromXp(int item_xp);

std::vector<LocPlacement> RegionLocations(int region_x, int region_y);

std::string ClueSearchTargetJson(int x, int y, int plane);

constexpr std::uint8_t kTileBlockN   = 0x01;  // north edge (toward +Y)
constexpr std::uint8_t kTileBlockS   = 0x02;
constexpr std::uint8_t kTileBlockE   = 0x04;  // east edge (toward +X)
constexpr std::uint8_t kTileBlockW   = 0x08;
constexpr std::uint8_t kTileBlockFull = 0x10; // whole tile impassable
constexpr std::uint8_t kTileHasWall  = 0x20;  // a wall placement exists (render hint)

void RegionBlockedFill(int player_x, int player_y, int plane, int radius,
                       std::vector<std::uint8_t>& out);

void RegionHeightsFill(int player_x, int player_y, int plane, int radius,
                       std::vector<std::int16_t>& out);

std::int16_t TileHeight(int wx, int wy, int plane);

void TileCornerHeights(int wx, int wy, int plane, std::int16_t out[4]);

int TileEffPlane(int wx, int wy, int plane);
std::int16_t TileHeightAtPlane(int wx, int wy, int eff_plane);

void RegionCornerHeightsFill(int player_x, int player_y, int plane, int radius,
                             std::vector<std::int16_t>& out);

}  // namespace rtx::cache
