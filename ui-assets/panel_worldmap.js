// RuneToolsX panel: World Map (full-world terrain browser: pan/zoom/search/layers).
// Spliced inline into client.html at load; shares its global scope (no IIFE); shared helpers
// ($, bridge, myPid, ...) and the clue-map data tables (MAP_LABELS, MAP_TELEPORTS, LODESTONES,
// HIDEY, nearLabel, teleReqMet) live in other spliced files and resolve at call time.

  // Terrain comes from bridge().mapWindow in fixed CHUNKS so pan/zoom never re-renders the
  // world server-side: one chunk = one cached offscreen canvas. Every level renders 512 px
  // images; higher px-per-tile just covers fewer tiles, so the bridge payload stays constant.
  // upto = the level's own ts, so the drawn image is always at native scale or minified,
  // never magnified (magnifying is what made the terrain look pixelated).
  const WM_LEVELS = [
    { ts: 2,  ch: 256, upto: 2 },
    { ts: 4,  ch: 128, upto: 4 },
    { ts: 8,  ch: 64,  upto: 8 },
    { ts: 16, ch: 32,  upto: 1e9 },  // WM_ZMAX 12 < 16: still minified
  ];
  const WM_ZMIN = 0.3, WM_ZMAX = 12, WM_CACHE_MAX = 130, WM_INFLIGHT_MAX = 2;
  let wmCam = { x: 3213.5, y: 3429.5, z: 1.4, p: 0 };   // world tile centre + px/tile + floor
  let wmLayer = { labels: true, lodes: true, teles: true, hidey: false, marks: true };
  let wmCamSaved = false;   // a persisted camera skips the centre-on-player of the first open
  try {
    const c = JSON.parse(localStorage.getItem('rtxWmCam') || 'null');
    if (c && isFinite(c.x) && isFinite(c.y) && isFinite(c.z)) {
      wmCam = { x: +c.x, y: +c.y, z: Math.max(WM_ZMIN, Math.min(WM_ZMAX, +c.z)), p: (c.p | 0) || 0 };
      wmCamSaved = true;
    }
    const l = JSON.parse(localStorage.getItem('rtxWmLayers') || 'null');
    if (l) for (const k in wmLayer) if (typeof l[k] === 'boolean') wmLayer[k] = l[k];
  } catch (e) {}
  let wmCamSaveT = 0;
  function wmSaveCam() {
    clearTimeout(wmCamSaveT);
    wmCamSaveT = setTimeout(function () {
      try { localStorage.setItem('rtxWmCam', JSON.stringify({ x: Math.round(wmCam.x * 10) / 10, y: Math.round(wmCam.y * 10) / 10, z: Math.round(wmCam.z * 1000) / 1000, p: wmCam.p })); } catch (e) {}
    }, 400);
    wmCamSaved = true;
  }
  function wmSaveLayers() { try { localStorage.setItem('rtxWmLayers', JSON.stringify(wmLayer)); } catch (e) {} }
  function wmLevel(z) { for (const L of WM_LEVELS) if (z <= L.upto) return L; return WM_LEVELS[WM_LEVELS.length - 1]; }

  // ---- chunk cache (LRU by Map insertion order) + bounded fetch queue ----
  const wmCache = new Map();    // "ts|plane|ix|iy" -> {cv: canvas | null (no map data)}
  const wmQueued = new Map();   // same key -> {key, ts, plane, ix, iy, half, cx, cy, pri, busy}
  let wmInflight = 0, wmFailAt = 0;
  function wmKey(ts, plane, ix, iy) { return ts + '|' + plane + '|' + ix + '|' + iy; }
  function wmGet(ts, plane, ix, iy, peek) {
    const k = wmKey(ts, plane, ix, iy), r = wmCache.get(k);
    if (r && !peek) { wmCache.delete(k); wmCache.set(k, r); }   // touch = move to LRU tail
    return r;
  }
  function wmSet(key, rec) {
    wmCache.delete(key); wmCache.set(key, rec);
    while (wmCache.size > WM_CACHE_MAX) { const first = wmCache.keys().next().value; wmCache.delete(first); }
  }
  function wmEnqueue(ts, plane, ix, iy, pri) {
    const k = wmKey(ts, plane, ix, iy);
    if (wmCache.has(k)) return;
    const j = wmQueued.get(k);
    if (j) { if (pri < j.pri) j.pri = pri; return; }
    const L = WM_LEVELS.find(v => v.ts === ts), ch = L.ch;
    wmQueued.set(k, { key: k, ts: ts, plane: plane, ix: ix, iy: iy, half: ch / 2, cx: ix * ch + ch / 2, cy: iy * ch + ch / 2, pri: pri, busy: false });
  }
  function wmDecode(meta) {
    const W = meta.w | 0, bin = atob(meta.b64), a = new Uint8ClampedArray(bin.length);
    for (let i = 0; i < bin.length; i++) a[i] = bin.charCodeAt(i);
    const cv = document.createElement('canvas'); cv.width = W; cv.height = W;
    cv.getContext('2d').putImageData(new ImageData(a, W, W), 0, 0);
    return cv;
  }
  async function wmPump() {
    if (wmInflight >= WM_INFLIGHT_MAX) return;
    if (Date.now() - wmFailAt < 1500) return;                  // bridge hiccup: brief backoff, the next draw retries
    let best = null;
    for (const j of wmQueued.values()) if (!j.busy && (!best || j.pri < best.pri)) best = j;
    if (!best || !bridge() || !bridge().mapWindow) return;
    best.busy = true; wmInflight++;
    let rec = null;
    try {
      const meta = JSON.parse((await bridge().mapWindow(best.cx, best.cy, best.plane, best.half, best.ts)) || '{}');
      rec = { cv: (meta && meta.b64) ? wmDecode(meta) : null };   // "{}" = genuinely no map data here
    } catch (e) {
      rec = null; wmFailAt = Date.now();
      // A static view only pumps from wmDraw; without this a transient bridge failure
      // would leave the coarse stand-in on screen until the next pan/zoom.
      setTimeout(wmKick, 1600);
    }
    wmInflight--; wmQueued.delete(best.key);
    if (rec) wmSet(best.key, rec);
    wmKick(); wmPump();
  }

  // ---- draw scheduling: one rAF per invalidation, idle when the tab is closed ----
  let wmRafOn = false;
  function wmKick() {
    if (wmRafOn) return; wmRafOn = true;
    (typeof requestAnimationFrame === 'function' ? requestAnimationFrame : function (cb) { setTimeout(cb, 16); })(function () {
      wmRafOn = false;
      if (activeTab !== 'worldmap') return;
      try { wmDraw(); } catch (e) {}
      if (wmFly) wmKick();
    });
  }

  // ---- icon images for canvas markers (item icons + cache sprites are data URLs) ----
  const wmImgs = new Map();        // url -> {img, ok}
  const wmSpritePend = new Set();
  function wmImg(url) {
    if (!url) return null;
    let r = wmImgs.get(url);
    if (!r) {
      r = { img: new Image(), ok: false };
      r.img.onload = function () { r.ok = true; wmKick(); };
      r.img.src = url;
      wmImgs.set(url, r);
    }
    return r.ok ? r.img : null;
  }
  function wmSpriteUrl(id) {
    if (!id || id <= 0) return '';
    if (SPRITES.has(id)) return SPRITES.get(id) || '';
    if (!wmSpritePend.has(id) && bridge() && bridge().sprite) {
      wmSpritePend.add(id);
      try {
        const r = bridge().sprite(id);
        if (r && typeof r.then === 'function')
          r.then(function (u) { SPRITES.set(id, (typeof u === 'string') ? u : ''); wmSpritePend.delete(id); wmKick(); },
                 function () { SPRITES.set(id, ''); wmSpritePend.delete(id); });
        else { SPRITES.set(id, (typeof r === 'string') ? r : ''); wmSpritePend.delete(id); }
      } catch (e) { SPRITES.set(id, ''); wmSpritePend.delete(id); }
    }
    return SPRITES.get(id) || '';
  }

  // ---- live state pulled on open ----
  let wmPlayer = null, wmFollow = false, wmSel = null, wmFly = null;
  let wmHideyVals = {};            // varp id -> value, for the 2-bit hidey-hole state fields
  let wmMarks = [];                // screen-space hover targets: {sx, sy, r, tip}
  let wmHover = { x: -1, y: -1 };  // cursor tile (status readout)
  async function wmOpen() {
    try { if (typeof fetchLodestones === 'function') fetchLodestones(true); } catch (e) {}
    try { if (typeof fetchMarkers === 'function') fetchMarkers(true); } catch (e) {}
    try { if (typeof clueMapTelePrefetch === 'function') clueMapTelePrefetch(); } catch (e) {}
    try {
      if (bridge() && bridge().varps && typeof HIDEY_VARPS !== 'undefined')
        wmHideyVals = JSON.parse(await bridge().varps(myPid(), HIDEY_VARPS.join(','))) || {};
    } catch (e) {}
    if (!wmCamSaved) {             // first ever open: land on the player rather than Varrock
      try { const P = await scanPlayerTile(); if (P) { wmCam.x = P.x + 0.5; wmCam.y = P.y + 0.5; wmCam.p = P.p | 0; } } catch (e) {}
    }
    wmKick();
  }
  // Player marker + follow. Cheap and self-guarding, so a plain interval is fine.
  let wmTickN = 0;
  setInterval(async function () {
    // typeof-guard: this can fire before the main script has run (activeTab not yet defined)
    if (typeof activeTab === 'undefined' || activeTab !== 'worldmap' || !document.getElementById('wmStage')) return;
    // Teleport gate varbits (spellbook etc.) can change while the map is open; refresh
    // every ~5s so "wrong spellbook" clears right after switching books.
    if (++wmTickN % 10 === 0) { try { await clueMapTelePrefetch(); wmKick(); } catch (e) {} }
    try {
      const P = await scanPlayerTile();
      const moved = !!P !== !!wmPlayer || (P && wmPlayer && (P.x !== wmPlayer.x || P.y !== wmPlayer.y || P.p !== wmPlayer.p));
      wmPlayer = P;
      if (P && wmFollow) { wmCam.x = P.x + 0.5; wmCam.y = P.y + 0.5; if (wmCam.p !== (P.p | 0)) { wmCam.p = P.p | 0; wmPaintBar(); } wmSaveCam(); }
      if (moved || wmFollow) wmKick();
    } catch (e) {}
  }, 500);

  function wmSetPlane(p) { if (wmCam.p === p) return; wmCam.p = p; wmSaveCam(); wmPaintBar(); wmKick(); }
  function wmFlyTo(x, y, p, sel) {
    if (p != null && (p | 0) !== wmCam.p) wmSetPlane(p | 0);
    const tz = Math.max(wmCam.z, 2.5);
    wmFly = { t0: Date.now(), dur: 380, fx: wmCam.x, fy: wmCam.y, fz: wmCam.z, tx: x + 0.5, ty: y + 0.5, tz: tz };
    wmFollow = false; wmSel = sel || null;
    wmPaintBar(); wmKick();
  }

  // ---- search across every location source the panels already carry ----
  function wmScore(name, s) {
    const l = String(name || '').toLowerCase();
    if (l.startsWith(s)) return 0;
    if (l.split(/[\s\-\/(),']+/).some(function (w) { return w.startsWith(s); })) return 1;
    if (l.indexOf(s) >= 0) return 2;
    return -1;
  }
  function wmSearchEntries(q) {
    const s = q.trim().toLowerCase(), out = [];
    if (!s) return out;
    const co = s.match(/^(\d{1,5})[,\s]+(\d{1,5})(?:[,\s]+(\d))?$/);   // "3213 3429" or "x,y,plane"
    if (co) out.push({ sc: 0, ty: 'Tile', nm: '(' + co[1] + ', ' + co[2] + ')' + (co[3] ? ' floor ' + co[3] : ''), dt: 'go to coordinate', x: +co[1], y: +co[2], p: +(co[3] || 0) });
    for (const d of MAP_LABELS) {
      const sc = wmScore(d.n, s); if (sc < 0) continue;
      out.push({ sc: sc, ty: 'Place', nm: d.n, dt: d.x + ', ' + d.y + (d.p ? ' f' + d.p : ''), x: d.x, y: d.y, p: d.p | 0 });
    }
    if (typeof LODESTONES !== 'undefined') for (const l of LODESTONES) {
      const sc = wmScore(l.n + ' lodestone', s); if (sc < 0) continue;
      out.push({ sc: sc, ty: 'Lodestone', nm: l.n, dt: l.kb ? 'key ' + l.kb : '', x: l.x, y: l.y, p: l.p | 0 });
    }
    for (const T of MAP_TELEPORTS) {
      let sc = wmScore(T.n, s); const ss = wmScore(T.src || '', s);
      if (ss >= 0 && (sc < 0 || ss + 1 < sc)) sc = ss + 1;
      if (sc < 0) continue;
      const rq = teleRqText(T);
      out.push({ sc: sc, ty: 'Teleport', nm: T.n, dt: (T.src || '') + (T.kb ? ' [' + T.kb + ']' : '') + (rq ? ' - req ' + rq : ''), x: T.x, y: T.y, p: T.p | 0 });
    }
    if (typeof HIDEY !== 'undefined') for (const hh of HIDEY) {
      const sc = wmScore(hh.loc + ' ' + hh.n + ' hidey', s); if (sc < 0) continue;
      out.push({ sc: sc, ty: 'Hidey-hole', nm: hh.loc, dt: hh.n + ' - ' + (HIDEY_TIERS[hh.t] || ''), x: hh.x, y: hh.y, p: hh.p | 0 });
    }
    if (typeof markerList !== 'undefined') for (const m of markerList) {
      const gx = (m.region >> 8) * 64 + m.lx, gy = (m.region & 0xFF) * 64 + m.ly;   // region = (x>>6)<<8 | (y>>6)
      const nm = m.label || ('Marker (' + gx + ', ' + gy + ')');
      const sc = wmScore(nm + ' marker', s); if (sc < 0) continue;
      out.push({ sc: sc, ty: 'Marker', nm: nm, dt: gx + ', ' + gy + (m.plane ? ' f' + m.plane : ''), x: gx, y: gy, p: m.plane | 0 });
    }
    out.sort(function (a, b) { return (a.sc - b.sc) || (a.nm < b.nm ? -1 : 1); });
    return out.slice(0, 40);
  }
  let wmResSel = 0;
  function wmRenderResults() {
    const box = $('wmResults'), inp = $('wmSearch'); if (!box || !inp) return;
    const es = wmSearchEntries(inp.value || '');
    box._es = es;
    if (!es.length) { box.style.display = 'none'; box.innerHTML = ''; return; }
    if (wmResSel >= es.length) wmResSel = es.length - 1;
    box.style.display = '';
    box.innerHTML = es.map(function (e, i) {
      return '<div class="wm-res' + (i === wmResSel ? ' sel' : '') + '" data-i="' + i + '">'
        + '<span class="ty">' + e.ty + '</span><span class="nm">' + htmlEsc(e.nm) + '</span>'
        + (e.dt ? '<span class="dt">' + htmlEsc(e.dt) + '</span>' : '') + '</div>';
    }).join('');
  }
  function wmPickResult(i) {
    const box = $('wmResults'), inp = $('wmSearch');
    const es = (box && box._es) || [];
    const e = es[i]; if (!e) return;
    if (box) { box.style.display = 'none'; box.innerHTML = ''; }
    if (inp) inp.blur();
    wmFlyTo(e.x, e.y, e.p, { x: e.x, y: e.y, p: e.p | 0, nm: e.nm, dt: (e.ty === 'Tile' ? '' : e.ty + (e.dt ? ' - ' + e.dt : '')) });
  }

  // ---- overlay painters (screen space; each registers hover targets in wmMarks) ----
  // Marker keybinds come from teleKb() (panel_towers.js, next to the teleport data): the
  // explicit kb field, else the bracketed tokens many sheet rows carry in the name.
  // Icon draw that keeps the sprite's own aspect ratio, never upscales past native size,
  // and snaps to whole pixels - forcing a square box at fractional coords distorted them.
  function wmDrawIcon(cx, img, mx, my, box) {
    const w = img.naturalWidth || img.width || box, h = img.naturalHeight || img.height || box;
    const s = Math.min(1, box / Math.max(w, h));
    const dw = Math.max(1, Math.round(w * s)), dh = Math.max(1, Math.round(h * s));
    cx.drawImage(img, Math.round(mx - dw / 2), Math.round(my - dh / 2), dw, dh);
  }
  // Place-label styling, shared by both map surfaces.
  // A halo alone cannot hold up here: the basemap's brightness changes under every glyph, so
  // legibility swings wildly. Instead each label gets its own PLATE - a dark translucent pill
  // with no border - which fixes text contrast at a constant, high value while the plate
  // itself is dark enough to recede into the map. Type stays small and light so the plates
  // read as chrome, not content.
  function wmLabelPlate(cx, x, y, w, h, r, fill) {
    cx.beginPath();
    cx.moveTo(x + r, y); cx.lineTo(x + w - r, y); cx.quadraticCurveTo(x + w, y, x + w, y + r);
    cx.lineTo(x + w, y + h - r); cx.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
    cx.lineTo(x + r, y + h); cx.quadraticCurveTo(x, y + h, x, y + h - r);
    cx.lineTo(x, y + r); cx.quadraticCurveTo(x, y, x + r, y);
    cx.closePath(); cx.fillStyle = fill; cx.fill();
  }
  function wmLabelText(cx, text, x, y, hot, bx, by, bw, bh, dim) {
    cx.save();
    if (dim != null) cx.globalAlpha = dim;
    if (bw) {
      cx.shadowColor = 'rgba(0,0,0,0.45)'; cx.shadowBlur = 4; cx.shadowOffsetY = 1;
      wmLabelPlate(cx, bx, by, bw, bh, 3, hot ? 'rgba(10,26,24,0.86)' : 'rgba(9,12,17,0.72)');
      cx.shadowBlur = 0; cx.shadowOffsetY = 0;
      if (hot) {
        cx.lineWidth = 1; cx.strokeStyle = 'rgba(126,240,216,0.55)';
        wmLabelPlate(cx, bx + 0.5, by + 0.5, bw - 1, bh - 1, 3, 'rgba(0,0,0,0)');
        cx.stroke();
      }
    }
    cx.fillStyle = hot ? '#8ef0d8' : 'rgba(232,237,244,0.96)';
    cx.fillText(text, x, y);
    cx.restore();
  }
  const wmTextW = new Map();   // label -> measured px width (font is constant)
  function wmChip(cx, sxp, syp, text, placed, hot) {
    cx.font = '500 10.5px system-ui, "Segoe UI", sans-serif'; cx.textAlign = 'center'; cx.textBaseline = 'middle';
    try { cx.letterSpacing = '0.35px'; } catch (e) {}
    let tw = wmTextW.get(text);
    if (tw === undefined) { tw = Math.ceil(cx.measureText(text).width); wmTextW.set(text, tw); }
    const bw = tw + 9, bh = 14;
    const x0 = sxp - bw / 2, y0 = syp - bh / 2;
    if (placed.some(function (p) { return x0 < p.x + p.w + 2 && x0 + bw + 2 > p.x && y0 < p.y + p.h + 2 && y0 + bh + 2 > p.y; })) return false;
    placed.push({ x: x0, y: y0, w: bw, h: bh });
    wmLabelText(cx, text, sxp, syp, hot, x0, y0, bw, bh);
    return true;
  }
  function wmDraw() {
    const stage = $('wmStage'), cv = $('wmCanvas'); if (!stage || !cv) return;
    const w = stage.clientWidth | 0, h = stage.clientHeight | 0; if (!w || !h) return;
    // Back the canvas at DEVICE resolution: a CSS-resolution store gets stretched by display
    // scaling and everything (icons, text, terrain) renders soft. All drawing below stays in
    // CSS units via the transform; wmMarks hit-testing is CSS-space too, so nothing else moves.
    const dpr = (typeof devicePixelRatio === 'number' && devicePixelRatio > 0) ? devicePixelRatio : 1;
    const bwpx = Math.round(w * dpr), bhpx = Math.round(h * dpr);
    if (cv.width !== bwpx) cv.width = bwpx;
    if (cv.height !== bhpx) cv.height = bhpx;
    if (wmFly) {                                                // fly-to animation (ease-out cubic)
      const t = Math.min(1, (Date.now() - wmFly.t0) / wmFly.dur), e = 1 - Math.pow(1 - t, 3);
      wmCam.x = wmFly.fx + (wmFly.tx - wmFly.fx) * e;
      wmCam.y = wmFly.fy + (wmFly.ty - wmFly.fy) * e;
      wmCam.z = wmFly.fz + (wmFly.tz - wmFly.fz) * e;
      if (t >= 1) { wmFly = null; wmSaveCam(); }
    }
    const z = wmCam.z, plane = wmCam.p;
    const sx = function (tx) { return (tx - wmCam.x) * z + w / 2; };
    const sy = function (ty) { return (wmCam.y - ty) * z + h / 2; };
    const cx = cv.getContext('2d');
    cx.setTransform(dpr, 0, 0, dpr, 0, 0);
    cx.fillStyle = '#0b0d12'; cx.fillRect(0, 0, w, h);
    wmMarks = [];
    const vx0 = Math.floor(wmCam.x - w / 2 / z), vx1 = Math.ceil(wmCam.x + w / 2 / z);
    const vy0 = Math.floor(wmCam.y - h / 2 / z), vy1 = Math.ceil(wmCam.y + h / 2 / z);
    // terrain chunks (exact level, with the finest cached coarser level as a stand-in while
    // loading). Destination rects are snapped to shared integer edges so adjacent chunks
    // tile without hairline seams under the always-on bilinear smoothing.
    const L = wmLevel(z * dpr), ch = L.ch, Lidx = WM_LEVELS.indexOf(L);   // level picked in DEVICE px/tile so scaled displays stay native-sharp
    const ix0 = Math.max(0, Math.floor(vx0 / ch)), ix1 = Math.min(Math.floor(16383 / ch), Math.floor(vx1 / ch));
    const iy0 = Math.max(0, Math.floor(vy0 / ch)), iy1 = Math.min(Math.floor(16383 / ch), Math.floor(vy1 / ch));
    cx.imageSmoothingEnabled = true;
    try { cx.imageSmoothingQuality = 'high'; } catch (e) {}
    const ccx = (vx0 + vx1) / 2, ccy = (vy0 + vy1) / 2;
    for (let ix = ix0; ix <= ix1; ix++) {
      for (let iy = iy0; iy <= iy1; iy++) {
        const dx0 = Math.round(sx(ix * ch)), dx1 = Math.round(sx((ix + 1) * ch));
        const dy0 = Math.round(sy((iy + 1) * ch)), dy1 = Math.round(sy(iy * ch));
        const rec = wmGet(L.ts, plane, ix, iy);
        if (rec && rec.cv) { cx.drawImage(rec.cv, dx0, dy0, dx1 - dx0, dy1 - dy0); continue; }
        if (!rec) wmEnqueue(L.ts, plane, ix, iy, Math.abs(ix * ch + ch / 2 - ccx) + Math.abs(iy * ch + ch / 2 - ccy));
        if (rec && !rec.cv) continue;                           // known-empty: leave the void dark
        for (let li = Lidx - 1; li >= 0; li--) {                // loading: nearest coarser cached level
          const B = WM_LEVELS[li], bch = B.ch;
          const bix = Math.floor(ix * ch / bch), biy = Math.floor(iy * ch / bch);
          const b = wmGet(B.ts, plane, bix, biy, true);
          if (b && b.cv) {
            const px = (ix * ch - bix * bch) * B.ts, py = (biy * bch + bch - ch - iy * ch) * B.ts;
            cx.drawImage(b.cv, px, py, ch * B.ts, ch * B.ts, dx0, dy0, dx1 - dx0, dy1 - dy0);
            break;
          }
        }
      }
    }
    wmPump();
    // hidey-holes (built state from the 2-bit varbit: 0 none, 1 built, 2 stocked)
    if (wmLayer.hidey && z >= 1.1 && typeof HIDEY !== 'undefined') {
      for (const hh of HIDEY) {
        if ((hh.p | 0) !== plane || hh.x < vx0 || hh.x > vx1 || hh.y < vy0 || hh.y > vy1) continue;
        const mx = sx(hh.x + 0.5), my = sy(hh.y + 0.5);
        const st = ((wmHideyVals[hh.vp] || 0) >>> hh.b) & 3;
        cx.lineWidth = 1.5; cx.strokeStyle = 'rgba(0,0,0,0.8)';
        cx.fillStyle = st === 2 ? '#46e0c0' : st === 1 ? 'rgba(70,224,192,0.45)' : 'rgba(120,130,150,0.55)';
        cx.beginPath(); cx.rect(mx - 5, my - 4, 10, 8); cx.fill(); cx.stroke();
        wmMarks.push({ sx: mx, sy: my, r: 7, tip: '<b>Hidey-hole</b> ' + htmlEsc(hh.loc) + '<br>' + htmlEsc(hh.n) + ' - ' + (HIDEY_TIERS[hh.t] || '') + '<br><span style="opacity:.65">' + (st === 2 ? 'built + stocked' : st === 1 ? 'built, empty' : 'not built') + ' - ' + hh.x + ', ' + hh.y + '</span>' });
      }
    }
    // user tile markers
    if (wmLayer.marks && typeof markerList !== 'undefined') {
      for (const m of markerList) {
        if ((m.plane | 0) !== plane) continue;
        const gx = (m.region >> 8) * 64 + m.lx, gy = (m.region & 0xFF) * 64 + m.ly;
        if (gx < vx0 || gx > vx1 || gy < vy0 || gy > vy1) continue;
        const s0 = Math.max(4, Math.min(14, z));
        cx.fillStyle = mkHex(mkParseColor(m.color)); cx.strokeStyle = 'rgba(0,0,0,0.8)'; cx.lineWidth = 1.2;
        cx.beginPath(); cx.rect(sx(gx + 0.5) - s0 / 2, sy(gy + 0.5) - s0 / 2, s0, s0); cx.fill(); cx.stroke();
        wmMarks.push({ sx: sx(gx + 0.5), sy: sy(gy + 0.5), r: s0 / 2 + 3, tip: '<b>' + htmlEsc(m.label || 'Tile marker') + '</b><br>' + gx + ', ' + gy });
      }
    }
    // teleport destinations (dense: only at close zoom, distance-clustered so stacks stay
    // readable - grid-cell bucketing split same-spot stacks landing across a cell border)
    if (wmLayer.teles && z >= 1.6) {
      const buckets = [];
      for (const T of MAP_TELEPORTS) {
        if ((T.p | 0) !== plane || T.x < vx0 || T.x > vx1 || T.y < vy0 || T.y > vy1) continue;
        const mx = sx(T.x + 0.5), my = sy(T.y + 0.5);
        let b = null;
        for (let i = 0; i < buckets.length; i++) {
          const q = buckets[i], dx = q.mx - mx, dy = q.my - my;
          if (dx * dx + dy * dy <= 289) { b = q; break; }   // within 17px of a group anchor
        }
        if (!b) { b = { mx: mx, my: my, ts: [] }; buckets.push(b); }
        b.ts.push(T);
      }
      for (const b of buckets) {
        // Stacked groups show EACH member's own icon (up to three, fanned around the
        // anchor) instead of one icon hiding the rest; bigger stacks add a +N badge.
        const show = b.ts.slice(0, 3), n = show.length, sp = 13;
        for (let i = 0; i < n; i++) {
          const T2 = show[i];
          cx.globalAlpha = !teleWhyCached(T2) ? 1 : 0.45;
          const url = T2.iconUrl || (T2.item ? resolveIcon(T2.item) : (T2.sp ? wmSpriteUrl(T2.sp) : ''));
          const img = url ? wmImg(url) : null;
          const ox = b.mx + (i - (n - 1) / 2) * sp;
          if (img) wmDrawIcon(cx, img, ox, b.my, n === 1 ? 20 : 17);
          else {
            cx.fillStyle = '#4dd28a'; cx.strokeStyle = '#0b0d12'; cx.lineWidth = 2;
            cx.beginPath(); cx.arc(ox, b.my, 5, 0, 6.2832); cx.fill(); cx.stroke();
          }
        }
        cx.globalAlpha = 1;
        const kb = teleKb(b.ts[0]);
        if (b.ts.length > 3) {
          const extra = '+' + (b.ts.length - 3);
          cx.font = '700 9px system-ui, sans-serif'; cx.textAlign = 'center'; cx.textBaseline = 'middle';
          cx.beginPath(); cx.arc(b.mx + (n / 2) * sp + 5, b.my - 8, 7, 0, 6.2832); cx.fillStyle = '#4dd28a'; cx.fill();
          cx.fillStyle = '#06120b'; cx.fillText(extra, b.mx + (n / 2) * sp + 5, b.my - 7.5);
        } else if (b.ts.length === 1 && kb && z >= 2.4) {
          cx.font = '700 10px system-ui, sans-serif'; cx.textAlign = 'center'; cx.textBaseline = 'top';
          const kw = Math.ceil(cx.measureText(kb).width + 6);
          const bx = Math.round(b.mx - kw / 2), by = Math.round(b.my + 10);
          cx.fillStyle = '#4dd28a'; cx.fillRect(bx, by, kw, 13);
          cx.fillStyle = '#06120b'; cx.fillText(kb, bx + kw / 2, by + 1.5);
        }
        wmMarks.push({ sx: b.mx, sy: b.my, r: 10 + (n - 1) * sp / 2 + 3, ts: b.ts });   // tooltip built lazily on hover
      }
    }
    // lodestones (sparse: always drawn; grey until the unlock varbit says otherwise)
    if (wmLayer.lodes && typeof LODESTONES !== 'undefined') {
      const lookup = (typeof lodeData !== 'undefined' && lodeData) ? lodeData : null;
      for (const l of LODESTONES) {
        if ((l.p | 0) !== plane) continue;   // Prifddinas / City of Um are plane-1 destinations
        if (l.x < vx0 || l.x > vx1 || l.y < vy0 || l.y > vy1) continue;
        const mx = sx(l.x + 0.5), my = sy(l.y + 0.5);
        let unlocked = true;
        if (lookup) { const d = lookup.find(function (q) { return q.n === l.n; }); if (d) unlocked = !!d.unlocked; }
        cx.globalAlpha = unlocked ? 1 : 0.4;
        const img = z >= 0.7 ? wmImg(wmSpriteUrl(l.sp)) : null;
        if (img) wmDrawIcon(cx, img, mx, my, 22);
        else {
          const r0 = 6;
          cx.beginPath(); cx.moveTo(mx, my - r0); cx.lineTo(mx + r0, my); cx.lineTo(mx, my + r0); cx.lineTo(mx - r0, my); cx.closePath();
          cx.fillStyle = 'rgba(140,110,255,0.95)'; cx.fill();
          cx.lineWidth = 1.5; cx.strokeStyle = '#fff'; cx.stroke();
        }
        cx.globalAlpha = 1;
        wmMarks.push({ sx: mx, sy: my, r: 11, tip: '<b>' + htmlEsc(l.n) + ' lodestone</b><br><span style="opacity:.75">' + (l.kb ? 'key ' + htmlEsc(l.kb) + ' - ' : '') + (unlocked ? 'unlocked' : 'locked') + '</span>' });
      }
    }
    // place labels (nearest-to-centre first, overlap-culled like the clue maps)
    if (wmLayer.labels) {
      const inview = MAP_LABELS.filter(function (d) { return (d.p || 0) === plane && d.x >= vx0 && d.x <= vx1 && d.y >= vy0 && d.y <= vy1; });
      inview.sort(function (a, b) { return (Math.abs(a.x - ccx) + Math.abs(a.y - ccy)) - (Math.abs(b.x - ccx) + Math.abs(b.y - ccy)); });
      const placed = [];
      cx.save();
      for (const d of inview.slice(0, 34)) {
        if (wmChip(cx, sx(d.x + 0.5), sy(d.y + 0.5), d.n, placed, false))
          wmMarks.push({ sx: sx(d.x + 0.5), sy: sy(d.y + 0.5), r: 8, tip: '<b>' + htmlEsc(d.n) + '</b><br>' + d.x + ', ' + d.y });
      }
      cx.restore();
    }
    // selection pin
    if (wmSel && (wmSel.p | 0) === plane) {
      const mx = sx(wmSel.x + 0.5), my = sy(wmSel.y + 0.5);
      cx.lineCap = 'round';
      const gap = 9, len = 10;
      const tick = function (dx, dy) {
        cx.beginPath(); cx.moveTo(mx + dx * gap, my + dy * gap); cx.lineTo(mx + dx * (gap + len), my + dy * (gap + len));
        cx.lineWidth = 4; cx.strokeStyle = 'rgba(0,0,0,0.55)'; cx.stroke();
        cx.lineWidth = 2; cx.strokeStyle = 'rgba(255,255,255,0.95)'; cx.stroke();
      };
      tick(0, -1); tick(0, 1); tick(-1, 0); tick(1, 0);
      cx.lineWidth = 2.6; cx.strokeStyle = '#ff2d95'; cx.beginPath(); cx.arc(mx, my, 10, 0, 6.2832); cx.stroke();
      cx.lineWidth = 1.2; cx.strokeStyle = 'rgba(255,255,255,0.85)'; cx.beginPath(); cx.arc(mx, my, 12.5, 0, 6.2832); cx.stroke();
      if (wmSel.nm) { const placed = []; cx.save(); wmChip(cx, mx, my - 24, wmSel.nm, placed, true); cx.restore(); }
      wmMarks.push({ sx: mx, sy: my, r: 12, tip: '<b>' + htmlEsc(wmSel.nm || 'Pinned tile') + '</b><br>' + wmSel.x + ', ' + wmSel.y + '<br><span style="opacity:.65">' + (nearLabel(wmSel.x, wmSel.y, plane) || '') + '</span>' });
    }
    // player
    if (wmPlayer && (wmPlayer.p | 0) === plane) {
      const mx = sx(wmPlayer.x + 0.5), my = sy(wmPlayer.y + 0.5);
      if (mx >= -20 && mx <= w + 20 && my >= -20 && my <= h + 20) {
        cx.beginPath(); cx.arc(mx, my, 5.5, 0, 6.2832);
        cx.fillStyle = '#ffd23f'; cx.fill();
        cx.lineWidth = 2; cx.strokeStyle = '#fff'; cx.stroke();
        wmMarks.push({ sx: mx, sy: my, r: 8, tip: '<b>You</b><br>' + wmPlayer.x + ', ' + wmPlayer.y });
      }
    }
    wmPaintStatus();
    wmHoverBox();
  }

  function wmTeleTip(ts) {
    return ts.map(function (t2) {
      const rq = (typeof teleRqLive === 'function') ? teleRqLive(t2, 'html') : htmlEsc(teleRqText(t2)), why = teleWhyCached(t2);
      const ci = (typeof teleChargeInfo === 'function') ? teleChargeInfo(t2) : null;
      // green = usable now, red = a requirement is unmet (named below)
      const unk = (typeof teleTaskSetWhy === 'function') && teleTaskSetWhy(t2) === '?';
      const dot = '<span style="color:' + (why ? '#ff6b6b' : unk ? '#fbbf24' : '#4dd28a') + '">●</span> ';
      return dot + '<b>' + htmlEsc(t2.n) + '</b><br><span style="opacity:.75">' + htmlEsc(t2.src || '')
        + (t2.kb ? ' [' + htmlEsc(t2.kb) + ']' : '')
        + (ci && ci.used < ci.max ? '<br>daily teleports ' + ci.used + '/' + ci.max + ' · ' + teleResetIn() : '')
        + ((typeof teleInPassage === 'function' && teleInPassage(t2) && telePassage && telePassage.free)
             ? '<br>unlimited charges (in the passage)'
             : (typeof teleItemChargeVal === 'function' && teleItemChargeVal(t2) !== null
                 ? '<br>' + teleItemChargeVal(t2).toLocaleString()
                   + (teleItemChargeMax(t2) ? '/' + teleItemChargeMax(t2) : '') + ' charges'
                   + ((typeof teleInPassage === 'function' && teleInPassage(t2)) ? ' (in the passage)' : '') : ''))
        + (rq ? '<br>req: ' + (rq === 'unverified' ? '<span style="color:#fbbf24">unverified</span>' : rq) : '')
        + (why ? '<br>' + htmlEsc(why) : '') + '</span>';
    }).join('<hr style="border:none;border-top:1px solid rgba(255,255,255,0.15);margin:3px 0">');
  }
  // The hovered TILE outlined at the map's own scale. A positioned div, so pointing at
  // tiles never forces a canvas repaint; wmDraw re-syncs it after pan/zoom.
  function wmHoverBox() {
    const stage = document.getElementById('wmStage'); if (!stage) return;
    let box = document.getElementById('wmHoverTile');
    if (!box) {
      box = document.createElement('div'); box.id = 'wmHoverTile';
      box.style.cssText = 'position:absolute;pointer-events:none;z-index:6;display:none;'
        + 'border:1.5px solid rgba(255,170,60,0.95);box-shadow:0 0 0 1px rgba(0,0,0,0.55);border-radius:1px';
      stage.appendChild(box);
    }
    const z = wmCam.z;
    if (wmHover.x < 0 || z < 2) { box.style.display = 'none'; return; }
    const px = (wmHover.x - wmCam.x) * z + stage.clientWidth / 2;
    const py = (wmCam.y - (wmHover.y + 1)) * z + stage.clientHeight / 2;
    box.style.left = px + 'px'; box.style.top = py + 'px';
    box.style.width = z + 'px'; box.style.height = z + 'px';
    box.style.display = 'block';
  }
  // ---- chrome: bar chips + status line ----
  function wmPaintBar() {
    const bar = $('wmBar'); if (!bar) return;
    const chips = bar.querySelectorAll('[data-wm]');
    for (let i = 0; i < chips.length; i++) {
      const c = chips[i], k = c.dataset.wm;
      if (k.indexOf('f') === 0 && k.length === 2) c.classList.toggle('on', wmCam.p === +k[1]);
      else if (k === 'follow') c.classList.toggle('on', wmFollow);
      else if (wmLayer[k] !== undefined) c.classList.toggle('on', !!wmLayer[k]);
    }
  }
  function wmPaintStatus() {
    const st = $('wmStatus'); if (!st) return;
    const near = (wmHover.x >= 0) ? (nearLabel(wmHover.x, wmHover.y, wmCam.p) || '') : '';
    let html = (wmHover.x >= 0 ? '<b>' + wmHover.x + ', ' + wmHover.y + '</b>' + (near ? ' <span class="wm-near">' + htmlEsc(near) + '</span>' : '') : '<span>hover the map for coordinates</span>')
      + '<span style="margin-left:auto">floor ' + wmCam.p + ' - ' + (Math.round(wmCam.z * 100) / 100) + ' px/tile</span>';
    if (wmSel) html += '<span class="wm-x" id="wmSelClear" title="Clear pin">&times;</span>';
    setHTML(st, html);
    const xb = $('wmSelClear');
    if (xb && !xb._b) { xb._b = 1; xb.addEventListener('click', function () { wmSel = null; wmKick(); }); }
  }

  // ---- panel ----
  const WM_CSS = ''
    + '.wm-wrap{display:flex;flex-direction:column;gap:6px;height:100%;min-height:0}'
    + '.wm-searchrow{position:relative;flex:0 0 auto}'
    + '.wm-search{width:100%;box-sizing:border-box;height:30px;padding:0 10px;background:var(--bg-elev);border:1px solid var(--border);border-radius:8px;color:var(--text);font-size:12px;outline:none}'
    + '.wm-search:focus{border-color:var(--border-hi)}'
    + '.wm-results{position:absolute;top:32px;left:0;right:0;z-index:30;max-height:260px;overflow-y:auto;background:#12151d;border:1px solid var(--border-hi);border-radius:8px;box-shadow:0 8px 22px rgba(0,0,0,0.55)}'
    + '.wm-res{display:flex;gap:7px;align-items:center;padding:5px 8px;cursor:pointer;font-size:11.5px}'
    + '.wm-res:hover,.wm-res.sel{background:rgba(124,92,252,0.14)}'
    + '.wm-res .ty{flex:0 0 auto;font-size:9px;font-weight:800;text-transform:uppercase;letter-spacing:0.4px;padding:1px 6px;border-radius:7px;background:rgba(255,255,255,0.07);color:var(--text-dim)}'
    + '.wm-res .nm{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}'
    + '.wm-res .dt{flex:0 1 auto;color:var(--text-mute);font-size:10px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}'
    + '.wm-bar{display:flex;flex-wrap:wrap;gap:4px;align-items:center;flex:0 0 auto}'
    + '.wm-chip{padding:2px 9px;border-radius:9px;border:1px solid var(--border);background:var(--bg-elev);color:var(--text-dim);font-size:10.5px;font-weight:600;cursor:pointer;user-select:none}'
    + '.wm-chip:hover{border-color:var(--border-hi);color:var(--text)}'
    + '.wm-chip.on{color:#46e0c0;border-color:rgba(70,224,192,0.55);background:rgba(70,224,192,0.12)}'
    + '.wm-sep{width:1px;height:16px;background:var(--border);margin:0 2px}'
    + '.wm-stage{position:relative;flex:1 1 auto;min-height:220px;overflow:hidden;border:1px solid var(--border);border-radius:8px;background:#0b0d12;cursor:grab}'
    + '.wm-stage.grabbing{cursor:grabbing}'
    + '#wmCanvas{position:absolute;left:0;top:0;width:100%;height:100%}'
    + '.wm-tip{position:absolute;z-index:20;display:none;pointer-events:none;background:rgba(9,11,17,0.94);border:1px solid var(--border-hi);border-radius:6px;padding:5px 8px;font-size:11px;line-height:1.45;max-width:240px}'
    + '.wm-status{flex:0 0 18px;height:18px;overflow:hidden;font-size:10.5px;color:var(--text-dim);display:flex;gap:8px;align-items:center}'
    + '.wm-status span{white-space:nowrap}'
    + '.wm-status .wm-near{flex:0 1 auto;min-width:0;overflow:hidden;text-overflow:ellipsis}'
    + '.wm-status b{color:var(--text);white-space:nowrap}'
    + '.wm-x{cursor:pointer;color:var(--text-mute);font-weight:800;padding:0 4px;font-size:13px}';
  let wmDrag = null, wmWinBound = false;
  function renderWorldMap() {
    const c = $('content');
    if ($('wmWrap')) { wmKick(); return; }        // already mounted: the 250ms poll must not rebuild (kills drag/search focus)
    injectStyle('wm-style', WM_CSS);
    c.innerHTML = '';
    const wrap = document.createElement('div'); wrap.id = 'wmWrap'; wrap.className = 'wm-wrap';
    wrap.innerHTML = ''
      + '<div class="wm-searchrow">'
      + '  <input id="wmSearch" class="wm-search" type="text" placeholder="Search places, teleports, lodestones, hidey-holes, markers, or x,y..." autocomplete="off" spellcheck="false">'
      + '  <div id="wmResults" class="wm-results" style="display:none"></div>'
      + '</div>'
      + '<div id="wmBar" class="wm-bar">'
      + '  <div class="wm-chip" data-wm="f0">0</div><div class="wm-chip" data-wm="f1">1</div>'
      + '  <div class="wm-chip" data-wm="f2">2</div><div class="wm-chip" data-wm="f3">3</div>'
      + '  <div class="wm-sep"></div>'
      + '  <div class="wm-chip" data-wm="follow" title="Keep the view centred on you">Follow</div>'
      + '  <div class="wm-chip" id="wmYou" title="Centre on your position">You</div>'
      + '  <div class="wm-sep"></div>'
      + '  <div class="wm-chip" data-wm="labels" title="Place names">Labels</div>'
      + '  <div class="wm-chip" data-wm="lodes" title="Lodestones">Lodes</div>'
      + '  <div class="wm-chip" data-wm="teles" title="Teleport destinations (zoom in)">Teles</div>'
      + '  <div class="wm-chip" data-wm="hidey" title="Hidey-holes (zoom in)">Hidey</div>'
      + '  <div class="wm-chip" data-wm="marks" title="Your tile markers">Marks</div>'
      + '  <div class="wm-sep"></div>'
      + '  <div class="wm-chip" id="wmZoomOut" title="Zoom out">&minus;</div>'
      + '  <div class="wm-chip" id="wmZoomIn" title="Zoom in">+</div>'
      + '</div>'
      + '<div id="wmStage" class="wm-stage"><canvas id="wmCanvas"></canvas><div id="wmTip" class="wm-tip"></div></div>'
      + '<div id="wmStatus" class="wm-status"></div>';
    c.appendChild(wrap);
    const stage = $('wmStage'), cvEl = $('wmCanvas'), inp = $('wmSearch'), res = $('wmResults');
    const zoomAt = function (mx, my, factor) {
      const nz = Math.max(WM_ZMIN, Math.min(WM_ZMAX, wmCam.z * factor));
      if (nz === wmCam.z) return;
      const wx = wmCam.x + (mx - stage.clientWidth / 2) / wmCam.z;
      const wy = wmCam.y - (my - stage.clientHeight / 2) / wmCam.z;
      wmCam.z = nz;
      wmCam.x = wx - (mx - stage.clientWidth / 2) / nz;
      wmCam.y = wy + (my - stage.clientHeight / 2) / nz;
      wmSaveCam(); wmKick();
    };
    stage.addEventListener('wheel', function (e) {
      e.preventDefault();
      const r = stage.getBoundingClientRect();
      zoomAt(e.clientX - r.left, e.clientY - r.top, e.deltaY < 0 ? 1.22 : 1 / 1.22);
    }, { passive: false });
    stage.addEventListener('mousedown', function (e) {
      wmDrag = { x: e.clientX, y: e.clientY, cx: wmCam.x, cy: wmCam.y, moved: false };
      stage.classList.add('grabbing'); e.preventDefault();
    });
    stage.addEventListener('mousemove', function (e) {
      const r = stage.getBoundingClientRect();
      const mx = e.clientX - r.left, my = e.clientY - r.top;
      const tx = Math.floor(wmCam.x + (mx - stage.clientWidth / 2) / wmCam.z);
      const ty = Math.floor(wmCam.y - (my - stage.clientHeight / 2) / wmCam.z);
      if (tx !== wmHover.x || ty !== wmHover.y) { wmHover = { x: tx, y: ty }; wmPaintStatus(); wmHoverBox(); }
      const tip = $('wmTip');
      if (tip && !wmDrag) {
        let hit = null, hd = Infinity;
        for (const m of wmMarks) { const d = Math.hypot(m.sx - mx, m.sy - my); if (d <= m.r + 5 && d < hd) { hd = d; hit = m; } }
        if (hit) {
          tip.innerHTML = hit.ts ? wmTeleTip(hit.ts) : hit.tip; tip.style.display = 'block';
          let lx = mx + 12, ty2 = my + 10;
          const tw = tip.offsetWidth, th = tip.offsetHeight;
          if (lx + tw > r.width - 2) lx = mx - tw - 12;
          if (lx < 2) lx = 2;
          if (ty2 + th > r.height - 2) ty2 = r.height - th - 2;
          if (ty2 < 2) ty2 = 2;
          tip.style.left = lx + 'px'; tip.style.top = ty2 + 'px';
        } else tip.style.display = 'none';
      }
    });
    stage.addEventListener('mouseleave', function () { const tip = $('wmTip'); if (tip) tip.style.display = 'none'; wmHover = { x: -1, y: -1 }; wmPaintStatus(); wmHoverBox(); });
    if (!wmWinBound) {
      wmWinBound = true;
      window.addEventListener('mousemove', function (e) {
        if (!wmDrag) return;
        const st2 = document.getElementById('wmStage'); if (!st2) { wmDrag = null; return; }
        const dx = e.clientX - wmDrag.x, dy = e.clientY - wmDrag.y;
        if (Math.abs(dx) + Math.abs(dy) > 3) { wmDrag.moved = true; if (wmFollow) { wmFollow = false; wmPaintBar(); } }
        wmCam.x = Math.max(64, Math.min(16320, wmDrag.cx - dx / wmCam.z));
        wmCam.y = Math.max(64, Math.min(16320, wmDrag.cy + dy / wmCam.z));
        wmKick();
      });
      window.addEventListener('mouseup', function (e) {
        const st2 = document.getElementById('wmStage');
        if (wmDrag && st2 && !wmDrag.moved) {                   // click (no drag) = pin the tile
          const r = st2.getBoundingClientRect();
          const mx = e.clientX - r.left, my = e.clientY - r.top;
          if (mx >= 0 && my >= 0 && mx <= r.width && my <= r.height) {
            const tx = Math.floor(wmCam.x + (mx - st2.clientWidth / 2) / wmCam.z);
            const ty = Math.floor(wmCam.y - (my - st2.clientHeight / 2) / wmCam.z);
            wmSel = { x: tx, y: ty, p: wmCam.p, nm: '', dt: '' };
            wmKick();
          }
        }
        if (wmDrag) { wmDrag = null; if (st2) st2.classList.remove('grabbing'); wmSaveCam(); }
      });
      window.addEventListener('resize', function () { if (activeTab === 'worldmap') wmKick(); });
    }
    $('wmBar').addEventListener('click', function (e) {
      const chip = e.target.closest ? e.target.closest('.wm-chip') : null; if (!chip) return;
      const k = chip.dataset.wm;
      if (k && k.indexOf('f') === 0 && k.length === 2) wmSetPlane(+k[1]);
      else if (k === 'follow') { wmFollow = !wmFollow; wmPaintBar(); wmKick(); }
      else if (k && wmLayer[k] !== undefined) { wmLayer[k] = !wmLayer[k]; wmSaveLayers(); wmPaintBar(); wmKick(); }
      else if (chip.id === 'wmYou') { if (wmPlayer) wmFlyTo(wmPlayer.x, wmPlayer.y, wmPlayer.p, null); }
      else if (chip.id === 'wmZoomOut') zoomAt(stage.clientWidth / 2, stage.clientHeight / 2, 1 / 1.5);
      else if (chip.id === 'wmZoomIn') zoomAt(stage.clientWidth / 2, stage.clientHeight / 2, 1.5);
    });
    inp.addEventListener('input', function () { wmResSel = 0; wmRenderResults(); });
    inp.addEventListener('focus', function () { wmRenderResults(); });
    inp.addEventListener('blur', function () { setTimeout(function () { const b = $('wmResults'); if (b) b.style.display = 'none'; }, 150); });   // let a result click land first
    inp.addEventListener('keydown', function (e) {
      const es = (res && res._es) || [];
      if (e.key === 'ArrowDown') { wmResSel = Math.min(es.length - 1, wmResSel + 1); wmRenderResults(); e.preventDefault(); }
      else if (e.key === 'ArrowUp') { wmResSel = Math.max(0, wmResSel - 1); wmRenderResults(); e.preventDefault(); }
      else if (e.key === 'Enter') { wmPickResult(wmResSel); e.preventDefault(); }
      else if (e.key === 'Escape') { res.style.display = 'none'; inp.blur(); }
    });
    res.addEventListener('mousedown', function (e) {           // mousedown beats the input blur timer
      const row = e.target.closest ? e.target.closest('.wm-res') : null;
      if (row) { wmPickResult(+row.dataset.i); e.preventDefault(); }
    });
    wmPaintBar();
    wmKick();
  }
