  const WIN_DEF = { w: 440, h: 560, minW: 330, minH: 240 };
  const WIN_SIZES = {
    player: { w: 470, h: 540 },
    worldmap: { w: 740, h: 580, minW: 460, minH: 340 },
    dung: { w: 560, h: 620 },
    'g:banks': { w: 520, h: 600, minW: 380 },
    'g:inv': { w: 400, h: 560 },
    'g:cluehub': { w: 500, h: 620 },
    'g:utils': { w: 390, h: 440, minH: 200 },
    interfaces: { w: 540, h: 620, minW: 420 },
    cachex: { w: 580, h: 640, minW: 440 },
    cs2: { w: 620, h: 640, minW: 460 },
    menuswap: { w: 540, h: 600, minW: 420 },
    netprobe: { w: 580, h: 620, minW: 440 },
    chatlog: { w: 520, h: 400 },
    combatlog: { w: 640, h: 460, minW: 480 },
    scene: { w: 470, h: 560, minW: 360 },
    alerts: { w: 470, h: 600 },
    ticks: { w: 360, h: 330, minH: 200 },
    screenshot: { w: 400, h: 350, minH: 220 },
    xptracker: { w: 420, h: 480 },
    xpmeter: { w: 264, h: 210, minW: 172, minH: 78 },
    metronome: { w: 132, h: 132, minW: 84, minH: 84 },
    auras: { w: 420, h: 560 },
    'aura:': { w: 240, h: 84, minW: 40, minH: 40 },
  };
  const WIN_HUD = new Set(['xpmeter', 'metronome']);
  function wmIsHud(id) { return WIN_HUD.has(id) || (typeof auraIsHudTab === 'function' && auraIsHudTab(id)); }
  const Z_HUD = 900000;              // HUD band: a HUD never hides behind a data panel
  function wmZ(w) { return (w.hud ? Z_HUD : 0) + w.z; }
  const wm = {
    wins: new Map(),        // wid -> win {wid, tab, tabs[], group, el, body, pane, tabsEl, x,y,w,h,z,min}
    zTop: 10,
    focused: null,          // wid
    barX: null, barY: null, barPill: false,
    barYf: null,            // vertical centre as a fraction of the frame height (free drag); null = edge-docked
    fullscreen: false,
    restored: false,        // layout restore ran (or gave up)
    dirty: false,           // user changed layout since restore (enables saves)
  };
  function widFor(tabId) { const g = TAB_GROUP_OF[tabId]; return g ? ('g:' + g.id) : tabId; }
  function wmMintWid(tabId) {
    const base = widFor(tabId);
    if (!wm.wins.has(base)) return base;
    let i = 2;                                  // undocked group member: 'g:<id>' is still taken
    while (wm.wins.has(base + '#' + i)) i++;
    return base + '#' + i;
  }
  function wmWinOf(tabId) {
    for (const w of wm.wins.values()) if (w.tabs.indexOf(tabId) >= 0) return w;
    return null;
  }
  function wmWinOfAny(ids) {
    let hidden = null;
    for (const id of ids) {
      const w = wmWinOf(id);
      if (w && !w.min) return w;
      if (w && !hidden) hidden = w;
    }
    return hidden;
  }
  function paneVisible(id) {
    const w = wmWinOf(id);
    return !!(w && !w.min && !w.rolled && w.tab === id);
  }
  function paneRoot(id) { return __paneRoots[id] || null; }
  function wmVisibleWins() {
    const out = [];
    for (const w of wm.wins.values()) if (!w.min && !w.rolled) out.push(w);
    return out;
  }
  function withPane(w, fn) {
    const pa = activeTab, pr = __paneRoot;
    activeTab = w.tab;
    __paneRoot = w.pane;
    try { return fn(); } finally { activeTab = pa; __paneRoot = pr; }
  }
  function paneTabOf(root) {
    for (const w of wm.wins.values()) if (w.pane === root) return w.tab;
    return '';
  }
  function paneScrollTrack(root) {
    if (!root || root.__rtxScrollBound) return;
    root.__rtxScrollBound = 1;
    root.__rtxScrollMem = new Map();
    root.addEventListener('scroll', (e) => {
      const el = e.target;
      if (!el || (el !== root && !root.contains(el))) return;
      const mem = root.__rtxScrollMem;
      const tab = paneTabOf(root);
      let key = null, sel = null, i = 0;
      if (el === root) { key = tab + '||'; }
      else {
        if (el.id) sel = '#' + CSS_esc(el.id);
        else if (typeof el.className === 'string' && el.className.trim())
          sel = '.' + el.className.trim().split(/\s+/).map(CSS_esc).join('.');
        else return;                       // no stable handle -> cannot match it back
        try { i = Array.prototype.indexOf.call(root.querySelectorAll(sel), el); } catch (err) { return; }
        if (i < 0) return;
        key = tab + '|' + sel + '|' + i;
      }
      if (mem.size > 48 && !mem.has(key)) return;   // bounded: stale entries are harmless
      mem.set(key, { tab, sel, i, top: el.scrollTop, left: el.scrollLeft });
    }, true);
  }
  function CSS_esc(s) { return String(s).replace(/[^A-Za-z0-9_-]/g, '\\$&'); }
  function paneScrollRestore(root, tab) {
    const mem = root && root.__rtxScrollMem;
    if (!mem || !mem.size) return;
    for (const s of mem.values()) {
      if (s.tab !== tab) continue;          // another docked tab's offsets, not this one's
      let el = null;
      if (s.sel === null) el = root;
      else { let list; try { list = root.querySelectorAll(s.sel); } catch (e) { continue; }
             el = list[s.i] || null; }
      if (!el) continue;
      if (s.top > 0 && el.scrollTop === 0) {
        const max = el.scrollHeight - el.clientHeight;
        if (max > 0) el.scrollTop = Math.min(s.top, max);
      }
      if (s.left > 0 && el.scrollLeft === 0) {
        const max = el.scrollWidth - el.clientWidth;
        if (max > 0) el.scrollLeft = Math.min(s.left, max);
      }
    }
  }
  function withScrollKeep(root, tab, fn) {
    paneScrollTrack(root);
    try { return fn(); } finally { paneScrollRestore(root, tab); }
  }
  function paneRun(id, fn) {
    const w = wmWinOf(id);
    if (!w || w.min || w.tab !== id) return;
    withPane(w, () => withScrollKeep(w.pane, id, fn));
  }
  function renderPaneFor(w) { withPane(w, () => withScrollKeep(w.pane, w.tab, () => { renderPane(); wmRenderTabs(w); })); }
  function uiRepaintAll() {
    for (const w of wm.wins.values()) {
      if (w.min || w.tab === 'uisettings') continue;
      try {
        withPane(w, () => { const c = $('content'); if (c) c.innerHTML = ''; });
        renderPaneFor(w);
      } catch (e) {}
    }
  }

  function paneLeave(id) {
    if (id === 'metronome' && metroVisual()) {
      try { setMetroMode(metroAudio() ? 'audio' : 'off'); } catch (e) {}
    }
    if (id === 'xpmeter' && xpOn) {
      try { xpOn = false; saveXpCfg(); paneRun('xptracker', () => { const wr = $('xpWrap'); if (wr) buildXpConfig(wr); }); } catch (e) {}
    }
    { const P = RTX.panels[id]; if (P && typeof P.close === 'function') { try { P.close(); } catch (e) {} } }
    if (String(id).indexOf('plugin:') === 0) {
      try { if (bridge() && bridge().guideMarks) bridge().guideMarks(myPid(), ''); } catch (e) {}
      try { if (bridge() && bridge().uiHighlight) bridge().uiHighlight(myPid(), 0, 0, 0, 0); } catch (e) {}
      try { if (bridge() && bridge().panelViz) bridge().panelViz(myPid(), ''); } catch (e) {}
      try { if (bridge() && bridge().overlayHighlight) bridge().overlayHighlight(myPid(), ''); } catch (e) {}
      try { overlayHighlightResync(); } catch (e) {}
      try { if (bridge() && bridge().centerText) bridge().centerText(myPid(), ''); } catch (e) {}
      if (typeof pluginRelease === 'function') pluginRelease(id.slice(7)); else pluginUnmount(id.slice(7));
    }
  }

  function wmClamp(w) {
    const vw = window.innerWidth || 1280, vh = window.innerHeight || 720;
    const sz = winSizeFor(w.wid, w.tab);
    if (w.w < sz.minW) w.w = sz.minW;
    if (w.w > vw) w.w = vw;
    const eh = w.rolled ? ((w.el && w.el.offsetHeight) || 36) : w.h;
    if (!w.rolled) {
      if (w.h < sz.minH) w.h = sz.minH;
      if (w.h > vh) w.h = vh;
    }
    if (w.x + w.w > vw) w.x = vw - w.w;
    if (w.y + eh > vh) w.y = vh - eh;
    if (w.x < 0) w.x = 0;
    if (w.y < 0) w.y = 0;
  }
  function winSizeFor(wid, tab) {
    let pre = null;
    if (typeof tab === 'string' && tab.indexOf('aura:') === 0) {
      pre = Object.assign({}, WIN_SIZES['aura:']);
      try { const a = (typeof auraGet === 'function') ? auraGet(tab.slice(5)) : null; if (a) { pre.w = Math.max(pre.minW, (a.w | 0) + 4); pre.h = Math.max(pre.minH, (a.h | 0) + 22); } } catch (e) {}
    }
    return Object.assign({}, WIN_DEF, WIN_SIZES[wid] || WIN_SIZES[tab] || pre || {});
  }
  function wmApplyGeom(w) {
    w.el.style.left = w.x + 'px';
    w.el.style.top = w.y + 'px';
    w.el.style.width = w.w + 'px';
    w.el.style.height = w.rolled ? 'auto' : (w.h + 'px');
    w.el.classList.toggle('narrow', w.w < 400);
    w.el.classList.toggle('tight', w.w < 350);
  }
  function wmToggleRoll(w) {
    w.rolled = !w.rolled;
    w.el.classList.toggle('rolled', !!w.rolled);
    wmApplyGeom(w);
    if (!w.rolled) renderPaneFor(w);
    syncMarkerArm();
    renderMenubar();
    wmRectsSoon(); wmSaveSoon();
  }
  function wmApplyLock(w) {
    if (!w.el) return;
    w.el.classList.toggle('locked', !!w.locked);
    const b = w.el.querySelector('.win-lock');
    if (b) b.classList.toggle('on', !!w.locked);
  }
  function wmToggleLock(w) {
    w.locked = !w.locked;
    wmApplyLock(w);
    wmRectsSoon(); wmSaveSoon();
    if (w.tab === 'metronome') {
      setMetroLock(w.locked);
    }
    if (w.tab === 'xpmeter') {
      xpLock = w.locked;
      saveXpCfg();
      paneRun('xptracker', () => { const wr = $('xpWrap'); if (wr) buildXpConfig(wr); });
    }
  }
  function wmFocus(w) {
    if (!w) return;
    if (wm.focused && wm.focused !== w.wid) {
      const p = wm.wins.get(wm.focused);
      if (p) p.el.classList.remove('focus');
    }
    wm.focused = w.wid;
    w.el.classList.add('focus');
    w.z = ++wm.zTop;
    w.el.style.zIndex = String(wmZ(w));
    activeTab = w.tab;
    try { localStorage.setItem('rtxDevTab', activeTab); } catch (e) {}
    syncMarkerArm();
  }
  let _spawnN = 0;
  function wmSpawnPos(sz) {
    const vw = window.innerWidth || 1280, vh = window.innerHeight || 720;
    const step = 30, n = _spawnN++ % 8;
    let x = Math.max(12, Math.min(24 + n * step, vw - sz.w - 12));
    let y = Math.max(60, Math.min(64 + n * step, vh - sz.h - 12));
    return { x, y };
  }

  const WIN_SVG = (p) => '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" ' +
    'stroke-linecap="round" stroke-linejoin="round">' + (p || '') + '</svg>';

  function wmCreateWindow(wid, tab, group, geom, noAnim) {
    // HUD panels (XP meter, metronome, aura bars) are always solo windows: their chrome, lock
    // button and leave handlers belong to that one panel, so they never share a window with a
    // settings tab and a group never mounts inside HUD chrome.
    if (group && wmIsHud(tab.id)) group = null;
    const sz = winSizeFor(wid, tab.id);
    const hasBox = !!(geom && !geom.nogeom);
    const pos = hasBox ? { x: geom.x, y: geom.y } : wmSpawnPos(sz);
    const w = {
      wid, tab: tab.id, tabs: [tab.id], group: group || null,
      groupIco: group ? tab.icon : null,   // the icon an intact group reverts to (wmSyncTitle)
      x: pos.x, y: pos.y,
      w: hasBox ? geom.w : sz.w, h: hasBox ? geom.h : sz.h,
      z: ++wm.zTop, min: !!(geom && geom.min),
      hud: wmIsHud(tab.id),
      locked: wmIsHud(tab.id) && !!(geom && geom.lock),
      el: null, body: null, pane: null, tabsEl: null, nameEl: null, verEl: null, titleEl: null, icoEl: null,
    };
    if (group) {
      const avail = allTabs();
      w.tabs = group.tabs.filter(id => avail.some(t => t.id === id) && !wmWinOf(id) && !wmIsHud(id));
      if (w.tabs.indexOf(tab.id) < 0) w.tabs.push(tab.id);
    }
    const el = document.createElement('section');
    el.className = (noAnim ? 'win' : 'win opening') + (w.hud ? ' hud' : '');
    el.dataset.wid = wid;
    el.style.zIndex = String(wmZ(w));
    const title = document.createElement('div');
    title.className = 'win-title';
    const name = document.createElement('span');
    name.className = 'win-name';
    name.textContent = group ? group.label : tab.label;
    title.appendChild(name);
    const ver = document.createElement('span');   // a tab that carries a version shows it after the name
    ver.className = 'win-ver';
    ver.textContent = (!group && tab.ver) ? ('v' + tab.ver) : '';
    title.appendChild(ver);
    w.nameEl = name;
    w.verEl = ver;
    w.titleEl = title;
    const tabs = document.createElement('div');
    tabs.className = 'win-tabs';   // appended BELOW the title, before the body
    w.tabsEl = tabs;
    const btns = document.createElement('div');
    btns.className = 'win-btns';
    const mkB = (glyph, tip, fn, cls) => {
      const b = document.createElement('button');
      b.type = 'button';
      if (cls) b.className = cls;
      b.title = tip;
      const g = document.createElement('i'); g.className = 'g ' + glyph;
      b.appendChild(g);
      b.addEventListener('click', (e) => { e.stopPropagation(); fn(); });
      return b;
    };
    if (w.hud) btns.appendChild(mkB('g-lock', 'Lock (click-through)', () => wmToggleLock(w), 'win-lock'));
    btns.appendChild(mkB('g-min', 'Minimize', () => wmMinimize(w)));
    btns.appendChild(mkB('g-x', 'Close', () => wmClose(wid), 'win-close'));
    title.appendChild(btns);
    title.addEventListener('dblclick', (e) => {
      if (e.target && e.target.closest && (e.target.closest('.win-btns') || e.target.closest('.win-tabs'))) return;
      wmToggleRoll(w);
    });
    el.appendChild(title);
    el.appendChild(tabs);   // group member strip row (empty + hidden for solo panels)
    const body = document.createElement('div');
    body.className = 'win-body';
    const pane = document.createElement('section');
    pane.className = 'content';
    body.appendChild(pane);
    el.appendChild(body);
    w.body = body;
    w.pane = pane;
    for (const dir of ['n', 's', 'w', 'e', 'nw', 'ne', 'sw', 'se']) {
      const h = document.createElement('div');
      h.className = 'rz rz-' + dir;
      h.dataset.dir = dir;
      h.addEventListener('mousedown', (e) => wmResizeStart(w, dir, e));
      el.appendChild(h);
    }
    el.addEventListener('mousedown', () => { if (wm.focused !== wid) { wmFocus(w); wmRectsSoon(); wmSaveSoon(); } }, true);
    title.addEventListener('mousedown', (e) => wmDragStart(w, e));
    // Republish rects once win-in's scale(0.965) is gone, or the consume region is the scaled box.
    el.addEventListener('animationend', () => { el.classList.remove('opening'); wmRectsSoon(); });
    $('winlayer').appendChild(el);
    w.el = el;
    wm.wins.set(wid, w);
    __paneRoots[w.tab] = pane;
    wmClamp(w);
    wmApplyGeom(w);
    wmApplyLock(w);
    if (w.min) el.style.display = 'none';
    wmRenderTabs(w);
    return w;
  }
  function wmTabLabel(w, id) {
    const t = allTabs().find(x => x.id === id);
    if (w.group) {
      const i = w.group.tabs.indexOf(id);
      if (i >= 0 && w.group.subs && w.group.subs[i]) return w.group.subs[i];
    }
    return t ? t.label : id;
  }
  function wmIsWholeGroup(w) {
    if (!w.group) return false;
    const avail = allTabs();
    const mem = w.group.tabs.filter(id => avail.some(t => t.id === id));
    const have = w.tabs.filter(id => avail.some(t => t.id === id));
    return have.length === mem.length && mem.every(id => have.indexOf(id) >= 0);
  }
  function wmSyncTitle(w) {
    if (!w.nameEl) return;
    const t = allTabs().find(x => x.id === w.tab);
    const whole = wmIsWholeGroup(w);
    w.nameEl.textContent = whole ? w.group.label : (t ? t.label : w.tab);
    if (w.verEl) w.verEl.textContent = (!whole && t && t.ver) ? ('v' + t.ver) : '';
  }
  function wmRenderTabs(w) {
    if (!w.tabsEl) return;
    const avail = allTabs();
    const shown = w.tabs.filter(id => avail.some(x => x.id === id));
    const multi = w.tabs.length > 1;
    const sig = multi ? (shown.join(',') + '|' + w.tab + '|' + w.tabs.length) : '';
    if (w.tabsEl._sig === sig) return;
    w.tabsEl._sig = sig;
    w.tabsEl.innerHTML = '';
    wmSyncTitle(w);   // title tracks the same inputs, so it rides this guard
    if (!multi) return;
    shown.forEach((id) => {
      const t = avail.find(x => x.id === id);
      const b = document.createElement('button');
      if (id === w.tab) b.className = 'on';
      const lab = document.createElement('span');
      lab.textContent = wmTabLabel(w, id);
      b.appendChild(lab);
      b.dataset.tab = id;                       // reorder needs to map an element back to its tab
      if (_tabDrag && _tabDrag.w === w && _tabDrag.id === id) b.className += ' tabdrag';
      if (shown.length > 1) {
        const x = document.createElement('i');
        x.className = 'win-tabx';
        x.textContent = '×';
        x.title = 'Close ' + lab.textContent;
        x.addEventListener('mousedown', (e) => e.stopPropagation());   // never starts a drag
        x.addEventListener('click', (e) => { e.stopPropagation(); wmCloseTabIn(w, id); });
        b.appendChild(x);
        b.addEventListener('mousedown', (e) => wmTabDragStart(w, id, e));
      } else {
        b.className += (b.className ? ' ' : '') + 'solo';
      }
      b.addEventListener('click', (e) => {
        e.stopPropagation();
        if (_tabDragged) { _tabDragged = false; return; }   // that press undocked instead
        if (id !== w.tab) wmSwitchTab(w, t);
      });
      w.tabsEl.appendChild(b);
    });
    if (w.group && w.group.tabs.indexOf(w.tab) >= 0)
      try { localStorage.setItem('rtxGrpLast:' + w.group.id, w.tab); } catch (e) {}
  }
  function wmReorderTabAt(w, id, x, y) {
    if (!w.tabsEl) return;
    let overId = null;
    for (const p of Array.from(w.tabsEl.children)) {
      const pid2 = p.dataset && p.dataset.tab;
      if (!pid2 || pid2 === id) continue;
      const r = p.getBoundingClientRect();
      if (y < r.top - 4 || y > r.bottom + 4) continue;      // a different wrapped row
      if (x >= r.left && x <= r.right) { overId = pid2; break; }
    }
    if (!overId) return;
    const from = w.tabs.indexOf(id), to = w.tabs.indexOf(overId);
    if (from < 0 || to < 0 || from === to) return;
    w.tabs.splice(from, 1);
    w.tabs.splice(to, 0, id);
    wmRenderTabs(w);
    wmSaveSoon();
  }
  function wmSetTabs(w, ids) {
    w.tabs = wmIsHud(w.tab) ? [w.tab] : ids.filter(id => !wmIsHud(id));   // HUD panels stay solo
    if (w.tabs.indexOf(w.tab) < 0) w.tabs.unshift(w.tab);
    wmRenderTabs(w);
  }
  function wmSwitchTab(w, t) {
    if (w.tab === t.id) return;
    paneLeave(w.tab);
    delete __paneRoots[w.tab];
    w.tab = t.id;
    __paneRoots[w.tab] = w.pane;
    w.pane.innerHTML = '';
    if (wm.focused === w.wid) { activeTab = w.tab; try { localStorage.setItem('rtxDevTab', activeTab); } catch (e) {} }
    wmRenderTabs(w);
    renderPaneFor(w);
    withPane(w, () => tabEntryKicks(w.tab));
    syncMarkerArm();
    wmSaveSoon();
  }
  function wmMinimize(w) {
    try { if (typeof closeSoundMenu === 'function') closeSoundMenu(); } catch (e) {}
    w.min = true;
    w.el.style.display = 'none';
    if (wm.focused === w.wid) { wm.focused = null; }
    syncMarkerArm();
    renderMenubar();
    wmRectsSoon(); wmSaveSoon();
  }
  let _wmHiddenSet = null;
  function wmToggleAll() {
    const open = [...wm.wins.values()].filter(w => !w.min);
    if (open.length) {
      _wmHiddenSet = open.map(w => w.wid);
      for (const w of open) wmMinimize(w);
    } else {
      const ids = _wmHiddenSet || [...wm.wins.keys()];
      _wmHiddenSet = null;
      for (const id of ids) { const w = wm.wins.get(id); if (w && w.min) wmRestore(w); }
    }
    try { wmPushRects(); } catch (e) {}
  }
  function wmRestore(w) {
    w.min = false;
    w.el.style.display = '';
    wmClamp(w); wmApplyGeom(w);
    renderPaneFor(w);
    renderMenubar();
    wmRectsSoon(); wmSaveSoon();
    syncMarkerArm();
  }
  function wmClose(wid) {
    const w = wm.wins.get(wid);
    if (!w) return;
    try { if (typeof closeSoundMenu === 'function') closeSoundMenu(); } catch (e) {}
    wm.dirty = true;            // closing is a layout change; see the note in openTab
    paneLeave(w.tab);
    delete __paneRoots[w.tab];
    wm.wins.delete(wid);
    try { const a = document.activeElement; if (a && w.el.contains(a)) a.blur(); } catch (e) {}
    if (w.el && w.el.parentNode) w.el.parentNode.removeChild(w.el);
    if (wm.focused === wid) wm.focused = null;
    syncMarkerArm();
    renderMenubar();
    wmRectsSoon(); wmSaveSoon();
  }
  function wmMountTab(w, id) {
    w.tab = id;
    __paneRoots[id] = w.pane;
    w.pane.innerHTML = '';
    try { if (typeof pluginHoldersSync === 'function') pluginHoldersSync(w); } catch (e) {}   // plugin frames survive a tab switch hidden
    if (wm.focused === w.wid) { activeTab = id; try { localStorage.setItem('rtxDevTab', id); } catch (e) {} }
    wmRenderTabs(w);
    renderPaneFor(w);
    withPane(w, () => tabEntryKicks(id));
  }
  function wmNeighbourTab(w, i) {
    const avail = allTabs();
    const ok = (id) => avail.some(t => t.id === id);
    for (let d = 0; d < w.tabs.length; d++) {
      const r = i + d, l = i - 1 - d;
      if (r < w.tabs.length && ok(w.tabs[r])) return w.tabs[r];
      if (l >= 0 && ok(w.tabs[l])) return w.tabs[l];
    }
    return null;
  }
  function wmCloseTabIn(w, id) {
    const i = w.tabs.indexOf(id);
    if (i < 0) return;
    if (w.tabs.length <= 1) { wmClose(w.wid); return; }
    const wasShown = (w.tab === id);
    w.tabs.splice(i, 1);
    const nx = wasShown ? wmNeighbourTab(w, i) : null;
    if (wasShown && !nx) { wmClose(w.wid); return; }   // only build-hidden leftovers: nothing to mount
    if (wasShown) paneLeave(id);
    delete __paneRoots[id];
    if (wasShown) wmMountTab(w, nx);
    else wmRenderTabs(w);
    syncMarkerArm();
    renderMenubar();
    wmRectsSoon(); wmSaveSoon();
  }
  function wmCloseTabId(id) {
    const w = wmWinOf(id);
    if (w) wmCloseTabIn(w, id);
  }

  function wmPtIn(el, x, y) {
    if (!el) return false;
    const r = el.getBoundingClientRect();
    return r.width > 0 && r.height > 0 && x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
  }
  function wmDockTargetAt(x, y, skip) {
    if (skip && skip.hud) return null;
    let best = null;
    for (const w of wmVisibleWins()) {
      if (w === skip || w.rolled || w.hud) continue;
      if (!wmPtIn(w.titleEl, x, y) && !wmPtIn(w.tabsEl, x, y)) continue;
      if (!best || w.z > best.z) best = w;   // overlapping title bars: the top one wins
    }
    if (best) for (const w of wm.wins.values()) {
      if (w === best || w === skip || w.min) continue;
      if (w.z > best.z && wmPtIn(w.el, x, y)) return null;
    }
    return best;
  }
  function wmDockInto(src, tgt) {
    const act = src.tab;
    delete __paneRoots[act];
    for (const id of src.tabs) if (tgt.tabs.indexOf(id) < 0) tgt.tabs.push(id);
    wm.wins.delete(src.wid);
    try { const a = document.activeElement; if (a && src.el.contains(a)) a.blur(); } catch (e) {}
    if (src.el && src.el.parentNode) src.el.parentNode.removeChild(src.el);
    if (wm.focused === src.wid) wm.focused = null;
    const t = allTabs().find(x => x.id === act);
    if (t) wmSwitchTab(tgt, t);   // normal switch path: leaves tgt's tab, mounts + kicks ours
    else wmRenderTabs(tgt);
    wmFocus(tgt);
    syncMarkerArm();
    renderMenubar();
    wmRectsSoon(); wmSaveSoon();
  }
  function wmUndockTab(w, id, ev) {
    if (w.tabs.length <= 1) return null;
    const t = allTabs().find(x => x.id === id);
    if (!t) return null;
    const i = w.tabs.indexOf(id);
    const wasShown = (w.tab === id);
    w.tabs.splice(i, 1);
    const nx = wasShown ? wmNeighbourTab(w, i) : null;
    if (wasShown) delete __paneRoots[id];   // moving, not closing: no paneLeave
    if (wasShown && !nx) {
      wm.wins.delete(w.wid);
      if (w.el && w.el.parentNode) w.el.parentNode.removeChild(w.el);
      if (wm.focused === w.wid) wm.focused = null;
    } else if (wasShown) {
      wmMountTab(w, nx);
    } else {
      wmRenderTabs(w);
    }
    const geom = { x: Math.round(ev.clientX - 70), y: Math.round(ev.clientY - 14),
                   w: w.w, h: w.h, min: false };
    const nw = wmCreateWindow(wmMintWid(id), t, TAB_GROUP_OF[id] || null, geom, true);
    wmSetTabs(nw, [id]);        // one member of its group, not the whole group again
    wmFocus(nw);
    renderPaneFor(nw);
    withPane(nw, () => tabEntryKicks(id));
    syncMarkerArm();
    renderMenubar();
    wmRectsSoon(); wmSaveSoon();
    return nw;
  }

  let _wmDrag = null;
  let _tabDrag = null;        // pill pressed, not yet dragged far enough to undock
  let _tabDragged = false;    // that press became an undock -> swallow its click
  function wmDragStart(w, e) {
    if (e.button !== 0) return;
    if (e.target && (e.target.closest && (e.target.closest('.win-btns') || e.target.closest('.win-tabs')))) return;
    _wmDrag = { w, mode: 'move', sx: e.clientX, sy: e.clientY, ox: w.x, oy: w.y, dock: null };
    w.el.classList.add('dragging');
    e.preventDefault();
  }
  function wmTabDragStart(w, id, e) {
    if (e.button !== 0) return;
    if (e.target && e.target.closest && e.target.closest('.win-tabx')) return;
    _tabDragged = false;
    _tabDrag = { w, id, sx: e.clientX, sy: e.clientY };
  }
  function wmResizeStart(w, dir, e) {
    if (e.button !== 0) return;
    wmFocus(w);
    _wmDrag = { w, mode: 'size', dir, sx: e.clientX, sy: e.clientY, ox: w.x, oy: w.y, ow: w.w, oh: w.h };
    w.el.classList.add('sizing');
    e.preventDefault();
    e.stopPropagation();
  }
  document.addEventListener('mousemove', (e) => {
    if (_tabDrag && !_wmDrag) {
      const td0 = _tabDrag;
      const sr = td0.w.tabsEl ? td0.w.tabsEl.getBoundingClientRect() : null;
      if (sr && e.clientX >= sr.left - 28 && e.clientX <= sr.right + 28 &&
                e.clientY >= sr.top - 14 && e.clientY <= sr.bottom + 14) {
        if (Math.abs(e.clientX - td0.sx) < 6 && Math.abs(e.clientY - td0.sy) < 6) return;
        _tabDragged = true;                       // this press is a drag, not a plain click
        wmReorderTabAt(td0.w, td0.id, e.clientX, e.clientY);
        return;
      }
      if (Math.abs(e.clientX - _tabDrag.sx) < 8 && Math.abs(e.clientY - _tabDrag.sy) < 8) return;
      const td = _tabDrag; _tabDrag = null;
      const nw = wmUndockTab(td.w, td.id, e);
      if (!nw) return;
      _tabDragged = true;
      _wmDrag = { w: nw, mode: 'move', sx: e.clientX, sy: e.clientY, ox: nw.x, oy: nw.y, dock: null };
      nw.el.classList.add('dragging');
    }
    const d = _wmDrag;
    if (!d) return;
    const dx = e.clientX - d.sx, dy = e.clientY - d.sy;
    const w = d.w;
    if (d.mode === 'move') {
      w.x = d.ox + dx; w.y = d.oy + dy;
      const vw = window.innerWidth, vh = window.innerHeight;
      if (Math.abs(w.x) < 10) w.x = 0;
      if (Math.abs(w.y) < 10) w.y = 0;
      if (Math.abs(vw - (w.x + w.w)) < 10) w.x = vw - w.w;
      if (Math.abs(vh - (w.y + w.h)) < 10) w.y = vh - w.h;
      const tgt = wmDockTargetAt(e.clientX, e.clientY, w);
      if (tgt !== d.dock) {
        if (d.dock) d.dock.el.classList.remove('dock-target');
        d.dock = tgt;
        if (tgt) tgt.el.classList.add('dock-target');
      }
    } else {
      const dir = d.dir, sz = winSizeFor(w.wid, w.tab);
      if (dir.indexOf('e') >= 0) w.w = Math.max(sz.minW, d.ow + dx);
      if (dir.indexOf('s') >= 0) w.h = Math.max(sz.minH, d.oh + dy);
      if (dir.indexOf('w') >= 0) { const nw = Math.max(sz.minW, d.ow - dx); w.x = d.ox + (d.ow - nw); w.w = nw; }
      if (dir.indexOf('n') >= 0) { const nh = Math.max(sz.minH, d.oh - dy); w.y = d.oy + (d.oh - nh); w.h = nh; }
    }
    wmClamp(w);
    wmApplyGeom(w);
    wmRectsSoon();
    if (d.mode === 'size' && typeof auraIsHudTab === 'function' && auraIsHudTab(w.tab) && !w.min) renderPaneFor(w);
  });
  document.addEventListener('mouseup', () => {
    _tabDrag = null;            // press ended without travelling: it was a plain click
    if (!_wmDrag) return;
    const d = _wmDrag, w = d.w;
    w.el.classList.remove('dragging');
    w.el.classList.remove('sizing');
    _wmDrag = null;
    wm.dirty = true;
    if (d.dock) {
      d.dock.el.classList.remove('dock-target');
      if (wm.wins.get(d.dock.wid) === d.dock && wm.wins.get(w.wid) === w) wmDockInto(w, d.dock);
    }
    wmRectsSoon(); wmSaveSoon();
  });

