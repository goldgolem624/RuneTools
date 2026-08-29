// RuneToolsX panel: The Restless Ghost quest guide (quest 27).
// Spliced inline into client.html at load; IIFE (window exports + registerTab; see the RTX registry in client.html).
(function () {

  // Old-generation quest: progress is a whole VARP, not a varbit. Start 1, complete at 5.
  const RG_PROG = 2324, RG_DONE = 5;
  const RG_AMULET = 552;   // Ghostspeak amulet (handed over by Father Urhney at v=2)
  const RG_SKULL = 553;    // Muddy skull (searched out of the Rocks at v=3 -> 4)
  async function rgVarp() {
    try { const vp = JSON.parse(await rtxData.raw('state.varps', String(RG_PROG))); return (vp && vp[RG_PROG]) | 0; } catch (e) { return 0; }
  }
  async function rgStep() {
    const v = await rgVarp();
    if (v === 0) {   // not started: Father Aereck in the Lumbridge Church chapel
      await qgDialogNpc('Father Aereck', 'Talk to Father Aereck', 3246, 3405, 0,
                         "i'm looking for a quest", 'accept');
      return;
    }
    if (v === 1) {   // accepted -> Father Urhney in the swamp hut
      await qgDialogNpc('Father Urhney', 'Talk to Father Urhney', 3209, 3148, 0,
                         'father aereck sent me to talk to you', 'a ghost is haunting his graveyard');
      return;
    }
    if (v === 2) {   // Ghostspeak amulet received
      if ((await qgEquipCount(RG_AMULET)) > 0) {
        // Opening the coffin spawns the Restless ghost (npc 457).
        const sc = await qgScene();
        if (sc.npcs.some(n => n && n.id === 457)) {
          await qgDialogNpc('#457', 'Talk to the Restless ghost', 0, 0, 0, 'tell me what the problem is');
          return;
        }
        if (!(await qgObjectById(89480, 'Coffin\nOpen', sc.objects))) qgObject('Coffin', 'Open', 3250, 3192, 0);
        return;
      }
      qgItem(RG_AMULET, 'Equip the Ghostspeak amulet');
      return;
    }
    if (v === 3) {   // ghost heard out -> the Rocks south-east of the mining site (loc 47713)
      if (!(await qgObjectById(47713, 'Rocks\nSearch'))) qgObject('Rocks', 'Search', 3235, 3146, 0);
      return;
    }
    if (v === 4) {   // skull in hand -> re-open the coffin (89480), then use the skull on it (89483)
      const sc = await qgScene();
      if (sc.objects.some(o => o && o.id === 89483)) {
        // Write the tile + item channels directly each tick: qgObjectById/qgItem clear the other
        // channel first, and that clear-then-redraw flashes the skull box.
        qgClrNpc(); qgClrDlg();
        const marks = qgIdMarks(sc.objects, 89483, 'Coffin\nUse the Muddy skull on it');
        qgOv('overlay.guideTiles', marks.length ? marks : [{ x: 3251, y: 3193, plane: 0, label: 'Coffin\nUse the Muddy skull on it' }]);
        qgOv('overlay.highlightItem', RG_SKULL, 'Use on the Coffin');
        return;
      }
      if (!(await qgObjectById(89480, 'Coffin\nOpen', sc.objects))) qgObject('Coffin', 'Open', 3250, 3192, 0);
      return;
    }
    switch (v) {
      default: qgClearAll(); break;   // 5 = quest COMPLETE -> nothing to guide
    }
  }
  let rgMonV = null, rgMonBusy = false;
  function rgMonText() {
    return 'progress varp ' + RG_PROG + ' = ' + (rgMonV === null ? '(reading...)' : rgMonV) +
           '   (start 1, complete ' + RG_DONE + ')';
  }
  function rgMonRefresh() {
    if (rgMonBusy) return;
    if (!paneVisible('quests') && !paneVisible('questfocus')) return;
    if (!bridge()) return;
    rgMonBusy = true;
    (async () => {
      try {
        const nv = await rgVarp();
        if (nv !== rgMonV) {
          rgMonV = nv;
          paneRun('questfocus', () => { qgSig = ''; renderQuestFocus(); });
          paneRun('quests', () => { questDetailSig = ''; renderQuests(); });
        }
      } catch (e) {}
      rgMonBusy = false;
    })();
  }
  (function () { function rgMonLoop() { try { rgMonRefresh(); } catch (e) {} setTimeout(rgMonLoop, 1100); } setTimeout(rgMonLoop, 1700); })();
  // Auto-completes the quick-guide checkboxes; qgAutoTick reads the `vps` list and hands the
  // values to done() as its third argument. Steps with no varp of their own latch on
  // proximity, so walking away cannot untick them; the latch resets when progress falls back.
  let rgAtHut = false;
  let rgAtCoffin = false;
  let rgAtRocks = false;
  let rgBackCoffin = false; // stands in for the skeleton step, which has no signal of its own
  QG_AUTO['The Restless Ghost'] = {
    vbs: [],
    vps: [RG_PROG],
    inv: true,
    done: (vb, inv, vp) => {
      const v = (vp && vp[RG_PROG]) | 0;
      const s = new Set();
      if (v >= 1) s.add(0);   // Talk to Father Aereck (accepting -> 1)
      if (v < 1) rgAtHut = false;
      if (v === 1 && qgP && (qgP.p | 0) === 0 && Math.abs(qgP.x - 3207) <= 7 && Math.abs(qgP.y - 3149) <= 7) rgAtHut = true;
      if (v > 1 || rgAtHut) s.add(1);   // Follow the shore to the mining site, west to the swamp hut
      if (v >= 2) s.add(2);   // Talk to Father Urhney inside the hut (amulet received -> 2)
      if (v < 2) rgAtCoffin = false;
      if (v === 2 && qgP && (qgP.p | 0) === 0 && Math.abs(qgP.x - 3250) <= 4 && Math.abs(qgP.y - 3192) <= 4) rgAtCoffin = true;
      if (v > 2 || rgAtCoffin) s.add(3);   // Go back to the graveyard and open the coffin
      if (v >= 3) s.add(4);   // Equip the Ghostspeak amulet and talk to the ghost
      if (v < 3) rgAtRocks = false;
      if (v === 3 && qgP && (qgP.p | 0) === 0 && Math.abs(qgP.x - 3235) <= 6 && Math.abs(qgP.y - 3146) <= 6) rgAtRocks = true;
      if (v > 3 || rgAtRocks) s.add(5);   // Head back to the mining site
      if (v > 4 || (v === 4 && inv && inv.has(RG_SKULL))) s.add(6);   // Search the rocks -> Muddy skull
      if (v < 4) rgBackCoffin = false;
      if (v === 4 && qgP && (qgP.p | 0) === 0 && Math.abs(qgP.x - 3251) <= 7 && Math.abs(qgP.y - 3193) <= 7) rgBackCoffin = true;
      if (v > 4 || rgBackCoffin) s.add(7);   // Kill the skeleton or run away; ticks on returning to the coffin
      if (v >= RG_DONE) { s.add(8); s.add(9); }   // Use the skull on the open coffin + Quest completed!
      return s;
    },
  };

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { rgMonText, rgStep });
})();
