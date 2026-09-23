// RuneToolsX panel: Scene (nearby players, NPCs, objects, ground items, specials).
(function () {

  sceneData = null;   // {players:[{uid,x,y,self,name}], npcs:[..], objects:[{id,x,y,plane,type,dist,name,actions}]}
  let sceneFetching = false;
  sceneShow = { players: false, npcs: false, objects: false, specials: false, ground: false };
  // Type-4 world entities, named by gfx id (sec+0x74).
  const SPECIAL_NAMES = { 7307: 'Time sprite', 7164: 'Rockertunity', 8447: "Lumberjack's Intuition",
    6841: 'Scan: FAR (blue, beyond 2x range)', 6842: 'Scan: MEDIUM (orange, 1-2x range)', 6843: 'Scan: CLOSE (red, within range)' };
  function specialName(g) { return SPECIAL_NAMES[g] || ('Special (gfx ' + g + ')'); }
  const ADORN_NAMES = { 8920: 'Health bar' };
  function adornName(g) { return ADORN_NAMES[g] || ('Actor effect (gfx ' + g + ')'); }
  sceneRange = 20;
  let sceneTerm     = '';     // list filter: name or id substring (list-only; overlay/nameplates keep the full set)
  let sceneTerms    = [];
  try { const v = JSON.parse(localStorage.getItem('rtxSceneTerms') || '[]'); if (Array.isArray(v)) sceneTerms = v.filter(x => typeof x === 'string').slice(0, 12); } catch (e) {}
  function sceneTermsSave() { try { localStorage.setItem('rtxSceneTerms', JSON.stringify(sceneTerms)); } catch (e) {} }
  function sceneAllTerms() { const t = sceneTerm.trim().toLowerCase(); const out = sceneTerms.map(x => x.toLowerCase()); if (t) out.push(t); return out; }
  try { if (window.__rtxDevReload) sceneTerm = localStorage.getItem('rtxSceneTerm') || ''; } catch (e) {}
  let sceneTotal    = 0;      // pre-filter count, shown as "matches / total" while filtering
  sceneInteractable = false;
  sceneNameplates = false;
  const sceneObjOutlines = new Set();   // 'id:x:y:plane' outlined this session
  let sceneSig      = '';
  let sceneScroll   = 0;
  let sceneGround   = [];     // dropped ground items (companion type-3 publish): [{id,x,y,plane}]
  const sceneItemNames = {};  // item id -> name (cached; '' = fetch pending)
  function groundItemName(id) { return sceneItemNames[id] || ('Item ' + id); }
  async function resolveGroundNames() {
    if (!bridge() || !bridge().itemInfo) return;
    let any = false;
    for (const g of sceneGround) {
      if (!g || g.id == null || sceneItemNames[g.id] !== undefined) continue;
      sceneItemNames[g.id] = '';
      try { const info = JSON.parse(await rtxData.raw('cache.itemInfo', g.id)); sceneItemNames[g.id] = (info && info.name) || ('Item ' + g.id); any = true; } catch (e) {}
    }
    if (any) paneRun('scene', () => { sceneSig = ''; renderScene(); });
  }

  async function fetchScene() {
    if (!bridge() || sceneFetching) return;
    sceneFetching = true;
    try {
      const d = JSON.parse(await bridge().sceneEntities(myPid(), sceneRange));
      sceneData = (d && Array.isArray(d.npcs) && Array.isArray(d.players))
                    ? { objects: [], specials: [], ...d } : { players: [], npcs: [], objects: [], specials: [] };
    } catch (e) { /* keep previous sceneData */ }
    if ((sceneShow.ground || sceneMarkCount()) && bridge().groundItems) {
      try { const g = JSON.parse((await rtxData.raw('state.groundItems')) || '[]'); sceneGround = Array.isArray(g) ? g : []; }
      catch (e) { sceneGround = []; }
      if (sceneShow.ground) resolveGroundNames();
      sceneMarksSweep();
    } else { sceneGround = []; }
    sceneFetching = false;
    reconcileNameplates();   // keep pinned nameplates applied to the current uids (even when the tab is closed)
    paneRun('scene', renderScene);
  }

  function sceneSelfPos() {
    if (sceneData && Array.isArray(sceneData.players)) {
      const s = sceneData.players.find(p => p.self);
      if (s) return [s.x, s.y];
    }
    return [null, null];
  }

  function sceneItems() {
    if (!sceneData) return null;
    const [px, py] = sceneSelfPos();
    const cheby = (x, y) => (px == null) ? -1 : Math.max(Math.abs(x - px), Math.abs(y - py));
    const inRange = d => px == null || d < 0 || d <= sceneRange;
    const out = [];
    if (sceneShow.players && Array.isArray(sceneData.players))
      for (const p of sceneData.players) {
        if (p.self) continue;
        const d = cheby(p.x, p.y); if (!inRange(d)) continue;
        out.push({ type: 'player', name: p.name, x: p.x, y: p.y, plane: p.plane, dist: d, uid: p.uid, combat: p.combat, anim: p.anim });
      }
    const hasActs = e => e && Array.isArray(e.actions) && e.actions.length > 0;
    if (sceneShow.npcs && Array.isArray(sceneData.npcs))
      for (const n of sceneData.npcs) {
        if (sceneInteractable && !hasActs(n)) continue;
        const d = cheby(n.x, n.y); if (!inRange(d)) continue;
        out.push({ type: 'npc', name: n.name, x: n.x, y: n.y, plane: n.plane, dist: d, id: n.id, uid: n.uid, combat: n.combat, anim: n.anim, actions: n.actions });
      }
    if (sceneShow.objects && Array.isArray(sceneData.objects))
      for (const o of sceneData.objects) {
        if (sceneInteractable && !hasActs(o)) continue;
        if (o.vis === false) continue;
        out.push({ type: 'object', name: o.name, x: o.x, y: o.y, dist: (o.dist == null ? -1 : o.dist), id: o.id, plane: o.plane, otype: o.type, actions: o.actions, rt: !!o.rt, w: o.w | 0 || 1, h: o.h | 0 || 1 });
      }
    if (sceneShow.ground && Array.isArray(sceneGround))
      for (const g of sceneGround) {
        const d = cheby(g.x, g.y); if (!inRange(d)) continue;
        out.push({ type: 'ground', name: groundItemName(g.id), x: g.x, y: g.y, dist: d, id: g.id, plane: g.plane });
      }
    if (sceneShow.specials && Array.isArray(sceneData.specials))
      for (const s of sceneData.specials) {
        const ring = s.gfx >= 6841 && s.gfx <= 6843;
        const nm = (s.t === 13)
          ? (s.k === 'scan' ? 'Scan coordinate (dig here)'
             : s.k === 'dest' ? 'Walk destination'
             : 'Ground marker (unidentified)')
          : (s.k === 'adorn' ? adornName(s.gfx) : specialName(s.gfx));
        if (ring || !s.w) {
          out.push({ type: 'special', name: nm, x: (px != null ? px : s.x),
                     y: (py != null ? py : s.y), dist: 0, id: s.gfx, gfx: s.gfx, uid: s.uid });
        } else {
          const d = cheby(s.x, s.y); if (!inRange(d)) continue;
          out.push({ type: 'special', name: nm, x: s.x, y: s.y, dist: d,
                     id: s.gfx, gfx: s.gfx, uid: s.uid, plane: s.p });
        }
      }
    out.sort((a, b) => {
      const ad = a.dist < 0 ? 1e9 : a.dist, bd = b.dist < 0 ? 1e9 : b.dist;
      return ad !== bd ? ad - bd : (a.name || '').localeCompare(b.name || '');
    });
    sceneTotal = out.length;
    const terms = sceneAllTerms();
    if (!terms.length) return out;
    return out.filter(n => {
      const nm = (n.name || '').toLowerCase(), idv = String(n.id != null ? n.id : (n.uid != null ? n.uid : ''));
      return terms.some(t => nm.indexOf(t) !== -1 || idv.indexOf(t) !== -1);
    });
  }

  function saveSceneView() {
    prefSet('rtxSceneView', JSON.stringify(
      { show: sceneShow, range: sceneRange, inter: !!sceneInteractable }));
  }
  function sceneAdoptView(raw) {
    try {
      const sv = JSON.parse(raw || 'null');
      if (!sv || typeof sv !== 'object') return false;
      if (sv.show && typeof sv.show === 'object')
        for (const k in sceneShow) if (typeof sv.show[k] === 'boolean') sceneShow[k] = sv.show[k];
      if (typeof sv.range === 'number') sceneRange = Math.max(1, Math.min(64, sv.range | 0)) || 20;
      if (typeof sv.inter === 'boolean') sceneInteractable = sv.inter;
      return true;
    } catch (e) { return false; }
  }
  sceneAdoptView(sceneSeed('rtxSceneView'));
  function sceneSeed(k) { try { return localStorage.getItem(k); } catch (e) { return null; } }
  function sceneApplyDurablePrefs() {
    if (!sceneAdoptView(prefGet('rtxSceneView', null))) return;
    sceneSig = '';
    try { if (sceneNameplates) pushOverlay(); } catch (e) {}
    try { paneRun('scene', renderScene); } catch (e) {}
  }
  function toggleSceneKind(kind) {
    sceneShow[kind] = !sceneShow[kind];
    saveSceneView();
    sceneSig = '';
    if ((kind === 'objects' && sceneShow.objects) || (kind === 'ground' && sceneShow.ground)) fetchScene();
    renderScene();
    if (sceneNameplates) pushOverlay();   // nameplate kinds mirror these toggles
  }

  const outlineSet = new Set();
  function sceneTileKey(x, y, plane) { return (((x >> 6) << 8) | (y >> 6)) + '/' + (x & 63) + '/' + (y & 63) + '/' + (plane | 0); }
  function sceneTileMarked(x, y, plane) {
    const ml = (typeof markerList !== 'undefined' && Array.isArray(markerList)) ? markerList : [];
    const k = sceneTileKey(x, y, plane);
    return ml.some(m => (m.region + '/' + m.lx + '/' + m.ly + '/' + (m.plane | 0)) === k);
  }
  let sceneMarks = null;   // key -> { x, y, plane, id }
  function sceneMarksLoad() {
    if (sceneMarks) return sceneMarks;
    sceneMarks = {};
    try { const v = JSON.parse(prefGet('rtxSceneMarks', '{}')); if (v && typeof v === 'object') sceneMarks = v; } catch (e) {}
    return sceneMarks;
  }
  function sceneMarksSave() { try { prefSet('rtxSceneMarks', JSON.stringify(sceneMarksLoad())); } catch (e) {} }
  function sceneMarkCount() { return Object.keys(sceneMarksLoad()).length; }
  function sceneMarksSweep() {
    const m = sceneMarksLoad(); const keys = Object.keys(m); if (!keys.length) return;
    const b = bridge(); if (!b || !b.markerRemove) return;
    let changed = false;
    keys.forEach(k => {
      const r = m[k];
      if (r.id === -1) return;   // object footprint group: static, only removed by hand
      const still = sceneGround.some(g => g.id === r.id && g.x === r.x && g.y === r.y && ((g.plane | 0) === (r.plane | 0)));
      if (still) return;
      const W = Math.max(1, r.w | 0 || 1), H = Math.max(1, r.h | 0 || 1);
      for (let dx = 0; dx < W; dx++) for (let dy = 0; dy < H; dy++) {
        const tx = r.x + dx, ty = r.y + dy;
        try { b.markerRemove(myPid(), ((tx >> 6) << 8) | (ty >> 6), tx & 63, ty & 63, r.plane | 0); } catch (e) {}
      }
      delete m[k]; changed = true;
    });
    if (changed) { sceneMarksSave(); try { if (typeof fetchMarkers === 'function') fetchMarkers(true); } catch (e) {} try { if (typeof pushOverlay === 'function') pushOverlay(); } catch (e) {} }
  }
  function sceneMarksClearAll() {
    const b = bridge(); if (!b || !b.markerRemove) return;
    const m = sceneMarksLoad();
    for (const k in m) {
      const r = m[k];
      const W = Math.max(1, r.w | 0 || 1), H = Math.max(1, r.h | 0 || 1);
      for (let dx = 0; dx < W; dx++) for (let dy = 0; dy < H; dy++) {
        const tx = r.x + dx, ty = r.y + dy;
        try { b.markerRemove(myPid(), ((tx >> 6) << 8) | (ty >> 6), tx & 63, ty & 63, r.plane | 0); } catch (e) {}
      }
      delete m[k];
    }
    sceneMarksSave();
    const ml = (typeof markerList !== 'undefined' && Array.isArray(markerList)) ? markerList : [];
    const gold = (typeof mkParseColor === 'function') ? mkParseColor('#ffc93a') : 0xFFC93A;
    ml.forEach(mk => { let c = 0; try { c = (typeof mkParseColor === 'function') ? mkParseColor(mk.color) : 0; } catch (e) {} if (c === gold) { try { b.markerRemove(myPid(), mk.region, mk.lx, mk.ly, mk.plane | 0); } catch (e) {} } });
    try { if (typeof fetchMarkers === 'function') fetchMarkers(true); } catch (e) {}
    try { if (typeof pushOverlay === 'function') pushOverlay(); } catch (e) {}
    sceneSig = ''; setTimeout(renderScene, 150);
  }
  function toggleTileMark(x, y, plane, label, itemId, fw, fh) {
    const b = bridge();
    if (!b || !b.markerAdd) return;
    const m = sceneMarksLoad(), k = sceneTileKey(x, y, plane);
    const W = Math.max(1, Math.min(8, fw | 0 || 1)), H = Math.max(1, Math.min(8, fh | 0 || 1));
    const each = fn => { for (let dx = 0; dx < W; dx++) for (let dy = 0; dy < H; dy++) fn(x + dx, y + dy); };
    try {
      if (sceneTileMarked(x, y, plane)) {
        each((tx, ty) => { try { b.markerRemove(myPid(), ((tx >> 6) << 8) | (ty >> 6), tx & 63, ty & 63, plane | 0); } catch (e) {} });
        delete m[k]; sceneMarksSave();
      } else {
        each((tx, ty) => { try { b.markerAdd(myPid(), ((tx >> 6) << 8) | (ty >> 6), tx & 63, ty & 63, plane | 0, 0xFFC93A, (tx === x && ty === y) ? String(label || '').slice(0, 40) : ''); } catch (e) {} });
        if (itemId != null) m[k] = { x, y, plane: plane | 0, id: itemId, w: W, h: H };
        else m[k] = { x, y, plane: plane | 0, id: -1, w: W, h: H };   // object group: tracked for group removal only
        sceneMarksSave();
        try { if (typeof overlayState !== 'undefined' && overlayState && !overlayState.markers) { overlayState.markers = true; if (typeof saveOverlayCfg === 'function') saveOverlayCfg(); } } catch (e) {}
      }
    } catch (e) {}
    try { if (typeof fetchMarkers === 'function') fetchMarkers(true); } catch (e) {}
    try { if (typeof pushOverlay === 'function') pushOverlay(); } catch (e) {}
    sceneSig = ''; setTimeout(renderScene, 150);
  }
  function toggleOutline(uid, npcId) {
    const b = bridge();
    if (!b || !b.outlineNpc) return;
    const on = !outlineSet.has(uid);
    try { b.outlineNpc(myPid(), uid, on); } catch (e) {}
    if (on) outlineSet.add(uid); else outlineSet.delete(uid);
    sceneSig = ''; renderScene();
  }
  const nameplateNames   = new Set();
  const nameplateSet     = new Set();
  nameplatesLoaded = false;
  function nameplatesActive() { return nameplateNames.size > 0 || sceneMarkCount() > 0; }   // keeps scene data fresh in the background (pinned nameplates, outstanding item marks)
  function loadNameplateNames() {
    const b = bridge();
    if (!b || !b.nameplatesLoad || !myPid()) return;
    nameplatesLoaded = true;
    try {
      const raw = JSON.parse(b.nameplatesLoad(myPid()) || '{}');
      const arr = Array.isArray(raw) ? raw : (Array.isArray(raw.names) ? raw.names : []);
      nameplateNames.clear(); for (const n of arr) if (typeof n === 'string' && n) nameplateNames.add(n);
      if (!Array.isArray(raw) && typeof raw.pill === 'boolean') sceneNameplates = raw.pill;
    } catch (e) {}
    const npill = document.getElementById('sceneNpPill');
    if (npill) npill.classList.toggle('on', sceneNameplates);
    try { if (typeof pushOverlay === 'function') pushOverlay(); } catch (e) {}
    if (nameplateNames.size || sceneNameplates) fetchScene();   // pull the scene so pinned players re-apply immediately
  }
  function saveNameplates() {
    const b = bridge();
    if (!b || !b.nameplatesSave || !myPid()) return;
    try { b.nameplatesSave(myPid(), JSON.stringify({ pill: !!sceneNameplates, names: [...nameplateNames] })); } catch (e) {}
  }
  function reconcileNameplates() {
    const b = bridge();
    if (!b || !b.nameplatePlayer || !sceneData || !Array.isArray(sceneData.players)) return;
    const live = new Set();
    for (const p of sceneData.players) {
      if (p.self || !p.uid) continue;
      live.add(p.uid);
      const want = !!(p.name && nameplateNames.has(p.name));
      const have = nameplateSet.has(p.uid);
      if (want && !have) { try { b.nameplatePlayer(myPid(), p.uid, true); } catch (e) {} nameplateSet.add(p.uid); }
      else if (!want && have) { try { b.nameplatePlayer(myPid(), p.uid, false); } catch (e) {} nameplateSet.delete(p.uid); }
    }
    for (const u of [...nameplateSet]) if (!live.has(u)) nameplateSet.delete(u);
  }
  function toggleNameplate(name) {
    if (!name) return;
    if (nameplateNames.has(name)) nameplateNames.delete(name); else nameplateNames.add(name);
    saveNameplates();
    reconcileNameplates();
    sceneSig = ''; renderScene();
  }

  function renderScene() {
    const c = $('content');
    let sw = document.getElementById('sceneWrap');
    if (!sw) {
      c.innerHTML = '';
      sw = document.createElement('div'); sw.id = 'sceneWrap'; sw.className = 'scene-wrap';
      const hdr = document.createElement('div'); hdr.className = 'scene-hdr';
      const tog = document.createElement('div'); tog.className = 'scene-toggle';
      const mk = (kind, label) => {
        const btn = document.createElement('button');
        btn.id = 'sceneTog_' + kind; btn.textContent = label;
        btn.addEventListener('click', () => toggleSceneKind(kind));
        return btn;
      };
      tog.appendChild(mk('players', 'Players'));
      tog.appendChild(mk('npcs', 'NPCs'));
      tog.appendChild(mk('objects', 'Objects'));
      tog.appendChild(mk('ground', 'Ground'));
      tog.appendChild(mk('specials', 'Specials'));
      hdr.appendChild(tog);
      const srow = document.createElement('div'); srow.className = 'scene-search';
      const srch = document.createElement('input');
      srch.id = 'sceneSearch'; srch.className = 'bank-search';
      srch.type = 'text'; srch.placeholder = 'Filter by name or id (Enter keeps it, add another)...';
      srch.value = sceneTerm; srch.spellcheck = false;
      srch.addEventListener('input', () => { sceneTerm = srch.value; try { localStorage.setItem('rtxSceneTerm', sceneTerm); } catch (e) {} renderScene(); });
      const addTerm = () => {
        const v = srch.value.trim(); if (!v) return;
        if (!sceneTerms.some(x => x.toLowerCase() === v.toLowerCase()) && sceneTerms.length < 12) sceneTerms.push(v);
        sceneTermsSave(); srch.value = ''; sceneTerm = ''; try { localStorage.setItem('rtxSceneTerm', ''); } catch (e) {}
        sceneSig = ''; renderScene();
      };
      srch.addEventListener('keydown', e => {
        if (e.key === 'Enter') { addTerm(); e.stopPropagation(); return; }
        if (e.key === 'Escape' && srch.value) { srch.value = ''; sceneTerm = ''; try { localStorage.setItem('rtxSceneTerm', ''); } catch (e2) {} renderScene(); e.stopPropagation(); }
      });
      const addB = document.createElement('button'); addB.type = 'button'; addB.className = 'mk-btn scene-addterm'; addB.textContent = '+';
      addB.title = 'Keep this term and filter by another as well (any of them matches)';
      addB.addEventListener('click', addTerm);
      const cnt = document.createElement('span'); cnt.className = 'cnt'; cnt.id = 'sceneCnt'; cnt.textContent = '...';
      const lcnt = document.createElement('span'); lcnt.className = 'cnt'; lcnt.id = 'sceneLive';
      lcnt.title = 'Objects the client is tracking live, of the objects listed. Only these know whether the game is showing them right now.';
      srow.appendChild(srch); srow.appendChild(addB); srow.appendChild(lcnt); srow.appendChild(cnt);
      const chips = document.createElement('div'); chips.className = 'scene-chips'; chips.id = 'sceneChips';
      const rangeRow = document.createElement('div'); rangeRow.className = 'scene-range';
      const rlbl = document.createElement('span'); rlbl.className = 'lbl'; rlbl.textContent = 'Range';
      const rng = document.createElement('input');
      // 64 = the plugin-SDK scene cap; the native reader itself accepts up to 128.
      rng.type = 'range'; rng.min = '1'; rng.max = '64'; rng.id = 'sceneRangeInput'; rng.value = String(sceneRange);
      const rval = document.createElement('span'); rval.className = 'val'; rval.id = 'sceneRangeVal';
      rval.textContent = sceneRange + ' tiles';
      rng.addEventListener('input', () => {
        sceneRange = parseInt(rng.value, 10) || 20;
        rval.textContent = sceneRange + ' tiles';
        saveSceneView();
        sceneSig = '';
        fetchScene();          // re-pull objects; players/NPCs re-filter on render
        if (sceneNameplates) pushOverlay();
      });
      rangeRow.appendChild(rlbl); rangeRow.appendChild(rng); rangeRow.appendChild(rval);
      const intc = document.createElement('div'); intc.className = 'scene-int'; intc.setAttribute('role', 'button');
      intc.title = 'Only show NPCs/objects that have right-click actions';
      const ilbl = document.createElement('span'); ilbl.className = 'lbl'; ilbl.textContent = 'Interactable';
      const ipill = document.createElement('div'); ipill.id = 'sceneIntPill';
      ipill.className = 'al-pill' + (sceneInteractable ? ' on' : ''); ipill.appendChild(document.createElement('span'));
      intc.appendChild(ilbl); intc.appendChild(ipill);
      intc.addEventListener('click', () => {
        sceneInteractable = !sceneInteractable;
        ipill.classList.toggle('on', sceneInteractable);
        saveSceneView();
        sceneSig = ''; renderScene();
        pushOverlay();   // overlay markers respect the same filter
      });
      const npc = document.createElement('div'); npc.className = 'scene-int'; npc.setAttribute('role', 'button');
      npc.title = 'Draw floating name labels over players, NPCs and objects in the game world.\nWhich kinds show and the range follow the toggles above.';
      const nlbl = document.createElement('span'); nlbl.className = 'lbl'; nlbl.textContent = 'Nameplates';
      const npill = document.createElement('div'); npill.id = 'sceneNpPill';
      npill.className = 'al-pill' + (sceneNameplates ? ' on' : ''); npill.appendChild(document.createElement('span'));
      npc.appendChild(nlbl); npc.appendChild(npill);
      npc.addEventListener('click', () => {
        sceneNameplates = !sceneNameplates;
        npill.classList.toggle('on', sceneNameplates);
        saveNameplates();
        pushOverlay();
      });
      const clr = document.createElement('button'); clr.type = 'button'; clr.className = 'mk-btn scene-clear'; clr.id = 'sceneClearMarks';
      clr.textContent = 'Clear marks';
      clr.title = 'Remove the tile markers placed from this list (item and object marks). Other markers are left alone; see the Markers panel for those.';
      clr.addEventListener('click', (e) => { e.stopPropagation(); sceneMarksClearAll(); });
      const pills = document.createElement('div'); pills.className = 'scene-pills';
      pills.appendChild(intc); pills.appendChild(npc); pills.appendChild(clr);
      const list = document.createElement('div'); list.id = 'sceneList'; list.className = 'scene-list';
      list.addEventListener('scroll', () => { sceneScroll = list.scrollTop; });
      sw.appendChild(hdr); sw.appendChild(srow); sw.appendChild(chips); sw.appendChild(rangeRow); sw.appendChild(pills); sw.appendChild(list); c.appendChild(sw);
      sceneSig = '';
    }
    ['players', 'npcs', 'objects', 'ground', 'specials'].forEach(k => {
      const b = $('sceneTog_' + k); if (b) b.classList.toggle('is-active', !!sceneShow[k]);
    });

    const list = document.getElementById('sceneList');
    const items = sceneItems();
    $('sceneCnt').textContent = (items === null) ? '...'
      : (sceneAllTerms().length && items.length !== sceneTotal) ? items.length + ' / ' + sceneTotal : items.length;
    // Live count: objects the client itself is tracking, as opposed to entries listed from the map
    // cache. Only a live object carries its current state, so zero here means nothing can tell a
    // standing tree from a stump.
    {
      const lc = $('sceneLive');
      if (lc) {
        const rt = (sceneData && typeof sceneData.rtObjs === 'number') ? sceneData.rtObjs : null;
        lc.textContent = (sceneShow.objects && rt !== null) ? (rt < 0 ? 'live off' : 'live ' + rt) : '';
        lc.style.color = (sceneShow.objects && rt !== null && rt <= 0) ? '#f4a15d' : 'var(--text-dim)';
      }
    }

    const st = 'st' + (lastSnap ? (lastSnap.status | 0) : -1) + ',' + sceneTotal + '|';
    const sig = (items === null) ? 'null' + st
      : st + items.length + '|q' + sceneAllTerms().join('|') + '|ol' + [...outlineSet].join(',') + '|np' + [...nameplateNames].join(',') + '|' +
        items.map(n => n.type + (n.id || 0) + ':' + (n.uid || 0) + ':' +
          n.x + ',' + n.y + ':' + n.dist + ':' + (n.combat || 0) + ':' + (n.anim == null ? -1 : n.anim) + ':' + (n.name || '') + ':' + (n.actions || []).join('|')).join(';');
    if (sig === sceneSig) return;
    sceneSig = sig;

    const keep = list.scrollTop;
    list.innerHTML = '';
    if (items === null) { list.innerHTML = '<div class="empty">Reading scene...</div>'; return; }
    const chipsEl = $('sceneChips');
    if (chipsEl) {
      const csig = sceneTerms.join('|');
      if (chipsEl.dataset.sig !== csig) {
        chipsEl.dataset.sig = csig; chipsEl.textContent = '';
        chipsEl.style.display = sceneTerms.length ? '' : 'none';
        sceneTerms.forEach((t, i) => {
          const ch = document.createElement('span'); ch.className = 'scene-chip';
          const tx = document.createElement('span'); tx.textContent = t; ch.appendChild(tx);
          const x = document.createElement('button'); x.type = 'button'; x.textContent = String.fromCharCode(0xd7); x.title = 'Remove this filter';
          x.addEventListener('click', e => { e.stopPropagation(); sceneTerms.splice(i, 1); sceneTermsSave(); sceneSig = ''; renderScene(); });
          ch.appendChild(x); chipsEl.appendChild(ch);
        });
      }
    }
    if (!sceneShow.players && !sceneShow.npcs && !sceneShow.objects && !sceneShow.ground && !sceneShow.specials) {
      list.innerHTML = '<div class="empty">Nothing selected - toggle Players, NPCs, Objects, Ground or Specials above.</div>'; return;
    }
    if (!items.length) {
      if (sceneAllTerms().length && sceneTotal > 0) {
        const esc = sceneAllTerms().join('" or "').replace(/[<>&]/g, '');
        list.innerHTML = '<div class="empty">No name or id matching "' + esc + '" (' + sceneTotal + ' in range).</div>'; return;
      }
      let dg = '';
      const sd = sceneData && sceneData.sdiag;
      const vd = sceneData && sceneData.vdiag;
      if (sd && sceneShow.specials) dg = '<br><span style="color:var(--text-dim);font-size:10.5px">observer: installed ' + (sd[3] | 0) + ' &nbsp;fires ' + (sd[0] | 0) + ' &nbsp;type-4 ' + (sd[1] | 0) + ' &nbsp;gfx ' + (sd[2] | 0) + ' &nbsp;types 0x' + ((sd[4] | 0) >>> 0).toString(16) + ' &nbsp;@0x' + ((sd[5] | 0) >>> 0).toString(16) + '<br>tracked ptrs: ' + (sd[6] | 0) + ' &nbsp;hook uptime: ' + (sd[8] | 0) + 's' + '</span>';
      let why;
      if (!lastSnap) why = 'No client data - is the reader attached?';
      else if ((lastSnap.status | 0) !== 30) why = 'Not in-game - ' + fmtStatus(lastSnap) + '.';
      else why = 'Nothing in range.';
      list.innerHTML = '<div class="empty">' + why + dg + '</div>'; return;
    }

    const BADGE = { player: ['P', 'b-player'], npc: ['N', 'b-npc'], object: ['O', 'b-object'], special: ['S', 'b-special'], ground: ['I', 'b-object'] };
    const head = document.createElement('div'); head.className = 'scene-thead';
    [['', ''], ['Name', 'h-name'], ['Lvl', 'r h-lvl'], ['Dist', 'r h-dist'], ['Tile', 'r h-tile']].forEach(([h, cls]) => {
      const cell = document.createElement('div'); cell.textContent = h;
      if (cls) cell.className = cls;
      head.appendChild(cell);
    });
    list.appendChild(head);

    const frag = document.createDocumentFragment();
    for (const n of items) {
      const bi = BADGE[n.type];
      const row = document.createElement('div'); row.className = 'scene-trow';
      const bd = document.createElement('span'); bd.className = 'scene-badge ' + bi[1]; bd.textContent = bi[0]; bd.title = n.type;
      const namec = document.createElement('div'); namec.className = 'namec';
      const nm = document.createElement('div'); nm.className = 'nm';
      const dispName = n.name || (n.type === 'npc' ? 'NPC ' + n.uid : n.type === 'object' ? 'Object ' + n.id : n.type === 'ground' ? 'Item ' + n.id : n.type === 'special' ? 'Special' : 'Player ' + n.uid);
      nm.textContent = dispName;
      let nameRow = nm;
      if (n.type === 'npc' && n.uid) {
        const wrap = document.createElement('div'); wrap.className = 'namet';
        const ob = document.createElement('button'); ob.className = 'ol-btn';
        const on = outlineSet.has(n.uid);
        ob.textContent = '◳';
        ob.classList.toggle('on', on);
        ob.title = on ? 'Hide in-game box outline' : 'Box-outline this NPC in-game';
        ob.addEventListener('click', (e) => { e.stopPropagation(); toggleOutline(n.uid, n.id); });
        wrap.appendChild(ob); wrap.appendChild(nm);
        nameRow = wrap;
      } else if (n.type === 'ground' || n.type === 'object') {
        const wrap = document.createElement('div'); wrap.className = 'namet';
        if (n.type === 'object' && bridge() && bridge().outlineObject) {
          const ok = n.id + ':' + n.x + ':' + n.y + ':' + (n.plane | 0);
          const ob2 = document.createElement('button'); ob2.className = 'ol-btn';
          const on2 = sceneObjOutlines.has(ok);
          ob2.textContent = String.fromCharCode(0x25f3);
          ob2.classList.toggle('on', on2);
          ob2.title = on2 ? 'Hide in-game box outline' : 'Box-outline this object in-game (live model box when tracked, footprint otherwise)';
          ob2.addEventListener('click', (e) => {
            e.stopPropagation();
            const now = !sceneObjOutlines.has(ok);
            try { rtxData.sync('overlay.outlineObject', n.id, n.x, n.y, n.plane | 0, now); } catch (err) {}
            if (now) sceneObjOutlines.add(ok); else sceneObjOutlines.delete(ok);
            sceneSig = ''; renderScene();
          });
          wrap.appendChild(ob2);
        }
        if (n.type === 'object') { wrap.appendChild(nm); nameRow = wrap; } else {
        const ob = document.createElement('button'); ob.className = 'ol-btn';
        const on = sceneTileMarked(n.x, n.y, n.plane);
        ob.textContent = '◈';
        ob.classList.toggle('on', on);
        ob.title = on ? 'Remove the tile marker under this ' + (n.type === 'ground' ? 'item' : 'object') : 'Mark the tile this ' + (n.type === 'ground' ? 'item' : 'object') + ' is on (a user marker, see the Markers panel)';
        ob.addEventListener('click', (e) => { e.stopPropagation(); toggleTileMark(n.x, n.y, n.plane, n.name, n.type === 'ground' ? n.id : null, 1, 1); });
        wrap.appendChild(ob); wrap.appendChild(nm);
        nameRow = wrap;
        }
      } else if (n.type === 'player' && n.name) {
        const wrap = document.createElement('div'); wrap.className = 'namet';
        const ob = document.createElement('button'); ob.className = 'ol-btn';
        const on = nameplateNames.has(n.name);
        ob.textContent = '▭';
        ob.classList.toggle('on', on);
        ob.title = on ? 'Unpin this player\'s nameplate' : 'Pin a nameplate over this player (kept across sessions)';
        ob.addEventListener('click', (e) => { e.stopPropagation(); toggleNameplate(n.name); });
        wrap.appendChild(ob); wrap.appendChild(nm);
        nameRow = wrap;
      }
      const sub = document.createElement('div'); sub.className = 'sub';
      const subTxt = n.type === 'npc' ? 'id ' + n.id + (n.uid ? ' · uid ' + n.uid : '') + (typeof n.anim === 'number' && n.anim >= 0 ? ' · anim ' + n.anim : '')
                   : n.type === 'object' ? 'id ' + n.id + (n.plane ? ' · p' + n.plane : '')
                   : n.type === 'ground' ? 'item ' + n.id + (n.plane ? ' · p' + n.plane : '')
                   : n.type === 'special' ? 'gfx ' + n.gfx + (n.uid ? ' · uid ' + n.uid : '') + (n.plane ? ' · p' + n.plane : '')
                   : 'uid ' + n.uid + (typeof n.anim === 'number' && n.anim >= 0 ? ' · anim ' + n.anim : '');
      const acts = (n.actions && n.actions.length) ? n.actions.map(a => String(a).replace(/[<>&]/g, '')).join(', ') : '';
      const live = n.rt ? ' · <span class="act" style="color:#34d399">live</span>' : '';
      sub.innerHTML = subTxt + live + (acts ? ' · <span class="act">' + acts + '</span>' : '');
      namec.appendChild(nameRow); namec.appendChild(sub);
      const tipLines = [dispName, subTxt.replace(' · ', ', ')];
      if (n.combat && n.combat > 0) tipLines.push('Combat level ' + n.combat);
      if (n.dist >= 0) tipLines.push('Distance ' + n.dist + (n.dist === 1 ? ' tile' : ' tiles'));
      tipLines.push('Tile (' + n.x + ', ' + n.y + (typeof n.plane === 'number' ? ', ' + n.plane : '') + ')');
      if (acts) tipLines.push('Actions: ' + acts);
      if (n.rt) tipLines.push('Live-tracked');
      row.dataset.tip = tipLines.join('\n');
      const lvlc = document.createElement('div'); lvlc.className = 'num lvl';
      lvlc.textContent = (n.combat && n.combat > 0) ? n.combat : '-';
      const distc = document.createElement('div'); distc.className = 'num';
      distc.textContent = (n.dist >= 0) ? n.dist : '-';
      const tilec = document.createElement('div'); tilec.className = 'tile';
      tilec.textContent = '(' + n.x + ', ' + n.y + (typeof n.plane === 'number' && n.plane ? ', ' + n.plane : '') + ')';   // plane only above ground: keeps the column inside its track
      row.appendChild(bd); row.appendChild(namec); row.appendChild(lvlc); row.appendChild(distc); row.appendChild(tilec);
      frag.appendChild(row);
    }
    list.appendChild(frag);
    list.scrollTop = keep || sceneScroll || 0;
  }

Object.assign(window, { fetchScene, loadNameplateNames, nameplatesActive, renderScene, saveSceneView, sceneApplyDurablePrefs, sceneSelfPos });
registerTab({ id: 'scene', render: renderScene, open: function () { fetchScene(); } });
})();
