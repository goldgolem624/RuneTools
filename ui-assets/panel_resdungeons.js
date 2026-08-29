// RuneToolsX panel: Resource Dungeons (first-visit XP claimed per dungeon, plotted on the map).
// Spliced inline into client.html at load; IIFE (window exports + registerTab; see the RTX registry in client.html).
//
// Everything about the dungeons themselves is the game's own data (CS2, build 949):
//   enum 3537       the list the in-game Resource Dungeons screen walks (21 entries, in its order)
//   script 3151     display name per dungeon id
//   script 3144     Dungeoneering level requirement per id
//   script 3136     the "first-visit XP claimed" varbit per id (the Very Resourceful flags)
//   script 3150     members-only flag (ids 1000+ are the free-to-play trio; the rest members)
// The in-game screen greys an entry when the level is short OR the XP is unclaimed, so
// "claimed" here is exactly what lights it up there. Entrance coordinates are not in the
// cache or scripts; they are entered once (Coordinates > Import) and kept durable.
(function () {

  const RD_LIST = [
    // [id, name, level, varbit, members]
    [0,    'Edgeville Dungeon: Chaos Druids', 10,  2458,  1],
    [1,    'Dwarven Mine',                    15,  2459,  1],
    [2,    'Edgeville Dungeon: Hill Giants',  20,  2460,  1],
    [3,    'Karamja Volcano',                 25,  2461,  1],
    [4,    'Daemonheim Peninsula',            30,  2462,  1],
    [5,    'Baxtorian Falls',                 35,  2463,  1],
    [6,    'Mining Guild',                    45,  2464,  1],
    [7,    'Taverley Dungeon: Hellhounds',    55,  2465,  1],
    [8,    'Taverley Dungeon: Blue Dragons',  60,  2466,  1],
    [9,    'Varrock Sewers',                  65,  2467,  1],
    [10,   'Chaos Tunnels',                   70,  2468,  1],
    [11,   'Al Kharid Mine',                  75,  2469,  1],
    [12,   'Brimhaven Dungeon',               80,  2470,  1],
    [13,   'Asgarnian Ice Dungeon',           85,  2471,  1],
    [14,   "Kal'gerion Dungeon",              90,  22569, 1],
    [16,   'Prifddinas: Gorajo',              95,  25871, 1],
    [17,   'Prifddinas: Motherlode',          115, 25872, 1],
    [18,   'Slayer Tower',                    100, 43691, 1],
    [1000, 'Polypore Dungeon',                82,  508,   0],
    [1001, 'Dragontooth Celestial Dungeon',   67,  42344, 0],
    [1002, 'Braindeath Island',               50,  41302, 0],
  ];
  let rdVb = null; let rdFetching = false; let rdFetchAt = 0; let rdSig = ''; let rdCoords = null; rdShowDone = true;
  function rdCoordsLoad() {
    if (rdCoords) return rdCoords;
    let v = null;
    try { v = JSON.parse((typeof prefGet === 'function' ? prefGet('rtxResDungCoords', '') : localStorage.getItem('rtxResDungCoords')) || 'null'); } catch (e) {}
    rdCoords = (v && typeof v === 'object') ? v : {};
    return rdCoords;
  }
  function rdCoordsSave() { try { prefSet('rtxResDungCoords', JSON.stringify(rdCoords || {})); } catch (e) {} }
  async function fetchResDungeons() {
    if (!bridge() || !bridge().varbits || rdFetching) return;
    const t = Date.now(); if (t - rdFetchAt < 2000) return; rdFetchAt = t; rdFetching = true;
    try {
      const vb = JSON.parse(await rtxData.raw('state.varbitsCsv', RD_LIST.map(r => r[3]).join(',')) || 'null');
      if (vb && typeof vb === 'object' && Object.keys(vb).length) rdVb = vb;
    } catch (e) {}
    rdFetching = false;
    paneRun('resdungeons', renderResDungeons);
  }
  // Import block: one dungeon per line, "Name: x, y[, plane]" or "Name x y". Names match on a
  // case-insensitive prefix of the game name so "Dwarven Mine 3035 9772" lands.
  function rdImport(text) {
    const c = rdCoordsLoad(); let n = 0;
    for (const raw of String(text || '').split(/\r?\n/)) {
      const m = raw.trim().match(/^(.+?)[\s:,]+(\d{3,5})[\s,]+(\d{3,5})(?:[\s,]+(\d))?\s*$/);
      if (!m) continue;
      const key = m[1].trim().toLowerCase().replace(/[^a-z0-9]/g, '');
      const hit = RD_LIST.find(r => r[1].toLowerCase().replace(/[^a-z0-9]/g, '').startsWith(key) || key.startsWith(r[1].toLowerCase().replace(/[^a-z0-9]/g, '')));
      if (!hit) continue;
      c[hit[0]] = { x: +m[2], y: +m[3], p: m[4] ? +m[4] : 0 }; n++;
    }
    if (n) { rdCoordsSave(); rdSig = ''; rdPushPins(); }
    return n;
  }
  function rdPushPins() {
    if (typeof wmSetExtPins !== 'function') return;
    const c = rdCoordsLoad(), pins = [];
    for (const r of RD_LIST) {
      const k = c[r[0]]; if (!k) continue;
      const done = rdVb ? ((rdVb[String(r[3])] | 0) === 1) : null;
      pins.push({ x: k.x, y: k.y, p: k.p | 0, nm: r[1], sub: 'Level ' + r[2] + (done === null ? '' : done ? ' · XP claimed' : ' · XP unclaimed'), col: done ? '#4dd28a' : '#f5b241', group: 'resdung' });
    }
    wmSetExtPins('resdung', pins);
  }
  function renderResDungeons() {
    const c = $('content');
    let wrap = $('rdWrap');
    if (!wrap) {
      injectStyle('rdCss', `
        .rd-top { display: flex; align-items: center; gap: 8px; margin: 2px 12px 6px; flex-wrap: wrap; }
        .rd-cnt { color: var(--text-dim); font-size: 11px; margin-left: auto; font-variant-numeric: tabular-nums; }
        .rd-card { margin: 0 12px 10px; background: var(--bg-elev); border: 1px solid var(--border); border-radius: 10px; }
        .rd-row { display: flex; align-items: center; gap: 10px; padding: 7px 12px; }
        .rd-row + .rd-row { border-top: 1px solid var(--border); }
        .rd-dot { width: 9px; height: 9px; border-radius: 50%; flex: none; background: var(--text-mute); }
        .rd-row.done .rd-dot { background: #4dd28a; box-shadow: 0 0 6px rgba(77,210,138,.6); }
        .rd-row.todo .rd-dot { background: #f5b241; }
        .rd-nm { flex: 1; min-width: 0; color: var(--text); font-size: 12px; }
        .rd-row.done .rd-nm { color: var(--text-dim); }
        .rd-sub { color: var(--text-mute); font-size: 10.5px; margin-top: 1px; }
        .rd-lv { font-size: 10.5px; padding: 2px 8px; border-radius: 999px; border: 1px solid var(--border); color: var(--text-dim); white-space: nowrap; font-variant-numeric: tabular-nums; }
        .rd-lv.ok { color: #4dd28a; border-color: rgba(77,210,138,.45); }
        .rd-lv.no { color: #e05656; border-color: rgba(224,86,86,.45); }
        .rd-btn { font-size: 10.5px; padding: 2px 9px; border-radius: 999px; border: 1px solid var(--border); background: transparent; color: var(--text-dim); cursor: pointer; white-space: nowrap; }
        .rd-btn:hover { color: var(--text); border-color: var(--accent-hi); }
        .rd-btn:disabled { opacity: .35; cursor: default; }
        .rd-note { margin: 6px 14px 10px; color: var(--text-mute); font-size: 11px; line-height: 1.45; }
        .rd-imp { margin: 0 12px 10px; }
        .rd-imp textarea { width: 100%; min-height: 70px; background: var(--bg-elev); color: var(--text); border: 1px solid var(--border); border-radius: 8px; padding: 6px 8px; font: 11px/1.4 Consolas, monospace; resize: vertical; }
        .rd-empty { margin: 10px 14px; color: var(--text-dim); font-size: 12px; }`);
      c.innerHTML = ''; rdSig = '';
      wrap = document.createElement('div'); wrap.id = 'rdWrap'; wrap.className = 'pane'; c.appendChild(wrap);
      wrap.addEventListener('click', ev => {
        const b = ev.target.closest ? ev.target.closest('[data-rd-map]') : null;
        if (b) {
          const id = +b.dataset.rdMap, k = rdCoordsLoad()[id], r = RD_LIST.find(x => x[0] === id);
          if (!k || !r) return;
          rdPushPins();
          try { const t = allTabs().find(x => x.id === 'worldmap'); if (t) openTab(t); } catch (e) {}
          setTimeout(() => { try { wmFlyTo(k.x, k.y, k.p | 0, { x: k.x, y: k.y, p: k.p | 0, nm: r[1] }); } catch (e) {} }, 60);
          return;
        }
        if (ev.target.id === 'rdShowDone') { rdShowDone = !rdShowDone; rdSig = ''; renderResDungeons(); return; }
        if (ev.target.id === 'rdImpBtn') {
          const ta = $('rdImpText'); const n = rdImport(ta ? ta.value : '');
          uiNotify(n ? ('Imported coordinates for ' + n + ' dungeon' + (n === 1 ? '' : 's') + '.') : 'No lines matched. Use one "Name: x, y" per line.', {});
          renderResDungeons(); return;
        }
        if (ev.target.id === 'rdPinAll') { rdPushPins(); try { const t = allTabs().find(x => x.id === 'worldmap'); if (t) openTab(t); } catch (e) {} return; }
      });
    }
    if (!rdVb) { wrap.innerHTML = '<div class="rd-empty">Reading resource dungeon flags... (be in-world)</div>'; rdSig = ''; return; }
    const lv = (typeof lastSnap !== 'undefined' && lastSnap && Array.isArray(lastSnap.skills) && lastSnap.skills[24]) ? (lastSnap.skills[24][0] | 0) : null;
    const coords = rdCoordsLoad();
    const sig = RD_LIST.map(r => (rdVb[String(r[3])] | 0)).join('') + '|' + lv + '|' + rdShowDone + '|' + Object.keys(coords).length;
    if (sig === rdSig) return; rdSig = sig;
    const esc = x => String(x).replace(/&/g, '&amp;').replace(/</g, '&lt;');
    let done = 0; const rows = [];
    for (const r of RD_LIST) {
      const claimed = (rdVb[String(r[3])] | 0) === 1; if (claimed) done++;
      if (claimed && !rdShowDone) continue;
      const k = coords[r[0]];
      const lvCls = lv === null ? '' : (lv >= r[2] ? ' ok' : ' no');
      rows.push('<div class="rd-row ' + (claimed ? 'done' : 'todo') + '"><span class="rd-dot"></span>'
        + '<div class="rd-nm">' + esc(r[1]) + '<div class="rd-sub">' + (claimed ? 'First-visit XP claimed' : 'XP not yet claimed') + (r[4] ? '' : ' · free-to-play') + (k ? ' · ' + k.x + ', ' + k.y : '') + '</div></div>'
        + '<span class="rd-lv' + lvCls + '" title="Dungeoneering level required">' + r[2] + '</span>'
        + '<button class="rd-btn" data-rd-map="' + r[0] + '"' + (k ? '' : ' disabled title="No coordinates yet: add them under Coordinates"') + '>Map</button></div>');
    }
    let h = '<div class="rd-top"><button class="rd-btn" id="rdShowDone">' + (rdShowDone ? 'Hide claimed' : 'Show claimed') + '</button>'
      + '<button class="rd-btn" id="rdPinAll"' + (Object.keys(coords).length ? '' : ' disabled') + '>Pin all on map</button>'
      + '<span class="rd-cnt">' + done + ' / ' + RD_LIST.length + ' claimed</span></div>';
    h += '<div class="rd-card">' + (rows.length ? rows.join('') : '<div class="rd-empty">Every dungeon claimed.</div>') + '</div>';
    h += '<div class="rd-note">Names, levels and the claimed flags are the game\'s own (enum 3537, scripts 3151 / 3144 / 3136, the Very Resourceful varbits). The in-game list lights an entry only when the level is met and the XP is claimed; here the level pill and the dot say which is missing.</div>';
    h += '<div class="rd-imp"><div class="rd-note" style="margin:0 2px 4px">Coordinates: one per line, "Name: x, y" (plane optional). Names match on a prefix.</div><textarea id="rdImpText" placeholder="Dwarven Mine: x, y&#10;Taverley Dungeon: Hellhounds: x, y, plane"></textarea><div style="margin-top:6px"><button class="rd-btn" id="rdImpBtn">Import coordinates</button></div></div>';
    wrap.innerHTML = h;
    rdPushPins();
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { fetchResDungeons });
registerTab({ id: 'resdungeons', render: renderResDungeons, open: function () { rdSig = ''; rdFetchAt = 0; fetchResDungeons(); } });
})();
