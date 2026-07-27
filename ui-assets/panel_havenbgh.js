// RuneToolsX panel: Big Game Hunter (Havenhythe) - giant chinchompas/kebbits. Separate from
// the Anachronia BGH panel (tab id 'bgh').
// Spliced inline into client.html; bare classic script sharing one global scope.

  // Creature data keyed by boss-bar HUD struct id (boss HUD iface 1181, the same generic HUD as
  // Anachronia).
  const HAVEN_BGH_CREATURES = {
    52833: { tier: 1, name: 'Giant chinchompa',             npc: 32778, bait: [383,   'Raw shark'],                 hunter: 96,  slayer: 77, cost: 367,   xp: 21000, sxp: 500 },
    52834: { tier: 2, name: 'Giant carnivorous chinchompa', npc: 32779, bait: [60678, 'Raw giant kebbit meat'],     hunter: 100, slayer: 81, cost: 60521, xp: 28500, sxp: 1000 },
    52835: { tier: 3, name: 'Giant volatile chinchompa',    npc: 32780, bait: [60678, 'Raw giant kebbit meat'],     hunter: 104, slayer: 85, cost: 60521, xp: 48750, sxp: 1500 },
    52836: { tier: 4, name: 'Giant golden chinchompa',      npc: 32781, bait: [60678, 'Raw giant kebbit meat'],     hunter: 108, slayer: 91, cost: 60521, xp: 62500, sxp: 2000 },
    52837: { tier: 1, name: 'Giant kebbit',                 npc: 32782, bait: [60677, 'Raw giant chinchompa meat'], hunter: 98,  slayer: 77, cost: 65680, xp: 25750, sxp: 750 },
    52838: { tier: 2, name: 'Giant spiked kebbit',          npc: 32784, bait: [60677, 'Raw giant chinchompa meat'], hunter: 102, slayer: 81, cost: 65680, xp: 33750, sxp: 1500 },
    52839: { tier: 3, name: 'Giant diseased kebbit',        npc: 32786, bait: [60677, 'Raw giant chinchompa meat'], hunter: 106, slayer: 85, cost: 65680, xp: 60000, sxp: 2000 },
    52840: { tier: 4, name: 'Giant dashing kebbit',         npc: 32788, bait: [60677, 'Raw giant chinchompa meat'], hunter: 110, slayer: 91, cost: 65680, xp: 75000, sxp: 2000 },
  };

  // Hunting areas (world tiles).
  const HAVEN_BGH_AREAS = [
    { key: 'chinchompa', name: 'Chinchompa area', x: 3652, y: 8036 },
    { key: 'kebbit',     name: 'Kebbit pen',      x: 3617, y: 8015 },
  ];

  let havenBghData = null;
  let havenBghFetching = false;
  let havenBghSig = '';          // paint signature: rebuild the DOM only on a real change

  // Moth-jar buffs: each varbit holds the number of 15-SECOND PERIODS the buff has left
  // (0 = inactive), packed in varp 12771. Time remaining = periods * 15s.
  const HAVEN_MOTH_BUFFS = [
    { vb: 60867, name: 'Sugilite moth jar', effect: 'Max aggression 10 -> 14' },
    { vb: 60868, name: 'Calcite moth jar',  effect: '+30% trap build / arm speed' },
    { vb: 60870, name: 'Azurite moth jar',  effect: 'Removes 1 moth / mushroom type' },
  ];

  // Live signals: varbit 60866 = supplies-box access (1 = yes); varp 10947 = aggression;
  // varbits 60867/60868/60870 = moth-jar buff timers; varbits 60768-60771 (varp 12693) =
  // log-trap spot states (1 default, 2 built, 3 armed); varbit 53292 = creature health.
  async function fetchHavenBgh() {
    if (!bridge() || havenBghFetching) return;
    havenBghFetching = true;
    try {
      let vb = {}, vp = {};
      if (bridge().varbits) { try { vb = JSON.parse(await bridge().varbits(myPid(), '60866,60867,60868,60870,60768,60769,60770,60771,53292,44297,44298,44299') || '{}'); } catch (e) {} }
      if (bridge().varps)   { try { vp = JSON.parse(await bridge().varps(myPid(), '10947') || '{}'); } catch (e) {} }
      const buffs = {}; for (const b of HAVEN_MOTH_BUFFS) buffs[b.vb] = (vb[String(b.vb)] | 0);
      const traps = { 60768: (vb['60768'] | 0), 60769: (vb['60769'] | 0), 60770: (vb['60770'] | 0), 60771: (vb['60771'] | 0) };
      // Hollow-tree search state: one varbit per tree, 1 = searched (on cooldown), 0 = searchable.
      const trees = { 44297: (vb['44297'] | 0), 44298: (vb['44298'] | 0), 44299: (vb['44299'] | 0) };
      havenBghData = { boxAccess: (vb['60866'] | 0) === 1, aggro: (vp['10947'] | 0), bossHp: (vb['53292'] | 0), buffs: buffs, traps: traps, trees: trees };
    } catch (e) { /* keep previous */ }
    havenBghFetching = false;
    try { await havenBghTrackDiscovery(); } catch (e) {}   // pair ground-moth colour with the HP drop -> ideal poison
    if (activeTab === 'havenbgh') renderHavenBgh();
  }

  // ---- Step-by-step guide ----
  // Opt-in, driven from questGuideTick in client.html. Each tick reads scene + inventory and boxes
  // the next in-world action via overlay.guideTiles, always the instance closest to the player.
  let havenBghGuideOn = false;
  try { havenBghGuideOn = localStorage.getItem('rtxHavenBghGuide') === '1'; } catch (e) {}
  let havenBghGuideMsg = '', havenBghGuidePhase = '';
  let havenBghSceneObjs = [], havenBghSceneNpcs = [];
  let havenBghMothHl = false;   // moth NPC-highlight active (cleared when they despawn / step passes)
  let havenBghTrigOn = false;   // creature-on-bait alert active (owns the centerText slot while true)
  // Hunt-creature NPC base cfg ids (the resolved morph may differ -> name fallback in the check).
  const HAVEN_CREATURE_NPC_IDS = {};
  for (const _sk in HAVEN_BGH_CREATURES) HAVEN_CREATURE_NPC_IDS[HAVEN_BGH_CREATURES[_sk].npc] = 1;
  // The live hunt-creature NPC if it is in scene; null between encounters.
  function havenBghFindCreature() {
    for (const n of havenBghSceneNpcs) {
      if (!n) continue;
      if (HAVEN_CREATURE_NPC_IDS[n.id] || /^giant .*(chinchompa|kebbit)/i.test(n.name || '')) return n;
    }
    return null;
  }

  // STEP 1: GATHERING. Log Pile / Rope are click-PER-ITEM: one click yields exactly one item, so
  // the player must click repeatedly (click-and-wait does not keep gathering).
  const HAVEN_GATHER = {
    logs: { need: 4, item: 60740, id: 136942, re: /^log pile$/i, loc: 'Log Pile' },
    rope: { need: 5, item: 60741, id: 136944, re: /^rope$/i,     loc: 'Rope' },
  };
  // STEP 2: BUILD. The "Broken trap" loc is present until the trap is built (build uses 1 log +
  // 1 rope); its PRESENCE is the build-state signal.
  const HAVEN_TRAP = { brokenId: 136903, brokenRe: /^broken trap$/i };
  // STEP 3: MOTHS. Per-tree searched state (1 = on cooldown, 0 = searchable again). Spawned
  // catchable moths are NPCS with a "Catch" action (the ground-item moths are only trap bait).
  // NPC ids: Verdite 32790 / Lazurite 32791 / Purpurite 32792.
  const HAVEN_TREE_VBS = [44297, 44298, 44299];
  // Per-tree loc id -> its searched varbit, so the guide points only at a tree that is ITSELF
  // searchable, not any tree while some other one is off cooldown.
  const HAVEN_TREES = { 136939: 44297, 136940: 44298, 136941: 44299 };
  const HAVEN_MOTH_NPC_IDS = { 32790: 1, 32791: 1, 32792: 1 };
  // STEP 4: LOG TRAPS. Each spot takes TWO interactions: Build the "Unbuilt log trap" (1 log +
  // 1 rope) then Set it (1 rope). Spot state varbits (varp 12693): 1 default, 2 built, 3 armed.
  const HAVEN_LOGTRAP = {
    unbuiltId: 136922, unbuiltRe: /^unbuilt log trap$/i,   // Build (1 log + 1 rope)
    setId: 136923,     setRe: /^log trap$/i,                // Set (1 rope); anchored name excludes the armed variant
    spots: [60768, 60769, 60770, 60771],
  };
  // STEP 5: HIDE + BAIT. Hide in Tall grass by the closest armed trap, then bait it. Loc 136924
  // carries the per-colour bait actions. Hidden state = varc 779 (3593 hidden / 2698 visible).
  const HAVEN_ARMED = { id: 136924, re: /^log trap \(armed\)$/i };
  const HAVEN_HIDE  = { grassRe: /^tall grass$/i, varc: '5:779', hiddenVal: 3593, visibleVal: 2698 };

  // ---- Persistent hazard / area marks (drawn every guide tick alongside the step tiles) ----
  // Brittle bone locs are stealth hazards: every position seen this session stays marked RED.
  // The Tall grass tufts (4 connected) bound one hide-area: its full span is marked GREEN.
  // Burnt grass = the same area after it burns: marked YELLOW with an 18s (30 x 0.6s ticks)
  // countdown from the tick it transitioned from Tall grass (lastSnap.tick_count + 30).
  const HAVEN_MARK_RGB = { bone: 0xE05050, tall: 0x5BD75B, burnt: 0xE8C34A };
  const HAVEN_BONE_RE = /^brittle bone/i, HAVEN_BURNT_RE = /^burnt grass$/i;
  const HAVEN_GRASS_LINK = 2;    // Chebyshev link distance: only directly connected tufts form one area
  let havenBghBones = {};        // 'x,y' -> {x,y,plane}: every Brittle bone seen this session
  let havenBghTallAreas = [];    // [{x1,y1,x2,y2,plane}] current tall-grass cluster bounds
  let havenBghBurntAreas = [];   // [{x1,y1,x2,y2,plane,deadline}] deadline = tick# when the 18s ends (0 = start unseen)
  function havenBghHazardReset() { havenBghBones = {}; havenBghTallAreas = []; havenBghBurntAreas = []; }
  // Cluster objects into groups linked by Chebyshev distance, each reduced to its bbox.
  function havenBghClusters(objs) {
    const rem = objs.slice(), out = [];
    while (rem.length) {
      const grp = [rem.pop()];
      for (let i = 0; i < grp.length; i++)
        for (let j = rem.length - 1; j >= 0; j--)
          if (Math.max(Math.abs(rem[j].x - grp[i].x), Math.abs(rem[j].y - grp[i].y)) <= HAVEN_GRASS_LINK)
            grp.push(rem.splice(j, 1)[0]);
      let x1 = grp[0].x, y1 = grp[0].y, x2 = x1, y2 = y1;
      for (const o of grp) { x1 = Math.min(x1, o.x); y1 = Math.min(y1, o.y); x2 = Math.max(x2, o.x); y2 = Math.max(y2, o.y); }
      // members: the actual tuft anchors -- marks draw per tuft (exact loc footprints); the bbox is
      // only for nearest-N selection + burn-transition overlap tracking.
      out.push({ x1: x1, y1: y1, x2: x2, y2: y2, plane: grp[0].plane | 0, n: grp.length,
                 members: grp.map(o => ({ x: o.x, y: o.y })) });
    }
    return out;
  }
  function havenBghOverlap(a, b) { return a.x1 <= b.x2 && b.x1 <= a.x2 && a.y1 <= b.y2 && b.y1 <= a.y2; }
  // Update the hazard state from the current scene snapshot (call after havenBghUpdateScene).
  function havenBghHazardTick() {
    const tick = (lastSnap && lastSnap.tick_count) || 0;
    for (const o of havenBghSceneObjs)
      if (HAVEN_BONE_RE.test(o.name || '')) havenBghBones[o.x + ',' + o.y] = { x: o.x, y: o.y, plane: o.plane | 0 };
    const prevTall = havenBghTallAreas;
    // The scene object list mixes STATIC cache placements (which keep the pre-morph "Tall grass"
    // name forever) with live morph-aware entries, so a burnt patch can read as BOTH names on the
    // same tiles. Burnt wins: drop tall tufts on a burnt tile, then drop any tall cluster whose area
    // overlaps a burnt cluster.
    const burntObjs = havenBghSceneObjs.filter(o => HAVEN_BURNT_RE.test(o.name || ''));
    const burntTile = {};
    for (const o of burntObjs) burntTile[o.x + ',' + o.y] = 1;
    const tallObjs = havenBghSceneObjs.filter(o => HAVEN_HIDE.grassRe.test(o.name || '') && !burntTile[o.x + ',' + o.y]);
    const burnt = havenBghClusters(burntObjs);
    havenBghTallAreas = havenBghClusters(tallObjs).filter(t => !burnt.some(b => havenBghOverlap(t, b)));
    // Carry a running countdown across ticks by bbox overlap; a NEW burnt area overlapping what was
    // Tall grass last tick just transitioned -> deadline = now + 30 ticks (18s). Burnt already
    // present with no observed transition gets no countdown.
    havenBghBurntAreas = burnt.map(b => {
      const prev = havenBghBurntAreas.find(p => havenBghOverlap(p, b));
      const deadline = prev ? prev.deadline : (tick > 0 && prevTall.some(t => havenBghOverlap(t, b)) ? tick + 30 : 0);
      return { x1: b.x1, y1: b.y1, x2: b.x2, y2: b.y2, plane: b.plane, members: b.members, deadline: deadline };
    });
  }
  // All marks -> one guideMarks payload: STEP tiles first (default colour, so they keep the
  // direction arrow), then the rgb-tagged hazard marks. Extended record:
  // x \x1f y \x1f plane \x1f label \x1f snap \x1f rgb [\x1f x2 \x1f y2].
  // Chebyshev distance from the player (qgP) to a tile / an area bbox; Infinity when the player
  // tile is unknown so sorting still works.
  function havenBghPlayerDist(x1, y1, x2, y2) {
    if (!qgP) return Infinity;
    const dx = Math.max(x1 - qgP.x, 0, qgP.x - x2), dy = Math.max(y1 - qgP.y, 0, qgP.y - y2);
    return Math.max(dx, dy);
  }
  function havenBghSetMarks(stepTiles) {
    const recs = [];
    const clean = s => String(s || '').replace(/[\x1e\x1f]/g, ' ');
    for (const t of (stepTiles || []))
      recs.push(t.x + '\x1f' + t.y + '\x1f' + (t.plane | 0) + '\x1f' + clean(t.label));
    // Only the NEAREST 2 bones + 3 tall-grass areas: the pen has many of each and marking them all
    // buries the screen. Burnt areas stay unlimited -- their countdowns matter.
    const bones = Object.keys(havenBghBones).map(k => havenBghBones[k])
      .sort((a, b) => havenBghPlayerDist(a.x, a.y, a.x, a.y) - havenBghPlayerDist(b.x, b.y, b.x, b.y))
      .slice(0, 2);
    // '-' prefix = match-only name: the reader still wraps the loc's footprint but shows no pill.
    for (const b of bones)
      recs.push(b.x + '\x1f' + b.y + '\x1f' + b.plane + '\x1f' + '-Brittle bone' + '\x1f0\x1f' + HAVEN_MARK_RGB.bone);
    const tick = (lastSnap && lastSnap.tick_count) || 0;
    // REGION marks (trailing field 1): the reader merges each patch's tuft footprints into flat
    // floor-level zones covering exactly the grass tiles. Disjoint patches under the same label
    // split into separate shapes reader-side, each with its own pill.
    const tallNear = havenBghTallAreas.slice()
      .sort((a, b) => havenBghPlayerDist(a.x1, a.y1, a.x2, a.y2) - havenBghPlayerDist(b.x1, b.y1, b.x2, b.y2))
      .slice(0, 3);
    for (const a of tallNear)
      for (const m of a.members)
        recs.push(m.x + '\x1f' + m.y + '\x1f' + a.plane + '\x1f-Tall grass\x1f0\x1f' + HAVEN_MARK_RGB.tall + '\x1f0\x1f0\x1f1');
    for (const a of havenBghBurntAreas) {
      const secs = a.deadline > 0 ? Math.max(0, Math.ceil((a.deadline - tick) * 0.6)) : -1;
      const lbl = '-Burnt grass' + (secs >= 0 ? '\n' + secs + 's' : '');   // pill shows ONLY the countdown
      for (const m of (a.members || []))
        recs.push(m.x + '\x1f' + m.y + '\x1f' + a.plane + '\x1f' + lbl + '\x1f0\x1f' + HAVEN_MARK_RGB.burnt + '\x1f0\x1f0\x1f1');
    }
    try { bridge().guideMarks(myPid(), recs.join('\x1e')); } catch (e) {}
  }

  // ---- Bound (stunned by the creature) -> big screen-centre countdown ----
  // The debuff bar (group 291) shows "Bound" with sprite 14392 and a seconds timer; while it is
  // active, centerText draws "Bound - Ns" mid-screen.
  const HAVEN_BOUND_SPRITE = 14392;
  let havenBghBoundShown = false;
  async function havenBghBoundTick() {
    if (!bridge().centerText) return;   // host without the overlay channel yet
    if (havenBghTrigOn) return;         // the trigger-the-trap alert owns the centre slot
    let d = null;
    try { d = JSON.parse((await bridge().buffs(myPid())) || 'null'); } catch (e) {}
    const list = (d && d.debuffs) || [];
    const b = list.find(x => (x.sprite | 0) === HAVEN_BOUND_SPRITE || /^bound$/i.test(x.name || ''));
    if (b) {
      const secs = (b.secs | 0) > 0 ? (b.secs | 0) : (parseInt(b.timer, 10) || 0);
      try { bridge().centerText(myPid(), 'Bound' + (secs > 0 ? ' - ' + secs + 's' : '')); } catch (e) {}
      havenBghBoundShown = true;
    } else if (havenBghBoundShown) {
      havenBghBoundShown = false;
      try { bridge().centerText(myPid(), ''); } catch (e) {}
    }
  }

  // Per colour: moth = jar moth, poison = extracted poison, spikes = poisoned grenwall spikes,
  // trap = armed poisoned spike trap.
  const HAVEN_POISON = {
    green:  { moth: 60749, mothName: 'Verdite moth',   poison: 60758, spikes: 60752, trap: 60757 },
    blue:   { moth: 60750, mothName: 'Lazurite moth',  poison: 60759, spikes: 60753, trap: 60755 },
    purple: { moth: 60751, mothName: 'Purpurite moth', poison: 60760, spikes: 60754, trap: 60756 },
  };
  const HAVEN_COLOUR_HEX = { green: '#5bd75b', blue: '#5bc8ff', purple: '#c58bff' };

  // ---- Poison discovery: identify the critical (ideal) moth colour ----
  // A trap fire drops boss HP (varbit 53292) ~30000 for the critical colour, ~15000/~7500
  // otherwise. The baited colour is read from the moth GROUND ITEM on the trap tile. The HP fall
  // accumulates as one cycle and commits when stable: >=22500 = ideal, smaller = eliminated.
  // Reset on HP rise (new creature).
  const HAVEN_MOTH_TO_COLOUR = {};
  for (const _c in HAVEN_POISON) HAVEN_MOTH_TO_COLOUR[HAVEN_POISON[_c].moth] = _c;
  let havenBghPrevHp = null, havenBghCycleAccum = 0, havenBghStable = 0, havenBghCycleActive = false;
  let havenBghLastBait = null;   // colour last seen baited on an armed trap (attribution target)
  // Per-colour tier from the trap-fire drop: 'best' (30000), 'normal' (15000), 'resistant' (7500),
  // null (untested).
  let havenBghDisc = { tier: { green: null, blue: null, purple: null } };
  function havenBghResetDisc() {
    havenBghDisc = { tier: { green: null, blue: null, purple: null } };
    havenBghLastBait = null; havenBghCycleAccum = 0; havenBghStable = 0; havenBghCycleActive = false;
  }
  // Best colour: the one measured 'best', or the sole untested colour by elimination (exactly one
  // colour is best per encounter).
  function havenBghActiveColour() {
    const t = havenBghDisc.tier;
    for (const c of ['green', 'blue', 'purple']) if (t[c] === 'best') return c;
    const untested = ['green', 'blue', 'purple'].filter(c => !t[c]);
    return untested.length === 1 ? untested[0] : null;
  }
  function havenBghCommitCycle(drop) {
    const col = havenBghLastBait;
    if (!col || drop < 4000) return;              // no known bait colour / just noise
    // 7500 = resistant, 15000 = normal, 30000 = best (thresholds at the midpoints).
    havenBghDisc.tier[col] = drop >= 22500 ? 'best' : drop >= 11250 ? 'normal' : 'resistant';
  }
  async function havenBghTrackDiscovery() {
    const hp = havenBghData ? (havenBghData.bossHp | 0) : 0;
    if (hp <= 0) { havenBghPrevHp = hp; return; }   // no active creature -> just track the baseline

    // Remember which colour is on an armed trap tile (read before the fire consumes it).
    await havenBghUpdateScene();
    let ground = [];
    try { if (bridge().groundItems) ground = JSON.parse((await bridge().groundItems(myPid())) || '[]'); } catch (e) {}
    if (Array.isArray(ground) && ground.length) {
      const traps = havenBghSceneObjs.filter(o => o.id === HAVEN_ARMED.id || HAVEN_ARMED.re.test(o.name || ''));
      for (const g of ground) {
        const col = HAVEN_MOTH_TO_COLOUR[g.id];
        // Proximity, not tile equality: the trap's x/y is its SW anchor while the baited moth lands
        // elsewhere in the footprint, so exact matching misses baits.
        if (col && traps.some(t => Math.max(Math.abs(t.x - g.x), Math.abs(t.y - g.y)) <= 3)) { havenBghLastBait = col; break; }
      }
    }

    // HP-drop cycle: accumulate while falling, commit when stable. A rise = NEW CREATURE, but the
    // discovered tiers CARRY OVER -- the optimal poison only rerolls when the main (middle box) trap
    // relocates, so a new creature resets only the per-fire cycle bookkeeping.
    if (havenBghPrevHp != null) {
      if (hp > havenBghPrevHp) { havenBghLastBait = null; havenBghCycleAccum = 0; havenBghStable = 0; havenBghCycleActive = false; }
      else if (hp < havenBghPrevHp) { havenBghCycleAccum += (havenBghPrevHp - hp); havenBghCycleActive = true; havenBghStable = 0; }
      else if (havenBghCycleActive && ++havenBghStable >= 2) { havenBghCommitCycle(havenBghCycleAccum); havenBghCycleActive = false; havenBghCycleAccum = 0; havenBghStable = 0; }
    }
    havenBghPrevHp = hp;
  }

  // Refresh the cached scene objects (guide-only cost). Range 64 is the reader's max: at 40 the
  // Broken trap / Log Pile are out of range when joining at the pen entrance.
  async function havenBghUpdateScene() {
    if (!bridge() || !bridge().sceneEntities) return;
    try {
      const sc = JSON.parse((await bridge().sceneEntities(myPid(), 64)) || '{}');
      havenBghSceneObjs = (sc && Array.isArray(sc.objects)) ? sc.objects : [];
      havenBghSceneNpcs = (sc && Array.isArray(sc.npcs)) ? sc.npcs : [];
      havenBghMainTrapTick();
    } catch (e) { /* keep last */ }
  }
  // The MAIN (middle box) trap relocates exactly when the encounter rerolls the optimal poison:
  // same coordinates = the ideal colour carried over. So the discovered tiers persist across
  // creatures and reset ONLY when this trap moves.
  let havenBghMainTrapPos = null;
  function havenBghMainTrapTick() {
    let best = null;
    for (const o of havenBghSceneObjs) {
      const nm = (o && o.name) ? String(o.name) : '';
      // "Broken trap" / its built morph -- names end in "trap" but exclude the log-trap family.
      if (!/trap$/i.test(nm) || /log/i.test(nm)) continue;
      if (!best || (o.dist | 0) < (best.dist | 0)) best = o;
    }
    if (!best) return;                     // out of range -> keep the last known anchor
    if (havenBghMainTrapPos && (havenBghMainTrapPos.x !== best.x || havenBghMainTrapPos.y !== best.y)) {
      havenBghResetDisc();                 // trap moved -> the optimal poison rerolled
    }
    havenBghMainTrapPos = { x: best.x, y: best.y };
  }
  // Nearest matching scene object to the player (by loc id, or name-regex fallback); null if none.
  function havenBghNearestObj(id, re) {
    let best = null, bd = Infinity;
    for (const o of havenBghSceneObjs) {
      const match = (id && o.id === id) || (re && re.test(o.name || ''));
      if (!match) continue;
      const d = (typeof o.dist === 'number') ? o.dist : 0;
      if (d < bd) { bd = d; best = o; }
    }
    return best;
  }
  // Nearest matching scene object to a GIVEN tile (Chebyshev), e.g. the Tall grass next to a
  // specific armed trap rather than the one nearest the player.
  function havenBghNearestObjToTile(id, re, tx, ty) {
    let best = null, bd = Infinity;
    for (const o of havenBghSceneObjs) {
      const match = (id && o.id === id) || (re && re.test(o.name || ''));
      if (!match) continue;
      const d = Math.max(Math.abs(o.x - tx), Math.abs(o.y - ty));
      if (d < bd) { bd = d; best = o; }
    }
    return best;
  }

  async function havenBghGuideStep() {
    await havenBghUpdateScene();
    havenBghHazardTick();                            // bones / tall grass / burnt grass from this scene pass
    havenBghBoundTick().catch(function () {});       // stun countdown (independent of the step)
    // Scene warm-up gate: the pen is an instance, so its objects arrive via the companion spawn feed
    // or as they come into range. Until ANY pen-signature object is visible, guessing a step is
    // worse than saying so.
    const penReady = havenBghSceneObjs.some(o => {
      const nm = (o && o.name) ? String(o.name) : '';
      return o.id === HAVEN_TRAP.brokenId || HAVEN_TRAP.brokenRe.test(nm)
          || o.id === HAVEN_LOGTRAP.unbuiltId || o.id === HAVEN_LOGTRAP.setId || o.id === HAVEN_ARMED.id
          || o.id === HAVEN_GATHER.logs.id || o.id === HAVEN_GATHER.rope.id
          || HAVEN_TREES[o.id] || /^hollow tree$/i.test(nm);
    });
    if (!penReady) {
      havenBghSetMarks([]);
      havenBghGuidePhase = 'Scanning';
      havenBghGuideMsg = 'Waiting for the encounter objects to load (move toward the pen centre if this persists)...';
      if (activeTab === 'havenbgh') renderHavenBgh();
      return;
    }
    let inv = [];
    try { const iv = JSON.parse((await PLUGIN_API['state.inventory'].run([], myPid())) || '{}'); if (iv && Array.isArray(iv.items)) inv = iv.items; } catch (e) {}
    const count = (id) => { let n = 0; for (const it of inv) if (it[1] === id) n += (it[2] > 0 ? it[2] : 1); return n; };

    const logs = count(HAVEN_GATHER.logs.item), rope = count(HAVEN_GATHER.rope.item);
    const needLogs = logs < HAVEN_GATHER.logs.need, needRope = rope < HAVEN_GATHER.rope.need;
    // Broken trap present -> not built yet; stops step 1 re-triggering after the build consumes materials.
    const brokenTrap = havenBghNearestObj(HAVEN_TRAP.brokenId, HAVEN_TRAP.brokenRe);

    if (brokenTrap && (needLogs || needRope)) {
      // Box the nearest instance of each still-needed object (closest to the player).
      const tiles = [];
      if (needLogs) { const o = havenBghNearestObj(HAVEN_GATHER.logs.id, HAVEN_GATHER.logs.re); if (o) tiles.push({ x: o.x, y: o.y, plane: o.plane || 0, label: 'Log Pile\nTake giant logs (' + logs + '/' + HAVEN_GATHER.logs.need + ')' }); }
      if (needRope) { const o = havenBghNearestObj(HAVEN_GATHER.rope.id, HAVEN_GATHER.rope.re); if (o) tiles.push({ x: o.x, y: o.y, plane: o.plane || 0, label: 'Rope\nTake rope (' + rope + '/' + HAVEN_GATHER.rope.need + ')' }); }
      havenBghSetMarks(tiles);
      havenBghGuidePhase = 'Gather';
      havenBghGuideMsg = 'Gather: Take-1 the Log Pile (' + logs + '/4) and Rope (' + rope + '/5). '
                       + 'Click each REPEATEDLY -- one click = one item, so keep clicking; don\'t click once and wait.';
      if (activeTab === 'havenbgh') renderHavenBgh();
      return;
    }

    if (brokenTrap) {
      havenBghSetMarks([{ x: brokenTrap.x, y: brokenTrap.y, plane: brokenTrap.plane || 0, label: 'Broken trap\nBuild the trap' }]);
      havenBghGuidePhase = 'Build';
      havenBghGuideMsg = 'Build the trap: use Build on the Broken trap (uses 1 log + 1 rope).';
      if (activeTab === 'havenbgh') renderHavenBgh();
      return;
    }

    // SKIN: the dead creature is on the ground ("Dead giant ..." with a Skin action) -> harvest it.
    // Top in-pen priority.
    {
      let dead = null;
      for (const o of havenBghSceneObjs) {
        if (!/^dead giant /i.test(o.name || '')) continue;
        const acts = Array.isArray(o.actions) ? o.actions : [];
        if (acts.some(a => /^skin/i.test(a || ''))) { dead = o; break; }
      }
      if (dead) {
        havenBghSetMarks([{ x: dead.x, y: dead.y, plane: dead.plane | 0, label: dead.name + '\nSkin' }]);
        havenBghGuidePhase = 'Skin';
        havenBghGuideMsg = 'Kill complete -- Skin the ' + dead.name.replace(/^dead /i, '').toLowerCase() + '!';
        if (activeTab === 'havenbgh') renderHavenBgh();
        return;
      }
    }

    // KILL: the creature is ACTIVE (its NPC is in scene) with HP varbit 53292 at 0 -> subdued; the
    // Giant box trap's "Bait" action is the finisher. Without the creature present, the same "Bait"
    // action means "(re)start the encounter", NOT the kill, so gating on the action alone false-fires.
    {
      const hpNow = havenBghData ? (havenBghData.bossHp | 0) : 0;
      if (hpNow === 0 && havenBghFindCreature()) {
        let kill = null;
        for (const o of havenBghSceneObjs) {
          if (!/^giant box trap$/i.test(o.name || '')) continue;
          const acts = Array.isArray(o.actions) ? o.actions : [];
          if (acts.some(a => /^bait/i.test(a || ''))) { kill = o; break; }
        }
        if (kill) {
          havenBghSetMarks([{ x: kill.x, y: kill.y, plane: kill.plane | 0, label: 'Giant box trap\nBait to Kill' }]);
          havenBghGuidePhase = 'Kill';
          havenBghGuideMsg = 'The creature is subdued -- use "Bait to Kill" on the Giant box trap!';
          if (activeTab === 'havenbgh') renderHavenBgh();
          return;
        }
      }
    }

    // ACTIVE HUNT: a poison moth lies by an armed trap -> the bait is set. Show WHICH colour is on
    // the trap, and the moment the hunted creature stands on the bait (Chebyshev <= 1, the trigger
    // window) fire a sound + centre-screen alert. This pre-empts the moth/trap steps.
    {
      let ground = [];
      try { const gj = JSON.parse((await bridge().groundItems(myPid())) || '[]'); if (Array.isArray(gj)) ground = gj; } catch (e) {}
      const armedObjs = havenBghSceneObjs.filter(o => o.id === HAVEN_ARMED.id || HAVEN_ARMED.re.test(o.name || ''));
      let bait = null;
      for (const g of ground) {
        const col = HAVEN_MOTH_TO_COLOUR[g.id];
        if (!col) continue;
        const tr = armedObjs.find(t => Math.max(Math.abs(t.x - g.x), Math.abs(t.y - g.y)) <= 3);
        if (tr) { bait = { col: col, x: g.x, y: g.y, trap: tr }; break; }
      }
      if (bait) {
        const crea = havenBghFindCreature();
        const onBait = !!(crea && Math.max(Math.abs(crea.x - bait.x), Math.abs(crea.y - bait.y)) <= 1);
        const wasOn = havenBghTrigOn;
        havenBghTrigOn = onBait;
        if (onBait) {
          if (!wasOn) {   // edge-trigger the audibles; the text holds while it stands there
            try { bridge().playSound('alert 5'); } catch (e) {}
            try { qgOv('overlay.flashGame'); } catch (e) {}
          }
          try { if (bridge().centerText) bridge().centerText(myPid(), 'TRIGGER THE TRAP!'); } catch (e) {}
        } else if (wasOn) {
          try { if (bridge().centerText) bridge().centerText(myPid(), ''); } catch (e) {}   // clear only what this panel set
        }
        havenBghSetMarks([{ x: bait.trap.x, y: bait.trap.y, plane: bait.trap.plane || 0, label: 'Log trap (armed)\nBaited: ' + bait.col }]);
        havenBghGuidePhase = 'Hunt';
        havenBghGuideMsg = onBait
          ? 'NOW -- trigger the trap!'
          : ('Trap baited with ' + bait.col + '. Stay hidden and wait -- the alert fires the moment the creature is on the bait.');
        if (activeTab === 'havenbgh') renderHavenBgh();
        return;
      }
      if (havenBghTrigOn) {   // bait gone (fired / despawned) -> release the alert slot
        havenBghTrigOn = false;
        try { if (bridge().centerText) bridge().centerText(myPid(), ''); } catch (e) {}
      }
    }

    // Step 3: catch moths (Search the hollow trees around the arena edge). A search spawns 3x of
    // each colour (the Azurite buff removes 1 colour). Need one each of TWO colours normally, any
    // ONE moth with Azurite active. Moths despawn fast.
    const moth = { green: count(HAVEN_POISON.green.moth), blue: count(HAVEN_POISON.blue.moth), purple: count(HAVEN_POISON.purple.moth) };
    const distinctColours = (moth.green > 0 ? 1 : 0) + (moth.blue > 0 ? 1 : 0) + (moth.purple > 0 ? 1 : 0);
    const totalMoths = moth.green + moth.blue + moth.purple;
    const azuriteActive = !!(havenBghData && havenBghData.buffs && (havenBghData.buffs[60870] | 0) > 0);
    // DYNAMIC requirements from the live fight: once the ideal colour is known each fire does 30k,
    // so fires-left = ceil(HP / 30k) drives BOTH the moths to hold and the traps to arm, recomputed
    // every tick as the HP falls. Ideal unknown -> discovery needs (two colours to test, one with
    // Azurite). Moth holdings cap at 3 (a search yields 3).
    const liveHp = havenBghData ? (havenBghData.bossHp | 0) : 0;
    const idealCol = havenBghActiveColour();
    const firesLeft = liveHp > 0 ? Math.ceil(liveHp / 30000) : 0;
    const mothTarget = Math.min(Math.max(firesLeft, 1), 3);
    const mothsDone = idealCol
      ? count(HAVEN_POISON[idealCol].moth) >= mothTarget
      : (azuriteActive ? (totalMoths >= 1) : (distinctColours >= 2));
    if (mothsDone && havenBghMothHl) { havenBghMothHl = false; qgClrNpc(); }   // step passed -> drop the moth boxes
    if (!mothsDone) {
      // Spawned moths FIRST: searching a tree spawns them as NPCS that flutter around and despawn
      // fast. Box every live "... moth" by NAME through the NPC-highlight channel -- it tracks the
      // moving models, which tile marks cannot.
      const mothNames = [];
      for (const n of havenBghSceneNpcs) {
        const nm = (n && n.name) ? String(n.name) : '';
        if ((HAVEN_MOTH_NPC_IDS[n && n.id] || /moth$/i.test(nm)) && nm && mothNames.indexOf(nm) < 0) mothNames.push(nm);
      }
      if (mothNames.length) {
        // Ideal colour known -> box ONLY that colour's moths.
        let wantNames = mothNames;
        if (idealCol && mothNames.indexOf(HAVEN_POISON[idealCol].mothName) >= 0) wantNames = [HAVEN_POISON[idealCol].mothName];
        qgOv('overlay.highlight', wantNames.slice(0, 6));
        havenBghMothHl = true;
        havenBghSetMarks([]);                       // hazard marks stay; the tree pointer drops
        havenBghGuidePhase = 'Moths';
        havenBghGuideMsg = idealCol
          ? ('Moths are out -- catch the ' + idealCol + ' ones (' + HAVEN_POISON[idealCol].mothName + '): '
             + count(HAVEN_POISON[idealCol].moth) + '/' + mothTarget + ' (' + firesLeft + ' fires x 30k covers the ' + liveHp.toLocaleString() + ' HP).')
          : (azuriteActive
            ? ('Moths are out (' + mothNames.join(', ') + ') -- catch ANY ONE, QUICKLY (' + totalMoths + '/1). Azurite is active, so one colour is removed.')
            : ('Moths are out (' + mothNames.join(', ') + ') -- catch them QUICKLY, one each of TWO different colours (' + distinctColours + '/2 colours).'));
        if (activeTab === 'havenbgh') renderHavenBgh();
        return;
      }
      if (havenBghMothHl) { havenBghMothHl = false; qgClrNpc(); }   // they despawned -> back to the tree
      // No moths out -> search a Hollow tree that is ITSELF searchable: each tree's own varbit
      // (HAVEN_TREES) reads 1 while on cooldown, 0 when searchable again. Nearest eligible wins;
      // name-match fallback only if no id-mapped tree is in scene.
      const trees = (havenBghData && havenBghData.trees) || {};
      const searchable = HAVEN_TREE_VBS.filter(v => (trees[v] | 0) === 0).length;
      let tree = null, treeD = Infinity;
      for (const o of havenBghSceneObjs) {
        const tvb = HAVEN_TREES[o.id];
        if (!tvb || (trees[tvb] | 0) !== 0) continue;
        const d = (typeof o.dist === 'number') ? o.dist : 0;
        if (d < treeD) { treeD = d; tree = o; }
      }
      if (!tree && searchable > 0) tree = havenBghNearestObj(0, /^hollow tree$/i);
      const tiles = tree ? [{ x: tree.x, y: tree.y, plane: tree.plane || 0, label: 'Hollow tree\nSearch for moths' }] : [];
      havenBghSetMarks(tiles);
      havenBghGuidePhase = 'Moths';
      havenBghGuideMsg = searchable === 0
        ? 'All Hollow trees were just searched -- wait for them to reset, then search again.'
        : (idealCol
          ? ('Search a Hollow tree (' + searchable + '/3 searchable) -- catch ' + mothTarget + ' ' + idealCol + ' moth' + (mothTarget === 1 ? '' : 's')
             + ' (' + firesLeft + ' fires x 30k covers the ' + liveHp.toLocaleString() + ' HP).')
          : (azuriteActive
            ? ('Search a Hollow tree (' + searchable + '/3 searchable) -- moths despawn fast, so be ready. Azurite is active: any ONE moth counts (' + totalMoths + '/1).')
            : ('Search a Hollow tree (' + searchable + '/3 searchable) -- moths despawn fast, so be ready. You need TWO different colours (' + distinctColours + '/2).')));
      if (activeTab === 'havenbgh') renderHavenBgh();
      return;
    }

    // Step 4: build + arm the log traps. Spot state via varbits 60768-60771 (1 default / 2 built /
    // 3 armed) is the done-gate. Target is DYNAMIC: ideal colour known -> one trap per 30k fire
    // still needed; ideal unknown -> the discovery count (2, or 1 with Azurite). Only 4 spots exist.
    const trapTarget = (idealCol && firesLeft > 0) ? Math.min(4, firesLeft) : (azuriteActive ? 1 : 2);
    const traps = (havenBghData && havenBghData.traps) || {};
    let armedTraps = 0, builtUnarmed = 0;
    for (const v of HAVEN_LOGTRAP.spots) { const s = traps[v] | 0; if (s === 3) armedTraps++; else if (s === 2) builtUnarmed++; }
    if (armedTraps < trapTarget) {
      // Materials FIRST, for the WHOLE remaining job so it is one gathering trip: every still-unbuilt
      // trap costs 1 log + 1 rope to Build, and every non-armed trap 1 rope to Set.
      const buildsNeeded = Math.max(0, trapTarget - armedTraps - builtUnarmed);
      const setsNeeded   = Math.max(0, trapTarget - armedTraps);
      const needL = buildsNeeded, needR = buildsNeeded + setsNeeded;
      if (logs < needL || rope < needR) {
        const tiles = [];
        if (logs < needL) { const o = havenBghNearestObj(HAVEN_GATHER.logs.id, HAVEN_GATHER.logs.re); if (o) tiles.push({ x: o.x, y: o.y, plane: o.plane || 0, label: 'Log Pile\nTake giant logs (' + logs + '/' + needL + ')' }); }
        if (rope < needR) { const o = havenBghNearestObj(HAVEN_GATHER.rope.id, HAVEN_GATHER.rope.re); if (o) tiles.push({ x: o.x, y: o.y, plane: o.plane || 0, label: 'Rope\nTake rope (' + rope + '/' + needR + ')' }); }
        havenBghSetMarks(tiles);
        havenBghGuidePhase = 'Gather';
        havenBghGuideMsg = 'Not enough materials for the log trap: need ' + needL + ' log + ' + needR + ' rope, have '
                         + logs + ' log / ' + rope + ' rope. Take-1 the Log Pile and Rope repeatedly.';
        if (activeTab === 'havenbgh') renderHavenBgh();
        return;
      }
      if (builtUnarmed > 0) {
        // A trap is built but not armed -> Set the nearest Log trap (1 rope).
        const o = havenBghNearestObj(HAVEN_LOGTRAP.setId, HAVEN_LOGTRAP.setRe);
        const tiles = o ? [{ x: o.x, y: o.y, plane: o.plane || 0, label: 'Log trap\nSet (arm) -- uses 1 rope' }] : [];
        havenBghSetMarks(tiles);
        havenBghGuidePhase = 'Arm trap';
        havenBghGuideMsg = 'Set the Log trap to arm it (uses 1 rope). Traps armed: ' + armedTraps + '/' + trapTarget + '.';
      } else {
        // Build a new trap on the nearest Unbuilt log trap (1 log + 1 rope), then Set it (1 rope).
        const o = havenBghNearestObj(HAVEN_LOGTRAP.unbuiltId, HAVEN_LOGTRAP.unbuiltRe);
        const tiles = o ? [{ x: o.x, y: o.y, plane: o.plane || 0, label: 'Unbuilt log trap\nBuild -- uses 1 log + 1 rope' }] : [];
        havenBghSetMarks(tiles);
        havenBghGuidePhase = 'Build trap';
        havenBghGuideMsg = 'Build a log trap: Build the Unbuilt log trap (1 log + 1 rope), then Set it (1 rope). Traps armed: ' + armedTraps + '/' + trapTarget + '.';
      }
      if (activeTab === 'havenbgh') renderHavenBgh();
      return;
    }

    // Step 5: hide in the tall grass by the closest armed trap, then bait it with a moth.
    const armedTrap = havenBghNearestObj(HAVEN_ARMED.id, HAVEN_ARMED.re);
    if (armedTrap) {
      // Hidden state from varc 779 (3593 hidden / 2698 visible).
      let varcs = {};
      if (bridge().varcsDumpAll) { try { varcs = JSON.parse((await bridge().varcsDumpAll(myPid())) || '{}'); } catch (e) {} }
      const hidden = (varcs[HAVEN_HIDE.varc] | 0) === HAVEN_HIDE.hiddenVal;

      if (!hidden) {
        // Mark the Tall grass next to THIS armed trap; wait until the player is hidden before baiting.
        const grass = havenBghNearestObjToTile(0, HAVEN_HIDE.grassRe, armedTrap.x, armedTrap.y);
        const tiles = grass ? [{ x: grass.x, y: grass.y, plane: grass.plane || 0, label: 'Tall grass\nHide here, then wait until hidden' }] : [];
        havenBghSetMarks(tiles);
        havenBghGuidePhase = 'Hide';
        havenBghGuideMsg = 'Hide in the Tall grass by the armed Log trap and wait until you are hidden before baiting.';
        if (activeTab === 'havenbgh') renderHavenBgh();
        return;
      }

      // Hidden -> bait the armed trap: prefer the discovered ideal colour if held, else the colour(s)
      // held (baiting an unknown colour IS the discovery cycle).
      const ideal = havenBghActiveColour();
      const haveIdeal = ideal && count(HAVEN_POISON[ideal].moth) > 0;
      let colourTxt;
      if (haveIdeal) colourTxt = ideal;
      else {
        // Suggest only UNTESTED held colours: a colour with a known tier adds no information and wastes
        // the bait. Every held colour already tested -> fall back to all held.
        const held = ['green', 'blue', 'purple'].filter(c => count(HAVEN_POISON[c].moth) > 0);
        const untestedHeld = held.filter(c => !(havenBghDisc.tier[c]));
        const pick = untestedHeld.length ? untestedHeld : held;
        colourTxt = pick.length ? pick.join(' / ') : 'the moth';
      }
      havenBghSetMarks([{ x: armedTrap.x, y: armedTrap.y, plane: armedTrap.plane || 0, label: 'Log trap (armed)\nBait with moth (' + colourTxt + ')' }]);
      havenBghGuidePhase = 'Bait';
      havenBghGuideMsg = 'Hidden -- bait the armed Log trap: "Bait with moth (' + colourTxt + ')"'
                       + (haveIdeal ? ' -- the ideal colour.' : ' (baiting an untested colour reveals the ideal one).');
      if (activeTab === 'havenbgh') renderHavenBgh();
      return;
    }

    havenBghSetMarks([]);   // hazard/area marks stay up between steps
    havenBghGuidePhase = 'Traps armed';
    havenBghGuideMsg = 'Log traps armed (' + armedTraps + '/' + trapTarget + '). Next steps not wired yet.';
    if (activeTab === 'havenbgh') renderHavenBgh();
  }

  function renderHavenBgh() {
    const c = $('content');
    let wrap = $('havenBghWrap');
    if (!wrap) {
      c.innerHTML = '';
      wrap = document.createElement('div'); wrap.id = 'havenBghWrap'; wrap.className = 'pane';
      // flex:0 0 auto so the list sizes to content and .content scrolls (matches the other panels).
      wrap.style.cssText = 'flex:0 0 auto;display:flex;flex-direction:column;gap:14px;padding:4px 2px;';
      c.appendChild(wrap); havenBghSig = '';
    }
    paintHavenBgh();
  }

  function havenBghSectionLabel(text) {
    const el = document.createElement('div');
    el.textContent = text;
    el.style.cssText = 'font-size:11px;text-transform:uppercase;letter-spacing:.5px;color:var(--text-dim);';
    return el;
  }
  // Buff time from 15s periods -> "m:ss" (or "Ns" under a minute). Steps by 15s.
  function havenFmtDur(secs) {
    if (secs <= 0) return '';
    const m = Math.floor(secs / 60), s = secs % 60;
    return m > 0 ? (m + ':' + String(s).padStart(2, '0')) : (s + 's');
  }

  function paintHavenBgh() {
    const wrap = $('havenBghWrap'); if (!wrap) return;
    const sig = JSON.stringify([havenBghData, havenBghGuideOn, havenBghGuidePhase, havenBghGuideMsg, havenBghDisc]);
    if (sig === havenBghSig) return;
    havenBghSig = sig;
    wrap.innerHTML = '';

    const gk = document.createElement('div');
    gk.style.cssText = 'display:flex;align-items:center;gap:8px;font-size:12px;color:var(--text-dim);cursor:pointer;';
    const gpill = document.createElement('div'); gpill.className = 'al-pill' + (havenBghGuideOn ? ' on' : '');
    gpill.appendChild(document.createElement('span'));
    gk.addEventListener('click', () => {
      havenBghGuideOn = !havenBghGuideOn;
      gpill.classList.toggle('on', havenBghGuideOn);
      try { localStorage.setItem('rtxHavenBghGuide', havenBghGuideOn ? '1' : '0'); } catch (e) {}
      if (!havenBghGuideOn) {
        try { qgOv('overlay.guideTiles', []); } catch (e) {}
        havenBghHazardReset();
        if (havenBghMothHl) { havenBghMothHl = false; qgClrNpc(); }
        if (havenBghBoundShown || havenBghTrigOn) {
          havenBghBoundShown = false; havenBghTrigOn = false;
          try { if (bridge().centerText) bridge().centerText(myPid(), ''); } catch (e) {}
        }
        havenBghGuideMsg = ''; havenBghGuidePhase = '';
      }
      havenBghSig = ''; if (activeTab === 'havenbgh') renderHavenBgh();
    });
    gk.appendChild(gpill); gk.appendChild(document.createTextNode('Guide me step-by-step'));
    wrap.appendChild(gk);

    if (havenBghGuideOn) {
      const card = document.createElement('div');
      card.style.cssText = 'padding:10px;border:1px solid var(--accent-lo);border-radius:8px;background:rgba(255,255,255,0.03);';
      const h = document.createElement('div'); h.style.cssText = 'font-size:11px;text-transform:uppercase;letter-spacing:.5px;color:var(--text-dim);margin-bottom:4px;';
      h.textContent = havenBghGuidePhase ? ('Step: ' + havenBghGuidePhase) : 'Guide';
      const m = document.createElement('div'); m.style.cssText = 'font-size:13px;line-height:1.5;';
      m.textContent = havenBghGuideMsg || 'Starting...';
      card.appendChild(h); card.appendChild(m);
      wrap.appendChild(card);
    }

    // Status: boss HP (varbit 53292) + aggression counter (varp 10947) + supplies box (varbit 60866).
    wrap.appendChild(havenBghSectionLabel('Status'));
    {
      const bossHp = havenBghData ? (havenBghData.bossHp | 0) : 0;
      const hrow = document.createElement('div');
      hrow.style.cssText = 'display:flex;justify-content:space-between;align-items:center;padding:8px 10px;border:1px solid var(--border);border-radius:8px;background:rgba(255,255,255,0.03);' + (bossHp > 0 ? '' : 'opacity:0.55;');
      const hl = document.createElement('span'); hl.textContent = 'Creature HP'; hl.style.cssText = 'font-size:13px;';
      const hv = document.createElement('span'); hv.textContent = bossHp > 0 ? bossHp.toLocaleString() : '-';
      hv.style.cssText = 'font-size:12px;font-weight:600;color:' + (bossHp > 0 ? 'var(--accent-hi)' : 'var(--text-dim)') + ';font-variant-numeric:tabular-nums;';
      hrow.appendChild(hl); hrow.appendChild(hv);
      wrap.appendChild(hrow);

      const aggro = havenBghData ? (havenBghData.aggro | 0) : 0;
      const arow = document.createElement('div');
      arow.style.cssText = 'display:flex;justify-content:space-between;align-items:center;padding:8px 10px;border:1px solid var(--border);border-radius:8px;background:rgba(255,255,255,0.03);';
      const al = document.createElement('span'); al.textContent = 'Aggression'; al.style.cssText = 'font-size:13px;';
      const av = document.createElement('span'); av.textContent = String(aggro);
      av.style.cssText = 'font-size:12px;font-weight:600;color:var(--accent-hi);font-variant-numeric:tabular-nums;';
      arow.appendChild(al); arow.appendChild(av);
      wrap.appendChild(arow);

      const access = !!(havenBghData && havenBghData.boxAccess);
      const brow = document.createElement('div');
      brow.style.cssText = 'display:flex;justify-content:space-between;align-items:center;padding:8px 10px;border:1px solid var(--border);border-radius:8px;background:rgba(255,255,255,0.03);';
      const bl = document.createElement('span'); bl.textContent = "Fox's hunting supplies box"; bl.style.cssText = 'font-size:13px;';
      const bv = document.createElement('span'); bv.textContent = access ? 'Access' : 'No access';
      bv.style.cssText = 'font-size:12px;font-weight:600;color:' + (access ? 'var(--accent-hi)' : 'var(--text-dim)') + ';';
      brow.appendChild(bl); brow.appendChild(bv);
      wrap.appendChild(brow);
    }

    wrap.appendChild(havenBghSectionLabel('Poison'));
    {
      const tier = (havenBghDisc && havenBghDisc.tier) || {};
      const best = havenBghActiveColour();
      const cap = s => s.charAt(0).toUpperCase() + s.slice(1);
      const row = document.createElement('div');
      row.style.cssText = 'display:flex;justify-content:space-between;align-items:center;padding:8px 10px;border:1px solid var(--border);border-radius:8px;background:rgba(255,255,255,0.03);';
      const l = document.createElement('span'); l.textContent = 'Ideal poison'; l.style.cssText = 'font-size:13px;';
      const v = document.createElement('span'); v.style.cssText = 'font-size:12px;font-weight:600;';
      if (best) { v.textContent = cap(best) + (tier[best] === 'best' ? '' : ' (by elimination)'); v.style.color = HAVEN_COLOUR_HEX[best] || 'var(--accent-hi)'; }
      else { v.textContent = 'discovering...'; v.style.color = 'var(--text-dim)'; }
      row.appendChild(l); row.appendChild(v);
      wrap.appendChild(row);
      const TIER = { best: '30k best', normal: '15k normal', resistant: '7.5k resist' };
      const trow = document.createElement('div'); trow.style.cssText = 'display:flex;gap:6px;';
      for (const c of ['green', 'blue', 'purple']) {
        const known = tier[c];
        const chip = document.createElement('div');
        chip.style.cssText = 'flex:1;display:flex;flex-direction:column;align-items:center;gap:3px;padding:6px 4px;border:1px solid ' + (known === 'best' ? (HAVEN_COLOUR_HEX[c]) : 'var(--border)') + ';border-radius:8px;background:rgba(255,255,255,0.03);' + (known ? '' : 'opacity:0.5;');
        const sw = document.createElement('span'); sw.style.cssText = 'width:10px;height:10px;border-radius:3px;background:' + (HAVEN_COLOUR_HEX[c] || '#888') + ';';
        const nm = document.createElement('span'); nm.textContent = cap(c); nm.style.cssText = 'font-size:11px;';
        const tl = document.createElement('span'); tl.textContent = known ? TIER[known] : 'untested';
        tl.style.cssText = 'font-size:10px;color:' + (known === 'best' ? HAVEN_COLOUR_HEX[c] : 'var(--text-dim)') + ';';
        chip.appendChild(sw); chip.appendChild(nm); chip.appendChild(tl);
        trow.appendChild(chip);
      }
      wrap.appendChild(trow);
    }

    // Moth-jar buffs: active buffs with remaining time (varbits 60867/60868/60870, 15s periods).
    wrap.appendChild(havenBghSectionLabel('Moth jar buffs'));
    {
      const buffs = (havenBghData && havenBghData.buffs) || {};
      for (const b of HAVEN_MOTH_BUFFS) {
        const periods = buffs[b.vb] | 0;
        const secs = periods * 15;
        const active = secs > 0;
        const row = document.createElement('div');
        row.style.cssText = 'display:flex;justify-content:space-between;align-items:center;gap:10px;padding:8px 10px;border:1px solid var(--border);border-radius:8px;background:rgba(255,255,255,0.03);' + (active ? '' : 'opacity:0.55;');
        const left = document.createElement('div');
        const nm = document.createElement('div'); nm.textContent = b.name; nm.style.cssText = 'font-size:13px;';
        const ef = document.createElement('div'); ef.textContent = b.effect; ef.style.cssText = 'font-size:11px;color:var(--text-dim);margin-top:2px;';
        left.appendChild(nm); left.appendChild(ef);
        const t = document.createElement('span');
        t.textContent = active ? havenFmtDur(secs) : 'Inactive';
        t.style.cssText = 'font-size:12px;font-weight:600;white-space:nowrap;font-variant-numeric:tabular-nums;color:' + (active ? 'var(--accent-hi)' : 'var(--text-dim)') + ';';
        row.appendChild(left); row.appendChild(t);
        wrap.appendChild(row);
      }
    }

    wrap.appendChild(havenBghSectionLabel('Areas'));
    const areas = document.createElement('div');
    areas.style.cssText = 'display:flex;flex-direction:column;gap:8px;';
    for (const a of HAVEN_BGH_AREAS) {
      const row = document.createElement('div');
      row.style.cssText = 'display:flex;justify-content:space-between;align-items:center;padding:8px 10px;border:1px solid var(--border);border-radius:8px;background:rgba(255,255,255,0.03);';
      const l = document.createElement('span'); l.textContent = a.name; l.style.cssText = 'font-size:13px;';
      const r = document.createElement('span');
      r.textContent = '(' + a.x + ', ' + a.y + ')';
      r.style.cssText = 'font-size:12px;color:var(--text-dim);font-variant-numeric:tabular-nums;';
      row.appendChild(l); row.appendChild(r);
      areas.appendChild(row);
    }
    wrap.appendChild(areas);

    wrap.appendChild(havenBghSectionLabel('Bait by creature'));
    const list = document.createElement('div');
    list.style.cssText = 'display:flex;flex-direction:column;gap:4px;';
    const rows = Object.values(HAVEN_BGH_CREATURES).sort((a, b) => a.tier - b.tier || a.name.localeCompare(b.name));
    for (const cr of rows) {
      const row = document.createElement('div');
      row.style.cssText = 'display:grid;grid-template-columns:auto 1fr auto;gap:8px;align-items:center;padding:6px 10px;border:1px solid var(--border);border-radius:8px;';
      const badge = document.createElement('div'); badge.textContent = 'T' + cr.tier;
      badge.style.cssText = 'font-size:11px;font-weight:600;padding:2px 7px;border-radius:6px;background:var(--accent-lo);color:#fff;';
      const nm = document.createElement('div'); nm.textContent = cr.name; nm.style.cssText = 'font-size:13px;';
      const bait = document.createElement('div'); bait.textContent = cr.bait[1];
      bait.style.cssText = 'font-size:12px;color:var(--text-dim);white-space:nowrap;';
      row.appendChild(badge); row.appendChild(nm); row.appendChild(bait);
      list.appendChild(row);
    }
    wrap.appendChild(list);
  }
