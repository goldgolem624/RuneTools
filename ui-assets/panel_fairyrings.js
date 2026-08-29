// RuneToolsX panel: Fairy Rings (every destination, its code, and whether YOU can use it).
// Spliced inline into client.html at load; IIFE (window exports + registerTab; see the RTX registry in client.html).
//
// All of it is the game's own data (CS2, build 949):
//   dbtable 121        the fairy ring travel log: col 1 = ring key, col 2 = code ("A I Q"),
//                      col 3 = destination text ("Asgarnia: Mudskipper Point"); keys 1000+ are
//                      the multi-hop quest destinations (Fairy Resistance HQ, Ork's Rift, Kethsi).
//   script 3101        ring key -> the "logged" varbit (1 once you have travelled there)
//   script 3104        ring key -> the requirement gate the game applies (quest progress vars);
//                      ported below verbatim, evaluated against live varbits/varps.
//   script 15809(20)   Fort Forinthry Grove tier (vb 52991), the gate for BJP.
// The human-readable requirement text names the quest each gate tests.
(function () {

  const FR_LOGGED = { 444: 12079, 1: 12082, 2: 12081, 3: 12080, 10: 12091, 11: 12094, 12: 12093, 13: 12092, 20: 12087, 21: 12090, 22: 12089, 23: 12088,
    30: 12083, 31: 12086, 32: 12085, 33: 12084, 100: 12127, 101: 12130, 102: 12129, 103: 12128, 110: 12139, 111: 12142, 112: 12141, 113: 12140,
    120: 12135, 121: 12138, 122: 12137, 123: 12136, 130: 12131, 131: 12134, 132: 12133, 133: 12132, 200: 12111, 201: 12114, 202: 12113, 203: 12112,
    210: 12123, 211: 12126, 212: 12125, 213: 12124, 220: 12119, 221: 12122, 222: 12121, 223: 12120, 230: 12115, 231: 12118, 232: 12117, 233: 12116,
    300: 12095, 301: 12098, 302: 12097, 303: 12096, 310: 12107, 311: 12110, 312: 12109, 313: 12108, 320: 12103, 321: 12106, 322: 12105, 323: 12104,
    330: 12099, 331: 12102, 332: 12101, 333: 12100, 1000: 12076, 1001: 9963, 1002: 11645 };
  // Requirement gates: [varbits needed], [varps needed], test(V, P, extra) -> true when usable, text.
  // V(id) = live varbit, P(id) = live varp; extra.staff = dramen/lunar staff item 9025 count in
  // the backpack (INV_TOTAL(93, 9025) in the script).
  const FR_GATES = {
    // req = the wiki's stated requirement + the exact game test (variable and threshold) that
    // the in-game ring log applies, so nothing here is "partial": it is the step the game checks.
    1:    { vb: [18021], test: (V) => V(18021) >= 225, req: 'The World Wakes completed [game test: vb 18021 >= 225]' },
    10:   { vb: [9928], test: (V) => !(V(9928) < 105 && V(9928) !== 95), req: "A Fairy Tale III: reached the Ork's Rift stage [vb 9928 >= 105, or = 95]" },
    33:   { vb: [11533], test: (V) => V(11533) >= 13, req: 'Death to the Dorgeshuun completed [vb 11533 >= 13]' },
    100:  { vb: [9928, 9939], test: (V) => !(V(9928) < 105 && (V(9928) !== 95 || V(9939) < 3)), req: "A Fairy Tale III: reached the Ork's Rift stage [vb 9928 >= 105, or = 95 with vb 9939 >= 3]" },
    111:  { vp: [2696], test: (V, P) => P(2696) > 100, req: 'In Search of the Myreque completed [varp 2696 > 100]' },
    123:  { vb: [11610], test: (V) => V(11610) >= 400, req: 'Ritual of the Mahjarrat completed [vb 11610 >= 400]' },
    131:  { vb: [25043, 23198], test: (V) => !(V(25043) === 0 || V(23198) < 400), req: "Plague's End completed [vb 23198 >= 400] and the Prifddinas ring unlocked [vb 25043 = 1]" },
    200:  { vp: [2262], test: (V, P) => P(2262) >= 9, req: 'The Fremennik Trials completed [varp 2262 >= 9]' },
    212:  { vb: [9928, 9937], test: (V) => !(V(9928) < 105 && (V(9928) !== 95 || V(9937) < 2)), req: "A Fairy Tale III: reached the Ork's Rift stage [vb 9928 >= 105, or = 95 with vb 9937 >= 2]" },
    223:  { vb: [36140], test: (V) => V(36140) >= 50, req: 'The Jack of Spades: Imperial district access [vb 36140 >= 50]' },
    321:  { vb: [60595], test: (V) => V(60595) >= 51, req: 'Visions of Havenhythe: shrine stage [vb 60595 >= 51]' },
    231:  { vb: [36856], test: (V) => V(36856) === 1, req: "Ring repaired with 5 bittercap mushrooms after starting Legends' Quest [object morph vb 36856 = 1]" },
    302:  { vb: [9929], test: (V) => V(9929) !== 0, req: 'A Fairy Tale III started: Fairy Very Wise met [npc morph vb 9929 != 0]' },
    313:  { vb: [13322], test: (V) => V(13322) >= 18, req: 'Land of the Goblins: Yu\'biusk reached [vb 13322 >= 18]' },
    330:  { vb: [52991], test: (V) => V(52991) >= 2, req: 'Fort Forinthry Grove built to tier 2 [object morph vb 52991 >= 2]' },
    331:  { vb: [30276], test: (V) => V(30276) !== 0, req: 'Ring repaired with bittercap mushrooms at The Lost Grove [object morph vb 30276 != 0]' },
    332:  { vp: [2330], test: (V, P) => P(2330) >= 8, req: 'Holy Grail: Fisher Realm reached [varp 2330 >= 8]' },
    333:  { vb: [12078], test: (V) => V(12078) !== 0, req: 'Ring repaired with bittercap mushrooms in the Ancient Cavern (Barbarian Training) [object morph vb 12078 != 0]' },
    110:  { vb: [52651], test: (V) => V(52651) >= 35, req: 'Secrets of Amberfell: Amberfell reached [vb 52651 >= 35]' },
    13:   { vb: [9663], test: (V) => V(9663) !== 0, req: 'Morytania ring log flag [vb 9663 != 0, named ach_if_it_bleeds in the script dump]' },
    322:  { vb: [9663], test: (V) => V(9663) !== 0, req: 'Morytania ring log flag [vb 9663 != 0, named ach_if_it_bleeds in the script dump]' },
    221:  { vb: [9663], test: (V) => V(9663) !== 0, req: 'Morytania ring log flag [vb 9663 != 0, named ach_if_it_bleeds in the script dump]' },
    1000: { vb: [12056, 9929, 12068, 12066], test: (V, P, x) => V(12056) >= 40 && !(x.staff === 0 && V(9929) < 2) && (V(12068) + V(12066)) === 5, req: 'A Fairy Tale II reached the Fairy Resistance stage, and a fairy staff in your backpack (or Fairy Very Wise met twice) [vb 12056 >= 40; item 9025 in backpack or vb 9929 >= 2; vb 12068 + vb 12066 = 5]' },
    20:   { vb: [61256], test: (V) => V(61256) !== 0, req: 'Fairy ring built in your house [vb 61256 != 0]' },
  };
  const FR_BASE_REQ = "Priest in Peril, Nature Spirit, Lost City and A Fairy Tale I completed, A Fairy Tale II: Cure a Queen started (wiki); the game gates each ring's log entry on the tests shown per row";
  let frUnused = 0;
  let frRows = null; let frVb = null; let frVp = null; let frStaff = null; let frFetching = false; let frFetchAt = 0; let frSig = ''; let frFilter = 'all'; frSearch = '';
  async function fetchFairyRings() {
    if (!bridge() || frFetching) return;
    const t = Date.now(); if (t - frFetchAt < 2000) return; frFetchAt = t; frFetching = true;
    try {
      if (!frRows && bridge().dbRows) {
        const rows = JSON.parse(await bridge().dbRows(121) || 'null');
        if (Array.isArray(rows) && rows.length) {
          frRows = rows.map(r => ({ key: (r.i && r.i['1'] && r.i['1'][0]) | 0, code: (r.s && r.s['2'] && r.s['2'][0]) || '', dest: (r.s && r.s['3'] && r.s['3'][0]) || '' }))
                       .filter(r => r.code);
          // Codes with no destination text are the game's UNUSED combinations (9 of the 64,
          // e.g. AKR, AJP, DIQ); they are not places, so they are counted, not listed.
          frUnused = frRows.filter(r => !r.dest).length;
          frRows = frRows.filter(r => r.dest);
        }
      }
      const vbs = new Set(Object.values(FR_LOGGED)); for (const k in FR_GATES) for (const v of (FR_GATES[k].vb || [])) vbs.add(v);
      const vps = new Set(); for (const k in FR_GATES) for (const v of (FR_GATES[k].vp || [])) vps.add(v);
      if (bridge().varbits) { const vb = JSON.parse(await bridge().varbits(myPid(), [...vbs].join(',')) || 'null'); if (vb && Object.keys(vb).length) frVb = vb; }
      if (bridge().varps) { const vp = JSON.parse(await bridge().varps(myPid(), [...vps].join(',')) || 'null'); if (vp && Object.keys(vp).length) frVp = vp; }
      if (bridge().containerItems) {
        try { const inv = JSON.parse(await bridge().containerItems(myPid(), 93) || 'null'); frStaff = (inv && Array.isArray(inv.items)) ? inv.items.filter(it => it && it[1] === 9025).reduce((s, it) => s + (it[2] > 0 ? it[2] : 1), 0) : null; } catch (e) {}
      }
    } catch (e) {}
    frFetching = false;
    paneRun('fairyrings', renderFairyRings);
  }
  function frState(r) {
    const V = id => (frVb && frVb[String(id)] !== undefined) ? (frVb[String(id)] | 0) : 0;
    const P = id => (frVp && frVp[String(id)] !== undefined) ? (frVp[String(id)] | 0) : 0;
    const g = FR_GATES[r.key];
    const ok = g ? !!g.test(V, P, { staff: frStaff === null ? 0 : frStaff }) : true;
    const logged = FR_LOGGED[r.key] !== undefined ? V(FR_LOGGED[r.key]) === 1 : false;
    return { ok, logged, req: g ? g.req : '' };
  }
  function frCoord(code) {
    // Coordinates only from the world map's icon binding (fairy network); never typed here.
    try {
      if (typeof wmBuildStops !== 'function') return null;
      wmBuildStops();
      if (!wmStopIdx) return null;
      const c3 = code.replace(/\s+/g, '').toUpperCase();
      for (const [k, rec] of wmStopIdx) if (k.startsWith('fairy_ring|') && rec.stop.name.toUpperCase().startsWith(c3 + ' ') && rec.ml >= 0) return { x: rec.x, y: rec.y, p: rec.p };
    } catch (e) {}
    return null;
  }
  function renderFairyRings() {
    const c = $('content');
    let wrap = $('frWrap');
    if (!wrap) {
      injectStyle('frCss', `
        .fr-top { display: flex; align-items: center; gap: 8px; margin: 2px 12px 6px; flex-wrap: wrap; }
        .fr-top input { flex: 1 1 160px; min-width: 120px; }
        .fr-cnt { color: var(--text-dim); font-size: 11px; margin-left: auto; font-variant-numeric: tabular-nums; }
        .fr-card { margin: 0 12px 10px; background: var(--bg-elev); border: 1px solid var(--border); border-radius: 10px; }
        .fr-row { display: flex; align-items: center; gap: 10px; padding: 7px 12px; }
        .fr-row + .fr-row { border-top: 1px solid var(--border); }
        .fr-code { font: 700 12px/1.3 Consolas, monospace; letter-spacing: .12em; color: var(--accent-hi); min-width: 46px; max-width: 120px; white-space: normal; }
        .fr-row.no .fr-code { color: var(--text-mute); }
        .fr-nm { flex: 1; min-width: 0; color: var(--text); font-size: 12px; }
        .fr-row.no .fr-nm { color: var(--text-dim); }
        .fr-sub { color: var(--text-mute); font-size: 10.5px; margin-top: 1px; }
        .fr-pill { font-size: 10.5px; padding: 2px 9px; border-radius: 999px; border: 1px solid var(--border); color: var(--text-dim); white-space: nowrap; }
        .fr-pill.ok { color: #4dd28a; border-color: rgba(77,210,138,.45); }
        .fr-pill.no { color: #e05656; border-color: rgba(224,86,86,.45); }
        .fr-pill.seen { color: var(--accent-hi); border-color: var(--accent-ring); }
        .fr-btn { font-size: 10.5px; padding: 2px 9px; border-radius: 999px; border: 1px solid var(--border); background: transparent; color: var(--text-dim); cursor: pointer; white-space: nowrap; }
        .fr-btn:hover { color: var(--text); border-color: var(--accent-hi); }
        .fr-btn.on { color: var(--accent-hi); border-color: var(--accent-hi); }
        .fr-note { margin: 6px 14px 10px; color: var(--text-mute); font-size: 11px; line-height: 1.45; }
        .fr-empty { margin: 10px 14px; color: var(--text-dim); font-size: 12px; }`);
      c.innerHTML = ''; frSig = '';
      wrap = document.createElement('div'); wrap.id = 'frWrap'; wrap.className = 'pane'; c.appendChild(wrap);
      wrap.addEventListener('click', ev => {
        const b = ev.target.closest ? ev.target.closest('[data-fr-f]') : null;
        if (b) { frFilter = b.dataset.frF; frSig = ''; renderFairyRings(); return; }
        const m = ev.target.closest ? ev.target.closest('[data-fr-map]') : null;
        if (m) {
          const k = m.dataset.frMap; const r = frRows.find(x => String(x.key) === k); const co = r ? frCoord(r.code) : null;
          if (!co) return;
          try { const t = allTabs().find(x => x.id === 'worldmap'); if (t) openTab(t); } catch (e) {}
          setTimeout(() => { try { wmFlyTo(co.x, co.y, co.p, { x: co.x, y: co.y, p: co.p, nm: r.code.trim().split(/\s+/).join('') + ' ' + r.dest }); } catch (e) {} }, 60);
        }
      });
      wrap.addEventListener('input', ev => { if (ev.target.id === 'frSearch') { frSearch = ev.target.value; frSig = ''; renderFairyRings(); } });
    }
    if (!frRows) { wrap.innerHTML = '<div class="fr-empty">Reading the fairy ring log from the cache...</div>'; frSig = ''; return; }
    const states = frRows.map(r => frState(r));
    const sig = frFilter + '|' + frSearch + '|' + states.map(s => (s.ok ? 1 : 0) + (s.logged ? 2 : 0)).join('') + '|' + (frVb ? 1 : 0);
    if (sig === frSig) return; frSig = sig;
    const esc = x => String(x).replace(/&/g, '&amp;').replace(/</g, '&lt;');
    const q = frSearch.trim().toLowerCase();
    let usable = 0, seen = 0; const rows = [];
    frRows.forEach((r, i) => {
      const s = states[i]; if (s.ok) usable++; if (s.logged) seen++;
      if (frFilter === 'usable' && !s.ok) return; if (frFilter === 'locked' && s.ok) return; if (frFilter === 'unvisited' && s.logged) return;
      // "A I Q" -> "AIQ"; multi-hop sequences ("AIR DLR DJQ AJS") keep their hops, arrowed.
      const groups = r.code.trim().split(/\s+/);
      const code = groups.length > 3 ? groups.join(' → ') : groups.join('');
      const reqPlain = s.req ? s.req.replace(/\s*\[[^\]]*\]\s*/g, ' ').replace(/\s+/g, ' ').trim() : '';
      const reqTest = s.req ? (s.req.match(/\[([^\]]*)\]/g) || []).map(x => x.slice(1, -1)).join('; ') : '';
      if (q && (groups.join('') + ' ' + code + ' ' + r.dest).toLowerCase().indexOf(q) < 0) return;
      const parts = r.dest.split(': '); const region = parts.length > 1 ? parts[0] : ''; const place = parts.length > 1 ? parts.slice(1).join(': ') : r.dest;
      const co = frCoord(r.code);
      rows.push('<div class="fr-row ' + (s.ok ? 'ok' : 'no') + '"><span class="fr-code">' + esc(code) + '</span>'
        + '<div class="fr-nm">' + esc(place || '(unnamed)') + '<div class="fr-sub">' + esc(region || '') + (reqPlain ? (region ? ' · ' : '') + '<span title="Game test: ' + esc(reqTest) + '">Requires: ' + esc(reqPlain) + '</span>' : '') + '</div></div>'
        + (s.logged ? '<span class="fr-pill seen" title="You have travelled here (game log)">visited</span>' : '')
        + '<span class="fr-pill ' + (s.ok ? 'ok' : 'no') + '">' + (s.ok ? 'usable' : 'locked') + '</span>'
        + (co ? '<button class="fr-btn" data-fr-map="' + r.key + '">Map</button>' : '') + '</div>');
    });
    let h = '<div class="fr-top"><input id="frSearch" class="bank-search" type="text" placeholder="Search code or destination..." value="' + esc(frSearch) + '" spellcheck="false">'
      + ['all', 'usable', 'locked', 'unvisited'].map(f => '<button class="fr-btn' + (frFilter === f ? ' on' : '') + '" data-fr-f="' + f + '">' + f[0].toUpperCase() + f.slice(1) + '</button>').join('')
      + '<span class="fr-cnt">' + usable + ' / ' + frRows.length + ' usable · ' + seen + ' visited' + (frUnused ? ' · ' + frUnused + ' unused codes' : '') + '</span></div>';
    h += '<div class="fr-card">' + (rows.length ? rows.join('') : '<div class="fr-empty">Nothing matches.</div>') + '</div>';
    wrap.innerHTML = h;
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { fetchFairyRings });
registerTab({ id: 'fairyrings', render: renderFairyRings, open: function () { frSig = ''; frFetchAt = 0; fetchFairyRings(); } });
})();
