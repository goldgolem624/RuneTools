#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>


namespace rtx::reader {

struct SkillLevel {
    int real    = 0;    // base level
    int boosted = 0;    // current level incl. buffs/drains
    int xp      = -1;   // experience points; -1 when not available (Necromancy)
};

struct GeSlot {
    int           slot     = 0;            // 0..7
    int           status   = 0;            // 0=empty, 2=active, 5=complete
    int           type     = 0;            // 0=BUY, 1=SELL (only meaningful when status != 0)
    int           item_id  = 0;
    long long     price    = 0;            // per item, gp (i64)
    int           quantity = 0;
    int           filled   = 0;            // completed_quantity
    long long     filled_value = 0;        // completed_value, gp
};

struct Snapshot {
    std::uint32_t pid           = 0;
    std::string   client_version;          // VS_VERSIONINFO ProductVersion

    bool          in_world      = false;   // world > 0
    int           world         = 0;
    int           status        = -1;
    std::string   status_label;             // "" when raw byte is unverified
    std::string   display_name;             // JX_DISPLAY_NAME from target PEB
    std::string   gfx_mode;                 // OpenGL | Vulkan | DirectX; empty when undetermined

    std::uint64_t tick_count    = 0;
    double        last_tick_ms  = 0.0;     // wall-clock ms between the last two ticks (~1 ms resolution)

    long long     working_set_mb = 0;      // process RSS in MiB
    long long     priv_bytes_mb  = 0;      // commit / private bytes in MiB
    double        cpu_pct        = 0.0;    // % of total host CPU since last sample

    std::vector<GeSlot> ge_slots;

    std::vector<SkillLevel> skills;

