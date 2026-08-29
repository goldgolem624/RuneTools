// rtx-layout.js: Layout persistence (wmSnapshot, wmSaveNow, wmSaveSoon, wmLayoutRestore), openTab, tabEntryKicks.
// Loads after: rtx-wm.js; installs pagehide/resize listeners at load.
// Plain script, page globals by design: every top-level name here is a page global that panels and the other core files use.
  // ---- layout persistence (per-account host store; atomic tmp+rename native side) ----
  let _wmSaveT = 0;
  function wmSnapshot() {
    const wins = {};
    for (const [wid, w] of wm.wins) {
      wins[wid] = { tab: w.tab, tabs: w.tabs.slice(), x: Math.round(w.x), y: Math.round(w.y),
                    w: Math.round(w.w), h: Math.round(w.h), z: w.z, min: w.min ? 1 : 0,
                    roll: w.rolled ? 1 : 0, lock: w.locked ? 1 : 0 };
    }
    return { v: 1, bar: { x: wm.barX, y: wm.barY, yf: wm.barYf, pill: wm.barPill ? 1 : 0 },
             toast: { anchor: toastCfg.anchor, dx: toastCfg.dx | 0, dy: toastCfg.dy | 0, w: toastCfg.w | 0 },
             wins };
  }
  function wmSaveNow() {
    try {
      if (bridge() && bridge().layoutSave)
        bridge().layoutSave(myPid(), JSON.stringify(wmSnapshot()));
    } catch (e) {}
  }
  function wmSaveSoon() {
    if (!wm.restored) return;   // never clobber a saved layout with boot state
    if (_wmSaveT) return;
    _wmSaveT = setTimeout(() => { _wmSaveT = 0; wmSaveNow(); }, 500);
  }
  // The 500 ms debounce means a change made just before the view goes away was never written.
  // Flush any pending save on teardown.
  window.addEventListener('pagehide', () => {
    if (!wm.restored || !_wmSaveT) return;
    clearTimeout(_wmSaveT); _wmSaveT = 0;
    wmSaveNow();
  });
  async function wmLayoutRestore() {
    let saved = null;
    try {
      const s = await bridge().layoutLoad(myPid());
      saved = s ? JSON.parse(s) : null;
    } catch (e) {}
    if (!saved || saved.v !== 1) return false;   // first run, or account not resolved yet
    // Preferences: "restore panels on launch" off keeps the bar and alert placement but
    // starts with no windows open.
    if (uiCfg().restore === false) saved.wins = {};
    if (saved.bar) {
      if (typeof saved.bar.x === 'number') wm.barX = saved.bar.x;
      if (saved.bar.y === 'bottom' || saved.bar.y === 'top') wm.barY = saved.bar.y;
      if (typeof saved.bar.yf === 'number') wm.barYf = Math.min(1, Math.max(0, saved.bar.yf));
      wm.barPill = !!saved.bar.pill;
    }
    if (saved.toast && typeof saved.toast === 'object') {
      const t = saved.toast;
      if (TOAST_ANCHORS.some(a => a[0] === t.anchor)) toastCfg.anchor = t.anchor;
      if (typeof t.dx === 'number') toastCfg.dx = t.dx | 0;
      if (typeof t.dy === 'number') toastCfg.dy = t.dy | 0;
      if (typeof t.w === 'number' && t.w >= 220) toastCfg.w = t.w | 0;
      applyToastPos();
    }
    const ws = saved.wins || {};
    const order = Object.keys(ws).sort((a, b) => (ws[a].z || 0) - (ws[b].z || 0));
    for (const wid of order) {
      const st = ws[wid] || {};
      const avail = allTabs();
      // Docked set as saved, minus anything this build/account no longer shows and
      // anything an earlier window already claimed (a tab lives in exactly one window).
      const docked = Array.isArray(st.tabs) && st.tabs.length > 0;
      const ids = (docked ? st.tabs : [st.tab || wid])
        .filter(id => avail.some(x => x.id === id) && !wmWinOf(id));
      if (!ids.length) continue;              // nothing left to show -> no empty window
      const t = avail.find(x => x.id === (ids.indexOf(st.tab) >= 0 ? st.tab : ids[0]));
      // min and lock are NOT geometry: gating them on a sane w/h meant a window saved without
      // usable dimensions came back un-minimized and unlocked. Pass them either way and let
      // wmCreateWindow fall back to the default size/position when the box is unusable.
      const geom = (st.w > 0 && st.h > 0)
        ? { x: st.x | 0, y: st.y | 0, w: st.w | 0, h: st.h | 0, min: !!st.min, lock: !!st.lock }
        : { min: !!st.min, lock: !!st.lock, nogeom: 1 };
      const w2 = openTab(t, { noFocus: true, geom, restoring: true });
      // Only a docking-era save overrides the set openTab built; a pre-docking layout
      // has no `tabs` and must keep its group's members.
      if (w2 && docked) wmSetTabs(w2, ids);
      if (w2 && st.roll) wmToggleRoll(w2);
    }
    wm.restored = true;
    renderMenubar();
    syncMarkerArm();
    wmRectsSoon();
    return true;
  }

  // Open (or focus) the window for a tab. `t` = a tab object from allTabs().
  // opts: { noFocus, geom:{x,y,w,h,min} } for layout restore.
  function openTab(t, opts) {
    opts = opts || {};
    // Opening a window IS a layout change, but wmSaveSoon refuses to write until wm.restored,
    // and wm.dirty was only ever set by a drag or resize. So on a first run -- or during the
    // up-to-60s window while the account is still resolving and layoutLoad returns {} -- every
    // window the user opened was discarded unless they happened to drag one. Restores pass
    // `restoring` so replaying a saved layout does not mark it dirty.
    if (!opts.restoring) wm.dirty = true;
    const g = TAB_GROUP_OF[t.id] || null;
    let w = wmWinOf(t.id);   // wherever it is docked, not where its group would put it
    if (w) {
      if (w.tab !== t.id) wmSwitchTab(w, t);
      if (w.min) wmRestore(w);
      wmFocus(w);
      closeMbDrop();
      wmRectsSoon(); wmSaveSoon();
      return w;
    }
    w = wmCreateWindow(wmMintWid(t.id), t, g, opts.geom || null);
    if (g) { try { localStorage.setItem('rtxGrpLast:' + g.id, t.id); } catch (e) {} }
    if (!opts.noFocus && !w.min) wmFocus(w);
    if (!w.min) renderPaneFor(w);
    withPane(w, () => tabEntryKicks(w.tab));
    renderMenubar();
    closeMbDrop();
    wmRectsSoon(); wmSaveSoon();
    return w;
  }

  // Keep every window reachable when the client shrinks or DPI changes.
  window.addEventListener('resize', () => {
    for (const w of wm.wins.values()) { wmClamp(w); wmApplyGeom(w); }
    positionMenubar();
    applyToastPos();     // anchored, so it re-derives from the new client size
    wmRectsSoon();
  });

  // Pull the relevant data immediately on entering a tab rather than waiting for the next
  // poll -- and ARM anything the tab owns (vars observer, netprobe capture). Split out of
  // openTab so the dev hot-reload boot restore runs the SAME entry actions: a restored
  // Vars tab must arm its observer or varWatchOn stays false while the host observer from
  // the pre-reload page stays armed forever (the mismatched disarm early-returns).
  function tabEntryKicks(id) {
    // Registered panels own their entry kick (registerTab({ open })); the chain below is
    // what this script still renders itself, plus ids gated on more than the tab id.
    { const P = RTX.panels[id]; if (P && typeof P.open === 'function') P.open(); }
    if (id === 'materials') { matFetchKey = ''; fetchMaterials(); }
    if (id === 'baitbox') { baitFetchKey = ''; fetchBaitBox(); }
    if (id === 'artefacts') { wbFetchKey = ''; fetchWorkbench(); fetchArchShop(); }
    if (id === 'archshop') { matUnlockSig = ''; fetchArchShop(); }
    if (id === 'dung' && DUNG_ENABLED) { dungSig = ''; fetchDungeoneering(); }
    if (id === 'info' || id === 'player') fetchPlayerVp();
    if (id === 'player') fetchGameGoals();
  }
