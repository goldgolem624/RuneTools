// RuneToolsX panel: Console (Developer). Every line the client, the launcher and any plugin
// (HTML or Lua) logs, with level and source filters, search, per-line copy and export.
// Reads rtxConsole (core/rtx-console.js) and tails launcher.log through host.logTail while open.
(function () {

  const LV = ['debug', 'info', 'warn', 'error'];
  const SHOW_MAX = 2000;                       // rows rendered at once (the ring keeps 5000)
  const DEF = { levels: { debug: true, info: true, warn: true, error: true }, source: 'all', tag: '', q: '', regex: false, autoscroll: true, paused: false, wrap: true };
  let cfg = null;
  let dirty = true, timer = 0, logTimer = 0, logOff = 0, logBusy = false;
  let pausedAt = 0;                            // seq of the newest row shown while paused
  let rowsShown = 0, rowsTotal = 0;
  let flashT = 0;

  function cfgLoad() {
    if (cfg) return cfg;
    cfg = Object.assign({}, DEF, { levels: Object.assign({}, DEF.levels) });
    try {
      const raw = prefGet('rtxConsoleCfg', null);
      if (raw) { const v = JSON.parse(raw); if (v && typeof v === 'object') { Object.assign(cfg, v); cfg.levels = Object.assign({}, DEF.levels, v.levels || {}); } }
    } catch (e) {}
    cfg.q = ''; cfg.paused = false;            // never persist a search or a pause
    return cfg;
  }
  function cfgSave() { try { prefSet('rtxConsoleCfg', JSON.stringify(cfg)); } catch (e) {} }

  rtxConsole.on(() => { dirty = true; });

  // launcher.log lines carry no level: infer one from the wording, ignoring counts like "0 failed"
  function levelOf(line) {
    const l = line.toLowerCase();
    if (/\b(error|crash|crashed|threw|exception|fatal|unhandled)\b/.test(l)) return 'error';
    if (/\bfail(ed|ure|s)?\b/.test(l) && !/\b0 fail/.test(l)) return 'warn';
    if (/\b(warn|warning|retry|retrying|refused|not set|timed out|timeout)\b/.test(l)) return 'warn';
    return 'info';
  }
  // "[HH:MM:SS.mmm] message": keep the launcher's own time as the entry time
  function launcherTime(hms) {
    const m = /^(\d\d):(\d\d):(\d\d)(?:\.(\d{1,3}))?$/.exec(hms);
    if (!m) return Date.now();
    const d = new Date();
    d.setHours(+m[1], +m[2], +m[3], m[4] ? +(m[4] + '00').slice(0, 3) : 0);
    if (d.getTime() > Date.now() + 60000) d.setDate(d.getDate() - 1);   // a line from before midnight
    return d.getTime();
  }
  async function pollLauncherLog() {
    if (logBusy || !paneVisible('console')) return;
    logBusy = true;
    try {
      const r = await rtxData.call('host.logTail', logOff);
      if (r && typeof r.text === 'string') {
        if (r.text) {
          for (const ln of r.text.split(/\r?\n/)) {
            if (!ln) continue;
            const m = /^\[(\d\d:\d\d:\d\d(?:\.\d+)?)\]\s*(.*)$/.exec(ln);
            const text = m ? m[2] : ln;
            const rec = rtxConsole.push({ level: levelOf(text), source: 'launcher', text });
            if (m) rec.t = launcherTime(m[1]);
          }
        }
        logOff = Number(r.offset) || 0;
      }
    } catch (e) {}
    logBusy = false;
  }

  function matches(rec) {
    if (!cfg.levels[rec.level]) return false;
    if (cfg.source !== 'all') {
      if (cfg.source === 'plugins') { if (rec.source !== 'plugin') return false; }
      else if (cfg.source.indexOf('plugin:') === 0) { if (rec.source !== 'plugin' || rec.plugin !== cfg.source.slice(7)) return false; }
      else if (rec.source !== cfg.source) return false;
    }
    if (cfg.tag && rec.tag !== cfg.tag) return false;
    if (cfg.q) {
      if (cfg.regex) {
        try { if (!new RegExp(cfg.q.slice(0, 200), 'i').test(rec.text)) return false; }
        catch (e) { if (rec.text.toLowerCase().indexOf(cfg.q.toLowerCase()) < 0) return false; }
      } else if ((rec.text + ' ' + rec.tag + ' ' + rec.plugin).toLowerCase().indexOf(cfg.q.toLowerCase()) < 0) return false;
    }
    return true;
  }

  // Theme language shared with Alerts / Events: segmented controls (.al-seg), the themed dropdown
  // (.al-sndsel + .sndmenu), .al-input fields, flat bordered buttons and an uppercase accent title.
  const CSS = `
    .cs-wrap { display: flex; flex-direction: column; height: 100%; min-height: 0; }
    .cs-head { display: flex; align-items: center; gap: 8px; padding: 12px 14px 8px; flex-wrap: wrap; }
    .cs-title { flex: 1; font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: .05em; color: var(--accent-hi); }
    .cs-btn { appearance: none; font: inherit; font-size: 11px; font-weight: 600; padding: 4px 10px; border-radius: 6px; border: 1px solid var(--border-hi); background: var(--bg-elev-2); color: var(--text); cursor: pointer; white-space: nowrap; flex: none; }
    .cs-btn:hover { border-color: var(--accent-hi); }
    .cs-tools { display: flex; align-items: center; gap: 8px; padding: 0 14px 8px; flex-wrap: wrap; min-width: 0; }
    .cs-tools .al-input { flex: 1 1 220px; }
    .cs-seg button .n { margin-left: 5px; font-weight: 400; opacity: .75; }
    .cs-seg button.lv-warn.on { background: var(--warn); color: #1a1408; }
    .cs-seg button.lv-error.on { background: var(--err); color: #fff; }
    .cs-seg button.lv-debug.on { background: var(--text-mute); color: #fff; }
    .cs-pick { text-transform: none; }
    .cs-pick .cur { white-space: nowrap; overflow: hidden; text-overflow: ellipsis; max-width: 180px; }
    .cs-pick .lab { color: var(--text-mute); font-size: 10px; text-transform: uppercase; letter-spacing: .05em; margin-right: 2px; }
    .cs-menu .sndmenu-it { text-transform: none; }
    .cs-status { padding: 0 14px 8px; font-size: 11px; color: var(--text-mute); display: flex; gap: 14px; flex-wrap: wrap; }
    .cs-list { flex: 1 1 auto; min-height: 120px; overflow: auto; margin: 0 12px 12px; background: var(--bg-elev); border: 1px solid var(--border); border-radius: 10px; font-family: var(--font-mono, monospace); font-size: 11px; line-height: 1.5; }
    .cs-row { display: flex; gap: 8px; padding: 3px 8px 3px 10px; border-bottom: 1px solid var(--border); align-items: baseline; cursor: pointer; }
    .cs-row:last-child { border-bottom: 0; }
    .cs-row:hover { background: rgba(var(--accent-rgb), .07); }
    .cs-row.copied { background: rgba(var(--accent-rgb), .2); }
    .cs-row.lv-error { background: rgba(255, 107, 107, .05); }
    .cs-row.lv-debug { opacity: .7; }
    .cs-row .t { flex: 0 0 82px; color: var(--text-mute); }
    .cs-row .l { flex: 0 0 40px; font-weight: 700; }
    .cs-row .l.info { color: var(--accent-hi); } .cs-row .l.warn { color: var(--warn); } .cs-row .l.error { color: var(--err); } .cs-row .l.debug { color: var(--text-mute); }
    .cs-row .s { flex: 0 0 120px; color: var(--text-dim); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
    .cs-row .s .tag { color: var(--brass, var(--accent-hi)); margin-left: 5px; }
    .cs-row .m { flex: 1; color: var(--text); white-space: pre-wrap; word-break: break-word; overflow-wrap: anywhere; min-width: 0; }
    .cs-list.nowrap .cs-row .m { white-space: pre; overflow: hidden; text-overflow: ellipsis; }
    .cs-row .c { flex: none; opacity: 0; appearance: none; font: inherit; font-size: 10px; padding: 0 6px; border-radius: 4px; border: 1px solid var(--border-hi); background: var(--bg-elev-2); color: var(--text-dim); cursor: pointer; }
    .cs-row:hover .c { opacity: 1; }
    .cs-empty { padding: 18px; color: var(--text-mute); text-align: center; }
  `;

  // ---- themed dropdown (same classes as the Alerts sound picker; own ids and listeners) ----
  function menuClose() { const m = document.getElementById('csMenu'); if (m) { m.remove(); try { wmRectsSoon(); } catch (e) {} } }
  function menuPlace(pop, anchor) {
    const r = uiScreenRect(anchor);                 // zoom-corrected: the menu lives at body level
    const W = window.innerWidth, H = window.innerHeight, SB = 20;
    const below = H - r.bottom - 8, above = r.top - 8;
    const openUp = above > below;
    const avail = Math.max(80, openUp ? above : below);
    if (pop.offsetHeight > avail) pop.style.maxHeight = avail + 'px';
    const pw = pop.offsetWidth, ph = Math.min(pop.offsetHeight, avail);
    let left = r.left;
    if (left + pw > W - SB) left = r.right - pw;
    left = Math.max(8, Math.min(left, W - pw - SB));
    let top = openUp ? (r.top - ph - 4) : (r.bottom + 4);
    top = Math.max(8, Math.min(top, H - ph - 8));
    pop.style.left = left + 'px'; pop.style.top = top + 'px';
  }
  function menuOpen(anchor, items, current, onPick) {
    menuClose();
    const pop = document.createElement('div'); pop.className = 'sndmenu cs-menu'; pop.id = 'csMenu';
    for (const it of items) {
      const el = document.createElement('div'); el.className = 'sndmenu-it' + (it.v === current ? ' sel' : '');
      const nm = document.createElement('span'); nm.textContent = it.label; el.appendChild(nm);
      if (it.count != null) { const n = document.createElement('span'); n.className = 'pv'; n.textContent = String(it.count); el.appendChild(n); }
      el.addEventListener('click', () => { onPick(it.v); menuClose(); });
      pop.appendChild(el);
    }
    document.body.appendChild(pop);
    menuPlace(pop, anchor);
    try { wmRectsSoon(); } catch (e) {}
    const sel = pop.querySelector('.sndmenu-it.sel'); if (sel) sel.scrollIntoView({ block: 'nearest' });
  }
  const insideMenu = e => !!(e && e.target && e.target.nodeType === 1 && e.target.closest && e.target.closest('#csMenu'));
  document.addEventListener('click', e => { if (!insideMenu(e)) menuClose(); });
  document.addEventListener('scroll', e => { if (!insideMenu(e)) menuClose(); }, true);
  document.addEventListener('keydown', e => { if (e.key === 'Escape') menuClose(); });
  function picker(label, getItems, getVal, setVal) {
    const b = document.createElement('button'); b.type = 'button'; b.className = 'al-sndsel cs-pick';
    const lab = document.createElement('span'); lab.className = 'lab'; lab.textContent = label;
    const cur = document.createElement('span'); cur.className = 'cur';
    const car = document.createElement('span'); car.className = 'cv'; car.textContent = '\u25BE';
    b.appendChild(lab); b.appendChild(cur); b.appendChild(car);
    b.refresh = () => { const items = getItems(); const v = getVal(); const it = items.find(x => x.v === v) || items[0]; cur.textContent = it ? it.label : ''; };
    b.addEventListener('click', e => {
      e.stopPropagation();
      if (document.getElementById('csMenu')) { menuClose(); return; }
      menuOpen(b, getItems(), getVal(), v => { setVal(v); b.refresh(); });
    });
    b.refresh();
    return b;
  }
  function seg(defs) {   // defs: [{ id, label, cls, count, title, get, set }]
    const wrap = document.createElement('div'); wrap.className = 'al-seg cs-seg';
    for (const d of defs) {
      const b = document.createElement('button'); b.type = 'button'; b.dataset.id = d.id; b.className = d.cls || '';
      const t = document.createElement('span'); t.textContent = d.label; b.appendChild(t);
      if (d.count) { const n = document.createElement('span'); n.className = 'n'; b.appendChild(n); }
      if (d.title) b.title = d.title;
      b.addEventListener('click', () => { d.set(!d.get()); refreshChrome(); });
      wrap.appendChild(b);
    }
    wrap.refresh = counts => {
      for (const d of defs) {
        const b = wrap.querySelector('[data-id="' + d.id + '"]'); if (!b) continue;
        b.classList.toggle('on', !!d.get());
        if (d.count) { const n = b.querySelector('.n'); if (n) n.textContent = String((counts && counts[d.id]) || 0); }
      }
    };
    return wrap;
  }

  function flatBtn(label, title, click) {
    const b = document.createElement('button'); b.type = 'button'; b.className = 'cs-btn'; b.textContent = label;
    if (title) b.title = title;
    b.addEventListener('click', click);
    return b;
  }

  async function copyText(text, el) {
    let ok = false;
    try { ok = (await rtxData.call('clipboard.copy', text)) !== null; } catch (e) { ok = false; }
    if (el) { el.classList.add('copied'); clearTimeout(flashT); flashT = setTimeout(() => el.classList.remove('copied'), 500); }
    try { uiNotify(ok ? 'Copied to the clipboard.' : 'Copy failed.', { ttl: 1500 }); } catch (e) {}
  }

  function visibleRecords() {
    const all = rtxConsole.all();
    const out = [];
    const cut = cfg.paused ? pausedAt : Infinity;
    for (let i = 0; i < all.length; i++) { const r = all[i]; if (r.seq > cut) break; if (matches(r)) out.push(r); }
    rowsTotal = out.length;
    return out.length > SHOW_MAX ? out.slice(out.length - SHOW_MAX) : out;
  }

  let ui = null;   // { modes, rx, levels, src, tag }
  function sourceItems() {
    const seen = new Map();
    for (const r of rtxConsole.all()) { const k = r.source === 'plugin' ? 'plugin:' + r.plugin : r.source; seen.set(k, (seen.get(k) || 0) + 1); }
    const items = [{ v: 'all', label: 'All sources', count: rtxConsole.all().length },
                   { v: 'client', label: 'Client', count: seen.get('client') || 0 },
                   { v: 'launcher', label: 'Launcher', count: seen.get('launcher') || 0 }];
    const plugins = Array.from(seen.keys()).filter(k => k.indexOf('plugin:') === 0).sort();
    if (plugins.length) items.push({ v: 'plugins', label: 'All plugins', count: plugins.reduce((n, k) => n + seen.get(k), 0) });
    for (const k of plugins) items.push({ v: k, label: k.slice(7), count: seen.get(k) });
    return items;
  }
  function tagItems() {
    const seen = new Map();
    for (const r of rtxConsole.all()) if (r.tag) seen.set(r.tag, (seen.get(r.tag) || 0) + 1);
    const items = [{ v: '', label: 'All tags' }];
    for (const t of Array.from(seen.keys()).sort()) items.push({ v: t, label: t, count: seen.get(t) });
    return items;
  }

  function buildChrome(wrap) {
    const head = document.createElement('div'); head.className = 'cs-head';
    const title = document.createElement('div'); title.className = 'cs-title'; title.textContent = 'Console';
    head.appendChild(title);
    const modes = seg([
      { id: 'pause',  label: 'Pause',  title: 'Keep collecting but stop showing new lines', get: () => cfg.paused, set: v => { cfg.paused = v; if (v) { const all = rtxConsole.all(); pausedAt = all.length ? all[all.length - 1].seq : 0; } dirty = true; } },
      { id: 'follow', label: 'Follow', title: 'Keep the newest line in view', get: () => cfg.autoscroll, set: v => { cfg.autoscroll = v; cfgSave(); if (v) scrollEnd(); } },
      { id: 'wrap',   label: 'Wrap',   title: 'Wrap long messages', get: () => cfg.wrap, set: v => { cfg.wrap = v; cfgSave(); const l = $('csList'); if (l) l.classList.toggle('nowrap', !v); } },
    ]);
    head.appendChild(modes);
    head.appendChild(flatBtn('Copy', 'Copy every line shown, as text', () => copyText(visibleRecords().map(rtxConsole.format).join('\n'), null)));
    head.appendChild(flatBtn('Copy JSON', 'Copy the lines shown as JSON records', () => copyText(JSON.stringify(visibleRecords(), null, 1), null)));
    head.appendChild(flatBtn('Clear', '', () => { rtxConsole.clear(); dirty = true; }));
    wrap.appendChild(head);

    const tools = document.createElement('div'); tools.className = 'cs-tools';
    const q = document.createElement('input'); q.id = 'csQ'; q.className = 'al-input'; q.placeholder = 'Search messages, tags and plugin ids'; q.spellcheck = false; q.value = cfg.q;
    q.addEventListener('input', () => { cfg.q = q.value.slice(0, 200); dirty = true; });
    tools.appendChild(q);
    const rx = seg([{ id: 'regex', label: 'Regex', title: 'Treat the search as a regular expression', get: () => cfg.regex, set: v => { cfg.regex = v; cfgSave(); dirty = true; } }]);
    tools.appendChild(rx);
    const levels = seg(LV.map(l => ({ id: l, label: l, cls: 'lv-' + l, count: true, get: () => cfg.levels[l], set: v => { cfg.levels[l] = v; cfgSave(); dirty = true; } })));
    tools.appendChild(levels);
    const src = picker('Source', sourceItems, () => cfg.source, v => { cfg.source = v; cfgSave(); dirty = true; });
    tools.appendChild(src);
    const tag = picker('Tag', tagItems, () => cfg.tag, v => { cfg.tag = v; cfgSave(); dirty = true; });
    tools.appendChild(tag);
    wrap.appendChild(tools);

    const status = document.createElement('div'); status.className = 'cs-status'; status.id = 'csStatus';
    wrap.appendChild(status);
    ui = { modes, rx, levels, src, tag };
  }

  function refreshChrome() {
    if (!ui) return;
    const counts = { debug: 0, info: 0, warn: 0, error: 0 };
    for (const r of rtxConsole.all()) counts[r.level]++;
    ui.modes.refresh(); ui.rx.refresh(); ui.levels.refresh(counts);
    if (!sourceItems().some(i => i.v === cfg.source)) cfg.source = 'all';
    if (!tagItems().some(i => i.v === cfg.tag)) cfg.tag = '';
    ui.src.refresh(); ui.tag.refresh();
  }

  function scrollEnd() { const l = $('csList'); if (l) l.scrollTop = l.scrollHeight; }

  function fmtTime(t) {
    const d = new Date(t); const p = n => String(n).padStart(2, '0');
    return p(d.getHours()) + ':' + p(d.getMinutes()) + ':' + p(d.getSeconds()) + '.' + String(d.getMilliseconds()).padStart(3, '0');
  }

  function row(rec) {
    const el = document.createElement('div'); el.className = 'cs-row lv-' + rec.level;
    const t = document.createElement('span'); t.className = 't'; t.textContent = fmtTime(rec.t);
    const l = document.createElement('span'); l.className = 'l ' + rec.level; l.textContent = rec.level.toUpperCase();
    const s = document.createElement('span'); s.className = 's';
    s.textContent = rec.source === 'plugin' ? rec.plugin : rec.source;
    s.title = rec.source === 'plugin' ? ('plugin ' + rec.plugin + (rec.runtime ? ', ' + rec.runtime + ' runtime' : '')) : rec.source;
    if (rec.tag) { const tg = document.createElement('span'); tg.className = 'tag'; tg.textContent = rec.tag; s.appendChild(tg); }
    const m = document.createElement('span'); m.className = 'm'; m.textContent = rec.text;
    const c = document.createElement('button'); c.className = 'c'; c.textContent = 'copy'; c.title = 'Copy this line';
    el.appendChild(t); el.appendChild(l); el.appendChild(s); el.appendChild(m); el.appendChild(c);
    el.addEventListener('click', ev => {
      const sel = window.getSelection && window.getSelection();
      if (sel && String(sel).length && ev.target !== c) return;      // let a drag selection stand
      copyText(rtxConsole.format(rec), el);
    });
    return el;
  }

  function rebuildList() {
    const list = $('csList'); if (!list) return;
    const recs = visibleRecords();
    rowsShown = recs.length;
    const frag = document.createDocumentFragment();
    if (!recs.length) { const e = document.createElement('div'); e.className = 'cs-empty'; e.textContent = rtxConsole.all().length ? 'No lines match the current filters.' : 'Nothing logged yet.'; frag.appendChild(e); }
    else for (const r of recs) frag.appendChild(row(r));
    list.innerHTML = ''; list.appendChild(frag);
    if (cfg.autoscroll && !cfg.paused) scrollEnd();
    const st = $('csStatus');
    if (st) {
      const total = rtxConsole.all().length, dropped = rtxConsole.dropped();
      st.textContent = '';
      const a = document.createElement('span'); a.textContent = rowsShown + (rowsTotal > rowsShown ? ' of ' + rowsTotal + ' matching (newest)' : ' shown') + ', ' + total + ' kept' + (dropped ? ', ' + dropped + ' oldest dropped' : '');
      const b = document.createElement('span'); b.textContent = cfg.paused ? 'Paused: new lines are kept but not shown.' : 'Click a line to copy it.';
      st.appendChild(a); st.appendChild(b);
    }
  }

  function tick() {
    if (!paneVisible('console')) return;
    if (dirty) { dirty = false; refreshChrome(); rebuildList(); }
  }

  function renderConsole() {
    const c = paneRoot('console'); if (!c) return;
    cfgLoad();
    let wrap = $('csWrapRoot');
    if (!wrap) {
      injectStyle('csCss', CSS);
      c.innerHTML = '';
      wrap = document.createElement('div'); wrap.id = 'csWrapRoot'; wrap.className = 'cs-wrap';
      buildChrome(wrap);
      const list = document.createElement('div'); list.id = 'csList'; list.className = 'cs-list' + (cfg.wrap ? '' : ' nowrap');
      list.addEventListener('wheel', () => {
        if (!cfg.autoscroll) return;
        setTimeout(() => {
          const atEnd = list.scrollHeight - list.scrollTop - list.clientHeight < 8;
          if (!atEnd) { cfg.autoscroll = false; cfgSave(); refreshChrome(); }
        }, 50);
      }, { passive: true });
      wrap.appendChild(list);
      c.appendChild(wrap);
      dirty = true;
      if (!timer) timer = setInterval(tick, 250);
      if (!logTimer) logTimer = setInterval(pollLauncherLog, 1000);
      pollLauncherLog();
    }
    tick();
  }

  registerTab({ id: 'console', render: renderConsole });
})();
