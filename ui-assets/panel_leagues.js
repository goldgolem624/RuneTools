// RuneToolsX panel: Leagues (league picker, tiers, relics, blessings, trophies,
// searchable task browser). Spliced inline into client.html; bare classic script
// sharing one global scope.
// Everything renders from the live cache DBTables (CS2-space ids, bridge dbRows):
//   326 headers (one row per league: 17039 Catalyst, 19883 Equilibrium)
//   327 tier-order rows, 328 tier rows, 329 relic/blessing rows
//   333 trophies, 334 tasks, 335 config, 336 localities
// Header row (verified against both live rows, build 940 post-Equilibrium):
//   col 0  = league NUMBER (1, 2, ...) - also the task-table membership column index
//   col 1/2 = name / short name;  col 6 = points bar max;  col 7 = interface group
//   col 9  = relic tier-order row (327);  col 10 = blessing tier-order row (327, L2+)
//   col 12 = trophies row (333);  col 13 = config row (335)
//   col 22 = region-unlock enum (9287: tasks needed per region, shared)
//   col 28 = league points var_reference (L1 varp 12426, L2 varp 13521)
// Tier row (328): col 1 = relic ids, col 2 = points cost, col 4 = XP multiplier,
//   col 5/6/7 = tier passive (ref, value, text) tuples, col 10 = unlock notes.
// Relic/blessing row (329): col 0/1 = name/desc, col 2 = blessing cost, col 3 =
//   blessing alignment, col 5 = granted item (obj), col 7/8 = normal/selected icon
//   SPRITES (script20257 renders them as graphic), col 10..13 = effect
//   (var_reference, value, any-flag, text) quads. Blessing rows also carry an
//   (alignment ref, int) tuple at col 9 whose 2nd sub flattens into the col-10
//   list ahead of the effect refs - mkRelic right-aligns to shed it.
// Task row (334, schema CHANGED with Equilibrium - old col2=ach/col3=ref is gone):
//   col 0 = UI component id, col 1..N = per-league membership booleans (script20981
//   reads db 334.<leagueNum> with the number compiled in), col 3 = achievement id,
//   col 4 = completion var_reference, col 5 = type code, col 6 = locality id
//   (336 tuple col 1; the 336 row's col 0 value = the "Global" id), col 7 = tier
//   1-5 (points enum 9332 = {1:10,2:30,3:80,4:200,5:400}), col 8 = members-only.
// Task text mirrors script20131: quest subcats phrase as "Complete the quest: X.",
// Archaeology-mystery achievements as "Solve the Archaeology mystery: X.", else name.

  let lgData = null, lgFetching = false, lgFetchAt = 0, lgSig = '';
  const LG_T = { header: 326, tierOrder: 327, tiers: 328, relics: 329, trophies: 333, tasks: 334, cfg: 335, cats: 336, regions: 382, regionInfo: 383 };
  // dbRows emits each row as {f: fileid, i: ints, s: strings} -- the id key is `f`.
  const lgCol = (r, m, k) => { const t = (r && r[m]) || {}; return (t[k] && t[k][0] !== undefined) ? t[k][0] : null; };
  const lgList = (r, m, k) => { const t = (r && r[m]) || {}; return Array.isArray(t[k]) ? t[k] : []; };

  let lgCurNum = 0;            // selected league number; 0 = auto (newest with tasks)
  let lgVbVals = null;         // varbit id -> live value (current league's vars)
  let lgVpVals = {};           // varp id -> live value
  let lgAchById = null;        // achievement id -> def (incl. the hidden league entries)
  const lgFilter = { q: '', tier: 0, cat: -1, status: 'all' };
  let lgShowMax = 80, lgListDirty = false;

  // A var_reference resolves to a varbit ((1<<24)|id) or a raw varp id.
  const lgRefIsVb = ref => (ref >>> 24) === 1;
  const lgRefId = ref => lgRefIsVb(ref) ? (ref & 0xFFFFFF) : (ref >>> 0);
  function lgRefVal(ref) { return lgRefIsVb(ref) ? ((lgVbVals && lgVbVals[ref & 0xFFFFFF]) | 0) : (lgVpVals[ref >>> 0] | 0); }

  // Per-tier relic-pick vars: enum 9082 (0-based tier -> var_reference, varbits
  // 58410+), vb 58460 grants a bonus pick on tiers 1-3 (script20141). Shared across
  // leagues; the server resets the values per league. Tasks-completed counter for
  // region unlocks is per league and hardcoded in the game (script21081: vb 58389
  // for Equilibrium; Catalyst's is dead weight now that the league ended).
  const LG_PICK_ENUM = 9082, LG_BONUS_VB = 58460;
  const LG_TASKSDONE_VB = { 2: 58389 };
  // Region unlock state: varp 12327 is a locality BIT ARRAY (script20136 =
  // unk10982(varplayer_12327, bit); a region counts as unlocked when every bit of
  // its table-382 col-1 enum is set - script20133). Bit indexes run past 31, so
  // the array continues into the next varp: bit b -> varp 12327+(b>>5), bit b&31.
  const LG_REGION_VP = 12327;
  let lgPickVbs = null;        // tier index (0-based) -> varbit id
  let lgPoints = null;         // live league points (header var_reference)
  let lgEnums = {};            // enum id -> {k: v} via bridge enumInfo, fetched once

  function lgCur() {
    if (!lgData || !lgData.leagues.length) return null;
    if (lgCurNum) { const m = lgData.leagues.find(l => l.num === lgCurNum); if (m) return m; }
    // auto: newest league that actually has tasks (a stripped league keeps its header)
    const withTasks = lgData.leagues.filter(l => l.tasks.length);
    return (withTasks.length ? withTasks : lgData.leagues)[Math.max(0, (withTasks.length ? withTasks : lgData.leagues).length - 1)];
  }

  async function lgEnum(id) {
    if (!id) return null;
    if (lgEnums[id]) return lgEnums[id];
    // misses are NOT cached: the bridge may simply not be ready yet, and the
    // 5s poll makes retries free
    if (bridge() && bridge().enumInfo) {
      try {
        const em = JSON.parse(await bridge().enumInfo(id) || 'null');
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
      const grab = async id => { try { return JSON.parse(await bridge().dbRows(id) || 'null') || []; } catch (e) { return []; } };
      const [hdrs, orders, tiers, relics, trophies, tasks, cfgs, cats, regions, regionInfos] = await Promise.all(
        [LG_T.header, LG_T.tierOrder, LG_T.tiers, LG_T.relics, LG_T.trophies, LG_T.tasks, LG_T.cfg, LG_T.cats,
         LG_T.regions, LG_T.regionInfo].map(grab));
      if (!hdrs.length || !tiers.length) return;          // cache not open yet; retry next poll
      const by = rows => { const m = {}; for (const r of rows) m[r.f] = r; return m; };
      const orderBy = by(orders), tierBy = by(tiers), relicBy = by(relics),
            trophyBy = by(trophies), cfgBy = by(cfgs), catBy = by(cats), regInfoBy = by(regionInfos);

      // Region catalog (table 382, alphabetical): col 1 = locality-bit enum, col 2 =
      // table-383 info row. The info row carries no name column; pull it out of the
      // "The <name> region covers ..." description string.
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
        // Blessing rows carry an (alignment var_reference, int) tuple at col 9 whose
        // 2nd sub flattens into the col-10 list AHEAD of the effect refs. Right-align
        // the refs to the value count to shed the spill (relic rows have no col 9,
        // so this is a no-op there).
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
          // 329.7/329.8 are the normal/selected icon SPRITES (script20257 renders
          // them as graphic), not varbits
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
        // localities: cfg col 6 tuple's 3rd entry names the 336 row; its (id, sprite,
        // name) triples flatten to i1/i2/s3, and its col 0 holds the "Global" id.
        const cfg = cfgBy[lgCol(h, 'i', '13')];
        const catRow = cfg ? catBy[lgList(cfg, 'i', '6')[2]] : null;
        const catName = {};
        if (catRow) {
          const ids = lgList(catRow, 'i', '1'), names = lgList(catRow, 's', '3');
          for (let i = 0; i < ids.length && i < names.length; i++)
            if (catName[ids[i]] === undefined) catName[ids[i]] = names[i];
          const g = lgCol(catRow, 'i', '0');
          if (g != null && catName[g] === undefined) catName[g] = 'Global';
        }
        const memberCol = String(num);
        const trophyRow = trophyBy[lgCol(h, 'i', '12')];
        leagues.push({
          num: num,
          name: lgCol(h, 's', '1') || ('League ' + num),
          sub: lgCol(h, 's', '2') || '',
          barMax: lgCol(h, 'i', '6') | 0,
          ptsRef: (lgCol(h, 'i', '28') | 0) >>> 0,
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
              ach: lgCol(r, 'i', '3') | 0,
              vb: lgRefIsVb(ref) ? (ref & 0xFFFFFF) : 0,
              type: lgCol(r, 'i', '5') | 0,
              cat: lgCol(r, 'i', '6') | 0,
              tier: lgCol(r, 'i', '7') | 0,
              // db 334.8 = "allowed on FREE worlds" (script20296 shows the Membership
              // Required badge when it is false), so 1 = F2P-capable, 0 = members-only
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

  // Achievement defs (shared cache with the Achievements tab): the task's col-3 id
  // points straight at its achievement - hidden entries carry the task sentence.
  async function lgEnsureAch() {
    if (lgAchById) return;
    if (!achDefs && bridge() && bridge().achievements) {
      try { achDefs = JSON.parse(await bridge().achievements()) || []; } catch (e) { achDefs = []; }
    }
    if (!achDefs || !achDefs.length) return;
    const map = {};
    for (const a of achDefs) map[a.id] = a;
    lgAchById = map; lgListDirty = true;
    lgVbKick();   // re-read live vars now that requirement varbits are known
  }
  // script20131's phrasing, from the same signals it uses (subcat / sprite):
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
  // Live vars for the SELECTED league: task completion vars, relic/blessing effect +
  // state vars, pick vars, the points var, the tasks-done counter. Varbits go through
  // the shared chunked resolver; raw-varp references through varps().
  async function lgVbKick() {
    const l = lgCur(); if (!l) return;
    await lgPickEnsure();
    await lgEnum(l.ptsEnum);
    await lgEnum(l.regionEnum);
    const vbs = new Set(), vps = new Set();
    const addRef = ref => (lgRefIsVb(ref) ? vbs : vps).add(lgRefId(ref));
    for (const t of l.tasks) {
      if (t.vb > 0) vbs.add(t.vb);
      // the task's achievement carries the REAL progress counters (op-14 reqs),
      // which are often different varbits from the db-334 completion var
      const a = lgAchById && lgAchById[t.ach];
      if (a) for (const q of (a.reqs || [])) for (const vb of q.varbits) if (vb > 0) vbs.add(vb);
    }
    for (const tl of [l.tiers, l.blessTiers]) for (const tr of tl) for (const x of tr.relics) {
      for (const f of (x.fx || [])) addRef(f.ref);
    }
    if (lgPickVbs) { for (const k in lgPickVbs) vbs.add(lgPickVbs[k]); vbs.add(LG_BONUS_VB); }
    if (LG_TASKSDONE_VB[l.num]) vbs.add(LG_TASKSDONE_VB[l.num]);
    if (l.ptsRef > 0) addRef(l.ptsRef);
    if (lgData.regions && lgData.regions.length) {
      vps.add(LG_REGION_VP); vps.add(LG_REGION_VP + 1);   // bit array spans two varps (bits 0-40)
      for (const rg of lgData.regions) await lgEnum(rg.bitsEnum);
    }
    const ids = [...vbs];
    const out = {};
    for (let i = 0; i < ids.length; i += 150) {
      try { Object.assign(out, await readVarbitValues(ids.slice(i, i + 150))); } catch (e) {}
    }
    let vpOut = {};
    if (vps.size && bridge().varps) {
      try { vpOut = JSON.parse(await bridge().varps(myPid(), [...vps].join(','))) || {}; } catch (e) {}
    }
    lgVbVals = out; lgVpVals = vpOut;
    lgPoints = l.ptsRef > 0 ? lgRefVal(l.ptsRef) : null;
    lgListDirty = true;
    paneRun('leagues', renderLeagues);
  }

  // script20144: any-flagged effect var != 0 -> active; any var below its value -> not.
  function lgRelicActive(x) {
    if (!lgVbVals || !(x.fx || []).length) return false;
    for (const f of x.fx) {
      const v = lgRefVal(f.ref);
      if (f.any && v !== 0) return true;
      if (v < f.val) return false;
    }
    return true;
  }
  // null = live data not in yet; else script20133's rule: every locality bit set.
  function lgRegionUnlocked(reg) {
    const em = reg.bitsEnum ? lgEnums[reg.bitsEnum] : null;
    if (!em || lgVpVals[LG_REGION_VP] === undefined) return null;
    for (const k in em) {
      const bit = em[k] | 0;
      const v = lgVpVals[LG_REGION_VP + (bit >>> 5)] | 0;
      if (!((v >>> (bit & 31)) & 1)) return false;
    }
    return true;
  }
  function lgTierPicks(i) {   // script20141: pick var + the tiers-1-3 bonus
    if (!lgPickVbs || !lgVbVals) return 0;
    const vb = lgPickVbs[i];
    let n = vb ? ((lgVbVals[vb] | 0)) : 0;
    if (i <= 2 && ((lgVbVals[LG_BONUS_VB] | 0) === 1)) n += 1;
    return n;
  }
  // Task progress, evaluated the same way the game's achievement system does: each
  // op-14 requirement sums its varbits' live values against its target (e.g. the
  // "Harvest any memory from a wisp 200 times" task counts in vb 59669 -> 180/200).
  // The db-334 completion var (t.vb) is only a done FLAG when it is not itself one
  // of the requirement counters.
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
    // completion flag only counts when the var is not one of the progress counters
    const vbIsCounter = reqs.some(r => r.vbs.indexOf(t.vb) >= 0);
    const flagDone = t.vb > 0 && !vbIsCounter && vbv >= 1;
    const done = (reqDone === null) ? flagDone : (reqDone || flagDone);
    let v = vbv, tgt = 1;
    if (reqs.length) { const show = reqs.find(r => !r.ok) || reqs[0]; v = show.cur; tgt = show.tgt; }
    return { v: v, tgt: tgt, done: done, known: !!(t.vb || reqs.length), reqs: reqs };
  }

  // ---- pinned tasks: sticky toasts with live progress -------------------------
  // Pins are task ROW ids (unique across leagues), persisted in localStorage.
  // lgPinsPoll runs from the main refresh loop every tick regardless of open
  // windows, reads only the pinned tasks' varbits, and updates each task's
  // sticky toast in place. Dismissing a toast (its X) unpins the task.
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
    const msg = (st.done ? '✓ ' : '') + lgTaskText(t)
      + (showN ? '  ·  ' + st.v.toLocaleString() + ' / ' + st.tgt.toLocaleString() : '');
    let rec = lgPinToasts[t.f];
    if (rec && rec.closing) {          // user hit the X: dismiss = untrack
      delete lgPinToasts[t.f];
      lgPinSet(t.f, false);
      lgListDirty = true;
      return;
    }
    if (!rec) {
      rec = uiNotify(msg, { sticky: true });
      if (rec) lgPinToasts[t.f] = rec;
      return;
    }
    if (rec.msg !== msg) {
      rec.msg = msg;
      const b = rec.el && rec.el.querySelector('.toast-msg');
      if (b) b.textContent = msg;
    }
  }
  async function lgPinsPoll() {
    const pins = lgPinsList();
    if (!pins.length || !bridge()) return;
    const now = Date.now();
    if (now - lgPinsAt < 2000) return;
    lgPinsAt = now;
    if (!lgData) { fetchLeagues(); return; }    // table data first; toasts next tick
    if (!lgAchById) lgEnsureAch();
    const tasks = [];
    for (const l of lgData.leagues) for (const t of l.tasks) if (pins.indexOf(t.f) >= 0) tasks.push(t);
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
        // started but not finished: any requirement counter above zero
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
      // F2P tag: free-world capable per db 334.8, unless the achievement itself is
      // members-flagged (mirrors script20296's ACHIEVEMENT_GETMEMBERS || !col8 check;
      // the `members` field arrives with launcher builds that emit it)
      const achM = lgAchById && lgAchById[t.ach];
      sub.textContent = (l.catName[t.cat] || ('Locality ' + t.cat)) + ' · Tier ' + t.tier
        + (t.f2p && !(achM && achM.members) ? ' · F2P' : '');
      nm.appendChild(sub);
      r.appendChild(nm);
      // pin toggle: tracked tasks keep a sticky toast with live progress
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
      if (a && (a.name || '').trim() && (a.desc || '').trim() && a.name.trim() !== a.desc.trim()) tipParts.push(a.desc.trim());
      // one line per requirement counter, so each task's live varbit tracker is
      // reviewable in place (value/target + which varbit feeds it)
      for (const q of (st.reqs || [])) {
        tipParts.push((q.desc ? q.desc + ': ' : '') + q.cur.toLocaleString() + '/' + q.tgt.toLocaleString()
          + '  [vb ' + q.vbs.join('+') + ']' + (q.ok ? ' ✓' : ''));
      }
      tipParts.push('Row ' + t.f + ' · achievement ' + t.ach
        + (t.vb ? ' · completion vb ' + t.vb + ' = ' + (lgVbVals ? (lgVbVals[t.vb] | 0) : '?') : ''));
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

  // One relic/blessing tier list card (shared by the Relics and Blessings sections).
  function lgPaintTierList(tc, tierList, kind) {
    tc.innerHTML = '';
    tierList.forEach((t, i) => {
      const reached = kind === 'relic' && lgPoints != null && lgPoints >= t.cost;
      const picks = kind === 'relic' ? lgTierPicks(i) : 0;
      const anyActive = t.relics.some(x => lgRelicActive(x));
      const r = document.createElement('div'); r.className = 'lg-row';
      const nm = document.createElement('div'); nm.className = 'lg-nm';
      const ttl = document.createElement('div');
      ttl.textContent = 'Tier ' + (i + 1) + (t.xp > 1 ? ' · ' + t.xp + 'x XP' : '');
      nm.appendChild(ttl);
      const chips = document.createElement('div'); chips.className = 'lg-relics';
      for (const x of t.relics) {
        const on = lgRelicActive(x);
        const ch = document.createElement('span'); ch.className = 'lg-relic' + (on ? ' on' : '');
        const sprId = on && x.sprOn ? x.sprOn : x.spr;
        if (sprId || x.item) {
          const ic = document.createElement('span'); ic.className = 'lg-rico';
          if (sprId) loadSpriteIcon(ic, sprId, 36); else attachBankIcon(ic, x.item);
          ch.appendChild(ic);
        }
        const tx = document.createElement('span'); tx.textContent = x.name; ch.appendChild(tx);
        const tip = [x.name + (on ? ' (active)' : '')];
        if (x.desc) tip.push(x.desc.replace(/<[^>]*>/g, ''));
        for (const f of (x.fx || [])) if (f.txt) tip.push('- ' + f.txt.replace(/<[^>]*>/g, ''));
        tip.push('Row ' + x.row);
        ch.dataset.tip = tip.join('\n');
        chips.appendChild(ch);
      }
      nm.appendChild(chips);
      if (t.passives.length) { const sb = document.createElement('div'); sb.className = 'lg-sub'; sb.textContent = t.passives.length + ' tier passives'; nm.appendChild(sb); }
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
    const tc = $('lgTierCard');
    if (tc) lgPaintTierList(tc, l.tiers, 'relic');
    const bc = $('lgBlessCard');
    if (bc) lgPaintTierList(bc, l.blessTiers, 'blessing');
    // trophies + region unlocks repaint with live values
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
          // slot 1 is always Karamja (hardcoded in the game's own script21081);
          // later slots are free picks, so they have no fixed name
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
        // Which regions are actually open (varp 12327 bit array; the picks are a
        // bitmask, so the ORDER slots were chosen in is not recoverable - this
        // list is the ground truth of what is unlocked right now).
        if (lgData.regions && lgData.regions.length && lgVpVals[LG_REGION_VP] !== undefined) {
          const r = document.createElement('div'); r.className = 'lg-row';
          const nm = document.createElement('div'); nm.className = 'lg-nm';
          const head = document.createElement('div'); head.textContent = 'Unlocked regions'; nm.appendChild(head);
          const chips = document.createElement('div'); chips.className = 'lg-relics';
          for (const rg of lgData.regions) {
            const st = lgRegionUnlocked(rg);
            const ch = document.createElement('span'); ch.className = 'lg-relic' + (st ? ' on' : '');
            const tx = document.createElement('span'); tx.textContent = rg.name; ch.appendChild(tx);
            ch.dataset.tip = rg.name + (st === null ? ' (state unknown)' : (st ? ' (unlocked)' : ' (locked)'))
              + '\nRow ' + rg.row + ' · bits enum ' + rg.bitsEnum;
            chips.appendChild(ch);
          }
          nm.appendChild(chips);
          r.appendChild(nm);
          rgc.appendChild(r);
        }
      }
    }
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
        .lg-row { display: flex; align-items: flex-start; gap: 10px; padding: 8px 12px; }
        .lg-row + .lg-row { border-top: 1px solid var(--border); }
        .lg-nm { flex: 1; min-width: 0; color: var(--text); font-size: 12px; }
        .lg-sub { color: var(--text-mute); font-size: 10.5px; margin-top: 2px; }
        .lg-pill { font-size: 10.5px; padding: 2px 9px; border-radius: 999px; border: 1px solid var(--border); color: var(--text-dim); white-space: nowrap; font-variant-numeric: tabular-nums; flex: 0 0 auto; }
        .lg-empty { margin: 10px 14px; color: var(--text-dim); font-size: 12px; }
        .lg-ctl { display: flex; flex-wrap: wrap; gap: 5px; margin: 6px 0 8px; }
        .lg-search { flex: 1 1 130px; height: 26px; padding: 0 9px; background: var(--bg-elev); border: 1px solid var(--border); border-radius: 7px; color: var(--text); font-size: 11.5px; outline: none; }
        .lg-search:focus { border-color: var(--border-hi); }
        .lg-cap { color: var(--text-mute); font-size: 10.5px; margin: 2px 2px 6px; }
        .lg-tabs { display: flex; gap: 5px; margin: 4px 0 2px; }
        .lg-tab { font-size: 11px; padding: 4px 12px; border-radius: 999px; border: 1px solid var(--border); background: var(--bg-elev); color: var(--text-dim); cursor: pointer; }
        .lg-tab.on { border-color: var(--accent-hi); color: #fff; background: linear-gradient(90deg, rgba(124,92,252,0.28), rgba(124,92,252,0.10)); }
        /* A GRID, not flex-wrap: relics are equal-rank choices, and variable-width
           chips wrapped ragged (a tier of three broke 2 + 1 with the orphan a
           different width). Equal columns line the tiers up with each other, and
           auto-fit puts a whole tier on one row as soon as the window is wide
           enough. */
        .lg-relics { display: grid; grid-template-columns: repeat(auto-fit, minmax(132px, 1fr)); gap: 5px; margin-top: 6px; }
        .lg-relic { display: flex; align-items: center; gap: 6px; min-width: 0; padding: 3px 8px 3px 4px; border: 1px solid var(--border); border-radius: 8px; background: var(--bg-elev-2); font-size: 11px; color: var(--text); }
        .lg-relic > span:last-child { min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
        .lg-relic.on { border-color: var(--accent-hi); background: linear-gradient(90deg, rgba(124,92,252,0.28), rgba(124,92,252,0.10)); color: #fff; }
        .lg-avail { color: var(--warn, #fbbf24); border-color: var(--warn, #fbbf24); }
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
    const sig = l.num + '|' + l.name + '|' + l.tasks.length + '|'
      + l.tiers.map(t => t.cost + ':' + t.relics.length).join(',') + '|' + l.blessTiers.length;
    if (sig === lgSig) {
      if (!lgAchById) lgEnsureAch();   // achievements may load after the tables did
      if (lgListDirty) { lgListDirty = false; lgPaintTiers(); lgPaintList(); }
      return;
    }
    lgSig = sig;
    wrap.innerHTML = '';
    const sec = t => { const e = document.createElement('div'); e.className = 'lg-sec'; e.textContent = t; wrap.appendChild(e); };
    const card = () => { const e = document.createElement('div'); e.className = 'lg-card'; wrap.appendChild(e); return e; };

    // league tabs (only when the cache knows more than one league)
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
    const tc = card(); tc.id = 'lgTierCard';
    if (l.blessTiers.length) { sec('Blessings'); const bc = card(); bc.id = 'lgBlessCard'; }
    if (l.regionEnum) { sec('Region unlocks'); const rc = card(); rc.id = 'lgRegionCard'; }
    if (l.trophies.length) { sec('Trophies'); const trc = card(); trc.id = 'lgTrophyCard'; }
    lgPaintTiers();

    sec('Tasks');
    // controls (built once per data signature; the list repaints without touching them)
    const ctl = document.createElement('div'); ctl.className = 'lg-ctl';
    const inp = document.createElement('input'); inp.className = 'lg-search'; inp.type = 'text';
    inp.placeholder = 'Search tasks, objectives, localities...'; inp.value = lgFilter.q;
    inp.addEventListener('input', () => { lgFilter.q = inp.value.trim(); lgShowMax = 80; lgPaintList(); });
    ctl.appendChild(inp);
    // Themed dropdown (native <select> renders with system chrome in Ultralight): the
    // alerts panel's trigger + body-level .sndmenu popup pattern, via its shared placeMenu.
    const mkSel = (opts, cur, set) => {
      const b = document.createElement('button'); b.type = 'button'; b.className = 'al-typesel';
      let curV = String(cur);
      const label = () => { const o = opts.find(o2 => String(o2[0]) === curV); return o ? o[1] : String(curV); };
      const txt = document.createElement('span'); txt.textContent = label();
      const car = document.createElement('span'); car.className = 'cv'; car.textContent = '▾';
      b.appendChild(txt); b.appendChild(car);
      b.addEventListener('click', e => {
        e.stopPropagation();
        // wmRectsSoon after every open/close/pick: the popup is body-level and can
        // hang past the panel's window rect, and input over the transparent overlay
        // only reaches the page inside registered rects - without the refresh the
        // menu's overhang is click-through to the GAME (wheel spun the camera).
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
