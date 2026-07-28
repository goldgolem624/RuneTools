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
  const MAP_LABELS = [{"n":"Picatoris Fishing Colony","x":2335,"y":3681,"p":0},{"n":"Falconer","x":2377,"y":3598,"p":0},{"n":"Memorial to Guthix","x":2273,"y":3554,"p":0},{"n":"Eagles' Peak","x":2329,"y":3488,"p":0},{"n":"Poison Waste","x":2240,"y":3098,"p":0},{"n":"Tyras Camp","x":2188,"y":3145,"p":0},{"n":"Port Tyras","x":2154,"y":3122,"p":0},{"n":"Lletya","x":2339,"y":3172,"p":0},{"n":"Isafdar","x":2240,"y":3193,"p":0},{"n":"Elf Camp","x":2197,"y":3251,"p":0},{"n":"Arandar","x":2345,"y":3292,"p":0},{"n":"Observatory","x":2440,"y":3163,"p":0},{"n":"Battlefield","x":2517,"y":3243,"p":0},{"n":"West Ardougne","x":2523,"y":3305,"p":0},{"n":"Underground Pass Entrance","x":2435,"y":3315,"p":0},{"n":"Combat Training Camp","x":2518,"y":3370,"p":0},{"n":"Gnome Agility Training Area","x":2481,"y":3426,"p":0},{"n":"Gnome Ball Field","x":2397,"y":3489,"p":0},{"n":"Grand Tree","x":2465,"y":3494,"p":0},{"n":"Baxtorian Falls","x":2511,"y":3465,"p":0},{"n":"Barbarian outpost","x":2542,"y":3564,"p":0},{"n":"Warforge Dig Site","x":2410,"y":2838,"p":0},{"n":"Oo'glog","x":2564,"y":2849,"p":0},{"n":"Feldip Hills","x":2559,"y":2978,"p":0},{"n":"Gu'Tanoth","x":2522,"y":3038,"p":0},{"n":"Jiggig","x":2467,"y":3046,"p":0},{"n":"Yanile","x":2553,"y":3093,"p":0},{"n":"Wizards' Guild","x":2590,"y":3087,"p":0},{"n":"Fight Arena","x":2593,"y":3164,"p":0},{"n":"Tree Gnome Village","x":2529,"y":3169,"p":0},{"n":"Ardougne Monastery","x":2607,"y":3213,"p":0},{"n":"Port Khazard","x":2653,"y":3162,"p":0},{"n":"Tower of Life","x":2649,"y":3219,"p":0},{"n":"Clocktower","x":2570,"y":3242,"p":0},{"n":"East Ardougne","x":2615,"y":3306,"p":0},{"n":"Witchhaven","x":2719,"y":3285,"p":0},{"n":"Manor Farm","x":2655,"y":3357,"p":0},{"n":"Legends' Guild","x":2729,"y":3370,"p":0},{"n":"Catherby","x":2802,"y":3444,"p":0},{"n":"Sorcerer's Tower","x":2703,"y":3405,"p":0},{"n":"Stormguard Citadel Dig Site","x":2678,"y":3401,"p":0},{"n":"Ranging Guild","x":2669,"y":3430,"p":0},{"n":"Mcgrubor's Wood","x":2608,"y":3398,"p":0},{"n":"Seers Village","x":2705,"y":3483,"p":0},{"n":"Camelot","x":2758,"y":3502,"p":0},{"n":"Sinclair Mansion","x":2742,"y":3568,"p":0},{"n":"Golden Apple Tree","x":2765,"y":3609,"p":0},{"n":"Rellekka","x":2649,"y":3676,"p":0},{"n":"Keldagrim Entrance","x":2732,"y":3712,"p":0},{"n":"Rellekka Hunter Area","x":2721,"y":3785,"p":0},{"n":"Waterbirth Island","x":2534,"y":3741,"p":0},{"n":"Etceteria","x":2607,"y":3875,"p":0},{"n":"Miscellania","x":2529,"y":3867,"p":0},{"n":"Jatizso","x":2402,"y":3805,"p":0},{"n":"Neitiznot","x":2329,"y":3803,"p":0},{"n":"Lighthouse","x":2508,"y":3635,"p":0},{"n":"Void Knights' Outpost","x":2653,"y":2656,"p":0},{"n":"Ape Atoll","x":2749,"y":2749,"p":0},{"n":"Crash Island","x":2915,"y":2720,"p":0},{"n":"Kharazi Jungle","x":2857,"y":2920,"p":0},{"n":"Shilo Village","x":2849,"y":2982,"p":0},{"n":"Tai Bwo Wannai","x":2792,"y":3067,"p":0},{"n":"Musa Point","x":2908,"y":3163,"p":0},{"n":"Tzhaar City","x":2844,"y":3173,"p":0},{"n":"Brimhaven","x":2768,"y":3180,"p":0},{"n":"Crandor","x":2837,"y":3272,"p":0},{"n":"Fishing Platform","x":2774,"y":3283,"p":0},{"n":"Entrana","x":2836,"y":3361,"p":0},{"n":"Lunar Isle","x":2109,"y":3907,"p":0},{"n":"Taverley","x":2906,"y":3463,"p":0},{"n":"Heroes guild","x":2907,"y":3514,"p":0},{"n":"Burthorpe","x":2856,"y":3542,"p":0},{"n":"Warriors' Guild","x":2857,"y":3541,"p":0},{"n":"Dark Wizards' Tower","x":2907,"y":3341,"p":0},{"n":"White Wolf Mountain","x":2831,"y":3502,"p":0},{"n":"Death Plateau","x":2861,"y":3593,"p":0},{"n":"Trollheim","x":2888,"y":3673,"p":0},{"n":"Troll Stronghold","x":2830,"y":3675,"p":0},{"n":"God Wars Dungeon","x":2916,"y":3742,"p":0},{"n":"Wilderness Agility Training Area","x":2997,"y":3950,"p":0},{"n":"Pirates' Hideout","x":3041,"y":3953,"p":0},{"n":"Mage Arena","x":3104,"y":3934,"p":0},{"n":"Deserted Keep","x":3154,"y":3933,"p":0},{"n":"Lava Maze","x":3076,"y":3857,"p":0},{"n":"Red Dragon Isle","x":3199,"y":3830,"p":0},{"n":"The Forgotten Cemetary","x":2976,"y":3751,"p":0},{"n":"Wilderness Crator","x":3135,"y":3727,"p":0},{"n":"Scorpion Pit","x":3233,"y":3945,"p":0},{"n":"Chaos Elemental (boss)","x":3270,"y":3955,"p":0},{"n":"Rogues' Castle","x":3287,"y":3932,"p":0},{"n":"Volcano (Wilderness)","x":3369,"y":3950,"p":0},{"n":"Dragonkin Laboratory","x":3368,"y":3888,"p":0},{"n":"Demonic Ruins","x":3288,"y":3887,"p":0},{"n":"Ruins East","x":3228,"y":3737,"p":0},{"n":"Ruins West","x":2974,"y":3695,"p":0},{"n":"Bandit Camp","x":3039,"y":3689,"p":0},{"n":"Dark Warriors' Fortress","x":3029,"y":3633,"p":0},{"n":"Black Knights' Fortress","x":3019,"y":3558,"p":0},{"n":"Abyss Entrance","x":3100,"y":3554,"p":0},{"n":"Graveyard of Shadows","x":3225,"y":3684,"p":0},{"n":"Wilderness Chaos Alter","x":3240,"y":3610,"p":0},{"n":"Daemonheim","x":3450,"y":3711,"p":0},{"n":"Fort Forinthry","x":3306,"y":3554,"p":0},{"n":"Goblin Village","x":2956,"y":3505,"p":0},{"n":"Captured Temple","x":2950,"y":3476,"p":0},{"n":"Falador","x":2965,"y":3382,"p":0},{"n":"White Knights' Castle","x":2967,"y":3341,"p":0},{"n":"Artisans' Workshop","x":3045,"y":3340,"p":0},{"n":"Party Room","x":3046,"y":3377,"p":0},{"n":"Ice Mountain","x":3008,"y":3485,"p":0},{"n":"Dwarven Mine","x":3009,"y":3451,"p":0},{"n":"Invention Guild","x":2995,"y":3438,"p":0},{"n":"Edgeville Monastery","x":3052,"y":3490,"p":0},{"n":"Barbarian Village","x":3079,"y":3421,"p":0},{"n":"Edgeville","x":3088,"y":3491,"p":0},{"n":"Crafting Guild","x":2933,"y":3286,"p":0},{"n":"Remmington","x":2957,"y":3208,"p":0},{"n":"White Knight Camp","x":2995,"y":3237,"p":0},{"n":"Port Sarim","x":3027,"y":3222,"p":0},{"n":"Mudskipper Point","x":2995,"y":3117,"p":0},{"n":"Asgarnian Ice Dungeon Entrance","x":3008,"y":3149,"p":0},{"n":"Falador Farm","x":3034,"y":3287,"p":0},{"n":"Draynor Manor","x":3109,"y":3348,"p":0},{"n":"Draynor Village","x":3104,"y":3263,"p":0},{"n":"Wizards' Tower","x":3102,"y":3157,"p":0},{"n":"Lumbridge Swamp","x":3196,"y":3171,"p":0},{"n":"Lumbridge","x":3223,"y":3240,"p":0},{"n":"Champions' Guild","x":3192,"y":3358,"p":0},{"n":"Varrock","x":3213,"y":3429,"p":0},{"n":"Cooks' Guild","x":3143,"y":3448,"p":0},{"n":"Grand Exchange","x":3163,"y":3493,"p":0},{"n":"Infernal Source Dig Site","x":3261,"y":3503,"p":0},{"n":"Exam Centre","x":3361,"y":3347,"p":0},{"n":"Archeology Guild","x":3323,"y":3378,"p":0},{"n":"Varrock Digsite","x":3358,"y":3428,"p":0},{"n":"Silvarea","x":3364,"y":3484,"p":0},{"n":"Temple","x":3411,"y":3485,"p":0},{"n":"God Wars Dungeon 3 Entrance","x":3328,"y":3449,"p":0},{"n":"Mage Training Arena","x":3363,"y":3309,"p":0},{"n":"Garden of Kharid","x":3314,"y":3302,"p":0},{"n":"Het's Oasis","x":3364,"y":3234,"p":0},{"n":"Kharid-et Dig Site","x":3357,"y":3194,"p":0},{"n":"Citharede Abbey","x":3422,"y":3164,"p":0},{"n":"Al Kharid","x":3290,"y":3169,"p":0},{"n":"Shantay Pass","x":3304,"y":3124,"p":0},{"n":"Kalphite Hive","x":3220,"y":3115,"p":0},{"n":"Bedabin Camp","x":3169,"y":3039,"p":0},{"n":"Desert Mining Camp","x":3289,"y":3026,"p":0},{"n":"Bandit Camp","x":3175,"y":2981,"p":0},{"n":"Pollinivneach","x":3358,"y":2970,"p":0},{"n":"Quarry","x":3172,"y":2911,"p":0},{"n":"Pyramid","x":3233,"y":2898,"p":0},{"n":"Goebie Camp","x":3090,"y":2863,"p":0},{"n":"Exiled Kalphite Hive","x":3238,"y":2858,"p":0},{"n":"Workers District","x":3156,"y":2797,"p":0},{"n":"Merchant District","x":3228,"y":2782,"p":0},{"n":"Imperial District","x":3098,"y":2688,"p":0},{"n":"Port District","x":3152,"y":2641,"p":0},{"n":"Menaphos","x":3226,"y":2728,"p":0},{"n":"Sophanem","x":3299,"y":2785,"p":0},{"n":"Agility Pyramid","x":3364,"y":2840,"p":0},{"n":"God Wars Dungeon 2","x":3378,"y":2882,"p":0},{"n":"Nardah","x":3426,"y":2914,"p":0},{"n":"Uzer","x":3478,"y":3091,"p":0},{"n":"Mausoleum","x":3502,"y":3573,"p":0},{"n":"Fenkenstrain's Castle","x":3548,"y":3552,"p":0},{"n":"Slayer Tower","x":3423,"y":3538,"p":0},{"n":"Canifis","x":3492,"y":3488,"p":0},{"n":"Haunted Woods","x":3564,"y":3493,"p":0},{"n":"Ectofuntus","x":3661,"y":3519,"p":0},{"n":"Port Phasmatys","x":3667,"y":3487,"p":0},{"n":"Mort Myre Swamp","x":3436,"y":3400,"p":0},{"n":"Meiyerditch","x":3616,"y":3264,"p":0},{"n":"Barrows","x":3565,"y":3288,"p":0},{"n":"Mort'ton","x":3489,"y":3289,"p":0},{"n":"Burgh De Rott","x":3501,"y":3224,"p":0},{"n":"Abandoned Mine","x":3447,"y":3234,"p":0},{"n":"EverLight Dig Site","x":3695,"y":3208,"p":0},{"n":"Harmony","x":3797,"y":2858,"p":0},{"n":"Mos Le'Harmless","x":3711,"y":3027,"p":0},{"n":"Dragontooth Island","x":3803,"y":3546,"p":0},{"n":"Wendlewick","x":3481,"y":1557,"p":0},{"n":"Amberfell","x":3709,"y":1558,"p":0},{"n":"Anachronia","x":5432,"y":2339,"p":0},{"n":"Observation Output","x":5594,"y":2528,"p":0},{"n":"Crypt of Varanus","x":5320,"y":2414,"p":0},{"n":"Anachronia Dinosaur Farm","x":5198,"y":2373,"p":0},{"n":"Tuai Leit","x":1813,"y":11929,"p":0},{"n":"Archaeology Campus","x":3359,"y":3377,"p":0},{"n":"Death's Office","x":414,"y":674,"p":0},{"n":"Trahaearn","x":2231,"y":3311,"p":0},{"n":"Ardougne Zoo","x":2614,"y":3272,"p":0},{"n":"City of Um","x":1108,"y":1777,"p":1},{"n":"Distilleries","x":3783,"y":2999,"p":0},{"n":"Melzar's Maze","x":2937,"y":3255,"p":0},{"n":"Rimmington","x":2955,"y":3222,"p":0},{"n":"Custom's Office","x":2966,"y":3194,"p":0},{"n":"Jail","x":3125,"y":3243,"p":0},{"n":"Market","x":3081,"y":3250,"p":0},{"n":"Park","x":3004,"y":3381,"p":0},{"n":"Um Ritual Site","x":1037,"y":1774,"p":1},{"n":"The Heart","x":3200,"y":6970,"p":1},{"n":"Zaros's Bastion","x":3129,"y":6911,"p":1},{"n":"Zamorak's Rampart","x":3138,"y":7039,"p":1},{"n":"Sliske's Necropolis","x":3270,"y":7045,"p":1},{"n":"Seren's Encampment","x":3256,"y":6912,"p":1},{"n":"Skinweaver","x":4643,"y":5382,"p":0},{"n":"Commander Akhomet","x":3166,"y":2729,"p":0},{"n":"Grand Vizier Ehsan","x":3197,"y":2769,"p":0},{"n":"Admiral Wadud","x":3178,"y":2648,"p":0},{"n":"Outpost","x":2436,"y":3347,"p":0},{"n":"Tree Gnome Stronghold","x":2440,"y":3468,"p":0},{"n":"Otto's Grotto","x":2502,"y":3488,"p":0},{"n":"Swamp","x":2419,"y":3512,"p":0},{"n":"Barbarian Assault","x":2521,"y":3571,"p":0},{"n":"Fremennik Province","x":2670,"y":3629,"p":0},{"n":"Courthouse","x":2736,"y":3468,"p":0},{"n":"Flax","x":2742,"y":3443,"p":0},{"n":"Beehives","x":2759,"y":3443,"p":0},{"n":"Necromancer","x":2669,"y":3240,"p":0},{"n":"Trawler","x":2687,"y":3168,"p":0},{"n":"Castle Wars","x":2442,"y":3090,"p":0},{"n":"South Feldip Hills","x":2469,"y":2852,"p":0},{"n":"Tirannwn","x":2240,"y":3219,"p":0},{"n":"Wilderness Crater","x":3136,"y":3712,"p":0},{"n":"Frozen Waste Plateau","x":2964,"y":3925,"p":0},{"n":"Trollweiss Mountain","x":2782,"y":3859,"p":0},{"n":"Ice Path","x":2855,"y":3809,"p":0},{"n":"Boneyard","x":3272,"y":3677,"p":0},{"n":"River Lum","x":3168,"y":3351,"p":0},{"n":"Aquanites","x":2724,"y":9974,"p":0},{"n":"Kurask","x":2699,"y":9998,"p":0},{"n":"Turoth","x":2723,"y":10004,"p":0},{"n":"Jellies","x":2704,"y":10027,"p":0},{"n":"Basilisks","x":2742,"y":10010,"p":0},{"n":"Pyrefiends","x":2761,"y":10004,"p":0},{"n":"Cockatrice","x":2791,"y":10036,"p":0},{"n":"Rock slugs","x":2800,"y":10017,"p":0},{"n":"Cave crawlers","x":2790,"y":9997,"p":0},{"n":"Giant frogs","x":3226,"y":9547,"p":0},{"n":"Swamp cave","x":3192,"y":9569,"p":0},{"n":"Keep Le Faye","x":2770,"y":3400,"p":0},{"n":"Ewan's Grove","x":2916,"y":3483,"p":0},{"n":"Astram Farm","x":2885,"y":3487,"p":0},{"n":"Rogue's Den","x":2891,"y":3443,"p":0},{"n":"Fight Pit","x":4571,"y":5091,"p":0},{"n":"Fight Cave","x":4611,"y":5130,"p":0},{"n":"Library","x":4629,"y":5170,"p":0},{"n":"Main Plaza","x":4671,"y":5156,"p":0},{"n":"Birthing Pool","x":4717,"y":5165,"p":0},{"n":"Fight Kiln","x":4743,"y":5170,"p":0},{"n":"Celestial Dragon Dungeon","x":2268,"y":5980,"p":0},{"n":"Puro-Puro","x":2427,"y":4445,"p":0},{"n":"Otherworldly beings","x":2384,"y":4424,"p":0},{"n":"Beware of the mushrooms","x":2418,"y":4377,"p":0},{"n":"The market","x":2483,"y":4448,"p":0},{"n":"Throne room","x":2446,"y":4426,"p":0}];
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
    // sp: set the Fishing Guild Teleport spell sprite (struct param spell_prayer_ability_sprite).
    { n: "Fishing Guild", src: "Lunar spellbook", x: 2612, y: 3381, p: 0, kb: "", item: 0, sp: 0,
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
      if (!teleReqMet(T)) continue;
      shown.push(T);
    }
    if (!shown.length) { el.style.display = 'none'; el.innerHTML = ''; return ''; }
    // Nearest last so it paints over the others where markers overlap.
    shown.sort((a, b) => Math.max(Math.abs(b.x - ctx0), Math.abs(b.y - cty0))
                       - Math.max(Math.abs(a.x - ctx0), Math.abs(a.y - cty0)));
    el.style.display = '';
    el.innerHTML = '';
    const notes = [];
    for (const T of shown) {
      const m = document.createElement('div');
      m.className = 'clue-map-lode clue-map-tele';
      m.style.left = (proj.projX(T.x) / proj.W * 100) + '%';
      m.style.top = (proj.projY(T.y) / proj.W * 100) + '%';
      const icon = document.createElement('div');
      const url = T.item ? resolveIcon(T.item) : '';
      if (T.sp) { icon.className = 'cml-icon'; loadSpriteIcon(icon, T.sp); }
      else if (url) { icon.className = 'cml-icon'; setIconBg(icon, url); }
      else { icon.className = 'cml-dot'; }
      const kb = document.createElement('div'); kb.className = 'cml-kb'; kb.textContent = T.kb || '';
      m.appendChild(icon); m.appendChild(kb);
      const parts = [];
      if (T.src) parts.push(T.src);
      if (T.kb) parts.push('option ' + T.kb);
      parts.push(T.n);
      m.title = parts.join(', ');
      el.appendChild(m);
      const d = Math.max(Math.abs(T.x - ctx0), Math.abs(T.y - cty0));
      notes.push(T.n + (T.kb ? ' [' + T.kb + ']' : '') + ' (' + d + ')');
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
    if (c.a === 'coordinate' || c.a === 'map') s = (c.a === 'coordinate' ? 'Dig at ' : 'Go to ') + '(' + c.x + ', ' + c.y + ')' + (c.p ? ' · floor ' + c.p : '');
    else if (c.a === 'search') s = 'Go to (' + c.x + ', ' + c.y + ') and search the clue object (drawers/crate/etc.)' + (c.p ? ' · floor ' + c.p : '');
    else if (c.a === 'npc') { const ni = CLUE_NPC_INFO[c.npc] || {}; const ans = clueLiveAnswer(c.npc) || ni.a; s = 'Talk to ' + (c.nn || ('NPC #' + c.npc)) + (c.gd ? ' [quest-gated]' : '') + (ans ? ' · answer: ' + ans : ''); }
    else if (c.a === 'scan') { if (isCompassClue(c)) { s = 'Compass clue (follow the needle)'; } else { const ar = CLUE_SCAN.get(c.en), r = CLUE_SCAN_RESOLVED.get(c.en); s = 'Scan ' + (ar || ('area (enum ' + c.en + ')')) + (r ? ' · ' + r.spots.length + ' spots' + (r.r ? ' · range ' + r.r : '') : ''); } }
    else if (c.a === 'keyitem') s = 'Search for key (' + (c.req || 'key item') + ')';
    else s = 'Emote / cryptic  ·  tap for the emote-clue guide (location, items, hidey-hole)';
    if (c.req && c.a !== 'keyitem') s += ' · needs ' + c.req;
    return s;
  }

