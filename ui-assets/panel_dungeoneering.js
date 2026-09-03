// RuneToolsX panel: Dungeoneering (Daemonheim floor status + explored floor map).
// Spliced inline into client.html at load; IIFE (window exports + registerTab; see the RTX registry in client.html).
//
// Sources = the live interface trees (bridge interfaceGroup), which mirror the
// server-pushed varcs (CS2-mapped: keys held = varc 1812-1875 via enum 5734
// (script2725 rebuilds the HUD key row from them), floor timer = varc 4190 seconds
// (script9734 ticks it on 945:11), speedrun progress = varc 1233/2381 (script5873),
// gatestone flags = varc 6569/6570 (spr 13165 set / 13166 unset on 945:2 / 945:4).
// Group 945 = the floor HUD; group 942 = the floor map (32px room tiles + 2831 start
// overlay, 24px key markers, 11px spr 2825 gatestone marker; pooled widgets park at
// garbage coords like 2534,-30969 and are dropped by the sane-rect clamp).
// The "Hard Mode!" text exists in the tree even when inactive (the +0x50 vis mask reads
// 0 across groups 942/945), so hidden-vs-shown text cannot be trusted -- only dynamic
// content (created key rows, text values, sprite ids) is displayed here.
(function () {

let dungData = null; let dungFetching = false; dungSig = '';

const DUNG_KEY_COLORS = ['Orange', 'Silver', 'Yellow', 'Green', 'Blue', 'Purple', 'Crimson', 'Gold'];
const DUNG_KEY_SHAPES = ['triangle', 'diamond', 'rectangle', 'pentagon', 'corner', 'crescent', 'wedge', 'shield'];
const DUNG_KEY_HEX = { Orange: '#f59e42', Silver: '#c9ced9', Yellow: '#f5d442', Green: '#5fd07a',
                       Blue: '#5b9cf6', Purple: '#a06bff', Crimson: '#f25c5c', Gold: '#f0c419' };
// World-tile -> map-cell mapping. Rooms are 14x14 tiles with a 2-tile unwalkable
// gap on every side -> 16-tile pitch ( The grid is
// grounded per floor by the start-room CONSTELLATION FIT below (the smuggler
// wanders, so his tile can't anchor it); world +x = east (map col+), +y = north
// (map row-). The anchor is cached per floor; cleared on floor reset / exit.
const DUNG_ROOM_PITCH = 16, DUNG_ROOM_W = 14;
const DUNG_MAP_XSIGN = 1, DUNG_MAP_YSIGN = -1;
let dungPlanDbg = '';      // planner counters for the debug line
let dungMandatoryKeyRooms = {};   // cell -> 1: yielded a key nothing works without
let dungForcedKeys = {};          // key idx -> 'route' | 'blocks': proven-forced keys,
                                  // published for the tooltip tier between "marked
                                  // critical" (human) and "not critical" (nothing)
let dungKeyFillerVeto = {};       // key idx -> 1: planner ignores its crit mark (behind filler)
let dungFloorSW = null;    // {x,y} world tile of the start room's SW corner (constellation fit)
// Start-room reference constellation: loc id -> room-local [x,y] from the SW corner
// (-1/14 = door locs in the gaps). The fit in dungReconcileScene matches these under
// 4 rotations to ground each new floor. One merged table serves every theme: themes
// never coexist on a floor, absent ids simply don't vote, and the SHARED ids (2342,
// 49257, 123933, 137198/199/205 -- same local spot in both themes measured so far)
// keep one consistent entry. Only single-instance locs are listed; ids that appear
// several times in a start room (abandoned: 25636 x10, 50343 doors x4, 51050-52 x2)
// would vote for several translations and are left out.
const DUNG_SW_REF = {
  // frozen theme (wall-touch calibrated
  2342: [1, 12], 17144: [1, 12], 49257: [6, 6], 49934: [6, 12], 49937: [0, 5],
  50035: [9, 13], 50191: [1, 1], 50195: [3, 0], 50196: [0, 3], 50197: [2, 2],
  50205: [6, 8], 50229: [7, 0], 50241: [13, 7], 50346: [-1, 7], 50374: [7, -1],
  50386: [14, 7], 51156: [12, -1], 51456: [10, 0], 51457: [13, 0], 51577: [4, 9],
  53124: [14, 3], 123933: [1, 1], 123948: [7, 7], 137159: [6, 6],
  137198: [13, 12], 137199: [13, 13], 137205: [13, 11],
  // abandoned theme (floor-12 live snapshot: SW corner pinned by the four
  // 50343 door locs at the wall-centre gaps; bridged into this canonical frame by the
  // six shared ids, which all fit one rot-180 placement exactly)
  17146: [1, 12], 49935: [9, 8], 49938: [0, 4], 50036: [9, 12], 50192: [0, 0],
  50198: [2, 0], 50199: [0, 2], 50200: [2, 2], 50206: [6, 8], 50224: [6, 0],
  50232: [0, 6], 50240: [13, 6], 50273: [6, 14], 50433: [6, -1], 50441: [-1, 6],
  50449: [14, 6], 50604: [11, -1], 50910: [10, 0], 50911: [13, 0], 51030: [3, 9],
  53125: [14, 3], 123932: [6, 6],
  // remaining themes, CACHE-derived (js5-5 start-room template bank at x=113,
  // y 5249-5313 / 5377-5441 / 5505-5537; bank order after frozen+abandoned suggests
  // furnished / occult / warped -- label unconfirmed, the fit doesn't need it).
  // Only rotation-safe ids: 1x1 footprints whose class is  to surface
  // (Anvil/Summoning obelisk/Statue counterparts matched the template EXACTLY on
  // both live-measured themes), plus the 3x3 smuggler-stall family 17142/48/50 at
  // the live-consistent [1,12] (square footprints report rotation-invariant tiles;
  // 17144/17146 both measured there live). Non-square locs (furnace 2x2, anvils
  // 1x2, table 3x2, RC altar 4x3) shift with floor rotation -> deliberately absent.
  // UNVALIDATED until a live floor 18+ fit confirms (expect >=6 votes there).
  17142: [1, 12], 50203: [2, 2], 50207: [6, 8], 52004: [10, 0], 52005: [13, 0],   // theme bank 3
  17148: [1, 12], 54887: [2, 2], 53883: [6, 8], 54662: [10, 0], 54663: [13, 0],   // theme bank 4
  17150: [1, 12], 55815: [2, 2], 55605: [6, 8],                                   // theme bank 5
};
let dungLastTimer = -1;   // varc 4190 seconds; a drop means a new floor -> re-anchor
let dungKeyCache = {};    // "x,y" -> {x,y,ki}: ground keys REMEMBERED until picked up
let dungOpenedAt = {};    // cell -> Date.now() the room was FIRST seen opened. The map
                          // reveals a room instantly but its ground key is only scanned
                          // a beat later, so "this room holds no key" is not trusted
                          // until the room has been open for a moment (see the prune).
let dungRoomRes = {};     // cell -> { "skillIdx|tier|level": {skill,skillIdx,tier,level} }
                          // EVERY resource ever seen in that room. `rooms` is rebuilt
                          // each poll from the LIVE scene, so without this a room's
                          // contents vanish the moment you walk out of render range.
let dungHeldInit = false; // the first poll on a floor seeds dungHeldSeen WITHOUT
                          // attributing: keys already in the ring were not picked up here
let dungHeldSeen = {};    // key idx -> 1: already seen in the key ring, so a later
                          // appearance is not a fresh pickup
let dungKeyDoor = {};     // key idx -> cell of the door that NEEDS it. Follows the marker
                          // while visible, latches on open, because opening the door clears
                          // the requirement and the fact would otherwise be lost -- and it
                          // is the fact that proves the trip to fetch the key was forced.
let dungKeySrc = {};      // key idx -> {x,y} world tile the key was PICKED UP from: the
                          // source room of a critical key stays on the critical path
                          // even after pickup turns it into a "dead end"
const DUNG_KEY_MANUAL = 'manual promotion';   // dungCritKeys value for a hand-promoted key
const DUNG_KEY_PARTY = 'party mark';          // dungCritKeys value for a party-relayed mark
let dungCritKeyBlock = {};   // key idx -> 1: hand-DEMOTED keys the auto-latch must not re-add
// STRUCTURALLY DERIVED crit keys (planner-written): a key fetched from a proven-critical
// dead end is critical, because that branch exists to fetch it and reaches nothing else.
// Kept apart from dungCritKeys so a derivation is never relayed to the party as if it
// were a human mark, and a hand demotion still silences it like any other mark.
let dungDerivedCritKeys = {};   // key idx -> why-string
let dungCritKeyTouch = {};   // key idx -> Date.now() of the last local toggle: the party merge
                             // won't override it for ~3s (same optimistic hold as noncrit)
let dungRestored = false;    // floor-marks restore ran this UI session (a panel rebuild resets
                             // every module variable -- manual marks must survive it)
// Persist the floor's marks across PANEL REBUILDS (a hand-promoted
// critical key vanished on reload). Keyed to the floor by the timer varc: restore only
// when the current timer is AT/PAST the saved one, so a new floor never inherits.
function dungSaveMarks() {
  try {
    localStorage.setItem('rtxDgMarks', JSON.stringify({ t: Date.now(), tm: dungLastTimer,
      ck: dungCritKeys, cb: dungCritKeyBlock, nc: dungManualNonCrit, mc: dungManualCrit, dl: dungDoorLevels, ks: dungKeySrc, kd: dungKeyDoor, rr: dungRoomRes,
      cl: dungCritLatch,
      sw: dungFloorSW }));   // the ANCHOR too -- see below
  } catch (e) {}
}
function dungDropMarks() { try { localStorage.removeItem('rtxDgMarks'); } catch (e) {} }
// A KEY IS CRITICAL ONLY IF A HUMAN SAID SO. There is exactly one tier: dungCritKeys,
// holding keys promoted by hand (right-click) and marks relayed by a party mate.
// The panel derives nothing: "this key was found somewhere critical, therefore the key is
// critical" is unsound, and a guess relayed over the party channel comes back as
// DUNG_KEY_PARTY, latched and indistinguishable from an observation. A sound derivation
// would need its own tier, ranked BELOW this one, unlatched, and never relayed.
let dungCritKeys = {};        // key idx -> why: hand promotions, local and party mates'
// Critical unless hand-demoted.
// **A key found on the critical path does NOT guarantee its door leads to another
// critical room** -- it is an indicator, not a proof. That is why
// nothing derives criticality any more.
function dungKeyIsCrit(i) { return !dungCritKeyBlock[i] && (!!dungCritKeys[i] || !!dungDerivedCritKeys[i]); }
function dungKeyWhy(i) { return dungCritKeys[i] || dungDerivedCritKeys[i] || ''; }
function dungKeyName(i) { const k = dungKeyInfo(18202 + 2 * (i - 1)); return k ? k.name : ''; }
// attribute-safe text for the panel's own tooltips (names come from the game, so quotes and
// angle brackets must not be able to break out of the attribute)
function dungAttr(t) { return String(t == null ? '' : t).replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); }
let dungDoorLevels = {};  // map cell 'gx,gy' -> {skill, level}: skill-door requirement
                          // parsed from the examine tooltip (group 1177 text / varc-str
                          // 2251, "...requires level N <Skill> to optimally unlock...");
                          // EXAMINING the door is required, hover alone never writes it.
let dungTipLast = '';     // last processed 1177 tooltip sentence (change-triggered)
// The examine sentence is TRANSIENT and the door it describes can only be resolved
// once the map is built. Capturing and attaching in one step meant an examine was
// silently lost whenever the Dungeoneering tab wasn't the active one (fetch is
// tab-gated) or the tooltip closed between polls. So the SENTENCE is now observed on
// the tab-independent 600ms tick and parked here -- anchored by the EXAMINED DOOR'S
// OWN TILE (from the engine hover slot, which holds the loc's id + world tile while
// the cursor is on it), with the player's tile as fallback -- and attached by the
// next full fetch.
let dungPendingTip = null;   // {text, x, y, dx, dy, t} -- x/y = player tile, dx/dy = examined door's own tile
let dungTipDbg = '';         // last attach attempt, surfaced on the map's debug line
let dungHoverLoc = null;     // {x, y, t}: last hovered scenery. The engine hover slot carries the
                             // loc's OWN world tile, and examining requires the cursor on the door,
                             // so this is the exact door tile at examine time.
function dungReadDoorTip() {
  try {
    if (bridge().hoverEntity) {
      const hv = JSON.parse(rtxData.sync('state.hoverEntity') || '{}');
      if (hv && hv.ok && hv.kind === 'loc' && hv.x > 0) dungHoverLoc = { x: hv.x, y: hv.y, t: Date.now() };
    }
  } catch (e) {}
  try {
    const w = dungFetchGroup(1177).find(w2 => w2.x && /requires level \d+/i.test(w2.x));
    if (w && w.x !== dungTipLast) {
      dungTipLast = w.x;
      const hl = (dungHoverLoc && Date.now() - dungHoverLoc.t < 6000) ? dungHoverLoc : null;
      dungPendingTip = { text: w.x, x: dungSelfPos ? dungSelfPos.x : null,
                         y: dungSelfPos ? dungSelfPos.y : null,
                         dx: hl ? hl.x : null, dy: hl ? hl.y : null, t: Date.now() };
    }
  } catch (e) {}
}
let dungPartyStats = {};  // display name -> [level per SKILL_NAMES idx] from the official
                          // hiscores (index_lite). Session cache; usage is gated to the
                          // CURRENT floor's roster, so stale non-party stats never apply.
let dungHsPoll = {};      // names whose fetch is in flight (polled, no re-ping)
let dungHsDone = {};      // names resolved (ok or error) this session -- never re-asked
let dungPbOpen = false;   // Party best validation grid: collapsed by default
let dungPartyOpen = false;   // master Party section (roster + best + sync): collapsed by default
let dungSelfLvSig = '';   // last own-live-levels report (code|name|levels); re-sent on change
let dungSyncInput = '';   // in-progress text in the "join a code" box (survives re-renders)
let dungManualNonCrit = {};   // map-cell 'gx,gy' -> 1: right-clicked to force NON-critical
                              // (excluded from crit evidence + faded). Per-floor; shared across
                              // party clients via the 'noncrit' sync fact.
let dungNonCritTouch = {};    // cell -> Date.now() of the last local mark/unmark: the party
                              // merge won't override a cell for ~3s so an optimistic local
                              // change isn't undone before its report round-trips.
let dungPartyHoldUntil = 0;   // after a local floor reset: skip re-ingesting the launcher's
                              // door/noncrit cache until the party 'reset' round-trips (the
                              // cache may briefly still hold the PREVIOUS floor's facts).
let dungManualCrit = {};      // map-cell 'gx,gy' -> 1: LEFT-CLICKED to force CRITICAL
                              // (evidence like any other; approach chain lights). Mutually
                              // exclusive with dungManualNonCrit; party-synced ('critroom').
let dungManualCritTouch = {}; // cell -> Date.now() of the last local crit mark/unmark (3s
                              // optimistic hold against the party mirror, like noncrit)
// ROOMS PROVEN CRITICAL THIS FLOOR. Demotion needs proof, not an inference or evidence
// going stale: onPath is rebuilt from scratch every poll, so a proof that stops being
// OBSERVABLE (key spent, marker gone) would unmark rooms mid-floor. Point facts latch
// here and only the player's own non-crit mark clears one. Floor-scoped, persisted.
let dungCritLatch = {};       // map-cell 'gx,gy' -> the why-string captured at proof time
// THE PATH CURRENTLY BEING EXPLORED. Keep exploring one path until it yields either a key
// that opens another door or a dead end; switching branches early is usually less
// efficient. The last recommended frontier; the ranking may only move OFF its branch for an
// openable door or when the branch ends. Floor-scoped, not persisted (a UI preference,
// not a floor fact).
let dungStickyObj = '';
let dungNonCritSeen = {};     // cells CONFIRMED in the shared set at least once: a mirror may
let dungManualCritSeen = {};  // only REMOVE a mark it has seen (a real teammate un-mark) --
                              // never one that simply failed to arrive (old build / old server
                              // rejecting the fact kind deleted fresh local marks otherwise)
let dungHoverCell = '';       // map cell under the mouse -- H routes to it
// mark/unmark a room non-critical locally AND relay to the party (same cell coords are
// absolute floor-grid, identical for every client in the instance). The two manual
// designations are mutually exclusive: setting one clears the other everywhere.
function dungSetNonCrit(cell, on) {
  if (on) { dungManualNonCrit[cell] = 1; delete dungManualCrit[cell]; dungManualCritTouch[cell] = Date.now(); }
  else delete dungManualNonCrit[cell];
  dungNonCritTouch[cell] = Date.now();
  dungPartyReport('noncrit', { cell: cell, on: !!on });
  if (on) dungPartyReport('critroom', { cell: cell, on: false });
  dungSig = ''; renderDungeoneering();
}
function dungSetManualCrit(cell, on) {
  if (on) { dungManualCrit[cell] = 1; delete dungManualNonCrit[cell]; dungNonCritTouch[cell] = Date.now(); }
  else delete dungManualCrit[cell];
  dungManualCritTouch[cell] = Date.now();
  dungPartyReport('critroom', { cell: cell, on: !!on });
  if (on) dungPartyReport('noncrit', { cell: cell, on: false });
  dungSig = ''; renderDungeoneering();
}
// Cross-PC party sync: read/generate/join the shared code via the launcher bridge.
function dungSyncCode() { try { return (bridge().partyGetCode && rtxData.sync('party.partyGetCode')) || ''; } catch (e) { return ''; } }
function dungSyncAction(act, wrap) {
  try {
    if (act === 'new') {
      const chars = 'abcdefghijkmnpqrstuvwxyz23456789';   // no easily-confused chars
      let code = '';
      for (let i = 0; i < 6; i++) code += chars[Math.floor(Math.random() * chars.length)];
      rtxData.sync('party.partySetCode', code); dungSyncInput = '';
    } else if (act === 'join') {
      const box = wrap.querySelector('.dg-sync-in');
      const code = ((box ? box.value : dungSyncInput) || '').trim().toLowerCase();
      if (code.length >= 4) { rtxData.sync('party.partySetCode', code); dungSyncInput = ''; }
    } else if (act === 'leave') {
      rtxData.sync('party.partySetCode', ''); dungSyncInput = '';
    }
  } catch (e) {}
  dungSig = ''; renderDungeoneering();
}
// skill sprites (cache js5-8), indexed by SKILL_NAMES order -- same table the
// XP tracker uses (block at 16038 is alphabetical; Necromancy is 30936)
const DUNG_SKILL_SPR = [
  16040, 16045, 16160, 16041, 16058, 16057, 16055, 16043, 16197, 16051,
  16050, 16049, 16044, 16061, 16056, 16052, 16038, 16196, 16060, 16048,
  16059, 16053, 16042, 16195, 16047, 16046, 16054, 16039, 30936];
// HELP-THE-GHOST (restoration) room. One id per THEME FAMILY, all five cache-verified
// by matching name+actions across js5-16 -- a solver written from the single
// live id would go silent on the other four floors, which is how every other room here
// has broken at least once.
//   Damaged pillar  Repair
//   Broken pot      Repair
//   Jewellery box   Fill
const DUNG_GHOST_PILLAR = { 54580: 1, 54591: 1, 54602: 1, 55457: 1, 55472: 1 };
const DUNG_GHOST_POT    = { 54577: 1, 54588: 1, 54599: 1, 55455: 1, 55470: 1 };
const DUNG_GHOST_BOX    = { 54576: 1, 54587: 1, 54598: 1, 55453: 1, 55468: 1 };
// The COFFIN is the room's payoff: unlock it once the restoring is done.
//. Five variants, cache-matched on name+Unlock. NB the families are NOT
// arithmetically aligned across themes -- the live room showed box 55453 beside coffin
// 40181 -- so every id is listed and whichever is in the room wins. Do not "derive" the
// coffin from the box id.
const DUNG_GHOST_COFFIN = { 40181: 1, 54571: 1, 54582: 1, 54593: 1, 55465: 1 };
//...and once unlocked the same tile also carries a Bless-remains coffin, which is the
// real last step. Both variants coexist, so the BLESS one being
// present is what says the unlock is already done -- same rule as the fixed pot/pillar.
// The families are NOT aligned: four themes pair +1 (54571->54572), but the live room
// held Unlock 40181 beside Bless 55451. Match by membership, never by arithmetic.
const DUNG_GHOST_COFFIN_BLESS = { 54572: 1, 54583: 1, 54594: 1, 55451: 1, 55466: 1 };
// DONE MARKERS. The fixed model appears ALONGSIDE the broken one at the same tile rather than
// replacing it, and its presence is what says that part is finished. So each task is checked PER TILE: an actionable object whose done-twin
// shares its tile is already handled and must not be guided, or the room keeps telling
// you to repair a pot you have already repaired.
const DUNG_GHOST_POT_DONE    = { 54578: 1, 54589: 1, 54600: 1, 55456: 1, 55471: 1 };
const DUNG_GHOST_PILLAR_DONE = { 54581: 1, 54592: 1, 54603: 1, 55458: 1, 55473: 1 };
// Box done-ids are the actionless "Jewellery box" locs. 55454 is  and
// 55469 pairs the same way (+1 from its Fill twin 55468); the rest are the remaining
// actionless boxes that are NOT clearly other content. UNLIKE the pot and pillar
// families this mapping is not arithmetically consistent -- the Fill boxes 54576/54587/
// 54598 are each followed by a Broken pot -- so these three are INFERRED, not verified.
// A wrong entry here can only matter if that exact id turns up in a ghost room.
const DUNG_GHOST_BOX_DONE    = { 55454: 1, 55469: 1, 40173: 1, 40180: 1, 55464: 1 };
// "Antique ring", the ONLY item in the cache with that name, so no theme table needed.
// It is a GROUND ITEM both on the floor and in the pack -- dungGroundCache / dungInvCount,
// never `objs`.
const DUNG_GHOST_RING = 19879;
// Ghost-room puzzle: NPC 10989 is the ghost to KILL (10990 are decoys). Outline
// its bounding box IN-SCENE (by uid, since all ghosts share the name "Ghost") and
// reconcile as ghosts come and go. Outline persists in-scene once set.
// Sliding-block puzzle: 8 block NPCs on a 3x3 grid (SE cell empty when solved). Each
// id has a fixed TARGET cell [col 0=W..2=E, row 0=N..2=S].
// FIVE THEME FAMILIES (js5-18 scan: every npc named "Sliding block" with a
// Move action). Supporting only 12125-12132 meant the solver was silent on four floors
// out of five -- the same theme-variant gap that hid the Lights-Out, crystal and chest
// solvers:
//   12125-12132   12133-12140   12141-12148   12149-12156   12963-12970
// Each family is 8 CONSECUTIVE ids with 8 DISTINCT models, and the id order within a
// family is the grid's reading order.
// *** VERIFIED only for 12125-12132 (the original table). For the other four the
// index -> cell mapping is INFERRED FROM POSITION: there is no constant model offset
// between families, so the cache cannot confirm the piece order carries across themes.
// If a block's marked target is wrong in game, that family needs its own capture --
// the id set is right regardless, so it will guide to the wrong cell rather than not
// guide at all. ***
// EXCLUDED: 17043-17046 and 26307-26310 are also "Sliding block" but come in FOURS,
// so they are a different (smaller) grid, not a 3x3 -- guessing cells for them would
// be wrong 8 ways out of 9.
const DUNG_PUZZLE = (() => {
  const cells = [[0, 0], [1, 0], [2, 0],   // NW  N  NE
                 [0, 1], [1, 1], [2, 1],   //  W  C   E
                 [0, 2], [1, 2]];          // SW  S  (SE empty)
  const t = {};
  for (const base of [12125, 12133, 12141, 12149, 12963])
    cells.forEach((c, i) => { t[base + i] = c; });
  return t;
})();
// Statues puzzle: pushable -> its static target statue (see dungReconcileScene).
// ALL FIVE THEME FAMILIES, derived from the cache: js5-18 npcs named "Statue"; PUSHABLE = has Push+Pull,
// STATIC = no action. Each theme is a block of 4 statics + 4 pushables paired BY
// INDEX, and every pair is verified to share the same op1 MODEL id (the statue you
// match by appearance) -- so this is measured, not an assumed id offset. Note the
// offset is NOT constant: 12 for the 109xx blocks, 4 for the 121xx/129xx ones.
// (Daemonheim has 6 floor tiers but only 5 statue themes -- Abandoned 1 and 2 share.)
// NB 13069-13088 are also "Statue" with a Push action but are a DIFFERENT puzzle
// (Push only, no Pull; 5 models x 4 orientation variants) -- deliberately excluded.
const DUNG_STATUE_PAIR = {
  10954: 10942, 10955: 10943, 10956: 10944, 10957: 10945,   // models 54891/54900/54891/54900
  10958: 10946, 10959: 10947, 10960: 10948, 10961: 10949,   // models 54898/54890/54898/54890 (floor 13)
  10962: 10950, 10963: 10951, 10964: 10952, 10965: 10953,   // models 54887/54889/54887/54889
  12121: 12117, 12122: 12118, 12123: 12119, 12124: 12120,   // models 54887/54889/54887/54889
  12956: 12952, 12957: 12953, 12958: 12954, 12959: 12955,   // models 61661/61660/61661/61660
};
let dungStatues = null;   // [{id, east, cur:[x,y], tgt:[x,y], done}] while the room is in scene
let dungMonoCharge = null;   // varc 1233 while in a dungeon (generic room progress: monolith 0-195, emotes 67/134/201)
// Poison maze: what a mined pedestal is WORTH in running tiles. Measured
//: ~4s to mine one, ~3.3 tiles/s running -> breaking through is only
// worth it when the detour round would be longer than ~13 tiles. Tune either
// number and the router re-balances itself.
const DUNG_MAZE_MINE_SEC = 4, DUNG_RUN_TILES_PER_SEC = 3.3;
const DUNG_MAZE_MINE_TILES = Math.round(DUNG_MAZE_MINE_SEC * DUNG_RUN_TILES_PER_SEC);
const DUNG_MAZE_TIMER_VAR = 1233;   // poison-maze countdown (starts ~205 after the switch is pulled, ticks to 0);
                                    // shared varc 1233 (also monolith/emote progress) -- room context disambiguates
let dungMazeTimer = null;    // live maze countdown value while in the maze room
let dungArmableTiles = {};   // "x,y" -> 1: tiles that held an arm-statue this visit (armed ones re-id as references)

// Daemonheim skilling resources -> {skill, skillIdx, level} from CACHE ids,
// decoded from js5-16 locs / js5-18 npcs, not the wiki). Hunter + Divination are NPCs;
// Fishing/Woodcutting/Mining/Farming are locs (id = base + 2*(tier-1), x3 variants at
// +0/+20/+40). Level = the fixed Dungeoneering tier scale (Divination tops at 85, the
// rest at 90). skillIdx = the RS3 skill id (SKILL_NAMES order: WC 8, Fish 10, Mine 14,
// Farm 19, Hunter 21, Div 25).
const DUNG_RES_SCALE = [1, 10, 20, 30, 40, 50, 60, 70, 80, 90];
const DUNG_RES_DIV   = [1, 10, 20, 30, 40, 50, 60, 70, 80, 85];
// A completable resource within this many levels of the party's best is "guaranteed"
// critical regardless of tier (at level 120, anything requiring >105
// is guaranteed critical -> a 15-level margin). Tunable.
const DUNG_CRIT_MARGIN = 15;
// Doors use a TIGHTER near-the-party window than resources ("this
// filter is 10 levels" on a level-77 door at party best 92 shown as near-critical).
const DUNG_DOOR_CRIT_MARGIN = 10;
// Gathering skills that can go PAST 99 (all of them, in modern RS3). The T8 rule is
// UNIVERSAL for these ("it's universal for the skills that go past 99"
// -- confirmed live on both a level-102 WC party (T8 tree) AND a level-102 Mining party
// (T8 rock)): for a party still below 110 in the skill, tier 8 is also on the critical
// path. Only applies where T8 is actually completable (its level <= the party best;
// otherwise it's a bonus tier).
const DUNG_CAP110_SKILLS = { Woodcutting: 1, Mining: 1, Fishing: 1, Farming: 1, Hunter: 1 };
// 10 name sequence).
const DUNG_RES_FAMS = [
  ['Woodcutting', 8,  2, [49705, 49725, 49745, 53751, 55494, 82267]],
  ['Mining',      14, 2, [49766, 49786, 49806, 53771, 55514, 82247]],
  ['Farming',     19, 2, [49826, 49846, 49866, 53791, 55534, 82287]],
  ['Fishing',     10, 1, [49923, 82307]],
  ['Hunter',      21, 1, [11086]],
];
function dungResource(id) {
  if (id >= 18140 && id <= 18149)
    return { skill: 'Divination', skillIdx: 25, tier: id - 18139, level: DUNG_RES_DIV[id - 18140] };
  for (const [sk, sidx, st, bases] of DUNG_RES_FAMS)
    for (const b of bases) {
      const d = id - b;
      if (d < 0 || d > st * 9 || d % st) continue;
      const tier = d / st + 1;
      return { skill: sk, skillIdx: sidx, tier: tier, level: DUNG_RES_SCALE[tier - 1] };
    }
  return null;
}
// Local player's BASE level in RS3 skill `idx` (skills[idx] = [real, boosted, xp]), or
// null. FUTURE: the party's HIGHEST in that skill would be more correct, but other
// players' skill levels aren't in this client's memory -- only the local player's.
function dungSkillLevel(idx) {
  try { const s = lastSnap && lastSnap.skills && lastSnap.skills[idx]; return (s && s[0] > 0) ? s[0] : null; } catch (e) { return null; }
}
// Is this roster entry the local player? Compare on alphanumerics only, lowercased -- immune to
// the space-vs-NBSP difference between the account name (JX_DISPLAY_NAME, regular
// space) and the party interface name (NBSP), and to any stray encoding byte.
function dungIsSelf(n) {
  if (!dungSelfName) return false;
  const strip = s => (s || '').replace(/[^a-z0-9]/gi, '').toLowerCase();
  return strip(n) === strip(dungSelfName);
}
// PARTY's best level in RS3 skill idx: own live level, raised by any current
// roster member's hiscore level (any party member can cut the node / open the
// door, and the generator scales content to the party). Self's hiscore entry is
// skipped -- the live skill read is authoritative here.
function dungPartyBest(idx) {
  let best = dungSkillLevel(idx), by = null;
  for (const n in dungPartyStats) {
    if (!dungPartyRoster[n] || dungIsSelf(n)) continue;
    const lv = dungPartyStats[n] && dungPartyStats[n][idx];
    if (typeof lv === 'number' && (best == null || lv > best)) { best = lv; by = n; }
  }
  return { best: best, by: by };
}
// Highest resource TIER (1-10) the party can actually COMPLETE in a skill: the top
// tier whose required level is <= the party's best level. null when no level is known.
function dungMaxTier(skillIdx, skillName) {
  const b = dungPartyBest(skillIdx);
  if (b.best == null) return { maxTier: null, best: b.best, by: b.by };
  const scale = skillName === 'Divination' ? DUNG_RES_DIV : DUNG_RES_SCALE;
  let mt = 0;
  for (let t = 0; t < scale.length; t++) if (scale[t] <= b.best) mt = t + 1;
  return { maxTier: mt, best: b.best, by: b.by };
}
// A resource's band RELATIVE to the party's ceiling:
//   'bonus'    = a tier ABOVE the party's level (uncompletable -> likely a bonus room),
//   'critical' = a completable tier that is either in the top 2 the party can do, OR
//                within DUNG_CRIT_MARGIN levels of the party's best (guaranteed band,
//                which widens the band for high-level parties -- at 120, >=105 is
//                critical), OR tier 8 for a sub-110 party on a 110-cap gathering skill
//                (a level-102 WC party's critical path includes a T8 tree),
//   'filler'   = completable but below all of those -> off the critical path.
function dungResBand(res, maxTier, best) {
  if (best == null || maxTier == null) return null;
  if (res.level > best) return 'bonus';                              // can't complete it
  const topBand = res.tier >= maxTier - 1;                           // top 2 completable tiers
  const guaranteed = res.level >= best - DUNG_CRIT_MARGIN;           // within the guaranteed margin
  const t8 = !!DUNG_CAP110_SKILLS[res.skill] && best < 110 && res.tier === 8;   // sub-110 gathering
  return (topBand || guaranteed || t8) ? 'critical' : 'filler';
}
// Criticality is COMPOSED, not single-factor : a top-band resource
// marks the PATH only when the room can actually carry it -- a dead-end room with just
// the resource (one door, no ground key, no locked door, no gatestone) is a detour
// worth doing, never the route. Its green T-badge stays; the pink path ring does not.
// Unknown door layout counts as pathworthy (don't suppress on missing data).
function dungResPathworthy(c, kk, roomsMap) {
  if ((c.groundKeys && c.groundKeys.length) || c.key || c.gate) return true;
  // Same trap as evOf and the prune: a room known but not yet walked into carries
  // doors = 0, which is missing data, not a one-door dead end.
  if (c.unex || !c.doors) return true;      // unknown layout: don't suppress on missing data
  // EFFECTIVE door count : a door into a room marked
  // NON-critical is an eliminated branch and doesn't count -- a T9 whose only other
  // exit leads to an eliminated room is effectively a dead end. Doors into unknown
  // space still count (they could lead onward).
  let n = 0;
  if (kk && roomsMap) {
    const p = kk.split(',').map(Number);
    for (const st of [[1, 0, -1], [2, 1, 0], [4, 0, 1], [8, -1, 0]]) {
      if (!(c.doors & st[0])) continue;
      if (!dungManualNonCrit[(p[0] + st[1]) + ',' + (p[1] + st[2])]) n++;
    }
  } else {
    n = (c.doors & 1) + ((c.doors >> 1) & 1) + ((c.doors >> 2) & 1) + ((c.doors >> 3) & 1);
  }
  return n >= 2;
}
// Party-best level in the skill a captured door requirement names (dungDoorLevels
// entry); doors say 'Constitution' where SKILL_NAMES says 'Hitpoints'.
function dungDoorMine(dl) {
  if (!dl) return null;
  const dn = dl.skill === 'Constitution' ? 'Hitpoints' : dl.skill;
  const si = SKILL_NAMES.findIndex(n => n.toLowerCase() === dn.toLowerCase());
  return si >= 0 ? dungPartyBest(si).best : null;
}
// A captured door requirement judged against the PARTY's best level in that skill.
// ONE definition, because this is read by both the planner and the render:
//   'critical' = at, or within DUNG_DOOR_CRIT_MARGIN below, the party's ceiling -- a
//                real gate the floor expects this party to open;
//   'low'      = far below the party -> a trivial side path;
//   'above'    = ABOVE the party's best -> also likely non-essential :
//                the generator does not gate the critical path behind a door the party
//                cannot open, the same reasoning that makes an uncompletable resource
//                a bonus room. (It can still be forced at a failure chance, so it is
//                faded rather than hidden.)
function dungDoorBand(dl) {
  if (!dl) return null;
  // A requirement of 106+ is critical UNCONDITIONALLY, party levels known or not:
  // bonus-room skill doors only generate in the 1..105 band, so a 106+ reading can
  // only sit on the critical path.
  if (dl.level >= 106) return 'critical';
  const best = dungDoorMine(dl);
  if (best == null) return null;
  if (dl.level > best) return 'above';
  if (dl.level < best - DUNG_DOOR_CRIT_MARGIN) return 'low';
  return 'critical';
}
// Emotes puzzle: NPC 10966 performs an emote; pick the matching dialogue option
// (Wave / Nod head / Shake head / Laugh / Cry). anim id -> option text, filled from
// live observation -- until mapped, the box label shows the latched anim id so each
// emote's id can be recorded. Progress = varc 1233: 67/134/201 = 1/2/3 done.
// 861 laugh, 856 shake head, 863 wave. 855 nod / 860 cry complete
// the classic emote anim table the verified three sit in.
// 855/856/860/861/863 are ; the rest are the same classic emote anim run
// (857 think, 858 bow, 859 angry, 862 cheer, 864 clap, 865 dance -- 858 showed live as
// a bare "anim 858" label, ). A wrong option word only fails to
// highlight the dialogue row; the "do: <emote>" label still beats a raw anim number.
const DUNG_EMOTE_ANIM = { 855: 'nod', 856: 'shake head', 857: 'think', 858: 'bow',
                          859: 'angry', 860: 'cry', 861: 'laugh', 862: 'cheer',
                          863: 'wave', 864: 'clap', 865: 'dance' };
let dungEmoteLast = -1;   // last non-idle anim the paired statue played (latched until the next)
let dungEmoteWatch = '';  // "x,y" of the tracked statue -- a pad change drops the stale latch
let dungLodeKey = '', dungLodeTick = -1;   // rotating crystal: active tile + game tick it appeared
let dungLodeWarnAt = -1, dungLodeCenterOn = false;   // predicted next-appearance tick already warned for; centre text active
let dungLodeCenterAt = 0, dungLodeSeenAt = 0;   // when the prompt went up / when 49510 was last seen (watchdogs)
let dungLodeMarkAt = 0;      // Date.now() of the last cycle mark (appearance/arrival) -- the ms countdown anchor
let dungLodeWinMs = 0;       // measured click-window length (how long the mark persisted), ms
let dungLodeSuppress = false;   // sync phase (all four crystals up) -> no click prompt
let dungLodeInRoom = false;     // the large crystal is in the PLAYER'S room -> countdown may show
let dungLodeCenter = null;   // {x,y} of the large crystal's tile (the arrival point)
let dungLodeDbg = '';        // crystal-room pad-resolution readout for the map dbg line
let dungMonoDone = {};       // "x,y" -> true: monoliths already charged to 100% (stop guiding those)
let dungSelfPos = null;      // {x,y} live player tile (fetch + tick), for same-room gating
let dungCurDoors = 0;        // door bitmask (N1 E2 S4 W8) of the player's current room, from the map fetch
let dungRouteTarget = '';    // map cell 'gx,gy' clicked -- route from the player's room to it
let dungBossWarnOn = false;  // boss 9919 icicle-attack warning currently on screen
let dungPartyRoster = {};    // name -> 1, every player seen in this instance INCLUDING self;
                             // p-indices come from the SORTED name list, so every party
                             // member's client shows identical p-numbers (multibox-friendly)
let dungSelfName = '';       // this client's own display name (tagged "you" in the list)
let dungIcePlan = null;      // {sig, anchor:'lx,ly', stops:[{k:'lx,ly', press}...], idx} held slide tour (stable while executing);
                            // sig = room + sorted UNPRESSED-pad set, so a pressed pad (id change) re-solves
let dungIceSettle = null;    // {key:'lx,ly', n} consecutive reconciles on the same tile (settle detector)
let dungIceDump = null;      // ice room model + plan, copyable JSON (map tab) -- live verification beats re-reasoning
let dungMazeDump = null;     // poison-maze model + route, same paste-back loop (bad routes get fixed against data)
let dungBarrelDump = null;   // barrel room: live pad-relative geometry + the rotation fit's per-turn scores
let dungColFerret = null;    // latched colour in the coloured-ferret room: one target at a time
// Plate marks MUST carry an rgb. The reader hides plain (rgb 0) destination tiles
// whenever a labelled NPC box stands at the objective -- the "clue NPC found" rule -- and
// this room always boxes the ferret, so a colourless plate mark was silently dropped
// ("only showing ferret, not the red plate"). A COLOURED mark counts as
// an annotation and survives. Matching the plate box to the ferret's own colour also
// makes the pairing readable at a glance.
const DUNG_FERRET_RGB = { Red: 0xff4444, Blue: 0x4488ff, Green: 0x33cc66,
                          Yellow: 0xffdd33, Orange: 0xff8822 };
let dungFerretPlan = null;   // {anchor:'x,y', stops:['x,y'...], idx} held fish route (stable while the ferret walks)
let dungFerretSettle = null; // {key:'x,y', n} ferret settle detector
// Shared overlay channels are published ONLY WHEN THE VALUE CHANGES. Re-publishing the
// same boxes every tick, or clearing and redrawing them within one tick, let the
// overlay's frame pump catch the gap and the labels flashed.
let dungHerbHlLast = '';     // last uiHighlight rect this panel published ('' = none)
// WHICH feature published it. uiHighlight is ONE shared rect, so a caller passing its own
// tag only clears a box IT drew; no tag = force-clear. Without the tag, one feature's
// per-tick clear wipes another's box and the clear+redraw reads as a flash.
let dungHerbHlOwner = '';
function dungHerbClear(owner) {
  if (!dungHerbHlLast) return;   // never wipe a box another feature put there
  if (owner && dungHerbHlOwner && dungHerbHlOwner !== owner) return;   // not ours
  dungHerbHlLast = ''; dungHerbHlOwner = '';
  try { rtxData.sync('overlay.uiHighlight', 0, 0, 0, 0); } catch (e) {}
}
// Hoardstalker riddle, latched: the answer must survive the riddle dialogue CLOSING
// while you walk to the container, so it cannot be read fresh each tick.
let dungHoardRiddle = null;  // {k, item, opt|gid, loc} or null
let dungGroundCache = [];    // last groundItems() result: hoardstalker piles live there
// Strange-plant room: a colour change REPLACES the loc, and the capture keeps the old
// state beside the new one at the same tile, so liveness is decided by PER-TILE recency
// (the id that appeared at a tile most recently is the live state -- see the room block).
let dungPlantSeen = {};      // 'x,y|loc id' -> tick first seen in this room
// Seeker room: per-npc patrol/turn memory. Spawns: learned turn TILES (every bend in
// the walk direction -- legs are 4 ticks and can corner mid-room), measured speed, and
// the walk heading, for position-based countdowns. Sentinel: turn-phase anchor (moves
// only EARLIER; observations can only lag) + the learned rotation step.
let dungSeekerMem = {};      // npc uid -> {face, at, x, y, hd, mt, spd, turns, step, pend}
// Crystal room: a crystal is INVISIBLE while it sits under its pressure plate, so the
// scene drops it for a chunk of every cycle. Remember each colour's last position so
// the readout stays continuous instead of blanking.
let dungCrysMem = {};        // colour name -> {d, dx, dy, c, t}
// Poltergeist: the herb is named in the sarcophagus INSCRIPTION, and that dialogue
// closes while you walk to the patch, so latch it.
let dungPoltHerb = null;
// Dialogue TEXT sits at a DIFFERENT comp per group: 1186's inscription is comp 3
// ('s live tree, 1184's npc line is comp 10. Guessing one comp per
// group silently reads nothing, so scan a small range and join what comes back.
const DUNG_DLG_GROUPS = [1186, 1184, 1191];   // server-message / npc / player
const DUNG_DLG_COMPS = Array.from({ length: 24 }, (_, i) => i).join(',');
function dungDlgText() {
  for (const g of DUNG_DLG_GROUPS) {
    try {
      const d = JSON.parse(bridge().interfaceComps(myPid(), g, DUNG_DLG_COMPS) || '{}');
      if (!d || !d.open || !Array.isArray(d.comps)) continue;
      const t = d.comps.map(c => c.text || '').join(' ').trim();
      if (t) return t.toLowerCase();
    } catch (e) {}
  }
  return '';
}
let dungInvHlLast = '';      // last panelViz payload this panel published ('' = none)
// `owner` names the feature doing the clearing. panelViz is a SHARED single channel, and a
// clear-then-redraw inside one tick is what the frame pump catches as a FLICKER, so a
// tagged clear only wipes what that same feature published.
let dungInvHlOwner = '';
function dungInvClear(owner) {
  if (owner && dungInvHlOwner && dungInvHlOwner !== owner) return;   // not ours to wipe
  if (!dungInvHlLast) return;  // same shared-channel rule as dungHerbClear
  dungInvHlLast = ''; dungInvHlOwner = '';
  try { rtxData.sync('overlay.panelViz', ''); } catch (e) {}
}
// How many of `itemId` are in the backpack.
function dungInvCount(itemId) {
  try {
    const inv = JSON.parse(rtxData.sync('state.inventory') || '{}');
    let n = 0;
    for (const it of (inv.items || [])) if (it[1] === itemId) n++;
    return n;
  } catch (e) { return 0; }
}
// Box EVERY backpack slot holding `itemId` (panelViz takes '|'-separated boxes, unlike
// overlay.highlightItem which stops at the first match) and label each with the action.
function dungHighlightInvItem(itemId, label, owner) {
  try {
    if (!itemId) { dungInvClear(owner); return 0; }
    const inv = JSON.parse(rtxData.sync('state.inventory') || '{}');
    const segs = [];
    // Box every matching slot but LABEL ONLY THE FIRST: four stacked labels on adjacent
    // backpack slots just overlapped into an unreadable smear. The
    // boxes alone show which items are meant; one label says what to do with them.
    let held = 0;
    for (const it of (inv.items || [])) {
      if (it[1] !== itemId) continue;
      held++;
      const r = JSON.parse(rtxData.sync('state.invSlotRect', it[0]) || '{}');
      if (r && r.w > 0) segs.push(r.x + ',' + r.y + ',' + r.w + ',' + r.h + ',' + (segs.length ? '' : label));
    }
    if (segs.length) {
      const payload = segs.join('|');
      dungInvHlOwner = owner || '';
      // PUBLISH EVERY TICK, do not dedupe: re-sending an identical payload is visually a no-op,
      // whereas not sending lets the highlight lapse after a single tick.
      rtxData.sync('overlay.panelViz', payload);
      dungInvHlLast = payload;
      return segs.length;
    }
    // HELD BUT UNMEASURABLE is not the same as GONE. invSlotRect can transiently return
    // w=0 while the backpack is mid-redraw, and clearing on that made the box flicker --
    // and a screenshot landing on a failed frame showed no highlight at all, which read
    // as "the highlight is broken". Keep the last good boxes and try
    // again next tick; only clear when the item has actually left the backpack.
    if (held) return 0;
    dungInvClear(owner);
  } catch (e) {}
  return 0;
}
let dungWasIn = false;       // in-dungeon latch: clear the shared overlay channels ONCE on exit
function dungClearOverlays() {
  if (!dungWasIn) return;    // never spam empty pushes outside -- they'd stomp quests/alerts
  dungWasIn = false;
  dungHighlightList([]); dungGuideTiles([]); dungHerbClear(); dungInvClear();
  dungLodeKey = ''; dungLodeTick = -1; dungLodeWarnAt = -1; dungLodeCenter = null;
  dungIcePlan = null; dungIceSettle = null; dungIceDump = null; dungMazeDump = null; dungFerretPlan = null; dungFerretSettle = null; dungColFerret = null;
  dungLodeMarkAt = 0; dungLodeWinMs = 0; dungLodeSuppress = false; dungLodeInRoom = false;
  if (dungLodeCenterOn) { dungLodeCenterOn = false; try { bridge().centerText(myPid(), ''); } catch (e) {} }
}

// In-scene NPC highlight (box + floating label above the head) via the SAME mechanism
// every quest guide uses -- overlay.highlightNpc with "#<id>" to match by NPC id + a
// label. The reader follows the id each frame, so it tracks a moving NPC automatically;
// the ids here are unique so no tile hint is needed. Empty -> clear the highlight.
// dungHighlightList: several boxes at once via bridge().overlayHighlight directly (the
// plugin-facing overlay.highlight sanitiser strips the '#' and '|' the needles need).
// One box per needle -- "#<id>|label" or "name|label"; ',' is the csv separator.
// Pushed EVERY reconcile, never latched: the channel is shared (panel_alerts syncs it,
// clearing it once on load), so like the quest guides these are re-asserted each tick --
// a latch left the box wiped until the needle set happened to change.
function dungHighlightList(needles) {
  try { bridge().overlayHighlight(myPid(), (needles || []).map(s => String(s).replace(/,/g, ' ')).join(',')); } catch (e) {}
}
function dungHighlightNpc(id, label) {
  dungHighlightList(id ? ['#' + id + (label ? '|' + label : '')] : []);
}

// Suspicious grooves puzzle: loc 67103 is the groove to search. Locs aren't NPCs, so it
// goes through the quest GUIDE-TILE channel (overlay.guideTiles boxes the named loc at
// the tile, same as qgObject). Same no-flicker latch as the NPC channel.
// Same channel as the quest guideTiles command, but via bridge().guideMarks directly
// so marks can carry the optional rgb tail: the reader HIDES plain (rgb 0) destination
// tiles whenever a labelled NPC box stands at the objective ("clue NPC found" rule),
// and the statue's boxed pushable is right next to its target tile -- a COLOURED mark
// is treated as an annotation and stays visible. Fields: x\x1f y\x1f plane\x1f label
// [\x1f snap \x1f rgb].
function dungGuideTiles(marks) {
  try {
    const recs = (marks || []).map(m =>
      (m.x | 0) + '\x1f' + (m.y | 0) + '\x1f' + ((m.plane || 0) | 0) + '\x1f'
      + String(m.label == null ? '' : m.label).slice(0, 95).replace(/[\x1e\x1f]/g, ' ')
      + ((m.rgb || m.snap) ? '\x1f' + (m.snap ? 1 : 0) + '\x1f' + ((m.rgb | 0) || 0) : ''));
    rtxData.sync('overlay.guideMarks', recs.join('\x1e'));
  } catch (e) {}
}

// From a scene read: refresh the start-room anchor and set the in-scene highlights --
// the ghost to KILL (10989), else the sliding-block move (NPC channel), plus the
// suspicious-grooves loc 67103 (guide-tile channel, independent of the NPC one).
// Puzzle: cluster the blocks into a 3x3, find the empty cell, and prefer the tile whose
// TARGET is the empty cell (clicking it slides it HOME); else a movable+misplaced tile.
function dungReconcileScene(npcs, objs) {
  npcs = npcs || [];
  if (!dungWasIn) {   // first reconcile since load/exit: sweep a stale centre text left
    try { bridge().centerText(myPid(), ''); } catch (e) {}   // by a previous UI session
  }
  dungWasIn = true;   // reconcile only runs in a dungeon; arms the one-shot exit clear
  // Same-room gate: scene range is 100 tiles, so puzzle NPCs from NEIGHBOURING rooms
  // are in the list too -- without this, a higher-priority puzzle one room over
  // hijacks the chain (case: a Monolith next door starved the pondskater box).
  // Anchored on the smuggler's room grid; a fixed mod-16 lattice phase looked right
  // on three floors but MISPLACED the player on a fourth, so it is NOT trusted.
  // FAILS CLOSED: no anchor -> nothing guided (a cross-room box is worse).
  const here = e => {
    if (!dungFloorSW || !dungSelfPos || typeof e.x !== 'number') return false;
    const a = dungRoomOf(dungFloorSW, e.x, e.y), b = dungRoomOf(dungFloorSW, dungSelfPos.x, dungSelfPos.y);
    return a.rx === b.rx && a.ry === b.ry;
  };
  // Clear the shared overlay channels only when not in the poltergeist room: clearing
  // unconditionally and letting the branch redraw below makes the boxes blink every tick.
  // MUST sit AFTER `here` is declared -- it calls it, and `const` gives no hoisting.
  {
    const poltHere = npcs.some(n => n.id === 11245 && typeof n.x === 'number' && here(n))
      || (objs || []).some(o => typeof o.x === 'number' && here(o) &&
           ((o.id >= 54074 && o.id <= 54081) || (o.id >= 54094 && o.id <= 54101)));
    if (!poltHere) { dungHerbClear('herb'); dungInvClear('polt'); dungPoltHerb = null; }   // drop a stale herb
  }
  const marks = [];   // guide-tile channel: grooves/switches locs + statue target tile
  // Suspicious grooves: stepping a WRONG tile leaves a 67097 marker loc on that spot
  // ( -- same pattern as the pulled-switch 49384).
  {
    // NO PREDICTION. The "highest loc id in the row is safe" rule is FALSIFIED
    //: on this floor the bottom row's safe tile was 67120 while
    // 67121 -- the higher id, and an entry in a confirmed-safe table from an
    // EARLIER floor -- sat right beside it. So the safe tile varies per floor and no
    // id-derived rule (or static id table) can be trusted; guessing here costs real
    // damage. Only OBSERVED facts are drawn now, and only the few that matter:
    //   67099  = a confirmed-safe marker -> green, and nothing else
    // Untried grooves are deliberately NOT marked one by one: a box on every tile was
    // unreadable ("too busy and hard to tell what's going on"), and an "untried"
    // box carries no information the tile art doesn't already give. Wrong steps (67097)
    // replace the groove loc in-world anyway, so they need no box either.
    const marked = {};
    // 67099 "Grooves" = CONFIRMED safe tiles: plain green marks
    for (const o of (objs || []))
      if (o.id === 67099 && typeof o.x === 'number' && here(o) && !marked[o.x + ',' + o.y] && marks.length < 16) {
        marked[o.x + ',' + o.y] = true;
        marks.push({ x: o.x, y: o.y, plane: o.plane || 0, label: 'Safe', rgb: 0x5fd07a });
      }
  }
  // Poison maze: concentric SQUARE rings; the WALLS are on the EDGE between adjacent
  // rings (not on whole tiles). So every tile is walkable, but CROSSING a ring
  // boundary requires a Barrier on the OUTER (higher) ring at that spot. Room-local
  // 14x14, centre 6.5,6.5: band 0 = the 4x4 centre (Chebyshev radius <= 1.5), bands
  // 1..5 = the rings at radius 2.5/3.5/4.5/5.5/6.5. Pedestals + Switches (49351) block
  // tiles. BFS player -> Locked chest (49345); the barriers crossed get numbered AABB
  // marks. Validated on the live maze: the 5-barrier route, one barrier per ring.
  // Poison-maze tile blockers. Pedestals are SEVEN cache families, fully enumerated
  // (js5-16 audit, was two): Mine-actioned 49360-49374 + 54412-54416,
  // actionless 50957-50967, 51503-51513, 52051-52061, 54976-54988, 55877-55887 (the
  // actionless ranges have unused ids inside them -- harmless). Both kinds turn up in
  // one room (capture: 49371-73 next to 51511). 54110-54117 are the pedestal
  // ROOM's 2x2 pedestals, a different puzzle -- deliberately NOT listed. The name and
  // Mine-action fallbacks stay so an unknown future theme still blocks.
  const DUNG_MAZE_PEDESTAL = {};
  for (const pr of [[49360, 49374], [50957, 50967], [51503, 51513], [52051, 52061],
                    [54412, 54416], [54976, 54988], [55877, 55887]])
    for (let pi = pr[0]; pi <= pr[1]; pi++) DUNG_MAZE_PEDESTAL[pi] = 1;
  const dungMazeBlocks = o =>
    DUNG_MAZE_PEDESTAL[o.id]
    || o.name === 'Pedestal' || o.name === 'Switch'
    || (o.actions || []).some(a => a === 'Mine');
  // Chest families run in THREE THEME VARIANTS with matching indices (js5-16 scan):
  // locked 49345/49346/49347 -> open 49348/49349/49350. Matching by id alone covers only
  // 49345; the other themes rely on the name fallback, which a rename would break.
  // Daemonheim "Locked chest" (Open) runs to THIRTEEN ids, not three -- 49886-49895 are
  // the same signature and were only ever matched by the o.name fallback (js5-16 audit
  //. "Open chest" (Search) has 49348-49350 plus 49896/49897/49908/49909
  // (no action) as further solved states.
  const DUNG_CHEST_LOCKED = { 49345:1, 49346:1, 49347:1, 49886:1, 49887:1, 49888:1,
                              49889:1, 49890:1, 49891:1, 49892:1, 49893:1, 49894:1, 49895:1 };
  const DUNG_CHEST_OPEN   = { 49348:1, 49349:1, 49350:1,
                              49896:1, 49897:1, 49908:1, 49909:1 };
  const mazeChest = (objs || []).find(o => (DUNG_CHEST_LOCKED[o.id] || o.name === 'Locked chest')
                                           && typeof o.x === 'number' && here(o));
  // SOLVED: the opened chest appears as "Open chest" (Search) AT THE SAME TILE, with the
  // locked id still listed beside it -- the usual per-tile coexistence. Its presence
  // ends the puzzle, so stop guiding rather than keep routing the
  // player to a chest they have already opened.
  const mazeDone = (objs || []).some(o => (DUNG_CHEST_OPEN[o.id] || o.name === 'Open chest')
                                          && typeof o.x === 'number' && here(o));
  const mbars = (objs || []).filter(o => o.name === 'Barrier' && typeof o.x === 'number' && here(o));
  const isMazeRoom = mazeChest && !mazeDone && mbars.length >= 5;   // Barriers present -> it's the MAZE (a Locked chest also sits in the EMOTE room; varc 1233 is progress there, not a timer)
  if (isMazeRoom && dungFloorSW && dungSelfPos) {
    // NEVER gate the solution on the timer var. varc 1233 reads 0 while the maze is plainly
    // running (in-game Time Remaining bar still ticking) -- it is shared with the
    // monolith/emote progress, so it simply isn't a reliable "started" flag. The route is
    // valid before the pull as well as during, so it is always drawn; the start Switch is
    // just an extra mark while it is still there, and the countdown shows only when the var
    // carries a real value. Start switch = FOUR theme variants (js5-16 scan):
    // 49351/49352/49353 (block form, alongside chests 49345-50 / fountains 49354-56) + 54409
    // (the 54xxx theme, beside chest 54407). Name fallback (already inside
    // isMazeRoom, whose only Switch is the start switch) future-proofs an un-enumerated
    // theme, matching the chest/pedestal style.
    const DUNG_MAZE_SWITCH = { 49351: 1, 49352: 1, 49353: 1, 54409: 1 };
    const sw = (objs || []).find(o => (DUNG_MAZE_SWITCH[o.id] || o.name === 'Switch') && typeof o.x === 'number' && here(o));
    if (sw) marks.push({ x: sw.x, y: sw.y, plane: 0, label: 'Pull to start the maze', rgb: 0xf0c419, snap: 1 });
    if (typeof dungMazeTimer === 'number' && dungMazeTimer > 0)
      marks.push({ x: mazeChest.x, y: mazeChest.y, plane: 0, label: 'Time left: ' + dungMazeTimer, rgb: 0x5fd07a, snap: 1 });
  }
  {
    const chest = mazeChest;
    if (isMazeRoom && dungFloorSW && dungSelfPos) {
      const pr = dungRoomOf(dungFloorSW, dungSelfPos.x, dungSelfPos.y);
      const swx = dungFloorSW.x + DUNG_ROOM_PITCH * pr.rx, swy = dungFloorSW.y + DUNG_ROOM_PITCH * pr.ry;
      const barSet = {}, blk = {};
      for (const b of mbars) barSet[(b.x - swx) + ',' + (b.y - swy)] = 1;
      // In-room test by COORDINATE, not here(): here() re-derives the room through
      // dungRoomOf and disagrees at the edges, which let pedestals sitting on a ring
      // wall fall out of the blocked set -- and the route then walked through one
      //. Anything inside the 14x14 local frame counts, full stop.
      //
      // A pedestal with a MINE action is not a wall, it is a TOLL: mining it opens the
      // tile, and doing so is often much quicker than walking the long way round
      //. Measured: ~4s to mine, ~3.3 tiles/s running, so breaking
      // through is worth roughly DUNG_MAZE_MINE_TILES tiles of detour. Pedestals with
      // no Mine action (the 51503-51513 family) stay hard walls.
      const mineable = {};
      for (const o of (objs || [])) {
        if (typeof o.x !== 'number' || !dungMazeBlocks(o)) continue;
        const lx = o.x - swx, ly = o.y - swy;
        if (lx < 0 || lx > 13 || ly < 0 || ly > 13) continue;
        if ((o.actions || []).some(a => a === 'Mine')) mineable[lx + ',' + ly] = 1;
        else blk[lx + ',' + ly] = 1;
      }
      // GROUND-TRUTH walls (js5-5 room bank, region 2_66..2_75; every template cell and
      // both themes share ONE interior). Template = 16x16 with a 1-tile shared border, so
      // live local = rot(template) - (1,1); a live dump validated the mapping 100% (chest,
      // switch, entry door and all 16 ring barriers matched at rotation 1). The previous
      // concentric-band abstraction routed through geometry the real room does not have.
      // Wall entries are [x, y, type, rot]: type 0 = one edge, 2 = corner (rot and rot+1);
      // edge dirs 0=W 1=N 2=E 3=S; rotation r maps (x,y)->(y,15-x) and d->(d+1)&3.
      const TPL_WALLS = [[0,1,0,2],[0,2,0,2],[0,3,0,2],[0,4,0,2],[0,5,0,2],[0,6,0,2],[0,7,0,2],[0,8,0,2],[0,9,0,2],[0,10,0,2],[0,11,0,2],[0,12,0,2],[0,13,0,2],[0,14,0,2],[1,0,0,1],[1,2,0,2],[1,3,0,2],[1,5,0,2],[1,6,0,2],[1,7,0,2],[1,8,0,2],[1,9,0,2],[1,10,0,2],[1,11,0,2],[1,13,0,2],[1,15,0,3],[2,0,0,1],[2,1,0,1],[2,3,0,2],[2,4,0,2],[2,5,0,2],[2,6,0,2],[2,7,0,2],[2,9,0,2],[2,10,0,2],[2,11,0,2],[2,12,0,2],[2,14,0,3],[2,15,0,3],[3,0,0,1],[3,1,0,1],[3,2,0,1],[3,4,0,2],[3,5,0,2],[3,6,0,2],[3,7,0,2],[3,8,0,2],[3,9,0,2],[3,10,0,2],[3,11,0,2],[3,13,0,3],[3,14,0,3],[3,15,0,3],[4,0,0,1],[4,2,0,1],[4,3,0,1],[4,5,0,2],[4,7,0,2],[4,8,0,2],[4,9,0,2],[4,10,0,2],[4,12,0,3],[4,13,0,3],[4,14,0,3],[4,15,0,3],[5,0,0,1],[5,1,0,1],[5,2,0,1],[5,4,0,1],[5,6,0,2],[5,7,0,2],[5,8,0,2],[5,9,0,2],[5,11,0,3],[5,12,0,3],[5,13,0,3],[5,14,0,3],[5,15,0,3],[6,0,0,1],[6,1,0,1],[6,2,0,1],[6,3,0,1],[6,4,0,1],[6,5,0,1],[6,10,0,3],[6,11,0,3],[6,13,0,3],[6,14,0,3],[6,15,0,3],[7,0,0,1],[7,1,0,1],[7,2,0,1],[7,3,0,1],[7,4,0,1],[7,5,0,1],[7,11,0,3],[7,12,0,3],[7,13,0,3],[7,14,0,3],[7,15,0,3],[8,0,0,1],[8,2,0,1],[8,3,0,1],[8,4,0,1],[8,10,0,3],[8,11,0,3],[8,12,0,3],[8,13,0,3],[8,15,0,3],[9,0,0,1],[9,1,0,1],[9,2,0,1],[9,3,0,1],[9,4,0,1],[9,5,0,1],[9,10,0,3],[9,11,0,3],[9,12,0,3],[9,13,0,3],[9,14,0,3],[9,15,0,3],[10,0,0,1],[10,1,0,1],[10,3,0,1],[10,4,0,1],[10,6,0,0],[10,7,0,0],[10,8,0,0],[10,9,0,0],[10,11,0,3],[10,13,0,3],[10,14,0,3],[10,15,0,3],[11,0,0,1],[11,1,2,0],[11,2,0,1],[11,3,0,1],[11,5,0,0],[11,6,0,0],[11,7,0,0],[11,9,0,0],[11,10,0,0],[11,12,0,3],[11,13,0,3],[11,14,0,3],[11,15,0,3],[12,0,0,1],[12,1,0,1],[12,2,0,1],[12,4,0,0],[12,5,0,0],[12,6,0,0],[12,7,0,0],[12,8,0,0],[12,9,0,0],[12,10,0,0],[12,11,0,0],[12,13,0,3],[12,14,0,3],[12,15,0,3],[13,0,0,1],[13,1,0,1],[13,3,0,0],[13,4,0,0],[13,5,0,0],[13,6,0,0],[13,8,0,0],[13,9,0,0],[13,10,0,0],[13,11,0,0],[13,12,0,0],[13,14,0,3],[13,15,0,3],[14,0,0,1],[14,2,0,0],[14,4,0,0],[14,5,0,0],[14,6,0,0],[14,7,0,0],[14,8,0,0],[14,9,0,0],[14,11,0,0],[14,12,0,0],[14,13,0,0],[14,15,0,3],[15,1,0,0],[15,2,0,0],[15,3,0,0],[15,4,0,0],[15,5,0,0],[15,6,0,0],[15,7,0,0],[15,8,0,0],[15,9,0,0],[15,10,0,0],[15,11,0,0],[15,12,0,0],[15,13,0,0],[15,14,0,0]];
      const TPL_BLOCK = [[6,6],[6,9],[7,6],[9,6],[9,9]];   // centre pillars + rock (scenery on the outer walkway is outside the playfield)
      const TPL_BARS = [[1,4,2],[1,12,2],[2,8,2],[4,1,1],[4,6,2],[5,3,1],[6,12,3],[7,10,3],[8,5,1],[8,14,3],[10,2,1],[10,12,3],[11,8,0],[13,7,0],[14,3,0],[14,10,0]];
      const TPL_DOOR = [8, 1, 1], TPL_CHEST = [8, 8];
      const rotP = (x, y, r) => { for (let i2 = 0; i2 < r; i2++) { const t2 = x; x = y; y = 15 - t2; } return [x, y]; };
      const toLive = (x, y, r) => { const p4 = rotP(x, y, r); return [p4[0] - 1, p4[1] - 1]; };
      const gx = chest.x - swx, gy = chest.y - swy, sx = dungSelfPos.x - swx, sy = dungSelfPos.y - swy;
      // Rotation = the one that puts the template chest on the live chest and the ring
      // barriers on live barrier tiles (>= 12 of 16: captures can miss a couple).
      let mrot = -1;
      for (let r = 0; r < 4; r++) {
        const c2 = toLive(TPL_CHEST[0], TPL_CHEST[1], r);
        if (c2[0] !== gx || c2[1] !== gy) continue;
        let hits = 0;
        for (const b2 of TPL_BARS) { const p4 = toLive(b2[0], b2[1], r); if (barSet[p4[0] + ',' + p4[1]]) hits++; }
        if (hits >= 12) { mrot = r; break; }
      }
      const wallE = {}, passE = {};   // "x,y,d" -> 1 / owner tile key ("x,y")
      if (mrot >= 0) {
        for (const w of TPL_WALLS) {
          const p4 = toLive(w[0], w[1], mrot), d0 = (w[3] + mrot) & 3;
          wallE[p4[0] + ',' + p4[1] + ',' + d0] = 1;
          if (w[2] === 2) wallE[p4[0] + ',' + p4[1] + ',' + ((d0 + 1) & 3)] = 1;
        }
        for (const b2 of TPL_BLOCK) { const p4 = toLive(b2[0], b2[1], mrot); blk[p4[0] + ',' + p4[1]] = 1; }
        for (const b2 of TPL_BARS.concat([TPL_DOOR])) {
          const p4 = toLive(b2[0], b2[1], mrot), d0 = (b2[2] + mrot) & 3;
          passE[p4[0] + ',' + p4[1] + ',' + d0] = p4[0] + ',' + p4[1];
        }
      }
      // A step is blocked when a wall edge sits on EITHER side of the boundary (edge d
      // from the source tile, or the opposite edge d^... from the destination). Barrier
      // and entry-door edges are the passable exceptions - they are the route's marks.
      const OPP = { 0: 2, 1: 3, 2: 0, 3: 1 };   // W<->E, N<->S
      const DIRD = { '1,0': 2, '-1,0': 0, '0,1': 1, '0,-1': 3 };   // step vector -> edge dir from source (world axes: +y = N)
      const edgeAt = (x, y, d, nx, ny) => {
        const k1 = x + ',' + y + ',' + d, k2 = nx + ',' + ny + ',' + OPP[d];
        return { wall: wallE[k1] || wallE[k2], pass: passE[k1] || passE[k2] };
      };
      const open = (x, y) => x >= 0 && x <= 13 && y >= 0 && y <= 13 && !blk[x + ',' + y];
      // Costed search (not plain BFS) so "mine through" and "run around" compete on
      // TIME: one tile of running = 1, mining a pedestal = DUNG_MAZE_MINE_TILES more.
      // 14x14 = 196 nodes, so scanning for the cheapest open node is plenty fast.
      const mprev = {}, dist = {}, done = {};
      const startK = sx + ',' + sy, goalK = gx + ',' + gy;
      dist[startK] = 0; mprev[startK] = null;
      let mg = null;
      if (mrot >= 0) while (true) {
        let bk = null, bd = Infinity;
        for (const k in dist) if (!done[k] && dist[k] < bd) { bd = dist[k]; bk = k; }
        if (bk === null) break;
        if (bk === goalK) { mg = bk; break; }
        done[bk] = 1;
        const p0 = bk.split(',').map(Number), x = p0[0], y = p0[1];
        for (const dd of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
          const nx = x + dd[0], ny = y + dd[1], k = nx + ',' + ny;
          if (!open(nx, ny) || done[k]) continue;
          const e = edgeAt(x, y, DIRD[dd[0] + ',' + dd[1]], nx, ny);
          if (e.wall && !e.pass) continue;
          const nd = bd + 1 + (mineable[k] ? DUNG_MAZE_MINE_TILES : 0);
          if (dist[k] === undefined || nd < dist[k]) { dist[k] = nd; mprev[k] = bk; }
        }
      }
      // Room dump for the paste-back loop: everything the router SAW plus the route it
      // chose, so a nonsensical path gets diagnosed against data instead of screenshots.
      dungMazeDump = {
        origin: [swx, swy], self: [sx, sy], chest: [gx, gy], rot: mrot, timer: dungMazeTimer,
        barriers: Object.keys(barSet), mineable: Object.keys(mineable), blocked: Object.keys(blk),
        objsIn: (objs || []).filter(o => typeof o.x === 'number'
            && o.x - swx >= 0 && o.x - swx <= 13 && o.y - swy >= 0 && o.y - swy <= 13)
          .map(o => [o.id, o.x - swx, o.y - swy, (o.actions || []).filter(Boolean).join('/'), o.name || '']),
        route: mg ? (() => { const c = []; for (let k = mg; k; k = mprev[k]) c.unshift(k); return c; })() : null,
        cost: mg ? dist[mg] : null
      };
      if (mg) {
        const chain = [];
        for (let k = mg; k; k = mprev[k]) chain.unshift(k);
        // Mark only what the walk actually USES: the outer barrier tile of each band
        // crossing, plus mine tolls on the path. Marking every path tile that happened
        // to hold a barrier numbered the START tile when the route never crossed there
        // (user dump 2026-07-31: mark "1" at the player's feet, route walking away).
        // Marks follow the ACTUAL crossings: each barrier/door edge the walk passes gets a
        // number at the loc's own tile ('enter' for the outer door), mine tolls get MINE.
        let step = 1;
        const marked = {};
        const doorTile = (() => { const b = mbars.find(b2 => b2.id === 49344); return b ? (b.x - swx) + ',' + (b.y - swy) : ''; })();
        for (let ci = 0; ci < chain.length; ci++) {
          const k = chain[ci], p3 = k.split(',').map(Number);
          if (mineable[k] && !marked[k] && marks.length < 16) {
            marked[k] = 1;
            marks.push({ x: swx + p3[0], y: swy + p3[1], plane: 0,
                         label: step + ' - MINE through', rgb: 0xe8b34b, snap: 1 });
            step++;
          }
          if (ci > 0) {
            const pp = chain[ci - 1].split(',').map(Number);
            const e = edgeAt(pp[0], pp[1], DIRD[(p3[0] - pp[0]) + ',' + (p3[1] - pp[1])], p3[0], p3[1]);
            if (e.pass && !marked[e.pass] && marks.length < 16) {
              marked[e.pass] = 1;
              const po = e.pass.split(',').map(Number);
              marks.push({ x: swx + po[0], y: swy + po[1], plane: 0,
                           label: e.pass === doorTile ? step + ' - enter' : String(step), rgb: 0x5ab8f0, snap: 1 });
              step++;
            }
          }
        }
      }
    }
  }
  // BARREL / BREWING ROOM. Repair a barrel, fill it at the spout, push it onto the pad.
  //
  //   1. take the Broken barrel bits (item 17422 -- the only item of that name)
  //   2. Fix the broken barrel with them
  //   3. push it under the EXPELLING pipe -- the flowing one; Dry pipes are inert
  //   4. push the filled barrel onto the Pressure pad
  //
  // STATE IS READ FROM LIVE ACTIONS, with the id sets only as a fallback. An actionless
  // Barrel (11074) is the finished article: filled and standing on its pad with nothing
  // left to do, the same convention as the actionless Coffin meaning "blessed".
  //
  // WHAT THIS DOES NOT DO: route the barrel pushes. The pushable barrels spawn in a
  // fixed layout with a fixed sequence to reach the bits, but that sequence has not been
  // supplied yet -- and a wrong push in this room is unrecoverable, so it guides the
  // TARGETS and leaves the pushing to the player rather than inventing a path.
  // Likewise there is no fill-level readout yet, so stages 3 and 4 are offered together
  // once the barrel is repaired.
  {
    const inR = e => typeof e.x === 'number' && here(e);
    const B_PUSH = { 11072: 1, 11073: 1, 11686: 1 };
    const B_FIX  = { 11075: 1 };
    const B_DONE = { 11074: 1 };
    // Dry pipes are listed only to be EXCLUDED -- pointing at one would fill nothing.
    const P_SPOUT = { 39969: 1, 49687: 1, 49689: 1, 49692: 1, 54288: 1 };
    const P_PAD   = { 52206: 1, 54282: 1, 35232: 1 };
    const BITS = 17422;
    // THE PUSH SOLUTION, anchored on the PRESSURE PAD. The room ROTATES per instance but
    // the barrels are FIXED relative to the pad, so everything is stored
    // as (dx,dy) offsets from the pad and matched under rotation at runtime -- no absolute
    // tiles, no bits anchor.
    //   BARREL_OFFS = the 13 push-barrel start tiles (the corner barrels make the set
    //                 rotationally unique, so the orientation fit has one answer).
    //   BARREL_SOLN = the 5 one-tile pushes, ordered, as [fromOff, toOff].
    const BARREL_OFFS = [[3,-4],[3,-3],[3,-2],[4,-5],[4,-2],[4,-1],[5,-4],[5,-3],[5,-1],
                         [-2,-8],[-1,-8],[-2,3],[-1,3]];
    const BARREL_SOLN = [[[5,-3],[5,-4]],[[5,-1],[5,0]],[[4,-1],[3,-1]],[[-1,3],[-1,4]],[[-2,3],[-3,3]]];
    // One quarter-turn: (x,y) -> (y,-x). Four of these cover every room orientation
    // (Daemonheim rooms rotate, never mirror), so one handedness suffices.
    const rotOff = (p, r) => { let x = p[0], y = p[1]; for (let i = 0; i < r; i++) { const t = x; x = y; y = -t; } return [x, y]; };
    // (no DIR_NAME any more -- push directions are not displayed; see the push step below)
    const isBarrel = n => n.name === 'Barrel';
    const act = (e, a) => (e.actions || []).some(x => x === a);
    const broken = npcs.find(n => inR(n) && (B_FIX[n.id] || (isBarrel(n) && act(n, 'Fix'))));
    const spout  = (objs || []).find(o => inR(o) && (P_SPOUT[o.id] || o.name === 'Expelling pipe'));
    const pad    = (objs || []).find(o => inR(o) && (P_PAD[o.id] || o.name === 'Pressure pad'));
    const doneB  = npcs.find(n => inR(n) && (B_DONE[n.id] || (isBarrel(n) && !(n.actions || []).length)));
    // A pressure pad ALONE is not barrel-room evidence: the EMOTE room has pads too,
    // and the shared progress varc 1233 reads 201 there, so the pad-only entry drew
    // "Full -- push the barrel onto this" in the emote room. Enter
    // only on something the barrel room alone has: a Barrel npc or the Expelling pipe.
    if (broken || spout || (pad && npcs.some(n => inR(n) && isBarrel(n)))) {
      // Finished: a barrel with nothing left to do is sitting on the pad.
      const settled = doneB && pad && doneB.x === pad.x && doneB.y === pad.y;
      if (settled) {
        dungGuideTiles(marks);
        dungHighlightNpc(0);
        return;
      }
      const bitsTile = (dungGroundCache || []).find(g => g && g.id === BITS && here(g));
      // THE PATH-CLEAR PUSHES come first (they open the way to the bits). Recover the
      // room's rotation by fitting the captured barrel layout to the LIVE push barrels,
      // both as pad offsets; the rotation with the most on-tile matches wins (barrels
      // pushed so far have moved only 1 tile, so the untouched majority still pins it).
      const pushBarrels = pad ? npcs.filter(n => inR(n) && isBarrel(n) && act(n, 'Push')) : [];
      const liveOff = new Set(pushBarrels.map(n => (n.x - pad.x) + ',' + (n.y - pad.y)));
      let bestR = -1, bestHits = -1;
      const rotScores = [];
      if (pad) for (let r = 0; r < 4; r++) {
        let hits = 0;
        for (const p of BARREL_OFFS) { const o = rotOff(p, r); if (liveOff.has(o[0] + ',' + o[1])) hits++; }
        rotScores.push(hits);
        if (hits > bestHits) { bestHits = hits; bestR = r; }
      }
      // A TIE means the fit is guessing: `hits > bestHits` silently keeps the lowest r,
      // which points the whole solution a quarter-turn wrong (step 1
      // labelled north when the room wanted east). Refuse to guide on an ambiguous fit
      // rather than send the player at the wrong barrel.
      const rotTied = rotScores.filter(h => h === bestHits).length > 1;
      // The captured push step: the first move (in order) whose source barrel is still on
      // its start tile. A move completes when the barrel leaves that tile -- driven by
      // what ACTUALLY moved, so a mis-push just keeps the same step live until it's right.
      // Require a solid fit (>= 8 of 13) before trusting the rotation. Live geometry + the
      // fit's working, copyable from the map tab.
      if (pad) dungBarrelDump = {
        pad: [pad.x, pad.y], rotScores: rotScores, bestR: bestR, bestHits: bestHits,
        tied: rotTied,
        liveOffsets: pushBarrels.map(n => [n.x - pad.x, n.y - pad.y])
                                .sort((a, b) => a[0] - b[0] || a[1] - b[1]),
        capturedOffsets: BARREL_OFFS,
        fittedSoln: bestR >= 0 ? BARREL_SOLN.map(m => [rotOff(m[0], bestR), rotOff(m[1], bestR)]) : null
      };
      const pushStep = () => {
        if (!pad || bestHits < 8 || rotTied) return null;
        for (let i = 0; i < BARREL_SOLN.length; i++) {
          const fr = rotOff(BARREL_SOLN[i][0], bestR), to = rotOff(BARREL_SOLN[i][1], bestR);
          const fx = pad.x + fr[0], fy = pad.y + fr[1];
          const b = pushBarrels.find(n => n.x === fx && n.y === fy);
          if (b) return { b: b, step: i + 1, of: BARREL_SOLN.length,
                          dx: to[0] - fr[0], dy: to[1] - fr[1],
                          tx: pad.x + to[0], ty: pad.y + to[1], plane: b.plane || 0 };
        }
        return null;
      };
      if (broken && dungInvCount(BITS) <= 0) {
        const ps = pushStep();
        if (ps) {
          dungGuideTiles(marks);
          // WHICH BARREL ONLY -- no destination mark, no direction word.
          // The barrel POSITIONS rotate reliably (a live room fit the captured set 13/13 at
          // 180 degrees, scores 0/0/13/0), but its step 1 still had to go east where the
          // rotated capture said north: the recorded per-move directions do NOT survive the
          // rotation, so anything beyond "this barrel" would be a guess pointed at the
          // player. The step still advances off the barrel LEAVING its tile, which needs no
          // direction to detect.
          // TILE-ANCHORED needle: every push barrel is npc 11072, so an id-only needle let
          // the reader box whichever 11072 it found first. Same bug the ghost room fixed on
          //; the anchor is what disambiguates same-id entities.
          dungHighlightList(['#' + ps.b.id + '|Push this barrel  ('
                             + ps.step + ' of ' + ps.of + ')|'
                             + ps.b.x + ';' + ps.b.y + ';1']);
          return;
        }
        // Pushes done (or no fit) -> take the bits. GROUND ITEM: dungGroundCache, not objs.
        for (const g of (dungGroundCache || []))
          if (g && g.id === BITS && typeof g.x === 'number' && here(g) && marks.length < 16)
            marks.push({ x: g.x, y: g.y, plane: g.plane || 0, rgb: 0x5fd07a,
                         label: 'Take the barrel bits' });
        dungGuideTiles(marks);
        dungHighlightNpc(0);
        return;
      }
      if (broken) {
        dungGuideTiles(marks);
        dungHighlightList(['#' + broken.id + '|Fix this barrel|'    // anchored, as above
                           + broken.x + ';' + broken.y + ';1']);
        return;
      }
      // FILL LEVEL = varc 1233, 0..200. Already read each tick into
      // dungMonoCharge -- 1233 is a GENERIC per-room progress counter, shared with the
      // monolith (0-195) and the emotes room (67/134/201), so it means nothing on its own
      // and is only trusted here because the room is known to be a barrel room.
      // Full -> the barrel goes on the pad; not full -> it goes under the spout. Before
      // this the two were offered together because there was no way to tell them apart.
      const fill = (typeof dungMonoCharge === 'number') ? dungMonoCharge : null;
      const full = fill != null && fill >= 200;
      if (!full && spout && marks.length < 16)
        marks.push({ x: spout.x, y: spout.y, plane: spout.plane || 0, rgb: 0x5fd07a,
                     label: '-Expelling pipe' + String.fromCharCode(10)
                          + 'Push the barrel under this to fill it'
                          + (fill != null ? '  (' + fill + '/200)' : '') });
      if (full && pad && marks.length < 16)
        marks.push({ x: pad.x, y: pad.y, plane: pad.plane || 0, rgb: 0x5fd07a,
                     label: '-Pressure pad' + String.fromCharCode(10) + 'Full -- push the barrel onto this' });
      // the other end of the job, shown quietly so the order is visible without a pill
      if (!full && pad && marks.length < 16)
        marks.push({ x: pad.x, y: pad.y, plane: pad.plane || 0, rgb: 0x4a90d9, label: '' });
      dungGuideTiles(marks);
      dungHighlightNpc(0);
      return;
    }
  }
  // COLOURED-FERRET ROOM -- a different puzzle from the fish/plate one below. Five
  // ferrets, each to be SCARED onto the pressure plate of its own colour.
  //
  // ENTIRELY NAME- AND ACTION-DRIVEN, no id tables. The cache holds 10 plate ids per
  // colour across three separate blocks (33xxx, 543xx, 137xxx) and two npc ids per
  // colour, and the live room mixes them -- so any table would be a large thing to get
  // wrong for no benefit. The live objects already say everything needed:
  //
  //   ferret name  "<Colour> ferret"          -> which plate it wants
  //   ferret has the Scare action             -> still to do
  //   ferret has LOST Scare                   -> that colour is finished
  //   plate name   "<Colour> pressure plate"  -> the target
  //
  // Verified against a live room : blue and orange were solved, and
  // both signals agreed -- their ferrets carried the solved ids (12369 / 13726) AND had
  // no Scare action, while red/green/yellow kept 1216x ids and the Scare option. The
  // solved plates also gain a second co-located id, which is a third way to read the
  // same fact and is deliberately NOT used: one signal is enough and Scare is the one
  // that says what to DO.
  {
    const colFerrets = npcs.filter(n => typeof n.x === 'number' && here(n)
                                     && /^[A-Z][a-z]+ ferret$/.test(String(n.name || '')));
    if (colFerrets.length) {
      const todo = colFerrets.filter(n => (n.actions || []).some(a => a === 'Scare'));
      // ONE AT A TIME, and LATCHED rather than recomputed: the pick is the ferret nearest the
      // player, held until that colour is finished (its Scare disappears) or it leaves the room.
      // Re-picking every reconcile would swap the target as the player walks past another ferret.
      if (dungColFerret && !todo.some(n => String(n.name).split(' ')[0] === dungColFerret))
        dungColFerret = null;                        // finished or gone -> free the latch
      if (!dungColFerret && todo.length) {
        let best = null, bestD = Infinity;
        for (const f of todo) {
          const d = dungSelfPos
            ? Math.max(Math.abs(f.x - dungSelfPos.x), Math.abs(f.y - dungSelfPos.y)) : 0;
          // deterministic tiebreak on the name, so an exact distance tie cannot flip
          if (d < bestD || (d === bestD && best && f.name < best.name)) { bestD = d; best = f; }
        }
        if (best) dungColFerret = String(best.name).split(' ')[0];
      }
      const pick = todo.find(n => String(n.name).split(' ')[0] === dungColFerret);
      if (pick) {
        const colour = dungColFerret, low = colour.toLowerCase();
        const plate = (objs || []).find(o => typeof o.x === 'number' && here(o)
                                          && o.name === colour + ' pressure plate');
        const rgb = DUNG_FERRET_RGB[colour] || 0xffffff;
        // Plate: a coloured box, NO label. The destination is obvious once it is outlined
        // in the ferret's colour, and three pills at once is unreadable.
        if (plate) marks.push({ x: plate.x, y: plate.y, plane: plate.plane || 0,
                                label: '', rgb: rgb });
        // WHERE TO STAND. A scared ferret flees directly AWAY from the player, so to
        // drive it onto its plate you stand on the far side of it: the tile one step
        // beyond the ferret along the plate->ferret line. Knowing the
        // plate is only half the instruction -- the position is the part that is easy to
        // get wrong and slow to work out by eye.
        //
        // CARDINAL ONLY -- a scare drives the ferret along one axis, never diagonally
        //. So the stand tile is always directly N/S/E/W of it, and the
        // room is played one axis at a time: a push changes only the axis it is along, so
        // zero one offset, then the other.
        //   already on the plate's row    -> push along x, the finishing shot
        //   already on its column         -> push along y, the finishing shot
        //   neither                       -> close the BIGGER gap first
        //
        // UNVERIFIED: how far one scare moves a ferret. Only the direction is computed
        // here and that holds either way -- if a scare moves it a single tile, the same
        // tile is simply re-offered until the axis is zeroed, since it recomputes from
        // the ferret's live position each reconcile.
        if (plate) {
          const dx = pick.x - plate.x, dy = pick.y - plate.y;
          let sx = 0, sy = 0;
          if (dy === 0 && dx !== 0) sx = Math.sign(dx);
          else if (dx === 0 && dy !== 0) sy = Math.sign(dy);
          else if (Math.abs(dx) >= Math.abs(dy)) sx = Math.sign(dx);
          else sy = Math.sign(dy);
          if (sx || sy) marks.push({ x: pick.x + sx, y: pick.y + sy, plane: pick.plane || 0,
                                     label: 'Stand here, scare ' + low, rgb: rgb });
        }
        dungGuideTiles(marks);
        dungHighlightList(['#' + pick.id]);   // box only; the stand tile carries the text
      } else {
        dungGuideTiles(marks);
        dungHighlightList([]);
      }
      return;
    }
  }
  // Ferret puzzle: throw fish onto tiles in the ferret's straight-line sight to walk it
  // onto the pressure plate. Holes must never be on the run. BFS over straight runs in
  // ALL 8 directions (diagonal moves are legal) from the ferret's LIVE
  // tile; minimum throws, marked in order, re-solved as it moves.
  //
  // ALL FLOOR STYLES (cache-derived, not just the hand-measured themes). The boards sit
  // in TWO id blocks, not one, so no arithmetic rule reaches them all:
  //
  //   49546-49560  three themes, laid out as 3 tiles, then 6 holes (plain x3 then
  //                corner x3), then 6 plates. A theme is a fixed offset into each run:
  //                theme 2 = tile 49548, hole 49551, corner 49554, plate 49557, which is
  //                exactly the floor that exposed the gap.
  //   54293-54297  a fourth theme: tile 54293, holes 54294/54295, plates 54296/54297.
  //
  // Located by scanning js5-16 for the Tile/Hole/Pressure plate families and confirming
  // each block with its Fishing spot (every ferret room has one) -- there are exactly
  // four, so this puzzle does not appear in all five theme families.
  // NB the two plates per theme are presumably the up/pressed states; both are accepted
  // because they share a tile, so which one is found cannot change the pathing.
  {
    const DUNG_FERRET_TILE = { 49546: 1, 49547: 1, 49548: 1, 54293: 1 };
    const DUNG_FERRET_HOLE = { 49549: 1, 49550: 1, 49551: 1, 49552: 1, 49553: 1, 49554: 1,
                               54294: 1, 54295: 1 };
    const DUNG_FERRET_PLATE = { 49555: 1, 49556: 1, 49557: 1, 49558: 1, 49559: 1, 49560: 1,
                                54296: 1, 54297: 1 };
    // 11007 and 11010 are the Daemonheim ferrets; every other npc named "Ferret" is a
    // Hunter box-trap catch, far outside this id block.
    const DUNG_FERRET_NPC = { 11007: 1, 11010: 1 };
    const ferret = npcs.find(n => DUNG_FERRET_NPC[n.id] && typeof n.x === 'number' && here(n));
    const plateO = (objs || []).find(o => DUNG_FERRET_PLATE[o.id] && typeof o.x === 'number' && here(o));
    if (ferret && plateO) {
      const ftiles = (objs || []).filter(o =>
        (DUNG_FERRET_TILE[o.id] || DUNG_FERRET_HOLE[o.id]) && typeof o.x === 'number' && here(o));
      if (ftiles.length >= 9) {
        const fsk = ferret.x + ',' + ferret.y, fpk = plateO.x + ',' + plateO.y;
        // ferret settle: same tile two reconciles running = not mid-walk
        if (dungFerretSettle && dungFerretSettle.key === fsk) dungFerretSettle.n++;
        else dungFerretSettle = { key: fsk, n: 1 };
        const fsettled = dungFerretSettle.n >= 2;
        // hold the route while the ferret executes it: advance on arrivals, and
        // only re-solve when it SETTLES somewhere off-plan (a mis-throw)
        if (dungFerretPlan) {
          while (dungFerretPlan.idx < dungFerretPlan.stops.length && dungFerretPlan.stops[dungFerretPlan.idx] === fsk) dungFerretPlan.idx++;
          if (dungFerretPlan.idx >= dungFerretPlan.stops.length) dungFerretPlan = null;
          else if (fsettled && fsk !== dungFerretPlan.anchor && dungFerretPlan.stops.indexOf(fsk) < 0)
            dungFerretPlan = null;
        }
        if (!dungFerretPlan && fsettled && fsk !== fpk) {
          let fx0 = 1e9, fx1 = -1, fy0 = 1e9, fy1 = -1;
          const fbad = {};
          for (const t of ftiles) {
            if (t.x < fx0) fx0 = t.x; if (t.x > fx1) fx1 = t.x;
            if (t.y < fy0) fy0 = t.y; if (t.y > fy1) fy1 = t.y;
            if (!DUNG_FERRET_TILE[t.id] && !(t.x === plateO.x && t.y === plateO.y)) fbad[t.x + ',' + t.y] = 1;
          }
          const fpass = (x, y) => x >= fx0 && x <= fx1 && y >= fy0 && y <= fy1 && !fbad[x + ',' + y];
          const fprev = {}; fprev[fsk] = null;
          const fq = [[ferret.x, ferret.y]];
          for (let qi = 0; qi < fq.length; qi++) {
            const x = fq[qi][0], y = fq[qi][1];
            for (const dd of [[1, 0], [-1, 0], [0, 1], [0, -1], [1, 1], [1, -1], [-1, 1], [-1, -1]]) {
              let nx = x, ny = y;
              while (fpass(nx + dd[0], ny + dd[1])) {
                nx += dd[0]; ny += dd[1];
                const k = nx + ',' + ny;
                if (fprev[k] === undefined) { fprev[k] = x + ',' + y; fq.push([nx, ny]); }
              }
            }
          }
          if (fprev[fpk] !== undefined) {
            const stops = [];
            for (let k = fpk; k && k !== fsk; k = fprev[k]) stops.unshift(k);
            dungFerretPlan = { anchor: fsk, stops: stops, idx: 0 };
          }
        }
        if (dungFerretPlan) {
          let step = 1;
          for (const k of dungFerretPlan.stops.slice(dungFerretPlan.idx)) {
            if (marks.length >= 16) break;
            const p2 = k.split(',').map(Number);
            marks.push({ x: p2[0], y: p2[1], plane: 0,
                         label: (k === fpk ? step + ' - plate' : String(step)), rgb: 0x5ae1e1 });
            step++;
          }
        }
      }
    }
  }
  // Lodestone pillar room: the jump route is FIXED ("always
  // exactly this path"; all 7 js5-5 templates are interior-identical, so nothing
  // per-instance to solve). 7x7 'Gap' lattice (theme family 49567/68/69), the
  // 'Lodestone' (Unlock) on the platform off one side; the route below is the
  //  6-gap sequence in lattice-local coords for the CANONICAL
  // orientation (origin = min-x/min-y lattice corner when the lodestone sits on
  // the -Y side; entry corner is then max-x/max-y). Other spawns rotate the whole
  // room, and the lodestone's side of the lattice pins the rotation. Solved room
  // morphs the lodestone to 'Active lodestone' (no actions) -> guidance clears.
  // NB rotations only, no mirror seen; a floor drawing this wrong = mirrored
  // spawn, capture it and add door-based mirror detection.
  {
    const DUNG_LODE_ROUTE = [[6, 5], [5, 4], [3, 4], [1, 4], [0, 3], [0, 1]];
    const lode = (objs || []).find(o => o.name === 'Lodestone' && typeof o.x === 'number' && here(o) &&
      o.actions && o.actions.length);
    // solved = the activated morph is in the room (cache: 'Active lodestone' 49573;
    // 'Activated lodestone' 49319/50785/51053/51099/51601/51647 = the other themes'
    // wording; all actionless). Checked explicitly in case the Unlock loc REMAINS
    // alongside it, tunnel-style -- the action filter alone wouldn't clear then.
    const lodeDone = (objs || []).some(o =>
      (o.name === 'Active lodestone' || o.name === 'Activated lodestone') && typeof o.x === 'number' && here(o));
    const lgaps = (objs || []).filter(o => o.name === 'Gap' && typeof o.x === 'number' && here(o));
    if (lode && !lodeDone && lgaps.length >= 20) {
      let gx0 = 1e9, gx1 = -1, gy0 = 1e9, gy1 = -1;
      for (const g of lgaps) {
        if (g.x < gx0) gx0 = g.x; if (g.x > gx1) gx1 = g.x;
        if (g.y < gy0) gy0 = g.y; if (g.y > gy1) gy1 = g.y;
      }
      if (gx1 - gx0 === 6 && gy1 - gy0 === 6) {   // full 7x7 lattice in scene
        const dx = lode.x - (gx0 + 3), dy = lode.y - (gy0 + 3);   // lodestone vs lattice centre
        // map canonical (cx,cy) -> world by the rotation that puts the lodestone
        // back on the canonical -Y side
        let xf = null;
        if (Math.abs(dy) > Math.abs(dx)) xf = dy < 0
          ? (c => [gx0 + c[0], gy0 + c[1]])                 // lodestone -Y: canonical
          : (c => [gx0 + 6 - c[0], gy0 + 6 - c[1]]);        // +Y: rot 180
        else xf = dx < 0
          ? (c => [gx0 + c[1], gy0 + 6 - c[0]])             // -X: rot 90 CW
          : (c => [gx0 + 6 - c[1], gy0 + c[0]]);            // +X: rot 90 CCW
        let step = 1;
        for (const c of DUNG_LODE_ROUTE) {
          if (marks.length >= 16) break;
          const w = xf(c);
          marks.push({ x: w[0], y: w[1], plane: 0,
                       label: (step === DUNG_LODE_ROUTE.length ? step + ' - to the lodestone' : String(step)),
                       rgb: 0xf0c419, snap: 1 });
          step++;
        }
      }
    }
  }
  // Ice-slide room ("Icy pressure pads", frozen theme). Mechanics re-grounded
  // on the wiki + live routes; the previous model was BACKWARDS
  // on both stop rules and planned impossible tours:
  //   (a) PADS STOP THE SLIDE -- a pad is pressed by LANDING on it; you cannot slide
  //       over one (old model: "buttons never halt, sliding over presses");
  //   (b) furniture stops you on ENTERING its 3x3 surround -- sliding PAST a barrel
  //       one tile to the side halts you beside it, not only a head-on collision;
  //   (c) otherwise you slide to the last free tile before furniture or the wall.
  // Obstacles are the EXPLICIT furniture ids (Barrels 49328 / Chair 49329 / Crate
  // 49330). Everything else in the room -- Snow/Trash, the nameless edge locs
  // 49307-15/49332-34, doors -- is scenery the slide model IGNORES: any looser rule
  // ("every non-button loc blocks", then "every loc not named Snow/Trash blocks")
  // degenerated plans into 1-tile hops twice. Pads change id when pressed (49320-23
  // unpressed -> 49324-27 pressed; PRESSED pads still stop a slide) so the remaining
  // set reads straight from live loc ids, no stand-tracking. Cache-audited:
  // 49320-27 is the ONLY Daemonheim Button family (the other cache Button block at
  // 24588+ is a museum), so this room has NO theme variants. NPCs are NOT in the
  // model (wiki: purely environmental); they ARE in the copyable dump below so one
  // live route can falsify that if a slide ever stops at a monster.
  {
    const iceUnpressed = (objs || []).filter(o =>
      o.id >= 49320 && o.id <= 49323 && typeof o.x === 'number' && here(o));   // pads still to press
    const icePressed = (objs || []).filter(o =>
      o.id >= 49324 && o.id <= 49327 && typeof o.x === 'number' && here(o));
    const ICE_FURN = { 49328: 1, 49329: 1, 49330: 1 };
    const iceFurn = (objs || []).filter(o =>
      ICE_FURN[o.id] && typeof o.x === 'number' && here(o));
    if (iceUnpressed.length && dungFloorSW && dungSelfPos) {
      const pr = dungRoomOf(dungFloorSW, dungSelfPos.x, dungSelfPos.y);
      const swx = dungFloorSW.x + DUNG_ROOM_PITCH * pr.rx, swy = dungFloorSW.y + DUNG_ROOM_PITCH * pr.ry;
      const roomKey = pr.rx + ',' + pr.ry;
      // The ICE SHEET is inset one tile inside the 14x14 room grid: the 0/13 ring is
      // the wall base (dump: wall-line 'Rock' at x=0, every pad and
      // furniture within 1..11; a 0..13 model rested slides ON the ring and its wall
      // mark floated fully outside the room, screenshot).
      const ICE_LO = 1, ICE_HI = DUNG_ROOM_W - 2;
      const furn = {}, pad = {};   // furn value = the furniture's name, for "stops at X" labels
      for (const o of iceFurn) furn[(o.x - swx) + ',' + (o.y - swy)] = o.name || 'furniture';
      for (const o of iceUnpressed) pad[(o.x - swx) + ',' + (o.y - swy)] = 1;
      for (const o of icePressed) pad[(o.x - swx) + ',' + (o.y - swy)] = 1;
      const px = dungSelfPos.x - swx, py = dungSelfPos.y - swy;
      const posKey = px + ',' + py;
      // settle detector: never (re)solve mid-slide
      if (dungIceSettle && dungIceSettle.key === posKey) dungIceSettle.n++;
      else dungIceSettle = { key: posKey, n: 1 };
      const settled = dungIceSettle.n >= 2;
      const rem = iceUnpressed.map(o => ({ x: o.x - swx, y: o.y - swy }));
      const remSig = roomKey + '|' + rem.map(b => b.x + ',' + b.y).sort().join(';');
      // Hold the plan STABLE while executing (marks don't jump mid-slide); re-solve only
      // when: settled AND (no plan / the room or remaining-pad set changed / the player
      // settled somewhere the plan didn't route to). A pressed pad changes the loc id
      // -> remSig changes -> re-solve for the smaller set.
      if (dungIcePlan && dungIcePlan.sig === remSig) {
        while (dungIcePlan.idx < dungIcePlan.stops.length && dungIcePlan.stops[dungIcePlan.idx].k === posKey) dungIcePlan.idx++;
        if (dungIcePlan.idx >= dungIcePlan.stops.length) dungIcePlan = null;
        // The ONE valid resting place is the tile the next leg starts from: the
        // anchor at idx 0, the stop just consumed after that -- nothing else.
        // "Anywhere in the stops list" kept a stale tour alive when the player
        // wandered onto a LATER stop (dump: player at step 4, panel showing step 1),
        // and allowing the ANCHOR mid-tour aimed the click tile along a leg that
        // starts elsewhere (south click tile for a north slide).
        // Any deviation re-solves from where the player actually is.
        else if (settled && !(dungIcePlan.idx === 0
                   ? posKey === dungIcePlan.anchor
                   : dungIcePlan.stops[dungIcePlan.idx - 1].k === posKey))
          dungIcePlan = null;
      } else if (dungIcePlan) dungIcePlan = null;   // set changed -> stale
      // one maximal slide from (cx,cy): advance tile by tile; stop ON a pad (landing
      // presses it,  or on the last free tile BEFORE furniture
      // or the wall. HEAD-ON ONLY: furniture does NOT halt a slide passing beside it
      // -- a lateral 3x3 stop-field kept predicting 1-tile "stops by Chair/Crate"
      // hops the game never makes (; the wiki's "stop in
      // the obstacle's 3x3 range" is just the last-free-tile-before-collision. RS
      // corner rule: a DIAGONAL step needs BOTH flanking cardinal
      // tiles furniture-free -- adjacent furniture leaves 5 usable directions and can
      // also end a diagonal slide early. null = cannot move at all.
      const slideTo = (cx, cy, dd) => {
        let nx = cx, ny = cy;
        for (;;) {
          const tx = nx + dd[0], ty = ny + dd[1];
          if (tx < ICE_LO || ty < ICE_LO || tx > ICE_HI || ty > ICE_HI || furn[tx + ',' + ty]) break;
          if (dd[0] && dd[1] && (furn[tx + ',' + ny] || furn[nx + ',' + ty])) break;
          nx = tx; ny = ty;
          if (pad[nx + ',' + ny]) break;
        }
        return (nx === cx && ny === cy) ? null : [nx, ny];
      };
      if (!dungIcePlan && settled && rem.length) {
        const bit = {};
        rem.forEach((b, i) => { bit[b.x + ',' + b.y] = 1 << i; });
        const full = (1 << rem.length) - 1;
        const DIRS = [[1, 0], [-1, 0], [0, 1], [0, -1], [1, 1], [1, -1], [-1, 1], [-1, -1]];
        const popc = m => { let n = 0; while (m) { n += m & 1; m >>= 1; } return n; };
        // Standing on an unpressed pad does NOT press it (dump: self
        // on 49323 with the id still in the unpressed range) -- so the start mask is
        // ALWAYS empty, and a tour that must press the pad underfoot slides away and
        // lands back on it.
        const m0 = 0;
        const start = posKey + ':' + m0;
        const prev = {}; prev[start] = null;
        const q = [[px, py, m0]];
        let goalK = null, bestK = null, bestBits = popc(m0);
        // BFS over (tile, pressed-mask): the first full-mask state reached is the
        // FEWEST-SLIDES tour (optimal); fallback = most pads when full is unreachable.
        for (let qi = 0; qi < q.length && !goalK; qi++) {
          const cx2 = q[qi][0], cy2 = q[qi][1], m = q[qi][2];
          for (const dd of DIRS) {
            const to = slideTo(cx2, cy2, dd);
            if (!to) continue;
            const nm = m | (bit[to[0] + ',' + to[1]] || 0);   // a pad presses only where the slide STOPS
            const k = to[0] + ',' + to[1] + ':' + nm;
            if (prev[k] !== undefined) continue;
            prev[k] = cx2 + ',' + cy2 + ':' + m;
            q.push([to[0], to[1], nm]);
            const pb = popc(nm);
            if (pb > bestBits) { bestBits = pb; bestK = k; }
            if (nm === full) { goalK = k; break; }
          }
        }
        const endK = goalK || bestK;
        if (endK) {
          // stops carry the click DIRECTION (compass, y+ = north) and the stop REASON,
          // so each in-room mark explains itself.
          const stops = [];
          for (let k = endK; k && k !== start; k = prev[k]) {
            const kp = k.split(':')[0], pp = prev[k].split(':')[0];
            const [sx2, sy2] = kp.split(',').map(Number), [ox2, oy2] = pp.split(',').map(Number);
            const dx2 = Math.sign(sx2 - ox2), dy2 = Math.sign(sy2 - oy2);
            // stop reason: the furniture the slide ran into (tile ahead, or a corner
            // flank for a diagonal), else '' = wall/pad
            const by = furn[(sx2 + dx2) + ',' + (sy2 + dy2)]
              || (dx2 && dy2 && (furn[(sx2 + dx2) + ',' + sy2] || furn[sx2 + ',' + (sy2 + dy2)]))
              || '';
            stops.unshift({ k: kp,
                            press: k.split(':')[1] !== prev[k].split(':')[1],
                            dir: (dy2 > 0 ? 'N' : dy2 < 0 ? 'S' : '') + (dx2 > 0 ? 'E' : dx2 < 0 ? 'W' : ''),
                            ddx: dx2, ddy: dy2, padStop: !!pad[kp], by: by });
          }
          if (stops.length) dungIcePlan = { sig: remSig, anchor: posKey, stops: stops, idx: 0 };
        }
      }
      const iceLoc = o => ({ id: o.id, n: o.name || '', x: o.x - swx, y: o.y - swy });
      dungIceDump = {
        v: 'ice-v6',   // model version stamp -- proves which slide rules built the plan in a pasted dump
        room: roomKey, sw: [swx, swy], self: [px, py], settled: settled,
        padsUn: iceUnpressed.map(iceLoc), padsPr: icePressed.map(iceLoc), furn: iceFurn.map(iceLoc),
        other: (objs || []).filter(o => typeof o.x === 'number' && here(o)
          && !(o.id >= 49320 && o.id <= 49330)).map(iceLoc),
        npcs: (npcs || []).filter(n => typeof n.x === 'number' && here(n))
          .map(n => ({ id: n.id, n: n.name || '', x: n.x - swx, y: n.y - swy })),
        plan: dungIcePlan ? dungIcePlan.stops.slice(dungIcePlan.idx) : null
      };
      // render ONLY the next two stops, IN ORDER. The held plan advances as each stop is
      // reached, so the pair walks the tour step by step; direction + stop reason stay on the
      // label. NOTHING renders mid-slide: while the position is still changing (!settled) the
      // marks drop, and the next pair appears once the player has come to rest.
      if (dungIcePlan && settled) {
        let step = 1;
        for (const s of dungIcePlan.stops.slice(dungIcePlan.idx, dungIcePlan.idx + 2)) {
          if (marks.length >= 16) break;
          const p2 = s.k.split(',').map(Number);
          // STEP 1 is ONE mark: the adjacent tile to CLICK ( -- no
          // destination mark). Direction is recomputed from the PLAYER's tile, not
          // taken from the stored leg, as a second guard against ever aiming a click
          // tile along a leg that starts elsewhere. Green = this slide lands on a pad.
          // snap MUST be 0 on any bare-ice tile: snap makes the reader hunt for a
          // nearby live loc and box ITS model AABB -- a click tile beside furniture
          // drew the Barrels' 2x1 box half outside the room. Only
          // step-2 marks that sit ON a pad loc snap to it.
          if (step === 1) {
            marks.push({ x: swx + px + Math.sign(p2[0] - px), y: swy + py + Math.sign(p2[1] - py), plane: 0,
                         label: '1 - click here', rgb: s.press ? 0x5fd07a : 0xf0c75a, snap: 0 });
          } else
            marks.push({ x: swx + p2[0], y: swy + p2[1], plane: 0,
                         label: '2 - then ' + (s.dir || '?')
                           + (s.press ? ', land on pad' : s.padStop ? ', stops on pad'
                              : s.by ? ' (stops at ' + s.by + ')' : ' (to wall)'),
                         rgb: s.press ? 0x5fd07a : 0x5ab8f0, snap: (s.press || s.padStop) ? 1 : 0 });
          step++;
        }
      }
    }
  }
  // Switches room: every Pull switch in the room gets a Pull mark (first label line =
  // loc name so the reader boxes the loc itself; second line = the action). A PULLED
  // switch spawns an actionless done-twin at the SAME tile while the Pull loc stays in
  // the data, so any switch sharing a done-twin's coordinate is already done -- skip it.
  // FOUR theme variants, ALL enumerated from a js5-16 whole-index name+action scan
  //: the done-offset is NOT constant (+3 for the 49381 block, +1 for 54333),
  // so match on explicit id sets, never an offset. The scan found no 5th 'Switch'/'Lever'
  // Pull loc anywhere in the Daemonheim range -- 4 is the complete cache set.
  const DUNG_SWITCH_PULL = { 49381: 1, 49382: 1, 49383: 1, 54333: 1 };
  const DUNG_SWITCH_DONE = { 49384: 1, 49385: 1, 49386: 1, 54334: 1 };
  const pulled = {};
  for (const o of (objs || [])) if (DUNG_SWITCH_DONE[o.id] && typeof o.x === 'number') pulled[o.x + ',' + o.y] = true;
  for (const o of (objs || []))
    if (DUNG_SWITCH_PULL[o.id] && typeof o.x === 'number' && here(o) && !pulled[o.x + ',' + o.y] && marks.length < 16)
      marks.push({ x: o.x, y: o.y, plane: o.plane || 0, label: (o.name || '') + '\nPull' });
  // (Power-up plate marks moved BELOW the lodeSolved read: they were emitted with no
  // solved gate at all, so the box sat on the pad forever after the room completed.)
  // FREMENNIK SCOUT room (new content; live capture + js5-16 scan: the
  // scout (npc 11001, Talk to) wants battleaxes smithed from the Crate of bars, bows
  // fletched from the Crate of logs and fish cooked from the Crate of raw fish.
  // Crate id families across themes -- fish 35275/49522-24/54302, bars
  // 35277/49528-30/54304, logs 35279/49534-36/54306. Only ACTIONED crates mark
  // (Empty crate 49519-21 and friends are the emptied/decor states), so each task's
  // box drops off as its crate is used up. The scout presumably re-ids once the room
  // completes; that solved gate gets added when the id is captured.
  // Crate STATE MACHINE (cooking replaced raw 49524 with
  // 'Crate of fish' 49527 Take-from at the same tile, fletching 49536 -> 49539):
  //   work state (make the goods)  ->  Take-from state (collect them)  ->  Empty.
  // 35xxx/54xxx themes step +1 per state, the 49xxx theme trios step +3.
  const DUNG_SCOUT_CRATES = {};
  for (const ci of [35275, 49522, 49523, 49524, 54302]) DUNG_SCOUT_CRATES[ci] = 'Cook the fish';
  for (const ci of [35277, 49528, 49529, 49530, 54304]) DUNG_SCOUT_CRATES[ci] = 'Smith battleaxes';
  for (const ci of [35279, 49534, 49535, 49536, 54306]) DUNG_SCOUT_CRATES[ci] = 'Fletch bows';
  // The Take-from successor means that task is SOLVED -- it is a done-marker for its
  // tile, never a task itself.
  const DUNG_SCOUT_DONE = {};
  for (const ci of [35276, 49525, 49526, 49527, 54303,
                    35278, 49531, 49532, 49533, 54305,
                    35280, 49537, 49538, 49539, 54307]) DUNG_SCOUT_DONE[ci] = 1;
  {
    const inSR = o => typeof o.x === 'number' && here(o);
    const works = (objs || []).filter(o => DUNG_SCOUT_CRATES[o.id] && inSR(o));
    const dones = (objs || []).filter(o => DUNG_SCOUT_DONE[o.id] && inSR(o));
    if (works.length || dones.length) {
      // SAME-TILE STATE PRECEDENCE: the capture keeps replaced states listed (stale
      // raw crate beside the new fish crate), so a done state or an Empty crate at a
      // tile ends that tile -- only a work state with no successor beside it marks.
      // ROOM SOLVED = all three tasks superseded: every work tile
      // is then suppressed, nothing marks, and the block goes quiet on its own.
      const doneAt = {};
      for (const o of (objs || [])) {
        if (typeof o.x !== 'number') continue;
        if (DUNG_SCOUT_DONE[o.id] || o.name === 'Empty crate') doneAt[o.x + ',' + o.y] = 1;
      }
      for (const o of works) {
        if (marks.length >= 16) break;
        if (doneAt[o.x + ',' + o.y]) continue;
        marks.push({ x: o.x, y: o.y, plane: o.plane || 0, rgb: 0x5fd07a,
                     label: '-' + (o.name || 'Crate') + '\n' + DUNG_SCOUT_CRATES[o.id] });
      }
      dungGuideTiles(marks);
      dungHighlightNpc(0);
      return;
    }
  }
  // Divine skinweaver boss (skeletal horde): Tunnel locs 49286-49288 need blocking;
  // a done one gains a "Blocked tunnel" 49289/49290 at the SAME coordinate while the
  // Tunnel loc stays ( ) -- skip those. The room spans
  // two cells and the tunnels sit in wall gaps, so the same-room gate would drop
  // some; instead gate on the Divine Skinweaver NPC 10058 being in scene (short NPC
  // capture range = the player is at the boss; the tunnel ids exist nowhere else).
  if (npcs.some(n => n.id === 10058)) {
    const DUNG_TUNNEL = { 49286: 1, 49287: 1, 49288: 1 };
    const DUNG_TUNNEL_DONE = { 49289: 1, 49290: 1, 49291: 1 };   // blocked = tunnel id + 3
    const tdone = {};
    for (const o of (objs || [])) if (DUNG_TUNNEL_DONE[o.id] && typeof o.x === 'number') tdone[o.x + ',' + o.y] = true;
    for (const o of (objs || []))
      if (DUNG_TUNNEL[o.id] && typeof o.x === 'number' && !tdone[o.x + ',' + o.y] && marks.length < 16)
        marks.push({ x: o.x, y: o.y, plane: o.plane || 0, label: (o.name || 'Tunnel') + '\nBlock', rgb: 0xe8b34b });
  }
  // Rotating crystal: 49510 appears BRIEFLY every 5 game ticks (~3s; 1 tick ~
  // 600ms). Each appearance restarts the phase clock; the phase persists while
  // the crystal is absent. The centre-screen prompt (same channel as the BGH
  // "Bound" banner) fires ONE TICK EARLY -- at phase 4 -- so a click begun on the
  // prompt lands exactly as the crystal appears; it clears after the moment
  // passes. The mark on the live crystal stays for aiming.
  // 49513 present = the crystal puzzle is SOLVED: drop every crystal aid (click
  // prompt, sync marks, phase clock) for this room.
  // "Large crystal" 49507-49515 is NINE ids = THREE STATES x THREE THEMES, not nine
  // themes. Proven by a solved-room capture : 49508 AND 49514 sit
  // live on the SAME centre tile (10535,2791) with every coloured crystal gone, i.e.
  // base + solved coexisting, the same per-tile state coexistence as the censers.
  //   base/idle  49507, 49508, 49509   <- the centre while the puzzle runs
  //   active     49510, 49511, 49512   <- the "appearing" one that marks the cycle
  //   solved     49513, 49514, 49515
  // Theme index is consistent across states: this floor is theme 1 (49508/49511/49514),
  // the original code was written against theme 0 (49507/49510/49513) and only ever
  // failed because of THEME coverage. 54275-77 = the FOURTH theme's large crystal
  // (single-variant 54xxx block, scene capture, in the same base/active/solved
  // state order the 49xxx block uses (pattern fill).
  const DUNG_BIG_BASE   = { 49507:1, 49508:1, 49509:1, 54275:1 };
  const DUNG_BIG_ACTIVE = { 49510:1, 49511:1, 49512:1, 54276:1 };
  const DUNG_BIG_SOLVED = { 49513:1, 49514:1, 49515:1, 54277:1 };
  // SOLVED = the SECOND large-crystal state APPEARING in scene : the
  // game replaces base -> solved at the centre, and since the capture keeps the
  // replaced base, a solved room lists BOTH states on one tile (proven capture: 49508 +
  // 49514 at 8263,1848). Matched by the known solved ids, plus -- theme-proof -- any
  // TWO distinct non-ACTIVE large-crystal states on the same tile ('Large crystal' by
  // name). ACTIVE is excluded: the appearing state legitimately coexists with the base
  // every cycle mid-puzzle.
  let lodeSolved = (objs || []).some(o => DUNG_BIG_SOLVED[o.id] && typeof o.x === 'number' && here(o));
  if (!lodeSolved) {
    const bigAt = {};
    for (const o of (objs || [])) {
      if (typeof o.x !== 'number' || DUNG_BIG_ACTIVE[o.id] || !here(o)) continue;
      if (!(DUNG_BIG_BASE[o.id] || DUNG_BIG_SOLVED[o.id] || o.name === 'Large crystal')) continue;
      const tk = o.x + ',' + o.y;
      (bigAt[tk] = bigAt[tk] || new Set()).add(o.id);
      if (bigAt[tk].size >= 2) { lodeSolved = true; break; }
    }
  }
  if (lodeSolved && (dungLodeTick >= 0 || dungLodeCenterOn)) {
    dungLodeKey = ''; dungLodeTick = -1; dungLodeWarnAt = -1; dungCrysMem = {};
    if (dungLodeCenterOn) { dungLodeCenterOn = false; try { bridge().centerText(myPid(), ''); } catch (e) {} }
  }
  // Power-up locs -- cache name "Inactive lodestone" (js5-16 -- all 3
  // themes per colour plus the 54xxx theme's four singles: mark each with its action,
  // but NEVER once the room is solved (ungated, the box lingered on the pad after
  // completion, ).
  const DUNG_POWERUP = { 54263: 1, 54266: 1, 54269: 1, 54272: 1 };
  for (const pb of [49471, 49480, 49489, 49498]) { DUNG_POWERUP[pb] = 1; DUNG_POWERUP[pb + 1] = 1; DUNG_POWERUP[pb + 2] = 1; }
  if (!lodeSolved)
    for (const o of (objs || []))
      if (DUNG_POWERUP[o.id] && typeof o.x === 'number' && here(o) && marks.length < 16)
        marks.push({ x: o.x, y: o.y, plane: o.plane || 0, label: (o.name || '') + '\nPower up' });
  const activeLode = lodeSolved ? null : (objs || []).find(o => DUNG_BIG_ACTIVE[o.id] && typeof o.x === 'number' && here(o));
  // Four coloured crystals travel the cross arms toward the centre; they must all
  // arrive at the same time. While ALL FOUR are in scene the puzzle is in its sync
  // phase -- no click prompt; instead compare each crystal's distance to the centre
  // (the large crystal's tile): equal = in sync, and any crystal off the majority
  // distance gets a mark in its own colour with how far off it is.
  // THREE THEME VARIANTS PER COLOUR (js5-16 scan) -- hardcoding one id per
  // colour meant the solver saw nothing on other floors (seen with the
  // 49487/49496 pair). The cache lays each colour out as NINE CONSECUTIVE ids:
  //   blue   49468-70 crystal | 49471-73 Power-up | 49474-76 lodestone
  //   red    49477-79 crystal | 49480-82 Power-up | 49483-85 lodestone
  //   green  49486-88 crystal | 49489-91 Power-up | 49492-94 lodestone
  //   yellow 49495-97 crystal | 49498-500 Power-up | 49501-03 lodestone
  // Plain "Crystal" 49465-67 (no action) are the ARM/track segments, not movers.
  const DUNG_CRYSTALS = {};
  for (const [base, n, c] of [[49468, 'Blue crystal', 0x5ab8f0], [49477, 'Red crystal', 0xf25c5c],
                              [49486, 'Green crystal', 0x5fd07a], [49495, 'Yellow crystal', 0xf0c419]])
    for (let v = 0; v < 3; v++) DUNG_CRYSTALS[base + v] = { n: n, c: c };
  // Fourth theme, single id per colour (54xxx block, js5-16.
  DUNG_CRYSTALS[54262] = { n: 'Blue crystal',   c: 0x5ab8f0 };
  DUNG_CRYSTALS[54265] = { n: 'Red crystal',    c: 0xf25c5c };
  DUNG_CRYSTALS[54268] = { n: 'Green crystal',  c: 0x5fd07a };
  DUNG_CRYSTALS[54271] = { n: 'Yellow crystal', c: 0xf0c419 };
  const crys = lodeSolved ? [] : (objs || []).filter(o => DUNG_CRYSTALS[o.id] && typeof o.x === 'number' && here(o));
  // Room variant WITHOUT the appearing crystal 49510: the large crystal is the
  // static 49507 -- the cycle mark is instead the moment ANY coloured crystal
  // reaches its coordinate (edge-triggered per crystal id).
  const bigLode = lodeSolved ? null : (objs || []).find(o => DUNG_BIG_BASE[o.id] && typeof o.x === 'number' && here(o));
  if (bigLode) dungLodeCenter = { x: bigLode.x, y: bigLode.y };
  let ltick = -1;
  if (activeLode || bigLode || dungLodeTick >= 0) { try { ltick = bridge().gameTick(myPid()); } catch (e) {} }
  const dungLodeMarkEnd = () => { dungLodeKey = ''; };   // (window length is a known 600ms, not measured)
  // Cycle-anchor with LEEWAY: a mark is DETECTED up to a reconcile (~600ms) after it
  // actually happened, and that lag varies per cycle -- re-anchoring on every
  // detection made the countdown jerk back and forth. Instead the anchor persists
  // across cycles and only tightens: an EARLIER-than-predicted sighting pulls the
  // anchor back (it is closer to the true mark), a later one is ignored as sampling
  // lag, and only a large error (>900ms: real desync) re-locks the clock.
  const dungLodeAnchor = t => {
    const CYC = 2400;
    if (!dungLodeMarkAt) { dungLodeMarkAt = t; return; }
    let err = (t - dungLodeMarkAt) % CYC;
    if (err > CYC / 2) err -= CYC;
    if (Math.abs(err) > 900) dungLodeMarkAt = t;
    else if (err < 0) dungLodeMarkAt += err;
  };
  if (activeLode) {
    const lk = activeLode.x + ',' + activeLode.y;
    dungLodeCenter = { x: activeLode.x, y: activeLode.y };
    dungLodeSeenAt = Date.now();
    if (lk !== dungLodeKey) { dungLodeKey = lk; dungLodeAnchor(Date.now()); if (ltick >= 0) dungLodeTick = ltick; }   // appearance = cycle mark
  } else if (bigLode) {
    // a crystal never lands ON the large crystal -- it gets within 1 tile and
    // resets, so "arrival" = Chebyshev distance <= 1
    const arr = crys.find(c => Math.max(Math.abs(c.x - bigLode.x), Math.abs(c.y - bigLode.y)) <= 1);
    if (arr) {
      dungLodeSeenAt = Date.now();
      const lk = 'arr' + arr.id;
      if (lk !== dungLodeKey) { dungLodeKey = lk; dungLodeAnchor(Date.now()); if (ltick >= 0) dungLodeTick = ltick; }   // arrival = cycle mark
    } else dungLodeMarkEnd();
  } else dungLodeMarkEnd();
  // staleness: no cycle mark for 12s -> the cycle model is dead, stop prompting
  if (dungLodeTick >= 0 && dungLodeSeenAt && Date.now() - dungLodeSeenAt > 12000) { dungLodeTick = -1; dungLodeWarnAt = -1; }
  // the fast dungLodeTimerTick (100ms, client.html) owns the centre text: it renders
  // a live ms countdown to the click window and the window's remaining time. It is
  // suppressed while the player is outside the crystal room, and (below, once the
  // sync state is computed) while several active crystals are OUT OF SYNC -- their
  // staggered pillar arrivals would re-anchor the countdown every few ticks, so the
  // out-of-sync marks are the only guidance that makes sense then.
  dungLodeInRoom = !!(activeLode || bigLode);
  let crysOutSync = false;
  dungLodeDbg = '';
  // NOT gated on crys.length: a hidden crystal is exactly the case that blanks the readout.
  // Run whenever the room is known, and let the remembered set decide.
  if (crys.length >= 2 || Object.keys(dungCrysMem).length >= 2) {
    let ctr = dungLodeCenter;
    if (!ctr && crys.length >= 3) {   // derive: vertical movers share x, horizontal movers share y
      const xs = {}, ys = {};
      for (const c of crys) { xs[c.x] = (xs[c.x] || 0) + 1; ys[c.y] = (ys[c.y] || 0) + 1; }
      const cx = Object.keys(xs).find(k => xs[k] >= 2), cy = Object.keys(ys).find(k => ys[k] >= 2);
      if (cx != null && cy != null) ctr = { x: +cx, y: +cy };
    }
    if (ctr) {
      // the out-of-sync notice belongs on the arm's PRESSURE PAD -- where the player
      // actually clicks -- not on the moving crystal. The pad for a
      // colour = the pad loc aligned with that crystal's rail (same row/column as the
      // centre, on the crystal's side), farthest from the centre (the accessible
      // end). No pad in scene -> fall back to the crystal itself.
      // pads = loc 52206 (one per arm at +-5 from the
      // centre 49507/49510; rail 'Crystal' 49465 locs pave the arms between them)
      const cpads = (objs || []).filter(o => (o.id === 52206 || o.id === 54282 || /pressure pad/i.test(o.name || '')) && typeof o.x === 'number' && here(o));
      const padFor = c => {
        let best = null, bd = -1;
        for (const p of cpads) {
          const onArm = (c.x === ctr.x && c.y !== ctr.y)
            ? (p.x === ctr.x && Math.sign(p.y - ctr.y) === Math.sign(c.y - ctr.y))
            : (p.y === ctr.y && Math.sign(p.x - ctr.x) === Math.sign(c.x - ctr.x));
          if (!onArm) continue;
          const dd = Math.max(Math.abs(p.x - ctr.x), Math.abs(p.y - ctr.y));
          if (dd > bd) { bd = dd; best = p; }
        }
        if (best) return best;
        // no pad loc matched this reconcile -> SYNTHESIZE the pad tile: pads sit at
        // exactly +-5 from the centre on each arm (and a
        // tile mark needs no entity -- the notice must never ride the crystal again.
        if (c.x === ctr.x && c.y !== ctr.y) return { x: ctr.x, y: ctr.y + 5 * Math.sign(c.y - ctr.y), plane: c.plane || 0 };
        if (c.y === ctr.y && c.x !== ctr.x) return { x: ctr.x + 5 * Math.sign(c.x - ctr.x), y: ctr.y, plane: c.plane || 0 };
        return null;
      };
      // A crystal runs plate -> 1 -> 2 -> 3 -> 4 -> centre, and while it sits UNDER its
      // plate it is NOT in the scene at all. Remember each colour's arm and distance,
      // and treat "known here but absent now" as AT THE PLATE, the far end of its arm.
      const PLATE_D = 5;                       // pads sit at +-5 from the centre
      const nowT = Date.now(), visible = {};
      for (const c of crys) {
        const nm = DUNG_CRYSTALS[c.id].n;
        visible[nm] = 1;
        dungCrysMem[nm] = { d: Math.max(Math.abs(c.x - ctr.x), Math.abs(c.y - ctr.y)),
                            dx: Math.sign(c.x - ctr.x), dy: Math.sign(c.y - ctr.y),
                            c: DUNG_CRYSTALS[c.id].c, t: nowT };
      }
      const ents = [];
      for (const nm in dungCrysMem) {
        const m = dungCrysMem[nm];
        if (nowT - m.t > 30000) { delete dungCrysMem[nm]; continue; }   // left the room
        ents.push({ nm: nm, hidden: !visible[nm], c: m.c, dx: m.dx, dy: m.dy,
                    d: visible[nm] ? m.d : PLATE_D });
      }
      // the pad on a given arm, by DIRECTION from the centre (works even when the pad
      // loc is not in scene: pads sit at exactly +-5, )
      const padOn = (dx, dy) => {
        for (const p of cpads)
          if (Math.sign(p.x - ctr.x) === dx && Math.sign(p.y - ctr.y) === dy) return p;
        return (dx || dy) ? { x: ctr.x + PLATE_D * dx, y: ctr.y + PLATE_D * dy, plane: 0 } : null;
      };
      dungLodeDbg = ' | crys[v4] ctr ' + ctr.x + ',' + ctr.y + ' pads ' + cpads.length
                  + ' known ' + ents.length + ' hidden ' + ents.filter(e => e.hidden).length;
      if (ents.length >= 2) {
        // DO NOT recompute "who is ahead" every tick. Distance-to-centre is a SAWTOOTH:
        // a crystal that reaches the centre resets to the plate, so its distance jumps
        // 0 -> PLATE_D and the leader INVERTS once per cycle. That made the marks flip
        // between pads every few ticks. A cyclic track has no stable
        // total order, so stop asserting one.
        // Instead show each arm's OWN cycle step, which is a stable per-crystal fact,
        // and only state the comparison when it is unambiguous: every step equal.
        // THE INVARIANT: every crystal advances one step per cycle tick TOGETHER, so
        // the PAIRWISE OFFSETS never change -- only the absolute distances wrap. So
        // work in offsets, which are stable, and never in "who is closest right now",
        // which is not.
        // FIVE positions, 0-4. A crystal NEVER lands on the centre
        // tile -- it gets within 1 and resets -- so the occupied distances are 5 (hidden
        // under the pad) then 4,3,2,1, and the cycle wraps from 4 back to 0. Using 6
        // here would compute hold counts that are one too long.
        const CYC = PLATE_D;                      // 0=under pad, 1..4 = rail, then wrap
        const step = e => PLATE_D - e.d;          // d5->0, d4->1, d3->2, d2->3, d1->4
        // Hold time to bring everything onto `ref`: a crystal k steps ahead of ref must
        // be held for k. Pick the ref that minimises TOTAL holding -- also invariant,
        // so the instruction stays put instead of flipping between pads.
        let ref = null, refCost = 1e9;
        for (const r of ents) {
          let cost = 0;
          for (const e of ents) cost += ((step(e) - step(r)) % CYC + CYC) % CYC;
          if (cost < refCost) { refCost = cost; ref = r; }
        }
        const holdFor = e => ((step(e) - step(ref)) % CYC + CYC) % CYC;
        crysOutSync = refCost > 0;
        for (const e of ents) {
          const k = holdFor(e);
          dungLodeDbg += ' ' + e.nm[0] + (e.hidden ? '(plate)' : '') + '+' + k;
          // Only mark what needs ACTION : a pad reading "leave
          // running" or "IN SYNC" is a box telling you to do nothing, which just
          // crowds the room. No hold needed -> no mark.
          if (!k || marks.length >= 16) continue;
          const at = padOn(e.dx, e.dy);
          if (!at) continue;
          const short = e.nm.replace(/ crystal$/i, '');
          // GOTCHA: the overlay treats a guide label's FIRST LINE as a loc NAME and
          // snaps the box to the nearest live loc so named within 8 tiles -- putting
          // the crystal's name first made the box ride the CRYSTAL no matter what tile
          // anchored. '-Pressure pad' = match the PAD's footprint, hide the title.
          marks.push({ x: at.x, y: at.y, plane: at.plane || 0, rgb: e.c,
                       label: '-Pressure pad\n' + short + '  HOLD ' + k });
        }
      }
    }
  }
  // several active crystals out of sync -> their staggered arrivals would thrash the
  // click countdown; sync phase (all four up) also suppresses it
  dungLodeSuppress = crys.length >= 4 || (crys.length >= 2 && crysOutSync);
  // Tile-flip puzzle (Lights Out): a 5x5 of coloured recesses; flipping one also flips
  // its + neighbours. THREE THEME VARIANTS EXIST PER COLOUR (js5-16 scan),
  // so match on a SET -- hardcoding the two ids one floor happened to use meant the
  // solver silently did nothing on the others (a floor running the
  // 49639/49642 pair):
  //   green  "Imbue,Force"  49638, 49639, 49640
  //   yellow "Imbue,Force"  49641, 49642, 49643
  // The plain no-action "Tile" locs (49546-49548, 49634-49637) are the recess frames and
  // COEXIST at the same cells, so they are excluded: the grid is the 25 coloured ones.
  // The 25 colour locs themselves define the grid (cluster unique xs/ys, like the
  // sliding blocks).
  // EVERY loc named "Green/Yellow tile" carrying Imbue+Force (js5-16 audit,
  // not just the Daemonheim-range three. Extra ids are harmless: the solver only fires
  // when EXACTLY 25 of them sit in one room, so an unrelated tile cannot trigger it.
  const DUNG_LO_GREEN  = { 3873:1, 39859:1, 49638:1, 49639:1, 49640:1, 54065:1 };
  const DUNG_LO_YELLOW = { 3874:1, 39860:1, 49641:1, 49642:1, 49643:1, 54066:1 };
  // Solve GF(2) by first-row enumeration (32 cases cover every solution) for BOTH
  // target colours, keep the fewer-press one, and mark each press tile in-world,
  // coloured by the target colour. Presses are order-independent; the marks
  // recompute from live state as tiles flip, so done presses drop off on their own.
  const ftiles = (objs || []).filter(o => (DUNG_LO_GREEN[o.id] || DUNG_LO_YELLOW[o.id])
                                          && typeof o.x === 'number' && here(o));
  if (ftiles.length === 25) {
    const fxs = Array.from(new Set(ftiles.map(t => t.x))).sort((a, b) => a - b);
    const fys = Array.from(new Set(ftiles.map(t => t.y))).sort((a, b) => a - b);
    const fat = {}; let fok = fxs.length === 5 && fys.length === 5;
    if (fok) for (const t of ftiles) {
      const c = fxs.indexOf(t.x), r = fys.indexOf(t.y);
      if (fat[r * 5 + c]) { fok = false; break; }
      fat[r * 5 + c] = t;
    }
    if (fok) {
      const state = [];
      for (let i = 0; i < 25; i++) state.push(DUNG_LO_YELLOW[fat[i].id] ? 1 : 0);   // 1 = yellow
      const fsolve = want => {   // want[i]=1 -> that cell still needs its colour flipped
        let bestP = null;
        for (let first = 0; first < 32; first++) {
          const press = new Array(25).fill(0), st = want.slice();
          const apply = i => {
            const r = (i / 5) | 0, c = i % 5;
            st[i] ^= 1;
            if (r > 0) st[i - 5] ^= 1;
            if (r < 4) st[i + 5] ^= 1;
            if (c > 0) st[i - 1] ^= 1;
            if (c < 4) st[i + 1] ^= 1;
          };
          for (let c = 0; c < 5; c++) if (first & (1 << c)) { press[c] = 1; apply(c); }
          for (let i = 5; i < 25; i++) if (st[i - 5]) { press[i] = 1; apply(i); }   // chase down
          if (st.every(v => !v)) {
            const n = press.reduce((a, b) => a + b, 0);
            if (!bestP || n < bestP.n) bestP = { press: press, n: n };
          }
        }
        return bestP;
      };
      const toGreen = fsolve(state), toYellow = fsolve(state.map(v => v ^ 1));
      let pickSol = toGreen, frgb = 0x5fd07a, ftgt = 'green';   // green target -> green marks
      if (toYellow && (!toGreen || toYellow.n < toGreen.n)) { pickSol = toYellow; frgb = 0xf0c419; ftgt = 'yellow'; }
      if (pickSol) {
        let flbl = 'Flip each marked tile (any order) -> all ' + ftgt;   // one summary pill, rest plain boxes
        for (let i = 0; i < 25 && marks.length < 16; i++) if (pickSol.press[i]) {
          marks.push({ x: fat[i].x, y: fat[i].y, plane: fat[i].plane || 0, label: flbl, rgb: frgb });
          flbl = '';
        }
      }
    }
  }
  // Ground the 16-pitch room grid on the START ROOM'S FURNITURE, not the smuggler --
  // he wanders (measured at local (6,6), (6,7) and (7,7) on different floors). The
  // start room's loc layout is FIXED but spawns in one of 4 rotations: fit the scene
  // locs against the reference constellation under each rotation; the consensus
  // translation IS the room's SW corner (16-vote consensus vs 2 for every
  // wrong rotation). Reference calibrated by wall-touch on one floor,.
  // NB: ids are theme furniture -- another floor THEME may need its own table.
  // Re-fit EVERY reconcile (instances load the whole floor, so the start-room
  // furniture is always in scene) and OVERWRITE dungFloorSW -- a stale anchor from a
  // previous floor was drawing the player in the wrong room when the floor-reset
  // detection missed the transition.
  if (objs && objs.length) {
    const cand = objs.filter(o => DUNG_SW_REF[o.id] && typeof o.x === 'number');
    if (cand.length >= 6) {
      const S = 13, ROTS = [
        l => [l[0], l[1]], l => [l[1], S - l[0]],
        l => [S - l[0], S - l[1]], l => [S - l[1], l[0]]];
      let bestKey = '', bestN = 0;
      for (const R of ROTS) {
        const votes = {};
        for (const o of cand) {
          const r = R(DUNG_SW_REF[o.id]);
          const key = (o.x - r[0]) + ',' + (o.y - r[1]);
          votes[key] = (votes[key] || 0) + 1;
          if (votes[key] > bestN) { bestN = votes[key]; bestKey = key; }
        }
      }
      if (bestN >= 6) {   // confident fit -> (re)set the anchor; a low-vote fit leaves the current one
        const p = bestKey.split(',').map(Number);
        // ANCHOR MOVED -> EVERY CELL-KEYED CACHE IS NOW WRONG. Map cells are derived from
        // this corner, so a shift silently re-points cached facts at different rooms:
        // dungRoomRes would show a resource in the wrong room, dungKeyDoor would claim
        // the wrong door needs a key. This is the "floor-reset detection missed the
        // transition" case the re-fit exists to correct, so the caches it invalidates
        // must go with it.
        // Only the DERIVED caches are dropped -- the player's own marks are left alone
        // rather than wiped on a fit that might yet prove spurious.
        if (dungFloorSW && (dungFloorSW.x !== p[0] || dungFloorSW.y !== p[1])) {
          dungRoomRes = {}; dungKeyDoor = {};
        }
        dungFloorSW = { x: p[0], y: p[1] };
        dungSaveMarks();   // latch it immediately: the fit is only possible in the start room
      }
    }
  }
  // Statues puzzle: statics 10942-10945 (north 5x5 grids) mark TARGET cells; pushables
  // 10954-10957 (south grids) must be pushed to the SAME cell of their own grid.
  // Room layout (measured, room-local from the SW corner): four 5x5
  // grids at cols 1-5 west / 8-12 east, rows 1-5 south / 8-12 north, walkway cross at
  // 6-7. Pairing 10954->10942, 10955->10943, 10956->10944, 10957->10945.
  dungStatues = null;
  if (dungFloorSW) {
    const gcell = l => { const c = l - (l >= 7 ? 8 : 1); return (c >= 0 && c <= 4) ? c : null; };
    const rows = [];
    for (const pIdStr in DUNG_STATUE_PAIR) {
      const pId = +pIdStr, sId = DUNG_STATUE_PAIR[pIdStr];
      const pn = npcs.find(n => n.id === pId && typeof n.x === 'number' && here(n));
      const sn = npcs.find(n => n.id === sId && typeof n.x === 'number' && here(n));   // same room: scene range sees neighbours
      if (!pn || !sn) continue;
      const pl = dungRoomOf(dungFloorSW, pn.x, pn.y), sl = dungRoomOf(dungFloorSW, sn.x, sn.y);
      const cur = [gcell(pl.lx), gcell(pl.ly)], tgt = [gcell(sl.lx), gcell(sl.ly)];
      if (cur[0] == null || cur[1] == null || tgt[0] == null || tgt[1] == null) continue;   // off-grid (mid-push?)
      // target's WORLD tile: the pushable's room SW + its south-grid origin + target cell
      const swr = { x: dungFloorSW.x + DUNG_ROOM_PITCH * pl.rx, y: dungFloorSW.y + DUNG_ROOM_PITCH * pl.ry };
      rows.push({ id: pId, east: pl.lx >= 7, cur: cur, tgt: tgt,
                  tx: swr.x + (pl.lx >= 7 ? 8 : 1) + tgt[0], ty: swr.y + 1 + tgt[1],
                  done: cur[0] === tgt[0] && cur[1] === tgt[1] });
    }
    if (rows.length) dungStatues = rows;
  }
  // Boss 9919: animation 13338 = the icicle attack -- flash a centre-screen dodge
  // warning while it plays (independent side-channel, never blocks the guide chain)
  {
    const iceBoss = npcs.find(n => n.id === 9919 && typeof n.x === 'number' && here(n));
    if (iceBoss && iceBoss.anim === 13338) {
      if (!dungBossWarnOn) { dungBossWarnOn = true; try { bridge().centerText(myPid(), 'DODGE - icicles!'); } catch (e) {} }
    } else if (dungBossWarnOn) {
      dungBossWarnOn = false;
      try { bridge().centerText(myPid(), ''); } catch (e) {}
    }
  }
  // Ghost KILL room, matched by ID FAMILY 10981-11000 (js5-18: ten
  // combat-tier target/decoy pairs, all one model 21154. Verified pairs: 10987/10988,
  // 10989/10990, 10993/10994 -- the target is ALWAYS the LOWEST Ghost id in the room
  // (3/3 themes). Needs >= 2 ghosts with >= 2 distinct ids before guiding (a lone or
  // uniform pack has nothing to pick between). NAME matching is banned here: the
  // regular Daemonheim ghost MONSTERS (10821-10830) are also attackable "Ghost"s, and
  // one prowling a NEIGHBOURING room both entered the pick and got boxed through the
  // wall by the un-anchored needle.
  // HELP-THE-GHOST ROOM. The ghost (npc 11246 -- a "Ghost" carrying TALK, not Attack,
  // the only one in the game that does, js5-18 scan is the ROOM MARKER, not
  // the objective. Talking to it is not the task (correcting an earlier
  // "Talk to it" guide): the room is RESTORED by repairing its furniture and returning
  // the ring.
  //
  //   Damaged pillar  Repair
  //   Broken pot      Repair
  //   Antique ring    ground item 19879 -> pick up -> Fill it into the Jewellery box
  //
  // All five theme variants of each loc are listed (cache-verified, one per
  // theme family); the ring is item 19879 in every theme -- the only item so named.
  const talkGhost = npcs.find(n => n.id === 11246 && typeof n.x === 'number' && here(n));
  const ghostRoomHere = talkGhost
    || (objs || []).some(o => typeof o.x === 'number' && here(o) &&
         (DUNG_GHOST_PILLAR[o.id] || DUNG_GHOST_POT[o.id] || DUNG_GHOST_BOX[o.id]));
  if (ghostRoomHere) {
    const roomObjs = (objs || []).filter(o => typeof o.x === 'number' && here(o));
    const haveRing = dungInvCount(DUNG_GHOST_RING) > 0;
    // Tiles that already carry a done-marker, per task. Keyed by tile because the two
    // models share one, and a room can hold more than one pot or pillar.
    const doneAt = { pot: {}, pillar: {}, box: {} };
    for (const o of roomObjs) {
      const t = o.x + ',' + o.y;
      if (DUNG_GHOST_POT_DONE[o.id]) doneAt.pot[t] = 1;
      else if (DUNG_GHOST_PILLAR_DONE[o.id]) doneAt.pillar[t] = 1;
      else if (DUNG_GHOST_BOX_DONE[o.id]) doneAt.box[t] = 1;
    }
    // The ring is a CHAIN: on the floor -> in the pack -> into the box. Guide only the
    // step that is actually next, or the room reads as three simultaneous jobs.
    if (haveRing) {
      for (const o of roomObjs) if (DUNG_GHOST_BOX[o.id] && !doneAt.box[o.x + ',' + o.y])
        marks.push({ x: o.x, y: o.y, plane: o.plane || 0, label: 'Fill -- put the ring back', rgb: 0x33cc66 });
    } else {
      for (const g of (dungGroundCache || []))
        if (g && g.id === DUNG_GHOST_RING && typeof g.x === 'number' && here(g))
          marks.push({ x: g.x, y: g.y, plane: g.plane || 0, label: 'Take the antique ring', rgb: 0x33cc66 });
    }
    // Repairs are independent of the ring chain, so they show alongside it.
    for (const o of roomObjs) {
      const t = o.x + ',' + o.y;
      if (DUNG_GHOST_PILLAR[o.id] && !doneAt.pillar[t])
        marks.push({ x: o.x, y: o.y, plane: o.plane || 0, label: 'Repair the pillar' });
      else if (DUNG_GHOST_POT[o.id] && !doneAt.pot[t])
        marks.push({ x: o.x, y: o.y, plane: o.plane || 0, label: 'Repair the pot' });
    }
    // THE COFFIN LAST -- once every task above is marked done, so the room guides in the
    // order it is actually played: restore, then unlock.
    if (!marks.length) {
      // SNAPPED, because the coffin is the only multi-tile object in this room (config
      // footprint 2x2; pot, pillar and box are all 1x1, so their single-tile boxes are
      // already right). A plain tile mark boxed one corner of it.
      // `snap` makes the reader find the live object, take its loc footprint centred on
      // the model's AABB, and draw one perimeter around the whole thing. The label's
      // first line is the NAME it matches on; the leading '-' keeps that name out of the
      // pill so only the instruction shows.
      // DRIVEN BY THE LIVE ACTION, not by id. The id tables still gate the room and act
      // as a fallback, but which STEP is outstanding is read from the object's own
      // actions, so a theme whose ids are not catalogued still guides correctly.
      //
      // THE UNLOCK VARIANT NEVER GOES AWAY. After blessing, the tile still reads
      // "Coffin 40181 - Unlock" alongside an ACTIONLESS "Coffin 55452".
      // So Unlock-present cannot mean "still locked", and the actionless coffin is the
      // only sound completion signal -- it is absent before unlocking, absent after
      // unlocking, and appears only once blessed.
      const isCoffin = o => o.name === 'Coffin'
                         || DUNG_GHOST_COFFIN[o.id] || DUNG_GHOST_COFFIN_BLESS[o.id];
      const hasAct = (o, a) => (o.actions || []).some(x => x === a);
      const coffinDone = {};
      for (const o of roomObjs)
        if (isCoffin(o) && !(o.actions || []).length) coffinDone[o.x + ',' + o.y] = 1;
      const pend = f => roomObjs.filter(o => isCoffin(o) && f(o) && !coffinDone[o.x + ',' + o.y]);
      // Bless supersedes Unlock: both sit on the tile once it is open, and blessing is
      // then what is left.
      const bless = pend(o => hasAct(o, 'Bless-remains'));
      const coffins = bless.length ? bless : pend(o => hasAct(o, 'Unlock'));
      for (const o of coffins)
        marks.push({ x: o.x, y: o.y, plane: o.plane || 0,
                     label: '-Coffin' + String.fromCharCode(10)
                          + (bless.length ? 'Bless the remains' : 'Unlock the coffin'),
                     snap: true, rgb: 0xffcc33 });
    }
    dungGuideTiles(marks);
    // The ghost is only worth boxing while nothing else is left to do -- otherwise it
    // draws the eye away from the objects that ARE the task.
    dungHighlightNpc(marks.length ? 0 : (talkGhost ? 11246 : 0),
                     marks.length ? '' : 'Nothing left to restore');
    return;
  }
  // The kill room's ghosts, by the puzzle id family (see the comment block above).
  const ghosts = npcs.filter(n => n.id >= 10981 && n.id <= 11000 && typeof n.x === 'number' && here(n));
  const ghostNeedle = g => '#' + g.id + '|Kill this ghost|' + g.x + ';' + g.y + ';1';
  // Needles carry the ghost's TILE anchor: '#id' alone lets the reader box any same-id ghost
  // within scene range, including one in the next room.
  // A LONE ghost has nothing to disambiguate, so it IS the target.
  if (ghosts.length === 1) {
    dungGuideTiles(marks);
    dungHighlightList([ghostNeedle(ghosts[0])]);
    return;
  }
  if (ghosts.length >= 2) {
    const gids = Array.from(new Set(ghosts.map(n => n.id))).sort((a, b) => a - b);
    if (gids.length >= 2) {
      const tgt = ghosts.filter(n => n.id === gids[0]).sort((a, b) => a.x - b.x || a.y - b.y)[0];
      dungGuideTiles(marks);
      dungHighlightList([ghostNeedle(tgt)]);
      return;
    }
  }
  // Pondskater puzzle: 12091/12092/12093 are decoys; 12089 carries the key.
  const skater = npcs.find(n => n.id === 12089 && typeof n.x === 'number' && here(n));
  if (skater) { dungGuideTiles(marks); dungHighlightNpc(12089, 'Has the key'); return; }
  // Read-and-arm statues room: 3 REFERENCE statues (one per row) tell the weapon;
  // arm the SAME-Y armable with the combat-triangle counter. Reference trios share a
  // weapon model in the cache (tail model): DC02 refs -> staff (11027 anchored live),
  // DB0F refs = ranged -> sword (11030 anchored), DB2F refs = mage -> bow (by
  // elimination). All three armables box at once, each labelled with its weapon.
  const DUNG_STATUE_READ = {
    11020: 'staff', 11027: 'staff', 11028: 'staff', 11029: 'staff',
    11021: 'sword', 11022: 'sword', 11023: 'sword', 11030: 'sword', 11031: 'sword', 11032: 'sword',
    11024: 'bow', 11025: 'bow', 11026: 'bow', 11033: 'bow', 11034: 'bow', 11035: 'bow',
    // THEME SIBLINGS (js5-18 models: same statue bodies [55837,55842] and
    // the SAME weapon tail models as the validated trios above -- 56322 staff,
    // 56079 sword, 56111 bow -- so the mapping carries over 1:1.
    12108: 'staff', 12109: 'sword', 12110: 'bow', 12111: 'staff', 12112: 'sword', 12113: 'bow',
    13051: 'staff', 13052: 'sword', 13053: 'bow', 13054: 'staff', 13055: 'sword', 13056: 'bow',
  };
  // "Arm" action families (js5-18): the original two, plus the theme siblings found
  // when a 12095 room went unguided -- read-room single armables 12106 and
  // 13049 (same body models as 11012-14), and the simple-room trios 12094-96 and
  // 13057-59 (param-74 chains to their armed forms, exactly like 11036-44).
  const DUNG_ARM_IDS = n => (n >= 11012 && n <= 11014) || (n >= 11036 && n <= 11044)
    || n === 12106 || n === 13049 || (n >= 12094 && n <= 12096) || (n >= 13057 && n <= 13059);
  const armables = npcs.filter(n => DUNG_ARM_IDS(n.id) && typeof n.x === 'number' && here(n));
  // An ARMED statue takes a weapon-holding id IDENTICAL to a reference statue, so its
  // own tile would then be read as a reference and mis-pair the remaining armables.
  // Remember every tile that has held an armable this visit and exclude those from the
  // reference pool; reset when the room is solved / left (no armables present).
  if (!armables.length) dungArmableTiles = {};
  for (const a of armables) dungArmableTiles[a.x + ',' + a.y] = 1;
  const readRefs = npcs.filter(n => DUNG_STATUE_READ[n.id] && typeof n.x === 'number' && here(n) && !dungArmableTiles[n.x + ',' + n.y]);
  if (readRefs.length && armables.length) {
    // all armables can share ONE npc id (three 11012s), so each needle carries
    // its statue's TILE anchor ("x;y" -- the csv-safe form) to box THAT instance's
    // model AABB with the label above it, exactly like the quest NPC boxes.
    const needles = [];
    for (const a of armables) {
      // rooms rotate: refs pair with armables along EITHER axis (same y in one
      // orientation, same x in the rotated one) -- exact match first, then +-1
      const r = readRefs.find(rr => rr.y === a.y || rr.x === a.x)
             || readRefs.find(rr => Math.abs(rr.y - a.y) <= 1 || Math.abs(rr.x - a.x) <= 1);
      if (r) needles.push('#' + a.id + '|Arm with ' + DUNG_STATUE_READ[r.id] + '|' + a.x + ';' + a.y);
    }
    if (needles.length) { dungGuideTiles(marks); dungHighlightList(needles); return; }
  }
  // Simple arm-statues room (no reference statues): cache says 11036-11044 are the
  // armable "Statue" variants (the only ids with an Arm action; param 74 = the armed
  // id each becomes). The required weapon is FIXED per pose id (11042 -> 11048 staff,
  // ); fill in the others as observed live.
  // CACHE-VERIFIED (js5-18 param 74 = the ARMED form each statue becomes;
  // the raw param reads one byte early, so the id is value >> 8):
  //   11036-38 -> 11051-53, 11039-41 -> 11045-47, 11042-44 -> 11048-50.
  // Armed anchors are known -- 11051 sword, 11045 bow, 11048 staff -- so each
  // triple's weapon follows, and both hardcoded entries (11036 sword,
  // 11042 staff) fall out of it unchanged. 11039 had no entry, so the box just said
  // "Arm this statue" when the answer was a bow ; confirmed
  // independently by the room itself, where 11048 (magic) + 11051 (melee) were already
  // armed, leaving Ranged as the missing style.
  const DUNG_ARM_WEAPON = {
    11036: 'sword', 11037: 'sword', 11038: 'sword',   // -> 11051-53  melee
    11039: 'bow',   11040: 'bow',   11041: 'bow',     // -> 11045-47  ranged
    11042: 'staff', 11043: 'staff', 11044: 'staff',   // -> 11048-50  magic
    // 12xxx/13xxx themes, PATTERN-FILLED (every theme maps
    // its armable trio onto its armed-form block by the SAME permutation -- 1st
    // armable -> 3rd form (sword), 2nd -> 1st form (bow), 3rd -> 2nd form (staff);
    // that is exactly 11036-44's shape, the param-74 chains repeat it (12094 -> 12099,
    // 12095 -> 12097, 12096 -> 12098; 13057 -> 13062, 13058 -> 13060, 13059 -> 13061),
    // and the one live validation (12095 = bow) lands where the pattern predicts.
    12094: 'sword', 12095: 'bow', 12096: 'staff',
    13057: 'sword', 13058: 'bow', 13059: 'staff',
  };
  const DUNG_ARM_STYLE = { sword: 'Melee', bow: 'Ranged', staff: 'Magic' };
  // The other armable family (11012-11014) carries NO param 74, so it has no per-id
  // answer. Fall back to the room itself: the ARMED statues present show two of the
  // three styles and the unarmed one takes the missing one. Armed families (cache):
  // 11045-47 bow, 11048-50 staff, 11051-53 sword. Only used when exactly one style is
  // missing, so it never guesses.
  // 12097-99 / 13060-62 follow the 11045/48/51 block order (bow, staff, sword) --
  // same pattern fill as DUNG_ARM_WEAPON above; 12097 = bow is the  one.
  const DUNG_ARMED_STYLE = id => (id >= 11045 && id <= 11047) ? 'bow'
                               : (id >= 11048 && id <= 11050) ? 'staff'
                               : (id >= 11051 && id <= 11053) ? 'sword'
                               : (id === 12097 || id === 13060) ? 'bow'
                               : (id === 12098 || id === 13061) ? 'staff'
                               : (id === 12099 || id === 13062) ? 'sword'
                               : null;
  const armStatue = npcs.find(n => DUNG_ARM_IDS(n.id) && typeof n.x === 'number' && here(n));
  if (armStatue) {
    let w = DUNG_ARM_WEAPON[armStatue.id];
    if (!w) {
      const have = new Set();
      for (const n of npcs)
        if (typeof n.x === 'number' && here(n)) { const st = DUNG_ARMED_STYLE(n.id); if (st) have.add(st); }
      const missing = ['sword', 'bow', 'staff'].filter(x => !have.has(x));
      if (have.size >= 2 && missing.length === 1) w = missing[0];
    }
    dungGuideTiles(marks);
    dungHighlightNpc(armStatue.id, w ? 'Arm with a ' + w + ' (' + DUNG_ARM_STYLE[w] + ')' : 'Arm this statue');
    return;
  }
  // Emotes puzzle: statue NPCs (10966) perform emotes; each player stands on a
  // PRESSURE PAD (loc 52206) and copies ONLY the statue paired with THEIR pad -- the
  // statue NEAREST that pad (the pairing lines run straight out from each pad to the
  // statue on its row/column, which the nearest-statue rule reproduces under any
  // room rotation). BROKEN statues (10972) mark the pads the current party size
  // doesn't need, so pairing runs against working AND broken statues -- a pad whose
  // nearest statue is broken never steals a farther working one, it's just "not
  // needed". THREE THEMES x THREE STATES, not one flat block (js5-18 scan + live
  // captures : ACTIVE 10966-10968 perform the emote; DONE 10969-10971 are the same
  // statues at +3 once their emote is copied / the room completes (this floor's
  // 10967 stood at the SAME tiles as 10970 after solving -- treating DONE as active
  // kept the watch box + anim label up after the puzzle, ); BROKEN 10972-10974 mark
  // pads the party size doesn't need. DONE and BROKEN both join the pairing set (a
  // pad whose nearest statue is done/broken needs nobody), only ACTIVE is ever
  // watched or numbered. No active statue left -> the block does not claim the room
  // and the function tail clears the channels.
  // Theme families from js5-18 (blocks anchored by the "Broken statue" ids 10972-74 /
  // 12116 / 12962). The 121xx and 129xx themes ship only TWO non-broken ids each, so all
  // active instances share one id (live-confirmed: an unsolved room shows two 12114s);
  // the second id is the done re-id by positional analogy with the validated 109xx block.
  const DUNG_EMOTE_STATUE = { 10966: 1, 10967: 1, 10968: 1, 12114: 1, 12960: 1 };
  const DUNG_EMOTE_DONE   = { 10969: 1, 10970: 1, 10971: 1, 12115: 1, 12961: 1 };
  const DUNG_EMOTE_BROKEN = { 10972: 1, 10973: 1, 10974: 1, 12116: 1, 12962: 1 };
  const emoteStatues = npcs.filter(n =>
    (DUNG_EMOTE_STATUE[n.id] || DUNG_EMOTE_DONE[n.id] || DUNG_EMOTE_BROKEN[n.id])
    && typeof n.x === 'number' && here(n));
  if (emoteStatues.some(n => DUNG_EMOTE_STATUE[n.id])) {
    // Every "Pressure pad" loc the cache ships for these rooms: 52206 (model 54793, the
    // original theme), 54282 (59262, the 121xx theme), 35232/97487 (61706). Without the
    // theme's pad id the watch fell back to "first statue" and boxed the wrong one.
    const DUNG_EMOTE_PADS = { 52206: 1, 54282: 1, 35232: 1, 97487: 1 };
    const pads = (objs || []).filter(o => DUNG_EMOTE_PADS[o.id] && typeof o.x === 'number' && here(o));
    const nearest = (x, y) => {
      let s = null, sd = Infinity;
      for (const st of emoteStatues) {
        const dd = (st.x - x) * (st.x - x) + (st.y - y) * (st.y - y);
        if (dd < sd) { sd = dd; s = st; }
      }
      return s;
    };
    const myPad = (dungSelfPos && pads.length) ? pads.find(p => p.x === dungSelfPos.x && p.y === dungSelfPos.y) : null;
    const watch = pads.length ? (myPad ? nearest(myPad.x, myPad.y) : null)
                              : emoteStatues.find(n => DUNG_EMOTE_STATUE[n.id]);
    if (watch && DUNG_EMOTE_STATUE[watch.id]) {
      const wk = watch.x + ',' + watch.y;
      if (dungEmoteWatch !== wk) { dungEmoteWatch = wk; dungEmoteLast = -1; }   // new statue -> drop the stale latch
      if (typeof watch.anim === 'number' && watch.anim >= 0) dungEmoteLast = watch.anim;
      const opt = DUNG_EMOTE_ANIM[dungEmoteLast];
      if (opt) { try { PLUGIN_API['overlay.highlightOption'].run([opt], myPid()); } catch (e) {} }
      const prog = (dungMonoCharge != null && dungMonoCharge >= 0 && dungMonoCharge % 67 === 0) ? (dungMonoCharge / 67) : null;
      const lbl = (prog != null ? prog + '/3 done' : '')
        + (dungEmoteLast >= 0 ? (prog != null ? ' - ' : '') + (opt ? 'do: ' + opt : 'anim ' + dungEmoteLast) : '');
      // tile-hinted needle so the box lands on this statue, not another one sharing the id
      dungGuideTiles(marks);
      dungHighlightList(['#' + watch.id + '|' + (lbl || 'Copy this statue') + '|' + watch.x + ';' + watch.y + ';1']);
      return;
    }
    // Not standing on a pad: mark only the pads that MATTER, numbered. A pad paired to
    // a broken statue (10972) needs nobody, and labelling those just crowded the room
    // -- its absence says "not this one" well enough. The label is the bare number so
    // positions can be called out.
    let padN = 0;
    for (const p of pads) {
      if (marks.length >= 16) break;
      const st = nearest(p.x, p.y);
      if (!st || !DUNG_EMOTE_STATUE[st.id]) continue;
      padN++;
      marks.push({ x: p.x, y: p.y, plane: p.plane || 0, label: String(padN), rgb: 0x5fd07a });
    }
    dungGuideTiles(marks); dungHighlightList([]); return;
  }
  // POLTERGEIST ROOM (cache scan +  mapping). NPC 11245 is
  // the only "Poltergeist" in js5-18 -- no theme variants to chase. Loc families:
  //   Sarcophagus 54078-54081  Read/Open -> NAMES the herb the censers want
  //               54082-54085  (no action) = already read/opened
  //   Herb patch  54074-54076  Harvest   -> the herb is picked from the CHAT options
  //   Censer      54094-54097  Add herb  -> still empty
  //               54098-54101  Light     -> filled, needs lighting
  //               54102-54105  (no action) = lit, done
  // NO id->herb TABLE. The inscription is the ONLY source; with no inscription
  // read yet the room says READ THE SARCOPHAGUS, which is the honest next step.
  // Interface-720 option order, and also the vocabulary the INSCRIPTION is matched
  // against. Longest-first is not needed: no herb name contains another.
  const DUNG_POLT_HERBS = ['Corianger', 'Explosemary', 'Parslay',
                          'Cardamaim', 'Papreaper', 'Slaughtercress'];
  // The herb picker is the "SELECT AN OPTION" interface GROUP 720, not the chat box, so
  // overlay.highlightOption -- which only reads the dialogue -- never saw it. Its option rows are TEXT comps on a stride of 3 (tree: 27 =
  // "4. Cardamaim", 30 = "5. Papreaper", 33 = "6. Slaughtercress" -> comp 15 + 3n for
  // option n), and interfaceComps already returns ABSOLUTE rects for this group (its
  // panel origin is in kPanelOrigins), so the box is drawn straight with uiHighlight.
  // The six harvested herbs carry a "#Consecrate" item action and sit at 19653-19658 in
  // the SAME order as the interface-720 options (js5-19 scan; consecrating
  // one yields "Consecrated herb" 19659, which is what a censer actually wants.
  const DUNG_HERB_ITEM = { Corianger: 19653, Explosemary: 19654, Parslay: 19655,
                           Cardamaim: 19656, Papreaper: 19657, Slaughtercress: 19658 };
  const DUNG_HERB_DONE_ITEM = 19659;
  // Loc 50114 (a plain farming patch) takes the herb patch's place at the SAME tiles
  // once every herb is picked ( -- by then the 54074-76 patch
  // is gone, so "no patch in the room" already means spent; kept here for recognition.
  const DUNG_HERB_SPENT_LOC = 50114;
  const DUNG_HERB_GROUP = 720;
  const DUNG_HERB_COMPS = [18, 21, 24, 27, 30, 33].join(',');   // options 1..6
  // ALWAYS clear when not drawing -- the interface being CLOSED is the common
  // case and the early return left the last box stranded on screen.
  // uiHighlight is a SHARED single-rect channel (quest guides use it too), so only ever
  // clear a box this panel put up: dungHerbHlLast tracks that.
  function dungHighlightHerb(herb) {
    let drew = false;
    try {
      if (herb) {
        const d = JSON.parse(bridge().interfaceComps(myPid(), DUNG_HERB_GROUP, DUNG_HERB_COMPS) || '{}');
        if (d && d.open && d.hasAbs && Array.isArray(d.comps)) {
          const want = herb.toLowerCase();
          for (const c of d.comps) {
            const t = (c.text || '').toLowerCase();
            if (t && c.w > 0 && t.indexOf(want) >= 0) {
              const rect = c.x + ',' + c.y + ',' + c.w + ',' + c.h;
              if (rect !== dungHerbHlLast) { rtxData.sync('overlay.uiHighlight', c.x, c.y, c.w, c.h); dungHerbHlLast = rect; }
              dungHerbHlOwner = 'herb';
              drew = true;
              break;
            }
          }
        }
      }
      if (!drew) dungHerbClear('herb');
    } catch (e) {}
    return drew;
  }
  // Generic option-row matcher, shared with the hoardstalker take-list. Matches on TEXT
  // and NEVER on the option NUMBER: these lists paginate, so the number shifts between
  // pages. Does NOT clear on miss -- the caller decides, because a miss can legitimately
  // mean "try the next page" rather than "nothing to draw".
  function dungHighlightOptionText(group, comps, want, owner) {
    try {
      if (!want) return false;
      const d = JSON.parse(bridge().interfaceComps(myPid(), group, comps) || '{}');
      if (!d || !d.open || !d.hasAbs || !Array.isArray(d.comps)) return false;
      const w = String(want).toLowerCase();
      for (const c of d.comps) {
        const t = (c.text || '').toLowerCase();
        if (t && c.w > 0 && t.indexOf(w) >= 0) {
          const rect = c.x + ',' + c.y + ',' + c.w + ',' + c.h;
          if (rect !== dungHerbHlLast) { rtxData.sync('overlay.uiHighlight', c.x, c.y, c.w, c.h); dungHerbHlLast = rect; }
          dungHerbHlOwner = owner || 'opt';
          return true;
        }
      }
    } catch (e) {}
    return false;
  }
  {
    const inRoom = o => typeof o.x === 'number' && here(o);
    const polt      = npcs.find(n => n.id === 11245 && inRoom(n));
    // Sarcophagi coexist per tile exactly like the censers: an OPENED one is listed as
    // 54079 "Read, Open" AND 54083 (no action) at the same coordinates.
    //. Group by tile, keep the most advanced state, and only carry forward
    // the ones still to interact with -- so a finished room stops marking anything.
    const sarcAt = {};                     // tile -> {st: 0 still shut | 1 opened, o}
    for (const o of (objs || [])) {
      if (!inRoom(o)) continue;
      const st = (o.id >= 54078 && o.id <= 54081) ? 0 : (o.id >= 54082 && o.id <= 54085) ? 1 : -1;
      if (st < 0) continue;
      const k = o.x + ',' + o.y;
      if (sarcAt[k] === undefined || st > sarcAt[k].st) sarcAt[k] = { st: st, o: o };
    }
    const sarcs = [];
    for (const k in sarcAt) if (sarcAt[k].st === 0) sarcs.push(sarcAt[k].o);
    // Same coexistence for the patch: loc 50114 (spent) shows up at the patch's own
    // tiles once every herb is picked, so a patch tile carrying it is finished.
    const spentTiles = {};
    for (const o of (objs || [])) if (o.id === DUNG_HERB_SPENT_LOC && inRoom(o)) spentTiles[o.x + ',' + o.y] = 1;
    const patches   = (objs || []).filter(o => o.id >= 54074 && o.id <= 54076 && inRoom(o)
                                               && !spentTiles[o.x + ',' + o.y]);
    // A censer's STATES COEXIST in the scene at one tile: once its herb is in, the same
    // object is listed as 54095 "Add herb" AND 54099 "Light" at identical coordinates
    // ( Filtering by id alone therefore kept telling you to
    // add a herb to a censer that already had one. Group by TILE, keep the most
    // ADVANCED state, and let that decide what the censer still needs.
    const censAt = {};                     // tile -> {st: 0 empty | 1 filled | 2 lit, o}
    for (const o of (objs || [])) {
      if (!inRoom(o)) continue;
      const st = (o.id >= 54094 && o.id <= 54097) ? 0
               : (o.id >= 54098 && o.id <= 54101) ? 1
               : (o.id >= 54102 && o.id <= 54105) ? 2 : -1;
      if (st < 0) continue;
      const k = o.x + ',' + o.y;
      if (censAt[k] === undefined || st > censAt[k].st) censAt[k] = { st: st, o: o };
    }
    const censEmpty = [], censLight = [];
    for (const k in censAt) {
      if (censAt[k].st === 0) censEmpty.push(censAt[k].o);
      else if (censAt[k].st === 1) censLight.push(censAt[k].o);
    }
    if (polt || sarcs.length || censEmpty.length || censLight.length) {
      // THE HERB IS NAMED IN THE SARCOPHAGUS INSCRIPTION, NOT BY ITS LOC ID.
      // Live proof : "Here lies Leif, posthumously honoured with the
      // discovery of cardamaim." while the guide was saying Slaughtercress, because the
      // id->herb table only ever had 54079 confirmed and any other sarcophagus fell
      // through to the last match. Read the inscription the way the hoardstalker reads
      // its riddle, and let the id table be a fallback only.
      let herb = dungPoltHerb;
      {
        const t = dungDlgText();
        const hit = t && DUNG_POLT_HERBS.find(h => t.indexOf(h.toLowerCase()) >= 0);
        if (hit) { dungPoltHerb = herb = hit; }
      }
      // These locs are MULTI-TILE (cache: herb patch 2x1, sarcophagus 2x2, censer 1x1),
      // so a bare tile mark boxes only the anchor corner. Prefixing the label with
      // "-<loc name>" makes the overlay snap the box to that loc's real model AABB and
      // hide the name line, so the box wraps the whole object and only the step text
      // shows (the patch is 2x1).
      // Filling and lighting run in PARALLEL, not as two phases: a censer can be lit as
      // soon as ITS OWN herb is in, without waiting for the other three.
      //. So every filled censer always carries its Light mark, whatever is
      // still outstanding elsewhere in the room.
      for (const c2 of censLight)
        if (marks.length < 16) marks.push({ x: c2.x, y: c2.y, plane: c2.plane || 0, label: '-Censer\nLight', rgb: 0xe8b34b });
      if (censEmpty.length) {
        // Three sub-steps for what is still empty, chosen by what is in the backpack:
        //   raw herb held    -> CONSECRATE it (the item's own action)
        //   consecrated held -> put it in a censer
        //   neither          -> go harvest, and box the herb row in interface 720
        const herbItem = herb ? DUNG_HERB_ITEM[herb] : 0;
        const rawN = herbItem ? dungInvCount(herbItem) : 0;
        const doneN = dungInvCount(DUNG_HERB_DONE_ITEM);
        if (rawN > 0) {
          dungHerbClear('herb');
          dungHighlightInvItem(herbItem, 'Consecrate', 'polt');
        } else if (doneN > 0) {
          dungHerbClear('herb'); dungInvClear();
          for (const c2 of censEmpty)
            if (marks.length < 16) marks.push({ x: c2.x, y: c2.y, plane: c2.plane || 0, label: '-Censer\nAdd herb', rgb: 0xe8b34b });
        } else {
          dungInvClear();
          // Only box the patch once the herb is KNOWN. "Harvest the herb" is a step you
          // cannot act on correctly yet -- picking the wrong one wastes the patch -- and
          // showing it alongside the sarcophagus prompt just buried the step that
          // actually unblocks you. Same rule as the crystal pads: mark only what is
          // actionable.
          if (herb) for (const p2 of patches)
            if (marks.length < 16) marks.push({ x: p2.x, y: p2.y, plane: p2.plane || 0,
              label: '-Herb patch\nHarvest ' + herb + ' x' + censEmpty.length, rgb: 0x5fd07a });
          if (herb) dungHighlightHerb(herb);   // interface 720, not the chat box
          // herb unknown -> read a sarcophagus; that is the step that names it
          if (!herb) for (const sc of sarcs)
            if (marks.length < 16) marks.push({ x: sc.x, y: sc.y, plane: sc.plane || 0, label: '-Sarcophagus\nRead: it names the herb', rgb: 0x5ab8f0 });
        }
      } else {
        dungHerbClear('herb'); dungInvClear();   // nothing left to gather
        // Every censer LIT (state 2 = the 54102-54105 family, e.g. 54103 seen live) ->
        // the sarcophagus is the payoff: Open it.
        const censKeys = Object.keys(censAt);
        if (censKeys.length && censKeys.every(k => censAt[k].st === 2))
          for (const sc of sarcs)
            if (marks.length < 16) marks.push({ x: sc.x, y: sc.y, plane: sc.plane || 0, label: '-Sarcophagus\nOpen it', rgb: 0x5fd07a });
      }
      // The step text belongs on the OBJECT you click, once. Repeating it on the
      // poltergeist box just doubled the same sentence on screen,
      // so the NPC carries no label while the room is about the censers.
      dungGuideTiles(marks);
      dungHighlightNpc(0);
      return;
    }
  }
  // PEDESTAL ROOM (js5-16 scan); every family runs in FOURS):
  //   Pedestal 54110-54113 idle | 54114-54117 ACTIVATED (2x2, no action either way;
  //            solved capture shows 54111 and 54115 live together on the same tile)
  //   Pillar   54118-54121 base | 54122-54125 "Fix" (broken) | 54126-54129 (no action)
  //   Rubble   54130-54133 + 54138-54141 "Mine" (blocking) | 54134-54137 + 54142-54145 (cleared)
  // A broken pillar is listed AT THE SAME TILE as its base (capture: 54119 and
  // 54123 both live at 12330,666), exactly like the poltergeist censers -- so decide
  // per TILE and let a "done" id at that tile suppress the actionable one.
  // CONFIRMED by the solved-room capture, and the two families behave DIFFERENTLY:
  //   pillars  -> the Fix id DISAPPEARS and 54127 appears;
  //   rubble   -> 54131 STAYS AND KEEPS ITS "Mine" ACTION while 54135 appears.
  // So the done-tile suppression is load-bearing: without it the guide would go on
  // telling you to mine rubble that is already cleared.
  {
    const inR = o => typeof o.x === 'number' && here(o);
    const isPed      = o => o.id >= 54110 && o.id <= 54117;
    const isFix      = o => o.id >= 54122 && o.id <= 54125;
    const isFixDone  = o => o.id >= 54126 && o.id <= 54129;
    const isMine     = o => (o.id >= 54130 && o.id <= 54133) || (o.id >= 54138 && o.id <= 54141);
    const isMineDone = o => (o.id >= 54134 && o.id <= 54137) || (o.id >= 54142 && o.id <= 54145);
    // PER FAMILY, not one shared map. A single doneTile meant a CLEARED-RUBBLE marker
    // suppressed a broken PILLAR sharing that tile, and a FIXED-PILLAR marker suppressed
    // uncleared rubble -- the two families genuinely do share tiles in this room, and the
    // marker only ever speaks for its own kind.
    const fixDone = {}, mineDone = {};
    for (const o of (objs || [])) {
      if (!inR(o)) continue;
      if (isFixDone(o)) fixDone[o.x + ',' + o.y] = 1;
      else if (isMineDone(o)) mineDone[o.x + ',' + o.y] = 1;
    }
    // the live ACTION is the game's own statement of what is still to do, so require it
    const act = (o, a) => (o.actions || []).some(x => x === a);
    //...but an action is NOT enough here. A cleared loc can stay in the worldview still
    // advertising "Mine" while nothing is drawn ("they are actually
    // showing in scene with the action... there must be something saying they arent
    // actually visible"). `vis` is the reader's report of whether the object has a live
    // rendered model, derived from a non-degenerate model AABB -- the same test the guide
    // code uses before it will draw a 3D box at all.
    // Tri-state on purpose: undefined means an older reader that does not publish the
    // field, and that must read as visible rather than silently blanking every mark.
    const shown = o => o.vis !== false;
    const fixes = (objs || []).filter(o => isFix(o) && inR(o) && !fixDone[o.x + ',' + o.y] && act(o, 'Fix') && shown(o));
    const mines = (objs || []).filter(o => isMine(o) && inR(o) && !mineDone[o.x + ',' + o.y] && act(o, 'Mine') && shown(o));
    const peds  = (objs || []).filter(o => isPed(o) && inR(o));
    if (peds.length || fixes.length || mines.length) {
      // rubble first: it is what blocks the way to the pillars
      for (const o of mines)
        if (marks.length < 16) marks.push({ x: o.x, y: o.y, plane: o.plane || 0, label: '-Rubble\nMine to clear', rgb: 0xe8b34b });
      for (const o of fixes)
        if (marks.length < 16) marks.push({ x: o.x, y: o.y, plane: o.plane || 0, label: '-Pillar\nFix', rgb: 0x5fd07a });
      dungGuideTiles(marks);
      dungHighlightNpc(0);
      return;
    }
  }
  // SEEKER SENTINEL ROOM -- GAZE LINE, CALIBRATION BUILD.
  // Sentinel npc 10941 rotates on the spot; four "Seeker spawn" (Subdue) patrol the
  // corners. The sentinel's facing comes from the reader as `face`, a heading in
  // degrees derived from its yaw quaternion (see Reader.cpp).
  //
  // NO OFFSET. The +45 that two calibration readings seemed to demand was not a
  // coordinate offset at all -- it was the reader sampling the LAGGING quaternion
  // (sec+0x1D0). Reader.cpp now reads the leading one (sec+0x1E0), which is the live
  // facing, so the heading is already correct.
  const DUNG_SENTINEL = { 10941: 1, 25128: 1 };   // both npcs named 'Seeker sentinel'
  const DUNG_GAZE_OFFSET = 0;
  // He blocks a whole SECTION of the room, not a straight line, so
  // the gaze is a CONE. Half-angle is a calibration constant like the offset was: 45
  // gives a 90-degree quadrant, which matches "that entire section" for a room split
  // into four. Narrow or widen it once the drawn wedge is compared against the game.
  // EIGHT EQUAL SECTIONS, so the wedge is 45 degrees TOTAL:
  // half-angle 22.5. The previous 45 gave a 90-degree quadrant, and a 90-degree wedge
  // measured over Chebyshev rings is literally a SQUARE on a diagonal facing -- which is
  // the square that showed up on SE.
  // Room is 14x14 with the sentinel at its centre, so reach is ~7 tiles to the wall.
  const DUNG_GAZE_LEN  = 9;   // walked from the CENTRE, so the body clip eats the
                              // first tile or two; 9 puts ~8 usable tiles on each
                              // line, which is the most 16 marks allows for two.
  const DUNG_GAZE_HALF = 22.5;
  // Footprint comes from the npc feed (`size`, cache opcode 12) -- never assumed. The
  // cache decoder keeps opcode 12 and the
  // reader publishes it. Origin = the model CENTRE, and the ray starts clear of the
  // model so it does not draw over him ("starting at 2 tiles so it
  // looks cleaner").
  // The four "Seeker spawn" patrollers: subdue each one WHILE IT FACES AWAY and the
  // sentinel's gaze is elsewhere. A subdued spawn drops its Subdue
  // action (same tell as the coloured ferrets losing Scare). Matched by name as well
  // as id so a theme sibling (the 25128-sentinel floors) still matches.
  const DUNG_SEEKER_SPAWN = { 10933: 1, 10935: 1, 10938: 1, 10939: 1 };
  // Turn cadences, in ms (game tick 600ms): the sentinel re-aims every 5 ticks; a spawn
  // patrols in 4-TICK LEGS and changes direction at the end of EVERY leg -- the
  // straight-line model (turns only at the far extremes, every 8 ticks) ran a constant
  // one-leg (~2.7s) late on every turnaround. The stamp period is a
  // fallback only; the real countdown walks to a LEARNED turn tile (see turnEta).
  const DUNG_SENT_TURN_MS = 5 * 600, DUNG_SPAWN_TURN_MS = 4 * 600;
  {
    const sent = npcs.filter(n => DUNG_SENTINEL[n.id] && typeof n.x === 'number' && here(n));
    const spawns = npcs.filter(n =>
      (DUNG_SEEKER_SPAWN[n.id] || n.name === 'Seeker spawn') && typeof n.x === 'number' && here(n));
    if (!sent.length && !spawns.length) dungSeekerMem = {};   // not in the room: drop the phases
    if (sent.length || spawns.length) {
      const now = Date.now();
      const norm = d => ((d % 360) + 360) % 360;
      const angdiff = (a, b) => { const d = Math.abs(norm(a) - norm(b)); return d > 180 ? 360 - d : d; };
      // Bearing in the same convention line() draws with: 0 = north (+y), 90 = east.
      const bearing = (fx, fy, tx, ty) => norm(Math.atan2(tx - fx, ty - fy) * 180 / Math.PI);
      // Track each npc's facing; a change stamps the turn time for the countdowns.
      // Facing falls back to the walk vector when the reader can't give `face` -- a
      // pacing spawn's movement IS its facing.
      const faceOf = n => {
        const m = dungSeekerMem[n.uid] || (dungSeekerMem[n.uid] = { face: null, at: 0, x: n.x, y: n.y });
        let f = (typeof n.face === 'number' && n.face >= 0) ? norm(n.face) : null;
        if (f === null && (m.x !== n.x || m.y !== n.y)) f = bearing(m.x, m.y, n.x, n.y);
        // Patrol learning (spawns), geometry-free: measure the walk SPEED from
        // observed movement (never assume tiles/tick), and record every tile where
        // the walk direction bent by 45+ degrees as a TURN TILE. The paths bend at
        // the ends of every 4-tick leg -- including 90-degree corners mid-room -- so
        // an endpoint-extremes line model predicted turns a whole leg late
        // ("always turn around with ~2.7s on the tooltip").
        if (m.x !== n.x || m.y !== n.y) {
          const step = Math.max(Math.abs(n.x - m.x), Math.abs(n.y - m.y));
          const hd = bearing(m.x, m.y, n.x, n.y);
          if (m.mt && now - m.mt <= 1500 && step <= 4) {
            const inst = step / ((now - m.mt) / 1000);
            m.spd = m.spd ? m.spd * 0.7 + inst * 0.3 : inst;   // tiles per second
          }
          if (m.hd != null && angdiff(hd, m.hd) > 45) {
            m.turns = m.turns || {};
            if (m.turns[m.x + ',' + m.y] || Object.keys(m.turns).length < 8)
              m.turns[m.x + ',' + m.y] = 1;   // it turned ON the tile it stood on
          }
          m.hd = hd; m.mt = now;
        }
        if (f !== null) {
          if (m.face !== null && angdiff(f, m.face) > 30) {
            // Signed turn size: two matching consecutive deltas lock in a rotation
            // STEP (the sentinel sweeps in fixed increments -> future directions
            // become predictable). A turn observation can only ever be LATE (600ms
            // poll), so the phase anchor only moves EARLIER: an observation landing
            // before the predicted grid tightens the anchor, one landing after is
            // read as detection lag and ignored. Re-anchor outright when stale.
            const d = (((f - m.face) % 360) + 540) % 360 - 180;
            if (m.pend != null && Math.abs(d - m.pend) <= 10) m.step = d;
            m.pend = d;
            const period = DUNG_SENTINEL[n.id] ? DUNG_SENT_TURN_MS : DUNG_SPAWN_TURN_MS;
            if (!m.at || now - m.at > 60000) m.at = now;
            else {
              let r = (now - m.at) % period;
              if (r > period / 2) r -= period;
              if (r < 0) m.at += r;
            }
          }
          m.face = f;
        }
        m.x = n.x; m.y = n.y;
        return m.face;
      };
      // Sentinel gaze test for a WORLD tile: inside the drawn cone of any sentinel
      // (same half-angle as the cone below, so the label and the red tiles agree).
      const sentFaces = sent.map(s => ({ s: s, f: faceOf(s) }));
      const inGaze = (x, y) => sentFaces.some(sf => {
        if (sf.f === null) return true;                 // unreadable: assume watched
        const sz2 = (typeof sf.s.size === 'number' && sf.s.size > 0) ? sf.s.size : 2;
        const o2 = (sz2 % 2 === 0) ? -0.5 : 0;
        return angdiff(bearing(sf.s.x + o2, sf.s.y + o2, x, y), norm(sf.f + DUNG_GAZE_OFFSET)) <= DUNG_GAZE_HALF;
      });
      // Stamp-based countdown, bias-corrected: turn observations lag the real turn by
      // 0..600ms (the poll period), so knock off half of that instead of showing the
      // systematically-late raw figure.
      const eta = (m, period) => {
        if (!m || !m.at) return '';
        const left = Math.max(0, period - ((now - m.at) % period) - 300) / 1000;
        return ' ~' + left.toFixed(1) + 's';
      };
      // Time until the gaze actually LEAVES a tile -- not merely until the next
      // re-aim, which can point at the same section again. With a learned rotation
      // step the future directions are known: count re-aims until the cone clears the
      // tile. Without one, say only that the gaze MAY shift.
      const gazeEta = (x, y) => {
        const sf = sentFaces[0];
        if (!sf || sf.f === null) return '';
        const sm = dungSeekerMem[sf.s.uid];
        if (!sm || !sm.at) return '';
        const next = Math.max(0, DUNG_SENT_TURN_MS - ((now - sm.at) % DUNG_SENT_TURN_MS) - 300);
        if (!sm.step) return ' - may shift' + ' ~' + (next / 1000).toFixed(1) + 's';
        const sz2 = (typeof sf.s.size === 'number' && sf.s.size > 0) ? sf.s.size : 2;
        const o2 = (sz2 % 2 === 0) ? -0.5 : 0;
        const b = bearing(sf.s.x + o2, sf.s.y + o2, x, y);
        let fdir = norm(sf.f + DUNG_GAZE_OFFSET), k = 1;
        while (k < 8) {
          fdir = norm(fdir + sm.step);
          if (angdiff(fdir, b) > DUNG_GAZE_HALF) break;
          k++;
        }
        return ' - off in ~' + ((next + (k - 1) * DUNG_SENT_TURN_MS) / 1000).toFixed(1) + 's';
      };
      // SPAWNS ARE MARKED ON THE NPC, not the ground : the highlight
      // channel boxes the model by "#id" and the reader follows it each frame, so the
      // box tracks the patrol walk instead of lagging a tile behind it. Ground marks
      // stay for the gaze cone only. Needle labels must avoid ',' and '|' (the needle
      // separators), hence the dash phrasing.
      const needles = [];
      for (const sp of spawns) {
        const subdued = !(sp.actions || []).some(a => a === 'Subdue');
        if (subdued) { needles.push('#' + sp.id + '|Subdued'); continue; }
        const f = faceOf(sp);
        const m = dungSeekerMem[sp.uid];
        // Facing away = the player is in its REAR half-circle. Unknown facing (just
        // arrived, not yet seen moving) counts as facing you -- never call NOW blind.
        const away = f !== null && dungSelfPos
          && angdiff(f, bearing(sp.x, sp.y, dungSelfPos.x, dungSelfPos.y)) > 90;
        const gazed = inGaze(sp.x, sp.y);
        // POSITION-BASED turn eta: distance to the nearest LEARNED turn tile ahead on
        // the current walk direction, over the MEASURED speed. No phase stamp -> none
        // of the poll-lag lateness, and no geometry assumption -> corners count as
        // turns too. Falls back to the stamp countdown until a
        // waypoint on this leg has been seen once.
        const turnEta = () => {
          if (!m || m.hd == null || !m.spd || !m.turns) return null;
          const hx = Math.sin(m.hd * Math.PI / 180), hy = Math.cos(m.hd * Math.PI / 180);
          let ahead = null;
          for (const tk2 in m.turns) {
            const p2 = tk2.split(',');
            const vx = (+p2[0]) - sp.x, vy = (+p2[1]) - sp.y;
            const along = vx * hx + vy * hy;              // tiles ahead on this leg
            const across = Math.abs(vx * hy - vy * hx);   // sideways offset off the leg
            if (along < 0.5 || across > 1.1) continue;
            if (ahead === null || along < ahead) ahead = along;
          }
          return ahead !== null ? ahead / m.spd : null;
        };
        let label;
        if (away && !gazed) label = 'Subdue NOW';
        else if (gazed)     label = 'Wait - gaze' + gazeEta(sp.x, sp.y);
        else {
          const t = turnEta();
          label = 'Wait - facing you' + (t != null ? ' ~' + t.toFixed(1) + 's'
                                                    : eta(m, DUNG_SPAWN_TURN_MS));
        }
        needles.push('#' + sp.id + '|' + label);
      }
      for (const s of sent) {
        if (typeof s.face !== 'number' || s.face < 0) {
          needles.push('#' + s.id + '|Sentinel: facing UNREADABLE');
          continue;
        }
        const deg = ((s.face + DUNG_GAZE_OFFSET) % 360 + 360) % 360;
        // Footprint-derived geometry. THESE MUST STAY WITH THE CODE THAT USES THEM: dropping them
        // while leaving their uses throws a ReferenceError every tick and blanks the whole panel,
        // which node --check cannot catch.
        const sz = (typeof s.size === 'number' && s.size > 0) ? s.size : 2;
        // THE REPORTED TILE IS THE MODEL'S NORTH-EAST TILE, NOT ITS SOUTH-WEST ONE.
        // The game stores the model CENTRE in fine units, and for an EVEN-sized model
        // that centre lands exactly on a tile boundary: the sentinel reads 5812224/512
        // = 11352.0 exactly. Truncating to int therefore names the NE tile, and the
        // model extends in -x/-y from it. Proven on screen: the anchor box drew on the
        // model's NE corner.
        //   even size -> centre sits half a tile BELOW/LEFT of the reported tile
        //   odd size  -> fine centre is tile*512+256, truncates to the tile itself
        // Getting this backwards pushed the origin a full tile the wrong way on BOTH
        // axes, which is the displacement no amount of angle tuning could fix.
        const org = (sz % 2 === 0) ? -0.5 : 0;
        const start = Math.max(1, Math.ceil(sz / 2));   // just past the model edge
        // TWO EDGE LINES, not a filled block: a solid wedge only reaches 5 tiles inside the 16-mark
        // cap, while the lines reach the wall. Walk each edge with a DDA (step the dominant axis one
        // tile at a time), not by sampling round(sin*i), which skips and doubles tiles on diagonals.
        // Rasterise FROM THE MODEL CENTRE and drop the tiles inside the model's own footprint, so
        // the ray starts exactly at its edge with no gap.
        const cx = s.x + org, cy = s.y + org;            // model centre, tile-index space
        // Footprint: the reported tile is the NE one, so the body runs back sz-1 tiles.
        const bx0 = s.x - sz + 1, by0 = s.y - sz + 1;
        const inBody = (x, y) => x >= bx0 && x <= s.x && y >= by0 && y <= s.y;
        const line = (bear, len) => {
          const rad = ((bear % 360 + 360) % 360) * Math.PI / 180;
          const ex = Math.sin(rad) * len, ey = Math.cos(rad) * len;
          const steps = Math.max(Math.abs(ex), Math.abs(ey));
          const out = [];
          for (let n = 1; n <= Math.round(steps); n++) {
            const t = n / steps;
            const gx = Math.round(cx + ex * t), gy = Math.round(cy + ey * t);
            if (inBody(gx, gy)) continue;                // clip his own tiles
            out.push([gx, gy]);
          }
          return out;
        };
        const seen = {};
        const runs = [line(deg - DUNG_GAZE_HALF, DUNG_GAZE_LEN),
                      line(deg + DUNG_GAZE_HALF, DUNG_GAZE_LEN)];
        // interleave so a truncation shortens BOTH lines evenly rather than drawing one
        // whole line and half the other
        const most = Math.max(runs[0].length, runs[1].length);
        const pend = [];
        for (let n = 0; n < most; n++)
          for (const r of runs) if (r[n]) pend.push(r[n]);
        for (const [gx, gy] of pend) {
          if (marks.length >= 16) break;
          const kk2 = gx + ',' + gy;
          if (seen[kk2]) continue;          // the two lines share tiles close in
          seen[kk2] = 1;
          marks.push({ x: gx, y: gy, plane: s.plane || 0, rgb: 0xff5a5a, label: '' });
        }
        // LABEL RIDES THE LAST TILE ALREADY DRAWN -- no separate centre-line mark. That
        // extra mark landed between the two lines and read as a stray disconnected box
        
        // NO LABEL : the compass name adds nothing once the lines are
        // correct -- you can see which way he is looking. Dropping it also stops a pill
        // covering a tile you might need to read, and frees a mark for reach.
      }
      dungGuideTiles(marks);              // ground: the gaze cone only
      dungHighlightList(needles);         // npc boxes: spawns + unreadable sentinel
      return;
    }
  }
  // COLOURED RECESS PUZZLE (cache scan. Push/Pull each Block onto the
  // nearest coloured recess, mix the vials at the Shelves, then use each vial on the
  // block standing on the matching colour.
  //   recess (no action), FOUR theme variants each:
  //     blue   54504, 54525, 54546, 54623      green  54506, 54527, 54548, 54625
  //     yellow 54508, 54529, 54550, 54627      violet 54510, 54531, 54552, 54629
  //   Shelves "Mix Blue/Green/Yellow/Violet": 35241, 35242, 35243, 35245, 35246
  //   vials: Blue 19869, Green 19871, Yellow 19873, Violet 19875
  // THE BLOCK REPORTS ITS OWN COMPLETION. Block npcs run FIVE PER THEME: a generic
  // "Block" then the four dyed ones at +1..+4 (Blue, Green, Yellow, Violet) --
  // 13024, 13029, 13034, 13039, 13044 are the generics. Once a vial is applied the npc
  // BECOMES "Violet block" etc. Reading completion off the INVENTORY instead meant the
  // guide kept pointing at an empty slot after the vial was used, and kept guiding a
  // solved room. The dyed block is the source of truth.
  const DUNG_BLOCK_COL = {};        // npc id -> '' while generic, colour name once dyed
  for (const base of [13024, 13029, 13034, 13039, 13044]) {
    DUNG_BLOCK_COL[base] = '';
    ['Blue', 'Green', 'Yellow', 'Violet'].forEach((c, i) => { DUNG_BLOCK_COL[base + 1 + i] = c; });
  }
  const DUNG_RECESS = {};
  for (const [c, ids] of [['Blue',   [54504, 54525, 54546, 54623]],
                          ['Green',  [54506, 54527, 54548, 54625]],
                          ['Yellow', [54508, 54529, 54550, 54627]],
                          ['Violet', [54510, 54531, 54552, 54629]]])
    for (const i of ids) DUNG_RECESS[i] = c;
  const DUNG_VIAL_ITEM  = { Blue: 19869, Green: 19871, Yellow: 19873, Violet: 19875 };
  const DUNG_RECESS_RGB = { Blue: 0x5ab8f0, Green: 0x5fd07a, Yellow: 0xf0c419, Violet: 0xb06cff };
  const DUNG_SHELVES = { 35241: 1, 35242: 1, 35243: 1, 35245: 1, 35246: 1 };
  {
    const inR = e => typeof e.x === 'number' && here(e);
    const blocks = npcs.filter(n => DUNG_BLOCK_COL[n.id] !== undefined && inR(n));
    const recs   = (objs || []).filter(o => DUNG_RECESS[o.id] && inR(o));
    if (blocks.length && recs.length) {
      const kk = e => e.x + ',' + e.y;
      const blockAt = {};
      for (const b of blocks) blockAt[kk(b)] = b;
      // STABLE ORDER: `objs` order is not stable between polls, and an unsorted list
      // made the chosen colour alternate every tick and the highlight flicker.
      const seated = recs.filter(r => blockAt[kk(r)]).sort((p1, p2) => p1.x - p2.x || p1.y - p2.y);
      const freeR  = recs.filter(r => !blockAt[kk(r)]);
      const loose  = blocks.filter(b => !recs.some(r => kk(r) === kk(b)));
      // A seated recess whose block is already DYED is finished, whatever the backpack
      // says. Only the rest still want a vial.
      const pending = seated.filter(r => !DUNG_BLOCK_COL[blockAt[kk(r)].id]);
      if (!loose.length && !pending.length) {
        dungInvClear('recess');                 // solved: say nothing at all
        dungGuideTiles(marks);
        dungHighlightNpc(0);
        return;
      }
      // Phase 1: each loose block goes to its NEAREST free recess.
      for (const b of loose) {
        if (marks.length >= 16) break;
        let best = null, bd = 1e9;
        for (const r of freeR) {
          const dd = Math.max(Math.abs(r.x - b.x), Math.abs(r.y - b.y));
          if (dd < bd) { bd = dd; best = r; }
        }
        const col = best ? DUNG_RECESS[best.id] : '';
        marks.push({ x: b.x, y: b.y, plane: b.plane || 0,
                     rgb: col ? DUNG_RECESS_RGB[col] : 0xffffff,
                     label: col ? ('Push/Pull to the ' + col + ' recess')
                                : 'Push/Pull onto a recess' });
      }
      // Phase 2: the colour being applied now = first PENDING colour whose vial is held.
      const holdCol = pending.map(r => DUNG_RECESS[r.id])
                             .find(c => DUNG_VIAL_ITEM[c] && dungInvCount(DUNG_VIAL_ITEM[c]));
      // Only that block is marked: boxing the finished ones was noise.
      for (const r of pending) {
        const c = DUNG_RECESS[r.id];
        if (c !== holdCol || marks.length >= 16) continue;
        marks.push({ x: r.x, y: r.y, plane: r.plane || 0, rgb: DUNG_RECESS_RGB[c] || 0xffffff,
                     label: 'Use the ' + c + ' vial here' });
      }
      if (holdCol) dungHighlightInvItem(DUNG_VIAL_ITEM[holdCol],
                                        'Use on the ' + holdCol + ' block', 'recess');
      else dungInvClear('recess');
      // Mix whatever is still missing, in one trip. The Shelves are WALL-MOUNTED, so
      // their tile can fall in the neighbouring room cell and `here()` drops them --
      // match by PROXIMITY to the recesses instead (shelves 5 tiles away were
      // being skipped while the recesses in the same room marked fine).
      const need = pending.map(r => DUNG_RECESS[r.id])
                          .filter(c => DUNG_VIAL_ITEM[c] && !dungInvCount(DUNG_VIAL_ITEM[c]));
      if (need.length) {
        let shelf = null, shd = 1e9;
        for (const o of (objs || [])) {
          if (!DUNG_SHELVES[o.id] || typeof o.x !== 'number') continue;
          for (const r of recs) {
            const dd = Math.max(Math.abs(o.x - r.x), Math.abs(o.y - r.y));
            if (dd < shd) { shd = dd; shelf = o; }
          }
        }
        if (shelf && shd <= 24 && marks.length < 16)
          marks.push({ x: shelf.x, y: shelf.y, plane: shelf.plane || 0,
                       rgb: DUNG_RECESS_RGB[need[0]] || 0xffffff,
                       label: '-Shelves' + String.fromCharCode(10) + 'Mix ' + need.join(', ') });
      }
      dungGuideTiles(marks);
      dungHighlightNpc(0);
      return;
    }
  }
  // STRANGE PLANT ROOM (js5-16 scan); maze templates js5-5). One
  // Uproot plant sets the target COLOUR; you chop matching plants to open gaps in the
  // vine maze until you can reach and uproot it, and the colour CHANGES OFTEN.
  // Every colour has 4 "Chop" ids, 4 no-action ids and 2 "Uproot" ids:
  //   blue   Uproot 35507/35520  Chop 35577,35616,35715,35799
  //   purple Uproot 35523/35525  Chop 35602,35655,35719,35804
  //   red    Uproot 35562/35568  Chop 35606,35689,35734,35809
  //   yellow Uproot 35569/35576  Chop 35611,35709,35778,35830
  const DUNG_PLANT_UPROOT = { 35507:'blue',   35520:'blue',   35523:'purple', 35525:'purple',
                              35562:'red',    35568:'red',    35569:'yellow', 35576:'yellow' };
  const DUNG_PLANT_CHOP = {
    35577:'blue',   35616:'blue',   35715:'blue',   35799:'blue',
    35602:'purple', 35655:'purple', 35719:'purple', 35804:'purple',
    35606:'red',    35689:'red',    35734:'red',    35809:'red',
    35611:'yellow', 35709:'yellow', 35778:'yellow', 35830:'yellow' };
  // The 4 no-action ids per colour (same js5-16 scan): a plant not offering Chop. At a
  // template gap tile this is read as "already open" for the routing below.
  const DUNG_PLANT_DEAD = {
    35588:'blue',   35625:'blue',   35718:'blue',   35800:'blue',
    35604:'purple', 35685:'purple', 35720:'purple', 35808:'purple',
    35609:'red',    35708:'red',    35739:'red',    35812:'red',
    35613:'yellow', 35712:'yellow', 35780:'yellow', 35835:'yellow' };
  const DUNG_PLANT_RGB = { blue:0x4aa3ff, purple:0xb06cff, red:0xff5a5a, yellow:0xf2d24b };
  // THE MAZE IS CACHEABLE. The room is one of 4 fixed 16x16 templates (js5-5 room bank
  // region rx6, cells (1,0)..(1,3), one per baked door set; themes reskin the art but
  // the geometry is identical), rotated per instance. '#' = vine hedge / wall, 'C' = a
  // chop-plant gap, 'U' = the 2x2 uproot plant, '.' = floor. Row index = local Y (south
  // row first), string index = local X. Live rooms sit 16-ALIGNED in the dynamic region:
  // validated against a live capture -- origin (13040,2176), template (1,1)
  // rotated 180 put 'U' under the live uproot and 'C' under every live plant tile.
  const DUNG_PLANT_MAZES = [
    ['################','#.....#........#','#..UU#####.....#','#..UU#.#.#C#...#','#...##.##..##..#','##C##...C...C..#','#...#...#...#..#','##..##.####.##.#','##...C.#..C..###','##C#.#.#..##...#','#..###.C#..#...#','#......#.#.##..#','#...#C##...##..#','#..##....#C##..#','#..#.....#..#..#','################'],
    ['################','#..#.....#.#...#','#..##C####.C...#','#..##......#...#','##C#..##C###...#','#....##.......##','#..###....##C###','#..C..#..##...##','#.##...#.#.UU###','#.#....###.UU.##','#.###..#.###...#','#.###..C.....###','#...##.###.#C###','###C##...###...#','#.........C....#','################'],
    ['################','#....#....##...#','#...##C..##....#','#...##.#C#..#C##','###........##.##','#.###C####C#..##','#.##..........##','#.#...###C#..###','#.#..##...##.#.#','#....C..UU.C.#.#','#....###UU.#...#','#...##.#..##...#','#..##.###C#.##.#','#.##....#......#','#.#....##......#','################'],
    ['################','#..........#...#','##C#...#C#C#...#','#..#C###....####','#........##C#.##','#..###..#......#','##C#.#C##.#....#','##........#.#..#','##........##.#.#','#..####C#..#...#','#..#..#.####C#.#','#.#UU.#......###','#C.UU.C........#','#.....##.......#','#......###.....#','################'],
  ];
  {
    const inR = o => typeof o.x === 'number' && here(o);
    // vis === false = captured by the companion but no longer drawn (phantom-loc
    // filter). Colour changes REPLACE the loc and the capture keeps the earlier spawn, so
    // the raw list carries one stale state per past change.
    const plants = (objs || []).filter(o =>
      (DUNG_PLANT_UPROOT[o.id] || DUNG_PLANT_CHOP[o.id] || DUNG_PLANT_DEAD[o.id])
      && o.vis !== false && inR(o));
    const ups = plants.filter(o => DUNG_PLANT_UPROOT[o.id]);
    if (!ups.length) dungPlantSeen = {};       // not in the room: drop the history
    else {
      // PER-TILE RECENCY decides which state is real. The capture lists OLD and NEW
      // states together at one tile (capture: red 35734 AND yellow
      // 35778 both at 13044,2189), so at each tile the id that appeared most recently
      // is the live one. Marking any id that merely matched the target colour boxed
      // plants that were visibly another colour.
      const now = Date.now(), seen = {};
      for (const o of plants) {
        const k = o.x + ',' + o.y + '|' + o.id;
        seen[k] = 1;
        if (!dungPlantSeen[k]) dungPlantSeen[k] = now;
      }
      for (const k in dungPlantSeen) if (!seen[k]) delete dungPlantSeen[k];
      const tile = {};             // 'x,y' -> {o, t, tie}: the live state per tile
      for (const o of plants) {
        const tk = o.x + ',' + o.y, t = dungPlantSeen[tk + '|' + o.id] || 0;
        if (!tile[tk] || t > tile[tk].t) tile[tk] = { o: o, t: t, tie: false };
        else if (t === tile[tk].t && o.id !== tile[tk].o.id) {
          // SAME first-seen tick -- history unknown (first sighting already doubled,
          // e.g. right after a panel reload). Group by MOST ADVANCED state, the same
          // rule the sarcophagus/censer and herb-patch blocks use for coexisting loc
          // states: a no-action (spent) state outranks a bloomed one, so a tile whose
          // live state may be spent is never boxed -- an "any state matches"
          // tie fallback drew boxes on tiles with no flower standing.
          // screenshots. Preferring spent only ever costs a mark, and it
          // comes back on the next colour change, which recency resolves. Two
          // ACTIONABLE states tying (colour genuinely unknown) stay flagged `tie`
          // for the fallback colour rule below.
          const deadNew = !!DUNG_PLANT_DEAD[o.id], deadCur = !!DUNG_PLANT_DEAD[tile[tk].o.id];
          if (deadNew && !deadCur) tile[tk] = { o: o, t: t, tie: false };
          else if (!deadNew && !deadCur) tile[tk].tie = true;
          // (spent already winning, or both spent: keep it, no tie -- not boxed either way)
        }
      }
      // The uproot too is grouped by recency ACROSS its 2x2 footprint: colour changes
      // can re-anchor the replacement loc on another footprint tile, leaving a stale
      // uproot state on the earlier one. The most recently appeared uproot state is the
      // live one.
      let upo = null, upT = -1;
      for (const tk in tile) {
        const tv = tile[tk];
        if (DUNG_PLANT_UPROOT[tv.o.id] && tv.t > upT) { upT = tv.t; upo = tv.o; }
      }
      const col = upo ? DUNG_PLANT_UPROOT[upo.id] : '';
      const rgb = DUNG_PLANT_RGB[col] || 0x5fd07a;
      const plantCol = o2 => DUNG_PLANT_UPROOT[o2.id] || DUNG_PLANT_CHOP[o2.id] || DUNG_PLANT_DEAD[o2.id];

      // Match the instance to a template: origin is the 16-aligned block, then the
      // (variant, rotation) whose grid puts 'U' under the live uproot tile and 'C'
      // under EVERY live gap tile. Rotation is applied cell-wise, so the 2x2 uproot
      // needs no anchor bookkeeping. Two different grids fitting the same evidence
      // drops the route rather than guessing between them.
      let grid = null, gOx = 0, gOy = 0;
      if (upo) {
        const ox = upo.x & ~15, oy = upo.y & ~15;
        const fits = plants.every(o => o.x >= ox && o.x < ox + 16 && o.y >= oy && o.y < oy + 16);
        const gaps = [];
        for (const tk in tile) {
          const o2 = tile[tk].o;
          if (!DUNG_PLANT_UPROOT[o2.id]) gaps.push([o2.x - ox, o2.y - oy]);
        }
        if (fits && gaps.length >= 3) {
          let best = null, dup = false;
          for (const tpl of DUNG_PLANT_MAZES) for (let r = 0; r < 4; r++) {
            const g = [];
            for (let y = 0; y < 16; y++) g.push(new Array(16).fill('#'));
            for (let y = 0; y < 16; y++) for (let x = 0; x < 16; x++) {
              let px = x, py = y;
              for (let i = 0; i < r; i++) { const t2 = px; px = py; py = 15 - t2; }
              g[py][px] = tpl[y][x];
            }
            if (g[upo.y - oy][upo.x - ox] !== 'U') continue;
            if (!gaps.every(p => g[p[1]][p[0]] === 'C')) continue;
            if (best) dup = true; else best = g;
          }
          if (best && !dup) { grid = best; gOx = ox; gOy = oy; }
        }
      }

      // ROUTE: fewest CHOPS from the player's tile to the uproot, steps as tie-break
      // (a chop is what the puzzle actually charges -- the colour has to cycle onto
      // the gap before it opens). A 'C' gap whose live state is a no-action id is
      // already open and costs only the step.
      let route = null;
      if (grid && dungSelfPos) {
        const sx = dungSelfPos.x - gOx, sy = dungSelfPos.y - gOy;
        if (sx >= 0 && sx < 16 && sy >= 0 && sy < 16 && grid[sy][sx] !== '#') {
          // Open = nothing standing there. ABSENT counts: once the companion drops
          // despawned locs (del-capture gate, a chopped plant vanishes
          // from the feed entirely rather than turning into a no-action id.
          const openGap = (x, y) => {
            const tv = tile[(gOx + x) + ',' + (gOy + y)];
            return !tv || !!DUNG_PLANT_DEAD[tv.o.id];
          };
          const cost = new Array(256).fill(Infinity);
          const par = new Array(256).fill(-1);
          const done = new Array(256).fill(false);
          cost[sy * 16 + sx] = 0;
          let goal = -1;
          for (;;) {
            let u = -1, uc = Infinity;
            for (let i = 0; i < 256; i++) if (!done[i] && cost[i] < uc) { uc = cost[i]; u = i; }
            if (u < 0) break;
            done[u] = true;
            const ux = u % 16, uy = (u / 16) | 0;
            if (grid[uy][ux] === 'U') { goal = u; break; }
            for (const d of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
              const nx = ux + d[0], ny = uy + d[1];
              if (nx < 0 || nx > 15 || ny < 0 || ny > 15) continue;
              const ch = grid[ny][nx];
              if (ch === '#') continue;
              const st = 1 + (ch === 'C' && !openGap(nx, ny) ? 1024 : 0);
              if (uc + st < cost[ny * 16 + nx]) { cost[ny * 16 + nx] = uc + st; par[ny * 16 + nx] = u; }
            }
          }
          if (goal >= 0) {
            route = [];
            for (let u = goal; u >= 0; u = par[u]) {
              const ux = u % 16, uy = (u / 16) | 0;
              const tv = tile[(gOx + ux) + ',' + (gOy + uy)];
              if (grid[uy][ux] === 'C' && tv && !DUNG_PLANT_DEAD[tv.o.id]) route.push(tv);
            }
            route.reverse();       // par-walk built it goal-first
          }
        }
      }

      // Box the uproot at its REAL 2x2 footprint  when the matched
      // grid gives the footprint; the reported tile alone otherwise. Label only the
      // reported tile so the labels never stack.
      if (upo) {
        if (grid) {
          for (let y = 0; y < 16; y++) for (let x = 0; x < 16; x++) {
            if (grid[y][x] !== 'U' || marks.length >= 16) continue;
            const anchor = (gOx + x) === upo.x && (gOy + y) === upo.y;
            marks.push({ x: gOx + x, y: gOy + y, plane: upo.plane || 0, rgb: rgb,
                         label: anchor ? '-Uproot plant\nNow ' + col : '' });
          }
        } else if (marks.length < 16) {
          marks.push({ x: upo.x, y: upo.y, plane: upo.plane || 0, rgb: rgb,
                       label: '-Uproot plant\nNow ' + col });
        }
      }
      if (route && route.length) {
        // THE MAZE ROUTE, not the whole room: only the gaps on the way to the uproot,
        // numbered in walking order. A gap that is not the target colour right now is
        // a grey WAIT -- chopping it does nothing until the colour cycles onto it.
        for (let i = 0; i < route.length && marks.length < 16; i++) {
          const tv = route[i], c2 = plantCol(tv.o);
          const ready = !tv.tie && c2 === col && DUNG_PLANT_CHOP[tv.o.id];
          marks.push({ x: tv.o.x, y: tv.o.y, plane: tv.o.plane || 0,
                       rgb: ready ? rgb : 0x8a93a6,
                       label: '-' + (i + 1) + '. ' + (ready ? 'Chop' : 'Wait (now ' + c2 + ')') });
        }
      } else {
        // No settled route (template mismatch, player outside the block, or ambiguous
        // evidence): fall back to boxing every gap whose LIVE state matches the target
        // colour.
        for (const tk in tile) {
          if (marks.length >= 16) break;
          const tv = tile[tk], o2 = tv.o;
          const match = DUNG_PLANT_CHOP[o2.id] === col
            || (tv.tie && plants.some(o3 => o3.x === o2.x && o3.y === o2.y && DUNG_PLANT_CHOP[o3.id] === col));
          if (!match) continue;
          marks.push({ x: o2.x, y: o2.y, plane: o2.plane || 0, rgb: rgb,
                       label: '-Strange ' + col + ' plant\nChop' });
        }
      }
      dungGuideTiles(marks);
      dungHighlightNpc(0);
      return;
    }
  }
  // HOARDSTALKER RIDDLE ROOM (js5-18 scan.
  // Riddle-giver: NPC 11011 "Enigmatic hoardstalker", action "Get-Riddle". The named
  // variants carry "Take" and run in the usual id PAIRS:
  //   11146/47 Cub   11148/49 Little  11150/51 Naive  11152/53 Keen   11154/55 Brave
  //   11156/57 Brah  11158/59 Naabe   11160/61 Wise   11162/63 Adept  11164/65 Sachem
  // DELIBERATELY EXCLUDED: 16825/16826, 20287, 20289 and 22422-22425 are "Gorajo
  // hoardstalker", the card/shop NPC, a different feature entirely.
  // You Search a Food barrel (49598) and take what the riddle asked for.
  // THE RIDDLE NPC IS THE ONE CARRYING "Get-Riddle" -- matched on the live action, not
  // an id list. 11146-11165 are excluded: the cache shows them as "Cub hoardstalker" /
  // "Sachem hoardstalker" carrying Interact/Take/Attack, the Gorajo companions, nothing to
  // do with the riddle. Any of them standing in the room kept this
  // block alive, so the riddle box stayed pinned after the puzzle was done and after the
  // riddle-giver was gone. A js5-18 sweep finds exactly ONE npc in the
  // game with Get-Riddle: 11011, the Enigmatic hoardstalker.
  //
  // Matching on the action also gives solved-detection for free IF the option is dropped
  // once the riddle is answered -- the same way the coloured ferrets lose Scare.
  const DUNG_HOARD_NPCS = { 11011: 1 };
  const dungIsRiddler = n => (n.actions || []).some(a => a === 'Get-Riddle')
                          || DUNG_HOARD_NPCS[n.id];
  // THE NPC ID DOES NOT DETERMINE THE ANSWER. The RIDDLE does: you
  // Get-Riddle and the text appears in the NPC dialogue, group 1184 comp 10 -- the same
  // read the Murder-on-the-Border and No-Place-Like-Home guides already use. Confirmed
  // live: "While many call me mould / Some call me savoury." -> Gissel mushroom, 4.
  const DUNG_HOARD_IFACE = [1184, 1186, 1191];   // npc / server-message / player dialogue
  // `k` = a distinctive lowercase fragment of the riddle. `opt` = the take-list ROW text
  // where it differs from the item name (the list says "Mushroom." for Gissel mushroom
  // and "Poison." for Weapon poison, so matching on the item name alone would miss).
  // `loc` = which container. Only container 4 has a known loc id, so riddles for 1-3
  // still name the item and container without boxing anything.
  // Two container KINDS, and they need different handling:
  //   `gid` = the item lies ON THE GROUND with an "(o)" name suffix -> box the ground
  //           item by ITEM id (locations 1 and 3, two separate floor piles).
  //   `opt` = the item is inside a searchable object -> box the take-list ROW by text
  //           (location 2 = chest 49595, location 4 = food barrel 49598).
  const DUNG_HOARD_RIDDLES = [
    { k:'serpent am i',             item:'Cave eel',           opt:'Cave eel',           loc:4 },
    { k:'born through fire',        item:'Ashes',              gid:17379,                loc:3 },
    { k:'currently i have no head', item:'Headless arrow',     gid:17403,                loc:1 },
    { k:'from your veins i flow',   item:'Blood rune',         opt:'Blood rune',         loc:2 },
    { k:'cursed with but one eye',  item:'Needle',             opt:'Needle',             loc:2 },
    { k:'water that brings fire',   item:'Firebreath whiskey', opt:'Firebreath whiskey', loc:4 },
    { k:'i am worthless',           item:'Coins',              opt:'Coins',              loc:2 },
    { k:'faith in a dark god',      item:'Unholy symbol',      opt:'Unholy symbol',      loc:2 },
    { k:'denizens of the sea',      item:'Fishing rod',        opt:'Fishing rod',        loc:2 },
    { k:'remove my yellow skin',    item:'Banana',             opt:'Banana',             loc:4 },
    { k:'sticks and stones',        item:'Bones',              gid:17387,                loc:3 },
    { k:'blunt force i provide',    item:'Hammer',             gid:17401,                loc:1 },
    { k:'slowest of assassins',     item:'Weapon poison',      opt:'Poison',             loc:4 },
    { k:'though i am light',        item:'Feather',            gid:17393,                loc:1 },
    { k:'destined to protect',      item:'Novite kiteshield',  gid:17405,                loc:3 },
    { k:'call me mould',            item:'Gissel mushroom',    opt:'Mushroom',           loc:4 },
    { k:'fill a room with me',      item:'Vial of water',      opt:'Vial of water',      loc:4 },
    { k:'deathslinger is merely',   item:'Bowstring',          gid:17389,                loc:1 },
  ];
  // 2 = chest (Search, 5 rows: Blood rune / Coins / Fishing rod / Needle / Unholy
  // symbol), 4 = food barrel (Search, 6 items over 2 pages). 1 and 3 are ground piles.
  // THEME VARIANTS (barrel not highlighted): every Daemonheim puzzle
  // prop ships one loc id per tileset, in consecutive triples -- cache ground truth
  // (js5-16 names): Chest 49594/49595/49596, Food barrel 49597/49598/49599. The single
  // mid-triple id only matched one theme.
  const DUNG_HOARD_LOC_OBJ = { 2: { 49594: 1, 49595: 1, 49596: 1 },
                               4: { 49597: 1, 49598: 1, 49599: 1 } };
  // Group 1188 = "Dialogue option select", for BOTH the chest list and the paged barrel
  // list. The row TEXT comps are NOT on a clean stride here -- the live tree shows
  // option 1's label at comp 6 and option 2's at comp 33, each inside its own row LAYER
  // -- so scan a RANGE and match on text instead of guessing ids. " number comps in the
  // same range never contain an item name, so they cannot false-match.
  const DUNG_HOARD_GROUP = 1188;
  // MUST stay <= 64 ids: InterfaceCompsJson returns "{}" for a longer list, which matches
  // nothing at all.
  const DUNG_HOARD_COMPS = Array.from({ length: 64 }, (_, i) => i).join(',');
  {
    const inR = o => typeof o.x === 'number' && here(o);
    const hoard = npcs.find(n => dungIsRiddler(n) && inR(n));
    if (!hoard) { dungHoardRiddle = null; dungHerbClear('riddle'); }   // left the room: drop a stale answer + this panel's box
    else {
      // Latch while the riddle dialogue is up: it closes as you walk to the container.
      {
        const t = dungDlgText();
        const hit = t && DUNG_HOARD_RIDDLES.find(r => t.indexOf(r.k) >= 0);
        if (hit) dungHoardRiddle = hit;
      }
      const r = dungHoardRiddle;
      const cid = (r && DUNG_HOARD_LOC_OBJ[r.loc]) || null;   // id-set: any theme variant
      // DISPLAY name carries the "(o)" origin suffix that every Daemonheim hoardstalker
      // item has (the wiki take-table lists them all as "<name> (o)", and the ground
      // piles spawn with that exact suffix) --. The take-LIST rows drop
      // it ("Vial of water."), so the highlight MATCH text (`want`) stays without it.
      const itemDisp = r ? r.item + ' (o)' : '';
      // ONE target, not all of them : a room holds several barrels and
      // boxing every one was unreadable noise. Nearest to the player wins.
      const nearest = (list, match) => {
        if (!dungSelfPos) return null;
        let best = null, bd = 1e9;
        for (const e of (list || [])) {
          if (!match(e) || typeof e.x !== 'number' || !here(e)) continue;
          const dd = Math.max(Math.abs(e.x - dungSelfPos.x), Math.abs(e.y - dungSelfPos.y));
          if (dd < bd) { bd = dd; best = e; }
        }
        return best;
      };
      if (r && r.gid) {
        // Locations 1 and 3 are floor piles: the answer is a GROUND ITEM carrying an
        // "(o)" name suffix, so there is nothing to search, just pick it up.
        const g = nearest(dungGroundCache, e => e.id === r.gid);
        if (g && marks.length < 16)
          marks.push({ x: g.x, y: g.y, plane: g.plane || 0, rgb: 0x5fd07a,
                       label: '-' + itemDisp + '\nTake this' });
      } else if (cid) {
        const best = nearest(objs, e => cid[e.id]);
        if (best && marks.length < 16)
          marks.push({ x: best.x, y: best.y, plane: best.plane || 0, rgb: 0x5fd07a,
                       label: (r.loc === 2 ? '-Chest' : '-Food barrel')
                              + '\nSearch, take ' + itemDisp });
      }
      // The take-list can paginate ("(1 OF 2)" with a "More..." row), so box the row by
      // TEXT and fall back to More... when the item is not on this page. Never key off
      // the option NUMBER: it shifts between pages. Ground-pile riddles have no list.
      const want = (r && !r.gid) ? (r.opt || r.item) : '';
      if (!want || !(dungHighlightOptionText(DUNG_HOARD_GROUP, DUNG_HOARD_COMPS, want, 'riddle')
                  || dungHighlightOptionText(DUNG_HOARD_GROUP, DUNG_HOARD_COMPS, 'more', 'riddle')))
        dungHerbClear('riddle');
      dungGuideTiles(marks);
      dungHighlightNpc(hoard.id,
        !r ? 'Get-Riddle, then read it'
           : 'Riddle wants: ' + itemDisp);
      return;
    }
  }
  // Broken-leg NPC: 11004 needs his leg repaired.
  const legNpc = npcs.find(n => n.id === 11004 && typeof n.x === 'number' && here(n));
  if (legNpc) { dungGuideTiles(marks); dungHighlightNpc(11004, 'Repair his leg'); return; }
  // Construct: 11005 needs charging.
  const construct = npcs.find(n => n.id === 11005 && typeof n.x === 'number' && here(n));
  if (construct) { dungGuideTiles(marks); dungHighlightNpc(11005, 'Charge the construct'); return; }
  // Damaged constructs (cache: the three Repair-action ids): 11002 arm, 11003 head
  // (by elimination -- arm and leg are ), 11004 leg (own branch above).
  const armless = npcs.find(n => n.id === 11002 && typeof n.x === 'number' && here(n));
  if (armless) { dungGuideTiles(marks); dungHighlightNpc(11002, 'Attach an arm'); return; }
  const headless = npcs.find(n => n.id === 11003 && typeof n.x === 'number' && here(n));
  if (headless) { dungGuideTiles(marks); dungHighlightNpc(11003, 'Attach a head'); return; }
  // Monolith room, matched by NAME so every theme's id family works (seen: 10975
  // inactive + 10978 active on frozen, 10979 active w/ Count-charges on abandoned;
  // the shade needle below is name-matched already). An 'Activate' action (or the
  // verified inactive id 10975) = the INACTIVE form -> guide activating it; any
  // other Monolith = active -> progress bar from varc 1233 + box the shades.
  // At 100% that monolith is DONE: remember its tile and stop guiding it (and the
  // shades). A monolith at OTHER coordinates is a fresh puzzle and tracks again.
  // Charged monoliths keep their id and just switch anim on
  // abandoned: 13069 charging -> 13072 charged, id 10979 both) -- an anim in this
  // set = DONE even when the varc-1233 latch was missed (e.g. re-entering the
  // room later). Anim ids are per-theme; extend as observed.
  const DUNG_MONO_DONE_ANIM = { 13072: 1 };
  const monosHere = npcs.filter(n => n.name === 'Monolith' && typeof n.x === 'number'
    && !dungMonoDone[n.x + ',' + n.y] && !DUNG_MONO_DONE_ANIM[n.anim] && here(n));
  const monoOff = monosHere.find(n => n.id === 10975 || (n.actions || []).some(a => a && /activate/i.test(a)));
  if (monoOff) { dungGuideTiles(marks); dungHighlightNpc(monoOff.id, 'Activate the monolith'); return; }
  const mono = monosHere[0];
  if (mono) {
    const MAX = 195;   // varc 1233 caps at 195 when full
    if (dungMonoCharge != null && dungMonoCharge >= MAX) {
      dungMonoDone[mono.x + ',' + mono.y] = true;   // charged -> fall through, clear below
    } else {
      const needles = [];
      let lbl = '';
      if (dungMonoCharge != null) {
        const v = dungMonoCharge;
        if (v >= 0 && v <= MAX) {
          const f = Math.round(v / MAX * 10);
          lbl = '[' + '#'.repeat(f) + '-'.repeat(10 - f) + '] ' + Math.round(v / MAX * 100) + '%';
        } else lbl = 'Progress ' + v;
      }
      // shades FIRST: the reader aims the direction arrow at the first labelled
      // highlight, and the thing to walk to is a shade, not the monolith. The '*'
      // match-ALL needle boxes EVERY shade, bounded to +-6 tiles of the monolith
      // ("x;y;6" anchor -- shades a room over belong to a different monolith).
      // Needs the launcher rebuilt with the reader's '*'-needle support.
      needles.push('*mysterious shade|Kill|' + mono.x + ';' + mono.y + ';6');
      needles.push('#' + mono.id + (lbl ? '|' + lbl : ''));
      dungGuideTiles(marks); dungHighlightList(needles); return;
    }
  }
  const pblocks = npcs.filter(n => DUNG_PUZZLE[n.id] && typeof n.x === 'number' && here(n));
  if (pblocks.length >= 6) {
    const xs = Array.from(new Set(pblocks.map(b => b.x))).sort((a, b) => a - b);   // W->E
    const ys = Array.from(new Set(pblocks.map(b => b.y))).sort((a, b) => b - a);   // N->S
    if (xs.length === 3 && ys.length === 3) {
      const grid = [[null, null, null], [null, null, null], [null, null, null]];
      for (const b of pblocks) { const c = xs.indexOf(b.x), r = ys.indexOf(b.y); if (c >= 0 && r >= 0) grid[r][c] = b; }
      let er = -1, ec = -1;
      for (let r = 0; r < 3; r++) for (let c = 0; c < 3; c++) if (!grid[r][c]) { er = r; ec = c; }
      if (er >= 0) {
        let home = null, any = null;
        for (let r = 0; r < 3; r++) for (let c = 0; c < 3; c++) {
          const b = grid[r][c]; if (!b) continue;
          const t = DUNG_PUZZLE[b.id], correct = (c === t[0] && r === t[1]);
          if (correct || (Math.abs(r - er) + Math.abs(c - ec) !== 1)) continue;   // must be movable + misplaced
          if (t[0] === ec && t[1] === er) home = b; else if (!any) any = b;        // target == empty -> home move
        }
        const pick = home || any;
        if (pick) { dungGuideTiles(marks); dungHighlightNpc(pick.id, 'Slide this tile'); return; }
      }
    }
  }
  // statues: box the FIRST misplaced pushable with the push it needs, and mark its
  // target tile in-world (one statue at a time -- the next lights up when this one
  // lands). Grid-local +x = world east, +y = world north, so the delta reads as
  // directions directly.
  if (dungStatues) {
    const mis = dungStatues.find(s => !s.done);
    if (mis) {
      const dx = mis.tgt[0] - mis.cur[0], dy = mis.tgt[1] - mis.cur[1];
      const parts = [];
      if (dx) parts.push(Math.abs(dx) + ' ' + (dx > 0 ? 'east' : 'west'));
      if (dy) parts.push(Math.abs(dy) + ' ' + (dy > 0 ? 'north' : 'south'));
      marks.push({ x: mis.tx, y: mis.ty, plane: 0, label: 'Push the statue here', rgb: 0x5fd07a });
      dungGuideTiles(marks);
      dungHighlightNpc(mis.id, 'Push ' + parts.join(', '));
      return;
    }
  }
  dungGuideTiles(marks);
  dungHighlightNpc(0);   // nothing to guide -> clear
}
// per-party-member colours (p1..p5; the player's own arrow stays red). Picked for
// contrast against the brown room tiles AND the dark board -- no yellows/ambers
// (invisible on the tan floor art).
const DUNG_PARTY_COLS = ['#39d3d3', '#f06bd8', '#e8ecff', '#8c5aff', '#6bf06b'];
const dungMateCol = pn => DUNG_PARTY_COLS[(pn - 1) % DUNG_PARTY_COLS.length] || DUNG_PARTY_COLS[0];
// camera-yaw -> arrow rotation, calibrated : SIGN +1, OFFSET 180
// (north-camera -> north arrow; NE -> NE). yaw 5115 runs the SAME rotational sense
// as the screen once offset by 180deg.
const DUNG_YAW_SIGN = 1, DUNG_YAW_OFFSET = 180;

// World tile -> (room delta from the anchor room, in-room local 0..13) on the 16-pitch
// grid, anchored at the start room's SW corner. Gap tiles (local 14/15) clamp onto the
// room to their west/south, so a marker in a doorway draws at that room's wall.
function dungRoomOf(sw, wx, wy) {
  const dx = wx - sw.x, dy = wy - sw.y;
  const rx = Math.floor(dx / DUNG_ROOM_PITCH), ry = Math.floor(dy / DUNG_ROOM_PITCH);
  return { rx: rx, ry: ry,
           lx: Math.min(dx - rx * DUNG_ROOM_PITCH, DUNG_ROOM_W - 1),
           ly: Math.min(dy - ry * DUNG_ROOM_PITCH, DUNG_ROOM_W - 1) };
}
// In-room local (0..13) -> pixel offset inside the 56px map cell. Half-span 20px is
// slightly compressed so an 18px marker at the far wall still sits INSIDE the room
// tile (at full span the arrow overhung the border and read as "in the gap").
function dungSubPx(lx, ly) {
  const H = 28, S = 20, R = (DUNG_ROOM_W - 1) / 2;   // centre of the 0..13 span = 6.5
  if (typeof lx !== 'number' || typeof ly !== 'number') return { left: H, top: H };   // unknown -> centre
  return { left: H + DUNG_MAP_XSIGN * (lx - R) / R * S,
           top:  H + DUNG_MAP_YSIGN * (ly - R) / R * S };
}
function dungSubStyle(lx, ly) { const p = dungSubPx(lx, ly); return 'left:' + p.left.toFixed(1) + 'px;top:' + p.top.toFixed(1) + 'px'; }

// Key item ids are a clean lattice: 18202 + 2*(index-1), colour-major then shape
// (enum 5734 maps HUD key index 1-64 to these same ids; cache-verified).
function dungKeyInfo(item) {
  if (item < 18202 || item > 18328 || (item - 18202) % 2) return null;
  const i = (item - 18202) / 2;
  const color = DUNG_KEY_COLORS[i >> 3];
  return { idx: i + 1, color: color, si: i & 7, item: item,
           name: color + ' ' + DUNG_KEY_SHAPES[i & 7] + ' key' };
}

// The eight door-key SHAPES as inline glyphs (triangle, diamond, rectangle,
// pentagon, corner, crescent, wedge, shield), filled with the key colour.
const DUNG_SHAPE_PATHS = [
  'M6 1 L11 10.5 H1 Z',
  'M6 0.8 L11.2 6 L6 11.2 L0.8 6 Z',
  'M1.5 3.5 H10.5 V8.5 H1.5 Z',
  'M6 0.8 L11.2 4.7 L9.2 10.8 H2.8 L0.8 4.7 Z',
  'M2 2 H10 V6 H6.2 V10 H2 Z',
  'M8.3 1.4 A5.1 5.1 0 1 0 8.3 10.6 A6.4 6.4 0 0 1 8.3 1.4 Z',
  'M2 10 V2 A8 8 0 0 1 10 10 Z',
  'M6 0.8 L10.6 2.4 V6 C10.6 8.7 8.7 10.4 6 11.2 C3.3 10.4 1.4 8.7 1.4 6 V2.4 Z',
];
function dungShape(si, hex, px) {
  return '<svg class="dg-shape" width="' + px + '" height="' + px + '" viewBox="0 0 12 12">'
    + '<path d="' + DUNG_SHAPE_PATHS[si] + '" fill="' + hex + '" stroke="rgba(0,0,0,.5)" stroke-width="0.8"/></svg>';
}

// Complete map-marker tables from the renderer CS2 script5999:
//   room graphics 2831 = start, 2833 = boss; keys enum 3008 (24px obj);
//   skill-door graphics enum 371 keyed by skill id (16px), names enum 108;
//   player arrow 2825-2829 by facing; gatestone objs 17489 / 29468 / 18829.
const DUNG_START_SPR = 2831, DUNG_BOSS_SPR = 2833;
// unexplored room art: "?" tiles (2787-2790 / 2806-2809) + dark key/skill-locked
// (35883-35886). A room whose bg is ONLY these hasn't been entered yet.
// Unexplored = the map cell shows ONLY "?" sprites. Read from the artwork, never
// inferred from door bits -- doors = 0 means "no exits known", which is a different
// thing and got this wrong three separate ways on.
function dungCellUnex(c) {
  return !!(c && c.bg && c.bg.length && c.bg.every(sp => DUNG_UNEX_SPR.has(sp)));
}
const DUNG_UNEX_SPR = new Set([2787, 2788, 2789, 2790, 2806, 2807, 2808, 2809, 35883, 35884, 35885, 35886]);
// NB: a "non-critical path" theory (dark-hatch room sprites = skippable side rooms)
// was DISPROVEN by live readings -- the dark tier just means "unexplored", shared by
// plain "?" rooms AND locked/skill-door rooms, and such rooms sit on the critical path
// with no alternative. So no non-crit marking is drawn.
// three gatestones; the room is outlined in the stone's colour (sampled from its
// item icon). 17489 personal (teal), 29468 second (red), 18829 group (blue).
const DUNG_GATESTONES = {
  17489: { name: 'Gatestone', color: '#5ae1b4' },
  29468: { name: 'Gatestone 2', color: '#e1625a' },
  18829: { name: 'Group gatestone', color: '#5a9fe1' },
};
const DUNG_SKILL_DOOR = {
  197: 'Attack', 198: 'Strength', 199: 'Defence', 200: 'Ranged', 201: 'Prayer',
  202: 'Magic', 203: 'Constitution', 204: 'Agility', 205: 'Herblore', 206: 'Thieving',
  207: 'Crafting', 208: 'Fletching', 209: 'Mining', 210: 'Smithing', 211: 'Fishing',
  212: 'Cooking', 213: 'Firemaking', 214: 'Woodcutting', 215: 'Runecrafting',
  216: 'Slayer', 217: 'Farming', 220: 'Hunter', 221: 'Construction', 222: 'Summoning',
  3028: 'Dungeoneering', 9170: 'Divination', 26541: 'Invention', 10782: 'Archaeology',
  30934: 'Necromancy', 1817: 'Quest', 1818: 'Quest',
};

// Room-tile sprite -> door bitmask (N=1 E=2 S=4 W=8). A door = a floor STUB that
// reaches the outermost tile-edge pixel at the edge centre (walls are inset 2+px);
// decoded from the js5-8 room sprites and verified mutual against live grid
// connectivity. Two adjacent rooms link only when BOTH carry the
// matching door. Sprites not listed => doors unknown, no line.
const DUNG_ROOM_DOORS = {
  2787: 4, 2788: 8, 2789: 1, 2790: 2, 2791: 4, 2792: 8, 2793: 1, 2794: 2,
  2795: 12, 2796: 9, 2797: 3, 2798: 6, 2799: 13, 2800: 11, 2801: 7, 2802: 14,
  2803: 15, 2804: 5, 2805: 10, 2806: 4, 2807: 8, 2808: 1, 2809: 2, 2810: 4,
  2811: 8, 2812: 1, 2813: 2, 2814: 12, 2815: 9, 2816: 3, 2817: 6, 2818: 13,
  2819: 11, 2820: 7, 2821: 14, 2822: 15, 2823: 5, 2824: 10,
  35883: 4, 35884: 8, 35885: 1, 35886: 2,
};

function dungFetchGroup(gid) {
  try { return (JSON.parse(rtxData.sync('state.interfaceGroup', gid) || '{}').widgets) || []; }
  catch (e) { return []; }
}

// Party interface groups: 91 = Daemonheim lobby formation panel, 92 = in-dungeon
// party list. Both carry the member names in TEXT comps 19-23.
const DUNG_PARTY_GROUPS = [91, 92];
// Placeholder labels that occupy a name slot before the party is formed (comp 19
// shows "Party Leader" until a real leader name replaces it) -- never a real name.
const DUNG_PARTY_LABELS = { 'Party Leader': 1, 'Party Member': 1, 'Party member': 1 };

// RS display names in interface text carry NON-BREAKING spaces (u00a0), e.g.
// "Aurni x" -- normalise to a plain space so the roster key, the scene name and the
// hiscores lookup (aurni_x) all agree. Module-level so both the tab fetch and the
// off-tab tick share it.
function rosterKey(s) { return (s || '').replace(/\s+/g, ' ').replace(/["<>&]/g, '').trim(); }

function dungRosterAdd(n) {
  if (!n) return;
  const strip = s => (s || '').replace(/[^a-z0-9]/gi, '').toLowerCase();
  const id = strip(n);
  if (!id) return;
  for (const e in dungPartyRoster) if (strip(e) === id) return;   // already present in some form
  dungPartyRoster[n] = 1;
}

// Party roster (from the party interface group 92 name slots comps 19-23) + official
// index_lite hiscore fetch. Runs during FORMATION (group 92 open in the lobby) as well
// as inside a dungeon, so stats are ready before the first floor. Each new name is
// asked once; pending/ok entries never re-ping (the bridge caches per session), so
// polling is free. Returns true if the party interface was open.
function dungPumpPartyHiscores(party92) {
  // The local player's own name from the account snapshot (works in the lobby too, before
  // any scene read sets dungSelfName). Self is NEVER hiscore-fetched: the local live skills
  // come from the client's own memory (the stats panel / dungSkillLevel), so a self lookup
  // would be a wasted call.
  try { if (lastSnap && lastSnap.display_name) dungSelfName = rosterKey(lastSnap.display_name); } catch (e) {}
  // name slots comps 19-23 of whichever party interface is open: group 91 = the
  // Daemonheim LOBBY formation panel, group 92 = the in-dungeon party list. Both use
  // the same layout.
  if (party92)
    for (const gid of DUNG_PARTY_GROUPS)
      for (const w of dungFetchGroup(gid)) {
        const comp = w.t ? w.t[1] : -1;
        const raw = w.x;
        // A REAL display name renders its internal space as a non-breaking space
        // (U+00A0); the interface's placeholder/role labels ("Party Leader", "Invite
        // Player") use a regular space -- so a raw regular space marks a label, not a
        // member. Belt-and-braces: also drop the known placeholder strings. (If the
        // reader ever normalises NBSP->space, the denylist still catches "Party Leader".)
        if (comp >= 19 && comp <= 23 && raw && raw.length >= 1 && raw.length <= 20
            && raw.indexOf(' ') < 0 && !DUNG_PARTY_LABELS[raw])
          dungRosterAdd(rosterKey(raw));
      }
  // prune any placeholder that leaked in earlier (e.g. before this filter existed):
  // drop it from the roster and its fetch bookkeeping so it stops showing as a member.
  for (const l in DUNG_PARTY_LABELS) {
    delete dungPartyRoster[l]; delete dungHsPoll[l]; delete dungHsDone[l]; delete dungPartyStats[l];
  }
  // heal a roster that already forked before the identity dedupe existed: the same
  // player under two byte-variants doubled the party count and the hiscore fetches
  {
    const seen = {};
    for (const n of Object.keys(dungPartyRoster)) {
      const id = n.replace(/[^a-z0-9]/gi, '').toLowerCase();
      if (seen[id]) { delete dungPartyRoster[n]; delete dungHsPoll[n]; }
      else seen[id] = 1;
    }
  }
  if (bridge().hiscores) {
    for (const n in dungPartyRoster) if (!dungIsSelf(n) && !dungHsDone[n] && !dungHsPoll[n]) dungHsPoll[n] = 1;
    const ask = Object.keys(dungHsPoll);
    if (ask.length) {
      try {
        const hs = JSON.parse(rtxData.sync('host.hiscores', ask.join(',')) || '{}');
        for (const n in hs) {
          if (hs[n].s === 'ok') {
            const lv = [], lines = (hs[n].b || '').split(/\r?\n/);
            for (let i = 1; i <= SKILL_NAMES.length && i < lines.length; i++) {
              const p = lines[i].split(',');               // rank,level,xp; line 0 = Overall
              lv[i - 1] = (p.length >= 2 && +p[1] > 0) ? +p[1] : null;
            }
            dungPartyStats[n] = lv;
            delete dungHsPoll[n]; dungHsDone[n] = 1;
            dungPartyReport('hiscore', { name: n, levels: lv });   // relay to the party so nobody else looks this player up
          } else if (hs[n].s === 'error') { delete dungHsPoll[n]; dungHsDone[n] = 1; }
        }
      } catch (e) {}
    }
  }
  // Share the local player's LIVE levels with the party as a 'hiscore' fact -- hiscores lag
  // behind live training, which made two clients band the same room differently
  // (. Every member shares live, every member merges the
  // shared values, so the rules run on identical numbers everywhere. Re-sent only
  // when a level (or the code) changes; the local copy ignores the echo (dungIsSelf).
  try {
    if (dungSelfName && lastSnap && lastSnap.skills && dungSyncCode()) {
      const lv = SKILL_NAMES.map((_, i) => dungSkillLevel(i));
      const lsig = dungSyncCode() + '|' + dungSelfName + '|' + lv.join(',');
      if (lsig !== dungSelfLvSig) { dungSelfLvSig = lsig; dungPartyReport('hiscore', { name: dungSelfName, levels: lv }); }
    }
  } catch (e) {}
  dungPartyMerge();   // pull in door levels + hiscores other members shared
  return !!party92;
}

// ---- Cross-PC party sync (manual code): relay facts one member learns to the rest via
// the RuneTools server. The launcher holds the shared cache + SSE stream; this panel just
// report locally-learned facts and merge received ones. See Bridge.cpp party* fns.
function dungPartyReport(kind, data) {
  try { if (bridge().partyReport && bridge().partyGetCode && rtxData.sync('party.partyGetCode'))
          rtxData.sync('party.partyReport', kind, JSON.stringify(data)); } catch (e) {}
}
// Merge the launcher's party cache into local state. Received DOORS fill dungDoorLevels
// for cells not examined locally; received HISCORES fill dungPartyStats AND mark the name
// done so that player is never looked up again. Never re-reports
// (only local captures report), so there's no echo loop.
function dungPartyMerge() {
  dungSyncCode();   // touch partyGetCode -> loads the persisted code + starts the SSE loop if idle
  let pd = null;
  try { if (bridge().partyData) pd = JSON.parse(rtxData.sync('party.partyData') || '{}'); } catch (e) {}
  if (!pd || !pd.code) return;
  const hs = pd.hiscores || {};
  for (const name in hs) {
    if (dungIsSelf(name)) continue;                 // the local live stats win
    if (Array.isArray(hs[name])) {
      // OVERWRITE, not first-write-wins: a member's live-shared levels must replace
      // the lagged official hiscore this client may have fetched first, so every
      // client converges on the same numbers (the server cache is the shared truth).
      dungPartyStats[name] = hs[name];
      dungHsDone[name] = 1; delete dungHsPoll[name];   // don't fetch what a teammate already shared
    }
  }
  // doors + non-crit marks are FLOOR-scoped: right after a floor change is detected
  // (and reported 'reset'), the launcher cache may still hold the previous floor's
  // facts until the reset round-trips -- skip re-ingesting them during the hold.
  // Hiscores above are party-scoped and always safe to merge.
  if (Date.now() < dungPartyHoldUntil) return;
  // MIRROR GUARD: until the bridge has received an actual server snapshot for this
  // code, its cache is NOT the party's truth -- acting on it deletes the player's own
  // fresh non-crit mark 3s after the click whenever the report round-trip wasn't
  // live yet (server not deployed / stream still connecting; 
  // as "marks flip back by themselves").
  if (!pd.synced) return;
  const doors = pd.doors || {};
  for (const cell in doors) {
    const d = doors[cell];
    if (d && d.skill && d.level && !dungDoorLevels[cell])
      dungDoorLevels[cell] = { skill: d.skill, level: d.level | 0, shared: true };
  }
  // Room-mark mirrors. Two safety rules learned the hard way:
  // (1) mirror a fact kind ONLY when the launcher's payload actually carries its
  //     field -- an older build/server omits it entirely, and treating "absent" as
  //     "empty set" deletes the player's fresh local marks;
  // (2) REMOVAL is ack-based: a mark may only be mirror-removed after it has been
  //     SEEN in the shared set at least once (a real teammate un-mark). A mark whose
  //     report never landed (rejected kind, lost POST) stays local forever instead
  //     of silently vanishing after the 3s hold.
  const now = Date.now();
  if (Array.isArray(pd.noncrit)) {
    const want = {};
    for (const cell of pd.noncrit) want[cell] = 1;
    const fresh = cell => dungNonCritTouch[cell] && now - dungNonCritTouch[cell] < 3000;
    for (const cell in want) { dungNonCritSeen[cell] = 1; if (!fresh(cell)) dungManualNonCrit[cell] = 1; }
    for (const cell in dungManualNonCrit)
      if (!want[cell] && !fresh(cell) && dungNonCritSeen[cell]) { delete dungManualNonCrit[cell]; delete dungNonCritSeen[cell]; }
  }
  if (Array.isArray(pd.critrooms)) {
    const wantC = {};
    for (const cell of pd.critrooms) wantC[cell] = 1;
    const freshC = cell => dungManualCritTouch[cell] && now - dungManualCritTouch[cell] < 3000;
    const freshN = cell => dungNonCritTouch[cell] && now - dungNonCritTouch[cell] < 3000;
    for (const cell in wantC) { dungManualCritSeen[cell] = 1; if (!freshC(cell)) { dungManualCrit[cell] = 1; if (!freshN(cell)) delete dungManualNonCrit[cell]; } }
    for (const cell in dungManualCrit)
      if (!wantC[cell] && !freshC(cell) && dungManualCritSeen[cell]) { delete dungManualCrit[cell]; delete dungManualCritSeen[cell]; }
  }
  // manual key promotions/demotions are OVERRIDES relayed as deltas (idx -> 1/0):
  // on = ensure critical (an existing auto reason is kept), off = demote + block the
  // auto-latch here too. Same 3s hold protects the local fresh toggle from the echo.
  const ck = pd.critkeys || {};
  for (const i in ck) {
    const ki = +i;
    if (!(ki >= 1 && ki <= 64)) continue;
    if (dungCritKeyTouch[ki] && now - dungCritKeyTouch[ki] < 3000) continue;
    if (ck[i]) { if (!dungCritKeys[ki]) dungCritKeys[ki] = DUNG_KEY_PARTY; delete dungCritKeyBlock[ki]; }
    else { delete dungCritKeys[ki]; dungCritKeyBlock[ki] = 1; }
  }
}


// client.html owns ONE shared tooltip (#global-tip): a document-level mouseover on [data-tip]
// rendering the attribute as text (white-space: pre-line, so a newline in the attribute gives
// a new line). The map uses THAT; never add a second implementation. A `title` attribute
// would become a second tooltip the day the engine honours it, so move them all onto this
// panel's own attribute after each render.
function dungStripTitles(root) {
  if (!root) return;
  const els = root.querySelectorAll('[title]');
  for (let i = 0; i < els.length; i++) {
    const el = els[i], t = el.getAttribute('title');
    el.removeAttribute('title');
    // INSIDE a map cell the ROOM's tooltip is the one worth reading. A marker with its
    // own data-tip wins closest() and replaced the room's whole reasoning with a single
    // line ("Woodcutting door"), hiding the captured level and the critical/non-essential
    // verdict  -- so markers lose the title and inherit the room's.
    if (el.closest && el.closest('.dg-cell')) continue;
    if (t && !el.getAttribute('data-tip')) el.setAttribute('data-tip', t);
  }
}
// ===========================================================================
// CRITICAL-PATH PLANNER
// ===========================================================================
// ONE objective-directed model rework). It replaces a split brain:
// a graph-based "critical path" and a separate point-scored "explore next" that
// shared no inputs and could contradict each other on screen (the pink chain
// pointing one way while the explore ring pointed another).
//
//   FACTS -> STRUCTURE -> INFERENCE -> PROOFS -> RANKING -> PLAN -> RENDER
//
// Two properties the model guarantees:
//  * everything is DERIVED FRESH from latched OBSERVATIONS (a key seen on the
//    floor, a door requirement examined, a room marked), so nothing
//    goes stale when the map opens up;
//  * the derivation runs to a FIXED POINT, so a conclusion that unlocks another
//    (key is critical -> its door room is evidence -> its approach chain -> a
//    key lying on that chain) settles in the SAME tick, not one poll per hop.
// The recommendation is simply the FIRST STEP of the winning plan, so "where to
// go next" and "where the path runs" can no longer disagree.

const DUNG_DIRS = [[1, 0, -1], [2, 1, 0], [4, 0, 1], [8, -1, 0]];   // doorBit, dx, dy (N E S W)
// ROOM SPACE. From an unmapped cell, run a ray in each of
// the four directions and count the free cells until the ray leaves the floor or hits a
// an already-known room. The sum is how much room the dungeon still has to grow THAT
// WAY. It is deliberately LOCAL and cheap -- unlike the flood-fill `area` below, which
// measures a frontier's exclusive catchment, this measures elbow room, and the two
// disagree usefully: a cell wedged between known rooms scores low even when the flood
// beyond it is huge.
function dungRoomSpace(rooms, floor, key) {
  // Same 8x8 fallback as dungFrontierArea. A ray is bounded by the floor edge or a
  // known room and NOTHING ELSE, so on a poll where the map root widget was missed
  // (floor unset) an east/south ray with no room ahead of it never terminated and the
  // panel froze inside this loop (probe.
  const cols = (floor && floor.cols) || 8, rows = (floor && floor.rows) || 8;
  const p = key.split(',').map(Number);
  let total = 0;
  for (const st of DUNG_DIRS) {
    let x = p[0] + st[1], y = p[1] + st[2];
    while (x >= 0 && y >= 0 && x < cols && y < rows && !rooms[x + ',' + y]) {
      total++; x += st[1]; y += st[2];
    }
  }
  return total;
}
// CRITICAL-ROOM BUDGET. A large floor's critical chain is always
// 19..23 rooms INCLUDING the start room and the boss room. So once k of them are
// identified, between 19-k and 23-k remain. Two things fall out of that:
//   * a frontier whose entire reachable unknown area is SMALLER than the minimum
//     remaining count cannot possibly contain the rest of the chain -- it is a side
//     branch by arithmetic, not by guesswork;
//   * when the maximum remaining hits 0 the chain must already be complete, so the
//     boss is reachable through rooms seen so far.
// Large-floor critical trees span 18..23 rooms INCLUDING the start and boss rooms.
const DUNG_CRIT_MIN = 18, DUNG_CRIT_MAX = 23;
// `critKeysHeld` = critical keys in the pack. Each one is a door that still has to be
// opened, and the room behind it is ON the chain -- so it is already accounted for
// among the remaining rooms. The rooms still to find by
// EXPLORING is therefore the remainder MINUS those. This is the `need` the expected-
// rooms figure is measured against; it was also the bound of a `fits` gate that rejected
// branches outright, removed -- a small region is less likely to hold the
// chain, not incapable of it.
// `foundOverride`: identified-room count supplied by a caller that runs BEFORE onPath
// is painted (the planner ranks first and paints after, and `rooms` is rebuilt every
// poll, so counting onPath mid-plan always came out as start+boss and the tooltip's
// need sat at ~18 all floor while the header said 13 -- probe.
function dungCritBudget(rooms, critKeysHeld, foundOverride) {
  let found = 0;
  if (foundOverride != null) found = foundOverride;
  else for (const k in rooms) {
    const r = rooms[k];
    // An unexplored room is never FOUND. The planner sets onPath on the frontier it
    // RECOMMENDS (for the pink route look), and counting that guess inflated the
    // header by one whenever a recommendation was up. A ?-room marked
    // critical stays counted: that is an identification, not a guess.
    if (r && (r.start || r.boss || (r.onPath && (!r.unex || dungManualCrit[k])))) found++;
  }
  const keyed = Math.max(0, critKeysHeld || 0);
  const min = Math.max(0, DUNG_CRIT_MIN - found);
  const max = Math.max(0, DUNG_CRIT_MAX - found);
  return { found: found, min: min, max: max, keyed: keyed,
           // still to DISCOVER: the keyed rooms are remaining, but not unknown
           unkMin: Math.max(0, min - keyed),
           unkMax: Math.max(0, max - keyed) };
}
const DUNG_OPPBIT = { 1: 4, 2: 8, 4: 1, 8: 2 };

// Mutual-door neighbours -- both rooms must carry the shared edge's door (the same
// rule the connector bars are drawn with). A room whose sprite cannot be read has
// no known exits and ends a chain.
function dungNbrs(rooms, kk) {
  const c = rooms[kk];
  if (!c || c.doors === undefined) return [];
  const p = kk.split(',').map(Number), out = [];
  for (const st of DUNG_DIRS) {
    if (!(c.doors & st[0])) continue;
    const nk = (p[0] + st[1]) + ',' + (p[1] + st[2]), n = rooms[nk];
    if (!n) continue;
    if (n.doors !== undefined && !(n.doors & DUNG_OPPBIT[st[0]])) continue;
    out.push(nk);
  }
  return out;
}
// BFS over the room graph; canEnter(cell) gates passage (locked doors, exclusions).
function dungBfs(rooms, from, canEnter) {
  const par = {}, dist = {};
  if (!rooms[from]) return { par: par, dist: dist };
  par[from] = null; dist[from] = 0;
  const q = [from];
  for (let i = 0; i < q.length; i++) {
    for (const nk of dungNbrs(rooms, q[i])) {
      if (dist[nk] !== undefined) continue;
      if (canEnter && !canEnter(nk)) continue;
      par[nk] = q[i]; dist[nk] = dist[q[i]] + 1; q.push(nk);
    }
  }
  return { par: par, dist: dist };
}
function dungPath(par, k) {
  const out = [];
  for (let c = k; c != null && par[c] !== undefined; c = par[c]) out.push(c);
  return out.reverse();
}
// Key ledger straight from observations: where each key is and which door needs it.
function dungLedger(rooms, held) {
  const led = {}, at = i => (led[i] = led[i] || { held: false, floor: null, door: null });
  held.forEach(i => { at(i).held = true; });
  for (const kk in rooms) {
    const c = rooms[kk];
    for (const gk of (c.groundKeys || [])) at(gk.idx).floor = kk;
    if (c.key) at(c.key.idx).door = kk;
  }
  return led;
}
// Rooms reachable from `from` once every key that can be picked up ALONG THE WAY is taken.
// A key never makes anything unreachable, so this greedy closure is exact -- and it
// IS the elimination model: a locked frontier whose key is nowhere on the map simply
// never enters the reachable set.
function dungReachClosure(rooms, from, led, held) {
  const keys = new Set(held);
  let out = dungBfs(rooms, from, null), grew = true, guard = 0;
  while (grew && guard++ < 12) {
    grew = false;
    out = dungBfs(rooms, from, kk => { const c = rooms[kk]; return !c.key || keys.has(c.key.idx); });
    for (const i in led) {
      const f = led[i].floor;
      if (f && out.dist[f] !== undefined && !keys.has(+i)) { keys.add(+i); grew = true; }
    }
  }
  return { reach: out, keys: keys };
}
// Unknown floor-grid area each frontier gates (ties shared) -- the prior on "the
// boss is this way" before any other evidence exists.
function dungFrontierArea(rooms, fronts, floor) {
  const area = {}, dist = {};
  for (const f of fronts) {
    area[f] = 0;
    const p = f.split(',').map(Number), d = {}, q = [[p[0], p[1], 0]];
    for (let i = 0; i < q.length; i++) {
      for (const st of DUNG_DIRS) {
        const nx = q[i][0] + st[1], ny = q[i][1] + st[2], k = nx + ',' + ny;
        if (nx < 0 || ny < 0 || (floor && (nx >= floor.cols || ny >= floor.rows))) continue;
        if (rooms[k] || d[k] !== undefined) continue;
        d[k] = q[i][2] + 1; q.push([nx, ny, q[i][2] + 1]);
      }
    }
    dist[f] = d;
  }
  // REGION = THE BOUNDING BOX OF THE FOUR RAYS. Cast a ray from the room in each
  // direction, counting free cells until a known room or the floor edge stops it, then
  //
  //     width  = left + right + 1     (the +1 is the room itself)
  //     height = up   + down  + 1
  //     region = width * height
  //
  // Exactly as specified : the shield door had 1 left, 1 right and 5
  // down -> 3 x 6 = 18; the ? had 5 left and 2 right with nothing above or below ->
  // 8 x 1 = 8. The shield door has far more area available, and this says so.
  //
  // WHY NOT THE OTHER TWO THINGS THIS HAS BEEN:
  //  - A FLOOD counts everything eventually reachable. Free space on a part-explored
  //    floor is normally one connected blob -- a strip along the top joins the block down
  //    the right by squeezing around a corner -- so every frontier scores the whole thing
  //    and none of them can be told apart (measured: 48 vs 45 on a case where the honest
  //    answer is 8 vs 18).
  //  - The LINE SUM is these same four rays ADDED, which is a perimeter, not an area: it
  //    scores a 1-wide corridor of 7 the same as a block that is 7 cells across.
  //
  // Multiplying the extents is what turns the rays into an area. It cannot leak around a
  // corner, because a box has no corners to turn.
  const cols = (floor && floor.cols) || 8, rows = (floor && floor.rows) || 8;
  for (const f of fronts) {
    const p = f.split(',').map(Number);
    const ray = (dx, dy) => {
      let n = 0;
      for (let x = p[0] + dx, y = p[1] + dy; ; x += dx, y += dy) {
        if (x < 0 || y < 0 || x >= cols || y >= rows) break;   // floor edge
        if (rooms[x + ',' + y]) break;                          // a known room
        n++;
      }
      return n;
    };
    area[f + '#pot'] = (ray(-1, 0) + ray(1, 0) + 1) * (ray(0, -1) + ray(0, 1) + 1);
  }
  const all = {};
  for (const f of fronts) for (const k in dist[f]) if (!rooms[k]) all[k] = 1;
  // Total reachable unmapped space -- the denominator for the share each frontier
  // commands. Cells reachable from TWO frontiers are counted once here but credited to
  // both above, which is correct: a room there really is reachable either way.
  area['#allcells'] = Object.keys(all).length;
  for (const k in all) {
    let bd = Infinity, own = [];
    for (const f of fronts) {
      const dd = dist[f][k];
      if (dd === undefined) continue;
      if (dd < bd) { bd = dd; own = [f]; } else if (dd === bd) own.push(f);
    }
    for (const f of own) area[f]++;
  }
  return area;
}

// The planner. Annotates `rooms` in place (onPath / critSelfWhy / critPathWhy /
// rec / need / recWhy / unex / pot) and returns the route edges + derived key
// criticality + the chosen action.
function dungPlan(rooms, ctx) {
  const startKey = ctx.startKey, held = ctx.held, floor = ctx.floor;
  const from = (ctx.playerKey && rooms[ctx.playerKey]) ? ctx.playerKey : startKey;
  const led = dungLedger(rooms, held);
  const planNow = Date.now();
  for (const kk in rooms) {
    const c = rooms[kk];
    c.unex = !!(c.bg && c.bg.length && c.bg.every(s => DUNG_UNEX_SPR.has(s)));
    if (!c.unex && c.doors && !dungOpenedAt[kk]) dungOpenedAt[kk] = planNow;
    c.onPath = false; c.critSelfWhy = null; c.critPathWhy = null;
    c.rec = false; c.need = false; c.recCrit = false; c.recWhy = null; c.pot = 0; c.lowVia = false;
    c.deadBranch = false; c.refuted = false; c.keyFrom = null; c.keyFromAssumed = false;
  }

  // ===========================================================================
  // STAGE A -- FACTS. Latched observations only, nothing derived: which key
  // came out of which room (keyCameFrom / keyFrom), the boss and start cells,
  // and which persisted criticality latches count as FACTS. Manual marks
  // (dungManualCrit / dungManualNonCrit), door readings (dungDoorLevels) and
  // the key-door latch (dungKeyDoor) are module state read directly by the
  // stages below.
  // ===========================================================================
  // ---- STRUCTURAL REFUTATION FIRST (rewrite, : "there was no key in
  // that room? it wasnt critical?") ----
  // The model's core split: FACTS (a key was taken here, a critical key's door, the
  // boss, your own mark) versus INFERENCES (a near-level door reading, a top-tier
  // resource -- priors on where the chain runs). Only facts may latch as proofs;
  // an inference must RETRACT the moment structure disproves it. Grading a door reading
  // "confirmed" and latching it HARD leaves an opened level-93 door onto one empty fishing
  // dead end stuck
  // "a critical room" forever. Refutation is therefore computed FIRST, from the map
  // alone, and the evidence pass consults it.
  // An ASSUMED source (key ring flipped while a teammate picked it up elsewhere) may
  // not argue anything, vetoes included -- it may be naming the wrong room entirely.
  //
  // EXCEPT ON A SOLO FLOOR. The whole distrust exists because the shared key ring also
  // flips on a TEAMMATE's pickup; with nobody else in the instance, a ring flip while
  // standing in a room PROVES the pickup happened there. Keeping the distrust solo made
  // the yellow-pentagon room read "Spur --... with no key" while its own tooltip said
  // a key entered the ring there, and the room dropped off the path.
  const soloFloor = Object.keys(dungPartyRoster).length <= 1;
  const srcUsable = sc => sc && typeof sc.x === 'number' && (!sc.assumed || soloFloor);
  // Key origins (also feeds the render: keyFrom = what the room contained).
  const keyCameFrom = {};
  for (const si in dungKeySrc) {
    const sc = dungKeySrc[si];
    if (sc && typeof sc.x === 'number') {
      const cc = ctx.cellOfWorld(sc.x, sc.y);
      // keyFrom = what the room CONTAINED, fine to report from an assumed source.
      // keyCameFrom = an argument that you HAD to come here, which an assumed source
      // cannot support, because it may be naming the room a teammate was standing in.
      if (cc) {
        if (rooms[cc]) { rooms[cc].keyFrom = si; rooms[cc].keyFromAssumed = !!sc.assumed; }
        if (!sc.assumed || soloFloor) keyCameFrom[cc] = si;   // solo: the flip IS proof
      }
    }
  }
  // The boss and start cells -- anchor facts for every stage below.
  let bossCell = null, startCell = null;
  for (const kk in rooms) {
    if (rooms[kk].boss) bossCell = kk;
    if (rooms[kk].start) startCell = kk;
  }
  // A surviving FACT latch also protects: a hard latch is a point fact whose
  // observation may have vanished between polls (key-source lost -- the
  // rule) and structure must not prune it. Old builds also latched DOOR INFERENCES
  // hard ("the level-NN <skill> door room") and persisted them; the new code never
  // latches that why-string, so its shape identifies a stale inference latch, which
  // gets NO protection and is cleared below once its branch is proven empty.
  const staleInfLatch = kk => {
    const w = dungCritLatch[kk] ? String(dungCritLatch[kk]) : '';
    return !!w && (w[0] === '~' || /^the level-\d+ /.test(w));
  };
  const factLatch = kk => !!dungCritLatch[kk] && !staleInfLatch(kk);

  // ===========================================================================
  // STAGE B -- STRUCTURE. Pure map geometry -- no evidence is weighed here:
  // start-rooted BFS (sTop), the settled boss route (routeSet), the filler
  // shadow (lowVia) + key filler veto, the finished-and-empty branch fixpoint
  // (deadBranch) with refuted flags, the frontier list, the reachability
  // closure (cl / live) and each frontier's region (area).
  // ===========================================================================
  // Topological BFS from the START room: the floor's own structure, independent of
  // where the player happens to be standing. Used to judge which frontier a piece of
  // evidence actually gates (see the ranking below); the room graph doesn't change
  // between passes, so it is computed once.
  const sTop = dungBfs(rooms, (startKey && rooms[startKey]) ? startKey : from, null);
  // THE SETTLED ROUTE, computed UP FRONT (it needs only the room graph).
  let routeSet = null;
  // One start-rooted, lock-blind BFS, shared by the settled route and the fetch chains below,
  // so the settled route obeys the same MUTUAL-door rule as every other traversal.
  const sFull = startCell ? dungBfs(rooms, startCell, null) : null;
  if (bossCell && sFull && sFull.dist[bossCell] !== undefined) {   // boss actually reachable
    routeSet = {};
    for (let c = bossCell; c != null; c = sFull.par[c]) { routeSet[c] = 1; if (c === startCell) break; }
  }
  // FILLER SHADOWS WHAT IT GATES. A room whose lowest-tier resource is filler is side
  // content, and so is anything you can only reach THROUGH it -- generated floors keep
  // filler with filler ("the room(s) beyond it should have been also
  // marked as non-critical"). They have to be MARKED, not merely deprioritised, or the
  // map still shows them as ordinary unexplored rooms worth visiting.
  // FILLER GATES EVERYTHING BEHIND IT -- computed FIRST, because it is a VETO and a
  // veto has to be in hand before any evidence is weighed, or a room can be marked critical
  // on key evidence and only then be found to sit behind filler.
  // Reachable from start WITHOUT entering a filler room; anything known but not
  // reachable that way sits behind one.
  // Rooted at the start like sTop, with the same player-room fallback: the ranking's
  // filler veto reads lowVia (see the grade), so a floor whose start room was misread
  // must not silently lose the veto altogether.
  const cleanRoot = (startKey && rooms[startKey]) ? startKey : (rooms[from] ? from : null);
  if (cleanRoot) {
    const clean = dungBfs(rooms, cleanRoot,
                          kk => !(rooms[kk] && rooms[kk].resLow)).dist;
    for (const kk in rooms) {
      const r = rooms[kk];
      if (!r || r.start || r.boss || dungManualCrit[kk]) continue;
      if (clean[kk] === undefined) r.lowVia = true;   // only reachable via filler
    }
  }
  // A KEY IS NOT CRITICAL UNTIL PROVEN. Every automatic derivation has now been removed
  // (repeatedly). They all reduced to "this key is needed to get past the
  // next door", which is a fact about the CURRENT frontier, not about the boss chain --
  // and on a barely-explored floor every room is trivially a cut vertex, so they fired on
  // essentially every key. The panel had 4 keys flagged critical at 5/64 rooms explored.
  // What remains: dungCritKeys, i.e. keys YOU promoted or a party mate relayed. The
  // planner still fetches keys it needs (that is the `needkey` action and does not depend
  // on criticality) -- it just stops CALLING them critical.
  // A KEY FOUND BEHIND FILLER CANNOT BE CRITICAL. Not a weighting, a structural
  // impossibility: if the only way to that key is through a filler room, then the key is
  // off the path, whatever anyone has marked it ("it literally cant be
  // critical as it lives behind a low level resource"). This OVERRIDES party marks,
  // because a mate's mark may itself be a guess -- but NOT the player's OWN promotion:
  // a hand mark on this floor is the player overruling the prior, same as dungManualCrit
  // overrules lowVia on a room. (Before, the chip said "critical" while the planner
  // silently ignored it -- the two must not disagree without saying so.)
  const keyBehindFiller = {};
  for (const si in dungKeySrc) {
    const sc = dungKeySrc[si];
    if (!srcUsable(sc)) continue;
    if (dungCritKeys[si] === DUNG_KEY_MANUAL) continue;
    const cc = ctx.cellOfWorld(sc.x, sc.y);
    const cr = cc && rooms[cc];
    if (cr && (cr.lowVia || cr.resLow) && !dungManualCrit[cc]) keyBehindFiller[si] = 1;
  }
  dungKeyFillerVeto = keyBehindFiller;   // published so the key chips can SAY it is vetoed
  // Same rule as dungKeyIsCrit, plus the filler veto above.
  const isCritKey = i => !keyBehindFiller[i] && dungKeyIsCrit(i);
  // FINISHED-AND-EMPTY BRANCHES, to a fixpoint (backwards prune). A room is proven
  // OFF the chain when everything beyond it is explored and yielded nothing: not the
  // start/boss/your mark, no locked door, no key in it, no key reported from it (any
  // source, assumed included -- pruning a possible key room is worse than keeping a
  // spur), and no door to undiscovered space (a frontier is unfinished business).
  // Purely structural -- evidence and latches deliberately have NO say here: this
  // verdict is exactly the "strong evidence" that demotion requires, and it is what
  // retracts a refuted door/resource prior. A prune that skipped evidenced and latched
  // rooms could never take one off the path.
  const deadBranch = {};
  for (let dp = 0; dp < 64; dp++) {
    let changed = false;
    for (const kk in rooms) {
      const c = rooms[kk];
      if (!c || deadBranch[kk] || c.boss || c.start || dungManualCrit[kk] || factLatch(kk)) continue;
      if (c.unex || !c.doors) continue;               // no verdict without data
      if (c.key || (c.groundKeys && c.groundKeys.length) || c.keyFrom || keyCameFrom[kk]) continue;
      // "No key here" is only trusted once the room has been open a moment: the map
      // shows the room a few frames before the ground scan can report its key, and
      // pruning in that window would eat a possible key room.
      if (!dungOpenedAt[kk] || planNow - dungOpenedAt[kk] < 1200) continue;
      const p0 = kk.split(',').map(Number);
      let live = 0, frontier = false;
      for (const st of DUNG_DIRS) {
        if (!(c.doors & st[0])) continue;
        const n = rooms[(p0[0] + st[1]) + ',' + (p0[1] + st[2])];
        if (!n || n.unex || !n.doors) { frontier = true; break; }     // more to find
        if (!deadBranch[(p0[0] + st[1]) + ',' + (p0[1] + st[2])]) live++;
      }
      if (frontier) continue;
      if (live <= 1) { deadBranch[kk] = true; c.deadBranch = true; changed = true; }
    }
    if (!changed) break;
  }
  // Rooms whose criticality PRIOR the prune just disproved -- the render says
  // "retracted" instead of repeating the refuted claim.
  for (const kk in deadBranch) {
    const c = rooms[kk];
    if (c && (dungDoorBand(dungDoorLevels[kk]) === 'critical'
              || (c.res && c.res.band === 'critical'))) c.refuted = true;
  }
  // THE BOSS ON THE MAP SETTLES EVERY UNKNOWN. The boss icon only appears once the
  // room has been walked into, and walking in means the whole critical tree between
  // start and boss was ALREADY opened: any room still unopened now (locked doors
  // included) hangs off the path, and every critical key was already spent. So:
  //  - unopened rooms join the dead-branch set (manual promotions and fact latches
  //    stay the player's overrule, as everywhere);
  //  - solo (where the key ring is trustworthy), a key whose door was never even
  //    SEEN cannot gate the route: veto its crit mark like a filler key. Party
  //    floors keep the mark: a mate may have seen the door this client never did.
  if (bossCell) {
    for (const kk in rooms) {
      const c = rooms[kk];
      if (!c || c.boss || c.start || dungManualCrit[kk] || factLatch(kk)) continue;
      if (!(c.unex || c.key)) continue;   // opened rooms keep their own verdicts
      deadBranch[kk] = true; c.deadBranch = true;
    }
    if (soloFloor) for (const si in dungKeySrc) {
      if (dungCritKeys[si] === DUNG_KEY_MANUAL) continue;
      if (!dungKeyDoor[si] && !(led[si] && led[si].door)) keyBehindFiller[si] = 1;
    }
  }

  // OBJECTIVES -- the boss once found, otherwise every live frontier.
  const bossKey = bossCell;   // the boss, once mapped, is the only objective
  const fronts = [];
  for (const kk in rooms) {
    if (rooms[kk].boss) continue;   // the destination, never a frontier
    // A room still showing a LOCKED DOOR is unexplored space whatever its background
    // sprite says (the map only draws the lock while it is shut). Keying frontiers on
    // the sprite alone meant a room whose key is held could never be the objective,
    // so the planner sent the player exploring a level-45 skill door instead.
    if ((rooms[kk].unex || rooms[kk].key) && !dungManualNonCrit[kk]) fronts.push(kk);
  }
  const cl = dungReachClosure(rooms, from, led, held);
  const cands = bossKey ? [bossKey] : fronts;
  const live = cands.filter(t => cl.reach.dist[t] !== undefined);
  // rooms opening directly off the one the player is standing in
  const adjToPlayer = new Set(rooms[from] ? dungNbrs(rooms, from) : []);
  const planDiag = { rooms: Object.keys(rooms).length, fronts: fronts.length, live: live.length,
               from: from || '-' };
  const area = fronts.length ? dungFrontierArea(rooms, fronts, floor) : {};

  // ===========================================================================
  // STAGE C -- INFERENCE (the evidence pass): why a room matters in its own
  // right. evid = why-string, evStr = ranking strength (2 confirmed / 1 prior),
  // evFact = latchable proof. Strength and factness are separate AXES: a read
  // near-level door RANKS as confirmed but is an inference and must retract;
  // only facts may latch.
  // ===========================================================================
  const evid = {}, evStr = {}, evFact = {};
  // ASSUMED sources are excluded ON PARTY FLOORS: the shared key ring flips on a
  // TEAMMATE's pickup too, so an assumed source may be naming whatever room this client's
  // player was standing in. Solo, the flip is proof (srcUsable).
  const srcCells = {};
  for (const si in dungKeySrc) {
    if (!isCritKey(si) || !srcUsable(dungKeySrc[si])) continue;
    const cell = ctx.cellOfWorld(dungKeySrc[si].x, dungKeySrc[si].y);
    if (rooms[cell]) srcCells[cell] = dungKeyName(+si) || 'key';
  }
  // THE DOOR A CRITICAL KEY OPENED STAYS EVIDENCE. `c.key` exists only while the door
  // is SHUT, but the usual discovery order is: use the key, explore beyond, THEN
  // realise the key was critical and promote it -- at which point the marker is gone
  // and the "door needing the critical key" branch below can never fire. dungKeyDoor
  // latched the cell while it was shut; read it, same rule as dungDoorLevels
  // ("door levels outlive the marker", ).
  const critDoorCell = {};
  for (const i in dungKeyDoor) {
    if (isCritKey(i) && rooms[dungKeyDoor[i]]) critDoorCell[dungKeyDoor[i]] = dungKeyName(+i) || 'key';
  }
  // Evidence is GRADED, not just counted : a captured door
  // requirement, a critical key or the boss is something that can be CONFIRMED about this
  // floor; a top-tier resource is only a prior on where content was generated. A
  // confirmed room therefore outranks any number of resource rooms -- counting them
  // equally let three distant T10s outvote a confirmed 101 Mining door next door.
  for (const kk in rooms) {
    const c = rooms[kk];
    if (dungManualNonCrit[kk]) continue;
    // str: 2 = ranks as confirmed, 1 = ranks as a prior. fact: latchable proof --
    // ONLY facts survive structural disproof; str and fact are separate axes (a read
    // near-level door RANKS high but is still an inference and must retract).
    let why = null, str = 2, fact = true;
    if (dungManualCrit[kk]) why = 'a room you marked critical';
    else if (c.boss) why = 'the boss room';
    else if ((c.groundKeys || []).some(gk => isCritKey(gk.idx))) why = 'a critical key lying here';
    else if (srcCells[kk]) why = 'the room that held the critical ' + srcCells[kk];
    else if (c.key && isCritKey(c.key.idx)) {
      // Every critical key is human-marked now, so this is always confirmed evidence.
      why = 'the door needing the critical ' + c.key.name;
    }
    else if (critDoorCell[kk]) {
      //...and the same door AFTER it was opened, read from the latch (see above).
      why = 'the door that needed the critical ' + critDoorCell[kk];
    }
    else if (!deadBranch[kk]) {
      // INFERENCES -- priors on where the chain was generated, refutable by
      // exploration. A finished-and-empty branch (deadBranch) gets NO inference
      // evidence at all: the level-93 Construction door hiding one T10 fishing
      // dead end must read as retracted, not "a critical room".
      fact = false;
      const dl = dungDoorLevels[kk];
      if (dungDoorBand(dl) === 'critical')
        why = 'the level-' + dl.level + ' ' + dl.skill + ' door room';
      else if (c.res && c.res.band === 'critical' && !c.resLow
               && dungResPathworthy(c, kk, rooms)) {
        why = 'a critical ' + c.res.skill + ' T' + c.res.tier + ' room'; str = 1;
      }
    }
    if (why) { evid[kk] = why; evStr[kk] = str; if (fact) evFact[kk] = 1; }
  }

  // ===========================================================================
  // STAGE D -- PROOFS: forced keys and must-visit rooms. Combines STRUCTURE
  // with INFERENCE (the confirmed set is evStr>=2 evidence plus the latch),
  // which is why this stage sits after the evidence pass. Products: forcedKey /
  // mandatoryKey / mandatorySrc and must / mustWhy.
  //
  // PROVENANCE DOES NOT MAKE A KEY CRITICAL. Not the start room, not a must-pass
  // room, not an evidenced room (twice). All three describe places
  // you would walk through anyway, so finding a key there says nothing about
  // whether its DOOR is on the route. Every version of "it was lying in X" is a
  // hint about the key's origin, not a fact about its destination.
  // What IS sound is structural: a key is FORCED when the floor cannot be
  // continued without it or its door sits on the settled route -- that is the
  // forcedKey fixpoint below, a property of connectivity, not an inheritance.
  // (Two derivation loops and an unread srcNote/srcOnly tooltip map implemented
  // the unsound version until. The tooltip provenance shown
  // is room.keySrc / keyFrom, built in the render.)
  //
  // Nothing derives criticality (see dungKeyIsCrit), and the one sound cascade -- forced
  // keys -- reaches its own fixpoint right here, so the planner is single-pass.
  // ===========================================================================
  const must = {};           // cell -> 1: cuts the start from every objective / a proof
  const mustWhy = {};        // dominator rooms' tooltip why (written in RENDER)
  const mandatoryKey = {};   // key idx -> 1: nothing is reachable without it
  const mandatorySrc = {};   // cell -> 1: yielded such a key, so the trip there was forced
  const forcedKey = {};      // key idx -> 'blocks' | 'route': proven forced, either way
  // WHICH KEYS ARE MANDATORY -- proven, not inferred. A key is mandatory when removing
  // it from the obtainable set leaves NO live frontier reachable: the floor cannot be
  // continued without it. That makes the trip to fetch it forced, and the room it came
  // out of is therefore on the critical path ("the green corner is
  // the only path forward... so the room containing the key is also critical").
  //
  // This is provenance flowing BACKWARDS, the only direction it flows: it says the key
  // had to be COLLECTED, never that the door it opens leads anywhere critical.
  //
  // NOT the `must` cut-vertex test, which was tried for this and removed: on a barely
  // explored floor almost every room is a cut vertex, so it fired on nearly every key.
  // Reachability-without-the-key is exact and cannot over-fire that way -- if anything
  // else is still reachable, the key simply is not proven necessary yet.
  const reachWithout = excl => {
    const keys = new Set();
    held.forEach(i => { if (i !== excl) keys.add(i); });
    // RE-LOCK THE DOOR THIS KEY OPENED. Opening a door clears its requirement, so on
    // the CURRENT graph removing an already-spent key changes nothing and it looks
    // optional -- the test could only ever catch doors still shut (a
    // room whose key opened the way to a confirmed-critical room was marked a spur).
    // dungKeyDoor latched that door while it was still locked, which is exactly the
    // historical fact needed to ask "was there a route WITHOUT this key".
    const relock = dungKeyDoor[excl] || null;
    const gate = kk => {
      if (relock && kk === relock && !keys.has(excl)) return false;
      const c = rooms[kk];
      return !c.key || keys.has(c.key.idx);
    };
    // ROOTED AT THE START ROOM, not the player. The question is historical -- "could
    // the floor have been continued without this key" -- and the answer must not
    // depend on which side of the re-locked door the player happens to stand. Rooted
    // at the player, a key spent on the only way onward was never proven mandatory:
    // once you walked through, everything ahead stayed reachable FROM YOU, and the
    // whole fetch chain behind you stayed unmarked (the "later discovered to be
    // critical" case, ). Falls back to the player only when the start
    // room was never read, same as sTop.
    const structFrom = (startKey && rooms[startKey]) ? startKey : from;
    let out = dungBfs(rooms, structFrom, gate);
    for (let g = 0; g < 12; g++) {
      let grew = false;
      for (const i in led) {
        const f = led[i].floor;
        if (f && +i !== excl && out.dist[f] !== undefined && !keys.has(+i)) { keys.add(+i); grew = true; }
      }
      if (!grew) break;
      out = dungBfs(rooms, structFrom, gate);
    }
    return out;
  };
  // Rooms carrying CONFIRMED evidence (a near-level skill door and the like). Reaching
  // one of these is not optional, so a key without which one becomes unreachable was
  // not optional either.
  const confirmed = [];
  for (const kk in evStr) if (evStr[kk] >= 2 && rooms[kk] && cl.reach.dist[kk] !== undefined) confirmed.push(kk);
  //...plus every room PROVEN earlier this floor (the latch). A proof does not
  // expire when its evidence stops being observable, and a key that gated a latched
  // room was exactly as forced as one gating live evidence -- without this, the
  // orange-triangle key that "revealed that room up north that is critical" read
  // (not critical) and its source room dropped off the path.
  for (const kk in dungCritLatch) if (rooms[kk] && confirmed.indexOf(kk) < 0) confirmed.push(kk);
  // FORCED KEYS, TO A FIXPOINT. Two ways a key is proven forced:
  //  - 'route': the boss is known and the door this key opened sits on the settled
  //    route (routeSet, computed up top). Proven by the route, no BFS needed.
  //  - 'blocks': without it, nothing is left (blocksAll) or confirmed-critical
  //    content is unreachable (blocksCrit).
  // Each proven key's OBSERVED source room is itself confirmed-critical content
  // ("the trip there was forced"), so it joins the confirmed set and the loop
  // re-runs: that is what lets a CHAIN of keys settle -- key A's door is on the
  // route, fetching A needed key B, so B is forced too and B's source room marks.
  // Single-shot, this never cascaded and the earlier rooms stayed unmarked
  // ("a key that's later discovered to be critical").
  // Bounded: each round either proves at least one new key (<= 64) or stops.
  if (rooms[(startKey && rooms[startKey]) ? startKey : from]) {
    const confSet = new Set(confirmed);
    for (let guard = 0; guard < 8; guard++) {
      let grew = false;
      for (const si in dungKeySrc) {
        if (forcedKey[si]) continue;
        const sc = dungKeySrc[si];
        const srcCc = srcUsable(sc) ? ctx.cellOfWorld(sc.x, sc.y) : null;
        const dk = dungKeyDoor[si];
        // 'route' also covers a door latched onto any PROVEN-critical room, boss known
        // or not: a critical lock forces the fetch, even though a critical fetch never
        // says anything about the lock (critical rooms drop bonus keys too).
        if (dk && ((routeSet && routeSet[dk]) || confSet.has(dk))) forcedKey[si] = 'route';
        else {
          if (!live.length && !confSet.size) continue;
          const r = reachWithout(+si);
          // (a) nothing left to explore without it -- the still-locked case
          const blocksAll = live.length && !live.some(t => r.dist[t] !== undefined);
          // (b) it was the way to confirmed-critical content -- the already-spent
          //     case. A key's OWN source cell is excluded from its test: "this key
          //     gates the room it lay in" would be self-justifying.
          let blocksCrit = false;
          confSet.forEach(kk => { if (kk !== srcCc && r.dist[kk] === undefined) blocksCrit = true; });
          if (!(blocksAll || blocksCrit)) continue;
          forcedKey[si] = 'blocks';
          mandatoryKey[+si] = 1;   // only 'blocks' keys: the tooltip says "nothing is
                                   // reachable without it", which 'route' cannot claim
        }
        grew = true;
        if (srcCc && rooms[srcCc]) { mandatorySrc[srcCc] = 1; confSet.add(srcCc); }
      }
      if (!grew) break;
    }
    // A PROVEN-CRITICAL DEAD END THAT YIELDED A KEY exists to fetch that key: the
    // branch reaches nothing else, so the key is critical (a side-branch terminus).
    // Purely structural, so it may derive key criticality without a human mark; a
    // hand demotion still silences it, and it is never relayed as a mark.
    for (const kk in keyCameFrom) {
      const si = keyCameFrom[kk];
      if (dungDerivedCritKeys[si] || keyBehindFiller[si] || dungCritKeyBlock[si]) continue;
      const c = rooms[kk];
      if (!c || c.unex || !c.doors) continue;
      const proven = dungManualCrit[kk] || factLatch(kk) || mandatorySrc[kk] || (evStr[kk] || 0) >= 2;
      if (!proven) continue;
      const par = sTop.par[kk];
      const p0 = kk.split(',').map(Number);
      let open = false, frontier = false;
      for (const st of DUNG_DIRS) {
        if (!(c.doors & st[0])) continue;
        const nk = (p0[0] + st[1]) + ',' + (p0[1] + st[2]);
        const n = rooms[nk];
        if (!n || n.unex || !n.doors) { frontier = true; break; }
        if (nk !== par && !deadBranch[nk]) open = true;
      }
      if (frontier || open) continue;
      dungDerivedCritKeys[si] = 'fetched from a proven critical dead end';
    }
  }
  // MUST-VISIT -- rooms whose removal cuts the start off from EVERY live
  //    objective. Topological, so locked rooms still count as gates. Runs BEFORE the
  //    ranking, because it depends only on the room graph and
  //    `live`, and the budget's found count below needs it in hand.
  if (startKey && rooms[startKey]) {
    const reachSkip = skip => dungBfs(rooms, startKey, kk => kk !== skip).dist;
    const base = reachSkip(null);
    const liveB = live.filter(t => base[t] !== undefined);
    // DOMINATORS OF PROOFS ARE PROOFS (a boss beside the start left
    // "9 found" -- scattered proven rooms with none of the corridors every route to
    // them must cross). If removing a room cuts the start off from a PROVEN room,
    // every walk to that proof passed through it: a forced traversal, latched and
    // counted like any other proof. It cannot over-fire: it only ever anchors on rooms
    // that are ALREADY proofs.
    const provenB = [];
    for (const kk in rooms) {
      if (base[kk] === undefined || rooms[kk].start) continue;
      if ((evStr[kk] || 0) >= 2 || dungCritLatch[kk] || mandatorySrc[kk] || dungManualCrit[kk] || rooms[kk].boss)
        provenB.push(kk);
    }
    // Key SOURCE rooms are proofs too (backwards provenance): the trip happened, so
    // the corridors every walk to them crossed are forced as well.
    for (const si in dungKeySrc) {
      const sc2 = dungKeySrc[si];
      if (!srcUsable(sc2)) continue;
      const cc2 = ctx.cellOfWorld(sc2.x, sc2.y);
      if (cc2 && rooms[cc2] && base[cc2] !== undefined && !rooms[cc2].start && provenB.indexOf(cc2) < 0)
        provenB.push(cc2);
    }
    // CAPACITY FORCING (large floors only): the critical tree spans at least
    // DUNG_CRIT_MIN rooms INCLUDING start and boss. If avoiding a room leaves fewer
    // than that many rooms even after crediting every unmapped cell the remainder
    // could still open into, the path cannot fit around it: proven critical.
    // Unmapped cells flood-fill into 4-connected regions once; a region adjacent to
    // several frontier rooms is credited to each rather than partitioned. The
    // over-count is deliberate: it only ever makes the rule fire LESS often.
    const bigFloor = floor && floor.cols > 4 && floor.rows > 4;
    let regionOf = null, regionSize = null;
    if (bigFloor) {
      regionOf = {}; regionSize = [];
      for (let ry = 0; ry < floor.rows; ry++) for (let rx = 0; rx < floor.cols; rx++) {
        const k0 = rx + ',' + ry;
        if (rooms[k0] || regionOf[k0] !== undefined) continue;
        const id = regionSize.length; let n = 0; const q = [[rx, ry]];
        regionOf[k0] = id;
        for (let i = 0; i < q.length; i++) {
          n++;
          for (const st of DUNG_DIRS) {
            const nx = q[i][0] + st[1], ny = q[i][1] + st[2], nk2 = nx + ',' + ny;
            if (nx < 0 || ny < 0 || nx >= floor.cols || ny >= floor.rows) continue;
            if (rooms[nk2] || regionOf[nk2] !== undefined) continue;
            regionOf[nk2] = id; q.push([nx, ny]);
          }
        }
        regionSize.push(n);
      }
    }
    if (liveB.length || provenB.length) for (const kk in rooms) {
      if (kk === startKey || rooms[kk].start || dungManualNonCrit[kk] || base[kk] === undefined) continue;
      const d2 = reachSkip(kk);
      if (liveB.length && liveB.every(t => t === kk || d2[t] === undefined)) {
        must[kk] = 1;
        // (was: the key of this room's door derived critical from "only way onward".
        //  Removed -- on a barely-explored floor every room is a cut vertex, so it
        //  fired on essentially every key.)
      }
      if (!must[kk] && provenB.some(t => t !== kk && d2[t] === undefined)) {
        must[kk] = 1;
        mustWhy[kk] = 'the only way to a proven room';   // written in RENDER (stage G)
      }
      if (bigFloor && !must[kk] && !deadBranch[kk]) {
        let potential = 0; const regs = new Set();
        for (const rk in d2) {
          potential++;
          const rc = rooms[rk];
          // Frontier rooms (unexplored, or still showing a locked door) may open into
          // adjacent unmapped space; credit those regions to the survivor count.
          if (!(rc && (rc.unex || rc.key))) continue;
          const rp = rk.split(',').map(Number);
          for (const st of DUNG_DIRS) {
            const rid = regionOf[(rp[0] + st[1]) + ',' + (rp[1] + st[2])];
            if (rid !== undefined) regs.add(rid);
          }
        }
        regs.forEach(id => { potential += regionSize[id]; });
        if (potential < DUNG_CRIT_MIN) {
          must[kk] = 1;
          mustWhy[kk] = 'the floor cannot fit the critical path around it';
        }
      }
    }
  }

  // ===========================================================================
  // STAGE E -- RANKING (kept verbatim -- rewritten and confirmed).
  // Products: objective / objWhy / tied, plus the sticky-path hold.
  // ===========================================================================
  let objective = null, objWhy = '', tied = {};
  // PICK ONE -- ranked, never summed: DISCRIMINATING evidence, then unknown
  //    region, then elbow room.  (Distance never ranks; see below.)
  //
  //    "Discriminating" is the subtle part. Routes to different frontiers share a
  //    prefix, and evidence on the shared part says nothing about WHICH way to go.
  //    Counting all evidence on the route made the FURTHEST frontier win purely by
  //    having more rooms behind it, and made the answer flip every time the player
  //    moved (live). So: take routes from the START room (a
  //    structural property of the floor, stable as the player walks) and count
  //    evidence only on the stretch EXCLUSIVE to that frontier.
  //
  //    DISTANCE IS NOT A TERM. Where the critical path runs is a property of the
  //    floor, not of where the player is standing ("distance is
  //    totally irrelevant for critical pathing"). It is still REPORTED in the
  //    tooltip, because trip length is worth knowing, but it never ranks. Unknown
  //    area only breaks ties between similarly-distant options: a central frontier
  //    gates more unknown cells than a corner one, so ranking area above distance
  //    quietly sent the player to the far side of the floor (live).
  //    Distance is bucketed so a room or two of difference doesn't jitter the
  //    answer while walking.
  const routeOf = {}, shared = {};
  for (const t of live) {
    const rs = dungPath(sTop.par, t);
    routeOf[t] = rs;
    for (const pk of rs) shared[pk] = (shared[pk] || 0) + 1;
  }
  // "Exclusive" = evidence sitting on a room that ONLY this frontier's route passes
  // through, so it cannot be claimed by a rival frontier. Also returns WHAT that
  // evidence is and WHERE, because "1 exclusive evidence" on its own is unauditable
  // ("what exactly is the exclusive evidence").
  const evOf = t => {
    let s = 0, n = 0, why = '', at = '';
    for (const pk of (routeOf[t] || [])) {
      if (!evid[pk] || shared[pk] !== 1) continue;
      // EVIDENCE IS SPENT ONCE THE ROOM IS REACHED. A level-101 door tells you the
      // room BEHIND IT is worth reaching -- it does not say which of that room's exits
      // continues the path. Once the room is explored, the door has been answered,
      // and the room's own criticality may already be explained by a continuation that
      // have ALREADY found (the Invention room may have been critical
      // to reach the green door, not the '?' beyond it). Counting it again for a
      // frontier further along is the same forwards-inheritance error as the keys.
      // So only UNEXPLORED rooms carry evidence forward: the frontier itself, and
      // anything unexplored still on its route.
      // EXPLORED is read from the map SPRITE (`unex`), not from door bits. Testing
      // `doors !== undefined` treated a known-but-unwalked room as explored, because
      // those carry doors = 0 -- so a frontier's OWN evidence was thrown away, and a
      // level-85 Prayer door with the party at 93 (guaranteed critical) came out at
      // grade 0 and lost to an unevidenced ?. Same root cause as the
      // dead-end prune.
      if (rooms[pk] && !rooms[pk].unex) continue;
      n++;
      if (evStr[pk] > s) { s = evStr[pk]; why = evid[pk]; at = pk; }
    }
    return { s: s, n: n, why: why, at: at };
  };
  // Budget first: how many critical rooms can still be out there. `budget.min` is the
  // FEWEST that must remain, so any branch with less unknown space than that cannot
  // hold the rest of the chain.
  let critHeld = 0;
  held.forEach(idx => { if (isCritKey(idx)) critHeld++; });
  // FOUND, counted from what is identified RIGHT NOW: confirmed-evidence rooms,
  // must-pass rooms, and the chains linking them back to the start -- the same set
  // the presentation will paint. dungCritBudget's own onPath count cannot work here
  // (onPath is painted after the ranking, and `rooms` is rebuilt every poll), so it
  // always came out as start+boss and `need` sat at ~18 for the whole floor while
  // the header said 13 (probe. Unexplored rooms count only when the
  // marked them -- locating a critical door is not FINDING its room -- which
  // is the same reading the header uses.
  const idSet = {};
  const ided = kk => { const r = rooms[kk]; if (r && (!r.unex || dungManualCrit[kk])) idSet[kk] = 1; };
  if (startKey && rooms[startKey]) idSet[startKey] = 1;
  // IDENTIFIED = PROVEN, never the connecting chains. The chains are the current
  // best geometry, and counting them made "found" COLLAPSE when they rerouted
  // (6 -> 3 in one poll, ). Proofs latch, so this count is monotone
  // within a floor -- it can only grow toward the 19-23 budget, exactly like the
  // real information does. The chains still paint pink below; they just don't
  // claim to be identified rooms.
  for (const kk in evStr) if (evStr[kk] >= 2) ided(kk);
  for (const kk in must) ided(kk);
  for (const kk in dungCritLatch) ided(kk);
  for (const kk in dungManualCrit) ided(kk);
  // Key-source rooms count (backwards provenance: the trip to fetch a key happened,
  // so its room is identified critical -- five held keys left five source rooms
  // uncounted,  "still only 12").
  for (const si in dungKeySrc) {
    const sc3 = dungKeySrc[si];
    if (srcUsable(sc3)) { const cc3 = ctx.cellOfWorld(sc3.x, sc3.y); if (cc3) ided(cc3); }
  }
  // The settled boss walk IS proven geometry once the boss is on the map -- it is
  // what made the end-of-floor "20 found - 0-3 to go" read right.
  if (routeSet) for (const kk in routeSet) ided(kk);
  const budget = dungCritBudget(rooms, critHeld, Object.keys(idSet).length);
  // CHAIN CONTINUATION : a frontier at the tip of a run of PROVEN
  // rooms is likelier to continue the chain. The evidence STACKS with the length --
  // 2 in a row is a small nudge, 3 larger, 4 larger still, and so on (not a
  // 3+ threshold) -- so this is the UNCAPPED consecutive-proven count and the
  // lexicographic term discriminates a 6-run from a 2-run. Ranked BELOW grade: a run
  // is still an inference, so it never outranks a READ door or overrides the filler
  // veto -- it decides among frontiers with no hard evidence of their own, the
  // "? at the end of the pink row" case.
  const provenRank = kk => (evStr[kk] || 0) >= 2 || must[kk] || dungCritLatch[kk]
                        || mandatorySrc[kk] || dungManualCrit[kk];
  const chainOf = t => {
    const rs = routeOf[t] || [];
    let n2 = 0;
    for (let i2 = rs.length - 1; i2 >= 0; i2--) {
      const pk = rs[i2];
      if (pk === t) continue;                    // the frontier itself
      if (!provenRank(pk)) break;
      n2++;
    }
    return n2;
  };
  // THE CRITICAL PATH IS (TYPICALLY) THE LONGEST PATH : the
  // generator lays the 19-23 chain as the floor's longest start-to-boss walk, so a
  // frontier DEEPER on its start-route is structurally likelier to be the chain's
  // continuation. Depth is start-rooted (sTop), so unlike the banned player-distance
  // rule it is a property of the floor and never drifts as you walk. Ranked below
  // chain/continues (local proof beats a global prior, and the finish-your-branch
  // behaviour stays) but ABOVE region: extending the longest known path beats the
  // bigger empty box.
  // STICK WITH THE PATH BEING EXPLORED. The continues-term above only
  // holds while a frontier hangs off the room you stand in; a rival that gains grade-2
  // route evidence still outranked a half-explored branch and sent the party across the
  // floor mid-push. The stronger rule: once a branch is being explored, keep
  // recommending it until it yields a KEY THAT OPENS A DOOR (an openable frontier
  // elsewhere breaks the hold) or it DEAD-ENDS (no live, unvetoed frontier left on the
  // branch -- then the normal ranking resumes). Anchor = the last recommendation; if it
  // has since been explored, the branch continues through any live frontier whose
  // start-route passes through it.
  const stickyAnchor = (dungStickyObj && rooms[dungStickyObj]) ? dungStickyObj : '';
  let stickyObj = null, stickyWhy = '';
  let stkEv = -1, stkLeads = -1, stkChain = -1, stkCont = -1, stkDepth = -1, stkPot = -1, stkSpB = -1, stkCount = -1, stkSpace = -1;
  let bestEv = -1, bestLeads = -1, bestChain = -1, bestCont = -1, bestDepth = -1, bestPot = -1, bestSpB = -1, bestNear = Infinity, bestCount = -1, bestSpace = -1, bestOpen = false;
  for (const t of live) {
    const cost = cl.reach.dist[t] !== undefined ? cl.reach.dist[t] : Infinity;
    if (bossKey) {                          // only one objective matters: the boss
      if (cost < bestNear) { bestNear = cost; objective = t; }
      continue;
    }
    const e = evOf(t), ar = area[t] || 0;
    // Elbow room in the four cardinal directions (line-sum), used as the
    // preference among otherwise equal options: more space = more floor left to find.
    const sp = dungRoomSpace(rooms, floor, t);
    // DOES IT LEAD ANYWHERE AT ALL? space 0 means no free cell in any cardinal
    // direction, catchment 0 means no unknown cell only this frontier can reach --
    // together, a room enclosed by what is already known, which cannot extend the chain
    // however it ranks elsewhere. Live case: a frontier 8 rooms away with
    // "space 0, catchment 0" was picked as the objective.
    // This is the ONLY thing catchment still decides; it does not rank.
    const leadsOn = (sp > 0 || ar > 0) ? 1 : 0;
    // FINISH THE BRANCH YOU ARE ON. A frontier hanging off the room you are standing in
    // continues the current push; anything else abandons it. Without this the ranking
    // re-decided from scratch every tick and would walk you off a critical chain to a
    // roomier ? across the floor, leaving the branch half-done: do not move off a branch
    // until it yields something useful, a key or a dead end.
    //
    // It ranks BELOW grade and leadsOn, so within THIS ranking real evidence still
    // pulls ahead -- but see the sticky-path override after the loop :
    // once a branch is being explored, evidence on a rival branch no longer moves the
    // recommendation; only an openable door or the branch ending does. This term still
    // matters for ordering WITHIN the sticky branch and for fresh choices.
    //
    // NOT the distance rule that was removed. That one ranked rooms as MORE CRITICAL
    // for being close, which is wrong -- criticality is a property of the floor, not of
    // where you stand. This changes only the ORDER you visit equally-good options in.
    // When the room you are in has no unexplored exits left -- a dead end, or you have
    // taken what was there -- every candidate scores 0 here and the normal ranking
    // resumes, which is the "until a key or a dead end" part.
    const continues = adjToPlayer.has(t) ? 1 : 0;
    const pot = area[t + '#pot'] || 0;   // the ray bounding box: width x height
    // EXPECTED CRITICAL ROOMS BEHIND THIS FRONTIER.
    //
    // A small region is NOT ruled out (correcting an earlier hard
    // reject): 7 cells can still hold the one or two rooms that hold a needed key.
    // It simply commands a smaller SHARE of what is left to find.
    //
    // The model: `need` critical rooms are still unplaced, and the reachable unmapped
    // space is `allN` cells. Spread them uniformly over that space and the count
    // landing in a region of `pot` cells is need * pot / allN -- capped at pot, since
    // a region cannot hold more rooms than it has cells. That is an EXPECTATION, so a
    // 7-cell region scores ~1.4 of 8 rather than 0, which is exactly the "could be a
    // room or two, but the other way is likelier" reading.
    //
    // Uniform placement is the honest assumption: how densely Daemonheim fills a region is
    // unmeasured, and inventing a prior would be guessing.
    const need = budget.unkMin || 0;
    const allN = area['#allcells'] || 0;
    // Capped at `need` as well as `pot`: the box is a bounding rectangle, NOT a
    // subset of allN's flood cells (it can span known rooms and unreachable space),
    // so pot/allN can exceed 1 and the tooltip printed "expect 11.2 of 8".
    const expCrit = (need > 0 && allN > 0) ? Math.min(pot, need, need * pot / allN) : 0;
    // NOT A RANKING TERM. Within one pass `need` and `allN` are floor-wide constants,
    // so this is `pot` times a constant -- a monotone rescale that can never disagree
    // with region and never separates two frontiers region did not already separate
    // (measured: zero direction disagreements). It had two slots in the order holding
    // one signal. It stays because "expect 5 of 15 rooms" is a readable thing to put in
    // a tooltip, but it is not independent evidence and must not be ranked as if it
    // were. NB any probability derived SOLELY from region size has this property; to
    // add information it would have to take a different input.
    // A frontier whose OWN entrance is a requirement judged non-essential (a door
    // far below, or above, the party) is a side path: rank it BELOW even an
    // unevidenced option, so "explore past the level-45 door" can never be offered
    // while a room whose key is held is waiting. Denying it
    // evidence was not enough -- routes to it still inherited evidence from rooms
    // passed on the way, tying it with the real objective on every term below it.
    const ownBand = dungDoorBand(dungDoorLevels[t]);
    //...and anything only reachable THROUGH a low-tier room inherits that verdict
    // (anything reachable only through that door is also non-critical).
    // Generated floors keep filler with filler, so a branch
    // entered through a side room is side content. Read from `lowVia`, the clean-BFS
    // result computed up top: reachable from the start only by crossing filler.
    // REACHABILITY is the rule, not a walk of this frontier's route: requiring the filler
    // room to be EXCLUSIVE to the route fails both ways -- one filler room feeding a
    // junction with TWO unexplored doors vetoes NEITHER (the more a filler room gates, the
    // more damning it is, not less), and a frontier with a clean detour is vetoed whenever
    // the BFS-shortest route happens to cross filler. lowVia also respects a manual critical
    // mark on the frontier, which the route walk overrode.
    // TOP OF THE ORDER: a critical door whose key is held RIGHT NOW. That is
    // the strongest thing the floor can say -- a confirmed requirement plus the
    // means to satisfy it -- so it outranks any amount of inferred evidence. Walking
    // into the unknown is what you do when there are no hints left to follow, not
    // while a key in your pack opens a known door.
    const own = rooms[t];
    // Holding the key for a door KNOWN to be critical is the strongest signal there is.
    // Holding one for a door only INFERRED critical is not -- the inference chain
    // is "key was lying on the path, therefore its door is on the path", and that step
    // is not sound. So only a CONFIRMED key earns the top grade; a
    // derived one ranks as the ordinary evidence it is.
    // isCritKey, not dungKeyIsCrit: the filler veto must gate the grade-3 tier the
    // same way it gates evidence, or a vetoed key's door still outranked everything.
    const openableCrit = !!(own.key && held.has(own.key.idx) && isCritKey(own.key.idx));
    //...and a door openable RIGHT NOW ranks above walking into the unknown even
    // when its key is not confirmed critical. That is the rule
    // ("not while a key in your pack opens a known door"), but tying it to
    // a confirmed-only test made it dead: once auto-derivation of critical keys was
    // removed, almost no key is confirmed, so holding one stopped counting for
    // anything and an unevidenced ? outranked a shield door whose key was held
    //. A locked door is a KNOWN gate with KNOWN rooms behind it;
    // a ? is a guess. Ranked below real evidence (a near-level skill door, e.s = 2),
    // above nothing.
    // A key ON THE FLOOR counts as good as one in the pack, provided it is reachable.
    // Only the pack was checked, so a door whose key was lying in the room the player was
    // STANDING IN scored as an unevidenced frontier and lost to a bigger region across
    // the floor (the green rectangle key was on the ground and its
    // door was the very next one). Picking it up is a step, not an obstacle -- and the
    // plan stage below already redirects to collect a key it needs, so the ranking was
    // the only thing not treating it as reachable progress.
    const ownKey = own.key ? own.key.idx : null;
    const ownKeyFloor = (ownKey != null && led[ownKey]) ? led[ownKey].floor : null;
    const keyInPack = ownKey != null && held.has(ownKey);
    const keyOnFloor = !keyInPack && !!ownKeyFloor && cl.reach.dist[ownKeyFloor] !== undefined;
    const openableAny = keyInPack || keyOnFloor;
    const grade = (ownBand === 'low' || ownBand === 'above' || own.lowVia) ? -1
                : openableCrit ? 3 : Math.max(e.s, openableAny ? 1 : 0);
    // ELBOW ROOM, bucketed in threes -- a TIEBREAK under region, not a rival to it.
    // Region (width x height) already answers "how much floor is this way"; the line
    // sum answers "how far is visible", which is the same four rays ADDED instead of
    // multiplied. Kept only to separate frontiers whose boxes come out equal.
    // Raw `sp` is still the last term, so bucketing loses nothing.
    const spB = Math.floor(sp / 3);
    // DISTANCE IS NOT A RANKING TERM. Where the critical path goes is a property of
    // the floor, not of where the player happens to be standing -- a door 12 rooms
    // away is exactly as critical as one next door. `cost` is still REPORTED in the
    // tooltip, because knowing the trip length is useful even though it must not sway
    // the call. Final tiebreak is the cell key: arbitrary, but DETERMINISTIC, so a
    // genuine tie cannot flip between polls the way the crystal readout did. Strict
    // lexicographic order, written as a term list so the nesting cannot get miscounted
    // (it did, adding a term by hand). First difference decides.
    const chainB = chainOf(t);
    const depth = (routeOf[t] || []).length;   // rooms from the start: longest-path prior
    const TERMS = [
      [grade,    bestEv],       // confirmed evidence
      [leadsOn,  bestLeads],    // does it lead anywhere at all
      [chainB,   bestChain],    // extends a run of proven rooms (capped at 3)
      [continues, bestCont],    // continues the branch you are standing on
      [depth,    bestDepth],    // deeper on its start-route = the longest-path prior
      [pot,      bestPot],      // REGION: the ray bounding box, width x height
      [spB,      bestSpB],      // elbow room
      [e.n,      bestCount],    // how much exclusive evidence
      [sp,       bestSpace],    // raw space
    ];
    let wins = false, allEqual = true;
    for (const [mine, best] of TERMS) {
      if (mine !== best) { wins = mine > best; allEqual = false; break; }
    }
    // WHY THIS ROOM RANKS WHERE IT DOES. The ranking is a strict lexicographic order,
    // so the honest explanation is simply the terms in order -- without this the
    // tooltip could only say "Explore this way", which answers what to do and not why
    // here. Built for every candidate because the sticky-path pick
    // below needs the same explanation for a room that did NOT win the open ranking.
    const whyStr = 'grade ' + (grade === 3 ? '3 (critical door, key held)'
                          : grade === 1 && openableAny
                            ? (keyInPack ? '1 (locked door, key in your pack)'
                                         : '1 (locked door, its key is on the floor and reachable)')
                          : grade === -1 ? (own.lowVia ? '-1 (reached only through a low-tier room)'
                                                       : '-1 (own door judged non-essential)')
                          : String(grade) + (grade > 0 ? ' (evidence on route)' : ' (no evidence)'))
             + ' | ' + (leadsOn ? 'leads on' : 'DEAD END (no space either way)')
             + (chainB ? ' | extends a run of ' + chainB + ' proven room' + (chainB === 1 ? '' : 's') : '')
             + (continues ? ' | continues the branch you are on' : '')
             + ' | depth ' + depth + ' from the start (the critical path is the longest path)'
             + ' | expect ' + (Math.round(expCrit * 10) / 10) + ' of ' + need
             + ' crit rooms (block ' + pot + ' of ' + allN + ' free cells on the floor)'
             + ' | region ' + pot + ' | space bucket ' + spB
             + ' | ' + cost + ' rooms away'
             + ' | ' + e.n + ' exclusive evidence'
             + (e.why ? ' (' + e.why + (e.at ? ' @' + e.at : '') + ')' : '')
             + ' | space ' + sp + ' | catchment ' + ar + ' (diagnostic, does not rank)';
    // Every term equal -- a GENUINE tie, and on a fresh floor with two unexplored
    // doors and nothing else known that is the normal state, not an edge case. Record
    // the tied cells so they can all be offered ("it's essentially a
    // 50/50, just mark both"); still pick a deterministic single objective for the
    // route line, or the drawn path would flip between polls.
    if (allEqual) { tied[t] = 1; wins = (objective === null || t < objective); }
    else if (wins) tied = { [t]: 1 };   // a strict win discards the previous tie set
    if (wins) {
      bestEv = grade; bestLeads = leadsOn; bestChain = chainB; bestCont = continues; bestDepth = depth; bestPot = pot; bestSpB = spB; bestCount = e.n; bestSpace = sp; bestOpen = openableCrit || openableAny; objective = t;
      objWhy = whyStr;
    }
    // The best live continuation of the path being explored: this frontier IS the
    // last recommendation, or its start-route passes through it (the anchor was
    // explored and the branch carried on). Vetoed (-1) and dead-end frontiers never
    // qualify -- when none qualifies the branch has ended and the hold releases.
    // Ranked among themselves by the same term order as the open ranking.
    if (stickyAnchor && grade > -1 && leadsOn &&
        (t === stickyAnchor || (routeOf[t] || []).indexOf(stickyAnchor) >= 0)) {
      const STERMS = [
        [grade, stkEv], [leadsOn, stkLeads], [chainB, stkChain], [continues, stkCont],
        [depth, stkDepth], [pot, stkPot], [spB, stkSpB], [e.n, stkCount], [sp, stkSpace],
      ];
      let sWins = false, sEq = true;
      for (const [mine, best] of STERMS) {
        if (mine !== best) { sWins = mine > best; sEq = false; break; }
      }
      if (sEq) sWins = (stickyObj === null || t < stickyObj);
      if (sWins) {
        stkEv = grade; stkLeads = leadsOn; stkChain = chainB; stkCont = continues; stkDepth = depth; stkPot = pot; stkSpB = spB; stkCount = e.n; stkSpace = sp;
        stickyObj = t; stickyWhy = whyStr;
      }
    }
  }

  // THE STICKY-PATH OVERRIDE : the open ranking may prefer a
  // different branch on soft terms (route evidence, depth, region) while the branch
  // being explored has not finished -- but switching mid-branch costs the walk back,
  // so the recommendation holds the current path. Only two things release it, exactly
  // a DOOR OPENABLE RIGHT NOW appearing anywhere (grade 3, or
  // a locked door whose key is in the pack / reachable on the floor -- "a key that
  // reveals another door that can be opened"), or the branch ending (stickyObj null: every
  // frontier on it is explored, vetoed, or a dead end). The boss overrides everything
  // as before.
  if (!bossKey && stickyObj && objective && objective !== stickyObj) {
    if (!(bestEv === 3 || bestOpen)) {
      objective = stickyObj;
      tied = { [stickyObj]: 1 };
      objWhy = 'staying on the path being explored (no key or dead end yet; a rival branch '
             + 'ranked higher on soft terms, but only an openable door or the branch ending '
             + 'moves the plan off a branch mid-push) | ' + stickyWhy;
    }
  }
  // Whatever is recommended now IS the path being explored for the next poll --
  // including a key-break switch: the opened door starts the new branch.
  if (objective) dungStickyObj = objective;

  // ===========================================================================
  // STAGE F -- PLAN (kept verbatim). Products: action + the route (onPath /
  // edges). Walk toward the objective; the first door that cannot be opened turns the
  // plan into "fetch that key first" (repeat, bounded). This is the elimination
  // insight as plain cost: a key on the floor is a detour, not a special case.
  // ===========================================================================
  const onPath = {}, edges = {};
  let action = null;
  const addPath = t => {
    const p = dungPath(cl.reach.par, t);
    for (let i = 0; i < p.length; i++) {
      onPath[p[i]] = 1;
      if (i) { edges[p[i - 1] + '|' + p[i]] = 1; edges[p[i] + '|' + p[i - 1]] = 1; }
    }
    return p;
  };
  if (objective) {
    let target = objective, fetch = null, guard = 0;
    while (guard++ < 8) {
      const p = addPath(target);
      let blk = null;
      for (const pk of p) { const c = rooms[pk]; if (c.key && !held.has(c.key.idx)) { blk = c.key.idx; break; } }
      if (blk == null) break;
      // NOT a criticality verdict. Needing a key to reach the
      // CURRENT objective is a routing fact, not proof the key is on the boss chain.
      const fc = led[blk] && led[blk].floor;
      if (!fc || fc === target) { action = { kind: 'needkey', cell: (led[blk] && led[blk].door) || target, key: blk }; target = null; break; }
      fetch = blk; target = fc;
    }
    if (target) action = fetch != null ? { kind: 'key', cell: target, key: fetch }
                                       : { kind: bossKey ? 'boss' : 'explore', cell: target };
  } else if (cands.length) {
    // Nothing reachable even with every obtainable key: the way onward is locked
    // and its key is not on the map.
    const openAll = dungBfs(rooms, from, null);
    const reach = cands.filter(t => openAll.dist[t] !== undefined).sort((a, b) => openAll.dist[a] - openAll.dist[b]);
    if (reach.length) {
      for (const pk of dungPath(openAll.par, reach[0])) {
        onPath[pk] = 1;
        const c = rooms[pk];
        if (c.key && !held.has(c.key.idx)) {
          // (was: derive critical from 'only way onward' -- removed, see below)
          action = { kind: 'needkey', cell: pk, key: c.key.idx };
          break;
        }
      }
    }
  }

  // ===========================================================================
  // STAGE G -- RENDER ANNOTATIONS + LATCH MAINTENANCE: contiguous chains, one
  // pink tier, ONE recommendation. The room annotations (onPath / critSelfWhy /
  // critPathWhy / rec / need / recWhy) are written here and only here, and the
  // criticality + key-door latches are maintained at the end.
  // ===========================================================================
  const sb = dungBfs(rooms, (startKey && rooms[startKey]) ? startKey : from, null);
  const chain = (kk, why) => {
    let prev = kk;
    for (let c = sb.par[kk]; c != null; c = sb.par[c]) {
      edges[prev + '|' + c] = 1; edges[c + '|' + prev] = 1; prev = c;
      onPath[c] = 1;
      const r = rooms[c];
      if (r && !r.start && !dungManualNonCrit[c] && !r.critPathWhy && !evid[c]) r.critPathWhy = 'the way to ' + why;
    }
  };
  // Dominators of proofs carry their why from STAGE D (recorded as mustWhy,
  // written here so every room annotation is a render-stage product). Same
  // first-writer-wins guard as every writer below.
  for (const kk in mustWhy)
    if (rooms[kk] && !rooms[kk].critSelfWhy && !rooms[kk].critPathWhy)
      rooms[kk].critPathWhy = mustWhy[kk];
  // ONLY CONFIRMED EVIDENCE CLAIMS THE PATH. evStr 2 = something captured about THIS
  // floor (a read door level, a critical key, the boss). e. a top-tier resource room,
  // which says where content tends to be generated and nothing about the chain. A prior
  // still keeps its critSelfWhy so the tooltip explains it, and still counts for RANKING
  // via evStr -- it just no longer asserts membership of the chain.
  for (const kk in evid) {
    rooms[kk].critSelfWhy = evid[kk];
    if ((evStr[kk] || 0) >= 2) { onPath[kk] = 1; chain(kk, evid[kk]); }
  }
  for (const kk in must) {
    onPath[kk] = 1;
    if (!rooms[kk].critSelfWhy && !rooms[kk].critPathWhy) rooms[kk].critPathWhy = 'the only way onward';
    chain(kk, 'the only way onward');
  }
  if (startKey && rooms[startKey]) onPath[startKey] = 1;
  // (keyCameFrom + the finished-branch prune moved to the TOP of the planner -- the
  // refutation-first rewrite, : the evidence pass consults them now.)
  // A KEY TAKEN FROM A ROOM PROVES THE TRIP -- backwards provenance, the rule this
  // model is built on. These rooms were only ever PROTECTED (wipe/prune exemptions)
  // but never painted, latched or counted unless a separate forced-key proof landed,
  // so five held keys left five source rooms unmarked ("still only
  // 12 critical rooms"). Mark them; the latch block below then makes it permanent.
  for (const kk in keyCameFrom) {
    if (!rooms[kk] || dungManualNonCrit[kk]) continue;
    onPath[kk] = 1;
    if (!rooms[kk].critSelfWhy && !evid[kk])
      rooms[kk].critSelfWhy = 'it held the ' + (dungKeyName(+keyCameFrom[kk]) || 'key')
                            + ' (the trip here is proven by the pickup)';
  }
  // LATCH THE DOOR EACH KEY OPENS. Opening the door clears `c.key`, so the link between
  // a key and the door that needed it evaporates exactly when it becomes useful. Latched
  // here, it survives (and is persisted with the other marks).
  for (const kk in rooms) {
    const c = rooms[kk];
    // While the marker is VISIBLE the observation is the truth -- follow it if it moved
    // (an early widget-churn cell latched before the anchor settled must not stick, and
    // it is persisted to disk). Once the door opens the marker vanishes and the latch
    // keeps the last observed cell, which is the point of latching.
    if (c && c.key && dungKeyDoor[c.key.idx] !== kk) { dungKeyDoor[c.key.idx] = kk; dungSaveMarks(); }
  }
  // The dead-branch prune is the structural finished-branch fixpoint at the top of the
  // planner. Skipping evidenced and hard-latched rooms is what stops a refuted door prior
  // demoting -- under the fact/inference split, facts are protected STRUCTURALLY
  // (keys, boss, start, marks) and inferences are precisely what the prune retracts.)
  // ONCE THE BOSS IS FOUND, STOP GUESSING. `onPath` above is a UNION OF HYPOTHESES --
  // every evidenced room, every cut vertex, and the chain leading to each -- so it forms
  // a TREE. A critical path is a CHAIN, so a branching pink set is proof that some
  // branches are speculation. While the boss is unknown that is the best available, but
  // the moment it is on the map the real answer is knowable: the route from the start
  // room to the boss. Everything else marked purely on evidence was a guess that is now
  // settled, and keeping it inflates the room count and the map ("some
  // of the rooms are marked critical, when they actually weren't").
  // KEPT alongside the route: rooms that yielded a key (you had to go there for it) and
  // anything marked critical by hand.
  // (bossCell / startCell / sFull / routeSet are computed in FACTS/STRUCTURE,
  // before the proofs, so the forced-key proof could use them -- see there.)
  // A FORCED FETCH MAKES ITS WHOLE CHAIN CRITICAL ("that key was
  // actually critical, making everything along that long chain to get down there also
  // critical"). If the door a key opened sits on the route, then going to wherever that
  // key lay was not optional, and neither was any room you had to cross to reach it.
  //
  // This is provenance flowing BACKWARDS, which is the only direction it flows. The
  // trigger is the DOOR being on the route -- not the key having been found somewhere
  // critical, which proves nothing (corrected repeatedly).
  const fetchChain = {};
  // The chain PREFERS doors that were actually opened: the trip happened, so a route
  // through open doors exists, and the lock-blind sFull could thread the drawn chain
  // through a still-locked door the trip never used. Lock-blind stays as the fallback
  // (a misread sprite must not silently drop a proven chain).
  const sOpen = startCell ? dungBfs(rooms, startCell, kk => !rooms[kk].key) : null;
  for (const si in dungKeySrc) {
    // FORCED = proven in the pass loop (forcedKey): 'blocks' = nothing (or confirmed-
    // critical content) is reachable without it -- stands on its own, does NOT need the
    // boss ; 'route' = the boss is known and the door this key opened
    // sits on the settled route. Anything weaker stays out: while the boss is unknown,
    // `onPath` is a union of hypotheses, and hanging a fetch chain off a guess is
    // forwards inheritance again.
    if (!forcedKey[si]) continue;
    const sc = dungKeySrc[si];
    if (!srcUsable(sc)) continue;
    const src = ctx.cellOfWorld(sc.x, sc.y);
    if (!src || !startCell || !rooms[src]) continue;
    const nm = dungKeyName(+si) || 'key';
    const par = (sOpen && sOpen.dist[src] !== undefined) ? sOpen.par : sFull.par;
    for (const c of dungPath(par, src)) {
      fetchChain[c] = 1;
      // say WHY the room is pink -- a bare onPath painted the chain with no reason line
      const rc = rooms[c];
      if (rc && c !== src && !rc.start && !evid[c] && !rc.critPathWhy)
        rc.critPathWhy = 'the trip to fetch the ' + nm;
    }
    fetchChain[src] = 1;
    const rs = rooms[src];
    if (rs && !rs.critSelfWhy && !evid[src])
      rs.critSelfWhy = 'it held the ' + nm + (forcedKey[si] === 'route'
        ? ', which the boss route needed' : ', which nothing was reachable without');
  }
  for (const kk in fetchChain) if (rooms[kk]) onPath[kk] = 1;
  dungMandatoryKeyRooms = {};
  dungForcedKeys = forcedKey;   // publish for the tooltip's proven-forced tier
  for (const si in dungKeySrc) {
    if (!mandatoryKey[+si]) continue;
    const sc = dungKeySrc[si];
    if (!srcUsable(sc)) continue;
    const cc = ctx.cellOfWorld(sc.x, sc.y);
    if (cc) dungMandatoryKeyRooms[cc] = 1;
  }
  for (const kk in onPath) {
    if (!rooms[kk] || dungManualNonCrit[kk]) continue;
    // A PROVEN FORCED TRIP OUTRANKS EVERY HEURISTIC. fetchChain rooms sit on a path that
    // SHOWED was mandatory -- without that key, either nothing was reachable or
    // confirmed-critical content was. deadBranch and lowVia are structural verdicts
    // about rooms nothing has been proven about, and they must not delete a proof.
    // They were being applied FIRST, so the whole south-west leg walked to fetch the
    // boss-door key came out unmarked: spend the key and every room on it reads as a
    // spur. (dungTerminal is gone -- the finished-branch fixpoint
    // at the top covers the single-leaf case it tested.)
    if (!fetchChain[kk]) {
      if (deadBranch[kk]) continue;
      // Filler-gated rooms are off the path by construction -- they were being faded on
      // the map while still carrying a pink "Critical:" line in the tooltip.
      if (rooms[kk].lowVia && !dungManualCrit[kk]) continue;
      // FINDING THE BOSS SETTLES THE ROUTE, NOT THE CHAIN. The wipe below exists to
      // retract route SPECULATION (hypothesis chains, strength-1 resource priors) once
      // the real start->boss walk is known -- but the boss DOOR still needs its key
      // legs, and the 19-23 budget still counts them. Deleting CONFIRMED evidence
      // (evStr 2: a read guaranteed-band door, a critical key's door or source) left
      // "6 found, 13-17 to go" -- the header asserting rooms the map just unmarked
      // ("makes absolutely no sense"). A confirmed room is a proof,
      // and an inference must not delete a proof (same rule as fetchChain above).
      if (routeSet && !routeSet[kk] && !keyCameFrom[kk] && !dungManualCrit[kk]
          && evStr[kk] !== 2 && !must[kk]
          && !rooms[kk].boss && !rooms[kk].start) continue;  // settled: not on the real route
          // (must survives the wipe: cut-to-objective rooms and dominators of proofs
          //  are forced traversals, and the wipe runs BEFORE the latch block -- without
          //  the exemption they were wiped on the very poll they were first proven)
    }
    rooms[kk].onPath = true;
  }
  // A ROOM PROVEN CRITICAL STAYS CRITICAL ("strong evidence
  // to override" -- rooms kept dropping off the path with nothing proving they were
  // off it). onPath is rebuilt every poll, so a proof that stops being observable
  // silently demoted its room. Point facts about a room never go stale -- a key was
  // taken/seen here, its confirmed door was read, the trip was proven forced -- so
  // they LATCH for the floor. Demotion needs the player's own non-crit mark; the
  // dead-branch prune, filler shadow and route wipe are INFERENCES and may not touch
  // a latched room (the same inference-vs-proof rule as fetchChain above).
  {
    let latchChanged = false;
    for (const kk in rooms) {
      const r = rooms[kk];
      const cur = dungCritLatch[kk] ? String(dungCritLatch[kk]) : '';
      // A HARD latch never re-writes; a SOFT one may upgrade to hard when a point
      // fact lands later.
      if (!r || r.unex || dungManualNonCrit[kk] || (cur && cur[0] !== '~')) continue;
      // `must` latches too: a room that cut the start off from EVERY live objective
      // was forcibly TRAVERSED... but that proof was made against objectives that were
      // themselves speculation, so it latches SOFT ('~' prefix): structural disproof
      // (everything beyond explored and empty) may retract it. Point facts -- a key
      // taken here, confirmed evidence, a proven fetch -- latch HARD and are never
      // structurally disprovable (the room leading only to a too-low
      // farming room must demote; the room a key came from must not).
      // FACTS LATCH HARD; must-derived traversal proofs latch SOFT; inferences never
      // latch at all (the fact/inference split, ). A near-level door
      // ranks evStr 2 but is NOT a fact -- dungDoorLevels already persists the reading,
      // so nothing is lost by not latching the conclusion: it recomputes every poll
      // and retracts cleanly when the branch finishes empty.
      if (r.onPath && ((evFact[kk] && evStr[kk] === 2) || fetchChain[kk] || keyCameFrom[kk] || mandatorySrc[kk] || must[kk])) {
        const hard = (evFact[kk] && evStr[kk] === 2) || fetchChain[kk] || keyCameFrom[kk] || mandatorySrc[kk];
        if (!cur || hard) {
          dungCritLatch[kk] = (hard ? '' : '~')
                            + (r.critSelfWhy || r.critPathWhy || evid[kk] || 'proven earlier this floor');
          latchChanged = true;
        }
      }
    }
    for (const kk in dungCritLatch) {
      if (dungManualNonCrit[kk]) { delete dungCritLatch[kk]; latchChanged = true; continue; }
      const r = rooms[kk];
      if (!r) continue;
      const why = String(dungCritLatch[kk]);
      // STRUCTURAL DISPROOF CLEARS A LATCH when the room has no LIVE fact behind it:
      // every way on from here is explored and yielded nothing, which IS the strong
      // evidence demotion requires. Reaches SOFT (must-derived) latches.
      // and STALE door-inference latches persisted by older builds
      // ( -- identified by their why-string, see staleInfLatch).
      // FACT latches are protected from the prune itself, so they never appear here
      // as deadBranch; a proof whose observation vanished stays marked.
      const factNow = evFact[kk] || fetchChain[kk] || keyCameFrom[kk] || mandatorySrc[kk];
      if (deadBranch[kk] && !factNow) {
        delete dungCritLatch[kk]; latchChanged = true; continue;
      }
      r.deadBranch = false;                 // a latched room never renders as a spur
      if (r.onPath) continue;
      r.onPath = true;
      if (!r.critSelfWhy) r.critSelfWhy = 'proven earlier this floor: ' + why.replace(/^~/, '');
    }
    if (latchChanged) dungSaveMarks();
  }
  for (const f in area) if (rooms[f]) rooms[f].pot = area[f];

  // EQUALLY GOOD ALTERNATIVES GET MARKED TOO. When candidates tie on every term there
  // is no basis for preferring one, and a single ring implies a confidence the ranking
  // does not have -- on a fresh floor with two unexplored doors and nothing else known,
  // that is the normal state ("it's essentially a 50/50, just mark
  // both"). The route line still runs to one of them, chosen deterministically, or the
  // drawn path would flip between polls.
  const tieN = Object.keys(tied).length;
  if (tieN > 1) for (const tk in tied) {
    const tr = rooms[tk];
    if (!tr || dungManualNonCrit[tk] || (action && tk === action.cell)) continue;
    tr.rec = true;
    tr.recWhy = 'Explore this way -- ties with ' + (tieN - 1) + ' other option'
      + (tieN > 2 ? 's' : '') + ' on every signal, so there is nothing to choose between them'
      + (objWhy ? String.fromCharCode(10) + 'Why: ' + objWhy : '');
  }
  if (action && rooms[action.cell] && !dungManualNonCrit[action.cell]) {
    const r = rooms[action.cell];
    r.onPath = true; r.recCrit = true;
    if (action.kind === 'needkey') {
      r.need = true;
      r.recWhy = 'Needs the ' + (dungKeyName(action.key) || 'key') + ' -- not on the map yet';
    } else {
      r.rec = true;
      const ownKey = (r.key && held.has(r.key.idx)) ? r.key.name : null;
      r.recWhy = action.kind === 'key' ? 'Pick up the ' + (dungKeyName(action.key) || 'key') + ' here -- the route needs it'
               : action.kind === 'boss' ? 'Head for the boss room'
               : ownKey ? 'Open this with the ' + ownKey + ' -- you have it'
               : 'Explore this way';
      if (objWhy) r.recWhy += String.fromCharCode(10) + 'Why: ' + objWhy;
    }
  }
  // PLANNER DIAGNOSTICS. "Nothing is marked to explore" has several distinct causes --
  // no frontiers found, frontiers found but none reachable, an objective with no
  // walkable path -- and they are indistinguishable from the map alone. Reported in the
  // debug line rather than reasoned about from the code, which is easy to get wrong).
  planDiag.objective = objective || '-'; planDiag.action = action ? action.kind : '-';
  return { edges: edges, objective: objective, action: action, diag: planDiag };
}

async function fetchDungeoneering() {
  if (!bridge() || dungFetching) return; dungFetching = true;
  try {
    let groups = [];
    try { groups = (JSON.parse(rtxData.sync('state.interfaceGroups') || '{}').groups) || []; } catch (e) {}
    const inDung = groups.some(g => g.id === 945);
    const mapOpen = groups.some(g => g.id === 942);
    const party92 = groups.some(g => DUNG_PARTY_GROUPS.indexOf(g.id) >= 0);
    if (!inDung) { dungDropMarks(); dungFloorSW = null; dungLastTimer = -1; dungKeyCache = {}; dungOpenedAt = {}; dungKeySrc = {}; dungKeyDoor = {}; dungKeyFillerVeto = {}; dungHeldSeen = {}; dungHeldInit = false; dungRoomRes = {}; dungCritKeys = {}; dungDerivedCritKeys = {}; dungCritKeyBlock = {}; dungCritKeyTouch = {}; dungDoorLevels = {}; dungTipLast = ''; dungStatues = null; dungMonoDone = {}; dungEmoteLast = -1; dungEmoteWatch = ''; dungManualNonCrit = {}; dungNonCritTouch = {}; dungManualCrit = {}; dungManualCritTouch = {}; dungNonCritSeen = {}; dungManualCritSeen = {}; dungStickyObj = ''; dungClearOverlays(); }   // left the dungeon -> drop anchor + keys + highlights (no party 'reset': the rest of the party may still be on the floor)
    // Roster is party context, not dungeon context: keep it while the party interface
    // is open (forming), drop it only when there's no party context at all.
    if (!inDung && !party92) dungPartyRoster = {};
    // Fetch party stats whenever a party exists (forming in the lobby OR in a dungeon)
    // so the levels are ready before the first floor.
    dungPumpPartyHiscores(party92);
    const d = { in: inDung, mapOpen: mapOpen, party92: party92, keys: [], timer: '', deaths: '',
                skips: [], prog: '', progW: 0, map: null };
    // per-floor SKIP indicators (945 comps 1/2 + 3/4, CS2 scripts 6535-6537):
    // comp 1 icon = lock melter (item 37410, varc 6569), comp 3 icon = warped
    // gorajan trailblazer body (item 38542, varc 6570); the paired comp 2/4
    // sprite is 13165 (check = skip USED this floor) or 13166 (X = not used).
    const skipMeta = { 1: { name: 'Lock melter', item: 37410 },
                       3: { name: 'Trailblazer outfit', item: 38542 } };
    const skipUsed = {};   // comp 2/4 -> used bool
    if (inDung) {
      // one scene + varc read up top, shared by the anchor, ghost puzzle, facing and
      // floor-reset. Reset (timer 4190 drops) clears the anchor BEFORE it is re-acquired.
      let vc = {};
      try { vc = JSON.parse(await rtxData.raw('state.varcsAll') || '{}'); } catch (e) {}
      const tmr = vc['5:4190'];
      // restore floor marks saved before a panel rebuild (same-floor check: the timer
      // only counts up within a floor, so an older/absent save never applies)
      if (!dungRestored && typeof tmr === 'number') {
        dungRestored = true;
        try {
          const s = JSON.parse(localStorage.getItem('rtxDgMarks') || 'null');
          if (s && Date.now() - s.t < 7200000 && tmr >= (s.tm || 0)) {
            dungCritKeys = Object.assign(s.ck || {}, dungCritKeys);
            dungCritKeyBlock = Object.assign(s.cb || {}, dungCritKeyBlock);
            dungManualNonCrit = Object.assign(s.nc || {}, dungManualNonCrit);
            dungManualCrit = Object.assign(s.mc || {}, dungManualCrit);
            dungDoorLevels = Object.assign(s.dl || {}, dungDoorLevels);
            dungKeySrc = Object.assign(s.ks || {}, dungKeySrc);
            dungKeyDoor = Object.assign(s.kd || {}, dungKeyDoor);
            dungRoomRes = Object.assign(s.rr || {}, dungRoomRes);
            dungCritLatch = Object.assign(s.cl || {}, dungCritLatch);
            // THE ANCHOR MUST PERSIST TOO. dungFloorSW comes ONLY from the start-room
            // constellation fit, so once the panel reloads mid-floor it can never be
            // re-derived -- you are nowhere near the start room. That left `sw NONE`
            // for the rest of the floor, which fails `here()` closed (no puzzle
            // guidance at all) and leaves the map unable to place YOU (:
            // "is there no retry... why is the player not drawn"). There is no
            // retry that could work; persisting is the only fix.
            if (!dungFloorSW && s.sw && typeof s.sw.x === 'number') dungFloorSW = s.sw;
          }
        } catch (e) {}
      }
      if (typeof tmr === 'number') { if (tmr < dungLastTimer - 2) { dungFloorSW = null; dungKeyCache = {}; dungOpenedAt = {}; dungKeySrc = {}; dungKeyDoor = {}; dungKeyFillerVeto = {}; dungHeldSeen = {}; dungHeldInit = false; dungRoomRes = {}; dungCritKeys = {}; dungDerivedCritKeys = {}; dungCritKeyBlock = {}; dungCritKeyTouch = {}; dungDoorLevels = {}; dungCritLatch = {}; dungDropMarks(); dungTipLast = ''; dungMonoDone = {}; dungEmoteLast = -1; dungEmoteWatch = ''; dungLodeTick = -1; dungLodeWarnAt = -1; dungRouteTarget = ''; dungManualNonCrit = {}; dungNonCritTouch = {}; dungManualCrit = {}; dungManualCritTouch = {}; dungNonCritSeen = {}; dungManualCritSeen = {}; dungStickyObj = ''; dungPartyHoldUntil = Date.now() + 3000; dungPartyReport('reset', {}); } dungLastTimer = tmr; }   // roster persists across floors (party context, not floor); 'reset' clears the party channel's floor-scoped facts
      const yw = vc['5:5115'];
      if (typeof yw === 'number') d.yaw = ((yw % 16284) + 16284) % 16284;
      dungMonoCharge = (typeof vc['5:1233'] === 'number') ? vc['5:1233'] : null;   // monolith progress
      dungMazeTimer = (DUNG_MAZE_TIMER_VAR && typeof vc['5:' + DUNG_MAZE_TIMER_VAR] === 'number') ? vc['5:' + DUNG_MAZE_TIMER_VAR] : null;   // poison-maze countdown
      let sceneNpcs = [], sceneObjs = [], dungSelf = null, dungMates = [];
      // NB: RS display names in interface text carry NON-BREAKING spaces (u00a0),
      // e.g. "Aurni x" -- normalise to a plain space so the roster key, the scene
      // name and the hiscores lookup all agree (the lookup form is aurni_x).
      const rosterKey = s => (s || '').replace(/\s+/g, ' ').replace(/["<>&]/g, '').trim();
      try { const se = JSON.parse(await bridge().sceneEntities(myPid(), 128) || '{}'); sceneNpcs = se.npcs || []; sceneObjs = se.objects || [];
            const sp = (se.players || []).find(p => p && p.self);
            if (sp && typeof sp.x === 'number') { dungSelf = { x: sp.x, y: sp.y }; if (sp.name) { dungSelfName = rosterKey(sp.name); } }
            dungMates = (se.players || []).filter(p => p && !p.self && typeof p.x === 'number'); } catch (e) {}
      // everyone inside the instance IS the party (self included): floor-scoped roster
      if (dungSelfName) dungRosterAdd(dungSelfName);
      for (const m of dungMates) dungRosterAdd(rosterKey(m.name || ('#' + m.uid)));
      // scene mates just added to the roster (inside the instance) -> ask for them too;
      // the group-92 roster + hiscore pump already ran up top (works in formation too)
      dungPumpPartyHiscores(false);
      dungSelfPos = dungSelf || dungSelfPos;   // same-room gate anchor for the reconcile
      dungReconcileScene(sceneNpcs, sceneObjs);   // start-room anchor + ghost / puzzle / grooves highlight

      for (const w of dungFetchGroup(945)) {
        const comp = w.t ? w.t[1] : -1;
        // key icons surface as obj-icon graphics (s = 131072 + item) or item widgets (it)
        const cand = (w.s >= 131072) ? w.s - 131072 : (w.it || 0);
        if (cand) {
          const k = dungKeyInfo(cand);
          if (k && !d.keys.some(x => x.idx === k.idx)) d.keys.push(k);
        }
        if (w.x && comp === 11 && /^\d{1,3}:\d{2}(:\d{2})?$/.test(w.x)) d.timer = w.x;
        if (w.x && comp === 5 && /^\d+$/.test(w.x)) d.deaths = w.x;
        if ((comp === 2 || comp === 4) && (w.s === 13165 || w.s === 13166))
          skipUsed[comp] = (w.s === 13165);       // 13165 check = used, 13166 X = not used
        if (w.x && comp === 26 && w.x !== 'Progress bar text') d.prog = w.x;
        if (comp === 28 && w.r) d.progW = w.r[2];   // script5873 fill: 14px at zero
      }
      // a skip indicator is present when its check/X sprite is in the tree
      for (const iconComp in skipMeta) {
        const flag = +iconComp + 1;               // comp 2 pairs with 1, comp 4 with 3
        if (skipUsed[flag] !== undefined)
          d.skips.push({ name: skipMeta[iconComp].name, item: skipMeta[iconComp].item, used: skipUsed[flag] });
      }
      // speedrun bar shows only when genuinely active: real text AND a fill
      // beyond script5873's 14px zero-width base
      if (d.progW <= 15) d.prog = '';
      d.keys.sort((a, b) => a.idx - b.idx);

      // Floor map (group 942, comp-8 children). Classification mirrors the
      // renderer script5999: room TILES (>=30px graphics, incl. the 2831 start /
      // 2833 boss overlays) render as their real sprite; markers are 24px key
      // objs, 16px skill-door graphics (enum 371), 2825-2829 player arrow, and
      // the 20px gatestone objs 17489/29468/18829. A room's requirement = the key
      // or skill door it shows; a key room turns green when that key is held
      // (script8152 = key item -> held varc, the game's own outline test).
      const held = new Set(d.keys.map(k => k.idx));
      const rooms = {}; let player = null;
      const cellOf = (cx, cy) => rooms[cx + ',' + cy] ||
        (rooms[cx + ',' + cy] = { bg: [], key: null, door: null, gate: 0, boss: false });
      for (const w of dungFetchGroup(942)) {
        // comp 3 = the map root: its pixel size IS the floor size (12px pad each side,
        // 32px per room cell) -- 152x152 = 4x4 floor, 280x280 = 8x8.
        // Room widget coords are ABSOLUTE grid cells within this floor.
        if (w.t && w.t[1] === 3 && w.r && w.r[2] >= 100) {
          const fc = Math.round((w.r[2] - 24) / 32), fr = Math.round((w.r[3] - 24) / 32);
          if (fc >= 2 && fc <= 8 && fr >= 2 && fr <= 8) d.floor = { cols: fc, rows: fr };
        }
        if (!w.t || w.t[1] !== 8 || w.t[2] < 0 || !w.r) continue;
        const x = w.r[0], y = w.r[1], ww = w.r[2], hh = w.r[3];
        if (x < 0 || y < 0 || x > 240 || y > 240) continue;      // pooled/off-map widget
        // rooms sit at 12 + 32*cell (12px board pad) -- subtract the pad before
        // bucketing. Identical for the aligned 32px room tiles, but the ARROW moves
        // continuously: without the pad its cell flipped early in a room's east/south.
        const cx = Math.floor((x + ww / 2 - 12) / 32), cy = Math.floor((y + hh / 2 - 12) / 32);
        const cand = (w.s >= 131072) ? w.s - 131072 : (w.it || 0);
        if (cand && DUNG_GATESTONES[cand]) {
          cellOf(cx, cy).gate = cand;                            // 17489/29468/18829
        } else if (cand && dungKeyInfo(cand)) {
          cellOf(cx, cy).key = dungKeyInfo(cand);                // 24px key on the floor
        } else if (DUNG_SKILL_DOOR[w.s]) {
          cellOf(cx, cy).door = { spr: w.s, name: DUNG_SKILL_DOOR[w.s] };
        } else if (w.s >= 2825 && w.s <= 2829) {
          player = { cx: cx, cy: cy };
        } else if (ww >= 30 && hh >= 30 && w.s > 0 && w.s < 131072) {
          const cell = cellOf(cx, cy);
          cell.bg.push(w.s);
          if (w.s === DUNG_BOSS_SPR) cell.boss = true;
          if (w.s === DUNG_START_SPR) cell.start = true;
          if (DUNG_ROOM_DOORS[w.s] !== undefined) cell.doors = DUNG_ROOM_DOORS[w.s];
        }
      }
      for (const kk in rooms) {
        const c = rooms[kk];
        if (!c.bg.length && !c.gate && !c.key && !c.door) { delete rooms[kk]; continue; }
        c.haveKey = !!(c.key && held.has(c.key.idx));            // green-border test
      }
      if (Object.keys(rooms).length) d.map = { rooms: rooms, player: player };

      // Ground keys: the dungeon map only dots a key once you are near it, so this panel tracks
      // them from the scene ground items and REMEMBER each one (world tile -> key)
      // until it's picked up. A remembered key is dropped only when the player is
      // close enough to see it and it's gone (picked up); keys that merely scroll out
      // of the loaded scene stay remembered, so a missed key keeps showing on the map.
      // Each key maps to its true room via the 14+2 grid, anchored on the start
      // room's SW corner from the constellation fit; without the anchor keys are
      // not placed (an approximate grid rounds edge keys into the wrong room).
      if (d.map) {
        try {
          let startCell = null;
          for (const kk in rooms) if (rooms[kk].start) { const p = kk.split(',').map(Number); startCell = { cx: p[0], cy: p[1] }; break; }
          // NB: dungFloorSW comes ONLY from the start-room constellation fit. Do NOT
          // approximate it from the player being "somewhere in the start room": that
          // poisons the grid by up to +-7 tiles and shifts every marker.

          const gi = JSON.parse(await rtxData.raw('state.groundItems') || '[]');
          if (Array.isArray(gi)) dungGroundCache = gi;   // hoardstalker ground piles
          const present = {};
          for (const g of (Array.isArray(gi) ? gi : [])) {
            const ki = dungKeyInfo(g.id);
            if (!ki || typeof g.x !== 'number') continue;
            const kk = g.x + ',' + g.y;
            present[kk] = true;
            dungKeyCache[kk] = { x: g.x, y: g.y, ki: ki };   // remember it
          }
          // Forget keys that are gone AND within pickup range (<=12 tiles: the loaded
          // scene always reports these, so absence == picked up). Distant keys persist.
          // ALSO: each key index exists once per floor, so a cached ground key whose
          // index is now HELD (the key-ring varcs) was picked up -- authoritative,
          // works no matter how far from the tile the pickup was noticed.
          for (const kk in dungKeyCache) {
            const c = dungKeyCache[kk];
            if (held.has(c.ki.idx)) { dungKeySrc[c.ki.idx] = { x: c.x, y: c.y }; delete dungKeyCache[kk]; dungSaveMarks(); continue; }
            if (dungSelf && !present[kk] && Math.max(Math.abs(c.x - dungSelf.x), Math.abs(c.y - dungSelf.y)) <= 12)
              delete dungKeyCache[kk];
          }
          // A KEY GRABBED BEFORE THE PANEL EVER SAW IT LYING THERE still has to come from
          // somewhere. The loop above only records a source for keys that made it into
          // dungKeyCache -- walk in and take one in the same tick and there is no record
          // at all, so the room then reported "Dead end (nothing here)" about a room a
          // key had just come out of.
          //
          // Fall back to where the player is standing when the key enters the ring. That
          // is ASSUMED, not observed: the ring is shared, so a teammate's pickup also
          // trips it and would name the wrong room. So it is flagged, and the flag is
          // honoured downstream -- an assumed source may say what a room contained, but
          // it is NEVER allowed to argue that anything is on the critical path.
          if (!dungHeldInit) { held.forEach(i => { dungHeldSeen[i] = 1; }); dungHeldInit = true; }
          else for (const i of held) {
            if (dungKeySrc[i] || dungHeldSeen[i]) continue;
            dungHeldSeen[i] = 1;
            if (dungSelf) { dungKeySrc[i] = { x: dungSelf.x, y: dungSelf.y, assumed: 1 }; dungSaveMarks(); }
          }
          held.forEach(i => { dungHeldSeen[i] = 1; });
          // Room grid: 16-tile pitch anchored at the start room's SW corner (from the
          // smuggler sighting). Every position -- the player's and each key's -- is that
          // entity's WORLD tile pushed through dungRoomOf; the map-arrow widget rect is
          // NOT used (capture showed it off the room lattice entirely), and one
          // source of truth means the cell and in-room offset can never disagree.
          // NO fallback anchor: an approximate grid puts edge keys in the WRONG room.
          const anchor = (dungFloorSW && startCell)
            ? { swx: dungFloorSW.x, swy: dungFloorSW.y, cx: startCell.cx, cy: startCell.cy } : null;
          const sw = anchor ? { x: anchor.swx, y: anchor.swy } : null;
          if (anchor && player && dungSelf) {
            const r = dungRoomOf(sw, dungSelf.x, dungSelf.y);
            const tc = anchor.cx + DUNG_MAP_XSIGN * r.rx, tr = anchor.cy + DUNG_MAP_YSIGN * r.ry;
            if (rooms[tc + ',' + tr]) {   // sanity: only override onto a known room
              player.cx = tc; player.cy = tr; player.lx = r.lx; player.ly = r.ly;
              dungCurDoors = rooms[tc + ',' + tr].doors || 0;   // for the ice-slide goal
            }
          } else if (player && d.map) {
            // no anchor -> the only cell source is the game's map-arrow widget, which
            // is proven unreliable: draw NO arrow rather than one in the wrong room
            d.map.player = null;
          }
          // party members on the map (same anchored math as the player)
          d.mates = [];
          if (anchor) for (const m of dungMates) {
            const r2 = dungRoomOf(sw, m.x, m.y);
            const tc2 = anchor.cx + DUNG_MAP_XSIGN * r2.rx, tr2 = anchor.cy + DUNG_MAP_YSIGN * r2.ry;
            if (rooms[tc2 + ',' + tr2]) {
              const key = rosterKey(m.name || ('#' + m.uid));
              d.mates.push({ name: key, pn: Object.keys(dungPartyRoster).sort().indexOf(key) + 1, cx: tc2, cy: tr2, lx: r2.lx, ly: r2.ly });
            }
          }
          // KEY PROVENANCE: which key was originally picked up in this room. Recorded so
          // that after a floor you can check whether a key the panel called "critical"
          // actually came out of a critical room -- the designation means "found in a
          // room on the critical path", and it is NOT yet established that such keys
          // must be used (a floor finished with two of them unspent, ).
          // Provenance only; nothing reads it for planning.
          // reset first: if a room object survives between polls, pushing would stack
          // the same key over and over
          for (const kk3 in rooms) if (rooms[kk3]) rooms[kk3].keySrc = null;
          if (anchor) for (const si in dungKeySrc) {
            const src = dungKeySrc[si];
            if (!src || typeof src.x !== 'number') continue;
            const r3 = dungRoomOf(sw, src.x, src.y);
            const kc = anchor.cx + DUNG_MAP_XSIGN * r3.rx, kr = anchor.cy + DUNG_MAP_YSIGN * r3.ry;
            const room3 = rooms[kc + ',' + kr];
            if (!room3) continue;
            (room3.keySrc = room3.keySrc || []).push({ idx: +si, name: dungKeyName(+si) || ('key ' + si) });
          }
          // TEMP diagnostics (remove once sub-room placement is confirmed live)
          d.dbg = 'sw ' + (anchor ? anchor.swx + ',' + anchor.swy + ' @cell ' + anchor.cx + ',' + anchor.cy : 'NONE')
                + ' | self ' + (dungSelf ? dungSelf.x + ',' + dungSelf.y : 'NONE')
                + ' | cell ' + (player ? player.cx + ',' + player.cy : 'NONE')
                + (player && player.lx !== undefined ? ' | local ' + player.lx + ',' + player.ly : '')
                + dungLodeDbg + dungTipDbg + dungPlanDbg;
          const addKey = (cell, ki, o) => { if (!cell.groundKeys) cell.groundKeys = []; if (!cell.groundKeys.some(k => k.idx === ki.idx)) cell.groundKeys.push({ idx: ki.idx, color: ki.color, item: ki.item, name: ki.name, lx: o.lx, ly: o.ly }); };
          if (anchor) for (const kk in dungKeyCache) {
            const c = dungKeyCache[kk];
            const r = dungRoomOf(sw, c.x, c.y);
            const cell = rooms[(anchor.cx + DUNG_MAP_XSIGN * r.rx) + ',' + (anchor.cy + DUNG_MAP_YSIGN * r.ry)];
            if (cell) addKey(cell, c.ki, r);   // only draw in a known room
          }
          // Skilling-resource rooms: map each scene resource to its map cell (round-based,
          // so an edge tree folds into its room) and tag the room with its resource.
          //
          // A ROOM YOU HAVE NOT OPENED HAS NOTHING IN SCENE: instances do NOT load every
          // resource up front, so a badge on an unexplored room is a stale cache entry
          // from the PREVIOUS floor.
          //
          // So it is an INVARIANT: a resource attributed to an unexplored cell is bad
          // data, whatever produced it -- stale cache, a shifted anchor, or an edge node
          // rounding in from the room next door. Guarded at both ends below: never
          // recorded against an unexplored cell, and never displayed for one.
          // The room's BAND is RELATIVE to the party's ceiling:
          // critical = within the top 2 tiers the party can actually COMPLETE; a tier
          // ABOVE the party's level = a bonus room (they can't finish it); well below =
          // filler. So T9/T10 is critical ONLY for a ~90+ party -- a level-50 party's
          // critical band is its own top tiers, and T10 is a bonus room for them.
          if (anchor) for (const e of sceneNpcs.concat(sceneObjs)) {
            if (typeof e.x !== 'number') continue;
            const res = dungResource(e.id);
            // Divination wisps are NOT a path signal : they spawn
            // without regard to the critical path, so they must never drive the
            // band badge, the key-drop crit latch, or the low-level fade.
            if (!res || res.skill === 'Divination') continue;
            const rx = Math.round((e.x - sw.x - 6.5) / DUNG_ROOM_PITCH), ry = Math.round((e.y - sw.y - 6.5) / DUNG_ROOM_PITCH);
            const ck = (anchor.cx + DUNG_MAP_XSIGN * rx) + ',' + (anchor.cy + DUNG_MAP_YSIGN * ry);
            if (rooms[ck] && !dungCellUnex(rooms[ck])) {
              // RECORD the sighting. Grading happens below, off the record, so a room is
              // graded the same whether or not its nodes are currently on screen.
              const sig = res.skillIdx + '|' + res.tier + '|' + res.level;
              const st = (dungRoomRes[ck] = dungRoomRes[ck] || {});
              if (!st[sig]) {
                st[sig] = { skill: res.skill, skillIdx: res.skillIdx, tier: res.tier, level: res.level };
                dungSaveMarks();
              }
            }
          }
          // GRADE FROM THE RECORD, NOT THE SCENE. `rooms` is rebuilt every poll and the
          // loop above only sees what is in render range, so grading from the scene would
          // erase what a room contained the moment it scrolls out of view ("once you know that
          // resource is in the room, it stays there for grading"). A resource node does
          // not move or deplete, so the sighting is permanent.
          //
          // Bands are RECOMPUTED here rather than stored, because "filler" is relative to
          // the party's levels and those change when someone joins or leaves.
          for (const ck in dungRoomRes) {
            const cell = rooms[ck];
            if (!cell || dungCellUnex(cell)) continue;   // see the invariant above
            for (const sig in dungRoomRes[ck]) {
              const r0 = dungRoomRes[ck][sig];
              const mt = dungMaxTier(r0.skillIdx, r0.skill);
              const nr = { skill: r0.skill, tier: r0.tier, level: r0.level,
                           mine: dungSkillLevel(r0.skillIdx), best: mt.best, by: mt.by,
                           maxTier: mt.maxTier, band: dungResBand(r0, mt.maxTier, mt.best) };
              // REPRESENT THE ROOM BY ITS LOWEST-TIER RESOURCE. A low tier tells you far
              // more than a high one: generated floors keep filler with filler, so a T2
              // Hunter alongside a T10 Woodcutting means the room is filler, and the T10
              // is incidental. Picking the best band instead labelled
              // that room "T10 -- in the party's top tier, likely critical path", which
              // is the opposite of what the T2 says.
              // Ties on tier break to the lower level, same reasoning.
              if (!cell.res || nr.tier < cell.res.tier
                  || (nr.tier === cell.res.tier && nr.level < cell.res.level)) cell.res = nr;
              //...but REMEMBER that a low one was here. Keeping only the best band threw
              // the fact away, and it turns out to be the decisive one: a room holding a
              // LOW-tier resource alongside a high one is a side room, and the low tier
              // OVERRIDES (a T9 Woodcutting + T5 Fishing room that was
              // not on the route). Generated content puts filler together with filler.
              // resLow is a PROOF the room is off the path, so it is STATIC, not
              // party-relative: only the top two tiers of a resource type generate on
              // both sides of the split, so any tier below those proves a bonus room
              // whatever the party's levels are. An uncompletable (above-level)
              // resource proves NOTHING: it also generates on the critical path. The
              // party-relative band above stays what it was: an advisory for badges
              // and ranking priors, never a proof.
              if (nr.tier > 0 && nr.tier < DUNG_RES_SCALE.length - 1) {
                if (!cell.resLow || nr.tier < cell.resLow.tier) cell.resLow = nr;
              }
            }
          }
          // ---- ONE planner: criticality, the route and the recommendation all come
          // out of the same objective-directed model (see dungPlan). Runs after the
          // resource/ground-key facts above are in place, and needs nothing else.
          {
            // NB not gated on the constellation anchor: the room GRAPH alone is
            // enough to route and recommend. The anchor only sharpens it (it is
            // what places ground keys and the player's true cell), so without it
            // the plan starts from the start room instead of going silent.
            const plan = dungPlan(rooms, {
              startKey: startCell ? startCell.cx + ',' + startCell.cy : null,
              playerKey: (anchor && player && player.cx !== undefined) ? player.cx + ',' + player.cy : null,
              held: held,
              floor: d.floor,
              cellOfWorld: (wx, wy) => {
                if (!anchor) return '';
                const r = dungRoomOf(sw, wx, wy);
                return (anchor.cx + DUNG_MAP_XSIGN * r.rx) + ',' + (anchor.cy + DUNG_MAP_YSIGN * r.ry);
              },
            });
            if (d.map) d.map.critEdges = plan.edges;
            if (plan.diag) {
              const g = plan.diag;
              dungPlanDbg = ' | plan rooms ' + g.rooms + ' fronts ' + g.fronts
                          + ' live ' + g.live + ' from ' + g.from
                          + ' obj ' + g.objective + ' act ' + g.action;
            }
            // DERIVED MARKS ARE NOT RELAYED. Sending one put a GUESS on the
            // party channel, where it came back as `DUNG_KEY_PARTY` -- an observation,
            // latched, and indistinguishable from a human mark. That
            // round trip laundered "the planner suspects" into "the party knows" and
            // defeated the whole point of keeping derivations unlatched, which is that a
            // conclusion must never outlive the situation that produced it.
            // Only a HUMAN promotion (right-click) is relayed now; see dungPartyReport
            // at the manual-promote handler.
          }
          // Skill-door LEVEL capture: the entity tooltip (group 1177, its one TEXT
          // widget = varc-string 2251; EXAMINING the door is required, hover never
          // writes it) carries the exact requirement -- "...requires level 104
          // Strength to optimally unlock...". On a NEW sentence, attach
          // (skill, level) to the door being examined: unambiguous when the floor
          // has one door of that skill; with several, only when the player stands
          // at exactly ONE of them (within 8 tiles of that room's rect -- you
          // examine the door you're next to). Anything still ambiguous is DROPPED,
          // never guessed. Latched per floor.
          {
            dungReadDoorTip();                    // also poll here (tab open = 1s cadence)
            const tip = dungPendingTip;
            if (tip && Date.now() - tip.t < 60000) {
              const m = tip.text.match(/requires level (\d+) ([A-Za-z]+)/i);
              if (!m) dungPendingTip = null;
              else {
                const lvl = +m[1], sk = m[2];
                const cand = Object.keys(rooms).filter(kk =>
                  rooms[kk].door && rooms[kk].door.name.toLowerCase() === sk.toLowerCase());
                let target = cand.length === 1 ? cand[0] : null, how = '';
                // Several doors of that skill: the EXAMINED DOOR'S OWN TILE (tip.dx/dy, captured
                // from the engine hover slot at examine time) is the exact anchor -- try it first
                // with a tight radius. Fall back to the tile the player stood on (tip.x/y),
                // nearest-candidate with tie-drop, for captures where no loc hover was seen.
                if (!target && cand.length > 1 && anchor && (tip.dx != null || tip.x != null)) {
                  const distOf = (kk, px, py) => {
                    const p2 = kk.split(',').map(Number);
                    const rx = (p2[0] - anchor.cx) * DUNG_MAP_XSIGN, ry = (p2[1] - anchor.cy) * DUNG_MAP_YSIGN;
                    const rsx = sw.x + DUNG_ROOM_PITCH * rx, rsy = sw.y + DUNG_ROOM_PITCH * ry;
                    const ddx = Math.max(rsx - px, px - (rsx + DUNG_ROOM_W - 1), 0);
                    const ddy = Math.max(rsy - py, py - (rsy + DUNG_ROOM_W - 1), 0);
                    return Math.max(ddx, ddy);
                  };
                  const tries = [];
                  if (tip.dx != null) tries.push([tip.dx, tip.dy, 4, 'door']);   // door sits on the room edge -> ~0
                  if (tip.x != null)  tries.push([tip.x, tip.y, 16, 'self']);
                  for (const [px, py, lim, tag] of tries) {
                    let best = null, bd = Infinity, tie = false;
                    for (const kk of cand) {
                      const dd = distOf(kk, px, py);
                      if (dd < bd) { bd = dd; best = kk; tie = false; }
                      else if (dd === bd) tie = true;
                    }
                    if (best && !tie && bd <= lim) { target = best; how = ' (' + tag + ')'; break; }   // never guess a tie
                  }
                }
                dungTipDbg = ' | tip L' + lvl + ' ' + sk + ' cand=' + cand.length + (target ? ' -> ' + target + how : ' UNATTACHED');
                if (target) {
                  dungDoorLevels[target] = { skill: sk, level: lvl };
                  dungPartyReport('door', { cell: target, skill: sk, level: lvl });   // share with the party
                  dungPendingTip = null;
                }
              }
            } else if (tip) { dungPendingTip = null; dungTipLast = ''; }   // stale: drop it, but
            // clear the dedupe stamp too -- otherwise a capture that never attached
            // (map hadn't drawn the door yet, ambiguous candidates) would block
            // that exact sentence for the rest of the floor and re-examining did nothing.
          }
        } catch (e) {}
      }

      // Exploration guidance now comes from dungPlan (called above): the
      // recommendation is literally the first step of the winning plan, so it can
      // never disagree with the drawn path.
    }
    if (inDung) dungSaveMarks();   // marks survive a panel rebuild (restored above)
    dungData = d;
  } finally { dungFetching = false; }
  paneRun('dung', renderDungeoneering);
}

// Party roster list + collapsible "Party best" hiscore-validation grid. Shared by the
// in-dungeon view and the formation (lobby) view. Returns an HTML string; call sites
// run the shared sprite loader afterwards (dg-rspr-mk) to paint the skill icons.
function dungPartyBestHtml(party92, alwaysOpen) {
  // ensure the local name is known at render (lastSnap.display_name is reliably set by
  // the time anything renders -- it drives the panel header), so self is never
  // mistaken for a mate even if the first fetch tick raced ahead of the snapshot.
  try { if (lastSnap && lastSnap.display_name) dungSelfName = rosterKey(lastSnap.display_name); } catch (e) {}
  // one boxed, collapsible master section (it's not critical
  // mid-floor -- hideable as a whole). The formation (lobby) view is ABOUT the
  // party, so it renders open with no collapse chrome.
  if (!alwaysOpen) {
    const n = Object.keys(dungPartyRoster).length;
    let out = '<div class="dg-sect dg-phd" title="Party roster, party-best levels and cross-PC sync -- click to '
      + (dungPartyOpen ? 'collapse' : 'expand') + '">'
      + '<span>' + (dungPartyOpen ? '▾' : '▸') + ' Party</span>'
      + '<span class="dg-count">' + (n || 0) + (dungSyncCode() ? ' · sync on' : '') + '</span></div>';
    if (dungPartyOpen) out += '<div class="dg-party-box">' + dungPartyBestHtml(party92, true) + '</div>';
    return out;
  }
  let html = '';
  const partyNames = Object.keys(dungPartyRoster).sort();
  // per-MEMBER hiscore fetch status -> a coloured dot on the chip: self = the local live
  // stats (never fetched), green = fetched ok, amber = fetching, red = no hiscores found
  // (privated / typo / ironman) so it's obvious WHICH person's lookup failed.
  const memStatus = n =>
    dungIsSelf(n) ? { cls: 'self', txt: 'your own live stats (not fetched)' }
    : dungPartyStats[n] ? { cls: 'ok', txt: 'hiscores fetched' }
    : dungHsPoll[n] ? { cls: 'wait', txt: 'fetching hiscores...' }
    : dungHsDone[n] ? { cls: 'fail', txt: 'no hiscores found' }
    : { cls: 'wait', txt: 'queued' };
  if (partyNames.length > 1) {
    html += '<div class="dg-sect"><span>Party</span><span class="dg-count">' + partyNames.length + '</span></div>'
      + '<div class="dg-party">'
      + partyNames.map((n, i) => {
          const s = memStatus(n);
          return '<span class="dg-pmem dg-pm-' + s.cls + '" title="' + n + ' — ' + s.txt + '">'
            + '<i class="dg-pmdot"></i>'
            + '<b style="color:' + (dungIsSelf(n) ? '#f25c5c' : dungMateCol(i + 1)) + '">p' + (i + 1) + '</b>' + n + '</span>';
        }).join('')
      + '</div>';
  }
  if (!party92)
    html += '<div class="dg-none">Open the game\'s party interface so every mate\'s name (and stats) can be fetched.</div>';
  // Party best level per skill = the levels the rules actually use (the local live level
  // merged with mates' fetched hiscores). Clean aligned table: icon | level | provider.
  const rows = [];
  for (let i = 0; i < SKILL_NAMES.length; i++) {
    const b = dungPartyBest(i);
    if (b.best == null) continue;
    rows.push('<span class="dg-pbrow" title="' + SKILL_NAMES[i] + ' — party best ' + b.best + ' (' + (b.by || 'you') + ')">'
      + '<span class="dg-rspr-mk dg-pbspr" data-spr="' + DUNG_SKILL_SPR[i] + '"></span>'
      + '<b>' + b.best + '</b><span class="dg-pbw">' + (b.by || 'you') + '</span></span>');
  }
  const mates = partyNames.filter(n => !dungIsSelf(n));
  const okN = mates.filter(n => dungPartyStats[n]).length;
  if (rows.length) {
    html += '<div class="dg-sect dg-pbhd" title="Party best level per skill -- click to ' + (dungPbOpen ? 'collapse' : 'expand') + '">'
      + '<span>' + (dungPbOpen ? '▾' : '▸') + ' Party best</span>'
      + '<span class="dg-count">' + (mates.length ? okN + '/' + mates.length + ' fetched' : 'you only') + '</span></div>';
    if (dungPbOpen) html += '<div class="dg-pb">' + rows.join('') + '</div>';
  }
  // Cross-PC party sync: one member makes a code, shares it (voice/Discord), everyone
  // joins it -> examined door levels + fetched hiscores relay between PCs. If the launcher
  // lacks the bridge (old build), the whole section is hidden.
  if (bridge().partyGetCode) {
    const code = dungSyncCode();
    html += '<div class="dg-sect"><span>Party sync</span>'
      + '<span class="dg-count">' + (code ? 'on' : 'off') + '</span></div>';
    if (code) {
      html += '<div class="dg-sync"><span class="dg-sync-lbl">Code</span>'
        + '<code class="dg-sync-code">' + code + '</code>'
        + '<button class="dg-sync-btn" data-sync="leave">Leave</button></div>'
        + '<div class="dg-sync-hint">Share this code so party members\' clients sync door levels &amp; hiscores.</div>';
    } else {
      html += '<div class="dg-sync">'
        + '<input class="dg-sync-in" type="text" maxlength="16" placeholder="enter a code" value="' + dungSyncInput.replace(/"/g, '') + '">'
        + '<button class="dg-sync-btn" data-sync="join">Join</button>'
        + '<button class="dg-sync-btn dg-sync-new" data-sync="new">New code</button></div>'
        + '<div class="dg-sync-hint">Make a code and share it, or join a party member\'s code, to sync door levels &amp; hiscores across PCs.</div>';
    }
  }
  return html;
}

function renderDungeoneering() {
  const c = paneRoot('dung');
  if (!c) return;
  let wrap = $('dgWrap');
  if (!wrap) {
    c.innerHTML = ''; wrap = document.createElement('div'); wrap.id = 'dgWrap'; wrap.className = 'pk-wrap'; c.appendChild(wrap); dungSig = '';
    // click a room -> route to it (click the same room again to clear)
    wrap.addEventListener('click', ev => {
      const t = ev.target;
      if (t && t.closest && t.closest('.dg-pbhd')) {
        dungPbOpen = !dungPbOpen; dungSig = ''; renderDungeoneering(); return;
      }
      if (t && t.closest && t.closest('.dg-phd')) {
        dungPartyOpen = !dungPartyOpen; dungSig = ''; renderDungeoneering(); return;
      }
      // party-sync controls
      const sb = t && t.closest ? t.closest('[data-sync]') : null;
      if (sb) { dungSyncAction(sb.getAttribute('data-sync'), wrap); return; }
      // ice-room dump -> OS clipboard
      const ib = t && t.closest ? t.closest('[data-icedump]') : null;
      if (ib) {
        try { if (dungIceDump && bridge() && bridge().copyClipboard) { bridge().copyClipboard(JSON.stringify(dungIceDump)); ib.textContent = 'Copied'; } } catch (e) {}
        return;
      }
      const bb = t && t.closest ? t.closest('[data-barreldump]') : null;
      if (bb) {
        try { if (dungBarrelDump && bridge() && bridge().copyClipboard) { bridge().copyClipboard(JSON.stringify(dungBarrelDump)); bb.textContent = 'Copied'; } } catch (e) {}
        return;
      }
      const mb2 = t && t.closest ? t.closest('[data-mazedump]') : null;
      if (mb2) {
        try { if (dungMazeDump && bridge() && bridge().copyClipboard) { bridge().copyClipboard(JSON.stringify(dungMazeDump)); mb2.textContent = 'Copied'; } } catch (e) {}
        return;
      }
      // key chip -> toggle critical: promote a held key / demote a critical one
      // (a manual demotion is remembered so the auto-latch can't re-add it)
      const keyEl = t && t.closest ? t.closest('.dg-key[data-kidx]') : null;
      if (keyEl) {
        const ki = +keyEl.getAttribute('data-kidx');
        const promote = !dungKeyIsCrit(ki);
        if (promote) { dungCritKeys[ki] = DUNG_KEY_MANUAL; delete dungCritKeyBlock[ki]; }
        else { delete dungCritKeys[ki]; dungCritKeyBlock[ki] = 1; }
        dungCritKeyTouch[ki] = Date.now();
        dungPartyReport('critkey', { idx: ki, on: promote });   // relay the override to the party
        dungSig = ''; renderDungeoneering(); return;
      }
      // left-click a room -> mark it CRITICAL (route moved to hover + H)
      const cell = t && t.closest ? t.closest('.dg-cell[data-cell]') : null;
      if (!cell) return;
      dungSetManualCrit(cell.getAttribute('data-cell'), true);
    });
    // hover tracking for the H route key
    wrap.addEventListener('mouseover', ev => {
      const cell = ev.target && ev.target.closest ? ev.target.closest('.dg-cell[data-cell]') : null;
      dungHoverCell = cell ? cell.getAttribute('data-cell') : '';
    });
    if (!window.__dgKeyHooked) {
      window.__dgKeyHooked = true;
      document.addEventListener('keydown', ev => {
        if (ev.key !== 'h' && ev.key !== 'H') return;
        if (ev.target && (ev.target.tagName === 'INPUT' || ev.target.tagName === 'TEXTAREA')) return;
        if (typeof paneVisible === 'undefined' || !paneVisible('dung') || !dungHoverCell) return;
        dungRouteTarget = (dungRouteTarget === dungHoverCell) ? '' : dungHoverCell;
        dungSig = ''; renderDungeoneering();
      });
    }
    // remember what's typed in the join box so a re-render (a hiscore arriving) restores it
    wrap.addEventListener('input', ev => {
      if (ev.target && ev.target.classList && ev.target.classList.contains('dg-sync-in')) dungSyncInput = ev.target.value;
    });
    wrap.addEventListener('keydown', ev => {
      if (ev.key === 'Enter' && ev.target && ev.target.classList && ev.target.classList.contains('dg-sync-in'))
        dungSyncAction('join', wrap);
    });
    // right-click a room -> mark it NON-critical; middle-click -> un-mark. Shared across
    // the party (dungSetNonCrit relays it). Ultralight's WebKit does not reliably
    // dispatch contextmenu/auxclick DOM events -- the raw mousedown (button 2 = right,
    // 1 = middle) is what actually arrives, so the actions live THERE; contextmenu is
    // handled only to suppress any default menu if the engine ever raises it.
    wrap.addEventListener('contextmenu', ev => { ev.preventDefault(); });
    wrap.addEventListener('mousedown', ev => {
      if (ev.button !== 1 && ev.button !== 2) return;
      const cell = ev.target && ev.target.closest ? ev.target.closest('.dg-cell[data-cell]') : null;
      if (!cell) return;
      ev.preventDefault();
      const k = cell.getAttribute('data-cell');
      if (ev.button === 2) dungSetNonCrit(k, true);                       // right: mark non-critical
      else { dungSetNonCrit(k, false); dungSetManualCrit(k, false); }     // middle: clear BOTH designations
    });
  }
  const d = dungData;
  if (!d) { wrap.innerHTML = '<div class="stor-empty">Reading...</div>'; dungSig = ''; return; }
  if (!d.in) {
    // Party FORMING (party interface open in the lobby) or a roster already fetched
    // -> show the party + party-best so stats are visible/validated before entering.
    const formingNames = Object.keys(dungPartyRoster);
    if (d.party92 || formingNames.length) {
      const fsig = 'form|' + formingNames.sort().join(',') + '|' + d.party92 + '|' + dungPbOpen + '|' + dungSyncCode()
        + '|' + Object.keys(dungHsPoll).length + '|' + Object.keys(dungHsDone).length + '|' + Object.keys(dungPartyStats).sort().join(',')
        + '|' + SKILL_NAMES.map((_, i) => { let b = null; for (const n in dungPartyStats) { if (dungPartyRoster[n] && typeof dungPartyStats[n][i] === 'number' && (b == null || dungPartyStats[n][i] > b)) b = dungPartyStats[n][i]; } return b || 0; }).join('.');
      if (fsig === dungSig) return;
      dungSig = fsig;
      wrap.innerHTML = '<div class="dg-head"><div><div class="dg-title">Daemonheim</div>'
        + '<div class="dg-sub">forming party</div></div></div>' + dungPartyBestHtml(d.party92, true);
      wrap.querySelectorAll('.dg-rspr-mk').forEach(el => loadSpriteIcon(el, +el.dataset.spr, 40));
      dungStripTitles(wrap);
      return;
    }
    wrap.innerHTML = '<div class="stor-empty">Not on a Daemonheim floor.<br>'
      + '<span class="dg-hint">Floor status, held keys and the explored map appear here in a dungeon.</span></div>';
    dungSig = ''; return;
  }
  // arrow rotation from live camera yaw; computed here so the fast-path (below) can
  // spin the arrow every fetch without a full re-render.
  const yawDeg = (d.yaw !== undefined) ? (DUNG_YAW_SIGN * d.yaw / 16284 * 360 + DUNG_YAW_OFFSET) : 0;
  // NB: d.timer AND the yaw arrow are OUT of the sig -- they change every tick and a
  // full re-render restarts the CSS pulse animations (visible clipping). Both are
  // updated in place below instead. Ghosts are outlined in-scene, not rendered here.
  const sig = JSON.stringify([d.keys.map(k => k.idx + (dungKeyIsCrit(k.idx) ? 'c' : '')), d.deaths,
    d.skips.map(s => s.name + (s.used ? '1' : '0')), d.prog, d.mapOpen, d.party92,
    d.floor ? d.floor.cols + 'x' + d.floor.rows : 0, dungRouteTarget,
    (d.mates || []).map(m => m.name + '@' + m.cx + ',' + m.cy).join(';'), Object.keys(dungPartyRoster).sort().join('|'),
    SKILL_NAMES.map((_, i) => { const b = dungPartyBest(i); return (b.best || 0) + (b.by || ''); }).join('.'),
    Object.keys(dungHsPoll).length, Object.keys(dungHsDone).length, dungPbOpen, dungPartyOpen, dungSyncCode(), Object.keys(dungManualNonCrit).sort().join(','), Object.keys(dungManualCrit).sort().join(','), Object.keys(dungPartyStats).sort().join('|'),
    // chip veto notes + opened-door crit tooltips read these latches, so they must
    // re-render when a latch or a crit mark moves even if nothing else changed
    Object.keys(dungKeyFillerVeto).sort().join(','),
    Object.keys(dungKeyDoor).map(i => i + (dungKeyIsCrit(i) ? 'c' : '') + '@' + dungKeyDoor[i]).sort().join('|'),
    d.map ? [Object.keys(d.map.rooms).map(k => {
      const c = d.map.rooms[k];
      return k + ':' + c.bg.join('.') + '/' + c.gate + '/' + (c.key ? c.key.idx : 0) + '/' +
             (c.door ? c.door.spr : 0) + (dungDoorLevels[k] ? 'L' + dungDoorLevels[k].level : '') + '/' + (c.haveKey ? 1 : 0) + (c.boss ? 'B' : '') +
             (c.pot ? 'P' + c.pot : '') + (c.rec ? 'R' : '') + (c.need ? 'N' : '') + (c.recCrit ? 'RC' : '') +
             (c.key && dungKeyIsCrit(c.key.idx) ? 'C' : '') + (c.critPathWhy ? 'W' : '') + (c.critSelfWhy ? 'S' : '') + (c.onPath ? 'P' : '') + (c.recWhy || '') +
             (c.res ? 'x' + c.res.skill + c.res.level + (c.res.band || '') + (c.res.mine != null ? 'm' + c.res.mine : '') + (c.res.best != null ? 'b' + c.res.best : '') : '') +
             '/' + (c.groundKeys ? c.groundKeys.map(g => g.idx + '@' + (g.lx == null ? 'c' : Math.round(g.lx)) + ',' + (g.ly == null ? 'c' : Math.round(g.ly))).join('.') : '');
    }), (d.map.player ? d.map.player.cx + ',' + d.map.player.cy : 0)] : 0]);   // player SUB-position is fast-pathed, not in the sig
  // structural sig unchanged -> only refresh the live timer text, so the pulse
  // animations keep running smoothly instead of restarting on a full re-render.
  if (sig === dungSig) {
    const te = wrap.querySelector('.dg-timer');
    if (te) te.textContent = d.timer || '';
    const ar = wrap.querySelector('.dg-yawarrow');   // spin the player arrow live
    if (ar && d.yaw !== undefined) ar.style.transform = 'rotate(' + yawDeg.toFixed(1) + 'deg)';
    const pc = wrap.querySelector('.dg-player');      // and walk it around inside the room
    if (pc && d.map && d.map.player) { const p = dungSubPx(d.map.player.lx, d.map.player.ly); pc.style.left = p.left.toFixed(1) + 'px'; pc.style.top = p.top.toFixed(1) + 'px'; }
    if (d.mates && d.mates.length) {                  // party dots walk too
      const els = wrap.querySelectorAll('.dg-mate-mk');
      for (let i = 0; i < els.length; i++) {
        const m = d.mates.find(mm => mm.name === els[i].getAttribute('data-mate'));
        if (m) { const p = dungSubPx(m.lx, m.ly); els[i].style.left = p.left.toFixed(1) + 'px'; els[i].style.top = p.top.toFixed(1) + 'px'; }
      }
    }
    const dbg = wrap.querySelector('.dg-dbg');        // TEMP diagnostics
    if (dbg && d.dbg) dbg.textContent = d.dbg;
    return;
  }
  dungSig = sig;

  let html = '<div class="dg-head">'
    + '<div><div class="dg-title">Daemonheim</div><div class="dg-sub">floor in progress</div></div>'
    + (d.timer ? '<div class="dg-timer">' + d.timer + '</div>' : '')
    + '</div>';

  html += '<div class="dg-strip">'
    + (d.deaths !== '' ? '<span class="dg-chip"><span class="dg-chip-l">Deaths</span><b>' + d.deaths + '</b></span>' : '')
    + d.skips.map(s =>
        '<span class="dg-chip" title="' + s.name + ' skip ' + (s.used ? 'has been used this floor' : 'not used this floor (available)') + '">'
        + '<span class="dg-mini" data-item="' + s.item + '"></span>'
        + '<span class="dg-chip-l">' + s.name + '</span>'
        + '<span class="dg-skip ' + (s.used ? 'used' : 'avail') + '">' + (s.used ? 'used' : 'ready') + '</span></span>').join('')
    + '</div>';

  // Held keys split by criticality: keys witnessed lying in a critical room
  // (start room / T9-T10 resource room) open critical-path doors -> their own
  // section on top; the rest below. Sections collapse away when empty.
  const critHeld = d.keys.filter(k => dungKeyIsCrit(k.idx));
  const normHeld = d.keys.filter(k => !dungKeyIsCrit(k.idx));
  // chips are clickable togglers: a held key promotes to critical / a critical key
  // demotes back (the demotion is remembered so the auto-latch can't re-add it)
  const keyChip = (k, crit) => {
    const why = dungKeyWhy(k.idx);
    // Say WHICH kind of claim this is. Only a hand/party mark is a verdict; everything
    // else is the panel's own inference and is labelled as such, with the raw reason
    // shown so a wrong one is identifiable. If the planner is IGNORING the mark (the
    // key's source room sits behind filler, which structurally rules it out) the chip
    // has to say so -- a chip reading "critical" while the map marks nothing looked like
    // the panel was broken.
    const veto = dungKeyFillerVeto[k.idx]
      ? ' IGNORED by the planner: it was found behind a filler room, so its door cannot'
        + ' be on the path.'
      : '';
    const tt = crit
      ? (why === DUNG_KEY_MANUAL ? 'Promoted to critical by you.' + veto + ' Click to demote.'
         : why === DUNG_KEY_PARTY ? 'Marked critical by the party.' + veto + ' Click to demote.'
         : 'INFERRED critical, not confirmed -- reason: ' + why
           + '. A key being found somewhere important does not prove its door is on the'
           + ' path.' + veto + ' Click to demote.')
      : 'Click to promote to a critical key';
    return '<span class="dg-key' + (crit ? ' dg-key-crit' : '') + '" data-kidx="' + k.idx
      + '" style="border-left-color:' + DUNG_KEY_HEX[k.color] + ';cursor:pointer" data-tip="' + dungAttr(tt) + '">'
      + '<span class="dg-keyico" data-item="' + k.item + '"></span>' + k.name + '</span>';
  };
  if (critHeld.length) {
    html += '<div class="dg-sect"><span>Critical keys</span><span class="dg-count">' + critHeld.length + '</span></div>'
      + '<div class="dg-keys">' + critHeld.map(k => keyChip(k, true)).join('') + '</div>';
  }
  html += '<div class="dg-sect"><span>' + (critHeld.length ? 'Other keys' : 'Keys held') + '</span><span class="dg-count">' + normHeld.length + '</span></div>';
  html += normHeld.length
    ? '<div class="dg-keys">' + normHeld.map(k => keyChip(k, false)).join('') + '</div>'
    : '<div class="dg-none">' + (critHeld.length ? 'No other keys.' : 'No keys held.') + '</div>';

  if (d.prog) {
    html += '<div class="dg-sect"><span>Speedrun progress</span></div>'
      + '<div class="dg-prog">' + d.prog + '</div>';
  }

  // arrow rotates by camera yaw (yawDeg computed above). The MAP arrow carries
  // dg-yawarrow so the fast-path can spin it; the legend arrow is static north.
  const dgArrowSvg = (deg, cls) => '<svg class="dg-shape ' + cls + '" width="18" height="18" viewBox="0 0 12 12" style="transform:rotate(' + deg.toFixed(1) + 'deg)">'
    + '<path d="M6 1 L10.5 10.5 L6 8 L1.5 10.5 Z" fill="#f25c5c" stroke="rgba(0,0,0,.6)" stroke-width="0.9"/></svg>';
  const dgArrow = dgArrowSvg(yawDeg, 'dg-yawarrow');
  if (d.map) {
    const cells = Object.keys(d.map.rooms).map(k => k.split(',').map(Number));
    const xs = cells.map(p => p[0]), ys = cells.map(p => p[1]);
    // Floor size known (map-root pixels) -> the board spans the WHOLE floor grid, with
    // faint slots for unexplored cells, so explored rooms sit at their true place in
    // the overall dungeon layout (room widget coords are absolute floor cells).
    let x0 = Math.min.apply(null, xs), x1 = Math.max.apply(null, xs);
    let y0 = Math.min.apply(null, ys), y1 = Math.max.apply(null, ys);
    if (d.floor) { x0 = 0; y0 = 0; x1 = Math.max(x1, d.floor.cols - 1); y1 = Math.max(y1, d.floor.rows - 1); }
    const ncols = x1 - x0 + 1, nrows = y1 - y0 + 1;
    // clean node-graph layout: rooms are tiles on an absolutely-positioned board,
    // connections are short rounded bars sitting in the GAPS between linked rooms
    // (reads as a circuit/subway map instead of lines crossing the room art).
    const CELL = 56, GAP = 12, PITCH = CELL + GAP, PAD = 4;
    const bw = PAD * 2 + ncols * PITCH - GAP, bh = PAD * 2 + nrows * PITCH - GAP;
    const left = (col) => PAD + col * PITCH, top = (row) => PAD + row * PITCH;
    // Large floors overflow the panel width -> scale the whole board down to fit
    // (a transform keeps the node-graph crisp and every marker proportional).
    const avail = Math.max(160, (wrap.clientWidth || 300) - 4);
    const scale = Math.min(1, avail / bw);
    html += '<div class="dg-sect"><span>Explored map</span><span class="dg-count">'
      + cells.length + (d.floor ? ' / ' + (d.floor.cols * d.floor.rows) + ' rooms (' + d.floor.cols + 'x' + d.floor.rows + ' floor)' : ' rooms') + '</span></div>';
    // Critical-chain progress. The chain is 19-23 rooms INCLUDING start and boss, so
    // the remaining window is a hard bound, not an estimate. It is the `need` behind the
    // planner's expected-rooms figure, so showing it makes that number auditable.
    {
      const ck = (d.keys || []).filter(k => dungKeyIsCrit(k.idx)).length;
      const cb = dungCritBudget(d.map.rooms, ck);
      const span = (a2, b2) => (a2 === b2 ? String(a2) : a2 + '-' + b2);
      // "16 of 19-23 found": the budget total INCLUDES the key-fetch legs, not just the
      // settled boss walk, so found can sit below 19 with the route known -- the
      // remainder is legs whose rooms are still unidentified:
      // "only 16? shouldnt it be at least 19" -- the total makes the invariant visible).
      html += '<div class="dg-sect dg-sub2"><span>Critical rooms</span><span class="dg-count">'
        + cb.found + (cb.max > 0 ? ' of ' + span(cb.found + cb.min, cb.found + cb.max) : '') + ' found'
        + (cb.max > 0 ? ' &middot; ' + span(cb.min, cb.max) + ' to go' : ' &middot; chain may be complete')
        // Held critical keys are doors that MUST be opened, so their rooms are among
        // the remainder but are not unknown -- show the split so the number the planner
        // actually gates on is visible.
        + (ck > 0 && cb.max > 0
             ? ' <span class="dg-dim">(' + ck + ' behind held key' + (ck === 1 ? '' : 's')
               + ', ' + span(cb.unkMin, cb.unkMax) + ' to find)</span>' : '')
        + '</span></div>';
    }
    html += '<div class="dg-boardscale" style="height:' + Math.ceil(bh * scale) + 'px">';
    html += '<div class="dg-board" style="width:' + bw + 'px;height:' + bh + 'px;transform:scale(' + scale.toFixed(4) + ')">';
    // unexplored floor slots first (under everything): where rooms COULD be
    if (d.floor)
      for (let gy = y0; gy <= y1; gy++) for (let gx = x0; gx <= x1; gx++)
        if (!d.map.rooms[gx + ',' + gy])
          html += '<span class="dg-cell dg-unex" style="left:' + left(gx - x0) + 'px;top:' + top(gy - y0) + 'px"></span>';
    // clicked-target route: BFS over the door graph from the player's room, only
    // entering rooms that can be entered (no key needed, or key held; the target itself always
    // allowed); unexplored rooms can END a route but not be routed THROUGH. The
    // route's EDGES light their connectors, so the chain is readable even when
    // several route rooms sit side by side (adjacent but non-consecutive rooms
    // get no lit bar between them).
    let routeSet = null, routeEdges = null;
    if (dungRouteTarget && d.map.rooms[dungRouteTarget] && d.map.player) {
      const skey = d.map.player.cx + ',' + d.map.player.cy;
      if (d.map.rooms[skey]) {
        const enter = k2 => { const r = d.map.rooms[k2]; return r && (!r.key || r.haveKey || k2 === dungRouteTarget); };
        const rprev = {}; rprev[skey] = null; const rq = [skey];
        for (let qi = 0; qi < rq.length && rprev[dungRouteTarget] === undefined; qi++) {
          const p0 = rq[qi].split(',').map(Number);
          const cur = d.map.rooms[rq[qi]];
          if (cur.doors === undefined) continue;   // unexplored: no known exits
          const nbrs = [];
          if (cur.doors & 1) nbrs.push(p0[0] + ',' + (p0[1] - 1));
          if (cur.doors & 2) nbrs.push((p0[0] + 1) + ',' + p0[1]);
          if (cur.doors & 4) nbrs.push(p0[0] + ',' + (p0[1] + 1));
          if (cur.doors & 8) nbrs.push((p0[0] - 1) + ',' + p0[1]);
          for (const nk of nbrs) {
            if (rprev[nk] !== undefined || !enter(nk)) continue;
            rprev[nk] = rq[qi]; rq.push(nk);
          }
        }
        if (rprev[dungRouteTarget] !== undefined) {
          routeSet = {}; routeEdges = {};
          for (let k2 = dungRouteTarget; k2; k2 = rprev[k2]) {
            routeSet[k2] = 1;
            if (rprev[k2]) { routeEdges[rprev[k2] + '|' + k2] = 1; routeEdges[k2 + '|' + rprev[k2]] = 1; }
          }
        }
      }
    }
    // connectors next (under the tiles)
    for (const k in d.map.rooms) {
      const [gx, gy] = k.split(',').map(Number), dm = d.map.rooms[k].doors;
      if (dm === undefined) continue;
      const col = gx - x0, row = gy - y0;
      const ek = (gx + 1) + ',' + gy, sk2 = gx + ',' + (gy + 1);
      const e = d.map.rooms[ek], s = d.map.rooms[sk2];
      const connCls = k2 => routeEdges && routeEdges[k + '|' + k2] ? ' dg-conn-route'
        : (d.map.critEdges && d.map.critEdges[k + '|' + k2] ? ' dg-conn-crit' : '');
      if ((dm & 2) && e && e.doors !== undefined && (e.doors & 8))
        html += '<i class="dg-conn' + connCls(ek) + '" style="left:' + (left(col) + CELL - 3) + 'px;top:' + (top(row) + CELL / 2 - 4) + 'px;width:' + (GAP + 6) + 'px;height:8px"></i>';
      if ((dm & 4) && s && s.doors !== undefined && (s.doors & 1))
        html += '<i class="dg-conn' + connCls(sk2) + '" style="top:' + (top(row) + CELL - 3) + 'px;left:' + (left(col) + CELL / 2 - 4) + 'px;height:' + (GAP + 6) + 'px;width:8px"></i>';
    }
    // OPENED critical-key doors, by cell: room.key vanishes when the door opens, but
    // the tooltip must keep saying the room's door needed a critical key (same
    // "outlives the marker" rule as dungDoorLevels; the latch is the truth).
    const openedCritDoor = {};
    for (const i in dungKeyDoor) if (dungKeyIsCrit(i)) openedCritDoor[dungKeyDoor[i]] = +i;
    for (const k in d.map.rooms) {
      const [gx, gy] = k.split(',').map(Number), room = d.map.rooms[k];
      const col = gx - x0, row = gy - y0;
      const here = d.map.player && d.map.player.cx === gx && d.map.player.cy === gy;
      // DEAD END: exactly one door (nowhere to go but back), nothing to collect (no
      // ground key), no requirement worth visiting (locked/skill door, gatestone),
      // and not the boss/start -> dim so the eye skips over it. UNEXPLORED "?" rooms
      // are NEVER dead ends -- their layout/contents aren't known yet.
      const unex = room.bg.length > 0 && room.bg.every(s => DUNG_UNEX_SPR.has(s));
      const dcnt = room.doors === undefined ? 9 : ((room.doors & 1) + ((room.doors >> 1) & 1) + ((room.doors >> 2) & 1) + ((room.doors >> 3) & 1));
      // A resource does NOT rescue a room from being a dead end :
      // one door, nothing to collect and no requirement is structurally the end of the
      // line whatever is standing in it. The T-badge still says the node is worth doing;
      // the room just stops competing for the eye with actual route rooms.
      // A room a key was TAKEN from is not empty -- it is the reason you walked down
      // there. Reporting it as "Dead end (nothing here)" threw away the single strongest
      // piece of backwards evidence on the floor.
      const dead = !unex && dcnt <= 1 && !(room.groundKeys && room.groundKeys.length) && !room.key && !room.door && !room.gate && !room.boss && !room.start && !here && !room.keyFrom;
      room.dead = dead;
      let tile = room.bg.map(s => '<span class="dg-rspr" data-spr="' + s + '"></span>').join('');
      // The room REQUIREMENT (locked-door key OR skill door) is the primary thing to
      // read -> drawn BIG and CENTRED. Ground key, player arrow and party dots sit at
      // their real in-room positions. A critical (level>=80) resource gets a small
      // green corner tag; low-tier resources and the potential-count number are NOT
      // drawn (the potential still drives the explore ring, just without a badge).
      const gate = room.gate ? DUNG_GATESTONES[room.gate] : null;
      const manualNC = !!dungManualNonCrit[k];   // hand-forced non-critical -> fade, suppress crit
      // the shut door's marker, or the latch once it is opened -- both are the same fact
      const critKeyIdx = room.key ? (dungKeyIsCrit(room.key.idx) ? room.key.idx : null)
                                  : (openedCritDoor[k] != null ? openedCritDoor[k] : null);
      const critSrc = (critKeyIdx != null && !manualNC) ? dungKeyWhy(critKeyIdx) : null;
      let ov = '';
      if (room.key)
        ov += '<span class="dg-mk dg-ctr" title="Locked door: ' + room.key.name + (room.haveKey ? ' (you have it)' : '') + '">'
            + '<span class="dg-doorplate dg-bigreq" style="--kc:' + DUNG_KEY_HEX[room.key.color] + '">'
            + '<span class="dg-keyico dg-bigico" data-item="' + room.key.item + '"></span></span></span>';
      else if (room.door)
        ov += '<span class="dg-mk dg-ctr" title="' + room.door.name + ' door">'
            + '<span class="dg-doorplate dg-skillplate dg-bigreq"><span class="dg-rspr-mk dg-bigspr" data-spr="' + room.door.spr + '"></span></span></span>';
      for (const gk of (room.groundKeys || []))
        ov += '<span class="dg-mk dg-floorkey dg-atpos" style="' + dungSubStyle(gk.lx, gk.ly) + '" title="' + gk.name + ' on the floor"><span class="dg-keyico" data-item="' + gk.item + '"></span></span>';
      // only the CRITICAL band (party's top tiers) gets the green corner badge; bonus/
      // filler rooms carry no badge (and are faded via room.lowRes above)
      if (room.res && room.res.band === 'critical' && !manualNC)
        ov += '<span class="dg-mk dg-bl"><span class="dg-res" title="' + room.res.skill + ' tier ' + room.res.tier + ' (level ' + room.res.level + ')'
            + (room.res.mine != null ? ', you ' + room.res.mine : '')
            + (room.res.by ? ', best ' + room.res.best + ' (' + room.res.by + ')' : '')
            + (dungResPathworthy(room, gx + ',' + gy, d.map.rooms) ? ' -- in the party\'s top tier, likely critical path'
                                       : ' -- top tier, worth doing (dead end, not the route)')
            + '">T' + room.res.tier + '</span></span>';
      for (const m of (d.mates || []))
        if (m.cx === gx && m.cy === gy) {
          const mcol = dungMateCol(m.pn);
          ov += '<span class="dg-mk dg-atpos dg-mate-mk" data-mate="' + m.name + '" style="' + dungSubStyle(m.lx, m.ly) + '" title="p' + m.pn + ' - ' + m.name + '">'
              + '<span class="dg-mate" style="background:' + mcol + ';box-shadow:0 0 5px ' + mcol + '"></span>'
              + '<span class="dg-mate-n" style="color:' + mcol + '">p' + m.pn + '</span></span>';
        }
      if (here) ov += '<span class="dg-mk dg-player" style="' + dungSubStyle(d.map.player.lx, d.map.player.ly) + '">' + dgArrow + '</span>';
      // A resource room OUTSIDE the party's critical band = off the critical path ->
      // faded like a dead end: 'filler' (completable but well below the top tiers) OR
      // 'bonus' (a tier the party can't complete -> a bonus room). Only the 'critical'
      // band (top 2 completable tiers) stays bright. Never faded for boss/start/here.
      room.lowRes = !!(room.res && (room.res.band === 'filler' || room.res.band === 'bonus'))
        && !room.boss && !room.start && !here && !room.critPathWhy;   // never fade the approach path to a critical room
      // Skill door with a captured requirement (examine tooltip) more than 10
      // levels below the player's own = non-essential side path; same fade.
      // NOT gated on room.door: opening the door removes the map marker, but the
      // captured level (and the room's criticality) must survive the walk-through.
      const dl = dungDoorLevels[k] || null;
      const doorMine = dungDoorMine(dl);
      const doorBand = dungDoorBand(dl);
      room.lowDoor = (doorBand === 'low' || doorBand === 'above')
        && !room.boss && !room.start && !here;
      // A captured skill door the PARTY can unlock and whose requirement is near/at the
      // party's level (>= partyBest - margin) is a hard gate -> the path behind it is
      // likely critical (uses party best, not the local level: a level-81 Magic door is
      // critical when a mate has 87 even if the local level is lower). Trivial doors far below the party
      // are lowDoor (faded) instead. Marked with the same violet dg-crit ring as crit keys.
      //...and never for a REFUTED room: the planner proved the branch behind this
      // door finished and empty, so the near-level prior is retracted (:
      // the level-93 Construction door hiding one fishing dead end is NOT critical).
      room.critDoor = doorBand === 'critical' && !manualNC && !room.refuted;
      const reqs = [];
      for (const gk of (room.groundKeys || [])) reqs.push(gk.name + ' (on the floor)');
      if (room.key) reqs.push('Locked door: ' + room.key.name + (room.haveKey ? ' (you have it)' : ''));
      if (critSrc) {
        const opened = !room.key;   // read from the latch: the door has been opened
        reqs.push((critSrc === DUNG_KEY_MANUAL
          ? 'Its key was promoted to critical by you'
          : critSrc === DUNG_KEY_PARTY
          ? 'Its key was marked critical by the party'
          : 'Its key was dropped in ' + critSrc + ' -- likely critical')
          + (opened ? ' (door opened)' : ' -- explore'));
      }
      if (room.door) reqs.push(room.door.name + ' door'
        + (dl ? ' (level ' + dl.level + (doorMine != null ? ', party best ' + doorMine : '') + ')' : '')
        + (room.critDoor ? ' -- near the party\'s level, the path behind it is likely critical'
           : doorBand === 'above' ? ' -- ABOVE the party best, likely non-essential'
           : doorBand === 'low' ? ' -- well below the party, likely non-essential' : ''));
      else if (dl) reqs.push(dl.skill + ' door, opened (level ' + dl.level
        + (doorMine != null ? ', party best ' + doorMine : '') + ')'
        + (room.critDoor ? ' -- near the party\'s level, likely critical'
           : room.refuted && doorBand === 'critical'
             ? ' -- near the party\'s level, but everything beyond is explored and empty: retracted'
           : doorBand === 'above' ? ' -- ABOVE the party best, likely non-essential' : ''));
      if (gate) reqs.push(gate.name);
      if (room.res) reqs.push(room.res.skill + ' resource, tier ' + room.res.tier + ' (level ' + room.res.level + ')'
        + (room.res.mine != null ? ' (you ' + room.res.mine
            + (room.res.by ? ', best ' + room.res.best + ' by ' + room.res.by : '') + ')' : '')
        + (room.res.band === 'critical'
             ? (dungResPathworthy(room, k, d.map.rooms) ? ' -- in the party\'s top tier, likely critical path'
                                        : ' -- top tier, worth doing (dead end, not the route)')
           : room.res.band === 'bonus' ? ' -- above the party\'s level, likely a bonus room'
           : room.res.band === 'filler' ? ' -- well below the party, likely filler' : ''));
      // ONE criticality line: the room's STRONGEST reason only (dedupe
      //: "Critical:..." and "On the way to..." stacked as two
      // overlapping claims about the same room). critSelfWhy (why THIS room
      // matters) outranks critPathWhy (it sits on a path to something else);
      // the self line still yields to the boss / critDoor / resource lines,
      // which already state the same reason in their own words.
      if (room.critSelfWhy && !room.boss && !room.critDoor && !(room.res && room.res.band === 'critical'))
        reqs.push('Critical: ' + room.critSelfWhy);
      else if (room.critPathWhy) reqs.push('On ' + room.critPathWhy + ' -- keys found here are critical');
      if (room.start) reqs.push('Start room -- always on the critical path');
      if (room.boss) reqs.push('Boss room');
      if (room.pot) reqs.push(room.pot + ' unexplored room' + (room.pot === 1 ? '' : 's') + ' this way');
      // KEY PROVENANCE, ONE LINE PER KEY (dedupe, : "Key found
      // here", "A key was taken from this room" and "taken here is REQUIRED"
      // overlapped with contradictory qualifiers in one tooltip). Each key taken
      // from this room gets exactly one sentence: the pickup fact plus the PROVEN
      // forced tier -- 'route' (its door is on the settled boss route) / 'blocks'
      // (nothing was reachable without it) / not known yet. Say ONLY what is
      // proven about the key's DOOR: provenance flows BACKWARDS, the pickup
      // proves the trip and nothing about the door (corrected
      // repeatedly), so the no-proof tier reads "not known yet" -- never
      // "(not critical)", the wording that made a proven-forced key read as a
      // contradiction (the orange-triangle case). The room's
      // own criticality is the criticality line's claim, not this one's ("door
      // on the route" was being printed off the SOURCE room's onPath, and the
      // key line asserted ON/NOT-on-path beside it -- ).
      // keySrc (anchored grid) and keyFrom (planner cell fit) are the same
      // dungKeySrc observations through two mappings -- union them by key idx
      // so no key prints twice.
      const provKeys = (room.keySrc || []).map(ks => ks.idx);
      if (room.keyFrom != null && provKeys.indexOf(+room.keyFrom) < 0) provKeys.push(+room.keyFrom);
      for (const pi of provKeys) {
        const psrc = dungKeySrc[pi];
        reqs.push('The ' + (dungKeyName(pi) || 'key')
          + (psrc && psrc.assumed ? ' entered the key ring while you were in this room'
                                  : ' was taken from this room')
          + (dungForcedKeys[pi] === 'route' ? ' -- its door is on the settled boss route, so this trip was forced'
             : dungForcedKeys[pi] ? ' -- nothing on the floor was reachable without it, so this trip was forced'
             : ' -- where its door leads is not known yet'));
      }
      if (room.recWhy) reqs.push('NEXT: ' + room.recWhy);
      if (here) reqs.push('You are here');
      if (manualNC) reqs.push('Marked non-critical by you (middle-click to clear)');
      if (dungManualCrit[k]) reqs.push('Marked critical by you (middle-click to clear)');
      if (routeSet && routeSet[k]) reqs.push(k === dungRouteTarget ? 'Route target (H to clear)' : 'On the route');
      // A dead end can only claim to have MATTERED on a fact. `dead` already excludes
      // every fact that forces a trip (a key taken here sets keyFrom, which un-deads
      // the room and prints its own provenance line above), so the one truthful
      // "critical dead end" left is the player's own mark. Never assert "its key is taken":
      // that is fabricated whenever no key ever existed there.
      const critDead = dead && !!dungManualCrit[k];
      if (dead) reqs.push(critDead
        ? 'Dead end -- no way onward (kept critical by your own mark)'
        : room.res
        ? 'Dead end -- the ' + room.res.skill + ' node is all there is here'
        : 'Dead end (nothing here)');
      // Say WHY a room went dim, or a pruned spur just reads as an unvisited room
      // A pruned room that DID report a key can only happen on a party floor (solo,
      // the ring flip is trusted and blocks the prune) -- say why the key didn't count
      // instead of claiming "no key" in the same tooltip that reports one.
      if (room.deadBranch && !dead) reqs.push(room.keyFrom
        ? 'Spur -- ways on from here are finished; the key-ring flip seen here was not '
          + 'trusted (party floor: a teammate may have picked that key up elsewhere)'
        : 'Spur -- every way on from here ends in a dead end with no key, so the path cannot run through it');
      if (room.lowVia && !room.lowRes) reqs.push('Only reachable through a filler room');
      const title = reqs.length ? ' data-tip="' + dungAttr(reqs.join('\n')) + '"' : '';
      // ONE source of truth: the planner already decided what is on the path
      // (dungPlan -> room.onPath). The render must never re-derive it from the raw signals,
      // or there are two definitions of "critical" that can drift apart.
      // A critical dead end keeps its ON-PATH look: greying it out is what made a
      // room that fed the boss read as irrelevant once its key was taken.
      const cls = 'dg-cell dg-room' + (dead && !critDead ? ' dg-dead' : '') + (room.lowRes || room.lowVia || room.lowDoor || room.deadBranch || manualNC ? ' dg-lowres' : '')
        + (manualNC ? ' dg-noncrit' : '')
        + (room.haveKey ? ' dg-havekey' : '') + (room.boss ? ' dg-boss' : '')
        + (manualNC ? '' : (room.rec ? ' dg-rec' : '') + (room.need ? ' dg-need' : '') + (room.recCrit ? ' dg-reccrit' : '')
           + (room.onPath ? ' dg-crit' : ''))   // ONE pink box for the whole path, whatever the reason 
        + (routeSet && routeSet[k] ? (k === dungRouteTarget ? ' dg-rtgt' : ' dg-route') : '')
        + (dungRouteTarget === k && !routeSet ? ' dg-rtgt-bad' : '');
      const style = ' style="left:' + left(col) + 'px;top:' + top(row) + 'px'
        + (gate ? ';border-color:' + gate.color + ';box-shadow:0 0 0 1px ' + gate.color + ',0 0 8px ' + gate.color + '66' : '') + '"';
      html += '<span class="' + cls + '" data-cell="' + k + '"' + style + title + '><span class="dg-tile">' + tile + '</span>'
           + (ov ? '<span class="dg-ovs">' + ov + '</span>' : '') + '</span>';
    }
    html += '</div></div>';   // dg-board + dg-boardscale
    // legend: only show gatestone rings for stones actually on this floor's map
    const gatesOnMap = [];
    for (const k in d.map.rooms) { const g = d.map.rooms[k].gate; if (g && gatesOnMap.indexOf(g) < 0) gatesOnMap.push(g); }
    // Legend, grouped by MEANING so each ring reads unambiguously :
    // what a room contains -> what is critical (pink) -> where to go (violet/amber)
    // -> what to skip (dim) -> who is where. Entries appear only when on the map.
    const anyRoom = f => Object.keys(d.map.rooms).some(k => f(d.map.rooms[k], k));
    html += '<div class="dg-legend">'
      // contents
      + '<span><span class="dg-doorplate" style="--kc:var(--text-dim)"><span class="dg-keyico" data-item="18208"></span></span>door req</span>'
      + '<span><span class="dg-floorkey" style="animation:none"><span class="dg-keyico" data-item="18208"></span></span>key on floor</span>'
      + '<span><span class="dg-lg-hk"></span>have key</span>'
      + (anyRoom(r => r.boss) ? '<span><span class="dg-lg-ring" style="border-color:var(--err)"></span>boss</span>' : '')
      // critical-path evidence (pink): top-band resource, party-level door, crit-key door
      + (anyRoom(r => r.onPath) ? '<span><span class="dg-lg-crit"></span>critical path</span>' : '')
      // where to go next
      + (anyRoom(r => r.rec) ? '<span><span class="dg-lg-rec"></span>do this next</span>' : '')
      + (anyRoom(r => r.need) ? '<span><span class="dg-lg-need"></span>best (need key)</span>' : '')
      // what to skip
      + (anyRoom(r => r.dead) ? '<span><span class="dg-lg-dead"></span>dead end</span>' : '')
      + (anyRoom(r => r.lowRes || r.lowVia || r.lowDoor || r.deadBranch) ? '<span><span class="dg-lg-low"></span>low level (off path)</span>' : '')
      + (Object.keys(dungManualNonCrit).length ? '<span><span class="dg-lg-nc"></span>non-critical (you)</span>' : '')
      // gatestones + people
      + gatesOnMap.map(g =>'<span><span class="dg-lg-ring" style="border-color:' + DUNG_GATESTONES[g].color + '"></span>' + DUNG_GATESTONES[g].name + '</span>').join('')
      + '<span>' + dgArrowSvg(0, '') + 'you</span>'
      + ((d.mates || []).length ? '<span><span class="dg-mate" style="display:inline-block"></span>party</span>' : '')
      + '</div>';
    html += '<div class="dg-hint" style="padding:2px 2px 0;font-size:9.5px;opacity:.7">Left-click: mark critical · right-click: mark non-critical · middle-click: clear either · hover + H: route to room</div>';
    if (d.dbg) html += '<div class="dg-dbg">' + d.dbg + '</div>';   // TEMP diagnostics
  } else if (d.mapOpen === false) {
    html += '<div class="dg-sect"><span>Explored map</span></div>'
      + '<div class="dg-none">Open the in-game dungeon map once to populate it.</div>';
  }

  // Ice-room dump: one click -> OS clipboard (Ultralight has no free-text selection
  // copy). The JSON is the room model the solver actually used -- paste it back when
  // a plan looks wrong so the mechanics get verified against data, not memory.
  if (dungIceDump) html += '<div class="dg-dbg"><button class="dg-sync-btn" data-icedump="1">Copy ice-room dump</button> ice: '
    + dungIceDump.padsUn.length + ' pad(s) left, plan '
    + (dungIceDump.plan ? dungIceDump.plan.length + ' slide(s)' : 'NONE') + '</div>';
  if (dungMazeDump) html += '<div class="dg-dbg"><button class="dg-sync-btn" data-mazedump="1">Copy maze dump</button> maze: '
    + dungMazeDump.barriers.length + ' barrier(s), route '
    + (dungMazeDump.route ? dungMazeDump.route.length + ' tile(s), cost ' + dungMazeDump.cost : 'NONE') + '</div>';
  // Same paste-back loop for the barrel room's rotation fit -- the scores say whether the
  // orientation was actually determined or just won a tie.
  if (dungBarrelDump) html += '<div class="dg-dbg"><button class="dg-sync-btn" data-barreldump="1">Copy barrel-room dump</button> barrels: '
    + dungBarrelDump.liveOffsets.length + ' live, fit rot ' + dungBarrelDump.bestR
    + ' (' + dungBarrelDump.rotScores.join('/') + ')'
    + (dungBarrelDump.tied ? ' <b>TIED -- not guiding</b>' : '') + '</div>';

  // Party / Party best / Party sync live BELOW the map (they're not
  // critical mid-floor and must never push the map down).
  html += dungPartyBestHtml(d.party92);

  wrap.innerHTML = html;
  // paint the real room sprites + skill-door marker sprites (Interfaces-tab path)
  wrap.querySelectorAll('.dg-rspr').forEach(el => loadSpriteIcon(el, +el.dataset.spr, 112));
  wrap.querySelectorAll('.dg-rspr-mk').forEach(el => loadSpriteIcon(el, +el.dataset.spr, 40));
  // skip + key item icons from the bundled pack (same path as bank/inventory)
  wrap.querySelectorAll('.dg-mini,.dg-keyico').forEach(el => { const u = resolveIcon(+el.dataset.item); if (u) el.style.backgroundImage = "url('" + u + "')"; });
  dungStripTitles(wrap);
}

// Tab-independent tick: keep the ghost / sliding-puzzle in-scene highlight live even
// when the Dungeoneering tab isn't open (the puzzle target changes as blocks slide, and
// you're usually watching the game, not the panel). Skips when the dung tab is active
// (fetchDungeoneering already reconciles) and when not in a dungeon (one cheap group check).
let dungTickBusy = false;
// The crystal-room "CLICK NOW" centre-text countdown is REMOVED (:
// usually inaccurate -- the reconcile-lag anchor could not hold the true phase). The
// interval still runs this to sweep any leftover centre text; the out-of-sync pad
// marks are the room's guidance now.
function dungLodeTimerTick() {
  if (dungLodeCenterOn) { dungLodeCenterOn = false; try { bridge().centerText(myPid(), ''); } catch (e) {} }
}

async function dungSceneTick() {
  if (dungTickBusy || (typeof paneVisible !== 'undefined' && paneVisible('dung'))) return;
  if (!bridge() || !bridge().sceneEntities || typeof PLUGIN_API === 'undefined') return;
  dungTickBusy = true;
  try {
    let inDung = false, party92 = false;
    try { const gs = (JSON.parse(rtxData.sync('state.interfaceGroups') || '{}').groups) || [];
          inDung = gs.some(g => g.id === 945); party92 = gs.some(g => DUNG_PARTY_GROUPS.indexOf(g.id) >= 0); } catch (e) {}
    if (!inDung) {
      // party FORMING off-tab: keep the roster + hiscore fetch going so stats are
      // ready when the floor starts; drop the roster only with no party context.
      if (party92) dungPumpPartyHiscores(true);
      else dungPartyRoster = {};
      dungClearOverlays(); return;
    }
    let npcs = [], objs = [];
    try { const se = JSON.parse(await bridge().sceneEntities(myPid(), 128) || '{}'); npcs = se.npcs || []; objs = se.objects || [];
          const sp = (se.players || []).find(p => p && p.self); if (sp && typeof sp.x === 'number') dungSelfPos = { x: sp.x, y: sp.y }; } catch (e) {}
    // Ground keys, observed TAB-INDEPENDENTLY. The map fetch only runs while the
    // Dungeoneering tab is active, so a key picked up while you were looking at the
    // game was never seen lying anywhere -- and with no provenance the room it came
    // from never counted as critical ("the top left that contained a
    // critical key wasn't marked"). Only ADD here; deciding a key was PICKED UP needs
    // the key-ring read, so the next full fetch does that from this cache.
    try {
      const gi = JSON.parse(await rtxData.raw('state.groundItems') || '[]');
      if (Array.isArray(gi)) dungGroundCache = gi;   // hoardstalker ground piles
      for (const g of (Array.isArray(gi) ? gi : [])) {
        const ki = dungKeyInfo(g.id);
        if (ki && typeof g.x === 'number') dungKeyCache[g.x + ',' + g.y] = { x: g.x, y: g.y, ki: ki };
      }
    } catch (e) {}
    dungReadDoorTip();   // examine sentences are transient AND the dung tab is usually
                         // NOT the active one while playing -- catch them here (600ms,
                         // tab-independent) and let the next full fetch attach them
    const needMazeVar = DUNG_MAZE_TIMER_VAR && objs.some(o => o.id === 49345 || o.name === 'Locked chest');
    if (npcs.some(n => n.name === 'Monolith' || (n.id >= 10966 && n.id <= 10971)) || needMazeVar) {   // monolith/emote (all statue themes)/maze room -> keep the progress varc fresh off-tab
      try { const vc = JSON.parse(await rtxData.raw('state.varcsAll') || '{}');
            dungMonoCharge = (typeof vc['5:1233'] === 'number') ? vc['5:1233'] : null;
            dungMazeTimer = (DUNG_MAZE_TIMER_VAR && typeof vc['5:' + DUNG_MAZE_TIMER_VAR] === 'number') ? vc['5:' + DUNG_MAZE_TIMER_VAR] : null; } catch (e) {}
    }
    dungReconcileScene(npcs, objs);
  } finally { dungTickBusy = false; }
}

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { dungLodeTimerTick, dungSceneTick, fetchDungeoneering, renderDungeoneering });
})();
