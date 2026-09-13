  const rtxPrefs = Object.create(null);
  let _prefsReady = false, _prefsSaveT = 0, _prefsInitP = null;
  const PREF_DURABLE = ['rtxSoundMuted', 'rtxSoundVol', 'rtxOverlayCfg', 'rtxSceneView', 'rtxAuras', 'rtxAuraSeen', 'rtxAuraIcons',
                        'rtxUi', 'rtxHideName', 'rtxMetroLock', 'rtxWeNotify', 'rtxDwNotify', 'rtxResDungCoords', 'rtxSceneMarks',
                        'rtxBankOverlay', 'rtxBankSort', 'rtxBankPriceBasis', 'rtxBankTabs'];
  function prefGet(key, def) {
    if (Object.prototype.hasOwnProperty.call(rtxPrefs, key)) return rtxPrefs[key];
    try { const s = localStorage.getItem(key); if (s !== null) return s; } catch (e) {}
    return def === undefined ? null : def;
  }
  function prefSet(key, val) {
    rtxPrefs[key] = String(val);
    try { localStorage.setItem(key, String(val)); } catch (e) {}   // keep the seed in step
    if (_prefsSaveT) return;
    _prefsSaveT = setTimeout(() => {
      _prefsSaveT = 0;
      try { if (bridge() && bridge().prefsSave) bridge().prefsSave(JSON.stringify(rtxPrefs)); } catch (e) {}
    }, 400);
  }
  async function prefsInit() {
    if (_prefsReady || !bridge() || !bridge().prefsLoad) return;
    let disk = null;
    try { const s = await bridge().prefsLoad(); const v = JSON.parse(s || '{}'); if (v && typeof v === 'object') disk = v; } catch (e) {}
    if (!disk) return;
    _prefsReady = true;
    let migrated = 0;
    for (const k of PREF_DURABLE) {
      if (Object.prototype.hasOwnProperty.call(rtxPrefs, k)) { migrated++; continue; }
      if (Object.prototype.hasOwnProperty.call(disk, k)) { rtxPrefs[k] = disk[k]; continue; }
      try { const s = localStorage.getItem(k); if (s !== null) { rtxPrefs[k] = s; migrated++; } } catch (e) {}
    }
    for (const k in rtxPrefs) { try { localStorage.setItem(k, rtxPrefs[k]); } catch (e) {} }
    if (migrated) { try { if (bridge().prefsSave) bridge().prefsSave(JSON.stringify(rtxPrefs)); } catch (e) {} }
    try { sndApplyDurablePrefs(); } catch (e) {}
    try { ovApplyDurablePrefs(); } catch (e) {}
    try { sceneApplyDurablePrefs(); } catch (e) {}
    try { if (typeof auraApplyDurablePrefs === 'function') auraApplyDurablePrefs(); } catch (e) {}
    try { _uiCfg = null; uiApply(); const w = $('uisWrap'); if (w) w.remove(); } catch (e) {}
  }

