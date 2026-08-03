// RuneToolsX panel: Quests (full progress tracker with requirements + guides).
// Spliced inline into client.html at load; shares its global scope (no IIFE).

  // Definitions decode from the LIVE cache (bridge.quests() -> configs archive 35 QuestType; varbit
  // trackers pre-resolved to [varp,lsb,msb]). Status: < start = not started, < end = in progress, else complete.
  let QUESTS = null;
  let QUEST_BY_ID = null;  // id -> def
  let QUEST_VARPS = null;  // every varp the tracker reads (progress + specials + QP)
  let QUEST_VB = {};       // special-case varbit id -> [varp,lsb,msb] (payload "vb")
  // EXTRA per-quest requirements the cache configs don't encode, judged as a varbit slice >= min
  // over the same varp snapshot (b = [varp,lsb,msb]). Fort Forinthry building tiers per CS2
  // script7163: Workshop = vp10761[0:3], Chapel [4:7], Command Centre [8:11], Town Hall [12:15].
  const QUEST_XREQS = {
    490: [   // Murder on the Border
      { t: 'Build Town Hall (Tier 1)',      b: [10761, 12, 15], min: 1, done: 'Built', todo: 'Not built' },
      { t: 'Build Command Centre (Tier 1)', b: [10761, 8, 11],  min: 1, done: 'Built', todo: 'Not built' },
      { t: 'Build Workshop (Tier 1)',       b: [10761, 0, 3],   min: 1, done: 'Built', todo: 'Not built' },
      { t: 'Build Chapel (Tier 1)',         b: [10761, 4, 7],   min: 1, done: 'Built', todo: 'Not built' },
    ],
  };
  async function questEnsureDefs() {
    if (QUESTS) return true;
    if (!bridge() || !bridge().quests) return false;
    let j = null;
    try { j = JSON.parse(await bridge().quests()); } catch (e) { return false; }
    if (!j || !Array.isArray(j.quests) || !j.quests.length) return false;   // cache not open yet
    QUESTS = j.quests;
    QUEST_VB = j.vb || {};
    QUEST_BY_ID = new Map(QUESTS.map(q => [q.id, q]));
    const s = new Set([1297, 2615, 2339, 2695, 2675, 1295, 2793, 2426, 2427]);   // QP + special-case varps
    for (const k in QUEST_VB) s.add(QUEST_VB[k][0]);
    for (const k in QUEST_XREQS) QUEST_XREQS[k].forEach(x => s.add(x.b[0]));
    QUESTS.forEach(q => { if (q.v) s.add(q.v[0]); if (q.b) s.add(q.b[0]); });
    QUEST_VARPS = [...s];
    return true;
  }
  // The journal's own label enums: length (param 7855) via enum 13354, age (param 7831) via enum
  // 13275, start-location area (param 9393) via enum 9686. An empty enum just skips those labels.
  let QUEST_ENUMS = null;
  async function questEnsureEnums() {
    if (QUEST_ENUMS || !bridge() || !bridge().enumInfo) return;
    const grab = async id => { try { return JSON.parse(await bridge().enumInfo(id)) || {}; } catch (e) { return {}; } };
    const ln = await grab(13354), ag = await grab(13275), ar = await grab(9686);
    // enumInfo returns {} while the enum cache is still opening -> leave null and retry next poll
    if (Object.keys(ln).length || Object.keys(ag).length || Object.keys(ar).length)
      QUEST_ENUMS = { ln, ag, ar };
  }
  const QDIFF = ['Novice', 'Intermediate', 'Experienced', 'Master', 'Grandmaster', 'Special'];
  const QSTATUS = ['Not started', 'In progress', 'Completed'];
  function qBits(vp, varp, lsb, msb) {
    const w = msb - lsb + 1, m = w >= 31 ? 0x7FFFFFFF : ((1 << w) - 1);
    return (((vp[varp] || 0) >>> 0) >>> lsb) & m;
  }
  function qVbBits(vp, vbid) {
    const t = QUEST_VB[vbid];
    return t ? qBits(vp, t[0], t[1], t[2]) : 0;
  }
  const qBand = (v, a, b) => v < a ? 0 : (v < b ? 1 : 2);
  // -> 0 not started / 1 in progress / 2 complete / -1 no tracker in the configs.
  function questStatus(q, vp) {
    switch (q.id) {
      case 92:  { const v = vp[2615] || 0; if (v > 6) return 2; return (v === 0 && qVbBits(vp, 12894) === 0) ? 0 : 1; }
      case 140: return (vp[2339] || 0) >= 80 ? 2 : qBand(qVbBits(vp, 10848), 1, 65);
      case 111: return (vp[2695] || 0) >= 4 ? 2 : qBand(qVbBits(vp, 13366), 1, 8);
      case 275: { const v = vp[2675] || 0; if ((v >>> 20) & 1) return 2; return ((v >>> 1) & 1) ? 1 : 0; }
      case 131: return qBand(vp[1295] || 0, 1, 1000);
      case 70:  return qBand(vp[2793] || 0, 1, 15);
      case 270: { if ((vp[2426] || 0) === 10) return 2; return (((vp[2427] || 0) >>> 11) & 1) ? 1 : 0; }
      case 324: { if (qVbBits(vp, 12349) >= 110) return 2; return qBand(qVbBits(vp, 12334), 1, 35); }
    }
    if (q.v) return qBand(vp[q.v[0]] || 0, q.v[1], q.v[2]);
    if (q.b) return qBand(qBits(vp, q.b[0], q.b[1], q.b[2]), q.b[3], q.b[4]);
    return -1;
  }
  // live REAL skill levels (index = the quest statreq skill id; same order as SKILL_NAMES)
  function questSkillLevels() {
    const sk = (lastSnap && Array.isArray(lastSnap.skills)) ? lastSnap.skills : [];
    return i => (sk[i] ? sk[i][0] : 0);
  }
  function questReqs(q, vp, stOf) {
    const lvl = questSkillLevels();
    const out = { skills: [], quests: [], qp: null, missing: 0 };
    if (q.rqp) {
      out.qp = { need: q.rqp, have: vp[1297] || 0, ok: (vp[1297] || 0) >= q.rqp };
      if (!out.qp.ok) out.missing++;
    }
    (q.rs || []).forEach(t => {
      const have = lvl(t[0]), ok = have >= t[1];
      out.skills.push({ id: t[0], need: t[1], have, ok });
      if (!ok) out.missing++;
    });
    (q.rq || []).forEach(id => {
      const r = QUEST_BY_ID.get(id); if (!r) return;
      const st = stOf(id), ok = st === 2;
      out.quests.push({ id, name: r.n, st, ok });
      if (!ok) out.missing++;
    });
    out.extras = (QUEST_XREQS[q.id] || []).map(x => {
      const ok = qBits(vp, x.b[0], x.b[1], x.b[2]) >= x.min;
      if (!ok) out.missing++;
      return { name: x.t, ok, done: x.done, todo: x.todo };
    });
    return out;
  }
  // Full prerequisite closure: every transitive quest, the max level per skill (with the quest that demands it) and the max QP.
  function questClosure(q) {
    const quests = new Map(), skills = new Map();
    let qp = q.rqp ? { need: q.rqp, src: q.n } : null;
    const seen = new Set([q.id]);
    const visit = (id) => {
      if (seen.has(id)) return;
      seen.add(id);
      const c = QUEST_BY_ID.get(id); if (!c) return;
      quests.set(id, c);
      if (c.rqp && (!qp || c.rqp > qp.need)) qp = { need: c.rqp, src: c.n };
      (c.rs || []).forEach(t => {
        const cur = skills.get(t[0]);
        if (!cur || t[1] > cur.need) skills.set(t[0], { need: t[1], src: c.n });
      });
      (c.rq || []).forEach(visit);
    };
    (q.rs || []).forEach(t => skills.set(t[0], { need: t[1], src: q.n }));
    (q.rq || []).forEach(visit);
    return { quests: [...quests.values()], skills, qp };
  }
  let questsData = null, questFetching = false, questListSig = '', questDetailSig = '', questFetchAt = 0;
  let questFSearch = '', questFStatus = 0;   // 0 all / 1 done / 2 in progress / 3 not started / 4 ready
  let questView = null;    // null = list, else a quest config id (detail page)
  let questNav = [];
  async function fetchQuests(force) {
    if (!bridge() || questFetching) return;
    const _t = Date.now(); if (!force && _t - questFetchAt < 2000) return; questFetchAt = _t;
    questFetching = true;
    try {
      if (!await questEnsureDefs()) return;   // cache not open yet -> retry next poll
      await questEnsureEnums();
      let vp = {};
      if (bridge().varps) { try { vp = JSON.parse(await bridge().varps(myPid(), QUEST_VARPS.join(','))); } catch (e) {} }
      if (!vp || !Object.keys(vp).length) return;   // empty read -> keep last good
      const st = {};
      QUESTS.forEach(q => { st[q.id] = questStatus(q, vp); });
      // Re-render only on real change: the detail page rebuilds wholesale and would reset scroll each poll.
      const lvl = questSkillLevels();
      let sig = (vp[1297] || 0) + '|';
      QUESTS.forEach(q => { sig += st[q.id]; });
      for (let i = 0; i < 29; i++) sig += ',' + lvl(i);
      const changed = !questsData || questsData.sig !== sig;
      questsData = { vp, st, qp: vp[1297] || 0, sig };
      if (activeTab === 'quests' && (changed || (!$('questWrap') && !$('questDetailWrap')))) renderQuests();
      if (activeTab === 'questfocus' && (changed || !$('qfWrap'))) renderQuestFocus();
      if (changed) updateQuestHighlight();   // completion can clear the step NPC mark
    } finally { questFetching = false; }
  }
  // A not-started quest is "ready" when its FULL closure, plus the QUEST_XREQS extras, is met.
  function questReady(q, d) {
    const lvl = questSkillLevels();
    const c = questClosure(q);
    if (c.qp && d.qp < c.qp.need) return false;
    for (const [id, s] of c.skills) if (lvl(id) < s.need) return false;
    for (const r of c.quests) if (d.st[r.id] !== 2) return false;
    for (const x of QUEST_XREQS[q.id] || []) if (qBits(d.vp, x.b[0], x.b[1], x.b[2]) < x.min) return false;
    return true;
  }
  function questPill(st, ready) {
    if (st === 2) return ['q-done', 'Completed'];
    if (st === 1) return ['q-prog', 'In progress'];
    if (st === -1) return ['pet-na', 'Untracked'];
    return ready ? ['q-ready', 'Ready to start'] : ['q-not', 'Not started'];
  }
  function openQuestDetail(id, push) {
    if (push && questView !== null) questNav.push(questView);
    questView = id; questListSig = '';
    renderQuests();
  }
  function renderQuests() {
    if (questView !== null) { renderQuestDetail(); return; }
    const c = $('content');
    let wrap = $('questWrap');
    if (!wrap) {
      c.innerHTML = ''; questListSig = '';
      wrap = document.createElement('div'); wrap.id = 'questWrap'; wrap.className = 'pk-wrap'; c.appendChild(wrap);
      const tb = document.createElement('div'); tb.className = 'pet-toolbar';
      const chips = document.createElement('div'); chips.className = 'pet-chips';
      ['All', 'Completed', 'In progress', 'Not started', 'Ready'].forEach((nm, i) => {
        const b = document.createElement('button'); b.className = 'pet-chip' + (i === questFStatus ? ' on' : '');
        b.textContent = nm; b.dataset.val = i; chips.appendChild(b);
      });
      const search = document.createElement('input'); search.className = 'pet-search'; search.id = 'questSearch';
      search.placeholder = 'Search quest, id, or "guided"...'; search.value = questFSearch;
      tb.appendChild(chips); tb.appendChild(search); wrap.appendChild(tb);
      const cnt = document.createElement('div'); cnt.id = 'questCnt'; cnt.className = 'pet-count'; wrap.appendChild(cnt);
      const list = document.createElement('div'); list.id = 'questList'; list.className = 'pet-list'; wrap.appendChild(list);
      tb.addEventListener('click', e => {
        const b = e.target.closest('.pet-chip'); if (!b) return;
        questFStatus = +b.dataset.val;
        tb.querySelectorAll('.pet-chip').forEach(x => x.classList.toggle('on', x === b));
        questListSig = ''; renderQuestList();
      });
      search.addEventListener('input', () => { questFSearch = search.value.toLowerCase(); questListSig = ''; renderQuestList(); });
    }
    renderQuestList();
  }
  // True when this quest has full in-world VISUAL guidance (NPC/object/dialogue/item
  // highlighting) - the step-fn registry panel_visions.js declares. typeof-guarded so an
  // older build without that splice never throws here.
  function questHasVisualGuide(name) {
    try { return typeof QUEST_GUIDES === 'object' && !!QUEST_GUIDES[name]; } catch (e) { return false; }
  }
  function renderQuestList() {
    const list = $('questList'); if (!list) return;
    const d = questsData;
    if (!d) { list.innerHTML = '<div class="empty">Reading... (be in-world)</div>'; questListSig = ''; return; }
    const ready = {};   // computed lazily only for not-started quests (closure walk)
    const idSearch = /^\d+$/.test(questFSearch);   // digits = also match config ids by prefix
    // Typing "guided" (any 3+ char prefix) filters to the visually-guided quests.
    const guidedSearch = questFSearch.length >= 3 && 'guided'.indexOf(questFSearch) === 0;
    const items = QUESTS.filter(q => {
      if (questFSearch) {
        const idHit = idSearch && String(q.id).indexOf(questFSearch) === 0;
        const gHit = guidedSearch && questHasVisualGuide(q.n);
        if (!idHit && !gHit && q.n.toLowerCase().indexOf(questFSearch) < 0) return false;
      }
      const st = d.st[q.id];
      if (questFStatus === 1) return st === 2;
      if (questFStatus === 2) return st === 1;
      if (questFStatus === 3) return st === 0;
      if (questFStatus === 4) { if (st !== 0) return false; ready[q.id] = questReady(q, d); return ready[q.id]; }
      return true;
    });
    const doneN = QUESTS.filter(q => d.st[q.id] === 2).length;
    const progN = QUESTS.filter(q => d.st[q.id] === 1).length;
    const cnt = $('questCnt');
    if (cnt) cnt.textContent = doneN + '/' + QUESTS.length + ' completed  ·  ' + progN + ' in progress  ·  ' +
      d.qp + ' quest points' + (items.length !== QUESTS.length ? '  ·  ' + items.length + ' shown' : '');
    const sig = questFStatus + '|' + questFSearch + '|' + items.map(q => q.id + ':' + d.st[q.id]).join(',');
    if (sig === questListSig) return;
    questListSig = sig;
    list.innerHTML = '';
    if (!items.length) { list.innerHTML = '<div class="empty">No quests match.</div>'; return; }
    for (const q of items) {
      const st = d.st[q.id];
      if (ready[q.id] === undefined && st === 0) ready[q.id] = questReady(q, d);
      const row = document.createElement('div'); row.className = 'pet-row q-link' + (st === 0 && !ready[q.id] ? ' pet-locked' : '');
      row.style.cursor = 'pointer';
      const ico = document.createElement('div'); ico.className = 'pet-ico';
      if (q.ic > 0) loadSpriteIcon(ico, q.ic);
      row.appendChild(ico);
      const info = document.createElement('div'); info.className = 'bs-info';
      const top = document.createElement('div'); top.className = 'bs-top';
      const nm = document.createElement('div'); nm.className = 'bs-nm'; nm.textContent = q.n;
      top.appendChild(nm); info.appendChild(top);
      const chips = document.createElement('div'); chips.className = 'bs-chips';
      const chip = (cls, txt) => { const s = document.createElement('span'); s.className = 'bs-chip ' + cls; s.textContent = txt; chips.appendChild(s); };
      if (questHasVisualGuide(q.n)) chip('qg', 'Guided');
      if (q.p) chip('m1', q.p + ' QP');
      if (q.d !== undefined) chip('lg', QDIFF[q.d] || ('Diff ' + q.d));
      if (q.mq) chip('lg', 'Miniquest');
      if (idSearch) chip('lg', '#' + q.id);
      const nreq = (q.rq ? q.rq.length : 0) + (q.rs ? q.rs.length : 0) + (q.rqp ? 1 : 0);
      if (nreq && st === 0) {
        const r = questReqs(q, d.vp, id => d.st[id]);
        if (r.missing) chip('m2', r.missing + ' req' + (r.missing > 1 ? 's' : '') + ' missing');
      }
      if (chips.childNodes.length) info.appendChild(chips);
      row.appendChild(info);
      const pill = questPill(st, ready[q.id]);
      const stEl = document.createElement('span'); stEl.className = 'pet-st ' + pill[0]; stEl.textContent = pill[1];
      row.appendChild(stEl);
      row.addEventListener('click', () => { questNav = []; openQuestDetail(q.id, false); });
      list.appendChild(row);
    }
  }
  function renderQuestDetail() {
    const c = $('content');
    const q = QUEST_BY_ID ? QUEST_BY_ID.get(questView) : null;
    const d = questsData;
    // renderPane() polls every 250 ms; rebuild only on real change or hover flickers and scroll resets.
    const sig = questView + '|' + questNav.join(',') + '|' + (d ? d.sig : '');
    if (sig === questDetailSig && $('questDetailWrap')) return;
    questDetailSig = sig;
    c.innerHTML = '';
    const wrap = document.createElement('div'); wrap.id = 'questDetailWrap'; wrap.className = 'pane stor-wrap';
    c.appendChild(wrap);
    if (!q || !d) { wrap.innerHTML = '<div class="empty">Reading...</div>'; return; }
    const back = document.createElement('button'); back.className = 'q-back';
    const prev = questNav.length ? QUEST_BY_ID.get(questNav[questNav.length - 1]) : null;
    back.textContent = '← ' + (prev ? prev.n : 'All quests');
    back.addEventListener('click', () => {
      if (questNav.length) { questView = questNav.pop(); }
      else { questView = null; }
      questListSig = ''; renderQuests();
    });
    wrap.appendChild(back);
    const st = d.st[q.id];
    const isReady = st === 0 ? questReady(q, d) : false;
    qgEnsureLoaded();
    const qgg = questGuideFor(q.n);   // computed here so the focus button can sit by the title
    const hdr = document.createElement('div'); hdr.className = 'qd-hdr';
    const ico = document.createElement('div'); ico.className = 'qd-ico';
    if (q.ic > 0) loadSpriteIcon(ico, q.ic);
    hdr.appendChild(ico);
    const nm = document.createElement('div'); nm.className = 'qd-nm'; nm.textContent = q.n; hdr.appendChild(nm);
    const pill = questPill(st, isReady);
    const stEl = document.createElement('span'); stEl.className = 'pet-st ' + pill[0]; stEl.textContent = pill[1];
    hdr.appendChild(stEl);
    wrap.appendChild(hdr);
    // Focusing opens the Focused Quest tab and drives the in-world visual guide.
    if (qgg) {
      const focused = qgFocusName() === q.n;
      const fbtn = document.createElement('button'); fbtn.className = 'qd-focus' + (focused ? ' on' : '');
      fbtn.innerHTML = '<svg viewBox="0 0 24 24" width="15" height="15" fill="currentColor" style="vertical-align:-2px;margin-right:7px"><path d="M12 2l2.9 6.3 6.9.7-5.1 4.6 1.4 6.8L12 17.8 5.9 20.4l1.4-6.8L2.2 9l6.9-.7z"/></svg>' +
                       (focused ? 'Focused (tap to unfocus)' : 'Focus this quest');
      fbtn.addEventListener('click', () => {
        setQuestFocus(qgFocusName() === q.n ? '' : q.n);
        const ot = TABS.find(x => x.id === 'questfocus');
        if (qgFocusName() && ot) openTab(ot); else { questDetailSig = ''; renderQuests(); }
      });
      wrap.appendChild(fbtn);
    }
    const meta = document.createElement('div'); meta.className = 'bs-chips'; meta.style.marginBottom = '10px';
    const mchip = (cls, txt) => { const s = document.createElement('span'); s.className = 'bs-chip ' + cls; s.textContent = txt; meta.appendChild(s); };
    if (questHasVisualGuide(q.n)) mchip('qg', 'Guided in-world');
    mchip('m1', (q.p || 0) + ' quest point' + (q.p === 1 ? '' : 's'));
    mchip('lg', 'ID ' + q.id);
    if (q.d !== undefined) mchip('lg', QDIFF[q.d] || ('Difficulty ' + q.d));
    mchip('lg', q.m ? 'Members' : 'Free-to-play');
    if (q.yr) mchip('lg', String(q.yr));
    if (q.mq) mchip('lg', 'Miniquest');
    if (QUEST_ENUMS && q.ln !== undefined && QUEST_ENUMS.ln[q.ln]) mchip('lg', QUEST_ENUMS.ln[q.ln]);
    if (QUEST_ENUMS && q.ag !== undefined && QUEST_ENUMS.ag[q.ag]) mchip('lg', QUEST_ENUMS.ag[q.ag]);
    if (q.par && QUEST_BY_ID.get(q.par)) mchip('m2', 'Part of: ' + QUEST_BY_ID.get(q.par).n);
    wrap.appendChild(meta);
    // Journal description (param 5968)
    if (q.ds) {
      const ds = document.createElement('div'); ds.className = 'qd-desc'; ds.textContent = q.ds;
      wrap.appendChild(ds);
    }
    if (st >= 0 && (q.v || q.b)) {
      const v = q.v ? (d.vp[q.v[0]] || 0) : qBits(d.vp, q.b[0], q.b[1], q.b[2]);
      const a = q.v ? q.v[1] : q.b[3], b = q.v ? q.v[2] : q.b[4];
      const box = document.createElement('div'); box.className = 'stor-box';
      const h = document.createElement('div'); h.className = 'stor-h'; h.textContent = 'Progress'; box.appendChild(h);
      const row = document.createElement('div'); row.className = 'q-req ' + (st === 2 ? 'ok' : (st === 1 ? '' : 'miss'));
      row.dataset.tip = (q.v ? ('varp ' + q.v[0]) : ('varp ' + q.b[0] + ' bits ' + q.b[1] + '-' + q.b[2])) +
                        ' = ' + v + '\nstarted at ' + a + ', complete at ' + b;
      const lbl = document.createElement('span'); lbl.className = 'qr-n';
      lbl.textContent = st === 2 ? 'Complete' : (st === 1 ? 'In progress' : 'Not started');
      const val = document.createElement('span'); val.className = 'qr-v';
      val.textContent = st === 1 ? Math.min(100, Math.round((v - a) / Math.max(1, b - a) * 100)) + '%' : '';
      row.appendChild(lbl); row.appendChild(val); box.appendChild(row);
      wrap.appendChild(box);
    }
    // Getting started + items/combat (journal params 7814/9392, 7815, 7816).
    const proseBox = (title, entries) => {
      const bx = document.createElement('div'); bx.className = 'stor-box';
      const hh = document.createElement('div'); hh.className = 'stor-h'; hh.textContent = title; bx.appendChild(hh);
      entries.forEach(e => {
        if (!e[1]) return;
        if (e[0]) { const sb = document.createElement('div'); sb.className = 'qd-sub'; sb.textContent = e[0]; bx.appendChild(sb); }
        const tx = document.createElement('div'); tx.className = 'qd-text'; tx.textContent = e[1];
        if (e[2]) tx.dataset.tip = e[2];
        bx.appendChild(tx);
      });
      return bx;
    };
    if (q.sp) {
      const area = (QUEST_ENUMS && q.ar !== undefined && QUEST_ENUMS.ar[q.ar]) ? QUEST_ENUMS.ar[q.ar] : '';
      // op10 start coords, packed (plane<<28 | x<<14 | y); surfaced as a tooltip
      const tip = (q.xy || []).map(v => {
        const pl = v >>> 28, x = (v >>> 14) & 0x3FFF, y = v & 0x3FFF;
        return '(' + x + ', ' + y + (pl ? ', plane ' + pl : '') + ')';
      }).join('  ');
      wrap.appendChild(proseBox('Getting started' + (area ? ' - ' + area : ''), [[null, q.sp, tip || null]]));
    }
    if (q.it || q.cb) {
      wrap.appendChild(proseBox('Before you start', [['Items', q.it], ['Combat', q.cb]]));
    }
    const lvl = questSkillLevels();
    const reqs = questReqs(q, d.vp, id => d.st[id]);
    const reqRow = (cls, mk) => { const r = document.createElement('div'); r.className = 'q-req ' + cls; mk(r); return r; };
    const box = document.createElement('div'); box.className = 'stor-box';
    const h = document.createElement('div'); h.className = 'stor-h';
    h.textContent = 'Requirements' + (reqs.missing ? ' (' + reqs.missing + ' missing)' : (reqs.skills.length + reqs.quests.length + reqs.extras.length || reqs.qp ? ' (all met)' : ''));
    box.appendChild(h);
    if (!reqs.skills.length && !reqs.quests.length && !reqs.extras.length && !reqs.qp) {
      const e = document.createElement('div'); e.className = 'stor-empty'; e.textContent = 'None.'; box.appendChild(e);
    }
    if (reqs.qp) box.appendChild(reqRow(reqs.qp.ok ? 'ok' : 'miss', r => {
      r.innerHTML = '<span class="qr-n">Quest points</span><span class="qr-v">' + reqs.qp.have + ' / ' + reqs.qp.need + '</span>';
    }));
    reqs.skills.forEach(s => box.appendChild(reqRow(s.ok ? 'ok' : 'miss', r => {
      // 'sk-icon' + data-skill so the async sprite load repaints this box too
      const ic2 = document.createElement('div'); ic2.className = 'qr-ico sk-icon';
      ic2.dataset.skill = String(s.id); attachSkillIcon(ic2, s.id); r.appendChild(ic2);
      const n = document.createElement('span'); n.className = 'qr-n'; n.textContent = (SKILL_NAMES[s.id] || ('Skill ' + s.id)); r.appendChild(n);
      const v = document.createElement('span'); v.className = 'qr-v'; v.textContent = s.have + ' / ' + s.need; r.appendChild(v);
    })));
    reqs.quests.forEach(rq => box.appendChild(reqRow((rq.ok ? 'ok' : 'miss') + ' q-link', r => {
      const c2 = QUEST_BY_ID.get(rq.id);
      const ic2 = document.createElement('div'); ic2.className = 'qr-ico';
      if (c2 && c2.ic > 0) loadSpriteIcon(ic2, c2.ic);
      r.appendChild(ic2);
      const n = document.createElement('span'); n.className = 'qr-n'; n.textContent = rq.name; r.appendChild(n);
      const v = document.createElement('span'); v.className = 'qr-v'; v.textContent = rq.st === 2 ? 'Completed' : QSTATUS[rq.st] || 'Untracked'; r.appendChild(v);
      r.addEventListener('click', () => openQuestDetail(rq.id, true));
    })));
    reqs.extras.forEach(x => box.appendChild(reqRow(x.ok ? 'ok' : 'miss', r => {
      r.innerHTML = '<span class="qr-n">' + htmlEsc(x.name) + '</span><span class="qr-v">' +
                    htmlEsc(x.ok ? (x.done || 'Done') : (x.todo || 'Incomplete')) + '</span>';
    })));
    wrap.appendChild(box);
    // Full prerequisite chain, shown only when it ADDS something over the direct requirements.
    const cl = questClosure(q);
    const indirect = cl.quests.filter(r => !(q.rq || []).includes(r.id));
    const addsInfo = indirect.length > 0 ||
                     [...cl.skills.values()].some(s => s.src !== q.n) ||
                     (cl.qp && cl.qp.src !== q.n);
    if (addsInfo && (cl.quests.length || cl.skills.size || cl.qp)) {
      const box2 = document.createElement('div'); box2.className = 'stor-box';
      let missN = 0;
      const lvlMiss = [...cl.skills].filter(([id, s]) => lvl(id) < s.need).length;
      const qMiss = cl.quests.filter(r => d.st[r.id] !== 2).length;
      missN = lvlMiss + qMiss + ((cl.qp && d.qp < cl.qp.need) ? 1 : 0);
      const h2 = document.createElement('div'); h2.className = 'stor-h';
      h2.textContent = 'Full prerequisite chain' + (missN ? ' (' + missN + ' missing)' : ' (all met)');
      box2.appendChild(h2);
      if (cl.qp) box2.appendChild(reqRow(d.qp >= cl.qp.need ? 'ok' : 'miss', r => {
        const n = document.createElement('span'); n.className = 'qr-n'; n.textContent = 'Quest points'; r.appendChild(n);
        if (cl.qp.src !== q.n) {
          const src = document.createElement('span'); src.className = 'qr-src'; src.textContent = cl.qp.src; r.appendChild(src);
        }
        const v = document.createElement('span'); v.className = 'qr-v'; v.textContent = d.qp + ' / ' + cl.qp.need; r.appendChild(v);
      }));
      [...cl.skills].sort((a, b2) => (lvl(a[0]) < a[1].need ? 0 : 1) - (lvl(b2[0]) < b2[1].need ? 0 : 1) || b2[1].need - a[1].need)
        .forEach(([id, s]) => box2.appendChild(reqRow(lvl(id) >= s.need ? 'ok' : 'miss', r => {
          const ic2 = document.createElement('div'); ic2.className = 'qr-ico sk-icon';
          ic2.dataset.skill = String(id); attachSkillIcon(ic2, id); r.appendChild(ic2);
          const n = document.createElement('span'); n.className = 'qr-n'; n.textContent = (SKILL_NAMES[id] || ('Skill ' + id)); r.appendChild(n);
          if (s.src !== q.n) {
            const src = document.createElement('span'); src.className = 'qr-src'; src.textContent = s.src; r.appendChild(src);
          }
          const v = document.createElement('span'); v.className = 'qr-v'; v.textContent = lvl(id) + ' / ' + s.need; r.appendChild(v);
        })));
      cl.quests.sort((a, b2) => (d.st[a.id] === 2 ? 1 : 0) - (d.st[b2.id] === 2 ? 1 : 0) || a.n.localeCompare(b2.n))
        .forEach(r2 => box2.appendChild(reqRow((d.st[r2.id] === 2 ? 'ok' : 'miss') + ' q-link', r => {
          const ic2 = document.createElement('div'); ic2.className = 'qr-ico';
          if (r2.ic > 0) loadSpriteIcon(ic2, r2.ic);
          r.appendChild(ic2);
          const n = document.createElement('span'); n.className = 'qr-n';
          n.textContent = r2.n;
          r.appendChild(n);
          const v = document.createElement('span'); v.className = 'qr-v';
          v.textContent = d.st[r2.id] === 2 ? 'Completed' : QSTATUS[d.st[r2.id]] || 'Untracked'; r.appendChild(v);
          r.addEventListener('click', () => openQuestDetail(r2.id, true));
        })));
      wrap.appendChild(box2);
    }
    const requiredFor = [];
    QUEST_BY_ID.forEach(o => { if (o.id !== q.id && (o.rq || []).includes(q.id)) requiredFor.push(o); });
    if (requiredFor.length) {
      const rfbox = document.createElement('div'); rfbox.className = 'stor-box';
      const rfh = document.createElement('div'); rfh.className = 'stor-h';
      rfh.textContent = 'Required for (' + requiredFor.length + ')';
      rfbox.appendChild(rfh);
      requiredFor.sort((a, b2) => (d.st[a.id] === 2 ? 1 : 0) - (d.st[b2.id] === 2 ? 1 : 0) || a.n.localeCompare(b2.n))
        .forEach(o => rfbox.appendChild(reqRow('q-link', r => {
          const ic2 = document.createElement('div'); ic2.className = 'qr-ico';
          if (o.ic > 0) loadSpriteIcon(ic2, o.ic);
          r.appendChild(ic2);
          const n = document.createElement('span'); n.className = 'qr-n'; n.textContent = o.n; r.appendChild(n);
          const v = document.createElement('span'); v.className = 'qr-v';
          v.textContent = d.st[o.id] === 2 ? 'Completed' : QSTATUS[d.st[o.id]] || 'Untracked'; r.appendChild(v);
          r.addEventListener('click', () => openQuestDetail(o.id, true));
        })));
      wrap.appendChild(rfbox);
    }
    // Rewards: QP + XP lines (params 7818-7822) + other rewards (7823), <br>-separated.
    if (q.p || q.rx || q.rw) {
      const rbx = document.createElement('div'); rbx.className = 'stor-box';
      const rh = document.createElement('div'); rh.className = 'stor-h'; rh.textContent = 'Rewards'; rbx.appendChild(rh);
      const li = t => {
        t = (t || '').trim(); if (!t) return;
        const el = document.createElement('div'); el.className = 'qd-li';
        const sp2 = document.createElement('span'); sp2.textContent = t;
        el.appendChild(sp2); rbx.appendChild(el);
      };
      if (q.p) li(q.p + ' quest point' + (q.p === 1 ? '' : 's'));
      (q.rx ? q.rx.split(/<br\s*\/?\s*>/i) : []).forEach(li);
      (q.rw ? q.rw.split(/<br\s*\/?\s*>/i) : []).forEach(li);
      wrap.appendChild(rbx);
    }
    // Quick guide from the wiki-imported quest_guides.js, spliced in by the host.
    if (qgg) {
      const gbox = document.createElement('div'); gbox.className = 'stor-box';
      const gh = document.createElement('div'); gh.className = 'stor-h'; gh.textContent = 'Quick guide';
      gbox.appendChild(gh);
      const gbody = document.createElement('div');
      gbody.innerHTML = qgGuideHtml(q.n, qgg, st === 2);
      gbody.addEventListener('click', e => {
        const stp = e.target.closest('.myst-step');
        if (stp && stp.dataset.qn !== undefined) {
          qgToggleStep(stp.dataset.qn, +stp.dataset.i);
          questDetailSig = ''; renderQuests();
          return;
        }
        const rs = e.target.closest('.myst-reset');
        if (rs && rs.dataset.qreset !== undefined) {
          if (questGSteps) { delete questGSteps[rs.dataset.qreset]; qgSave(); updateQuestHighlight(); }
          questDetailSig = ''; renderQuests();
        }
      });
      gbox.appendChild(gbody);
      wrap.appendChild(gbox);
    }
    // Replacement-history footnote (param 7888, e.g. "In 2012 this quest replaced ...")
    if (q.rp) {
      const rp = document.createElement('div'); rp.className = 'qd-note'; rp.textContent = q.rp;
      wrap.appendChild(rp);
    }
  }
