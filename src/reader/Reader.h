#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// Out-of-process snapshot of every running rs2client.exe. Walks static pointer
// chains and exposes one POD per client.

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

    std::uint64_t tick_count    = 0;
    double        last_tick_ms  = 0.0;     // wall-clock ms between the last two ticks (~1 ms resolution)

    // Per-process resource usage (refreshed each Sample).
    long long     working_set_mb = 0;      // process RSS in MiB
    long long     priv_bytes_mb  = 0;      // commit / private bytes in MiB
    double        cpu_pct        = 0.0;    // % of total host CPU since last sample

    // Grand Exchange slots. Always 8 entries; empty slots have status=0.
    std::vector<GeSlot> ge_slots;

    // 29 skills in canonical order (Attack..Necromancy); empty until
    // in-world. Indices: 3 = Hitpoints, 5 = Prayer.
    std::vector<SkillLevel> skills;

    // Bank (container 95). It only exists in memory while the bank window is
    // open; it is cached to disk so it can be shown when closed. These fields
    // are a lightweight status only -- the items go via BankJson().
    bool          bank_open      = false;  // container 95 currently present
    int           bank_count     = 0;      // filled slots (live when open)
    long long     bank_cached_at = 0;      // unix seconds of the disk snapshot
    // Metal bank (container 858): same capture-while-open + disk-cache model; items via
    // MetalBankJson().
    bool          metalbank_open      = false;
    int           metalbank_count     = 0;
    long long     metalbank_cached_at = 0;
    // Archaeology material storage (container 885): same capture-while-open + disk-cache model;
    // items via MaterialsJson().
    bool          materials_open      = false;
    int           materials_count     = 0;
    long long     materials_cached_at = 0;
    // Group Ironman shared bank (container 963): same capture-while-open + disk-cache model;
    // items via GroupBankJson().
    bool          groupbank_open      = false;
    int           groupbank_count     = 0;
    long long     groupbank_cached_at = 0;
    // Anachronia bait box (container 867): same capture-while-open + disk-cache model;
    // items via BaitBoxJson().
    bool          baitbox_open      = false;
    int           baitbox_count     = 0;
    long long     baitbox_cached_at = 0;
    // Archaeologist's workbench damaged-artefact storage (container 1008): same model;
    // items via WorkbenchJson().
    bool          workbench_open      = false;
    int           workbench_count     = 0;
    long long     workbench_cached_at = 0;
};

// Host machine info (constant per boot). Cached after first call.
struct HostInfo {
    std::string   cpu_name;
    int           cpu_logical_cores = 0;
    std::string   gpu_name;                 // primary adapter
    long long     ram_total_mb     = 0;
    long long     ram_used_mb      = 0;     // live, refreshed each call
};

// Snapshot every live rs2client.exe. Empty vector when none are running.
// Idempotent + cheap; lazily attaches and refreshes the per-PID cache.
std::vector<Snapshot> SampleAll();
HostInfo              ReadHost();

// JSON variants for the JS bridge.
std::string SamplesJson();
std::string HostJson();

// On-demand reader diagnostic: exercises every major read chain (patterns, root, varps,
// varcs, interfaces, skills, containers, scene, view matrix, companion channels) and
// reports ok/fail per subsystem -- run after a game update to see exactly what broke.
//   {"version":"..","checks":[{"k":..,"ok":0|1|2,"d":..},..]}   (2 = warn/informational)
std::string ReaderHealthJson(std::uint32_t pid);

// Stable per-account key for anything stored per character. JX_DISPLAY_NAME when the Jagex
// Launcher set it, else the in-memory character name - the STEAM client sets no JX_ vars, so
// the env var alone silently yields no identity there (markers, per-account settings). Empty
// only before login, when neither source exists yet.
std::string AccountKey(std::uint32_t pid);

// Serve a per-panel read from a background cache so the UI thread never does the cross-process
// read itself (keeps it free to pump the embedded host's activation -> no window-switch hitch).
// Returns the last cached value, or "" before the first background build (caller substitutes its
// own empty-state default). `build` runs on the background thread, so it must be self-contained.
std::string ReadAsync(const std::string& key, std::function<std::string()> build);

