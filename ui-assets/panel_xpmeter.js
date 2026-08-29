// RuneToolsX panel: XP Meter (HUD window).
// Spliced inline into client.html at load; IIFE (window exports + registerTab; see the RTX registry in client.html).
//
// Replaces the companion-drawn XP card. Only the RENDERING moves: the 1 Hz sampler, the
// session baseline and the per-skill rate maths stay native (they run on the reader thread
// and must keep sampling while nothing is shown), and this reads the same rtx.xpPanelState
// snapshot the XP Tracker panel already uses -- see fetchXpTracker in panel_xptracker.js,
// which owns the fetch for both windows.
//
// It also fixes a reported bug by construction: the native card lived in a separate topmost
// window hit-tested with a bare PtInRect, so it took clicks even when a panel was drawn over
// it. A HUD window shares the one z-ordered consume-rect list, so overlap resolves properly.
//
// Row order is skill id, deliberately. The native widget ordered by first-gain time, which
// this snapshot does not carry; sorting by gained (as the Tracker mirror does) would make
// rows swap places under the cursor, which is wrong for a glanceable HUD.
(function () {

  let xpmDisp = Object.create(null);   // skill id -> eased displayed `gained`
  let xpmDispTotal = 0;
  let xpmRaf = 0, xpmSig = '', xpmEls = null;

  function xpmEase(cur, target) {
    const d = target - cur;
    if (Math.abs(d) < 1) return target;
    return cur + d * 0.18;
  }
  function xpmElapsed(ms) {
    const s = Math.floor((+ms || 0) / 1000);
    if (s <= 0) return '';
    if (s >= 3600) return Math.floor(s / 3600) + 'h ' + Math.floor((s % 3600) / 60) + 'm';
    return Math.floor(s / 60) + 'm ' + (s % 60) + 's';
  }

  function xpmVisibleRows(d) {
    if (!d || !d.rows) return [];
    return d.rows.filter(r => xpAuto ? r.gained > 0 : ((xpMask >>> r.id) & 1) !== 0);
  }

  // Rebuild the row DOM only when the SET of rows changes; every other frame just writes
  // text and bar widths, so an idle meter dirties a few dozen pixels rather than the window.
  function xpmBuild(host, d) {
    host.innerHTML = '';
    xpmEls = { rows: Object.create(null), total: null, head: null, elapsed: null };
    const head = document.createElement('div'); head.className = 'xpm-head';
    head.innerHTML = '<span class="xpm-dot"></span><span class="xpm-t">XP / hr</span>' +
                     '<span class="xpm-el"></span><span class="xpm-ph"></span>';
    host.appendChild(head);
    xpmEls.head = head.querySelector('.xpm-ph');
    xpmEls.elapsed = head.querySelector('.xpm-el');
    const mk = (id, label, cls) => {
      const r = document.createElement('div'); r.className = 'xpm-row' + (cls ? ' ' + cls : '');
      r.innerHTML = '<span class="xpm-bar"></span><span class="xpm-n"></span>' +
                    '<span class="xpm-g"></span><span class="xpm-p"></span>';
      r.querySelector('.xpm-n').textContent = label;
      host.appendChild(r);
      return { el: r, bar: r.querySelector('.xpm-bar'), g: r.querySelector('.xpm-g'), p: r.querySelector('.xpm-p') };
    };
    if (xpTotal && d && d.total) xpmEls.total = mk(-1, 'Total', 'is-total');
    for (const r of xpmVisibleRows(d)) xpmEls.rows[r.id] = mk(r.id, SKILL_NAMES[r.id] || ('#' + r.id), '');
    if (!xpmVisibleRows(d).length && !xpmEls.total) {
      const e = document.createElement('div'); e.className = 'xpm-empty';
      e.textContent = xpAuto ? 'No XP yet' : 'No skills pinned';
      host.appendChild(e);
    }
  }

  function xpmPaint() {
    xpmRaf = 0;
    if (!paneVisible('xpmeter')) { xpmEls = null; xpmSig = ''; return; }
    const host = document.getElementById('xpmBody');
    if (!host) { xpmEls = null; xpmSig = ''; return; }
    const d = xpStateData;
    const rows = xpmVisibleRows(d);
    const sig = rows.map(r => r.id).join(',') + '|' + (xpTotal ? 't' : '') + '|' + (d && d.rows ? '1' : '0');
    if (sig !== xpmSig || !xpmEls) { xpmSig = sig; xpmBuild(host, d); }

    if (d && d.rows) {
      let settled = true;
      const maxPh = rows.reduce((m, r) => Math.max(m, +r.ph || 0), 0) || 1;
      if (xpmEls.total && d.total) {
        const t = +d.total.gained || 0;
        xpmDispTotal = xpmEase(xpmDispTotal, t);
        if (xpmDispTotal !== t) settled = false;
        xpmEls.total.g.textContent = '+' + fmtXpNum(Math.round(xpmDispTotal));
        xpmEls.total.p.textContent = fmtXpNum(+d.total.ph || 0) + '/h';
        xpmEls.total.bar.style.width = '100%';
      }
      if (xpmEls.head) xpmEls.head.textContent = d.total ? fmtXpNum(+d.total.ph || 0) + '/h' : '';
      if (xpmEls.elapsed) xpmEls.elapsed.textContent = xpmElapsed(d.elapsed);
      for (const r of rows) {
        const e = xpmEls.rows[r.id]; if (!e) continue;
        const target = +r.gained || 0;
        const cur = xpmDisp[r.id] === undefined ? target : xpmEase(xpmDisp[r.id], target);
        xpmDisp[r.id] = cur;
        if (cur !== target) settled = false;
        e.g.textContent = '+' + fmtXpNum(Math.round(cur));
        e.p.textContent = fmtXpNum(+r.ph || 0) + '/h';
        e.bar.style.width = Math.max(0, Math.min(100, ((+r.ph || 0) / maxPh) * 100)).toFixed(1) + '%';
      }
      // Idle when every counter has caught up: the 4 Hz sweep restarts us on the next gain.
      if (settled) return;
    } else {
      return;   // no snapshot yet; the sweep calls renderXpMeter again once one lands
    }
    xpmRaf = requestAnimationFrame(xpmPaint);
  }

  function renderXpMeter() {
    const c = $('content');
    if (!c.querySelector('#xpmBody')) {
      c.innerHTML = '';
      injectStyle('xpmCss',
        '.xpm{display:flex;flex-direction:column;height:100%;padding:5px 6px;gap:1px;' +
        'background:rgba(14,15,21,0.80);border:1px solid rgba(var(--accent-rgb),0.42);border-radius:8px;' +
        'font-variant-numeric:tabular-nums;overflow:hidden}' +
        '.xpm-head{display:flex;align-items:center;gap:5px;padding:0 2px 3px;' +
        'border-bottom:1px solid rgba(255,255,255,0.07);margin-bottom:2px}' +
        '.xpm-dot{width:6px;height:6px;border-radius:50%;background:var(--accent);flex:none;' +
        'box-shadow:0 0 6px var(--accent);animation:xpm-breathe 2.2s ease-in-out infinite}' +
        '@keyframes xpm-breathe{0%,100%{opacity:.45}50%{opacity:1}}' +
        '.xpm-t{font-size:10px;letter-spacing:.05em;text-transform:uppercase;color:var(--text-mute)}' +
        '.xpm-el{font-size:9.5px;color:var(--text-mute);opacity:.75;margin-left:auto}' +
        '.xpm-ph{font-size:11px;color:var(--text);font-weight:600}' +
        '.xpm-row{position:relative;display:flex;align-items:center;gap:6px;' +
        'padding:1px 3px;border-radius:4px;font-size:11px;line-height:1.45}' +
        '.xpm-row.is-total{color:var(--accent)}' +
        '.xpm-bar{position:absolute;left:0;top:0;bottom:0;width:0;border-radius:4px;' +
        'background:rgba(var(--accent-rgb),0.16);transition:width .35s var(--ease);z-index:0}' +
        '.xpm-n{position:relative;z-index:1;flex:1;min-width:0;overflow:hidden;' +
        'text-overflow:ellipsis;white-space:nowrap;color:var(--text)}' +
        '.xpm-g{position:relative;z-index:1;color:var(--ok);flex:none}' +
        '.xpm-p{position:relative;z-index:1;color:var(--text-mute);flex:none;min-width:52px;text-align:right}' +
        '.xpm-empty{font-size:10.5px;color:var(--text-mute);padding:6px 3px;text-align:center}');
      const body = document.createElement('div');
      body.id = 'xpmBody'; body.className = 'xpm';
      c.appendChild(body);
      xpmEls = null; xpmSig = '';
    }
    if (!xpmRaf) xpmRaf = requestAnimationFrame(xpmPaint);
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { renderXpMeter });
registerTab({ id: 'xpmeter', render: renderXpMeter, open: function () { applyXpOverlay(); fetchXpTracker(true); } });
})();
