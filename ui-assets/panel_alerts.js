// RuneToolsX panel: Alerts (sound + screen flash on game events).
// Spliced inline into client.html; IIFE (window exports + registerTab; see the RTX registry in client.html).
(function () {

  // Triggers are evaluated every poll so they fire with another tab open. Sounds play through
  // the C++ bridge (Ultralight has no HTML5 audio); the flash is drawn on the game window by
  // the overlay.
  const ALERT_SOUNDS = ['alert 1', 'alert 2', 'alert 3', 'alert 4', 'alert 5', 'alert 6', 'alert 7', 'alert 8', 'alert 9', 'alert 10', 'alert 11', 'alert 12', 'alert 13', 'alert 14', 'alert 15', 'alert 16', 'alert 17', 'alert 18', 'alert 19', 'none'];
  // Matched as a case-insensitive SUBSTRING of the NPC name, and shared by the alert
  // and the on-screen highlight (syncOverlayHighlight sends this same list), so an
  // entry added here lights the event up as well as announcing it.
  const RANDOM_EVENT_NAMES = ['Divine blessing', 'Seren spirit', 'Catalyst of alteration',
                              'Fire spirit', 'Manifested knowledge', 'Guthixian butterfly',
                              // Scripture of Elidinis souls (spawn while skilling with the
                              // book active; ~51 ticks to act before they vanish).
                              'Lost Soul', 'Unstable Soul', 'Vengeful Soul', 'Mimicking Soul'];
  // What to DO with a soul, appended to its alert: each type wants a different reaction,
  // and the wrong one (walking into a Vengeful Soul) forfeits the XP.
  const SOUL_HINTS = {
    'lost soul': 'guide it',
    'unstable soul': 'stabilise it, then guide',
    'vengeful soul': 'keep away until it stops, then guide',
    'mimicking soul': 'corner it, then guide',
  };
  // Per-event opt-out. Stored as an "off" set rather than an "on" set so a config saved
  // before this existed (and any event added later) defaults to ON.
  function randomEventOff() {
    const r = alertCfg && alertCfg.rules && alertCfg.rules.random;
    if (!r) return {};
    if (!r.off || typeof r.off !== 'object') r.off = {};
    return r.off;
  }
  function randomEventOn(name) { return !randomEventOff()[name]; }
  function randomEventSet(name, on) {
    const off = randomEventOff();
    if (on) delete off[name]; else off[name] = 1;
  }
  // The events actually being watched: shared by the alert test and the on-screen
  // highlight, so a chip switched off stops doing both.
  function randomEventNames() { return RANDOM_EVENT_NAMES.filter(randomEventOn); }

  const ALERT_META = [
    { id: 'random',  name: 'Random events',     desc: 'Pick which events alert and highlight on screen' },
    { id: 'levelup', name: 'Level up',          desc: 'A skill level increases' },
    { id: 'target',  name: 'Skill target reached', desc: 'An in-game skill target (level/XP) is hit' },
    { id: 'ge',      name: 'GE offer complete', desc: 'A Grand Exchange offer finishes' },
    { id: 'idle',    name: 'Player idle',       desc: 'You stop moving and stop animating', param: { label: 'After', suffix: 's', def: 5, min: 1, max: 600 }, repeatable: true },
    // Distinct from 'Player idle' above: that one watches your AVATAR standing still, this one
    // watches the server's own logout clock, which runs on INPUT (cursor movement inside the
    // game window, clicks, keys) and does not run at all while you are in combat.
    { id: 'logout',  name: 'Idle logout warning', desc: 'The idle-logout timer is nearly up (the server sends you to the lobby)', param: { label: 'Under', suffix: 's left', def: 10, min: 3, max: 300 }, repeatable: true, repeatHint: 'Keep warning every 5s while the timer is still under the threshold' },
  ];
  // How an alert is delivered: in the game overlay, a native Windows notification, or both.
  const NOTIFY_TYPES = ['ingame', 'windows', 'both'];
  const NOTIFY_LABELS = { ingame: 'In-game', windows: 'Windows', both: 'Both' };
  const ALERT_DEFAULTS = {
    master: true,
    unfocusedOnly: false,   // every alert: only while the game window is NOT in the foreground
    rules: {
      random:  { enabled: true,  sound: 'alert 1', flash: false, notify: 'ingame' },
      levelup: { enabled: false, sound: 'alert 2', flash: true,  notify: 'ingame' },
      target:  { enabled: false, sound: 'alert 2', flash: true,  notify: 'ingame' },
      ge:      { enabled: false, sound: 'alert 3', flash: false, notify: 'ingame' },
      idle:    { enabled: false, sound: 'alert 4', flash: false, val: 5, repeat: false, notify: 'ingame' },
      logout:  { enabled: false, sound: 'alert 9', flash: true,  val: 10, repeat: false, notify: 'both' },
    },
    custom: [],
  };
  const CUSTOM_TYPES = [['name', 'Name'], ['panim', 'Player anim'], ['nanim', 'NPC anim'], ['auglevel', 'Augment lvl'], ['invslots', 'Inv slots'], ['invitem', 'Inv item'], ['buff', 'Buff value'], ['vitals', 'Player stat'], ['farm', 'Farm patch'], ['ground', 'Ground item']];
  // Vitals alert: value compared = the current amount shown on the orb (points / adrenaline %).
  const VITALS = [['hp', 'Life points'], ['prayer', 'Prayer'], ['summon', 'Summoning'], ['adren', 'Adrenaline']];
  // Inventory-slot conditions (count = used slots, free = empty slots, cap = 28).
  const INVSLOT_CONDS = [['empty', 'Empty'], ['full', 'Full'], ['usedge', 'Used ≥'], ['usedle', 'Used ≤'], ['freele', 'Free ≤']];
  // Inventory-item conditions (matched by item name, summing stacks across slots).
  const INVITEM_CONDS = [['contains', 'Contains'], ['ge', 'Count ≥'], ['le', 'Count ≤'], ['eq', 'Count =']];
  // Buff-value comparators (value = the number shown on the buff bar, verbatim).
  const BUFF_CONDS = [['active', 'Active'], ['inactive', 'Not active'], ['lt', 'Less than'], ['le', '≤'], ['eq', 'Equal to'], ['ge', '≥'], ['gt', 'Greater than']];
  const BUFF_PRESENCE = ['active', 'inactive'];
  // Farm-patch alert conditions -> the patch state codes they match.
  const FARM_CONDS = [['ready', 'Ready'], ['disease', 'Diseased'], ['dead', 'Dead'], ['water', 'Needs water'], ['attention', 'Needs attention']];
  const FARM_COND_CODES = { ready: [4, 32], disease: [33], dead: [34], water: [7], attention: [33, 34, 7] };
  const CUSTOM_TYPE_SET = ['name', 'panim', 'nanim', 'auglevel', 'invslots', 'invitem', 'buff', 'vitals', 'farm', 'ground'];
  // Types an extra AND condition may use: everything that is a plain "is this true right now"
  // test. Augment level and farm patch are per-item rising edges, so they only make sense as the
  // main trigger; they can still be gated BY the conditions below.
  const AND_TYPES = CUSTOM_TYPES.filter(t => t[0] !== 'auglevel' && t[0] !== 'farm');
  const AND_TYPE_SET = AND_TYPES.map(t => t[0]);
  const AND_MAX = 6;
  // A condition inside `also`: the same fields as the main trigger, minus delivery.
  function normAnd(c) {
    if (!c || typeof c !== 'object') return null;
    const type = (AND_TYPE_SET.indexOf(c.type) >= 0 ? c.type : 'buff');
    const defCond = type === 'invslots' ? 'empty' : type === 'invitem' ? 'contains' : type === 'buff' ? 'inactive' : type === 'vitals' ? 'lt' : '';
    return {
      type: type,
      kind: (['npc', 'player', 'object'].indexOf(c.kind) >= 0 ? c.kind : 'npc'),
      text: typeof c.text === 'string' ? c.text : '',
      anim: typeof c.anim === 'number' ? c.anim : 0,
      cond: typeof c.cond === 'string' ? c.cond : defCond,
      num:  typeof c.num === 'number' ? c.num : 1,
      stat: (VITALS.some(s => s[0] === c.stat) ? c.stat : 'hp'),
      item: typeof c.item === 'number' ? c.item : 0,
      not:  typeof c.not === 'boolean' ? c.not : false,   // invert: "AND NOT ..."
      op:   c.op === 'or' ? 'or' : 'and',                 // connector to what precedes it
    };
  }
  const ALERT_PRESETS = [
    { name: 'Pickpocket failed',   type: 'panim',    anim: 424, sound: 'alert 5' },
    { name: 'Augment Siphon',      type: 'auglevel', anim: 12,  sound: 'alert 6' },
    { name: 'Augment Disassemble', type: 'auglevel', anim: 9,   sound: 'alert 7' },
    { name: 'Inventory full',      type: 'invslots', cond: 'full',  sound: 'alert 8' },
    { name: 'Inventory empty',     type: 'invslots', cond: 'empty', sound: 'alert 9' },
  ];
  alertCfg = null;
  let alertCustomSeq = 0;
  // How long a random event stays "present" after the last poll that saw it. Long enough to
  // bridge a dropped sighting, short enough that the box clears promptly once it really goes.
  const RANDOM_HOLD_MS = 3000;
  let alertState     = { ready: false, pid: 0, prevSkills: null, prevGeDone: {}, prevRandom: false, randomSeenAt: 0, idleSince: null, idleFired: false, custom: {}, augSeen: {}, farmSeen: {} };
  let alertLog       = [];

  function normCustom(w) {
    if (!w || typeof w !== 'object') return null;
    const type = (CUSTOM_TYPE_SET.indexOf(w.type) >= 0 ? w.type : 'name');
    const defCond = type === 'invslots' ? 'empty' : type === 'invitem' ? 'contains' : type === 'buff' ? 'lt' : type === 'vitals' ? 'lt' : type === 'farm' ? 'ready' : '';
    return {
      id:      w.id || ('c' + Date.now() + '_' + (alertCustomSeq++)),
      type:    type,
      kind:    (['npc', 'player', 'object'].indexOf(w.kind) >= 0 ? w.kind : 'npc'),
      text:    typeof w.text === 'string' ? w.text : '',
      anim:    typeof w.anim === 'number' ? w.anim : (type === 'auglevel' ? 12 : 0),
      augItem: typeof w.augItem === 'number' ? w.augItem : 0,   // auglevel: specific item id (0 = any equipped)
      cond:    typeof w.cond === 'string' ? w.cond : defCond,
      num:     typeof w.num === 'number' ? w.num : 1,
      stat:    (VITALS.some(s => s[0] === w.stat) ? w.stat : 'hp'),
      vb:      typeof w.vb === 'number' ? w.vb : 0,             // farm: watched patch varbit id (0 = any)
      item:    typeof w.item === 'number' ? w.item : 0,         // ground: exact item id from the cache (0 = match by name)
      label:   typeof w.label === 'string' ? w.label : '',
      sound:   (ALERT_SOUNDS.indexOf(w.sound) >= 0 ? w.sound : 'alert 1'),
      flash:   typeof w.flash === 'boolean' ? w.flash : false,
      notify:  (NOTIFY_TYPES.indexOf(w.notify) >= 0 ? w.notify : 'ingame'),
      repeat:  typeof w.repeat === 'boolean' ? w.repeat : false,
      enabled: typeof w.enabled === 'boolean' ? w.enabled : true,
      also:    Array.isArray(w.also) ? w.also.map(normAnd).filter(Boolean).slice(0, AND_MAX) : [],
    };
  }
  function loadAlertCfg() {
    let saved = {};
    try { saved = JSON.parse((bridge() && bridge().alertsLoad && rtxData.sync('host.alertsLoad')) || '{}'); } catch (e) {}
    alertCfg = JSON.parse(JSON.stringify(ALERT_DEFAULTS));
    if (saved && typeof saved === 'object') {
      if (typeof saved.master === 'boolean') alertCfg.master = saved.master;
      if (typeof saved.unfocusedOnly === 'boolean') alertCfg.unfocusedOnly = saved.unfocusedOnly;
      // Older builds had this on the idle rule alone; carry it over as the global switch.
      else if (saved.rules && saved.rules.idle && saved.rules.idle.unfocusedOnly === true) alertCfg.unfocusedOnly = true;
      if (saved.rules) for (const id in alertCfg.rules) {
        const s = saved.rules[id]; if (!s) continue;
        if (typeof s.enabled === 'boolean') alertCfg.rules[id].enabled = s.enabled;
        if (typeof s.sound === 'string' && ALERT_SOUNDS.indexOf(s.sound) >= 0) alertCfg.rules[id].sound = s.sound;
        if (typeof s.flash === 'boolean') alertCfg.rules[id].flash = s.flash;
        if (typeof s.notify === 'string' && NOTIFY_TYPES.indexOf(s.notify) >= 0) alertCfg.rules[id].notify = s.notify;
        if (typeof s.repeat === 'boolean' && 'repeat' in alertCfg.rules[id]) alertCfg.rules[id].repeat = s.repeat;
        if (typeof s.val === 'number' && 'val' in alertCfg.rules[id]) alertCfg.rules[id].val = s.val;
        // Per-event opt-out (Random events: Fire spirit and friends). Stored as an "off" set
        // so an event added in a later build defaults to ON, and so a config written before
        // this existed still loads. Was previously dropped on save, which is why switching a
        // single event off never survived a reload.
        if (s.off && typeof s.off === 'object') {
          const off = {};
          for (const k in s.off) if (s.off[k]) off[k] = 1;
          alertCfg.rules[id].off = off;
        }
      }
      if (Array.isArray(saved.custom)) alertCfg.custom = saved.custom.map(normCustom).filter(w => w && w.id !== 'seed_catalyst');
    }
  }
  let _alertSaveT = 0, _alertSaveTries = 0;
  function saveAlertCfg() {
    if (!alertCfg) return;
    const out = { master: alertCfg.master, unfocusedOnly: !!alertCfg.unfocusedOnly, rules: {}, custom: [] };
    for (const id in alertCfg.rules) {
      const r = alertCfg.rules[id];
      out.rules[id] = { enabled: r.enabled, sound: r.sound, flash: r.flash, notify: r.notify };
      if ('repeat' in r) out.rules[id].repeat = r.repeat;
      if ('val' in r) out.rules[id].val = r.val;
      // Only written when something is actually switched off, so the file stays clean for
      // the default (everything on) and an empty map never masks a later default change.
      if (r.off && typeof r.off === 'object') {
        const off = {};
        for (const k in r.off) if (r.off[k]) off[k] = 1;
        if (Object.keys(off).length) out.rules[id].off = off;
      }
    }
    out.custom = (alertCfg.custom || []).map(w => ({ id: w.id, type: w.type, kind: w.kind, text: w.text, anim: w.anim, augItem: w.augItem, cond: w.cond, num: w.num, stat: w.stat, vb: w.vb, item: w.item, label: w.label, sound: w.sound, flash: w.flash, notify: w.notify, repeat: w.repeat, enabled: w.enabled,
      also: (w.also || []).map(c => ({ type: c.type, kind: c.kind, text: c.text, anim: c.anim, cond: c.cond, num: c.num, stat: c.stat, item: c.item, not: c.not })) }));
    let ok = false;
    try { ok = !!rtxData.sync('act.alertsSave', JSON.stringify(out)); } catch (e) {}
    // The host REFUSES the write (returns false) until it can resolve the account, since the
    // file is per-character. That refusal used to be discarded, so a change made before the
    // character name was readable was silently lost and came back as the default on reload.
    // Retry a bounded number of times instead of dropping it.
    if (ok) { _alertSaveTries = 0; return; }
    if (_alertSaveT || _alertSaveTries >= 10) return;
    _alertSaveTries++;
    _alertSaveT = setTimeout(() => { _alertSaveT = 0; saveAlertCfg(); }, 2000);
  }
  function screenFlash() {
    // Flash the GAME window (drawn by the overlay over the game), not the launcher.
    try { rtxData.sync('overlay.flashGame'); } catch (e) {}
  }
  // Native Windows notification, shown by the launcher (Shell_NotifyIcon -> Win10/11 toast):
  // event = title; body = account, time, world.
  function winNotify(msg) {
    if (!msg) return;
    const acct = (lastSnap && lastSnap.display_name) || 'RuneTools';
    const d = new Date(); const p2 = (n) => String(n).padStart(2, '0');
    const parts = [acct, p2(d.getHours()) + ':' + p2(d.getMinutes()) + ':' + p2(d.getSeconds())];
    if (lastSnap && lastSnap.world) parts.push('World ' + lastSnap.world);
    try { bridge().notifyWindows(msg, parts.join('  -  ')); } catch (e) {}
  }
  // Route one alert by its delivery type. cfg = { sound, flash, notify }. The sound always plays
  // (set it to 'none' to mute); sticky = the in-game card stays until clicked.
  // Is the game (or its host frame) the foreground window? Cheap Win32 query on the host; an
  // older host without the function reports "not focused" so alerts keep working.
  function gameHasFocus() {
    try { return !!(bridge() && bridge().gameFocused && rtxData.sync('state.gameFocused')); } catch (e) { return false; }
  }
  // Global gate: when "only when tabbed out" is on, nothing alerts while you are looking at the
  // game. Checked at delivery time so it covers every rule, custom alert and aura action alike.
  function alertsSuppressed() { return !!(alertCfg && alertCfg.unfocusedOnly && gameHasFocus()); }
  function deliverAlert(cfg, msg, sticky) {
    if (alertsSuppressed()) return;
    const nt = cfg.notify || 'ingame';
    if (cfg.sound && cfg.sound !== 'none') { try { bridge().playSound(cfg.sound); } catch (e) {} }
    if (nt === 'ingame' || nt === 'both') {
      if (cfg.flash) screenFlash();
      // Rendered by the in-game UI layer (uiNotify), not the companion's GL cards.
      if (msg) { try { uiNotify(msg, { sticky: sticky, ttl: sticky ? 0 : 5000 }); } catch (e) {} }
    }
    if (nt === 'windows' || nt === 'both') winNotify(msg);
  }
  function logAlert(now, msg) {
    alertLog.unshift({ t: now, msg: msg }); if (alertLog.length > 5) alertLog.pop();
    paneRun('alerts', renderAlertLog);
  }
  function fireAlert(id, msg, force) {
    const r = alertCfg.rules[id]; if (!r) return;
    const now = Date.now();
    if (!force && r._last && now - r._last < 1500) return;  // per-rule debounce (repeats pass force)
    r._last = now;
    deliverAlert(r, msg, false);
    logAlert(now, msg);
  }
  function invSlotsLabel(w) {
    switch (w.cond) {
      case 'full':   return 'Inventory full';
      case 'usedge': return 'Inventory has ≥ ' + (w.num || 0) + ' items';
      case 'usedle': return 'Inventory has ≤ ' + (w.num || 0) + ' items';
      case 'freele': return 'Inventory ≤ ' + (w.num || 0) + ' free slot' + ((w.num || 0) === 1 ? '' : 's');
      case 'empty':
      default:       return 'Inventory empty';
    }
  }
  function invItemLabel(w) {
    const nm = w.text ? '"' + w.text + '"' : 'item';
    switch (w.cond) {
      case 'ge': return 'Inventory has ≥ ' + (w.num || 0) + '× ' + nm;
      case 'le': return 'Inventory has ≤ ' + (w.num || 0) + '× ' + nm;
      case 'eq': return 'Inventory has ' + (w.num || 0) + '× ' + nm;
      case 'contains':
      default:   return 'Inventory contains ' + nm;
    }
  }
  function buffCondText(cond) { const t = BUFF_CONDS.find(c => c[0] === cond); return t ? t[1].toLowerCase() : '<'; }
  function buffLabel(w) {
    const nm = w.text ? '"' + w.text + '"' : 'buff';
    if (w.cond === 'active')   return nm + ' active';
    if (w.cond === 'inactive') return nm + ' not active';
    return nm + ' ' + buffCondText(w.cond) + ' ' + (w.num || 0);
  }
  function vitalsName(stat) { const t = VITALS.find(s => s[0] === stat); return t ? t[1] : 'Life points'; }
  function vitalsLabel(w) { return vitalsName(w.stat) + ' ' + buffCondText(w.cond) + ' ' + (w.num || 0); }
  // null = NO READING, which is not the same as zero, and vitalsMatch drops null so an unread
  // vital cannot fire an alert.
  //
  // HITPOINTS MOVED. Varp 659 used to pack cur/max as (v & 0xffff)/2 and (v>>>16)&0xffff; it now
  // reads 0 forever, and no clientscript in the cache references it any more. Live lifepoints are
  // varp 13537, holding the CURRENT value directly (user-observed ticking 1434 -> 1462 with regen;
  // no scaling, and nothing packed in the high bits). Reading the dead varp is what produced
  // "Hitpoints less than 550" on repeat at full health: the 0 was the reading, not the player.
  // Prayer and summoning still pack their max in the high bits, so a zero there is still no
  // reading. Adrenaline has no packed max and 0% is normal, so only a missing key counts.
  function vitalValue(stat) {
    if (!playerVp) return null;
    const u = k => (playerVp[k] || 0) >>> 0;
    if (stat === 'hp')     return playerVp['13537'] === undefined ? null : u('13537');
    if (stat === 'prayer') { const v = u('3274'); return ((v >>> 16) & 0x7f) ? Math.floor((v & 0x7fff) / 10) : null; }
    if (stat === 'summon') { const v = u('8040'); return ((v >>> 16) & 0x7f) ? Math.floor((v & 0x7fff) / 10) : null; }
    if (stat === 'adren')  return playerVp['679'] === undefined ? null : Math.floor(u('679') / 10);
    return null;
  }
  function cmpNum(v, cond, n) {
    switch (cond) { case 'le': return v <= n; case 'eq': return v === n; case 'ge': return v >= n; case 'gt': return v > n; case 'lt': default: return v < n; }
  }
  function vitalsMatch(w) { const v = vitalValue(w.stat); return v != null && cmpNum(v, w.cond, w.num || 0); }
  function customLabel(w) {
    if (w.label) return w.label;
    const base = customLabelBase(w);
    if (Array.isArray(w.also) && w.also.length)
      return base + w.also.map(c => (c.op === 'or' ? ' OR ' : ' AND ') + andLabel(c)).join('');
    return base;
  }
  function customLabelBase(w) {
    if (w.type === 'panim') return 'Player anim ' + w.anim;
    if (w.type === 'nanim') return 'NPC anim ' + w.anim;
    if (w.type === 'auglevel') return 'Equipped augment reaches lvl ' + w.anim;
    if (w.type === 'invslots') return invSlotsLabel(w);
    if (w.type === 'invitem') return invItemLabel(w);
    if (w.type === 'buff') return buffLabel(w);
    if (w.type === 'vitals') return vitalsLabel(w);
    if (w.type === 'farm') return (w.vb ? farmPatchLabel(w.vb) : 'Any patch') + ' - ' + farmCondLabel(w.cond);
    if (w.type === 'ground') return 'Ground item "' + (w.text || ('#' + w.item)) + '" dropped';
    return (w.kind.charAt(0).toUpperCase() + w.kind.slice(1)) + ' "' + w.text + '"';
  }
  function invSlotsMatch(w) {
    if (!invPresent()) return false;
    const used = invUsed(), cap = invCapacity(), free = cap - used, n = w.num || 0;
    switch (w.cond) {
      case 'full':   return used >= cap;
      case 'usedge': return used >= n;
      case 'usedle': return used <= n;
      case 'freele': return free <= n;
      case 'empty':
      default:       return used === 0;
    }
  }
  function invItemMatch(w) {
    if (!invPresent() || !w.text) return false;
    const t = w.text.toLowerCase();
    let qty = 0, slots = 0;
    for (const it of invItems()) {
      if ((it[3] || '').toLowerCase().indexOf(t) >= 0) { qty += (it[2] > 0 ? it[2] : 1); slots++; }
    }
    const n = w.num || 0;
    switch (w.cond) {
      case 'ge': return qty >= n;
      case 'le': return qty <= n;          // includes 0 - fires when you run out
      case 'eq': return qty === n;
      case 'contains':
      default:   return slots > 0;
    }
  }
  // Comparable value of a buff = the NUMBER shown on its bar, verbatim. The buff bar does not
  // reliably expose seconds vs stacks vs percent (its "kind" field misclassifies), so no unit is
  // ever inferred: "porter < 5" means < 5 of whatever the bar shows.
  function buffValue(b) {
    // Counts use uppercase K/M multipliers (1.5K = 1500); a lowercase unit is a timer
    // (40m = 40 minutes) and is left as shown.
    const m = String(b.timer || '').trim().match(/(-?\d+(?:\.\d+)?)\s*([KkMm])?/);
    if (!m) return null;
    let v = parseFloat(m[1]);
    if (m[2] === 'K' || m[2] === 'k') v *= 1000;
    else if (m[2] === 'M')           v *= 1000000;
    return Math.round(v);
  }
  function activeBuffs() {
    return (buffsData && Array.isArray(buffsData.buffs)) ? buffsData.buffs : [];
  }
  // Returns false when the buff is not active or has no readable value, so "less than" only
  // fires while the buff is up.
  function buffMatch(w) {
    if (!w.text) return false;
    const t = w.text.toLowerCase();
    const b = activeBuffs().find(x => (x.name || '').toLowerCase().indexOf(t) >= 0);
    if (w.cond === 'active')   return !!b;
    if (w.cond === 'inactive') return !b;
    if (!b) return false;
    const v = buffValue(b);
    if (v == null) return false;
    const n = w.num || 0;
    switch (w.cond) {
      case 'le': return v <= n;
      case 'eq': return v === n;
      case 'ge': return v >= n;
      case 'gt': return v > n;
      case 'lt':
      default:   return v < n;
    }
  }
  // Instantaneous truth of one condition (the main trigger or an entry of `also`), from the
  // data already polled this tick. `within` = the scene-range test around the player.
  function condPresent(c, within) {
    if (c.type === 'invslots') return invSlotsMatch(c);
    if (c.type === 'invitem')  return invItemMatch(c);
    if (c.type === 'buff')     return buffMatch(c);
    if (c.type === 'vitals')   return vitalsMatch(c);
    if (c.type === 'ground')   return groundMatch(c, within);
    if (c.type === 'panim')    return !!(infoData && infoData.in && infoData.anim === c.anim);
    if (c.type === 'nanim')    return !!(sceneData && Array.isArray(sceneData.npcs) &&
                                          sceneData.npcs.some(n => n.anim === c.anim && within(n.x, n.y)));
    if (c.type === 'name' || !c.type) {
      if (!c.text) return false;
      const list = c.kind === 'player' ? (sceneData && sceneData.players)
                 : c.kind === 'object' ? (sceneData && sceneData.objects)
                 : (sceneData && sceneData.npcs);
      if (!Array.isArray(list)) return false;
      const t = c.text.toLowerCase();
      return list.some(e => !e.self && (e.name || '').toLowerCase().indexOf(t) >= 0
                            && (c.kind === 'object' || within(e.x, e.y)));
    }
    return false;
  }
  // Extra conditions with AND/OR connectors: the list splits into OR-groups, AND binding
  // tighter, so trigger AND c1 OR c2 AND c3 reads (trigger AND c1) OR (c2 AND c3).
  // gate = the ANDs riding with the main trigger all hold; alt = some OR-group fully
  // holds ON ITS OWN, which fires the alert without the main trigger (that is what OR
  // means). `not` inverts one entry; an empty list gates nothing.
  function alsoGroups(w, within) {
    const list = Array.isArray(w.also) ? w.also : [];
    const ok = c => { const p = condPresent(c, within); return c.not ? !p : p; };
    let gate = true, alt = false, cur = null;   // cur = the OR-group being accumulated
    for (const c of list) {
      if (c.op === 'or') { if (cur !== null) alt = alt || cur; cur = ok(c); }
      else if (cur === null) gate = gate && ok(c);
      else cur = cur && ok(c);
    }
    if (cur !== null) alt = alt || cur;
    return { gate: gate, alt: alt };
  }
  // Rising-edge triggers (augment level, farm patch) use this as a pass/fail gate: the
  // edge still has to happen, and then either its AND group or any OR group approves.
  function alsoOk(w, within) { const r = alsoGroups(w, within); return r.gate || r.alt; }
  function andLabel(c) {
    let l;
    if (c.type === 'panim') l = 'player anim ' + c.anim;
    else if (c.type === 'nanim') l = 'NPC anim ' + c.anim;
    else if (c.type === 'invslots') l = invSlotsLabel(c);
    else if (c.type === 'invitem') l = invItemLabel(c);
    else if (c.type === 'buff') l = buffLabel(c);
    else if (c.type === 'vitals') l = vitalsLabel(c);
    else if (c.type === 'ground') l = 'ground item "' + (c.text || ('#' + c.item)) + '"';
    else l = (c.kind || 'npc') + ' "' + (c.text || '') + '"';
    return (c.not ? 'not ' : '') + l;
  }
  function fireCustom(w, force) {
    const now = Date.now();
    if (!force && w._last && now - w._last < 1500) return;
    w._last = now;
    deliverAlert(w, customLabel(w), false);
    logAlert(now, customLabel(w));
  }
  function fireFarm(w, msg) {
    deliverAlert(w, msg, false);
    logAlert(Date.now(), msg);
  }
  // Augment-level alert: per-item, equipped only, with a PERSISTENT in-game notification that
  // stays until clicked.
  function fireAugment(w, it) {
    const now = Date.now();
    const msg = 'Augmented item level ' + (it.level || 0) + ' reached on ' + (it.name || ('item ' + it.id));
    deliverAlert(w, msg, true);
    logAlert(now, msg);
  }
  // Card over the GAME window (auto-dismiss ~5s); repeats are deduped by text and
  // shown with a repeat count. Rendered by the in-game UI layer.
  function fireToast(msg) {
    if (!msg) return;
    try { uiNotify(msg, { ttl: 5000 }); } catch (e) {}
  }
  // Dropped items on the ground (companion publish), polled while a Ground item alert exists.
  // Ids are resolved to cache names once each and remembered for the session.
  let groundData = null, _groundFetching = false, _groundAt = 0;
  const groundNames = {};
  async function fetchGround() {
    if (!bridge() || !bridge().groundItems || _groundFetching) return;
    const now = Date.now(); if (now - _groundAt < 600) return; _groundAt = now;
    _groundFetching = true;
    try { const g = JSON.parse((await rtxData.raw('state.groundItems')) || '[]'); groundData = Array.isArray(g) ? g : []; } catch (e) {}
    try {
      if (bridge().itemInfo && groundData) for (const g of groundData) {
        if (!g || g.id == null || groundNames[g.id] !== undefined) continue;
        if (Object.keys(groundNames).length >= 2000) for (const k in groundNames) delete groundNames[k];   // bounded name cache
        groundNames[g.id] = '';
        try { const info = JSON.parse(await rtxData.raw('cache.itemInfo', g.id)); groundNames[g.id] = (info && info.name) || ''; } catch (e) {}
      }
    } catch (e) {}
    _groundFetching = false;
  }
  function groundMatch(c, within) {
    if (!groundData) return false;
    const q = String(c.text || '').toLowerCase().trim();
    if (!c.item && !q) return false;
    return groundData.some(g => {
      if (!g) return false;
      if (c.item) { if (g.id !== c.item) return false; }
      else if ((groundNames[g.id] || '').toLowerCase().indexOf(q) < 0) return false;
      return within(g.x, g.y);
    });
  }
  function customNeedsGround(w) { return w.enabled && anyCond(w, c => c.type === 'ground'); }
  function alertsNeedGround() {
    return !!(alertCfg && alertCfg.master && Array.isArray(alertCfg.custom) && alertCfg.custom.some(customNeedsGround));
  }
  // Each feed is needed if the MAIN trigger or any extra AND condition reads it.
  function anyCond(w, f) { return f(w) || (Array.isArray(w.also) && w.also.some(f)); }
  function customNeedsScene(w) { return w.enabled && anyCond(w, c => c.type === 'nanim' || c.type === 'ground' || (c.type === 'name' && !!c.text)); }
  function customNeedsInfo(w)  { return w.enabled && anyCond(w, c => c.type === 'panim'); }
  function customNeedsPerks(w) { return w.enabled && w.type === 'auglevel'; }
  function customNeedsInv(w)   { return w.enabled && anyCond(w, c => c.type === 'invslots' || c.type === 'invitem'); }
  function customNeedsBuffs(w)  { return w.enabled && anyCond(w, c => c.type === 'buff'); }
  function customNeedsVitals(w)  { return w.enabled && anyCond(w, c => c.type === 'vitals'); }
  function customNeedsFarming(w) { return w.enabled && w.type === 'farm'; }
  function alertsNeedVitals() {
    return !!(alertCfg && alertCfg.master && Array.isArray(alertCfg.custom) && alertCfg.custom.some(customNeedsVitals));
  }
  function alertsNeedGoals() {
    return !!(alertCfg && alertCfg.master && alertCfg.rules.target && alertCfg.rules.target.enabled);
  }
  function alertsNeedFarming() {
    return !!(alertCfg && alertCfg.master && Array.isArray(alertCfg.custom) && alertCfg.custom.some(customNeedsFarming));
  }
  function alertsNeedPerks() {
    return !!(alertCfg && alertCfg.master && Array.isArray(alertCfg.custom) && alertCfg.custom.some(customNeedsPerks));
  }
  function alertsNeedInv() {
    return !!(alertCfg && alertCfg.master && Array.isArray(alertCfg.custom) && alertCfg.custom.some(customNeedsInv));
  }
  function alertsNeedBuffs() {
    return !!(alertCfg && alertCfg.master && Array.isArray(alertCfg.custom) && alertCfg.custom.some(customNeedsBuffs));
  }
  function alertsNeedScene() {
    if (!alertCfg || !alertCfg.master) return false;
    if (alertCfg.rules.random && alertCfg.rules.random.enabled) return true;
    return Array.isArray(alertCfg.custom) && alertCfg.custom.some(customNeedsScene);
  }
  function alertsNeedInfo() {
    if (!alertCfg || !alertCfg.master) return false;
    if (alertCfg.rules.idle && alertCfg.rules.idle.enabled) return true;
    // The logout warning reads infoMember.idleMs, which fetchInfo() populates -- so it must
    // keep the poll alive with the Player State window closed, which is the whole point.
    if (alertCfg.rules.logout && alertCfg.rules.logout.enabled) return true;
    return Array.isArray(alertCfg.custom) && alertCfg.custom.some(customNeedsInfo);
  }
  // Claim the overlay highlight only while THIS client has a random event in range. The overlay
  // is a singleton: pushing names unconditionally makes its target pid "whoever polled last" and
  // the ring lands on the wrong client.
  let _hiLast = null;
  clueHighlightNpc = '';   // talk-to clue: target NPC name to outline in-world; '' = none
  function syncOverlayHighlight() {
    const pid = myPid();
    if (!pid) return;               // __rtx_pid not set yet: retry later, do NOT latch
    const on = !!(alertCfg && alertCfg.master && alertCfg.rules.random
                  && alertCfg.rules.random.enabled && alertState.prevRandom);
    // Shared by-name channel: the host outlines every scene NPC matching a listed name.
    const parts = [];
    if (on) { const en = randomEventNames(); if (en.length) parts.push(en.join(',')); }
    if (clueHighlightNpc) parts.push(clueHighlightNpc);
    const s = pid + '|' + parts.join(',');
    if (s === _hiLast) return;
    _hiLast = s;
    try { bridge().overlayHighlight(pid, parts.join(',')); } catch (e) {}
  }
  // Re-assert after someone else cleared the shared channel (e.g. a plugin's paneLeave wipe).
  function overlayHighlightResync() { _hiLast = null; try { syncOverlayHighlight(); } catch (e) {} }
  function evalAlerts() {
    if (!alertCfg) loadAlertCfg();
    if (!alertCfg || !alertCfg.master) { alertState.ready = false; return; }
    const pid = myPid();
    if (pid !== alertState.pid)
      alertState = { ready: false, pid: pid, prevSkills: null, prevGeDone: {}, prevRandom: false, randomSeenAt: 0, idleSince: null, idleFired: false, logoutFired: false, custom: {}, augSeen: {}, farmSeen: {}, goalTgt: {} };
    if (!alertState.custom) alertState.custom = {};
    if (!alertState.augSeen) alertState.augSeen = {};
    if (!alertState.farmSeen) alertState.farmSeen = {};
    if (!alertState.goalTgt) alertState.goalTgt = {};
    const en = id => alertCfg.rules[id] && alertCfg.rules[id].enabled;
    const snap = lastSnap;
    // Only evaluate when actually In-game (status 30): world>0 is also true during Logging in /
    // Lobby / Changing worlds, which would burst-fire stale alerts.
    const inw = !!(snap && snap.status === 30);
    if (!inw) {   // baseline so re-entry doesn't burst-fire
      alertState.prevSkills = null; alertState.prevGeDone = {}; alertState.prevRandom = false;
      alertState.randomSeenAt = 0;
      alertState.idleSince = null; alertState.idleFired = false; alertState.logoutFired = false; alertState.custom = {}; alertState.augSeen = {}; alertState.farmSeen = {}; alertState.goalTgt = {};
      alertState.ready = !!snap; return;
    }
    const sk = snap.skills;
    if (en('levelup') && alertState.ready && alertState.prevSkills && Array.isArray(sk)) {
      for (let i = 0; i < sk.length && i < alertState.prevSkills.length; i++) {
        if (sk[i] && alertState.prevSkills[i] && sk[i][0] > alertState.prevSkills[i][0]) {
          fireAlert('levelup', (SKILL_NAMES[i] || 'Skill') + ' level ' + sk[i][0]); break;
        }
      }
    }
    // Skill target reached: rising edge via prevSkills (still last poll's xp here), plus a
    // remembered target so it still fires when the game clears the target on hit.
    if (en('target') && Array.isArray(sk)) {
      if (!alertState.goalTgt) alertState.goalTgt = {};
      for (let i = 0; i < sk.length; i++) {
        const g = gameGoals[i];
        if (g && g.target > 0) {
          const txp = g.mode === 'xp' ? g.target : xpForLevel(g.target, i === 26);
          alertState.goalTgt[i] = { xp: txp, label: g.mode === 'xp' ? (g.target.toLocaleString() + ' XP') : ('level ' + g.target) };
        }
        const tgt = alertState.goalTgt[i];
        const curXp  = (sk[i] && sk[i][2] !== undefined) ? sk[i][2] : -1;
        const prevXp = (alertState.prevSkills && alertState.prevSkills[i]) ? alertState.prevSkills[i][2] : -1;
        if (alertState.ready && tgt && tgt.xp > 0 && prevXp >= 0 && prevXp < tgt.xp && curXp >= tgt.xp) {
          fireAlert('target', (SKILL_NAMES[i] || 'Skill') + ' target reached (' + tgt.label + ')');
          delete alertState.goalTgt[i];   // one-shot until a new target is set
        }
      }
    }
    alertState.prevSkills = Array.isArray(sk) ? sk.map(s => s.slice()) : null;
    if (Array.isArray(snap.ge_slots)) {
      for (const g of snap.ge_slots) {
        const done = g.quantity > 0 && g.filled >= g.quantity;
        const was  = !!alertState.prevGeDone[g.slot];
        if (en('ge') && alertState.ready && done && !was) geOfferAlert(g);
        alertState.prevGeDone[g.slot] = done;
      }
    }
    if (en('idle')) {
      const ir = alertCfg.rules.idle, secs = ir.val || 5, thr = secs * 1000, info = infoData;
      const isIdle = !!(info && info.in && info.moving === false && info.anim === -1);
      // "Only while the game is in the background": you are idle on purpose when the game is
      // the window you are looking at, so the alert is for the tab-out case only. Polled here
      // (cheap Win32 on the host) rather than cached, so returning to the game silences the
      // repeat on the very next tick. An older host without the function behaves as before.
      // The tabbed-out gate is global now (alertsSuppressed, applied at delivery); here the
      // idle clock keeps running while focused so tabbing out after standing still alerts at once.
      const focusOk = !alertsSuppressed();
      if (isIdle && focusOk) {
        const tnow = Date.now();
        if (alertState.idleSince == null) alertState.idleSince = tnow;
        if (alertState.ready && tnow - alertState.idleSince >= thr) {
          if (!alertState.idleFired) {
            fireAlert('idle', 'Idle ' + secs + 's'); alertState.idleFired = true; alertState.idleLast = tnow;
          } else if (ir.repeat && tnow - (alertState.idleLast || 0) >= thr) {
            fireAlert('idle', 'Still idle', true); alertState.idleLast = tnow;
          }
        }
      } else if (!isIdle) { alertState.idleSince = null; alertState.idleFired = false; }
      // isIdle but the game has focus: keep idleSince running so that tabbing out after already
      // standing still for the threshold alerts promptly, but never fire while focused.
    } else { alertState.idleSince = null; alertState.idleFired = false; }
    // Idle LOGOUT warning. Unlike the rule above (which watches the avatar), this reads the
    // server's own clock: idleMs is how long since the client last REPORTED input, which is
    // what the logout timer runs on. Rearms itself the moment input resets the clock.
    if (en('logout')) {
      const lr = alertCfg.rules.logout, warn = lr.val || 10;
      const m = infoMember;
      const live = !!(m && m.resolved && typeof m.idleMs === 'number' && m.idleMs >= 0 &&
                      typeof m.idleLogoutSeconds === 'number' && m.idleLogoutSeconds > 0);
      const left = live ? (m.idleLogoutSeconds - Math.floor(m.idleMs / 1000)) : -1;
      if (live && left > 0 && left <= warn) {
        if (!alertState.logoutFired) {
          fireAlert('logout', 'Logout in ' + left + 's');
          alertState.logoutFired = true; alertState.logoutLast = tnow;
        } else if (lr.repeat && tnow - (alertState.logoutLast || 0) >= 5000) {
          fireAlert('logout', 'Logout in ' + left + 's', true); alertState.logoutLast = tnow;
        }
      } else if (!live || left > warn) {
        alertState.logoutFired = false;   // input reset the clock (or it went unreadable)
      }
    } else { alertState.logoutFired = false; }
    const [px, py] = sceneSelfPos();
    const within = (x, y) => px == null ? true : Math.max(Math.abs(x - px), Math.abs(y - py)) <= sceneRange;
    if (sceneData && Array.isArray(sceneData.npcs)) {
      const lc = randomEventNames().map(s => s.toLowerCase());
      const hit = sceneData.npcs.find(n => within(n.x, n.y)
                    && lc.some(nm => (n.name || '').toLowerCase().indexOf(nm) >= 0));
      // Hold the "present" state briefly after the last sighting instead of recomputing it
      // from scratch every poll. A single poll that fails to match -- the scene list churning,
      // a morph NPC whose name resolves empty for a frame, or the event sitting right on the
      // range edge -- otherwise did two visible things: it dropped the name from the overlay
      // channel and put it straight back (the highlight box blinking), and it re-armed the
      // rising edge below so the SAME event could alert again and again.
      const nowR = Date.now();
      if (hit) alertState.randomSeenAt = nowR;
      const present = !!hit ||
        (alertState.randomSeenAt && (nowR - alertState.randomSeenAt) < RANDOM_HOLD_MS);
      if (en('random') && alertState.ready && hit && !alertState.prevRandom) {
        const hint = SOUL_HINTS[(hit.name || '').toLowerCase()];
        fireAlert('random', (hit.name || 'Random event') + ' nearby' + (hint ? ' - ' + hint : ''));
      }
      alertState.prevRandom = !!present;
    }
    if (Array.isArray(alertCfg.custom)) {
      for (const w of alertCfg.custom) {
        if (w.type === 'auglevel') {
          const seen = alertState.augSeen[w.id] || (alertState.augSeen[w.id] = {});
          const thr  = (typeof w.anim === 'number' && w.anim > 0) ? w.anim : 12;
          const eq   = (w.enabled && perksData && Array.isArray(perksData.items))
                     ? perksData.items.filter(it => it.container === 94 && (!w.augItem || it.id === w.augItem)) : [];
          const live = {};
          for (const it of eq) {
            const ik = it.slot + ':' + it.id;
            live[ik] = true;
            const reached = (it.level || 0) >= thr;
            if (!(ik in seen)) { seen[ik] = reached; continue; }   // baseline first sight (no fire)
            if (alertState.ready && reached && !seen[ik] && alsoOk(w, within)) fireAugment(w, it);
            seen[ik] = reached;
          }
          for (const k in seen) if (!live[k]) delete seen[k];       // unequipped -> re-arm
          continue;
        }
        // Farm patch: per-patch rising edge. A patch already matching when the alert is armed is
        // baselined (no fire); "Any" watches every patch.
        if (w.type === 'farm') {
          if (!w.enabled || !farmData) continue;   // don't baseline until varps are in
          const seen = alertState.farmSeen[w.id] || (alertState.farmSeen[w.id] = {});
          const watch = w.vb ? [w.vb] : FARM_ALL_VB;
          for (const vbid of watch) {
            const hit = farmStateMatch(vbid, w.cond);
            if (!(vbid in seen)) { seen[vbid] = hit; continue; }
            if (alertState.ready && hit && !seen[vbid] && alsoOk(w, within)) fireFarm(w, farmPatchLabel(vbid) + ' - ' + farmCondLabel(w.cond));
            seen[vbid] = hit;
          }
          continue;
        }
        // Main trigger AND its conditions, or any OR-group on its own. All sampled from
        // the same poll, so "boss is mid magic attack AND no magic prayer is up" is a
        // single consistent read.
        const rg = alsoGroups(w, within);
        const present = !!w.enabled && ((condPresent(w, within) && rg.gate) || rg.alt);
        const was = !!alertState.custom[w.id];
        if (alertState.ready && present) {
          if (!was) { fireCustom(w); w._rlast = Date.now(); }
          else if (w.repeat && Date.now() - (w._rlast || 0) >= 3000) {
            fireCustom(w, true); w._rlast = Date.now();
          }
        }
        alertState.custom[w.id] = present;
      }
    }
    alertState.ready = true;
  }
  function alertPill(on) { const p = document.createElement('div'); p.className = 'al-pill' + (on ? ' on' : ''); p.appendChild(document.createElement('span')); return p; }

  // ---- sound select popup (native <select> renders poorly in Ultralight) ----
  function closeSoundMenu() { const m = document.getElementById('sndMenu'); if (m) { m.remove(); try { wmRectsSoon(); } catch (e) {} } }
  function placeMenu(pop, anchor) {
    const r = anchor.getBoundingClientRect();
    const W = window.innerWidth, H = window.innerHeight;
    const SB = 20;                                  // gutter so the scrollbar never overlaps the menu
    const below = H - r.bottom - 8, above = r.top - 8;
    const openUp = above > below;
    const avail = Math.max(80, openUp ? above : below);
    if (pop.offsetHeight > avail) pop.style.maxHeight = avail + 'px';
    const pw = pop.offsetWidth, ph = Math.min(pop.offsetHeight, avail);
    let left = r.left;
    if (left + pw > W - SB) left = r.right - pw;
    left = Math.max(8, Math.min(left, W - pw - SB));
    // The anchor's window paints its content scrollbar ABOVE a fixed popup in this renderer, so
    // a menu whose left edge lands in that lane gets a bar drawn over its text. If the menu
    // would straddle the window's right-edge scrollbar lane, shove it fully past the lane
    // (or fully inside the window when there is no room to the right).
    try {
      const winEl = anchor.closest ? anchor.closest('.win') : null;
      if (winEl) {
        const wr = winEl.getBoundingClientRect().right;
        const LANE = 14;
        // Any intersection of [left, left+pw] with the lane [wr-LANE, wr] counts, including a
        // menu that starts inside the window and runs out across the edge.
        if (left < wr && left + pw > wr - LANE) {
          if (wr + 2 + pw <= W - SB) left = wr + 2;                  // fully past the lane
          else left = Math.max(8, wr - LANE - 6 - pw);               // fully inside the window
        }
      }
    } catch (e) {}
    let top = openUp ? (r.top - ph - 4) : (r.bottom + 4);
    top = Math.max(8, Math.min(top, H - ph - 8));
    pop.style.left = Math.max(8, left) + 'px';
    pop.style.top  = top + 'px';
  }
  function openSoundMenu(anchor, current, onPick) {
    closeSoundMenu();
    const pop = document.createElement('div'); pop.className = 'sndmenu'; pop.id = 'sndMenu';
    for (const s of ALERT_SOUNDS) {
      const it = document.createElement('div'); it.className = 'sndmenu-it' + (s === current ? ' sel' : '');
      const nm = document.createElement('span'); nm.textContent = s; it.appendChild(nm);
      if (s !== 'none') {
        const pv = document.createElement('span'); pv.className = 'pv'; pv.textContent = '▶';
        pv.addEventListener('click', e => { e.stopPropagation(); try { bridge().playSound(s); } catch (_) {} });
        it.appendChild(pv);
      }
      it.addEventListener('click', () => { onPick(s); closeSoundMenu(); });
      pop.appendChild(it);
    }
    document.body.appendChild(pop);                         // body-level so the scroll pane can't clip it
    placeMenu(pop, anchor);
    try { wmRectsSoon(); } catch (e) {}
    const selIt = pop.querySelector('.sndmenu-it.sel'); if (selIt) selIt.scrollIntoView({ block: 'nearest' });
  }
  function soundTrigger(getVal, setVal) {
    const b = document.createElement('button'); b.type = 'button'; b.className = 'al-sndsel';
    const cur = document.createElement('span'); cur.className = 'cur'; cur.textContent = getVal();
    const car = document.createElement('span'); car.className = 'cv'; car.textContent = '▾';
    b.appendChild(cur); b.appendChild(car);
    b.addEventListener('click', e => {
      e.stopPropagation();
      if (document.getElementById('sndMenu')) { closeSoundMenu(); return; }
      openSoundMenu(b, getVal(), v => { setVal(v); cur.textContent = v; if (v !== 'none') { try { bridge().playSound(v); } catch (_) {} } });
    });
    return b;
  }
  // Dismiss on outside click / page scroll / Escape, but NOT for events originating inside the
  // menu, so scrolling it does not close it.
  const insideSndMenu = e => !!(e && e.target && e.target.nodeType === 1 &&
                                e.target.closest && e.target.closest('.sndmenu'));
  document.addEventListener('click', e => { if (!insideSndMenu(e)) closeSoundMenu(); });
  document.addEventListener('scroll', e => { if (!insideSndMenu(e)) closeSoundMenu(); }, true);
  document.addEventListener('keydown', e => { if (e.key === 'Escape') closeSoundMenu(); });

  function segControl(opts, get, set) {
    const seg = document.createElement('div'); seg.className = 'al-seg';
    opts.forEach(([v, lab]) => {
      const b = document.createElement('button'); b.type = 'button'; b.textContent = lab;
      if (get() === v) b.classList.add('on');
      b.addEventListener('click', () => { if (get() === v) return; set(v, seg, b); });
      seg.appendChild(b);
    });
    return seg;
  }
  function kindSeg(w) {
    return segControl([['npc', 'NPC'], ['player', 'Player'], ['object', 'Object']],
      () => w.kind, (v, seg, b) => { w.kind = v; seg.querySelectorAll('button').forEach(x => x.classList.remove('on')); b.classList.add('on'); saveAlertCfg(); });
  }
  function setCustomType(w, v) {
    if ((w.type || 'name') === v) return;
    // An extra AND condition (no id) is limited to instantaneous tests.
    if (!w.id && AND_TYPE_SET.indexOf(v) < 0) return;
    w.type = v;
    if (v === 'auglevel' && (!w.anim || w.anim < 1)) w.anim = 12;
    if (v === 'invslots') { if (INVSLOT_CONDS.every(c => c[0] !== w.cond)) w.cond = 'empty'; }
    if (v === 'invitem')  { if (INVITEM_CONDS.every(c => c[0] !== w.cond)) w.cond = 'contains'; }
    if (v === 'buff')     { if (BUFF_CONDS.every(c => c[0] !== w.cond)) w.cond = 'lt'; fetchBuffs(); }
    if (v === 'vitals')   { if (BUFF_CONDS.every(c => c[0] !== w.cond)) w.cond = 'lt'; if (!VITALS.some(s => s[0] === w.stat)) w.stat = 'hp'; fetchPlayerVp(); }
    if (v === 'farm')     { if (FARM_CONDS.every(c => c[0] !== w.cond)) w.cond = 'ready'; fetchFarming(); }
    if (v === 'ground')   { fetchGround(); }
    if ((v === 'invslots' || v === 'invitem' || v === 'buff' || v === 'vitals') && (w.num == null)) w.num = 1;
    saveAlertCfg(); renderCustomList();
  }
  function typeTrigger(w) {
    const types = w.id ? CUSTOM_TYPES : AND_TYPES;
    const labelFor = v => { const t = types.find(x => x[0] === v); return t ? t[1] : 'Name'; };
    const b = document.createElement('button'); b.type = 'button'; b.className = 'al-typesel';
    const cur = document.createElement('span'); cur.className = 'cur'; cur.textContent = labelFor(w.type || 'name');
    const car = document.createElement('span'); car.className = 'cv'; car.textContent = '▾';
    b.appendChild(cur); b.appendChild(car);
    b.addEventListener('click', e => {
      e.stopPropagation();
      if (document.getElementById('sndMenu')) { closeSoundMenu(); return; }
      openChoice(b, types.map(([v, lab]) => ({ label: lab, act: () => setCustomType(w, v) })));
    });
    return b;
  }
  // Options pulled live from the equipment container (94) on open; "Any equipped" watches all.
  function augItemTrigger(w) {
    const b = document.createElement('button'); b.type = 'button'; b.className = 'al-typesel';
    const cur = document.createElement('span'); cur.className = 'cur';
    const car = document.createElement('span'); car.className = 'cv'; car.textContent = '▾';
    const nameOf = id => {
      const it = (perksData && Array.isArray(perksData.items) ? perksData.items : [])
                   .find(x => x.container === 94 && x.id === id);
      return it ? (it.name || ('Item ' + id)) : ('Item ' + id);
    };
    cur.textContent = w.augItem ? nameOf(w.augItem) : 'Any equipped';
    b.appendChild(cur); b.appendChild(car);
    b.addEventListener('click', e => {
      e.stopPropagation();
      if (document.getElementById('sndMenu')) { closeSoundMenu(); return; }
      const items = (perksData && Array.isArray(perksData.items)) ? perksData.items : [];
      const eq = items.filter(it => it.container === 94);
      if (!perksData) fetchPerks();   // not pulled yet -- reopen the dropdown once it loads
      const opts = [{ label: 'Any equipped', act: () => { w.augItem = 0; saveAlertCfg(); renderCustomList(); } }];
      for (const it of eq) {
        const id = it.id, lbl = (it.name || ('Item ' + id)) + ' · L' + (it.level || 0);
        opts.push({ label: lbl, act: () => { w.augItem = id; saveAlertCfg(); renderCustomList(); } });
      }
      if (!eq.length) opts.push({ label: '(no augmented items equipped)', act: () => {} });
      openChoice(b, opts);
    });
    return b;
  }
  function statTrigger(w) {
    const labelFor = v => { const t = VITALS.find(x => x[0] === v); return t ? t[1] : 'Life points'; };
    const b = document.createElement('button'); b.type = 'button'; b.className = 'al-typesel';
    const cur = document.createElement('span'); cur.className = 'cur'; cur.textContent = labelFor(w.stat || 'hp');
    const car = document.createElement('span'); car.className = 'cv'; car.textContent = '▾';
    b.appendChild(cur); b.appendChild(car);
    b.addEventListener('click', e => {
      e.stopPropagation();
      if (document.getElementById('sndMenu')) { closeSoundMenu(); return; }
      openChoice(b, VITALS.map(([v, lab]) => ({ label: lab, act: () => { w.stat = v; w.cond = (v === 'adren' ? 'gt' : 'lt'); saveAlertCfg(); renderCustomList(); } })));
    });
    return b;
  }
  function condTrigger(w, conds) {
    const labelFor = v => { const t = conds.find(x => x[0] === v); return t ? t[1] : conds[0][1]; };
    const b = document.createElement('button'); b.type = 'button'; b.className = 'al-typesel';
    const cur = document.createElement('span'); cur.className = 'cur'; cur.textContent = labelFor(w.cond);
    const car = document.createElement('span'); car.className = 'cv'; car.textContent = '▾';
    b.appendChild(cur); b.appendChild(car);
    b.addEventListener('click', e => {
      e.stopPropagation();
      if (document.getElementById('sndMenu')) { closeSoundMenu(); return; }
      openChoice(b, conds.map(([v, lab]) => ({ label: lab, act: () => { w.cond = v; if (w.num == null) w.num = 1; saveAlertCfg(); renderCustomList(); } })));
    });
    return b;
  }
  function buffTrigger(w) {
    const b = document.createElement('button'); b.type = 'button'; b.className = 'al-typesel'; b.title = 'Pick from active buffs';
    const cur = document.createElement('span'); cur.className = 'cur'; cur.textContent = 'Active';
    const car = document.createElement('span'); car.className = 'cv'; car.textContent = '▾';
    b.appendChild(cur); b.appendChild(car);
    b.addEventListener('click', e => {
      e.stopPropagation();
      if (document.getElementById('sndMenu')) { closeSoundMenu(); return; }
      if (!buffsData) fetchBuffs();
      const opts = activeBuffs().filter(x => x.name).map(x => {
        const v = buffValue(x);
        // show the bar's current number verbatim (no unit) so the threshold matches the display
        return { label: x.name + (v != null ? ' · now ' + v : ''), act: () => { w.text = x.name; saveAlertCfg(); renderCustomList(); } };
      });
      if (!opts.length) opts.push({ label: '(no active buffs - open the Buffs tab)', act: () => {} });
      openChoice(b, opts);
    });
    return b;
  }
  function farmPatchTrigger(w) {
    const b = document.createElement('button'); b.type = 'button'; b.className = 'al-typesel';
    const cur = document.createElement('span'); cur.className = 'cur'; cur.textContent = w.vb ? farmPatchLabel(w.vb) : 'Any patch';
    const car = document.createElement('span'); car.className = 'cv'; car.textContent = '▾';
    b.appendChild(cur); b.appendChild(car);
    b.addEventListener('click', e => {
      e.stopPropagation();
      if (document.getElementById('sndMenu')) { closeSoundMenu(); return; }
      const opts = [{ label: 'Any patch', act: () => { w.vb = 0; saveAlertCfg(); renderCustomList(); } }];
      for (const t of FARM.types) for (const vb of t[2])
        opts.push({ label: farmPatchLabel(vb), act: () => { w.vb = vb; saveAlertCfg(); renderCustomList(); } });
      openChoice(b, opts);
    });
    return b;
  }
  function numField(w, key, min) {
    const num = document.createElement('input'); num.type = 'number'; num.className = 'al-num'; num.min = String(min);
    num.value = String(w[key] != null ? w[key] : min);
    num.addEventListener('click', e => e.stopPropagation());
    num.addEventListener('input', () => { const v = parseInt(num.value, 10); w[key] = isNaN(v) ? min : v; saveAlertCfg(); });
    return num;
  }
  function customCard(w) {
    const card = document.createElement('div'); card.className = 'al-cust' + (w.enabled ? '' : ' off');
    const head = document.createElement('div'); head.className = 'al-cust-head';
    const enp = alertPill(w.enabled); enp.title = 'Enable this alert';
    enp.addEventListener('click', () => { w.enabled = !w.enabled; enp.classList.toggle('on', w.enabled); card.classList.toggle('off', !w.enabled); saveAlertCfg(); });
    const del = document.createElement('button'); del.type = 'button'; del.className = 'al-del'; del.textContent = '×'; del.title = 'Remove'; del.style.marginLeft = 'auto';
    del.addEventListener('click', () => { alertCfg.custom = alertCfg.custom.filter(x => x !== w); saveAlertCfg(); renderCustomList(); });
    head.appendChild(enp); head.appendChild(typeTrigger(w)); head.appendChild(del);
    card.appendChild(head);
    const grid = document.createElement('div'); grid.className = 'al-grid';
    const addRow = (labelText, control) => {
      const l = document.createElement('span'); l.className = 'al-lab'; l.textContent = labelText;
      grid.appendChild(l); grid.appendChild(control);
    };
    condRows(w, addRow);
    card.appendChild(grid);
    // Extra conditions: the alert only fires while ALL of them hold as well. Each is a small
    // card of its own with the same type picker and fields as the main trigger.
    const andHost = document.createElement('div'); andHost.className = 'al-and-list';
    (w.also || []).forEach((c, i) => andHost.appendChild(andCard(w, c, i)));
    card.appendChild(andHost);
    if (!w.also || w.also.length < AND_MAX) {
      const addAnd = document.createElement('button'); addAnd.type = 'button'; addAnd.className = 'al-add al-and-add';
      addAnd.textContent = '+ AND condition';
      addAnd.title = 'Require another condition to be true at the same time (e.g. a buff NOT active). Click its AND tag afterwards to turn it into an OR alternative.';
      addAnd.addEventListener('click', () => {
        if (!w.also) w.also = [];
        w.also.push(normAnd({ type: 'buff', cond: 'inactive' }));
        fetchBuffs();
        saveAlertCfg(); renderCustomList();
      });
      card.appendChild(addAnd);
    }
    const sndGrid = document.createElement('div'); sndGrid.className = 'al-grid';
    const addRow2 = (labelText, control) => {
      const l = document.createElement('span'); l.className = 'al-lab'; l.textContent = labelText;
      sndGrid.appendChild(l); sndGrid.appendChild(control);
    };
    const sndCell = document.createElement('div'); sndCell.className = 'al-cell';
    sndCell.appendChild(soundTrigger(() => w.sound, v => { w.sound = v; saveAlertCfg(); }));
    const play = document.createElement('button'); play.type = 'button'; play.className = 'al-play'; play.textContent = '▶'; play.title = 'Preview';
    play.addEventListener('click', e => { e.stopPropagation(); if (w.sound !== 'none') { try { bridge().playSound(w.sound); } catch (_) {} } });
    sndCell.appendChild(play);
    addRow2('Sound', sndCell);
    card.appendChild(sndGrid);
    return customCardFoot(w, card);
  }
  function andCard(w, c, i) {
    const box = document.createElement('div'); box.className = 'al-and';
    const head = document.createElement('div'); head.className = 'al-cust-head';
    // AND/OR toggle. AND must hold alongside the trigger; OR starts an alternative that
    // fires the alert on its own (AND binds tighter, so conditions after an OR chain to it).
    const tag = document.createElement('button'); tag.type = 'button';
    tag.className = 'al-evt al-and-tag' + (c.op === 'or' ? ' on' : '');
    tag.textContent = c.op === 'or' ? 'OR' : 'AND';
    tag.title = 'Click to switch. AND: must hold together with the trigger. OR: fires the alert on its own (later AND conditions chain to it).';
    tag.addEventListener('click', e => {
      e.stopPropagation();
      c.op = c.op === 'or' ? 'and' : 'or';
      saveAlertCfg(); renderCustomList();
    });
    head.appendChild(tag);
    // NOT toggle: "AND NOT using a magic prayer" reads better than hunting for an inverse comparator.
    const notB = document.createElement('button'); notB.type = 'button'; notB.className = 'al-evt' + (c.not ? ' on' : '');
    notB.textContent = 'NOT'; notB.title = 'Invert this condition';
    notB.addEventListener('click', e => { e.stopPropagation(); c.not = !c.not; notB.classList.toggle('on', c.not); saveAlertCfg(); });
    head.appendChild(notB);
    head.appendChild(typeTrigger(c));
    const del = document.createElement('button'); del.type = 'button'; del.className = 'al-del'; del.textContent = '×'; del.title = 'Remove this condition'; del.style.marginLeft = 'auto';
    del.addEventListener('click', () => { w.also.splice(i, 1); saveAlertCfg(); renderCustomList(); });
    head.appendChild(del);
    box.appendChild(head);
    const grid = document.createElement('div'); grid.className = 'al-grid';
    const addRow = (labelText, control) => {
      const l = document.createElement('span'); l.className = 'al-lab'; l.textContent = labelText;
      grid.appendChild(l); grid.appendChild(control);
    };
    condRows(c, addRow);
    box.appendChild(grid);
    return box;
  }
  // The type-specific fields of one condition. `w` is either the alert itself (main trigger)
  // or an entry of alert.also; every helper below only touches the fields of the object it is
  // given, so the same controls serve both.
  function condRows(w, addRow) {
    if (w.type === 'invslots') {
      addRow('Condition', condTrigger(w, INVSLOT_CONDS));
      if (w.cond === 'usedge' || w.cond === 'usedle' || w.cond === 'freele') addRow('Count', numField(w, 'num', 0));
    } else if (w.type === 'invitem') {
      const inp = document.createElement('input'); inp.type = 'text'; inp.className = 'al-input';
      inp.placeholder = 'Item name contains...'; inp.value = w.text || '';
      inp.addEventListener('input', () => { w.text = inp.value; saveAlertCfg(); });
      addRow('Item', inp);
      addRow('Match', condTrigger(w, INVITEM_CONDS));
      if (w.cond === 'ge' || w.cond === 'le' || w.cond === 'eq') addRow('Count', numField(w, 'num', 0));
    } else if (w.type === 'buff') {
      // Free-text name (matched by "contains") is needed for "Not active": the buff will not be on
      // the live quick-pick list.
      if (!buffsData) fetchBuffs();
      const cell = document.createElement('div'); cell.className = 'al-cell';
      const inp = document.createElement('input'); inp.type = 'text'; inp.className = 'al-input';
      inp.placeholder = 'Buff name contains...'; inp.value = w.text || '';
      inp.addEventListener('input', () => { w.text = inp.value; saveAlertCfg(); });
      cell.appendChild(inp);
      cell.appendChild(buffTrigger(w));
      addRow('Buff', cell);
      addRow('Is', condTrigger(w, BUFF_CONDS));
      if (BUFF_PRESENCE.indexOf(w.cond) < 0) addRow('Value', numField(w, 'num', 0));
    } else if (w.type === 'ground') {
      // Name search against the cache: typing lists matching items with their ids; picking one
      // pins the exact id (shown in the field), typing free text matches dropped items by name.
      const cell = document.createElement('div'); cell.className = 'al-cell';
      const inp = document.createElement('input'); inp.type = 'text'; inp.className = 'al-input';
      inp.placeholder = 'Item name (search the cache)...'; inp.value = w.text || '';
      let pickPaint = null;
      const openPick = () => {
        const q = inp.value.trim();
        if (!q || !bridge() || !bridge().menuSearch) return;
        let hits = [];
        try { const r = JSON.parse(rtxData.sync('host.menuSearch', 0, q, 40) || '{}'); hits = Array.isArray(r.hits) ? r.hits : []; } catch (e) {}
        const ents = hits.map(h => ({ label: h.name + '  (' + h.id + ')', raw: true, act: () => { w.text = h.name; w.item = h.id; inp.value = h.name; saveAlertCfg(); renderCustomList(); } }));
        if (!ents.length) ents.push({ label: '(no item named like that in the cache)', act: () => {} });
        closeSoundMenu(); openChoice(inp, ents);
      };
      inp.addEventListener('input', () => { w.text = inp.value; w.item = 0; saveAlertCfg(); openPick(); });
      inp.addEventListener('keydown', e => { e.stopPropagation(); if (e.key === 'Escape') closeSoundMenu(); });
      inp.addEventListener('click', e => e.stopPropagation());
      cell.appendChild(inp);
      const find = document.createElement('button'); find.type = 'button'; find.className = 'al-typesel'; find.title = 'Search the cache for this name';
      find.textContent = w.item ? ('id ' + w.item) : 'Search'; find.addEventListener('click', e => { e.stopPropagation(); openPick(); });
      cell.appendChild(find);
      addRow('Item', cell);
      const h = document.createElement('div'); h.className = 'al-desc';
      h.textContent = w.item ? 'Matches that exact item id when it is dropped within range.' : 'Matches any dropped item whose name contains the text. Pick a search result to pin the exact id.';
      addRow('', h);
    } else if (w.type === 'vitals') {
      if (!playerVp) fetchPlayerVp();
      addRow('Stat', statTrigger(w));
      addRow('Is', condTrigger(w, BUFF_CONDS));
      addRow('Value', numField(w, 'num', 0));
    } else if (w.type === 'farm') {
      if (!farmData) fetchFarming();
      addRow('Patch', farmPatchTrigger(w));
      addRow('When', condTrigger(w, FARM_CONDS));
    } else if (w.type === 'panim' || w.type === 'nanim' || w.type === 'auglevel') {
      const isLvl = w.type === 'auglevel';
      const num = document.createElement('input'); num.type = 'number'; num.className = 'al-num'; num.min = isLvl ? '1' : '-1';
      num.value = String(w.anim != null ? w.anim : 0);
      num.addEventListener('input', () => { const v = parseInt(num.value, 10); w.anim = isNaN(v) ? 0 : v; saveAlertCfg(); });
      addRow(isLvl ? 'Level' : 'Anim id', num);
      if (isLvl) { if (!perksData) fetchPerks(); addRow('Item', augItemTrigger(w)); }
    } else {
      const cell = document.createElement('div'); cell.className = 'al-cell';
      cell.appendChild(kindSeg(w));
      const inp = document.createElement('input'); inp.type = 'text'; inp.className = 'al-input';
      inp.placeholder = 'Name contains...'; inp.value = w.text || '';
      inp.addEventListener('input', () => { w.text = inp.value; saveAlertCfg(); });
      cell.appendChild(inp);
      addRow('Name', cell);
    }
  }
  function customCardFoot(w, card) {
    const foot = document.createElement('div'); foot.className = 'al-toggles';
    const mkTog = (label, on, set, tip) => {
      const t = document.createElement('div'); t.className = 'al-tog';
      const lb = document.createElement('span'); lb.className = 'al-lab'; lb.textContent = label;
      const p = alertPill(on); if (tip) p.title = tip;
      p.addEventListener('click', () => { const nv = !p.classList.contains('on'); p.classList.toggle('on', nv); set(nv); saveAlertCfg(); });
      t.appendChild(lb); t.appendChild(p); return t;
    };
    foot.appendChild(mkTog('Flash', w.flash, v => w.flash = v, 'Flash the game screen'));
    const nt = document.createElement('div'); nt.className = 'al-tog';
    const ntl = document.createElement('span'); ntl.className = 'al-lab'; ntl.textContent = 'Notify';
    nt.appendChild(ntl); nt.appendChild(notifyControl(() => w.notify, v => { w.notify = v; }));
    foot.appendChild(nt);
    foot.appendChild(mkTog('Repeat', w.repeat, v => w.repeat = v, 'Keep notifying while the trigger stays active'));
    card.appendChild(foot);
    return card;
  }
  function renderCustomList() {
    const host = document.getElementById('alertCustomList'); if (!host) return;
    // Same reason as the Auras editor: wiping a focused input fires no focusout and leaves
    // keyboard capture stuck on. Blur it first.
    const fe = document.activeElement;
    if (fe && host.contains(fe) && fe.blur) { try { fe.blur(); } catch (e) {} }
    host.innerHTML = '';
    if (!Array.isArray(alertCfg.custom) || !alertCfg.custom.length) {
      const e = document.createElement('div'); e.className = 'al-desc'; e.style.padding = '2px 4px';
      e.textContent = 'No custom alerts. Add one (or pick a preset) to watch a name, a player/NPC animation, an equipped augment level, your inventory (slots / items), a buff value (e.g. timer below N), a farming patch becoming ready, or an item dropping on the ground. Add AND conditions to combine them, e.g. NPC anim 1234 AND NOT buff "Deflect Magic" active.';
      host.appendChild(e); return;
    }
    for (const w of alertCfg.custom) host.appendChild(customCard(w));
  }
  function addCustom(seed) {
    if (!alertCfg.custom) alertCfg.custom = [];
    alertCfg.custom.push(Object.assign(
      { id: 'c' + Date.now() + '_' + (alertCustomSeq++), type: 'name', kind: 'npc', text: '', anim: 0, label: '', sound: 'alert 1', flash: false, enabled: true },
      seed || {}));
    saveAlertCfg(); renderCustomList();
  }
  function addPreset(p) {
    addCustom({ type: p.type || 'name', kind: p.kind || 'npc', text: p.text || '', anim: p.anim || 0,
                cond: p.cond, num: p.num, label: p.name || '', sound: p.sound || 'alert 1', flash: !!p.flash });
  }
  function openChoice(anchor, entries) {
    closeSoundMenu();
    const pop = document.createElement('div'); pop.className = 'sndmenu'; pop.id = 'sndMenu';
    for (const e of entries) {
      const it = document.createElement('div'); it.className = 'sndmenu-it';
      // raw: keep the label's exact case (token guides, item names); the default capitalize
      // exists for the sound names ("alert 1" -> "Alert 1") and mangles anything case-sensitive.
      if (e.raw) it.style.textTransform = 'none';
      const nm = document.createElement('span'); nm.textContent = e.label; nm.style.flex = '1'; it.appendChild(nm);
      it.addEventListener('click', () => { e.act(); closeSoundMenu(); });
      pop.appendChild(it);
    }
    document.body.appendChild(pop);
    placeMenu(pop, anchor);
    // The menu floats outside its window: publish its rect, or any entry that hangs past the
    // window edge is a click on the game instead.
    try { wmRectsSoon(); } catch (e) {}
  }

  function notifyControl(get, set, id) {
    const b = document.createElement('button'); b.type = 'button'; b.className = 'al-sndsel';
    if (id) b.id = id;
    b.title = 'Where to deliver this alert: in the game overlay, a Windows notification, or both';
    const paint = () => { b.textContent = NOTIFY_LABELS[get()] || 'In-game'; };
    paint();
    b.addEventListener('click', e => {
      e.stopPropagation();
      const i = NOTIFY_TYPES.indexOf(get());
      set(NOTIFY_TYPES[(i + 1) % NOTIFY_TYPES.length]);
      paint(); saveAlertCfg();
    });
    return b;
  }
  function renderAlerts() {
    if (!alertCfg) loadAlertCfg();
    const c = $('content');
    if ($('alertsWrap')) { reflectAlerts(); return; }
    c.innerHTML = '';
    const wrap = document.createElement('div'); wrap.id = 'alertsWrap'; wrap.className = 'al-wrap';
    const m = document.createElement('div'); m.className = 'al-master'; m.setAttribute('role', 'button');
    const ml = document.createElement('div');
    const mn = document.createElement('div'); mn.className = 'al-name'; mn.textContent = 'Enable alerts';
    const md = document.createElement('div'); md.className = 'al-desc'; md.textContent = 'Master switch - sound plays + the game window flashes';
    ml.appendChild(mn); ml.appendChild(md);
    const mp = alertPill(false); mp.id = 'al_master_pill';
    m.appendChild(ml); m.appendChild(mp);
    m.addEventListener('click', () => { alertCfg.master = !alertCfg.master; reflectAlerts(); saveAlertCfg(); });
    wrap.appendChild(m);
    // Global focus gate, right under the master switch: applies to every alert below, custom
    // alerts and aura actions included.
    const fo = document.createElement('div'); fo.className = 'al-master'; fo.setAttribute('role', 'button');
    const fol = document.createElement('div');
    const fon = document.createElement('div'); fon.className = 'al-name'; fon.textContent = 'Only alert when tabbed out';
    const fod = document.createElement('div'); fod.className = 'al-desc';
    const hostOk = !!(bridge() && bridge().gameFocused);
    fod.textContent = hostOk ? 'Stay quiet while the game window has focus; sound, flash and messages fire only when you are in another window'
                             : 'Needs a newer RuneToolsX launcher (this one cannot tell whether the game has focus), so it has no effect yet';
    if (!hostOk) fod.style.color = '#ffb86b';
    fol.appendChild(fon); fol.appendChild(fod);
    const fop = alertPill(false); fop.id = 'al_focus_pill';
    fo.appendChild(fol); fo.appendChild(fop);
    fo.addEventListener('click', () => { alertCfg.unfocusedOnly = !alertCfg.unfocusedOnly; reflectAlerts(); saveAlertCfg(); });
    wrap.appendChild(fo);
    for (const meta of ALERT_META) {
      const r = alertCfg.rules[meta.id];
      const card = document.createElement('div'); card.className = 'al-card'; card.id = 'al_' + meta.id;
      const head = document.createElement('div'); head.className = 'al-head'; head.setAttribute('role', 'button');
      const hl = document.createElement('div');
      const nm = document.createElement('div'); nm.className = 'al-name'; nm.textContent = meta.name;
      const ds = document.createElement('div'); ds.className = 'al-desc'; ds.textContent = meta.desc;
      hl.appendChild(nm); hl.appendChild(ds);
      const enp = alertPill(r.enabled); enp.id = 'al_' + meta.id + '_en';
      head.appendChild(hl); head.appendChild(enp);
      head.addEventListener('click', () => { r.enabled = !r.enabled; reflectAlerts(); saveAlertCfg(); });
      card.appendChild(head);
      const ctl = document.createElement('div'); ctl.className = 'al-ctl'; ctl.id = 'al_' + meta.id + '_ctl';
      if (meta.param) {
        const pc = document.createElement('div'); pc.className = 'al-param';
        const plab = document.createElement('span'); plab.className = 'lab'; plab.textContent = meta.param.label;
        const pin = document.createElement('input'); pin.type = 'number'; pin.className = 'al-num';
        if (meta.param.min != null) pin.min = String(meta.param.min);
        if (meta.param.max != null) pin.max = String(meta.param.max);
        pin.value = String(r.val != null ? r.val : meta.param.def);
        pin.addEventListener('click', e => e.stopPropagation());
        pin.addEventListener('input', e => {
          e.stopPropagation();
          const v = parseInt(pin.value, 10);
          r.val = isNaN(v) ? meta.param.def : v;
          saveAlertCfg();
        });
        pc.appendChild(plab); pc.appendChild(pin);
        if (meta.param.suffix) { const su = document.createElement('span'); su.className = 'lab'; su.textContent = meta.param.suffix; pc.appendChild(su); }
        ctl.appendChild(pc);
      }
      // Per-event picker: which of the tracked events actually alert + highlight.
      // Absent from a saved config means "all on", so upgrading changes nothing.
      if (meta.id === 'random') {
        const pick = document.createElement('div'); pick.className = 'al-events';
        for (const nm2 of RANDOM_EVENT_NAMES) {
          const ch = document.createElement('button'); ch.type = 'button';
          ch.className = 'al-evt' + (randomEventOn(nm2) ? ' on' : '');
          ch.textContent = nm2;
          ch.addEventListener('click', e => {
            e.stopPropagation();
            randomEventSet(nm2, !randomEventOn(nm2));
            ch.classList.toggle('on', randomEventOn(nm2));
            syncOverlayHighlight();          // the highlight list changed
            saveAlertCfg();
          });
          pick.appendChild(ch);
        }
        ctl.appendChild(pick);
      }
      const fl = document.createElement('div'); fl.className = 'al-flash';
      const flab = document.createElement('span'); flab.className = 'lab'; flab.textContent = 'Flash';
      const flp = alertPill(r.flash); flp.id = 'al_' + meta.id + '_flash';
      flp.addEventListener('click', e => { e.stopPropagation(); r.flash = !r.flash; reflectAlerts(); saveAlertCfg(); });
      fl.appendChild(flab); fl.appendChild(flp);
      const nf = document.createElement('div'); nf.className = 'al-flash';
      const nlab = document.createElement('span'); nlab.className = 'lab'; nlab.textContent = 'Notify';
      nf.appendChild(nlab);
      nf.appendChild(notifyControl(() => r.notify, v => { r.notify = v; }, 'al_' + meta.id + '_notify'));
      const snd = document.createElement('div'); snd.className = 'al-snd';
      const slab = document.createElement('span'); slab.className = 'lab'; slab.textContent = 'Sound';
      const strig = soundTrigger(() => r.sound, v => { r.sound = v; saveAlertCfg(); });
      const play = document.createElement('button'); play.type = 'button'; play.className = 'al-play'; play.textContent = '▶'; play.title = 'Preview';
      play.addEventListener('click', e => { e.stopPropagation(); if (r.sound !== 'none') { try { bridge().playSound(r.sound); } catch (_) {} } });
      snd.appendChild(slab); snd.appendChild(strig); snd.appendChild(play);
      ctl.appendChild(fl);
      ctl.appendChild(nf);
      if (meta.repeatable) {     // "keep notifying" while the condition holds (e.g. idle)
        const rp = document.createElement('div'); rp.className = 'al-flash';
        const rlab = document.createElement('span'); rlab.className = 'lab'; rlab.textContent = 'Repeat';
        const rpp = alertPill(r.repeat); rpp.id = 'al_' + meta.id + '_repeat';
        rpp.title = meta.repeatHint || ('Keep notifying every ' + (r.val || 5) + 's while still idle');
        rpp.addEventListener('click', e => { e.stopPropagation(); r.repeat = !r.repeat; reflectAlerts(); saveAlertCfg(); });
        rp.appendChild(rlab); rp.appendChild(rpp); ctl.appendChild(rp);
      }
      ctl.appendChild(snd);
      card.appendChild(ctl);
      wrap.appendChild(card);
    }
    const sect = document.createElement('div'); sect.className = 'al-sect'; sect.textContent = 'Custom alerts';
    wrap.appendChild(sect);
    const clist = document.createElement('div'); clist.id = 'alertCustomList';
    clist.style.display = 'flex'; clist.style.flexDirection = 'column'; clist.style.gap = '6px';
    wrap.appendChild(clist);
    const addRow = document.createElement('div'); addRow.style.display = 'flex'; addRow.style.gap = '6px';
    const add = document.createElement('button'); add.type = 'button'; add.className = 'al-add';
    add.style.width = 'auto'; add.style.flex = '1'; add.textContent = '+ Add custom alert';
    add.addEventListener('click', () => addCustom());
    const pre = document.createElement('button'); pre.type = 'button'; pre.className = 'al-add';
    pre.style.width = 'auto'; pre.style.flex = '0 0 auto'; pre.textContent = '+ Preset ▾';
    pre.addEventListener('click', e => {
      e.stopPropagation();
      if (document.getElementById('sndMenu')) { closeSoundMenu(); return; }
      openChoice(pre, ALERT_PRESETS.map(p => ({
        label: p.name + (p.type === 'panim' ? ' · player anim ' + p.anim
                       : p.type === 'nanim' ? ' · NPC anim ' + p.anim
                       : p.type === 'auglevel' ? ' · level ' + p.anim : ''),
        act: () => addPreset(p)
      })));
    });
    addRow.appendChild(add); addRow.appendChild(pre);
    wrap.appendChild(addRow);
    const log = document.createElement('div'); log.className = 'al-log';
    const lt = document.createElement('div'); lt.className = 'ttl'; lt.textContent = 'Recent';
    const ll = document.createElement('div'); ll.id = 'alertLogList';
    log.appendChild(lt); log.appendChild(ll); wrap.appendChild(log);
    c.appendChild(wrap);
    renderCustomList(); reflectAlerts(); renderAlertLog();
  }
  function reflectAlerts() {
    if (!alertCfg) return;
    const mp = $('al_master_pill'); if (mp) mp.classList.toggle('on', !!alertCfg.master);
    const fp = $('al_focus_pill'); if (fp) fp.classList.toggle('on', !!alertCfg.unfocusedOnly);
    for (const meta of ALERT_META) {
      const r = alertCfg.rules[meta.id];
      const en = $('al_' + meta.id + '_en');    if (en) en.classList.toggle('on', r.enabled);
      const fl = $('al_' + meta.id + '_flash'); if (fl) fl.classList.toggle('on', r.flash);
      const nf = $('al_' + meta.id + '_notify'); if (nf) nf.textContent = NOTIFY_LABELS[r.notify] || 'In-game';
      const rp = $('al_' + meta.id + '_repeat'); if (rp) rp.classList.toggle('on', !!r.repeat);
      const ctl = $('al_' + meta.id + '_ctl');  if (ctl) ctl.style.opacity = (alertCfg.master && r.enabled) ? 1 : 0.4;
      const card = $('al_' + meta.id);          if (card) card.style.opacity = alertCfg.master ? 1 : 0.55;
    }
  }
  function renderAlertLog() {
    const ll = $('alertLogList'); if (!ll) return;
    if (!alertLog.length) { ll.innerHTML = '<div class="ev"><span class="m" style="color:var(--text-mute)">No alerts yet</span><span></span></div>'; return; }
    ll.innerHTML = '';
    for (const e of alertLog) {
      const row = document.createElement('div'); row.className = 'ev';
      const m = document.createElement('span'); m.className = 'm'; m.textContent = e.msg;
      const d = new Date(e.t);
      const t = document.createElement('span');
      t.textContent = ('0' + d.getHours()).slice(-2) + ':' + ('0' + d.getMinutes()).slice(-2) + ':' + ('0' + d.getSeconds()).slice(-2);
      row.appendChild(m); row.appendChild(t); ll.appendChild(row);
    }
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { ALERT_SOUNDS, FARM_CONDS, FARM_COND_CODES, alertsNeedBuffs, alertsNeedFarming, alertsNeedGoals, alertsNeedGround, alertsNeedInfo, alertsNeedInv, alertsNeedPerks, alertsNeedScene, alertsNeedVitals, alertsSuppressed, closeSoundMenu, evalAlerts, fetchGround, fireAlert, loadAlertCfg, openChoice, overlayHighlightResync, placeMenu, saveAlertCfg, syncOverlayHighlight, winNotify });
registerTab({ id: 'alerts', render: renderAlerts });
})();
