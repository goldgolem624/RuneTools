// RuneToolsX panel: XP Tracker (configures the host-drawn on-game overlay and mirrors its metrics).
// Spliced inline into client.html at load; IIFE (window exports + registerTab; see the RTX registry in client.html).
(function () {

  xpStateData = null; let xpStateRaw = ''; let xpLiveSig = ''; let xpFetching = false; let xpFetchAt = 0;
  async function fetchXpTracker(force) {
    if (!bridge() || !bridge().xpPanelState || xpFetching) return;
    const _t = Date.now(); if (!force && _t - xpFetchAt < 1000) return; xpFetchAt = _t;
    xpFetching = true;
    try { const r = await rtxData.raw('host.xpPanelState'); xpStateData = JSON.parse(r); xpStateRaw = r; }
    catch (e) {} finally { xpFetching = false; }
    paneRun('xptracker', renderXpLive);
  }
  function fmtXpNum(v) {
    v = +v || 0;
    // Precision by magnitude (Preferences convention): billions x.yy, millions x.y, thousands x.y.
    if (v >= 1e9) return (v / 1e9).toFixed(2) + 'B';
    if (v >= 1e6) return (v / 1e6).toFixed(1) + 'M';
    if (v >= 10000) return (v / 1000).toFixed(1) + 'K';
    return v.toLocaleString();
  }
  function renderXpTracker() {
    const c = $('content');
    let wrap = $('xpWrap');
    if (!wrap) {
      c.innerHTML = '';
      wrap = document.createElement('div'); wrap.id = 'xpWrap'; wrap.className = 'pane stor-wrap'; c.appendChild(wrap);
      buildXpConfig(wrap);
      const live = document.createElement('div'); live.id = 'xpLive'; wrap.appendChild(live);
    }
    renderXpLive();
  }
  function buildXpConfig(wrap) {
    let box = $('xpCfgBox');
    if (box) box.remove();
    box = document.createElement('div'); box.id = 'xpCfgBox'; box.className = 'stor-box';
    const live = $('xpLive');
    if (live) wrap.insertBefore(box, live); else wrap.appendChild(box);
    const h = document.createElement('div'); h.className = 'stor-h'; h.textContent = 'On-screen panel'; box.appendChild(h);
    // applyXpOverlay keeps the native sampler pointed at this client; xpSyncWindow opens or
    // closes the XP Meter HUD window, which is what actually draws now.
    const rebuild = () => { saveXpCfg(); applyXpOverlay(); xpSyncWindow(); buildXpConfig(wrap); };
    const settingRow = (name, desc, mkControl, onClick) => {
      const r = document.createElement('div');
      r.style.cssText = 'display:flex;align-items:center;gap:10px;padding:7px 9px;border-radius:7px;background:var(--bg-elev);margin-bottom:5px;' + (onClick ? 'cursor:pointer;' : '');
      const l = document.createElement('div'); l.style.cssText = 'flex:1 1 auto;min-width:0;';
      const nm = document.createElement('div'); nm.className = 'al-name'; nm.textContent = name;
      const ds = document.createElement('div'); ds.className = 'al-desc'; ds.textContent = desc;
      l.appendChild(nm); l.appendChild(ds);
      r.appendChild(l); r.appendChild(mkControl());
      if (onClick) r.addEventListener('click', onClick);
      box.appendChild(r);
      return r;
    };
    const pill = (on) => {
      const p = document.createElement('div'); p.className = 'al-pill' + (on ? ' on' : '');
      p.appendChild(document.createElement('span'));
      return p;
    };
    settingRow('Show on game',
               'Opens the XP Meter as its own window over the game. Drag its title bar to move it, resize from any edge, and double-click the title to roll it up.',
               () => pill(xpOn), () => { xpOn = !xpOn; rebuild(); });
    settingRow('Lock panel',
               'Locked panels are click-through: the mouse passes straight to the game, so the meter cannot be dragged or resized until you unlock it here.',
               () => pill(xpLock), () => { setXpLock(!xpLock); buildXpConfig(wrap); });
    settingRow('Total row',
               'Adds a combined row that sums every skill.',
               () => pill(xpTotal), () => { xpTotal = !xpTotal; rebuild(); });
    settingRow('Skills shown',
               xpAuto ? 'Active: a row appears for each skill as it gains XP this session.'
                      : 'Custom: only the skills picked below are shown.',
               () => {
                 const g = document.createElement('div'); g.className = 'pet-chips';
                 [['Active', true], ['Custom', false]].forEach(([lbl, val]) => {
                   const b = document.createElement('button'); b.className = 'pet-chip' + (xpAuto === val ? ' on' : '');
                   b.textContent = lbl;
                   b.addEventListener('click', e => { e.stopPropagation(); xpAuto = val; rebuild(); });
                   g.appendChild(b);
                 });
                 return g;
               }, null);
    if (!xpAuto) {
      const grid = document.createElement('div'); grid.className = 'pet-chips';
      grid.style.cssText = 'margin:2px 0 8px;';
      for (let i = 0; i < 29; i++) {
        const on = ((xpMask >>> i) & 1) !== 0;
        const b = document.createElement('button'); b.className = 'pet-chip' + (on ? ' on' : '');
        const ico = document.createElement('span'); ico.className = 'qr-ico sk-icon';
        ico.dataset.skill = String(i);
        ico.style.verticalAlign = '-4px'; ico.style.marginRight = '4px';
        attachSkillIcon(ico, i);
        b.appendChild(ico); b.appendChild(document.createTextNode(SKILL_NAMES[i] || ('#' + i)));
        b.addEventListener('click', () => {
          xpMask = (xpMask ^ (1 << i)) >>> 0;
          rebuild();
        });
        grid.appendChild(b);
      }
      box.appendChild(grid);
    }
    settingRow('Session',
               'Gains and XP/h count from the session start. Reset restarts them from now.',
               () => {
                 const b = document.createElement('button'); b.className = 'pet-chip';
                 b.textContent = 'Reset';
                 b.addEventListener('click', e => {
                   e.stopPropagation();
                   try { rtxData.sync('act.xpPanelReset'); } catch (e2) {}
                   fetchXpTracker(true);
                 });
                 return b;
               }, null);
  }
  function renderXpLive() {
    const live = $('xpLive'); if (!live) return;
    const d = xpStateData;
    // Tracker samples at 1 Hz but renderPane() runs every 250 ms; skip no-op rebuilds.
    const sig = xpStateRaw + '|' + xpAuto + '|' + xpMask + '|' + xpTotal;
    if (sig === xpLiveSig && live.childNodes.length) return;
    xpLiveSig = sig;
    live.innerHTML = '';
    const box = document.createElement('div'); box.className = 'stor-box';
    const h = document.createElement('div'); h.className = 'stor-h';
    if (!d || !d.rows) { h.textContent = 'Session'; box.appendChild(h); box.insertAdjacentHTML('beforeend', '<div class="stor-empty">Reading...</div>'); live.appendChild(box); return; }
    const el = +d.elapsed || 0, s = Math.floor(el / 1000);
    h.textContent = 'Session' + (s > 0 ? ': ' + (s >= 3600 ? Math.floor(s / 3600) + 'h ' + Math.floor((s % 3600) / 60) + 'm' : Math.floor(s / 60) + 'm ' + (s % 60) + 's') : '');
    box.appendChild(h);
    const row = (name, gained, ph, accent) => {
      const r = document.createElement('div'); r.className = 'q-req' + (accent ? ' ok' : '');
      r.innerHTML = '<span class="qr-n">' + name + '</span>' +
                    '<span class="qr-src">+' + fmtXpNum(gained) + '</span>' +
                    '<span class="qr-v">' + fmtXpNum(ph) + '/h</span>';
      box.appendChild(r);
    };
    if (d.total) row('Total (all skills)', d.total.gained, d.total.ph, true);
    const rows = d.rows.filter(r2 => xpAuto ? r2.gained > 0 : ((xpMask >>> r2.id) & 1) !== 0);
    rows.sort((a, b) => b.gained - a.gained);
    for (const r2 of rows) row(SKILL_NAMES[r2.id] || ('#' + r2.id), r2.gained, r2.ph, false);
    if (!rows.length) box.insertAdjacentHTML('beforeend', '<div class="stor-empty">No XP gained yet this session.</div>');
    live.appendChild(box);
    const note = document.createElement('div'); note.className = 'pet-count'; note.style.marginTop = '6px';
    note.textContent = 'Skills at the 200,000,000 XP cap cannot be tracked: the game discards their gains.';
    live.appendChild(note);
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { buildXpConfig, fetchXpTracker, fmtXpNum, renderXpTracker });
registerTab({ id: 'xptracker', render: renderXpTracker, open: function () { applyXpOverlay(); fetchXpTracker(true); } });
})();
