// RuneToolsX panel: Fight Logs (fights recorded by the combat recorder: picker, summary strip, damage
// tables, health chart, buff uptimes, rotation strip, event list). Every number comes from
// panel_fights_stats.js so the website page shows the same figures. Without the recorder bridge
// functions (older launcher) the panel shows one line and nothing else.
(function () {

  const FL_TABS = [['overview', 'Overview'], ['dealt', 'Damage done'], ['taken', 'Damage taken'], ['health', 'Health'], ['buffs', 'Buffs'], ['casts', 'Casts'], ['events', 'Events']];
  const FL_EV_CATS = [['hit', 'Hits'], ['cast', 'Casts'], ['buff', 'Buffs'], ['vitals', 'Vitals'], ['anim', 'Animations'], ['target', 'Targets'], ['tracker', 'Trackers'], ['other', 'Other']];
  const FL_EV_CAT = { hit: 'hit', cast: 'cast', buff: 'buff', channel: 'cast', lp: 'vitals', adren: 'vitals', prayer: 'vitals', bar: 'vitals', stat: 'vitals', anim: 'anim', target: 'target', tracker: 'tracker' };
  const FL_ROWS = 400, FL_COLORS = ['#e0b34c', '#4cc0c0', '#c98cf0', '#e06c6c', '#7f9fbf', '#67c07a'];
  const FL_STYLE_COLOR = { melee: '#e06c6c', ranged: '#67c07a', magic: '#7f9fbf', necromancy: '#c98cf0', conjure: '#9a7fd0', typeless: '#9aa0ad', poison: '#5fd07a' };
  const fl = { rows: null, rowsAt: 0, char: '', logId: '', fight: -2, tab: 'overview', log: null, loading: '', live: null, liveId: '', liveSeq: 0, liveAt: 0,
               rec: null, recAt: 0, zoom: null, hl: 0, evFilt: { hit: true, cast: true, buff: true, vitals: false, anim: false, target: true, tracker: false, other: true },
               evSearch: '', evShow: FL_ROWS, sig: '', pickSig: '', status: '', confirmDel: 0, drag: null };
  const FL_SPR = new Map(), FL_SPR_PENDING = new Set();
  const S = () => window.combatStats;

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
    if (u) { e.style.backgroundImage = "url('" + u + "')"; return; }
    if (FL_SPR_PENDING.has(sid) || typeof rtxData !== 'object') return;
    FL_SPR_PENDING.add(sid);
    rtxData.raw('cache.sprite', sid).then(url => {
      FL_SPR_PENDING.delete(sid);
      if (!url) return;
      FL_SPR.set(sid, url);
      document.querySelectorAll('[data-spr="' + sid + '"]').forEach(x => { x.style.backgroundImage = "url('" + url + "')"; });
    }).catch(() => FL_SPR_PENDING.delete(sid));
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
    paneRun('fights', flPaint);
  }
  async function flRecToggle() {
    if (!flHas('combatRecordEnabled')) return;
    const want = !fl.rec;
    try { fl.rec = flBool(await flCall('combatRecordEnabled', want)); } catch (e) {}
    fl.recAt = 0;
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
    if (!w) {
      c.innerHTML = ''; fl.sig = ''; fl.pickSig = '';
      w = el('div', 'pk-wrap fl-wrap'); w.id = 'flWrap'; c.appendChild(w);
      const pick = el('div', 'fl-pick'); pick.id = 'flPick';
      fl.ddChar = flDd(v => { fl.char = v; fl.logId = ''; fl.fight = -2; flPickDefaults(); flPaint(); });
      fl.ddLog = flDd(v => flSelectLog(v));
      fl.ddFight = flDd(v => { fl.fight = Number(v); fl.zoom = null; fl.hl = 0; flPaint(); });
      const live = el('span', 'fl-chip', 'Live'); live.id = 'flLiveChip'; live.dataset.tip = 'The recorder has an open fight for this client. The view refreshes every second.';
      const rec = el('button', 'fl-rec'); rec.id = 'flRec'; rec.type = 'button'; rec.addEventListener('click', flRecToggle);
      pick.appendChild(fl.ddChar); pick.appendChild(fl.ddLog); pick.appendChild(fl.ddFight); pick.appendChild(live); pick.appendChild(rec);
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
    if (!S()) { $('flNote').textContent = 'panel_fights_stats.js did not load.'; return; }
    const has = flHas('fightsList');
    const log = flLog(), n = fl.fight;
    flPaintPicker(has);
    const width = ($('flBody') && $('flBody').clientWidth) || 0;
    const sig = [has, fl.logId, n, fl.tab, log ? log.events.length : -1, log ? (log.fights || []).length : -1, fl.zoom ? fl.zoom.join(',') : '', fl.hl, fl.evSearch, JSON.stringify(fl.evFilt),
                 fl.evShow, fl.rec, fl.liveId, fl.loading, fl.status, fl.confirmDel, width, flRows().length].join('|');
    if (sig === fl.sig) return;
    fl.sig = sig;
    const note = $('flNote');
    note.textContent = !has ? 'This launcher build has no fight recorder.' : fl.status ? fl.status : fl.loading ? 'Loading...' : '';
    const rec = $('flRec');
    if (fl.rec === null) rec.style.display = 'none';
    else { rec.style.display = ''; rec.textContent = 'Record: ' + (fl.rec ? 'on' : 'off'); rec.className = 'fl-rec ' + (fl.rec ? 'on' : 'off'); rec.dataset.tip = fl.rec ? 'Fights are being recorded to disk. Click to stop.' : 'Nothing is recorded. Click to start recording fights.'; }
    $('flLiveChip').className = 'fl-chip' + (fl.live ? ' on' : '');
    for (const b of $('flTabs').children) b.classList.toggle('on', b.dataset.tab === fl.tab);
    flPaintStrip(log, n);
    const body = $('flBody'); body.innerHTML = '';
    if (!log) {
      body.appendChild(el('div', 'fl-empty', !has ? '' : fl.loading ? 'Loading...' : flRows().length || fl.live ? 'Select a log.' : 'No fights recorded yet.' + (fl.rec === false ? ' Recording is off.' : '')));
    } else {
      const r = { overview: flOverview, dealt: flDealt, taken: flTaken, health: flHealth, buffs: flBuffs, casts: flCasts, events: flEvents }[fl.tab] || flOverview;
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
    fl.ddChar.setItems(chars.map(c => ({ value: c, label: c })), fl.char);
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
    for (const f of fights) fi.push({ value: f.n, label: '#' + (f.n + 1) + ' ' + (f.boss || (f.kind === 'kills' ? 'kills x' + (f.kills || 0) : f.kind || '')) + ' ' + S().fmtMs((f.end - f.start) * 20) });
    if (!fi.length) fi.push({ value: -1, label: fl.logId === 'live' ? 'Open fight' : 'Whole log' });
    fl.ddFight.setItems(fi, fl.fight);
    $('flPick').style.display = has || fl.live ? '' : 'none';
  }
  function flPaintStrip(log, n) {
    const strip = $('flStrip'), line = $('flLine');
    strip.innerHTML = ''; line.textContent = '';
    if (!log) { strip.style.display = 'none'; return; }
    strip.style.display = '';
    const st = S(), sm = st.summary(log, n);
    const cells = [['Duration', st.fmtMs(sm.durMs), ''], ['DPS', Math.round(sm.dps).toLocaleString(), sm.dpm.toFixed(0) + ' per minute'], ['Dealt', st.fmtNum(sm.dealt), sm.dealt.toLocaleString() + ' in ' + sm.hits + ' hits, ' + sm.crits + ' crits'],
                   ['Taken', st.fmtNum(sm.taken), sm.taken.toLocaleString() + ', ' + sm.blocked + ' blocked' + (sm.healed ? ', healed ' + sm.healed.toLocaleString() : '')], ['Deaths', String(sm.deaths), ''], ['Max hit', st.fmtNum(sm.maxHit), sm.maxHit.toLocaleString()]];
    for (const [k, v, tip] of cells) {
      const d = el('div', 'fl-kv'); d.appendChild(el('div', 'k', k)); d.appendChild(el('div', 'v', v)); if (tip) d.dataset.tip = tip; strip.appendChild(d);
    }
    const f = n >= 0 && log.fights ? log.fights.find(x => x.n === n) : null;
    const parts = [];
    if (f && f.boss) parts.push(f.boss);
    else parts.push('kills x' + sm.kills);
    const tg = {};
    for (const [a] of sm.targets) { const act = st.actorOf(log, a); const key = (act.name || 'NPC') + (act.id >= 0 ? ' (' + act.id + ')' : ''); tg[key] = (tg[key] || 0) + 1; }
    const tk = Object.keys(tg);
    if (tk.length) parts.push(tk.slice(0, 3).map(k => k + (tg[k] > 1 ? ' x' + tg[k] : '')).join(', ') + (tk.length > 3 ? ' +' + (tk.length - 3) : ''));
    parts.push('companion ' + (log.log && log.log.companion ? 'yes' : 'no'));
    line.textContent = parts.join('  \u00b7  ');
  }
  function flPaintButtons(log) {
    const btns = $('flBtns'); btns.innerHTML = '';
    const id = fl.logId;
    if (!log || !id || id === 'live') return;
    const mk = (label, cls, fn, tip) => { const b = el('button', 'fl-btn' + (cls ? ' ' + cls : ''), label); b.type = 'button'; if (tip) b.dataset.tip = tip; b.addEventListener('click', fn); btns.appendChild(b); return b; };
    const row = flRows().find(r => r.id === id);
    if (flHas('fightUpload') && flHas('fightUploadStatus') && !(row && row.upload)) mk('Upload', 'gold', async () => { fl.status = 'Uploading...'; flPaint(); try { const r = await flCall('fightUpload', id); fl.status = r && r.ok ? 'Upload queued.' : 'Upload failed.'; } catch (e) { fl.status = 'Upload failed.'; } fl.rowsAt = 0; flPaint(); });
    if (flHas('fightExport')) mk('Export', '', async () => { try { const r = await flCall('fightExport', id, true); fl.status = r && r.ok ? 'Exported to ' + (r.path || 'the export folder') + '.' : (r && r.error ? String(r.error) : ''); } catch (e) { fl.status = 'Export failed.'; } flPaint(); }, 'Save the log as .json.gz (Save As)');
    if (flHas('fightOpenFolder')) mk('Open folder', '', () => { flCall('fightOpenFolder'); });
    if (flHas('fightDelete')) {
      const now = Date.now();
      if (fl.confirmDel && now - fl.confirmDel < 4000) mk('Confirm delete', 'warn', async () => { fl.confirmDel = 0; try { await flCall('fightDelete', id); } catch (e) {} fl.logId = ''; fl.log = null; fl.rowsAt = 0; fl.status = 'Deleted.'; await flFetchList(true); flPaint(); });
      else mk('Delete', '', () => { fl.confirmDel = Date.now(); flPaint(); setTimeout(() => { if (fl.confirmDel && Date.now() - fl.confirmDel >= 4000) { fl.confirmDel = 0; paneRun('fights', flPaint); } }, 4200); });
    }
  }

  // ---- tabs --------------------------------------------------------------------------------------------------
  function flH(text) { return el('div', 'fl-h', text); }
  function flIcon(sid) { const i = el('span', 'fl-ico'); flSprite(i, sid); return i; }
  function flBar(label, value, total, cls, icon) {
    const st = S(), row = el('div', 'fl-bar' + (cls ? ' ' + cls : ''));
    const nm = el('span', 'n'); if (icon) nm.appendChild(flIcon(icon)); nm.appendChild(document.createTextNode(label)); nm.dataset.tip = label;
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
    for (const s of st.STYLES) { if (!ss.split[s]) continue; const r = flBar(s, ss.split[s], ss.total, ''); r.querySelector('i').style.background = FL_STYLE_COLOR[s] || ''; g.appendChild(r); }
    if (!ss.total) g.appendChild(el('div', 'fl-empty', 'No damage dealt.'));
    body.appendChild(g);
    body.appendChild(flH('Top abilities'));
    const g2 = el('div', 'fl-bars');
    for (const row of ab.rows.filter(r => r.struct).slice(0, 5)) g2.appendChild(flBar(row.name, row.total, ab.total, '', row.icon));
    if (sm.unattributed) g2.appendChild(flBar('Unattributed', sm.unattributed, ab.total, 'dim'));
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
    const h = el('div', 'fl-tr h'); for (const x of head) h.appendChild(el('span', x[1] ? 'num' : '', x[0])); t.appendChild(h);
    for (const r of rows) t.appendChild(r);
    return t;
  }
  function flCells(row, cells) { for (const [text, cls, tip] of cells) { const s = el('span', cls || '', text); if (tip) s.dataset.tip = tip; row.appendChild(s); } }
  function flShare(share) { const s = el('span', 'sh'); const i = el('i'); i.style.width = (share * 100).toFixed(1) + '%'; s.appendChild(i); s.dataset.tip = (share * 100).toFixed(1) + '%'; return s; }
  function flDealt(log, n, body) {
    const st = S(), ab = st.byAbility(log, n);
    if (!ab.rows.length) { body.appendChild(el('div', 'fl-empty', 'No damage dealt.')); return; }
    const rows = [];
    for (const r of ab.rows) {
      const tr = el('div', 'fl-tr click' + (r.struct ? '' : ' dim') + (fl.hl && fl.hl === r.struct ? ' on' : ''));
      const nm = el('span', 'n'); if (r.icon) nm.appendChild(flIcon(r.icon)); nm.appendChild(document.createTextNode(r.name)); nm.dataset.tip = r.name + (r.struct ? ' (struct ' + r.struct + ')' : '\nHits no cast explains: procs, bleeds, hits before the first cast'); tr.appendChild(nm);
      flCells(tr, [[String(r.hits), 'num'], [String(r.crits), 'num'], [st.fmtNum(r.avg), 'num', Math.round(r.avg).toLocaleString()], [st.fmtNum(r.max), 'num', r.max.toLocaleString()], [st.fmtNum(r.total), 'num tot', r.total.toLocaleString()]]);
      tr.appendChild(flShare(r.share));
      tr.addEventListener('click', () => { fl.hl = fl.hl === r.struct ? 0 : r.struct; fl.tab = 'health'; flPaint(); });
      rows.push(tr);
    }
    body.appendChild(flTable('', [['Ability'], ['Hits', 1], ['Crits', 1], ['Avg', 1], ['Max', 1], ['Total', 1], ['Share', 1]], rows));
    body.appendChild(el('div', 'fl-note2', 'Click a row to mark its hits on the Health chart.'));
  }
  function flTaken(log, n, body) {
    const st = S(), bs = st.bySource(log, n);
    if (!bs.rows.length) { body.appendChild(el('div', 'fl-empty', 'No damage taken.')); return; }
    const rows = [];
    for (const r of bs.rows) {
      const tr = el('div', 'fl-tr' + (r.actor < 0 ? ' dim' : ''));
      const nm = el('span', 'n', r.name + (r.id >= 0 ? ' (' + r.id + ')' : '')); nm.dataset.tip = r.actor < 0 ? 'No NPC targeted you at that tick' : 'actor ' + r.actor; tr.appendChild(nm);
      flCells(tr, [[String(r.hits), 'num'], [String(r.blocked), 'num'], [st.fmtNum(r.avg), 'num', Math.round(r.avg).toLocaleString()], [st.fmtNum(r.max), 'num', r.max.toLocaleString()], [st.fmtNum(r.total), 'num tot', r.total.toLocaleString()]]);
      tr.appendChild(flShare(r.share));
      rows.push(tr);
    }
    body.appendChild(flTable('taken', [['Source'], ['Hits', 1], ['Blocked', 1], ['Avg', 1], ['Max', 1], ['Total', 1], ['Share', 1]], rows));
  }

  // Health chart: player LP (left axis), target LP % (right axis), adrenaline area, prayer dotted, hits as
  // ticks above (dealt) and below (taken) the axis, kills and deaths as rules, buffs as a band. Drag zooms.
  function flHealth(log, n, body) {
    const st = S(), se = st.series(log, n);
    const cont = el('div', 'fl-chart'); cont.id = 'flChart'; body.appendChild(cont);
    const legend = el('div', 'fl-legend');
    const lg = (c, t) => { const s = el('span'); const i = el('i'); i.style.background = c; s.appendChild(i); s.appendChild(document.createTextNode(t)); legend.appendChild(s); };
    lg('#e8eaf0', 'your LP'); lg('rgba(232,194,106,0.8)', 'adrenaline'); lg('#4cc0c0', 'prayer'); lg('#5fd07a', 'dealt'); lg('#ff6b6b', 'taken');
    const tops = st.summary(log, n).targets.slice(0, 4);
    tops.forEach(([a], i) => { const act = st.actorOf(log, a); lg(FL_COLORS[i % FL_COLORS.length], (act.name || 'NPC') + ' LP %'); });
    body.appendChild(legend);
    body.appendChild(el('div', 'fl-note2', fl.zoom ? 'Zoomed. Double-click to reset.' : 'Drag to zoom. Hover for the nearest hit.'));
    flDraw(cont, log, n, se, tops.map(t => t[0]));
    cont.addEventListener('mousedown', e => { const m = cont.__m; if (!m) return; fl.drag = { x0: flPx(cont, e), x1: flPx(cont, e) }; });
    cont.addEventListener('mousemove', e => {
      const m = cont.__m; if (!m) return;
      const px = flPx(cont, e);
      if (fl.drag) { fl.drag.x1 = px; const sel = cont.querySelector('#flSel'); if (sel) { const a = Math.min(fl.drag.x0, px), b = Math.max(fl.drag.x0, px); sel.setAttribute('x', a); sel.setAttribute('width', Math.max(1, b - a)); sel.setAttribute('visibility', 'visible'); } return; }
      flHover(cont, px);
    });
    const up = () => {
      const m = cont.__m, d = fl.drag; fl.drag = null;
      if (!m || !d) return;
      if (Math.abs(d.x1 - d.x0) < 6) { const sel = cont.querySelector('#flSel'); if (sel) sel.setAttribute('visibility', 'hidden'); return; }
      const a = Math.max(m.r.start, Math.min(m.r.end, m.cOf(Math.min(d.x0, d.x1)))), b = Math.max(m.r.start, Math.min(m.r.end, m.cOf(Math.max(d.x0, d.x1))));
      if (b - a >= 30) { fl.zoom = [Math.floor(a), Math.ceil(b)]; flPaint(); }
    };
    cont.addEventListener('mouseup', up);
    cont.addEventListener('mouseleave', () => { if (fl.drag) up(); const g = cont.querySelector('#flHov'); if (g) g.innerHTML = ''; });
    cont.addEventListener('dblclick', () => { fl.zoom = null; fl.drag = null; flPaint(); });
  }
  function flPx(cont, e) {
    const m = cont.__m, rect = cont.getBoundingClientRect ? cont.getBoundingClientRect() : { left: 0, width: m.W };
    return (e.clientX - rect.left) * (rect.width ? m.W / rect.width : 1);
  }
  function flDraw(cont, log, n, se, tops) {
    const st = S(), r = se.range;
    const W = Math.max(300, cont.clientWidth || (cont.parentElement && cont.parentElement.clientWidth) || 360);
    const L = 44, R = 34, PT = 8, PH = 148, TB = 34, AX = 14, BB = 20;
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
    if (se.adren.length) s += '<path class="adr" d="' + step(se.adren, p => p[1] / 1000, yPct, true) + '"/>';
    if (se.prayer.length) s += '<path class="pr" d="' + step(se.prayer, p => p[2] > 0 ? p[1] / (p[2] * 100) : 0, yPct, false) + '"/>';
    tops.forEach((a, i) => {
      const t = se.targets[a]; if (!t) return;
      for (const seg of t.segs) if (seg.length) s += '<path class="tg" stroke="' + FL_COLORS[i % FL_COLORS.length] + '" d="' + step(seg, p => (p[2] || t.lpMax) ? p[1] / (p[2] || t.lpMax) : 0, yPct, false, seg[seg.length - 1][0] + st.TICK) + '"/>';
    });
    if (se.lp.length) s += '<path class="lp" d="' + step(se.lp, p => p[1], yLp, false) + '"/>';
    for (const k of se.kills) if (k[0] >= z0 && k[0] <= z1) s += '<line class="kill" x1="' + f1(x(k[0])) + '" x2="' + f1(x(k[0])) + '" y1="' + PT + '" y2="' + (axisY + TB) + '"/>';
    for (const d of se.deaths) if (d[0] >= z0 && d[0] <= z1) s += '<line class="death" x1="' + f1(x(d[0])) + '" x2="' + f1(x(d[0])) + '" y1="' + PT + '" y2="' + (axisY + TB) + '"/>';
    const maxD = se.dealt.reduce((m, h) => Math.max(m, h[1]), 1), maxT = se.taken.reduce((m, h) => Math.max(m, h[1]), 1);
    for (const h of se.dealt) { if (h[0] < z0 || h[0] > z1) continue; const hh = 3 + 28 * h[1] / maxD; s += '<line class="hd' + (fl.hl && h[2] === fl.hl ? ' hl' : '') + (h[4] ? ' crit' : '') + '" x1="' + f1(x(h[0])) + '" x2="' + f1(x(h[0])) + '" y1="' + axisY + '" y2="' + f1(axisY - hh) + '"/>'; }
    for (const h of se.taken) { if (h[0] < z0 || h[0] > z1) continue; const hh = h[1] ? 3 + 28 * h[1] / maxT : 3; s += '<line class="ht' + (h[1] ? '' : ' blk') + '" x1="' + f1(x(h[0])) + '" x2="' + f1(x(h[0])) + '" y1="' + axisY + '" y2="' + f1(axisY + hh) + '"/>'; }
    s += '<line class="ax" x1="' + L + '" x2="' + (W - R) + '" y1="' + axisY + '" y2="' + axisY + '"/>';
    s += '<text class="lbl" x="' + (L - 4) + '" y="' + (PT + 8) + '" text-anchor="end">' + st.fmtNum(lpMax) + '</text><text class="lbl" x="' + (L - 4) + '" y="' + f1(yLp(lpMax / 2) + 3) + '" text-anchor="end">' + st.fmtNum(lpMax / 2) + '</text><text class="lbl" x="' + (L - 4) + '" y="' + (axisY - 1) + '" text-anchor="end">0</text>';
    s += '<text class="lbl" x="' + (W - R + 4) + '" y="' + (PT + 8) + '">100%</text><text class="lbl" x="' + (W - R + 4) + '" y="' + f1(yPct(0.5) + 3) + '">50%</text><text class="lbl" x="' + (W - R + 4) + '" y="' + (axisY - 1) + '">0%</text>';
    const spanMs = span * st.CYCLE_MS, stepS = [5, 10, 15, 30, 60, 120, 300, 600, 1800].find(v => spanMs / (v * 1000) <= 7) || 3600;
    const t0 = (z0 - r.start) * st.CYCLE_MS;
    for (let t = Math.ceil(t0 / (stepS * 1000)) * stepS * 1000; t <= t0 + spanMs; t += stepS * 1000) {
      const c = r.start + t / st.CYCLE_MS;
      s += '<line class="ax" x1="' + f1(x(c)) + '" x2="' + f1(x(c)) + '" y1="' + (axisY + TB) + '" y2="' + (axisY + TB + 3) + '"/><text class="lbl" x="' + f1(x(c)) + '" y="' + (axisY + TB + AX - 2) + '" text-anchor="middle">' + st.fmtMs(t) + '</text>';
    }
    const up = st.uptimes(log, n).rows.filter(u => u.uptime < 0.98).slice(0, 6);
    up.forEach((u, i) => {
      const y = axisY + TB + AX + 2 + i * 3;
      for (const sp of u.spans) { if (sp[1] < z0 || sp[0] > z1) continue; s += '<line class="buffb" stroke="' + (u.type ? '#e06c6c' : FL_COLORS[i % FL_COLORS.length]) + '" x1="' + f1(x(Math.max(z0, sp[0]))) + '" x2="' + f1(x(Math.min(z1, sp[1]))) + '" y1="' + y + '" y2="' + y + '"><title>' + flEsc(u.name) + '</title></line>'; }
    });
    s += '<rect id="flSel" class="sel" x="0" y="' + PT + '" width="1" height="' + (PH + TB) + '" visibility="hidden"/><g id="flHov" class="hov"></g></svg>';
    cont.innerHTML = s;
    cont.__m = { W, H, L, R, PT, axisY, TB, x, cOf, z0, z1, r, se, log, n, tops };
  }
  function flEsc(t) { return String(t).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;'); }
  function flHover(cont, px) {
    const m = cont.__m, g = cont.querySelector('#flHov'); if (!m || !g) return;
    const st = S(), log = m.log, se = m.se;
    let best = null, bd = 9;
    const consider = (c, text, cls) => { const d = Math.abs(m.x(c) - px); if (d < bd) { bd = d; best = { c, text, cls }; } };
    for (const h of se.dealt) if (h[0] >= m.z0 && h[0] <= m.z1) consider(h[0], 'you hit ' + st.actorLabel(log, h[3]) + ' ' + h[1].toLocaleString() + (h[4] ? ' crit' : '') + (h[2] ? ' (' + st.ability(log, h[2]).name + ')' : ''), 'hd');
    for (const h of se.taken) if (h[0] >= m.z0 && h[0] <= m.z1) consider(h[0], (h[1] ? 'hit on you ' + h[1].toLocaleString() : 'blocked') + (h[2] >= 0 ? '' : ''), 'ht');
    for (const k of se.kills) if (k[0] >= m.z0 && k[0] <= m.z1) consider(k[0], st.actorLabel(log, k[1]) + ' died', 'kill');
    for (const d of se.deaths) if (d[0] >= m.z0 && d[0] <= m.z1) consider(d[0], 'you died', 'death');
    if (!best) { g.innerHTML = ''; return; }
    const lines = [st.fmtMsTenths((best.c - m.r.start) * st.CYCLE_MS) + '  tick ' + (st.cycleTick(log, best.c) - st.cycleTick(log, m.r.start)), best.text];
    const lp = se.lp.filter(p => p[0] <= best.c).pop(); if (lp) lines.push('your LP ' + lp[1].toLocaleString() + ' / ' + lp[2].toLocaleString());
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
    const dur = Math.max(1, up.range.end - up.range.start), g = el('div', 'fl-gantt');
    for (const u of up.rows) {
      const row = el('div', 'fl-gr' + (u.type ? ' debuff' : ''));
      const nm = el('span', 'n'); if (u.icon) nm.appendChild(flIcon(u.icon)); nm.appendChild(document.createTextNode(u.name)); nm.dataset.tip = (u.fullName || u.name) + '\n' + (u.type ? 'debuff' : 'buff') + ', struct ' + u.struct + ', ' + u.spans.length + ' span' + (u.spans.length === 1 ? '' : 's');
      const bar = el('span', 'g');
      for (const sp of u.spans) { const i = el('i'); i.style.left = ((sp[0] - up.range.start) / dur * 100).toFixed(2) + '%'; i.style.width = Math.max(0.3, (sp[1] - sp[0]) / dur * 100).toFixed(2) + '%'; i.dataset.tip = st.fmtMs((sp[0] - up.range.start) * st.CYCLE_MS) + ' to ' + st.fmtMs((sp[1] - up.range.start) * st.CYCLE_MS); bar.appendChild(i); }
      row.appendChild(nm); row.appendChild(bar); row.appendChild(el('span', 'v', (u.uptime * 100).toFixed(0) + '%'));
      g.appendChild(row);
    }
    body.appendChild(g);
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
      const nm = el('span', 'n'); if (r.icon) nm.appendChild(flIcon(r.icon)); nm.appendChild(document.createTextNode(r.name)); nm.dataset.tip = r.name + ' (struct ' + r.struct + ')'; tr.appendChild(nm);
      flCells(tr, [[String(r.casts), 'num'], [st.fmtNum(r.avg), 'num', Math.round(r.avg).toLocaleString()], [st.fmtNum(r.total), 'num tot', r.total.toLocaleString()], [st.fmtNum(r.perCast), 'num', Math.round(r.perCast).toLocaleString()]]);
      rows.push(tr);
    }
    body.appendChild(flTable('casts', [['Ability'], ['Casts', 1], ['Avg hit', 1], ['Total', 1], ['Per cast', 1]], rows));
  }

  function flEvRows(log, n) {
    const st = S(), r = st.range(log, n), q = fl.evSearch.trim().toLowerCase(), out = [], counts = {};
    for (let i = 0; i < log.events.length; i++) {
      const e = log.events[i];
      if (e[1] < r.start) continue;
      if (e[1] > r.end) break;
      const cat = FL_EV_CAT[st.EVENT_NAMES[e[0]]] || 'other';
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
    const st = S(), tb = el('div', 'chat-toolbar');
    const search = el('input', 'pet-search'); search.placeholder = 'Search: text, ability, kind, value'; search.value = fl.evSearch;
    search.addEventListener('input', () => { fl.evSearch = search.value; fl.evShow = FL_ROWS; flEvList(log, n); });
    const copy = el('button', 'pet-chip', 'Copy rows'); copy.type = 'button'; copy.dataset.tip = 'Copy the matching rows as tab-separated text.';
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
      const cls = d.type === 'hit' ? (/^you hit/.test(d.text) ? 'dealt' : /on you/.test(d.text) ? 'taken' : /healed/.test(d.text) ? 'heal' : 'dim') : d.type === 'cast' ? 'cast' : (d.cat === 'vitals' || d.cat === 'anim' || d.cat === 'tracker') ? 'dim' : '';
      const row = el('div', 'fl-ev ' + cls);
      row.appendChild(el('span', 't', st.fmtMsTenths(d.ms)));
      row.appendChild(el('span', 'k', String(d.tick)));
      row.appendChild(el('span', 'e', d.text));
      row.appendChild(el('span', 'v', d.value === '' ? '' : (typeof d.value === 'number' ? st.fmtNum(d.value) : String(d.value))));
      const a = el('span', 'a', d.ability || d.kind || ''); if (d.ability) a.dataset.tip = d.ability; row.appendChild(a);
      row.dataset.tip = 'cycle ' + d.c + (d.hitmark !== '' ? '  hitmark ' + d.hitmark : '') + (d.actor ? '  ' + d.actor : '') + '  ' + d.type;
      frag.appendChild(row);
    }
    list.appendChild(frag);
    if (rows.length > fl.evShow) { const more = el('button', 'pet-chip', 'Show ' + Math.min(FL_ROWS, rows.length - fl.evShow) + ' more'); more.type = 'button'; more.addEventListener('click', () => { fl.evShow += FL_ROWS; flEvList(log, n); }); list.appendChild(more); }
  }

  Object.assign(window, { fetchFights: flPoll, flPaint });
  registerTab({ id: 'fights', label: 'Fight Logs', cat: 'Combat', icon: '<path d="M4 19h16"/><path d="M4 19V7"/><path d="M7 15l3-5 3 3 4-7"/><path d="M14 4l6 6-9 9-6-6z" opacity=".35"/>',
                render: renderFights, open: function () { fl.sig = ''; fl.rowsAt = 0; } });
})();
