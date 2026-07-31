// RuneToolsX panel: Player-Owned Ports (voyages, ships, visitors, resources, buildings + alerts).
// Spliced inline into client.html at load; bare classic script sharing its one global scope.
// Var ids, enums and formulas mirror the game's own CS2 scripts (7353 ships, 7357 visitors,
// 7337 buildings, 7359 stats, 4187 daily clock, 4223 regions; region names enum 7156, scroll
// names enum 603 in vb-17497 index order per script7372).

  const PP_RES = [   // [name, varp, sprite]
    ['Chimes', 3441, 15673], ['Bamboo', 3442, 15674], ['Gunpowder', 3443, 15677],
    ['Black slate', 3444, 15675], ['Cherrywood', 3445, 15678], ['Jade', 3446, 15679],
    ['Stainless steel', 3447, 15676], ['Terracotta', 4038, 21916], ['Azure', 4039, 21917]];
  const PP_TRADE = [   // [name, varbit, itemId]
    ['Ancient bones', 17415, 26317], ['Plate', 17416, 26320], ['Spices', 17417, 26318],
    ['Lacquer', 17418, 26319], ['Chi', 17419, 26321], ['Pearls', 21334, 30566],
    ['Koi scales', 21335, 30567]];
  // [name, stateVarbit, storiesVarbit, storiesAddonVarbit, cluesVarbit|0] in the engine's fixed
  // index order 1-12. Stories = base + group add-on; only the six newer adventurers have clues.
  const PP_VISITORS = [
    ['Whaler', 16995, 16994, 17019, 26788], ['Biologist', 16999, 16998, 17006, 0],
    ['Occultist', 17003, 17002, 17019, 0],  ['Assassin', 17008, 17007, 17019, 0],
    ['Convict', 17012, 17011, 17006, 0],    ['Missionary', 17016, 17015, 17006, 0],
    ['Trapper', 21307, 21306, 21318, 26787], ['Chef', 21311, 21310, 21318, 26786],
    ['Architect', 21315, 21314, 21318, 0],  ['Exile', 26836, 26835, 26847, 26789],
    ['Memory', 26840, 26839, 26847, 26790], ['Tengu', 26844, 26843, 26847, 26791]];
  // Scroll-piece counters, summed as the game's "Scroll pieces found" stat.
  const PP_SCROLLS = (() => {
    const a = [];
    for (let i = 17465; i <= 17474; i++) a.push(i);
    for (let i = 17477; i <= 17486; i++) a.push(i);
    for (let i = 17488; i <= 17494; i++) a.push(i);
    a.push(21319, 21320, 21321, 26782, 26783, 26784, 26785);
    return a;
  })();
  // "Port upgrades" tier tables (level -> tier, +0 past the end).
  const PP_UPG_TIERS = [
    [26825, [1, 2, 3, 4, 5, 5, 5, 6, 7, 7, 7]],   // Bar
    [26828, [1, 2, 3, 4, 5, 5, 5, 6, 6, 6]],      // Shipwright
    [26829, [1, 2, 3, 4, 5, 6, 7, 8, 9]],         // Warehouse
    [17398, [1, 2, 3, 4]],                        // Office
    [17399, [1, 2, 3, 4, 5]],                     // Workshop
    [26830, [1, 2, 3, 4, 5, 5, 5, 6, 6, 6]]];     // Lodgings
  // "Ship parts unlocked" bitmasks: varps 3378/3379 (bits 0-31) + vb 17033/17034 (bits 0-15).
  const PP_PART_VBS = [17033, 17034];
  const PP_RANKS = [[400, "Cabin Boy/Girl"], [800, "Bo'sun"], [1200, 'First Officer'],
                    [1600, "Cap'n"], [2000, 'Commodore'], [3500, 'Admiral'],
                    [4500, 'Admiral of the Fleet'], [Infinity, 'Portmaster']];
  // [label, currentLevelVarbit, tierEnum]; the tier enum maps level -> a struct holding the tier
  // NAME (3118), BONUS TEXT (3125) and cost (3132/3133 + 3134/3135 = resource id + amount).
  const PP_BUILDINGS = [
    ['Bar', 26825, 7117], ['Office', 17398, 7118], ['Workshop', 17399, 7119],
    ['Lodgings', 26830, 7147], ['Shipwright', 26828, 7146], ['Warehouse', 26829, 7148],
    ['Portal', 17401, 7151],
    ['Totem 1', 17405, 7150], ['Totem 2', 17406, 7150], ['Totem 3', 17407, 7150], ['Totem 4', 17408, 7150],
    ['Icon 1', 21336, 7149], ['Icon 2', 21337, 7149], ['Icon 3', 21338, 7149]];
  // Per-ship varbits: [namePart x3, voyageId, returnedFlag, timerIdx]
  const PP_SHIPS = [
    { nm: [17040, 17041, 17042], voy: 17071, ret: 17072, idx: 17081 },
    { nm: [17048, 17049, 17050], voy: 17082, ret: 17083, idx: 17092 },
    { nm: [17056, 17057, 17058], voy: 17093, ret: 17094, idx: 17103 },
    { nm: [17064, 17065, 17066], voy: 17104, ret: 17105, idx: 17114 }];
  const PP_RETVBS = [16962, 16966, 16970, 16974, 16978, 16982, 16986, 16990];   // timer idx 1-8
  const PP_DAMAGED = { 32722: 1, 32723: 1, 32724: 1 };   // voyage-id sentinels = ship damaged
  // Zone-unlock thresholds of vb 17458 (cumulative distance explored), highest first.
  const PP_ZONES = [[4100000, 8], [2300000, 7], [1200000, 6], [450000, 5], [140000, 4], [40000, 3], [5000, 2]];

  // Equipped part per ship and slot (script1161): [slot label, catalog enum, vb per ship 1-4].
  // Declared BEFORE the PP_VB_IDS IIFE below, which reads it at script load.
  const PP_EQUIP = [
    ['Hull',   1010, [17035, 17043, 17051, 17059]],
    ['Deck 1', 1011, [17037, 17045, 17053, 17061]],
    ['Deck 2', 1011, [17038, 17046, 17054, 17062]],
    ['Rudder', 1012, [17036, 17044, 17052, 17060]],
    ['Ram',    1013, [17039, 17047, 17055, 17063]],
  ];
  const PP_VB_IDS = (() => {
    const ids = new Set([17121, 17115, 17116, 17117, 17118, 17119, 17120, 17423, 17421,
                         17457, 17495, 17458,
                         17126, 17127, 17128,   // viewed voyage: per-stat success % (script2022)
                         17129, 17130, 17132,   // viewed voyage: tab, slot, selected ship (script2022)
                         17080, 17091, 17102, 17113,   // per-ship ABOARD bitmasks (script7295)
                         17497,                 // focused Trader's scroll (index 1-34)
                         17220, 17224, 17225,   // captain for hire: name-enum picker + first/surname indices
                         17462, 17463,          // black market: offered resource id + stock
                         17461, 17420,          // crew rerolls available, +5-reroll tokens
                         21322, 21323, 21324,   // the Trader: offered good, stock, payment resource
                         33141]);               // Arc: chimes earned toward the 20k daily sale cap (live-validated 2026-07-30)

    for (const s of PP_SHIPS) { s.nm.forEach(x => ids.add(x)); ids.add(s.voy); ids.add(s.ret); ids.add(s.idx); }
    for (let k = 0; k < 5; k++) {   // ship captain k+1: name picker/first/surname + 4 traits
      const b = 13 * k;
      [17155 + b, 17159 + b, 17160 + b, 17148 + b, 17149 + b, 17150 + b, 17151 + b].forEach(x => ids.add(x));
    }
    PP_EQUIP.forEach(e => e[2].forEach(x => ids.add(x)));   // equipped-part indices per ship
    PP_RETVBS.forEach(x => ids.add(x));
    PP_TRADE.forEach(t => ids.add(t[1]));
    PP_VISITORS.forEach(v2 => { ids.add(v2[1]); ids.add(v2[2]); ids.add(v2[3]); if (v2[4]) ids.add(v2[4]); });
    PP_BUILDINGS.forEach(b => ids.add(b[1]));
    PP_SCROLLS.forEach(x => ids.add(x));
    PP_PART_VBS.forEach(x => ids.add(x));
    return Array.from(ids).join(',');
  })();
  // 3390 = the ship number the crew window is viewing (script6184; 0 = window closed).
  const PP_VP_IDS = PP_RES.map(r => r[1]).join(',') + ',3415,3378,3379,3380,3390';

  let ppVb = null, ppVp = null, ppFetching = false, _ppAt = 0, ppSig = '';
  let ppVoyEnum = null;                 // enum 1022: voyage id -> struct id
  const ppVoyNames = {};                // struct id -> resolved voyage info
  let ppNameEnums = null;
  let ppCapEnums = null;
  let ppIconEnum = null;
  // Voyage rewards: enum 2164 = reward type -> name (1-9 resources, 10 Port resources, 11-18
  // trade goods, 19-23 misc). Type 25 = an adventurer, has no entry in 2164, and its "amount"
  // is the index into enum 1626 rather than a quantity.
  let ppRewardEnum = null, ppAdvEnum = null;
  async function ppRewardTables() {
    if (!ppRewardEnum) { try { ppRewardEnum = JSON.parse(await bridge().enumInfo(2164) || 'null'); } catch (e) {} }
    if (!ppAdvEnum)    { try { ppAdvEnum    = JSON.parse(await bridge().enumInfo(1626) || 'null'); } catch (e) {} }
    return !!ppRewardEnum;
  }
  function ppRewardText(t, a) {
    if (t === 25) { const nm = ppAdvEnum && ppAdvEnum[a]; return nm ? 'The ' + nm : ('adventurer #' + a); }
    const nm = ppRewardEnum && ppRewardEnum[t];
    return (nm || ('#' + t)) + (a > 0 ? ' x ' + a : '');
  }
  // ---- crew for hire (inventory container 678) -------------------------------
  // Hire candidates are real ITEMS in container 678 (slots 0-2 crew + slot 3 captain); stats and
  // price are item params 3080 icon, 3081-3084 stats, (3093,3094)/(3095,3096) = resource + amount.
  const PP_CREW_CONTAINER = 678, PP_CREW_CAPTAIN_SLOT = 3;
  const PP_CREW_STATS = [['Morale', 3082], ['Combat', 3083], ['Seafaring', 3084], ['Speed', 3081]];
  // script7318 prints each trait name literally when its item param is > 0, in this order.
  const PP_CREW_TRAITS = [[3100, 'Pirate Band'], [3101, 'Solidarity'], [3102, 'Staunch'],
                          [3103, 'Rallying Cry'], [3104, 'Gambler'], [3106, 'Explosive'],
                          [3107, 'Merchant'], [3108, 'Resurrects'], [3105, 'Good Fortune']];
  let ppCrew = null;                    // container rows: [slot, itemId, stack, name]
  let ppCrewAt = 0;                     // when the roster was last seen live (ms)
  const ppItemParams = {};              // item id -> {ints:{},strs:{}} (cache-static, memoized)
  // Container 678 is the only source of the hire list (no varbit mirror) and exists only while the
  // game has it synced, so snapshot it -- stamped with the UTC day index, since the list rerolls
  // at the same daily reset as voyages.
  function ppCrewDay() { return Math.floor(Date.now() / 86400000); }
  let ppCrewSavedDay = -1;
  function ppCrewKey() { return 'rtxPortsCrew:' + ((lastSnap && lastSnap.display_name) || '-'); }
  function ppCrewSave() {
    try { localStorage.setItem(ppCrewKey(), JSON.stringify({ t: ppCrewAt, d: ppCrewSavedDay, items: ppCrew || [] })); } catch (e) {}
  }
  function ppCrewDrop() {
    ppCrew = null; ppCrewAt = 0; ppCrewSavedDay = -1;
    try { localStorage.removeItem(ppCrewKey()); } catch (e) {}
  }
  function ppCrewLoad() {
    if (ppCrew) {
      if (ppCrewSavedDay !== ppCrewDay()) ppCrewDrop();
      return;
    }
    try {
      const raw = JSON.parse(localStorage.getItem(ppCrewKey()) || 'null');
      if (!raw || !Array.isArray(raw.items) || !raw.items.length) return;
      if (raw.d !== ppCrewDay()) { try { localStorage.removeItem(ppCrewKey()); } catch (e2) {} return; }
      ppCrew = raw.items; ppCrewAt = raw.t || 0; ppCrewSavedDay = raw.d;
    } catch (e) {}
  }
  async function ppLoadCrew() {
    if (!bridge().containerItems || !bridge().itemParams) return;
    await ppRewardTables();             // enum 2164 names the cost resources
    ppCrewLoad();
    try {
      const d = JSON.parse(await bridge().containerItems(myPid(), PP_CREW_CONTAINER) || '{}');
      // An empty container means "not synced right now", not "the port has nobody".
      if (d && Array.isArray(d.items) && d.items.some(r0 => (r0[1] | 0) > 0)) {
        ppCrew = d.items; ppCrewAt = Date.now(); ppCrewSavedDay = ppCrewDay(); ppCrewSave();
      }
    } catch (e) { /* keep the previous roster */ }
    for (const r0 of (ppCrew || [])) {
      const id = r0[1] | 0;
      if (id > 0 && ppItemParams[id] === undefined) {
        try { ppItemParams[id] = JSON.parse(await bridge().itemParams(id) || 'null'); }
        catch (e) { ppItemParams[id] = null; }
      }
    }
  }
  // ---- your crew (container 677) ---------------------------------------------
  // Hired crew + captains are ITEMS in container 677, 6 slots per ship (script7311: 0-5 ship 1,
  // 6-11 ship 2, 12-17 ship 3, 18-23 ship 4, 24-29 block 5). A slot's varc is 2655+slot: bits
  // 0-12 = crew xp (level ladder script7317), bits 25-30 = captain index for captain items.
  // Captains are the item keys of enum 2173 (script7318's enum_hasoutput gate); captain k's name
  // is varbits 17155/17159/17160 + 13*(k-1) via enums 2195|5767 + 5768, its traits varbits
  // 17148..17151 + 13*(k-1) via enum 2175 -> struct param 3090.
  const PP_ROSTER_INV = 677, PP_ROSTER_SLOTS = 30, PP_ROSTER_VC0 = 2655;
  const PP_XP_LEVELS = [7, 21, 50, 100, 200, 400, 800, 1500, 2300, 4000];   // script7317
  let ppRoster = null, ppRosterAt = 0, ppVc = null;
  let ppCapSet = null;      // enum 2173 keys = captain item ids
  let ppRoleEnum = null;    // enum 2175: index -> trait/role struct
  const ppRoleNames = {};
  function ppCrewLevel(vc) {
    let l = 0;
    for (const t of PP_XP_LEVELS) { if ((vc & 8191) >= t) l++; else break; }
    return l;
  }
  function ppRosterSaveKey() { return 'rtxPortsRoster:' + ((lastSnap && lastSnap.display_name) || '-'); }
  async function ppLoadRoster() {
    if (!bridge().containerItems || !bridge().itemParams) return;
    if (!ppRoster) {
      try {
        const raw = JSON.parse(localStorage.getItem(ppRosterSaveKey()) || 'null');
        if (raw && Array.isArray(raw.items) && raw.items.length) { ppRoster = raw.items; ppRosterAt = raw.t || 0; }
      } catch (e) {}
    }
    try {
      const d = JSON.parse(await bridge().containerItems(myPid(), PP_ROSTER_INV) || '{}');
      // An empty container means "not synced right now", not "no crew".
      if (d && Array.isArray(d.items) && d.items.some(r0 => (r0[1] | 0) > 0)) {
        ppRoster = d.items; ppRosterAt = Date.now();
        try { localStorage.setItem(ppRosterSaveKey(), JSON.stringify({ t: ppRosterAt, items: ppRoster })); } catch (e) {}
      }
    } catch (e) { /* keep the previous roster */ }
    if (bridge().varcsDumpAll) {
      try {
        const vc = JSON.parse(await bridge().varcsDumpAll(myPid()) || 'null');
        if (vc) {
          ppVc = {};
          for (let s = 0; s < PP_ROSTER_SLOTS; s++) ppVc[s] = vc['5:' + (PP_ROSTER_VC0 + s)] | 0;
          // Central-LARGE slot origin (1477 comp 724): the ports screens' trees are
          // SLOT-RELATIVE, so every highlight rect needs this added (live-proven: the
          // varcs equal comp 724's accumulated position at two different spots).
          ppVc[6463] = vc['5:6463'] | 0; ppVc[6464] = vc['5:6464'] | 0;
          // Crew-window header ship totals (script6184; live-pinned 2026-07-29):
          // morale 2607, combat 2605, seafaring 2609, speed 2603. Absent = window closed.
          for (const id of [2603, 2605, 2607, 2609])
            if (vc['5:' + id] !== undefined) ppVc[id] = vc['5:' + id] | 0;
        }
      } catch (e) {}
    }
    if (!ppCapSet) {
      try {
        const e2 = JSON.parse(await bridge().enumInfo(2173) || 'null');
        // enum_hasoutput tests the enum's VALUES (the 20 'Captain' item ids), not its keys.
        if (e2) { ppCapSet = {}; for (const k in e2) ppCapSet[e2[k] | 0] = 1; }
      } catch (e) {}
    }
    if (!ppRoleEnum) { try { ppRoleEnum = JSON.parse(await bridge().enumInfo(2175) || 'null'); } catch (e) {} }
    for (const r0 of (ppRoster || [])) {
      const id = r0[1] | 0;
      if (id > 0 && ppItemParams[id] === undefined) {
        try { ppItemParams[id] = JSON.parse(await bridge().itemParams(id) || 'null'); }
        catch (e) { ppItemParams[id] = null; }
      }
    }
  }
  async function ppRoleInfo(idx) {
    if (!idx || idx <= 0 || !ppRoleEnum) return null;
    if (ppRoleNames[idx] === undefined) {
      ppRoleNames[idx] = null;
      try {
        const sid = ppRoleEnum[idx] | 0;
        if (sid > 0) {
          const sp = JSON.parse(await bridge().structParams(sid) || 'null');
          const I = (sp && sp.ints) || {};
          ppRoleNames[idx] = { name: (sp && sp.strs && sp.strs['3090']) || '',
                              st: [I['3082'] | 0, I['3083'] | 0, I['3084'] | 0] };
        }
      } catch (e) { delete ppRoleNames[idx]; }
    }
    return ppRoleNames[idx];
  }
  async function ppRoleName(idx) { const r = await ppRoleInfo(idx); return (r && r.name) || ''; }
  // Effective morale/combat/seafaring exactly as script7312 computes them: item base +10% per
  // level, + reallocation points (varc bits 16-18/19-21/22-24, x10 crew / x20 captain), and for
  // captain k the enum-2175 struct at k plus its 4 trait structs (varbits 17148+13(k-1)..+3).
  async function ppEffStats(sl, id) {
    const vc = (ppVc && ppVc[sl]) | 0;
    const p = (ppItemParams[id] && ppItemParams[id].ints) || {};
    const lvl = ppCrewLevel(vc);
    const isCap = !!(ppCapSet && ppCapSet[id]);
    const mult = isCap ? 20 : 10;
    const pts = [(vc >>> 16) & 7, (vc >>> 19) & 7, (vc >>> 22) & 7];
    const out = [3082, 3083, 3084].map((pk, i) => {
      const base = p[pk] | 0;
      return base + Math.floor(10 * lvl * base / 100) + pts[i] * mult;
    });
    const k = (vc >>> 25) & 63;
    if (isCap && k >= 1 && k <= 5) {
      const r0 = await ppRoleInfo(k);
      if (r0) for (let i = 0; i < 3; i++) out[i] += r0.st[i];
      for (let ti = 0; ti < 4; ti++) {
        const rt = await ppRoleInfo(ppV(17148 + 13 * (k - 1) + ti));
        if (rt) for (let i = 0; i < 3; i++) out[i] += rt.st[i];
      }
    }
    return out;
  }
  // Captain k (1-5) display name; same enum walk as the captain for hire.
  async function ppShipCaptainName(k) {
    if (!ppCapEnums) await ppCaptainName();
    if (!ppCapEnums) return '';
    const b = 13 * (k - 1);
    const first = ppCapEnums[ppV(17155 + b) === 1 ? 1 : 0][ppV(17159 + b)];
    const sur   = ppCapEnums[2][ppV(17160 + b)];
    return [first, sur].filter(x => typeof x === 'string' && x.trim()).join(' ').trim();
  }

  // ---- The Trader (interface 1028) -------------------------------------------
  // vb 21322 = offered trade good (11-17), 21323 = stock, 21324 = payment resource (2-9).
  // Price per unit is COMPUTED, not stored: pool[good] / base[payResource].
  const PP_PAY_BASE  = { 2: 120, 3: 160, 4: 200, 5: 240, 6: 320, 7: 400, 8: 500, 9: 640 };
  const PP_GOOD_POOL = { 11: 550000, 12: 400000, 13: 750000, 14: 750000, 15: 750000,
                         16: 1200000, 17: 1200000 };
  function ppTraderPrice(good, pay) {
    const b = PP_PAY_BASE[pay] || 0, pool = PP_GOOD_POOL[good] || 0;
    return (b > 0 && pool > 0) ? Math.floor(pool / b) : 0;
  }

  // One building tier -> {name, desc, cost}; null past the top tier. Cache-static, so memoized.
  const ppTierEnums = {}, ppTierCache = {};
  async function ppBuildTier(eid, lvl) {
    const key = eid + ':' + lvl;
    if (ppTierCache[key] !== undefined) return ppTierCache[key];
    ppTierCache[key] = null;
    try {
      if (!ppTierEnums[eid]) ppTierEnums[eid] = JSON.parse(await bridge().enumInfo(eid) || 'null');
      const sid = (ppTierEnums[eid] && ppTierEnums[eid][lvl]) | 0;
      if (sid > 0) {
        await ppRewardTables();
        const sp = JSON.parse(await bridge().structParams(sid) || 'null');
        const I = (sp && sp.ints) || {}, S = (sp && sp.strs) || {};
        const res = [];
        const cost = [[3132, 3133], [3134, 3135]].map(([rp, ap]) => {
          const r = I[rp] | 0, a = I[ap] | 0;
          if (!r || !a) return '';
          res.push(r);
          return a.toLocaleString() + ' ' + ((ppRewardEnum && ppRewardEnum[r]) || ('#' + r));
        }).filter(Boolean).join(' + ');
        ppTierCache[key] = { name: S['3118'] || '', desc: (S['3125'] || ''), cost, res };
      }
    } catch (e) { delete ppTierCache[key]; }   // cache miss -> retry next paint
    return ppTierCache[key];
  }
  const ppPlain = s => String(s || '').replace(/<br>/gi, ' · ').replace(/<[^>]*>/g, '').trim();
  // Totem, Icon and Portal hotspots are a CHOICE of decoration, not a ladder (script7337 only
  // tests `built == level`), so they have options rather than a "next" upgrade.
  const PP_PICK_ENUMS = { 7149: 1, 7150: 1, 7151: 1 };
  // Several buildings BRANCH: levels grouped at the SAME rank are alternatives (Bar 4/5/6 =
  // Sinister|Nautical|Luxurious), so the raw enum index over-counts the real upgrade steps.
  const PP_TIER_RANKS = {
    7117: [0, 1, 2, 3, 4, 4, 4, 5, 6, 6, 6],   // Bar        (gate vb 26826)
    7146: [0, 1, 2, 3, 4, 4, 4, 5, 5, 5],      // Shipwright (gate vb 26831)
    7147: [0, 1, 2, 3, 4, 4, 4, 5, 5, 5],      // Lodgings   (gate vb 26833)
  };
  // Highest level the tier enum actually defines, so a row can show "tier 2 / 10".
  async function ppTierMax(eid) {
    if (!ppTierEnums[eid]) {
      try { ppTierEnums[eid] = JSON.parse(await bridge().enumInfo(eid) || 'null'); } catch (e) { return 0; }
    }
    let mx = 0;
    for (const k of Object.keys(ppTierEnums[eid] || {})) {
      const n = +k;
      if (n > mx && (ppTierEnums[eid][k] | 0) > 0) mx = n;
    }
    return mx;
  }
  // Every option of a pick-style hotspot, level 0 ("Nothing") excluded.
  async function ppTierList(eid) {
    if (!ppTierEnums[eid]) {
      try { ppTierEnums[eid] = JSON.parse(await bridge().enumInfo(eid) || 'null'); } catch (e) { return []; }
    }
    const out = [];
    for (const k of Object.keys(ppTierEnums[eid] || {}).map(Number).sort((a, b) => a - b)) {
      if (k <= 0) continue;
      const t = await ppBuildTier(eid, k);
      if (t && t.name) out.push(t);
    }
    return out;
  }

  // ---- progression guide -------------------------------------------------------
  // Region ladder, all cache-proven: thresholds = script4223; a region's new resource id is
  // region+1 (script7370 prints enum 7152[region+1]); trade goods (reward types 11-17) first
  // appear on voyages gated at 40,000 distance (voyage structs, param 3055 = required distance),
  // i.e. The Hook. Region names resolve live from enum 7156.
  const PP_REGIONS = [[1, 0], [2, 5000], [3, 40000], [4, 140000], [5, 450000], [6, 1200000], [7, 2300000], [8, 4100000]];
  // Ladder buildings in the guide's upgrade-priority order; Office leads because tiers 2/3 are
  // the 3rd/4th ship (script4186).
  const PP_GUIDE_ORDER = ['Office', 'Shipwright', 'Lodgings', 'Warehouse', 'Bar', 'Workshop'];
  const PP_GUIDE_PICKS = ['Totem 1', 'Totem 2', 'Totem 3', 'Totem 4', 'Icon 1', 'Icon 2', 'Icon 3', 'Portal'];
  // Wiki guide: the three most useful totems, in likely order of use - Map table (clue voyages),
  // Pandora's Box (random events), Jade statue (trade goods). Enum-7150 levels 6, 4, 5.
  const PP_TOTEM_RECS = [6, 4, 5];
  const ppBuilding = nm => PP_BUILDINGS.filter(b => b[0] === nm)[0];
  let ppRegionEnum = null, ppScrollEnum = null;
  async function ppGuideEnums() {
    if (!ppRegionEnum) { try { ppRegionEnum = JSON.parse(await bridge().enumInfo(7156) || 'null'); } catch (e) {} }
    if (!ppScrollEnum) { try { ppScrollEnum = JSON.parse(await bridge().enumInfo(603) || 'null'); } catch (e) {} }
  }
  const ppRegionName = z => (ppRegionEnum && ppRegionEnum[z]) || ('Region ' + z);
  // Enum 603 = the 34 scroll effects in vb-17497 index order (script7372 pairs each piece varbit
  // with its enum-603 key); the name is the text before the first <br>.
  const ppScrollName = i => ppPlain(String((ppScrollEnum && ppScrollEnum[i]) || '').split('<br>')[0]) || ('Scroll #' + i);
  const ppResRegion = rid => Math.max(1, (rid | 0) - 1);   // resource id -> region that unlocks it
  // Ship parts (script7032): four catalogs, ownership = TESTBIT on the listed word (vb 17033/34
  // pack into varp 3380). Struct params: 662 name, 660 index, 664/665/684/688 = speed/morale/
  // combat/seafaring, cost pairs (701,702)+(703,704) as resource id -> amount, 4485 = successor.
  const PP_PARTS = [
    ['Hulls', 1010, 3378, 0], ['Deck items', 1011, 3379, 0],
    ['Rudders', 1012, 17033, 1], ['Rams/Figureheads', 1013, 17034, 1]];
  const ppPartEnums = {};
  async function ppPartList(eid) {
    if (ppPartEnums[eid]) return ppPartEnums[eid];
    try {
      const en = JSON.parse(await bridge().enumInfo(eid) || 'null');
      if (!en) return null;
      await ppRewardTables();
      const out = [];
      for (const k of Object.keys(en).map(Number).sort((a, b) => a - b)) {
        const sid = en[k] | 0;
        if (sid <= 0) continue;
        const sp = JSON.parse(await bridge().structParams(sid) || 'null');
        const I = (sp && sp.ints) || {}, S = (sp && sp.strs) || {};
        const cost = [[I['701'] | 0, I['702'] | 0], [I['703'] | 0, I['704'] | 0]]
          .filter(c => c[0] > 0 && c[1] > 0);
        out.push({ idx: I['660'] | 0, name: S['662'] || ('Part ' + k), cost,
                   st: [I['665'] | 0, I['684'] | 0, I['688'] | 0, I['664'] | 0] });
      }
      ppPartEnums[eid] = out;
    } catch (e) { return null; }
    return ppPartEnums[eid];
  }
  const ppPartCost = cost => cost.map(c =>
    c[1].toLocaleString() + ' ' + ((ppRewardEnum && ppRewardEnum[c[0]]) || ('#' + c[0]))).join(' + ');

  // Highest rank (and an enum level reaching it) whose build costs use only resources already
  // unlocked - the guide's per-region upgrade target, derived from the tier structs.
  async function ppReachableTier(eid, zone) {
    const mx = await ppTierMax(eid);
    const rk = PP_TIER_RANKS[eid];
    const best = { rank: 0, lvl: 0 };
    for (let lvl = 1; lvl <= mx; lvl++) {
      const t = await ppBuildTier(eid, lvl);
      if (!t || !(t.res || []).every(r => ppResRegion(r) <= zone)) continue;
      const rank = rk ? (rk[lvl] != null ? rk[lvl] : lvl) : lvl;
      if (rank > best.rank) { best.rank = rank; best.lvl = lvl; }
    }
    return best;
  }

  // Minutes to the daily refresh (1440 - DATE_MINUTES mod 1440); one clock for voyages and crew.
  function ppDayMins() { return 1440 - (Math.floor(Date.now() / 60000) % 1440); }
  function ppDayText() {
    const m = ppDayMins();
    return m >= 60 ? Math.floor(m / 60) + 'h ' + (m % 60 < 10 ? '0' : '') + (m % 60) + 'm' : m + 'm';
  }
  function ppCrewCost(p) {
    return [[3093, 3094], [3095, 3096]].map(([rp, ap]) => {
      const rid = p[rp] | 0, amt = p[ap] | 0;
      if (!rid || !amt) return '';
      return amt.toLocaleString() + ' ' + ((ppRewardEnum && ppRewardEnum[rid]) || ('#' + rid));
    }).filter(Boolean).join(' + ');
  }

  // ---- in-game move highlight (crew screen, interface group 916) ---------------
  // The next move's crew-name TEXT widget sits on its 35x35 portrait cell, so boxing that
  // widget's absolute rect marks the portrait. Same-name duplicates: first match wins.
  let ppMovesHi = false;
  try { ppMovesHi = localStorage.getItem('rtxPortsMovesHi') === '1'; } catch (e) {}
  let ppMoveTarget = null;              // {a:{sl}, b:{sl}} = next move member + dest socket
  let _ppHiSig = null;
  let ppReplanWhy = '';                 // reason tag for the NEXT plan write ('button', ...)
  let ppPlanGoneAt = 0;                 // debounce clock for the all-voyages-gone check
  // Move-list double-read confirmation: container 677 updates one game tick before the slot
  // varcs travel with a moved member, so a single poll can misattach levels between same-id
  // crew and INVERT the suggested moves for ~600ms. A new move list renders only after two
  // consecutive polls agree.
  let ppStepsShownSig = null, ppStepsPendingSig = null, ppStepsView = null;
  // AUTHORITATIVE in-memory stores. localStorage is a SILENT NO-OP in this Ultralight build
  // (proven live: the plan mint counter never incremented while fresh plans minted every
  // repaint), so module state is the real persistence; localStorage stays as best-effort
  // backup for dock restarts where it does work.
  const ppPlanByChar = {};              // charName -> frozen plan
  const ppGearByChar = {};              // charName -> gear array
  let ppPlanN = 0;                      // session mint counter
  // The Crew Roster is group 1276 (live-probed 2026-07-29): member portraits are comp 7,
  // 35x35, with SUB = the container-677 slot (cells comp 5, ship badges comp 9 - all
  // slot-keyed), so targeting is by slot ONLY. Name matching is banned here: the same
  // names also appear in the Crew For Hire panel and the selected-member pane, which is
  // what the boxes were landing on. Neither group has a panel-origin entry (no "a"), so
  // window coords come from a depth-stack accumulation of the parent-relative offsets -
  // the roster window roots at the game-view origin.
  // Slot-keyed member comps in the ship crew window (916, script7252, live-verified
  // 2026-07-29): viewed ship's captain socket comp 186 + crew sockets comp 182 ('None' when
  // empty; only the viewed ship's block renders them); available captains comp 191,
  // available crew comp 199 (its sub 30 = the dismiss cell, never a real slot). Crew Roster
  // screen (1276): portraits comp 7. Matching is comp-whitelisted with NO size fallback -
  // unrelated buttons share sub indices (43:0 'Voyage list' stole the slot-0 box).
  const PP_CREW_GROUPS = [916, 1276];
  const PP_HI_COMPS = { 7: 1, 182: 1, 186: 1, 191: 1, 199: 1 };
  // A parked member and the ship row's EMPTY socket share the same slot sub (182:5 'None' vs
  // 199:5 portrait), so the SOURCE box prefers people comps (199/191/7) and only accepts a
  // socket that is not 'None'; the DEST box prefers sockets (182/186).
  function ppFindWidget(ws, sl, wantSocket) {
    const org = [];
    let people = null, socket = null, socketNone = false;
    for (const w of ws) {
      const d = w.d | 0, r = w.r || [0, 0, 0, 0];
      const ax = (d > 0 && org[d - 1] ? org[d - 1][0] : 0) + r[0];
      const ay = (d > 0 && org[d - 1] ? org[d - 1][1] : 0) + r[1];
      org[d] = [ax, ay];
      if (!(r[2] > 0 && r[3] > 0) || !w.t || w.t[2] !== sl || !PP_HI_COMPS[w.t[1]]) continue;
      // Trees are relative to the central-LARGE slot (1477:724) = varcs 6463/6464; w.a is
      // deliberately ignored (it can carry stale Interfaces-tab calibration nudges).
      const rect = [((ppVc && ppVc[6463]) | 0) + ax, ((ppVc && ppVc[6464]) | 0) + ay, r[2], r[3]];
      if (w.t[1] === 182 || w.t[1] === 186) {
        if (!socket) { socket = rect; socketNone = String(w.x || '').indexOf('None') >= 0; }
      } else if (!people) people = rect;
      if (people && socket) break;
    }
    // A destination is ONLY ever a ship-row socket (182 crew / 186 captain) - falling back
    // to a person widget pointed "here" at whoever is parked in that slot number.
    if (wantSocket) return socket;
    return people || (socket && !socketNone ? socket : null);
  }
  function ppMovesHiTick() {
    if (!bridge() || !bridge().interfaceGroup || !bridge().uiHighlight) return;
    let ra = null, rb = null, rbLabel = 'here';
    if (ppMovesHi && ppMoveTarget) {
      try {
        for (const gid of PP_CREW_GROUPS) {
          const ws = (JSON.parse(bridge().interfaceGroup(myPid(), gid) || '{}').widgets) || [];
          if (!ws.length) continue;
          ra = ppFindWidget(ws, ppMoveTarget.a.sl, false);
          if (ppMoveTarget.b) {
            // If the destination is on ANOTHER ship's view, the socket isn't on screen and
            // an eager drop lands on the WRONG ship (replace-on-assign). Flag the window
            // header instead so the user switches views first.
            const destShip = Math.floor(ppMoveTarget.b.sl / 6) + 1;
            const view = (ppVp && ppVp['3390']) | 0;
            if (gid === 916 && view >= 1 && view !== destShip) {
              const org = [];
              for (const w of ws) {
                const d = w.d | 0, r = w.r || [0, 0, 0, 0];
                const ax = (d > 0 && org[d - 1] ? org[d - 1][0] : 0) + r[0];
                const ay = (d > 0 && org[d - 1] ? org[d - 1][1] : 0) + r[1];
                org[d] = [ax, ay];
                if (w.t && w.t[1] === 323 && r[2] > 0) {   // the header's ship-number digit
                  rb = [((ppVc && ppVc[6463]) | 0) + ax, ((ppVc && ppVc[6464]) | 0) + ay, r[2], r[3]];
                  rbLabel = 'go to Ship ' + destShip;
                  break;
                }
              }
            } else rb = ppFindWidget(ws, ppMoveTarget.b.sl, true);
          }
          if (ra || rb) break;
        }
      } catch (e) {}
    }
    const sig = (ra ? ra.join(',') : '') + '|' + (rb ? rb.join(',') + rbLabel : '');
    if (sig === _ppHiSig) return;       // clears once when the boxes go away, then stays quiet
    _ppHiSig = sig;
    try {
      bridge().uiHighlight(myPid(), ra ? ra[0] : 0, ra ? ra[1] : 0, ra ? ra[2] : 0, ra ? ra[3] : 0);
    } catch (e) {}
    try {
      if (bridge().panelViz) bridge().panelViz(myPid(), rb ? (rb.join(',') + ',' + rbLabel) : '');
    } catch (e) {}
  }

  let ppNotify = {};                    // alert bells: ship / voyages / visitor
  try { ppNotify = JSON.parse(localStorage.getItem('rtxPortsNotify') || '{}') || {}; } catch (e) {}
  function ppNotifyAny() { for (const k in ppNotify) if (ppNotify[k]) return true; return false; }
  let ppPrev = null;                    // previous alert snapshot (edge detection)

  async function fetchPorts(bg) {
    if (!bridge() || !bridge().varbits || ppFetching) return;
    const now = Date.now();
    // While a crew window is open (varp 3390 > 0) the user is actively rearranging - poll
    // fast so rows/moves/boxes track the game instead of lagging seconds behind.
    const gate = bg ? 10000 : ((ppVp && (ppVp['3390'] | 0) > 0) ? 600 : 2000);
    if (now - _ppAt < gate) return;
    _ppAt = now; ppFetching = true;
    try {
      const vb = JSON.parse(await bridge().varbits(myPid(), PP_VB_IDS) || 'null');
      if (vb && typeof vb === 'object' && Object.keys(vb).length) ppVb = vb;
      const vp = JSON.parse(await bridge().varps(myPid(), PP_VP_IDS) || 'null');
      if (vp && typeof vp === 'object') ppVp = vp;
      await ppLoadCrew();
      await ppLoadRoster();
      ppAlertTick();
      ppMovesHiTick();
    } catch (e) { /* keep previous */ }
    ppFetching = false;
    if (activeTab === 'ports') renderPorts();
  }

  const ppV = k => ((ppVb && ppVb[String(k)]) | 0);
  const ppP = k => ((ppVp && ppVp[String(k)]) | 0);
  function ppShipCount() {   // script4186
    if (ppV(17398) > 2) return 4;
    if (ppV(17398) > 1) return 3;
    if (ppV(17495) >= 63) return 2;
    return ppV(17457) === 1 ? 1 : 0;
  }
  function ppZones() {
    const d = ppV(17458);
    for (const [th, n] of PP_ZONES) if (d >= th) return n;
    return ppV(17495) >= 63 ? 1 : 0;
  }
  // Status priority: Returned > (timer -> Damaged | Sailing ETA) > Ready. Absolute return
  // time = 22000000 + the indexed timer varbit, in Unix minutes.
  function ppShipStatus(s) {
    if (ppV(s.ret) > 0) return { st: 'returned', txt: 'Returned - collect!' };
    const idx = ppV(s.idx);
    if (idx >= 1 && idx <= 8) {
      if (PP_DAMAGED[ppV(s.voy)]) return { st: 'damaged', txt: 'Damaged' };
      const eta = (22000000 + ppV(PP_RETVBS[idx - 1])) - Math.floor(Date.now() / 60000);
      if (eta > 0) return { st: 'sailing', txt: 'Sailing - ' + (eta >= 60 ? Math.floor(eta / 60) + 'h ' + (eta % 60) + 'm' : eta + 'm') };
      return { st: 'sailing', txt: 'Arriving soon...' };
    }
    return { st: 'ready', txt: 'Ready to sail' };
  }
  // Voyage id -> enum 1022 -> struct: 2365 name, 3056/3057/3058 the three adversity-stat
  // requirements, reward slots type 3060/3062/3064 with amount 3061/3063/3065.
  async function ppVoyageInfo(id) {
    if (!id || id <= 0 || PP_DAMAGED[id]) return null;
    if (!ppVoyEnum) {
      try { ppVoyEnum = JSON.parse(await bridge().enumInfo(1022) || 'null'); } catch (e) {}
      if (!ppVoyEnum) return null;
    }
    const st = ppVoyEnum[id] | 0;
    if (st <= 0) return null;
    if (ppVoyNames[st] === undefined) {
      let info = null;
      await ppRewardTables();
      try {
        const sp = JSON.parse(await bridge().structParams(st) || 'null');
        if (sp && (sp.strs || sp.ints)) {
          const I = sp.ints || {}, S = sp.strs || {};
          const rw = [];
          for (const [tp, ap] of [[3060, 3061], [3062, 3063], [3064, 3065]]) {
            const t = I[tp] | 0, a = I[ap] | 0;
            if (t > 0 || a > 0) rw.push(ppRewardText(t, a));
          }
          // req = the adversity stats in the game's own order: morale, combat, seafaring.
          info = { name: S['2365'] || '', req: [I['3056'] | 0, I['3057'] | 0, I['3058'] | 0], rw };
        }
      } catch (e) {}
      if (info) ppVoyNames[st] = info;   // only cache a RESOLVED voyage; a failed read retries
    }
    return ppVoyNames[st];
  }
  async function ppVoyageName(id) { const i = await ppVoyageInfo(id); return (i && i.name) || ''; }
  async function ppShipName(s, i) {   // 3 name-part varbits -> enums 1014/1015/1018
    if (!ppNameEnums) {
      try {
        ppNameEnums = [JSON.parse(await bridge().enumInfo(1014) || '{}'),
                       JSON.parse(await bridge().enumInfo(1015) || '{}'),
                       JSON.parse(await bridge().enumInfo(1018) || '{}')];
      } catch (e) { ppNameEnums = null; return 'Ship ' + (i + 1); }
    }
    const parts = s.nm.map((vbid, j) => {
      const val = ppV(vbid);
      const p = ppNameEnums[j] && ppNameEnums[j][val];
      return (typeof p === 'string') ? p.trim() : '';
    }).filter(Boolean);
    return parts.length ? parts.join(' ') : ('Ship ' + (i + 1));
  }

  // Captain first name = enum 2195 (or 5767 when vb 17220 == 1) at vb 17224, surname = 5768 at 17225.
  async function ppCaptainName() {
    if (!ppCapEnums) {
      try {
        ppCapEnums = [JSON.parse(await bridge().enumInfo(2195) || '{}'),
                      JSON.parse(await bridge().enumInfo(5767) || '{}'),
                      JSON.parse(await bridge().enumInfo(5768) || '{}')];
      } catch (e) { ppCapEnums = null; return ''; }
    }
    const first = ppCapEnums[ppV(17220) === 1 ? 1 : 0][ppV(17224)];
    const sur   = ppCapEnums[2][ppV(17225)];
    return [first, sur].filter(x => typeof x === 'string' && x.trim()).join(' ').trim();
  }
  // Ports icon (enum 1024) for a resource/good id; the item configs' inventory icons differ.
  async function ppGoodSprite(id) {
    if (!ppIconEnum) {
      try { ppIconEnum = JSON.parse(await bridge().enumInfo(1024) || 'null'); } catch (e) {}
      if (!ppIconEnum) return 0;
    }
    return ppIconEnum[id] | 0;
  }
  // Black market: vb 17462 = offered id (1-9 resources, 11-17 goods), vb 17463 = stock.
  function ppMarketOffer() {
    const id = ppV(17462);
    if (id >= 1 && id <= 9)  return { name: PP_RES[id - 1][0], spr: PP_RES[id - 1][2] };
    if (id >= 11 && id <= 17) return { name: PP_TRADE[id - 11][0], item: PP_TRADE[id - 11][2] };
    return null;
  }

  // Port statistics, computed exactly as Captain's Log script7359 does.
  function ppStats() {
    let met = 0, stories = 0, clues = 0;
    const seenAddon = {};
    for (const v2 of PP_VISITORS) {
      if (ppV(v2[1]) > 0) met++;
      stories += ppV(v2[2]);
      if (!seenAddon[v2[3]]) { seenAddon[v2[3]] = 1; stories += ppV(v2[3]); }
      if (v2[4]) clues += ppV(v2[4]);
    }
    let scrolls = 0;
    for (const id of PP_SCROLLS) scrolls += ppV(id);
    let upgrades = 0;
    for (const [vbid, tiers] of PP_UPG_TIERS) upgrades += tiers[ppV(vbid)] || 0;
    let parts = 0;
    const pop = (x, bits) => { let n2 = 0; for (let b = 0; b < bits; b++) n2 += (x >>> b) & 1; return n2; };
    parts += pop(ppP(3378), 32) + pop(ppP(3379), 32) + pop(ppV(17033), 16) + pop(ppV(17034), 16);
    const score = Math.floor(ppV(17458) / 5000) + met * 50 + stories * 20 + scrolls * 3 +
                  clues * 10 + upgrades * 10 + parts * 5;
    let rank = 'Portmaster';
    for (const [th, nm] of PP_RANKS) if (score < th) { rank = nm; break; }
    return { met, stories, clues, scrolls, upgrades, parts, score, rank };
  }

  // ---- alerts (edge-triggered) -----------------------------------------------
  function ppAlertState() {
    const ships = ppShipCount();
    let returned = 0, inPort = 0;
    for (let i = 0; i < ships; i++) if (ppV(PP_SHIPS[i].ret) > 0) returned++;
    for (const [, vbid] of PP_VISITORS) if (ppV(vbid) === 2) inPort++;
    const avail = ppV(17121) + (ppV(17115) > 0 ? 1 : 0) + (ppV(17116) > 0 ? 1 : 0) + (ppV(17117) > 0 ? 1 : 0);
    return { returned, inPort, voyages: avail };
  }
  function ppAlertTick() {
    if (!ppVb) return;
    const cur = ppAlertState();
    if (ppPrev) {
      const MSG = {
        ship:    'Player-Owned Ports: a ship has returned to your port.',
        visitor: 'Player-Owned Ports: an adventurer is visiting your port.',
        voyages: 'Player-Owned Ports: new voyages are available.',
      };
      const fire = k => {
        try { bridge().overlayNotify(myPid(), MSG[k], 8000); } catch (e) {}
        try { if (bridge().notifyWindows) bridge().notifyWindows('RuneToolsX', MSG[k]); } catch (e) {}
      };
      if (ppNotify.ship    && cur.returned > ppPrev.returned) fire('ship');
      if (ppNotify.visitor && cur.inPort   > ppPrev.inPort)   fire('visitor');
      if (ppNotify.voyages && cur.voyages  > ppPrev.voyages)  fire('voyages');
    }
    ppPrev = cur;   // first fetch = baseline only (no login burst)
  }
  function ppBell(key, tip) {
    const b = document.createElement('button'); b.className = 'pp-bell' + (ppNotify[key] ? ' on' : '');
    b.dataset.tip = tip;
    b.innerHTML = '<svg viewBox="0 0 24 24" width="12" height="12" fill="none" stroke="currentColor" stroke-width="2"><path d="M18 16v-5a6 6 0 1 0-12 0v5l-1.5 2h15z"/><path d="M10 20a2 2 0 0 0 4 0"/></svg>';
    b.addEventListener('click', () => {
      ppNotify[key] = ppNotify[key] ? 0 : 1;
      try { localStorage.setItem('rtxPortsNotify', JSON.stringify(ppNotify)); } catch (e) {}
      b.classList.toggle('on', !!ppNotify[key]);
    });
    return b;
  }

  // ---- render ----------------------------------------------------------------
  function renderPorts() {
    const c = $('content');
    let wrap = $('ppWrap');
    if (!wrap) {
      injectStyle('ppCss', `
          .pp-sec { margin: 2px 12px 0; }
          .pp-schead { display: flex; align-items: center; gap: 8px; padding: 10px 2px 6px; }
          .pp-st { flex: 1; color: var(--accent-hi); font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: .05em; }
          .pp-timer { color: var(--text); font-size: 11px; font-variant-numeric: tabular-nums; background: var(--bg-elev); border: 1px solid var(--border); border-radius: 6px; padding: 2px 8px; }
          .pp-card { background: var(--bg-elev); border: 1px solid var(--border); border-radius: 10px; }
          .pp-row { display: flex; align-items: center; gap: 10px; padding: 8px 12px; }
          .pp-row + .pp-row { border-top: 1px solid var(--border); }
          .pp-nm { flex: 1; min-width: 0; color: var(--text); font-size: 12.5px; }
          .pp-sub { color: var(--text-mute); font-size: 10.5px; margin-top: 1px; }
          .pp-pill { font-size: 10.5px; padding: 2px 9px; border-radius: 999px; border: 1px solid var(--border); color: var(--text-dim); white-space: nowrap; }
          .pp-pill.ok { color: var(--ok); border-color: rgba(77, 210, 138, .45); }
          .pp-pill.go { color: var(--accent-hi); border-color: var(--accent-lo); }
          .pp-pill.warn { color: #e0a856; border-color: rgba(224, 168, 86, .45); }
          .pp-pill.bad { color: var(--err); border-color: rgba(242, 92, 92, .45); }
          .pp-bar { height: 4px; margin: 0 12px 8px; border-radius: 2px; background: var(--bg); overflow: hidden; }
          .pp-bar > div { height: 100%; background: var(--accent-hi); }
          .pp-chip.cur { color: var(--accent-hi); border-color: var(--accent-lo); }
          .pp-sub.cur { color: var(--accent-hi); }
          .pp-chip.lock { opacity: .55; }
          .pp-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(88px, 1fr)); gap: 1px; }
          .pp-cell { display: flex; align-items: center; gap: 7px; padding: 7px 10px; min-width: 0; }
          .pp-ico { flex: 0 0 auto; width: 20px; height: 20px; }
          .pp-cnm { flex: 1; min-width: 0; color: var(--text-mute); font-size: 10.5px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
          .pp-cv { color: var(--text); font-size: 12px; font-variant-numeric: tabular-nums; }
          .pp-chips { display: flex; gap: 6px; flex-wrap: wrap; padding: 8px 12px; }
          .pp-chip { font-size: 10.5px; padding: 2px 8px; border-radius: 999px; border: 1px solid var(--border); color: var(--text-mute); }
          .pp-chip.on { color: var(--ok); border-color: rgba(77, 210, 138, .45); }
          .pp-chip.sail { color: var(--accent-hi); border-color: var(--accent-lo); }
          .pp-bell { flex: 0 0 auto; display: flex; align-items: center; justify-content: center; width: 22px; height: 22px; border-radius: 6px; background: none; border: 1px solid var(--border); color: var(--text-mute); cursor: pointer; padding: 0; }
          .pp-bell.on { color: var(--accent-hi); border-color: var(--accent-lo); }
          .pp-empty { margin: 10px 14px; color: var(--text-dim); font-size: 12px; }`);
      c.innerHTML = ''; ppSig = '';
      wrap = document.createElement('div'); wrap.id = 'ppWrap'; wrap.className = 'pane'; c.appendChild(wrap);
    }
    if (!ppVb) {
      wrap.innerHTML = '<div class="pp-empty">Reading Player-Owned Ports state... (be in-world)</div>';
      ppSig = ''; return;
    }
    const sig = PP_VB_IDS.split(',').map(k => ppVb[k] | 0).join(',') + '|' +
                PP_VP_IDS.split(',').map(k => (ppVp && ppVp[k]) | 0).join(',') + '|' +
                // hire list + crew roster are containers, not vars: changes must repaint
                (ppCrew || []).map(r0 => r0[0] + ':' + r0[1]).join(',') + '|' +
                (ppRoster || []).map(r0 => r0[0] + ':' + r0[1]).join(',') + '|' +
                // crew-window header varcs (ship totals) + slot varcs feed gear/levels
                [2603, 2605, 2607, 2609].map(k => (ppVc && ppVc[k]) | 0).join(',') + '|' +
                Math.floor(Date.now() / 60000);   // ETA/reset countdowns are minute-granular
    if (sig === ppSig) return;
    ppSig = sig;
    wrap.innerHTML = '';
    const sec = (title, timerText) => {
      const s = document.createElement('div'); s.className = 'pp-sec';
      const h = document.createElement('div'); h.className = 'pp-schead';
      const t = document.createElement('div'); t.className = 'pp-st'; t.textContent = title; h.appendChild(t);
      if (timerText) { const tm = document.createElement('span'); tm.className = 'pp-timer'; tm.textContent = timerText; h.appendChild(tm); }
      s.appendChild(h);
      const card = document.createElement('div'); card.className = 'pp-card'; s.appendChild(card);
      wrap.appendChild(s);
      return card;
    };
    const row = (name, sub, pill, pillCls, bell) => {
      const r = document.createElement('div'); r.className = 'pp-row';
      const nm = document.createElement('div'); nm.className = 'pp-nm';
      const t = document.createElement('div'); t.textContent = name; nm.appendChild(t);
      if (sub) { const sb = document.createElement('div'); sb.className = 'pp-sub'; sb.textContent = sub; nm.appendChild(sb); }
      r.appendChild(nm);
      if (bell) r.appendChild(bell);
      if (pill != null) { const p = document.createElement('span'); p.className = 'pp-pill' + (pillCls ? ' ' + pillCls : ''); p.textContent = pill; r.appendChild(p); }
      return r;
    };

    // vb 17495 = tutorial progress; 63 = completed, and the port state is meaningless before that.
    if (ppV(17495) < 63) {
      wrap.innerHTML = '<div class="pp-empty">Complete the Player-Owned Ports tutorial to see your port here.</div>';
      return;
    }
    const ships = ppShipCount();

    {
      const ov = sec('Port', null);
      ov.appendChild(row('Regions unlocked', ppV(17458).toLocaleString() + ' distance explored',
                         ppZones() + ' / 8'));
      // vb 17497 = 1-34, indexing the piece counters in PP_SCROLLS order; 4/4 = complete.
      const focus = ppV(17497);
      if (focus >= 1 && focus <= PP_SCROLLS.length) {
        const pieces = ppV(PP_SCROLLS[focus - 1]);
        const fr = row('Focused scroll', 'Resolving...', pieces + ' / 4', pieces >= 4 ? 'warn' : null);
        ov.appendChild(fr);
        (async () => {
          await ppGuideEnums();
          const sb = fr.querySelector('.pp-sub');
          if (sb) sb.textContent = ppScrollName(focus)
            + (pieces >= 4 ? ' · complete - change it on the Port Management page' : '');
        })();
      }
    }

    {   // Progression guide: live goals along the region ladder (cache anchors at the guide consts).
      const g = sec('Progression guide', null);
      const zone = ppZones(), dist = ppV(17458);
      const chips = document.createElement('div'); chips.className = 'pp-chips';
      const bar = document.createElement('div'); bar.className = 'pp-bar';
      const fill = document.createElement('div'); bar.appendChild(fill);
      const body = document.createElement('div');
      g.appendChild(chips); g.appendChild(bar); g.appendChild(body);
      (async () => {
        await ppGuideEnums(); await ppRewardTables();
        for (const [z, th] of PP_REGIONS) {
          const ch = document.createElement('span');
          ch.className = 'pp-chip' + (z < zone ? ' on' : z === zone ? ' cur' : ' lock');
          ch.textContent = ppRegionName(z);
          const resNm = (ppRewardEnum && ppRewardEnum[z + 1]) || '';
          // Reward gating proven from the voyage structs (param 3055 minima): scroll pieces
          // (type 20) from 5k, trade goods (11-17) and xp tomes (19) from 40k.
          ch.dataset.tip = ppRegionName(z)
            + (th ? '\nUnlocks at ' + th.toLocaleString() + ' distance' : '\nStarting region')
            + (resNm ? '\nNew resource: ' + resNm : '')
            + (z === 2 ? '\nScroll pieces from here on' : z === 3 ? '\nTrade goods from here on' : '');
          chips.appendChild(ch);
        }
        const rows = [], metGoals = [];
        const goal = (nm, sub, pill, cls, tip) => rows.push({ nm, sub, pill, cls, tip });
        if (zone < 8) {
          const lo = PP_REGIONS[zone - 1][1], hi = PP_REGIONS[zone][1];
          const pct = Math.max(0, Math.min(100, Math.floor((dist - lo) * 100 / (hi - lo))));
          fill.style.width = pct + '%';
          goal('Explore to ' + ppRegionName(zone + 1),
               dist.toLocaleString() + ' / ' + hi.toLocaleString() + ' distance', pct + '%', 'go',
               'Favour voyages with distance rewards until the next region unlocks.');
        } else fill.style.width = '100%';
        // Ladder buildings: the target is the best rank whose costs your unlocked resources cover.
        for (const nm of PP_GUIDE_ORDER) {
          const [, vbid, eid] = ppBuilding(nm);
          const lv = ppV(vbid);
          const rk = PP_TIER_RANKS[eid];
          const curRank = rk ? (rk[lv] != null ? rk[lv] : lv) : lv;
          const tgt = await ppReachableTier(eid, zone);
          if (tgt.rank > curRank) {
            const nextLvls = rk ? rk.map((r2, i) => [r2, i]).filter(x => x[0] === curRank + 1).map(x => x[1])
                                : [lv + 1];
            const nexts = [];
            for (const nl of nextLvls) { const t = await ppBuildTier(eid, nl); if (t) nexts.push(t); }
            if (!nexts.length) { const t = await ppBuildTier(eid, tgt.lvl); if (t) nexts.push(t); }
            const one = nexts.length === 1 ? nexts[0] : null;
            const shipNote = eid !== 7118 ? '' :
              curRank < 2 ? ' - tier 2 is your 3rd ship' : curRank < 3 ? ' - tier 3 is your 4th ship' : '';
            goal('Upgrade ' + nm + shipNote,
                 one ? one.name + (one.cost ? ' - ' + one.cost : '')
                     : 'Choose: ' + nexts.map(t => t.name).join(' | '),
                 'tier ' + curRank + ' → ' + tgt.rank, 'bad',
                 nexts.map(t => t.name + (t.cost ? '  (' + t.cost + ')' : '')
                               + (t.desc ? '\n' + ppPlain(t.desc) : '')).join('\n\n'));
          } else if (curRank > 0) metGoals.push(nm + ' at tier ' + curRank + ' - max for your regions');
        }
        // Ship parts: next affordable purchase per catalog, cheapest chain step first.
        for (const [cat, peid, word, isVb] of PP_PARTS) {
          const parts = await ppPartList(peid);
          if (!parts || !parts.length) continue;
          const ownedBit = i => ((isVb ? ppV(word) : ppP(word)) >>> i) & 1;
          const owned = parts.filter(p => ownedBit(p.idx)).length;
          const avail = parts.filter(p => !ownedBit(p.idx) && p.cost.every(c => ppResRegion(c[0]) <= zone));
          if (!avail.length) {
            if (owned) metGoals.push(cat + ': ' + owned + ' / ' + parts.length + ' ship parts');
            continue;
          }
          const nxt = avail[0];
          goal('Buy ' + nxt.name, cat + (nxt.cost.length ? ' - ' + ppPartCost(nxt.cost) : ' - free'),
               owned + ' / ' + parts.length, 'bad',
               avail.slice(0, 8).map(p =>
                 p.name + '  (' + (ppPartCost(p.cost) || 'free') + ')'
                 + [' morale +' + p.st[0], ' combat +' + p.st[1], ' seafaring +' + p.st[2], ' speed +' + p.st[3]]
                   .filter((s2, i2) => p.st[i2] > 0).join(' ·')).join('\n'));
        }
        // Pick hotspots surface once any option's costs are covered; before that they stay hidden.
        // Totem hotspots walk PP_TOTEM_RECS: each empty totem recommends the next unbuilt pick.
        const totemBuilt = {};
        for (const tn of ['Totem 1', 'Totem 2', 'Totem 3', 'Totem 4']) totemBuilt[ppV(ppBuilding(tn)[1])] = 1;
        let recIdx = 0;
        const recNames = [];
        for (const lvl of PP_TOTEM_RECS) { const t = await ppBuildTier(7150, lvl); if (t) recNames.push(t.name); }
        for (const nm of PP_GUIDE_PICKS) {
          const [, vbid, eid] = ppBuilding(nm);
          if (ppV(vbid) > 0) { metGoals.push(nm + ' built'); continue; }
          const opts = (await ppTierList(eid)).filter(o => (o.res || []).every(r2 => ppResRegion(r2) <= zone));
          if (!opts.length) continue;   // costs not covered yet: hidden until it's the step
          let sub = opts.length + ' option' + (opts.length > 1 ? 's' : '') + (opts[0].cost ? ' - ' + opts[0].cost : '');
          let tip = 'Options:\n' + opts.map(o => o.name + (o.cost ? '  (' + o.cost + ')' : '')).join('\n');
          if (eid === 7150) {
            while (recIdx < PP_TOTEM_RECS.length && totemBuilt[PP_TOTEM_RECS[recIdx]]) recIdx++;
            if (recIdx < PP_TOTEM_RECS.length) {
              const rec = await ppBuildTier(7150, PP_TOTEM_RECS[recIdx]);
              if (rec) sub = 'Recommended: ' + rec.name + (rec.cost ? ' - ' + rec.cost : '');
              recIdx++;   // the next empty totem recommends the next pick in order
            }
            tip = 'Build order: ' + recNames.join(' → ') + '\n\n' + tip;
          }
          goal('Build ' + nm, sub, 'empty', 'bad', tip);
        }
        const st = ppStats();
        if (zone >= 2) {   // scroll-piece voyages exist from The Skull (type-20 rewards, 3055 >= 5000)
          const focus = ppV(17497);
          if (!focus) {
            goal('Pick a focused scroll', 'Choose on the Port Management scrolls page', 'none', 'bad', null);
          } else if (ppV(PP_SCROLLS[focus - 1]) >= 4) {
            goal('Switch your focused scroll', ppScrollName(focus) + ' is complete', '4 / 4', 'warn', null);
          }
        }
        if (zone >= 3) {   // trade-good voyages exist from The Hook (types 11-17, 3055 >= 40000)
          let goods = 0; PP_TRADE.forEach(t => goods += ppV(t[1]));
          if (!goods && !st.scrolls) {
            goal('Run trade-good voyages', 'Trade goods drop from voyages here on', 'to do', 'bad', null);
          } else {
            metGoals.push('Trade voyages running - ' + goods + ' goods, ' + st.scrolls + ' scroll pieces');
          }
        }
        if (st.met < 12) {
          const unmet = PP_VISITORS.filter(v2 => ppV(v2[1]) === 0).map(v2 => v2[0]);
          goal('Meet the adventurers', 'Not yet met: ' + unmet.join(', '), st.met + ' / 12', 'bad', null);
        } else metGoals.push('All 12 adventurers met');
        if (zone === 8) {
          let comp = 0; PP_SCROLLS.forEach(id => { if (ppV(id) >= 4) comp++; });
          if (comp < PP_SCROLLS.length) goal('Complete every scroll', null, comp + ' / ' + PP_SCROLLS.length, 'bad', null);
          else metGoals.push('All ' + PP_SCROLLS.length + ' scrolls complete');
        }
        for (const r2 of rows) {
          const el = row(r2.nm, r2.sub, r2.pill, r2.cls);
          if (r2.tip) el.dataset.tip = r2.tip;
          body.appendChild(el);
        }
        if (!rows.length) body.appendChild(row('Nothing left to do here right now', null, 'up to date', 'ok'));
        if (metGoals.length) {
          const dr = row('Goals met', null, String(metGoals.length), 'ok');
          dr.dataset.tip = metGoals.join('\n');
          body.appendChild(dr);
        }
      })();
    }

    {
      const vo = sec('Voyages', 'new in ' + ppDayText());
      const avail = ppV(17121) + (ppV(17115) > 0 ? 1 : 0) + (ppV(17116) > 0 ? 1 : 0) + (ppV(17117) > 0 ? 1 : 0);
      vo.appendChild(row('Voyages available', 'Rerolls left: ' + ppV(17423) +
                         (ppV(17421) ? ' · +5 tokens: ' + ppV(17421) : ''),
                         String(avail), avail > 0 ? 'go' : null, ppBell('voyages', 'Notify when new voyages arrive')));
      // The two in-game tabs are separate slot varbits: Standard 17115-17, Special 17118-20.
      const slotRow = (label, vbids, empty) => {
        const r = row(label, 'Resolving...', null);
        vo.appendChild(r);
        (async () => {
          const names = [], tips = [];
          for (const vbid of vbids) {
            const id = ppV(vbid);
            if (id <= 0) continue;
            const info = await ppVoyageInfo(id);
            names.push((info && info.name) || ('Voyage #' + id));
            // Tooltip uses the global tip's opt-in markup path (same as perk descriptions).
            if (info) {
              const need = ['morale', 'combat', 'seafaring']
                .map((lab, k) => info.req[k] > 0 ? '<col=8a93a8>' + lab + '</col> ' + info.req[k] : '')
                .filter(Boolean).join('   ');
              tips.push('<col=ffd24a>' + info.name + '</col>'
                + (need ? '<br>' + need : '')
                + (info.rw.length ? '<br><col=8a93a8>rewards</col>  ' + info.rw.join(', ') : ''));
            }
          }
          const sb = r.querySelector('.pp-sub');
          if (sb) sb.textContent = names.length ? names.join(' · ') : empty;
          if (tips.length) { r.dataset.tip = tips.join('<br><br>'); r.dataset.tipHtml = '1'; }
        })();
        return r;
      };
      slotRow('Standard voyages', [17115, 17116, 17117], 'No standard voyages on offer');
      const spRow = slotRow('Special voyages', [17118, 17119, 17120], 'No special voyages on offer');
      if (ppP(3415) > 0) {
        const p = document.createElement('span'); p.className = 'pp-pill go';
        p.textContent = String(ppP(3415)); spRow.appendChild(p);
      }
    }

    {
      const sh = sec('Ships', null);
      const hdr = row('Fleet', null, null, null, ppBell('ship', 'Notify when a ship returns'));
      sh.appendChild(hdr);
      if (!ships) sh.appendChild(row('No ships yet', null));
      for (let i = 0; i < ships; i++) {
        const s = PP_SHIPS[i];
        const st = ppShipStatus(s);
        const r = row('Ship ' + (i + 1), '', st.txt,
                      st.st === 'returned' ? 'ok' : st.st === 'sailing' ? 'go' : st.st === 'damaged' ? 'warn' : null);
        sh.appendChild(r);
        (async () => {   // name + voyage resolve async; write into the finished row
          const nm = await ppShipName(s, i);
          const t = r.querySelector('.pp-nm div'); if (t) t.textContent = nm;
          const voyNm = await ppVoyageName(ppV(s.voy));
          const sb = r.querySelector('.pp-sub');
          if (sb) sb.textContent = voyNm || (st.st === 'ready' ? 'In port' : '');
        })();
      }
    }

    {   // Your crew: container 677 blocks per ship + optimal crew-to-ship-to-voyage assignment.
      const yc = sec('Your crew', null);
      const members = (ppRoster || []).filter(r0 => (r0[1] | 0) > 0);
      if (!members.length) {
        yc.appendChild(row('Crew roster not loaded', 'Open Port Management once to capture it', null));
      } else {
        if (ppRosterAt) {
          const mins = Math.floor((Date.now() - ppRosterAt) / 60000);
          const age = mins < 1 ? 'just now' : mins < 60 ? mins + 'm ago'
                    : Math.floor(mins / 60) + 'h ' + (mins % 60) + 'm ago';
          yc.appendChild(row('Last seen', 'captured from the port interfaces', age));
        }
        (async () => {
          // Members with the game's own effective stats. ABOARD is NOT the slot block: ship n
          // holds slots [6n..6n+5] but a member is aboard only if their bit is set in that
          // ship's mask varbit (script7295: ship 1 vb 17080, 2 vb 17091, 3 vb 17102, 4 vb
          // 17113; varp 3404 = all ships). Everyone else is the spare pool.
          const PP_ABOARD_VBS = [17080, 17091, 17102, 17113];
          const mem = [];
          for (const r0 of members) {
            const sl = r0[0] | 0, id = r0[1] | 0;
            const isCap = !!(ppCapSet && ppCapSet[id]);
            let nm = r0[3] || ('Item ' + id);
            if (isCap) {
              const k = (((ppVc && ppVc[sl]) | 0) >>> 25) & 63;
              if (k >= 1 && k <= 5) { const cn = await ppShipCaptainName(k); if (cn) nm = cn; }
            }
            const b = Math.floor(sl / 6);
            const aboard = (b < 4 && b < ships && ((ppV(PP_ABOARD_VBS[b]) >>> (sl - 6 * b)) & 1)) ? b : -1;
            mem.push({ sl, id, nm, cap: isCap, st: await ppEffStats(sl, id), aboard });
          }
          const shipList = [];
          for (let b = 0; b < Math.min(ships, 4); b++) shipList.push(b);
          // A ship under way (or returned, awaiting collection) locks its crew: nobody moves
          // on or off until it docks, and it cannot take another voyage.
          const shipLocked = shipList.map(b =>
            b < PP_SHIPS.length && ['sailing', 'returned'].indexOf(ppShipStatus(PP_SHIPS[b]).st) >= 0);
          const freeShips = shipList.filter(b => !shipLocked[b]);
          const shLabel = b => b < 0 ? 'Spare' : 'Ship ' + (b + 1);
          const lv = m => ppCrewLevel((ppVc && ppVc[m.sl]) | 0);
          const memLine = m => (m.cap ? 'CAPTAIN  ' : '') + m.nm + ' - Lv ' + lv(m) + ' - ' + m.st.join('/');
          // Gear = the crew window's OWN header totals minus our crew totals for the viewed
          // ship. varp 3390 = the viewed ship number (the header digit, comp 323; 0 = window
          // closed) and the header varcs are client-computed from the same live state, so
          // this pair can never disagree about which ship is being measured - unlike the
          // voyage screen's varps 3408-10 + vb 17132, which mis-keyed gear to the wrong ship.
          const gearChar = (lastSnap && lastSnap.display_name) || '-';
          const gearKey = 'rtxPortsGear:' + gearChar;
          if (!ppGearByChar[gearChar]) {
            ppGearByChar[gearChar] = [];
            try {
              const st = JSON.parse(localStorage.getItem(gearKey) || 'null');
              if (st && st.g) ppGearByChar[gearChar] = st.g;
            } catch (e) {}
          }
          const gear = ppGearByChar[gearChar];
          const gearOf = b => (gear[b] && gear[b].length === 3) ? gear[b] : [0, 0, 0];
          const vShip = ppP(3390);
          if (vShip >= 1 && vShip <= shipList.length && ppVc && ppVc[2607] !== undefined) {
            const sb = vShip - 1;
            const ct = [0, 0, 0];
            mem.forEach(m => { if (m.aboard === sb) for (let j = 0; j < 3; j++) ct[j] += m.st[j]; });
            const g3 = [(ppVc[2607] | 0) - ct[0], (ppVc[2605] | 0) - ct[1], (ppVc[2609] | 0) - ct[2]];
            // A negative component means our aboard read is STALE (we counted crew the game
            // no longer has aboard) - writing it would bake the error into every estimate.
            if (g3.every(x2 => x2 >= 0 && x2 < 3000)) {
              gear[sb] = g3;
              try { localStorage.setItem(gearKey, JSON.stringify({ g: gear })); } catch (e) {}
            }
          }
          for (const b of shipList.concat([-1])) {
            const bm = mem.filter(m => m.aboard === b);
            if (b < 0 && !bm.length) continue;
            const t = [0, 0, 0];
            bm.forEach(m => { for (let j = 0; j < 3; j++) t[j] += m.st[j]; });
            const g2 = b < 0 ? [0, 0, 0] : gearOf(b);
            const capM = bm.filter(m => m.cap)[0];
            const hasGear = g2[0] || g2[1] || g2[2];
            const r = row(shLabel(b),
                          (capM ? 'Captain ' + capM.nm + ' · ' : '') + bm.length + ' aboard',
                          b < 0 ? String(bm.length) : t.map((x2, j) => x2 + g2[j]).join(' / '), null);
            r.dataset.tip = 'morale / combat / seafaring = crew ' + t.join('/')
              + (hasGear ? ' + ship gear ' + g2.join('/') : ' (gear uncalibrated - open this ship\'s crew window once)')
              + '\n\n' + (bm.map(memLine).join('\n') || 'Nobody aboard');
            yc.appendChild(r);
            if (b >= 0 && b < PP_SHIPS.length) {
              const t2 = r.querySelector('.pp-nm div');
              ppShipName(PP_SHIPS[b], b).then(snm => { if (t2 && snm) t2.textContent = snm; });
            }
          }
          // Live cross-check: script2022 mirrors the viewed voyage's per-stat success in
          // vb 17126-17128 (overall = their minimum) while the assign screen is open.
          if (ppV(17126) || ppV(17127) || ppV(17128)) {
            const g3 = [ppV(17126), ppV(17127), ppV(17128)];
            yc.appendChild(row('Game success (viewed voyage)',
                               'morale ' + g3[0] + '% · combat ' + g3[1] + '% · seafaring ' + g3[2] + '%',
                               Math.min(g3[0], g3[1], g3[2]) + '%', 'go'));
          }

          // ---- optimal assignment ------------------------------------------
          const voys = [];
          for (const vbid of [17115, 17116, 17117, 17118, 17119, 17120]) {
            const vid = ppV(vbid);
            if (vid <= 0) continue;
            const info = await ppVoyageInfo(vid);
            if (info && (info.req[0] + info.req[1] + info.req[2]) > 0)
              voys.push({ vid, name: info.name || ('Voyage #' + vid), req: info.req,
                          sp: vbid >= 17118 });   // special-tab slots: story/adventurer voyages
          }
          if (!voys.length || !shipList.length) return;
          const cov = (t, req) => {
            let c = Infinity;
            for (let i = 0; i < 3; i++) if (req[i] > 0) c = Math.min(c, t[i] / req[i]);
            return c === Infinity ? 1 : c;
          };
          // Full requirement coverage first; special (story) voyages weigh 10x so they always
          // make the cut when ships are scarce. The spare-coverage tiebreak is UNWEIGHTED and
          // tiny, so it can never justify reshuffling ships (that fight goes to stab()).
          const score = cs => cs.reduce((a, e2) => a + e2.w * Math.min(e2.c, 1) + e2.c * 0.0001, 0);
          // Hill climb over member -> ship|spare placements. A ship takes at most 1 captain +
          // 5 crew, and a voyage ship without a captain scores 0 (it cannot sail).
          const CAP_CREW = 5;
          const runOpt = reqByShip => {
            const asg = mem.map(m => (m.aboard >= 0 && m.aboard < shipList.length) ? m.aboard : -1);
            const tot = [], crewN = [], capN = [];
            for (const b of shipList) { tot.push(gearOf(b).slice()); crewN.push(0); capN.push(0); }
            mem.forEach((m, i) => {
              if (asg[i] < 0) return;
              for (let j = 0; j < 3; j++) tot[asg[i]][j] += m.st[j];
              (m.cap ? capN : crewN)[asg[i]]++;
            });
            const covs = () => {
              const out = [];
              for (let b = 0; b < reqByShip.length; b++) {
                const rq = reqByShip[b];
                if (rq) out.push({ c: capN[b] > 0 ? cov(tot[b], rq.req) : 0, w: rq.w });
              }
              return out;
            };
            // Stability bias: prefer plans that keep members where they already are, so the
            // recommendation doesn't flip-flop mid-execution. 0.002/member = a rearrangement
            // must gain ~0.2% coverage per move it demands (0.02% on 10x specials) before the
            // plan changes; a full two-ship reshuffle (~11 moves) needs a substantial win.
            const stab = () => {
              let n2 = 0;
              for (let i = 0; i < mem.length; i++) if (asg[i] === mem[i].aboard) n2++;
              return n2 * 0.002;
            };
            const place = (i, dst) => {
              const src = asg[i], m = mem[i];
              if (src >= 0) { for (let j = 0; j < 3; j++) tot[src][j] -= m.st[j]; (m.cap ? capN : crewN)[src]--; }
              if (dst >= 0) { for (let j = 0; j < 3; j++) tot[dst][j] += m.st[j]; (m.cap ? capN : crewN)[dst]++; }
              asg[i] = dst;
            };
            let cur = score(covs()) + stab();
            let improved = true, guard = 0;
            while (improved && guard++ < 40) {
              improved = false;
              for (let i = 0; i < mem.length; i++) {
                const src = asg[i];
                if (src >= 0 && shipLocked[src]) continue;   // crew at sea cannot leave
                for (let dst = -1; dst < shipList.length; dst++) {
                  if (dst === src) continue;
                  if (dst >= 0 && shipLocked[dst]) continue; // nor can anyone board a sailing ship
                  if (dst >= 0 && (mem[i].cap ? capN[dst] >= 1 : crewN[dst] >= CAP_CREW)) continue;
                  place(i, dst);
                  const ns = score(covs()) + stab();
                  if (ns > cur + 1e-9) { cur = ns; improved = true; break; }
                  place(i, src);
                }
              }
              for (let i = 0; i < mem.length; i++) for (let j = i + 1; j < mem.length; j++) {
                if (asg[i] === asg[j] || mem[i].cap !== mem[j].cap) continue;
                const bi = asg[i], bj = asg[j];
                if ((bi >= 0 && shipLocked[bi]) || (bj >= 0 && shipLocked[bj])) continue;
                for (let s2 = 0; s2 < 3; s2++) {
                  if (bi >= 0) tot[bi][s2] += mem[j].st[s2] - mem[i].st[s2];
                  if (bj >= 0) tot[bj][s2] += mem[i].st[s2] - mem[j].st[s2];
                }
                asg[i] = bj; asg[j] = bi;
                const ns = score(covs()) + stab();
                if (ns > cur + 1e-9) { cur = ns; improved = true; }
                else {
                  for (let s2 = 0; s2 < 3; s2++) {
                    if (bi >= 0) tot[bi][s2] += mem[i].st[s2] - mem[j].st[s2];
                    if (bj >= 0) tot[bj][s2] += mem[j].st[s2] - mem[i].st[s2];
                  }
                  asg[i] = bi; asg[j] = bj;
                }
              }
            }
            return { asg, tot, capN, score: cur };
          };
          const perms = (arr, n) => {
            const out = [];
            const rec = (cur2, rest) => {
              if (cur2.length === n) { out.push(cur2); return; }
              for (let i = 0; i < rest.length; i++)
                rec(cur2.concat([rest[i]]), rest.slice(0, i).concat(rest.slice(i + 1)));
            };
            rec([], arr);
            return out;
          };
          // Crew move freely between in-port ships, so only the voyage SUBSET matters when
          // pairing; sailing ships are excluded from fresh solves entirely (freeShips).
          // ---- frozen plan --------------------------------------------------
          // ONE plan per set of offered voyages, and it NEVER auto-recomputes while those
          // voyages stand. Every earlier signature-based invalidation (roster fingerprints,
          // gear sigs) turned transient container/varc desyncs into replans that flipped the
          // plan per viewed screen. Drift now only re-renders the SAME stored target; the
          // Replan button forces a fresh solve when the user wants one.
          // No character name = no key; computing under a fallback key made a one-repaint
          // snapshot gap look like "no plan exists" and minted a fresh (possibly mirror) plan.
          const charName = (lastSnap && lastSnap.display_name) || '';
          if (!charName) return;
          const planKey = 'rtxPortsPlan:' + charName;
          const fpOf = m => m.id + ':' + m.st.join(',');
          const byId = {}; voys.forEach(v => { byId[v.vid] = v; });
          let plan = ppPlanByChar[charName] || null;
          if (!plan) { try { plan = JSON.parse(localStorage.getItem(planKey) || 'null'); } catch (e) {} }
          let replanWhy = plan ? '' : (ppReplanWhy || 'new');
          // Plan-shape validation must NOT reference live-derived values (shipList.length
          // flaps when the office/story varbits misread for one poll - that nulled plans).
          if (plan && Array.isArray(plan.order) && !plan.pairs)   // legacy shape: voyage k -> ship k
            plan.pairs = plan.order.map((vid, k) => ({ vid, b: k }));
          if (plan && !(Array.isArray(plan.pairs) && Array.isArray(plan.target)
                        && plan.pairs.length > 0 && plan.target.length > 0)) {
            plan = null; replanWhy = 'reset'; delete ppPlanByChar[charName];
          }
          if (plan) {
            // A written plan is IMMUTABLE. A voyage that left the offers just drops its row
            // (the ship sailed); unresolved-but-still-offered pauses. A fresh plan happens
            // ONLY when the slots read sanely AND every planned voyage is gone (new day), or
            // via the manual Replan button - transient var glitches can never replan.
            const rawIds = {}; let anySlot = false;
            for (const vbid of [17115, 17116, 17117, 17118, 17119, 17120]) {
              const id2 = ppV(vbid);
              rawIds[id2] = 1;
              if (id2 > 0) anySlot = true;
            }
            if (!anySlot) return;
            if (plan.pairs.every(p2 => !rawIds[p2.vid] && !byId[p2.vid])) {
              // Debounced: gone must persist for 5s of repaints before it counts as a new
              // day - a single glitched varbit poll can never mint a new plan.
              if (!ppPlanGoneAt) { ppPlanGoneAt = Date.now(); return; }
              if (Date.now() - ppPlanGoneAt < 5000) return;
              plan = null; replanWhy = 'new day'; delete ppPlanByChar[charName];
            } else {
              ppPlanGoneAt = 0;
              if (plan.pairs.some(p2 => !byId[p2.vid] && rawIds[p2.vid])) return;
            }
          }
          let best = null;
          if (plan) {
            // Rebuild the assignment from the stored target: same fingerprint on the right
            // ship first, then same fingerprint anywhere, then same item id (stats drifted,
            // e.g. a level-up). An unmatched spot stays unfilled rather than replanning.
            const asg = mem.map(() => -1);
            const used = {};
            // Members aboard a locked (sailing) ship are pinned there and can never be
            // claimed for another ship's target.
            mem.forEach((m, i) => {
              if (m.aboard >= 0 && shipLocked[m.aboard]) { used[i] = 1; asg[i] = m.aboard; }
            });
            const claim = (b, pred) => {
              for (let i = 0; i < mem.length; i++)
                if (!used[i] && pred(mem[i])) { used[i] = 1; asg[i] = b; return true; }
              return false;
            };
            for (let b = 0; b < shipList.length; b++) {
              if (shipLocked[b]) continue;   // its crew is fixed above
              for (const f of (plan.target[b] || [])) {
                const id2 = parseInt(f, 10);
                const hit = claim(b, m => m.aboard === b && fpOf(m) === f)
                         || claim(b, m => fpOf(m) === f)
                         || claim(b, m => m.id === id2);
                void hit;
              }
            }
            const tot = [], capN = [];
            for (const b of shipList) { tot.push(gearOf(b).slice()); capN.push(0); }
            mem.forEach((m, i) => {
              if (asg[i] < 0) return;
              for (let j = 0; j < 3; j++) tot[asg[i]][j] += m.st[j];
              if (m.cap) capN[asg[i]]++;
            });
            // Sailed/rerolled voyages render no row; their target crew is at sea anyway.
            best = { asg, tot, capN,
                     map: plan.pairs.map(p2 => ({ v: byId[p2.vid], b: p2.b })).filter(e2 => e2.v) };
          } else {
            // Fresh solve: voyages pair only onto ships that are IN PORT.
            const nFree = Math.min(freeShips.length, voys.length);
            if (!nFree) return;
            for (const vsel of perms(voys, nFree)) {
              const reqByShip = shipList.map(() => null);
              vsel.forEach((v, k) => { reqByShip[freeShips[k]] = { req: v.req, w: v.sp ? 10 : 1 }; });
              const r2 = runOpt(reqByShip);
              if (!best || r2.score > best.score)
                best = { asg: r2.asg, tot: r2.tot, capN: r2.capN, score: r2.score,
                         map: vsel.map((v, k) => ({ v, b: freeShips[k] })) };
            }
            if (!best) return;
            const mintN = ++ppPlanN;
            plan = { pairs: best.map.map(e2 => ({ vid: e2.v.vid, b: e2.b })),
                     target: shipList.map(b => mem.filter((m, i) => best.asg[i] === b).map(fpOf)),
                     created: Date.now(), why: replanWhy || 'new', n: mintN };
            ppPlanByChar[charName] = plan;       // module store = the real persistence
            ppReplanWhy = '';
            try { localStorage.setItem(planKey, JSON.stringify(plan)); } catch (e) {}
          }
          const anyGear = shipList.some(b => { const g2 = gearOf(b); return g2[0] || g2[1] || g2[2]; });
          // The plan's birth time is shown so any unexpected recompute is visible instantly -
          // if this clock ever changes without the Replan button, a replan leak came back.
          const planAge = plan && plan.created ? new Date(plan.created) : null;
          const planTxt = planAge ? ' · plan #' + ((plan && plan.n) || '?') + ' '
                                  + ('0' + planAge.getHours()).slice(-2) + ':'
                                  + ('0' + planAge.getMinutes()).slice(-2) + ':'
                                  + ('0' + planAge.getSeconds()).slice(-2)
                                  + (plan && plan.why ? ' (' + plan.why + ')' : '') : '';
          const hdr = row('Optimal assignment',
                          (anyGear ? 'crew effective stats + calibrated ship gear'
                                   : 'crew only - open each ship\'s crew window once to calibrate its gear')
                          + planTxt, null);
          hdr.dataset.tip = 'Coverage = worst stat vs the voyage requirement, from the game\'s own effective\n'
            + 'crew stats (script7312) plus each ship\'s gear constant, calibrated live from the\n'
            + 'crew window\'s own header totals.\n'
            + 'The plan is frozen until these voyages change - use the arrows button to replan.';
          {   // manual replan: the ONLY way to recompute while the same voyages are offered
            const rp = document.createElement('button');
            rp.className = 'pp-bell';
            rp.dataset.tip = 'Recompute the plan from scratch (uses current crew + calibrated gear)';
            rp.innerHTML = '<svg viewBox="0 0 24 24" width="12" height="12" fill="none" stroke="currentColor" stroke-width="2"><path d="M20 11a8 8 0 1 0-2.3 6"/><path d="M20 5v6h-6"/></svg>';
            rp.addEventListener('click', () => {
              ppReplanWhy = 'button';
              delete ppPlanByChar[charName];
              try { localStorage.removeItem(planKey); } catch (e) {}
              ppSig = '';
              renderPorts();
            });
            hdr.appendChild(rp);
          }
          yc.appendChild(hdr);
          for (const { v, b } of best.map.slice().sort((p, q) => (q.v.sp | 0) - (p.v.sp | 0))) {
            const bm = mem.filter((m, i) => best.asg[i] === b);
            const capM = bm.filter(m => m.cap)[0];
            const names = (capM ? ['Capt. ' + capM.nm] : []).concat(bm.filter(m => !m.cap).map(m => m.nm));
            const pct = best.capN[b] > 0 ? Math.floor(cov(best.tot[b], v.req) * 100) : 0;
            const need = ['morale', 'combat', 'seafaring']
              .map((lab, i) => v.req[i] > 0 ? lab + ' ' + best.tot[b][i] + '/' + v.req[i] : '')
              .filter(Boolean).join(' · ');
            const r = row((v.sp ? '★ ' : '') + v.name,
                          shLabel(b) + (shipLocked[b] ? ' (at sea)' : '') + ' · ' + names.join(', '),
                          pct + '%', pct >= 100 ? 'ok' : 'warn');
            r.dataset.tip = v.name + (v.sp ? ' (special voyage - prioritised)' : '') + '\n' + need;
            yc.appendChild(r);
          }
          // Refit scan: for ship b and a voyage requirement, the best part per equip slot -
          // OWNED parts as plain swaps, unowned-but-AFFORDABLE parts tagged with their cost.
          // Deltas swap the equipped part's stats within the ship totals; the shipwright %
          // multiplier is not modelled and each line is independent - totals recalibrate
          // from the crew window after refitting, so the plan corrects itself.
          const ownWordOf = { 1010: [3378, 0], 1011: [3379, 0], 1012: [17033, 1], 1013: [17034, 1] };
          const haveRes = rid => (rid >= 1 && rid <= 9) ? ppP(PP_RES[rid - 1][1]) : 0;
          const ppRefitScan = async (b, req, tot0) => {
            const cur0 = cov(tot0, req);
            const subs = [];
            const suggested = {};   // eid -> {idx: 1}: never offer one part to two slots
            for (let si = 0; si < PP_EQUIP.length; si++) {
              const eq = PP_EQUIP[si], slotNm = eq[0], eid = eq[1], vbs = eq[2];
              const parts = await ppPartList(eid);
              if (!parts || b >= vbs.length) continue;
              const curIdx = ppV(vbs[b]);
              const curP = parts.filter(p => p.idx === curIdx)[0];
              const curSt = curP ? curP.st : [0, 0, 0, 0];
              const own = ownWordOf[eid];
              const ownedBit = i2 => (((own[1] ? ppV(own[0]) : ppP(own[0])) >>> i2) & 1);
              const otherDeck = eid === 1011 ? ppV(PP_EQUIP[si === 1 ? 2 : 1][2][b]) : -1;
              const taken = suggested[eid] || (suggested[eid] = {});
              let bestP = null, bestC = cur0, bestOwned = false;
              for (const p of parts) {
                if (p.idx === curIdx || p.idx === otherDeck || taken[p.idx]) continue;
                const owned = !!ownedBit(p.idx);
                if (!owned && !p.cost.every(c2 => haveRes(c2[0]) >= c2[1])) continue;
                const t2 = [0, 1, 2].map(j => tot0[j] + p.st[j] - curSt[j]);
                const c2 = cov(t2, req);
                // an owned part wins ties against an equally good purchase
                if (c2 > bestC + 1e-9 || (owned && !bestOwned && bestP && c2 > bestC - 1e-9)) {
                  bestC = c2; bestP = p; bestOwned = owned;
                }
              }
              if (bestP && Math.floor(Math.min(bestC, 1) * 100) > Math.floor(Math.min(cur0, 1) * 100)) {
                taken[bestP.idx] = 1;
                subs.push(slotNm + ': ' + (curP ? curP.name : 'empty') + ' → ' + bestP.name
                          + ' (+' + (Math.floor(Math.min(bestC, 1) * 100) - Math.floor(Math.min(cur0, 1) * 100)) + '%)'
                          + (bestOwned ? '' : ' · buy: ' + (ppPartCost(bestP.cost) || 'free')));
              }
            }
            return subs;
          };
          const refitRow = (title, subs, tip) => {
            const r = row(title, null, String(subs.length), 'go');
            const nmEl = r.querySelector('.pp-nm');
            subs.forEach(s3 => {
              const d = document.createElement('div'); d.className = 'pp-sub'; d.textContent = s3;
              if (nmEl) nmEl.appendChild(d);
            });
            r.dataset.tip = tip;
            yc.appendChild(r);
          };
          const refitTip = 'Swap at the Shipwright. Gains are per-line estimates from raw part stats;\n'
                         + 'the shipwright % bonus is not modelled - the totals recalibrate after refitting.';
          // The voyage screen's SELECTED mission gets first-class treatment: whatever mission
          // the user is inspecting (vb 17129 tab / 17130 slot -> offer slot, vb 17132 = the
          // selected ship, per script2022), recommend the parts that raise ITS success.
          if ((ppV(17126) || ppV(17127) || ppV(17128)) && ppV(17130) >= 1 && ppV(17132) >= 1) {
            const slotVb = (ppV(17129) === 1 ? [17118, 17119, 17120] : [17115, 17116, 17117])[ppV(17130) - 1];
            const vShipB = ppV(17132) - 1;
            const vInfo = slotVb ? await ppVoyageInfo(ppV(slotVb)) : null;
            if (vInfo && vShipB >= 0 && vShipB < shipList.length
                && (vInfo.req[0] + vInfo.req[1] + vInfo.req[2]) > 0) {
              const t0 = gearOf(vShipB).slice();
              mem.forEach(m => { if (m.aboard === vShipB) for (let j = 0; j < 3; j++) t0[j] += m.st[j]; });
              const subs = await ppRefitScan(vShipB, vInfo.req, t0);
              if (subs.length) {
                refitRow('Refit for viewed voyage: ' + (vInfo.name || '?') + ' (' + shLabel(vShipB) + ')',
                         subs, refitTip);
              }
            }
          }
          // And the plan's pairing gets the same scan per ship (a ship at sea can't refit).
          for (const { v, b } of best.map) {
            if (!(best.capN[b] > 0) || shipLocked[b]) continue;
            const subs = await ppRefitScan(b, v.req, best.tot[b]);
            if (subs.length) refitRow('Refit ' + shLabel(b) + ' for ' + v.name, subs, refitTip);
          }
          // Assign/remove moves against the aboard masks. Order: removals (free sockets),
          // then CAPTAINS (the game refuses crew on a captainless ship), then crew.
          const steps = [];
          mem.forEach((m, i) => { if (m.aboard !== best.asg[i]) steps.push({ m, from: m.aboard, to: best.asg[i] }); });
          const stepRank = s2 => s2.to < 0 ? 0 : s2.m.cap ? 1 : 2;
          steps.sort((a2, b2) => stepRank(a2) - stepRank(b2));
          // Build the display view-model, then gate it behind double-read confirmation:
          // a list differing from the shown one renders only after two polls agree.
          let view = null;
          if (steps.length) {
            const m0 = steps[0].m;
            // Destination = the ship's free socket (captain socket = the block's first slot,
            // crew the other five). When the ship is FULL, the game replaces on assign, so
            // point at the socket of a member the plan moves OFF that ship instead.
            let destSl = -1, replaceNm = '';
            if (steps[0].to >= 0) {
              const b = steps[0].to, taken = {};
              mem.forEach(m2 => { if (m2.aboard === b) taken[m2.sl] = 1; });
              if (m0.cap) {
                const capLeaver = steps.filter(s4 => s4.from === b && s4.m.cap)[0];
                if (!taken[6 * b]) destSl = 6 * b;
                else if (capLeaver) { destSl = capLeaver.m.sl; replaceNm = capLeaver.m.nm; }
              } else {
                for (let s3 = 6 * b + 1; s3 <= 6 * b + 5; s3++) if (!taken[s3]) { destSl = s3; break; }
                if (destSl < 0) {
                  const leaver = steps.filter(s4 => s4.from === b && !s4.m.cap)[0];
                  if (leaver) { destSl = leaver.m.sl; replaceNm = leaver.m.nm; }
                }
              }
            }
            view = {
              srcSl: m0.sl, destSl,
              lines: steps.map((s2, mi) => {
                const what = s2.to < 0 ? 'take off ' + shLabel(s2.from)
                           : s2.from < 0 ? 'put on ' + shLabel(s2.to)
                           : shLabel(s2.from) + ' → ' + shLabel(s2.to);
                return (mi === 0 ? '▶ ' : '') + s2.m.nm + (s2.m.cap ? ' (captain)' : '')
                     + ' · Lv ' + lv(s2.m) + ' · ' + s2.m.st.join('/') + ' · ' + what
                     + (mi === 0 && replaceNm ? ' · replacing ' + replaceNm : '');
              }),
            };
          }
          const stepSig = view ? view.lines.join('\n') : '';
          if (ppStepsShownSig === null) {
            ppStepsShownSig = stepSig; ppStepsView = view;     // very first computation: accept
          } else if (stepSig !== ppStepsShownSig) {
            if (stepSig === ppStepsPendingSig) {
              ppStepsShownSig = stepSig; ppStepsView = view;   // confirmed twice: accept
            } else {
              ppStepsPendingSig = stepSig;                     // first sighting: keep showing the old list
              // Repaints are sig-gated, so one game change yields exactly ONE recompute -
              // without this the confirming second read only came from the minute tick
              // (up to 60s of "not responding"). Clearing ppSig makes the next poll
              // recompute; it keeps clearing until the pending list confirms or reverts.
              ppSig = '';
            }
          }
          const shown = (ppStepsShownSig === stepSig) ? view : ppStepsView;
          if (shown && shown.lines.length) {
            ppMoveTarget = { a: { sl: shown.srcSl }, b: shown.destSl >= 0 ? { sl: shown.destSl } : null };
            const r = row('Moves to make', null, String(shown.lines.length), 'go');
            const nmEl = r.querySelector('.pp-nm');
            shown.lines.forEach((ln2, mi) => {
              const d = document.createElement('div'); d.className = 'pp-sub' + (mi === 0 ? ' cur' : '');
              d.textContent = ln2;
              if (nmEl) nmEl.appendChild(d);
            });
            const tg = document.createElement('button');
            tg.className = 'pp-bell' + (ppMovesHi ? ' on' : '');
            tg.dataset.tip = 'Box the next member to move on the in-game crew screen';
            tg.innerHTML = '<svg viewBox="0 0 24 24" width="12" height="12" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="8"/><circle cx="12" cy="12" r="2"/></svg>';
            tg.addEventListener('click', () => {
              ppMovesHi = !ppMovesHi;
              try { localStorage.setItem('rtxPortsMovesHi', ppMovesHi ? '1' : '0'); } catch (e) {}
              tg.classList.toggle('on', ppMovesHi);
              ppMovesHiTick();
            });
            r.insertBefore(tg, r.querySelector('.pp-pill'));
            yc.appendChild(r);
          } else {
            ppMoveTarget = null;
            yc.appendChild(row('Crew placement already optimal', null, 'ok', 'ok'));
          }
        })();
      }
    }

    {
      const cw = sec('Crew for hire', 'new crew in ' + ppDayText());
      // vb 17461 = rerolls available (shown in place of the timer), vb 17420 = +5-reroll tokens.
      if (ppV(17461) > 0 || ppV(17420) > 0)
        cw.appendChild(row('Rerolls', ppV(17420) ? '+5 reroll tokens: ' + ppV(17420) : null,
                           ppV(17461) ? String(ppV(17461)) : 'none', ppV(17461) ? 'go' : null));
      const rows = (ppCrew || []).filter(r0 => (r0[1] | 0) > 0);
      if (!rows.length) {
        cw.appendChild(row('Hire list not loaded', 'Open the Crew Roster in game once to capture it', null));
      } else {
        if (ppCrewAt) {
          const mins = Math.floor((Date.now() - ppCrewAt) / 60000);
          const age = mins < 1 ? 'just now' : mins < 60 ? mins + 'm ago'
                    : Math.floor(mins / 60) + 'h ' + (mins % 60) + 'm ago';
          cw.appendChild(row('Last seen', 'captured from the Crew Roster window', age));
        }
        for (const r0 of rows) {
          const slot = r0[0] | 0, id = r0[1] | 0, nm = r0[3] || ('Item ' + id);
          const p = (ppItemParams[id] && ppItemParams[id].ints) || {};
          const stats = PP_CREW_STATS.map(([lab, k]) => (p[k] | 0) > 0 ? lab + ' ' + p[k] : '')
                                     .filter(Boolean).join(' · ');
          const traits = PP_CREW_TRAITS.filter(([k]) => (p[k] | 0) > 0).map(t => t[1]).join(', ');
          const cost = ppCrewCost(p);
          const r = row(nm + (slot === PP_CREW_CAPTAIN_SLOT ? ' (captain)' : ''),
                        (stats || 'no stats') + (traits ? '  ·  ' + traits : ''),
                        cost || 'free', cost ? null : 'ok');
          if (traits) r.dataset.tip = nm + '\nTraits: ' + traits;
          const spr = p[3080] | 0;
          if (spr > 0) {
            const ico = document.createElement('div'); ico.className = 'pp-ico';
            loadSpriteIcon(ico, spr, 40);
            r.insertBefore(ico, r.firstChild);
          }
          cw.appendChild(r);
        }
      }
    }

    {
      const vi = sec('Visitors', null);
      {   // Captain for hire + Black Market (the Port Visitors pane's other two slots)
        const capRow = row('Captain for hire', 'Resolving...', null);
        vi.appendChild(capRow);
        (async () => {
          const nm = await ppCaptainName();
          const sb = capRow.querySelector('.pp-sub');
          if (sb) sb.textContent = nm || 'No captain available';
        })();
        {   // The Trader: offered good + stock, computed price, affordability of one unit.
          const good = ppV(21322), stock = ppV(21323), pay = ppV(21324);
          if (good > 0) {
            const price  = ppTraderPrice(good, pay);
            const goodNm = (ppRewardEnum && ppRewardEnum[good]) || ('#' + good);
            const payNm  = (ppRewardEnum && ppRewardEnum[pay]) || ('#' + pay);
            const have   = (pay >= 1 && pay <= 9) ? ppP(PP_RES[pay - 1][1]) : 0;
            const afford = price > 0 && have >= price;
            const tr = row('The Trader',
              stock > 0 ? stock + ' ' + goodNm + (price ? ' at ' + price.toLocaleString() + ' ' + payNm + ' each' : '')
                        : 'Sold out · new stock in ' + ppDayText(),
              stock > 0 ? (afford ? 'can afford' : 'not enough') : null,
              stock > 0 ? (afford ? 'ok' : 'warn') : null);
            if (stock > 0 && price > 0)
              tr.dataset.tip = 'Buy 1 for ' + price.toLocaleString() + ' ' + payNm
                             + '\nBuy ' + stock + ' for ' + (price * stock).toLocaleString() + ' ' + payNm
                             + '\nYou have ' + have.toLocaleString() + ' ' + payNm;
            const ico = document.createElement('div'); ico.className = 'pp-ico';
            (async () => { const spr = await ppGoodSprite(good); if (spr > 0) loadSpriteIcon(ico, spr, 40); })();
            tr.insertBefore(ico, tr.firstChild);
            vi.appendChild(tr);
          }
        }
        const offer = ppMarketOffer();
        const bm = row('Black market', offer ? offer.name + ' · x ' + ppV(17463).toLocaleString()
                                             : 'No offer today', null);
        if (offer) {
          const ico = document.createElement('div'); ico.className = 'pp-ico';
          if (offer.spr) loadSpriteIcon(ico, offer.spr, 40);
          else if (offer.item) { const u = resolveIcon(offer.item); if (u) setIconBg(ico, u); }
          bm.insertBefore(ico, bm.firstChild);
        }
        vi.appendChild(bm);
      }
      // Visitor state 2 = In Port, 3 = Under Way; both count as "visiting" in the game's pane.
      const inPort = PP_VISITORS.filter(v2 => ppV(v2[1]) === 2).map(v2 => v2[0]);
      const away   = PP_VISITORS.filter(v2 => ppV(v2[1]) === 3).map(v2 => v2[0]);
      const parts = [];
      if (inPort.length) parts.push('In port: ' + inPort.join(', '));
      if (away.length)   parts.push('Under way: ' + away.join(', '));
      vi.appendChild(row('Adventurers', parts.length ? parts.join('  ·  ') : 'Nobody is visiting right now',
                         null, null, ppBell('visitor', 'Notify when an adventurer arrives')));
      const chips = document.createElement('div'); chips.className = 'pp-chips';
      for (const [name, vbid] of PP_VISITORS) {
        const st2 = ppV(vbid);
        const ch = document.createElement('span');
        ch.className = 'pp-chip' + (st2 === 2 ? ' on' : st2 === 3 ? ' sail' : '');
        ch.textContent = name;
        ch.dataset.tip = name + ': ' + (st2 === 2 ? 'in port' : st2 === 3 ? 'under way (on a voyage)' : st2 === 0 ? 'not yet met' : 'away');
        chips.appendChild(ch);
      }
      vi.appendChild(chips);
    }

    {
      const re = sec('Resources', null);
      const grid = document.createElement('div'); grid.className = 'pp-grid';
      for (const [name, vpid, spr] of PP_RES) {
        const cell = document.createElement('div'); cell.className = 'pp-cell'; cell.dataset.tip = name;
        const ico = document.createElement('div'); ico.className = 'pp-ico';
        loadSpriteIcon(ico, spr, 40);
        cell.appendChild(ico);
        const nm = document.createElement('div'); nm.className = 'pp-cnm'; nm.textContent = name; cell.appendChild(nm);
        const val = document.createElement('div'); val.className = 'pp-cv'; val.textContent = ppP(vpid).toLocaleString(); cell.appendChild(val);
        grid.appendChild(cell);
      }
      re.appendChild(grid);
    }

    {
      const tg = sec('Trade goods', null);
      const grid = document.createElement('div'); grid.className = 'pp-grid';
      PP_TRADE.forEach(([name, vbid, itemId], ti) => {
        const cell = document.createElement('div'); cell.className = 'pp-cell'; cell.dataset.tip = name;
        const ico = document.createElement('div'); ico.className = 'pp-ico';
        (async () => {   // the game's own icon (good ids 11-17); item icon fallback
          const spr = await ppGoodSprite(11 + ti);
          if (spr > 0) loadSpriteIcon(ico, spr, 40);
          else { const url = resolveIcon(itemId); if (url) setIconBg(ico, url); }
        })();
        cell.appendChild(ico);
        const nm = document.createElement('div'); nm.className = 'pp-cnm'; nm.textContent = name; cell.appendChild(nm);
        const val = document.createElement('div'); val.className = 'pp-cv'; val.textContent = ppV(vbid).toLocaleString(); cell.appendChild(val);
        grid.appendChild(cell);
      });
      tg.appendChild(grid);
    }

    {   // The Arc: daily cap on chimes earned by selling goods to Arc traders. vb 33141 counts
        // UP and matches the game's own "(19,999/20,000)" sale messages (live-validated 2026-07-30).
      const cap = 20000, sold = ppV(33141);
      const arc = sec('The Arc', 'resets in ' + ppDayText());
      arc.appendChild(row('Daily chime limit',
        sold >= cap ? 'Cap reached - sales earn no more chimes today' : 'Chimes earned from sales today',
        sold.toLocaleString() + ' / ' + cap.toLocaleString(), sold >= cap ? 'warn' : null));
      const bar = document.createElement('div'); bar.className = 'pp-bar';
      const fill = document.createElement('div'); fill.style.width = Math.max(0, Math.min(100, sold * 100 / cap)) + '%';
      bar.appendChild(fill); arc.appendChild(bar);
    }

    {
      const bu = sec('Buildings', null);
      for (const [name, vbid, eid] of PP_BUILDINGS) {
        const lv = ppV(vbid), pick = !!PP_PICK_ENUMS[eid];
        const r = row(name, 'Resolving...', pick ? (lv > 0 ? 'built' : 'empty') : 'tier ' + lv,
                      lv > 0 ? 'ok' : null);
        bu.appendChild(r);
        (async () => {
          const cur = await ppBuildTier(eid, lv);
          if (!pick) {                   // ladder buildings: progress in RANKS, not enum indices
            const rk = PP_TIER_RANKS[eid];
            const curRank = rk ? (rk[lv] != null ? rk[lv] : lv) : lv;
            const maxRank = rk ? Math.max.apply(null, rk) : await ppTierMax(eid);
            const pl = r.querySelector('.pp-pill');
            if (pl && maxRank > 0) {
              pl.textContent = 'tier ' + curRank + ' / ' + maxRank;
              if (curRank >= maxRank) pl.className = 'pp-pill ok';
            }
          }
          const sb = r.querySelector('.pp-sub');
          if (sb) sb.textContent = (cur && lv > 0) ? (cur.name + (cur.desc ? ' · ' + ppPlain(cur.desc) : ''))
                                                   : (pick ? 'nothing built here' : 'not built');
          const tip = [];
          if (cur && lv > 0) tip.push(cur.name + (cur.desc ? '\n' + ppPlain(cur.desc) : ''));
          if (pick) {        // a menu of decorations: show what CAN go here, not a "next tier"
            const opts = await ppTierList(eid);
            if (opts.length) tip.push('Options:\n' + opts.map(o =>
              '  ' + o.name + (o.cost ? '  (' + o.cost + ')' : '')).join('\n'));
          } else {
            // The next RANK can offer several alternatives, so list every level at that rank.
            const rk2 = PP_TIER_RANKS[eid];
            const curR = rk2 ? (rk2[lv] != null ? rk2[lv] : lv) : lv;
            const nextLevels = rk2 ? rk2.map((r, i) => [r, i]).filter(x => x[0] === curR + 1).map(x => x[1])
                                   : [lv + 1];
            const nexts = [];
            for (const nl of nextLevels) { const t = await ppBuildTier(eid, nl); if (t) nexts.push(t); }
            if (nexts.length === 1) {
              tip.push('NEXT: ' + nexts[0].name + (nexts[0].desc ? '\n' + ppPlain(nexts[0].desc) : '')
                       + (nexts[0].cost ? '\nCost: ' + nexts[0].cost : ''));
            } else if (nexts.length > 1) {
              tip.push('NEXT (choose one):\n' + nexts.map(t =>
                '  ' + t.name + (t.cost ? '  (' + t.cost + ')' : '')
                + (t.desc ? '\n     ' + ppPlain(t.desc) : '')).join('\n'));
            } else tip.push('Fully upgraded');
          }
          r.dataset.tip = tip.join('\n\n');
        })();
      }
    }
  }
