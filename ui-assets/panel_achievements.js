// RuneToolsX panel: Achievements + Combat Mastery (two tabs sharing one cache fetch;
// Combat Mastery = achievements with a combat_mastery_category).
// Spliced inline into client.html; bare classic script sharing one global scope.

  // Definitions come from the live cache (js5-57 via bridge.achievements()); each leaf carries
  // op-14 requirements [{value, desc, varbits[]}], satisfied when the live varbit value >= target.
  let achDefs = null;
  let achState = null;
  let achFetching = false, achListSig = '', achFetchAt = 0;
  let achFSearch = '', achFStatus = 0;   // filter: 0 all / 1 complete / 2 in progress / 3 incomplete
  let cmFSearch = '', cmFStatus = 0, cmFTier = -1, cmFBoss = -1, cmListSig = '';
  // combat_mastery_category id -> [tier name, tier index 0..5]
  const ACH_CM = { 13980: ['Easy', 0], 14042: ['Medium', 1], 14305: ['Hard', 2],
                   14313: ['Elite', 3], 14314: ['Master', 4], 14420: ['Grandmaster', 5] };
  const ACH_CM_TIERS = ['Easy', 'Medium', 'Hard', 'Elite', 'Master', 'Grandmaster'];
  function achTier(a) { return (a.cm != null && ACH_CM[a.cm]) ? ACH_CM[a.cm] : null; }
  // Leagues tasks ship as nameless, category-5619, hidden achievements (empty-name trackable
  // <=> cat 5619), not real account achievements. Excluded everywhere.
  const ACH_LEAGUES_CAT = 5619;
  function achIsLeagues(a) { return a.cat === ACH_LEAGUES_CAT || !(a.name && a.name.trim()); }
  // Bit requirements, normalized into their two id spaces:
  //   op 25 -> vbits [{vb, bit, n}]: bit BIT of VARBIT vb's VALUE
  //   op 23 -> vpbits [{vp, bit, n}]: bit BIT of VARP vp
  // Pre-split launcher builds merge both ops under "reqs23" where the op is unknowable; those
  // are classified by whether the bit fits inside the id's varbit definition.
  function achBitReqs(a) {
    const vbits = [], vpbits = [];
    for (const q of (a.reqs25 || [])) vbits.push({ vb: q.vb, bit: q.bit, n: q.n || '' });
    for (const q of (a.reqsvpb || [])) vpbits.push({ vp: q.vp, bit: q.bit, n: q.n || '' });
    for (const q of (a.reqs23 || [])) {
      const r = storageVbMap && storageVbMap[q.vp];
      if (r && q.bit <= r.msb - r.lsb) vbits.push({ vb: q.vp, bit: q.bit, n: q.n || '' });
      else vpbits.push({ vp: q.vp, bit: q.bit, n: q.n || '' });
    }
    return { vbits: vbits, vpbits: vpbits };
  }
  // One op-25 bit read out of a varbit's live VALUE; shared so the Achievements / Mysteries /
  // Vars panels can never disagree on the bit indexing.
  function achBitFromVbVal(vbId, vbVal, bit) { return ((vbVal || 0) >>> bit) & 1; }
  // op 13 varp reqs, normalized: [{vps, v, n}] - the SUM of the varps' live values >= v.
  function achVarpReqs(a) {
    const out = [];
    for (const q of (a.reqsvp || [])) out.push({ vps: q.vps || [q.vp], v: q.v, n: q.n || '' });
    return out;
  }
  // Trackable = has any live-checkable requirement group.
  function achTrackable(a) {
    return !!((a.reqs && a.reqs.length) || (a.reqs23 && a.reqs23.length) ||
              (a.reqs25 && a.reqs25.length) || (a.reqsvpb && a.reqsvpb.length) ||
              (a.reqsvp && a.reqsvp.length));
  }
  // How many requirements must be satisfied. `needN` (cache op 30): [1] = ANY one (OR),
  // [n]==reqs = ALL (AND); multi-element groups are summed. Absent -> AND all reqs.
  function achNeed(a) {
    if (a.needN && a.needN.length) return a.needN.reduce((s, x) => s + x, 0);
    return (a.reqs || []).length + (a.reqs23 || []).length + (a.reqs25 || []).length +
           (a.reqsvpb || []).length + (a.reqsvp || []).length;
  }

  // id -> achDef, and child-id -> parent achDef (from each parent's op-15 `subach` list).
  let _achDefById = null, _achLeafParent = null;
  function achDefById() { if (!_achDefById && achDefs) { _achDefById = {}; for (const a of achDefs) _achDefById[a.id] = a; } return _achDefById || {}; }
  function achLeafParent() { if (!_achLeafParent && achDefs) { _achLeafParent = {}; for (const p of achDefs) if (p.subach) for (const c of p.subach) _achLeafParent[c] = p; } return _achLeafParent || {}; }

  // Custom dark dropdown (Ultralight renders native <select> popups OS-white). Returns a wrapper
  // div with .setItems(items, value) + .getValue(); onChange(value) fires on pick.
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

  // Combat task -> its BOSS parent. The six "Combat Mastery - <tier>" parents are tier rollups,
  // so they are skipped: each task maps to its boss, not its tier bucket.
  let _cmLeafBoss = null;
  function cmLeafBoss() {
    if (!_cmLeafBoss && achDefs) {
      _cmLeafBoss = {};
      for (const p of achDefs) {
        if (!p.subach || /^Combat Mastery\b/i.test(p.name || '')) continue;
        for (const c of p.subach) _cmLeafBoss[c] = p;
      }
    }
    return _cmLeafBoss || {};
  }

  async function fetchAchievements(force) {
    if (!bridge() || achFetching) return;
    const _t = Date.now(); if (!force && _t - achFetchAt < 2500) return; achFetchAt = _t;
    achFetching = true;
    try {
      if (!achDefs) { try { achDefs = JSON.parse(await bridge().achievements()) || []; } catch (e) { achDefs = []; } }
      await ensureVbMap();
      // Leagues tasks are excluded here: they would add ~750 entries to the single varp read.
      const vps = new Set();
      for (const a of achDefs) {
        if (achIsLeagues(a)) continue;
        for (const q of (a.reqs || [])) for (const vb of q.varbits) {
          const r = storageVbMap && storageVbMap[vb]; if (r) vps.add(r.varp);
        }
        const br = achBitReqs(a);
        for (const b of br.vbits) { const r = storageVbMap && storageVbMap[b.vb]; if (r) vps.add(r.varp); }
        for (const b of br.vpbits) vps.add(b.vp);
        for (const q of achVarpReqs(a)) for (const id of q.vps) vps.add(id);
      }
      let vp = {};
      if (vps.size && bridge().varps) { try { vp = JSON.parse(await bridge().varps(myPid(), [...vps].join(','))); } catch (e) {} }
      const done = new Set(), prog = {};
      for (const a of achDefs) {
        if (!achTrackable(a) || achIsLeagues(a)) continue;
        let sat = 0; const lines = [];
        for (const q of (a.reqs || [])) {
          // multi-varbit requirement: sum the fields
          let cur = 0; const srcs = [];
          for (const vb of q.varbits) {
            const r = storageVbMap && storageVbMap[vb];
            cur += (readVb(vb, vp) || 0);
            srcs.push(r ? ('varbit ' + vb + ' = varp ' + r.varp + ' bits ' + r.lsb + '-' + r.msb) : ('varbit ' + vb));
          }
          const okq = cur >= q.value; if (okq) sat++;
          lines.push({ label: q.desc || '', cur: cur, req: q.value, ok: okq, src: srcs.join(' + ') });
        }
        const br = achBitReqs(a);
        for (const b of br.vbits) {               // op 25: one bit of a varbit's value
          const bv = achBitFromVbVal(b.vb, readVb(b.vb, vp) || 0, b.bit);
          const okq = bv >= 1; if (okq) sat++;
          const r = storageVbMap && storageVbMap[b.vb];
          lines.push({ label: b.n, cur: bv, req: 1, ok: okq,
                       src: 'varbit ' + b.vb + ' bit ' + b.bit + (r ? ' (varp ' + r.varp + ')' : '') });
        }
        for (const b of br.vpbits) {              // op 23: one bit of a varp
          const bv = ((+vp[b.vp] || 0) >>> b.bit) & 1;
          const okq = bv >= 1; if (okq) sat++;
          lines.push({ label: b.n, cur: bv, req: 1, ok: okq, src: 'varp ' + b.vp + ' bit ' + b.bit });
        }
        for (const q of achVarpReqs(a)) {         // op 13: sum of live varps >= value
          let cur = 0; for (const id of q.vps) cur += (+vp[id] || 0);
          const okq = cur >= q.v; if (okq) sat++;
          lines.push({ label: q.n, cur: cur, req: q.v, ok: okq, src: 'varp ' + q.vps.join(' + ') });
        }
        // Completion must be PROVEN, never assumed. With no requirements to check, sat 0 >=
        // need 0 marked the entry done, which (a) reported achievements we cannot evaluate as
        // complete and (b) pre-empted the rollup pass below, since it skips anything already
        // done - so a set like "Ardougne Set Tasks - Easy" read complete at 6/23 tasks.
        if (achNeed(a) > 0 && sat >= achNeed(a)) done.add(a.id);
        prog[a.id] = lines;
      }
      // Rollups (a `subach` list) are complete when `needN` (else all) of their subs are; run to a
      // fixed point so NESTED rollups propagate.
      let chg = true, guard = 0;
      while (chg && guard++ < 24) {
        chg = false;
        for (const p of achDefs) {
          if (done.has(p.id) || achIsLeagues(p) || !p.subach || !p.subach.length) continue;
          let sub = 0; for (const c of p.subach) if (done.has(c)) sub++;
          const need = (p.needN && p.needN.length) ? p.needN.reduce((s, x) => s + x, 0) : p.subach.length;
          if (sub >= need) { done.add(p.id); chg = true; }
        }
      }
      // Anything with no requirements of its own and no sub-achievements cannot be judged
      // either way; callers should treat these as unknown rather than incomplete.
      const unknown = new Set();
      for (const a of achDefs) {
        if (done.has(a.id) || achIsLeagues(a)) continue;
        if (achNeed(a) === 0 && !(a.subach && a.subach.length)) unknown.add(a.id);
      }
      achState = { done: done, prog: prog, unknown: unknown };
    } finally { achFetching = false; }
    paneRun('achievements', renderAchievements2);
    paneRun('combatmastery', renderCombatMastery);
  }

  async function pluginAchievements(pid) {
    if (!achDefs) { try { achDefs = JSON.parse(await bridge().achievements()) || []; } catch (e) { achDefs = []; } }
    await ensureVbMap();
    const vps = new Set();
    for (const a of achDefs) {
      if (achIsLeagues(a)) continue;
      for (const q of (a.reqs || [])) for (const vb of q.varbits) {
        const r = storageVbMap && storageVbMap[vb]; if (r) vps.add(r.varp);
      }
      const br = achBitReqs(a);
      for (const b of br.vbits) { const r = storageVbMap && storageVbMap[b.vb]; if (r) vps.add(r.varp); }
      for (const b of br.vpbits) vps.add(b.vp);
      for (const q of achVarpReqs(a)) for (const id of q.vps) vps.add(id);
    }
    let vp = {};
    if (vps.size && bridge().varps) { try { vp = JSON.parse(await bridge().varps(pid, [...vps].join(','))); } catch (e) {} }
    const out = [];
    for (const a of achDefs) {
      if (!achTrackable(a) || achIsLeagues(a)) continue;
      let sat = 0; const reqs = [];
      for (const q of (a.reqs || [])) {
        let cur = 0; for (const vb of q.varbits) cur += (readVb(vb, vp) || 0);
        const ok = cur >= q.value; if (ok) sat++;
        reqs.push({ description: q.desc || '', current: cur, target: q.value, complete: ok, varbits: q.varbits.slice() });
      }
      const br = achBitReqs(a);
      for (const b of br.vbits) {               // op 25: one bit of a varbit's value
        const bv = achBitFromVbVal(b.vb, readVb(b.vb, vp) || 0, b.bit);
        const ok = bv >= 1; if (ok) sat++;
        reqs.push({ description: b.n, current: bv, target: 1, complete: ok, varbits: [b.vb] });
      }
      for (const b of br.vpbits) {              // op 23: one bit of a varp
        const bv = ((+vp[b.vp] || 0) >>> b.bit) & 1;
        const ok = bv >= 1; if (ok) sat++;
        reqs.push({ description: b.n, current: bv, target: 1, complete: ok, varbits: [], varps: [b.vp] });
      }
      for (const q of achVarpReqs(a)) {         // op 13: sum of live varps >= value
        let cur = 0; for (const id of q.vps) cur += (+vp[id] || 0);
        const ok = cur >= q.v; if (ok) sat++;
        reqs.push({ description: q.n, current: cur, target: q.v, complete: ok, varbits: [], varps: q.vps.slice() });
      }
      const need = achNeed(a);
      const tier = achTier(a);
      out.push({ id: a.id, name: a.name || '', description: a.desc || '', reward: a.reward || '',
                 points: a.points || 0, complete: sat >= need, requirementsNeeded: need,
                 combatMasteryTier: tier ? tier[0] : null, requirements: reqs });
    }
    return out;
  }

  function achRowEl(a, st) {
    const isDone = st.done.has(a.id);
    const row = document.createElement('div'); row.className = 'pet-row' + (isDone ? '' : ' pet-locked');
    const tier = achTier(a);
    const lines = st.prog[a.id] || [];
    const need = achNeed(a);
    const tip = [a.name];
    if (a.desc) tip.push(a.desc);
    if (tier) tip.push('Combat mastery: ' + tier[0] + ' (source: cache combat_mastery_category ' + a.cm + ')');
    if (a.reward) tip.push('Reward: ' + a.reward);
    if (a.points) tip.push(a.points + ' achievement points');
    if (lines.length > 1) tip.push('Requires ' + need + ' of ' + lines.length + ' below' +
      (need === 1 ? ' (any one)' : need >= lines.length ? ' (all)' : ''));
    for (const ln of lines) {
      const head = ln.label ? ln.label : 'Requirement';
      tip.push((ln.ok ? '✓ ' : '• ') + head + '  (' + ln.cur + '/' + ln.req + ')');
      if (ln.src) tip.push('    Source: ' + ln.src);
    }
    tip.push('Status: ' + (isDone ? 'complete' : 'incomplete'));
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
    // Progress chip: never show "3/3 but Incomplete". need >= 2 -> progress toward the requirement
    // COUNT; need 1 -> the best single count's progress.
    let chip = null;
    if (!isDone) {
      if (need >= 2) {
        const sat = lines.reduce((s, l) => s + (l.ok ? 1 : 0), 0);
        chip = sat + ' / ' + need + ' done';
      } else {
        let best = null;
        for (const ln of lines) if (ln.req > 1 && ln.cur > 0 && (!best || ln.cur / ln.req > best.cur / best.req)) best = ln;
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
    const started = (a) => { const ls = st.prog[a.id] || []; for (const l of ls) if (l.cur > 0) return true; return false; };
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

  // The "boss" of a combat task = the PARENT achievement listing it in `subach`; all combat
  // tasks share one flat category, so the category is NOT the boss.
  // Returns { parentId -> { name, n } } over the combat tasks that have a parent.
  // Longest common prefix of a set of strings, trimmed at a word boundary and stripped of a
  // leading verb. Used to NAME a group of orphan tasks from what they say: "Defeat Ivar, King
  // of Bones, solo, using..." + "Defeat Ivar, King of Bones." -> "Ivar, King of Bones".
  function cmCommonName(descs) {
    if (!descs.length) return '';
    let pre = descs[0] || '';
    for (const d of descs) {
      let i = 0; while (i < pre.length && i < d.length && pre[i] === d[i]) i++;
      pre = pre.slice(0, i);
      if (!pre) break;
    }
    pre = pre.replace(/^\s*(?:Defeat|Kill|Complete)\s+/i, '');
    // Back off to the last clean boundary so a half-word is never shown.
    pre = pre.replace(/[\s,;:.\-]+$/, '');
    if (pre.length > 46) pre = pre.slice(0, 46).replace(/\s+\S*$/, '') + '...';
    return pre.trim();
  }

  // Boss -> task count for the dropdown.
  //
  // A combat task normally hangs off a BOSS achievement, and that parent is the group. Some
  // do not: Ivar's two tasks are listed by no parent at all, so keying purely on the parent
  // dropped the boss from this list entirely while "All bosses" still showed its tasks --
  // visible in the list, impossible to filter to. Orphans are now grouped by their own
  // subcategory and named from what their descriptions have in common, so a boss the cache
  // has not parented is still selectable instead of silently missing.
  function cmBosses() {
    const lb = cmLeafBoss(); const m = {};
    const orphans = {};
    for (const a of (achDefs || [])) {
      if (!(achTier(a) && achTrackable(a) && !achIsLeagues(a))) continue;
      const p = lb[a.id];
      if (p) {
        if (!m[p.id]) m[p.id] = { name: p.name || ('#' + p.id), n: 0 };
        m[p.id].n++;
        continue;
      }
      const key = (a.subcat != null) ? ('s' + a.subcat) : 'other';
      (orphans[key] = orphans[key] || []).push(a);
    }
    // Synthetic ids sit far above real achievement ids so they can never collide with one.
    let synth = 900000000;
    for (const key in orphans) {
      const list = orphans[key];
      const nm = cmCommonName(list.map(x => x.desc || x.name || '')) ||
                 (list.length === 1 ? (list[0].name || 'Other') : 'Other');
      const id = synth++;
      m[id] = { name: nm, n: list.length, ids: list.map(x => x.id) };
    }
    return m;
  }
  function cmPopulateBoss() {
    const dd = $('cmBoss'); if (!dd || !dd.setItems) return;
    const m = cmBosses(); const ids = Object.keys(m).map(Number).sort((a, b) => m[a].name.localeCompare(m[b].name));
    if (dd.dataset.n === String(ids.length)) return;
    dd.dataset.n = String(ids.length);
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
    const started = (a) => { const ls = st.prog[a.id] || []; for (const l of ls) if (l.cur > 0) return true; return false; };
    const inTier = (a) => { if (cmFTier < 0) return true; const t = achTier(a); return t && t[1] === cmFTier; };
    const lb = cmLeafBoss();
    // Real boss parents match by parent id; the synthetic orphan groups (see cmBosses)
    // carry their member ids instead, since those tasks have no parent to match on.
    const bosses = cmBosses();
    const synthSel = (cmFBoss >= 0 && bosses[cmFBoss] && bosses[cmFBoss].ids)
                      ? new Set(bosses[cmFBoss].ids) : null;
    const inBoss = (a) => {
      if (cmFBoss < 0) return true;
      if (synthSel) return synthSel.has(a.id);
      const p = lb[a.id]; return !!(p && p.id === cmFBoss);
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
