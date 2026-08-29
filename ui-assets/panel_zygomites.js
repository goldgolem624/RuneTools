// RuneToolsX panel: Anachronia base camp guide (tab id 'anachronia').
// The Ancient Zygomites tracker that used to live in this file moved out to the
// marketplace plugin com.runetools.zygomites (2026-08-03); the file name stays because
// the Dock.cpp inject list is compiled and this section hot-syncs without a rebuild.
(function () {

// ---- Anachronia base camp guide (tab id 'anachronia') ----
// Lives in this file because panel files are compiled into the inject list; adding code to
// an already-registered file hot-syncs without a rebuild. Ground truth: db table 50 =
// resources (name/sprite; row ids 1726-1731 are the cost references), db table 51 =
// buildings (name/tier costs/effects), script6061 = building index -> tier varbit,
// script6642 = resource index -> varp 8589-8594, script5844 = workers vb 44275-44280,
// script6801 rate = workers * 60/hr, script6641 caps 5000/25000/80000/150000 by Storehouse
// tier, script2072 max workers 10/15/30/60 by Sleeping Quarters tier (+5/+10 when
// vb44292 == 30 and vb52504 tier 2/3).
  const ANA_RES = [
    { n: 'Wood',   vp: 8589, wvb: 44275, sp: 946, row: 1726 },
    { n: 'Vines',  vp: 8590, wvb: 44276, sp: 944, row: 1727 },
    { n: 'Leaves', vp: 8591, wvb: 44277, sp: 945, row: 1728 },
    { n: 'Hides',  vp: 8592, wvb: 44278, sp: 948, row: 1729 },
    { n: 'Stone',  vp: 8593, wvb: 44279, sp: 950, row: 1730 },
    { n: 'Clay',   vp: 8594, wvb: 44280, sp: 949, row: 1731 },
  ];
  // c: [resourceRow, [tier1, tier2, tier3]] pairs; fx: effect text per tier
  const ANA_BUILDINGS = [
    { n: 'Town Hall', vb: 44262, mt: 3, c: [[1726,[1000,20000,70000]],[1727,[1000,20000,70000]],[1730,[1000,20000,70000]]],
      fx: ['Allows tier 1 buildings & attracts new visitors.','Allows tier 2 buildings & attracts new visitors.','Allows tier 3 buildings & attracts new visitors.'] },
    { n: 'Storehouse', vb: 44263, mt: 3, c: [[1727,[3000,22000,75000]],[1729,[3000,22000,75000]],[1730,[3000,22000,75000]]],
      fx: ['Max resources 25,000.','Max resources 80,000.','Max resources 150,000.'] },
    { n: 'Sleeping Quarters', vb: 44264, mt: 3, c: [[1726,[10000,65000,130000]],[1728,[10000,65000,130000]],[1729,[10000,65000,130000]]],
      fx: ['15 workers.','30 workers.','60 workers.'] },
    { n: 'Spa', vb: 44265, mt: 3, c: [[1726,[7500,60000,120000]],[1728,[7500,60000,120000]],[1731,[7500,60000,120000]]],
      fx: ['+5% Agility XP on the island.','+7% Agility XP on the island.','+10% Agility XP + spa effects last 50% longer.'] },
    { n: 'Hunter Lodge', vb: 44266, mt: 3, c: [[1726,[7500,60000,120000]],[1728,[7500,60000,120000]],[1729,[7500,60000,120000]]],
      fx: ['10% extra woodcutting resources in Big Game encounters.','Big Game creatures take +2.4s to catch you.','One frog type removed each Big Game encounter.'] },
    { n: 'Slayer Lodge', vb: 44267, mt: 3, c: [[1727,[7500,60000,120000]],[1730,[7500,60000,120000]],[1731,[7500,60000,120000]]],
      fx: ['+1% damage on the island.','+3% damage on the island.','+6% damage + Slayer helmet stand (global helm bonus).'] },
    { n: 'Player Lodge', vb: 44268, mt: 3, c: [[1727,[12000,75000,150000]],[1729,[12000,75000,150000]],[1731,[12000,75000,150000]]],
      fx: ['+10% chance of finding building resources.','Restores stats + POH teletab modification.','Skillcape stand (passive skillcape perk).'] },
    { n: 'Bank Chest', vb: 44269, mt: 1, c: [[1726,[500]],[1730,[500]]], fx: ['Bank chest at the Town Hall.'] },
    { n: 'Lodestone', vb: 44270, mt: 1, c: [[1730,[150]],[1731,[150]]], fx: ['Anachronia lodestone.'] },
    { n: 'Remote Totem Access Point', vb: 47593, mt: 1, c: [[1730,[150000]],[1727,[150000]],[1726,[150000]]],
      fx: ['Base camp totem that charges all other totems.'] },
    { n: 'Deposit box (NW)', vb: 44271, mt: 1, c: [[1726,[30000]],[1730,[30000]]], fx: ['Deposit box on Anachronia.'] },
    { n: 'Deposit box (NE)', vb: 44272, mt: 1, c: [[1726,[30000]],[1730,[30000]]], fx: ['Deposit box on Anachronia.'] },
    { n: 'Deposit box (SW)', vb: 44273, mt: 1, c: [[1726,[30000]],[1730,[30000]]], fx: ['Deposit box on Anachronia.'] },
    { n: 'Deposit box (SE)', vb: 44274, mt: 1, c: [[1726,[30000]],[1730,[30000]]], fx: ['Deposit box on Anachronia.'] },
    { n: 'Bait box', vb: 44331, mt: 3, c: [[1726,[500,5000,100000]],[1729,[500,5000,100000]]],
      fx: ['Bait box throughout Anachronia.','Larger bait box.','Larger bait box that acts as a deposit box.'] },
  ];
  const ANA_CAPS = [5000, 25000, 80000, 150000];   // by Storehouse tier
  const ANA_WMAX = [10, 15, 30, 60];               // by Sleeping Quarters tier
  const ANA_RES_BY_ROW = {};
  for (const r of ANA_RES) ANA_RES_BY_ROW[r.row] = r;
  let anaVp = null, anaVb = null, anaTick = 0;

  async function anaRefresh() {
    try {
      const vps = ANA_RES.map(function (r) { return r.vp; });
      const vbs = [44292, 52504];
      for (const r of ANA_RES) vbs.push(r.wvb);
      for (const b of ANA_BUILDINGS) vbs.push(b.vb);
      const vp = JSON.parse(await bridge().varps(myPid(), vps.join(','))) || {};
      const vb = await readVarbitValues(vbs);
      const sig = JSON.stringify(vp) + JSON.stringify(vb);
      if (sig !== anaRefresh._sig) { anaRefresh._sig = sig; anaVp = vp; anaVb = vb; anaPaint(); }
      else if (!anaVp) { anaVp = vp; anaVb = vb; }
    } catch (e) {}
  }
  function anaOpen() { anaRefresh(); }
  setInterval(function () {
    if (typeof paneVisible === 'undefined' || !paneVisible('anachronia') || !document.getElementById('anaWrap')) return;
    if (++anaTick % 4 === 0) anaRefresh();
  }, 700);

  function anaCap() { return ANA_CAPS[Math.min(3, (anaVb && anaVb[44263]) | 0)]; }
  function anaWorkerMax() {
    let m = ANA_WMAX[Math.min(3, (anaVb && anaVb[44264]) | 0)];
    // the game's own bonus rule (script2072): leafy-bush morph 30 + tiered structure
    if (anaVb && (anaVb[44292] | 0) === 30) {
      const t = anaVb[52504] | 0;
      if (t === 2) m += 5; else if (t === 3) m += 10;
    }
    return m;
  }
  function anaFmt(v) { return (v | 0).toLocaleString('en-US'); }
  function anaStars(t, mt) {
    let h = '';
    for (let i = 1; i <= mt; i++) h += '<span class="ana-star' + (t >= i ? ' on' : '') + '">&#9733;</span>';
    return h;
  }
  function anaPaint() {
    const el = $('anaWrap'); if (!el || !anaVp) return;
    const cap = anaCap(), wmax = anaWorkerMax();
    let used = 0;
    for (const r of ANA_RES) used += (anaVb[r.wvb] | 0);
    let h = '<div class="card ana-head"><div class="ana-title">Anachronia Base Camp</div>'
      + '<div class="ana-sub">Workers <b>' + used + '</b>/' + wmax + ' assigned - each gathers 60/hr. '
      + 'Storage cap <b>' + anaFmt(cap) + '</b> per resource.</div></div>';
    h += '<div class="card"><div class="ana-sec">Resources</div>';
    for (const r of ANA_RES) {
      const v = anaVp[r.vp] | 0, w = anaVb[r.wvb] | 0, rate = w * 60;
      const pct = Math.min(100, Math.round(v / cap * 100));
      let eta = '';
      if (v >= cap) eta = 'full';
      else if (rate > 0) {
        const hrs = (cap - v) / rate;
        eta = 'full in ' + (hrs >= 1 ? Math.floor(hrs) + 'h ' : '') + Math.round((hrs % 1) * 60) + 'm';
      }
      h += '<div class="ana-res"><span class="ana-ric" data-sp="' + r.sp + '"></span>'
        + '<span class="ana-rn">' + r.n + '</span>'
        + '<span class="ana-rv"><b>' + anaFmt(v) + '</b><span style="opacity:.55">/' + anaFmt(cap) + '</span></span>'
        + '<span class="ana-rw" title="workers assigned">' + (w ? w + ' <span style="opacity:.6">(' + anaFmt(rate) + '/hr)</span>' : '<span style="opacity:.4">idle</span>') + '</span>'
        + '<span class="ana-eta">' + eta + '</span>'
        + '<div class="ana-bar"><div style="width:' + pct + '%"></div></div></div>';
    }
    h += '</div><div class="card"><div class="ana-sec">Buildings</div>';
    for (const b of ANA_BUILDINGS) {
      const t = Math.min(b.mt, (anaVb[b.vb] | 0));
      const fx = t > 0 ? b.fx[Math.min(t, b.fx.length) - 1] : '';
      let next = '';
      if (t < b.mt) {
        const parts = b.c.map(function (cc) {
          const need = cc[1][t]; if (need == null) return '';
          const res = ANA_RES_BY_ROW[cc[0]];
          const have = res ? (anaVp[res.vp] | 0) : 0;
          return '<span style="color:' + (have >= need ? '#4dd28a' : '#fbbf24') + '">' + anaFmt(need) + ' ' + (res ? res.n : cc[0]) + '</span>';
        }).filter(Boolean);
        next = parts.length ? ('next: ' + parts.join(' + ')) : '';
      }
      h += '<div class="ana-b"><div class="ana-brow"><span class="ana-bn">' + b.n + '</span>'
        + '<span class="ana-bst">' + anaStars(t, b.mt) + '</span></div>'
        + (fx ? '<div class="ana-bfx">' + fx + '</div>' : '')
        + (next ? '<div class="ana-bnx">' + next + '</div>' : '')
        + '</div>';
    }
    h += '</div>';
    el.innerHTML = h;
    const ics = el.querySelectorAll('.ana-ric');
    for (let i = 0; i < ics.length; i++) {
      try { loadSpriteIcon(ics[i], +ics[i].dataset.sp); } catch (e) {}
    }
  }

  function renderAnachronia() {
    const c = $('content'); if (!c) return;
    if ($('anaWrap')) return;                       // 250ms poll: never rebuild a live mount
    injectStyle('anaCss',
      '.ana-wrap{display:flex;flex-direction:column;gap:8px}'
      + '.ana-title{font-weight:700;font-size:14px}'
      + '.ana-sub{font-size:11.5px;color:var(--text-dim)}'
      + '.ana-sec{font-weight:700;font-size:13px;margin-bottom:6px}'
      + '.ana-res{display:grid;grid-template-columns:22px 52px 1fr auto auto;gap:4px 8px;align-items:center;padding:4px 0;font-size:12px}'
      + '.ana-ric{width:20px;height:20px;background-size:contain;background-repeat:no-repeat;background-position:center}'
      + '.ana-rv{font-variant-numeric:tabular-nums}'
      + '.ana-rw{font-variant-numeric:tabular-nums}'
      + '.ana-eta{opacity:.65;font-size:11px;text-align:right}'
      + '.ana-bar{grid-column:1 / -1;height:4px;border-radius:2px;background:rgba(255,255,255,0.08);overflow:hidden}'
      + '.ana-bar > div{height:100%;background:#4dd28a;border-radius:2px}'
      + '.ana-b{padding:6px 0;border-bottom:1px solid rgba(255,255,255,0.05)}'
      + '.ana-b:last-child{border-bottom:none}'
      + '.ana-brow{display:flex;align-items:center;gap:8px}'
      + '.ana-bn{flex:1;font-size:12.5px;font-weight:600}'
      + '.ana-star{color:rgba(255,255,255,0.18);font-size:13px}'
      + '.ana-star.on{color:#ffd23f}'
      + '.ana-bfx{font-size:11.5px;opacity:.75;margin-top:2px}'
      + '.ana-bnx{font-size:11px;margin-top:2px;opacity:.9}');
    c.innerHTML = '';
    const wrap = document.createElement('div');
    wrap.id = 'anaWrap'; wrap.className = 'ana-wrap';
    wrap.innerHTML = '<div class="card"><div class="ana-sub">Reading base camp data...</div></div>';
    c.appendChild(wrap);
    anaPaint(); anaRefresh();
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
registerTab({ id: 'anachronia', render: renderAnachronia, open: function () { if (typeof anaOpen === 'function') anaOpen(); } });
})();
