const dgEsc = s => String(s == null ? '' : s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
// Sources: interface group 945 (floor HUD) and 942 (floor map). Keys held = varc 1812-1875 via enum 5734,
// floor timer = varc 4190 (seconds), speedrun = varc 1233/2381, gatestone flags = varc 6569/6570 (spr 13165/13166).
(function () {

let dungData = null; let dungFetching = false; dungSig = '';

const DUNG_KEY_COLORS = ['Orange', 'Silver', 'Yellow', 'Green', 'Blue', 'Purple', 'Crimson', 'Gold'];
const DUNG_KEY_SHAPES = ['triangle', 'diamond', 'rectangle', 'pentagon', 'corner', 'crescent', 'wedge', 'shield'];
const DUNG_KEY_HEX = { Orange: '#f59e42', Silver: '#c9ced9', Yellow: '#f5d442', Green: '#5fd07a',
                       Blue: '#5b9cf6', Purple: '#a06bff', Crimson: '#f25c5c', Gold: '#f0c419' };
// Rooms are 14x14 tiles with a 2-tile gap on every side (16-tile pitch); world +x = east (map col+), +y = north (map row-).
const DUNG_ROOM_PITCH = 16, DUNG_ROOM_W = 14;
const DUNG_MAP_XSIGN = 1, DUNG_MAP_YSIGN = -1;
let dungPlanDbg = '';      // planner counters for the debug line
let dungMandatoryKeyRooms = {};   // cell -> 1: yielded a key nothing works without
let dungForcedKeys = {};          // key idx -> 'route' | 'blocks': proven-forced keys
let dungKeyFillerVeto = {};       // key idx -> 1: planner ignores its crit mark (behind filler)
let dungFloorSW = null;    // {x,y} world tile of the start room's SW corner (constellation fit)
const DUNG_SW_REF = {
  2342: [1, 12], 17144: [1, 12], 49257: [6, 6], 49934: [6, 12], 49937: [0, 5],
  50035: [9, 13], 50191: [1, 1], 50195: [3, 0], 50196: [0, 3], 50197: [2, 2],
  50205: [6, 8], 50229: [7, 0], 50241: [13, 7], 50346: [-1, 7], 50374: [7, -1],
  50386: [14, 7], 51156: [12, -1], 51456: [10, 0], 51457: [13, 0], 51577: [4, 9],
  53124: [14, 3], 123933: [1, 1], 123948: [7, 7], 137159: [6, 6],
  137198: [13, 12], 137199: [13, 13], 137205: [13, 11],
  17146: [1, 12], 49935: [9, 8], 49938: [0, 4], 50036: [9, 12], 50192: [0, 0],
  50198: [2, 0], 50199: [0, 2], 50200: [2, 2], 50206: [6, 8], 50224: [6, 0],
  50232: [0, 6], 50240: [13, 6], 50273: [6, 14], 50433: [6, -1], 50441: [-1, 6],
  50449: [14, 6], 50604: [11, -1], 50910: [10, 0], 50911: [13, 0], 51030: [3, 9],
  53125: [14, 3], 123932: [6, 6],
  // remaining themes, cache-derived (js5-5 start-room template bank at x=113, y 5249-5313 / 5377-5441 / 5505-5537).
  17142: [1, 12], 50203: [2, 2], 50207: [6, 8], 52004: [10, 0], 52005: [13, 0],   // theme bank 3
  17148: [1, 12], 54887: [2, 2], 53883: [6, 8], 54662: [10, 0], 54663: [13, 0],   // theme bank 4
  17150: [1, 12], 55815: [2, 2], 55605: [6, 8],                                   // theme bank 5
};
let dungLastTimer = -1;   // varc 4190 seconds; a drop means a new floor -> re-anchor
let dungKeyCache = {};    // "x,y" -> {x,y,ki}: ground keys remembered until picked up
let dungOpenedAt = {};    // cell -> Date.now() first seen opened (ground keys scan a beat after reveal; see the prune)
let dungRoomRes = {};     // cell -> { "skillIdx|tier|level": {skill,skillIdx,tier,level} }, every resource ever seen there
let dungHeldInit = false; // first poll on a floor seeds dungHeldSeen without attributing pickups
let dungHeldSeen = {};    // key idx -> 1: already seen in the key ring
let dungKeyDoor = {};     // key idx -> cell of the door that needs it; latches on open (the requirement clears then)
let dungKeySrc = {};      // key idx -> {x,y} world tile the key was picked up from
const DUNG_KEY_MANUAL = 'manual promotion';   // dungCritKeys value for a hand-promoted key
const DUNG_KEY_PARTY = 'party mark';          // dungCritKeys value for a party-relayed mark
let dungCritKeyBlock = {};   // key idx -> 1: hand-demoted keys the auto-latch must not re-add
let dungDerivedCritKeys = {};   // key idx -> why-string
let dungCritKeyTouch = {};   // key idx -> Date.now() of the last local toggle (~3s optimistic hold against the party merge)
let dungRestored = false;    // floor-marks restore ran this UI session (a panel rebuild resets module state)
function dungSaveMarks() {
  try {
    localStorage.setItem('rtxDgMarks', JSON.stringify({ t: Date.now(), tm: dungLastTimer,
      ck: dungCritKeys, cb: dungCritKeyBlock, nc: dungManualNonCrit, mc: dungManualCrit, dl: dungDoorLevels, ks: dungKeySrc, kd: dungKeyDoor, rr: dungRoomRes,
      cl: dungCritLatch,
      sw: dungFloorSW }));
  } catch (e) {}
}
function dungDropMarks() { try { localStorage.removeItem('rtxDgMarks'); } catch (e) {} }
let dungCritKeys = {};        // key idx -> why: hand promotions, local and party mates'
function dungKeyIsCrit(i) { return !dungCritKeyBlock[i] && (!!dungCritKeys[i] || !!dungDerivedCritKeys[i]); }
function dungKeyWhy(i) { return dungCritKeys[i] || dungDerivedCritKeys[i] || ''; }
function dungKeyName(i) { const k = dungKeyInfo(18202 + 2 * (i - 1)); return k ? k.name : ''; }
function dungAttr(t) { return String(t == null ? '' : t).replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); }
let dungDoorLevels = {};  // map cell 'gx,gy' -> {skill, level}: parsed from the examine tooltip
let dungTipLast = '';     // last processed 1177 tooltip sentence (change-triggered)
let dungPendingTip = null;   // {text, x, y, dx, dy, t}: x/y = player tile, dx/dy = examined door's own tile
let dungTipDbg = '';         // last attach attempt, surfaced on the map's debug line
let dungHoverLoc = null;     // {x, y, t}: last hovered scenery (engine hover slot carries the loc's own world tile)
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
let dungPartyStats = {};  // display name -> [level per SKILL_NAMES idx] from hiscores index_lite; gated to the current roster
let dungHsPoll = {};      // names whose fetch is in flight
let dungHsDone = {};      // names resolved (ok or error) this session
let dungPbOpen = false;   // Party best validation grid: collapsed by default
let dungPartyOpen = false;   // master Party section (roster + best + sync): collapsed by default
let dungSelfLvSig = '';   // last own-live-levels report (code|name|levels); re-sent on change
let dungSyncInput = '';   // in-progress text in the "join a code" box (survives re-renders)
let dungManualNonCrit = {};   // map-cell 'gx,gy' -> 1: right-clicked to force non-critical; party-synced ('noncrit')
let dungNonCritTouch = {};    // cell -> Date.now() of the last local mark/unmark (~3s hold against the party merge)
let dungPartyHoldUntil = 0;   // after a local floor reset: skip the launcher's door/noncrit cache until 'reset' round-trips
let dungManualCrit = {};      // map-cell 'gx,gy' -> 1: left-clicked to force critical; exclusive with noncrit; party-synced ('critroom')
let dungManualCritTouch = {}; // cell -> Date.now() of the last local crit mark/unmark (3s hold)
let dungCritLatch = {};       // map-cell 'gx,gy' -> the why-string captured at proof time
let dungStickyObj = '';
let dungNonCritSeen = {};     // cells confirmed in the shared set at least once: a mirror may only
let dungManualCritSeen = {};  // remove a mark it has seen, never one that simply failed to arrive
let dungHoverCell = '';       // map cell under the mouse; H routes to it
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
// skill sprites (cache js5-8), indexed by SKILL_NAMES order; same table the XP tracker uses
const DUNG_SKILL_SPR = [
  16040, 16045, 16160, 16041, 16058, 16057, 16055, 16043, 16197, 16051,
  16050, 16049, 16044, 16061, 16056, 16052, 16038, 16196, 16060, 16048,
  16059, 16053, 16042, 16195, 16047, 16046, 16054, 16039, 30936];
const DUNG_GHOST_PILLAR = { 54580: 1, 54591: 1, 54602: 1, 55457: 1, 55472: 1 };
const DUNG_GHOST_POT    = { 54577: 1, 54588: 1, 54599: 1, 55455: 1, 55470: 1 };
const DUNG_GHOST_BOX    = { 54576: 1, 54587: 1, 54598: 1, 55453: 1, 55468: 1 };
const DUNG_GHOST_COFFIN = { 40181: 1, 54571: 1, 54582: 1, 54593: 1, 55465: 1 };
const DUNG_GHOST_COFFIN_BLESS = { 54572: 1, 54583: 1, 54594: 1, 55451: 1, 55466: 1 };
const DUNG_GHOST_POT_DONE    = { 54578: 1, 54589: 1, 54600: 1, 55456: 1, 55471: 1 };
const DUNG_GHOST_PILLAR_DONE = { 54581: 1, 54592: 1, 54603: 1, 55458: 1, 55473: 1 };
// Actionless "Jewellery box" locs; 40173/40180/55464 are inferred, not verified.
const DUNG_GHOST_BOX_DONE    = { 55454: 1, 55469: 1, 40173: 1, 40180: 1, 55464: 1 };
const DUNG_GHOST_RING = 19879;
// Ghost-room puzzle: NPC 10989 is the ghost to kill (10990 are decoys); outlined in-scene by uid.
// Five theme families of 8 consecutive ids in grid reading order; only 12125-12132 verified, the rest inferred.
const DUNG_PUZZLE = (() => {
  const cells = [[0, 0], [1, 0], [2, 0],   // NW  N  NE
                 [0, 1], [1, 1], [2, 1],   //  W  C   E
                 [0, 2], [1, 2]];          // SW  S  (SE empty)
  const t = {};
  for (const base of [12125, 12133, 12141, 12149, 12963])
    cells.forEach((c, i) => { t[base + i] = c; });
  return t;
})();
const DUNG_STATUE_PAIR = {
  10954: 10942, 10955: 10943, 10956: 10944, 10957: 10945,
  10958: 10946, 10959: 10947, 10960: 10948, 10961: 10949,
  10962: 10950, 10963: 10951, 10964: 10952, 10965: 10953,
  12121: 12117, 12122: 12118, 12123: 12119, 12124: 12120,
  12956: 12952, 12957: 12953, 12958: 12954, 12959: 12955,
};
let dungStatues = null;   // [{id, east, cur:[x,y], tgt:[x,y], done}] while the room is in scene
let dungMonoCharge = null;   // varc 1233 while in a dungeon (generic room progress: monolith 0-195, emotes 67/134/201)
const DUNG_MAZE_MINE_SEC = 4, DUNG_RUN_TILES_PER_SEC = 3.3;
const DUNG_MAZE_MINE_TILES = Math.round(DUNG_MAZE_MINE_SEC * DUNG_RUN_TILES_PER_SEC);
const DUNG_MAZE_TIMER_VAR = 1233;   // poison-maze countdown (~205 after the switch, ticks to 0); shared with monolith/emote progress
let dungMazeTimer = null;    // live maze countdown value while in the maze room
let dungArmableTiles = {};   // "x,y" -> 1: tiles that held an arm-statue this visit (armed ones re-id as references)

// (id = base + 2*(tier-1), x3 variants at +0/+20/+40). skillIdx = SKILL_NAMES order (WC 8, Fish 10, Mine 14, Farm 19, Hunter 21, Div 25).
const DUNG_RES_SCALE = [1, 10, 20, 30, 40, 50, 60, 70, 80, 90];
const DUNG_RES_DIV   = [1, 10, 20, 30, 40, 50, 60, 70, 80, 85];
const DUNG_CRIT_MARGIN = 15;
const DUNG_DOOR_CRIT_MARGIN = 10;
const DUNG_CAP110_SKILLS = { Woodcutting: 1, Mining: 1, Fishing: 1, Farming: 1, Hunter: 1 };
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
// Local player's base level in RS3 skill idx (skills[idx] = [real, boosted, xp]), or null.
function dungSkillLevel(idx) {
  try { const s = lastSnap && lastSnap.skills && lastSnap.skills[idx]; return (s && s[0] > 0) ? s[0] : null; } catch (e) { return null; }
}
// Compare on alphanumerics only: the party interface name uses NBSP where JX_DISPLAY_NAME has a space.
function dungIsSelf(n) {
  if (!dungSelfName) return false;
  const strip = s => (s || '').replace(/[^a-z0-9]/gi, '').toLowerCase();
  return strip(n) === strip(dungSelfName);
}
function dungPartyBest(idx) {
  let best = dungSkillLevel(idx), by = null;
  for (const n in dungPartyStats) {
    if (!dungPartyRoster[n] || dungIsSelf(n)) continue;
    const lv = dungPartyStats[n] && dungPartyStats[n][idx];
    if (typeof lv === 'number' && (best == null || lv > best)) { best = lv; by = n; }
  }
  return { best: best, by: by };
}
function dungMaxTier(skillIdx, skillName) {
  const b = dungPartyBest(skillIdx);
  if (b.best == null) return { maxTier: null, best: b.best, by: b.by };
  const scale = skillName === 'Divination' ? DUNG_RES_DIV : DUNG_RES_SCALE;
  let mt = 0;
  for (let t = 0; t < scale.length; t++) if (scale[t] <= b.best) mt = t + 1;
  return { maxTier: mt, best: b.best, by: b.by };
}
function dungResBand(res, maxTier, best) {
  if (best == null || maxTier == null) return null;
  if (res.level > best) return 'bonus';
  const topBand = res.tier >= maxTier - 1;
  const guaranteed = res.level >= best - DUNG_CRIT_MARGIN;
  const t8 = !!DUNG_CAP110_SKILLS[res.skill] && best < 110 && res.tier === 8;
  return (topBand || guaranteed || t8) ? 'critical' : 'filler';
}
function dungResPathworthy(c, kk, roomsMap) {
  if ((c.groundKeys && c.groundKeys.length) || c.key || c.gate) return true;
  if (c.unex || !c.doors) return true;      // doors = 0 is missing data, not a one-door dead end
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
function dungDoorMine(dl) {
  if (!dl) return null;
  const dn = dl.skill === 'Constitution' ? 'Hitpoints' : dl.skill;
  const si = SKILL_NAMES.findIndex(n => n.toLowerCase() === dn.toLowerCase());
  return si >= 0 ? dungPartyBest(si).best : null;
}
function dungDoorBand(dl) {
  if (!dl) return null;
  if (dl.level >= 106) return 'critical';
  const best = dungDoorMine(dl);
  if (best == null) return null;
  if (dl.level > best) return 'above';
  if (dl.level < best - DUNG_DOOR_CRIT_MARGIN) return 'low';
  return 'critical';
}
// Emotes puzzle: NPC 10966 performs an emote; anim id -> dialogue option text. Progress = varc 1233 (67/134/201).
const DUNG_EMOTE_ANIM = { 855: 'nod', 856: 'shake head', 857: 'think', 858: 'bow',
                          859: 'angry', 860: 'cry', 861: 'laugh', 862: 'cheer',
                          863: 'wave', 864: 'clap', 865: 'dance' };
let dungEmoteLast = -1;   // last non-idle anim the paired statue played (latched until the next)
let dungEmoteWatch = '';  // "x,y" of the tracked statue; a pad change drops the stale latch
let dungLodeKey = '', dungLodeTick = -1;   // rotating crystal: active tile + game tick it appeared
let dungLodeWarnAt = -1, dungLodeCenterOn = false;   // predicted next-appearance tick already warned for; centre text active
let dungLodeCenterAt = 0, dungLodeSeenAt = 0;   // when the prompt went up / when 49510 was last seen (watchdogs)
let dungLodeMarkAt = 0;      // Date.now() of the last cycle mark (appearance/arrival), the ms countdown anchor
let dungLodeWinMs = 0;       // measured click-window length, ms
let dungLodeSuppress = false;   // sync phase (all four crystals up) -> no click prompt
let dungLodeInRoom = false;     // the large crystal is in the player's room -> countdown may show
let dungLodeCenter = null;   // {x,y} of the large crystal's tile (the arrival point)
let dungLodeDbg = '';        // crystal-room pad-resolution readout for the map dbg line
let dungMonoDone = {};       // "x,y" -> true: monoliths already charged to 100%
let dungSelfPos = null;      // {x,y} live player tile (fetch + tick)
let dungCurDoors = 0;        // door bitmask (N1 E2 S4 W8) of the player's current room
let dungRouteTarget = '';    // map cell 'gx,gy' clicked; route from the player's room to it
let dungBossWarnOn = false;  // boss 9919 icicle-attack warning currently on screen
let dungPartyRoster = {};    // name -> 1, every player seen in this instance including self; p-indices come from the sorted name list
let dungSelfName = '';       // this client's own display name
let dungIcePlan = null;      // {sig, anchor:'lx,ly', stops:[{k:'lx,ly', press}...], idx} held slide tour; sig = room + sorted unpressed-pad set
let dungIceSettle = null;    // {key:'lx,ly', n} consecutive reconciles on the same tile (settle detector)
let dungIceDump = null;      // ice room model + plan, copyable JSON (map tab)
let dungMazeDump = null;     // poison-maze model + route, copyable JSON
let dungBarrelDump = null;   // barrel room: live pad-relative geometry + the rotation fit's per-turn scores
let dungColFerret = null;    // latched colour in the coloured-ferret room: one target at a time
const DUNG_FERRET_RGB = { Red: 0xff4444, Blue: 0x4488ff, Green: 0x33cc66,
                          Yellow: 0xffdd33, Orange: 0xff8822 };
let dungFerretPlan = null;   // {anchor:'x,y', stops:['x,y'...], idx} held fish route (stable while the ferret walks)
let dungFerretSettle = null; // {key:'x,y', n} ferret settle detector
let dungHerbHlLast = '';     // last uiHighlight rect this panel published ('' = none)
let dungHerbHlOwner = '';    // feature that published it; a tagged clear only wipes its own box, no tag = force-clear
function dungHerbClear(owner) {
  if (!dungHerbHlLast) return;
  if (owner && dungHerbHlOwner && dungHerbHlOwner !== owner) return;
  dungHerbHlLast = ''; dungHerbHlOwner = '';
  try { rtxData.sync('overlay.uiHighlight', 0, 0, 0, 0); } catch (e) {}
}
let dungHoardRiddle = null;  // {k, item, opt|gid, loc} or null
let dungGroundCache = [];    // last groundItems() result: hoardstalker piles live there
let dungPlantSeen = {};      // 'x,y|loc id' -> tick first seen in this room
let dungSeekerMem = {};      // npc uid -> {face, at, x, y, hd, mt, spd, turns, step, pend}
let dungCrysMem = {};        // colour name -> {d, dx, dy, c, t}
let dungPoltHerb = null;
// Dialogue text comp differs per group (1186 inscription = comp 3, 1184 npc line = comp 10), so scan a range.
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
let dungInvHlOwner = '';     // same owner rule as dungHerbHlOwner
function dungInvClear(owner) {
  if (owner && dungInvHlOwner && dungInvHlOwner !== owner) return;
  if (!dungInvHlLast) return;
  dungInvHlLast = ''; dungInvHlOwner = '';
  try { rtxData.sync('overlay.panelViz', ''); } catch (e) {}
}
function dungInvCount(itemId) {
  try {
    const inv = JSON.parse(rtxData.sync('state.inventory') || '{}');
    let n = 0;
    for (const it of (inv.items || [])) if (it[1] === itemId) n++;
    return n;
  } catch (e) { return 0; }
}
function dungHighlightInvItem(itemId, label, owner) {
  try {
    if (!itemId) { dungInvClear(owner); return 0; }
    const inv = JSON.parse(rtxData.sync('state.inventory') || '{}');
    const segs = [];
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
      rtxData.sync('overlay.panelViz', payload);
      dungInvHlLast = payload;
      return segs.length;
    }
    if (held) return 0;
    dungInvClear(owner);
  } catch (e) {}
  return 0;
}
let dungWasIn = false;       // in-dungeon latch: clear the shared overlay channels once on exit
function dungClearOverlays() {
  if (!dungWasIn) return;    // empty pushes outside would stomp quests/alerts
  dungWasIn = false;
  dungHighlightList([]); dungGuideTiles([]); dungHerbClear(); dungInvClear();
  dungLodeKey = ''; dungLodeTick = -1; dungLodeWarnAt = -1; dungLodeCenter = null;
  dungIcePlan = null; dungIceSettle = null; dungIceDump = null; dungMazeDump = null; dungFerretPlan = null; dungFerretSettle = null; dungColFerret = null;
  dungLodeMarkAt = 0; dungLodeWinMs = 0; dungLodeSuppress = false; dungLodeInRoom = false;
  if (dungLodeCenterOn) { dungLodeCenterOn = false; try { bridge().centerText(myPid(), ''); } catch (e) {} }
}

// One box per needle: "#<id>|label" or "name|label", ',' separated. Re-asserted every reconcile (shared channel).
function dungHighlightList(needles) {
  try { bridge().overlayHighlight(myPid(), (needles || []).map(s => String(s).replace(/,/g, ' ')).join(',')); } catch (e) {}
}
function dungHighlightNpc(id, label) {
  dungHighlightList(id ? ['#' + id + (label ? '|' + label : '')] : []);
}

// Guide-tile marks via overlay.guideMarks. Fields: x\x1f y\x1f plane\x1f label [\x1f snap \x1f rgb].
// The reader hides rgb-0 destination tiles when a labelled NPC box stands at the objective; coloured marks stay.
function dungGuideTiles(marks) {
  try {
    const recs = (marks || []).map(m =>
      (m.x | 0) + '\x1f' + (m.y | 0) + '\x1f' + ((m.plane || 0) | 0) + '\x1f'
      + String(m.label == null ? '' : m.label).slice(0, 95).replace(/[\x1e\x1f]/g, ' ')
      + ((m.rgb || m.snap) ? '\x1f' + (m.snap ? 1 : 0) + '\x1f' + ((m.rgb | 0) || 0) : ''));
    rtxData.sync('overlay.guideMarks', recs.join('\x1e'));
  } catch (e) {}
}

function dungReconcileScene(npcs, objs) {
  npcs = npcs || [];
  if (!dungWasIn) {   // first reconcile since load/exit: sweep a stale centre text
    try { bridge().centerText(myPid(), ''); } catch (e) {}
  }
  dungWasIn = true;
  const here = e => {
    if (!dungFloorSW || !dungSelfPos || typeof e.x !== 'number') return false;
    const a = dungRoomOf(dungFloorSW, e.x, e.y), b = dungRoomOf(dungFloorSW, dungSelfPos.x, dungSelfPos.y);
    return a.rx === b.rx && a.ry === b.ry;
  };
  {
    const poltHere = npcs.some(n => n.id === 11245 && typeof n.x === 'number' && here(n))
      || (objs || []).some(o => typeof o.x === 'number' && here(o) &&
           ((o.id >= 54074 && o.id <= 54081) || (o.id >= 54094 && o.id <= 54101)));
    if (!poltHere) { dungHerbClear('herb'); dungInvClear('polt'); dungPoltHerb = null; }
  }
  const marks = [];   // guide-tile channel: grooves/switches locs + statue target tile
  // Suspicious grooves: safe tiles vary per floor and no id rule holds, so only observed 67099 (confirmed safe) is marked.
  {
    const marked = {};
    for (const o of (objs || []))
      if (o.id === 67099 && typeof o.x === 'number' && here(o) && !marked[o.x + ',' + o.y] && marks.length < 16) {
        marked[o.x + ',' + o.y] = true;
        marks.push({ x: o.x, y: o.y, plane: o.plane || 0, label: 'Safe', rgb: 0x5fd07a });
      }
  }
  // Poison maze tile blockers: pedestal families (js5-16), Mine-actioned 49360-49374 + 54412-54416, the rest actionless.
  const DUNG_MAZE_PEDESTAL = {};
  for (const pr of [[49360, 49374], [50957, 50967], [51503, 51513], [52051, 52061],
                    [54412, 54416], [54976, 54988], [55877, 55887]])
    for (let pi = pr[0]; pi <= pr[1]; pi++) DUNG_MAZE_PEDESTAL[pi] = 1;
  const dungMazeBlocks = o =>
    DUNG_MAZE_PEDESTAL[o.id]
    || o.name === 'Pedestal' || o.name === 'Switch'
    || (o.actions || []).some(a => a === 'Mine');
  const DUNG_CHEST_LOCKED = { 49345:1, 49346:1, 49347:1, 49886:1, 49887:1, 49888:1,
                              49889:1, 49890:1, 49891:1, 49892:1, 49893:1, 49894:1, 49895:1 };
  const DUNG_CHEST_OPEN   = { 49348:1, 49349:1, 49350:1,
                              49896:1, 49897:1, 49908:1, 49909:1 };
  const mazeChest = (objs || []).find(o => (DUNG_CHEST_LOCKED[o.id] || o.name === 'Locked chest')
                                           && typeof o.x === 'number' && here(o));
  const mazeDone = (objs || []).some(o => (DUNG_CHEST_OPEN[o.id] || o.name === 'Open chest')
                                          && typeof o.x === 'number' && here(o));
  const mbars = (objs || []).filter(o => o.name === 'Barrier' && typeof o.x === 'number' && here(o));
  const isMazeRoom = mazeChest && !mazeDone && mbars.length >= 5;   // a Locked chest also sits in the emote room; barriers disambiguate
  if (isMazeRoom && dungFloorSW && dungSelfPos) {
    // Never gate on varc 1233 (reads 0 while running; shared with monolith/emote progress). Start switch ids per theme (js5-16).
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
      const mineable = {};
      for (const o of (objs || [])) {
        if (typeof o.x !== 'number' || !dungMazeBlocks(o)) continue;
        const lx = o.x - swx, ly = o.y - swy;
        if (lx < 0 || lx > 13 || ly < 0 || ly > 13) continue;
        if ((o.actions || []).some(a => a === 'Mine')) mineable[lx + ',' + ly] = 1;
        else blk[lx + ',' + ly] = 1;
      }
      // Entries are [x, y, type, rot]: type 0 = one edge, 2 = corner (rot and rot+1); dirs 0=W 1=N 2=E 3=S; rotation r maps (x,y)->(y,15-x).
      const TPL_WALLS = [[0,1,0,2],[0,2,0,2],[0,3,0,2],[0,4,0,2],[0,5,0,2],[0,6,0,2],[0,7,0,2],[0,8,0,2],[0,9,0,2],[0,10,0,2],[0,11,0,2],[0,12,0,2],[0,13,0,2],[0,14,0,2],[1,0,0,1],[1,2,0,2],[1,3,0,2],[1,5,0,2],[1,6,0,2],[1,7,0,2],[1,8,0,2],[1,9,0,2],[1,10,0,2],[1,11,0,2],[1,13,0,2],[1,15,0,3],[2,0,0,1],[2,1,0,1],[2,3,0,2],[2,4,0,2],[2,5,0,2],[2,6,0,2],[2,7,0,2],[2,9,0,2],[2,10,0,2],[2,11,0,2],[2,12,0,2],[2,14,0,3],[2,15,0,3],[3,0,0,1],[3,1,0,1],[3,2,0,1],[3,4,0,2],[3,5,0,2],[3,6,0,2],[3,7,0,2],[3,8,0,2],[3,9,0,2],[3,10,0,2],[3,11,0,2],[3,13,0,3],[3,14,0,3],[3,15,0,3],[4,0,0,1],[4,2,0,1],[4,3,0,1],[4,5,0,2],[4,7,0,2],[4,8,0,2],[4,9,0,2],[4,10,0,2],[4,12,0,3],[4,13,0,3],[4,14,0,3],[4,15,0,3],[5,0,0,1],[5,1,0,1],[5,2,0,1],[5,4,0,1],[5,6,0,2],[5,7,0,2],[5,8,0,2],[5,9,0,2],[5,11,0,3],[5,12,0,3],[5,13,0,3],[5,14,0,3],[5,15,0,3],[6,0,0,1],[6,1,0,1],[6,2,0,1],[6,3,0,1],[6,4,0,1],[6,5,0,1],[6,10,0,3],[6,11,0,3],[6,13,0,3],[6,14,0,3],[6,15,0,3],[7,0,0,1],[7,1,0,1],[7,2,0,1],[7,3,0,1],[7,4,0,1],[7,5,0,1],[7,11,0,3],[7,12,0,3],[7,13,0,3],[7,14,0,3],[7,15,0,3],[8,0,0,1],[8,2,0,1],[8,3,0,1],[8,4,0,1],[8,10,0,3],[8,11,0,3],[8,12,0,3],[8,13,0,3],[8,15,0,3],[9,0,0,1],[9,1,0,1],[9,2,0,1],[9,3,0,1],[9,4,0,1],[9,5,0,1],[9,10,0,3],[9,11,0,3],[9,12,0,3],[9,13,0,3],[9,14,0,3],[9,15,0,3],[10,0,0,1],[10,1,0,1],[10,3,0,1],[10,4,0,1],[10,6,0,0],[10,7,0,0],[10,8,0,0],[10,9,0,0],[10,11,0,3],[10,13,0,3],[10,14,0,3],[10,15,0,3],[11,0,0,1],[11,1,2,0],[11,2,0,1],[11,3,0,1],[11,5,0,0],[11,6,0,0],[11,7,0,0],[11,9,0,0],[11,10,0,0],[11,12,0,3],[11,13,0,3],[11,14,0,3],[11,15,0,3],[12,0,0,1],[12,1,0,1],[12,2,0,1],[12,4,0,0],[12,5,0,0],[12,6,0,0],[12,7,0,0],[12,8,0,0],[12,9,0,0],[12,10,0,0],[12,11,0,0],[12,13,0,3],[12,14,0,3],[12,15,0,3],[13,0,0,1],[13,1,0,1],[13,3,0,0],[13,4,0,0],[13,5,0,0],[13,6,0,0],[13,8,0,0],[13,9,0,0],[13,10,0,0],[13,11,0,0],[13,12,0,0],[13,14,0,3],[13,15,0,3],[14,0,0,1],[14,2,0,0],[14,4,0,0],[14,5,0,0],[14,6,0,0],[14,7,0,0],[14,8,0,0],[14,9,0,0],[14,11,0,0],[14,12,0,0],[14,13,0,0],[14,15,0,3],[15,1,0,0],[15,2,0,0],[15,3,0,0],[15,4,0,0],[15,5,0,0],[15,6,0,0],[15,7,0,0],[15,8,0,0],[15,9,0,0],[15,10,0,0],[15,11,0,0],[15,12,0,0],[15,13,0,0],[15,14,0,0]];
      const TPL_BLOCK = [[6,6],[6,9],[7,6],[9,6],[9,9]];   // centre pillars + rock (scenery on the outer walkway is outside the playfield)
      const TPL_BARS = [[1,4,2],[1,12,2],[2,8,2],[4,1,1],[4,6,2],[5,3,1],[6,12,3],[7,10,3],[8,5,1],[8,14,3],[10,2,1],[10,12,3],[11,8,0],[13,7,0],[14,3,0],[14,10,0]];
      const TPL_DOOR = [8, 1, 1], TPL_CHEST = [8, 8];
      const rotP = (x, y, r) => { for (let i2 = 0; i2 < r; i2++) { const t2 = x; x = y; y = 15 - t2; } return [x, y]; };
      const toLive = (x, y, r) => { const p4 = rotP(x, y, r); return [p4[0] - 1, p4[1] - 1]; };
      const gx = chest.x - swx, gy = chest.y - swy, sx = dungSelfPos.x - swx, sy = dungSelfPos.y - swy;
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
      const OPP = { 0: 2, 1: 3, 2: 0, 3: 1 };   // W<->E, N<->S
      const DIRD = { '1,0': 2, '-1,0': 0, '0,1': 1, '0,-1': 3 };   // step vector -> edge dir from source (world axes: +y = N)
      const edgeAt = (x, y, d, nx, ny) => {
        const k1 = x + ',' + y + ',' + d, k2 = nx + ',' + ny + ',' + OPP[d];
        return { wall: wallE[k1] || wallE[k2], pass: passE[k1] || passE[k2] };
      };
      const open = (x, y) => x >= 0 && x <= 13 && y >= 0 && y <= 13 && !blk[x + ',' + y];
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
  // Barrel room: take the bits (item 17422), Fix the barrel, push it under the Expelling pipe, then onto the Pressure pad.
  // State is read from live actions with id sets as fallback; an actionless Barrel (11074) is finished.
  {
    const inR = e => typeof e.x === 'number' && here(e);
    const B_PUSH = { 11072: 1, 11073: 1, 11686: 1 };
    const B_FIX  = { 11075: 1 };
    const B_DONE = { 11074: 1 };
    const P_SPOUT = { 39969: 1, 49687: 1, 49689: 1, 49692: 1, 54288: 1 };
    const P_PAD   = { 52206: 1, 54282: 1, 35232: 1 };
    const BITS = 17422;
    // BARREL_SOLN = the 5 one-tile pushes as [fromOff, toOff].
    const BARREL_OFFS = [[3,-4],[3,-3],[3,-2],[4,-5],[4,-2],[4,-1],[5,-4],[5,-3],[5,-1],
                         [-2,-8],[-1,-8],[-2,3],[-1,3]];
    const BARREL_SOLN = [[[5,-3],[5,-4]],[[5,-1],[5,0]],[[4,-1],[3,-1]],[[-1,3],[-1,4]],[[-2,3],[-3,3]]];
    const rotOff = (p, r) => { let x = p[0], y = p[1]; for (let i = 0; i < r; i++) { const t = x; x = y; y = -t; } return [x, y]; };
    const isBarrel = n => n.name === 'Barrel';
    const act = (e, a) => (e.actions || []).some(x => x === a);
    const broken = npcs.find(n => inR(n) && (B_FIX[n.id] || (isBarrel(n) && act(n, 'Fix'))));
    const spout  = (objs || []).find(o => inR(o) && (P_SPOUT[o.id] || o.name === 'Expelling pipe'));
    const pad    = (objs || []).find(o => inR(o) && (P_PAD[o.id] || o.name === 'Pressure pad'));
    const doneB  = npcs.find(n => inR(n) && (B_DONE[n.id] || (isBarrel(n) && !(n.actions || []).length)));
    if (broken || spout || (pad && npcs.some(n => inR(n) && isBarrel(n)))) {
      const settled = doneB && pad && doneB.x === pad.x && doneB.y === pad.y;
      if (settled) {
        dungGuideTiles(marks);
        dungHighlightNpc(0);
        return;
      }
      const bitsTile = (dungGroundCache || []).find(g => g && g.id === BITS && here(g));
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
      const rotTied = rotScores.filter(h => h === bestHits).length > 1;   // ambiguous fit: refuse to guide
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
          dungHighlightList(['#' + ps.b.id + '|Push this barrel  ('
                             + ps.step + ' of ' + ps.of + ')|'
                             + ps.b.x + ';' + ps.b.y + ';1']);
          return;
        }
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
        dungHighlightList(['#' + broken.id + '|Fix this barrel|'
                           + broken.x + ';' + broken.y + ';1']);
        return;
      }
      // fill level = varc 1233, 0..200 (dungMonoCharge); full -> pad, else -> spout
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
      if (!full && pad && marks.length < 16)
        marks.push({ x: pad.x, y: pad.y, plane: pad.plane || 0, rgb: 0x4a90d9, label: '' });
      dungGuideTiles(marks);
      dungHighlightNpc(0);
      return;
    }
  }
  {
    const colFerrets = npcs.filter(n => typeof n.x === 'number' && here(n)
                                     && /^[A-Z][a-z]+ ferret$/.test(String(n.name || '')));
    if (colFerrets.length) {
      const todo = colFerrets.filter(n => (n.actions || []).some(a => a === 'Scare'));
      if (dungColFerret && !todo.some(n => String(n.name).split(' ')[0] === dungColFerret))
        dungColFerret = null;
      if (!dungColFerret && todo.length) {
        let best = null, bestD = Infinity;
        for (const f of todo) {
          const d = dungSelfPos
            ? Math.max(Math.abs(f.x - dungSelfPos.x), Math.abs(f.y - dungSelfPos.y)) : 0;
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
        if (plate) marks.push({ x: plate.x, y: plate.y, plane: plate.plane || 0,
                                label: '', rgb: rgb });
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
        dungHighlightList(['#' + pick.id]);
      } else {
        dungGuideTiles(marks);
        dungHighlightList([]);
      }
      return;
    }
  }
  {
    const DUNG_FERRET_TILE = { 49546: 1, 49547: 1, 49548: 1, 54293: 1 };
    const DUNG_FERRET_HOLE = { 49549: 1, 49550: 1, 49551: 1, 49552: 1, 49553: 1, 49554: 1,
                               54294: 1, 54295: 1 };
    const DUNG_FERRET_PLATE = { 49555: 1, 49556: 1, 49557: 1, 49558: 1, 49559: 1, 49560: 1,
                                54296: 1, 54297: 1 };
    const DUNG_FERRET_NPC = { 11007: 1, 11010: 1 };   // the Daemonheim ferrets; other "Ferret" npcs are Hunter catches
    const ferret = npcs.find(n => DUNG_FERRET_NPC[n.id] && typeof n.x === 'number' && here(n));
    const plateO = (objs || []).find(o => DUNG_FERRET_PLATE[o.id] && typeof o.x === 'number' && here(o));
    if (ferret && plateO) {
      const ftiles = (objs || []).filter(o =>
        (DUNG_FERRET_TILE[o.id] || DUNG_FERRET_HOLE[o.id]) && typeof o.x === 'number' && here(o));
      if (ftiles.length >= 9) {
        const fsk = ferret.x + ',' + ferret.y, fpk = plateO.x + ',' + plateO.y;
        if (dungFerretSettle && dungFerretSettle.key === fsk) dungFerretSettle.n++;
        else dungFerretSettle = { key: fsk, n: 1 };
        const fsettled = dungFerretSettle.n >= 2;
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
  // Lodestone pillar room: fixed jump route over a 7x7 'Gap' lattice (49567/68/69), in lattice-local coords.
  {
    const DUNG_LODE_ROUTE = [[6, 5], [5, 4], [3, 4], [1, 4], [0, 3], [0, 1]];
    const lode = (objs || []).find(o => o.name === 'Lodestone' && typeof o.x === 'number' && here(o) &&
      o.actions && o.actions.length);
    // solved = 'Active lodestone' 49573 / 'Activated lodestone' (other themes), all actionless
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
  // Ice-slide room. Pads stop a slide (pressed by landing; 49320-23 unpressed -> 49324-27 pressed, both stop);
  {
    const iceUnpressed = (objs || []).filter(o =>
      o.id >= 49320 && o.id <= 49323 && typeof o.x === 'number' && here(o));
    const icePressed = (objs || []).filter(o =>
      o.id >= 49324 && o.id <= 49327 && typeof o.x === 'number' && here(o));
    const ICE_FURN = { 49328: 1, 49329: 1, 49330: 1 };
    const iceFurn = (objs || []).filter(o =>
      ICE_FURN[o.id] && typeof o.x === 'number' && here(o));
    if (iceUnpressed.length && dungFloorSW && dungSelfPos) {
      const pr = dungRoomOf(dungFloorSW, dungSelfPos.x, dungSelfPos.y);
      const swx = dungFloorSW.x + DUNG_ROOM_PITCH * pr.rx, swy = dungFloorSW.y + DUNG_ROOM_PITCH * pr.ry;
      const roomKey = pr.rx + ',' + pr.ry;
      const ICE_LO = 1, ICE_HI = DUNG_ROOM_W - 2;
      const furn = {}, pad = {};   // furn value = the furniture's name, for "stops at X" labels
      for (const o of iceFurn) furn[(o.x - swx) + ',' + (o.y - swy)] = o.name || 'furniture';
      for (const o of iceUnpressed) pad[(o.x - swx) + ',' + (o.y - swy)] = 1;
      for (const o of icePressed) pad[(o.x - swx) + ',' + (o.y - swy)] = 1;
      const px = dungSelfPos.x - swx, py = dungSelfPos.y - swy;
      const posKey = px + ',' + py;
      if (dungIceSettle && dungIceSettle.key === posKey) dungIceSettle.n++;
      else dungIceSettle = { key: posKey, n: 1 };
      const settled = dungIceSettle.n >= 2;
      const rem = iceUnpressed.map(o => ({ x: o.x - swx, y: o.y - swy }));
      const remSig = roomKey + '|' + rem.map(b => b.x + ',' + b.y).sort().join(';');
      if (dungIcePlan && dungIcePlan.sig === remSig) {
        while (dungIcePlan.idx < dungIcePlan.stops.length && dungIcePlan.stops[dungIcePlan.idx].k === posKey) dungIcePlan.idx++;
        if (dungIcePlan.idx >= dungIcePlan.stops.length) dungIcePlan = null;
        else if (settled && !(dungIcePlan.idx === 0
                   ? posKey === dungIcePlan.anchor
                   : dungIcePlan.stops[dungIcePlan.idx - 1].k === posKey))
          dungIcePlan = null;
      } else if (dungIcePlan) dungIcePlan = null;
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
        const m0 = 0;
        const start = posKey + ':' + m0;
        const prev = {}; prev[start] = null;
        const q = [[px, py, m0]];
        let goalK = null, bestK = null, bestBits = popc(m0);
        for (let qi = 0; qi < q.length && !goalK; qi++) {
          const cx2 = q[qi][0], cy2 = q[qi][1], m = q[qi][2];
          for (const dd of DIRS) {
            const to = slideTo(cx2, cy2, dd);
            if (!to) continue;
            const nm = m | (bit[to[0] + ',' + to[1]] || 0);
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
          const stops = [];
          for (let k = endK; k && k !== start; k = prev[k]) {
            const kp = k.split(':')[0], pp = prev[k].split(':')[0];
            const [sx2, sy2] = kp.split(',').map(Number), [ox2, oy2] = pp.split(',').map(Number);
            const dx2 = Math.sign(sx2 - ox2), dy2 = Math.sign(sy2 - oy2);
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
        v: 'ice-v6',   // model version stamp
        room: roomKey, sw: [swx, swy], self: [px, py], settled: settled,
        padsUn: iceUnpressed.map(iceLoc), padsPr: icePressed.map(iceLoc), furn: iceFurn.map(iceLoc),
        other: (objs || []).filter(o => typeof o.x === 'number' && here(o)
          && !(o.id >= 49320 && o.id <= 49330)).map(iceLoc),
        npcs: (npcs || []).filter(n => typeof n.x === 'number' && here(n))
          .map(n => ({ id: n.id, n: n.name || '', x: n.x - swx, y: n.y - swy })),
        plan: dungIcePlan ? dungIcePlan.stops.slice(dungIcePlan.idx) : null
      };
      if (dungIcePlan && settled) {
        let step = 1;
        for (const s of dungIcePlan.stops.slice(dungIcePlan.idx, dungIcePlan.idx + 2)) {
          if (marks.length >= 16) break;
          const p2 = s.k.split(',').map(Number);
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
  const DUNG_SWITCH_PULL = { 49381: 1, 49382: 1, 49383: 1, 54333: 1 };
  const DUNG_SWITCH_DONE = { 49384: 1, 49385: 1, 49386: 1, 54334: 1 };
  const pulled = {};
  for (const o of (objs || [])) if (DUNG_SWITCH_DONE[o.id] && typeof o.x === 'number') pulled[o.x + ',' + o.y] = true;
  for (const o of (objs || []))
    if (DUNG_SWITCH_PULL[o.id] && typeof o.x === 'number' && here(o) && !pulled[o.x + ',' + o.y] && marks.length < 16)
      marks.push({ x: o.x, y: o.y, plane: o.plane || 0, label: (o.name || '') + '\nPull' });
  // Fremennik scout room (npc 11001): crate state machine work -> Take-from -> Empty at the same tile.
  // Crate ids per theme: fish 35275/49522-24/54302, bars 35277/49528-30/54304, logs 35279/49534-36/54306.
  const DUNG_SCOUT_CRATES = {};
  for (const ci of [35275, 49522, 49523, 49524, 54302]) DUNG_SCOUT_CRATES[ci] = 'Cook the fish';
  for (const ci of [35277, 49528, 49529, 49530, 54304]) DUNG_SCOUT_CRATES[ci] = 'Smith battleaxes';
  for (const ci of [35279, 49534, 49535, 49536, 54306]) DUNG_SCOUT_CRATES[ci] = 'Fletch bows';
  const DUNG_SCOUT_DONE = {};
  for (const ci of [35276, 49525, 49526, 49527, 54303,
                    35278, 49531, 49532, 49533, 54305,
                    35280, 49537, 49538, 49539, 54307]) DUNG_SCOUT_DONE[ci] = 1;
  {
    const inSR = o => typeof o.x === 'number' && here(o);
    const works = (objs || []).filter(o => DUNG_SCOUT_CRATES[o.id] && inSR(o));
    const dones = (objs || []).filter(o => DUNG_SCOUT_DONE[o.id] && inSR(o));
    if (works.length || dones.length) {
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
  // Divine skinweaver boss: Tunnel locs 49286-49288 need blocking; a done one becomes "Blocked tunnel" 49289/49290.
  if (npcs.some(n => n.id === 10058)) {
    const DUNG_TUNNEL = { 49286: 1, 49287: 1, 49288: 1 };
    const DUNG_TUNNEL_DONE = { 49289: 1, 49290: 1, 49291: 1 };   // blocked = tunnel id + 3
    const tdone = {};
    for (const o of (objs || [])) if (DUNG_TUNNEL_DONE[o.id] && typeof o.x === 'number') tdone[o.x + ',' + o.y] = true;
    for (const o of (objs || []))
      if (DUNG_TUNNEL[o.id] && typeof o.x === 'number' && !tdone[o.x + ',' + o.y] && marks.length < 16)
        marks.push({ x: o.x, y: o.y, plane: o.plane || 0, label: (o.name || 'Tunnel') + '\nBlock', rgb: 0xe8b34b });
  }
  // base 49507-09/54275, active 49510-12/54276, solved 49513-15/54277.
  const DUNG_BIG_BASE   = { 49507:1, 49508:1, 49509:1, 54275:1 };
  const DUNG_BIG_ACTIVE = { 49510:1, 49511:1, 49512:1, 54276:1 };
  const DUNG_BIG_SOLVED = { 49513:1, 49514:1, 49515:1, 54277:1 };
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
  const DUNG_POWERUP = { 54263: 1, 54266: 1, 54269: 1, 54272: 1 };
  for (const pb of [49471, 49480, 49489, 49498]) { DUNG_POWERUP[pb] = 1; DUNG_POWERUP[pb + 1] = 1; DUNG_POWERUP[pb + 2] = 1; }
  if (!lodeSolved)
    for (const o of (objs || []))
      if (DUNG_POWERUP[o.id] && typeof o.x === 'number' && here(o) && marks.length < 16)
        marks.push({ x: o.x, y: o.y, plane: o.plane || 0, label: (o.name || '') + '\nPower up' });
  const activeLode = lodeSolved ? null : (objs || []).find(o => DUNG_BIG_ACTIVE[o.id] && typeof o.x === 'number' && here(o));
  // consecutive ids: 3 crystal | 3 Power-up | 3 lodestone (blue 49468, red 49477, green 49486, yellow 49495).
  const DUNG_CRYSTALS = {};
  for (const [base, n, c] of [[49468, 'Blue crystal', 0x5ab8f0], [49477, 'Red crystal', 0xf25c5c],
                              [49486, 'Green crystal', 0x5fd07a], [49495, 'Yellow crystal', 0xf0c419]])
    for (let v = 0; v < 3; v++) DUNG_CRYSTALS[base + v] = { n: n, c: c };
  DUNG_CRYSTALS[54262] = { n: 'Blue crystal',   c: 0x5ab8f0 };
  DUNG_CRYSTALS[54265] = { n: 'Red crystal',    c: 0xf25c5c };
  DUNG_CRYSTALS[54268] = { n: 'Green crystal',  c: 0x5fd07a };
  DUNG_CRYSTALS[54271] = { n: 'Yellow crystal', c: 0xf0c419 };
  const crys = lodeSolved ? [] : (objs || []).filter(o => DUNG_CRYSTALS[o.id] && typeof o.x === 'number' && here(o));
  const bigLode = lodeSolved ? null : (objs || []).find(o => DUNG_BIG_BASE[o.id] && typeof o.x === 'number' && here(o));
  if (bigLode) dungLodeCenter = { x: bigLode.x, y: bigLode.y };
  let ltick = -1;
  if (activeLode || bigLode || dungLodeTick >= 0) { try { ltick = bridge().gameTick(myPid()); } catch (e) {} }
  const dungLodeMarkEnd = () => { dungLodeKey = ''; };
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
    if (lk !== dungLodeKey) { dungLodeKey = lk; dungLodeAnchor(Date.now()); if (ltick >= 0) dungLodeTick = ltick; }
  } else if (bigLode) {
    const arr = crys.find(c => Math.max(Math.abs(c.x - bigLode.x), Math.abs(c.y - bigLode.y)) <= 1);
    if (arr) {
      dungLodeSeenAt = Date.now();
      const lk = 'arr' + arr.id;
      if (lk !== dungLodeKey) { dungLodeKey = lk; dungLodeAnchor(Date.now()); if (ltick >= 0) dungLodeTick = ltick; }
    } else dungLodeMarkEnd();
  } else dungLodeMarkEnd();
  if (dungLodeTick >= 0 && dungLodeSeenAt && Date.now() - dungLodeSeenAt > 12000) { dungLodeTick = -1; dungLodeWarnAt = -1; }
  dungLodeInRoom = !!(activeLode || bigLode);
  let crysOutSync = false;
  dungLodeDbg = '';
  if (crys.length >= 2 || Object.keys(dungCrysMem).length >= 2) {
    let ctr = dungLodeCenter;
    if (!ctr && crys.length >= 3) {   // vertical movers share x, horizontal movers share y
      const xs = {}, ys = {};
      for (const c of crys) { xs[c.x] = (xs[c.x] || 0) + 1; ys[c.y] = (ys[c.y] || 0) + 1; }
      const cx = Object.keys(xs).find(k => xs[k] >= 2), cy = Object.keys(ys).find(k => ys[k] >= 2);
      if (cx != null && cy != null) ctr = { x: +cx, y: +cy };
    }
    if (ctr) {
      // the out-of-sync notice goes on the arm's pressure pad (loc 52206/54282, +-5 from the centre), not the crystal
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
        if (c.x === ctr.x && c.y !== ctr.y) return { x: ctr.x, y: ctr.y + 5 * Math.sign(c.y - ctr.y), plane: c.plane || 0 };
        if (c.y === ctr.y && c.x !== ctr.x) return { x: ctr.x + 5 * Math.sign(c.x - ctr.x), y: ctr.y, plane: c.plane || 0 };
        return null;
      };
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
      const padOn = (dx, dy) => {
        for (const p of cpads)
          if (Math.sign(p.x - ctr.x) === dx && Math.sign(p.y - ctr.y) === dy) return p;
        return (dx || dy) ? { x: ctr.x + PLATE_D * dx, y: ctr.y + PLATE_D * dy, plane: 0 } : null;
      };
      dungLodeDbg = ' | crys[v4] ctr ' + ctr.x + ',' + ctr.y + ' pads ' + cpads.length
                  + ' known ' + ents.length + ' hidden ' + ents.filter(e => e.hidden).length;
      if (ents.length >= 2) {
        const CYC = PLATE_D;                      // 0=under pad, 1..4 = rail, then wrap
        const step = e => PLATE_D - e.d;          // d5->0, d4->1, d3->2, d2->3, d1->4
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
          if (!k || marks.length >= 16) continue;
          const at = padOn(e.dx, e.dy);
          if (!at) continue;
          const short = e.nm.replace(/ crystal$/i, '');
          marks.push({ x: at.x, y: at.y, plane: at.plane || 0, rgb: e.c,
                       label: '-Pressure pad\n' + short + '  HOLD ' + k });
        }
      }
    }
  }
  dungLodeSuppress = crys.length >= 4 || (crys.length >= 2 && crysOutSync);
  const DUNG_LO_GREEN  = { 3873:1, 39859:1, 49638:1, 49639:1, 49640:1, 54065:1 };
  const DUNG_LO_YELLOW = { 3874:1, 39860:1, 49641:1, 49642:1, 49643:1, 54066:1 };
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
      let pickSol = toGreen, frgb = 0x5fd07a, ftgt = 'green';
      if (toYellow && (!toGreen || toYellow.n < toGreen.n)) { pickSol = toYellow; frgb = 0xf0c419; ftgt = 'yellow'; }
      if (pickSol) {
        let flbl = 'Flip each marked tile (any order) -> all ' + ftgt;
        for (let i = 0; i < 25 && marks.length < 16; i++) if (pickSol.press[i]) {
          marks.push({ x: fat[i].x, y: fat[i].y, plane: fat[i].plane || 0, label: flbl, rgb: frgb });
          flbl = '';
        }
      }
    }
  }
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
      if (bestN >= 6) {
        const p = bestKey.split(',').map(Number);
        if (dungFloorSW && (dungFloorSW.x !== p[0] || dungFloorSW.y !== p[1])) {
          dungRoomRes = {}; dungKeyDoor = {};
        }
        dungFloorSW = { x: p[0], y: p[1] };
        dungSaveMarks();
      }
    }
  }
  // own grid. Room-local grids at cols 1-5 west / 8-12 east, rows 1-5 south / 8-12 north, walkway at 6-7.
  dungStatues = null;
  if (dungFloorSW) {
    const gcell = l => { const c = l - (l >= 7 ? 8 : 1); return (c >= 0 && c <= 4) ? c : null; };
    const rows = [];
    for (const pIdStr in DUNG_STATUE_PAIR) {
      const pId = +pIdStr, sId = DUNG_STATUE_PAIR[pIdStr];
      const pn = npcs.find(n => n.id === pId && typeof n.x === 'number' && here(n));
      const sn = npcs.find(n => n.id === sId && typeof n.x === 'number' && here(n));
      if (!pn || !sn) continue;
      const pl = dungRoomOf(dungFloorSW, pn.x, pn.y), sl = dungRoomOf(dungFloorSW, sn.x, sn.y);
      const cur = [gcell(pl.lx), gcell(pl.ly)], tgt = [gcell(sl.lx), gcell(sl.ly)];
      if (cur[0] == null || cur[1] == null || tgt[0] == null || tgt[1] == null) continue;   // off-grid (mid-push?)
      const swr = { x: dungFloorSW.x + DUNG_ROOM_PITCH * pl.rx, y: dungFloorSW.y + DUNG_ROOM_PITCH * pl.ry };
      rows.push({ id: pId, east: pl.lx >= 7, cur: cur, tgt: tgt,
                  tx: swr.x + (pl.lx >= 7 ? 8 : 1) + tgt[0], ty: swr.y + 1 + tgt[1],
                  done: cur[0] === tgt[0] && cur[1] === tgt[1] });
    }
    if (rows.length) dungStatues = rows;
  }
  // Boss 9919: anim 13338 = the icicle attack; centre-screen dodge warning while it plays
  {
    const iceBoss = npcs.find(n => n.id === 9919 && typeof n.x === 'number' && here(n));
    if (iceBoss && iceBoss.anim === 13338) {
      if (!dungBossWarnOn) { dungBossWarnOn = true; try { bridge().centerText(myPid(), 'DODGE - icicles!'); } catch (e) {} }
    } else if (dungBossWarnOn) {
      dungBossWarnOn = false;
      try { bridge().centerText(myPid(), ''); } catch (e) {}
    }
  }
  const talkGhost = npcs.find(n => n.id === 11246 && typeof n.x === 'number' && here(n));
  const ghostRoomHere = talkGhost
    || (objs || []).some(o => typeof o.x === 'number' && here(o) &&
         (DUNG_GHOST_PILLAR[o.id] || DUNG_GHOST_POT[o.id] || DUNG_GHOST_BOX[o.id]));
  if (ghostRoomHere) {
    const roomObjs = (objs || []).filter(o => typeof o.x === 'number' && here(o));
    const haveRing = dungInvCount(DUNG_GHOST_RING) > 0;
    const doneAt = { pot: {}, pillar: {}, box: {} };   // done-marker tiles per task
    for (const o of roomObjs) {
      const t = o.x + ',' + o.y;
      if (DUNG_GHOST_POT_DONE[o.id]) doneAt.pot[t] = 1;
      else if (DUNG_GHOST_PILLAR_DONE[o.id]) doneAt.pillar[t] = 1;
      else if (DUNG_GHOST_BOX_DONE[o.id]) doneAt.box[t] = 1;
    }
    if (haveRing) {
      for (const o of roomObjs) if (DUNG_GHOST_BOX[o.id] && !doneAt.box[o.x + ',' + o.y])
        marks.push({ x: o.x, y: o.y, plane: o.plane || 0, label: 'Fill -- put the ring back', rgb: 0x33cc66 });
    } else {
      for (const g of (dungGroundCache || []))
        if (g && g.id === DUNG_GHOST_RING && typeof g.x === 'number' && here(g))
          marks.push({ x: g.x, y: g.y, plane: g.plane || 0, label: 'Take the antique ring', rgb: 0x33cc66 });
    }
    for (const o of roomObjs) {
      const t = o.x + ',' + o.y;
      if (DUNG_GHOST_PILLAR[o.id] && !doneAt.pillar[t])
        marks.push({ x: o.x, y: o.y, plane: o.plane || 0, label: 'Repair the pillar' });
      else if (DUNG_GHOST_POT[o.id] && !doneAt.pot[t])
        marks.push({ x: o.x, y: o.y, plane: o.plane || 0, label: 'Repair the pot' });
    }
    if (!marks.length) {
      const isCoffin = o => o.name === 'Coffin'
                         || DUNG_GHOST_COFFIN[o.id] || DUNG_GHOST_COFFIN_BLESS[o.id];
      const hasAct = (o, a) => (o.actions || []).some(x => x === a);
      const coffinDone = {};
      for (const o of roomObjs)
        if (isCoffin(o) && !(o.actions || []).length) coffinDone[o.x + ',' + o.y] = 1;
      const pend = f => roomObjs.filter(o => isCoffin(o) && f(o) && !coffinDone[o.x + ',' + o.y]);
      const bless = pend(o => hasAct(o, 'Bless-remains'));
      const coffins = bless.length ? bless : pend(o => hasAct(o, 'Unlock'));
      for (const o of coffins)
        marks.push({ x: o.x, y: o.y, plane: o.plane || 0,
                     label: '-Coffin' + String.fromCharCode(10)
                          + (bless.length ? 'Bless the remains' : 'Unlock the coffin'),
                     snap: true, rgb: 0xffcc33 });
    }
    dungGuideTiles(marks);
    dungHighlightNpc(marks.length ? 0 : (talkGhost ? 11246 : 0),
                     marks.length ? '' : 'Nothing left to restore');
    return;
  }
  const ghosts = npcs.filter(n => n.id >= 10981 && n.id <= 11000 && typeof n.x === 'number' && here(n));
  const ghostNeedle = g => '#' + g.id + '|Kill this ghost|' + g.x + ';' + g.y + ';1';
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
  const DUNG_STATUE_READ = {
    11020: 'staff', 11027: 'staff', 11028: 'staff', 11029: 'staff',
    11021: 'sword', 11022: 'sword', 11023: 'sword', 11030: 'sword', 11031: 'sword', 11032: 'sword',
    11024: 'bow', 11025: 'bow', 11026: 'bow', 11033: 'bow', 11034: 'bow', 11035: 'bow',
    12108: 'staff', 12109: 'sword', 12110: 'bow', 12111: 'staff', 12112: 'sword', 12113: 'bow',
    13051: 'staff', 13052: 'sword', 13053: 'bow', 13054: 'staff', 13055: 'sword', 13056: 'bow',
  };
  const DUNG_ARM_IDS = n => (n >= 11012 && n <= 11014) || (n >= 11036 && n <= 11044)
    || n === 12106 || n === 13049 || (n >= 12094 && n <= 12096) || (n >= 13057 && n <= 13059);
  const armables = npcs.filter(n => DUNG_ARM_IDS(n.id) && typeof n.x === 'number' && here(n));
  if (!armables.length) dungArmableTiles = {};
  for (const a of armables) dungArmableTiles[a.x + ',' + a.y] = 1;
  const readRefs = npcs.filter(n => DUNG_STATUE_READ[n.id] && typeof n.x === 'number' && here(n) && !dungArmableTiles[n.x + ',' + n.y]);
  if (readRefs.length && armables.length) {
    const needles = [];
    for (const a of armables) {
      const r = readRefs.find(rr => rr.y === a.y || rr.x === a.x)
             || readRefs.find(rr => Math.abs(rr.y - a.y) <= 1 || Math.abs(rr.x - a.x) <= 1);
      if (r) needles.push('#' + a.id + '|Arm with ' + DUNG_STATUE_READ[r.id] + '|' + a.x + ';' + a.y);
    }
    if (needles.length) { dungGuideTiles(marks); dungHighlightList(needles); return; }
  }
  const DUNG_ARM_WEAPON = {
    11036: 'sword', 11037: 'sword', 11038: 'sword',   // -> 11051-53  melee
    11039: 'bow',   11040: 'bow',   11041: 'bow',     // -> 11045-47  ranged
    11042: 'staff', 11043: 'staff', 11044: 'staff',   // -> 11048-50  magic
    12094: 'sword', 12095: 'bow', 12096: 'staff',
    13057: 'sword', 13058: 'bow', 13059: 'staff',
  };
  const DUNG_ARM_STYLE = { sword: 'Melee', bow: 'Ranged', staff: 'Magic' };
  // 11012-11014 carry no param 74: infer from the armed statues present when exactly one style is missing.
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
  const DUNG_EMOTE_STATUE = { 10966: 1, 10967: 1, 10968: 1, 12114: 1, 12960: 1 };
  const DUNG_EMOTE_DONE   = { 10969: 1, 10970: 1, 10971: 1, 12115: 1, 12961: 1 };
  const DUNG_EMOTE_BROKEN = { 10972: 1, 10973: 1, 10974: 1, 12116: 1, 12962: 1 };
  const emoteStatues = npcs.filter(n =>
    (DUNG_EMOTE_STATUE[n.id] || DUNG_EMOTE_DONE[n.id] || DUNG_EMOTE_BROKEN[n.id])
    && typeof n.x === 'number' && here(n));
  if (emoteStatues.some(n => DUNG_EMOTE_STATUE[n.id])) {
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
      if (dungEmoteWatch !== wk) { dungEmoteWatch = wk; dungEmoteLast = -1; }
      if (typeof watch.anim === 'number' && watch.anim >= 0) dungEmoteLast = watch.anim;
      const opt = DUNG_EMOTE_ANIM[dungEmoteLast];
      if (opt) { try { PLUGIN_API['overlay.highlightOption'].run([opt], myPid()); } catch (e) {} }
      const prog = (dungMonoCharge != null && dungMonoCharge >= 0 && dungMonoCharge % 67 === 0) ? (dungMonoCharge / 67) : null;
      const lbl = (prog != null ? prog + '/3 done' : '')
        + (dungEmoteLast >= 0 ? (prog != null ? ' - ' : '') + (opt ? 'do: ' + opt : 'anim ' + dungEmoteLast) : '');
      dungGuideTiles(marks);
      dungHighlightList(['#' + watch.id + '|' + (lbl || 'Copy this statue') + '|' + watch.x + ';' + watch.y + ';1']);
      return;
    }
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
  // Poltergeist room (npc 11245). Locs: Sarcophagus 54078-81 Read/Open, 54082-85 opened; Herb patch 54074-76 Harvest;
  // Censer 54094-97 Add herb, 54098-101 Light, 54102-105 lit. The herb is named only by the sarcophagus inscription.
  const DUNG_POLT_HERBS = ['Corianger', 'Explosemary', 'Parslay',
                          'Cardamaim', 'Papreaper', 'Slaughtercress'];   // interface-720 option order
  // herb items 19653-19658 (same order; "#Consecrate" action) -> Consecrated herb 19659, which the censer wants
  const DUNG_HERB_ITEM = { Corianger: 19653, Explosemary: 19654, Parslay: 19655,
                           Cardamaim: 19656, Papreaper: 19657, Slaughtercress: 19658 };
  const DUNG_HERB_DONE_ITEM = 19659;
  const DUNG_HERB_SPENT_LOC = 50114;   // plain farming patch that replaces the herb patch once picked out
  const DUNG_HERB_GROUP = 720;         // "SELECT AN OPTION" picker; text comps at 15 + 3n, interfaceComps gives absolute rects
  const DUNG_HERB_COMPS = [18, 21, 24, 27, 30, 33].join(',');   // options 1..6
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
    const spentTiles = {};
    for (const o of (objs || [])) if (o.id === DUNG_HERB_SPENT_LOC && inRoom(o)) spentTiles[o.x + ',' + o.y] = 1;
    const patches   = (objs || []).filter(o => o.id >= 54074 && o.id <= 54076 && inRoom(o)
                                               && !spentTiles[o.x + ',' + o.y]);
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
      let herb = dungPoltHerb;
      {
        const t = dungDlgText();
        const hit = t && DUNG_POLT_HERBS.find(h => t.indexOf(h.toLowerCase()) >= 0);
        if (hit) { dungPoltHerb = herb = hit; }
      }
      for (const c2 of censLight)
        if (marks.length < 16) marks.push({ x: c2.x, y: c2.y, plane: c2.plane || 0, label: '-Censer\nLight', rgb: 0xe8b34b });
      if (censEmpty.length) {
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
          if (herb) for (const p2 of patches)
            if (marks.length < 16) marks.push({ x: p2.x, y: p2.y, plane: p2.plane || 0,
              label: '-Herb patch\nHarvest ' + herb + ' x' + censEmpty.length, rgb: 0x5fd07a });
          if (herb) dungHighlightHerb(herb);
          if (!herb) for (const sc of sarcs)
            if (marks.length < 16) marks.push({ x: sc.x, y: sc.y, plane: sc.plane || 0, label: '-Sarcophagus\nRead: it names the herb', rgb: 0x5ab8f0 });
        }
      } else {
        dungHerbClear('herb'); dungInvClear();
        const censKeys = Object.keys(censAt);
        if (censKeys.length && censKeys.every(k => censAt[k].st === 2))
          for (const sc of sarcs)
            if (marks.length < 16) marks.push({ x: sc.x, y: sc.y, plane: sc.plane || 0, label: '-Sarcophagus\nOpen it', rgb: 0x5fd07a });
      }
      dungGuideTiles(marks);
      dungHighlightNpc(0);
      return;
    }
  }
  // Pedestal room (js5-16): Pedestal 54110-13 idle | 54114-17 activated; Pillar 54118-21 base | 54122-25 Fix | 54126-29 done;
  {
    const inR = o => typeof o.x === 'number' && here(o);
    const isPed      = o => o.id >= 54110 && o.id <= 54117;
    const isFix      = o => o.id >= 54122 && o.id <= 54125;
    const isFixDone  = o => o.id >= 54126 && o.id <= 54129;
    const isMine     = o => (o.id >= 54130 && o.id <= 54133) || (o.id >= 54138 && o.id <= 54141);
    const isMineDone = o => (o.id >= 54134 && o.id <= 54137) || (o.id >= 54142 && o.id <= 54145);
    const fixDone = {}, mineDone = {};
    for (const o of (objs || [])) {
      if (!inR(o)) continue;
      if (isFixDone(o)) fixDone[o.x + ',' + o.y] = 1;
      else if (isMineDone(o)) mineDone[o.x + ',' + o.y] = 1;
    }
    const act = (o, a) => (o.actions || []).some(x => x === a);
    // vis = reader's report of a live rendered model (non-degenerate AABB); undefined = older reader, treat as visible
    const shown = o => o.vis !== false;
    const fixes = (objs || []).filter(o => isFix(o) && inR(o) && !fixDone[o.x + ',' + o.y] && act(o, 'Fix') && shown(o));
    const mines = (objs || []).filter(o => isMine(o) && inR(o) && !mineDone[o.x + ',' + o.y] && act(o, 'Mine') && shown(o));
    const peds  = (objs || []).filter(o => isPed(o) && inR(o));
    if (peds.length || fixes.length || mines.length) {
      for (const o of mines)
        if (marks.length < 16) marks.push({ x: o.x, y: o.y, plane: o.plane || 0, label: '-Rubble\nMine to clear', rgb: 0xe8b34b });
      for (const o of fixes)
        if (marks.length < 16) marks.push({ x: o.x, y: o.y, plane: o.plane || 0, label: '-Pillar\nFix', rgb: 0x5fd07a });
      dungGuideTiles(marks);
      dungHighlightNpc(0);
      return;
    }
  }
  const DUNG_SENTINEL = { 10941: 1, 25128: 1 };   // both npcs named 'Seeker sentinel'
  const DUNG_GAZE_OFFSET = 0;
  const DUNG_GAZE_LEN  = 9;
  const DUNG_GAZE_HALF = 22.5;
  const DUNG_SEEKER_SPAWN = { 10933: 1, 10935: 1, 10938: 1, 10939: 1 };
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
      const bearing = (fx, fy, tx, ty) => norm(Math.atan2(tx - fx, ty - fy) * 180 / Math.PI);
      const faceOf = n => {
        const m = dungSeekerMem[n.uid] || (dungSeekerMem[n.uid] = { face: null, at: 0, x: n.x, y: n.y });
        let f = (typeof n.face === 'number' && n.face >= 0) ? norm(n.face) : null;
        if (f === null && (m.x !== n.x || m.y !== n.y)) f = bearing(m.x, m.y, n.x, n.y);
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
      const sentFaces = sent.map(s => ({ s: s, f: faceOf(s) }));
      const inGaze = (x, y) => sentFaces.some(sf => {
        if (sf.f === null) return true;                 // unreadable: assume watched
        const sz2 = (typeof sf.s.size === 'number' && sf.s.size > 0) ? sf.s.size : 2;
        const o2 = (sz2 % 2 === 0) ? -0.5 : 0;
        return angdiff(bearing(sf.s.x + o2, sf.s.y + o2, x, y), norm(sf.f + DUNG_GAZE_OFFSET)) <= DUNG_GAZE_HALF;
      });
      const eta = (m, period) => {
        if (!m || !m.at) return '';
        const left = Math.max(0, period - ((now - m.at) % period) - 300) / 1000;
        return ' ~' + left.toFixed(1) + 's';
      };
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
      const needles = [];
      for (const sp of spawns) {
        const subdued = !(sp.actions || []).some(a => a === 'Subdue');
        if (subdued) { needles.push('#' + sp.id + '|Subdued'); continue; }
        const f = faceOf(sp);
        const m = dungSeekerMem[sp.uid];
        const away = f !== null && dungSelfPos
          && angdiff(f, bearing(sp.x, sp.y, dungSelfPos.x, dungSelfPos.y)) > 90;
        const gazed = inGaze(sp.x, sp.y);
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
        const sz = (typeof s.size === 'number' && s.size > 0) ? s.size : 2;
        const org = (sz % 2 === 0) ? -0.5 : 0;
        const start = Math.max(1, Math.ceil(sz / 2));   // just past the model edge
        const cx = s.x + org, cy = s.y + org;            // model centre, tile-index space
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
        
      }
      dungGuideTiles(marks);              // ground: the gaze cone only
      dungHighlightList(needles);         // npc boxes: spawns + unreadable sentinel
      return;
    }
  }
  //     blue   54504, 54525, 54546, 54623      green  54506, 54527, 54548, 54625
  //     yellow 54508, 54529, 54550, 54627      violet 54510, 54531, 54552, 54629
  //   Shelves "Mix Blue/Green/Yellow/Violet": 35241, 35242, 35243, 35245, 35246
  //   vials: Blue 19869, Green 19871, Yellow 19873, Violet 19875
  // 13024, 13029, 13034, 13039, 13044 are the generics. Once a vial is applied the npc
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
      const seated = recs.filter(r => blockAt[kk(r)]).sort((p1, p2) => p1.x - p2.x || p1.y - p2.y);
      const freeR  = recs.filter(r => !blockAt[kk(r)]);
      const loose  = blocks.filter(b => !recs.some(r => kk(r) === kk(b)));
      const pending = seated.filter(r => !DUNG_BLOCK_COL[blockAt[kk(r)].id]);
      if (!loose.length && !pending.length) {
        dungInvClear('recess');                 // solved: say nothing at all
        dungGuideTiles(marks);
        dungHighlightNpc(0);
        return;
      }
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
      const holdCol = pending.map(r => DUNG_RECESS[r.id])
                             .find(c => DUNG_VIAL_ITEM[c] && dungInvCount(DUNG_VIAL_ITEM[c]));
      for (const r of pending) {
        const c = DUNG_RECESS[r.id];
        if (c !== holdCol || marks.length >= 16) continue;
        marks.push({ x: r.x, y: r.y, plane: r.plane || 0, rgb: DUNG_RECESS_RGB[c] || 0xffffff,
                     label: 'Use the ' + c + ' vial here' });
      }
      if (holdCol) dungHighlightInvItem(DUNG_VIAL_ITEM[holdCol],
                                        'Use on the ' + holdCol + ' block', 'recess');
      else dungInvClear('recess');
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
  const DUNG_PLANT_DEAD = {
    35588:'blue',   35625:'blue',   35718:'blue',   35800:'blue',
    35604:'purple', 35685:'purple', 35720:'purple', 35808:'purple',
    35609:'red',    35708:'red',    35739:'red',    35812:'red',
    35613:'yellow', 35712:'yellow', 35780:'yellow', 35835:'yellow' };
  const DUNG_PLANT_RGB = { blue:0x4aa3ff, purple:0xb06cff, red:0xff5a5a, yellow:0xf2d24b };
  const DUNG_PLANT_MAZES = [
    ['################','#.....#........#','#..UU#####.....#','#..UU#.#.#C#...#','#...##.##..##..#','##C##...C...C..#','#...#...#...#..#','##..##.####.##.#','##...C.#..C..###','##C#.#.#..##...#','#..###.C#..#...#','#......#.#.##..#','#...#C##...##..#','#..##....#C##..#','#..#.....#..#..#','################'],
    ['################','#..#.....#.#...#','#..##C####.C...#','#..##......#...#','##C#..##C###...#','#....##.......##','#..###....##C###','#..C..#..##...##','#.##...#.#.UU###','#.#....###.UU.##','#.###..#.###...#','#.###..C.....###','#...##.###.#C###','###C##...###...#','#.........C....#','################'],
    ['################','#....#....##...#','#...##C..##....#','#...##.#C#..#C##','###........##.##','#.###C####C#..##','#.##..........##','#.#...###C#..###','#.#..##...##.#.#','#....C..UU.C.#.#','#....###UU.#...#','#...##.#..##...#','#..##.###C#.##.#','#.##....#......#','#.#....##......#','################'],
    ['################','#..........#...#','##C#...#C#C#...#','#..#C###....####','#........##C#.##','#..###..#......#','##C#.#C##.#....#','##........#.#..#','##........##.#.#','#..####C#..#...#','#..#..#.####C#.#','#.#UU.#......###','#C.UU.C........#','#.....##.......#','#......###.....#','################'],
  ];
  {
    const inR = o => typeof o.x === 'number' && here(o);
    const plants = (objs || []).filter(o =>
      (DUNG_PLANT_UPROOT[o.id] || DUNG_PLANT_CHOP[o.id] || DUNG_PLANT_DEAD[o.id])
      && o.vis !== false && inR(o));
    const ups = plants.filter(o => DUNG_PLANT_UPROOT[o.id]);
    if (!ups.length) dungPlantSeen = {};       // not in the room: drop the history
    else {
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
          const deadNew = !!DUNG_PLANT_DEAD[o.id], deadCur = !!DUNG_PLANT_DEAD[tile[tk].o.id];
          if (deadNew && !deadCur) tile[tk] = { o: o, t: t, tie: false };
          else if (!deadNew && !deadCur) tile[tk].tie = true;
        }
      }
      let upo = null, upT = -1;
      for (const tk in tile) {
        const tv = tile[tk];
        if (DUNG_PLANT_UPROOT[tv.o.id] && tv.t > upT) { upT = tv.t; upo = tv.o; }
      }
      const col = upo ? DUNG_PLANT_UPROOT[upo.id] : '';
      const rgb = DUNG_PLANT_RGB[col] || 0x5fd07a;
      const plantCol = o2 => DUNG_PLANT_UPROOT[o2.id] || DUNG_PLANT_CHOP[o2.id] || DUNG_PLANT_DEAD[o2.id];

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

      let route = null;
      if (grid && dungSelfPos) {
        const sx = dungSelfPos.x - gOx, sy = dungSelfPos.y - gOy;
        if (sx >= 0 && sx < 16 && sy >= 0 && sy < 16 && grid[sy][sx] !== '#') {
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
        for (let i = 0; i < route.length && marks.length < 16; i++) {
          const tv = route[i], c2 = plantCol(tv.o);
          const ready = !tv.tie && c2 === col && DUNG_PLANT_CHOP[tv.o.id];
          marks.push({ x: tv.o.x, y: tv.o.y, plane: tv.o.plane || 0,
                       rgb: ready ? rgb : 0x8a93a6,
                       label: '-' + (i + 1) + '. ' + (ready ? 'Chop' : 'Wait (now ' + c2 + ')') });
        }
      } else {
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
  // Riddle-giver: NPC 11011 "Enigmatic hoardstalker", action "Get-Riddle". The named
  //   11146/47 Cub   11148/49 Little  11150/51 Naive  11152/53 Keen   11154/55 Brave
  //   11156/57 Brah  11158/59 Naabe   11160/61 Wise   11162/63 Adept  11164/65 Sachem
  const DUNG_HOARD_NPCS = { 11011: 1 };
  const dungIsRiddler = n => (n.actions || []).some(a => a === 'Get-Riddle')
                          || DUNG_HOARD_NPCS[n.id];
  const DUNG_HOARD_IFACE = [1184, 1186, 1191];   // npc / server-message / player dialogue
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
  // (js5-16 names): Chest 49594/49595/49596, Food barrel 49597/49598/49599. The single
  const DUNG_HOARD_LOC_OBJ = { 2: { 49594: 1, 49595: 1, 49596: 1 },
                               4: { 49597: 1, 49598: 1, 49599: 1 } };
  // Group 1188 = "Dialogue option select", for BOTH the chest list and the paged barrel
  const DUNG_HOARD_GROUP = 1188;
  const DUNG_HOARD_COMPS = Array.from({ length: 64 }, (_, i) => i).join(',');
  {
    const inR = o => typeof o.x === 'number' && here(o);
    const hoard = npcs.find(n => dungIsRiddler(n) && inR(n));
    if (!hoard) { dungHoardRiddle = null; dungHerbClear('riddle'); }   // left the room: drop a stale answer + this panel's box
    else {
      {
        const t = dungDlgText();
        const hit = t && DUNG_HOARD_RIDDLES.find(r => t.indexOf(r.k) >= 0);
        if (hit) dungHoardRiddle = hit;
      }
      const r = dungHoardRiddle;
      const cid = (r && DUNG_HOARD_LOC_OBJ[r.loc]) || null;   // id-set: any theme variant
      const itemDisp = r ? r.item + ' (o)' : '';
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
  const armless = npcs.find(n => n.id === 11002 && typeof n.x === 'number' && here(n));
  if (armless) { dungGuideTiles(marks); dungHighlightNpc(11002, 'Attach an arm'); return; }
  const headless = npcs.find(n => n.id === 11003 && typeof n.x === 'number' && here(n));
  if (headless) { dungGuideTiles(marks); dungHighlightNpc(11003, 'Attach a head'); return; }
  // Monolith room, matched by NAME so every theme's id family works (seen: 10975
  // inactive + 10978 active on frozen, 10979 active w/ Count-charges on abandoned;
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
const DUNG_PARTY_COLS = ['#39d3d3', '#f06bd8', '#e8ecff', '#8c5aff', '#6bf06b'];
const dungMateCol = pn => DUNG_PARTY_COLS[(pn - 1) % DUNG_PARTY_COLS.length] || DUNG_PARTY_COLS[0];
// (north-camera -> north arrow; NE -> NE). yaw 5115 runs the SAME rotational sense
const DUNG_YAW_SIGN = 1, DUNG_YAW_OFFSET = 180;

function dungRoomOf(sw, wx, wy) {
  const dx = wx - sw.x, dy = wy - sw.y;
  const rx = Math.floor(dx / DUNG_ROOM_PITCH), ry = Math.floor(dy / DUNG_ROOM_PITCH);
  return { rx: rx, ry: ry,
           lx: Math.min(dx - rx * DUNG_ROOM_PITCH, DUNG_ROOM_W - 1),
           ly: Math.min(dy - ry * DUNG_ROOM_PITCH, DUNG_ROOM_W - 1) };
}
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
//   player arrow 2825-2829 by facing; gatestone objs 17489 / 29468 / 18829.
const DUNG_START_SPR = 2831, DUNG_BOSS_SPR = 2833;
// unexplored room art: "?" tiles (2787-2790 / 2806-2809) + dark key/skill-locked
// (35883-35886). A room whose bg is ONLY these hasn't been entered yet.
function dungCellUnex(c) {
  return !!(c && c.bg && c.bg.length && c.bg.every(sp => DUNG_UNEX_SPR.has(sp)));
}
const DUNG_UNEX_SPR = new Set([2787, 2788, 2789, 2790, 2806, 2807, 2808, 2809, 35883, 35884, 35885, 35886]);
// 17489 personal (teal), 29468 second (red), 18829 group (blue).
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

const DUNG_PARTY_GROUPS = [91, 92];
const DUNG_PARTY_LABELS = { 'Party Leader': 1, 'Party Member': 1, 'Party member': 1 };

function rosterKey(s) { return (s || '').replace(/\s+/g, ' ').replace(/["<>&]/g, '').trim(); }

function dungRosterAdd(n) {
  if (!n) return;
  const strip = s => (s || '').replace(/[^a-z0-9]/gi, '').toLowerCase();
  const id = strip(n);
  if (!id) return;
  for (const e in dungPartyRoster) if (strip(e) === id) return;   // already present in some form
  dungPartyRoster[n] = 1;
}

function dungPumpPartyHiscores(party92) {
  try { if (lastSnap && lastSnap.display_name) dungSelfName = rosterKey(lastSnap.display_name); } catch (e) {}
  if (party92)
    for (const gid of DUNG_PARTY_GROUPS)
      for (const w of dungFetchGroup(gid)) {
        const comp = w.t ? w.t[1] : -1;
        const raw = w.x;
        if (comp >= 19 && comp <= 23 && raw && raw.length >= 1 && raw.length <= 20
            && raw.indexOf(' ') < 0 && !DUNG_PARTY_LABELS[raw])
          dungRosterAdd(rosterKey(raw));
      }
  for (const l in DUNG_PARTY_LABELS) {
    delete dungPartyRoster[l]; delete dungHsPoll[l]; delete dungHsDone[l]; delete dungPartyStats[l];
  }
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

function dungPartyReport(kind, data) {
  try { if (bridge().partyReport && bridge().partyGetCode && rtxData.sync('party.partyGetCode'))
          rtxData.sync('party.partyReport', kind, JSON.stringify(data)); } catch (e) {}
}
function dungPartyMerge() {
  dungSyncCode();   // touch partyGetCode -> loads the persisted code + starts the SSE loop if idle
  let pd = null;
  try { if (bridge().partyData) pd = JSON.parse(rtxData.sync('party.partyData') || '{}'); } catch (e) {}
  if (!pd || !pd.code) return;
  const hs = pd.hiscores || {};
  for (const name in hs) {
    if (dungIsSelf(name)) continue;                 // the local live stats win
    if (Array.isArray(hs[name])) {
      dungPartyStats[name] = hs[name];
      dungHsDone[name] = 1; delete dungHsPoll[name];   // don't fetch what a teammate already shared
    }
  }
  if (Date.now() < dungPartyHoldUntil) return;
  if (!pd.synced) return;
  const doors = pd.doors || {};
  for (const cell in doors) {
    const d = doors[cell];
    if (d && d.skill && d.level && !dungDoorLevels[cell])
      dungDoorLevels[cell] = { skill: d.skill, level: d.level | 0, shared: true };
  }
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
  const ck = pd.critkeys || {};
  for (const i in ck) {
    const ki = +i;
    if (!(ki >= 1 && ki <= 64)) continue;
    if (dungCritKeyTouch[ki] && now - dungCritKeyTouch[ki] < 3000) continue;
    if (ck[i]) { if (!dungCritKeys[ki]) dungCritKeys[ki] = DUNG_KEY_PARTY; delete dungCritKeyBlock[ki]; }
    else { delete dungCritKeys[ki]; dungCritKeyBlock[ki] = 1; }
  }
}


function dungStripTitles(root) {
  if (!root) return;
  const els = root.querySelectorAll('[title]');
  for (let i = 0; i < els.length; i++) {
    const el = els[i], t = el.getAttribute('title');
    el.removeAttribute('title');
    if (el.closest && el.closest('.dg-cell')) continue;
    if (t && !el.getAttribute('data-tip')) el.setAttribute('data-tip', t);
  }
}

const DUNG_DIRS = [[1, 0, -1], [2, 1, 0], [4, 0, 1], [8, -1, 0]];   // doorBit, dx, dy (N E S W)
function dungRoomSpace(rooms, floor, key) {
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
const DUNG_CRIT_MIN = 18, DUNG_CRIT_MAX = 23;
function dungCritBudget(rooms, critKeysHeld, foundOverride) {
  let found = 0;
  if (foundOverride != null) found = foundOverride;
  else for (const k in rooms) {
    const r = rooms[k];
    if (r && (r.start || r.boss || (r.onPath && (!r.unex || dungManualCrit[k])))) found++;
  }
  const keyed = Math.max(0, critKeysHeld || 0);
  const min = Math.max(0, DUNG_CRIT_MIN - found);
  const max = Math.max(0, DUNG_CRIT_MAX - found);
  return { found: found, min: min, max: max, keyed: keyed,
           unkMin: Math.max(0, min - keyed),
           unkMax: Math.max(0, max - keyed) };
}
const DUNG_OPPBIT = { 1: 4, 2: 8, 4: 1, 8: 2 };

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

  const soloFloor = Object.keys(dungPartyRoster).length <= 1;
  const srcUsable = sc => sc && typeof sc.x === 'number' && (!sc.assumed || soloFloor);
  const keyCameFrom = {};
  for (const si in dungKeySrc) {
    const sc = dungKeySrc[si];
    if (sc && typeof sc.x === 'number') {
      const cc = ctx.cellOfWorld(sc.x, sc.y);
      if (cc) {
        if (rooms[cc]) { rooms[cc].keyFrom = si; rooms[cc].keyFromAssumed = !!sc.assumed; }
        if (!sc.assumed || soloFloor) keyCameFrom[cc] = si;   // solo: the flip IS proof
      }
    }
  }
  let bossCell = null, startCell = null;
  for (const kk in rooms) {
    if (rooms[kk].boss) bossCell = kk;
    if (rooms[kk].start) startCell = kk;
  }
  const staleInfLatch = kk => {
    const w = dungCritLatch[kk] ? String(dungCritLatch[kk]) : '';
    return !!w && (w[0] === '~' || /^the level-\d+ /.test(w));
  };
  const factLatch = kk => !!dungCritLatch[kk] && !staleInfLatch(kk);

  const sTop = dungBfs(rooms, (startKey && rooms[startKey]) ? startKey : from, null);
  let routeSet = null;
  const sFull = startCell ? dungBfs(rooms, startCell, null) : null;
  if (bossCell && sFull && sFull.dist[bossCell] !== undefined) {   // boss actually reachable
    routeSet = {};
    for (let c = bossCell; c != null; c = sFull.par[c]) { routeSet[c] = 1; if (c === startCell) break; }
  }
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
  const isCritKey = i => !keyBehindFiller[i] && dungKeyIsCrit(i);
  const deadBranch = {};
  for (let dp = 0; dp < 64; dp++) {
    let changed = false;
    for (const kk in rooms) {
      const c = rooms[kk];
      if (!c || deadBranch[kk] || c.boss || c.start || dungManualCrit[kk] || factLatch(kk)) continue;
      if (c.unex || !c.doors) continue;               // no verdict without data
      if (c.key || (c.groundKeys && c.groundKeys.length) || c.keyFrom || keyCameFrom[kk]) continue;
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
  for (const kk in deadBranch) {
    const c = rooms[kk];
    if (c && (dungDoorBand(dungDoorLevels[kk]) === 'critical'
              || (c.res && c.res.band === 'critical'))) c.refuted = true;
  }
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

  const bossKey = bossCell;   // the boss, once mapped, is the only objective
  const fronts = [];
  for (const kk in rooms) {
    if (rooms[kk].boss) continue;   // the destination, never a frontier
    if ((rooms[kk].unex || rooms[kk].key) && !dungManualNonCrit[kk]) fronts.push(kk);
  }
  const cl = dungReachClosure(rooms, from, led, held);
  const cands = bossKey ? [bossKey] : fronts;
  const live = cands.filter(t => cl.reach.dist[t] !== undefined);
  const adjToPlayer = new Set(rooms[from] ? dungNbrs(rooms, from) : []);
  const planDiag = { rooms: Object.keys(rooms).length, fronts: fronts.length, live: live.length,
               from: from || '-' };
  const area = fronts.length ? dungFrontierArea(rooms, fronts, floor) : {};

  const evid = {}, evStr = {}, evFact = {};
  const srcCells = {};
  for (const si in dungKeySrc) {
    if (!isCritKey(si) || !srcUsable(dungKeySrc[si])) continue;
    const cell = ctx.cellOfWorld(dungKeySrc[si].x, dungKeySrc[si].y);
    if (rooms[cell]) srcCells[cell] = dungKeyName(+si) || 'key';
  }
  const critDoorCell = {};
  for (const i in dungKeyDoor) {
    if (isCritKey(i) && rooms[dungKeyDoor[i]]) critDoorCell[dungKeyDoor[i]] = dungKeyName(+i) || 'key';
  }
  for (const kk in rooms) {
    const c = rooms[kk];
    if (dungManualNonCrit[kk]) continue;
    let why = null, str = 2, fact = true;
    if (dungManualCrit[kk]) why = 'a room you marked critical';
    else if (c.boss) why = 'the boss room';
    else if ((c.groundKeys || []).some(gk => isCritKey(gk.idx))) why = 'a critical key lying here';
    else if (srcCells[kk]) why = 'the room that held the critical ' + srcCells[kk];
    else if (c.key && isCritKey(c.key.idx)) {
      why = 'the door needing the critical ' + c.key.name;
    }
    else if (critDoorCell[kk]) {
      why = 'the door that needed the critical ' + critDoorCell[kk];
    }
    else if (!deadBranch[kk]) {
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

  const must = {};           // cell -> 1: cuts the start from every objective / a proof
  const mustWhy = {};        // dominator rooms' tooltip why (written in RENDER)
  const mandatoryKey = {};   // key idx -> 1: nothing is reachable without it
  const mandatorySrc = {};   // cell -> 1: yielded such a key, so the trip there was forced
  const forcedKey = {};      // key idx -> 'blocks' | 'route': proven forced, either way
  const reachWithout = excl => {
    const keys = new Set();
    held.forEach(i => { if (i !== excl) keys.add(i); });
    const relock = dungKeyDoor[excl] || null;
    const gate = kk => {
      if (relock && kk === relock && !keys.has(excl)) return false;
      const c = rooms[kk];
      return !c.key || keys.has(c.key.idx);
    };
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
  const confirmed = [];
  for (const kk in evStr) if (evStr[kk] >= 2 && rooms[kk] && cl.reach.dist[kk] !== undefined) confirmed.push(kk);
  for (const kk in dungCritLatch) if (rooms[kk] && confirmed.indexOf(kk) < 0) confirmed.push(kk);
  if (rooms[(startKey && rooms[startKey]) ? startKey : from]) {
    const confSet = new Set(confirmed);
    for (let guard = 0; guard < 8; guard++) {
      let grew = false;
      for (const si in dungKeySrc) {
        if (forcedKey[si]) continue;
        const sc = dungKeySrc[si];
        const srcCc = srcUsable(sc) ? ctx.cellOfWorld(sc.x, sc.y) : null;
        const dk = dungKeyDoor[si];
        if (dk && ((routeSet && routeSet[dk]) || confSet.has(dk))) forcedKey[si] = 'route';
        else {
          if (!live.length && !confSet.size) continue;
          const r = reachWithout(+si);
          const blocksAll = live.length && !live.some(t => r.dist[t] !== undefined);
          let blocksCrit = false;
          confSet.forEach(kk => { if (kk !== srcCc && r.dist[kk] === undefined) blocksCrit = true; });
          if (!(blocksAll || blocksCrit)) continue;
          forcedKey[si] = 'blocks';
          mandatoryKey[+si] = 1;   // only 'blocks' keys: the tooltip says "nothing is
        }
        grew = true;
        if (srcCc && rooms[srcCc]) { mandatorySrc[srcCc] = 1; confSet.add(srcCc); }
      }
      if (!grew) break;
    }
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
  if (startKey && rooms[startKey]) {
    const reachSkip = skip => dungBfs(rooms, startKey, kk => kk !== skip).dist;
    const base = reachSkip(null);
    const liveB = live.filter(t => base[t] !== undefined);
    const provenB = [];
    for (const kk in rooms) {
      if (base[kk] === undefined || rooms[kk].start) continue;
      if ((evStr[kk] || 0) >= 2 || dungCritLatch[kk] || mandatorySrc[kk] || dungManualCrit[kk] || rooms[kk].boss)
        provenB.push(kk);
    }
    for (const si in dungKeySrc) {
      const sc2 = dungKeySrc[si];
      if (!srcUsable(sc2)) continue;
      const cc2 = ctx.cellOfWorld(sc2.x, sc2.y);
      if (cc2 && rooms[cc2] && base[cc2] !== undefined && !rooms[cc2].start && provenB.indexOf(cc2) < 0)
        provenB.push(cc2);
    }
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

  let objective = null, objWhy = '', tied = {};
  const routeOf = {}, shared = {};
  for (const t of live) {
    const rs = dungPath(sTop.par, t);
    routeOf[t] = rs;
    for (const pk of rs) shared[pk] = (shared[pk] || 0) + 1;
  }
  const evOf = t => {
    let s = 0, n = 0, why = '', at = '';
    for (const pk of (routeOf[t] || [])) {
      if (!evid[pk] || shared[pk] !== 1) continue;
      if (rooms[pk] && !rooms[pk].unex) continue;
      n++;
      if (evStr[pk] > s) { s = evStr[pk]; why = evid[pk]; at = pk; }
    }
    return { s: s, n: n, why: why, at: at };
  };
  let critHeld = 0;
  held.forEach(idx => { if (isCritKey(idx)) critHeld++; });
  const idSet = {};
  const ided = kk => { const r = rooms[kk]; if (r && (!r.unex || dungManualCrit[kk])) idSet[kk] = 1; };
  if (startKey && rooms[startKey]) idSet[startKey] = 1;
  for (const kk in evStr) if (evStr[kk] >= 2) ided(kk);
  for (const kk in must) ided(kk);
  for (const kk in dungCritLatch) ided(kk);
  for (const kk in dungManualCrit) ided(kk);
  for (const si in dungKeySrc) {
    const sc3 = dungKeySrc[si];
    if (srcUsable(sc3)) { const cc3 = ctx.cellOfWorld(sc3.x, sc3.y); if (cc3) ided(cc3); }
  }
  if (routeSet) for (const kk in routeSet) ided(kk);
  const budget = dungCritBudget(rooms, critHeld, Object.keys(idSet).length);
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
    const sp = dungRoomSpace(rooms, floor, t);
    const leadsOn = (sp > 0 || ar > 0) ? 1 : 0;
    const continues = adjToPlayer.has(t) ? 1 : 0;
    const pot = area[t + '#pot'] || 0;   // the ray bounding box: width x height
    const need = budget.unkMin || 0;
    const allN = area['#allcells'] || 0;
    const expCrit = (need > 0 && allN > 0) ? Math.min(pot, need, need * pot / allN) : 0;
    const ownBand = dungDoorBand(dungDoorLevels[t]);
    const own = rooms[t];
    const openableCrit = !!(own.key && held.has(own.key.idx) && isCritKey(own.key.idx));
    const ownKey = own.key ? own.key.idx : null;
    const ownKeyFloor = (ownKey != null && led[ownKey]) ? led[ownKey].floor : null;
    const keyInPack = ownKey != null && held.has(ownKey);
    const keyOnFloor = !keyInPack && !!ownKeyFloor && cl.reach.dist[ownKeyFloor] !== undefined;
    const openableAny = keyInPack || keyOnFloor;
    const grade = (ownBand === 'low' || ownBand === 'above' || own.lowVia) ? -1
                : openableCrit ? 3 : Math.max(e.s, openableAny ? 1 : 0);
    const spB = Math.floor(sp / 3);
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
    if (allEqual) { tied[t] = 1; wins = (objective === null || t < objective); }
    else if (wins) tied = { [t]: 1 };   // a strict win discards the previous tie set
    if (wins) {
      bestEv = grade; bestLeads = leadsOn; bestChain = chainB; bestCont = continues; bestDepth = depth; bestPot = pot; bestSpB = spB; bestCount = e.n; bestSpace = sp; bestOpen = openableCrit || openableAny; objective = t;
      objWhy = whyStr;
    }
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

  if (!bossKey && stickyObj && objective && objective !== stickyObj) {
    if (!(bestEv === 3 || bestOpen)) {
      objective = stickyObj;
      tied = { [stickyObj]: 1 };
      objWhy = 'staying on the path being explored (no key or dead end yet; a rival branch '
             + 'ranked higher on soft terms, but only an openable door or the branch ending '
             + 'moves the plan off a branch mid-push) | ' + stickyWhy;
    }
  }
  if (objective) dungStickyObj = objective;

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
      const fc = led[blk] && led[blk].floor;
      if (!fc || fc === target) { action = { kind: 'needkey', cell: (led[blk] && led[blk].door) || target, key: blk }; target = null; break; }
      fetch = blk; target = fc;
    }
    if (target) action = fetch != null ? { kind: 'key', cell: target, key: fetch }
                                       : { kind: bossKey ? 'boss' : 'explore', cell: target };
  } else if (cands.length) {
    const openAll = dungBfs(rooms, from, null);
    const reach = cands.filter(t => openAll.dist[t] !== undefined).sort((a, b) => openAll.dist[a] - openAll.dist[b]);
    if (reach.length) {
      for (const pk of dungPath(openAll.par, reach[0])) {
        onPath[pk] = 1;
        const c = rooms[pk];
        if (c.key && !held.has(c.key.idx)) {
          action = { kind: 'needkey', cell: pk, key: c.key.idx };
          break;
        }
      }
    }
  }

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
  for (const kk in mustWhy)
    if (rooms[kk] && !rooms[kk].critSelfWhy && !rooms[kk].critPathWhy)
      rooms[kk].critPathWhy = mustWhy[kk];
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
  for (const kk in keyCameFrom) {
    if (!rooms[kk] || dungManualNonCrit[kk]) continue;
    onPath[kk] = 1;
    if (!rooms[kk].critSelfWhy && !evid[kk])
      rooms[kk].critSelfWhy = 'it held the ' + (dungKeyName(+keyCameFrom[kk]) || 'key')
                            + ' (the trip here is proven by the pickup)';
  }
  for (const kk in rooms) {
    const c = rooms[kk];
    if (c && c.key && dungKeyDoor[c.key.idx] !== kk) { dungKeyDoor[c.key.idx] = kk; dungSaveMarks(); }
  }
  const fetchChain = {};
  const sOpen = startCell ? dungBfs(rooms, startCell, kk => !rooms[kk].key) : null;
  for (const si in dungKeySrc) {
    if (!forcedKey[si]) continue;
    const sc = dungKeySrc[si];
    if (!srcUsable(sc)) continue;
    const src = ctx.cellOfWorld(sc.x, sc.y);
    if (!src || !startCell || !rooms[src]) continue;
    const nm = dungKeyName(+si) || 'key';
    const par = (sOpen && sOpen.dist[src] !== undefined) ? sOpen.par : sFull.par;
    for (const c of dungPath(par, src)) {
      fetchChain[c] = 1;
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
    if (!fetchChain[kk]) {
      if (deadBranch[kk]) continue;
      if (rooms[kk].lowVia && !dungManualCrit[kk]) continue;
      if (routeSet && !routeSet[kk] && !keyCameFrom[kk] && !dungManualCrit[kk]
          && evStr[kk] !== 2 && !must[kk]
          && !rooms[kk].boss && !rooms[kk].start) continue;  // settled: not on the real route
    }
    rooms[kk].onPath = true;
  }
  {
    let latchChanged = false;
    for (const kk in rooms) {
      const r = rooms[kk];
      const cur = dungCritLatch[kk] ? String(dungCritLatch[kk]) : '';
      if (!r || r.unex || dungManualNonCrit[kk] || (cur && cur[0] !== '~')) continue;
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
    if (!inDung && !party92) dungPartyRoster = {};
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
      let vc = {};
      try { vc = JSON.parse(await rtxData.raw('state.varcsAll') || '{}'); } catch (e) {}
      const tmr = vc['5:4190'];
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
      const rosterKey = s => (s || '').replace(/\s+/g, ' ').replace(/["<>&]/g, '').trim();
      try { const se = JSON.parse(await bridge().sceneEntities(myPid(), 128) || '{}'); sceneNpcs = se.npcs || []; sceneObjs = se.objects || [];
            const sp = (se.players || []).find(p => p && p.self);
            if (sp && typeof sp.x === 'number') { dungSelf = { x: sp.x, y: sp.y }; if (sp.name) { dungSelfName = rosterKey(sp.name); } }
            dungMates = (se.players || []).filter(p => p && !p.self && typeof p.x === 'number'); } catch (e) {}
      if (dungSelfName) dungRosterAdd(dungSelfName);
      for (const m of dungMates) dungRosterAdd(rosterKey(m.name || ('#' + m.uid)));
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
      for (const iconComp in skipMeta) {
        const flag = +iconComp + 1;               // comp 2 pairs with 1, comp 4 with 3
        if (skipUsed[flag] !== undefined)
          d.skips.push({ name: skipMeta[iconComp].name, item: skipMeta[iconComp].item, used: skipUsed[flag] });
      }
      if (d.progW <= 15) d.prog = '';
      d.keys.sort((a, b) => a.idx - b.idx);

      // renderer script5999: room TILES (>=30px graphics, incl. the 2831 start /
      // (script8152 = key item -> held varc, the game's own outline test).
      const held = new Set(d.keys.map(k => k.idx));
      const rooms = {}; let player = null;
      const cellOf = (cx, cy) => rooms[cx + ',' + cy] ||
        (rooms[cx + ',' + cy] = { bg: [], key: null, door: null, gate: 0, boss: false });
      for (const w of dungFetchGroup(942)) {
        if (w.t && w.t[1] === 3 && w.r && w.r[2] >= 100) {
          const fc = Math.round((w.r[2] - 24) / 32), fr = Math.round((w.r[3] - 24) / 32);
          if (fc >= 2 && fc <= 8 && fr >= 2 && fr <= 8) d.floor = { cols: fc, rows: fr };
        }
        if (!w.t || w.t[1] !== 8 || w.t[2] < 0 || !w.r) continue;
        const x = w.r[0], y = w.r[1], ww = w.r[2], hh = w.r[3];
        if (x < 0 || y < 0 || x > 240 || y > 240) continue;      // pooled/off-map widget
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

      if (d.map) {
        try {
          let startCell = null;
          for (const kk in rooms) if (rooms[kk].start) { const p = kk.split(',').map(Number); startCell = { cx: p[0], cy: p[1] }; break; }

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
          for (const kk in dungKeyCache) {
            const c = dungKeyCache[kk];
            if (held.has(c.ki.idx)) { dungKeySrc[c.ki.idx] = { x: c.x, y: c.y }; delete dungKeyCache[kk]; dungSaveMarks(); continue; }
            if (dungSelf && !present[kk] && Math.max(Math.abs(c.x - dungSelf.x), Math.abs(c.y - dungSelf.y)) <= 12)
              delete dungKeyCache[kk];
          }
          if (!dungHeldInit) { held.forEach(i => { dungHeldSeen[i] = 1; }); dungHeldInit = true; }
          else for (const i of held) {
            if (dungKeySrc[i] || dungHeldSeen[i]) continue;
            dungHeldSeen[i] = 1;
            if (dungSelf) { dungKeySrc[i] = { x: dungSelf.x, y: dungSelf.y, assumed: 1 }; dungSaveMarks(); }
          }
          held.forEach(i => { dungHeldSeen[i] = 1; });
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
            d.map.player = null;
          }
          d.mates = [];
          if (anchor) for (const m of dungMates) {
            const r2 = dungRoomOf(sw, m.x, m.y);
            const tc2 = anchor.cx + DUNG_MAP_XSIGN * r2.rx, tr2 = anchor.cy + DUNG_MAP_YSIGN * r2.ry;
            if (rooms[tc2 + ',' + tr2]) {
              const key = rosterKey(m.name || ('#' + m.uid));
              d.mates.push({ name: key, pn: Object.keys(dungPartyRoster).sort().indexOf(key) + 1, cx: tc2, cy: tr2, lx: r2.lx, ly: r2.ly });
            }
          }
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
          if (anchor) for (const e of sceneNpcs.concat(sceneObjs)) {
            if (typeof e.x !== 'number') continue;
            const res = dungResource(e.id);
            if (!res || res.skill === 'Divination') continue;
            const rx = Math.round((e.x - sw.x - 6.5) / DUNG_ROOM_PITCH), ry = Math.round((e.y - sw.y - 6.5) / DUNG_ROOM_PITCH);
            const ck = (anchor.cx + DUNG_MAP_XSIGN * rx) + ',' + (anchor.cy + DUNG_MAP_YSIGN * ry);
            if (rooms[ck] && !dungCellUnex(rooms[ck])) {
              const sig = res.skillIdx + '|' + res.tier + '|' + res.level;
              const st = (dungRoomRes[ck] = dungRoomRes[ck] || {});
              if (!st[sig]) {
                st[sig] = { skill: res.skill, skillIdx: res.skillIdx, tier: res.tier, level: res.level };
                dungSaveMarks();
              }
            }
          }
          for (const ck in dungRoomRes) {
            const cell = rooms[ck];
            if (!cell || dungCellUnex(cell)) continue;   // see the invariant above
            for (const sig in dungRoomRes[ck]) {
              const r0 = dungRoomRes[ck][sig];
              const mt = dungMaxTier(r0.skillIdx, r0.skill);
              const nr = { skill: r0.skill, tier: r0.tier, level: r0.level,
                           mine: dungSkillLevel(r0.skillIdx), best: mt.best, by: mt.by,
                           maxTier: mt.maxTier, band: dungResBand(r0, mt.maxTier, mt.best) };
              if (!cell.res || nr.tier < cell.res.tier
                  || (nr.tier === cell.res.tier && nr.level < cell.res.level)) cell.res = nr;
              if (nr.tier > 0 && nr.tier < DUNG_RES_SCALE.length - 1) {
                if (!cell.resLow || nr.tier < cell.resLow.tier) cell.resLow = nr;
              }
            }
          }
          {
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
          }
          // Skill-door LEVEL capture: the entity tooltip (group 1177, its one TEXT
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
          }
        } catch (e) {}
      }

    }
    if (inDung) dungSaveMarks();   // marks survive a panel rebuild (restored above)
    dungData = d;
  } finally { dungFetching = false; }
  paneRun('dung', renderDungeoneering);
}

function dungPartyBestHtml(party92, alwaysOpen) {
  try { if (lastSnap && lastSnap.display_name) dungSelfName = rosterKey(lastSnap.display_name); } catch (e) {}
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
          return '<span class="dg-pmem dg-pm-' + s.cls + '" title="' + dgEsc(n) + ' - ' + s.txt + '">'
            + '<i class="dg-pmdot"></i>'
            + '<b style="color:' + (dungIsSelf(n) ? '#f25c5c' : dungMateCol(i + 1)) + '">p' + (i + 1) + '</b>' + dgEsc(n) + '</span>';
        }).join('')
      + '</div>';
  }
  if (!party92)
    html += '<div class="dg-none">Open the game\'s party interface so every mate\'s name (and stats) can be fetched.</div>';
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
    wrap.addEventListener('click', ev => {
      const t = ev.target;
      if (t && t.closest && t.closest('.dg-pbhd')) {
        dungPbOpen = !dungPbOpen; dungSig = ''; renderDungeoneering(); return;
      }
      if (t && t.closest && t.closest('.dg-phd')) {
        dungPartyOpen = !dungPartyOpen; dungSig = ''; renderDungeoneering(); return;
      }
      const sb = t && t.closest ? t.closest('[data-sync]') : null;
      if (sb) { dungSyncAction(sb.getAttribute('data-sync'), wrap); return; }
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
      const cell = t && t.closest ? t.closest('.dg-cell[data-cell]') : null;
      if (!cell) return;
      dungSetManualCrit(cell.getAttribute('data-cell'), true);
    });
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
    wrap.addEventListener('input', ev => {
      if (ev.target && ev.target.classList && ev.target.classList.contains('dg-sync-in')) dungSyncInput = ev.target.value;
    });
    wrap.addEventListener('keydown', ev => {
      if (ev.key === 'Enter' && ev.target && ev.target.classList && ev.target.classList.contains('dg-sync-in'))
        dungSyncAction('join', wrap);
    });
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
  const yawDeg = (d.yaw !== undefined) ? (DUNG_YAW_SIGN * d.yaw / 16284 * 360 + DUNG_YAW_OFFSET) : 0;
  const sig = JSON.stringify([d.keys.map(k => k.idx + (dungKeyIsCrit(k.idx) ? 'c' : '')), d.deaths,
    d.skips.map(s => s.name + (s.used ? '1' : '0')), d.prog, d.mapOpen, d.party92,
    d.floor ? d.floor.cols + 'x' + d.floor.rows : 0, dungRouteTarget,
    (d.mates || []).map(m => m.name + '@' + m.cx + ',' + m.cy).join(';'), Object.keys(dungPartyRoster).sort().join('|'),
    SKILL_NAMES.map((_, i) => { const b = dungPartyBest(i); return (b.best || 0) + (b.by || ''); }).join('.'),
    Object.keys(dungHsPoll).length, Object.keys(dungHsDone).length, dungPbOpen, dungPartyOpen, dungSyncCode(), Object.keys(dungManualNonCrit).sort().join(','), Object.keys(dungManualCrit).sort().join(','), Object.keys(dungPartyStats).sort().join('|'),
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

  const critHeld = d.keys.filter(k => dungKeyIsCrit(k.idx));
  const normHeld = d.keys.filter(k => !dungKeyIsCrit(k.idx));
  const keyChip = (k, crit) => {
    const why = dungKeyWhy(k.idx);
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

  const dgArrowSvg = (deg, cls) => '<svg class="dg-shape ' + cls + '" width="18" height="18" viewBox="0 0 12 12" style="transform:rotate(' + deg.toFixed(1) + 'deg)">'
    + '<path d="M6 1 L10.5 10.5 L6 8 L1.5 10.5 Z" fill="#f25c5c" stroke="rgba(0,0,0,.6)" stroke-width="0.9"/></svg>';
  const dgArrow = dgArrowSvg(yawDeg, 'dg-yawarrow');
  if (d.map) {
    const cells = Object.keys(d.map.rooms).map(k => k.split(',').map(Number));
    const xs = cells.map(p => p[0]), ys = cells.map(p => p[1]);
    let x0 = Math.min.apply(null, xs), x1 = Math.max.apply(null, xs);
    let y0 = Math.min.apply(null, ys), y1 = Math.max.apply(null, ys);
    if (d.floor) { x0 = 0; y0 = 0; x1 = Math.max(x1, d.floor.cols - 1); y1 = Math.max(y1, d.floor.rows - 1); }
    const ncols = x1 - x0 + 1, nrows = y1 - y0 + 1;
    const CELL = 56, GAP = 12, PITCH = CELL + GAP, PAD = 4;
    const bw = PAD * 2 + ncols * PITCH - GAP, bh = PAD * 2 + nrows * PITCH - GAP;
    const left = (col) => PAD + col * PITCH, top = (row) => PAD + row * PITCH;
    const avail = Math.max(160, (wrap.clientWidth || 300) - 4);
    const scale = Math.min(1, avail / bw);
    html += '<div class="dg-sect"><span>Explored map</span><span class="dg-count">'
      + cells.length + (d.floor ? ' / ' + (d.floor.cols * d.floor.rows) + ' rooms (' + d.floor.cols + 'x' + d.floor.rows + ' floor)' : ' rooms') + '</span></div>';
    {
      const ck = (d.keys || []).filter(k => dungKeyIsCrit(k.idx)).length;
      const cb = dungCritBudget(d.map.rooms, ck);
      const span = (a2, b2) => (a2 === b2 ? String(a2) : a2 + '-' + b2);
      html += '<div class="dg-sect dg-sub2"><span>Critical rooms</span><span class="dg-count">'
        + cb.found + (cb.max > 0 ? ' of ' + span(cb.found + cb.min, cb.found + cb.max) : '') + ' found'
        + (cb.max > 0 ? ' &middot; ' + span(cb.min, cb.max) + ' to go' : ' &middot; chain may be complete')
        + (ck > 0 && cb.max > 0
             ? ' <span class="dg-dim">(' + ck + ' behind held key' + (ck === 1 ? '' : 's')
               + ', ' + span(cb.unkMin, cb.unkMax) + ' to find)</span>' : '')
        + '</span></div>';
    }
    html += '<div class="dg-boardscale" style="height:' + Math.ceil(bh * scale) + 'px">';
    html += '<div class="dg-board" style="width:' + bw + 'px;height:' + bh + 'px;transform:scale(' + scale.toFixed(4) + ')">';
    if (d.floor)
      for (let gy = y0; gy <= y1; gy++) for (let gx = x0; gx <= x1; gx++)
        if (!d.map.rooms[gx + ',' + gy])
          html += '<span class="dg-cell dg-unex" style="left:' + left(gx - x0) + 'px;top:' + top(gy - y0) + 'px"></span>';
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
    const openedCritDoor = {};
    for (const i in dungKeyDoor) if (dungKeyIsCrit(i)) openedCritDoor[dungKeyDoor[i]] = +i;
    for (const k in d.map.rooms) {
      const [gx, gy] = k.split(',').map(Number), room = d.map.rooms[k];
      const col = gx - x0, row = gy - y0;
      const here = d.map.player && d.map.player.cx === gx && d.map.player.cy === gy;
      const unex = room.bg.length > 0 && room.bg.every(s => DUNG_UNEX_SPR.has(s));
      const dcnt = room.doors === undefined ? 9 : ((room.doors & 1) + ((room.doors >> 1) & 1) + ((room.doors >> 2) & 1) + ((room.doors >> 3) & 1));
      const dead = !unex && dcnt <= 1 && !(room.groundKeys && room.groundKeys.length) && !room.key && !room.door && !room.gate && !room.boss && !room.start && !here && !room.keyFrom;
      room.dead = dead;
      let tile = room.bg.map(s => '<span class="dg-rspr" data-spr="' + s + '"></span>').join('');
      const gate = room.gate ? DUNG_GATESTONES[room.gate] : null;
      const manualNC = !!dungManualNonCrit[k];   // hand-forced non-critical -> fade, suppress crit
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
          ov += '<span class="dg-mk dg-atpos dg-mate-mk" data-mate="' + dgEsc(m.name) + '" style="' + dungSubStyle(m.lx, m.ly) + '" title="p' + m.pn + ' - ' + dgEsc(m.name) + '">'
              + '<span class="dg-mate" style="background:' + mcol + ';box-shadow:0 0 5px ' + mcol + '"></span>'
              + '<span class="dg-mate-n" style="color:' + mcol + '">p' + m.pn + '</span></span>';
        }
      if (here) ov += '<span class="dg-mk dg-player" style="' + dungSubStyle(d.map.player.lx, d.map.player.ly) + '">' + dgArrow + '</span>';
      room.lowRes = !!(room.res && (room.res.band === 'filler' || room.res.band === 'bonus'))
        && !room.boss && !room.start && !here && !room.critPathWhy;   // never fade the approach path to a critical room
      const dl = dungDoorLevels[k] || null;
      const doorMine = dungDoorMine(dl);
      const doorBand = dungDoorBand(dl);
      room.lowDoor = (doorBand === 'low' || doorBand === 'above')
        && !room.boss && !room.start && !here;
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
      if (room.critSelfWhy && !room.boss && !room.critDoor && !(room.res && room.res.band === 'critical'))
        reqs.push('Critical: ' + room.critSelfWhy);
      else if (room.critPathWhy) reqs.push('On ' + room.critPathWhy + ' -- keys found here are critical');
      if (room.start) reqs.push('Start room -- always on the critical path');
      if (room.boss) reqs.push('Boss room');
      if (room.pot) reqs.push(room.pot + ' unexplored room' + (room.pot === 1 ? '' : 's') + ' this way');
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
      const critDead = dead && !!dungManualCrit[k];
      if (dead) reqs.push(critDead
        ? 'Dead end -- no way onward (kept critical by your own mark)'
        : room.res
        ? 'Dead end -- the ' + room.res.skill + ' node is all there is here'
        : 'Dead end (nothing here)');
      if (room.deadBranch && !dead) reqs.push(room.keyFrom
        ? 'Spur -- ways on from here are finished; the key-ring flip seen here was not '
          + 'trusted (party floor: a teammate may have picked that key up elsewhere)'
        : 'Spur -- every way on from here ends in a dead end with no key, so the path cannot run through it');
      if (room.lowVia && !room.lowRes) reqs.push('Only reachable through a filler room');
      const title = reqs.length ? ' data-tip="' + dungAttr(reqs.join('\n')) + '"' : '';
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
    const gatesOnMap = [];
    for (const k in d.map.rooms) { const g = d.map.rooms[k].gate; if (g && gatesOnMap.indexOf(g) < 0) gatesOnMap.push(g); }
    const anyRoom = f => Object.keys(d.map.rooms).some(k => f(d.map.rooms[k], k));
    html += '<div class="dg-legend">'
      + '<span><span class="dg-doorplate" style="--kc:var(--text-dim)"><span class="dg-keyico" data-item="18208"></span></span>door req</span>'
      + '<span><span class="dg-floorkey" style="animation:none"><span class="dg-keyico" data-item="18208"></span></span>key on floor</span>'
      + '<span><span class="dg-lg-hk"></span>have key</span>'
      + (anyRoom(r => r.boss) ? '<span><span class="dg-lg-ring" style="border-color:var(--err)"></span>boss</span>' : '')
      + (anyRoom(r => r.onPath) ? '<span><span class="dg-lg-crit"></span>critical path</span>' : '')
      + (anyRoom(r => r.rec) ? '<span><span class="dg-lg-rec"></span>do this next</span>' : '')
      + (anyRoom(r => r.need) ? '<span><span class="dg-lg-need"></span>best (need key)</span>' : '')
      + (anyRoom(r => r.dead) ? '<span><span class="dg-lg-dead"></span>dead end</span>' : '')
      + (anyRoom(r => r.lowRes || r.lowVia || r.lowDoor || r.deadBranch) ? '<span><span class="dg-lg-low"></span>low level (off path)</span>' : '')
      + (Object.keys(dungManualNonCrit).length ? '<span><span class="dg-lg-nc"></span>non-critical (you)</span>' : '')
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

  if (dungIceDump) html += '<div class="dg-dbg"><button class="dg-sync-btn" data-icedump="1">Copy ice-room dump</button> ice: '
    + dungIceDump.padsUn.length + ' pad(s) left, plan '
    + (dungIceDump.plan ? dungIceDump.plan.length + ' slide(s)' : 'NONE') + '</div>';
  if (dungMazeDump) html += '<div class="dg-dbg"><button class="dg-sync-btn" data-mazedump="1">Copy maze dump</button> maze: '
    + dungMazeDump.barriers.length + ' barrier(s), route '
    + (dungMazeDump.route ? dungMazeDump.route.length + ' tile(s), cost ' + dungMazeDump.cost : 'NONE') + '</div>';
  if (dungBarrelDump) html += '<div class="dg-dbg"><button class="dg-sync-btn" data-barreldump="1">Copy barrel-room dump</button> barrels: '
    + dungBarrelDump.liveOffsets.length + ' live, fit rot ' + dungBarrelDump.bestR
    + ' (' + dungBarrelDump.rotScores.join('/') + ')'
    + (dungBarrelDump.tied ? ' <b>TIED -- not guiding</b>' : '') + '</div>';

  html += dungPartyBestHtml(d.party92);

  wrap.innerHTML = html;
  wrap.querySelectorAll('.dg-rspr').forEach(el => loadSpriteIcon(el, +el.dataset.spr, 112));
  wrap.querySelectorAll('.dg-rspr-mk').forEach(el => loadSpriteIcon(el, +el.dataset.spr, 40));
  wrap.querySelectorAll('.dg-mini,.dg-keyico').forEach(el => { const u = resolveIcon(+el.dataset.item); if (u) el.style.backgroundImage = "url('" + u + "')"; });
  dungStripTitles(wrap);
}

let dungTickBusy = false;
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
      if (party92) dungPumpPartyHiscores(true);
      else dungPartyRoster = {};
      dungClearOverlays(); return;
    }
    let npcs = [], objs = [];
    try { const se = JSON.parse(await bridge().sceneEntities(myPid(), 128) || '{}'); npcs = se.npcs || []; objs = se.objects || [];
          const sp = (se.players || []).find(p => p && p.self); if (sp && typeof sp.x === 'number') dungSelfPos = { x: sp.x, y: sp.y }; } catch (e) {}
    try {
      const gi = JSON.parse(await rtxData.raw('state.groundItems') || '[]');
      if (Array.isArray(gi)) dungGroundCache = gi;   // hoardstalker ground piles
      for (const g of (Array.isArray(gi) ? gi : [])) {
        const ki = dungKeyInfo(g.id);
        if (ki && typeof g.x === 'number') dungKeyCache[g.x + ',' + g.y] = { x: g.x, y: g.y, ki: ki };
      }
    } catch (e) {}
    dungReadDoorTip();   // examine sentences are transient AND the dung tab is usually
    const needMazeVar = DUNG_MAZE_TIMER_VAR && objs.some(o => o.id === 49345 || o.name === 'Locked chest');
    if (npcs.some(n => n.name === 'Monolith' || (n.id >= 10966 && n.id <= 10971)) || needMazeVar) {   // monolith/emote (all statue themes)/maze room -> keep the progress varc fresh off-tab
      try { const vc = JSON.parse(await rtxData.raw('state.varcsAll') || '{}');
            dungMonoCharge = (typeof vc['5:1233'] === 'number') ? vc['5:1233'] : null;
            dungMazeTimer = (DUNG_MAZE_TIMER_VAR && typeof vc['5:' + DUNG_MAZE_TIMER_VAR] === 'number') ? vc['5:' + DUNG_MAZE_TIMER_VAR] : null; } catch (e) {}
    }
    dungReconcileScene(npcs, objs);
  } finally { dungTickBusy = false; }
}

Object.assign(window, { dungLodeTimerTick, dungSceneTick, fetchDungeoneering, renderDungeoneering });
})();
