// RuneToolsX panel: Murder on the Border quest guide (quest 490, Fort Forinthry).
(function () {

  // Progress varbit 52689 (cache quest config js5-2 archive 35 file 490): start value 5,
  // complete at 195. Requires quest 489 (New Foundations); 1 QP.
  const MOTB_PROG = 52689, MOTB_DONE = 195;
  // Quest start: Aster in the Fort Forinthry townhall -- npc 29809 at 3302,3571.
  const MOTB_ASTER = 29809, MOTB_ASTER_X = 3302, MOTB_ASTER_Y = 3571;
  const MOTB_FLAVOR1 = ['bear that in mind', 'just like any other day', 'my kind of party'];
  let motbFlavor1 = 0;
  const MOTB_FLAVOR2 = ['sure it will be fine', 'keep our guard up', 'keep things entertaining'];
  let motbFlavor2 = 0;
  const MOTB_BILL = 29806, MOTB_BILL_X = 3286, MOTB_BILL_Y = 3557;   // Bill
  const MOTB_BLUEPRINTS = 125059, MOTB_BP_X = 3287, MOTB_BP_Y = 3555;   // Fort Forinthry blueprints table
  // 12x willow frame 54848/54849n, 6x stone wall segment 54460/54461n.
  const MOTB_FRAME = 54848, MOTB_FRAME_N = 54849, MOTB_WALL = 54460, MOTB_WALL_N = 54461;
  // Blueprint picking runs in the Make-X window (frame 1371 / content 1370): recipe list = 1371
  // comp 22, Kitchen (Tier 1) cell = sub 49; selected-recipe title = 1370 comp 13 text; Start
  // blueprint button = 1370 comp 29 ('Start blueprint' text at 29:3).
  const MOTB_MAKEX_FRAME = 1371, MOTB_MAKEX_LIST = 22, MOTB_KITCHEN_SUB = 49;
  const MOTB_MAKEX_CONTENT = 1370, MOTB_MAKEX_TITLE = 13, MOTB_MAKEX_START = 29;
  // Kitchen construction hotspots (3 spots): per-spot 2-bit loc-morph varbits, 0 = no blueprint,
  // 1 = plain hotspot, 2 = the OPTIMAL hotspot (loc 125242 'Optimal Construction hotspot', action
  // unknown, so it is picked by ELIMINATION when neither known varbit reads 2.
  const MOTB_HOT1 = 51675, MOTB_HOT1_TILE = [3315, 3564];   // vb 51675 [30:31]
  const MOTB_HOT2 = 51674, MOTB_HOT2_TILE = [3313, 3565];   // vb 51674 [28:29]
  const MOTB_HOT3 = 51677, MOTB_HOT3_TILE = [3319, 3567];   // vb 51677 [0:1] (the third locmorph, by elimination)
  const MOTB_KITCHEN_VB = 33400;    // kitchen tier (vp 10762 [14:17]): 1 = Tier 1 built (flips with quest -> 20)
  // The banquet portal (light blue, south of the well): loc 125244 "Murder on the Border", action
  const MOTB_PORTAL = 125244, MOTB_PORTAL_X = 3303, MOTB_PORTAL_Y = 3541;
  const MOTB_WELCOME1 = ['welcome to fort forinthry', 'very perceptive', 'say nothing'];
  let motbWelcome1 = 0;
  const MOTB_WELCOME2 = ['welcome to fort forinthry', "don't have time to chat", 'not ruin the banquet'];
  let motbWelcome2 = 0;
  const MOTB_WELCOME3 = ['duke of edgeville', "actually, i don't", 'someone unimportant'];
  let motbWelcome3 = 0;
  const MOTB_WELCOME4 = ["we've got this", 'surrounded by idiots', 'say nothing'];
  let motbWelcome4 = 0;
  const MOTB_GUESTS_CENTRAL = [29915, 29930];               // Aster, Duke Horacio (central room, ground)
  const MOTB_GUESTS_WEST = [29934, 29920, 29939];           // Rodney, Simon, Duchess Alba (western room, ground)
  const MOTB_GUESTS_UP = [29929, 29927, 29918, 29916];      // Bianca, Iris, King Roald, Ellamaria (second floor)
  const MOTB_GUESTC_OPTS = ['same dish as the other guests', 'just make him the dish', 'say nothing'];   // central room
  let motbGuestC = 0;
  const MOTB_GRP_CENTRAL_VB = 52710, MOTB_GRP_WEST_VB = 52708, MOTB_GRP_UP_VB = 52709;
  const MOTB_FEAST1 = ["doing the best i can", 'offering any support', 'deserve better than you', 'say nothing'];
  let motbFeast1 = 0;
  const MOTB_FEAST2 = ['lock down the fort', 'bottom of this', 'say nothing'];
  let motbFeast2 = 0;
  const MOTB_ASTER85 = ['glad i have your support', "isn't a novel", 'above suspicion yourself', 'say nothing'];
  let motbAster85 = 0;
  const MOTB_NUTROAST = 125283;   // Nut roast loc (Part 1, kitchen SE corner), action Investigate
  const MOTB_NUTROAST_VB = 52692, MOTB_SATCHEL_VB = 52693, MOTB_SIMON_VB = 52695, MOTB_BURIEDBOX_VB = 52694;   // (52690 bitfield: 2/6/22/30 after items 1/2/3/4)
  const MOTB_MEAL_VB = 52696;   // "Detect the poison" (5/8) done -- potion used on the duke's meal (52690 bitfield -> 62)
  const MOTB_KINGROALD_VB = 52691;   // (6/8) Talk to King Roald done (52690 bitfield -> 63)
  const MOTB_ALBA = 29939;      // Duchess Alba npc (7/8, Command Centre) -- talk + Link the "Nut Roast" clue via 1030
  const MOTB_ALBA_CLUE_SUB = 1;   // 1030 "Nut Roast" clue Link entry = comp 19 sub 1
  const MOTB_ALBA_VB = 52697;     // (7/8) Duchess Alba clue linked (52690 bitfield -> 127)
  const MOTB_BIANCA = 29929;      // Bianca npc (8/8, outside the chapel) -- talk + Link "Stolen Jewellery" (1030 comp 19 sub 4, from the live board dump)
  const MOTB_BIANCA_CLUE_SUB = 4;
  const MOTB_BIANCA_OPTS = ['that s unacceptable', "doesn't bother me", 'very poorly hidden', 'reflect poorly on me'];
  let motbBianca = 0;
  const MOTB_BIANCA_VB = 52698;   // (8/8) Bianca clue linked. 52690 is a BITFIELD of the 8 Part-1 done flags 52691-52698 -> 255 = all done; quest varbit 52689 -> 95 = Part 1 complete.
  const MOTB_STAIRS0 = 125006, MOTB_STAIRS1 = 125007, MOTB_STAIRS2 = 125008;
  const MOTB_TRAPDOOR = 125009;   // Trap door (Town Hall top floor), action Bottom floor
  const MOTB_ASTER_P2 = 29915;   // Aster on the Town Hall top floor (same instance id)
  const MOTB_ASSASSIN = 29951, MOTB_ASSASSIN_DEAD = 29952;
  const MOTB_ROALD2_OPTS = ['guards were unaccounted for', "confirm aster's innocence"];
  let motbRoald2 = 0;
  const MOTB_P2_BITFIELD = 52699, MOTB_P2_ITEM1_VB = 52700;
  const MOTB_WAXSEAL = 125287, MOTB_WAXSEAL_VB = 52702;   // Wax seal loc (Part 2 item 2/8), Investigate; done flag 52702 (52699 bitfield -> 5). (flags not sequential from 52700: item1=52700, item2=52702)
  const MOTB_IRIS = 29926, MOTB_IRIS_CLUE_SUB = 10;   // Iris npc (Part 2 item 3/8, Workshop) -- talk + Link "Strange Seal" (1030 comp 19 sub 10, live board dump)
  const MOTB_IRIS_DX = -43, MOTB_IRIS_DY = -14;
  const MOTB_IRIS_OPTS = ['this is all an act', 'really is this dense', "i'm not sure"];
  let motbIris = 0;
  const MOTB_IRIS_VB = 52703;   // (3/8) Iris clue linked (52699 bitfield -> 13)
  const MOTB_PARCHMENT = 125289, MOTB_PARCHMENT_VB = 52701;   // Burnt parchment loc (Part 2 item 4/8), Investigate; done flag 52701 (52699 bitfield -> 15)
  const MOTB_PRINCESS = 29941, MOTB_AMULET_CLUE_SUB = 11;
  const MOTB_PSPOT1 = { anchor: 29948, kind: 'npc', dx: -9, dy: 9 };
  const MOTB_PSPOT2 = { anchor: 29948, kind: 'npc', dx: 22, dy: -7 };   // spot 2 = Bill + (+22,-7) (tile 6508,478 - Bill 6486,485)
  const MOTB_PSPOT3 = { anchor: 29929, kind: 'npc', dx: 10, dy: 6 };    // spot 3 = Bianca 29929 + (+10,+6) (tile 6530,489 - Bianca 6520,483)
  const MOTB_MISCITEMS = 125252;   // Miscellaneous items loc (coffer ritual, appear at spot 1), action Investigate
  const MOTB_COFFER = 125253, MOTB_COFFER_VB = 52704;   // Unearthed coffer loc (Part 2 (5/8)), Investigate; done flag 52704 completes the whole "Find the unearthed coffer" flat [27] (52699 bitfield -> 31)
  const MOTB_ALBA6_OPTS = ['sorry to hear that', "hold his fathers actions", 'say nothing'];
  let motbAlba6 = 0;
  const MOTB_ALBA6_VB = 52705;   // (6/8) Duchess Alba talked (52699 bitfield -> 63)
  const MOTB_RODNEY = 29934, MOTB_RODNEY_CLUE_SUB = 12, MOTB_RODNEY_VB = 52706;   // Rodney npc (Part 2, Workshop) -- [29] ask about Ellamaria, then (7/8) Link "Scorched Will" = 1030 comp 19 sub 12; done flag 52706 (52699 bitfield -> 127)
  const MOTB_RODNEY_K = 29935, MOTB_RODNEY_K_X = 3315, MOTB_RODNEY_K_Y = 3569;   // Rodney npc (Finishing up, in the Kitchen) -- DIFFERENT id from the Part-2 Workshop Rodney 29934
  const MOTB_RODNEY190 = ['look forward to working', 'murder anyone', 'have much choice'];   // [36] Kitchen-Rodney trailing flavour (~), any works, sticky random pick
  let motbRodney190 = 0;
  const MOTB_ELLAMARIA = 29916, MOTB_ELLAMARIA_VB = 52707;   // Queen Ellamaria npc (Part 2 (8/8), by the well); done flag 52707 (52699 bitfield -> 255 = all 8)
  const MOTB_ELLA165_1 = ['stolen from simon after he entered', 'simon left it there himself', "found on the king's road"];
  let motbElla165_1 = 0;
  const MOTB_ELLA165_2 = ['wanted his wealth', 'king roald was their target', 'ruler of edgeville'];
  let motbElla165_2 = 0;
  const MOTB_ELLA165_3 = ['excuse to protect you', 'complete their plan', 'talking about someone else'];
  let motbElla165_3 = 0;
  const MOTB_ELLA165_4 = ['zamorakians offered them power', 'vendetta against the king', 'revenge on queen ellamaria'];
  let motbElla165_4 = 0;
  const MOTB_IDENTIFY_GROUP = 720, MOTB_BIANCA_OPT = 28;
  const MOTB_FATE175 = ['she broke the law', 'risk her harming anyone', 'she deserves it', 'like it either', 'say nothing'];
  let motbFate175 = 0;
  const MOTB_ASTER185 = ['ask how aster is feeling', 'say nothing'];
  let motbAster185 = 0;
  let motbRodneyAsked = false;   // [29] latch: Rodney's "Ask Bianca." line seen (reset off v=155)
  async function motbPrincessSpot(spot, sc, label) {   // mark the relative tile, or talk to Princess once standing on it
    const list = spot.kind === 'obj' ? sc.objects : sc.npcs;
    const a = list.find(e => e && e.id === spot.anchor);
    if (!a) { qgClearAll(); return; }             // anchor out of scene -> nothing to anchor to
    const tx = a.x + spot.dx, ty = a.y + spot.dy;
    if (qgP && (qgP.x | 0) === tx && (qgP.y | 0) === ty) { await qgNpc('#' + MOTB_PRINCESS, 'Talk to Princess'); return; }
    qgClrNpc(); qgClrDlg(); qgClrItem();
    qgOv('overlay.guideTiles', [{ x: tx, y: ty, plane: a.plane | 0, label: label }]);
  }
  const MOTB_SATCHEL = 125285;   // Strange satchel loc (Part 1 item 2/8), action Investigate
  const MOTB_SATCHEL_OPTS = ['who this belongs to', 'could be a diversion', 'add this to my collection'];
  let motbSatchel = 0;
  const MOTB_SIMON = 29920;   // Simon npc (Part 1 item 3/8, outside the Town Hall) -- talk + Link the clue
  const MOTB_LINK_GROUP = 1030, MOTB_LINK_CLUE_COMP = 19, MOTB_LINK_CLUE_SUB = 2;
  const MOTB_BURIEDBOX = 125281;   // Half-buried box loc (Part 1 item 4/8, NE corner), action Investigate
  const MOTB_POISON_BASE = 54584;   // poison detection potion (base) -- from "Take a vial" at the satchel
  const MOTB_CUPBOARD = 125248;     // Supply cupboard loc (Kitchen), action Search
  const MOTB_HOLLYHOCK = 54581;    // hollyhock item id (from the Kitchen supply cupboard)
  const MOTB_UNHEATED = 54585;     // unheated poison detection potion (hollyhock) -- from combining
  const MOTB_HEATED = 54588;       // poison detection potion (hollyhock), heated -- the Cook output (1370:12 item)
  const MOTB_RANGE = 125195;       // Range loc (Kitchen), heat the vial
  const MOTB_DUKEMEAL = 125249;    // Duke's meal (meat pate) loc on the dining table -- use the potion on it
  async function motbBoxOpt() {   // box the first chooser option matching any needle
    try { return await PLUGIN_API['overlay.highlightOption'].run(Array.prototype.slice.call(arguments), myPid()); } catch (e) { return false; }
  }
  async function motbReenter() {   // true = outside the instance -> portal (or its confirm chooser) highlighted
    if (qgInInstance(qgP)) return false;
    let boxed = false;
    try { boxed = await PLUGIN_API['overlay.highlightOption'].run(['yes'], myPid()); } catch (e) {}
    if (boxed) { qgClrNpc(); qgClrTiles(); qgClrItem(); return true; }
    const sc = await qgScene();
    if (!(await qgObjectById(MOTB_PORTAL, 'Murder on the Border\nContinue quest', sc.objects))) qgObject('Murder on the Border', 'Continue quest', MOTB_PORTAL_X, MOTB_PORTAL_Y, 0);
    return true;
  }
  function motbIfaceSubBox(group, comp, sub, label) {   // box one templated grid/list cell
    try {
      const d = JSON.parse(bridge().interfaceComps(myPid(), group, String(comp)) || '{}');
      if (!d || !d.open || !d.hasAbs || !Array.isArray(d.comps)) return false;
      const hit = d.comps.find(c => c && (c.sub | 0) === sub && (c.w | 0) > 0);
      if (!hit) return false;
      qgClrNpc(); qgClrTiles(); qgClrDlg();
      try { rtxData.sync('overlay.panelViz', hit.x + ',' + hit.y + ',' + hit.w + ',' + hit.h + ',' + label); } catch (e) {}
      return true;
    } catch (e) { return false; }
  }
  async function motbIfaceTextHas(group, comp, needle) {   // any comp text contains `needle`
    try {
      const d = JSON.parse(await bridge().interfaceComps(myPid(), group, String(comp)) || '{}');
      if (d && Array.isArray(d.comps))
        return d.comps.some(c => c && String(c.text || '').toLowerCase().indexOf(needle) >= 0);
    } catch (e) {}
    return false;
  }


  async function motbStep() {
    let vbm = {}; try { vbm = await readVarbitValues([MOTB_PROG, MOTB_HOT1, MOTB_HOT2, MOTB_HOT3, MOTB_GRP_CENTRAL_VB, MOTB_GRP_WEST_VB, MOTB_GRP_UP_VB, MOTB_NUTROAST_VB, MOTB_SATCHEL_VB, MOTB_SIMON_VB, MOTB_BURIEDBOX_VB, MOTB_MEAL_VB, MOTB_KINGROALD_VB, MOTB_ALBA_VB, MOTB_WAXSEAL_VB, MOTB_IRIS_VB, MOTB_PARCHMENT_VB, MOTB_COFFER_VB, MOTB_ALBA6_VB, MOTB_RODNEY_VB]); } catch (e) { return; }
    const v = vbm[MOTB_PROG] | 0;
    if (v !== 155) motbRodneyAsked = false;   // Rodney "Ask Bianca" latch only meaningful at v=155
    if (hudShown && v !== 15) hudSet(0, '', false);
    if (v >= MOTB_DONE) { qgClearAll(); return; }   // 195 = complete: nothing to guide
    if (v === 0 || v === 5) {
      if (!motbFlavor1) motbFlavor1 = qgRand(3) + 1;
      if (!motbFlavor2) motbFlavor2 = qgRand(3) + 1;
      await qgDialogNpc('#' + MOTB_ASTER, 'Talk to Aster', MOTB_ASTER_X, MOTB_ASTER_Y, 0,
        'talk about quests', 'murder on the border', 'accept',
        MOTB_FLAVOR1[motbFlavor1 - 1], MOTB_FLAVOR2[motbFlavor2 - 1]);
      return;
    }
    if (v === 10) {
      await qgDialogNpc('#' + MOTB_BILL, 'Talk to Bill', MOTB_BILL_X, MOTB_BILL_Y, 0,
        'murder on the border');
      return;
    }
    if (v === 15) {
      const h1 = vbm[MOTB_HOT1] | 0, h2 = vbm[MOTB_HOT2] | 0, h3 = vbm[MOTB_HOT3] | 0;
      if (h1 !== 0 || h2 !== 0 || h3 !== 0) {
        if (hudShown) hudSet(0, '', false);
        const t = h1 === 2 ? MOTB_HOT1_TILE : (h2 === 2 ? MOTB_HOT2_TILE : MOTB_HOT3_TILE);
        qgTile(t[0], t[1], 0, 'Optimal Construction hotspot\nBuild');
        return;
      }
      const frames = (await qgInvCount(MOTB_FRAME)) + (await qgInvCount(MOTB_FRAME_N));
      const walls = (await qgInvCount(MOTB_WALL)) + (await qgInvCount(MOTB_WALL_N));
      if (frames < 12 || walls < 6) hudSet(31684, 'Need willow frames ' + frames + '/12 and stone wall segments ' + walls + '/6', true);
      else if (hudShown) hudSet(0, '', false);
      if (qgIfaceOpen(MOTB_MAKEX_FRAME)) {
        if (await motbIfaceTextHas(MOTB_MAKEX_CONTENT, MOTB_MAKEX_TITLE, 'kitchen (tier 1)')) {
          if (qgIfaceComp(MOTB_MAKEX_CONTENT, MOTB_MAKEX_START, 'Start blueprint')) return;
        }
        if (motbIfaceSubBox(MOTB_MAKEX_FRAME, MOTB_MAKEX_LIST, MOTB_KITCHEN_SUB, 'Kitchen (Tier 1)')) return;
      }
      const sc = await qgScene();
      if (!(await qgObjectById(MOTB_BLUEPRINTS, 'Fort Forinthry blueprints\nCheck plans', sc.objects))) qgObject('Fort Forinthry blueprints', 'Check plans', MOTB_BP_X, MOTB_BP_Y, 0);
      return;
    }
    if (v === 20) {
      await qgDialogNpc('#' + MOTB_ASTER, 'Talk to Aster', MOTB_ASTER_X, MOTB_ASTER_Y, 0,
        'talk about quests', 'murder on the border');
      return;
    }
    if (v === 25) {
      if (await motbReenter()) return;
      qgClearAll();
      return;
    }
    if (v === 30) {
      if (await motbReenter()) return;
      if (!motbWelcome1) motbWelcome1 = qgRand(3) + 1;
      let boxed = false;
      try { boxed = await PLUGIN_API['overlay.highlightOption'].run([MOTB_WELCOME1[motbWelcome1 - 1]], myPid()); } catch (e) {}
      if (boxed) { qgClrNpc(); qgClrTiles(); qgClrItem(); return; }
      qgClearAll();
      return;
    }
    if (v === 35) {
      if (await motbReenter()) return;
      if (!motbWelcome2) motbWelcome2 = qgRand(3) + 1;
      let boxed = false;
      try { boxed = await PLUGIN_API['overlay.highlightOption'].run([MOTB_WELCOME2[motbWelcome2 - 1]], myPid()); } catch (e) {}
      if (boxed) { qgClrNpc(); qgClrTiles(); qgClrItem(); return; }
      qgClearAll();
      return;
    }
    if (v === 40) {
      if (await motbReenter()) return;
      if (!motbWelcome3) motbWelcome3 = qgRand(3) + 1;
      let boxed = false;
      try { boxed = await PLUGIN_API['overlay.highlightOption'].run([MOTB_WELCOME3[motbWelcome3 - 1]], myPid()); } catch (e) {}
      if (boxed) { qgClrNpc(); qgClrTiles(); qgClrItem(); return; }
      qgClearAll();
      return;
    }
    if (v === 45) {
      if (await motbReenter()) return;
      qgClearAll();
      return;
    }
    if (v === 50) {
      if (await motbReenter()) return;
      if (!motbWelcome4) motbWelcome4 = qgRand(3) + 1;
      let boxed = false;
      try { boxed = await PLUGIN_API['overlay.highlightOption'].run([MOTB_WELCOME4[motbWelcome4 - 1]], myPid()); } catch (e) {}
      if (boxed) { qgClrNpc(); qgClrTiles(); qgClrItem(); return; }
      qgClearAll();
      return;
    }
    if (v === 55 || v === 60 || v === 65) {
      if (await motbReenter()) return;
      const centralDone = (vbm[MOTB_GRP_CENTRAL_VB] | 0) >= 1;   // vb 52710 -> 1
      const westDone = (vbm[MOTB_GRP_WEST_VB] | 0) >= 1;         // vb 52708 -> 1
      const upDone = (vbm[MOTB_GRP_UP_VB] | 0) >= 1;             // vb 52709 -> 1
      if (centralDone && westDone && upDone) {
        await qgDialogNpc('#29915', 'Talk to Aster', 0, 0, 0, 'start the feast');
        return;
      }
      if (!motbGuestC) motbGuestC = qgRand(3) + 1;
      const needles = [];
      if (!centralDone) needles.push(MOTB_GUESTC_OPTS[motbGuestC - 1]);
      let boxed = false;
      if (needles.length) {
        try { boxed = await PLUGIN_API['overlay.highlightOption'].run(needles, myPid()); } catch (e) {}
      }
      if (boxed) { qgClrNpc(); qgClrTiles(); qgClrItem(); return; }
      const pp = qgP ? (qgP.p | 0) : 0;
      const parts = [];
      if (pp === 1) { if (!upDone) MOTB_GUESTS_UP.forEach(id => parts.push('#' + id + '|Second floor guest')); }
      else {
        if (!centralDone) MOTB_GUESTS_CENTRAL.forEach(id => parts.push('#' + id + '|Central room guest'));
        if (!westDone) MOTB_GUESTS_WEST.forEach(id => parts.push('#' + id + '|Western room guest'));
      }
      qgClrTiles(); qgClrDlg(); qgClrItem();
      try { bridge().overlayHighlight(myPid(), parts.join(',')); } catch (e) {}
      return;
    }
    if (v === 75 || v === 80) {
      if (await motbReenter()) return;
      if (!motbFeast1) motbFeast1 = qgRand(4) + 1;
      if (!motbFeast2) motbFeast2 = qgRand(3) + 1;
      let boxed = false;
      try { boxed = await PLUGIN_API['overlay.highlightOption'].run([MOTB_FEAST1[motbFeast1 - 1], MOTB_FEAST2[motbFeast2 - 1]], myPid()); } catch (e) {}
      if (boxed) { qgClrNpc(); qgClrTiles(); qgClrItem(); return; }
      qgClearAll();
      return;
    }
    if (v === 85) {
      if (await motbReenter()) return;
      if (!motbAster85) motbAster85 = qgRand(4) + 1;
      await qgDialogNpc('#29915', 'Talk to Aster', 0, 0, 0, MOTB_ASTER85[motbAster85 - 1]);
      return;
    }
    if (v === 90) {
      if (await motbReenter()) return;
      const sc = await qgScene();
      if ((vbm[MOTB_NUTROAST_VB] | 0) < 1) {
        if (!(await qgObjectById(MOTB_NUTROAST, 'Nut roast\nInvestigate', sc.objects))) qgClearAll();
        return;
      }
      if ((vbm[MOTB_SATCHEL_VB] | 0) < 1) {
        if (!motbSatchel) motbSatchel = qgRand(3) + 1;
        let boxed = false;
        try { boxed = await PLUGIN_API['overlay.highlightOption'].run([MOTB_SATCHEL_OPTS[motbSatchel - 1]], myPid()); } catch (e) {}
        if (boxed) { qgClrNpc(); qgClrTiles(); qgClrItem(); return; }
        if (!(await qgObjectById(MOTB_SATCHEL, 'Strange satchel\nInvestigate', sc.objects))) qgClearAll();
        return;
      }
      if ((vbm[MOTB_SIMON_VB] | 0) < 1) {
        if (motbIfaceSubBox(MOTB_LINK_GROUP, MOTB_LINK_CLUE_COMP, MOTB_LINK_CLUE_SUB, 'Link this clue')) return;
        let boxed = false;
        try { boxed = await PLUGIN_API['overlay.highlightOption'].run(['link a clue to simon'], myPid()); } catch (e) {}
        if (boxed) { qgClrNpc(); qgClrTiles(); qgClrItem(); return; }
        await qgNpc('#' + MOTB_SIMON, 'Talk to Simon');
        return;
      }
      if ((vbm[MOTB_BURIEDBOX_VB] | 0) < 1) {
        if (!(await qgObjectById(MOTB_BURIEDBOX, 'Half-buried box\nInvestigate', sc.objects))) qgClearAll();
        return;
      }
      const clrBox = () => { qgClrNpc(); qgClrTiles(); qgClrItem(); };
      if ((vbm[MOTB_MEAL_VB] | 0) >= 1) {
        if ((vbm[MOTB_KINGROALD_VB] | 0) < 1) {
          await qgDialogNpc('#29918', 'Talk to King Roald', 0, 0, 0, 'talk to king roald', 'notice anything suspicious', 'that s all for now');
          return;
        }
        if ((vbm[MOTB_ALBA_VB] | 0) < 1) {
          if (motbIfaceSubBox(MOTB_LINK_GROUP, MOTB_LINK_CLUE_COMP, MOTB_ALBA_CLUE_SUB, 'Link this clue')) return;
          let alb = false;
          try { alb = await PLUGIN_API['overlay.highlightOption'].run(['link a clue to duchess alba'], myPid()); } catch (e) {}
          if (alb) { qgClrNpc(); qgClrTiles(); qgClrItem(); return; }
          await qgNpc('#' + MOTB_ALBA, 'Talk to Duchess Alba');
          return;
        }
        if (motbIfaceSubBox(MOTB_LINK_GROUP, MOTB_LINK_CLUE_COMP, MOTB_BIANCA_CLUE_SUB, 'Link this clue')) return;
        if (!motbBianca) motbBianca = qgRand(4) + 1;
        let bnc = false;
        try { bnc = await PLUGIN_API['overlay.highlightOption'].run([MOTB_BIANCA_OPTS[motbBianca - 1], 'link a clue to bianca'], myPid()); } catch (e) {}
        if (bnc) { qgClrNpc(); qgClrTiles(); qgClrItem(); return; }
        await qgNpc('#' + MOTB_BIANCA, 'Talk to Bianca');
        return;
      }
      const nBase = await qgInvCount(MOTB_POISON_BASE);
      const nHolly = await qgInvCount(MOTB_HOLLYHOCK);
      const nUnheated = await qgInvCount(MOTB_UNHEATED);
      if ((await qgInvCount(MOTB_HEATED)) >= 1) {
        if (await motbBoxOpt('meat')) { clrBox(); return; }
        qgClrNpc(); qgClrDlg();
        const marks = qgIdMarks(sc.objects, MOTB_DUKEMEAL, "Duke's meal\nUse the potion");
        if (marks.length) qgOv('overlay.guideTiles', marks); else qgClrTiles();
        qgOv('overlay.highlightItem', MOTB_HEATED, "Use on the duke's meal");
        return;
      }
      if (nUnheated >= 1) {
        if (qgIfaceOpen(MOTB_MAKEX_FRAME) && await motbIfaceTextHas(MOTB_MAKEX_CONTENT, MOTB_MAKEX_TITLE, 'poison detection potion')) {
          if (qgIfaceComp(MOTB_MAKEX_CONTENT, MOTB_MAKEX_START, 'Cook')) return;
        }
        if (!(await qgObjectById(MOTB_RANGE, 'Range\nHeat the vial', sc.objects))) qgClearAll();
        return;
      }
      if (nBase >= 1 && nHolly >= 1) {
        if (await motbBoxOpt('leave')) { clrBox(); return; }
        await qgItems([[MOTB_HOLLYHOCK, 'Use the hollyhock on the vial'], [MOTB_POISON_BASE, '']]);
        return;
      }
      if (nBase >= 1) {
        if (await motbBoxOpt('take hollyhock', 'leave')) { clrBox(); return; }
        if (!(await qgObjectById(MOTB_CUPBOARD, 'Supply cupboard\nSearch', sc.objects))) qgClearAll();
        return;
      }
      if (await motbBoxOpt('take a vial')) { clrBox(); return; }
      if (!(await qgObjectById(MOTB_SATCHEL, 'Strange satchel\nTake a vial', sc.objects))) qgClearAll();
      return;
    }
    if (v === 95) {
      if (await motbReenter()) return;
      const pl = qgP ? (qgP.p | 0) : 0;
      if (pl >= 3) { await qgNpc('#' + MOTB_ASTER_P2, 'Talk to Aster'); return; }
      const sc = await qgScene();
      const stair = pl === 0 ? MOTB_STAIRS0 : (pl === 1 ? MOTB_STAIRS1 : MOTB_STAIRS2);
      const label = (pl === 0 ? 'Stone stairs' : 'Regular Stairs') + '\nTop floor';
      if (!(await qgObjectById(stair, label, sc.objects))) qgClearAll();
      return;
    }
    if (v === 100) {
      if (await motbReenter()) return;
      await qgNpc('#' + MOTB_ASSASSIN, 'Attack the assassin');
      return;
    }
    if (v === 105) {
      if (await motbReenter()) return;
      await qgNpc('#' + MOTB_ASTER_P2, 'Talk to Aster');
      return;
    }
    if (v === 110) {
      if (await motbReenter()) return;
      await qgNpc('#' + MOTB_ASSASSIN_DEAD, 'Search the assassin');
      return;
    }
    if (v === 115) {
      if (await motbReenter()) return;
      await qgNpc('#' + MOTB_ASTER_P2, 'Talk to Aster');
      return;
    }
    if (v === 120) {
      if (await motbReenter()) return;
      const pl = qgP ? (qgP.p | 0) : 0;
      if (pl <= 0) {
        if (!motbRoald2) motbRoald2 = qgRand(2) + 1;
        await qgDialogNpc('#29918', 'Talk to King Roald', 0, 0, 0, 'talk about the assassin', MOTB_ROALD2_OPTS[motbRoald2 - 1]);
        return;
      }
      const sc = await qgScene();
      const loc = pl >= 3 ? MOTB_TRAPDOOR : (pl === 2 ? MOTB_STAIRS2 : MOTB_STAIRS1);
      const label = (pl >= 3 ? 'Trap Door' : 'Regular Stairs') + '\nBottom floor';
      if (!(await qgObjectById(loc, label, sc.objects))) qgClearAll();
      return;
    }
    if (v === 160) {
      if (await motbReenter()) return;
      await qgDialogNpc('#' + MOTB_ASTER_P2, 'Talk to Aster', 0, 0, 0, 'i m ready');
      return;
    }
    if (v === 165) {
      if (await motbReenter()) return;
      if (!motbElla165_1) motbElla165_1 = qgRand(3) + 1;
      if (!motbElla165_2) motbElla165_2 = qgRand(3) + 1;
      if (!motbElla165_3) motbElla165_3 = qgRand(3) + 1;
      if (!motbElla165_4) motbElla165_4 = qgRand(3) + 1;
      let e1 = false;
      try { e1 = await PLUGIN_API['overlay.highlightOption'].run([MOTB_ELLA165_1[motbElla165_1 - 1], MOTB_ELLA165_2[motbElla165_2 - 1], MOTB_ELLA165_3[motbElla165_3 - 1], MOTB_ELLA165_4[motbElla165_4 - 1]], myPid()); } catch (e) {}
      if (e1) { qgClrNpc(); qgClrTiles(); qgClrItem(); return; }
      await qgNpc('#' + MOTB_ASTER_P2, 'Talk to Aster');
      return;
    }
    if (v === 170) {
      if (await motbReenter()) return;
      if (qgIfaceComp(MOTB_IDENTIFY_GROUP, MOTB_BIANCA_OPT, 'Bianca Dunnet')) return;
      await qgNpc('#' + MOTB_ASTER_P2, 'Talk to Aster');
      return;
    }
    if (v === 175) {
      if (await motbReenter()) return;
      if (!motbFate175) motbFate175 = qgRand(5) + 1;
      let f = false;
      try { f = await PLUGIN_API['overlay.highlightOption'].run([MOTB_FATE175[motbFate175 - 1]], myPid()); } catch (e) {}
      if (f) { qgClrNpc(); qgClrTiles(); qgClrItem(); return; }
      await qgNpc('#' + MOTB_ASTER_P2, 'Talk to Aster');
      return;
    }
    if (v === 180) {
      if (await motbReenter()) return;
      await qgDialogNpc('#29918', 'Talk to King Roald', 0, 0, 0, 'end the banquet');
      return;
    }
    if (v === 185) {
      if (!motbAster185) motbAster185 = qgRand(2) + 1;
      await qgDialogNpc('#' + MOTB_ASTER, 'Talk to Aster', MOTB_ASTER_X, MOTB_ASTER_Y, 0,
        'talk about quests', 'murder on the border', MOTB_ASTER185[motbAster185 - 1]);
      return;
    }
    if (v === 190) {
      if (MOTB_RODNEY190.length && !motbRodney190) motbRodney190 = qgRand(MOTB_RODNEY190.length) + 1;
      await qgDialogNpc('#' + MOTB_RODNEY_K, 'Talk to Rodney', MOTB_RODNEY_K_X, MOTB_RODNEY_K_Y, 0,
        ...(motbRodney190 ? [MOTB_RODNEY190[motbRodney190 - 1]] : []));
      return;
    }
    if (v === 130) {
      if (await motbReenter()) return;
      const sc = await qgScene(64);
      await motbPrincessSpot(MOTB_PSPOT1, sc, 'Princess spot\nStand here');
      return;
    }
    if (v === 135) {
      if (await motbReenter()) return;
      const sc = await qgScene(64);
      if (!(await qgObjectById(MOTB_MISCITEMS, 'Miscellaneous items\nInvestigate', sc.objects))) qgClearAll();
      return;
    }
    if (v === 140) {
      if (await motbReenter()) return;
      const sc = await qgScene(64);
      await motbPrincessSpot(MOTB_PSPOT2, sc, 'Princess spot\nStand here');
      return;
    }
    if (v === 145) {
      if (await motbReenter()) return;
      const sc = await qgScene(64);
      await motbPrincessSpot(MOTB_PSPOT3, sc, 'Princess spot\nStand here');
      return;
    }
    if (v === 150) {
      if (await motbReenter()) return;
      const sc = await qgScene(64);
      if (!(await qgObjectById(MOTB_COFFER, 'Unearthed coffer\nInvestigate', sc.objects))) qgClearAll();
      return;
    }
    if (v === 155) {
      if (await motbReenter()) return;
      if ((vbm[MOTB_ALBA6_VB] | 0) < 1) {
        if (!motbAlba6) motbAlba6 = qgRand(3) + 1;
        await qgDialogNpc('#' + MOTB_ALBA, 'Talk to Duchess Alba', 0, 0, 0, 'talk to duchess alba', 'beef with king roald', MOTB_ALBA6_OPTS[motbAlba6 - 1], 'that s all for now');
        return;
      }
      if (!motbRodneyAsked && await motbIfaceTextHas(1184, 10, 'ask bianca')) motbRodneyAsked = true;
      if (!motbRodneyAsked && (vbm[MOTB_RODNEY_VB] | 0) < 1) {
        await qgDialogNpc('#' + MOTB_RODNEY, 'Talk to Rodney', 0, 0, 0, 'talk to rodney', 'ask about ellamaria', 'that s all for now');
        return;
      }
      if ((vbm[MOTB_RODNEY_VB] | 0) < 1) {
        if (motbIfaceSubBox(MOTB_LINK_GROUP, MOTB_LINK_CLUE_COMP, MOTB_RODNEY_CLUE_SUB, 'Link this clue')) return;
        let rd = false;
        try { rd = await PLUGIN_API['overlay.highlightOption'].run(['link a clue to rodney'], myPid()); } catch (e) {}
        if (rd) { qgClrNpc(); qgClrTiles(); qgClrItem(); return; }
        await qgNpc('#' + MOTB_RODNEY, 'Talk to Rodney');
        return;
      }
      await qgDialogNpc('#' + MOTB_ELLAMARIA, 'Talk to Queen Ellamaria', 0, 0, 0, 'talk to ellamaria', 'friends with bianca', 'that s all for now');
      return;
    }
    if (v === 125) {
      if (await motbReenter()) return;
      const sc = await qgScene(64);
      if ((vbm[MOTB_WAXSEAL_VB] | 0) < 1) {
        if (!(await qgObjectById(MOTB_WAXSEAL, 'Wax seal\nInvestigate', sc.objects))) qgClearAll();
        return;
      }
      if ((vbm[MOTB_IRIS_VB] | 0) < 1) {
        if (motbIfaceSubBox(MOTB_LINK_GROUP, MOTB_LINK_CLUE_COMP, MOTB_IRIS_CLUE_SUB, 'Link this clue')) return;
        if (!motbIris) motbIris = qgRand(3) + 1;
        let ir = false;
        try { ir = await PLUGIN_API['overlay.highlightOption'].run([MOTB_IRIS_OPTS[motbIris - 1], 'link a clue to iris'], myPid()); } catch (e) {}
        if (ir) { qgClrNpc(); qgClrTiles(); qgClrItem(); return; }
        if (sc.npcs.some(n => n && n.id === MOTB_IRIS)) { await qgNpc('#' + MOTB_IRIS, 'Talk to Iris'); return; }
        const seal = sc.objects.find(o => o && o.id === MOTB_WAXSEAL);
        if (seal) {
          qgClrNpc(); qgClrDlg(); qgClrItem();
          qgOv('overlay.guideTiles', [{ x: seal.x + MOTB_IRIS_DX, y: seal.y + MOTB_IRIS_DY, plane: seal.plane | 0, label: 'Iris\nTalk to (this way)' }]);
          return;
        }
        qgClearAll();
        return;
      }
      if ((vbm[MOTB_PARCHMENT_VB] | 0) < 1) {
        if (!(await qgObjectById(MOTB_PARCHMENT, 'Burnt parchment\nInvestigate', sc.objects))) qgClearAll();
        return;
      }
      if (motbIfaceSubBox(MOTB_LINK_GROUP, MOTB_LINK_CLUE_COMP, MOTB_AMULET_CLUE_SUB, 'Link this clue')) return;
      let pr = false;
      try { pr = await PLUGIN_API['overlay.highlightOption'].run(['link a clue to princess'], myPid()); } catch (e) {}
      if (pr) { qgClrNpc(); qgClrTiles(); qgClrItem(); return; }
      await qgNpc('#' + MOTB_PRINCESS, 'Talk to Princess');
      return;
    }
    qgClearAll();
  }

  const MOTB_MON_VBS = [MOTB_PROG];
  let motbMonVb = null, motbMonBusy = false;
  function motbMonText() {
    if (!motbMonVb) return 'progress vb ' + MOTB_PROG + ' = (reading...)';
    return 'progress vb ' + MOTB_PROG + ' = ' + (motbMonVb[MOTB_PROG] | 0) +
           '   (start 5, complete ' + MOTB_DONE + ')';
  }
  function motbMonRefresh() {
    if (motbMonBusy) return;
    if (!paneVisible('quests') && !paneVisible('questfocus')) return;   // monitor not on screen
    if (!bridge() || typeof PLUGIN_API === 'undefined') return;
    motbMonBusy = true;
    (async () => {
      try {
        const vb = await readVarbitValues(MOTB_MON_VBS);
        const changed = !motbMonVb || MOTB_MON_VBS.some(id => (vb[id] | 0) !== (motbMonVb[id] | 0));
        motbMonVb = vb;
        if (changed) {
          paneRun('questfocus', () => { qgSig = ''; renderQuestFocus(); });
          paneRun('quests', () => { questDetailSig = ''; renderQuests(); });
        }
      } catch (e) {}
      motbMonBusy = false;
    })();
  }
  (function () { function motbMonLoop() { try { motbMonRefresh(); } catch (e) {} setTimeout(motbMonLoop, 1100); } setTimeout(motbMonLoop, 1750); })();

  QG_AUTO['Murder on the Border'] = {
    vbs: [MOTB_PROG, MOTB_HOT1, MOTB_HOT2, MOTB_HOT3, MOTB_KITCHEN_VB, MOTB_GRP_CENTRAL_VB, MOTB_GRP_WEST_VB, MOTB_GRP_UP_VB, MOTB_NUTROAST_VB, MOTB_SATCHEL_VB, MOTB_SIMON_VB, MOTB_BURIEDBOX_VB, MOTB_MEAL_VB, MOTB_KINGROALD_VB, MOTB_ALBA_VB, MOTB_BIANCA_VB, MOTB_P2_ITEM1_VB, MOTB_WAXSEAL_VB, MOTB_IRIS_VB, MOTB_PARCHMENT_VB, MOTB_COFFER_VB, MOTB_ALBA6_VB, MOTB_RODNEY_VB],
    done: (vb) => {
      const v = vb[MOTB_PROG] | 0;
      const s = new Set();
      if (v >= 10) s.add(0);   // Talk to Aster (conversation done -> 10)
      if (v >= 15) s.add(1);   // Talk to Bill about the Kitchen (-> 15)
      if (v > 15 || (v >= 15 && (((vb[MOTB_HOT1] | 0) || (vb[MOTB_HOT2] | 0) || (vb[MOTB_HOT3] | 0)) !== 0))) s.add(2);
      if (v >= 20 || (v >= 15 && (vb[MOTB_KITCHEN_VB] | 0) >= 1)) s.add(3);
      if (v >= 25) s.add(4);   // The banquet: Talk to Aster (-> 25)
      if (v >= 30) s.add(5);   // Click the banquet portal, cutscene starts (-> 30)
      if (v >= 50) s.add(6);
      if (v > 65 || (v >= 55 && (vb[MOTB_GRP_CENTRAL_VB] | 0) >= 1 && (vb[MOTB_GRP_WEST_VB] | 0) >= 1 && (vb[MOTB_GRP_UP_VB] | 0) >= 1)) s.add(7);
      if (v >= 75) s.add(8);   // Aster starts the feast (-> 75, cutscene)
      if (v >= 85) s.add(9);   // Converse with the guests during the feast cutscene (75->80->85)
      if (v >= 90) s.add(10);  // Continue with Aster -> Mystery journal (-> 90, item 54563)
      if ((vb[MOTB_NUTROAST_VB] | 0) >= 1) s.add(11);   // (1/8) Investigate the nut roast (vb 52692 -> 1)
      if ((vb[MOTB_SATCHEL_VB] | 0) >= 1) s.add(12);    // (2/8) Investigate the strange satchel (vb 52693 -> 1)
      if ((vb[MOTB_SIMON_VB] | 0) >= 1) s.add(13);      // (3/8) Talk to Simon + Link the clue (vb 52695 -> 1)
      if ((vb[MOTB_BURIEDBOX_VB] | 0) >= 1) s.add(14);  // (4/8) Investigate the half-buried box (vb 52694 -> 1)
      if ((vb[MOTB_MEAL_VB] | 0) >= 1) s.add(15);       // "Detect the poison" step incl. (5/8) use on the duke's meal (vb 52696 -> 1)
      if ((vb[MOTB_KINGROALD_VB] | 0) >= 1) s.add(16);  // (6/8) Talk to King Roald in the Chapel (vb 52691 -> 1)
      if ((vb[MOTB_ALBA_VB] | 0) >= 1) s.add(17);       // (7/8) Duchess Alba, Link the Nut Roast clue (vb 52697 -> 1)
      if ((vb[MOTB_BIANCA_VB] | 0) >= 1) s.add(18);     // (8/8) Bianca, Link the Stolen Jewellery clue (vb 52698 -> 1)
      if (v >= 95) for (let i = 11; i <= 18; i++) s.add(i);   // Part 1 complete (-> 95); all its steps done regardless of the flags
      if (v >= 100) s.add(19);   // Ascend the Town Hall + talk to Aster (-> 100)
      if (v >= 105) s.add(20);   // Fight the assassin (dead -> 105)
      if (v >= 110) s.add(21);   // Talk to Aster after defeating the assassin (-> 110)
      if (v > 115) s.add(22);    // Search the body THEN talk to Aster: search -> 115, the talk (this v=115 highlight) completes it at the next value
      if ((vb[MOTB_P2_ITEM1_VB] | 0) >= 1) s.add(23);   // Part 2 (1/8) Speak with anyone by the well (vb 52700 -> 1)
      if ((vb[MOTB_WAXSEAL_VB] | 0) >= 1) s.add(24);    // Part 2 (2/8) Investigate the wax seal (vb 52702 -> 1)
      if ((vb[MOTB_IRIS_VB] | 0) >= 1) s.add(25);       // Part 2 (3/8) Speak to Iris + Link the Strange Seal (vb 52703 -> 1)
      if ((vb[MOTB_PARCHMENT_VB] | 0) >= 1) s.add(26);  // Part 2 (4/8) Investigate the burnt parchment (vb 52701 -> 1)
      if ((vb[MOTB_COFFER_VB] | 0) >= 1) s.add(27);     // "Find the unearthed coffer" whole step incl. (5/8) investigate (vb 52704 -> 1)
      if ((vb[MOTB_ALBA6_VB] | 0) >= 1) s.add(28);      // Part 2 (6/8) Talk to Duchess Alba in the Command Centre (vb 52705 -> 1)
      if (motbRodneyAsked || v > 155 || (vb[MOTB_RODNEY_VB] | 0) >= 1) s.add(29);   // [29] Talk to Rodney about Ellamaria: "Ask Bianca." latched, quest past 155, OR the (7/8) Scorched Will (52706) done implies it
      if ((vb[MOTB_RODNEY_VB] | 0) >= 1) s.add(30);     // Part 2 (7/8) Speak to Rodney + Link the Scorched Will (vb 52706 -> 1)
      if (v > 160) s.add(31);   // (8/8) Ellamaria + finish speaking with Aster: Ellamaria -> 52707/160, the Aster "Yes I'm ready" completes it past 160
      if (v >= 170) s.add(32);  // [32] "Talk to anyone in the courtyard" = the v=165 Aster 4-option identify dialogue; done once the quest advances to 170 (identifying Bianca is then the active step)
      if (v >= 180) s.add(33);  // [33] "Identify the murderer as Bianca" incl. the "what should happen to her" flavor pick -> the cutscene advances the quest to 180
      if (v >= 185) s.add(34);  // [34] "Talk to King Roald" ("End the banquet.") -> ending the banquet advances the quest to 185
      if (v >= 190) s.add(35);  // [35] "Talk to Aster in the Town Hall" -> the conversation advances the quest to 190 (varbit 51668 also -> 2 here)
      if (v >= MOTB_DONE) {   // complete -> every step of the generated guide done
        const g = (window.QUEST_GUIDES || {})['Murder on the Border'];
        const total = ((g && g.sections) || []).reduce((n, sec) => n + ((sec.s || []).length), 0);
        for (let i = 0; i < total; i++) s.add(i);
      }
      return s;
    },
  };

Object.assign(window, { motbMonText, motbStep });
})();
