// RuneToolsX panel: Lockbox clue solver.
// Spliced inline into client.html; bare classic script sharing one global scope.

  // Interface 1933: 25 GRAPHIC components (id 1..25, row-major 5x5); styles in varbits
  // 39624..39648 (slot N -> 39623+N; 0=melee 1=ranged 2=magic). A click cycles the tile + its 4
  // ORTHOGONAL neighbours by +1; goal: every tile the same style. GF(3) lights-out: solve
  // A x = (target-grid). The plus matrix has nullity 3, so all 3 targets are tried and the fewest
  // clicks kept. Positioned via varc 3089/3090 (kPanelOrigins), so ifaceCompRects yields true
  // screen rects.
  const LOCKBOX_VB0 = 39624, LOCKBOX_STYLE = ['melee', 'ranged', 'magic'], LOCKBOX_SYM = ['0', '1', '2'];
  const LOCKBOX_COL = ['#ff7a6b', '#5ec8ff', '#c89bff'];                  // melee / ranged / magic tints
  const LOCKBOX_IDS = Array.from({ length: 25 }, (_, i) => i + 1).join(',');
  const LOCKBOX_VB_IDS = Array.from({ length: 25 }, (_, i) => LOCKBOX_VB0 + i);
  const lockboxA = (function () {                                        // clicking a tile cycles IT + 4 orthogonal neighbours (+)
    const A = Array.from({ length: 25 }, () => new Array(25).fill(0));
    const D = [[0, 0], [1, 0], [-1, 0], [0, 1], [0, -1]];
    for (let r = 0; r < 5; r++) for (let c = 0; c < 5; c++) { const i = r * 5 + c;
      for (const d of D) { const rr = r + d[0], cc = c + d[1]; if (rr >= 0 && rr < 5 && cc >= 0 && cc < 5) A[i][rr * 5 + cc] = (A[i][rr * 5 + cc] + 1) % 3; } }
    return A;
  })();
  function lockboxSolveB(b) {                                            // A x = b over GF(3); minimal-sum x, or null
    const N = 25, M = lockboxA.map((row, i) => row.concat(b[i])), piv = []; let r = 0;
    for (let c = 0; c < N; c++) {
      let pr = -1; for (let i = r; i < N; i++) if (M[i][c] % 3) { pr = i; break; }
      if (pr < 0) continue;
      const t = M[r]; M[r] = M[pr]; M[pr] = t;
      const inv = (M[r][c] % 3 === 1) ? 1 : 2; M[r] = M[r].map(x => (x * inv) % 3);
      for (let i = 0; i < N; i++) if (i !== r && M[i][c] % 3) { const f = M[i][c] % 3; for (let k = 0; k <= N; k++) M[i][k] = ((M[i][k] - f * M[r][k]) % 3 + 3) % 3; }
      piv.push([r, c]); r++;
    }
    for (let i = r; i < N; i++) if (M[i][N] % 3) return null;            // inconsistent
    const pivcols = piv.map(p => p[1]), free = []; for (let c = 0; c < N; c++) if (pivcols.indexOf(c) < 0) free.push(c);
    let best = null; const combos = Math.pow(3, free.length);
    for (let mask = 0; mask < combos; mask++) {                          // enumerate the null space (3^2 = 9) for the min
      const x = new Array(N).fill(0); let m = mask;
      for (const fc of free) { x[fc] = m % 3; m = (m / 3) | 0; }
      for (let p = piv.length - 1; p >= 0; p--) { const rr = piv[p][0], cc = piv[p][1]; let s = M[rr][N]; for (let c2 = cc + 1; c2 < N; c2++) s -= M[rr][c2] * x[c2]; x[cc] = ((s % 3) + 3) % 3; }
      let tot = 0; for (let i = 0; i < N; i++) tot += x[i];
      if (best === null || tot < best.t) best = { t: tot, x: x };
    }
    return best;
  }
  function lockboxSolve(grid, lockTarget) {                              // least-clicks across the 3 uniform targets
    let best = null; const targets = (lockTarget != null) ? [lockTarget] : [0, 1, 2];
    for (const T of targets) {
      const b = grid.map(v => (((T - v) % 3) + 3) % 3), r = lockboxSolveB(b);
      if (r && (best === null || r.t < best.total)) best = { target: T, clicks: r.x, total: r.t };
    }
    if (!best && lockTarget != null) return lockboxSolve(grid, null);    // locked target now unsolvable -> any
    return best;
  }
  let lockboxBusy = false, lockboxTarget = null, lockboxOwnsPanel = false, lockboxDrawSig = '', lockboxHl = false;
  function lockboxClearHl() { if (!lockboxHl) return; try { if (bridge() && bridge().puzzleCells) bridge().puzzleCells(myPid(), ''); } catch (e) {} lockboxHl = false; }
  async function lockboxTick() {
    if (lockboxBusy || !bridge() || !bridge().ifaceCompRects) return;
    lockboxBusy = true;
    try {
      // 1933 open? ifaceCompRects resolves the 25 cell rects via the 3089/3090 position varc.
      let cr = null;
      try { cr = JSON.parse(bridge().ifaceCompRects(myPid(), 1933, LOCKBOX_IDS, 0) || '{}'); } catch (e) {}
      const open = cr && cr.comps && Object.keys(cr.comps).length > 0;
      if (!open) { lockboxOwnsPanel = false; lockboxTarget = null; lockboxDrawSig = ''; lockboxClearHl(); return; }
      lockboxOwnsPanel = true;                                          // take the puzzle panel + overlay (puzzleTick/knotTick defer)
      // A failed varp read and a solved board are the SAME 25 zeros, so the values cannot decide
      // between them -- trust the out-of-band flag instead of printing a confident "Solved." over
      // an unsolved lockbox. Two of the modes this catches are PERMANENT, not one-tick hiccups, so
      // say so on screen rather than silently freezing the last frame: lockboxOwnsPanel is already
      // true above, which keeps puzzleTick/knotTick deferred, and a bare return would leave another
      // solver's stale click plan sitting over an open lockbox.
      let vals = null; try { vals = await readVarbitValues(LOCKBOX_VB_IDS); } catch (e) {}
      if (!vals || vals._ok !== true) { lockboxTarget = null; lockboxDrawWait(); lockboxClearHl(); return; }
      const grid = LOCKBOX_VB_IDS.map(id => (((vals[id] | 0) % 3) + 3) % 3);
      if (grid.every(v => v === grid[0])) { lockboxTarget = null; lockboxDraw(grid, null, 0, null); lockboxClearHl(); return; }
      const sol = lockboxSolve(grid, lockboxTarget);
      if (!sol) { lockboxTarget = null; lockboxDraw(grid, null, -1, null); lockboxClearHl(); return; }
      lockboxTarget = sol.target;                                       // lock the chosen (fewest-click) target so the plan is stable
      const order = []; for (let s = 0; s < 25; s++) if (sol.clicks[s] > 0) order.push(s);   // every tile to click, reading order
      lockboxDraw(grid, sol, sol.total, order);
      lockboxHighlight(order, sol.clicks, cr.comps, cr.abs);           // box EVERY tile (count inside, brightness by order)
    } catch (e) {} finally { lockboxBusy = false; }
  }
  // Panel (cluePuzzle): 5x5 of the live styles as 0/1/2; every tile that needs clicking is boxed
  // gold with its click count, brightness following the reading order to match the in-game overlay.
  function lockboxDraw(grid, sol, total, order) {
    const el = $('cluePuzzle'); if (!el) return;
    el.style.display = ''; el._phId = -1; el._lbph = -1;
    const tgt = sol ? sol.target : grid[0];
    const sig = grid.join('') + '|' + (sol ? sol.clicks.join('') : 'x') + '|' + total;
    if (sig === lockboxDrawSig) return; lockboxDrawSig = sig;
    let head;
    if (!sol && total === 0) head = '<b>Solved.</b>';
    else if (total === -1) head = 'no solution (unexpected board - reopen the lockbox)';
    else head = 'click each highlighted tile the number of times shown  ·  <b>' + total + '</b> click' + (total === 1 ? '' : 's') + ' <span style="opacity:0.55">→ all ' + LOCKBOX_STYLE[tgt] + '</span>'
              + '<div style="opacity:0.6;font-size:11px;margin-top:2px">order doesn\'t matter; the brightest tile is just the suggested next one (reading order)</div>';
    let g = '<div style="margin-top:8px">';
    for (let r = 0; r < 5; r++) {
      g += '<div style="display:flex;gap:3px;margin-bottom:3px">';
      for (let c = 0; c < 5; c++) {
        const i = r * 5 + c, v = grid[i], cnt = sol ? sol.clicks[i] : 0, rank = order ? order.indexOf(i) : -1;
        let bd = 'rgba(255,255,255,0.10)', bw = '1px', sh = '';
        if (rank >= 0) { const fa = rank === 0 ? 0.95 : rank === 1 ? 0.72 : rank === 2 ? 0.52 : 0.36; bd = 'rgba(255,196,64,' + fa + ')'; bw = '2px'; sh = 'box-shadow:0 0 6px rgba(255,196,64,' + (fa * 0.5) + ')'; }
        g += '<div style="position:relative;width:30px;height:30px;border-radius:5px;display:flex;align-items:center;justify-content:center;background:' + LOCKBOX_COL[v] + '22;border:' + bw + ' solid ' + bd + ';' + sh + '">'
           + '<span style="font-weight:700;font-size:13px;color:' + LOCKBOX_COL[v] + '">' + LOCKBOX_SYM[v] + '</span>'
           + (cnt > 0 ? '<span style="position:absolute;top:-3px;right:0;font-size:9px;font-weight:700;color:#ffd479;text-shadow:0 0 2px #000">×' + cnt + '</span>' : '')
           + '</div>';
      }
      g += '</div>';
    }
    g += '</div>';
    const html = '<span style="opacity:0.65;text-transform:uppercase;font-size:10px;letter-spacing:0.6px">Lockbox</span><div style="margin-top:4px">' + head + '</div>' + g;
    setHTML(el, html);
  }
  // No usable read this tick (see lockboxTick). Neutral placeholder, guarded by lockboxDrawSig so
  // the permanent-failure case does not rebuild the DOM 11x/second on the render thread.
  function lockboxDrawWait() {
    const el = $('cluePuzzle'); if (!el) return;
    el.style.display = ''; el._phId = -1; el._lbph = -1;
    if (lockboxDrawSig === 'wait') return; lockboxDrawSig = 'wait';
    setHTML(el, '<span style="opacity:0.65;text-transform:uppercase;font-size:10px;letter-spacing:0.6px">Lockbox</span>'
              + '<div style="margin-top:4px;opacity:0.7">reading the board...</div>');
  }
  // In-game: box EVERY tile that needs clicking (component id = slot+1) via puzzleCells, click
  // count inside, brightness by reading order. Re-sent each tick; order does not matter (clicks
  // commute).
  function lockboxHighlight(order, clicks, comps, abs) {
    if (!abs || !comps || !bridge() || !bridge().puzzleCells) { lockboxClearHl(); return; }
    const segs = [];
    order.forEach((s, rank) => { const c = comps[+s + 1]; if (c) segs.push(c[0] + ',' + c[1] + ',' + c[2] + ',' + c[3] + ',' + Math.min(rank, 3) + ',' + clicks[s]); });
    if (segs.length) { bridge().puzzleCells(myPid(), segs.join(';')); lockboxHl = true; } else lockboxClearHl();
  }
