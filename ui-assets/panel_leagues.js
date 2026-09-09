// RuneToolsX panel: Leagues. Live cache DBTables (CS2 ids, bridge dbRows): 326 headers (17039 Catalyst, 19883 Equilibrium), 327 tier order, 328 tiers, 329 relics/blessings, 333 trophies, 334 tasks, 335 config, 336 localities. 326: col 0 league number (= task membership column), 1/2 name/short, 6 points max, 7 interface group, 9 relic tier-order row, 10 blessing tier-order row, 12 trophies row, 13 config row, 22 region-unlock enum 9287, 28 points var_reference (L1 varp 12426, L2 varp 13521).
// 328: col 1 relic ids, 2 cost, 4 XP mult, 5/6/7 passive (ref, value, text), 10 unlock notes. 329: col 0/1 name/desc, 2 blessing cost, 3 alignment, 5 item, 7/8 icon sprites, 10..13 effect quads (ref, value, any-flag, text); blessing rows spill a col-9 tuple into col 10 (mkRelic right-aligns). 334 (post-Equilibrium): col 0 component id, 1..N per-league membership, 3 achievement id, 4 completion ref, 5 type, 6 locality id (336), 7 tier 1-5 (points enum 9332 = {1:10,2:30,3:80,4:200,5:400}), 8 members-only; text mirrors script20131.
(function () {

  lgData = null; let lgFetching = false; let lgFetchAt = 0; let lgSig = '';
  const LG_T = { header: 326, tierOrder: 327, tiers: 328, relics: 329, trophies: 333, tasks: 334, cfg: 335, cats: 336, regions: 382, regionInfo: 383 };
  const lgCol = (r, m, k) => { const t = (r && r[m]) || {}; return (t[k] && t[k][0] !== undefined) ? t[k][0] : null; };
  const lgList = (r, m, k) => { const t = (r && r[m]) || {}; return Array.isArray(t[k]) ? t[k] : []; };

  let lgCurNum = 0;            // selected league number; 0 = auto (newest with tasks)
  let lgSection = 'tasks';     // active sub-tab: tasks | regions | relics | blessings | trophies
  const lgPassOpen = {};       // "<kind><tierIndex>" -> tier passives expanded
  let lgVbVals = null;         // varbit id -> live value (current league's vars)
  let lgVpVals = {};           // varp id -> live value
  let lgAchById = null;        // achievement id -> def (incl. the hidden league entries)
  const lgFilter = { q: '', tier: 0, cat: -1, status: 'all' };
  let lgShowMax = 80, lgListDirty = false;

  // var_reference: varbit ((1<<24)|id) or raw varp id.
  const lgRefIsVb = ref => (ref >>> 24) === 1;
  const lgRefId = ref => lgRefIsVb(ref) ? (ref & 0xFFFFFF) : (ref >>> 0);
  function lgRefVal(ref) { return lgRefIsVb(ref) ? ((lgVbVals && lgVbVals[ref & 0xFFFFFF]) | 0) : (lgVpVals[ref >>> 0] | 0); }

  // Per-tier relic-pick vars: enum 9082 (tier -> var_reference, varbits 58410+); vb 58460 = bonus pick on tiers 1-3 (script20141). Tasks-completed counter for region unlocks: vb 58389 (Equilibrium, script21081).
  const LG_PICK_ENUM = 9082, LG_BONUS_VB = 58460;
  // League status vars (research/leagues2/LEAGUES2.md; bar maths = script20244): blessings progress = vb61497 - vb1668, span = vb61498 - vb1668; vb61499 blessing tier (15 = none); vb61498 next cost (255 = done); vb1668 previous cost (varp 13489).
  // Alignments: vb61678 Zamorak, vb61680 Guthix, vb61679 Saradomin. vb61518 relic resets remaining; vb61685 adrenaline costs zeroed; vb58531 ritual soul bonus percent; vb61500 / vb61501 next region threshold (2047 = done) / region tier.
  const LG_STATUS_VBS = [61497, 61498, 61499, 1668, 61678, 61679, 61680, 61518, 61685, 58531, 61500, 61501];
  const LG_TASKSDONE_VB = { 2: 58389 };
  // Region unlock state: varp 12327 is a locality bitmask (locality id = bit index; script20133 requires every bit of the region's table-382 col-1 enum).
  const LG_ACTIVE_VP = typeof VP !== 'undefined' ? VP.LEAGUE : 12314;
  let lgActiveLeague = null;   // varp 12314, null until read
  const LG_REGION_VP = 12327;
  const LG_REGION_MAXBIT = 41;                 // locality ids seen up to 40
  let lgRegionLow = null;     // bits 0-31, from the plain varp read
  let lgRegionHigh = null;    // bits 32-40, from the wide read minus the marker
  let lgPickVbs = null;        // tier index (0-based) -> varbit id
  let lgPoints = null;         // live league points (header var_reference)
  let lgEnums = {};            // enum id -> {k: v} via bridge enumInfo, fetched once

  function lgCur() {
    if (!lgData || !lgData.leagues.length) return null;
    if (lgCurNum) { const m = lgData.leagues.find(l => l.num === lgCurNum); if (m) return m; }
    const withTasks = lgData.leagues.filter(l => l.tasks.length);
    return (withTasks.length ? withTasks : lgData.leagues)[Math.max(0, (withTasks.length ? withTasks : lgData.leagues).length - 1)];
  }

  async function lgEnum(id) {
    if (!id) return null;
    if (lgEnums[id]) return lgEnums[id];
    if (bridge() && bridge().enumInfo) {
      try {
        const em = JSON.parse(await rtxData.raw('cache.enumInfo', id) || 'null');
        if (em && Object.keys(em).length) lgEnums[id] = em;
      } catch (e) {}
    }
    return lgEnums[id] || null;
  }

  async function fetchLeagues() {
    if (!bridge() || !bridge().dbRows || lgFetching) return;
    const t = Date.now();
    if (t - lgFetchAt < 5000) return;
    lgFetchAt = t; lgFetching = true;
    try {
      const grab = async id => { try { return JSON.parse(await rtxData.raw('cache.dbRows', id) || 'null') || []; } catch (e) { return []; } };
      const [hdrs, orders, tiers, relics, trophies, tasks, cfgs, cats, regions, regionInfos] = await Promise.all(
        [LG_T.header, LG_T.tierOrder, LG_T.tiers, LG_T.relics, LG_T.trophies, LG_T.tasks, LG_T.cfg, LG_T.cats,
         LG_T.regions, LG_T.regionInfo].map(grab));
      if (!hdrs.length || !tiers.length) return;          // cache not open yet; retry next poll
      const by = rows => { const m = {}; for (const r of rows) m[r.f] = r; return m; };
      const orderBy = by(orders), tierBy = by(tiers), relicBy = by(relics),
            trophyBy = by(trophies), cfgBy = by(cfgs), catBy = by(cats), regInfoBy = by(regionInfos);

      const regionCatalog = regions.map(r => {
        const info = regInfoBy[lgCol(r, 'i', '2')];
        let name = '';
        if (info) {
          for (const k in (info.s || {})) {
            for (const v of info.s[k]) {
              const m = /^The (.+?) region covers/.exec(v || '');
              if (m) { name = m[1]; break; }
            }
            if (name) break;
          }
        }
        return { row: r.f, name: name || ('Region ' + r.f), bitsEnum: lgCol(r, 'i', '1') | 0 };
      });

      const mkRelic = id => {
        const r = relicBy[id];
        if (!r) return { row: id, name: '#' + id, desc: '', fx: [] };
        const refsRaw = lgList(r, 'i', '10'), vals = lgList(r, 'i', '11'),
              bools = lgList(r, 'i', '12'), texts = lgList(r, 's', '13');
        const refs = refsRaw.slice(Math.max(0, refsRaw.length - vals.length));
        const fx = [];
        for (let i = 0; i < refs.length; i++)
          fx.push({ ref: refs[i] >>> 0, val: vals[i] | 0, any: (bools[i] | 0) === 1, txt: texts[i] || '' });
        return {
          row: id,
          name: lgCol(r, 's', '0') || ('#' + id),
          desc: lgCol(r, 's', '1') || '',
          item: lgCol(r, 'i', '5') | 0,
          cost: lgCol(r, 'i', '2') | 0,        // blessings only
          spr: lgCol(r, 'i', '7') | 0, sprOn: lgCol(r, 'i', '8') | 0,
          fx: fx
        };
      };
      const mkTiers = orderId => {
        const ord = orderBy[orderId];
        if (!ord) return [];
        return lgList(ord, 'i', '0').map(id => tierBy[id]).filter(Boolean).map(r => ({
          cost: lgCol(r, 'i', '2') | 0,
          xp: lgCol(r, 'i', '4') | 0,
          relics: lgList(r, 'i', '1').map(mkRelic),
          passives: lgList(r, 's', '7'),
          notes: lgList(r, 's', '10')
        }));
      };

      const leagues = [];
      for (const h of hdrs) {
        const num = lgCol(h, 'i', '0') | 0;
        if (!num) continue;
        const cfg = cfgBy[lgCol(h, 'i', '13')];
        const catRow = cfg ? catBy[lgList(cfg, 'i', '6')[2]] : null;
        const catName = {};
        if (catRow) {
          const ids = lgList(catRow, 'i', '4'), names = lgList(catRow, 's', '3');
          for (let i = 0; i < ids.length && i < names.length; i++)
            if (catName[ids[i]] === undefined) catName[ids[i]] = names[i];
        }
        const memberCol = String(num);
        const trophyRow = trophyBy[lgCol(h, 'i', '12')];
        leagues.push({
          num: num,
          name: lgCol(h, 's', '1') || ('League ' + num),
          sub: lgCol(h, 's', '2') || '',
          barMax: lgCol(h, 'i', '6') | 0,
          ptsRef: (lgCol(h, 'i', '28') | 0) >>> 0,
          hasRegions: (lgCol(h, 'i', '21') | 0) === 1,
          regionEnum: lgCol(h, 'i', '22') | 0,
          ptsEnum: cfg ? (lgList(cfg, 'i', '2')[0] | 0) : 0,
          tiers: mkTiers(lgCol(h, 'i', '9')),
          blessTiers: lgCol(h, 'i', '10') != null ? mkTiers(lgCol(h, 'i', '10')) : [],
          trophies: trophyRow ? lgList(trophyRow, 'i', '0').map((pts, i) => ({
            pts: pts | 0,
            item: lgList(trophyRow, 'i', '1')[i] | 0,
            name: lgList(trophyRow, 's', '2')[i] || ''
          })) : [],
          catName: catName,
          tasks: tasks.filter(r => (lgCol(r, 'i', memberCol) | 0) === 1).map(r => {
            const ref = (lgCol(r, 'i', '4') | 0) >>> 0;
            return {
              f: r.f,
              comp: lgCol(r, 'i', '0') | 0,
              c1: lgCol(r, 'i', '1') | 0,
              c2: lgCol(r, 'i', '2') | 0,
              ach: lgCol(r, 'i', '3') | 0,
              vb: lgRefIsVb(ref) ? (ref & 0xFFFFFF) : 0,
              type: lgCol(r, 'i', '5') | 0,
              cat: lgCol(r, 'i', '6') | 0,
              tier: lgCol(r, 'i', '7') | 0,
              f2p: (lgCol(r, 'i', '8') | 0) === 1
            };
          })
        });
      }
      leagues.sort((a, b) => a.num - b.num);
      lgData = { leagues: leagues, regions: regionCatalog };
      lgVbKick();
      lgEnsureAch();
    } finally { lgFetching = false; }
    paneRun('leagues', renderLeagues);
  }

  async function lgEnsureAch() {
    if (lgAchById) return;
    if (!achDefs && bridge() && bridge().achievements) {
      try { achDefs = JSON.parse(await rtxData.raw('cache.achievements')) || []; } catch (e) { achDefs = []; }
    }
    if (!achDefs || !achDefs.length) return;
    const map = {};
    for (const a of achDefs) map[a.id] = a;
    lgAchById = map; lgListDirty = true;
    lgVbKick();   // re-read live vars now that requirement varbits are known
  }
  const LG_QUEST_SUBCATS = { 4747: 1, 4748: 1 };
  const LG_MYST_SUBCATS = { 4882: 1, 4883: 1, 4884: 1, 4885: 1, 4886: 1, 4887: 1 };
  function lgTaskText(t) {
    const a = lgAchById && lgAchById[t.ach];
    if (a) {
      const nm = (a.name || '').trim(), ds = (a.desc || '').trim();
      if (LG_QUEST_SUBCATS[a.subcat] && nm) return 'Complete the quest: ' + nm + '.';
      if (LG_MYST_SUBCATS[a.subcat] && a.sprite === 10265 && nm) return 'Solve the Archaeology mystery: ' + nm + '.';
      return nm || ds || ('Task #' + t.f);
    }
    return 'Task #' + t.f;
  }
  function lgTaskPts(l, t) {
    const em = l.ptsEnum ? lgEnums[l.ptsEnum] : null;
    return (em && em[t.tier]) ? (em[t.tier] | 0) : 0;
  }

  async function lgPickEnsure() {
    if (lgPickVbs) return;
    const em = await lgEnum(LG_PICK_ENUM);
    if (em) {
      const m = {};
      for (const k in em) { const v = em[k] >>> 0; if ((v >>> 24) === 1) m[k | 0] = v & 0xFFFFFF; }
      if (Object.keys(m).length) lgPickVbs = m;
    }
  }
  async function lgVbKick() {
    const l = lgCur(); if (!l) return;
    await lgPickEnsure();
    await lgEnum(l.ptsEnum);
    await lgEnum(l.regionEnum);
    const vbs = new Set(), vps = new Set();
    const addRef = ref => (lgRefIsVb(ref) ? vbs : vps).add(lgRefId(ref));
    for (const t of l.tasks) {
      if (t.vb > 0) vbs.add(t.vb);
      const a = lgAchById && lgAchById[t.ach];
      if (a) for (const q of (a.reqs || [])) for (const vb of q.varbits) if (vb > 0) vbs.add(vb);
    }
    for (const tl of [l.tiers, l.blessTiers]) for (const tr of tl) for (const x of tr.relics) {
      for (const f of (x.fx || [])) addRef(f.ref);
    }
    if (lgPickVbs) { for (const k in lgPickVbs) vbs.add(lgPickVbs[k]); vbs.add(LG_BONUS_VB); }
    if (LG_TASKSDONE_VB[l.num]) vbs.add(LG_TASKSDONE_VB[l.num]);
    if (l.blessTiers.length) for (const v of LG_STATUS_VBS) vbs.add(v);
    if (l.ptsRef > 0) addRef(l.ptsRef);
    vps.add(LG_ACTIVE_VP);
    if (l.hasRegions && lgData.regions && lgData.regions.length) {
      vps.add(LG_REGION_VP);                       // 32-bit read: the proven source
      for (const rg of lgData.regions) await lgEnum(rg.bitsEnum);
    }
    const ids = [...vbs];
    const out = {};
    for (let i = 0; i < ids.length; i += 150) {
      try { Object.assign(out, await readVarbitValues(ids.slice(i, i + 150))); } catch (e) {}
    }
    let vpOut = {};
    if (vps.size && bridge().varps) {
      try { vpOut = JSON.parse(await rtxData.raw('state.varps', [...vps].join(','))) || {}; } catch (e) {}
    }
    lgVbVals = out; lgVpVals = vpOut;
    lgPoints = l.ptsRef > 0 ? lgRefVal(l.ptsRef) : null;
    lgActiveLeague = vpOut[LG_ACTIVE_VP] !== undefined ? (vpOut[LG_ACTIVE_VP] | 0) : null;
    if (l.hasRegions && vpOut[LG_REGION_VP] !== undefined) {
      lgRegionLow = (vpOut[LG_REGION_VP] | 0) >>> 0;
      lgRegionHigh = null;
      if (bridge().varpsLong) {
        try {
          const d = JSON.parse(await rtxData.raw('state.varpsLong', String(LG_REGION_VP))) || {};
          const raw = d[String(LG_REGION_VP)];
          if (raw !== undefined) {
            let u = Number(raw);
            if (isFinite(u)) {
              if (u < 0) u += 18446744073709551616;          // i64 -> unsigned
              let hi = Math.floor(u / 4294967296);           // exact: error << 2^32
              hi = hi % 2147483648;                          // drop the 0x80000000 marker
              if (hi >= 0 && hi < (1 << (LG_REGION_MAXBIT - 32))) lgRegionHigh = hi;
            }
          }
        } catch (e) {}
      }
    }
    lgListDirty = true;
    paneRun('leagues', renderLeagues);
  }

  function lgRelicActive(x) {
    if (!lgVbVals || !(x.fx || []).length) return false;
    for (const f of x.fx) {
      const v = lgRefVal(f.ref);
      if (f.any && v !== 0) return true;
      if (v < f.val) return false;
    }
    return true;
  }
  function lgRegionUnlocked(reg) {
    const em = reg.bitsEnum ? lgEnums[reg.bitsEnum] : null;
    if (!em || lgRegionLow === null) return null;
    let unknown = false;
    for (const k in em) {
      const bit = em[k] | 0;
      if (bit < 32) {
        if (!((lgRegionLow >>> bit) & 1)) return false;
      } else if (lgRegionHigh !== null) {
        if (!((lgRegionHigh >>> (bit - 32)) & 1)) return false;
      } else {
        unknown = true;              // wide read unavailable; keep checking the rest
      }
    }
    return unknown ? null : true;
  }
  function lgTierPicks(i) {   // script20141: pick var + the tiers-1-3 bonus
    if (!lgPickVbs || !lgVbVals) return 0;
    const vb = lgPickVbs[i];
    let n = vb ? ((lgVbVals[vb] | 0)) : 0;
    if (i <= 2 && ((lgVbVals[LG_BONUS_VB] | 0) === 1)) n += 1;
    return n;
  }
  function lgTaskState(t) {
    if (!lgVbVals) return { v: 0, tgt: 1, done: false, known: false, reqs: [] };
    const a = lgAchById && lgAchById[t.ach];
    const reqs = [];
    let reqDone = null;
    if (a && (a.reqs || []).length) {
      let sat = 0;
      for (const q of a.reqs) {
        let cur = 0;
        for (const vb of (q.varbits || [])) cur += (lgVbVals[vb] | 0);
        const ok = q.value > 0 ? cur >= q.value : cur > 0;
        if (ok) sat++;
        reqs.push({ cur: cur, tgt: Math.max(1, q.value | 0), ok: ok, desc: q.desc || '', vbs: (q.varbits || []).slice() });
      }
      const need = (a.needN && a.needN.length) ? a.needN.reduce((s, x) => s + x, 0) : reqs.length;
      reqDone = sat >= Math.min(need, reqs.length);
    }
    const vbv = t.vb ? (lgVbVals[t.vb] | 0) : 0;
    const vbIsCounter = reqs.some(r => r.vbs.indexOf(t.vb) >= 0);
    const flagDone = t.vb > 0 && !vbIsCounter && vbv >= 1;
    const done = (reqDone === null) ? flagDone : (reqDone || flagDone);
    let v = vbv, tgt = 1;
    if (reqs.length) { const show = reqs.find(r => !r.ok) || reqs[0]; v = show.cur; tgt = show.tgt; }
    return { v: v, tgt: tgt, done: done, known: !!(t.vb || reqs.length), reqs: reqs };
  }

  const LG_PINS_KEY = 'rtxLeaguePins';
  let lgPinsCache = null, lgPinToasts = {}, lgPinsAt = 0;
  function lgPinsList() {
    if (!lgPinsCache) {
      try { lgPinsCache = JSON.parse(localStorage.getItem(LG_PINS_KEY) || '[]') || []; }
      catch (e) { lgPinsCache = []; }
    }
    return lgPinsCache;
  }
  function lgPinHas(row) { return lgPinsList().indexOf(row) >= 0; }
  function lgPinSet(row, on) {
    lgPinsCache = lgPinsList().filter(x => x !== row);
    if (on) lgPinsCache.push(row);
    try { localStorage.setItem(LG_PINS_KEY, JSON.stringify(lgPinsCache)); } catch (e) {}
  }
  function lgPinToastSync(t) {
    const st = lgTaskState(t);
    const showN = st.tgt > 1 || st.v > 0;
    const msg = lgTaskText(t)
      + (showN ? '\u00a0\u00a0' + st.v.toLocaleString() + ' / ' + st.tgt.toLocaleString() : '');
    let rec = lgPinToasts[t.f];
    if (rec && rec.closing) {          // user hit the X: dismiss = untrack
      delete lgPinToasts[t.f];
      lgPinSet(t.f, false);
      lgListDirty = true;
      return;
    }
    if (!rec) {
      rec = uiNotify(msg, { sticky: true });
      if (rec) {
        lgPinToasts[t.f] = rec;
        if (rec.el) rec.el.classList.add('nodot');   // the left edge already carries the state
        if (st.done && rec.el) rec.el.classList.add('done');
        const x = rec.el && rec.el.querySelector('.toast-x');
        if (x) x.addEventListener('click', () => {
          delete lgPinToasts[t.f];
          lgPinSet(t.f, false);
          lgListDirty = true;
          paneRun('leagues', renderLeagues);
        });
      }
      return;
    }
    if (rec.msg !== msg) {
      rec.msg = msg;
      const b = rec.el && rec.el.querySelector('.toast-msg');
      if (b) b.textContent = msg;
    }
    if (rec.el) rec.el.classList.toggle('done', !!st.done);
  }
  async function lgPinsPoll() {
    const pins = lgPinsList();
    if (!pins.length || !bridge()) return;
    const now = Date.now();
    if (now - lgPinsAt < 2000) return;
    lgPinsAt = now;
    if (!lgData) { fetchLeagues(); return; }    // table data first; toasts next tick
    if (!lgAchById) lgEnsureAch();
    const tasks = [], seenRow = {};
    for (const l of lgData.leagues) for (const t of l.tasks) {
      if (seenRow[t.f] || pins.indexOf(t.f) < 0) continue;
      seenRow[t.f] = 1; tasks.push(t);
    }
    if (!tasks.length) return;
    const vbs = new Set();
    for (const t of tasks) {
      if (t.vb > 0) vbs.add(t.vb);
      const a = lgAchById && lgAchById[t.ach];
      if (a) for (const q of (a.reqs || [])) for (const vb of (q.varbits || [])) if (vb > 0) vbs.add(vb);
    }
    const ids = [...vbs];
    const out = {};
    for (let i = 0; i < ids.length; i += 150) {
      try { Object.assign(out, await readVarbitValues(ids.slice(i, i + 150))); } catch (e) { return; }
    }
    lgVbVals = Object.assign(lgVbVals || {}, out);
    for (const t of tasks) lgPinToastSync(t);
  }

  function lgVisibleTasks(l) {
    const q = lgFilter.q.toLowerCase();
    return l.tasks.filter(t => {
      if (lgFilter.tier && t.tier !== lgFilter.tier) return false;
      if (lgFilter.cat >= 0 && t.cat !== lgFilter.cat) return false;
      if (lgFilter.status !== 'all') {
        const st = lgTaskState(t);
        if (lgFilter.status === 'done' && !st.done) return false;
        if (lgFilter.status === 'todo' && st.done) return false;
        if (lgFilter.status === 'prog' && (st.done || !(st.reqs || []).some(q => q.cur > 0))) return false;
      }
      if (q) {
        const a = lgAchById && lgAchById[t.ach];
        const txt = (lgTaskText(t) + ' ' + (a ? (a.desc || '') : '') + ' '
          + (l.catName[t.cat] || '')).toLowerCase();
        if (txt.indexOf(q) < 0) return false;
      }
      return true;
    });
  }

  function lgPaintList() {
    const list = $('lgTaskList'), cap = $('lgTaskCap'), l = lgCur();
    if (!list || !l) return;
    const vis = lgVisibleTasks(l);
    let doneN = 0, ptsDone = 0, ptsAll = 0, untracked = 0;
    if (lgVbVals) for (const t of l.tasks) {
      const p = lgTaskPts(l, t); ptsAll += p;
      const st = lgTaskState(t);
      if (st.done) { doneN++; ptsDone += p; }
      if (!st.known) untracked++;
    }
    if (cap) cap.textContent = vis.length + ' of ' + l.tasks.length + ' tasks'
      + (lgVbVals ? ' · ' + doneN + ' done' + (ptsAll ? ' · ' + ptsDone.toLocaleString() + ' / ' + ptsAll.toLocaleString() + ' pts' : '') : '')
      + (untracked ? ' · ' + untracked + ' untracked' : '')
      + (lgAchById ? '' : ' · loading task text...');
    list.innerHTML = '';
    for (const t of vis.slice(0, lgShowMax)) {
      const st = lgTaskState(t);
      const r = document.createElement('div'); r.className = 'lg-row';
      const nm = document.createElement('div'); nm.className = 'lg-nm';
      const ttl = document.createElement('div');
      ttl.textContent = lgTaskText(t);
      if (st.done) ttl.style.color = 'var(--ok)';
      nm.appendChild(ttl);
      const sub = document.createElement('div'); sub.className = 'lg-sub';
      sub.textContent = (l.catName[t.cat] || ('Locality ' + t.cat)) + ' · Tier ' + t.tier;
      nm.appendChild(sub);
      r.appendChild(nm);
      const pin = document.createElement('button'); pin.type = 'button';
      const pinPaint = on => { pin.className = 'lg-pin' + (on ? ' on' : ''); pin.textContent = on ? '★' : '☆'; };
      pinPaint(lgPinHas(t.f));
      pin.dataset.tip = 'Pin: sticky alert with live progress';
      pin.addEventListener('click', e => {
        e.stopPropagation();
        const on = !lgPinHas(t.f);
        lgPinSet(t.f, on);
        pinPaint(on);
        if (on) { lgPinsAt = 0; lgPinsPoll(); }
        else { const rec = lgPinToasts[t.f]; if (rec) { delete lgPinToasts[t.f]; toastClose(rec); } }
      });
      r.appendChild(pin);
      const p = document.createElement('span'); p.className = 'lg-pill';
      const pts = lgTaskPts(l, t);
      p.textContent = (pts ? pts + ' pts' : 'Tier ' + t.tier)
        + (st.known ? (st.done ? ' · done' : (st.v > 0 ? ' · ' + st.v + '/' + st.tgt : '')) : '');
      if (st.done) { p.style.color = 'var(--ok)'; p.style.borderColor = 'var(--ok)'; }
      r.appendChild(p);
      const a = lgAchById && lgAchById[t.ach];
      const tipParts = [];
      if (a && (a.name || '').trim() && (a.desc || '').trim() && a.name.trim() !== a.desc.trim()) tipParts.push(lgCleanText(a.desc));
      for (const q of (st.reqs || [])) {
        tipParts.push((q.desc ? q.desc + ': ' : '') + q.cur.toLocaleString() + '/' + q.tgt.toLocaleString()
          + '  [vb ' + q.vbs.join('+') + ']' + (q.ok ? ' ✓' : ''));
      }
      const achDbg = lgAchById && lgAchById[t.ach];
      tipParts.push('Row ' + t.f + ' · achievement ' + t.ach
        + (t.vb ? ' · completion vb ' + t.vb + ' = ' + (lgVbVals ? (lgVbVals[t.vb] | 0) : '?') : ''));
      tipParts.push('Row cols: c1=' + t.c1 + ' c2=' + t.c2 + ' type=' + t.type
        + ' · ach reqs: ' + ((achDbg && achDbg.reqs && achDbg.reqs.length)
            ? achDbg.reqs.map(function (q) { return q.value; }).join(',') : 'none'));
      tipParts.push('F2P: db334.8=' + (t.f2p ? 1 : 0)
        + ' · ach.members=' + (achDbg ? (achDbg.members ? 1 : 0) : (lgAchById ? 'not-in-map' : 'defs-not-loaded'))
        + ' -> ' + ((t.f2p && !(achDbg && achDbg.members)) ? 'F2P' : 'members'));
      r.dataset.tip = tipParts.join('\n');
      list.appendChild(r);
    }
    if (vis.length > lgShowMax) {
      const more = document.createElement('div'); more.className = 'lg-row';
      more.style.cursor = 'pointer'; more.style.justifyContent = 'center';
      more.textContent = 'Show ' + Math.min(200, vis.length - lgShowMax) + ' more (' + (vis.length - lgShowMax) + ' hidden)';
      more.addEventListener('click', () => { lgShowMax += 200; lgPaintList(); });
      list.appendChild(more);
    }
  }

  function lgCleanText(s) {
    return String(s == null ? '' : s).replace(/<\/?br\s*\/?>/gi, '\n').replace(/<[^>]*>/g, '').trim();
  }

  function lgPaintTierList(tc, tierList, kind, live) {
    tc.innerHTML = '';
    if (!live) {
      const n = document.createElement('div'); n.className = 'lg-row';
      const t0 = document.createElement('div'); t0.className = 'lg-nm';
      const h = document.createElement('div'); h.textContent = 'Past league - picks not shown';
      const s = document.createElement('div'); s.className = 'lg-sub';
      s.textContent = 'Which ' + (kind === 'relic' ? 'relics' : 'blessings') + ' you chose is not recoverable: '
        + 'the game marks them from their effect vars, and those are shared with the current league '
        + 'and cleared when a league ends.';
      t0.appendChild(h); t0.appendChild(s); n.appendChild(t0); tc.appendChild(n);
    }
    tierList.forEach((t, i) => {
      const reached = live && kind === 'relic' && lgPoints != null && lgPoints >= t.cost;
      const picks = live && kind === 'relic' ? lgTierPicks(i) : 0;
      const anyActive = live && t.relics.some(x => lgRelicActive(x));
      const r = document.createElement('div'); r.className = 'lg-row';
      const nm = document.createElement('div'); nm.className = 'lg-nm';
      const ttl = document.createElement('div');
      ttl.textContent = 'Tier ' + (i + 1) + (t.xp > 1 ? ' · ' + t.xp + 'x XP' : '');
      nm.appendChild(ttl);
      const chips = document.createElement('div'); chips.className = 'lg-relics';
      for (const x of t.relics) {
        const on = live && lgRelicActive(x);
        const ch = document.createElement('span'); ch.className = 'lg-relic' + (on ? ' on' : '');
        const sprId = on && x.sprOn ? x.sprOn : x.spr;
        if (sprId || x.item) {
          const ic = document.createElement('span'); ic.className = 'lg-rico';
          if (sprId) loadSpriteIcon(ic, sprId, 36); else attachBankIcon(ic, x.item);
          ch.appendChild(ic);
        }
        const tx = document.createElement('span'); tx.textContent = x.name; ch.appendChild(tx);
        const tip = [x.name + (on ? ' (active)' : '')];
        if (x.desc) tip.push(lgCleanText(x.desc));
        for (const f of (x.fx || [])) if (f.txt) tip.push('- ' + lgCleanText(f.txt));
        tip.push('Row ' + x.row);
        ch.dataset.tip = tip.join('\n');
        chips.appendChild(ch);
      }
      nm.appendChild(chips);
      if (t.passives.length || t.notes.length) {
        const key = kind + i, open = !!lgPassOpen[key];
        const sb = document.createElement('div'); sb.className = 'lg-sub lg-exp';
        sb.textContent = (open ? '▾ ' : '▸ ') + (t.passives.length
          ? t.passives.length + ' tier passive' + (t.passives.length === 1 ? '' : 's')
          : t.notes.length + ' note' + (t.notes.length === 1 ? '' : 's'));
        sb.addEventListener('click', ev => {
          ev.stopPropagation();
          lgPassOpen[key] = !open;
          lgPaintTiers();
        });
        nm.appendChild(sb);
        if (open) {
          const box = document.createElement('div'); box.className = 'lg-pass';
          const line = (txt, cls) => {
            const d = document.createElement('div'); d.className = 'lg-pass-it' + (cls ? ' ' + cls : '');
            d.textContent = '• ' + lgCleanText(txt);
            box.appendChild(d);
          };
          for (const p of t.passives) line(p);
          for (const n2 of t.notes) line(n2, 'lg-note');
          nm.appendChild(box);
        }
      }
      r.appendChild(nm);
      if (reached && picks > 0 && !anyActive) {
        const av = document.createElement('span'); av.className = 'lg-pill lg-avail';
        av.textContent = 'choice available';
        r.appendChild(av);
      }
      const p = document.createElement('span'); p.className = 'lg-pill';
      p.textContent = kind === 'relic' ? t.cost.toLocaleString() + ' pts' : 'unlock ' + t.cost;
      if (reached) { p.style.color = 'var(--ok)'; p.style.borderColor = 'var(--ok)'; }
      r.appendChild(p);
      tc.appendChild(r);
    });
  }

  function lgPaintTiers() {
    const l = lgCur(), hd = $('lgHead');
    if (!l) return;
    if (hd) hd.textContent = l.name + (l.sub ? ' · ' + l.sub : '') + (lgPoints != null ? ' · ' + lgPoints.toLocaleString() + ' pts' : '');
    const live = (lgActiveLeague === null) || (lgActiveLeague === l.num);
    const tc = $('lgTierCard');
    if (tc) lgPaintTierList(tc, l.tiers, 'relic', live);
    const bc = $('lgBlessCard');
    if (bc) lgPaintTierList(bc, l.blessTiers, 'blessing', live);
    const trc = $('lgTrophyCard');
    if (trc) {
      trc.innerHTML = '';
      for (const tr of l.trophies) {
        const r = document.createElement('div'); r.className = 'lg-row';
        const nm = document.createElement('div'); nm.className = 'lg-nm';
        const w = document.createElement('div'); w.style.display = 'flex'; w.style.alignItems = 'center'; w.style.gap = '6px';
        if (tr.item) { const ic = document.createElement('span'); ic.className = 'lg-rico'; attachBankIcon(ic, tr.item); w.appendChild(ic); }
        const tx = document.createElement('span'); tx.textContent = tr.name || ('Item ' + tr.item); w.appendChild(tx);
        nm.appendChild(w);
        r.appendChild(nm);
        const p = document.createElement('span'); p.className = 'lg-pill';
        p.textContent = tr.pts.toLocaleString() + ' pts';
        if (lgPoints != null && lgPoints >= tr.pts) { p.style.color = 'var(--ok)'; p.style.borderColor = 'var(--ok)'; }
        r.appendChild(p);
        r.dataset.tip = 'Item ' + tr.item;
        trc.appendChild(r);
      }
    }
    const rgc = $('lgRegionCard');
    if (rgc) {
      rgc.innerHTML = '';
      const em = l.regionEnum ? lgEnums[l.regionEnum] : null;
      const doneVb = LG_TASKSDONE_VB[l.num];
      const done = (doneVb && lgVbVals) ? (lgVbVals[doneVb] | 0) : null;
      if (em) {
        const keys = Object.keys(em).sort((a, b) => a - b);
        keys.forEach((k, i) => {
          const need = em[k] | 0;
          const r = document.createElement('div'); r.className = 'lg-row';
          const nm = document.createElement('div'); nm.className = 'lg-nm';
          nm.textContent = i === 0 ? 'Karamja' : 'Region choice ' + i;
          r.appendChild(nm);
          const p = document.createElement('span'); p.className = 'lg-pill';
          p.textContent = need + ' tasks' + (done != null ? ' · ' + Math.min(done, need) + '/' + need : '');
          if (done != null && done >= need) { p.style.color = 'var(--ok)'; p.style.borderColor = 'var(--ok)'; }
          r.appendChild(p);
          rgc.appendChild(r);
        });
        if (done != null) {
          const cap = document.createElement('div'); cap.className = 'lg-row';
          cap.textContent = done.toLocaleString() + ' league tasks completed';
          cap.style.color = 'var(--text-dim)';
          rgc.appendChild(cap);
        }
        if (lgData.regions && lgData.regions.length && lgRegionLow !== null) {
          const r = document.createElement('div'); r.className = 'lg-row';
          const nm = document.createElement('div'); nm.className = 'lg-nm';
          const head = document.createElement('div'); head.textContent = 'Unlocked regions'; nm.appendChild(head);
          const chips = document.createElement('div'); chips.className = 'lg-relics';
          let unknownN = 0;
          for (const rg of lgData.regions) {
            const st = lgRegionUnlocked(rg);
            if (st === null) unknownN++;
            const ch = document.createElement('span');
            ch.className = 'lg-relic' + (st === true ? ' on' : st === null ? ' unk' : '');
            const tx = document.createElement('span'); tx.textContent = rg.name; ch.appendChild(tx);
            const bits = Object.keys(lgEnums[rg.bitsEnum] || {}).map(k => lgEnums[rg.bitsEnum][k]);
            ch.dataset.tip = rg.name
              + (st === null ? ' (unknown: needs locality bit 32+, which is not readable yet)'
                             : (st ? ' (unlocked)' : ' (locked)'))
              + '\nRow ' + rg.row + ' · localities ' + bits.join(', ');
            chips.appendChild(ch);
          }
          nm.appendChild(chips);
          if (unknownN) {
            const note = document.createElement('div'); note.className = 'lg-sub';
            note.textContent = unknownN + ' region' + (unknownN === 1 ? '' : 's')
              + ' need a locality bit above 31, which this build cannot read yet - shown as unknown rather than locked.';
            nm.appendChild(note);
          }
          r.appendChild(nm);
          rgc.appendChild(r);
        }
      }
    }
  }

  function lgPaintStatus() {
    const el = document.getElementById('lgStatus'); if (!el) return;
    const l = lgCur(); if (!l || !l.blessTiers.length) { el.style.display = 'none'; return; }
    const vb = lgVbVals;
    if (!vb) { el.innerHTML = '<div class="lg-empty">Reading league vars... (be in-world)</div>'; return; }
    const v = id => vb[id] | 0;
    const rows = [];
    const row = (k, val) => rows.push('<div class="lg-strow"><span>' + k + '</span><b>' + val + '</b></div>');
    const tierIdx = v(61499), nextCost = v(61498), prevCost = v(1668), done = v(61497);
    if (nextCost === 255) row('Blessings', 'all tiers unlocked · ' + done + ' tasks');
    else {
      const span = nextCost - prevCost, prog = done - prevCost;
      row('Blessing tier', (tierIdx === 15 ? 'none' : '#' + (tierIdx + 1)) + ' · ' + done + ' tasks · next at ' + nextCost
        + (span > 0 ? ' (' + Math.max(0, prog) + '/' + span + ')' : ''));
    }
    const za = v(61678), gu = v(61680), sa = v(61679);
    if (za || gu || sa) row('Alignment', 'Zamorak ' + za + ' · Guthix ' + gu + ' · Saradomin ' + sa);
    if (vb[61518] !== undefined) row('Resets left (relics & blessings)', v(61518) + ' / 4');
    if (l.tiers.length && lgPoints != null) {
      let active = 0, effects = 0;
      for (const t of l.tiers) if (lgPoints >= t.cost) { active++; effects += (t.passives || []).length; }
      const extra = [];
      if (v(61685) === 1) extra.push('free adrenaline');
      if (v(58531) > 0) extra.push('+' + v(58531) + '% ritual souls');
      row('Passive tiers', active + ' / ' + l.tiers.length + ' active · ' + effects + ' effects live'
        + (extra.length ? ' · ' + extra.join(' · ') : ''));
    }
    if (vb[61500] !== undefined && v(61500) !== 2047)
      row('Next region unlock', 'at ' + v(61500) + ' tasks (region tier ' + v(61501) + ')');
    el.style.display = '';
    el.innerHTML = rows.length ? rows.join('') : '<div class="lg-empty">No league status vars yet (be in-world on a league character).</div>';
  }
  function renderLeagues() {
    const c = $('content');
    let wrap = $('lgWrap');
    if (!wrap) {
      c.innerHTML = ''; lgSig = '';
      injectStyle('lgCss', `
        .lg-sec { color: var(--accent-hi); font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: .05em; padding: 14px 2px 6px; }
        .lg-wrap { margin: 2px 12px 0; }
        .lg-card { background: var(--bg-elev); border: 1px solid var(--border); border-radius: 10px; }
        .lg-strow { display: flex; align-items: center; justify-content: space-between; gap: 10px; padding: 6px 12px; font-size: 12px; }
        .lg-strow + .lg-strow { border-top: 1px solid var(--border); }
        .lg-strow span { color: var(--text-dim); }
        .lg-strow b { color: var(--text); font-weight: 600; text-align: right; }
        .lg-row { display: flex; align-items: flex-start; gap: 10px; padding: 8px 12px; }
        .lg-row + .lg-row { border-top: 1px solid var(--border); }
        .lg-nm { flex: 1; min-width: 0; color: var(--text); font-size: 12px; }
        .lg-sub { color: var(--text-mute); font-size: 10.5px; margin-top: 2px; }
        .lg-pill { font-size: 10.5px; padding: 2px 9px; border-radius: 999px; border: 1px solid var(--border); color: var(--text-dim); white-space: nowrap; font-variant-numeric: tabular-nums; flex: 0 0 auto; }
        .lg-empty { margin: 10px 14px; color: var(--text-dim); font-size: 12px; }
        .lg-ctl { display: flex; flex-wrap: wrap; gap: 5px; margin: 6px 0 8px; }
        .lg-searchbox { position: relative; flex: 1 1 130px; display: flex; min-width: 0; }
        .lg-search { flex: 1 1 130px; height: 26px; padding: 0 9px; background: var(--bg-elev); border: 1px solid var(--border); border-radius: 7px; color: var(--text); font-size: 11.5px; outline: none; }
        .lg-search:focus { border-color: var(--border-hi); }
        /* the field owns the row's flex; leave room on the right for the x */
        .lg-searchbox .lg-search { flex: 1 1 auto; min-width: 0; padding-right: 22px; }
        .lg-clear { position: absolute; right: 1px; top: 0; height: 26px; display: flex; align-items: center;
                    padding: 0 6px; border: none; background: none; color: var(--text-mute);
                    font-size: 15px; line-height: 1; cursor: pointer; }
        .lg-clear:hover { color: var(--text); }
        .lg-cap { color: var(--text-mute); font-size: 10.5px; margin: 2px 2px 6px; }
        .lg-tabs { display: flex; gap: 5px; margin: 4px 0 2px; flex-wrap: wrap; }
        .lg-subtabs { margin: 2px 0 8px; }
        .lg-tab { font-size: 11px; padding: 4px 12px; border-radius: 999px; border: 1px solid var(--border); background: var(--bg-elev); color: var(--text-dim); cursor: pointer; }
        .lg-tab.on { border-color: var(--accent-hi); color: #fff; background: linear-gradient(90deg, rgba(var(--accent-rgb),0.28), rgba(var(--accent-rgb),0.10)); }
        /* A GRID, not flex-wrap: relics are equal-rank choices, and variable-width
           chips wrapped ragged (a tier of three broke 2 + 1 with the orphan a
           different width). Equal columns line the tiers up with each other, and
           auto-fit puts a whole tier on one row as soon as the window is wide
           enough. */
        .lg-relics { display: grid; grid-template-columns: repeat(auto-fit, minmax(132px, 1fr)); gap: 5px; margin-top: 6px; }
        .lg-relic { display: flex; align-items: center; gap: 6px; min-width: 0; padding: 3px 8px 3px 4px; border: 1px solid var(--border); border-radius: 8px; background: var(--bg-elev-2); font-size: 11px; color: var(--text); }
        .lg-relic > span:last-child { min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
        .lg-relic.on { border-color: var(--accent-hi); background: linear-gradient(90deg, rgba(var(--accent-rgb),0.28), rgba(var(--accent-rgb),0.10)); color: #fff; }
        /* unknown, NOT locked: a dashed edge reads as "no answer" at a glance */
        .lg-relic.unk { border-style: dashed; color: var(--text-mute); }
        .lg-avail { color: var(--warn, #fbbf24); border-color: var(--warn, #fbbf24); }
        .lg-exp { cursor: pointer; }
        .lg-exp:hover { color: var(--text); }
        .lg-pass { display: flex; flex-direction: column; gap: 2px; margin: 4px 0 1px; }
        .lg-pass-it { color: var(--text-dim); font-size: 10.5px; line-height: 1.4; }
        .lg-pass-it.lg-note { color: var(--warn, #fbbf24); }
        .lg-pin { background: none; border: none; padding: 0 3px; margin-top: -1px; flex: 0 0 auto;
                  font-size: 14px; line-height: 1; color: var(--text-mute); cursor: pointer; }
        .lg-pin:hover { color: var(--text); }
        .lg-pin.on { color: #f0b429; }
        .lg-rico { width: 18px; height: 18px; flex: 0 0 auto; background-size: contain; background-repeat: no-repeat; background-position: center; }`);
      wrap = document.createElement('div'); wrap.id = 'lgWrap'; wrap.className = 'lg-wrap'; c.appendChild(wrap);
    }
    const d = lgData, l = lgCur();
    if (!d || !l) {
      if (lgSig !== 'wait') { lgSig = 'wait'; wrap.innerHTML = '<div class="lg-empty">Reading league data from the cache... (be in-world). Before a league is live the cache holds placeholder data only.</div>'; }
      return;
    }
    const sections = [];
    if (l.tasks.length) sections.push(['tasks', 'Tasks']);
    if (l.hasRegions && l.regionEnum) sections.push(['regions', 'Regions']);
    if (l.tiers.length) sections.push(['relics', 'Relics']);
    if (l.blessTiers.length) sections.push(['blessings', 'Blessings']);
    if (l.trophies.length) sections.push(['trophies', 'Trophies']);
    if (!sections.length) sections.push(['tasks', 'Tasks']);
    if (!sections.some(s => s[0] === lgSection)) lgSection = sections[0][0];

    const sig = l.num + '|' + l.name + '|' + l.tasks.length + '|'
      + l.tiers.map(t => t.cost + ':' + t.relics.length).join(',') + '|' + l.blessTiers.length
      + '|' + lgSection;
    if (sig === lgSig) {
      if (!lgAchById) lgEnsureAch();   // achievements may load after the tables did
      if (lgListDirty) { lgListDirty = false; lgPaintTiers(); lgPaintList(); lgPaintStatus(); }
      return;
    }
    lgSig = sig;
    wrap.innerHTML = '';
    const sec = t => { const e = document.createElement('div'); e.className = 'lg-sec'; e.textContent = t; wrap.appendChild(e); };
    const card = () => { const e = document.createElement('div'); e.className = 'lg-card'; wrap.appendChild(e); return e; };

    if (d.leagues.length > 1) {
      const tabs = document.createElement('div'); tabs.className = 'lg-tabs';
      for (const lg of d.leagues) {
        const b = document.createElement('button'); b.type = 'button';
        b.className = 'lg-tab' + (lg.num === l.num ? ' on' : '');
        b.textContent = lg.sub || ('League ' + lg.num);
        b.dataset.tip = lg.name + (lg.tasks.length ? '' : '\n(no task data in cache)');
        b.addEventListener('click', () => {
          if (lgCurNum === lg.num) return;
          lgCurNum = lg.num;
          lgFilter.cat = -1; lgFilter.tier = 0; lgShowMax = 80;
          lgVbVals = null; lgPoints = null; lgSig = '';
          lgVbKick();
          paneRun('leagues', renderLeagues);
        });
        tabs.appendChild(b);
      }
      wrap.appendChild(tabs);
    }

    sec(l.name + (l.sub ? ' · ' + l.sub : ''));
    wrap.lastChild.id = 'lgHead';
    if (l.blessTiers.length) {
      const st = document.createElement('div'); st.className = 'lg-card'; st.id = 'lgStatus';
      wrap.appendChild(st);
      lgPaintStatus();
    }

    if (sections.length > 1) {
      const stabs = document.createElement('div'); stabs.className = 'lg-tabs lg-subtabs';
      for (const [key, label] of sections) {
        const b = document.createElement('button'); b.type = 'button';
        b.className = 'lg-tab' + (key === lgSection ? ' on' : '');
        b.textContent = label;
        b.addEventListener('click', () => {
          if (lgSection === key) return;
          lgSection = key;
          lgShowMax = 80;
          paneRun('leagues', renderLeagues);   // sig carries the section: rebuilds
        });
        stabs.appendChild(b);
      }
      wrap.appendChild(stabs);
    }

    if (lgSection === 'relics')    { const tc = card();  tc.id  = 'lgTierCard'; }
    if (lgSection === 'blessings') { const bc = card();  bc.id  = 'lgBlessCard'; }
    if (lgSection === 'regions')   { const rc = card();  rc.id  = 'lgRegionCard'; }
    if (lgSection === 'trophies')  { const trc = card(); trc.id = 'lgTrophyCard'; }
    lgPaintTiers();
    if (lgSection !== 'tasks') return;

    const ctl = document.createElement('div'); ctl.className = 'lg-ctl';
    const sbox = document.createElement('div'); sbox.className = 'lg-searchbox';
    const inp = document.createElement('input'); inp.className = 'lg-search'; inp.type = 'text';
    inp.placeholder = 'Search tasks, objectives, localities...'; inp.value = lgFilter.q;
    const clr = document.createElement('button'); clr.type = 'button'; clr.className = 'lg-clear';
    clr.textContent = '×'; clr.dataset.tip = 'Clear search (Esc)';
    const syncClr = () => { clr.style.display = inp.value ? '' : 'none'; };
    const applyQ = () => { lgFilter.q = inp.value.trim(); lgShowMax = 80; syncClr(); lgPaintList(); };
    inp.addEventListener('input', applyQ);
    inp.addEventListener('keydown', e => {
      const esc = e.key === 'Escape' || e.key === 'Esc' || e.keyCode === 27 || e.which === 27;
      if (!esc || !inp.value) return;
      e.stopPropagation();
      e.preventDefault();
      inp.value = ''; applyQ();
    });
    clr.addEventListener('click', () => { inp.value = ''; applyQ(); inp.focus(); });
    syncClr();
    sbox.appendChild(inp); sbox.appendChild(clr);
    ctl.appendChild(sbox);
    const mkSel = (opts, cur, set) => {
      const b = document.createElement('button'); b.type = 'button'; b.className = 'al-typesel';
      let curV = String(cur);
      const label = () => { const o = opts.find(o2 => String(o2[0]) === curV); return o ? o[1] : String(curV); };
      const txt = document.createElement('span'); txt.textContent = label();
      const car = document.createElement('span'); car.className = 'cv'; car.textContent = '▾';
      b.appendChild(txt); b.appendChild(car);
      b.addEventListener('click', e => {
        e.stopPropagation();
        const old = document.getElementById('lgMenu');
        if (old) { old.remove(); try { wmRectsSoon(); } catch (e2) {} return; }
        const pop = document.createElement('div'); pop.className = 'sndmenu'; pop.id = 'lgMenu';
        for (const [v, lab] of opts) {
          const it = document.createElement('div');
          it.className = 'sndmenu-it' + (String(v) === curV ? ' sel' : '');
          it.textContent = lab;
          it.addEventListener('click', () => {
            pop.remove(); curV = String(v); txt.textContent = label();
            try { wmRectsSoon(); } catch (e2) {}
            set(v); lgShowMax = 80; lgPaintList();
          });
          pop.appendChild(it);
        }
        document.body.appendChild(pop);          // body-level so the scroll pane can't clip it
        placeMenu(pop, b);
        try { wmRectsSoon(); } catch (e2) {}
        const selIt = pop.querySelector('.sndmenu-it.sel'); if (selIt) selIt.scrollIntoView({ block: 'nearest' });
      });
      return b;
    };
    if (!renderLeagues._lgMenuBound) {
      renderLeagues._lgMenuBound = true;
      const inside = e => !!(e && e.target && e.target.nodeType === 1 && e.target.closest && e.target.closest('#lgMenu'));
      const close = () => { const m = document.getElementById('lgMenu'); if (m) { m.remove(); try { wmRectsSoon(); } catch (e2) {} } };
      document.addEventListener('click', e => { if (!inside(e)) close(); });
      document.addEventListener('scroll', e => { if (!inside(e)) close(); }, true);
      document.addEventListener('keydown', e => { if (e.key === 'Escape') close(); });
    }
    const em = l.ptsEnum ? lgEnums[l.ptsEnum] : null;
    const tierLabel = n => 'Tier ' + n + (em && em[n] ? ' (' + em[n] + ' pts)' : '');
    ctl.appendChild(mkSel([[0, 'All tiers'], [1, tierLabel(1)], [2, tierLabel(2)], [3, tierLabel(3)], [4, tierLabel(4)], [5, tierLabel(5)]],
      lgFilter.tier, v => lgFilter.tier = +v));
    const catOpts = [[-1, 'All localities']].concat(Object.keys(l.catName).map(k => [k, l.catName[k]])
      .sort((a, b) => a[1] < b[1] ? -1 : 1));
    ctl.appendChild(mkSel(catOpts, lgFilter.cat, v => lgFilter.cat = +v));
    ctl.appendChild(mkSel([['all', 'All statuses'], ['todo', 'Incomplete'], ['prog', 'In progress'], ['done', 'Complete']],
      lgFilter.status, v => lgFilter.status = v));
    wrap.appendChild(ctl);
    const capEl = document.createElement('div'); capEl.className = 'lg-cap'; capEl.id = 'lgTaskCap'; wrap.appendChild(capEl);
    const listCard = card(); listCard.id = 'lgTaskList';
    lgPaintList();
  }

Object.assign(window, { fetchLeagues, lgPinsPoll });
registerTab({ id: 'leagues', render: renderLeagues, open: function () { lgSig = ''; fetchLeagues(); } });
})();
