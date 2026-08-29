// RuneToolsX panel: Making History quest guide (quest 124).
// Spliced inline into client.html; IIFE (window exports + registerTab; see the RTX registry in client.html).
(function () {

  // Progress varbit 9664 (cache quest config js5-2 archive 35 file 124; varp 2173 bits 0-2):
  // start value 1, complete at 4. Requires quest 27 (The Restless Ghost).
  const MH_PROG = 9664, MH_DONE = 4;
  const MH_ERIN = 9665;    // sub varbit [3-5]: Erin/key thread -- 0 -> 1 when the Silver merchant hands over the key
  const MH_KEY = 6754;     // Enchanted key (from the Silver merchant at 9665=1)
  const MH_JOURNAL = 6755; // Journal (unlocked out of the chest; handed to Jorral at the end)
  const MH_SCROLL = 6758;  // Scroll (from Droalak at 9667=5; also for Jorral at the end)
  const MH_DROALAK = 9667; // sub varbit [9-12]: Droalak/scroll thread -- 0 -> 2 on the first Droalak
                           // chat, -> 4 when Melina gets the sapphire amulet (9668 flips with it)
  const MH_DRON = 9666;    // sub varbit [6-8]: Dron thread -- 0 -> 2 after Blanin's chat
  const MH_OUTPOST = 9671; // sub varbit [20]: 0 -> 1 when the Outpost is restored (mid Jorral finale, v=3)
  // Required items (wiki list, ids cache-checked). Rendered by qgItemsRow via QG_REQ;
  // '' = the whole-quest list shown at the top of the walkthrough.
  function mhItems(section) {
    const spade = { id: 952,  n: 1, name: 'Spade (or the meerkats familiar; buyable from Richard during the quest)' };
    const ecto  = { id: 4278, n: 2, name: 'Ecto-tokens (or Ghosts Ahoy done, or 2,600 coins to charter ships)' };
    const ammy  = { id: 1694, n: 1, name: 'Sapphire amulet (strung, un-enchanted)' };
    const ghost = { id: 552,  n: 1, name: 'Ghostspeak amulet (or cramulet; not needed with hard Morytania achievements)' };
    switch (section) {
      case '': return [spade, ecto, ammy, ghost];
    }
    return null;   // per-section lists can land here once the guide is built
  }
  // King Lathas won't talk while any Mourner outfit piece is worn -- box a removal warning.
  const MH_MOURNER = new Set([6065, 6066, 6067, 6068, 6069, 6070, 10621, 51590, 51591]);
  async function mhMournerWorn() {   // one equipment read -> true if any Mourner piece is equipped
    let eq = null; try { eq = JSON.parse(await rtxData.raw('state.equipment')); } catch (e) {}
    if (eq && Array.isArray(eq.items)) for (const it of eq.items) if (MH_MOURNER.has(it[1])) return true;
    return false;
  }
  // Dron's quiz answers include bare numbers (8, 36, 12), and the shared overlay.highlightOption
  // does BIDIRECTIONAL SUBSTRING matching, so needle '8' also matches the wrong option "38.".
  // This boxer uses EXACT (normalized) equality instead. DialogJson returns the answer text with
  // no option-index prefix (the "1."/"2." label is a separate, dropped component), so nothing
  // needs stripping.
  const mhNorm = s => String(s == null ? '' : s).toLowerCase().replace(/[^a-z0-9 ]+/g, ' ').replace(/\s+/g, ' ').trim();
  const mhIsNum = s => /^[0-9]+$/.test(s);
  async function mhBoxExact(needles) {
    let dlg = null; try { dlg = JSON.parse(await rtxData.raw('state.dialog')); } catch (e) {}
    const wants = needles.map(mhNorm).filter(Boolean);
    const box = o => { try { rtxData.sync('overlay.uiHighlight', o.x, o.y, o.w, o.h); } catch (e) {} };
    if (dlg && dlg.hasAbs && Array.isArray(dlg.options)) {
      const opts = dlg.options.filter(o => o && (o.w > 0) && (o.text || '').toLowerCase().indexOf('<str') < 0)
                              .map(o => ({ o, body: mhNorm(o.text) })).filter(x => x.body);
      // Pass 1: EXACT normalized equality. This must win before any substring test -- the needle
      // "fifth and fourth" exactly matches that option, so the shorter option "fourth" cannot steal
      // the box.
      for (const x of opts) if (wants.includes(x.body)) { box(x.o); return true; }
      // Pass 2: forward-only substring (option text CONTAINS the whole needle), phrases only. Never
      // the reverse, and never numeric, so '8' cannot touch "38".
      for (const x of opts) if (!mhIsNum(x.body) && wants.some(w => !mhIsNum(w) && x.body.indexOf(w) >= 0)) { box(x.o); return true; }
    }
    try { rtxData.sync('overlay.uiHighlight', 0, 0, 0, 0); } catch (e) {}
    return false;
  }
  async function mhStep() {
    let vbm = {}; try { vbm = await readVarbitValues([MH_PROG, MH_ERIN, MH_DROALAK, MH_DRON, MH_OUTPOST]); } catch (e) { return; }
    const v = vbm[MH_PROG] | 0, erin = vbm[MH_ERIN] | 0, droalak = vbm[MH_DROALAK] | 0, dron = vbm[MH_DRON] | 0, outpost = vbm[MH_OUTPOST] | 0;
    if (v === 0) {   // not started: Jorral at the outpost north-west of West Ardougne
      await qgDialogNpc('Jorral', 'Speak with Jorral', 2437, 3347, 0, 'tell me more');
      return;
    }
    if (v === 1) {   // accepted -> the Silver merchant (npc 569) in the East Ardougne market;
                     // his chooser boxes "3 Ask about the outpost." MH_ERIN 1 = Enchanted key held.
      if (erin >= 2) {   // chest (item 6759) dug up -> unlock it: both slots boxed at once
        if (droalak >= 5) {   // Scroll 6758 held -> Phasmatys done (no ghostspeak needed any more);
                              // on to Blanin (npc 2940) west of the Rellekka cow pen, then Dron
          if (dron >= 3) {   // quiz passed -> back to Jorral at the Outpost with BOTH the Journal and
                             // Scroll. Jorral analyses all three evidence threads (any order): show the
                             // journal -> 9665=4, discuss the warrior Dron -> 9666=4, analyse the
                             // scroll -> 9667=6. The Letter comes once all three are done.
            if (erin >= 4 && dron >= 4 && droalak >= 6) { qgClearAll(); return; }
            const haveJ = (await qgInvCount(MH_JOURNAL)) > 0, haveS = (await qgInvCount(MH_SCROLL)) > 0;
            const miss = !haveJ && !haveS ? ' - BRING THE JOURNAL AND SCROLL'
                       : !haveJ ? ' - BRING THE JOURNAL'
                       : !haveS ? ' - BRING THE SCROLL' : '';
            await qgNpc('Jorral', 'Talk to Jorral' + miss, 2437, 3347, 0);
            return;
          }
          if (dron >= 2) {   // Dron (npc 2939) west of the helmet shop. The two intro lines + the
                             // 12-answer quiz all go through mhBoxExact -- exact match so the '8'
                             // answer can't box the wrong "38." option in the 36/38 chooser.
            const boxed = await mhBoxExact([
              "i'm after important answers", "why you're the famous warrior dron",
              'an iron mace', 'breakfast', 'lunch', 'bunnies', 'red', '36', '8',
              'fifth and fourth', 'northeast side of town', 'blanin', 'fluffy',
              '12, but what does that have to do with anything']);
            if (boxed) { qgClrNpc(); qgClrTiles(); qgClrItem(); return; }
            await qgNpc('#2939', 'Talk to Dron', 2655, 3701, 0);
            return;
          }
          await qgNpc('#2940', 'Talk to Blanin', 2675, 3670, 0);
          return;
        }
        const journal = (await qgInvCount(MH_JOURNAL)) > 0;
        if (journal || droalak >= 1) {   // the Port Phasmatys ghosts only talk with the Ghostspeak amulet (552) worn
          if ((await qgEquipCount(552)) < 1) { qgItem(552, 'Equip the Ghostspeak amulet'); return; }
        }
        if (droalak >= 4) { await qgNpc('#2938', 'Retrieve the scroll from Droalak', 3662, 3472, 0); return; }   // amulet delivered -> back to Droalak
        if (droalak >= 2) {   // building east of the General Store; she needs the sapphire amulet (1694) handed over
          const warn = (await qgInvCount(1694)) > 0 ? '' : ' - GET A SAPPHIRE AMULET (strung, un-enchanted) FIRST';
          await qgNpc('#2935', 'Talk to Melina' + warn, 3671, 3484, 0);
          return;
        }
        if (journal) { await qgNpc('#2938', 'Talk to Droalak', 3662, 3472, 0); return; }   // npc 2938, Port Phasmatys
        await qgItems([[MH_KEY, 'Use on the Chest'], [6759, '']]);   // blank target label -> no overlap with the action label
        return;
      }
      if (erin >= 1) {   // key in hand -> the quest dig spot is FIXED; no hot-and-cold needed. Tile + spade shown
                         // hot-and-cold needed. Tile + spade shown together (overwrite, no clears).
        qgClrNpc(); qgClrDlg();
        qgOv('overlay.guideTiles', [{ x: 2440, y: 3140, plane: 0, label: 'Dig here' }]);
        qgOv('overlay.highlightItem', 952, 'Dig with the Spade');
        return;
      }
      await qgDialogNpc('#569', 'Talk to the Silver merchant', 2659, 3316, 0, 'ask about the outpost');
      return;
    }
    if (v === 2) {   // Letter (item 6756) received -> Ardougne Castle, climb to King Lathas on floor 2.
                     // He won't talk while a Mourner outfit is worn -> warn to remove it first.
      const suffix = (await mhMournerWorn()) ? ' (REMOVE YOUR MOURNER OUTFIT FIRST)' : '';
      // On the upper floor (plane 1) with Lathas loaded -> outline him; otherwise the staircase.
      const sc = await qgScene();
      const lathas = sc.npcs.find(n => n && n.name && String(n.name).toLowerCase() === 'king lathas');
      if ((qgP && (qgP.p | 0) === 1) && lathas) { await qgNpc('King Lathas', 'Talk to King Lathas' + suffix, lathas.x, lathas.y, 1); return; }
      if (!(await qgObjectById(34871, 'Staircase\nClimb up to King Lathas' + suffix, sc.objects))) qgObject('Staircase', 'Climb up to King Lathas' + suffix, 2571, 3285, 0);
      return;
    }
    if (v === 3) {   // King Lathas done -> back to the Outpost; 9671 -> 1 = Outpost restored, keep talking
      await qgNpc('Jorral', outpost >= 1 ? 'Continue the dialogue with Jorral' : 'Return to Jorral', 2437, 3347, 0);
      return;
    }
    switch (v) {
      default: qgClearAll(); break;   // 4 = quest COMPLETE -> nothing to guide
    }
  }
  // Live monitor: own poll loop. ALL of varp 2173's varbits ride along -- only 9664 is understood,
  // the rest are watched until the quest reveals what they mean.
  const MH_MON_VBS = [MH_PROG, 9665, 9666, 9667, 9668, 9669, 9670, 9671, 9672];
  const MH_MON_TAG = { 9664: 'progress [0-2]', 9665: 'Erin/key [3-5]', 9666: 'Dron [6-8]', 9667: 'Droalak [9-12]',
                       9668: 'Melina amulet [13]', 9669: '? [14]', 9670: '? [15-19]', 9671: 'Outpost restored [20]', 9672: '? [21-31]' };
  let mhMonVb = null, mhMonBusy = false;
  function mhMonText() {
    if (!mhMonVb) return 'progress vb ' + MH_PROG + ' = (reading...)';
    return MH_MON_VBS.map(id =>
      'vb ' + id + ' ' + (MH_MON_TAG[id] || '') + ' = ' + (mhMonVb[id] | 0) +
      (id === MH_PROG ? '   (start 1, complete ' + MH_DONE + ')' : '')).join('\n');
  }
  function mhMonRefresh() {
    if (mhMonBusy) return;
    if (!paneVisible('quests') && !paneVisible('questfocus')) return;   // monitor not on screen
    if (!bridge() || typeof PLUGIN_API === 'undefined') return;
    mhMonBusy = true;
    (async () => {
      try {
        const vb = await readVarbitValues(MH_MON_VBS);
        const changed = !mhMonVb || MH_MON_VBS.some(id => (vb[id] | 0) !== (mhMonVb[id] | 0));
        mhMonVb = vb;
        if (changed) {
          paneRun('questfocus', () => { qgSig = ''; renderQuestFocus(); });
          paneRun('quests', () => { questDetailSig = ''; renderQuests(); });
        }
      } catch (e) {}
      mhMonBusy = false;
    })();
  }
  (function () { function mhMonLoop() { try { mhMonRefresh(); } catch (e) {} setTimeout(mhMonLoop, 1100); } setTimeout(mhMonLoop, 1800); })();
  // "Go to the Outpost" has no varbit: it completes by STANDING within 10 tiles of 2436,3347 at
  // v=0 (latched; the quest cannot go below 0, so there is no reset condition).
  let mhAtOutpost = false;
  let mhJournalSeen = false;   // latch: Journal 6755 seen in the pack (banking it must not untick the chest step)
  // Auto-complete the quick-guide checkboxes from MH_PROG thresholds only.
  QG_AUTO['Making History'] = {
    vbs: [MH_PROG, MH_ERIN, MH_DROALAK, MH_DRON],
    inv: true,
    done: (vb, inv) => {
      const v = vb[MH_PROG] | 0, erin = vb[MH_ERIN] | 0, droalak = vb[MH_DROALAK] | 0, dron = vb[MH_DRON] | 0;
      const s = new Set();
      if (v === 0 && qgP && (qgP.p | 0) === 0 && Math.abs(qgP.x - 2436) <= 10 && Math.abs(qgP.y - 3347) <= 10) mhAtOutpost = true;
      if (v > 0 || mhAtOutpost) s.add(0);   // Go to the Outpost (arrive within 10 tiles of Jorral)
      if (v >= 1) { s.add(1); s.add(2); }   // Jorral cutscene + agree to help (accept -> 1; the accept chooser follows the cutscene)
      if (v > 1 || (v === 1 && erin >= 1)) s.add(3);   // Talk to the silver merchant (9665 -> 1 = Enchanted key 6754 received)
      if (v > 1 || (v === 1 && erin >= 2)) { s.add(4); s.add(5); }   // Cross the bridge + dig up the chest (9665 -> 2 = Chest 6759 held)
      if (v === 1 && inv && inv.has(MH_JOURNAL)) mhJournalSeen = true;
      if (v > 1 || mhJournalSeen) s.add(6);   // Use the key on the chest (Journal 6755 received, latched)
      if (v > 1 || (v === 1 && droalak >= 2)) s.add(7);   // Speak to Droalak in Port Phasmatys (9667 -> 2)
      if (v > 1 || (v === 1 && droalak >= 4)) s.add(8);   // Talk to Melina with the sapphire amulet (9667 -> 4, 9668 -> 1)
      if (v > 1 || (v === 1 && droalak >= 5)) s.add(9);   // Talk to Droalak again for the scroll (9667 -> 5, Scroll 6758 held)
      if (v > 1 || (v === 1 && dron >= 2)) s.add(10);     // Talk to Blanin in Rellekka (9666 -> 2)
      if (v > 1 || (v === 1 && dron >= 3)) { s.add(11); s.add(12); }   // Talk to Dron + answer his quiz (9666 -> 3 after the quiz)
      if (v > 1 || (v === 1 && erin >= 4 && dron >= 4 && droalak >= 6)) s.add(13);   // Return to Jorral, talk until he gives a Letter (journal 9665->4 + warrior 9666->4 + scroll 9667->6)
      if (v >= 3) s.add(14);   // Talk to King Lathas on floor 2 of Ardougne Castle (-> 3)
      if (v >= MH_DONE) { s.add(15); s.add(16); }   // Return to Jorral + Quest complete! (9664 -> 4)
      return s;
    },
  };

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { mhItems, mhMonText, mhStep });
})();