    bool          bank_open      = false;  // container 95 currently present
    int           bank_count     = 0;      // filled slots (live when open)
    long long     bank_cached_at = 0;      // unix seconds of the disk snapshot
    bool          metalbank_open      = false;
    int           metalbank_count     = 0;
    long long     metalbank_cached_at = 0;
    bool          materials_open      = false;
    int           materials_count     = 0;
    long long     materials_cached_at = 0;
    bool          groupbank_open      = false;
    int           groupbank_count     = 0;
    long long     groupbank_cached_at = 0;
    bool          baitbox_open      = false;
    int           baitbox_count     = 0;
    long long     baitbox_cached_at = 0;
    bool          workbench_open      = false;
    int           workbench_count     = 0;
    long long     workbench_cached_at = 0;
};

struct HostInfo {
    std::string   cpu_name;
    int           cpu_logical_cores = 0;
    std::string   gpu_name;                 // primary adapter
    long long     ram_total_mb     = 0;
    long long     ram_used_mb      = 0;     // live, refreshed each call
};

std::vector<Snapshot> SampleAll();

// What the background sampler (every 200 ms) last saw of each client, without sampling again: for callers
// that only need who is where and their skill XP.
struct ClientPresence {
    std::uint32_t pid = 0;
    int           status = -1;
    bool          in_world = false;
    std::string   display_name;
    bool          have_xp = false;
    int           xp[29] = {};        // -1 where unavailable
};
std::vector<ClientPresence> LastPresence();
HostInfo              ReadHost();

std::string SamplesJson();
std::string HostJson();

std::string ReaderHealthJson(std::uint32_t pid);

std::string AccountKey(std::uint32_t pid);

std::string ReadAsync(const std::string& key, std::function<std::string()> build);

std::string SceneJson(std::uint32_t pid, int obj_range = 20);

// Live terrain (the game's own height grids, fine units). Snapshot once per overlay build for the
// 3x3 regions around a tile, then look corners up from the snapshot. INT32_MIN = unknown.
bool LiveTerrainSnapshot(std::uint32_t pid, int cx, int cy, int plane);
std::int32_t LiveCornerHeight(std::uint32_t pid, int wx, int wy, int plane);
std::int32_t LiveTileLift(std::uint32_t pid, int wx, int wy, int plane);   // per-tile standing offset the game adds for actors (usually 0)
int LiveTileEffPlane(std::uint32_t pid, int wx, int wy, int plane);        // plane + 1 on bridge tiles, as the game samples them
int LiveTileVoid(std::uint32_t pid, int wx, int wy, int plane);            // map tile settings bit 0 (void/blocked) on the effective plane; -1 unknown
bool LiveCornerHeights(std::uint32_t pid, int wx, int wy, int plane, std::int32_t out[4]);   // tile corners as stood on: effective plane + offset

// Combat log: hitsplat events on every actor in the scene. Poll at 5 Hz from one thread; read
// events with seq > since (max_events capped at 2000). JSON: {seq, gap, events:[...]}.
void CombatLogPoll(std::uint32_t pid);
std::string CombatLogJson(std::uint32_t pid, std::uint64_t since, int max_events);

std::string GroundItemsJson(std::uint32_t pid);

struct OverlayPoint {
    float       wx = 0, wy = 0, wz = 0;   // world FINE coords (tile * 512), z = height
    int         kind = 0;
    std::string label;
    bool        has_box = false;
    float       box[12] = {0};             // 4 * (x,y,z)
    float       box_h = 0;                 // prism height in world-fine units (0 = flat)
    bool        has_box3d = false;
    float       bmin[3] = {0}, bmax[3] = {0};
    int         tile_x = 0, tile_y = 0;
    float       head_z = 0;
    bool        is_self = false;  // local player (nameplates skip it)
    int         uid = 0;                  // entity uid (sec+0x88)
    int         rgb = 0;                  // guide marks only: 0 = default marker colour, else 0xRRGGBB
    int         rgb2 = 0;                 // guide marks only: optional second tone (two-tone flat tile)
    int         edge_mask = 15;           // flat guide tiles only: outline edges to draw (bit 0 south, 1 east, 2 north, 3 west)
    bool        has_true = false;         // players/NPCs: true_x/y came from a live movement route (actor is moving)
    int         true_x = 0, true_y = 0;   // players/NPCs: server tile (visible tile when has_true is false)
    int         src = 0;                  // kind 5 (true-tile outline) only: 1 = NPC, 2 = player
};

struct GuideSite { int gx = 0, gy = 0; std::string label; bool snap_obj = false; int rgb = 0; int gx2 = 0, gy2 = 0; int region = 0; int plane = 0; int rgb2 = 0; };

// matrix: live view-projection, 16 floats at worldView + 0x13070; perspective-divide to project.
struct OverlayFrame {
    bool        ok = false;
    float       matrix[16] = {0};
    std::uint64_t matrix_addr = 0;        // where in the game that matrix was read from
    int         player_tx = 0, player_ty = 0, plane = 0;
    float       player_z  = 0;            // fine z used as the grid plane height
    float       player_fx = 0, player_fy = 0;   // player FINE world position (smooth, sub-tile)
    int         player_ttx = 0, player_tty = 0; // player server tile from the movement route (visible tile when stationary)
    bool        player_route = false;           // the route was non-empty (player is moving)
    std::int16_t anchor_h = -32768;       // player tile cache surface height (informational; heights are absolute: fine-z = 32 * h); -32768 = none
    std::vector<OverlayPoint> points;     // players/NPCs/objects (per the want* flags)
    int         grid_r = 0;               // grid radius; blocked grid is (2r+1)^2
    std::vector<std::uint8_t> blocked;    // 1 = unwalkable; idx gx*(2r+1)+gy, tile (player-r+g)
    std::vector<std::int16_t> heights;    // cache tile-height per grid corner ((2r+2)^2); -32768 = unknown
    std::vector<std::int32_t> heights_fine; // live terrain per grid corner, fine units, same indexing; INT32_MIN = unknown (empty when the live grid was unreadable)
    std::uint32_t pid = 0;                // client the frame was built from (live terrain lookups)
    std::vector<OverlayPoint> highlights; // NPCs to highlight (e.g. random events), drawn prominently
    std::vector<OverlayPoint> guides;     // resolved guide sites (footprint prism or flat tile + label)
    std::vector<int> guide_path;          // BFS walkable path player -> first guide site, tile (x,y) pairs
    bool        has_arrow = false;
    int         arrow_tx = 0, arrow_ty = 0;
    // Gameview rect in interface space: varcs (view 1000) x 3005, y 3006, w 3001, h 3002; fallback widget 1477:27->28.
    int gv_x = 0, gv_y = 0, gv_w = 0, gv_h = 0;
    int lc_w = 0, lc_h = 0;
    float ui_scale = 0.0f;
};

struct OutlineLocReq { int id = 0, x = 0, y = 0, plane = 0; };

bool BuildOverlayFrame(std::uint32_t pid, bool want_players, bool want_npcs,
                       bool want_objects, bool want_specials, int grid_radius, bool interactable,
                       bool want_true_tile,
                       const std::vector<std::string>& highlight_names,
                       const std::vector<int>& outline_uids,
                       const std::vector<OutlineLocReq>& outline_locs,
                       const std::vector<GuideSite>& guide_sites,
                       OverlayFrame& out);

bool ReadViewMetrics(std::uint32_t pid, OverlayFrame& out);

std::string PlayerInfoJson(std::uint32_t pid);
std::string SocialJson(std::uint32_t pid);   // world id + friends list with worlds

bool PlayerTile(std::uint32_t pid, int& tx, int& ty, int& plane);


std::string BankJson(std::uint32_t pid);

std::string MetalBankJson(std::uint32_t pid);

std::string MaterialsJson(std::uint32_t pid);

std::string GroupBankJson(std::uint32_t pid);

std::string BaitBoxJson(std::uint32_t pid);
std::string NexusJson(std::uint32_t pid);      // Necromancy nexus (container 953), disk-cached

std::string WorkbenchJson(std::uint32_t pid);

std::string InventoryJson(std::uint32_t pid);
std::string EquipmentJson(std::uint32_t pid);
std::string ContainerItemsJson(std::uint32_t pid, int container_id);
std::string OpenContainersJson(std::uint32_t pid);
std::string PofJson(std::uint32_t pid);
std::string ItemExtraIntsJson(std::uint32_t pid, int container_id, int item_id, int slot = -1);

std::string LocMorphsJson(std::uint32_t pid, const std::string& ids_csv);
std::string VarpsJson(std::uint32_t pid, const std::string& ids_csv);
// The same read without JSON: out[i] = value of ids[i]. False when the client is not readable.
bool Varps(std::uint32_t pid, const std::vector<int>& ids, std::vector<int>& out);
std::string VarbitsJson(std::uint32_t pid, const std::string& ids_csv);

std::string MembershipJson(std::uint32_t pid);
std::string ClientStateJson(std::uint32_t pid);   // typing focus, cutscene id, client option values

std::string VarpsDumpAllJson(std::uint32_t pid);
std::string ServerOpsJson();
std::string VarcsDumpAllJson(std::uint32_t pid);
std::string VarDomainStoresJson(std::uint32_t pid);
std::string VarcLongsJson(std::uint32_t pid, const std::string& ids_csv);
std::string VarcIntsJson(std::uint32_t pid, const std::string& ids_csv);
std::string VarpsLongJson(std::uint32_t pid, const std::string& ids_csv);
std::string VarcStringsJson(std::uint32_t pid, const std::string& ids_csv);
std::string VarcStringsDumpAllJson(std::uint32_t pid);

std::string VarsDumpJson(std::uint32_t pid);

bool VarsWatch(std::uint32_t pid, bool on);

std::string ServerPacketsJson(std::uint32_t pid);
std::string ServerPacketFeedJson(std::uint32_t pid, std::uint64_t since);
bool ServerPacketFeedEnable(std::uint32_t pid, bool on);
std::string EventsJson(std::uint32_t pid, std::uint64_t since);
bool EventsMaskSet(std::uint32_t pid, const std::uint32_t mask[8]);

bool RenderToggle(std::uint32_t pid, int which, bool on);
std::string GpuTimingJson(std::uint32_t pid);   // companion per-pass GPU timing (Vulkan)
std::uint64_t RenderInputWindow(std::uint32_t pid);

bool TickState(std::uint32_t pid, std::uint32_t& count, double& age_ms);

bool SkillsXp(std::uint32_t pid, int out[29]);

std::string PerksJson(std::uint32_t pid);

std::string InterfaceGroupsJson(std::uint32_t pid);
std::string InterfaceGroupJson(std::uint32_t pid, int groupId);
std::string IfaceCompRectsJson(std::uint32_t pid, int group, const std::string& compsCsv, int mountComp);
std::string IfaceSpriteParentRectJson(std::uint32_t pid, int group, int sprite);
int CompassHeadingValue(std::uint32_t pid);
std::string CompassTargetJson(std::uint32_t pid);

std::string ScanSolutionJson(std::uint32_t pid);
// Hover target from the engine slot *(input_proc+0x13F8).
std::string HoverEntityJson(std::uint32_t pid);
// The scenery under the cursor, from the same slot, and the entry the scene walk has for it:
// the tile and definition id the scene share lists the object under. in_scene is false when
// the walk has no such object.
struct HoverLocInfo {
    int         id = 0, x = 0, y = 0;   // what the game reports: definition and hovered tile
    std::string verb;                   // the default action the game shows for it
    // an item in an interface slot instead of scenery: HoverLoc returns false and fills these
    int         item_id = -1, item_slot = -1, item_iface = 0, item_comp = 0;
    bool        in_scene = false;
    int         scene_x = 0, scene_y = 0, scene_id = 0;
};
bool HoverLoc(std::uint32_t pid, HoverLocInfo& out);
std::string PuzzleStateJson(std::uint32_t pid);
std::string PuzzleCellRectsJson(std::uint32_t pid);
std::string InterfaceSizeSearchJson(std::uint32_t pid, int w, int h, int tol);

void SetIfaceOffset(int group, int dx, int dy);

std::string DialogJson(std::uint32_t pid);

std::string InterfaceCompsJson(std::uint32_t pid, int group, const std::string& compsCsv);

std::string PanelRectsJson(std::uint32_t pid);

std::string InvSlotRectJson(std::uint32_t pid, int slotIndex);

std::string ChatJson(std::uint32_t pid);

// Buff slot: slotArr=*(slot+0x1c8); icon *(slotArr+0x8) (sprite u16 @+0x188, item i32 @+0x1a0); timer *(slotArr+0x20), inline @+0x180.
std::string BuffsJson(std::uint32_t pid);

std::string AbilityCooldownsJson(std::uint32_t pid);

std::string ActionBarJson(std::uint32_t pid);

}  // namespace rtx::reader
