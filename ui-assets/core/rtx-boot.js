  try { uiApply(); } catch (e) {}

  let _refreshFailMsg = '';
  attachBridge._waits = 0;
  function refreshFail(msg) { if (msg === _refreshFailMsg) return; _refreshFailMsg = msg; console.error('rtx refresh: ' + msg); }
  // Per-account fullscreen preference: applied once the host window exists (retried for ~15 s).
  let _fsPrefPid = 0, _fsPrefTries = 0;
  function fullscreenPrefApply() {
    const pid = myPid();
    if (!pid || _fsPrefPid === pid || !lastSnap || !lastSnap.display_name) return;
    const b = bridge();
    if (!b || !b.fullscreenPrefLoad || !b.hostFullscreen) { _fsPrefPid = pid; return; }
    if (String(b.fullscreenPrefLoad(pid) || '').trim() !== '1') { _fsPrefPid = pid; return; }
    if (b.hostFullscreen(pid, true)) {
      _fsPrefPid = pid;
      if (typeof wm !== 'undefined' && wm) wm.fullscreen = true;
      const r = $('fsTgl'); if (r) r.classList.add('on');
      if (typeof wmRectsSoon === 'function') wmRectsSoon();
    } else if (++_fsPrefTries > 60) {
      _fsPrefPid = pid;
    }
  }
  // Rune Caches: the launcher's heartbeat thread learns about drops from runetools.io; each client
  // window asks once every few seconds whether its own character earned one and announces it.
  function lootTick() {
    try {
      const b = bridge();
      if (!b || !b.lootPoll || !myPid()) return;
      const st = JSON.parse(b.lootPoll(myPid()) || '{}');
      if (!st || !st.enabled || !st.drop) return;   // opt-in in Settings; off means nothing is announced
      const more = st.unopened > 1 ? ' You have ' + st.unopened + ' waiting.' : '';
      uiNotify('Rune Cache obtained! Open it on the RuneTools website at runetools.io/loot.' + more, { sticky: true });
      try { if (b.playSound) b.playSound('alert1'); } catch (e) {}
    } catch (e) {}
  }

  async function refresh() {
    if (!bridge()) { refreshFail('bridge missing: typeof window.rtx = ' + typeof window.rtx); return; }
    if (refresh._busy) return;      // 250 ms timer vs awaits: no overlapping passes
    refresh._busy = true;
    try {
    let snaps = null;
    try { snaps = JSON.parse(await bridge().gameSnapshots()); }
    catch (e) { refreshFail('gameSnapshots threw: ' + (e && e.message ? e.message : e)); return; }
    if (!Array.isArray(snaps)) { refreshFail('gameSnapshots returned non-array: ' + String(JSON.stringify(snaps)).slice(0, 200)); return; }
    const me = snaps.find(s => Number(s.pid) === myPid());
    if (!me) refreshFail('no snapshot for pid ' + myPid() + '; have [' + snaps.map(s => s.pid).join(',') + ']');
    else if (_refreshFailMsg) { _refreshFailMsg = ''; console.log('rtx refresh: snapshot for pid ' + myPid() + ' resumed'); }
    lastSnap = me || null;
    try { fullscreenPrefApply(); } catch (e) {}
    try { pluginGrantsEnsure(); } catch (e) {}
    if ((metroVisual() || metroAudio()) && myPid() !== _metroAppliedPid) applyMetroOverlay();
    if ((xpOn || _xpAppliedPid) && myPid() && myPid() !== _xpAppliedPid) applyXpOverlay();
    if (!alertCfg) loadAlertCfg();
    if (typeof nameplatesLoaded !== 'undefined' && !nameplatesLoaded) loadNameplateNames();
    if (lastSnap && lastSnap.in_world) fetchChat(false);
    if (lastSnap && lastSnap.in_world && typeof fetchCombatLog === 'function') fetchCombatLog();
    if ((alertsNeedScene() || (typeof nameplatesActive === 'function' && nameplatesActive())) && !paneVisible('scene')) fetchScene();
    if (alertsNeedInfo() && !paneVisible('info')) fetchInfo();
    if (alertsNeedVitals() && !paneVisible('info') && !paneVisible('player')) fetchPlayerVp();
    // Dailies availability bells: keep the activity varbits polled (10s background
    if (typeof dwNotifyAny === 'function' && dwNotifyAny() && !paneVisible('dailies')) fetchDailies(false, true);
    if (typeof scNotifyPoll === 'function') scNotifyPoll();
    if (typeof lgPinsPoll === 'function') lgPinsPoll();
    skBarsTick();
    if (alertsNeedGoals() && !paneVisible('player')) fetchGameGoals();
    if (alertsNeedPerks() && !paneVisible('perks')) fetchPerks();
    if (alertsNeedInv() && !paneVisible('inventory')) fetchInv();
    if (alertsNeedBuffs() && !paneVisible('buffs')) fetchBuffs();
    if (alertsNeedFarming() && !paneVisible('farming')) fetchFarming();
    if (typeof alertsNeedGround === 'function' && alertsNeedGround()) fetchGround();
    evalAlerts();
    syncOverlayHighlight();
    for (const _w of wmVisibleWins()) withPane(_w, refreshPaneTick);
    if (paneVisible('quests') || paneVisible('questfocus') ||
        (questGSteps && questGSteps.__focus)) fetchQuests();
    if (paneVisible('xptracker') || paneVisible('xpmeter')) fetchXpTracker();
    if (paneVisible('archmysteries') || paneVisible('mystfocus') ||
        mystSteps === null || !!mystFocusName()) fetchArchMysteries();
    if (!host || (refresh._hostT = (refresh._hostT||0) + 1) % 4 === 0) {
      try { host = JSON.parse(await bridge().hostInfo()); } catch (e) {}
    }
    try { updateStatusStrip(await bridgeJson('bridgeStatus', myPid())); } catch (e) {}
    renderHeader();
    for (const _w of wmVisibleWins()) {
      const P = RTX.panels[_w.tab];
      if (P && typeof P.refresh === 'function') { try { P.refresh(); } catch (e) {} }
      renderPaneFor(_w);
    }
    sizeAllIcons();
    pluginPush();     // feed the host tick/state to every mounted plugin window
    } finally { refresh._busy = false; }
  }

  function refreshPaneTick() {
    {
    if (activeTab === 'markers') fetchMarkers(false);
    if (activeTab === 'bank' && lastSnap) {
      const key = (lastSnap.bank_open ? '1' : '0') + '|' + (lastSnap.bank_cached_at || 0);
      const emptyClosed = !lastSnap.bank_open &&
                          (!bankData || !bankData.items || bankData.items.length === 0);
      if (!bankData || key !== bankFetchKey || emptyClosed) {
        bankFetchKey = key; fetchBank();
      }
    }
    if (activeTab === 'metalbank' && lastSnap) {
      const key = (lastSnap.metalbank_open ? '1' : '0') + '|' + (lastSnap.metalbank_cached_at || 0);
      const emptyClosed = !lastSnap.metalbank_open && (!mbankData || !mbankData.items || mbankData.items.length === 0);
      if (!mbankData || key !== mbankFetchKey || emptyClosed) { mbankFetchKey = key; fetchMetalBank(); }
    }
    if (activeTab === 'materials' && lastSnap) {
      const key = (lastSnap.materials_open ? '1' : '0') + '|' + (lastSnap.materials_cached_at || 0);
      const emptyClosed = !lastSnap.materials_open && (!matData || !matData.items || matData.items.length === 0);
      if (!matData || key !== matFetchKey || emptyClosed) { matFetchKey = key; fetchMaterials(); }
    }
    if (activeTab === 'groupbank' && lastSnap) {
      const key = (lastSnap.groupbank_open ? '1' : '0') + '|' + (lastSnap.groupbank_cached_at || 0);
      const emptyClosed = !lastSnap.groupbank_open && (!gbankData || !gbankData.items || gbankData.items.length === 0);
      if (!gbankData || key !== gbankFetchKey || emptyClosed) { gbankFetchKey = key; fetchGroupBank(); }
    }
    if (activeTab === 'baitbox' && lastSnap) {
      const key = (lastSnap.baitbox_open ? '1' : '0') + '|' + (lastSnap.baitbox_cached_at || 0);
      const emptyClosed = !lastSnap.baitbox_open && (!baitData || !baitData.items || baitData.items.length === 0);
      if (!baitData || key !== baitFetchKey || emptyClosed) { baitFetchKey = key; fetchBaitBox(); }
    }
    if (activeTab === 'artefacts' && lastSnap) {
      const key = (lastSnap.workbench_open ? '1' : '0') + '|' + (lastSnap.workbench_cached_at || 0);
      const emptyClosed = !lastSnap.workbench_open && (!wbData || !wbData.items || wbData.items.length === 0);
      if (!wbData || key !== wbFetchKey || emptyClosed) { wbFetchKey = key; fetchWorkbench(); }
    }
    if (activeTab === 'inventory') fetchInv();
    if (activeTab === 'equipment') fetchEquip();
    if (activeTab === 'components') fetchComponents();   // Invention component counts (live varps)
    if (activeTab === 'machines') fetchMachinesTab();     // Invention machine varbits (live)
    if (activeTab === 'storage') fetchStorage();
    if (activeTab === 'containers') fetchContainers();
    if (activeTab === 'farming') fetchFarming();
    if (activeTab === 'pof') fetchPof();
    if (activeTab === 'scene') fetchScene();
    if (activeTab === 'perks') fetchPerks();
    if (paneVisible('auras') || (typeof auraAnyHudVisible === 'function' && auraAnyHudVisible()) || (typeof auraNeedsPoll === 'function' && auraNeedsPoll())) fetchAuras();
    if (activeTab === 'buffs') fetchBuffs();
    if (activeTab === 'abilities') fetchAbilities();
    if (activeTab === 'pets') fetchPets();
    if (activeTab === 'bosses') fetchBosses();
    if (activeTab === 'achievements') fetchAchievements();
    if (activeTab === 'combatmastery') fetchAchievements();
    if (activeTab === 'areatasks') fetchAreaTasks();
    if (activeTab === 'gimtasks') fetchGimTasks();
    if (activeTab === 'archshop') fetchArchShop();
    if (activeTab === 'familiar') fetchFamiliar();
    if (activeTab === 'dung' && DUNG_ENABLED) fetchDungeoneering();
    if (activeTab === 'archresearch') fetchArchResearch();
    if (activeTab === 'relics') fetchRelics();
    if (activeTab === 'reputation') fetchReputation();
    if (activeTab === 'wardrobe') fetchWardrobe();
    if (activeTab === 'resdungeons') fetchResDungeons();
    if (activeTab === 'fairyrings') fetchFairyRings();
    if (activeTab === 'bossinfo') fetchBossInfo();
    if (activeTab === 'hideyholes') fetchHidey();
    if (activeTab === 'clues') { fetchClues(); fetchGlobetrotter(); }
    if (activeTab === 'lodestones') fetchLodestones();
    if (activeTab === 'scarabs') renderScarabs();
    if (activeTab === 'obelisks') renderObelisks();
    if (activeTab === 'tasks') fetchTasks();
    if (activeTab === 'dailies') fetchDailies();
    if (activeTab === 'kingdom') fetchKingdom();
    if (activeTab === 'rituals') fetchRituals();
    if (activeTab === 'toolbelt') fetchToolbelt();
    if (activeTab === 'shopcaps') fetchShopCaps();
    if (activeTab === 'currencies') fetchCurrencies();
    if (activeTab === 'farmcol') fetchFarmCol();
    if (activeTab === 'archcol') fetchArchCol();
    if (activeTab === 'collections') fetchClueCol();
    if (activeTab === 'leagues') fetchLeagues();
    if (activeTab === 'info') fetchInfo();
    if (activeTab === 'info' || activeTab === 'player') fetchPlayerVp();
    if (activeTab === 'player') fetchGameGoals();
    if (activeTab === 'vars') fetchVars();
    }
  }


  // Event channel poll: 100 ms, independent of refresh (which stays at 4 Hz). Drains
  const rtxEventsPoll = { seq: 0, pid: 0, tick: -1, tickAt: 0, busy: false, polls: 0, fails: 0 };
  async function rtxEventsTick() {
    if (rtxEventsPoll.busy || !bridge() || !bridge().events) return;
    rtxEventsPoll.busy = true;
    try {
      const pid = myPid();
      if (pid !== rtxEventsPoll.pid) { rtxEventsPoll.pid = pid; rtxEventsPoll.seq = 0; rtxEventsPoll.tick = -1; }
      const d = await rtxData.call('state.events', rtxEventsPoll.seq);
      rtxEventsPoll.polls++;
      if (!d || d.ok === false) { if (rtxEventsPoll.ok) rtxEventsPoll.fails++; return; }
      rtxEventsPoll.ok = true;
      if (typeof d.tick === 'number' && d.tick >= 0 && d.tick !== rtxEventsPoll.tick) {
        const now = performance.now();
        const dt = rtxEventsPoll.tick >= 0 ? now - rtxEventsPoll.tickAt : 0;
        rtxEventsPoll.tick = d.tick; rtxEventsPoll.tickAt = now;
        rtxEvents.emit('gameTick', { kind: 'gameTick', tick: d.tick, dtMs: dt });
      }
      const evs = Array.isArray(d.events) ? d.events : [];
      for (const ev of evs) {
        if (!(ev.seq > rtxEventsPoll.seq)) continue;   // served() may replay the previous span
        rtxEventsPoll.seq = ev.seq;
        rtxEvents.emit(ev.kind || 'raw', ev);
      }
      if (typeof d.seq === 'number' && d.seq > rtxEventsPoll.seq) rtxEventsPoll.seq = d.seq;
    } catch (e) { rtxEventsPoll.fails++; }
    finally { rtxEventsPoll.busy = false; }
  }
  window.rtxEventsPoll = rtxEventsPoll;   // the Events panel shows poll health from here

  function attachBridge() {
    if (!bridge()) {
      if (!attachBridge._waits++) console.log('rtx boot: waiting for the bridge (typeof window.rtx = ' + typeof window.rtx + ', pid ' + myPid() + ')');
      setTimeout(attachBridge, 100); return;
    }
    console.log('rtx boot: bridge attached after ' + attachBridge._waits + ' waits, pid ' + myPid());
    _prefsInitP = prefsInit();
    loadMarkerKb(); fetchMarkers(true);
    document.addEventListener('keydown', e => {
      if (!markerCapturing) return;
      e.preventDefault();
      const vk = e.keyCode || e.which || 0;
      if (vk && vk !== 27) { markerKb[markerCapturing] = vk; saveMarkerKb(); }
      markerCapturing = null; paintKbKeys();
    }, true);
    pushOverlay();
    pluginBrokerInit();
    renderMenubar();
    applyToastPos();   // defaults until wmLayoutRestore supplies the saved placement
    (async function bootLayout(tries) {
      if (wm.restored) return;
      if (wm.dirty) { wm.restored = true; wmSaveSoon(); return; }
      if (!bootLayout._plugins) { bootLayout._plugins = 1; try { await loadPlugins(); } catch (e) {} }
      if (_prefsInitP) { try { await _prefsInitP; } catch (e) {} }
      let ok = false;
      try { ok = await wmLayoutRestore(); } catch (e) {}
      if (ok) { try { xpSyncWindow(true); metroSyncWindow(true); } catch (e) {} return; }
      if (tries > 0 && !wm.dirty) { setTimeout(() => bootLayout(tries - 1), 2000); return; }
      wm.restored = true;                       // enable saves from here on
      if (!wm.wins.size && !wm.dirty) {
        const t = allTabs().find(x => x.id === 'player');
        if (t) openTab(t);                      // first run: a single Skills window
      }
      try { xpSyncWindow(); metroSyncWindow(); } catch (e) {}
    })(30);
    wmRectsSoon();
    refresh();
    setInterval(refresh, 250);
    console.log('rtx boot: refresh scheduled');
    setInterval(rtxEventsTick, 150);   // event channel (rtxEvents); not tied to refresh
    // varsWatch is a latch in the companion share (it only clears on a companion reset): re-arm at 0.5 Hz
    setInterval(function () { try { if (bridge() && bridge().varsWatch) bridge().varsWatch(myPid(), true); } catch (e) {} }, 2000);
    setInterval(function () { try { if (typeof sndTick === 'function') sndTick(); } catch (e) {} }, 250);   // Sounds transport (no-op unless that tab is active)
    setInterval(function () { try { if (typeof mnuTick === 'function') mnuTick(); } catch (e) {} }, 300);   // Right-click menu inspector (no-op unless that tab is active)
    setInterval(knotTick, 250);       // celtic-knot arrow overlay: tab-independent so it tracks the panel anywhere
    setInterval(lootTick, 5000);      // Rune Caches: announce a cache earned by this character (linked launchers only)
    if (DUNG_ENABLED) {
      setInterval(dungSceneTick, 600);  // Dungeoneering ghost / sliding-puzzle in-scene highlight
      setInterval(dungLodeTimerTick, 100);  // Dungeoneering crystal-room ms click countdown (centre text)
    }
    setInterval(megAnswerTick, 600);  // Meg weekly-question best-answer highlight (dialogue 1188, tab-independent)
    // lockbox solver overlay (interface 1933): tab-independent; 90 ms while it owns the panel, else 300.
    (function lockboxLoop() { try { lockboxTick(); } catch (e) {} setTimeout(lockboxLoop, lockboxOwnsPanel ? 90 : 600); })();
    // towers (Skyscrapers) solver overlay (interface 1934): tab-independent, adaptive like the lockbox.
    (function towersLoop() { try { towersTick(); } catch (e) {} setTimeout(towersLoop, towersOwnsPanel ? 120 : 800); })();
    setInterval(ifaceMonTick, 600);   // Interfaces tab: open/close monitor (no-op unless that tab is active)
    loadPlugins();   // discover installed (signed) + sideloaded dev plugins -> Plugins category
    setInterval(pluginDevWatch, 2000);   // hot-reload sideloaded dev plugins on folder changes
  }

