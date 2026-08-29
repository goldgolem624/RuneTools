// RuneToolsX panel: Archaeology mysteries (requirements + focused mystery) + metal bank UI.
// Spliced inline into client.html; IIFE (window exports + registerTab; see the RTX registry in client.html).
(function () {

  // Requirement live-checks beyond skills/mysteries: solved varbits + the Qualification achievement
  // chains, evaluated recursively from the cache achievement defs.
  const MYST_REQ_STATIC_VBS = [
    52651,                                            // Secrets of Amberfell stage (5 started, 170 complete)
    61089,                                            // Moonrise Dig Site access (>= 2; below that, talk to the NPC at 3749,1651)
    51347,                                            // Twilight of the Gods stage (5 started, 195 complete)
    55603,                                            // Thought Process special research done
    13322,                                            // Land of the Goblins stage (1 started, 18 complete)
    46468,                                            // Archaeology tutorial stage (>=1 complete)
    46004,                                            // wingsuit tier owned (1 v1, 2 v2, 3 v3; item 50120 via script2518)
    47191, 47192, 47193, 47194, 47195, 47196, 47197, 47198,   // Kharid-et shadow anchors powered (loc morphs 117067-74)
    47015, 47016, 47017, 47018, 47019,                // master archaeologist's outfit pieces (guild shop)
    48158];                                           // Orthen site introduction (cs2 script14595 per-site map; talked to Mr Mordaut)
  const MYST_REQ_ACH_NAMES = ['qualification - intern', 'qualification - assistant',
    'qualification - associate', 'qualification - professor', 'qualification - guildmaster'];
  let mystReqVb = null;        // varbit id -> live value (prefetched per mysteries poll)
  let mystReqVp = null;        // extra varp id -> value (achievement bit reqs)
  let _achByName = null, _achById = null, _mystReqAt = 0;
  function mystAchMaps() {
    if (!achDefs) return null;
    if (!_achById) {
      _achById = new Map(); _achByName = new Map();
      for (const a of achDefs) { _achById.set(a.id, a); if (a.name) _achByName.set(a.name.toLowerCase(), a); }
    }
    return _achById;
  }
  // every varbit/varp the achievement's full sub-tree reads
  function achChainNeeds(id, vbs, vps, seen) {
    const byId = mystAchMaps(); if (!byId || seen.has(id)) return;
    seen.add(id);
    const a = byId.get(id); if (!a) return;
    for (const q of (a.reqs || [])) for (const v of (q.varbits || [])) vbs.add(v);
    const br = achBitReqs(a);
    for (const b of br.vbits) vbs.add(b.vb);            // op 25: varbit bits
    for (const b of br.vpbits) vps.add(b.vp);           // op 23: varp bits
    for (const q of achVarpReqs(a)) for (const id of q.vps) vps.add(id);
    for (const s of (a.subach || [])) achChainNeeds(s, vbs, vps, seen);
  }
  // recursive completion: satisfied subrequirements (varbit reqs + skill reqs + varp-bit reqs +
  // sub-achievements) >= needN (op 30), default all
  function achEvalDone(id, memo) {
    memo = memo || new Map();
    if (memo.has(id)) return memo.get(id);
    memo.set(id, false);                              // cycle guard
    const byId = mystAchMaps(); if (!byId) return false;
    const a = byId.get(id); if (!a) return false;
    const lvl = questSkillLevels();
    let total = 0, ok = 0;
    for (const q of (a.reqs || [])) { total++; if (q.varbits && q.varbits.length && ((mystReqVb && mystReqVb[q.varbits[0]]) || 0) >= q.value) ok++; }
    for (const s of (a.skills || [])) { total++; if (lvl(s.s) >= s.l) ok++; }
    const br = achBitReqs(a);
    for (const b of br.vbits) {           // op 25: bit BIT of a VARBIT's value
      total++;
      if (achBitFromVbVal(b.vb, (mystReqVb && mystReqVb[b.vb]) || 0, b.bit)) ok++;
    }
    for (const b of br.vpbits) {          // op 23: bit BIT of a VARP
      total++;
      const v = (mystReqVp && mystReqVp[b.vp]) || (mystVp && mystVp[b.vp]) || 0;
      if ((v >>> b.bit) & 1) ok++;
    }
    for (const q of achVarpReqs(a)) {                  // op 13: SUM of live VARPs >= value
      total++;
      let v = 0;
      for (const id of q.vps) v += (mystReqVp && mystReqVp[id]) || (mystVp && mystVp[id]) || 0;
      if (v >= q.v) ok++;
    }
    for (const sid of (a.subach || [])) { total++; if (achEvalDone(sid, memo)) ok++; }
    const need = (a.needN && a.needN.length) ? a.needN[0] : total;
    const done = total > 0 && ok >= need;
    memo.set(id, done);
    return done;
  }
  // Per-mystery PROGRESS-STAGE varbits (CS2 script14584 = the journal's own status fn; dbrow table
  // 92 ids joined to names via the archive-41 rows). [vb, solvedAt] -- the stage clamps at solvedAt
  // for display; {bits, n} = bitmask form (Contract Claws: BITCOUNT(vb 47091) of 12 gargoyles,
  // vb 47151 flips when Ophiuchus frees). Most of these varbits are LOC/NPC MORPHS. Page-only
  // mysteries are absent on purpose: their progress IS the page count (script14587).
  // {vp, bit} = research-started flag: Secrets of the Monolith is research-driven
  // (script14630/14629 over the research bit-banks; started varps 9297/9298/11740, completed
  // 9299/9300/11741, bit = row field-368640 index; research 2922 "Mysterious Monolith: Existence"
  // = index 38 -> started varp 9298 bit 6, completed varp 9300 bit 6).
  const MYST_STAGE = {
    'Secrets of the Monolith': { vp: 9298, bit: 6 },
    'Writings on the Walls': [46695, 2],
    'Breaking the Seal': [47184, 6],
    'Prison Break': [47181, 5],
    'Time Served': [47185, 2],
    'The Forgotten Prisoner': [47183, 3],
    'The Vault of Shadows': [47175, 3],
    'Fallen Angels': [47035, 3],
    'Hallowed Be...': [47036, 3],
    'The Everlight': [47037, 6],
    'Eyes in Their Stars': [47082, 6],
    'Embrace the Chaos': [47083, 3],
    'Contract Claws': { bits: 47091, n: 12 },
    'Dagon Bye': [47104, 7],
    'Leap of Faith': [47247, 5],
    'Wing Out': [47248, 5],
    "Howl's Floating Workshop": [47252, 6],
    'Out of the Crucible': [47271, 7],
    'Into the Forge': [47272, 3],
    'You Have Chosen...': [47284, 7],
    'Teleport Node On': [48161, 10],
    'Know Thy Measure': [48162, 12],
    'Fragmented Memories': [48163, 9],
    'Mysterious City': [48164, 6],
    'Empty Children': [49892, 5],
    'Secrets of the Inquisition': [49893, 5],
    'A New Age': [55622, 7],
    'Path of the Initiate': [61071, 4],
    'Inside You There Are Two Wolves': [61075, 4],
    'The Final Revolution': [61088, 4],
  };
  function mystStageVb(name) {   // varbit id to prefetch (null for the varp-bit form)
    const s = MYST_STAGE[name];
    if (!s || s.vp !== undefined) return null;
    return s.bits !== undefined ? s.bits : s[0];
  }
  function mystStageSrc(name) {  // human-readable source for tooltips
    const s = MYST_STAGE[name];
    if (!s) return '';
    if (s.vp !== undefined) return 'varp ' + s.vp + ' bit ' + s.bit;
    if (s.bits !== undefined) return 'varbit ' + s.bits + ' (bitmask)';
    return 'varbit ' + s[0];
  }
  // -> [cur, max] or null; max 1 = binary research flag. Reads the panel's prefetched maps unless
  // explicit vb/vp maps are passed (the plugin SDK's state.mysteries does its own reads).
  function mystStageVal(name, vbMap, vpMap) {
    const s = MYST_STAGE[name];
    if (!s) return null;
    const vpm = vpMap || mystReqVp, vbm = vbMap || mystReqVb;
    if (s.vp !== undefined) {
      if (!vpm) return null;
      return [((vpm[s.vp] || 0) >>> s.bit) & 1, 1];
    }
    if (!vbm) return null;
    if (s.bits !== undefined) {
      let n = 0, v = (vbm[s.bits] | 0) >>> 0;
      while (v) { n += v & 1; v >>>= 1; }
      return [n, s.n];
    }
    const v = vbm[s[0]] | 0;
    return [Math.min(v, s[1]), s[1]];
  }
  // Same-varp varbit CANDIDATES for the focused mystery. The cache has NO validated per-step
  // varbit -> mystery link: step state (e.g. Path of the Initiate's offering bowls) exists ONLY as
  // loc-config morph varbits, vb 61072 is referenced by ZERO scripts, the stage varbit's only
  // reference is the journal dispatcher script14584, and no struct/dbrow field ties steps to a
  // mystery. Content state is allocated in per-varp blocks, so the stage varbit's varp siblings
  // are strong CANDIDATES -- but one varp can hold several mysteries' state (varp 12883 = the
  // Initiate bowls AND the lunar slabs), so these render explicitly unvalidated, with live values.
  let _mystSibCache = null;   // { name, val: {varp, sibs:[[vb,lsb,msb],..]} }
  function mystStageSiblings(name) {
    const vb = mystStageVb(name);
    if (vb === null || !storageVbMap) return null;
    if (_mystSibCache && _mystSibCache.name === name) return _mystSibCache.val;
    const def = storageVbMap[vb];
    if (!def) return null;
    const sibs = [];
    for (const id in storageVbMap) {
      const d = storageVbMap[id];
      if (d && d.varp === def.varp) sibs.push([+id, d.lsb, d.msb]);
    }
    sibs.sort((a, b) => a[1] - b[1] || a[0] - b[0]);
    const val = { varp: def.varp, sibs };
    _mystSibCache = { name: name, val: val };
    return val;
  }
  async function mystReqPrefetch() {
    const now = Date.now();
    if (now - _mystReqAt < 1500) return;
    _mystReqAt = now;
    if (!achDefs && bridge().achievements) {
      try { achDefs = JSON.parse(await rtxData.raw('cache.achievements')) || []; } catch (e) { achDefs = []; }
      _achById = null;
    }
    // achBitReqs classifies legacy merged bit reqs via storageVbMap - load it up front so the first
    // pass already routes varbit vs varp bits correctly.
    await ensureVbMap();
    const vbs = new Set(MYST_REQ_STATIC_VBS), vps = new Set();
    if (mystAchMaps()) for (const nm of MYST_REQ_ACH_NAMES) {
      const a = _achByName.get(nm);
      if (a) achChainNeeds(a.id, vbs, vps, new Set());
    }
    for (const nm in ARCH_MYST_INFO)   // varbit-tracked auto steps ({vb,v} specs) read live too
      for (const [, spec] of (ARCH_MYST_INFO[nm].auto || [])) if (spec && spec.vb) vbs.add(spec.vb);
    for (const nm in MYST_STAGE) {   // per-mystery stage varbits (or research varps)
      const s = MYST_STAGE[nm], vb = mystStageVb(nm);
      if (vb !== null) vbs.add(vb); else vps.add(s.vp);
    }
    // Focused mystery: read its stage varp's sibling varbits live (candidates section), and load the
    // CS2 rename table for their morph labels (shared with the Vars panel).
    if (typeof varNamesLoad === 'function') varNamesLoad();
    const fnm = mystFocusName();
    if (fnm) {
      const sb = mystStageSiblings(fnm);
      if (sb) for (const s2 of sb.sibs) vbs.add(s2[0]);
    }
    try { mystReqVb = await readVarbitValues([...vbs]); } catch (e) {}
    if (vps.size) { try { mystReqVp = JSON.parse(await rtxData.raw('state.varps', [...vps].join(','))); } catch (e) {} }
  }
  function mystReqSigVal() {   // cheap change signal for render sigs
    let s = 0;
    if (mystReqVb) for (const k in mystReqVb) s += mystReqVb[k];
    if (mystReqVp) for (const k in mystReqVp) s += mystReqVp[k];
    return s;
  }
  // token -> live check (null = no definition, render neutral)
  function mystReqResolve(t) {
    if (!mystReqVb) return null;
    const vb = id => mystReqVb[id] || 0;
    const low = t.toLowerCase();
    if (low.indexOf('twilight of the gods') >= 0)
      return { ok: vb(51347) >= 195, tip: 'varbit 51347 = ' + vb(51347) + ' (complete at 195)' };
    if (low.indexOf('secrets of amberfell') >= 0)
      return { ok: vb(52651) >= 170, tip: 'varbit 52651 = ' + vb(52651) + ' (complete at 170)' };
    if (low.indexOf('moonrise dig site access') >= 0)
      return { ok: vb(61089) >= 2, tip: 'varbit 61089 = ' + vb(61089) + ' (>= 2 = access; below that, talk to the archaeologist at 3749,1651)' };
    if (low.indexOf('thought process') >= 0)
      return { ok: vb(55603) >= 1, tip: 'varbit 55603 = ' + vb(55603) };
    if (low.indexOf('land of the goblins') >= 0)   // "partial completion of ..." intentionally checks FULL completion
      return { ok: vb(13322) >= 18, tip: 'varbit 13322 = ' + vb(13322) + ' (complete at 18)' };
    if (low.indexOf('archaeology tutorial') >= 0)
      return { ok: vb(46468) >= 1, tip: 'varbit 46468 = ' + vb(46468) };
    if (low.indexOf('mordaut') >= 0)                  // Orthen site introduction (script14595)
      return { ok: vb(48158) >= 1, tip: 'varbit 48158 = ' + vb(48158) + ' (Orthen site introduction; 0 = not yet talked)' };
    const wm = low.match(/^wingsuit v(\d)/);
    if (wm) return { ok: vb(46004) >= +wm[1], tip: 'wingsuit tier owned: ' + vb(46004) };
    if (low.indexOf('shadow anchors powered') >= 0) {
      let n = 0;
      for (let id = 47191; id <= 47198; id++) if (vb(id)) n++;
      return { ok: n >= 8, tip: 'shadow anchors powered: ' + n + '/8' };
    }
    if (low.indexOf("master archaeologist's outfit") >= 0) {
      const pieces = [47015, 47016, 47017, 47018, 47019];
      const n = pieces.filter(id => vb(id) >= 1).length;
      return { ok: n >= pieces.length, tip: 'outfit pieces unlocked: ' + n + '/' + pieces.length };
    }
    const qm = low.match(/^qualification - ([a-z]+)/);
    if (qm && mystAchMaps()) {
      const a = _achByName.get('qualification - ' + qm[1]);
      if (a) return { ok: achEvalDone(a.id), tip: 'achievement ' + a.id + ' (evaluated from its sub-achievement chain)' };
    }
    return null;
  }
  function mystReqHtml(req) {
    const lvl = questSkillLevels();
    const toks = []; let cur = '', depth = 0;
    for (const ch of req) {                          // commas inside parens don't split a token
      if (ch === '(') depth++;
      else if (ch === ')') depth = depth > 0 ? depth - 1 : 0;
      if (ch === ',' && depth === 0) { toks.push(cur); cur = ''; } else cur += ch;
    }
    toks.push(cur);
    return toks.filter(t => t.trim()).map(t => {
      const tt = t.trim();
      const m = tt.match(/^(\d+)\s+([A-Za-z]+)(\s*\(.*\))?$/);
      if (m) {
        const si = SKILL_NAMES.findIndex(n => n.toLowerCase() === m[2].toLowerCase());
        if (si >= 0) {
          const have = lvl(si);
          if (!have) return mystEsc(tt);                       // no live skill data yet
          const okR = have >= +m[1];
          return '<span style="color:' + (okR ? '#5fd07a' : '#f06e6e') + ';" title="your level: ' + have + '">' + mystEsc(tt) + '</span>';
        }
      }
      const partial = /\(partial\)\s*$/i.test(tt);
      const base = tt.replace(/\s*\(partial\)\s*$/i, '').replace(/\s+completed$/i, '').trim();
      const mb = mystLookup(base);
      if (mb) {
        const okR = mystDone(mb[0], mb[1], mystVp);
        if (okR) return '<span style="color:#5fd07a;" title="mystery solved">' + mystEsc(tt) + '</span>';
        if (!partial) return '<span style="color:#f06e6e;" title="mystery not solved">' + mystEsc(tt) + '</span>';
      }
      const rr = mystReqResolve(tt);                  // solved varbits + qualification chains
      if (rr) return '<span style="color:' + (rr.ok ? '#5fd07a' : '#f06e6e') + ';" title="' + mystEsc(rr.tip || '') + '">' + mystEsc(tt) + '</span>';
      return mystEsc(tt);
    }).join(', ');
  }
  // Which account the in-memory blob was read for. mystSteps is per-character, but it was
  // only reset on entering the Mysteries tab, so switching account with the tab already open
  // (or while working in Focused Mystery) left the previous character's steps loaded -- and
  // the next save wrote them into the NEW character's file. Same guard the Notes panel uses.
  let mystLoadedPid = -1;
  function mystSaveSteps() {
    if (mystSteps === null || !bridge() || !bridge().mystSave) return;
    if (mystLoadedPid !== myPid()) return;   // never write one character's blob to another
    try { rtxData.sync('act.mystSave', JSON.stringify(mystSteps)); } catch (e) {}
  }
  // ---- focused mystery: pinned per account (stored in the same blob under "__focus"); gets its
  //      own tab with steps/tracking, and can mark the current step's NPC in-world ----
  let mfSig = '';
  function mystFocusName() { return (mystSteps && typeof mystSteps.__focus === 'string') ? mystSteps.__focus : ''; }
  function setMystFocus(nm) {
    if (mystSteps === null) mystSteps = {};
    if (nm) mystSteps.__focus = nm; else delete mystSteps.__focus;
    mystSaveSteps(); mystSig = ''; mfSig = '';
    updateMystHighlight();
  }
  // done/auto state per step. Auto specs: number = at least N pages found; array = those page
  // indices found; {vb, v} = live varbit >= v (for pageless mysteries). Anything else = a manually
  // saved checkmark.
  function mystStepStates(name, info) {
    const out = [];
    if (!info || !info.steps) return out;
    const autoBy = {};
    if (info.auto) for (const [si, spec] of info.auto) autoBy[si] = spec;
    const manual = (mystSteps && mystSteps[name]) || [];
    let pf = -1;
    for (let i = 0; i < info.steps.length; i++) {
      const spec = autoBy[i];
      let dn;
      if (spec !== undefined) {
        if (typeof spec === 'number') {
          if (pf < 0) { pf = 0; for (const [idx] of (info.pages || [])) if (mystPageFound(idx)) pf++; }
          dn = pf >= spec;
        } else if (Array.isArray(spec)) dn = spec.every(idx => mystPageFound(idx));
        else dn = !!mystReqVb && (mystReqVb[spec.vb] | 0) >= spec.v;
      } else dn = manual.indexOf(i) >= 0;
      // No explicit spec: a step that NAMES a special research tracks itself from the live research
      // bits, matched against the real names from DBTable 90. Only a step that actually names one is
      // taken over; everything else stays hand-tickable.
      let resAuto = false;
      if (spec === undefined) {
        const rd = (typeof archResearchDoneIn === 'function') ? archResearchDoneIn(info.steps[i]) : null;
        if (rd !== null) { dn = rd; resAuto = true; }
      }
      out.push({ done: dn, auto: spec !== undefined || resAuto });
    }
    return out;
  }
  // Intentional no-op: per-step NPC marking belongs in a plugin (rtx.plugin.overlay.highlight);
  // the stub is kept so callers stay simple.
  function updateMystHighlight() {}
  let mystAt = 0;
  async function fetchArchMysteries(force) {
    if (!bridge() || mystFetching) return;
    const now = Date.now();
    if (!force && now - mystAt < 750) return;   // also polled in the background while in-world marking is on
    mystFetching = true; mystAt = now;
    // keep the research bits fresh so steps naming a special research tick themselves
    try { if (typeof archResearchEnsure === 'function') await archResearchEnsure(); } catch (e) {}
    if ((mystSteps === null || mystLoadedPid !== myPid()) && bridge().mystLoad) {
      mystLoadedPid = myPid();
      try { const d = JSON.parse(await rtxData.raw('host.mystLoad')); mystSteps = (d && typeof d === 'object') ? d : {}; }
      catch (e) { mystSteps = {}; }
    }
    // 9302/9303 = mystery completion bits; 9205/9206/9207/9564/11732 = the global journal-page bank
    // (page found = bit idx%32 of varp [idx/32], CS2 script13039); 11733 = table-31 collectible bits
    // (CS2 script18966)
    try { mystVp = JSON.parse(await rtxData.raw('state.varps', '9302,9303,9205,9206,9207,9564,11732,11733')); } catch (e) { /* keep previous */ }
    try { await mystReqPrefetch(); } catch (e) {}     // requirement-line live values
    mystFetching = false;
    updateMystHighlight();
    paneRun('archmysteries', renderArchMysteries);
    paneRun('mystfocus', renderMystFocus);
  }
  function renderArchMysteries() {
    const c = $('content');
    let wrap = $('mystWrap');
    if (!wrap) {
      c.innerHTML = ''; wrap = document.createElement('div'); wrap.id = 'mystWrap'; wrap.className = 'pk-wrap'; c.appendChild(wrap); mystSig = '';
      // one delegated handler: step checkbox toggle, uncheck-all, row expand/collapse
      wrap.addEventListener('click', e => {
        const st = e.target.closest('.myst-step');
        if (st) {
          if (st.dataset.auto) return;               // page-tracked steps are not manually toggleable
          if (mystSteps === null) mystSteps = {};
          const nm = st.dataset.m, idx = +st.dataset.i;
          const arr = (mystSteps[nm] = mystSteps[nm] || []);
          const p = arr.indexOf(idx);
          if (p >= 0) arr.splice(p, 1); else arr.push(idx);
          mystSaveSteps(); mystSig = ''; renderArchMysteries();
          return;
        }
        const pin = e.target.closest('.myst-pin');
        if (pin) {
          const nm = pin.dataset.m;
          if (mystFocusName() === nm) {
            setMystFocus('');
            mystSig = ''; renderArchMysteries();
          } else {
            setMystFocus(nm);
            const ft = TABS.find(x => x.id === 'mystfocus');
            if (ft) openTab(ft);
          }
          return;
        }
        const rs = e.target.closest('.myst-reset');
        if (rs) {
          if (mystSteps) { delete mystSteps[rs.dataset.m]; mystSaveSteps(); }
          mystSig = ''; renderArchMysteries();
          return;
        }
        const h = e.target.closest('.myst-h');
        if (h) {
          const nm = h.dataset.m;
          if (mystOpen.has(nm)) mystOpen.delete(nm); else mystOpen.add(nm);
          mystSig = ''; renderArchMysteries();
        }
      });
    }
    let totDone = 0, totAll = 0, ptsDone = 0, ptsAll = 0;
    for (const g of ARCH_MYSTERIES) for (const [, varp, bit, pts] of g.m) {
      totAll++; ptsAll += pts;
      if (mystDone(varp, bit, mystVp)) { totDone++; ptsDone += pts; }
    }
    const sig = totDone + '/' + totAll + '|' + ptsDone + '|' + [...mystOpen].join(',') + '|' + JSON.stringify(mystSteps || 0) +
                '|' + MYST_PAGE_VARPS.concat(MYST_C31_VARP).map(v => (mystVp && mystVp[v]) || 0).join(',') +
                '|' + (mystCachePages ? 1 : 0) +
                '|' + ((lastSnap && lastSnap.skills) ? lastSnap.skills.reduce((a, s) => a + (s ? s[0] : 0), 0) : 0) +
                '|' + mystFocusName() + '|' + mystReqSigVal()
                + '|' + ((typeof archResSigVal === 'function') ? archResSigVal() : '');
    if (sig === mystSig) return; mystSig = sig;
    let html = '<div class="arch-head"><span>Archaeology mysteries</span>' +
               '<span class="arch-tot">' + totDone + ' / ' + totAll + ' · ' + ptsDone + '/' + ptsAll + ' pts</span></div>';
    for (const g of ARCH_MYSTERIES) {
      const done = g.m.filter(([, varp, bit]) => mystDone(varp, bit, mystVp)).length;
      html += '<div class="arch-grp"><div class="arch-grp-h"><span>' + g.site + '</span>' +
              '<span class="arch-grp-c' + (done === g.m.length ? ' full' : '') + '">' + done + ' / ' + g.m.length + '</span></div><div class="arch-rows">';
      for (const [name, varp, bit, pts] of g.m) {
        const ok = mystDone(varp, bit, mystVp);
        const info = ARCH_MYST_INFO[name];
        const open = mystOpen.has(name);
        const pc = ok ? null : mystPageCount(info, name);   // live page progress badge while unsolved
        const st = ok ? null : mystStageVal(name);          // live stage badge (script14584 varbits)
        const rowHtml = '<div class="arch-row myst-h' + (ok ? ' is-done' : '') + '" data-m="' + mystEsc(name) + '">' +
                '<div class="arch-nx"><span class="arch-n">' +
                (mystFocusName() === name ? '<span style="color:var(--accent-hi);" title="Focused mystery">&#9673; </span>' : '') +
                mystEsc(name) + '</span>' +
                '<span class="arch-d">' + (info && info.tip ? mystEsc(info.tip) : pts + ' points') + '</span></div>' + mystBadgeHtml(ok, pc, st, name) + '</div>';
        if (!open) { html += rowHtml; continue; }
        // expanded = ONE card: the row becomes the card header, generic info below (steps + page
        // tracking live on the Focused Mystery tab)
        const detHtml = info
          ? (mystMetaHtml(info, pts, false, name) +
             '<span class="myst-pin" data-m="' + mystEsc(name) + '">' +
             (mystFocusName() === name ? 'Unpin focused mystery' : 'Pin as focused mystery') + '</span>')
          : '<div class="myst-tip">No guide data for this mystery.</div>';
        html += '<div class="myst-card' + (ok ? ' is-done' : '') + '">' + rowHtml +
                '<div class="myst-det">' + detHtml + '</div></div>';
      }
      html += '</div></div>';
    }
    wrap.innerHTML = html;
  }
  function mystBadgeHtml(ok, pc, st, name) {
    if (ok) return '<span class="arch-b ok">Solved</span>';
    if (pc) {
      // Page-collection mysteries: all pages found = the journal's state 3, "I should talk to the site
      // manager" (script14585/14584). Only for mysteries WITHOUT a stage varbit -- the stage-tracked
      // ones have their own remaining flow after the pages and never return that state.
      if (pc[0] === pc[1] && !MYST_STAGE[name])
        return '<span class="arch-b ok" title="All pages found (journal state 3, script14585)">Talk to the site manager</span>';
      return '<span class="arch-b' + (pc[0] === pc[1] ? ' ok' : '') + '">' + pc[0] + '/' + pc[1] + ' pages</span>';
    }
    if (st && st[0] > 0)
      return '<span class="arch-b" title="' + mystStageSrc(name) + ' (script14584)">' +
             (st[1] === 1 ? 'Research started' : 'stage ' + st[0] + '/' + st[1]) + '</span>';
    return '<span class="arch-b">Unsolved</span>';
  }
  // Focused-tab candidates list: every varbit sharing the stage varbit's varp, with the CS2 morph
  // labels and live values. Explicitly unvalidated (see mystStageSiblings).
  function mystSiblingsHtml(name) {
    const sb = mystStageSiblings(name);
    if (!sb || sb.sibs.length < 2) return '';
    const stageVb = mystStageVb(name);
    let h = '<div class="myst-meta" style="margin-top:8px;"><div><b>Varp ' + sb.varp + ' state (candidates):</b> ' +
            '<span style="color:var(--text-mute);">same-varp allocation, unvalidated - can include other content\'s varbits</span></div>';
    for (const s of sb.sibs) {
      const id = s[0], lsb = s[1], msb = s[2];
      const nm2 = (typeof varbitName === 'function' && varbitName(id)) || '';
      const v = mystReqVb ? (mystReqVb[id] | 0) : 0;
      h += '<div style="display:flex;gap:8px;font-size:11px;font-variant-numeric:tabular-nums;' +
           (id === stageVb ? 'color:var(--accent-hi);' : '') + '">' +
           '<span style="min-width:66px;">vb ' + id + '</span>' +
           '<span style="min-width:50px;color:var(--text-mute);">[' + lsb + (msb !== lsb ? '-' + msb : '') + ']</span>' +
           '<span style="flex:1;overflow:hidden;text-overflow:ellipsis;color:var(--text-dim);">' +
           mystEsc(nm2) + (id === stageVb ? ' (stage varbit)' : '') + '</span>' +
           '<span>' + v + '</span></div>';
    }
    return h + '</div>';
  }
  function mystMetaHtml(info, pts, withTip, name) {
    let h = '<div class="myst-meta">';
    if (withTip && info.tip) h += '<div>' + mystEsc(info.tip) + '</div>';
    // Live progress stage (script14584 varbit), with the varbit id spelled out.
    const st = name ? mystStageVal(name) : null;
    if (st) h += '<div><b>Stage:</b> ' +
                 (st[1] === 1 ? (st[0] ? 'Research started' : 'Research not started') : st[0] + ' / ' + st[1]) +
                 ' <span style="color:var(--text-mute);">(' + mystStageSrc(name) + ')</span></div>';
    if (info.loc) h += '<div><b>Location:</b> ' + mystEsc(info.loc) + '</div>';
    if (info.req) h += '<div><b>Requirements:</b> ' + mystReqHtml(info.req) + '</div>';
    if (info.items && info.items.length) h += '<div><b>Required items:</b> ' + mystEsc(info.items.join(', ')) + '</div>';
    if (info.rewards && info.rewards.length) h += '<div><b>Rewards:</b> ' + mystEsc(mystFmtNums(info.rewards.join(', '))) + '</div>';
    h += '<div><b>Points:</b> ' + pts + '</div></div>';
    return h;
  }
  function mystPagesHtml(info, ok, name) {
    const lists = mystPagesFor(name, info);
    if (!lists || (!lists.pg.length && !lists.c31.length)) return '';
    let got = 0, chips = '', i = 0;
    const chip = (f, label) => {
      if (f) got++;
      chips += '<span class="arch-b' + (f ? ' ok' : '') + '" title="' + mystEsc(label) +
               (f ? ' (found)' : ' (missing)') + '">' + (++i) + '</span>';
    };
    for (const e of lists.pg)  chip(ok || mystPageFound(e[0]), e[1]);
    for (const e of lists.c31) chip(ok || mystC31Found(e[0]), e[1]);
    const tot = lists.pg.length + lists.c31.length;
    return '<div style="display:flex;flex-wrap:wrap;align-items:center;gap:4px;padding-bottom:7px;font-size:11px;color:var(--text-dim);">' +
           '<span style="white-space:nowrap;"><b style="color:var(--text);font-weight:600;">Pages:</b> ' +
           (ok ? tot : got) + '/' + tot + '</span>' + chips + '</div>';
  }
  function mystStepsHtml(name, info, ok) {
    if (!info.steps || !info.steps.length) return '';
    const sts = mystStepStates(name, info);
    let cur = -1;
    if (!ok) for (let i = 0; i < sts.length; i++) if (!sts[i].done) { cur = i; break; }
    let h = '<div class="myst-steps">';
    info.steps.forEach((s, i) => {
      const dn = ok || sts[i].done;
      h += '<div class="myst-step' + (dn ? ' done' : '') + (sts[i].auto ? ' auto' : '') + (i === cur ? ' cur' : '') +
           '" data-m="' + mystEsc(name) + '" data-i="' + i + '"' +
           (sts[i].auto ? ' data-auto="1" title="Tracked automatically from your live progress"' : '') + '>' +
           '<span class="myst-cb"></span><span class="tx">' + mystEsc(s) + '</span></div>';
    });
    h += '</div>';
    const manual = (mystSteps && mystSteps[name]) || [];
    if (!ok && manual.length) h += '<span class="myst-reset" data-m="' + mystEsc(name) + '">Uncheck all</span>';
    return h;
  }
  function renderMystFocus() {
    const c = $('content');
    let wrap = $('mfWrap');
    if (!wrap) {
      c.innerHTML = ''; wrap = document.createElement('div'); wrap.id = 'mfWrap'; wrap.className = 'pk-wrap'; c.appendChild(wrap); mfSig = '';
      const live = document.createElement('div'); live.id = 'mfLive'; wrap.appendChild(live);
      wrap.addEventListener('click', e => {
        if (e.target.closest('#mfUnpin')) {
          setMystFocus('');
          const ot = TABS.find(x => x.id === 'archmysteries');
          if (ot) openTab(ot);
          return;
        }
        const st = e.target.closest('.myst-step');
        if (st) {
          if (st.dataset.auto) return;                // page-tracked steps are not manually toggleable
          if (st.dataset.m === undefined) return;     // not a tracked step row
          if (mystSteps === null) mystSteps = {};
          const nm = st.dataset.m, idx = +st.dataset.i;
          const arr = (mystSteps[nm] = mystSteps[nm] || []);
          const p = arr.indexOf(idx);
          if (p >= 0) arr.splice(p, 1); else arr.push(idx);
          mystSaveSteps(); updateMystHighlight(); mfSig = ''; renderMystFocus();
          return;
        }
        const rs = e.target.closest('.myst-reset');
        if (rs) {
          if (mystSteps) { delete mystSteps[rs.dataset.m]; mystSaveSteps(); updateMystHighlight(); }
          mfSig = ''; renderMystFocus();
        }
      });
    }
    const nm = mystFocusName();
    const live = $('mfLive');
    if (!nm) { live.innerHTML = '<div class="stor-empty">No focused mystery. Pin one from the Mysteries tab.</div>'; mfSig = ''; return; }
    const info = ARCH_MYST_INFO[nm];
    const mb = mystLookup(nm);
    let ok = mb ? mystDone(mb[0], mb[1], mystVp) : false;
    const pts = mb ? mb[2] : 0;
    const pc = ok ? null : mystPageCount(info, nm);
    const st = ok ? null : mystStageVal(nm);
    const sig = JSON.stringify([nm, ok, pc, st, mystReqSigVal(), mystCachePages ? 1 : 0,
      MYST_PAGE_VARPS.concat(MYST_C31_VARP).map(v => (mystVp && mystVp[v]) || 0),
      (mystSteps && mystSteps[nm]) || [],
      (lastSnap && lastSnap.skills) ? lastSnap.skills.reduce((a, s) => a + (s ? s[0] : 0), 0) : 0,
      (typeof archResSigVal === 'function') ? archResSigVal() : '']);
    if (sig === mfSig) return; mfSig = sig;
    let html = '<div class="arch-head"><span>' + mystEsc(nm) + '</span><span style="display:flex;align-items:center;gap:10px;">' +
               mystBadgeHtml(ok, pc, st, nm) + '<span class="myst-reset" id="mfUnpin" style="padding:0;">Unpin</span></span></div>';
    html += '<div class="myst-det">' +
            (info ? (mystMetaHtml(info, pts, true, nm) + mystPagesHtml(info, ok, nm) + mystSiblingsHtml(nm) + mystStepsHtml(nm, info, ok))
                  : '<div class="myst-tip">No guide data for this mystery.</div>' + mystSiblingsHtml(nm)) + '</div>';
    live.innerHTML = html;
  }
  // Bar item ids from enum 15093 (idx 18-33, 44-45, 47, 49). Everything else = ore.
  const MBANK_BARS = new Set([2349,2351,2353,2355,2359,2361,2357,2363,44838,44840,44842,44844,
                              45984,45986,45988,45991,57435,57444,59210,60296]);
  async function fetchMetalBank() {
    if (!bridge() || mbankFetching) return;
    mbankFetching = true;
    try {
      const d = JSON.parse(await rtxData.raw('state.metalBank'));
      mbankData = d && Array.isArray(d.items) ? d : { open: false, items: [], count: 0, cached_at: 0 };
    } catch (e) { /* keep previous */ }
    mbankFetching = false;
    paneRun('metalbank', renderMetalBank);
  }

  function renderMetalBank() {
    const c = $('content');
    let wrap = $('mbankWrap');
    if (!wrap) {
      c.innerHTML = '';
      wrap = document.createElement('div'); wrap.id = 'mbankWrap'; wrap.className = 'bank-wrap';
      const top = document.createElement('div'); top.className = 'bank-top';
      const inp = document.createElement('input'); inp.id = 'mbankSearch'; inp.className = 'bank-search';
      inp.type = 'text'; inp.placeholder = 'Search ore or bar by name or id...'; inp.value = mbankTerm; inp.spellcheck = false;
      inp.addEventListener('input', () => { mbankTerm = inp.value; mbankSig = ''; paintMetalBank(); });
      const meta = document.createElement('div'); meta.id = 'mbankMeta'; meta.className = 'bank-meta';
      top.appendChild(inp); top.appendChild(meta);
      const body = document.createElement('div'); body.id = 'mbankBody'; body.className = 'mbank-body';
      wrap.appendChild(top); wrap.appendChild(body); c.appendChild(wrap); mbankSig = '';
    }
    paintMetalBank();
  }

  function paintMetalBank() {
    const body = $('mbankBody'); if (!body) return;
    const items = (mbankData && mbankData.items) ? mbankData.items : [];
    const t = mbankTerm.trim().toLowerCase();
    const shown = !t ? items : items.filter(it => String(it[1]).indexOf(t) !== -1 || (it[3] || '').toLowerCase().indexOf(t) !== -1);
    const sig = t + '|' + (mbankData ? mbankData.cached_at + '|' + mbankData.open + '|' + items.length : 'x');
    if (sig === mbankSig) return; mbankSig = sig;
    const meta = $('mbankMeta');
    if (meta) {
      const liveTxt = (mbankData && mbankData.open) ? '<span class="live">live</span>'
        : (mbankData && mbankData.cached_at ? 'cached ' + new Date(mbankData.cached_at * 1000).toLocaleString() : '');
      const total = (mbankData && mbankData.count) ? mbankData.count : 0;
      meta.innerHTML = (total ? total + ' stacks' : '') + (liveTxt ? '<br>' + liveTxt : '');
    }
    body.innerHTML = '';
    if (!mbankData) { body.innerHTML = '<div class="bank-empty" style="display:flex"><div class="warn" style="background:rgba(255,255,255,0.04);border-color:var(--border);color:var(--text-dim)">Reading...</div></div>'; return; }
    if (!items.length) {
      body.innerHTML = '<div class="bank-empty" style="display:flex"><div class="warn">' +
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M12 9v4M12 17h.01M10.3 3.9 1.8 18a2 2 0 0 0 1.7 3h17a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0z"/></svg>' +
        '<div><b>No metal bank data cached yet.</b><br>Open the metal bank in game (tap a furnace, forge or anvil) to populate this window.</div></div></div>';
      return;
    }
    if (!shown.length) { body.innerHTML = '<div class="bank-empty" style="display:flex"><div class="warn" style="background:rgba(255,255,255,0.04);border-color:var(--border);color:var(--text-dim)">No ore or bar matches "' + mbankTerm.replace(/[<>&"]/g, '') + '".</div></div>'; return; }
    const ores = shown.filter(it => !MBANK_BARS.has(it[1]));
    const bars = shown.filter(it => MBANK_BARS.has(it[1]));
    const section = (title, list) => {
      if (!list.length) return;
      const grp = document.createElement('div'); grp.className = 'fm-grp'; grp.textContent = title + ' · ' + list.length;
      body.appendChild(grp);
      const grid = document.createElement('div'); grid.className = 'stor-grid';
      for (const it of list) {
        const [slot, id, stack, name] = it;
        grid.appendChild(bankCell(id, stack, name, '\nMetal bank (container 858 · INV_TOTAL)'));
      }
      body.appendChild(grid);
    };
    section('Ores', ores);
    section('Bars', bars);
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { MYST_STAGE, fetchArchMysteries, fetchMetalBank, mystFocusName, mystSaveSteps, mystStageVal, mystStageVb });
registerTab({ id: 'metalbank', render: renderMetalBank, open: function () { mbankFetchKey = ''; fetchMetalBank(); } });
registerTab({ id: 'archmysteries', render: renderArchMysteries, open: function () { mystSig = ''; mystSteps = null; fetchArchMysteries(true); } });
registerTab({ id: 'mystfocus', render: renderMystFocus, open: function () { mfSig = ''; fetchArchMysteries(true); } });
})();
