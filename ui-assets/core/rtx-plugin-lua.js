  // ---- Lua plugin host (core/rtx-plugin-lua.js) --------------------------------------------------
  // A Lua plugin has no frame. The launcher runs it in a sandboxed Lua 5.4 state (src/launcher/LuaHost.cpp)
  // and this file is its window: it mounts the plugin, feeds it the same tick / state / events push a
  // JavaScript plugin gets, renders the widget tree the plugin publishes, and answers every rtx.* call
  // the plugin makes through __rtxLuaCall, which walks the same PLUGIN_API table, scope check and rate
  // limiter as the iframe broker. One API table, two runtimes.

  const luaAsync = new Map();   // "id|method|args" -> { v, t }: last value of an async PLUGIN_API method

  // The method list handed to the runtime at load: every PLUGIN_API entry a plugin may be granted.
  function luaMethodList() {
    return Object.keys(PLUGIN_API)
      .filter(k => !PLUGIN_API[k].scope || PLUGIN_SCOPES.has(PLUGIN_API[k].scope))
      .map(k => ({ name: k, scope: PLUGIN_API[k].scope || null }));
  }

  window.__rtxLuaCall = function (id, method, argsJson) {
    const m = pluginMounts.get(String(id));
    if (!m || !m.lua) return JSON.stringify({ ok: false, e: 'plugin not mounted' });
    let args = [];
    try { args = JSON.parse(argsJson); } catch (e) { args = []; }
    if (!Array.isArray(args)) args = (args && typeof args === 'object' && Object.keys(args).length) ? [args] : [];   // {} is Lua's empty argument list
    const def = PLUGIN_API[String(method)];
    if (!def) return JSON.stringify({ ok: false, e: 'unknown method' });
    if (def.scope && m.scopes.indexOf(def.scope) === -1) return JSON.stringify({ ok: false, e: 'scope not granted: ' + def.scope });
    if (!pluginRateOk(m.id, method)) return JSON.stringify({ ok: false, e: 'rate limited' });
    if (!bridge()) return JSON.stringify({ ok: false, e: 'host unavailable' });
    const finish = raw => {
      let v = raw;
      if (def.json === true) { try { v = JSON.parse(raw); } catch (e) { v = null; } }
      else if (def.json === 'maybe') { try { v = raw ? JSON.parse(raw) : null; } catch (e) { v = null; } }
      return v === undefined ? null : v;
    };
    let raw;
    try { raw = def.run(args, myPid(), m.id, null); }
    catch (e) { return JSON.stringify({ ok: false, e: 'call failed' }); }
    if (raw && typeof raw.then === 'function') {
      // An async method (quests, pets, dailies, ui.settings, ...): resolve in the background and hand the
      // plugin the last resolved value; the first call of a new key reports pending.
      const key = m.id + '|' + method + '|' + argsJson;
      raw.then(r => {
        luaAsync.set(key, { v: finish(r), t: Date.now() });
        if (luaAsync.size > 512) for (const k of luaAsync.keys()) { luaAsync.delete(k); if (luaAsync.size <= 384) break; }
      }, () => {});
      const c = luaAsync.get(key);
      if (c) { try { return JSON.stringify({ ok: true, v: c.v, age: Date.now() - c.t }); } catch (e) { return JSON.stringify({ ok: true, v: null }); } }
      return JSON.stringify({ pending: true });
    }
    try { return JSON.stringify({ ok: true, v: finish(raw) }); }
    catch (e) { return JSON.stringify({ ok: false, e: 'result not serialisable' }); }
  };

  let _luaCssDone = false;
  function luaEnsureCss() {
    if (_luaCssDone) return; _luaCssDone = true;
    const st = document.createElement('style');
    st.textContent =
      '.lua-host{font-size:var(--font-size,13px);}' +
      '.lua-body{padding:12px 14px;flex:1 1 auto;display:flex;flex-direction:column;gap:8px;align-content:flex-start;}' +
      '.lua-h{font-size:1.1em;font-weight:600;margin:2px 0;}' +
      '.lua-t{line-height:1.45;white-space:pre-wrap;word-break:break-word;}' +
      '.lua-t.muted{color:var(--text-dim,#8b8b9e);font-size:.9em;}' +
      '.lua-row{display:flex;flex-wrap:wrap;gap:8px;align-items:center;}' +
      '.lua-col{display:flex;flex-direction:column;gap:8px;}' +
      '.lua-card{background:var(--panel,#1a1b23);border:1px solid var(--border,rgba(255,255,255,.06));border-radius:10px;padding:12px;display:flex;flex-direction:column;gap:8px;}' +
      '.lua-btn{background:rgba(255,255,255,.06);color:var(--text,#e8e8ef);border:1px solid var(--border,rgba(255,255,255,.1));border-radius:8px;padding:7px 14px;font-weight:600;cursor:pointer;font:inherit;}' +
      '.lua-btn.primary{background:var(--accent-hi,#7c5cfc);color:#fff;border-color:transparent;}' +
      '.lua-btn:hover{filter:brightness(1.12);}' +
      '.lua-toggle{display:flex;align-items:center;gap:8px;cursor:pointer;}' +
      '.lua-toggle input{width:16px;height:16px;accent-color:var(--accent-hi,#7c5cfc);}' +
      '.lua-input{background:var(--bg-elev,#101117);color:var(--text,#e8e8ef);border:1px solid var(--border,rgba(255,255,255,.1));border-radius:8px;padding:6px 10px;font:inherit;min-width:120px;}' +
      '.lua-label{color:var(--text-dim,#8b8b9e);font-size:.85em;}' +
      '.lua-prog{position:relative;height:14px;background:rgba(255,255,255,.06);border-radius:7px;overflow:hidden;min-width:120px;flex:1 1 120px;}' +
      '.lua-prog>i{position:absolute;left:0;top:0;bottom:0;background:var(--accent-hi,#7c5cfc);border-radius:7px;}' +
      '.lua-prog>b{position:absolute;inset:0;font-size:.75em;font-weight:600;text-align:center;line-height:14px;color:#fff;}' +
      '.lua-table{border-collapse:collapse;width:100%;font-size:.92em;}' +
      '.lua-table th{text-align:left;color:var(--text-dim,#8b8b9e);font-weight:600;padding:4px 8px;border-bottom:1px solid var(--border,rgba(255,255,255,.08));}' +
      '.lua-table td{padding:4px 8px;border-bottom:1px solid rgba(255,255,255,.04);font-variant-numeric:tabular-nums;}' +
      '.lua-kv{display:grid;grid-template-columns:auto 1fr;gap:3px 14px;}' +
      '.lua-kv .k{color:var(--text-dim,#8b8b9e);}' +
      '.lua-badge{display:inline-block;padding:2px 8px;border-radius:999px;font-size:.8em;font-weight:600;background:rgba(255,255,255,.08);}' +
      '.lua-badge.ok{background:rgba(52,211,153,.18);color:#34d399;}.lua-badge.warn{background:rgba(224,176,0,.18);color:#e0b000;}.lua-badge.err{background:rgba(255,99,99,.18);color:#ff6363;}' +
      '.lua-sep{border:0;border-top:1px solid var(--border,rgba(255,255,255,.08));margin:2px 0;width:100%;}' +
      '.lua-err{background:rgba(255,99,99,.12);border:1px solid rgba(255,99,99,.35);border-radius:8px;padding:8px 10px;font-size:.88em;white-space:pre-wrap;word-break:break-word;}' +
      '.lua-console{border-top:1px solid var(--border,rgba(255,255,255,.08));font-size:.82em;}' +
      '.lua-console summary{cursor:pointer;padding:6px 14px;color:var(--text-dim,#8b8b9e);user-select:none;}' +
      '.lua-console pre{margin:0;padding:6px 14px 10px;max-height:180px;overflow:auto;white-space:pre-wrap;word-break:break-word;color:var(--text-dim,#8b8b9e);font-family:Consolas,monospace;}' +
      '.lua-console pre .warn{color:#e0b000;}.lua-console pre .error{color:#ff6363;}';
    document.head.appendChild(st);
  }

  function luaMountPlugin(c, id, tab, granted) {
    luaEnsureCss();
    c.innerHTML = '';
    const wrap = document.createElement('div');
    wrap.dataset.pluginHost = id;
    wrap.className = 'lua-host';
    wrap.style.cssText = 'padding:0;flex:1 1 auto;min-height:0;display:flex;flex-direction:column;overflow:auto;';
    const err = document.createElement('div'); err.className = 'lua-err'; err.style.cssText = 'margin:12px 14px 0;display:none;';
    const body = document.createElement('div'); body.className = 'lua-body';
    const con = document.createElement('details'); con.className = 'lua-console';
    const sum = document.createElement('summary'); sum.textContent = 'Console'; con.appendChild(sum);
    const pre = document.createElement('pre'); con.appendChild(pre);
    wrap.appendChild(err); wrap.appendChild(body); wrap.appendChild(con);
    c.appendChild(wrap);
    const m = { id, frame: null, lua: true, scopes: granted.filter(s => tab.scopes.indexOf(s) !== -1),
                el: wrap, body, pre, err, sum, wantsState: false, logCount: 0, hasUi: false };
    pluginMounts.set(id, m);
    let res = null;
    try {
      if (!bridge() || typeof bridge().luaLoad !== 'function') throw new Error('no runtime');
      res = JSON.parse(bridge().luaLoad(id, tab.source, m.scopes.join(','), JSON.stringify(luaMethodList())));
    } catch (e) { res = { ok: false, error: 'This launcher build has no Lua runtime. Update RuneTools to run Lua plugins.', log: [] }; }
    luaApplyResult(m, res);
    if (!m.hasUi && res && res.ok) luaEmptyState(m);
  }

  function luaEmptyState(m) {
    m.body.innerHTML = '';
    const t = document.createElement('div'); t.className = 'lua-t muted';
    t.textContent = 'Running. This plugin has not published a panel; its output appears in the console below.';
    m.body.appendChild(t);
  }

  function luaApplyResult(m, r) {
    if (!r || typeof r !== 'object') return;
    if (Array.isArray(r.log) && r.log.length) {
      for (const line of r.log) {
        const s = String(line);
        const span = document.createElement('span');
        const lvl = s.indexOf('warn:') === 0 ? 'warn' : (s.indexOf('error:') === 0 ? 'error' : '');
        if (lvl) span.className = lvl;
        span.textContent = s + '\n';
        m.pre.appendChild(span);
        m.logCount++;
        if (lvl === 'error' && m.sum) { m.sum.textContent = 'Console (errors)'; }
      }
      while (m.pre.childNodes.length > 200) m.pre.removeChild(m.pre.firstChild);
      if (m.pre.parentNode && m.pre.parentNode.open) m.pre.scrollTop = m.pre.scrollHeight;
    }
    if (r.failed) {
      m.err.style.display = '';
      m.err.textContent = 'Plugin stopped: ' + (r.error || 'repeated errors') +
        '\nFix the script and save it (developer plugins reload on save) or reinstall the plugin.';
    } else if (r.error) {
      m.err.style.display = '';
      m.err.textContent = String(r.error);
    } else if (r.ok) {
      m.err.style.display = 'none';
    }
    if (typeof r.wantsState === 'boolean') m.wantsState = r.wantsState;
    if (Object.prototype.hasOwnProperty.call(r, 'ui')) {
      if (r.ui == null) { m.hasUi = false; luaEmptyState(m); }
      else { m.hasUi = true; luaRenderTree(m, r.ui); }
    }
  }

  function luaTickPlugin(m, batch) {
    if (!bridge() || typeof bridge().luaTick !== 'function') return;
    const canState = m.scopes.indexOf('state.read') !== -1;
    let ev = '', st = '';
    if (canState && batch && batch.length) { try { ev = JSON.stringify(batch); } catch (e) { ev = ''; } }
    if (canState && m.wantsState && lastSnap) { try { st = JSON.stringify(lastSnap); } catch (e) { st = ''; } }
    let r = null;
    try { r = JSON.parse(bridge().luaTick(m.id, ev, st)); } catch (e) { r = null; }
    if (r) luaApplyResult(m, r);
  }

  function luaSendEvent(m, event, data) {
    if (!bridge() || typeof bridge().luaEvent !== 'function') return;
    if (event !== 'settings' && event !== 'theme') return;     // ready / tick / state / events come from the runtime itself
    let payload = '';
    try { payload = JSON.stringify(data == null ? {} : data); } catch (e) { payload = '{}'; }
    let r = null;
    try { r = JSON.parse(bridge().luaEvent(m.id, event, payload)); } catch (e) { r = null; }
    if (r) luaApplyResult(m, r);
  }

  function luaUiEvent(m, widget, handler, value) {
    if (!bridge() || typeof bridge().luaUiEvent !== 'function') return;
    let payload = 'null';
    try { payload = JSON.stringify(value === undefined ? null : value); } catch (e) { payload = 'null'; }
    let r = null;
    try { r = JSON.parse(bridge().luaUiEvent(m.id, String(widget) + ':' + handler, payload)); } catch (e) { r = null; }
    if (r) luaApplyResult(m, r);
  }

  // ---- widget tree renderer ----
  // Nodes: heading{text} text{text,muted} card{children} row{children} col{children} button{id,label,primary,onClick}
  // toggle{id,label,value,onChange} input{id,value,placeholder,label,onChange} select{id,value,options:[{v,label}],label,onChange}
  // progress{value,max,label} table{columns:[..],rows:[[..]]} kv{items:[[k,v],..]} badge{text,tone} sep spacer{h}
  // Handlers are booleans in the tree (the runtime keeps the functions); the page posts "<id>:<handler>" back.
  const LUA_STR = (v, n) => String(v == null ? '' : v).slice(0, n || 500);
  function luaRenderTree(m, tree) {
    const focusId = (document.activeElement && document.activeElement.dataset) ? document.activeElement.dataset.luaId : null;
    let focusSel = null;
    if (focusId && document.activeElement.tagName === 'INPUT' && document.activeElement.type === 'text')
      focusSel = [document.activeElement.selectionStart, document.activeElement.selectionEnd];
    m.body.innerHTML = '';
    const list = Array.isArray(tree) ? tree : [tree];
    let count = 0;
    const build = (node, depth) => {
      if (!node || typeof node !== 'object' || depth > 8 || ++count > 500) return null;
      const t = String(node.type || 'text');
      let el = null;
      const kids = (parent) => { if (Array.isArray(node.children)) for (const c of node.children) { const k = build(c, depth + 1); if (k) parent.appendChild(k); } };
      switch (t) {
        case 'heading': el = document.createElement('div'); el.className = 'lua-h'; el.textContent = LUA_STR(node.text, 200); break;
        case 'text': el = document.createElement('div'); el.className = 'lua-t' + (node.muted ? ' muted' : ''); el.textContent = LUA_STR(node.text, 4000);
          if (typeof node.color === 'string' && /^#[0-9a-fA-F]{3,8}$/.test(node.color)) el.style.color = node.color; break;
        case 'card': el = document.createElement('div'); el.className = 'lua-card'; if (node.title) { const h = document.createElement('div'); h.className = 'lua-h'; h.textContent = LUA_STR(node.title, 200); el.appendChild(h); } kids(el); break;
        case 'row': el = document.createElement('div'); el.className = 'lua-row'; kids(el); break;
        case 'col': el = document.createElement('div'); el.className = 'lua-col'; kids(el); break;
        case 'button': el = document.createElement('button'); el.className = 'lua-btn' + (node.primary ? ' primary' : ''); el.textContent = LUA_STR(node.label || node.text || 'Button', 80);
          if (node.disabled) el.disabled = true;
          if (node.onClick && node.id) el.addEventListener('click', () => luaUiEvent(m, node.id, 'onClick', null)); break;
        case 'toggle': {
          el = document.createElement('label'); el.className = 'lua-toggle';
          const inp = document.createElement('input'); inp.type = 'checkbox'; inp.checked = !!node.value; inp.dataset.luaId = LUA_STR(node.id, 64);
          const sp = document.createElement('span'); sp.textContent = LUA_STR(node.label, 120);
          if (node.onChange && node.id) inp.addEventListener('change', () => luaUiEvent(m, node.id, 'onChange', inp.checked));
          el.appendChild(inp); el.appendChild(sp); break;
        }
        case 'input': {
          el = document.createElement('div'); el.className = 'lua-row';
          if (node.label) { const l = document.createElement('span'); l.className = 'lua-label'; l.textContent = LUA_STR(node.label, 120); el.appendChild(l); }
          const inp = document.createElement('input'); inp.type = 'text'; inp.className = 'lua-input'; inp.spellcheck = false;
          inp.value = LUA_STR(node.value, 500); inp.placeholder = LUA_STR(node.placeholder, 80); inp.dataset.luaId = LUA_STR(node.id, 64);
          if (node.onChange && node.id) inp.addEventListener('change', () => luaUiEvent(m, node.id, 'onChange', inp.value.slice(0, 500)));
          el.appendChild(inp); break;
        }
        case 'select': {
          el = document.createElement('div'); el.className = 'lua-row';
          if (node.label) { const l = document.createElement('span'); l.className = 'lua-label'; l.textContent = LUA_STR(node.label, 120); el.appendChild(l); }
          const sel = document.createElement('select'); sel.className = 'lua-input'; sel.dataset.luaId = LUA_STR(node.id, 64);
          for (const o of (Array.isArray(node.options) ? node.options : []).slice(0, 64)) {
            const op = document.createElement('option');
            const v = (o && typeof o === 'object') ? o.v : o;
            op.value = LUA_STR(v, 80); op.textContent = LUA_STR((o && typeof o === 'object' && o.label != null) ? o.label : v, 80);
            if (String(v) === String(node.value)) op.selected = true;
            sel.appendChild(op);
          }
          if (node.onChange && node.id) sel.addEventListener('change', () => luaUiEvent(m, node.id, 'onChange', sel.value));
          el.appendChild(sel); break;
        }
        case 'progress': {
          el = document.createElement('div'); el.className = 'lua-row';
          if (node.label) { const l = document.createElement('span'); l.className = 'lua-label'; l.textContent = LUA_STR(node.label, 120); el.appendChild(l); }
          const bar = document.createElement('div'); bar.className = 'lua-prog';
          const max = Number(node.max) > 0 ? Number(node.max) : 1;
          const val = Math.max(0, Math.min(max, Number(node.value) || 0));
          const fill = document.createElement('i'); fill.style.width = (100 * val / max).toFixed(1) + '%';
          const lab = document.createElement('b'); lab.textContent = node.text != null ? LUA_STR(node.text, 40) : (Math.round(100 * val / max) + '%');
          bar.appendChild(fill); bar.appendChild(lab); el.appendChild(bar); break;
        }
        case 'table': {
          el = document.createElement('div'); el.style.overflowX = 'auto';
          const tb = document.createElement('table'); tb.className = 'lua-table';
          if (Array.isArray(node.columns) && node.columns.length) {
            const tr = document.createElement('tr');
            for (const c of node.columns.slice(0, 16)) { const th = document.createElement('th'); th.textContent = LUA_STR(c, 60); tr.appendChild(th); }
            tb.appendChild(tr);
          }
          for (const r of (Array.isArray(node.rows) ? node.rows : []).slice(0, 200)) {
            const tr = document.createElement('tr');
            for (const c of (Array.isArray(r) ? r : [r]).slice(0, 16)) { const td = document.createElement('td'); td.textContent = LUA_STR(c, 200); tr.appendChild(td); }
            tb.appendChild(tr);
          }
          el.appendChild(tb); break;
        }
        case 'kv': {
          el = document.createElement('div'); el.className = 'lua-kv';
          for (const it of (Array.isArray(node.items) ? node.items : []).slice(0, 100)) {
            const k = document.createElement('span'); k.className = 'k'; k.textContent = LUA_STR(Array.isArray(it) ? it[0] : (it && it.k), 80);
            const v = document.createElement('span'); v.className = 'v'; v.textContent = LUA_STR(Array.isArray(it) ? it[1] : (it && it.v), 200);
            el.appendChild(k); el.appendChild(v);
          }
          break;
        }
        case 'badge': el = document.createElement('span'); el.className = 'lua-badge' + (['ok', 'warn', 'err'].indexOf(node.tone) >= 0 ? ' ' + node.tone : ''); el.textContent = LUA_STR(node.text, 60); break;
        case 'sep': el = document.createElement('hr'); el.className = 'lua-sep'; break;
        case 'spacer': el = document.createElement('div'); el.style.height = Math.max(0, Math.min(200, Number(node.h) || 8)) + 'px'; break;
        default: el = document.createElement('div'); el.className = 'lua-t muted'; el.textContent = '[unknown widget: ' + LUA_STR(t, 40) + ']'; break;
      }
      return el;
    };
    for (const n of list) { const el = build(n, 1); if (el) m.body.appendChild(el); }
    if (focusId) {
      const again = m.body.querySelector('[data-lua-id="' + focusId.replace(/"/g, '') + '"]');
      if (again) { try { again.focus(); if (focusSel && again.setSelectionRange) again.setSelectionRange(focusSel[0], focusSel[1]); } catch (e) {} }
    }
  }
