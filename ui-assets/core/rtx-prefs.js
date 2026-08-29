// rtx-prefs.js: Durable UI preferences: rtxPrefs, PREF_DURABLE, prefGet, prefSet, prefsInit (prefs.json through the bridge, localStorage as seed).
// Loads after: rtx-shim.js (localStorage).
// Plain script, page globals by design: every top-level name here is a page global that panels and the other core files use.
  // ---- durable UI preferences ----
  // localStorage above is session-grade at best: the shim makes it in-memory on a broken
  // build, and even when it works it is wiped whenever ui-assets are re-extracted, which is
  // every app update. That is fine for a scroll position and NOT fine for a hand-collected
  // sound mute list. Anything a user would resent re-doing goes here instead, in prefs.json.
  //
  // The localStorage value is still the seed: on first load each key is migrated across, so
  // nobody loses what they already had, and the localStorage copy is kept in step so an older
  // launcher (no prefsLoad) still reads something sane.
  const rtxPrefs = Object.create(null);
  let _prefsReady = false, _prefsSaveT = 0, _prefsInitP = null;
  const PREF_DURABLE = ['rtxSoundMuted', 'rtxSoundVol', 'rtxOverlayCfg', 'rtxSceneView', 'rtxAuras', 'rtxAuraSeen', 'rtxAuraIcons',
                        'rtxUi', 'rtxHideName', 'rtxMetroLock', 'rtxWeNotify', 'rtxDwNotify', 'rtxResDungCoords', 'rtxSceneMarks'];
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
      // A value written THIS session (prefSet before the disk read finished) is newer than the
      // file; keep it and make sure it reaches the disk, instead of clobbering it with the old one.
      if (Object.prototype.hasOwnProperty.call(rtxPrefs, k)) { migrated++; continue; }
      if (Object.prototype.hasOwnProperty.call(disk, k)) { rtxPrefs[k] = disk[k]; continue; }
      // Not on disk yet: adopt whatever localStorage still holds, so an upgrade keeps it.
      try { const s = localStorage.getItem(k); if (s !== null) { rtxPrefs[k] = s; migrated++; } } catch (e) {}
    }
    // Push the migrated values back into localStorage too, so the panels that read it
    // directly at parse time (before this runs) see the durable value on the next start.
    for (const k in rtxPrefs) { try { localStorage.setItem(k, rtxPrefs[k]); } catch (e) {} }
    if (migrated) { try { if (bridge().prefsSave) bridge().prefsSave(JSON.stringify(rtxPrefs)); } catch (e) {} }
    // Anything read at parse time needs a nudge now that the durable value is in hand.
    try { sndApplyDurablePrefs(); } catch (e) {}
    try { ovApplyDurablePrefs(); } catch (e) {}
    try { sceneApplyDurablePrefs(); } catch (e) {}
    try { if (typeof auraApplyDurablePrefs === 'function') auraApplyDurablePrefs(); } catch (e) {}
    try { _uiCfg = null; uiApply(); const w = $('uisWrap'); if (w) w.remove(); } catch (e) {}
  }

