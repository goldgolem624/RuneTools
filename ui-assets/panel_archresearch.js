// RuneToolsX panel: Archaeology research (Field Study / Report status).
// Spliced inline into client.html; bare classic script sharing one global scope.
// DBTable 90 is the research list: column 0 is the progress BIT, 3 the name, 6 the Field Study
// text and 7 the Report text (bridge archResearch decodes it). Progress is live varp bits,
// 32 per varp:
//   field study known -> varp 9297 (bits 0-31), 9298 (32-63), 11740 (64-95)
//   report filed      -> varp 9299,             9300,          11741
// A research is only fully done once the REPORT is filed.

const ARCH_RES_FIELD_VARPS  = [9297, 9298, 11740];
const ARCH_RES_REPORT_VARPS = [9299, 9300, 11741];

// Enum 14082 maps the journal's tab index to a per-culture enum, and each of those lists that
// culture's research BY DBRow id -- hence the `d` (row id) field. Anything in no culture enum
// is not a book page: the "<site> Dig Site" rows (kind 1) are site-wide team research, and a
// few named ones live outside the book.
const ARCH_CULTURES = [
  [0, 'Special',      14083], [1, 'Armadylean', 14084], [2, 'Bandosian',   14085],
  [3, 'Dragonkin',    10575], [4, 'Guthixian',   3016], [5, 'Saradominist', 14086],
  [6, 'Zamorakian',   14087], [7, 'Zarosian',   14088],
];
const ARCH_GRP_SITE = 'Dig sites', ARCH_GRP_OTHER = 'Other research';
let archResCult = null;      // DBRow id -> culture name, built once from the cache enums

let archResList = null;      // [{b,n,f,r}] from the cache; static for the game build
let archResVp = {};
let archResAt = 0, archResBusy = false, archResSig = '';
let archResFilter = '', archResHideDone = false;

// Shared by this panel AND the mystery guides, so both always agree on what is done.
async function archResearchEnsure(force) {
  if (archResBusy) return;
  const now = Date.now();
  if (!force && now - archResAt < 1500) return;
  archResBusy = true; archResAt = now;
  try {
    if (archResList === null) {
      if (!bridge().archResearch) { archResList = []; return; }   // older launcher build
      try { archResList = JSON.parse(bridge().archResearch() || '[]') || []; }
      catch (e) { archResList = []; }
    }
    // Culture pages: one enum read each, once per session (static for the game build).
    if (archResCult === null && bridge().enumInfo) {
      const map = {};
      for (const [, name, eid] of ARCH_CULTURES) {
        try {
          const e = JSON.parse(await bridge().enumInfo(eid) || '{}');
          for (const k in e) { const row = e[k]; if (row > 0) map[row] = name; }
        } catch (err) { /* a missing culture enum just leaves those rows ungrouped */ }
      }
      archResCult = map;
    }
    const ids = ARCH_RES_FIELD_VARPS.concat(ARCH_RES_REPORT_VARPS).join(',');
    try {
      const d = JSON.parse(await bridge().varps(myPid(), ids));
      if (d && typeof d === 'object') archResVp = d;
    } catch (e) {}
  } finally { archResBusy = false; }
}

// bit -> is it set in this varp bank? (banks hold 32 bits each, in order)
function archResBitSet(bank, bit) {
  if (!(bit >= 0)) return false;
  const vid = bank[Math.floor(bit / 32)];
  if (vid === undefined) return false;
  const raw = archResVp[vid];
  return typeof raw === 'number' && ((raw >>> (bit % 32)) & 1) === 1;
}
// 'report' = fully researched, 'field' = studied but not written up, 'none' = untouched
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

// Does this guide step name a research that is COMPLETED? true/false, or null when the step
// names no known research (caller then leaves the step manually tickable). Matching is by the
// research's cache NAME appearing in the step text; the longest match wins, so "Incomplete
// Portal Network III" is never mistaken for "Incomplete Portal Network I".
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

// compact live-progress stamp, so panels displaying research state repaint the moment a field
// study or report lands
function archResSigVal() {
  return ARCH_RES_FIELD_VARPS.concat(ARCH_RES_REPORT_VARPS).map(v => archResVp[v] | 0).join('.');
}

async function fetchArchResearch() {
  await archResearchEnsure();
  if (activeTab === 'archresearch') renderArchResearch();
}

// Culture grouping joins the book enums on the DBRow id, which only a launcher build carrying
// the `d`/`t` fields emits. Without them every row would collapse into one "Other research"
// heap, so fall back to the plain ungrouped list.
function archResGrouped() {
  return !!(archResList && archResList.length && archResList.some(e => e.d != null));
}
// Which culture page a research belongs to, else the site-wide / outside-the-book buckets.
function archResGroup(e) {
  const c = archResCult && archResCult[e.d];
  if (c) return c;
  return e.t === 1 ? ARCH_GRP_SITE : ARCH_GRP_OTHER;
}
function archResGroupOrder() {
  return ARCH_CULTURES.map(x => x[1]).concat([ARCH_GRP_SITE, ARCH_GRP_OTHER]);
}

// The header + filter bar are built ONCE and never re-rendered: rebuilding them on a keystroke
// destroys the focused <input>. Only #arList is repainted.
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
      archResHideDone = !archResHideDone; archResSig = ''; paintArchResearch();
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
  // 'field' = the field study is done and the write-up is outstanding, i.e. the research team
  // still has to be put on it at the guild.
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
    // Older launcher build: no row ids to join the culture enums on, so show one plain list.
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