// Nearby scene players + NPCs + named objects for one client as JSON:
//   {"players":[{"name","uid","x","y","combat"},..],
//    "npcs":   [{"name","id","uid","x","y","combat","actions":[..]},..],
//    "objects":[{"name","id","x","y","plane","type","dist","actions":[..]},..]}
// Players/NPCs are walked live out-of-process from the worldView scene-entity
// vector. Objects are cache-driven (MAPSV2 index 5 + loc configs index 16):
// named static scenery within `obj_range` tiles (Chebyshev) of the local
// player, sorted nearest-first. Objects are empty when the player tile cannot be
// resolved; all arrays are empty when not in-world.
std::string SceneJson(std::uint32_t pid, int obj_range = 20);

// Dropped ground items (scene entity type 3) the companion publishes: [{id,x,y,plane},..].
std::string GroundItemsJson(std::uint32_t pid);

// ---- World-grid overlay support ----------------------------------------
// One projectable world point. kind: 0 = object, 1 = NPC, 2 = player.
struct OverlayPoint {
    float       wx = 0, wy = 0, wz = 0;   // world FINE coords (tile * 512), z = height
    int         kind = 0;
    std::string label;
    // Optional ground footprint (the object's covered tiles): the 4 corners in
    // world-fine coords, terrain-followed, ordered SW,SE,NE,NW. has_box = draw it.
    // box_h > 0 extrudes the footprint upward into a prism (static cache locs carry no
    // model height, so a footprint-scaled height stands in -- flat quads read as floor
    // decals, not objects).
    bool        has_box = false;
    float       box[12] = {0};             // 4 * (x,y,z)
    float       box_h = 0;                 // prism height in world-fine units (0 = flat)
    // Optional live model 3D bounding box (true visual size + height) in world-fine
    // coords (x=east, y=north, z=up). Preferred over the flat footprint when present.
    bool        has_box3d = false;
    float       bmin[3] = {0}, bmax[3] = {0};
    // Highlighted NPCs: the tile they stand on (for a ground tile mark) and the
    // model-top height (label floats above the head). 0 = unknown.
    int         tile_x = 0, tile_y = 0;
    float       head_z = 0;
    bool        is_self = false;  // local player (nameplates skip it)
    int         uid = 0;                  // entity uid (sec+0x88) -- lets the launcher nameplate specific players
    int         rgb = 0;                  // guide marks only: 0 = default marker colour, else 0xRRGGBB
    int         edge_mask = 15;           // flat guide tiles only: which outline edges to draw
                                          // (bit 0 south, 1 east, 2 north, 3 west) -- region
                                          // interiors skip shared edges so a zone reads as ONE shape
};

// Transient guide site (focused-mystery guidance): a labelled world tile. The frame
// build resolves each into OverlayFrame::guides -- when the label's first line names
// a cache loc near the tile, the point carries that object's footprint prism;
// otherwise it stays a flat 1x1 tile.
// snap_obj: ignore the cache heightmap and snap the mark to the nearest live scene
// object's model AABB (true rendered height); skipped when no live object sits near
// the tile. rgb: 0 = default marker colour, else 0xRRGGBB. gx2/gy2: optional area
// extent -- the mark becomes a flat ground rect spanning (gx,gy)..(gx2,gy2), skipping
// loc matching. region: sites sharing (label, rgb) merge into one flat zone, the
// union of each site's resolved loc footprint, perimeter outlined with one label.
// plane: the MARK's plane (may differ from the player's) -- matching and heights use it,
// so an obstacle above/below the player still boxes correctly.
struct GuideSite { int gx = 0, gy = 0; std::string label; bool snap_obj = false; int rgb = 0; int gx2 = 0, gy2 = 0; int region = 0; int plane = 0; };

