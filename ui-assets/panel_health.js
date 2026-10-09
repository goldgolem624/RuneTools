// RuneToolsX panel: Health check (Developer): what a game update moved, changed or broke, by feature,
// then the rows themselves: client code, calibration, live layout, packets, cache content and the
// companion, grouped in dependency order. Runs by itself on a new exe or a cache update (the host
// starts it); Run check starts one by hand.
(function () {

  let hcData = null, hcBusy = false, hcSeq = -1, hcPollTimer = 0;
  let hcDiff = null, hcHist = [], hcCmp = null, hcCmpA = '', hcCmpB = '';
  const hcOpen = {};          // group -> expanded (user toggles survive re-renders)
  const hcFeatOpen = {};      // impact list entry -> its rows shown
  let icMisses = null, icCov = null, icNames = {}, icTimer = 0;
  let hcKeepScroll = [];   // .hc-list scrollTop values captured before a rebuild
  let hcSig = '';          // last rendered data signature
  async function icFetch() {
    try { icMisses = JSON.parse(await rtxData.raw('host.iconMisses')); } catch (e) { icMisses = null; }
    try { icCov = JSON.parse(await rtxData.raw('host.iconCoverage')); } catch (e) { icCov = null; }
    if (icCov && icCov.pending) { clearTimeout(icTimer); icTimer = setTimeout(icFetch, 1500); }
    {
      const want = (icCov && Array.isArray(icCov.missing) ? icCov.missing.slice(0, 40) : [])
        .concat(icMisses && Array.isArray(icMisses.misses) ? icMisses.misses.slice(0, 40).map(x => x[0]) : []);
      for (const id of want) {
        if (id in icNames) continue;
        try { const d = JSON.parse(await rtxData.raw('cache.itemInfo', id) || 'null'); icNames[id] = (d && d.name) || ''; } catch (e) { icNames[id] = ''; }
      }
    }
    paneRun('health', renderHealth);
  }
  function hcParse(t) { try { return JSON.parse(t); } catch (e) { return null; } }
  async function hcHistory() {
    hcHist = hcParse(await rtxData.raw('host.healthHistory')) || [];
    if (Array.isArray(hcHist) && hcHist.length) {
      hcDiff = hcParse(await rtxData.raw('host.healthDiff', '', hcHist[0].name));
      if (!hcCmpB) hcCmpB = hcHist[0].name;
      if (!hcCmpA && hcHist.length > 1) hcCmpA = hcHist[1].name;
    }
  }
  async function hcPoll() {
    clearTimeout(hcPollTimer);
    const p = hcParse(await rtxData.raw('host.readerHealthPoll'));
    if (p && !p.running && p.seq !== hcSeq) {
      hcSeq = p.seq; hcData = p.run; hcBusy = false;
      try { await hcHistory(); } catch (e) {}
      icFetch();
    } else if (p && p.running) {
      hcPollTimer = setTimeout(hcPoll, 400);
    } else { hcBusy = false; }
    paneRun('health', renderHealth);
  }
  async function hcRun() {
    if (hcBusy || !bridge() || !bridge().readerHealthStart) return;
    hcBusy = true; paneRun('health', renderHealth);
    let pins = '{}';
    try { pins = JSON.stringify(window.RTX_PINS || {}); } catch (e) {}
    try { hcSeq = Number(await rtxData.raw('host.readerHealthStart', pins)); } catch (e) { hcBusy = false; }
    hcPollTimer = setTimeout(hcPoll, 400);
  }
  // A run this panel did not start (the automatic one, or another window's) is picked up from the
  // shared latest-run state; the host answers both with the same seq.
  function hcRefresh() {
    const h = window.__rtxHealth;
    if (!h || !h.run || hcBusy || h.seq === hcSeq) return;
    hcSeq = h.seq; hcData = h.run;
    hcHistory().catch(() => {}).then(() => paneRun('health', renderHealth));
  }
  async function hcMarkReviewed() {
    const b = hcData && hcData.build;
    if (!b) return;
    try { await rtxData.raw('host.healthMarkReviewed', hcData.version || '', b.stamp || ''); } catch (e) {}
    hcRun();
  }
  function hcCopy() {
    if (!hcData || !bridge() || !bridge().copyClipboard) return;
    try { bridge().copyClipboard(JSON.stringify(hcData, null, 1)); if (typeof uiNotify === 'function') uiNotify('Report copied'); } catch (e) {}
  }
  // The readable report: the host's text when it has one, else the impact summary built here.
  function hcSummaryLocal() {
    if (!hcData) return '';
    const im = hcImpact(), b = hcData.build || {}, s = hcData.summary || {};
    const o = ['RuneTools health check  ' + (hcData.version || '') + '  ' + (b.flavour || '') + ' ' + (b.stamp || ''),
               hcChangeLine(im), hcVerdictLine(im, s), ''];
    for (const [name, list] of [['Broken', im.broken], ['Working with a warning', im.moved], ['Format changed', im.format], ['Not verified', im.unverified]]) {
      if (!list || !list.length) continue;
      o.push('== ' + name + ' ==');
      for (const e of list) o.push('  ' + e.feature + ': ' + (e.why || '') + (e.need ? ' (needs: ' + e.need + ')' : '') + '  [' + (e.rows || []).join(', ') + ']');
    }
    return o.join('\n') + '\n';
  }
  async function hcCopySummary() {
    if (!hcData || !bridge() || !bridge().copyClipboard) return;
    let text = '';
    try { if (bridge().healthSummary) text = await bridge().healthSummary(hcHist.length && hcHist[0].name ? hcHist[0].name : ''); } catch (e) {}
    if (!text || /^no health run/.test(text)) text = hcSummaryLocal();
    else text = hcSummaryLocal() + '\n' + text;
    try { bridge().copyClipboard(text); if (typeof uiNotify === 'function') uiNotify('Summary copied'); } catch (e) {}
  }
  async function hcCompare() {
    if (!hcCmpA || !hcCmpB) return;
    hcCmp = hcParse(await rtxData.raw('host.healthDiff', hcCmpA, hcCmpB));
    paneRun('health', renderHealth);
  }

  const DOT = s => s === 1 ? 'ok' : s === 0 ? 'bad' : s === 2 ? 'warn' : 'na';
  const WORD = s => s === 1 ? 'pass' : s === 0 ? 'fail' : s === 2 ? 'warn' : 'not checked';
  const KIND_WORD = k => (typeof rtxHealthWord === 'function') ? rtxHealthWord(k) : '';
  function el(tag, cls, text) { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; }
  function hcImpact() { return (typeof rtxHealthImpact === 'function' && hcData) ? (rtxHealthImpact(hcData) || {}) : {}; }
  function hcLevel(im) { return (typeof rtxHealthLevel === 'function') ? rtxHealthLevel(im) : 'none'; }
  const names = (list, max) => (typeof rtxHealthNames === 'function') ? rtxHealthNames(list, max) : (list || []).map(e => e.feature).join(', ');

  // The tab a feature name opens: a tab or tab group label, else a fixed home for the cross-cutting names.
  const FEATURE_TAB = {
    'overlay': 'overlay', 'companion': 'system', 'plugins': 'pluginbrowse', 'item tooltips': 'inventory', 'npc tooltips': 'scene',
    'loc labels': 'scene', 'game text': 'buffs', 'packet decoders': 'netprobe', 'events channel': 'events', 'containers': 'containers',
    'engine markers': 'markers', 'native outlines': 'overlay', 'menu swaps': 'menuswap', 'panel positions': 'uisettings',
    'interface pins': 'interfaces', 'health check': 'health', 'specials': 'overlay', 'skill guides': 'player', 'thieving levels': 'scene',
    'tree timers': 'overlay', 'blocked tiles': 'overlay', 'terrain heights': 'overlay', 'loot': 'inventory', 'var domain stores': 'vars',
    'zone events': 'events', 'everything': 'health', 'mainData layout': 'health', 'tooltips': 'inventory', 'chat capture': 'chatlog',
  };
  function featureTab(name) {
    const p = String(name || '').split(': ')[0].trim().toLowerCase();
    const tabs = (typeof allTabs === 'function') ? allTabs() : (typeof TABS !== 'undefined' ? TABS : []);
    let t = tabs.find(x => String(x.label).toLowerCase() === p);
    if (!t && typeof TAB_GROUPS !== 'undefined') { const g = TAB_GROUPS.find(x => String(x.label).toLowerCase() === p); if (g) t = tabs.find(x => x.id === g.tabs[0]); }
    if (!t && FEATURE_TAB[p]) t = tabs.find(x => x.id === FEATURE_TAB[p]);
    return (t && t.id !== 'health') ? t : null;
  }

  // The first line of the summary: what changed in the game since the last clean run.
  function hcChangeLine(im) {
    const b = (im && im.build) || {};
    if (im && im.derived) {
      const row = (hcData.checks || []).find(r => r.id === 'build.game');
      return row ? String(row.d || '') : '';
    }
    const arch = (b.archives || []).length ? ', ' + b.archives.length + ' cache archive' + (b.archives.length === 1 ? '' : 's') + ' changed (' + b.archives.slice(0, 6).join(', ') + (b.archives.length > 6 ? ', ...' : '') + ')' : '';
    if (b.exe === 'new' || b.exe === 'unvalidated') return 'Game updated' + (b.firstSeen ? ' ' + b.firstSeen : '') + ': ' + (b.exe === 'new' ? 'new client ' : 'client ') + (b.to || '') + (b.from ? ' (was ' + b.from + ')' : '') + (b.exe === 'unvalidated' ? ', not yet validated by this RuneTools build' : '') + arch;
    if (b.cache === 'changed') return 'Cache updated' + (b.firstSeen ? ' ' + b.firstSeen : '') + arch;
    return 'No game change since the last clean run' + (b.to ? ' (' + b.to + ')' : '');
  }
  function hcVerdictLine(im, s) {
    const n = k => ((im && im[k]) || []).length;
    const unv = ((im && im.unverified) || []).filter(e => !e.precondition).length;
    const parts = [];
    if (n('broken')) parts.push(n('broken') + (n('broken') === 1 ? ' feature broken' : ' features broken'));
    if (n('moved')) parts.push(n('moved') + ' working with a warning');
    if (n('format')) parts.push(n('format') + ' format change' + (n('format') === 1 ? '' : 's'));
    if (unv) parts.push(unv + ' not verifiable here');
    const ok = (im && typeof im.ok === 'number') ? im.ok : (s.pass | 0);
    parts.push(ok + ' checks pass');
    return parts.join(', ') + '.';
  }
  function hcAdviceLine(im, level, s) {
    const total = (s.pass | 0) + (s.fail | 0) + (s.warn | 0) + (s.unchecked | 0), done = total - (s.unchecked | 0);
    const pre = (im && im.complete === false) ? 'Log in so RuneTools can finish checking (' + done + ' of ' + total + ' done). ' : '';
    if (level === 'red') return pre + 'Those panels show stale or empty data until a RuneTools update; everything else works.';
    if (level === 'amber') return pre + 'Everything works. Moved values are in use; rows not verified need the game state named below.';
    return pre + 'Everything RuneTools reads still works.';
  }

  // Fact keys of the run diff, in words. The rev.* epochs become dates; moved addresses name what moved.
  const REV_NAMES = { '2/5': 'inventories', '2/11': 'params', '2/34': 'map scenes', '2/35': 'quests', '2/36': 'map elements', '2/40': 'dbtables',
                      '2/41': 'dbrows', '2/46': 'hitmarks', '2/60': 'player vars', '2/61': 'npc vars', '2/62': 'client vars', '2/63': 'world vars',
                      '2/64': 'region vars', '2/65': 'object vars', '2/66': 'clan vars', '2/67': 'clan setting vars', '2/68': 'campaign vars',
                      '2/69': 'varbits', '2/75': 'group vars', '3': 'interfaces', '5': 'maps', '8': 'sprites', '12': 'scripts', '14': 'sound effects',
                      '16': 'locs', '17': 'enums', '18': 'npcs', '19': 'items', '22': 'structs', '23': 'world map', '40': 'music', '57': 'achievements' };
  const dateOf = v => { const n = Number(v); if (!(n > 1e9)) return String(v); const d = new Date(n * 1000); return d.toISOString().slice(0, 10); };
  function factText(f) {
    const k = String(f.k || ''), was = String(f.was), now = String(f.now);
    let m;
    if ((m = /^rev\.(.+)$/.exec(k))) return (REV_NAMES[m[1]] || 'archive ' + m[1]) + ' repacked ' + dateOf(now) + ' (was ' + dateOf(was) + ')';
    if ((m = /^sig\.(.+)\.rva$/.exec(k))) return m[1] + ' moved ' + was + ' -> ' + now;
    if ((m = /^anchor\.(.+)\.rva$/.exec(k))) return m[1] + ' anchor moved ' + was + ' -> ' + now;
    if ((m = /^calib\.(.+)$/.exec(k))) return m[1] + ' moved ' + was + ' -> ' + now;
    if ((m = /^hook\.(.+)$/.exec(k))) return 'companion hook ' + m[1] + ': ' + was + ' -> ' + now;
    if ((m = /^pin\.(\w+)\.(.+)\.official$/.exec(k))) return m[1] + ' ' + m[2] + ' renamed: ' + now;
    if ((m = /^cache\.archives\.(\d+)$/.exec(k))) return 'index ' + m[1] + ' archives ' + was + ' -> ' + now;
    if ((m = /^ophist\.(\w+)\.(\w+)$/.exec(k))) return m[1] + ' opcode ' + m[2] + ' count ' + was + ' -> ' + now;
    if ((m = /^op\.(\w+)\.(\w+)$/.exec(k))) return 'packet ' + m[1] + ' ' + m[2] + ': ' + was + ' -> ' + now;
    return k + ': ' + was + ' -> ' + now;
  }
  // a fact already told by a listed row is left out of the fact list
  function factRowId(k) {
    let m;
    if ((m = /^sig\.(.+)\.rva$/.exec(k))) return 'code.sig.' + m[1];
    if ((m = /^calib\.(.+)$/.exec(k))) return 'calib.' + m[1];
    if ((m = /^hook\.(.+)$/.exec(k))) return 'comp.hook.' + m[1];
    if ((m = /^op\.(\w+)\.(\w+)$/.exec(k))) return 'pkt.' + m[1];
    return '';
  }
  const SINCE = { 'last clean': 'the last clean run', 'previous build': 'the previous build', 'previous run': 'the previous run',
                  'earlier run': 'the previous run (more failures)' };
  function runLabel(name) {
    const m = /^(\d{4})(\d\d)(\d\d)-(\d\d)(\d\d)\d\d-(\w+)/.exec(String(name || ''));
    return m ? m[1] + '-' + m[2] + '-' + m[3] + ' ' + m[4] + ':' + m[5] + ' on ' + m[6] : String(name || '').replace('.json', '');
  }

  function renderDiffList(card, diff, rowCap, factCap) {
    const list = el('div', 'hc-list');
    const rows = diff.rows || [], facts = diff.facts || [];
    const listed = {};
    for (const r0 of rows.slice(0, rowCap)) {
      listed[r0.id] = 1;
      const r = el('div', 'hc-row');
      r.appendChild(el('span', 'hc-dot ' + DOT(r0.now)));
      r.appendChild(el('span', 'hc-k', r0.k || r0.id));
      const feats = (hcData && (hcData.checks || []).find(x => x.id === r0.id) || {}).f;
      if (Array.isArray(feats) && feats.length) r.appendChild(el('span', 'hc-feat', feats.slice(0, 3).join(', ')));
      r.appendChild(el('span', 'hc-d', (r0.was < 0 ? 'new row' : WORD(r0.was)) + ' to ' + (r0.now < 0 ? 'row gone' : WORD(r0.now))));
      list.appendChild(r);
    }
    const shown = facts.filter(f => !listed[factRowId(f.k)]);
    for (const f of shown.slice(0, factCap)) {
      const r = el('div', 'hc-row');
      r.appendChild(el('span', 'hc-id', String(f.k).split('.')[0]));
      const t = factText(f);
      const d = el('span', 'hc-d', t); d.title = f.k + ': ' + f.was + ' -> ' + f.now;
      r.appendChild(d);
      list.appendChild(r);
    }
    const hidden = Math.max(0, rows.length - rowCap) + Math.max(0, shown.length - factCap) + (facts.length - shown.length);
    if (!rows.length && !shown.length) list.appendChild(el('div', 'hc-row', 'No difference'));
    if (hidden) list.appendChild(el('div', 'hc-row hc-more', hidden + ' more not shown' + (facts.length - shown.length ? ' (' + (facts.length - shown.length) + ' told by the rows above)' : '')));
    card.appendChild(list);
  }

  function renderHealth() {
    const c = $('content');
    let wrap = $('hcWrap');
    if (!wrap) {
      injectStyle('hcCss', `
          .hc-head { display: flex; align-items: center; gap: 6px; padding: 12px 14px 8px; }
          .hc-title { flex: 1; font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: .05em; color: var(--accent-hi); }
          .hc-ver { padding: 0 14px 8px; font-size: 11px; color: var(--text-dim); display: flex; flex-wrap: wrap; gap: 6px; align-items: center; }
          .hc-chip { border: 1px solid var(--border); border-radius: 6px; padding: 1px 6px; background: var(--bg-elev); }
          .hc-chip.warn { border-color: rgba(224,180,87,.5); color: #e0b457; }
          .hc-card { margin: 0 12px; background: var(--bg-elev); border: 1px solid var(--border); border-radius: 10px; }
          .hc-card + .hc-card { margin-top: 8px; }
          .hc-row { display: flex; align-items: center; gap: 8px; padding: 7px 12px; font-size: 12px; }
          .hc-row + .hc-row, .hc-grp + .hc-row, .hc-row + .hc-sub2, .hc-row + .hc-why { border-top: 1px solid var(--border); }
          .hc-dot { width: 8px; height: 8px; border-radius: 50%; flex: 0 0 auto; }
          .hc-dot.ok { background: #4dd28a; box-shadow: 0 0 6px rgba(77,210,138,.35); }
          .hc-dot.warn { background: #e0b457; }
          .hc-dot.na { background: var(--text-mute, #666); opacity: .6; }
          .hc-dot.bad { background: #e05656; box-shadow: 0 0 6px rgba(224,86,86,.35); }
          .hc-k { flex: 0 0 auto; color: var(--text); }
          .hc-k.na { color: var(--text-dim); }
          .hc-feat { color: var(--text-mute, #777); font-size: 10.5px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; max-width: 30%; }
          .hc-d { margin-left: auto; color: var(--text-dim); font-size: 11px; text-align: right; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; max-width: 62%; }
          .hc-row.wrap .hc-d { white-space: normal; }
          .hc-kind { color: var(--text); font-weight: 600; }
          .hc-why { padding: 0 12px 7px 28px; font-size: 10.5px; color: var(--text-dim); }
          .hc-why b { color: var(--text); font-weight: 600; }
          .hc-grp { display: flex; align-items: center; gap: 8px; padding: 8px 12px; font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: .04em; color: var(--accent-hi); cursor: pointer; }
          .hc-grp .hc-n { margin-left: auto; color: var(--text-dim); font-weight: 600; letter-spacing: 0; text-transform: none; }
          .hc-grp .hc-gf { color: var(--text-mute, #777); font-weight: 500; letter-spacing: 0; text-transform: none; font-size: 10.5px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
          .hc-status { margin: 0 14px 10px; padding: 9px 11px; border-radius: 8px; border: 1px solid; line-height: 1.4; }
          .hc-sline { display: flex; align-items: center; gap: 9px; }
          .hc-sdot { width: 9px; height: 9px; border-radius: 50%; flex: 0 0 auto; background: currentColor; box-shadow: 0 0 8px currentColor; }
          .hc-st { font-weight: 600; font-size: 12.5px; }
          .hc-sd { font-size: 11px; color: var(--text-dim); margin-top: 3px; }
          .hc-sd b { color: var(--text); font-weight: 600; }
          .hc-status.ok { color: #4dd28a; border-color: rgba(77,210,138,.35); background: rgba(77,210,138,.09); }
          .hc-status.warn { color: #e0b457; border-color: rgba(224,180,87,.35); background: rgba(224,180,87,.09); }
          .hc-status.bad { color: #e05656; border-color: rgba(224,86,86,.38); background: rgba(224,86,86,.10); }
          .hc-status.na { color: var(--text-dim); border-color: var(--border); background: var(--bg-elev); }
          .hc-btnrow { display: flex; flex-wrap: wrap; gap: 6px; margin: 0 14px 8px; align-items: center; }
          .hc-ab { padding: 1px 7px; font-size: 10.5px; margin-left: 4px; }
          .hc-ab.on { border-color: var(--accent); color: var(--accent-hi); }
          .hc-open { padding: 1px 7px; font-size: 10.5px; flex: 0 0 auto; }
          .hc-hint { margin: 10px 14px; font-size: 11.5px; color: var(--text-dim); line-height: 1.5; }
          .hc-sub { margin: 14px 14px 6px; font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: .05em; color: var(--accent-hi); }
          .hc-sub small { color: var(--text-dim); font-weight: 500; text-transform: none; letter-spacing: 0; margin-left: 6px; }
          .hc-list { max-height: 260px; overflow-y: auto; overflow-x: hidden; border-top: 1px solid var(--border); }
          .hc-list .hc-row { padding: 4px 12px; font-size: 11.5px; }
          .hc-id { flex: 0 0 auto; color: var(--text-dim); font-variant-numeric: tabular-nums; min-width: 48px; }
          .hc-more { color: var(--text-mute, #777); font-size: 10.5px; justify-content: flex-end; }
          .hc-fe { cursor: pointer; }
          .hc-fe .hc-k { font-weight: 600; }
          .hc-fe .hc-n { color: var(--text-dim); font-size: 10.5px; flex: 0 0 auto; }
          .hc-fe-rows .hc-row { padding-left: 28px; background: rgba(255,255,255,.02); }
          .hc-need { color: #e0b457; }`);
      c.innerHTML = '';
      wrap = document.createElement('div'); wrap.id = 'hcWrap'; wrap.className = 'pane'; c.appendChild(wrap);
    }
    let body = document.getElementById('hcBody');
    if (!body) {
      wrap.innerHTML = '';
      const head = el('div', 'hc-head');
      head.appendChild(el('span', 'hc-title', 'Health check'));
      const btn = el('button', 'vw-btn'); btn.id = 'hcRunBtn';
      btn.dataset.tip = 'Check everything the app reads from the game (runs by itself after a game update)';
      btn.addEventListener('click', hcRun);
      const cs = el('button', 'vw-btn', 'Copy summary'); cs.id = 'hcCopySumBtn';
      cs.dataset.tip = 'Copy the readable report: what changed, what it breaks, then every row';
      cs.addEventListener('click', hcCopySummary);
      const cp = el('button', 'vw-btn', 'Copy JSON'); cp.id = 'hcCopyBtn';
      cp.dataset.tip = 'Copy this run as JSON';
      cp.addEventListener('click', hcCopy);
      head.appendChild(btn); head.appendChild(cs); head.appendChild(cp);
      wrap.appendChild(head);
      body = document.createElement('div'); body.id = 'hcBody'; wrap.appendChild(body);
    }
    hcRefresh();
    const runBtn = document.getElementById('hcRunBtn');
    if (runBtn) runBtn.textContent = hcBusy ? 'Checking...' : 'Run check';
    const h = window.__rtxHealth || {};
    const hcSigNow = JSON.stringify([hcData && hcData.summary, hcSeq, hcBusy, hcDiff, hcCmp, hcHist.length, hcCmpA, hcCmpB, hcOpen, hcFeatOpen, icMisses, icCov, h.pending, h.running]);
    if (hcSigNow === hcSig && body.childNodes.length) return;
    hcSig = hcSigNow;
    hcKeepScroll = Array.from(document.querySelectorAll('.hc-list')).map(l => l.scrollTop);
    body.innerHTML = '';

    if (!hcData || !Array.isArray(hcData.checks)) {
      let hint = 'Checks the client code, offsets, live layout, packets, cache content and the companion. It runs by itself when the game client or its cache is new; Run check starts one now.';
      if (hcBusy || h.running) hint = 'Checking the game client...';
      else if (h.pending) hint = 'Game ' + (h.pending.trigger === 'cache update' ? 'content ' : '') + 'updated (' + (h.pending.why || h.pending.trigger) + '). The check runs once you are in game, or start it now.';
      body.appendChild(el('div', 'hc-hint', hint));
      renderIcons(body);
      return;
    }
    const checks = hcData.checks;
    const s = hcData.summary || {};
    const byId = {};
    for (const r of checks) if (r.id) byId[r.id] = r;
    const im = hcImpact();
    const level = hcLevel(im);
    // the summary: what changed in the game, the verdict by feature, what to do
    {
      const state = level === 'red' ? 'bad' : level === 'amber' ? 'warn' : level === 'green' ? 'ok' : 'na';
      const st = el('div', 'hc-status ' + state);
      const line = el('div', 'hc-sline');
      line.appendChild(el('span', 'hc-sdot'));
      line.appendChild(el('span', 'hc-st', hcChangeLine(im) || 'Health check'));
      st.appendChild(line);
      const v = el('div', 'hc-sd'); v.appendChild(el('b', null, hcVerdictLine(im, s)));
      st.appendChild(v);
      st.appendChild(el('div', 'hc-sd', hcAdviceLine(im, level, s)));
      if ((im.broken || []).length || (im.moved || []).length) {
        const ids = [].concat(im.broken || [], im.moved || []).map(e => (e.rows || [])[0]).filter(Boolean);
        const uniq = ids.filter((x, i) => ids.indexOf(x) === i);
        if (uniq.length) st.appendChild(el('div', 'hc-sd', 'Rows to fix: ' + uniq.slice(0, 8).join(', ') + (uniq.length > 8 ? ' and ' + (uniq.length - 8) + ' more' : '')));
      }
      body.appendChild(st);
    }
    // build strip and run summary
    {
      const b = hcData.build || {};
      const ctx = hcData.context || {};
      const v = el('div', 'hc-ver');
      const flav = b.flavour === 'vulkan' ? 'Vulkan' : b.flavour === 'opengl' ? 'OpenGL' : (b.flavour || '');
      v.appendChild(el('span', 'hc-chip', hcData.version || 'unknown build'));
      if (flav) v.appendChild(el('span', 'hc-chip', flav));
      if (b.stamp) v.appendChild(el('span', 'hc-chip', b.stamp));
      v.appendChild(el('span', 'hc-chip' + (b.known ? '' : ' warn'), b.known ? 'validated' + (b.validated ? ' ' + b.validated : '') : 'new build'));
      const trig = ctx.trigger || im.trigger || h.trigger;
      if (trig) v.appendChild(el('span', 'hc-chip', 'run: ' + trig));
      if (im.complete === false) v.appendChild(el('span', 'hc-chip warn', 'incomplete'));
      v.appendChild(el('span', null, (s.pass | 0) + ' pass  ' + (s.fail | 0) + ' fail  ' + (s.warn | 0) + ' warn  ' + (s.unchecked | 0) + ' not checked  ' + (s.ms | 0) + ' ms'));
      const gb = byId['build.game'];
      if (gb && gb.ok === 2) {
        const mr = el('button', 'vw-btn', 'Mark reviewed');
        mr.dataset.tip = 'Clear the update notice for this game build';
        mr.addEventListener('click', hcMarkReviewed);
        v.appendChild(mr);
      }
      body.appendChild(v);
    }
    // by feature: broken, moved but working, format changed, not verified
    {
      const lists = [['Broken', im.broken, 'bad', 'needs a RuneTools update'], ['Working with a warning', im.moved, 'warn', 'a moved value, a stale table or an id near its ceiling'],
                     ['Format changed', im.format, 'warn', 'bytes read differently'], ['Not verified', im.unverified, 'na', 'could not be checked here']];
      for (const [title, list, dot, what] of lists) {
        if (!list || !list.length) continue;
        const card = el('div', 'hc-card');
        const gh = el('div', 'hc-grp');
        gh.appendChild(el('span', 'hc-dot ' + dot));
        gh.appendChild(el('span', null, title));
        gh.appendChild(el('span', 'hc-gf', what));
        gh.appendChild(el('span', 'hc-n', String(list.length)));
        card.appendChild(gh);
        for (const e of list) {
          const key = title + '|' + e.feature;
          const r = el('div', 'hc-row hc-fe');
          r.appendChild(el('span', 'hc-dot ' + (e.precondition ? 'na' : dot)));
          r.appendChild(el('span', 'hc-k', e.feature));
          const t = featureTab(e.feature);
          if (t) {
            const ob = el('button', 'vw-btn hc-open', 'Open');
            ob.dataset.tip = 'Open ' + t.label;
            ob.addEventListener('click', ev => { ev.stopPropagation(); try { openTab(t); } catch (e2) {} });
            r.appendChild(ob);
          }
          const why = el('span', 'hc-d', (e.need ? 'needs: ' + e.need + '. ' : '') + (e.why || ''));
          why.title = e.why || '';
          if (e.need) why.classList.add('hc-need');
          r.appendChild(why);
          r.appendChild(el('span', 'hc-n', (e.rows || []).length + (hcFeatOpen[key] ? ' -' : ' +')));
          r.addEventListener('click', () => { hcFeatOpen[key] = !hcFeatOpen[key]; hcSig = ''; paneRun('health', renderHealth); });
          card.appendChild(r);
          if (hcFeatOpen[key]) {
            const sub = el('div', 'hc-fe-rows');
            for (const id of (e.rows || [])) {
              const chk = byId[id];
              const rr = el('div', 'hc-row wrap');
              rr.appendChild(el('span', 'hc-dot ' + (chk ? DOT(chk.ok) : 'na')));
              rr.appendChild(el('span', 'hc-k', chk ? chk.k : id));
              rr.appendChild(el('span', 'hc-d', chk ? (chk.d || '') : ''));
              sub.appendChild(rr);
            }
            card.appendChild(sub);
          }
        }
        body.appendChild(card);
      }
    }
    // groups, in the order the check ran them (each depends on the ones before)
    body.appendChild(el('div', 'hc-sub', 'All checks'));
    const groups = [];
    const gmap = {};
    for (const r of checks) {
      const g = r.g || 'Other';
      if (!gmap[g]) { gmap[g] = []; groups.push(g); }
      gmap[g].push(r);
    }
    for (const g of groups) {
      const rows = gmap[g];
      const worst = rows.some(r => r.ok === 0) ? 0 : rows.some(r => r.ok === 2) ? 2 : rows.every(r => r.ok === 3) ? 3 : 1;
      const pass = rows.filter(r => r.ok === 1).length;
      if (!(g in hcOpen)) hcOpen[g] = worst === 0;
      const card = el('div', 'hc-card');
      const gh = el('div', 'hc-grp');
      gh.appendChild(el('span', 'hc-dot ' + DOT(worst)));
      gh.appendChild(el('span', null, g));
      // the features the group's non-passing rows touch
      const touched = [];
      for (const r of rows) if (r.ok !== 1 && Array.isArray(r.f)) for (const f of r.f) { const p = String(f).split(': ')[0]; if (touched.indexOf(p) < 0) touched.push(p); }
      if (touched.length) gh.appendChild(el('span', 'hc-gf', touched.slice(0, 4).join(', ') + (touched.length > 4 ? ' +' + (touched.length - 4) : '')));
      gh.appendChild(el('span', 'hc-n', pass + '/' + rows.length + (hcOpen[g] ? '' : '  +')));
      gh.addEventListener('click', () => { hcOpen[g] = !hcOpen[g]; hcSig = ''; paneRun('health', renderHealth); });
      card.appendChild(gh);
      if (hcOpen[g]) {
        for (const chk of rows) {
          const r = el('div', 'hc-row' + (chk.ok === 0 ? ' wrap' : ''));
          r.appendChild(el('span', 'hc-dot ' + DOT(chk.ok)));
          r.appendChild(el('span', 'hc-k' + (chk.ok === 3 ? ' na' : ''), chk.k));
          if (chk.ok !== 1 && chk.ok !== 0 && Array.isArray(chk.f) && chk.f.length) {
            const fe = el('span', 'hc-feat', chk.f.slice(0, 3).join(', ') + (chk.f.length > 3 ? ' +' + (chk.f.length - 3) : ''));
            fe.title = chk.f.join(', ');
            r.appendChild(fe);
          }
          const dt = el('span', 'hc-d');
          const kw = chk.kind ? KIND_WORD(chk.kind) : '';
          const d = String(chk.d || '');
          if (kw && !/^(MOVED|FORMAT|GONE|NEW|UNVERIFIED)\b/.test(d)) { dt.appendChild(el('span', 'hc-kind', kw)); dt.appendChild(document.createTextNode(d ? ': ' + d : '')); }
          else dt.textContent = d;
          if (chk.ok === 3 && chk.need) { dt.appendChild(document.createTextNode(d ? '. ' : '')); dt.appendChild(el('span', 'hc-need', 'Needs: ' + chk.need)); }
          dt.title = d;
          r.appendChild(dt);
          card.appendChild(r);
          if ((chk.ok === 0 || chk.ok === 2) && (chk.exp || chk.got || (chk.ok === 0 && Array.isArray(chk.f) && chk.f.length))) {
            const why = el('div', 'hc-why');
            if (chk.exp || chk.got) why.appendChild(document.createTextNode('Expected ' + (chk.exp || '?') + ', found ' + (chk.got || '?') + '. '));
            if (chk.ok === 0 && Array.isArray(chk.f) && chk.f.length) { why.appendChild(el('b', null, 'Breaks: ')); why.appendChild(document.createTextNode(chk.f.join(', '))); }
            card.appendChild(why);
          }
        }
      }
      body.appendChild(card);
    }
    // what changed since the baseline run, in words
    if (hcDiff && hcDiff.ok && ((hcDiff.rows || []).length || (hcDiff.facts || []).length)) {
      const sub = el('div', 'hc-sub', 'Changed since ' + (SINCE[hcDiff.against] || 'the last clean run'));
      const b = hcData.build || {};
      sub.appendChild(el('small', null, 'this run ' + runLabel(hcHist.length ? hcHist[0].name : '') + (b.stamp ? '' : '') + (hcHist.length > 1 ? ', ' + (hcHist.length - 1) + ' earlier runs kept' : '')));
      body.appendChild(sub);
      const card = el('div', 'hc-card');
      renderDiffList(card, hcDiff, 60, 60);
      body.appendChild(card);
    }
    // compare any two runs from the history
    if (hcHist.length > 1) {
      body.appendChild(el('div', 'hc-sub', 'Compare runs'));
      const card = el('div', 'hc-card');
      const list = el('div', 'hc-list');
      for (const hr of hcHist.slice(0, 30)) {
        const r = el('div', 'hc-row');
        r.appendChild(el('span', 'hc-dot ' + (hr.fail ? 'bad' : hr.warn ? 'warn' : 'ok')));
        r.appendChild(el('span', 'hc-id', runLabel(hr.name)));
        r.appendChild(el('span', 'hc-d', hr.fail + ' fail  ' + hr.warn + ' warn  ' + (hr.unchecked | 0) + ' not checked'));
        for (const side of ['A', 'B']) {
          const picked = (side === 'A' ? hcCmpA : hcCmpB) === hr.name;
          const bt = el('button', 'vw-btn hc-ab' + (picked ? ' on' : ''), side);
          bt.addEventListener('click', () => { if (side === 'A') hcCmpA = hr.name; else hcCmpB = hr.name; hcCmp = null; hcSig = ''; paneRun('health', renderHealth); });
          r.appendChild(bt);
        }
        list.appendChild(r);
      }
      card.appendChild(list);
      body.appendChild(card);
      const row = el('div', 'hc-btnrow');
      const go = el('button', 'vw-btn', 'Compare A with B');
      go.addEventListener('click', hcCompare);
      row.appendChild(go);
      body.appendChild(row);
      if (hcCmp && hcCmp.ok) {
        const card2 = el('div', 'hc-card');
        renderDiffList(card2, hcCmp, 80, 100);
        body.appendChild(card2);
      }
    }
    renderIcons(body);
    if (hcKeepScroll.length) { const keep = hcKeepScroll; hcKeepScroll = []; setTimeout(() => { const ls = body.querySelectorAll('.hc-list'); ls.forEach((l, i) => { if (keep[i] != null) l.scrollTop = keep[i]; }); }, 0); }   // after layout, or Ultralight clamps it to 0
  }

  function renderIcons(body) {
    if (!icMisses && !icCov) return;
    const sub = document.createElement('div'); sub.className = 'hc-sub'; sub.textContent = 'Item icons';
    body.appendChild(sub);
    const card = document.createElement('div'); card.className = 'hc-card';
    const row = (state, k, d) => {
      const r = document.createElement('div'); r.className = 'hc-row';
      const dot = document.createElement('span'); dot.className = 'hc-dot ' + state;
      const nm = document.createElement('span'); nm.className = 'hc-k'; nm.textContent = k;
      const dt = document.createElement('span'); dt.className = 'hc-d'; dt.textContent = d;
      r.appendChild(dot); r.appendChild(nm); r.appendChild(dt); card.appendChild(r);
    };
    const m = icMisses || {};
    row(m.rendered ? 'ok' : 'warn', 'Rendered icons', String(m.rendered || 0));
    row(m.packIcons ? 'ok' : 'bad', 'Icon pack', m.packIds ? (m.packIds + ' ids, ' + m.packIcons + ' icons') : 'not loaded');
    if (icCov && icCov.pending) row('warn', 'Cache coverage', 'scanning...');
    else if (icCov && typeof icCov.items === 'number') {
      const miss = (icCov.missing || []).length;
      row('ok', 'Cache items with names', String(icCov.items));
      row(miss ? 'warn' : 'ok', 'Missing icons', miss ? (miss + ' of ' + icCov.items) : 'none');
      if (miss) {
        const list = document.createElement('div'); list.className = 'hc-list';
        for (const id of icCov.missing.slice(0, 40)) {
          const r = document.createElement('div'); r.className = 'hc-row';
          const i = document.createElement('span'); i.className = 'hc-id'; i.textContent = id;
          const n = document.createElement('span'); n.className = 'hc-k'; n.textContent = icNames[id] || '';
          r.appendChild(i); r.appendChild(n); list.appendChild(r);
        }
        if (miss > 40) {
          const r = document.createElement('div'); r.className = 'hc-row';
          const n = document.createElement('span'); n.className = 'hc-d'; n.textContent = '+ ' + (miss - 40) + ' more';
          r.appendChild(n); list.appendChild(r);
        }
        card.appendChild(list);
      }
    }
    const misses = Array.isArray(m.misses) ? m.misses : [];
    row(misses.length ? 'warn' : 'ok', 'Requested but absent this session', misses.length ? String(m.missCount) : 'none');
    if (misses.length) {
      const list = document.createElement('div'); list.className = 'hc-list';
      for (const [id, n] of misses.slice(0, 40)) {
        const r = document.createElement('div'); r.className = 'hc-row';
        const i = document.createElement('span'); i.className = 'hc-id'; i.textContent = id;
        const k = document.createElement('span'); k.className = 'hc-k'; k.textContent = icNames[id] || '';
        const d = document.createElement('span'); d.className = 'hc-d'; d.textContent = n + 'x';
        r.appendChild(i); r.appendChild(k); r.appendChild(d); list.appendChild(r);
      }
      card.appendChild(list);
    }
    body.appendChild(card);
  }

  Object.assign(window, { renderHealth });
  registerTab({ id: 'health', render: renderHealth, refresh: hcRefresh });
})();
