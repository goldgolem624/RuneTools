// rtx-player.js: Player State tab data (fetchInfo, fetchPlayerVp, fetchGameGoals, skillGoalProgress), tick metronome config and XP tracker overlay config (metroSyncWindow, xpSyncWindow, setXpLock...).
// Loads after: rtx-bridge.js; reads localStorage seeds at load (rtx-shim.js).
// Plain script, page globals by design: every top-level name here is a page global that panels and the other core files use.
  // ---- Group Bank tab -> panel_groupbank.js (spliced inline at load) ----
  // ---- Scene tab -> panel_scene.js (spliced inline at load) ----
  // ---- Overlay tab -> panel_overlay.js (spliced inline at load) ----
  // ---- Markers tab -> panel_markers.js (spliced inline at load) ----
  // ---- Rendering tab -> panel_rendering.js (spliced inline at load) ----
  // ---- Alerts tab -> panel_alerts.js (spliced inline at load) ----
  // ---- Perks tab -> panel_perks.js (spliced inline at load) ----
  // ---- Buffs tab -> panel_buffs.js (spliced inline at load) ----
  // ---- Info tab (local player state) ----
  let infoData = null;
  let infoVb = null;   // extra Player State varbits: 4163 = total penguin points
  let infoCam = null;  // live camera varcs: 1971 zoom, 5114 pitch, 5115 yaw (0..16284 = full circle)
  let infoHover = null;   // engine hover slot (hoverEntity bridge): live id/name/action/tile verification
  let infoHoverHeld = null;   // last REAL hover target, held so the panel stays readable after unhover
  // Membership tier. Free-vs-member is NOT var state - it is the PLAYERMEMBER engine op - so it
  // needs its own bridge call rather than riding the varp/varbit fetches below. Premier IS a
  // varbit (50572) but is folded into the same call so the tier arrives consistent.
  let infoMember = null;
  async function fetchInfo() {
    if (!bridge() || !bridge().playerInfo) return;
    try { infoData = JSON.parse(await bridge().playerInfo(myPid())); } catch (e) { infoData = null; }
    if (bridge().hoverEntity) { try { infoHover = JSON.parse(bridge().hoverEntity(myPid()) || '{}'); } catch (e) { infoHover = null; } }
    if (bridge().membership) { try { infoMember = JSON.parse(await bridge().membership(myPid()) || 'null'); } catch (e) { infoMember = null; } }
    if (bridge().varbits) { try { infoVb = JSON.parse(await bridge().varbits(myPid(), String(typeof VB !== 'undefined' ? VB.PENGUIN_POINTS : 4163)) || 'null'); } catch (e) {} }
    if (bridge().varcsDumpAll) {
      try {
        const vc = JSON.parse(await bridge().varcsDumpAll(myPid()) || '{}');
        // varcsDumpAll keys are scope-prefixed "5:<id>" (5 = varc-int)
        infoCam = { zoom: vc['5:1971'], pitch: vc['5:5114'], yaw: vc['5:5115'] };
      } catch (e) { infoCam = null; }
    }
    paneRun('info', renderInfo);
  }

  // Player-state varps shared by the Player State panel and the Skills footer
  // (virtual-levels-enabled). Varp-hashmap decodes:
  //   458  bit30 = "virtual levels" display setting (varbit 19007)
  //   13537 HP: the CURRENT lifepoints, raw (no scaling, nothing packed above bit 15).
  //   13538 HP MAX: the maximum lifepoints, raw. Located live (2026-08-17) by value-search
  //         over the varp map at 1,068/1,500 (only 13538 held 1500) and confirmed on a second
  //         account at 5,600/5,600. Engine-fed like 13537; no clientscript references either.
  //         Varp 659 used to carry cur = (v & 0xffff)/2, max = (v>>>16)&0xffff; it now reads 0
  //         permanently and no clientscript in the cache mentions it, so it is gone, not merely
  //         idle. It is left OUT of the request list rather than kept as a fallback: a dead varp
  //         reads 0, and 0 is indistinguishable from a real low reading. Max lives in 13538 (above).
  //   3274 Prayer:    cur = (v & 0x7fff)/10, max = ((v>>>16)&0x7f)*10 (points)
  //   8040 Summoning: cur = (v & 0x7fff)/10, max = ((v>>>16)&0x7f)*10 (points)
  //   1787 Familiar special-move (spell) points: raw current value, max const 60 (CS2 script777/11687)
  //   679  Adrenaline = v/10 (%)            463 Run toggle (0 walk / 1 run)
  //   1297 Quest points earned
  // 4818 = account-mode flags; 5984 = charge-pack charge (raw, /3000), 5982 = capacity
  // blueprints, 5991 = combat drain rate (CS2-confirmed). Leagues identity (CS2-reviewed):
  // 12314 = league NUMBER, matched against db 326.0 by script20117 (0 = not a league
  // character; gates read "> 0" everywhere, "== 2" for Equilibrium paths); league points
  // varp per the header's db 326.28 var_reference: L1 12426, L2 13521.
  const PLAYER_VARP_IDS = '458,13537,13538,3274,8040,1787,679,463,1297,4818,5984,5982,5991,12314,12426,13521';
  let playerVp = null;
  // Account-wide varbits shown on the Info tab. 39152 (bits 0-15) = TOTAL Dungeoneering floors
  // completed all-time: player state, not per-floor state, so it belongs here.
  // 58389 = league tasks completed (script21081's region-unlock counter). NOTE
  // vb 58421 is NOT fetched: it is a server multiplier used only by the archaeology
  // restoration XP preview (script14688/20151) and does not match the headline tier
  // multiplier (observed 10 while the tier table said 12). The displayed multiplier
  // is derived from the tier table (db 328.4) against live points instead.
  const PLAYER_VARBIT_IDS = '39152,58389';
  let playerVb = null;
  async function fetchPlayerVp() {
    if (!bridge() || !bridge().varps) return;
    // Require a NON-EMPTY object: VarpsJson returns "{}" when it cannot attach to the game, and
    // accepting that set playerVp to an empty map where every vital read 0 -- which reads as a
    // real value everywhere downstream rather than as "not read".
    try { const d = JSON.parse(await bridge().varps(myPid(), PLAYER_VARP_IDS));
          if (d && typeof d === 'object' && Object.keys(d).length) playerVp = d; } catch (e) {}
    if (!bridge().varbits) return;
    try { const v = JSON.parse(await bridge().varbits(myPid(), PLAYER_VARBIT_IDS)); if (v && typeof v === 'object') playerVb = v; } catch (e) {}
  }
  function virtualLevelsOn() { return !!(playerVp && (((playerVp['458'] || 0) >>> 30) & 1)); }

  // ---- In-game skill targets (read live from the "Set target" varps) ----
  // skill index (= stat id) -> RS3 skills-interface skillId, from cache enum 1482.
  const GOAL_STAT_SKILLID = [1,5,2,6,3,7,4,16,18,19,15,17,11,14,13,9,8,10,20,21,12,23,22,24,25,26,27,28,29];
  // PRIMARY path (all classic skills AND Dungeoneering): varp 1115 = has-target bitmask (by
  // skillId), 1117 = type bitmask (1=level,0=xp), value = varp(1117+skillId). Dungeoneering
  // (skill index 24 -> skillId 25 -> varp 1142) is on THIS table.
  // EXCLUSIVE dedicated-var skills (NOT on the arithmetic table; their arithmetic slot holds
  // stale data): Divination (varp 3839), Invention (6095), Archaeology (9410), Necromancy
  // (11204). The dedicated var holds the LEVEL when <= the skill's cap, else the raw XP target;
  // 0 = no goal. For these the table is NEVER consulted.
  // Skill index -> the varp holding that skill's TARGET VALUE, for the four skills that are not
  // on the arithmetic table. Decoded from clientscript-4037, which maps stat id -> value varp:
  // case 26 -> 3839 (Divination), 27 -> 6095 (Invention), 28 -> 9410 (Archaeology),
  // 29 -> 11204 (Necromancy). Those stat ids line up with GOAL_STAT_SKILLID above.
  //
  // Each of these varps has a PARTNER (3840 / 6096 / 9411 / 11205, and value+25 on the classic
  // table). The partner is NOT a goal-type flag - live capture showed 9411 = 10 with no target
  // set at all - and neither varp is cleared when a target is removed, which is why stale values
  // sit in them. Whether a target EXISTS is not stored in either: see the has-bit below.
  const GOAL_VAR_OVERRIDES = { 25: 3839, 26: 6095, 27: 9410, 28: 11204 };
  const GOAL_VARP_IDS = (function () {
    const a = [1115, 1117]; for (let s = 1; s <= 29; s++) a.push(1117 + s);
    for (const k in GOAL_VAR_OVERRIDES) a.push(GOAL_VAR_OVERRIDES[k]);
    return a.join(',');
  })();
  let gameGoals = {};
  let _gameGoalsAt = 0;
  async function fetchGameGoals() {
    if (!bridge() || !bridge().varps) return;
    const now = Date.now(); if (now - _gameGoalsAt < 1000) return; _gameGoalsAt = now;
    let d = null;
    try { d = JSON.parse(await bridge().varps(myPid(), GOAL_VARP_IDS)); } catch (e) { return; }
    if (!d) return;
    // The dedicated goal varps (181/6095/...) can be getStorage-backed like the appearance varps
    // (172/2017): absent from the flat varp hashmap, but pushed through the companion var
    // observer. Union: the direct hashmap read wins when nonzero, the companion fills misses.
    let dc = null;
    try { if (bridge().varsDump) dc = JSON.parse(await bridge().varsDump(myPid())); } catch (e) {}
    const has = (d['1115'] || 0) >>> 0, typ = (d['1117'] || 0) >>> 0;
    const g = {};
    // Mirrors the game's own pair of accessors:
    //   script4036(stat) = TESTBIT(varp 1115, stat)  -> IS a target set
    //   script4037(stat) = [TESTBIT(varp 1117, stat), valueVarp, partnerVarp]  -> 1 = level goal
    // Both bitmasks are indexed by STAT ID, and they cover the dedicated-varp skills exactly as
    // they cover the classic ones - only the value's location differs. Earlier this code skipped
    // the has-bit for those four and inferred a target purely from "value is nonzero", which
    // reported a target for anyone whose varp still held a cleared goal's value (owner-reported:
    // "In-game target: level 20" on Archaeology with no target set, varp 9410 stale at 20).
    for (let i = 0; i < GOAL_STAT_SKILLID.length; i++) {
      const sk = GOAL_STAT_SKILLID[i];
      if (!((has >>> sk) & 1)) continue;                 // no target set for this skill
      const ov = GOAL_VAR_OVERRIDES[i];
      // Dedicated-varp skills must NOT read the arithmetic slot (1117+skillId): it holds stale,
      // unrelated data for them (Invention's slot 1144 = 77, shadowing the real 6095 = 145).
      const val = ov ? (((d[String(ov)] || 0) >>> 0) || ((dc && (dc['4:' + ov] >>> 0)) || 0))
                     : (d[String(1117 + sk)] | 0);
      if (val <= 0) continue;
      g[i] = { mode: ((typ >>> sk) & 1) ? 'level' : 'xp', target: val, game: true };
    }
    gameGoals = g;
  }
  // The live in-game target for skill `i` (read from the game; not user-editable).
  function effectiveGoal(i) { return gameGoals[i] || null; }
  function xpForLevel(level, elite) {
    const t = elite ? ELITE_XP_TABLE : XP_TABLE;
    if (level < 1) return 0;
    return t[Math.min(level, t.length - 1)] || 0;
  }
  // Progress of skill `i` (current xp) toward its goal: actual XP earned over the
  // XP the target requires. For a level goal the target XP is the XP for that level.
  function skillGoalProgress(i, xp) {
    const g = effectiveGoal(i); if (!g || xp < 0 || g.target <= 0) return null;
    const targetXp = g.mode === 'xp' ? g.target : xpForLevel(g.target, i === 26);
    if (targetXp <= 0) return null;
    return { pct: Math.max(0, Math.min(1, xp / targetXp)), mode: g.mode, target: g.target, done: xp >= targetXp, game: !!g.game };
  }

  // Stopwatch / Notes / Counter panels live in panel_stopwatch.js / panel_notes.js /
  // panel_counter.js, spliced inline ahead of this script at load (Dock.cpp); shared
  // global scope, so their render*() functions are called from renderPane() below.

  // ---- Tick metronome ----
  // Widget + audio click live in the overlay, locked to the real server tick (audio
  // fires only on a confirmed tick -> no doubles). This UI only picks mode/interval.
  const METRO_MODES = [['off', 'Off'], ['visual', 'Visual'], ['audio', 'Audio'], ['both', 'Both']];
  let metroMode = 'off';        // off | visual | audio | both
  let metroInterval = 1;        // beat every N ticks (1..6)
  let metroLocked = false;      // locked = click-through, non-movable widget
  let _metroAppliedPid = -1;
  try {
    const sm = localStorage.getItem('rtxMetroMode'); if (sm) metroMode = sm;
    const si = parseInt(localStorage.getItem('rtxMetroIv'), 10); if (si >= 1 && si <= 6) metroInterval = si;
    metroLocked = localStorage.getItem('rtxMetroLock') === '1';
  } catch (e) {}
  function metroVisual() { return metroMode === 'visual' || metroMode === 'both'; }
  function metroAudio()  { return metroMode === 'audio'  || metroMode === 'both'; }
  // The DIAL is now the 'metronome' HUD window; only the audio click stays native, because
  // it fires off the confirmed tick edge on the reader's 5 ms poll thread and routing that
  // through the page would put the UI pump between the tick and the sound. So the visual
  // flag passed to the companion is permanently false: nothing else drives that drawing.
  function applyMetroOverlay() {
    try { bridge().metronome(myPid(), false, metroAudio(), metroInterval, true); _metroAppliedPid = myPid(); } catch (e) {}
  }
  // Open/close the dial window to match the mode. Opening is idempotent (openTab focuses an
  // existing window), so this is safe to call from any settings change.
  // `adopt` is passed once, right after a layout restore. The layout is per-account but the
  // mode is machine-wide, so without it turning the dial off on one character would close the
  // window another character had saved open -- and write that closure into their layout.
  // A restored window is that account's own answer, so it wins and the mode follows it.
  function metroSyncWindow(adopt) {
    const want = metroVisual();
    const w = wmWinOf('metronome');
    if (adopt && w && !want) {
      metroMode = metroAudio() ? 'both' : 'visual';
      try { localStorage.setItem('rtxMetroMode', metroMode); } catch (e) {}
      applyMetroOverlay();
      paneRun('ticks', renderTicks);
      return;
    }
    if (want && !w) {
      const t = allTabs().find(x => x.id === 'metronome');
      if (t) {
        const nw = openTab(t, { noFocus: metroLocked });
        if (nw && metroLocked) { nw.locked = true; wmApplyLock(nw); wmRectsSoon(); wmSaveSoon(); }
      }
    } else if (!want && w) {
      wmCloseTabId('metronome');
    }
  }
  // Was missing entirely: panel_tasks.js called setMetroLock() and threw a ReferenceError,
  // so the lock button never worked and rtxMetroLock was never written.
  function setMetroLock(v) {
    metroLocked = !!v;
    prefSet('rtxMetroLock', metroLocked ? '1' : '0');   // durable pref, not bare localStorage
    const w = wmWinOf('metronome');
    if (w) { w.locked = metroLocked; wmApplyLock(w); wmRectsSoon(); wmSaveSoon(); }
    paneRun('ticks', renderTicks);
  }
  // ---- XP tracker overlay config; the panel itself is drawn + sampled by the host
  //      overlay, this is only its configuration. ----
  let xpOn = false, xpLock = false, xpTotal = true, xpAuto = true, xpMask = 0, _xpAppliedPid = 0;
  try {
    xpOn    = localStorage.getItem('rtxXpOn') === '1';
    xpLock  = localStorage.getItem('rtxXpLock') === '1';
    xpTotal = localStorage.getItem('rtxXpTotal') !== '0';
    xpAuto  = localStorage.getItem('rtxXpAuto') !== '0';
    const xm = parseInt(localStorage.getItem('rtxXpMask'), 10); if (xm >= 0) xpMask = xm;
  } catch (e) {}
  function saveXpCfg() {
    try {
      localStorage.setItem('rtxXpOn', xpOn ? '1' : '0');
      localStorage.setItem('rtxXpLock', xpLock ? '1' : '0');
      localStorage.setItem('rtxXpTotal', xpTotal ? '1' : '0');
      localStorage.setItem('rtxXpAuto', xpAuto ? '1' : '0');
      localStorage.setItem('rtxXpMask', String(xpMask));
    } catch (e) {}
  }
  // The CARD is now the 'xpmeter' HUD window. The call still goes out with visible=false
  // because it is what configures and keeps alive the native 1 Hz sampler + session
  // baseline, which the meter reads back through xpPanelState; only the drawing moved.
  function applyXpOverlay() {
    try { bridge().xpPanel(myPid(), false, true, xpTotal, xpAuto, xpMask); _xpAppliedPid = myPid(); } catch (e) {}
  }
  function xpSyncWindow(adopt) {          // see metroSyncWindow for what `adopt` is for
    const w = wmWinOf('xpmeter');
    if (adopt && w && !xpOn) {
      xpOn = true;
      saveXpCfg();
      paneRun('xptracker', () => { const wr = $('xpWrap'); if (wr) buildXpConfig(wr); });
      return;
    }
    if (xpOn && !w) {
      const t = allTabs().find(x => x.id === 'xpmeter');
      if (t) {
        const nw = openTab(t, { noFocus: xpLock });
        if (nw && xpLock) { nw.locked = true; wmApplyLock(nw); wmRectsSoon(); wmSaveSoon(); }
      }
    } else if (!xpOn && w) {
      wmCloseTabId('xpmeter');
    }
  }
  function setXpLock(v) {
    xpLock = !!v;
    saveXpCfg();
    const w = wmWinOf('xpmeter');
    if (w) { w.locked = xpLock; wmApplyLock(w); wmRectsSoon(); wmSaveSoon(); }
    paneRun('xptracker', renderXpTracker);
  }
  // Tile-marker keybinds fire (default A/D = movement) only while the Markers panel is the OPEN,
  // non-collapsed tab -- otherwise they'd place tiles during normal play. Push that state to the
  // host, which gates the keys. Called on every tab switch + collapse toggle.
  let _markerArmed = null;
  function syncMarkerArm() {
    const on = paneVisible('markers');
    if (on === _markerArmed) return; _markerArmed = on;
    try { if (bridge() && bridge().markerKeybindsArm) bridge().markerKeybindsArm(myPid(), on); } catch (e) {}
  }
  function setMetroMode(m) { metroMode = m; try { localStorage.setItem('rtxMetroMode', m); } catch (e) {} applyMetroOverlay(); metroSyncWindow(); paneRun('ticks', renderTicks); }
  function setMetroInterval(n) { metroInterval = n; try { localStorage.setItem('rtxMetroIv', String(n)); } catch (e) {} applyMetroOverlay(); paneRun('ticks', renderTicks); }
