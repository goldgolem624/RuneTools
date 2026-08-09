// RuneToolsX panel: Leagues (tiers, relics, searchable task browser).
// Spliced inline into client.html; bare classic script sharing one global scope.
// Everything renders from the live cache DBTables (326 header, 327 tier order, 328 tiers,
// 329 relics, 334 tasks, 336 categories; CS2-space ids - the bridge decomposes them).
// Task row (CS2-verified against clientscript-20294/20296/20305, the game's own task list):
//   col 0 = the task's UI COMPONENT id (fed to CC_FIND in script20294) - NOT a struct;
//           resolving it as one produced plausible-but-wrong "objectives" (small ids
//           collide with the base-camp building structs: "Brewery", "Woodcutting Stump")
//   col 2 = the task's ACHIEVEMENT id (js5-57) - name/desc/points/req live there
//   col 3 = completion varbit as a var-reference (0x1000000 | varbit)
//   col 4 = internal type code (6 = "Complete the task set" phrasing in script20131)
//   col 5 = region/category INDEX into table 336's tuple column
//   col 6 = TIER 1-5; league points = enum (table 335 row col 0[2]) = {1:10,2:30,3:80,4:200,5:400}
// Task text mirrors script20131: quest subcats phrase as "Complete the quest: X.",
// Archaeology-mystery achievements as "Solve the Archaeology mystery: X.", else name/desc.

  let lgData = null, lgFetching = false, lgFetchAt = 0, lgSig = '';
  const LG_T = { header: 326, tierOrder: 327, tiers: 328, relics: 329, cfg: 335, tasks: 334, cats: 336 };
  // dbRows emits each row as {f: fileid, i: ints, s: strings} -- the id key is `f`.
  const lgCol = (r, m, k) => { const t = (r && r[m]) || {}; return (t[k] && t[k][0] !== undefined) ? t[k][0] : null; };
  const lgList = (r, m, k) => { const t = (r && r[m]) || {}; return Array.isArray(t[k]) ? t[k] : []; };

  let lgVbVals = null;         // varbit id -> live value
  let lgAchById = null;        // achievement id -> def (incl. the hidden league entries)
  let lgPts = null;            // tier -> league points, decoded live from the config enum
  const lgFilter = { q: '', tier: 0, cat: -1, status: 'all' };
  let lgShowMax = 80, lgListDirty = false;

  async function fetchLeagues() {
    if (!bridge() || !bridge().dbRows || lgFetching) return;
    const t = Date.now();
    if (t - lgFetchAt < 5000) return;
    lgFetchAt = t; lgFetching = true;
    try {
      const grab = async id => { try { return JSON.parse(await bridge().dbRows(id) || 'null') || []; } catch (e) { return []; } };
      const [hdr, order, tiers, relics, cfg, tasks, cats] = await Promise.all(
        [LG_T.header, LG_T.tierOrder, LG_T.tiers, LG_T.relics, LG_T.cfg, LG_T.tasks, LG_T.cats].map(grab));
      if (!tiers.length && !relics.length) return;        // cache not open yet; retry next poll
      const relicById = {};
      for (const r of relics) {
        // col 5 effect quads [var_reference, value, bool, text] flatten to i5/i6/i7 + s8;
        // script20144: bool && var != 0 -> ACTIVE, var < value -> inactive, all pass -> active.
        // A relic's own effect vars ARE its picked state.
        const refs = lgList(r, 'i', '5'), vals = lgList(r, 'i', '6'), bools = lgList(r, 'i', '7');
        const fx = [];
        for (let i = 0; i < refs.length; i++) fx.push({ ref: refs[i] >>> 0, val: vals[i] | 0, any: (bools[i] | 0) === 1 });
        relicById[r.f] = {
          name: lgCol(r, 's', '0') || ('#' + r.f),
          desc: lgCol(r, 's', '1') || '',
          spr: lgCol(r, 'i', '3') | 0,   // relic icon sprite (col 4 = the larger selected-state variant)
          fx: fx
        };
      }
      const tierIds = order.length ? lgList(order[0], 'i', '0') : tiers.map(r => r.f);
      const tierRows = tierIds.map(id => tiers.find(r => r.f === id)).filter(Boolean);
      // Categories row: tuple column flattens to ints col 4 = category id (== position),
      // strings col 3 = name (verified on the live 949.5 row; task col 5 indexes these).
      const catName = {};
      if (cats.length) {
        const ids = lgList(cats[0], 'i', '4'), names = lgList(cats[0], 's', '3');
        for (let i = 0; i < ids.length && i < names.length; i++) catName[ids[i]] = names[i];
      }
      // League points enum: table 335 config row, col 0's third subcolumn (flattened key '2',
      // first element - declaration order keeps it first). script20305 feeds col-6 into it.
      let ptsEnum = 0;
      if (cfg.length) ptsEnum = lgList(cfg[0], 'i', '2')[0] | 0;
      if (ptsEnum > 0 && !lgPts && bridge().enumInfo) {
        try {
          const em = JSON.parse(await bridge().enumInfo(ptsEnum) || 'null');
          if (em && Object.keys(em).length) { lgPts = {}; for (const k in em) lgPts[k] = em[k] | 0; }
        } catch (e) {}
      }
      const taskRows = tasks.map(r => {
        const ref = lgCol(r, 'i', '3') | 0;
        return {
          f: r.f,
          comp: lgCol(r, 'i', '0') | 0,
          count: Math.max(1, lgCol(r, 'i', '1') | 0),
          ach: lgCol(r, 'i', '2') | 0,
          vb: (ref >>> 24) === 1 ? (ref & 0xFFFFFF) : 0,
          type: lgCol(r, 'i', '4') | 0,
          cat: lgCol(r, 'i', '5') | 0,
          tier: lgCol(r, 'i', '6') | 0
        };
      });
      lgData = {
        name: hdr.length ? (lgCol(hdr[0], 's', '1') || 'Leagues') : 'Leagues',
        sub: hdr.length ? (lgCol(hdr[0], 's', '2') || '') : '',
        ptsVarp: hdr.length ? (lgCol(hdr[0], 'i', '22') | 0) : 0,   // 326.22 var_reference = the player's league points varp (12426)
        tiers: tierRows.map(r => ({
          cost: lgCol(r, 'i', '2') | 0,
          relics: lgList(r, 'i', '1').map(id => relicById[id] || { name: '#' + id, desc: '' }),
          passives: lgList(r, 's', '4')
        })),
        catName: catName,
        tasks: taskRows
      };
      lgVbKick();
      lgEnsureAch();
    } finally { lgFetching = false; }
    paneRun('leagues', renderLeagues);
  }

  // Achievement defs (shared cache with the Achievements tab): the task's col-2 id points
  // straight at its achievement - the hidden cat-5619 entries carry the task sentence.
  async function lgEnsureAch() {
    if (lgAchById) return;
    if (!achDefs && bridge() && bridge().achievements) {
      try { achDefs = JSON.parse(await bridge().achievements()) || []; } catch (e) { achDefs = []; }
    }
    if (!achDefs || !achDefs.length) return;
    const map = {};
    for (const a of achDefs) map[a.id] = a;
    lgAchById = map; lgListDirty = true;
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
    return 'Task #' + t.f + (t.count > 1 ? ' x' + t.count.toLocaleString() : '');
  }
  function lgTaskPts(t) { return (lgPts && lgPts[t.tier]) ? lgPts[t.tier] : 0; }

  // Per-tier relic-pick vars: enum 9082 (keytype 0 -> var_reference), hardcoded in the
  // game's own script20140 ("Tier N does not have a variable!" guard). Values decode as
  // (1<<24)|varbit -> varbits 58410+tier; vb 58460 grants a bonus pick on tiers 1-3
  // (script20141). A tier can pick when its var > 0 AND the points tier is reached
  // (script20142) - live-validatable when Equilibrium runs.
  const LG_PICK_ENUM = 9082, LG_BONUS_VB = 58460;
  let lgPickVbs = null;        // tier index (0-based) -> varbit id
  let lgPoints = null;         // live league points (header varp)
  async function lgPickEnsure() {
    if (lgPickVbs || !bridge() || !bridge().enumInfo) return;
    try {
      const em = JSON.parse(await bridge().enumInfo(LG_PICK_ENUM) || 'null');
      if (em) {
        const m = {};
        for (const k in em) { const v = em[k] >>> 0; if ((v >>> 24) === 1) m[k | 0] = v & 0xFFFFFF; }
        if (Object.keys(m).length) lgPickVbs = m;
      }
    } catch (e) {}
  }
  // A var_reference resolves to a varbit ((1<<24)|id) or a raw varp id.
  const lgRefIsVb = ref => (ref >>> 24) === 1;
  const lgRefId = ref => lgRefIsVb(ref) ? (ref & 0xFFFFFF) : (ref >>> 0);
  // Live completion varbits + relic effect vars + pick vars, read in chunks through the
  // shared varbit->varp resolver; raw-varp refs and the points varp go through varps().
  async function lgVbKick() {
    if (!lgData) return;
    await lgPickEnsure();
    const vbs = new Set(lgData.tasks.map(t => t.vb).filter(v => v > 0));
    const vps = new Set();
    for (const t of lgData.tiers) for (const x of t.relics) for (const f of (x.fx || []))
      (lgRefIsVb(f.ref) ? vbs : vps).add(lgRefId(f.ref));
    if (lgPickVbs) { for (const k in lgPickVbs) vbs.add(lgPickVbs[k]); vbs.add(LG_BONUS_VB); }
    if (lgData.ptsVarp > 0) vps.add(lgData.ptsVarp);
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
    lgPoints = lgData.ptsVarp > 0 ? (vpOut[lgData.ptsVarp] | 0) : null;
    lgListDirty = true;
  }
  let lgVpVals = {};
  function lgRefVal(ref) { return lgRefIsVb(ref) ? ((lgVbVals && lgVbVals[ref & 0xFFFFFF]) | 0) : (lgVpVals[ref >>> 0] | 0); }
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
  function lgTierPicks(i) {   // script20141: pick var + the tiers-1-3 bonus
    if (!lgPickVbs || !lgVbVals) return 0;
    const vb = lgPickVbs[i];
    let n = vb ? ((lgVbVals[vb] | 0)) : 0;
    if (i <= 2 && ((lgVbVals[LG_BONUS_VB] | 0) === 1)) n += 1;
    return n;
  }
  function lgTaskState(t) {
    if (!t.vb || !lgVbVals) return { v: 0, tgt: t.count, done: false, known: false };
    const a = lgAchById && lgAchById[t.ach];
    let tgt = t.count;
    if (a) for (const q of (a.reqs || [])) if ((q.varbits || []).indexOf(t.vb) >= 0 && q.value > 0) { tgt = q.value; break; }
    const v = lgVbVals[t.vb] | 0;
    return { v: v, tgt: tgt, done: v >= tgt, known: true };
  }

  function lgVisibleTasks() {
    const d = lgData; if (!d) return [];
    const q = lgFilter.q.toLowerCase();
    return d.tasks.filter(t => {
      if (lgFilter.tier && t.tier !== lgFilter.tier) return false;
      if (lgFilter.cat >= 0 && t.cat !== lgFilter.cat) return false;
      if (lgFilter.status !== 'all') {
        const st = lgTaskState(t);
        if (lgFilter.status === 'done' && !st.done) return false;
        if (lgFilter.status === 'todo' && st.done) return false;
      }
      if (q) {
        const a = lgAchById && lgAchById[t.ach];
        const txt = (lgTaskText(t) + ' ' + (a ? (a.desc || '') : '') + ' '
          + (d.catName[t.cat] || '')).toLowerCase();
        if (txt.indexOf(q) < 0) return false;
      }
      return true;
    });
  }

  function lgPaintList() {
    const list = $('lgTaskList'), cap = $('lgTaskCap'); if (!list || !lgData) return;
    const vis = lgVisibleTasks();
    let doneN = 0, ptsDone = 0, ptsAll = 0;
    if (lgVbVals) for (const t of lgData.tasks) {
      const p = lgTaskPts(t); ptsAll += p;
      if (lgTaskState(t).done) { doneN++; ptsDone += p; }
    }
    if (cap) cap.textContent = vis.length + ' of ' + lgData.tasks.length + ' tasks'
      + (lgVbVals ? ' · ' + doneN + ' done' + (lgPts ? ' · ' + ptsDone.toLocaleString() + ' / ' + ptsAll.toLocaleString() + ' pts' : '') : '')
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
      sub.textContent = (lgData.catName[t.cat] || ('Region ' + t.cat)) + ' · Tier ' + t.tier;
      nm.appendChild(sub);
      r.appendChild(nm);
      const p = document.createElement('span'); p.className = 'lg-pill';
      const pts = lgTaskPts(t);
      p.textContent = (pts ? pts + ' pts' : 'Tier ' + t.tier)
        + (st.known ? (st.done ? ' · done' : (st.v > 0 ? ' · ' + st.v + '/' + st.tgt : '')) : '');
      if (st.done) { p.style.color = 'var(--ok)'; p.style.borderColor = 'var(--ok)'; }
      r.appendChild(p);
      const a = lgAchById && lgAchById[t.ach];
      const tipParts = [];
      if (a && (a.name || '').trim() && (a.desc || '').trim() && a.name.trim() !== a.desc.trim()) tipParts.push(a.desc.trim());
      tipParts.push('Row ' + t.f + ' · achievement ' + t.ach + (t.vb ? ' · varbit ' + t.vb + ' = ' + st.v + '/' + st.tgt : ''));
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

  // Tiers card + header points repaint (live vars land after the first build).
  function lgPaintTiers() {
    const d = lgData, tc = $('lgTierCard'), hd = $('lgHead');
    if (!d || !tc) return;
    if (hd) hd.textContent = d.name + (d.sub ? ' · ' + d.sub : '') + (lgPoints != null ? ' · ' + lgPoints.toLocaleString() + ' pts' : '');
    tc.innerHTML = '';
    d.tiers.forEach((t, i) => {
      const reached = lgPoints != null && lgPoints >= t.cost;
      const picks = lgTierPicks(i);
      const anyActive = t.relics.some(x => lgRelicActive(x));
      const r = document.createElement('div'); r.className = 'lg-row';
      const nm = document.createElement('div'); nm.className = 'lg-nm';
      const ttl = document.createElement('div'); ttl.textContent = 'Tier ' + (i + 1); nm.appendChild(ttl);
      const chips = document.createElement('div'); chips.className = 'lg-relics';
      for (const x of t.relics) {
        const on = lgRelicActive(x);
        const ch = document.createElement('span'); ch.className = 'lg-relic' + (on ? ' on' : '');
        if (x.spr) { const ic = document.createElement('span'); ic.className = 'lg-rico'; loadSpriteIcon(ic, x.spr, 36); ch.appendChild(ic); }
        const tx = document.createElement('span'); tx.textContent = x.name; ch.appendChild(tx);
        ch.dataset.tip = x.name + (on ? ' (active)' : '') + (x.desc ? '\n' + x.desc : '');
        chips.appendChild(ch);
      }
      nm.appendChild(chips);
      if (t.passives.length) { const sb = document.createElement('div'); sb.className = 'lg-sub'; sb.textContent = t.passives.length + ' tier passives'; nm.appendChild(sb); }
      r.appendChild(nm);
      // Pick-available: the tier's relic point is granted and unspent while the points
      // threshold is reached (the same two gates the game's pick button checks).
      if (reached && picks > 0 && !anyActive) {
        const av = document.createElement('span'); av.className = 'lg-pill lg-avail';
        av.textContent = 'choice available';
        r.appendChild(av);
      }
      const p = document.createElement('span'); p.className = 'lg-pill';
      p.textContent = t.cost.toLocaleString() + ' pts';
      if (reached) { p.style.color = 'var(--ok)'; p.style.borderColor = 'var(--ok)'; }
      r.appendChild(p);
      tc.appendChild(r);
    });
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
        .lg-rico { width: 18px; height: 18px; flex: 0 0 auto; background-size: contain; background-repeat: no-repeat; background-position: center; }`);
      wrap = document.createElement('div'); wrap.id = 'lgWrap'; wrap.className = 'lg-wrap'; c.appendChild(wrap);
    }
    const d = lgData;
    if (!d) {
      if (lgSig !== 'wait') { lgSig = 'wait'; wrap.innerHTML = '<div class="lg-empty">Reading league data from the cache... (be in-world). Before a league is live the cache holds placeholder data only.</div>'; }
      return;
    }
    const sig = d.name + '|' + d.tasks.length + '|' + d.tiers.map(t => t.cost + ':' + t.relics.length).join(',');
    if (sig === lgSig) {
      if (!lgAchById) lgEnsureAch();   // achievements may load after the tables did
      if (lgListDirty) { lgListDirty = false; lgPaintTiers(); lgPaintList(); }
      return;
    }
    lgSig = sig;
    wrap.innerHTML = '';
    const sec = t => { const e = document.createElement('div'); e.className = 'lg-sec'; e.textContent = t; wrap.appendChild(e); };
    const card = () => { const e = document.createElement('div'); e.className = 'lg-card'; wrap.appendChild(e); return e; };
    const row = (box, name, sub, pill, tip) => {
      const r = document.createElement('div'); r.className = 'lg-row';
      const nm = document.createElement('div'); nm.className = 'lg-nm';
      const t = document.createElement('div'); t.textContent = name; nm.appendChild(t);
      if (sub) { const sb = document.createElement('div'); sb.className = 'lg-sub'; sb.textContent = sub; nm.appendChild(sb); }
      r.appendChild(nm);
      if (pill != null) { const p = document.createElement('span'); p.className = 'lg-pill'; p.textContent = pill; r.appendChild(p); }
      if (tip) r.dataset.tip = tip;
      box.appendChild(r);
    };
    sec(d.name + (d.sub ? ' · ' + d.sub : ''));
    wrap.lastChild.id = 'lgHead';
    const tc = card(); tc.id = 'lgTierCard';
    lgPaintTiers();

    sec('Tasks');
    // controls (built once per data signature; the list repaints without touching them)
    const ctl = document.createElement('div'); ctl.className = 'lg-ctl';
    const inp = document.createElement('input'); inp.className = 'lg-search'; inp.type = 'text';
    inp.placeholder = 'Search tasks, objectives, regions...'; inp.value = lgFilter.q;
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
        const old = document.getElementById('lgMenu'); if (old) { old.remove(); return; }
        const pop = document.createElement('div'); pop.className = 'sndmenu'; pop.id = 'lgMenu';
        for (const [v, lab] of opts) {
          const it = document.createElement('div');
          it.className = 'sndmenu-it' + (String(v) === curV ? ' sel' : '');
          it.textContent = lab;
          it.addEventListener('click', () => {
            pop.remove(); curV = String(v); txt.textContent = label();
            set(v); lgShowMax = 80; lgPaintList();
          });
          pop.appendChild(it);
        }
        document.body.appendChild(pop);          // body-level so the scroll pane can't clip it
        placeMenu(pop, b);
        const selIt = pop.querySelector('.sndmenu-it.sel'); if (selIt) selIt.scrollIntoView({ block: 'nearest' });
      });
      return b;
    };
    if (!renderLeagues._lgMenuBound) {
      renderLeagues._lgMenuBound = true;
      const inside = e => !!(e && e.target && e.target.nodeType === 1 && e.target.closest && e.target.closest('#lgMenu'));
      const close = () => { const m = document.getElementById('lgMenu'); if (m) m.remove(); };
      document.addEventListener('click', e => { if (!inside(e)) close(); });
      document.addEventListener('scroll', e => { if (!inside(e)) close(); }, true);
      document.addEventListener('keydown', e => { if (e.key === 'Escape') close(); });
    }
    const tierLabel = n => 'Tier ' + n + (lgPts && lgPts[n] ? ' (' + lgPts[n] + ' pts)' : '');
    ctl.appendChild(mkSel([[0, 'All tiers'], [1, tierLabel(1)], [2, tierLabel(2)], [3, tierLabel(3)], [4, tierLabel(4)], [5, tierLabel(5)]],
      lgFilter.tier, v => lgFilter.tier = +v));
    const catOpts = [[-1, 'All regions']].concat(Object.keys(d.catName).map(k => [k, d.catName[k]])
      .sort((a, b) => a[1] < b[1] ? -1 : 1));
    ctl.appendChild(mkSel(catOpts, lgFilter.cat, v => lgFilter.cat = +v));
    ctl.appendChild(mkSel([['all', 'All statuses'], ['todo', 'Incomplete'], ['done', 'Complete']],
      lgFilter.status, v => lgFilter.status = v));
    wrap.appendChild(ctl);
    const capEl = document.createElement('div'); capEl.className = 'lg-cap'; capEl.id = 'lgTaskCap'; wrap.appendChild(capEl);
    const listCard = card(); listCard.id = 'lgTaskList';
    lgPaintList();
  }
