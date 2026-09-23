// RuneToolsX panel: Quest quick guides + Focused Quest tab.
(function () {

  questGSteps = null;            // {"<quest name>":[stepIdx,..], "__focus": name}
  let qgFetching = false; qgSig = '';
  let _qgByLower = null;
  let qgDataState = 0;              // 0 = untried, 1 = loading, 2 = done (or failed)
  function questGuidesReady() { return qgDataState === 2; }
  async function questGuidesLoad() {
    if (qgDataState) return qgDataState === 2;
    qgDataState = 1;
    try {
      const txt = (bridge() && bridge().uiAsset) ? await rtxData.raw('host.uiAsset', 'quest_guides.js') : '';
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
    const guide = g => (g && Array.isArray(g.sections)) ? g : null;   // anything else is not guide text
    if (guide(G[name])) return G[name];
    if (!_qgByLower) { _qgByLower = {}; for (const k in G) _qgByLower[k.toLowerCase()] = G[k]; }
    return guide(_qgByLower[name.toLowerCase()]) ||
           guide(_qgByLower[(name + ' (miniquest)').toLowerCase()]) || null;
  }
  let qgLoadedPid = -1;
  async function qgEnsureLoaded() {
    if ((questGSteps !== null && qgLoadedPid === myPid()) || qgFetching || !bridge() || !bridge().questLoad) return;
    qgFetching = true;
    qgLoadedPid = myPid();
    try { const d = JSON.parse(await rtxData.raw('host.questLoad')); questGSteps = (d && typeof d === 'object') ? d : {}; }
    catch (e) { questGSteps = {}; }
    qgFetching = false;
    updateQuestHighlight();
    paneRun('questfocus', renderQuestFocus);
    if (paneVisible('quests')) { questDetailSig = ''; renderQuests(); }
  }
  function qgSave() {
    if (qgLoadedPid !== myPid()) return;   // never write one character's progress to another
    try { rtxData.sync('act.questSave', JSON.stringify(questGSteps || {})); } catch (e) {}
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
  function updateQuestHighlight() {}
  function qgToggleStep(nm, idx) {
    if (questGSteps === null) questGSteps = {};
    const arr = (questGSteps[nm] = questGSteps[nm] || []);
    const p = arr.indexOf(idx);
    if (p >= 0) arr.splice(p, 1); else arr.push(idx);
    qgSave(); updateQuestHighlight();
  }
  function qgEsc(s) { return htmlEsc(s); }
  // Chat option markers as the source pages write them: a number is the option to pick, ? and # mean
  // the number changes with the state of the conversation, ~ means any option will do.
  function qgChatOpt(raw) {
    const o = String(raw || '').trim();
    let m = o.match(/^(\d+)\s*\.?\s*(.*)$/);
    if (m && m[2]) return { n: m[1], t: m[2] };
    m = o.match(/^([?#~])\s*(.*)$/);
    if (m) return { n: m[1] === '~' ? 'Any' : 'Varies', t: m[2] || '' };
    return { n: '·', t: o };
  }
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
          const c = qgChatOpt(o);
          return '<span class="qg-copt"><span class="qg-cnum' + (c.n === '·' ? '' : ' qg-cvar') + '">' +
                 qgEsc(c.n) + '</span><span class="qg-ctxt">' + qgEsc(c.t) + '</span></span>';
        }).join('') + '</span>';
      }
    }
    return h;
  }
  // Live option numbers. A guide can only print the number the source page saw: the game renumbers
  // the list as the conversation changes, which is what "Varies" means. While an option-select
  // dialogue is open, match each printed option against the live list by its text and show the
  // number the player actually has to press.
  let qgDlgOpts = [];            // [{n, key}] for the dialogue currently open
  let qgDlgSig = '';
  let qgDlgBusy = false;
  function qgOptKey(t) {
    return String(t || '').toLowerCase()
      .replace(/[‘’]/g, "'").replace(/[“”]/g, '"')
      .replace(/\[?player name\]?/g, '').replace(/[^a-z0-9' ]+/g, ' ')
      .replace(/\s+/g, ' ').trim();
  }
  function qgApplyLiveNums(root) {
    const host = root || document;
    const opts = host.querySelectorAll ? host.querySelectorAll('.qg-copt') : [];
    for (const el of opts) {
      const num = el.querySelector('.qg-cnum'), txt = el.querySelector('.qg-ctxt');
      if (!num || !txt) continue;
      if (!num.dataset.qgBase) num.dataset.qgBase = num.textContent;
      const k = qgOptKey(txt.textContent);
      let hit = null;
      if (k) for (const o of qgDlgOpts) {
        if (o.key === k) { hit = o; break; }
        if (!hit && o.key && (o.key.indexOf(k) === 0 || k.indexOf(o.key) === 0)) hit = o;
      }
      if (hit) {
        num.textContent = String(hit.n);
        num.classList.remove('qg-cvar');
        num.classList.add('qg-clive');
        el.classList.add('qg-copt-live');
      } else if (num.classList.contains('qg-clive')) {
        num.textContent = num.dataset.qgBase;
        num.classList.remove('qg-clive');
        el.classList.remove('qg-copt-live');
        if (num.textContent === 'Varies' || num.textContent === 'Any') num.classList.add('qg-cvar');
      }
    }
  }
  function qgDlgRefresh() {
    if (qgDlgBusy) return;
    if (!paneVisible('quests') && !paneVisible('questfocus')) return;
    if (!bridge() || !bridge().dialog) return;
    qgDlgBusy = true;
    (async () => {
      try {
        const d = JSON.parse(await bridge().dialog(myPid()) || '{}');
        const list = (d && Array.isArray(d.options)) ? d.options : [];
        const next = list.map(o => ({ n: o.n, key: qgOptKey(o.text) })).filter(o => o.key);
        const sig = next.map(o => o.n + ':' + o.key).join('|');
        if (sig !== qgDlgSig) { qgDlgSig = sig; qgDlgOpts = next; qgApplyLiveNums(); }
      } catch (e) {}
      qgDlgBusy = false;
    })();
  }
  (function () { function loop() { try { qgDlgRefresh(); } catch (e) {} setTimeout(loop, 700); } setTimeout(loop, 1500); })();
  function qgWiki(s) {
    s = String(s || '');
    let prev;
    do { prev = s; s = s.replace(/\{\{[^{}]*\}\}/g, ''); } while (s !== prev);
    s = s.replace(/\{\{\s*[Cc]hecklist\b[\s|;*]*/g, '');
    s = s.replace(/\{\{[\s\S]*$/g, '');                    // unmatched opener -> drop marker + its params
    s = s.replace(/\}\}/g, '');
    // Wikilinks, including the truncated "[[page]" the extractor sometimes leaves behind.
    s = s.replace(/\[\[([^\[\]|]*)\|([^\[\]]*?)\]?\]?/g, '$2');
    s = s.replace(/\[\[([^\[\]]*?)\]?\]?/g, '$1');
    return s.replace(/[ \t]{2,}/g, ' ').trim();
  }
  // A few quick guides carry an inline wikitable ({| ... |}) that the extractor left as raw markup:
  // recipe/lookup tables the player actually needs. Parse them instead of printing the source.
  const QG_HL = '@@HL@@';                    // marks a cell the wiki highlighted (i.e. the answers)
  function qgTableRows(body) {
    body = body.replace(/(\|\|?)\s*class\s*=\s*"[^"]*table-bg-[^"]*"\s*\|/g, '$1' + QG_HL);
    body = body.replace(/\b[A-Za-z-]+\s*=\s*"[^"]*"/g, ' ')      // class="wikitable"
               .replace(/\b[A-Za-z-]+\s*=\s*"[^"|!]*/g, ' ');    // unterminated: class="wikitable ...
    const rows = [];
    for (const raw of body.split(/\|-+/)) {
      const r = raw.trim(); if (!r) continue;
      const hdr = r.charAt(0) === '!';
      const cells = r.split(hdr ? /!!|\|\||[!|]/ : /\|\||\|/).map(c => {
        const t = c.trim();
        return t.indexOf(QG_HL) === 0 ? { t: t.slice(QG_HL.length).trim(), hl: true } : { t: t, hl: false };
      });
      while (cells.length && !cells[0].t) cells.shift();
      while (cells.length && !cells[cells.length - 1].t) cells.pop();
      if (cells.length) rows.push({ hdr: hdr, cells: cells });
    }
    if (!rows.length) return null;
    let n = 0; for (const r of rows) n = Math.max(n, r.cells.length);
    const data = rows.filter(r => !r.hdr), src = data.length ? data : rows;
    const keep = [];                             // drop columns that are empty in every data row
    for (let i = 0; i < n; i++) if (src.some(r => r.cells[i] && r.cells[i].t)) keep.push(i);
    if (!keep.length) return null;
    return rows.map(r => ({ hdr: r.hdr, cells: keep.map(i => r.cells[i] || { t: '', hl: false }) }));
  }
  function qgTableHtml(body) {
    const rows = qgTableRows(body); if (!rows) return '';
    let h = '<table class="qg-tbl">';
    for (const r of rows) {
      h += '<tr>';
      for (const c of r.cells) {
        h += r.hdr ? '<th>' + qgEsc(c.t) + '</th>'
                   : '<td' + (c.hl ? ' class="hl"' : '') + '>' + qgEsc(c.t) + '</td>';
      }
      h += '</tr>';
    }
    return h + '</table>';
  }
  // Lift every table out of a line -> { text, html }; the ":" that introduced it goes with it.
  function qgSplitTables(line) {
    let html = '';
    const text = String(line).replace(/\{\|([\s\S]*?)\|\}/g, function (m, body) {
      html += qgTableHtml(body); return ' ';
    });
    return { text: text.replace(/[ \t]{2,}/g, ' ').replace(/\s*:\s*$/, '').trim(), html: html };
  }
  const QG_REQ = {
    'Hermit Permits': [{ id: 954, n: 3, name: 'Rope' }, { id: 401, n: 6, name: 'Seaweed' }, { id: 1759, n: 1, name: 'Ball of wool' }],
    'Secrets of Amberfell': () => amberItems(''),
    'Wiz Kid': () => wizkidItems(),
    'Making History': () => mhItems(''),
  };
  const QG_SEC_REQ = {
    'Secrets of Amberfell': sec => amberItems(sec),
  };
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
  function qgItemsRow(list) {
    let h = '<div style="display:flex;flex-wrap:wrap;gap:12px;padding:4px 0 2px;">';
    for (const it of list) {
      if (it.id) {
        let url = ''; try { url = bridge() ? rtxData.sync('cache.itemIcon', it.id) : ''; } catch (e) {}   // itemIcon() returns the data URL synchronously
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
        let tbl = '';
        const lines = s.split('\n').map(qgWiki).map(l => {
          const r = qgSplitTables(l); tbl += r.html; return r.text;
        }).filter(l => l.trim());
        if (!lines.length && !tbl) { i++; continue; }   // pure wiki markup -> hide it, keep the index stable
        const dn = done || manual.indexOf(i) >= 0 || (typeof qgAutoDone !== 'undefined' && qgAutoDone[nm] && qgAutoDone[nm].has(i));
        h += '<div class="myst-step' + (dn ? ' done' : '') + '" data-qn="' + qgEsc(nm) + '" data-i="' + i + '">' +
             '<span class="myst-cb"></span><span class="tx">' + qgChatHtml(lines[0]) +
             lines.slice(1).map(l => '<br><span style="opacity:.75;">' + qgChatHtml(l) + '</span>').join('') +
             tbl + '</span></div>';
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
    qgApplyLiveNums(wrap);
  }

Object.assign(window, { qgApplyLiveNums, qgEnsureLoaded, qgFocusName, qgGuideHtml, qgSave, qgToggleStep, questGuideFor, questGuidesReady, renderQuestFocus, setQuestFocus, updateQuestHighlight });
registerTab({ id: 'questfocus', render: renderQuestFocus, open: function () { qgSig = ''; fetchQuests(true); qgEnsureLoaded(); } });
})();
