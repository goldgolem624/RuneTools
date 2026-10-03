// RuneToolsX panel: Death Plateau quest guide (quest 140).
(function () {

  // Guide text: the quick guide with the cavern route split into its obstacles (sub-points of step 12).
  window.QUEST_GUIDES = window.QUEST_GUIDES || {};
  window.QUEST_GUIDES['Death Plateau'] = { sections: [
    { n: '', r: '', t: 'Getting started', s: [
      "Talk to Commander Denulth who is located north east of the Burthorpe Lodestone, south of the agility course. {Chat: 1 Do you have any quests for me? > Accept} (The quest icon can be hidden by the block of soldiers he's south of.)",
    ] },
    { n: '1 Free inventory space', r: '', t: 'The dwarves', s: [
      'Head west past the castle and through a defensive wall.',
      'Go north (past troll invasion) and into a cave.',
      "Talk to Sabbot. {Chat: 1 I've been sent to look for a route to Death Plateau. Can you help?}",
      'Exit the cave and follow the path to the west.',
      'At the intersection, head west and then go south and into a house.',
      'Talk to Freda.',
    ] },
    { n: '', r: 'A weapon', t: 'Boots and routes', s: [
      'Home Teleport to Burthorpe.',
      'Speak to Dunstan outside the house to the south-east of Denulth to add spikes to the climbing boots. {Chat: 1 Can you put some fresh spikes on these climbing boots for me?}',
      'Bring the spiked boots back to Freda.',
      'Read the Survey that is received.',
      "Go back to Sabbot's cave and mine the wall west of him.",
      "Proceed through the cavern's obstacles and climb the cliffside at the end.\n  - Cliffside: jump down.\n  - Gap: squeeze through.\n  - Crevice: jump.\n  - Rope swing: swing on.\n  - Stepping stones: jump.\n  - Second gap: squeeze through.\n  - Cliffside at the end: climb.",
      'Exit the cave through the cave exit.',
      'The Map will approach the player. {Chat: 3 Prepare to die, troll!}',
      'Kill The Map.\n  - The Map will not be counted as killed if you are in a group.',
      'Home Teleport to Burthorpe.',
    ] },
    { n: '', r: '', t: 'Finishing up', s: [
      'Talk to Commander Denulth.',
      'Watch or skip the cutscene.',
      'Quest complete!',
    ] },
  ] };

  // Progress varbit 10848 (varp 2337 bits 0-6, quest config js5-2 archive 35 file 140): start 5, complete 65.
  // Accounts that finished the pre-rework quest carry varp 2339 >= 80 instead.
  const DP_PROG = 10848, DP_START = 5, DP_DONE = 65;
  const DP_SIDE = [10849, 10850, 10851, 10852, 10853, 10854, 10855, 10856, 10857];   // the other varbits on varp 2337
  // Quest start: Commander Denulth, npc 1060 at 2918,3561 (Burthorpe training camp).
  const DP_DENULTH = 1060, DP_DENULTH_X = 2918, DP_DENULTH_Y = 3561;
  // v=5: the cave entrance north-west of the castle, loc 34395 at 2857,3578.
  const DP_CAVE = 34395, DP_CAVE_X = 2857, DP_CAVE_Y = 3578;
  // Inside the cave (around 2269,4752): Sabbot, npc 15096.
  const DP_SABBOT = 15096, DP_CAVE_IN_X = 2269, DP_CAVE_IN_Y = 4752;
  let dpInCave = false;   // steps 1-2 (reach the cave) have no var: latched on standing inside it
  // v=10: leave through loc 67568 "Sabbot's cave" (Exit).
  const DP_CAVE_EXIT = 67568;
  // v=10, outside: Freda, npc 15099 at 2821,3556.
  const DP_FREDA = 15099, DP_FREDA_X = 2821, DP_FREDA_Y = 3556;
  const DP_CLIMBING_BOOTS = 18788;   // from Freda at v=15
  const DP_SPIKED_BOOTS = 3107;      // from Dunstan at v=30
  const DP_SURVEY = 23084;           // from Freda at v=35
  // v=40, in the cave: Rock Wall loc 67562 at 2265,4758 (Mine).
  const DP_WALL = 67562, DP_WALL_X = 2265, DP_WALL_Y = 4758;
  const DP_WALL_VB = 10849;   // 1 = wall ready, 2 = being mined (v=45), 3 = mined through
  // The cavern behind the wall (around 3405,4283): obstacles in route order.
  const DP_CAVERN_X = 3405, DP_CAVERN_Y = 4283;
  const DP_CLIFF1 = 67674, DP_CLIFF1_X = 3406, DP_CLIFF1_Y = 4281;   // Cliffside (Jump-down), plane 2
  const DP_OBST1_DONE = [3406, 4279, 1];                            // landing tile after the jump: obstacle 1 passed
  const DP_GAP1 = 67676, DP_GAP1_X = 3421, DP_GAP1_Y = 4280;         // Gap (Squeeze-through), plane 1
  const DP_OBST2_DONE = [3422, 4280, 1];                            // through the gap: obstacle 2 passed
  const DP_CREV1 = 67678, DP_CREV1_X = 3434, DP_CREV1_Y = 4276;     // Crevice (Jump), plane 1
  const DP_OBST3_DONE = [3434, 4275, 1];                            // over the crevice: obstacle 3 passed
  const DP_ROPE1 = 67752, DP_ROPE1_X = 3432, DP_ROPE1_Y = 4261;     // Rope swing (Swing-on), plane 1
  const DP_OBST4_DONE = [3430, 4261, 1];                            // after the swing: obstacle 4 passed
  const DP_STONES1 = 67679, DP_STONES1_X = 3421, DP_STONES1_Y = 4260;   // Stepping stones (Jump), plane 1
  const DP_OBST5_DONE = [3415, 4260, 1];                            // across the stones: obstacle 5 passed
  const DP_GAP2 = 67676, DP_GAP2_X = 3417, DP_GAP2_Y = 4253;         // second Gap (Squeeze-through), plane 1
  const DP_OBST6_DONE = [3417, 4252, 1];                            // through the second gap: obstacle 6 passed
  const DP_CLIFF2 = 67570, DP_CLIFF2_X = 3422, DP_CLIFF2_Y = 4237;   // Cliffside (Climb) at the end, plane 1
  const DP_OBST7_DONE = [3425, 4238, 2];                            // top of the climb: the route is done
  const DP_HOLE = 67569, DP_HOLE_X = 3436, DP_HOLE_Y = 4239;         // Cliffside hole (Exit), plane 2
  // Outside is an instance (x >= 6400), so tiles mean nothing there: The Map, npc 15100, in scene = out of the cave.
  const DP_MAP = 15100;
  let dpOutside = false;
  let dpAtBurthorpe2 = false;   // step 16 (Home Teleport back) latched within 20 tiles of the lodestone
  // obstacles passed, latched on the landing tiles (no var records them); kept in the prefs so a panel reload
  // mid-route does not forget them
  let dpObst = 0; try { dpObst = parseInt(prefGet('rtxDpObst', '0'), 10) || 0; } catch (e) {}
  function dpObstSet(n) { if (n <= dpObst) return; dpObst = n; try { prefSet('rtxDpObst', String(n)); } catch (e) {} }
  let dpAtBurthorpe = false;   // step 7 (Home Teleport to Burthorpe) has no var: latched within 20 tiles of the lodestone
  const DP_LODE_X = 2899, DP_LODE_Y = 3544;
  // v=15, after the teleport: Dunstan, npc 1082 at 2926,3551.
  const DP_DUNSTAN = 1082, DP_DUNSTAN_X = 2926, DP_DUNSTAN_Y = 3551;
  function dpLodestoneHud(on) {
    const lo = (typeof LODESTONES !== 'undefined') ? LODESTONES.find(l => l.n === 'Burthorpe') : null;
    hudSet(lo ? lo.sp : 0, 'Teleport to the Burthorpe lodestone', !!(on && lo));
    if (on) { qgClrNpc(); qgClrTiles(); qgClrDlg(); qgClrItem(); }
  }
  async function dpBoxOpt(opt) {
    let b = false; try { b = await PLUGIN_API['overlay.highlightOption'].run([opt], myPid()); } catch (e) {}
    if (b) { qgClrNpc(); qgClrTiles(); qgClrItem(); }
    return b;
  }
  function dpNear(t) { return !!qgP && (qgP.p | 0) === t[2] && (qgP.x | 0) === t[0] && (qgP.y | 0) === t[1]; }   // the exact tile and plane
  // Mark one loc by id AND tile: the cavern repeats the same loc id (several Gaps), so the id alone marks them all.
  function dpObjAt(sc, id, x, y, p, name, action) {
    qgClrNpc(); qgClrDlg(); qgClrItem();
    const o = sc.objects.find(e => e && e.id === id && e.x === x && e.y === y && (e.plane | 0) === p);
    if (o) qgOv('overlay.guideTiles', [{ x: o.x, y: o.y, plane: p, label: name + '\n' + action }]);
    else qgObject(name, action, x, y, p);
  }
  async function dpStep() {
    let vbm = {}; try { vbm = await readVarbitValues([DP_PROG, DP_WALL_VB]); } catch (e) { return; }
    const v = vbm[DP_PROG] | 0, wall = vbm[DP_WALL_VB] | 0;
    if (v >= DP_DONE) { qgClearAll(); return; }
    if (v < 45 || wall < 3) { if (dpObst) { dpObst = 0; try { prefSet('rtxDpObst', '0'); } catch (e) {} } dpOutside = false; }
    if (v < DP_START) {   // step 0: Talk to Commander Denulth {1 Do you have any quests for me? > Accept}
      if (await dpBoxOpt('accept')) return;
      if (await dpBoxOpt('any quests for me')) return;
      await qgNpc('#' + DP_DENULTH, 'Talk to Commander Denulth', DP_DENULTH_X, DP_DENULTH_Y, 0);
      return;
    }
    if (v < 5) dpInCave = false;
    if (v >= 5 && qgP && Math.abs(qgP.x - DP_CAVE_IN_X) <= 40 && Math.abs(qgP.y - DP_CAVE_IN_Y) <= 40) dpInCave = true;
    if (v === 5) {
      const sc = await qgScene(64);
      if (sc.npcs.some(n => n && n.id === DP_SABBOT)) {
        if (await dpBoxOpt('route to death plateau')) return;   // {1 I've been sent to look for a route to Death Plateau. Can you help?}
        await qgNpc('#' + DP_SABBOT, 'Talk to Sabbot'); return;
      }
      if (!(await qgObjectById(DP_CAVE, 'Cave entrance', sc.objects))) qgObject('Cave entrance', '', DP_CAVE_X, DP_CAVE_Y, 0);
      return;
    }
    if (v === 10) {
      if (qgP.y >= 4000) {   // still underground: the cave map sits at y 4700+
        const sc = await qgScene(64);
        if (!(await qgObjectById(DP_CAVE_EXIT, "Sabbot's cave\nExit", sc.objects))) qgClearAll();
        return;
      }
      await qgNpc('#' + DP_FREDA, 'Talk to Freda', DP_FREDA_X, DP_FREDA_Y, 0);
      return;
    }
    if (v < 15) dpAtBurthorpe = false;
    if (v === 15) {
      if (!dpAtBurthorpe && Math.abs(qgP.x - DP_LODE_X) <= 20 && Math.abs(qgP.y - DP_LODE_Y) <= 20) dpAtBurthorpe = true;
      if (!dpAtBurthorpe) { dpLodestoneHud(true); return; }   // HUD: teleport to the Burthorpe lodestone
      dpLodestoneHud(false);
      if (await dpBoxOpt('fresh spikes')) return;   // {1 Can you put some fresh spikes on these climbing boots for me?}
      await qgNpc('#' + DP_DUNSTAN, 'Talk to Dunstan', DP_DUNSTAN_X, DP_DUNSTAN_Y, 0);
      return;
    }
    if (v === 30) { await qgNpc('#' + DP_FREDA, 'Bring the spiked boots to Freda', DP_FREDA_X, DP_FREDA_Y, 0); return; }
    if (v === 35) { qgItem(DP_SURVEY, 'Read the Survey'); return; }
    if (v === 40) {   // survey read; varbit 10849 = 1 morphs the rock wall in Sabbot's cave
      if (qgP.y < 4000) {
        const sc = await qgScene(64);
        if (!(await qgObjectById(DP_CAVE, 'Cave entrance', sc.objects))) qgObject('Cave entrance', '', DP_CAVE_X, DP_CAVE_Y, 0);
        return;
      }
      const sc = await qgScene(64);
      if (!(await qgObjectById(DP_WALL, 'Rock Wall\nMine', sc.objects))) qgObject('Rock Wall', 'Mine', DP_WALL_X, DP_WALL_Y, 0);
      return;
    }
    if (v === 45) {
      if (wall < 3) {   // still mining
        const sc = await qgScene(64);
        if (!(await qgObjectById(DP_WALL, 'Rock Wall\nMine', sc.objects))) qgObject('Rock Wall', 'Mine', DP_WALL_X, DP_WALL_Y, 0);
        return;
      }
      const sc = await qgScene(64);
      const map = sc.npcs.some(n => n && n.id === DP_MAP);
      if (map) { dpOutside = true; dpObstSet(7); }   // past the whole route, whatever the latches missed
      if (map) { await qgNpc('#' + DP_MAP, 'The Map'); return; }
      if (Math.abs(qgP.x - DP_CAVERN_X) <= 80 && Math.abs(qgP.y - DP_CAVERN_Y) <= 80) {   // inside the cavern
        if (dpNear(DP_OBST1_DONE)) dpObstSet(1);
        if (dpNear(DP_OBST2_DONE)) dpObstSet(2);
        if (dpNear(DP_OBST3_DONE)) dpObstSet(3);
        if (dpNear(DP_OBST4_DONE)) dpObstSet(4);
        if (dpNear(DP_OBST5_DONE)) dpObstSet(5);
        if (dpNear(DP_OBST6_DONE)) dpObstSet(6);
        if (dpNear(DP_OBST7_DONE)) dpObstSet(7);
        if (dpObst < 1) { dpObjAt(sc, DP_CLIFF1, DP_CLIFF1_X, DP_CLIFF1_Y, 2, 'Cliffside', 'Jump-down'); return; }
        if (dpObst === 1) { dpObjAt(sc, DP_GAP1, DP_GAP1_X, DP_GAP1_Y, 1, 'Gap', 'Squeeze-through'); return; }
        if (dpObst === 2) { dpObjAt(sc, DP_CREV1, DP_CREV1_X, DP_CREV1_Y, 1, 'Crevice', 'Jump'); return; }
        if (dpObst === 3) { dpObjAt(sc, DP_ROPE1, DP_ROPE1_X, DP_ROPE1_Y, 1, 'Rope swing', 'Swing-on'); return; }
        if (dpObst === 4) { dpObjAt(sc, DP_STONES1, DP_STONES1_X, DP_STONES1_Y, 1, 'Stepping stones', 'Jump'); return; }
        if (dpObst === 5) { dpObjAt(sc, DP_GAP2, DP_GAP2_X, DP_GAP2_Y, 1, 'Gap', 'Squeeze-through'); return; }
        if (dpObst === 6) { dpObjAt(sc, DP_CLIFF2, DP_CLIFF2_X, DP_CLIFF2_Y, 1, 'Cliffside', 'Climb'); return; }
        dpObjAt(sc, DP_HOLE, DP_HOLE_X, DP_HOLE_Y, 2, 'Cliffside hole', 'Exit');   // step 13: out through the hole
        return;
      }
      // mined through: the same loc is now "Cavern" (Enter)
      if (!(await qgObjectById(DP_WALL, 'Cavern\nEnter', sc.objects))) qgObject('Cavern', 'Enter', DP_WALL_X, DP_WALL_Y, 0);
      return;
    }
    if (v === 50) {   // out of the cave: The Map approaches
      dpObstSet(7);
      const sc = await qgScene(64);
      if (sc.npcs.some(n => n && n.id === DP_MAP)) {
        if (await dpBoxOpt('prepare to die')) return;   // {3 Prepare to die, troll!}
        await qgNpc('#' + DP_MAP, 'Talk to The Map'); return;
      }
      qgClearAll();
      return;
    }
    if (v < 60) dpAtBurthorpe2 = false;
    if (v === 60) {   // The Map is dead: back to Burthorpe, then Denulth
      if (!dpAtBurthorpe2 && Math.abs(qgP.x - DP_LODE_X) <= 20 && Math.abs(qgP.y - DP_LODE_Y) <= 20) dpAtBurthorpe2 = true;
      if (!dpAtBurthorpe2) { dpLodestoneHud(true); return; }
      dpLodestoneHud(false);
      await qgNpc('#' + DP_DENULTH, 'Talk to Commander Denulth', DP_DENULTH_X, DP_DENULTH_Y, 0);
      return;
    }
    if (v === 55) {   // the fight is on
      const sc = await qgScene(64);
      if (sc.npcs.some(n => n && n.id === DP_MAP)) { await qgNpc('#' + DP_MAP, 'Kill The Map'); return; }
      qgClearAll();
      return;
    }
    qgClearAll();   // later steps are mapped as the quest is played
  }
  QG_AUTO['Death Plateau'] = {
    vbs: [DP_PROG, DP_WALL_VB],
    done: (vb) => {
      const v = vb[DP_PROG] | 0, wall = vb[DP_WALL_VB] | 0;
      const s = new Set();
      if (v >= 5) s.add(0);   // Talk to Commander Denulth (accepted)
      if (v > 5 || dpInCave) { s.add(1); s.add(2); }   // Head west past the castle + into the cave
      if (v >= 10) s.add(3);   // Talk to Sabbot
      if (v >= 15) { s.add(4); s.add(5); s.add(6); }   // Exit the cave, west then south to the house, talk to Freda (climbing boots 18788)
      if (v > 15 || dpAtBurthorpe) s.add(7);   // Home Teleport to Burthorpe
      if (v >= 30) s.add(8);   // Dunstan spiked the boots (spiked boots 3107)
      if (v >= 35) s.add(9);   // Boots returned to Freda (Survey 23084)
      if (v >= 40) s.add(10);  // Survey read
      if (v > 45 || (v === 45 && wall >= 3)) s.add(11);   // Rock Wall mined through
      if (dpObst >= 1) s.add('12.1');   // Cliffside jumped
      if (dpObst >= 2) s.add('12.2');   // Gap squeezed through
      if (dpObst >= 3) s.add('12.3');   // Crevice jumped
      if (dpObst >= 4) s.add('12.4');   // Rope swing done
      if (dpObst >= 5) s.add('12.5');   // Stepping stones crossed
      if (dpObst >= 6) s.add('12.6');   // Second gap squeezed through
      if (dpObst >= 7 || v >= 50) { for (let i = 1; i <= 7; i++) s.add('12.' + i); s.add(12); }   // the whole route is behind (v=50 outside)
      if (dpOutside || v >= 50) s.add(13);   // Out through the cliffside hole (The Map in scene, v=50)
      if (v >= 55) s.add(14);   // The Map approached, fight started
      if (v >= 60) s.add(15);   // The Map killed
      if (v > 60 || dpAtBurthorpe2) s.add(16);   // Home Teleport to Burthorpe
      if (v >= DP_DONE) { s.add(17); s.add(18); s.add(19); }   // Denulth, cutscene, quest complete
      return s;
    },
  };
  const DP_MON_VBS = [DP_PROG].concat(DP_SIDE);
  let dpMonVb = null, dpMonBusy = false;
  function dpMonText() {
    if (!dpMonVb) return 'progress vb ' + DP_PROG + ' = (reading...)';
    const lines = ['progress vb ' + DP_PROG + ' = ' + (dpMonVb[DP_PROG] | 0) + '   (start ' + DP_START + ', complete ' + DP_DONE + ')'];
    const side = DP_SIDE.filter(id => (dpMonVb[id] | 0) !== 0).map(id => id + '=' + (dpMonVb[id] | 0));
    lines.push('varp 2337 side vbs: ' + (side.length ? side.join(' ') : '(all 0)'));
    return lines.join('\n');
  }
  function dpMonRefresh() {
    if (dpMonBusy) return;
    if (!paneVisible('quests') && !paneVisible('questfocus')) return;
    if (!bridge() || typeof PLUGIN_API === 'undefined') return;
    dpMonBusy = true;
    (async () => {
      try {
        const vb = await readVarbitValues(DP_MON_VBS);
        const changed = !dpMonVb || DP_MON_VBS.some(id => (vb[id] | 0) !== (dpMonVb[id] | 0));
        dpMonVb = vb;
        if (changed) {
          paneRun('questfocus', () => { qgSig = ''; renderQuestFocus(); });
          paneRun('quests', () => { questDetailSig = ''; renderQuests(); });
        }
      } catch (e) {}
      dpMonBusy = false;
    })();
  }
  (function () { function dpMonLoop() { try { dpMonRefresh(); } catch (e) {} setTimeout(dpMonLoop, 1100); } setTimeout(dpMonLoop, 1650); })();

Object.assign(window, { dpMonText, dpStep });
})();
