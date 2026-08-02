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
  let agiCleared = [];    // obstacle index -> true once taken this lap
  let agiLastIdx = -1;    // last obstacle we saw the player at
  let agiPos = null;      // {x,y,plane}

  function agiSectionOf(i) { return ANACH_SECTIONS[ANACH_COURSE[i][6]]; }
  function agiLevel() {
    try { const l = questSkillLevels(); return l ? (l(AGI_SKILL) | 0) : 0; } catch (e) { return 0; }
  }
  // Section is runnable only at its own Agility level; a full lap needs the highest of them.
  function agiSectionOk(si) { const lv = agiLevel(); return lv <= 0 || lv >= ANACH_SECTIONS[si][3]; }

  // The game's own progress: one varbit per section holding a BITMASK that fills from bit
  // (n-1) downward, one bit per obstacle taken, and resets to 0 the moment the section
  // completes. Live-captured on a section A lap: 32, 48, 56, 60, 62, then 0.
  // Only section A's varbit is confirmed; the rest fall back to proximity until captured.
  // Every section's obstacle mask, all live-captured. A, B and C are varp 8587's three 8-bit
  // fields; D through G are varp 8586's four. The ids run strictly downward in section order.
  const ANACH_SECTION_VB = {
    0: 44261, 1: 44260, 2: 44259, 3: 44258, 4: 44257, 5: 44256, 6: 44255,
  };
  // Varp 8585 is the LAP tracker: one bit per section, filling downward from bit 6, so
  // section i owns bit (6 - i) and a full lap reads 127. Live: 96 = A+B, 112 = A+B+C.
  // The same shape as the per-section masks, and far better than inferring completion from a
  // section mask resetting to 0 - that reset is indistinguishable from "not started".
  const ANACH_LAP_VP = 8585;
  let agiLapMask = null;
  function agiSectionComplete(si) {
    if (agiLapMask == null) return !!agiSectionDone[si];
    return (agiLapMask & (1 << (ANACH_SECTIONS.length - 1 - si))) !== 0;
  }
  let agiVb = {};          // varbit id -> value
  let agiVbWas = {};       // last mask seen, to catch the full -> 0 completion flip
  let agiSectionDone = {}; // section index -> completed this lap

  async function agiReadVb() {
    if (!bridge()) return;
    const ids = [];
    for (const k in ANACH_SECTION_VB) ids.push(ANACH_SECTION_VB[k]);
    if (ids.length && bridge().varbits) {
      try { agiVb = JSON.parse(await bridge().varbits(myPid(), ids.join(','))) || {}; } catch (e) {}
    }
    if (bridge().varps) {
      try {
        const vp = JSON.parse(await bridge().varps(myPid(), String(ANACH_LAP_VP))) || {};
        const v = vp[ANACH_LAP_VP];
        if (v !== undefined) agiLapMask = v | 0;
      } catch (e) {}
    }
  }
  function agiPopcount(v) { let n = 0; while (v) { n += v & 1; v >>>= 1; } return n; }
  // The BIT is tied to the obstacle, not to the order it was taken: obstacle j (0-based, in
  // forward route order) owns bit (n-1-j). Live-proven by running section A both ways - going
  // forward the mask fills 32,48,56,60,62 (downward from bit 5), going backward it fills
  // 1,3,7,15,31 (upward from bit 0). So the mask says exactly WHICH obstacles are done, and
  // the course reads correctly whichever direction it is run.
  function agiSectionOffset(si) {
    let n = 0;
    for (let i = 0; i < ANACH_COURSE.length; i++) { if (ANACH_COURSE[i][6] === si) return n; n = i + 1; }
    return -1;
  }
  function agiVbDone(i) {
    const si = ANACH_COURSE[i][6], vb = ANACH_SECTION_VB[si];
    if (vb == null) return null;
    if (agiSectionComplete(si)) return true;           // whole section finished this lap
    const total = ANACH_SECTIONS[si][4];
    let first = -1;
    for (let k = 0; k < ANACH_COURSE.length; k++) if (ANACH_COURSE[k][6] === si) { first = k; break; }
    const j = i - first;                               // position within the section
    return ((agiVb[vb] | 0) & (1 << (total - 1 - j))) !== 0;
  }
  // True when this obstacle is done: the game's own bit where we have it, else what we saw.
  function agiIsDone(i) {
    const v = agiVbDone(i);
    return v === null ? !!agiCleared[i] : v;
  }

  // Progress per section: the game's mask where we have it, else the obstacles we saw.
  function agiSectionProgress() {
    const out = ANACH_SECTIONS.map(function (s) { return { done: 0, total: s[4], live: false }; });
    for (let i = 0; i < ANACH_COURSE.length; i++)
      if (agiIsDone(i)) out[ANACH_COURSE[i][6]].done++;
    for (const k in ANACH_SECTION_VB) out[+k].live = true;
    return out;
  }
  function agiLapDone() {
    let n = 0;
    for (let i = 0; i < ANACH_COURSE.length; i++) if (agiIsDone(i)) n++;
    return n;
  }

  // The next obstacle to take: the one after the furthest cleared, skipping sections the
  // player has no level for (they can start anywhere, so an unreachable section is not a wall).
  // Index range of a section, as [first, last] in route order.
  function agiSectionRange(si) {
    let a = -1, b = -1;
    for (let i = 0; i < ANACH_COURSE.length; i++)
      if (ANACH_COURSE[i][6] === si) { if (a < 0) a = i; b = i; }
    return [a, b];
  }
  // Which obstacle to point at. A section only scores as a consecutive run, so the choice is
  // NOT simply the nearest outstanding obstacle - that can land mid-section, which is no use
  // as a starting point and sends you to the wrong cliff face when several share a name.
  //   - a section already under way continues from where it left off, in the direction it is
  //     being run (the mask says which end has been eaten)
  //   - otherwise the target is the nearest section ENTRY, meaning the first or last obstacle
  //     of a section that is not finished
  function agiNextIdx() {
    const okSec = function (si) { return agiSectionOk(si) && !agiSectionComplete(si); };
    // 1. a section in progress
    for (let si = 0; si < ANACH_SECTIONS.length; si++) {
      if (!okSec(si)) continue;
      const r = agiSectionRange(si);
      let done = 0;
      for (let i = r[0]; i <= r[1]; i++) if (agiIsDone(i)) done++;
      if (!done || done === r[1] - r[0] + 1) continue;      // untouched or finished
      // running forward eats the low indices first, backward eats the high ones
      const fwd = agiIsDone(r[0]);
      for (let k = 0; k <= r[1] - r[0]; k++) {
        const i = fwd ? r[0] + k : r[1] - k;
        if (!agiIsDone(i)) return i;
      }
    }
    // 2. nothing under way: the nearest entry point of an unfinished section
    let best = -1, bd = 1e9;
    for (let si = 0; si < ANACH_SECTIONS.length; si++) {
      if (!okSec(si)) continue;
      const r = agiSectionRange(si);
      for (const i of [r[0], r[1]]) {
        if (agiIsDone(i)) continue;
        if (!agiPos) { if (best < 0) best = i; continue; }
        const d = agiDist(ANACH_COURSE[i]);
        if (d < bd) { bd = d; best = i; }
      }
    }
    return best;
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
  }

  // ---- in-game highlight -------------------------------------------------------
  function agiHighlight() {
    if (!bridge() || !bridge().guideMarks) return;
    const i = agiNextIdx();
    if (i < 0) { try { bridge().guideMarks(myPid(), ''); } catch (e) {} return; }
    const o = ANACH_COURSE[i];
    const enc = function (x, y, p, lab, rgb) {
      return (x | 0) + '\x1f' + (y | 0) + '\x1f' + (p | 0) + '\x1f'
           + String(lab).replace(/[\x1e\x1f]/g, ' ').slice(0, 80) + (rgb ? '\x1f0\x1f' + rgb : '');
    };
    // First line NAMES the loc: the host boxes that loc's footprint near the tile.
    const marks = [enc(o[3], o[4], o[5], o[1] + '\n' + o[2] + '  (' + agiSectionOf(i)[0] + ')')];
    try { bridge().guideMarks(myPid(), marks.join('\x1e')); } catch (e) {}
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
    await agiReadVb();
    agiHighlight();
    agiPaint();
  }
  setInterval(function () { agiTick(); }, 700);

  function agiPaint() {
    const el = $('agiBody'); if (!el) return;
    const lv = agiLevel();
    const prog = agiSectionProgress();
    const done = agiLapDone(), next = agiNextIdx();
    const secDone = ANACH_SECTIONS.filter(function (_, si) { return agiSectionComplete(si); }).length;
    const pct = Math.round(done / ANACH_COURSE.length * 100);

    let html = '<header class="agi-hd">'
      + '<div class="agi-fig"><b>' + done + '</b><span>/ ' + ANACH_COURSE.length + '</span></div>'
      + '<div class="agi-cap">obstacles this lap</div>'
      + '<div class="agi-chips">'
      + (agiLapMask != null
          ? '<span class="agi-chip' + (secDone === 7 ? ' ok' : '') + '">' + secDone + ' / 7 sections</span>' : '')
      + (lv > 0 ? '<span class="agi-chip">' + lv + ' Agility</span>' : '')
      + '</div>'
      + '<div class="agi-bar"><i style="width:' + pct + '%"></i></div>'
      + '</header>';

    if (next >= 0) {
      const o = ANACH_COURSE[next], sec = agiSectionOf(next);
      html += '<div class="agi-next">'
        + '<div class="agi-nh">next obstacle</div>'
        + '<div class="agi-nn">' + htmlEsc(o[2] + ' ' + o[1].toLowerCase()) + '</div>'
        + '<div class="agi-nm"><span>section ' + sec[0] + '</span>'
        + '<span>loc ' + o[0] + '</span>'
        + '<span>' + o[3] + ', ' + o[4] + (o[5] ? ' p' + o[5] : '') + '</span>'
        + (agiPos ? '<span>' + agiDist(o) + ' tiles</span>' : '') + '</div>'
        + '</div>';
    } else {
      html += '<div class="agi-next is-complete"><div class="agi-nn">lap complete</div>'
        + '<div class="agi-nm"><span>all 52 obstacles taken</span></div></div>';
    }

    html += '<div class="agi-secs">';
    for (let si = 0; si < ANACH_SECTIONS.length; si++) {
      const s = ANACH_SECTIONS[si], p = prog[si];
      const ok = agiSectionOk(si), full = p.done >= p.total;
      const cls = 'agi-sec' + (full ? ' is-done' : '') + (ok ? '' : ' is-locked');
      html += '<div class="' + cls + '">'
        + '<span class="agi-let">' + s[0] + '</span>'
        + '<span class="agi-name"><b>' + s[2] + '</b><em>' + s[1] + '</em></span>'
        + '<span class="agi-lvl' + (ok ? '' : ' short') + '">' + s[3] + '</span>'
        + '<span class="agi-track"><i style="width:' + Math.round(p.done / p.total * 100) + '%"></i></span>'
        + '<span class="agi-cnt">' + p.done + '<em>/' + p.total + '</em></span>'
        + '</div>';
    }
    html += '</div>';


    el.innerHTML = html;
  }

  function renderAgility() {
    const c = $('content'); if (!c) return;
    if ($('agiBody')) { agiPaint(); return; }   // renderPane polls: never rebuild a live mount
    c.innerHTML = '';
    injectStyle('agiCss',
      '.agi-wrap{display:flex;flex-direction:column;gap:14px;height:100%;min-height:0;'
      + 'overflow:auto;padding:2px}'
      // header: the figure carries the weight, everything else stays quiet
      + '.agi-hd{display:grid;grid-template-columns:auto 1fr;grid-template-rows:auto auto auto;'
      + 'column-gap:10px;row-gap:2px;align-items:baseline}'
      + '.agi-fig{grid-row:1/3;display:flex;align-items:baseline;gap:3px}'
      + '.agi-fig b{font-size:30px;line-height:1;font-variant-numeric:tabular-nums}'
      + '.agi-fig span{font-size:13px;color:var(--text-dim)}'
      + '.agi-cap{font-size:12px;color:var(--text-dim);align-self:center}'
      + '.agi-chips{grid-column:2;display:flex;gap:6px;flex-wrap:wrap}'
      + '.agi-chip{font-size:10px;padding:2px 7px;border-radius:999px;color:var(--text-dim);'
      + 'border:1px solid var(--border);white-space:nowrap}'
      + '.agi-chip.ok{color:#4dd28a;border-color:rgba(77,210,138,.4)}'
      + '.agi-bar{grid-column:1/3;height:5px;border-radius:3px;margin-top:8px;'
      + 'background:rgba(255,255,255,.07);overflow:hidden}'
      + '.agi-bar>i{display:block;height:100%;background:#4dd28a;transition:width .25s ease}'
      // section rows
      + '.agi-secs{display:flex;flex-direction:column;gap:5px}'
      + '.agi-sec{display:grid;grid-template-columns:26px 1fr auto 76px 46px;align-items:center;'
      + 'gap:11px;padding:9px 12px;border-radius:9px;border:1px solid var(--border);'
      + 'background:rgba(255,255,255,.022)}'
      + '.agi-sec.is-done{border-color:rgba(77,210,138,.32);background:rgba(77,210,138,.07)}'
      + '.agi-sec.is-locked{opacity:.4}'
      + '.agi-let{display:grid;place-items:center;width:26px;height:26px;border-radius:7px;'
      + 'background:rgba(255,255,255,.06);font-size:12px;font-weight:600;color:var(--text-dim)}'
      + '.agi-sec.is-done .agi-let{background:rgba(77,210,138,.18);color:#4dd28a}'
      + '.agi-name{display:flex;flex-direction:column;line-height:1.25;min-width:0}'
      + '.agi-name b{font-size:13px;font-weight:550}'
      + '.agi-name em{font-style:normal;font-size:10.5px;color:var(--text-dim)}'
      + '.agi-lvl{font-size:11px;color:var(--text-dim);font-variant-numeric:tabular-nums}'
      + '.agi-lvl.short{color:#ff6b6b}'
      + '.agi-track{height:4px;border-radius:2px;background:rgba(255,255,255,.07);overflow:hidden}'
      + '.agi-track>i{display:block;height:100%;background:#4dd28a;transition:width .25s ease}'
      + '.agi-cnt{font-size:12px;text-align:right;font-variant-numeric:tabular-nums}'
      + '.agi-cnt em{font-style:normal;color:var(--text-dim)}'
      // next obstacle
      + '.agi-next{padding:12px 14px;border-radius:10px;border:1px solid var(--border);'
      + 'border-left:3px solid #7c6df2;background:rgba(124,109,242,.06)}'
      + '.agi-next.is-complete{border-left-color:#4dd28a;background:rgba(77,210,138,.06)}'
      + '.agi-nh{font-size:9.5px;text-transform:uppercase;letter-spacing:.1em;color:var(--text-dim)}'
      + '.agi-nn{font-size:16px;margin:3px 0 5px}'
      + '.agi-nm{display:flex;gap:14px;flex-wrap:wrap;font-size:11.5px;color:var(--text-dim)}'
      // tools
      + '.agi-tools{display:flex;gap:7px;flex-wrap:wrap}'
      + '.agi-b{flex:1 1 0;min-width:92px;padding:7px 10px;font:inherit;font-size:11.5px;'
      + 'border-radius:7px;border:1px solid var(--border);background:rgba(255,255,255,.045);'
      + 'color:var(--text);cursor:pointer;transition:background .15s}'
      + '.agi-b:hover{background:rgba(255,255,255,.09)}'
      + '.agi-b:focus-visible{outline:2px solid #7c6df2;outline-offset:1px}'
      + '.agi-b.ghost{flex:0 0 auto;color:var(--text-dim)}'
      + '.agi-b.ghost.on{color:#fbbf24;border-color:rgba(251,191,36,.45)}'
);
    const w = document.createElement('div');
    w.className = 'agi-wrap';
    w.innerHTML = '<div id="agiBody"></div>';
    c.appendChild(w);
    agiPaint();
    agiTick();
  }