// Everything the overlay needs for one frame of one client, gathered in a
// single locked pass. `matrix` is the live view-projection matrix (16 floats
// at worldView + 0x13070); apply the perspective-divide to project a
// world-fine point to game-client pixels.
struct OverlayFrame {
    bool        ok = false;
    float       matrix[16] = {0};
    int         player_tx = 0, player_ty = 0, plane = 0;
    float       player_z  = 0;            // fine z used as the grid plane height
    float       player_fx = 0, player_fy = 0;   // player FINE world position (smooth, sub-tile)
    std::int16_t anchor_h = -32768;       // player tile cache surface height (informational; heights are absolute: fine-z = 32 * h); -32768 = none
    std::vector<OverlayPoint> points;     // players/NPCs/objects (per the want* flags)
    int         grid_r = 0;               // grid radius; blocked grid is (2r+1)^2
    std::vector<std::uint8_t> blocked;    // 1 = unwalkable; idx gx*(2r+1)+gy, tile (player-r+g)
    std::vector<std::int16_t> heights;    // cache tile-height per grid corner ((2r+2)^2); -32768 = unknown
    std::vector<OverlayPoint> highlights; // NPCs to highlight (e.g. random events), drawn prominently
    std::vector<OverlayPoint> guides;     // resolved guide sites (footprint prism or flat tile + label)
    std::vector<int> guide_path;          // BFS walkable path player -> first guide site, tile (x,y) pairs
    // Direction arrow: a ground chevron at the player pointing toward the objective tile
    // (the active highlighted NPC, or the first guide site). Shows DIRECTION, not a path.
    bool        has_arrow = false;
    int         arrow_tx = 0, arrow_ty = 0;
    // Gameview (3D viewport) rect within the window, in PHYSICAL PIXELS. The game
    // publishes it in varcs (view 1000: x 3005, y 3006, w 3001, h 3002 -- see
    // script8701/8709, which read/write a uniform x,y,w,h record per view); the
    // interface widget 1477:27->28 is the fallback. The camera renders into THIS
    // rect, not the whole client, so the projection must use it (tiles drift
    // otherwise when UI is docked).
    // gv_w/gv_h == 0 means "not resolved" -> overlay falls back to the window.
    // The rect is in the game's LOGICAL interface space: identical to the backbuffer
    // in the normal case, but a per-monitor-aware client on an OS-scaled monitor lays
    // its interface out at physical/scale and upscales (seen live: viewport 1707x890
    // inside a 2560-wide backbuffer on a 150% monitor). lc_w/lc_h is the full client
    // in that SAME space (the 1477 root frame), so backbufferW / lc_w is the exact
    // logical->pixel conversion for this rect; 0 = unresolved (assume 1:1).
    int gv_x = 0, gv_y = 0, gv_w = 0, gv_h = 0;
    int lc_w = 0, lc_h = 0;
    // Interface-space -> physical-pixel scale, derived live as the published pixel
    // gameview width over the same rect measured in the interface widget tree. That
    // ratio IS the Interface Scaling setting (70%..300%, setting 481 -> enum 488)
    // combined with any sub-800x600 downscale, without having to locate either.
    // 0 = underivable this frame -> callers fall back to the 800x600 model.
    float ui_scale = 0.0f;
};

// Gather a frame for `pid`. Entities are walked live; objects come from the
// MAPSV2 cache for the occupied regions (only when want_objects); the
// walkability grid (cache-driven) is filled for `grid_radius` tiles around the
// player. Any live NPC whose name contains one of `highlight_names` (case-
// insensitive), or whose uid (sec+0x88) is in `outline_uids`, is added to
// out.highlights regardless of want_npcs (outline matches carry the live model AABB
// box; name matches carry only a centre point). Returns ok=false when not in-world.
bool BuildOverlayFrame(std::uint32_t pid, bool want_players, bool want_npcs,
                       bool want_objects, bool want_specials, int grid_radius, bool interactable,
                       const std::vector<std::string>& highlight_names,
                       const std::vector<int>& outline_uids,
                       const std::vector<GuideSite>& guide_sites,
                       OverlayFrame& out);

// Local-player info for one client as JSON (for the Player State tab):
//   {"in":bool,"x":N,"y":N,"plane":N,"region":N,"lx":N,"ly":N,"anim":N,"moving":bool}
// region = (x>>6)<<8 | (y>>6); lx/ly = local tile within the region (0..63).
std::string PlayerInfoJson(std::uint32_t pid);

// Local player's current GLOBAL tile (x,y) + plane (same chain as PlayerInfoJson). Returns
// false when not in-world. Used by the tile-marker keybind to mark the player's tile.
bool PlayerTile(std::uint32_t pid, int& tx, int& ty, int& plane);


// Bank contents for one client as JSON:
//   {"open":bool,"character":"..","cached_at":N,"count":N,
//    "items":[[slot,item_id,stack],..]}   (filled slots only)
// Live from container 95 when the bank is open, otherwise from the disk
// cache for that client's character. Empty items when neither is available.
std::string BankJson(std::uint32_t pid);

