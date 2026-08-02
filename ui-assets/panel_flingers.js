// RuneToolsX panel: Fish Flingers (competition timer, discovered ratings, catches, big fish).
// Spliced inline into client.html at load; bare classic script sharing one global scope.
// The ratings are NOT in the cache - they are generated per competition and delivered as
// interface text - so everything here is read from the live interface and scene.

  // The six species slots and their order are cache-baked (interface 922 defaults); only the
  // adjective changes per competition, so rows are matched by their fish TYPE.
  const FF_TYPES = ['herring', 'cod', 'bass', 'trout', 'pike', 'salmon'];
  const FF_RATINGS_GROUP = 1286;    // "Discovered Ratings"
  const FF_RESULTS_GROUP = 922;     // "Competition Results"

  let ffGroups = [];        // open interface groups
  let ffTimer = null;       // {mm, ss, gid} remaining
  let ffRows = [];          // [{species, habitat, weight, bait, hook, distance}]
  let ffCatch = [];         // [{item, n}] catch strip, most recent first
  let ffBig = [];           // big fish in scene
  let ffTackle = [];        // current tackle slots
  let ffSeenAt = 0;

  function ffText(w) { return String(w.x || '').replace(/<[^>]*>/g, '').trim(); }

  // The timer is three adjacent TEXT components: "06", ":", "04". Found by shape so a
  // changed interface id cannot silently break it.
  function ffFindTimer(ws) {
    const t = ws.filter(function (w) { return w.ty === 'text'; })
                .map(function (w) { return { x: (w.r || [0])[0], y: (w.r || [0, 0])[1], s: ffText(w) }; })
                .filter(function (w) { return w.s; });
    for (let i = 0; i < t.length; i++) {
      if (t[i].s !== ':') continue;
      const near = function (o) { return Math.abs(o.y - t[i].y) <= 6 && Math.abs(o.x - t[i].x) <= 40; };
      const mm = t.filter(function (o) { return o !== t[i] && near(o) && o.x < t[i].x && /^\d{1,2}$/.test(o.s); });
      const ss = t.filter(function (o) { return o !== t[i] && near(o) && o.x > t[i].x && /^\d{1,2}$/.test(o.s); });
      if (mm.length && ss.length) return { mm: +mm[mm.length - 1].s, ss: +ss[0].s };
    }
    return null;
  }

  // The catch strip is a run of GRAPHIC components on one row, each an item icon. The reader
  // encodes an obj-icon graphic as 131072 + item id.
  function ffFindCatches(ws) {
    const g = ws.filter(function (w) { return w.ty === 'graphic' && w.s >= 131072; })
                .map(function (w) { return { x: (w.r || [0])[0], y: (w.r || [0, 0])[1], id: w.s - 131072 }; });
    if (g.length < 3) return [];
    const byRow = {};
    for (const o of g) { const k = Math.round(o.y / 4); (byRow[k] = byRow[k] || []).push(o); }
    let best = [];
    for (const k in byRow) if (byRow[k].length > best.length) best = byRow[k];
    if (best.length < 3) return [];
    best.sort(function (a, b) { return a.x - b.x; });
    const out = [];
    for (const o of best) {
      const last = out[out.length - 1];
      if (last && last.item === o.id) last.n++;
      else out.push({ item: o.id, n: 1 });
    }
    return out;
  }

  // Ratings grid: cells are laid out visually, so rows come from the y of each cell and the
  // columns from x order - the component ids are NOT in the on-screen column order.
  function ffFindRatings(ws) {
    const cells = ws.filter(function (w) { return w.ty === 'text' && ffText(w); })
                    .map(function (w) { return { x: (w.r || [0])[0], y: (w.r || [0, 0])[1], s: ffText(w) }; });
    if (!cells.length) return [];
    const rows = {};
    for (const c of cells) { const k = Math.round(c.y / 6); (rows[k] = rows[k] || []).push(c); }
    const out = [];
    for (const k of Object.keys(rows).sort(function (a, b) { return a - b; })) {
      const r = rows[k].sort(function (a, b) { return a.x - b.x; });
      const names = r.map(function (c) { return c.s; });
      const first = (names[0] || '').toLowerCase();
      const type = FF_TYPES.find(function (t) { return first.indexOf(t) >= 0; });
      if (!type) continue;                       // header row and chrome drop out here
      out.push({ species: names[0], type: type, cells: names.slice(1) });
    }
    return out;
  }

  // Current tackle: each slot is a LAYER holding one graphic and (for the numeric slots) a
  // TEXT. The graphics are DYNAMIC - the reader emits no sprite id for a pointer-backed
  // graphic - so the icon cannot be named. Its pixel size and offset ARE readable and differ
  // per slot, so they stand in as a fingerprint until the mapping is learned.
  function ffFindTackle(ws) {
    const layers = ws.filter(function (w) { return w.ty === 'layer'; });
    const out = [];
    for (const L of layers) {
      const lt = L.t || [0, 0, 0], lr = L.r || [0, 0, 0, 0];
      if (lr[2] !== 30 || lr[3] !== 30) continue;            // the tackle slots are 30x30
      const kids = ws.filter(function (w) {
        const t = w.t || [0, 0, 0];
        return w !== L && (w.d || 0) > (L.d || 0) && Math.abs(t[1] - lt[1]) <= 3;
      });
      const g = kids.find(function (w) { return w.ty === 'graphic'; });
      const tx = kids.find(function (w) { return w.ty === 'text' && /^\d+$/.test(ffText(w)); });
      if (!g && !tx) continue;
      const gr = g ? (g.r || [0, 0, 0, 0]) : null;
      out.push({ comp: lt[1], y: lr[1],
                 n: tx ? +ffText(tx) : null,
                 icon: gr ? (gr[2] + 'x' + gr[3]) : null,
                 spr: g && g.s ? g.s : null });
    }
    return out.sort(function (a, b) { return a.y - b.y; });
  }

  async function ffScan() {
    if (typeof activeTab === 'undefined' || activeTab !== 'flingers' || !bridge()) return;
    try { ffGroups = (JSON.parse(bridge().interfaceGroups(myPid()) || '{}').groups) || []; } catch (e) { ffGroups = []; }

    let timer = null, rows = [], catches = [], tackle = [];
    for (const g of ffGroups) {
      let ws = [];
      try { ws = (JSON.parse(bridge().interfaceGroup(myPid(), g.id) || '{}').widgets) || []; } catch (e) { continue; }
      if (!ws.length) continue;
      if (!timer) { const t = ffFindTimer(ws); if (t) timer = t; }
      if (!rows.length && (g.id === FF_RATINGS_GROUP || g.id === FF_RESULTS_GROUP)) rows = ffFindRatings(ws);
      if (!catches.length) { const c = ffFindCatches(ws); if (c.length) catches = c; }
      if (!tackle.length) { const k = ffFindTackle(ws); if (k.length) tackle = k; }
    }
    if (timer) ffTimer = timer;
    if (rows.length) { ffRows = rows; ffSeenAt = Date.now(); }
    if (catches.length) ffCatch = catches;
    if (tackle.length) ffTackle = tackle;

    if (bridge().sceneEntities) {
      try {
        const sc = JSON.parse((await bridge().sceneEntities(myPid(), 40)) || '{}');
        const all = (sc.npcs || []).concat(sc.objects || []);
        ffBig = all.filter(function (e) { return /big fish/i.test(e.name || ''); });
      } catch (e) {}
    }
    ffPaint();
  }
  setInterval(function () { ffScan(); }, 1000);

  function ffPaint() {
    const el = $('ffBody'); if (!el) return;
    let html = '';

    html += '<div class="ff-top">'
      + '<div class="ff-clock">' + (ffTimer
          ? (ffTimer.mm + ':' + String(ffTimer.ss).padStart(2, '0'))
          : '<span class="ff-dim">--:--</span>')
      + '</div><div class="ff-clab">remaining</div>'
      + '<div class="ff-big' + (ffBig.length ? ' on' : '') + '">'
      + (ffBig.length
          ? ffBig.length + ' big fish &middot; nearest ' + Math.min.apply(null, ffBig.map(function (b) { return b.dist == null ? 99 : b.dist; })) + ' tiles'
          : 'no big fish in range')
      + '</div></div>';

    if (ffRows.length) {
      html += '<table class="ff-tab"><thead><tr><th>species</th><th>habitat</th><th>weight</th>'
        + '<th>bait</th><th>hook</th><th>distance</th></tr></thead><tbody>';
      for (const r of ffRows) {
        html += '<tr><td class="ff-sp">' + htmlEsc(r.species) + '</td>';
        for (let i = 0; i < 5; i++) {
          const v = r.cells[i] === undefined ? '' : r.cells[i];
          const unknown = !v || v === '?';
          html += '<td class="' + (unknown ? 'ff-q' : 'ff-v') + '">' + htmlEsc(unknown ? '?' : v) + '</td>';
        }
        html += '</tr>';
      }
      html += '</tbody></table>';
      const found = ffRows.reduce(function (n, r) {
        return n + r.cells.filter(function (c) { return c && c !== '?'; }).length; }, 0);
      html += '<div class="ff-note">' + found + ' of ' + (ffRows.length * 5) + ' ratings discovered</div>';
    } else {
      html += '<div class="ff-empty">Open the Discovered Ratings window to read the table.</div>';
    }

    if (ffTackle.length) {
      html += '<div class="ff-sec">current tackle</div><div class="ff-tack">';
      for (const t of ffTackle) {
        html += '<span class="ff-slot">'
          + (t.n != null ? '<b>' + t.n + '</b>' : '<b class="ff-dim">&mdash;</b>')
          + '<i>' + (t.spr ? 'spr ' + t.spr : (t.icon || 'no icon')) + '</i></span>';
      }
      html += '</div><div class="ff-note">icons are dynamic, so the sprite id is not exposed; '
        + 'the size shown is a fingerprint of which icon is up.</div>';
    }
    if (ffCatch.length) {
      let tot = 0;
      for (const c of ffCatch) tot += c.n;
      html += '<div class="ff-sec">catches <span class="ff-dim">' + tot + '</span></div><div class="ff-strip">';
      for (const c of ffCatch) {
        const u = (typeof resolveIcon === 'function') ? resolveIcon(c.item) : '';
        html += '<span class="ff-cat" title="item ' + c.item + '">'
          + (u ? '<i style="background-image:url(\'' + u + '\')"></i>' : '')
          + (c.n > 1 ? '<b>' + c.n + '</b>' : '') + '</span>';
      }
      html += '</div>';
    }
    el.innerHTML = html;
  }

  function renderFlingers() {
    const c = $('content'); if (!c) return;
    if ($('ffBody')) { ffPaint(); return; }     // renderPane polls: never rebuild a live mount
    c.innerHTML = '';
    injectStyle('ffCss',
      '.ff-wrap{display:flex;flex-direction:column;gap:10px;height:100%;min-height:0;overflow:auto}'
      + '.ff-top{display:grid;grid-template-columns:auto 1fr;grid-template-rows:auto auto;'
      + 'gap:0 10px;align-items:center}'
      + '.ff-clock{grid-row:1/3;font-size:30px;font-variant-numeric:tabular-nums;line-height:1}'
      + '.ff-clab{font-size:10px;text-transform:uppercase;letter-spacing:.08em;opacity:.45;align-self:end}'
      + '.ff-big{font-size:12px;opacity:.55;align-self:start}'
      + '.ff-big.on{opacity:1;color:#4dd28a}'
      + '.ff-tab{width:100%;border-collapse:collapse;font-size:12px}'
      + '.ff-tab th{text-align:left;font-weight:500;font-size:10px;text-transform:uppercase;'
      + 'letter-spacing:.06em;opacity:.45;padding:0 6px 4px 0;border-bottom:1px solid rgba(255,255,255,.08)}'
      + '.ff-tab td{padding:4px 6px 4px 0;border-bottom:1px solid rgba(255,255,255,.04)}'
      + '.ff-sp{white-space:nowrap}'
      + '.ff-q{opacity:.3}'
      + '.ff-v{color:#4dd28a;font-variant-numeric:tabular-nums}'
      + '.ff-note,.ff-empty{font-size:11px;opacity:.5}'
      + '.ff-sec{font-size:10px;text-transform:uppercase;letter-spacing:.08em;opacity:.45}'
      + '.ff-dim{opacity:.45}'
      + '.ff-strip{display:flex;flex-wrap:wrap;gap:4px}'
      + '.ff-cat{position:relative;width:26px;height:26px;border-radius:4px;'
      + 'background:rgba(255,255,255,.04);display:grid;place-items:center}'
      + '.ff-cat i{width:22px;height:22px;background-size:contain;background-repeat:no-repeat;'
      + 'background-position:center}'
      + '.ff-cat b{position:absolute;right:1px;bottom:0;font-size:9px;color:#fbbf24}'
      + '.ff-tack{display:flex;gap:8px;flex-wrap:wrap}'
      + '.ff-slot{display:flex;flex-direction:column;align-items:center;gap:1px;padding:4px 9px;'
      + 'border-radius:5px;background:rgba(255,255,255,.04);min-width:46px}'
      + '.ff-slot b{font-size:15px;font-variant-numeric:tabular-nums}'
      + '.ff-slot i{font-style:normal;font-size:9px;opacity:.45}');
    const w = document.createElement('div');
    w.className = 'ff-wrap';
    w.innerHTML = '<div id="ffBody"></div>';
    c.appendChild(w);
    ffPaint();
    ffScan();
  }
