// RuneToolsX panel: Health check (Developer): on-demand update check, client code, calibration, live
// layout, packets, cache content and the companion, grouped in dependency order.
(function () {

  let hcData = null, hcBusy = false, hcSeq = -1, hcPollTimer = 0;
  let hcDiff = null, hcHist = [], hcCmp = null, hcCmpA = '', hcCmpB = '';
  const hcOpen = {};          // group -> expanded (user toggles survive re-renders)
  let icMisses = null, icCov = null, icNames = {}, icTimer = 0;
  let hcKeepScroll = [];   // .hc-list scrollTop values captured before a rebuild
  let hcSig = '';          // last rendered data signature
  async function icFetch() {
    try { icMisses = JSON.parse(await rtxData.raw('host.iconMisses')); } catch (e) { icMisses = null; }
    try { icCov = JSON.parse(await rtxData.raw('host.iconCoverage')); } catch (e) { icCov = null; }
    if (icCov && icCov.pending) { clearTimeout(icTimer); icTimer = setTimeout(icFetch, 1500); }
    {
      const want = (icCov && Array.isArray(icCov.missing) ? icCov.missing.slice(0, 40) : [])
        .concat(icMisses && Array.isArray(icMisses.misses) ? icMisses.misses.slice(0, 40).map(x => x[0]) : []);
      for (const id of want) {
        if (id in icNames) continue;
        try { const d = JSON.parse(await rtxData.raw('cache.itemInfo', id) || 'null'); icNames[id] = (d && d.name) || ''; } catch (e) { icNames[id] = ''; }
      }
    }
    paneRun('health', renderHealth);
  }
  function hcParse(t) { try { return JSON.parse(t); } catch (e) { return null; } }
  async function hcHistory() {
    hcHist = hcParse(await rtxData.raw('host.healthHistory')) || [];
    if (Array.isArray(hcHist) && hcHist.length) {
      hcDiff = hcParse(await rtxData.raw('host.healthDiff', '', hcHist[0].name));
      if (!hcCmpB) hcCmpB = hcHist[0].name;
      if (!hcCmpA && hcHist.length > 1) hcCmpA = hcHist[1].name;
    }
  }
  async function hcPoll() {
    clearTimeout(hcPollTimer);
    const p = hcParse(await rtxData.raw('host.readerHealthPoll'));
    if (p && !p.running && p.seq !== hcSeq) {
      hcSeq = p.seq; hcData = p.run; hcBusy = false;
      try { await hcHistory(); } catch (e) {}
      icFetch();
    } else if (p && p.running) {
      hcPollTimer = setTimeout(hcPoll, 400);
    } else { hcBusy = false; }
    paneRun('health', renderHealth);
  }
  async function hcRun() {
    if (hcBusy || !bridge() || !bridge().readerHealthStart) return;
    hcBusy = true; paneRun('health', renderHealth);
    let pins = '{}';
    try { pins = JSON.stringify(window.RTX_PINS || {}); } catch (e) {}
    try { hcSeq = Number(await rtxData.raw('host.readerHealthStart', pins)); } catch (e) { hcBusy = false; }
    hcPollTimer = setTimeout(hcPoll, 400);
  }
  async function hcMarkReviewed() {
    const b = hcData && hcData.build;
    if (!b) return;
    try { await rtxData.raw('host.healthMarkReviewed', hcData.version || '', b.stamp || ''); } catch (e) {}
    hcRun();
  }
  function hcCopy() {
    if (!hcData || !bridge() || !bridge().copyClipboard) return;
    try { bridge().copyClipboard(JSON.stringify(hcData, null, 1)); if (typeof uiNotify === 'function') uiNotify('Report copied'); } catch (e) {}
  }
  async function hcCompare() {
    if (!hcCmpA || !hcCmpB) return;
    hcCmp = hcParse(await rtxData.raw('host.healthDiff', hcCmpA, hcCmpB));
    paneRun('health', renderHealth);
  }

  const DOT = s => s === 1 ? 'ok' : s === 0 ? 'bad' : s === 2 ? 'warn' : 'na';
  const WORD = s => s === 1 ? 'pass' : s === 0 ? 'fail' : s === 2 ? 'warn' : 'not checked';
  // which part of the game a failing row says moved
  const CAUSE = { 'Client code': 'code', 'Calibration': 'code', 'Content': 'content', 'Cache format': 'content', 'Companion': 'companion' };
  function el(tag, cls, text) { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; }

  function renderHealth() {
    const c = $('content');
    let wrap = $('hcWrap');
    if (!wrap) {
      injectStyle('hcCss', `
          .hc-head { display: flex; align-items: center; gap: 6px; padding: 12px 14px 8px; }
          .hc-title { flex: 1; font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: .05em; color: var(--accent-hi); }
          .hc-ver { padding: 0 14px 8px; font-size: 11px; color: var(--text-dim); display: flex; flex-wrap: wrap; gap: 6px; align-items: center; }
          .hc-chip { border: 1px solid var(--border); border-radius: 6px; padding: 1px 6px; background: var(--bg-elev); }
          .hc-chip.warn { border-color: rgba(224,180,87,.5); color: #e0b457; }
          .hc-card { margin: 0 12px; background: var(--bg-elev); border: 1px solid var(--border); border-radius: 10px; }
          .hc-card + .hc-card { margin-top: 8px; }
          .hc-row { display: flex; align-items: center; gap: 8px; padding: 7px 12px; font-size: 12px; }
          .hc-row + .hc-row, .hc-grp + .hc-row, .hc-row + .hc-sub2 { border-top: 1px solid var(--border); }
          .hc-dot { width: 8px; height: 8px; border-radius: 50%; flex: 0 0 auto; }
          .hc-dot.ok { background: #4dd28a; box-shadow: 0 0 6px rgba(77,210,138,.35); }
          .hc-dot.warn { background: #e0b457; }
          .hc-dot.na { background: var(--text-mute, #666); opacity: .6; }
          .hc-dot.bad { background: #e05656; box-shadow: 0 0 6px rgba(224,86,86,.35); }
          .hc-k { flex: 0 0 auto; color: var(--text); }
          .hc-k.na { color: var(--text-dim); }
          .hc-d { margin-left: auto; color: var(--text-dim); font-size: 11px; text-align: right; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; max-width: 62%; }
          .hc-row.wrap .hc-d { white-space: normal; }
          .hc-why { padding: 0 12px 7px 28px; font-size: 10.5px; color: var(--text-dim); }
          .hc-why b { color: var(--text); font-weight: 600; }
          .hc-grp { display: flex; align-items: center; gap: 8px; padding: 8px 12px; font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: .04em; color: var(--accent-hi); cursor: pointer; }
          .hc-grp .hc-n { margin-left: auto; color: var(--text-dim); font-weight: 600; letter-spacing: 0; text-transform: none; }
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
          .hc-ab { padding: 1px 7px; font-size: 10.5px; margin-left: 4px; }
          .hc-ab.on { border-color: var(--accent); color: var(--accent-hi); }
          .hc-hint { margin: 10px 14px; font-size: 11.5px; color: var(--text-dim); line-height: 1.5; }
          .hc-sub { margin: 14px 14px 6px; font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: .05em; color: var(--accent-hi); }
          .hc-list { max-height: 260px; overflow-y: auto; overflow-x: hidden; border-top: 1px solid var(--border); }
          .hc-list .hc-row { padding: 4px 12px; font-size: 11.5px; }
          .hc-id { flex: 0 0 auto; color: var(--text-dim); font-variant-numeric: tabular-nums; min-width: 48px; }`);
      c.innerHTML = '';
      wrap = document.createElement('div'); wrap.id = 'hcWrap'; wrap.className = 'pane'; c.appendChild(wrap);
    }
    let body = document.getElementById('hcBody');
    if (!body) {
      wrap.innerHTML = '';
      const head = el('div', 'hc-head');
      head.appendChild(el('span', 'hc-title', 'Health check'));
      const btn = el('button', 'vw-btn'); btn.id = 'hcRunBtn';
      btn.dataset.tip = 'Check everything the app reads from the game (run after a game update)';
      btn.addEventListener('click', hcRun);
      const cp = el('button', 'vw-btn', 'Copy report'); cp.id = 'hcCopyBtn';
      cp.dataset.tip = 'Copy this run as JSON';
      cp.addEventListener('click', hcCopy);
      head.appendChild(btn); head.appendChild(cp);
      wrap.appendChild(head);
      body = document.createElement('div'); body.id = 'hcBody'; wrap.appendChild(body);
    }
    const runBtn = document.getElementById('hcRunBtn');
    if (runBtn) runBtn.textContent = hcBusy ? 'Checking...' : 'Run check';
    const hcSigNow = JSON.stringify([hcData && hcData.summary, hcSeq, hcBusy, hcDiff, hcCmp, hcHist.length, hcCmpA, hcCmpB, hcOpen, icMisses, icCov]);
    if (hcSigNow === hcSig && body.childNodes.length) return;
    hcSig = hcSigNow;
    hcKeepScroll = Array.from(document.querySelectorAll('.hc-list')).map(l => l.scrollTop);
    body.innerHTML = '';

    if (!hcData || !Array.isArray(hcData.checks)) {
      body.appendChild(el('div', 'hc-hint', hcBusy ? 'Checking the game client...' : 'Checks the client code, offsets, live layout, packets, cache content and the companion. Run it after a game update.'));
      renderIcons(body);
      return;
    }
    const checks = hcData.checks;
    const s = hcData.summary || {};
    const byId = {};
    for (const r of checks) if (r.id) byId[r.id] = r;
    // verdict banner: the first failing link and what kind of change it is
    {
      const bad = checks.filter(x => x.ok === 0);
      const warn = checks.filter(x => x.ok === 2).length;
      const state = bad.length ? 'bad' : (warn ? 'warn' : 'ok');
      const st = el('div', 'hc-status ' + state);
      st.appendChild(el('span', 'hc-sdot'));
      const tx = el('div', 'hc-stx');
      let title, sub;
      if (bad.length) {
        const first = byId[s.firstFail] || bad[0];
        const deps = checks.filter(x => x.dep === (first.id || '')).length;
        title = (first.k || 'A check') + ' failed' + (deps ? '. ' + deps + ' check' + (deps === 1 ? '' : 's') + ' depend on it.' : '');
        const cnt = {};
        for (const r of bad) { const k = CAUSE[r.g] || 'layout'; cnt[k] = (cnt[k] || 0) + 1; }
        const parts = [];
        if (cnt.code) parts.push('Game code moved: ' + cnt.code);
        if (cnt.layout) parts.push('Live layout: ' + cnt.layout);
        if (cnt.content) parts.push('Content moved: ' + cnt.content);
        if (cnt.companion) parts.push('Companion: ' + cnt.companion);
        sub = parts.join('. ') + '.';
      } else if (warn) {
        title = warn === 1 ? '1 check needs attention' : warn + ' checks need attention';
        sub = 'Nothing failed.';
      } else {
        title = 'Everything is working';
        sub = 'All ' + (s.pass || checks.length) + ' checks passed.';
      }
      tx.appendChild(el('div', 'hc-st', title));
      tx.appendChild(el('div', 'hc-sd', sub));
      st.appendChild(tx);
      body.appendChild(st);
    }
    // build strip and run summary
    {
      const b = hcData.build || {};
      const v = el('div', 'hc-ver');
      const flav = b.flavour === 'vulkan' ? 'Vulkan' : b.flavour === 'opengl' ? 'OpenGL' : (b.flavour || '');
      v.appendChild(el('span', 'hc-chip', hcData.version || 'unknown build'));
      if (flav) v.appendChild(el('span', 'hc-chip', flav));
      if (b.stamp) v.appendChild(el('span', 'hc-chip', b.stamp));
      v.appendChild(el('span', 'hc-chip' + (b.known ? '' : ' warn'), b.known ? 'validated' + (b.validated ? ' ' + b.validated : '') : 'new build'));
      v.appendChild(el('span', null, (s.pass | 0) + ' pass  ' + (s.fail | 0) + ' fail  ' + (s.warn | 0) + ' warn  ' + (s.unchecked | 0) + ' not checked  ' + (s.ms | 0) + ' ms'));
      const gb = byId['build.game'];
      if (gb && gb.ok === 2) {
        const mr = el('button', 'vw-btn', 'Mark reviewed');
        mr.dataset.tip = 'Clear the update notice for this game build';
        mr.addEventListener('click', hcMarkReviewed);
        v.appendChild(mr);
      }
      body.appendChild(v);
    }
    // groups, in the order the check ran them (each depends on the ones before)
    const groups = [];
    const gmap = {};
    for (const r of checks) {
      const g = r.g || 'Other';
      if (!gmap[g]) { gmap[g] = []; groups.push(g); }
      gmap[g].push(r);
    }
    for (const g of groups) {
      const rows = gmap[g];
      const worst = rows.some(r => r.ok === 0) ? 0 : rows.some(r => r.ok === 2) ? 2 : rows.every(r => r.ok === 3) ? 3 : 1;
      const pass = rows.filter(r => r.ok === 1).length;
      if (!(g in hcOpen)) hcOpen[g] = worst === 0;
      const card = el('div', 'hc-card');
      const gh = el('div', 'hc-grp');
      gh.appendChild(el('span', 'hc-dot ' + DOT(worst)));
      gh.appendChild(el('span', null, g));
      gh.appendChild(el('span', 'hc-n', pass + '/' + rows.length + (hcOpen[g] ? '' : '  +')));
      gh.addEventListener('click', () => { hcOpen[g] = !hcOpen[g]; hcSig = ''; paneRun('health', renderHealth); });
      card.appendChild(gh);
      if (hcOpen[g]) {
        for (const chk of rows) {
          const r = el('div', 'hc-row' + (chk.ok === 0 ? ' wrap' : ''));
          r.appendChild(el('span', 'hc-dot ' + DOT(chk.ok)));
          r.appendChild(el('span', 'hc-k' + (chk.ok === 3 ? ' na' : ''), chk.k));
          const dt = el('span', 'hc-d', chk.d || ''); dt.title = chk.d || '';
          r.appendChild(dt);
          card.appendChild(r);
          if (chk.ok === 0 && (Array.isArray(chk.f) && chk.f.length || chk.exp || chk.got)) {
            const why = el('div', 'hc-why');
            if (chk.exp || chk.got) why.appendChild(document.createTextNode('Expected ' + (chk.exp || '?') + ', found ' + (chk.got || '?') + '. '));
            if (Array.isArray(chk.f) && chk.f.length) { why.appendChild(el('b', null, 'Breaks: ')); why.appendChild(document.createTextNode(chk.f.join(', '))); }
            card.appendChild(why);
          }
        }
      }
      body.appendChild(card);
    }
    // what changed since the last clean run, else the previous build's, else the previous run
    if (hcDiff && hcDiff.ok && ((hcDiff.rows || []).length || (hcDiff.facts || []).length)) {
      const since = { 'last clean': 'the last clean run', 'previous build': 'the previous build', 'previous run': 'the previous run',
                      'earlier run': 'the previous run (more failures)' };
      body.appendChild(el('div', 'hc-sub', 'Changed since ' + (since[hcDiff.against] || 'the last clean run')));
      const card = el('div', 'hc-card');
      const list = el('div', 'hc-list');
      for (const r0 of (hcDiff.rows || []).slice(0, 60)) {
        const r = el('div', 'hc-row');
        r.appendChild(el('span', 'hc-dot ' + DOT(r0.now)));
        r.appendChild(el('span', 'hc-k', r0.k || r0.id));
        r.appendChild(el('span', 'hc-d', (r0.was < 0 ? 'new' : WORD(r0.was)) + ' to ' + (r0.now < 0 ? 'gone' : WORD(r0.now))));
        list.appendChild(r);
      }
      for (const f of (hcDiff.facts || []).slice(0, 80)) {
        const r = el('div', 'hc-row');
        r.appendChild(el('span', 'hc-id', f.k));
        const d = el('span', 'hc-d', f.was + '  to  ' + f.now); d.title = f.was + ' to ' + f.now;
        r.appendChild(d);
        list.appendChild(r);
      }
      card.appendChild(list);
      body.appendChild(card);
    }
    // compare any two runs from the history
    if (hcHist.length > 1) {
      body.appendChild(el('div', 'hc-sub', 'Compare runs'));
      const card = el('div', 'hc-card');
      const list = el('div', 'hc-list');
      for (const h of hcHist.slice(0, 30)) {
        const r = el('div', 'hc-row');
        r.appendChild(el('span', 'hc-dot ' + (h.fail ? 'bad' : h.warn ? 'warn' : 'ok')));
        r.appendChild(el('span', 'hc-id', h.name.replace('.json', '')));
        r.appendChild(el('span', 'hc-d', h.fail + ' fail  ' + h.warn + ' warn'));
        for (const side of ['A', 'B']) {
          const picked = (side === 'A' ? hcCmpA : hcCmpB) === h.name;
          const b = el('button', 'vw-btn hc-ab' + (picked ? ' on' : ''), side);
          b.addEventListener('click', () => { if (side === 'A') hcCmpA = h.name; else hcCmpB = h.name; hcCmp = null; hcSig = ''; paneRun('health', renderHealth); });
          r.appendChild(b);
        }
        list.appendChild(r);
      }
      card.appendChild(list);
      body.appendChild(card);
      const row = el('div', 'hc-btnrow');
      const go = el('button', 'vw-btn', 'Compare A with B');
      go.addEventListener('click', hcCompare);
      row.appendChild(go);
      body.appendChild(row);
      if (hcCmp && hcCmp.ok) {
        const card = el('div', 'hc-card');
        const list = el('div', 'hc-list');
        if (!(hcCmp.rows || []).length && !(hcCmp.facts || []).length) list.appendChild(el('div', 'hc-row', 'No difference'));
        for (const r0 of (hcCmp.rows || []).slice(0, 80)) {
          const r = el('div', 'hc-row');
          r.appendChild(el('span', 'hc-dot ' + DOT(r0.now)));
          r.appendChild(el('span', 'hc-k', r0.k || r0.id));
          r.appendChild(el('span', 'hc-d', (r0.was < 0 ? 'new' : WORD(r0.was)) + ' to ' + (r0.now < 0 ? 'gone' : WORD(r0.now))));
          list.appendChild(r);
        }
        for (const f of (hcCmp.facts || []).slice(0, 120)) {
          const r = el('div', 'hc-row');
          r.appendChild(el('span', 'hc-id', f.k));
          r.appendChild(el('span', 'hc-d', f.was + '  to  ' + f.now));
          list.appendChild(r);
        }
        card.appendChild(list);
        body.appendChild(card);
      }
    }
    renderIcons(body);
    if (hcKeepScroll.length) { const keep = hcKeepScroll; hcKeepScroll = []; setTimeout(() => { const ls = body.querySelectorAll('.hc-list'); ls.forEach((l, i) => { if (keep[i] != null) l.scrollTop = keep[i]; }); }, 0); }   // after layout, or Ultralight clamps it to 0
  }

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
    row(m.rendered ? 'ok' : 'warn', 'Rendered icons', String(m.rendered || 0));
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

Object.assign(window, { renderHealth });
registerTab({ id: 'health', render: renderHealth });
})();