// Metal bank (container 858: ores + bars). Same model as BankJson -- live when the metal
// bank is open at a forge, else the per-character disk cache.
std::string MetalBankJson(std::uint32_t pid);

// Archaeology material storage (container 885). Same model as BankJson -- live while the
// material storage UI is open, else the per-character disk cache.
std::string MaterialsJson(std::uint32_t pid);

// Group Ironman shared bank (container 963). Same model as BankJson -- live while the group
// storage UI is open, else the encrypted per-character disk cache. JSON identical to BankJson.
std::string GroupBankJson(std::uint32_t pid);

// Anachronia bait box (container 867). Same model as BankJson -- live while the bait-box UI is
// open, else the encrypted per-character disk cache. JSON identical to BankJson.
std::string BaitBoxJson(std::uint32_t pid);

// Archaeologist's workbench damaged-artefact storage (container 1008). Same model as BankJson --
// live while the workbench UI is open, else the encrypted per-character disk cache. Capacity is
// gated by the guild-shop workbench upgrades (varbit 61463 -> 125/175/225), and the container
// holds nothing at all until the first upgrade is bought. JSON identical to BankJson.
std::string WorkbenchJson(std::uint32_t pid);

// Inventory (container 93) and worn equipment (container 94) for one client as
// JSON, both live from the interface-container manager:
//   {"present":bool,"count":N,"cap":N,"items":[[slot,item_id,stack,"name"],..]}
// `cap` is the container's slot capacity (28 for the inventory), `count` the
// number of filled slots, and `items` lists only the filled slots (slot =
// array index; equipment slot indices follow container 94's worn-slot layout).
// present=false when the container isn't mapped (not in-world).
std::string InventoryJson(std::uint32_t pid);
std::string EquipmentJson(std::uint32_t pid);
// Read ANY live container by id -> {"present":bool,"count":N,"cap":N,"items":[[slot,id,stack,name],..]}.
// Used by the storage-box tracker (e.g. wood box = container 937). Same chain as inventory/bank.
std::string ContainerItemsJson(std::uint32_t pid, int container_id);
// Enumerate EVERY container currently live in the interface-container manager (most exist only while
// their window is open) -> {"containers":[{"id":N,"count":N,"cap":N},..]}. Backs the Containers panel.
std::string OpenContainersJson(std::uint32_t pid);
// Player-Owned Farm pens (851-857): species + primary trait per animal, bank-style per-pen disk cache.
// {"pens":[{"id":N,"open":bool,"cached_at":sec,"animals":[[item,trait,"species"],..]},..]}
std::string PofJson(std::uint32_t pid);
// Per-slot Extra_ints of an item -> {"present":bool,"key":[..],"pos":[..]} (base gem bag = packed counts).
// slot < 0 = the first slot holding item_id (legacy behaviour). Pass a slot to read THAT
// slot: two stacks of the same id (e.g. two Passages of the abyss) carry different
// per-instance values, and an id-only lookup always returned the first one's.
std::string ItemExtraIntsJson(std::uint32_t pid, int container_id, int item_id, int slot = -1);

// Read player varps live from the MainData+0x36040 hashmap. `ids_csv` = comma-
// separated varp ids; returns {"<id>":value,..}. Unset varps read back as 0 (engine
// default). Foundation for varbit-driven panels: a varbit is a bit-slice of a varp,
// so the caller fetches the underlying varp(s) and extracts the bits.
std::string VarpsJson(std::uint32_t pid, const std::string& ids_csv);
// Read varbit ids (cache def -> backing varp + bit slice) -> {"<id>":value,..}.
std::string VarbitsJson(std::uint32_t pid, const std::string& ids_csv);

// Membership tier + live idle time ->
//   {"resolved":bool,"member":0|1,"premier":0|1,"jagexAccount":0|1,"tier":0|1|2,
//    "idleLogoutSeconds":300|600|900,"idleMs":<ms or -1>,"expiryRaw":<u64>}
// tier: 0 = free, 1 = member, 2 = premier. NOTE this is not script19385's ordering (that one
// returns 1 for premier and 2 for member); this ascends with entitlement instead.
// member comes from engine state, NOT a var -- it reproduces the PLAYERMEMBER op's own two
// dereferences off MainData. premier is varbit 50572. `resolved` is false before login, when
// the account object is still null; treat every other field as meaningless then.
// idleLogoutSeconds is the ADDITIVE idle budget: 5 min base, +5 members, +5 Jagex account,
// capped at 15 (premier is NOT part of it). The timer only runs OUT OF COMBAT.
// idleMs IS live -- ms since the client last reported input to the server (pointer flush or key
// batch), which is what the server's timer actually runs on; -1 while unavailable.
std::string MembershipJson(std::uint32_t pid);

