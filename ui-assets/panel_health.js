// RuneToolsX panel: Health check (Developer) -- on-demand diagnostic of every major read chain,
// so after a GAME UPDATE one glance shows which anchor/offset chain broke.
// Spliced inline into client.html; IIFE (window exports + registerTab; see the RTX registry in client.html).
(function () {

  let hcData = null, hcBusy = false;
  // Item icon coverage: pack stats + session misses (cheap) and the one-time cache walk
  // (host runs it on a background thread and answers {pending:1} until it lands).
  let icMisses = null, icCov = null, icNames = {}, icTimer = 0;
  // Live capture (host.iconCapture): the launcher reads the client's GPU icon atlas and
  // saves PNGs for the icons on screen, so a pack miss heals itself. Status is polled.
  let icCap = null, icCapTimer = 0;
  let hcKeepScroll = [];   // .hc-list scrollTop values captured before a rebuild
  async function icCapFetch() {
    try { icCap = JSON.parse(await rtxData.raw('host.iconCaptureStatus')); } catch (e) { icCap = null; }
    clearTimeout(icCapTimer);
    if (icCap && icCap.busy) icCapTimer = setTimeout(icCapFetch, 700);
    paneRun('health', renderHealth);
  }
  async function icCapRun() {
    try { icCap = JSON.parse(await rtxData.raw('host.iconCapture')); } catch (e) { icCap = null; }
    clearTimeout(icCapTimer); icCapTimer = setTimeout(icCapFetch, 700);
    paneRun('health', renderHealth);
  }
  async function icFetch() {
    icCapFetch();
    try { icMisses = JSON.parse(await rtxData.raw('host.iconMisses')); } catch (e) { icMisses = null; }
    try { icCov = JSON.parse(await rtxData.raw('host.iconCoverage')); } catch (e) { icCov = null; }
    if (icCov && icCov.pending) { clearTimeout(icTimer); icTimer = setTimeout(icFetch, 1500); }
    {
      // names for the shown rows only; the full missing list can be a few hundred ids
      const want = (icCov && Array.isArray(icCov.missing) ? icCov.missing.slice(0, 40) : [])
        .concat(icMisses && Array.isArray(icMisses.misses) ? icMisses.misses.slice(0, 40).map(x => x[0]) : []);
      for (const id of want) {
        if (id in icNames) continue;
        try { const d = JSON.parse(await rtxData.raw('cache.itemInfo', id) || 'null'); icNames[id] = (d && d.name) || ''; } catch (e) { icNames[id] = ''; }
      }
    }
    paneRun('health', renderHealth);
  }
  async function hcRun() {
    if (hcBusy || !bridge() || !bridge().readerHealth) return;
    hcBusy = true; paneRun('health', renderHealth);
    try { hcData = JSON.parse(await rtxData.raw('host.readerHealth')); } catch (e) { hcData = null; }
    hcBusy = false;
    icFetch();
    paneRun('health', renderHealth);
  }
  function renderHealth() {
    const c = $('content');
    let wrap = $('hcWrap');
    if (!wrap) {
      injectStyle('hcCss', `
          .hc-head { display: flex; align-items: center; gap: 8px; padding: 12px 14px 8px; }
          .hc-title { flex: 1; font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: .05em; color: var(--accent-hi); }
          .hc-ver { padding: 0 14px 8px; font-size: 11px; color: var(--text-dim); }
          .hc-card { margin: 0 12px; background: var(--bg-elev); border: 1px solid var(--border); border-radius: 10px; }
          .hc-row { display: flex; align-items: center; gap: 8px; padding: 7px 12px; font-size: 12px; }
          .hc-row + .hc-row { border-top: 1px solid var(--border); }
          .hc-dot { width: 8px; height: 8px; border-radius: 50%; flex: 0 0 auto; }
          .hc-dot.ok { background: #4dd28a; box-shadow: 0 0 6px rgba(77,210,138,.35); }
          .hc-dot.warn { background: var(--text-dim); }
          .hc-dot.bad { background: #e05656; box-shadow: 0 0 6px rgba(224,86,86,.35); }
          .hc-k { flex: 0 0 auto; color: var(--text); }
          .hc-d { margin-left: auto; color: var(--text-dim); font-size: 11px; text-align: right; overflow: hidden; text-overflow: ellipsis; }
          /* Verdict banner: the panel's headline answer, so it leads rather than trailing
             the check list. Colour lives on the container and the dot inherits it. */
          .hc-status { display: flex; align-items: center; gap: 9px; margin: 0 14px 10px;
              padding: 9px 11px; border-radius: 8px; border: 1px solid; line-height: 1.35; }
          .hc-sdot { width: 9px; height: 9px; border-radius: 50%; flex: 0 0 auto;
              background: currentColor; box-shadow: 0 0 8px currentColor; }
          .hc-stx { min-width: 0; }
          .hc-st { font-weight: 600; font-size: 12.5px; }
          .hc-sd { font-size: 11px; color: var(--text-dim); margin-top: 1px; }
          .hc-status.ok { color: #4dd28a; border-color: rgba(77,210,138,.35); background: rgba(77,210,138,.09); }
          .hc-status.warn { color: #e0b457; border-color: rgba(224,180,87,.35); background: rgba(224,180,87,.09); }
          .hc-status.bad { color: #e05656; border-color: rgba(224,86,86,.38); background: rgba(224,86,86,.10); }
          .hc-btnrow { display: flex; flex-wrap: wrap; gap: 6px; margin: 0 14px 8px; align-items: center; }
          .hc-idin { flex: 1 1 190px; min-width: 0; background: var(--bg-elev); border: 1px solid var(--border);
              border-radius: 7px; color: var(--text); font-size: 12px; padding: 6px 9px; outline: none; }
          .hc-idin:focus { border-color: var(--accent); }
          .hc-hint { margin: 10px 14px; font-size: 11.5px; color: var(--text-dim); line-height: 1.5; }
          .hc-sub { margin: 14px 14px 6px; font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: .05em; color: var(--accent-hi); }
          .hc-list { max-height: 260px; overflow-y: auto; overflow-x: hidden; border-top: 1px solid var(--border); }
          .hc-list .hc-row { padding: 4px 12px; font-size: 11.5px; }
          .hc-id { flex: 0 0 auto; color: var(--text-dim); font-variant-numeric: tabular-nums; min-width: 48px; }`);
      c.innerHTML = '';
      wrap = document.createElement('div'); wrap.id = 'hcWrap'; wrap.className = 'pane'; c.appendChild(wrap);
    }
    // The chrome (buttons + the id field) is built ONCE and then only updated in place.
    // renderHealth() runs on the 250 ms poll, so rebuilding it every tick would blow away
    // the text field mid-keystroke and drop focus. Only #hcBody is re-rendered.
    let body = document.getElementById('hcBody');
    if (!body) {
      wrap.innerHTML = '';
      const head = document.createElement('div'); head.className = 'hc-head';
      const t = document.createElement('span'); t.className = 'hc-title'; t.textContent = 'Reader health check';
      const btn = document.createElement('button'); btn.className = 'vw-btn'; btn.id = 'hcRunBtn';
      btn.dataset.tip = 'Verify every feature can read the game correctly (useful after a game update)';
      btn.addEventListener('click', hcRun);
      head.appendChild(t); head.appendChild(btn);
      wrap.appendChild(head);

      body = document.createElement('div'); body.id = 'hcBody'; wrap.appendChild(body);
    }
    // in-place label updates, so no element is replaced while it may hold focus
    const runBtn = document.getElementById('hcRunBtn');
    if (runBtn) runBtn.textContent = hcBusy ? 'Checking...' : 'Run check';
    hcKeepScroll = Array.from(document.querySelectorAll('.hc-list')).map(l => l.scrollTop);
    body.innerHTML = '';

    if (!hcData || !Array.isArray(hcData.checks)) {
      const e2 = document.createElement('div'); e2.className = 'hc-hint';
      e2.textContent = 'Checks that every feature can read the game correctly. Worth running after a game update if something looks off.';
      body.appendChild(e2);
      return;
    }
    // Verdict FIRST. It is the one thing the panel exists to answer, and it used to be a
    // small line under a long scrolling list, so on a tall check list you had to scroll to
    // find out whether anything was wrong.
    {
      const bad = hcData.checks.filter(x => x.ok === 0).length;
      const warn = hcData.checks.filter(x => x.ok === 2).length;
      const state = bad ? 'bad' : (warn ? 'warn' : 'ok');
      const n = hcData.checks.length;
      const st = document.createElement('div'); st.className = 'hc-status ' + state;
      const dot = document.createElement('span'); dot.className = 'hc-sdot';
      const tx = document.createElement('div'); tx.className = 'hc-stx';
      const t1 = document.createElement('div'); t1.className = 'hc-st';
      const t2 = document.createElement('div'); t2.className = 'hc-sd';
      t1.textContent = bad ? (bad + ' check' + (bad === 1 ? '' : 's') + ' failing')
                     : warn ? (warn === 1 ? '1 check needs attention' : warn + ' checks need attention')
                     : 'Everything is working';
      t2.textContent = bad ? 'A recent game update may be the cause. An app update will fix this.'
                     : warn ? (n - warn) + ' of ' + n + ' checks fully passed.'
                     : 'All ' + n + ' checks passed.';
      tx.appendChild(t1); tx.appendChild(t2);
      st.appendChild(dot); st.appendChild(tx);
      body.appendChild(st);
    }
    if (hcData.version) {
      const v = document.createElement('div'); v.className = 'hc-ver';
      v.textContent = 'client ' + hcData.version; body.appendChild(v);
    }
    const card = document.createElement('div'); card.className = 'hc-card';
    for (const chk of hcData.checks) {
      const r = document.createElement('div'); r.className = 'hc-row';
      const dot = document.createElement('span');
      dot.className = 'hc-dot ' + (chk.ok === 1 ? 'ok' : chk.ok === 2 ? 'warn' : 'bad');
      const nm = document.createElement('span'); nm.className = 'hc-k'; nm.textContent = chk.k;
      const dt = document.createElement('span'); dt.className = 'hc-d'; dt.textContent = chk.d || '';
      r.appendChild(dot); r.appendChild(nm); r.appendChild(dt);
      card.appendChild(r);
    }
    body.appendChild(card);
    renderIcons(body);
    // Restore list scroll positions recorded before this rebuild (see hcKeepScroll).
    if (hcKeepScroll.length) { const ls = body.querySelectorAll('.hc-list'); ls.forEach((l, i) => { if (hcKeepScroll[i] != null) l.scrollTop = hcKeepScroll[i]; }); hcKeepScroll = []; }
  }

  // "Item icons" card: is the bundled items.pack keeping up with the live cache.
  function renderIcons(body) {
    if (!icMisses && !icCov) return;
    const sub = document.createElement('div'); sub.className = 'hc-sub'; sub.textContent = 'Item icons';
    body.appendChild(sub);
    const card = document.createElement('div'); card.className = 'hc-card';
    const row = (state, k, d) => {
      const r = document.createElement('div'); r.className = 'hc-row';
      const dot = document.createElement('span'); dot.className = 'hc-dot ' + state;
      const nm = document.createElement('span'); nm.className = 'hc-k'; nm.textContent = k;
      const dt = document.createElement('span'); dt.className = 'hc-d'; dt.textContent = d;
      r.appendChild(dot); r.appendChild(nm); r.appendChild(dt); card.appendChild(r);
    };
    const m = icMisses || {};
    // Capture first: it is the action; the coverage lists below are the evidence.
    const cap = icCap || {};
    {
      const r = document.createElement('div'); r.className = 'hc-row';
      const b = document.createElement('button'); b.className = 'vw-btn';
      b.textContent = cap.busy ? 'Capturing...' : 'Capture icons now';
      b.disabled = !!cap.busy;
      b.addEventListener('click', () => { icCapture(); });
      const d = document.createElement('span'); d.className = 'hc-d';
      d.textContent = 'captured this session: ' + String(cap.sessionWritten || 0);
      r.appendChild(b); r.appendChild(d); card.appendChild(r);
    }
    if (cap.lastAt) {
      const when = new Date(cap.lastAt).toLocaleTimeString();
      // atlas: which GL texture passed the pack-similarity check, and every candidate's score
      const atlas = cap.candidates ? '; atlas: name ' + (cap.atlasName || 0) + ', score ' + (cap.atlasScore || '0') +
          ' (candidates: ' + cap.candidates + ')' : '';
      row(cap.status === 'ok' ? 'ok' : 'bad', 'Last capture', when + ': ' + (cap.status || '?') +
          (cap.status === 'ok' ? ' (' + cap.written + ' new of ' + cap.cells + ' cells)' : '') + atlas);
    }
    row(m.packIcons ? 'ok' : 'bad', 'Icon pack', m.packIds ? (m.packIds + ' ids, ' + m.packIcons + ' icons') : 'not loaded');
    if (icCov && icCov.pending) row('warn', 'Cache coverage', 'scanning...');
    else if (icCov && typeof icCov.items === 'number') {
      const miss = (icCov.missing || []).length;
      row('ok', 'Cache items with names', String(icCov.items));
      row(miss ? 'warn' : 'ok', 'Missing icons', miss ? (miss + ' of ' + icCov.items) : 'none');
      if (miss) {
        const list = document.createElement('div'); list.className = 'hc-list';
        for (const id of icCov.missing.slice(0, 40)) {
          const r = document.createElement('div'); r.className = 'hc-row';
          const i = document.createElement('span'); i.className = 'hc-id'; i.textContent = id;
          const n = document.createElement('span'); n.className = 'hc-k'; n.textContent = icNames[id] || '';
          r.appendChild(i); r.appendChild(n); list.appendChild(r);
        }
        if (miss > 40) {
          const r = document.createElement('div'); r.className = 'hc-row';
          const n = document.createElement('span'); n.className = 'hc-d'; n.textContent = '+ ' + (miss - 40) + ' more';
          r.appendChild(n); list.appendChild(r);
        }
        card.appendChild(list);
      }
    }
    const misses = Array.isArray(m.misses) ? m.misses : [];
    row(misses.length ? 'warn' : 'ok', 'Requested but absent this session', misses.length ? String(m.missCount) : 'none');
    if (misses.length) {
      const list = document.createElement('div'); list.className = 'hc-list';
      for (const [id, n] of misses.slice(0, 40)) {
        const r = document.createElement('div'); r.className = 'hc-row';
        const i = document.createElement('span'); i.className = 'hc-id'; i.textContent = id;
        const k = document.createElement('span'); k.className = 'hc-k'; k.textContent = icNames[id] || '';
        const d = document.createElement('span'); d.className = 'hc-d'; d.textContent = n + 'x';
        r.appendChild(i); r.appendChild(k); r.appendChild(d); list.appendChild(r);
      }
      card.appendChild(list);
    }
body.appendChild(card);
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { renderHealth });
registerTab({ id: 'health', render: renderHealth });
})();
