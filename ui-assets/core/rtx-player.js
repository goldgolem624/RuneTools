  let infoData = null;
  let infoVb = null;   // extra Player State varbits: 4163 = total penguin points
  let infoCam = null;  // live camera varcs: 1971 zoom, 5114 pitch, 5115 yaw (0..16284 = full circle)
  let infoHover = null;   // engine hover slot (hoverEntity bridge): live id/name/action/tile verification
  let infoHoverHeld = null;   // last REAL hover target, held so the panel stays readable after unhover
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

  //   458  bit30 = "virtual levels" display setting (varbit 19007)
  const PLAYER_VARP_IDS = '458,13537,13538,3274,8040,1787,679,463,1297,4818,5984,5982,5991,12314,12426,13521';
  let playerVp = null;
  const PLAYER_VARBIT_IDS = '39152,58389';
  let playerVb = null;
  async function fetchPlayerVp() {
    if (!bridge() || !bridge().varps) return;
    try { const d = JSON.parse(await bridge().varps(myPid(), PLAYER_VARP_IDS));
          if (d && typeof d === 'object' && Object.keys(d).length) playerVp = d; } catch (e) {}
    if (!bridge().varbits) return;
    try { const v = JSON.parse(await bridge().varbits(myPid(), PLAYER_VARBIT_IDS)); if (v && typeof v === 'object') playerVb = v; } catch (e) {}
  }
  function virtualLevelsOn() { return !!(playerVp && (((playerVp['458'] || 0) >>> 30) & 1)); }

  const GOAL_STAT_SKILLID = [1,5,2,6,3,7,4,16,18,19,15,17,11,14,13,9,8,10,20,21,12,23,22,24,25,26,27,28,29];
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
    let dc = null;
    try { if (bridge().varsDump) dc = JSON.parse(await bridge().varsDump(myPid())); } catch (e) {}
    const has = (d['1115'] || 0) >>> 0, typ = (d['1117'] || 0) >>> 0;
    const g = {};
    for (let i = 0; i < GOAL_STAT_SKILLID.length; i++) {
      const sk = GOAL_STAT_SKILLID[i];
      if (!((has >>> sk) & 1)) continue;                 // no target set for this skill
      const ov = GOAL_VAR_OVERRIDES[i];
      const val = ov ? (((d[String(ov)] || 0) >>> 0) || ((dc && (dc['4:' + ov] >>> 0)) || 0))
                     : (d[String(1117 + sk)] | 0);
      if (val <= 0) continue;
      g[i] = { mode: ((typ >>> sk) & 1) ? 'level' : 'xp', target: val, game: true };
    }
    gameGoals = g;
  }
  function effectiveGoal(i) { return gameGoals[i] || null; }
  function xpForLevel(level, elite) {
    const t = elite ? ELITE_XP_TABLE : XP_TABLE;
    if (level < 1) return 0;
    return t[Math.min(level, t.length - 1)] || 0;
  }
  function skillGoalProgress(i, xp) {
    const g = effectiveGoal(i); if (!g || xp < 0 || g.target <= 0) return null;
    const targetXp = g.mode === 'xp' ? g.target : xpForLevel(g.target, i === 26);
    if (targetXp <= 0) return null;
    return { pct: Math.max(0, Math.min(1, xp / targetXp)), mode: g.mode, target: g.target, done: xp >= targetXp, game: !!g.game };
  }


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
  function applyMetroOverlay() {
    try { bridge().metronome(myPid(), false, metroAudio(), metroInterval, true); _metroAppliedPid = myPid(); } catch (e) {}
  }
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
  function setMetroLock(v) {
    metroLocked = !!v;
    prefSet('rtxMetroLock', metroLocked ? '1' : '0');   // durable pref, not bare localStorage
    const w = wmWinOf('metronome');
    if (w) { w.locked = metroLocked; wmApplyLock(w); wmRectsSoon(); wmSaveSoon(); }
    paneRun('ticks', renderTicks);
  }
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
  let _markerArmed = null;
  function syncMarkerArm() {
    const on = paneVisible('markers');
    if (on === _markerArmed) return; _markerArmed = on;
    try { if (bridge() && bridge().markerKeybindsArm) bridge().markerKeybindsArm(myPid(), on); } catch (e) {}
  }
  function setMetroMode(m) { metroMode = m; try { localStorage.setItem('rtxMetroMode', m); } catch (e) {} applyMetroOverlay(); metroSyncWindow(); paneRun('ticks', renderTicks); }
  function setMetroInterval(n) { metroInterval = n; try { localStorage.setItem('rtxMetroIv', String(n)); } catch (e) {} applyMetroOverlay(); paneRun('ticks', renderTicks); }