// Dump EVERY currently-set player varp by polling the varp hashmap directly, keyed
// "4:<id>" (scope 4 = varp). The Vars tab uses this for varp/varbit rows: server-updated
// varps (box counts, etc.) the companion push-observer never re-captures are always current
// here. External-only (no companion). See VarpsDumpAllJson in Reader.cpp.
std::string VarpsDumpAllJson(std::uint32_t pid);
// Every set varc-int polled from the global client-var hashmap (store+0x7630), keyed "5:<id>". External-only.
std::string VarcsDumpAllJson(std::uint32_t pid);
// Read specific varcs as 64-bit values from the same global map -> {"<id>":"<i64 as string>",..}.
// For long-typed varcs (death-interface item prices 4829-4875/7109-7111, at-risk varc 4876).
std::string VarcLongsJson(std::uint32_t pid, const std::string& ids_csv);
// Read specific varcs as 32-bit INTS from the same global map -> {"<id>":<i32>,..}. The right
// read for int-typed varcs (window records, toggles): the i64 read picks up adjacent union
// bytes in the high half and turns a clean 0/1 into 19-digit garbage.
std::string VarcIntsJson(std::uint32_t pid, const std::string& ids_csv);
std::string VarpsLongJson(std::uint32_t pid, const std::string& ids_csv);
// Read specific varc-STRINGS by id from the same global map -> {"<id>":"<string>",..} (present+parseable
// ids only). The targeted read for a known string var, e.g. 2251 = the interface-1177 hover tooltip.
std::string VarcStringsJson(std::uint32_t pid, const std::string& ids_csv);
// Every currently-populated varc-STRING polled from the global map, keyed "2:<id>" (scope 2). The Vars
// tab merges this so string vars (tooltip/names/examines) show with no companion. External-only.
std::string VarcStringsDumpAllJson(std::uint32_t pid);

// Live snapshot of EVERY scope's vars (varp/varbit/varc/string-int) resolved in-process
// by the companion's var-push observer -- the single read path for client vars (varc) the
// launcher can't resolve externally, and equally for varps/varbits. Keyed "<scope>:<id>"
// (separate id-spaces). {} when the companion isn't loaded / the watcher isn't enabled.
std::string VarsDumpJson(std::uint32_t pid);

// Enable/disable the in-process var observer (it only does work while enabled). Call with
// true when the Vars watcher opens, false when it closes. False if the companion's absent.
bool VarsWatch(std::uint32_t pid, bool on);

// Server->client packet protocol, read live from the running client (no game hook):
// enumerates every inbound opcode (0x00..0xE5) with its length/kind and handler RVA.
// {"ok":false,...} when unattached or the opcode table can't be resolved.
std::string ServerPacketsJson(std::uint32_t pid);
// Live decoded-packet feed from the companion's framer hook. Returns records with
// seq > `since` plus capture-health meta. {"ok":false,...} when the companion/panel
// isn't built into the running client.
std::string ServerPacketFeedJson(std::uint32_t pid, std::uint64_t since);
// Arm/disarm the companion framer capture (re-armed each poll while the panel is open;
// auto-disarms ~3s after the last arm). False if the companion section isn't present.
bool ServerPacketFeedEnable(std::uint32_t pid, bool on);

// Render toggles: which 0 = hide NPCs, 1 = hide other players, 2 = hide whole scene,
// 3 = keep the client rendering at full rate while its window is unfocused,
// 4 = true-embed (client is a child of the host with detached input queues).
bool RenderToggle(std::uint32_t pid, int which, bool on);
// The client window that actually takes keyboard input (the render-target child inside the
// client's frame), published by the companion. 0 until the client has presented a frame.
// The embedded-mode host must relay key messages here -- the frame window ignores them.
std::uint64_t RenderInputWindow(std::uint32_t pid);

