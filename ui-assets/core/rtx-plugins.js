  let   pluginTabs    = [];     // [{ id, manifest:{id,name,version,author,entry,description}, scopes:[] }]
  const pluginMounts  = new Map();   // plugin id -> { id, frame, scopes }; one per open plugin window
  const pluginSettingsReg = new Map();   // plugin id -> { name, schema, values }
  function pluginSettingsSchema(raw) {
    if (!Array.isArray(raw)) return null;
    const out = [];
    for (const c of raw.slice(0, 24)) {
      if (!c || typeof c !== 'object') continue;
      const key = String(c.key || '');
      if (!/^[A-Za-z0-9_.-]{1,32}$/.test(key)) continue;
      const type = String(c.type || '');
      if (['toggle', 'select', 'slider', 'text'].indexOf(type) < 0) continue;
      const ctl = { key, type, label: pClampStr(c.label || key, 48), hint: pClampStr(c.hint || '', 120) };
      if (type === 'select') {
        ctl.options = (Array.isArray(c.options) ? c.options : []).slice(0, 12)
          .map(o => ({ v: pClampStr(o && o.v, 40), label: pClampStr((o && o.label) || (o && o.v), 32) }))
          .filter(o => o.v !== '');
        if (!ctl.options.length) continue;
      }
      if (type === 'slider') {
        ctl.min = pClampNum(c.min, -1e6, 1e6); ctl.max = pClampNum(c.max, -1e6, 1e6);
        if (!(ctl.max > ctl.min)) continue;
        ctl.step = pClampNum(c.step || 1, 1e-3, ctl.max - ctl.min);
      }
      ctl.def = pluginSettingClamp(ctl, c.default);
      out.push(ctl);
    }
    return out.length ? out : null;
  }
  function pluginSettingClamp(ctl, v) {
    if (ctl.type === 'toggle') return !!v;
    if (ctl.type === 'slider') return pClampNum(v, ctl.min, ctl.max);
    if (ctl.type === 'select') return ctl.options.some(o => o.v === v) ? v : ctl.options[0].v;
    return pClampStr(v == null ? '' : v, 200);
  }
  function pluginSettingsSet(id, key, v) {
    const reg = pluginSettingsReg.get(id);
    if (!reg) return;
    const ctl = reg.schema.find(c => c.key === key);
    if (!ctl) return;
    reg.values[key] = pluginSettingClamp(ctl, v);
    try { bridge().pluginStoreSave(myPid(), id, '~settings', JSON.stringify(reg.values)); } catch (e) {}
    const m = pluginMounts.get(id);
    if (m) pluginSendEvent(m, 'settings', Object.assign({}, reg.values));
  }
  // keepHolder: the holder stays, for a stopped plugin whose window shows why it stopped
  function pluginUnmount(id, keepHolder) {
    const m = pluginMounts.get(id);
    if (m && m.lua) { try { bridge().luaUnload(id); } catch (e) {} }
    if (m && m.kbFocus) kbGrab(false);
    pluginMounts.delete(id);
    if (!keepHolder && typeof pluginHolderDrop === 'function') pluginHolderDrop(id);
    if (typeof ovPointAt === 'function') { try { ovPointAt('plugin:' + id, '', ''); } catch (e) {} }   // its arrow goes with it
  }

  // A frame plugin that failed to load or broke a rule stays stopped, with the reason in its window, until
  // its window is opened again, it is reinstalled or its developer copy changes. Mounting it again on the
  // next refresh would only repeat the failure four times a second.
  const pluginFailed = new Map();   // plugin id -> the reason shown in place of the plugin
  function pluginFail(id, why) {
    pluginFailed.set(id, why);
    const h = pluginHolders.get(id);
    pluginUnmount(id, true);
    if (h) pluginFailShow(h, why);
  }
  function pluginFailShow(h, why) {
    const cur = h.el.firstElementChild;
    if (cur && cur.dataset && cur.dataset.pluginFail && cur.textContent === why) return;   // build-once
    h.el.innerHTML = '';
    const d = document.createElement('div'); d.className = 'empty'; d.dataset.pluginFail = '1'; d.textContent = why;
    h.el.appendChild(d);
  }

  // A frame asks for the game keyboard while one of its fields has focus (kbWant). The host holds it for the
  // frame only while that frame has focus in a window that is showing. The frame never learns of a click in
  // the game or of its window being minimized, so this is checked again on every focus change and each tick:
  // the keyboard is taken back when that no longer holds, and given again when the frame has focus again.
  function pluginKbAllowed(m) {
    if (!m.frame || document.activeElement !== m.frame) return false;
    const h = pluginHolders.get(m.id);
    return !!(h && !h.el.hidden && !pluginInBackground(m.id) && paneVisible('plugin:' + m.id));
  }
  function pluginKbSync() {
    for (const m of pluginMounts.values()) {
      const on = !!m.kbWant && pluginKbAllowed(m);
      if (on !== !!m.kbFocus) { m.kbFocus = on; kbGrab(on); }
    }
  }

  function pluginSendEvent(m, event, data) {
    if (m && m.lua) { luaSendEvent(m, event, data); return; }
    if (!m || !m.frame || !m.frame.contentWindow) return;
    try { m.frame.contentWindow.postMessage({ __rtxPlugin: PLUGIN_PROTO, kind: 'event', event, data }, '*'); } catch (e) {}
  }

  const pluginEventQueue = [];
  function pluginPush() {
    const batch = pluginEventQueue.length ? pluginEventQueue.splice(0) : null;
    try { pluginKbSync(); } catch (e) {}   // the periodic pass over the keyboard claims
    // 'state' is the changed snapshot: serialised once per tick for every plugin, and handed to each only when it
    // differs from what that plugin last got (a newly mounted one gets it at once).
    let snapJson = '';
    if (lastSnap && pluginMounts.size) { try { snapJson = JSON.stringify(lastSnap); } catch (e) { snapJson = ''; } }
    for (const m of pluginMounts.values()) {
      if (m.lua) { luaTickPlugin(m, batch, snapJson); continue; }   // the Lua runtime gets tick, events and state in one call
      if (!m.frame) continue;
      pluginSendEvent(m, 'tick', null);
      if (m.scopes.indexOf('state.read') !== -1) {
        if (snapJson && m.stateSent !== snapJson) { m.stateSent = snapJson; pluginSendEvent(m, 'state', lastSnap); }
        if (batch) pluginSendEvent(m, 'events', batch);
      }
    }
  }

  function pluginBrokerInit() {
    rtxEvents.on('*', function (ev) { if (pluginMounts.size && pluginEventQueue.length < 4096) pluginEventQueue.push(ev); });
    // focus leaving a frame (a click in the game blurs it) or entering one: check the keyboard claims at once
    const kbSoon = () => setTimeout(pluginKbSync, 0);
    document.addEventListener('focusin', kbSoon);
    document.addEventListener('focusout', kbSoon);
    window.addEventListener('blur', kbSoon);
    window.addEventListener('message', async (ev) => {
      const m = ev.data;
      if (!m || m.__rtxPlugin !== PLUGIN_PROTO) return;
      let mounted = null;
      for (const pm of pluginMounts.values())
        if (pm.frame && ev.source === pm.frame.contentWindow) { mounted = pm; break; }
      if (!mounted) return;
      if (ev.origin !== 'null') return;
      if (m.kind === 'hello') {
        pluginSendEvent(mounted, 'ready', { scopes: mounted.scopes, apiVersion: '1.0', pluginId: mounted.id });
        return;
      }
      if (m.kind === 'kb') {
        // Taking the game keyboard needs the frame to hold focus in a window that is showing (pluginKbSync);
        // letting go is always allowed.
        mounted.kbWant = !!m.on;
        pluginKbSync();
        return;
      }
      if (m.kind !== 'call') return;
      const reply = (ok, payload) => {
        try {
          mounted.frame.contentWindow.postMessage({
            __rtxPlugin: PLUGIN_PROTO, kind: 'reply', id: m.id, ok,
            result: ok ? payload : undefined, error: ok ? undefined : String(payload)
          }, '*');
        } catch (e) {}
      };
      try {
        // A method is a plain name the table owns: anything else (an array, 'constructor') would find an entry
        // while missing every per-method rate limit.
        if (typeof m.method !== 'string' || !Object.prototype.hasOwnProperty.call(PLUGIN_API, m.method)) return reply(false, 'unknown method');
        const def = PLUGIN_API[m.method];
        if (def.scope && mounted.scopes.indexOf(def.scope) === -1) return reply(false, 'scope not granted: ' + def.scope);
        if (!pluginRateOk(mounted.id, m.method)) return reply(false, 'rate limited');
        if (!bridge()) return reply(false, 'host unavailable');
        const args = Array.isArray(m.args) ? m.args : [];
        const raw = await def.run(args, myPid(), mounted.id, mounted.frame);
        let result = raw;
        if (def.json === true) { try { result = JSON.parse(raw); } catch (e) { result = null; } }
        else if (def.json === 'maybe') { try { result = raw ? JSON.parse(raw) : null; } catch (e) { result = null; } }
        reply(true, result);
      } catch (e) { reply(false, 'call failed'); }
    });
  }

  // Grants are a per-account store (core/rtx-boot.js): the launcher reads and writes the file of the account
  // behind this client, whatever kind of client it is, and a save is refused (the grants read again) once
  // that file holds anything other than what was read, so one account's grants never go over another's.
  let pluginGrants = Object.create(null);   // plugin id -> granted scope array
  let pluginGrantsSt = null;                // made on first use: the store helpers come from core/rtx-boot.js, spliced later
  let _pgSaveT = 0, _pgReadAt = 0;
  function pluginAcctKey() { return (lastSnap && lastSnap.display_name) || ''; }
  function pluginGrantsStore() {
    return pluginGrantsSt || (pluginGrantsSt = acctStore(() => bridge().pluginGrantsLoad(myPid()),
                                                         t => bridge().pluginGrantsSave(myPid(), t), pluginGrantsReload));
  }
  // Take the grants from the file of the account this client has now. An empty answer (no account yet, or a
  // file that could not be read) holds nothing, and leaves the store unread so no save goes over that file.
  function pluginGrantsRead() {
    const st = pluginGrantsStore();
    const s = acctStoreRead(st);
    if (!s) { st.base = null; return false; }
    let v = null;
    try { v = JSON.parse(s); } catch (e) {}
    const next = Object.create(null);
    if (v && typeof v === 'object') for (const k in v) if (Array.isArray(v[k])) next[k] = v[k];
    pluginGrants = next;
    return true;
  }
  // Every running plugin (showing, behind another tab or off-screen) ran on consent the grants just read may
  // not hold. Showing panes mount again on the new grants, hidden tabs when shown, and the sync below starts
  // only the background plugins the new grants cover.
  function pluginGrantsRestart() {
    for (const id of Array.from(pluginMounts.keys())) pluginUnmount(id);
    for (const w of wm.wins.values())
      if (String(w.tab).indexOf('plugin:') === 0) renderPaneFor(w);
    try { pluginBackgroundSync(); } catch (e) {}
  }
  // Plugins may start now (the grants were read, or installed plugins were let through). The refresh pass
  // draws only windows that are open, and a rolled-up window is not drawn again until it is unrolled, so
  // every plugin window that is not minimized is drawn here, which mounts its plugin, then the background
  // plugins start.
  function pluginWindowsRender() {
    for (const w of Array.from(wm.wins.values()))
      if (!w.min && String(w.tab).indexOf('plugin:') === 0) { try { renderPaneFor(w); } catch (e) {} }
    try { pluginBackgroundSync(); } catch (e) {}
  }
  // The account behind this client changed (or became known) since the grants were read. A pending save holds
  // the last account's grants, so it goes too.
  function pluginGrantsReload() {
    clearTimeout(_pgSaveT); _pgSaveT = 0;
    if (!pluginGrantsRead()) pluginGrants = Object.create(null);
    pluginGrantsRestart();
  }
  // The first read, and the first after a reload that could not read the file; while it comes back empty it
  // is tried again every couple of seconds. Later changes of account come in through the store check, which
  // calls pluginGrantsReload.
  function pluginGrantsEnsure() {
    if (!bridge() || !myPid() || (pluginGrantsSt && pluginGrantsSt.base !== null)) return;
    const now = Date.now();
    if (now < _pgReadAt) return;
    _pgReadAt = now + 2000;
    const early = pluginMounts.size > 0;    // enabled before the account was known: that consent is not kept
    if (!pluginGrantsRead()) return;
    if (early) pluginGrantsRestart();
    else pluginWindowsRender();
  }
  function pluginGrantsSaveSoon(delay) {
    if (_pgSaveT) return;
    _pgSaveT = setTimeout(() => {
      _pgSaveT = 0;
      let ok = false;
      try { ok = acctStoreWrite(pluginGrantsStore(), JSON.stringify(pluginGrants)); } catch (e) {}
      // Saved, or refused: a file never read or since changed is not written. A failed write (at the login
      // screen there is no account to write for yet) is tried again until it is saved or refused, however
      // long the login takes, rather than dropped after a few tries.
      if (ok === false) pluginGrantsSaveSoon(2000);
    }, delay || 400);
  }
  function pluginGetGranted(id) { return pluginGrants[id] || null; }
  function pluginSetGranted(id, scopes) {
    pluginGrants[id] = scopes || [];
    pluginGrantsSaveSoon();
    try { pluginBackgroundSync(); } catch (e) {}
  }
  function pluginClearGranted(id) {
    delete pluginGrants[id];
    pluginFailed.delete(id);                // a reinstall tries a stopped plugin again
    pluginGrantsSaveSoon();
    if (pluginInBackground(id)) pluginUnmount(id);
  }
  function pluginGrantCovers(granted, want) {
    if (!Array.isArray(granted)) return false;
    for (const s of (want || [])) if (granted.indexOf(s) === -1) return false;
    return true;
  }

  // ---- background plugins: manifest "background": true keeps a granted plugin running with no window open ----
  // Its frame lives in an off-screen host and gets the same ticks, state and events as a windowed one. Opening
  // its window mounts it there instead (a fresh frame: an iframe cannot be moved without reloading), and
  // closing the window sends it back off-screen. Revoking its scopes stops it.
  function pluginBgHost() {
    let el = document.getElementById('pluginBg');
    if (!el) {
      el = document.createElement('div'); el.id = 'pluginBg';
      el.style.cssText = 'position:absolute;left:-4000px;top:0;width:800px;height:600px;overflow:hidden;pointer-events:none;opacity:0;';
      document.body.appendChild(el);
    }
    return el;
  }
  function pluginIsBackground(id) { const t = pluginTabs.find(p => p.id === id); return !!(t && t.manifest && t.manifest.background); }
  function pluginInBackground(id) { const h = pluginHolders.get(id); return !!(h && h.el.parentNode && h.el.parentNode.id === 'pluginBg'); }
  function pluginBackgroundSync() {
    for (const tab of pluginTabs) {
      if (!tab.manifest.background) continue;
      const id = tab.id;
      const granted = pluginGetGranted(id);
      if (!granted || !pluginGrantCovers(granted, tab.scopes)) continue;   // not enabled: nothing runs
      if (pluginMounts.get(id) || pluginFailed.has(id)) continue;          // already running, windowed or not; or stopped
      const w = wmWinOf('plugin:' + id);
      if (w && !w.min && w.tab === 'plugin:' + id) continue;              // showing in a window: that mounts it
      const h = pluginHolderFor(id, null);
      if (h.el.parentNode !== pluginBgHost()) pluginBgHost().appendChild(h.el);
      h.el.hidden = false; h.wid = null;
      pluginMountInto(id, tab, granted, h);
    }
  }
  // A plugin window closed: a background plugin restarts off-screen, any other plugin stops. One that was
  // stopped for a failure is tried again from here on.
  function pluginRelease(id) {
    pluginFailed.delete(id);
    pluginUnmount(id);
    // the window manager calls this before it drops the tab or window: sync once that has happened
    setTimeout(() => { try { pluginBackgroundSync(); } catch (e) {} }, 0);
  }

  // ---- revoked plugins: the launcher checks the signed revocation list when it starts and hourly after ----
  // It deletes a revoked installed plugin and refuses to load it again, but a copy this page already runs
  // keeps running, so the page asks here: any running copy of an id newly on the list is stopped (frame or
  // Lua state, in a window or off-screen) and the plugin list read again. Installed plugins wait for the
  // first check of this run, so one revoked at startup never gets to run before its folder is gone.
  let pluginRevokeChecked = false;   // the first check has finished (or this launcher has none)
  let _pluginRevokeSeen = 0;         // entries of the launcher's list already acted on (it only grows)
  let _pluginRevokeBusy = false;
  async function pluginRevokedPoll() {
    const b = bridge();
    if (!b || _pluginRevokeBusy) return;
    let r = { checked: true, removed: [] };
    if (typeof b.pluginRevoked === 'function') {
      try { r = JSON.parse(b.pluginRevoked()); } catch (e) { return; }
      if (!r || typeof r !== 'object') return;
    }
    const removed = Array.isArray(r.removed) ? r.removed : [];
    const fresh = removed.slice(_pluginRevokeSeen);
    _pluginRevokeSeen = Math.max(_pluginRevokeSeen, removed.length);
    for (const id of fresh) {
      if (typeof id !== 'string' || !pluginMounts.has(id)) continue;
      try { pluginUnmount(id); } catch (e) {}
      try { rtx.log && rtx.log('plugin ' + id + ' unmounted: revoked'); } catch (e) {}
    }
    const release = !pluginRevokeChecked && r.checked === true;
    if (release) pluginRevokeChecked = true;
    if (!fresh.length && !release) return;
    _pluginRevokeBusy = true;
    try {
      if (fresh.length) await loadPlugins();   // drops revoked tabs and their windows, then starts the rest
      else pluginWindowsRender();              // held windows, rolled-up ones too, then background plugins
    } catch (e) {}
    finally { _pluginRevokeBusy = false; }
  }
  setInterval(() => { pluginRevokedPoll().catch(() => {}); }, 1000);

  const PLUGIN_TAB_ICON = '<rect x="4" y="4" width="7" height="7" rx="1"/><rect x="13" y="4" width="7" height="7" rx="1"/><rect x="4" y="13" width="7" height="7" rx="1"/><path d="M13.5 17h6.5M16.75 13.75v6.5"/>';
  const PLUGIN_BROWSE_ICON = '<circle cx="11" cy="11" r="7"/><path d="M20.5 20.5l-4.2-4.2"/><path d="M11 8v6M8 11h6"/>';
  // Group Bank only applies to group-ironman accounts: varp 4818 bit 19 (varbit 56612,
  let gimInGroup = null;
  setInterval(async () => {
    try {
      if (!bridge() || !bridge().varps || !myPid()) return;
      const amVp = typeof VP !== 'undefined' ? VP.ACCOUNT_MODE : 4818;
      const vp = JSON.parse(await bridge().varps(myPid(), String(amVp))) || {};
      if (vp[amVp] === undefined) return;
      const inG = ((vp[amVp] >> 19) & 1) === 1;
      if (inG !== gimInGroup) { gimInGroup = inG; try { renderSidebar(); } catch (e) {} }
    } catch (e) {}
  }, 10000);
  function allTabs() {
    const extra = [{ id: 'pluginbrowse', label: 'Browse plugins', cat: 'Plugins', icon: PLUGIN_BROWSE_ICON }];
    pluginTabs.forEach(p => extra.push({ id: 'plugin:' + p.id, label: p.manifest.name, ver: p.manifest.version || '', cat: 'Plugins', icon: PLUGIN_TAB_ICON }));
    if (typeof auraGroupTabs === 'function') { try { auraGroupTabs().forEach(t => extra.push(t)); } catch (e) {} }
    return TABS.filter(t => (t.id !== 'dung' || DUNG_ENABLED) && (t.id !== 'groupbank' || gimInGroup !== false))
      .map(t => { const g = TAB_GROUP_OF[t.id]; return (g && g.cat && g.cat !== t.cat) ? Object.assign({}, t, { cat: g.cat }) : t; })
      .concat(extra);
  }

  const PLUGIN_THEME_VARS = ['accent', 'accent-hi', 'accent-lo', 'accent-rgb', 'accent-ring',
                             'bg', 'bg-elev', 'bg-elev-2', 'panel', 'panel-2', 'win-bg',
                             'border', 'border-hi', 'text', 'text-dim', 'text-mute',
                             'ok', 'warn', 'err', 'font-ui', 'font-size'];
  function pluginThemeVars() {
    const cs = getComputedStyle(document.documentElement);
    const out = {};
    for (const n of PLUGIN_THEME_VARS) {
      const v = (cs.getPropertyValue('--' + n) || '').trim();
      if (v) out['--rtx-' + n] = v;
    }
    return out;
  }
  function pluginThemeCss() {
    const v = pluginThemeVars();
    let css = ':root{';
    for (const k in v) css += k + ':' + v[k] + ';';
    css += '}';
    css += 'html,body{margin:0;background:var(--rtx-bg,#14151c);color:var(--rtx-text,#e8e8ef);' +
           'font-family:var(--rtx-font-ui,-apple-system,"Segoe UI",Roboto,sans-serif);' +
           'font-size:var(--rtx-font-size,13px);}';
    return css;
  }
  function pluginThemeBroadcast() {
    if (!pluginMounts.size) return;
    const vars = pluginThemeVars();
    for (const m of pluginMounts.values()) pluginSendEvent(m, 'theme', vars);
  }

  function pluginBuildSrcdoc(entryHtml) {
    const csp = '<meta http-equiv="Content-Security-Policy" content="' +
      "default-src 'none'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; " +
      "img-src 'self' data:; font-src 'self' data:; connect-src 'none'; frame-src 'none'; " +
      "form-action 'none'; base-uri 'none'" + '">';
    const inject = csp + '<style>' + pluginThemeCss() + '</style>' +
                   '<scr' + 'ipt>' + PLUGIN_SDK_SHIM + '</scr' + 'ipt>';
    let headHtml = '', bodyHtml = '';
    try {
      const doc = new DOMParser().parseFromString(String(entryHtml || ''), 'text/html');
      headHtml = doc.head ? doc.head.innerHTML : '';
      bodyHtml = doc.body ? doc.body.innerHTML : '';
    } catch (e) { headHtml = ''; bodyHtml = ''; }   // fail CLOSED: no author markup without the CSP
    return '<!DOCTYPE html><html><head>' + inject + headHtml + '</head><body>' + bodyHtml + '</body></html>';
  }

  function renderPluginPermission(id, tab, prev) {
    const c = $('content'); c.innerHTML = '';
    const m = tab.manifest;
    const isUpdate = Array.isArray(prev) && prev.length > 0;
    const card = document.createElement('div');
    card.dataset.pluginPerm = id;
    card.style.cssText = 'max-width:420px;margin:24px auto;background:#1a1b23;border:1px solid rgba(255,255,255,.08);border-radius:12px;padding:20px;';
    const labels = { 'state.read': 'Read your live game state', 'cache.read': 'Read game cache data (items, sprites, enums)', 'overlay': 'Draw overlays on the game', 'sound': 'Play alert sounds', 'storage': 'Store its own settings', 'notify.os': 'Show Windows notifications', 'notify.discord': 'Post messages to your Discord webhook (it never sees the URL)', 'clipboard': 'Copy text to your clipboard', 'clipboard.read': 'Read your clipboard contents', 'telemetry': 'Write log files to its own folder (RuneToolsX\plugin-logs)' };
    let h = '<div style="font-size:1.1em;font-weight:600;">' + pluginEsc(m.name) + '</div>';
    h += '<div style="color:#8b8b9e;font-size:.85em;margin:2px 0 6px;">v' + pluginEsc(m.version) + (m.author ? (' - ' + pluginEsc(m.author)) : '') + '</div>';
    if (tab.source === 'dev')
        h += '<div style="color:#e0b000;font-size:.8em;margin-bottom:12px;">Unsigned developer plugin - only enable plugins you trust.</div>';
    else
        h += '<div style="color:#34d399;font-size:.8em;margin-bottom:12px;">Verified - signed by RuneTools.</div>';
    if (m.description) h += '<div style="font-size:.9em;margin-bottom:12px;">' + pluginEsc(m.description) + '</div>';
    if (m.runtime === 'lua')
      h += '<div style="color:#8b8b9e;font-size:.8em;margin:-6px 0 12px;">Lua plugin: runs in the built-in Lua runtime with no web content, and reaches the game only through the access listed below.</div>';
    if (isUpdate)
      h += '<div style="color:#e0b000;font-size:.82em;margin-bottom:10px;">This plugin now asks for more access than you allowed. Review the new items before enabling it.</div>';
    h += '<div style="font-size:.82em;color:#8b8b9e;margin-bottom:6px;">This plugin can:</div><ul style="margin:0 0 14px 18px;font-size:.88em;line-height:1.6;">';
    (tab.scopes.length ? tab.scopes : ['(no special access)']).forEach(s => {
      const isNew = isUpdate && prev.indexOf(s) === -1;
      h += '<li' + (isNew ? ' style="color:#e0b000;font-weight:600;"' : '') + '>' + pluginEsc(labels[s] || s) +
           (isNew ? ' <span style="font-weight:400;font-size:.85em;">(new)</span>' : '') + '</li>';
    });
    h += '</ul>';
    { const acct = pluginAcctKey();
      h += '<div style="font-size:.78em;color:#8b8b9e;margin:-6px 0 12px;">Applies to ' +
           (acct ? pluginEsc(acct) : 'this character') + ' only.</div>'; }
    card.innerHTML = h;
    const row = document.createElement('div');
    row.style.cssText = 'display:flex;gap:8px;';
    const btn = document.createElement('button');
    btn.textContent = 'Enable plugin';
    btn.style.cssText = 'background:var(--accent-hi);color:#fff;border:none;border-radius:8px;padding:9px 18px;font-weight:600;cursor:pointer;';
    btn.addEventListener('click', () => {
      pluginSetGranted(id, tab.scopes);
      const w = wmWinOf('plugin:' + id);
      if (w && !w.min && w.tab === ('plugin:' + id)) renderPaneFor(w);
    });
    const deny = document.createElement('button');
    deny.textContent = 'Deny';
    deny.style.cssText = 'background:rgba(255,255,255,.06);color:#f0f0f5;border:1px solid rgba(255,255,255,.1);border-radius:8px;padding:9px 18px;font-weight:600;cursor:pointer;';
    deny.addEventListener('click', () => {
      pluginClearGranted(id);
      wmCloseTabId('plugin:' + id);
      const bt = allTabs().find(x => x.id === 'pluginbrowse');
      if (bt) openTab(bt);
    });
    row.appendChild(btn); row.appendChild(deny);
    card.appendChild(row);
    c.appendChild(card);
  }

  // ---- plugin holders: a mounted plugin lives in a layer over the window body, not in the pane ----
  // The pane is wiped on every tab switch (wmMountTab). An iframe removed from the DOM is destroyed
  // and re-parenting one reloads it, so a plugin that shares a window with other tabs would lose its
  // state, and its overlay (tree timers, guide tiles) would freeze, whenever another tab was in
  // front. Holders keep the frame alive and merely hide it while another tab shows; ticks, state
  // and events keep flowing so overlays stay current. A holder is removed only on unmount.
  const pluginHolders = new Map();   // plugin id -> { el, wid }
  let _pluginHoldCss = false;
  function pluginHolderFor(id, w) {
    if (!_pluginHoldCss) {
      _pluginHoldCss = true;
      try { injectStyle('pluginHoldCss',
        '.win-body { position: relative; }' +
        '.plugin-host { position: absolute; inset: 0; display: flex; flex-direction: column; min-height: 0; min-width: 0; padding: 14px 16px; overflow: hidden; background: var(--win-bg, transparent); }' +
        '.plugin-host[hidden] { display: none; }'); } catch (e) {}
    }
    let h = pluginHolders.get(id);
    if (!h) {
      h = { el: document.createElement('div'), wid: null };
      h.el.className = 'plugin-host'; h.el.dataset.pluginHost = id;
      pluginHolders.set(id, h);
    }
    const body = w && w.body;
    if (body && h.el.parentNode !== body) {   // moving between windows reloads a frame; same window never does
      const pm = pluginMounts.get(id);
      // that reload is its own page again, not a navigation; and the fresh page never takes back a keyboard
      // claim the old page made, so that claim ends here
      if (pm) { pm.frLoads = 0; pm.kbWant = false; }
      body.appendChild(h.el); h.wid = w.wid;
    }
    return h;
  }
  // Called by wmMountTab: show the holder of the tab now in front, hide every other holder in that window.
  function pluginHoldersSync(w) {
    if (!w || !w.body) return;
    for (const [id, h] of pluginHolders) {
      if (h.el.parentNode !== w.body) continue;
      h.el.hidden = (w.tab !== 'plugin:' + id);
    }
  }
  function pluginHolderDrop(id) {
    const h = pluginHolders.get(id);
    if (!h) return;
    pluginHolders.delete(id);
    try { h.el.remove(); } catch (e) {}
  }

  function renderPlugin(id) {
    const c = $('content');
    if (!c) return;
    const tab = pluginTabs.find(p => p.id === id);
    if (!tab) { c.innerHTML = '<div class="empty">Plugin not found.</div>'; pluginUnmount(id); return; }
    pluginGrantsEnsure();               // a store not readable yet just prompts once more
    const granted = pluginGetGranted(id);
    if (!granted || !pluginGrantCovers(granted, tab.scopes)) {
      const cur = c.firstElementChild;
      if (cur && cur.dataset && cur.dataset.pluginPerm === id) return;   // build-once
      renderPluginPermission(id, tab, granted); pluginUnmount(id); return;
    }
    if (pluginInBackground(id)) pluginUnmount(id);   // running off-screen: restart inside the window instead
    const w = wmWinOf('plugin:' + id);
    const h = pluginHolderFor(id, w);
    h.el.hidden = false;
    const cur = c.firstElementChild;
    if (!(cur && cur.dataset && cur.dataset.pluginPane === id)) {
      c.innerHTML = '';
      const ph = document.createElement('div'); ph.dataset.pluginPane = id; ph.style.cssText = 'flex:1 1 auto;min-height:0;';
      c.appendChild(ph);                 // the pane stays empty behind the holder
    }
    const why = pluginFailed.get(id);
    if (why) { pluginFailShow(h, why); return; }
    pluginMountInto(id, tab, granted, h);
  }
  // Build the plugin's frame (or Lua state) inside holder h; shared by windowed and background mounts.
  function pluginMountInto(id, tab, granted, h) {
    if (pluginMounts.get(id) && h.el.firstChild) return;                   // alive from an earlier show: nothing to rebuild
    if (tab.source === 'installed' && !pluginRevokeChecked) {                // held until the revocation check (pluginRevokedPoll)
      pluginFailShow(h, 'Waiting for the plugin safety check.');
      return;
    }
    h.el.innerHTML = '';
    if (tab.manifest.runtime === 'lua') { luaMountPlugin(h.el, id, tab, granted); return; }   // core/rtx-plugin-lua.js
    const wrap = document.createElement('div'); wrap.dataset.pluginFrame = id;
    wrap.style.cssText = 'padding:0;flex:1 1 auto;min-height:0;display:flex;flex-direction:column;';
    const fr = document.createElement('iframe');
    fr.setAttribute('sandbox', 'allow-scripts');     // NO allow-same-origin -> opaque origin
    fr.setAttribute('referrerpolicy', 'no-referrer');
    const mnt = { id, frame: fr, scopes: granted.filter(s => tab.scopes.indexOf(s) !== -1), frLoads: 0 };
    fr.addEventListener('load', () => {
      if (pluginMounts.get(id) !== mnt) return;   // already stopped or replaced
      if (++mnt.frLoads > 1) {
        pluginFail(id, 'Plugin disabled: ' + tab.manifest.name + ' tried to navigate its page, which plugins are not permitted to do.');
        try { rtx.log && rtx.log('plugin ' + id + ' unmounted: unexpected frame navigation'); } catch (e) {}
      }
    });
    fr.style.cssText = 'border:0;display:block;width:100%;flex:1 1 auto;min-height:0;background:#14151c;';
    h.el.appendChild(wrap);
    pluginMounts.set(id, mnt);
    (async () => {
      let entry = '';
      const entryFn = (tab.source === 'installed') ? bridge().pluginInstalledEntry : bridge().pluginDevEntry;
      try { entry = await entryFn.call(bridge(), id, tab.manifest.entry); } catch (e) {}
      const m = pluginMounts.get(id);
      if (!m || m.frame !== fr) return;             // window closed / remounted while loading
      if (!entry) { pluginFail(id, 'Failed to load plugin entry file.'); return; }
      fr.srcdoc = pluginBuildSrcdoc(entry);
      wrap.appendChild(fr);
    })();
  }

  async function readPluginSet(listFn, manifestFn, source) {
    const out = [];
    let ids = [];
    try { ids = JSON.parse(await listFn.call(bridge())); } catch (e) { ids = []; }
    for (const id of (Array.isArray(ids) ? ids : [])) {
      let man = null;
      try { man = JSON.parse(await manifestFn.call(bridge(), id)); } catch (e) {}
      if (!man || man.rtxPluginManifest !== 1) continue;
      const scopes = Array.isArray(man.scopes) ? man.scopes.filter(s => PLUGIN_SCOPES.has(s)) : [];
      const clean = s => String(s == null ? '' : s).replace(/[<>&"']/g, '');
      const entry = (typeof man.entry === 'string' && man.entry && !/[\\/]|\.\./.test(man.entry)) ? man.entry : 'index.html';
      // runtime "lua": the launcher runs <main> in its sandboxed Lua host instead of mounting <entry> in a frame
      const runtime = (man.runtime === 'lua') ? 'lua' : 'html';
      const main = (typeof man.main === 'string' && /^[A-Za-z0-9_.-]+\.lua$/.test(man.main) && man.main.indexOf('..') < 0) ? man.main : 'main.lua';
      out.push({
        id, source,
        manifest: { id: clean(man.id).slice(0, 80) || id, name: clean(man.name).slice(0, 48) || id, version: clean(man.version).slice(0, 20), author: clean(man.author).slice(0, 60), entry, runtime, main, background: man.background === true, description: clean(man.description).slice(0, 280) },
        scopes
      });
    }
    return out;
  }

  async function loadPlugins() {
    if (!bridge()) return;
    const byId = {};
    if (bridge().pluginInstalledList) {
      const inst = await readPluginSet(bridge().pluginInstalledList, bridge().pluginInstalledManifest, 'installed');
      inst.forEach(p => { byId[p.id] = p; });
    }
    if (bridge().pluginDevList) {
      const dev = await readPluginSet(bridge().pluginDevList, bridge().pluginDevManifest, 'dev');
      dev.forEach(p => { if (!byId[p.id]) byId[p.id] = p; });   // installed wins over a dev copy
    }
    pluginTabs = Object.keys(byId).map(k => byId[k]);
    renderSidebar();
    for (const w of Array.from(wm.wins.values())) {
      for (const tid of w.tabs.slice()) {
        if (String(tid).indexOf('plugin:') !== 0) continue;
        const id = String(tid).slice('plugin:'.length);
        if (pluginTabs.some(p => p.id === id)) { if (!w.min && w.tab === tid) renderPaneFor(w); }
        else {
          console.log('plugin window ' + id + ' not in loaded list [' + pluginTabs.map(p => p.id).join(',') + '] - closing');
          wmCloseTabIn(w, tid);
        }
      }
    }
    try { pluginBackgroundSync(); } catch (e) {}
  }

  let pluginDevStampLast = null;
  async function pluginDevWatch() {
    try {
      if (!bridge() || typeof bridge().pluginDevStamp !== 'function') return;
      const stamp = String(await bridge().pluginDevStamp());
      if (pluginDevStampLast === null) { pluginDevStampLast = stamp; return; }
      if (stamp === pluginDevStampLast) return;
      const prev = pluginDevStampLast; pluginDevStampLast = stamp;
      await loadPlugins();
      const tok = s => {
        const m = {};
        String(s).split(';').forEach(t => { const i = t.indexOf(':'); if (i > 0) m[t.slice(0, i)] = t.slice(i + 1); });
        return m;
      };
      const before = tok(prev), after = tok(stamp);
      for (const id of new Set([...pluginMounts.keys(), ...pluginFailed.keys()])) {   // running or stopped
        const tab = pluginTabs.find(p => p.id === id);
        if (!tab || tab.source !== 'dev' || before[id] === after[id]) continue;
        pluginFailed.delete(id);
        pluginUnmount(id);
        const w = wmWinOf('plugin:' + id);
        if (w && !w.min && w.tab === ('plugin:' + id)) { w.pane.innerHTML = ''; renderPaneFor(w); }
      }
      try { pluginBackgroundSync(); } catch (e) {}
    } catch (e) {}
  }
