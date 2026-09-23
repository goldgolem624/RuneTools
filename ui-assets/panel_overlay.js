// RuneToolsX panel: Overlay tab (drives the external world-grid window).
(function () {
  overlayState = { enabled: false, grid: true, players: false, npcs: false, objects: false, specials: false, walk_only: false, true_tile: false, interactable: false, radius: 12, markers: true, occlude: true, occlude_hide: true, inframe_trial: true, inframe_default_v1: true, hover_outline: false, hover_when: -1, hover_style: -1, hover_self: 0, hover_pl: 0, hover_npc: 0, hover_atk: 0, hover_obj: 0, hover_loot: 0, hover_width: 8, hover_self_w: 0, hover_pl_w: 0, hover_npc_w: 0, hover_atk_w: 0, hover_obj_w: 0, hover_loot_w: 0, tooltip_values: true, mark_test: false, mark_height: 60, mark_pointer_scale: 100, mark_arrow: 1, mark_target: 0, mark_query: '', mark_path: 1 };
  // key in overlayState, label, the game's own colour for that kind
  // The game's own look, as the game itself shows it: its chevrons at the feet, its yellow arrow, its
  // diamonds along the way and a wide frame on a marked tile. None of that is a choice. The arrows show
  // from as far as the game can be asked for; 90 tiles is the most its own arithmetic holds.
  const OV_MARK_LOOK = { tile: 140133, chevrons: 92026, arrow: 0, range: 90 };
  const OV_MARK_SLIDERS = [['mark_height', 'Arrow height', 0, 255, ''], ['mark_pointer_scale', 'Chevron size', 100, 400, '%']];
  const OV_MARK_CHOICES = [['mark_target', 'Point at', [[0, 'NPC'], [1, 'Object'], [2, 'Tile']]],
                           ['mark_arrow', 'Arrow', [[1, 'On'], [0, 'Off']]],
                           ['mark_path', 'Trail', [[1, 'On'], [0, 'Off']]]];
  // key of the colour, label, the game's own colour for it, key of the outline width
  // The game's own highlight categories, each confirmed by setting a distinct colour in the game
  // and reading the table back. Friendly players and loot each occupy two slots, which the launcher
  // keeps in step.
  const OV_HOVER_KINDS = [
    ['hover_self', 'Yourself',          0xFFD300, 'hover_self_w', 0],
    ['hover_pl',   'Friendly players',  0x821FFF, 'hover_pl_w',   1],
    ['hover_npc',  'Friendly NPCs',     0xFC8EAC, 'hover_npc_w',  3],
    ['hover_atk',  'Enemies',           0xFF0D16, 'hover_atk_w',  4],
    ['hover_obj',  'Interactables',     0x1AEBFF, 'hover_obj_w',  5],
    ['hover_loot', 'Loot',              0x1AFF1A, 'hover_loot_w', 6]];
  const OV_HOVER_COLOURS = [0x46E0C0, 0x57C6E0, 0x8BE05A, 0xF0C04A, 0xE0564E, 0xC07AE0, 0xE0903C, 0xECEFF3];
  function ovAdopt(raw) {
    try {
      const sv = JSON.parse(raw || 'null');
      if (!sv || typeof sv !== 'object') return false;
      for (const k in overlayState) {
        if (typeof sv[k] === typeof overlayState[k]) overlayState[k] = sv[k];
      }
      overlayState.radius = Math.max(1, Math.min(64, overlayState.radius | 0)) || 12;
      overlayState.hover_width = Math.max(2, Math.min(16, overlayState.hover_width | 0)) || 8;
      // one width for everything came first: it seeds the three that replaced it
      OV_HOVER_KINDS.forEach(([, , , wKey]) => { overlayState[wKey] = Math.max(2, Math.min(16, (overlayState[wKey] | 0) || overlayState.hover_width)); });
      // settings saved while these were a trial: Hide and drawing inside the game are the defaults now,
      // and a saved copy from before that day is brought up to them once
      if (sv.inframe_default_v1 !== true) { overlayState.occlude = true; overlayState.occlude_hide = true; overlayState.inframe_trial = true; overlayState.tooltip_values = true; overlayState.inframe_default_v1 = true; }
      overlayState.mark_test = false;   // never comes back on by itself: a target is asked for again by whoever wants it
      return true;
    } catch (e) { return false; }
  }
  OV_HOVER_KINDS.forEach(([, , , wKey]) => { overlayState[wKey] = overlayState[wKey] || overlayState.hover_width; });
  ovAdopt((function () { try { return localStorage.getItem('rtxOverlayCfg'); } catch (e) { return null; } })());
  function ovApplyDurablePrefs() {
    if (!ovAdopt(prefGet('rtxOverlayCfg', null))) return;
    try { pushOverlay(); } catch (e) {}
    try { paneRun('overlay', renderOverlay); } catch (e) {}
  }
  function saveOverlayCfg() { prefSet('rtxOverlayCfg', JSON.stringify(overlayState)); }
  renderHide = { npcs: false, players: false, all: false };
  // Where the thing to point at is right now: kind -1 = not in view, 0 = a tile, 1 = an NPC (uid = its index in the game).
  let ovMarkFound = { kind: -1, x: 0, y: 0, plane: 0, uid: -1, text: '' };
  let ovMarkTimer = null;
  const OV_MARK_REACH = 48;   // tiles from the player; the launcher and the game module hold the same line
  // Requests from callers (plugins, guides), newest last. One stands at a time and wins over the panel's own
  // setting: the game has one arrow to give.
  const ovPointRequests = new Map();   // source -> { target: 0 npc | 1 object | 2 tile, query }
  const OV_POINT_KINDS = { npc: 0, object: 1, tile: 2 };
  function ovPointAt(source, kind, query) {
    const key = String(source || 'host');
    const target = OV_POINT_KINDS[String(kind || '').toLowerCase()];
    const text = String(query == null ? '' : query).trim().slice(0, 96);
    ovPointRequests.delete(key);
    if (target !== undefined && text) ovPointRequests.set(key, { target, query: text });
    pushOverlay();
    ovMarkResolve();
    return true;
  }
  function ovMarkWanted() {
    let last = null;
    ovPointRequests.forEach(r => { last = r; });
    if (last) return last;
    return overlayState.mark_test ? { target: overlayState.mark_target | 0, query: overlayState.mark_query } : null;
  }
  function ovMarkMatches(entity, query) {
    if (/^\d+$/.test(query)) return (entity.id | 0) === parseInt(query, 10);
    return String(entity.name || '').toLowerCase().indexOf(query) >= 0;
  }
  async function ovMarkResolve() {
    const b = bridge(), wanted = ovMarkWanted();
    const query = String((wanted && wanted.query) || '').trim().toLowerCase();
    const target = wanted ? wanted.target : 0;
    let me = null;
    let found = { kind: -1, x: 0, y: 0, plane: 0, uid: -1, text: query ? 'Not in view' : 'Nothing entered' };
    if (wanted && query && b && b.sceneEntities) {
      let scene = null;
      try { scene = JSON.parse(await b.sceneEntities(myPid(), 64) || 'null'); } catch (e) {}
      me = scene && (scene.players || []).find(p => p.self);
      const plane = me ? (me.plane | 0) : 0;
      if (target === 2) {
        const n = query.split(/[^0-9]+/).filter(Boolean).map(v => parseInt(v, 10));
        // the game can only mark ground it has loaded: a tile out of reach of the player is refused
        const far = !me || Math.max(Math.abs(n[0] - (me.x | 0)), Math.abs(n[1] - (me.y | 0))) > OV_MARK_REACH;
        if (n.length < 2) found.text = 'Enter x, y';
        else if (far) found.text = 'Too far away';
        else found = { kind: 0, x: n[0], y: n[1], plane: n.length >= 3 ? Math.min(3, n[2]) : plane, uid: -1, text: 'Tile ' + n[0] + ', ' + n[1] };
      } else if (scene) {
        const list = (target === 0 ? scene.npcs : scene.objects) || [];
        let best = null, bestD = 1e9;
        list.forEach(e => {
          if (!ovMarkMatches(e, query)) return;
          const d = me ? Math.max(Math.abs((e.x | 0) - (me.x | 0)), Math.abs((e.y | 0) - (me.y | 0))) : 0;
          if (d < bestD && d <= OV_MARK_REACH) { best = e; bestD = d; }
        });
        if (best) {
          const npc = target === 0;
          found = { kind: npc ? 1 : 0, x: best.x | 0, y: best.y | 0, plane: best.plane === undefined ? plane : (best.plane | 0), uid: npc ? (best.uid | 0) : -1,
                    text: (best.name || ('#' + best.id)) + (me ? ', ' + bestD + (bestD === 1 ? ' tile away' : ' tiles away') : '') };
        }
      }
    }
    found.fromX = me ? (me.x | 0) : 0; found.fromY = me ? (me.y | 0) : 0;
    const moved = found.fromX !== ovMarkFound.fromX || found.fromY !== ovMarkFound.fromY || found.kind !== ovMarkFound.kind || found.x !== ovMarkFound.x || found.y !== ovMarkFound.y || found.plane !== ovMarkFound.plane || found.uid !== ovMarkFound.uid;
    ovMarkFound = found;
    const st = $('ov_markstatus'); if (st) st.textContent = wanted ? found.text : '';
    if (moved) pushOverlay();
  }
  function ovMarkWatch() {
    const want = !!ovMarkWanted();
    if (want && !ovMarkTimer) ovMarkTimer = setInterval(ovMarkResolve, 1000);
    if (!want && ovMarkTimer) { clearInterval(ovMarkTimer); ovMarkTimer = null; }
    ovMarkResolve();
  }
  function pushOverlay() {
    const b = bridge();
    saveOverlayCfg();          // every control routes through here, so this is the one hook
    if (!!ovMarkWanted() !== !!ovMarkTimer) ovMarkWatch();   // the target search runs with or without this tab open
    if (!b || !b.overlayConfig) return;
    const s = overlayState;
    const npOn  = (typeof sceneNameplates !== 'undefined') && sceneNameplates;
    const npPl  = false;
    const npNp  = (typeof sceneShow !== 'undefined') ? !!sceneShow.npcs    : true;
    const npOb  = (typeof sceneShow !== 'undefined') ? !!sceneShow.objects : false;
    const npRng = (typeof sceneRange !== 'undefined') ? sceneRange : 20;
    const npInter = npOn && (typeof sceneInteractable !== 'undefined') && !!sceneInteractable;
    try { b.overlayConfig(myPid(), s.enabled, s.grid, s.players, s.npcs, s.objects, s.radius, s.walk_only, (s.interactable || npInter), s.specials, markersActive(),
                          npOn, npPl, npNp, npOb, npRng, overlayState.occlude !== false, s.true_tile, !!s.hover_outline, s.hover_obj | 0, s.hover_npc | 0, s.hover_atk | 0, s.hover_obj_w | 0, !!s.tooltip_values,
                          !!ovMarkWanted(), OV_MARK_LOOK.tile, OV_MARK_LOOK.arrow, s.mark_height | 0, OV_MARK_LOOK.chevrons,
                          0, 0, 0, 0, OV_MARK_LOOK.range, s.mark_pointer_scale | 0, 0, !!s.mark_arrow,
                          ovMarkFound.kind, ovMarkFound.x | 0, ovMarkFound.y | 0, ovMarkFound.plane | 0, ovMarkFound.uid,
                          !!s.mark_path, ovMarkFound.fromX | 0, ovMarkFound.fromY | 0,
                          s.hover_npc_w | 0, s.hover_atk_w | 0, !!s.occlude_hide, !!s.inframe_trial,
                          s.hover_when | 0, s.hover_style | 0, ovHoverSpec('c'), ovHoverSpec('w')); } catch (e) {}
  }
  // "category:value,..." for the categories that are set; the launcher leaves the rest to the game.
  function ovHoverSpec(which) {
    const out = [];
    OV_HOVER_KINDS.forEach(([key, , , wKey, cat]) => {
      const v = which === 'c' ? (overlayState[key] | 0) : (overlayState[wKey] | 0);
      if (which === 'c') { if (v) out.push(cat + ':' + v); }
      else if (v > 0) out.push(cat + ':' + v);
    });
    return out.join(',');
  }
  function ovHex(rgb) { return '#' + ('000000' + ((rgb | 0) & 0xFFFFFF).toString(16)).slice(-6); }
  // A dropdown row in the same shape as a toggle row. The app's own dark dropdown is used rather
  // than a native select, which Ultralight draws with an OS-white popup.
  function ovSelectRow(id, key, name, sub, opts) {
    const r = document.createElement('div'); r.className = 'ov-row'; r.dataset.sel = key;
    const l = document.createElement('div'); l.className = 'ov-l';
    const n = document.createElement('div'); n.className = 'ov-name'; n.textContent = name;
    l.appendChild(n);
    if (sub) { const s = document.createElement('div'); s.className = 'ov-sub'; s.textContent = sub; l.appendChild(s); }
    const dd = document.createElement('div'); dd.className = 'pet-dd'; dd.id = id;
    dd.style.cssText = 'flex:0 0 auto;min-width:132px;max-width:150px;';
    const btn = document.createElement('button'); btn.type = 'button'; btn.className = 'pet-dd-btn'; dd.appendChild(btn);
    const pop = document.createElement('div'); pop.className = 'pet-dd-pop'; pop.style.right = '0'; pop.style.left = 'auto'; dd.appendChild(pop);
    let shown = null;
    const paint = () => {
      const cur = overlayState[key] | 0;
      const sel = opts.find(([v]) => v === cur) || opts[0];
      btn.textContent = sel[1];
      // The panel repaints on a timer. Rebuilding the list while it is open takes the option out
      // from under the cursor before the click lands, so it is only built when it has to be.
      if (dd.classList.contains('open') || shown === cur) return;
      shown = cur;
      pop.innerHTML = '';
      opts.forEach(([v, t]) => {
        const o = document.createElement('div'); o.className = 'pet-dd-opt' + (v === cur ? ' on' : '');
        o.textContent = t;
        o.addEventListener('click', (e) => {
          e.stopPropagation(); dd.classList.remove('open');
          overlayState[key] = v; shown = null; paint(); pushOverlay();
        });
        pop.appendChild(o);
      });
    };
    btn.addEventListener('click', (e) => {
      e.stopPropagation();
      const open = dd.classList.contains('open');
      document.querySelectorAll('.pet-dd.open').forEach(x => x.classList.remove('open'));
      if (!open) { shown = null; paint(); dd.classList.add('open'); }
    });
    dd.repaint = paint;
    paint();
    r.appendChild(l); r.appendChild(dd);
    return r;
  }
  if (!window._petDDClose) { window._petDDClose = true; document.addEventListener('click', () => document.querySelectorAll('.pet-dd.open').forEach(x => x.classList.remove('open'))); }
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
  // Rows that only mean something on the Vulkan client are shown there only. The renderer comes
  // from the client snapshot; until it is known the rows stay, since hiding them by mistake is worse.
  function ovIsVulkan() {
    const g = (lastSnap && lastSnap.gfx_mode) ? String(lastSnap.gfx_mode) : '';
    return g === '' || /vulkan/i.test(g);
  }
  function ovReflectRenderer() {
    const vk = ovIsVulkan();
    ['ov_behind', 'ov_inframe'].forEach(id => { const el = $(id); if (el) el.style.display = vk ? '' : 'none'; });
  }
  function reflectOverlay() {
    ovReflectRenderer();
    const map = { ov_enabled: 'enabled', ov_grid: 'grid', ov_players: 'players', ov_npcs: 'npcs', ov_objects: 'objects', ov_interactable: 'interactable', ov_inframe: 'inframe_trial', ov_specials: 'specials', ov_walkonly: 'walk_only', ov_truetile: 'true_tile', ov_hoverol: 'hover_outline', ov_tipvals: 'tooltip_values', ov_marktest: 'mark_test' };
    for (const id in map) { const el = $(id); if (el) el.classList.toggle('on', !!overlayState[map[id]]); }
    {
      const behind = overlayState.occlude === false ? 0 : (overlayState.occlude_hide ? 2 : 1);
      if ($('ovWrap')) Array.from($('ovWrap').querySelectorAll('.ov-behindchip')).forEach(chip => {
        const on = (chip.dataset.value | 0) === behind;
        chip.style.borderColor = on ? 'var(--accent)' : 'var(--border)';
        chip.style.color = on ? 'var(--accent)' : '';
      });
    }
    const rng = $('ov_radius');
    if (rng) { rng.value = overlayState.radius; const lbl = $('ov_radlbl'); if (lbl) lbl.textContent = overlayState.radius + ' tiles'; }
    { const qi = $('ov_markquery'); if (qi && document.activeElement !== qi) qi.value = overlayState.mark_query || ''; }
    ovMarkWatch();
    OV_MARK_SLIDERS.forEach(([key, , min, max, unit]) => {
      const v = Math.max(min, Math.min(max, overlayState[key] | 0)); overlayState[key] = v;
      const r = $('ov_' + key); if (r) r.value = v;
      const l = $('ov_' + key + '_lbl'); if (l) l.textContent = v + unit;
    });
    OV_MARK_CHOICES.forEach(([key, , choices]) => {
      if (!choices.some(([value]) => value === (overlayState[key] | 0))) overlayState[key] = choices[0][0];
    });
    if ($('ovWrap')) Array.from($('ovWrap').querySelectorAll('.ov-markrow[data-choice]')).forEach(row => {
      Array.from(row.querySelectorAll('.ov-markchip')).forEach(chip => {
        const on = (chip.dataset.value | 0) === (overlayState[row.dataset.choice] | 0);
        chip.style.borderColor = on ? 'var(--accent)' : 'var(--border)';
        chip.style.color = on ? 'var(--text)' : '';
      });
    });
    const ow = $('ovWrap');
    if (ow) Array.from(ow.querySelectorAll('.ov-row[data-sel]')).forEach(row => {
      row.style.opacity = overlayState.hover_outline ? 1 : 0.45;
      const dd = row.querySelector('.pet-dd');
      if (dd && dd.repaint) dd.repaint();
    });
    // a silhouette fills the entity and has no border, so the size rows have nothing to say
    if (ow) { const borders = (overlayState.hover_style | 0) !== 1;
      Array.from(ow.querySelectorAll('.ov-hovrow[data-width]')).forEach(row => {
        row.style.display = borders ? 'flex' : 'none';
      }); }
    if (ow) Array.from(ow.querySelectorAll('.ov-hovrow')).forEach(row => {
      row.style.opacity = overlayState.hover_outline ? 1 : 0.45;
      const wKey = row.dataset.width;
      if (wKey) { const hw = $('ov_' + wKey); if (hw) hw.value = overlayState[wKey]; const l = $('ov_' + wKey + '_lbl'); if (l) l.textContent = overlayState[wKey]; }
      if (!row.dataset.key) return;
      const cur = overlayState[row.dataset.key] | 0;
      let preset = false;
      Array.from(row.querySelectorAll('.mk-sw[data-col]')).forEach(sw => { const on = (sw.dataset.col | 0) === cur; preset = preset || on; sw.classList.toggle('sel', on); });
      const own = row.querySelector('.ov-owncol');
      if (own) { own.classList.toggle('sel', !preset); own.style.background = preset ? 'transparent' : ovHex(cur); own.textContent = preset ? '+' : ''; }
    });
    const dim = !overlayState.enabled;
    ['ov_grid', 'ov_players', 'ov_npcs', 'ov_objects', 'ov_interactable', 'ov_specials', 'ov_walkonly', 'ov_truetile', 'ov_radius'].forEach(id => {
      const el = $(id); if (el) { const r = el.closest('.ov-row'); if (r) r.style.opacity = dim ? 0.45 : 1; }
    });
    const wo = $('ov_walkonly'); if (wo && !dim) wo.style.opacity = overlayState.grid ? 1 : 0.45;
  }
  function renderOverlay() {
    const c = $('content');
    if ($('ovWrap')) { reflectOverlay(); return; }
    c.innerHTML = '';
    const wrap = document.createElement('div'); wrap.id = 'ovWrap'; wrap.className = 'ov-wrap';
    wrap.appendChild(ovToggleRow('ov_hoverol', 'Highlight entities', 'The game highlights NPCs and scenery itself. Works with the overlay off, and leaves your game settings alone where these say to'));
    wrap.appendChild(ovSelectRow('ov_hoverwhen', 'hover_when', 'When', 'Leave your game setting, or override it',
      [[-1, 'Game setting'], [0, 'Mouseover'], [1, 'Nearby'], [2, 'Always on']]));
    wrap.appendChild(ovSelectRow('ov_hoverstyle', 'hover_style', 'Style', 'A silhouette fills the whole entity; a border traces its edge at the sizes below',
      [[-1, 'Game setting'], [1, 'Silhouette'], [0, 'Border']]));
    // outline colour per kind of target; the first swatch keeps the colour the game uses for it
    OV_HOVER_KINDS.forEach(([key, label, game, wKey]) => {
      const row = document.createElement('div'); row.className = 'ov-hovrow'; row.dataset.key = key;
      row.style.cssText = 'display:flex;align-items:center;gap:8px;padding:0 10px 4px 18px;';
      const name = document.createElement('div'); name.className = 'ov-sub'; name.textContent = label;
      name.style.cssText = 'width:80px;flex:none;';
      const pal = document.createElement('div'); pal.className = 'mk-pal';
      pal.style.cssText = 'padding:0;flex:1 1 auto;min-width:0;display:flex;flex-wrap:wrap;gap:4px;';
      [0].concat(OV_HOVER_COLOURS).forEach(col => {
        const sw = document.createElement('div'); sw.className = 'mk-sw'; sw.dataset.col = col;
        sw.title = col ? '' : 'Game colour';
        sw.style.background = '#' + ('000000' + (col || game).toString(16)).slice(-6);
        if (!col) sw.style.borderStyle = 'dashed';
        sw.addEventListener('click', () => { overlayState[key] = col; reflectOverlay(); pushOverlay(); });
        pal.appendChild(sw);
      });
      {
        // any other colour: the picker the Auras panel has
        const own = document.createElement('div'); own.className = 'mk-sw ov-owncol'; own.title = 'Pick any colour';
        own.style.cssText = 'display:flex;align-items:center;justify-content:center;font-size:13px;line-height:1;color:var(--text-dim);border-style:dashed;';
        own.addEventListener('click', e => {
          e.stopPropagation();
          if (typeof auraColorPicker !== 'function') return;
          if (document.getElementById('sndMenu')) { if (typeof closeSoundMenu === 'function') closeSoundMenu(); return; }
          auraColorPicker(own, ovHex((overlayState[key] | 0) || game), hex => {
            overlayState[key] = parseInt(String(hex).replace('#', ''), 16) || 0x010101;   // 0 means the game's colour, so black is one step off it
            reflectOverlay(); pushOverlay();
          });
        });
        pal.appendChild(own);
      }
      row.appendChild(name); row.appendChild(pal); wrap.appendChild(row);
      // its own outline width; the game's is 4
      const wrow = document.createElement('div'); wrow.className = 'ov-hovrow'; wrow.dataset.width = wKey;
      wrow.style.cssText = 'display:flex;align-items:center;gap:8px;padding:0 10px 8px 18px;';
      const wname = document.createElement('div'); wname.className = 'ov-sub'; wname.textContent = 'Thickness';
      wname.style.cssText = 'width:80px;flex:none;';
      const rng = document.createElement('input'); rng.type = 'range'; rng.min = '1'; rng.max = '24'; rng.id = 'ov_' + wKey; rng.style.cssText = 'flex:1 1 60px;min-width:0;';
      const val = document.createElement('div'); val.className = 'ov-sub'; val.id = 'ov_' + wKey + '_lbl'; val.style.cssText = 'width:22px;text-align:right;flex:none;';
      rng.addEventListener('input', () => { overlayState[wKey] = parseInt(rng.value, 10) || 8; val.textContent = overlayState[wKey]; pushOverlay(); });
      wrow.appendChild(wname); wrow.appendChild(rng); wrow.appendChild(val); wrap.appendChild(wrow);
    });
    wrap.appendChild(ovToggleRow('ov_tipvals', 'Extra info in tooltips', "Item price, alch value and skill levels, and the level an object needs, in the game's own tooltip"));
    wrap.appendChild(ovToggleRow('ov_marktest', 'Point the way', "The game's own arrow over an NPC, an object or a tile, with its chevrons at your feet turning towards it"));
    {
      const row = document.createElement('div'); row.className = 'ov-markrow';
      row.style.cssText = 'display:flex;align-items:center;gap:8px;padding:0 12px 6px 24px;';
      const inp = document.createElement('input'); inp.type = 'text'; inp.className = 'al-input'; inp.id = 'ov_markquery';
      inp.spellcheck = false; inp.placeholder = 'Name or id, or x, y for a tile';
      inp.addEventListener('input', () => { overlayState.mark_query = inp.value; pushOverlay(); ovMarkResolve(); });
      const st = document.createElement('div'); st.className = 'ov-sub'; st.id = 'ov_markstatus'; st.style.cssText = 'flex:none;max-width:45%;text-align:right;';
      row.appendChild(inp); row.appendChild(st); wrap.appendChild(row);
    }
    {
      // Buttons and sliders only: each can give nothing but a value the game takes
      OV_MARK_CHOICES.forEach(([key, label, choices]) => {
        const row = document.createElement('div'); row.className = 'ov-markrow'; row.dataset.choice = key;
        row.style.cssText = 'display:flex;flex-wrap:wrap;align-items:center;gap:4px 6px;padding:0 12px 6px 24px;';
        const name = document.createElement('div'); name.className = 'ov-sub'; name.textContent = label;
        name.style.cssText = 'width:84px;flex:none;';
        row.appendChild(name);
        choices.forEach(([value, text]) => {
          const chip = document.createElement('div'); chip.className = 'ov-sub ov-markchip'; chip.dataset.value = String(value); chip.textContent = text;
          chip.setAttribute('role', 'button');
          chip.style.cssText = 'padding:2px 8px;border:1px solid var(--border);border-radius:10px;cursor:pointer;';
          chip.addEventListener('click', () => { overlayState[key] = value; reflectOverlay(); pushOverlay(); });
          row.appendChild(chip);
        });
        wrap.appendChild(row);
      });
      OV_MARK_SLIDERS.forEach(([key, label, min, max, unit]) => {
        const row = document.createElement('div'); row.className = 'ov-markrow';
        row.style.cssText = 'display:flex;align-items:center;gap:8px;padding:0 12px 6px 24px;';
        const name = document.createElement('div'); name.className = 'ov-sub'; name.textContent = label;
        name.style.cssText = 'width:84px;flex:none;';
        const rng = document.createElement('input'); rng.type = 'range'; rng.min = String(min); rng.max = String(max); rng.id = 'ov_' + key; rng.style.flex = '1';
        const val = document.createElement('div'); val.className = 'ov-sub'; val.id = 'ov_' + key + '_lbl'; val.style.cssText = 'width:52px;text-align:right;flex:none;';
        rng.addEventListener('input', () => { overlayState[key] = parseInt(rng.value, 10) || 0; val.textContent = overlayState[key] + unit; pushOverlay(); });
        row.appendChild(name); row.appendChild(rng); row.appendChild(val); wrap.appendChild(row);
      });
    }
    wrap.appendChild(ovToggleRow('ov_enabled', 'Enable overlay', 'Draw markers into the game\'s 3D view'));
    const sep = document.createElement('div'); sep.className = 'ov-sep'; wrap.appendChild(sep);
    wrap.appendChild(ovToggleRow('ov_grid', 'Tile grid', 'World grid lines on the ground'));
    wrap.appendChild(ovToggleRow('ov_walkonly', 'Walkable only', 'Hide unwalkable tiles (else they tint red)'));
    wrap.appendChild(ovToggleRow('ov_truetile', 'True tile', 'Outline the tile the game holds for you (pink); with Players or NPCs on, also for those that are moving. Same value as trueTile in the plugin API'));
    {
      // one choice, not two switches: hiding is a way of treating what is behind scenery, as fading is
      const row = document.createElement('div'); row.className = 'ov-row'; row.id = 'ov_behind'; row.style.cursor = 'default';
      const l = document.createElement('div'); l.className = 'ov-l';
      const n = document.createElement('div'); n.className = 'ov-name'; n.textContent = 'Behind scenery';
      const sub = document.createElement('div'); sub.className = 'ov-sub'; sub.textContent = 'Marker parts behind terrain, walls, trees, players and NPCs';
      l.appendChild(n); l.appendChild(sub); row.appendChild(l);
      const chips = document.createElement('div'); chips.style.cssText = 'display:flex;gap:6px;flex:none;';
      [[0, 'Show'], [1, 'Fade'], [2, 'Hide']].forEach(([value, text]) => {
        const chip = document.createElement('div'); chip.className = 'ov-sub ov-behindchip'; chip.dataset.value = String(value); chip.textContent = text;
        chip.setAttribute('role', 'button');
        chip.style.cssText = 'padding:2px 8px;border:1px solid var(--border);border-radius:10px;cursor:pointer;';
        chip.addEventListener('click', () => { overlayState.occlude = value > 0; overlayState.occlude_hide = value === 2; reflectOverlay(); pushOverlay(); });
        chips.appendChild(chip);
      });
      row.appendChild(chips);
      wrap.appendChild(row);
    }
    wrap.appendChild(ovToggleRow('ov_inframe', 'Draw inside the game', 'The grid and markers are drawn into the game picture itself: behind trees, walls, players and NPCs, and under the whole game interface'));
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
    [['ov_enabled', 'enabled'], ['ov_grid', 'grid'], ['ov_walkonly', 'walk_only'], ['ov_truetile', 'true_tile'], ['ov_inframe', 'inframe_trial'], ['ov_players', 'players'],
     ['ov_npcs', 'npcs'], ['ov_objects', 'objects'], ['ov_interactable', 'interactable'], ['ov_specials', 'specials'], ['ov_hoverol', 'hover_outline'], ['ov_tipvals', 'tooltip_values'], ['ov_marktest', 'mark_test']].forEach(([id, key]) => {
      $(id).addEventListener('click', () => { overlayState[key] = !overlayState[key]; reflectOverlay(); pushOverlay(); });
    });
    reflectOverlay();
  }

Object.assign(window, { ovApplyDurablePrefs, ovToggleRow, pushOverlay, saveOverlayCfg, ovPointAt, ovReflectRenderer });
registerTab({ id: 'overlay', render: renderOverlay });
})();
