// RuneToolsX panel: Storage boxes (ore/wood/soil/gem/essence/rune pouch/quiver/...).
// Spliced inline into client.html at load; IIFE (window exports + registerTab; see the RTX registry in client.html).
(function () {

  // Per-type counts live in varbits (ore/soil/upgraded gem bag), a container (wood 937) or the item's own Extra_ints (base gem bag).
  const STORAGE = {
    ore: {
      boxes: { 44779: 'Bronze', 44781: 'Iron', 44783: 'Steel', 44785: 'Mithril', 44787: 'Adamant', 44789: 'Rune', 44791: 'Orikalkum', 44793: 'Necronium', 44795: 'Bane', 44797: 'Elder rune', 57172: 'Primal' },
      vb: [['Copper', 43188], ['Tin', 43190], ['Iron', 43192], ['Coal', 43194], ['Silver', 43196], ['Mithril', 43198], ['Adamantite', 43200], ['Luminite', 43202], ['Gold', 43204], ['Runite', 43206], ['Orichalcite', 43208], ['Drakolith', 43210], ['Necrite', 43212], ['Phasmatite', 43214], ['Banite', 43216], ['Light animica', 43218], ['Dark animica', 43220], ['Novite', 55880], ['Bathus', 55883], ['Marmaros', 55886], ['Kratonium', 55889], ['Fractite', 55892], ['Zephyrium', 55895], ['Argonite', 55898], ['Katagon', 55901], ['Gorgonite', 55904], ['Promethium', 55907], ['Platinum', 58113]]
    },
    wood: {
      boxes: { 54895: 'Wood', 54897: 'Oak', 54899: 'Willow', 54901: 'Teak', 54903: 'Maple', 54905: 'Acadia', 54907: 'Mahogany', 54909: 'Yew', 54911: 'Magic', 54913: 'Elder', 58253: 'Eternal magic' },
      container: 937, roster: 7206   // logs + wood spirits + bird's nests
    },
    soil: {
      boxes: { 49538: 'Archaeological soil box' }, capVb: 47021,    // capacity varbit: 0->50,1->100,2->250,3->500
      // soil counts are VARPS read DIRECTLY (the value IS the count) -- NOT varbits.
      varp: [['Ancient gravel', 9370], ['Saltwater mud', 9371], ['Fiery brimstone', 9372], ['Aerated sediment', 9373], ['Earthen clay', 9374], ['Volcanic ash', 9578]]
    },
    gem: {
      // The upgraded bag's counts live in varps 13475/13476, 7 bits each (CS2 clientscript-13776
      // case 31455), listed in in-game display order (the ids are not sequential there).
      boxes: { 31455: 'Upgraded' },                                // tier label; base bag 18338 = 'Basic'
      vb: [['Sapphire', 61469], ['Emerald', 61470], ['Ruby', 61471], ['Diamond', 61472],
           ['Dragonstone', 61473], ['Opal', 61475], ['Jade', 61474], ['Red topaz', 61476]]
      // perCap unset: the 7-bit field caps at 127 and the cache states no real per-gem limit.
    },
    plank: {
      boxes: { 51022: 'Plank box' }, container: 895, roster: 16107
    },
    essence: {   // RC essence pouches; COUNT and stored TYPE are PER-POUCH.
                 // Tuple = [name, countVb, typeVb, capacity, pouchItemId, decayVarp, decayMax].
                 // Durability% = (decayMax - varp)*100/decayMax, integer div (Med/Large/Giant wear
                 // on varps 3217/3218/3219, max 800/1000/1200); Small never decays and the
                 // Conservation of Energy relic forces 100%. The Massive pouch (24205) packs count
                 // (bits 0-4), durability (bits 6-15 of 756) and type (bits 29-31) in Extra_int key 0.
      pouches: [
        ['Small pouch',     16497, 16502, 3,  5509,  0,    0],
        ['Medium pouch',    16498, 16503, 6,  5510,  3217, 800],
        ['Large pouch',     16499, 16504, 9,  5512,  3218, 1000],
        ['Giant pouch',     16500, 16505, 12, 5514,  3219, 1200],
        ['Massive pouch',   'massive', 24205, 18, 24205, 0, 0],
        ['Expansive pouch', 57685, 57686, 70, 58451, 0, 0]
      ],
      massive: { item: 24205, cap: 18, durMax: 756 },
      types: { 0: ['Pure essence', 7936], 1: ['Rune essence', 1436], 2: ['Pure essence', 7936], 3: ['Impure essence', 55667] }
    },
    clue: {      // Charos' clue carrier: each tier's scroll + casket count is a VARP read directly (like soil)
      boxes: { 47836: "Charos' clue carrier" },
      varp: [['Easy clue', 8681], ['Medium clue', 8682], ['Hard clue', 8683], ['Elite clue', 8684], ['Master clue', 8685],
             ['Easy casket', 8686], ['Medium casket', 8687], ['Hard casket', 8688], ['Elite casket', 8689], ['Master casket', 8690]]
    },
    rune: {      // rune pouches (Small/Large/Grasping + every dye): per-slot count in Extra_ints keys 0,2,3,4
                 // (value=count); key 1 packs each slot's rune TYPE as 6-bit indices (LSB=first slot, RS3 order)
      items: {
        38451: 'Small rune pouch', 38453: 'Large rune pouch', 44390: 'Small rune pouch (red)',
        44393: 'Large rune pouch (red)', 44395: 'Small rune pouch (blue)', 44398: 'Large rune pouch (blue)',
        44400: 'Small rune pouch (yellow)', 44403: 'Large rune pouch (yellow)', 44405: 'Small rune pouch (green)',
        44408: 'Large rune pouch (green)', 44410: 'Small rune pouch (purple)', 44413: 'Large rune pouch (purple)',
        44415: 'Small rune pouch (orange)', 44418: 'Large rune pouch (orange)', 44420: 'Small rune pouch (black)',
        44423: 'Large rune pouch (black)', 44425: 'Small rune pouch (pink)', 44428: 'Large rune pouch (pink)',
        52215: 'Grasping rune pouch', 52217: 'Grasping rune pouch', 52218: 'Grasping rune pouch (blue)',
        52220: 'Grasping rune pouch (blue)', 52221: 'Grasping rune pouch (black)', 52223: 'Grasping rune pouch (black)',
        52224: 'Grasping rune pouch (green)', 52226: 'Grasping rune pouch (green)', 52227: 'Grasping rune pouch (orange)',
        52229: 'Grasping rune pouch (orange)', 52230: 'Grasping rune pouch (pink)', 52232: 'Grasping rune pouch (pink)',
        52233: 'Grasping rune pouch (purple)', 52235: 'Grasping rune pouch (purple)', 52236: 'Grasping rune pouch (red)',
        52238: 'Grasping rune pouch (red)', 52239: 'Grasping rune pouch (yellow)', 52241: 'Grasping rune pouch (yellow)',
        54122: 'Rune pouch'
      }
    },
    quiver: {    // Pernix's quivers: ammo slot 0 packs (count<<8)|typeByte in key 0 -> count=(v>>8)&0xFFFF; key 1 = 2nd-ammo count
      items: { 52581: "Pernix's quiver (blue)", 52583: "Pernix's quiver (green)", 52584: "Pernix's quiver (purple)" }
    },
    nexus:  { container: 953 },   // Necromancy nexus: Bone/Miasma/Flesh/Spirit rune + Ectoplasm (generic container read)
    brooch: { container: 891 },   // Brooch of the Gods: decorated/exquisite urns + effigies (generic container read)
    money:  { container: 623 },   // Money pouch: Coins (995) split across slots; total = remainder gp + billions*1e9
    sandy:  { vb: [['Sandy Sand', 61090]] }   // Sandy Sand currency: count lives in varbit 61090 (no container; item 61835 is the icon)
  };
  // RS3 rune index -> name (1..21); combination runes (5..10) are a best-effort ordering.
  const RUNE_NAMES = { 1: 'Air', 2: 'Water', 3: 'Earth', 4: 'Fire', 5: 'Dust', 6: 'Lava', 7: 'Mist', 8: 'Mud', 9: 'Smoke', 10: 'Steam', 11: 'Mind', 12: 'Body', 13: 'Cosmic', 14: 'Chaos', 15: 'Nature', 16: 'Law', 17: 'Death', 18: 'Astral', 19: 'Blood', 20: 'Soul', 21: 'Wrath', 22: 'Time' };
  // Item NAME -> item id for grid icons of the varbit/varp-backed boxes; no entry = text cell.
  const STOR_ICON = {
    'Copper': 436, 'Tin': 438, 'Iron': 440, 'Coal': 453, 'Silver': 442, 'Mithril': 447, 'Adamantite': 449,
    'Luminite': 44820, 'Gold': 444, 'Runite': 451, 'Orichalcite': 44822, 'Drakolith': 44824, 'Necrite': 44826,
    'Phasmatite': 44828, 'Banite': 21778, 'Light animica': 44830, 'Dark animica': 44832, 'Novite': 17630,
    'Bathus': 17632, 'Marmaros': 17634, 'Kratonium': 17636, 'Fractite': 17638, 'Zephyrium': 17640,
    'Argonite': 17642, 'Katagon': 17644, 'Gorgonite': 17646, 'Promethium': 17648, 'Platinum': 59207,
    'Sapphire': 1623, 'Emerald': 1621, 'Ruby': 1619, 'Diamond': 1617, 'Dragonstone': 1631,
    'Opal': 1625, 'Jade': 1627, 'Red topaz': 1629,
    'Ancient gravel': 49517, 'Saltwater mud': 49519, 'Fiery brimstone': 49521, 'Aerated sediment': 49523,
    'Earthen clay': 49525, 'Volcanic ash': 50696,
    'Air rune': 556, 'Water rune': 555, 'Earth rune': 557, 'Fire rune': 554, 'Dust rune': 4696, 'Lava rune': 4699,
    'Mist rune': 4695, 'Mud rune': 4698, 'Smoke rune': 4697, 'Steam rune': 4694, 'Mind rune': 558, 'Body rune': 559,
    'Cosmic rune': 564, 'Chaos rune': 562, 'Nature rune': 561, 'Law rune': 563, 'Death rune': 560, 'Astral rune': 9075,
    'Blood rune': 565, 'Soul rune': 566, 'Wrath rune': 21773, 'Time rune': 58450,
    'Small pouch': 5509, 'Medium pouch': 5510, 'Large pouch': 5512, 'Giant pouch': 5514, 'Expansive pouch': 58451,
    'Coins': 995, 'Sandy Sand': 61835,
    'Easy clue': 42006, 'Medium clue': 42007, 'Hard clue': 42008, 'Elite clue': 42009, 'Master clue': 42010,
    'Easy casket': 42001, 'Medium casket': 42002, 'Hard casket': 42003, 'Elite casket': 42004, 'Master casket': 42005
  };
  // Quiver arrow-type byte (key0 & 0xFF) -> ammo name; byte is the key of cache enum 16608.
  // The bolt slot stores only a count, no type byte, so bolts can't be named from quiver data.
  const AMMO_TYPE = {
    1: 'Abyssalbane arrow', 2: 'Abyssalbane bolt', 3: 'Adamant arrow', 4: 'Adamant bolts', 5: 'Adamant brutal', 6: 'Araxyte arrow',
    7: 'Ascendri bolts', 8: 'Ascendri bolts (e)', 9: 'Ascension bolts', 10: 'Bakriminel bolts', 11: 'Barbed bolts', 12: 'Basiliskbane arrow',
    13: 'Basiliskbane bolt', 14: 'Black bolts', 15: 'Black brutal', 16: 'Blurite bolts', 17: 'Bolt rack', 18: 'Bone bolts',
    19: 'Broad-tipped bolts', 20: 'Broad arrow', 21: 'Bronze arrow', 22: 'Bronze bolts', 23: 'Bronze brutal', 24: 'Coral bolts',
    25: 'Dark arrow', 26: 'Diamond bolts', 27: 'Diamond bolts (e)', 28: 'Dragon arrow', 29: 'Dragon bolts', 30: 'Dragon bolts (e)',
    31: 'Dragonbane arrow', 32: 'Dragonbane bolt', 33: 'Emerald bolts', 34: 'Emerald bolts (e)', 35: 'Fragment arrows', 36: 'Fragment bolts',
    37: 'Guthix arrows', 38: 'Hand cannon shot', 39: 'Ice arrows', 40: 'Iron arrow', 41: 'Iron bolts', 42: 'Iron brutal',
    43: 'Jade bolts', 44: 'Jade bolts (e)', 45: 'Kebbit bolts', 46: 'Long kebbit bolts', 47: 'Mithril arrow', 48: 'Mithril bolts',
    49: 'Mithril brutal', 50: 'Ogre arrow', 51: 'Onyx bolts', 52: 'Onyx bolts (e)', 53: 'Opal bolts', 54: 'Opal bolts (e)',
    55: 'Pearl bolts', 56: 'Pearl bolts (e)', 57: 'Royal bolts', 58: 'Ruby bolts', 59: 'Ruby bolts (e)', 60: 'Rune arrow',
    61: 'Rune brutal', 62: 'Rune bolts', 63: 'Sapphire bolts', 64: 'Sapphire bolts (e)', 65: 'Saradomin arrows', 66: 'Silver bolts',
    67: 'Steel arrow', 68: 'Steel bolts', 69: 'Steel brutal', 70: 'Topaz bolts', 71: 'Topaz bolts (e)', 72: 'Wallasalkibane arrow',
    73: 'Wallasalkibane bolt', 74: 'Zamorak arrows', 75: 'Wild arrow', 76: 'Blisterwood stakes (ammo)', 77: 'Wyvern spines', 78: 'Stalker arrow',
    79: 'Opal bakriminel bolts', 80: 'Sapphire bakriminel bolts', 81: 'Jade bakriminel bolts', 82: 'Pearl bakriminel bolts', 83: 'Emerald bakriminel bolts', 84: 'Red topaz bakriminel bolts',
    85: 'Ruby bakriminel bolts', 86: 'Diamond bakriminel bolts', 87: 'Dragonstone bakriminel bolts', 88: 'Onyx bakriminel bolts', 89: 'Hydrix bakriminel bolts', 90: 'Opal bakriminel bolts (e)',
    91: 'Sapphire bakriminel bolts (e)', 92: 'Jade bakriminel bolts (e)', 93: 'Pearl bakriminel bolts (e)', 94: 'Emerald bakriminel bolts (e)', 95: 'Red topaz bakriminel bolts (e)', 96: 'Ruby bakriminel bolts (e)',
    97: 'Diamond bakriminel bolts (e)', 98: 'Dragonstone bakriminel bolts (e)', 99: 'Onyx bakriminel bolts (e)', 100: 'Hydrix bakriminel bolts (e)', 101: 'Blight bolts', 102: 'Black stone arrows',
    103: 'Deathspore arrows', 104: 'Splintering arrows', 105: 'Dinarrow', 106: 'Jas dragonbane arrow', 107: 'Jas demonbane arrow', 108: 'Ful arrow',
    109: 'Wen arrow', 110: 'Bik arrow', 111: 'Primal arrow', 112: 'Primal bolts', 113: 'Havensilver bolt', 114: 'Havensilver bolt +1',
    115: 'Havensilver bolt +2'
  };
  // Quiver ammo-type byte -> item id (enum 16608 value), for the ammo icon in the Storage grid.
  const AMMO_ID = {
    1: 21655, 2: 21675, 3: 890, 4: 9143, 5: 4798, 6: 31737, 7: 31868, 8: 31881, 9: 28465, 10: 24116,
    11: 881, 12: 21650, 13: 21670, 14: 13083, 15: 4788, 16: 9139, 17: 4740, 18: 8882, 19: 13280, 20: 4160,
    21: 882, 22: 877, 23: 4773, 24: 24304, 25: 29617, 26: 9340, 27: 9243, 28: 11212, 29: 9341, 30: 9244,
    31: 21640, 32: 21660, 33: 9338, 34: 9241, 35: 28464, 36: 28463, 37: 19157, 38: 15243, 39: 78, 40: 884,
    41: 9140, 42: 4778, 43: 9335, 44: 9237, 45: 10158, 46: 10159, 47: 888, 48: 9142, 49: 4793, 50: 2866,
    51: 9342, 52: 9245, 53: 879, 54: 9236, 55: 880, 56: 9238, 57: 24336, 58: 9339, 59: 9242, 60: 892,
    61: 4803, 62: 9144, 63: 9337, 64: 9240, 65: 19152, 66: 9145, 67: 886, 68: 9141, 69: 4783, 70: 9336,
    71: 9239, 72: 21645, 73: 21665, 74: 19162, 75: 34235, 76: 35705, 77: 35989, 78: 41575, 79: 41634, 80: 41639,
    81: 41644, 82: 41649, 83: 41654, 84: 41659, 85: 41664, 86: 41669, 87: 41674, 88: 41679, 89: 41684, 90: 41623,
    91: 41624, 92: 41625, 93: 41626, 94: 41627, 95: 41628, 96: 41629, 97: 41630, 98: 41631, 99: 41632, 100: 41633,
    101: 42748, 102: 47501, 103: 52299, 104: 52289, 105: 53038, 106: 53043, 107: 53050, 108: 53055, 109: 53060, 110: 53065,
    111: 58036, 112: 58041, 113: 60312, 114: 60317, 115: 60322
  };
  const SOIL_CAP = [50, 100, 250, 500];
  let storageData = null; storageVbMap = null; let storageSig = ''; let storageFetching = false;
  let storageVbMapTry = 0;    // last varbitMap attempt (ms) -- bridge().varbitMap is SYNCHRONOUS native work
  // cfg.roster -> ordered item ids, read from the SAME cache enum the game's own fill script walks
  // (Plank box CS2 case 51022 does enum_getvalue(0, 33, 16107, i) for i < ENUM_GETOUTPUTCOUNT).
  // Hardcoding these rosters is what hid Magic/Elder/Eternal planks: the list was the whitelist, so
  // a tier Jagex added and we never transcribed was dropped without a trace. Cached per session --
  // enumInfo is synchronous native cache work and must not run on every poll tick.
  const storRoster = {}, storRosterTry = {};
  async function storRosterIds(eid) {
    if (!eid) return null;
    if (storRoster[eid]) return storRoster[eid];
    if (!bridge() || !bridge().enumInfo) return null;
    // Same guard as ensureVbMap: EnumJson runs cache work SYNCHRONOUSLY on the render thread, so a
    // permanently closed cache must not re-read it four times a second for the life of the session.
    const now = Date.now(); if (now - (storRosterTry[eid] || 0) < 2000) return null; storRosterTry[eid] = now;
    let m = await bridgeJson('enumInfo', eid);
    if (!m) return null;
    // Enum keys are the game's display order; they are not guaranteed to start at 0 (enum 7206
    // does, enum 6544 starts at 1), so sort numerically rather than counting from zero.
    const ids = Object.keys(m).map(Number).sort((a, b) => a - b).map(k => m[k] | 0).filter(v => v > 0);
    if (!ids.length) return null;      // CONFIGS index not open yet -> retry next poll, do NOT cache
    storRoster[eid] = ids;
    return ids;
  }
  // Runecrafting pouches never degrade while Conservation of Energy is harnessed
  // (panel_archresearch.js reads the live relic slots).
  function storNoDecay() {
    return typeof archRelicActive === 'function' && archRelicActive('Conservation of Energy');
  }

  // varbitMap is keyed by varp -> [[varbitId,lsb,msb],..]; invert to varbitId -> {varp,lsb,msb}.
  function buildVbReverse(vbm) { const m = {}; for (const vp in vbm) for (const d of vbm[vp]) m[d[0]] = { varp: +vp, lsb: d[1], msb: d[2] }; return m; }
  async function ensureVbMap() {
    // VarbitMapJson returns "{}" while the CONFIGS index is not open (CacheReader.cpp:1363,1404),
    // and buildVbReverse({}) is a TRUTHY empty object -- caching that latched EVERY varbit in the
    // app to 0 for the rest of the session: the lockbox reads "Solved." forever, and achievements /
    // farming / abilities / quests / mysteries all silently read zero. Only accept a non-empty map,
    // and back off between retries, because bridge().varbitMap does NOT go through served()/
    // ReadAsync like varps does (Bridge.cpp:921-924) -- it runs cache work synchronously on the
    // render thread, and an unthrottled retry would do that on every readVarbitValues call.
    if (storageVbMap || !bridge().varbitMap) return;
    const now = Date.now(); if (now - storageVbMapTry < 2000) return; storageVbMapTry = now;
    try { const m = buildVbReverse(await bridgeJson('varbitMap')); if (Object.keys(m).length) storageVbMap = m; } catch (e) {}
  }
  function readVb(vbId, vpData) {
    const r = storageVbMap && storageVbMap[vbId]; if (!r) return null;
    const v = (vpData[r.varp] || 0) >>> 0, w = r.msb - r.lsb, mask = w >= 31 ? 0xffffffff : ((1 << (w + 1)) - 1);
    return (v >>> r.lsb) & mask;
  }
  // Passage of the abyss: charges in Extra_int var 30214, and the filled slots packed into the
  // next var as 4-bit nibbles (least significant first, one per slot, in the order the game
  // lists them). Each nibble indexes enum 15018, which names the jewellery; unset slots are
  // simply absent, so the nibble count is the fill count. Variants share the same layout.
  // Icons for the Passage's slots: enum 15018 names the jewellery generically, so map each
  // name to a representative item id (charge variants share one icon).
  const PASSAGE_ICON = {
    'Ferocious ring': 15400, 'Games necklace': 3863, 'Ring of duelling': 2562,
    'Ring of wealth': 2572, 'Skills necklace': 11105, 'Ring of slaying': 13286,
    'Amulet of glory': 1704, 'Combat bracelet': 11126, 'Dig Site pendant': 11190,
    'Ring of respawn': 39366, 'Enlightened amulet': 39387, "Traveller's necklace": 39372,
    "Delver's anklet": 59241
  };
  // One item id per colour, all sharing the same Extra_ints layout. Confirmed in-game:
  // 44542 red, 44543 purple, 44544 green, 44545 yellow.
  const PASSAGE_IDS = [44542, 44543, 44544, 44545];
  // The unattuned form cannot teleport, so it stores no jewellery and holds no charges. Listed
  // separately rather than dropped: holding one should say why the panel is empty.
  const PASSAGE_UNATTUNED = 44540;
  const PASSAGE_ENUM = 15018, PASSAGE_SLOTS = 6;
  const PASSAGE_FREE_VB = 52159;   // Dark Facet of Passage: teleports stop consuming charges
  let passageNames = null;
  async function readPassage(held) {
    if (!bridge().itemExtraInts || !held) return null;
    let id = 0;
    for (const pid of PASSAGE_IDS) if (held.has(pid)) { id = pid; break; }
    if (!id) {
      if (held.has(PASSAGE_UNATTUNED)) return { name: 'Unattuned - attune it to teleport', cap: PASSAGE_SLOTS, items: [] };
      return null;
    }
    // held merges backpack + worn, so try the backpack first and fall back to equipment.
    let k = null;
    for (const cont of [93, 94]) {
      let ei = null;
      ei = await bridgeJson('itemExtraInts', myPid(), cont, id);
      if (ei && ei.key && ((ei.key['0'] | 0) || (ei.key['1'] | 0))) { k = ei.key; break; }
    }
    if (!k) return null;
    const charges = k['0'] | 0, packed = (k['1'] | 0) >>> 0;
    if (!passageNames && bridge().enumInfo) {
      passageNames = (await bridgeJson('enumInfo', PASSAGE_ENUM)) || null;
    }
    const items = [];
    let v = packed;
    while (v && items.length < PASSAGE_SLOTS) {
      const ix = v & 15; v >>>= 4;
      if (!ix) continue;
      const nm = (passageNames && passageNames[String(ix)]) || ('Unknown (' + ix + ')');
      items.push([nm, 1, PASSAGE_ICON[nm] || 0]);
    }
    // Dark Facet of Passage (varbit 52159) makes teleports free, so the counter stops mattering.
    let unlimited = false;
    try { const vb = await readVarbitValues([PASSAGE_FREE_VB]); unlimited = ((vb && vb[PASSAGE_FREE_VB]) | 0) === 1; } catch (e) {}
    const label = unlimited ? 'Unlimited charges' : ('Charges ' + charges.toLocaleString());
    return { name: label, cap: PASSAGE_SLOTS, items: items };
  }
  function storHeldBox(cat, held) { for (const id in STORAGE[cat].boxes) if (held.has(+id)) return STORAGE[cat].boxes[id]; return null; }

  async function fetchStorage() {
    if (!bridge() || storageFetching) return; storageFetching = true;
    try {
      // Contents live in varbits/varps/containers, readable WITHOUT the box; inventory only LABELS the held tier.
      let inv = await bridgeJson('inventory', myPid());
      const held = new Set((inv && inv.items || []).map(it => it[1]));
      let eq = await bridgeJson('equipment', myPid());   // worn items (rune pouch / quiver are equipped)
      (eq && eq.items || []).forEach(it => held.add(it[1]));
      await ensureVbMap();
      // Harnessed relic powers gate what some rows should SAY (pouch decay); own throttle,
      // so calling it here is a no-op most polls and needs no tab to have been opened.
      try { if (typeof archRelicEnsure === 'function') await archRelicEnsure(); } catch (e) {}
      const varpSet = new Set();
      const addVb = (vb) => { const r = storageVbMap && storageVbMap[vb]; if (r) varpSet.add(r.varp); };
      STORAGE.ore.vb.forEach(x => addVb(x[1]));
      STORAGE.gem.vb.forEach(x => addVb(x[1]));
      STORAGE.essence.pouches.forEach(p => { if (p[1] !== 'massive') { addVb(p[1]); addVb(p[2]); } if (p[5]) varpSet.add(p[5]); });
      addVb(STORAGE.soil.capVb);
      STORAGE.soil.varp.forEach(x => varpSet.add(x[1]));
      STORAGE.clue.varp.forEach(x => varpSet.add(x[1]));
      STORAGE.sandy.vb.forEach(x => addVb(x[1]));
      let vp = {};
      if (varpSet.size) vp = await bridgeJson('varps', myPid(), [...varpSet].join(','));
      // item tuple = [name, count, itemId, source] -- source is the provenance shown in the cell tooltip.
      const vbSrc = (vb) => { const r = storageVbMap && storageVbMap[vb]; return r ? ('varbit ' + vb + ' = varp ' + r.varp + ' bits ' + r.lsb + '-' + r.msb) : ('varbit ' + vb); };
      const fromVb = (list) => list.map(x => [x[0], readVb(x[1], vp) || 0, STOR_ICON[x[0]] || 0, vbSrc(x[1])]).filter(x => x[1] > 0);
      const fromVarp = (list) => list.map(x => [x[0], (vp[x[1]] || 0) >>> 0, STOR_ICON[x[0]] || 0, 'varp ' + x[1]]).filter(x => x[1] > 0);
      const oreBox = storHeldBox('ore', held), woodBox = storHeldBox('wood', held), soilBox = storHeldBox('soil', held), gemBox = storHeldBox('gem', held);
      const out = { ore: null, wood: null, soil: null, gem: null, plank: null, essence: null, clue: null, rune: null, quiver: null, nexus: null, brooch: null, money: null, currency: null, sandy: null };
      const oreItems = fromVb(STORAGE.ore.vb);
      if (oreBox || oreItems.length) out.ore = { name: oreBox || '', items: oreItems };
      // Gem bag tier: the cache defines no "upgrade purchased" flag, so ownership is inferred from
      // where the data lives; the base bag (18338) keeps its counts in its own Extra_ints (held only).
      const gemItems = fromVb(STORAGE.gem.vb);
      if (gemBox || gemItems.length) {
        out.gem = { name: gemBox || STORAGE.gem.boxes[31455], items: gemItems };
      } else if (held.has(18338) && bridge().itemExtraInts) {
        let ei = await bridgeJson('itemExtraInts', myPid(), 93, 18338);
        // Extra_ints is flat [key0,val0,key1,val1,..], so "Extra_ints[1]" = first VALUE = pos[0].
        const packed = (ei && ei.pos && ei.pos.length ? ei.pos[0] : 0) || 0;
        const gbyte = ['key 0 byte 0', 'key 0 byte 1', 'key 0 byte 2', 'key 0 byte 3'];
        const gi = [['Sapphire', packed % 256], ['Emerald', Math.floor(packed / 256) % 256], ['Ruby', Math.floor(packed / 65536) % 256], ['Diamond', Math.floor(packed / 16777216) % 256]]
          .map((x, i) => [x[0], x[1], STOR_ICON[x[0]] || 0, 'Extra_int ' + gbyte[i] + ' (item 18338)']).filter(x => x[1] > 0);
        out.gem = { name: 'Basic', items: gi };
      }
      const soilItems = fromVarp(STORAGE.soil.varp);
      if (soilBox || soilItems.length) out.soil = { name: soilBox || '', cap: SOIL_CAP[readVb(STORAGE.soil.capVb, vp) || 0] || 50, items: soilItems };
      // Container-backed boxes. cfg.roster (a cache enum) fixes the game's display ORDER; the counts
      // and names come from the container itself, so the roster decides sequence, never membership.
      // Anything in the container the roster does not list is appended rather than dropped -- the old
      // code filtered by a hardcoded list whose "show everything" fallback only fired when the list
      // matched NOTHING, so a partial match (5 of 7 plank types) silently swallowed the rest.
      const readContainer = async (cfg, heldName) => {
        let r = await bridgeJson('containerItems', myPid(), cfg.container);
        const rows = (r && r.items) || [];
        // One id can span several slots; sum the stacks and keep every slot for the cell tooltip.
        const byId = {}, nameOf = {}, slotsOf = {};
        rows.forEach(it => {
          const id = it[1];
          byId[id] = (byId[id] || 0) + it[2];
          if (!(id in nameOf)) { nameOf[id] = it[3] || ('#' + id); slotsOf[id] = []; }
          slotsOf[id].push(it[0]);
        });
        const order = (await storRosterIds(cfg.roster)) || [];
        const seen = new Set(), ids = [];
        order.forEach(id => { if (!seen.has(id)) { seen.add(id); ids.push(id); } });
        Object.keys(byId).map(Number).forEach(id => { if (!seen.has(id)) { seen.add(id); ids.push(id); } });
        const items = ids.filter(id => (byId[id] || 0) > 0).map(id =>
          [nameOf[id], byId[id], id,
           'container ' + cfg.container + ' · item ' + id + ' · slot ' + slotsOf[id].join(', ')]);
        return (heldName || items.length) ? { name: heldName || '', items } : null;
      };
      out.wood  = await readContainer(STORAGE.wood,  woodBox);
      out.plank = await readContainer(STORAGE.plank, storHeldBox('plank', held));
      out.nexus  = await readContainer(STORAGE.nexus,  null);
      out.brooch = await readContainer(STORAGE.brooch, null);
      // read an item's Extra_ints from the worn (94) or backpack (93) slot; ei.key[k] = value at key k.
      const eiFor = async (id) => {
        if (!bridge().itemExtraInts) return null;
        for (const cid of [94, 93]) { { const r = await bridgeJson('itemExtraInts', myPid(), cid, id); if (r && r.present) return r; } }
        return null;
      };
      // Essence pouches: tuple = [name, count, pouchIconId, src, cap, durStr, essenceIconId],
      // essenceIconId 0 = empty. The Massive pouch shows whenever held (durability matters empty).
      const essType = (t) => STORAGE.essence.types[t & 7] || STORAGE.essence.types[0];
      const eM = STORAGE.essence.massive;
      const essItems = [];
      for (const p of STORAGE.essence.pouches) {
        const pname = p[0], cntVb = p[1], typeVb = p[2], cap = p[3], pouchIcon = p[4];
        if (cntVb === 'massive') {
          if (!held.has(eM.item)) continue;
          const ei = await eiFor(eM.item);
          const v = ((ei && ei.key && ei.key[0]) || 0) >>> 0;
          const cnt = v & 0x1f, dur = (v >>> 6) & 0x3ff, tt = (v >>> 29) & 7, ty = essType(tt);
          essItems.push([pname + (cnt > 0 ? ' · ' + ty[0] : ''), cnt, pouchIcon,
            'item ' + eM.item + ' · Extra_int key 0 · count bits 0-4 · type bits 29-31 · durability bits 6-15',
            cap, 'Durability ' + dur + ' / ' + eM.durMax + ' (' + Math.round(dur / eM.durMax * 100) + '%)',
            cnt > 0 ? ty[1] : 0]);
          continue;
        }
        // A HELD pouch stays rendered at count 0: dropping the cell made the strip
        // reflow on every fill/empty cycle mid-training (owner-reported jitter).
        const cnt = readVb(cntVb, vp) || 0;
        if (cnt <= 0 && !held.has(pouchIcon)) continue;
        const ty = essType(readVb(typeVb, vp) || 0);
        // decayVarp p[5] / decayMax p[6]; 0 = no decay (Small/Massive/Expansive).
        const decayVarp = p[5], decayMax = p[6];
        let durStr = '';
        if (decayVarp && decayMax) {
          const wear = (vp[decayVarp] || 0) >>> 0;
          const pct = Math.max(0, Math.floor((decayMax - Math.min(wear, decayMax)) * 100 / decayMax));
          // Conservation of Energy stops pouch decay outright, so the wear varp is moot while
          // it is harnessed. The relic state is READ (varp 12086 + its preset's varbits), so
          // state the effect as fact instead of hedging "100% if the relic is active".
          durStr = storNoDecay()
            ? 'Durability 100% - Conservation of Energy is harnessed, so pouches do not degrade'
            : 'Durability ' + pct + '% (' + (decayMax - wear) + ' / ' + decayMax + ' · varp ' + decayVarp + ')';
        }
        essItems.push([pname + (cnt > 0 ? ' · ' + ty[0] : ''), cnt, pouchIcon,
          'count = ' + vbSrc(cntVb) + ' · type = ' + vbSrc(typeVb), cap, durStr, cnt > 0 ? ty[1] : 0]);
      }
      if (essItems.length) out.essence = { name: '', essence: true, items: essItems };
      const clueItems = fromVarp(STORAGE.clue.varp);
      if (storHeldBox('clue', held) || clueItems.length) out.clue = { name: storHeldBox('clue', held) || '', items: clueItems };
      const runes = [];
      for (const id in STORAGE.rune.items) {
        if (!held.has(+id)) continue;
        const ei = await eiFor(+id); if (!ei || !ei.key) continue;
        const th = ei.key[1] >>> 0, slotKeys = [0, 2, 3, 4], items = [];   // key 1 packs each slot's rune type (6-bit, LSB first); counts at keys 0,2,3,4
        for (let i = 0; i < slotKeys.length; i++) {
          const idx = (th >> (6 * i)) & 0x3F; if (!idx) continue;
          const cnt = (ei.key[slotKeys[i]] || 0) >>> 0; if (cnt <= 0) continue;
          const rn = (RUNE_NAMES[idx] || ('Rune #' + idx)) + ' rune';
          items.push([rn, cnt, STOR_ICON[rn] || 0, 'Extra_int key ' + slotKeys[i] + ' · type = key 1 bits ' + (6 * i) + '-' + (6 * i + 5)]);
        }
        runes.push({ name: STORAGE.rune.items[id], items });
      }
      if (runes.length) out.rune = runes;
      const quivers = [];
      for (const id in STORAGE.quiver.items) {
        if (!held.has(+id)) continue;
        const ei = await eiFor(+id); if (!ei || !ei.key) continue;
        // key0 packs the equipped ammo AND the stored ammo's type: [secondaryType:9 | primaryCount:15 |
        // primaryType:8]; both type fields are keys of cache enum 16608 (AMMO_TYPE). key1 = stored count.
        const k0 = (ei.key[0] || 0) >>> 0, k1 = (ei.key[1] || 0) >>> 0;
        const pType = k0 & 0xFF, pCount = (k0 >>> 8) & 0x7FFF, sType = (k0 >>> 23) & 0x1FF, sCount = k1 & 0xFFFF;
        const items = [];
        if (pType && AMMO_TYPE[pType] && pCount > 0) items.push([AMMO_TYPE[pType], pCount, AMMO_ID[pType] || 0, 'Extra_int key 0 · count bits 8-22, type bits 0-7 (enum 16608)']);
        if (sType && AMMO_TYPE[sType] && sCount > 0) items.push([AMMO_TYPE[sType], sCount, AMMO_ID[sType] || 0, 'Extra_int key 0 type bits 23-31 + key 1 count (enum 16608)']);
        quivers.push({ name: STORAGE.quiver.items[id], items });
      }
      if (quivers.length) out.quiver = quivers;
      // Money pouch: item 995 holds the sub-billion remainder, item 54830 counts billions.
      try {
        const r = await bridgeJson('containerItems', myPid(), STORAGE.money.container);
        const rows = (r && r.items) || [];
        const stackOf = (id) => (rows.find(it => it[1] === id) || [0, 0, 0])[2] >>> 0;
        const total = stackOf(54830) * 1000000000 + stackOf(995);
        if (total > 0) out.money = { name: '', items: [['Coins', total, 995, 'container 623 · item 995 (gp) + item 54830 (×1e9)']] };
      } catch (e) {}
      // Currency pouch: inventory interface group 1473, comp 20 = currency icons; item id at
      // node+0x1a0, amount at node+0x1a8 (reader's "it"/"n"). Needs group 1473 present.
      try {
        const ig = await bridgeJson('interfaceGroup', myPid(), 1473);
        const cur = [];
        for (const w of ((ig && ig.widgets) || [])) {
          if (w.t && w.t[1] === 20 && w.it > 0 && (w.n || 0) > 0) {
            const nm = (w.x || '').replace(/<[^>]*>/g, '').trim();
            cur.push([nm || ('Item ' + w.it), w.n >>> 0, w.it, 'inventory currency pouch (group 1473)']);
          }
        }
        if (cur.length) out.currency = { name: '', items: cur };
      } catch (e) {}
      const sandyItems = fromVb(STORAGE.sandy.vb);
      if (sandyItems.length) out.sandy = { name: '', items: sandyItems };
      out.passage = await readPassage(held);
      storageData = out;
    } finally { storageFetching = false; }
    paneRun('storage', renderStorage);
  }

  // RS3-style count abbreviation: <100k full, 100k+ -> K, 1m+ -> M, 1b+ -> B, 1t+ -> T, 1q+ -> Q (2dp).
  function storAbbr(n) {
    return n >= 1e15 ? +(n / 1e15).toFixed(2) + 'Q' : n >= 1e12 ? +(n / 1e12).toFixed(2) + 'T'
         : n >= 1e9 ? +(n / 1e9).toFixed(2) + 'B' : n >= 1e6 ? +(n / 1e6).toFixed(2) + 'M'
         : n >= 1e5 ? Math.floor(n / 1e3) + 'K' : ('' + n);
  }
  function renderStorage() {
    const c = $('content');
    let wrap = $('storWrap');
    if (!wrap) {
      c.innerHTML = ''; wrap = document.createElement('div'); wrap.id = 'storWrap'; wrap.className = 'pane stor-wrap'; c.appendChild(wrap); storageSig = '';
    }
    const d = storageData;
    const list = [];
    const add = (title, b) => { if (b) list.push([title, b]); };
    const many = (title, arr) => { if (Array.isArray(arr)) arr.forEach(b => list.push([title, b])); };
    add('Ore box', d && d.ore); add('Wood box', d && d.wood); add('Plank box', d && d.plank);
    add('Soil box', d && d.soil); add('Gem bag', d && d.gem); add('Essence pouches', d && d.essence);
    add('Clue carrier', d && d.clue); many('Rune pouch', d && d.rune); many('Quiver', d && d.quiver);
    add('Nexus', d && d.nexus); add('Brooch of the Gods', d && d.brooch); add('Money pouch', d && d.money);
    add('Currency pouch', d && d.currency);
    add('Sandy Sand', d && d.sandy);
    add('Passage of the abyss', d && d.passage);
    const boxItems = (b) => b.items || [];
    // it[5] (durability text) is part of the signature so wear ticks - and the relic
    // flipping pouch decay off - actually repaint instead of waiting for a count change.
    const sig = storNoDecay() + '|' + list.map(([t, b]) => t + '|' + (b.name || '') + '|' + (b.cap || '') + '|' + boxItems(b).map(it => it[0] + ':' + it[1] + ':' + (it[2] || 0) + ':' + (it[5] || '')).join(',')).join(';');
    if (sig === storageSig) { sizeAllIcons(); return; }
    storageSig = sig;
    const scrollAt = c.scrollTop;   // a rebuild resets the .content scroll; restored below
    wrap.innerHTML = '';
    if (!list.length) { wrap.innerHTML = '<div class="stor-empty">No storage box contents found.</div>'; return; }
    const makeCell = (it, cap) => {
      const name = it[0], count = it[1], id = it[2] || STOR_ICON[name] || 0, src = it[3] || '', icap = it[4] || cap, extra = it[5] || '';
      const url = id ? resolveIcon(id) : '';
      const cell = document.createElement('div'); cell.className = 'bank-cell stor-cell';
      cell.dataset.tip = name + '\n' + count.toLocaleString() + (icap ? ' / ' + icap : '') + (extra ? '\n' + extra : '') + (src ? '\n' + src : '');  // -> #global-tip
      if (url) { const ico = document.createElement('div'); ico.className = 'bank-icon'; ico.dataset.itemId = String(id); setIconBg(ico, url); cell.appendChild(ico); }
      else { const tn = document.createElement('div'); tn.className = 'stor-tn'; tn.textContent = name; cell.appendChild(tn); }
      const fa = fmtAmt(count);
      const amt = document.createElement('span'); amt.className = 'bank-amt' + (fa.c ? ' ' + fa.c : ''); amt.textContent = fa.t || count; cell.appendChild(amt);
      return cell;
    };
    const gridOf = (items, cap) => { const g = document.createElement('div'); g.className = 'stor-grid'; items.forEach(it => g.appendChild(makeCell(it, cap))); return g; };
    // Essence cell: it = [name,count,pouchIcon,src,cap,durStr,essIcon].
    const makeEssenceCell = (it) => {
      const name = it[0], count = it[1], pouchId = it[2], src = it[3] || '', cap = it[4] || 0, dur = it[5] || '', essId = it[6] || 0;
      const cell = document.createElement('div'); cell.className = 'stor-ecell';
      cell.dataset.tip = name + '\n' + count.toLocaleString() + (cap ? ' / ' + cap : '') + (dur ? '\n' + dur : '') + (src ? '\n' + src : '');
      const mkIcon = (id, cls) => { const u = id ? resolveIcon(id) : ''; const e = document.createElement('div'); e.className = 'bank-icon ' + cls; if (u) { e.dataset.itemId = String(id); setIconBg(e, u); } return e; };
      cell.appendChild(mkIcon(pouchId, 'stor-epouch'));
      // The arrow + essence slot ALWAYS renders (empty box when essId 0): appearing and
      // vanishing with the contents resized the cell every fill/empty (owner-reported).
      const ar = document.createElement('span'); ar.className = 'stor-earrow'; ar.textContent = '›';
      if (!essId) ar.style.opacity = '0.35';
      cell.appendChild(ar);
      cell.appendChild(mkIcon(essId, 'stor-eess'));
      const cz = document.createElement('span'); cz.className = 'stor-ecap';
      cz.style.fontVariantNumeric = 'tabular-nums';
      // Reserve the full-count width ("12 / 12") so 1- vs 2-digit counts don't shift the row.
      if (cap) cz.style.minWidth = (2 * String(cap).length + 3) + 'ch';
      cz.innerHTML = count.toLocaleString() + (cap ? ' <span class="cap">/ ' + cap + '</span>' : '');
      cell.appendChild(cz);
      const dm = dur.match(/(\d+)%/); if (dm) { const db = document.createElement('span'); db.className = 'stor-edur'; db.textContent = dm[1] + '%'; cell.appendChild(db); }
      return cell;
    };
    for (const [title, b] of list) {
      const sec = document.createElement('div'); sec.className = 'stor-box';
      const h = document.createElement('div'); h.className = 'stor-h';
      h.innerHTML = title + (b.name ? ' · <span class="stor-tier">' + b.name + '</span>' : '');
      sec.appendChild(h);
      if (!b.items.length) {
        const e = document.createElement('div'); e.className = 'stor-empty'; e.textContent = 'empty'; sec.appendChild(e);
      } else if (b.essence) {
        const g = document.createElement('div'); g.className = 'stor-egrid'; b.items.forEach(it => g.appendChild(makeEssenceCell(it))); sec.appendChild(g);
      } else {
        sec.appendChild(gridOf(b.items, b.cap));
      }
      wrap.appendChild(sec);
    }
    if (scrollAt > 0) c.scrollTop = scrollAt;
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { STORAGE, ensureVbMap, fetchStorage, readVb });
registerTab({ id: 'storage', render: renderStorage, open: function () { storageSig = ''; fetchStorage(); } });
})();
