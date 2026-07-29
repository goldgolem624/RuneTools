// RuneToolsX panel: Cache Explorer (Developer).
// Spliced inline into client.html; bare classic script sharing one global scope.
// One sub-tab per cache family. Varbits/varps/varcs have bulk bridge reads and load in one
// call; enums, structs, dbtables and items are per-id lookups, so those are SCANNED over an
// id range (chunked and cancellable) and the results cached in memory for the session.
// Built once and updated in place: renderCachex() runs on the 250 ms poll, and rebuilding
// would drop focus from the search box mid-keystroke.

  const CX_TYPES = [
    { k: 'enum',    label: 'Enums',    fn: 'enumInfo',     lo: 0, hi: 20000 },
    { k: 'struct',  label: 'Structs',  fn: 'structParams', lo: 0, hi: 60000 },
    { k: 'dbtable', label: 'DBTables', fn: 'dbRows',       lo: 0, hi: 600 },
    { k: 'item',    label: 'Items',    fn: 'itemInfo',     lo: 0, hi: 63500 },
    { k: 'varbit',  label: 'Varbits',  bulk: 'varbitMap' },
    { k: 'varp',    label: 'Varps',    bulk: 'varpsDumpAll' },
    { k: 'varc',    label: 'Varcs',    bulk: 'varcsDumpAll' }
  ];
  const CX_MAX_ROWS = 800;          // rendering every hit would stall the view; note the rest
  const CX_CHUNK = 200;             // ids per yield, so a long scan stays responsive
  let cxTab = 'enum', cxFilter = '', cxOpen = null, cxSeq = 0;
  const cxState = {};               // per type: { rows:[{id,sum,data}], busy, done, total, err, loaded }
  // Persisted results. The game cache only changes on a game UPDATE, so a snapshot stays
  // valid for as long as the client build does: the build string is the cache key, and a
  // re-scan of an already-covered range costs nothing. Widening a range only scans the part
  // that is missing. Live player state (varps/varcs) is deliberately NOT cached.
  // Snapshots live in real files (%USERPROFILE%/RuneToolsX/cachex/<family>.json) via the
  // bridge, the same way the CS2 extraction writes its output: a full struct scan is many MB,
  // far past what the renderer can hold. The game cache only changes on a game UPDATE, so the
  // client build is the cache key and a stored scan stays valid until the game moves.
  // Live player state (varps/varcs) is deliberately never stored.
  // Several varcs carry a world tile packed as (plane<<28)|(x<<14)|y -- the compass-clue
  // target (1323) among them. Decoding any value that lands in a sane tile range makes those
  // obvious at a glance instead of reading as an arbitrary integer.
  function cxCoord(v) {
    if (!(v > 16384)) return '';
    const x = (v >> 14) & 0x3FFF, y = v & 0x3FFF, z = (v >> 28) & 0x3;
    if (x < 1000 || x > 16000 || y < 1000 || y > 16000) return '';
    return '   -> tile ' + x + ', ' + y + (z ? ' plane ' + z : '');
  }
  function cxWhen(ms) {
    try {
      const d = new Date(ms);
      const p = n => (n < 10 ? '0' : '') + n;
      return d.getDate() + ' ' + ['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'][d.getMonth()]
             + ' ' + p(d.getHours()) + ':' + p(d.getMinutes());
    } catch (e) { return ''; }
  }
  const cxBuild = () => (typeof lastSnap !== 'undefined' && lastSnap && lastSnap.client_version) || '';
  const cxCacheable = k => k !== 'varp' && k !== 'varc';
  async function cxSave(k) {
    if (!cxCacheable(k) || !bridge() || !bridge().cacheStoreSave) return;
    const st = cxSt(k);
    try {
      const slim = st.rows.map(r => ({ id: r.id, sum: r.sum, data: r.data, txt: r.txt }));
      const blob = JSON.stringify({ v: cxBuild(), lo: st.lo0, hi: st.hi0, at: st.at || Date.now(), rows: slim });
      const ok = await bridge().cacheStoreSave('cx' + k, blob);
      st.cached = ok ? 'saved' : 'save failed';
    } catch (e) { st.cached = 'save failed'; }
    cxPaint();
  }
  async function cxLoad(k) {
    if (!cxCacheable(k) || !bridge() || !bridge().cacheStoreLoad) return null;
    try {
      const raw = await bridge().cacheStoreLoad('cx' + k);
      if (!raw) return null;
      const o = JSON.parse(raw);
      if (!o || !Array.isArray(o.rows)) return null;
      if (o.v !== cxBuild()) return null;              // a game update invalidates the scan
      return o;
    } catch (e) { return null; }
  }
  async function cxClear(k) {
    const st = cxSt(k);
    st.rows = []; st.loaded = false; st.cached = ''; st.reused = false; st.at = 0;
    st.lo0 = undefined; st.hi0 = undefined;
    try { if (bridge() && bridge().cacheStoreSave) await bridge().cacheStoreSave('cx' + k, ''); } catch (e) {}
  }

  // Live var dumps key by DOMAIN:id ("4:1234" varp, "5:1234" varc); string varcs are bare.
  const cxKeyId = k => { const p = String(k).split(':'); return parseInt(p[p.length - 1], 10); };
  const cxT = k => CX_TYPES.filter(t => t.k === k)[0];
  const cxSt = k => (cxState[k] = cxState[k] || { rows: [], busy: false, done: 0, total: 0, loaded: false });
  // The from/to boxes belong to the ACTIVE family: stash them on switch and restore, so a
  // struct scan range is not carried over onto dbtables.
  function cxSaveRange() {
    const lo = $('cxLo'), hi = $('cxHi'), st = cxSt(cxTab);
    if (lo) st.lo = lo.value;
    if (hi) st.hi = hi.value;
  }
  function cxLoadRange() {
    const lo = $('cxLo'), hi = $('cxHi'), t = cxT(cxTab), st = cxSt(cxTab);
    if (lo) lo.value = (st.lo !== undefined) ? st.lo : (t.lo !== undefined ? t.lo : '');
    if (hi) hi.value = (st.hi !== undefined) ? st.hi : (t.hi !== undefined ? t.hi : '');
  }

  // One-line summary per entry: what you scan the list for before opening anything.
  function cxSummarise(k, id, d) {
    try {
      if (k === 'enum') {
        const ks = Object.keys(d);
        const first = ks.slice(0, 3).map(x => x + '=' + d[x]).join(', ');
        return ks.length + ' entries  ' + first;
      }
      if (k === 'struct' || k === 'item') {
        const I = d.ints || {}, S = d.strs || {};
        if (k === 'item' && d.name) return d.name;
        const s0 = Object.keys(S).map(x => S[x]).filter(Boolean)[0] || '';
        return Object.keys(I).length + ' ints, ' + Object.keys(S).length + ' strs  ' + s0;
      }
      if (k === 'dbtable') {
        const r0 = d[0] || {};
        const s = r0.s || {};
        const first = Object.keys(s).map(x => s[x] && s[x][0]).filter(Boolean)[0] || '';
        return d.length + ' rows  ' + first;
      }
    } catch (e) {}
    return '';
  }
  // Readable detail. Cache strings carry literal <br> markup and rows hold long parallel
  // arrays, so values are unwrapped one per line and every column is labelled: cramming a
  // whole row onto one line is what made this unreadable.
  const CX_STR_WRAP = 96;
  function cxText(v) {
    return String(v).replace(/<br\s*\/?>/gi, '\n').replace(/<[^>]+>/g, '').trim();
  }
  function cxLines(v, ind) {
    const t = cxText(v);
    if (!t) return ind + '""';
    return t.split('\n').map(l => ind + l).join('\n');
  }
  // One value inline; several become a numbered list so parallel columns stay alignable.
  function cxColumn(label, vals, out) {
    const arr = Array.isArray(vals) ? vals : [vals];
    if (!arr.length) { out.push('  ' + label + ': []'); return; }
    if (arr.length === 1) {
      const v = arr[0];
      if (typeof v !== 'string') { out.push('  ' + label + ': ' + v); return; }
      const t = cxText(v);
      if (t.length <= CX_STR_WRAP && t.indexOf('\n') < 0) out.push('  ' + label + ': ' + t);
      else { out.push('  ' + label + ':'); out.push(cxLines(v, '      ')); }
      return;
    }
    if (arr.every(x => typeof x !== 'string') && arr.length <= 16) {
      out.push('  ' + label + ': [' + arr.join(', ') + ']'); return;
    }
    out.push('  ' + label + ':  (' + arr.length + ')');
    arr.forEach((x, i) => {
      const head = '    [' + i + '] ';
      if (typeof x !== 'string') { out.push(head + x); return; }
      const t = cxText(x);
      if (t.length <= CX_STR_WRAP && t.indexOf('\n') < 0) out.push(head + t);
      else { out.push('    [' + i + ']'); out.push(cxLines(x, '        ')); }
    });
  }
  // What the filter searches, and what a match snippet is cut from. Deliberately NOT raw
  // JSON: slicing that produced snippets full of ","ge_limit":-1,"value": noise. This is the
  // same labelled shape the detail view shows, flattened onto one line.
  function cxIndexText(k, d) {
    const out = [];
    const add = v => {
      if (v == null) return;
      const arr = Array.isArray(v) ? v : [v];
      for (const x of arr) out.push(typeof x === 'string' ? cxText(x).replace(/\s+/g, ' ') : String(x));
    };
    try {
      if (k === 'enum') {
        for (const key of Object.keys(d)) { out.push(key + '='); add(d[key]); }
      } else if (k === 'struct' || k === 'item') {
        if (d.name) out.push(d.name);
        const I = d.ints || {}, S = d.strs || {};
        for (const key of Object.keys(S)) { out.push('param ' + key); add(S[key]); }
        for (const key of Object.keys(I)) { out.push('param ' + key); add(I[key]); }
      } else if (k === 'dbtable') {
        for (const r of d) {
          out.push('row ' + r.f);
          const I = r.i || {}, S = r.s || {};
          for (const key of Object.keys(S)) { out.push('col ' + key); add(S[key]); }
          for (const key of Object.keys(I)) { out.push('col ' + key); add(I[key]); }
        }
      } else add(d);
    } catch (e) {}
    return out.join(' ');
  }
  function cxDetail(k, d) {
    const out = [];
    const sortNum = (a, b) => (isFinite(+a) && isFinite(+b)) ? (+a) - (+b) : (a < b ? -1 : 1);
    if (k === 'enum') {
      for (const key of Object.keys(d).sort(sortNum)) cxColumn(String(key), d[key], out);
    } else if (k === 'struct' || k === 'item') {
      const I = d.ints || {}, S = d.strs || {};
      if (d.name) out.push('  name: ' + d.name);
      for (const key of Object.keys(I).sort(sortNum)) cxColumn('param ' + key, I[key], out);
      for (const key of Object.keys(S).sort(sortNum)) cxColumn('param ' + key, S[key], out);
      if (!out.length) out.push('  ' + JSON.stringify(d));
    } else if (k === 'dbtable') {
      for (const r of d) {
        out.push('row ' + r.f);
        const I = r.i || {}, S = r.s || {};
        const cols = {};
        for (const cc of Object.keys(I)) cols[cc] = I[cc];
        for (const cc of Object.keys(S)) cols[cc] = S[cc];
        for (const cc of Object.keys(cols).sort(sortNum)) cxColumn('col ' + cc, cols[cc], out);
        out.push('');
      }
    } else {
      out.push('  ' + JSON.stringify(d));
    }
    return out.join('\n');
  }

  // Bulk families: one bridge call each.
  async function cxLoadBulk(k) {
    const st = cxSt(k);
    if (st.busy || st.loaded) return;
    if (k === 'varbit') {
      const saved = await cxLoad(k);
      if (saved) { st.rows = saved.rows; st.at = saved.at; st.loaded = true; st.cached = 'saved'; cxPaint(); return; }
    }
    st.busy = true; cxPaint();
    const rows = [];
    try {
      if (k === 'varbit') {
        const m = JSON.parse(await bridge().varbitMap() || 'null') || {};
        for (const vp of Object.keys(m)) {
          for (const d of m[vp]) rows.push({ id: d[0], sum: 'varp ' + vp + ' bits ' + d[1] + '-' + d[2], data: null });
        }
      } else if (k === 'varp') {
        const m = JSON.parse(await bridge().varpsDumpAll(myPid()) || 'null') || {};
        for (const key of Object.keys(m)) {
          const v = m[key] | 0;
          rows.push({ id: cxKeyId(key), sum: v + '   0x' + (v >>> 0).toString(16), data: null });
        }
      } else if (k === 'varc') {
        let ints = {}, strs = {};
        try { ints = JSON.parse(await bridge().varcsDumpAll(myPid()) || 'null') || {}; } catch (e) {}
        if (bridge().varcStringsDumpAll) { try { strs = JSON.parse(await bridge().varcStringsDumpAll(myPid()) || 'null') || {}; } catch (e) {} }
        for (const key of Object.keys(ints)) {
          const v = ints[key] | 0;
          rows.push({ id: cxKeyId(key), sum: v + cxCoord(v), data: null });
        }
        for (const key of Object.keys(strs)) rows.push({ id: cxKeyId(key), sum: '"' + strs[key] + '"  (string)', data: null });
      }
    } catch (e) { st.err = String(e); }
    rows.sort((a, b) => a.id - b.id);
    st.rows = rows; st.loaded = true; st.busy = false;
    if (k === 'varbit') { st.lo0 = 0; st.hi0 = 0; cxSave(k); }   // cache-derived, so worth keeping
    cxPaint();
  }

  // Scanned families: one bridge call per id, chunked so the UI keeps painting. Anything the
  // stored snapshot already covers is reused, so only genuinely new ids cost a round trip.
  async function cxScan(k) {
    const t = cxT(k), st = cxSt(k);
    if (!t || !t.fn || st.busy || !bridge() || !bridge()[t.fn]) return;
    const lo = Math.max(0, parseInt(($('cxLo') || {}).value, 10) || t.lo);
    const hi = Math.max(lo, parseInt(($('cxHi') || {}).value, 10) || t.hi);

    // Adopt a stored snapshot for this build before deciding what still needs scanning.
    if (!st.loaded) {
      const saved = await cxLoad(k);
      if (saved) {
        st.rows = saved.rows; st.lo0 = saved.lo; st.hi0 = saved.hi; st.at = saved.at;
        st.loaded = true; st.cached = 'saved';
      }
    }
    // Coverage is only ever widened over ids we have ACTUALLY read. Claiming the whole
    // requested range up front meant a cancelled scan looked complete, so every later press
    // found nothing to do and the button appeared dead.
    const gaps = [];
    const have = st.loaded && st.lo0 !== undefined && st.hi0 !== undefined;
    if (have) {
      if (lo < st.lo0) gaps.push([lo, Math.min(hi, st.lo0 - 1), 'low']);
      if (hi > st.hi0) gaps.push([Math.max(lo, st.hi0 + 1), hi, 'high']);
    } else {
      gaps.push([lo, hi, 'init']);
    }
    const total = gaps.reduce((a, g) => a + (g[1] - g[0] + 1), 0);
    if (!total) { st.loaded = true; st.reused = true; cxPaint(); return; }   // already covered

    const seq = ++cxSeq;
    st.busy = true; st.done = 0; st.total = total; st.err = ''; st.loaded = true; st.reused = false;
    cxPaint();
    let stopped = false;
    for (const g of gaps) {
      if (stopped) break;
      if (g[2] === 'init') { st.rows = []; st.lo0 = g[0]; st.hi0 = g[0] - 1; }
      for (let id = g[0]; id <= g[1]; id++) {
        if (seq !== cxSeq) { stopped = true; break; }               // cancelled or retargeted
        let d = null;
        try { d = JSON.parse(await bridge()[t.fn](id) || 'null'); } catch (e) { d = null; }
        const empty = !d || (Array.isArray(d) ? !d.length : !Object.keys(d).length);
        // Index the whole entry, not just the summary: the point of the filter is finding a
        // value buried in a column, which the one-line summary never shows.
        if (!empty) st.rows.push({ id: id, sum: cxSummarise(k, id, d), data: d, txt: cxIndexText(k, d) });
        if (g[2] !== 'low') st.hi0 = id;                            // grew the top end
        st.done++;
        if (st.done % CX_CHUNK === 0) { cxPaint(); await new Promise(r => setTimeout(r, 0)); }
      }
      if (!stopped && g[2] === 'low') st.lo0 = g[0];                // the low gap is contiguous
    }
    st.rows.sort((a, b) => a.id - b.id);
    st.busy = false;
    st.at = Date.now();
    cxPaint();
    cxSave(k);          // partial work is worth keeping: coverage records how far it got
  }

  function cxRows(k) {
    const st = cxSt(k);
    const f = cxFilter.trim().toLowerCase();
    if (!f) return st.rows;
    return st.rows.filter(r => {
      if (String(r.id) === f || String(r.id).indexOf(f) === 0) return true;
      if (r._l === undefined) r._l = ((r.sum || '') + ' ' + (r.txt || '')).toLowerCase();
      return r._l.indexOf(f) >= 0;
    });
  }

  // Repaint only the parts that change; the chrome and the search box are never rebuilt.
  function cxPaint() {
    const wrap = $('cxWrap');
    if (!wrap || activeTab !== 'cachex') return;
    const t = cxT(cxTab), st = cxSt(cxTab);
    wrap.querySelectorAll('.cx-chip').forEach(b => b.classList.toggle('on', b.dataset.k === cxTab));
    const rangeRow = $('cxRange');
    if (rangeRow) rangeRow.style.display = t.bulk ? 'none' : '';
    const scanBtn = $('cxScanBtn');
    if (scanBtn) scanBtn.textContent = st.busy ? 'Cancel' : (t.bulk ? 'Load' : 'Scan');
    const stat = $('cxStat');
    if (stat) {
      const shown = cxRows(cxTab).length;
      const when = st.at ? '  ·  scanned ' + cxWhen(st.at) : '';
      const range = (!t.bulk && st.lo0 !== undefined) ? '  ·  ids ' + st.lo0 + '-' + st.hi0 : '';
      stat.textContent = st.busy
        ? (t.bulk ? 'loading...' : 'scanning ' + st.done + ' / ' + st.total + '  ·  ' + st.rows.length + ' found')
        : (!st.loaded ? (t.bulk ? 'press Load' : 'press Scan')
                      : shown + (shown === st.rows.length ? '' : ' of ' + st.rows.length) + ' entries'
                        + range + when + (st.reused ? '  ·  already covered' : ''));
    }
    const list = $('cxList');
    if (!list) return;
    const rows = cxRows(cxTab);
    const filt = cxFilter.trim().toLowerCase();
    const sig = cxTab + '|' + st.rows.length + '|' + cxFilter + '|' + cxOpen + '|' + st.busy;
    if (list._sig === sig) return;
    list._sig = sig;
    list.innerHTML = '';
    if (!rows.length) {
      const e = document.createElement('div'); e.className = 'cx-empty';
      e.textContent = st.busy ? 'Working...' : (st.loaded ? 'Nothing matches.' : 'Nothing loaded yet.');
      list.appendChild(e); return;
    }
    for (const r of rows.slice(0, CX_MAX_ROWS)) {
      const row = document.createElement('div'); row.className = 'cx-row';
      const idEl = document.createElement('span'); idEl.className = 'cx-id'; idEl.textContent = r.id;
      const sum = document.createElement('span'); sum.className = 'cx-sum';
      // When the hit is buried in a column the summary will not show it, so echo the
      // surrounding text instead of leaving the row looking like a false positive.
      let label = r.sum || '';
      if (filt && r.txt && (r.sum || '').toLowerCase().indexOf(filt) < 0) {
        const at = r.txt.toLowerCase().indexOf(filt);
        if (at >= 0) {
          const a = Math.max(0, at - 32), b = Math.min(r.txt.length, at + filt.length + 52);
          label = (a ? '...' : '') + r.txt.slice(a, b).trim() + (b < r.txt.length ? '...' : '');
        }
      }
      sum.textContent = label;
      row.appendChild(idEl); row.appendChild(sum);
      const key = cxTab + ':' + r.id;
      if (r.data) {
        row.classList.add('cx-click');
        row.addEventListener('click', () => { cxOpen = (cxOpen === key) ? null : key; cxPaint(); });
      }
      list.appendChild(row);
      if (cxOpen === key && r.data) {
        const det = document.createElement('pre'); det.className = 'cx-det';
        det.textContent = cxDetail(cxTab, r.data);
        list.appendChild(det);
      }
    }
    if (rows.length > CX_MAX_ROWS) {
      const more = document.createElement('div'); more.className = 'cx-empty';
      more.textContent = 'Showing the first ' + CX_MAX_ROWS + ' of ' + rows.length + '. Narrow the search to see the rest.';
      list.appendChild(more);
    }
  }

  function renderCachex() {
    const c = $('content');
    if ($('cxWrap')) { cxPaint(); return; }
    injectStyle('cxCss', `
      .cx-wrap { display: flex; flex-direction: column; height: 100%; gap: 8px; padding: 10px 12px 0; }
      .cx-tabs { display: flex; flex-wrap: wrap; gap: 5px; }
      .cx-chip { flex: 0 0 auto; font-size: 11px; font-weight: 600; padding: 4px 11px; border-radius: 12px;
          cursor: pointer; background: var(--bg-elev); border: 1px solid var(--border); color: var(--text-dim); }
      .cx-chip.on { color: var(--text); background: var(--accent-soft, rgba(124,92,252,.16)); border-color: var(--accent); }
      .cx-ctl { display: flex; flex-wrap: wrap; gap: 6px; align-items: center; }
      .cx-in { min-width: 0; background: var(--bg-elev); border: 1px solid var(--border); border-radius: 7px;
          color: var(--text); font-size: 12px; padding: 5px 9px; outline: none; }
      .cx-in:focus { border-color: var(--accent); }
      .cx-num { width: 74px; flex: 0 0 auto; }
      .cx-search { flex: 1 1 160px; }
      .cx-stat { font-size: 11px; color: var(--text-mute); white-space: nowrap; }
      .cx-list { flex: 1; min-height: 0; overflow-y: auto; background: var(--bg-elev);
          border: 1px solid var(--border); border-radius: 10px; margin-bottom: 10px; }
      .cx-row { display: flex; gap: 10px; padding: 5px 11px; font-size: 11.5px; align-items: baseline; }
      .cx-row + .cx-row { border-top: 1px solid var(--border); }
      .cx-click { cursor: pointer; }
      .cx-click:hover { background: rgba(255,255,255,0.04); }
      .cx-id { flex: 0 0 auto; min-width: 58px; color: var(--accent-hi); font-variant-numeric: tabular-nums; font-weight: 600; }
      .cx-sum { flex: 1; min-width: 0; color: var(--text-dim); overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
      .cx-det { margin: 0; padding: 8px 12px 10px 18px; background: rgba(0,0,0,0.28); color: var(--text);
          font-family: Consolas, monospace; font-size: 11px; line-height: 1.5;
          white-space: pre-wrap; overflow-wrap: anywhere; user-select: text; }
      .cx-empty { padding: 12px; color: var(--text-dim); font-size: 12px; }`);
    c.innerHTML = '';
    const wrap = document.createElement('div'); wrap.id = 'cxWrap'; wrap.className = 'cx-wrap'; c.appendChild(wrap);

    const tabs = document.createElement('div'); tabs.className = 'cx-tabs';
    CX_TYPES.forEach(t => {
      const b = document.createElement('button'); b.className = 'cx-chip'; b.dataset.k = t.k; b.textContent = t.label;
      b.addEventListener('click', () => {
        cxSaveRange();                                    // keep the range we are leaving
        cxTab = t.k; cxOpen = null; cxSeq++;              // leaving a tab cancels its scan
        cxLoadRange();
        const l = $('cxList'); if (l) l._sig = '';
        cxPaint();
        const s = cxSt(t.k);
        if (!s.loaded && !s.busy) {
          cxLoad(t.k).then(saved => {
            if (saved) {
              s.rows = saved.rows; s.lo0 = saved.lo; s.hi0 = saved.hi; s.at = saved.at;
              s.loaded = true; s.cached = 'saved';
              const l2 = $('cxList'); if (l2) l2._sig = '';
              cxPaint();
            } else if (t.bulk) cxLoadBulk(t.k);
          });
        }
      });
      tabs.appendChild(b);
    });
    wrap.appendChild(tabs);

    const ctl = document.createElement('div'); ctl.className = 'cx-ctl';
    const rng = document.createElement('span'); rng.id = 'cxRange'; rng.className = 'cx-ctl';
    const lo = document.createElement('input'); lo.id = 'cxLo'; lo.className = 'cx-in cx-num'; lo.placeholder = 'from';
    const hi = document.createElement('input'); hi.id = 'cxHi'; hi.className = 'cx-in cx-num'; hi.placeholder = 'to';
    rng.appendChild(lo); rng.appendChild(hi);
    ctl.appendChild(rng);
    const go = document.createElement('button'); go.id = 'cxScanBtn'; go.className = 'vw-btn'; go.textContent = 'Scan';
    go.addEventListener('click', () => {
      const t2 = cxT(cxTab), s2 = cxSt(cxTab);
      if (s2.busy) { cxSeq++; s2.busy = false; cxPaint(); return; }   // cancel
      if (t2.bulk) { s2.loaded = false; cxLoadBulk(cxTab); } else cxScan(cxTab);
    });
    ctl.appendChild(go);
    const clr = document.createElement('button'); clr.className = 'vw-btn'; clr.textContent = 'Clear';
    clr.dataset.tip = 'Drop the stored snapshot for this family and read it again from the cache';
    clr.addEventListener('click', () => {
      cxSeq++; cxOpen = null;
      cxClear(cxTab).then(() => { const l = $('cxList'); if (l) l._sig = ''; cxPaint(); });
    });
    ctl.appendChild(clr);
    const srch = document.createElement('input'); srch.id = 'cxSearch'; srch.className = 'cx-in cx-search';
    srch.placeholder = 'Filter by id or content';
    srch.addEventListener('input', () => { cxFilter = srch.value; cxOpen = null; cxPaint(); });
    ctl.appendChild(srch);
    const stat = document.createElement('span'); stat.id = 'cxStat'; stat.className = 'cx-stat';
    ctl.appendChild(stat);
    wrap.appendChild(ctl);

    const list = document.createElement('div'); list.id = 'cxList'; list.className = 'cx-list';
    wrap.appendChild(list);

    // seed the range boxes for the starting tab
    const t0 = cxT(cxTab);
    lo.value = t0.lo; hi.value = t0.hi;
    cxPaint();
  }
