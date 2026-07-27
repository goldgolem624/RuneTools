// RuneToolsX panel: Big Game Hunter (Anachronia); the Havenhythe variant is panel_havenbgh.js.
// Spliced inline into client.html; bare classic script sharing one global scope.
// Reads player varps/varbits only (external, no companion).

  let bghData = null;
  let bghFetching = false, bghStructSig = '', bghVolSig = '', bghFetchKey = '';
  // Best-trap discovery. Trap damage is server-side (boss HP varbit 53292 is display-only), so
  // it is MEASURED: with the fixed 2x red + 1x blue setup, one full firing's HP drop tells which
  // colour is best (best trap 30000, wrong 7500-15000): >60000 = the 2x colour, 45000-60000 =
  // the 1x colour, <45000 = the unused colour. Keyed by the pressure-plate tile.
  const BGH_BEST_DMG = 30000;              // a best-colour trap's damage
  let bghBestColors = {};
  try { bghBestColors = JSON.parse(localStorage.getItem('rtxBghBest') || '{}') || {}; } catch (e) {}
  let bghLastCreature = null;              // last creature hunted {struct,name,baitId,baitName}: lets the bait pad work inside the
// instance, where coords differ from the overworld area
  try { bghLastCreature = JSON.parse(localStorage.getItem('rtxBghLastCreature') || 'null'); } catch (e) {}
  let bghPlate = null;
  let bghSceneObjs = [];
  let bghSceneNpcs = [];
  let bghCoordKey = null;                  // signature of the current trap layout ("plane:x:y")
  // HP-drop cycle detector: a firing shows up as HP falling over 1-2 polls then going stable.
  let bghPrevHp = null, bghCycleAccum = 0, bghCycleActive = false, bghStableTicks = 0;
  let bghLastFullComp = null;
  let bghGuideOn = false;
  try { bghGuideOn = localStorage.getItem('rtxBghGuide') === '1'; } catch (e) {}
  let bghGuideMsg = '';
  let bghGuidePhase = '';
  let bghFletchWarnQty = -1;

  // Live state from the shared boss/creature HUD + trap loc-object morph varps. Creature
  // identity = boss-bar struct id in varp 10946 (only the BGH dinosaur structs below count as
  // "hunting"); health = varbits 53292/53294; aggression = varps 10947/10948. Trap state is
  // bit-sliced out of varps 8603 (Scorpion sites A/B) and 8604 (Scorpion site C + plate).
  // Struct id -> name + optimal bait [itemId, label] (BGH creature config dbtable,
  // rows 1756-1764 col 9).
  const BGH_CREATURES = {
    9689: { name: 'Arcane apoterrasaur',  bait: [383,   'Raw shark'] },
    9690: { name: 'Scimitops',            bait: [42249, 'Raw sailfish'] },
    9691: { name: 'Bagrada rex',          bait: [389,   'Raw manta ray'] },
    9692: { name: 'Spicati apoterrasaur', bait: [47956, 'Raw bagrada rex meat'] },
    9710: { name: 'Asciatops',            bait: [47948, 'Raw arcane apoterrasaur meat'] },
    9716: { name: 'Corbicula rex',        bait: [47952, 'Raw scimitops meat'] },
    9840: { name: 'Oculi apoterrasaur',   bait: [47964, 'Raw asciatops meat'] },
    9841: { name: 'Malletops',            bait: [47968, 'Raw corbicula rex meat'] },
    9843: { name: 'Pavosaurus rex',       bait: [47960, 'Raw spicati apoterrasaur meat'] },
  };
  const BGH_SCORPION_STATES = ['Empty', 'Building', 'Built (arm it)', 'Armed'];   // varbit value 0..3
  // Per-site armed-scorpion item id (type-33 varps 8628/8629/8630 = sites A/B/C) -> which poison
  // armed it: 47988<-47945 golden, 47989<-47946 phantasmal, 47990<-47947 sky-blue; 47987 = built.
  const BGH_SCORPION_ARMED = {
    47988: { label: 'Armed (golden)',     color: '#e8b923' },   // gold
    47989: { label: 'Armed (phantasmal)', color: '#ff5555' },   // red
    47990: { label: 'Armed (sky-blue)',   color: '#5bc8ff' },   // sky-blue
  };
  // Armed-scorpion item id -> short colour key; colour key -> display + swatch + which spear arms it.
  const BGH_ARMED_COLOR = { 47988: 'gold', 47989: 'red', 47990: 'blue' };
  const BGH_COLOR_INFO = {
    red:  { label: 'Red (phantasmal)', hex: '#ff5555', spear: 47946 },
    blue: { label: 'Sky-blue',         hex: '#5bc8ff', spear: 47947 },
    gold: { label: 'Golden',           hex: '#e8b923', spear: 47945 },
  };
  // Per-colour "frog removed" varbit: 1 = eliminated (repellent / T3 lodge) this encounter,
  // 0 = active candidate. 44325 = gold, 44326 = red (phantasmal), 44327 = blue.
  // The best colour is never removed, so exactly 1 active colour IS the best.
  const BGH_FROG_VARBIT = { gold: 44325, red: 44326, blue: 44327 };
  const BGH_COLOUR_ORDER = ['red', 'blue', 'gold'];   // canonical: active[0] armed 2x, active[1] 1x, active[2] = the "neither"
  // Classify a 2x-active[0] + 1x-active[1] firing by total HP drop; `active` = the ordered
  // candidate colours. Best trap 30000, second-best 15000, worst 7500. Null if the cycle is
  // not a clean 3-trap one.
  function bghClassifyDrop(drop, active) {
    if (!active || active.length < 2 || drop < 22500 || drop > 78000) return null;
    if (drop > 60000) return active[0];              // 30000 + 30000 + wrong  -> the 2x colour is best
    if (drop >= 45000) return active[1];             // 30000 + wrong + wrong  -> the 1x colour is best
    return active[2] || null;                        // all three wrong        -> the unused (3rd) colour
  }
  function bghComposition(a, b, c) {
    const comp = { red: 0, blue: 0, gold: 0, armed: 0 };
    for (const id of [a, b, c]) {
      const k = BGH_ARMED_COLOR[id];
      if (k) { comp[k]++; comp.armed++; }
    }
    return comp;
  }
  function bghTrackDamage(cur, comp, coordKey, active) {
    if (cur == null) return;
    if (comp && comp.armed === 3) bghLastFullComp = comp;
    if (bghPrevHp == null) { bghPrevHp = cur; return; }
    if (cur > bghPrevHp) {                                       // HP rose: new creature / reset
      bghCycleActive = false; bghCycleAccum = 0; bghStableTicks = 0; bghPrevHp = cur; return;
    }
    if (cur < bghPrevHp) {
      bghCycleAccum += (bghPrevHp - cur); bghCycleActive = true; bghStableTicks = 0; bghPrevHp = cur; return;
    }
    if (bghCycleActive && ++bghStableTicks >= 2) {               // ~500ms stable: the firing is done
      bghCommitDiscovery(bghCycleAccum, coordKey, active);
      bghCycleActive = false; bghCycleAccum = 0; bghStableTicks = 0;
    }
  }
  function bghCommitDiscovery(drop, coordKey, active) {
    if (!coordKey || bghBestColors[coordKey] || !active || active.length < 2) return;
    const comp = bghLastFullComp;                                // must be exactly 2x active[0] + 1x active[1]
    if (!comp || comp.armed !== 3 || comp[active[0]] !== 2 || comp[active[1]] !== 1) return;
    const best = bghClassifyDrop(drop, active);
    if (!best) return;
    bghBestColors[coordKey] = best;
    try { localStorage.setItem('rtxBghBest', JSON.stringify(bghBestColors)); } catch (e) {}
  }
  function bghScorpion(label, stateVal, armedItemId) {
    const armed = stateVal === 3;
    let state = BGH_SCORPION_STATES[stateVal] || '-', color = null;
    if (armed) { const a = BGH_SCORPION_ARMED[armedItemId]; state = a ? a.label : 'Armed'; color = a ? a.color : null; }
    return { label: label, state: state, armed: armed, color: color };
  }
  // In-world trap boxes anchor on the pressure plate's live tile; the scorpion spots are fixed
  // offsets from it. Marks carry the snap flag (5th guideMarks field = 1) so the overlay boxes
  // the nearest LIVE object's model AABB and draws nothing when none is there, instead of
  // floating at the wrong cache height.
  const BGH_PLATE_IDS = new Set([113837, 113838, 113839, 113840, 113841,
    113842, 113843, 113844, 113845, 113846, 113847, 113848, 113849, 113850, 113851]);
  const BGH_SPOT_OFFSETS = { A: [0, 10], B: [10, -6], C: [-9, -7] };   // [dx, dy] from the plate tile
  const BGH_SITES = ['A', 'B', 'C'];
  function bghPlateState(v) {   // varbit 44314 value 0..31
    if (v <= 0) return 'Empty';
    if (v === 1) return 'Hotspot (build)';
    if (v === 2) return 'Built (bait it)';
    if (v >= 3 && v <= 12) return 'Baited';
    return '-';
  }
  async function bghUpdatePlate() {
    if (!bridge() || !bridge().sceneEntities) return;
    try {
      const sc = JSON.parse((await bridge().sceneEntities(myPid(), 40)) || '{}');   // 40 covers the arena; cheaper than 64 (cost ~ range^2)
      const objs = (sc && Array.isArray(sc.objects)) ? sc.objects : [];
      bghSceneObjs = objs;
      bghSceneNpcs = (sc && Array.isArray(sc.npcs)) ? sc.npcs : [];
      const plate = objs.find(o => BGH_PLATE_IDS.has(o.id) || /pressure plate|ancient stone/i.test(o.name || ''));
      // A successful scene read with no plate means the traps were left -> forget the layout. During
      // an encounter the plate is always in range; read failures keep the last known layout.
      bghPlate = plate ? { x: plate.x, y: plate.y, plane: plate.plane || 0 } : null;
    } catch (e) { /* keep last scene */ }
  }
  // Nearest matching scene object (id set, or name regex fallback); null if none. Ranked by the
  // object's `dist` (Chebyshev from the player, computed C++-side).
  function bghNearestObj(ids, nameRe) {
    let best = null, bd = Infinity;
    for (const o of bghSceneObjs) {
      const match = (ids && ids.indexOf(o.id) >= 0) || (nameRe && nameRe.test(o.name || ''));
      if (!match) continue;
      const d = (typeof o.dist === 'number') ? o.dist : 0;
      if (d < bd) { bd = d; best = o; }
    }
    return best;
  }
  // Box the nearest match at its REAL tile; if none is loaded, clear the tile box (x=0 makes
  // qgObject clear) rather than marking the player's tile.
  function bghMarkObj(ids, nameRe, locName, label) {
    const o = bghNearestObj(ids, nameRe);
    if (o) { qgObject(locName, label, o.x, o.y, o.plane || 0); return true; }
    qgObject(locName, label, 0, 0, 0);
    return false;
  }
  function bghIsCreatureName(nm) {
    nm = (nm || '').toLowerCase(); if (!nm) return false;
    for (const k in BGH_CREATURES) if (BGH_CREATURES[k].name.toLowerCase() === nm) return true;
    return false;
  }
  // A dead creature is a live OBJECT (ids 113852-113860) named after the creature with a "Skin"
  // action; returns it, else null.
  function bghFindSkinObj() {
    for (const o of bghSceneObjs) {
      const acts = o.actions || [];
      if (acts.some(a => a && String(a).toLowerCase() === 'skin') && bghIsCreatureName(o.name)) return o;
    }
    return null;
  }
  // Hunt creatures still ALIVE are NPCs (carcasses are objects). Discovery only destroys a
  // scorpion + the plate when a SINGLE creature is active; with 2+ active neither is destroyed.
  function bghCountActiveCreatures() {
    let n = 0; for (const npc of bghSceneNpcs) if (npc && bghIsCreatureName(npc.name)) n++; return n;
  }
  async function fetchBgh() {
    if (!bridge() || !bridge().varps || bghFetching) return;
    bghFetching = true;
    try {
      const vp = JSON.parse(await bridge().varps(myPid(), '10946,10947,10948,8603,8604,8628,8629,8630'));
      const vb = await readVarbitValues([53292, 53294, 44325, 44326, 44327, 44266, 59015]);
      await bghUpdatePlate();
      const v8603 = (vp['8603'] || 0) >>> 0, v8604 = (vp['8604'] || 0) >>> 0;
      const struct = vp['10946'] || 0;
      const cre = BGH_CREATURES[struct] || null;
      if (cre) bghSetLastCreature(struct);
      const hpCur = vb[53292] || 0, hpMax = vb[53294] || 0;
      // Per-site state (0 empty, 1 building, 2 built, 3 armed) + armed colour; plate 0..31.
      const sA = (v8603 >>> 27) & 3, sB = (v8603 >>> 29) & 3, sC = (v8604 >>> 0) & 3;
      const sites = [
        { sv: sA, color: BGH_ARMED_COLOR[vp['8628'] || 0] || null },
        { sv: sB, color: BGH_ARMED_COLOR[vp['8629'] || 0] || null },
        { sv: sC, color: BGH_ARMED_COLOR[vp['8630'] || 0] || null },
      ];
      const plateState = (v8604 >>> 2) & 31;
      // Trap layout signature (best colour is keyed to it -- a new tile means rediscovery).
      bghCoordKey = bghPlate ? (bghPlate.plane + ':' + bghPlate.x + ':' + bghPlate.y) : null;
      // Frog elimination: exactly 1 active colour means it IS the best (skip discovery).
      const removed = { gold: (vb[44325] || 0) === 1, red: (vb[44326] || 0) === 1, blue: (vb[44327] || 0) === 1 };
      const active = BGH_COLOUR_ORDER.filter(c => !removed[c]);
      const elimBest = active.length === 1 ? active[0] : null;
      if (elimBest && bghCoordKey && !bghBestColors[bghCoordKey]) {
        bghBestColors[bghCoordKey] = elimBest;
        try { localStorage.setItem('rtxBghBest', JSON.stringify(bghBestColors)); } catch (e) {}
      }
      const comp = bghComposition(vp['8628'] || 0, vp['8629'] || 0, vp['8630'] || 0);
      bghTrackDamage(hpCur, comp, bghCoordKey, active);
      const best = elimBest || (bghCoordKey ? (bghBestColors[bghCoordKey] || null) : null);
      // Guidance: best-colour traps still needed to finish = HP left / 30000 (0..3 sites).
      const need = best ? Math.max(0, Math.min(3, Math.ceil(hpCur / BGH_BEST_DMG))) : null;
      bghData = {
        hunting: !!cre,
        name: cre ? cre.name : null,
        bait: cre ? cre.bait : null,
        hp: { cur: hpCur, max: hpMax },
        aggro: { cur: vp['10947'] || 0, max: vp['10948'] || 0 },
        disc: { coordKey: bghCoordKey, best: best, need: need, active: active, removed: removed, haveLayout: !!bghPlate },
        lodgeTier: vb[44266] || 0,               // hunter lodge tier (0-7); >=3 removes a frog per encounter
        quickTraps: (vb[59015] || 0) === 1,      // Quick traps perk unlocked
        sites: sites,
        plateState: plateState,
        traps: [
          bghScorpion('Scorpion (site A)', (v8603 >>> 27) & 3, vp['8628'] || 0),
          bghScorpion('Scorpion (site B)', (v8603 >>> 29) & 3, vp['8629'] || 0),
          bghScorpion('Scorpion (site C)', (v8604 >>> 0) & 3, vp['8630'] || 0),
          { label: 'Pressure plate', state: bghPlateState((v8604 >>> 2) & 31), armed: (((v8604 >>> 2) & 31) >= 3) },
        ],
      };
    } catch (e) { /* keep previous */ }
    bghFetching = false;
    if (activeTab === 'bgh') renderBgh();
  }
  const BGH_ITEM = { logs: 47942, spear: 47944, vines: 47943 };
  const BGH_POIS_ITEM = { red: 47946, blue: 47947, gold: 47945 };   // poisoned wooden spear per colour
  const BGH_FROG_NAME = { red: 'Phantasmal poison frog', blue: 'Sky-blue poison frog', gold: 'Golden poison frog' };
  const BGH_ARM_ACTION = { red: 'Arm (phantasmal)', blue: 'Arm (sky-blue)', gold: 'Arm (golden)' };
  const BGH_SPEAR_NAME = { red: 'phantasmal', blue: 'sky-blue', gold: 'golden' };   // clean single-word spear name (matches the in-game Arm option)
  // Locs highlighted (BGH cluster): id set + name fallback; the nearest live instance's real
  // tile is boxed, not the player's.
  const BGH_OBJ = {
    baitPad:       { ids: Array.from({ length: 18 }, (_, i) => 113804 + i), re: /^bait pad$/i },
    baitBox:       { ids: [113880, 113881, 113882], re: /^bait box$/i },
    tree:          { ids: [113827], re: /^tree$/i },
    vines:         { ids: [113828], re: /jungle vine/i },
    rubble:        { ids: [113834, 113833], re: /^rubble$/i },
    scorpionBuilt: { ids: [113835], re: /scorpion \(built\)/i },
    plateHotspot:  { ids: [113839], re: /pressure plate hotspot/i },
    plateBuilt:    { ids: [113840, 113841], re: /pressure plate \(built\)/i },
  };
  const BGH_AREAS = [
    { x: 5334, y: 2316, r: 50, struct: 9689 },   // Arcane apoterrasaur
    { x: 5465, y: 2233, r: 50, struct: 9690 },   // Scimitops
    { x: 5393, y: 2253, r: 50, struct: 9691 },   // Bagrada rex
    { x: 5523, y: 2292, r: 50, struct: 9692 },   // Spicati apoterrasaur
    { x: 5617, y: 2249, r: 50, struct: 9710 },   // Asciatops
    { x: 5682, y: 2327, r: 50, struct: 9716 },   // Corbicula rex
    { x: 5610, y: 2375, r: 50, struct: 9840 },   // Oculi apoterrasaur
    { x: 5498, y: 2388, r: 50, struct: 9841 },   // Malletops
    { x: 5529, y: 2442, r: 50, struct: 9843 },   // Pavosaurus rex
  ];
  function bghCreatureByCoords(p) {
    if (!p) return null;
    for (const a of BGH_AREAS) {
      const dx = p.x - a.x, dy = p.y - a.y;
      if (dx * dx + dy * dy <= a.r * a.r) {
        const cre = BGH_CREATURES[a.struct];
        if (cre) return { struct: a.struct, name: cre.name, baitId: cre.bait[0], baitName: cre.bait[1] };
      }
    }
    return null;
  }
  // Remember the hunted creature so the bait pad still works inside the instance, where the
  // player's coordinates no longer match the overworld area check.
  function bghSetLastCreature(struct) {
    const c = BGH_CREATURES[struct]; if (!c) return;
    bghLastCreature = { struct: struct, name: c.name, baitId: c.bait[0], baitName: c.bait[1] };
    try { localStorage.setItem('rtxBghLastCreature', JSON.stringify(bghLastCreature)); } catch (e) {}
  }
  // State-driven planner: each tick, compute the remaining production chain (gather / fletch /
  // poison / build / arm) from live inventory + trap state and emit the single most-upstream
  // missing action. A kill needs ceil(HP / 30000) armed best traps, capped at what is built.
  // Trap damage 30000 / 15000 / 7500: the 2+1 discovery removes at least 30000, leaving at most
  // 60000, so the 2 spare spears from the fletch-of-5 always finish the creature.
  async function bghGuideStep() {
    const d = bghData;
    const set = (phase, msg) => { bghGuidePhase = phase; bghGuideMsg = msg; };
    if (!d) { set('', 'Reading...'); return; }
    const best = d.disc ? d.disc.best : null;
    const hunting = d.hunting, hp = d.hp.cur || 0, hpMax = d.hp.max || 90000;
    // Read the backpack ONCE per tick and count locally; per-count inventory reads back up the
    // 700ms guide loop and lag the in-world highlights.
    let bghInv = [];
    try { const iv = JSON.parse((await PLUGIN_API['state.inventory'].run([], myPid())) || '{}'); if (iv && Array.isArray(iv.items)) bghInv = iv.items; } catch (e) {}
    const invCount = (id) => { let n = 0; for (const it of bghInv) if (it[1] === id) n += (it[2] > 0 ? it[2] : 1); return n; };
    // Skin the kill (top priority). An encounter can have several creatures, so a carcass does
    // NOT mean the hunt is over.
    const skinObj = bghFindSkinObj();
    if (skinObj) {
      qgObject(skinObj.name, 'Skin', skinObj.x, skinObj.y, skinObj.plane || 0);
      set('Skin', 'Skin the ' + skinObj.name + '.'); return;
    }
    // Pre-fight: start at the bait pad once 3 bait are held. Creature/bait is keyed by overworld
    // coords; inside the instance those do not match, so a bait pad in scene falls back to the
    // last creature hunted.
    if (!hunting) {
      const byCoords = bghCreatureByCoords(qgP);
      if (byCoords) bghSetLastCreature(byCoords.struct);
      const padObj = bghNearestObj(BGH_OBJ.baitPad.ids, BGH_OBJ.baitPad.re);
      const area = byCoords || (padObj ? bghLastCreature : null);
      if (!area) { qgClearAll(); set('', 'Move to a Big Game Hunter creature area to begin.'); return; }
      const baitN = invCount(area.baitId);
      if (baitN < 3) {
        bghMarkObj(BGH_OBJ.baitBox.ids, BGH_OBJ.baitBox.re, 'Bait box', 'Withdraw ' + area.baitName + ' (' + baitN + '/3)');
        set('Prep', 'Withdraw ' + area.baitName + ' from the bait box (' + baitN + '/3).'); return;
      }
      bghMarkObj(BGH_OBJ.baitPad.ids, BGH_OBJ.baitPad.re, 'Bait pad', 'Start encounter');
      set('Start', 'Start the encounter (' + area.name + ') at the bait pad.'); return;
    }
    const logs = invCount(BGH_ITEM.logs), wooden = invCount(BGH_ITEM.spear), vines = invCount(BGH_ITEM.vines);
    const poisInv = { red: invCount(BGH_POIS_ITEM.red), blue: invCount(BGH_POIS_ITEM.blue), gold: invCount(BGH_POIS_ITEM.gold) };
    const sites = (d.sites && d.sites.length === 3) ? d.sites : [{ sv: 0 }, { sv: 0 }, { sv: 0 }];
    const armedOf = { red: 0, blue: 0, gold: 0 };
    for (const s of sites) if (s.sv === 3 && s.color) armedOf[s.color]++;
    const builtTotal = sites.filter(s => s.sv >= 2).length;      // built or armed (both count as built)
    const builtUnarmed = sites.filter(s => s.sv === 2).length;
    const plateBuilt = d.plateState >= 2, plateBaited = d.plateState >= 3;
    // Once the creature is damaged, 1 trap decays per fire and decayed ones are NOT rebuilt:
    // finish with whatever is still built. A fresh kill (full HP) rebuilds all 3.
    const midKill = hunting && hp > 0 && hp < hpMax;
    const buildTarget = midKill ? builtTotal : 3;
    const hpPlan = (hunting && hp > 0) ? hp : (hpMax || 90000);
    // Best trap = 30000, so traps to finish = ceil(HP / 30000), capped by what will be built.
    const killTraps = best ? Math.max(1, Math.min(3, Math.ceil(hpPlan / BGH_BEST_DMG))) : 3;
    const needArmed = Math.max(0, Math.min(killTraps, buildTarget));
    // Discovery probes the active candidates (2x first, 1x second); elimination may leave only 2,
    // so it is not always red/blue.
    const discPair = (d.disc && d.disc.active && d.disc.active.length >= 2) ? d.disc.active : ['red', 'blue'];
    const needColors = best ? { [best]: needArmed } : { [discPair[0]]: 2, [discPair[1]]: 1 };
    const order = best ? [best] : [discPair[0], discPair[1]];
    let armedGood = 0; for (const c in needColors) armedGood += Math.min(armedOf[c], needColors[c]);
    // Remaining-chain deficits: armed traps + inventory stock count against them, so a mid-kill
    // finish only fills the gap.
    let poisonDeficit = 0;
    for (const c in needColors) poisonDeficit += Math.max(0, needColors[c] - armedOf[c] - poisInv[c]);
    const woodenTarget = poisonDeficit + (best ? 0 : 2);         // discovery keeps 2 spare spears for the finish
    const deficitBuild = Math.max(0, buildTarget - builtTotal);
    const plateNeedsBuild = !plateBuilt;
    const deficitWooden = Math.max(0, woodenTarget - wooden);
    // The discovery firing destroys the centre plate ONLY with a single creature active (plate
    // built twice); with 2+ active nothing is destroyed. Logs: single discovery 10 (5+3+2),
    // multi-creature discovery 9 (5+3+1), known kill 7 (3+3+1).
    const multiActive = bghCountActiveCreatures() >= 2;
    const rebuildExpected = !best && !multiActive;
    const plateLogs = (plateBuilt ? 0 : 1) + (rebuildExpected ? 1 : 0);
    const logsNeeded = deficitWooden + deficitBuild + plateLogs;
    const vinesNeeded = deficitBuild;
    const nextArmColor = () => { for (const c of order) if (armedOf[c] < needColors[c]) return c; return order[0]; };
    const nextPoisonColor = () => { for (const c of order) if (needColors[c] - armedOf[c] - poisInv[c] > 0) return c; return null; };
    // Gather = ONE step: chop the tree OR cut the vines, whichever is closer AND still needed.
    {
      const needLogs = logs < logsNeeded, needVines = vines < vinesNeeded;
      if (needLogs || needVines) {
        const logCnt = 'logs ' + logs + '/' + logsNeeded, vineCnt = 'vines ' + vines + '/' + vinesNeeded;
        const treeObj = needLogs ? bghNearestObj(BGH_OBJ.tree.ids, BGH_OBJ.tree.re) : null;
        const vineObj = needVines ? bghNearestObj(BGH_OBJ.vines.ids, BGH_OBJ.vines.re) : null;
        const td = (treeObj && typeof treeObj.dist === 'number') ? treeObj.dist : 1e9;
        const vd = (vineObj && typeof vineObj.dist === 'number') ? vineObj.dist : 1e9;
        if (treeObj && (td <= vd || !vineObj)) {
          qgObject(treeObj.name || 'Tree', 'Chop jungle logs (' + logs + '/' + logsNeeded + ')', treeObj.x, treeObj.y, treeObj.plane || 0);
          set('Gather', 'Chop jungle logs (' + logCnt + ', ' + vineCnt + ').'); return;
        }
        if (vineObj) {
          qgObject(vineObj.name || 'Jungle vines', 'Cut jungle vines (' + vines + '/' + vinesNeeded + ')', vineObj.x, vineObj.y, vineObj.plane || 0);
          set('Gather', 'Cut jungle vines (' + vineCnt + ', ' + logCnt + ').'); return;
        }
        qgObject(needLogs ? 'Tree' : 'Jungle vines', needLogs ? 'Chop jungle logs' : 'Cut jungle vines', 0, 0, 0);
        set('Gather', needLogs ? ('Chop jungle logs (' + logCnt + ').') : ('Cut jungle vines (' + vineCnt + ').')); return;
      }
    }
    if (poisonDeficit > 0 && wooden < woodenTarget) {
      const makeAmt = woodenTarget - wooden;
      // If the Fletching make-X interface (group 1371) is open, verify its quantity and warn otherwise.
      let qty = -1;
      try {
        // interfaceGroup returns the full tree {widgets:[{ty,x=text,r:[x,y,w,h],...}]}. The make-X
        // quantity is a pure-integer TEXT in the right-side quantity bar (r[0]>=200, ~24px tall).
        const g = JSON.parse((await bridge().interfaceGroup(myPid(), 1371)) || '{}');
        const ws = (g && Array.isArray(g.widgets)) ? g.widgets : [];
        for (const c of ws) {
          if (c.ty === 'text' && typeof c.x === 'string' && /^\d+$/.test(c.x.trim()) && c.r && c.r[0] >= 200 && c.r[3] >= 18 && c.r[3] <= 30) { qty = parseInt(c.x, 10); break; }
        }
      } catch (e) {}
      if (qty >= 0 && qty !== makeAmt) {
        if (bghFletchWarnQty !== qty) { bghFletchWarnQty = qty; try { bridge().overlayToast(myPid(), 'Craft x' + makeAmt + ' spears (not ' + qty + ')'); } catch (e) {} }
        qgItem(BGH_ITEM.logs, 'Set the amount to ' + makeAmt);
        set('Fletch', 'Set the make amount to ' + makeAmt + ' (currently ' + qty + '), then fletch.'); return;
      }
      bghFletchWarnQty = -1;
      qgItem(BGH_ITEM.logs, 'Fletch into wooden spears (' + wooden + '/' + woodenTarget + ')');
      set('Fletch', 'Fletch logs into wooden spears (' + wooden + '/' + woodenTarget + ').'); return;
    }
    const pc = nextPoisonColor();
    if (pc) {
      const have = poisInv[pc], needp = needColors[pc];
      await qgNpc(BGH_FROG_NAME[pc], 'Poison a spear (' + have + '/' + needp + ')');
      set('Poison', 'Poison a spear at the ' + BGH_COLOR_INFO[pc].label + ' frog (' + have + '/' + needp + ').'); return;
    }
    // Build + arm interleaved by proximity: arm the nearest built scorpion OR build the nearest
    // rubble, whichever is closer.
    {
      const needBuild = builtTotal < buildTarget;
      const needArm = armedGood < needArmed && builtUnarmed > 0;
      if (needBuild || needArm) {
        const buildCnt = 'build ' + builtTotal + '/' + buildTarget, armCnt = 'arm ' + armedGood + '/' + needArmed;
        const rubbleObj = needBuild ? bghNearestObj(BGH_OBJ.rubble.ids, BGH_OBJ.rubble.re) : null;
        const scorpObj = needArm ? bghNearestObj(BGH_OBJ.scorpionBuilt.ids, BGH_OBJ.scorpionBuilt.re) : null;
        const rd = (rubbleObj && typeof rubbleObj.dist === 'number') ? rubbleObj.dist : 1e9;
        const sd = (scorpObj && typeof scorpObj.dist === 'number') ? scorpObj.dist : 1e9;
        if (scorpObj && (sd <= rd || !rubbleObj)) {
          const c = nextArmColor(), sp = BGH_SPEAR_NAME[c] || 'poisoned';
          qgObject(scorpObj.name || 'Scorpion (built)', 'Arm with a ' + sp + ' spear (' + armCnt + ')', scorpObj.x, scorpObj.y, scorpObj.plane || 0);
          set('Arm', 'Arm the nearest scorpion with a ' + sp + ' spear (' + armCnt + ', ' + buildCnt + ').'); return;
        }
        if (rubbleObj) {
          qgObject(rubbleObj.name || 'Rubble', 'Build a scorpion trap (' + buildCnt + ')', rubbleObj.x, rubbleObj.y, rubbleObj.plane || 0);
          set('Build', 'Build the nearest scorpion trap (' + buildCnt + ', ' + armCnt + ').'); return;
        }
        // needed but the object isn't loaded in the scene yet: keep the instruction, clear the box
        qgObject('Scorpion', needArm ? 'Arm a scorpion' : 'Build a scorpion trap', 0, 0, 0);
        set(needArm ? 'Arm' : 'Build', needArm ? ('Arm a scorpion (' + armCnt + ').') : ('Build a scorpion trap (' + buildCnt + ').')); return;
      }
    }
    if (plateNeedsBuild) { bghMarkObj(BGH_OBJ.plateHotspot.ids, BGH_OBJ.plateHotspot.re, 'Pressure plate hotspot', 'Build the pressure plate'); set('Plate', 'Build the pressure plate.'); return; }
    if (plateBaited) { qgClearAll(); set('Wait', best ? 'Baited. Waiting for the traps to fire.' : 'Baited. Watch the health drop to reveal the best colour.'); return; }
    // Baiting needs the creature's bait in inventory; if empty, withdraw it from the bait box first.
    const baitId = d.bait ? d.bait[0] : 0, baitName = d.bait ? d.bait[1] : 'bait';
    const baitHave = baitId ? invCount(baitId) : 1;
    if (baitId && baitHave < 1) {
      bghMarkObj(BGH_OBJ.baitBox.ids, BGH_OBJ.baitBox.re, 'Bait box', 'Withdraw ' + baitName + ' (' + baitHave + '/1)');
      set('Bait', 'Withdraw ' + baitName + ' from the bait box (' + baitHave + '/1) to bait.'); return;
    }
    bghMarkObj(BGH_OBJ.plateBuilt.ids, BGH_OBJ.plateBuilt.re, 'Pressure plate (built)', 'Bait the pressure plate');
    set('Bait', best ? ('Bait to fire ' + needArmed + ' ' + BGH_COLOR_INFO[best].label + ' trap' + (needArmed > 1 ? 's' : '') + '.') : ('Bait to run discovery (2 ' + discPair[0] + ' + 1 ' + discPair[1] + ').'));
  }
  function renderBgh() {
    const c = $('content');
    let wrap = $('bghWrap');
    if (!wrap) {
      c.innerHTML = '';
      wrap = document.createElement('div'); wrap.id = 'bghWrap'; wrap.className = 'pane';
      // flex:0 0 auto (not the .pane default flex:1): size to content so the tall varbit list makes
      // .content scroll instead of flex-shrinking every group until its rows clip.
      wrap.style.cssText = 'flex:0 0 auto;display:flex;flex-direction:column;gap:14px;padding:4px 2px;';
      c.appendChild(wrap); bghStructSig = '';
    }
    paintBgh();
  }
  function bghBar(label, cur, max, id) {
    const pct = max > 0 ? Math.max(0, Math.min(100, (cur / max) * 100)) : 0;
    const row = document.createElement('div'); if (id) row.id = id;
    const head = document.createElement('div');
    head.style.cssText = 'display:flex;justify-content:space-between;font-size:12px;color:var(--text-dim);margin-bottom:4px;';
    const l = document.createElement('span'); l.textContent = label;
    const v = document.createElement('span'); v.className = 'bgh-bar-val'; v.textContent = cur.toLocaleString() + (max > 0 ? ' / ' + max.toLocaleString() : '');
    head.appendChild(l); head.appendChild(v);
    const track = document.createElement('div');
    track.style.cssText = 'height:10px;border-radius:5px;background:rgba(255,255,255,0.06);border:1px solid var(--border);overflow:hidden;';
    const fill = document.createElement('div'); fill.className = 'bgh-bar-fill';
    fill.style.cssText = 'height:100%;width:' + pct.toFixed(1) + '%;background:linear-gradient(90deg,var(--accent-lo),var(--accent-hi));transition:width 220ms;';
    track.appendChild(fill);
    row.appendChild(head); row.appendChild(track);
    return row;
  }
  function bghSetBar(id, cur, max) {
    const row = document.getElementById(id); if (!row) return;
    const val = row.querySelector('.bgh-bar-val'), fill = row.querySelector('.bgh-bar-fill');
    if (val) val.textContent = cur.toLocaleString() + (max > 0 ? ' / ' + max.toLocaleString() : '');
    if (fill) fill.style.width = (max > 0 ? Math.max(0, Math.min(100, (cur / max) * 100)) : 0).toFixed(1) + '%';
  }
  function updateBghVolatile(d) {
    const m = document.getElementById('bghGuideMsgEl'); if (m) m.textContent = bghGuideMsg || 'Starting...';
    if (d && d.hunting) { bghSetBar('bghBarHealth', d.hp.cur, d.hp.max); bghSetBar('bghBarAggro', d.aggro.cur, d.aggro.max); }
  }
  function paintBgh() {
    const wrap = $('bghWrap'); if (!wrap) return;
    const d = bghData;
    // STRUCTURAL fields trigger a full rebuild; VOLATILE values (health, aggression, guide
    // message) patch in place so the panel does not rebuild on every health/aggro tick.
    const structSig = JSON.stringify([bghGuideOn, bghGuidePhase, !d ? 0 : [d.hunting, d.name, d.bait, d.traps, d.disc, d.lodgeTier, d.quickTraps]]);
    const volSig = JSON.stringify([bghGuideMsg, !d ? 0 : [d.hp, d.aggro]]);
    if (structSig === bghStructSig) {
      if (volSig !== bghVolSig) { bghVolSig = volSig; updateBghVolatile(d); }
      return;
    }
    bghStructSig = structSig; bghVolSig = volSig;
    wrap.innerHTML = '';
    const gk = document.createElement('div');
    gk.style.cssText = 'display:flex;align-items:center;gap:8px;font-size:12px;color:var(--text-dim);cursor:pointer;';
    const gpill = document.createElement('div'); gpill.className = 'al-pill' + (bghGuideOn ? ' on' : '');
    gpill.appendChild(document.createElement('span'));
    gk.addEventListener('click', () => {
      bghGuideOn = !bghGuideOn;
      gpill.classList.toggle('on', bghGuideOn);
      try { localStorage.setItem('rtxBghGuide', bghGuideOn ? '1' : '0'); } catch (e) {}
      if (!bghGuideOn) { try { qgClearAll(); } catch (e) {} bghGuideMsg = ''; bghGuidePhase = ''; }
      bghStructSig = ''; if (activeTab === 'bgh') renderBgh();
    });
    gk.appendChild(gpill); gk.appendChild(document.createTextNode('Guide me step-by-step'));
    wrap.appendChild(gk);
    if (bghGuideOn) {
      const gs = document.createElement('div');
      gs.style.cssText = 'display:flex;flex-direction:column;gap:2px;padding:9px 11px;border-radius:6px;border:1px solid var(--border);background:rgba(124,109,242,0.08);';
      const gh = document.createElement('div');
      gh.style.cssText = 'color:var(--text-dim);text-transform:uppercase;font-size:10px;letter-spacing:0.04em;';
      gh.textContent = bghGuidePhase ? ('Step: ' + bghGuidePhase) : 'Guide';
      const gt = document.createElement('div'); gt.id = 'bghGuideMsgEl';
      gt.style.cssText = 'font-size:13px;color:var(--text);line-height:1.35;';
      gt.textContent = bghGuideMsg || 'Starting...';
      gs.appendChild(gh); gs.appendChild(gt);
      wrap.appendChild(gs);
    }
    if (!d) { const e = document.createElement('div'); e.className = 'stor-empty'; e.textContent = 'Reading...'; wrap.appendChild(e); return; }
    if (!d.hunting) {
      const e = document.createElement('div'); e.className = 'stor-empty';
      e.textContent = 'No Big Game Hunter creature tracked. Start a hunt on Anachronia.';
      wrap.appendChild(e);
    } else {
      const hdr = document.createElement('div');
      hdr.style.cssText = 'font-size:15px;font-weight:600;color:var(--text);';
      hdr.textContent = d.name;
      wrap.appendChild(hdr);
      if (d.bait) {
        const br = document.createElement('div');
        br.style.cssText = 'display:flex;align-items:center;gap:8px;padding:8px 10px;border-radius:6px;border:1px solid var(--border);background:rgba(255,255,255,0.03);';
        const ico = document.createElement('div'); ico.className = 'bank-icon';
        // Set the bg URL DIRECTLY: attachBankIcon -> sizeIcon picks the native pixel size while
        // clientWidth is 0 then flips to contain, which zooms on every re-render; with no
        // data-ico-url, sizeAllIcons leaves it alone.
        ico.style.cssText = 'position:relative;inset:auto;width:24px;height:24px;flex:0 0 24px;background-size:contain;';
        const bico = resolveIcon(d.bait[0]); if (bico) ico.style.backgroundImage = "url('" + bico + "')";
        br.appendChild(ico);
        const tx = document.createElement('span'); tx.style.cssText = 'font-size:13px;color:var(--text);';
        tx.innerHTML = '<span style="color:var(--text-dim)">Bait: </span>';
        tx.appendChild(document.createTextNode(d.bait[1]));
        br.appendChild(tx); wrap.appendChild(br);
      }
      wrap.appendChild(bghBar('Health', d.hp.cur, d.hp.max, 'bghBarHealth'));
      wrap.appendChild(bghBar('Aggression', d.aggro.cur, d.aggro.max, 'bghBarAggro'));
    }
    {
      const lh = document.createElement('div');
      lh.style.cssText = 'font-size:12px;font-weight:600;color:var(--text-dim);text-transform:uppercase;letter-spacing:0.04em;margin-top:6px;';
      lh.textContent = 'Hunter lodge';
      wrap.appendChild(lh);
      const box = document.createElement('div');
      box.style.cssText = 'display:flex;flex-direction:column;gap:5px;padding:9px 11px;border-radius:6px;border:1px solid var(--border);background:rgba(255,255,255,0.03);font-size:13px;color:var(--text);';
      const kv = (k, valNode) => {
        const row = document.createElement('div'); row.style.cssText = 'display:flex;align-items:center;gap:8px;';
        const kk = document.createElement('span'); kk.style.cssText = 'color:var(--text-dim);min-width:96px;'; kk.textContent = k;
        row.appendChild(kk); row.appendChild(valNode); return row;
      };
      const txt = (s, col) => { const e = document.createElement('span'); if (col) e.style.color = col; e.textContent = s; return e; };
      box.appendChild(kv('Lodge tier', txt(String(d.lodgeTier || 0) + (d.lodgeTier >= 3 ? ' (removes a frog)' : ''))));
      box.appendChild(kv('Quick traps', txt(d.quickTraps ? 'Unlocked' : 'Locked', d.quickTraps ? 'var(--accent-hi)' : 'var(--text-mute)')));
      const bf = document.createElement('div'); bf.style.cssText = 'display:flex;align-items:center;gap:6px;flex-wrap:wrap;';
      const removed = (d.disc && d.disc.removed) || {};
      const blocked = ['red', 'blue', 'gold'].filter(c => removed[c]);
      if (!blocked.length) { bf.appendChild(txt('None', 'var(--text-mute)')); }
      else for (const c of blocked) {
        const chip = document.createElement('span');
        chip.style.cssText = 'display:inline-flex;align-items:center;gap:5px;padding:1px 7px 1px 5px;border-radius:10px;border:1px solid var(--border);font-size:12px;';
        const sw = document.createElement('span'); sw.style.cssText = 'width:10px;height:10px;border-radius:3px;background:' + BGH_COLOR_INFO[c].hex + ';';
        chip.appendChild(sw); chip.appendChild(document.createTextNode(BGH_COLOR_INFO[c].label));
        bf.appendChild(chip);
      }
      box.appendChild(kv('Blocked frogs', bf));
      wrap.appendChild(box);
    }
    {
      const bt = document.createElement('div');
      bt.style.cssText = 'font-size:12px;font-weight:600;color:var(--text-dim);text-transform:uppercase;letter-spacing:0.04em;margin-top:6px;';
      bt.textContent = 'Best trap';
      wrap.appendChild(bt);
      const card = document.createElement('div');
      card.style.cssText = 'display:flex;flex-direction:column;gap:6px;padding:9px 11px;border-radius:6px;border:1px solid var(--border);background:rgba(255,255,255,0.03);';
      const disc = d.disc || {};
      const line = (colorKey, main, sub) => {
        const row = document.createElement('div'); row.style.cssText = 'display:flex;align-items:center;gap:9px;';
        if (colorKey) {
          const sw = document.createElement('span');
          sw.style.cssText = 'width:14px;height:14px;flex:0 0 14px;border-radius:4px;border:1px solid var(--border);background:' + (BGH_COLOR_INFO[colorKey] ? BGH_COLOR_INFO[colorKey].hex : '#888') + ';';
          row.appendChild(sw);
        }
        const tx = document.createElement('div');
        tx.style.cssText = 'font-size:13px;color:var(--text);line-height:1.35;';
        tx.appendChild(document.createTextNode(main));
        if (sub) { const s = document.createElement('div'); s.style.cssText = 'font-size:11.5px;color:var(--text-dim);'; s.textContent = sub; tx.appendChild(s); }
        row.appendChild(tx); return row;
      };
      if (!disc.haveLayout) {
        card.appendChild(line(null, 'Move next to the traps so the layout is detected.'));
      } else if (disc.best) {
        const info = BGH_COLOR_INFO[disc.best];
        const main = 'Best colour: ' + info.label;
        const sub = (disc.need != null && d.hunting)
          ? ('Arm ' + disc.need + ' scorpion' + (disc.need === 1 ? '' : 's') + ' ' + disc.best + ' (health / 30000).')
          : 'Layout learned. Arm all scorpions ' + disc.best + '.';
        card.appendChild(line(disc.best, main, sub));
      } else {
        const ap = (disc.active && disc.active.length >= 2) ? disc.active : ['red', 'blue'];
        const rem = disc.removed ? Object.keys(disc.removed).filter(c => disc.removed[c]) : [];
        const sub = 'Build 2 ' + ap[0] + ' + 1 ' + ap[1] + ', then bait once. The health drop reveals the best colour.'
          + (rem.length ? ' (' + rem.join(', ') + ' eliminated)' : '');
        card.appendChild(line(null, 'Discovering best colour...', sub));
      }
      wrap.appendChild(card);
    }
    // Traps section (always shown -- trap state persists independent of an active creature).
    const th = document.createElement('div');
    th.style.cssText = 'font-size:12px;font-weight:600;color:var(--text-dim);text-transform:uppercase;letter-spacing:0.04em;margin-top:6px;';
    th.textContent = 'Traps';
    wrap.appendChild(th);
    for (const t of d.traps) {
      const row = document.createElement('div');
      row.style.cssText = 'display:flex;justify-content:space-between;align-items:center;padding:8px 10px;border-radius:6px;border:1px solid var(--border);background:rgba(255,255,255,0.03);';
      const l = document.createElement('span'); l.style.cssText = 'font-size:13px;color:var(--text);'; l.textContent = t.label;
      const s = document.createElement('span');
      const col = t.color || (t.armed ? 'var(--accent-hi)' : 'var(--text-dim)');
      s.style.cssText = 'font-size:12px;font-weight:600;color:' + col + ';';
      s.textContent = t.state;
      row.appendChild(l); row.appendChild(s);
      wrap.appendChild(row);
    }
  }