// Live tick state for `pid`: server-tick `count` + `age_ms` since the last tick
// (precise QPC; -1 if unknown). Returns false if the client isn't tracked. Backs
// the overlay metronome (count -> beat every N ticks, age -> smooth phase).
bool TickState(std::uint32_t pid, std::uint32_t& count, double& age_ms);

// Current XP of all 29 skills for `pid` (one block read off the live skill
// records; same source as the snapshot). out[i] = XP, or -1 when that record
// isn't available yet. Returns false when the client isn't tracked / the chain
// isn't readable (lobby, loading). Backs the overlay XP-tracker panel.
bool SkillsXp(std::uint32_t pid, int out[29]);

// Augmented items in inventory (container 93) + equipment (94) as JSON:
//   {"items":[{"container":N,"slot":N,"id":N,"name":"..","xp":N,"level":N,
//              "gizmos":N,"perks":[{"id":N,"name":".."},..]},..]}
// Reads each container slot's extended data (Extra_ints): [1]=item XP, [3]/[5]=
// gizmo perk pairs (two 15-bit ids), [4]=gizmo count. Perk names + level come
// from the cache. Empty when not in-world or nothing is augmented.
std::string PerksJson(std::uint32_t pid);

// Interfaces inspector (lazy, via the static interface-container chain):
//   InterfaceGroupsJson      -> {"groups":[{"id":G,"n":topLevelCount},..]}  (cheap)
//   InterfaceGroupJson(gid)  -> {"widgets":[{"g":G,"t":[i1,i2,i3],"d":depth,"r":[x,y,w,h]},..]}
// The UI lists groups (collapsed) and fetches a group's widgets only on expand,
// so the whole tree is never walked unless asked.
std::string InterfaceGroupsJson(std::uint32_t pid);
std::string InterfaceGroupJson(std::uint32_t pid, int groupId);
// Screen rects of specific components in a group, anchored to a 1477 mount slot (celtic-knot arrow boxes).
std::string IfaceCompRectsJson(std::uint32_t pid, int group, const std::string& compsCsv, int mountComp);
// Absolute screen rect of the layer CONTAINING `sprite` in `group`. Window chrome has no
// stable component id (the same tab strip is comp 109 in one layout, 306 in another), but
// its sprite does, so this addresses it by sprite and returns the container. Search-only,
// so it is not subject to InterfaceGroupJson's 4000-widget output cap.
// {"ok":1,"x":..,"y":..,"w":..,"h":..} or {"ok":0}.
std::string IfaceSpriteParentRectJson(std::uint32_t pid, int group, int sprite);
// Compass-clue needle rotation (group 996 / comp 5 / +0x180), or -1 if not open. bearing_N = raw/5.8127.
int CompassHeadingValue(std::uint32_t pid);
// Compass-clue dig tile straight from varc 1323 (packed (plane<<28)|(x<<14)|y) -> "x,y,plane" or "" if unset.
std::string CompassTargetJson(std::uint32_t pid);
// Scan-clue SOLUTION tile from the server. The game sends inbound opcode 83 (payload
// 02 00 00 <xHi xLo> <yHi yLo> 14 00 96 FF FF FF FF) carrying the exact dig tile, but ONLY while the
// player is within the orb's range (red pulse, <=11 paces). Arms the companion netprobe hook and returns
// the freshest matching record as {"ok":true,"x":X,"y":Y}, freshness-gated so it
// clears when the player leaves range. Requires the companion netprobe framer hook.
std::string ScanSolutionJson(std::uint32_t pid);
// Entity under the mouse, from the engine's stable hover-target slot (*(input_proc+0x13F8)):
// {"ok":true,"kind":"npc"|"player"|"loc","verb":..,"name":..,"id":..,"uid":..,"x":..,"y":..,"p":..}.
// Locs carry id + tile inline in the slot; NPCs/players resolve their scene uid against the live
// entity vector. Target-less verbs (Walk here/Cancel/Continue) report {"ok":true,"verb":..} alone.
std::string HoverEntityJson(std::uint32_t pid);
// Puzzle-box board (group 1931 / comp 18): 25 raw cell sprites row-major as JSON, or "[]" if not open.
std::string PuzzleStateJson(std::uint32_t pid);
// Absolute screen rects of the 25 puzzle-box cells (for highlighting the next tile in-game):
// {"abs":1,"cells":[[x,y,w,h]|null x25]}. "abs":0 = origin unresolved (don't draw); "[]" if not open.
std::string PuzzleCellRectsJson(std::uint32_t pid);
// Search every open group for components sized (w x h) +- tol -> {"matches":[{"g","c","s","w","h","ax","ay"},..]}.
// Pairs a panel to its game-frame mount slot by size (e.g. donation 656's 512x352 root <-> 1477:728).
std::string InterfaceSizeSearchJson(std::uint32_t pid, int w, int h, int tol);

