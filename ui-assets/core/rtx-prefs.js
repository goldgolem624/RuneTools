  const rtxPrefs = Object.create(null);
  let _prefsReady = false, _prefsSaveT = 0, _prefsInitP = null;
  const PREF_DURABLE = ['rtxSoundMuted', 'rtxSoundVol', 'rtxOverlayCfg', 'rtxSceneView', 'rtxAuras', 'rtxAuraSeen', 'rtxAuraIcons',
                        'rtxUi', 'rtxHideName', 'rtxMetroLock', 'rtxWeNotify', 'rtxDwNotify', 'rtxResDungCoords', 'rtxSceneMarks',
                        'rtxBankOverlay', 'rtxBankSort', 'rtxBankPriceBasis', 'rtxBankTabs', 'rtxVirtual126', 'rtxFarmLayout', 'rtxFarmOrder', 'rtxQgFolded', 'rtxChatMute'];
  // prefs.json is shared by every client window, and each page keeps its own copy. So a page writes only the
  // keys it changed, over what the file holds at that moment, and takes up the keys other windows wrote.
  // Every page runs on the launcher's one UI thread, so no other write can land between that read and write.
  const _prefsPending = new Set();                // keys changed here and not written yet
  const _prefsOwn = Object.create(null);          // key -> the value this page's own state was built from
  let _prefsSeen = null, _prefsSeenText = null;   // the file as this page last read or wrote it
  let _prefsApplying = null;                      // keys a panel is reading back in (prefsApply)
  // What reads each durable pref back into its panel: run once the file is loaded, and again for the keys
  // another window has changed since.
  const PREF_APPLY = [
    [['rtxSoundMuted', 'rtxSoundVol'], () => sndApplyDurablePrefs()],
    [['rtxOverlayCfg'], () => ovApplyDurablePrefs()],
    [['rtxSceneView'], () => sceneApplyDurablePrefs()],
    [['rtxSceneMarks'], () => { if (typeof sceneMarksReload === 'function') sceneMarksReload(); }],
    [['rtxWeNotify'], () => { if (typeof scNotifyReload === 'function') scNotifyReload(); }],
    [['rtxDwNotify'], () => { if (typeof dwNotifyReload === 'function') dwNotifyReload(); }],
    [['rtxAuras', 'rtxAuraSeen'], () => { if (typeof auraApplyDurablePrefs === 'function') auraApplyDurablePrefs(); }],
    [['rtxBankOverlay', 'rtxBankSort', 'rtxBankPriceBasis', 'rtxBankTabs'], () => { if (typeof bankApplyDurablePrefs === 'function') bankApplyDurablePrefs(); }],
    [['rtxUi'], () => { _uiCfg = null; uiApply(); const w = $('uisWrap'); if (w) w.remove(); }],
  ];
  function prefGet(key, def) {
    if (Object.prototype.hasOwnProperty.call(rtxPrefs, key)) return rtxPrefs[key];
    try { const s = localStorage.getItem(key); if (s !== null) return s; } catch (e) {}
    return def === undefined ? null : def;
  }
  function prefSet(key, val) {
    const v = String(val);
    if (_prefsApplying && _prefsApplying.has(key)) {
      // a panel storing what it just read back: the file already has it, so it is not a change
      rtxPrefs[key] = v; _prefsOwn[key] = v;
      try { localStorage.setItem(key, v); } catch (e) {}
      return;
    }
    // Storing the value this page's state already had is no change. Writing it again would put this
    // page's older copy over what another window has written to that key since.
    if (!_prefsPending.has(key) && _prefsOwn[key] === v) return;
    rtxPrefs[key] = v;
    try { localStorage.setItem(key, v); } catch (e) {}   // keep the seed in step
    _prefsPending.add(key);
    if (_prefsSaveT) return;
    _prefsSaveT = setTimeout(() => { _prefsSaveT = 0; prefsWrite(); }, 400);
  }
  // The file as it is now, { text, obj }; null when it cannot be read or is not a prefs object.
  function prefsRead() {
    try {
      const s = bridge().prefsLoad();
      if (typeof s !== 'string') return null;
      if (s === _prefsSeenText && _prefsSeen) return { text: s, obj: _prefsSeen };
      const v = JSON.parse(s || '{}');
      return (v && typeof v === 'object' && !Array.isArray(v)) ? { text: s, obj: v } : null;
    } catch (e) { return null; }
  }
  // Takes into this page the keys another window changed since this page last saw the file; a key this page
  // changed and has not written yet stays, and the write puts it over the other. A key taken counts as this
  // page's own value from then on, so storing any other value, the old one included, is a change. Returns
  // the keys taken.
  function prefsTake(cur) {
    const took = [];
    if (cur.text === _prefsSeenText) return took;
    if (_prefsReady && _prefsSeen) {
      for (const k in cur.obj) {
        const v = cur.obj[k];
        if (_prefsPending.has(k) || v === _prefsSeen[k] || v === rtxPrefs[k]) continue;
        rtxPrefs[k] = v; _prefsOwn[k] = v; took.push(k);
        try { localStorage.setItem(k, v); } catch (e) {}
      }
    }
    _prefsSeen = cur.obj; _prefsSeenText = cur.text;
    return took;
  }
  // Panels rebuild their state from the keys taken; what they store back while doing so is that value.
  function prefsApply(keys) {
    if (!keys.length) return;
    for (const [ks, fn] of PREF_APPLY) {
      const hit = ks.filter(k => keys.indexOf(k) >= 0);
      if (!hit.length) continue;
      _prefsApplying = new Set(hit);
      try { fn(); } catch (e) {}
      _prefsApplying = null;
    }
  }
  // Prefs holding a JSON map with one entry per character. A write keeps the entries another window changed
  // since this page read the map, so two windows changing different characters do not undo each other.
  const PREF_MAPS = new Set(['rtxFarmLayout', 'rtxFarmOrder', 'rtxQgFolded']);
  function prefMergeMap(disk, mine, base) {
    let d, m, b;
    try { d = JSON.parse(disk || '{}'); m = JSON.parse(mine || '{}'); b = JSON.parse(base || '{}'); } catch (e) { return mine; }
    if (!d || typeof d !== 'object' || !m || typeof m !== 'object' || !b || typeof b !== 'object') return mine;
    const same = (x, y) => JSON.stringify(x) === JSON.stringify(y);   // entries may be objects
    for (const k in b) if (!(k in m)) delete d[k];         // removed here
    for (const k in m) if (!same(m[k], b[k])) d[k] = m[k]; // set or changed here
    return JSON.stringify(d);
  }
  function prefsWrite() {
    const b = bridge();
    if (!b || !b.prefsSave) return;
    const cur = b.prefsLoad ? prefsRead() : null;
    const took = cur ? prefsTake(cur) : [];
    if (cur && !_prefsPending.size) { prefsApply(took); return; }
    // A file that cannot be read gets this page's whole copy, which also repairs a damaged one.
    const out = Object.assign(Object.create(null), rtxPrefs, cur ? cur.obj : null);
    for (const k of _prefsPending) {
      if (PREF_MAPS.has(k) && cur && typeof cur.obj[k] === 'string' && cur.obj[k] !== _prefsOwn[k])
        rtxPrefs[k] = prefMergeMap(cur.obj[k], rtxPrefs[k], _prefsOwn[k]);
      out[k] = rtxPrefs[k];
    }
    const text = JSON.stringify(out);
    let ok = false;
    try { ok = !!b.prefsSave(text); } catch (e) {}
    if (ok) {
      for (const k of _prefsPending) _prefsOwn[k] = rtxPrefs[k];
      _prefsPending.clear();
      _prefsSeen = out; _prefsSeenText = text;
    }
    prefsApply(took);
  }
  // Every few seconds: take up what other windows wrote, and try again a write that failed.
  function prefsTick() {
    if (!bridge() || !bridge().prefsLoad) return;
    if (_prefsPending.size) { if (!_prefsSaveT) prefsWrite(); return; }
    const cur = prefsRead();
    if (cur) prefsApply(prefsTake(cur));
  }
  async function prefsInit() {
    if (_prefsReady || !bridge() || !bridge().prefsLoad) return;
    let disk = null, text = null;
    try { const s = await bridge().prefsLoad(); const v = JSON.parse(s || '{}'); if (v && typeof v === 'object') { disk = v; text = s; } } catch (e) {}
    if (!disk) return;
    _prefsReady = true;
    _prefsSeen = disk; _prefsSeenText = text;
    let migrated = 0;
    for (const k of PREF_DURABLE) {
      if (Object.prototype.hasOwnProperty.call(rtxPrefs, k)) { migrated++; continue; }
      if (Object.prototype.hasOwnProperty.call(disk, k)) { rtxPrefs[k] = disk[k]; _prefsOwn[k] = disk[k]; continue; }
      try { const s = localStorage.getItem(k); if (s !== null) { rtxPrefs[k] = s; _prefsPending.add(k); migrated++; } } catch (e) {}
    }
    for (const k in rtxPrefs) { try { localStorage.setItem(k, rtxPrefs[k]); } catch (e) {} }
    if (migrated) prefsWrite();
    for (const [, fn] of PREF_APPLY) { try { fn(); } catch (e) {} }
    setInterval(prefsTick, 2000);
  }

