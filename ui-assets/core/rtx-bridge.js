  let __paneRoot = null;
  const __paneRoots = Object.create(null);   // tab id -> .content element
  function paneRoot(id) { return __paneRoots[id] || null; }
  const $ = id => (id === 'content' && __paneRoot) ? __paneRoot : document.getElementById(id);
  // ---- CS2 switch maps (rtx.cs2Switches -> %USERPROFILE%\RuneToolsX\cs2\switches.json) ----
  let _cs2SwP = null;
  function cs2SwitchScripts() {
    if (!_cs2SwP) _cs2SwP = (async () => {
      try {
        const d = JSON.parse(await bridge().cs2Switches() || '{}');
        if (d && d.scripts && Object.keys(d.scripts).length) return d.scripts;
      } catch (e) {}
      _cs2SwP = null;            // absent or unreadable: retry on a later call
      return {};
    })();
    return _cs2SwP;
  }
  async function cs2SwitchEntries(scriptId) {
    if (!bridge() || !bridge().cs2Switches) return null;
    const sc = await cs2SwitchScripts();
    const e = sc && sc[String(scriptId)];
    return (e && e.entries && typeof e.entries === 'object') ? e.entries : null;
  }
  const myPid = () => Number((window.__rtx_pid || 0));
  let activeTab = 'player';   // the FOCUSED window's tab (panel self-gates + dev reload)
  try { if (window.__rtxDevReload) { const t = localStorage.getItem('rtxDevTab');
        if (t && (String(t).indexOf('plugin:') === 0 || (TABS.some(x => x.id === t) && (t !== 'dung' || DUNG_ENABLED)))) { activeTab = t; console.log('[devreload] restored tab ' + t); }
        else console.log('[devreload] restore rejected, stored=' + t);
      } } catch (e) { console.log('[devreload] restore threw: ' + e); }
  let lastSnap  = null;
  let host      = null;   // cached after first hostInfo() call

  function bridge() {
    return (typeof window.rtx === 'object') ? window.rtx : null;
  }
  const _bjErrAt = new Map();
  async function bridgeJson(method, ...args) {
    const b = bridge();
    if (!b || typeof b[method] !== 'function') {
      bridgeJson.lastWhy = b ? 'unsupported' : 'noclient';
      bridgeJson.whyByMethod[method] = bridgeJson.lastWhy;
      return null;
    }
    try { return bridgeJson.parse(method, await b[method](...args)); }
    catch (e) { return bridgeJson.fail(method, e); }
  }
  bridgeJson.parse = function (method, text) {
    const v = JSON.parse(text);
    if (v && typeof v === 'object' && !Array.isArray(v) && v.ok === false && typeof v.why === 'string') {
      bridgeJson.lastWhy = v.why;
      bridgeJson.whyByMethod[method] = v.why;
      return null;
    }
    delete bridgeJson.whyByMethod[method];
    return v;
  };
  bridgeJson.fail = function (method, e) {
    const now = Date.now();
    if (now - (_bjErrAt.get(method) || 0) >= 30000) { _bjErrAt.set(method, now); console.error('rtx bridgeJson ' + method + ': ' + e); }
    bridgeJson.lastWhy = 'error';
    bridgeJson.whyByMethod[method] = 'error';
    return null;
  };
  bridgeJson.lastWhy = null;          // why of the most recent failed call, null after a success-free start
  bridgeJson.whyByMethod = Object.create(null);
  function bridgeWhyText(why) {
    switch (why) {
      case 'noclient':    return 'Waiting for the game client';
      case 'unsupported': return 'Launcher too old for this panel';
      case 'pending':     return 'Loading';
      case 'noargs':
      case 'error':       return 'Reader error, see log';
      default:            return '';
    }
  }
  function paneEmpty(el, method) {
    if (!el) return false;
    const why = bridgeJson.whyByMethod[method] || null;
    const text = bridgeWhyText(why);
    if (!text) return false;
    el.innerHTML = '';
    const d = document.createElement('div');
    d.className = 'empty';
    d.textContent = text;
    el.appendChild(d);
    return true;
  }
  function updateStatusStrip(st) {
    let el = document.getElementById('rtxStatusStrip');
    if (!el) {
      el = document.createElement('div');
      el.id = 'rtxStatusStrip';
      document.body.appendChild(el);
    }
    let text = '';
    if (st && st.ok === true) {
      if (!st.attached) { text = 'Waiting for the game client'; updateStatusStrip._stale = 0; }
      else if (st.reader === 'stale') {
        updateStatusStrip._stale = (updateStatusStrip._stale || 0) + 1;
        if (updateStatusStrip._stale >= 3) text = 'Game updated: reader offsets stale (build ' + (st.build || '?') + ')';
      } else updateStatusStrip._stale = 0;
    }
    if (el.textContent !== text) el.textContent = text;
    el.classList.toggle('on', !!text);
  }
  function rtxLog(...a) { console.log('rtx', ...a); }

