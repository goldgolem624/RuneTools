// RuneToolsX panel: Quest quick guides + Focused Quest tab.
// Step data comes from quest_guides.js (wiki import); per-account checkmarks persist
// through the questLoad/questSave bridges.
(function () {

  questGSteps = null;            // {"<quest name>":[stepIdx,..], "__focus": name}
  let qgFetching = false; qgSig = '';
  let _qgByLower = null;
  // The bulk guide data is NOT spliced into the page (see Dock.cpp kFiles): 1.3MB of one line
  // was half of everything the client parsed at startup, for data only these tabs use. It is
  // fetched on first use instead. Panels that register their own guide (Murder on the Border,
  // No Place Like Home) write into window.QUEST_GUIDES at startup, so the load MERGES into
  // whatever is already there rather than replacing the object.
  let qgDataState = 0;              // 0 = untried, 1 = loading, 2 = done (or failed)
  function questGuidesReady() { return qgDataState === 2; }
  async function questGuidesLoad() {
    if (qgDataState) return qgDataState === 2;
    qgDataState = 1;
    try {
      const txt = (bridge() && bridge().uiAsset) ? await bridge().uiAsset('quest_guides.js') : '';
      const a = txt.indexOf('{'), b = txt.lastIndexOf('}');
      if (a >= 0 && b > a) {
        const data = JSON.parse(txt.slice(a, b + 1));
        window.QUEST_GUIDES = window.QUEST_GUIDES || {};
        for (const k in data) if (!window.QUEST_GUIDES[k]) window.QUEST_GUIDES[k] = data[k];
        _qgByLower = null;          // rebuilt on the next lookup now the set has grown
      }
    } catch (e) { /* leave whatever panels registered themselves */ }
    qgDataState = 2;
    qgSig = '';                     // the guide list was rendered without this data: force a repaint
    return true;
  }
  function questGuideFor(name) {
    if (!qgDataState) questGuidesLoad();      // first ask kicks the load; this call returns null
    const G = window.QUEST_GUIDES;
    if (!G || !name) return null;
    if (G[name]) return G[name];
    if (!_qgByLower) { _qgByLower = {}; for (const k in G) _qgByLower[k.toLowerCase()] = G[k]; }
    return _qgByLower[name.toLowerCase()] ||
           _qgByLower[(name + ' (miniquest)').toLowerCase()] || null;
  }
  // Which account the blob was read for. Without this the guard was "have I loaded anything
  // at all", so switching character kept the previous one's ticked steps in memory and the
  // next save wrote them into the new character's file. Same guard as notes/mysteries.
  let qgLoadedPid = -1;
  async function qgEnsureLoaded() {
    if ((questGSteps !== null && qgLoadedPid === myPid()) || qgFetching || !bridge() || !bridge().questLoad) return;
    qgFetching = true;
    qgLoadedPid = myPid();
    try { const d = JSON.parse(await bridge().questLoad(myPid())); questGSteps = (d && typeof d === 'object') ? d : {}; }
    catch (e) { questGSteps = {}; }
    qgFetching = false;
    updateQuestHighlight();
    paneRun('questfocus', renderQuestFocus);
    if (paneVisible('quests')) { questDetailSig = ''; renderQuests(); }
  }
  function qgSave() {
    if (qgLoadedPid !== myPid()) return;   // never write one character's progress to another
    try { bridge().questSave(myPid(), JSON.stringify(questGSteps || {})); } catch (e) {}
  }
  function qgFocusName() { return (questGSteps && typeof questGSteps.__focus === 'string') ? questGSteps.__focus : ''; }
  function setQuestFocus(nm) {
    if (questGSteps === null) questGSteps = {};
    if (nm) questGSteps.__focus = nm; else delete questGSteps.__focus;
    qgSave(); qgSig = ''; questDetailSig = '';
    updateQuestHighlight();
  }
  function qgFlat(g) {               // running step index across all sections
    const out = [];
    let i = 0;
    for (const sec of g.sections) for (const s of sec.s) out.push({ sec: sec, txt: s, i: i++ });
    return out;
  }
  function qgQuestByName(nm) { return (typeof QUESTS !== 'undefined' && QUESTS) ? QUESTS.find(q => q.n === nm) : null; }
  function qgQuestDone(nm) {
    const q = qgQuestByName(nm);
    return !!(q && questsData && questsData.st[q.id] === 2);
  }
  // Stub: current-step NPC marking lives in a plugin (rtx.plugin.overlay.highlight).
  function updateQuestHighlight() {}
  function qgToggleStep(nm, idx) {
    if (questGSteps === null) questGSteps = {};
    const arr = (questGSteps[nm] = questGSteps[nm] || []);
    const p = arr.indexOf(idx);
    if (p >= 0) arr.splice(p, 1); else arr.push(idx);
    qgSave(); updateQuestHighlight();
  }
  function qgEsc(s) { return htmlEsc(s); }
  // "{Chat: a > b}" groups render one row per dialogue option (number chip + click text);
  // text BETWEEN two groups is a conditional branch intro (amber condition line).
  function qgChatHtml(line) {
    if (line.indexOf('{Chat:') < 0) return qgEsc(line);
    const parts = [];
    let i = 0;
    while (i < line.length) {
      const s = line.indexOf('{Chat:', i);
      if (s < 0) { parts.push({ t: 'x', v: line.slice(i) }); break; }
      if (s > i) parts.push({ t: 'x', v: line.slice(i, s) });
      const e = line.indexOf('}', s);
      parts.push({ t: 'c', v: line.slice(s + 6, e < 0 ? line.length : e) });
      i = e < 0 ? line.length : e + 1;
    }
    let h = '';
    for (let p = 0; p < parts.length; p++) {
      if (parts[p].t === 'x') {
        const txt = parts[p].v.trim();
        if (!txt) continue;
        const beforeGroup = parts.slice(p + 1).some(q => q.t === 'c');
        const afterGroup = parts.slice(0, p).some(q => q.t === 'c');
        h += (afterGroup && beforeGroup)
           ? '<span class="qg-cond">' + qgEsc(txt) + '</span>'
           : qgEsc(txt) + ' ';
      } else {
        h += '<span class="qg-chat">' + parts[p].v.trim().split(' > ').map(o => {
          const m = o.trim().match(/^(\d+)\s+(.+)$/);
          return '<span class="qg-copt"><span class="qg-cnum">' + qgEsc(m ? m[1] : '·') +
                 '</span><span class="qg-ctxt">' + qgEsc(m ? m[2] : o.trim()) + '</span></span>';
        }).join('') + '</span>';
      }
    }
    return h;
  }
  // Strip leaked wiki {{template}} markup; only DOUBLE braces are touched ({Chat:..} uses
  // single braces). {{Checklist..}} WRAPS real steps: drop its opener and keep the steps.
  function qgWiki(s) {
    s = String(s || '');
    let prev;
    do { prev = s; s = s.replace(/\{\{[^{}]*\}\}/g, ''); } while (s !== prev);
    s = s.replace(/\{\{\s*[Cc]hecklist\b[\s|;*]*/g, '');
    s = s.replace(/\{\{[\s\S]*$/g, '');                    // unmatched opener -> drop marker + its params
    s = s.replace(/\}\}/g, '');
    return s.replace(/[ \t]{2,}/g, ' ').trim();
  }
  // Per-quest "Required items" (curated; not in the cache quest config). A value may be a
  // function, evaluated per render, so the list can adjust to live state.
  const QG_REQ = {
    'Hermit Permits': [{ id: 954, n: 3, name: 'Rope' }, { id: 401, n: 6, name: 'Seaweed' }, { id: 1759, n: 1, name: 'Ball of wool' }],
    'Secrets of Amberfell': () => amberItems(''),
    'Wiz Kid': () => wizkidItems(),
    'Making History': () => mhItems(''),
  };
  // Per-quest, per-SECTION item lists: quest name -> fn(sectionTitle) -> list or null.
  const QG_SEC_REQ = {
    'Secrets of Amberfell': sec => amberItems(sec),
  };
  // Per-quest live progress monitor: quest name -> fn -> text (multi-line ok, '' = hidden).
  const QG_MON = {
    'Necromancy!': () => necroMonText(),
    'The Restless Ghost': () => rgMonText(),
    'Making History': () => mhMonText(),
    'New Foundations': () => nfMonText(),
    "There's No Place Like Home...": () => tnpMonText(),
    'Murder on the Border': () => motbMonText(),
  };
  function qgMonHtml(nm) {
    const f = QG_MON[nm]; if (!f) return '';
    let txt = ''; try { txt = f() || ''; } catch (e) {}
    if (!txt) return '';
    return '<div class="stor-h" style="margin-top:8px;">Monitor</div>' +
           '<div class="myst-tip" style="white-space:pre-line;font-family:monospace;">' + qgEsc(txt) + '</div>';
  }
  // Item row: {id,n,name} renders an icon + "n× name", {name} alone is plain text, and an
  // entry carrying a live backpack count ({..,have}) shows have/need instead of the count.
  function qgItemsRow(list) {
    let h = '<div style="display:flex;flex-wrap:wrap;gap:12px;padding:4px 0 2px;">';
    for (const it of list) {
      if (it.id) {
        let url = ''; try { url = bridge() ? bridge().itemIcon(it.id) : ''; } catch (e) {}   // itemIcon() returns the data URL synchronously
        const label = (it.have !== undefined)
          ? qgEsc(it.name) + ' <span style="color:' + (it.have >= it.n ? 'var(--ok)' : '#f1a64c') + ';">' + it.have + '/' + it.n + '</span>'
          : it.n + '× ' + qgEsc(it.name);
        h += '<span style="display:inline-flex;align-items:center;gap:5px;">' + (url ? '<img src="' + url + '" style="width:26px;height:26px;">' : '') + '<span>' + label + '</span></span>';
      } else {
        h += '<span style="display:inline-flex;align-items:center;opacity:.85;">' + qgEsc(it.name) + '</span>';
      }
    }
    return h + '</div>';
  }
  function qgReqList(nm) {
    let req = QG_REQ[nm];
    if (typeof req === 'function') { try { req = req(); } catch (e) { req = null; } }
    return (req && req.length) ? req : null;
  }
  function qgSecReqList(nm, sec) {
    const f = QG_SEC_REQ[nm]; if (!f) return null;
    let req = null; try { req = f(sec); } catch (e) {}
    return (req && req.length) ? req : null;
  }
  function qgReqHtml(nm) {
    const req = qgReqList(nm); if (!req) return '';
    return '<div class="stor-h" style="margin-top:8px;">Required items</div>' + qgItemsRow(req);
  }
  function qgGuideHtml(nm, g, done) {
    const manual = (questGSteps && questGSteps[nm]) || [];
    let i = 0, h = qgReqHtml(nm) + qgMonHtml(nm);
    for (const sec of g.sections) {
      const st = qgWiki(sec.t), sn = qgWiki(sec.n), sr = qgWiki(sec.r);
      if (st) h += '<div class="stor-h" style="margin-top:8px;">' + qgEsc(st) + '</div>';
      const sireq = qgSecReqList(nm, st);
      if (sireq) h += '<div class="myst-tip">Needed:</div>' + qgItemsRow(sireq);
      else if (sn) h += '<div class="myst-tip">Needed: ' + qgEsc(sn) + '</div>';
      if (sr) h += '<div class="myst-tip">Recommended: ' + qgEsc(sr) + '</div>';
      h += '<div class="myst-steps">';
      for (const s of sec.s) {
        const lines = s.split('\n').map(qgWiki).filter(l => l.trim());
        if (!lines.length) { i++; continue; }   // step was pure wiki markup -> hide it but keep the index stable
        const dn = done || manual.indexOf(i) >= 0 || (typeof qgAutoDone !== 'undefined' && qgAutoDone[nm] && qgAutoDone[nm].has(i));
        h += '<div class="myst-step' + (dn ? ' done' : '') + '" data-qn="' + qgEsc(nm) + '" data-i="' + i + '">' +
             '<span class="myst-cb"></span><span class="tx">' + qgChatHtml(lines[0]) +
             lines.slice(1).map(l => '<br><span style="opacity:.75;">' + qgChatHtml(l) + '</span>').join('') +
             '</span></div>';
        i++;
      }
      h += '</div>';
    }
    if (!done && manual.length) h += '<span class="myst-reset" data-qreset="' + qgEsc(nm) + '">Uncheck all</span>';
    return h;
  }
  function renderQuestFocus() {
    const c = paneRoot('questfocus'); if (!c) return;   // also reached from event handlers, outside shell dispatch
    let wrap = $('qfWrap');
    if (!wrap) {
      c.innerHTML = ''; wrap = document.createElement('div'); wrap.id = 'qfWrap'; wrap.className = 'pk-wrap'; c.appendChild(wrap); qgSig = '';
      wrap.addEventListener('click', e => {
        if (e.target.closest('#qfUnpin')) {
          setQuestFocus('');
          const ot = TABS.find(x => x.id === 'quests');
          if (ot) openTab(ot);
          return;
        }
        const st = e.target.closest('.myst-step');
        if (st && st.dataset.qn !== undefined) {
          qgToggleStep(st.dataset.qn, +st.dataset.i);
          qgSig = ''; renderQuestFocus();
          return;
        }
        const rs = e.target.closest('.myst-reset');
        if (rs && rs.dataset.qreset !== undefined) {
          if (questGSteps) { delete questGSteps[rs.dataset.qreset]; qgSave(); updateQuestHighlight(); }
          qgSig = ''; renderQuestFocus();
        }
      });
    }
    qgEnsureLoaded();
    const nm = qgFocusName();
    if (!nm) {
      const esig = 'empty';   // guard: don't rebuild every 250ms poll
      if (qgSig === esig && $('qfWrap')) return;
      qgSig = esig;
      wrap.innerHTML = '<div class="stor-empty">No focused quest. Open a quest in the Quests tab and pin it.</div>';
      return;
    }
    const g = questGuideFor(nm);
    const done = qgQuestDone(nm);
    const q = qgQuestByName(nm);
    const stTxt = (q && questsData) ? questPill(questsData.st[q.id], false)[1] : '';
    const sig = JSON.stringify([nm, done, stTxt, (questGSteps && questGSteps[nm]) || []]);
    if (sig === qgSig) return; qgSig = sig;
    let html = '<div class="arch-head"><span>' + qgEsc(nm) + '</span><span style="display:flex;align-items:center;gap:10px;">' +
               (stTxt ? '<span class="pet-st ' + (done ? 'q-done' : 'q-prog') + '">' + qgEsc(stTxt) + '</span>' : '') +
               '<span class="myst-reset" id="qfUnpin" style="padding:0;">Unpin</span></span></div>';
    html += '<div class="myst-det">' + (g ? qgGuideHtml(nm, g, done)
                : '<div class="myst-tip">No quick guide data for this quest.</div>') + '</div>';
    wrap.innerHTML = html;
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { qgEnsureLoaded, qgFocusName, qgGuideHtml, qgSave, qgToggleStep, questGuideFor, questGuidesReady, renderQuestFocus, setQuestFocus, updateQuestHighlight });
registerTab({ id: 'questfocus', render: renderQuestFocus, open: function () { qgSig = ''; fetchQuests(true); qgEnsureLoaded(); } });
})();
