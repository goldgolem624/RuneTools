// RuneToolsX panel: Auras. A WeakAuras-style display engine for the in-game HUD.
// Spliced inline into client.html; IIFE (window exports + registerTab; see the RTX registry in client.html).
//
// MODEL (mirrors WeakAuras' data table, trimmed to what this game exposes):
//   aura = { uid, name, type, parent, children[], x, y, w, h, ...display props,
//            triggers: { list: [trigger], logic: 'any'|'all', active: -1|index },
//            conditions: [ { checks: [ {trig, v, op, val} ], changes: [ {prop, val} ] } ],
//            actions: { show: {sound, flash, notify, msg}, hide: {...} },
//            anim: { start, main, finish }, load: { combat } }
//   types: icon | bar | text | ring | pips (displays), group (static: children keep their own
//          x/y/w/h inside it), dyngroup (lays children out: grow direction, spacing, sort, limit,
//          and a trigger that matches several things draws one CLONE per match).
//   Every TOP-LEVEL aura or group is its own HUD window (tab 'aura:<uid>'), so it is dragged,
//   resized and locked like every other window; its children move with it.
//
// TRIGGERS produce a STATE: { show, progressType: 'timed'|'static'|'none', remaining, duration,
//   value, total, name, icon, stacks }. Several triggers per aura combine with any/all; the
//   "active" trigger supplies what the display shows (first active, or a fixed one). Conditions
//   read any trigger's state ("trigger 2 remaining < 5") and change display properties while they
//   hold; Actions fire once on show / on hide; Animations are CSS presets.
//
// TEXT TOKENS: %p progress (remaining time or value), %t total (duration or max), %n name,
//   %s stacks, %v value, %r remaining seconds (raw), %c percent, %2.p = token from trigger 2,
//   %% = a literal %.
//
// NOTE ON LOAD ORDER: panels are spliced BEFORE client.html's own script runs, so prefGet /
// prefSet do not exist yet at parse time. Config is loaded lazily on first use, never at top level.
(function () {

  // ---------------------------------------------------------------- constants
  const AURA_TYPES = [['icon', 'Icon'], ['bar', 'Bar'], ['text', 'Text'], ['ring', 'Ring'], ['pips', 'Stack pips'], ['group', 'Group'], ['dyngroup', 'Dynamic group']];
  const AURA_DISPLAY_TYPES = ['icon', 'bar', 'text', 'ring', 'pips'];
  const AURA_TRIG_SRCS = [['buff', 'Buff'], ['debuff', 'Debuff'], ['ability', 'Ability cooldown'], ['vital', 'Vital'], ['panim', 'Player animation'], ['nanim', 'NPC animation'], ['nearby', 'Nearby name'], ['invslots', 'Inventory slots'], ['invitem', 'Inventory item'], ['always', 'Always on']];
  const AURA_VITALS = [['hp', 'Health'], ['prayer', 'Prayer'], ['summon', 'Summoning'], ['adren', 'Adrenaline']];
  const AURA_OPS_NUM = [['any', 'Any value'], ['<', '<'], ['<=', '<='], ['=', '='], ['>=', '>='], ['>', '>']];
  const AURA_OPS_PRES = [['active', 'Active'], ['inactive', 'Not active']];
  const AURA_FIELDS_BUFF = [['time', 'Time left'], ['stacks', 'Value / stacks']];
  const AURA_COND_VARS = [['show', 'Active'], ['remaining', 'Time left'], ['duration', 'Duration'], ['value', 'Value'], ['stacks', 'Stacks'], ['pct', 'Percent'], ['name', 'Name']];
  const AURA_COND_OPS = [['<', '<'], ['<=', '<='], ['=', '='], ['>=', '>='], ['>', '>'], ['!=', 'not =']];
  const AURA_COND_PROPS = [['color', 'Colour'], ['alpha', 'Opacity'], ['glow', 'Glow'], ['desat', 'Desaturate'], ['hide', 'Hide'], ['text', 'Text override'], ['scale', 'Scale'], ['sound', 'Play sound']];
  const AURA_ANIM_START = [['none', 'None'], ['fade', 'Fade in'], ['grow', 'Grow'], ['shrink', 'Shrink in'], ['slideL', 'Slide from left'], ['slideR', 'Slide from right'], ['slideU', 'Slide from top'], ['slideD', 'Slide from bottom'], ['bounce', 'Bounce']];
  const AURA_ANIM_MAIN = [['none', 'None'], ['pulse', 'Pulse'], ['flash', 'Flash'], ['shake', 'Shake'], ['spin', 'Spin'], ['wobble', 'Wobble'], ['bounce', 'Bounce']];
  const AURA_ANIM_FINISH = [['none', 'None'], ['fade', 'Fade out'], ['shrink', 'Shrink'], ['grow', 'Grow out'], ['slideL', 'Slide left'], ['slideR', 'Slide right'], ['slideU', 'Slide up'], ['slideD', 'Slide down']];
  const AURA_GROW = [['right', 'Right'], ['left', 'Left'], ['down', 'Down'], ['up', 'Up'], ['grid', 'Grid (wrap)']];
  const AURA_SORT = [['none', 'Trigger order'], ['asc', 'Time left ascending'], ['desc', 'Time left descending'], ['name', 'Name']];
  const AURA_NOTIFY = [['none', 'No message'], ['ingame', 'In-game'], ['windows', 'Windows'], ['both', 'Both']];
  const AURA_COMBAT = [['any', 'Always'], ['in', 'Only in combat'], ['out', 'Only out of combat']];
  const AURA_PALETTE = ['#8c6ffd', '#b14df0', '#4fe07a', '#ff5a5a', '#ffc93a', '#4fb2ff', '#f2f2f8', '#ff8a3d', '#2dd4bf', '#0e0f14'];
  const AURA_MAX_CLONES = 20;
  const AURA_EXPORT_PREFIX = 'RTXA1!';

  // A fresh aura of a given type: every field present, so nothing downstream needs null checks.
  function auraDefaults(type) {
    const d = {
      uid: '', name: '', type: type || 'icon', parent: null, children: [], winGeom: null,
      x: 0, y: 0, w: 56, h: 56,
      alpha: 1, color: '#ffffff', desat: false, hideIdle: true, glow: false,
      // icon
      showTime: true, showStacks: true, sweep: true, iconName: '',
      // bar
      orient: 'h', barColor: '#8c6ffd', bgColor: '#0e0f14', inverse: false, textL: '%n', textR: '%p',
      // text
      text: '%p', fontSize: 18, textColor: '#ffffff', align: 'center',
      // ring
      ringColor: '#8c6ffd', ringWidth: 6,
      // pips
      max: 5, pipColor: '#b14df0',
      // dyngroup
      grow: 'right', space: 4, limit: 0, sort: 'none', gridCols: 5, childW: 48, childH: 48,
      triggers: { list: [], logic: 'any', active: -1 },
      conditions: [],
      actions: { show: { sound: 'none', flash: false, notify: 'none', msg: '' }, hide: { sound: 'none', flash: false, notify: 'none', msg: '' } },
      anim: { start: 'none', main: 'none', finish: 'none' },
      load: { combat: 'any' },
    };
    if (type === 'bar') { d.w = 180; d.h = 22; }
    if (type === 'text') { d.w = 120; d.h = 28; }
    if (type === 'ring') { d.w = 64; d.h = 64; }
    if (type === 'pips') { d.w = 64; d.h = 76; }
    if (type === 'group') { d.w = 240; d.h = 120; }
    if (type === 'dyngroup') { d.w = 240; d.h = 56; }
    return d;
  }
  function auraTrigDefaults(src) {
    return { src: src || 'buff', name: '', field: 'time', op: 'active', val: 0, stat: 'hp', kind: 'npc', anim: 0, clones: false };
  }
  function auraNewUid() { return 'a' + Date.now().toString(36) + Math.floor(Math.random() * 1e6).toString(36); }
  function auraIsNum(v) { return typeof v === 'number' && isFinite(v); }
  function auraNormTrig(t) {
    const d = auraTrigDefaults();
    if (!t || typeof t !== 'object') return d;
    const o = Object.assign({}, d);
    if (AURA_TRIG_SRCS.some(s => s[0] === t.src)) o.src = t.src;
    if (typeof t.name === 'string') o.name = t.name;
    if (t.field === 'time' || t.field === 'stacks') o.field = t.field;
    if (typeof t.op === 'string') o.op = t.op;
    if (auraIsNum(t.val)) o.val = t.val;
    if (AURA_VITALS.some(v => v[0] === t.stat)) o.stat = t.stat;
    if (['npc', 'player', 'object'].indexOf(t.kind) >= 0) o.kind = t.kind;
    if (auraIsNum(t.anim)) o.anim = Math.round(t.anim);
    if (typeof t.clones === 'boolean') o.clones = t.clones;
    return o;
  }
  function auraNorm(a) {
    if (!a || typeof a !== 'object') return null;
    const type = AURA_TYPES.some(t => t[0] === a.type) ? a.type : 'icon';
    const d = auraDefaults(type);
    const o = Object.assign({}, d);
    for (const k in d) {
      if (!(k in a)) continue;
      const v = a[k], dv = d[k];
      if (k === 'triggers' || k === 'conditions' || k === 'actions' || k === 'anim' || k === 'load' || k === 'children') continue;
      if (typeof dv === 'number' && auraIsNum(v)) o[k] = v;
      else if (typeof dv === 'string' && typeof v === 'string') o[k] = v;
      else if (typeof dv === 'boolean' && typeof v === 'boolean') o[k] = v;
      else if (dv === null && (v === null || typeof v === 'string')) o[k] = v;
    }
    o.uid = (typeof a.uid === 'string' && a.uid) ? a.uid : auraNewUid();
    o.winGeom = (a.winGeom && typeof a.winGeom === 'object' && auraIsNum(a.winGeom.w) && auraIsNum(a.winGeom.h))
      ? { x: a.winGeom.x | 0, y: a.winGeom.y | 0, w: a.winGeom.w | 0, h: a.winGeom.h | 0, lock: !!a.winGeom.lock } : null;
    o.children = Array.isArray(a.children) ? a.children.filter(c => typeof c === 'string') : [];
    if (a.triggers && typeof a.triggers === 'object') {
      o.triggers = { list: Array.isArray(a.triggers.list) ? a.triggers.list.map(auraNormTrig).slice(0, 8) : [],
                     logic: a.triggers.logic === 'all' ? 'all' : 'any',
                     active: auraIsNum(a.triggers.active) ? Math.round(a.triggers.active) : -1 };
    }
    if (Array.isArray(a.conditions)) {
      o.conditions = a.conditions.filter(c => c && typeof c === 'object').map(c => ({
        checks: Array.isArray(c.checks) ? c.checks.filter(x => x && typeof x === 'object').map(x => ({
          trig: auraIsNum(x.trig) ? Math.round(x.trig) : -1,
          v: AURA_COND_VARS.some(z => z[0] === x.v) ? x.v : 'remaining',
          op: AURA_COND_OPS.some(z => z[0] === x.op) ? x.op : '<',
          val: (typeof x.val === 'string' || auraIsNum(x.val) || typeof x.val === 'boolean') ? x.val : 5,
        })) : [],
        changes: Array.isArray(c.changes) ? c.changes.filter(x => x && typeof x === 'object').map(x => ({
          prop: AURA_COND_PROPS.some(z => z[0] === x.prop) ? x.prop : 'color',
          val: (typeof x.val === 'string' || auraIsNum(x.val) || typeof x.val === 'boolean') ? x.val : '',
        })) : [],
      })).slice(0, 12);
    }
    const na = (x, dd) => {
      const r = Object.assign({}, dd);
      if (x && typeof x === 'object') {
        if (typeof x.sound === 'string') r.sound = x.sound;
        if (typeof x.flash === 'boolean') r.flash = x.flash;
        if (AURA_NOTIFY.some(n => n[0] === x.notify)) r.notify = x.notify;
        if (typeof x.msg === 'string') r.msg = x.msg;
      }
      return r;
    };
    if (a.actions && typeof a.actions === 'object') o.actions = { show: na(a.actions.show, d.actions.show), hide: na(a.actions.hide, d.actions.hide) };
    if (a.anim && typeof a.anim === 'object') {
      o.anim = {
        start: AURA_ANIM_START.some(z => z[0] === a.anim.start) ? a.anim.start : 'none',
        main: AURA_ANIM_MAIN.some(z => z[0] === a.anim.main) ? a.anim.main : 'none',
        finish: AURA_ANIM_FINISH.some(z => z[0] === a.anim.finish) ? a.anim.finish : 'none',
      };
    }
    if (a.load && typeof a.load === 'object') o.load = { combat: AURA_COMBAT.some(z => z[0] === a.load.combat) ? a.load.combat : 'any' };
    return o;
  }

  // ---------------------------------------------------------------- config store
  let auraCfg = null;        // { v: 3, auras: {uid: aura}, roots: [uid] }
  let auraDirty = false;
  const auraWinSig = {};
  let auraEdSel = null;      // editor: selected uid
  let auraEdTab = 'display';
  let auraEdSig = '';
  let auraEdImport = false;
  const auraIconByName = {};
  let auraSeen = null;

  function auraCfgLoad() {
    if (auraCfg) return;
    auraCfg = { v: 3, auras: {}, roots: [] };
    let v = null;
    try { const raw = prefGet('rtxAuras', ''); if (raw) v = JSON.parse(raw); } catch (e) { v = null; }
    if (!v || typeof v !== 'object') return;
    if (v.v === 3 && v.auras && typeof v.auras === 'object') {
      for (const k in v.auras) { const a = auraNorm(v.auras[k]); if (a) { a.uid = k; auraCfg.auras[k] = a; } }
      auraCfg.roots = (Array.isArray(v.roots) ? v.roots : []).filter(u => auraCfg.auras[u]);
      // Repair: children that vanished, parents that vanished, roots missing.
      for (const k in auraCfg.auras) {
        const a = auraCfg.auras[k];
        a.children = a.children.filter(c => auraCfg.auras[c]);
        a.children.forEach(c => { auraCfg.auras[c].parent = k; });
        if (a.parent && !auraCfg.auras[a.parent]) a.parent = null;
        if (!a.parent && auraCfg.roots.indexOf(k) < 0) auraCfg.roots.push(k);
      }
      auraCfg.roots = auraCfg.roots.filter(u => !auraCfg.auras[u].parent);
    } else if (v.v === 2 && Array.isArray(v.groups)) {
      auraMigrateV2(v.groups);
    } else if (Array.isArray(v.names)) {
      auraMigrateV2([{ id: 'legacy', name: 'Auras', items: v.names.map(n => ({ name: n, src: 'buff', mode: 'icon' })), showIdle: !!v.showIdle, showName: !!v.showName }]);
    }
  }
  // v2 (groups of {name, src, mode, max, color}) -> one dynamic group per v2 group, one aura per item.
  function auraMigrateV2(groups) {
    groups.forEach(g => {
      if (!g || !Array.isArray(g.items)) return;
      const grp = auraDefaults('dyngroup');
      grp.uid = (typeof g.id === 'string' && g.id) ? 'g_' + g.id : auraNewUid();
      grp.name = g.name || 'Auras'; grp.grow = 'grid'; grp.gridCols = g.cols > 0 ? g.cols : 6; grp.childW = 48; grp.childH = 48; grp.space = 6;
      auraCfg.auras[grp.uid] = grp; auraCfg.roots.push(grp.uid);
      g.items.forEach(it => {
        if (!it || !it.name) return;
        const src = it.src || 'buff';
        let a;
        if (src === 'vital') {
          a = auraDefaults('bar'); a.name = ({ hp: 'Health', prayer: 'Prayer', summon: 'Summoning', adren: 'Adrenaline' })[it.name] || it.name;
          a.barColor = ({ hp: '#ff5a5a', prayer: '#4fb2ff', summon: '#4fe07a', adren: '#ffc93a' })[it.name] || '#8c6ffd';
          a.textL = '%n'; a.textR = '%p / %t'; a.hideIdle = false;
          a.triggers.list.push(Object.assign(auraTrigDefaults('vital'), { stat: it.name, op: 'any' }));
        } else if (src === 'ability') {
          a = auraDefaults('icon'); a.name = it.name; a.hideIdle = false;
          a.triggers.list.push(Object.assign(auraTrigDefaults('ability'), { name: it.name, op: 'any' }));
        } else {
          a = auraDefaults(it.mode === 'pips' ? 'pips' : 'icon'); a.name = it.name; a.hideIdle = !g.showIdle;
          if (it.mode === 'pips') { a.max = it.max || 5; a.pipColor = ({ purple: '#b14df0', green: '#4fe07a', red: '#ff5a5a', gold: '#ffc93a', blue: '#4fb2ff', white: '#f2f2f8' })[it.color] || '#8c6ffd'; }
          a.triggers.list.push(Object.assign(auraTrigDefaults('buff'), { name: it.name, op: 'active' }));
        }
        a.uid = auraNewUid(); a.parent = grp.uid; grp.children.push(a.uid); auraCfg.auras[a.uid] = a;
      });
    });
    auraDirty = true; auraCfgSave();
  }
  function auraCfgSave() {
    auraDirty = true;
    try { prefSet('rtxAuras', JSON.stringify({ v: 3, auras: auraCfg.auras, roots: auraCfg.roots })); } catch (e) {}
  }
  function auraApplyDurablePrefs() {
    if (auraDirty || !auraCfg) return;
    auraCfg = null; auraSeen = null;
    auraCfgLoad();
    auraInvalidate();
    if (typeof renderMenubar === 'function') { try { renderMenubar(); } catch (e) {} }
    auraRepaintAll();
  }
  function auraAll() { auraCfgLoad(); return auraCfg.auras; }
  function auraGet(uid) { return uid ? (auraAll()[uid] || null) : null; }
  function auraRoots() { auraCfgLoad(); return auraCfg.roots.map(u => auraCfg.auras[u]).filter(Boolean); }
  function auraInvalidate() { for (const k in auraWinSig) delete auraWinSig[k]; auraEdSig = ''; }

  // ---------------------------------------------------------------- HUD tabs (one per root)
  function auraTabId(uid) { return 'aura:' + uid; }
  function auraGroupTabs() {
    if (typeof prefGet !== 'function') return [];
    return auraRoots().map(a => ({ id: auraTabId(a.uid), label: a.name || 'Aura', cat: 'HUD', hidden: true, icon: '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>' }));
  }
  function auraIsHudTab(id) { return typeof id === 'string' && id.indexOf('aura:') === 0; }
  function auraAnyHudVisible() { return typeof paneVisible === 'function' && auraRoots().some(a => paneVisible(auraTabId(a.uid))); }
  function auraWinShown(uid) { return !!(typeof wmWinOf === 'function' && wmWinOf(auraTabId(uid))); }
  // Hiding closes the HUD window, and the window manager forgets a closed window's placement;
  // the aura remembers its own so show/hide round-trips to the same spot, size and lock state.
  function auraWinShow(uid, on) {
    const tid = auraTabId(uid);
    const a = auraGet(uid);
    if (on) {
      if (auraWinShown(uid)) return;
      const t = allTabs().find(x => x.id === tid);
      if (!t) return;
      const g = a && a.winGeom;
      openTab(t, g ? { noFocus: true, geom: { x: g.x | 0, y: g.y | 0, w: g.w | 0, h: g.h | 0, lock: !!g.lock } } : { noFocus: true });
    } else {
      auraWinRemember(uid);
      if (typeof wmCloseTabId === 'function') wmCloseTabId(tid);
    }
  }
  function auraWinRemember(uid) {
    const a = auraGet(uid); if (!a) return;
    const w = (typeof wmWinOf === 'function') ? wmWinOf(auraTabId(uid)) : null;
    if (!w || !(w.w > 0 && w.h > 0)) return;
    a.winGeom = { x: w.x | 0, y: w.y | 0, w: w.w | 0, h: w.h | 0, lock: !!w.locked };
    auraCfgSave();
  }
  function auraMigrateWindow() {}
  // Auras with on-show / on-hide actions must be evaluated even when no aura window is open,
  // otherwise a sound configured on an aura whose window is closed would never play.
  function auraNeedsPoll() {
    const all = auraAll();
    for (const k in all) {
      const ac = all[k].actions;
      if ((ac.show.sound !== 'none' || ac.show.flash || ac.show.notify !== 'none') || (ac.hide.sound !== 'none' || ac.hide.flash || ac.hide.notify !== 'none')) return true;
      if (all[k].conditions.some(c => c.changes.some(ch => ch.prop === 'sound'))) return true;
    }
    return false;
  }

  // ---------------------------------------------------------------- data feeds
  let auraData = null, auraFetching = false, _aurasAt = 0;
  function auraNeeds() {
    const n = { buffs: false, abil: false, vital: false, info: false, scene: false, inv: false };
    const all = auraAll();
    for (const k in all) {
      const a = all[k];
      if (a.load && a.load.combat !== 'any') n.scene = true;
      a.triggers.list.forEach(t => {
        if (t.src === 'buff' || t.src === 'debuff') n.buffs = true;
        else if (t.src === 'ability') n.abil = true;
        else if (t.src === 'vital') n.vital = true;
        else if (t.src === 'panim') n.info = true;
        else if (t.src === 'nanim' || t.src === 'nearby') n.scene = true;
        else if (t.src === 'invslots' || t.src === 'invitem') n.inv = true;
      });
    }
    return n;
  }
  async function fetchAuras() {
    if (!bridge() || auraFetching) return;
    const now = Date.now();
    if (now - _aurasAt < 400) return;
    _aurasAt = now;
    auraFetching = true;
    const need = auraNeeds();
    try { await auraCatalogLoad(); } catch (e) {}
    try {
      if (bridge().buffs) {
        const d = JSON.parse(await rtxData.raw('state.buffs'));
        auraData = (d && Array.isArray(d.buffs)) ? d : { buffs: [], debuffs: [] };
        auraRemember(auraEffects().map(e => e.b.name));
      }
    } catch (e) {}
    try {
      const vis = id => typeof paneVisible === 'function' && paneVisible(id);
      if (need.abil && typeof fetchAbilities === 'function') await fetchAbilities();
      if (need.vital && typeof fetchPlayerVp === 'function') await fetchPlayerVp();
      if (need.info && typeof fetchInfo === 'function' && !vis('info')) await fetchInfo();
      if (need.scene && typeof fetchScene === 'function' && !vis('scene')) await fetchScene();
      if (need.inv && typeof fetchInv === 'function' && !vis('inventory')) await fetchInv();
    } catch (e) {}
    auraFetching = false;
    auraRepaintAll();
  }
  let _auraGeomAt = 0;
  function auraRepaintAll() {
    if (typeof paneRun !== 'function') return;
    try { auraTick(); } catch (e) {}
    // Remember placements of open aura windows every few seconds (covers closing via the X).
    const now = Date.now();
    if (now - _auraGeomAt > 3000) {
      _auraGeomAt = now;
      for (const a of auraRoots()) {
        const w = (typeof wmWinOf === 'function') ? wmWinOf(auraTabId(a.uid)) : null;
        if (w && w.w > 0 && w.h > 0) {
          const g = a.winGeom;
          if (!g || g.x !== (w.x | 0) || g.y !== (w.y | 0) || g.w !== (w.w | 0) || g.h !== (w.h | 0) || g.lock !== !!w.locked) { a.winGeom = { x: w.x | 0, y: w.y | 0, w: w.w | 0, h: w.h | 0, lock: !!w.locked }; auraCfgSave(); }
        }
      }
    }
    paneRun('auras', renderAuras);
    for (const a of auraRoots()) paneRun(auraTabId(a.uid), () => renderAuraGroup(a.uid));
  }
  function auraEdRepaint() { auraRepaintAll(); }
  function auraEffects() {
    if (!auraData) return [];
    const out = [];
    (auraData.buffs || []).forEach(b => out.push({ b: b, debuff: false }));
    (auraData.debuffs || []).forEach(b => out.push({ b: b, debuff: true }));
    return out;
  }
  function auraSeenLoad() {
    if (auraSeen) return auraSeen;
    auraSeen = [];
    try { const v = JSON.parse(prefGet('rtxAuraSeen', '[]')); if (Array.isArray(v)) auraSeen = v.filter(x => typeof x === 'string').slice(0, 400); } catch (e) {}
    return auraSeen;
  }
  function auraRemember(names) {
    const seen = auraSeenLoad(); let changed = false;
    for (const n of names) { if (n && seen.indexOf(n) < 0) { seen.push(n); changed = true; } }
    if (changed) { seen.sort((a, b) => a.localeCompare(b)); try { prefSet('rtxAuraSeen', JSON.stringify(seen.slice(0, 400))); } catch (e) {} }
  }
  function auraSecsOf(b) {
    if (typeof b.secs === 'number' && b.secs > 0) return b.secs;
    const t = String(b.timer || '').trim(); if (!t) return 0;
    const m = t.match(/^(\d+):(\d{1,2})$/); if (m) return (+m[1]) * 60 + (+m[2]);
    const n = parseFloat(t); return isFinite(n) ? n : 0;
  }
  function auraCountOf(b) {
    const m = String(b.timer || '').trim().match(/(-?\d+(?:\.\d+)?)\s*([KkMm])?/);
    if (!m) return null;
    let v = parseFloat(m[1]);
    if (m[2] === 'K' || m[2] === 'k') v *= 1000; else if (m[2] === 'M') v *= 1000000;
    return Math.round(v);
  }
  function auraLabel(n) { return String(n).replace(/\s+is on cooldown\.?$/i, '').replace(/\.$/, '').trim() || n; }
  function auraAbilitySlots() {
    const d = (typeof abarData !== 'undefined') ? abarData : null;
    if (!d || !Array.isArray(d.bars)) return [];
    const out = [], seen = {};
    d.bars.forEach(b => (b.slots || []).forEach(sl => { if (sl.name && !seen[sl.id]) { seen[sl.id] = 1; out.push(sl); } }));
    return out;
  }
  function auraAbilityFind(name) {
    const slots = auraAbilitySlots();
    let sl = slots.find(x => x.name === name);
    if (!sl && name) { const t = String(name).toLowerCase(); sl = slots.find(x => (x.name || '').toLowerCase().indexOf(t) >= 0); }
    return sl || null;
  }
  function auraAbilityCd(sl) {
    if (!sl) return 0;
    if (sl.cd) { const t = String(sl.cd).trim(); const m = t.match(/^(\d+):(\d{1,2})$/); if (m) return (+m[1]) * 60 + (+m[2]); const n = parseFloat(t); if (isFinite(n)) return n; }
    try { if (typeof abCdLive === 'function') { const v = abCdLive(sl); if (v != null) return v; } } catch (e) {}
    return 0;
  }
  function auraAbilityCdMax(sl) {
    try { if (typeof abCdMap === 'function') { const m = abCdMap(); const r = m && sl && m[sl.id]; if (r && r.c) return r.c * 0.6; } } catch (e) {}
    return 0;
  }
  function auraVital(stat) {
    const vp = (typeof playerVp !== 'undefined') ? playerVp : null;
    if (!vp) return null;
    const u = k => (vp[k] || 0) >>> 0;
    if (stat === 'hp')     return vp['13537'] === undefined ? null : { cur: u('13537'), max: vp['13538'] !== undefined ? u('13538') : 0 };
    if (stat === 'prayer') { const v = u('3274'); const mx = ((v >>> 16) & 0x7f) * 10; return mx ? { cur: Math.floor((v & 0x7fff) / 10), max: mx } : null; }
    if (stat === 'summon') { const v = u('8040'); const mx = ((v >>> 16) & 0x7f) * 10; return mx ? { cur: Math.floor((v & 0x7fff) / 10), max: mx } : null; }
    if (stat === 'adren')  return vp['679'] === undefined ? null : { cur: Math.floor(u('679') / 10), max: 100 };
    return null;
  }
  function auraSelf() {
    const sd = (typeof sceneData !== 'undefined') ? sceneData : null;
    if (!sd || !Array.isArray(sd.players)) return null;
    return sd.players.find(p => p.self) || null;
  }
  function auraInCombat() { const s = auraSelf(); return s ? (typeof s.combat === 'number' && s.combat > 0) : null; }

  // ---------------------------------------------------------------- trigger evaluation
  const auraMaxSeen = {};   // key -> longest remaining seen (the game reports time LEFT only)
  function auraCmp(v, op, n) {
    switch (op) { case '<': return v < n; case '<=': return v <= n; case '=': return v === n; case '>=': return v >= n; case '>': return v > n; case '!=': return v !== n; default: return true; }
  }
  // Returns an array of states (clones); empty = not triggered.
  function auraTrigStates(t, a, ti) {
    const out = [];
    const key = a.uid + ':' + ti;
    if (t.src === 'always') { out.push({ show: true, progressType: 'none', name: a.name, icon: null, stacks: 0 }); return out; }
    if (t.src === 'buff' || t.src === 'debuff') {
      const want = t.src === 'debuff';
      const q = String(t.name || '').toLowerCase().trim();
      const all = auraEffects().filter(e => e.debuff === want);
      let hits = [];
      if (q) { const ex = all.filter(e => e.b.name === t.name); hits = ex.length ? ex : all.filter(e => (e.b.name || '').toLowerCase().indexOf(q) >= 0); }
      else if (t.clones) hits = all;   // empty name + clones = every effect on that side
      if (t.op === 'inactive') { if (!hits.length) out.push({ show: true, progressType: 'none', name: t.name, icon: null, stacks: 0 }); return out; }
      hits.slice(0, t.clones ? AURA_MAX_CLONES : 1).forEach((e, i) => {
        const secs = auraSecsOf(e.b), cnt = auraCountOf(e.b);
        const k = key + ':' + e.b.name;
        if (secs > 0) { if (secs > (auraMaxSeen[k] || 0)) auraMaxSeen[k] = secs; } else delete auraMaxSeen[k];
        const val = t.field === 'stacks' ? (cnt == null ? 0 : cnt) : secs;
        if (t.op !== 'active' && t.op !== 'any' && !auraCmp(val, t.op, t.val || 0)) return;
        const timed = secs > 0;
        out.push({ show: true, progressType: timed ? 'timed' : (cnt != null ? 'static' : 'none'),
                   remaining: secs, duration: auraMaxSeen[k] || secs, value: cnt == null ? secs : cnt, total: cnt == null ? (auraMaxSeen[k] || secs) : Math.max(cnt, 1),
                   name: e.b.name, icon: { sprite: e.b.sprite || 0, item: e.b.item || 0 }, stacks: cnt == null ? 0 : cnt, cloneId: e.b.name });
      });
      return out;
    }
    if (t.src === 'ability') {
      const sl = auraAbilityFind(t.name);
      const cd = auraAbilityCd(sl);
      const on = cd > 0;
      if (t.op === 'inactive' && on) return out;
      if (t.op === 'active' && !on) return out;
      if (t.op !== 'active' && t.op !== 'inactive' && t.op !== 'any' && !auraCmp(cd, t.op, t.val || 0)) return out;
      const cmax = Math.max(auraAbilityCdMax(sl), on ? (auraMaxSeen[key] || 0) : 0, cd);
      if (on) auraMaxSeen[key] = cmax; else delete auraMaxSeen[key];
      out.push({ show: true, progressType: on ? 'timed' : 'none', remaining: cd, duration: cmax, value: cd, total: cmax,
                 name: sl ? sl.name : t.name, icon: sl ? { sprite: sl.id || 0, item: sl.item || 0 } : null, stacks: 0, ready: !on });
      return out;
    }
    if (t.src === 'vital') {
      const v = auraVital(t.stat);
      if (!v) return out;
      const pct = v.max > 0 ? Math.round(v.cur / v.max * 100) : 0;
      if (t.op !== 'any' && t.op !== 'active' && !auraCmp(t.field === 'stacks' ? pct : v.cur, t.op, t.val || 0)) return out;
      const vl = AURA_VITALS.find(x => x[0] === t.stat);
      out.push({ show: true, progressType: 'static', value: v.cur, total: v.max || 100, remaining: 0, duration: 0, name: vl ? vl[1] : t.stat, icon: null, stacks: 0, pct: pct });
      return out;
    }
    if (t.src === 'panim') {
      const info = (typeof infoData !== 'undefined') ? infoData : null;
      if (info && info.in && info.anim === t.anim) out.push({ show: true, progressType: 'none', name: 'Anim ' + t.anim, icon: null, stacks: 0 });
      return out;
    }
    if (t.src === 'nanim' || t.src === 'nearby') {
      const sd = (typeof sceneData !== 'undefined') ? sceneData : null;
      if (!sd) return out;
      let within = () => true;
      try { if (typeof sceneSelfPos === 'function') { const p = sceneSelfPos(); const r = (typeof sceneRange === 'number') ? sceneRange : 64; if (p && p[0] != null) within = (x, y) => Math.max(Math.abs(x - p[0]), Math.abs(y - p[1])) <= r; } } catch (e) {}
      if (t.src === 'nanim') {
        const hits = (sd.npcs || []).filter(n => n.anim === t.anim && within(n.x, n.y));
        hits.slice(0, t.clones ? AURA_MAX_CLONES : 1).forEach((n, i) => out.push({ show: true, progressType: 'none', name: n.name || ('Anim ' + t.anim), icon: null, stacks: hits.length, cloneId: n.uid != null ? n.uid : i }));
        return out;
      }
      const q = String(t.name || '').toLowerCase().trim(); if (!q) return out;
      const list = t.kind === 'player' ? sd.players : t.kind === 'object' ? sd.objects : sd.npcs;
      const hits = (Array.isArray(list) ? list : []).filter(e => !e.self && (e.name || '').toLowerCase().indexOf(q) >= 0 && (t.kind === 'object' || within(e.x, e.y)));
      if (t.op === 'inactive') { if (!hits.length) out.push({ show: true, progressType: 'none', name: t.name, icon: null, stacks: 0 }); return out; }
      hits.slice(0, t.clones ? AURA_MAX_CLONES : 1).forEach((e, i) => out.push({ show: true, progressType: 'none', name: e.name, icon: null, stacks: hits.length, cloneId: e.uid != null ? e.uid : i }));
      return out;
    }
    if (t.src === 'invslots' || t.src === 'invitem') {
      if (typeof invPresent !== 'function' || !invPresent()) return out;
      if (t.src === 'invslots') {
        const used = invUsed(), cap = invCapacity();
        const val = t.field === 'stacks' ? (cap - used) : used;
        if (t.op !== 'any' && t.op !== 'active' && !auraCmp(val, t.op, t.val || 0)) return out;
        out.push({ show: true, progressType: 'static', value: used, total: cap, name: 'Inventory', icon: null, stacks: cap - used });
        return out;
      }
      const q = String(t.name || '').toLowerCase().trim(); if (!q) return out;
      let qty = 0, slots = 0, first = null;
      for (const it of invItems()) { if ((it[3] || '').toLowerCase().indexOf(q) >= 0) { qty += (it[2] > 0 ? it[2] : 1); slots++; if (!first) first = it; } }
      if (t.op === 'inactive') { if (!slots) out.push({ show: true, progressType: 'none', name: t.name, icon: null, stacks: 0 }); return out; }
      if (!slots) return out;
      if (t.op !== 'any' && t.op !== 'active' && !auraCmp(qty, t.op, t.val || 0)) return out;
      out.push({ show: true, progressType: 'static', value: qty, total: Math.max(qty, 1), name: first ? first[3] : t.name, icon: first ? { item: first[1] || first[0] || 0, sprite: 0 } : null, stacks: qty });
      return out;
    }
    return out;
  }
  // Evaluate an aura: { show, states (clones of the active trigger), per: [states per trigger], act }
  function auraEval(a) {
    const per = a.triggers.list.map((t, i) => { try { return auraTrigStates(t, a, i); } catch (e) { return []; } });
    const n = per.length;
    const onCount = per.filter(s => s.length > 0).length;
    let show = n === 0 ? true : (a.triggers.logic === 'all' ? onCount === n : onCount > 0);
    let loaded = true;
    if (a.load && a.load.combat !== 'any') {
      const ic = auraInCombat();
      if (ic !== null && ((a.load.combat === 'in') !== ic)) { show = false; loaded = false; }
    }
    let act = -1;
    if (a.triggers.active >= 0 && a.triggers.active < n && per[a.triggers.active].length) act = a.triggers.active;
    else act = per.findIndex(s => s.length > 0);
    const states = act >= 0 ? per[act] : [];
    return { show, loaded, states, per, act };
  }

  // ---------------------------------------------------------------- conditions
  function auraCondVar(st, v) {
    if (!st) return null;
    switch (v) {
      case 'show': return true;
      case 'remaining': return st.progressType === 'timed' ? (st.remaining || 0) : null;
      case 'duration': return st.progressType === 'timed' ? (st.duration || 0) : null;
      case 'value': return st.value == null ? null : st.value;
      case 'stacks': return st.stacks || 0;
      case 'pct': return st.pct != null ? st.pct : (st.total > 0 && st.value != null ? Math.round(st.value / st.total * 100) : (st.duration > 0 ? Math.round((st.remaining || 0) / st.duration * 100) : null));
      case 'name': return st.name || '';
    }
    return null;
  }
  // Active overrides for a clone: {color, alpha, glow, desat, hide, text, scale, _sound:[ci]}
  function auraCondApply(a, ev, st) {
    const o = {};
    a.conditions.forEach((c, ci) => {
      if (!c.checks.length) return;
      const ok = c.checks.every(ch => {
        const s = ch.trig < 0 ? st : (ev.per[ch.trig] && ev.per[ch.trig][0]) || null;
        if (ch.v === 'show') { const want = !(ch.val === false || ch.val === 0 || ch.val === 'false' || ch.val === '0'); return (!!s) === want; }
        const cur = auraCondVar(s, ch.v);
        if (cur == null) return false;
        if (ch.v === 'name') { const q = String(ch.val).toLowerCase(), m = String(cur).toLowerCase().indexOf(q) >= 0; return ch.op === '!=' ? !m : m; }
        const n = parseFloat(ch.val); if (!isFinite(n)) return false;
        return auraCmp(cur, ch.op, n);
      });
      if (!ok) return;
      c.changes.forEach(ch => {
        if (ch.prop === 'sound') { o._sound = o._sound || []; o._sound.push(ci); return; }
        o[ch.prop] = ch.val;
      });
    });
    return o;
  }

  // ---------------------------------------------------------------- text tokens
  // Seconds under a minute, m:ss under ten minutes, then whole minutes (79m) and hours (2h).
  function auraFmtTime(s) {
    s = Math.max(0, s);
    if (s < 60) return String(Math.round(s));
    if (s < 600) { const m = Math.floor(s / 60), r = Math.round(s % 60); return r === 60 ? (m + 1) + ':00' : m + ':' + (r < 10 ? '0' : '') + r; }
    if (s < 5400) return Math.round(s / 60) + 'm';
    const h = Math.floor(s / 3600), mm = Math.round((s % 3600) / 60);
    return mm ? h + 'h' + (mm < 10 ? '0' : '') + mm : h + 'h';
  }
  // Short form for numbers drawn ON a tile, where there is no room for thousands separators.
  function auraFmtShort(v) {
    if (typeof v !== 'number') return String(v == null ? '' : v);
    const ab = Math.abs(v);
    if (ab >= 1000000) return (v / 1000000).toFixed(ab >= 10000000 ? 0 : 1).replace(/\.0$/, '') + 'M';
    if (ab >= 10000) return (v / 1000).toFixed(ab >= 100000 ? 0 : 1).replace(/\.0$/, '') + 'K';
    return String(v);
  }
  // Shrink tile text so a long string never runs off the tile: ~0.6em per glyph at 900 weight.
  function auraFitText(b, size, txt) {
    const n = Math.max(1, String(txt || '').length);
    const fit = (size * 0.95) / (n * 0.6);
    b.style.fontSize = Math.max(9, Math.min(size * 0.42, fit)).toFixed(1) + 'px';
  }
  function auraFmtNum(v) { return (typeof v === 'number') ? (Math.abs(v) >= 10000 ? v.toLocaleString() : String(v)) : String(v == null ? '' : v); }
  function auraToken(st, sym, a) {
    switch (sym) {
      case 'p': if (!st) return ''; return st.progressType === 'timed' ? auraFmtTime(st.remaining || 0) : (st.value != null ? auraFmtNum(st.value) : '');
      case 't': if (!st) return ''; return st.progressType === 'timed' ? auraFmtTime(st.duration || 0) : (st.total != null ? auraFmtNum(st.total) : '');
      case 'n': return st ? (st.name || a.name || '') : (a.name || '');
      case 's': return st && st.stacks ? String(st.stacks) : '';
      case 'v': return st && st.value != null ? auraFmtNum(st.value) : '';
      case 'r': return st ? String(Math.round(st.remaining || 0)) : '';
      case 'c': { const v = auraCondVar(st, 'pct'); return v == null ? '' : v + '%'; }
    }
    return '';
  }
  function auraText(str, a, ev, st) {
    return String(str == null ? '' : str).replace(/%%|%(\d+)\.([a-z])|%([a-z])/g, (m, ti, tsym, sym) => {
      if (m === '%%') return '%';
      if (ti !== undefined) { const s = ev.per[(+ti) - 1] && ev.per[(+ti) - 1][0]; return auraToken(s || null, tsym, a); }
      return auraToken(st, sym, a);
    });
  }

  // ---------------------------------------------------------------- icons
  function auraSetIcon(el, url) { el.style.backgroundImage = "url('" + url + "')"; el.style.backgroundSize = 'contain'; }
  // name -> {sprite, item}: remembered per install, so a placeholder can show the art of a buff
  // that is not up right now (and never was this session).
  let auraIconMemo = null;
  function auraIconMemoLoad() {
    if (auraIconMemo) return auraIconMemo;
    auraIconMemo = {};
    try { const v = JSON.parse(prefGet('rtxAuraIcons', '{}')); if (v && typeof v === 'object') auraIconMemo = v; } catch (e) {}
    return auraIconMemo;
  }
  let _iconMemoT = 0;
  function auraIconRemember(name, icon) {
    if (!name || !icon || (!icon.sprite && !icon.item)) return;
    const m = auraIconMemoLoad();
    const cur = m[name];
    if (cur && cur.sprite === (icon.sprite || 0) && cur.item === (icon.item || 0)) return;
    m[name] = { sprite: icon.sprite || 0, item: icon.item || 0 };
    if (_iconMemoT) return;
    _iconMemoT = setTimeout(() => { _iconMemoT = 0; try { const keys = Object.keys(m); if (keys.length > 600) keys.slice(0, keys.length - 600).forEach(k => delete m[k]); prefSet('rtxAuraIcons', JSON.stringify(m)); } catch (e) {} }, 1500);
  }
  // The cache's own catalogue of buff/debuff names and icons (rtx.buffCatalog) and ability
  // configs (name -> ability id, which is also its sprite id) seed the icon memory and the name
  // lists, so placeholders and pickers know every effect from the start, not only ones seen.
  let auraCatalog = null, auraCatalogTried = 0;
  async function auraCatalogLoad() {
    if (auraCatalog || !bridge()) return;
    const now = Date.now(); if (now - auraCatalogTried < 5000) return; auraCatalogTried = now;
    let cat = null;
    try { if (bridge().buffCatalog) { const d = JSON.parse((await rtxData.raw('cache.buffCatalog')) || '{}'); if (d && (Array.isArray(d.buffs) || Array.isArray(d.debuffs))) cat = d; } } catch (e) {}
    if (!cat) return;                                   // cache not open yet, retry later
    auraCatalog = { buffs: {}, debuffs: {} };
    const m = auraIconMemoLoad(); const names = [];
    const take = (rows, side) => (rows || []).forEach(r => {
      if (!Array.isArray(r) || typeof r[0] !== 'string' || !r[0]) return;
      const ic = r[2] ? { sprite: 0, item: r[1] | 0 } : { sprite: r[1] | 0, item: 0 };
      auraCatalog[side][r[0]] = ic;
      if (!m[r[0]]) m[r[0]] = ic;
      names.push(r[0]);
    });
    take(cat.buffs, 'buffs'); take(cat.debuffs, 'debuffs');
    auraRemember(names);
    try { prefSet('rtxAuraIcons', JSON.stringify(m)); } catch (e) {}
    auraInvalidate();
  }
  function auraAbilityCfgIcon(name) {
    try { if (typeof abCfgLoad === 'function') abCfgLoad(); } catch (e) {}
    const cfg = (typeof abCfg !== 'undefined') ? abCfg : null;
    if (!cfg || !name) return null;
    let c = cfg[name];
    if (!c) { const q = name.toLowerCase(); for (const k in cfg) if (k.toLowerCase().indexOf(q) >= 0) { c = cfg[k]; break; } }
    return (c && c.i) ? { sprite: c.i | 0, item: 0 } : null;
  }
  // Best icon for an idle aura: the action-bar slot or the cache's ability id for abilities; the
  // remembered / catalogued icon for buffs and debuffs.
  function auraIdleIcon(a) {
    const t = a.triggers.list[0];
    if (!t) return null;
    if (t.src === 'ability') { const sl = auraAbilityFind(t.name); if (sl) return { sprite: sl.id || 0, item: sl.item || 0 }; return auraAbilityCfgIcon(t.name); }
    const m = auraIconMemoLoad();
    if (t.name && m[t.name]) return m[t.name];
    if (t.name) { const q = t.name.toLowerCase(); for (const k in m) if (k.toLowerCase().indexOf(q) >= 0) return m[k]; }
    return null;
  }
  function auraIconFor(el, icon, name) {
    if (icon && name) auraIconRemember(name, icon);
    if (!icon) { const k = name && auraIconByName[name]; if (k) auraSetIcon(el, k); else el.style.backgroundImage = ''; return; }
    if (icon.item) { const url = resolveIcon(icon.item); if (url) { auraSetIcon(el, url); if (name) auraIconByName[name] = url; return; } }
    if (!icon.sprite) { el.style.backgroundImage = ''; return; }
    const sid = icon.sprite, cached = SPRITES.get(sid);
    if (cached) { auraSetIcon(el, cached); if (name) auraIconByName[name] = cached; return; }
    el.dataset.spr = sid;
    if (!bridge() || !bridge().sprite || SPRITE_PENDING.has(sid)) return;
    SPRITE_PENDING.add(sid);
    (async () => {
      try {
        const url = await rtxData.raw('cache.sprite', sid); SPRITE_PENDING.delete(sid);
        if (url) { SPRITES.set(sid, url); if (name) auraIconByName[name] = url; document.querySelectorAll('.au-icon[data-spr="' + sid + '"]').forEach(n => auraSetIcon(n, url)); }
      } catch (e) { SPRITE_PENDING.delete(sid); }
    })();
  }

  // ---------------------------------------------------------------- actions + edge tracking
  const auraShownPrev = {};
  const auraCondSoundPrev = {};
  function auraDeliver(act, a, ev, st) {
    if (!act) return;
    // Honour the Alerts panel's "only when tabbed out" switch.
    try { if (typeof alertsSuppressed === 'function' && alertsSuppressed()) return; } catch (e) {}
    if (act.sound && act.sound !== 'none') { try { bridge().playSound(act.sound); } catch (e) {} }
    if (act.flash) { try { rtxData.sync('overlay.flashGame'); } catch (e) {} }
    const msg = act.msg ? auraText(act.msg, a, ev, st) : (a.name || 'Aura');
    if (act.notify === 'ingame' || act.notify === 'both') { try { uiNotify(msg, { ttl: 5000 }); } catch (e) {} }
    if (act.notify === 'windows' || act.notify === 'both') { try { if (typeof winNotify === 'function') winNotify(msg); } catch (e) {} }
  }
  // One evaluation pass over EVERY aura (not only visible windows) so actions fire even for a
  // root whose window is closed. Results are cached per tick for the renderers.
  let auraEvalCache = {};
  function auraTick() {
    auraEvalCache = {};
    const all = auraAll();
    for (const uid in all) {
      const a = all[uid];
      const ev = auraEval(a);
      auraEvalCache[uid] = ev;
      const shown = ev.show && (ev.states.length > 0 || a.triggers.list.length === 0);
      const was = !!auraShownPrev[uid];
      if (auraShownPrev[uid] === undefined) { auraShownPrev[uid] = shown; continue; }
      if (shown && !was) auraDeliver(a.actions.show, a, ev, ev.states[0] || null);
      if (!shown && was) auraDeliver(a.actions.hide, a, ev, null);
      auraShownPrev[uid] = shown;
      if (shown && ev.states.length) {
        const o = auraCondApply(a, ev, ev.states[0]);
        const cur = (o._sound || []).join(',');
        const prev = auraCondSoundPrev[uid] || '';
        if (cur && cur !== prev) {
          (o._sound || []).forEach(ci => { if (prev.split(',').indexOf(String(ci)) < 0) { const ch = a.conditions[ci].changes.find(x => x.prop === 'sound'); if (ch && ch.val && ch.val !== 'none' && !(typeof alertsSuppressed === 'function' && alertsSuppressed())) { try { bridge().playSound(ch.val); } catch (e) {} } } });
        }
        auraCondSoundPrev[uid] = cur;
      } else auraCondSoundPrev[uid] = '';
    }
  }

  // ---------------------------------------------------------------- rendering
  // Keyed reconcile: one element per visible (aura, clone), updated in place so the CSS
  // animations (start / main / finish) behave.
  function auraReconcile(host, items, make, update, finishClassFor) {
    const keep = {};
    items.forEach(it => { keep[it.key] = it; });
    Array.from(host.children).forEach(el => {
      const k = el.dataset.key;
      if (k === undefined) return;            // hints etc.
      if (keep[k] && !el.dataset.finishing) return;
      if (el.dataset.finishing) return;
      const fin = finishClassFor ? finishClassFor(el) : '';
      if (fin) { el.dataset.finishing = '1'; el.classList.add('au-fin-' + fin); el.style.pointerEvents = 'none'; setTimeout(() => { try { el.remove(); } catch (e) {} }, 420); }
      else el.remove();
    });
    let prev = null;
    items.forEach(it => {
      let el = Array.from(host.children).find(x => x.dataset.key === it.key && !x.dataset.finishing);
      if (!el) { el = make(it); el.dataset.key = it.key; host.appendChild(el); }
      update(el, it);
      const want = prev ? prev.nextSibling : host.firstChild;
      if (want !== el) host.insertBefore(el, want);
      prev = el;
    });
  }
  function auraApplyCommon(el, a, ov) {
    const alpha = ov.alpha != null ? +ov.alpha : a.alpha;
    el.style.opacity = String(isFinite(alpha) ? Math.max(0, Math.min(1, alpha)) : 1);
    el.classList.toggle('au-glow', ov.glow != null ? !!ov.glow : !!a.glow);
    el.classList.toggle('au-desat', ov.desat != null ? !!ov.desat : !!a.desat);
    const sc = ov.scale != null ? +ov.scale : 1;
    el.style.transform = (isFinite(sc) && sc !== 1) ? 'scale(' + sc + ')' : '';
    AURA_ANIM_MAIN.forEach(m => { if (m[0] !== 'none') el.classList.remove('au-m-' + m[0]); });
    if (a.anim.main !== 'none') el.classList.add('au-m-' + a.anim.main);
  }
  function auraSweepInto(el, frac) {
    let sw = auraChild(el, 'au-sweep');
    if (frac <= 0) { if (sw) sw.remove(); return; }
    if (!sw) { sw = document.createElement('div'); sw.className = 'au-sweep'; sw.innerHTML = '<div class="au-hh au-hr"><i></i></div><div class="au-hh au-hl"><i></i></div>'; el.appendChild(sw); }
    const deg = frac * 360;
    sw.querySelector('.au-hr i').style.transform = 'rotate(' + Math.min(deg, 180).toFixed(2) + 'deg)';
    const hl = sw.querySelector('.au-hl'); hl.style.display = deg > 180 ? '' : 'none';
    hl.querySelector('i').style.transform = 'rotate(' + deg.toFixed(2) + 'deg)';
  }
  function auraFracOf(st, inverse) {
    let frac = 0;
    if (st) { if (st.progressType === 'timed' && st.duration > 0) frac = st.remaining / st.duration; else if (st.progressType === 'static' && st.total > 0) frac = st.value / st.total; else frac = 1; }
    frac = Math.max(0, Math.min(1, frac));
    return inverse ? 1 - frac : frac;
  }
  function auraChild(el, cls) { for (let i = 0; i < el.children.length; i++) if (el.children[i].classList.contains(cls)) return el.children[i]; return null; }
  function auraSub(el, cls, html) {
    let n = auraChild(el, cls);
    if (!n) { n = document.createElement('div'); n.className = cls; if (html) n.innerHTML = html; el.appendChild(n); }
    return n;
  }
  // Build / update the visual for one (aura, state) pair inside `el` (already sized by the caller).
  function auraPaint(el, a, ev, st, ov) {
    const color = ov.color != null ? String(ov.color) : null;
    const textOv = ov.text != null ? String(ov.text) : null;
    const trigSrc = a.triggers.list[ev.act] ? a.triggers.list[ev.act].src : '';
    const fallbackName = a.triggers.list[0] ? a.triggers.list[0].name : a.name;
    el.classList.toggle('au-off', !st);
    if (a.type === 'icon') {
      const ic = auraSub(el, 'au-icon');
      auraIconFor(ic, st ? st.icon : auraIdleIcon(a), st ? st.name : fallbackName);
      const tint = color || (a.color && a.color !== '#ffffff' ? a.color : '');
      el.style.borderColor = tint || '';
      el.style.boxShadow = tint ? ('0 0 0 1px ' + tint + ', 0 2px 8px rgba(0,0,0,0.85)') : '';
      el.classList.toggle('au-ready', !!(st && st.ready));
      el.classList.toggle('au-oncd', !!(st && st.progressType === 'timed' && trigSrc === 'ability'));
      auraSweepInto(el, (a.sweep && st && st.progressType === 'timed' && st.duration > 0) ? Math.max(0, Math.min(1, st.remaining / st.duration)) : 0);
      const tm = auraSub(el, 'au-time', '<b></b>');
      let txt = '';
      if (textOv != null) txt = auraText(textOv, a, ev, st);
      else if (st && a.showTime && st.progressType === 'timed' && st.remaining > 0) txt = auraFmtTime(st.remaining);
      else if (st && a.showStacks && st.progressType === 'static' && st.value != null) txt = auraFmtShort(st.value);
      tm.firstChild.textContent = txt;
      auraFitText(tm.firstChild, Math.min(parseFloat(el.style.width) || 56, parseFloat(el.style.height) || 56), txt);
      tm.classList.toggle('au-soon', !!(st && st.progressType === 'timed' && st.remaining > 0 && st.remaining <= 5 && textOv == null));
      const sk = auraSub(el, 'au-stk');
      sk.textContent = (st && a.showStacks && st.progressType === 'timed' && st.stacks > 0) ? String(st.stacks) : '';
      return;
    }
    if (a.type === 'bar') {
      const bc = color || a.barColor;
      const pct = (auraFracOf(st, a.inverse) * 100).toFixed(1) + '%';
      el.style.backgroundColor = a.bgColor;
      el.style.backgroundImage = 'linear-gradient(' + (a.orient === 'v' ? '0deg' : '90deg') + ', ' + bc + ' 0%, ' + bc + ' ' + pct + ', rgba(0,0,0,0) ' + pct + ', rgba(0,0,0,0) 100%)';
      const tx = auraSub(el, 'au-vtext', '<span></span><b></b>');
      tx.style.color = a.textColor;
      tx.firstChild.textContent = textOv != null ? auraText(textOv, a, ev, st) : auraText(a.textL, a, ev, st);
      tx.lastChild.textContent = textOv != null ? '' : auraText(a.textR, a, ev, st);
      return;
    }
    if (a.type === 'text') {
      el.style.color = color || a.textColor;
      el.style.fontSize = a.fontSize + 'px';
      el.style.justifyContent = a.align === 'left' ? 'flex-start' : a.align === 'right' ? 'flex-end' : 'center';
      el.textContent = auraText(textOv != null ? textOv : a.text, a, ev, st);
      return;
    }
    if (a.type === 'ring') {
      const rc = color || a.ringColor;
      const ring = auraSub(el, 'au-ring', '<div class="au-hh au-hr"><i></i></div><div class="au-hh au-hl"><i></i></div><div class="au-ring-in"></div>');
      ring.style.setProperty('--rc', rc); ring.style.setProperty('--rw', a.ringWidth + 'px');
      const deg = auraFracOf(st, a.inverse) * 360;
      ring.querySelector('.au-hr i').style.transform = 'rotate(' + Math.min(deg, 180).toFixed(2) + 'deg)';
      const hl = ring.querySelector('.au-hl'); hl.style.display = deg > 180 ? '' : 'none';
      hl.querySelector('i').style.transform = 'rotate(' + deg.toFixed(2) + 'deg)';
      const tm = auraSub(el, 'au-time', '<b></b>');
      const rt = auraText(textOv != null ? textOv : a.text, a, ev, st);
      tm.firstChild.textContent = rt;
      tm.firstChild.style.color = a.textColor;
      auraFitText(tm.firstChild, (Math.min(parseFloat(el.style.width) || 56, parseFloat(el.style.height) || 56) - 2 * a.ringWidth) * 0.75, rt);
      return;
    }
    if (a.type === 'pips') {
      const ic = auraSub(el, 'au-icon');
      auraIconFor(ic, st ? st.icon : auraIdleIcon(a), st ? st.name : fallbackName);
      const cnt = st ? Math.round(st.stacks || st.value || 0) : 0;
      const tm = auraSub(el, 'au-time', '<b></b>');
      const pt = textOv != null ? auraText(textOv, a, ev, st) : (st ? String(cnt) : '');
      tm.firstChild.textContent = pt;
      auraFitText(tm.firstChild, Math.min(parseFloat(el.style.width) || 56, parseFloat(el.style.height) || 56), pt);
      tm.classList.toggle('au-full', cnt >= a.max);
      const pp = auraSub(el, 'au-pips');
      const mx = Math.max(1, a.max); const perRow = mx > 10 ? Math.ceil(mx / 2) : mx;
      pp.style.gridTemplateColumns = 'repeat(' + perRow + ', 1fr)';
      pp.style.setProperty('--pip', color || a.pipColor);
      if (pp.childNodes.length !== mx) { pp.textContent = ''; for (let i = 0; i < mx; i++) pp.appendChild(document.createElement('i')); }
      Array.from(pp.children).forEach((p, i) => { p.className = i < cnt ? 'on' : ''; });
      pp.classList.toggle('au-full', cnt >= mx);
      return;
    }
  }
  // Items to draw for an aura (each = one clone); [] when hidden.
  function auraItems(a, sizeW, sizeH, parentIsDyn) {
    const ev = auraEvalCache[a.uid] || auraEval(a);
    if (!ev.loaded) return [];                       // a Load rule hides it outright
    if (!ev.show || !ev.states.length) {
      // Not triggered: either gone, or a dimmed placeholder so the slot stays visible.
      if (a.hideIdle && a.triggers.list.length) return [];
      const ov = auraCondApply(a, ev, null);
      if (ov.hide) return [];
      return [{ key: a.uid, a, ev, st: null, ov, w: sizeW, h: sizeH }];
    }
    const list = parentIsDyn ? ev.states : ev.states.slice(0, 1);
    const out = [];
    list.forEach((st, i) => {
      const ov = auraCondApply(a, ev, st);
      if (ov.hide) return;
      out.push({ key: a.uid + (parentIsDyn ? '#' + (st.cloneId != null ? st.cloneId : i) : ''), a, ev, st, ov, w: sizeW, h: sizeH });
    });
    return out;
  }
  function auraMakeEl(it) {
    const el = document.createElement('div');
    el.className = 'au-el au-t-' + it.a.type + (it.a.anim.start !== 'none' ? ' au-s-' + it.a.anim.start : '');
    el.dataset.uid = it.a.uid; el.dataset.type = it.a.type;
    return el;
  }
  function auraUpdateEl(el, it, edit) {
    // Type changed under a reused element (same uid): drop the previous type's children,
    // classes and inline styles, otherwise the new look paints over the old one.
    if (el.dataset.type !== it.a.type) {
      el.textContent = '';
      el.removeAttribute('style');
      el.className = 'au-el au-t-' + it.a.type;
      el.dataset.type = it.a.type;
    }
    el.style.width = it.w + 'px'; el.style.height = it.h + 'px';
    el.style.setProperty('--au-size', Math.min(it.w, it.h) + 'px');
    auraApplyCommon(el, it.a, it.ov);
    auraPaint(el, it.a, it.ev, it.st, it.ov);
    el.classList.toggle('au-sel', !!(edit && auraEdSel === it.a.uid));
    el.classList.toggle('au-editable', !!(edit && it.a.parent && auraGet(it.a.parent) && auraGet(it.a.parent).type === 'group'));
    el.title = edit ? (it.a.name || it.a.type) : '';
  }
  function auraFinishClass(el) {
    const a = auraGet(el.dataset.uid);
    return (a && a.anim.finish !== 'none') ? a.anim.finish : '';
  }
  // Children of a static group: absolute boxes. Children of a dynamic group: flow.
  function auraRenderChildrenInto(host, grp, edit) {
    const dyn = grp.type === 'dyngroup';
    host.classList.toggle('au-dyn', dyn); host.classList.toggle('au-static', !dyn);
    if (dyn) {
      host.style.display = 'flex';
      host.style.gap = grp.space + 'px';
      host.style.flexDirection = grp.grow === 'left' ? 'row-reverse' : grp.grow === 'down' ? 'column' : grp.grow === 'up' ? 'column-reverse' : 'row';
      host.style.flexWrap = grp.grow === 'grid' ? 'wrap' : 'nowrap';
      host.style.alignContent = 'flex-start';
      host.style.maxWidth = grp.grow === 'grid' ? (grp.gridCols * (grp.childW + grp.space)) + 'px' : '';
    } else { host.style.display = 'block'; host.style.maxWidth = ''; host.style.gap = ''; }
    let items = [];
    grp.children.forEach(cu => {
      const c = auraGet(cu); if (!c) return;
      if (c.type === 'group' || c.type === 'dyngroup') {
        const ev = auraEvalCache[c.uid] || auraEval(c);
        if (!ev.show) return;
        items.push({ key: c.uid, a: c, ev, st: null, ov: {}, w: c.w, h: c.h, nested: true });
        return;
      }
      items = items.concat(auraItems(c, dyn ? grp.childW : c.w, dyn ? grp.childH : c.h, dyn));
    });
    if (dyn) {
      if (grp.sort === 'asc' || grp.sort === 'desc') {
        const rem = it => it.st ? (it.st.progressType === 'timed' ? it.st.remaining : (it.st.value || 0)) : 1e9;
        items.sort((x, y) => grp.sort === 'asc' ? rem(x) - rem(y) : rem(y) - rem(x));
      } else if (grp.sort === 'name') items.sort((x, y) => String(x.st ? x.st.name : x.a.name).localeCompare(String(y.st ? y.st.name : y.a.name)));
      if (grp.limit > 0) items = items.slice(0, grp.limit);
    }
    auraReconcile(host, items, it => {
      if (it.nested) { const el = document.createElement('div'); el.className = 'au-el au-t-' + it.a.type; el.dataset.uid = it.a.uid; const inner = document.createElement('div'); inner.className = 'au-kids'; el.appendChild(inner); return el; }
      return auraMakeEl(it);
    }, (el, it) => {
      if (!dyn) {
        // Keep children inside the group box even if the group was shrunk after they were placed.
        const BW = host.clientWidth, BH = host.clientHeight;
        if (BW > 0 && BH > 0) {
          const cw = it.nested ? it.a.w : it.w, chh = it.nested ? it.a.h : it.h;
          const nx = Math.max(0, Math.min(it.a.x, Math.max(0, BW - cw))), ny = Math.max(0, Math.min(it.a.y, Math.max(0, BH - chh)));
          if (nx !== it.a.x || ny !== it.a.y) { it.a.x = nx; it.a.y = ny; auraCfgSave(); }
        }
        el.style.position = 'absolute'; el.style.left = it.a.x + 'px'; el.style.top = it.a.y + 'px';
      }
      else { el.style.position = 'relative'; el.style.left = ''; el.style.top = ''; el.style.flex = 'none'; }
      if (it.nested) {
        el.style.width = it.a.w + 'px'; el.style.height = it.a.h + 'px';
        el.classList.toggle('au-sel', !!(edit && auraEdSel === it.a.uid));
        el.classList.toggle('au-editable', !!edit);
        auraRenderChildrenInto(el.firstChild, it.a, edit);
        if (edit && !dyn) auraArmDrag(el, it.a); else el.onmousedown = null;
        return;
      }
      auraUpdateEl(el, it, edit);
      if (edit && !dyn) auraArmDrag(el, it.a); else { el.onmousedown = null; Array.from(el.children).filter(x => x.classList.contains('au-rz')).forEach(x => x.remove()); }
      el.onclick = edit ? (e => { e.stopPropagation(); auraEdSel = it.a.uid; auraInvalidate(); auraEdRepaint(); }) : null;
    }, auraFinishClass);
  }
  // Drag a child inside a static group (edit mode): moves its x/y; the corner handle resizes.
  // Snaps to the group's edges and to siblings (edge alignment, adjacency, and matching size
  // when resizing), and never lets a child leave the group's box.
  const AURA_SNAP = 10;
  function auraArmDrag(el, a) {
    if (!auraChild(el, 'au-rz')) ['n', 's', 'w', 'e', 'nw', 'ne', 'sw', 'se'].forEach(d => { const rz = document.createElement('div'); rz.className = 'au-rz au-rz-' + d; rz.dataset.dir = d; el.appendChild(rz); });
    el.onmousedown = e => {
      if (e.button !== 0) return;
      e.stopPropagation(); e.preventDefault();
      auraEdSel = a.uid; auraEdSig = '';
      const rz = !!(e.target.classList && e.target.classList.contains('au-rz'));
      const dir = rz ? (e.target.dataset.dir || 'se') : '';
      const host = el.parentElement;
      const BW = host ? host.clientWidth : 1e9, BH = host ? host.clientHeight : 1e9;
      const par = auraGet(a.parent);
      const sibs = par ? par.children.map(auraGet).filter(x => x && x.uid !== a.uid) : [];
      const near = (p, q) => Math.abs(p - q) <= AURA_SNAP;
      const overlapY = (s, y, h) => s.y < y + h + AURA_SNAP && s.y + s.h > y - AURA_SNAP;
      const overlapX = (s, x, w) => s.x < x + w + AURA_SNAP && s.x + s.w > x - AURA_SNAP;
      const sx = e.clientX, sy = e.clientY, ox = a.x, oy = a.y, ow = a.w, oh = a.h;
      const mv = ev => {
        const dx = ev.clientX - sx, dy = ev.clientY - sy;
        if (rz) {
          // Any corner: the opposite corner stays put. Work in edges, snap the moving edges to
          // sibling / group edges, and match sibling sizes.
          const west = dir.indexOf('w') >= 0, north = dir.indexOf('n') >= 0;
          const east = dir.indexOf('e') >= 0, south = dir.indexOf('s') >= 0;
          let L = ox, T = oy, R = ox + ow, B = oy + oh;
          if (west) L = Math.min(ox + ow - 12, Math.round(ox + dx)); else if (east) R = Math.max(ox + 12, Math.round(ox + ow + dx));
          if (north) T = Math.min(oy + oh - 12, Math.round(oy + dy)); else if (south) B = Math.max(oy + 12, Math.round(oy + oh + dy));
          const ex = [0, BW], ey = [0, BH];
          sibs.forEach(sb => { ex.push(sb.x, sb.x + sb.w); ey.push(sb.y, sb.y + sb.h); });
          ex.forEach(c => { if (west) { if (near(L, c)) L = c; } else if (east && near(R, c)) R = c; });
          ey.forEach(c => { if (north) { if (near(T, c)) T = c; } else if (south && near(B, c)) B = c; });
          sibs.forEach(sb => {
            if ((west || east) && near(R - L, sb.w)) { if (west) L = R - sb.w; else R = L + sb.w; }
            if ((north || south) && near(B - T, sb.h)) { if (north) T = B - sb.h; else B = T + sb.h; }
          });
          if (ev.shiftKey && (west || east) && (north || south)) { const m = Math.max(R - L, B - T); if (west) L = R - m; else R = L + m; if (north) T = B - m; else B = T + m; }
          L = Math.max(0, L); T = Math.max(0, T); R = Math.min(BW, R); B = Math.min(BH, B);
          a.x = L; a.y = T; a.w = Math.max(12, R - L); a.h = Math.max(12, B - T);
        } else {
          let x = Math.round(ox + dx), y = Math.round(oy + dy), w = ow, h = oh;
          let done = false;
          // 1. Butt up against a neighbour: side by side -> share top and height; stacked -> share left and width.
          for (const sb of sibs) {
            if (overlapY(sb, y, h)) {
              if (near(x, sb.x + sb.w)) { x = sb.x + sb.w; y = sb.y; h = sb.h; done = true; break; }
              if (near(x + w, sb.x))    { x = sb.x - w;     y = sb.y; h = sb.h; done = true; break; }
            }
            if (overlapX(sb, x, w)) {
              if (near(y, sb.y + sb.h)) { y = sb.y + sb.h; x = sb.x; w = sb.w; done = true; break; }
              if (near(y + h, sb.y))    { y = sb.y - h;     x = sb.x; w = sb.w; done = true; break; }
            }
          }
          // 2. Otherwise align edges with neighbours and the group box.
          if (!done) {
            const xs = [0, BW], ys = [0, BH];
            sibs.forEach(sb => { xs.push(sb.x, sb.x + sb.w, sb.x + (sb.w - w) / 2); ys.push(sb.y, sb.y + sb.h, sb.y + (sb.h - h) / 2); });
            xs.forEach(c => { if (near(x, c)) x = c; else if (near(x + w, c)) x = c - w; });
            ys.forEach(c => { if (near(y, c)) y = c; else if (near(y + h, c)) y = c - h; });
            x = Math.round(x); y = Math.round(y);
          }
          a.w = Math.max(12, Math.min(w, BW)); a.h = Math.max(12, Math.min(h, BH));
          a.x = Math.max(0, Math.min(x, Math.max(0, BW - a.w))); a.y = Math.max(0, Math.min(y, Math.max(0, BH - a.h)));
        }
        el.style.left = a.x + 'px'; el.style.top = a.y + 'px'; el.style.width = a.w + 'px'; el.style.height = a.h + 'px';
      };
      const up = () => { document.removeEventListener('mousemove', mv); document.removeEventListener('mouseup', up); auraCfgSave(); auraInvalidate(); auraEdRepaint(); };
      document.addEventListener('mousemove', mv); document.addEventListener('mouseup', up);
    };
  }

  // A root's HUD window.
  function renderAuraGroup(uid) {
    const a = auraGet(uid);
    const c = $('content');
    if (!a) { c.textContent = ''; return; }
    let host = auraChild(c, 'au-root');
    if (!host) { c.innerHTML = ''; host = document.createElement('div'); host.className = 'au-root'; c.appendChild(host); }
    const win = (typeof wmWinOf === 'function') ? wmWinOf(auraTabId(uid)) : null;
    const locked = !!(win && win.locked);
    const edit = !locked && (typeof paneVisible === 'function') && paneVisible('auras');
    host.classList.toggle('au-edit', edit);
    // Edit mode on a top-level window: big corner grips that resize the WINDOW itself (the
    // frame's own 5px edge strips are hard to hit on a small HUD), and dragging empty space
    // moves it, so the window is handled like any child box.
    const grips = Array.from(host.children).filter(x => x.classList.contains('au-wrz'));
    if (edit && win && typeof wmResizeStart === 'function') {
      if (!grips.length) ['n', 's', 'w', 'e', 'nw', 'ne', 'sw', 'se'].forEach(d => {
        const gz = document.createElement('div'); gz.className = 'au-rz au-wrz au-rz-' + d;
        gz.addEventListener('mousedown', e => { e.stopPropagation(); wmResizeStart(win, d, e); });
        host.appendChild(gz);
      });
      host.onmousedown = e => { if (e.target === host && typeof wmDragStart === 'function') { wmDragStart(win, e); } };
      host.onclick = e => { if (e.target === host) { auraEdSel = uid; auraInvalidate(); auraEdRepaint(); } };
    } else { grips.forEach(x => x.remove()); host.onmousedown = null; host.onclick = null; }
    host.dataset.label = edit ? ((a.name || a.type) + (a.type === 'dyngroup' ? '  (dynamic)' : a.type === 'group' ? '  (group)' : '')) : '';
    // Usable box = the content area clipped to the window frame (the frame's border and the
    // resize grips can eat a few pixels that clientHeight still reports), minus a 2px margin.
    let W = Math.max(8, c.clientWidth), H = Math.max(8, c.clientHeight);
    try {
      const cr = c.getBoundingClientRect();
      const fr = (win && win.el) ? win.el.getBoundingClientRect() : cr;
      W = Math.max(8, Math.floor(Math.min(cr.right, fr.right - 1) - cr.left) - 2);
      H = Math.max(8, Math.floor(Math.min(cr.bottom, fr.bottom - 1) - cr.top) - 2);
    } catch (e) {}
    const setHint = txt => {
      let hint = auraChild(host, 'au-hudhint');
      if (!txt) { if (hint) hint.remove(); return; }
      if (!hint) { hint = document.createElement('div'); hint.className = 'au-hint au-hudhint'; host.appendChild(hint); }
      hint.textContent = txt;
    };
    if (a.type === 'group' || a.type === 'dyngroup') {
      host.classList.add('au-kids');
      auraRenderChildrenInto(host, a, edit);
      setHint((!a.children.length && !locked) ? ((a.name || 'Group') + ': empty. Add auras to it in the Auras panel.') : '');
      return;
    }
    host.classList.remove('au-kids'); host.style.display = 'block';
    // Square displays (icon, ring, pips) keep their aspect: the smaller side of the window,
    // centred. Bars and text stretch to the window.
    const square = a.type === 'icon' || a.type === 'ring' || a.type === 'pips';
    const sw = square ? Math.min(W, H) : W, sh = square ? Math.min(W, H) : H;
    const items = auraItems(a, sw, sh, false);
    auraReconcile(host, items, auraMakeEl, (el, it) => {
      auraUpdateEl(el, it, edit);
      // Pin to the content box with CSS rather than trusting measured pixels: a stretched display
      // (bar, text) takes the whole box via inset, a square one is centred by auto margins and
      // capped at 100% so it can never run past the window edge.
      el.style.position = 'absolute'; el.style.left = '1px'; el.style.top = '1px'; el.style.right = '1px'; el.style.bottom = '1px';
      el.style.maxWidth = 'calc(100% - 2px)'; el.style.maxHeight = 'calc(100% - 2px)'; el.style.margin = 'auto';
      if (!square) { el.style.width = 'auto'; el.style.height = 'auto'; }
      el.onclick = edit ? (e => { e.stopPropagation(); auraEdSel = a.uid; auraInvalidate(); auraEdRepaint(); }) : null;
    }, auraFinishClass);
    setHint((!items.length && !locked && !a.triggers.list.length) ? ((a.name || 'Aura') + ': no trigger yet.') : '');
  }

  // ---------------------------------------------------------------- import / export
  function auraSubtree(uid, out) {
    const a = auraGet(uid); if (!a) return out;
    out.push(a); a.children.forEach(c => auraSubtree(c, out)); return out;
  }
  function auraExportString(uid) {
    const list = auraSubtree(uid, []);
    const json = JSON.stringify({ m: 'aura', v: 1, root: uid, auras: list });
    let b64 = '';
    try { b64 = btoa(unescape(encodeURIComponent(json))); } catch (e) { b64 = ''; }
    return AURA_EXPORT_PREFIX + b64;
  }
  function auraImportString(str) {
    str = String(str || '').trim();
    if (str.indexOf(AURA_EXPORT_PREFIX) !== 0) throw new Error('Not an aura string');
    const json = decodeURIComponent(escape(atob(str.slice(AURA_EXPORT_PREFIX.length))));
    const p = JSON.parse(json);
    if (!p || p.m !== 'aura' || !Array.isArray(p.auras)) throw new Error('Bad aura string');
    const map = {};
    p.auras.forEach(a => { if (a && a.uid) map[a.uid] = auraNewUid(); });
    let root = null;
    p.auras.forEach(raw => {
      const a = auraNorm(raw); if (!a || !raw.uid) return;
      a.uid = map[raw.uid];
      a.parent = (raw.parent && map[raw.parent]) ? map[raw.parent] : null;
      a.children = (a.children || []).map(c => map[c]).filter(Boolean);
      auraCfg.auras[a.uid] = a;
      if (raw.uid === p.root) root = a;
    });
    if (!root) throw new Error('No root');
    root.parent = null;
    auraCfg.roots.push(root.uid);
    auraCfgSave(); auraInvalidate();
    if (typeof renderMenubar === 'function') { try { renderMenubar(); } catch (e) {} }
    return root;
  }

  // ---------------------------------------------------------------- presets
  const AURA_PRESETS = [
    { name: 'Necromancy stacks', build: () => {
      const g = auraDefaults('dyngroup'); g.name = 'Necromancy'; g.grow = 'right'; g.childW = 52; g.childH = 70; g.space = 6; g.w = 260; g.h = 76;
      const mk = (name, max, col) => { const a = auraDefaults('pips'); a.name = name; a.max = max; a.pipColor = col; a.hideIdle = false; a.triggers.list.push(Object.assign(auraTrigDefaults('buff'), { name, op: 'active' })); return a; };
      const kids = [mk('Necrosis', 12, '#b14df0'), mk('Residual Soul', 5, '#4fe07a')];
      const ld = auraDefaults('icon'); ld.name = 'Living Death'; ld.triggers.list.push(Object.assign(auraTrigDefaults('buff'), { name: 'Living Death', op: 'active' })); ld.anim.start = 'grow'; ld.anim.finish = 'fade';
      kids.push(ld);
      return { root: g, kids };
    } },
    { name: 'Protection prayers', build: () => {
      const g = auraDefaults('dyngroup'); g.name = 'Prayers'; g.grow = 'right'; g.childW = 40; g.childH = 40; g.space = 4; g.w = 240; g.h = 44;
      const kids = ['Deflect Magic', 'Deflect Missiles', 'Deflect Melee', 'Deflect Necromancy', 'Soul Split'].map(n => { const a = auraDefaults('icon'); a.name = n; a.hideIdle = false; a.triggers.list.push(Object.assign(auraTrigDefaults('buff'), { name: n, op: 'active' })); return a; });
      return { root: g, kids };
    } },
    { name: 'Vitals (bars)', build: () => {
      const g = auraDefaults('dyngroup'); g.name = 'Vitals'; g.grow = 'down'; g.childW = 220; g.childH = 22; g.space = 4; g.w = 230; g.h = 110;
      const mk = (stat, label, col, low) => { const a = auraDefaults('bar'); a.name = label; a.barColor = col; a.hideIdle = false; a.textL = '%n'; a.textR = stat === 'adren' ? '%p%%' : '%p / %t';
        a.triggers.list.push(Object.assign(auraTrigDefaults('vital'), { stat, op: 'any' }));
        if (low) a.conditions.push({ checks: [{ trig: -1, v: 'pct', op: '<', val: 25 }], changes: [{ prop: 'glow', val: true }] });
        return a; };
      return { root: g, kids: [mk('hp', 'Health', '#ff5a5a', true), mk('prayer', 'Prayer', '#4fb2ff', true), mk('adren', 'Adrenaline', '#ffc93a', false), mk('summon', 'Summoning', '#4fe07a', false)] };
    } },
    { name: 'Low health warning (text)', build: () => {
      const a = auraDefaults('text'); a.name = 'LOW HP'; a.text = 'LOW HEALTH  %p'; a.fontSize = 26; a.textColor = '#ff5a5a'; a.w = 260; a.h = 40; a.anim.main = 'flash'; a.anim.start = 'grow';
      a.triggers.list.push(Object.assign(auraTrigDefaults('vital'), { stat: 'hp', field: 'stacks', op: '<', val: 40 }));
      a.actions.show.sound = 'alert 4';
      return { root: a, kids: [] };
    } },
    { name: 'Every buff (dynamic, sorted)', build: () => {
      const g = auraDefaults('dyngroup'); g.name = 'Buffs'; g.grow = 'grid'; g.gridCols = 8; g.childW = 36; g.childH = 36; g.space = 3; g.sort = 'asc'; g.w = 320; g.h = 80;
      const a = auraDefaults('icon'); a.name = 'Any buff'; a.triggers.list.push(Object.assign(auraTrigDefaults('buff'), { name: '', op: 'active', clones: true }));
      return { root: g, kids: [a] };
    } },
  ];
  function auraAddPreset(p) {
    const b = p.build();
    const root = b.root; root.uid = auraNewUid();
    auraCfg.auras[root.uid] = root; auraCfg.roots.push(root.uid);
    b.kids.forEach(k => { k.uid = auraNewUid(); k.parent = root.uid; root.children.push(k.uid); auraCfg.auras[k.uid] = k; });
    auraCfgSave(); auraInvalidate();
    if (typeof renderMenubar === 'function') { try { renderMenubar(); } catch (e) {} }
    auraEdSel = root.uid;
    setTimeout(() => { try { auraWinShow(root.uid, true); } catch (e) {} }, 0);
    auraEdRepaint();
  }

  // ---------------------------------------------------------------- editor helpers
  // Candidates for a name field: [{name, icon, live, side}] for buffs/debuffs (catalogue +
  // names seen + active now) or abilities (cache configs + what is on the bar).
  function auraNameCandidates(kind) {
    const out = [], seen = {};
    const push = (name, icon, live, side) => { if (!name || seen[name]) return; seen[name] = 1; out.push({ name, icon, live, side }); };
    if (kind === 'ability') {
      auraAbilitySlots().forEach(sl => push(sl.name, { sprite: sl.id || 0, item: sl.item || 0 }, true, 'ability'));
      try { if (typeof abCfgLoad === 'function') abCfgLoad(); } catch (e) {}
      const cfg = (typeof abCfg !== 'undefined') ? abCfg : null;
      if (cfg) Object.keys(cfg).sort().forEach(n => push(n, cfg[n].i ? { sprite: cfg[n].i | 0, item: 0 } : null, false, 'ability'));
      return out;
    }
    const want = kind === 'debuff';
    auraEffects().filter(e => e.debuff === want).forEach(e => push(e.b.name, { sprite: e.b.sprite || 0, item: e.b.item || 0 }, true, kind));
    const m = auraIconMemoLoad();
    const cat = auraCatalog ? auraCatalog[want ? 'debuffs' : 'buffs'] : null;
    if (cat) Object.keys(cat).sort().forEach(n => push(n, cat[n], false, kind));
    auraSeenLoad().forEach(n => push(n, m[n] || null, false, kind));
    return out;
  }
  // Live search list under a name input: filters as you type, shows the icon of each match,
  // click (or Enter for the first) fills the field. Shares #sndMenu so the usual outside-click
  // and Escape handling closes it.
  function auraNamePicker(inp, kind, onPick) {
    if (typeof closeSoundMenu === 'function') closeSoundMenu();
    const pop = document.createElement('div'); pop.className = 'sndmenu au-npick'; pop.id = 'sndMenu';
    pop.addEventListener('mousedown', e => e.stopPropagation());
    pop.addEventListener('click', e => e.stopPropagation());
    const list = document.createElement('div'); list.className = 'au-npick-list'; pop.appendChild(list);
    const paint = () => {
      const q = String(inp.value || '').trim().toLowerCase();
      const all = auraNameCandidates(kind);
      let rows = q ? all.filter(c => c.name.toLowerCase().indexOf(q) >= 0) : all;
      // exact / starts-with first, then live ones, then the rest
      rows.sort((x, y) => {
        const sx = q ? (x.name.toLowerCase() === q ? 0 : x.name.toLowerCase().indexOf(q) === 0 ? 1 : 2) : 2;
        const sy = q ? (y.name.toLowerCase() === q ? 0 : y.name.toLowerCase().indexOf(q) === 0 ? 1 : 2) : 2;
        if (sx !== sy) return sx - sy;
        if (x.live !== y.live) return x.live ? -1 : 1;
        return x.name.localeCompare(y.name);
      });
      list.textContent = '';
      if (!rows.length) { const h = document.createElement('div'); h.className = 'sndmenu-it au-npick-none'; h.textContent = q ? 'No ' + kind + ' matching "' + q + '"' : 'Nothing known yet'; list.appendChild(h); return; }
      rows.slice(0, 60).forEach((c, i) => {
        const it = document.createElement('div'); it.className = 'sndmenu-it au-npick-row' + (i === 0 && q ? ' first' : '');
        const ic = document.createElement('div'); ic.className = 'au-icon au-npick-ico'; auraIconFor(ic, c.icon, c.name); it.appendChild(ic);
        const nm = document.createElement('span'); nm.textContent = c.name; nm.style.flex = '1'; it.appendChild(nm);
        if (c.live) { const lv = document.createElement('span'); lv.className = 'au-npick-live'; lv.textContent = kind === 'ability' ? 'on bar' : 'active'; it.appendChild(lv); }
        it.addEventListener('click', () => { onPick(c.name); if (typeof closeSoundMenu === 'function') closeSoundMenu(); });
        list.appendChild(it);
      });
      if (rows.length > 60) { const h = document.createElement('div'); h.className = 'sndmenu-it au-npick-none'; h.textContent = '+' + (rows.length - 60) + ' more, keep typing'; list.appendChild(h); }
      // The list changes height with every keystroke; the click region must follow it, and so
      // must its placement (it may need to flip above the field as it grows).
      if (pop.parentNode) { try { if (typeof placeMenu === 'function') placeMenu(pop, inp); } catch (e) {} try { wmRectsSoon(); } catch (e) {} }
    };
    paint();
    document.body.appendChild(pop);
    if (typeof placeMenu === 'function') placeMenu(pop, inp);
    try { wmRectsSoon(); } catch (e) {}
    return paint;
  }
  // Name input wired to the picker: opens on focus/typing, Enter takes the first match.
  function auraNameInput(t, kind, a, ph) {
    const inp = auraInput(t.name, v => { t.name = v; auraTouch(a); }, ph);
    let repaint = null;
    const open = () => { if (!document.getElementById('sndMenu') || !repaint) repaint = auraNamePicker(inp, kind, n => { t.name = n; inp.value = n; auraTouch(a); }); else repaint(); };
    inp.addEventListener('focus', open);
    inp.addEventListener('input', () => { t.name = inp.value; if (document.getElementById('sndMenu') && repaint) repaint(); else open(); });
    inp.addEventListener('keydown', e => {
      e.stopPropagation();
      if (e.key === 'Enter') { const f = document.querySelector('#sndMenu .au-npick-row.first'); if (f) { f.click(); } else { inp.blur(); } }
      if (e.key === 'Escape') { if (typeof closeSoundMenu === 'function') closeSoundMenu(); inp.blur(); }
    });
    return inp;
  }
  // Open a choice menu anchored to `el`; clicking the same anchor again closes it.
  function auraMenu(el, ents) {
    if (typeof openChoice !== 'function') return;
    if (document.getElementById('sndMenu')) { const own = el.dataset.menuOpen === '1'; if (typeof closeSoundMenu === 'function') closeSoundMenu(); if (own) { el.dataset.menuOpen = ''; return; } }
    document.querySelectorAll('[data-menu-open="1"]').forEach(x => { x.dataset.menuOpen = ''; });
    el.dataset.menuOpen = '1';
    openChoice(el, ents);
  }
  function auraMkBtn(label, title, fn, cls) {
    const b = document.createElement('button'); b.type = 'button'; b.className = 'au-btn' + (cls ? ' ' + cls : '');
    b.textContent = label; if (title) b.title = title;
    b.addEventListener('click', e => { e.stopPropagation(); fn(e); });
    return b;
  }
  function auraCycle(opts, get, set, title) {
    const b = document.createElement('button'); b.type = 'button'; b.className = 'al-typesel au-cyc'; if (title) b.title = title;
    const cur = document.createElement('span'); cur.className = 'cur'; const car = document.createElement('span'); car.className = 'cv'; car.textContent = String.fromCharCode(0x25be);
    b.appendChild(cur); b.appendChild(car);
    const paint = () => { const o = opts.find(x => String(x[0]) === String(get())); cur.textContent = o ? o[1] : String(get()); };
    paint();
    b.addEventListener('click', e => {
      e.stopPropagation();
      // Second click on the same trigger closes its menu instead of reopening it.
      if (document.getElementById('sndMenu')) { const own = b.dataset.menuOpen === '1'; if (typeof closeSoundMenu === 'function') closeSoundMenu(); if (own) { b.dataset.menuOpen = ''; return; } }
      document.querySelectorAll('[data-menu-open="1"]').forEach(x => { x.dataset.menuOpen = ''; });
      b.dataset.menuOpen = '1';
      if (typeof openChoice === 'function') { openChoice(b, opts.map(o => ({ label: o[1], act: () => { set(o[0]); paint(); } }))); return; }
      const i = opts.findIndex(x => String(x[0]) === String(get()));
      set(opts[(i + 1) % opts.length][0]); paint();
    });
    return b;
  }
  // The app's toggle: a pill (see .al-pill), never a native checkbox.
  function auraPill(on, fn, title) {
    const p = document.createElement('div'); p.className = 'al-pill' + (on ? ' on' : ''); p.appendChild(document.createElement('span'));
    if (title) p.title = title;
    p.addEventListener('click', e => { e.stopPropagation(); const nv = !p.classList.contains('on'); p.classList.toggle('on', nv); fn(nv); });
    return p;
  }
  function auraCheck(label, on, fn) {
    const lab = document.createElement('div'); lab.className = 'au-opt';
    lab.appendChild(auraPill(on, fn));
    lab.appendChild(document.createTextNode(label));
    return lab;
  }
  function auraInput(val, fn, ph, num, cls) {
    const i = document.createElement('input'); i.type = num ? 'number' : 'text'; i.className = cls || (num ? 'al-num' : 'al-input au-gname');
    i.value = val == null ? '' : String(val); if (ph) i.placeholder = ph;
    i.addEventListener('change', () => { fn(num ? (isNaN(parseFloat(i.value)) ? 0 : parseFloat(i.value)) : i.value); });
    i.addEventListener('keydown', e => { if (e.key === 'Enter') i.blur(); e.stopPropagation(); });
    i.addEventListener('click', e => e.stopPropagation());
    return i;
  }
  // ---- colour picker: saturation/value square + hue bar + swatches + hex ----
  function auraHexToHsv(hex) {
    const m = /^#?([0-9a-f]{6})$/i.exec(String(hex || '').trim()); if (!m) return { h: 260, s: 0.5, v: 1 };
    const n = parseInt(m[1], 16), r = ((n >> 16) & 255) / 255, g = ((n >> 8) & 255) / 255, b = (n & 255) / 255;
    const mx = Math.max(r, g, b), mn = Math.min(r, g, b), d = mx - mn;
    let h = 0; if (d) { if (mx === r) h = ((g - b) / d) % 6; else if (mx === g) h = (b - r) / d + 2; else h = (r - g) / d + 4; h *= 60; if (h < 0) h += 360; }
    return { h, s: mx ? d / mx : 0, v: mx };
  }
  function auraHsvToHex(h, s, v) {
    const c = v * s, x = c * (1 - Math.abs((h / 60) % 2 - 1)), m = v - c;
    let r = 0, g = 0, b = 0;
    if (h < 60) { r = c; g = x; } else if (h < 120) { r = x; g = c; } else if (h < 180) { g = c; b = x; } else if (h < 240) { g = x; b = c; } else if (h < 300) { r = x; b = c; } else { r = c; b = x; }
    const to = q => ('0' + Math.round((q + m) * 255).toString(16)).slice(-2);
    return '#' + to(r) + to(g) + to(b);
  }
  function auraColorPicker(anchor, cur, onPick) {
    if (typeof closeSoundMenu === 'function') closeSoundMenu();
    const pop = document.createElement('div'); pop.className = 'sndmenu au-cp'; pop.id = 'sndMenu';
    pop.addEventListener('mousedown', e => e.stopPropagation());
    pop.addEventListener('click', e => e.stopPropagation());
    let hsv = auraHexToHsv(cur);
    const sv = document.createElement('div'); sv.className = 'au-cp-sv';
    const svDot = document.createElement('i'); sv.appendChild(svDot);
    const hue = document.createElement('div'); hue.className = 'au-cp-hue';
    const hueDot = document.createElement('i'); hue.appendChild(hueDot);
    const row = document.createElement('div'); row.className = 'au-cp-row';
    const prev = document.createElement('span'); prev.className = 'au-swatch';
    const hex = document.createElement('input'); hex.type = 'text'; hex.className = 'al-input au-hex';
    hex.addEventListener('keydown', e => { e.stopPropagation(); if (e.key === 'Enter') hex.blur(); });
    hex.addEventListener('change', () => { const v = hex.value.trim(); if (/^#[0-9a-fA-F]{6}$/.test(v)) { hsv = auraHexToHsv(v); paint(); onPick(v); } });
    row.appendChild(prev); row.appendChild(hex);
    const pal = document.createElement('div'); pal.className = 'au-cp-pal';
    AURA_PALETTE.forEach(c => { const b = document.createElement('span'); b.className = 'au-swatch'; b.style.background = c; b.title = c; b.addEventListener('click', () => { hsv = auraHexToHsv(c); paint(); onPick(c); }); pal.appendChild(b); });
    const paint = () => {
      const col = auraHsvToHex(hsv.h, hsv.s, hsv.v);
      sv.style.backgroundImage = 'linear-gradient(0deg, #000 0%, rgba(0,0,0,0) 100%), linear-gradient(90deg, #fff 0%, ' + auraHsvToHex(hsv.h, 1, 1) + ' 100%)';
      svDot.style.left = (hsv.s * 100) + '%'; svDot.style.top = ((1 - hsv.v) * 100) + '%';
      hueDot.style.left = (hsv.h / 360 * 100) + '%';
      prev.style.background = col; hex.value = col;
    };
    const drag = (el, fn) => {
      el.addEventListener('mousedown', e => {
        e.preventDefault(); e.stopPropagation();
        const mv = ev => { const r = el.getBoundingClientRect(); fn(Math.max(0, Math.min(1, (ev.clientX - r.left) / r.width)), Math.max(0, Math.min(1, (ev.clientY - r.top) / r.height))); paint(); onPick(auraHsvToHex(hsv.h, hsv.s, hsv.v)); };
        const up = () => { document.removeEventListener('mousemove', mv); document.removeEventListener('mouseup', up); };
        document.addEventListener('mousemove', mv); document.addEventListener('mouseup', up); mv(e);
      });
    };
    drag(sv, (x, y) => { hsv.s = x; hsv.v = 1 - y; });
    drag(hue, x => { hsv.h = Math.min(359.9, x * 360); });
    paint();
    pop.appendChild(sv); pop.appendChild(hue); pop.appendChild(row); pop.appendChild(pal);
    document.body.appendChild(pop);
    if (typeof placeMenu === 'function') placeMenu(pop, anchor);
    try { wmRectsSoon(); } catch (e) {}
  }
  function auraColor(val, fn) {
    const wrap = document.createElement('span'); wrap.className = 'au-colwrap';
    const sw = document.createElement('span'); sw.className = 'au-swatch'; sw.style.background = val || '#ffffff';
    const inp = auraInput(val, v => { const s = String(v).trim(); if (/^#[0-9a-fA-F]{6}$/.test(s)) { sw.style.background = s; fn(s); } }, '#rrggbb', false, 'al-input au-hex');
    sw.title = 'Pick a colour';
    sw.addEventListener('click', e => {
      e.stopPropagation();
      if (document.getElementById('sndMenu')) { if (typeof closeSoundMenu === 'function') closeSoundMenu(); return; }
      auraColorPicker(sw, inp.value || val, c => { sw.style.background = c; inp.value = c; fn(c); });
    });
    wrap.appendChild(sw); wrap.appendChild(inp);
    return wrap;
  }
  // Text tokens, for the guide menu and the legend under token fields.
  const AURA_TOKENS = [
    ['%p', 'Progress: time left (timed) or the value (static)'],
    ['%t', 'Total: full duration or the maximum'],
    ['%n', 'Name of what triggered (buff, ability, NPC...)'],
    ['%s', 'Stacks / count shown on the buff'],
    ['%v', 'Raw value (stacks, hitpoints, count...)'],
    ['%c', 'Percent of total, e.g. 72%'],
    ['%r', 'Time left in whole seconds'],
    ['%2.p', 'Any token from trigger 2 (%1.n, %3.s ...)'],
    ['%%', 'A literal % sign'],
  ];
  // A text field that accepts tokens: the % button lists them with meanings and inserts one.
  function auraTextInput(val, fn, ph) {
    const cell = document.createElement('div'); cell.className = 'au-cell au-tokcell';
    const inp = auraInput(val, fn, ph);
    inp.title = 'Tokens: ' + AURA_TOKENS.map(t => t[0]).join(' ') + '. Click % for the guide.';
    const b = auraMkBtn('%', 'Insert a token (shows what each one means)', () => {
      if (typeof openChoice !== 'function') return;
      auraMenu(b, AURA_TOKENS.map(t => ({ label: t[0] + '   ' + t[1], raw: true, act: () => { inp.value = (inp.value || '') + t[0]; fn(inp.value); } })));
    }, 'au-sm');
    cell.appendChild(inp); cell.appendChild(b);
    return cell;
  }
  function auraTokenLegend() {
    const h = document.createElement('div'); h.className = 'au-hint';
    h.textContent = 'Tokens: %p progress, %t total, %n name, %s stacks, %v value, %c percent, %2.p from trigger 2, %% literal. Anything else is printed as typed.';
    return h;
  }
  function auraRow(grid, label, ctl, tip) {
    const l = document.createElement('span'); l.className = 'au-lab'; l.textContent = label; if (tip) l.title = tip;
    grid.appendChild(l); grid.appendChild(ctl);
  }
  function auraGrid() { const g = document.createElement('div'); g.className = 'au-egrid'; return g; }
  function auraSoundSel(get, set) {
    const snds = (typeof ALERT_SOUNDS !== 'undefined') ? ALERT_SOUNDS : ['none'];
    const cell = document.createElement('div'); cell.className = 'au-cell';
    cell.appendChild(auraCycle(snds.map(s => [s, s]), get, set, 'Sound'));
    cell.appendChild(auraMkBtn('▶', 'Preview', () => { const s = get(); if (s && s !== 'none') { try { bridge().playSound(s); } catch (e) {} } }, 'au-sm'));
    return cell;
  }
  function auraTouch(a) { auraCfgSave(); auraInvalidate(); auraEdRepaint(); }
  function auraCreate(type, parentUid) {
    const a = auraDefaults(type); a.uid = auraNewUid();
    const n = Object.keys(auraAll()).length + 1;
    a.name = (AURA_TYPES.find(t => t[0] === type) || ['', 'Aura'])[1] + ' ' + n;
    if (parentUid && auraGet(parentUid)) {
      const p = auraGet(parentUid); a.parent = parentUid; p.children.push(a.uid);
      if (p.type === 'group') { a.x = 8 + (p.children.length - 1) * 12; a.y = 8 + (p.children.length - 1) * 12; }
    } else auraCfg.roots.push(a.uid);
    if (AURA_DISPLAY_TYPES.indexOf(type) >= 0) a.triggers.list.push(auraTrigDefaults('buff'));
    auraCfg.auras[a.uid] = a;
    auraCfgSave(); auraInvalidate();
    if (typeof renderMenubar === 'function') { try { renderMenubar(); } catch (e) {} }
    auraEdSel = a.uid; auraEdTab = AURA_DISPLAY_TYPES.indexOf(type) >= 0 ? 'triggers' : 'display';
    if (!a.parent) setTimeout(() => { try { auraWinShow(a.uid, true); } catch (e) {} }, 0);
    auraEdRepaint();
  }
  function auraDelete(uid) {
    const a = auraGet(uid); if (!a) return;
    const list = auraSubtree(uid, []);
    if (!a.parent) { auraWinShow(uid, false); auraCfg.roots = auraCfg.roots.filter(u => u !== uid); }
    else { const p = auraGet(a.parent); if (p) p.children = p.children.filter(c => c !== uid); }
    list.forEach(x => { delete auraCfg.auras[x.uid]; delete auraShownPrev[x.uid]; });
    if (auraEdSel === uid) auraEdSel = a.parent || null;
    auraCfgSave(); auraInvalidate();
    if (typeof renderMenubar === 'function') { try { renderMenubar(); } catch (e) {} }
    auraEdRepaint();
  }
  function auraDuplicate(uid) {
    const str = auraExportString(uid);
    try {
      const src = auraGet(uid);
      const root = auraImportString(str);
      root.name = (src.name || 'Aura') + ' copy';
      if (src.parent) {
        const p = auraGet(src.parent);
        auraCfg.roots = auraCfg.roots.filter(u => u !== root.uid);
        root.parent = p.uid; p.children.splice(p.children.indexOf(uid) + 1, 0, root.uid);
        root.x = src.x + 10; root.y = src.y + 10;
      }
      auraCfgSave(); auraInvalidate(); auraEdSel = root.uid;
      if (!root.parent) setTimeout(() => { try { auraWinShow(root.uid, true); } catch (e) {} }, 0);
      auraEdRepaint();
    } catch (e) {}
  }
  function auraMove(uid, dir) {
    const a = auraGet(uid); if (!a) return;
    const arr = a.parent ? auraGet(a.parent).children : auraCfg.roots;
    const i = arr.indexOf(uid); const j = i + dir;
    if (i < 0 || j < 0 || j >= arr.length) return;
    arr.splice(j, 0, arr.splice(i, 1)[0]);
    auraTouch(a);
  }
  function auraReparent(uid, newParent) {
    const a = auraGet(uid); if (!a || uid === newParent || (a.parent || null) === (newParent || null)) return;
    let p = newParent; while (p) { if (p === uid) return; const pa = auraGet(p); p = pa ? pa.parent : null; }
    if (a.parent) { const op = auraGet(a.parent); if (op) op.children = op.children.filter(c => c !== uid); }
    else { auraWinShow(uid, false); auraCfg.roots = auraCfg.roots.filter(u => u !== uid); }
    if (newParent) { const np = auraGet(newParent); np.children.push(uid); a.parent = newParent; }
    else { a.parent = null; auraCfg.roots.push(uid); setTimeout(() => { try { auraWinShow(uid, true); } catch (e) {} }, 0); }
    auraCfgSave(); auraInvalidate();
    if (typeof renderMenubar === 'function') { try { renderMenubar(); } catch (e) {} }
    auraEdRepaint();
  }

  // ---------------------------------------------------------------- editor
  function renderAuras() {
    auraCfgLoad();
    const c = $('content');
    let w = document.getElementById('auWrap');
    if (!w) { c.innerHTML = ''; w = document.createElement('div'); w.id = 'auWrap'; w.className = 'au-wrap au-editor'; c.appendChild(w); auraEdSig = ''; }
    // Never rebuild the editor under a field being typed in: the live signature below changes
    // whenever a buff ticks on or off, which would wipe a half-typed name.
    const ae = document.activeElement;
    if (ae && (ae.tagName === 'INPUT' || ae.tagName === 'TEXTAREA') && w.contains(ae)) return;
    const all = auraAll();
    const liveSig = auraEffects().map(e => e.b.name).join('|') + '|' + Object.keys(all).map(u => { const ev = auraEvalCache[u]; return ev && ev.show && (ev.states.length || !all[u].triggers.list.length) ? 1 : 0; }).join('');
    const winSig = auraRoots().map(a => { const ww = (typeof wmWinOf === 'function') ? wmWinOf(auraTabId(a.uid)) : null; return ww ? (ww.locked ? 2 : 1) : 0; }).join('');
    const sel = auraGet(auraEdSel);
    const sig = JSON.stringify([auraCfg.roots, Object.keys(all).length, auraEdSel, auraEdTab, auraEdImport, liveSig, winSig, sel, sel ? sel.children.map(cu => (all[cu] || {}).name) : null]);
    if (sig === auraEdSig) return;
    auraEdSig = sig;
    // Rebuilding under a focused control never fires focusout for it; blur first so the
    // keyboard-capture flag follows reality (see syncKbCapture in client.html).
    const fe = document.activeElement;
    if (fe && w.contains(fe) && fe.blur) { try { fe.blur(); } catch (e) {} }
    w.textContent = '';

    const bar = document.createElement('div'); bar.className = 'au-bar';
    const newB = auraMkBtn('+ New', 'Create a new aura or group', () => {
      if (typeof openChoice !== 'function') { auraCreate('icon', null); return; }
      const s = auraGet(auraEdSel);
      const into = s && (s.type === 'group' || s.type === 'dyngroup') ? s.uid : (s && s.parent ? s.parent : null);
      const intoName = into ? (auraGet(into).name || 'group') : '';
      const ents = AURA_TYPES.map(t => ({ label: t[1] + (into && t[0] !== 'group' && t[0] !== 'dyngroup' ? '  (into ' + intoName + ')' : ''), act: () => auraCreate(t[0], (t[0] === 'group' || t[0] === 'dyngroup') ? null : into) }));
      if (into) ents.push({ label: 'Top-level icon (own window)', act: () => auraCreate('icon', null) });
      auraMenu(newB, ents);
    });
    bar.appendChild(newB);
    const preB = auraMkBtn('Presets', 'Ready-made auras', () => { auraMenu(preB, AURA_PRESETS.map(p => ({ label: p.name, act: () => auraAddPreset(p) }))); });
    bar.appendChild(preB);
    bar.appendChild(auraMkBtn('Import', 'Paste an aura string', () => { auraEdImport = !auraEdImport; auraEdSig = ''; auraEdRepaint(); }));
    w.appendChild(bar);
    if (auraEdImport) {
      const box = document.createElement('div'); box.className = 'au-group';
      const ta = document.createElement('textarea'); ta.className = 'au-ta'; ta.placeholder = 'Paste an aura string (' + AURA_EXPORT_PREFIX + '...) here';
      ta.addEventListener('keydown', e => e.stopPropagation());
      const st = document.createElement('div'); st.className = 'au-hint';
      const row = document.createElement('div'); row.className = 'au-bar';
      row.appendChild(auraMkBtn('Import', '', () => { try { const r = auraImportString(ta.value); auraEdSel = r.uid; auraEdImport = false; setTimeout(() => { try { auraWinShow(r.uid, true); } catch (e) {} }, 0); auraEdRepaint(); } catch (e) { st.textContent = 'Could not import: ' + ((e && e.message) || e); } }));
      row.appendChild(auraMkBtn('Cancel', '', () => { auraEdImport = false; auraEdSig = ''; auraEdRepaint(); }));
      box.appendChild(ta); box.appendChild(row); box.appendChild(st); w.appendChild(box);
    }

    const tree = document.createElement('div'); tree.className = 'au-tree';
    if (!auraCfg.roots.length) {
      const h = document.createElement('div'); h.className = 'au-hint'; h.textContent = 'No auras yet. Use New or Presets. Every top-level aura or group is its own on-screen window.'; tree.appendChild(h);
    }
    const addNode = (uid, depth) => {
      const a = all[uid]; if (!a) return;
      const ev = auraEvalCache[uid];
      const row = document.createElement('div'); row.className = 'au-node' + (auraEdSel === uid ? ' on' : '') + (ev && ev.show && (ev.states.length || !a.triggers.list.length) ? ' live' : '');
      row.style.paddingLeft = (6 + depth * 16) + 'px';
      if (!a.parent) {
        row.appendChild(auraPill(auraWinShown(uid), v => { auraWinShow(uid, v); auraEdSig = ''; setTimeout(auraEdRepaint, 0); }, 'Show on screen'));
      } else { const sp = document.createElement('span'); sp.className = 'au-dot'; row.appendChild(sp); }
      const ty = document.createElement('span'); ty.className = 'au-ntype'; ty.textContent = (AURA_TYPES.find(t => t[0] === a.type) || ['', a.type])[1]; row.appendChild(ty);
      const nm = document.createElement('span'); nm.className = 'au-nname'; nm.textContent = a.name || '(unnamed)'; row.appendChild(nm);
      if (!a.parent) {
        const ww = (typeof wmWinOf === 'function') ? wmWinOf(auraTabId(uid)) : null;
        if (ww) row.appendChild(auraMkBtn(ww.locked ? 'Unlock' : 'Lock', ww.locked ? 'Unlock: the window can be moved, resized and clicked again' : 'Lock: click-through on screen', () => { ww.locked = !ww.locked; try { wmApplyLock(ww); wmRectsSoon(); wmSaveSoon(); } catch (e) {} auraInvalidate(); auraEdRepaint(); }, 'au-sm' + (ww.locked ? ' au-locked' : '')));
      }
      row.addEventListener('click', () => { auraEdSel = uid; auraInvalidate(); auraEdRepaint(); });
      tree.appendChild(row);
      a.children.forEach(cu => addNode(cu, depth + 1));
    };
    auraCfg.roots.forEach(u => addNode(u, 0));
    w.appendChild(tree);

    const a = sel;
    if (!a) { const h = document.createElement('div'); h.className = 'au-hint'; h.textContent = 'Select an aura above to edit it. While this panel is open, unlocked on-screen windows let you drag and resize auras inside a group.'; w.appendChild(h); return; }
    const card = document.createElement('div'); card.className = 'au-group au-editing';
    const head = document.createElement('div'); head.className = 'au-ghead';
    if (a.parent) {
      const par = auraGet(a.parent);
      head.appendChild(auraMkBtn('↑ ' + ((par && par.name) || 'Group'), 'Back to the group this aura is in', () => { auraEdSel = a.parent; auraInvalidate(); auraEdRepaint(); }, 'au-sm au-crumb'));
    }
    head.appendChild(auraInput(a.name, v => { a.name = v; auraCfgSave(); const ww = (typeof wmWinOf === 'function') ? wmWinOf(auraTabId(a.uid)) : null; if (ww && typeof wmSyncTitle === 'function') { try { wmSyncTitle(ww); } catch (e) {} } if (typeof renderMenubar === 'function') { try { renderMenubar(); } catch (e) {} } auraInvalidate(); auraEdRepaint(); }, 'Name'));
    head.appendChild(auraMkBtn('▲', 'Move up', () => auraMove(a.uid, -1), 'au-sm'));
    head.appendChild(auraMkBtn('▼', 'Move down', () => auraMove(a.uid, 1), 'au-sm'));
    head.appendChild(auraMkBtn('Copy', 'Duplicate', () => auraDuplicate(a.uid), 'au-sm'));
    head.appendChild(auraMkBtn('×', 'Delete (and everything inside it)', () => auraDelete(a.uid), 'au-sm au-del'));
    card.appendChild(head);
    const tabs = document.createElement('div'); tabs.className = 'au-tabs';
    const isGrp = a.type === 'group' || a.type === 'dyngroup';
    const tabList = [['display', isGrp ? 'Group' : 'Display'], ['triggers', 'Triggers'], ['conditions', 'Conditions'], ['actions', 'Actions'], ['anim', 'Animations'], ['load', 'Load'], ['export', 'Export']];
    tabList.forEach(([id, lab]) => {
      const t = document.createElement('button'); t.type = 'button'; t.className = 'au-tab' + (auraEdTab === id ? ' on' : ''); t.textContent = lab;
      if (id === 'triggers' && a.triggers.list.length) t.textContent += ' (' + a.triggers.list.length + ')';
      if (id === 'conditions' && a.conditions.length) t.textContent += ' (' + a.conditions.length + ')';
      t.addEventListener('click', () => { auraEdTab = id; auraEdSig = ''; auraEdRepaint(); });
      tabs.appendChild(t);
    });
    card.appendChild(tabs);
    const body = document.createElement('div'); body.className = 'au-tabbody';
    try {
      if (auraEdTab === 'display') auraEdDisplay(body, a);
      else if (auraEdTab === 'triggers') auraEdTriggers(body, a);
      else if (auraEdTab === 'conditions') auraEdConditions(body, a);
      else if (auraEdTab === 'actions') auraEdActions(body, a);
      else if (auraEdTab === 'anim') auraEdAnim(body, a);
      else if (auraEdTab === 'load') auraEdLoad(body, a);
      else if (auraEdTab === 'export') auraEdExport(body, a);
    } catch (e) { const h = document.createElement('div'); h.className = 'au-hint'; h.textContent = 'Editor error: ' + e; body.appendChild(h); }
    card.appendChild(body);
    w.appendChild(card);
  }

  function auraEdDisplay(body, a) {
    const g = auraGrid();
    const isGrp = a.type === 'group' || a.type === 'dyngroup';
    auraRow(g, 'Type', auraCycle(isGrp ? AURA_TYPES.filter(t => t[0] === 'group' || t[0] === 'dyngroup') : AURA_TYPES.filter(t => AURA_DISPLAY_TYPES.indexOf(t[0]) >= 0), () => a.type, v => { a.type = v; auraTouch(a); }));
    const sub = auraSubtree(a.uid, []).map(s => s.uid);
    const groups = Object.values(auraAll()).filter(x => (x.type === 'group' || x.type === 'dyngroup') && sub.indexOf(x.uid) < 0);
    auraRow(g, 'Inside', auraCycle([['', 'Top level (own window)']].concat(groups.map(x => [x.uid, x.name || 'Group'])), () => a.parent || '', v => auraReparent(a.uid, v || null), 'Which group this aura belongs to'));
    const p = a.parent ? auraGet(a.parent) : null;
    if (p && p.type === 'group') {
      const pos = document.createElement('div'); pos.className = 'au-cell';
      pos.appendChild(document.createTextNode('x')); pos.appendChild(auraInput(a.x, v => { a.x = Math.round(v); auraTouch(a); }, '', true));
      pos.appendChild(document.createTextNode('y')); pos.appendChild(auraInput(a.y, v => { a.y = Math.round(v); auraTouch(a); }, '', true));
      auraRow(g, 'Position', pos, 'Offset inside the group. You can also drag it on screen while this panel is open and the window is unlocked.');
    }
    if (p && p.type === 'dyngroup' && !isGrp) { const h = document.createElement('div'); h.className = 'au-hint'; h.textContent = 'Size and position come from the dynamic group.'; auraRow(g, 'Size', h); }
    else if (p) {
      const sz = document.createElement('div'); sz.className = 'au-cell';
      sz.appendChild(document.createTextNode('w')); sz.appendChild(auraInput(a.w, v => { a.w = Math.max(8, Math.round(v)); auraTouch(a); }, '', true));
      sz.appendChild(document.createTextNode('h')); sz.appendChild(auraInput(a.h, v => { a.h = Math.max(8, Math.round(v)); auraTouch(a); }, '', true));
      auraRow(g, 'Size', sz);
    } else {
      const h = document.createElement('div'); h.className = 'au-hint'; h.textContent = 'Top level: drag and resize its window on screen; lock it for click-through.'; auraRow(g, 'Place', h);
    }
    if (!isGrp) {
      auraRow(g, 'Opacity', auraInput(a.alpha, v => { a.alpha = Math.max(0, Math.min(1, v)); auraTouch(a); }, '', true), '0 to 1');
      auraRow(g, 'When idle', auraCycle([[true, 'Hide until triggered'], [false, 'Keep a dimmed placeholder']], () => a.hideIdle, v => { a.hideIdle = v; auraTouch(a); }));
      auraRow(g, 'Glow', auraCheck('Always glow', a.glow, v => { a.glow = v; auraTouch(a); }));
    }
    if (a.type === 'icon') {
      auraRow(g, 'Tint', auraColor(a.color, v => { a.color = v; auraTouch(a); }), 'Border tint; white = none');
      auraRow(g, 'Timer', auraCheck('Show remaining time', a.showTime, v => { a.showTime = v; auraTouch(a); }));
      auraRow(g, 'Stacks', auraCheck('Show stacks / value', a.showStacks, v => { a.showStacks = v; auraTouch(a); }));
      auraRow(g, 'Sweep', auraCheck('Cooldown sweep', a.sweep, v => { a.sweep = v; auraTouch(a); }));
      auraRow(g, 'Desaturate', auraCheck('Grey the icon', a.desat, v => { a.desat = v; auraTouch(a); }));
    } else if (a.type === 'bar') {
      auraRow(g, 'Orientation', auraCycle([['h', 'Horizontal'], ['v', 'Vertical']], () => a.orient, v => { a.orient = v; auraTouch(a); }));
      auraRow(g, 'Bar colour', auraColor(a.barColor, v => { a.barColor = v; auraTouch(a); }));
      auraRow(g, 'Background', auraColor(a.bgColor, v => { a.bgColor = v; auraTouch(a); }));
      auraRow(g, 'Text colour', auraColor(a.textColor, v => { a.textColor = v; auraTouch(a); }));
      auraRow(g, 'Left text', auraTextInput(a.textL, v => { a.textL = v; auraTouch(a); }, '%n'));
      auraRow(g, 'Right text', auraTextInput(a.textR, v => { a.textR = v; auraTouch(a); }, '%p'));
      auraRow(g, '', auraTokenLegend());
      auraRow(g, 'Inverse', auraCheck('Fill as it runs out', a.inverse, v => { a.inverse = v; auraTouch(a); }));
    } else if (a.type === 'text') {
      auraRow(g, 'Text', auraTextInput(a.text, v => { a.text = v; auraTouch(a); }, '%p'));
      auraRow(g, '', auraTokenLegend());
      auraRow(g, 'Size', auraInput(a.fontSize, v => { a.fontSize = Math.max(8, Math.round(v)); auraTouch(a); }, '', true));
      auraRow(g, 'Colour', auraColor(a.textColor, v => { a.textColor = v; auraTouch(a); }));
      auraRow(g, 'Align', auraCycle([['left', 'Left'], ['center', 'Centre'], ['right', 'Right']], () => a.align, v => { a.align = v; auraTouch(a); }));
    } else if (a.type === 'ring') {
      auraRow(g, 'Ring colour', auraColor(a.ringColor, v => { a.ringColor = v; auraTouch(a); }));
      auraRow(g, 'Thickness', auraInput(a.ringWidth, v => { a.ringWidth = Math.max(2, Math.round(v)); auraTouch(a); }, '', true));
      auraRow(g, 'Text', auraTextInput(a.text, v => { a.text = v; auraTouch(a); }, '%p'));
      auraRow(g, '', auraTokenLegend());
      auraRow(g, 'Text colour', auraColor(a.textColor, v => { a.textColor = v; auraTouch(a); }));
      auraRow(g, 'Inverse', auraCheck('Fill as it runs out', a.inverse, v => { a.inverse = v; auraTouch(a); }));
    } else if (a.type === 'pips') {
      auraRow(g, 'Pips', auraInput(a.max, v => { a.max = Math.max(1, Math.min(30, Math.round(v))); auraTouch(a); }, '', true), 'The stack cap');
      auraRow(g, 'Pip colour', auraColor(a.pipColor, v => { a.pipColor = v; auraTouch(a); }));
    } else if (a.type === 'dyngroup') {
      auraRow(g, 'Grow', auraCycle(AURA_GROW, () => a.grow, v => { a.grow = v; auraTouch(a); }));
      if (a.grow === 'grid') auraRow(g, 'Per row', auraInput(a.gridCols, v => { a.gridCols = Math.max(1, Math.round(v)); auraTouch(a); }, '', true));
      auraRow(g, 'Spacing', auraInput(a.space, v => { a.space = Math.max(0, Math.round(v)); auraTouch(a); }, '', true));
      const sz = document.createElement('div'); sz.className = 'au-cell';
      sz.appendChild(document.createTextNode('w')); sz.appendChild(auraInput(a.childW, v => { a.childW = Math.max(8, Math.round(v)); auraTouch(a); }, '', true));
      sz.appendChild(document.createTextNode('h')); sz.appendChild(auraInput(a.childH, v => { a.childH = Math.max(8, Math.round(v)); auraTouch(a); }, '', true));
      auraRow(g, 'Child size', sz);
      auraRow(g, 'Sort', auraCycle(AURA_SORT, () => a.sort, v => { a.sort = v; auraTouch(a); }));
      auraRow(g, 'Limit', auraInput(a.limit, v => { a.limit = Math.max(0, Math.round(v)); auraTouch(a); }, '', true), '0 = no limit');
    } else if (a.type === 'group') {
      const h = document.createElement('div'); h.className = 'au-hint'; h.textContent = 'Static group: each child keeps its own position and size inside the window. Drag children on screen while this panel is open.'; auraRow(g, 'Layout', h);
    }
    body.appendChild(g);
    if (isGrp) {
      const kb = document.createElement('div'); kb.className = 'au-bar';
      const addK = auraMkBtn('+ Child', 'Add an aura inside this group', () => { auraMenu(addK, AURA_TYPES.map(t => ({ label: t[1], act: () => auraCreate(t[0], a.uid) }))); });
      kb.appendChild(addK);
      body.appendChild(kb);
    }
  }
  function auraEdTriggers(body, a) {
    const top = auraGrid();
    if (a.triggers.list.length > 1) {
      auraRow(top, 'Logic', auraCycle([['any', 'Any trigger active'], ['all', 'All triggers active']], () => a.triggers.logic, v => { a.triggers.logic = v; auraTouch(a); }));
      auraRow(top, 'Shows', auraCycle([[-1, 'First active trigger']].concat(a.triggers.list.map((t, i) => [i, 'Trigger ' + (i + 1)])), () => a.triggers.active, v => { a.triggers.active = +v; auraTouch(a); }), 'Which trigger supplies the icon, time and name');
    }
    body.appendChild(top);
    a.triggers.list.forEach((t, i) => {
      const box = document.createElement('div'); box.className = 'au-and';
      const head = document.createElement('div'); head.className = 'au-cust-head';
      const tag = document.createElement('span'); tag.className = 'au-and-tag'; tag.textContent = 'Trigger ' + (i + 1); head.appendChild(tag);
      head.appendChild(auraCycle(AURA_TRIG_SRCS, () => t.src, v => { t.src = v; t.op = (v === 'vital' || v === 'always' || v === 'invslots') ? 'any' : (v === 'ability' ? 'any' : 'active'); auraTouch(a); }));
      const ev = auraEvalCache[a.uid];
      const live = document.createElement('span'); live.className = 'au-gcount'; live.textContent = ev && ev.per[i] && ev.per[i].length ? 'active' : 'inactive'; head.appendChild(live);
      head.appendChild(auraMkBtn('×', 'Remove trigger', () => { a.triggers.list.splice(i, 1); auraTouch(a); }, 'au-sm au-del'));
      box.appendChild(head);
      const g = auraGrid();
      if (t.src === 'buff' || t.src === 'debuff') {
        const cell = document.createElement('div'); cell.className = 'au-cell';
        const ninp = auraNameInput(t, t.src, a, 'Type to search ' + (t.src === 'debuff' ? 'debuffs' : 'buffs') + '...');
        cell.appendChild(ninp);
        const pick = auraMkBtn('▾', 'Browse every ' + (t.src === 'debuff' ? 'debuff' : 'buff') + ' the game knows', () => {
          if (document.getElementById('sndMenu')) { if (typeof closeSoundMenu === 'function') closeSoundMenu(); return; }
          auraNamePicker(ninp, t.src, n => { t.name = n; ninp.value = n; auraTouch(a); });
        }, 'au-sm');
        cell.appendChild(pick);
        auraRow(g, 'Name', cell, 'Leave empty and tick Clones to show every buff');
        auraRow(g, 'When', auraCycle(AURA_OPS_PRES.concat(AURA_OPS_NUM.slice(1)), () => t.op, v => { t.op = v; auraTouch(a); }));
        if (AURA_OPS_PRES.every(o => o[0] !== t.op)) {
          auraRow(g, 'Compare', auraCycle(AURA_FIELDS_BUFF, () => t.field, v => { t.field = v; auraTouch(a); }));
          auraRow(g, 'Value', auraInput(t.val, v => { t.val = v; auraTouch(a); }, '', true));
        }
        auraRow(g, 'Clones', auraCheck('One display per matching buff (in a dynamic group)', t.clones, v => { t.clones = v; auraTouch(a); }));
      } else if (t.src === 'ability') {
        const cell = document.createElement('div'); cell.className = 'au-cell';
        if (typeof fetchAbilities === 'function') { try { fetchAbilities(); } catch (e) {} }
        const ainp = auraNameInput(t, 'ability', a, 'Type to search abilities...');
        cell.appendChild(ainp);
        const pick = auraMkBtn('▾', 'Browse every ability (your bar first)', () => {
          if (document.getElementById('sndMenu')) { if (typeof closeSoundMenu === 'function') closeSoundMenu(); return; }
          auraNamePicker(ainp, 'ability', n => { t.name = n; ainp.value = n; auraTouch(a); });
        }, 'au-sm');
        cell.appendChild(pick);
        auraRow(g, 'Ability', cell);
        auraRow(g, 'When', auraCycle([['any', 'Always (shows cooldown)'], ['active', 'On cooldown'], ['inactive', 'Ready']].concat(AURA_OPS_NUM.slice(1).map(o => [o[0], 'Cooldown ' + o[1]])), () => t.op, v => { t.op = v; auraTouch(a); }));
        if (['any', 'active', 'inactive'].indexOf(t.op) < 0) auraRow(g, 'Seconds', auraInput(t.val, v => { t.val = v; auraTouch(a); }, '', true));
      } else if (t.src === 'vital') {
        auraRow(g, 'Vital', auraCycle(AURA_VITALS, () => t.stat, v => { t.stat = v; auraTouch(a); }));
        auraRow(g, 'When', auraCycle(AURA_OPS_NUM, () => t.op, v => { t.op = v; auraTouch(a); }));
        if (t.op !== 'any') {
          auraRow(g, 'Compare', auraCycle([['time', 'Current value'], ['stacks', 'Percent']], () => t.field, v => { t.field = v; auraTouch(a); }));
          auraRow(g, 'Value', auraInput(t.val, v => { t.val = v; auraTouch(a); }, '', true));
        }
      } else if (t.src === 'panim' || t.src === 'nanim') {
        auraRow(g, 'Anim id', auraInput(t.anim, v => { t.anim = Math.round(v); auraTouch(a); }, '', true));
        if (t.src === 'nanim') auraRow(g, 'Clones', auraCheck('One display per NPC', t.clones, v => { t.clones = v; auraTouch(a); }));
      } else if (t.src === 'nearby') {
        auraRow(g, 'Kind', auraCycle([['npc', 'NPC'], ['player', 'Player'], ['object', 'Object']], () => t.kind, v => { t.kind = v; auraTouch(a); }));
        auraRow(g, 'Name', auraInput(t.name, v => { t.name = v; auraTouch(a); }, 'Name contains...'));
        auraRow(g, 'When', auraCycle([['active', 'Nearby'], ['inactive', 'Not nearby']], () => t.op, v => { t.op = v; auraTouch(a); }));
        auraRow(g, 'Clones', auraCheck('One display per match', t.clones, v => { t.clones = v; auraTouch(a); }));
      } else if (t.src === 'invslots') {
        auraRow(g, 'Compare', auraCycle([['time', 'Used slots'], ['stacks', 'Free slots']], () => t.field, v => { t.field = v; auraTouch(a); }));
        auraRow(g, 'When', auraCycle(AURA_OPS_NUM, () => t.op, v => { t.op = v; auraTouch(a); }));
        if (t.op !== 'any') auraRow(g, 'Value', auraInput(t.val, v => { t.val = v; auraTouch(a); }, '', true));
      } else if (t.src === 'invitem') {
        auraRow(g, 'Item', auraInput(t.name, v => { t.name = v; auraTouch(a); }, 'Item name contains...'));
        auraRow(g, 'When', auraCycle([['active', 'In inventory'], ['inactive', 'Not in inventory']].concat(AURA_OPS_NUM.slice(1).map(o => [o[0], 'Count ' + o[1]])), () => t.op, v => { t.op = v; auraTouch(a); }));
        if (['active', 'inactive'].indexOf(t.op) < 0) auraRow(g, 'Count', auraInput(t.val, v => { t.val = v; auraTouch(a); }, '', true));
      } else {
        const h = document.createElement('div'); h.className = 'au-hint'; h.textContent = 'Always active: use with Conditions, or for static text.'; auraRow(g, 'Note', h);
      }
      box.appendChild(g);
      body.appendChild(box);
    });
    if (a.triggers.list.length < 8) body.appendChild(auraMkBtn('+ Add trigger', '', () => { a.triggers.list.push(auraTrigDefaults('buff')); auraTouch(a); }, 'au-add'));
  }
  function auraEdConditions(body, a) {
    const hint = document.createElement('div'); hint.className = 'au-hint'; hint.textContent = 'While every check in a condition holds, its changes apply. Checks read a trigger state: time left, stacks, percent, name.'; body.appendChild(hint);
    a.conditions.forEach((c, ci) => {
      const box = document.createElement('div'); box.className = 'au-and';
      const head = document.createElement('div'); head.className = 'au-cust-head';
      const tag = document.createElement('span'); tag.className = 'au-and-tag'; tag.textContent = 'Condition ' + (ci + 1); head.appendChild(tag);
      head.appendChild(auraMkBtn('×', 'Remove condition', () => { a.conditions.splice(ci, 1); auraTouch(a); }, 'au-sm au-del'));
      box.appendChild(head);
      const checks = document.createElement('div'); checks.className = 'au-items';
      c.checks.forEach((ch, k) => {
        const row = document.createElement('div'); row.className = 'au-row';
        const lab = document.createElement('span'); lab.className = 'au-lab'; lab.textContent = k ? 'and' : 'if'; row.appendChild(lab);
        row.appendChild(auraCycle([[-1, 'Shown trigger']].concat(a.triggers.list.map((t, i) => [i, 'Trigger ' + (i + 1)])), () => ch.trig, v => { ch.trig = +v; auraTouch(a); }));
        row.appendChild(auraCycle(AURA_COND_VARS, () => ch.v, v => { ch.v = v; auraTouch(a); }));
        if (ch.v === 'show') row.appendChild(auraCycle([[true, 'is active'], [false, 'is not active']], () => !(ch.val === false || ch.val === 0 || ch.val === 'false' || ch.val === '0'), v => { ch.val = v; auraTouch(a); }));
        else if (ch.v === 'name') { row.appendChild(auraCycle([['=', 'contains'], ['!=', 'does not contain']], () => ch.op === '!=' ? '!=' : '=', v => { ch.op = v; auraTouch(a); })); row.appendChild(auraInput(ch.val, v => { ch.val = v; auraTouch(a); }, 'text')); }
        else { row.appendChild(auraCycle(AURA_COND_OPS, () => ch.op, v => { ch.op = v; auraTouch(a); })); row.appendChild(auraInput(ch.val, v => { ch.val = v; auraTouch(a); }, '', true)); }
        row.appendChild(auraMkBtn('×', '', () => { c.checks.splice(k, 1); auraTouch(a); }, 'au-sm au-del'));
        checks.appendChild(row);
      });
      box.appendChild(checks);
      box.appendChild(auraMkBtn('+ check', '', () => { c.checks.push({ trig: -1, v: 'remaining', op: '<', val: 5 }); auraTouch(a); }, 'au-sm'));
      const changes = document.createElement('div'); changes.className = 'au-items';
      c.changes.forEach((ch, k) => {
        const row = document.createElement('div'); row.className = 'au-row';
        const lab = document.createElement('span'); lab.className = 'au-lab'; lab.textContent = k ? 'and' : 'then'; row.appendChild(lab);
        row.appendChild(auraCycle(AURA_COND_PROPS, () => ch.prop, v => { ch.prop = v; ch.val = (v === 'glow' || v === 'desat' || v === 'hide') ? true : (v === 'alpha' || v === 'scale') ? 1 : v === 'color' ? '#ff5a5a' : v === 'sound' ? 'alert 1' : ''; auraTouch(a); }));
        if (ch.prop === 'color') row.appendChild(auraColor(String(ch.val), v => { ch.val = v; auraTouch(a); }));
        else if (ch.prop === 'glow' || ch.prop === 'desat' || ch.prop === 'hide') row.appendChild(auraCycle([[true, 'on'], [false, 'off']], () => !!ch.val, v => { ch.val = v; auraTouch(a); }));
        else if (ch.prop === 'alpha' || ch.prop === 'scale') row.appendChild(auraInput(ch.val, v => { ch.val = v; auraTouch(a); }, '', true));
        else if (ch.prop === 'sound') row.appendChild(auraSoundSel(() => ch.val, v => { ch.val = v; auraTouch(a); }));
        else row.appendChild(auraTextInput(String(ch.val), v => { ch.val = v; auraTouch(a); }, 'text with %p %n ...'));
        row.appendChild(auraMkBtn('×', '', () => { c.changes.splice(k, 1); auraTouch(a); }, 'au-sm au-del'));
        changes.appendChild(row);
      });
      box.appendChild(changes);
      box.appendChild(auraMkBtn('+ change', '', () => { c.changes.push({ prop: 'color', val: '#ff5a5a' }); auraTouch(a); }, 'au-sm'));
      body.appendChild(box);
    });
    if (a.conditions.length < 12) body.appendChild(auraMkBtn('+ Add condition', '', () => { a.conditions.push({ checks: [{ trig: -1, v: 'remaining', op: '<', val: 5 }], changes: [{ prop: 'color', val: '#ff5a5a' }] }); auraTouch(a); }, 'au-add'));
  }
  function auraEdActions(body, a) {
    [['show', 'On show'], ['hide', 'On hide']].forEach(([k, lab]) => {
      const act = a.actions[k];
      const box = document.createElement('div'); box.className = 'au-and';
      const tag = document.createElement('div'); tag.className = 'au-and-tag'; tag.textContent = lab; box.appendChild(tag);
      const g = auraGrid();
      auraRow(g, 'Sound', auraSoundSel(() => act.sound, v => { act.sound = v; auraTouch(a); }));
      auraRow(g, 'Flash', auraCheck('Flash the game window', act.flash, v => { act.flash = v; auraTouch(a); }));
      auraRow(g, 'Message', auraCycle(AURA_NOTIFY, () => act.notify, v => { act.notify = v; auraTouch(a); }));
      if (act.notify !== 'none') { auraRow(g, 'Text', auraTextInput(act.msg, v => { act.msg = v; auraTouch(a); }, 'Defaults to the aura name')); auraRow(g, '', auraTokenLegend()); }
      box.appendChild(g); body.appendChild(box);
    });
  }
  function auraEdAnim(body, a) {
    const g = auraGrid();
    auraRow(g, 'Start', auraCycle(AURA_ANIM_START, () => a.anim.start, v => { a.anim.start = v; auraTouch(a); }), 'Plays when the aura appears');
    auraRow(g, 'Main', auraCycle(AURA_ANIM_MAIN, () => a.anim.main, v => { a.anim.main = v; auraTouch(a); }), 'Loops while shown');
    auraRow(g, 'Finish', auraCycle(AURA_ANIM_FINISH, () => a.anim.finish, v => { a.anim.finish = v; auraTouch(a); }), 'Plays when it disappears');
    body.appendChild(g);
  }
  function auraEdLoad(body, a) {
    const g = auraGrid();
    auraRow(g, 'Combat', auraCycle(AURA_COMBAT, () => a.load.combat, v => { a.load.combat = v; auraTouch(a); }), 'Read from your own combat timer in the scene');
    body.appendChild(g);
    const h = document.createElement('div'); h.className = 'au-hint'; h.textContent = 'Load rules decide whether the aura runs at all; triggers decide whether it shows.'; body.appendChild(h);
  }
  function auraEdExport(body, a) {
    const ta = document.createElement('textarea'); ta.className = 'au-ta'; ta.readOnly = true; ta.value = auraExportString(a.uid);
    ta.addEventListener('click', () => { try { ta.select(); } catch (e) {} });
    ta.addEventListener('keydown', e => e.stopPropagation());
    body.appendChild(ta);
    const row = document.createElement('div'); row.className = 'au-bar';
    row.appendChild(auraMkBtn('Copy', 'Copy to clipboard', () => { try { ta.select(); document.execCommand('copy'); } catch (e) {} }));
    const h = document.createElement('div'); h.className = 'au-hint'; h.textContent = 'Share this string; Import on another install recreates the aura and everything inside it.'; row.appendChild(h);
    body.appendChild(row);
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { auraAnyHudVisible, auraApplyDurablePrefs, auraColor, auraGet, auraGroupTabs, auraIsHudTab, auraNeedsPoll, fetchAuras, renderAuraGroup });
registerTab({ id: 'auras', render: renderAuras });
})();
