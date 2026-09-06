// rtx-plugins.js: Plugin SDK host broker: plugin registry + mounts, per-plugin settings, permission grants, the sandboxed frame (CSP + srcdoc), plugin windows, dev hot-reload and allTabs
// Loads after: rtx-wm.js, rtx-bridge.js, rtx-notify.js, rtx-plugin-api.js, rtx-plugin-hud.js; starts the GIM membership poll interval at load.
// Plain script, page globals by design: every top-level name here is a page global that panels and the other core files use.
  // ===================== Plugin SDK (host broker) =====================
  // Plugins run in a sandboxed <iframe sandbox="allow-scripts"> (opaque origin,
  // no allow-same-origin) so they cannot reach window.rtx, the host DOM, or each
  // other. Their only channel is postMessage to this broker, which exposes a
  // curated rtx.plugin API gated by manifest scopes + rate limits.
  // See PLUGIN_SDK.md. A plugin only ever reaches the read, overlay, sound, and
  // storage methods its granted scopes allow.
  let   pluginTabs    = [];     // [{ id, manifest:{id,name,version,author,entry,description}, scopes:[] }]
  const pluginMounts  = new Map();   // plugin id -> { id, frame, scopes }; one per open plugin window
  // ---- per-plugin settings, declared by the plugin and rendered on the host's own
  // Preferences page (rtx-settings.js) so every plugin gets standard controls for
  // free. The plugin calls rtx.plugin.ui.settings(schema) once at boot; values are
  // stored host-side in the plugin's storage bucket under the reserved key
  // '~settings' (host-mediated: no 'storage' scope needed) and pushed back to the
  // plugin as a 'settings' event on declare and on every change.
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
  // Change one value (Preferences page calls this): clamp, persist, push to the plugin.
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
  function pluginUnmount(id) {
    // Release keyboard capture a frame claimed: an unmounted frame fires no focusout, and a
    // leaked grab would eat every keystroke meant for the game from then on.
    const m = pluginMounts.get(id);
    if (m && m.kbFocus) kbGrab(false);
    pluginMounts.delete(id);
  }

  function pluginSendEvent(m, event, data) {
    if (!m || !m.frame || !m.frame.contentWindow) return;
    try { m.frame.contentWindow.postMessage({ __rtxPlugin: PLUGIN_PROTO, kind: 'event', event, data }, '*'); } catch (e) {}
  }

  // Push the host poll cadence to every mounted plugin (no free-polling in plugins).
  // Game events collected by rtxEvents since the last push ride along as ONE 'events'
  // batch per plugin with state.read; `tick` stays the 4 Hz heartbeat it always was.
  const pluginEventQueue = [];
  function pluginPush() {
    const batch = pluginEventQueue.length ? pluginEventQueue.splice(0) : null;
    for (const m of pluginMounts.values()) {
      if (!m.frame) continue;
      pluginSendEvent(m, 'tick', null);
      if (m.scopes.indexOf('state.read') !== -1) {
        pluginSendEvent(m, 'state', lastSnap);
        if (batch) pluginSendEvent(m, 'events', batch);
      }
    }
  }

  function pluginBrokerInit() {
    // rtxEvents (core/rtx-data.js) loads after this file, so subscribe here, at attach time.
    rtxEvents.on('*', function (ev) { if (pluginMounts.size && pluginEventQueue.length < 4096) pluginEventQueue.push(ev); });
    window.addEventListener('message', async (ev) => {
      const m = ev.data;
      if (!m || m.__rtxPlugin !== PLUGIN_PROTO) return;
      // Route by SOURCE frame: several plugin windows can be mounted at once, and
      // an unknown source is ignored exactly like before.
      let mounted = null;
      for (const pm of pluginMounts.values())
        if (pm.frame && ev.source === pm.frame.contentWindow) { mounted = pm; break; }
      if (!mounted) return;
      // Defense in depth alongside the load-count teardown: a legitimate sandboxed
      // srcdoc frame always has the opaque origin ('null'); anything else is not a
      // plugin document we built.
      if (ev.origin !== 'null') return;
      if (m.kind === 'hello') {
        pluginSendEvent(mounted, 'ready', { scopes: mounted.scopes, apiVersion: '1.0', pluginId: mounted.id });
        return;
      }
      if (m.kind === 'kb') {
        // A text field inside the sandboxed frame gained/lost focus (the SDK shim watches;
        // this page cannot see across the opaque origin). Refcounted per mount so several
        // open plugins cannot unbalance each other.
        if (!!m.on !== !!mounted.kbFocus) { mounted.kbFocus = !!m.on; kbGrab(mounted.kbFocus); }
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
        const def = PLUGIN_API[m.method];
        if (!def) return reply(false, 'unknown method');
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

  // ---- plugin permission grants ----
  // PER ACCOUNT, and deliberately not migrated from the old machine-wide localStorage key.
  // Consent is a security decision about "this plugin may read this character's data", and
  // the old store answered it once for every character on the machine. Carrying those grants
  // forward would re-create exactly that: silent authorisation on accounts that never
  // consented. Users consent once more per character instead, which is the point.
  //
  // Grants live in memory for the session as well (localStorage was unreliable in the
  // LoadHTML panel, and the per-account file cannot be written until the character name
  // resolves), so a grant is never lost mid-session just because the store is not ready.
  let pluginGrants = Object.create(null);   // plugin id -> granted scope array
  // The account the in-memory map belongs to. NOT the pid: logging into a different
  // character reuses the same client (same pid) while changing which account's grants apply,
  // so a pid check would happily keep the previous character's consents in memory.
  let pluginGrantsAcct = null;              // null = never bound to an account yet
  let _pgSaveT = 0, _pgSaveTries = 0, _pgLoading = false;
  function pluginAcctKey() { return (lastSnap && lastSnap.display_name) || ''; }
  async function pluginGrantsEnsure() {
    const acct = pluginAcctKey();
    if (!acct || _pgLoading || pluginGrantsAcct === acct) return;
    _pgLoading = true;
    let loaded = null;
    try {
      const s = await bridge().pluginGrantsLoad(myPid());
      const v = JSON.parse(s || '{}');
      if (v && typeof v === 'object') loaded = v;
    } catch (e) {}
    _pgLoading = false;
    if (!loaded) return;                    // store not readable yet: retry on the next call
    const first = (pluginGrantsAcct === null);
    const merged = Object.create(null);
    for (const k in loaded) if (Array.isArray(loaded[k])) merged[k] = loaded[k];
    // Only on the FIRST binding do session grants survive: those were made moments earlier by
    // this user, before the character name resolved, and dropping them would re-prompt for a
    // consent just given. On an account CHANGE the in-memory map belongs to the previous
    // character and must not carry over -- that is the whole bug being fixed.
    if (first) for (const k in pluginGrants) merged[k] = pluginGrants[k];
    pluginGrants = merged;
    pluginGrantsAcct = acct;
    if (first) pluginGrantsSaveSoon();
    // A different character's grants are now in force, so anything mounted under the old
    // ones must be re-evaluated rather than left running.
    if (!first) for (const w of wm.wins.values())
      if (String(w.tab).indexOf('plugin:') === 0) { pluginUnmount(w.tab.slice(7)); renderPaneFor(w); }
  }
  function pluginGrantsSaveSoon(delay) {
    if (_pgSaveT) return;
    _pgSaveT = setTimeout(() => {
      _pgSaveT = 0;
      let ok = false;
      try { ok = !!bridge().pluginGrantsSave(myPid(), JSON.stringify(pluginGrants)); } catch (e) {}
      // Refused until the account resolves (the file is per-character). Retry rather than
      // silently losing the consent, backing off to 2s so we are not spinning every 400ms.
      if (ok) { _pgSaveTries = 0; return; }
      if (_pgSaveTries < 10) { _pgSaveTries++; pluginGrantsSaveSoon(2000); }
    }, delay || 400);
  }
  function pluginGetGranted(id) { return pluginGrants[id] || null; }
  function pluginSetGranted(id, scopes) {
    pluginGrants[id] = scopes || [];
    pluginGrantsSaveSoon();
  }
  function pluginClearGranted(id) {
    delete pluginGrants[id];
    pluginGrantsSaveSoon();
  }
  // A grant only covers the scopes it was given for. If a plugin update asks for anything
  // new, the old consent does NOT cover it: re-prompt with the new list. (The per-call gate
  // already refuses an ungranted scope, so this was never an escalation -- but the user was
  // never asked, and the plugin just failed silently.)
  function pluginGrantCovers(granted, want) {
    if (!Array.isArray(granted)) return false;
    for (const s of (want || [])) if (granted.indexOf(s) === -1) return false;
    return true;
  }

  const PLUGIN_TAB_ICON = '<rect x="4" y="4" width="7" height="7" rx="1"/><rect x="13" y="4" width="7" height="7" rx="1"/><rect x="4" y="13" width="7" height="7" rx="1"/><path d="M13.5 17h6.5M16.75 13.75v6.5"/>';
  const PLUGIN_BROWSE_ICON = '<circle cx="11" cy="11" r="7"/><path d="M20.5 20.5l-4.2-4.2"/><path d="M11 8v6M8 11h6"/>';
  // Group Bank only applies to group-ironman accounts: varp 4818 bit 19 (varbit 56612,
  // "in a GIM group") flips to 1 for every GIM variant. Unknown state (null, nothing read
  // yet) keeps the tab visible - hiding is only ever done on a confirmed non-GIM read.
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
    pluginTabs.forEach(p => extra.push({ id: 'plugin:' + p.id, label: p.manifest.name, cat: 'Plugins', icon: PLUGIN_TAB_ICON }));
    // Aura groups: one HUD tab each (hidden from the menus; the Auras panel shows/hides them).
    if (typeof auraGroupTabs === 'function') { try { auraGroupTabs().forEach(t => extra.push(t)); } catch (e) {} }
    return TABS.filter(t => (t.id !== 'dung' || DUNG_ENABLED) && (t.id !== 'groupbank' || gimInGroup !== false))
      .map(t => { const g = TAB_GROUP_OF[t.id]; return (g && g.cat && g.cat !== t.cat) ? Object.assign({}, t, { cat: g.cat }) : t; })
      .concat(extra);
  }

  // The host's live theme, handed to plugin frames as CSS custom properties. A plugin
  // is its own document with an opaque origin, so it inherits nothing from the host:
  // without this every plugin hardcodes colours and ignores the user's accent, scale
  // and font entirely. Names are rtx-prefixed so they can never collide with a
  // plugin's own variables, and every one carries a fallback at the use site.
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
    // Native defaults so a plugin that styles nothing still matches the client. The
    // author's own head content is spliced AFTER this, so anything they set wins.
    css += 'html,body{margin:0;background:var(--rtx-bg,#14151c);color:var(--rtx-text,#e8e8ef);' +
           'font-family:var(--rtx-font-ui,-apple-system,"Segoe UI",Roboto,sans-serif);' +
           'font-size:var(--rtx-font-size,13px);}';
    return css;
  }
  // Push the theme to every mounted plugin when the user changes it (uiApply calls this).
  function pluginThemeBroadcast() {
    if (!pluginMounts.size) return;
    const vars = pluginThemeVars();
    for (const m of pluginMounts.values()) pluginSendEvent(m, 'theme', vars);
  }

  // Build the plugin document: CSP + SDK shim FIRST in a canonical skeleton, with the
  // author's head/body content re-serialized behind them. The old approach spliced the
  // CSP after the first regex match of "<head>", which an entry file could steer into
  // an HTML comment ("<!-- <head> -->"), leaving the real document with NO CSP at all
  // (free network access from inside the frame). A real HTML parse cannot be steered
  // by comments, and the CSP meta preceding every author node means it always applies.
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
    // prev = scopes already consented to for THIS character, when the prompt is a re-ask
    // because the plugin now wants more than it did.
    const isUpdate = Array.isArray(prev) && prev.length > 0;
    const card = document.createElement('div');
    card.dataset.pluginPerm = id;
    card.style.cssText = 'max-width:420px;margin:24px auto;background:#1a1b23;border:1px solid rgba(255,255,255,.08);border-radius:12px;padding:20px;';
    const labels = { 'state.read': 'Read your live game state', 'cache.read': 'Read game cache data (items, sprites, enums)', 'overlay': 'Draw overlays on the game', 'sound': 'Play alert sounds', 'storage': 'Store its own settings', 'notify.os': 'Show Windows notifications', 'clipboard': 'Copy text to your clipboard', 'clipboard.read': 'Read your clipboard contents' };
    let h = '<div style="font-size:1.1em;font-weight:600;">' + pluginEsc(m.name) + '</div>';
    h += '<div style="color:#8b8b9e;font-size:.85em;margin:2px 0 6px;">v' + pluginEsc(m.version) + (m.author ? (' - ' + pluginEsc(m.author)) : '') + '</div>';
    if (tab.source === 'dev')
        h += '<div style="color:#e0b000;font-size:.8em;margin-bottom:12px;">Unsigned developer plugin - only enable plugins you trust.</div>';
    else
        h += '<div style="color:#34d399;font-size:.8em;margin-bottom:12px;">Verified - signed by RuneTools.</div>';
    if (m.description) h += '<div style="font-size:.9em;margin-bottom:12px;">' + pluginEsc(m.description) + '</div>';
    if (isUpdate)
      h += '<div style="color:#e0b000;font-size:.82em;margin-bottom:10px;">This plugin now asks for more access than you allowed. Review the new items before enabling it.</div>';
    h += '<div style="font-size:.82em;color:#8b8b9e;margin-bottom:6px;">This plugin can:</div><ul style="margin:0 0 14px 18px;font-size:.88em;line-height:1.6;">';
    (tab.scopes.length ? tab.scopes : ['(no special access)']).forEach(s => {
      const isNew = isUpdate && prev.indexOf(s) === -1;
      h += '<li' + (isNew ? ' style="color:#e0b000;font-weight:600;"' : '') + '>' + pluginEsc(labels[s] || s) +
           (isNew ? ' <span style="font-weight:400;font-size:.85em;">(new)</span>' : '') + '</li>';
    });
    h += '</ul>';
    // Consent is per character, so name the one being authorised: the same plugin on another
    // character is a separate decision, and the user should not be surprised by that.
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

  function renderPlugin(id) {
    const c = $('content');
    if (!c) return;
    const tab = pluginTabs.find(p => p.id === id);
    if (!tab) { c.innerHTML = '<div class="empty">Plugin not found.</div>'; pluginUnmount(id); return; }
    pluginGrantsEnsure();               // async; a not-yet-loaded store just prompts once more
    const granted = pluginGetGranted(id);
    // Consent must cover everything the CURRENT manifest asks for: an update that adds a
    // scope has not been consented to, even though an older grant exists.
    if (!granted || !pluginGrantCovers(granted, tab.scopes)) {
      const cur = c.firstElementChild;
      if (cur && cur.dataset && cur.dataset.pluginPerm === id) return;   // build-once
      renderPluginPermission(id, tab, granted); pluginUnmount(id); return;
    }
    const cur = c.firstElementChild;
    if (cur && cur.dataset && cur.dataset.pluginHost === id) return;      // already mounted
    c.innerHTML = '';
    const wrap = document.createElement('div'); wrap.dataset.pluginHost = id;
    // Fill the window; the plugin scrolls inside itself. A plugin can opt into a
    // fixed height via rtx.plugin.ui.setHeight(px).
    wrap.style.cssText = 'padding:0;flex:1 1 auto;min-height:0;display:flex;flex-direction:column;';
    const fr = document.createElement('iframe');
    fr.setAttribute('sandbox', 'allow-scripts');     // NO allow-same-origin -> opaque origin
    fr.setAttribute('referrerpolicy', 'no-referrer');
    // ESCAPE HARDENING: the sandbox permits a document to navigate ITSELF, and a
    // navigated frame keeps its window identity, so the broker would keep trusting a
    // remote page that escaped the spliced CSP (its own CSP governs it after
    // navigation). Exactly one load event (the srcdoc mount) is legitimate; any
    // further load tears the plugin down.
    // The frame is attached to the DOM only AFTER srcdoc is set (see below): an iframe
    // inserted empty first loads about:blank and fires a load of its own, which counted
    // as a navigation and disabled every plugin on mount.
    let frLoads = 0;
    fr.addEventListener('load', () => {
      frLoads++;
      if (frLoads > 1) {
        pluginUnmount(id);
        try { fr.remove(); } catch (e) {}
        c.innerHTML = '<div class="empty">Plugin disabled: ' + pluginEsc(tab.manifest.name) +
                      ' tried to navigate its page, which plugins are not permitted to do.</div>';
        try { rtx.log && rtx.log('plugin ' + id + ' unmounted: unexpected frame navigation'); } catch (e) {}
      }
    });
    fr.style.cssText = 'border:0;display:block;width:100%;flex:1 1 auto;min-height:0;background:#14151c;';
    c.appendChild(wrap);
    // Least privilege: the mount carries the INTERSECTION of what was granted and what the
    // manifest still asks for, so a scope dropped from a later manifest stops working even
    // though the older grant still lists it. The per-call gate reads this list.
    pluginMounts.set(id, { id, frame: fr, scopes: granted.filter(s => tab.scopes.indexOf(s) !== -1) });
    (async () => {
      let entry = '';
      const entryFn = (tab.source === 'installed') ? bridge().pluginInstalledEntry : bridge().pluginDevEntry;
      try { entry = await entryFn.call(bridge(), id, tab.manifest.entry); } catch (e) {}
      const m = pluginMounts.get(id);
      if (!m || m.frame !== fr) return;             // window closed / remounted while loading
      if (!entry) { c.innerHTML = '<div class="empty">Failed to load plugin entry file.</div>'; pluginUnmount(id); return; }
      // Content first, THEN attach: one document, one load event, no about:blank.
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
      // Strip HTML-significant chars from author-supplied strings shown in host DOM.
      const clean = s => String(s == null ? '' : s).replace(/[<>&"']/g, '');
      const entry = (typeof man.entry === 'string' && man.entry && !/[\\/]|\.\./.test(man.entry)) ? man.entry : 'index.html';
      out.push({
        id, source,
        manifest: { id: clean(man.id).slice(0, 80) || id, name: clean(man.name).slice(0, 48) || id, version: clean(man.version).slice(0, 20), author: clean(man.author).slice(0, 60), entry, description: clean(man.description).slice(0, 280) },
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
    // Windows restored (or opened) before this list existed now have real manifests:
    // re-render each open plugin tab; drop any whose plugin no longer exists. Scans
    // w.tabs, since a plugin can be docked alongside anything.
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
  }

  // Dev-plugin hot reload: poll a cheap host-side fingerprint of plugins-dev so a
  // folder dropped in (or an edited entry file) shows up without a client restart.
  // The first poll only seeds the baseline; later changes reload the plugin list
  // and remount any open dev plugin whose own folder changed.
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
      for (const id of Array.from(pluginMounts.keys())) {
        const tab = pluginTabs.find(p => p.id === id);
        if (!tab || tab.source !== 'dev' || before[id] === after[id]) continue;
        pluginUnmount(id);
        const w = wmWinOf('plugin:' + id);
        if (w && !w.min && w.tab === ('plugin:' + id)) { w.pane.innerHTML = ''; renderPaneFor(w); }
      }
    } catch (e) {}
  }
