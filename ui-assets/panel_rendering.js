// RuneToolsX panel: Rendering (hide NPCs/players/scene via the companion).
(function () {

  // Client options as the game holds them (name, value label, range), from the clientState bridge.
  let rndOptSig = '';
  const rndWords = n => String(n).replace(/([a-z])([A-Z])/g, '$1 $2');
  function rndPaintOptions(cs) {
    const box = $('rndOpts'); if (!box) return;
    const named = cs && Array.isArray(cs.optionsNamed) ? cs.optionsNamed : null;
    if (!named || !named.length) { box.style.display = 'none'; rndOptSig = ''; return; }
    const cells = [];
    if (cs.windowModeLabel) cells.push(['Window mode', cs.windowModeLabel, '']);
    if (cs.presetLabel) cells.push(['Preset', cs.presetLabel, '']);
    if (Array.isArray(cs.fullscreenSize) && cs.fullscreenSize[0] > 0) cells.push(['Fullscreen size', cs.fullscreenSize[0] + ' x ' + cs.fullscreenSize[1], '']);
    for (const o of named) {
      if (!o || !o.name) continue;
      cells.push([rndWords(o.name), o.label || String(o.value), 'option ' + o.id + (o.label ? ', value ' + o.value : '') + (o.min != null && o.max != null ? ', range ' + o.min + '..' + o.max : '')]);
    }
    const sig = JSON.stringify(cells);
    if (sig === rndOptSig) return;
    rndOptSig = sig;
    box.innerHTML = '';
    const h = document.createElement('div'); h.className = 'stor-h'; h.textContent = 'Client options'; box.appendChild(h);
    const g = document.createElement('div'); g.className = 'cs-grid';
    for (const c of cells) {
      const cell = document.createElement('div'); cell.className = 'cs-cell'; if (c[2]) cell.title = c[2];
      const k = document.createElement('span'); k.className = 'k'; k.textContent = c[0];
      const v = document.createElement('span'); v.className = 'v'; v.textContent = c[1];
      cell.appendChild(k); cell.appendChild(v); g.appendChild(cell);
    }
    box.appendChild(g); box.style.display = '';
  }
  function renderRendering() {
    const c = $('content');
    if ($('rndWrap')) return;          // built once; toggle state lives in renderHide
    c.innerHTML = '';
    const wrap = document.createElement('div'); wrap.id = 'rndWrap'; wrap.className = 'ov-wrap';
    wrap.appendChild(ovToggleRow('rh_npcs', 'Hide NPCs', 'Stop drawing NPC models'));
    wrap.appendChild(ovToggleRow('rh_players', 'Hide other players', 'Stop drawing other player models (keeps you)'));
    wrap.appendChild(ovToggleRow('rh_all', 'Hide everything', 'Blank the whole 3D scene (legacy Insert key)'));
    const hint = document.createElement('div'); hint.className = 'ov-hint';
    hint.textContent = 'Client-side only - these never reach the game or other players, and reset when you ' +
                       'restart. Needs the in-process companion (the same one that powers the Vars/Scene tabs).';
    wrap.appendChild(hint);
    const opts = document.createElement('div'); opts.id = 'rndOpts'; opts.className = 'stor-box'; opts.style.cssText = 'display:none;margin:4px 8px 8px';
    wrap.appendChild(opts);
    const gpuTitle = document.createElement('div'); gpuTitle.className = 'section-title'; gpuTitle.textContent = 'GPU frame (Vulkan)';
    const gpu = document.createElement('div'); gpu.id = 'rndGpu'; gpu.className = 'ov-hint'; gpu.textContent = 'No timing data. The Vulkan client publishes per-pass GPU times once its companion is armed.';
    wrap.appendChild(gpuTitle); wrap.appendChild(gpu);
    c.appendChild(wrap);
    const tick = async () => {
      const el = $('rndGpu');
      if (!el || !document.body.contains(el)) { clearInterval(timer); return; }
      try { if (bridge() && bridge().clientState) rndPaintOptions(await rtxData.call('state.clientState')); } catch (e) {}
      let d = null;
      try { if (bridge() && bridge().gpuTiming) d = JSON.parse(await Promise.resolve(bridge().gpuTiming(myPid())) || '{}'); } catch (e) {}
      if (!d || !Array.isArray(d.passes) || !d.passes.length) return;
      const rows = d.passes.map((p, i) => '<div style="display:flex;gap:8px;font:11px var(--font-mono)"><span style="flex:none;width:22px;color:var(--text-mute)">' + i + '</span>'
        + '<span style="flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap">' + String(p.desc).replace(/</g, '&lt;') + '</span>'
        + '<span style="flex:none;width:52px;text-align:right;color:var(--text-mute)">' + p.draws + '</span>'
        + '<span style="flex:none;width:64px;text-align:right">' + (p.us / 1000).toFixed(2) + ' ms</span></div>');
      el.innerHTML = '<div style="margin-bottom:4px">Frame ' + d.frame + ': GPU ' + (d.total_us / 1000).toFixed(2) + ' ms across ' + d.passes.length + ' passes, present interval ' + (d.frame_us / 1000).toFixed(1) + ' ms</div>' + rows.join('');
    };
    const timer = setInterval(tick, 1000); tick();
    [['rh_npcs', 'npcs', 0], ['rh_players', 'players', 1], ['rh_all', 'all', 2]].forEach(([id, key, which]) => {
      const el = $(id); if (!el) return;
      el.classList.toggle('on', !!renderHide[key]);
      el.addEventListener('click', () => {
        renderHide[key] = !renderHide[key];
        el.classList.toggle('on', renderHide[key]);
        try { if (bridge() && bridge().renderToggle) rtxData.sync('act.renderToggle', which, renderHide[key]); } catch (e) {}
      });
    });
  }

registerTab({ id: 'rendering', render: renderRendering });
})();
