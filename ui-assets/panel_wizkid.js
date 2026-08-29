// RuneToolsX panel: Wiz Kid quest guide (quest 529).
// Spliced inline into client.html at load; IIFE (window exports + registerTab; see the RTX registry in client.html).
(function () {

  // Progress varbit 60829 (quest config js5-2 archive 35 file 529): starts at 5, complete at 95.
  const WIZKID_PROG = 60829, WIZKID_DONE = 95;
  const WIZKID_RUNES = [
    { id: 556, n: 8, name: 'Air rune' },
    { id: 555, n: 6, name: 'Water rune' },
    { id: 557, n: 4, name: 'Earth rune' },
    { id: 561, n: 2, name: 'Nature rune' },
    { id: 563, n: 4, name: 'Law rune' },
  ];
  let wizkidHave = null, wizkidHaveAt = 0, wizkidHaveBusy = false;   // id -> backpack count (null until first scan)
  let wizkidV = -1;
  const WIZKID_TEACH_OPTS = ["i'm not sure i want to teach you", 'what could go wrong'];   // v=5 Ben chooser -- either works
  let wizkidTeachPick = -1;
  const WIZKID_BOOK_VB = 0;           // spellbook varbit (varp 4 b0-1): this quest needs 0 = standard
  const WIZKID_TELE_SPRITE = 35665;   // Wendlewick Teleport spell icon (struct 53006 spell_prayer_ability_sprite)
  const WIZKID_MAGIC_SPRITE = 16055;  // Magic skill icon (SKILL_SPRITES[6]) for the spellbook alert
  const WIZKID_TKGRAB_SPRITE = 14332; // Telekinetic Grab spell icon (struct 14757 spell_prayer_ability_sprite)
  const WIZKID_B2B_SPRITE = 14380;    // Bones to Bananas spell icon (cast on the backpack bones at v=60)
  const WIZKID_DOORS = [45964, 45966];        // v=30 door halves, closed (instance tiles are dynamic)
  const WIZKID_DOORS_OPEN = [45965, 45967];   // open variants: in scene = the doors have been opened
  const WIZKID_STAIRS = 36795;                // staircase to the top floor (after the doors open)
  const WIZKID_STAIRS_DOWN = 36797;           // staircase back down (v=35)
  const WIZKID_FLOWERS = 136628;              // Flowers to Take (v=40)
  const WIZKID_BUNCH = 60458;                 // Bunch of flowers (holding it = the take step is done)
  const WIZKID_STOOL = 136625;                // Stool to Place flowers on (v=40, with the bunch)
  const WIZKID_CROWBONES = 60460;             // Crow bones (2 from digging the mound)
  const WIZKID_MOUND = 136631;                // Dirt mound to Dig (v=60, without the bones)
  // Latches: placing removes the item and grabbing clears the ground stack, so a raw check would un-complete the step.
  let wizkidHadBunch = false;
  let wizkidFlowersPlaced = false;
  let wizkidHadBones = false;                 // both crow bones have been in the backpack (v=60, reset below 60)
  let wizkidBonesPlaced = false;
  // Backpack rune-count scan (throttled); a change clears the guide signatures to force a repaint.
  function wizkidHaveRefresh() {
    if (wizkidHaveBusy || Date.now() - wizkidHaveAt < 2000) return;
    if (!bridge() || typeof PLUGIN_API === 'undefined') return;
    wizkidHaveBusy = true;
    (async () => {
      try {
        const inv = JSON.parse((await PLUGIN_API['state.inventory'].run([], myPid())) || '{}');
        const have = {};
        for (const r of WIZKID_RUNES) have[r.id] = 0;
        if (inv && Array.isArray(inv.items))
          for (const it of inv.items) if (have[it[1]] !== undefined) have[it[1]] += (it[2] > 0 ? it[2] : 1);
        if (!wizkidHave || WIZKID_RUNES.some(r => wizkidHave[r.id] !== have[r.id])) {
          wizkidHave = have; qgSig = ''; questDetailSig = '';
        }
      } catch (e) {}
      wizkidHaveAt = Date.now(); wizkidHaveBusy = false;
    })();
  }
  function wizkidItems() {
    wizkidHaveRefresh();
    return WIZKID_RUNES.map(r => ({ id: r.id, n: r.n, name: r.name, have: wizkidHave ? wizkidHave[r.id] : undefined }));
  }
  // Quick-guide auto-checkmarks: flat step index -> done once WIZKID_PROG passes that value (QG_AUTO is declared in the earlier-spliced panel_visions.js).
  QG_AUTO['Wiz Kid'] = {
    vbs: [WIZKID_PROG], inv: true,
    done: (vb, inv) => {
      const v = vb[WIZKID_PROG] | 0;
      const s = new Set();
      if (v >= 10) s.add(0);   // Talk to Ben at Marigold Farm (start)
      if (v >= 15) s.add(1);   // Talk to him again to give him the runes
      if (v >= 20) s.add(2);   // Talk to him again to continue the quest
      if (v >= 30) s.add(3);   // Cast the Wendlewick Teleport spell, and talk to him again
      if (v >= 35) s.add(4);   // Enter the windmill, climb to the top floor, talk to him
      if (v >= 40) s.add(5);   // Go back to the bottom floor and talk to him outside
      if (inv.has(WIZKID_BUNCH)) wizkidHadBunch = true;
      if (v > 40 || wizkidHadBunch || wizkidFlowersPlaced) s.add(6);   // Farmhouse counter -> Bunch of flowers (latched)
      if (v > 40 || wizkidFlowersPlaced) s.add(7);                     // Flowers placed on the stool (Telekinetic Grab step)
      if (v >= 55) s.add(8);   // Place the flowers back on the stool and talk to Ben
      if (v > 60 || wizkidHadBones || wizkidBonesPlaced) s.add(9);   // Dig the dirt mound for 2 sets of crow bones (latched)
      if (v >= 65) s.add(10);   // Place a bone on the stool near Ben, then cast the Bones to Bananas spell
      if (v >= 75) s.add(11);   // Talk to Ben
      if (v >= 80) s.add(12);   // Wizards' Tower floor 3, talk to Archmage Sedridor
      if (v >= 85) s.add(13);   // Go down one floor and talk to Wizard Isidor to the west
      if (v >= 90) s.add(14);   // Return to Ben and talk to him or Wizard Isidor
      if (v >= 95) s.add(15);   // Quest complete (WIZKID_DONE)
      return s;
    }
  };
  // Keybinds for the quest's spells on a VISIBLE action bar: slot ids ARE sprite ids, and a secondary bar counts only while its gate varbit is set.
  let wizkidKeys = {}, wizkidKeyAt = 0, wizkidKeyBusy = false;
  function wizkidKeyRefresh() {
    if (wizkidKeyBusy || Date.now() - wizkidKeyAt < 3000) return;
    if (!bridge() || !bridge().actionBar) return;
    wizkidKeyBusy = true;
    (async () => {
      try {
        const j = JSON.parse(await bridge().actionBar(myPid()));
        const gate = { 1: 29138, 2: 29139, 3: 29140, 4: 29141 };
        const vb = await readVarbitValues(Object.values(gate));
        const MOD = ['', 'Shift+', 'Ctrl+', 'Alt+'];
        const keys = {};
        for (const b of (j.bars || [])) {
          if (b.bar !== 0 && !((vb[gate[b.bar]] | 0) > 0)) continue;
          for (const s of (b.slots || []))
            if (s.key && keys[s.id] === undefined && (s.id === WIZKID_TELE_SPRITE || s.id === WIZKID_TKGRAB_SPRITE || s.id === WIZKID_B2B_SPRITE))
              keys[s.id] = (s.mod ? MOD[s.mod] : '') + s.key;
        }
        wizkidKeys = keys;
      } catch (e) {}
      wizkidKeyAt = Date.now(); wizkidKeyBusy = false;
    })();
  }
  function wizkidKeyTxt(sprite) { return wizkidKeys[sprite] ? ' (keybind: ' + wizkidKeys[sprite] + ')' : ''; }
  async function wizkidTalkNpc(name, label, tx, ty, tp, ...extra) {
    return qgDialogNpc(name, label, tx, ty, tp, 'wiz kid', ...extra);
  }
  async function wizkidStep() {
    wizkidHaveRefresh();
    let vbm = {}; try { vbm = await readVarbitValues([WIZKID_PROG, WIZKID_BOOK_VB]); } catch (e) { return; }
    const v = vbm[WIZKID_PROG] | 0;
    if (v !== wizkidV) { wizkidV = v; qgSig = ''; questDetailSig = ''; }
    if (v < 40) { wizkidHadBunch = false; wizkidFlowersPlaced = false; }
    if (v < 60) { wizkidHadBones = false; wizkidBonesPlaced = false; }
    // The quest's spells exist only on the standard spellbook: alert on any other book.
    const wrongBook = (vbm[WIZKID_BOOK_VB] | 0) !== 0 && v < WIZKID_DONE;
    if (wrongBook) hudSet(WIZKID_MAGIC_SPRITE, 'Switch to the standard spellbook for Wiz Kid', true);
    else if (v !== 20 && v !== 40 && v !== 60 && hudShown) hudSet(0, '', false);   // 20/40/60 manage the HUD themselves (cast prompts)
    if (v < 5) { await wizkidTalkNpc('Ben', 'Talk to Ben', 3562, 1479, 0); return; }   // not started -> Ben on Marigold Farm
    if (v === 5) {
      if (wizkidTeachPick < 0) wizkidTeachPick = hosRand(WIZKID_TEACH_OPTS.length);
      await wizkidTalkNpc('Ben', 'Talk to Ben', 3562, 1479, 0, WIZKID_TEACH_OPTS[wizkidTeachPick]);
      return;
    }
    if (v === 10) {   // give Ben the runes (choosers: Accept / "Yes, I brought enough runes for me too.")
      await wizkidTalkNpc('Ben', 'Give Ben the runes', 3562, 1479, 0, 'accept', 'yes, i brought enough runes');
      return;
    }
    if (v === 15) {   // inside the instance -> outline Ben where he stands (no tile fallback); chooser: Yes.
      await wizkidTalkNpc('Ben', 'Talk to Ben', 0, 0, 0, 'yes');
      return;
    }
    if (v === 20) {   // in Ben's instance: cast the taught teleport; back in the world: continue with Ben ("Yes.")
      if (qgInInstance(qgP) && qgSceneNpc(await qgScene(), 'Ben')) {
        qgClrNpc(); qgClrTiles(); qgClrDlg(); qgClrItem();
        if (!wrongBook) {
          wizkidKeyRefresh();
          hudSet(WIZKID_TELE_SPRITE, 'Cast Wendlewick Teleport' + wizkidKeyTxt(WIZKID_TELE_SPRITE), true);
        }
        return;
      }
      if (!wrongBook && hudShown) hudSet(0, '', false);
      await wizkidTalkNpc('Ben', 'Talk to Ben', 3562, 1479, 0, 'yes');
      return;
    }
    if (v === 25) {   // after the teleport lands -> back to Ben
      await wizkidTalkNpc('Ben', 'Talk to Ben', 3562, 1479, 0);
      return;
    }
    if (v === 30) {   // open the doors -> take the stairs -> Ben on the top floor (all read live from the scene)
      const sc = await qgScene();
      if ((qgP.p | 0) === 2 && qgSceneNpc(sc, 'Ben')) {
        await wizkidTalkNpc('Ben', 'Talk to Ben');
        return;
      }
      if (sc.objects.some(o => o && WIZKID_DOORS_OPEN.includes(o.id))) await qgObjectById(WIZKID_STAIRS, 'Climb top floor', sc.objects);
      else await qgObjectById(WIZKID_DOORS, 'Door\nOpen', sc.objects);
      return;
    }
    if (v === 35) {   // leave the tower: climb down, out through the doors (when north of them), back to Ben
      const sc = await qgScene();
      if ((qgP.p | 0) !== 0) {
        await qgObjectById(WIZKID_STAIRS_DOWN, 'Climb down to bottom', sc.objects);
        return;
      }
      // Stale spawn records keep the CLOSED ids listed after the swap, so the OPEN ids are the truth.
      const opened = sc.objects.some(o => o && WIZKID_DOORS_OPEN.includes(o.id));
      const doors = opened ? [] : sc.objects.filter(o => o && WIZKID_DOORS.includes(o.id) && (qgP.y | 0) > o.y - 1);
      if (doors.length) { await qgObjectById(WIZKID_DOORS, 'Door\nOpen', doors); return; }
      await wizkidTalkNpc('Ben', 'Talk to Ben', 3562, 1479, 0);
      return;
    }
    if (v === 40) {   // take the flowers -> place them on the stool -> back to Ben
      // Driven by observable state so an undone stage recovers; latches feed only the checkmarks.
      const sc = await qgScene();
      const stools = sc.objects.filter(o => o && o.id === WIZKID_STOOL);
      let ground = [];
      try { const gi = JSON.parse((await PLUGIN_API['state.groundItems'].run([], myPid())) || '[]');
            if (Array.isArray(gi)) ground = gi.filter(it => it && it.id === WIZKID_BUNCH && stools.some(s => s.x === it.x && s.y === it.y)); } catch (e) {}
      if (ground.length) {
        wizkidFlowersPlaced = true;
        qgClrNpc(); qgClrDlg(); qgClrItem();
        qgOv('overlay.guideTiles', ground.map(it => ({ x: it.x, y: it.y, plane: it.plane || 0, label: 'Bunch of flowers' })));
        if (!wrongBook) {
          wizkidKeyRefresh();
          hudSet(WIZKID_TKGRAB_SPRITE, 'Cast Telekinetic Grab on the flowers' + wizkidKeyTxt(WIZKID_TKGRAB_SPRITE), true);
        }
        return;
      }
      if (!wrongBook && hudShown) hudSet(0, '', false);
      if ((await qgInvCount(WIZKID_BUNCH)) > 0) await qgObjectById(WIZKID_STOOL, 'Stool\nPlace flowers', stools);
      else await qgObjectById(WIZKID_FLOWERS, 'Flowers\nTake', sc.objects);
      return;
    }
    if (v === 45) {   // holding the bunch: place it on the stool again; otherwise it's placed -> Ben
      if ((await qgInvCount(WIZKID_BUNCH)) > 0) {
        let objs = [];
        try { const sc = JSON.parse((await PLUGIN_API['state.scene'].run([60], myPid())) || '{}');
              if (sc && Array.isArray(sc.objects)) objs = sc.objects; } catch (e) {}
        qgClrNpc(); qgClrDlg(); qgClrItem();
        const marks = objs.filter(o => o && o.id === WIZKID_STOOL).map(o => ({ x: o.x, y: o.y, plane: o.plane || 0, label: 'Stool\nPlace flowers' }));
        if (marks.length) qgOv('overlay.guideTiles', marks); else qgClrTiles();
        return;
      }
      await wizkidTalkNpc('Ben', 'Talk to Ben');
      return;
    }
    if (v >= 50 && v < 60) {   // keep the conversation with Ben going
      await wizkidTalkNpc('Ben', 'Talk to Ben');
      return;
    }
    if (v === 60) {   // crow bones: dig 2 sets -> place ONE on the stool (Ben's) -> cast B2B on the carried set
      const sc = await qgScene();
      const stools = sc.objects.filter(o => o && o.id === WIZKID_STOOL);
      let onStool = false;
      try { const gi = JSON.parse((await PLUGIN_API['state.groundItems'].run([], myPid())) || '[]');
            if (Array.isArray(gi)) onStool = gi.some(it => it && it.id === WIZKID_CROWBONES && stools.some(s => s.x === it.x && s.y === it.y)); } catch (e) {}
      if (onStool) wizkidBonesPlaced = true;
      const bones = await qgInvCount(WIZKID_CROWBONES);
      if (bones >= 2) wizkidHadBones = true;
      if (bones > 0 && onStool) {
        qgItem(WIZKID_CROWBONES, 'Cast Bones to Bananas');
        if (!wrongBook) {
          wizkidKeyRefresh();
          hudSet(WIZKID_B2B_SPRITE, 'Cast Bones to Bananas' + wizkidKeyTxt(WIZKID_B2B_SPRITE), true);
        }
        return;
      }
      if (!wrongBook && hudShown) hudSet(0, '', false);
      if (bones > 0) await qgObjectById(WIZKID_STOOL, 'Stool\nPlace bones', stools);
      else await qgObjectById(WIZKID_MOUND, 'Dirt mound\nDig', sc.objects);
      return;
    }
    if (v >= 65 && v < 75) {   // lesson done -> back to Ben, keep the dialogue going
      await wizkidTalkNpc('Ben', 'Talk to Ben');
      return;
    }
    if (v === 75) {   // Wizards' Tower floor 3 (= plane 2) -> Archmage Sedridor ("Talk about 'Wiz Kid'.")
      // The fallback marker draws on the PLAYER'S plane, so near the tower the label names his floor.
      if ((qgP.p | 0) !== 2 && qgP.x < 6400 &&
          Math.abs(qgP.x - 3097) <= 40 && Math.abs(qgP.y - 3147) <= 40) {
        qgClrNpc(); qgClrDlg(); qgClrItem();
        const up = (qgP.p | 0) < 2;
        qgOv('overlay.guideTiles', [{ x: 3097, y: 3147, plane: qgP.p | 0,
          label: 'Archmage Sedridor\n' + (up ? 'UP' : 'DOWN') + ' on floor 3 - ' + (up ? 'Ascend' : 'Descend') + ' using the Beam' }]);
        return;
      }
      await wizkidTalkNpc('Archmage Sedridor', 'Talk to Archmage Sedridor', 3097, 3147, 2);
      return;
    }
    if (v === 80) {   // beam down to floor 2 (plane 1), then Wizard Isidor
      if ((qgP.p | 0) === 1) {
        await wizkidTalkNpc('Wizard Isidor', 'Talk to Wizard Isidor');
        return;
      }
      let objs = [];
      try { const sc = JSON.parse((await PLUGIN_API['state.scene'].run([60], myPid())) || '{}');
            if (sc && Array.isArray(sc.objects)) objs = sc.objects; } catch (e) {}
      qgClrNpc(); qgClrDlg(); qgClrItem();
      const marks = objs.filter(o => o && o.name && String(o.name).toLowerCase() === 'beam')
                        .map(o => ({ x: o.x, y: o.y, plane: o.plane || 0, label: 'Beam\nDescend' }));
      if (marks.length) qgOv('overlay.guideTiles', marks); else qgClrTiles();
      return;
    }
    if (v === 85 || v === 90) {   // back to Ben on Marigold Farm (chooser: "Continue 'Wiz Kid'?" -> Yes.)
      await wizkidTalkNpc('Ben', 'Talk to Ben', 3562, 1479, 0, 'yes');
      return;
    }
    qgClrNpc(); qgClrTiles(); qgClrDlg(); qgClrItem();   // past the last captured step -> no overlay
    if (!wrongBook && hudShown) hudSet(0, '', false);
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { wizkidItems, wizkidStep });
})();
