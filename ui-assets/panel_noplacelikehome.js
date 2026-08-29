// RuneToolsX panel: There's No Place Like Home... quest guide (quest 532).
// Spliced inline into client.html at load; IIFE (window exports + registerTab; see the RTX registry in client.html).
(function () {

  // Progress varbit 61434 (quest config js5-2 archive 35 file 532): starts at 5, complete at 100.
  const TNP_PROG = 61434, TNP_DONE = 100;
  // Quest start marker (config startpath coord 2946,3228). Two npc ids carry Alfred's name
  // (14862 / 22768), so match him by NAME with the tile as the out-of-scene fallback.
  const TNP_ALFRED_X = 2946, TNP_ALFRED_Y = 3228;

  // Quick guide text from the wiki (CC BY-NC-SA 3.0); quest_guides.js splices before the panels.
  window.QUEST_GUIDES["There's No Place Like Home..."] = { sections: [{ n: '', r: '', t: 'Getting started', s: [
    'Talk to Alfred Stonemason in Rimmington to learn about Player-owned houses.',
    'Talk to the Estate Agent to receive a plot of land.',
    'Enter the house portal.',
    'Talk to Alfred Stonemason and use the Remove Furniture option in the House Controls interface to clean up leftover items.',
    'Use the house portal to return to Rimmington.',
    'Talk to Alfred Stonemason to receive logs.',
    'Use the sawmill to cut the logs into planks.',
    'Use the furniture workbench to create a crude wooden chair. To interact with the workbench, you may need to bank all items equipped and in your inventory to continue.',
    'Store the crude wooden chair in the furniture storage.',
    'Enter Build Mode and place the crude wooden chair.',
    'Click the chair again to change its position.',
    'Talk to Alfred and enter build mode again.',
    'Click on a doorway to add a room, click again to customise it.',
    'Exit the house portal and talk to Alfred Stonemason.',
    'Quest complete!',
  ] }] };   // 15 steps, flat idx 0-14

  const TNP_AGENT = 4247, TNP_AGENT_X = 2950, TNP_AGENT_Y = 3223;
  const TNP_PORTAL = 139151, TNP_PORTAL_X = 2937, TNP_PORTAL_Y = 3221;
  const TNP_PORTAL_IN = 138057;     // interior exit portal loc, inside the house instance
  const TNP_SAWMILL = 139148, TNP_SAWMILL_X = 2942, TNP_SAWMILL_Y = 3219;
  const TNP_WORKBENCH = 139147, TNP_WORKBENCH_X = 2941, TNP_WORKBENCH_Y = 3229;
  const TNP_STORAGE = 139146, TNP_STORAGE_X = 2939, TNP_STORAGE_Y = 3220;
  const TNP_DOORWAY = 139011;       // Edit it in Build Mode to add a room
  const TNP_ALFRED_HOUSE = 22768;   // Alfred inside the house AND by the Rimmington portal
  const TNP_SUB_WALK = 61435;       // sub-varbit: 1 once you walk up to Alfred in the house
  const TNP_SUB_REMOVE = 61438;     // sub-varbit: leftover-removal count, +1 per item
  const TNP_LEFTOVER_COUNT = 7;
  const TNP_SUB_PLANKS = 61436;     // sub-varbit: planks cut at the sawmill (2 = both)
  let tnpInHouse = false;           // "Enter the house portal" latch
  // Alfred also stands in Rimmington by the portal, so inside = Alfred in scene AND the
  // overworld portal 139151 absent (the interior has its own exit loc, 138057).
  function tnpInside(sc) {
    return sc.npcs.some(n => n && n.id === TNP_ALFRED_HOUSE) &&
           !sc.objects.some(o => o && o.id === TNP_PORTAL);
  }
  // House edit mode = varp 12914 === 1, covering both Remove Furniture and Build Mode. It is
  // unset until the first toggle, but an unset varp reads back its default, never 1.
  const TNP_EDIT_MODE = 12914;
  const TNP_HC_GROUP = 1665, TNP_HC_REMOVE = 25, TNP_HC_BUILD = 29;   // House Controls: 25 = Remove Furniture, 29 = Build Mode toggle
  // Sawmill Make-X window: 1371 = the frame and the open signal; 1370 = the content, which is
  // ALWAYS loaded (so its presence means nothing) and holds Construct at comp 29.
  const TNP_CRAFT_FRAME = 1371, TNP_CRAFT_CONTENT = 1370, TNP_CRAFT_CONSTRUCT = 29;
  // Furniture Construction: 1516 comp 23 is an empty button SLOT (the new-style UI mounts the
  // real button into it, with no "Construct" text node anywhere), so the slot rect gets boxed.
  const TNP_FC_GROUP = 1516, TNP_FC_CONSTRUCT = 23;
  // Furniture Storage 1518 is ALWAYS open, so gate on the fullscreen frame 1514. Layer 19 subs
  // = occupied slots, whose graphic comes from the ITEM id rather than a sprite: match by NAME.
  const TNP_POH_FRAME = 1514, TNP_FS_GROUP = 1518, TNP_FS_SLOTS = 19;
  // Placement sidebar 1512 is always open too (gate on 1514): comp 15 = the cell background,
  // comp 16 = the icon whose +0x188 packs the ITEM id. Match on 16, box the 15 with the same sub.
  const TNP_BM_GROUP = 1512, TNP_BM_CELL = 15, TNP_BM_ICONS = 16;
  const TNP_CHAIR_ITEM = 61880;
  // Furniture catalogue (DBTable master 114 sub 2): varp 12908 = the SELECTED piece's index
  // (261 = crude chair); col 2 = its placed/ghost loc.
  const TNP_SEL_VARP = 12908, TNP_CHAIR_IDX = 261, TNP_CHAIR_LOC = 138056;
  // vb 61191 = furniture placed count; the fresh quest house starts at 2, so >= 3 = chair
  // placed. sel == 261 means a ghost is active, else the only 138056 in scene is the chair.
  const TNP_PLACED_VB = 61191, TNP_PLACED_BASE = 3;
  // varp 12912 = the loc id of the piece being MOVED; varp 12919 = the cursor ghost's anchor
  // packed as plane<<28 | x<<14 | y, the only position source since the ghost is not a scene loc.
  const TNP_MOVING_VARP = 12912, TNP_POS_VARP = 12919;
  // vb 61215 = selected building slot (1 = square room); the 1512 grid re-templates with
  // classic sprites for rooms, icon 16:1 = spr 36117 = square room.
  const TNP_ROOM_VB = 61215, TNP_ROOM_SQUARE = 1, TNP_ROOM_SQUARE_SPR = 36117;
  let tnpBuildAgain = false;        // "enter build mode again" latch
  // vb 61190 = room count; the quest house starts at 1, so >= 2 = the square room was built.
  // The selection varbit 61215 stays 1 past the build, so the count is the only phase signal.
  const TNP_ROOMCOUNT_VB = 61190, TNP_ROOM_BUILT = 2;
  // The 7 leftovers to clear; they only carry a "Remove" option while Remove Furniture is on.
  const TNP_LEFTOVERS = {
    138381: 'Broken chair', 138382: 'Broken stool', 138383: 'Broken table',
    138384: 'Broken round table', 138385: 'Cobweb', 138386: 'Cobweb', 138387: 'Cobweb',
  };
  // Talking to Alfred about removal doesn't move the quest varbit, so that half of step 3 is
  // gated on his dialogue line (group 1184 = NPC chat, comp 10) and latched past the chat.
  const TNP_ALFRED_MSG = 'furniture removal is a very easy process';
  let tnpAlfredTalked = false;
  // tnpNpcSaid strips punctuation, so the needle skips the apostrophe word.
  const TNP_ALFRED_EXPLORE_MSG = 'take a look around inside';
  let tnpExploreSaid = false;
  async function tnpVarp(id) {
    try { const vp = JSON.parse(await bridge().varps(myPid(), String(id))); return (vp && vp[id]) | 0; } catch (e) { return 0; }
  }
  function qgIfaceOpen(group) {   // group present in the live interface table (root comp readable)
    try {
      const cr = JSON.parse(bridge().ifaceCompRects(myPid(), group, '0', 0) || '{}');
      return !!(cr && cr.comps && cr.comps['0']);
    } catch (e) { return false; }
  }
  function tnpBuildSlotBox(itemId, label) {   // box the sidebar cell holding item `itemId`
    if (!qgIfaceOpen(TNP_POH_FRAME)) return false;
    try {
      const d = JSON.parse(bridge().interfaceComps(myPid(), TNP_BM_GROUP, TNP_BM_CELL + ',' + TNP_BM_ICONS) || '{}');
      if (!d || !d.open || !d.hasAbs || !Array.isArray(d.comps)) return false;
      const icon = d.comps.find(c => c && (c.obj | 0) === itemId);
      if (!icon) return false;
      const cell = d.comps.find(c => c && (c.comp | 0) === TNP_BM_CELL && (c.sub | 0) === (icon.sub | 0)) || icon;
      qgClrNpc(); qgClrTiles(); qgClrDlg();
      try { bridge().panelViz(myPid(), cell.x + ',' + cell.y + ',' + cell.w + ',' + cell.h + ',' + label); } catch (e) {}
      return true;
    } catch (e) { return false; }
  }
  function tnpRoomSlotBox(label) {   // box the sidebar's SQUARE ROOM cell (sub 1, icon spr 36117)
    if (!qgIfaceOpen(TNP_POH_FRAME)) return false;
    try {
      const d = JSON.parse(bridge().interfaceComps(myPid(), TNP_BM_GROUP, TNP_BM_CELL + ',' + TNP_BM_ICONS) || '{}');
      if (!d || !d.open || !d.hasAbs || !Array.isArray(d.comps)) return false;
      const icon = d.comps.find(c => c && (c.comp | 0) === TNP_BM_ICONS && (c.sub | 0) === TNP_ROOM_SQUARE && (c.spr | 0) === TNP_ROOM_SQUARE_SPR);
      if (!icon) return false;
      const cell = d.comps.find(c => c && (c.comp | 0) === TNP_BM_CELL && (c.sub | 0) === TNP_ROOM_SQUARE) || icon;
      qgClrNpc(); qgClrTiles(); qgClrDlg();
      try { bridge().panelViz(myPid(), cell.x + ',' + cell.y + ',' + cell.w + ',' + cell.h + ',' + label); } catch (e) {}
      return true;
    } catch (e) { return false; }
  }
  function tnpStorageSlotBox(needle, label) {   // box the Storage cell whose item name contains `needle`
    if (!qgIfaceOpen(TNP_POH_FRAME)) return false;
    try {
      const d = JSON.parse(bridge().interfaceComps(myPid(), TNP_FS_GROUP, String(TNP_FS_SLOTS)) || '{}');
      if (!d || !d.open || !d.hasAbs || !Array.isArray(d.comps)) return false;
      const hit = d.comps.find(c => c && (c.w | 0) > 0 && String(c.text || '').toLowerCase().indexOf(needle) >= 0);
      if (!hit) return false;
      qgClrNpc(); qgClrTiles(); qgClrDlg();
      try { bridge().panelViz(myPid(), hit.x + ',' + hit.y + ',' + hit.w + ',' + hit.h + ',' + label); } catch (e) {}
      return true;
    } catch (e) { return false; }
  }
  async function tnpNpcSaid(needle) {   // true when the open NPC chat message contains `needle`
    try {
      const d = JSON.parse(await bridge().interfaceComps(myPid(), 1184, '10') || '{}');
      if (d && Array.isArray(d.comps)) {
        const norm = s => String(s == null ? '' : s).toLowerCase().replace(/[^a-z0-9 ]+/g, ' ').replace(/\s+/g, ' ').trim();
        return d.comps.some(c => c && norm(c.text).indexOf(needle) >= 0);
      }
    } catch (e) {}
    return false;
  }
  async function tnpStep() {
    let vbm = {}; try { vbm = await readVarbitValues([TNP_PROG, TNP_SUB_WALK, TNP_SUB_REMOVE, TNP_SUB_PLANKS, TNP_PLACED_VB, TNP_ROOM_VB, TNP_ROOMCOUNT_VB]); } catch (e) { return; }
    const v = vbm[TNP_PROG] | 0;
    // The HUD notification belongs only to the v=45 confirm-building state; drop it elsewhere.
    if (hudShown && !(v === 45 && (vbm[TNP_ROOM_VB] | 0) === TNP_ROOM_SQUARE
                      && (vbm[TNP_ROOMCOUNT_VB] | 0) < TNP_ROOM_BUILT)) hudSet(0, '', false);
    if (v >= TNP_DONE) { qgClearAll(); return; }   // 100 = complete: nothing to guide
    if (v === 0) {   // not started -> Alfred in Rimmington (quest start marker tile)
      await qgNpc('Alfred Stonemason', 'Talk to Alfred Stonemason', TNP_ALFRED_X, TNP_ALFRED_Y, 0);
      return;
    }
    if (v === 5) {   // Alfred talked to -> the Estate agent for a plot of land
      await qgNpc('#' + TNP_AGENT, 'Talk to the Estate Agent', TNP_AGENT_X, TNP_AGENT_Y, 0);
      return;
    }
    if (v < 10) tnpInHouse = false;
    if (v !== 10) tnpExploreSaid = false;
    if (v < 45) tnpBuildAgain = false;
    if (v !== 15) tnpAlfredTalked = false;
    if (v === 10) {   // plot received -> the house portal. Inside: walk to the tile that flips
                      // vb 61435 (instance coords, so +1,+6 relative to Alfred), then talk to him.
      const sc = await qgScene();
      if (tnpInside(sc)) {
        tnpInHouse = true;
        const alf = sc.npcs.find(n => n && n.id === TNP_ALFRED_HOUSE);
        // The tile only shows after Alfred's explore prompt; before that, he is the action.
        if ((vbm[TNP_SUB_WALK] | 0) < 1) {
          if (!tnpExploreSaid && (await tnpNpcSaid(TNP_ALFRED_EXPLORE_MSG))) tnpExploreSaid = true;
          if (!tnpExploreSaid) { await qgNpc('#' + TNP_ALFRED_HOUSE, 'Talk to Alfred Stonemason'); return; }
          qgTile(alf.x + 1, alf.y + 6, alf.plane | 0, 'Explore the house\nWalk here');
          return;
        }
        await qgNpc('#' + TNP_ALFRED_HOUSE, 'Talk to Alfred Stonemason');
        return;
      }
      if (!(await qgObjectById(TNP_PORTAL, 'House portal\nEnter house', sc.objects))) qgObject('House portal', 'Enter house', TNP_PORTAL_X, TNP_PORTAL_Y, 0);
      return;
    }
    if (v === 15) {   // explored -> Alfred, then Remove Furniture; outside -> the portal
      // At 7/7 the game auto-teleports back and the varbit moves to 20, so this only covers
      // the transient window before it catches up.
      if ((vbm[TNP_SUB_REMOVE] | 0) >= TNP_LEFTOVER_COUNT) { qgClearAll(); return; }
      const sc = await qgScene(64);
      if (tnpInside(sc)) {
        // Leftovers carry the Remove option only in this mode, and leave the scene as they
        // are cleared, so the marks shrink to none.
        if ((await tnpVarp(TNP_EDIT_MODE)) === 1) {
          const marks = sc.objects.filter(o => o && TNP_LEFTOVERS[o.id])
            .map(o => ({ x: o.x, y: o.y, plane: o.plane | 0, label: TNP_LEFTOVERS[o.id] + '\nRemove' }));
          qgClrNpc(); qgClrDlg(); qgClrItem();
          qgOv('overlay.guideTiles', marks);
          return;
        }
        if (await tnpNpcSaid(TNP_ALFRED_MSG)) tnpAlfredTalked = true;
        // Whenever House Controls is up, its Remove Furniture button IS the action.
        if (qgIfaceComp(TNP_HC_GROUP, TNP_HC_REMOVE, 'Remove Furniture')) return;
        await qgNpc('#' + TNP_ALFRED_HOUSE, 'Talk to Alfred Stonemason');
        return;
      }
      if (!(await qgObjectById(TNP_PORTAL, 'House portal\nEnter house', sc.objects))) qgObject('House portal', 'Enter house', TNP_PORTAL_X, TNP_PORTAL_Y, 0);
      return;
    }
    if (v === 20) {   // auto-teleported back -> Alfred hands over the logs, not the wiki's Plank maker
      await qgNpc('Alfred Stonemason', 'Talk to Alfred Stonemason', TNP_ALFRED_X, TNP_ALFRED_Y, 0);
      return;
    }
    if (v === 25) {   // logs received -> the sawmill, Cut planks; crafting window up -> Construct
      // Both planks cut: Alfred's dialogue auto-continues while the varbit catches up to 30.
      if ((vbm[TNP_SUB_PLANKS] | 0) >= 2) {
        await qgNpc('Alfred Stonemason', 'Talk to Alfred Stonemason', TNP_ALFRED_X, TNP_ALFRED_Y, 0);
        return;
      }
      if (qgIfaceOpen(TNP_CRAFT_FRAME) && qgIfaceComp(TNP_CRAFT_CONTENT, TNP_CRAFT_CONSTRUCT, 'Construct')) return;
      const sc = await qgScene();
      if (!(await qgObjectById(TNP_SAWMILL, 'Sawmill\nCut planks', sc.objects))) qgObject('Sawmill', 'Cut planks', TNP_SAWMILL_X, TNP_SAWMILL_Y, 0);
      return;
    }
    if (v === 30) {   // planks cut -> the Furniture workbench, Construct; its window's button
                      // slot when open (1516 opens/closes with the window, so no extra gate)
      if (qgIfaceComp(TNP_FC_GROUP, TNP_FC_CONSTRUCT, 'Construct')) return;
      const sc = await qgScene();
      if (!(await qgObjectById(TNP_WORKBENCH, 'Furniture workbench\nConstruct', sc.objects))) qgObject('Furniture workbench', 'Construct', TNP_WORKBENCH_X, TNP_WORKBENCH_Y, 0);
      return;
    }
    if (v === 35) {   // chair crafted -> the Furniture storage, Deposit all (the Alfred talk
                      // here is optional); storage window open -> box the chair's own cell
      if (tnpStorageSlotBox('crude wooden chair', 'Deposit')) return;
      const sc = await qgScene();
      if (!(await qgObjectById(TNP_STORAGE, 'Furniture storage\nDeposit all', sc.objects))) qgObject('Furniture storage', 'Deposit all', TNP_STORAGE_X, TNP_STORAGE_Y, 0);
      return;
    }
    if (v === 40) {   // chair deposited -> the house portal; inside -> Alfred, then Build Mode
      const sc = await qgScene();
      if (!sc.objects.some(o => o && o.id === TNP_PORTAL)) {
        // Ghost riding the cursor (sel varp = 261, or move varp = the chair's loc) -> tile at
        // the varp-12919 anchor; placed -> the scene 138056; neither -> the sidebar cell.
        if ((await tnpVarp(TNP_EDIT_MODE)) === 1) {
          const moving = (await tnpVarp(TNP_MOVING_VARP)) === TNP_CHAIR_LOC;
          if (moving || (await tnpVarp(TNP_SEL_VARP)) === TNP_CHAIR_IDX) {
            const pc = await tnpVarp(TNP_POS_VARP);
            if (pc > 0) { qgTile((pc >> 14) & 0x3FFF, pc & 0x3FFF, (pc >> 28) & 3, 'Crude wooden chair\nClick to place'); return; }
            qgClearAll();
            return;
          }
          if ((vbm[TNP_PLACED_VB] | 0) >= TNP_PLACED_BASE) {
            if (!(await qgObjectById(TNP_CHAIR_LOC, 'Crude wooden chair\nMove', sc.objects))) qgClearAll();
            return;
          }
          if (!tnpBuildSlotBox(TNP_CHAIR_ITEM, 'Crude wooden chair')) qgClearAll();
          return;
        }
        // Build Mode off -> the House Controls toggle when open, else Alfred.
        if (qgIfaceComp(TNP_HC_GROUP, TNP_HC_BUILD, 'Build Mode')) return;
        await qgNpc('#' + TNP_ALFRED_HOUSE, 'Talk to Alfred Stonemason');
        return;
      }
      if (!(await qgObjectById(TNP_PORTAL, 'House portal\nEnter house', sc.objects))) qgObject('House portal', 'Enter house', TNP_PORTAL_X, TNP_PORTAL_Y, 0);
      return;
    }
    if (v === 45) {   // chair placed + repositioned -> Alfred, Build Mode again, then Edit the
                      // doorway. All in-house: outside -> back through the portal.
      const sc = await qgScene();
      if (sc.objects.some(o => o && o.id === TNP_PORTAL)) {
        if (!(await qgObjectById(TNP_PORTAL, 'House portal\nEnter house', sc.objects))) qgObject('House portal', 'Enter house', TNP_PORTAL_X, TNP_PORTAL_Y, 0);
        return;
      }
      // The doorway's Edit opens a dialogue whose options depend on state ("Add a room."
      // before the room exists, "Customise this doorway." after); they are mutually exclusive.
      if ((await tnpVarp(TNP_EDIT_MODE)) === 1) {
        tnpBuildAgain = true;
        // One highlightOption read for both needles: per-needle qgDialogOpt calls
        // clear-then-redraw every poll and flash the boxes.
        let boxed = false;
        try { boxed = await PLUGIN_API['overlay.highlightOption'].run(['add a room', 'customise this doorway', 'a wall'], myPid()); } catch (e) {}
        if (boxed) { qgClrNpc(); qgClrTiles(); qgClrItem(); return; }
        // Room built wins over everything: the selection varbit stays 1 past the build.
        if ((vbm[TNP_ROOMCOUNT_VB] | 0) >= TNP_ROOM_BUILT) {
          if (!(await qgObjectById(TNP_DOORWAY, 'Doorway\nEdit', sc.objects))) qgClearAll();
          return;
        }
        // Square room selected -> HUD text (36121 = the quest's journal icon sprite).
        if ((vbm[TNP_ROOM_VB] | 0) === TNP_ROOM_SQUARE) {
          qgClrNpc(); qgClrTiles(); qgClrDlg(); qgClrItem();
          hudSet(36121, 'Click to confirm building the room', true);
          return;
        }
        // Neither: the square-room sidebar cell, doorway Edit as the opener fallback.
        if (tnpRoomSlotBox('Square room')) return;
        if (!(await qgObjectById(TNP_DOORWAY, 'Doorway\nEdit', sc.objects))) qgClearAll();
        return;
      }
      // Build Mode off -> the toggle when House Controls is open, else Alfred (id-match only).
      if (qgIfaceComp(TNP_HC_GROUP, TNP_HC_BUILD, 'Build Mode')) return;
      await qgNpc('#' + TNP_ALFRED_HOUSE, 'Talk to Alfred Stonemason');
      return;
    }
    if (v === 50) {   // room added + doorway customised -> exit the house; outside -> Alfred
      const sc = await qgScene();
      if (sc.objects.some(o => o && o.id === TNP_PORTAL)) {
        await qgNpc('Alfred Stonemason', 'Talk to Alfred Stonemason', TNP_ALFRED_X, TNP_ALFRED_Y, 0);
        return;
      }
      if (!(await qgObjectById(TNP_PORTAL_IN, 'House portal\nExit', sc.objects))) qgClearAll();
      return;
    }
    if (v === 90) {   // final talk under way (50 -> 90 mid-dialogue) -> keep Alfred highlighted
      await qgNpc('Alfred Stonemason', 'Talk to Alfred Stonemason', TNP_ALFRED_X, TNP_ALFRED_Y, 0);
      return;
    }
    qgClearAll();
  }

  // Live monitor: shows the progress varbits above the walkthrough.
  const TNP_MON_VBS = [TNP_PROG, TNP_SUB_WALK, TNP_SUB_REMOVE, TNP_SUB_PLANKS];
  let tnpMonVb = null, tnpMonBusy = false;
  function tnpMonText() {
    if (!tnpMonVb) return 'progress vb ' + TNP_PROG + ' = (reading...)';
    return 'progress vb ' + TNP_PROG + ' = ' + (tnpMonVb[TNP_PROG] | 0) +
           '   (start 5, complete ' + TNP_DONE + ')\n' +
           'sub vb ' + TNP_SUB_WALK + ' = ' + (tnpMonVb[TNP_SUB_WALK] | 0) + '   (walked to Alfred in the house)\n' +
           'sub vb ' + TNP_SUB_REMOVE + ' = ' + (tnpMonVb[TNP_SUB_REMOVE] | 0) + ' / ' + TNP_LEFTOVER_COUNT + '   (leftover items removed)\n' +
           'sub vb ' + TNP_SUB_PLANKS + ' = ' + (tnpMonVb[TNP_SUB_PLANKS] | 0) + '   (planks cut at the sawmill)';
  }
  function tnpMonRefresh() {
    if (tnpMonBusy) return;
    if (!paneVisible('quests') && !paneVisible('questfocus')) return;   // monitor not on screen
    if (!bridge() || typeof PLUGIN_API === 'undefined') return;
    tnpMonBusy = true;
    (async () => {
      try {
        const vb = await readVarbitValues(TNP_MON_VBS);
        const changed = !tnpMonVb || TNP_MON_VBS.some(id => (vb[id] | 0) !== (tnpMonVb[id] | 0));
        tnpMonVb = vb;
        if (changed) {
          paneRun('questfocus', () => { qgSig = ''; renderQuestFocus(); });
          paneRun('quests', () => { questDetailSig = ''; renderQuests(); });
        }
      } catch (e) {}
      tnpMonBusy = false;
    })();
  }
  (function () { function tnpMonLoop() { try { tnpMonRefresh(); } catch (e) {} setTimeout(tnpMonLoop, 1100); } setTimeout(tnpMonLoop, 1750); })();

  // Auto-complete the quick-guide checkboxes from TNP_PROG thresholds. Flat idx 0-14.
  QG_AUTO["There's No Place Like Home..."] = {
    vbs: [TNP_PROG, TNP_SUB_WALK, TNP_SUB_REMOVE, TNP_SUB_PLANKS],
    done: (vb) => {
      const v = vb[TNP_PROG] | 0;
      const s = new Set();
      if (v >= 5) s.add(0);
      if (v >= 10) s.add(1);
      // vb 61435 flips on the walk-up inside; the scene latch covers the moment before.
      if (v > 10 || (vb[TNP_SUB_WALK] | 0) >= 1 || tnpInHouse) s.add(2);
      // Steps 3+4: 7/7 of vb 61438 finishes the removal and auto-teleports the player back,
      // so both tick together and neither is done by merely talking or toggling the mode.
      if (v > 15 || (vb[TNP_SUB_REMOVE] | 0) >= TNP_LEFTOVER_COUNT) { s.add(3); s.add(4); }
      if (v >= 25) s.add(5);
      // vb 61436 = 2 covers the window before the quest varbit catches up.
      if (v >= 30 || (vb[TNP_SUB_PLANKS] | 0) >= 2) s.add(6);
      if (v >= 35) s.add(7);
      if (v >= 40) s.add(8);
      if (v >= 45) { s.add(9); s.add(10); }   // the build-mode sequence lands as one transition
      if (v >= 50 || tnpBuildAgain) s.add(11);
      if (v >= 50) s.add(12);
      if (v >= TNP_DONE) for (let i = 0; i <= 14; i++) s.add(i);
      return s;
    },
  };

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { qgIfaceOpen, tnpMonText, tnpStep });
})();
