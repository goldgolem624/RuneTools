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
// "claimed" here is exactly what lights it up there. Entrance coordinates ship with the panel
// (RD_COORDS below) because they are the one part of this that the cache does not carry.
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
  // Entrance coordinates. These are the destinations the Dungeoneering cape teleports to, one
  // per dungeon, which is the entrance in every case. They are NOT in the cache: no struct
  // carries the teleport destination param for them (only the 114 spellbook and lodestone
  // teleports do), the packed coordinates appear in no clientscript, and no dbrow names a
  // dungeon beside a coordinate. They are curated data, from the RuneScape Cartography
  // dataset (mejrs/data_rs3, teleport_data.json, group "Dungeoneering cape"), the same
  // provenance as the world map's transport networks.
  //
  // Bound to dungeons by level: the cape's list is ordered by requirement and all 21 levels
  // are distinct, so the pairing is unambiguous. Two ends of it were checked directly against
  // the in-game map (chaos druids 3134,9915 and hill giants 3105,9829) and every other entry
  // sits where its dungeon is.
  const RD_COORDS = {
    0:    { x: 3134, y: 9915, p: 0 },   // Edgeville Dungeon: Chaos Druids (10)
    1:    { x: 3036, y: 9774, p: 0 },   // Dwarven Mine (15)
    2:    { x: 3105, y: 9829, p: 0 },   // Edgeville Dungeon: Hill Giants (20)
    3:    { x: 2843, y: 9555, p: 0 },   // Karamja Volcano (25)
    4:    { x: 3512, y: 3663, p: 0 },   // Daemonheim Peninsula (30)
    5:    { x: 2581, y: 9897, p: 0 },   // Baxtorian Falls (35)
    6:    { x: 3022, y: 9739, p: 0 },   // Mining Guild (45)
    7:    { x: 2856, y: 9840, p: 0 },   // Taverley Dungeon: Hellhounds (55)
    8:    { x: 2915, y: 9806, p: 0 },   // Taverley Dungeon: Blue Dragons (60)
    9:    { x: 3166, y: 9879, p: 0 },   // Varrock Sewers (65)
    10:   { x: 3159, y: 5524, p: 0 },   // Chaos Tunnels (70)
    11:   { x: 3299, y: 3307, p: 0 },   // Al Kharid Mine (75)
    12:   { x: 2697, y: 9441, p: 0 },   // Brimhaven Dungeon (80)
    13:   { x: 3032, y: 9599, p: 0 },   // Asgarnian Ice Dungeon (85)
    14:   { x: 3398, y: 3665, p: 0 },   // Kal'gerion Dungeon (90)
    16:   { x: 2235, y: 3420, p: 1 },   // Prifddinas: Gorajo (95)
    17:   { x: 2233, y: 3396, p: 1 },   // Prifddinas: Motherlode (115)
    18:   { x: 3434, y: 3529, p: 0 },   // Slayer Tower (100)
    1000: { x: 4659, y: 5491, p: 3 },   // Polypore Dungeon (82)
    1001: { x: 3812, y: 3528, p: 0 },   // Dragontooth Celestial Dungeon (67)
    1002: { x: 2125, y: 5146, p: 0 },   // Braindeath Island (50)
  };
  let rdVb = null; let rdFetching = false; let rdFetchAt = 0; let rdSig = ''; rdShowDone = true;
  function rdCoordsLoad() { return RD_COORDS; }
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
        + '<button class="rd-btn" data-rd-map="' + r[0] + '">Map</button></div>');
    }
    let h = '<div class="rd-top"><button class="rd-btn" id="rdShowDone">' + (rdShowDone ? 'Hide claimed' : 'Show claimed') + '</button>'
      + '<button class="rd-btn" id="rdPinAll">Pin all on map</button>'
      + '<span class="rd-cnt">' + done + ' / ' + RD_LIST.length + ' claimed</span></div>';
    h += '<div class="rd-card">' + (rows.length ? rows.join('') : '<div class="rd-empty">Every dungeon claimed.</div>') + '</div>';
    h += '<div class="rd-note">Names, levels and the claimed flags are the game\'s own (enum 3537, scripts 3151 / 3144 / 3136, the Very Resourceful varbits). The in-game list lights an entry only when the level is met and the XP is claimed; here the level pill and the dot say which is missing.</div>';
    wrap.innerHTML = h;
    rdPushPins();
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { fetchResDungeons });
registerTab({ id: 'resdungeons', render: renderResDungeons, open: function () { rdSig = ''; rdFetchAt = 0; fetchResDungeons(); } });
})();
