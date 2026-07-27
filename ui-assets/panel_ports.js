// RuneToolsX panel: Player-Owned Ports (voyages, ships, visitors, resources, buildings + alerts).
// Spliced inline into client.html at load; bare classic script sharing its one global scope.
// Var ids, enums and formulas mirror the game's own CS2 scripts (7353 ships, 7357 visitors,
// 7337 buildings, 7359 stats, 4187 daily clock).

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

  const PP_VB_IDS = (() => {
    const ids = new Set([17121, 17115, 17116, 17117, 17118, 17119, 17120, 17423, 17421,
                         17457, 17495, 17458,
                         17497,                 // focused Trader's scroll (index 1-34)
                         17220, 17224, 17225,   // captain for hire: name-enum picker + first/surname indices
                         17462, 17463,          // black market: offered resource id + stock
                         17461, 17420,          // crew rerolls available, +5-reroll tokens
                         21322, 21323, 21324]); // the Trader: offered good, stock, payment resource

    for (const s of PP_SHIPS) { s.nm.forEach(x => ids.add(x)); ids.add(s.voy); ids.add(s.ret); ids.add(s.idx); }
    PP_RETVBS.forEach(x => ids.add(x));
    PP_TRADE.forEach(t => ids.add(t[1]));
    PP_VISITORS.forEach(v2 => { ids.add(v2[1]); ids.add(v2[2]); ids.add(v2[3]); if (v2[4]) ids.add(v2[4]); });
    PP_BUILDINGS.forEach(b => ids.add(b[1]));
    PP_SCROLLS.forEach(x => ids.add(x));
    PP_PART_VBS.forEach(x => ids.add(x));
    return Array.from(ids).join(',');
  })();
  const PP_VP_IDS = PP_RES.map(r => r[1]).join(',') + ',3415,3378,3379';

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
        const cost = [[3132, 3133], [3134, 3135]].map(([rp, ap]) => {
          const r = I[rp] | 0, a = I[ap] | 0;
          return (r && a) ? a.toLocaleString() + ' ' + ((ppRewardEnum && ppRewardEnum[r]) || ('#' + r)) : '';
        }).filter(Boolean).join(' + ');
        ppTierCache[key] = { name: S['3118'] || '', desc: (S['3125'] || ''), cost };
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

  let ppNotify = {};                    // alert bells: ship / voyages / visitor
  try { ppNotify = JSON.parse(localStorage.getItem('rtxPortsNotify') || '{}') || {}; } catch (e) {}
  function ppNotifyAny() { for (const k in ppNotify) if (ppNotify[k]) return true; return false; }
  let ppPrev = null;                    // previous alert snapshot (edge detection)

  async function fetchPorts(bg) {
    if (!bridge() || !bridge().varbits || ppFetching) return;
    const now = Date.now();
    if (now - _ppAt < (bg ? 10000 : 2000)) return;
    _ppAt = now; ppFetching = true;
    try {
      const vb = JSON.parse(await bridge().varbits(myPid(), PP_VB_IDS) || 'null');
      if (vb && typeof vb === 'object' && Object.keys(vb).length) ppVb = vb;
      const vp = JSON.parse(await bridge().varps(myPid(), PP_VP_IDS) || 'null');
      if (vp && typeof vp === 'object') ppVp = vp;
      await ppLoadCrew();
      ppAlertTick();
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
          .pp-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 1px; }
          .pp-cell { display: flex; align-items: center; gap: 7px; padding: 7px 10px; }
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
                // the roster is a container, not a var: without it a reroll never repaints
                (ppCrew || []).map(r0 => r0[0] + ':' + r0[1]).join(',') + '|' +
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
        ov.appendChild(row('Focused scroll',
                           pieces >= 4 ? 'Complete - change it on the Port Management page' : null,
                           pieces + ' / 4', pieces >= 4 ? 'warn' : null));
      }
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
