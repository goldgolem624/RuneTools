// RuneToolsX panel: Fight Logs (fights recorded by the combat recorder: picker, summary strip, damage
// tables, health chart, buff uptimes, rotation strip, event list). Every number comes from
// panel_fights_stats.js so the website page shows the same figures. Without the recorder bridge
// functions (older launcher) the panel shows one line and nothing else.
(function () {

  const FL_TABS = [['overview', 'Overview'], ['dealt', 'Dealt'], ['taken', 'Taken'], ['health', 'Timeline'], ['buffs', 'Buffs'], ['gear', 'Gear'], ['casts', 'Casts'], ['mechs', 'Mechanics'], ['events', 'Events']];
  const FL_EV_CATS = [['hit', 'Hits'], ['cast', 'Casts'], ['buff', 'Buffs'], ['mech', 'Mechanics'], ['vitals', 'Vitals'], ['anim', 'Animations'], ['fx', 'Effects'], ['target', 'Targets'], ['tracker', 'Trackers'], ['other', 'Other']];
  const FL_EV_CAT = { hit: 'hit', cast: 'cast', buff: 'buff', channel: 'cast', mech: 'mech', lp: 'vitals', adren: 'vitals', prayer: 'vitals', bar: 'vitals', stat: 'vitals', anim: 'anim', gfx: 'fx', proj: 'fx', sound: 'fx', target: 'target', tracker: 'tracker' };
  const FL_ROWS = 400, FL_COLORS = ['#e0b34c', '#4cc0c0', '#c98cf0', '#e06c6c', '#7f9fbf', '#67c07a'], FL_MECH = '#ff9f43';
  const FL_STYLE_COLOR = { melee: '#e06c6c', ranged: '#67c07a', magic: '#7f9fbf', necromancy: '#c98cf0', conjure: '#9a7fd0', typeless: '#9aa0ad', poison: '#5fd07a' };
  const fl = { rows: null, rowsAt: 0, char: '', logId: '', fight: -2, tab: 'overview', log: null, loading: '', live: null, liveId: '', liveSeq: 0, liveAt: 0,
               rec: null, recAt: 0, zoom: null, hl: 0, mhl: '', evFilt: { hit: true, cast: true, buff: true, mech: true, vitals: false, anim: false, fx: false, target: true, tracker: false, other: true },
               evSearch: '', evShow: FL_ROWS, sig: '', pickSig: '', status: '', confirmDel: 0, drag: null, hide: {}, vis: null };
  const FL_VIS = [['private', 'Private'], ['unlisted', 'Unlisted'], ['public', 'Public']];
  const flVisName = v => (FL_VIS.find(x => x[0] === v) || FL_VIS[0])[1];
  const FL_SPR = new Map(), FL_SPR_PENDING = new Set();
  const FL_TAC = new Map(), FL_TAC_PENDING = new Set(), FL_TAC_MISS = new Map();
  const S = () => window.combatStats;
  const FL_CSS = '.fl-chart .mk { stroke: ' + FL_MECH + '; stroke-width: 1.5; }\n.fl-chart .mk.hl { stroke: var(--accent-hi); stroke-width: 2.5; }\n' +
    '.fl-gear .g { height: 16px; }\n.fl-gear .g i { background: #6f8fb8; color: #0b0d12; font: 600 10px/16px var(--font-mono); padding-left: 4px; overflow: hidden; white-space: nowrap; text-overflow: ellipsis; box-sizing: border-box; }\n.fl-gear .g i.k1 { background: #b89a5c; }\n.fl-gear .g i.k2 { background: #7fae8a; }\n' +
    '.fl-gear .g i.k3 { background: #a77fb0; }\n.fl-gear .g i.k4 { background: #c27f6f; }\n.fl-gear .g i.k5 { background: #6fa9ad; }\n' +
    '.fl-gear .g i .fl-ico { width: 14px; height: 14px; vertical-align: -3px; margin-right: 4px; background-color: transparent; background-size: contain; background-position: center; background-repeat: no-repeat; }\n' +
    '.fl-inv { display: grid; grid-template-columns: repeat(4, 40px); gap: 3px; margin: 2px 0 6px; }\n' +
    '.fl-inv > span { position: relative; height: 36px; border-radius: 4px; background-color: var(--bg-elev-2); background-repeat: no-repeat; background-position: center; background-size: 36px 32px; }\n' +
    '.fl-inv > span b { position: absolute; left: 2px; top: 1px; font: 600 9.5px var(--font-mono); color: #ffe066; text-shadow: 0 1px 1px #000; }\n' +
    '.fl-chg { display: inline-flex; align-items: center; gap: 4px; min-width: 0; }\n.fl-chg .fl-ico { width: 14px; height: 14px; background-color: transparent; background-size: contain; background-position: center; background-repeat: no-repeat; }\n' +
    '.fl-table.inv { --fl-cols: 52px 30px minmax(0, 3fr); }\n' +
    '.fl-gear .fl-perks { grid-column: 1 / -1; margin: -2px 0 3px; padding-left: 2px; font: 500 10.5px var(--font-mono); color: var(--text-dim, #9aa3b2); white-space: normal; }\n' +
    '.fl-perkd { display: block; font: 500 10.5px var(--font-mono); color: var(--text-dim, #9aa3b2); padding-left: 2px; white-space: normal; }\n' +
    '.fl-vis .pet-dd-btn { font: 600 10px var(--font-mono); letter-spacing: .06em; text-transform: uppercase; padding: 4px 22px 4px 9px; white-space: nowrap; }\n.fl-vis .pet-dd-pop { left: auto; right: 0; }\n' +
    '.fl-vis .pet-dd-btn::before { content: "Upload: "; }\n.fl-narrow .fl-vis .pet-dd-btn::before { content: none; }\n.fl-vis { flex: none; width: auto; }\n.fl-vis .pet-dd-btn { width: auto; }\n.fl-narrow .fl-vis .pet-dd-btn { letter-spacing: .02em; padding-right: 18px; }\n' +
    '.fl-inv > span i.fl-spec { position: absolute; right: 0; bottom: 0; width: 20px; height: 18px; border-radius: 3px; background-color: rgba(0,0,0,.6); background-repeat: no-repeat; background-position: center; background-size: 20px 18px; }\n' +
    '.fl-lgt span { cursor: pointer; user-select: none; }\n.fl-lgt span:hover { color: var(--text); }\n.fl-lgt span.off { opacity: 0.4; text-decoration: line-through; }\n' +
    '.fl-swapc { display: flex; flex-direction: column; min-width: 0; }\n.fl-swapc .fl-chg { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }\n' +
    '.fl-table.swaps { --fl-cols: 52px minmax(56px, .8fr) minmax(0, 3fr); }\n.fl-table.used { --fl-cols: minmax(0, 2fr) 54px minmax(0, 2fr); }\n' +
    '.fl-narrow .fl-table.used { --fl-cols: minmax(0, 1fr) 54px; }\n' +
    '.fl-chart .mkg { stroke: rgba(255,159,67,0.55); stroke-width: 1; stroke-dasharray: 2 3; }\n.fl-chart .mkr { stroke: var(--border); stroke-width: 1; }\n' +
    '.fl-chart .mkl { font: 9px var(--font-mono); fill: ' + FL_MECH + '; }\n' +
    '.fl-table.mech { --fl-cols: minmax(0, 1fr) 34px 46px 46px 50px 50px minmax(56px, .8fr); }\n' +
    '.fl-narrow .fl-table.mech { --fl-cols: minmax(0, 1fr) 34px 50px minmax(64px, .9fr); }\n' +
    '.fl-tr .tl { position: relative; height: 8px; background: var(--bg-elev-2); border-radius: 2px; overflow: hidden; }\n' +
    '.fl-tr .tl i { position: absolute; top: 0; bottom: 0; width: 2px; margin-left: -1px; background: ' + FL_MECH + '; }\n' +
    '.fl-ev.mech .e { color: ' + FL_MECH + '; }';

  function flHas(name) { const b = bridge(); return !!(b && typeof b[name] === 'function'); }
  // Recorder bridge calls: strings are JSON, booleans and objects pass through, a missing function = null.
  async function flCall(name) {
    const b = bridge(); if (!b || typeof b[name] !== 'function') return null;
    let r = await b[name].apply(b, Array.prototype.slice.call(arguments, 1));
    if (typeof r === 'string') { try { r = JSON.parse(r); } catch (e) {} }
    return r;
  }
  function flBool(r) { return r === true || r === 1 || r === 'true' || r === '1' || !!(r && typeof r === 'object' && (r.enabled || r.on || r.value === true)); }
  function el(tag, cls, text) { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; }
  function flRows() { const r = fl.rows; return Array.isArray(r) ? r : (r && (r.logs || r.rows)) || []; }
  function flLog() { return fl.logId === 'live' ? fl.live : (fl.log && fl.log.__id === fl.logId ? fl.log : null); }
  function flFmtDate(ms) {
    const d = new Date(ms), p2 = n => (n < 10 ? '0' : '') + n;
    return ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'][d.getMonth()] + ' ' + d.getDate() + ' ' + p2(d.getHours()) + ':' + p2(d.getMinutes());
  }
  function flSprite(e, sid) {
    if (!sid) return;
    e.dataset.spr = sid;
    const u = FL_SPR.get(sid);
    if (u) { e.style.backgroundImage = "url('" + u + "')"; e.classList.add('img'); return; }
    if (FL_SPR_PENDING.has(sid) || typeof rtxData !== 'object') return;
    FL_SPR_PENDING.add(sid);
    rtxData.raw('cache.sprite', sid).then(url => {
      FL_SPR_PENDING.delete(sid);
      if (!url) return;
      FL_SPR.set(sid, url);
      document.querySelectorAll('[data-spr="' + sid + '"]').forEach(x => { x.style.backgroundImage = "url('" + url + "')"; x.classList.add('img'); });
    }).catch(() => FL_SPR_PENDING.delete(sid));
  }
  // An item's icon on an element: buffs whose picture is the item they come from (an overload, a pair of gloves).
  const FL_ITEM_ICON = new Map(), FL_ITEM_PENDING = new Set(), FL_BUFF_ITEM = new Map(), FL_BUFF_ITEM_PENDING = new Set();
  function flItemIconSet(x, url) { x.style.backgroundImage = "url('" + url + "')"; x.classList.add('img'); x.classList.remove('none'); }
  function flItemIcon(e, itemId) {
    if (!(itemId > 0)) return;
    e.dataset.itm = itemId;
    const u = FL_ITEM_ICON.get(itemId);
    if (u) { flItemIconSet(e, u); return; }
    if (FL_ITEM_PENDING.has(itemId) || !flHas('itemIcon')) return;
    FL_ITEM_PENDING.add(itemId);
    Promise.resolve(bridge().itemIcon(itemId)).then(url => {
      FL_ITEM_PENDING.delete(itemId);
      if (!url || typeof url !== 'string') return;
      FL_ITEM_ICON.set(itemId, url);
      document.querySelectorAll('[data-itm="' + itemId + '"]').forEach(x => flItemIconSet(x, url));
    }).catch(() => FL_ITEM_PENDING.delete(itemId));
  }
  // The item a buff comes from (struct param 4677): the log's dictionary, else the player's cache.
  function flBuffItem(e, struct, item) {
    if (item > 0) { flItemIcon(e, item); return; }
    const k = FL_BUFF_ITEM.get(struct);
    if (k !== undefined) { if (k > 0) flItemIcon(e, k); return; }
    e.dataset.bitm = struct;
    if (FL_BUFF_ITEM_PENDING.has(struct) || typeof rtxData !== 'object' || !flHas('structParams')) return;
    FL_BUFF_ITEM_PENDING.add(struct);
    rtxData.raw('cache.structParams', struct).then(t => {
      FL_BUFF_ITEM_PENDING.delete(struct);
      let sp = null;
      try { sp = typeof t === 'string' ? JSON.parse(t || 'null') : t; } catch (x) { sp = null; }
      const it = sp && sp.ints ? Number(sp.ints['4677']) || 0 : 0;
      FL_BUFF_ITEM.set(struct, it);
      if (it > 0) document.querySelectorAll('[data-bitm="' + struct + '"]').forEach(x => flItemIcon(x, it));
    }).catch(() => FL_BUFF_ITEM_PENDING.delete(struct));
  }
  function flDd(onChange) {
    const dd = el('div', 'pet-dd'), btn = el('button', 'pet-dd-btn'), pop = el('div', 'pet-dd-pop');
    btn.type = 'button'; dd.appendChild(btn); dd.appendChild(pop);
    let items = [], cur = '';
    const paint = () => {
      const sel = items.find(it => String(it.value) === String(cur));
      btn.textContent = sel ? sel.label : (items.length ? items[0].label : '');
      pop.innerHTML = '';
      for (const it of items) {
        const o = el('div', 'pet-dd-opt' + (String(it.value) === String(cur) ? ' on' : ''), it.label);
        o.addEventListener('click', e => { e.stopPropagation(); cur = it.value; dd.classList.remove('open'); paint(); onChange(it.value); });
        pop.appendChild(o);
      }
    };
    btn.addEventListener('click', e => {
      e.stopPropagation();
      const open = dd.classList.contains('open');
      document.querySelectorAll('.pet-dd.open').forEach(x => x.classList.remove('open'));
      if (!open) dd.classList.add('open');
    });
    dd.setItems = (list, val) => { items = list || []; if (val !== undefined) cur = val; paint(); };
    dd.getValue = () => cur;
    paint();
    return dd;
  }
  if (!window._petDDClose) { window._petDDClose = true; document.addEventListener('click', () => document.querySelectorAll('.pet-dd.open').forEach(x => x.classList.remove('open'))); }
  function flCss() {
    if (!document.head) return;
    let s = document.getElementById('flMechCss');
    if (!s) { s = document.createElement('style'); s.id = 'flMechCss'; document.head.appendChild(s); }
    if (s.textContent !== FL_CSS) s.textContent = FL_CSS;
  }

  // Tactic title (struct param 4832) and text (4833) from the user's cache, loaded once per struct. Elements
  // with data-tac get the text added to their tip when it arrives; a hovered chart marker redraws its label.
  function flTactic(sid) {
    if (!sid) return null;
    if (FL_TAC.has(sid)) return FL_TAC.get(sid);
    if (FL_TAC_PENDING.has(sid) || Date.now() - (FL_TAC_MISS.get(sid) || 0) < 15000 || typeof rtxData !== 'object' || !flHas('structParams')) return null;
    FL_TAC_PENDING.add(sid);
    rtxData.raw('cache.structParams', sid).then(t => {
      FL_TAC_PENDING.delete(sid);
      let sp = null;
      try { sp = typeof t === 'string' ? JSON.parse(t || 'null') : t; } catch (e) { sp = null; }
      if (!sp || typeof sp !== 'object') { FL_TAC_MISS.set(sid, Date.now()); return; }
      const strs = sp.strs || {}, v = { title: S().plain(strs['4832']), text: S().plain(strs['4833']) };
      FL_TAC.set(sid, v);
      document.querySelectorAll('[data-tac="' + sid + '"]').forEach(x => flTacApply(x, v));
      const ch = $('flChart');
      if (ch && ch.__hov && ch.__hov.mech && ch.__hov.mech.tactic === sid) flHover(ch, ch.__hov.px, ch.__hov.py);
    }).catch(() => { FL_TAC_PENDING.delete(sid); FL_TAC_MISS.set(sid, Date.now()); });
    return null;
  }
  function flTacApply(e, v) { const t = [v.title, v.text].filter(Boolean).join('\n'); e.dataset.tip = (e.dataset.tipb || '') + (t ? '\n\n' + t : ''); }
  function flTacTip(e, x) {
    e.dataset.tipb = x.label + '\n' + (x.cues || []).map(q => (S().MECH_KINDS[q.kind] || 'kind ' + q.kind) + ' ' + q.id + (q.n > 1 ? ' x' + q.n : '')).join('\n');
    e.dataset.tip = e.dataset.tipb;
    if (!x.tactic) return;
    e.dataset.tac = x.tactic;
    const v = flTactic(x.tactic);
    if (v) flTacApply(e, v);
  }
  // Word wrap to at most `max` lines of `cols` characters; a cut ends in "...".
  function flWrap(text, cols, max) {
    const out = [];
    for (const para of String(text || '').split('\n')) {
      let line = '';
      for (const w of para.split(/\s+/)) {
        if (!w) continue;
        if (line && line.length + 1 + w.length > cols) { out.push(line); line = ''; }
        line = line ? line + ' ' + w : (w.length > cols ? w.slice(0, cols) : w);
      }
      if (line) out.push(line);
    }
    if (out.length > max) { out.length = max; out[max - 1] = (out[max - 1].length > cols - 3 ? out[max - 1].slice(0, cols - 3) : out[max - 1]) + '...'; }
    return out;
  }

  // ---- data: index rows, one log, the open log, the record flag ----------------------------------------
  async function flFetchList(force) {
    if (!flHas('fightsList')) return;
    const now = Date.now();
    if (!force && now - fl.rowsAt < 5000) return;
    fl.rowsAt = now;
    try { const r = await flCall('fightsList'); if (r) fl.rows = r; } catch (e) {}
    flPickDefaults();
    paneRun('fights', flPaint);
  }
  async function flLoad(id) {
    if (!id || id === 'live' || fl.loading === id || (fl.log && fl.log.__id === id)) return;
    fl.loading = id; fl.status = '';
    paneRun('fights', flPaint);
    let log = null;
    try { log = await flCall('fightLoad', id); } catch (e) { log = null; }
    if (fl.loading === id) fl.loading = '';
    if (!log || typeof log !== 'object' || !Array.isArray(log.events)) { fl.status = 'Could not read this log.'; paneRun('fights', flPaint); return; }
    if (!log.fights) log.fights = [];
    log.__id = id;
    fl.log = log;
    if (fl.logId === id && fl.fight === -2) fl.fight = log.fights.length ? log.fights[log.fights.length - 1].n : -1;
    paneRun('fights', flPaint);
  }
  async function flLivePoll(force) {
    if (!flHas('fightsCurrent')) return;
    const now = Date.now();
    if (!force && now - fl.liveAt < 1000) return;
    fl.liveAt = now;
    let r = null;
    try { r = await flCall('fightsCurrent', myPid(), fl.liveSeq); } catch (e) { r = null; }
    if (!r || !r.logId) {
      if (fl.live) { fl.live = null; fl.liveId = ''; fl.liveSeq = 0; fl.rowsAt = 0; if (fl.logId === 'live') { fl.logId = ''; fl.fight = -2; } flPickDefaults(); paneRun('fights', flPaint); }
      return;
    }
    if (r.logId !== fl.liveId) {
      fl.liveId = r.logId; fl.liveSeq = 0;
      fl.live = { format: 1, log: {}, clock: null, dict: {}, actors: [], fights: [], events: [], __id: 'live' };
      if (fl.wantLive) { fl.wantLive = false; fl.logId = 'live'; fl.fight = -1; fl.zoom = null; fl.hl = 0; fl.status = ''; }
    }
    const L = fl.live, h = r.header || {};
    Object.assign(L.log, h.log || (h.character ? h : {}));
    if (h.clock || r.clock) L.clock = h.clock || r.clock;
    if (h.dict || r.dict) L.dict = h.dict || r.dict;
    if (Array.isArray(r.actors) && r.actors.length) L.actors = r.actors;
    if (Array.isArray(r.fights)) L.fights = r.fights;
    if (Array.isArray(r.events) && r.events.length) {
      for (const e of r.events) L.events.push(e);
      if (L.events.length > 20000) L.events.splice(0, L.events.length - 20000);
    }
    if (typeof r.seq === 'number') fl.liveSeq = r.seq;
    if (!fl.logId) { fl.logId = 'live'; fl.fight = -1; }
    if (fl.logId === 'live') { if (fl.fight === -2) fl.fight = -1; paneRun('fights', flPaint); }
  }
  async function flRecPoll(force) {
    if (!flHas('combatRecordEnabled')) { fl.rec = null; return; }
    const now = Date.now();
    if (!force && now - fl.recAt < 5000) return;
    fl.recAt = now;
    try { fl.rec = flBool(await flCall('combatRecordEnabled')); } catch (e) {}
    if (flHas('combatUploadVisibility')) { try { const v = await flCall('combatUploadVisibility'); if (FL_VIS.some(x => x[0] === v)) fl.vis = v; } catch (e) {} }
    paneRun('fights', flPaint);
  }
  async function flRecToggle() {
    if (!flHas('combatRecordEnabled')) return;
    const want = !fl.rec;
    try { fl.rec = flBool(await flCall('combatRecordEnabled', want)); } catch (e) {}
    fl.recAt = 0;
    if (want && fl.rec) { fl.wantLive = true; if (fl.live) { flSelectLog('live'); fl.wantLive = false; } }   // recording on: the panel follows the live log as soon as it opens
    flRecPoll(true);
  }
  function flPickDefaults() {
    const rows = flRows();
    const chars = flChars();
    if (!chars.length) { if (fl.logId !== 'live') fl.logId = ''; return; }
    const mine = (lastSnap && lastSnap.display_name) || '';
    if (!fl.char || chars.indexOf(fl.char) < 0) fl.char = chars.indexOf(mine) >= 0 ? mine : chars[0];
    const logs = flLogsOf(fl.char);
    if (fl.logId === 'live' && fl.live) return;
    if (!fl.logId || !logs.some(r => r.id === fl.logId)) {
      fl.logId = fl.live && flLiveChar() === fl.char ? 'live' : (logs.length ? logs[0].id : '');
      fl.fight = fl.logId === 'live' ? -1 : -2; fl.zoom = null; fl.hl = 0;
    }
    if (fl.logId && fl.logId !== 'live') flLoad(fl.logId);
    void rows;
  }
  function flLiveChar() { return (fl.live && fl.live.log && fl.live.log.character) || (lastSnap && lastSnap.display_name) || ''; }
  function flChars() {
    const set = new Set(flRows().map(r => r.character).filter(Boolean));
    if (fl.live) set.add(flLiveChar());
    return Array.from(set);
  }
  function flLogsOf(ch) { return flRows().filter(r => r.character === ch).sort((a, b) => (b.startedAt || 0) - (a.startedAt || 0)); }
  function flSelectLog(id) {
    fl.logId = id; fl.zoom = null; fl.hl = 0; fl.status = ''; fl.confirmDel = 0;
    fl.fight = id === 'live' ? -1 : -2;
    if (id && id !== 'live') flLoad(id);
    flPaint();
  }
  function flPoll() { flFetchList(false); flLivePoll(false); flRecPoll(false); }

  // ---- shell -----------------------------------------------------------------------------------------------
  function renderFights() {
    const c = $('content');
    let w = $('flWrap');
    flCss();
    if (!w) {
      c.innerHTML = ''; fl.sig = ''; fl.pickSig = '';
      w = el('div', 'pk-wrap fl-wrap'); w.id = 'flWrap'; c.appendChild(w);
      const pick = el('div', 'fl-pick'); pick.id = 'flPick';
      fl.ddChar = flDd(v => { fl.char = v; fl.logId = ''; fl.fight = -2; flPickDefaults(); flPaint(); });
      fl.ddLog = flDd(v => flSelectLog(v));
      fl.ddFight = flDd(v => { fl.fight = Number(v); fl.zoom = null; fl.hl = 0; flPaint(); });
      const live = el('span', 'fl-chip', 'Live'); live.id = 'flLiveChip'; live.dataset.tip = 'The recorder has an open fight for this client. The view refreshes every second.';
      const rec = el('button', 'fl-rec'); rec.id = 'flRec'; rec.type = 'button'; rec.addEventListener('click', flRecToggle);
      // upload visibility: a log takes the value set when it starts recording
      fl.ddVis = flDd(async v => { try { const r = await flCall('combatUploadVisibility', v); if (FL_VIS.some(x => x[0] === r)) fl.vis = r; } catch (e) {} flPaint(); });
      fl.ddVis.classList.add('fl-vis'); fl.ddVis.style.display = 'none';
      fl.ddVis.dataset.tip = 'How uploads appear on runetools.io. A log keeps the setting it had when it started recording.';
      const side = el('div', 'fl-pick-r'); side.appendChild(live); side.appendChild(fl.ddVis); side.appendChild(rec);
      pick.appendChild(fl.ddChar); pick.appendChild(fl.ddLog); pick.appendChild(fl.ddFight); pick.appendChild(side);
      w.appendChild(pick);
      const note = el('div', 'fl-note'); note.id = 'flNote'; w.appendChild(note);
      const strip = el('div', 'fl-strip'); strip.id = 'flStrip'; w.appendChild(strip);
      const line = el('div', 'fl-line'); line.id = 'flLine'; w.appendChild(line);
      const tabs = el('div', 'fl-tabs'); tabs.id = 'flTabs';
      for (const [id, label] of FL_TABS) { const b = el('button', '', label); b.type = 'button'; b.dataset.tab = id; b.addEventListener('click', () => { fl.tab = id; flPaint(); }); tabs.appendChild(b); }
      w.appendChild(tabs);
      const body = el('div', 'fl-body'); body.id = 'flBody'; w.appendChild(body);
      const btns = el('div', 'fl-btns'); btns.id = 'flBtns'; w.appendChild(btns);
      flFetchList(true); flRecPoll(true); flLivePoll(true);
    }
    flPoll();
    flPaint();
  }

  function flPaint() {
    const w = $('flWrap'); if (!w) return;
    const ww = w.clientWidth || 0;
    if (ww) w.classList.toggle('fl-narrow', ww < 440);
    if (!S()) { $('flNote').textContent = 'panel_fights_stats.js did not load.'; return; }
    const has = flHas('fightsList');
    const log = flLog(), n = fl.fight;
    flPaintPicker(has);
    const hasMech = !!(log && S().mechCount && S().mechCount(log) > 0);
    if (log && !hasMech && fl.tab === 'mechs') fl.tab = 'overview';
    const hasGear = !!(log && log.events.some(e => e[0] === 20));
    if (log && !hasGear && fl.tab === 'gear') fl.tab = 'overview';
    const width = ($('flBody') && $('flBody').clientWidth) || 0;
    const sig = [has, fl.logId, n, fl.tab, log ? log.events.length : -1, log ? (log.fights || []).length : -1, fl.zoom ? fl.zoom.join(',') : '', fl.hl, fl.mhl, fl.evSearch, JSON.stringify(fl.evFilt), JSON.stringify(fl.hide), fl.vis,
                 fl.evShow, fl.rec, fl.liveId, fl.loading, fl.status, fl.confirmDel, width, flRows().length].join('|');
    if (fl.ddVis) {
      fl.ddVis.style.display = fl.vis ? '' : 'none';
      if (fl.vis && fl.ddVis.getValue() !== fl.vis) fl.ddVis.setItems(FL_VIS.map(([v, l]) => ({ value: v, label: l })), fl.vis);
    }
    if (sig === fl.sig) return;
    fl.sig = sig;
    const note = $('flNote');
    note.textContent = !has ? 'This launcher build has no fight recorder.' : fl.status ? fl.status : fl.loading ? 'Loading...' : '';
    const rec = $('flRec');
    if (fl.rec === null) rec.style.display = 'none';
    else { rec.style.display = ''; rec.textContent = 'Record: ' + (fl.rec ? 'on' : 'off'); rec.className = 'fl-rec ' + (fl.rec ? 'on' : 'off'); rec.dataset.tip = fl.rec ? 'Fights are being recorded to disk. Click to stop.' : 'Nothing is recorded. Click to start recording fights.'; }
    $('flLiveChip').className = 'fl-chip' + (fl.live ? ' on' : '');
    for (const b of $('flTabs').children) { b.classList.toggle('on', b.dataset.tab === fl.tab); if (b.dataset.tab === 'mechs') b.style.display = hasMech ? '' : 'none'; if (b.dataset.tab === 'gear') b.style.display = hasGear ? '' : 'none'; }
    flPaintStrip(log, n);
    const body = $('flBody'); body.innerHTML = '';
    if (!log) {
      body.appendChild(el('div', 'fl-empty', !has ? '' : fl.loading ? 'Loading...' : flRows().length || fl.live ? 'Select a log.' : 'No fights recorded yet.' + (fl.rec === false ? ' Recording is off.' : '')));
    } else {
      const r = { overview: flOverview, dealt: flDealt, taken: flTaken, health: flHealth, buffs: flBuffs, gear: flGear, casts: flCasts, mechs: flMechs, events: flEvents }[fl.tab] || flOverview;
      try { r(log, n, body); } catch (e) { body.appendChild(el('div', 'fl-empty', 'Could not draw this tab: ' + (e && e.message ? e.message : e))); }
    }
    flPaintButtons(log);
  }
  function flPaintPicker(has) {
    const chars = flChars(), logs = flLogsOf(fl.char), log = flLog();
    const fights = log ? (log.fights || []) : ((logs.find(r => r.id === fl.logId) || {}).fights || []);
    const psig = [chars.join(','), logs.map(r => r.id + (r.upload ? '+' : '')).join(','), fl.char, fl.logId, fl.fight, fights.length, fl.liveId].join('|');
    if (psig === fl.pickSig) return;
    fl.pickSig = psig;
    // no logs yet: a placeholder like the log picker's, not an empty box
    fl.ddChar.setItems(chars.length ? chars.map(c => ({ value: c, label: c })) : [{ value: '', label: 'No characters' }], chars.length ? fl.char : '');
    const items = [];
    if (fl.live && flLiveChar() === fl.char) items.push({ value: 'live', label: 'Live: ' + flFmtDate((fl.live.log && fl.live.log.startedAt) || Date.now()) });
    for (const r of logs) {
      const dur = (r.endedAt && r.startedAt) ? S().fmtMs(r.endedAt - r.startedAt) : '';
      const kind = (r.fights || []).some(f => f.kind === 'boss' || f.kind === 'encounter') ? (r.fights.find(f => f.boss) || {}).boss || 'boss' : ((r.fights || []).length ? 'kills' : '');
      items.push({ value: r.id, label: flFmtDate(r.startedAt || 0) + (dur ? ' ' + dur : '') + (kind ? ' ' + kind : '') + (r.upload ? ' (uploaded)' : '') });
    }
    if (!items.length) items.push({ value: '', label: has ? 'No logs' : 'No recorder' });
    fl.ddLog.setItems(items, fl.logId);
    const fi = [];
    if (fights.length > 1) fi.push({ value: -1, label: 'Whole log' });
    for (const f of fights) {
      let what = f.boss || '';
      if (!what && f.kind === 'kills') { let k = f.kills || 0; if (log) { try { k = S().summary(log, f.n).kills; } catch (e) {} } what = k + (k === 1 ? ' kill' : ' kills'); }
      let ms = ((log && S().fightEnd ? S().fightEnd(log, f) : f.end) - f.start) * 20;   // an open fight runs to the last event; a kill shows the game's kill time: boss spawn to its death
      if (log && f.kind === 'boss') { try { const info = S().fightInfo(log, f.n); if (info && info.killMs) ms = info.killMs; } catch (e) {} }
      const kt = log && f.kind === 'boss' && S().fmtMsTenths ? S().fmtMsTenths(ms) : S().fmtMs(ms);
      fi.push({ value: f.n, label: '#' + (f.n + 1) + ' ' + (what || f.kind || '') + ' ' + kt });
    }
    if (!fi.length) fi.push({ value: -1, label: fl.logId === 'live' ? 'Open fight' : 'Whole log' });
    fl.ddFight.setItems(fi, fl.fight);
    $('flPick').style.display = has || fl.live ? '' : 'none';
  }
  // After Upload: follow the launcher's state for that log until it is sent or fails, so the line under the
  // pickers says what happened instead of staying on "queued".
  function flUploadFollow(id, tries) {
    tries = tries || 0;
    if (tries > 60 || fl.logId !== id) return;
    setTimeout(async () => {
      if (fl.logId !== id) return;
      let st = null;
      try { st = await flCall('fightUploadStatus', id); } catch (e) {}
      const state = st && st.state;
      if (state === 'sent') { fl.status = 'Uploaded to runetools.io.'; fl.rowsAt = 0; flPaint(); return; }
      if (state === 'failed') { fl.status = st.error ? String(st.error).slice(0, 120) : 'Upload failed.'; fl.rowsAt = 0; flPaint(); return; }   // the launcher's text is a sentence
      if (state === 'queued' || state === 'sending' || state === 'retry') { const t = state === 'retry' ? 'Upload will retry.' : 'Uploading...'; if (fl.status !== t) { fl.status = t; flPaint(); } }
      flUploadFollow(id, tries + 1);
    }, 1500);
  }
  function flPaintStrip(log, n) {
    const strip = $('flStrip'), line = $('flLine');
    strip.innerHTML = ''; line.textContent = '';
    if (!log) { strip.style.display = 'none'; return; }
    strip.style.display = '';
    const st = S(), sm = st.summary(log, n);
    // a kill: the game's own kill time (boss spawn to its death) and the DPS over that window
    let kill = null;
    try { const fi = n >= 0 && st.fightInfo ? st.fightInfo(log, n) : null; if (fi && fi.result === 'kill' && fi.killMs) kill = { ms: fi.killMs, dps: st.metrics(log, n).dps, fight: fi.durMs }; } catch (e) {}
    const cells = [kill ? ['Kill time', st.fmtMsTenths ? st.fmtMsTenths(kill.ms) : st.fmtMs(kill.ms), 'Boss spawn to its death; the fight ran ' + st.fmtMs(kill.fight)] : ['Duration', st.fmtMs(sm.durMs), ''],
                   kill ? ['DPS', Math.round(kill.dps).toLocaleString(), 'Over the kill time'] : ['DPS', Math.round(sm.dps).toLocaleString(), sm.dpm.toFixed(0) + ' per minute'], ['Dealt', st.fmtNum(sm.dealt), sm.dealt.toLocaleString() + ' in ' + sm.hits + ' hits, ' + sm.crits + ' crits'],
                   ['Taken', st.fmtNum(sm.taken), sm.taken.toLocaleString() + ', ' + sm.blocked + ' blocked' + (sm.healed ? ', healed ' + sm.healed.toLocaleString() : '')], ['Deaths', String(sm.deaths), ''], ['Max hit', st.fmtNum(sm.maxHit), sm.maxHit.toLocaleString()]];
    for (const [k, v, tip] of cells) {
      const d = el('div', 'fl-kv'); d.appendChild(el('div', 'k', k)); d.appendChild(el('div', 'v', v)); if (tip) d.dataset.tip = tip; strip.appendChild(d);
    }
    const f = n >= 0 && log.fights ? log.fights.find(x => x.n === n) : null;
    const parts = [];
    if (f && f.boss) parts.push(f.boss);
    const tg = {};
    for (const [a] of sm.targets) { const act = st.actorOf(log, a); const key = (act.name || 'NPC') + (act.id >= 0 ? ' (' + act.id + ')' : ''); tg[key] = (tg[key] || 0) + 1; }
    const tk = Object.keys(tg);
    if (tk.length) parts.push(tk.slice(0, 3).map(k => k + (tg[k] > 1 ? ' x' + tg[k] : '')).join(', ') + (tk.length > 3 ? ' +' + (tk.length - 3) : ''));
    parts.push(sm.kills + (sm.kills === 1 ? ' kill' : ' kills'));
    line.textContent = parts.join('  \u00b7  ');
    line.dataset.tip = line.textContent;
  }
  function flPaintButtons(log) {
    const btns = $('flBtns'); btns.innerHTML = '';
    const id = fl.logId;
    if (!log || !id || id === 'live') return;
    const mk = (label, cls, fn, tip) => { const b = el('button', 'fl-btn' + (cls ? ' ' + cls : ''), label); b.type = 'button'; if (tip) b.dataset.tip = tip; b.addEventListener('click', fn); btns.appendChild(b); return b; };
    const row = flRows().find(r => r.id === id);
    const as = (row && row.uploadAs) || fl.vis;
    if (flHas('fightUpload') && flHas('fightUploadStatus') && !(row && row.upload)) mk('Upload', 'gold', async () => {
      fl.status = 'Uploading...'; flPaint();
      let ok = false;
      try { const r = await flCall('fightUpload', id); ok = !!(r && r.ok); } catch (e) {}
      fl.status = ok ? 'Upload queued.' : 'Upload failed.'; fl.rowsAt = 0; flPaint();
      if (ok) flUploadFollow(id);
    }, as ? 'Uploads as ' + flVisName(as).toLowerCase() + (row && row.uploadAs ? ', set when this log started recording' : '') : '');
    if (flHas('fightExport')) mk('Export', '', async () => { try { const r = await flCall('fightExport', id, true); fl.status = r && r.ok ? 'Exported to ' + (r.path || 'the export folder') + '.' : (r && r.error ? String(r.error) : ''); } catch (e) { fl.status = 'Export failed.'; } flPaint(); }, 'Save the log as .json.gz (Save As)');
    if (flHas('fightOpenFolder')) mk('Folder', '', () => { flCall('fightOpenFolder'); }, 'Open the folder that holds the logs');
    if (flHas('fightDelete')) {
      const now = Date.now();
      if (fl.confirmDel && now - fl.confirmDel < 4000) mk('Confirm', 'warn', async () => { fl.confirmDel = 0; try { await flCall('fightDelete', id); } catch (e) {} fl.logId = ''; fl.log = null; fl.rowsAt = 0; fl.status = 'Deleted.'; await flFetchList(true); flPaint(); });
      else mk('Delete', '', () => { fl.confirmDel = Date.now(); flPaint(); setTimeout(() => { if (fl.confirmDel && Date.now() - fl.confirmDel >= 4000) { fl.confirmDel = 0; paneRun('fights', flPaint); } }, 4200); });
    }
  }

  // ---- tabs --------------------------------------------------------------------------------------------------
  function flH(text) { return el('div', 'fl-h', text); }
  function flIcon(sid) { const i = el('span', 'fl-ico' + (sid ? '' : ' none')); flSprite(i, sid); return i; }
  // A name cell: the icon, or an empty slot of the same size when `slot` asks for alignment, then the text.
  // A name with its icon: a sprite, else the item it comes from (the familiar's pouch), else an empty slot.
  function flName(cls, text, icon, slot, item) {
    const nm = el('span', cls);
    if (icon || slot) { const box = flIcon(icon); if (!icon && item > 0) flItemIcon(box, item); nm.appendChild(box); }
    nm.appendChild(el('span', 'nt', text));
    return nm;
  }
  function flBar(label, value, total, cls, icon, item) {
    const st = S(), row = el('div', 'fl-bar' + (cls ? ' ' + cls : ''));
    const nm = flName('n', label, icon, icon !== undefined, item); nm.dataset.tip = label;
    const b = el('span', 'b'); const i = el('i'); i.style.width = (total ? value / total * 100 : 0).toFixed(1) + '%'; b.appendChild(i);
    const v = el('span', 'v'); v.textContent = st.fmtNum(value); const sm = el('small', '', (total ? value / total * 100 : 0).toFixed(1) + '%'); v.appendChild(sm);
    v.dataset.tip = value.toLocaleString();
    row.appendChild(nm); row.appendChild(b); row.appendChild(v);
    return row;
  }
  function flOverview(log, n, body) {
    const st = S(), ss = st.styleSplit(log, n), sm = st.summary(log, n), ab = st.byAbility(log, n), tc = st.trackerCheck(log, n);
    body.appendChild(flH('Damage by style'));
    const g = el('div', 'fl-bars');
    for (const s of st.STYLES) { if (!ss.split[s]) continue; const r = flBar(s.charAt(0).toUpperCase() + s.slice(1), ss.split[s], ss.total, ''); r.querySelector('i').style.background = FL_STYLE_COLOR[s] || ''; g.appendChild(r); }
    if (!ss.total) g.appendChild(el('div', 'fl-empty', 'No damage dealt.'));
    body.appendChild(g);
    body.appendChild(flH('Top abilities'));
    const g2 = el('div', 'fl-bars');
    for (const row of ab.rows.filter(r => r.struct).slice(0, 5)) g2.appendChild(flBar(row.name, row.total, ab.total, '', row.icon || 0, row.item || 0));
    if (sm.unattributed) g2.appendChild(flBar('Unattributed', sm.unattributed, ab.total, 'dim', 0));
    body.appendChild(g2);
    body.appendChild(flH('Game tracker'));
    const t = el('div', 'fl-strip');
    const kv = (k, v, tip) => { const d = el('div', 'fl-kv'); d.appendChild(el('div', 'k', k)); d.appendChild(el('div', 'v', v)); if (tip) d.dataset.tip = tip; t.appendChild(d); };
    kv('Tracker damage', tc.trackerDealt == null ? '-' : st.fmtNum(tc.trackerDealt), tc.trackerDealt == null ? 'No tracker cells in this fight' : tc.trackerDealt.toLocaleString() + ' (column 22, unit open)');
    kv('Recorded', st.fmtNum(tc.dealt), tc.dealt.toLocaleString());
    kv('Tracker max', tc.trackerMax == null ? '-' : st.fmtNum(tc.trackerMax), tc.trackerMax == null ? '' : tc.trackerMax.toLocaleString());
    kv('Recorded max', st.fmtNum(tc.maxHit), tc.maxHit.toLocaleString());
    kv('Casts', String(sm.casts), '');
    kv('Kills', String(sm.kills), '');
    body.appendChild(t);
  }
  function flTable(cls, head, rows) {
    const t = el('div', 'fl-table' + (cls ? ' ' + cls : ''));
    const h = el('div', 'fl-tr h'); for (const x of head) h.appendChild(el('span', ((x[1] ? 'num' : '') + (x[2] ? ' opt' : '')).trim(), x[0])); t.appendChild(h);
    for (const r of rows) t.appendChild(r);
    return t;
  }
  function flCells(row, cells) { for (const [text, cls, tip] of cells) { const s = el('span', cls || '', text); if (tip) s.dataset.tip = tip; row.appendChild(s); } }
  // Tooltip for an ability row; rows under a negative struct are hit kinds, not casts.
  function flRowTip(r) {
    if (r.struct === -1) return r.name + '\nSpirit damage from your conjures; the game gives no source per hit.';
    if (r.struct === -2) return r.name + '\nPoison ticks every 1.8 s; with a Putrid Zombie up, its stench.';
    if (!r.struct) return r.name + '\nHits no cast explains: procs, bleeds, hits before the first cast';
    return r.name + ' (struct ' + r.struct + ')';
  }
  function flShare(share) { const s = el('span', 'sh'); const i = el('i'); i.style.width = (share * 100).toFixed(1) + '%'; s.appendChild(i); s.dataset.tip = (share * 100).toFixed(1) + '%'; return s; }
  function flDealt(log, n, body) {
    const st = S(), ab = st.byAbility(log, n);
    if (!ab.rows.length) { body.appendChild(el('div', 'fl-empty', 'No damage dealt.')); return; }
    const rows = [];
    for (const r of ab.rows) {
      const tr = el('div', 'fl-tr click' + (r.struct ? '' : ' dim') + (fl.hl && fl.hl === r.struct ? ' on' : ''));
      const nm = flName('n', r.name, r.icon, true, r.item); nm.dataset.tip = flRowTip(r); tr.appendChild(nm);
      flCells(tr, [[String(r.hits), 'num'], [String(r.crits), 'num opt'], [st.fmtNum(r.avg), 'num opt', Math.round(r.avg).toLocaleString()], [st.fmtNum(r.max), 'num opt', r.max.toLocaleString()], [st.fmtNum(r.total), 'num tot', r.total.toLocaleString()]]);
      tr.appendChild(flShare(r.share));
      tr.addEventListener('click', () => { fl.hl = fl.hl === r.struct ? 0 : r.struct; fl.tab = 'health'; flPaint(); });
      rows.push(tr);
    }
    body.appendChild(flTable('', [['Ability'], ['Hits', 1], ['Crits', 1, 1], ['Avg', 1, 1], ['Max', 1, 1], ['Total', 1], ['Share', 1]], rows));
    body.appendChild(el('div', 'fl-note2', 'Click a row to mark its hits on the Timeline.'));    // your familiar's hits: apart from yours, not in your damage
    const fam = st.familiar ? st.familiar(log, n) : null;
    if (fam) {
      body.appendChild(flH('Familiar'));
      const fr = fam.rows.map(x => { const tr = el('div', 'fl-tr'); tr.appendChild(flName('n', x.name)); flCells(tr, [[String(x.hits), 'num'], [st.fmtNum(x.max), 'num opt', x.max.toLocaleString()], [st.fmtNum(x.total), 'num', x.total.toLocaleString()]]); return tr; });
      body.appendChild(flTable('', [['Familiar'], ['Hits', 1], ['Max', 1, 1], ['Total', 1]], fr));
      body.appendChild(el('div', 'fl-note2', 'Counted in your damage and DPS, as the row above.'));
    }
  }
  function flTaken(log, n, body) {
    const st = S(), bs = st.bySource(log, n);
    if (!bs.rows.length) { body.appendChild(el('div', 'fl-empty', 'No damage taken.')); return; }
    const rows = [];
    for (const r of bs.rows) {
      const tr = el('div', 'fl-tr' + (r.actor < 0 ? ' dim' : ''));
      const nm = flName('n', r.name + (r.id >= 0 ? ' (' + r.id + ')' : '')); nm.dataset.tip = r.actor < 0 ? 'No NPC targeted you at that tick' : 'actor ' + r.actor; tr.appendChild(nm);
      flCells(tr, [[String(r.hits), 'num'], [String(r.blocked), 'num opt'], [st.fmtNum(r.avg), 'num opt', Math.round(r.avg).toLocaleString()], [st.fmtNum(r.max), 'num opt', r.max.toLocaleString()], [st.fmtNum(r.total), 'num tot', r.total.toLocaleString()]]);
      tr.appendChild(flShare(r.share));
      rows.push(tr);
    }
    body.appendChild(flTable('taken', [['Source'], ['Hits', 1], ['Blocked', 1, 1], ['Avg', 1, 1], ['Max', 1, 1], ['Total', 1], ['Share', 1]], rows));
  }

  // Health chart: player LP (left axis), target LP % (right axis), adrenaline area, prayer dotted, hits as
  // ticks above (dealt) and below (taken) the axis, kills and deaths as rules, buffs as a band, boss mechanics
  // as labelled ticks in a row per boss above the plot. Drag zooms.
  function flHealth(log, n, body) {
    const st = S(), se = st.series(log, n);
    se.mk = st.mechs ? st.mechs(log, n) : null;
    const cont = el('div', 'fl-chart'); cont.id = 'flChart'; body.appendChild(cont);
    const legend = el('div', 'fl-legend fl-lgt');
    const lg = (c, t, key) => {
      const s = el('span', fl.hide[key] ? 'off' : ''); const i = el('i'); i.style.background = c; s.appendChild(i); s.appendChild(document.createTextNode(t));
      s.dataset.tip = (fl.hide[key] ? 'Show ' : 'Hide ') + t;
      s.addEventListener('click', () => { if (fl.hide[key]) delete fl.hide[key]; else fl.hide[key] = 1; flPaint(); });
      legend.appendChild(s);
    };
    lg('#e8eaf0', 'your LP', 'lp'); lg('rgba(232,194,106,0.8)', 'adrenaline', 'adr'); lg('#4cc0c0', 'prayer', 'pr'); lg('#5fd07a', 'dealt', 'hd'); lg('#ff6b6b', 'taken', 'ht');
    if (st.uptimes(log, n).rows.some(u => u.uptime < 0.98)) lg('#6f8fb8', 'buff bars', 'band');
    if (se.mk && se.mk.uses.length) lg(FL_MECH, 'mechanic', 'mech');
    const tops = st.summary(log, n).targets.slice(0, 4);
    const tnames = tops.map(([a]) => st.actorOf(log, a).name || 'NPC'), tseen = {};
    tops.forEach((t, i) => {
      const nm = tnames[i], dup = tnames.filter(x => x === nm).length > 1;
      tseen[nm] = (tseen[nm] || 0) + 1;
      lg(FL_COLORS[i % FL_COLORS.length], nm + (dup ? ' ' + tseen[nm] : '') + ' LP %', 't' + t[0]);
    });
    body.appendChild(legend);
    body.appendChild(el('div', 'fl-note2', (fl.zoom ? 'Zoomed. Double-click to reset.' : 'Drag to zoom. Hover for the nearest hit.') + ' Click a legend entry to hide it.'));
    flDraw(cont, log, n, se, tops.map(t => t[0]));
    cont.addEventListener('mousedown', e => { const m = cont.__m; if (!m) return; fl.drag = { x0: flPx(cont, e), x1: flPx(cont, e) }; });
    cont.addEventListener('mousemove', e => {
      const m = cont.__m; if (!m) return;
      const px = flPx(cont, e);
      if (fl.drag) { fl.drag.x1 = px; const sel = cont.querySelector('#flSel'); if (sel) { const a = Math.min(fl.drag.x0, px), b = Math.max(fl.drag.x0, px); sel.setAttribute('x', a); sel.setAttribute('width', Math.max(1, b - a)); sel.setAttribute('visibility', 'visible'); } return; }
      flHover(cont, px, flPy(cont, e));
    });
    const up = () => {
      const m = cont.__m, d = fl.drag; fl.drag = null;
      if (!m || !d) return;
      if (Math.abs(d.x1 - d.x0) < 6) { const sel = cont.querySelector('#flSel'); if (sel) sel.setAttribute('visibility', 'hidden'); return; }
      const a = Math.max(m.r.start, Math.min(m.r.end, m.cOf(Math.min(d.x0, d.x1)))), b = Math.max(m.r.start, Math.min(m.r.end, m.cOf(Math.max(d.x0, d.x1))));
      if (b - a >= 30) { fl.zoom = [Math.floor(a), Math.ceil(b)]; flPaint(); }
    };
    cont.addEventListener('mouseup', up);
    cont.addEventListener('mouseleave', () => { if (fl.drag) up(); cont.__hov = null; const g = cont.querySelector('#flHov'); if (g) g.innerHTML = ''; });
    cont.addEventListener('dblclick', () => { fl.zoom = null; fl.drag = null; flPaint(); });
  }
  // The chart's rect in screen px, the units of clientX/Y: getBoundingClientRect alone is divided by the window
  // body's zoom (font size), which put the hover line and drag zoom off the pointer.
  function flRect(cont) {
    if (typeof uiScreenRect === 'function') return uiScreenRect(cont);
    return cont.getBoundingClientRect ? cont.getBoundingClientRect() : { left: 0, top: 0, width: cont.__m.W };
  }
  function flPx(cont, e) {
    const m = cont.__m, r = flRect(cont);
    return (e.clientX - r.left) * (r.width ? m.W / r.width : 1);
  }
  function flPy(cont, e) {   // the chart keeps its aspect, so the width ratio holds for y too
    const m = cont.__m, r = flRect(cont);
    return (e.clientY - r.top) * (r.width ? m.W / r.width : 1);
  }
  function flDraw(cont, log, n, se, tops) {
    const st = S(), r = se.range;
    const W = Math.max(300, cont.clientWidth || (cont.parentElement && cont.parentElement.clientWidth) || 360);
    const hid = fl.hide, mk = se.mk && se.mk.uses.length && !hid.mech ? se.mk : null, mkB = mk ? mk.bosses : [], MR = 12, MT = mkB.length ? 4 + mkB.length * MR : 0;
    const bossLbl = mkB.map(b => { const bn = st.bossName(log, b); return /^NPC \d+$/.test(bn) ? String(b) : bn; });
    const L = Math.round(Math.max(44, Math.min(W * 0.2, 8 + 5.4 * Math.max(0, ...bossLbl.map(t => t.length))))), R = 34, PT = 8 + MT, PH = 148, TB = 34, AX = 14, BB = 20;
    const axisY = PT + PH, H = axisY + TB + AX + BB + 2;
    const z0 = fl.zoom ? fl.zoom[0] : r.start, z1 = fl.zoom ? fl.zoom[1] : r.end, span = Math.max(30, z1 - z0);
    const x = c => L + (c - z0) / span * (W - L - R), cOf = px => z0 + (px - L) / (W - L - R) * span;
    const lpMax = se.lpMax || 1;
    const yLp = v => PT + PH - Math.max(0, Math.min(1, v / lpMax)) * PH, yPct = p => PT + PH - Math.max(0, Math.min(1, p)) * PH;
    const f1 = v => Math.round(v * 10) / 10;
    // step-after path over [c, value] points, clipped to the zoom window; the last value holds to holdTo (default: the range end)
    const step = (pts, val, yf, close, holdTo) => {
      let d = '', prev = null;
      for (const p of pts) {
        if (p[0] > z1) break;
        const px = Math.max(L, x(p[0])), py = yf(val(p));
        if (prev === null) d += 'M' + f1(px) + ' ' + f1(py); else d += 'H' + f1(px) + 'V' + f1(py);
        prev = p;
      }
      if (prev !== null) { d += 'H' + f1(Math.max(L, x(Math.min(z1, holdTo == null ? r.end : Math.min(r.end, holdTo))))); if (close) d += 'V' + f1(yf(0)) + 'H' + f1(Math.max(L, x(pts[0][0]))) + 'Z'; }
      return d;
    };
    let s = '<svg xmlns="http://www.w3.org/2000/svg" width="' + W + '" height="' + H + '" viewBox="0 0 ' + W + ' ' + H + '" class="fl-svg">';
    for (const p of [0, 0.5, 1]) s += '<line class="grid" x1="' + L + '" x2="' + (W - R) + '" y1="' + f1(yPct(p)) + '" y2="' + f1(yPct(p)) + '"/>';
    if (span <= 100 * st.TICK) {
      const ph = (log.clock && log.clock.phase) || 0;
      for (let c = z0 - ((z0 - ph) % st.TICK + st.TICK) % st.TICK; c <= z1; c += st.TICK) s += '<line class="tick" x1="' + f1(x(c)) + '" x2="' + f1(x(c)) + '" y1="' + PT + '" y2="' + (axisY + TB) + '"/>';
    }
    if (se.adren.length && !hid.adr) s += '<path class="adr" d="' + step(se.adren, p => p[1] / 1000, yPct, true) + '"/>';
    if (se.prayer.length && !hid.pr) s += '<path class="pr" d="' + step(se.prayer, p => p[2] > 0 ? p[1] / (p[2] * 100) : 0, yPct, false) + '"/>';
    tops.forEach((a, i) => {
      const t = se.targets[a]; if (!t || hid['t' + a]) return;
      for (const seg of t.segs) if (seg.length) s += '<path class="tg" stroke="' + FL_COLORS[i % FL_COLORS.length] + '" d="' + step(seg, p => (p[2] || t.lpMax) ? p[1] / (p[2] || t.lpMax) : 0, yPct, false, seg[seg.length - 1][0] + st.TICK) + '"/>';
    });
    if (se.lp.length && !hid.lp) s += '<path class="lp" d="' + step(se.lp, p => p[1], yLp, false) + '"/>';
    for (const k of se.kills) if (k[0] >= z0 && k[0] <= z1) s += '<line class="kill" x1="' + f1(x(k[0])) + '" x2="' + f1(x(k[0])) + '" y1="' + PT + '" y2="' + (axisY + TB) + '"/>';
    for (const d of se.deaths) if (d[0] >= z0 && d[0] <= z1) s += '<line class="death" x1="' + f1(x(d[0])) + '" x2="' + f1(x(d[0])) + '" y1="' + PT + '" y2="' + (axisY + TB) + '"/>';
    const maxD = se.dealt.reduce((m, h) => Math.max(m, h[1]), 1), maxT = se.taken.reduce((m, h) => Math.max(m, h[1]), 1);
    if (!hid.hd) for (const h of se.dealt) { if (h[0] < z0 || h[0] > z1) continue; const hh = 3 + 28 * h[1] / maxD; s += '<line class="hd' + (fl.hl && h[2] === fl.hl ? ' hl' : '') + (h[4] ? ' crit' : '') + '" x1="' + f1(x(h[0])) + '" x2="' + f1(x(h[0])) + '" y1="' + axisY + '" y2="' + f1(axisY - hh) + '"/>'; }
    if (!hid.ht) for (const h of se.taken) { if (h[0] < z0 || h[0] > z1) continue; const hh = h[1] ? 3 + 28 * h[1] / maxT : 3; s += '<line class="ht' + (h[1] ? '' : ' blk') + '" x1="' + f1(x(h[0])) + '" x2="' + f1(x(h[0])) + '" y1="' + axisY + '" y2="' + f1(axisY + hh) + '"/>'; }
    s += '<line class="ax" x1="' + L + '" x2="' + (W - R) + '" y1="' + axisY + '" y2="' + axisY + '"/>';
    s += '<text class="lbl" x="' + (L - 4) + '" y="' + (PT + 8) + '" text-anchor="end">' + st.fmtNum(lpMax) + '</text><text class="lbl" x="' + (L - 4) + '" y="' + f1(yLp(lpMax / 2) + 3) + '" text-anchor="end">' + st.fmtNum(lpMax / 2) + '</text><text class="lbl" x="' + (L - 4) + '" y="' + (axisY - 1) + '" text-anchor="end">0</text>';
    s += '<text class="lbl" x="' + (W - R + 4) + '" y="' + (PT + 8) + '">100%</text><text class="lbl" x="' + (W - R + 4) + '" y="' + f1(yPct(0.5) + 3) + '">50%</text><text class="lbl" x="' + (W - R + 4) + '" y="' + (axisY - 1) + '">0%</text>';
    const spanMs = span * st.CYCLE_MS, stepS = [5, 10, 15, 30, 60, 120, 300, 600, 1800].find(v => spanMs / (v * 1000) <= 7) || 3600;
    const t0 = (z0 - r.start) * st.CYCLE_MS;
    for (let t = Math.ceil(t0 / (stepS * 1000)) * stepS * 1000; t <= t0 + spanMs; t += stepS * 1000) {
      const c = r.start + t / st.CYCLE_MS;
      s += '<line class="ax" x1="' + f1(x(c)) + '" x2="' + f1(x(c)) + '" y1="' + (axisY + TB) + '" y2="' + (axisY + TB + 3) + '"/><text class="lbl" x="' + f1(x(c)) + '" y="' + (axisY + TB + AX - 2) + '" text-anchor="middle">' + st.fmtMs(t) + '</text>';
    }
    const up = hid.band ? [] : st.uptimes(log, n).rows.filter(u => u.uptime < 0.98).slice(0, 6);
    up.forEach((u, i) => {
      const y = axisY + TB + AX + 2 + i * 3;
      for (const sp of u.spans) { if (sp[1] < z0 || sp[0] > z1) continue; s += '<line class="buffb" stroke="' + (u.type ? '#e06c6c' : FL_COLORS[i % FL_COLORS.length]) + '" x1="' + f1(x(Math.max(z0, sp[0]))) + '" x2="' + f1(x(Math.min(z1, sp[1]))) + '" y1="' + y + '" y2="' + y + '"><title>' + flEsc(u.name) + '</title></line>'; }
    });
    // mechanic rows: the boss name in the left margin, a tick per use, the label where the next tick leaves room
    const lblFit = (t, room) => { const k = Math.floor(room / 5.4); return k >= t.length ? t : k >= 5 ? t.slice(0, k - 1) + '.' : ''; };
    mkB.forEach((boss, bi) => {
      const y0 = 4 + bi * MR, y1 = y0 + MR - 2;
      s += '<text class="lbl" x="' + (L - 4) + '" y="' + (y1 - 1) + '" text-anchor="end">' + flEsc(lblFit(bossLbl[bi], L - 6)) + '</text>';
      s += '<line class="mkr" x1="' + L + '" x2="' + (W - R) + '" y1="' + (y1 + 0.5) + '" y2="' + (y1 + 0.5) + '"/>';
      const pts = mk.uses.filter(k => k.boss === boss && k.c >= z0 && k.c <= z1);
      pts.forEach((k, j) => {
        const px = x(k.c), hl = !!fl.mhl && fl.mhl === k.row;
        if (hl) s += '<line class="mkg" x1="' + f1(px) + '" x2="' + f1(px) + '" y1="' + PT + '" y2="' + (axisY + TB) + '"/>';
        s += '<line class="mk' + (hl ? ' hl' : '') + '" x1="' + f1(px) + '" x2="' + f1(px) + '" y1="' + (y0 + 1) + '" y2="' + y1 + '"/>';
        if (j > 0 && pts[j - 1].label === k.label) return;
        const t = lblFit(k.label, (j + 1 < pts.length ? x(pts[j + 1].c) : W - 2) - px - 5);
        if (t) s += '<text class="mkl" x="' + f1(px + 2.5) + '" y="' + (y1 - 1) + '">' + flEsc(t) + '</text>';
      });
    });
    if (mk) for (const row of mk.rows) if (row.tactic) flTactic(row.tactic);
    s += '<rect id="flSel" class="sel" x="0" y="' + PT + '" width="1" height="' + (PH + TB) + '" visibility="hidden"/><g id="flHov" class="hov"></g></svg>';
    cont.innerHTML = s;
    cont.__m = { W, H, L, R, PT, axisY, TB, x, cOf, z0, z1, r, se, log, n, tops, mk, mkB, MR, MT };
  }
  function flEsc(t) { return String(t).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;'); }
  function flHover(cont, px, py) {
    const m = cont.__m, g = cont.querySelector('#flHov'); if (!m || !g) return;
    const st = S(), log = m.log, se = m.se;
    let best = null, bd = 9;
    const consider = (c, text, cls, mech) => { const d = Math.abs(m.x(c) - px); if (d < bd) { bd = d; best = { c, text, cls, mech }; } };
    // over a mechanic row only that row's ticks count
    const row = m.MT && py != null && py < m.MT + 2 ? m.mkB[Math.max(0, Math.min(m.mkB.length - 1, Math.floor((py - 4) / m.MR)))] : null;
    if (m.mk) for (const k of m.mk.uses) if (k.c >= m.z0 && k.c <= m.z1 && (row == null || k.boss === row)) consider(k.c, st.bossName(log, k.boss, k.actor) + ': ' + k.label, 'mech', k);
    if (row == null) {
      if (!fl.hide.hd) for (const h of se.dealt) if (h[0] >= m.z0 && h[0] <= m.z1) consider(h[0], 'you hit ' + st.actorLabel(log, h[3]) + ' ' + h[1].toLocaleString() + (h[4] ? ' crit' : '') + (h[2] ? ' (' + st.ability(log, h[2]).name + ')' : ''), 'hd');
      if (!fl.hide.ht) for (const h of se.taken) if (h[0] >= m.z0 && h[0] <= m.z1) consider(h[0], (h[1] ? 'hit on you ' + h[1].toLocaleString() : 'blocked') + (h[2] >= 0 ? '' : ''), 'ht');
      for (const k of se.kills) if (k[0] >= m.z0 && k[0] <= m.z1) consider(k[0], st.actorLabel(log, k[1]) + ' died', 'kill');
      for (const d of se.deaths) if (d[0] >= m.z0 && d[0] <= m.z1) consider(d[0], 'you died', 'death');
    }
    cont.__hov = { px, py, mech: best ? best.mech || null : null };
    if (!best) { g.innerHTML = ''; return; }
    const lines = [st.fmtMsTenths((best.c - m.r.start) * st.CYCLE_MS) + '  tick ' + (st.cycleTick(log, best.c) - st.cycleTick(log, m.r.start)), best.text];
    const cols = Math.max(24, Math.min(64, Math.floor((m.W - 24) / 6.2)));
    if (best.mech) {
      const seen = [], k = best.mech;
      for (const q of k.cues) { const t = (st.MECH_KINDS[q.kind] || 'kind ' + q.kind) + ' ' + q.id; if (seen.indexOf(t) < 0) seen.push(t); }
      for (const l of flWrap(seen.join(', ') + (k.actor >= 0 ? ', ' + st.actorLabel(log, k.actor) : ''), cols, 2)) lines.push(l);
    }
    const lp = fl.hide.lp ? null : se.lp.filter(p => p[0] <= best.c).pop(); if (lp) lines.push('your LP ' + lp[1].toLocaleString() + ' / ' + lp[2].toLocaleString());
    const tac = best.mech && best.mech.tactic ? flTactic(best.mech.tactic) : null;
    if (tac) {
      if (tac.title) lines.push(tac.title.length > cols ? tac.title.slice(0, cols - 3) + '...' : tac.title);
      for (const l of flWrap(tac.text, cols, 4)) lines.push(l);
    }
    const w = Math.max.apply(null, lines.map(l => l.length)) * 6.2 + 12, h = lines.length * 13 + 8;
    let bx = m.x(best.c) + 10; if (bx + w > m.W) bx = m.x(best.c) - w - 10; if (bx < 0) bx = 2;
    let out = '<line class="ax" x1="' + m.x(best.c) + '" x2="' + m.x(best.c) + '" y1="' + m.PT + '" y2="' + (m.axisY + m.TB) + '"/>';
    out += '<rect x="' + bx + '" y="' + (m.PT + 2) + '" width="' + w + '" height="' + h + '" rx="3"/>';
    lines.forEach((l, i) => { out += '<text x="' + (bx + 6) + '" y="' + (m.PT + 2 + 12 + i * 13) + '">' + flEsc(l) + '</text>'; });
    g.innerHTML = out;
  }

  function flBuffs(log, n, body) {
    const st = S(), up = st.uptimes(log, n);
    if (!up.rows.length) { body.appendChild(el('div', 'fl-empty', 'No buffs or debuffs in this fight.')); return; }
    const dur = Math.max(1, up.range.end - up.range.start);
    const grp = u => st.buffGroup ? st.buffGroup(log, u.struct) : (u.type ? 'debuff' : 'buff');
    for (const [key, title] of [['buff', 'Buffs'], ['debuff', 'Debuffs on you'], ['consumable', 'Consumables'], ['summon', 'Conjures']]) {
    const rows = up.rows.filter(u => { const k = grp(u); return k === key || (key === 'buff' && ['debuff', 'consumable', 'summon'].indexOf(k) < 0); });
    if (!rows.length && key !== 'buff' && key !== 'debuff') continue;
    body.appendChild(flH(title));
    if (!rows.length) { body.appendChild(el('div', 'fl-empty', key === 'debuff' ? 'No debuffs.' : 'No buffs.')); continue; }
    const g = el('div', 'fl-gantt');
    for (const u of rows) {
      const row = el('div', 'fl-gr' + (key === 'debuff' ? ' debuff' : ''));
      const nm = flName('n', u.name, u.icon, true); if (!u.icon && nm.firstChild) flBuffItem(nm.firstChild, u.struct, u.item); nm.dataset.tip = (u.fullName || u.name) + (u.desc ? '\n' + u.desc : '') + '\n' + (u.type ? 'debuff' : 'buff') + ', struct ' + u.struct + ', ' + u.spans.length + ' span' + (u.spans.length === 1 ? '' : 's');
      const bar = el('span', 'g');
      for (const sp of u.spans) { const i = el('i'); i.style.left = ((sp[0] - up.range.start) / dur * 100).toFixed(2) + '%'; i.style.width = Math.max(0.3, (sp[1] - sp[0]) / dur * 100).toFixed(2) + '%'; i.dataset.tip = st.fmtMs((sp[0] - up.range.start) * st.CYCLE_MS) + ' to ' + st.fmtMs((sp[1] - up.range.start) * st.CYCLE_MS); bar.appendChild(i); }
      row.appendChild(nm); row.appendChild(bar); row.appendChild(el('span', 'v', (u.uptime * 100).toFixed(0) + '%'));
      g.appendChild(row);
    }
    body.appendChild(g);
    }
  }

  // A change cell: the old item's icon and name (and count), then the new one; an empty slot reads "empty".
  function flChange(from, fromName, fromN, to, toName, toN) {
    const c = el('span', 'fl-chg nt');
    const side = (id, nm, n) => {
      if (id >= 0) { const ic = el('span', 'fl-ico none'); flItemIcon(ic, id); c.appendChild(ic); }
      c.appendChild(document.createTextNode((id >= 0 ? nm : 'empty') + (id >= 0 && n > 1 ? ' x' + n.toLocaleString() : '')));
    };
    side(from, fromName, fromN);
    c.appendChild(el('span', 'dim', ' to '));
    side(to, toName, toN);
    return c;
  }
  // Gear: what each equipment slot held over the fight, every swap, and what left the inventory.
  function flGear(log, n, body) {
    const st = S(), g = st.gear ? st.gear(log, n) : null;
    if (!g || !g.has) { body.appendChild(el('div', 'fl-empty', 'No gear in this log. Recorded from the next launcher update on.')); return; }
    const dur = Math.max(1, g.range.end - g.range.start), at = c => st.fmtMsTenths((c - g.range.start) * st.CYCLE_MS);
    body.appendChild(flH('Equipment'));
    if (!g.slots.length) body.appendChild(el('div', 'fl-empty', 'Nothing worn.'));
    else {
      const gg = el('div', 'fl-gantt fl-gear');
      for (const s of g.slots) {
        const row = el('div', 'fl-gr'), nm = el('span', 'n', s.name), bar = el('span', 'g');
        nm.dataset.tip = s.name + '\n' + s.spans.map(x => x.name + (x.spec ? ' storing ' + x.spec.name : '') + (x.perks && x.perks.length ? ' (' + x.perks.join(', ') + ')' : '')).join(', then ');
        const tone = {};   // one colour per item in this slot (an amulet per stored special), kept wherever it comes back
        s.spans.forEach((sp, k) => {
          const key = sp.item + ':' + (sp.spec ? sp.spec.item : '');
          if (tone[key] == null) tone[key] = Object.keys(tone).length % 6;
          const i = el('i'); i.className = 'k' + tone[key];
          i.style.left = ((sp.from - g.range.start) / dur * 100).toFixed(2) + '%';
          i.style.width = Math.max(0.3, (sp.to - sp.from) / dur * 100).toFixed(2) + '%';
          i.dataset.tip = sp.name + (sp.spec ? '\nStores ' + sp.spec.name : '') + (sp.perks && sp.perks.length ? '\n' + sp.perks.join(', ') : '') + '\n' + at(sp.from) + ' to ' + at(sp.to);
          const ic = el('span', 'fl-ico none'); flItemIcon(ic, sp.item); i.appendChild(ic); i.appendChild(document.createTextNode(sp.spec ? sp.spec.name : sp.name));
          bar.appendChild(i);
        });
        row.appendChild(nm); row.appendChild(bar); row.appendChild(el('span', 'v', String(s.spans.length)));
        row.lastChild.dataset.tip = s.spans.length === 1 ? 'Worn the whole time' : s.spans.length + ' items over the fight';
        gg.appendChild(row);
        // the perks of what this slot held, in order (a perk swap shows as two lists)
        const pl = []; for (const sp of s.spans) if (sp.perks && sp.perks.length) { const t = sp.perks.join(', '); if (pl[pl.length - 1] !== t) pl.push(t); }
        if (pl.length) { const pr = el('div', 'fl-perks', pl.join('  then  ')); pr.dataset.tip = pl.join('\nthen\n'); gg.appendChild(pr); }
        // an Essence of Finality: the special each amulet in this slot stored, in order
        const sl = []; for (const sp of s.spans) if (sp.spec && sl[sl.length - 1] !== sp.spec.name) sl.push(sp.spec.name);
        if (sl.length) { const pr = el('div', 'fl-perks', 'Stores ' + sl.join('  then  ')); pr.dataset.tip = 'Special attack stored in the amulet\n' + sl.join('\nthen\n'); gg.appendChild(pr); }
      }
      body.appendChild(gg);
    }
    body.appendChild(flH('Swaps'));
    if (!g.swaps.length) body.appendChild(el('div', 'fl-empty', 'No equipment changes.'));
    else {
      const rows = g.swaps.map(w => {
        const r = el('div', 'fl-tr');
        flCells(r, [[at(w.c), 'num'], [w.slotName, 'nt']]);
        const fp = (w.fromPerks || []).join(', '), tp = (w.toPerks || []).join(', ');
        const cell = el('span', 'fl-swapc');
        cell.dataset.tip = (w.fromName || 'empty') + (fp ? ' (' + fp + ')' : '') + '\nto\n' + (w.toName || 'empty') + (tp ? ' (' + tp + ')' : '');
        if (w.perksOnly) {
          // the same item with other perks: the item once, then only the perks that changed
          const ch = el('span', 'fl-chg nt'), ic = el('span', 'fl-ico none'); flItemIcon(ic, w.to);
          ch.appendChild(ic); ch.appendChild(document.createTextNode(w.toName)); cell.appendChild(ch);
          const was = w.fromPerks || [], now = w.toPerks || [];
          const out = was.filter(x => now.indexOf(x) < 0), inn = now.filter(x => was.indexOf(x) < 0);
          if (out.length || inn.length) cell.appendChild(el('span', 'fl-perkd', 'Perks: ' + (out.join(', ') || 'none') + '  to  ' + (inn.join(', ') || 'none')));
          const fs = w.fromSpec ? w.fromSpec.name : 'nothing', ts = w.toSpec ? w.toSpec.name : 'nothing';
          if (fs !== ts) cell.appendChild(el('span', 'fl-perkd', 'Stores: ' + fs + '  to  ' + ts));
        } else {
          cell.appendChild(flChange(w.from, w.fromName, 0, w.to, w.toName, 0));
          if (tp) cell.appendChild(el('span', 'fl-perkd', 'Perks: ' + tp));
          if (w.toSpec) cell.appendChild(el('span', 'fl-perkd', 'Stores: ' + w.toSpec.name));
        }
        r.appendChild(cell);
        return r;
      });
      body.appendChild(flTable('swaps', [['Time', 1], ['Slot'], ['Change']], rows));
    }
    body.appendChild(flH('Inventory at the start'));
    const inv = el('div', 'fl-inv');
    for (const x of g.startInv) {
      const cell = el('span');
      if (x.item >= 0) {
        flItemIcon(cell, x.item); cell.dataset.tip = x.name + (x.count > 1 ? ' x' + x.count.toLocaleString() : '') + (x.spec ? '\nStores ' + x.spec.name : '') + '\nslot ' + (x.slot + 1);
        if (x.count > 1) cell.appendChild(el('b', '', x.count >= 100000 ? Math.floor(x.count / 1000) + 'K' : String(x.count)));
        if (x.spec) { const w = el('i', 'fl-spec'); flItemIcon(w, x.spec.item); cell.appendChild(w); }   // the weapon whose special the amulet stores
      } else cell.dataset.tip = 'Empty, slot ' + (x.slot + 1);
      inv.appendChild(cell);
    }
    body.appendChild(inv);
    body.appendChild(flH('Inventory changes'));
    if (!g.invChanges.length) body.appendChild(el('div', 'fl-empty', 'No inventory changes.'));
    else {
      const rows = g.invChanges.map(w => {
        const r = el('div', 'fl-tr');
        flCells(r, [[at(w.c), 'num'], [String(w.slot + 1), 'num', 'Inventory slot ' + (w.slot + 1)]]);
        const ch = flChange(w.from, w.fromName, w.fromCount, w.to, w.toName, w.toCount);
        ch.dataset.tip = (w.fromName ? w.fromName + (w.fromCount > 1 ? ' x' + w.fromCount : '') : 'empty') + ' to ' + (w.toName ? w.toName + (w.toCount > 1 ? ' x' + w.toCount : '') : 'empty');
        r.appendChild(ch);
        return r;
      });
      body.appendChild(flTable('inv', [['Time', 1], ['Slot', 1], ['Change']], rows));
    }
    body.appendChild(flH('Used from the inventory'));
    if (!g.used.length) body.appendChild(el('div', 'fl-empty', 'Nothing used.'));
    else {
      const rows = g.used.map(u => {
        const r = el('div', 'fl-tr');
        flCells(r, [[u.name, 'nt', u.name], [String(u.used) + (u.unit ? ' ' + u.unit : ''), 'num'], [u.times.map(at).join(', '), 'nt opt', u.times.map(at).join(', ')]]);
        return r;
      });
      body.appendChild(flTable('used', [['Item'], ['Used', 1], ['When', 0, 1]], rows));
    }
  }

  function flCasts(log, n, body) {
    const st = S(), ca = st.casts(log, n);
    if (!ca.list.length) { body.appendChild(el('div', 'fl-empty', 'No casts in this fight.')); return; }
    const cells = new Array(ca.ticks).fill(0);   // 0 idle, 1 gap, 2 channel, 3 cast, 4 gcd-only cast
    const byTick = {};
    let prev = null;
    for (const k of ca.list) {
      if (k.tick < 0 || k.tick >= ca.ticks) continue;
      if (prev !== null) for (let t = prev + 3; t < k.tick; t++) if (!cells[t]) cells[t] = 1;
      for (let t = k.tick + 1; t < Math.min(ca.ticks, k.tick + k.span); t++) if (cells[t] < 2) cells[t] = 2;
      cells[k.tick] = k.src === 3 ? 4 : 3; (byTick[k.tick] = byTick[k.tick] || []).push(k); prev = k.tick;
    }
    const wrap = el('div', 'fl-rot'), row = el('div', 'fl-rotin');
    for (let t = 0; t < ca.ticks; t++) {
      const d = el('div', 'fl-tk' + (cells[t] === 1 ? ' gap' : cells[t] === 2 ? ' chan' : cells[t] >= 3 ? ' cast' + (cells[t] === 4 ? ' gcd' : '') : ''));
      const ks = byTick[t];
      if (ks) { const k = ks[ks.length - 1]; d.dataset.tip = ks.map(x => x.name).join('\n') + '\n' + st.fmtMsTenths(t * st.TICK * st.CYCLE_MS) + ', tick ' + t; d.textContent = k.name.charAt(0); if (k.icon) flSprite(d, k.icon); }
      row.appendChild(d);
    }
    wrap.appendChild(row);
    const ax = el('div', 'fl-rotax');
    for (let t = 0; t < ca.ticks; t += 50) ax.appendChild(el('span', '', st.fmtMs(t * st.TICK * st.CYCLE_MS)));
    wrap.appendChild(ax);
    body.appendChild(wrap);
    body.appendChild(el('div', 'fl-note2', ca.list.length + ' casts over ' + ca.ticks + ' ticks, ' + ca.idle + ' idle ticks after the global cooldown.'));
    const rows = [];
    for (const r of ca.table) {
      const tr = el('div', 'fl-tr');
      const nm = flName('n', r.name, r.icon, true, r.item); nm.dataset.tip = flRowTip(r); tr.appendChild(nm);
      flCells(tr, [[String(r.casts), 'num'], [st.fmtNum(r.avg), 'num opt', Math.round(r.avg).toLocaleString()], [st.fmtNum(r.total), 'num tot', r.total.toLocaleString()], [st.fmtNum(r.perCast), 'num', Math.round(r.perCast).toLocaleString()]]);
      rows.push(tr);
    }
    body.appendChild(flTable('casts', [['Ability'], ['Casts', 1], ['Avg hit', 1, 1], ['Total', 1], ['Per cast', 1]], rows));
  }

  function flMechs(log, n, body) {
    const st = S(), mk = st.mechs ? st.mechs(log, n) : null;
    if (!mk || !mk.rows.length) { body.appendChild(el('div', 'fl-empty', 'No boss mechanics in this fight.')); return; }
    const r = mk.range, dur = Math.max(1, r.end - r.start), at = c => st.fmtMs((c - r.start) * st.CYCLE_MS);
    const gap = ms => ms < 10000 ? (ms / 1000).toFixed(1) + 's' : st.fmtMs(ms);
    for (const boss of mk.bosses) {
      if (mk.bosses.length > 1) body.appendChild(flH(st.bossName(log, boss)));
      const rows = [];
      for (const x of mk.rows) {
        if (x.boss !== boss) continue;
        const id = x.id, tr = el('div', 'fl-tr click' + (fl.mhl === id ? ' on' : ''));
        const nm = flName('n', x.label); flTacTip(nm, x); tr.appendChild(nm);
        const has = x.gaps.length > 0;
        flCells(tr, [[String(x.count), 'num'], [at(x.first), 'num opt'], [at(x.last), 'num opt'],
                     [has ? gap(x.avgGap) : '-', 'num', has ? 'Between uses: ' + x.gaps.map(gap).join(', ') : ''], [has ? gap(x.minGap) : '-', 'num opt', has ? 'Longest ' + gap(x.maxGap) : '']]);
        const tl = el('span', 'tl'); tl.dataset.tip = x.times.map(at).join(', ');
        for (const c of x.times) { const i = el('i'); i.style.left = ((c - r.start) / dur * 100).toFixed(2) + '%'; tl.appendChild(i); }
        tr.appendChild(tl);
        tr.addEventListener('click', () => { fl.mhl = fl.mhl === id ? '' : id; fl.tab = 'health'; flPaint(); });
        rows.push(tr);
      }
      body.appendChild(flTable('mech', [['Mechanic'], ['Uses', 1], ['First', 1, 1], ['Last', 1, 1], ['Avg gap', 1], ['Min gap', 1, 1], ['Timeline']], rows));
    }
    body.appendChild(el('div', 'fl-note2', 'Click a row to mark it on the Timeline.'));
  }

  function flEvRows(log, n) {
    const st = S(), r = st.range(log, n), q = fl.evSearch.trim().toLowerCase(), out = [], counts = {};
    for (let i = 0; i < log.events.length; i++) {
      const e = log.events[i];
      if (e[1] < r.start) continue;
      if (e[1] > r.end) break;
      const cat = FL_EV_CAT[st.typeName ? st.typeName(log, e) : st.EVENT_NAMES[e[0]]] || 'other';
      counts[cat] = (counts[cat] || 0) + 1;
      if (!fl.evFilt[cat]) continue;
      const d = st.describe(log, i);
      if (q && (d.text + ' ' + d.actor + ' ' + d.ability + ' ' + d.kind + ' ' + d.value + ' ' + d.type).toLowerCase().indexOf(q) < 0) continue;
      d.cat = cat; d.ms = (e[1] - r.start) * st.CYCLE_MS; d.tick = st.cycleTick(log, e[1]) - st.cycleTick(log, r.start);
      out.push(d);
    }
    return { rows: out, counts };
  }
  function flEvents(log, n, body) {
    const st = S(), tb = el('div', 'fl-evbar');
    const search = el('input', 'pet-search'); search.placeholder = 'Search: text, ability, kind, value'; search.value = fl.evSearch;
    search.addEventListener('input', () => { fl.evSearch = search.value; fl.evShow = FL_ROWS; flEvList(log, n); });
    const copy = el('button', 'fl-btn', 'Copy rows'); copy.type = 'button'; copy.dataset.tip = 'Copy the matching rows as tab-separated text.';
    copy.addEventListener('click', () => {
      const rows = flEvRows(log, n).rows;
      const lines = ['time\ttick\tcycle\ttype\tactor\tevent\tkind\tvalue\thitmark\tability'];
      for (const d of rows) lines.push([st.fmtMsTenths(d.ms), d.tick, d.c, d.type, d.actor, d.text, d.kind, d.value, d.hitmark, d.ability].join('\t'));
      flCall('copyClipboard', lines.join('\n'));
      const cnt = $('flEvCnt'); if (cnt) cnt.textContent = 'Copied ' + rows.length + ' rows as tab-separated text.';
    });
    tb.appendChild(search); tb.appendChild(copy); body.appendChild(tb);
    const chips = el('div', 'pet-chips'); chips.id = 'flEvChips'; body.appendChild(chips);
    chips.addEventListener('click', e => { const b = e.target.closest ? e.target.closest('.pet-chip') : null; if (!b || !b.dataset.cat) return; fl.evFilt[b.dataset.cat] = !fl.evFilt[b.dataset.cat]; fl.evShow = FL_ROWS; flEvList(log, n); });
    body.appendChild(el('div', 'chat-count')).id = 'flEvCnt';
    const head = el('div', 'fl-ev h');
    for (const [c, t] of [['t', 'Time'], ['k', 'Tick'], ['e', 'Event'], ['v', 'Value'], ['a', 'Ability']]) head.appendChild(el('span', c, t));
    body.appendChild(head);
    const list = el('div', 'chat-list'); list.id = 'flEvList'; body.appendChild(list);
    flEvList(log, n);
  }
  function flEvList(log, n) {
    const st = S(), list = $('flEvList'), chips = $('flEvChips'), cnt = $('flEvCnt'); if (!list) return;
    const { rows, counts } = flEvRows(log, n);
    chips.innerHTML = '';
    for (const [cat, label] of FL_EV_CATS) { const b = el('button', 'pet-chip' + (fl.evFilt[cat] ? ' on' : ''), label + (counts[cat] ? ' ' + counts[cat] : '')); b.type = 'button'; b.dataset.cat = cat; chips.appendChild(b); }
    cnt.textContent = rows.length + ' events' + (rows.length > fl.evShow ? ', first ' + fl.evShow + ' shown' : '');
    list.innerHTML = '';
    if (!rows.length) { list.appendChild(el('div', 'chat-empty', 'No events match.')); return; }
    const frag = document.createDocumentFragment();
    for (const d of rows.slice(0, fl.evShow)) {
      const cls = d.type === 'hit' ? (/^you hit/.test(d.text) ? 'dealt' : /on you/.test(d.text) ? 'taken' : /healed/.test(d.text) ? 'heal' : 'dim') : d.type === 'cast' ? 'cast' : d.type === 'mech' ? 'mech' : (d.cat === 'vitals' || d.cat === 'anim' || d.cat === 'fx' || d.cat === 'tracker') ? 'dim' : '';
      const row = el('div', 'fl-ev ' + cls);
      row.appendChild(el('span', 't', st.fmtMsTenths(d.ms)));
      row.appendChild(el('span', 'k', String(d.tick)));
      row.appendChild(el('span', 'e', d.text));
      row.appendChild(el('span', 'v', d.value === '' ? '' : (typeof d.value === 'number' ? st.fmtNum(d.value) : String(d.value))));
      const a = el('span', 'a', d.ability || d.kind || ''); if (d.ability) a.dataset.tip = d.ability; row.appendChild(a);
      row.dataset.tip = d.text + '\ncycle ' + d.c + (d.hitmark !== '' ? '  hitmark ' + d.hitmark : '') + (d.actor ? '  ' + d.actor : '') + '  ' + d.type;
      frag.appendChild(row);
    }
    list.appendChild(frag);
    if (rows.length > fl.evShow) { const more = el('button', 'pet-chip', 'Show ' + Math.min(FL_ROWS, rows.length - fl.evShow) + ' more'); more.type = 'button'; more.addEventListener('click', () => { fl.evShow += FL_ROWS; flEvList(log, n); }); list.appendChild(more); }
  }

  Object.assign(window, { fetchFights: flPoll, flPaint });
  registerTab({ id: 'fights', label: 'Fight Logs', cat: 'Combat', icon: '<path d="M4 19h16"/><path d="M4 19V7"/><path d="M7 15l3-5 3 3 4-7"/><path d="M14 4l6 6-9 9-6-6z" opacity=".35"/>',
                render: renderFights, open: function () { fl.sig = ''; fl.rowsAt = 0; } });
})();
