// RuneToolsX panel: Globetrotter outfit guide + clue scroll list/solver UI.
// Spliced inline into client.html; IIFE (window exports + registerTab; see the RTX registry in client.html).
(function () {

  // Worn pieces are detected from the equipment container (94) by item name, so every tier variant
  // counts.
  const GLOBETROTTER = [
    { key: 'jacket',     name: 'Jacket',     cost: '1,500', effect: 'Teleport directly to the clue scroll location.', charges: 'Up to 3 charges (+1 every 5 trails).' },
    { key: 'shorts',     name: 'Shorts',     cost: '1,000', effect: 'Automatically perform the correct emote for emote clues.', charges: 'Unlimited.' },
    { key: 'boots',      name: 'Boots',      cost: '500',   effect: 'Unlimited run energy for a random duration.', charges: 'Up to 3 charges (+1 every 2 trails).' },
    { key: 'arm guards', name: 'Arm guards', cost: '750',   effect: 'Use teleport scrolls stored within the arm guards.', charges: 'Up to 1,000 of each teleport scroll.' },
    { key: 'backpack',   name: 'Backpack',   cost: '1,200', effect: 'Exchange your current clue for a new one of the same difficulty.', charges: 'Up to 3 charges (+1 every 5 trails).' },
  ];
  const GLOBETROTTER_SET = [
    'Acts as the emote-clue outfit when the matching hidey-hole is filled.',
    '2 pieces: +1 reward reroll storage per tier (total 4).',
    '4 pieces: +1 more reroll storage per tier (total 5).',
    '5 pieces: clues per outfit-ability charge reduced by 1, and +1 charge stored in each piece.',
  ];
  let gtWorn = {}, gtFetching = false, gtExpanded = false;
  const GT_NAMES = {};
  // Live charge state (CS2 script10761 tooltip math): cap = 3 + set flag, where the set flag is
  // vb39460+39461+39462+39463+39464 >= 5 (script3862); trails per charge = 5 (jacket/backpack) or
  // 2 (boots) minus the set flag; unlimited charges when vp12314 > 0 and vb58456 > 0 (script20152).
  const GT_CHARGE_VBS = { jacket: [39468, 39465, 5], backpack: [39469, 39466, 5], boots: [39470, 39467, 2] };  // [charges vb, progress vb, base trails/charge]
  let gtVb = null, gtVp = null, gtChFetching = false, gtChargesAt = 0, gtWornAt = 0;
  async function fetchGtCharges() {
    if (!bridge() || !bridge().varbits || gtChFetching) return; gtChFetching = true;
    try {
      let vb = null; try { vb = JSON.parse(await rtxData.raw('state.varbitsCsv', '39460,39461,39462,39463,39464,39465,39466,39467,39468,39469,39470,58456') || 'null'); } catch (e) {}
      let vp = null; try { vp = JSON.parse(await rtxData.raw('state.varps', String(typeof VP !== 'undefined' ? VP.LEAGUE : 12314)) || 'null'); } catch (e) {}
      if (vb) { gtVb = vb; gtVp = vp; }
    } finally { gtChFetching = false; }
    paneRun('globetrotter', renderGlobetrotter);
  }
  async function fetchGlobetrotter() {
    if (!bridge() || gtFetching) return; gtFetching = true;
    try {
      let eq = null; try { eq = JSON.parse(await rtxData.raw('state.container', 94)); } catch (e) {}
      const worn = {}, list = (eq && Array.isArray(eq.items)) ? eq.items : [];
      for (const it of list) {
        const id = Array.isArray(it) ? (it[1] | 0) : 0; if (id <= 0) continue;
        let nm = GT_NAMES[id];
        if (nm === undefined) { try { nm = (JSON.parse(await rtxData.raw('cache.itemInfo', id)).name) || ''; } catch (e) { nm = ''; } GT_NAMES[id] = nm; }
        if (/globetrotter/i.test(nm)) { const low = nm.toLowerCase(); for (const g of GLOBETROTTER) if (low.indexOf(g.key) >= 0) { worn[g.key] = nm; break; } }
      }
      gtWorn = worn;
    } finally { gtFetching = false; }
    paneRun('globetrotter', renderGlobetrotter);
  }
  function renderGlobetrotterTab() {
    const c = $('content');
    if (!$('gtPanel')) {
      c.innerHTML = '';
      const wrap = document.createElement('div'); wrap.className = 'pk-wrap'; c.appendChild(wrap);
      const gt = document.createElement('div'); gt.id = 'gtPanel'; gt.style.cssText = 'padding:8px 10px;border-radius:8px;background:rgba(255,255,255,0.04);border:1px solid rgba(255,255,255,0.1)'; wrap.appendChild(gt);
      gt.addEventListener('click', e => { if (e.target.closest('#gtHead')) { gtExpanded = !gtExpanded; renderGlobetrotter(); } });
    }
    // The tab renders on the 250ms loop; the bridge reads are throttled (worn = container scan,
    // charges = varbits).
    const now = Date.now();
    if (now - gtWornAt > 4000) { gtWornAt = now; fetchGlobetrotter(); }
    if (now - gtChargesAt > 1500) { gtChargesAt = now; fetchGtCharges(); }
    renderGlobetrotter();
  }
  let clueStatsVp = {}, clueStatsSig = '';
  async function renderClueStats() {
    const c = $('content');
    let wrap = $('clueStatsWrap');
    if (!wrap) { c.innerHTML = ''; wrap = document.createElement('div'); wrap.id = 'clueStatsWrap'; wrap.className = 'pk-wrap'; wrap.style.padding = '2px'; c.appendChild(wrap); }
    try { if (bridge() && bridge().varps) clueStatsVp = JSON.parse(await rtxData.raw('state.varps', '7802,7803,7804,7805,7806')); } catch (e) {}
    const tiers = [['Easy', 7802, '#c3b3c3'], ['Medium', 7803, '#deb46c'], ['Hard', 7804, '#dad3d1'], ['Elite', 7805, '#ffff57'], ['Master', 7806, '#67d4ff']];
    const sig = tiers.map(t => clueStatsVp[t[1]] | 0).join(',');
    if (sig === clueStatsSig && wrap._h) return; clueStatsSig = sig;
    let total = 0; for (const t of tiers) total += (clueStatsVp[t[1]] | 0);
    let html = '<div style="font-weight:600;font-size:14px;margin:2px 2px 10px">Treasure Trail completions</div>';
    for (const [name, vid, col] of tiers) {
      html += '<div style="display:flex;justify-content:space-between;align-items:center;padding:9px 12px;border-radius:8px;background:rgba(255,255,255,0.04);border:1px solid rgba(255,255,255,0.08);margin-bottom:6px">'
        + '<b style="color:' + col + '">' + name + '</b><span style="font-weight:700;font-size:15px">' + (clueStatsVp[vid] | 0) + '</span></div>';
    }
    html += '<div style="display:flex;justify-content:space-between;align-items:center;padding:9px 12px;margin-top:4px;border-top:1px solid rgba(255,255,255,0.12)"><b style="opacity:0.7">Total</b><span style="font-weight:700;font-size:15px">' + total + '</span></div>';
    wrap._h = html; wrap.innerHTML = html;
  }
  function renderGlobetrotter() {
    const el = $('gtPanel'); if (!el) return;
    const n = Object.keys(gtWorn).length;
    const esc = s => String(s).replace(/[&<>]/g, x => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;' }[x]));
    const gv = id => (gtVb ? (gtVb[id] | 0) : 0);
    const haveCh = !!gtVb;
    const setFlag = ((gv(39460) + gv(39461) + gv(39462) + gv(39463) + gv(39464)) >= 5) ? 1 : 0;
    const unlimited = haveCh && gtVp && ((gtVp[typeof VP !== 'undefined' ? VP.LEAGUE : 12314] | 0) > 0) && (gv(58456) > 0);
    const chargeCap = 3 + setFlag;
    let h = '<div id="gtHead" style="display:flex;align-items:center;justify-content:space-between;cursor:pointer;gap:8px">'
      + '<span style="font-weight:600;font-size:13px">Globetrotter outfit</span>'
      + '<span style="display:flex;align-items:center;gap:8px"><span style="font-size:12px;color:' + (n === 5 ? '#5fd07a' : (n ? '#c9a253' : '#9aa0ad')) + '">' + n + ' / 5 worn</span>'
      + '<span style="opacity:0.6;font-size:11px">' + (gtExpanded ? '▾' : '▸') + '</span></span></div>';
    h += '<div style="display:flex;gap:5px;margin-top:7px">';
    for (const g of GLOBETROTTER) { const on = !!gtWorn[g.key];
      h += '<span style="flex:1;text-align:center;font-size:10px;padding:3px 2px;border-radius:5px;border:1px solid ' + (on ? 'rgba(95,208,122,0.5)' : 'rgba(255,255,255,0.12)') + ';background:' + (on ? 'rgba(95,208,122,0.14)' : 'rgba(255,255,255,0.03)') + ';color:' + (on ? '#7fe0a0' : '#9aa0ad') + '">' + esc(g.name) + '</span>'; }
    h += '</div>';
    if (haveCh) {   // live charge summary, visible even when collapsed
      const seg = [];
      for (const g of GLOBETROTTER) { const ch = GT_CHARGE_VBS[g.key]; if (!ch) continue;
        const v = gv(ch[0]), full = unlimited || v >= chargeCap;
        seg.push(esc(g.name) + ' <b style="color:' + (full ? '#5fd07a' : (v > 0 ? '#c9a253' : '#9aa0ad')) + '">' + (unlimited ? '&#8734;' : v + '/' + chargeCap) + '</b>'); }
      h += '<div style="margin-top:6px;font-size:11px;opacity:0.9">Charges: ' + seg.join(' &middot; ') + '</div>';
    }
    if (gtExpanded) {
      h += '<div style="margin-top:8px;display:flex;flex-direction:column;gap:6px">';
      for (const g of GLOBETROTTER) { const on = !!gtWorn[g.key];
        const ch = GT_CHARGE_VBS[g.key];
        let chLine = esc(g.charges);
        if (ch && haveCh) {
          chLine = unlimited ? 'Charges: <b style="color:#5fd07a">Unlimited!</b>'
            : 'Charges: <b style="color:' + (gv(ch[0]) >= chargeCap ? '#5fd07a' : '#c9a253') + '">' + gv(ch[0]) + '/' + chargeCap + '</b>'
              + ' &middot; Next charge: ' + gv(ch[1]) + '/' + (ch[2] - setFlag) + ' trails';
        }
        h += '<div style="padding:6px 8px;border-radius:6px;background:rgba(255,255,255,0.03);border:1px solid rgba(255,255,255,0.08)">'
          + '<div style="display:flex;justify-content:space-between"><b style="color:' + (on ? '#7fe0a0' : 'inherit') + '">Globetrotter ' + esc(g.name.toLowerCase()) + '</b><span style="opacity:0.55;font-size:11px">' + (on ? 'worn' : g.cost + ' pts') + '</span></div>'
          + '<div style="opacity:0.82;font-size:11px;margin-top:2px">' + esc(g.effect) + '</div>'
          + '<div style="opacity:0.7;font-size:10px;margin-top:1px">' + chLine + '</div>'
          + ((ch && haveCh) ? '<div style="opacity:0.45;font-size:10px;margin-top:1px">' + esc(g.charges) + '</div>' : '') + '</div>'; }
      h += '<div style="margin-top:2px;padding:6px 8px;border-radius:6px;background:rgba(120,180,255,0.07);border:1px solid rgba(120,180,255,0.16)"><b style="font-size:11px">Set effects</b><ul style="margin:3px 0 0;padding-left:16px;font-size:11px;line-height:1.45">';
      GLOBETROTTER_SET.forEach((s, i) => { const active = (i === 0) || (i === 1 && n >= 2) || (i === 2 && n >= 4) || (i === 3 && (haveCh ? setFlag === 1 : n >= 5));
        h += '<li style="opacity:' + (active ? '0.95' : '0.4') + ';color:' + (active && i > 0 ? '#7fb0ff' : 'inherit') + '">' + esc(s) + '</li>'; });
      h += '</ul></div></div>';
    }
    setHTML(el, h);
  }
  function renderClues() {
    const c = $('content');
    let wrap = $('clueWrap');
    if (!wrap) {
      c.innerHTML = ''; clueListSig = '';
      wrap = document.createElement('div'); wrap.id = 'clueWrap'; wrap.className = 'pk-wrap'; c.appendChild(wrap);
      // Live view = difficulty tabs + held-clue list. "Browse all" is a separate read-only database
      // mode that never drives the live solver / map / in-world marks.
      const tabs = document.createElement('div'); tabs.id = 'clueTabs'; tabs.style.cssText = 'display:flex;gap:4px;margin-bottom:7px;flex-wrap:wrap'; wrap.appendChild(tabs);
      tabs.addEventListener('click', e => {
        const t = e.target.closest('[data-tier]'); if (t) { clueLiveTier = +t.dataset.tier; clueRenderFocus(); return; }
        if (e.target.closest('#clueBrowseBtn')) { clueBrowse = true; clueListSig = ''; clueRenderFocus(); return; }
        if (e.target.closest('#clueBackBtn')) { clueBrowse = false; clueRenderFocus(); return; }
      });
      const heldList = document.createElement('div'); heldList.id = 'clueHeld'; heldList.style.cssText = 'margin-bottom:6px'; wrap.appendChild(heldList);
      heldList.addEventListener('click', e => {
        const r = e.target.closest('[data-hid]'); if (!r) return;
        const id = +r.dataset.hid; if (id === activeClueId) return;
        const pin = clueVarcPinned();   // the varc owns the compass/tetracompass choice, not the click
        if (pin >= 0 && pin !== id && clueVarcIsPair(id)) return;
        activeClueId = id; clueMapZoom = 1; clueMapPin = null; compassMapSig = ''; selectClue(); renderClueHeld();
      });
      const tb = document.createElement('div'); tb.id = 'clueToolbar'; tb.className = 'pet-toolbar';
      const mkchip = (label, grp, val, cur) => { const b = document.createElement('button'); b.className = 'pet-chip' + (val === cur ? ' on' : ''); b.textContent = label; b.dataset.grp = grp; b.dataset.val = val; return b; };
      const tierRow = document.createElement('div'); tierRow.className = 'pet-chips';
      tierRow.appendChild(mkchip('All', 'tier', '-1', String(clueFTier)));
      CLUE_TIERS.forEach((nm, i) => tierRow.appendChild(mkchip(nm, 'tier', String(i), String(clueFTier))));
      const actRow = document.createElement('div'); actRow.className = 'pet-chips';
      actRow.appendChild(mkchip('All', 'act', '', clueFAction));
      Object.keys(CLUE_ACT_LBL).forEach(k => actRow.appendChild(mkchip(CLUE_ACT_LBL[k], 'act', k, clueFAction)));
      const heldBtn = document.createElement('button'); heldBtn.id = 'clueHeldBtn'; heldBtn.className = 'pet-chip hh-totbtn' + (clueFHeld ? ' on' : ''); heldBtn.textContent = 'Held only';
      const search = document.createElement('input'); search.className = 'pet-search'; search.id = 'clueSearch'; search.placeholder = 'Search id or action...'; search.value = clueFSearch;
      tb.appendChild(tierRow); tb.appendChild(actRow); tb.appendChild(heldBtn); tb.appendChild(search); wrap.appendChild(tb);
      const cnt = document.createElement('div'); cnt.id = 'clueCnt'; cnt.className = 'pet-count'; wrap.appendChild(cnt);
      const cmp = document.createElement('div'); cmp.id = 'clueCompass'; cmp.style.cssText = 'display:none;font-size:12px;padding:8px 11px;margin:2px 0 6px;border-radius:8px;background:rgba(120,180,255,0.08);border:1px solid rgba(120,180,255,0.18);line-height:1.5'; wrap.appendChild(cmp);
      const pz = document.createElement('div'); pz.id = 'cluePuzzle'; pz.style.cssText = 'display:none;font-size:12px;padding:8px 11px;margin:2px 0 6px;border-radius:8px;background:rgba(120,180,255,0.08);border:1px solid rgba(120,180,255,0.18);line-height:1.5'; wrap.appendChild(pz);
      const em = document.createElement('div'); em.id = 'clueEmote'; em.style.cssText = 'display:none;font-size:12px;padding:8px 11px;margin:2px 0 6px;border-radius:8px;background:rgba(120,180,255,0.06);border:1px solid rgba(120,180,255,0.16);line-height:1.55'; wrap.appendChild(em);
      const kn = document.createElement('div'); kn.id = 'clueKnot'; kn.style.cssText = 'display:none;font-size:12px;padding:8px 11px;margin:2px 0 6px;border-radius:8px;background:rgba(120,180,255,0.08);border:1px solid rgba(120,180,255,0.18);line-height:1.5'; wrap.appendChild(kn);
      em.addEventListener('click', e => {
        if (e.target.closest('#emClear')) { clueDismissAuto(); return; }   // clear the sticky auto-identified clue
        const row = e.target.closest('[data-ei]'); if (row) { clueAuto = null; clueScrollSig = '-'; clueEmoteSel = +row.dataset.ei; renderEmotePanel(); return; }
        if (e.target.closest('#emHidey')) { clueHidey = !clueHidey; try { localStorage.setItem('rtxClueHidey', clueHidey ? '1' : '0'); } catch (e2) {} renderEmotePanel(); return; }
        if (e.target.closest('#emBack')) { clueAuto = null; clueScrollSig = '-'; clueEmoteSel = -1; renderEmotePanel(); return; }
      });
      em.addEventListener('input', e => { if (e.target.id === 'emSearch') { clueEmoteSearch = e.target.value.toLowerCase(); renderEmotePanel(); } });
      const mapWrap = document.createElement('div'); mapWrap.id = 'clueMapWrap'; mapWrap.className = 'clue-map-wrap'; mapWrap.style.display = 'none';
      const mapStage = document.createElement('div'); mapStage.className = 'clue-map-stage';
      const mapInner = document.createElement('div'); mapInner.id = 'clueMapInner'; mapInner.className = 'clue-map-inner';
      const cv = document.createElement('canvas'); cv.id = 'clueMapCanvas'; cv.width = 384; cv.height = 384; cv.className = 'clue-map'; mapInner.appendChild(cv);
      const mapPulse = document.createElement('div'); mapPulse.id = 'clueMapPulse'; mapPulse.className = 'clue-map-pulse'; mapPulse.style.display = 'none'; mapInner.appendChild(mapPulse);
      const mapLode = document.createElement('div'); mapLode.id = 'clueMapLode'; mapLode.className = 'clue-map-lode'; mapLode.style.display = 'none';
      mapLode.innerHTML = '<div class="cml-icon"></div><div class="cml-kb"></div>';
      mapInner.appendChild(mapLode);
      const mapTele = document.createElement('div'); mapTele.id = 'clueMapTele'; mapTele.className = 'clue-map-teles'; mapTele.style.display = 'none';
      mapInner.appendChild(mapTele);
      const mapRange = document.createElement('div'); mapRange.id = 'clueMapRange'; mapRange.className = 'clue-map-range'; mapRange.style.display = 'none'; mapRange.title = 'Scan range'; mapInner.appendChild(mapRange);
      const mapPlayer = document.createElement('div'); mapPlayer.id = 'clueMapPlayer'; mapPlayer.className = 'clue-map-player'; mapPlayer.style.display = 'none'; mapPlayer.title = 'You'; mapInner.appendChild(mapPlayer);
      const mapTip = document.createElement('div'); mapTip.id = 'clueMapTip';
      mapTip.style.cssText = 'display:none;position:absolute;z-index:30;pointer-events:none;background:rgba(12,14,20,0.95);color:#eaeef5;border:1px solid rgba(120,200,255,0.5);border-radius:6px;padding:4px 7px;font-size:11px;line-height:1.35;white-space:nowrap;box-shadow:0 3px 10px rgba(0,0,0,0.5)';
      mapInner.appendChild(mapTip);
      mapStage.appendChild(mapInner);
      clueMapBindZoom(mapStage);
      const mapTitle = document.createElement('div'); mapTitle.id = 'clueMapTitle'; mapTitle.style.cssText = 'display:none;font-weight:700;font-size:14px;text-align:center;margin:0 0 6px;color:#eaeef5';
      mapWrap.appendChild(mapTitle);                       // bold scan-area title, above the map (scan clues only)
      mapWrap.appendChild(mapStage);
      const mapCap = document.createElement('div'); mapCap.id = 'clueMapCap'; mapCap.className = 'clue-map-cap'; mapWrap.appendChild(mapCap);
      const mapFloors = document.createElement('div'); mapFloors.id = 'clueMapFloors'; mapFloors.style.cssText = 'display:none;gap:5px;align-items:center;margin-top:4px;font-size:11px;flex-wrap:wrap'; mapWrap.appendChild(mapFloors);
      // Manual floor switch for a multi-floor scan map. Clicking the floor you are already
      // viewing releases the pick and goes back to following the player's own floor.
      mapFloors.addEventListener('click', e => {
        const b = e.target.closest('button[data-floor]'); if (!b) return;
        const want = +b.dataset.floor;
        scanPlaneSel = (scanPlaneSel === want) ? null : want;
        scanPlaneSelFor = activeClueId;
        selectClue();
      });
      wrap.appendChild(mapWrap);
      const list = document.createElement('div'); list.id = 'clueList'; list.className = 'pet-list'; wrap.appendChild(list);
      tb.addEventListener('click', e => {
        if (e.target.closest('#clueHeldBtn')) { clueFHeld = !clueFHeld; heldBtn.classList.toggle('on', clueFHeld); clueListSig = ''; renderCluesList(); return; }
        const b = e.target.closest('.pet-chip'); if (!b) return;
        if (b.dataset.grp === 'tier') clueFTier = +b.dataset.val; else clueFAction = b.dataset.val;
        tb.querySelectorAll('.pet-chip[data-grp="' + b.dataset.grp + '"]').forEach(x => x.classList.toggle('on', x === b));
        clueListSig = ''; renderCluesList();
      });
      search.addEventListener('input', () => { clueFSearch = search.value.toLowerCase(); clueListSig = ''; renderCluesList(); });
      // Browse-all list is READ-ONLY: it must not touch the live held-clue solver / map / in-world marks.
      list.addEventListener('click', e => {
        if (clueBrowse) return;
        const r = e.target.closest('.pet-row'); if (!r || r.dataset.cid === undefined) return;
        const id = +r.dataset.cid, c = CLUE_DATA.find(z => z.i === id);
        if (!clueTile(c) && !(c && c.a === 'emote')) return;   // selectable if it has a tile, or is an emote clue (hidey-hole reference)
        activeClueId = (activeClueId === id) ? -1 : id;
        clueMapZoom = 1; clueMapPin = null; compassMapSig = ''; selectClue(); clueListSig = ''; renderCluesList();
      });
    }
    clueVarcRoute();   // varc 1323 decides whether the compass clue or the tetracompass is showing
    clueRenderFocus();
    compassTick();   // poll the compass needle while this page is open; drives the banner + dig tile
    tetraTick();     // powered tetracompass: shares the compass varc, marks its own dig tile
    puzzleTick();    // poll the puzzle-box board (interface 1931); drives the optimal-solver guide
    clueScrollTick();   // poll the open clue scroll (interface 345); auto-identifies emote/cryptic clues
  }
  // Mode dispatcher. LIVE = difficulty tabs + held-clue list + solver (held clues only). BROWSE =
  // the read-only database, which never touches the live solver (activeClueId is dropped on entry).
  function clueRenderFocus() {
    const tabs = $('clueTabs'), held = $('clueHeld'), tb = $('clueToolbar'), cnt = $('clueCnt'), list = $('clueList');
    if (!tabs) return;
    const SOLVER = ['clueCompass', 'cluePuzzle', 'clueEmote', 'clueKnot', 'clueMapWrap'];
    // Opening a puzzle box in-game selects its held clue so the solver shows there.
    if (cluePuzzleOpen && !cluePuzzleWasOpen && !clueBrowse && cluePuzzleHeld.length) { activeClueId = cluePuzzleHeld[0].i; clueLiveTier = -1; }
    cluePuzzleWasOpen = cluePuzzleOpen;
    if (clueBrowse) {
      if (tabs._tsig !== 'B') { tabs.innerHTML = '<button id="clueBackBtn" class="pet-chip">&#8249; Held clues</button>'; tabs._tsig = 'B'; }
      held.style.display = 'none';
      if (tb) tb.style.display = ''; if (cnt) cnt.style.display = ''; if (list) list.style.display = '';
      SOLVER.forEach(id => { const el = $(id); if (el) el.style.display = 'none'; });
      if (activeClueId !== -1) { activeClueId = -1; selectClue(); }
      renderCluesList();
      return;
    }
    if (tb) tb.style.display = 'none'; if (cnt) cnt.style.display = 'none'; if (list) list.style.display = 'none';
    held.style.display = '';
    renderClueTabs();
    renderClueHeld();
  }
  function renderClueTabs() {
    const tabs = $('clueTabs'); if (!tabs) return;
    const hl = clueHeldList(); const cnt = {}; for (const c of hl) cnt[c.t] = (cnt[c.t] || 0) + 1;
    const sig = 'L|' + clueLiveTier + '|' + hl.length + '|' + [0, 1, 2, 3, 4].map(t => cnt[t] || 0).join(',');
    if (tabs._tsig === sig) return; tabs._tsig = sig;
    const tab = (tier, label) => { const n = (tier < 0) ? hl.length : (cnt[tier] || 0), on = clueLiveTier === tier;
      return '<button data-tier="' + tier + '" class="pet-chip' + (on ? ' on' : '') + '"' + (n ? '' : ' style="opacity:0.4"') + '>' + label + (n ? ' <span style="opacity:0.7">' + n + '</span>' : '') + '</button>'; };
    let h = tab(-1, 'All'); for (let t = 0; t < 5; t++) h += tab(t, CLUE_TIERS[t]);
    h += '<button id="clueBrowseBtn" class="pet-chip" style="margin-left:auto">Browse all</button>';
    tabs.innerHTML = h;
  }
  // Auto-selects a held clue so the solver always shows one. Puzzle clues (slider / lockbox /
  // towers) list here too and route through selectClue.
  // Clue ids seen in the backpack, so a clue LEAVING it can be detected (= completed).
  // fetchClues already resets on the ARRIVAL edge (a clue id re-entering the inventory); this
  // is the departure edge, which also covers a swap that completes inside one poll interval.
  // Confirmed over two polls: a single empty read during a bridge hiccup must not wipe the
  // eliminations of a scan you are still solving.
  const clueHeldSeen = new Set(), clueGoneCnt = {};
  function renderClueHeld() {
    const el = $('clueHeld'); if (!el) return;
    const hl = clueHeldList();
    {
      const now = new Set(hl.map(c => c.i));
      for (const id of now) { clueHeldSeen.add(id); clueGoneCnt[id] = 0; }
      for (const id of [...clueHeldSeen]) {
        if (now.has(id)) continue;
        clueGoneCnt[id] = (clueGoneCnt[id] || 0) + 1;
        if (clueGoneCnt[id] < 2) continue;
        clueHeldSeen.delete(id); delete clueGoneCnt[id];
        try { scanElimResetFor(id); } catch (e) {}   // same reset the arrival edge uses
      }
    }
    const list = (clueLiveTier < 0) ? hl : hl.filter(c => c.t === clueLiveTier);
    if (!list.some(c => c.i === activeClueId)) {                  // keep the active clue if in view, else pick the first
      const want = list.length ? list[0].i : -1;
      if (activeClueId !== want) { activeClueId = want; clueMapZoom = 1; clueMapPin = null; compassMapSig = ''; selectClue(); }
    }
    const pin = clueVarcPinned();   // pinned rows render inert, so the lockout is visible not silent
    const sig = 'H|' + clueLiveTier + '|' + activeClueId + '|' + pin + '|' + (g_scanMeerkats ? 'm' : '') + '|' + list.map(c => c.i).join(',');
    if (el._hsig === sig) return; el._hsig = sig;
    const esc = s => String(s).replace(/[&<>]/g, x => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;' }[x]));
    if (!hl.length) { el.innerHTML = '<div style="text-align:center;padding:14px 6px;opacity:0.7;font-size:12px;line-height:1.5">No clue scrolls held.<br>Hold a clue and it appears here. Use <b>Browse all</b> for the full database.</div>'; return; }
    if (!list.length) { el.innerHTML = '<div style="text-align:center;padding:12px 6px;opacity:0.6;font-size:12px">No ' + esc(CLUE_TIERS[clueLiveTier]) + ' clues held.</div>'; return; }
    let h = '';
    for (const c of list) { const on = c.i === activeClueId;
      const lock = pin >= 0 && pin !== c.i && clueVarcIsPair(c.i);   // the other half of the pair
      h += '<div data-hid="' + c.i + '"' + (lock ? ' data-tip="The game is pointing at the other one right now - it switches back on its own."' : '') + ' style="display:flex;align-items:center;gap:8px;padding:7px 9px;border-radius:7px;cursor:' + (lock ? 'default' : 'pointer') + ';margin-bottom:4px;' + (lock ? 'opacity:0.45;' : '') + 'background:' + (on ? 'rgba(120,180,255,0.14)' : 'rgba(255,255,255,0.04)') + ';border:1px solid ' + (on ? 'rgba(120,180,255,0.45)' : 'rgba(255,255,255,0.08)') + '">'
        + '<span class="clue-tier t' + c.t + '">' + CLUE_TIERS[c.t] + '</span>'
        + '<div style="flex:1;min-width:0;font-size:12px;font-weight:600;white-space:nowrap;overflow:hidden;text-overflow:ellipsis">' + esc(clueActionText(c)) + '</div>'
        + (on ? '<span style="color:#7fb0ff;font-size:11px;flex:none">solving</span>'
              : lock ? '<span style="color:#8c93a8;font-size:11px;flex:none">on hold</span>' : '') + '</div>';
    }
    el.innerHTML = h;
  }
  function renderCluesList() {
    const list = $('clueList'); if (!list) return;
    if (!clueData) { list.innerHTML = '<div class="empty">Reading... (be in-world)</div>'; clueListSig = ''; return; }
    const items = clueData.filter(c =>
      (clueFTier < 0 || c.t === clueFTier) &&
      (!clueFAction || clueAct(c) === clueFAction) &&
      (!clueFHeld || clueHeld.has(c.i)) &&
      (!clueFSearch || String(c.i).indexOf(clueFSearch) >= 0 || clueActionText(c).toLowerCase().indexOf(clueFSearch) >= 0));
    const heldCount = clueData.filter(c => clueHeld.has(c.i)).length;
    const cnt = $('clueCnt'); if (cnt) cnt.textContent = heldCount + ' held now  ·  ' + clueData.length + ' clues mapped' + (items.length !== clueData.length ? '  ·  ' + items.length + ' shown' : '');
    const sig = clueFTier + '|' + clueFAction + '|' + clueFHeld + '|' + clueFSearch + '|' + heldCount + '|' + items.map(c => c.i).join(',');
    if (sig === clueListSig) return;
    clueListSig = sig;
    list.innerHTML = '';
    const clueRow = (c) => {
      const held = clueHeld.has(c.i);
      const loc = !!clueTile(c) || c.a === 'emote';
      const row = document.createElement('div');
      row.className = 'pet-row' + (held ? ' clue-held' : '') + (loc ? ' clue-loc' : '') + (c.i === activeClueId ? ' clue-active' : '');
      row.dataset.cid = c.i;
      row.dataset.tip = 'Clue scroll (' + CLUE_TIERS[c.t] + ') #' + c.i + '\n' + clueActionText(c) + (loc ? '\n\nClick to pin this tile on the map + highlight it in-world.' : '') + (held ? '\n\nYou are holding this clue right now.' : '');
      const tier = document.createElement('div'); tier.className = 'clue-tier t' + c.t; tier.textContent = CLUE_TIERS[c.t]; row.appendChild(tier);
      const info = document.createElement('div'); info.className = 'pet-info';
      const nm = document.createElement('div'); nm.className = 'pet-nm'; nm.textContent = clueActionText(c); info.appendChild(nm);
      const ni = (c.a === 'npc') ? (CLUE_NPC_INFO[c.npc] || {}) : {};
      const sub = document.createElement('div'); sub.className = 'hh-loc';
      sub.textContent = (ni.l ? ni.l + '  ·  ' : '') + CLUE_ACT_LBL[clueAct(c)] + ' clue · #' + c.i; info.appendChild(sub);
      row.appendChild(info);
      if (held) { const st = document.createElement('div'); st.className = 'hh-st hh-filled'; st.textContent = 'Held'; row.appendChild(st); }
      return row;
    };
    const secHdr = (txt) => { const h = document.createElement('div'); h.textContent = txt;
      h.style.cssText = 'font-size:10px;text-transform:uppercase;letter-spacing:0.5px;opacity:0.55;font-weight:700;padding:9px 4px 4px'; return h; };
    // "Held now" = held clues WITHIN the current filter; "Other clues" = the rest of the filtered
    // set. Both honour the tier/type/search chips, so a held clue is never shown twice.
    const heldClues = items.filter(c => clueHeld.has(c.i));
    const showHeld = heldClues.length && !clueFHeld;
    const otherItems = showHeld ? items.filter(c => !clueHeld.has(c.i)) : items;
    if (showHeld) {
      list.appendChild(secHdr('Held now (' + heldClues.length + ')'));
      for (const c of heldClues) list.appendChild(clueRow(c));
      if (otherItems.length) list.appendChild(secHdr('Other clues'));
    }
    if (!items.length) { const e = document.createElement('div'); e.className = 'empty'; e.textContent = 'No clues match.'; list.appendChild(e); return; }
    for (const c of otherItems) list.appendChild(clueRow(c));
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { clueRenderFocus, fetchGlobetrotter, renderClueStats, renderClues, renderCluesList, renderGlobetrotterTab });
registerTab({ id: 'clues', render: renderClues, open: function () { clueListSig = ''; fetchClues(true); }, close: function () { try { activeClueId = -1; if (bridge() && bridge().guideMarks) rtxData.sync('overlay.guideMarks', ''); clueSetNpc(''); } catch (e) {} } });
registerTab({ id: 'globetrotter', render: renderGlobetrotterTab });
registerTab({ id: 'cluestats', render: renderClueStats });
})();
