// rtx-plugin-hud.js: the host-rendered ability HUD strip a plugin drives through overlay.hudAbilities (one per plugin, draggable, icons resolved from the cache)
// Loads after: rtx-wm.js (wmRectsSoon/wmPushRects) and rtx-plugin-api.js.
// Plain script, page globals by design: every top-level name here is a page global that panels and the other core files use.
  // ---- plugin ability HUD: a small floating strip over the game, host-rendered ----
  // Payload: { title, sub, cur:[{id,key}], next:[{id,gap}] }. Lives in the in-game UI layer
  // (this page composites into the game frame), draggable by its header, one per plugin.
  const pluginHuds = new Map();          // plugin id -> { el, body, ttl, sub }
  // Dragged position, kept across close/recreate: an empty tick or a loop wrap closes the
  // strip and the next push rebuilds it, which must not teleport it back to the default spot.
  const pluginHudPos = new Map();        // plugin id -> { left, top }
  const pluginHudIcons = new Map();      // ability id -> data URL promise result ('' = none)
  function pluginHudIcon(id, el) {
    if (pluginHudIcons.has(id)) { const u = pluginHudIcons.get(id); if (u) el.style.backgroundImage = 'url(' + u + ')'; return; }
    try {
      Promise.resolve(bridge().sprite(id)).then(u => {
        pluginHudIcons.set(id, u || '');
        if (u && el.isConnected) el.style.backgroundImage = 'url(' + u + ')';
      }).catch(() => pluginHudIcons.set(id, ''));
    } catch (e) {}
  }
  function pluginHudClose(slug) {
    const h = pluginHuds.get(slug);
    if (h) { try { h.el.remove(); } catch (e) {} pluginHuds.delete(slug); wmRectsSoon(); }
  }
  function pluginHudSet(slug, payload) {
    const cur = payload && Array.isArray(payload.cur) ? payload.cur.slice(0, 6) : [];
    // Only an explicit null closes the strip. An empty tick (nothing firing right now, e.g.
    // the tail before a loop wraps) keeps it alive showing the title/sub and upcoming icons,
    // instead of blinking out and back.
    if (!payload) { pluginHudClose(slug); return; }
    let h = pluginHuds.get(slug);
    if (!h) {
      const el = document.createElement('div');
      el.style.cssText = 'position:fixed;z-index:950000;top:110px;left:50%;transform:translateX(-50%);' +
        'background:rgba(12,13,18,0.92);border:1px solid rgba(140,111,253,0.45);border-radius:10px;' +
        'padding:6px 10px 8px;min-width:180px;box-shadow:0 8px 24px rgba(0,0,0,0.5);user-select:none';
      const p0 = pluginHudPos.get(slug);
      if (p0) {
        el.style.transform = 'none';
        el.style.left = Math.max(0, Math.min(p0.left, (window.innerWidth || 1280) - 60)) + 'px';
        el.style.top  = Math.max(0, Math.min(p0.top,  (window.innerHeight || 720) - 40)) + 'px';
      }
      const head = document.createElement('div');
      head.style.cssText = 'display:flex;align-items:center;gap:8px;cursor:grab;margin-bottom:5px';
      const ttl = document.createElement('span');
      ttl.style.cssText = 'flex:1;font-size:11px;font-weight:700;color:#e8edf4;white-space:nowrap;overflow:hidden;text-overflow:ellipsis';
      const sub = document.createElement('span');
      sub.style.cssText = 'font-size:10px;color:#99a0b3;white-space:nowrap';
      const x = document.createElement('span');
      x.textContent = '\u00d7';
      x.style.cssText = 'cursor:pointer;color:#99a0b3;font-size:13px;padding:0 2px';
      x.addEventListener('click', () => pluginHudClose(slug));
      head.appendChild(ttl); head.appendChild(sub); head.appendChild(x);
      const body = document.createElement('div');
      body.style.cssText = 'display:flex;align-items:center;gap:5px';
      el.appendChild(head); el.appendChild(body);
      // header drag (screen px both sides; this layer is unzoomed)
      head.addEventListener('mousedown', (e) => {
        if (e.target === x) return;
        e.preventDefault();
        const r = el.getBoundingClientRect();
        el.style.transform = 'none'; el.style.left = r.left + 'px'; el.style.top = r.top + 'px';
        const sx = e.clientX - r.left, sy = e.clientY - r.top;
        const mv = ev => { el.style.left = Math.max(0, ev.clientX - sx) + 'px'; el.style.top = Math.max(0, ev.clientY - sy) + 'px'; wmPushRects(); };
        const up = () => {
          document.removeEventListener('mousemove', mv); document.removeEventListener('mouseup', up);
          pluginHudPos.set(slug, { left: parseFloat(el.style.left) || 0, top: parseFloat(el.style.top) || 0 });
          wmPushRects();
        };
        document.addEventListener('mousemove', mv); document.addEventListener('mouseup', up);
      });
      document.body.appendChild(el);
      h = { el, body, ttl, sub };
      pluginHuds.set(slug, h);
    }
    wmRectsSoon();   // size follows the payload (icon count), so refresh the consume rect every set
    h.ttl.textContent = pClampStr(payload.title, 40) || 'Rotation';
    h.sub.textContent = pClampStr(payload.sub, 60);
    h.body.innerHTML = '';
    const isNow = payload.now !== false;   // absent = old plugin builds -> green as before
    const tile = (id, big, key, dim) => {
      const d = document.createElement('div');
      const sz = big ? 44 : 26;
      d.style.cssText = 'position:relative;width:' + sz + 'px;height:' + sz + 'px;border-radius:6px;flex:none;' +
        'border:1px solid ' + (big ? (isNow ? 'rgba(77,210,138,0.8)' : 'rgba(245,178,65,0.75)') : 'rgba(255,255,255,0.12)') + ';' +
        'background:#20222c center/' + (sz - 6) + 'px no-repeat;' + (dim ? 'opacity:0.62;' : '');
      pluginHudIcon(pClampId(id), d);
      if (key) {
        const k = document.createElement('span');
        k.textContent = pClampStr(key, 6);
        k.style.cssText = 'position:absolute;bottom:-3px;right:-3px;background:#0c0d12;border:1px solid rgba(255,255,255,0.18);' +
          'border-radius:4px;font-size:10px;font-weight:700;padding:0 3px;color:#9d83ff';
        d.appendChild(k);
      }
      return d;
    };
    for (const it of cur) h.body.appendChild(tile(it && it.id, true, it && it.key, false));
    const next = Array.isArray(payload.next) ? payload.next.slice(0, 6) : [];
    for (const it of next) {
      const gap = document.createElement('span');
      gap.textContent = '+' + Math.max(0, Math.min(99, (it && it.gap) | 0));
      gap.style.cssText = 'font-size:9px;color:#5e6580;flex:none';
      h.body.appendChild(gap);
      h.body.appendChild(tile(it && it.id, false, '', true));
    }
  }
