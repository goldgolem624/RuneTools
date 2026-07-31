// RuneToolsX panel: Towers (Skyscrapers) clue solver + clue map/scan machinery.
// Spliced inline into client.html at load; shares its global scope (no IIFE); shared
// helpers ($, bridge, myPid, ...) live in the main script and resolve at call time.

  // Towers: interface 1934. Latin square 1-5; each edge clue = towers visible from that side.
  // Clue widgets by t[1]=layer / t[2]=sub: 5 = top (per column), 4 = bottom, 3 = left (per row),
  // 2 = right; 0 or unreadable = no constraint. Placed grid in varbits 39675..39699 (cell i =
  // 39675+i, row-major, 0 = empty) drives only the diff -- the solve uses the clues alone.
  const TOWERS_VB_IDS = Array.from({ length: 25 }, (_, i) => 39675 + i);
  function towersVis(line) { let m = 0, c = 0; for (const v of line) { if (v > m) { m = v; c++; } } return c; }
  const TOWERS_PERMS = (function () { const out = []; (function gen(a) { if (a.length === 5) { out.push(a.slice()); return; } for (let v = 1; v <= 5; v++) if (a.indexOf(v) < 0) { a.push(v); gen(a); a.pop(); } })([]); return out; })();
  function towersSolve(top, bottom, left, right) {
    const N = 5, rows = [];
    for (let r = 0; r < N; r++) {
      rows[r] = TOWERS_PERMS.filter(p => (!left[r] || towersVis(p) === left[r]) && (!right[r] || towersVis(p.slice().reverse()) === right[r]));
      if (!rows[r].length) return null;
    }
    const sol = []; let found = null, count = 0;
    (function bt(r) {
      if (count > 1) return;
      if (r === N) {
        for (let c = 0; c < N; c++) { const col = sol.map(row => row[c]);
          if (top[c] && towersVis(col) !== top[c]) return;
          if (bottom[c] && towersVis(col.slice().reverse()) !== bottom[c]) return; }
        count++; if (!found) found = sol.map(row => row.slice()); return;
      }
      for (const p of rows[r]) {
        let ok = true;
        for (let c = 0; c < N && ok; c++) for (let rr = 0; rr < r; rr++) if (sol[rr][c] === p[c]) { ok = false; break; }
        if (!ok) continue;
        sol.push(p); bt(r + 1); sol.pop();
        if (count > 1) return;
      }
    })(0);
    return found;                                            // first solution (the clues normally make it unique)
  }
  let towersBusy = false, towersOwnsPanel = false, towersDrawSig = '', towersHl = false;
  function towersClearHl() { if (!towersHl) return; try { if (bridge() && bridge().puzzleCells) bridge().puzzleCells(myPid(), ''); } catch (e) {} towersHl = false; }
  async function towersTick() {
    if (towersBusy || !bridge() || !bridge().interfaceGroup) return;
    towersBusy = true;
    try {
      let wj = null; try { wj = JSON.parse(await bridge().interfaceGroup(myPid(), 1934) || '{}'); } catch (e) {}
      const widgets = wj && wj.widgets;
      if (!widgets || !widgets.length) { towersOwnsPanel = false; towersDrawSig = ''; towersClearHl(); return; }
      const top = [0, 0, 0, 0, 0], bottom = [0, 0, 0, 0, 0], left = [0, 0, 0, 0, 0], right = [0, 0, 0, 0, 0];
      const topXc = [], leftYc = []; let sawClue = false;    // clue centres -> derive cell rects for the overlay
      for (const w of widgets) {
        if (!w.t || w.t.length < 3) continue;
        const layer = w.t[1], sub = w.t[2]; if (sub < 0 || sub > 4) continue;
        const val = (w.x != null) ? parseInt(w.x, 10) : NaN;
        const ax = w.a ? w.a[0] : null, ay = w.a ? w.a[1] : null, rw = w.r ? w.r[2] : 0, rh = w.r ? w.r[3] : 0;
        if (layer === 5) { if (!isNaN(val)) top[sub] = val; if (ax != null) topXc[sub] = ax + rw / 2; sawClue = true; }
        else if (layer === 4) { if (!isNaN(val)) bottom[sub] = val; sawClue = true; }
        else if (layer === 3) { if (!isNaN(val)) left[sub] = val; if (ay != null) leftYc[sub] = ay + rh / 2; sawClue = true; }
        else if (layer === 2) { if (!isNaN(val)) right[sub] = val; sawClue = true; }
      }
      if (!sawClue) { towersOwnsPanel = false; towersDrawSig = ''; towersClearHl(); return; }
      towersOwnsPanel = true;
      let vals = {}; try { vals = await readVarbitValues(TOWERS_VB_IDS); } catch (e) {}
      const grid = TOWERS_VB_IDS.map(id => { const v = vals[id] | 0; return (v >= 1 && v <= 5) ? v : 0; });
      const sol = towersSolve(top, bottom, left, right);
      towersDraw(grid, sol);
      towersHighlight(grid, sol, topXc, leftYc);
    } catch (e) {} finally { towersBusy = false; }
  }
  // 5x5 of the TARGET height per cell; cells that differ from the placed grid are boxed gold.
  function towersDraw(grid, sol) {
    const el = $('cluePuzzle'); if (!el) return;
    el.style.display = ''; el._phId = -1; el._lbph = -1;
    const sig = grid.join('') + '|' + (sol ? sol.join('|') : 'x');
    if (sig === towersDrawSig) return; towersDrawSig = sig;
    const ar = sol ? towersActiveRow(grid, sol) : -1;
    let head;
    if (!sol) head = 'no solution - are all edge clues readable?';
    else if (ar < 0) head = '<b>Solved.</b>';
    else head = 'set each cell to the height shown<div style="opacity:0.6;font-size:11px;margin-top:2px">in-game badges show one row at a time, bottom-up (so the right-click menu stays clear)</div>';
    let g = '<div style="margin-top:8px">';
    for (let r = 0; r < 5; r++) {
      g += '<div style="display:flex;gap:3px;margin-bottom:3px">';
      for (let c = 0; c < 5; c++) {
        const cur = grid[r * 5 + c], tgt = sol ? sol[r][c] : 0, wrong = sol && cur !== tgt, active = wrong && r === ar;
        const bd = active ? 'rgba(255,196,64,0.95)' : wrong ? 'rgba(255,196,64,0.38)' : 'rgba(255,255,255,0.10)', bw = wrong ? '2px' : '1px';
        const sh = active ? 'box-shadow:0 0 6px rgba(255,196,64,0.4)' : '';
        g += '<div style="width:30px;height:30px;border-radius:5px;display:flex;align-items:center;justify-content:center;background:rgba(255,255,255,0.04);border:' + bw + ' solid ' + bd + ';' + sh + '">'
           + '<span style="font-weight:700;font-size:14px;color:' + (active ? '#ffd479' : wrong ? 'rgba(255,212,121,0.55)' : cur ? '#cdd6e4' : '#5b6270') + '">' + (tgt || '·') + '</span></div>';
      }
      g += '</div>';
    }
    g += '</div>';
    const html = '<span style="opacity:0.65;text-transform:uppercase;font-size:10px;letter-spacing:0.6px">Towers</span><div style="margin-top:4px">' + head + '</div>' + g;
    setHTML(el, html);
  }
  // In-game badges show target heights ONE ROW AT A TIME, bottom-up, so the right-click menu
  // stays clear. Cell centre = (top-clue x, left-clue y).
  function towersActiveRow(grid, sol) {                       // bottommost row still wrong (-1 = solved)
    for (let r = 4; r >= 0; r--) for (let c = 0; c < 5; c++) if (grid[r * 5 + c] !== sol[r][c]) return r;
    return -1;
  }
  function towersHighlight(grid, sol, topXc, leftYc) {
    if (!sol || !bridge() || !bridge().puzzleCells) { towersClearHl(); return; }
    const r = towersActiveRow(grid, sol);
    if (r < 0) { towersClearHl(); return; }
    let cw = 36; if (topXc[0] != null && topXc[1] != null) cw = Math.abs(topXc[1] - topXc[0]);
    let ch = cw; if (leftYc[0] != null && leftYc[1] != null) ch = Math.abs(leftYc[1] - leftYc[0]);
    const bw = Math.max(20, Math.round(cw * 0.78)), bh = Math.max(20, Math.round(ch * 0.78));
    const segs = [];
    for (let c = 0; c < 5; c++) {
      if (grid[r * 5 + c] === sol[r][c]) continue;
      if (topXc[c] == null || leftYc[r] == null) continue;
      const x0 = Math.round(topXc[c] - bw / 2), y0 = Math.round(leftYc[r] - bh / 2);
      segs.push(x0 + ',' + y0 + ',' + bw + ',' + bh + ',0,' + sol[r][c]);
    }
    if (segs.length) { bridge().puzzleCells(myPid(), segs.join(';')); towersHl = true; } else towersClearHl();
  }

  // Scan eliminations are IN-MEMORY only: a persisted set keyed by AREA carries over to the NEXT
  // clue of the same area and shows a bogus "DIG HERE".
  function scanElimSave() {}
  function scanElimLoad() { try { localStorage.removeItem('rtxScanElim'); } catch (e) {} }   // drop any stale persisted state
  function scanReset() {
    const c = (activeClueId >= 0) ? CLUE_DATA.find(z => z.i === activeClueId) : null; if (!c || c.a !== 'scan') return;
    const rec = scanSpotsFor(c); if (!rec) return;
    scanElimByArea[rec.key || ('en' + c.en)] = new Set(); scanElimSave(); selectClue();
  }
  // The scan ring is a type-4 scene "special": gfx 6841 = blue (d > 2R), 6842 = orange (R < d <=
  // 2R), 6843 = red (d <= R), giving the exact band for per-band elimination.
  async function scanRingBand() {
    if (!bridge() || !bridge().sceneEntities) return null;
    try {
      const d = JSON.parse((await bridge().sceneEntities(myPid(), 16)) || '{}');
      const sp = (d.specials || []).find(s => s.gfx >= 6841 && s.gfx <= 6843);
      if (sp) return sp.gfx === 6841 ? 'blue' : sp.gfx === 6842 ? 'orange' : 'red';
    } catch (e) {}
    return null;
  }
  let scanBandNote = '';
  function scanBandSetCap() {
    const cap = $('clueMapCap'); if (!cap || cap.textContent.indexOf('Scan') !== 0) return;
    const base = cap.textContent.replace(/\s*·\s*(ring:|exact dig tile).*$/, '');
    cap.textContent = base + (scanBandNote ? '  ·  ' + scanBandNote : '');
  }
  async function scanTick() {
    if (activeClueId < 0 || !bridge()) return;
    const c = CLUE_DATA.find(z => z.i === activeClueId);
    if (!c || c.a !== 'scan') return;
    const rec = scanSpotsFor(c); if (!rec || !rec.spots || !rec.spots.length) return;
    // PRIMARY (exact): the server places the scan proximity ring at the true dig tile via inbound
    // opcode 83, but only while the player is within orb range (<=11 paces, red pulse). op83 is a
    // generic scene-graphic opcode, so the coord counts only when it matches a candidate (<=2 tiles).
    try {
      if (bridge().scanSolution) {
        const sol = JSON.parse(bridge().scanSolution(myPid()) || '{}');
        if (sol && sol.ok && sol.x > 0 && sol.y > 0) {
          let bi = -1, bd = 1e9;
          rec.spots.forEach((s, i) => { const d = Math.max(Math.abs(s[0] - sol.x), Math.abs(s[1] - sol.y)); if (d < bd) { bd = d; bi = i; } });
          if (bi >= 0 && bd <= 2) {
            const elim = scanElimGet(rec.key || ('en' + c.en));
            let changed = false;
            rec.spots.forEach((s, i) => { if (i !== bi && !elim.has(i)) { elim.add(i); changed = true; } });
            scanBandNote = 'exact dig tile from orb (' + sol.x + ', ' + sol.y + ')';
            if (changed) { scanElimSave(); selectClue(); }
            scanBandSetCap();
            return;
          }
        }
      }
    } catch (e) {}
    // SECONDARY: some scans publish the EXACT dig tile to varc 1323 (packed (plane<<28)|(x<<14)|y,
    // same as compass). Bbox-gated so a stale compass varc from a previous clue can't hijack it.
    try {
      const raw = (bridge().compassTarget && bridge().compassTarget(myPid())) || '';
      const tp = raw.split(',').map(Number);
      if (tp.length >= 2 && tp[0] > 0 && tp[1] > 0) {
        let bx0 = 1e9, by0 = 1e9, bx1 = -1e9, by1 = -1e9;
        for (const s of rec.spots) { bx0 = Math.min(bx0, s[0]); bx1 = Math.max(bx1, s[0]); by0 = Math.min(by0, s[1]); by1 = Math.max(by1, s[1]); }
        if (tp[0] >= bx0 - 8 && tp[0] <= bx1 + 8 && tp[1] >= by0 - 8 && tp[1] <= by1 + 8) {
          let bi = -1, bd = 1e9;
          rec.spots.forEach((s, i) => { const d = Math.abs(s[0] - tp[0]) + Math.abs(s[1] - tp[1]); if (d < bd) { bd = d; bi = i; } });
          // EXACT match only (<=1 covers an off-by-one on the anchor tile): varc 1323 is primarily the
          // COMPASS target, so a wider radius lets a stale value snap to a candidate and fake a SOLVED.
          if (bi >= 0 && bd <= 1) {
            const elim = scanElimGet(rec.key || ('en' + c.en));
            let changed = false;
            rec.spots.forEach((s, i) => { if (i !== bi && !elim.has(i)) { elim.add(i); changed = true; } });
            scanBandNote = 'exact dig tile from clue (' + tp[0] + ', ' + tp[1] + ')';
            if (changed) { scanElimSave(); selectClue(); }
            scanBandSetCap();
            return;
          }
        }
      }
    } catch (e) {}
    if (rec.spots.length > COMPASS_FIELD_MIN) return;         // compass field: solved by the needle, not the scan orb
    const band = await scanRingBand();
    scanBandNote = band ? 'ring: ' + band : '';
    scanBandSetCap();
    // Detection model: the orb pulse bands
    // the HORIZONTAL Chebyshev distance to the dig spot, INDEPENDENT of floor. red => d<=R;
    // orange => R<d<=2R; blue => d>2R. Cross-floor spots are kept on band-match, not dropped
    // (conservative: the true spot always matches the observed band, so it is never eliminated).
    if (!band) return;
    const R = rec.r || scanLiveRange(); if (!R) return;
    const P = await scanPlayerTile(); if (!P) return;
    const elim = scanElimGet(rec.key || ('en' + c.en));
    let changed = false; const added = [];
    // The ring colour can trail the true position by ~1 movement tick, hence the TOL margin.
    const TOL = 2;
    rec.spots.forEach((s, i) => {
      if (elim.has(i)) return;
      const d = Math.max(Math.abs(s[0] - P.x), Math.abs(s[1] - P.y));   // horizontal paces (any floor)
      let drop = false;
      if (band === 'blue')        drop = d <= 2 * R - TOL;
      else if (band === 'orange') drop = d <= R - TOL || d > 2 * R + TOL;
      else if (band === 'red')    drop = d > R + TOL;
      if (drop) { elim.add(i); added.push(i); changed = true; }
    });
    // Never eliminate the LAST candidate: zeroing the set means the band reading was unreliable.
    if (elim.size >= rec.spots.length) { for (const i of added) elim.delete(i); changed = false; }
    if (changed) { scanElimSave(); selectClue(); scanBandSetCap(); }
  }
  // Map zoom: the inner div is sized to (stageWidth * zoom) and the stage scrolls, so the canvas
  // and the DOM markers scale and pan together.
  let clueMapZoom = 1, clueMapDrag = null, clueMapStageEl = null, clueMapZoomBound = false;
  function applyMapZoom() {
    const stage = clueMapStageEl || document.querySelector('.clue-map-stage'), inner = $('clueMapInner');
    if (!stage || !inner) return;
    const sz = Math.round((stage.clientWidth || 280) * clueMapZoom);
    inner.style.width = sz + 'px'; inner.style.height = sz + 'px';
  }
  function clueMapBindZoom(stage) {
    clueMapStageEl = stage;
    stage.addEventListener('wheel', e => {
      e.preventDefault();
      const old = clueMapZoom;
      clueMapZoom = Math.max(1, Math.min(6, clueMapZoom * (e.deltaY < 0 ? 1.18 : 1 / 1.18)));
      if (clueMapZoom === old) return;
      const rect = stage.getBoundingClientRect(), r = clueMapZoom / old;
      const px = e.clientX - rect.left + stage.scrollLeft, py = e.clientY - rect.top + stage.scrollTop;
      applyMapZoom();
      stage.scrollLeft = px * r - (e.clientX - rect.left); stage.scrollTop = py * r - (e.clientY - rect.top);
    }, { passive: false });
    stage.addEventListener('mousedown', e => { clueMapDrag = { x: e.clientX, y: e.clientY, sl: stage.scrollLeft, st: stage.scrollTop }; stage.classList.add('grabbing'); e.preventDefault(); });
    stage.addEventListener('contextmenu', e => { e.preventDefault(); scanReset(); });   // right-click = reset this scan's eliminated spots
    if (!clueMapZoomBound) {
      clueMapZoomBound = true;
      window.addEventListener('mousemove', e => { if (!clueMapDrag || !clueMapStageEl) return; clueMapStageEl.scrollLeft = clueMapDrag.sl - (e.clientX - clueMapDrag.x); clueMapStageEl.scrollTop = clueMapDrag.st - (e.clientY - clueMapDrag.y); });
      window.addEventListener('mouseup', () => { if (clueMapDrag) { clueMapDrag = null; if (clueMapStageEl) clueMapStageEl.classList.remove('grabbing'); } });
    }
  }
  // Scan map: every candidate spot (+ bbox) over terrain, else a self-scaled overview.
  // clueMapProj = {projX, projY, W, plane} lets the player marker place tiles without a redraw.
  function clueMapEsc(s) { return htmlEsc(s); }
  // Nearest named place to a tile (same plane preferred) -> {name, dist (chebyshev), dir, x, y}.
  function nearPlace(x, y, p) {
    let best = null, bd = Infinity, anyB = null, ad = Infinity;
    for (const d of MAP_LABELS) {
      const dist = Math.max(Math.abs(d.x - x), Math.abs(d.y - y));
      if (dist < ad) { ad = dist; anyB = d; }
      if ((d.p || 0) === (p || 0) && dist < bd) { bd = dist; best = d; }
    }
    if (!best) { best = anyB; bd = ad; }
    if (!best) return null;
    return { name: best.n, dist: bd, dir: nearDir(x - best.x, y - best.y), x: best.x, y: best.y };
  }
  function nearDir(dx, dy) {              // compass dir of the tile relative to the place (RS3: +y = north)
    let s = ''; if (dy > 4) s += 'N'; else if (dy < -4) s += 'S'; if (dx > 4) s += 'E'; else if (dx < -4) s += 'W';
    return s || 'at';
  }
  function nearLabel(x, y, p) {
    const n = nearPlace(x, y, p); if (!n) return '';
    return n.dist <= 6 ? ('at ' + n.name) : (n.dist + ' tiles ' + n.dir + ' of ' + n.name);
  }
  // Hover marks are repopulated in canvas-pixel space on every map draw; the cursor finds the
  // nearest. clueMapMarks = [{sx, sy, r, label(html)}].
  let clueMapMarks = [];
  function clueMapTipBind(cv) {
    if (!cv || cv._tipBound) return; cv._tipBound = true;
    cv.addEventListener('mousemove', e => {
      const tip = $('clueMapTip'); if (!tip || !clueMapProj) return;
      const rect = cv.getBoundingClientRect(); if (!rect.width) { tip.style.display = 'none'; return; }
      const W = clueMapProj.W, mx = (e.clientX - rect.left) / rect.width * W, my = (e.clientY - rect.top) / rect.height * W;
      let hit = null, hd = Infinity;
      for (const m of clueMapMarks) { const d = Math.hypot(m.sx - mx, m.sy - my); if (d <= (m.r || 6) + 7 && d < hd) { hd = d; hit = m; } }
      if (hit) {
        const mi = cv.parentElement, mr = mi.getBoundingClientRect();
        tip.innerHTML = hit.label; tip.style.display = 'block';
        // Clamp so the tooltip stays fully inside the map panel (flip left / pull up on overflow).
        let lx = e.clientX - mr.left + 12, ty = e.clientY - mr.top + 10;
        const tw = tip.offsetWidth, th = tip.offsetHeight;
        if (lx + tw > mr.width - 2) lx = e.clientX - mr.left - tw - 12;
        if (lx < 2) lx = 2;
        if (ty + th > mr.height - 2) ty = mr.height - th - 2;
        if (ty < 2) ty = 2;
        tip.style.left = lx + 'px';
        tip.style.top = ty + 'px';
      } else tip.style.display = 'none';
    });
    cv.addEventListener('mouseleave', () => { const tip = $('clueMapTip'); if (tip) tip.style.display = 'none'; });
  }
  // Curated world-map place labels, drawn as TEXT on the clue maps (dark-outlined name at the tile).
  const MAP_LABELS = [{"n":"Picatoris Fishing Colony","x":2335,"y":3681,"p":0},{"n":"Falconer","x":2377,"y":3598,"p":0},{"n":"Memorial to Guthix","x":2273,"y":3554,"p":0},{"n":"Eagles' Peak","x":2329,"y":3488,"p":0},{"n":"Poison Waste","x":2240,"y":3098,"p":0},{"n":"Tyras Camp","x":2188,"y":3145,"p":0},{"n":"Port Tyras","x":2154,"y":3122,"p":0},{"n":"Lletya","x":2339,"y":3172,"p":0},{"n":"Isafdar","x":2240,"y":3193,"p":0},{"n":"Elf Camp","x":2197,"y":3251,"p":0},{"n":"Arandar","x":2345,"y":3292,"p":0},{"n":"Observatory","x":2440,"y":3163,"p":0},{"n":"Battlefield","x":2517,"y":3243,"p":0},{"n":"West Ardougne","x":2523,"y":3305,"p":0},{"n":"Underground Pass Entrance","x":2435,"y":3315,"p":0},{"n":"Combat Training Camp","x":2518,"y":3370,"p":0},{"n":"Gnome Agility Training Area","x":2481,"y":3426,"p":0},{"n":"Gnome Ball Field","x":2397,"y":3489,"p":0},{"n":"Grand Tree","x":2465,"y":3494,"p":0},{"n":"Baxtorian Falls","x":2511,"y":3465,"p":0},{"n":"Barbarian outpost","x":2542,"y":3564,"p":0},{"n":"Warforge Dig Site","x":2410,"y":2838,"p":0},{"n":"Oo'glog","x":2564,"y":2849,"p":0},{"n":"Feldip Hills","x":2559,"y":2978,"p":0},{"n":"Gu'Tanoth","x":2522,"y":3038,"p":0},{"n":"Jiggig","x":2467,"y":3046,"p":0},{"n":"Yanile","x":2553,"y":3093,"p":0},{"n":"Wizards' Guild","x":2590,"y":3087,"p":0},{"n":"Fight Arena","x":2593,"y":3164,"p":0},{"n":"Tree Gnome Village","x":2529,"y":3169,"p":0},{"n":"Ardougne Monastery","x":2607,"y":3213,"p":0},{"n":"Port Khazard","x":2653,"y":3162,"p":0},{"n":"Tower of Life","x":2649,"y":3219,"p":0},{"n":"Clocktower","x":2570,"y":3242,"p":0},{"n":"East Ardougne","x":2615,"y":3306,"p":0},{"n":"Witchhaven","x":2719,"y":3285,"p":0},{"n":"Manor Farm","x":2655,"y":3357,"p":0},{"n":"Legends' Guild","x":2729,"y":3370,"p":0},{"n":"Catherby","x":2802,"y":3444,"p":0},{"n":"Sorcerer's Tower","x":2703,"y":3405,"p":0},{"n":"Stormguard Citadel Dig Site","x":2678,"y":3401,"p":0},{"n":"Ranging Guild","x":2669,"y":3430,"p":0},{"n":"Mcgrubor's Wood","x":2645,"y":3482,"p":0},{"n":"Seers Village","x":2705,"y":3483,"p":0},{"n":"Camelot","x":2758,"y":3502,"p":0},{"n":"Sinclair Mansion","x":2742,"y":3568,"p":0},{"n":"Golden Apple Tree","x":2765,"y":3609,"p":0},{"n":"Rellekka","x":2649,"y":3676,"p":0},{"n":"Keldagrim Entrance","x":2732,"y":3712,"p":0},{"n":"Rellekka Hunter Area","x":2721,"y":3785,"p":0},{"n":"Waterbirth Island","x":2534,"y":3741,"p":0},{"n":"Etceteria","x":2607,"y":3875,"p":0},{"n":"Miscellania","x":2529,"y":3867,"p":0},{"n":"Jatizso","x":2402,"y":3805,"p":0},{"n":"Neitiznot","x":2329,"y":3803,"p":0},{"n":"Lighthouse","x":2508,"y":3635,"p":0},{"n":"Void Knights' Outpost","x":2653,"y":2656,"p":0},{"n":"Ape Atoll","x":2749,"y":2749,"p":0},{"n":"Crash Island","x":2915,"y":2720,"p":0},{"n":"Kharazi Jungle","x":2857,"y":2920,"p":0},{"n":"Shilo Village","x":2848,"y":2984,"p":0},{"n":"Tai Bwo Wannai","x":2792,"y":3067,"p":0},{"n":"Karamja","x":2864,"y":3059,"p":0},{"n":"Musa Point","x":2908,"y":3163,"p":0},{"n":"Tzhaar City","x":2844,"y":3173,"p":0},{"n":"Brimhaven","x":2768,"y":3180,"p":0},{"n":"Crandor","x":2837,"y":3272,"p":0},{"n":"Fishing Platform","x":2774,"y":3283,"p":0},{"n":"Entrana","x":2836,"y":3361,"p":0},{"n":"Lunar Isle","x":2109,"y":3907,"p":0},{"n":"Taverley","x":2906,"y":3463,"p":0},{"n":"Heroes guild","x":2907,"y":3514,"p":0},{"n":"Burthorpe","x":2856,"y":3542,"p":0},{"n":"Warriors' Guild","x":2857,"y":3541,"p":0},{"n":"Dark Wizards' Tower","x":2907,"y":3341,"p":0},{"n":"White Wolf Mountain","x":2831,"y":3502,"p":0},{"n":"Death Plateau","x":2861,"y":3593,"p":0},{"n":"Trollheim","x":2888,"y":3673,"p":0},{"n":"Troll Stronghold","x":2830,"y":3675,"p":0},{"n":"God Wars Dungeon","x":2916,"y":3742,"p":0},{"n":"Wilderness Agility Training Area","x":2997,"y":3950,"p":0},{"n":"Pirates' Hideout","x":3041,"y":3953,"p":0},{"n":"Mage Arena","x":3104,"y":3934,"p":0},{"n":"Deserted Keep","x":3154,"y":3933,"p":0},{"n":"Lava Maze","x":3076,"y":3857,"p":0},{"n":"Red Dragon Isle","x":3199,"y":3830,"p":0},{"n":"The Forgotten Cemetary","x":2976,"y":3751,"p":0},{"n":"Wilderness Crator","x":3135,"y":3727,"p":0},{"n":"Scorpion Pit","x":3233,"y":3945,"p":0},{"n":"Chaos Elemental (boss)","x":3270,"y":3955,"p":0},{"n":"Rogues' Castle","x":3287,"y":3932,"p":0},{"n":"Volcano (Wilderness)","x":3369,"y":3950,"p":0},{"n":"Dragonkin Laboratory","x":3368,"y":3888,"p":0},{"n":"Demonic Ruins","x":3288,"y":3887,"p":0},{"n":"Ruins East","x":3228,"y":3737,"p":0},{"n":"Ruins West","x":2974,"y":3695,"p":0},{"n":"Bandit Camp","x":3039,"y":3689,"p":0},{"n":"Dark Warriors' Fortress","x":3029,"y":3633,"p":0},{"n":"Black Knights' Fortress","x":3019,"y":3558,"p":0},{"n":"Abyss Entrance","x":3100,"y":3554,"p":0},{"n":"Graveyard of Shadows","x":3225,"y":3684,"p":0},{"n":"Wilderness Chaos Alter","x":3240,"y":3610,"p":0},{"n":"Daemonheim","x":3450,"y":3711,"p":0},{"n":"Fort Forinthry","x":3306,"y":3554,"p":0},{"n":"Goblin Village","x":2956,"y":3505,"p":0},{"n":"Captured Temple","x":2950,"y":3476,"p":0},{"n":"Falador","x":2965,"y":3382,"p":0},{"n":"White Knights' Castle","x":2967,"y":3341,"p":0},{"n":"Artisans' Workshop","x":3045,"y":3340,"p":0},{"n":"Party Room","x":3046,"y":3377,"p":0},{"n":"Ice Mountain","x":3008,"y":3485,"p":0},{"n":"Dwarven Mine","x":3009,"y":3451,"p":0},{"n":"Invention Guild","x":2995,"y":3438,"p":0},{"n":"Edgeville Monastery","x":3052,"y":3490,"p":0},{"n":"Barbarian Village","x":3079,"y":3421,"p":0},{"n":"Edgeville","x":3088,"y":3491,"p":0},{"n":"Crafting Guild","x":2933,"y":3286,"p":0},{"n":"Remmington","x":2957,"y":3208,"p":0},{"n":"White Knight Camp","x":2995,"y":3237,"p":0},{"n":"Port Sarim","x":3027,"y":3222,"p":0},{"n":"Mudskipper Point","x":2995,"y":3117,"p":0},{"n":"Asgarnian Ice Dungeon Entrance","x":3008,"y":3149,"p":0},{"n":"Falador Farm","x":3034,"y":3287,"p":0},{"n":"Draynor Manor","x":3109,"y":3348,"p":0},{"n":"Draynor Village","x":3104,"y":3263,"p":0},{"n":"Wizards' Tower","x":3102,"y":3157,"p":0},{"n":"Lumbridge Swamp","x":3196,"y":3171,"p":0},{"n":"Lumbridge","x":3223,"y":3240,"p":0},{"n":"Champions' Guild","x":3192,"y":3358,"p":0},{"n":"Varrock","x":3213,"y":3429,"p":0},{"n":"Cooks' Guild","x":3143,"y":3448,"p":0},{"n":"Grand Exchange","x":3163,"y":3493,"p":0},{"n":"Infernal Source Dig Site","x":3261,"y":3503,"p":0},{"n":"Exam Centre","x":3361,"y":3347,"p":0},{"n":"Archeology Guild","x":3323,"y":3378,"p":0},{"n":"Varrock Digsite","x":3358,"y":3428,"p":0},{"n":"Silvarea","x":3364,"y":3484,"p":0},{"n":"Temple","x":3411,"y":3485,"p":0},{"n":"God Wars Dungeon 3 Entrance","x":3328,"y":3449,"p":0},{"n":"Mage Training Arena","x":3363,"y":3309,"p":0},{"n":"Garden of Kharid","x":3314,"y":3302,"p":0},{"n":"Het's Oasis","x":3364,"y":3234,"p":0},{"n":"Kharid-et Dig Site","x":3357,"y":3194,"p":0},{"n":"Citharede Abbey","x":3422,"y":3164,"p":0},{"n":"Al Kharid","x":3290,"y":3169,"p":0},{"n":"Shantay Pass","x":3304,"y":3124,"p":0},{"n":"Kalphite Hive","x":3220,"y":3115,"p":0},{"n":"Bedabin Camp","x":3169,"y":3039,"p":0},{"n":"Desert Mining Camp","x":3289,"y":3026,"p":0},{"n":"Bandit Camp","x":3175,"y":2981,"p":0},{"n":"Pollinivneach","x":3358,"y":2970,"p":0},{"n":"Quarry","x":3172,"y":2911,"p":0},{"n":"Pyramid","x":3233,"y":2898,"p":0},{"n":"Goebie Camp","x":3090,"y":2863,"p":0},{"n":"Exiled Kalphite Hive","x":3238,"y":2858,"p":0},{"n":"Workers District","x":3156,"y":2797,"p":0},{"n":"Merchant District","x":3228,"y":2782,"p":0},{"n":"Imperial District","x":3098,"y":2688,"p":0},{"n":"Port District","x":3152,"y":2641,"p":0},{"n":"Menaphos","x":3226,"y":2728,"p":0},{"n":"Sophanem","x":3299,"y":2785,"p":0},{"n":"Agility Pyramid","x":3364,"y":2840,"p":0},{"n":"God Wars Dungeon 2","x":3378,"y":2882,"p":0},{"n":"Nardah","x":3426,"y":2914,"p":0},{"n":"Uzer","x":3478,"y":3091,"p":0},{"n":"Mausoleum","x":3502,"y":3573,"p":0},{"n":"Fenkenstrain's Castle","x":3548,"y":3552,"p":0},{"n":"Slayer Tower","x":3423,"y":3538,"p":0},{"n":"Canifis","x":3492,"y":3488,"p":0},{"n":"Haunted Woods","x":3564,"y":3493,"p":0},{"n":"Ectofuntus","x":3661,"y":3519,"p":0},{"n":"Port Phasmatys","x":3667,"y":3487,"p":0},{"n":"Mort Myre Swamp","x":3436,"y":3400,"p":0},{"n":"Meiyerditch","x":3616,"y":3264,"p":0},{"n":"Barrows","x":3565,"y":3288,"p":0},{"n":"Mort'ton","x":3489,"y":3289,"p":0},{"n":"Burgh De Rott","x":3501,"y":3224,"p":0},{"n":"Abandoned Mine","x":3447,"y":3234,"p":0},{"n":"EverLight Dig Site","x":3695,"y":3208,"p":0},{"n":"Harmony","x":3797,"y":2858,"p":0},{"n":"Mos Le'Harmless","x":3711,"y":3027,"p":0},{"n":"Dragontooth Island","x":3803,"y":3546,"p":0},{"n":"Wendlewick","x":3481,"y":1557,"p":0},{"n":"Amberfell","x":3709,"y":1558,"p":0},{"n":"Anachronia","x":5432,"y":2339,"p":0},{"n":"Observation Output","x":5594,"y":2528,"p":0},{"n":"Crypt of Varanus","x":5320,"y":2414,"p":0},{"n":"Anachronia Dinosaur Farm","x":5198,"y":2373,"p":0},{"n":"Tuai Leit","x":1813,"y":11929,"p":0},{"n":"Archaeology Campus","x":3359,"y":3377,"p":0},{"n":"Death's Office","x":414,"y":674,"p":0},{"n":"Trahaearn","x":2231,"y":3311,"p":0},{"n":"Ardougne Zoo","x":2614,"y":3272,"p":0},{"n":"City of Um","x":1108,"y":1777,"p":1},{"n":"Distilleries","x":3783,"y":2999,"p":0},{"n":"Melzar's Maze","x":2937,"y":3255,"p":0},{"n":"Rimmington","x":2955,"y":3222,"p":0},{"n":"Custom's Office","x":2966,"y":3194,"p":0},{"n":"Jail","x":3125,"y":3243,"p":0},{"n":"Market","x":3081,"y":3250,"p":0},{"n":"Park","x":3004,"y":3381,"p":0},{"n":"Um Ritual Site","x":1037,"y":1774,"p":1},{"n":"The Heart","x":3200,"y":6970,"p":1},{"n":"Zaros's Bastion","x":3129,"y":6911,"p":1},{"n":"Zamorak's Rampart","x":3138,"y":7039,"p":1},{"n":"Sliske's Necropolis","x":3270,"y":7045,"p":1},{"n":"Seren's Encampment","x":3256,"y":6912,"p":1},{"n":"Skinweaver","x":4643,"y":5382,"p":0},{"n":"Commander Akhomet","x":3166,"y":2729,"p":0},{"n":"Grand Vizier Ehsan","x":3197,"y":2769,"p":0},{"n":"Admiral Wadud","x":3178,"y":2648,"p":0},{"n":"Outpost","x":2436,"y":3347,"p":0},{"n":"Tree Gnome Stronghold","x":2440,"y":3468,"p":0},{"n":"Otto's Grotto","x":2502,"y":3488,"p":0},{"n":"Swamp","x":2419,"y":3512,"p":0},{"n":"Barbarian Assault","x":2521,"y":3571,"p":0},{"n":"Fremennik Province","x":2670,"y":3629,"p":0},{"n":"Courthouse","x":2736,"y":3468,"p":0},{"n":"Flax","x":2742,"y":3443,"p":0},{"n":"Beehives","x":2759,"y":3443,"p":0},{"n":"Necromancer","x":2669,"y":3240,"p":0},{"n":"Trawler","x":2687,"y":3168,"p":0},{"n":"Castle Wars","x":2442,"y":3090,"p":0},{"n":"South Feldip Hills","x":2469,"y":2852,"p":0},{"n":"Tirannwn","x":2240,"y":3219,"p":0},{"n":"Wilderness Crater","x":3136,"y":3712,"p":0},{"n":"Frozen Waste Plateau","x":2964,"y":3925,"p":0},{"n":"Trollweiss Mountain","x":2782,"y":3859,"p":0},{"n":"Ice Path","x":2855,"y":3809,"p":0},{"n":"Boneyard","x":3272,"y":3677,"p":0},{"n":"River Lum","x":3168,"y":3351,"p":0},{"n":"Aquanites","x":2724,"y":9974,"p":0},{"n":"Kurask","x":2699,"y":9998,"p":0},{"n":"Turoth","x":2723,"y":10004,"p":0},{"n":"Jellies","x":2704,"y":10027,"p":0},{"n":"Basilisks","x":2742,"y":10010,"p":0},{"n":"Pyrefiends","x":2761,"y":10004,"p":0},{"n":"Cockatrice","x":2791,"y":10036,"p":0},{"n":"Rock slugs","x":2800,"y":10017,"p":0},{"n":"Cave crawlers","x":2790,"y":9997,"p":0},{"n":"Giant frogs","x":3226,"y":9547,"p":0},{"n":"Swamp cave","x":3192,"y":9569,"p":0},{"n":"Keep Le Faye","x":2770,"y":3400,"p":0},{"n":"Ewan's Grove","x":2916,"y":3483,"p":0},{"n":"Astram Farm","x":2885,"y":3487,"p":0},{"n":"Rogue's Den","x":2891,"y":3443,"p":0},{"n":"Fight Pit","x":4571,"y":5091,"p":0},{"n":"Fight Cave","x":4611,"y":5130,"p":0},{"n":"Library","x":4629,"y":5170,"p":0},{"n":"Main Plaza","x":4671,"y":5156,"p":0},{"n":"Birthing Pool","x":4717,"y":5165,"p":0},{"n":"Fight Kiln","x":4743,"y":5170,"p":0},{"n":"Celestial Dragon Dungeon","x":2268,"y":5980,"p":0},{"n":"Puro-Puro","x":2427,"y":4445,"p":0},{"n":"Otherworldly beings","x":2384,"y":4424,"p":0},{"n":"Beware of the mushrooms","x":2418,"y":4377,"p":0},{"n":"The market","x":2483,"y":4448,"p":0},{"n":"Throne room","x":2446,"y":4426,"p":0}];
  // Draw MAP_LABELS in the view tile-box as outlined text, capped and nearest-to-centre first.
  function clueMapDrawLabels(cx, proj, plane, vx0, vy0, vx1, vy1) {
    const ccx = (vx0 + vx1) / 2, ccy = (vy0 + vy1) / 2;
    const inview = MAP_LABELS.filter(d => (d.p || 0) === plane && d.x >= vx0 - 1 && d.x <= vx1 + 1 && d.y >= vy0 - 1 && d.y <= vy1 + 1);
    inview.sort((a, b) => (Math.abs(a.x - ccx) + Math.abs(a.y - ccy)) - (Math.abs(b.x - ccx) + Math.abs(b.y - ccy)));
    cx.save();
    cx.font = '700 11px system-ui, "Segoe UI", sans-serif'; cx.textAlign = 'center'; cx.textBaseline = 'middle';
    const placed = [];
    for (const d of inview.slice(0, 24)) {
      const sx = proj.projX(d.x), sy = proj.projY(d.y);
      if (sx < -proj.W || sx > proj.W * 2 || sy < -proj.W || sy > proj.W * 2) continue;
      const tw = Math.ceil(cx.measureText(d.n).width), bw = tw + 8, bh = 15;
      // Clamp the chip fully inside the canvas so edge places pin to the border, not clip off.
      const x0 = Math.max(2, Math.min(sx - bw / 2, proj.W - bw - 2));
      const y0 = Math.max(2, Math.min(sy - bh / 2, proj.W - bh - 2));
      if (placed.some(p => x0 < p.x + p.w + 2 && x0 + bw + 2 > p.x && y0 < p.y + p.h + 2 && y0 + bh + 2 > p.y)) continue;   // would overlap a closer label -> skip it
      placed.push({ x: x0, y: y0, w: bw, h: bh });
      const lx = x0 + bw / 2, ly = y0 + bh / 2;
      cx.fillStyle = 'rgba(7,9,14,0.82)'; cx.fillRect(x0, y0, bw, bh);
      cx.lineWidth = 1; cx.strokeStyle = 'rgba(120,200,255,0.35)'; cx.strokeRect(x0 + 0.5, y0 + 0.5, bw - 1, bh - 1);
      cx.lineWidth = 2.5; cx.strokeStyle = 'rgba(0,0,0,0.9)'; cx.strokeText(d.n, lx, ly);
      cx.fillStyle = '#ffffff'; cx.fillText(d.n, lx, ly);
      clueMapMarks.push({ sx: lx, sy: ly, r: 7, label: '<b>' + clueMapEsc(d.n) + '</b><br>' + d.x + ', ' + d.y });
    }
    cx.restore();
  }
  let clueMapProj = null;
  // Draw-sequence token: every map draw bumps this on entry and re-checks after each await,
  // bailing if a NEWER draw started; otherwise a slow earlier draw can clobber the canvas.
  let clueMapDrawSeq = 0;
  let scanPlaneSel = null, scanPlaneSelFor = -1;   // manual floor pick for multi-floor scans (null = auto); cleared in-region or on clue change
  // Live local-player dot on the drawn clue map, only when on the map's plane and inside its bounds.
  function clueMapPlayerDraw(P) {
    const el = $('clueMapPlayer'), rg = $('clueMapRange'), pr = clueMapProj;
    if (!el) return;
    const hide = () => { el.style.display = 'none'; if (rg) rg.style.display = 'none'; };
    if (!P || !pr || (P.p || 0) !== pr.plane) { hide(); return; }
    const px = pr.projX(P.x), py = pr.projY(P.y);
    if (px < 0 || px > pr.W || py < 0 || py > pr.W) { hide(); return; }
    el.style.display = 'block'; el.style.left = (px / pr.W * 100) + '%'; el.style.top = (py / pr.W * 100) + '%';
    // Scan bands measure horizontal Chebyshev distance, so the orb range draws as a square.
    if (rg) {
      if (!pr.scanR) { rg.style.display = 'none'; }
      else {
        const ts = pr.projX(P.x + 1) - px;                     // pixels per tile on this projection
        const half = (pr.scanR + 0.5) * ts;                    // tile centres +-R, plus the half tile each side
        rg.style.display = 'block';
        rg.style.left = ((px - half) / pr.W * 100) + '%'; rg.style.top = ((py - half) / pr.W * 100) + '%';
        rg.style.width = (2 * half / pr.W * 100) + '%'; rg.style.height = (2 * half / pr.W * 100) + '%';
      }
    }
  }
  // Keep the player marker live on whatever clue map is open.
  setInterval(async function () {
    const wrap = $('clueMapWrap');
    if (!wrap || wrap.style.display === 'none' || !clueMapProj) return;
    try {
      const P = await scanPlayerTile();
      clueMapPlayerDraw(P);
      // Live floor-snap: walk to a different floor that a multi-floor scan area covers -> redraw on your floor.
      if (P && (P.p || 0) !== clueMapProj.plane && activeClueId >= 0) {
        const ac = CLUE_DATA.find(z => z.i === activeClueId);
        if (ac && ac.a === 'scan') {
          const r = scanSpotsFor(ac), Rr = ((r && r.r) || 14) + scanRangeBonus();
          if (r && r.spots && r.spots.some(s => (s[2] || 0) === (P.p || 0) && Math.abs(s[0] - P.x) <= 2 * Rr && Math.abs(s[1] - P.y) <= 2 * Rr)) {
            scanPlaneSel = null; selectClue();
          }
        }
      }
    } catch (e) {}
    return;
  }, 450);
  // Meerkats familiar = +5 scan range: varp 1831 holds the summoned pouch item id; cached so
  // scanRangeBonus() reads the flag without await.
  let g_scanMeerkats = false, g_scanMeerkatsPouch = -1;
  async function refreshMeerkats() {
    try {
      const vp = JSON.parse(await bridge().varps(myPid(), '1831'));
      let pouch = (vp && vp['1831']) | 0; if (pouch <= 0 || pouch >= 0x7FFFFFFF) pouch = 0;
      if (pouch === g_scanMeerkatsPouch) return g_scanMeerkats;
      g_scanMeerkatsPouch = pouch;
      const was = g_scanMeerkats;
      if (!pouch) { g_scanMeerkats = false; }
      else {
        let name = (typeof FAM_NAMES !== 'undefined') ? FAM_NAMES[pouch] : undefined;
        if (name === undefined) { try { name = (JSON.parse(await bridge().itemInfo(pouch)).name) || ''; } catch (e) { name = ''; } if (typeof FAM_NAMES !== 'undefined') FAM_NAMES[pouch] = name; }
        g_scanMeerkats = /meerkat/i.test(name || '');
      }
      if (g_scanMeerkats !== was) { clueFocusSig = ''; try { clueRenderFocus(); } catch (e) {} }
      return g_scanMeerkats;
    } catch (e) { return g_scanMeerkats; }
  }
  function scanRangeBonus() { return g_scanMeerkats ? 5 : 0; }
  // Walkability (nomove) overlay (after mejrs): mapWindow `nomove` = one flag byte per window
  // tile (0x10 = full block, 0x01/02/04/08 = N/S/E/W wall edge), row-major wx*WT+wy, north-up.
  const clueWalk = false;   // draw code kept; no toggle UI
  function clueDrawNomove(ctx, meta) {
    if (!clueWalk || !ctx || !meta || !meta.nomove || !meta.wt || !meta.t) return;
    let b; try { const s = atob(meta.nomove); b = new Uint8Array(s.length); for (let i = 0; i < s.length; i++) b[i] = s.charCodeAt(i); } catch (e) { return; }
    const WT = meta.wt, TS = meta.t;
    ctx.save();
    ctx.fillStyle = 'rgba(214,64,64,0.38)';
    for (let wx = 0; wx < WT; wx++) for (let wy = 0; wy < WT; wy++) { if (b[wx * WT + wy] & 0x10) ctx.fillRect(wx * TS, ((WT - 1) - wy) * TS, TS, TS); }
    ctx.strokeStyle = 'rgba(255,86,86,0.92)'; ctx.lineWidth = Math.max(1, TS * 0.2);
    for (let wx = 0; wx < WT; wx++) for (let wy = 0; wy < WT; wy++) {
      const f = b[wx * WT + wy]; if (!(f & 0x0f)) continue;
      const px = wx * TS, py = ((WT - 1) - wy) * TS, e = TS - 0.5;
      ctx.beginPath();
      if (f & 0x01) { ctx.moveTo(px, py + 0.5); ctx.lineTo(px + TS, py + 0.5); }      // N (north = +Y = top)
      if (f & 0x02) { ctx.moveTo(px, py + e);   ctx.lineTo(px + TS, py + e); }        // S (bottom)
      if (f & 0x04) { ctx.moveTo(px + e, py);   ctx.lineTo(px + e, py + TS); }        // E (east = +X = right)
      if (f & 0x08) { ctx.moveTo(px + 0.5, py); ctx.lineTo(px + 0.5, py + TS); }      // W (left)
      ctx.stroke();
    }
    ctx.restore();
  }
  // Teleport DESTINATION markers (lodestones, after mejrs). meta.cx/cy + HALF map a world tile
  // into the panel; the nearest lodestone to the window centre draws brighter.
  const clueTele = false;   // draw code kept; no toggle UI
  function clueDrawTeleports(ctx, meta) {
    if (!clueTele || !ctx || !meta || meta.cx == null || !meta.wt || !meta.t) return;
    const WT = meta.wt, TS = meta.t, HALF = meta.h, x0 = meta.cx - HALF, y0 = meta.cy - HALF;
    let near = null, nd = 1e9;
    for (const l of LODESTONES) { const d = Math.abs(l.x - meta.cx) + Math.abs(l.y - meta.cy); if (d < nd) { nd = d; near = l; } }
    ctx.save();
    ctx.font = Math.max(9, TS) + 'px sans-serif'; ctx.textAlign = 'center'; ctx.textBaseline = 'bottom';
    for (const l of LODESTONES) {
      const wtx = l.x - x0, wty = l.y - y0;
      if (wtx < 0 || wtx >= WT || wty < 0 || wty >= WT) continue;
      const px = wtx * TS + TS / 2, py = (WT - wty - 1) * TS + TS / 2, r = Math.max(5, TS * 0.7), hot = (l === near);
      ctx.beginPath(); ctx.moveTo(px, py - r); ctx.lineTo(px + r, py); ctx.lineTo(px, py + r); ctx.lineTo(px - r, py); ctx.closePath();
      ctx.fillStyle = hot ? 'rgba(140,110,255,0.95)' : 'rgba(120,90,255,0.75)'; ctx.fill();
      ctx.lineWidth = hot ? 2 : 1; ctx.strokeStyle = '#fff'; ctx.stroke();
      ctx.fillStyle = '#fff'; ctx.fillText(l.n, px, py - r - 1);
    }
    ctx.restore();
  }
  // Objects (loc) overlay: scenery footprints from the mapWindow `objs` field
  // (10 bytes/loc: wtx u16, wty u16, dx u8, dy u8, id u32), drawn as outlines.
  const clueObjs = false;   // draw code kept; no toggle UI
  function clueDrawObjects(ctx, meta) {
    if (!clueObjs || !ctx || !meta || !meta.objs || !meta.wt || !meta.t) return;
    let b; try { const s = atob(meta.objs); b = new Uint8Array(s.length); for (let i = 0; i < s.length; i++) b[i] = s.charCodeAt(i); } catch (e) { return; }
    const WT = meta.wt, TS = meta.t;
    ctx.save();
    ctx.strokeStyle = 'rgba(90,200,255,0.85)'; ctx.lineWidth = Math.max(1, TS * 0.12);
    for (let i = 0; i + 10 <= b.length; i += 10) {
      const wtx = b[i] | (b[i + 1] << 8), wty = b[i + 2] | (b[i + 3] << 8), dx = b[i + 4] || 1, dy = b[i + 5] || 1;
      ctx.strokeRect(wtx * TS + 0.5, (WT - wty - dy) * TS + 0.5, dx * TS - 1, dy * TS - 1);   // north-up footprint box
    }
    ctx.restore();
  }
  // Place-label overlay: clueMapDrawLabels with a tile->pixel projection from the window meta.
  function clueDrawLabelsWindow(ctx, meta) {
    if (!ctx || !meta || meta.cx == null || !meta.wt || !meta.t) return;
    const WT = meta.wt, TS = meta.t, x0 = meta.cx - meta.h, y0 = meta.cy - meta.h;
    const proj = { W: meta.w, projX: x => (x - x0) * TS + TS / 2, projY: y => (WT - (y - y0) - 1) * TS + TS / 2 };
    clueMapDrawLabels(ctx, proj, meta.p | 0, x0, y0, x0 + WT, y0 + WT);
  }
  // Nearest UNLOCKED lodestone inside a map window, positioned on the shared marker element.
  // Both map renderers use this; returns the caption suffix (empty when none is in view).
  // Non-lodestone teleport destinations drawn on the clue maps beside the nearest lodestone.
  // kb = the in-game keybind; item = optional item id for the marker icon (0 = plain dot).
  // req is checked before the marker shows: quest = quest id that must be complete,
  // skill/level = minimum live level, vb/vbVal = a varbit that must equal vbVal.
  // Spellbook lives in varbit 0 (varp 4 bits 0-1): 0 standard, 1 ancient, 2 lunar.
  const SPELLBOOK_VB = 0, SPELLBOOK_LUNAR = 2;
  const MAP_TELEPORTS = [
    // Skills necklace charge variants all share one icon: 11105 (4), 11107 (3), 11109 (2), 11111 (1).
    { n: "Fishing Guild", src: "Skills necklace", x: 2615, y: 3385, p: 0, kb: "1", item: 11105 },
    // Struct 14857: sprite from spell_prayer_ability_sprite, level from spell_prayer_ability_req_value,
    // tile decoded from teleport_destination_location 42814775 = (x << 14) | y.
    { n: "Fishing Guild", src: "Lunar spellbook", x: 2613, y: 3383, p: 0, kb: "", item: 0, sp: 14414,
      req: { skill: 6, level: 85, vb: SPELLBOOK_VB, vbVal: SPELLBOOK_LUNAR } }
  ];
  // Requirement gate. skill/level compares the live level (skill 6 = Magic); vb/vbVal requires a
  // varbit to equal a value. Values are prefetched into teleVbCache by clueMapTelePrefetch.
  let teleVbCache = {};
  async function clueMapTelePrefetch() {
    const ids = [];
    for (const T of MAP_TELEPORTS) if (T.req && T.req.vb != null && ids.indexOf(T.req.vb) < 0) ids.push(T.req.vb);
    if (!ids.length) return;
    try { teleVbCache = (await readVarbitValues(ids)) || {}; } catch (e) { teleVbCache = {}; }
  }
  function teleReqMet(T) {
    const r = T.req;
    if (!r) return true;
    if (r.skill != null && r.level != null) {
      const lvl = questSkillLevels();
      if ((lvl(r.skill) | 0) < r.level) return false;
    }
    if (r.vb != null && (teleVbCache[r.vb] | 0) !== r.vbVal) return false;
    return true;
  }
  // Why a gated teleport is unavailable right now ('' = available). Names the actual failing
  // gate: the spellbook varbit means "wrong spellbook", not a locked unlock.
  function teleReqWhy(T) {
    const r = T.req;
    if (!r) return '';
    const why = [];
    if (r.skill != null && r.level != null) {
      const lvl = questSkillLevels();
      if ((lvl(r.skill) | 0) < r.level) why.push('needs ' + r.level + ' ' + (SKILL_NAMES[r.skill] || ('skill ' + r.skill)));
    }
    if (r.vb != null && (teleVbCache[r.vb] | 0) !== r.vbVal)
      why.push(r.vb === SPELLBOOK_VB ? 'wrong spellbook' : 'not unlocked');
    return why.join(', ');
  }
  async function clueMapLodeDraw(vx0, vy0, vx1, vy1, ctx0, cty0, proj) {
    const el = $('clueMapLode');
    if (!el) return '';
    let lvp = {};
    if (bridge() && bridge().varps) {
      try { lvp = JSON.parse(await bridge().varps(myPid(), LODE_VARPS.join(','))) || {}; } catch (e) {}
    }
    const unlocked = (l) => {
      const raw = (lvp[l.vp] || 0) >>> 0, w = l.hi - l.lo + 1;
      const m = w >= 32 ? 0xffffffff : ((1 << w) - 1);
      return ((raw >>> l.lo) & m) >= l.th;
    };
    let best = null, bestD = Infinity;
    for (const L of LODESTONES) {
      if (!L.x || !unlocked(L)) continue;
      if (L.x < vx0 || L.x > vx1 || L.y < vy0 || L.y > vy1) continue;
      const d = Math.max(Math.abs(L.x - ctx0), Math.abs(L.y - cty0));
      if (d < bestD) { bestD = d; best = L; }
    }
    if (!best) { el.style.display = 'none'; return ''; }
    el.style.display = '';
    el.style.left = (proj.projX(best.x) / proj.W * 100) + '%';
    el.style.top = (proj.projY(best.y) / proj.W * 100) + '%';
    const icon = el.querySelector('.cml-icon'); if (icon) loadSpriteIcon(icon, best.sp);
    const kbEl = el.querySelector('.cml-kb'); if (kbEl) kbEl.textContent = best.kb || '';
    el.title = best.n + ' lodestone' + (best.kb ? ' (' + best.kb + ')' : '');
    const dir = (best.y > cty0 ? 'N' : best.y < cty0 ? 'S' : '') + (best.x > ctx0 ? 'E' : best.x < ctx0 ? 'W' : '');
    return '  \u00b7  lode: ' + best.n + (best.kb ? ' [' + best.kb + ']' : '') + ' (' + bestD + (dir ? ' ' + dir : '') + ')';
  }
  // Nearest non-lodestone teleport in the same window, on its own marker element.
  async function clueMapTeleDraw(vx0, vy0, vx1, vy1, ctx0, cty0, proj, plane) {
    const el = $('clueMapTele');
    if (!el) return '';
    await clueMapTelePrefetch();
    const shown = [];
    for (const T of MAP_TELEPORTS) {
      if ((T.p || 0) !== (plane || 0)) continue;
      if (T.x < vx0 || T.x > vx1 || T.y < vy0 || T.y > vy1) continue;
      shown.push(T);   // unmet gates still show (faded, reason in the tooltip), same as the World Map
    }
    if (!shown.length) { el.style.display = 'none'; el.innerHTML = ''; return ''; }
    // Nearest first: it keeps its true tile and the others are nudged around it. The DOM
    // order is reversed at append time so the nearest still paints on top.
    shown.sort((a, b) => Math.max(Math.abs(a.x - ctx0), Math.abs(a.y - cty0))
                       - Math.max(Math.abs(b.x - ctx0), Math.abs(b.y - cty0)));
    el.style.display = '';
    el.innerHTML = '';
    // Teleports closer together than a marker's own footprint are grouped into one box, each
    // keeping its own icon, keybind and tooltip. Anchored at the group's centre so no single
    // member is shown away from its tile by more than the group's own span.
    const MIN_PCT = 9;
    const pt = shown.map(T => ({ T: T, x: proj.projX(T.x) / proj.W * 100, y: proj.projY(T.y) / proj.W * 100 }));
    const groups = [];
    for (const p of pt) {
      const g = groups.find(q => q.some(m => Math.abs(m.x - p.x) < MIN_PCT && Math.abs(m.y - p.y) < MIN_PCT));
      if (g) g.push(p); else groups.push([p]);
    }
    const notes = [];
    for (const g of groups) {
      const cx0 = g.reduce((a, m) => a + m.x, 0) / g.length;
      const cy0 = g.reduce((a, m) => a + m.y, 0) / g.length;
      const box = document.createElement('div');
      box.className = 'clue-map-lode clue-map-tele' + (g.length > 1 ? ' clue-map-telebox' : '');
      box.style.left = cx0 + '%';
      box.style.top = cy0 + '%';
      for (const m of g) {
        const T = m.T;
        const cell = document.createElement('div');
        cell.className = 'cml-cell';
        const icon = document.createElement('div');
        const url = T.iconUrl || (T.item ? resolveIcon(T.item) : '');
        if (T.sp) { icon.className = 'cml-icon'; loadSpriteIcon(icon, T.sp); }
        else if (url) { icon.className = 'cml-icon'; setIconBg(icon, url); }
        else { icon.className = 'cml-dot'; }
        cell.appendChild(icon);
        const kbTxt = teleKb(T);
        if (kbTxt) { const kb = document.createElement('div'); kb.className = 'cml-kb'; kb.textContent = kbTxt; cell.appendChild(kb); }
        const parts = [];
        if (T.src) parts.push(T.src);
        if (T.kb) parts.push('option ' + T.kb);   // name-embedded keys already read in the name itself
        parts.push(T.n);
        if (T.rq) parts.push('req: ' + teleRqText(T));   // sheet requirement text (display-only, ungated)
        const why = teleReqWhy(T);
        if (why) { cell.style.opacity = '0.45'; parts.push(why); }
        cell.dataset.tip = parts.join(', ');   // -> #global-tip (no native title tooltips here)
        box.appendChild(cell);
        const d = Math.max(Math.abs(T.x - ctx0), Math.abs(T.y - cty0));
        notes.push(T.n + (T.kb ? ' [' + T.kb + ']' : '') + ' (' + d + ')');
      }
      el.appendChild(box);
    }
    return '  ·  tele: ' + notes.join(', ');
  }
  async function drawScanMap(c0, opts) {
    const cv = $('clueMapCanvas'), cap = $('clueMapCap');
    const myseq = ++clueMapDrawSeq;
    clueMapMarks = []; clueMapTipBind(cv);
    const pz = $('clueMapPulse'); if (pz) pz.style.display = 'none';
    const lz = $('clueMapLode'); if (lz) lz.style.display = 'none'; const tz = $('clueMapTele'); if (tz) tz.style.display = 'none';
    const rec = opts.rec, elim = opts.elim || new Set(), spots = rec.spots;
    const Pnow = await scanPlayerTile();
    await refreshMeerkats();
    if (myseq !== clueMapDrawSeq) return;
    const Rr = (rec.r || scanLiveRange() || 14) + scanRangeBonus();
    // Scan areas can span MULTIPLE dungeon floors; render ONE plane and show only that floor's
    // spots. Prefer the player's floor when in the area, else the floor with the most spots.
    const planeCount = {}; for (const s of spots) { const p = s[2] || 0; planeCount[p] = (planeCount[p] || 0) + 1; }
    let aminx = 1e9, aminy = 1e9, amaxx = -1e9, amaxy = -1e9;   // bbox over ALL spots (to test if the player is in the area)
    for (const s of spots) { if (s[0] < aminx) aminx = s[0]; if (s[0] > amaxx) amaxx = s[0]; if (s[1] < aminy) aminy = s[1]; if (s[1] > amaxy) amaxy = s[1]; }
    const inArea = Pnow && planeCount[Pnow.p] && Pnow.x >= aminx - 2 * Rr && Pnow.x <= amaxx + 2 * Rr && Pnow.y >= aminy - 2 * Rr && Pnow.y <= amaxy + 2 * Rr;
    if (scanPlaneSelFor !== activeClueId) { scanPlaneSel = null; scanPlaneSelFor = activeClueId; }   // a manual pick is per-clue
    let plane;
    if (inArea) { plane = Pnow.p; scanPlaneSel = null; }
    else if (scanPlaneSel != null && planeCount[scanPlaneSel]) plane = scanPlaneSel;   // remote -> honour the manual floor switch
    else { plane = 0; let bc = -1; for (const p in planeCount) if (planeCount[p] > bc) { bc = planeCount[p]; plane = +p; } }
    const onPlane = s => (s[2] || 0) === plane;
    const offFloors = spots.length - (planeCount[plane] || 0);  // spots on other floors (shown in the caption)
    let minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;       // bbox of the remaining (un-eliminated) on-floor candidates
    let nLive = 0;
    spots.forEach((s, i) => { if (!onPlane(s) || elim.has(i)) return; nLive++;
      if (s[0] < minx) minx = s[0]; if (s[0] > maxx) maxx = s[0]; if (s[1] < miny) miny = s[1]; if (s[1] > maxy) maxy = s[1]; });
    if (nLive === 0) for (const s of spots) { if (!onPlane(s)) continue;   // (defensive: never all-eliminated, but fall back to all)
      if (s[0] < minx) minx = s[0]; if (s[0] > maxx) maxx = s[0]; if (s[1] < miny) miny = s[1]; if (s[1] > maxy) maxy = s[1]; }
    // VIEW box = spots, expanded to include the player only when within scanning reach.
    let vx0 = minx, vx1 = maxx, vy0 = miny, vy1 = maxy;
    if (Pnow && (Pnow.p || 0) === plane && Pnow.x >= minx - 2 * Rr && Pnow.x <= maxx + 2 * Rr && Pnow.y >= miny - 2 * Rr && Pnow.y <= maxy + 2 * Rr) {
      vx0 = Math.min(vx0, Pnow.x); vx1 = Math.max(vx1, Pnow.x); vy0 = Math.min(vy0, Pnow.y); vy1 = Math.max(vy1, Pnow.y);
    }
    const ext = Math.max(vx1 - vx0, vy1 - vy0);
    const ccx = Math.round((vx0 + vx1) / 2), ccy = Math.round((vy0 + vy1) / 2);
    const MAXHALF = 384;                                       // matches mapWindow's cap -> up to 768-tile regions
    let drewTerrain = false, W = 384, cx, projX, projY, lmBox = [vx0, vy0, vx1, vy1];   // landmark cull box = visible tiles
    {                                                          // always render real terrain (window capped to MAXHALF); the scaled-dots
      // overview below is only the last resort when the cache has no map data.
      const half = Math.max(24, Math.min(MAXHALF, Math.ceil(ext / 2) + Math.round(ext * 0.2) + 8));   // min 24 -> a solved single spot still shows local context
      let ts = Math.max(2, Math.min(8, Math.round(384 / (2 * half)))); if (ts & 1) ts++;
      let meta = null;
      try { meta = JSON.parse((await bridge().mapWindow(ccx, ccy, plane, half, ts)) || '{}'); } catch (e) {}
      if (myseq !== clueMapDrawSeq) return;
      W = (meta && meta.w) || 384; const TS = (meta && meta.t) || ts, H = (meta && meta.h) || half;
      if (cv.width !== W) { cv.width = W; cv.height = W; }
      cx = cv.getContext('2d'); cx.clearRect(0, 0, W, W);
      if (meta && meta.b64) { try { const bin = atob(meta.b64), a = new Uint8ClampedArray(bin.length); for (let i = 0; i < bin.length; i++) a[i] = bin.charCodeAt(i); cx.putImageData(new ImageData(a, W, W), 0, 0); clueDrawNomove(cx, meta); clueDrawObjects(cx, meta); clueDrawTeleports(cx, meta); drewTerrain = true; } catch (e) {} }
      projX = sx => (sx - (ccx - H)) * TS + TS / 2;
      projY = sy => ((2 * H - 1) - (sy - (ccy - H))) * TS + TS / 2;
      lmBox = [ccx - H, ccy - H, ccx + H, ccy + H];           // the full rendered window -> show all visible landmarks
    }
    if (!drewTerrain) {                                        // region too large for terrain (or no map data): scaled overview
      W = 384; if (cv.width !== W) { cv.width = W; cv.height = W; }
      cx = cv.getContext('2d'); cx.clearRect(0, 0, W, W); cx.fillStyle = '#0b0d12'; cx.fillRect(0, 0, W, W);
      const pad = 26, sc = (W - 2 * pad) / Math.max(1, ext);
      projX = sx => pad + (sx - vx0) * sc;
      projY = sy => W - pad - (sy - vy0) * sc;
    }
    const bx0 = projX(minx), bx1 = projX(maxx), by0 = projY(maxy), by1 = projY(miny);
    cx.lineWidth = 1; cx.strokeStyle = 'rgba(255,255,255,0.4)'; cx.setLineDash([4, 3]);
    cx.strokeRect(Math.min(bx0, bx1) - 2, Math.min(by0, by1) - 2, Math.abs(bx1 - bx0) + 4, Math.abs(by1 - by0) + 4);
    cx.setLineDash([]);
    clueMapDrawLabels(cx, { projX: projX, projY: projY, W: W }, plane, lmBox[0], lmBox[1], lmBox[2], lmBox[3]);
    const scanLodeNote = await clueMapLodeDraw(lmBox[0], lmBox[1], lmBox[2], lmBox[3], ccx, ccy,
                                               { projX: projX, projY: projY, W: W });
    const scanTeleNote = await clueMapTeleDraw(lmBox[0], lmBox[1], lmBox[2], lmBox[3], ccx, ccy,
                                         { projX: projX, projY: projY, W: W }, plane);
    if (myseq !== clueMapDrawSeq) return;   // a newer draw started while the varps were read
    let remain = 0; const last = (spots.length - elim.size) === 1;
    spots.forEach((s, i) => {
      if (!onPlane(s)) return;
      const px = projX(s[0]), py = projY(s[1]), out = elim.has(i); if (!out) remain++;
      cx.beginPath(); cx.arc(px, py, out ? 2.6 : 4, 0, 6.2832);
      cx.fillStyle = out ? 'rgba(130,130,140,0.55)' : '#ff2d95'; cx.fill();
      cx.lineWidth = 1.2; cx.strokeStyle = out ? 'rgba(0,0,0,0.4)' : 'rgba(0,0,0,0.75)'; cx.stroke();
      if (!out && last) { cx.lineWidth = 2; cx.strokeStyle = '#fff'; cx.beginPath(); cx.arc(px, py, 7.5, 0, 6.2832); cx.stroke(); }
      clueMapMarks.push({ sx: px, sy: py, r: out ? 3 : 5, label: 'Scan spot<br>' + s[0] + ', ' + s[1] + '<br><span style="opacity:.65">' + nearLabel(s[0], s[1], plane) + (out ? ' · ruled out' : '') + '</span>' });
    });
    // The player marker shares the spots' projection; scanR carries the scan-range square.
    clueMapProj = { projX: projX, projY: projY, W: W, plane: plane, scanR: Rr };
    if (Pnow && (Pnow.p || 0) === plane) clueMapMarks.push({ sx: projX(Pnow.x), sy: projY(Pnow.y), r: 6, label: 'You<br>' + Pnow.x + ', ' + Pnow.y });
    clueMapPlayerDraw(Pnow);
    const nextTxt = (remain === 1) ? '  ·  SOLVED (dig the spot)' : '';
    const floorTxt = (Object.keys(planeCount).length > 1) ? ('  ·  floor ' + plane + (offFloors ? ' (' + offFloors + ' on other floors)' : '')) : '';
    const areaTxt = nearLabel(ccx, ccy, plane) ? ('  ·  ' + nearLabel(ccx, ccy, plane)) : '';
    if (cap) cap.textContent = 'Scan ' + (opts.name || rec.key || '') + areaTxt + (Rr ? '  ·  range ' + Rr + (scanRangeBonus() ? ' (Meerkats +5)' : '') : '') + '  ·  ' + remain + ' of ' + (planeCount[plane] || spots.length) + ' spots' + floorTxt + nextTxt + scanLodeNote + scanTeleNote + (drewTerrain ? '' : '  ·  (overview)');
    { const tt = $('clueMapTitle'); if (tt) { tt.textContent = (opts.name || rec.key || 'Scan'); tt.style.display = ''; } }
    // Floor switcher: one button per floor with spots, disabled in-region (it follows your floor).
    const fl = $('clueMapFloors');
    if (fl) {
      const floors = Object.keys(planeCount).map(Number).sort((a, b) => a - b);
      if (floors.length <= 1) { fl.style.display = 'none'; fl._sig = ''; }
      else {
        const fsig = floors.join(',') + '|' + plane + '|' + (inArea ? 1 : 0);
        if (fl._sig !== fsig) {
          fl._sig = fsig; fl.style.display = 'flex';
          const lab = '<span style="opacity:0.6;margin-right:2px">' + (inArea ? 'floor (following you):' : 'view floor:') + '</span>';
          fl.innerHTML = lab + floors.map(p => {
            const on = p === plane;
            const st = 'padding:1px 8px;border-radius:5px;border:1px solid ' + (on ? '#46e0c0' : 'rgba(255,255,255,0.14)')
              + ';background:' + (on ? 'rgba(70,224,192,0.18)' : 'rgba(255,255,255,0.05)') + ';color:inherit;cursor:' + (inArea ? 'default' : 'pointer') + ';font-size:11px';
            return '<button data-floor="' + p + '"' + (inArea ? ' disabled' : '') + ' style="' + st + '">' + p + ' <span style="opacity:0.55">(' + planeCount[p] + ')</span></button>';
          }).join('');
        }
      }
    }
    applyMapZoom();
  }
  // Panel world-map: a terrain render centred on the clue tile (mapWindow bridge) plus a marker.
  async function drawClueMap(t, opts) {
    const wrap = $('clueMapWrap'), cv = $('clueMapCanvas'), cap = $('clueMapCap');
    if (!wrap || !cv) return;
    if (!t) { wrap.style.display = 'none'; const pz = $('clueMapPulse'); if (pz) pz.style.display = 'none'; const lz = $('clueMapLode'); if (lz) lz.style.display = 'none'; const tz = $('clueMapTele'); if (tz) tz.style.display = 'none'; const fz = $('clueMapFloors'); if (fz) { fz.style.display = 'none'; fz._sig = ''; } return; }
    wrap.style.display = '';
    { const fz = $('clueMapFloors'); if (fz) { fz.style.display = 'none'; fz._sig = ''; } }   // dig maps have no floor switcher (scan re-shows it)
    if (opts && opts.rec) { return drawScanMap(t, opts); }
    { const tt = $('clueMapTitle'); if (tt) tt.style.display = 'none'; }
    const myseq = ++clueMapDrawSeq;
    clueMapMarks = []; clueMapTipBind(cv);
    let meta = null;
    if (bridge() && bridge().mapWindow) {
      try { meta = JSON.parse((await bridge().mapWindow(t.x, t.y, t.p || 0, 64, 4)) || '{}'); } catch (e) {}
    }
    if (myseq !== clueMapDrawSeq) return;
    if (myseq !== clueMapDrawSeq) return;
    const W = (meta && meta.w) || 384, TS = (meta && meta.t) || 8, H = (meta && meta.h) || 24;
    if (cv.width !== W) { cv.width = W; cv.height = W; }
    const cx = cv.getContext('2d'); cx.clearRect(0, 0, W, W);
    if (meta && meta.b64) {
      try { const bin = atob(meta.b64), a = new Uint8ClampedArray(bin.length); for (let i = 0; i < bin.length; i++) a[i] = bin.charCodeAt(i); cx.putImageData(new ImageData(a, W, W), 0, 0); clueDrawNomove(cx, meta); clueDrawObjects(cx, meta); clueDrawTeleports(cx, meta); clueDrawLabelsWindow(cx, meta); } catch (e) {}
    }
    // the clue tile is rendered at the window centre: window tile (H,H) -> image (H*TS,(H-1)*TS).
    const mx = H * TS + TS / 2, my = (H - 1) * TS + TS / 2;
    // expose the projection so the live player marker can place itself on this map too.
    clueMapProj = { projX: function (sx) { return (sx - (t.x - H)) * TS + TS / 2; },
                    projY: function (sy) { return ((2 * H - 1) - (sy - (t.y - H))) * TS + TS / 2; }, W: W, plane: t.p || 0 };
    const _digP = await scanPlayerTile(); if (myseq !== clueMapDrawSeq) return; clueMapPlayerDraw(_digP);
    clueMapDrawLabels(cx, clueMapProj, t.p || 0, t.x - H, t.y - H, t.x + H, t.y + H);
    // Nearest UNLOCKED lodestone in the window (unlock read from the lodestone varbits) = the
    // teleport reference toward the spot.
    const lodeNote = await clueMapLodeDraw(t.x - H, t.y - H, t.x + H, t.y + H, t.x, t.y, clueMapProj);
    const teleNote = await clueMapTeleDraw(t.x - H, t.y - H, t.x + H, t.y + H, t.x, t.y, clueMapProj, t.p || 0);
    if (myseq !== clueMapDrawSeq) return;
    cx.lineCap = 'round';
    const gap = 10, len = 11;
    const tick = (dx, dy) => {
      cx.beginPath();
      cx.moveTo(mx + dx * gap, my + dy * gap); cx.lineTo(mx + dx * (gap + len), my + dy * (gap + len));
      cx.lineWidth = 4; cx.strokeStyle = 'rgba(0,0,0,0.55)'; cx.stroke();
      cx.lineWidth = 2; cx.strokeStyle = 'rgba(255,255,255,0.95)'; cx.stroke();
    };
    tick(0, -1); tick(0, 1); tick(-1, 0); tick(1, 0);
    const ring = (r, lw, col) => { cx.lineWidth = lw; cx.strokeStyle = col; cx.beginPath(); cx.arc(mx, my, r, 0, 6.2832); cx.stroke(); };
    ring(11, 5, 'rgba(0,0,0,0.6)'); ring(11, 2.6, '#ff2d95'); ring(13.5, 1.2, 'rgba(255,255,255,0.85)');
    cx.beginPath(); cx.arc(mx, my, 2.8, 0, 6.2832); cx.fillStyle = '#ff2d95'; cx.fill();
    cx.lineWidth = 1.4; cx.strokeStyle = '#fff'; cx.stroke();
    clueMapMarks.push({ sx: mx, sy: my, r: 13, label: '<b>' + (t.mark || 'DIG HERE') + '</b><br>' + t.x + ', ' + t.y + '<br><span style="opacity:.65">' + nearLabel(t.x, t.y, t.p || 0) + '</span>' });
    // Hidey-hole marker (emote clues): opts.hidey = [x, y, plane], drawn when on this plane.
    let hideyNote = '';
    if (opts && opts.hidey && (opts.hidey[2] || 0) === (t.p || 0)) {
      const hx = clueMapProj.projX(opts.hidey[0]), hy = clueMapProj.projY(opts.hidey[1]);
      if (hx >= 0 && hx <= W && hy >= 0 && hy <= W) {
        cx.lineWidth = 2; cx.strokeStyle = 'rgba(70,224,192,0.7)'; cx.setLineDash([5, 4]);
        cx.beginPath(); cx.moveTo(mx, my); cx.lineTo(hx, hy); cx.stroke(); cx.setLineDash([]);
        cx.fillStyle = '#46e0c0'; cx.strokeStyle = 'rgba(0,0,0,0.8)'; cx.lineWidth = 1.5;
        cx.beginPath(); cx.rect(hx - 5, hy - 4, 10, 8); cx.fill(); cx.stroke();
        cx.fillStyle = 'rgba(0,0,0,0.85)'; cx.fillRect(hx - 5, hy - 1.5, 10, 1.5);
      }
      const d = Math.max(Math.abs(opts.hidey[0] - t.x), Math.abs(opts.hidey[1] - t.y));
      hideyNote = '  ·  hidey ' + (hx >= 0 && hx <= W && hy >= 0 && hy <= W ? '' : '(off-map) ') + d + ' tiles';
    }
    // pulse at the tile centre (% so it tracks the scaled canvas).
    const pulse = $('clueMapPulse');
    if (pulse) { pulse.style.display = ''; pulse.style.left = (mx / W * 100) + '%'; pulse.style.top = (my / W * 100) + '%'; }
    const nearTxt = nearLabel(t.x, t.y, t.p || 0) ? ('  ·  ' + nearLabel(t.x, t.y, t.p || 0)) : '';
    if (cap) cap.textContent = 'tile (' + t.x + ', ' + t.y + ')' + (t.p ? '  ·  floor ' + t.p : '') + nearTxt + lodeNote + hideyNote + ((meta && meta.b64) ? '' : '  ·  no map data (rebuild launcher)');
    applyMapZoom();
  }
  const CLUE_ACT_LBL = { coordinate: 'Coordinate', map: 'Map', npc: 'NPC', scan: 'Scan', keyitem: 'Key item', emote: 'Emote/cryptic' };
  const CLUE_SCAN = new Map();   // scan-enum id -> resolved area (lazy, live cache)
  let clueResolving = false;
  // A few NPC challenge-scroll answers change with quest/world state: read the controlling varbits
  // live and show only the answer that applies. id -> [varp, bitLo, bitHi].
  const CLUE_VB = { 11334: [2395, 0, 7], 13931: [2785, 0, 5], 13734: [2759, 0, 7], 11610: [2430, 0, 8], 40083: [7864, 0, 3] };
  let clueVarps = {};   // varp id -> live value
  function clueVb(id) {
    const d = CLUE_VB[id]; if (!d) return null;
    const raw = clueVarps[d[0]]; if (raw === undefined || raw === null) return null;
    return ((raw >>> 0) >>> d[1]) & ((1 << (d[2] - d[1] + 1)) - 1);
  }
  // The single correct challenge answer for a state-dependent NPC, or null (not special / unreadable -> show both).
  function clueLiveAnswer(npc) {
    if (npc === 7181)  { const v = clueVb(11334); return v == null ? null : (v >= 100 ? '0' : '11'); }            // Caroline: Kennith's Concerns
    if (npc === 28)    { const e = clueVb(13931), r = clueVb(13734); if (e == null || r == null) return null;     // Zookeeper: Eagles' Peak + Red Raktuber
                         const n = (e >= 40 ? 1 : 0) + (r >= 140 ? 1 : 0); return n === 0 ? '40' : n === 1 ? '41' : '42'; }
    if (npc === 17135) { const a = clueVb(11610), b = clueVb(40083); if (a == null || b == null) return null;     // Moldark: Edgeville destroyed
                         return (a >= 400 && b < 10) ? '3' : '5'; }
    return null;
  }
  async function fetchClues(force) {
    if (!bridge() || clueFetching) return;
    const _t = Date.now(); if (!force && _t - clueFetchAt < 2000) return; clueFetchAt = _t;
    clueFetching = true;
    try {
      const held = new Set(), heldInv = new Set(), scrolls = [], puzzles = [];
      if (bridge().containerItems) {
        for (const cid of [93, 95]) {   // inventory (always) + bank (only while open)
          // items: [[slot, item_id, stack, name], ..] -- item id is index 1, name is index 3.
          try { const r = JSON.parse(await bridge().containerItems(myPid(), cid)); (r.items || []).forEach(x => { if (Array.isArray(x) && x[1] > 0) { held.add(x[1]); if (cid === 93) { heldInv.add(x[1]); if (x[3] && /clue scroll|puzzle box|challenge scroll/i.test(x[3])) scrolls.push(x[1]); if (x[3] && /puzzle (box|scroll box|casket)/i.test(x[3])) { const lc = x[3].toLowerCase(); const t = lc.includes('master') ? 4 : lc.includes('elite') ? 3 : lc.includes('hard') ? 2 : lc.includes('medium') ? 1 : 0; puzzles.push({ i: x[1], t: t, a: 'puzzle', nm: x[3] }); } } } }); } catch (e) {}
        }
      }
      // A clue must leave the inventory to be replaced, so a not-held -> held transition is a fresh
      // instance. The clue ITEM id cannot key it: every elite scan clue of an area shares one id.
      if (clueHeldPrevInit) {
        for (const id of held) if (!clueHeldPrev.has(id)) scanElimResetFor(id);
      }
      clueHeldPrev = new Set(held); clueHeldPrevInit = true;
      clueHeld = held; clueHeldInv = heldInv; cluePuzzleHeld = puzzles;
      // The tetracompass is a real item but not a clue-database entry, so it gets a synthetic
      // carousel entry the same way puzzle boxes do. Backpack only: a banked one solves nothing.
      tetraHeldEntry = heldInv.has(TETRA_POWERED)
        ? { i: TETRA_POWERED, t: 5, a: 'tetra', nm: 'Tetracompass (powered)' } : null;
      // Session decode cache lifetime: the held clue-scroll fingerprint (each step = a distinct item id).
      clueHeldScrollSig = scrolls.sort((a, b) => a - b).join(',');
      if (clueAuto) {
        // Deferred to here so the just-opened clue is already in the inventory snapshot; otherwise
        // open-within-one-fetch would falsely self-clear.
        if (clueAutoHeldSig === '?') clueAutoHeldSig = clueHeldScrollSig;
        else if (clueAutoHeldSig !== '' && clueAutoHeldSig !== clueHeldScrollSig) clueDismissAuto();
      }
      clueData = CLUE_DATA;
      // Varps backing the state-dependent challenge answers.
      if (bridge().varps) {
        const vps = [...new Set(Object.values(CLUE_VB).map(d => d[0]))].join(',');
        try { clueVarps = JSON.parse(await bridge().varps(myPid(), vps)) || {}; } catch (e) {}
      }
    } finally { clueFetching = false; }
    if (!scanElimLoaded) { scanElimLoaded = true; scanElimLoad(); }
    scanTick();   // narrow scan candidates from the live orb + player tile (no-op unless a scan is focused)
    if (activeTab === 'clues') renderClues();
    if (activeTab === 'globetrotter') renderGlobetrotterTab();
    if (activeTab === 'cluestats') renderClueStats();
    resolveClueNames();
  }
  function scanAreaText(e) {
    const vals = e ? Object.values(e) : [];
    if (!vals.length) return '';
    if (typeof vals[0] === 'string') return vals.filter(Boolean).join(', ');   // area name roster
    return vals.length + ' scan spot' + (vals.length === 1 ? '' : 's');         // coord roster -> count
  }
  async function resolveClueNames() {
    if (clueResolving || !bridge()) return;
    clueResolving = true;
    try {
      // NPC names are baked into CLUE_DATA (nn) from the cache, so only scan areas need a live resolve.
      let didResolve = false;
      if (bridge().enumInfo) for (const c of CLUE_DATA) {
        if (c.a === 'scan' && c.en > 0 && !CLUE_SCAN.has(c.en)) {
          CLUE_SCAN.set(c.en, '');
          try {
            const e = JSON.parse(await bridge().enumInfo(c.en));
            CLUE_SCAN.set(c.en, scanAreaText(e));
            const vals = e ? Object.values(e) : [];
            let key = '', rec = null, spots = null;
            if (vals.length && typeof vals[0] === 'string') {
              key = scanKey(vals[0]); rec = CLUE_SCAN_AREAS[key]; if (rec) spots = rec.s;   // name roster -> baked spots
            } else if (vals.length) {
              spots = vals.map(v => { v = v >>> 0; return [(v >> 14) & 0x3fff, v & 0x3fff, (v >> 28) & 3]; });   // packed coords
              for (const s of spots) { const kk = SCAN_SPOT_INDEX[s[0] + ',' + s[1] + ',' + (s[2] || 0)]; if (kk) { key = kk; break; } }   // area via any matching spot
              rec = key ? CLUE_SCAN_AREAS[key] : null;
            }
            if (spots && spots.length) { CLUE_SCAN_RESOLVED.set(c.en, { key, r: rec ? rec.r : 0, spots }); didResolve = true; }
          } catch (e2) {}
        }
      }
      // a focused scan clue selected before its enum resolved -> draw it now that spots exist
      if (didResolve && activeClueId >= 0) { const ac = CLUE_DATA.find(z => z.i === activeClueId); if (ac && ac.a === 'scan' && CLUE_SCAN_RESOLVED.has(ac.en)) selectClue(); }
    } finally { clueResolving = false; }
    if (activeTab === 'clues') { clueListSig = ''; renderCluesList(); }
  }
  function clueActionText(c) {
    let s;
    if (c.a === 'puzzle') return (c.nm || 'Puzzle box') + ' · solve it in the panel below';
    if (c.a === 'tetra') { const nm = c.nm || 'Tetracompass';
      if (!tetraOpen()) return nm + ' \u00b7 open it to read the dig site';
      const t = tetraTarget();
      return nm + (t ? ' \u00b7 dig at (' + t.x + ', ' + t.y + ')' : ' \u00b7 charge it to reveal a dig site'); }
    if (c.a === 'coordinate' || c.a === 'map') s = (c.a === 'coordinate' ? 'Dig at ' : 'Go to ') + '(' + c.x + ', ' + c.y + ')' + (c.p ? ' · floor ' + c.p : '');
    else if (c.a === 'search') s = 'Go to (' + c.x + ', ' + c.y + ') and search the clue object (drawers/crate/etc.)' + (c.p ? ' · floor ' + c.p : '');
    else if (c.a === 'npc') { const ni = CLUE_NPC_INFO[c.npc] || {}; const ans = clueLiveAnswer(c.npc) || ni.a; s = 'Talk to ' + (c.nn || ('NPC #' + c.npc)) + (c.gd ? ' [quest-gated]' : '') + (ans ? ' · answer: ' + ans : ''); }
    else if (c.a === 'scan') { if (isCompassClue(c)) { s = 'Compass clue (follow the needle)'; } else { const ar = CLUE_SCAN.get(c.en), r = CLUE_SCAN_RESOLVED.get(c.en); s = 'Scan ' + (ar || ('area (enum ' + c.en + ')')) + (r ? ' · ' + r.spots.length + ' spots' + (r.r ? ' · range ' + r.r : '') : ''); } }
    else if (c.a === 'keyitem') s = 'Search for key (' + (c.req || 'key item') + ')';
    else s = 'Emote / cryptic  ·  tap for the emote-clue guide (location, items, hidey-hole)';
    if (c.req && c.a !== 'keyitem') s += ' · needs ' + c.req;
    return s;
  }
  // ---- BEGIN generated mejrs teleport data (tools/pull_map_teleports.py) ----
  // 798 teleports from the crowdsourced map sheet; regenerate with the tool, do not hand-edit.
  const MAP_TELEPORTS_EXT = [
    {n:'Ring of Wealth - Grand Exchange',src:'Enchanted Jewellery',x:3162,y:3464,p:0,item:20659,kb:'1'},
    {n:'Ring of Wealth - Miscellania',src:'Enchanted Jewellery',x:2508,y:3861,p:1,item:20659,kb:'2'},
    {n:'Ring of Fortune - Grand Exchange',src:'Enchanted Jewellery',x:3162,y:3464,p:0,item:39808,kb:'1'},
    {n:'Ring of Fortune - Miscellania',src:'Enchanted Jewellery',x:2508,y:3861,p:1,item:39808,kb:'2'},
    {n:'Luck of the dwarves - Grand Exchange',src:'Enchanted Jewellery',x:3162,y:3464,p:0,item:39812,kb:'1'},
    {n:'Luck of the Dwarves - Miscellania',src:'Enchanted Jewellery',x:2508,y:3861,p:1,item:39812,kb:'2'},
    {n:'Luck of the Dwarves - Keldagrim',src:'Enchanted Jewellery',x:2857,y:10199,p:0,item:39812,kb:'3'},
    {n:'Hazelmere\'s signet ring - Grand Exchange',src:'Enchanted Jewellery',x:3162,y:3464,p:0,item:39814,kb:'1'},
    {n:'Hazelmere\'s signet ring - Miscellania',src:'Enchanted Jewellery',x:2508,y:3861,p:1,item:39814,kb:'2'},
    {n:'Hazelmere\'s signet ring - Keldagrim',src:'Enchanted Jewellery',x:2857,y:10199,p:0,item:39814,kb:'3'},
    {n:'Hazelmere\'s signet ring - Hazelmere\'s Statue',src:'Enchanted Jewellery',x:2424,y:3396,p:0,item:39814,kb:'4'},
    {n:'Amulet of glory - Edgeville',src:'Enchanted Jewellery',x:3087,y:3496,p:0,item:1712,kb:'1'},
    {n:'Amulet of glory - Draynor',src:'Enchanted Jewellery',x:3080,y:3250,p:0,item:1712,kb:'2'},
    {n:'Amulet of glory - Al Kharid',src:'Enchanted Jewellery',x:3305,y:3123,p:0,item:1712,kb:'3'},
    {n:'Amulet of glory - Karamja',src:'Enchanted Jewellery',x:2918,y:3176,p:0,item:1712,kb:'4'},
    {n:'Skills necklace - Fishing Guild',src:'Enchanted Jewellery',x:2615,y:3385,p:0,item:11105,kb:'1'},
    {n:'Skills necklace - Mining Guild',src:'Enchanted Jewellery',x:3016,y:3338,p:0,item:11105,kb:'2'},
    {n:'Skills necklace - Crafting Guild',src:'Enchanted Jewellery',x:2933,y:3290,p:0,item:11105,kb:'3'},
    {n:'Skills necklace - Cooking Guild',src:'Enchanted Jewellery',x:3143,y:3442,p:0,item:11105,kb:'4'},
    {n:'Skills necklace - Invention Guild',src:'Enchanted Jewellery',x:2997,y:3437,p:0,item:11105,kb:'5'},
    {n:'Skills necklace - Farming Guild',src:'Enchanted Jewellery',x:2646,y:3355,p:0,item:11105,kb:'6'},
    {n:'Skills necklace - Runecrafting Guild',src:'Enchanted Jewellery',x:3102,y:3152,p:3,item:11105,kb:'7'},
    {n:'Digsite pendant - Digsite',src:'Enchanted Jewellery',x:3355,y:3395,p:0,item:11194,kb:'1'},
    {n:'Digsite pendant - Senntisten',src:'Enchanted Jewellery',x:3378,y:3444,p:0,item:11194,kb:'2'},
    {n:'Digsite pendant - Exam Centre',src:'Enchanted Jewellery',x:3362,y:3345,p:0,item:11194,kb:'3'},
    {n:'Games necklace - Troll Invasion',src:'Enchanted Jewellery',x:2877,y:3560,p:0,item:3853,kb:'1'},
    {n:'Games necklace - Barbarian Outpost',src:'Enchanted Jewellery',x:2520,y:3571,p:0,item:3853,kb:'2'},
    {n:'Games necklace - Gamer\'s Grotto',src:'Enchanted Jewellery',x:2967,y:9678,p:0,item:3853,kb:'3'},
    {n:'Games necklace - Agoroth',src:'Enchanted Jewellery',x:3860,y:6827,p:0,item:3853,kb:'4'},
    {n:'Games necklace - Corporeal Beast',src:'Enchanted Jewellery',x:2885,y:4372,p:2,item:3853,kb:'5'},
    {n:'Games necklace - Burgh De Rott',src:'Enchanted Jewellery',x:3487,y:3237,p:0,item:3853,kb:'6'},
    {n:'Games necklace - Tears of Guthix',src:'Enchanted Jewellery',x:3250,y:9517,p:2,item:3853,kb:'7'},
    {n:'Ring of duelling - Al Kharid Duel Arena',src:'Enchanted Jewellery',x:3316,y:3234,p:0,item:2552,kb:'1'},
    {n:'Ring of duelling - Castle Wars Arena',src:'Enchanted Jewellery',x:2442,y:3089,p:0,item:2552,kb:'2'},
    {n:'Ring of duelling - South Feldip Hills',src:'Enchanted Jewellery',x:2413,y:2847,p:0,item:2552,kb:'3'},
    {n:'Ring of duelling - Fist of Guthix',src:'Enchanted Jewellery',x:1694,y:5601,p:0,item:2552,kb:'4'},
    {n:'Ring of respawn - Lumbridge',src:'Enchanted Jewellery',x:3223,y:3222,p:0,item:39366,kb:'1'},
    {n:'Ring of respawn - Falador',src:'Enchanted Jewellery',x:2971,y:3340,p:0,item:39366,kb:'2'},
    {n:'Ring of respawn - Camelot',src:'Enchanted Jewellery',x:2760,y:3478,p:0,item:39366,kb:'3'},
    {n:'Ring of respawn - Soul Wars',src:'Enchanted Jewellery',x:1893,y:3179,p:0,item:39366,kb:'4'},
    {n:'Ring of respawn - Burthorpe',src:'Enchanted Jewellery',x:2890,y:3539,p:0,item:39366,kb:'5'},
    {n:'Traveller\'s necklace - Wizard\'s Tower',src:'Enchanted Jewellery',x:3101,y:3180,p:0,item:39372,kb:'1'},
    {n:'Traveller\'s necklace - The Outpost',src:'Enchanted Jewellery',x:2444,y:3348,p:0,item:39372,kb:'2'},
    {n:'Traveller\'s necklace - South of the Desert Eagle\'s Eyrie',src:'Enchanted Jewellery',x:3421,y:3141,p:0,item:39372,kb:'3'},
    {n:'Enlightened amulet - Nexus',src:'Enchanted Jewellery',x:3216,y:3180,p:0,item:39387,kb:'1'},
    {n:'Enlightened amulet - South of the Graveyard of Shadows',src:'Enchanted Jewellery',x:3232,y:3657,p:0,item:39387,kb:'2'},
    {n:'Enlightened amulet - Desert bandit camp entrance',src:'Enchanted Jewellery',x:3168,y:2993,p:0,item:39387,kb:'3'},
    {n:'Combat bracelet - Warriors\' Guild',src:'Enchanted Jewellery',x:2880,y:3542,p:0,item:11118,kb:'1'},
    {n:'Combat bracelet - Champions\' Guild',src:'Enchanted Jewellery',x:3191,y:3365,p:0,item:11118,kb:'2'},
    {n:'Combat bracelet - Edgeville Monastery',src:'Enchanted Jewellery',x:3052,y:3488,p:0,item:11118,kb:'3'},
    {n:'Combat bracelet - Ranging Guild',src:'Enchanted Jewellery',x:2655,y:3441,p:0,item:11118,kb:'4'},
    {n:'Archaeology campus',src:'Master Archaeologist outfit',x:3336,y:3378,p:0,item:49941,kb:'1'},
    {n:'Kharid-et',src:'Master Archaeologist outfit',x:3345,y:3194,p:0,item:49941,kb:'2'},
    {n:'Infernal Source',src:'Master Archaeologist outfit',x:3271,y:3504,p:0,item:49941,kb:'3'},
    {n:'Everlight',src:'Master Archaeologist outfit',x:3697,y:3206,p:0,item:49941,kb:'4'},
    {n:'Stormguard Citadel',src:'Master Archaeologist outfit',x:2680,y:3403,p:0,item:49941,kb:'5'},
    {n:'Warforge',src:'Master Archaeologist outfit',x:2409,y:2824,p:0,item:49941,kb:'6'},
    {n:'Art Critic Jacques',src:'Master Archaeologist outfit',x:3254,y:3453,p:2,item:49941,kb:'1,1'},
    {n:'Chief Tess',src:'Master Archaeologist outfit',x:2550,y:2853,p:0,item:49941,kb:'1,2'},
    {n:'Generals Bentnoze & Wartface',src:'Master Archaeologist outfit',x:2957,y:3510,p:0,item:49941,kb:'1,3'},
    {n:'Isaura',src:'Master Archaeologist outfit',x:2921,y:9701,p:0,item:49941,kb:'1,4'},
    {n:'Lowse',src:'Master Archaeologist outfit',x:2985,y:3268,p:0,item:49941,kb:'1,5'},
    {n:'Sir Atcha',src:'Master Archaeologist outfit',x:2963,y:3346,p:0,item:49941,kb:'1,6'},
    {n:'Soran (Emissary of Zaros)',src:'Master Archaeologist outfit',x:3181,y:3417,p:0,item:49941,kb:'1,7'},
    {n:'Velucia',src:'Master Archaeologist outfit',x:3342,y:3383,p:0,item:49941,kb:'1,8'},
    {n:'Wise Old Man',src:'Master Archaeologist outfit',x:3088,y:3254,p:0,item:49941,kb:'1,9'},
    {n:'Varrock armour 4 - Bork',src:'Achievement tasks set',x:3143,y:5544,p:0,item:19757},
    {n:'Tirannwn quiver - Lletya',src:'Achievement tasks set',x:2345,y:3172,p:0,item:33719,kb:'1'},
    {n:'Tirannwn quiver  - Islwyn (post plagues end)',src:'Achievement tasks set',x:2175,y:3353,p:1,item:33719,kb:'2'},
    {n:'Tirannwn quiver  - Islwyn (pre plagues end - Eastern camp)',src:'Achievement tasks set',x:2291,y:3146,p:0,item:33719,kb:'2'},
    {n:'Tirannwn quiver  - Islwyn (pre plagues end - Western camp)',src:'Achievement tasks set',x:2204,y:3159,p:0,item:33719,kb:'2'},
    {n:'Tirannwn quiver 2+ - Poison Waste',src:'Achievement tasks set',x:1991,y:4175,p:0,item:33720,kb:'4'},
    {n:'Tirannwn quiver 2+ - Tyras Camp',src:'Achievement tasks set',x:2186,y:3148,p:0,item:33720,kb:'3'},
    {n:'Tirannwn quiver 3+ - Death Altar',src:'Achievement tasks set',x:1858,y:4639,p:0,item:33721,kb:'5'},
    {n:'Tirannwn quiver 3+ - Elf Camp',src:'Achievement tasks set',x:2203,y:3256,p:0,item:33721,kb:'6'},
    {n:'Tirannwn quiver 4 - Harmony Pillars',src:'Achievement tasks set',x:2221,y:3399,p:1,item:33722,kb:'8'},
    {n:'Tirannwn quiver 3 - Mushroom patch',src:'Achievement tasks set',x:2227,y:3135,p:0,item:33721,kb:'7'},
    {n:'Explorer\'s ring - Cabbage-port',src:'Achievement tasks set',x:3053,y:3291,p:0,item:13560},
    {n:'Wilderness sword - Wilderness herb patch',src:'Achievement tasks set',x:3141,y:3816,p:0,item:37904,kb:'1, 1'},
    {n:'Wilderness sword - Edgeville',src:'Achievement tasks set',x:3084,y:3502,p:0,item:37904,kb:'1, 2'},
    {n:'Wilderness sword 2+ - Forinthry Dungeon',src:'Achievement tasks set',x:3083,y:10060,p:0,item:37905,kb:'1, 3'},
    {n:'Wilderness sword 4  - Warband camp RDI',src:'Achievement tasks set',x:3298,y:3781,p:0,item:37907,kb:'1, 4'},
    {n:'Wilderness sword 4 - Wilderness Agility course',src:'Achievement tasks set',x:3000,y:3913,p:0,item:37907,kb:'1, 5'},
    {n:'Ardougne cloak - Kandarin Monastery',src:'Achievement tasks set',x:2606,y:3219,p:0,item:15345,kb:'1'},
    {n:'Ardougne cloak 2+ - Ardougne allotment',src:'Achievement tasks set',x:2671,y:3376,p:0,item:15347,kb:'2'},
    {n:'Desert amulet 2+ - Nardah',src:'Achievement tasks set',x:3428,y:2919,p:0,item:27094,kb:'1'},
    {n:'Desert amulet 4 - Uzer',src:'Achievement tasks set',x:3475,y:3096,p:0,item:27096,kb:'2'},
    {n:'Enchanted lyre - Waterbirth',src:'Achievement tasks set',x:2528,y:3741,p:0,item:3690,rq:'Fremennik hard achievements'},
    {n:'Enchanted lyre - Jatizso',src:'Achievement tasks set',x:2404,y:3781,p:0,item:3690,rq:'Fremennik elite achievements'},
    {n:'Enchanted lyre - Rellekka Market',src:'Achievement tasks set',x:2654,y:3693,p:0,item:3690,rq:'The Fremennik Trials'},
    {n:'Fremennik sea boots 4 - Rellekka',src:'Achievement tasks set',x:2643,y:3678,p:0,item:19766},
    {n:'Karamja gloves 3+ - Shilo Village gem dungeon',src:'Achievement tasks set',x:2840,y:9386,p:0,item:11140},
    {n:'Morytania legs 2+ - Ectofuntus slime pit',src:'Achievement tasks set',x:3683,y:9887,p:0,item:24135},
    {n:'Tureal/Spria',src:'Slayer cape',x:2889,y:3547,p:0,item:9786,kb:'1',rq:'99 Slayer'},
    {n:'Jacquelyn',src:'Slayer cape',x:3221,y:3223,p:0,item:9786,kb:'2',rq:'99 Slayer'},
    {n:'Vannaka and Mandrith',src:'Slayer cape',x:3093,y:3478,p:0,item:9786,kb:'3',rq:'99 Slayer'},
    {n:'Mazchna',src:'Slayer cape',x:3508,y:3508,p:0,item:9786,kb:'4',rq:'99 Slayer'},
    {n:'Chaeldar',src:'Slayer cape',x:2445,y:4431,p:0,item:9786,kb:'5',rq:'99 Slayer'},
    {n:'Sumona',src:'Slayer cape',x:3360,y:2993,p:0,item:9786,kb:'6',rq:'99 Slayer'},
    {n:'Duradel/Lapalok',src:'Slayer cape',x:2869,y:2982,p:1,item:9786,kb:'7',rq:'99 Slayer'},
    {n:'Kuradal',src:'Slayer cape',x:1686,y:5287,p:1,item:9786,kb:'8',rq:'99 Slayer'},
    {n:'Morvran',src:'Slayer cape',x:2195,y:3327,p:1,item:9786,kb:'9',rq:'99 Slayer'},
    {n:'Laniakea',src:'Slayer cape',x:5669,y:2137,p:1,item:9786,kb:'0',rq:'99 Slayer'},
    {n:'Edgeville Dungeon chaos druids dungeon',src:'Dungeoneering cape',x:3134,y:9915,p:0,item:15706,kb:'1',rq:'99 Dungeoneering'},
    {n:'Dwarven mine hidden mine',src:'Dungeoneering cape',x:3036,y:9774,p:0,item:15706,kb:'2',rq:'99 Dungeoneering'},
    {n:'Edgeville hill giant dungeon',src:'Dungeoneering cape',x:3105,y:9829,p:0,item:15706,kb:'3',rq:'99 Dungeoneering'},
    {n:'Karamja Volcano lesser demon dungeon',src:'Dungeoneering cape',x:2843,y:9555,p:0,item:15706,kb:'4',rq:'99 Dungeoneering'},
    {n:'Daemonheim woodcutting island dungeon',src:'Dungeoneering cape',x:3512,y:3663,p:0,item:15706,kb:'5',rq:'99 Dungeoneering'},
    {n:'Waterfall fire giant dungeon',src:'Dungeoneering cape',x:2581,y:9897,p:0,item:15706,kb:'6',rq:'99 Dungeoneering'},
    {n:'Mining Guild hidden mine',src:'Dungeoneering cape',x:3022,y:9739,p:0,item:15706,kb:'7',rq:'99 Dungeoneering'},
    {n:'Braindeath Island',src:'Dungeoneering cape',x:2125,y:5146,p:0,item:15706,kb:'8',rq:'99 Dungeoneering'},
    {n:'Taverly hellhound dungeon',src:'Dungeoneering cape',x:2856,y:9840,p:0,item:15706,kb:'9',rq:'99 Dungeoneering'},
    {n:'Taverly blue dragon dungeon',src:'Dungeoneering cape',x:2915,y:9806,p:0,item:15706,kb:'0,1',rq:'99 Dungeoneering'},
    {n:'Varrock moss giant dungeon',src:'Dungeoneering cape',x:3166,y:9879,p:0,item:15706,kb:'0,2',rq:'99 Dungeoneering'},
    {n:'Dragontooth celestial dragon dungeon',src:'Dungeoneering cape',x:3812,y:3528,p:0,item:15706,kb:'0,3',rq:'99 Dungeoneering'},
    {n:'Chaos Tunnels black demon dungeon',src:'Dungeoneering cape',x:3159,y:5524,p:0,item:15706,kb:'0,4',rq:'99 Dungeoneering'},
    {n:'Al Kharid hidden mine',src:'Dungeoneering cape',x:3299,y:3307,p:0,item:15706,kb:'0,5',rq:'99 Dungeoneering'},
    {n:'Brimhaven metal dragon dungeon',src:'Dungeoneering cape',x:2697,y:9441,p:0,item:15706,kb:'0,6',rq:'99 Dungeoneering'},
    {n:'Polypore Dungeon',src:'Dungeoneering cape',x:4659,y:5491,p:3,item:15706,kb:'0,7',rq:'99 Dungeoneering'},
    {n:'Ice Dungeon frost dragon dungeon',src:'Dungeoneering cape',x:3032,y:9599,p:0,item:15706,kb:'0,8',rq:'99 Dungeoneering'},
    {n:'Kal\'gerion demon dungeon',src:'Dungeoneering cape',x:3398,y:3665,p:0,item:15706,kb:'0,9',rq:'99 Dungeoneering'},
    {n:'Gorajo hoardstalker dungeon',src:'Dungeoneering cape',x:2235,y:3420,p:1,item:15706,kb:'0,0,1',rq:'99 Dungeoneering'},
    {n:'Morytania slayer tower dungeon',src:'Dungeoneering cape',x:3434,y:3529,p:0,item:15706,kb:'0,0,2',rq:'99 Dungeoneering'},
    {n:'Edimmu dungeon',src:'Dungeoneering cape',x:2233,y:3396,p:1,item:15706,kb:'0,0,3',rq:'99 Dungeoneering'},
    {n:'Edgeville Dungeon: Chaos Druids',src:'Hoardstalker ring',x:991,y:4584,p:0,item:28588},
    {n:'Dwarven Mine',src:'Hoardstalker ring',x:1042,y:4574,p:0,item:28588},
    {n:'Edgeville Dungeon: Hill Giants',src:'Hoardstalker ring',x:1136,y:4589,p:0,item:28588},
    {n:'Karamja Volcano lesser demon dungeon',src:'Hoardstalker ring',x:1186,y:4598,p:0,item:28588},
    {n:'Daemonheim Peninsula',src:'Hoardstalker ring',x:3498,y:3632,p:0,item:28588},
    {n:'Baxtorian Falls',src:'Hoardstalker ring',x:1255,y:4590,p:0,item:28588},
    {n:'Mining Guild',src:'Hoardstalker ring',x:1051,y:4521,p:0,item:28588},
    {n:'Braindeath Island',src:'Hoardstalker ring',x:2127,y:5146,p:0,item:28588},
    {n:'Taverly Dungeon: Blue Dragons',src:'Hoardstalker ring',x:1002,y:4523,p:0,item:28588},
    {n:'Taverly Dungeon: Hellhounds',src:'Hoardstalker ring',x:1392,y:4587,p:0,item:28588},
    {n:'Varrock Sewers',src:'Hoardstalker ring',x:1302,y:4580,p:0,item:28588},
    {n:'Dragontooth celestial dragon dungeon',src:'Hoardstalker ring',x:3812,y:3528,p:0,item:28588},
    {n:'Chaos Tunnels',src:'Hoardstalker ring',x:1237,y:4522,p:0,item:28588},
    {n:'Al Kharid Mine',src:'Hoardstalker ring',x:1181,y:4514,p:0,item:28588},
    {n:'Brimhaven Dungeon',src:'Hoardstalker ring',x:1140,y:4499,p:0,item:28588},
    {n:'Polypore Dungeon',src:'Hoardstalker ring',x:4661,y:5490,p:3,item:28588},
    {n:'Asgarnian Ice Dungeon',src:'Hoardstalker ring',x:1297,y:4511,p:0,item:28588},
    {n:'Kal\'gerion Dungeon',src:'Hoardstalker ring',x:1295,y:1287,p:0,item:28588},
    {n:'Prifddinas: Gorajo',src:'Hoardstalker ring',x:1317,y:4635,p:0,item:28588},
    {n:'Slayer Tower',src:'Hoardstalker ring',x:1166,y:4616,p:0,item:28588},
    {n:'Prifddinas: Motherlode',src:'Hoardstalker ring',x:1374,y:4612,p:0,item:28588},
    {n:'Ancient Guthix Temple',src:'Master Quest Cape',x:2540,y:5773,p:0,item:36166,kb:'1'},
    {n:'Behind the Scene',src:'Master Quest Cape',x:1181,y:5394,p:1,item:36166,kb:'2'},
    {n:'Champions\' Guild',src:'Master Quest Cape',x:3189,y:3360,p:0,item:36166,kb:'3'},
    {n:'The Empty Throne Room',src:'Master Quest Cape',x:2826,y:12630,p:2,item:36166,kb:'4'},
    {n:'Glacor Cavern',src:'Master Quest Cape',x:4195,y:5750,p:0,item:36166,kb:'5'},
    {n:'Heroes\' Guild - Fountain of Heroes',src:'Master Quest Cape',x:2921,y:9894,p:0,item:36166,kb:'6'},
    {n:'Legends\' Guild (outside)',src:'Master Quest Cape',x:2728,y:3348,p:0,item:36166,kb:'7'},
    {n:'Legends\' Guild (inside)',src:'Master Quest Cape',x:2729,y:3376,p:0,item:36166,kb:'7'},
    {n:'Tears of Guthix',src:'Master Quest Cape',x:3250,y:9515,p:2,item:36166,kb:'8'},
    {n:'Varrock Museum',src:'Master Quest Cape',x:3253,y:3449,p:0,item:36166,kb:'9'},
    {n:'The World Gate',src:'Master Quest Cape',x:2367,y:3361,p:0,item:36166,kb:'0'},
    {n:'Lletya',src:'Crystal teleport seed',x:2332,y:3170,p:0,item:39786,kb:'1',rq:'Mourning\'s End Part I'},
    {n:'Temple of Light',src:'Crystal teleport seed',x:1940,y:4640,p:0,item:39786,kb:'2',rq:'Within The Light'},
    {n:'Amlodd',src:'Crystal teleport seed',x:2157,y:3382,p:1,item:39786,kb:'3',rq:'Plague\'s End'},
    {n:'Cadarn',src:'Crystal teleport seed',x:2261,y:3339,p:1,item:39786,kb:'4',rq:'Plague\'s End'},
    {n:'Crwys',src:'Crystal teleport seed',x:2263,y:3384,p:1,item:39786,kb:'5',rq:'Plague\'s End'},
    {n:'Hefin',src:'Crystal teleport seed',x:2185,y:3410,p:1,item:39786,kb:'6',rq:'Plague\'s End'},
    {n:'Iorwerth',src:'Crystal teleport seed',x:2186,y:3312,p:1,item:39786,kb:'7',rq:'Plague\'s End'},
    {n:'Ithell',src:'Crystal teleport seed',x:2157,y:3340,p:1,item:39786,kb:'8',rq:'Plague\'s End'},
    {n:'Meilyr',src:'Crystal teleport seed',x:2232,y:3410,p:1,item:39786,kb:'9',rq:'Plague\'s End'},
    {n:'Trahaearn',src:'Crystal teleport seed',x:2231,y:3312,p:1,item:39786,kb:'0',rq:'Plague\'s End'},
    {n:'Jadinko Lair',src:'Grace of the Elves',x:3034,y:9231,p:0,item:44548},
    {n:'Lava Flow Mine',src:'Grace of the Elves',x:2177,y:5663,p:1,item:44548},
    {n:'Living Rock Caverns',src:'Grace of the Elves',x:3651,y:5122,p:0,item:44548},
    {n:'Brilliant wisps',src:'Grace of the Elves',x:3402,y:3293,p:0,item:44548},
    {n:'Radiant wisps',src:'Grace of the Elves',x:3807,y:3549,p:0,item:44548},
    {n:'Luminous wisps',src:'Grace of the Elves',x:3311,y:2657,p:0,item:44548},
    {n:'Incandescent wisps',src:'Grace of the Elves',x:2285,y:3047,p:0,item:44548},
    {n:'Runespan (upper)',src:'Grace of the Elves',x:4297,y:6040,p:1,item:44548},
    {n:'Tree Gnome Stronghold',src:'Grace of the Elves',x:2462,y:3444,p:0,item:44548},
    {n:'Zanaris Fairy Ring',src:'Grace of the Elves',x:2412,y:4434,p:0,item:44548},
    {n:'Puro-Puro',src:'Grace of the Elves',x:2591,y:4319,p:0,item:44548},
    {n:'Prifddinas Waterfall',src:'Grace of the Elves',x:2293,y:3404,p:3,item:44548},
    {n:'Invention Guild',src:'Grace of the Elves',x:6169,y:1041,p:0,item:44548},
    {n:'Menaphos VIP area',src:'Grace of the Elves',x:3186,y:2744,p:0,item:44548},
    {n:'Deep Sea Fishing',src:'Grace of the Elves',x:2135,y:7107,p:1,item:44548},
    {n:'Overgrown Idol',src:'Grace of the Elves',x:2949,y:2977,p:0,item:44548},
    {n:'Ring of kinship',src:'Other teleports',x:3442,y:3694,p:0,item:15707},
    {n:'Sixth-Age circuit - Guthix\'s shrine',src:'Other teleports',x:1923,y:5987,p:1,item:27477,kb:'1'},
    {n:'Sixth-Age circuit - World Gate',src:'Other teleports',x:2367,y:3358,p:0,item:27477,kb:'2'},
    {n:'Sixth-Age circuit - Guthix Memorial',src:'Other teleports',x:2264,y:3554,p:1,item:27477,kb:'3'},
    {n:'Sixth-Age circuit - Temple of Guthix (Tormented Demons)',src:'Other teleports',x:2540,y:5773,p:0,item:27477,kb:'4'},
    {n:'Drakan\'s medallion - Barrows',src:'Other teleports',x:3561,y:3311,p:0,item:21576,kb:'1'},
    {n:'Drakan\'s medallion - Burgh de Rott',src:'Other teleports',x:3496,y:3202,p:0,item:21576,kb:'2'},
    {n:'Drakan\'s medallion - Meiyerditch',src:'Other teleports',x:3627,y:9617,p:0,item:21576,kb:'3'},
    {n:'Drakan\'s medallion - Darkmeyer',src:'Other teleports',x:3625,y:3365,p:0,item:21576,kb:'4'},
    {n:'Drakan\'s medallion - Meiyderditch Laboratories',src:'Other teleports',x:3633,y:9696,p:0,item:21576,kb:'5'},
    {n:'Pharaoh\'s sceptre - Jalsavrah',src:'Other teleports',x:1942,y:4498,p:0,item:27090,kb:'1'},
    {n:'Pharaoh\'s sceptre - Jaleustrophos',src:'Other teleports',x:3341,y:2827,p:0,item:27090,kb:'2'},
    {n:'Pharaoh\'s sceptre - Jaldraocht',src:'Other teleports',x:3232,y:2897,p:0,item:27090,kb:'3'},
    {n:'Sceptre of the gods - Jalsavrah',src:'Other teleports',x:1942,y:4498,p:0,item:21536,kb:'1'},
    {n:'Sceptre of the gods - Jaleustrophos',src:'Other teleports',x:3341,y:2827,p:0,item:21536,kb:'2'},
    {n:'Sceptre of the gods - Jaldraocht',src:'Other teleports',x:3232,y:2897,p:0,item:21536,kb:'3'},
    {n:'Sceptre of the gods - Jalkabir',src:'Other teleports',x:3175,y:2729,p:0,item:21536,kb:'4'},
    {n:'Skull sceptre - Outside',src:'Other teleports',x:3081,y:3421,p:0,item:9013,kb:'1'},
    {n:'Skull sceptre - Vault of War',src:'Other teleports',x:1862,y:5242,p:0,item:9013,kb:'2'},
    {n:'Skull sceptre - Catacomb of Famine',src:'Other teleports',x:2044,y:5245,p:0,item:9013,kb:'3'},
    {n:'Skull sceptre - Pit of Pestilence',src:'Other teleports',x:2125,y:5253,p:0,item:9013,kb:'4'},
    {n:'Skull sceptre - Sepulchre of Death',src:'Other teleports',x:2360,y:5213,p:0,item:9013,kb:'5'},
    {n:'Broomstick - Sorceress\'s Garden',src:'Other teleports',x:2914,y:5476,p:0,item:14057},
    {n:'Ectophial',src:'Other teleports',x:3657,y:3524,p:0,item:4251},
    {n:'Camulet/Cramulet - Enakhra\'s temple',src:'Other teleports',x:3194,y:2926,p:0,item:6707},
    {n:'Bonesack(e)',src:'Other teleports',x:3361,y:3505,p:0,item:15215},
    {n:'Ram skull helmet(e)',src:'Other teleports',x:3361,y:3505,p:0,item:15214},
    {n:'Remora\'s necklace',src:'Other teleports',x:2994,y:3231,p:0,item:24302},
    {n:'Book of Char',src:'Other teleports',x:2356,y:3437,p:0,item:23027},
    {n:'Invitation box',src:'Other teleports',x:3787,y:4360,p:1,item:29742,kb:'1'},
    {n:'Big Book o\' Piracy - Mos Le\'Harmless',src:'Other teleports',x:3684,y:2958,p:0,item:42379,kb:'1'},
    {n:'Big Book o\' Piracy - Braindeath Isle',src:'Other teleports',x:2162,y:5114,p:1,item:42379,kb:'2'},
    {n:'Big Book o\' Piracy - Dragontooth Isle',src:'Other teleports',x:3792,y:3559,p:0,item:42379,kb:'3'},
    {n:'Big Book o\' Piracy - Harmony Isle',src:'Other teleports',x:3797,y:2836,p:0,item:42379,kb:'4'},
    {n:'TokKul-Zo - Main Plaza',src:'Other teleports',x:4668,y:5151,p:1,item:23643,kb:'1'},
    {n:'TokKul-Zo - Fight Pit',src:'Other teleports',x:4603,y:5062,p:0,item:23643,kb:'2'},
    {n:'TokKul-Zo - Fight Cave',src:'Other teleports',x:4611,y:5126,p:0,item:23643,kb:'3'},
    {n:'TokKul-Zo - Fight Kiln',src:'Other teleports',x:4744,y:5173,p:0,item:23643,kb:'4'},
    {n:'TokKul-Zo - Fight Cauldron',src:'Other teleports',x:4784,y:5129,p:0,item:23643,kb:'5'},
    {n:'Witchdoctor mask/Juju teleport spiritbag',src:'Other teleports',x:2953,y:2933,p:0,item:19967},
    {n:'Communication device - Zamorak\'s Hideout',src:'Other teleports',x:3488,y:6169,p:1,item:24190},
    {n:'Void knight seal',src:'Other teleports',x:2658,y:2660,p:1,item:11666},
    {n:'Clan vexillum - Falador',src:'Other teleports',x:2967,y:3282,p:0,item:20709},
    {n:'Clan vexillum - Prifddinas',src:'Other teleports',x:2219,y:3373,p:1,item:20709},
    {n:'Ferocious ring - Kuradal (TODO probably the same as slayer cape)',src:'Other teleports',x:1740,y:5313,p:1,item:15398},
    {n:'Ferocious ring - Morvran (TODO also the hotkey varies with if its a ring or on slayer helm)',src:'Other teleports',x:2195,y:3328,p:1,item:15398},
    {n:'Ring of slaying - Sumona',src:'Other teleports',x:3360,y:2993,p:0,item:13281,kb:'1'},
    {n:'Ring of slaying - Morytania Slayer Tower',src:'Other teleports',x:3420,y:3525,p:0,item:13281,kb:'2'},
    {n:'Ring of slaying - Fremennik Slayer Dungeon',src:'Other teleports',x:2792,y:3615,p:0,item:13281,kb:'3'},
    {n:'Ring of slaying - Tarn\'s Lair',src:'Other teleports',x:3185,y:4599,p:0,item:13281,kb:'4'},
    {n:'Dominion medallion',src:'Other teleports',x:3741,y:6428,p:0,item:22338},
    {n:'Grand seed pod',src:'Other teleports',x:2465,y:3495,p:0,item:9469},
    {n:'Captain\'s log',src:'Other teleports',x:4447,y:7524,p:1,item:26358},
    {n:'Memory strand',src:'Other teleports',x:2295,y:3552,p:0,item:39486},
    {n:'Fang of Mohegan - Taverley (Pikkupstix)',src:'Other teleports',x:2930,y:3447,p:0,item:29000,kb:'1'},
    {n:'Fang of Mohegan - Gu\'Tanoth (Bogrog)',src:'Other teleports',x:2524,y:3055,p:0,item:29000,kb:'2'},
    {n:'Ancient seed',src:'Other teleports',x:5611,y:2456,p:0},
    {n:'Spirit Kyatt Familiar',src:'Other teleports',x:2323,y:3634,p:0,item:12812,kb:'2'},
    {n:'Spirit Graahk Familiar',src:'Other teleports',x:2787,y:3002,p:0,item:12810,kb:'2'},
    {n:'Light Creature Familiar  - Tears of Guthix',src:'Other teleports',x:3250,y:9517,p:2,item:32829,kb:'2'},
    {n:'Light Creature Familiar - Tormented Demons',src:'Other teleports',x:2540,y:5773,p:0,item:32829,kb:'3'},
    {n:'Lava Titan Familiar - Level 40 Wilderness',src:'Other teleports',x:3027,y:3834,p:0,item:12788,kb:'2'},
    {n:'Cape of Legends (outside the Legends\' Guild)',src:'Other teleports',x:2728,y:3348,p:0,item:1052},
    {n:'Cape of Legends (inside the Legends\' Guild)',src:'Other teleports',x:2729,y:3376,p:0,item:1052},
    {n:'Skull of Remembrance',src:'Other teleports',x:3035,y:3577,p:3,item:28927,kb:'1'},
    {n:'Shattered Worlds teleport scroll',src:'Other teleports',x:3168,y:3163,p:0,item:50218},
    {n:'Archaeology journal',src:'Other teleports',x:3336,y:3378,p:0,item:49429},
    {n:'Disk of Returning',src:'Other teleports',x:1183,y:5396,p:1,rq:'Gower Quest'},
    {n:'Mask of Stench - Slayer Tower near Markus',src:'Other teleports',x:3419,y:3524,p:0,item:31091,kb:'1'},
    {n:'Worldbearer ring (1)',src:'Other teleports',x:4326,y:7707,p:0,item:33915},
    {n:'Kethsi ring - Adamant dragons',src:'Other teleports',x:4512,y:6043,p:0,item:34987,kb:'1'},
    {n:'Kethsi ring - Rune dragons',src:'Other teleports',x:4767,y:6077,p:2,item:34987,kb:'2'},
    {n:'South Feldip Hills',src:'Standard Spellbook',x:2413,y:2847,p:0,sp:36136,rq:'10 Magic'},
    {n:'Taverley',src:'Standard Spellbook',x:2912,y:3423,p:0,sp:35031,rq:'19 Magic'},
    {n:'Varrock',src:'Standard Spellbook',x:3211,y:3434,p:0,sp:36137,rq:'25 Magic'},
    {n:'Varrock (Easy Varrock Achievements)',src:'Standard Spellbook',x:3162,y:3464,p:0,sp:36137,rq:'25 Magic, Easy Varrock Achievements'},
    {n:'Varrock (Elite Varrock Achievements)',src:'Standard Spellbook',x:3246,y:3479,p:0,sp:36137,rq:'25 Magic, Elite Varrock Achievements'},
    {n:'Lumbridge',src:'Standard Spellbook',x:3219,y:3248,p:0,sp:36135,rq:'31 Magic'},
    {n:'Falador',src:'Standard Spellbook',x:2965,y:3379,p:0,sp:36132,rq:'37 Magic'},
    {n:'Camelot',src:'Standard Spellbook',x:2759,y:3478,p:0,sp:36130,rq:'45 Magic'},
    {n:'Ardougne',src:'Standard Spellbook',x:2660,y:3303,p:0,sp:35028,rq:'51 Magic, Plague City'},
    {n:'Watchtower',src:'Standard Spellbook',x:2933,y:4714,p:2,sp:35033,rq:'58 Magic, Watchtower'},
    {n:'Watchtower (Hard Ardougne Achievements)',src:'Standard Spellbook',x:2574,y:3090,p:0,sp:35033,rq:'58 Magic, Watchtower, Hard Ardougne Achievements'},
    {n:'Trollheim',src:'Standard Spellbook',x:2880,y:3669,p:0,sp:35032,rq:'61 Magic, Edgar\'s Ruse'},
    {n:'God Wars Dungeon',src:'Standard Spellbook',x:2908,y:3714,p:0,sp:35029,rq:'61 Magic, The Mighty Fall'},
    {n:'Ape Atoll',src:'Standard Spellbook',x:2796,y:2797,p:1,sp:35027,rq:'64 Magic, Recipe for Disaster: Freeing King Awowogei'},
    {n:'Mazcab',src:'Standard Spellbook',x:4317,y:820,p:0,sp:35030,rq:'70 Magic, unlocked using a Mazcab teleport codex or Mazcab ability codex'},
    {n:'Anachronia - West totem',src:'Standard Spellbook',x:5285,y:2347,p:0,sp:10371,rq:'72 Magic'},
    {n:'Anachronia - North totem',src:'Standard Spellbook',x:5483,y:2574,p:0,sp:10371,rq:'72 Magic'},
    {n:'Anachronia - Southeast totem',src:'Standard Spellbook',x:5635,y:2234,p:0,sp:10371,rq:'72 Magic'},
    {n:'Max guild Teleport - Outside',src:'Standard Spellbook',x:2276,y:3327,p:1,sp:36145,rq:'99 in all skills'},
    {n:'Max guild Teleport - Inside',src:'Standard Spellbook',x:2276,y:3313,p:1,sp:36145,rq:'99 in all skills'},
    {n:'Kandarin Monastery',src:'Standard Spellbook',x:2606,y:3219,p:0,sp:36143,rq:'Ardougne medium achievements'},
    {n:'Manor Farm',src:'Standard Spellbook',x:2671,y:3376,p:0,sp:36144,rq:'Ardougne easy achievements'},
    {n:'Skeletal horror',src:'Standard Spellbook',x:3362,y:3503,p:0,sp:36142,rq:'Fur \'n\' Seek Wish list'},
    {n:'War\'s Retreat',src:'Standard Spellbook',x:3294,y:10127,p:0,sp:35042,rq:'10 boss kills'},
    {n:'Paddewwa (Edgeville Dungeon)',src:'Ancient Spellbook',x:3097,y:9884,p:0,sp:36128,rq:'54 Magic, Desert Treasure'},
    {n:'Senntisten (Digsite)',src:'Ancient Spellbook',x:3375,y:3404,p:0,sp:36129,rq:'60 Magic, Desert Treasure'},
    {n:'Kharyrll (Canifis)',src:'Ancient Spellbook',x:3499,y:3482,p:0,sp:36126,rq:'66 Magic, Desert Treasure'},
    {n:'Lassar (Ice Mountain)',src:'Ancient Spellbook',x:3006,y:3472,p:0,sp:36127,rq:'72 Magic, Desert Treasure'},
    {n:'Dareeyak (leve 26 Wilderness ruins)',src:'Ancient Spellbook',x:2967,y:3696,p:0,sp:36124,rq:'78 Magic, Desert Treasure'},
    {n:'Carrallanger (Graveyard of Shadows)',src:'Ancient Spellbook',x:3220,y:3667,p:0,rq:'84 Magic, Desert Treasure'},
    {n:'Annarkarl (Demonic Ruins)',src:'Ancient Spellbook',x:3289,y:3884,p:0,sp:36122,rq:'90 Magic, Desert Treasure'},
    {n:'Ghorrock (Ice Plateau)',src:'Ancient Spellbook',x:2978,y:3872,p:0,sp:36125,rq:'96 Magic, Desert Treasure'},
    {n:'Tele-group Moonclan',src:'Lunar Spellbook',x:2112,y:3916,p:0,sp:14428,rq:'70 Magic, Lunar Diplomacy'},
    {n:'Tele-group Waterbirth',src:'Lunar Spellbook',x:2547,y:3754,p:0,sp:14429,rq:'73 Magic, Lunar Diplomacy'},
    {n:'Tele-group Barbarian',src:'Lunar Spellbook',x:2542,y:3571,p:0,sp:14430,rq:'76 Magic, Lunar Diplomacy'},
    {n:'Tele-group Khazard',src:'Lunar Spellbook',x:2635,y:3168,p:0,sp:14431,rq:'79 Magic, Lunar Diplomacy'},
    {n:'Tele-group Fishing Guild',src:'Lunar Spellbook',x:2612,y:3381,p:0,sp:14432,rq:'85 Magic, Lunar Diplomacy'},
    {n:'Tele-group Catherby',src:'Lunar Spellbook',x:2789,y:3454,p:0,sp:14433,rq:'88 Magic, Lunar Diplomacy'},
    {n:'Tele-group Ice Plateau',src:'Lunar Spellbook',x:2973,y:3940,p:0,sp:14434,rq:'89 Magic, Lunar Diplomacy'},
    {n:'Tele-group Trollheim',src:'Lunar Spellbook',x:2818,y:3677,p:0,rq:'92 Magic, Lunar Diplomacy, 4000000 produce points'},
    {n:'Moonclan',src:'Lunar Spellbook',x:2112,y:3916,p:0,sp:14403,rq:'69 Magic, Lunar Diplomacy'},
    {n:'Ourania',src:'Lunar Spellbook',x:2466,y:3245,p:0,sp:14442,rq:'71 Magic, Lunar Diplomacy'},
    {n:'Waterbirth',src:'Lunar Spellbook',x:2547,y:3754,p:0,sp:14404,rq:'72 Magic, Lunar Diplomacy'},
    {n:'South Falador (Livid)',src:'Lunar Spellbook',x:3053,y:3309,p:0,sp:14444,rq:'72 Magic, Lunar Diplomacy, 35000 produce points'},
    {n:'Barbarian',src:'Lunar Spellbook',x:2542,y:3571,p:0,sp:14406,rq:'75 Magic, Lunar Diplomacy'},
    {n:'North Ardougne (Livid)',src:'Lunar Spellbook',x:2672,y:3378,p:0,sp:14446,rq:'76 Magic, Lunar Diplomacy, 110000 produce points'},
    {n:'Khazard',src:'Lunar Spellbook',x:2635,y:3168,p:0,sp:14408,rq:'78 Magic, Lunar Diplomacy'},
    {n:'Fishing Guild',src:'Lunar Spellbook',x:2612,y:3381,p:0,sp:14414,rq:'86 Magic, Lunar Diplomacy'},
    {n:'Catherby',src:'Lunar Spellbook',x:2789,y:3454,p:0,sp:14415,rq:'87 Magic, Lunar Diplomacy'},
    {n:'Ice Plateau',src:'Lunar Spellbook',x:2973,y:3940,p:0,sp:14416,rq:'89 Magic, Lunar Diplomacy'},
    {n:'Trollheim (Livid)',src:'Lunar Spellbook',x:2818,y:3677,p:0,rq:'92 Magic, Lunar Diplomacy, 370000 produce points'},
    {n:'Zanaris',src:'Portable fairy ring',x:2412,y:4434,p:0,item:41076,rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Asgarnia: Mudskipper Point',src:'Portable fairy ring',x:2996,y:3114,p:0,item:41076,kb:'AIQ',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Dungeons: Ancient cavern',src:'Portable fairy ring',x:1737,y:5342,p:0,item:41076,kb:'BJQ',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Dungeons: Dark cave south of Dorgesh-Kaan',src:'Portable fairy ring',x:2735,y:5221,p:0,item:41076,kb:'AJQ',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Dungeons: Glacor Cave',src:'Portable fairy ring',x:4183,y:5726,p:0,item:41076,kb:'DKQ',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Dungeons: Myreque hideout under the Hollows',src:'Portable fairy ring',x:3501,y:9821,p:3,item:41076,kb:'DLS',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Dungeons: TzHaar area',src:'Portable fairy ring',x:4622,y:5147,p:0,item:41076,kb:'BLP',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Feldip Hills: Jungle Hunter area',src:'Portable fairy ring',x:2571,y:2956,p:0,item:41076,kb:'AKS',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Feldip Hills: Near Gu\'Tanoth',src:'Portable fairy ring',x:2468,y:4189,p:0,item:41076,kb:'ALP',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Feldip Hills: South of Castle Wars',src:'Portable fairy ring',x:2385,y:3035,p:0,item:41076,kb:'BKP',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Islands: Ape Atoll',src:'Portable fairy ring',x:2735,y:2742,p:0,item:41076,kb:'CLR',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Islands: Jungle spiders near Yanille',src:'Portable fairy ring',x:2682,y:3081,p:0,item:41076,kb:'CLS',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Islands: Miscellania',src:'Portable fairy ring',x:2513,y:3884,p:0,item:41076,kb:'CIP',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Islands: Mos Le\'Harmless',src:'Portable fairy ring',x:3763,y:2930,p:0,item:41076,kb:'DIP',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Islands: Penguins near Miscellania',src:'Portable fairy ring',x:2500,y:3896,p:0,item:41076,kb:'AJS',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Islands: Poison Waste south of Isafdar',src:'Portable fairy ring',x:2213,y:3099,p:0,item:41076,kb:'DLR',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Islands: Polypore Dungeon',src:'Portable fairy ring',x:3410,y:3324,p:0,item:41076,kb:'BIP',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Islands: South of Draynor Village',src:'Portable fairy ring',x:3082,y:3206,p:0,item:41076,kb:'CLP',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Islands: South of Witchhaven',src:'Portable fairy ring',x:2700,y:3247,p:0,item:41076,kb:'AIR',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Kandarin: Ardougne Zoo unicorns',src:'Portable fairy ring',x:2635,y:3266,p:0,item:41076,kb:'BIS',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Kandarin: Legends\' Guild',src:'Portable fairy ring',x:2740,y:3351,p:0,item:41076,kb:'BLR',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Kandarin: McGrubor\'s Wood',src:'Portable fairy ring',x:2644,y:3495,p:0,item:41076,kb:'ALS',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Kandarin: North-west of Yanille',src:'Portable fairy ring',x:2528,y:3127,p:0,item:41076,kb:'CIQ',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Kandarin: Piscatoris Hunter area',src:'Portable fairy ring',x:2319,y:3619,p:0,item:41076,kb:'AKQ',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Kandarin: Sinclair Mansion (east)',src:'Portable fairy ring',x:2705,y:3576,p:0,item:41076,kb:'CJR',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Kandarin: Sinclair Mansion (west)',src:'Portable fairy ring',x:2676,y:3587,p:0,item:41076,kb:'DJR',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Kandarin: Slayer cave south-east of Rellekka',src:'Portable fairy ring',x:2780,y:3613,p:0,item:41076,kb:'AJR',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Kandarin: Snowy Hunter area',src:'Portable fairy ring',x:2744,y:3719,p:0,item:41076,kb:'DKS',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Kandarin: Tower of Life',src:'Portable fairy ring',x:2658,y:3230,p:0,item:41076,kb:'DJP',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Karamja: Kharazi Jungle',src:'Portable fairy ring',x:2901,y:2930,p:0,item:41076,kb:'CJS',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Karamja: South of Musa Point',src:'Portable fairy ring',x:2900,y:3111,p:0,item:41076,kb:'DKP',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Karamja: South of Tai Bwo Wannai',src:'Portable fairy ring',x:2801,y:3003,p:0,item:41076,kb:'CKR',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Kharidian Desert: Near Kalphite Hive',src:'Portable fairy ring',x:3251,y:3095,p:0,item:41076,kb:'BIQ',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Kharidian Desert: North of Nardah',src:'Portable fairy ring',x:3423,y:3016,p:0,item:41076,kb:'DLQ',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Menaphos: Imperial District',src:'Portable fairy ring',x:3086,y:2704,p:0,item:41076,kb:'CKQ',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Misthalin: Edgeville',src:'Portable fairy ring',x:3129,y:3496,p:0,item:41076,kb:'DKR',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Misthalin: Wizards\' Tower',src:'Portable fairy ring',x:3092,y:3137,p:0,item:41076,kb:'DIS',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Morytania: Canifis',src:'Portable fairy ring',x:3447,y:3470,p:0,item:41076,kb:'CKS',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Morytania: Haunted Woods east of Canifis',src:'Portable fairy ring',x:3597,y:3495,p:0,item:41076,kb:'ALQ',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Morytania: Mort Myre, south of Canifis',src:'Portable fairy ring',x:3469,y:3431,p:0,item:41076,kb:'BKR',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Other realms: Abyss',src:'Portable fairy ring',x:3059,y:4875,p:0,item:41076,kb:'ALR',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Other realms: Cosmic Entity\'s plane',src:'Portable fairy ring',x:2075,y:4848,p:1,item:41076,kb:'CKP',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Other realms: Enchanted Valley',src:'Portable fairy ring',x:3041,y:4532,p:0,item:41076,kb:'BKQ',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Other realms: Goraks\' Plane',src:'Portable fairy ring',x:3038,y:5348,p:0,item:41076,kb:'DIR',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Other realms: Naragi homeworld',src:'Portable fairy ring',x:2030,y:5982,p:2,item:41076,kb:'AIS',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Other realms: Realm of the Fisher King',src:'Portable fairy ring',x:2650,y:4730,p:0,item:41076,kb:'BJR',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Other realms: ScapeRune (Evil Bob\'s island)',src:'Portable fairy ring',x:3419,y:4772,p:0,item:41076,kb:'CIS',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Other realms: Sparse Plane',src:'Portable fairy ring',x:2455,y:4396,p:0,item:41076,kb:'BIR',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Other realms: Yu\'biusk',src:'Portable fairy ring',x:2229,y:4244,p:1,item:41076,kb:'BLQ',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'The Lost Grove',src:'Portable fairy ring',x:1359,y:5635,p:1,item:41076,kb:'BJS',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Tirannwn: Prifddinas (Clan Amlodd)',src:'Portable fairy ring',x:2130,y:3369,p:1,item:41076,kb:'DJS',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Kethsi',src:'Portable fairy ring',x:4026,y:5699,p:0,item:41076,kb:'DIR, AKS',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Fairy Queen\'s Hideout',src:'Portable fairy ring',x:1560,y:4234,p:0,item:41076,kb:'AIR, DLS, DJQ, AJS',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Orks Rift',src:'Portable fairy ring',x:1626,y:4176,p:0,item:41076,kb:'BIR, DIP, CLR, ALP',rq:'A Fairy Tale II - Cure a Queen, 94 Invention [B]'},
    {n:'Miscellania',src:'Teleport scrolls',x:2513,y:3858,p:0,item:19477},
    {n:'Lumber Yard',src:'Teleport scrolls',x:3306,y:3489,p:0},
    {n:'Bandit Camp',src:'Teleport scrolls',x:3172,y:2983,p:0,item:19476},
    {n:'Pollnivneach',src:'Teleport scrolls',x:3361,y:2970,p:0,item:19475},
    {n:'Phoenix Lair',src:'Teleport scrolls',x:2292,y:3620,p:0,item:19478},
    {n:'Tai Bwo Wannai',src:'Teleport scrolls',x:2805,y:3086,p:0,item:19479},
    {n:'Lighthouse',src:'Teleport scrolls',x:2508,y:3634,p:0,item:41804},
    {n:'Clocktower',src:'Teleport scrolls',x:2593,y:3254,p:0,item:41803},
    {n:'Gu\'Tanoth',src:'Teleport scrolls',x:2521,y:3062,p:0,item:41802},
    {n:'Grand Exchange',src:'Teleport scrolls',x:3162,y:3464,p:0,item:41801},
    {n:'Miscellania',src:'Globetrotter arm guards',x:2513,y:3858,p:0,item:42103,kb:'7'},
    {n:'Lumber Yard',src:'Globetrotter arm guards',x:3306,y:3489,p:0,item:42103,kb:'6'},
    {n:'Bandit Camp',src:'Globetrotter arm guards',x:3172,y:2983,p:0,item:42103,kb:'2'},
    {n:'Pollnivneach',src:'Globetrotter arm guards',x:3361,y:2970,p:0,item:42103,kb:'9'},
    {n:'Phoenix Lair',src:'Globetrotter arm guards',x:2292,y:3620,p:0,item:42103,kb:'8'},
    {n:'Tai Bwo Wannai',src:'Globetrotter arm guards',x:2805,y:3086,p:0,item:42103,kb:'0'},
    {n:'Lighthouse',src:'Globetrotter arm guards',x:2508,y:3634,p:0,item:42103,kb:'5'},
    {n:'Clocktower',src:'Globetrotter arm guards',x:2593,y:3254,p:0,item:42103,kb:'3'},
    {n:'Gu\'Tanoth',src:'Globetrotter arm guards',x:2521,y:3062,p:0,item:42103,kb:'4'},
    {n:'Grand Exchange',src:'Globetrotter arm guards',x:3162,y:3464,p:0,item:42103,kb:'1'},
    {n:'Tree Gnome Village',src:'Spirit tree re-rooter',x:2542,y:3169,p:0,item:41078,kb:'1'},
    {n:'Tree Gnome Stronghold',src:'Spirit tree re-rooter',x:2462,y:3444,p:0,item:41078,kb:'2'},
    {n:'Battlefield of Khazard',src:'Spirit tree re-rooter',x:2557,y:3259,p:0,item:41078,kb:'3'},
    {n:'Grand Exchange',src:'Spirit tree re-rooter',x:3185,y:3511,p:0,item:41078,kb:'4'},
    {n:'South Feldip Hills',src:'Spirit tree re-rooter',x:2416,y:2851,p:0,item:41078,kb:'5'},
    {n:'Port Sarim',src:'Spirit tree re-rooter',x:3058,y:3257,p:0,item:41078,kb:'6'},
    {n:'Etceteria',src:'Spirit tree re-rooter',x:2613,y:3855,p:0,item:41078,kb:'7'},
    {n:'Brimhaven',src:'Spirit tree re-rooter',x:2800,y:3203,p:0,item:41078,kb:'8'},
    {n:'Poison Waste',src:'Spirit tree re-rooter',x:2338,y:3109,p:0,item:41078,kb:'9'},
    {n:'Prifddinas',src:'Spirit tree re-rooter',x:2275,y:3371,p:1,item:41078,kb:'0'},
    {n:'Manor Farm',src:'Spirit tree re-rooter',x:2661,y:3383,p:0,item:41078,kb:'M'},
    {n:'Shifting Tombs',src:'Teletabs (excluding ones with spell versions)',x:2076,y:6952,p:0,item:40264},
    {n:'Sophanem Slayer Dungeon',src:'Teletabs (excluding ones with spell versions)',x:3288,y:2706,p:0,item:40263},
    {n:'Merchant District',src:'Teletabs (excluding ones with spell versions)',x:3207,y:2781,p:0,item:40261},
    {n:'Ports District',src:'Teletabs (excluding ones with spell versions)',x:3184,y:2654,p:0,item:40262},
    {n:'Worker District',src:'Teletabs (excluding ones with spell versions)',x:3158,y:2796,p:0,item:40259},
    {n:'Imperial District',src:'Teletabs (excluding ones with spell versions)',x:3175,y:2731,p:0,item:40260},
    {n:'Air altar teleport',src:'Teletabs (excluding ones with spell versions)',x:3125,y:3406,p:0,item:13599},
    {n:'Mind altar teleport',src:'Teletabs (excluding ones with spell versions)',x:2980,y:3513,p:0,item:13600},
    {n:'Water altar teleport',src:'Teletabs (excluding ones with spell versions)',x:3159,y:3188,p:0,item:13601},
    {n:'Earth altar teleport',src:'Teletabs (excluding ones with spell versions)',x:3303,y:3477,p:0,item:13602},
    {n:'Fire altar teleport',src:'Teletabs (excluding ones with spell versions)',x:3309,y:3251,p:0,item:13603},
    {n:'Body altar teleport',src:'Teletabs (excluding ones with spell versions)',x:3051,y:3441,p:0,item:13604},
    {n:'Cosmic altar teleport',src:'Teletabs (excluding ones with spell versions)',x:2407,y:4383,p:0,item:13605},
    {n:'Chaos altar teleport',src:'Teletabs (excluding ones with spell versions)',x:3058,y:3594,p:0,item:13606},
    {n:'Astral altar teleport',src:'Teletabs (excluding ones with spell versions)',x:2150,y:3862,p:0,item:13611},
    {n:'Nature altar teleport',src:'Teletabs (excluding ones with spell versions)',x:2865,y:3022,p:0,item:13607},
    {n:'Law altar teleport',src:'Teletabs (excluding ones with spell versions)',x:2857,y:3379,p:0,item:13608},
    {n:'Death altar teleport',src:'Teletabs (excluding ones with spell versions)',x:1864,y:4638,p:0,item:13609},
    {n:'Blood altar teleport',src:'Teletabs (excluding ones with spell versions)',x:3559,y:9778,p:0,item:13610},
    {n:'Runecrafting guild teleport',src:'Teletabs (excluding ones with spell versions)',x:1696,y:5465,p:2,item:13598},
    {n:'Chipped house - Rimmington',src:'Teletabs (excluding ones with spell versions)',x:2954,y:3225,p:0,item:18809},
    {n:'Chipped house - Taverley',src:'Teletabs (excluding ones with spell versions)',x:2882,y:3451,p:0,item:18810},
    {n:'Chipped house - Pollnivneach',src:'Teletabs (excluding ones with spell versions)',x:3339,y:3004,p:0,item:18811},
    {n:'Chipped house - Rellekka',src:'Teletabs (excluding ones with spell versions)',x:2668,y:3631,p:0,item:18812},
    {n:'Chipped house - Brimhaven',src:'Teletabs (excluding ones with spell versions)',x:2757,y:3177,p:0,item:18813},
    {n:'Chipped house - Yanille',src:'Teletabs (excluding ones with spell versions)',x:2546,y:3095,p:0,item:18814},
    {n:'Chipped house - Trollheim',src:'Teletabs (excluding ones with spell versions)',x:2891,y:3676,p:0,item:20175},
    {n:'Chipped house - Prifddinas',src:'Teletabs (excluding ones with spell versions)',x:2166,y:3336,p:1,item:32285},
    {n:'Chipped house - Otot',src:'Teletabs (excluding ones with spell versions)',x:4196,y:920,p:0,item:35136},
    {n:'Chipped house - Menaphos',src:'Teletabs (excluding ones with spell versions)',x:3124,y:2634,p:1,item:40671},
    {n:'Chipped house - Anachronia',src:'Teletabs (excluding ones with spell versions)',x:5436,y:2375,p:1,item:48171},
    {n:'Arc Journal - Port Sarim',src:'Teletabs (excluding ones with spell versions)',x:3050,y:3245,p:1,item:37729},
    {n:'Waiko',src:'Teletabs (excluding ones with spell versions)',x:1820,y:11616,p:1,item:37903},
    {n:'Whale\'s Maw',src:'Teletabs (excluding ones with spell versions)',x:2060,y:11800,p:0,item:38576},
    {n:'Tuai Leit',src:'Teletabs (excluding ones with spell versions)',x:1800,y:11961,p:0,item:38578},
    {n:'The Islands That Once Were Turtles',src:'Teletabs (excluding ones with spell versions)',x:2279,y:11503,p:0,item:38579},
    {n:'Aminishi',src:'Teletabs (excluding ones with spell versions)',x:2085,y:11276,p:0,item:38575},
    {n:'Goshima',src:'Teletabs (excluding ones with spell versions)',x:2462,y:11546,p:0,item:38580},
    {n:'Cyclosis',src:'Teletabs (excluding ones with spell versions)',x:2316,y:11224,p:0,item:38577},
    {n:'Dagannoth Kings',src:'Teletabs (excluding ones with spell versions)',x:1958,y:4375,p:1,item:38203},
    {n:'Dorgesh-kaan sphere',src:'Teletabs (excluding ones with spell versions)',x:2721,y:5275,p:0,item:10972},
    {n:'Goblin Village sphere',src:'Teletabs (excluding ones with spell versions)',x:2961,y:3506,p:0,item:11060},
    {n:'Plain of Mud sphere',src:'Teletabs (excluding ones with spell versions)',x:2582,y:9838,p:0,item:11794},
    {n:'Bandos throne room sphere',src:'Teletabs (excluding ones with spell versions)',x:2344,y:4247,p:0,item:14692},
    {n:'\'Chipped\' Varrock teleport',src:'Teletabs (excluding ones with spell versions)',x:3254,y:3449,p:0,item:42605},
    {n:'\'Chipped\' Lumbridge teleport',src:'Teletabs (excluding ones with spell versions)',x:3168,y:3199,p:0,item:42606},
    {n:'\'Chipped\' Falador teleport',src:'Teletabs (excluding ones with spell versions)',x:3007,y:3318,p:0,item:42607},
    {n:'\'Chipped\' Camelot teleport',src:'Teletabs (excluding ones with spell versions)',x:2796,y:3419,p:1,item:42608},
    {n:'\'Chipped\' Ardougne teleport',src:'Teletabs (excluding ones with spell versions)',x:2537,y:3308,p:0,item:42609},
    {n:'\'Chipped\' Watchtower teleport',src:'Teletabs (excluding ones with spell versions)',x:2444,y:3178,p:0,item:42610},
    {n:'The Heart teleport',src:'Teletabs (excluding ones with spell versions)',x:3200,y:6942,p:1,item:36919},
    {n:'Wizard\'s tower teleport',src:'Wicked hood, rune ethereal outfit',x:3109,y:3157,p:3,item:22332},
    {n:'Air Rune - Teleport',src:'Wicked hood, rune ethereal outfit',x:3127,y:3407,p:0,item:22332},
    {n:'Water Rune - Teleport',src:'Wicked hood, rune ethereal outfit',x:3165,y:3183,p:0,item:22332},
    {n:'Earth Rune - Teleport',src:'Wicked hood, rune ethereal outfit',x:3300,y:3477,p:0,item:22332},
    {n:'Fire Rune - Teleport',src:'Wicked hood, rune ethereal outfit',x:3309,y:3253,p:0,item:22332},
    {n:'Body Rune - Teleport',src:'Wicked hood, rune ethereal outfit',x:3050,y:3444,p:0,item:22332},
    {n:'Mind Rune - Teleport',src:'Wicked hood, rune ethereal outfit',x:2981,y:3509,p:0,item:22332},
    {n:'Death Rune - Teleport',src:'Wicked hood, rune ethereal outfit',x:1863,y:4636,p:0,item:22332},
    {n:'Chaos Rune - Teleport',src:'Wicked hood, rune ethereal outfit',x:2270,y:4845,p:0,item:22332},
    {n:'Law Rune - Teleport',src:'Wicked hood, rune ethereal outfit',x:2856,y:3378,p:0,item:22332},
    {n:'Nature Rune - Teleport',src:'Wicked hood, rune ethereal outfit',x:2864,y:3023,p:0,item:22332},
    {n:'Cosmic Rune - Teleport',src:'Wicked hood, rune ethereal outfit',x:2404,y:4379,p:0,item:22332},
    {n:'Astral Rune - Teleport',src:'Wicked hood, rune ethereal outfit',x:2155,y:3866,p:0,item:22332},
    {n:'Blood Rune - Teleport',src:'Wicked hood, rune ethereal outfit',x:3559,y:9778,p:0,item:22332},
    {n:'Soul Rune - Teleport',src:'Wicked hood, rune ethereal outfit',x:2016,y:6877,p:0,item:22332},
    {n:'Green Dragons > Green Dragons',src:'Dragon trinkets',x:3303,y:5468,p:0,item:34808,kb:'1,1'},
    {n:'Green Dragons > Brutal Green Dragons',src:'Dragon trinkets',x:2512,y:3511,p:1,item:34808,kb:'1,2'},
    {n:'Blue Dragons',src:'Dragon trinkets',x:2891,y:9769,p:1,item:34808,kb:'2'},
    {n:'Red Dragons',src:'Dragon trinkets',x:2731,y:9529,p:0,item:34808,kb:'3'},
    {n:'Black Dragons > Black Dragons',src:'Dragon trinkets',x:1565,y:4356,p:0,item:34808,kb:'4,1'},
    {n:'Black Dragons > King Black Dragon',src:'Dragon trinkets',x:3051,y:3519,p:0,item:34808,kb:'4,2'},
    {n:'Black Dragons > Queen Black Dragon',src:'Dragon trinkets',x:1198,y:6499,p:0,item:34808,kb:'4,3'},
    {n:'Bronze Dragons',src:'Metallic dragon trinkets',x:2723,y:9486,p:0,item:37193,kb:'1'},
    {n:'Iron Dragons',src:'Metallic dragon trinkets',x:2694,y:9443,p:0,item:37193,kb:'2'},
    {n:'Steel Dragons',src:'Metallic dragon trinkets',x:2708,y:9468,p:0,item:37193,kb:'3'},
    {n:'Mithril Dragons',src:'Metallic dragon trinkets',x:1778,y:5346,p:0,item:37193,kb:'4'},
    {n:'Adamant Dragons',src:'Metallic dragon trinkets',x:4516,y:6046,p:0,item:37193,kb:'5'},
    {n:'Rune Dragons',src:'Metallic dragon trinkets',x:2367,y:3358,p:0,item:37193,kb:'6'},
    {n:'Incandescent [2]',src:'Elder divination outfit',x:2281,y:3049,p:0,item:35978},
    {n:'Luminous [3]',src:'Elder divination outfit',x:3314,y:2659,p:0,item:35978},
    {n:'Radiant [4]',src:'Elder divination outfit',x:3806,y:3553,p:1,item:35978},
    {n:'Brilliant [5]',src:'Elder divination outfit',x:3406,y:3295,p:0,item:35978},
    {n:'Elder [6]',src:'Elder divination outfit',x:4268,y:6318,p:0,item:35978},
    {n:'Lustrous [7]',src:'Elder divination outfit',x:3467,y:3536,p:0,item:35978},
    {n:'Vibrant [8]',src:'Elder divination outfit',x:2419,y:2862,p:0,item:35978},
    {n:'Gleaming [9]',src:'Elder divination outfit',x:2891,y:3046,p:0,item:35978},
    {n:'Sparkling [0][1]',src:'Elder divination outfit',x:2768,y:3596,p:0,item:35978},
    {n:'Glowing [0][2]',src:'Elder divination outfit',x:2733,y:3410,p:0,item:35978},
    {n:'Bright [0][3]',src:'Elder divination outfit',x:3307,y:3395,p:0,item:35978},
    {n:'Flickering [0][4]',src:'Elder divination outfit',x:3007,y:3404,p:0,item:35978},
    {n:'Pale [0][5]',src:'Elder divination outfit',x:3123,y:3215,p:0,item:35978},
    {n:'Cursed (Wilderness) [0][6]',src:'Elder divination outfit',x:3147,y:3711,p:0,item:35978},
    {n:'Goebie ranger - Katanah',src:'Goebie rangers',x:4358,y:781,p:0},
    {n:'Goebie ranger - Otot',src:'Goebie rangers',x:4199,y:929,p:0},
    {n:'Goebie ranger - World Window',src:'Goebie rangers',x:4315,y:783,p:0},
    {n:'Goebie ranger - East Nemi Forest (arrive here from Katanah only)',src:'Goebie rangers',x:4226,y:794,p:0},
    {n:'Goebie ranger - North Nemi Forest (arrive here from Otot only)',src:'Goebie rangers',x:4185,y:831,p:0},
    {n:'Goebie ranger - Forest clearing',src:'Goebie rangers',x:4722,y:781,p:0},
    {n:'Troll - Abandoned Burthorpe Mine',src:'Slayer Masks/Helms',x:2877,y:3572,p:0,item:35279,kb:'1'},
    {n:'Broken Fingers/Dead Hand - Taverley Slayer Dungeon',src:'Slayer Masks/Helms',x:2221,y:4561,p:0,item:27616,kb:'2'},
    {n:'Broken Fingers/Dead Hand - Morytania Slayer Tower (near Markus)',src:'Slayer Masks/Helms',x:3419,y:3524,p:0,item:27616,kb:'2'},
    {n:'Broken Fingers/Dead Hand - Morytania Slayer Tower (ground floor)',src:'Slayer Masks/Helms',x:3408,y:3559,p:0,item:27616,kb:'3'},
    {n:'Broken Fingers/Dead Hand - Meiyerditch Labs',src:'Slayer Masks/Helms',x:3614,y:9764,p:0,item:27616,kb:'4'},
    {n:'Mourning/Keening - Morytania Slayer Tower (near Markus)',src:'Slayer Masks/Helms',x:3419,y:3524,p:0,item:28686,kb:'1'},
    {n:'Mourning/Keening - Morytania Slayer Tower (ground floor)',src:'Slayer Masks/Helms',x:3437,y:3557,p:0,item:28686,kb:'2'},
    {n:'Mourning/Keening - Pollnivneach well',src:'Slayer Masks/Helms',x:3358,y:9427,p:0,item:28686,kb:'3'},
    {n:'Stone/Petrification',src:'Slayer Masks/Helms',x:2802,y:10026,p:0,item:27618,kb:'1'},
    {n:'Reflection/Little Kings - Fremennik Slayer Dungeon',src:'Slayer Masks/Helms',x:2750,y:9996,p:0,item:27620,kb:'1'},
    {n:'Reflection/Little Kings - Pollnivneach well',src:'Slayer Masks/Helms',x:3315,y:4336,p:0,item:27620,kb:'2'},
    {n:'Crimson/Blood - Morytania Slayer Tower (near Markus)',src:'Slayer Masks/Helms',x:3419,y:3524,p:0,item:31089,kb:'1'},
    {n:'Crimson/Blood - Morytania Slayer Tower (1st floor)',src:'Slayer Masks/Helms',x:3415,y:3549,p:1,item:31089,kb:'2'},
    {n:'Crimson/Blood - Meiyerditch Labs',src:'Slayer Masks/Helms',x:3607,y:9747,p:0,item:31089,kb:'3'},
    {n:'Gelatin/Jellyhead - Fremennik Slayer Dungeon',src:'Slayer Masks/Helms',x:2715,y:10030,p:0,item:28688,kb:'1'},
    {n:'Gelatin/Jellyhead - Chaos Tunnels',src:'Slayer Masks/Helms',x:3285,y:5460,p:0,item:28688,kb:'2'},
    {n:'Gelatin/Jellyhead - Trollweiss ice caves',src:'Slayer Masks/Helms',x:2866,y:3927,p:0,item:28688,kb:'3'},
    {n:'Stench/Foulness - Morytania Slayer Tower (near Markus)',src:'Slayer Masks/Helms',x:3419,y:3524,p:0,item:31091,kb:'1'},
    {n:'Stench/Foulness - Morytania Slayer Tower (first floor)',src:'Slayer Masks/Helms',x:3442,y:3556,p:1,item:31091,kb:'2'},
    {n:'Stench/Foulness - Pollnivneach well',src:'Slayer Masks/Helms',x:3316,y:4336,p:0,item:31091,kb:'3'},
    {n:'Dust/Devilry - Smoke Dungeon',src:'Slayer Masks/Helms',x:3225,y:9394,p:0,item:28690,kb:'1'},
    {n:'Dust/Devilry - Chaos Tunnels',src:'Slayer Masks/Helms',x:3176,y:5515,p:0,item:28690,kb:'2'},
    {n:'Dust/Devilry - Sophanem Slayer Dungeon',src:'Slayer Masks/Helms',x:3290,y:2707,p:0,item:28690,kb:'3'},
    {n:'Kura/Kuraski - Fremennik Slayer Dungeon',src:'Slayer Masks/Helms',x:2704,y:10005,p:0,item:27622,kb:'1'},
    {n:'Kura/Kuraski - Pollnivneach well',src:'Slayer Masks/Helms',x:3276,y:4370,p:0,item:27622,kb:'3'},
    {n:'Green Wyrm/Verdant Wyrm',src:'Slayer Masks/Helms',x:2461,y:2909,p:0,item:31093,kb:'1'},
    {n:'Granite/Grotesquery - Morytania Slayer Tower (near Markus)',src:'Slayer Masks/Helms',x:3419,y:3524,p:0,item:28692,kb:'1'},
    {n:'Granite/Grotesquery -Morytania Slayer Tower (top floor)',src:'Slayer Masks/Helms',x:3435,y:3553,p:2,item:28692,kb:'2'},
    {n:'Granite/Grotesquery - Chaos Tunnels',src:'Slayer Masks/Helms',x:3177,y:5471,p:0,item:28692,kb:'3'},
    {n:'Dagannoth - Lighthouse',src:'Slayer Masks/Helms',x:2510,y:3635,p:0,item:35277,kb:'1'},
    {n:'Dagannoth - Waterbirth',src:'Slayer Masks/Helms',x:2528,y:3741,p:0,item:35277,kb:'2'},
    {n:'Yellow Wyrm/Parched Wyrm',src:'Slayer Masks/Helms',x:3374,y:3154,p:0,item:31095,kb:'1'},
    {n:'Vines/Mutation - Jadinko Lair',src:'Slayer Masks/Helms',x:3034,y:9231,p:0,item:28694,kb:'1'},
    {n:'Abyss/Warping - Morytania Slayer Tower (near Markus)',src:'Slayer Masks/Helms',x:3419,y:3524,p:0,item:27624,kb:'1'},
    {n:'Abyss/Warping - Morytania Slayer Tower (top floor)',src:'Slayer Masks/Helms',x:3413,y:3549,p:2,item:27624,kb:'2'},
    {n:'Abyss/Warping - Morytania Slayer Tower Dungeon',src:'Slayer Masks/Helms',x:3435,y:3530,p:0,item:27624,kb:'3'},
    {n:'Abyss/Warping - Abyss',src:'Slayer Masks/Helms',x:3058,y:4875,p:0,item:27624,kb:'5'},
    {n:'Black Demon',src:'Slayer Masks/Helms',x:2886,y:9770,p:0,item:35287,kb:'1'},
    {n:'Automatons',src:'Slayer Masks/Helms',x:1845,y:5987,p:0,item:35283,kb:'1'},
    {n:'Aquanites',src:'Slayer Masks/Helms',x:2701,y:9980,p:0,item:35289,kb:'1'},
    {n:'Gloom/Darkness - Mourning Tunnels',src:'Slayer Masks/Helms',x:2029,y:4661,p:0,item:31099,kb:'1'},
    {n:'Gloom/Darkness - Morytania Slayer Tower Dungeon',src:'Slayer Masks/Helms',x:3435,y:3530,p:0,item:31099,kb:'3'},
    {n:'Airut',src:'Slayer Masks/Helms',x:2274,y:3613,p:0,item:35285,kb:'1'},
    {n:'White Wyrm/Frozen Wyrm',src:'Slayer Masks/Helms',x:2736,y:3731,p:0,item:31097,kb:'1'},
    {n:'Ganodermics',src:'Slayer Masks/Helms',x:4618,y:5394,p:0,item:35281,kb:'1'},
    {n:'Hand holds',src:'Brimhaven Agility Arena',x:2784,y:9544,p:3},
    {n:'Stepping stone (Jump)',src:'Penguin Agility Course',x:2635,y:4061,p:1},
    {n:'Climbing Rocks (Climb)',src:'Agility Pyramid',x:3043,y:4697,p:3},
    {n:'Iorwerth',src:'Master camouflage outfit',x:2186,y:3312,p:1,item:37358,kb:'1',rq:'todo'},
    {n:'Ithell',src:'Master camouflage outfit',x:2157,y:3340,p:1,item:37358,kb:'2',rq:'todo'},
    {n:'Cadarn',src:'Master camouflage outfit',x:2261,y:3339,p:1,item:37358,kb:'3',rq:'todo'},
    {n:'Amlodd',src:'Master camouflage outfit',x:2157,y:3382,p:1,item:37358,kb:'4',rq:'todo'},
    {n:'Trahaearn',src:'Master camouflage outfit',x:2231,y:3312,p:1,item:37358,kb:'5',rq:'todo'},
    {n:'Hefin',src:'Master camouflage outfit',x:2185,y:3410,p:1,item:37358,kb:'6',rq:'todo'},
    {n:'Crwys',src:'Master camouflage outfit',x:2263,y:3384,p:1,item:37358,kb:'7',rq:'todo'},
    {n:'Meilyr',src:'Master camouflage outfit',x:2232,y:3410,p:1,item:37358,kb:'8',rq:'todo'},
    {n:'Pyramid Plunder',src:'Master camouflage outfit',x:3291,y:2808,p:0,item:37358,kb:'9',rq:'todo'},
    {n:'The Consortium',src:'Master camouflage outfit',x:2885,y:10199,p:1,item:37358,kb:'0,1',rq:'todo'},
    {n:'Lletya',src:'Master camouflage outfit',x:2333,y:3172,p:0,item:37358,kb:'0,2',rq:'todo'},
    {n:'Thieving Guild',src:'Master camouflage outfit',x:3223,y:3269,p:0,item:37358,kb:'0,3',rq:'todo'},
    {n:'Pollnivneach',src:'Master camouflage outfit',x:3362,y:2975,p:0,item:37358,kb:'0,4',rq:'todo'},
    {n:'Tracking > Polar Kebbit',src:'Volcanic Trapper outfit',x:2711,y:3824,p:1,item:41023,kb:'1,1'},
    {n:'Tracking > Common Kebbit',src:'Volcanic Trapper outfit',x:2336,y:3576,p:0,item:41023,kb:'1,2'},
    {n:'Tracking > Feldip Weasel',src:'Volcanic Trapper outfit',x:2525,y:2890,p:0,item:41023,kb:'1,3'},
    {n:'Tracking > Desert Devil',src:'Volcanic Trapper outfit',x:3409,y:3119,p:0,item:41023,kb:'1,4'},
    {n:'Tracking > Penguin',src:'Volcanic Trapper outfit',x:2695,y:4011,p:1,item:41023,kb:'1,5'},
    {n:'Tracking > Razor-backed Kebbit',src:'Volcanic Trapper outfit',x:2356,y:3604,p:0,item:41023,kb:'1,6'},
    {n:'Tracking > Shadow Jadinko',src:'Volcanic Trapper outfit',x:2953,y:2933,p:0,item:41023,kb:'1,7'},
    {n:'Tracking > Diseased Jadinko',src:'Volcanic Trapper outfit',x:2953,y:2933,p:0,item:41023,kb:'1,8'},
    {n:'Tracking > Camouflaged Jadinko',src:'Volcanic Trapper outfit',x:2953,y:2933,p:0,item:41023,kb:'1,9'},
    {n:'Butterfly Netting > Ruby Harvest',src:'Volcanic Trapper outfit',x:2335,y:3587,p:0,item:41023,kb:'2,1'},
    {n:'Butterfly Netting > Sapphire Glacialis',src:'Volcanic Trapper outfit',x:2716,y:3804,p:1,item:41023,kb:'2,2'},
    {n:'Butterfly Netting > Snowy Knight',src:'Volcanic Trapper outfit',x:2721,y:3781,p:0,item:41023,kb:'2,3'},
    {n:'Butterfly Netting  > Black warlock',src:'Volcanic Trapper outfit',x:2558,y:2914,p:0,item:41023,kb:'2,4'},
    {n:'Butterfly Netting > Charming Moth',src:'Volcanic Trapper outfit',x:3367,y:3742,p:0,item:41023,kb:'2,5',rq:'(lvl 28 Wilderness)'},
    {n:'Box Trapping > Ferret',src:'Volcanic Trapper outfit',x:2310,y:3520,p:0,item:41023,kb:'3,1'},
    {n:'Box Trapping > Red Gecko',src:'Volcanic Trapper outfit',x:2850,y:2911,p:0,item:41023,kb:'3,2'},
    {n:'Box Trapping > Orange Gecko',src:'Volcanic Trapper outfit',x:2916,y:3080,p:0,item:41023,kb:'3,3'},
    {n:'Box Trapping > Green Gecko',src:'Volcanic Trapper outfit',x:2925,y:3097,p:0,item:41023,kb:'3,4'},
    {n:'Box Trapping > Blue gecko',src:'Volcanic Trapper outfit',x:2833,y:3049,p:0,item:41023,kb:'3,5'},
    {n:'Box Trapping > Raccoon',src:'Volcanic Trapper outfit',x:3060,y:3423,p:0,item:41023,kb:'3,6'},
    {n:'Box Trapping > Cobalt Skillchompa',src:'Volcanic Trapper outfit',x:2455,y:3537,p:0,item:41023,kb:'3,7'},
    {n:'Box Trapping > Monkey',src:'Volcanic Trapper outfit',x:2787,y:3004,p:0,item:41023,kb:'3,8'},
    {n:'Box Trapping > Rabbits',src:'Volcanic Trapper outfit',x:2331,y:3531,p:0,item:41023,kb:'3,9,1'},
    {n:'Box Trapping > Viridian Skillchompa',src:'Volcanic Trapper outfit',x:3663,y:3436,p:0,item:41023,kb:'3,9,2'},
    {n:'Box Trapping > Platypus',src:'Volcanic Trapper outfit',x:2542,y:2821,p:0,item:41023,kb:'3,9,3'},
    {n:'Box Trapping > Chinchompa',src:'Volcanic Trapper outfit',x:2335,y:3587,p:0,item:41023,kb:'3,9,4'},
    {n:'Box Trapping > Penguin',src:'Volcanic Trapper outfit',x:2695,y:4011,p:1,item:41023,kb:'3,9,5'},
    {n:'Box Trapping > Carnivorous Chinchompa',src:'Volcanic Trapper outfit',x:2558,y:2914,p:0,item:41023,kb:'3,9,6'},
    {n:'Box Trapping > Pawya',src:'Volcanic Trapper outfit',x:2267,y:3239,p:0,item:41023,kb:'3,9,7'},
    {n:'Box Trapping > Azure Skillchompa',src:'Volcanic Trapper outfit',x:2731,y:3871,p:0,item:41023,kb:'3,9,8'},
    {n:'Box Trapping > Grenwall',src:'Volcanic Trapper outfit',x:2267,y:3239,p:0,item:41023,kb:'3,9,9,1'},
    {n:'Box Trapping > Crimson Skillchompa',src:'Volcanic Trapper outfit',x:3171,y:2867,p:0,item:41023,kb:'3,9,9,2'},
    {n:'Box Trapping > Crystal Skillchompa',src:'Volcanic Trapper outfit',x:2168,y:3192,p:0,item:41023,kb:'3,9,9,3'},
    {n:'Bird Snaring > Crimson Swift',src:'Volcanic Trapper outfit',x:2600,y:2928,p:0,item:41023,kb:'4,1'},
    {n:'Bird Snaring >  Golden Warbler',src:'Volcanic Trapper outfit',x:3410,y:3098,p:0,item:41023,kb:'4,2'},
    {n:'Bird Snaring > Copper Longtail',src:'Volcanic Trapper outfit',x:2335,y:3587,p:0,item:41023,kb:'4,3'},
    {n:'Bird Snaring > Cerulean Twitch',src:'Volcanic Trapper outfit',x:2721,y:3781,p:0,item:41023,kb:'4,4'},
    {n:'Bird Snaring > Tropical wagtail',src:'Volcanic Trapper outfit',x:2544,y:2880,p:0,item:41023,kb:'4,5'},
    {n:'Bird Snaring >  Wimpy Bird',src:'Volcanic Trapper outfit',x:2505,y:2822,p:0,item:41023,kb:'4,6'},
    {n:'Pit Falling > Spined Larupia',src:'Volcanic Trapper outfit',x:2565,y:2914,p:0,item:41023,kb:'5,1'},
    {n:'Pit Falling > Horned Graahk',src:'Volcanic Trapper outfit',x:2776,y:3006,p:0,item:41023,kb:'5,2'},
    {n:'Pit Falling >  Sabre-toothed Kyatt',src:'Volcanic Trapper outfit',x:2732,y:3786,p:0,item:41023,kb:'5,3'},
    {n:'Deadfall > Wild Kebbit',src:'Volcanic Trapper outfit',x:2339,y:3553,p:0,item:41023,kb:'6,1'},
    {n:'Deadfall > Barb-tailed Kebbit',src:'Volcanic Trapper outfit',x:2573,y:2928,p:0,item:41023,kb:'6,2'},
    {n:'Deadfall > Prickly Kebbit',src:'Volcanic Trapper outfit',x:2317,y:3602,p:0,item:41023,kb:'6,3'},
    {n:'Deadfall > Diseased Kebbit',src:'Volcanic Trapper outfit',x:2505,y:2822,p:0,item:41023,kb:'6,4'},
    {n:'Net Trapping > Green salamander',src:'Volcanic Trapper outfit',x:3552,y:3441,p:0,item:41023,kb:'7,1'},
    {n:'Net Trapping > Squirrel',src:'Volcanic Trapper outfit',x:2976,y:3443,p:0,item:41023,kb:'7,2'},
    {n:'Net Trapping > Orange Salamander',src:'Volcanic Trapper outfit',x:3410,y:3098,p:0,item:41023,kb:'7,3'},
    {n:'Net Trapping >  Penguin',src:'Volcanic Trapper outfit',x:2695,y:4011,p:1,item:41023,kb:'7,4'},
    {n:'Net Trapping > Red Salamander',src:'Volcanic Trapper outfit',x:2457,y:3221,p:0,item:41023,kb:'7,5'},
    {n:'Net Trapping > Black Salamander',src:'Volcanic Trapper outfit',x:3321,y:3664,p:0,item:41023,kb:'7,6'},
    {n:'Falconry',src:'Volcanic Trapper outfit',x:2378,y:3605,p:0,item:41023,kb:'8'},
    {n:'Normal Trees[1] > West Varrock [1]',src:'Nature\'s sentinel outfit',x:3135,y:3430,p:0,item:39745},
    {n:'Normal Trees[1] > East Varrock [2]',src:'Nature\'s sentinel outfit',x:3290,y:3475,p:0,item:39745},
    {n:'Oak Trees [2] > West Varrock [1]',src:'Nature\'s sentinel outfit',x:3166,y:3415,p:0,item:39745},
    {n:'Oak Trees [2] > East Varrock [2]',src:'Nature\'s sentinel outfit',x:3277,y:3475,p:0,item:39745},
    {n:'Willow Trees [3] > Draynor [1]',src:'Nature\'s sentinel outfit',x:3090,y:3232,p:0,item:39745},
    {n:'Willow Trees [3] > Catherby [2]',src:'Nature\'s sentinel outfit',x:2783,y:3429,p:0,item:39745},
    {n:'Willow Trees [3]  > Barbarian Outpost [3]',src:'Nature\'s sentinel outfit',x:2520,y:3579,p:0,item:39745},
    {n:'Maple Trees [4] > Seers\' [1]',src:'Nature\'s sentinel outfit',x:2728,y:3501,p:0,item:39745},
    {n:'Maple Trees [4] > Daemonheim Peninsula Resource Dungeon [2]',src:'Nature\'s sentinel outfit',x:3500,y:3626,p:0,item:39745},
    {n:'Yew Trees [5] > Seers\' Graveyard [1]',src:'Nature\'s sentinel outfit',x:2708,y:3463,p:0,item:39745},
    {n:'Yew Trees [5] > West Catherby [2]',src:'Nature\'s sentinel outfit',x:2757,y:3431,p:0,item:39745},
    {n:'Yew Trees [5] > Edgeville [3]',src:'Nature\'s sentinel outfit',x:3087,y:3475,p:0,item:39745},
    {n:'Yew Trees [5] > Varrock Palace [4]',src:'Nature\'s sentinel outfit',x:3208,y:3438,p:0,item:39745},
    {n:'Yew Trees [5] > Crwys sector [5]',src:'Nature\'s sentinel outfit',x:2261,y:3383,p:1,item:39745},
    {n:'Magic Trees [6] > East Ranging Guild [1]',src:'Nature\'s sentinel outfit',x:2694,y:3425,p:0,item:39745},
    {n:'Magic Trees [6] > Sorcerer\'s Tower [2]',src:'Nature\'s sentinel outfit',x:2702,y:3397,p:0,item:39745},
    {n:'Magic Trees [6] > Mage Training Arena [3]',src:'Nature\'s sentinel outfit',x:3357,y:3310,p:0,item:39745},
    {n:'Magic Trees [6] > South Tirannwn [4]',src:'Nature\'s sentinel outfit',x:2287,y:3140,p:0,item:39745},
    {n:'Magic Trees [6] > Crwys sector [5]',src:'Nature\'s sentinel outfit',x:2249,y:3367,p:1,item:39745},
    {n:'Elder Trees [7] > East Sorcerer\'s Tower [1]',src:'Nature\'s sentinel outfit',x:2734,y:3401,p:0,item:39745},
    {n:'Elder Trees [7] > South Yanille [2]',src:'Nature\'s sentinel outfit',x:2572,y:3063,p:0,item:39745},
    {n:'Elder Trees [7] > Tree Gnome Stronghold [3]',src:'Nature\'s sentinel outfit',x:2425,y:3456,p:0,item:39745},
    {n:'Elder Trees [7] > South Draynor [4]',src:'Nature\'s sentinel outfit',x:3094,y:3215,p:0,item:39745},
    {n:'Elder Trees [7] > Falador Farm [5]',src:'Nature\'s sentinel outfit',x:3052,y:3320,p:0,item:39745},
    {n:'Elder Trees [7] > South Varrock [6]',src:'Nature\'s sentinel outfit',x:3259,y:3370,p:0,item:39745},
    {n:'Elder Trees [7] > West Lletya [7]',src:'Nature\'s sentinel outfit',x:2294,y:3147,p:0,item:39745},
    {n:'Elder Trees [7] > Piscatoris [8]',src:'Nature\'s sentinel outfit',x:2321,y:3598,p:0,item:39745},
    {n:'Elder Trees [7][9] > South Edgeville [1]',src:'Nature\'s sentinel outfit',x:3094,y:3451,p:0,item:39745},
    {n:'Elder Trees [7][9] > North Rimmington [2]',src:'Nature\'s sentinel outfit',x:2933,y:3227,p:0,item:39745},
    {n:'Teak Trees [0][1] > Tai Bwo Wannai [1]',src:'Nature\'s sentinel outfit',x:2815,y:3084,p:0,item:39745},
    {n:'Teak Trees [0][1] > Ape Atoll [2]',src:'Nature\'s sentinel outfit',x:2774,y:2696,p:0,item:39745},
    {n:'Teak Trees [0][1] > South-west Castle Wars [3]',src:'Nature\'s sentinel outfit',x:2334,y:3048,p:0,item:39745},
    {n:'Mahogany Trees [0][2] > Tai Bwo Wannai [1]',src:'Nature\'s sentinel outfit',x:2815,y:3084,p:0,item:39745},
    {n:'Mahogany Trees [0][2] > Ape Atoll [2]',src:'Nature\'s sentinel outfit',x:2716,y:2708,p:0,item:39745},
    {n:'Mahogany Trees [0][2] > Kharazi Jungle [3]',src:'Nature\'s sentinel outfit',x:2932,y:2928,p:0,item:39745},
    {n:'Arctic Pine Trees [0][3]',src:'Nature\'s sentinel outfit',x:2355,y:3848,p:0,item:39745},
    {n:'Acadia Trees [0][4]',src:'Nature\'s sentinel outfit',x:3186,y:2720,p:0,item:39745},
    {n:'Choking Ivy [0][5] > North Varrock Palace [1]',src:'Nature\'s sentinel outfit',x:3217,y:3499,p:0,item:39745},
    {n:'Choking Ivy [0][5] > East Varrock Palace [2]',src:'Nature\'s sentinel outfit',x:3232,y:3460,p:0,item:39745},
    {n:'Choking Ivy [0][5] > North Falador [3]',src:'Nature\'s sentinel outfit',x:3015,y:3394,p:0,item:39745},
    {n:'Choking Ivy [0][5] > South Falador [4]',src:'Nature\'s sentinel outfit',x:3047,y:3326,p:0,item:39745},
    {n:'Choking Ivy [0][5] > South-east Taverly [5]',src:'Nature\'s sentinel outfit',x:2938,y:3428,p:0,item:39745},
    {n:'Choking Ivy [0][5] > East Ardougne Church [6]',src:'Nature\'s sentinel outfit',x:2623,y:3307,p:0,item:39745},
    {n:'Choking Ivy [0][5] > North Yanille [7]',src:'Nature\'s sentinel outfit',x:2594,y:3112,p:0,item:39745},
    {n:'Choking Ivy [0][5] > South Castle Wars [8]',src:'Nature\'s sentinel outfit',x:2429,y:3062,p:0,item:39745},
    {n:'Choking Ivy [0][5] > Crwys sector [9]',src:'Nature\'s sentinel outfit',x:2241,y:3375,p:2,item:39745},
    {n:'Overgrown Idols [0][6] > West of the Karamja shipyard [1]',src:'Nature\'s sentinel outfit',x:2932,y:3026,p:0,item:39745},
    {n:'Overgrown Idols [0][6] > North of the Jadinka vine cave [2]',src:'Nature\'s sentinel outfit',x:2949,y:2977,p:0,item:39745},
    {n:'Liberation of Mazcab',src:'Grouping System',x:4317,y:822,p:0},
    {n:'Elite Dungeon 1',src:'Grouping System',x:2094,y:11352,p:0},
    {n:'Elite Dungeon 2',src:'Grouping System',x:3372,y:3877,p:0},
    {n:'Elite Dungeon 3',src:'Grouping System',x:3510,y:3691,p:0},
    {n:'Dominion Tower',src:'Grouping System',x:3742,y:6428,p:0},
    {n:'Anima Islands',src:'Grouping System',x:3104,y:3021,p:3},
    {n:'Solak',src:'Grouping System',x:1373,y:5640,p:0},
    {n:'Dungeoneering',src:'Grouping System',x:3450,y:3728,p:0},
    {n:'Shifting Tombs',src:'Grouping System',x:2074,y:6946,p:0,rq:'50 Agility, The Jack of Spades'},
    {n:'Clan Wars (safe)',src:'Grouping System',x:2993,y:9679,p:0},
    {n:'Fist of Guthix',src:'Grouping System',x:1689,y:5600,p:0},
    {n:'TzHaar Fight Pit',src:'Grouping System',x:4600,y:5062,p:0},
    {n:'Clan Wars (dangerous)',src:'Grouping System',x:2993,y:9679,p:0},
    {n:'Deathmatch',src:'Grouping System',x:3119,y:3517,p:0},
    {n:'Burthorpe Games Room',src:'Grouping System',x:2208,y:4949,p:0},
    {n:'Conquest',src:'Grouping System',x:2645,y:2678,p:0,rq:'Conquest Tutorial'},
    {n:'Fishing Trawler',src:'Grouping System',x:2663,y:3161,p:0},
    {n:'Flash Powder Factory',src:'Grouping System',x:3039,y:4964,p:0,rq:'75 Agility, 75 thieving'},
    {n:'Shades of Mort\'ton',src:'Grouping System',x:3506,y:3317,p:0,rq:'Priest in Peril, Shades of Mort\'ton'},
    {n:'The Great Orb Project',src:'Grouping System',x:1698,y:5464,p:2,rq:'50 Runecrafting'},
    {n:'Trouble Brewing',src:'Grouping System',x:3814,y:3022,p:0,rq:'40 Cooking, Cabin Fever'},
    {n:'Vinesweeper',src:'Grouping System',x:1637,y:4707,p:0},
    {n:'Araxxor',src:'Grouping System',x:3700,y:3418,p:0,rq:'Morytania access'},
    {n:'Corporeal Beast',src:'Grouping System',x:2884,y:4373,p:2,sp:8133,rq:'Summer\'s End'},
    {n:'Dagannoth Kings',src:'Grouping System',x:2546,y:3758,p:0,sp:2883},
    {n:'Giant Mole',src:'Grouping System',x:2989,y:3380,p:0,sp:18932},
    {n:'God Wars Dungeon',src:'Grouping System',x:2884,y:3668,p:0},
    {n:'Gregorovic',src:'Grouping System',x:3276,y:7051,p:1,sp:22442},
    {n:'Helwyr',src:'Grouping System',x:3266,y:6907,p:1,sp:22438},
    {n:'Kalphite King',src:'Grouping System',x:3232,y:2857,p:0,sp:16697},
    {n:'Kalphite Queen',src:'Grouping System',x:3227,y:3103,p:0,sp:1158},
    {n:'King Black Dragon',src:'Grouping System',x:3051,y:3518,p:0,sp:50},
    {n:'Nex: Angel of Death',src:'Grouping System',x:2884,y:3668,p:0},
    {n:'Twin Furies',src:'Grouping System',x:3131,y:7044,p:1,sp:22453},
    {n:'Vindicta and Gorvek',src:'Grouping System',x:3122,y:6901,p:1,rq:'80 Attack'},
    {n:'Castle Wars',src:'Grouping System',x:2444,y:3088,p:0},
    {n:'Stealing Creation',src:'Grouping System',x:2968,y:9699,p:0},
    {n:'Soul Wars',src:'Grouping System',x:1890,y:3177,p:0},
    {n:'Pest Control',src:'Grouping System',x:2649,y:2648,p:0},
    {n:'Blast Furnace',src:'Grouping System',x:1940,y:4961,p:0},
    {n:'Heist',src:'Grouping System',x:2949,y:3419,p:0},
    {n:'Barrows: Rise of the Six',src:'Grouping System',x:3540,y:3307,p:0,sp:18538},
    {n:'Barbarian Assault',src:'Grouping System',x:2533,y:3571,p:0},
    {n:'Vorago',src:'Grouping System',x:2972,y:3428,p:0,sp:17182},
    {n:'Cabbage Facepunch Bonanza',src:'Grouping System',x:2994,y:9700,p:0},
    {n:'Modified farmer\'s hat',src:'Modified skilling outfits',x:3598,y:3523,p:0,item:34926},
    {n:'Modified artisan\'s bandana',src:'Modified skilling outfits',x:2933,y:3290,p:0,item:32281},
    {n:'Modified blacksmith\'s helmet',src:'Modified skilling outfits',x:3033,y:3339,p:0,item:32280},
    {n:'Modified diviner\'s headwear',src:'Modified skilling outfits',x:1923,y:5987,p:1,item:32279},
    {n:'Modified botanist\'s mask',src:'Modified skilling outfits',x:2790,y:3463,p:0,item:34923},
    {n:'Modified sous chef\'s toque',src:'Modified skilling outfits',x:3143,y:3441,p:0,item:34924},
    {n:'Modified shaman\'s headdress',src:'Modified skilling outfits',x:2523,y:3061,p:0,item:32278},
    {n:'Copper/Tin - Burthorpe mine',src:'Resource locator (random destination for each resource)',x:2279,y:4509,p:0,item:15005},
    {n:'Copper/Tin - Lumbridge south-east mine',src:'Resource locator (random destination for each resource)',x:3226,y:3148,p:0,item:15005},
    {n:'Clay - Burthorpe mine',src:'Resource locator (random destination for each resource)',x:2279,y:4509,p:0,item:15006},
    {n:'Clay - Lumbridge south-east mine',src:'Resource locator (random destination for each resource)',x:3226,y:3148,p:0,item:15006},
    {n:'Iron - Lumbridge south-west mine',src:'Resource locator (random destination for each resource)',x:3146,y:3147,p:0,item:15005},
    {n:'Iron - Varrock south-west mine',src:'Resource locator (random destination for each resource)',x:3181,y:3368,p:0,item:15005},
    {n:'Silver - Al Kharid mine',src:'Resource locator (random destination for each resource)',x:3299,y:3303,p:0,item:15006},
    {n:'Silver - Falador west mine',src:'Resource locator (random destination for each resource)',x:2926,y:3338,p:0,item:15006},
    {n:'Gold - Al Kharid mine',src:'Resource locator (random destination for each resource)',x:3299,y:3303,p:0,item:15007},
    {n:'Gold - Rimmington mine',src:'Resource locator (random destination for each resource)',x:3001,y:3237,p:0,item:15007},
    {n:'Mithril - Legends\' Guild south-west mine',src:'Resource locator (random destination for each resource)',x:2699,y:3332,p:0,item:15007},
    {n:'Mithril - Varrock south-east mine',src:'Resource locator (random destination for each resource)',x:3288,y:3364,p:0,item:15007},
    {n:'Adamantite - Rimmington mine',src:'Resource locator (random destination for each resource)',x:3001,y:3237,p:0,item:15008},
    {n:'Adamantite - Agility Pyramid north-west mine',src:'Resource locator (random destination for each resource)',x:3322,y:2874,p:0,item:15008},
    {n:'Runite - Fight Arena south-east mine',src:'Resource locator (random destination for each resource)',x:2628,y:3142,p:0,item:15008},
    {n:'Runite - Karamja Volcano mine',src:'Resource locator (random destination for each resource)',x:2860,y:9576,p:0,item:15008},
    {n:'Oak - West Ardougne, southeast of Carnillean mansion',src:'Resource locator (random destination for each resource)',x:2581,y:3265,p:0,item:15005},
    {n:'Oak - Gnome Stronghold, near the Spirit tree',src:'Resource locator (random destination for each resource)',x:2452,y:3448,p:0,item:15005},
    {n:'Oak - West of the Gnome Maze',src:'Resource locator (random destination for each resource)',x:2495,y:3178,p:0,item:15005},
    {n:'Oak - Tower of Life',src:'Resource locator (random destination for each resource)',x:2632,y:3224,p:0,item:15005},
    {n:'Oak - Draynor jail',src:'Resource locator (random destination for each resource)',x:3119,y:3245,p:0,item:15005},
    {n:'Oak - Lumber Yard',src:'Resource locator (random destination for each resource)',x:3294,y:3489,p:0,item:15005},
    {n:'Willow - West of the Gnome Maze',src:'Resource locator (random destination for each resource)',x:2482,y:3157,p:0,item:15005},
    {n:'Willow - South of Rantz in eastern Feldip Hills',src:'Resource locator (random destination for each resource)',x:2649,y:2958,p:0,item:15005},
    {n:'Willow - Witchaven',src:'Resource locator (random destination for each resource)',x:2702,y:3297,p:0,item:15005},
    {n:'Willow - South-east of Port Sarim Jail',src:'Resource locator (random destination for each resource)',x:3025,y:3170,p:0,item:15005},
    {n:'Willow - North of Crafting Guild',src:'Resource locator (random destination for each resource)',x:2917,y:3298,p:0,item:15005},
    {n:'Willow - Draynor village',src:'Resource locator (random destination for each resource)',x:3086,y:3232,p:0,item:15005},
    {n:'Willow - Northwest of McGrubor\'s Wood',src:'Resource locator (random destination for each resource)',x:2610,y:3502,p:0,item:15005},
    {n:'Mapple - Northwest of Sinclair Mansion',src:'Resource locator (random destination for each resource)',x:2703,y:3592,p:0,item:15006},
    {n:'Mapple - Seers\' Village courthouse',src:'Resource locator (random destination for each resource)',x:2748,y:3466,p:0,item:15006},
    {n:'Mapple - North of McGrubor\'s Wood',src:'Resource locator (random destination for each resource)',x:2654,y:3523,p:0,item:15006},
    {n:'Mapple - West of Legends\' Guild',src:'Resource locator (random destination for each resource)',x:2714,y:3368,p:0,item:15006},
    {n:'Mapple - Lighthouse island',src:'Resource locator (random destination for each resource)',x:2582,y:3598,p:0,item:15006},
    {n:'Special - Uzer',src:'Resource locator (random destination for each resource)',x:3511,y:3072,p:0,item:15006},
    {n:'Special - Far southwest of Castle Wars',src:'Resource locator (random destination for each resource)',x:2331,y:3047,p:0,item:15006},
    {n:'Special - South Ape Atoll',src:'Resource locator (random destination for each resource)',x:2774,y:2699,p:0,item:15006},
    {n:'Special - Southwest of Castle Wars',src:'Resource locator (random destination for each resource)',x:2364,y:3064,p:0,item:15006},
    {n:'Special - East Feldip Hills',src:'Resource locator (random destination for each resource)',x:2603,y:2974,p:0,item:15006},
    {n:'Special - Northwest of Oo\'glog',src:'Resource locator (random destination for each resource)',x:2515,y:2865,p:0,item:15006},
    {n:'Special - South Feldip hills, east of the Spirit tree',src:'Resource locator (random destination for each resource)',x:2440,y:2859,p:0,item:15006},
    {n:'Yew - Lumbridge graveyard',src:'Resource locator (random destination for each resource)',x:3248,y:3202,p:0,item:15007},
    {n:'Yew - East of Tyras Camp',src:'Resource locator (random destination for each resource)',x:2218,y:3138,p:0,item:15007},
    {n:'Yew - Piscatoris hunting area',src:'Resource locator (random destination for each resource)',x:2317,y:3604,p:0,item:15007},
    {n:'Yew - South of Varrock',src:'Resource locator (random destination for each resource)',x:3256,y:3362,p:0,item:15007},
    {n:'Yew - South-east of Legends\' Guild',src:'Resource locator (random destination for each resource)',x:2733,y:3336,p:0,item:15007},
    {n:'Yew - Seers\' Village church',src:'Resource locator (random destination for each resource)',x:2711,y:3462,p:0,item:15007},
    {n:'Yew - Lumber yard',src:'Resource locator (random destination for each resource)',x:3305,y:3467,p:0,item:15007},
    {n:'Magic - East of Ranging Guild',src:'Resource locator (random destination for each resource)',x:2694,y:3428,p:0,item:15008},
    {n:'Magic - Southeast Isafdar',src:'Resource locator (random destination for each resource)',x:2287,y:3140,p:0,item:15008},
    {n:'Magic - West Tree Gnome Stronghold',src:'Resource locator (random destination for each resource)',x:2438,y:3410,p:0,item:15008},
    {n:'Magic - Mage Training Arena',src:'Resource locator (random destination for each resource)',x:3372,y:3312,p:0,item:15008},
    {n:'Magic - East Tree Gnome Stronghold',src:'Resource locator (random destination for each resource)',x:2493,y:3416,p:0,item:15008},
    {n:'Elder - South of Draynor Village bank',src:'Resource locator (random destination for each resource)',x:3100,y:3217,p:0,item:15008},
    {n:'Elder - Directly south of Melzar\'s Maze, west of POH Portal in Rimmington',src:'Resource locator (random destination for each resource)',x:2937,y:3228,p:0,item:15008},
    {n:'Elder - East of the Sorcerer\'s Tower, north-east of the Legends\' Guild',src:'Resource locator (random destination for each resource)',x:2733,y:3402,p:0,item:15008},
    {n:'Elder - North-west of Tree Gnome Stronghold bank',src:'Resource locator (random destination for each resource)',x:2432,y:3459,p:0,item:15008},
    {n:'Elder - South of Edgeville, near the Stronghold of Safety.',src:'Resource locator (random destination for each resource)',x:3093,y:3451,p:0,item:15008},
    {n:'Elder - North of Falador Farm, south-east of Falador.',src:'Resource locator (random destination for each resource)',x:3053,y:3320,p:0,item:15008},
    {n:'Elder - Piscatoris hunter area',src:'Resource locator (random destination for each resource)',x:2316,y:3603,p:0,item:15008},
    {n:'Elder - South of Yanille',src:'Resource locator (random destination for each resource)',x:2571,y:3058,p:0,item:15008},
    {n:'Herblore secondaries 1 - East of Rellekka',src:'Resource locator (random destination for each resource)',x:2706,y:3668,p:0,item:15005},
    {n:'Herblore secondaries 1 - Catherby',src:'Resource locator (random destination for each resource)',x:2785,y:3463,p:0,item:15005},
    {n:'Herblore secondaries 1 - South-west of the Kalphite Hive',src:'Resource locator (random destination for each resource)',x:3215,y:3083,p:0,item:15005},
    {n:'Herblore secondaries 1 - West of Nardah bank',src:'Resource locator (random destination for each resource)',x:3406,y:2886,p:0,item:15005},
    {n:'Herblore secondaries 1 - Skavid Caves',src:'Resource locator (random destination for each resource)',x:2532,y:9466,p:0,item:15005},
    {n:'Herblore secondaries 1 - Varrock Sewers',src:'Resource locator (random destination for each resource)',x:3177,y:9885,p:0,item:15005},
    {n:'Herblore secondaries 1 - Edgeville Dungeon',src:'Resource locator (random destination for each resource)',x:3126,y:9951,p:0,item:15005},
    {n:'Herblore secondaries 1 - North Gnome Stronghold',src:'Resource locator (random destination for each resource)',x:2419,y:3508,p:0,item:15005},
    {n:'Herblore secondaries 1 - Lumbridge Swamp, north of the Nexus',src:'Resource locator (random destination for each resource)',x:3218,y:3191,p:0,item:15005},
    {n:'Herblore secondaries 1 - Mort Myre, south of the fairy ring',src:'Resource locator (random destination for each resource)',x:3466,y:3402,p:0,item:15005},
    {n:'Herblore secondaries 1 - North of Witch\'s House',src:'Resource locator (random destination for each resource)',x:2905,y:3392,p:0,item:15005},
    {n:'Herblore secondaries 2 - Red dragon isle',src:'Resource locator (random destination for each resource)',x:3217,y:3810,p:1,item:15007},
    {n:'Herblore secondaries 2 - Isafdar',src:'Resource locator (random destination for each resource)',x:2257,y:3167,p:0,item:15007},
    {n:'Herblore secondaries 2 - Taverly dungeon blue dragons',src:'Resource locator (random destination for each resource)',x:2905,y:9805,p:0,item:15007},
    {n:'Herblore secondaries 2 - Kalphite Hive',src:'Resource locator (random destination for each resource)',x:3415,y:9484,p:0,item:15007},
    {n:'Herblore secondaries 2 - Captured Temple south of Goblin Village',src:'Resource locator (random destination for each resource)',x:2953,y:3474,p:0,item:15007},
    {n:'Herblore secondaries 2 - Hobgoblin peninsula west of the Crafting guild',src:'Resource locator (random destination for each resource)',x:2907,y:3293,p:0,item:15007},
    {n:'Herblore secondaries 2 - Waterbirth island next to the boat',src:'Resource locator (random destination for each resource)',x:2544,y:3758,p:0,item:15007},
    {n:'Herblore secondaries 2 - Ogre island west of Yanille',src:'Resource locator (random destination for each resource)',x:2512,y:3081,p:0,item:15007},
    {n:'Fish 1 - Coastline south of Rimmington',src:'Resource locator (random destination for each resource)',x:2987,y:3180,p:0,item:15005},
    {n:'Fish 1 - East Lumbridge Swamp',src:'Resource locator (random destination for each resource)',x:3241,y:3160,p:0,item:15005},
    {n:'Fish 1 - Draynor village',src:'Resource locator (random destination for each resource)',x:3089,y:3233,p:0,item:15005},
    {n:'Fish 1 - Gunnarsgrunn',src:'Resource locator (random destination for each resource)',x:3102,y:3429,p:0,item:15005},
    {n:'Fish 1 - Lumbridge, near goblin house',src:'Resource locator (random destination for each resource)',x:3240,y:3249,p:0,item:15005},
    {n:'Fish 1 - Northwest of Sinclair mansion',src:'Resource locator (random destination for each resource)',x:2685,y:3597,p:0,item:15005},
    {n:'Fish 1 - East of Lumbridge church',src:'Resource locator (random destination for each resource)',x:3255,y:3206,p:0,item:15005},
    {n:'Fish 2 - Coastline south of Rimmington',src:'Resource locator (random destination for each resource)',x:2987,y:3180,p:0,item:15007},
    {n:'Fish 2 - Draynor village',src:'Resource locator (random destination for each resource)',x:3089,y:3233,p:0,item:15007},
    {n:'Fish 2 - Lumbridge, near goblin house',src:'Resource locator (random destination for each resource)',x:3240,y:3249,p:0,item:15007},
    {n:'Fish 2 - Gunnarsgrunn',src:'Resource locator (random destination for each resource)',x:3102,y:3429,p:0,item:15007},
    {n:'Fish 2 - West of the Gnome Maze',src:'Resource locator (random destination for each resource)',x:2476,y:3154,p:0,item:15007},
    {n:'Fish 2 - Chaos druid tower north of Ardougne',src:'Resource locator (random destination for each resource)',x:2563,y:3368,p:0,item:15007},
    {n:'Fish 2 - Hemenster',src:'Resource locator (random destination for each resource)',x:2645,y:3446,p:0,item:15007},
    {n:'Fish 2 - Musa Point',src:'Resource locator (random destination for each resource)',x:2925,y:3174,p:0,item:15007},
  ];
  // Sheet requirement text for display; 'todo' is the sheet's own placeholder, so it
  // surfaces as "unverified" rather than leaking the raw marker into tooltips.
  function teleRqText(T) { return T.rq === 'todo' ? 'unverified' : (T.rq || ''); }
  // Keybind for a teleport marker: the explicit kb field, else the bracketed tokens many
  // sheet rows carry in the NAME ("Vibrant [8]", "Choking Ivy [0][5] > Church [6]" -> "0,5,6").
  function teleKb(T) {
    if (T.kb) return T.kb;
    const m = String(T.n || '').match(/\[([^\]]+)\]/g);
    return m ? m.map(function (s) { return s.slice(1, -1); }).join(',') : '';
  }
  // Grouping System rows have no item to icon from; all share the game's grouping-teleport
  // badge (runescape.wiki Teleport_group_icon.png, embedded so it works offline).
  const TELE_GROUP_ICON = 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABQAAAAUCAYAAACNiR0NAAAD5klEQVR4nK2UW0ybZRzGfwWmLaVAKTBB2oHhUCgZMAhDGMvc4mKmsKCBXTgl6lhmvDDZDF4YL4x30y3RxGg2EjNjYjZwY8ws26KoJYxxmBTiOHSVY+kHXw8fLf0ohxS8IHwOJ4kXe67eN/+8v/zzPG8eeMJSbTf45tpQybPpObXAIcAIrABOwDozMdp8qiav738BT5w5n3L0+PsXQqHFV6b7bCxMuRA9bp7WqInT6tCZUjGWFKLRRP90/fsvTjadOy1sC/zq2/4iU+HuNnvH3bSpzh4AUvcUEJNsACAoenH9MQCAqaKU7Mpy55RtsPq9t4r6HwOeOHM+9fBrDf1DV28mS9NOTBWlGEsKcY86CIpeAGKSDSTlZOIedWC/1Y7emEbeq0fEOz9eLGo6d9q1BXjl7sL18Y7uaoe1g9319UTIfuy32lkMBrZYEh0TS/ZLB1nTxjF46RKZ+yvJqNzbVleuOwoQAfBp02Cx3xesdlg7yNxfSYTsx9bSiijN4ZEE9KpJdhXOsL5DQJTmsLW0EiH7ydxficPagd8XrD73ZcceBWhMT3vd3rsRWoLFgq2lFY8kYHxKpL5slX3xa7zxsp6PG+PI3vUQjyRga2klwWIBwN7bhyHPclwBri4tlwt/OYgxGQmMOfBIAup1kX3xa6gnAoREkZVFJyrVPO+8nYh6XcQjCfgePECXa2ZlxM7q0nK5AgTSwqJAVPJOZqacil8hUcQflJX7shxkWQ6Snb9h/ZzbTaQ2BlGaA0gDiAJYWgythMJhwnKQdVnGK/kwlC0zdFCDvLYEwNdXLQp4dWCUyAUf6bJMGAiFwywthpYf3dCliYxEnJ1FpdUCMDmwMdJGqPm3Vt0b30il1SLOzqKJjAQQFOB8IGjVZGQhjw2jN29sEiWt0/6DS9lwU4F7XcpZb7Ygjw2jychiPhC0KsBpr6050VyAV/Ix3ttNTlUtAAuD0fz80RLdl30E7nUx3/YLa+IiADlVtYz3duOVfCSaCwhMTzYrwAsfvtmv06lvmIorcFhvA5B/uIZYnUEBb4JidQbyD9cA4LDeJn/vAXQ69Y3PPqnqV0IB6Glrfjf3haqyoOBMGmi+RHbFi5SdbMQzMoBbmAIgKcVEorkA1/1O7HeukfpcLjEFpe5f73x3apOzpRwaGi8X6VMy2lz3O9P+/K2NWJ2BuKxc4mITAPAHfPgfDhNY8JJ/oJrU4gqnJIxXXzx77PFy2NTBug9SSp6va1qfnzsy0t+Db3ZCSXVHkoGEZ9IxF5Wiit95s6/rSkP7lc9dj77ftmAbGi+X6FMyNgvWxD8F+7skjDdfPHvsPwv2ietvxkOuNbe1TIcAAAAASUVORK5CYII=';
  // Sheet rows confirmed wrong in-game; kept out of the merge (and out of any future
  // regeneration) instead of deleting the row, so the exclusions stay reviewable in one place.
  const MAP_TELEPORTS_DROP = [
    'Shattered Worlds teleport scroll',   // no longer teleports to the south-of-Lumbridge spot (user-verified 2026-07-30)
  ];
  // Curated entries win: skip sheet rows within 2 tiles of one (better icons + gating).
  for (const T of MAP_TELEPORTS_EXT) {
    if (MAP_TELEPORTS_DROP.indexOf(T.n) >= 0) continue;
    if (!MAP_TELEPORTS.some(c => (c.p || 0) === (T.p || 0)
        && Math.abs(c.x - T.x) <= 2 && Math.abs(c.y - T.y) <= 2)) MAP_TELEPORTS.push(T);
  }
  for (const T of MAP_TELEPORTS) if (T.src === 'Grouping System') T.iconUrl = TELE_GROUP_ICON;
  // ---- END generated mejrs teleport data ----
