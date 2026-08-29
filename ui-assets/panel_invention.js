// RuneToolsX panel: Invention components (inventory-style grid).
// Spliced inline into client.html; IIFE (window exports + registerTab; see the RTX registry in client.html).
(function () {

  // Defs from the cache (enum 10742 -> dbrows): [name, countVarp, spriteId, rarity,
  // description (col 16416), obtainSources (col 16432)]. Count = live varp (CS2 script12054
  // index->varp table folded into countVarp); image = cache sprite (col 16448).
  // Rarity: 1 Junk, 2 Common, 3 Uncommon, 4 Rare, 5 Ancient, 6 Refined; array order drives the sections.
  const INV_COMPONENTS = [
    ['Simple parts',5998,26311,2,'Useful things for building with.','Ores, logs, hides, bars, planks, leather, basic weapons and clothing.'],
    ['Base parts',5999,26479,2,'Bits of framework for something more complex.','Bolts, weapons with hilts, shields.'],
    ['Blade parts',6000,26312,2,'Materials which can hold a keen edge.','Stabbing, slashing and thrown weapons.'],
    ['Magic parts',6001,26485,2,'Materials with magical potency.','Runes, signs and portents, magic weapons, magic armour.'],
    ['Organic parts',6002,26484,2,'Bits of something that was once alive.','Seeds, bones, ashes, food, herbs, potions.'],
    ['Spiritual parts',6003,26486,2,'Materials connected to the spirit realm.','Summoning charms, scrolls and pouches.'],
    ['Stave parts',6004,26478,2,'Bits and pieces of a staff, bowstave or similar.','Arrows, staves, spears, halberds, bows.'],
    ['Tensile parts',6005,26480,2,'Materials used to hold things in place.','Bows, crossbows, ranged armour.'],
    ['Head parts',6006,26481,2,'The business end of something.','Hatchets, battleaxes, blunt weapons, pickaxes, wands, ammunition.'],
    ['Connector parts',6007,26482,2,'Materials used to connect things together.','Claws, whips, crossbows, hatchets, jewellery.'],
    ['Cover parts',6008,26313,2,'Large parts for making a chassis or shell.','Armour, shields, clothing.'],
    ['Clear parts',6009,26483,2,'Things you can see through, in material form.','Gems, glass.'],
    ['Delicate parts',6010,26314,2,'Fine materials that need to be handled with care.','Orbs and books, jewellery, potions, glass, gems.'],
    ['Crafted parts',6011,26316,2,'Some work has already gone into this.','Summoning scrolls and pouches, spears, arrows, two-handed crossbows, potions, bars, planks, leather.'],
    ['Plated parts',6012,26487,2,'Casings, containers and parts of armour.','Mauls, thrown axes, melee armour.'],
    ['Flexible parts',6013,26488,2,'It\'s bendy!','Whips, bows, Summoning pouches.'],
    ['Deflecting parts',6014,26489,2,'Useful if you need to accelerate, decelerate or steer something.','Hybrid/melee/magic armour, shields, defenders, halberds.'],
    ['Metallic parts',6015,26490,2,'Good conductor, hard but slightly brittle.','Swords, knives.'],
    ['Spiked parts',6016,26491,2,'These are sharp and pointy, which could be useful.','Daggers, claws, one-handed crossbows, thrown weapons, bolts.'],
    ['Smooth parts',6017,26492,2,'Materials with low friction, pleasant to touch.','Glass, gems, jewellery, orbs and books, warhammers, maces, battleaxes.'],
    ['Padded parts',6018,26493,2,'Possible use as a shock absorber or just to increase comfort.','Ranged armour, clothing, staves.'],
    ['Crystal parts',6019,26315,2,'An orderly structure, good for focus or timing.','Harmonic dust, non-attuned crystal items, crystal tree blossoms.'],
    ['Precise components',6020,26494,3,'The quality of precision, carefully isolated.','Stabbing weapons, bows, arrows, wands.'],
    ['Sharp components',6021,26495,3,'The quality of sharpness, carefully isolated.','Slashing weapons, thrown weapons.'],
    ['Powerful components',6022,26318,3,'An amount of raw power taken from the thing that contained it.','Staves, magic/hybrid armour, Summoning pouches and scrolls, runes.'],
    ['Healthy components',6023,26500,3,'The platonic ideal of health in material form.','Food, herbs, potions.'],
    ['Heavy components',6025,26317,3,'A small quantity of pure weightiness.','Mauls, warhammers, maces, pickaxes.'],
    ['Stunning components',6064,26496,3,'Has a numbing quality to the touch.','Crossbows, bolts, halberds.'],
    ['Enhancing components',6065,26497,3,'The notion of making something better, in essential form.','Orbs and books, potions, glass, jewellery, food, herbs.'],
    ['Protective components',6026,26498,3,'The quality of protection, carefully isolated.','Armour and shields.'],
    ['Evasive components',6027,26499,3,'The quality of evasiveness, carefully isolated.','Ranged armour, hybrid armour.'],
    ['Precious components',6028,26319,3,'A quantum of value, stored as a material.','Jewellery, gems.'],
    ['Pious components',6029,26501,3,'The quality of faith, carefully isolated.','Bones, ashes.'],
    ['Light components',6030,26502,3,'Materials that lack weight, and seem to faintly glow.','Gems, daggers.'],
    ['Living components',6031,26503,3,'It\'s alive. Alive!','Food, herbs, seeds, logs, hides.'],
    ['Ethereal components',6032,26504,3,'These materials are not quite all here, at least not all at once.','Signs and portents, orbs and books, ashes.'],
    ['Variable components',6033,26505,3,'It\'s not quite certain what these do, but they probably do something.','Miscellaneous.'],
    ['Dextrous components',6034,26506,3,'Easily manipulated and well-balanced.','Swords, longswords, maces, shortbows, crossbows.'],
    ['Strong components',6035,26320,3,'This ought to be useful if you need something tough.','Melee armour, shields, hatchets, two-handed swords, warhammers, mauls, shieldbows.'],
    ['Swift components',6036,26507,3,'Always in motion, even when at rest.','Claws, knives, thrown weapons.'],
    ['Imbued components',6037,26508,3,'Thrumming with power and vibrating gently.','Wands, staves.'],
    ['Direct components',6038,26509,3,'These seem to have straightforward applications.','Spears, battleaxes, pickaxes.'],
    ['Subtle components',6039,26510,3,'These might be good for something underhanded.','Defenders, scimitars, whips.'],
    ['Offcut components',12213,34974,3,'Useful leftovers from an item.','Flatpacks, hunter gear, furs.'],
    ['Knightly components',6024,26530,4,'An oath can have force that takes an almost physical power.',''],
    ['Harnessed components',6040,26511,4,'Bursting with the power of harnessed elemental and catalytic runes.',''],
    ['Undead components',6041,26512,4,'This material pulses as if unwilling to die.',''],
    ['Seren components',6049,26513,4,'A faint singing can be heard in the depths of the material.',''],
    ['Dragonfire components',6042,26514,4,'Warm to the touch and glowing at its core.',''],
    ['Fungal components',6043,26515,4,'Pustulent, and more complex than you might expect.',''],
    ['Explosive components',6044,26516,4,'They hum. Don\'t drop them.',''],
    ['Corporeal components',6045,26517,4,'The soul of a dangerous predator lies within.',''],
    ['Saradomin components',6048,26518,4,'Godly, powerful, and certain in their purpose.',''],
    ['Zamorak components',6050,26519,4,'Godly, chaotic, and competitive.',''],
    ['Armadyl components',6046,26520,4,'Godly, simple, and well-meaning.',''],
    ['Bandos components',6047,26521,4,'Godly, heavy, and firm in their belief.',''],
    ['Zaros components',6051,26522,4,'They\'re so inscrutable you\'re not sure quite how they do what they do.',''],
    ['Oceanic components',6052,26523,4,'This material was salvaged from the armour of those who sing to beasts.',''],
    ['Resilient components',6053,26524,4,'Harder than iron, harder than steel.',''],
    ['Silent components',6054,26525,4,'Heady, with notes of leather and venom.',''],
    ['Noxious components',6055,26526,4,'Fortunately you\'re a professional, or handling this could be deadly.',''],
    ['Rumbling components',6056,26527,4,'All the pressure of a faultline, but far more compact.',''],
    ['Ascended components',6057,26528,4,'The ascension project is not an ethical use of science.',''],
    ['Pestiferous components',6058,26529,4,'Properties of the hive from beyond the worlds.',''],
    ['Fortunate components',6059,26531,4,'Some of the luck has clung to the salvaged parts of treasure.',''],
    ['Third-age components',6060,10769,4,'Sometimes things acquire virtue and power by dint of age alone.',''],
    ['Culinary components',6061,26533,4,'You\'re an inventor, not a chemist, but you still need to cook.',''],
    ['Brassican components',6062,26534,4,'Come on, are you even taking this seriously?',''],
    ['Shifting components',6063,26535,4,'Pulse and shift and generally express a desire to be something else.',''],
    ['Shadow components',6215,27117,4,'Did that just move?',''],
    ['Avernic components',6216,27114,4,'Smells of brimstone and war.',''],
    ['Ilujankan components',6217,27116,4,'A dying breed of components.',''],
    ['Cywir components',6218,27115,4,'Singing mixed with drumming can faintly be heard in the depths of the material.',''],
    ['Clockwork components',6508,28074,4,'Complex items with lots of gears and spinning bits.',''],
    ['Faceted components',6509,28075,4,'Magically clear, these components can also reflect or focus light.',''],
    ['Manufactured components',12214,34975,4,'Well-made and reliable.',''],
    ['Ecliptic components',12683,35591,4,'Individually insignificant, but together perfect.',''],
    ['Historic components',9400,10770,5,'Historically memorable.','Archaeological materials, artefacts.'],
    ['Classic components',9401,10778,5,'Something of excellence.','Archaeological materials, artefacts.'],
    ['Timeworn components',9402,10779,5,'Thoroughly worn down over time.',''],
    ['Vintage components',9403,10780,5,'The best of its kind.',''],
    ['Refined components',6066,26477,6,'Amazing what you can find in a pile of junk.',''],
    ['Junk',5997,26310,1,'There may be some use to this stuff, somehow?','']
  ];
  const COMP_RARITY = { 2: 'Common', 3: 'Uncommon', 4: 'Rare', 5: 'Ancient', 6: 'Refined', 1: 'Junk' };
  const COMP_VARP_CSV = INV_COMPONENTS.map(c => c[1]).join(',');
  let compVp = null, compSig = '', compTerm = '', compHideEmpty = false, compFetching = false;
  // compTerm is a search box (transient); "Hide empty" is a deliberate view choice, so keep it.
  try { compHideEmpty = localStorage.getItem('rtxCompHideEmpty') === '1'; } catch (e) {}
  // ---- Invention machines. CORRECTED SOURCES (the first cut read container 93 = the player
  // BACKPACK and container 1011 = an herb store keyed by enum 13250, so the card showed plain
  // inventory items - user-reported). The game's machine interface (script13676/13678) is
  // varbit-driven and those varbits are what we read now:
  //   vb 37614  machine type of the machine last inspected (enum 13175 key -> db22 row)
  //   vb 37615  hotspot the inspected machine occupies (1-7, enum 13176 -> db23 row)
  //   vb 37616/37617  current counts of the machine's two input items (db22 col 19 pairs)
  //   vb 37590  divine charge stored; varp 7270 items loaded (vb 48814 for batch machines)
  //   vb 37612/37613  total / used machine space across hotspots
  // These transmit when the player interacts with machines, so the per-machine card is
  // honest about being "as of last inspection".
  const MCH_VBS = [37614, 37615, 37616, 37617, 37590, 37612, 37613, 48814];
  let mchVb = null, mchVp7270 = null, mchDb22 = null, mchEnum13175 = null, mchNames = {};
  async function mchName(id) {
    if (mchNames[id] === undefined) {
      try { const d = JSON.parse(await rtxData.raw('cache.itemInfo', id) || 'null'); mchNames[id] = (d && d.name) || ''; } catch (e) {}
    }
    return mchNames[id] || ('Item ' + id);
  }
  async function fetchMachines() {
    if (!bridge() || !bridge().varbits) return;
    try {
      mchVb = JSON.parse(await rtxData.raw('state.varbitsCsv', MCH_VBS.join(',')) || 'null');
      const vp = JSON.parse(await rtxData.raw('state.varps', '7270') || '{}');
      mchVp7270 = vp['7270'] !== undefined ? (vp['7270'] | 0) : null;
      if (!mchEnum13175 && bridge().enumInfo) {
        try { mchEnum13175 = JSON.parse(await rtxData.raw('cache.enumInfo', 13175) || 'null'); } catch (e) {}
      }
      if (!mchDb22 && bridge().dbRows) {
        try {
          const rows = JSON.parse(await rtxData.raw('cache.dbRows', 22) || 'null');
          if (Array.isArray(rows) && rows.length) {
            mchDb22 = {};
            for (const r of rows) if (r && r.f !== undefined) mchDb22[r.f] = r;   // "f" = dbrow file id
          }
        } catch (e) {}
      }
      if (mchVb && mchDb22 && mchEnum13175) {
        const row = mchDb22[mchEnum13175[String(mchVb['37614'] | 0)]];
        if (row && row.i && row.i['19']) {
          const p = row.i['19'];
          for (let k = 0; k + 1 < p.length; k += 2) await mchName(p[k] | 0);
        }
      }
    } catch (e) {}
  }
  function mchHtml() {
    const vb = mchVb; if (!vb) return '';
    const v = k => (vb[String(k)] === undefined || vb[String(k)] === null) ? null : (vb[String(k)] | 0);
    const esc = x => String(x).replace(/&/g, '&amp;').replace(/</g, '&lt;');
    const type = v(37614), spaceMax = v(37612), spaceUsed = v(37613);
    if (!type && !spaceMax) return '';
    let h = '<div class="comp-sec">Machines</div><div class="mch-card">';
    if (spaceMax) {
      h += '<div class="mch-row"><span>Machine space used</span><span>' + (spaceUsed || 0) + ' / ' + spaceMax + '</span></div>';
    }
    if (type && mchDb22 && mchEnum13175) {
      const row = mchDb22[mchEnum13175[String(type)]];
      const nm = (row && row.s && row.s['1'] && row.s['1'][0]) || ('Machine type ' + type);
      h += '<div class="mch-row mch-hdr"><span>Last inspected machine</span><span>hotspot ' + (v(37615) || '?') + '</span></div>';
      h += '<div class="mch-row"><span>' + esc(nm) + '</span><span></span></div>';
      const batch = !!(row && row.i && row.i['10'] && (row.i['10'][0] | 0) === 2);
      const loaded = (batch && v(48814) > 0) ? v(48814) : mchVp7270;
      if (loaded !== null) h += '<div class="mch-row"><span>Items loaded</span><span>' + loaded.toLocaleString() + '</span></div>';
      if (row && row.i && row.i['19']) {
        const p = row.i['19'], cur = [v(37616), v(37617)];
        for (let k = 0; k + 1 < p.length && k < 4; k += 2) {
          const need = (p[k + 1] | 0), have = cur[k / 2];
          h += '<div class="mch-row"><span>' + esc(mchNames[p[k] | 0] || ('Item ' + p[k])) + '</span><span>'
            + (have === null ? '·' : have.toLocaleString()) + (loaded && need ? ' / ' + (need * loaded).toLocaleString() : '') + '</span></div>';
        }
      }
      const ch = v(37590);
      if (ch !== null) h += '<div class="mch-row"><span>Divine charge stored</span><span>' + ch.toLocaleString() + '</span></div>';
    }
    h += '<div class="mch-note">Read from the varbits the in-game machine interface uses (script 13676: vb 37614/37615 selection, 37616/37617 inputs, 37590 charge, 37612/37613 space, varp 7270 load). Values refresh when you interact with machines in game.</div></div>';
    return h;
  }
  async function fetchComponents() {
    if (!bridge() || !bridge().varps || compFetching) return;
    compFetching = true;
    try { const d = JSON.parse(await rtxData.raw('state.varps', COMP_VARP_CSV)); if (d && typeof d === 'object') compVp = d; } catch (e) {}
    compFetching = false;
    paneRun('components', paintComponents);
  }
  let mchFetching = false;
  async function fetchMachinesTab() {
    if (mchFetching) return;
    mchFetching = true;
    try { await fetchMachines(); } catch (e) {}
    mchFetching = false;
    paneRun('machines', renderMachines);
  }
  function renderMachines() {
    const c = $('content');
    const h = mchHtml();
    c.innerHTML = '<div class="bank-wrap"><div class="mbank-body" id="mchBody"></div></div>';
    const body = $('mchBody');
    if (!mchVb) {
      body.innerHTML = '<div class="bank-empty" style="display:flex"><div class="warn" style="background:rgba(255,255,255,0.04);border-color:var(--border);color:var(--text-dim)">Reading...</div></div>';
    } else if (!h) {
      body.innerHTML = '<div class="mch-card"><div class="mch-note">No machine data yet. Open a machine at an invention workbench in game once; the interface varbits (script 13676) transmit on interaction and this tab reads them.</div></div>';
    } else {
      body.innerHTML = h;
    }
  }
  // Component icon = a cache sprite (not an item icon). Same cache+async pattern as attachBuffIcon.
  function attachCompIcon(el, sid) {
    const cached = SPRITES.get(sid);
    if (cached) { setIconBg(el, cached); return; }
    el.dataset.spr = sid;
    if (!bridge() || !bridge().sprite || SPRITE_PENDING.has(sid)) return;
    SPRITE_PENDING.add(sid);
    (async () => {
      try {
        const url = await rtxData.raw('cache.sprite', sid); SPRITE_PENDING.delete(sid);
        if (url) { SPRITES.set(sid, url); document.querySelectorAll('.comp-icon[data-spr="' + sid + '"]').forEach(n => setIconBg(n, url)); }
      } catch (e) { SPRITE_PENDING.delete(sid); }
    })();
  }
  function renderComponents() {
    const c = $('content');
    let w = $('compWrap');
    if (!w) {
      c.innerHTML = '';
      w = document.createElement('div'); w.id = 'compWrap'; w.className = 'bank-wrap';
      const top = document.createElement('div'); top.className = 'bank-top';
      const inp = document.createElement('input'); inp.id = 'compSearch'; inp.className = 'bank-search';
      inp.type = 'text'; inp.placeholder = 'Search component...'; inp.value = compTerm; inp.spellcheck = false;
      inp.addEventListener('input', () => { compTerm = inp.value; compSig = ''; paintComponents(); });
      const he = document.createElement('label'); he.className = 'comp-toggle';
      const cb = document.createElement('input'); cb.type = 'checkbox'; cb.checked = compHideEmpty;
      cb.addEventListener('change', () => {
        compHideEmpty = cb.checked; compSig = '';
        try { localStorage.setItem('rtxCompHideEmpty', compHideEmpty ? '1' : '0'); } catch (e) {}
        paintComponents();
      });
      he.appendChild(cb); he.appendChild(document.createTextNode('Hide empty'));
      const meta = document.createElement('div'); meta.id = 'compMeta'; meta.className = 'bank-meta';
      top.appendChild(inp); top.appendChild(he); top.appendChild(meta);
      const body = document.createElement('div'); body.id = 'compBody'; body.className = 'mbank-body';
      w.appendChild(top); w.appendChild(body); c.appendChild(w); compSig = '';
    }
    paintComponents();
  }
  function paintComponents() {
    const body = $('compBody'); if (!body) return;
    if (!compVp) { body.innerHTML = '<div class="bank-empty" style="display:flex"><div class="warn" style="background:rgba(255,255,255,0.04);border-color:var(--border);color:var(--text-dim)">Reading...</div></div>'; return; }
    const t = compTerm.trim().toLowerCase();
    const u = k => (compVp[k] || 0) >>> 0;
    const owned = INV_COMPONENTS.filter(c => u(c[1]) > 0).length;
    const sig = t + '|' + compHideEmpty + '|' + INV_COMPONENTS.map(c => u(c[1])).join(',');
    if (sig === compSig) return; compSig = sig;
    const meta = $('compMeta'); if (meta) meta.textContent = owned + ' / ' + INV_COMPONENTS.length + ' owned';
    body.innerHTML = '';
    let curR = null, grid = null;
    for (const c of INV_COMPONENTS) {
      const name = c[0], varp = c[1], sprite = c[2], rarity = c[3], desc = c[4], src = c[5], cnt = u(varp);
      if (compHideEmpty && cnt === 0) continue;
      if (t && name.toLowerCase().indexOf(t) === -1) continue;
      if (rarity !== curR) {
        curR = rarity;
        const h = document.createElement('div'); h.className = 'comp-sec'; h.textContent = COMP_RARITY[rarity] || ('Tier ' + rarity);
        body.appendChild(h);
        grid = document.createElement('div'); grid.className = 'stor-grid'; body.appendChild(grid);
      }
      const cell = document.createElement('div'); cell.className = 'bank-cell stor-cell' + (cnt === 0 ? ' comp-zero' : '');
      const ico = document.createElement('div'); ico.className = 'bank-icon comp-icon'; attachCompIcon(ico, sprite); cell.appendChild(ico);
      if (cnt > 0) { const amt = fmtAmt(cnt); if (amt.t) { const a = document.createElement('span'); a.className = 'bank-amt' + (amt.c ? ' ' + amt.c : ''); a.textContent = amt.t; cell.appendChild(a); } }
      let tip = name + '\n' + (COMP_RARITY[rarity] || ('Tier ' + rarity)) + ' (x' + cnt.toLocaleString() + ')';
      if (desc) tip += '\n\n' + desc;
      if (src) tip += '\n\nObtain by disassembling: ' + src;
      tip += '\n\nSource: live varp ' + varp + ' (count) + cache enum 10742 (def)';
      cell.dataset.tip = tip;
      grid.appendChild(cell);
    }
    if (!body.children.length) body.innerHTML = '<div class="bank-empty" style="display:flex"><div class="warn" style="background:rgba(255,255,255,0.04);border-color:var(--border);color:var(--text-dim)">No components match.</div></div>';
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { fetchComponents, fetchMachinesTab });
registerTab({ id: 'components', render: renderComponents, open: function () { compSig = ''; fetchComponents(); } });
registerTab({ id: 'machines', render: renderMachines, open: function () { fetchMachinesTab(); } });
})();
