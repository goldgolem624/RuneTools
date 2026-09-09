// RuneToolsX panel: Rendering (hide NPCs/players/scene via the companion).
(function () {

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
    const gpuTitle = document.createElement('div'); gpuTitle.className = 'section-title'; gpuTitle.textContent = 'GPU frame (Vulkan)';
    const gpu = document.createElement('div'); gpu.id = 'rndGpu'; gpu.className = 'ov-hint'; gpu.textContent = 'No timing data. The Vulkan client publishes per-pass GPU times once its companion is armed.';
    wrap.appendChild(gpuTitle); wrap.appendChild(gpu);
    c.appendChild(wrap);
    const tick = async () => {
      const el = $('rndGpu');
      if (!el || !document.body.contains(el)) { clearInterval(timer); return; }
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
