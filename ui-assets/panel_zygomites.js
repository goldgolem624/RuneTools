// RuneToolsX panel: Ancient Zygomites (Anachronia hidden-zygomite hunt: island map + live tracker).
// Spliced inline into client.html; shares the global scope. Ground truth: achievement 1854
// "Leave No Zygomite Buried" (js5-57) names each zygomite and its 2-bit morph varbit
// (44387-44446 = morph parent NPCs 26484-26543 in the same order; state 0 = "Flower" [Pull],
// >= 1 = "Ancient zygomite" found - the same >= 1 rule the game's own progress script uses).
// Tiles = the route sheet's lat/long through x = 2440 + E*8/15, y = 3161 - S*8/15, a fit
// live-validated to <1 tile on three scene captures (Happy/Bobby/Dank).

  const ZYG_SPOTS = [
    {"r":1,"nm":"Frank","x":5323,"y":2181,"vb":44392,"d":"At the south end of the beach where The Stormbreaker arrives at the island."},
    {"r":2,"nm":"Happy","x":5378,"y":2280,"vb":44402,"d":"Requires 30 Agility. Head north from the beach and cross the vines shortcut."},
    {"r":3,"nm":"Dank","x":5363,"y":2268,"vb":44395,"d":"From Happy, go back across the vines and enter the path going under them; Dank is close to the entrance."},
    {"r":4,"nm":"Bobby","x":5393,"y":2310,"vb":44404,"d":"Continue all the way down the path; Bobby is at the end."},
    {"r":5,"nm":"Pete","x":5415,"y":2208,"vb":44400,"d":"Go south back to the beach, then take the path east; just off the path."},
    {"r":6,"nm":"Travis","x":5424,"y":2176,"vb":44401,"d":"Keep going past Pete to a junction going north-west; at the end of this short path under some foliage."},
    {"r":7,"nm":"Wayne","x":5449,"y":2160,"vb":44407,"d":"South of the Agility Course: take the first fork west and go all the way to the dead end."},
    {"r":8,"nm":"Morty","x":5524,"y":2214,"vb":44416,"d":"In the grenwall ruins; just north-east of the stairs down, by the building with a large root on it."},
    {"r":9,"nm":"Beverly","x":5503,"y":2198,"vb":44408,"d":"Same ruins as Morty; just inside the last doorway on the north-west."},
    {"r":10,"nm":"Dabby","x":5559,"y":2294,"vb":44417,"d":"Far north-east corner of the spicati apoterrasaur area, behind a tree."},
    {"r":11,"nm":"Pat","x":5501,"y":2282,"vb":44411,"d":"On a ledge north-west of the spicati apoterrasaur area, close to a root of a large tree."},
    {"r":12,"nm":"Nero","x":5451,"y":2281,"vb":44409,"d":"Up the steep thin incline by the common jadinkos, near a tree at the end of the path north."},
    {"r":13,"nm":"Edd","x":5399,"y":2301,"vb":44403,"d":"Through the bagrada rex area to the north-west, at the end of a short dead-end path."},
    {"r":14,"nm":"June","x":5467,"y":2272,"vb":44410,"d":"Past the common jadinkos, heading north on the path next to the river."},
    {"r":15,"nm":"Stuart","x":5547,"y":2374,"vb":44418,"d":"Near the waterfall past the aquatic jadinkos, at the base of the tree."},
    {"r":16,"nm":"Clay","x":5549,"y":2410,"vb":44419,"d":"North past the tree, east across the water; on the riverbed."},
    {"r":17,"nm":"Nate","x":5511,"y":2386,"vb":44420,"d":"On the peninsula between the amphibious jadinkos and the malletops."},
    {"r":18,"nm":"Howard","x":5448,"y":2384,"vb":44412,"d":"South-western corner of the malletops hunting grounds."},
    {"r":19,"nm":"Bernadette","x":5480,"y":2466,"vb":44414,"d":"On the narrow passage that goes underneath the Agility course."},
    {"r":20,"nm":"Raj","x":5450,"y":2495,"vb":44413,"d":"By the camouflaged jadinkos, under the overhanging tree at the orthenglass cache digsite."},
    {"r":21,"nm":"Noel","x":5499,"y":2492,"vb":44415,"d":"Ruins south of the feral dinosaurs; the ruin south-east of the fire."},
    {"r":22,"nm":"Carter","x":5528,"y":2500,"vb":44422,"d":"Just north of the ruined temple at the north end of the pavosaurus rex area."},
    {"r":23,"nm":"James","x":5431,"y":2515,"vb":44406,"d":"South of the venomous dinosaurs in the north-west; near large, medium and small bulbous cacti."},
    {"r":24,"nm":"Jerry","x":5396,"y":2533,"vb":44405,"d":"North past the venomous dinosaurs, west up a hill; next to a bulbous cactus."},
    {"r":25,"nm":"Abel","x":5357,"y":2552,"vb":44398,"d":"Very north-west point, on the path just east of the time ruins."},
    {"r":26,"nm":"John","x":5326,"y":2516,"vb":44397,"d":"Hidden behind a giant skeleton in the cannibal jadinko area."},
    {"r":27,"nm":"Charlie","x":5293,"y":2446,"vb":44390,"d":"On the southern path at the base of the second bulbous cactus."},
    {"r":28,"nm":"Dennis","x":5267,"y":2369,"vb":44389,"d":"Past the totem pedestal to the north-west, at the end of the dead end."},
    {"r":29,"nm":"Rami","x":5328,"y":2391,"vb":44396,"d":"At the bottom of the Anachronia Western Ruins mine."},
    {"r":30,"nm":"Opie","x":5337,"y":2249,"vb":44394,"d":"Anachronia south-west mine, just south of the light animica rocks."},
    {"r":31,"nm":"Cricket","x":5324,"y":2240,"vb":44393,"d":"Past the collapsed buildings with the pawyas; north-east dead end past the dragonkin statue."},
    {"r":32,"nm":"Olivia","x":5290,"y":2222,"vb":44388,"d":"Through the building with the felt material caches, south-west of Cricket."},
    {"r":33,"nm":"Mac","x":5285,"y":2186,"vb":44387,"d":"Take the first fork on the south-west heading north; at the dead end."},
    {"r":34,"nm":"Dee","x":5330,"y":2164,"vb":44391,"d":"On the outcrop overlooking The Stormbreaker ship."},
    {"r":35,"nm":"Tig","x":5377,"y":2097,"vb":44399,"d":"At the end of the south-west peninsula."},
    {"r":36,"nm":"Jenny","x":5567,"y":2490,"vb":44421,"d":"Follow the east wall away from the feral dinosaurs; right next to a bulbous cactus."},
    {"r":37,"nm":"Juice","x":5537,"y":2555,"vb":44423,"d":"Up the narrow path north-west, next to a pond near the brutish dinosaurs."},
    {"r":38,"nm":"Jeremy","x":5667,"y":2514,"vb":44441,"d":"On an outcrop along the northern path leading east."},
    {"r":39,"nm":"Leonard","x":5705,"y":2470,"vb":44446,"d":"East of the north-easternmost ruins."},
    {"r":40,"nm":"Chibs","x":5662,"y":2456,"vb":44440,"d":"Past the Anachronia canyon mine, west of the north-easternmost ruins."},
    {"r":41,"nm":"Tara","x":5685,"y":2431,"vb":44438,"d":"Directly south of the north-easternmost ruins."},
    {"r":42,"nm":"Gemma","x":5639,"y":2425,"vb":44439,"d":"North through a small path from the liverworts, south-east of the ruined temple wall's eastern end."},
    {"r":43,"nm":"Jax","x":5644,"y":2397,"vb":44437,"d":"Requires 70 Agility (roots + two vines from Gemma). North of the Overgrown idol."},
    {"r":44,"nm":"Beth","x":5665,"y":2366,"vb":44434,"d":"North-west of the corbicula rex area, near the cross vines shortcut."},
    {"r":45,"nm":"Penny","x":5730,"y":2346,"vb":44445,"d":"In the ruins north of the waterfall, on the ledge east of the dragonkin statue."},
    {"r":46,"nm":"Jessica","x":5692,"y":2320,"vb":44436,"d":"Requires 85 Agility. From Penny, jump the big block at the south-west building."},
    {"r":47,"nm":"Taylor","x":5688,"y":2285,"vb":44433,"d":"On a ledge south of the path east past the shadow jadinkos."},
    {"r":48,"nm":"Amy","x":5724,"y":2239,"vb":44444,"d":"Devil's snare area on the south-eastern island, at the north-east."},
    {"r":49,"nm":"Summer","x":5671,"y":2304,"vb":44435,"d":"Just west of the waterfall."},
    {"r":50,"nm":"Rick","x":5660,"y":2278,"vb":44432,"d":"Lampenflora area, south-eastern path south of the water pool; south of the vines shortcut."},
    {"r":51,"nm":"Barry","x":5611,"y":2226,"vb":44426,"d":"South of the pond near two mushrooms."},
    {"r":52,"nm":"Chris","x":5615,"y":2301,"vb":44428,"d":"North then south-east from the lampenflora area."},
    {"r":53,"nm":"Richard","x":5621,"y":2332,"vb":44429,"d":"By the poison frogs then east, on the edge of the cliff."},
    {"r":54,"nm":"Dave","x":5586,"y":2200,"vb":44425,"d":"South-eastern peninsula: on the north-west edge after crossing the water, between water and a tree root."},
    {"r":55,"nm":"Eddy","x":5631,"y":2206,"vb":44427,"d":"Shore area with fishing spots, all the way down the path to the west."},
    {"r":56,"nm":"Sheldon","x":5715,"y":2176,"vb":44443,"d":"Ruin just north-east of Laniakea; just east of the most eastern building."},
    {"r":57,"nm":"Ruth","x":5656,"y":2155,"vb":44431,"d":"Near the main staircase just north of Laniakea, by the orthen teleportation device."},
    {"r":58,"nm":"Liam","x":5594,"y":2106,"vb":44424,"d":"South-west path from the Anachronia Swamp mine, south past the devil's snare."},
    {"r":59,"nm":"Mary","x":5648,"y":2091,"vb":44430,"d":"All the way south to the edge of the water, on the path east of the swamp mine."},
    {"r":60,"nm":"Ed","x":5702,"y":2130,"vb":44442,"d":"Continue east on the path from Mary, all the way to the end."}
  ];
  const ZYG_COUNT_VB = 44447;   // live-observed total-found counter (6-bit, server-side; no CS2 readers)
  const ZYG_TS = 4, ZYG_CH = 128;              // one native level: 4 px/tile, 128-tile chunks
  // Anachronia proper is chunks ix 41-44, iy 16-19 (x 5248-5759, y 2048-2559); the map squares
  // around it hold unrelated instanced content, so nothing outside ever draws or fetches.
  const ZYG_IX0 = 41, ZYG_IX1 = 44, ZYG_IY0 = 16, ZYG_IY1 = 19;
  const ZYG_ZMIN = 0.5, ZYG_ZMAX = 4;
  let zygCam = { x: 5498, y: 2323, z: 0.78 };  // island centre; camera is per-session
  let zygVb = null, zygCount = 0, zygPlayer = null, zygSel = -1, zygDrag = null, zygFilter = '';
  const zygChunks = new Map();                 // "ix|iy" -> canvas | null (no data)
  const zygQueue = new Map();
  let zygInflight = 0, zygRafOn = false, zygWinBound = false, zygTickN = 0;

  function zygKick() {
    if (zygRafOn) return; zygRafOn = true;
    (typeof requestAnimationFrame === 'function' ? requestAnimationFrame : function (cb) { setTimeout(cb, 16); })(function () {
      zygRafOn = false;
      if (typeof activeTab === 'undefined' || activeTab !== 'zygomites') return;
      try { zygDraw(); } catch (e) {}
    });
  }
  function zygPump() {
    if (zygInflight >= 2) return;
    let best = null;
    for (const j of zygQueue.values()) if (!j.busy && (!best || j.pri < best.pri)) best = j;
    if (!best || !bridge() || !bridge().mapWindow) return;
    best.busy = true; zygInflight++;
    Promise.resolve(bridge().mapWindow(best.cx, best.cy, 0, ZYG_CH / 2, ZYG_TS)).then(function (raw) {
      let cv = null;
      try {
        const meta = JSON.parse(raw || '{}');
        if (meta && meta.b64) {
          const W = meta.w | 0, bin = atob(meta.b64), a = new Uint8ClampedArray(bin.length);
          for (let i = 0; i < bin.length; i++) a[i] = bin.charCodeAt(i);
          cv = document.createElement('canvas'); cv.width = W; cv.height = W;
          cv.getContext('2d').putImageData(new ImageData(a, W, W), 0, 0);
        }
      } catch (e) {}
      zygChunks.set(best.key, cv);
      zygInflight--; zygQueue.delete(best.key);
      zygKick(); zygPump();
    }, function () { zygInflight--; zygQueue.delete(best.key); setTimeout(zygPump, 1500); });
  }

  function zygFound(z) { return !!(zygVb && (zygVb[z.vb] | 0) >= 1); }
  function zygState(z) {
    if (!zygVb) return '';
    const v = zygVb[z.vb] | 0;
    return v >= 2 ? 'pulled' : v === 1 ? 'discovered' : 'missing';
  }
  async function zygRefresh() {
    try {
      const ids = ZYG_SPOTS.map(function (z) { return z.vb; }); ids.push(ZYG_COUNT_VB);
      const v = await readVarbitValues(ids);
      const sig = JSON.stringify(v);
      if (sig !== zygRefresh._sig) {
        zygRefresh._sig = sig; zygVb = v; zygCount = v[ZYG_COUNT_VB] | 0;
        zygPaintHead(); zygPaintList(); zygKick();
      } else if (!zygVb) { zygVb = v; }
    } catch (e) {}
  }
  function zygOpen() { zygRefresh(); zygKick(); }

  setInterval(async function () {
    if (typeof activeTab === 'undefined' || activeTab !== 'zygomites' || !document.getElementById('zygStage')) return;
    if (++zygTickN % 4 === 0) zygRefresh();
    try {
      const P = await scanPlayerTile();
      const moved = !!P !== !!zygPlayer || (P && zygPlayer && (P.x !== zygPlayer.x || P.y !== zygPlayer.y));
      zygPlayer = P;
      if (moved) zygKick();
    } catch (e) {}
  }, 700);

  function zygDraw() {
    const stage = $('zygStage'), cv = $('zygCanvas');
    if (!stage || !cv) return;
    const dpr = window.devicePixelRatio || 1;
    const w = stage.clientWidth, h = stage.clientHeight;
    if (!w || !h) return;
    if (cv.width !== Math.round(w * dpr) || cv.height !== Math.round(h * dpr)) {
      cv.width = Math.round(w * dpr); cv.height = Math.round(h * dpr);
      cv.style.width = w + 'px'; cv.style.height = h + 'px';
    }
    const cx = cv.getContext('2d');
    cx.setTransform(dpr, 0, 0, dpr, 0, 0);
    cx.fillStyle = '#0b0d12'; cx.fillRect(0, 0, w, h);
    const z = zygCam.z;
    const sx = function (wx) { return (wx - zygCam.x) * z + w / 2; };
    const sy = function (wy) { return (zygCam.y - wy) * z + h / 2; };
    const vx0 = zygCam.x - w / 2 / z, vx1 = zygCam.x + w / 2 / z;
    const vy0 = zygCam.y - h / 2 / z, vy1 = zygCam.y + h / 2 / z;
    cx.imageSmoothingEnabled = true; cx.imageSmoothingQuality = 'high';
    for (let ix = Math.max(ZYG_IX0, Math.floor(vx0 / ZYG_CH)); ix <= Math.min(ZYG_IX1, Math.floor(vx1 / ZYG_CH)); ix++) {
      for (let iy = Math.max(ZYG_IY0, Math.floor(vy0 / ZYG_CH)); iy <= Math.min(ZYG_IY1, Math.floor(vy1 / ZYG_CH)); iy++) {
        const key = ix + '|' + iy;
        if (!zygChunks.has(key) && !zygQueue.has(key)) {
          const ccx = ix * ZYG_CH + ZYG_CH / 2, ccy = iy * ZYG_CH + ZYG_CH / 2;
          zygQueue.set(key, { key: key, cx: ccx, cy: ccy, busy: false,
                              pri: Math.abs(ccx - zygCam.x) + Math.abs(ccy - zygCam.y) });
        }
        const img = zygChunks.get(key);
        if (!img) continue;
        // integer-snap shared edges so adjacent chunks never seam
        const x0 = Math.round(sx(ix * ZYG_CH)), x1 = Math.round(sx((ix + 1) * ZYG_CH));
        const y0 = Math.round(sy((iy + 1) * ZYG_CH)), y1 = Math.round(sy(iy * ZYG_CH));
        cx.drawImage(img, x0, y0, x1 - x0, y1 - y0);
      }
    }
    zygPump();
    // spot markers: route number in a disc; green when the morph varbit says found
    const marks = [];
    for (const s of ZYG_SPOTS) {
      const mx = sx(s.x + 0.5), my = sy(s.y + 0.5);
      if (mx < -20 || mx > w + 20 || my < -20 || my > h + 20) continue;
      const found = zygFound(s), r0 = s.r === zygSel ? 11 : 9;
      cx.globalAlpha = found ? 0.85 : 1;
      cx.beginPath(); cx.arc(mx, my, r0, 0, 6.2832);
      cx.fillStyle = found ? '#173b28' : '#3b2a17'; cx.fill();
      cx.lineWidth = s.r === zygSel ? 2.4 : 1.6;
      cx.strokeStyle = found ? '#4dd28a' : '#fbbf24'; cx.stroke();
      cx.font = '700 ' + (s.r === zygSel ? 11 : 10) + 'px system-ui, sans-serif';
      cx.textAlign = 'center'; cx.textBaseline = 'middle';
      cx.fillStyle = found ? '#4dd28a' : '#fbbf24';
      cx.fillText(String(s.r), mx, my + 0.5);
      cx.globalAlpha = 1;
      marks.push({ sx: mx, sy: my, r: r0 + 3, s: s });
    }
    zygDraw._marks = marks;
    if (zygPlayer) {
      const mx = sx(zygPlayer.x + 0.5), my = sy(zygPlayer.y + 0.5);
      if (mx >= -20 && mx <= w + 20 && my >= -20 && my <= h + 20) {
        cx.beginPath(); cx.arc(mx, my, 5.5, 0, 6.2832);
        cx.fillStyle = '#ffd23f'; cx.fill();
        cx.lineWidth = 2; cx.strokeStyle = '#fff'; cx.stroke();
      }
    }
  }

  function zygPaintHead() {
    const el = $('zygHead'); if (!el) return;
    const found = ZYG_SPOTS.filter(zygFound).length;
    const pct = Math.round(found / ZYG_SPOTS.length * 100);
    el.innerHTML = '<div class="zyg-title">Ancient Zygomites</div>'
      + '<div class="zyg-sub">Anachronia hidden zygomites - pull each Flower once. '
      + (zygVb ? ('<b>' + found + '</b>/60 found'
          + (zygCount !== found ? ' <span style="color:#fbbf24">(game counts ' + zygCount + ')</span>' : '')) : 'reading progress...')
      + '</div>'
      + '<div class="zyg-bar"><div style="width:' + pct + '%"></div></div>';
  }
  function zygFly(s) {
    zygSel = s.r;
    zygCam.x = s.x + 0.5; zygCam.y = s.y + 0.5;
    if (zygCam.z < 1.8) zygCam.z = 1.8;
    zygPaintList(); zygKick();
  }
  function zygPaintList() {
    const el = $('zygList'); if (!el) return;
    const q = zygFilter.trim().toLowerCase();
    let html = '';
    for (const s of ZYG_SPOTS) {
      if (q && s.nm.toLowerCase().indexOf(q) < 0 && String(s.r) !== q) continue;
      const found = zygFound(s);
      html += '<div class="zyg-row' + (found ? ' done' : '') + (s.r === zygSel ? ' sel' : '') + '" data-r="' + s.r + '">'
        + '<span class="zyg-n">' + s.r + '</span><span class="zyg-nm">' + htmlEsc(s.nm) + '</span>'
        + '<span class="zyg-st">' + zygState(s) + '</span></div>';
    }
    el.innerHTML = html || '<div style="opacity:.55;padding:8px 10px">no match</div>';
  }

  function renderZygomites() {
    const c = $('content'); if (!c) return;
    if ($('zygStage')) { zygKick(); return; }   // renderPane polls at 250ms: never rebuild a live mount (kills drag/filter focus)
    c.innerHTML = '';
    injectStyle('zygCss',
      '.zyg-wrap{display:flex;flex-direction:column;gap:8px;height:100%;min-height:0}'
      + '.zyg-head{flex:0 0 auto}'
      + '.zyg-title{font-weight:700;font-size:15px}'
      + '.zyg-sub{opacity:.85;font-size:12px;margin-top:2px}'
      + '.zyg-bar{height:5px;border-radius:3px;background:rgba(255,255,255,0.08);margin-top:6px;overflow:hidden}'
      + '.zyg-bar > div{height:100%;background:#4dd28a;border-radius:3px;transition:width 300ms var(--ease)}'
      + '.zyg-main{display:flex;flex-direction:column;gap:8px;flex:1;min-height:0}'
      + '.zyg-stage{position:relative;flex:1 1 62%;min-height:240px;min-width:0;border:1px solid var(--border-hi);border-radius:10px;overflow:hidden;background:#0b0d12;cursor:grab}'
      + '.zyg-stage.grabbing{cursor:grabbing}'
      + '.zyg-stage canvas{display:block;position:absolute;inset:0}'
      + '.zyg-tip{position:absolute;display:none;pointer-events:none;z-index:20;max-width:280px;background:#12151d;border:1px solid var(--border-hi);border-radius:8px;padding:7px 9px;font-size:12px;line-height:1.45;box-shadow:0 8px 22px rgba(0,0,0,0.55)}'
      + '.zyg-side{flex:1 1 38%;min-height:130px;display:flex;flex-direction:column;gap:6px}'
      + '.zyg-filter{flex:0 0 auto;background:#12151d;border:1px solid var(--border-hi);border-radius:8px;color:inherit;font:inherit;font-size:12px;padding:6px 9px;outline:none}'
      + '.zyg-list{flex:1;overflow-y:auto;border:1px solid var(--border-hi);border-radius:10px;background:rgba(255,255,255,0.02)}'
      + '.zyg-row{display:flex;align-items:center;gap:8px;padding:5px 9px;font-size:12px;cursor:pointer;border-bottom:1px solid rgba(255,255,255,0.05)}'
      + '.zyg-row:hover{background:rgba(255,255,255,0.05)}'
      + '.zyg-row.sel{background:rgba(77,210,138,0.10)}'
      + '.zyg-row .zyg-n{flex:0 0 22px;text-align:right;opacity:.6;font-variant-numeric:tabular-nums}'
      + '.zyg-row .zyg-nm{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}'
      + '.zyg-row .zyg-st{flex:0 0 auto;font-size:10.5px;color:#fbbf24}'
      + '.zyg-row.done .zyg-st{color:#4dd28a}'
      + '.zyg-row.done .zyg-nm{opacity:.6}');
    const wrap = document.createElement('div');
    wrap.className = 'zyg-wrap';
    wrap.innerHTML = '<div class="zyg-head card" id="zygHead"></div>'
      + '<div class="zyg-main">'
      + '  <div class="zyg-stage" id="zygStage"><canvas id="zygCanvas"></canvas><div class="zyg-tip" id="zygTip"></div></div>'
      + '  <div class="zyg-side">'
      + '    <input class="zyg-filter" id="zygFilter" type="text" placeholder="Filter by name or route #..." autocomplete="off" spellcheck="false">'
      + '    <div class="zyg-list" id="zygList"></div>'
      + '  </div>'
      + '</div>';
    c.appendChild(wrap);
    const stage = $('zygStage');
    const zoomAt = function (mx, my, f) {
      const nz = Math.max(ZYG_ZMIN, Math.min(ZYG_ZMAX, zygCam.z * f));
      if (nz === zygCam.z) return;
      const wx = zygCam.x + (mx - stage.clientWidth / 2) / zygCam.z;
      const wy = zygCam.y - (my - stage.clientHeight / 2) / zygCam.z;
      zygCam.z = nz;
      zygCam.x = wx - (mx - stage.clientWidth / 2) / nz;
      zygCam.y = wy + (my - stage.clientHeight / 2) / nz;
      zygKick();
    };
    stage.addEventListener('wheel', function (e) {
      e.preventDefault();
      const r = stage.getBoundingClientRect();
      zoomAt(e.clientX - r.left, e.clientY - r.top, e.deltaY < 0 ? 1.22 : 1 / 1.22);
    }, { passive: false });
    stage.addEventListener('mousedown', function (e) {
      zygDrag = { x: e.clientX, y: e.clientY, cx: zygCam.x, cy: zygCam.y, moved: false };
      stage.classList.add('grabbing'); e.preventDefault();
    });
    stage.addEventListener('mousemove', function (e) {
      const tip = $('zygTip'); if (!tip || zygDrag) return;
      const r = stage.getBoundingClientRect();
      const mx = e.clientX - r.left, my = e.clientY - r.top;
      let hit = null, hd = Infinity;
      for (const m of (zygDraw._marks || [])) {
        const d = Math.hypot(m.sx - mx, m.sy - my);
        if (d <= m.r + 4 && d < hd) { hd = d; hit = m; }
      }
      if (hit) {
        const s = hit.s, found = zygFound(s);
        tip.innerHTML = '<b>#' + s.r + ' ' + htmlEsc(s.nm) + '</b> <span style="color:' + (found ? '#4dd28a' : '#fbbf24') + '">' + zygState(s) + '</span>'
          + '<br><span style="opacity:.6">' + s.x + ', ' + s.y + '</span>'
          + '<br><span style="opacity:.85">' + htmlEsc(s.d) + '</span>';
        tip.style.display = 'block';
        let lx = mx + 12, ty = my + 10;
        const tw = tip.offsetWidth, th = tip.offsetHeight;
        if (lx + tw > r.width - 2) lx = mx - tw - 12;
        if (lx < 2) lx = 2;
        if (ty + th > r.height - 2) ty = r.height - th - 2;
        if (ty < 2) ty = 2;
        tip.style.left = lx + 'px'; tip.style.top = ty + 'px';
      } else tip.style.display = 'none';
    });
    stage.addEventListener('mouseleave', function () { const tip = $('zygTip'); if (tip) tip.style.display = 'none'; });
    if (!zygWinBound) {
      zygWinBound = true;
      window.addEventListener('mousemove', function (e) {
        if (!zygDrag) return;
        const st = document.getElementById('zygStage'); if (!st) { zygDrag = null; return; }
        const dx = e.clientX - zygDrag.x, dy = e.clientY - zygDrag.y;
        if (Math.abs(dx) + Math.abs(dy) > 3) zygDrag.moved = true;
        zygCam.x = Math.max(5248, Math.min(5760, zygDrag.cx - dx / zygCam.z));
        zygCam.y = Math.max(2048, Math.min(2560, zygDrag.cy + dy / zygCam.z));
        zygKick();
      });
      window.addEventListener('mouseup', function (e) {
        const st = document.getElementById('zygStage');
        if (zygDrag && st && !zygDrag.moved) {
          const r = st.getBoundingClientRect();
          const mx = e.clientX - r.left, my = e.clientY - r.top;
          let hit = null, hd = Infinity;
          for (const m of (zygDraw._marks || [])) {
            const d = Math.hypot(m.sx - mx, m.sy - my);
            if (d <= m.r + 4 && d < hd) { hd = d; hit = m; }
          }
          if (hit) { zygSel = hit.s.r; zygPaintList(); zygKick(); }
        }
        if (zygDrag) { zygDrag = null; if (st) st.classList.remove('grabbing'); }
      });
      window.addEventListener('resize', function () { if (typeof activeTab !== 'undefined' && activeTab === 'zygomites') zygKick(); });
    }
    $('zygFilter').addEventListener('input', function () { zygFilter = this.value; zygPaintList(); });
    $('zygList').addEventListener('click', function (e) {
      const row = e.target.closest ? e.target.closest('.zyg-row') : null; if (!row) return;
      const s = ZYG_SPOTS.find(function (z) { return z.r === +row.dataset.r; });
      if (s) zygFly(s);
    });
    zygPaintHead(); zygPaintList(); zygKick(); zygRefresh();
  }


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
    if (typeof activeTab === 'undefined' || activeTab !== 'anachronia' || !document.getElementById('anaWrap')) return;
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
    let h = '<div class="card ana-head"><div class="zyg-title">Anachronia Base Camp</div>'
      + '<div class="zyg-sub">Workers <b>' + used + '</b>/' + wmax + ' assigned - each gathers 60/hr. '
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
    wrap.innerHTML = '<div class="card"><div class="zyg-sub">Reading base camp data...</div></div>';
    c.appendChild(wrap);
    anaPaint(); anaRefresh();
  }
