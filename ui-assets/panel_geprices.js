// RuneToolsX panel: GE Prices -- real-time item prices with expandable history charts.
// Data is relayed through the RuneTools server (the single consumer of the upstream
// price API); the launcher caches latest/mapping (Bridge.cpp pricesCached/pricesMapping)
// and fetches per-item history on demand (pricesTimeseries, 5-min cached).
// Spliced inline into client.html; IIFE (window exports + registerTab; see the RTX registry in client.html).
(function () {

  // Parsed caches. The raw payloads are ~0.5 MB (latest) and ~1.4 MB (mapping); the
  // pane renders at 4 Hz, so parsing is throttled: latest every 30 s, mapping only
  // when its length changes (it is near-static).
  let gepLatest = null, gepLatestAt = 0, gepGen = 0;
  let gepMap = null, gepMapLen = -1;
  let gepShownGen = -1, gepShownQuery = null, gepShownOpen = -1;
  let gepOpenId = 0;            // expanded row's item id (0 = none)
  let gepLb = '7d';             // selected history lookback
  let gepDrawnSig = '';         // "id:lb:points" last drawn, so the chart redraws only on change
  let gepChartData = null;      // the series currently on the canvas (hover redraws from it)
  let gepSortKey = '';          // '' = default order; 'buy' | 'sell' | 'spread' | 'spreadpct'
  let gepSortDir = -1;          // -1 highest first, 1 lowest first
  let gepShownSort = '';        // sort+pin signature last rendered
  let gepPinRev = 0;            // bumped on pin/unpin so the list rebuilds
  let gepFavView = false;       // Favorites-only view toggle
  let gepPins = new Set();      // favorited item ids, persisted per machine
  try { gepPins = new Set(JSON.parse(localStorage.getItem('rtxGePins') || '[]')); } catch (e) {}
  function gepSavePins() { try { localStorage.setItem('rtxGePins', JSON.stringify([...gepPins])); } catch (e) {} }
  // Sort metric for an item (null = item lacks the data and sorts to the end).
  function gepMetric(it) {
    const p = gepLatest && gepLatest[it.id];
    if (!p) return null;
    if (gepSortKey === 'buy') return p.high != null ? p.high : null;
    if (gepSortKey === 'sell') return p.low != null ? p.low : null;
    if (p.high == null || p.low == null) return null;
    if (gepSortKey === 'spread') return p.high - p.low;
    if (gepSortKey === 'spreadpct') return p.low > 0 ? (p.high - p.low) / p.low : null;
    return null;
  }

  function gepPull() {
    const now = Date.now();
    if (!gepLatest || now - gepLatestAt > 30000) {
      try {
        const d = JSON.parse(bridge().pricesCached() || '{}');
        const data = d && d.data ? d.data : d;
        if (data && Object.keys(data).length) { gepLatest = data; gepGen++; }
      } catch (e) {}
      gepLatestAt = now;
    }
    try {
      const rawM = bridge().pricesMapping() || '[]';
      if (rawM.length !== gepMapLen) {
        const m = JSON.parse(rawM);
        if (Array.isArray(m) && m.length) {
          m.sort((a, b) => (a.name < b.name ? -1 : 1));
          for (const it of m) it._q = String(it.name || '').toLowerCase();
          gepMap = m; gepGen++;
        }
        gepMapLen = rawM.length;
      }
    } catch (e) {}
  }

  function gepAge(sec) {
    if (!sec) return '';
    const d = Math.max(0, Math.floor(Date.now() / 1000 - sec));
    return d < 90 ? d + 's' : d < 5400 ? Math.round(d / 60) + 'm' : d < 129600 ? Math.round(d / 3600) + 'h' : Math.round(d / 86400) + 'd';
  }
  const gepCss = (name, fb) => (getComputedStyle(document.documentElement).getPropertyValue(name) || fb).trim() || fb;

  // ---- history chart: avg high/low lines on a canvas, drawn in theme colors.
  // Redrawn per hover move (a few hundred points; trivial): hover = {x, y} in canvas
  // CSS px (or null). The exact cursor gets a live crosshair with price/time readouts;
  // the nearest sample gets the dashed marker + data box. ----
  // Backing-store scale for a canvas: the device pixel ratio TIMES the accumulated CSS
  // zoom the panel renders under (the font-size preference sets `zoom` on .win-body).
  // Sizing from clientWidth * dpr alone ignored that zoom, so the bitmap was smaller
  // than its painted box and the engine upscaled it -- the whole chart came out soft
  // and oversized. Capped so a big zoom on a wide panel cannot allocate a huge bitmap.
  function gepCanvasScale(cv) {
    let z = 1;
    try { if (typeof uiZoomOf === 'function') z = uiZoomOf(cv) || 1; } catch (e) {}
    if (!(z > 0) || z === 1) {
      const r = cv.getBoundingClientRect(), cw = cv.clientWidth;
      if (cw > 0 && r.width > 0) z = r.width / cw;
    }
    const k = (z || 1) * (window.devicePixelRatio || 1);
    return Math.max(1, Math.min(4, k));
  }
  function gepDrawChart(cv, series, hover) {
    const W = cv.clientWidth || 420, H = cv.clientHeight || 120;
    const k = gepCanvasScale(cv);
    // Only resize the backing store when it actually changes: assigning width/height
    // clears the canvas, and the hover path redraws on every pointer move.
    const bw = Math.round(W * k), bh = Math.round(H * k);
    if (cv.width !== bw || cv.height !== bh) { cv.width = bw; cv.height = bh; }
    const g = cv.getContext('2d');
    g.setTransform(k, 0, 0, k, 0, 0);
    cv._drawnW = W; cv._drawnH = H;
    g.clearRect(0, 0, W, H);
    const pts = series.filter(p => p.avgHighPrice != null || p.avgLowPrice != null);
    if (pts.length < 2) {
      g.fillStyle = gepCss('--text-mute', '#8b8b9e'); g.font = '11px sans-serif';
      if (pts.length === 1) {
        // A single recorded trade still deserves a mark, not an empty box: rares can go
        // months between sales. The timestamp is the START of an averaging bucket whose
        // width depends on the lookback (a day at 1y), so a coarse window can only say
        // "around" a date -- showing it as exact made 30d and 1y disagree by a day for
        // the same trade.
        const p = pts[0], v = p.avgHighPrice != null ? p.avgHighPrice : p.avgLowPrice;
        const color = p.avgHighPrice != null ? gepCss('--ok', '#4dd28a') : gepCss('--warn', '#f5b241');
        const step = series.length > 1 ? Math.abs(series[1].timestamp - series[0].timestamp) : 0;
        g.fillText('One recorded trade in this window (bucket average):', 10, H / 2 - 14);
        g.fillStyle = color;
        g.beginPath(); g.arc(16, H / 2 + 4, 3, 0, 6.2832); g.fill();
        g.font = '600 11px sans-serif';
        const mid = new Date((p.timestamp + step / 2) * 1000);
        const when = (step > 21600 ? 'around ' : 'on ') + (mid.getMonth() + 1) + '/' + mid.getDate();
        g.fillText(fmtGp(v) + ' gp ' + when, 26, H / 2 + 8);
      } else {
        g.fillText('No trades recorded in this window.', 10, H / 2);
      }
      return;
    }
    let min = Infinity, max = -Infinity;
    for (const p of pts) for (const v of [p.avgHighPrice, p.avgLowPrice])
      if (v != null) { if (v < min) min = v; if (v > max) max = v; }
    if (max === min) { max += 1; min -= 1; }
    const t0 = pts[0].timestamp, t1 = pts[pts.length - 1].timestamp;
    // Layout: legend row | price plot | volume strip | date labels.
    const padL = 6, padR = 6, padT = 16, padB = 14, volH = 24;
    const plotBot = H - padB - volH - 4;
    const X = t => padL + (W - padL - padR) * ((t - t0) / Math.max(1, t1 - t0));
    const Y = v => padT + (plotBot - padT) * (1 - (v - min) / (max - min));
    const mute = gepCss('--text-mute', '#8b8b9e');
    const dt = t => { const d = new Date(t * 1000); return (d.getMonth() + 1) + '/' + d.getDate(); };
    const dtf = t => {   // fine label: include time when the window is a day or less
      const d = new Date(t * 1000);
      const hm = ('0' + d.getHours()).slice(-2) + ':' + ('0' + d.getMinutes()).slice(-2);
      return (t1 - t0 <= 90000) ? dt(t) + ' ' + hm : dt(t);
    };
    // Labelled y gridlines at min / mid / max, and labelled x gridlines at quarter points.
    g.strokeStyle = gepCss('--border', 'rgba(255,255,255,0.08)');
    g.lineWidth = 1;
    g.font = '10px sans-serif';
    for (const v of [min, (min + max) / 2, max]) {
      g.beginPath(); g.moveTo(padL, Y(v)); g.lineTo(W - padR, Y(v)); g.stroke();
    }
    for (const f of [0.25, 0.5, 0.75]) {
      const t = t0 + (t1 - t0) * f, x = X(t);
      g.beginPath(); g.moveTo(x, padT); g.lineTo(x, H - padB); g.stroke();
      g.fillStyle = mute;
      const lbl = dtf(t);
      g.fillText(lbl, x - g.measureText(lbl).width / 2, H - padB + 11);
    }
    const okCol = gepCss('--ok', '#4dd28a'), warnCol = gepCss('--warn', '#f5b241');
    // CONSTANT lines: every sample with data connects to the next one with data, straight
    // through empty buckets (a rare that trades weekly reads as one continuous line, the
    // way the wiki draws it), with a dot on each actual sample.
    const line = (getV, color) => {
      const arr = [];
      for (const p of pts) { const v = getV(p); if (v != null) arr.push([X(p.timestamp), Y(v)]); }
      if (!arr.length) return;
      g.strokeStyle = color; g.lineWidth = 1.6; g.beginPath();
      arr.forEach(([x, y], i) => { if (i) g.lineTo(x, y); else g.moveTo(x, y); });
      g.stroke();
      if (arr.length <= 90) {
        g.fillStyle = color;
        for (const [x, y] of arr) { g.beginPath(); g.arc(x, y, 2.2, 0, 6.2832); g.fill(); }
      } else {
        const [x, y] = arr[arr.length - 1];
        g.fillStyle = color; g.beginPath(); g.arc(x, y, 2.6, 0, 6.2832); g.fill();
      }
    };
    line(p => p.avgLowPrice, warnCol);
    line(p => p.avgHighPrice, okCol);
    // Y gridline labels AFTER the lines, on a background chip: drawn first, a price line
    // passing near a gridline ran straight over its text.
    g.font = '10px sans-serif';
    for (const v of [min, (min + max) / 2, max]) {
      const lbl = fmtGp(v), lw2 = g.measureText(lbl).width;
      g.fillStyle = gepCss('--bg', '#14151c');
      g.globalAlpha = 0.85;
      g.fillRect(padL, Y(v) - 12, lw2 + 6, 12);
      g.globalAlpha = 1;
      g.fillStyle = mute;
      g.fillText(lbl, padL + 2, Y(v) - 3);
    }
    // Legend, wiki-style naming. Top RIGHT: the max-price gridline label owns the top left.
    g.font = '10px sans-serif';
    const legend = [['Instabuy', okCol], ['Instasell', warnCol]];
    let lw = 0;
    for (const [name] of legend) lw += 9 + g.measureText(name).width + 12;
    let lx = W - padR - lw;
    for (const [name, col] of legend) {
      g.fillStyle = col; g.beginPath(); g.arc(lx + 3, 7, 3, 0, 6.2832); g.fill();
      g.fillStyle = mute; g.fillText(name, lx + 9, 10);
      lx += 9 + g.measureText(name).width + 12;
    }
    // Volume strip: instabuy volume up from the strip's midline, instasell volume down.
    {
      const midY = H - padB - volH / 2 - 2;
      let vmax = 0;
      for (const p of series) vmax = Math.max(vmax, p.highPriceVolume || 0, p.lowPriceVolume || 0);
      if (vmax > 0) {
        g.strokeStyle = gepCss('--border', 'rgba(255,255,255,0.08)');
        g.beginPath(); g.moveTo(padL, midY); g.lineTo(W - padR, midY); g.stroke();
        // Bar width from the BUCKET STEP, not the sample count: the feed omits empty
        // buckets, so a sparse week may hold five samples and count-based bars became
        // day-wide slabs. Step = the smallest gap between consecutive samples.
        let step = Infinity;
        for (let i = 1; i < series.length; i++)
          step = Math.min(step, Math.abs(series[i].timestamp - series[i - 1].timestamp) || Infinity);
        if (!isFinite(step)) step = Math.max(1, t1 - t0);
        const bw = Math.max(1.5, Math.min(9, (W - padL - padR) * (step / Math.max(1, t1 - t0)) * 0.7));
        for (const p of series) {
          const x = X(p.timestamp) - bw / 2;
          const up = (p.highPriceVolume || 0) / vmax, dn = (p.lowPriceVolume || 0) / vmax;
          if (up > 0) { g.fillStyle = okCol; g.fillRect(x, midY - Math.max(1.5, up * (volH / 2 - 1)), bw, Math.max(1.5, up * (volH / 2 - 1))); }
          if (dn > 0) { g.fillStyle = warnCol; g.fillRect(x, midY + 0.5, bw, Math.max(1.5, dn * (volH / 2 - 1))); }
        }
      }
    }
    // Window edge dates.
    g.fillStyle = mute; g.font = '10px sans-serif';
    const endLbl = dtf(t1);
    g.fillText(dtf(t0), padL, H - padB + 11);
    g.fillText(endLbl, W - padR - g.measureText(endLbl).width, H - padB + 11);

    // Hover: exact-cursor crosshair with live price/time readouts...
    if (hover != null) {
      const hx = Math.max(padL, Math.min(W - padR, hover.x));
      const hy = Math.max(padT, Math.min(plotBot, hover.y));
      g.strokeStyle = mute; g.lineWidth = 0.75; g.globalAlpha = 0.6;
      g.beginPath(); g.moveTo(hx, padT); g.lineTo(hx, H - padB); g.stroke();
      g.beginPath(); g.moveTo(padL, hy); g.lineTo(W - padR, hy); g.stroke();
      g.globalAlpha = 1;
      // Price at the cursor height (right edge) and time at the cursor x (top of the axis).
      const hv = min + (1 - (hy - padT) / Math.max(1, plotBot - padT)) * (max - min);
      const ht = t0 + (hover.x - padL) / Math.max(1, W - padL - padR) * (t1 - t0);
      g.font = '10px sans-serif';
      const pv = fmtGp(hv), tv = dtf(Math.round(Math.max(t0, Math.min(t1, ht))));
      const pw = g.measureText(pv).width;
      g.fillStyle = gepCss('--bg', '#14151c');
      g.fillRect(W - padR - pw - 6, hy - 12, pw + 6, 13);
      g.fillStyle = gepCss('--text-dim', '#c6c6d2');
      g.fillText(pv, W - padR - pw - 3, hy - 2);
      const tw = g.measureText(tv).width;
      let tvx = Math.max(padL, Math.min(W - padR - tw, hx - tw / 2));
      g.fillStyle = gepCss('--bg', '#14151c');
      g.fillRect(tvx - 3, H - padB + 2, tw + 6, 12);
      g.fillStyle = gepCss('--text-dim', '#c6c6d2');
      g.fillText(tv, tvx, H - padB + 11);
    }
    // ...plus the dashed marker + data box on the sample nearest the cursor.
    if (hover != null) {
      const hoverX = hover.x;
      let best = null, bd = Infinity;
      for (const p of pts) { const d = Math.abs(X(p.timestamp) - hoverX); if (d < bd) { bd = d; best = p; } }
      if (best) {
        const bx = X(best.timestamp);
        g.strokeStyle = gepCss('--accent-hi', '#9d83ff'); g.lineWidth = 1;
        g.setLineDash([3, 3]);
        g.beginPath(); g.moveTo(bx, padT); g.lineTo(bx, H - padB); g.stroke();
        g.setLineDash([]);
        for (const [v, col] of [[best.avgHighPrice, okCol], [best.avgLowPrice, warnCol]]) {
          if (v == null) continue;
          g.fillStyle = col; g.beginPath(); g.arc(bx, Y(v), 3, 0, 6.2832); g.fill();
        }
        const rows = [[dtf(best.timestamp), mute]];
        // fmtGp, not toLocaleString: it honours the Numbers preference (compact 85.67b
        // vs full 85,670,000,000), and these are bucket AVERAGES, so the raw value
        // carries fractional gp that has no meaning on screen -- round it first.
        if (best.avgHighPrice != null) rows.push(['Instabuy ' + fmtGp(Math.round(best.avgHighPrice)) + ' gp' + (best.highPriceVolume ? ' x' + fmtGp(best.highPriceVolume) : ''), okCol]);
        if (best.avgLowPrice != null) rows.push(['Instasell ' + fmtGp(Math.round(best.avgLowPrice)) + ' gp' + (best.lowPriceVolume ? ' x' + fmtGp(best.lowPriceVolume) : ''), warnCol]);
        g.font = '10.5px sans-serif';
        let bw = 0;
        for (const [r] of rows) bw = Math.max(bw, g.measureText(r).width);
        bw += 16;
        const bh = rows.length * 14 + 8;
        let boxX = bx + 10; if (boxX + bw > W - 4) boxX = bx - bw - 10;
        boxX = Math.max(4, boxX);
        const boxY = padT + 2;
        g.fillStyle = gepCss('--bg', '#14151c');
        g.strokeStyle = gepCss('--border-hi', 'rgba(140,111,253,0.45)');
        g.beginPath(); g.rect(boxX, boxY, bw, bh); g.fill(); g.stroke();
        rows.forEach(([r, col], i) => { g.fillStyle = col; g.fillText(r, boxX + 8, boxY + 15 + i * 14); });
      }
    }
  }

  function gepDetail(it) {
    const p = (gepLatest && gepLatest[it.id]) || {};
    const box = document.createElement('div');
    box.style.cssText = 'grid-column:1/-1;background:var(--bg, #14151c);border:1px solid var(--border);' +
                        'border-radius:8px;padding:10px 12px;margin:2px 0 4px';
    // Fact strip: exact latest prices, spread and item facts.
    // One row, always: compact fmtGp values with the exact figures in the tooltips.
    const facts = document.createElement('div');
    // Wraps rather than clipping: with compact numbers everything fits on one line, and
    // with full-format prices (three 15-character figures) it needs a second rather
    // than hiding LIMIT/ALCH behind an invisible scroll.
    facts.style.cssText = 'display:flex;gap:4px 12px;flex-wrap:wrap;' +
                          'font-size:11.5px;margin-bottom:8px;color:var(--text-dim,#c6c6d2)';
    const fact = (k, v, title) => {
      const s = document.createElement('span');
      s.style.whiteSpace = 'nowrap';   // a label and its value never split across lines
      s.innerHTML = '<span style="color:var(--text-mute);font-size:10px;text-transform:uppercase;letter-spacing:0.05em">' + k + '</span> ';
      s.appendChild(document.createTextNode(v));
      if (title) s.title = title;
      facts.appendChild(s);
    };
    const nx = v => Number(v).toLocaleString() + ' gp';
    fact('Buy', p.high != null ? fmtGp(p.high) : '-',
         p.high != null ? nx(p.high) + (p.highTime ? ' · traded ' + gepAge(p.highTime) + ' ago' : '') : '');
    fact('Sell', p.low != null ? fmtGp(p.low) : '-',
         p.low != null ? nx(p.low) + (p.lowTime ? ' · traded ' + gepAge(p.lowTime) + ' ago' : '') : '');
    if (p.high != null && p.low != null && p.low > 0)
      fact('Spread', fmtGp(p.high - p.low) + ' (' + ((p.high - p.low) / p.low * 100).toFixed(1) + '%)', nx(p.high - p.low));
    if (it.limit) fact('Limit', fmtGp(it.limit), Number(it.limit).toLocaleString());
    if (it.highalch) fact('Alch', fmtGp(it.highalch), nx(it.highalch));
    box.appendChild(facts);
    // Lookback chips.
    const chips = document.createElement('div');
    chips.style.cssText = 'display:flex;gap:5px;margin-bottom:7px';
    for (const lb of ['24h', '7d', '30d', '1y']) {
      const b = document.createElement('button');
      b.className = 'tiny';
      b.textContent = lb;
      b.style.cssText = 'font-size:10.5px;padding:2px 9px;border-radius:999px;border:1px solid var(--border);' +
                        'background:' + (gepLb === lb ? 'var(--accent-soft, rgba(140,111,253,0.18))' : 'transparent') +
                        ';color:' + (gepLb === lb ? 'var(--accent-hi, #9d83ff)' : 'var(--text-dim, #c6c6d2)') + ';cursor:pointer';
      b.addEventListener('click', ev => { ev.stopPropagation(); gepLb = lb; gepDrawnSig = ''; gepChartData = null; gepShownOpen = -1; renderGePrices(); });
      chips.appendChild(b);
    }
    box.appendChild(chips);
    const cv = document.createElement('canvas');
    cv.id = 'gepChart';
    cv.style.cssText = 'width:100%;height:170px;display:block';
    // Hover: redraw with a crosshair + data box on the nearest sample. Pointer coords go
    // through uiEvPt: the pane sits under a CSS zoom (font-size preference), where raw
    // clientX arithmetic lands off the cursor by the zoom factor (see rtx-ui.js).
    cv.addEventListener('pointermove', ev => {
      if (!gepChartData) return;
      gepDrawChart(cv, gepChartData, uiEvPt(ev, cv));
    });
    cv.addEventListener('pointerleave', () => { if (gepChartData) gepDrawChart(cv, gepChartData, null); });
    box.appendChild(cv);
    const note = document.createElement('div');
    note.id = 'gepChartNote';
    note.style.cssText = 'font-size:10px;color:var(--text-mute);margin-top:4px';
    note.textContent = 'loading history...';
    box.appendChild(note);
    return box;
  }

  // Called every tick while a row is expanded: asks the launcher cache (which kicks the
  // fetch on a miss) and draws once the series lands or changes.
  function gepTickChart() {
    if (!gepOpenId) return;
    const cv = $('gepChart');
    if (!cv) return;
    let series = null, unavailable = false;
    try {
      if (!bridge().pricesTimeseries) unavailable = true;   // pre-update launcher
      else {
        const d = JSON.parse(bridge().pricesTimeseries(gepOpenId, gepLb) || '{}');
        if (d && Array.isArray(d.data)) series = d.data;
        else if (d && d.unavailable) unavailable = true;
      }
    } catch (e) {}
    if (unavailable) {
      const note0 = $('gepChartNote');
      if (note0) note0.textContent = 'History unavailable: this needs the updated launcher and price server (retrying).';
      return;
    }
    if (!series) return;   // still fetching; note keeps saying loading
    const sig = gepOpenId + ':' + gepLb + ':' + series.length + ':' + (series.length ? series[series.length - 1].timestamp : 0);
    // A list rebuild (fresh latest data every ~30s) recreates the detail card with a
    // blank canvas and the placeholder note, while the SERIES sig is unchanged -- so
    // "unchanged" alone must not skip the draw. The canvas carries its own painted flag.
    // Resizing the window changes the canvas box without changing the data, so the
    // drawn size is part of the "needs redraw" test or the chart sits stretched.
    const sized = cv._drawnW === cv.clientWidth && cv._drawnH === cv.clientHeight;
    if (sig === gepDrawnSig && cv.dataset.drawn === sig && sized) return;
    gepDrawnSig = sig;
    cv.dataset.drawn = sig;
    gepChartData = series;
    gepDrawChart(cv, series, null);
    const note = $('gepChartNote');
    if (note) note.textContent = series.length + (series.length === 1 ? ' sample · ' : ' samples · ') + gepLb + ' · hover for exact prices · refreshed every 5 min';
  }

  function renderGePrices() {
    const c = $('content'); if (!c) return;
    let wrap = $('gepWrap');
    if (!wrap) {
      c.innerHTML = '';
      wrap = document.createElement('div'); wrap.id = 'gepWrap'; wrap.className = 'pane';
      const bar = document.createElement('div');
      bar.style.cssText = 'display:flex;gap:8px;align-items:center;margin-bottom:10px';
      const inp = document.createElement('input');
      inp.id = 'gepQ'; inp.className = 'bank-search'; inp.placeholder = 'Search items...';
      inp.autocomplete = 'off'; inp.spellcheck = false; inp.style.flex = '1';
      inp.addEventListener('input', () => { gepShownQuery = null; renderGePrices(); });
      const st = document.createElement('span'); st.id = 'gepStatus';
      st.style.cssText = 'font-size:11px;color:var(--text-mute);white-space:nowrap';
      bar.appendChild(inp); bar.appendChild(st);
      const list = document.createElement('div'); list.id = 'gepList';
      list.style.cssText = 'overflow-x:auto';   // columns wider than the panel scroll together
      // Sort + Favorites bar. Clicking an active sort flips highest/lowest.
      const sbar = document.createElement('div');
      sbar.id = 'gepSort';
      sbar.style.cssText = 'display:flex;gap:5px;align-items:center;margin-bottom:8px;flex-wrap:wrap';
      const mkSort = (key, label) => {
        const b = document.createElement('button');
        b.dataset.key = key;
        b.textContent = label;
        b.style.cssText = 'font-size:10.5px;padding:2px 9px;border-radius:999px;border:1px solid var(--border);' +
                          'background:transparent;color:var(--text-dim,#c6c6d2);cursor:pointer;font:inherit;font-size:10.5px';
        b.addEventListener('click', () => {
          // Cycle: off -> highest first -> lowest first -> off again.
          if (key === 'favs') { gepFavView = !gepFavView; }
          else if (gepSortKey === key) {
            if (gepSortDir < 0) gepSortDir = 1;
            else { gepSortKey = ''; gepSortDir = -1; }
          }
          else { gepSortKey = key; gepSortDir = -1; }
          gepShownGen = -1;
          renderGePrices();
        });
        sbar.appendChild(b);
        return b;
      };
      mkSort('favs', '★ Favorites');
      mkSort('buy', 'Instabuy');
      mkSort('sell', 'Instasell');
      mkSort('spread', 'Spread');
      mkSort('spreadpct', 'Spread %');
      wrap.appendChild(bar); wrap.appendChild(sbar); wrap.appendChild(list);
      c.appendChild(wrap);
      gepShownGen = -1; gepShownQuery = null; gepShownOpen = -1;
    }
    gepPull();
    const st = $('gepStatus');
    if (!gepMap || !gepLatest) { st.textContent = 'loading prices...'; return; }
    const q = ($('gepQ').value || '').trim().toLowerCase();
    // numFmt rides the signature: switching Numbers compact/full changes both the
    // values and the column widths, so the list has to rebuild rather than sit stale.
    const ssig = gepSortKey + ':' + gepSortDir + ':' + (gepFavView ? 1 : 0) + ':' + gepPinRev +
                 ':' + (uiCfg().numFmt || 'compact');
    if (gepShownGen === gepGen && gepShownQuery === q && gepShownOpen === gepOpenId && gepShownSort === ssig) {
      for (const el of $('gepList').querySelectorAll('[data-ht]'))
        el.textContent = gepAge(+el.dataset.ht) + ' ago';
      gepTickChart();
      return;
    }
    gepShownGen = gepGen; gepShownQuery = q; gepShownOpen = gepOpenId; gepShownSort = ssig;
    // Reflect the sort/favorites chips.
    const sb = $('gepSort');
    if (sb) for (const b of sb.children) {
      const key = b.dataset.key;
      const on = key === 'favs' ? gepFavView : gepSortKey === key;
      b.style.background = on ? 'var(--accent-soft, rgba(140,111,253,0.18))' : 'transparent';
      b.style.color = on ? 'var(--accent-hi, #9d83ff)' : 'var(--text-dim, #c6c6d2)';
      if (key !== 'favs') b.textContent = b.textContent.replace(/ [↑↓]$/, '') + (on ? (gepSortDir < 0 ? ' ↓' : ' ↑') : '');
    }
    st.textContent = Object.keys(gepLatest).length + ' items · refreshed every 90s';
    const list = $('gepList'); list.innerHTML = '';
    const bySort = arr => {
      if (!gepSortKey) return arr;
      return arr.slice().sort((a, b) => {
        const ma = gepMetric(a), mb = gepMetric(b);
        if (ma == null && mb == null) return 0;
        if (ma == null) return 1;
        if (mb == null) return -1;
        return gepSortDir < 0 ? mb - ma : ma - mb;
      });
    };
    const favs = gepMap.filter(it => gepPins.has(it.id));
    let rows;
    if (gepFavView) {
      rows = favs.filter(it => !q || it._q.indexOf(q) !== -1);
    } else if (q) {
      rows = [];
      for (const it of gepMap) {
        if (it._q.indexOf(q) === -1) continue;
        rows.push(it);
        if (rows.length >= 400) break;
      }
      // Prefix matches first, then alphabetical (gepMap's own order), unless sorting.
      if (!gepSortKey) rows.sort((a, b) => (b._q.indexOf(q) === 0 ? 1 : 0) - (a._q.indexOf(q) === 0 ? 1 : 0));
      rows = rows.slice(0, 60);
    } else if (gepSortKey) {
      // Sorted browse: rank the whole book by the chosen metric.
      rows = gepMap.filter(it => gepMetric(it) != null);
    } else {
      // Default browse: the most valuable recently traded items rather than nothing.
      rows = gepMap.filter(it => { const p = gepLatest[it.id]; return p && p.high != null && p.high >= 1000000; })
                   .sort((a, b) => (gepLatest[b.id].high || 0) - (gepLatest[a.id].high || 0));
    }
    rows = bySort(rows).slice(0, 60);
    // Favorites lead the default browse view (skipped while searching or already in Favorites view).
    let favHead = [];
    if (!gepFavView && !q && favs.length) {
      favHead = bySort(favs);
      const favIds = new Set(favHead.map(it => it.id));
      rows = rows.filter(it => !favIds.has(it.id));
    }
    if (!rows.length && !favHead.length) {
      list.innerHTML = '<div class="empty">' + (gepFavView ? 'No favorites yet. Star an item to keep it here.' : 'No items match.') + '</div>';
      return;
    }
    // When sorting by spread, the sorted-by number must be visible on the row itself.
    const showSpread = gepSortKey === 'spread' || gepSortKey === 'spreadpct';
    // Column width follows the Numbers preference: "200,000,000,000" needs roughly
    // twice the room of "200.00b", and at the compact width it wrapped onto a second
    // line. The name column (1fr) gives up the space and ellipsises instead.
    const full = uiCfg().numFmt === 'full';
    const numW = full ? 116 : 62;
    const spreadW = full ? 116 : 74;
    // The name column keeps a readable floor instead of collapsing to nothing, and the
    // whole list scrolls sideways when the columns cannot fit the panel (a game panel
    // is often only ~400px wide, and full-format prices alone want 232px of that).
    const NAME_MIN = 96;
    const gridCols = '26px minmax(' + NAME_MIN + 'px, 1fr) ' + numW + 'px ' + numW + 'px' +
                     (showSpread ? ' ' + spreadW + 'px' : '') + ' 52px 18px';
    const cols = 6 + (showSpread ? 1 : 0);
    const rowMinW = 26 + NAME_MIN + numW * 2 + (showSpread ? spreadW : 0) + 52 + 18 + (cols * 8);
    // Column header, in the same grid as the rows so the labels sit over their columns.
    {
      const hd = document.createElement('div');
      hd.style.cssText = 'display:grid;grid-template-columns:' + gridCols + ';gap:8px;min-width:' + rowMinW + 'px;' +
                         'padding:2px 8px 4px;border-bottom:1px solid var(--border);' +
                         'font-size:9.5px;color:var(--text-mute);text-transform:uppercase;letter-spacing:0.07em';
      const cells = ['', 'Item', 'Instabuy', 'Instasell'];
      if (showSpread) cells.push(gepSortKey === 'spreadpct' ? 'Spread %' : 'Spread');
      cells.push('Traded', '');
      for (let i = 0; i < cells.length; i++) {
        const d = document.createElement('div');
        d.textContent = cells[i];
        if (i >= 2 && cells[i]) d.style.textAlign = 'right';
        hd.appendChild(d);
      }
      list.appendChild(hd);
    }
    const buildRow = (it) => {
      const p = gepLatest[it.id] || {};
      const open = gepOpenId === it.id;
      const row = document.createElement('div');
      row.style.cssText = 'display:grid;grid-template-columns:' + gridCols + ';gap:8px;align-items:center;min-width:' + rowMinW + 'px;' +
                          'padding:5px 8px;border-bottom:1px solid var(--border);cursor:pointer' +
                          (open ? ';background:var(--accent-soft, rgba(140,111,253,0.08))' : '');
      row.title = open ? 'Click to collapse' : 'Click for price history';
      row.addEventListener('click', () => {
        gepOpenId = open ? 0 : it.id;
        gepDrawnSig = ''; gepChartData = null; gepShownOpen = -1;
        renderGePrices();
      });
      // .ge-icon + data-item-id is attachIcon's contract: an icon resolved AFTER this
      // row was built finds its element through that selector. Without it, late icons
      // never land (the launch-week bug: half the list showed stale or empty art).
      const ic = document.createElement('div');
      ic.className = 'ge-icon';
      ic.dataset.itemId = String(it.id);
      ic.style.cssText = 'width:24px;height:24px;flex:none;background-size:contain;background-repeat:no-repeat;background-position:center';
      try { if (typeof attachIcon === 'function') attachIcon(ic, it.id); } catch (e) {}
      const nm = document.createElement('div');
      nm.textContent = it.name;
      nm.style.cssText = 'overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:12.5px';
      const hi = document.createElement('div');
      hi.textContent = p.high != null ? fmtGp(p.high) : '-';
      hi.title = 'Latest buy (high) price';
      hi.style.cssText = 'color:var(--ok, #4dd28a);font-variant-numeric:tabular-nums;font-size:12.5px;text-align:right;white-space:nowrap';
      const lo = document.createElement('div');
      lo.textContent = p.low != null ? fmtGp(p.low) : '-';
      lo.title = 'Latest sell (low) price';
      lo.style.cssText = 'color:var(--warn, #f5b241);font-variant-numeric:tabular-nums;font-size:12.5px;text-align:right;white-space:nowrap';
      const ag = document.createElement('div');
      const ht = p.highTime || p.lowTime || 0;
      ag.dataset.ht = String(ht);
      ag.textContent = ht ? gepAge(ht) + ' ago' : '';
      ag.style.cssText = 'color:var(--text-mute);font-size:10.5px;text-align:right;white-space:nowrap';
      // Favorite star. A button, not part of the row click.
      const fav = gepPins.has(it.id);
      const star = document.createElement('button');
      star.textContent = fav ? '★' : '☆';
      star.title = fav ? 'Remove from Favorites' : 'Add to Favorites';
      star.style.cssText = 'background:none;border:none;cursor:pointer;font-size:13px;padding:0;line-height:1;' +
                           'color:' + (fav ? 'var(--accent-hi, #9d83ff)' : 'var(--text-mute)');
      star.addEventListener('click', ev => {
        ev.stopPropagation();
        if (gepPins.has(it.id)) gepPins.delete(it.id); else gepPins.add(it.id);
        gepSavePins(); gepPinRev++;
        renderGePrices();
      });
      row.appendChild(ic); row.appendChild(nm); row.appendChild(hi); row.appendChild(lo);
      if (showSpread) {
        const sp = document.createElement('div');
        if (p.high != null && p.low != null && p.low > 0) {
          const d = p.high - p.low, pct = d / p.low * 100;
          // Junk-item spreads (a 6m item that instasells for 2 gp) reach millions of
          // percent; compact those the way gp values are compacted.
          const pctTxt = (pct >= 1000 ? fmtGp(Math.round(pct)) : pct.toFixed(1)) + '%';
          sp.textContent = gepSortKey === 'spreadpct' ? pctTxt : fmtGp(d);
          sp.title = 'Spread ' + Number(d).toLocaleString() + ' gp (' + Math.round(pct).toLocaleString() + '%)';
        } else sp.textContent = '-';
        sp.style.cssText = 'color:var(--accent-hi, #9d83ff);font-variant-numeric:tabular-nums;font-size:12.5px;text-align:right;white-space:nowrap';
        row.appendChild(sp);
      }
      row.appendChild(ag); row.appendChild(star);
      list.appendChild(row);
      if (open) list.appendChild(gepDetail(it));
    };
    const section = (label) => {
      const h = document.createElement('div');
      h.textContent = label;
      h.style.cssText = 'font-size:10px;color:var(--text-mute);text-transform:uppercase;letter-spacing:0.08em;' +
                        'padding:7px 8px 3px';
      list.appendChild(h);
    };
    if (favHead.length) {
      section('★ Favorites');
      for (const it of favHead) buildRow(it);
      if (rows.length) section('All items');
    }
    for (const it of rows) buildRow(it);
    gepTickChart();
  }

  registerTab({
    id: 'geprices', label: 'GE Prices', cat: 'Items',
    icon: '<path d="M4 19V5"/><path d="M4 19h16"/><path d="M7 14l3.5-4 3 2.5L18 7"/><circle cx="18" cy="7" r="1.4"/>',
    render: renderGePrices,
    open: function () { try { bridge().pricesCached(); bridge().pricesMapping(); } catch (e) {} },   // kick the fetches early
    close: function () { gepOpenId = 0; gepDrawnSig = ''; gepChartData = null; },
  });
})();
