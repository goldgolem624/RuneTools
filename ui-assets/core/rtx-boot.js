// rtx-boot.js: The poll loop (refresh, refreshPaneTick) and attachBridge. Spliced AFTER rtx_vars.js and every panel_*.js; client.html itself only calls attachBridge().
// Loads after: every core file and every panel: this is the last script before the inline attachBridge() call.
// Plain script, page globals by design: every top-level name here is a page global that panels and the other core files use.
  // localStorage seed: colours before first paint; prefsInit re-applies the durable copy.
  // Was the last load-time statement of rtx-ui.js; it runs here because uiApply reaches
  // bridge(), uiBarTick() and uiRepaintAll(), which only exist once every core file is in.
  try { uiApply(); } catch (e) {}

  async function refresh() {
    if (!bridge()) return;
    if (refresh._busy) return;      // 250 ms timer vs awaits: no overlapping passes
    refresh._busy = true;
    try {
    let snaps = null;
    try { snaps = JSON.parse(await bridge().gameSnapshots()); }
    catch (e) { return; }
    if (!Array.isArray(snaps)) return;
    const me = snaps.find(s => Number(s.pid) === myPid());
    lastSnap = me || null;
    // Rebind plugin consent to the logged-in character. Driven from here, not only from
    // renderPlugin, because a minimized plugin window keeps its frame mounted and can still
    // make calls -- so a character switch has to be noticed even when nothing is repainting.
    try { pluginGrantsEnsure(); } catch (e) {}
    // Keep the overlay metronome pointed at the current client.
    if ((metroVisual() || metroAudio()) && myPid() !== _metroAppliedPid) applyMetroOverlay();
    // Keep the XP tracker sampler on the active client across relogs, shown or not.
    if ((xpOn || _xpAppliedPid) && myPid() && myPid() !== _xpAppliedPid) applyXpOverlay();
    // Alerts run every poll regardless of the visible tab.
    if (!alertCfg) loadAlertCfg();
    // Pinned nameplates: load once, then keep scene data fresh so pins re-apply to
    // the right entity uid across relogs even off the Scene tab.
    if (typeof nameplatesLoaded !== 'undefined' && !nameplatesLoaded) loadNameplateNames();
    // Chat log accumulates in the background so it's complete even if the tab was
    // never opened; in-world only.
    if (lastSnap && lastSnap.in_world) fetchChat(false);
    if ((alertsNeedScene() || (typeof nameplatesActive === 'function' && nameplatesActive())) && !paneVisible('scene')) fetchScene();
    if (alertsNeedInfo() && !paneVisible('info')) fetchInfo();
    if (alertsNeedVitals() && !paneVisible('info') && !paneVisible('player')) fetchPlayerVp();
    // Dailies availability bells: keep the activity varbits polled (10s background
    // throttle inside fetchDailies) so armed notifications fire with the window closed.
    if (typeof dwNotifyAny === 'function' && dwNotifyAny() && !paneVisible('dailies')) fetchDailies(false, true);
    // World-event spawn bells (scarabs / soul obelisk): pure cache reads, so this is
    // cheap and runs regardless of which windows are open.
    if (typeof scNotifyPoll === 'function') scNotifyPoll();
    // Pinned league tasks: sticky progress toasts stay live with every poll,
    // whichever windows are open (self-gating: immediate no-op without pins).
    if (typeof lgPinsPoll === 'function') lgPinsPoll();
    // In-game Skills XP bars: drawn on the GAME's panel, so this runs regardless of
    // which of our windows are open (self-gating: immediate no-op when off).
    skBarsTick();
    if (alertsNeedGoals() && !paneVisible('player')) fetchGameGoals();
    if (alertsNeedPerks() && !paneVisible('perks')) fetchPerks();
    if (alertsNeedInv() && !paneVisible('inventory')) fetchInv();
    if (alertsNeedBuffs() && !paneVisible('buffs')) fetchBuffs();
    if (alertsNeedFarming() && !paneVisible('farming')) fetchFarming();
    if (typeof alertsNeedGround === 'function' && alertsNeedGround()) fetchGround();
    evalAlerts();
    syncOverlayHighlight();
    // ---- Live PANEL fetches: once per OPEN window, with the dispatch context
    // (activeTab + $('content')) bound to that window so refreshPaneTick's
    // untouched per-tab gates fire exactly once for their own panel.
    for (const _w of wmVisibleWins()) withPane(_w, refreshPaneTick);
    // Self-gating fetches: window-visible OR a background need (quest / mystery
    // step-NPC marking follows progress even with the window closed).
    if (paneVisible('quests') || paneVisible('questfocus') ||
        (questGSteps && questGSteps.__focus)) fetchQuests();
    if (paneVisible('xptracker') || paneVisible('xpmeter')) fetchXpTracker();
    if (paneVisible('archmysteries') || paneVisible('mystfocus') ||
        mystSteps === null || !!mystFocusName()) fetchArchMysteries();
    // Host info changes slowly; refresh it once per second to keep RAM live.
    if (!host || (refresh._hostT = (refresh._hostT||0) + 1) % 4 === 0) {
      try { host = JSON.parse(await bridge().hostInfo()); } catch (e) {}
    }
    // Status strip: one cheap bridgeStatus per tick (cached snapshot list, no new reads).
    try { updateStatusStrip(await bridgeJson('bridgeStatus', myPid())); } catch (e) {}
    renderHeader();
    for (const _w of wmVisibleWins()) {
      // Live-refresh hook: a registered panel may run per-tick work (registerTab({ refresh }))
      // ahead of its render; most panels just let render run on the tick.
      const P = RTX.panels[_w.tab];
      if (P && typeof P.refresh === 'function') { try { P.refresh(); } catch (e) {} }
      renderPaneFor(_w);
    }
    sizeAllIcons();
    pluginPush();     // feed the host tick/state to every mounted plugin window
    } finally { refresh._busy = false; }
  }

  // One open window's per-poll fetches; activeTab/__paneRoot are bound to it by
  // the caller (withPane), so the per-tab gates below read as before.
  function refreshPaneTick() {
    {
    // Markers tab: keep the list live so keybind-added markers appear.
    if (activeTab === 'markers') fetchMarkers(false);
    // Bank: fetch on first view and when content changes (cached_at bumps only on
    // change) or it opens/closes; the key compare avoids refetching every poll.
    if (activeTab === 'bank' && lastSnap) {
      const key = (lastSnap.bank_open ? '1' : '0') + '|' + (lastSnap.bank_cached_at || 0);
      // Keep retrying while empty & closed so a momentary empty result (before the
      // character name resolves) isn't cached as final.
      const emptyClosed = !lastSnap.bank_open &&
                          (!bankData || !bankData.items || bankData.items.length === 0);
      if (!bankData || key !== bankFetchKey || emptyClosed) {
        bankFetchKey = key; fetchBank();
      }
    }
    // Metal bank: same snapshot-gated fetch as the bank.
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
    // Live containers: refresh while their tab is open (cheap container walk).
    if (activeTab === 'inventory') fetchInv();
    if (activeTab === 'equipment') fetchEquip();
    if (activeTab === 'components') fetchComponents();   // Invention component counts (live varps)
    if (activeTab === 'machines') fetchMachinesTab();     // Invention machine varbits (live)
    if (activeTab === 'storage') fetchStorage();
    if (activeTab === 'containers') fetchContainers();
    if (activeTab === 'farming') fetchFarming();
    if (activeTab === 'pof') fetchPof();
    // Nearby NPCs: refresh while the Scene tab is open (cheap hashmap walk).
    if (activeTab === 'scene') fetchScene();
    if (activeTab === 'perks') fetchPerks();
    // Auras is a HUD: it must keep ticking while you are on some other tab, or looking at
    // no tab at all, so it polls on pane VISIBILITY rather than on being the active one.
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
    // Tracker countdowns are wall-clock: repaint every poll tick (renderPane only fires
    // on tab switch, so without this the "time left" pill froze at its first value).
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


  function attachBridge() {
    if (!bridge()) { setTimeout(attachBridge, 100); return; }
    // Durable preferences first: several panels read their seed at parse time, and this is
    // what restores them on the first run after an app update wiped localStorage. Async, so
    // each of those panels also gets a re-apply hook (see prefsInit).
    _prefsInitP = prefsInit();
    // Load tile-marker state first so the initial pushOverlay() reflects whether markers
    // should render; this starts the overlay thread for accounts that already have
    // markers, without opening the tab.
    loadMarkerKb(); fetchMarkers(true);
    // Keybind capture: while a "Press a key..." field is armed the next keypress
    // becomes the binding (Esc cancels); normal typing is untouched.
    document.addEventListener('keydown', e => {
      if (!markerCapturing) return;
      e.preventDefault();
      const vk = e.keyCode || e.which || 0;
      if (vk && vk !== 27) { markerKb[markerCapturing] = vk; saveMarkerKb(); }
      markerCapturing = null; paintKbKeys();
    }, true);
    // Register this pid with the overlay up front (overlay stays off): random-event
    // highlights need a target window even with the grid disabled.
    pushOverlay();
    pluginBrokerInit();
    renderMenubar();
    applyToastPos();   // defaults until wmLayoutRestore supplies the saved placement
    // Layout restore: layoutLoad returns "{}" until the account resolves (per-account
    // store), so retry for up to a minute; then fall back to a fresh default layout.
    // A user opening windows meanwhile marks the layout dirty and wins.
    (async function bootLayout(tries) {
      if (wm.restored) return;
      if (wm.dirty) { wm.restored = true; wmSaveSoon(); return; }
      // Plugin tabs must exist before the first restore attempt, or saved plugin
      // windows are silently dropped by the allTabs() lookup.
      if (!bootLayout._plugins) { bootLayout._plugins = 1; try { await loadPlugins(); } catch (e) {} }
      // Same for aura groups: their HUD tabs come from the durable prefs, so the prefs file must
      // be in before the restore runs or every saved group window is dropped as unknown.
      if (_prefsInitP) { try { await _prefsInitP; } catch (e) {} }
      let ok = false;
      try { ok = await wmLayoutRestore(); } catch (e) {}
      // The HUD widgets are driven by their own saved settings, not only by the window
      // layout: a user upgrading from the companion-drawn versions has rtxXpOn/rtxMetroMode
      // set but no window saved for them, and would otherwise see them silently disappear.
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
    // Keep the companion var-capture observer ON at all times (not just on the Vars
    // tab): it resolves client-side vars (panel origins, dialogue rects) the game
    // hashmaps don't keep current, else those highlights read stale. Cheap flag,
    // re-asserted each tick so it survives client switches.
    setInterval(function () { try { if (bridge() && bridge().varsWatch) bridge().varsWatch(myPid(), true); } catch (e) {} }, 250);
    setInterval(function () { try { if (typeof sndTick === 'function') sndTick(); } catch (e) {} }, 250);   // Sounds transport (no-op unless that tab is active)
    setInterval(function () { try { if (typeof mnuTick === 'function') mnuTick(); } catch (e) {} }, 300);   // Right-click menu inspector (no-op unless that tab is active)
    setInterval(knotTick, 250);       // celtic-knot arrow overlay: tab-independent so it tracks the panel anywhere
    // Dungeoneering's in-world helpers are tab-INDEPENDENT, so they must honour DUNG_ENABLED too,
    // or a hidden Dungeoneering still draws its room-solver highlights in Daemonheim.
    if (DUNG_ENABLED) {
      setInterval(dungSceneTick, 600);  // Dungeoneering ghost / sliding-puzzle in-scene highlight
      setInterval(dungLodeTimerTick, 100);  // Dungeoneering crystal-room ms click countdown (centre text)
    }
    setInterval(megAnswerTick, 600);  // Meg weekly-question best-answer highlight (dialogue 1188, tab-independent)
    // lockbox solver overlay (interface 1933): tab-independent; fast (~90ms) while
    // open so highlights track each click, slow (~300ms) idle.
    (function lockboxLoop() { try { lockboxTick(); } catch (e) {} setTimeout(lockboxLoop, lockboxOwnsPanel ? 90 : 300); })();
    // towers (Skyscrapers) solver overlay (interface 1934): tab-independent, adaptive like the lockbox.
    (function towersLoop() { try { towersTick(); } catch (e) {} setTimeout(towersLoop, towersOwnsPanel ? 120 : 400); })();
    setInterval(ifaceMonTick, 600);   // Interfaces tab: open/close monitor (no-op unless that tab is active)
    loadPlugins();   // discover installed (signed) + sideloaded dev plugins -> Plugins category
    setInterval(pluginDevWatch, 2000);   // hot-reload sideloaded dev plugins on folder changes
  }
