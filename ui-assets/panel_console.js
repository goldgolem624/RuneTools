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

  function levelOf(line) {
    const l = line.toLowerCase();
    if (/\b(error|crash|threw|exception|fatal|failed)\b/.test(l)) return 'error';
    if (/\b(warn|warning|retry|missing|refused|not set)\b/.test(l)) return 'warn';
    return 'info';
  }
  // launcher.log tail: first read from the start (bounded to the last 256 KB), then only new bytes
  async function pollLauncherLog() {
    if (logBusy || !paneVisible('console')) return;
    logBusy = true;
    try {
      const r = await rtxData.call('host.logTail', logOff);
      if (r && typeof r.text === 'string') {
        if (r.text) {
          const lines = r.text.split(/\r?\n/);
          for (const ln of lines) {
            if (!ln) continue;
            const m = /^(\d{4}-\d\d-\d\d[ T][\d:.]+)\s*(.*)$/.exec(ln);
            const text = m ? m[2] : ln;
            rtxConsole.push({ level: levelOf(text), source: 'launcher', text });
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

  function css() {
    if (document.getElementById('csCss')) return;
    const st = document.createElement('style'); st.id = 'csCss';
    st.textContent =
      '.cs-wrap{display:flex;flex-direction:column;height:100%;min-height:0;gap:8px;}' +
      '.cs-bar{display:flex;flex-wrap:wrap;gap:6px;align-items:center;}' +
      '.cs-bar .bank-search{flex:1 1 220px;min-width:160px;}' +
      '.cs-chip{border:1px solid var(--border,rgba(255,255,255,.1));background:rgba(255,255,255,.04);color:var(--text-dim,#8b8b9e);border-radius:999px;padding:3px 10px;font-size:.82em;font-weight:600;cursor:pointer;user-select:none;font:inherit;}' +
      '.cs-chip.on{color:var(--text,#e8e8ef);background:rgba(var(--accent-rgb,124,92,252),.16);border-color:rgba(var(--accent-rgb,124,92,252),.5);}' +
      '.cs-chip.lv-debug.on{color:#9aa0b4;}.cs-chip.lv-info.on{color:#7cc4ff;}.cs-chip.lv-warn.on{color:#e0b000;}.cs-chip.lv-error.on{color:#ff6363;}' +
      '.cs-chip .n{opacity:.7;font-weight:400;margin-left:4px;}' +
      '.cs-sel{background:var(--bg-elev,#101117);color:var(--text,#e8e8ef);border:1px solid var(--border,rgba(255,255,255,.1));border-radius:8px;padding:4px 8px;font:inherit;font-size:.85em;max-width:220px;}' +
      '.cs-btn{border:1px solid var(--border,rgba(255,255,255,.1));background:rgba(255,255,255,.06);color:var(--text,#e8e8ef);border-radius:8px;padding:5px 11px;font-size:.85em;font-weight:600;cursor:pointer;font:inherit;}' +
      '.cs-btn.warn{background:rgba(224,176,0,.18);border-color:rgba(224,176,0,.5);color:#e0b000;}' +
      '.cs-status{color:var(--text-dim,#8b8b9e);font-size:.8em;display:flex;gap:14px;flex-wrap:wrap;}' +
      '.cs-list{flex:1 1 auto;min-height:120px;overflow:auto;background:var(--bg-elev,#101117);border:1px solid var(--border,rgba(255,255,255,.08));border-radius:10px;font-family:Consolas,"Cascadia Mono",monospace;font-size:.86em;line-height:1.45;padding:4px 0;}' +
      '.cs-row{display:grid;grid-template-columns:86px 46px minmax(70px,auto) 1fr auto;gap:0 8px;align-items:start;padding:1px 10px;cursor:pointer;border-left:3px solid transparent;}' +
      '.cs-row:hover{background:rgba(255,255,255,.05);}' +
      '.cs-row.copied{background:rgba(var(--accent-rgb,124,92,252),.22);}' +
      '.cs-row.lv-warn{border-left-color:#e0b000;}.cs-row.lv-error{border-left-color:#ff6363;background:rgba(255,99,99,.06);}.cs-row.lv-debug{opacity:.72;}' +
      '.cs-ts{color:var(--text-mute,#5a5a6e);}' +
      '.cs-lv{font-weight:700;font-size:.85em;}.cs-lv.debug{color:#9aa0b4;}.cs-lv.info{color:#7cc4ff;}.cs-lv.warn{color:#e0b000;}.cs-lv.error{color:#ff6363;}' +
      '.cs-src{color:var(--text-dim,#8b8b9e);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:220px;}' +
      '.cs-src .tag{color:var(--accent-hi,#7c5cfc);margin-left:4px;}' +
      '.cs-msg{white-space:pre-wrap;word-break:break-word;}' +
      '.cs-list.nowrap .cs-msg{white-space:pre;overflow:hidden;text-overflow:ellipsis;}' +
      '.cs-copy{opacity:0;border:0;background:rgba(255,255,255,.08);color:var(--text-dim,#8b8b9e);border-radius:6px;padding:0 6px;font-size:.8em;cursor:pointer;font:inherit;}' +
      '.cs-row:hover .cs-copy{opacity:1;}' +
      '.cs-empty{padding:18px;color:var(--text-dim,#8b8b9e);text-align:center;}';
    document.head.appendChild(st);
  }

  function chip(label, on, cls, onClick, count) {
    const b = document.createElement('button'); b.className = 'cs-chip' + (cls ? ' ' + cls : '') + (on ? ' on' : '');
    b.textContent = label;
    if (count != null) { const n = document.createElement('span'); n.className = 'n'; n.textContent = String(count); b.appendChild(n); }
    b.addEventListener('click', onClick);
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

  function buildToolbar(wrap) {
    const bar = document.createElement('div'); bar.className = 'cs-bar'; bar.id = 'csBar';
    const q = document.createElement('input'); q.className = 'bank-search'; q.id = 'csQ'; q.placeholder = 'Search messages, tags and plugin ids'; q.spellcheck = false; q.value = cfg.q;
    q.addEventListener('input', () => { cfg.q = q.value.slice(0, 200); dirty = true; });
    bar.appendChild(q);
    bar.appendChild(chip('Regex', cfg.regex, '', () => { cfg.regex = !cfg.regex; cfgSave(); dirty = true; rebuildBar(); }));
    const lvWrap = document.createElement('span'); lvWrap.id = 'csLv'; lvWrap.style.cssText = 'display:inline-flex;gap:4px;';
    bar.appendChild(lvWrap);
    const src = document.createElement('select'); src.className = 'cs-sel'; src.id = 'csSrc';
    src.addEventListener('change', () => { cfg.source = src.value; cfgSave(); dirty = true; });
    bar.appendChild(src);
    const tag = document.createElement('select'); tag.className = 'cs-sel'; tag.id = 'csTag';
    tag.addEventListener('change', () => { cfg.tag = tag.value; cfgSave(); dirty = true; });
    bar.appendChild(tag);
    const bPause = document.createElement('button'); bPause.className = 'cs-btn'; bPause.id = 'csPause';
    bPause.addEventListener('click', () => {
      cfg.paused = !cfg.paused;
      if (cfg.paused) { const all = rtxConsole.all(); pausedAt = all.length ? all[all.length - 1].seq : 0; }
      dirty = true; rebuildBar();
    });
    bar.appendChild(bPause);
    bar.appendChild(chip('Follow', cfg.autoscroll, '', () => { cfg.autoscroll = !cfg.autoscroll; cfgSave(); rebuildBar(); if (cfg.autoscroll) scrollEnd(); }));
    bar.appendChild(chip('Wrap', cfg.wrap, '', () => { cfg.wrap = !cfg.wrap; cfgSave(); const l = $('csList'); if (l) l.classList.toggle('nowrap', !cfg.wrap); rebuildBar(); }));
    const bCopy = document.createElement('button'); bCopy.className = 'cs-btn'; bCopy.textContent = 'Copy shown';
    bCopy.title = 'Copy every line currently shown, as text';
    bCopy.addEventListener('click', () => copyText(visibleRecords().map(rtxConsole.format).join('\n'), null));
    bar.appendChild(bCopy);
    const bJson = document.createElement('button'); bJson.className = 'cs-btn'; bJson.textContent = 'Export JSON';
    bJson.title = 'Copy the shown lines as JSON records';
    bJson.addEventListener('click', () => copyText(JSON.stringify(visibleRecords(), null, 1), null));
    bar.appendChild(bJson);
    const bClear = document.createElement('button'); bClear.className = 'cs-btn warn'; bClear.textContent = 'Clear';
    bClear.addEventListener('click', () => { rtxConsole.clear(); dirty = true; });
    bar.appendChild(bClear);
    wrap.appendChild(bar);
    const status = document.createElement('div'); status.className = 'cs-status'; status.id = 'csStatus';
    wrap.appendChild(status);
  }

  function rebuildBar() {
    const lvWrap = $('csLv'); if (!lvWrap) return;
    const counts = { debug: 0, info: 0, warn: 0, error: 0 };
    const sources = new Map(), tags = new Set();
    for (const r of rtxConsole.all()) {
      counts[r.level]++;
      if (r.source === 'plugin') sources.set('plugin:' + r.plugin, 'plugin ' + r.plugin); else sources.set(r.source, r.source);
      if (r.tag) tags.add(r.tag);
    }
    lvWrap.innerHTML = '';
    for (const lv of LV) lvWrap.appendChild(chip(lv, cfg.levels[lv], 'lv-' + lv, () => { cfg.levels[lv] = !cfg.levels[lv]; cfgSave(); dirty = true; rebuildBar(); }, counts[lv]));
    const src = $('csSrc');
    if (src) {
      const want = ['all', 'client', 'launcher', 'plugins'].concat(Array.from(sources.keys()).filter(k => k.indexOf('plugin:') === 0).sort());
      const sig = want.join('|');
      if (src._sig !== sig) {
        src._sig = sig; src.innerHTML = '';
        for (const k of want) {
          const o = document.createElement('option'); o.value = k;
          o.textContent = k === 'all' ? 'All sources' : k === 'plugins' ? 'All plugins' : k === 'client' ? 'Client' : k === 'launcher' ? 'Launcher' : sources.get(k) || k;
          src.appendChild(o);
        }
      }
      src.value = want.indexOf(cfg.source) >= 0 ? cfg.source : 'all';
    }
    const tag = $('csTag');
    if (tag) {
      const list = Array.from(tags).sort();
      const sig = list.join('|');
      if (tag._sig !== sig) {
        tag._sig = sig; tag.innerHTML = '';
        const o0 = document.createElement('option'); o0.value = ''; o0.textContent = 'All tags'; tag.appendChild(o0);
        for (const t of list) { const o = document.createElement('option'); o.value = t; o.textContent = t; tag.appendChild(o); }
      }
      tag.value = (cfg.tag && list.indexOf(cfg.tag) >= 0) ? cfg.tag : '';
    }
    const bPause = $('csPause'); if (bPause) { bPause.textContent = cfg.paused ? 'Resume' : 'Pause'; bPause.classList.toggle('warn', cfg.paused); }
    const bar = $('csBar');
    if (bar) for (const b of bar.querySelectorAll('.cs-chip')) {
      if (b.textContent === 'Regex') b.classList.toggle('on', cfg.regex);
      if (b.textContent === 'Follow') b.classList.toggle('on', cfg.autoscroll);
      if (b.textContent === 'Wrap') b.classList.toggle('on', cfg.wrap);
    }
  }

  function scrollEnd() { const l = $('csList'); if (l) l.scrollTop = l.scrollHeight; }

  function row(rec) {
    const el = document.createElement('div'); el.className = 'cs-row lv-' + rec.level; el.dataset.seq = rec.seq;
    const d = new Date(rec.t); const p = n => String(n).padStart(2, '0');
    const ts = document.createElement('span'); ts.className = 'cs-ts';
    ts.textContent = p(d.getHours()) + ':' + p(d.getMinutes()) + ':' + p(d.getSeconds()) + '.' + String(d.getMilliseconds()).padStart(3, '0');
    const lv = document.createElement('span'); lv.className = 'cs-lv ' + rec.level; lv.textContent = rec.level.toUpperCase();
    const src = document.createElement('span'); src.className = 'cs-src';
    src.textContent = rec.source === 'plugin' ? rec.plugin + (rec.runtime === 'lua' ? ' (lua)' : '') : rec.source;
    src.title = rec.source === 'plugin' ? ('plugin ' + rec.plugin + (rec.runtime ? ', ' + rec.runtime + ' runtime' : '')) : rec.source;
    if (rec.tag) { const t = document.createElement('span'); t.className = 'tag'; t.textContent = '[' + rec.tag + ']'; src.appendChild(t); }
    const msg = document.createElement('span'); msg.className = 'cs-msg'; msg.textContent = rec.text;
    const cp = document.createElement('button'); cp.className = 'cs-copy'; cp.textContent = 'copy'; cp.title = 'Copy this line';
    el.appendChild(ts); el.appendChild(lv); el.appendChild(src); el.appendChild(msg); el.appendChild(cp);
    el.addEventListener('click', ev => {
      const sel = window.getSelection && window.getSelection();
      if (sel && String(sel).length && ev.target !== cp) return;      // let a drag selection stand
      copyText(rtxConsole.format(rec), el);
    });
    return el;
  }

  function rebuildList() {
    const list = $('csList'); if (!list) return;
    const atEnd = list.scrollHeight - list.scrollTop - list.clientHeight < 8;
    const recs = visibleRecords();
    rowsShown = recs.length;
    const frag = document.createDocumentFragment();
    if (!recs.length) { const e = document.createElement('div'); e.className = 'cs-empty'; e.textContent = rtxConsole.all().length ? 'No lines match the current filters.' : 'Nothing logged yet.'; frag.appendChild(e); }
    else for (const r of recs) frag.appendChild(row(r));
    list.innerHTML = ''; list.appendChild(frag);
    if (cfg.autoscroll && !cfg.paused) scrollEnd(); else if (atEnd && cfg.autoscroll) scrollEnd();
    const st = $('csStatus');
    if (st) {
      const total = rtxConsole.all().length, dropped = rtxConsole.dropped();
      st.textContent = '';
      const a = document.createElement('span'); a.textContent = 'Showing ' + rowsShown + (rowsTotal > rowsShown ? ' of ' + rowsTotal + ' matching (newest)' : '') + ', ' + total + ' kept' + (dropped ? ', ' + dropped + ' oldest dropped' : '');
      const b = document.createElement('span'); b.textContent = cfg.paused ? 'Paused: new lines are kept but not shown.' : 'Click a line to copy it. Plugins log with rtx.plugin.console (HTML) or rtx.console (Lua).';
      st.appendChild(a); st.appendChild(b);
    }
  }

  function tick() {
    if (!paneVisible('console')) return;
    if (dirty) { dirty = false; rebuildBar(); rebuildList(); }
  }

  function renderConsole() {
    const c = paneRoot('console'); if (!c) return;
    cfgLoad(); css();
    let wrap = $('csWrap');
    if (!wrap) {
      c.innerHTML = '';
      wrap = document.createElement('div'); wrap.id = 'csWrap'; wrap.className = 'cs-wrap';
      buildToolbar(wrap);
      const list = document.createElement('div'); list.id = 'csList'; list.className = 'cs-list' + (cfg.wrap ? '' : ' nowrap');
      list.addEventListener('scroll', () => {
        if (!cfg.autoscroll) return;
        const atEnd = list.scrollHeight - list.scrollTop - list.clientHeight < 8;
        if (!atEnd && list._userScroll) { cfg.autoscroll = false; cfgSave(); rebuildBar(); }
      });
      list.addEventListener('wheel', () => { list._userScroll = true; setTimeout(() => { list._userScroll = false; }, 300); }, { passive: true });
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
