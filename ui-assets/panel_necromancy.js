// RuneToolsX panel: Necromancy! quest guide (quest 493).
// Spliced inline into client.html at load; IIFE (window exports + registerTab; see the RTX registry in client.html).
(function () {

  // Progress varbit 53549: starts at 5, completes at 70.
  const NECRO_PROG = 53549, NECRO_DONE = 70;
  const NECRO_MOUNDS = 53550;   // sub varbit: staging of the ritual steps (Mound of dust NPCs 30429/30431, action Clear)
  const NECRO_WELL = 53551;     // sub varbit: 1 while the Well of Souls interface is open (v=60)
  const NECRO_DGUARD = 55502;   // Death guard item; equipping it progresses the quest
  const NECRO_TROLLS = 53552;   // sub varbit: 1 when the Ghost troll brute dies
  const NECRO_LOUT = 53553;     // sub varbit: 1 when the Ghost troll lout dies (53551 -> 5 with it)
  const NECRO_FINAL = 53554;    // sub varbit: 1 once the final options at v=70 are clicked through
  async function necroStep() {
    let vbm = {}; try { vbm = await readVarbitValues([NECRO_PROG, NECRO_MOUNDS, NECRO_WELL, NECRO_TROLLS]); } catch (e) { return; }
    const v = vbm[NECRO_PROG] | 0, mounds = vbm[NECRO_MOUNDS] | 0, well = vbm[NECRO_WELL] | 0, trolls = vbm[NECRO_TROLLS] | 0;
    // Underworld portal north of the Draynor lodestone (loc 127139, action Enter).
    if (v === 0) {   // not started; accepting the quest moves the varbit to 5
      let boxed = false; try { boxed = await PLUGIN_API['overlay.highlightOption'].run(['accept'], myPid()); } catch (e) {}
      if (boxed) { qgClrNpc(); qgClrTiles(); qgClrItem(); return; }   
      qgObject('Underworld portal', 'Enter to begin the quest', 3104, 3313, 0);
      return;
    }
    if (v === 5 || v === 10) { qgObject('Underworld portal', 'Enter to continue the dialogue', 3104, 3313, 0); return; }
    // Steps 15-60 run inside the quest instance; back out in the world, re-enter through the portal.
    if (v >= 15 && v <= 60 && qgP && !qgInInstance(qgP)) {
      qgObject('Underworld portal', 'Enter to return to the quest', 3104, 3313, 0);
      return;
    }
    if (v === 15) { await qgNpc('Death', 'Speak to Death'); return; }   
    if (v === 20) { await qgNpc('Death', 'Speak to Death'); return; }   // 20 = the cutscene, then the same conversation resumes
    if (v === 25 || v === 30 || v === 35) { await qgNpc('Death', 'Continue the dialogue with Death'); return; }
    if (v === 40 || v === 45) { await qgNpc('Malignius Mortifer', 'Speak to Malignius Mortifer'); return; }
    if (v === 50) {   // NECRO_MOUNDS staging: 0 first chat, 1 clear mounds, 2/3 Malignius, 4 bones, 5 candles
      if (mounds === 1) { await qgNpc('Mound of dust', 'Clear the Mounds of dust'); return; }
      if (mounds === 4) { qgObject('Pedestal', 'Place bones'); return; }   // no fixed tile; anchors at the player
      if (mounds === 5) { await qgNpc('#30433', 'Place candles on the Light source spots'); return; }   // action Place candle
      if (mounds === 7) { await qgNpc('#30432', 'Draw glyph on the Glyph spots'); return; }   // action Draw glyph (6 = candles done -> Malignius fallback)
      if (mounds === 8) { await qgNpc('#30435', 'Repair the Elemental I glyph'); return; }   // NPC 30435 "Elemental I (depleted)", action Repair
      if (mounds === 10) { qgObject('Platform', 'Start ritual'); return; }   // loc, player-anchored (9 = repaired -> Malignius fallback)
      await qgNpc('Malignius Mortifer', 'Continue the dialogue with Malignius Mortifer'); return;
    }
    if (v === 55) { await qgNpc('Ted', 'Continue the dialogue with Ted'); return; }   // 55 (53550 = 12) = the ritual is done
    if (v === 65) { await qgNpc('Ted', 'Continue the dialogue'); return; }   // 65 (53551 = 7) = the Malignius step is checked off
    if (v === 60) {   // NECRO_WELL staging: 1 interface open, 2 talent selected, 3 Malignius, 4 equip the Death guard
      if (well >= 6) { await qgNpc('Ted', 'Talk to Ted'); return; }   
      if (well >= 5) {   // trolls down -> interact with the Well again to unlock the talent
        if (!(await qgObjectById(127273, 'Well of Souls\nManage'))) qgObject('Well of Souls', 'Manage');
        return;
      }
      if (well >= 4) {
        if ((await qgEquipCount(NECRO_DGUARD)) > 0) {   // latch read by QG_AUTO
          necroDgWorn = true;
          const sc = await qgScene();
          const brute = sc.npcs.some(n => n && n.id === 30260), lout = sc.npcs.some(n => n && n.id === 30261);
          if (brute && trolls < 1) { await qgNpc('#30260', 'Kill the Ghost troll brute'); return; }   // NECRO_TROLLS 1 = brute dead
          if (lout) { await qgNpc('#30261', 'Kill the Ghost troll lout'); return; }
          qgClearAll(); return;   
        }
        qgItem(NECRO_DGUARD, 'Equip the Death guard');
        return;
      }
      if (well >= 3) { await qgNpc('Malignius Mortifer', 'Continue the dialogue with Malignius Mortifer'); return; }
      if (well >= 1) { qgClearAll(); return; }   // in the interface: select the Conjure Skeleton Warrior talent
      if (!(await qgObjectById(127273, 'Well of Souls\nManage'))) qgObject('Well of Souls', 'Manage');
      return;
    }
    switch (v) {
      default: qgClearAll(); break;   // 70 = quest complete
    }
  }
  // Live varbit monitor shown above the walkthrough in the quest detail / focus panel. Polled on its
  // own loop: renders are signature-gated, so a poll kicked from the render would stall once nothing changed.
  const NECRO_MON_VBS = [NECRO_PROG, NECRO_MOUNDS, NECRO_WELL, NECRO_TROLLS, NECRO_LOUT, NECRO_FINAL];
  let necroMonVb = null, necroMonBusy = false;
  function necroMonText() {
    if (!necroMonVb) return 'progress vb ' + NECRO_PROG + ' = (reading...)';
    return NECRO_MON_VBS.map(id =>
      (id === NECRO_PROG ? 'progress vb ' : 'sub vb ') + id + ' = ' + (necroMonVb[id] | 0) +
      (id === NECRO_PROG ? '   (start 5, complete ' + NECRO_DONE + ')' : '')).join('\n');
  }
  function necroMonRefresh() {
    if (necroMonBusy) return;
    if (!paneVisible('quests') && !paneVisible('questfocus')) return;   
    if (!bridge() || typeof PLUGIN_API === 'undefined') return;
    necroMonBusy = true;
    (async () => {
      try {
        const vb = await readVarbitValues(NECRO_MON_VBS);
        const changed = !necroMonVb || NECRO_MON_VBS.some(id => (vb[id] | 0) !== (necroMonVb[id] | 0));
        necroMonVb = vb;
        if (changed) {
          paneRun('questfocus', () => { qgSig = ''; renderQuestFocus(); });
          paneRun('quests', () => { questDetailSig = ''; renderQuests(); });
        }
      } catch (e) {}
      necroMonBusy = false;
    })();
  }
  (function () { function necroMonLoop() { try { necroMonRefresh(); } catch (e) {} setTimeout(necroMonLoop, 1100); } setTimeout(necroMonLoop, 1600); })();
  // Quick-guide checkboxes auto-completed from NECRO_PROG thresholds; step i = flat index across the
  // quick-guide sections (Starting out 0-2, Communion ritual 3-12, The Well of Souls 13-19).
  // QG_AUTO is declared in panel_visions.js.
  let necroWellOpened = false;   // latch: the Well interface flag 53551 drops back on close; opening once keeps the step done
  let necroTalentPicked = false; // latch: 53551 -> 2 = Conjure Skeleton Warrior talent selected
  let necroDgWorn = false;       // latch: Death guard seen equipped at 53551 >= 4 (set by necroStep)
  QG_AUTO['Necromancy!'] = {
    vbs: [NECRO_PROG, NECRO_MOUNDS, NECRO_WELL, NECRO_FINAL],
    done: (vb) => {
      const v = vb[NECRO_PROG] | 0, mounds = vb[NECRO_MOUNDS] | 0, well = vb[NECRO_WELL] | 0;
      const s = new Set();
      // v=5/10 do NOT complete step 0: the portal step is still in progress (re-enter to continue).
      if (v >= 15) s.add(0);   // Interact with the white portal {Accept}
      if (v >= 20) s.add(1);   // Speak to Death
      if (v >= 40) s.add(2);   // After the cutscene ends, speak to Death again
      if (v >= 50) s.add(3);   // Speak to Malignius Mortifer
      if (v > 50 || (v === 50 && mounds >= 2)) s.add(4);   // Clear the mounds of dust
      if (v > 50 || (v === 50 && mounds >= 5)) { s.add(5); s.add(6); }   // Place the bones on the pedestal + Speak to Malignius again
      if (v > 50 || (v === 50 && mounds >= 6)) s.add(7);   // Place the ritual candles on the four light source spots
      if (v > 50 || (v === 50 && mounds >= 7)) s.add(8);   // Speak to Malignius again
      if (v > 50 || (v === 50 && mounds >= 8)) { s.add(9); s.add(10); }   // Draw the Commune I glyph at two glyph spots + Speak to Malignius again
      if (v > 50 || (v === 50 && mounds >= 9)) s.add(11);   // Repair the broken glyph
      if (v > 50 || (v === 50 && mounds >= 11)) s.add(12);  // Stand on the nearby skull platform
      if (v < 60) { necroWellOpened = false; necroTalentPicked = false; necroDgWorn = false; }   // reset on quest restart
      if (v === 60 && well >= 1) necroWellOpened = true;
      if (v === 60 && well >= 2) necroTalentPicked = true;
      if (v > 60 || necroWellOpened) s.add(13);             // After the dialogue, manage the Well of Souls
      if (v > 60 || necroTalentPicked) s.add(14);           // Select the Conjure Skeleton Warrior talent
      if (v > 60 || necroDgWorn || (v === 60 && well >= 5)) s.add(15);   // Wield the death guard (53551 >= 5 implies it; the latch is memory-only)
      if (v > 60 || (v === 60 && well >= 5)) s.add(16);     // Kill the ghost troll brute + lout
      if (v > 60 || (v === 60 && well >= 6)) s.add(17);     // Interact with the Well again, unlock the talent
      if (v >= 65) s.add(18);                               // Speak to Malignius Mortifer
      if (v >= NECRO_DONE && (vb[NECRO_FINAL] | 0) >= 1) s.add(19);   // Quest complete (70 alone still has final options; 53554 -> 1 = truly done)
      return s;
    },
  };

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { necroMonText, necroStep });
})();
