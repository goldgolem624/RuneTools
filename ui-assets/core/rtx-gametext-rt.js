// rtx-gametext-rt.js: the runtime under the game's own text scripts (core/rtx-gametext.js, generated).
//
// A translated script calls into R for every var, inventory slot, param, enum and database row it
// reads, and for number formatting with the engine's integer semantics. A value not held yet is
// recorded in `needs` and the evaluation stops with Pending; the caller then runs fill(), which
// fetches exactly what was asked for, and evaluates again. Live data (vars, inventories, player,
// clock) is re-read by refresh() each tick so stacks, energy and charges move; cache data is kept.
// Interface ops draw nothing: the line appender collects into R.lines instead.
//
//   gameText.evaluate(root, args, ctx) -> { text, pending }    root: 'buff' | 'item' | ...
//   gameText.text(root, args, ctx)     -> Promise<string>       evaluate + fill until settled
//   gameText.fill() / refresh()        -> Promise; onChange(cb) fires after either changes state
//   gameText.render(el, text), gameText.plain(text)             the game's markup into DOM / plain text
const gameText = (function () {
  function Pending() {}
  function Missing(what) { this.what = what; }
  const STRING_PARAMS = { 2794: 1, 2795: 1 };   // the string params the scripts read most: name, description
  const RUNEDAY_EPOCH_MS = Date.UTC(2002, 1, 27);

  const R = {
    Pending, Missing,
    vars:  { vb: new Map(), vp: new Map(), vc: new Map() },
    live:  { player: null, clock: null, quests: null, varcStrings: null, inv: new Map(), itemExtra: new Map(), achievements: new Map() },
    cache: { structs: new Map(), items: new Map(), itemParams: new Map(), paramDefs: new Map(), enums: new Map(), tables: new Map() },
    needs: { vb: new Set(), vp: new Set(), vc: new Set(), structs: new Set(), items: new Set(), itemParams: new Set(), paramDefs: new Set(),
             enums: new Set(), tables: new Set(), inv: new Set(), itemExtra: new Set(), achievements: new Set(),
             player: false, clock: false, quests: false, varcStrings: false },
    ctx: { count: 0, vc: null },
    lines: [], arrays: new Map(), dbList: [], dbCursor: 0,

    // ---- vars -------------------------------------------------------------------------------
    varGet(kind, id) {
      if (kind === 'vc' && this.ctx.vc && Object.prototype.hasOwnProperty.call(this.ctx.vc, id)) return this.ctx.vc[id] | 0;
      const m = this.vars[kind];
      if (m.has(id)) return m.get(id) | 0;
      this.needs[kind].add(id); throw new Pending();
    },
    vb(id) { return this.varGet('vb', id); },
    vp(id) { return this.varGet('vp', id); },
    vc(id) { return this.varGet('vc', id); },
    // string client vars come from one dump of all of them, keyed "2:<id>"
    vcs(id) {
      const m = this.liveGet('varcStrings');
      const v = m['2:' + (id | 0)];
      return v == null ? (m[id | 0] == null ? '' : String(m[id | 0])) : String(v);
    },
    varRef(ref) { ref |= 0; return (ref >> 24) === 1 ? this.vb(ref & 0xFFFFFF) : this.vp(ref); },
    lookup(kind, id) {
      const c = this.cache[kind];
      if (c.has(id)) return c.get(id);
      this.needs[kind].add(id); throw new Pending();
    },
    liveGet(kind) {
      if (this.live[kind] !== null) return this.live[kind];
      this.needs[kind] = true; throw new Pending();
    },

    // ---- formatting and arithmetic, with the engine's integer semantics -----------------------
    s(v) { return v == null ? '' : String(v); },
    str(v) { return String(v | 0); },
    strLoc(v) { return (v | 0).toLocaleString('en-US'); },
    cat(a, b) { return String(a == null ? '' : a) + String(b == null ? '' : b); },
    len(v) { return String(v == null ? '' : v).length; },
    mod(a, b) { return b ? ((a | 0) % (b | 0)) : 0; },
    idiv(a, b) { return b ? Math.trunc((a | 0) / (b | 0)) : 0; },
    scale(v, from, to) { return this.idiv(Math.imul(v | 0, to | 0), from | 0); },
    max(a, b) { return Math.max(a | 0, b | 0); },
    min(a, b) { return Math.min(a | 0, b | 0); },
    pow(a, b) { return Math.pow(a | 0, b | 0) | 0; },
    abs(a) { return Math.abs(a | 0); },
    and(a, b) { return (a | 0) & (b | 0); },
    testbit(v, b) { return ((v | 0) >> (b | 0)) & 1; },
    setbit(v, b) { return (v | 0) | (1 << (b | 0)); },
    clearbit(v, b) { return (v | 0) & ~(1 << (b | 0)); },
    eq(a, b) {
      if (typeof a === 'number' && typeof b === 'number') return a === b;
      if (a !== null && typeof a === 'object' || b !== null && typeof b === 'object') return a === b;
      return String(a) === String(b);
    },
    key(x) { return x; },
    lang() { return 0; },
    colTag(rgb) { return '<col=' + ((rgb | 0) & 0xFFFFFF).toString(16).padStart(6, '0') + '>'; },
    textSwitch(c, a, b) { return (c | 0) === 1 ? a : b; },
    strcmp(a, b) { a = String(a); b = String(b); return a < b ? -1 : a > b ? 1 : 0; },

    // ---- time -------------------------------------------------------------------------------
    dateMinutes() { return Math.floor(Date.now() / 60000); },
    dateRuneday() { return Math.floor((Date.now() - RUNEDAY_EPOCH_MS) / 86400000); },
    dateMinutesFromRuneday(rd) { return Math.imul((rd | 0) + 11745, 1440); },
    dateRunedayToDate(rd) { const d = new Date(RUNEDAY_EPOCH_MS + (rd | 0) * 86400000); return [d.getUTCDate(), d.getUTCMonth() + 1, d.getUTCFullYear()]; },
    clientClock() { return this.liveGet('clock') | 0; },

    // ---- the player -------------------------------------------------------------------------
    skill(id, col) { const p = this.liveGet('player'); const s = p && p.skills && p.skills[id | 0]; return s ? (s[col] | 0) : 0; },
    statBase(id) { return this.skill(id, 0); },
    stat(id) { return this.skill(id, 1); },
    statXp(id) { return this.skill(id, 2); },
    mapMembers() { return 1; },
    playerMember() { return 1; },
    mapWorld() { return 0; },
    questStatus(id) { const q = this.liveGet('quests'); const row = Array.isArray(q) ? q.find(r => r.id === (id | 0)) : null; return row ? (row.status | 0) : 0; },
    questFinished(id) { return this.questStatus(id) === 2 ? 1 : 0; },
    questStarted(id) { return this.questStatus(id) >= 1 ? 1 : 0; },
    random(n) { return n > 0 ? Math.floor(Math.random() * (n | 0)) : 0; },
    // world list entry: [flags, name, x, activity, y, z, address]; no flag set means an ordinary world
    worldSpecific() { return [0, '', 0, '', 0, 0, '']; },
    achievement(id) {
      id |= 0;
      if (this.live.achievements.has(id)) return this.live.achievements.get(id);
      this.needs.achievements.add(id); throw new Pending();
    },
    achievementReqState(id) { const a = this.achievement(id); return a && a.complete ? 1 : 0; },
    achievementName(id) { const a = this.achievement(id); return (a && a.name) || ''; },

    // ---- inventories: container JSON { present, count, cap, items: [[slot, id, stack, name], ...] } ------------
    inv(id) {
      id |= 0;
      if (this.live.inv.has(id)) return this.live.inv.get(id);
      this.needs.inv.add(id); throw new Pending();
    },
    invObj(inv, slot) { const c = this.inv(inv); const it = (c.items || []).find(e => e[0] === (slot | 0)); return it ? (it[1] | 0) : -1; },
    invNum(inv, slot) { const c = this.inv(inv); const it = (c.items || []).find(e => e[0] === (slot | 0)); return it ? (it[2] | 0) : 0; },
    invTotal(inv, item) { const c = this.inv(inv); let n = 0; for (const e of (c.items || [])) if ((e[1] | 0) === (item | 0)) n += e[2] | 0; return n | 0; },
    invSize(inv) { return this.inv(inv).cap | 0; },
    invFree(inv) { const c = this.inv(inv); return ((c.cap | 0) - (c.count | 0)) | 0; },
    invTotalParam(inv, param) {
      const c = this.inv(inv); let n = 0;
      for (const e of (c.items || [])) { const v = this.itemParam(e[1] | 0, param | 0); if (typeof v === 'number') n += Math.imul(v, e[2] | 0); }
      return n | 0;
    },
    invVar(inv, slot, varobj) {
      const item = this.invObj(inv, slot);
      if (item < 0) return 0;
      const k = (inv | 0) + ':' + item;
      if (!this.live.itemExtra.has(k)) { this.needs.itemExtra.add(k); throw new Pending(); }
      const x = this.live.itemExtra.get(k);
      const v = x && x.key && x.key[varobj | 0];
      return v == null ? 0 : (v | 0);
    },
    invOtherVar(inv, slot, varobj) { return this.invVar(inv, slot, varobj); },

    // ---- cache lookups ----------------------------------------------------------------------
    // A param the struct or item does not set answers with the param definition's own default (0
    // or "" when the definition names none), which is what the scripts' "!= default" checks rely on.
    paramOf(st, p) {
      if (st.strs && Object.prototype.hasOwnProperty.call(st.strs, p)) return st.strs[p];
      if (st.ints && Object.prototype.hasOwnProperty.call(st.ints, p)) return st.ints[p] | 0;
      const def = this.lookup('paramDefs', p | 0);
      if (def && Object.prototype.hasOwnProperty.call(def, 'str')) return String(def.str);
      if (def && Object.prototype.hasOwnProperty.call(def, 'int')) return def.int | 0;
      return (STRING_PARAMS[p] || (def && def.type === 36)) ? '' : 0;
    },
    structParam(sid, p) { return this.paramOf(this.lookup('structs', sid | 0), p); },
    itemParam(item, p) { return this.paramOf(this.lookup('itemParams', item | 0), p); },
    item(item) { return this.lookup('items', item | 0) || {}; },
    itemName(item) { return this.item(item).name || ''; },
    itemDesc(item) { return this.item(item).desc || ''; },
    itemCategory(item) { const v = this.item(item).category; return v == null ? -1 : (v | 0); },
    itemMembers(item) { return this.item(item).members ? 1 : 0; },
    itemWearpos(item) { const v = this.item(item).wearpos; return v == null ? -1 : (v | 0); },
    itemWearpos2(item) { const v = this.item(item).wearpos2; return v == null ? -1 : (v | 0); },
    itemUncert(item) { const v = this.item(item).unnoted; return (v == null || v < 0) ? (item | 0) : (v | 0); },
    itemHasVarobj(item, varobj) { const vs = this.item(item).varobjs; return Array.isArray(vs) && vs.indexOf(varobj | 0) >= 0 ? 1 : 0; },
    enumOf(id) { return this.lookup('enums', id | 0); },
    enumValue(kt, vt, enumId, key) { const v = this.enumOf(enumId)[key]; return v == null ? '' : v; },
    enumString(enumId, key) { const v = this.enumOf(enumId)[key]; return v == null ? '' : String(v); },
    enumHas(enumId, key) { return Object.prototype.hasOwnProperty.call(this.enumOf(enumId), key) ? 1 : 0; },
    enumReverse(kt, vt, enumId, value) { const e = this.enumOf(enumId); for (const k in e) if (this.eq(e[k], value)) return k | 0; return -1; },
    enumCount(enumId) { return Object.keys(this.enumOf(enumId)).length; },
    // database rows: table and column packed into one id, table in the high bits, column in bits 4..11
    dbFind(tableCol, key) {
      const table = tableCol >> 12, col = (tableCol & 0xFFF) >> 4;
      const rows = this.lookup('tables', table);
      this.dbList = rows.filter(r => (r.i && r.i[col] && r.i[col].indexOf(key | 0) >= 0) || (r.s && r.s[col] && r.s[col].indexOf(String(key)) >= 0));
      this.dbCursor = 0;
    },
    dbNext() { return this.dbCursor < this.dbList.length ? this.dbList[this.dbCursor++] : -1; },
    dbField(row, tableCol, idx) {
      const col = (tableCol & 0xFFF) >> 4;
      if (!row || row === -1) return '';
      if (row.s && row.s[col] && row.s[col][idx] != null) return row.s[col][idx];
      if (row.i && row.i[col] && row.i[col][idx] != null) return row.i[col][idx] | 0;
      return '';
    },
    dbFieldCount(row, tableCol) {
      const col = (tableCol & 0xFFF) >> 4;
      if (!row || row === -1) return 0;
      return ((row.s && row.s[col]) || (row.i && row.i[col]) || []).length;
    },

    // ---- interfaces: nothing is drawn; the line appender collects --------------------------------
    ifClear() { this.lines = []; return 0; },
    ifNone() { return 0; },
    ifNoop() { return 0; },
    ifNeg() { return -1; },
    ifZero() { return 0; },
    // script 7238 in its declared order: (colour, style, layout, component, row, text); answers the next row
    appendLine(colour, style, layout, comp, row, text) { this.lines.push(String(text == null ? '' : text)); return ((row | 0) + 1) | 0; },
    // script arrays
    arrDef(n, size) { this.arrays.set(n | 0, new Array(Math.max(0, size | 0)).fill(0)); return 0; },
    arrSet(n, i, v) { const a = this.arrays.get(n | 0); if (a && i >= 0 && i < a.length) a[i | 0] = v; return 0; },
    arrGet(n, i) { const a = this.arrays.get(n | 0); return a && i >= 0 && i < a.length ? a[i | 0] : 0; },

    // ---- scripts ----------------------------------------------------------------------------
    call(sid, args) {
      const f = window.GAME_TEXT && GAME_TEXT.scripts[sid];
      if (!f) throw new Missing('script ' + sid);
      return f.apply(null, args);
    },
    missing(name) { throw new Missing(name); },
    stackCount() { return this.ctx.count | 0; },
  };

  let bound = false;
  function evaluate(root, args, ctx) {
    if (!window.GAME_TEXT) return { text: '', pending: false };
    if (!bound) { GAME_TEXT.bind(R); bound = true; }
    const sid = GAME_TEXT.roots[root];
    const f = sid && GAME_TEXT.scripts[sid];
    if (!f) return { text: '', pending: false };
    R.ctx = Object.assign({ count: 0, vc: null }, ctx || {});
    R.lines = []; R.arrays = new Map();
    try {
      const out = f.apply(null, args || []);
      const text = (typeof out === 'string') ? out : R.lines.join('<br>');
      return { text: text, pending: false };
    } catch (e) {
      if (e instanceof Pending) return { text: null, pending: true };
      if (!(e instanceof Missing)) console.warn('game text ' + root + ' ' + JSON.stringify(args) + ': ' + (e && e.message));
      return { text: '', pending: false };
    }
  }

  const listeners = [];
  function onChange(cb) { if (typeof cb === 'function') listeners.push(cb); }
  function changed() { for (const cb of listeners) { try { cb(); } catch (e) {} } }
  const parse = (t, dflt) => { try { const v = JSON.parse(t); return v == null ? dflt : v; } catch (e) { return dflt; } };

  // Fetch whatever the last evaluations asked for.
  let filling = false;
  async function fill() {
    if (filling) return;
    const n = R.needs;
    const any = Object.keys(n).some(k => n[k] instanceof Set ? n[k].size : n[k]);
    if (!any) return;
    filling = true;
    try {
      const take = k => { const a = Array.from(n[k]); n[k].clear(); return a; };
      const vb = take('vb'), vp = take('vp'), vc = take('vc');
      const jobs = [];
      const each = (ids, fn) => { for (const id of ids) jobs.push(fn(id).catch(() => {})); };
      if (vb.length) jobs.push(rtxData.raw('state.varbitsCsv', vb.join(',')).then(t => { const v = parse(t, {}); for (const id of vb) R.vars.vb.set(id, v[id] | 0); }).catch(() => {}));
      if (vp.length) jobs.push(rtxData.raw('state.varps', vp.join(',')).then(t => { const v = parse(t, {}); for (const id of vp) R.vars.vp.set(id, v[id] | 0); }).catch(() => {}));
      if (vc.length) jobs.push(rtxData.raw('state.varcs', vc).then(v => { v = v || {}; for (const id of vc) R.vars.vc.set(id, v[id] | 0); }).catch(() => {}));
      each(take('structs'),    id => rtxData.raw('cache.structParams', id).then(t => R.cache.structs.set(id, parse(t, {}))));
      each(take('itemParams'), id => rtxData.raw('cache.itemParams', id).then(t => R.cache.itemParams.set(id, parse(t, {}))));
      {   // the first param definition asked for brings every one the scripts name, in one pass
        let ids = take('paramDefs');
        if (ids.length && !R.paramsPrefetched) {
          R.paramsPrefetched = true;
          const all = new Set(ids); for (const p of ((window.GAME_TEXT && GAME_TEXT.params) || [])) if (!R.cache.paramDefs.has(p)) all.add(p);
          ids = Array.from(all);
        }
        each(ids, id => rtxData.raw('cache.paramDef', id).then(t => R.cache.paramDefs.set(id, parse(t, {}))));
      }
      each(take('items'),      id => rtxData.raw('cache.itemInfo', id).then(t => R.cache.items.set(id, parse(t, {}))));
      each(take('enums'),      id => rtxData.raw('cache.enumInfo', id).then(t => R.cache.enums.set(id, parse(t, {}))));
      each(take('tables'),     id => rtxData.raw('cache.dbRows', id).then(t => { const rows = parse(t, []); R.cache.tables.set(id, Array.isArray(rows) ? rows : []); }));
      each(take('inv'),        id => rtxData.raw('state.container', id).then(t => R.live.inv.set(id, parse(t, { items: [] }))));
      each(take('itemExtra'),  k => { const [c, i] = k.split(':').map(Number); return rtxData.raw('state.itemExtra', c, i).then(t => R.live.itemExtra.set(k, parse(t, {}))); });
      each(take('achievements'), id => rtxData.raw('state.achievement', id).then(a => R.live.achievements.set(id, (a && typeof a === 'object') ? a : parse(a, null))));
      if (n.player) { n.player = false; jobs.push(rtxData.raw('state.player').then(t => { R.live.player = parse(t, {}); }).catch(() => {})); }
      if (n.quests) { n.quests = false; jobs.push(rtxData.raw('state.quests').then(t => { R.live.quests = parse(t, []); }).catch(() => {})); }
      if (n.clock)  { n.clock = false;  jobs.push(rtxData.raw('state.buffs').then(t => { const b = parse(t, {}); R.live.clock = (b && b.cycles) | 0; }).catch(() => {})); }
      if (n.varcStrings) { n.varcStrings = false; jobs.push(rtxData.raw('state.varcStringsAll').then(t => { R.live.varcStrings = parse(t, {}); }).catch(() => {})); }
      await Promise.all(jobs);
    } catch (e) { /* asked again on the next pass */ }
    filling = false;
    changed();
  }

  // Re-read the live data in use: vars, inventories, item vars, the player, the clock.
  let refreshing = false;
  async function refresh() {
    if (refreshing) return false;
    refreshing = true;
    let moved = false;
    const upd = (m, v) => { for (const id of m.keys()) { const nv = v[id] | 0; if (m.get(id) !== nv) { m.set(id, nv); moved = true; } } };
    try {
      if (R.vars.vb.size) upd(R.vars.vb, parse(await rtxData.raw('state.varbitsCsv', Array.from(R.vars.vb.keys()).join(',')), {}));
      if (R.vars.vp.size) upd(R.vars.vp, parse(await rtxData.raw('state.varps', Array.from(R.vars.vp.keys()).join(',')), {}));
      if (R.vars.vc.size) upd(R.vars.vc, (await rtxData.raw('state.varcs', Array.from(R.vars.vc.keys()))) || {});
      for (const id of R.live.inv.keys()) {
        const nv = parse(await rtxData.raw('state.container', id), { items: [] });
        if (JSON.stringify(nv.items) !== JSON.stringify((R.live.inv.get(id) || {}).items)) { R.live.inv.set(id, nv); moved = true; }
      }
      for (const k of R.live.itemExtra.keys()) {
        const [c, i] = k.split(':').map(Number);
        const nv = parse(await rtxData.raw('state.itemExtra', c, i), {});
        if (JSON.stringify(nv.key) !== JSON.stringify((R.live.itemExtra.get(k) || {}).key)) { R.live.itemExtra.set(k, nv); moved = true; }
      }
      if (R.live.player !== null) {
        const p = parse(await rtxData.raw('state.player'), {});
        if (JSON.stringify(p.skills) !== JSON.stringify(R.live.player.skills)) { R.live.player = p; moved = true; }
      }
      if (R.live.clock !== null) { const b = parse(await rtxData.raw('state.buffs'), {}); R.live.clock = (b && b.cycles) | 0; }
      if (R.live.varcStrings !== null) {
        const s = parse(await rtxData.raw('state.varcStringsAll'), {});
        if (JSON.stringify(s) !== JSON.stringify(R.live.varcStrings)) { R.live.varcStrings = s; moved = true; }
      }
    } catch (e) {}
    refreshing = false;
    if (moved) changed();
    return moved;
  }

  // Evaluate and fetch until the text settles, for callers that want one answer. A script learns
  // what it needs one layer at a time (an item's params, then an enum those name, then a table),
  // so the rounds are generous; null comes back only if it never settles.
  async function text(root, args, ctx, tries) {
    let last = null;
    for (let i = 0; i < (tries || 120); i++) {
      const r = evaluate(root, args, ctx);
      if (!r.pending) return r.text;
      last = r;
      await fill();
    }
    console.warn('game text ' + root + ' ' + JSON.stringify(args) + ' did not settle; still waiting for ' + pendingSummary());
    return null;
  }
  function pendingSummary() {
    const n = R.needs, parts = [];
    for (const k of Object.keys(n)) { const v = n[k]; if (v instanceof Set ? v.size : v) parts.push(k + (v instanceof Set ? '[' + Array.from(v).slice(0, 4).join(',') + ']' : '')); }
    return parts.join(' ') || 'nothing';
  }

  // The game's text markup into DOM: <br>, <col=RRGGBB>...</col>, <sprite=N>, <nbsp>; anything else is dropped.
  function render(el, txt, spriteFn) {
    el.textContent = '';
    let cur = el;
    const re = /<[^>]*>/g;
    let last = 0, m;
    const put = t => { if (t) cur.appendChild(document.createTextNode(t)); };
    while ((m = re.exec(txt)) !== null) {
      put(txt.slice(last, m.index)); last = re.lastIndex;
      const tag = m[0].slice(1, -1);
      let mm;
      if (/^br\s*\/?$/i.test(tag)) { cur.appendChild(document.createElement('br')); continue; }
      if ((mm = /^col=([0-9a-fA-F]{1,6})$/.exec(tag))) {
        const span = document.createElement('span');
        span.style.color = '#' + parseInt(mm[1], 16).toString(16).padStart(6, '0');
        cur.appendChild(span); cur = span; continue;
      }
      if (/^\/col$/i.test(tag)) { if (cur !== el) cur = cur.parentNode; continue; }
      if ((mm = /^sprite=(\d+)$/.exec(tag))) {
        const sp = document.createElement('span'); sp.className = 'gt-spr';
        cur.appendChild(sp); if (spriteFn) spriteFn(sp, mm[1] | 0); continue;
      }
      if (/^nbsp$/i.test(tag)) { put(' '); continue; }
    }
    put(txt.slice(last));
  }
  const plain = t => String(t == null ? '' : t).replace(/<br\s*\/?>/gi, '\n').replace(/<nbsp>/gi, ' ').replace(/<[^>]*>/g, '');

  return { R, evaluate, fill, refresh, text, onChange, render, plain, Pending, Missing };
})();
