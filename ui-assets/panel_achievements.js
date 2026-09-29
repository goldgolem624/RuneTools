// RuneToolsX panel: Achievements + Combat Mastery (two tabs sharing one cache fetch;
(function () {

  achDefs = null;
  achState = null;
  let achFetching = false, achListSig = '', achFetchAt = 0;
  let achFSearch = '', achFStatus = 0;   // filter: 0 all / 1 complete / 2 in progress / 3 incomplete
  let cmFSearch = '', cmFStatus = 0, cmFTier = -1, cmFBoss = -1, cmListSig = '';
  const ACH_CM = { 13980: ['Easy', 0], 14042: ['Medium', 1], 14305: ['Hard', 2],
                   14313: ['Elite', 3], 14314: ['Master', 4], 14420: ['Grandmaster', 5] };
  const ACH_CM_TIERS = ['Easy', 'Medium', 'Hard', 'Elite', 'Master', 'Grandmaster'];
  // A task's tier is the game's tier rollup ("Combat Mastery - Hard" lists its tasks), which is what the game
  // counts: two tasks carry a tier tag but sit in no rollup. The tag is used only when no rollup is known.
  let _cmTierOf = null;
  function cmTierOf() {
    if (!_cmTierOf && achDefs) {
      _cmTierOf = {};
      const isRoll = p => /^Combat Mastery - (\w+)$/.exec(p.name || '');
      const rolls = new Set(achDefs.filter(isRoll).map(p => p.id));   // each tier list also lists the one below it
      for (const p of achDefs) {
        const m = isRoll(p); if (!m || !p.subach) continue;
        const i = ACH_CM_TIERS.indexOf(m[1]); if (i < 0) continue;
        for (const c of p.subach) if (!rolls.has(c)) _cmTierOf[c] = [ACH_CM_TIERS[i], i];
      }
    }
    return _cmTierOf || {};
  }
  function achTier(a) {
    const r = cmTierOf();
    if (Object.keys(r).length) return r[a.id] || null;
    return (a.cm != null && ACH_CM[a.cm]) ? ACH_CM[a.cm] : null;
  }
  // Leagues tasks ship as nameless, category-5619, hidden achievements (empty-name trackable
  // <=> cat 5619), not real account achievements. Excluded everywhere.
  const ACH_LEAGUES_CAT = 5619;
  function achIsLeagues(a) { return a.cat === ACH_LEAGUES_CAT || !(a.name && a.name.trim()); }
  function achBitReqs(a) {
    const vbits = [], vpbits = [];
    for (const q of (a.reqs25 || [])) vbits.push({ vb: q.vb, bit: q.bit, n: q.n || '', g: q.g | 0 });
    for (const q of (a.reqsvpb || [])) vpbits.push({ vp: q.vp, bit: q.bit, n: q.n || '', g: q.g | 0 });
    for (const q of (a.reqs23 || [])) {
      const r = storageVbMap && storageVbMap[q.vp];
      if (r && q.bit <= r.msb - r.lsb) vbits.push({ vb: q.vp, bit: q.bit, n: q.n || '', g: q.g | 0 });
      else vpbits.push({ vp: q.vp, bit: q.bit, n: q.n || '', g: q.g | 0 });
    }
    return { vbits: vbits, vpbits: vpbits };
  }
  function achBitFromVbVal(vbId, vbVal, bit) { return ((vbVal || 0) >>> bit) & 1; }
  function achVarpReqs(a) {
    const out = [];
    for (const q of (a.reqsvp || [])) out.push({ vps: q.vps || [q.vp], v: q.v, n: q.n || '', g: q.g | 0 });
    return out;
  }
  // Something the game judges completion on. Retired achievements (disabled) are no longer judged.
  function achTrackable(a) {
    if (a.disabled) return false;
    return !!((a.reqs && a.reqs.length) || (a.reqs23 && a.reqs23.length) ||
              (a.reqs25 && a.reqs25.length) || (a.reqsvpb && a.reqsvpb.length) ||
              (a.reqsvp && a.reqsvp.length) || (a.skills && a.skills.length) ||
              (a.quests && a.quests.length) || (a.subach && a.subach.length));
  }

  // Completion as the game judges it. Every requirement entry sits in a group: a group is met once needN[g]
  // of its entries are (all of them when needN gives no count for it), child achievements count in their
  // group, and the achievement is complete once needGroups of its groups are met (all of them without it).
  // The achievements under prev only unlock one; they are listed but never part of completion.
  function achLines(a, ctx) {
    const lines = [];
    const skName = sid => (typeof SKILL_NAMES !== 'undefined' && SKILL_NAMES[sid]) || ('skill ' + sid);
    for (const q of (a.reqs || [])) {
      let cur = 0; const srcs = [];
      for (const vb of q.varbits) { cur += ctx.vb(vb); srcs.push(ctx.vbSrc(vb)); }
      const lbl = q.desc || ((a.reqs.length === 1 && q.value > 1 && a.desc) ? a.desc : '');
      lines.push({ g: q.g | 0, label: lbl, cur: cur, req: q.value, ok: cur >= q.value, src: srcs.join(' + '), varbits: q.varbits });
    }
    const br = achBitReqs(a);
    for (const b of br.vbits) {                  // one bit of a varbit's value
      const bv = achBitFromVbVal(b.vb, ctx.vb(b.vb), b.bit);
      lines.push({ g: b.g, label: b.n, cur: bv, req: 1, ok: bv >= 1, src: 'varbit ' + b.vb + ' bit ' + b.bit, varbits: [b.vb] });
    }
    for (const b of br.vpbits) {                 // one bit of a varp
      const bv = (ctx.vp(b.vp) >>> b.bit) & 1;
      lines.push({ g: b.g, label: b.n, cur: bv, req: 1, ok: bv >= 1, src: 'varp ' + b.vp + ' bit ' + b.bit, varps: [b.vp] });
    }
    for (const q of achVarpReqs(a)) {            // sum of varps >= value
      let cur = 0; for (const id of q.vps) cur += ctx.vp(id);
      lines.push({ g: q.g, label: q.n, cur: cur, req: q.v, ok: cur >= q.v, src: 'varp ' + q.vps.join(' + '), varps: q.vps });
    }
    for (const sq of (a.skills || [])) {
      const sid = sq[0] | 0, lvl = sq[1] | 0, cur = ctx.skill(sid);
      lines.push({ g: sq[2] | 0, label: 'Level ' + lvl + ' ' + skName(sid), cur: cur, req: lvl, ok: cur >= lvl, src: 'live skill ' + sid });
    }
    (a.quests || []).forEach((qid, i) => {
      const d = ctx.quest(qid);                  // true, false, or null when the quest state is not known yet
      lines.push({ g: (a.questsG ? a.questsG[i] : 0) | 0, label: 'Quest: ' + ctx.questName(qid), cur: d ? 1 : 0, req: 1,
                   ok: d === true, src: 'quest ' + qid, unsure: d == null });
    });
    return lines;
  }
  // -> { ok, met, need, groups, want, unsure, short } or null when there is nothing to judge
  function achJudge(a, lines, done) {
    const groups = {};
    const grp = g => groups[g] || (groups[g] = { met: 0, n: 0, unsure: false });
    for (const ln of lines) { if (ln.unlock) continue; const G = grp(ln.g); G.n++; if (ln.ok) G.met++; if (ln.unsure) G.unsure = true; }
    (a.subach || []).forEach((cid, i) => { const G = grp((a.subachG ? a.subachG[i] : 0) | 0); G.n++; if (done.has(cid)) G.met++; });
    let nG = a.needN ? a.needN.length : 0;
    for (const k in groups) nG = Math.max(nG, (+k) + 1);
    const list = [];
    for (let g = 0; g < nG; g++) {
      const G = groups[g] || { met: 0, n: 0, unsure: false };
      const need = (a.needN && typeof a.needN[g] === 'number') ? a.needN[g] : G.n;
      if (!G.n && !need) continue;
      list.push({ met: Math.min(G.met, need), need: need, ok: G.met >= need, unsure: G.unsure, short: G.n < need });
    }
    if (!list.length) return null;
    const want = (typeof a.needGroups === 'number' && a.needGroups > 0) ? Math.min(a.needGroups, list.length) : list.length;
    const okN = list.filter(x => x.ok).length;
    const best = list.slice().sort((x, y) => (y.met / Math.max(1, y.need)) - (x.met / Math.max(1, x.need))).slice(0, want);
    return { ok: okN >= want, met: best.reduce((s, x) => s + x.met, 0), need: best.reduce((s, x) => s + x.need, 0),
             groups: list.length, want: want, unsure: list.some(x => x.unsure),
             short: list.filter(x => x.short).length > list.length - want };   // entries the data does not hold
  }
  // -> { done, prog, unknown, cnt } for every achievement except Leagues and retired ones
  function achEvaluate(defs, ctx) {
    const done = new Set(), unknown = new Set(), prog = {}, cnt = {};
    const live = defs.filter(a => !achIsLeagues(a) && achTrackable(a));
    for (const a of live) prog[a.id] = achLines(a, ctx);
    for (let pass = 0, chg = true; chg && pass < 32; pass++) {
      chg = false;
      for (const a of live) {
        if (done.has(a.id)) continue;
        const r = achJudge(a, prog[a.id], done);
        if (r && r.ok) { done.add(a.id); chg = true; }
      }
    }
    const byId = {}; for (const a of defs) byId[a.id] = a;
    for (const a of live) {
      const r = achJudge(a, prog[a.id], done);
      if (!r) { unknown.add(a.id); continue; }
      cnt[a.id] = { met: r.met, need: r.need, groups: r.groups, want: r.want };
      if (!done.has(a.id) && (r.unsure || r.short)) unknown.add(a.id);
      for (const pid of (a.prev || [])) {        // shown, never judged
        const p = byId[pid];
        prog[a.id].push({ g: -1, unlock: true, label: 'Unlocked by: ' + ((p && p.name) || ('achievement ' + pid)),
                          cur: done.has(pid) ? 1 : 0, req: 1, ok: done.has(pid), src: 'achievement ' + pid });
      }
    }
    return { done: done, prog: prog, unknown: unknown, cnt: cnt };
  }
  // The live sources the evaluator reads, for one client.
  function achCtx(vp, questSt) {
    const liveSk = (typeof lastSnap !== 'undefined' && lastSnap && Array.isArray(lastSnap.skills)) ? lastSnap.skills : null;
    return {
      vb: vb => readVb(vb, vp) || 0,
      vbSrc: vb => { const r = storageVbMap && storageVbMap[vb]; return r ? ('varbit ' + vb + ' = varp ' + r.varp + ' bits ' + r.lsb + '-' + r.msb) : ('varbit ' + vb); },
      vp: id => +vp[id] || 0,
      skill: sid => (liveSk && liveSk[sid]) ? (liveSk[sid][0] | 0) : 0,
      quest: qid => questSt ? (questSt[qid] === 2) : null,
      questName: qid => { const q = (typeof QUEST_BY_ID !== 'undefined' && QUEST_BY_ID) ? QUEST_BY_ID.get(qid) : null; return (q && q.name) || ('#' + qid); },
    };
  }
  // The decoded definitions, or null while the game cache is not open yet (an empty list is not kept, so the
  // next call asks again).
  async function achLoadDefs() {
    try { const d = JSON.parse(await rtxData.raw('cache.achievements')); return (Array.isArray(d) && d.length) ? d : null; }
    catch (e) { return null; }
  }
  // Every varp the evaluator will read.
  function achVarps(defs) {
    const vps = new Set();
    for (const a of defs) {
      if (achIsLeagues(a)) continue;
      for (const q of (a.reqs || [])) for (const vb of q.varbits) { const r = storageVbMap && storageVbMap[vb]; if (r) vps.add(r.varp); }
      const br = achBitReqs(a);
      for (const b of br.vbits) { const r = storageVbMap && storageVbMap[b.vb]; if (r) vps.add(r.varp); }
      for (const b of br.vpbits) vps.add(b.vp);
      for (const q of achVarpReqs(a)) for (const id of q.vps) vps.add(id);
    }
    return vps;
  }
  // Quest states of the focused client (fetchQuests has its own throttle and keeps the last good read).
  async function achQuestState() {
    try {
      if (typeof fetchQuests === 'function') await fetchQuests(false);
      return (typeof questsData !== 'undefined' && questsData) ? questsData.st : null;
    } catch (e) { return null; }
  }
  // Quest states read from one client's own varps, for a plugin that may run in another client.
  async function achQuestStateFor(pid) {
    try {
      if (typeof questEnsureDefs !== 'function' || !(await questEnsureDefs())) return null;
      if (typeof QUESTS === 'undefined' || !QUESTS || typeof QUEST_VARPS === 'undefined' || !bridge().varps) return null;
      const qv = JSON.parse(await bridge().varps(pid, QUEST_VARPS.join(',')) || '{}');
      if (!qv || !Object.keys(qv).length) return null;
      const st = {}; for (const q of QUESTS) st[q.id] = questStatus(q, qv);
      return st;
    } catch (e) { return null; }
  }

  let _achDefById = null;
  function achDefById() { if (!_achDefById && achDefs) { _achDefById = {}; for (const a of achDefs) _achDefById[a.id] = a; } return _achDefById || {}; }

  function mkDropdown(value, onChange) {
    const dd = document.createElement('div'); dd.className = 'pet-dd';
    const btn = document.createElement('button'); btn.type = 'button'; btn.className = 'pet-dd-btn'; dd.appendChild(btn);
    const pop = document.createElement('div'); pop.className = 'pet-dd-pop'; dd.appendChild(pop);
    let items = [], cur = value;
    const paint = () => {
      const sel = items.find(it => String(it.value) === String(cur));
      btn.textContent = sel ? sel.label : (items.length ? items[0].label : '');
      pop.innerHTML = '';
      for (const it of items) {
        const o = document.createElement('div'); o.className = 'pet-dd-opt' + (String(it.value) === String(cur) ? ' on' : '');
        o.textContent = it.label;
        o.addEventListener('click', (e) => { e.stopPropagation(); cur = it.value; dd.classList.remove('open'); paint(); onChange(it.value); });
        pop.appendChild(o);
      }
    };
    btn.addEventListener('click', (e) => {
      e.stopPropagation();
      const open = dd.classList.contains('open');
      document.querySelectorAll('.pet-dd.open').forEach(x => x.classList.remove('open'));
      if (!open) dd.classList.add('open');
    });
    dd.setItems = (newItems, newVal) => { items = newItems || []; if (newVal !== undefined) cur = newVal; paint(); };
    dd.getValue = () => cur;
    paint();
    return dd;
  }
  if (!window._petDDClose) { window._petDDClose = true; document.addEventListener('click', () => document.querySelectorAll('.pet-dd.open').forEach(x => x.classList.remove('open'))); }


  async function fetchAchievements(force) {
    if (!bridge() || achFetching) return;
    const _t = Date.now(); if (!force && _t - achFetchAt < 2500) return; achFetchAt = _t;
    achFetching = true;
    try {
      if (!achDefs) achDefs = await achLoadDefs();
      if (!achDefs) return;
      await ensureVbMap();
      // Leagues tasks are excluded here: they would add ~750 entries to the single varp read.
      const vps = achVarps(achDefs);
      let vp = {};
      if (vps.size && bridge().varps) { try { vp = JSON.parse(await rtxData.raw('state.varps', [...vps].join(','))); } catch (e) {} }
      achState = achEvaluate(achDefs, achCtx(vp, await achQuestState()));
    } finally { achFetching = false; }
    paneRun('achievements', renderAchievements2);
    paneRun('combatmastery', renderCombatMastery);
  }

  async function pluginAchievements(pid) {
    if (!achDefs) achDefs = await achLoadDefs();
    if (!achDefs) return [];
    await ensureVbMap();
    const vps = achVarps(achDefs);
    let vp = {};
    if (vps.size && bridge().varps) { try { vp = JSON.parse(await bridge().varps(pid, [...vps].join(','))); } catch (e) {} }
    const st = achEvaluate(achDefs, achCtx(vp, await achQuestStateFor(pid)));
    const byId = achDefById();
    const out = [];
    for (const a of achDefs) {
      if (!st.prog[a.id]) continue;
      const reqs = st.prog[a.id].map(ln => {
        const dsc = ln.label || (ln.req > 1 ? (a.desc || 'Counted by the game') : 'Marked complete by the game');
        const r = { description: dsc, current: ln.cur, target: ln.req, complete: !!ln.ok, varbits: (ln.varbits || []).slice() };
        if (ln.varps) r.varps = ln.varps.slice();
        if (ln.unlock) r.unlock = true;
        return r;
      });
      for (const cid of (a.subach || [])) {      // child achievements count toward completion like any other entry
        const ok = st.done.has(cid);
        reqs.push({ description: (byId[cid] && byId[cid].name) || ('achievement ' + cid), current: ok ? 1 : 0, target: 1,
                    complete: ok, varbits: [], achievement: cid });
      }
      const c = st.cnt[a.id], tier = achTier(a);
      out.push({ id: a.id, name: a.name || '', description: a.desc || '', reward: a.reward || '',
                 points: a.points || 0, complete: st.done.has(a.id), requirementsNeeded: c ? c.need : 0,
                 combatMasteryTier: tier ? tier[0] : null, requirements: reqs });
    }
    return out;
  }

  function achRowEl(a, st) {
    const isDone = st.done.has(a.id);
    const row = document.createElement('div'); row.className = 'pet-row' + (isDone ? '' : ' pet-locked');
    const tier = achTier(a);
    const lines = st.prog[a.id] || [];
    const c = (st.cnt && st.cnt[a.id]) || null;
    const need = c ? c.need : 0;
    const tip = [a.name];
    if (a.desc) tip.push(a.desc);
    if (tier) tip.push('Combat mastery: ' + tier[0]);
    if (a.reward) tip.push('Reward: ' + a.reward);
    if (a.points) tip.push(a.points + ' achievement points');
    const judged = lines.filter(ln => !ln.unlock);
    const kids = (a.subach || []).map((id, i) => ({ id: id, g: (a.subachG ? a.subachG[i] : 0) | 0 }));
    const gs = [...new Set(judged.map(ln => ln.g).concat(kids.map(k => k.g)))].sort((x, y) => x - y);
    const multi = gs.length > 1;
    if (multi) tip.push('Complete ' + (c && c.want < c.groups ? ('any ' + c.want + ' of ' + c.groups + ' parts') : 'every part') + ':');
    for (const g of gs) {
      const gl = judged.filter(ln => ln.g === g), gk = kids.filter(k => k.g === g);
      const n = gl.length + (gk.length ? 1 : 0), all = gl.length + gk.length;
      const gNeed = (a.needN && typeof a.needN[g] === 'number') ? a.needN[g] : all;
      const pick = gNeed >= all ? 'all' : gNeed <= 1 ? 'any one' : ('any ' + gNeed);
      if (multi) tip.push('Part ' + (g + 1) + (all > 1 && pick !== 'all' ? ' (' + pick + ' of ' + all + ')' : '') + ':');
      else if (n > 1 || gk.length > 1) tip.push(pick === 'all' ? 'Do all of these:' : ('Do ' + pick + ' of these:'));
      for (const ln of gl) {
        // Most requirements carry no text in the cache (the game's own builder, script 10988,
        let head = ln.label;
        if (!head) head = ln.req > 1 ? (a.desc || 'Counted by the game') : 'Marked complete by the game';
        const count = ln.req > 1 ? ('  ' + ln.cur + ' of ' + ln.req) : '';
        tip.push((ln.ok ? '✓ ' : '• ') + head + count);
        if (ln.src) tip.push('    Source: ' + ln.src);
      }
      if (gk.length) {
        const d = gk.filter(k => st.done.has(k.id)).length;
        tip.push((d >= Math.min(gNeed, gk.length) ? '✓ ' : '• ') + 'Sub-achievements  ' + d + ' of ' + gk.length);
      }
    }
    for (const ln of lines) if (ln.unlock) tip.push((ln.ok ? '✓ ' : '• ') + ln.label);
    const unsure = st.unknown && st.unknown.has(a.id);
    tip.push('Status: ' + (isDone ? 'complete' : unsure ? 'not known (some of its requirements cannot be read yet)' : 'incomplete'));
    row.dataset.tip = tip.join('\n');
    const ico = document.createElement('div'); ico.className = 'pet-ico';
    if (a.sprite > 0) loadSpriteIcon(ico, a.sprite);
    row.appendChild(ico);
    const info = document.createElement('div'); info.className = 'bs-info';
    const top = document.createElement('div'); top.className = 'bs-top';
    const nm = document.createElement('div'); nm.className = 'bs-nm'; nm.textContent = a.name || ('#' + a.id);
    top.appendChild(nm);
    if (tier) { const tb = document.createElement('span'); tb.className = 'ach-cm t' + tier[1]; tb.textContent = tier[0]; top.appendChild(tb); }
    info.appendChild(top);
    let chip = null;
    if (!isDone) {
      if (need >= 2) chip = c.met + ' / ' + need + ' done';
      else {
        let best = null;
        for (const ln of judged) if (ln.req > 1 && ln.cur > 0 && (!best || ln.cur / ln.req > best.cur / best.req)) best = ln;
        if (best) chip = Math.min(best.cur, best.req) + ' / ' + best.req;
      }
    }
    if (a.desc || chip) {
      const sub = document.createElement('div'); sub.className = 'bs-chips';
      if (chip) { const s = document.createElement('span'); s.className = 'bs-chip m2'; s.textContent = chip; sub.appendChild(s); }
      if (a.desc) { const d = document.createElement('span'); d.className = 'ach-desc'; d.textContent = a.desc; sub.appendChild(d); }
      info.appendChild(sub);
    }
    row.appendChild(info);
    const st2 = document.createElement('div');
    st2.className = 'pet-st ' + (isDone ? 'pet-yes' : 'pet-no');
    st2.textContent = isDone ? 'Complete' : 'Incomplete';
    row.appendChild(st2);
    return row;
  }

  function renderAchievements2() {
    const c = $('content');
    let wrap = $('achWrap');
    if (!wrap) {
      c.innerHTML = ''; achListSig = '';
      wrap = document.createElement('div'); wrap.id = 'achWrap'; wrap.className = 'pk-wrap'; c.appendChild(wrap);
      const tb = document.createElement('div'); tb.className = 'pet-toolbar';
      const stRow = document.createElement('div'); stRow.className = 'pet-chips';
      ['All', 'Complete', 'In progress', 'Incomplete'].forEach((nm, i) => {
        const b = document.createElement('button'); b.className = 'pet-chip' + (i === achFStatus ? ' on' : '');
        b.textContent = nm; b.dataset.val = i; stRow.appendChild(b);
      });
      const search = document.createElement('input'); search.className = 'pet-search'; search.id = 'achSearch';
      search.placeholder = 'Search achievement...'; search.value = achFSearch;
      tb.appendChild(stRow); tb.appendChild(search); wrap.appendChild(tb);
      const cnt = document.createElement('div'); cnt.id = 'achCnt'; cnt.className = 'pet-count'; wrap.appendChild(cnt);
      const list = document.createElement('div'); list.id = 'achList'; list.className = 'pet-list'; wrap.appendChild(list);
      stRow.addEventListener('click', e => {
        const b = e.target.closest('.pet-chip'); if (!b) return;
        achFStatus = +b.dataset.val;
        stRow.querySelectorAll('.pet-chip').forEach(x => x.classList.toggle('on', x === b));
        achListSig = ''; renderAchList();
      });
      search.addEventListener('input', () => { achFSearch = search.value.toLowerCase(); achListSig = ''; renderAchList(); });
    }
    renderAchList();
  }

  function renderAchList() {
    const list = $('achList'); if (!list) return;
    if (!achDefs) { list.innerHTML = '<div class="empty">Reading achievement cache...</div>'; achListSig = ''; return; }
    const st = achState || { done: new Set(), prog: {} };
    const trackable = achDefs.filter(a => achTrackable(a) && !achIsLeagues(a) && !achTier(a));
    const started = (a) => { const ls = st.prog[a.id] || []; for (const l of ls) if (!l.unlock && l.cur > 0) return true; return false; };
    const items = trackable.filter(a => {
      if (achFSearch && (a.name || '').toLowerCase().indexOf(achFSearch) < 0 &&
          (a.desc || '').toLowerCase().indexOf(achFSearch) < 0) return false;
      const isDone = st.done.has(a.id);
      if (achFStatus === 1 && !isDone) return false;
      if (achFStatus === 2 && (isDone || !started(a))) return false;
      if (achFStatus === 3 && isDone) return false;
      return true;
    });
    items.sort((a, b) => {
      const da = st.done.has(a.id) ? 1 : 0, db = st.done.has(b.id) ? 1 : 0;
      return da - db || (a.name || '').localeCompare(b.name || '');
    });
    const doneN = trackable.filter(a => st.done.has(a.id)).length;
    const cnt = $('achCnt');
    if (cnt) cnt.textContent = doneN + ' / ' + trackable.length + ' complete' +
      (items.length !== trackable.length ? '  ·  ' + items.length + ' shown' : '') +
      (achState ? '' : '  ·  reading...');
    const sig = achFStatus + '|' + achFSearch + '|' + (achState ? 1 : 0) + '|' +
      items.slice(0, 400).map(a => a.id + (st.done.has(a.id) ? 'D' : '') +
        ':' + (st.prog[a.id] || []).reduce((s, l) => s + l.cur, 0)).join(',');
    if (sig === achListSig) return;
    achListSig = sig;
    list.innerHTML = '';
    if (!items.length) { list.innerHTML = '<div class="empty">No achievements match.</div>'; return; }
    const MAX = 400;   // cap the DOM; search narrows below it
    for (const a of items.slice(0, MAX)) list.appendChild(achRowEl(a, st));
    if (items.length > MAX) {
      const more = document.createElement('div'); more.className = 'empty';
      more.textContent = '+' + (items.length - MAX) + ' more - refine your search';
      list.appendChild(more);
    }
  }

  function cmCommonName(descs) {
    if (!descs.length) return '';
    let pre = descs[0] || '';
    for (const d of descs) {
      let i = 0; while (i < pre.length && i < d.length && pre[i] === d[i]) i++;
      pre = pre.slice(0, i);
      if (!pre) break;
    }
    pre = pre.replace(/^\s*(?:Defeat|Kill|Complete)\s+/i, '');
    pre = pre.replace(/[\s,;:.\-]+$/, '');
    if (pre.length > 46) pre = pre.slice(0, 46).replace(/\s+\S*$/, '') + '...';
    return pre.trim();
  }

  // Enum 16086 = achievement category/subcategory display names (212 entries, keyed in the cat/subcat id space).
  let cmSubcatNames = null, cmSubcatLoading = false;
  function cmSubcatName(id) {
    if (id == null) return '';
    if (!cmSubcatNames) {
      if (!cmSubcatLoading && bridge() && bridge().enumInfo) {
        cmSubcatLoading = true;
        (async () => {
          try {
            const m = JSON.parse(await rtxData.raw('cache.enumInfo', 16086) || 'null');
            if (m && Object.keys(m).length) { cmSubcatNames = m; cmPopulateBoss(); renderCmList(); }
          } catch (e) {}
          cmSubcatLoading = false;
        })();
      }
      return '';
    }
    let v = cmSubcatNames[String(id)];
    if (!v) return '';
    v = String(v).replace(/<col=[^>]*>|<\/col>/gi, '');
    const segs = v.split(/<br\s*\/?>/i).map(x => x.trim()).filter(Boolean);
    return segs.length ? segs[segs.length - 1] : v.trim();
  }
  // Bosses as the game groups them: each task's subcategory, named by enum 16086.
  function cmBosses() {
    const orphans = {};
    const byName = {};
    const addTo = (nm, a) => {
      const e = byName[nm] = byName[nm] || { name: nm, n: 0, ids: [] };
      e.n++; e.ids.push(a.id);
    };
    for (const a of (achDefs || [])) {
      if (!(achTier(a) && achTrackable(a) && !achIsLeagues(a))) continue;
      const key = (a.subcat != null) ? ('s' + a.subcat) : 'other';
      (orphans[key] = orphans[key] || []).push(a);
    }
    for (const key in orphans) {
      const list = orphans[key];
      const subcat = list[0].subcat;
      const nm = cmSubcatName(subcat)
              || cmCommonName(list.map(x => x.desc || x.name || ''))
              || (list.length === 1 ? (list[0].name || '') : '')
              || (subcat != null ? ('Category #' + subcat) : 'Uncategorised');
      for (const a of list) addTo(nm, a);
    }
    let synth = 900000000;
    const m = {};
    for (const nm of Object.keys(byName).sort()) m[synth++] = byName[nm];
    return m;
  }
  function cmPopulateBoss() {
    const dd = $('cmBoss'); if (!dd || !dd.setItems) return;
    const m = cmBosses(); const ids = Object.keys(m).map(Number).sort((a, b) => m[a].name.localeCompare(m[b].name));
    // Signature on the NAMES, not the count: the enum 16086 load renames groups without
    const sig = ids.map(i => m[i].name + ':' + m[i].n).join('|');
    if (dd.dataset.n === sig) return;
    dd.dataset.n = sig;
    const items = [{ value: -1, label: 'All bosses' }];
    for (const id of ids) items.push({ value: id, label: m[id].name + ' (' + m[id].n + ')' });
    dd.setItems(items, cmFBoss);
  }

  function renderCombatMastery() {
    const c = $('content');
    let wrap = $('cmWrap');
    if (!wrap) {
      c.innerHTML = ''; cmListSig = '';
      wrap = document.createElement('div'); wrap.id = 'cmWrap'; wrap.className = 'pk-wrap'; c.appendChild(wrap);
      const tb = document.createElement('div'); tb.className = 'pet-toolbar';
      const stRow = document.createElement('div'); stRow.className = 'pet-chips';
      ['All', 'Complete', 'In progress', 'Incomplete'].forEach((nm, i) => {
        const b = document.createElement('button'); b.className = 'pet-chip' + (i === cmFStatus ? ' on' : '');
        b.textContent = nm; b.dataset.val = i; stRow.appendChild(b);
      });
      const search = document.createElement('input'); search.className = 'pet-search'; search.id = 'cmSearch';
      search.placeholder = 'Search combat task...'; search.value = cmFSearch;
      tb.appendChild(stRow); tb.appendChild(search); wrap.appendChild(tb);
      const tierRow = document.createElement('div'); tierRow.className = 'pet-toolbar';
      const tchips = document.createElement('div'); tchips.className = 'pet-chips';
      const mkTier = (label, val) => {
        const b = document.createElement('button'); b.className = 'pet-chip' + (val === cmFTier ? ' on' : '');
        b.textContent = label; b.dataset.tier = val; return b;
      };
      tchips.appendChild(mkTier('All tiers', -1));
      ACH_CM_TIERS.forEach((nm, i) => tchips.appendChild(mkTier(nm, i)));
      tierRow.appendChild(tchips); wrap.appendChild(tierRow);
      const bossRow = document.createElement('div'); bossRow.className = 'pet-toolbar';
      const bdd = mkDropdown(String(cmFBoss), (v) => { cmFBoss = +v; cmListSig = ''; renderCmList(); }); bdd.id = 'cmBoss';
      bossRow.appendChild(bdd); wrap.appendChild(bossRow);
      const cnt = document.createElement('div'); cnt.id = 'cmCnt'; cnt.className = 'pet-count'; wrap.appendChild(cnt);
      const list = document.createElement('div'); list.id = 'cmList'; list.className = 'pet-list'; wrap.appendChild(list);
      stRow.addEventListener('click', e => {
        const b = e.target.closest('.pet-chip'); if (!b) return;
        cmFStatus = +b.dataset.val;
        stRow.querySelectorAll('.pet-chip').forEach(x => x.classList.toggle('on', x === b));
        cmListSig = ''; renderCmList();
      });
      tchips.addEventListener('click', e => {
        const b = e.target.closest('.pet-chip'); if (!b) return;
        cmFTier = +b.dataset.tier;
        tchips.querySelectorAll('.pet-chip').forEach(x => x.classList.toggle('on', x === b));
        cmListSig = ''; renderCmList();
      });
      search.addEventListener('input', () => { cmFSearch = search.value.toLowerCase(); cmListSig = ''; renderCmList(); });
    }
    cmPopulateBoss();
    renderCmList();
  }

  function renderCmList() {
    const list = $('cmList'); if (!list) return;
    if (!achDefs) { list.innerHTML = '<div class="empty">Reading achievement cache...</div>'; cmListSig = ''; return; }
    const st = achState || { done: new Set(), prog: {} };
    const all = achDefs.filter(a => achTrackable(a) && !achIsLeagues(a) && achTier(a));
    const started = (a) => { const ls = st.prog[a.id] || []; for (const l of ls) if (!l.unlock && l.cur > 0) return true; return false; };
    const inTier = (a) => { if (cmFTier < 0) return true; const t = achTier(a); return t && t[1] === cmFTier; };
    const bosses = cmBosses();
    const synthSel = (cmFBoss >= 0 && bosses[cmFBoss] && bosses[cmFBoss].ids)
                      ? new Set(bosses[cmFBoss].ids) : null;
    const inBoss = (a) => {
      if (cmFBoss < 0) return true;
      return !!(synthSel && synthSel.has(a.id));
    };
    const items = all.filter(a => {
      if (!inTier(a) || !inBoss(a)) return false;
      if (cmFSearch && (a.name || '').toLowerCase().indexOf(cmFSearch) < 0 &&
          (a.desc || '').toLowerCase().indexOf(cmFSearch) < 0) return false;
      const isDone = st.done.has(a.id);
      if (cmFStatus === 1 && !isDone) return false;
      if (cmFStatus === 2 && (isDone || !started(a))) return false;
      if (cmFStatus === 3 && isDone) return false;
      return true;
    });
    items.sort((a, b) => {
      const ta = achTier(a), tb = achTier(b);
      const ia = ta ? ta[1] : 99, ib = tb ? tb[1] : 99; if (ia !== ib) return ia - ib;
      const da = st.done.has(a.id) ? 1 : 0, db = st.done.has(b.id) ? 1 : 0;
      return da - db || (a.name || '').localeCompare(b.name || '');
    });
    const pool = all.filter(a => inTier(a) && inBoss(a));
    const doneN = pool.filter(a => st.done.has(a.id)).length;
    const cnt = $('cmCnt');
    if (cnt) cnt.textContent = doneN + ' / ' + pool.length + ' complete' +
      (cmFTier >= 0 ? '  ·  ' + ACH_CM_TIERS[cmFTier] : '') +
      (items.length !== pool.length ? '  ·  ' + items.length + ' shown' : '') +
      (achState ? '' : '  ·  reading...');
    const sig = cmFStatus + '|' + cmFTier + '|' + cmFBoss + '|' + cmFSearch + '|' + (achState ? 1 : 0) + '|' +
      items.slice(0, 400).map(a => a.id + (st.done.has(a.id) ? 'D' : '') +
        ':' + (st.prog[a.id] || []).reduce((s, l) => s + l.cur, 0)).join(',');
    if (sig === cmListSig) return;
    cmListSig = sig;
    list.innerHTML = '';
    if (!items.length) { list.innerHTML = '<div class="empty">No combat tasks match.</div>'; return; }
    const MAX = 400;
    for (const a of items.slice(0, MAX)) list.appendChild(achRowEl(a, st));
    if (items.length > MAX) {
      const more = document.createElement('div'); more.className = 'empty';
      more.textContent = '+' + (items.length - MAX) + ' more - refine your search';
      list.appendChild(more);
    }
  }

Object.assign(window, { achBitFromVbVal, achBitReqs, achDefById, achIsLeagues, achLoadDefs, achVarpReqs, fetchAchievements, mkDropdown, pluginAchievements });
registerTab({ id: 'achievements', render: renderAchievements2, open: function () { achListSig = ''; fetchAchievements(true); } });
registerTab({ id: 'combatmastery', render: renderCombatMastery, open: function () { cmListSig = ''; fetchAchievements(true); } });
})();
