// RuneToolsX panel: Boss Info (daily encounter rotations + minigame spotlight).
// Ground truth: cs 11074 + 19823/19825 (Araxxi), 3293/14605 + enum 7211 (Vorago), 19847 + enum 7655 (Barrows RotS), 6747 + enum 10016 (spotlight).
(function () {

  let biSig = '';
  let biVb = null, biVp = null, biFetching = false, biFetchAt = 0;
  const BI_VB_IDS = '57662,57663,57664';
  const BI_VP_IDS = '12121,5419,5420,5421,5422,5423,10946';
  let biSpotEnum = null;                // enum 10016: index -> activity struct id
  const biSpotNames = {};               // struct id -> minigame name (param 1266)

  // Boss mechanics: rows from boss_mechanics.js (loaded on demand), tactic title (struct param 4832) and text
  // (4833) from the user's cache, else our label. The boss shown is the one picked until you enter a boss's
  // encounter (varp 10946 against each boss's encounter structs), then that boss; else the last one picked.
  const BM_FIELDS = ['kind', 'id', 'boss', 'key', 'label', 'tactic', 'window_ms', 'conf'];
  const BM_KINDS = ['', 'animation', 'graphic', 'tile graphic', 'projectile', 'sound', 'hint arrow', 'var', 'spawn'];
  let bmData = null, bmState = 0, bmFailAt = 0, bmPick = 0, bmLastEnc = 0, bmRev = 0, bmNameAt = 0;
  const bmTac = {}, bmName = {};
  const bmJson = t => (typeof t === 'string' ? JSON.parse(t || 'null') : (t || null));
  const bmPlain = s => String(s == null ? '' : s).replace(/<br\s*\/?>/gi, '\n').replace(/<[^>]*>/g, '').replace(/[ \t]+\n/g, '\n').replace(/\n{3,}/g, '\n\n').trim();
  function bmNorm(d) {
    if (!d || typeof d !== 'object' || !Array.isArray(d.rows)) return null;
    const rows = [], bosses = [];
    for (const r of d.rows) {
      let o = r;
      if (Array.isArray(r)) { o = {}; BM_FIELDS.forEach((k, i) => { o[k] = r[i]; }); }
      if (o && (o.boss | 0) > 0 && o.key != null) rows.push(o);
    }
    for (const b of (Array.isArray(d.bosses) ? d.bosses : [])) {
      const o = Array.isArray(b) ? { npc: b[0], phases: b[1], encounter: b[2] } : b;
      if (o && (o.npc | 0) > 0) bosses.push(o);
    }
    for (const r of rows) if (!bosses.some(b => (b.npc | 0) === (r.boss | 0))) bosses.push({ npc: r.boss | 0 });
    return bosses.length ? { bosses, rows } : null;
  }
  function bmEncs(b) {
    const v = b.encounter != null ? b.encounter : b.encounters != null ? b.encounters : b.struct != null ? b.struct : b.enc;
    return (Array.isArray(v) ? v : [v]).map(x => x | 0).filter(x => x > 0);
  }
  async function bmLoad() {
    if (bmData || bmState === 1 || (bmState === 2 && Date.now() - bmFailAt < 30000)) return;
    bmState = 1;
    let d = null;
    try {
      if (window.BOSS_MECHANICS) d = window.BOSS_MECHANICS;
      else if (bridge() && bridge().uiAsset) {
        const txt = String((await rtxData.raw('host.uiAsset', 'boss_mechanics.js')) || '');
        const a = txt.indexOf('{'), b = txt.lastIndexOf('}');
        if (a >= 0 && b > a) { try { d = JSON.parse(txt.slice(a, b + 1)); } catch (e) { d = null; } }
        if (!d && txt) { try { new Function(txt)(); d = window.BOSS_MECHANICS || null; } catch (e) { d = null; } }
      }
    } catch (e) { d = null; }
    bmData = bmNorm(d);
    if (bmData) { if (!window.BOSS_MECHANICS) window.BOSS_MECHANICS = d; bmState = 0; bmRev++; }
    else { bmState = 2; bmFailAt = Date.now(); }
  }
  // Names from the user's cache: the encounter struct's name (param 8849), else the NPC's name.
  async function bmLoadNames() {
    if (!bmData || !bridge()) return;
    if (Date.now() - bmNameAt > 30000) for (const k in bmName) if (bmName[k] === '') delete bmName[k];
    for (const b of bmData.bosses) {
      if (b.name || bmName[b.npc] !== undefined) continue;
      bmName[b.npc] = null;
      let nm = '';
      try {
        const enc = bmEncs(b)[0];
        if (enc && bridge().structParams) { const sp = bmJson(await rtxData.raw('cache.structParams', enc)); nm = (sp && sp.strs && sp.strs['8849']) || ''; }
        if (!nm && bridge().npcInfo) { const ni = bmJson(await rtxData.raw('cache.npcInfo', b.npc)); nm = (ni && ni.name) || ''; }
      } catch (e) { nm = ''; }
      bmName[b.npc] = nm;
      if (nm) bmRev++; else bmNameAt = Date.now();
    }
  }
  async function bmLoadTactics(rows) {
    if (!bridge() || !bridge().structParams) return;
    for (const r of rows) {
      const sid = r.tactic | 0;
      if (sid <= 0 || bmTac[sid] !== undefined) continue;
      bmTac[sid] = null;
      try {
        const sp = bmJson(await rtxData.raw('cache.structParams', sid));
        if (sp && typeof sp === 'object') { const s = sp.strs || {}; bmTac[sid] = { title: bmPlain(s['4832']), text: bmPlain(s['4833']) }; bmRev++; }
        else delete bmTac[sid];
      } catch (e) { delete bmTac[sid]; }
    }
  }
  function bmBossName(b) { return String(b.name || bmName[b.npc] || 'NPC ' + b.npc); }
  function bmCurrent() {
    const enc = biP(10946);
    return bmData && enc > 0 ? bmData.bosses.find(b => bmEncs(b).indexOf(enc) >= 0) || null : null;
  }
  function bmSelected() {
    if (!bmData) return null;
    const cur = bmCurrent();
    let npc = bmPick || (cur ? cur.npc | 0 : 0);
    if (!npc && typeof prefGet === 'function') npc = Number(prefGet('rtxBiMechBoss', 0)) | 0;
    return bmData.bosses.find(b => (b.npc | 0) === npc) || null;
  }

  function biRuneDay() { return Math.floor(Date.now() / 86400000) + 15; }

  const biV = k => ((biVb && biVb[String(k)]) | 0);
  const biP = k => ((biVp && biVp[String(k)]) | 0);
  const biOverridesOn = min => biP(12121) >= min;

  function biAraxxiPath() {             // script19823 -> script19825 (12-day cycle)
    if (biOverridesOn(3) && biV(57662) > 0)
      return { txt: ['', 'Spider Minions', 'Acid Pool', 'Darkness'][biV(57662)] || '?', ovr: true };
    const d = biRuneDay() % 12;
    return { txt: d <= 3 ? 'Spider Minions' : (d <= 7 ? 'Acid Pool' : 'Darkness'), ovr: false };
  }
  const BI_VORAGO = ['Ceiling Collapse', 'Scopulus', 'Vitalis', 'Green Bomb', 'Team Split', 'The End'];
  function biVoragoRotation() {         // script3293 -> script14605 (weekly, 6-week cycle)
    if (biOverridesOn(4) && biV(57664) > 0)
      return { txt: BI_VORAGO[biV(57664) - 1] || '?', ovr: true };   // enum 7211: 1..6 -> 0..5
    const r = Math.floor((biRuneDay() + 21) / 7) % 6;
    return { txt: BI_VORAGO[r], ovr: false };
  }
  const BI_BROTHERS = ['Ahrim', 'Dharok', 'Guthan', 'Karil', 'Torag', 'Verac'];
  let biRotsEnum = null;
  async function biLoadRotsEnum() {
    if (biRotsEnum || !bridge() || !bridge().enumInfo) return;
    try {
      const e = JSON.parse(await rtxData.raw('cache.enumInfo', 7655) || 'null');
      if (e && Object.keys(e).length) biRotsEnum = e;
    } catch (e) {}
  }
  function biBarrowsSides() {
    let mask = null, ovr = false;
    if (biOverridesOn(3) && biV(57663) > 0) { mask = biV(57663); ovr = true; }
    else if (biRotsEnum) mask = biRotsEnum[String(biRuneDay() % 20)] | 0;
    if (mask == null) return null;                 // enum not readable yet
    const west = [], east = [];
    BI_BROTHERS.forEach((nm, i) => ((mask >> i) & 1 ? west : east).push(nm));
    return { west: west.join(', ') || 'none', east: east.join(', ') || 'none', ovr };
  }

  async function biSpotName(idx) {
    if (!biSpotEnum) {
      try { biSpotEnum = JSON.parse(await rtxData.raw('cache.enumInfo', 10016) || 'null'); } catch (e) {}
      if (!biSpotEnum || !Object.keys(biSpotEnum).length) { biSpotEnum = null; return ''; }
    }
    const sid = biSpotEnum[String(idx)] | 0;
    if (sid <= 0) return '';
    if (biSpotNames[sid] === undefined) {
      try {
        const sp = JSON.parse(await rtxData.raw('cache.structParams', sid) || 'null');
        const nm = sp && sp.strs && sp.strs['1266'];
        if (nm) biSpotNames[sid] = nm;
      } catch (e) {}
    }
    return biSpotNames[sid] || '';
  }

  async function fetchBossInfo() {
    if (!paneVisible('bossinfo')) return;
    await biLoadRotsEnum();
    if (bridge() && bridge().varbits && !biFetching) {
      const t = Date.now();
      if (t - biFetchAt >= 2000) {
        biFetchAt = t; biFetching = true;
        try {
          const vb = JSON.parse(await rtxData.raw('state.varbitsCsv', BI_VB_IDS) || 'null');
          if (vb && typeof vb === 'object' && Object.keys(vb).length) biVb = vb;
          const vp = JSON.parse(await rtxData.raw('state.varps', BI_VP_IDS) || 'null');
          if (vp && typeof vp === 'object' && Object.keys(vp).length) biVp = vp;
        } catch (e) { /* keep previous; date math still renders */ }
        biFetching = false;
      }
    }
    try {
      await bmLoad();
      if (biP(10946) !== bmLastEnc) { bmLastEnc = biP(10946); if (bmCurrent()) bmPick = 0; }
      await bmLoadNames();
      const sel = bmSelected();
      if (sel) await bmLoadTactics(bmData.rows.filter(r => (r.boss | 0) === (sel.npc | 0)));
    } catch (e) {}
    paneRun('bossinfo', renderBossInfo);
  }

  function renderBossInfo() {
    const c = $('content');
    let wrap = $('biWrap');
    if (!wrap) {
      c.innerHTML = '';
      wrap = document.createElement('div'); wrap.id = 'biWrap'; wrap.className = 'pk-wrap';
      c.appendChild(wrap); biSig = '';
    }
    const arax = biAraxxiPath(), vor = biVoragoRotation(), rots = biBarrowsSides();
    const spotOk = biVp && (biP(5419) > 0 || biP(5420) > 0 || biP(5421) > 0 || biP(5422) > 0 || biP(5423) > 0);
    const bmSel = bmSelected();
    const sig = JSON.stringify([biRuneDay(), arax, vor, rots, spotOk,
                                biP(5419), biP(5420), biP(5421), biP(5422), biP(5423),
                                bmRev, bmSel ? bmSel.npc : 0, biP(10946)]);
    if (sig === biSig) return;
    biSig = sig;
    wrap.innerHTML = '';

    const section = (title) => {
      const h = document.createElement('div'); h.className = 'bi-sec'; h.textContent = title;
      wrap.appendChild(h);
    };
    const row = (name, lines) => {
      const r = document.createElement('div'); r.className = 'bi-row';
      const nm = document.createElement('div'); nm.className = 'bi-nm'; nm.textContent = name;
      r.appendChild(nm);
      for (const [label, value, cls] of lines) {
        const l = document.createElement('div'); l.className = 'bi-line';
        l.innerHTML = '<span class="bi-lbl">' + label + '</span><span class="bi-val' +
          (cls ? ' ' + cls : '') + '">' + value + '</span>';
        r.appendChild(l);
      }
      wrap.appendChild(r);
      return r;
    };
    const ovrTag = o => (o ? ' (personal override)' : '');
    const d = biRuneDay();

    section('Daily rotations');
    row('Araxxi', [['Blocked path', arax.txt + ovrTag(arax.ovr), 'bi-hi'],
                   ['Changes in', arax.ovr ? 'while overridden: never' : (4 - d % 4) + 'd', '']]);
    row('Vorago', [['Rotation (weekly)', vor.txt + ovrTag(vor.ovr), 'bi-hi'],
                   ['Changes in', vor.ovr ? 'while overridden: never' : (7 - ((d + 21) % 7)) + 'd', '']]);
    if (rots) row('Barrows: Rise of the Six', [['West', rots.west + ovrTag(rots.ovr), 'bi-hi'],
                                               ['East', rots.east, '']]);
    else row('Barrows: Rise of the Six', [['Sides', 'reading rotation table from the cache...', '']]);

    section('Minigame Spotlight');
    if (!spotOk) {
      row('Spotlight', [['Status', 'attach a logged-in client to read the rotation', '']]);
    } else {
      const dayName = days => {
        const d = new Date(Date.now() + days * 86400000);
        return ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'][d.getUTCDay()] + ' ' + d.getUTCDate() + ' ' +
               ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'][d.getUTCMonth()];
      };
      const left = biP(5419);
      const slots = [
        [5420, [['Spotlight active', '500% thaler', 'bi-hi'],
                ['Ends', 'in ' + left + 'd (' + dayName(left) + ', 00:00 UTC)', '']]],
        [5421, [['Starts', 'in ' + left + 'd · ' + dayName(left), '']]],
        [5422, [['Starts', 'in ' + (left + 3) + 'd · ' + dayName(left + 3), '']]],
        [5423, [['Starts', 'in ' + (left + 6) + 'd · ' + dayName(left + 6), '']]]];
      for (const [vpid, lines] of slots) {
        const r = row('Resolving...', lines);
        (async () => {
          const nm = await biSpotName(biP(vpid));
          const el = r.querySelector('.bi-nm');
          if (el) el.textContent = nm || ('Spotlight slot #' + biP(vpid));
        })();
      }
    }
    if (bmData) renderMechanics(wrap, section, bmSel);
  }

  // One card per mechanic: rows sharing a tactic struct, else rows sharing a label (a label with a tactic on
  // any of its rows joins that tactic's card). Title, then the cues, then the tactic text.
  function renderMechanics(wrap, section, sel) {
    const mk = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; };
    section('Boss mechanics');
    const cur = bmCurrent();
    const chips = mk('div', 'pet-chips'); chips.style.marginBottom = '6px';
    for (const b of bmData.bosses) {
      const c = mk('button', 'pet-chip' + (sel && (sel.npc | 0) === (b.npc | 0) ? ' on' : ''), bmBossName(b));
      c.type = 'button';
      c.dataset.tip = 'npc ' + b.npc + (cur && cur.npc === b.npc ? '\nCurrent encounter' : '');
      c.addEventListener('click', () => {
        bmPick = b.npc | 0;
        try { if (typeof prefSet === 'function') prefSet('rtxBiMechBoss', bmPick); } catch (e) {}
        biSig = ''; paneRun('bossinfo', renderBossInfo); fetchBossInfo();
      });
      chips.appendChild(c);
    }
    wrap.appendChild(chips);
    if (!sel) return;
    const rows = bmData.rows.filter(r => (r.boss | 0) === (sel.npc | 0));
    if (!rows.length) { wrap.appendChild(mk('div', 'bi-note', 'No mechanics listed.')); return; }
    const labTac = {}, groups = new Map(), lab = r => String(r.label || r.key);
    for (const r of rows) if ((r.tactic | 0) > 0 && !labTac[lab(r)]) labTac[lab(r)] = r.tactic | 0;
    for (const r of rows) {
      const tac = (r.tactic | 0) || labTac[lab(r)] || 0, k = tac ? 't' + tac : 'l' + lab(r);
      if (!groups.has(k)) groups.set(k, { tac, rows: [] });
      groups.get(k).rows.push(r);
    }
    const grid = mk('div');
    grid.style.cssText = 'display:grid;grid-template-columns:repeat(auto-fill,minmax(240px,1fr));gap:6px;align-items:start';
    for (const g of groups.values()) {
      const t = g.tac ? bmTac[g.tac] : null, title = (t && t.title) || lab(g.rows[0]);
      const card = mk('div', 'bi-row'); card.style.margin = '0';
      card.appendChild(mk('div', 'bi-nm', title));
      for (const r of g.rows) {
        const kind = BM_KINDS[r.kind | 0] || 'kind ' + r.kind, own = lab(r) !== title;
        const l = mk('div', 'bi-line');
        l.appendChild(mk('span', 'bi-lbl', own ? lab(r) : kind));
        const v = mk('span', 'bi-val', own ? kind + ' ' + r.id : String(r.id)); v.style.fontFamily = 'var(--font-mono)';
        l.appendChild(v);
        l.dataset.tip = r.key + (r.conf ? ', confidence ' + r.conf : '') + (r.window_ms ? ', window ' + r.window_ms + ' ms' : '') + ((r.tactic | 0) > 0 ? ', struct ' + r.tactic : '');
        card.appendChild(l);
      }
      if (t && t.text) { const tx = mk('div', 'bi-note', t.text); tx.style.whiteSpace = 'pre-line'; tx.style.marginTop = '4px'; card.appendChild(tx); }
      grid.appendChild(card);
    }
    wrap.appendChild(grid);
  }

Object.assign(window, { biRuneDay, fetchBossInfo });
registerTab({ id: 'bossinfo', render: renderBossInfo, open: function () { biSig = ''; fetchBossInfo(); } });
})();
