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
  // ---- health check: the latest run, shared by the status strip, the toast, the menubar badge and
  // the Health Check panel. Polled from the strip refresh; nothing happens on a host without
  // healthLatest. Levels: red = a feature is broken, amber = moved values, format changes or rows
  // not verified after an update, green = everything passes, none = no run yet.
  const RTX_HEALTH = window.__rtxHealth = { seq: -1, run: null, impact: null, level: 'none', pending: null,
                                             running: false, trigger: '', at: 0, pendingKey: '' };
  const RTX_HEALTH_WORDS = { moved: 'moved', format: 'changed format', gone: 'missing', new: 'new', unverified: 'not verified',
                             fallback: 'working on a fallback', stale: 'stale tables', precondition: 'needs a game state',
                             unrecorded: 'not verified' };
  function rtxHealthWord(kind) { return RTX_HEALTH_WORDS[String(kind || '').toLowerCase()] || ''; }
  function rtxHealthPanel(feature) { return String(feature || '').split(': ')[0]; }
  // The run's impact section; derived from its rows when the run does not carry one.
  function rtxHealthImpact(run) {
    if (!run) return null;
    if (run.impact && typeof run.impact === 'object') return run.impact;
    const rows = Array.isArray(run.checks) ? run.checks : [];
    const groups = { broken: {}, moved: {}, format: {}, unverified: {} };
    let ok = 0;
    for (const r of rows) {
      let kind = String(r.kind || '').toLowerCase();
      if (!kind) { const m = /^(MOVED|FORMAT|GONE|NEW|UNVERIFIED)\b/.exec(String(r.d || '')); kind = m ? m[1].toLowerCase() : ''; }
      let bucket = null;
      if (r.ok === 0) bucket = 'broken';
      else if (r.ok === 2) bucket = kind === 'format' ? 'format' : 'moved';
      else if (r.ok === 3) bucket = 'unverified';
      else { ok++; continue; }
      // an unchecked row is a precondition unless the run says the check itself could not be made
      const pre = bucket === 'unverified' && !(kind === 'unverified' || kind === 'unrecorded' || kind === 'gone');
      const feats = (Array.isArray(r.f) && r.f.length) ? r.f : ['Other'];
      for (const f of feats) {
        const name = rtxHealthPanel(f);
        const e = groups[bucket][name] || (groups[bucket][name] = { feature: name, why: r.d || '', rows: [], need: r.need || '', precondition: pre });
        const id = r.id || r.k;
        if (e.rows.indexOf(id) < 0) e.rows.push(id);
        if (!pre) e.precondition = false;
        if (!e.need && r.need) e.need = r.need;
      }
    }
    const b = run.build || {}, bg = rows.find(r => r.id === 'build.game'), bd = String((bg && bg.d) || '');
    const ctx = run.context || {};
    return { build: { exe: b.known ? 'same' : 'unvalidated', cache: /cache changed/.test(bd) ? 'changed' : 'same', from: '',
                      to: ((run.version || '') + ' ' + (b.stamp || '')).trim(), archives: [] },
             complete: !!(ctx.complete != null ? ctx.complete : ctx.inWorld), trigger: ctx.trigger || '',
             broken: Object.values(groups.broken), moved: Object.values(groups.moved), format: Object.values(groups.format),
             unverified: Object.values(groups.unverified), ok, derived: true };
  }
  function rtxHealthLevel(impact) {
    if (!impact) return 'none';
    if ((impact.broken || []).length) return 'red';
    const unv = (impact.unverified || []).filter(e => !e.precondition);
    if ((impact.moved || []).length || (impact.format || []).length || unv.length) return 'amber';
    return 'green';
  }
  function rtxHealthNames(list, max) {
    const n = (list || []).map(e => e.feature);
    if (n.length <= max) return n.join(', ');
    return n.slice(0, max).join(', ') + ' and ' + (n.length - max) + ' more';
  }
  function rtxHealthOpen() {
    try {
      const t = (typeof allTabs === 'function' ? allTabs() : TABS).find(x => x.id === 'health');
      if (t && typeof openTab === 'function') openTab(t);
    } catch (e) {}
  }
  // The notice for a finished run, in the words a player reads.
  function rtxHealthToast(h) {
    const run = h.run, im = h.impact;
    if (!run || !im || typeof uiNotify !== 'function') return;
    const to = (im.build && im.build.to) || run.version || '';
    const cacheOnly = h.trigger === 'cache update' || (im.build && im.build.exe === 'same' && im.build.cache === 'changed');
    const head = cacheOnly ? 'Game content updated' : ('Game updated' + (to ? ' to ' + to : ''));
    const s = run.summary || {}, done = (s.pass | 0) + (s.fail | 0) + (s.warn | 0), total = done + (s.unchecked | 0);
    const unv = (im.unverified || []).filter(e => !e.precondition).length;
    let sub, level = h.level;
    if (im.complete === false && level !== 'red') { sub = 'Log in so RuneTools can finish checking (' + done + ' of ' + total + ' done).'; level = 'amber'; }
    else if (level === 'red') sub = (im.broken.length === 1 ? '1 feature needs' : im.broken.length + ' features need') + ' a RuneTools update: ' + rtxHealthNames(im.broken, 4) + '. Everything else works.';
    else if ((im.format || []).length && cacheOnly) sub = im.format.length + ' format change' + (im.format.length === 1 ? '' : 's') + ': ' + rtxHealthNames(im.format, 3) + ' may show less.';
    else if (level === 'amber') sub = 'Everything works; ' + (im.moved || []).length + ' feature' + ((im.moved || []).length === 1 ? ' runs' : 's run') + ' with a warning and ' + unv + ' could not be verified yet.';
    else sub = 'Everything RuneTools reads still works (' + total + ' checks).';
    const key = 'health ' + head + ' ' + sub;
    const opts = { title: head, sub, foot: level === 'green' ? '' : 'Open Health check', onClick: rtxHealthOpen,
                   accent: level === 'red' ? '#e05656' : level === 'amber' ? '#e0b457' : '#4dd28a' };
    if (level === 'green') opts.ttl = 8000; else opts.sticky = true;
    uiNotify(key, opts);
  }
  async function rtxHealthPoll() {
    const b = bridge();
    if (!b || typeof b.healthLatest !== 'function') return;
    const now = Date.now();
    if (now - RTX_HEALTH.at < 3000) return;
    RTX_HEALTH.at = now;
    let d = null;
    try { d = JSON.parse(await b.healthLatest(RTX_HEALTH.seq)); } catch (e) { return; }
    if (!d || typeof d !== 'object') return;
    const was = RTX_HEALTH.level;
    RTX_HEALTH.running = !!d.running;
    RTX_HEALTH.pending = d.pending || null;
    RTX_HEALTH.trigger = d.trigger || '';
    if (d.run && d.seq !== RTX_HEALTH.seq) {
      RTX_HEALTH.seq = d.seq;
      RTX_HEALTH.run = d.run;
      RTX_HEALTH.impact = rtxHealthImpact(d.run);
      RTX_HEALTH.level = rtxHealthLevel(RTX_HEALTH.impact);
      // a run this page did not ask for is announced once per run, across page reloads
      let seen = '';
      try { seen = localStorage.getItem('rtxHealthToasted') || ''; } catch (e) {}
      const s = d.run.summary || {};
      const id = d.seq + ':' + (d.run.version || '') + ':' + (s.pass | 0) + ':' + (s.fail | 0) + ':' + (s.ms | 0);
      if (RTX_HEALTH.trigger !== 'manual' && seen !== id) {
        try { localStorage.setItem('rtxHealthToasted', id); } catch (e) {}
        rtxHealthToast(RTX_HEALTH);
      }
    }
    if (RTX_HEALTH.pending && !RTX_HEALTH.running && typeof uiNotify === 'function') {
      const p = RTX_HEALTH.pending, key = p.trigger + '|' + p.why;
      if (RTX_HEALTH.pendingKey !== key) {
        RTX_HEALTH.pendingKey = key;
        uiNotify('health pending ' + key, { title: p.trigger === 'cache update' ? 'Game content updated' : 'Game updated' + (p.version ? ' to ' + p.version : ''),
                                              sub: 'Checking what still works once you are in game...', ttl: 8000 });
      }
    }
    if (was !== RTX_HEALTH.level && typeof renderMenubar === 'function') { try { renderMenubar(); } catch (e) {} }
  }

  function updateStatusStrip(st) {
    let el = document.getElementById('rtxStatusStrip');
    if (!el) {
      el = document.createElement('div');
      el.id = 'rtxStatusStrip';
      el.addEventListener('click', rtxHealthOpen);
      document.body.appendChild(el);
    }
    rtxHealthPoll();
    let text = '', click = false;
    const h = RTX_HEALTH;
    if (h.level === 'red' && h.impact) {
      text = 'Game updated: ' + rtxHealthNames(h.impact.broken, 2) + ' affected. Open Health check';
      click = true;
    } else if (h.running && h.trigger !== 'manual') {
      text = 'Game updated: checking what still works...';
    } else if (h.pending && !h.run) {
      text = 'Game updated' + (h.pending.version ? ' to ' + h.pending.version : '') + ': the check runs once you are in game';
    } else if (st && st.ok === true) {
      // no run to go by: the one-byte status heuristic
      if (!st.attached) { text = 'Waiting for the game client'; updateStatusStrip._stale = 0; }
      else if (st.reader === 'stale') {
        updateStatusStrip._stale = (updateStatusStrip._stale || 0) + 1;
        if (updateStatusStrip._stale >= 3) text = 'Game updated: reader offsets stale (build ' + (st.build || '?') + ')';
      } else updateStatusStrip._stale = 0;
    }
    if (el.textContent !== text) el.textContent = text;
    el.classList.toggle('on', !!text);
    el.style.pointerEvents = click ? 'auto' : '';
    el.style.cursor = click ? 'pointer' : '';
  }
  function rtxLog(...a) { console.log('rtx', ...a); }

