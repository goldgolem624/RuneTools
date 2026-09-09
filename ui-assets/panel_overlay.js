// RuneToolsX panel: Overlay tab (drives the external world-grid window).
(function () {
  overlayState = { enabled: false, grid: true, players: false, npcs: false, objects: false, specials: false, walk_only: false, interactable: false, radius: 12, markers: true, occlude: true };
  function ovAdopt(raw) {
    try {
      const sv = JSON.parse(raw || 'null');
      if (!sv || typeof sv !== 'object') return false;
      for (const k in overlayState) {
        if (typeof sv[k] === typeof overlayState[k]) overlayState[k] = sv[k];
      }
      overlayState.radius = Math.max(1, Math.min(64, overlayState.radius | 0)) || 12;
      return true;
    } catch (e) { return false; }
  }
  ovAdopt((function () { try { return localStorage.getItem('rtxOverlayCfg'); } catch (e) { return null; } })());
  function ovApplyDurablePrefs() {
    if (!ovAdopt(prefGet('rtxOverlayCfg', null))) return;
    try { pushOverlay(); } catch (e) {}
    try { paneRun('overlay', renderOverlay); } catch (e) {}
  }
  function saveOverlayCfg() { prefSet('rtxOverlayCfg', JSON.stringify(overlayState)); }
  renderHide = { npcs: false, players: false, all: false };
  function pushOverlay() {
    const b = bridge();
    saveOverlayCfg();          // every control routes through here, so this is the one hook
    if (!b || !b.overlayConfig) return;
    const s = overlayState;
    const npOn  = (typeof sceneNameplates !== 'undefined') && sceneNameplates;
    const npPl  = false;
    const npNp  = (typeof sceneShow !== 'undefined') ? !!sceneShow.npcs    : true;
    const npOb  = (typeof sceneShow !== 'undefined') ? !!sceneShow.objects : false;
    const npRng = (typeof sceneRange !== 'undefined') ? sceneRange : 20;
    const npInter = npOn && (typeof sceneInteractable !== 'undefined') && !!sceneInteractable;
    try { b.overlayConfig(myPid(), s.enabled, s.grid, s.players, s.npcs, s.objects, s.radius, s.walk_only, (s.interactable || npInter), s.specials, markersActive(),
                          npOn, npPl, npNp, npOb, npRng, overlayState.occlude !== false); } catch (e) {}
  }
  function ovToggleRow(id, name, sub) {
    const r = document.createElement('div'); r.className = 'ov-row'; r.id = id; r.setAttribute('role', 'button');
    const l = document.createElement('div'); l.className = 'ov-l';
    const n = document.createElement('div'); n.className = 'ov-name'; n.textContent = name;
    l.appendChild(n);
    if (sub) { const s = document.createElement('div'); s.className = 'ov-sub'; s.textContent = sub; l.appendChild(s); }
    const pill = document.createElement('div'); pill.className = 'ov-pill'; pill.appendChild(document.createElement('span'));
    r.appendChild(l); r.appendChild(pill);
    return r;
  }
  function reflectOverlay() {
    const map = { ov_enabled: 'enabled', ov_grid: 'grid', ov_players: 'players', ov_npcs: 'npcs', ov_objects: 'objects', ov_interactable: 'interactable', ov_occlude: 'occlude', ov_specials: 'specials', ov_walkonly: 'walk_only' };
    for (const id in map) { const el = $(id); if (el) el.classList.toggle('on', !!overlayState[map[id]]); }
    const rng = $('ov_radius');
    if (rng) { rng.value = overlayState.radius; const lbl = $('ov_radlbl'); if (lbl) lbl.textContent = overlayState.radius + ' tiles'; }
    const dim = !overlayState.enabled;
    ['ov_grid', 'ov_players', 'ov_npcs', 'ov_objects', 'ov_interactable', 'ov_specials', 'ov_walkonly', 'ov_radius'].forEach(id => {
      const el = $(id); if (el) { const r = el.closest('.ov-row'); if (r) r.style.opacity = dim ? 0.45 : 1; }
    });
    const wo = $('ov_walkonly'); if (wo && !dim) wo.style.opacity = overlayState.grid ? 1 : 0.45;
  }
  function renderOverlay() {
    const c = $('content');
    if ($('ovWrap')) { reflectOverlay(); return; }
    c.innerHTML = '';
    const wrap = document.createElement('div'); wrap.id = 'ovWrap'; wrap.className = 'ov-wrap';
    wrap.appendChild(ovToggleRow('ov_enabled', 'Enable overlay', 'Draw markers into the game\'s 3D view'));
    const sep = document.createElement('div'); sep.className = 'ov-sep'; wrap.appendChild(sep);
    wrap.appendChild(ovToggleRow('ov_grid', 'Tile grid', 'World grid lines on the ground'));
    wrap.appendChild(ovToggleRow('ov_walkonly', 'Walkable only', 'Hide unwalkable tiles (else they tint red)'));
    wrap.appendChild(ovToggleRow('ov_occlude', 'Fade behind scenery', 'Marker parts behind terrain or objects are drawn faint instead of hidden (Vulkan client)'));
    wrap.appendChild(ovToggleRow('ov_players', 'Players', 'Marker + name per player'));
    wrap.appendChild(ovToggleRow('ov_npcs', 'NPCs', 'Marker + name per NPC'));
    wrap.appendChild(ovToggleRow('ov_objects', 'Objects', 'Footprint box per named scenery object'));
    wrap.appendChild(ovToggleRow('ov_interactable', 'Interactable only', 'Only NPCs/objects that have right-click actions'));
    wrap.appendChild(ovToggleRow('ov_specials', 'Specials', 'Time Sprites, Rockertunities + other type-4 spawns'));
    const rr = document.createElement('div'); rr.className = 'ov-row ov-slider';
    const srow = document.createElement('div'); srow.className = 'ov-srow';
    const rn = document.createElement('div'); rn.className = 'ov-name'; rn.textContent = 'Grid radius';
    const rs = document.createElement('div'); rs.className = 'ov-sval'; rs.id = 'ov_radlbl';
    srow.appendChild(rn); srow.appendChild(rs);
    const rng = document.createElement('input');
    rng.type = 'range'; rng.min = '2'; rng.max = '30'; rng.id = 'ov_radius';
    rng.addEventListener('input', () => {
      overlayState.radius = parseInt(rng.value, 10) || 12;
      const lbl = $('ov_radlbl'); if (lbl) lbl.textContent = overlayState.radius + ' tiles';
      pushOverlay();
    });
    rr.appendChild(srow); rr.appendChild(rng); wrap.appendChild(rr);
    const hint = document.createElement('div'); hint.className = 'ov-hint';
    hint.textContent = 'Markers are drawn into the game\'s 3D view. Grid follows your current plane height.';
    wrap.appendChild(hint);
    c.appendChild(wrap);
    [['ov_enabled', 'enabled'], ['ov_grid', 'grid'], ['ov_walkonly', 'walk_only'], ['ov_occlude', 'occlude'], ['ov_players', 'players'],
     ['ov_npcs', 'npcs'], ['ov_objects', 'objects'], ['ov_interactable', 'interactable'], ['ov_specials', 'specials']].forEach(([id, key]) => {
      $(id).addEventListener('click', () => { overlayState[key] = !overlayState[key]; reflectOverlay(); pushOverlay(); });
    });
    reflectOverlay();
  }

Object.assign(window, { ovApplyDurablePrefs, ovToggleRow, pushOverlay, saveOverlayCfg });
registerTab({ id: 'overlay', render: renderOverlay });
})();
