// RuneToolsX panel: Anachronia Agility Course (route guide, section progress, movement helper).
// Spliced inline into client.html at load; bare classic script sharing one global scope.
// Obstacle highlighting uses the OBJECT primitive (guideMarks) - there is no object outline.

  // [letter, side, tier, agility level, obstacle count]
  const ANACH_SECTIONS = [["A","Northern","Beginner",30,6],["B","Northern","Novice",50,8],["C","Northern","Advanced",70,8],["D","Eastern","Master",85,8],["E","Southern","Advanced",70,8],["F","Southern","Novice",50,6],["G","Southern","Beginner",30,8]];
  // The course, straight from the cache: obstacle locs 113687-113738, each with exactly one
  // placement. Route order is DESCENDING loc id, cross-checked obstacle-for-obstacle against
  // the game's own seven sections. [locId, name, action, x, y, plane, sectionIndex]
  const ANACH_COURSE = [[113738,"Cliff face","Traverse",5428,2384,0,0],[113737,"Cliff face","Traverse",5426,2388,0,0],[113736,"Ruined temple","Traverse",5425,2398,1,0],[113735,"Ruined temple","Traverse",5431,2408,1,0],[113734,"Cave entrance","Enter",5431,2418,0,0],[113733,"Roots","Cross",5485,2456,1,0],[113732,"Ruined temple","Traverse",5505,2463,0,1],[113731,"Ruined temple","Jump across",5505,2469,1,1],[113730,"Ruined temple","Traverse",5505,2479,1,1],[113729,"Ruined temple","Climb",5529,2492,1,1],[113728,"Ruined temple","Jump across",5537,2492,1,1],[113727,"Bones","Traverse",5565,2452,1,1],[113726,"Spine","Cross",5575,2453,1,1],[113725,"Bones","Traverse",5585,2452,1,1],[113724,"Cliff face","Traverse",5591,2448,0,2],[113723,"Ruined temple","Traverse",5602,2433,0,2],[113722,"Ruined temple","Traverse",5609,2433,1,2],[113721,"Ruined temple","Traverse",5617,2433,1,2],[113720,"Ruined temple","Traverse",5627,2433,0,2],[113719,"Roots","Traverse",5642,2425,1,2],[113718,"Vines","Cross",5644,2420,1,2],[113717,"Vines","Cross",5653,2399,1,2],[113716,"Vines","Cross",5655,2371,1,3],[113715,"Roots","Climb over",5676,2363,1,3],[113714,"Sunken column","Jump across",5696,2339,1,3],[113713,"Big block","Climb over",5693,2317,1,3],[113712,"Roots","Jump across",5686,2303,1,3],[113711,"Roots","Traverse",5684,2292,0,3],[113710,"Vines","Cross",5674,2290,1,3],[113709,"Vines","Cross",5663,2288,1,3],[113708,"Block","Climb over",5627,2287,1,4],[113707,"Ruins","Traverse",5594,2295,1,4],[113706,"Vines","Cross",5581,2295,1,4],[113705,"Bones","Climb over",5577,2289,1,4],[113704,"Rock","Traverse",5563,2272,1,4],[113703,"Root","Traverse",5553,2246,1,4],[113702,"Vines","Cross",5548,2237,1,4],[113701,"Vines","Cross",5548,2214,1,4],[113700,"Ruins","Traverse",5524,2182,0,5],[113699,"Ruins","Jump across",5495,2171,1,5],[113698,"Plank","Cross",5483,2171,1,5],[113697,"Ruined temple","Traverse",5474,2171,0,5],[113696,"Ruined column","Traverse",5456,2180,1,5],[113695,"Cliff face","Traverse",5437,2217,0,5],[113694,"Tree","Jump across",5390,2240,1,6],[113693,"Cliff face","Traverse",5376,2248,1,6],[113692,"Vines","Cross",5363,2282,1,6],[113691,"Root","Climb over",5368,2304,0,6],[113690,"Vines","Cross",5394,2320,1,6],[113689,"Cliff face","Traverse",5408,2324,0,6],[113688,"Cliff face","Traverse",5411,2325,0,6],[113687,"Temple wall","Traverse",5415,2324,1,6]];

  const AGI_SKILL = 16;                 // Agility
  const AGI_FIRST = 113687, AGI_LAST = 113738;
  let agiRec = null;      // record mode: {vars:{id:value}, marks:[], startedAt}
  let agiCleared = [];    // obstacle index -> true once taken this lap
  let agiLastIdx = -1;    // last obstacle we saw the player at
  let agiMoves = {};      // obstacle index -> 'surge' | 'dive', positions the player marked
  let agiPos = null;      // {x,y,plane}
  let agiCds = { surge: 0, dive: 0 };   // seconds remaining, 0 = ready

  try {
    const m = JSON.parse(localStorage.getItem('rtxAgiMoves') || 'null');
    if (m && typeof m === 'object') agiMoves = m;
  } catch (e) {}
  function agiSaveMoves() {
    try { localStorage.setItem('rtxAgiMoves', JSON.stringify(agiMoves)); } catch (e) {}
  }

  function agiSectionOf(i) { return ANACH_SECTIONS[ANACH_COURSE[i][6]]; }
  function agiLevel() {
    try { const l = questSkillLevels(); return l ? (l(AGI_SKILL) | 0) : 0; } catch (e) { return 0; }
  }
  // Section is runnable only at its own Agility level; a full lap needs the highest of them.
  function agiSectionOk(si) { const lv = agiLevel(); return lv <= 0 || lv >= ANACH_SECTIONS[si][3]; }

  // Progress per section, counted from the obstacles actually cleared this lap.
  function agiSectionProgress() {
    const out = ANACH_SECTIONS.map(function (s) { return { done: 0, total: s[4] }; });
    for (let i = 0; i < ANACH_COURSE.length; i++)
      if (agiCleared[i]) out[ANACH_COURSE[i][6]].done++;
    return out;
  }
  function agiLapDone() { let n = 0; for (const c of agiCleared) if (c) n++; return n; }

  // The next obstacle to take: the one after the furthest cleared, skipping sections the
  // player has no level for (they can start anywhere, so an unreachable section is not a wall).
  function agiNextIdx() {
    for (let i = 0; i < ANACH_COURSE.length; i++) {
      if (agiCleared[i]) continue;
      if (!agiSectionOk(ANACH_COURSE[i][6])) continue;
      return i;
    }
    return -1;
  }

  function agiDist(o) {
    if (!agiPos) return 1e9;
    return Math.max(Math.abs(o[3] - agiPos.x), Math.abs(o[4] - agiPos.y));
  }
  // Standing on an obstacle counts it as taken. Obstacles are far apart, so a 4-tile window
  // is unambiguous and tolerates the tile you land on rather than the tile of the loc.
  function agiSeen() {
    if (!agiPos) return;
    let best = -1, bd = 5;
    for (let i = 0; i < ANACH_COURSE.length; i++) {
      const d = agiDist(ANACH_COURSE[i]);
      if (d < bd) { bd = d; best = i; }
    }
    if (best < 0 || best === agiLastIdx) return;
    agiLastIdx = best;
    agiCleared[best] = true;
    if (agiRec) agiRecSnap(best);
  }

  // ---- record mode: which vars move as each obstacle is taken -------------------
  async function agiRecSnap(idx) {
    if (!agiRec || !bridge()) return;
    try {
      const raw = await bridge().varps(myPid(), agiRec.watch.join(','));
      const vp = JSON.parse(raw) || {};
      const moved = [];
      for (const k in vp) if (agiRec.vars[k] !== undefined && agiRec.vars[k] !== vp[k])
        moved.push({ v: +k, from: agiRec.vars[k], to: vp[k] });
      agiRec.vars = vp;
      if (moved.length) agiRec.log.push({ idx: idx, loc: ANACH_COURSE[idx][0], moved: moved });
    } catch (e) {}
  }
  async function agiRecStart() {
    if (!bridge()) return;
    // Watch a broad band of varps and let the deltas name themselves; the course counters
    // are unknown, so nothing is assumed about which one moves.
    const watch = [];
    for (let v = 0; v < 6000; v++) watch.push(v);
    agiRec = { watch: watch, vars: {}, log: [], startedAt: Date.now() };
    try { agiRec.vars = JSON.parse(await bridge().varps(myPid(), watch.join(','))) || {}; } catch (e) {}
    agiPaint();
  }
  function agiRecStop() { agiRec = null; agiPaint(); }
  function agiRecText() {
    if (!agiRec) return '';
    const seen = {};
    for (const e of agiRec.log) for (const m of e.moved) seen[m.v] = (seen[m.v] || 0) + 1;
    const ids = Object.keys(seen).sort(function (a, b) { return seen[b] - seen[a]; });
    return ids.slice(0, 12).map(function (v) { return 'vp ' + v + ' moved ' + seen[v] + 'x'; }).join(', ');
  }

  // ---- movement abilities ------------------------------------------------------
  // Cooldowns come from the ability's own varc clock pair, so a prompt never suggests an
  // ability that is still recharging.
  async function agiReadCds() {
    agiCds.surge = 0; agiCds.dive = 0;
    if (!bridge() || !bridge().abilityCooldowns) return;
    try {
      const a = JSON.parse(await bridge().abilityCooldowns(myPid())) || {};
      // remaining is in client-clock cycles (50/s); only abilities actually ticking appear.
      for (const c of (a.cooldowns || [])) {
        const nm = String(c.name || '').toLowerCase();
        const secs = Math.max(0, (+c.remaining || 0) / 50);
        if (nm.indexOf('surge') >= 0) agiCds.surge = Math.max(agiCds.surge, secs);
        else if (nm.indexOf('dive') >= 0) agiCds.dive = Math.max(agiCds.dive, secs);
      }
    } catch (e) {}
  }
  function agiMoveHint(i) {
    const m = agiMoves[ANACH_COURSE[i][0]];
    if (!m) return '';
    const cd = m === 'surge' ? agiCds.surge : agiCds.dive;
    if (cd > 0) return '<span class="agi-cd">' + m + ' in ' + cd.toFixed(1) + 's</span>';
    return '<span class="agi-go">' + m + ' ready</span>';
  }
  function agiMarkMove(kind) {
    const i = agiLastIdx >= 0 ? agiLastIdx : agiNextIdx();
    if (i < 0) return;
    const key = ANACH_COURSE[i][0];
    if (agiMoves[key] === kind) delete agiMoves[key]; else agiMoves[key] = kind;
    agiSaveMoves(); agiPaint();
  }

  // ---- in-game highlight -------------------------------------------------------
  function agiHighlight() {
    if (!bridge() || !bridge().guideMarks) return;
    const i = agiNextIdx();
    if (i < 0) { try { bridge().guideMarks(myPid(), ''); } catch (e) {} return; }
    const o = ANACH_COURSE[i];
    // First line NAMES the loc: the host boxes that loc's footprint near the tile.
    const label = o[1] + '\n' + o[2] + '  (' + agiSectionOf(i)[0] + ')';
    try { bridge().guideMarks(myPid(), o[3] + '\x1f' + o[4] + '\x1f' + o[5] + '\x1f' + label); } catch (e) {}
  }

  async function agiTick() {
    if (typeof activeTab === 'undefined' || activeTab !== 'agility') return;
    if (typeof scanPlayerTile === 'function') {
      try {
        const p = await scanPlayerTile();          // shared helper: {x, y, p}
        if (p && p.x) agiPos = { x: p.x | 0, y: p.y | 0, plane: p.p | 0 };
      } catch (e) {}
    }
    agiSeen();
    await agiReadCds();
    agiHighlight();
    agiPaint();
  }
  setInterval(function () { agiTick(); }, 700);

  function agiPaint() {
    const el = $('agiBody'); if (!el) return;
    const lv = agiLevel();
    const prog = agiSectionProgress();
    const done = agiLapDone(), next = agiNextIdx();

    let html = '<div class="agi-lap">'
      + '<b>' + done + '</b>/' + ANACH_COURSE.length + ' obstacles this lap'
      + (lv > 0 ? '<span class="agi-lv">' + lv + ' Agility</span>' : '')
      + '</div><div class="agi-bar"><div style="width:'
      + Math.round(done / ANACH_COURSE.length * 100) + '%"></div></div>';

    html += '<div class="agi-secs">';
    for (let si = 0; si < ANACH_SECTIONS.length; si++) {
      const s = ANACH_SECTIONS[si], p = prog[si];
      const ok = agiSectionOk(si), full = p.done >= p.total;
      html += '<div class="agi-sec' + (full ? ' done' : '') + (ok ? '' : ' locked') + '">'
        + '<span class="agi-let">' + s[0] + '</span>'
        + '<span class="agi-tier">' + s[2] + '<em>' + s[1] + '</em></span>'
        + '<span class="agi-req">' + s[3] + '</span>'
        + '<span class="agi-cnt">' + p.done + '/' + p.total + '</span>'
        + '<span class="agi-mini"><i style="width:' + Math.round(p.done / p.total * 100) + '%"></i></span>'
        + '</div>';
    }
    html += '</div>';

    if (next >= 0) {
      const o = ANACH_COURSE[next], s = agiSectionOf(next);
      html += '<div class="agi-next"><div class="agi-nh">next</div>'
        + '<div class="agi-nn">' + htmlEsc(o[2] + ' ' + o[1].toLowerCase()) + '</div>'
        + '<div class="agi-nm">section ' + s[0] + ' &middot; ' + o[3] + ', ' + o[4]
        + (agiPos ? ' &middot; ' + agiDist(o) + ' tiles' : '') + '</div>'
        + (agiMoveHint(next) ? '<div class="agi-nx">' + agiMoveHint(next) + '</div>' : '')
        + '</div>';
    } else {
      html += '<div class="agi-next"><div class="agi-nn">lap complete</div></div>';
    }

    html += '<div class="agi-btns">'
      + '<button id="agiSurge">mark surge here</button>'
      + '<button id="agiDive">mark dive here</button>'
      + '<button id="agiReset">reset lap</button>'
      + '<button id="agiRec">' + (agiRec ? 'stop recording' : 'record vars') + '</button>'
      + '</div>';
    if (agiRec) html += '<div class="agi-rec">recording &middot; ' + (agiRecText() || 'no var has moved yet') + '</div>';

    el.innerHTML = html;
    const b = function (id, fn) { const n = $(id); if (n) n.onclick = fn; };
    b('agiSurge', function () { agiMarkMove('surge'); });
    b('agiDive', function () { agiMarkMove('dive'); });
    b('agiReset', function () { agiCleared = []; agiLastIdx = -1; agiPaint(); });
    b('agiRec', function () { if (agiRec) agiRecStop(); else agiRecStart(); });
  }

  function renderAgility() {
    const c = $('content'); if (!c) return;
    if ($('agiBody')) { agiPaint(); return; }   // renderPane polls: never rebuild a live mount
    c.innerHTML = '';
    injectStyle('agiCss',
      '.agi-wrap{display:flex;flex-direction:column;gap:8px;height:100%;min-height:0;overflow:auto}'
      + '.agi-lap{display:flex;align-items:baseline;gap:8px;font-size:13px}'
      + '.agi-lap b{font-size:18px}'
      + '.agi-lv{margin-left:auto;opacity:.6;font-size:11px}'
      + '.agi-bar{height:4px;background:rgba(255,255,255,.08);border-radius:2px;overflow:hidden}'
      + '.agi-bar>div{height:100%;background:#4dd28a;transition:width .2s}'
      + '.agi-secs{display:flex;flex-direction:column;gap:3px}'
      + '.agi-sec{display:flex;align-items:center;gap:8px;padding:5px 8px;border-radius:5px;'
      + 'background:rgba(255,255,255,.03);font-size:12px}'
      + '.agi-sec.done{background:rgba(77,210,138,.10)}'
      + '.agi-sec.locked{opacity:.42}'
      + '.agi-let{width:14px;font-weight:600;opacity:.7}'
      + '.agi-tier{flex:1;display:flex;flex-direction:column;line-height:1.15}'
      + '.agi-tier em{font-style:normal;opacity:.45;font-size:10px}'
      + '.agi-req{opacity:.5;font-size:11px;width:22px;text-align:right}'
      + '.agi-cnt{width:30px;text-align:right;font-variant-numeric:tabular-nums}'
      + '.agi-mini{width:52px;height:3px;background:rgba(255,255,255,.08);border-radius:2px;overflow:hidden}'
      + '.agi-mini>i{display:block;height:100%;background:#4dd28a}'
      + '.agi-next{padding:8px 10px;border-radius:6px;background:rgba(255,255,255,.04)}'
      + '.agi-nh{font-size:10px;text-transform:uppercase;letter-spacing:.08em;opacity:.45}'
      + '.agi-nn{font-size:14px;margin:1px 0}'
      + '.agi-nm{font-size:11px;opacity:.55}'
      + '.agi-nx{margin-top:4px;font-size:11px}'
      + '.agi-cd{color:#fbbf24}.agi-go{color:#4dd28a}'
      + '.agi-btns{display:flex;gap:6px;flex-wrap:wrap}'
      + '.agi-btns button{flex:1;min-width:96px;padding:5px 8px;font-size:11px;border-radius:5px;'
      + 'border:1px solid rgba(255,255,255,.12);background:rgba(255,255,255,.05);color:inherit;cursor:pointer}'
      + '.agi-btns button:hover{background:rgba(255,255,255,.10)}'
      + '.agi-rec{font-size:11px;opacity:.6;line-height:1.4}');
    const w = document.createElement('div');
    w.className = 'agi-wrap';
    w.innerHTML = '<div id="agiBody"></div>';
    c.appendChild(w);
    agiPaint();
    agiTick();
  }
