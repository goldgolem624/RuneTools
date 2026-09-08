  function pluginEsc(s) { const d = document.createElement('div'); d.textContent = String(s == null ? '' : s); return d.innerHTML; }
  function htmlEsc(s) { return String(s == null ? '' : s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;'); }

  const UI_DEF = { scale: 100, accent: '#e8c26a', contrast: false, motion: true, opacity: 93,
                   restore: true, toastTtl: 5000, hideWorld: false, numFmt: 'compact', hiddenCats: [],
                   font: 'variable', fontSize: 13, tabular: false, compactTips: false,
                   barClock: false, bar24h: false, barSession: false, barXp: false };
  const UI_FONTS = [['Segoe UI Variable', 'variable', "'Segoe UI Variable', 'Segoe UI', system-ui, sans-serif"],
                    ['Calibri', 'calibri', "Calibri, 'Segoe UI', sans-serif"],
                    ['Verdana', 'verdana', "Verdana, Geneva, sans-serif"],
                    ['Consolas', 'consolas', "Consolas, 'Cascadia Mono', monospace"]];
  const UI_ACCENTS = [['Gold', '#e8c26a'], ['Violet', '#8c6ffd'], ['Blue', '#5b9cff'], ['Teal', '#2fd0c4'], ['Green', '#4dd28a'],
                      ['Amber', '#f5b241'], ['Red', '#ff6b6b'], ['Pink', '#ff7ac8'], ['Mono', '#c9cfdd']];
  function uiZoomOf(el) {
    let z = 1;
    for (let n = el; n && n.nodeType === 1; n = n.parentNode) {
      const v = parseFloat(getComputedStyle(n).zoom);
      if (v && v > 0 && v !== 1) z *= v;
    }
    return z;
  }
  function uiEvPt(e, el) {
    const Z = uiZoomOf(el), t = e.target;
    if (t === el && typeof e.offsetX === 'number') return { x: e.offsetX / Z, y: e.offsetY / Z };
    if (t && t.parentNode === el && typeof e.offsetX === 'number') return { x: e.offsetX / Z + (t.offsetLeft || 0), y: e.offsetY / Z + (t.offsetTop || 0) };
    const r = el.getBoundingClientRect();
    return { x: e.clientX / Z - r.left, y: e.clientY / Z - r.top };
  }
  let _uiCfg = null;
  function uiCfg() {
    if (_uiCfg) return _uiCfg;
    let v = null;
    try { v = JSON.parse(prefGet('rtxUi', '') || 'null'); } catch (e) {}
    _uiCfg = Object.assign({}, UI_DEF, (v && typeof v === 'object') ? v : {});
    if (!Array.isArray(_uiCfg.hiddenCats)) _uiCfg.hiddenCats = [];
    return _uiCfg;
  }
  function uiCfgSave() { prefSet('rtxUi', JSON.stringify(uiCfg())); }
  function uiHexMul(hex, m) {
    const h = String(hex || '').replace('#', ''); if (h.length !== 6) return hex;
    const c = [0, 2, 4].map(i => Math.max(0, Math.min(255, Math.round(parseInt(h.slice(i, i + 2), 16) * m))));
    return '#' + c.map(x => x.toString(16).padStart(2, '0')).join('');
  }
  function uiHexRgb(hex) {
    const h = String(hex || '').replace('#', ''); if (h.length !== 6) return '124,92,252';
    return [0, 2, 4].map(i => parseInt(h.slice(i, i + 2), 16)).join(',');
  }
  let _uiScaleSent = -1;
  function uiApply() {
    const c = uiCfg(), r = document.documentElement.style;
    r.setProperty('--accent-hi', c.accent);
    r.setProperty('--accent', c.accent);
    r.setProperty('--accent-lo', uiHexMul(c.accent, 0.78));
    r.setProperty('--accent-rgb', uiHexRgb(c.accent));
    r.setProperty('--accent-ring', 'rgba(' + uiHexRgb(c.accent) + ',0.28)');
    const op = Math.max(0.3, Math.min(1, (Number(c.opacity) || 93) / 100));
    const bgRgb = (getComputedStyle(document.documentElement).getPropertyValue('--bg-rgb') || '').trim() || '12,14,20';
    r.setProperty('--win-bg', 'rgba(' + bgRgb + ',' + op.toFixed(2) + ')');
    r.setProperty('--bar-bg', 'rgba(' + bgRgb + ',' + Math.min(1, op + 0.02).toFixed(2) + ')');
    document.documentElement.classList.toggle('hc', !!c.contrast);
    document.documentElement.classList.toggle('nomotion', !c.motion);
    document.documentElement.classList.toggle('tabnums', !!c.tabular);
    const fnt = UI_FONTS.find(f => f[1] === c.font) || UI_FONTS[0];
    r.setProperty('--font-ui', fnt[2]);
    const fs = Math.max(11, Math.min(17, Number(c.fontSize) || 13));
    r.setProperty('--font-size', fs + 'px');
    r.setProperty('--content-zoom', (fs / 13).toFixed(3));
    try { if (typeof wmRectsSoon === 'function') wmRectsSoon(); } catch (e) {}
    try { uiBarTick(); } catch (e) {}
    try { if (typeof pluginThemeBroadcast === 'function') pluginThemeBroadcast(); } catch (e) {}
    const sc = Math.max(50, Math.min(200, Number(c.scale) || 100));
    if (sc !== _uiScaleSent) {
      try { if (bridge() && bridge().uiScale) { bridge().uiScale(myPid(), sc / 100); _uiScaleSent = sc; } } catch (e) {}
      setTimeout(() => { try { window.dispatchEvent(new Event('resize')); } catch (e) {} try { uiRepaintAll(); } catch (e) {} }, 120);
    }
  }
  function uiCatHidden(id) { return id !== 'Settings' && uiCfg().hiddenCats.indexOf(id) >= 0; }
  // Compact mode patches Number.prototype.toLocaleString: whole values from 100,000 up become k/m/b.
  (function () {
    const native = Number.prototype.toLocaleString;
    Number.prototype.toLocaleString = function (loc, opt) {
      if (loc === undefined && opt === undefined && uiCfg().numFmt !== 'full') {
        const n = Number(this);
        if (isFinite(n) && Math.abs(n) >= 100000 && Math.floor(n) === n) {
          const a = Math.abs(n), sign = n < 0 ? '-' : '';
          if (a >= 1e9) return sign + (a / 1e9).toFixed(2) + 'b';
          if (a >= 1e6) return sign + (a / 1e6).toFixed(2) + 'm';
          return sign + (a / 1e3).toFixed(2) + 'k';
        }
      }
      return native.call(this, loc, opt);
    };
  })();
  function accentRgba(a) { return 'rgba(' + uiHexRgb(uiCfg().accent) + ',' + a + ')'; }
  let _barInfoSig = '', _barXpArmed = false, _sessStartMs = 0;
  function uiBarTick() {
    try {
      const bar = $('menubar');
      if (bar) {
        const r = bar.getBoundingClientRect(), vw = window.innerWidth || 1280, vh = window.innerHeight || 720;
        if (r.right > vw - 4 || r.left < 4 || r.bottom > vh - 4 || r.top < 4) { positionMenubar(); wmRectsSoon(); }
      }
    } catch (e) {}
    const el = $('hdr-info'); if (!el) return;
    const c = uiCfg(), parts = [];
    if (c.barClock) {
      const d = new Date();
      let h = d.getHours(), m = String(d.getMinutes()).padStart(2, '0');
      if (c.bar24h) parts.push('<b>' + String(h).padStart(2, '0') + ':' + m + '</b>');
      else parts.push('<b>' + ((h % 12) || 12) + ':' + m + '</b> ' + (h < 12 ? 'am' : 'pm'));
    }
    {
      const sn = (typeof lastSnap !== 'undefined') ? lastSnap : null;
      const loggedIn = !!(sn && sn.status >= 20);
      const key = 'rtxSessStart:' + myPid();
      if (loggedIn) {
        if (!_sessStartMs) {
          let saved = 0; try { saved = Number(localStorage.getItem(key)) || 0; } catch (e) {}
          _sessStartMs = saved > 0 ? saved : Date.now();
          try { localStorage.setItem(key, String(_sessStartMs)); } catch (e) {}
        }
      } else if (_sessStartMs && sn && sn.status >= 0) {
        _sessStartMs = 0; try { localStorage.removeItem(key); } catch (e) {}
      }
    }
    if (c.barSession) {
      const sec = _sessStartMs ? Math.floor((Date.now() - _sessStartMs) / 1000) : 0;
      const t = sec >= 3600 ? Math.floor(sec / 3600) + 'h ' + String(Math.floor((sec % 3600) / 60)).padStart(2, '0') + 'm'
                            : Math.floor(sec / 60) + 'm ' + String(sec % 60).padStart(2, '0') + 's';
      parts.push('Session <b>' + t + '</b>');
    }
    if (c.barXp) {
      if (!_barXpArmed) { _barXpArmed = true; try { if (typeof applyXpOverlay === 'function') applyXpOverlay(); } catch (e) {} }
      try { if (typeof fetchXpTracker === 'function') fetchXpTracker(false); } catch (e) {}
      const d = (typeof xpStateData !== 'undefined') ? xpStateData : null;
      {
        const ph = (d && d.total) ? (+d.total.ph || 0) : 0;
        parts.push('<b>' + (typeof fmtXpNum === 'function' ? fmtXpNum(ph) : ph) + '</b> xp/h');
      }
    }
    const html = parts.join('<span style="color:var(--text-mute)">|</span>');
    if (html !== _barInfoSig || el.innerHTML !== html) {
      _barInfoSig = html; el.innerHTML = html;
      try { positionMenubar(); } catch (e) {}
      try { wmRectsSoon(); } catch (e) {}
    }
  }
  setInterval(() => { try { uiBarTick(); } catch (e) {} }, 1000);

