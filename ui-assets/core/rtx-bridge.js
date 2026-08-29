// rtx-bridge.js: Per-window pane roots (__paneRoot, paneRoot, $), CS2 switch maps, myPid, activeTab, bridge, bridgeJson, bridgeWhyText, paneEmpty, updateStatusStrip, rtxLog.
// Loads after: rtx-shim.js (the window.__rtxDevReload probe reads localStorage at load).
// Plain script, page globals by design: every top-level name here is a page global that panels and the other core files use.
  // ---- Per-window pane roots. Panels were written against ONE shared #content;
  // each floating window now owns a .content element, registered here by tab id.
  // During synchronous shell dispatch (renderPaneFor / per-window refresh tick),
  // __paneRoot points at the dispatching window's pane so the untouched panel
  // code's $('content') resolves to ITS window. Panels' own async callbacks use
  // paneRoot('<tab>') directly (see the window-manager notes).
  let __paneRoot = null;
  const __paneRoots = Object.create(null);   // tab id -> .content element
  function paneRoot(id) { return __paneRoots[id] || null; }
  const $ = id => (id === 'content' && __paneRoot) ? __paneRoot : document.getElementById(id);
  // ---- CS2 switch maps (rtx.cs2Switches -> %USERPROFILE%\RuneToolsX\cs2\switches.json) ----
  // The sidecar extraction bakes the game's OWN switch tables (script id -> {desc, keyKind,
  // varKind, entries}). Panels that carry a hand-transcribed copy of the same table adopt this
  // as the fresher source when it is present: the baked table stays as the fallback for an
  // install that never ran an extraction, and adoption is ADDITIVE (new entries appear, baked
  // ones are never removed), so a bad extraction cannot subtract data.
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
  // Dev hot-reload continuity: a reload sets __rtxDevReload; reopen the tab that was open.
  // Normal launches ignore the stored tab. Never restore into a tab hidden in this build (that
  // would render a panel with no rail entry) -- checked against TABS itself. plugin:* tabs
  // can't be validated here (pluginTabs loads async at boot): accept them now, and
  // loadPlugins() re-renders or falls back once the real list exists.
  try { if (window.__rtxDevReload) { const t = localStorage.getItem('rtxDevTab');
        if (t && (String(t).indexOf('plugin:') === 0 || (TABS.some(x => x.id === t) && (t !== 'dung' || DUNG_ENABLED)))) { activeTab = t; console.log('[devreload] restored tab ' + t); }
        else console.log('[devreload] restore rejected, stored=' + t);
      } } catch (e) { console.log('[devreload] restore threw: ' + e); }
  let lastSnap  = null;
  let host      = null;   // cached after first hostInfo() call

  function bridge() {
    return (typeof window.rtx === 'object') ? window.rtx : null;
  }
  // Call a bridge method and parse its JSON; null on any failure. One console.error per
  // method per 30 s so a dead bridge cannot flood the log.
  const _bjErrAt = new Map();
  // Failure envelope: a binding that answers {"ok":false,"why":"<why>"} (why in noclient,
  // noargs, pending, error, unsupported) is normalised to null here, and the why is kept in
  // bridgeJson.lastWhy / bridgeJson.whyByMethod[method] so a panel can tell "no client" from
  // "empty data". Objects without a string `why` (e.g. {"ok":false} from scanSolution) pass
  // through untouched, so existing callers see exactly what they always did.
  async function bridgeJson(method, ...args) {
    const b = bridge();
    if (!b || typeof b[method] !== 'function') {
      bridgeJson.lastWhy = b ? 'unsupported' : 'noclient';
      bridgeJson.whyByMethod[method] = bridgeJson.lastWhy;
      return null;
    }
    try {
      const v = JSON.parse(await b[method](...args));
      if (v && typeof v === 'object' && !Array.isArray(v) && v.ok === false && typeof v.why === 'string') {
        bridgeJson.lastWhy = v.why;
        bridgeJson.whyByMethod[method] = v.why;
        return null;
      }
      delete bridgeJson.whyByMethod[method];
      return v;
    }
    catch (e) {
      const now = Date.now();
      if (now - (_bjErrAt.get(method) || 0) >= 30000) { _bjErrAt.set(method, now); console.error('rtx bridgeJson ' + method + ': ' + e); }
      bridgeJson.lastWhy = 'error';
      bridgeJson.whyByMethod[method] = 'error';
      return null;
    }
  }
  bridgeJson.lastWhy = null;          // why of the most recent failed call, null after a success-free start
  bridgeJson.whyByMethod = Object.create(null);
  // Short user-facing text for a failure why (see bridgeJson).
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
  // Render the empty-state text for a pane whose bridgeJson(method) came back null. Panels opt
  // in by calling this instead of painting their own "no data" copy; returns true when a why
  // was known and rendered, false when the null was a plain empty result.
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
  // Thin status strip across the top of the client, driven once per refresh() tick from
  // bridgeStatus. Hidden while a client is attached and the reader is healthy; a "stale"
  // reader is only announced after three consecutive ticks so the unverified moment right
  // after attach never flashes it.
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

