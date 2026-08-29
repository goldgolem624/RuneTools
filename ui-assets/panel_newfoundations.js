// RuneToolsX panel: New Foundations quest guide (quest 489).
// Spliced inline into client.html at load; IIFE (window exports + registerTab; see the RTX registry in client.html).
(function () {

  // Progress varbit 52592 (quest config js5-2 archive 35 file 489): starts at 5, complete at 65.
  const NF_PROG = 52592, NF_DONE = 65;
  const NF_TITLE = 52593;   // chosen quest-reward title: 1 Duke / 2 Duchess / 3 Dux (0 = not chosen yet)
  const NF_FORT = 52527, NF_PERFECT = 52528;   // 52527 = fortifications-phase tracker (= 4 at v=55); 52528 = Perfect Build % (0-100): rises when building at the OPTIMAL hotspot, drops at a non-optimal one; +1% Fort construction XP per 5%
  const NF_SPIKES = Array.from({ length: 40 }, (_, i) => 52558 + i);   // per-spot spike varbits 52558-52597: 0 = not in progress, 2 = OPTIMAL spot, 1 = non-optimal
  // Fortification build-spot map (spike varbit -> tile); optimal spot = varbit 2, readable out of scan range.
  const NF_FORT_SPOTS = [
    // North wall section 1
    { vb: 52563, x: 3292, y: 3575 },
    { vb: 52564, x: 3299, y: 3575 },
    { vb: 52565, x: 3306, y: 3575 },
    // North wall section 2
    { vb: 52566, x: 3312, y: 3575 },
    { vb: 52567, x: 3318, y: 3575 },
    { vb: 52568, x: 3325, y: 3575 },
    // North-west wall
    { vb: 52561, x: 3277, y: 3567 },
    { vb: 52562, x: 3280, y: 3573 },
    // South-west wall
    { vb: 52580, x: 3277, y: 3542 },
    { vb: 52581, x: 3285, y: 3534 },
    { vb: 52582, x: 3284, y: 3537 },
    // South wall section 1
    { vb: 52576, x: 3317, y: 3532 },
    { vb: 52577, x: 3309, y: 3532 },
    // South wall section 2
    { vb: 52578, x: 3295, y: 3532 },
    { vb: 52579, x: 3293, y: 3532 },
    // North-east wall
    { vb: 52569, x: 3334, y: 3567 },
    { vb: 52570, x: 3331, y: 3571 },
    // East wall
    { vb: 52571, x: 3334, y: 3566 },
    { vb: 52572, x: 3334, y: 3553 },
    // South-east wall
    { vb: 52573, x: 3332, y: 3542 },
    { vb: 52574, x: 3328, y: 3538 },
    { vb: 52575, x: 3325, y: 3535 },
    // Rear gate
    { vb: 52583, x: 3283, y: 3574 },
    { vb: 52584, x: 3289, y: 3574 },
    // Front gate
    { vb: 52585, x: 3307, y: 3532 },
    { vb: 52586, x: 3300, y: 3532 },
    // West wall
    { vb: 52559, x: 3275, y: 3545 },
    { vb: 52560, x: 3275, y: 3565 },
  ];
  // Workshop build-spot map (spike varbits 52535-52540 -> tile); optimal spot = varbit 2.
  const NF_WS_SPOTS = [
    { vb: 52535, x: 3284, y: 3554 },
    { vb: 52536, x: 3284, y: 3556 },
    { vb: 52537, x: 3284, y: 3549 },
    { vb: 52538, x: 3284, y: 3561 },
    { vb: 52539, x: 3276, y: 3557 },
    { vb: 52540, x: 3276, y: 3553 },
  ];
  const NF_SEG = 54460, NF_PLANK = 960;   // stone wall segment / plank item ids
  const NF_BANK = 125239, NF_BANK_X = 3283, NF_BANK_Y = 3555;   // Bank chest at the fort (withdraw materials)
  const NF_MATS = {                        // wiki idx -> { seg: stone wall segments, plank: planks }
    17: { seg: 6, plank: 10 },   // Front gate
    18: { seg: 6, plank: 10 },   // Rear gate
    19: { seg: 10, plank: 0 },   // West wall
    20: { seg: 5, plank: 0 },    // North-west wall
    21: { seg: 10, plank: 0 },   // North wall section 1
    22: { seg: 10, plank: 0 },   // North wall section 2
    23: { seg: 5, plank: 0 },    // North-east wall
    24: { seg: 10, plank: 0 },   // East wall
    25: { seg: 5, plank: 0 },    // South-east wall
    26: { seg: 5, plank: 0 },    // South wall section 1
    27: { seg: 5, plank: 0 },    // South wall section 2
    28: { seg: 5, plank: 0 },    // South-west wall
  };
  function nfMatShort(need, seg, plank) {
    const parts = [];
    if (seg < need.seg) parts.push((need.seg - seg) + ' stone wall segment' + (need.seg - seg === 1 ? '' : 's'));
    if (need.plank && plank < need.plank) parts.push((need.plank - plank) + ' plank' + (need.plank - plank === 1 ? '' : 's'));
    return parts.join(', ');
  }

  // Fort "built" markers: marker varbit = 52489 + wiki idx.
  const NF_FORT_DONE = {
    17: 52506,
    18: 52507,
    19: 52508,
    20: 52509,
    21: 52510,
    22: 52511,
    23: 52512,
    24: 52513,
    25: 52514,
    26: 52515,
    27: 52516,
    28: 52517,
  };
  const NF_PORTAL = 125127;   // New Foundations portal, outside King Roald's throne room in Varrock Palace (action Start Quest)
  const NF_PORTAL_X = 3218, NF_PORTAL_Y = 3473;   // loc map-origin tile; guideMarks boxes the footprint AABB from it
  const NF_ROALD = 29818;
  const NF_PORTAL2 = 125130, NF_PORTAL2_X = 3304, NF_PORTAL2_Y = 3524;   // Fort Forinthry "Continue Quest" portal NE of Varrock (v=15, after leaving the palace instance)
  const NF_ZOMBIE = 29829;
  const NF_BILL = 29822;      // Bill the architect, inside the Fort instance
  const NF_BILL2 = 29806, NF_BILL2_X = 3286, NF_BILL2_Y = 3557;   // Bill at the Workshop (overworld morph)
  const NF_ASTER = 29824, NF_ASTER_X = 3216, NF_ASTER_Y = 3405;   // Aster, outside the Blue Moon Inn in Varrock (Recruiting help)
  const NF_SIV = 29826, NF_SIV_X = 3081, NF_SIV_Y = 3421;         // Overseer Siv, centre of Gunnarsgrunn (Barbarian Village)
  const NF_FLINT = 29828, NF_FLINT_X = 3082, NF_FLINT_Y = 3249;   // Father Flint, Draynor Village marketplace
  const NF_HOTSPOT_NAME = 'Optimal Construction hotspot';   // the optimal hotspot's loc id changes as the flag moves between spots, so match by NAME
  const NF_BP = 125059, NF_BP_X = 3287, NF_BP_Y = 3555;     // Fort Forinthry blueprints table (also opens the Construction menu)
  // v=15 phase latches: set in nfStep, read by QG_AUTO, reset when v<15.
  let nfNearPortal2 = false;    // step 3: player came within 10 tiles of the portal 3304,3524
  let nfInFortInstance = false; // step 4: an Armoured zombie was seen in scene (= inside the instance)
  let nfHotspotSeen = false;    // steps 7-8: the Optimal Construction hotspot was seen in scene
  let nfTitlePick = null;       // random "Choose your title" pick, latched so the box does not jump
  // Other channels are cleared only on a hit, so the underlying highlight does not flicker.
  async function nfBoxOpt(opt) {
    let b = false; try { b = await PLUGIN_API['overlay.highlightOption'].run([opt], myPid()); } catch (e) {}
    if (b) { qgClrNpc(); qgClrTiles(); qgClrItem(); }
    return b;
  }
  async function nfBuildStep(sc) {
    if (qgIfaceComp(1370, 29, 'Start blueprint')) return;
    const opt = sc.objects.filter(o => o && o.name === NF_HOTSPOT_NAME);
    if (opt.length) {
      qgClrNpc(); qgClrDlg(); qgClrItem();
      qgOv('overlay.guideTiles', opt.map(o => ({ x: o.x, y: o.y, plane: o.plane | 0, label: NF_HOTSPOT_NAME + '\nBuild' })));
      return;
    }
    if (!(await qgObjectById(NF_BP, 'Fort Forinthry blueprints\nBuild', sc.objects)))
      qgObject('Fort Forinthry blueprints', 'Build', NF_BP_X, NF_BP_Y, 0);
  }
  async function nfStep() {
    let vbm = {}; try { vbm = await readVarbitValues([NF_PROG]); } catch (e) { return; }
    const v = vbm[NF_PROG] | 0;
    if (v < 15) { nfNearPortal2 = false; nfInFortInstance = false; }
    if (v < 25) nfHotspotSeen = false;
    if (v >= NF_DONE) { qgClearAll(); return; }
    // 64 = the reader's max scan radius; the optimal hotspot roams the whole fort perimeter.
    const sc = await qgScene(64);
    const zombie = sc.npcs.some(n => n && n.id === NF_ZOMBIE);   // Armoured zombies and Bill only exist inside the Fort instance
    const bill = sc.npcs.some(n => n && n.id === NF_BILL);
    if (zombie || bill) nfInFortInstance = true;
    if (v >= 25 && sc.objects.some(o => o && o.name === NF_HOTSPOT_NAME)) nfHotspotSeen = true;
    if (v >= 15 && qgP && (qgP.p | 0) === 0 &&
        Math.abs(qgP.x - NF_PORTAL2_X) <= 10 && Math.abs(qgP.y - NF_PORTAL2_Y) <= 10) nfNearPortal2 = true;
    // King Roald in scene is the reliable in-instance signal: the x>=6400 check fails here, and the varbit sits at 0 through his whole conversation.
    if (sc.npcs.some(n => n && n.id === NF_ROALD)) {
      if (await nfBoxOpt(v >= 10 ? 'sign me up' : 'accept')) return;
      await qgNpc('#' + NF_ROALD, 'Talk to King Roald');
      return;
    }
    if (zombie) { await qgNpc('#' + NF_ZOMBIE, 'Defeat the Armoured zombies'); return; }
    if (v === 20 && bill) { await qgNpc('#' + NF_BILL, 'Talk to Bill'); return; }
    if (v <= 10) {
      if (await nfBoxOpt('yes')) return;
      qgObject('New Foundations', v === 0 ? 'Start Quest' : 'Enter to continue New Foundations',
               NF_PORTAL_X, NF_PORTAL_Y, 0);
      return;
    }
    if (v >= 15 && v <= 20) {
      if (await nfBoxOpt('yes')) return;
      qgObject('New Foundations', 'Continue Quest', NF_PORTAL2_X, NF_PORTAL2_Y, 0);
      return;
    }
    if (v === 25) {
      if (qgIfaceComp(1370, 29, 'Start blueprint')) return;
      let ws = {}; try { ws = await readVarbitValues(NF_WS_SPOTS.map(s => s.vb)); } catch (e) {}
      const opt = NF_WS_SPOTS.filter(s => (ws[s.vb] | 0) === 2);
      if (opt.length) {
        qgClrNpc(); qgClrDlg(); qgClrItem();
        qgOv('overlay.guideTiles', opt.map(s => ({ x: s.x, y: s.y, plane: 0, label: 'Optimal Construction hotspot\nBuild' })));
        return;
      }
      await nfBuildStep(sc);
      return;
    }
    if (v === 30) { await qgNpc('#' + NF_BILL2, 'Talk to Bill', NF_BILL2_X, NF_BILL2_Y, 0); return; }
    if (v === 35) { await qgNpc('#' + NF_ASTER, 'Talk to Aster (Varrock)', NF_ASTER_X, NF_ASTER_Y, 0); return; }
    if (v === 40) { await qgNpc('#' + NF_SIV, 'Talk to Overseer Siv (Barbarian Village)', NF_SIV_X, NF_SIV_Y, 0); return; }
    if (v === 45) { await qgNpc('#' + NF_FLINT, 'Talk to Father Flint (Draynor)', NF_FLINT_X, NF_FLINT_Y, 0); return; }
    if (v === 50) { await qgNpc('#' + NF_BILL2, 'Talk to Bill', NF_BILL2_X, NF_BILL2_Y, 0); return; }
    // v=55: build the fortifications at the optimal spot; unmapped spots fall back to nfBuildStep.
    if (v === 55) {
      if (qgIfaceComp(1370, 29, 'Start blueprint')) return;
      let spikes = {}; try { spikes = await readVarbitValues(NF_FORT_SPOTS.map(s => s.vb)); } catch (e) {}
      const optimal = NF_FORT_SPOTS.filter(s => (spikes[s.vb] | 0) === 2);
      if (optimal.length) {
        // The piece's materials were already spent at Start blueprint -> no material check here.
        qgClrNpc(); qgClrDlg(); qgClrItem();
        qgOv('overlay.guideTiles', optimal.map(s => ({ x: s.x, y: s.y, plane: 0, label: 'Optimal Construction hotspot\nBuild' })));
        return;
      }
      // No hotspot up -> the next piece starts at the blueprints, where materials ARE consumed.
      let seg = 0, plank = 0, done = {}, bankSeg = 0, bankPlank = 0;
      try { seg = await qgInvCount(NF_SEG); plank = await qgInvCount(NF_PLANK); } catch (e) {}
      try { done = await readVarbitValues(Object.values(NF_FORT_DONE)); } catch (e) {}
      try {
        const bd = JSON.parse(await bridge().bankItems(myPid()));   // cached bank -> works even when closed
        if (bd && Array.isArray(bd.items)) for (const it of bd.items) {
          if (it[1] === NF_SEG) bankSeg += (it[2] > 0 ? it[2] : 1);
          else if (it[1] === NF_PLANK) bankPlank += (it[2] > 0 ? it[2] : 1);
        }
      } catch (e) {}
      let canNow = false, withBank = null;
      for (const idx in NF_MATS) {
        if ((done[NF_FORT_DONE[idx]] | 0) === 1) continue;
        const need = NF_MATS[idx], np = need.plank || 0;
        if (seg >= need.seg && plank >= np) { canNow = true; break; }
        const shortSeg = Math.max(0, need.seg - seg), shortPlank = Math.max(0, np - plank);
        if (bankSeg >= shortSeg && bankPlank >= shortPlank) {
          const missing = shortSeg + shortPlank;
          if (!withBank || missing < withBank.missing) withBank = { need, missing };
        }
      }
      if (!canNow && withBank) {
        const label = 'Withdraw ' + nfMatShort(withBank.need, seg, plank);
        if (!(await qgObjectById(NF_BANK, 'Bank chest\n' + label, sc.objects))) qgObject('Bank chest', label, NF_BANK_X, NF_BANK_Y, 0);
        return;
      }
      await nfBuildStep(sc);
      return;
    }
    if (v === 60) {
      let title = 0; try { const tv = await readVarbitValues([NF_TITLE]); title = tv[NF_TITLE] | 0; } catch (e) {}
      if (title === 0) {
        if (nfTitlePick === null) { const t = ['duke', 'duchess', 'dux']; nfTitlePick = t[Math.floor(Math.random() * t.length)]; }
        if (await nfBoxOpt(nfTitlePick)) return;
      }
      // Bill's npc id changes with his morph -> match by name or a known id.
      const b = sc.npcs.find(n => n && (n.name === 'Bill' || n.id === NF_BILL2 || n.id === NF_BILL));
      if (b) { await qgNpc('#' + b.id, 'Talk to Bill'); return; }
      // The 300-tile gate marks his world tile only in the fort overworld, not from the finishing-up instance.
      if (qgP && Math.abs(qgP.x - NF_BILL2_X) <= 300 && Math.abs(qgP.y - NF_BILL2_Y) <= 300) {
        qgTile(NF_BILL2_X, NF_BILL2_Y, 0, 'Talk to Bill');
        return;
      }
      qgClearAll();
      return;
    }
    qgClearAll();
  }
  const NF_MON_VBS = [NF_PROG, NF_FORT, NF_PERFECT].concat(NF_SPIKES);
  let nfMonVb = null, nfMonBusy = false;
  function nfMonText() {
    if (!nfMonVb) return 'progress vb ' + NF_PROG + ' = (reading...)';
    const lines = [
      'progress vb ' + NF_PROG + ' = ' + (nfMonVb[NF_PROG] | 0) + '   (start 5, complete ' + NF_DONE + ')',
      'sub vb ' + NF_FORT + ' = ' + (nfMonVb[NF_FORT] | 0),
      'Perfect Build (' + NF_PERFECT + ') = ' + (nfMonVb[NF_PERFECT] | 0) + '%',
    ];
    // Non-zero spikes only (id=value; 2 = optimal, 1 = non-optimal).
    const active = NF_SPIKES.filter(id => (nfMonVb[id] | 0) !== 0).map(id => id + '=' + (nfMonVb[id] | 0));
    lines.push('spikes ' + NF_SPIKES[0] + '-' + NF_SPIKES[NF_SPIKES.length - 1] + ': ' +
               (active.length ? active.join(' ') : '(all 0)'));
    return lines.join('\n');
  }
  function nfMonRefresh() {
    if (nfMonBusy) return;
    if (!paneVisible('quests') && !paneVisible('questfocus')) return;
    if (!bridge() || typeof PLUGIN_API === 'undefined') return;
    nfMonBusy = true;
    (async () => {
      try {
        const vb = await readVarbitValues(NF_MON_VBS);
        const changed = !nfMonVb || NF_MON_VBS.some(id => (vb[id] | 0) !== (nfMonVb[id] | 0));
        nfMonVb = vb;
        if (changed) {
          paneRun('questfocus', () => { qgSig = ''; renderQuestFocus(); });
          paneRun('quests', () => { questDetailSig = ''; renderQuests(); });
        }
      } catch (e) {}
      nfMonBusy = false;
    })();
  }
  (function () { function nfMonLoop() { try { nfMonRefresh(); } catch (e) {} setTimeout(nfMonLoop, 1100); } setTimeout(nfMonLoop, 1650); })();
  // Wiki flat step idx (section order): The beginning 0-2, Saving Bill 3-7, Building the Workshop
  // 8-10, Recruiting help 11-14, Building the fortifications 15-16, pieces 17-28, Finishing up 29-30.
  QG_AUTO['New Foundations'] = {
    vbs: [NF_PROG].concat(Object.values(NF_FORT_DONE)),
    done: (vb) => {
      const v = vb[NF_PROG] | 0;
      const s = new Set();
      if (v >= 5) s.add(0);    // Enter the New Foundations portal {1 Yes}
      if (v >= 10) s.add(1);   // Talk to King Roald {Accept} -> cutscene
      if (v >= 15) s.add(2);   // After the cutscene, talk with King Roald {2 Sign me up!}
      if (v > 15 || nfNearPortal2) s.add(3);      // Travel NE to the Fort Forinthry portal
      if (v > 15 || nfInFortInstance) s.add(4);   // Click the portal to Continue New Foundations {Yes}
      if (v >= 20) s.add(5);   // Defeat the Armoured zombies
      if (v >= 25) s.add(6);   // Talk to Bill
      if (v > 25 || nfHotspotSeen) { s.add(7); s.add(8); }   // Go SW to the blueprints + Check plans & start building
      if (v >= 30) s.add(9);   // Build the Workshop by interacting with the hotspots
      if (v >= 35) s.add(10);  // Talk to Bill
      if (v >= 40) s.add(11);  // Talk to Aster at the Blue Moon Inn
      if (v >= 45) s.add(12);  // Talk to Overseer Siv at Barbarian Village
      if (v >= 50) s.add(13);  // Talk to Father Flint at Draynor
      if (v >= 55) s.add(14);  // Head back to the fort and speak to Bill
      if (v >= 55) { s.add(15); s.add(16); }   // Proceed to build the fortifications + Find the hotspot
      for (const idx in NF_FORT_DONE) { if ((vb[NF_FORT_DONE[idx]] | 0) === 1) s.add(+idx); }   // each piece via its "built" marker
      if (v > 55) { for (let i = 17; i <= 28; i++) s.add(i); }   // off 55 -> whole fortifications section done
      if (v >= NF_DONE) { s.add(29); s.add(30); }   // Finishing up: talk to Bill + Quest complete!
      return s;
    },
  };

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { nfMonText, nfStep });
})();
