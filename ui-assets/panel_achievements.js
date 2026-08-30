// RuneToolsX panel: Achievements + Combat Mastery (two tabs sharing one cache fetch;
// Combat Mastery = achievements with a combat_mastery_category).
// Spliced inline into client.html; IIFE (window exports + registerTab; see the RTX registry in client.html).
(function () {

  // Definitions come from the live cache (js5-57 via bridge.achievements()); each leaf carries
  // op-14 requirements [{value, desc, varbits[]}], satisfied when the live varbit value >= target.
  achDefs = null;
  achState = null;
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
    // The game judges SEVEN requirement kinds (script19623): the five var-based ones, skill
    // levels (op 12) and prerequisite achievements (op 11). All seven make an achievement
    // trackable and all seven count toward completion.
    return !!((a.reqs && a.reqs.length) || (a.reqs23 && a.reqs23.length) ||
              (a.reqs25 && a.reqs25.length) || (a.reqsvpb && a.reqsvpb.length) ||
              (a.reqsvp && a.reqsvp.length) || (a.skills && a.skills.length) ||
              (a.prev && a.prev.length));
  }
  // How many requirements must be satisfied. `needN` (cache op 30): [1] = ANY one (OR),
  // [n]==reqs = ALL (AND); multi-element groups are summed. Absent -> AND all reqs.
  // needN (op 30) applies to the VAR-BASED requirements only. Skill levels and prerequisite
  // achievements are separate ALL-REQUIRED gates: the game has a dedicated ALLPREREQMET op,
  // and pooling them produced nonsense like Bug Swatter III reading "1 of 3 (any one)" across
  // [25 kills, completed-flag, Complete Bug Swatter II] - the truth is BS II required AND
  // 1-of-2 on the counter/flag pair.
  // How many of the VARIABLE requirements must be satisfied. needN comes from op 30
  // (subreq_count), which counts subrequirements in general, so it can name a number this
  // achievement has no variable requirements for: every skill milestone (Archaeology 5, 10,
  // 110 and the rest of that family) carries subreq_count 1 while its only requirement is the
  // op 12 skill gate. Comparing 0 satisfied against a required 1 left them permanently
  // incomplete, and the prerequisite chain blocked every level above them too. Clamp to the
  // pool that actually exists: with no variable requirements the judgement rests on the
  // gates, which is how the game reads them.
  function achPool(a) {
    return (a.reqs || []).length + (a.reqs23 || []).length + (a.reqs25 || []).length +
           (a.reqsvpb || []).length + (a.reqsvp || []).length;
  }
  function achNeed(a) {
    const pool = achPool(a);
    if (a.needN && a.needN.length) return Math.min(pool, a.needN.reduce((s, x) => s + x, 0));
    return pool;
  }
  function achGatesTotal(a) { return (a.skills || []).length + (a.prev || []).length; }

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
      if (!achDefs) { try { achDefs = JSON.parse(await rtxData.raw('cache.achievements')) || []; } catch (e) { achDefs = []; } }
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
      if (vps.size && bridge().varps) { try { vp = JSON.parse(await rtxData.raw('state.varps', [...vps].join(','))); } catch (e) {} }
      const done = new Set(), prog = {}, baseSat = {}, baseGates = {};
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
          // Single unnamed counter req: the achievement's own description IS its text
          // (that is how the game presents it - see the render-side comment on script10988).
          const lbl = q.desc || ((a.reqs.length === 1 && q.value > 1 && a.desc) ? a.desc : '');
          lines.push({ label: lbl, cur: cur, req: q.value, ok: okq, src: srcs.join(' + ') });
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
        // op 12: skill-level requirements, judged live against the skill panel. These count
        // toward achNeed, so skipping them here after they started counting there misjudged
        // every mixed achievement as incomplete.
        const liveSk = (typeof lastSnap !== 'undefined' && lastSnap && Array.isArray(lastSnap.skills)) ? lastSnap.skills : null;
        let gatesOk = true;
        for (const sq of (a.skills || [])) {          // hard gate, never part of the n-of-M pool
          const sid = sq[0] | 0, lvl = sq[1] | 0;
          const cur = (liveSk && liveSk[sid]) ? (liveSk[sid][0] | 0) : 0;
          const okq = cur >= lvl; if (!okq) gatesOk = false;
          lines.push({ label: 'Level ' + lvl + ' ' + ((typeof SKILL_NAMES !== 'undefined' && SKILL_NAMES[sid]) || ('skill ' + sid)),
                       cur: cur, req: lvl, ok: okq, src: 'live skill ' + sid, gate: true });
        }
        // op 11: prerequisite achievements. Judged in the fixed-point pass below, where the
        // referenced achievement's own done state is known; recorded pending here.
        for (const pid2 of (a.prev || [])) lines.push({ label: '', cur: 0, req: 1, ok: false, src: 'achievement ' + pid2, prereqOf: pid2, gate: true });
        // Completion must be PROVEN, never assumed. With no requirements to check, sat 0 >=
        // need 0 marked the entry done, which (a) reported achievements we cannot evaluate as
        // complete and (b) pre-empted the rollup pass below, since it skips anything already
        // done - so a set like "Ardougne Set Tasks - Easy" read complete at 6/23 tasks.
        baseSat[a.id] = sat; baseGates[a.id] = gatesOk;
        const varNeed = achNeed(a);
        const provable = varNeed > 0 || achGatesTotal(a) > 0;
        // Done now only when every judged piece passes AND nothing waits on a prereq (those
        // resolve in the fixed point below).
        if (provable && (varNeed === 0 || sat >= varNeed) && gatesOk && !(a.prev && a.prev.length) && varNeed + (a.skills || []).length > 0)
          done.add(a.id);
        prog[a.id] = lines;
      }
      // Fixed point over BOTH derived kinds: rollups (a `subach` list, complete when needN
      // else all of the subs are) and prerequisite requirements (op 11, complete when the
      // referenced achievement is done). Both can chain through each other, so they share
      // one loop; the prereq lines' ok/cur update as they resolve so the tooltip shows the
      // judged state, not the pending one.
      const defsById = achDefById();
      let chg = true, guard = 0;
      while (chg && guard++ < 24) {
        chg = false;
        for (const p of achDefs) {
          if (done.has(p.id) || achIsLeagues(p)) continue;
          if (p.subach && p.subach.length) {
            let sub = 0; for (const c of p.subach) if (done.has(c)) sub++;
            const need = (p.needN && p.needN.length) ? p.needN.reduce((s, x) => s + x, 0) : p.subach.length;
            if (sub >= need) { done.add(p.id); chg = true; continue; }
          }
          if (p.prev && p.prev.length && baseSat[p.id] !== undefined) {
            let prevOk = true;
            for (const ln of (prog[p.id] || [])) {
              if (ln.prereqOf === undefined) continue;
              if (!ln.label) ln.label = 'Requires achievement: ' + ((defsById[ln.prereqOf] && defsById[ln.prereqOf].name) || ('achievement #' + ln.prereqOf));
              const ok = done.has(ln.prereqOf);
              if (ok !== ln.ok) { ln.ok = ok; ln.cur = ok ? 1 : 0; }
              if (!ln.ok) prevOk = false;
            }
            // Prereqs are an ALL gate on top of the var pool and the skill gate, mirroring
            // the game's ALLPREREQMET; needN never spans them.
            const varNeed = achNeed(p);
            if (prevOk && baseGates[p.id] !== false && (varNeed === 0 || baseSat[p.id] >= varNeed)) { done.add(p.id); chg = true; }
          }
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
    if (!achDefs) { try { achDefs = JSON.parse(await rtxData.raw('cache.achievements')) || []; } catch (e) { achDefs = []; } }
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
        // Same wording as the tooltip: an unnamed requirement says what it counts, not the
        // name of an internal var (most carry no text in the cache at all).
        const dsc = q.desc || ((q.value > 1 && a.desc) ? a.desc
                    : (q.value > 1 ? 'Counted by the game' : 'Marked complete by the game'));
        reqs.push({ description: dsc, current: cur, target: q.value, complete: ok, varbits: q.varbits.slice() });
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
      // op 12: skill levels: a hard ALL gate beside the var pool (never inside needN).
      const liveSk = (lastSnap && Array.isArray(lastSnap.skills)) ? lastSnap.skills : null;
      let gatesOk = true;
      for (const sq of (a.skills || [])) {
        const sid = sq[0] | 0, lvl = sq[1] | 0;
        const cur = (liveSk && liveSk[sid]) ? (liveSk[sid][0] | 0) : 0;
        const ok = cur >= lvl; if (!ok) gatesOk = false;
        reqs.push({ description: 'Level ' + lvl + ' ' + ((typeof SKILL_NAMES !== 'undefined' && SKILL_NAMES[sid]) || ('skill ' + sid)),
                    current: cur, target: lvl, complete: ok, varbits: [], gate: true });
      }
      // op 11: prerequisite achievements: the ALLPREREQMET gate, resolved below.
      for (const pid2 of (a.prev || [])) {
        reqs.push({ description: '', current: 0, target: 1, complete: false, varbits: [], prereqOf: pid2, gate: true });
      }
      const need = achNeed(a);
      const tier = achTier(a);
      out.push({ id: a.id, name: a.name || '', description: a.desc || '', reward: a.reward || '',
                 points: a.points || 0, complete: (need === 0 || sat >= need) && gatesOk && !(a.prev && a.prev.length) && (need + (a.skills || []).length > 0),
                 requirementsNeeded: need,
                 combatMasteryTier: tier ? tier[0] : null, requirements: reqs, _sat: sat, _gates: gatesOk });
    }
    // Resolve prerequisite-achievement requirements. A prereq's completion can depend on
    // other achievements in this same result, so iterate to a fixed point (chains are short;
    // 8 passes covers any real depth) instead of trusting one walk's ordering.
    const byId = {}; for (const r of out) byId[r.id] = r;
    for (let pass = 0; pass < 8; pass++) {
      let changed = false;
      for (const r of out) {
        let prevSeen = false, prevOk = true;
        for (const q of r.requirements) {
          if (q.prereqOf === undefined) continue;
          prevSeen = true;
          const dep = byId[q.prereqOf];
          const defs = achDefById();
          if (!q.description) q.description = 'Requires achievement: ' + ((defs[q.prereqOf] && defs[q.prereqOf].name) || ('achievement #' + q.prereqOf));
          const ok = !!(dep && dep.complete);
          if (ok !== q.complete) { q.complete = ok; q.current = ok ? 1 : 0; changed = true; }
          if (!q.complete) prevOk = false;
        }
        if (!prevSeen) continue;
        const varOk = r.requirementsNeeded === 0 || r._sat >= r.requirementsNeeded;
        const nowDone = varOk && r._gates !== false && prevOk;
        if (nowDone !== r.complete) { r.complete = nowDone; changed = true; }
      }
      if (!changed) break;
    }
    for (const r of out) { delete r._sat; delete r._gates; }
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
    const poolLines = lines.filter(ln => !ln.gate);
    if (poolLines.length > 1) tip.push(need === 1 ? 'Do any one of these:'
      : need >= poolLines.length ? 'Do all of these:' : ('Do any ' + need + ' of these:'));
    if (lines.some(ln => ln.gate)) tip.push('Lines marked with a lock are required on top of that.');
    for (const ln of lines) {
      // Many requirements carry NO description in the cache: the game's own requirement-text
      // builder (script10988) reads the same per-requirement string and simply skips the line
      // when it is empty, letting the achievement's description plus aggregate progress carry
      // the meaning. There is no hidden text to recover, so fall back to an honest shape-based
      // label: a target above 1 is a progress counter, a target of exactly 1 is the
      // completion flag the server sets when the achievement is judged done.
      // Most requirements carry no text in the cache (the game's own builder, script 10988,
      // skips the empty ones and lets the achievement description carry the meaning), so an
      // unnamed line says what it is counting rather than naming an internal var: a target
      // above 1 is a tally the game keeps, a target of 1 is the flag it sets when done.
      let head = ln.label;
      if (!head) head = ln.req > 1 ? (a.desc || 'Counted by the game') : 'Marked complete by the game';
      const count = ln.req > 1 ? ('  ' + ln.cur + ' of ' + ln.req) : '';
      tip.push((ln.ok ? '✓ ' : ln.gate ? '• ' : '• ') + head + count);
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
  // NAMES COME FROM THE GAME. Enum 16086 is the achievement category/subcategory display-name
  // table (verified in the live cache: 212 entries, keys in the cat/subcat id space, values
  // like "Skilling Boss<br>    Croesus" and "    Nakatra" with markup expressing hierarchy).
  // A task's boss group resolves as: its parent achievement's name; else enum 16086 for its
  // subcategory (markup stripped, last hierarchy segment taken); else the common-prefix guess;
  // else "Category #<id>" so two unnamed groups can never blur into one "Other". Groups that
  // end up with the SAME name merge, so a parented "Nex" and a subcategory also named "Nex"
  // are one dropdown entry instead of two.
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
    // "Skilling Boss<br>    Croesus" -> "Croesus"; strip colour tags; trim indent.
    v = String(v).replace(/<col=[^>]*>|<\/col>/gi, '');
    const segs = v.split(/<br\s*\/?>/i).map(x => x.trim()).filter(Boolean);
    return segs.length ? segs[segs.length - 1] : v.trim();
  }
  function cmBosses() {
    const lb = achLeafParent();
    const orphans = {};
    const byName = {};
    const addTo = (nm, a) => {
      const e = byName[nm] = byName[nm] || { name: nm, n: 0, ids: [] };
      e.n++; e.ids.push(a.id);
    };
    for (const a of (achDefs || [])) {
      if (!(achTier(a) && achTrackable(a) && !achIsLeagues(a))) continue;
      const p = lb[a.id];
      if (p) { addTo(p.name || ('#' + p.id), a); continue; }
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
    // Synthetic ids sit far above real achievement ids so they can never collide with one.
    let synth = 900000000;
    const m = {};
    for (const nm of Object.keys(byName).sort()) m[synth++] = byName[nm];
    return m;
  }
  function cmPopulateBoss() {
    const dd = $('cmBoss'); if (!dd || !dd.setItems) return;
    const m = cmBosses(); const ids = Object.keys(m).map(Number).sort((a, b) => m[a].name.localeCompare(m[b].name));
    // Signature on the NAMES, not the count: the enum 16086 load renames groups without
    // changing how many there are, and a count-only guard kept the old labels.
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
    const started = (a) => { const ls = st.prog[a.id] || []; for (const l of ls) if (l.cur > 0) return true; return false; };
    const inTier = (a) => { if (cmFTier < 0) return true; const t = achTier(a); return t && t[1] === cmFTier; };
    const lb = cmLeafBoss();
    // Real boss parents match by parent id; the synthetic orphan groups (see cmBosses)
    // carry their member ids instead, since those tasks have no parent to match on.
    const bosses = cmBosses();
    // Every group carries its member ids now (parented and subcategory groups alike, so
    // same-named ones could merge); the old parent-id match branch is gone with it.
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

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { achBitFromVbVal, achBitReqs, achDefById, achIsLeagues, achVarpReqs, fetchAchievements, mkDropdown, pluginAchievements });
registerTab({ id: 'achievements', render: renderAchievements2, open: function () { achListSig = ''; fetchAchievements(true); } });
registerTab({ id: 'combatmastery', render: renderCombatMastery, open: function () { cmListSig = ''; fetchAchievements(true); } });
})();