// Live per-group origin nudge (dx,dy) added to the panel's resolved screen origin. For visually
// calibrating an interface highlight from the Interfaces tab without a rebuild. dx==dy==0 clears it.
void SetIfaceOffset(int group, int dx, int dy);

// Live dialogue option-select state (group 1188) with each option's exact text and
// ABSOLUTE screen rect (movable-panel position varc + within-group offset), for the
// quest auto-highlight. {} when no dialogue is open. hasAbs=false if the panel
// position varc isn't published yet (companion off / not resolved).
std::string DialogJson(std::uint32_t pid);

// Live text + absolute screen rect of specific COMPONENTS of an open interface group. compsCsv is a
// comma-separated comp-id list. General reader for tracking a panel -- e.g. NPC chat (group 1184):
// comp 4 = NPC name, comp 10 = message, comp 15 = the continue button (space/click target).
//   {"group":G,"open":bool,"hasAbs":bool,"comps":[{"comp":C,"text":"..","x":..,"y":..,"w":..,"h":..},..]}
std::string InterfaceCompsJson(std::uint32_t pid, int group, const std::string& compsCsv);

// Every movable panel whose position varc has resolved, with its true screen rect, for the
// Interfaces-tab panel visualizer. [{"name":..,"x":..,"y":..,"w":..,"h":..}, ..]
std::string PanelRectsJson(std::uint32_t pid);

// Live screen rect of a backpack slot (group 1473), positioned via the dialogue pipeline (panel
// varc + live within-group walk; no hardcoded grid). {"x","y","w","h","n"} or {} if unavailable.
std::string InvSlotRectJson(std::uint32_t pid, int slotIndex);

// Chat log lines (chatbox group 137, component-86 line widgets). Each line is the
// RAW message string (RS3 markup "<col=..>"/"<img=N>" + embedded "[HH:MM:SS]"
// timestamp intact, UTF-8-safe) plus its base text colour (the per-channel colour
// </col> resets to). Newest first. The UI parses the time, renders the colours,
// and accumulates a deduped searchable log. "{}" when not in-world.
//   {"lines":[{"raw":"<raw markup>","base":RRGGBB},..]}
std::string ChatJson(std::uint32_t pid);

// Active buffs (group 284) + debuffs (group 291) from the buff-bar widgets:
//   {"buffs":[{"sprite":S,"item":I,"name":"..","timer":"..","secs":N},..],
//    "debuffs":[..]}
// Per active slot: slotArr=*(slot+0x1c8) -> icon *(slotArr+0x8) (sprite u16
// @+0x188, item id i32 @+0x1a0) + timer text *(slotArr+0x20) (inline @+0x180).
// Names from the struct cache; the JS resolves the icon (itemIcon for item
// buffs, sprite for the rest). Empty arrays when not in-world / none active.
std::string BuffsJson(std::uint32_t pid);

// Active per-ability cooldowns from the engine cooldown registry (mgr embedded
// at root+0x19F78; hashmap ability_id->expiry vs CLIENTCLOCK at mod_base+0xED2FF8):
//   {"clock":N,"cooldowns":[{"id":K,"remaining":MS,"flag":F},..]}
// Empty when no per-ability cooldown is active (GCD-only basics aren't tracked).
std::string AbilityCooldownsJson(std::uint32_t pid);

// Bound abilities on every action bar (main group 1430 + secondaries 1670..1673), walked live
// from the interface tree. Per bar: the bound abilities in slot order, each
// {slot,id,name,enabled}; id == the cooldown-registry key, so the UI joins AbilityCooldownsJson
// for live cooldowns and gates the secondary bars on varbits 29138..29141.
//   {"bars":[{"bar":N,"group":G,"slots":[{"slot":N,"id":K,"name":"..","enabled":bool},..]},..]}
std::string ActionBarJson(std::uint32_t pid);

}  // namespace rtx::reader
