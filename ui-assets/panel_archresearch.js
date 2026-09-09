// RuneToolsX panel: Archaeology research (Field Study / Report status). DBTable 90 = research list: col 0 progress BIT, 3 name, 6 Field Study.
// Field study known -> varp 9297 (bits 0-31), 9298 (32-63), 11740 (64-95); report filed -> varp 9299, 9300, 11741.
(function () {

const ARCH_RES_FIELD_VARPS  = [9297, 9298, 11740];
const ARCH_RES_REPORT_VARPS = [9299, 9300, 11741];

// Enum 14082 maps the journal's tab index to a per-culture enum, and each of those lists that
const ARCH_CULTURES = [
  [0, 'Special',      14083], [1, 'Armadylean', 14084], [2, 'Bandosian',   14085],
  [3, 'Dragonkin',    10575], [4, 'Guthixian',   3016], [5, 'Saradominist', 14086],
  [6, 'Zamorakian',   14087], [7, 'Zarosian',   14088],
];
const ARCH_RELIC_PRESET_VP = 12086, ARCH_RELIC_VB_BASE = 57207, ARCH_RELIC_SLOTS = 3;
function archRelicVbs(preset) {
  const out = [];
  for (let s = 0; s < ARCH_RELIC_SLOTS; s++) out.push(ARCH_RELIC_VB_BASE + preset * 4 + s);
  return out;
}
let archRelicNames = null;   // db94 key -> power name, built once from the cache
let archRelicPreset = 0, archRelicVals = [];   // active preset + its 3 varbit values
let archRelicAt = 0, archRelicBusy = false;

async function archRelicEnsure(force) {
  if (archRelicBusy || !bridge()) return;
  const now = Date.now();
  if (!force && now - archRelicAt < 1500) return;
  archRelicBusy = true; archRelicAt = now;
  try {
    if (archRelicNames === null && bridge().dbRows) {
      try {
        const rows = JSON.parse(await rtxData.raw('cache.dbRows', 94) || 'null');
        if (Array.isArray(rows) && rows.length) {
          const map = {};
          for (const r of rows) {
            const key = (r.i && r.i['0'] && r.i['0'][0] !== undefined) ? (r.i['0'][0] | 0) : (r.f | 0);
            const nm = r.s && r.s['1'] && r.s['1'][0];
            if (nm) map[key] = nm;
          }
          archRelicNames = map;
        }
      } catch (e) { /* retry next call until the cache is open */ }
    }
    try {
      const pv = JSON.parse(await rtxData.raw('state.varps', String(ARCH_RELIC_PRESET_VP)));
      archRelicPreset = (pv && pv[ARCH_RELIC_PRESET_VP] | 0) || 0;
    } catch (e) {}
    if (bridge().varbits) {
      try {
        const vbs = archRelicVbs(archRelicPreset);
        const vb = JSON.parse(await rtxData.raw('state.varbitsCsv', vbs.join(',')) || 'null');
        archRelicVals = vbs.map(id => (vb && vb[id]) | 0);
      } catch (e) {}
    }
  } finally { archRelicBusy = false; }
}
function archRelicActive(name) {
  if (!name || !archRelicNames) return false;
  const want = String(name).toLowerCase();
  return archRelicVals.some(v => v > 0 && String(archRelicNames[v] || '').toLowerCase() === want);
}

const ARCH_GRP_SITE = 'Dig sites', ARCH_GRP_OTHER = 'Other research';
let archResCult = null;      // DBRow id -> culture name, built once from the cache enums

let archResList = null;      // [{b,n,f,r}] from the cache; static for the game build
let archResVp = {};
let archResAt = 0, archResBusy = false, archResSig = '';
let archResFilter = '', archResHideDone = false;
try { archResHideDone = localStorage.getItem('rtxArchResHide') === '1'; } catch (e) {}

async function archResearchEnsure(force) {
  if (archResBusy) return;
  const now = Date.now();
  if (!force && now - archResAt < 1500) return;
  archResBusy = true; archResAt = now;
  try {
    if (archResList === null) {
      if (!bridge().archResearch) { archResList = []; return; }   // older launcher build
      try { archResList = JSON.parse(rtxData.sync('cache.archResearch') || '[]') || []; }
      catch (e) { archResList = []; }
    }
    if (archResCult === null && bridge().enumInfo) {
      const map = {};
      for (const [, name, eid] of ARCH_CULTURES) {
        try {
          const e = JSON.parse(await rtxData.raw('cache.enumInfo', eid) || '{}');
          for (const k in e) { const row = e[k]; if (row > 0) map[row] = name; }
        } catch (err) { /* a missing culture enum just leaves those rows ungrouped */ }
      }
      archResCult = map;
    }
    const ids = ARCH_RES_FIELD_VARPS.concat(ARCH_RES_REPORT_VARPS).join(',');
    try {
      const d = JSON.parse(await rtxData.raw('state.varps', ids));
      if (d && typeof d === 'object') archResVp = d;
    } catch (e) {}
  } finally { archResBusy = false; }
  await archRelicEnsure(force);   // active preset + harnessed powers (own throttle)
}

function archResBitSet(bank, bit) {
  if (!(bit >= 0)) return false;
  const vid = bank[Math.floor(bit / 32)];
  if (vid === undefined) return false;
  const raw = archResVp[vid];
  return typeof raw === 'number' && ((raw >>> (bit % 32)) & 1) === 1;
}
function archResStatus(e) {
  if (!e) return 'none';
  if (archResBitSet(ARCH_RES_REPORT_VARPS, e.b)) return 'report';
  if (archResBitSet(ARCH_RES_FIELD_VARPS, e.b)) return 'field';
  return 'none';
}
function archResCounts() {
  let done = 0, part = 0, total = 0;
  for (const e of (archResList || [])) {
    if (e.t === 1) continue;                        // site-wide team research: repeatable, not tracked
    total++;
    const st = archResStatus(e);
    if (st === 'report') done++; else if (st === 'field') part++;
  }
  return { done: done, part: part, total: total };
}

function archResearchDoneIn(text) {
  if (!text || !archResList || !archResList.length) return null;
  const hay = String(text).toLowerCase();
  let best = null;
  for (const e of archResList) {
    const n = (e.n || '').trim();
    if (n.length < 4) continue;                     // too short to match safely
    if (hay.indexOf(n.toLowerCase()) < 0) continue;
    if (!best || n.length > best.n.length) best = e;
  }
  if (!best) return null;
  return archResStatus(best) === 'report';
}

function archResSigVal() {
  return ARCH_RES_FIELD_VARPS.concat(ARCH_RES_REPORT_VARPS).map(v => archResVp[v] | 0).join('.');
}

async function fetchArchResearch() {
  await archResearchEnsure();
  paneRun('archresearch', renderArchResearch);
}

function archResGrouped() {
  return !!(archResList && archResList.length && archResList.some(e => e.d != null));
}
function archResGroup(e) {
  const c = archResCult && archResCult[e.d];
  if (c) return c;
  return e.t === 1 ? ARCH_GRP_SITE : ARCH_GRP_OTHER;
}
function archResGroupOrder() {
  return ARCH_CULTURES.map(x => x[1]).concat([ARCH_GRP_SITE, ARCH_GRP_OTHER]);
}

function renderArchResearch() {
  const c = $('content');
  let wrap = $('arWrap');
  if (!wrap) {
    c.innerHTML = '';
    wrap = document.createElement('div');
    wrap.id = 'arWrap';
    wrap.className = 'pk-wrap';
    wrap.innerHTML = '<div class="dg-head"><div><div class="dg-title">Research</div>'
      + '<div class="dg-sub" id="arSub"></div></div></div>'
      + '<div class="ar-bar">'
      + '<input id="arFilter" class="dg-sync-in" type="text" placeholder="filter research" spellcheck="false">'
      + '<button id="arHideDone" class="dg-sync-btn">Show all</button></div>'
      + '<div id="arList"></div>';
    c.appendChild(wrap);
    archResSig = '';
    const inp = wrap.querySelector('#arFilter');
    inp.value = archResFilter;
    inp.addEventListener('input', () => { archResFilter = inp.value || ''; archResSig = ''; paintArchResearch(); });
    wrap.querySelector('#arHideDone').addEventListener('click', () => {
      archResHideDone = !archResHideDone; archResSig = '';
      try { localStorage.setItem('rtxArchResHide', archResHideDone ? '1' : '0'); } catch (e) {}
      paintArchResearch();
    });
  }
  paintArchResearch();
}

function paintArchResearch() {
  const list = $('arList'); if (!list) return;
  if (archResList === null) { list.innerHTML = '<div class="stor-empty">Reading the research list...</div>'; return; }
  if (!archResList.length) {
    list.innerHTML = '<div class="stor-empty">No research found in the cache.<br>'
      + '<span class="dg-hint">Needs a launcher build with the archResearch bridge.</span></div>';
    return;
  }
  const q = archResFilter.trim().toLowerCase();
  const rows = archResList.filter(e => {
    if (e.t === 1) return false;                    // "<site> Dig Site" team research is repeatable -- not worth listing
    const st = archResStatus(e);
    if (archResHideDone && st === 'report') return false;
    if (!q) return true;
    return (e.n || '').toLowerCase().indexOf(q) >= 0
        || (e.f || '').toLowerCase().indexOf(q) >= 0
        || (e.r || '').toLowerCase().indexOf(q) >= 0;
  });
  const cnt = archResCounts();
  const sig = JSON.stringify([cnt, q, archResHideDone, archResGrouped(),
                              rows.map(e => e.b + archResStatus(e))]);
  if (sig === archResSig) return;
  archResSig = sig;

  const sub = $('arSub');
  if (sub) {
    sub.textContent = cnt.done + ' of ' + cnt.total + ' reports filed'
      + (cnt.part ? ' · ' + cnt.part + ' awaiting your research team' : '');
  }
  const btn = $('arHideDone');
  if (btn) { btn.textContent = archResHideDone ? 'Showing outstanding' : 'Show all'; btn.classList.toggle('ar-on', archResHideDone); }

  const esc = s => String(s == null ? '' : s).replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  const LABEL = { report: 'Complete', field: 'Gather team', none: 'Not started' };
  const TIP = { field: 'Field study done — gather your research team to file the report.' };

  const rowHtml = e => {
    const st = archResStatus(e);
    const tip = [];
    if (e.f) tip.push('Field Study: ' + e.f);
    if (e.r) tip.push('Report: ' + e.r);
    if (TIP[st]) tip.push(TIP[st]);
    return '<div class="ar-row ar-' + st + '" data-tip="' + esc(tip.join('\n') || 'No notes recorded yet.') + '">'
         + '<span class="ar-dot"></span>'
         + '<span class="ar-name">' + esc(e.n || ('Research #' + e.b)) + '</span>'
         + '<span class="ar-state">' + LABEL[st] + '</span></div>';
  };
  let html = '';
  if (!archResGrouped()) {
    html = '<div class="ar-list">' + rows.map(rowHtml).join('') + '</div>';
  } else {
    const by = {};
    for (const e of rows) { const g = archResGroup(e); (by[g] = by[g] || []).push(e); }
    for (const g of archResGroupOrder()) {
      const items = by[g];
      if (!items || !items.length) continue;
      const gd = items.filter(e => archResStatus(e) === 'report').length;
      html += '<div class="ar-grp"><span>' + esc(g) + '</span><span class="ar-grp-c">'
           +  gd + ' / ' + items.length + '</span></div><div class="ar-list">'
           +  items.map(rowHtml).join('') + '</div>';
    }
  }
  if (!rows.length) html = '<div class="dg-none">Nothing matches that filter.</div>';
  list.innerHTML = html;
}

async function fetchRelics() {
  await archRelicEnsure();   // relic-only: archResearchEnsure can early-return on old builds
  paneRun('relics', renderRelics);
}
let relicsSig = '';
function renderRelics() {
  const c = $('content');
  let wrap = $('relicWrap');
  if (!wrap) {
    c.innerHTML = '';
    wrap = document.createElement('div'); wrap.id = 'relicWrap'; wrap.className = 'pk-wrap';
    wrap.innerHTML = '<div class="dg-head"><div><div class="dg-title">Active Relics</div>'
      + '<div class="dg-sub" id="relicSub"></div></div></div><div id="relicList"></div>';
    c.appendChild(wrap); relicsSig = '';
  }
  const esc = s => String(s == null ? '' : s).replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  const sig = JSON.stringify([archRelicPreset, archRelicVals, !!archRelicNames]);
  if (sig === relicsSig) return;
  relicsSig = sig;
  const sub = $('relicSub'); if (sub) sub.textContent = 'Monolith preset ' + (archRelicPreset + 1);
  const list = $('relicList'); if (!list) return;
  if (!archRelicVals.length) { list.innerHTML = '<div class="stor-empty">Reading the monolith...</div>'; return; }
  const rows = archRelicVals.map((v, i) => {
    const nm = v > 0 ? ((archRelicNames && archRelicNames[v]) || ('Power #' + v)) : 'Empty';
    return '<div class="ar-row ar-' + (v > 0 ? 'report' : 'none') + '">'
         + '<span class="ar-dot"></span>'
         + '<span class="ar-name">' + esc(nm) + '</span>'
         + '<span class="ar-state">Slot ' + (i + 1) + '</span></div>';
  }).join('');
  list.innerHTML = '<div class="ar-list">' + rows + '</div>';
}

Object.assign(window, { archRelicActive, archRelicEnsure, archResSigVal, archResearchDoneIn, archResearchEnsure, fetchArchResearch, fetchRelics });
registerTab({ id: 'archresearch', render: renderArchResearch, open: function () { archResSig = ''; fetchArchResearch(); } });
registerTab({ id: 'relics', render: renderRelics, open: function () { relicsSig = ''; fetchRelics(); } });
})();
