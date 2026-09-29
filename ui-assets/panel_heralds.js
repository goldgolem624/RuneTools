// RuneToolsX panel: Heralds of Crimson quest guide (quest 533, Havenhythe).
(function () {

  // Progress varbit 62090 (varp 13689 bits 0-7, cache quest config js5-2 archive 35 file 533):
  // start value 5, complete at 215. Requires Secrets of Amberfell.
  const HOC_PROG = 62090, HOC_DONE = 215;
  const HOC_PREP = 62091;   // varp 13689 bits 8-10: 1 = sent on to Zeke, 2 = Fox's task, 3 = traps armed,
                            // 4 = reporting back to Adam (62090 goes 65 during that talk, 70 when it ends)
  const HOC_PREP_COUNT = 62095;   // varp 13689 bits 17-19: 1 after Zeke, 2 after Liat, 3 after Samson,
                                  // 4 = halberd made, 5 = halberd shown to Liat
  const HOC_LIAT_DONE = 62099;    // varp 13689 bit 26: 1 once Liat has been talked to
  const HOC_ESTHER_DONE = 62098;  // varp 13689 bit 25: 1 while and after talking to Esther
  const HOC_SILVERQUILL = 62096;  // varp 13689 bits 20-21: 1 = preparations done, take the spines to Anya,
                                  // 2 = super antisanguine made, 3 = exalted lump given to Esther
  const HOC_SILVER_SPINES = 60338, HOC_SANGUINE_SPINES = 60339;
  // Anya in the Blighted Cave (the War's Retreat boss portal attuned to Silverquill lands next to her).
  const HOC_ANYA_X = 3421, HOC_ANYA_Y = 7899;
  // Anya's "Do you trust Anya?" chooser: every option works, so one is picked at random.
  const HOC_ANYA_TRUST_OPTS = ['i trust you', 'you seem trustworthy', 'you do keep running off', 'i do not trust you'];
  let hocAnyaTrustPick = -1;
  const HOC_ANYA_TRUST = 62122;   // varp 13691 bits 3-5: the trust answer picked (1-4), 0 = not asked yet
  // Her "Do you forgive Anya?" chooser comes after the trust one; any option works here too.
  const HOC_ANYA_FORGIVE_OPTS = ["there's nothing to forgive", 'i forgive you', 'only if you fix this', "no, i don't"];
  let hocAnyaForgivePick = -1;
  const HOC_ANYA_FORGIVE = 62123;   // varp 13691 bits 6-8: the forgive answer picked (1-4), 0 = not asked yet
  const HOC_CURE = 62097;           // varp 13689 bits 22-24: Anya's conversation 2 -> 3 -> 4, she takes the spines,
                                    // 5 = potion made, 6 = poured, 7 = told Anya (Curing Silverquill done)
  const HOC_SQ_POTION = 63953;      // Empowered antisanguine (silverquill)
  const HOC_SQ_ASLEEP = '#32856';   // Silverquill asleep in her den (action "Pour potion")
  const HOC_SQ_ASLEEP_ID = 32856;
  // Her den is not an instance: it lies just north of the "Cave ventricle" (Enter, 136536 at 3420,7911) and
  // Silverquill (3406,7920) is loaded while you still stand outside, so where you stand says which side you
  // are on. The way back out is the other ventricle (Exit, 136539 at 3419,7918).
  const HOC_VENTRICLE = 136536, HOC_VENTRICLE_X = 3420, HOC_VENTRICLE_Y = 7911;
  const HOC_VENTRICLE_EXIT = 136539, HOC_VENTRICLE_EXIT_X = 3419, HOC_VENTRICLE_EXIT_Y = 7918;
  const HOC_AZUR = '#32813', HOC_AZUR_X = 3852, HOC_AZUR_Y = 1637;   // Azur, Exalted Quarry (Heathervein)
  // After Azur and Aurora (varp 13691 bit 17 = 1) the quarry work is read from the backpack.
  const HOC_QUARRY = 62127;
  const HOC_LUMPS = [63926, 63927, 63928];           // essence lump, chunk, slab
  const HOC_EXALTED_LUMPS = [63929, 63930, 63931];   // exalted essence lump, chunk, slab
  const HOC_SUPER_ANTISANG = [64019, 64021, 64023, 64025];   // Super antisanguine (4) .. (1)
  const HOC_KWUARM_UNF = 105;
  const HOC_COLOSSAL_LEG = 63925;   // from the essence veins, while at the quarry
  const HOC_VEINS = [140915, 140917, 140918, 140919, 140920, 140921,            // essence veins (not cracked)
                     140923, 140925, 140926, 140927, 140928, 140929,            // essence deposits
                     140931, 140933, 140934, 140935, 140936, 140937];           // large essence deposits
  const HOC_PROCESSOR = 140911, HOC_QUARRY_BOX = 140912, HOC_COLOSSUS = 140913;
  const HOC_PROCESSOR_X = 3854, HOC_PROCESSOR_Y = 1635, HOC_COLOSSUS_X = 3850, HOC_COLOSSUS_Y = 1627;
  const HOC_BANK_BOOTH = 137292, HOC_BANK_X = 3878, HOC_BANK_Y = 1680;   // north of the quarry
  // Liat's halberd: Samson's smithing house north of the quarry, anvil on its west side by the furnace.
  const HOC_SAMSON = '#32807', HOC_SAMSON_X = 3856, HOC_SAMSON_Y = 1669;
  const HOC_ANVIL = 113258, HOC_ANVIL_X = 3850, HOC_ANVIL_Y = 1670;
  const HOC_UNF_SMITHING = 47068;   // Unfinished smithing item: the halberd has been started
  const HOC_HALBERD = 63860;   // Havensilver halberd
  const HOC_HAVENSILVER_BAR = 60296, HOC_MAPLE_LOGS = 1517;   // the halberd takes 4 bars and 1 maple logs
  // Fox's traps: Fox waits inside the cave in Hollow Hill (loc 136498 "Cave", action Enter). The game's own
  // box for this loc is about 33 tiles tall, so its highlight reaches far above the doorway.
  const HOC_FOX = '#32775', HOC_FOX_ID = 32775;
  const HOC_FOX_CAVE = 136498, HOC_FOX_CAVE_X = 3626, HOC_FOX_CAVE_Y = 1664;
  // After Fox (62091 = 2, then 3 once all are armed) three 3x3 trap spots appear west of the cave, one varbit each:
  // 0 = hidden, 1 = Unbuilt log trap (Build), 2 = Log trap (Set), 3 = Log trap (armed).
  const HOC_TRAPS = [{ vb: 62092, x: 3605, y: 1635 }, { vb: 62093, x: 3620, y: 1644 }, { vb: 62094, x: 3621, y: 1651 }];
  const HOC_GIANT_LOGS = 63951, HOC_ROPE = 63952;   // Fox hands over 3 of each
  // Inside Fox's cave (the hunters' camp under Hollow Hill); its mouth is loc 136500 "Cave", action Exit.
  const HOC_FOX_CAVE_EXIT = 136500, HOC_FOX_CAVE_EXIT_X = 3647, HOC_FOX_CAVE_EXIT_Y = 7981;
  function hocInFoxCave() {
    const P = qgP;
    return !!(P && (P.p | 0) === 0 && P.x >= 3570 && P.x <= 3720 && P.y >= 7960 && P.y <= 8080);
  }
  async function hocInvTotal(ids) { let n = 0; for (const id of ids) n += await qgInvCount(id); return n; }
  // The nearest few matching objects, marked with one label each.
  function hocNearestMarks(objs, ids, label, max) {
    const P = qgP, d = o => P ? Math.max(Math.abs(o.x - P.x), Math.abs(o.y - P.y)) : 0;
    return qgIdMarks(objs, ids, label).sort((a, b) => d(a) - d(b)).slice(0, max || 3);
  }
  async function hocQuarry() {
    if ((await hocInvTotal(HOC_SUPER_ANTISANG)) > 0) { qgClearAll(); return; }   // Esther's hand-over is still to be mapped
    const sc = await qgScene();
    const exalted = await hocInvTotal(HOC_EXALTED_LUMPS);
    hocExaltedLeft = exalted;
    if (exalted === 1 && (await qgInvCount(HOC_COLOSSAL_LEG)) < 1) {
      const marks = hocNearestMarks(sc.objects, HOC_VEINS, 'Essence\nMine until you get a colossal leg', 3);
      qgClrNpc(); qgClrDlg(); qgClrItem();
      if (marks.length) qgOv('overlay.guideTiles', marks);
      else qgTile(HOC_AZUR_X, HOC_AZUR_Y, 0, 'Mine essence veins until you get a colossal leg');
      return;
    }
    if (exalted === 1) {
      const kw = (await qgInvCount(HOC_KWUARM_UNF)) > 0 ? '' : ' (bring an unfinished Kwuarm potion)';
      if (!(await qgObjectById(HOC_BANK_BOOTH, 'Bank booth\nMix unf. Kwuarm + Exalted essence' + kw, sc.objects)))
        qgTile(HOC_BANK_X, HOC_BANK_Y, 0, 'Bank north of the quarry: mix an unfinished Kwuarm potion with Exalted essence' + kw + '. Keep the exalted lump for Esther');
      return;
    }
    if (exalted > 1) {
      if (!(await qgObjectById(HOC_PROCESSOR, 'Essence processor\nProcess all but one exalted lump', sc.objects)))
        qgObject('Essence processor', 'Process all but one exalted lump', HOC_PROCESSOR_X, HOC_PROCESSOR_Y, 0);
      return;
    }
    if ((await hocInvTotal(HOC_LUMPS)) > 0) {
      const marks = [...qgIdMarks(sc.objects, HOC_QUARRY_BOX, 'Deposit box\nDeposit 1 essence lump'),
                     ...qgIdMarks(sc.objects, HOC_COLOSSUS, 'Exalted colossus\nMine')];
      qgClrNpc(); qgClrDlg(); qgClrItem();
      if (marks.length) qgOv('overlay.guideTiles', marks);
      else qgObject('Exalted colossus', 'Deposit 1 lump in the deposit box, then mine the colossus', HOC_COLOSSUS_X, HOC_COLOSSUS_Y, 0);
      return;
    }
    const marks = hocNearestMarks(sc.objects, HOC_VEINS, 'Essence\nMine until you get lumps', 3);
    qgClrNpc(); qgClrDlg(); qgClrItem();
    if (marks.length) qgOv('overlay.guideTiles', marks);
    else qgTile(HOC_AZUR_X, HOC_AZUR_Y, 0, 'Mine essence veins in the Exalted Quarry');
  }
  function hocEastOfBarricade() { const P = qgP; return !!(P && (P.p | 0) === 0 && P.x >= 3764 && P.y < 6400); }
  // Steps with no var of their own tick from where you have been, latched until the stage changes.
  let hocExaltedLeft = -1;   // exalted lumps held at the last step, for the ticks
  let hocCrossed = false, hocAtQuarry = false, hocExaltedSeen = false, hocLegSeen = false, hocKeptOne = false, hocAtTraps = false;
  function hocInDen() { const P = qgP; return !!(P && (P.p | 0) === 0 && P.y >= 7915 && P.y <= 7960 && Math.abs(P.x - 3410) <= 30); }
  // Into the den with the potion: the entry prompt's "Yes.", Silverquill once inside, else the ventricle in.
  async function hocSilverquillDen() {
    let boxed = false; try { boxed = await PLUGIN_API['overlay.highlightOption'].run(['yes'], myPid()); } catch (e) {}
    if (boxed) { qgClrNpc(); qgClrTiles(); qgClrItem(); return; }
    if (hocInDen()) { await qgNpc(HOC_SQ_ASLEEP, 'Pour the potion onto Silverquill'); return; }
    const sc = await qgScene();
    if (!(await qgObjectById(HOC_VENTRICLE, 'Cave ventricle\nEnter', sc.objects)))
      qgObject('Cave ventricle', 'Enter', HOC_VENTRICLE_X, HOC_VENTRICLE_Y, 0);
  }
  const HOC_SORREL_X = 3732, HOC_SORREL_Y = 1573;   // by the Amberfell well
  const HOC_INANNA = '#32826', HOC_SHRINE_X = 3540, HOC_SHRINE_Y = 1423;   // Inanna at her shrine, where the Ghrazi events start
  // Varp 13690: the seven Ghrazi events, which come in any order. Nothing in the cache morphs on these,
  // so they only track the events.
  //   62105 (bits 0-2): the event under way, 0 between events. Inanna's "Follow my howls!" sets it,
  //                     talking to her back at the shrine clears it.
  //   62106 (bits 3-5): where you are: 1 at the shrine, then where Anya sent you (2 the farms, 3 Hollow Hill, 4 Wendlewick).
  //   HOC_EVENT_STATE:  event n's own 2-bit state: 1 = under way, 2 = further in (Raz: first talk with
  //                     Raz done, kept through the way back), 3 = closed with Inanna at the shrine.
  const HOC_EVENT = 62105, HOC_EVENT_STAGE = 62106;
  // each event's own state var: 1 -> 62107 (bits 6-7), 2..7 -> 62109..62114; 62108 (bits 8-9) not seen used
  const HOC_EVENT_STATE = { 1: 62107, 2: 62109, 3: 62110, 4: 62111, 5: 62112, 6: 62113, 7: 62114 };
  const hocEvState = (vb, n) => (vb[HOC_EVENT_STATE[n]] | 0);
  const HOC_EVENT_VBS = [62107, 62108, 62109, 62110, 62111, 62112, 62113, 62114];
  // Event number -> the guide steps of its section: the howl, Anya's teleport, ..., back to the shrine.
  // Filled in as each event is seen. 62106 on arrival: 2 at the farms, 3 at Hollow Hill, 4 at Wendlewick.
  const HOC_EVENTS = {
    3: { name: "Raz's fight", steps: [27, 28, 29, 30, 31] },
    1: { name: 'The farm fight', steps: [57, 58, 59, 60] },                         // the last section left: Ben, any spell
    2: { name: "Silverquill's fight", steps: [32, 33, 34, 35, 36] },               // the farms, Silverquill to calm
    4: { name: 'The trap fight in Hollow hill', steps: [37, 38, 39, 40, 41] },    // Fox's log traps
    5: { name: 'The second fight in Wendlewick', steps: [47, 48, 49, 50, 51] },   // 4 Sanguine Omegas
    6: { name: 'The third fight in Wendlewick', steps: [52, 53, 54, 55, 56] },    // the lighthouse barricade (crawlers, no omegas)
    7: { name: 'The fight in Wendlewick', steps: [42, 43, 44, 45, 46] },          // 3 Sanguine Omegas, Inanna east of the lodestone
  };
  const HOC_RAZ_STATE = HOC_EVENT_STATE[3];   // 62110
  // Raz's fight instance (62105 = 3): Ivar, King of Bones, Inanna and Raz as they appear there.
  const HOC_IVAR = '#32868', HOC_EVENT_INANNA = '#32863', HOC_RAZ = '#32870';
  const HOC_IVAR_ID = 32868, HOC_EVENT_INANNA_ID = 32863;
  const HOC_ASH = '#21050', HOC_ASH_X = 3760, HOC_ASH_Y = 1563;   // at the south-east barricade (a later Ash is 32845)
  const HOC_ADAM = '#20510', HOC_ADAM_X = 3482, HOC_ADAM_Y = 1573;   // in Wendlewick
  const HOC_TAVERN_X = 3499, HOC_TAVERN_Y = 1504;   // Adam, Esther and Zeke together there while 62090 is 105..120
  const HOC_PARASOL = 141032, HOC_PARASOL_X = 3542, HOC_PARASOL_Y = 1425;   // Anya's parasol at the shrine
  // Moonsylvar Wood's entrance: loc 140944 "Moonsylvar Wood" (Enter, Pass through); the wood itself is at 3732,1454.
  const HOC_MOONSYLVAR = 140944, HOC_MOONSYLVAR_X = 3802, HOC_MOONSYLVAR_Y = 1512;
  // Tracking: which entry of this chain is under way comes from the clue vars and 62117 (hocTrackTarget). The later clues' own vars (62101..62104, varp 13689 bits 28-31) turn their
  // objects, and a couple of decoys beside them, to Track/Inspect. Placement ids are what the scene
  // reports; alt = the id each morphs to. A clue's var turns on when Ash reaches it ("The scent leads to
  // this sheep farm." set 62101, with 62119 also going to 1); until then it has no Track option.
  // The order after the planter follows the quick guide, unconfirmed.
  const HOC_TRACK = 62117;
  // 62117 is trail progress, not a count of clues: 1 once Ash sniffs the parasol, 4 once the planter is
  // tracked, 0 again on reaching the tent, 7 once the chest is tracked. Each later clue's own var turns it
  // to Track: the planter's and the chest's when Ash reaches them (with "reached" flags 62119 at the farm and
  // 62120 at the tent), the Deserted Mine bloodsplatter's (62104) straight from tracking the chest, which
  // skips the one by the tent (62103, left out of the chain). So the clue under way is the latest whose var is on (the parasol
  // before any), done once 62117 reaches its doneAt (8 for the bloodsplatter, seen after tracking it); a done clue
  // points on to the next one.
  function hocTrackTarget(vb) {
    let k = 0;
    for (let i = HOC_TRACK_CHAIN.length - 1; i >= 1; i--) if ((vb[HOC_TRACK_CHAIN[i].vb] | 0) >= 1) { k = i; break; }
    const t = vb[HOC_TRACK] | 0, done = t >= HOC_TRACK_CHAIN[k].doneAt;
    return { k, done, next: done ? k + 1 : k };   // next = the chain entry to show
  }
  const HOC_TRACK_CHAIN = [
    { doneAt: 1, id: 141032, alt: [141034], x: 3542, y: 1425, name: "Anya's parasol", act: 'Track', where: 'at the shrine' },
    { vb: 62101, doneAt: 4, id: 141035, alt: [141037], x: 3615, y: 1433, name: 'Flower planter', act: 'Track', where: 'next to the building at Eastfold Farm' },
    { vb: 62102, doneAt: 7, id: 141042, alt: [141043], x: 3599, y: 1386, name: "Anya's chest", act: 'Track', where: "in Anya's tent" },
    { vb: 62104, doneAt: 8, id: 141049, alt: [141050], x: 3684, y: 1378, name: 'Bloodsplatter', act: 'Track', where: 'north-east of the Deserted Mine' },
  ];
  const HOC_ZEKE = '#26619', HOC_ZEKE_X = 3496, HOC_ZEKE_Y = 1565;   // the next building east of Adam
  const HOC_LIAT = '#32540', HOC_LIAT_X = 3507, HOC_LIAT_Y = 1538;   // at the smithy, may wander outside
  // Esther at the top of the Wendlewick lighthouse (plane 1); its stairs are on the ground floor.
  async function hocEsther(label) {
    const P = qgP;
    const near = (x, y, p, r) => !!(P && (P.p | 0) === p && Math.abs(P.x - x) <= r && Math.abs(P.y - y) <= r);
    if (near(3454, 1495, 1, 20)) { await hocTalkNpc('Esther', label, 3457, 1500, 1); return; }
    if (near(3449, 1495, 0, 52)) { qgObject('Stairs', 'Climb the lighthouse stairs', 3449, 1495, 0); return; }
    qgTile(3452, 1494, 0, 'Go to the Wendlewick lighthouse, then climb the stairs');
  }
  // The barricade Ash sends you over: loc 141015 "Barricade", action "Climb over".
  const HOC_BARRICADE = 141015, HOC_BARRICADE_X = 3762, HOC_BARRICADE_Y = 1559;
  // The Sanguine Sigma on the hill above Ash: npc 32851 (the quest's morph of 32850 at 35..40).
  const HOC_SIGMA = '#32851', HOC_SIGMA_X = 3760, HOC_SIGMA_Y = 1548;
  // Ash's "Any objections?" chooser at 15: every option works, so one is picked at random.
  const HOC_ASH_OPTS = ['like a date', 'i could use your help', 'i do need that potion', "i'd prefer not"];
  let hocAshPick = -1;

  // Raz's fight (event 3), inside its Hollow Hill instance.
  async function hocRazFight(vbm) {
    // Raz talked to again after her fight (no var: Anya closes it with "Indeed. We should leave."):
    // Inanna takes you back
    if ((vbm[HOC_RAZ_STATE] | 0) >= 3 || ((vbm[HOC_RAZ_STATE] | 0) === 2 && ((await hocLineSeen('razLeave')) || hocManualTick(30)))) {
      await hocTalkNpc(HOC_EVENT_INANNA, 'Talk to Inanna: she teleports you back to the shrine', 0, 0, 0);
      return;
    }
    if ((vbm[HOC_RAZ_STATE] | 0) === 2) {   // first talk with Raz done: watch her fight, then Raz again
      await hocTalkNpc(HOC_RAZ, "Watch Raz's fight, then talk to Raz again", 0, 0, 0);
      return;
    }
    // Ivar moves no var: his "We got it for now." marks him done, or having walked on from him (the
    // teleport lands beside Ivar, so being nearer Inanna than Ivar means you have moved on)
    if (!hocSaid.ivar) {
      try {
        const sc = await qgScene(), P = qgP;
        const d = n => Math.max(Math.abs(n.x - P.x), Math.abs(n.y - P.y));
        const ivar = (sc.npcs || []).find(n => n && n.id === HOC_IVAR_ID), inn = (sc.npcs || []).find(n => n && n.id === HOC_EVENT_INANNA_ID);
        if (P && inn && (!ivar || d(inn) < d(ivar))) hocSaid.ivar = true;
      } catch (e) {}
    }
    if ((await hocLineSeen('ivar')) || hocManualTick(29)) {
      if (await hocLineSeen('razAttack')) {   // Inanna: "Raz is under attack!"
        await hocTalkNpc(HOC_RAZ, "Witness Raz's fight, then talk to Raz", 0, 0, 0);
        return;
      }
      await hocTalkNpc(HOC_EVENT_INANNA, "Talk to Inanna near Raz's cabin", 0, 0, 0);
      return;
    }
    await hocTalkNpc(HOC_IVAR, 'Talk to Ivar, King of Bones', 0, 0, 0);
    return;
  }
  // A Wendlewick omega fight (event 5: four, event 7: three): Inanna, then the omegas, then Inanna takes you
  // back. Inanna's "Aid the militia with these sanguine werewolves!" starts the fight (or, missing that
  // line, an omega having lost life points); the nearest one still standing is boxed, with how many are left.
  const HOC_OMEGA_EVENTS = [5, 7];   // the two omega fights in Wendlewick
  let hocOmegaFought = false, hocOmegasCleared = false, hocOmegasSeen = false;
  async function hocOmegaFight(st) {
    const sc = await qgScene(), P = qgP;
    if (st >= 2) hocOmegasCleared = true;   // the event's own state: 2 once the omegas are down
    const omegas = (sc.npcs || []).filter(n => n && /sanguine omega/i.test(n.name || '') && (n.lp | 0) > 0);
    if (await hocLineSeen('militia')) hocOmegaFought = true;
    if (omegas.some(n => (n.lpMax | 0) > 0 && (n.lp | 0) < (n.lpMax | 0))) hocOmegaFought = true;
    if (omegas.length) hocOmegasSeen = true;
    if (hocOmegaFought && hocOmegasSeen && !omegas.length) hocOmegasCleared = true;   // only once some were seen (event 7's come later)
    if (hocOmegasCleared) { await hocTalkNpc(HOC_EVENT_INANNA, 'Talk to Inanna: she teleports you back to the shrine', 0, 0, 0); return; }
    if (hocOmegaFought && !omegas.length) { qgClearAll(); return; }   // started, the omegas not in view yet
    if (hocOmegaFought) {
      const d = n => P ? Math.max(Math.abs(n.x - P.x), Math.abs(n.y - P.y)) : 0;
      const t = omegas.reduce((a, b) => (d(b) < d(a) ? b : a));
      qgClrDlg(); qgClrItem(); qgClrTiles();
      qgOv('overlay.highlightNpc', '#' + t.id, 'Defeat the Sanguine Omegas (' + omegas.length + ' left)', t.x, t.y);
      return;
    }
    await hocTalkNpc(HOC_EVENT_INANNA, 'Talk to Inanna east of the lodestone', 0, 0, 0);
  }
  // The trap fight in Hollow Hill (event 4, 62106 = 3 like Raz's): Inanna, then the log traps built for
  // Fox are triggered once werewolves stand on them, then Inanna takes you back (the event's state,
  // 62111, goes to 2 once all three traps have fired). The armed traps are the ones offering Trigger
  // (in the instance: 141017, 141018, 141019 as "Log trap (armed)").
  // Where each trap's werewolf has to stand before the trap is triggered, as an offset from the trap's
  // tile (instance coordinates change per visit; the traps keep their ids). Seen in one visit, with
  // Inanna at 9840,1700: 141017 at 9811,1698 <- omega at 9817,1697; 141018 at 9826,1707 <- 9832,1706;
  // 141019 at 9827,1713 <- 9834,1717.
  const HOC_TRAP_SPOTS = { 141017: [6, -1], 141018: [6, -1], 141019: [7, 4] };
  const HOC_READY_RGB = 0x33DD55, HOC_WAIT_RGB = 0xE05545;
  // Centre-of-screen prompt (text slot 0), cleared as soon as no trap is ready or the fight is left.
  let hocCenterOn = true;   // starts true: a reload forgets a prompt still on screen, so the first clear must go through
  function hocCenter(text) {
    if (!text && !hocCenterOn) return;
    hocCenterOn = !!text;
    try { bridge().centerText(myPid(), text || '', 0, text ? HOC_READY_RGB : -1); } catch (e) {}
  }
  let hocTrapFightOn = false;
  async function hocTrapFight(st, vbm) {
    // a triggered trap's own var (62092..62094) drops from 3 to 0; all three at 0 = the traps are done
    if (HOC_TRAPS.every(t => ((vbm && vbm[t.vb]) | 0) === 0)) st = Math.max(st, 2);
    if (st >= 2) { hocCenter(''); await hocTalkNpc(HOC_EVENT_INANNA, 'Talk to Inanna: she teleports you back to the shrine', 0, 0, 0); return; }
    const sc = await qgScene();
    const armed = (sc.objects || []).filter(o => o && /log trap/i.test(o.name || '') && (o.actions || []).some(a => /trigger/i.test(a)));
    // the werewolves (Sanguine Omegas, 32852 here) stand idle with no actions and 1 life point until
    // Inanna starts it; one that fights (an action, or real life points) means the traps are next
    const wolves = (sc.npcs || []).some(n => n && /omega|werewolf/i.test(n.name || '') && ((n.actions || []).length > 0 || (n.lp | 0) > 1));
    if (await hocLineSeen('trapsGo')) hocTrapFightOn = true;
    if (armed.length && (wolves || hocManualTick(39))) hocTrapFightOn = true;
    if (hocTrapFightOn && armed.length) {
      // each trap is ready once a werewolf stands on (or beside) its spot
      const omegas = (sc.npcs || []).filter(n => n && /omega|werewolf/i.test(n.name || ''));
      const ready = o => {
        const off = HOC_TRAP_SPOTS[o.id];
        return !!off && omegas.some(n => Math.max(Math.abs(n.x - (o.x + off[0])), Math.abs(n.y - (o.y + off[1]))) <= 1);
      };
      // green TRIGGER NOW on a ready trap, red Wait on the rest, and a centre prompt while any is ready
      qgClrNpc(); qgClrDlg(); qgClrItem();
      qgOv('overlay.guideTiles', armed.map(o => ({ x: o.x + ((o.w | 0) > 1 ? 1 : 0), y: o.y + ((o.h | 0) > 1 ? 1 : 0), plane: o.plane | 0,
        color: ready(o) ? HOC_READY_RGB : HOC_WAIT_RGB,
        label: ready(o) ? 'Log trap (armed)\nTRIGGER NOW' : 'Log trap (armed)\nWait: werewolf not in place' })));
      hocCenter(armed.some(ready) ? 'TRIGGER THE GREEN LOG TRAP NOW' : '');
      return;
    }
    hocCenter('');
    await hocTalkNpc(HOC_EVENT_INANNA, 'Talk to Inanna', 0, 0, 0);
  }
  // Silverquill's fight (event 2, at the farms: 62106 = 2): calm Silverquill, the Dreadhog (32867, "Calm")
  // far east of the farm by the Blighted cave, watch her fight, then Inanna takes you back. The event's
  // state (62109) goes to 2 once her fight has ended; until then Silverquill is the target.
  const HOC_SQ_FIGHT = '#32867';
  async function hocSilverquillFight(st) {
    if (st >= 2) { await hocTalkNpc(HOC_EVENT_INANNA, 'Talk to Inanna: she teleports you back to the shrine', 0, 0, 0); return; }
    await hocTalkNpc(HOC_SQ_FIGHT, 'Calm Silverquill, the Dreadhog (far east, by the Blighted cave)', 0, 0, 0);
  }
  // The third Wendlewick fight (event 6, 62106 = 4): Inanna next to the lighthouse (Esther beside her,
  // Sanguine crawlers about), then the lighthouse barricade is repaired, then Inanna takes you back.
  // The lighthouse barricade shows as "Barricade (broken)" (141024) with Repair; the finished ones along
  // the town are 141023 with no action.
  let hocBarricadeSeen = false, hocInannaSpoke = false;
  async function hocLighthouseFight(st) {
    if (st >= 2) { await hocTalkNpc(HOC_EVENT_INANNA, 'Talk to Inanna: she teleports you back to the shrine', 0, 0, 0); return; }
    const sc = await qgScene();
    const fix = (sc.objects || []).filter(o => o && /barricade/i.test(o.name || '') && (o.actions || []).some(a => /repair|build|fix/i.test(a)));
    // The broken barricade (141024, Repair) stands by the lighthouse from the start, so it waits until
    // Inanna has been spoken with here (her name in the chat head), you stand at it, or step 54 is ticked.
    if (!hocInannaSpoke) {
      try {
        const d = JSON.parse(await bridge().interfaceComps(myPid(), 1184, '4,10') || '{}');
        if (d && Array.isArray(d.comps) && d.comps.some(c => c && c.comp === 4 && /inanna/i.test(String(c.text || '')))) hocInannaSpoke = true;
      } catch (e) {}
    }
    const P = qgP, at = o => P && Math.max(Math.abs(P.x - o.x), Math.abs(P.y - o.y)) <= 3;
    if (fix.length && (hocInannaSpoke || hocManualTick(54) || fix.some(at))) {
      hocBarricadeSeen = true;
      qgClrNpc(); qgClrDlg(); qgClrItem();
      qgOv('overlay.guideTiles', fix.map(o => ({ x: o.x, y: o.y, plane: o.plane | 0, label: o.name + '\nRepair the lighthouse barricade' })));
      return;
    }
    await hocTalkNpc(HOC_EVENT_INANNA, 'Talk to Inanna next to the lighthouse', 0, 0, 0);
  }
  // The farm fight (event 1, by elimination: the one section left once the other six were seen):
  // Anya says near the farm, Ben north-east of the farm is talked to (any spell), then Inanna takes you back.
  // In the farm instance Ben is 32869, with Onyx the Black (32873) and a Sanguine crawler beside him. He is
  // talked to twice before the event's state (62107) reaches 2, so any Ben in view is boxed until then.
  const HOC_BEN = '#32869';
  async function hocFarmFight(st) {
    if (st >= 2) { await hocTalkNpc(HOC_EVENT_INANNA, 'Talk to Inanna: she teleports you back to the shrine', 0, 0, 0); return; }
    const label = 'Talk to Ben (twice) north-east of the farm, choose any spell';
    if (!(await hocNearestNamed('Ben', label))) await hocTalkNpc(HOC_BEN, label, 0, 0, 0);
  }
  // Adam, Esther or Zeke in the tavern, whichever is nearest in view; else the tavern tile.
  async function hocTavern(label) {
    let hit = null;
    try {
      const sc = await qgScene(), P = qgP;
      const d = n => P ? Math.max(Math.abs(n.x - P.x), Math.abs(n.y - P.y)) : 0;
      for (const n of sc.npcs || []) if (n && /^(adam|zeke|esther)$/i.test(n.name || '') && (!hit || d(n) < d(hit))) hit = n;
    } catch (e) {}
    if (hit) await hocTalkNpc('#' + hit.id, label, hit.x, hit.y, hit.plane | 0);
    else await hocTalkNpc(HOC_ADAM, label, HOC_TAVERN_X, HOC_TAVERN_Y, 0);
  }
  // Moonsylvar Wood (62090 = 140): the quick guide's route, one entry per move.
  const HOC_WOOD_ROUTE = ['S', 'Log', 'W', 'S', 'W', 'E', 'Bloodsplatter', 'S', 'S', 'E', 'E', 'Bear', 'MS', 'E', 'S', 'E', 'E'];
  const HOC_WOOD_DIRS = { S: [0, -1], N: [0, 1], E: [1, 0], W: [-1, 0], MS: [0, -1] };
  const HOC_WOOD_WORDS = { S: 'south', N: 'north', E: 'east', W: 'west', MS: 'middle south' };
  // Progress is kept as the guide's own ticks on the route's sub-points (71.1 .. 71.17), so it survives a
  // reload and can be corrected by hand: the moves done are the ticked ones in a row from the first.
  // A path followed shows as a jump of over 20 tiles (each clearing sits elsewhere in the instance).
  // 62118 (varp 13690 bits 28-29) counts the Track moves done (1 after the Log, 2 after the Bloodsplatter; it
  // read 0 for a moment on reaching the Bloodsplatter), so it also fixes the least the route has reached.
  const HOC_WOOD_TRACKS = 62118;
  const HOC_CRIMSON_GATE = 141053, HOC_CRIMSON_GATE_X = 4078, HOC_CRIMSON_GATE_Y = 1612;   // centre of its 5x5
  let hocAtShuruk = false, hocInArena = false;
  // The fight's opening chat: any answer to the first chooser (one picked at random), then later
  // "You're not taking Inanna (Continue Quest)".
  const HOC_GHRAZI_OPTS1 = ["you're better than this", 'did you ever care about us', 'i should have expected this'];
  // the second, "Propose another plan.": any of its five
  const HOC_GHRAZI_OPTS2 = ['take exalted essence instead of inanna', 'destroy the portal from this side', 'stay with us here and fight',
                            'i go to vampyrium instead', 'i stop you'];
  let hocGhraziPick1 = -1, hocGhraziPick2 = -1;
  const HOC_FINAL_OPTS = ['nor do i', "i'm not sure how i feel", 'well i do'];
  let hocFinalPick = -1;
  const HOC_WOOD_PARTS = { 140: 0, 145: 6, 150: 11 };   // 62090 -> the route move that part starts at
  let hocWoodLastP = null;
  const hocWoodIsPath = m => !!HOC_WOOD_DIRS[m];
  function hocWoodIndex() { let i = 0; while (i < HOC_WOOD_ROUTE.length && hocManualTick('71.' + (i + 1))) i++; return i; }
  function hocWoodMark(i) { if (i < HOC_WOOD_ROUTE.length && !hocManualTick('71.' + (i + 1))) { try { qgToggleStep('Heralds of Crimson', '71.' + (i + 1)); } catch (e) {} } }
  // What each Track move is called in the wood: the Log and the Bloodsplatter are objects, the Bear is the
  // NPC "Dead bear" (32886, Track).
  const HOC_WOOD_TARGETS = { Log: /^log$/i, Bloodsplatter: /^bloodsplatter$/i, Bear: /^dead bear$/i };
  function hocWoodTarget(sc, move) {
    const re = HOC_WOOD_TARGETS[move] || new RegExp('^' + move + '$', 'i');
    const tracks = x => (x.actions || []).some(a => /track/i.test(a));
    const o = (sc.objects || []).find(x => x && re.test(x.name || '') && tracks(x));
    if (o) return Object.assign({ kind: 'obj' }, o);
    const n = (sc.npcs || []).find(x => x && re.test(x.name || '') && tracks(x));
    return n ? Object.assign({ kind: 'npc' }, n) : null;
  }
  async function hocWoodStep(t, start) {
    const sc = await qgScene(), P = qgP, N = HOC_WOOD_ROUTE.length;
    // every move before this part is behind you; and t = the Track moves done in the whole route (it read 0
    // on reaching the Bloodsplatter, then 2 once it was tracked), so everything up to the t-th Track too
    for (let j = 0; j < start; j++) hocWoodMark(j);
    const trackAt = HOC_WOOD_ROUTE.map((m, j) => hocWoodIsPath(m) ? -1 : j).filter(j => j >= 0);
    if (t >= 1) for (let j = 0; j <= trackAt[Math.min(t, trackAt.length) - 1]; j++) hocWoodMark(j);
    let i = hocWoodIndex();
    const jumped = !!(P && hocWoodLastP && Math.max(Math.abs(P.x - hocWoodLastP.x), Math.abs(P.y - hocWoodLastP.y)) > 20);
    if (P) hocWoodLastP = { x: P.x, y: P.y };
    if (i < N && hocWoodIsPath(HOC_WOOD_ROUTE[i]) && jumped) { hocWoodMark(i); i++; }
    // the next Track's target already in view while a path is still listed: that path is behind you
    if (i + 1 < N && hocWoodIsPath(HOC_WOOD_ROUTE[i]) && !hocWoodIsPath(HOC_WOOD_ROUTE[i + 1]) && hocWoodTarget(sc, HOC_WOOD_ROUTE[i + 1])) { hocWoodMark(i); i++; }
    const move = HOC_WOOD_ROUTE[i];
    if (!move) { qgClearAll(); return; }   // past the route
    if (await hocOption(['yes'])) return;
    const lbl = (i + 1) + '/' + HOC_WOOD_ROUTE.length + ': ';
    const dir = HOC_WOOD_DIRS[move];
    if (!dir) {   // a Track: the object (or NPC) of that name
      const o = hocWoodTarget(sc, move);
      if (o && o.kind === 'obj') { qgClrNpc(); qgClrDlg(); qgClrItem(); qgOv('overlay.guideTiles', [{ x: o.x, y: o.y, plane: o.plane | 0, label: o.name + '\n' + lbl + 'Track' }]); return; }
      if (o && o.kind === 'npc') { await hocTalkNpc('#' + o.id, lbl + 'Track the ' + o.name.toLowerCase(), o.x, o.y, o.plane | 0); return; }
      qgClearAll();
      return;
    }
    // a Path: the one lying furthest that way from the clearing's centre (the mean of its paths). Measured from
    // the centre, not from you: you may stand at an edge, and a move can go back out the way you came in.
    const paths = (sc.objects || []).filter(o => o && /^path$/i.test(o.name || '') && P && Math.max(Math.abs(o.x - P.x), Math.abs(o.y - P.y)) <= 30);
    const C = paths.length ? { x: paths.reduce((a, o) => a + o.x, 0) / paths.length, y: paths.reduce((a, o) => a + o.y, 0) / paths.length } : P;
    const score = o => { const dx = o.x - C.x, dy = o.y - C.y, len = Math.hypot(dx, dy) || 1; return (dx * dir[0] + dy * dir[1]) / len; };
    let pick = null;
    if (move === 'MS') {   // several paths lead south: the middle one
      const south = paths.filter(o => score(o) > 0.5).sort((a, b) => a.x - b.x);
      pick = south.length ? south[(south.length - 1) >> 1] : null;
    } else {
      for (const o of paths) if (score(o) > 0.5 && (!pick || score(o) > score(pick))) pick = o;
    }
    if (!pick) { qgClearAll(); return; }
    qgClrNpc(); qgClrDlg(); qgClrItem();
    qgOv('overlay.guideTiles', [{ x: pick.x, y: pick.y, plane: pick.plane | 0, label: 'Path\n' + lbl + 'Follow the ' + HOC_WOOD_WORDS[move] + ' path' }]);
  }
  // The fight (62090 from 180, when Vas and Mina arrive): Lady Karmina Ghrazi (32892) and Lord Vasily Ghrazi
  // (32893) once they are there to attack, Sanguine tumours (32894) to keep away from. At 300k life points
  // Vasily jumps onto a platform, and Karmina is the one to hit while the red tiles are dodged. (The scene
  // read both at a full 1,000,000 all fight, even after they were beaten, so the 300k switch may never fire.)
  let hocVasilyLow = false, hocFightSeen = false;
  async function hocGhraziFight() {
    const sc = await qgScene(), P = qgP;
    const d = n => P ? Math.max(Math.abs(n.x - P.x), Math.abs(n.y - P.y)) : 0;
    const npcs = (sc.npcs || []).filter(n => n && (n.lp | 0) > 0);
    const attackable = n => (n.actions || []).some(a => /attack/i.test(a));
    const kar = npcs.find(n => /karmina/i.test(n.name || '') && attackable(n));
    const vas = npcs.find(n => /vasily/i.test(n.name || '') && attackable(n));
    if (kar || vas) hocFightSeen = true;
    if (vas && (vas.lp | 0) <= 300000) hocVasilyLow = true;
    // about fifteen tumours stand around the arena: plain red tiles, the boss label carries the words
    const tumours = npcs.filter(n => /sanguine tumour/i.test(n.name || ''))
      .map(n => ({ x: n.x, y: n.y, plane: n.plane | 0, color: HOC_WAIT_RGB, label: '' }));
    let target = null, label = '';
    if (hocVasilyLow && kar) { target = kar; label = 'Attack Lady Karmina: keep off the red tiles'; }
    else {
      const pool = [kar, vas].filter(Boolean);
      if (pool.length) { target = pool.reduce((a, b) => (d(b) < d(a) ? b : a)); label = 'Attack ' + target.name + ' (keep clear of the red tumours)'; }
    }
    // the green circle when they summon from the Sanguine heart: a world graphic, taken to be gfx 5893 (with
    // 5894 on the same tile; seen in the fight, unconfirmed); stand in it
    let circle = null;
    try {
      const raw = JSON.parse((await PLUGIN_API['state.scene'].run([40], myPid())) || '{}');
      circle = (raw.effects || []).find(e => e && HOC_GREEN_CIRCLE_GFX.includes(e.gfx)) || null;
    } catch (e) {}
    const marks = circle ? [{ x: circle.x, y: circle.y, plane: 0, color: HOC_READY_RGB, label: 'Green circle\nStand in it' }, ...tumours] : tumours;
    hocCenter(circle ? 'STAND IN THE GREEN CIRCLE' : '');
    if (!target && !marks.length) { qgClearAll(); return; }   // the talk and cutscene before the fight
    qgClrDlg(); qgClrItem();
    if (target) qgOv('overlay.highlightNpc', '#' + target.id, label, target.x, target.y); else qgClrNpc();
    if (marks.length) qgOv('overlay.guideTiles', marks); else qgClrTiles();
  }
  const HOC_GREEN_CIRCLE_GFX = [5893, 5894];
  // Box one of these options when an option menu is open; true when one was boxed.
  async function hocOption(opts) {
    let boxed = false; try { boxed = await PLUGIN_API['overlay.highlightOption'].run(opts, myPid()); } catch (e) {}
    if (boxed) { qgClrNpc(); qgClrTiles(); qgClrItem(); }
    return !!boxed;
  }
  // Talks that move no var are marked done by an NPC chat line (group 1184, comp 10), latched once seen:
  //   zeke: "I guess I'll overcome my fear and pay her a visit..." (on the way back to Adam)
  //   ivar: "We got it for now." (Raz's fight, Hollow Hill)
  //   razAttack: Inanna's "Raz is under attack!" (Raz's fight, after Ivar)
  //   razLeave: Anya's "Indeed. We should leave." (ends the second talk with Raz)
  //   militia: Inanna starting a Wendlewick omega fight: "Aid the militia with these sanguine werewolves!"
  //            (event 5), "Put those sanguine werewolves to rest before they breach the smithy!" (event 7)
  //   trapsGo: Inanna's "We will push these vyres back. Head to the ridge and trigger the traps!" (event 4)
  //   sorrelDone: your own "Okay, thank you. I'll head to the barrier." (ends Sorrel's talk at 120)
  const HOC_LINES = { zeke: /overcome my fear/i, ivar: /we got it for now/i, razAttack: /raz is under attack/i,
                      razLeave: /we should leave/i, militia: /aid the militia|put those sanguine werewolves to rest/i, trapsGo: /trigger the traps/i,
                      sorrelDone: /head to the barrier/i };
  const hocSaid = {};
  // NPC lines sit in group 1184 comp 10; your own lines in group 1191, whose text comp is found by scanning.
  const HOC_PLAYER_COMPS = Array.from({ length: 24 }, (_, i) => i).join(',');
  async function hocLineSeen(key) {
    if (hocSaid[key]) return true;
    for (const [g, comps] of [[1184, '10'], [1191, HOC_PLAYER_COMPS]]) {
      try {
        const d = JSON.parse(await bridge().interfaceComps(myPid(), g, comps) || '{}');
        if (d && Array.isArray(d.comps) && d.comps.some(c => c && HOC_LINES[key].test(String(c.text || '')))) { hocSaid[key] = true; break; }
      } catch (e) {}
    }
    return !!hocSaid[key];
  }
  // A step ticked by hand in the guide, so a latch lost to a restart can be put back with one click.
  function hocManualTick(i) {
    try { const m = questGSteps && questGSteps['Heralds of Crimson']; return Array.isArray(m) && m.indexOf(i) >= 0; } catch (e) { return false; }
  }
  // Inanna changes ids: 32826 at her shrine, 32863 once the Ghrazi assault moves you into its instance.
  // Box whichever Inanna is in view (the nearest, on her own tile); otherwise mark her shrine spot.
  async function hocInanna(label, ...opts) {
    if (await hocNearestNamed('Inanna', label, ...opts)) return;
    return hocTalkNpc(HOC_INANNA, label, HOC_SHRINE_X, HOC_SHRINE_Y, 0, ...opts);
  }
  // Box the nearest NPC in view with this name, whatever id it has at the moment; false if none is in view.
  async function hocNearestNamed(name, label, ...opts) {
    let hit = null;
    try {
      const sc = await qgScene(), P = qgP;
      const d = n => P ? Math.max(Math.abs(n.x - P.x), Math.abs(n.y - P.y)) : 0;
      for (const n of sc.npcs || []) if (n && n.name === name && (!hit || d(n) < d(hit))) hit = n;
    } catch (e) {}
    if (!hit) return false;
    await hocTalkNpc('#' + hit.id, label, hit.x, hit.y, hit.plane | 0, ...opts);
    return true;
  }
  async function hocTalkNpc(name, label, tx, ty, tp, ...extra) {
    return qgDialogNpc(name, label, tx, ty, tp, 'heralds of crimson', ...extra);
  }
  async function hocBarricade() {
    const sc = await qgScene();
    if (!(await qgObjectById(HOC_BARRICADE, 'Barricade\nClimb over', sc.objects)))
      qgObject('Barricade', 'Climb over', HOC_BARRICADE_X, HOC_BARRICADE_Y, 0);
  }
  async function hocStep() {
    let vbm = {}; try { vbm = await readVarbitValues([HOC_PROG, HOC_PREP, HOC_PREP_COUNT, HOC_SILVERQUILL, HOC_ANYA_TRUST, HOC_ANYA_FORGIVE, HOC_CURE, HOC_QUARRY, ...HOC_TRAPS.map(t => t.vb), HOC_EVENT, HOC_EVENT_STAGE, ...HOC_EVENT_VBS, HOC_TRACK, ...HOC_TRACK_CHAIN.filter(c => c.vb).map(c => c.vb), HOC_WOOD_TRACKS]); } catch (e) { return; }
    const v = vbm[HOC_PROG] | 0, prep = vbm[HOC_PREP] | 0, talked = vbm[HOC_PREP_COUNT] | 0, silverquill = vbm[HOC_SILVERQUILL] | 0;
    // the centre prompt belongs to the trap fight and the Ghrazi fight only (each sets and clears its own)
    if (!(v === 85 && (vbm[HOC_EVENT] | 0) === 4 && (vbm[HOC_EVENT_STAGE] | 0) >= 2) && !(v >= 180 && v < HOC_DONE)) hocCenter('');
    if (v === 0) { await hocTalkNpc('Sorrel', 'Talk to Sorrel in Amberfell (fairy ring DLP)', HOC_SORREL_X, HOC_SORREL_Y, 0, 'accept', 'yes'); return; }
    if (v === 5) { await hocTalkNpc('Sorrel', 'Continue the conversation with Sorrel', HOC_SORREL_X, HOC_SORREL_Y, 0, 'accept', 'yes'); return; }
    if (v === 10) { await hocTalkNpc(HOC_ASH, 'Talk to Ash at the south-east barricade', HOC_ASH_X, HOC_ASH_Y, 0); return; }
    if (v === 15 || v === 20) {
      if (hocAshPick < 0) hocAshPick = qgRand(HOC_ASH_OPTS.length);
      await hocTalkNpc(HOC_ASH, 'Continue the conversation with Ash', HOC_ASH_X, HOC_ASH_Y, 0, HOC_ASH_OPTS[hocAshPick]);
      return;
    }
    if (v === 25 || v === 30) { await hocBarricade(); return; }   // 30 = the dialogue the barricade starts
    // 35 = the Sigma is up, 40 = "Are you ready to fight?" (Yes.) on attacking it
    if (v === 35 || v === 40) { await qgDialogNpc(HOC_SIGMA, 'Attack the Sanguine Sigma', HOC_SIGMA_X, HOC_SIGMA_Y, 0, 'yes'); return; }
    if (v === 45) { await hocTalkNpc(HOC_ASH, 'Go back to the barricade and talk to Ash', HOC_ASH_X, HOC_ASH_Y, 0); return; }
    if (v === 50) { await hocTalkNpc(HOC_ASH, 'Continue the conversation with Ash', HOC_ASH_X, HOC_ASH_Y, 0); return; }
    if (v === 55) { await hocTalkNpc(HOC_ADAM, 'Report to Adam in Wendlewick', HOC_ADAM_X, HOC_ADAM_Y, 0); return; }
    // 90: all seven Ghrazi events done and Inanna has taken you out (62106 = 0); she is still the one to
    // talk to. Tracking Inanna then starts at the tavern with Adam, Zeke or Esther (their quest copies
    // show as 20510, 26619 and 20511 while 62090 is 90..100).
    if (v === 90) { await hocInanna('Talk to Inanna'); return; }
    // 95 and 100: still that talk with Inanna
    if (v === 95 || v === 100) { await hocInanna('Continue the conversation with Inanna'); return; }
    // 105: the tavern (step 61). Adam, Esther and Zeke switch to another set of quest copies from 105 to
    // 120, standing together in the tavern (about 3499,1503; Adam 3499,1504, Esther 3500,1503, Zeke
    // 3499,1502), so whichever of them is nearest in view is boxed; else the tavern tile.
    if (v === 105) { await hocTavern('Head to the tavern and talk to Adam, Zeke or Esther'); return; }
    // 115: Anya's parasol inspected: back to the tavern, where they send you to the most experienced tracker (step 63)
    if (v === 115) { await hocTavern('Back to the tavern: talk to Adam, Zeke or Esther'); return; }
    // 120: sent to the most experienced tracker: Sorrel by the Amberfell well (step 64)
    // Sorrel moves no var: your "Okay, thank you. I'll head to the barrier." ends it; then Ash near the
    // barrier (step 65), any Ash in view (he has several ids in this quest), else his barricade spot.
    if (v === 120) {
      if ((await hocLineSeen('sorrelDone')) || hocManualTick(64)) {
        if (!(await hocNearestNamed('Ash', 'Talk to Ash near the barrier'))) await hocTalkNpc(HOC_ASH, 'Talk to Ash near the barrier', HOC_ASH_X, HOC_ASH_Y, 0);
        return;
      }
      await hocTalkNpc('Sorrel', 'Talk to Sorrel near the well in Amberfell', HOC_SORREL_X, HOC_SORREL_Y, 0);
      return;
    }
    // 125: Ash has moved to the shrine (62100 = 1 shows the Ash copy 32847 as 32845): talk to him there
    // to start tracking Anya and Inanna (step 66, "Yes.").
    // (only that copy: the barrier Ash is another NPC and nearer on the way)
    if (v === 125) { await hocTalkNpc('#32845', 'Talk to Ash at the Shrine of Inanna to start tracking', HOC_SHRINE_X, HOC_SHRINE_Y, 0, 'yes'); return; }
    // 135: the four clues are tracked; the trail goes into Moonsylvar Wood, south-west of Berylbrook (step 71)
    if (v === 135) {
      const sc = await qgScene();
      if (!(await qgObjectById(HOC_MOONSYLVAR, 'Moonsylvar Wood\nEnter, then follow the portals', sc.objects)))
        qgObject('Moonsylvar Wood', 'Enter (south-west of Berylbrook), then follow the portals', HOC_MOONSYLVAR_X, HOC_MOONSYLVAR_Y, 0);
      return;
    }
    // Tracking (130 on): 62117 counts the clues tracked so far and picks the next one (0 = Anya's parasol).
    // Each Track asks "Start tracking with Ash?" (Yes). Past the known clues the guide waits for the next step.
    // 140: inside Moonsylvar Wood (an instance; 62104 and 62117 reset on entry). The route is a list of
    // moves: a Path (loc 140945, Follow) in a direction from where you stand, or something to Track.
    // 62090 moves on inside the wood: 140 from the entrance, 145 at the Bloodsplatter (move 7), 150 taken to
    // be the Bear (move 12, unconfirmed); 62118 counts the Track moves of the whole route (Log 1, Bloodsplatter 2)
    if (v in HOC_WOOD_PARTS) { await hocWoodStep(vbm[HOC_WOOD_TRACKS] | 0, HOC_WOOD_PARTS[v]); return; }
    // 155, then 160 (seen west of Fenmoor with Ash): out of the wood, east through Fenmoor to Shuruk-Ba
    // (step 72; the Linbog Village bank is on the way), then the Crimson Gate (loc 141053 at 4076,1610, 5x5,
    // Enter from 155 on as 141055) with "Yes." (step 73). Past the gate, Anya or Inanna starts the fight
    // (step 74): any option twice, then "You're not taking Inanna (Continue Quest)".
    // 180 on: Vas and Mina arrive, then the fight
    if (v === 180 || v === 185) { await hocGhraziFight(); return; }
    // 190: the Ghrazi are beaten (they still read full life points and Attack, so the fight step must stop
    // here). The talk after it runs through 195 (Vas: "Mina..."), and at 200 the floor is clicked once (steps 78, 79).
    // 190 and 195: the talk after the fight; 200: the floor clicked once to go on. A small mark on your own tile says what to do (the
    // centre prompt was too big and sat over the dialogue).
    if (v === 190 || v === 195 || v === 200) {
      hocCenter('');
      const P = qgP;
      // 190 and 195 are the talk after the fight, 200 the one floor click
      const say = v === 200 ? 'Click the floor to continue' : 'Continue the conversation';
      qgClrNpc(); qgClrDlg(); qgClrItem();
      if (P) qgOv('overlay.guideTiles', [{ x: P.x, y: P.y, plane: P.p | 0, label: say }]); else qgClrTiles();
      return;
    }
    // 205 on (guessed): the floor clicked again; Anya or Inanna to finish (step 80, any option). The quest
    // copy of Inanna (32830) shows as her shrine self (32826) from 200 to 210, so out of view her shrine spot is marked.
    if (v >= 205 && v < HOC_DONE) {
      hocCenter('');
      // the last talk's chooser ("Nor do I." / "I'm not sure how I feel." / "Well, I do!"): any, one picked at random
      if (hocFinalPick < 0) hocFinalPick = qgRand(HOC_FINAL_OPTS.length);
      if (await hocOption([HOC_FINAL_OPTS[hocFinalPick]])) return;
      // 210: that talk done, the rewards to claim; they need 5 free backpack spaces ("You require 5 spaces free…")
      let used = 0;
      try { const inv = JSON.parse(await rtxData.raw('state.inventory')); if (inv && Array.isArray(inv.items)) used = inv.items.filter(it => it[1] > 0).length; } catch (e) {}
      const room = used <= 28 - 5 ? '' : '\nFREE ' + (used - 23) + ' MORE BACKPACK SPACE' + (used - 23 > 1 ? 'S' : '') + ' FOR THE REWARDS';
      const label = (v === 210 ? 'Claim the quest rewards from Anya or Inanna' : 'Talk to Anya or Inanna to finish the quest') + room;
      const sc = await qgScene(), P = qgP;
      const d = n => P ? Math.max(Math.abs(n.x - P.x), Math.abs(n.y - P.y)) : 0;
      let hit = null;
      for (const n of sc.npcs || []) if (n && /^(anya|inanna)$/i.test(n.name || '') && (!hit || d(n) < d(hit))) hit = n;
      if (hit) await hocTalkNpc('#' + hit.id, label, hit.x, hit.y, hit.plane | 0);
      else await hocTalkNpc(HOC_INANNA, label, HOC_SHRINE_X, HOC_SHRINE_Y, 0);
      return;
    }
    // 165: through the gate ("Are you ready to face the Ghrazi?" Yes.): Anya or Inanna begins the fight.
    // 170: in the arena with them (Anya 32825, Inanna 32831, the Sanguine heart idle), no Ghrazi yet: the same.
    if (v === 165 || v === 170 || v === 175) {   // 175: the first chooser answered (62124 = the pick), talk goes on
      if (hocGhraziPick1 < 0) hocGhraziPick1 = qgRand(HOC_GHRAZI_OPTS1.length);
      if (hocGhraziPick2 < 0) hocGhraziPick2 = qgRand(HOC_GHRAZI_OPTS2.length);
      // "Propose another plan." comes back without the plan picked and with "You're not taking Inanna. (Continue
      // quest)" added: that one first whenever it is offered, so no second plan gets boxed
      if (await hocOption(["you're not taking inanna"])) return;
      if (await hocOption([HOC_GHRAZI_OPTS1[hocGhraziPick1], HOC_GHRAZI_OPTS2[hocGhraziPick2]])) return;
      const label = 'Talk to Anya or Inanna to begin the fight';
      const sc = await qgScene(), P = qgP;
      const d = n => P ? Math.max(Math.abs(n.x - P.x), Math.abs(n.y - P.y)) : 0;
      let hit = null;
      for (const n of sc.npcs || []) if (n && /^(anya|inanna)$/i.test(n.name || '') && (!hit || d(n) < d(hit))) hit = n;
      if (hit) await hocTalkNpc('#' + hit.id, label, hit.x, hit.y, hit.plane | 0); else qgClearAll();
      return;
    }
    if (v === 155 || v === 160) {
      if (await hocOption(["you're not taking inanna", 'yes'])) return;
      const sc = await qgScene(), P = qgP;
      const d = n => P ? Math.max(Math.abs(n.x - P.x), Math.abs(n.y - P.y)) : 0;
      let hit = null;
      for (const n of sc.npcs || []) if (n && /^(anya|inanna)$/i.test(n.name || '') && (!hit || d(n) < d(hit))) hit = n;
      if (hit) { hocInArena = true; await hocTalkNpc('#' + hit.id, 'Talk to ' + hit.name + ' to begin the fight', hit.x, hit.y, hit.plane | 0); return; }
      if (!(await qgObjectById([HOC_CRIMSON_GATE, HOC_CRIMSON_GATE + 2], 'Crimson Gate\nEnter (east end of Shuruk-Ba)', sc.objects)))
        qgObject('Crimson Gate', 'Enter it at the east end of Shuruk-Ba (via Fenmoor; bank at Linbog Village)', HOC_CRIMSON_GATE_X, HOC_CRIMSON_GATE_Y, 0);
      return;
    }
    if (v === 130) {   // the clue chain runs at 130 only; its vars reset on the way into the wood
      if (await hocOption(['yes'])) return;
      const clue = HOC_TRACK_CHAIN[hocTrackTarget(vbm).next];
      if (!clue) { qgClearAll(); return; }
      // until Ash reaches it (its var still 0) the clue offers no Track: walk there with him first
      const act = (clue.vb && !(vbm[clue.vb] | 0)) ? 'Walk here with Ash' : clue.act;
      const sc = await qgScene();
      if (!(await qgObjectById([clue.id, ...clue.alt], clue.name + '\n' + act, sc.objects)))
        qgObject(clue.name, act + ' (' + clue.where + ')', clue.x, clue.y, 0);
      return;
    }
    // 110: the tavern sends you to check on Inanna: Anya's parasol at the shrine (placement 141032 at
    // 3542,1425, shown as 141033 "Anya's parasol" with Inspect from 110 to 125, 141034 with Track at 130)
    if (v === 110) {
      const sc = await qgScene();
      if (!(await qgObjectById([HOC_PARASOL, HOC_PARASOL + 1], "Anya's parasol\nInspect", sc.objects)))
        qgObject("Anya's parasol", 'Go to the shrine and inspect it', HOC_PARASOL_X, HOC_PARASOL_Y, 0);
      return;
    }
    // 70: Adam has the items. The Ghrazi events start from Inanna at her shrine (in any order).
    if (v === 70) { await hocInanna('Talk to Inanna at the Shrine of Inanna (equip for combat)', 'yes'); return; }
    // 75: the Ghrazi fight is on. Inanna again: she tells you to follow her howls.
    if (v === 75) { await hocInanna('Talk to Inanna again'); return; }
    // 80: partway through that talk ("The moon shall rise soon. They will attack at nightfall.")
    if (v === 80) { await hocInanna('Continue the conversation with Inanna'); return; }
    // 85: Anya's "Are you ready to face the Ghrazi assault?" then "Fend off Ghrazi assault?" (Yes.),
    // after which Inanna is talked to again ("I sense trouble. Follow my howls!") with no var moving
    // 62105 (varp 13690) is set by that "Follow my howls!" line (seen at 3, with 62110 = 1): Anya
    // knows where Inanna has gone. "Where's Inanna?" (at 3: "Inanna's howls are in the Hollow Hill
    // direction."), then "Teleport to Inanna?" (Yes.)
    // 62106 goes 1 -> 3 on Anya's teleport. The events sit in instances, so their NPCs are boxed by
    // id when in view, without fixed tiles.
    if (v === 85) {
      const ev = vbm[HOC_EVENT] | 0, inEvent = (vbm[HOC_EVENT_STAGE] | 0) >= 2;
      const st = ev ? hocEvState(vbm, ev) : 0;
      // all seven closed (each state at 3): Inanna, who takes you out (62090 then goes to 90)
      if (!ev && [1, 2, 3, 4, 5, 6, 7].every(n => hocEvState(vbm, n) >= 3)) { await hocInanna('Talk to Inanna'); return; }
      if (!ev) { await hocInanna('Talk to Inanna again: she tells you to follow her howls', 'yes'); return; }
      if (inEvent) {
        if (ev === 3) { await hocRazFight(vbm); return; }
        if (ev === 1) { await hocFarmFight(st); return; }
        if (ev === 2) { await hocSilverquillFight(st); return; }
        if (ev === 4) { await hocTrapFight(st, vbm); return; }
        if (HOC_OMEGA_EVENTS.includes(ev)) { await hocOmegaFight(st); return; }
        if (ev === 6) { await hocLighthouseFight(st); return; }
        qgClearAll();   // an event whose steps are not mapped yet
        return;
      }
      // back at the shrine with the event's fight behind you: Inanna closes it (62105 back to 0)
      if (st >= 2) { await hocInanna('Talk to Inanna: the fight is over'); return; }
      await hocTalkNpc('Anya', 'Talk to Anya: she knows where Inanna has gone and can teleport you', 0, 0, 0, "where's inanna", 'yes');
      return;
    }
    // 62091 = 4 is set during that talk with Adam, 62090 = 65 further into it, 70 once it ends
    if ((v === 60 && prep >= 4) || v === 65) { await hocTalkNpc(HOC_ADAM, 'Continue the conversation with Adam', HOC_ADAM_X, HOC_ADAM_Y, 0); return; }
    const cure = vbm[HOC_CURE] | 0;
    // 62096 = 2: the super antisanguine is made, so Esther's errand is done until the hand-over.
    // Next in the guide is Liat's halberd from Samson, just north of the quarry.
    if (v === 60 && silverquill >= 2) {
      // 62095 = 4: the halberd is made. Fox is in the cave in Hollow Hill: the entrance until he is in the scene.
      if (talked >= 4 && prep >= 2) {
        // Fox's task: every trap spot still to build or set, marked at its centre
        const marks = [];
        const kit = (await qgInvCount(HOC_GIANT_LOGS)) > 0 && (await qgInvCount(HOC_ROPE)) > 0;
        for (const t of (prep >= 3 ? [] : HOC_TRAPS)) {   // 62091 = 3: all armed, whatever the traps show later
          const st = vbm[t.vb] | 0;
          if (st === 1) marks.push({ x: t.x + 1, y: t.y + 1, plane: 0, label: 'Unbuilt log trap\nBuild' + (kit ? '' : ' (needs giant logs and a rope from Fox)') });
          else if (st === 2) marks.push({ x: t.x + 1, y: t.y + 1, plane: 0, label: 'Log trap\nSet' });
        }
        if (marks.length && hocInFoxCave()) {   // still inside with Fox: out through the cave mouth first
          const sc = await qgScene();
          if (!(await qgObjectById(HOC_FOX_CAVE_EXIT, 'Cave\nExit, then go west up the mountain', sc.objects)))
            qgObject('Cave', 'Exit, then go west up the mountain', HOC_FOX_CAVE_EXIT_X, HOC_FOX_CAVE_EXIT_Y, 0);
          return;
        }
        if (marks.length) { qgClrNpc(); qgClrDlg(); qgClrItem(); qgOv('overlay.guideTiles', marks); return; }
        // 62096 = 3: Esther has the exalted lump. Next the halberd goes to Liat at the smithy.
        if (silverquill >= 3 && talked >= 5) {   // 62095 = 5: Liat has seen the halberd, Zeke is next
          if ((await hocLineSeen('zeke')) || hocManualTick(24)) {
            await hocTalkNpc(HOC_ADAM, 'Talk to Adam', HOC_ADAM_X, HOC_ADAM_Y, 0);
            return;
          }
          await hocTalkNpc(HOC_ZEKE, 'Talk to Zeke in the building east of Adam', HOC_ZEKE_X, HOC_ZEKE_Y, 0);
          return;
        }
        if (silverquill >= 3) {
          const bring = (await qgInvCount(HOC_HALBERD)) < 1 ? '\nBRING THE HAVENSILVER HALBERD' : '';
          await hocTalkNpc(HOC_LIAT, 'Show the halberd to Liat at the smithy (she may be outside)' + bring, HOC_LIAT_X, HOC_LIAT_Y, 0);
          return;
        }
        // all three armed: back to Esther with an exalted essence lump, chunk or slab
        const bring = (await hocInvTotal(HOC_EXALTED_LUMPS)) < 1 ? '\nBRING AN EXALTED ESSENCE LUMP' : '';
        await hocEsther('Talk to Esther with the exalted essence lump' + bring);
        return;
      }
      if (talked >= 4) {
        const sc = await qgScene();
        if ((sc.npcs || []).some(n => n && n.id === HOC_FOX_ID)) { await hocTalkNpc(HOC_FOX, 'Talk to Fox', 0, 0, 0); return; }
        if (!(await qgObjectById(HOC_FOX_CAVE, 'Cave\nEnter, then talk to Fox', sc.objects)))
          qgObject('Cave', 'Enter the cave in Hollow Hill, then talk to Fox', HOC_FOX_CAVE_X, HOC_FOX_CAVE_Y, 0);
        return;
      }
      // 62095 = 3: Samson has taught the halberd. The anvil by his furnace until the smithing has
      // started (unfinished smithing item held), then nothing while it is finished.
      if (talked >= 3) {
        if ((await qgInvCount(HOC_UNF_SMITHING)) > 0 || (await qgInvCount(HOC_HALBERD)) > 0) { qgClearAll(); return; }
        const sc = await qgScene();
        const need = [], bars = await qgInvCount(HOC_HAVENSILVER_BAR);
        if (bars < 4) need.push((4 - bars) + (bars ? ' more' : '') + ' havensilver bar' + (4 - bars > 1 ? 's' : ''));
        if ((await qgInvCount(HOC_MAPLE_LOGS)) < 1) need.push('1 maple logs');
        const bring = need.length ? '\nBRING ' + need.join(' AND ').toUpperCase() : '';
        if (!(await qgObjectById(HOC_ANVIL, 'Anvil\nSmith a havensilver halberd' + bring, sc.objects)))
          qgObject('Anvil', 'Smith a havensilver halberd' + bring, HOC_ANVIL_X, HOC_ANVIL_Y, 0);
        return;
      }
      await hocTalkNpc(HOC_SAMSON, 'Talk to Samson in the smithing house north of the quarry', HOC_SAMSON_X, HOC_SAMSON_Y, 0);
      return;
    }
    // Curing Silverquill done: on to Esther's super antisanguine. No var moves on the way, so where you
    // stand decides: west of the barricade near Ash it is the barricade, east of it Azur in the quarry.
    if (v === 60 && silverquill === 1 && cure >= 7) {
      if ((vbm[HOC_QUARRY] | 0) >= 1) { await hocQuarry(); return; }   // Azur and Aurora done
      if (hocEastOfBarricade()) { await hocTalkNpc(HOC_AZUR, 'Talk to Azur, then Aurora, in the Exalted Quarry', HOC_AZUR_X, HOC_AZUR_Y, 0); return; }
      await hocBarricade();
      return;
    }
    if (v === 60 && silverquill === 1) {
      // with the potion in hand, Silverquill is next; before that it is all Anya
      if ((await qgInvCount(HOC_SQ_POTION)) > 0) { await hocSilverquillDen(); return; }
      const missing = [];
      if ((await qgInvCount(HOC_SILVER_SPINES)) < 1) missing.push('1 silver spine');
      if ((await qgInvCount(HOC_SANGUINE_SPINES)) < 1) missing.push('1 sanguine spine');
      const bring = missing.length ? '\nBRING ' + missing.join(' AND ').toUpperCase() : '';
      // 6 = the potion has been poured: out of the den and back to Anya
      if (cure >= 6) {
        if (hocInDen()) {
          let boxed = false; try { boxed = await PLUGIN_API['overlay.highlightOption'].run(['yes'], myPid()); } catch (e) {}
          if (boxed) { qgClrNpc(); qgClrTiles(); qgClrItem(); return; }
          const sc = await qgScene();
          if (!(await qgObjectById(HOC_VENTRICLE_EXIT, 'Cave ventricle\nExit', sc.objects)))
            qgObject('Cave ventricle', 'Exit', HOC_VENTRICLE_EXIT_X, HOC_VENTRICLE_EXIT_Y, 0);
          return;
        }
        await hocTalkNpc('Anya', 'Talk to Anya', HOC_ANYA_X, HOC_ANYA_Y, 0);
        return;
      }
      if (cure === 5) { await hocTalkNpc('Anya', 'Get the empowered antisanguine (silverquill) from Anya', HOC_ANYA_X, HOC_ANYA_Y, 0); return; }
      if (cure === 4) { await hocTalkNpc('Anya', 'Hand Anya the silver and sanguine spines' + bring, HOC_ANYA_X, HOC_ANYA_Y, 0, 'yes'); return; }
      const opts = ['yes'];
      if ((vbm[HOC_ANYA_TRUST] | 0) === 0) {   // the trust question is still to come
        if (hocAnyaTrustPick < 0) hocAnyaTrustPick = qgRand(HOC_ANYA_TRUST_OPTS.length);
        opts.unshift(HOC_ANYA_TRUST_OPTS[hocAnyaTrustPick]);
      } else if ((vbm[HOC_ANYA_FORGIVE] | 0) === 0) {   // then the forgive question
        if (hocAnyaForgivePick < 0) hocAnyaForgivePick = qgRand(HOC_ANYA_FORGIVE_OPTS.length);
        opts.unshift(HOC_ANYA_FORGIVE_OPTS[hocAnyaForgivePick]);
      }
      await hocTalkNpc('Anya', 'Talk to Anya in the Blighted Cave' + bring, HOC_ANYA_X, HOC_ANYA_Y, 0, ...opts);
      return;
    }
    if (v === 60) {
      if (prep >= 1 && talked === 0) { await hocTalkNpc(HOC_ZEKE, 'Talk to Zeke in the building to the east', HOC_ZEKE_X, HOC_ZEKE_Y, 0); return; }
      if (prep >= 1 && talked === 1) { await hocTalkNpc(HOC_LIAT, 'Talk to Liat at the smithy (she may be outside)', HOC_LIAT_X, HOC_LIAT_Y, 0); return; }
      if (prep >= 1) { await hocEsther('Talk to Esther at the top of the lighthouse'); return; }
      await hocTalkNpc(HOC_ADAM, 'Talk to Adam again', HOC_ADAM_X, HOC_ADAM_Y, 0);
      return;
    }
    qgClearAll();
  }
  QG_AUTO['Heralds of Crimson'] = {
    vbs: [HOC_PROG, HOC_PREP, HOC_PREP_COUNT, HOC_LIAT_DONE, HOC_SILVERQUILL, HOC_CURE, HOC_QUARRY, ...HOC_TRAPS.map(t => t.vb), HOC_EVENT, HOC_EVENT_STAGE, ...HOC_EVENT_VBS, HOC_TRACK, ...HOC_TRACK_CHAIN.filter(c => c.vb).map(c => c.vb)],
    inv: true,
    done: (vb, inv) => {
      const v = vb[HOC_PROG] | 0, prep = vb[HOC_PREP] | 0, talked = vb[HOC_PREP_COUNT] | 0, liat = vb[HOC_LIAT_DONE] | 0;
      const silverquill = vb[HOC_SILVERQUILL] | 0, cure = vb[HOC_CURE] | 0;
      const s = new Set();
      if (v >= 10) s.add(0);    // Talk to Sorrel in Amberfell
      if (v >= 25) s.add(1);    // Talk to Ash at the south-east barricade
      if (v >= 35) s.add(2);    // Click the Barricade to continue the dialogue
      if (v >= 45) s.add(3);    // Attack and defeat the Sigma werewolf on the hill above Ash
      if (v >= 55) s.add(4);    // Go back to the barricade and talk to Ash
      if (v >= 60) s.add(5);    // Report to Adam in Wendlewick
      if (v > 60 || (v === 60 && prep >= 1)) s.add(6);   // Talk to Adam again, then proceed to talk to
      if (v > 60 || (v === 60 && talked >= 1)) s.add('6.1');   // Zeke in the neighbouring building to the east
      if (v > 60 || (v === 60 && (liat >= 1 || talked >= 2))) s.add('6.2');   // Liat at the smithy
      if (v > 60 || (v === 60 && (talked >= 3 || silverquill >= 1))) s.add('6.3');   // Esther on the lighthouse's 1st floor
      if (cure >= 5 || (inv && inv.has(HOC_SQ_POTION))) s.add(7);   // Talk to Anya, hand her the spines: she has made the potion
      if (cure >= 6) s.add(8);   // Pour the potion onto Silverquill in her den
      if (cure >= 7) s.add(9);   // Exit the den and talk to Anya
      if (!(v === 60 && cure >= 7)) { hocCrossed = false; hocAtQuarry = false; }
      else {
        if (hocEastOfBarricade()) hocCrossed = true;
        const P = qgP;
        if (P && Math.max(Math.abs(P.x - HOC_AZUR_X), Math.abs(P.y - HOC_AZUR_Y)) <= 40) hocAtQuarry = true;
      }
      if (hocCrossed || hocAtQuarry) s.add(10);   // Jump over the barricade near Ash in Amberfell
      if (hocAtQuarry) s.add(11);                  // Run east via Berylbrook, then north to Heathervein
      const quarry = (vb[HOC_QUARRY] | 0) >= 1, has = ids => !!inv && ids.some(id => inv.has(id));
      if (quarry) { s.add(10); s.add(11); s.add(12); }   // talked to Azur and Aurora
      if (!(v === 60 && quarry)) { hocExaltedSeen = false; hocLegSeen = false; hocKeptOne = false; }
      else {
        if (has(HOC_EXALTED_LUMPS)) hocExaltedSeen = true;
        if (has([HOC_COLOSSAL_LEG])) hocLegSeen = true;
        // down to the one exalted lump kept for Esther: the rest has been processed
        if (hocExaltedSeen && hocExaltedLeft === 1) hocKeptOne = true;
      }
      if (quarry && (has(HOC_LUMPS) || hocExaltedSeen)) s.add(13);   // mined until lumps came out
      if (hocExaltedSeen) { s.add(14); s.add(15); }                  // deposited one, mined the colossus
      if (hocKeptOne) { s.add('15.1'); s.add('15.2'); }               // kept one for Esther, processed the rest
      if (hocLegSeen) s.add('15.3');                                  // mined a colossal leg
      if (has(HOC_SUPER_ANTISANG) || silverquill >= 2) {   // super antisanguine mixed (62096 = 2)
        for (const k of [7, 8, 9, 10, 11, 12, 13, 14, 15, '15.1', '15.2', 16]) s.add(k);
      }
      if (has([HOC_COLOSSAL_LEG])) s.add('15.3');
      if (v === 60 && silverquill >= 2 && talked >= 3) s.add(17);   // Samson taught the halberd
      if (has([HOC_HALBERD]) || (v === 60 && silverquill >= 2 && talked >= 4)) { s.add(17); s.add(18); }   // halberd smithed (62095 = 4)
      const traps = HOC_TRAPS.map(t => vb[t.vb] | 0);
      const foxDone = v === 60 && silverquill >= 2 && talked >= 4 && prep >= 2;
      if (!foxDone) hocAtTraps = false;
      else {
        const P = qgP;
        if (P && HOC_TRAPS.some(t => Math.max(Math.abs(P.x - t.x - 1), Math.abs(P.y - t.y - 1)) <= 10)) hocAtTraps = true;
      }
      const trapsDone = foxDone && (prep >= 3 || traps.every(t => t >= 3));      // 62091 = 3 once all three are armed
      if (foxDone) s.add(19);                                                    // talked to Fox (62091 = 2)
      if (foxDone && (hocAtTraps || trapsDone || traps.some(t => t >= 2))) s.add(20);   // up the mountain at the trap spots
      if (trapsDone) s.add(21);                                                  // all three built and set
      if (v === 60 && silverquill >= 3) s.add(22);                               // Esther has the exalted lump (62096 = 3)
      if (v === 60 && silverquill >= 3 && talked >= 5) s.add(23);               // Liat has seen the halberd (62095 = 5)
      if (!(v === 60 && silverquill >= 3 && talked >= 5)) hocSaid.zeke = false;
      if (hocSaid.zeke) s.add(24);                                                // Zeke's "overcome my fear" line seen
      // past 60 the whole return-to-Adam run is behind you
      if (v === 60 && prep >= 4) s.add(24);                                      // Adam's talk has begun (62091 = 4): Zeke is behind you
      if (v > 60) { for (let i = 0; i <= 24; i++) s.add(i); for (const k of ['6.1', '6.2', '6.3', '15.1', '15.2', '15.3']) s.add(k); }   // 65: in Adam's talk
      if (v >= 70) s.add(25);                                                    // 70: Adam has the items
      if (v >= 75) s.add(26);                                                    // 75: Inanna has started the fight with the Ghrazi
      if (v >= 90) for (let i = 26; i <= 60; i++) s.add(i);                     // 90: all seven Ghrazi events are behind you
      if (v >= 110) s.add(61);                                                   // 110: the tavern has sent you to check on Inanna
      if (v >= 115) s.add(62);                                                   // 115: Anya's parasol inspected
      if (v >= 120) s.add(63);                                                   // 120: the tavern named the most experienced tracker
      if (v !== 120) hocSaid.sorrelDone = false;
      if (hocSaid.sorrelDone || v >= 125) s.add(64);                             // Sorrel sent you to the barrier
      if (v >= 125) s.add(65);                                                   // 125: Ash at the barrier talked to (62100 = 1)
      if (v >= 130) s.add(66);                                                   // 130: tracking started with Ash at the shrine (guessed)
      // Moonsylvar Wood's moves are the sub-points of step 71: ticked as saved ticks while inside (hocWoodStep)
      // each later part of the wood puts the moves before it behind you; past the last part, all of step 71
      for (const [pv, start] of Object.entries(HOC_WOOD_PARTS)) if (v >= +pv) for (let j = 1; j <= start; j++) s.add('71.' + j);
      if (v > 150) { s.add(71); for (let j = 1; j <= HOC_WOOD_ROUTE.length; j++) s.add('71.' + j); }
      const tracked = v >= 135 ? 4 : v === 130 ? hocTrackTarget(vb).next : 0;   // clues tracked so far (all four by 135)
      if (tracked >= 1) s.add(67);                                               // Anya's parasol
      if (tracked >= 2) s.add(68);                                               // the flower planter at Eastfold Farm
      if (tracked >= 3) s.add(69);                                               // Anya's chest
      if (tracked >= 4) s.add(70);                                               // the bloodsplatter north-east of the Deserted Mine
      // The Ghrazi events, each by its own state var so the ticks outlast it: the howl once it has
      // started, Anya once you are in it, the whole section once you are back at the shrine after it.
      const ev = vb[HOC_EVENT] | 0, inEvent = v === 85 && (vb[HOC_EVENT_STAGE] | 0) >= 2;
      for (const k of Object.keys(HOC_EVENTS)) {
        const n = +k, steps = HOC_EVENTS[k].steps, st = hocEvState(vb, n), here = inEvent && ev === n;
        if (st >= 1) s.add(steps[0]);
        if (here || st >= 2) s.add(steps[1]);
        if (st >= 3 || (st >= 2 && !here)) for (const i of steps) s.add(i);
      }
      // Raz's middle steps, from the lines seen in its instance
      if (!(v === 85 && ev === 3)) { hocSaid.ivar = false; hocSaid.razAttack = false; hocSaid.razLeave = false; }
      if (hocSaid.ivar || (vb[HOC_RAZ_STATE] | 0) >= 2) s.add(29);             // Ivar's "We got it for now." seen
      if (hocSaid.razLeave) s.add(30);                                           // Anya's "Indeed. We should leave." after Raz again
      // The second Wendlewick fight's middle steps (event 5), from Inanna's line and the omegas in its instance
      // The farm fight's middle step (event 1): Ben talked to once its state moves on
      if (hocEvState(vb, 1) >= 2) s.add(59);
      // Silverquill's fight middle steps (event 2): calmed and fought once its state moves on
      if (hocEvState(vb, 2) >= 2) { s.add(34); s.add(35); }
      // The lighthouse fight's middle steps (event 6): Inanna talked to once the barricade is up for repair
      if (!(v === 85 && ev === 6)) { hocBarricadeSeen = false; hocInannaSpoke = false; }
      if (hocBarricadeSeen || hocEvState(vb, 6) >= 2) s.add(54);
      if (hocEvState(vb, 6) >= 2) s.add(55);
      // The trap fight's middle steps (event 4)
      if (!(v === 85 && ev === 4)) { hocTrapFightOn = false; hocSaid.trapsGo = false; }
      if (hocTrapFightOn || hocEvState(vb, 4) >= 2) s.add(39);        // Inanna talked to: werewolves by the traps
      const trapsFired = v === 85 && ev === 4 && HOC_TRAPS.every(t => (vb[t.vb] | 0) === 0);   // each trap's var 3 -> 0 when triggered
      if (hocEvState(vb, 4) >= 2 || trapsFired) { s.add(39); s.add(40); }   // traps triggered
      // (latches for the event under way; each event's own state keeps its ticks afterwards)
      if (!(v === 85 && HOC_OMEGA_EVENTS.includes(ev))) { hocOmegaFought = false; hocOmegasCleared = false; hocOmegasSeen = false; hocSaid.militia = false; }
      for (const n of HOC_OMEGA_EVENTS) {
        const steps = HOC_EVENTS[n].steps, here = v === 85 && ev === n;
        if (here && hocOmegaFought) s.add(steps[2]);                              // Inanna talked to: the omegas are being fought
        if ((here && hocOmegasCleared) || hocEvState(vb, n) >= 2) { s.add(steps[2]); s.add(steps[3]); }   // omegas down
      }
      // Shuruk-Ba: reached once you stand near it (the Crimson Gate is at its east end), latched while at 155
      if (v !== 155 && v !== 160) { hocAtShuruk = false; hocInArena = false; }
      else if (qgP && Math.max(Math.abs(qgP.x - HOC_CRIMSON_GATE_X), Math.abs(qgP.y - HOC_CRIMSON_GATE_Y)) <= 40) hocAtShuruk = true;
      if (hocAtShuruk || hocInArena || v >= 165) { s.add(72); s.add('72.1'); s.add('72.2'); }   // with its bank and canoe tips
      if (hocInArena || v >= 165) s.add(73);                                    // through the Crimson Gate (165)
      if (v > 175) s.add(74);                                                   // the fight begun with Anya or Inanna (unconfirmed)
      // the fight: its first two mechanics once the Ghrazi are there to attack, the platform once Vasily is at 300k
      if (v < 180) { hocFightSeen = false; hocVasilyLow = false; }
      if (hocFightSeen || v >= 190) { s.add(75); s.add(76); }
      if (hocVasilyLow || v >= 190) s.add(77);                                   // 190: the Ghrazi are beaten
      if (v >= 205) { s.add(78); s.add(79); }                                    // 205: the talk after the fight and the one floor click done
      if (v >= 210) s.add(80);                                                   // 210: the last talk done, the rewards to claim
      if (v >= HOC_DONE) for (let i = 0; i < 82; i++) s.add(i);
      return s;
    },
  };

  // Items for the quest and per section of the quick guide (shown under "Required items").
  function hocItems(section) {
    const silver = { id: HOC_SILVER_SPINES, n: 1, name: 'Silver spines' };
    const sanguine = { id: HOC_SANGUINE_SPINES, n: 1, name: 'Sanguine spines' };
    const kwuarm = { id: HOC_KWUARM_UNF, n: 1, name: 'Kwuarm potion (unf)' };
    const bars = { id: HOC_HAVENSILVER_BAR, n: 4, name: 'Havensilver bar' };
    const maple = { id: HOC_MAPLE_LOGS, n: 1, name: 'Maple logs' };
    switch (section) {
      case '':                  return [silver, sanguine, kwuarm, bars, maple, { name: '5 free backpack spaces for the rewards' }];
      case 'Curing Silverquill': return [silver, sanguine];
      case 'Create a super antisanguine potion for Esther (Heathervein)': return [kwuarm];
      case 'Create a havensilver halberd for Liat (Heathervein)': return [bars, maple];
      case 'Return to Adam with the items':
        return [{ id: HOC_EXALTED_LUMPS[0], n: 1, name: 'Exalted essence lump (or chunk/slab)' },
                { id: HOC_HALBERD, n: 1, name: 'Havensilver halberd' }];
      case 'Fight the Ghrazi Blood Knights': return [{ name: 'Combat gear' }, { name: '5 free backpack spaces for the rewards' }];
    }
    return null;
  }

Object.assign(window, { hocItems, hocStep });
})();
