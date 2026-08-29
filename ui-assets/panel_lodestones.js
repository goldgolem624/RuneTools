// RuneToolsX panel: Hidey-holes + Lodestones status (plus clue reference tables).
// Spliced inline into client.html; IIFE (window exports + registerTab; see the RTX registry in client.html).
(function () {

  // Hidey-holes: all 58 emote-clue storage holes. Each hole = one 2-bit varbit ([varp, bit]);
  // 0 = not built, 1 = built/empty, 2 = built/filled. Generated table; locations joined by world
  // coordinate, not table order.
  // Lodestones: each is a varbit as varp vp + bit range lo..hi; unlocked when field value >= th.
  // p = destination floor (omitted = 0): Prifddinas and the City of Um render on plane 1, so
  // their map markers must too.
  // Most are single bits (th 1); Bandit Camp and Lunar Isle are quest-gated (Desert Treasure
  // varbit >= 15, Lunar Diplomacy varbit >= 190). Generated table.
  // x/y = teleport-target world tile, kb = lodestone-menu keybind.
  const LODESTONES = [
    {n:"Al Kharid",sp:22245,vp:3,lo:0,hi:0,th:1,x:3297,y:3184,kb:"A"},
    {n:"Anachronia",sp:1080,vp:8595,lo:22,hi:22,th:1,x:5431,y:2338,kb:"Alt+D"},
    {n:"Ardougne",sp:22236,vp:3,lo:1,hi:1,th:1,x:2634,y:3348,kb:"Alt+A"},
    {n:"Ashdale",sp:22255,vp:3,lo:23,hi:23,th:1,x:2474,y:2708,kb:"Shift+A"},
    {n:"Bandit Camp",sp:22246,vp:2151,lo:0,hi:14,th:15,x:3214,y:2954,kb:"Alt+B"},
    {n:"Burthorpe",sp:22238,vp:3,lo:2,hi:2,th:1,x:2899,y:3544,kb:"B"},
    {n:"Canifis",sp:22248,vp:3,lo:15,hi:15,th:1,x:3517,y:3515,kb:"Alt+C"},
    {n:"Catherby",sp:22239,vp:3,lo:3,hi:3,th:1,x:2811,y:3449,kb:"C"},
    {n:"City of Um",sp:30011,vp:7040,lo:1,hi:1,th:1,x:1084,y:1768,p:1,kb:"U"},
    {n:"Draynor",sp:22242,vp:3,lo:4,hi:4,th:1,x:3105,y:3298,kb:"D"},
    {n:"Eagles' Peak",sp:22249,vp:3,lo:16,hi:16,th:1,x:2366,y:3479,kb:"Alt+E"},
    {n:"Edgeville",sp:22244,vp:3,lo:5,hi:5,th:1,x:3067,y:3505,kb:"E"},
    {n:"Falador",sp:22235,vp:3,lo:6,hi:6,th:1,x:2967,y:3403,kb:"F"},
    {n:"Fort Forinthry",sp:27050,vp:10762,lo:6,hi:6,th:1,x:3298,y:3525,kb:"Shift+F"},
    {n:"Fremennik Province",sp:22251,vp:3,lo:17,hi:17,th:1,x:2712,y:3677,kb:"Alt+F"},
    {n:"Karamja",sp:22252,vp:3,lo:18,hi:18,th:1,x:2761,y:3147,kb:"K"},
    {n:"Lumbridge",sp:22233,vp:3,lo:7,hi:7,th:1,x:3233,y:3221,kb:"L"},
    {n:"Lunar Isle",sp:22247,vp:2253,lo:0,hi:19,th:190,x:2088,y:3911,kb:"Alt+L"},
    {n:"Menaphos",sp:31130,vp:7040,lo:0,hi:0,th:1,x:3216,y:2716,kb:"M"},
    {n:"Oo'glog",sp:22250,vp:3,lo:19,hi:19,th:1,x:2532,y:2871,kb:"O"},
    {n:"Port Sarim",sp:22241,vp:3,lo:8,hi:8,th:1,x:3011,y:3215,kb:"P"},
    {n:"Prifddinas",sp:24249,vp:3,lo:24,hi:24,th:1,x:2208,y:3360,p:1,kb:"Alt+P"},
    {n:"Seers' Village",sp:22240,vp:3,lo:9,hi:9,th:1,x:2689,y:3482,kb:"S"},
    {n:"Taverley",sp:22237,vp:3,lo:10,hi:10,th:1,x:2878,y:3442,kb:"T"},
    {n:"Tirannwn",sp:22253,vp:3,lo:20,hi:20,th:1,x:2254,y:3149,kb:"Alt+T"},
    {n:"Varrock",sp:22234,vp:3,lo:11,hi:11,th:1,x:3214,y:3376,kb:"V"},
    {n:"Wendlewick",sp:35675,vp:7040,lo:2,hi:2,th:1,x:3462,y:1520,kb:"Alt+W"},
    {n:"Wilderness Crater",sp:22254,vp:3,lo:21,hi:21,th:1,x:3143,y:3635,kb:"W"},
    {n:"Yanille",sp:22243,vp:3,lo:12,hi:12,th:1,x:2529,y:3094,kb:"Y"},
  ];
  const LODE_VARPS = [...new Set(LODESTONES.map(l => l.vp))];
  // Quick teleports (owner var-capture): varp 5773 bit 0 = vb 28622 quick-charges
  // enabled, bits 1-12 = vb 28623 charge amount.
  const LODE_QUICK_VP = 5773;
  lodeQuick = null;   // {on, n}
  lodeData = null; let lodeFetching = false; let lodeListSig = ''; let lodeFetchAt = 0;
  let lodeFStatus = 0, lodeFSearch = '';   // status 0=all 1=unlocked 2=locked
  async function fetchLodestones(force) {
    if (!bridge() || lodeFetching) return;
    const _t = Date.now(); if (!force && _t - lodeFetchAt < 2000) return; lodeFetchAt = _t;
    lodeFetching = true;
    try {
      let vp = {};
      if (bridge().varps) { try { vp = JSON.parse(await bridge().varps(myPid(), LODE_VARPS.concat(LODE_QUICK_VP).join(','))); } catch (e) {} }
      const qraw = (vp[LODE_QUICK_VP] || 0) >>> 0;
      lodeQuick = { on: (qraw & 1) === 1, n: (qraw >>> 1) & 0xfff };
      lodeData = LODESTONES.map(l => {
        const raw = (vp[l.vp] || 0) >>> 0, width = l.hi - l.lo + 1;
        const mask = width >= 32 ? 0xffffffff : ((1 << width) - 1);
        return { ...l, unlocked: ((raw >>> l.lo) & mask) >= l.th };
      });
    } finally { lodeFetching = false; }
    paneRun('lodestones', renderLodestones);
  }
  function renderLodestones() {
    const c = $('content');
    let wrap = $('lodeWrap');
    if (!wrap) {
      c.innerHTML = ''; lodeListSig = '';
      wrap = document.createElement('div'); wrap.id = 'lodeWrap'; wrap.className = 'pk-wrap'; c.appendChild(wrap);
      const tb = document.createElement('div'); tb.className = 'pet-toolbar';
      const mkchip = (label, val, cur) => { const b = document.createElement('button'); b.className = 'pet-chip' + (val === cur ? ' on' : ''); b.textContent = label; b.dataset.val = val; return b; };
      const stRow = document.createElement('div'); stRow.className = 'pet-chips';
      ['All', 'Unlocked', 'Locked'].forEach((nm, i) => stRow.appendChild(mkchip(nm, i, lodeFStatus)));
      const search = document.createElement('input'); search.className = 'pet-search'; search.id = 'lodeSearch'; search.placeholder = 'Search lodestone\u2026'; search.value = lodeFSearch;
      tb.appendChild(stRow); tb.appendChild(search); wrap.appendChild(tb);
      const qk = document.createElement('div'); qk.id = 'lodeQuick'; qk.className = 'pet-count'; wrap.appendChild(qk);
      const cnt = document.createElement('div'); cnt.id = 'lodeCnt'; cnt.className = 'pet-count'; wrap.appendChild(cnt);
      const list = document.createElement('div'); list.id = 'lodeList'; list.className = 'pet-list'; wrap.appendChild(list);
      tb.addEventListener('click', e => {
        const b = e.target.closest('.pet-chip'); if (!b) return;
        lodeFStatus = +b.dataset.val;
        tb.querySelectorAll('.pet-chip').forEach(x => x.classList.toggle('on', x === b));
        lodeListSig = ''; renderLodeList();
      });
      search.addEventListener('input', () => { lodeFSearch = search.value.toLowerCase(); lodeListSig = ''; renderLodeList(); });
    }
    renderLodeList();
  }
  function renderLodeList() {
    const qk = $('lodeQuick');
    if (qk) qk.innerHTML = !lodeQuick ? ''
      : 'Quick teleport charges: <b style="color:var(--text)">' + lodeQuick.n.toLocaleString() + '</b>'
        + ' <span class="pet-st ' + (lodeQuick.on ? 'q-done' : 'm2') + '" style="margin-left:6px">'
        + (lodeQuick.on ? 'enabled' : 'disabled') + '</span>';
    const list = $('lodeList'); if (!list) return;
    const d = lodeData;
    if (!d) { list.innerHTML = '<div class="empty">Reading\u2026 (be in-world)</div>'; lodeListSig = ''; return; }
    const items = d.filter(l =>
      (lodeFStatus === 0 || (lodeFStatus === 1 ? l.unlocked : !l.unlocked)) &&
      (!lodeFSearch || l.n.toLowerCase().indexOf(lodeFSearch) >= 0));
    const got = d.filter(l => l.unlocked).length;
    const cnt = $('lodeCnt'); if (cnt) cnt.textContent = got + ' / ' + d.length + ' unlocked' + (items.length !== d.length ? '  \u00b7  ' + items.length + ' shown' : '');
    const sig = lodeFStatus + '|' + lodeFSearch + '|' + items.map(l => l.n + (l.unlocked ? '1' : '0')).join(',');
    if (sig === lodeListSig) return;
    lodeListSig = sig;
    list.innerHTML = '';
    if (!items.length) { list.innerHTML = '<div class="empty">No lodestones match.</div>'; return; }
    for (const l of items) {
      const row = document.createElement('div'); row.className = 'pet-row' + (l.unlocked ? '' : ' pet-locked');
      row.dataset.tip = l.n + ' Lodestone\nStatus: ' + (l.unlocked ? 'Unlocked' : 'Locked (not yet activated)') +
        '\nSource: varp ' + l.vp + (l.lo === l.hi ? ' bit ' + l.lo
                                                  : ' bits ' + l.lo + '-' + l.hi) +
        (l.th > 1 ? '\nUnlocked when value ≥ ' + l.th + ' (quest-gated)' : '');
      const ico = document.createElement('div'); ico.className = 'pet-ico';
      if (l.sp > 0) loadSpriteIcon(ico, l.sp);
      row.appendChild(ico);
      const info = document.createElement('div'); info.className = 'pet-info';
      const nm = document.createElement('div'); nm.className = 'pet-nm'; nm.textContent = l.n; info.appendChild(nm);
      row.appendChild(info);
      const st = document.createElement('div');
      st.className = 'pet-st ' + (l.unlocked ? 'pet-yes' : 'pet-no');
      st.textContent = l.unlocked ? 'Unlocked' : 'Locked';
      row.appendChild(st);
      list.appendChild(row);
    }
  }
  const HIDEY = [
    {n:"Crate",t:0,loc:"3rd floor of the East Ardougne windmill",vp:7788,b:26,x:2635,y:3383,p:2,fi:["Tiara","Wood camo top","Emerald ring"]},
    {n:"Beehive",t:0,loc:"Among the Catherby beehives",vp:7789,b:2,x:2762,y:3447,p:0,fi:["Iron armoured boots","Unholy symbol","Steel hatchet"]},
    {n:"Rock",t:0,loc:"At the crossroads north of Draynor Village",vp:7788,b:20,x:3112,y:3292,p:0,fi:["Iron chainbody","Sapphire ring","Shieldbow"]},
    {n:"Sack",t:0,loc:"At the entrance to Het's Oasis",vp:7789,b:20,x:3323,y:3236,p:0,fi:["Studded leather coif","Leather chaps","Iron chainbody"]},
    {n:"Rock",t:0,loc:"At the junction south of Sinclair Mansion",vp:7789,b:14,x:2731,y:3534,p:0,fi:["Leather cowl","Amulet of strength","Iron scimitar"]},
    {n:"Rock",t:0,loc:"By the crossroads north of Rimmington",vp:7789,b:6,x:2960,y:3241,p:0,fi:["Sapphire ring","Leather chaps","Yellow flowers"]},
    {n:"Barrel",t:0,loc:"In Draynor Village market",vp:7788,b:4,x:3078,y:3246,p:0,fi:["Studded chaps","Iron kiteshield","Steel longsword"]},
    {n:"Cart",t:0,loc:"In the Rimmington mine",vp:7788,b:22,x:2973,y:3236,p:0,fi:["Gold necklace","Gold ring","Bronze spear"]},
    {n:"Crate",t:0,loc:"In the Silvarea limestone mine",vp:7788,b:6,x:3376,y:3500,p:0,fi:["Steel pickaxe","Bronze platelegs","Steel med helm"]},
    {n:"Crate",t:0,loc:"Inside Varrock Dig Site Exam Centre",vp:7789,b:16,x:3351,y:3348,p:0,fi:["Ruby amulet","Blue flowers","Leather gloves"]},
    {n:"Potted plant",t:0,loc:"Inside the Falador Party Room",vp:7788,b:28,x:3041,y:3372,p:0,fi:["Steel full helm","Steel platebody","Iron plateskirt"]},
    {n:"Crate",t:0,loc:"Inside the Varrock Palace Library",vp:7788,b:24,x:3217,y:3494,p:0,fi:["Holy symbol","Leather vambraces","Iron warhammer"]},
    {n:"Barrel",t:0,loc:"Inside the shed in Lumbridge Swamp",vp:7788,b:0,x:3204,y:3169,p:0,fi:["Bronze dagger","Iron full helm","Gold ring"]},
    {n:"Crate",t:0,loc:"Near the Entrana ferry in Port Sarim",vp:7788,b:12,x:3042,y:3234,p:0,fi:["Studded leather coif","Steel plateskirt","Sapphire necklace"]},
    {n:"Rock",t:0,loc:"Near the entrance to Keep Le Faye",vp:7789,b:10,x:2756,y:3404,p:0,fi:["Iron platebody","Studded leather coif","Leather gloves"]},
    {n:"Rock",t:0,loc:"Near the entrance to the Fishing Guild",vp:7789,b:8,x:2619,y:3388,p:0,fi:["Bronze chainbody","Sapphire amulet","Emerald ring"]},
    {n:"Dead tree",t:0,loc:"Next to the Draynor Manor fountain",vp:7788,b:16,x:3091,y:3341,p:0,fi:["Bronze full helm","Studded chaps","Iron platebody"]},
    {n:"Rock",t:0,loc:"Next to the Taverley Stone Circle",vp:7788,b:30,x:2922,y:3476,p:0,fi:["Air tiara","Bronze 2h sword","Gold amulet"]},
    {n:"Rock",t:0,loc:"Next to the bridge on the Wizards' Tower island",vp:7788,b:2,x:3099,y:3189,p:0,fi:["Leather gloves","Iron med helm","Emerald ring"]},
    {n:"Rock",t:0,loc:"Next to the monkey pen in Ardougne Zoo",vp:7789,b:4,x:2599,y:3280,p:0,fi:["Studded body","Bronze platelegs","Mud pie"]},
    {n:"Rock",t:0,loc:"On Mudskipper Point",vp:7788,b:10,x:3004,y:3123,p:0,fi:["Gold ring","Leather chaps","Steel mace"]},
    {n:"Rock",t:0,loc:"Outside the Legends' Guild gates",vp:7788,b:8,x:2732,y:3352,p:0,fi:["Oak shieldbow","Iron platelegs","Emerald amulet"]},
    {n:"Sack of items",t:0,loc:"Outside the Lumbridge windmill",vp:7788,b:18,x:3165,y:3302,p:0,fi:["Sapphire necklace","Polar camo legs","Oak shortbow"]},
    {n:"Rocks",t:0,loc:"South of the Al Kharid scorpion mine",vp:7788,b:14,x:3295,y:3282,p:0,fi:["Leather gloves","Leather boots","Polar camo top"]},
    {n:"Tree stump",t:0,loc:"South of the entrance to Fort Forinthry",vp:7789,b:18,x:3299,y:3521,p:0,fi:["Bronze hatchet","Leather chaps","Hard leather body"]},
    {n:"Crate",t:1,loc:"By the ogres in the Combat Training Camp",vp:7790,b:28,x:2521,y:3377,p:0,fi:["Green dragonhide chaps","Green dragonhide body","Steel square shield"]},
    {n:"Rock",t:1,loc:"East of the Barbarian Village bridge",vp:7790,b:14,x:3115,y:3418,p:0,fi:["Mithril full helm","Steel kiteshield","Iron hatchet"]},
    {n:"Tree stump",t:1,loc:"Halfway up the tree in the Gnome Stronghold agility course",vp:7790,b:22,x:2473,y:3418,p:2,fi:["Green dragonhide chaps","Steel kiteshield","Ring of forging"]},
    {n:"Crates",t:1,loc:"Inside Hickton's Archery Emporium in Catherby",vp:7791,b:0,x:2829,y:3455,p:0,fi:["Blood'n'tar snelm (round)","Hard leather body","Silver sickle"]},
    {n:"Barrel",t:1,loc:"Inside the Barbarian Outpost agility training area",vp:7790,b:20,x:2545,y:3556,p:0,fi:["Steel platebody","Maple shortbow","Bronze armoured boots"]},
    {n:"Crate",t:1,loc:"Inside the Yanille bank",vp:7790,b:24,x:2613,y:3088,p:0,fi:["Snakeskin chaps","Iron crossbow","Adamant med helm"]},
    {n:"Crate",t:1,loc:"Inside the entrance of Tai Bwo Wannai village",vp:7790,b:16,x:2793,y:3073,p:0,fi:["Mithril med helm","Green dragonhide chaps","Ring of duelling"]},
    {n:"Crate",t:1,loc:"Mausoleum off the Morytania coast",vp:7790,b:12,x:3506,y:3574,p:0,fi:["Maple shieldbow","Mithril plateskirt"]},
    {n:"Crate",t:1,loc:"Near the entrance to Shantay Pass",vp:7791,b:2,x:3302,y:3130,p:0,fi:["Bruise blue snelm (pointed)","Staff of air"]},
    {n:"Stone blocks",t:1,loc:"Next to the Castle Wars bank",vp:7790,b:18,x:2448,y:3088,p:0,fi:["Ruby amulet","Mithril scimitar","Iron square shield"]},
    {n:"Barrel",t:1,loc:"Next to the east winch at Varrock Dig Site",vp:7790,b:30,x:3379,y:3445,p:0,fi:["Snakeskin boots","Blood'n'tar snelm (pointed)","Iron pickaxe"]},
    {n:"Crate",t:1,loc:"On the top floor of the Kandarin Observatory",vp:7790,b:26,x:2445,y:3161,p:1,fi:["Green dragonhide chaps","Mithril chainbody","Ruby amulet"]},
    {n:"Barrel",t:1,loc:"Outside The Hair Of The Dog Tavern in Canifis",vp:7790,b:10,x:3491,y:3480,p:0,fi:["Mithril platelegs","Spiny helmet","Iron 2h sword"]},
    {n:"Crate",t:2,loc:"Inside the Fishing Guild bank",vp:7789,b:24,x:2585,y:3424,p:0,fi:["Blue dragonhide chaps","Rune warhammer","Elemental shield"]},
    {n:"Potted plant",t:2,loc:"Inside the Shilo Village bank",vp:7790,b:8,x:2857,y:2954,p:0,fi:["Splitbark helm","Mud pie","Rune platebody"]},
    {n:"Rock",t:2,loc:"Inside the Zamorak temple in the eastern Wilderness",vp:7789,b:22,x:3245,y:3610,p:0,fi:["Bronze platelegs","Iron platebody","Blue dragonhide vambraces"]},
    {n:"Rock",t:2,loc:"Near the Gnome Glider on White Wolf Mountain",vp:7790,b:6,x:2845,y:3496,p:1,fi:["Ring of life","Rune hatchet","Mithril platelegs"]},
    {n:"Rock",t:2,loc:"Near the Mountain Camp goat enclosure",vp:7790,b:4,x:2790,y:3667,p:0,fi:["Rune full helm","Fire battlestaff","Blue dragonhide chaps"]},
    {n:"Dead tree",t:2,loc:"Near the Wilderness Bandit camp obelisk",vp:7790,b:0,x:3032,y:3723,p:0,fi:["Iron square shield","Iron pickaxe","Blue dragonhide vambraces"]},
    {n:"Crate",t:2,loc:"Next to Jamila's craft stall in Sophanem",vp:7789,b:30,x:3310,y:2784,p:0,fi:["Adamant 2h sword","Ring of life","Amulet of glory"]},
    {n:"Crate",t:2,loc:"On the top floor of the Kandarin lighthouse",vp:7789,b:26,x:2790,y:3667,p:0,fi:["Blue dragonhide body","Blue dragonhide vambraces"]},
    {n:"Log",t:2,loc:"South side of the Musa Point banana plantation",vp:7790,b:2,x:2914,y:3154,p:0,fi:["Diamond ring","Amulet of power"]},
    {n:"Plant",t:3,loc:"In front of the entrance to the Grand Library of Menaphos",vp:7791,b:14,x:3172,y:2711,p:0,fi:["Asylum surgeon's ring","Scabaras mask"]},
    {n:"Barrel",t:3,loc:"Inside the Green Ghost Inn in Port Phasmatys",vp:7791,b:12,x:3676,y:3494,p:0,fi:["Iban's staff","Ghostly cloak","Cavaliers"]},
    {n:"Barrel",t:3,loc:"Inside the Invention Guild",vp:7791,b:16,x:6176,y:1050,p:0,fi:["Lab coat top","Lab coat legs","Staff of light"]},
    {n:"Barrel",t:3,loc:"Inside the Lumbridge Fishing Supplies shop",vp:7791,b:22,x:3198,y:3258,p:0,fi:["Demon slayer gloves","Cape of legends","Boater"]},
    {n:"Rock",t:3,loc:"Inside the celestial dragon dungeon on Dragontooth Island",vp:7791,b:20,x:2277,y:5966,p:0,fi:["Dragon Rider amulet","Dragon defender","Dragon mask"]},
    {n:"Crates",t:3,loc:"Near the entrance to the Morytania Slayer Tower",vp:7791,b:10,x:3427,y:3541,p:0,fi:["Imphide hood","Prifddinian musician's robe top","Amulet of magic"]},
    {n:"Barrel",t:3,loc:"Next to the Edgeville Monastery garden",vp:7791,b:26,x:3056,y:3499,p:0,fi:["Holy Cithara","Saradomin's murmur","Ring of devotion"]},
    {n:"Potted plant",t:3,loc:"Next to the start of the Hefin District agility course",vp:7791,b:24,x:2181,y:3402,p:1,fi:["Prifddinian worker's trousers","Berserker ring","Ancient staff"]},
    {n:"Table",t:3,loc:"Next to the sulphur pit in the TzHaar City",vp:7791,b:4,x:4638,y:5106,p:0,fi:["Fire cape","Toktz-ket-xil","Spork"]},
    {n:"Water barrel",t:3,loc:"On the northern Waiko docks",vp:7791,b:8,x:1818,y:11653,p:0,fi:["Culinaromancer's gloves 10","Infinity boots","Dark bow"]},
    {n:"Rock",t:3,loc:"South west of the Charm Sprite cracked dolmen",vp:7791,b:6,x:2398,y:3369,p:0,fi:["Enhanced yaktwee stick","Dagon'hai hat","Amulet of ranging"]}
  ];   // HIDEY_GEN_END
  const HIDEY_VARPS = [...new Set(HIDEY.map(h => h.vp))];
  const HIDEY_TIERS = ['Easy', 'Medium', 'Hard', 'Master'];
  // Build cost is per clue tier (one hidey-hole at a time).
  const HIDEY_BUILD = [
    '4 planks, 10 steel nails (Construction 27)',
    '4 oak planks, 10 steel nails (Construction 42)',
    '4 teak planks, 10 steel nails (Construction 55)',
    '4 mahogany planks, 10 steel nails, 1 gold leaf (Construction 88)',
  ];
  // Structured form of HIDEY_BUILD (same quantities) for aggregating a shopping list; keep the two
  // in sync. MAT_ORDER fixes the display order so mixed-tier totals read planks (by wood), then
  // nails, then gold leaf.
  const HIDEY_BUILD_MAT = [
    { 'Plank': 4, 'Steel nails': 10 },
    { 'Oak plank': 4, 'Steel nails': 10 },
    { 'Teak plank': 4, 'Steel nails': 10 },
    { 'Mahogany plank': 4, 'Steel nails': 10, 'Gold leaf': 1 },
  ];
  const HIDEY_MAT_ORDER = ['Plank', 'Oak plank', 'Teak plank', 'Mahogany plank', 'Steel nails', 'Gold leaf'];
  // Treasure Trails clue map: every clue-scroll item id -> the action its def encodes (decoded from
  // items.json extra params: clue_coordinates/clue_tele_location -> dig tile, mediumClue_NPCId ->
  // talk, elite_clue_scan_location_enum -> scan, medium_clue_keyItemId -> key item; else
  // emote/cryptic text clue).
  const CLUE_TIERS = ["Easy","Medium","Hard","Elite","Master","Special"];
  // Emote-clue reference (62 clues): {t:tier 0-3, x,y,p:emote tile, em:emote(s),
  // it:[items], da:double-agent, hh:[name,x,y,plane]|null, tx:clue text}.
  const EMOTE_CLUES = [{"t":0,"x":3204,"y":3169,"p":0,"em":"Dance","it":["Bronze Dagger","Iron full helm","Gold ring"],"da":false,"hh":["Barrel",3204,3169,0],"tx":"Dance in the shack in Lumbridge Swamp. Equip a bronze dagger, iron full helm and a gold ring."},{"t":0,"x":3158,"y":3300,"p":0,"em":"Think","it":["Sapphire necklace","Polar camo legs","Oak shortbow"],"da":false,"hh":["Sack of items",3165,3302,0],"tx":"Think in the middle of the wheat field by the Lumbridge mill. Equip a sapphire necklace, polar camo legs and an oak shortbow."},{"t":0,"x":2741,"y":3538,"p":0,"em":"Laugh","it":["Leather cowl","Amulet of strength","Iron scimitar"],"da":false,"hh":["Rock",2731,3534,0],"tx":"Laugh at the crossroads south of Sinclair Mansion. Equip a leather cowl, amulet of strength and iron scimitar."},{"t":0,"x":3372,"y":3500,"p":0,"em":"Panic","it":["Bronze platelegs","Steel pickaxe","Steel helm"],"da":false,"hh":["Crate",3376,3500,0],"tx":"Panic in the limestone mine. Equip bronze platelegs, a steel pickaxe and a steel helmet."},{"t":0,"x":2677,"y":3169,"p":0,"em":"Panic","it":["Nothing"],"da":false,"hh":null,"tx":"Panic on the pier where you catch the Fishing Trawler. Have nothing equipped at all when you do."},{"t":0,"x":2603,"y":3279,"p":0,"em":"Raspberry","it":["Studded leather body","Bronze platelegs","Mud pie"],"da":false,"hh":["Rock",2599,3280,0],"tx":"Blow a raspberry at the monkey cage in Ardougne Zoo. Equip a studded leather body, bronze platelegs and a mud pie."},{"t":0,"x":3103,"y":3199,"p":0,"em":"Clap","it":["Iron helmet","Emerald ring","Leather gloves"],"da":false,"hh":["Rock",3099,3189,0],"tx":"Clap on the causeway to the Wizards' Tower. Equip an iron helmet, emerald ring and leather gloves."},{"t":0,"x":2955,"y":3242,"p":0,"em":"Twirl","it":["Sapphire ring","Yellow flowers","Leather chaps"],"da":false,"hh":["Rock",2960,3241,0],"tx":"Twirl at the crossroads north of Rimmington. Equip a Sapphire ring, yellow flowers and leather chaps."},{"t":0,"x":2761,"y":3402,"p":0,"em":"Raspberry","it":["Studded leather coif","Iron platebody","Leather gloves"],"da":false,"hh":["Rock",2756,3404,0],"tx":"Blow raspberries outside the entrance to Keep Le Faye. Equip a studded leather coif, iron platebody and leather gloves."},{"t":0,"x":3047,"y":3236,"p":0,"em":"Cheer","it":["Studded Leather coif","Steel plateskirt","Sapphire necklace"],"da":false,"hh":["Crate",3042,3234,0],"tx":"Cheer for the monks at Port Sarim. Equip a studded leather coif, steel plateskirt and a sapphire necklace."},{"t":0,"x":2994,"y":3119,"p":0,"em":"Wave","it":["Gold ring","Leather chaps","Steel mace"],"da":false,"hh":["Rock",3004,3123,0],"tx":"Wave on Mudskipper Point. Equip a gold ring, leather chaps and a steel mace."},{"t":0,"x":2980,"y":3241,"p":0,"em":"Shrug","it":["Gold necklace","Gold ring","Bronze spear"],"da":false,"hh":["Cart",2973,3236,0],"tx":"Shrug in the mine near Rimmington. Equip a gold necklace, gold ring and a bronze spear."},{"t":0,"x":3356,"y":3346,"p":0,"em":"Clap","it":["Ruby amulet","Blue flowers","Leather gloves"],"da":false,"hh":["Crate",3351,3348,0],"tx":"Clap in the main exam room of the Exam Centre. Equip a ruby amulet, blue flowers and leather gloves."},{"t":0,"x":3211,"y":3494,"p":0,"em":"Yawn","it":["Holy symbol","Leather vamraces","Iron warhammer"],"da":false,"hh":["Crate",3217,3494,0],"tx":"Yawn in Varrock Palace library. Equip a holy symbol, leather vambraces and an iron warhammer."},{"t":0,"x":3046,"y":3379,"p":0,"em":"Dance","it":["Steel full helmet","Steel platebody","Iron plateskirt"],"da":false,"hh":["Potted plant",3041,3372,0],"tx":"Dance in the Party Room. Equip a steel full helmet, steel platebody and an iron plateskirt."},{"t":0,"x":3089,"y":3336,"p":0,"em":"Twirl","it":["Iron platebody","Studden chaps","Bronze full helm"],"da":false,"hh":["Dead tree",3091,3341,0],"tx":"Twirl in Draynor Manor by the fountain. Equip an iron platebody, studded chaps and a bronze full helm."},{"t":0,"x":2760,"y":3444,"p":0,"em":"Jump for Joy","it":["Iron boots","Unholy smybol","Steel hatchet"],"da":false,"hh":["Beehive",2762,3447,0],"tx":"Jump for joy at the beehives. Equip iron boots, an unholy symbol and a steel hatchet."},{"t":0,"x":3300,"y":3304,"p":0,"em":"Headbang","it":["Polar Camo Top","Leather Gloves","Leather Boots"],"da":false,"hh":["Rocks",3295,3282,0],"tx":"Headbang in the mine north of Al-kharid. Equip a Polar Camo Top, Leather Gloves and Leather Boots."},{"t":0,"x":2729,"y":3348,"p":0,"em":"Bow or Curtsy","it":["Iron platelegs","Emerald Amulet","Oak Shieldbow"],"da":false,"hh":["Rock",2732,3352,0],"tx":"Bow or curtsy outside the entrance to the Legends' Guild. Equip iron platelegs, an emerald amulet and an oak shieldbow."},{"t":0,"x":2208,"y":4960,"p":0,"em":"Cheer","it":["Nothing"],"da":false,"hh":null,"tx":"Cheer in the centre of the Burthorpe Games Room. Have nothing equipped at all when you do."},{"t":0,"x":3110,"y":3295,"p":0,"em":"Dance","it":["Iron Chainbody","Sapphire Ring","Shieldbow"],"da":false,"hh":["Rock",3112,3292,0],"tx":"Dance at the crossroads north of Draynor. Equip an iron chainbody, sapphire ring and a shieldbow."},{"t":0,"x":2614,"y":3386,"p":0,"em":"Jig","it":["Emerald Ring","Sapphire Amulet","Bronze Chainbody"],"da":false,"hh":["Rock",2619,3388,0],"tx":"Dance a jig by the entrance to the Fishing Guild. Equip an emerald ring, sapphire amulet, and a bronze chainbody."},{"t":0,"x":3323,"y":3234,"p":0,"em":"Bow or Curtsy","it":["Iron Chainbody","Leather Chaps","Leather Coif"],"da":false,"hh":["Sack",3323,3236,0],"tx":"Bow or curtsy at the entrance to Het's Oasis. Equip an iron chainbody, leather chaps and a studded leather coif."},{"t":0,"x":3304,"y":3525,"p":0,"em":"Cry","it":["Hard Leather Body","Leather Chaps","Bronze Hatchet"],"da":false,"hh":["Tree stump",3299,3521,0],"tx":"Cry outside the south gates of Fort Forinthry. Equip a hard leather body, leather chaps and a bronze hatchet."},{"t":0,"x":2633,"y":3384,"p":2,"em":"Clap","it":["Emerald Ring","Wood Camo Top","Unenchanted Tiara"],"da":false,"hh":["Crate",2635,3383,2],"tx":"Clap on the top level of the mill north of East Ardougne. Equip an emerald ring, wood camo top and an unenchanted tiara."},{"t":0,"x":3081,"y":3251,"p":0,"em":"Yawn","it":["Iron Kiteshield","Steel Longsword","Studded Leather Chaps"],"da":false,"hh":["Barrel",3078,3246,0],"tx":"Yawn in Draynor Marketplace. Equip an iron kiteshield, steel longsword and studded leather chaps."},{"t":0,"x":2922,"y":3482,"p":0,"em":"Cheer","it":["Air Tiara","Bronze 2h Sword","Gold Amulet"],"da":false,"hh":["Rock",2922,3476,0],"tx":"Cheer at the Druids' Circle. Equip an air tiara, bronze two-handed sword and a gold amulet."},{"t":1,"x":3379,"y":3444,"p":0,"em":"Beckon/Bow or Curtsy","it":["Pointed Red and Black Snelm","Snakeskin Boots","Iron Pickaxe"],"da":false,"hh":["Barrel",3379,3445,0],"tx":"Beckon at the Digsite, near the eastern winch. Bow or curtsy before you talk to me. Equip a pointed red and black snelm, snakeskin boots, and an iron pickaxe."},{"t":1,"x":3503,"y":3574,"p":0,"em":"Panic/Wave","it":["Mithril Plateskirt","Maple Shieldbow","No boots"],"da":false,"hh":["Crate",3506,3574,0],"tx":"Panic by the mausoleum in Morytania. Wave before you speak to me. Equip a mithril plateskirt, a maple shieldbow and no boots."},{"t":1,"x":3305,"y":3123,"p":0,"em":"Jig/Bow or Curtsy","it":["Pointed Blue Snelm","Air Staff"],"da":false,"hh":["Crate",3302,3130,0],"tx":"Dance a jig under Shantay's Awning. Bow or curtsy before you talk to me. Equip a pointed blue snail helmet and an air staff."},{"t":1,"x":2441,"y":3166,"p":1,"em":"Think/Twirl","it":["Mithril Chainbody","Green Dragonhide Chaps","Ruby Amulet"],"da":false,"hh":["Crate",2445,3161,1],"tx":"Think under the lens in the Observatory. Twirl before you talk to me. Equip a mithril chainbody, green dragonhide chaps and a ruby amulet."},{"t":1,"x":3106,"y":3421,"p":0,"em":"Twirl/Salute","it":["Iron Hatchet","Steel Kiteshield","Mithril Full Helm"],"da":false,"hh":["Rock",3115,3418,0],"tx":"Twirl on the bridge by Barbarian Village. Salute before you talk to me. Equip an iron hatchet, steel kiteshield and a mithril full helmet."},{"t":1,"x":2612,"y":3093,"p":0,"em":"Jump for Joy/Jig","it":["Iron Crossbow","Adamant Med Helmet","Snakeskin Chaps"],"da":false,"hh":["Crate",2613,3088,0],"tx":"Jump for joy in Yanille bank. Dance a jig before you talk to me. Equip an iron crossbow, adamant helmet and snakeskin chaps."},{"t":1,"x":3495,"y":3489,"p":0,"em":"Dance/Bow or Curtsy","it":["Spine Helm","Mithril Platelegs","Iron two-handed sword"],"da":false,"hh":["Barrel",3491,3480,0],"tx":"Dance in the centre of Canifis. Bow or curtsy before you talk to me. Equip a spiny helmet, mithril platelegs and an iron two-handed sword."},{"t":1,"x":2542,"y":3551,"p":0,"em":"Cheer/Headbang","it":["Steel Platebody","Maple Shortbow","Bronze Boots"],"da":false,"hh":["Barrel",2545,3556,0],"tx":"Cheer in the Barbarian Agility Arena. Headbang before you talk to me. Equip a steel platebody, maple shortbow and bronze boots."},{"t":1,"x":2475,"y":3420,"p":2,"em":"Cry/No","it":["Green Dragonhide Chaps","Steel Kiteshield","Ring of Forging"],"da":false,"hh":["Tree stump",2473,3418,2],"tx":"Cry on the platform of the south-west tree in the Gnome Agility Arena. Indicate 'no' before you talk to me. Equip a steel kiteshield, ring of forging and green dragonhide chaps."},{"t":1,"x":2795,"y":3066,"p":0,"em":"Beckon/Clap","it":["Green dragonhide chaps","Ring of duelling","Mithril helm"],"da":false,"hh":["Crate",2793,3073,0],"tx":"Beckon in Tai Bwo Wannai. Clap before you talk to me. Equip green dragonhide chaps, a ring of duelling and a mithril helmet."},{"t":1,"x":2529,"y":3376,"p":0,"em":"Cheer/Angry","it":["Green dragonhide body","Green dragonhide chaps","Steel square shield"],"da":false,"hh":["Crate",2521,3377,0],"tx":"Cheer in the Ogre Pen in the Training Camp. Show you are angry before you talk to me. Equip a green dragonhide body and chaps, and a steel squareshield."},{"t":1,"x":2827,"y":3456,"p":0,"em":"Cry/Bow or Curtsy","it":["Round red and black snelm","Hard leather body","Unblessed silver sickle"],"da":false,"hh":["Crates",2829,3455,0],"tx":"Cry in the Catherby archery shop. Bow or curtsy before you talk to me. Equip a round red and black snelm, a hard leather body and an unblessed silver sickle."},{"t":1,"x":2443,"y":3090,"p":0,"em":"Yawn/Shrug","it":["Ruby amulet","Mithril scimitar","Iron square shield"],"da":false,"hh":["Stone blocks",2448,3088,0],"tx":"Yawn in the Castle Wars lobby. Shrug before you talk to me. Equip a ruby amulet, mithril scimitar and an iron square shield."},{"t":2,"x":3617,"y":3489,"p":0,"em":"Panic","it":["Nothing"],"da":true,"hh":null,"tx":"Panic in the heart of the Haunted Woods. Beware of double agents! Have no items equipped when you do."},{"t":2,"x":2794,"y":3672,"p":0,"em":"Laugh","it":["Rune full helm","Blue dragonhide chaps","Fire battlestaff"],"da":true,"hh":["Rock",2790,3667,0],"tx":"Laugh in the Jokul's tent in the Mountain Camp. Beware of double agents! Equip a rune full helmet, blue dragonhide chaps and a fire battlestaff."},{"t":2,"x":2849,"y":3494,"p":1,"em":"Panic","it":["Mithril platelegs","Ring of life","Rune hatchet"],"da":true,"hh":["Rock",2845,3496,1],"tx":"Panic by the pilot on White Wolf Mountain. Beware of double agents! Equip mithril platelegs, a ring of life, and a rune hatchet."},{"t":2,"x":2922,"y":3165,"p":0,"em":"Salute","it":["Diamond ring","Amulet of power","Nothing on your chest and legs"],"da":true,"hh":["Log",2914,3154,0],"tx":"Salute in the banana plantation. Beware of double agents! Equip a diamond ring, amulet of power and nothing on your chest and legs."},{"t":2,"x":3295,"y":2783,"p":0,"em":"Dance","it":["Ring of life","Uncharged amulet of glory","Adamant 2h sword"],"da":true,"hh":["Crate",3310,2784,0],"tx":"Dance at the cat-doored pyramid in Sophanem. Beware of double agents! Equip a ring of life, an amulet of glory and an adamant two-handed sword."},{"t":2,"x":2587,"y":3422,"p":0,"em":"Raspberry","it":["Elemental shield","Blue dragonhide chaps","Rune warhammer"],"da":true,"hh":["Crate",2585,3424,0],"tx":"Blow a raspberry in the Fishing Guild bank. Beware of double agents! Equip an elemental shield, blue dragonhide chaps and a rune warhammer."},{"t":2,"x":2853,"y":2954,"p":0,"em":"Blow kiss","it":["Splitbark helmet","Mud pie","Rune platebofy"],"da":true,"hh":["Potted plant",2857,2954,0],"tx":"Blow a kiss between the tables in Shilo Village bank. Beware of double agents! Equip a splitbark helm, mud pie and rune platebody."},{"t":2,"x":2509,"y":3641,"p":2,"em":"Bow or Curtsy","it":["Blue dragonhide body","Blue dragon vambraces","No jewellry"],"da":true,"hh":["Crate",2512,3639,2],"tx":"Bow or curtsy at the top of the lighthouse. Beware of double agents! Equip a blue dragonhide body, blue dragonhide vambraces and no jewellery."},{"t":2,"x":3036,"y":3733,"p":0,"em":"Yawn","it":["Iron square shield","Blue dragonhide vambraces","Iron pickaxe"],"da":true,"hh":["Dead tree",3032,3723,0],"tx":"Yawn near the Wilderness Bandit camp obelisk. Beware of double agents! Equip an iron square shield, blue dragon vambraces and an iron pickaxe."},{"t":2,"x":3240,"y":3609,"p":0,"em":"Shrug","it":["Bronze platelegs","Iron platebody","Blue dragonhide vambraces"],"da":true,"hh":["Rock",3245,3610,0],"tx":"Shrug in the Zamorak temple, found in the eastern Wilderness. Beware of double agents! Equip bronze platelegs, an iron platebody and blue dragonhide vambraces."},{"t":3,"x":4637,"y":5111,"p":0,"em":"Cheer","it":["Fire Cape","Toktz-Ket-Xil","Spork"],"da":true,"hh":["Table",4638,5106,0],"tx":"Cheer by the sulphur pit in the TzHaar City. Beware of double agents! Equip a fire cape, a Toktz-ket-xil and a spork."},{"t":3,"x":3424,"y":3558,"p":0,"em":"Headbang","it":["Imp-hide Hood","Prifddinian Musician's Robe Top","Amulet of Magic"],"da":true,"hh":["Crates",3427,3541,0],"tx":"Headbang inside the Slayer Tower. Beware of double agents! Equip an imp-hide hood, a Prifddinian musician's robe top and an amulet of magic."},{"t":3,"x":3677,"y":3495,"p":0,"em":"Laugh","it":["Iban's Staff","Ghostly Cloak","Cavalier"],"da":true,"hh":["Barrel",3676,3494,0],"tx":"Dare to laugh in the Green Ghost inn at Port Phasmatys. Beware of double agents! Equip an Iban's staff, a ghostly cloak and a cavalier."},{"t":3,"x":2272,"y":5985,"p":0,"em":"Raspberry","it":["Any Dragon Mask","Dragon Rider Amulet","Dragon Defender"],"da":true,"hh":["Rock",2277,5966,0],"tx":"Blow a Raspberry at the celestial dragons on Dragontooth Isle. Beware of double agents! Equip a dragon mask, a Dragon Rider amulet and a dragon defender."},{"t":3,"x":3175,"y":2710,"p":0,"em":"Wave","it":["Asylum Surgeon's Ring","Scabaras Mask"],"da":true,"hh":["Plant",3172,2711,0],"tx":"Wave in front of the entrance to the Grand Library of Menaphos. Beware of double agents! Equip an asylum surgeon's ring and the Scabaras mask."},{"t":3,"x":3052,"y":3505,"p":0,"em":"Jig","it":["Holy Cithara","Saradomin's Murmur","Ring of Devotion"],"da":true,"hh":["Barrel",3056,3499,0],"tx":"Jig in the Edgeville Monastery garden. Beware of double agents! Equip a holy cithara, Saradomin's murmur and a ring of devotion."},{"t":3,"x":2405,"y":3374,"p":0,"em":"Bow or Curtsy","it":["Enhances Yaktwee Stick","Dagon-Hai Hat","Amulet of Ranging"],"da":true,"hh":["Rock",2398,3369,0],"tx":"Bow or curtsy at the charm sprite hunter area. Beware of double agents! Equip an enhanced yaktwee stick, a Dagon-hai hat and an amulet of ranging."},{"t":3,"x":2189,"y":3404,"p":1,"em":"Think","it":["Ancient Staff","Prifddinian Worker's Trousers","Berserker Ring"],"da":true,"hh":["Potted plant",2181,3402,1],"tx":"Think in the Hefin district of Prifddinas. Beware of double agents! Equip an ancient staff, Prifddinian worker's trousers and a berserker ring."},{"t":3,"x":3197,"y":3255,"p":0,"em":"Shrug","it":["Demon Slayer Gloves","Any Boater","Cape of Legends"],"da":true,"hh":["Barrel",3198,3258,0],"tx":"Shrug in the Lumbridge Fishing Supplies shop. Beware of double agents! Equip demon slayer gloves, a boater and the cape of legends."},{"t":3,"x":6169,"y":1057,"p":0,"em":"Idea","it":["Lab Coat Top","Lab Coat Legs","Staff of Light"],"da":true,"hh":["Barrel",6176,1050,0],"tx":"Have an idea inside the Invention Guild. Beware of double agents! Equip a lab coat top, lab coat legs and a staff of light."},{"t":3,"x":1196,"y":7383,"p":0,"em":"Dance","it":["Dark bow","Infinity boots","Culinaromancer gloves 10"],"da":true,"hh":["Water barrel",1818,11653,0],"tx":"Dance on an Uncharted Isle. Beware of double agents! Equip a dark bow, some infinity boots and culinaromancer gloves 10."},{"t":3,"x":2277,"y":3327,"p":1,"em":"Salute","it":["Nothing"],"da":true,"hh":null,"tx":"Salute in the Max Guild Garden. Beware of double agents! Have no items equipped when you do."}];
  // Cryptic-clue reference (95): {t:tier, x,y,p:target tile, act:talk|search|dig,
  // tgt:npc/object, d:desc, tx:clue text}.
  const CRYPTIC_CLUES = [{"t":2,"x":3156,"y":3241,"p":0,"act":"talk","tgt":"Foreman George","d":"near the sandcastles","tx":"He's got a rebuildathon going on, and an endless stream of important visitors."},{"t":2,"x":3170,"y":3255,"p":0,"act":"talk","tgt":"Sheldon","d":"at the northern entrance","tx":"He's got one hat, two hat, three hat, many hats!"},{"t":2,"x":3186,"y":3233,"p":0,"act":"talk","tgt":"Wellington","d":"at the fishing spots","tx":"He's named after a boot, and carrying nets on his shoulder."},{"t":2,"x":3171,"y":3216,"p":0,"act":"talk","tgt":"Sarah","d":"at the southern entrance","tx":"She can be trusted, she isn't shy and she likes ducks."},{"t":2,"x":3165,"y":3215,"p":0,"act":"talk","tgt":"Flo","d":"at the southern entrance","tx":"She doesn't sell sea shells, but does share common ground with the feather of fletching."},{"t":2,"x":3167,"y":3257,"p":0,"act":"talk","tgt":"Lifeguard","d":"at the northern entrance","tx":"Some say he guards their life, others say he sits around watching beach balls roll around."},{"t":2,"x":3144,"y":3229,"p":0,"act":"talk","tgt":"Palmer","d":"at the western entrance","tx":"Some say he's got the beach in the palm of his hand. Others think he's just excited about coconuts."},{"t":0,"x":2612,"y":3306,"p":1,"act":"search","tgt":"Crate","d":"","tx":"A crate found in the tower of a church is your next location. A crate in the tower of a church is your next location."},{"t":0,"x":3028,"y":3218,"p":0,"act":"talk","tgt":"Captain Tobias.","d":"at Port Sarim","tx":"One of the sailors in Port Sarim is your next destination."},{"t":0,"x":3375,"y":3275,"p":0,"act":"talk","tgt":"Jeed","d":"north of Het's Oasis","tx":"Someone watching Het's Oasis is your next destination."},{"t":1,"x":3256,"y":3487,"p":0,"act":"search","tgt":"Closed Chest","d":"","tx":"You'll need to look for a city with a central fountain. Look for a locked chest in the city's chapel."},{"t":1,"x":3056,"y":3497,"p":0,"act":"search","tgt":"Drawers","d":"","tx":"The socks in these drawers are holier than thine, according to the tonsured owners."},{"t":1,"x":3113,"y":3153,"p":2,"act":"search","tgt":"Cupboard","d":"","tx":"Probably filled with wizards' socks."},{"t":1,"x":2800,"y":3074,"p":0,"act":"search","tgt":"Crate","d":"","tx":"In a village made of bamboo, look for some crates under one of the houses."},{"t":1,"x":2770,"y":3172,"p":0,"act":"search","tgt":"Crate","d":"","tx":"The owner of this crate has a hunch that he put more than fish inside."},{"t":1,"x":2575,"y":3326,"p":1,"act":"search","tgt":"Drawers","d":"","tx":"In a town where the guards are armed with maces, search the upstairs rooms of the public house."},{"t":1,"x":2764,"y":3273,"p":0,"act":"search","tgt":"Crate","d":"","tx":"Try not to step on any aquatic nasties while searching this crate."},{"t":1,"x":2399,"y":4471,"p":0,"act":"search","tgt":"Crate","d":"","tx":"After a hard day of spraying back the vegetation, you mite want to pop to the nearby forge and search the crates."},{"t":1,"x":3366,"y":3319,"p":1,"act":"search","tgt":"Bookcase","d":"","tx":"For any aspiring mage, I'm sure searching this bookcase will be a rewarding experience."},{"t":1,"x":3498,"y":3507,"p":0,"act":"search","tgt":"Crate","d":"","tx":"A town with a different sort of night-life is your destination. Search for some crates in one of the houses."},{"t":1,"x":2524,"y":3438,"p":0,"act":"search","tgt":"Hay bales","d":"","tx":"Hay! Stop for a bit and admire the scenery, just like the tourism promoter says."},{"t":1,"x":2905,"y":3189,"p":0,"act":"search","tgt":"Crate","d":"","tx":"North of the best monkey restaurant on Karamja, look for the centre of the triangle of boats and search there."},{"t":1,"x":2928,"y":3552,"p":0,"act":"search","tgt":"Drawers","d":"","tx":"Go to the village being attacked by trolls and search the drawers in one of the houses."},{"t":1,"x":2709,"y":3478,"p":0,"act":"search","tgt":"Drawers","d":"","tx":"In a town where everyone has perfect vision, seek some locked drawers in a house that sits opposite a workshop."},{"t":1,"x":2519,"y":3259,"p":0,"act":"search","tgt":"Crates","d":"","tx":"Being this far north has meant that these crates have escaped being battled over."},{"t":1,"x":2656,"y":3160,"p":0,"act":"dig","tgt":"","d":"in front of the door","tx":"After trawling for bars, go to the nearest place to smith them and dig by the door."},{"t":1,"x":3280,"y":2787,"p":0,"act":"search","tgt":"Crate","d":"","tx":"Sophind yourself some treasure by searching these boxes."},{"t":1,"x":3358,"y":2971,"p":0,"act":"dig","tgt":"","d":"next to the well","tx":"Dig here if you are not feeling too well after travelling through the desert. Ali heartily recommends it."},{"t":1,"x":3155,"y":5727,"p":0,"act":"search","tgt":"Sacks","d":"","tx":"You'll need to have Doug Deep into the distant past to get to these sacks"},{"t":1,"x":2671,"y":3437,"p":0,"act":"search","tgt":"Crate","d":"","tx":"This crate holds a better reward than a broken arrow."},{"t":1,"x":2513,"y":3041,"p":0,"act":"dig","tgt":"","d":"between the rocks","tx":"The rock cakes to the south are definitely more edible than the two rocks I buried the treasure between."},{"t":1,"x":2809,"y":3165,"p":1,"act":"search","tgt":"Drawers","d":"","tx":"Search the upstairs drawers of a house in a village where pirates are known to have a good time."},{"t":1,"x":3000,"y":3110,"p":0,"act":"dig","tgt":"","d":"on top of the starfish","tx":"Don't skip here, it's too muddy. You'll feel like a star if you dig here, though."},{"t":1,"x":3289,"y":3022,"p":0,"act":"search","tgt":"Crate","d":"","tx":"This crate is mine, all mine, even if it is in the middle of the desert."},{"t":1,"x":3356,"y":3507,"p":0,"act":"dig","tgt":"","d":"in the western shed","tx":"The treasure is buried in a small building full of bones. Here's a Hint: It's not near a graveyard."},{"t":1,"x":3644,"y":3494,"p":0,"act":"dig","tgt":"","d":"next to the mushroom","tx":"By the town of the dead, walk south down a rickety bridge, then dig near the spotted fungus."},{"t":1,"x":2811,"y":3160,"p":0,"act":"search","tgt":"Cupboard","d":"","tx":"This cupboard has treasure, pirate pots and corsair cutlery!"},{"t":1,"x":3354,"y":3349,"p":0,"act":"search","tgt":"Closed chest","d":"","tx":"The dead, red dragon watches over this chest. He must really dig the view."},{"t":1,"x":2691,"y":3508,"p":0,"act":"search","tgt":"Crate","d":"","tx":"This crate clearly marks the end of the line for coal."},{"t":1,"x":2512,"y":3641,"p":1,"act":"search","tgt":"Drawers","d":"","tx":"Go to this building to be illuminated, and check the drawers while you are there."},{"t":1,"x":2614,"y":3204,"p":0,"act":"search","tgt":"Crate","d":"","tx":"Find a crate close to the monks that like to paaarty!"},{"t":1,"x":2593,"y":3108,"p":1,"act":"search","tgt":"Closed chest","d":"","tx":"In a town where wizards are known to gather, search upstairs in a large house to the north."},{"t":1,"x":2698,"y":9684,"p":0,"act":"search","tgt":"Chest","d":"","tx":"This temple is rather sluggish. The chest just inside the entrance, however, is filled with goodies."},{"t":1,"x":2397,"y":3355,"p":0,"act":"search","tgt":"Gnome crates","d":"","tx":"The gnomes' nearby cart must have collapsed under the weight of all the treasure in these boxes!"},{"t":1,"x":3176,"y":2916,"p":0,"act":"dig","tgt":"","d":"west of the cart","tx":"Brush off the sand and dig in the quarry. There is a wheely handy barrow to the east. Don't worry, it's coal to dig there - in fact, it's all oclay."},{"t":1,"x":2438,"y":3165,"p":0,"act":"search","tgt":"Crate","d":"","tx":"Observe: In the crate just North of the stairs leading down, you will find the answer."},{"t":1,"x":2610,"y":3324,"p":1,"act":"search","tgt":"Drawers","d":"","tx":"In a town where thieves steal from stalls, search for some drawers in the upstairs of a house near the bank."},{"t":1,"x":2992,"y":3178,"p":0,"act":"dig","tgt":"","d":"west of the church near Port Sarim","tx":"While a sea view is nice, it seems this church has not seen visitors in a while. Dig outside the rim of the round window for a reward."},{"t":2,"x":3488,"y":3287,"p":0,"act":"dig","tgt":"","d":"at the centre of Mort'ton","tx":"Covered in shadows, the centre of the circle is where you will find the answer."},{"t":2,"x":2958,"y":3514,"p":0,"act":"talk","tgt":"General Bentnoze","d":"in Goblin Village","tx":"Generally speaking, his nose was very bent","ch":"slider"},{"t":2,"x":2671,"y":3416,"p":0,"act":"search","tgt":"Haystack","d":"","tx":"If you look closely enough, it seems that the archers have lost more than their needles."},{"t":2,"x":2187,"y":3285,"p":1,"act":"talk","tgt":"Lord Iorwerth","d":"in Prifddinas","tx":"There is no 'worthier' lord.","ch":"slider"},{"t":2,"x":3058,"y":3485,"p":0,"act":"talk","tgt":"Abbot Langley","d":"at the Edgeville Monastery","tx":"I am head of the abbey with a cold breeze from the west. 'A bag belt only?' he asked his balding brothers"},{"t":2,"x":3015,"y":3227,"p":0,"act":"talk","tgt":"Gerrant","d":"in the fishing shop in Port Sarim","tx":"My name's a tirade, fishing is my trade, by the docks is where my fortune is made. If a man carried my burden, he would break his back. I am not rich, but leave silver in my track. Speak to the keeper of my trail."},{"t":2,"x":3356,"y":3346,"p":0,"act":"talk","tgt":"Examiner","d":"in the Exam Centre","tx":"Often sought out by scholars of histories past, find me where words of wisdom speak volumes.","ch":"slider"},{"t":2,"x":2523,"y":3493,"p":1,"act":"search","tgt":"Boxes","d":"","tx":"A great view: watch the rapidly drying hides get splashed. Check the box you are sitting on."},{"t":2,"x":2134,"y":5162,"p":0,"act":"dig","tgt":"","d":"next to the puddle on top of the mountain on Braindeath Island","tx":"You will need to wash the old ash off of your spade when you dig here, but the only water nearby is stagnant."},{"t":2,"x":3134,"y":2803,"p":0,"act":"talk","tgt":"Hamid","d":"on the beach in Menaphos Worker District","tx":"Identify the back of this over-acting brother. (He's a long way from home.)","ch":"slider"},{"t":2,"x":3396,"y":2918,"p":0,"act":"dig","tgt":"","d":"next to the cacti west of Nardaah","tx":"As you desert this town, keep an eye out for a set of spines that could ruin nearby rugs: dig carefully around the greenery."},{"t":2,"x":2473,"y":3434,"p":0,"act":"talk","tgt":"Gnome trainer","d":"at the Gnome Agility Course","tx":"'Small Shoe.' Often found with rod on mushroom.","ch":"slider"},{"t":2,"x":3041,"y":9820,"p":0,"act":"search","tgt":"Mine Cart","d":"","tx":"It seems to have reached the end of the line, and it's still empty."},{"t":2,"x":2702,"y":3409,"p":1,"act":"search","tgt":"Bookcase","d":"","tx":"Read 'How to Breed Scorpions' By O.W. Thathurt."},{"t":2,"x":3095,"y":3153,"p":0,"act":"search","tgt":"Bookcase","d":"","tx":"Probably filled with books on magic."},{"t":2,"x":2491,"y":3489,"p":1,"act":"talk","tgt":"Heckel Funch","d":"in the south-east corner on the first floor of the Grand Tree","tx":"Citric cellar.","ch":"slider"},{"t":2,"x":2340,"y":3187,"p":0,"act":"search","tgt":"Crate","d":"","tx":"I'm sure they will let ya buy some things here, as long as you are in good 'ealth."},{"t":2,"x":2832,"y":9586,"p":0,"act":"dig","tgt":"","d":"on top of the Red spider's eggs spawn","tx":"Mine was the strangest birth under the sun. I left the crimson sack. Yet life had not begun. Entered the world and yet was seen by none."},{"t":2,"x":3478,"y":3091,"p":0,"act":"search","tgt":"Crate","d":"","tx":"His head might be hollow, but the crates nearby are filled with surprises."},{"t":2,"x":3161,"y":9905,"p":0,"act":"dig","tgt":"","d":"next to the cauldron","tx":"My giant guardians below the market streets would be fans of rock and roll, if only they could grab hold of it. Dig near my purple smoke!"},{"t":2,"x":2969,"y":2975,"p":0,"act":"dig","tgt":"","d":"by the fire next to Captain Klemfoodle","tx":"You can cook food on me, but don't cook any foodles - That would just be wrong."},{"t":2,"x":2598,"y":3267,"p":0,"act":"dig","tgt":"","d":"next to the torch in Ardougne Zoo","tx":"The beasts to my east snap claws and tails. The rest to my west can slide and eat fish. The force to my north will jump and they'll wail, Come dig by my fire and make a wish."},{"t":2,"x":2519,"y":3594,"p":0,"act":"dig","tgt":"","d":"on top of the shell","tx":"You don't need to go hopping mad - or take steps - to get to this treasure: just be totally shellfish."},{"t":2,"x":2576,"y":9583,"p":0,"act":"search","tgt":"Crate","d":"","tx":"When you get tired of fighting, go deep, deep down until you need an antidote."},{"t":2,"x":2666,"y":3238,"p":1,"act":"search","tgt":"Drawers","d":"","tx":"'Throat mage seeks companionship. Seek answers inside my furniture if interested.'"},{"t":2,"x":2576,"y":3464,"p":0,"act":"search","tgt":"Boxes","d":"","tx":"Must be full of railings"},{"t":2,"x":3219,"y":9617,"p":0,"act":"search","tgt":"Crate","d":"","tx":"You will need to under-cook to solve this one."},{"t":2,"x":3178,"y":2987,"p":0,"act":"search","tgt":"Crate","d":"","tx":"The cheapest water for miles around, but they react badly to religious icons."},{"t":2,"x":2723,"y":9891,"p":0,"act":"search","tgt":"Crate","d":"","tx":"You have all of the elements available to solve this clue. Fortunately you do not have to go as far as to stand in a draft."},{"t":2,"x":2561,"y":3323,"p":0,"act":"search","tgt":"Drawers","d":"","tx":"Try not to let yourself be dazzled when you search these drawers."},{"t":2,"x":3083,"y":3257,"p":0,"act":"dig","tgt":"","d":"in front of the window","tx":"Aggie I see, Lonely and southern I feel, I am neither inside nor outside the house, yet no house would be complete without me. The treasure lies beneath me!"},{"t":2,"x":3191,"y":9825,"p":0,"act":"dig","tgt":"","d":"in front of the gate","tx":"I am a token of the greatest love. I have no beginning or end. My eye is red, I can fit like a glove. Go to the place where it's money they lend, And dig by the gate to be my friend."},{"t":2,"x":2648,"y":3662,"p":0,"act":"search","tgt":"Crate","d":"","tx":"Navigating to this crate will be a trial."},{"t":2,"x":2851,"y":3494,"p":1,"act":"talk","tgt":"Captain Bleemadge","d":"at the top of White Wolf Mountain","tx":"This aviator is at the peak of his profession.","ch":"slider"},{"t":2,"x":3212,"y":3219,"p":0,"act":"talk","tgt":"Hans","d":"at Lumbridge Castle","tx":"Snah? I feel all confused, like one of those cakes."},{"t":2,"x":3213,"y":3216,"p":1,"act":"search","tgt":"Drawers","d":"","tx":"My home is grey, and made of stone, A castle with a search for a meal. Hidden in some drawers I am, across from a wooden wheel."},{"t":2,"x":3205,"y":3473,"p":0,"act":"talk","tgt":"Sir Prysin","d":"in Varrock Castle","tx":"Surprising? I bet he is... I bet he is..."},{"t":2,"x":3166,"y":3309,"p":2,"act":"search","tgt":"Crate","d":"","tx":"Four blades I have, yet draw no blood, Still I turn my prey to powder. If you are brave, come search my roof, It is there my blades are louder."},{"t":2,"x":3222,"y":3437,"p":0,"act":"talk","tgt":"Wilough","d":"near Varrock's fountain","tx":"My name is like a tree, yet it is spelt with a 'g'. Come see the fur which is right near me.","ch":"slider"},{"t":2,"x":3069,"y":3517,"p":0,"act":"talk","tgt":"Oziach","d":"in Edgeville","tx":"A strange little man who sells armour only to those who've proven themselves to be unafraid of dragons.","ch":"slider"},{"t":2,"x":3088,"y":3468,"p":0,"act":"dig","tgt":"","d":"next to the yew tree","tx":"Come to the evil ledge, Yew know yew want to. Try not to get stung. Come to the Try not to get stung."},{"t":2,"x":2818,"y":3351,"p":0,"act":"search","tgt":"Drawers","d":"","tx":"When no weapons are at hand, then is the time to reflect. In Saradomin's name, redemption draws closer."},{"t":2,"x":3170,"y":3885,"p":0,"act":"dig","tgt":"","d":"on top of the sapphire spawn","tx":"46 is my number. My body is the colour of burnt orange and crawls among those with eight. Three mouths I have, yet I cannot eat. My blinking blue eye hides my grave."},{"t":2,"x":3154,"y":3923,"p":0,"act":"dig","tgt":"","d":"in front of the lever in deep wilderness","tx":"If you didn't want to be here and in danger, you should lever things well enough alone."},{"t":2,"x":3235,"y":3673,"p":0,"act":"dig","tgt":"","d":"on top of the crossbow in the Graveyard of Shadows","tx":"I lie lonely and forgotten in mid wilderness, Where the dead rise from their beds. Feel free to quarrel and wind me up, and dig while you shoot their heads."},{"t":2,"x":2591,"y":3879,"p":0,"act":"dig","tgt":"","d":"at the crossoads on Etceteria","tx":"And so on, and so on, and so on. Walking from the land of many unimportant things leads to a choice of paths."},{"t":2,"x":2833,"y":2992,"p":0,"act":"search","tgt":"Bookcase","d":"","tx":"This village has a problem with cartloads of the undead. Try checking the bookcase to find the answer."},{"t":4,"x":3281,"y":2675,"p":0,"act":"skill","tgt":"Catch a plover bird in the southern part of Sophanem.","lvl":"73 Hunter","m":"Catch plover bird in southern Sophanem.","tx":"Crossing the desert I met a crocodile. She gave me a dazzling smile but she had something in her teeth."},{"t":4,"x":2667,"y":3303,"p":0,"act":"skill","tgt":"Steal from the Ardougne Gem Stall.","lvl":"75 Thieving","m":"Steal from Ardougne Gem Stall.","tx":"I once met a man returning from market. He showed me the spoils of his day. It's precious to see what people pay for."},{"t":4,"x":2187,"y":3443,"p":2,"act":"skill","tgt":"Go to the top of the Hefin Agility Course.","lvl":"77 Agility","m":"Go to top of Hefin Agility Course.","tx":"A view of the city from high above. I can hear their prayers below."},{"t":4,"x":3018,"y":9243,"p":0,"act":"skill","tgt":"Start a firepit with a Curly Root in the Jadinko Lair.","lvl":"83 Woodcutting, 83 Firemaking","m":"Burn curly root in Jadinko Lair by starting new firepit.","tx":"Round and round we go, burning, burning all aglow."},{"t":4,"x":3260,"y":3368,"p":0,"act":"skill","tgt":"Cut some elder logs from an elder tree.","lvl":"90 Woodcutting","m":"Cut elder logs from elder tree.","tx":"When I encountered this individual, she had more rings than any I had seen before."},{"t":4,"x":2796,"y":3451,"p":0,"act":"skill","tgt":"Cook a wild pie. The 'Bake Pie' spell works.","lvl":"85 Cooking","m":"Cook wild pie or wilder pie from raw form on range.","tx":"Rowdy Unruly Boisterous I am pie."},{"t":4,"x":2698,"y":5186,"p":2,"act":"skill","tgt":"Help Turgall at the Dorgesh-Kaan Agility Course.","lvl":"80 Agility","m":"Help Turgall at Dorgesh-Kaan Agility Course.","tx":"In a station of power someone requires assistance. Do a good deed to spread some happiness."},{"t":4,"x":3804,"y":3551,"p":0,"act":"skill","tgt":"Harvest a radiant memory.","lvl":"85 Divination","m":"Harvest radiant memory on Dragontooth Island.","tx":"Those things I once held dear continue to fade. I look upon her face and barely know who she is. But her eyes... They are still so radiant."},{"t":4,"x":2208,"y":3361,"p":1,"act":"skill","tgt":"Activate the Light Form Prayer in Prifddinas","lvl":"80 Prayer","m":"Use Light Form in Prifddinas.","tx":"There are those who prove themselves and become a beacon of light in the darkness. Come to the crystal city and become that figure."},{"t":4,"x":4310,"y":823,"p":0,"act":"skill","tgt":"Light some magic logs.","lvl":"75 Firemaking","m":"Burn magic logs or corrupted magic logs.","tx":"Stare into the flames for long enough and you may see something you didn't realise was there."},{"t":4,"x":2219,"y":3397,"p":1,"act":"skill","tgt":"Pickpocket any elf","lvl":"85 Thieving","m":"Pickpocket any elf.","tx":"The crystals tempt most however, there are other riches to gain for the quick-fingered."},{"t":4,"x":4299,"y":6081,"p":1,"act":"skill","npc":"Blood esswraith","tgt":"Siphon some blood runes in the Runespan.","lvl":"77 Runecrafting","m":"Siphon blood runes from blood esswraith or pool in Runespan.","tx":"Magical energy floats unleashed. The fluid of life that flows through us all. Span the gap to capture this energy."},{"t":4,"x":0,"y":0,"p":0,"act":"skill","tgt":"Craft a black dragonhide body.","lvl":"60 Crafting","m":"Craft black dragonhide body.","tx":"The spell struck me in the chest. Perhaps if I had worn armour today. Black looks better than red."},{"t":4,"x":0,"y":0,"p":0,"act":"skill","tgt":"Add a fire rune to a decorated cooking urn (nr).","lvl":"81 Crafting","m":"Add fire rune to decorated cooking urn.","tx":"Ernie once mentioned to me a secret to improve my cooking. His idea was strong but I went the extra mile."},{"t":4,"x":2796,"y":3451,"p":0,"act":"skill","tgt":"Cook a raw shark, raw rocktail or raw sailfish.","lvl":"80 Cooking","m":"Cook raw shark, rocktail, or sailfish.","tx":"I have all the ingredients for soup but sometimes a simple meal tastes the best."},{"t":4,"x":2885,"y":3507,"p":0,"act":"skill","tgt":"Smelt a Bane bar at a furnace.","lvl":"80 Smithing","m":"Smelt bane bar at furnace.","tx":"Take the journey to a distant mining site. Become a blight to your foes."},{"t":4,"x":2404,"y":4835,"p":0,"act":"skill","tgt":"Craft a death rune.","lvl":"65 Runecrafting","m":"Craft death runes at Death Altar.","tx":"Where there is Light there is also Death. Should you contain this you may progress."},{"t":4,"x":2460,"y":4893,"p":1,"act":"skill","tgt":"Craft a blood rune.","lvl":"77 Runecrafting","m":"Craft blood runes at Blood Altar in Morytania.","tx":"There is a temple where there are rivers of red. Bind the magic to get ahead."},{"t":4,"x":2189,"y":3447,"p":1,"act":"skill","tgt":"Completely use up a cleansing crystal on the Corrupted Seren Stone.","lvl":"75 Prayer","m":"Completely use cleansing crystal on Corrupted Seren Stone.","tx":"Corruption seeps from this stone but there are those who work to cleanse it. Lend your voice."},{"t":4,"x":3371,"y":3740,"p":0,"act":"skill","tgt":"Catch a charming moth in the Wilderness.","lvl":"88 Hunter, 83 Agility","m":"Catch charming moth bare-handed in Wilderness.","tx":"Charm me and I shall unlock. But be brave in the darkness."},{"t":4,"x":0,"y":0,"p":0,"act":"skill","tgt":"Burn some elder logs.","lvl":"90 Firemaking","m":"Burn elder logs.","tx":"I once met an ancient warrior who told me tales of his adventures. His eyes lit up as he spoke."},{"t":4,"x":3647,"y":5142,"p":0,"act":"skill","tgt":"Mine a concentrated gold deposit in the Living Rock Caverns.","lvl":"80 Mining","m":"Mine concentrated gold deposit in Living Rock Caverns.","tx":"Keeping all your gold together. That's a good idea. It shall be mine."},{"t":4,"x":2134,"y":7106,"p":0,"act":"skill","tgt":"Catch a raw great white shark.","lvl":"80 Fishing","m":"Catch raw great white shark using bait or swarm fishing.","tx":"Rumours of a great shark continue to spread. Catch me one and I will be fed."},{"t":4,"x":0,"y":0,"p":0,"act":"skill","tgt":"Create a Saradomin brew.","lvl":"81 Herblore","m":"Create Saradomin brew by mixing crushed bird nest into toadflax potion.","tx":"The God of Order requires a sacrifice of strength but in return restores me. If only we could keep that feeling in his absence."},{"t":4,"x":2886,"y":3505,"p":0,"act":"skill","tgt":"Smith a rune med helm or rune full helm.","lvl":"50 Smithing","m":"Smith rune med helm or rune full helm.","tx":"Head protection is never a bad thing. Make it from rune for the best chance of survival."},{"t":4,"x":0,"y":0,"p":0,"act":"skill","tgt":"Create a Zamorak brew.","lvl":"78 Herblore","m":"Create Zamorak brew by mixing jangerberries into torstol potion.","tx":"Being open to Chaos can raise your defences. It may also bring you closer to death. Take a moment to bottle this feeling."},{"t":4,"x":2840,"y":3432,"p":0,"act":"skill","tgt":"Catch two sharks at once.","lvl":"76 Agility, 76 Fishing","m":"Catch two sharks simultaneously using swarm fishing or Dwarven fish extractor.","tx":"Catching only one shark. That don't impress me much."},{"t":1,"x":2614,"y":3275,"p":0,"act":"talk","tgt":"Zookeeper","d":"in Ardougne Zoo","tx":"This anagram reveals who to speak to next: Eek Zero Op","cq":"How many animals are in the Ardougne Zoo?","ca":"40 (+1 with Eagles' Peak, +1 with Hunt for Red Raktuber)"},{"t":1,"x":2267,"y":4759,"p":0,"act":"talk","tgt":"Sabbot","d":"in the cave entrance under Death Plateau","tx":"This anagram reveals who to speak to next: Stab Ob","alt":{"q":"Death Plateau","x":3403,"y":4284,"p":0,"d":"in the cavern west, inside the cave"}},{"t":1,"x":2267,"y":4759,"p":0,"act":"talk","tgt":"Sabbot","d":"in the cave entrance under Death Plateau","tx":"This anagram reveals who to speak to next: Boast B","alt":{"q":"Death Plateau","x":3403,"y":4284,"p":0,"d":"in the cavern west, inside the cave"}},{"t":1,"x":2567,"y":3333,"p":0,"act":"talk","tgt":"Edmond","d":"north of Ardougne castle","tx":"This anagram reveals who to speak to next: Nod med","cq":"How many pigeon cages are there around the back of Jerico's house?","ca":"3"},{"t":1,"x":2881,"y":3444,"p":0,"act":"talk","tgt":"Nails Newton","d":"near Taverley Lodestone","tx":"This anagram reveals who to speak to next: Winston Lane","cq":"How many tables are there in the Pick and Lute Inn?","ca":"10"},{"t":1,"x":3400,"y":3149,"p":0,"act":"talk","tgt":"Valerio","d":"outside of the abbey","tx":"This anagram reveals who to speak to next: Or A Vile","cq":"How many windows look out into the Citharede Abbey courtyard?","ca":"17"},{"t":1,"x":2650,"y":9394,"p":0,"act":"talk","tgt":"Fycie","d":"in Rantz's cave","tx":"This anagram reveals who to speak to next: Icy Fe"},{"t":1,"x":3294,"y":3217,"p":0,"act":"talk","tgt":"Jaraah","d":"at the northern end of Al Kharid","tx":"This anagram reveals who to speak to next: Aha Jar"},{"t":1,"x":2409,"y":9819,"p":0,"act":"talk","tgt":"Brimstail","d":"in his cave at Tree Gnome Stronghold","tx":"This anagram reveals who to speak to next: Bail Trims"},{"t":1,"x":3209,"y":3215,"p":0,"act":"talk","tgt":"Cook","d":"in Lumbridge Castle's kitchen","tx":"This anagram reveals who to speak to next: Ok Co","cq":"How many cannons does Lumbridge Castle have?","ca":"7"},{"t":1,"x":3014,"y":3501,"p":0,"act":"talk","tgt":"Oracle","d":"on the peak of Ice Mountain","tx":"This anagram reveals who to speak to next: Are Col","cq":"If x is 15 and y is 3, what is 3x + y?","ca":"48"},{"t":1,"x":2722,"y":3304,"p":0,"act":"talk","tgt":"Caroline","d":"north Witchaven, by the coast","tx":"This anagram reveals who to speak to next: Arc O Line","alt":{"q":"Kennith's Concerns","x":2709,"y":3284,"p":0,"d":"upstairs in her house, west of the church"},"cq":"How many fishermen are there on the Fishing Platform?","ca":"11 (0 after Kennith's Concerns)"},{"t":1,"x":2541,"y":3169,"p":0,"act":"talk","tgt":"King Bolren","d":"in Tree Gnome Village","tx":"This anagram reveals who to speak to next: Goblin Kern"},{"t":1,"x":3207,"y":3149,"p":0,"act":"talk","tgt":"Father Urhney","d":"in the Lumbridge swamp","tx":"This anagram reveals who to speak to next: HE ANY FURTHER","cq":"How many mineable rocks are there in the mine to the east?","ca":"8"},{"t":1,"x":2659,"y":3668,"p":0,"act":"talk","tgt":"Brundt the Chieftain","d":"in Relekka Longhall","tx":"This anagram reveals who to speak to next: Dt Run B","cq":"How many people are waiting for the next bard to perform?","ca":"4"},{"t":1,"x":2459,"y":3381,"p":0,"act":"talk","tgt":"Femi","d":"at gates of Tree Gnome Stronghold","tx":"This anagram reveals who to speak to next: Me if"},{"t":1,"x":3270,"y":3183,"p":0,"act":"talk","tgt":"Karim","d":"in Al Kharid","tx":"This anagram reveals who to speak to next: R Ak Mi","cq":"I have 16 kebabs, I eat one myself and share the rest equally between 3 friends. How many do they have each?","ca":"5"},{"t":1,"x":2794,"y":3067,"p":0,"act":"talk","tgt":"Gabooty","d":"in Tai Bwo Wannai Village","tx":"This anagram reveals who to speak to next: Got A Boy","cq":"How many buildings are in the village?","ca":"11"},{"t":1,"x":2938,"y":3154,"p":0,"act":"talk","tgt":"Luthas","d":"at Musa Point","tx":"This anagram reveals who to speak to next: Halt Us"},{"t":1,"x":2542,"y":3305,"p":0,"act":"talk","tgt":"Recruiter","d":"in West Ardougne","tx":"This anagram reveals who to speak to next: Err Cure It","cq":"How many houses have a cross on the door?","ca":"20"},{"t":1,"x":2948,"y":3196,"p":0,"act":"talk","tgt":"Rommik","d":"in Rimmington","tx":"This anagram reveals who to speak to next: Im Krom","cq":"How many rocks in the Rimmington mine cannot be used to make bronze?","ca":"7"},{"t":1,"x":3233,"y":3421,"p":0,"act":"talk","tgt":"Lowe","d":"in Varrock's Archery Store","tx":"This anagram reveals who to speak to next: El Ow"},{"t":1,"x":3051,"y":3375,"p":0,"act":"talk","tgt":"Party Pete","d":"in the Party Room","tx":"This anagram reveals who to speak to next: Peaty Pert"},{"t":1,"x":3221,"y":3475,"p":0,"act":"talk","tgt":"King Roald","d":"in Varrock Castle","tx":"This anagram reveals who to speak to next: Lark In Dog","cq":"How many bookcases are there in the Varrock Palace library?","ca":"24"},{"t":2,"x":3287,"y":3236,"p":0,"act":"talk","tgt":"Cam the Camel","d":"north of Al Kharid","tx":"This anagram reveals who to speak to next: Me Am The Calc","ch":"slider"},{"t":2,"x":3287,"y":3236,"p":0,"act":"talk","tgt":"Cam the Camel","d":"north of Al Kharid","tx":"This anagram reveals who to speak to next: Ace Match Elm","ch":"slider"},{"t":2,"x":3287,"y":3236,"p":0,"act":"talk","tgt":"Cam the Camel","d":"north of Al Kharid","tx":"This anagram reveals who to speak to next: The Cal Came","ch":"slider"},{"t":2,"x":3425,"y":2928,"p":0,"act":"talk","tgt":"Shiratti the Custodian","d":"in Nardah","tx":"This anagram reveals who to speak to next: I Eat Its Chart Hints Do U","ch":"slider"},{"t":2,"x":2869,"y":9877,"p":0,"act":"talk","tgt":"Captain Ninto","d":"in the Dwarven Tunnel under the White Wolf Mountain","tx":"This anagram reveals who to speak to next: An Paint Tonic","ch":"slider"},{"t":2,"x":2342,"y":3676,"p":0,"act":"talk","tgt":"Ramara du Croissant","d":"at the Piscatoris Fishing Colony","tx":"This anagram reveals who to speak to next: Arr! So I am a crust, and?","ch":"slider"},{"t":2,"x":2406,"y":3498,"p":0,"act":"talk","tgt":"Gnome Coach","d":"north-east of the gnomeball field","tx":"This anagram reveals who to speak to next: C On Game Hoc","cq":"How many gnomes on the gnome ball field have red patches on their uniforms?","ca":"6"},{"t":2,"x":2904,"y":10207,"p":0,"act":"talk","tgt":"Riki the sculptor's model","d":"in eastern Keldagrim","tx":"This anagram reveals who to speak to next: He Do Pose. It Is Cultrrl, Mk?","ch":"slider"},{"t":2,"x":3109,"y":3156,"p":0,"act":"talk","tgt":"Professor Onglewip","d":"on the ground floor of Wizard's Tower","tx":"This anagram reveals who to speak to next: Profs Lose Wrong Pie","ch":"slider"},{"t":2,"x":2808,"y":3191,"p":0,"act":"talk","tgt":"Cap'n Izzy No-Beard","d":"at the Agility Arena","tx":"This anagram reveals who to speak to next: O Birdz A Zany En Pc","cq":"How many banana trees are there in the plantation?","ca":"33"},{"t":2,"x":3033,"y":3191,"p":0,"act":"talk","tgt":"Trader Stan","d":"in Port Sarim","tx":"This anagram reveals who to speak to next: Red Art Tans","ch":"slider"},{"t":2,"x":2588,"y":9489,"p":0,"act":"talk","tgt":"Wizard Frumscone","d":"in the basement of Wizard's Guild","tx":"This anagram reveals who to speak to next: Or Zinc Fumes Ward","ch":"slider"},{"t":2,"x":2528,"y":3162,"p":1,"act":"talk","tgt":"Bolkoy","d":"in Tree Gnome Village","tx":"This anagram reveals who to speak to next: By Look","cq":"How many flowers are there in the clearing below this platform?","ca":"13"},{"t":2,"x":2616,"y":3876,"p":0,"act":"talk","tgt":"Queen Sigrid","d":"upstairs in Etceteria castle","tx":"This anagram reveals who to speak to next: Sequin Dirge","alt":{"q":"Blood Runs Deep","x":2501,"y":3863,"p":0,"d":"upstairs in Miscellania castle"},"ch":"slider"},{"t":2,"x":2659,"y":3292,"p":0,"act":"talk","tgt":"Zenesha","d":"in her house south of Ardougne Market","tx":"This anagram reveals who to speak to next: A Zen She","ch":"slider"},{"t":2,"x":2444,"y":3052,"p":0,"act":"talk","tgt":"Uglug Nar","d":"south of Castle Wars","tx":"This anagram reveals who to speak to next: Gulag Run","ch":"slider"},{"t":2,"x":3361,"y":3506,"p":0,"act":"talk","tgt":"Odd Old Man","d":"at the limestone mine","tx":"This anagram reveals who to speak to next: Land Doomd","ch":"slider"},{"t":2,"x":2387,"y":4469,"p":0,"act":"talk","tgt":"Fairy Nuff","d":"north of the bank in Zanaris","tx":"This anagram reveals who to speak to next: I Faffy Run","ch":"slider"},{"t":4,"x":4646,"y":5384,"p":0,"act":"talk","tgt":"Ramokee skinweaver","d":"at the bottom of Polypore Dungeon","tx":"This anagram reveals who to speak to next: An exile that isn't wholly free WE IRK OVER NAMESAKE."},{"t":4,"x":3681,"y":2962,"p":0,"act":"talk","tgt":"Brother Tranquillity","d":"by the docks on Mos Le'Harmless","tx":"This anagram reveals who to speak to next: He often preaches of QUIT THY BRINE RAT ROLL","alt":{"q":"The Great Brain Roberry","x":3786,"y":2825,"p":0,"d":"on Harmony Island"}},{"t":4,"x":1785,"y":11953,"p":0,"act":"talk","tgt":"Sensei Seaworth","d":"on Tuaei Leit","tx":"This anagram reveals who to speak to next: They seek spirits O EASTERN WISHES","ch":"towers"},{"t":4,"x":3377,"y":3404,"p":0,"act":"talk","tgt":"Celia Diggory","d":"outside of the entrance to the empty throne room","tx":"This anagram reveals who to speak to next: This lady wants me to find ancient scrolls ERGO I DIG CLAY","ch":"towers"},{"t":4,"x":3001,"y":3267,"p":0,"act":"talk","tgt":"Malignius Mortifer","d":"north of the White Knight Camp","tx":"This anagram reveals who to speak to next: Master of the elements, he may REIGN US IF IMMORTAL","alt":{"q":"Necromancy!","x":1035,"y":1763,"p":1,"d":"at the Um ritual site"},"ch":"lockbox"},{"t":4,"x":2572,"y":3269,"p":0,"act":"talk","tgt":"Philipe Carnillean","d":"in the Carnillean household","alt":{"q":"Carnillean Rising","x":1131,"y":1717,"p":0,"d":"at the Reflection Pool"},"tx":"This anagram reveals who to speak to next: Young but stylish PIN HEIR ALL IN PLACE"},{"t":4,"x":2347,"y":3164,"p":0,"act":"talk","tgt":"Amaethwr","d":"in Lletya","tx":"This anagram reveals who to speak to next: If distracted from their work AH; WET ARM","ch":"lockbox"},{"t":4,"x":3088,"y":3255,"p":0,"act":"talk","tgt":"Wise Old Man","d":"in Draynor","tx":"This anagram reveals who to speak to next: In his youth, this adventurer was a WINSOME LAD"},{"t":4,"x":3256,"y":3355,"p":0,"act":"talk","tgt":"Paul Gower","d":"at the Gower farm","tx":"This anagram reveals who to speak to next: The cabbage diet must work because he has A PURE GLOW","alt":{"q":"Gower Quest","x":1056,"y":5550,"p":0,"d":"at the Life altar"}},{"t":4,"x":3198,"y":6961,"p":1,"act":"talk","tgt":"Soothsayer Sybil","d":"at the entrance to Telos","tx":"This anagram reveals who to speak to next: Age allows a new perspective in this vital place TABOO RISES SHYLY"},{"t":4,"x":2221,"y":3298,"p":1,"act":"talk","tgt":"Lady Trahaearn","d":"at the seren stones","tx":"This anagram reveals who to speak to next: With her age, it's no surprise to HEAR A LADY RANT"},{"t":4,"x":3419,"y":2938,"p":0,"act":"talk","tgt":"Ali the Wise","d":"in Nardaah","tx":"This anagram reveals who to speak to next: He claimed to be a human scholar but I SAW THE LIE"},{"t":4,"x":414,"y":674,"p":0,"act":"talk","tgt":"Death","d":"in Death's Office","tx":"This anagram reveals who to speak to next: His job leaves him kind of HATED"},{"t":1,"x":3492,"y":1507,"p":0,"act":"talk","tgt":"Miriam","d":"in Wendlewick","tx":"This anagram reveals who to speak to next: MR MIAI"},{"t":1,"x":3105,"y":3509,"p":0,"act":"talk","tgt":"Moldark, Emissary of Zamorak","d":"in Edgeville","tx":"This anagram reveals who to speak to next: IZ A AMMO LOAD FOR MRS YAKKERS","cq":"How many bottles are there on the stall to the east of Mr Ex?","ca":"5 (3 after Ritual of the Mahjarrat, before Rebuilding Edgeville)"}];
  const CLUE_DATA = [
    {i:2677,t:0,a:"search",x:3209,y:3218,p:1},
    {i:2678,t:0,a:"search",x:3228,y:3212,p:1},
    {i:2679,t:0,a:"search",x:3247,y:3244,p:0},
    {i:2680,t:0,a:"search",x:3301,y:3164,p:0},
    {i:2681,t:0,a:"emote"},
    {i:2682,t:0,a:"search",x:3289,y:3202,p:0},
    {i:2683,t:0,a:"npc",npc:541,nn:"Zeke"},
    {i:2684,t:0,a:"npc",npc:2824,nn:"Ellis"},
    {i:2685,t:0,a:"search",x:3203,y:3384,p:0},
    {i:2686,t:0,a:"npc",npc:733,nn:"Bartender"},
    {i:2687,t:0,a:"search",x:3250,y:3420,p:1},
    {i:2688,t:0,a:"search",x:3228,y:3433,p:0},
    {i:2689,t:0,a:"search",x:3156,y:3406,p:0},
    {i:2690,t:0,a:"search",x:3073,y:3430,p:0},
    {i:2691,t:0,a:"search",x:2971,y:3386,p:1},
    {i:2692,t:0,a:"search",x:2955,y:3390,p:0},
    {i:2693,t:0,a:"npc",npc:606,nn:"Squire Asrol"},
    {i:2694,t:0,a:"search",x:2969,y:3311,p:0},
    {i:2695,t:0,a:"search",x:3012,y:3222,p:0},
    {i:2696,t:0,a:"npc",npc:734,nn:"Bartender"},
    {i:2697,t:0,a:"npc",npc:918,nn:"Ned"},
    {i:2698,t:0,a:"npc",npc:284,nn:"Doric"},
    {i:2699,t:0,a:"npc",npc:586,nn:"Gaius"},
    {i:2700,t:0,a:"search",x:2828,y:3457,p:0},
    {i:2701,t:0,a:"npc",npc:563,nn:"Arhein"},
    {i:2702,t:0,a:"npc",npc:241,nn:"Sir Kay"},
    {i:2703,t:0,a:"search",x:2748,y:3495,p:2},
    {i:2704,t:0,a:"search",x:2658,y:3323,p:0},
    {i:2705,t:0,a:"search",x:2654,y:3299,p:0},
    {i:2706,t:0,a:"search",x:2615,y:3291,p:0},
    {i:2707,t:0,a:"search",x:2620,y:3336,p:0},
    {i:2708,t:0,a:"search",x:3206,y:3419,p:1},
    {i:2709,t:0,a:"search",x:3226,y:3452,p:0},
    {i:2710,t:0,a:"search",x:2575,y:3326,p:1},
    {i:2711,t:0,a:"search",x:2612,y:3306,p:1},
    {i:2712,t:0,a:"search",x:3029,y:3355,p:0},
    {i:2713,t:0,a:"coordinate",x:3164,y:3359,p:0},
    {i:2716,t:0,a:"coordinate",x:3290,y:3373,p:0},
    {i:2719,t:0,a:"coordinate",x:3043,y:3399,p:0},
    {i:3490,t:0,a:"search",x:2914,y:3521,p:0},
    {i:3491,t:0,a:"search",x:2598,y:3105,p:0},
    {i:3492,t:0,a:"search",x:2570,y:3085,p:0},
    {i:3493,t:0,a:"search",x:3000,y:9798,p:0},
    {i:3494,t:0,a:"search",x:2970,y:3214,p:1},
    {i:3495,t:0,a:"search",x:3016,y:3205,p:1},
    {i:3496,t:0,a:"npc",npc:376,nn:"Captain Tobias"},
    {i:3497,t:0,a:"search",x:3041,y:3364,p:1},
    {i:3498,t:0,a:"search",x:3060,y:3334,p:0},
    {i:3499,t:0,a:"search",x:2886,y:3449,p:0},
    {i:3500,t:0,a:"search",x:2894,y:3418,p:0},
    {i:3501,t:0,a:"search",x:3308,y:3206,p:0},
    {i:3502,t:0,a:"search",x:3106,y:3369,p:2},
    {i:3503,t:0,a:"search",x:2912,y:3530,p:0},
    {i:3504,t:0,a:"search",x:2660,y:3149,p:0},
    {i:3505,t:0,a:"search",x:2655,y:3323,p:1},
    {i:3506,t:0,a:"search",x:2636,y:3453,p:0},
    {i:3507,t:0,a:"search",x:2830,y:3448,p:0},
    {i:3508,t:0,a:"search",x:2716,y:3471,p:1},
    {i:3509,t:0,a:"search",x:2699,y:3470,p:0},
    {i:3510,t:0,a:"coordinate",x:2459,y:3505,p:0},
    {i:3512,t:0,a:"search",x:3097,y:3277,p:0},
    {i:3513,t:0,a:"npc",npc:809,nn:"Louisa"},
    {i:3514,t:0,a:"npc",npc:969,nn:"Jeed"},
    {i:3515,t:0,a:"search",x:3224,y:3492,p:0},
    {i:3516,t:0,a:"coordinate",x:2612,y:3481,p:0},
    {i:3518,t:0,a:"coordinate",x:3103,y:3133,p:0},
    {i:7236,t:0,a:"coordinate",x:2970,y:3414,p:0},
    {i:7238,t:0,a:"search",x:3509,y:3497,p:0},
    {i:10180,t:0,a:"emote"},
    {i:10182,t:0,a:"emote"},
    {i:10184,t:0,a:"emote"},
    {i:10186,t:0,a:"emote"},
    {i:10188,t:0,a:"emote"},
    {i:10190,t:0,a:"emote"},
    {i:10192,t:0,a:"emote"},
    {i:10194,t:0,a:"emote"},
    {i:10196,t:0,a:"emote"},
    {i:10198,t:0,a:"emote"},
    {i:10200,t:0,a:"emote"},
    {i:10202,t:0,a:"emote"},
    {i:10204,t:0,a:"emote"},
    {i:10206,t:0,a:"emote"},
    {i:10208,t:0,a:"emote"},
    {i:10210,t:0,a:"emote"},
    {i:10212,t:0,a:"emote"},
    {i:10214,t:0,a:"emote"},
    {i:10216,t:0,a:"emote"},
    {i:10218,t:0,a:"emote"},
    {i:10220,t:0,a:"emote"},
    {i:10222,t:0,a:"emote"},
    {i:10224,t:0,a:"emote"},
    {i:10226,t:0,a:"emote"},
    {i:10228,t:0,a:"emote"},
    {i:10230,t:0,a:"emote"},
    {i:10232,t:0,a:"emote"},
    {i:33264,t:0,a:"search",x:3018,y:3287,p:0},
    {i:33265,t:0,a:"search",x:3191,y:3257,p:0},
    {i:33266,t:0,a:"search",x:2914,y:3448,p:0},
    {i:33267,t:0,a:"npc",npc:15780,nn:"Challenge Mistress Fara"},
    {i:33268,t:0,a:"search",x:3415,y:3158,p:0},
    {i:60391,t:0,a:"search",x:3532,y:1483,p:1},
    {i:2801,t:1,a:"coordinate",x:3137,y:3252,p:0},
    {i:2803,t:1,a:"coordinate",x:2679,y:3110,p:0},
    {i:2805,t:1,a:"coordinate",x:2697,y:3207,p:0},
    {i:2807,t:1,a:"coordinate",x:2383,y:3370,p:0},
    {i:2809,t:1,a:"coordinate",x:2479,y:3158,p:0},
    {i:2811,t:1,a:"coordinate",x:2512,y:3467,p:0},
    {i:2813,t:1,a:"coordinate",x:2643,y:3252,p:0},
    {i:2815,t:1,a:"coordinate",x:2848,y:3296,p:0},
    {i:2817,t:1,a:"coordinate",x:2849,y:3033,p:0},
    {i:2819,t:1,a:"coordinate",x:3007,y:3144,p:0},
    {i:2821,t:1,a:"coordinate",x:2920,y:3403,p:0},
    {i:2823,t:1,a:"coordinate",x:3217,y:3188,p:0},
    {i:2825,t:1,a:"coordinate",x:3179,y:3344,p:0},
    {i:2827,t:1,a:"coordinate",x:3092,y:3226,p:0},
    {i:2829,t:1,a:"coordinate",x:2702,y:3429,p:0},
    {i:2831,t:1,a:"npc",npc:281,nn:"Monk",req:"Key"},
    {i:2833,t:1,a:"npc",npc:99,nn:"Guard dog",req:"Key"},
    {i:2835,t:1,a:"npc",npc:32,nn:"Guard",req:"Key"},
    {i:2837,t:1,a:"keyitem",ki:2838,req:"Key"},
    {i:2839,t:1,a:"keyitem",ki:2840,req:"Key"},
    {i:2841,t:1,a:"npc",npc:8460,nn:"Hazelmere"},
    {i:2843,t:1,a:"npc",npc:278,nn:"Cook"},
    {i:2845,t:1,a:"npc",npc:28,nn:"Zookeeper"},
    {i:2847,t:1,a:"npc",npc:550,nn:"Lowe"},
    {i:2848,t:1,a:"npc",npc:510,nn:"Hajedy"},
    {i:2849,t:1,a:"npc",npc:543,nn:"Karim"},
    {i:2851,t:1,a:"npc",npc:746,nn:"Oracle"},
    {i:2853,t:1,a:"npc",npc:635,nn:"Gnome ball referee"},
    {i:2855,t:1,a:"npc",npc:806,nn:"Donovan the Family Handyman"},
    {i:2856,t:1,a:"npc",npc:659,nn:"Party Pete"},
    {i:2857,t:1,a:"npc",npc:469,nn:"King Bolren"},
    {i:2858,t:1,a:"npc",npc:379,nn:"Luthas"},
    {i:3582,t:1,a:"coordinate",x:3443,y:3515,p:0},
    {i:3584,t:1,a:"coordinate",x:3430,y:3388,p:0},
    {i:3586,t:1,a:"coordinate",x:2919,y:3535,p:0},
    {i:3588,t:1,a:"coordinate",x:2888,y:3154,p:0},
    {i:3590,t:1,a:"coordinate",x:2743,y:3151,p:0},
    {i:3592,t:1,a:"coordinate",x:2387,y:3435,p:0},
    {i:3594,t:1,a:"coordinate",x:2416,y:3516,p:0},
    {i:3596,t:1,a:"coordinate",x:2906,y:3294,p:0},
    {i:3598,t:1,a:"search",x:2658,y:3488,p:0},
    {i:3599,t:1,a:"coordinate",x:2650,y:3231,p:0},
    {i:3601,t:1,a:"search",x:2565,y:3248,p:0},
    {i:3602,t:1,a:"coordinate",x:2924,y:3209,p:0},
    {i:3604,t:1,a:"search",x:2800,y:3074,p:0},
    {i:3605,t:1,a:"keyitem",ki:3606,req:"Key"},
    {i:3607,t:1,a:"npc",npc:1087,nn:"Penda",req:"Key"},
    {i:3609,t:1,a:"search",x:3498,y:3507,p:0},
    {i:3610,t:1,a:"search",x:2614,y:3204,p:0},
    {i:3611,t:1,a:"npc",npc:676,nn:"Femi"},
    {i:3612,t:1,a:"npc",npc:171,nn:"Brimstail"},
    {i:3613,t:1,a:"emote"},
    {i:3614,t:1,a:"npc",npc:1054,nn:"Ulizius"},
    {i:3615,t:1,a:"npc",npc:1042,nn:"Roavar"},
    {i:3616,t:1,a:"npc",npc:962,nn:"Jaraah"},
    {i:3617,t:1,a:"npc",npc:846,nn:"Kangai Mau"},
    {i:3618,t:1,a:"npc",npc:1011,nn:"Fycie"},
    {i:7274,t:1,a:"npc",npc:1294,nn:"Brundt the Chieftain"},
    {i:7276,t:1,a:"npc",npc:2519,nn:"Gabooty",gd:1},
    {i:7278,t:1,a:"npc",npc:720,nn:"Recruiter"},
    {i:7280,t:1,a:"npc",npc:7181,nn:"Caroline",gd:1},
    {i:7282,t:1,a:"npc",npc:3213,nn:"Edmond"},
    {i:7284,t:1,a:"npc",npc:648,nn:"King Roald"},
    {i:7286,t:1,a:"coordinate",x:2536,y:3865,p:0},
    {i:7288,t:1,a:"coordinate",x:3434,y:3266,p:0},
    {i:7290,t:1,a:"coordinate",x:2454,y:3230,p:0},
    {i:7292,t:1,a:"coordinate",x:2578,y:3597,p:0},
    {i:7294,t:1,a:"coordinate",x:2667,y:3562,p:0},
    {i:7296,t:1,a:"npc",npc:3246,nn:"Barbarian",req:"Key"},
    {i:7298,t:1,a:"npc",npc:1317,nn:"Market Guard",req:"Key"},
    {i:7300,t:1,a:"search",x:2764,y:3273,p:0},
    {i:7301,t:1,a:"npc",npc:13,nn:"Wizard",req:"Key"},
    {i:7303,t:1,a:"search",x:3289,y:3022,p:0},
    {i:7304,t:1,a:"search",x:2671,y:3437,p:0},
    {i:7305,t:1,a:"coordinate",x:2537,y:3881,p:0},
    {i:7307,t:1,a:"coordinate",x:2583,y:2990,p:0},
    {i:7309,t:1,a:"coordinate",x:2896,y:3119,p:0},
    {i:7311,t:1,a:"coordinate",x:3005,y:3475,p:0},
    {i:7313,t:1,a:"coordinate",x:3184,y:3150,p:0},
    {i:7315,t:1,a:"coordinate",x:2735,y:3638,p:0},
    {i:7317,t:1,a:"coordinate",x:2875,y:3046,p:0},
    {i:10254,t:1,a:"emote"},
    {i:10256,t:1,a:"emote"},
    {i:10258,t:1,a:"emote"},
    {i:10260,t:1,a:"emote"},
    {i:10262,t:1,a:"emote"},
    {i:10264,t:1,a:"emote"},
    {i:10266,t:1,a:"emote"},
    {i:10268,t:1,a:"emote"},
    {i:10270,t:1,a:"emote"},
    {i:10272,t:1,a:"emote"},
    {i:10274,t:1,a:"emote"},
    {i:10276,t:1,a:"emote"},
    {i:10278,t:1,a:"emote"},
    {i:13050,t:1,a:"search",x:2905,y:3189,p:0},
    {i:13051,t:1,a:"coordinate",x:2656,y:3161,p:0},
    {i:13053,t:1,a:"coordinate",x:3356,y:3507,p:0},
    {i:13055,t:1,a:"search",x:2399,y:4471,p:0},
    {i:13056,t:1,a:"coordinate",x:3175,y:2915,p:0},
    {i:13058,t:1,a:"coordinate",x:3359,y:2971,p:0},
    {i:13060,t:1,a:"search",x:3280,y:2787,p:0},
    {i:13061,t:1,a:"coordinate",x:3644,y:3494,p:0},
    {i:13063,t:1,a:"coordinate",x:3000,y:3110,p:0},
    {i:13065,t:1,a:"coordinate",x:2993,y:3178,p:0},
    {i:13067,t:1,a:"search",x:2438,y:3165,p:0},
    {i:13068,t:1,a:"search",x:2691,y:3508,p:0},
    {i:13069,t:1,a:"search",x:3366,y:3319,p:1},
    {i:13070,t:1,a:"search",x:2811,y:3160,p:0},
    {i:13071,t:1,a:"search",x:2698,y:9684,p:0},
    {i:13072,t:1,a:"npc",npc:188,nn:"Monk of Zamorak",req:"Key"},
    {i:13074,t:1,a:"search",x:2524,y:3438,p:0},
    {i:13075,t:1,a:"search",x:2519,y:3259,p:0},
    {i:13076,t:1,a:"coordinate",x:2513,y:3041,p:0},
    {i:13078,t:1,a:"search",x:2770,y:3172,p:0},
    {i:13079,t:1,a:"search",x:2397,y:3355,p:0},
    {i:13080,t:1,a:"search",x:3348,y:9822,p:0},
    {i:33284,t:1,a:"npc",npc:15085,nn:"Nails Newton"},
    {i:33286,t:1,a:"npc",npc:458,nn:"Father Urhney"},
    {i:33288,t:1,a:"npc",npc:14640,nn:"Valerio",gd:1},
    {i:33290,t:1,a:"npc",npc:585,nn:"Rommik"},
    {i:33292,t:1,a:"npc",npc:17135,nn:"Moldark, Emissary of Zamorak"},
    {i:60393,t:1,a:"npc",npc:32548,nn:"Miriam"},
    {i:2722,t:2,a:"search",x:3312,y:3528,p:0},
    {i:2723,t:2,a:"coordinate",x:3058,y:3884,p:0},
    {i:2725,t:2,a:"coordinate",x:2987,y:3963,p:0},
    {i:2727,t:2,a:"coordinate",x:3159,y:3959,p:0},
    {i:2729,t:2,a:"coordinate",x:3189,y:3963,p:0},
    {i:2731,t:2,a:"coordinate",x:3290,y:3889,p:0},
    {i:2733,t:2,a:"coordinate",x:3140,y:3804,p:0},
    {i:2735,t:2,a:"coordinate",x:2946,y:3819,p:0},
    {i:2737,t:2,a:"coordinate",x:3013,y:3846,p:0},
    {i:2739,t:2,a:"coordinate",x:3039,y:3960,p:0},
    {i:2741,t:2,a:"coordinate",x:3244,y:3792,p:0},
    {i:2743,t:2,a:"coordinate",x:3249,y:3739,p:0},
    {i:2745,t:2,a:"coordinate",x:2967,y:3689,p:0},
    {i:2747,t:2,a:"coordinate",x:3091,y:3571,p:0},
    {i:2773,t:2,a:"search",x:3219,y:9617,p:0},
    {i:2774,t:2,a:"coordinate",x:3089,y:3468,p:0},
    {i:2776,t:2,a:"coordinate",x:3191,y:9825,p:0},
    {i:2778,t:2,a:"npc",npc:558,nn:"Gerrant"},
    {i:2780,t:2,a:"coordinate",x:3083,y:3257,p:0},
    {i:2782,t:2,a:"search",x:3213,y:3216,p:1},
    {i:2783,t:2,a:"coordinate",x:2598,y:3267,p:0},
    {i:2785,t:2,a:"search",x:3166,y:3309,p:2},
    {i:2786,t:2,a:"coordinate",x:3235,y:3673,p:0},
    {i:2788,t:2,a:"coordinate",x:3170,y:3885,p:0},
    {i:2790,t:2,a:"coordinate",x:3161,y:9904,p:0},
    {i:2792,t:2,a:"emote"},
    {i:2793,t:2,a:"npc",npc:801,nn:"Abbot Langley"},
    {i:2794,t:2,a:"npc",npc:747,nn:"Oziach"},
    {i:2796,t:2,a:"npc",npc:9359,nn:"Sir Prysin"},
    {i:2797,t:2,a:"npc",npc:783,nn:"Wilough"},
    {i:2799,t:2,a:"npc",npc:4493,nn:"General Bentnoze"},
    {i:3520,t:2,a:"coordinate",x:2616,y:3077,p:0},
    {i:3522,t:2,a:"coordinate",x:2488,y:3308,p:0},
    {i:3524,t:2,a:"search",x:2457,y:3182,p:0},
    {i:3525,t:2,a:"search",x:3026,y:3629,p:0},
    {i:3526,t:2,a:"coordinate",x:2884,y:3667,p:0},
    {i:3528,t:2,a:"coordinate",x:2848,y:3684,p:0},
    {i:3530,t:2,a:"coordinate",x:2763,y:2974,p:0},
    {i:3532,t:2,a:"coordinate",x:2775,y:2891,p:0},
    {i:3534,t:2,a:"coordinate",x:2838,y:2914,p:0},
    {i:3536,t:2,a:"coordinate",x:2950,y:2902,p:0},
    {i:3538,t:2,a:"coordinate",x:2961,y:3024,p:0},
    {i:3540,t:2,a:"coordinate",x:2924,y:2963,p:0},
    {i:3542,t:2,a:"coordinate",x:3440,y:3341,p:0},
    {i:3544,t:2,a:"coordinate",x:3441,y:3419,p:0},
    {i:3546,t:2,a:"coordinate",x:2542,y:3031,p:0},
    {i:3548,t:2,a:"coordinate",x:2581,y:3030,p:0},
    {i:3550,t:2,a:"coordinate",x:3288,y:2982,p:0},
    {i:3552,t:2,a:"coordinate",x:3168,y:3041,p:0},
    {i:3554,t:2,a:"coordinate",x:3360,y:3243,p:0},
    {i:3556,t:2,a:"coordinate",x:3034,y:3805,p:0},
    {i:3558,t:2,a:"coordinate",x:3285,y:3943,p:0},
    {i:3560,t:2,a:"coordinate",x:2209,y:3161,p:0},
    {i:3562,t:2,a:"coordinate",x:2181,y:3206,p:0},
    {i:3564,t:2,a:"npc",npc:1182,nn:"Lord Iorwerth"},
    {i:3566,t:2,a:"emote"},
    {i:3568,t:2,a:"npc",npc:1008,nn:"Hamid"},
    {i:3570,t:2,a:"npc",npc:3810,nn:"Captain Bleemadge"},
    {i:3572,t:2,a:"search",x:2702,y:3409,p:1},
    {i:3573,t:2,a:"search",x:2523,y:3493,p:1},
    {i:3574,t:2,a:"search",x:2576,y:3464,p:0},
    {i:3575,t:2,a:"npc",npc:603,nn:"Heckel Funch"},
    {i:3577,t:2,a:"npc",npc:162,nn:"Gnome trainer"},
    {i:3579,t:2,a:"search",x:2818,y:3351,p:0},
    {i:3580,t:2,a:"coordinate",x:2832,y:9586,p:0},
    {i:7239,t:2,a:"coordinate",x:3021,y:3912,p:0},
    {i:7241,t:2,a:"coordinate",x:2722,y:3338,p:0},
    {i:7243,t:2,a:"coordinate",x:2592,y:3879,p:0},
    {i:7245,t:2,a:"coordinate",x:3488,y:3287,p:0},
    {i:7247,t:2,a:"search",x:2833,y:2991,p:0},
    {i:7248,t:2,a:"search",x:3178,y:2987,p:0},
    {i:7249,t:2,a:"search",x:3094,y:3152,p:0},
    {i:7250,t:2,a:"search",x:2723,y:9891,p:0},
    {i:7251,t:2,a:"search",x:3041,y:9820,p:0},
    {i:7252,t:2,a:"search",x:2576,y:9583,p:0},
    {i:7253,t:2,a:"search",x:3478,y:3091,p:0},
    {i:7254,t:2,a:"search",x:2671,y:3415,p:0},
    {i:7255,t:2,a:"search",x:2561,y:3323,p:0},
    {i:7256,t:2,a:"coordinate",x:2339,y:3311,p:0},
    {i:7258,t:2,a:"coordinate",x:3138,y:2969,p:0},
    {i:7260,t:2,a:"coordinate",x:2970,y:3749,p:0},
    {i:7262,t:2,a:"coordinate",x:3113,y:3602,p:0},
    {i:7264,t:2,a:"coordinate",x:3305,y:3692,p:0},
    {i:7266,t:2,a:"coordinate",x:2712,y:3732,p:0},
    {i:7268,t:2,a:"npc",npc:2802,nn:"Gnome Coach"},
    {i:7270,t:2,a:"npc",npc:471,nn:"Bolkoy"},
    {i:7272,t:2,a:"npc",npc:437,nn:"Cap'n Izzy No-Beard"},
    {i:10234,t:2,a:"emote"},
    {i:10236,t:2,a:"emote"},
    {i:10238,t:2,a:"emote"},
    {i:10240,t:2,a:"emote"},
    {i:10242,t:2,a:"emote"},
    {i:10244,t:2,a:"emote"},
    {i:10246,t:2,a:"emote"},
    {i:10248,t:2,a:"emote"},
    {i:10250,t:2,a:"emote"},
    {i:10252,t:2,a:"emote"},
    {i:13010,t:2,a:"npc",npc:2142,nn:"Riki the sculptor's model"},
    {i:13012,t:2,a:"npc",npc:3827,nn:"Ramara du Croissant"},
    {i:13014,t:2,a:"npc",npc:4585,nn:"Professor Onglewip"},
    {i:13016,t:2,a:"npc",npc:3044,nn:"Shiratti the Custodian"},
    {i:13018,t:2,a:"npc",npc:4650,nn:"Trader Stan"},
    {i:13020,t:2,a:"npc",npc:2039,nn:"Uglug Nar"},
    {i:13022,t:2,a:"npc",npc:4431,nn:"Fairy Nuff"},
    {i:13024,t:2,a:"npc",npc:2812,nn:"Cam the Camel"},
    {i:13026,t:2,a:"npc",npc:4594,nn:"Captain Ninto"},
    {i:13028,t:2,a:"npc",npc:589,nn:"Zenesha"},
    {i:13030,t:2,a:"npc",npc:460,nn:"Wizard Frumscone"},
    {i:13032,t:2,a:"npc",npc:1359,nn:"Queen Sigrid"},
    {i:13034,t:2,a:"emote"},
    {i:13036,t:2,a:"coordinate",x:2134,y:5163,p:0},
    {i:13038,t:2,a:"coordinate",x:2519,y:3594,p:0},
    {i:13040,t:2,a:"search",x:2666,y:3238,p:1},
    {i:13041,t:2,a:"search",x:2340,y:3187,p:0},
    {i:13042,t:2,a:"coordinate",x:2969,y:2975,p:0},
    {i:13044,t:2,a:"coordinate",x:3154,y:3924,p:0},
    {i:13046,t:2,a:"coordinate",x:3396,y:2918,p:0},
    {i:13048,t:2,a:"search",x:2648,y:3662,p:0},
    {i:13049,t:2,a:"search",x:2993,y:3686,p:0},
    {i:33269,t:2,a:"coordinate",x:2632,y:3407,p:0},
    {i:33272,t:2,a:"coordinate",x:2896,y:3397,p:0},
    {i:33275,t:2,a:"coordinate",x:2887,y:3044,p:0},
    {i:33278,t:2,a:"coordinate",x:2602,y:3063,p:0},
    {i:33281,t:2,a:"coordinate",x:2363,y:3461,p:0},
    {i:19043,t:3,a:"scan",en:3103},
    {i:19044,t:3,a:"scan",en:3104},
    {i:19045,t:3,a:"scan",en:3105},
    {i:19046,t:3,a:"scan",en:3106},
    {i:19047,t:3,a:"scan",en:3107},
    {i:19048,t:3,a:"scan",en:13503},
    {i:19049,t:3,a:"scan",en:3109},
    {i:19050,t:3,a:"scan",en:3110},
    {i:19051,t:3,a:"scan",en:3111},
    {i:19052,t:3,a:"scan",en:3112},
    {i:19053,t:3,a:"scan",en:3113},
    {i:19054,t:3,a:"scan",en:3114},
    {i:19055,t:3,a:"scan",en:3115},
    {i:19056,t:3,a:"scan",en:3116},
    {i:19057,t:3,a:"scan",en:3117},
    {i:19058,t:3,a:"scan",en:3118},
    {i:19059,t:3,a:"scan",en:3119},
    {i:19060,t:3,a:"scan",en:3120},
    {i:19061,t:3,a:"scan",en:3121},
    {i:19062,t:3,a:"scan",en:3122},
    {i:19063,t:3,a:"scan",en:3123},
    {i:19064,t:3,a:"scan",en:3124},
    {i:60392,t:3,a:"scan",en:10609},
    {i:41790,t:4,a:"scan",en:13504},
    {i:41791,t:4,a:"scan",en:13505},
    {i:41792,t:4,a:"scan",en:13506},
    {i:41793,t:4,a:"scan",en:13507},
    {i:41794,t:4,a:"emote"},
    {i:41796,t:4,a:"emote"},
    {i:41797,t:4,a:"emote"},
    {i:41798,t:4,a:"scan",en:13501},
    {i:59266,t:4,a:"scan",en:3},
    {i:19626,t:5,a:"emote"},
    {i:38473,t:5,a:"emote"},
    {i:38474,t:5,a:"emote"},
    {i:38475,t:5,a:"emote"},
    {i:38476,t:5,a:"emote"},
    {i:38477,t:5,a:"emote"},
    {i:38478,t:5,a:"emote"},
    {i:38479,t:5,a:"emote"},
    {i:38480,t:5,a:"emote"},
    {i:38481,t:5,a:"emote"},
    {i:38482,t:5,a:"emote"},
    {i:38483,t:5,a:"emote"},
    {i:38484,t:5,a:"emote"},
    {i:38485,t:5,a:"emote"},
    {i:38486,t:5,a:"emote"},
    {i:38487,t:5,a:"emote"},
    {i:38488,t:5,a:"emote"},
    {i:38489,t:5,a:"emote"},
    {i:38490,t:5,a:"emote"},
    {i:38491,t:5,a:"emote"},
    {i:38492,t:5,a:"emote"},
    {i:38493,t:5,a:"emote"},
    {i:38494,t:5,a:"emote"},
    {i:38495,t:5,a:"emote"},
    {i:38496,t:5,a:"emote"},
    {i:38497,t:5,a:"emote"},
    {i:38498,t:5,a:"emote"},
    {i:38499,t:5,a:"emote"},
    {i:38500,t:5,a:"emote"},
    {i:42679,t:5,a:"emote"},
    {i:43349,t:5,a:"emote"},
    {i:43350,t:5,a:"emote"},
    {i:52631,t:5,a:"emote"},
    {i:52632,t:5,a:"emote"}
  ];
  // Talk-to-NPC clue solutions: npc id -> {l:location, a:challenge answer with
  // state notes, x/y/p:tile}. Joined by NPC name; covers 70 of 79 npc clues.
  const CLUE_NPC_INFO = {
    28:{l:"in Ardougne Zoo",a:"40 (Before Eagles' Peak and Hunt for Red Raktuber); 41 (After either Eagles' Peak or Hunt for Red Raktuber); 42 (After both Eagles' Peak and Hunt for Red Raktuber)",x:2614,y:3275,p:0},
    162:{l:"at the Gnome Agility Course",x:2472,y:3433,p:0},
    171:{l:"in Brimstail's cave",a:"6859",x:2409,y:9819,p:0},
    241:{l:"in the courtyard of Camelot Castle",x:2758,y:3498,p:0},
    278:{l:"in Lumbridge Castle's kitchen",a:"7",x:3209,y:3215,p:0},
    284:{l:"north of Falador",x:2959,y:3439,p:0},
    379:{l:"at Musa Point",x:2938,y:3154,p:0},
    437:{l:"at the Agility Arena",a:"33",x:2808,y:3191,p:0},
    458:{l:"in the Lumbridge swamp",a:"8",x:3207,y:3149,p:0},
    460:{l:"in the basement of Wizard's Guild",x:2588,y:9489,p:0},
    469:{l:"in Tree Gnome Village",x:2541,y:3169,p:0},
    471:{l:"in Tree Gnome Village",a:"13",x:2528,y:3162,p:1},
    510:{l:"in Brimhaven",x:2781,y:3212,p:0},
    541:{l:"in Al Kharid",x:3287,y:3189,p:0},
    543:{l:"in Al Kharid",a:"5",x:3270,y:3183,p:0},
    550:{l:"in Varrock's Archery Store",x:3233,y:3421,p:0},
    558:{l:"in the fishing shop in Port Sarim",x:3014,y:3226,p:0},
    563:{l:"in Catherby",x:2803,y:3426,p:0},
    585:{l:"in Rimmington",a:"7",x:2948,y:3196,p:0},
    586:{l:"in Burthorpe",x:2929,y:3549,p:0},
    589:{l:"in her house south of Ardougne Market",x:2659,y:3292,p:0},
    603:{l:"in the south-east corner on the first floor of the Grand Tree",x:2490,y:3488,p:1},
    606:{l:"in the couryard of the White Knight's castle",x:2974,y:3343,p:0},
    635:{l:"at the gate of the Gnomeball Field",a:"5096",x:2385,y:3488,p:0},
    648:{l:"in Varrock Castle",a:"24",x:3221,y:3475,p:0},
    659:{l:"in the Party Room",x:3051,y:3375,p:0},
    676:{l:"at gates of Tree Gnome Stronghold",x:2459,y:3381,p:0},
    720:{l:"in West Ardougne",a:"20",x:2542,y:3305,p:0},
    733:{l:"in Port Sarim",x:3045,y:3257,p:0},
    734:{l:"in Port Sarim",x:3045,y:3257,p:0},
    746:{l:"on the peak of Ice Mountain",a:"48",x:3014,y:3501,p:0},
    747:{l:"in Edgeville",x:3068,y:3516,p:0},
    783:{l:"near Varrock's fountain",x:3221,y:3436,p:0},
    801:{l:"at the Edgeville Monastery",x:3057,y:3485,p:0},
    806:{l:"at Sinclair Mansion",x:2742,y:3580,p:1},
    809:{l:"in Sinclair Mansion",x:2737,y:3577,p:0},
    846:{l:"in Brimhaven",x:2793,y:3183,p:0},
    918:{l:"in Draynor",x:3100,y:3258,p:0},
    962:{l:"at the northern end of Al Kharid",x:3294,y:3217,p:0},
    969:{l:"north of Het's Oasis",x:3375,y:3275,p:0},
    1008:{l:"on the beach in Menaphos Worker District",x:3133,y:2802,p:0},
    1011:{l:"in Rantz's cave",x:2650,y:9394,p:0},
    1042:{l:"in Canifis' bar",x:3492,y:3473,p:0},
    1054:{l:"near the Swamp Gate",x:3443,y:3461,p:0},
    1182:{l:"in Prifddinas [After 'Plague's End']; at Iorwerth Camp [Before 'Plague's End']",x:2186,y:3284,p:1},
    1294:{l:"in Relekka Longhall",a:"4",x:2659,y:3668,p:0},
    1359:{l:"in Miscellania's Throneroom [After 'Blood Runs Deep']; in Etceteria Castle [Before 'Blood Runs Deep']",x:2501,y:3863,p:1},
    2039:{l:"south of Castle Wars",x:2444,y:3052,p:0},
    2142:{l:"in eastern Keldagrim",x:2904,y:10207,p:0},
    2519:{l:"in Tai Bwo Wannai Village",a:"11",x:2794,y:3067,p:0},
    2802:{l:"north-east of the gnomeball field",a:"6",x:2406,y:3498,p:0},
    2812:{l:"north of Al Kharid",x:3289,y:3238,p:0},
    2824:{l:"in Al Kharid",x:3273,y:3196,p:0},
    3044:{l:"in Nardah",x:3425,y:2928,p:0},
    3213:{l:"north of Ardougne castle",a:"3",x:2567,y:3333,p:0},
    3810:{l:"at the top of White Wolf Mountain",x:2850,y:3493,p:1},
    3827:{l:"at the Piscatoris Fishing Colony",x:2342,y:3676,p:0},
    4431:{l:"north of the bank in Zanaris",x:2387,y:4469,p:0},
    4493:{l:"in Goblin Village",x:2957,y:3514,p:0},
    4585:{l:"on the ground floor of Wizard's Tower",x:3109,y:3156,p:0},
    4594:{l:"in the Dwarven Tunnel under the White Wolf Mountain",x:2869,y:9877,p:0},
    4650:{l:"in Port Sarim",x:3033,y:3191,p:0},
    7181:{l:"in Witchhaven [Before 'Sea Slug']; in her house in Witchhaven [After 'Sea Slug'.]",a:"11 (Before 'Kennith's Concerns'); 0 (After 'Kennith's Concerns')",x:2716,y:3298,p:0},
    8460:{l:"east of Yanille",a:"6859",x:2677,y:3087,p:1},
    9359:{l:"in Varrock Castle",x:3204,y:3472,p:0},
    14640:{l:"outside of the abbey",a:"17",x:3400,y:3149,p:0},
    15085:{l:"near Taverley Lodestone",a:"10",x:2881,y:3444,p:0},
    15780:{l:"in Burthorpe",x:2883,y:3533,p:0},
    17135:{l:"in Edgeville",a:"5 (Edgeville intact); 3 (Edgeville destroyed)",x:3105,y:3509,p:0},
    32548:{l:"in Wendlewick",x:3492,y:1507,p:0}
  };
  hideyData = null; let hideyFetching = false; let hideyListSig = ''; let hideyFetchAt = 0;
  let hideyFTier = -1, hideyFStatus = 0, hideyFSearch = '', hideyTotals = false;   // status 0=all 1=not built 2=empty 3=filled
  async function fetchHidey(force) {
    if (!bridge() || hideyFetching) return;
    const _t = Date.now(); if (!force && _t - hideyFetchAt < 2000) return; hideyFetchAt = _t;
    hideyFetching = true;
    try {
      let vp = {};
      if (bridge().varps) { try { vp = JSON.parse(await bridge().varps(myPid(), HIDEY_VARPS.join(','))); } catch (e) {} }
      hideyData = HIDEY.map(h => { const v = (((vp[h.vp] || 0) >>> 0) >>> h.b) & 3; return { ...h, state: v, built: v >= 1, filled: v === 2 }; });
    } finally { hideyFetching = false; }
    paneRun('hideyholes', renderHidey);
    { const eel = $('clueEmote'); if (eel && eel.style.display !== 'none') renderEmotePanel(); }
  }
  // Inline location map: ONE persistent map node inserted directly UNDER the clicked row.
  // hideyMapFor = the hole it is open for; clicking the same row again closes it. The node
  // survives list rebuilds (renderHideyList re-inserts it after its row) because an innerHTML
  // wipe would destroy the canvas.
  let hideyMapFor = null, hideyMapEl = null;
  function hideyMapNode() {
    if (hideyMapEl) return hideyMapEl;
    const mapWrap = document.createElement('div'); mapWrap.id = 'hideyMapWrap'; mapWrap.className = 'clue-map-wrap';
    mapWrap.style.cssText = 'display:none;margin:6px 0 8px;';
    const mapStage = document.createElement('div'); mapStage.className = 'clue-map-stage';
    const mapInner = document.createElement('div'); mapInner.className = 'clue-map-inner';
    const mcv = document.createElement('canvas'); mcv.id = 'hideyMapCanvas'; mcv.width = 384; mcv.height = 384; mcv.className = 'clue-map'; mapInner.appendChild(mcv);
    mapStage.appendChild(mapInner); mapWrap.appendChild(mapStage);
    const mcap = document.createElement('div'); mcap.id = 'hideyMapCap'; mcap.className = 'clue-map-cap'; mapWrap.appendChild(mcap);
    return (hideyMapEl = mapWrap);
  }
  function hideyMapClose() {
    hideyMapFor = null;
    if (hideyMapEl) hideyMapEl.style.display = 'none';
    // Also drop the in-world guide mark that opened with the map.
    try { if (typeof clueGuide === 'function') clueGuide(null); } catch (e) {}
  }
  // Rows are keyed by data-hk = "x,y,p": hole NAMES repeat ("Crate" many times), so keying by name
  // lights the map link on every same-named row at once.
  function hideyMapLinksSync(list) {
    list.querySelectorAll('.pet-row[data-hx] .hh-maplink').forEach(a => {
      const open = a.closest('.pet-row').dataset.hk === hideyMapFor;
      a.textContent = open ? 'Hide map' : 'View location on map';
    });
  }
  function renderHidey() {
    const c = $('content');
    let wrap = $('hideyWrap');
    if (!wrap) {
      c.innerHTML = ''; hideyListSig = '';
      wrap = document.createElement('div'); wrap.id = 'hideyWrap'; wrap.className = 'pk-wrap'; c.appendChild(wrap);
      const tb = document.createElement('div'); tb.className = 'pet-toolbar';
      const mkchip = (label, grp, val, cur) => { const b = document.createElement('button'); b.className = 'pet-chip' + (val === cur ? ' on' : ''); b.textContent = label; b.dataset.grp = grp; b.dataset.val = val; return b; };
      const tierRow = document.createElement('div'); tierRow.className = 'pet-chips';
      tierRow.appendChild(mkchip('All', 'tier', -1, hideyFTier));
      HIDEY_TIERS.forEach((nm, i) => tierRow.appendChild(mkchip(nm, 'tier', i, hideyFTier)));
      const stRow = document.createElement('div'); stRow.className = 'pet-chips';
      ['All', 'Not built', 'Empty', 'Filled'].forEach((nm, i) => stRow.appendChild(mkchip(nm, 'st', i, hideyFStatus)));
      // Totals toggle: swaps the per-hole list for an aggregated shopping list (build materials + fill
      // items) covering everything still left to finish in the selected tier.
      const totBtn = document.createElement('button');
      totBtn.id = 'hideyTot'; totBtn.className = 'pet-chip hh-totbtn' + (hideyTotals ? ' on' : '');
      totBtn.textContent = 'Σ Totals';
      totBtn.title = 'Total planks, nails and fill items needed to build + fill everything not yet done';
      const search = document.createElement('input'); search.className = 'pet-search'; search.id = 'hideySearch'; search.placeholder = 'Search name or location...'; search.value = hideyFSearch;
      if (hideyTotals) { stRow.style.display = 'none'; search.style.display = 'none'; }
      tb.appendChild(tierRow); tb.appendChild(stRow); tb.appendChild(totBtn); tb.appendChild(search); wrap.appendChild(tb);
      const cnt = document.createElement('div'); cnt.id = 'hideyCnt'; cnt.className = 'pet-count'; wrap.appendChild(cnt);
      const list = document.createElement('div'); list.id = 'hideyList'; list.className = 'pet-list'; wrap.appendChild(list);
      list.addEventListener('click', e => {
        const row = e.target.closest('.pet-row[data-hx]'); if (!row) return;
        if (hideyMapFor === row.dataset.hk) { hideyMapClose(); hideyMapLinksSync(list); return; }
        hideyMapFor = row.dataset.hk;
        row.insertAdjacentElement('afterend', hideyMapNode());
        hideyMapLinksSync(list);
        const h = { n: row.dataset.hn, x: +row.dataset.hx, y: +row.dataset.hy, p: +row.dataset.hp };
        // In-world mark via the same guide primitive the clue/quest helpers use: label line 1 = the
        // object's name, so the overlay snaps the footprint prism onto the loc at that tile when the
        // area is loaded (plain gold tile mark otherwise).
        try { if (typeof clueGuide === 'function') clueGuide({ x: h.x, y: h.y, p: h.p }, h.n + '\nHidey-hole'); } catch (e2) {}
        drawHideyMap(h);
      });
      tb.addEventListener('click', e => {
        if (e.target.closest('#hideyTot')) {
          hideyTotals = !hideyTotals;
          totBtn.classList.toggle('on', hideyTotals);
          stRow.style.display = hideyTotals ? 'none' : '';
          search.style.display = hideyTotals ? 'none' : '';
          hideyListSig = ''; renderHideyList();
          return;
        }
        const b = e.target.closest('.pet-chip'); if (!b) return;
        if (b.dataset.grp === 'tier') hideyFTier = +b.dataset.val; else hideyFStatus = +b.dataset.val;
        tb.querySelectorAll('.pet-chip[data-grp="' + b.dataset.grp + '"]').forEach(x => x.classList.toggle('on', x === b));
        hideyListSig = ''; renderHideyList();
      });
      search.addEventListener('input', () => { hideyFSearch = search.value.toLowerCase(); hideyListSig = ''; renderHideyList(); });
    }
    renderHideyList();
  }
  function renderHideyList() {
    const list = $('hideyList'); if (!list) return;
    if (hideyTotals) { renderHideyTotals(); return; }
    const d = hideyData;
    if (!d) { list.innerHTML = '<div class="empty">Reading... (be in-world)</div>'; hideyListSig = ''; return; }
    const items = d.filter(h =>
      (hideyFTier < 0 || h.t === hideyFTier) &&
      (hideyFStatus === 0 || h.state === hideyFStatus - 1) &&
      (!hideyFSearch || h.n.toLowerCase().indexOf(hideyFSearch) >= 0 || (h.loc && h.loc.toLowerCase().indexOf(hideyFSearch) >= 0)));
    const built = d.filter(h => h.built).length, filled = d.filter(h => h.filled).length;
    const cnt = $('hideyCnt'); if (cnt) cnt.textContent = built + ' / ' + d.length + ' built  ·  ' + filled + ' / ' + d.length + ' filled' + (items.length !== d.length ? '  ·  ' + items.length + ' shown' : '');
    const sig = hideyFTier + '|' + hideyFStatus + '|' + hideyFSearch + '|' + items.map(h => h.n + h.state).join(',');
    if (sig === hideyListSig) return;
    hideyListSig = sig;
    list.innerHTML = '';
    if (!items.length) { list.innerHTML = '<div class="empty">No hidey-holes match.</div>'; return; }
    const STLBL = ['Not built', 'Empty', 'Filled'], STCLS = ['hh-none', 'hh-empty', 'hh-filled'];
    for (const h of items) {
      const row = document.createElement('div'); row.className = 'pet-row' + (h.state === 0 ? ' pet-locked' : '');
      if (h.x > 0) {
        row.dataset.hx = h.x; row.dataset.hy = h.y; row.dataset.hp = h.p || 0; row.dataset.hn = h.n;
        row.dataset.hk = h.x + ',' + h.y + ',' + (h.p || 0);   // unique key (names repeat)
        row.style.cursor = 'pointer';
      }
      const fillTxt = (h.fi && h.fi.length) ? h.fi.join(', ') : '(unknown)';
      row.dataset.tip = h.n + ' (' + HIDEY_TIERS[h.t] + ')\n' + h.loc +
        '\nBuild: ' + HIDEY_BUILD[h.t] +
        '\nFill: ' + fillTxt +
        '\nStatus: ' + (h.state === 0 ? 'Not built' : h.state === 1 ? 'Built - empty (not filled)' : 'Built and filled');
      const tier = document.createElement('div'); tier.className = 'hh-tier t' + h.t; tier.textContent = HIDEY_TIERS[h.t]; row.appendChild(tier);
      const info = document.createElement('div'); info.className = 'pet-info';
      const nm = document.createElement('div'); nm.className = 'pet-nm'; nm.textContent = h.n; info.appendChild(nm);
      const loc = document.createElement('div'); loc.className = 'hh-loc'; loc.textContent = h.loc; info.appendChild(loc);
      if (h.fi && h.fi.length) { const fl = document.createElement('div'); fl.className = 'hh-fill'; fl.textContent = 'Fill: ' + h.fi.join(' · '); info.appendChild(fl); }
      if (h.x > 0) {
        const ml = document.createElement('div'); ml.className = 'hh-maplink';
        ml.textContent = (hideyMapFor === h.x + ',' + h.y + ',' + (h.p || 0)) ? 'Hide map' : 'View location on map';
        info.appendChild(ml);
      }
      row.appendChild(info);
      const st = document.createElement('div'); st.className = 'hh-st ' + STCLS[h.state]; st.textContent = STLBL[h.state]; row.appendChild(st);
      list.appendChild(row);
    }
    // Re-seat the inline map after a rebuild: innerHTML wipes it out of the list, and its row may
    // have been filtered away (then it stays hidden until re-opened).
    if (hideyMapFor && hideyMapEl) {
      let anchor = null;
      list.querySelectorAll('.pet-row[data-hx]').forEach(r => { if (r.dataset.hk === hideyMapFor) anchor = r; });
      if (anchor) { anchor.insertAdjacentElement('afterend', hideyMapEl); hideyMapEl.style.display = ''; }
      else hideyMapClose();
    }
  }
  // Draw a world map centred on a hidey hole's tile (reuses the C++ mapWindow render the clue maps
  // use). Queries within the persistent node: getElementById misses it while the node sits
  // detached between list rebuilds.
  async function drawHideyMap(h) {
    const wrap = hideyMapNode();
    const cv = wrap.querySelector('#hideyMapCanvas'), cap = wrap.querySelector('#hideyMapCap');
    if (!wrap || !cv) return;
    if (!h || !(h.x > 0) || !bridge() || !bridge().mapWindow) { wrap.style.display = 'none'; return; }
    // Fixed 4 px per tile over 128 tiles is a 512 px image; on a scaled display the canvas
    // backing is larger than that and the terrain gets upscaled. Ask for what the element
    // actually shows (this map has no zoom, so it is a one-off calculation).
    // Same oversample as mapRes: the panel View renders at the monitor scale and JS is not
    // told, so a reported ratio of 1 would under-render this map on any scaled display.
    const hDpr = Math.max(1.5, Math.min(2, (typeof devicePixelRatio === 'number' && devicePixelRatio > 0) ? devicePixelRatio : 1));
    let hTs = Math.ceil(((cv.clientWidth || 384) * hDpr) / 128);
    if (hTs & 1) hTs++;
    hTs = Math.max(2, Math.min(16, hTs));
    let meta = await mapWindowCached(h.x, h.y, h.p || 0, 64, hTs);
    const W = (meta && meta.w) || 384, TS = (meta && meta.t) || 8, HH = (meta && meta.h) || 24;
    const cx = clueMapCtx(cv, W);
    const hU = cx._upx || 1;   // marker sizes in display px, not terrain px
    if (meta && (meta.png || meta.b64 || meta._k)) { try { clueMapBlit(cx, meta, W, cv); clueDrawNomove(cx, meta); clueDrawObjects(cx, meta); clueDrawTeleports(cx, meta); clueDrawLabelsWindow(cx, meta); } catch (e) {} }
    const mx = HH * TS + TS / 2, my = (HH - 1) * TS + TS / 2;   // the hole's tile sits at the window centre
    cx.lineWidth = 5 * hU; cx.strokeStyle = 'rgba(0,0,0,0.6)'; cx.beginPath(); cx.arc(mx, my, 11 * hU, 0, 6.2832); cx.stroke();
    cx.lineWidth = 2.6 * hU; cx.strokeStyle = '#46e0c0'; cx.beginPath(); cx.arc(mx, my, 11 * hU, 0, 6.2832); cx.stroke();
    cx.beginPath(); cx.arc(mx, my, 2.8 * hU, 0, 6.2832); cx.fillStyle = '#46e0c0'; cx.fill();
    wrap.style.display = '';
    if (cap) cap.textContent = (h.n || 'Hidey-hole') + '  ·  ' + h.x + ', ' + h.y + (h.p ? '  ·  floor ' + h.p : '')
      + (typeof nearLabel === 'function' ? ('  ·  ' + nearLabel(h.x, h.y, h.p || 0)) : '');
  }
  // Aggregated shopping list: every plank / nail / gold leaf still needed to BUILD, plus every
  // FILL item still needed, for the holes in the selected tier that are not already done. Build
  // mats count only unbuilt holes (state 0); fill items count every not-yet-filled hole (0 or 1).
  function renderHideyTotals() {
    const list = $('hideyList'); if (!list) return;
    const cnt = $('hideyCnt');
    const d = hideyData;
    if (!d) { list.innerHTML = '<div class="empty">Reading... (be in-world)</div>'; hideyListSig = ''; if (cnt) cnt.textContent = ''; return; }
    const inScope = d.filter(h => hideyFTier < 0 || h.t === hideyFTier);
    const toBuild = inScope.filter(h => h.state === 0);   // not built -> needs planks/nails/gold leaf
    const toFill  = inScope.filter(h => h.state !== 2);   // not filled (state 0 or 1) -> needs fill items
    const build = {};
    for (const h of toBuild) { const m = HIDEY_BUILD_MAT[h.t]; for (const k in m) build[k] = (build[k] || 0) + m[k]; }
    const fill = {};
    for (const h of toFill) for (const it of (h.fi || [])) fill[it] = (fill[it] || 0) + 1;
    const buildRows = HIDEY_MAT_ORDER.filter(k => build[k]).map(k => [k, build[k]]);
    const fillRows = Object.keys(fill).sort((a, b) => fill[b] - fill[a] || a.localeCompare(b)).map(k => [k, fill[k]]);
    const scopeName = hideyFTier < 0 ? 'all hidey-holes' : HIDEY_TIERS[hideyFTier] + ' hidey-holes';
    if (cnt) cnt.textContent = 'To finish ' + scopeName + ': ' + toBuild.length + ' to build · ' + toFill.length + ' to fill';
    const sig = 'T|' + hideyFTier + '|' + toBuild.length + '|' + toFill.length + '|'
              + buildRows.map(r => r[0] + r[1]).join(',') + '|' + fillRows.map(r => r[0] + r[1]).join(',');
    if (sig === hideyListSig) return;
    hideyListSig = sig;
    list.innerHTML = '';
    if (!toBuild.length && !toFill.length) {
      list.innerHTML = '<div class="empty">Everything in ' + scopeName + ' is built and filled. Nothing needed.</div>';
      return;
    }
    const sec = (title, note) => {
      const s = document.createElement('div'); s.className = 'hh-tot-sec';
      const a = document.createElement('span'); a.textContent = title; s.appendChild(a);
      const b = document.createElement('span'); b.className = 'hh-tot-secn'; b.textContent = note; s.appendChild(b);
      list.appendChild(s);
    };
    const row = (name, qty) => {
      const r = document.createElement('div'); r.className = 'pet-row hh-tot-row';
      const info = document.createElement('div'); info.className = 'pet-info';
      const nm = document.createElement('div'); nm.className = 'pet-nm'; nm.textContent = name; info.appendChild(nm);
      r.appendChild(info);
      const q = document.createElement('div'); q.className = 'hh-tot-qty'; q.textContent = '×' + qty.toLocaleString(); r.appendChild(q);
      list.appendChild(r);
    };
    if (buildRows.length) {
      sec('Build materials', toBuild.length + (toBuild.length === 1 ? ' hole to build' : ' holes to build'));
      for (const [k, v] of buildRows) row(k, v);
    }
    if (fillRows.length) {
      sec('Fill items', toFill.length + (toFill.length === 1 ? ' hole to fill' : ' holes to fill'));
      for (const [k, v] of fillRows) row(k, v);
    }
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { CLUE_DATA, CLUE_NPC_INFO, CLUE_TIERS, CRYPTIC_CLUES, EMOTE_CLUES, HIDEY, HIDEY_BUILD, HIDEY_TIERS, HIDEY_VARPS, LODESTONES, LODE_VARPS, fetchHidey, fetchLodestones });
registerTab({ id: 'hideyholes', render: renderHidey, open: function () { hideyListSig = ''; fetchHidey(true); }, close: function () { try { hideyMapClose(); } catch (e) {} } });
registerTab({ id: 'lodestones', render: renderLodestones, open: function () { lodeListSig = ''; fetchLodestones(true); } });
})();
