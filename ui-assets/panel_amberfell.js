// RuneToolsX panel: Secrets of Amberfell quest guide (quest 531).
// Spliced inline into client.html; IIFE (window exports + registerTab; see the RTX registry in client.html).
(function () {

  // Progress varbit 52651 (from cache quest config js5-2 archive 35 file 531): start value 5,
  // complete at 170. Requires Hearts of Sanguine; Slayer 28, Hunter 32, Herblore 35, Construction 30.
  const AMBER_PROG = 52651, AMBER_DONE = 170;
  const AMBER_BRIDGE = 60919;   // sub varbit: 0 -> 1 when the bridge is repaired
  const AMBER_ANYA = 52652;     // sub varbit: 1 -> 0 when Anya spawns in and follows the player
  // Interview questions: varp 12868 accumulates ONE bit per question asked and never resets,
  // so v=40 tracking is order-independent and survives relogs. 60920-60922 stay unused.
  const AMBER_Q = {
    rowan:   [60926, 60927, 60928],
    heather: [60931, 60932],
    fern:    [60935, 60936, 60937],
    heywood: [60923, 60929, 60930],
    ash:     [60933, 60934],          // 60934 = asked about last night
    sorrel:  [60924, 60925],          // the post-interview debrief (two chat stages)
  };
  const AMBER_Q_ALL = Object.values(AMBER_Q).flat();
  // Nine 4-bit "clue" counters store the ORDER each deduction fact was learned (1..9; 0 = not yet),
  // riding the top bits of the selection-pair varps: 60956/60957 Rowan, 60960 Heather, 60954/60955
  // Fern (varp 12870 b24-31), 60958/60959 Heywood, 60953 Ash, 60952 Sorrel (varp 12869 b24-31).
  // Deduction notes UI (group 518, positioned by varcs 6463/6464). Each suspect's two selections
  // (where / what doing) are varbits whose value is a suspect id bitmask (Fern 2, Ash 4, Rowan 8,
  // Heywood 16, Heather 32); a deduction is correct when BOTH equal the suspect's own id.
  // Entry: [vb option1, vb option2, correct value, comp option1, comp option2] (comps = the 195x30
  // option cells to box when wrong). Order matches the guide's five deduction steps.
  const AMBER_DEDUCTIONS = [   // [.., comp option1, comp option2, correct text 1, correct text 2]
    [60939, 60940, 2,  11, 12, 'In the woods', 'Gathering mushrooms'],           // Fern
    [60943, 60944, 4,  15, 16, 'By the river', 'Sending the message'],           // Ash
    [60947, 60948, 32, 20, 19, "In someone's house", 'Arguing with Sorrel'],     // Heather (top option = comp 20, bottom = 19)
    [60941, 60942, 8,  24, 23, 'In the vegetable patch', 'Tending the pumpkins'],// Rowan (top = 24, bottom = 23)
    [60945, 60946, 16, 29, 28, 'On patrol', 'Guarding the village'],             // Heywood (top = 29, bottom = 28)
  ];
  const AMBER_CHECK_COMP = 27;    // the Check button (176x38 layer)
  const AMBER_CHECKED = 60938;    // 1 = deductions submitted via the Check button (varp 12868 bit 18)
  const AMBER_TRUST_OPTS = ['i trust her', "she's a vampyre", "i don't trust her either"];   // v=62 Anya chooser -- any works
  let amberTrustPick = -1;
  const AMBER_TRUST = 60968;      // varp 12871 bits 27-30: which trust option was picked; > 0 = past that chooser
  const AMBER_WAVE = 54467;       // waves fight: counts waves cleared
  const AMBER_MOSS = 60918;       // v=115 sub varbit: 0 -> 1 after Mossbrain's first chat, 2 = offering bowl received
  const AMBER_MOTHS = 60917;      // v=115 sub varbit: spirit moths caught (0..5)
  // Highweald Forest hollow trees: [searched varbit (0 fresh, 1 searched), x, y]
  const AMBER_TREES = [[60849, 3557, 1620], [60850, 3545, 1657], [60851, 3511, 1642], [60852, 3487, 1680]];
  const AMBER_MEAT = 61623;       // Blessed raw rabbit meat (from checking the sprung snare) -> back to Joanna
  const AMBER_VIAL = 61626;       // Vial of blood (from Anya at v=140)
  const AMBER_ANTISANG = 60689;   // Antisanguine (3)
  const AMBER_EMPOWERED = 61627;  // Empowered antisanguine (vial of blood + antisanguine (3))
  const AMBER_ESTHER_OPTS = ['inanna gave it to me', 'anya helped'];   // Esther's mid-chat chooser -- either works
  let amberEstherPick = -1;
  const AMBER_INANNA150_OPTS = ['thank you for being honest', 'why are you telling me that now', "it's lady anastasia, then"];   // v=150 chooser -- any works, then "Yes."
  let amberInanna150Pick = -1;
  const AMBER_SORREL160_OPTS = ['you should be thanking ash', "you're welcome", "don't ever treat me like that again"];   // v=160 first chooser -- any works
  const AMBER_SORREL160B_OPTS = ["don't call her that", 'say nothing'];   // v=160 second chooser -- any works
  let amberSorrel160Pick = -1, amberSorrel160bPick = -1;
  const AMBER_DEDUCT_COMPS = '11,12,15,16,19,20,23,24,28,29,' + AMBER_CHECK_COMP;
  const AMBER_DEDUCT_VBS = AMBER_DEDUCTIONS.flatMap(d => [d[0], d[1]]);
  let amberDen = false; // latch: Inanna seen in scene = inside Inanna's Den (prayed at the altar)
  let amberDenAt = -1;  // the AMBER_PROG value the den latch was set at (reset when the value moves)
  // The bridge repair takes 20 nails of ONE kind: steel by default, or whichever kind is already
  // in the backpack. All seven smithable kinds count.
  const AMBER_NAILS = [
    { id: 1539, name: 'Steel nails' },
    { id: 4819, name: 'Bronze nails' },
    { id: 4820, name: 'Iron nails' },
    { id: 4821, name: 'Black nails' },
    { id: 4822, name: 'Mithril nails' },
    { id: 4823, name: 'Adamant nails' },
    { id: 4824, name: 'Rune nails' },
  ];
  let amberNailId = 0, amberNailAt = 0, amberNailBusy = false;   // 0 = none detected -> steel default
  function amberNail() { return AMBER_NAILS.find(n => n.id === amberNailId) || AMBER_NAILS[0]; }
  // Backpack scan for a carried nail kind (async, throttled); a change invalidates the guide
  // signatures so the next poll re-renders with the new kind.
  function amberNailRefresh() {
    if (amberNailBusy || Date.now() - amberNailAt < 2000) return;
    if (!bridge() || typeof PLUGIN_API === 'undefined') return;
    amberNailBusy = true;
    (async () => {
      try {
        const inv = JSON.parse((await PLUGIN_API['state.inventory'].run([], myPid())) || '{}');
        let found = 0;
        if (inv && Array.isArray(inv.items))
          for (const it of inv.items) { if (AMBER_NAILS.some(n => n.id === it[1])) { found = it[1]; break; } }
        if (found !== amberNailId) { amberNailId = found; qgSig = ''; questDetailSig = ''; }
      } catch (e) {}
      amberNailAt = Date.now(); amberNailBusy = false;
    })();
  }
  // Quick-guide item rows; section titles match the wiki quick-guide data, '' = the whole-quest list.
  function amberItems(section) {
    amberNailRefresh();
    const nail = amberNail();
    const rope  = { id: 954,   n: 2,  name: 'Rope' };
    const logs  = { id: 1511,  n: 10, name: 'Logs (any kind, all the same)' };
    const nails = { id: nail.id, n: 20, name: nail.name + (amberNailId ? '' : ' (or any one kind)') };
    const fish  = { id: 333,   n: 5,  name: 'Cooked fish (any)' };
    const net   = { id: 10010, n: 1,  name: 'Butterfly net' };
    const tease = { id: 10029, n: 1,  name: 'Teasing stick (or a ferret)' };
    const vial  = { id: 229,   n: 1,  name: 'Vial' };
    const anti  = { id: 60689, n: 1,  name: 'Antisanguine (3)' };
    switch (section) {
      case '':                       return [rope, logs, nails, fish, net, tease, vial, anti,
                                             { name: 'Combat gear' }, { name: '1 backpack space' }];
      case 'Getting started':        return [rope, logs, nails, { name: '1 backpack space' }];
      case 'Fighting the vampyres':  return [{ name: 'Combat gear' }];
      case 'Healing Inanna':         return [fish, net, tease, vial, anti];
    }
    return null;
  }
  // Quest-topic NPC: box any chooser option naming the quest, else highlight the NPC.
  async function amberTalkNpc(name, label, tx, ty, tp, ...extra) {
    return qgDialogNpc(name, label, tx, ty, tp, 'secrets of amberfell', ...extra);
  }
  // From v=50 the quest runs inside the temple instance: an open chooser gets boxed, otherwise
  // the NPC is outlined when instanced, and a player back in the world is pointed at the
  // temple quest marker to re-enter.
  async function amberInstanceStep(npc, label, ...opts) {
    let boxed = false; try { boxed = await PLUGIN_API['overlay.highlightOption'].run(['yes', ...opts], myPid()); } catch (e) {}
    if (boxed) { qgClrNpc(); qgClrTiles(); qgClrItem(); return; }
    let loaded = false;
    try { const sc = JSON.parse((await PLUGIN_API['state.scene'].run([60], myPid())) || '{}');
          loaded = !!(sc && Array.isArray(sc.npcs) && sc.npcs.some(n => n && n.name && String(n.name).toLowerCase() === npc.toLowerCase())); } catch (e) {}
    if (loaded || qgInInstance(qgP)) await qgNpc(npc, label);
    else qgObject('Secrets of Amberfell', 'Continue quest', 3743, 1658, 0);
  }
  async function amberStep() {
    let vbm = {}; try { vbm = await readVarbitValues([AMBER_PROG, AMBER_ANYA, AMBER_MOSS, AMBER_MOTHS]); } catch (e) { return; }
    const v = vbm[AMBER_PROG] | 0, anya = vbm[AMBER_ANYA] | 0;
    if (v !== amberDenAt) amberDen = false;
    // The quest Ranger is NPC 32560; match by id, not name -- several unrelated NPCs contain "Ranger".
    if (v === 0) { await amberTalkNpc('#32560', 'Talk to the Ranger', 3644, 1589, 0); return; }
    if (v === 5) { await amberTalkNpc('#32560', 'Continue dialogue with the Ranger', 3644, 1589, 0); return; }
    if (v === 10) { await amberTalkNpc('Adam', 'Take the S.O.S. note to Adam', 3484, 1574, 0); return; }
    if (v === 15) { await amberTalkNpc('Adam', 'Talk to Adam about the required items', 3484, 1574, 0); return; }
    if (v === 20) { await amberTalkNpc('#32560', 'Talk to the Ranger to repair the bridge (2x rope, 10x logs, 20x nails)', 3644, 1589, 0); return; }
    if (v === 25) {
      await amberTalkNpc('Adam', anya === 0 ? 'Continue the conversation with Adam' : 'Report the findings to Adam', 3484, 1574, 0);
      return;
    }
    if (v === 30) {
      if (anya === 1) { await amberTalkNpc('Anya', 'Talk to Anya to bring her along', 3600, 1381, 0); return; }
      const P = qgP;
      if (P && (P.p | 0) === 0 && P.x >= 3660) qgTile(3682, 1570, 0, 'Walk here to trigger a cutscene with Inanna');
      else qgObject('Bridge', 'Cross to Amberfell', 3656, 1589, 0);
      return;
    }
    if (v === 35) { await amberTalkNpc('Sorrel', 'Talk to Sorrel', 3732, 1573, 0); return; }
    if (v === 36 || v === 37) { await amberTalkNpc('Inanna', 'Talk to Inanna for the deduction notes', 3729, 1590, 0); return; }   // north of the well (37 = mid-dialogue; the notes, item 61620, arrive before 40)
    if (v === 40) {   // interviews: each NPC's own accumulator bits gate its step, so any order works
      let sv = {}; try { sv = await readVarbitValues([...AMBER_Q_ALL, AMBER_CHECKED]); } catch (e) {}
      const asked = ids => ids.every(id => (sv[id] | 0) === 1);
      // The interviewees share option TEXTS, so needles must come from the NPC actually being
      // talked to. The chooser carries no NPC name, so the speaking NPC is taken to be the nearest
      // interviewee in dialogue range; highlightOption no-ops when nothing matches.
      const AMBER_INTERVIEWS = [
        ['Farmer Rowan', 'rowan',   ['are these your pumpkins', 'can you tell me where everyone was last night', 'bye']],
        ['Heather',      'heather', ["i'm looking for someone who contacted wendlewick", 'bye']],
        ['Fern',         'fern',    ["i'm looking for someone who contacted wendlewick", 'can you tell me where everyone was last night', 'bye']],
        ['Heywood',      'heywood', ["i'm looking for someone who contacted wendlewick", 'can you tell me where everyone was last night', 'bye']],
        ['Ash',          'ash',     ['can you tell me where everyone was last night', 'bye']],
      ];
      const sc = await qgScene();
      let nearSpec = null, nearD = 1e9;
      for (const spec of AMBER_INTERVIEWS) {
        if (asked(AMBER_Q[spec[1]])) continue;
        const n = sc.npcs.find(x => x && x.name && String(x.name).toLowerCase() === spec[0].toLowerCase());
        if (!n || !P) continue;
        const d = Math.max(Math.abs(n.x - P.x), Math.abs(n.y - P.y));
        if (d < nearD) { nearD = d; nearSpec = spec; }
      }
      if (nearSpec) {
        let boxed = false; try { boxed = await PLUGIN_API['overlay.highlightOption'].run(nearSpec[2], myPid()); } catch (e) {}
        if (boxed) { qgClrNpc(); qgClrTiles(); qgClrItem(); return; }
      }
      if (!asked(AMBER_Q.rowan)) { await qgNpc('Farmer Rowan', 'Talk to Rowan by the pumpkin patch', 3731, 1565, 0); return; }
      if (!asked(AMBER_Q.heather)) { await qgNpc('Heather', 'Talk to Heather in the house to the south-west', 3724, 1556, 0); return; }
      if (!asked(AMBER_Q.fern)) { await qgNpc('Fern', 'Talk to Fern, north-east of the well', 3745, 1575, 0); return; }
      if (!asked(AMBER_Q.heywood)) { await qgNpc('Heywood', 'Talk to Heywood (roams around Amberfell, carrying a bow)'); return; }
      if (!asked(AMBER_Q.ash)) { await qgNpc('Ash', 'Talk to Ash at the barricade to the south-east', 3760, 1563, 0); return; }
      if (!asked(AMBER_Q.sorrel)) {
        await qgDialogNpc('Sorrel', 'Talk to Sorrel', 3732, 1573, 0,
                           "i'm just here to help", "i'll leave you be");
        return;
      }
      if ((sv[AMBER_CHECKED] | 0) !== 1) {
        // When the notes UI (group 518) is open, box the first option cell whose selection varbit is
        // wrong; all ten correct -> box the Check button. UI closed -> highlight the notes item.
        let dj = null; try { dj = JSON.parse((await PLUGIN_API['state.interface'].run([518, AMBER_DEDUCT_COMPS], myPid())) || '{}'); } catch (e) {}
        if (dj && dj.open && dj.hasAbs && Array.isArray(dj.comps)) {
          let dvb = {}; try { dvb = await readVarbitValues(AMBER_DEDUCT_VBS); } catch (e) {}
          let compId = 0, label = '';
          for (const [v1, v2, want, c1, c2, l1, l2] of AMBER_DEDUCTIONS) {
            if ((dvb[v1] | 0) !== want) { compId = c1; label = 'Set: ' + l1; break; }
            if ((dvb[v2] | 0) !== want) { compId = c2; label = 'Set: ' + l2; break; }
          }
          if (!compId) { compId = AMBER_CHECK_COMP; label = 'All correct - press Check'; }
          const c = dj.comps.find(k => k.comp === compId && k.w > 0);
          qgClrNpc(); qgClrTiles(); qgClrDlg();
          // Same overlay channel as highlightItem, so qgClrItem clears it.
          if (c) { try { bridge().panelViz(myPid(), c.x + ',' + c.y + ',' + c.w + ',' + c.h + ',' + label); } catch (e) {} }
          else qgClrItem();
          return;
        }
        qgItem(61620, 'Open the deduction notes');   // labels are clamped, keep short
        return;
      }
      await amberTalkNpc('Ash', 'Talk to Ash', 3760, 1563, 0);
      return;
    }
    if (v === 45) {
      let boxed = false; try { boxed = await PLUGIN_API['overlay.highlightOption'].run(['yes'], myPid()); } catch (e) {}
      if (boxed) { qgClrNpc(); qgClrTiles(); qgClrItem(); return; }
      qgObject('Secrets of Amberfell', 'Continue quest', 3743, 1658, 0);
      return;
    }
    if (v === 50) { await amberInstanceStep('Ash', 'Talk to Ash', "i've heard enough"); return; }
    if (v === 52 || v === 55 || v === 57) {
      await amberInstanceStep('Ash', 'Continue the dialogue with Ash',
                              'secrets of amberfell', 'what do you want from me', 'say nothing');
      return;
    }
    if (v === 60) { await amberInstanceStep('Anya', 'Talk to Anya', 'say nothing'); return; }
    if (v === 62) {
      let t = 0; try { t = (await readVarbitValues([AMBER_TRUST]))[AMBER_TRUST] | 0; } catch (e) {}
      if (t === 0) {
        if (amberTrustPick < 0) amberTrustPick = hosRand(AMBER_TRUST_OPTS.length);
        await amberInstanceStep('Anya', 'Talk to Anya', AMBER_TRUST_OPTS[amberTrustPick]);
      } else {
        await amberInstanceStep('Sorrel', 'Continue dialogue with Sorrel', 'secrets of amberfell');
      }
      return;
    }
    if (v === 64 || v === 66 || v === 68) { await amberInstanceStep('Sorrel', 'Continue dialogue with Sorrel', 'secrets of amberfell'); return; }
    if (v === 70) { await amberTalkNpc('Inanna', 'Begin the fight (combat gear; havensilver optional)', 3748, 1655, 0, 'yes'); return; }
    if (v >= 75 && v <= 85) {   // waves fight: 75 fight start, 80 at AMBER_WAVE=2, 85 at 3 (waves 1 and 4 move only the
// counter). A crawler-only wave must be finished with Inanna's special move or the fight
// loops forever.
      let crawler = null, vamp = null;
      try { const sc = JSON.parse((await PLUGIN_API['state.scene'].run([60], myPid())) || '{}');
            if (sc && Array.isArray(sc.npcs)) {
              crawler = sc.npcs.find(n => n && n.name && /sanguine crawler/i.test(n.name));
              vamp = sc.npcs.find(n => n && n.name && /vampyr/i.test(n.name));
            } } catch (e) {}
      let w = 0; try { w = (await readVarbitValues([AMBER_WAVE]))[AMBER_WAVE] | 0; } catch (e) {}
      const wl = w > 0 ? ' [waves down: ' + w + ']' : '';
      if (crawler && !vamp) {   // crawler-only wave: press the extra action button (Inanna's special) instead of fighting;
// fall back to the crawler outline until its origin varc publishes.
        if (qgExtraAction("Crawler-only wave: press this (Inanna's special)" + wl)) return;
        await qgNpc(crawler.name, "Crawler-only wave: finish with Inanna's special move" + wl, crawler.x, crawler.y, 0); return;
      }
      if (crawler) { await qgNpc(crawler.name, 'Kill the Sanguine crawlers first (step out of the blue box)' + wl, crawler.x, crawler.y, 0); return; }
      if (vamp) { await qgNpc(vamp.name, 'Fight the vampyres (step out of the blue box)' + wl, vamp.x, vamp.y, 0); return; }
      await amberTalkNpc('Inanna', v === 75 && w === 0 ? 'Talk to Inanna to fight the waves' : 'No wave up: continue with Inanna', 3748, 1655, 0);
      return;
    }
    // 90 = final wave down (fight over), 95 = the cutscene played -> Inanna's post-fight talk moves it to 100.
    if (v === 90 || v === 95) { await amberTalkNpc('Inanna', 'Talk to Inanna after the fight', 3748, 1655, 0); return; }
    if (v === 100) { await amberTalkNpc('Sorrel', 'Return to Sorrel in Amberfell (fairy ring DLP)', 3732, 1573, 0); return; }
    if (v === 105) { await amberTalkNpc('Anya', 'Talk to Anya at her camp (fairy ring BKS)', 3600, 1381, 0); return; }
    if (v === 110) { await amberTalkNpc('Shrine Tender Joanna', 'Talk to Shrine Tender Joanna', 3545, 1425, 0); return; }
    // 115 = Joanna handed over the blessed rabbit snare (61622) + spirit jar (61624).
    if (v === 115) {
      const moss = vbm[AMBER_MOSS] | 0;   // 0 first chat pending, 1 bring any 5 cooked fish, 2 offering bowl received
      if (moss >= 2) {
        // Searching a tree spawns spirit moths as scene NPCs -> mark the nearest moth while any are
        // up; otherwise box the nearest UNSEARCHED tree (per-tree varbits).
        const P = qgP, caught = vbm[AMBER_MOTHS] | 0;
        const dist = (x, y) => P ? Math.abs(x - P.x) + Math.abs(y - P.y) : 0;
        if ((await qgInvCount(AMBER_MEAT)) > 0) { await amberTalkNpc('Shrine Tender Joanna', 'Take the blessed rabbit meat to Shrine Tender Joanna', 3545, 1425, 0); return; }
        let sc = null;
        try { sc = JSON.parse((await PLUGIN_API['state.scene'].run([60], myPid())) || '{}'); } catch (e) {}
        // Spawned snare locs (sceneEntities .objects): 137316 = laid, 134263 = sprung.
        const objs = (sc && Array.isArray(sc.objects)) ? sc.objects : [];
        const snare = objs.find(o => o && o.id === 134263);
        if (snare) { qgObject('Blessed rabbit snare', 'Check the snare', snare.x, snare.y, snare.plane | 0); return; }
        if (caught >= 5) {
          if (objs.some(o => o && o.id === 137316)) {
            const bs = objs.filter(o => o && o.name && /jackalope burrow/i.test(o.name))
                           .sort((a, b) => dist(a.x, a.y) - dist(b.x, b.y));
            if (bs[0]) { qgObject('Jackalope burrow', 'Flush (teasing stick)', bs[0].x, bs[0].y, bs[0].plane | 0); return; }
            qgTile(3524, 1403, 0, 'Flush a Jackalope burrow with your teasing stick'); return;
          }
          if (P && P.x === 3524 && P.y === 1403 && (P.p | 0) === 0) { qgItem(61622, 'Lay trap'); return; }
          qgTile(3524, 1403, 0, 'Stand here, then lay the blessed rabbit snare');
          return;
        }
        let moth = null;
        if (sc && Array.isArray(sc.npcs)) {
          const ms = sc.npcs.filter(n => n && n.name && /spirit moth/i.test(n.name));
          ms.sort((a, b) => dist(a.x, a.y) - dist(b.x, b.y));
          moth = ms[0] || null;
        }
        if (moth) { await qgNpc(moth.name, 'Catch the spirit moths (' + caught + '/5)', moth.x, moth.y, 0); return; }
        let tvb = {}; try { tvb = await readVarbitValues(AMBER_TREES.map(t => t[0])); } catch (e) {}
        const fresh = AMBER_TREES.filter(t => (tvb[t[0]] | 0) === 0).sort((a, b) => dist(a[1], a[2]) - dist(b[1], b[2]));
        const t = fresh[0] || AMBER_TREES.slice().sort((a, b) => dist(a[1], a[2]) - dist(b[1], b[2]))[0];
        qgObject('Hollow tree', fresh.length
          ? (caught > 0 ? 'Search to spawn more spirit moths (' + caught + '/5)' : 'Equip your butterfly net, then Search to spawn spirit moths')
          : 'All trees searched (' + caught + '/5)', t[1], t[2], 0);
        return;
      }
      await amberTalkNpc('Mossbrain', moss === 1 ? 'Give Mossbrain any 5 cooked fish' : 'Talk to Mossbrain at the goblin camp', 3647, 1364, 0);
      return;
    }
    // Den entry: box an open "Yes" chooser; latch once Inanna is in scene (she exists only inside
    // the Den) and target her; otherwise the Altar of Inanna.
    const amberDenEntry = async (label, opts = [], onDen = null) => {
      let boxed = false; try { boxed = await PLUGIN_API['overlay.highlightOption'].run(['yes', ...opts], myPid()); } catch (e) {}
      if (boxed) { qgClrNpc(); qgClrTiles(); qgClrItem(); return; }
      let altar = null, inanna = false;
      try { const sc = JSON.parse((await PLUGIN_API['state.scene'].run([60], myPid())) || '{}');
            if (sc && Array.isArray(sc.npcs)) inanna = sc.npcs.some(n => n && n.name === 'Inanna');
            if (sc && Array.isArray(sc.objects)) altar = sc.objects.find(o => o && o.name && /altar of inanna/i.test(o.name)); } catch (e) {}
      if (inanna) { amberDen = true; amberDenAt = v; }
      if (amberDen) { if (onDen) await onDen(); else await qgDialogNpc('Inanna', label, 0, 0, 0, ...opts); return; }
      if (altar) qgObject('Altar of Inanna', "Pray to enter Inanna's Den", altar.x, altar.y, altar.plane | 0);
      else qgTile(3545, 1425, 0, "Pray at the Altar of Inanna to enter Inanna's Den");
    };
    if (v === 120) { await amberDenEntry('Talk to Inanna', ['move on', "let's do it"]); return; }
    // 125-135 = the Anya vial chain in the Den: 125 give the empty vial, 130/132 mid-dialogue,
    // 135 receive the vial of blood; at 140 the Vial of blood (61626) is held.
    if (v === 125 || v === 130 || v === 132 || v === 135) {
      await amberTalkNpc('Anya', v === 125 ? 'Give Anya the empty vial'
                               : v === 135 ? 'Talk to Anya to receive the vial of blood'
                                           : 'Continue with Anya', 0, 0, 0);
      return;
    }
    // Esther at the top of the Wendlewick lighthouse: walk-to -> stairs -> Esther on plane 1.
    const amberEsther = async (label) => {
      const LP = qgP;
      const near = (x, y, p, r) => !!(LP && (LP.p | 0) === p && Math.abs(LP.x - x) <= r && Math.abs(LP.y - y) <= r);
      if (near(3454, 1495, 1, 20)) {
        if (amberEstherPick < 0) amberEstherPick = hosRand(AMBER_ESTHER_OPTS.length);
        await amberTalkNpc('Esther', label, 3457, 1500, 1, AMBER_ESTHER_OPTS[amberEstherPick], "i'll get going on that");
        return;
      }
      if (near(3449, 1495, 0, 52)) { qgObject('Stairs', 'Climb the lighthouse stairs', 3449, 1495, 0); return; }
      qgTile(3452, 1494, 0, 'Go to the Wendlewick lighthouse, then climb the stairs');
    };
    if (v === 140) { await amberEsther('Talk to Esther'); return; }
    if (v === 150) {
      if (amberInanna150Pick < 0) amberInanna150Pick = hosRand(AMBER_INANNA150_OPTS.length);
      await amberDenEntry('Talk to Inanna', [AMBER_INANNA150_OPTS[amberInanna150Pick], 'yes']);
      return;
    }
    if (v === 152) { await qgDialogNpc('Anya', 'Talk to Anya to leave the Den', 0, 0, 0, 'yes'); return; }
    if (v === 155 || v === 160 || v === 165) {
      let used = -1;
      try { const inv = JSON.parse(await bridge().inventory(myPid())); if (inv && Array.isArray(inv.items)) used = inv.items.filter(it => it[1] > 0).length; } catch (e) {}
      const free = used >= 0 ? 28 - used : -1;
      const warn = (free >= 0 && free < 6) ? ' - FREE UP ' + (6 - free) + ' BACKPACK SLOTS FIRST' : '';
      if (amberSorrel160Pick < 0) amberSorrel160Pick = hosRand(AMBER_SORREL160_OPTS.length);
      if (amberSorrel160bPick < 0) amberSorrel160bPick = hosRand(AMBER_SORREL160B_OPTS.length);
      await amberTalkNpc('Sorrel', (v >= 160 ? 'Continue with Sorrel' : 'Talk to Sorrel in Amberfell (fairy ring DLP)') + warn, 3732, 1573, 0,
                         AMBER_SORREL160_OPTS[amberSorrel160Pick], AMBER_SORREL160B_OPTS[amberSorrel160bPick]);
      return;
    }
    if (v === 143 || v === 145) {
      if ((await qgInvCount(AMBER_EMPOWERED)) > 0) {
        const LP = qgP;   // stairs only at the lighthouse TOP (the Den instance is plane 1 too -- plane alone misfires there)
        const atLighthouse = !!(LP && (LP.p | 0) === 1 && Math.abs(LP.x - 3454) <= 20 && Math.abs(LP.y - 1495) <= 20);
        if (atLighthouse) { qgObject('Stairs', 'Climb down the lighthouse stairs', 3449, 1495, 1); return; }
        await amberDenEntry('', [], async () => qgItem(AMBER_EMPOWERED, 'Use on Inanna'));
        return;
      }
      await qgItems([[AMBER_ANTISANG, 'Use on the Vial of blood'], [AMBER_VIAL, '']]);   // blank target label -> no overlap with the action label
      return;
    }
    switch (v) {
      default: qgClearAll(); break;
    }
  }
  // Step i = flat index across sections. QG_AUTO is declared in the earlier-spliced panel_visions.js.
  QG_AUTO['Secrets of Amberfell'] = {
    vbs: [AMBER_PROG, ...AMBER_Q_ALL, ...AMBER_DEDUCT_VBS, AMBER_CHECKED, AMBER_MOSS, AMBER_MOTHS],
    inv: true,
    done: (vb, inv) => {
      const v = vb[AMBER_PROG] | 0;
      const at40 = (cond) => v > 40 || (v === 40 && cond);
      const q = ids => ids.every(id => (vb[id] | 0) === 1);  // all of an NPC's interview questions asked
      const s = new Set();
      if (v >= 5) s.add(0);    // Talk to the ranger next to the bridge south of Hollow Hill
      if (v >= 15) s.add(1);   // Take the S.O.S. note to Adam in Wendlewick
      if (v >= 25) s.add(2);   // Go back and talk to the ranger with the materials, repair the bridge
      if (v >= 30) s.add(3);   // Go back and talk to Adam to report the findings
      if (v >= 35) { s.add(4); s.add(5); }   // Return to the bridge and cross + Head south, cutscene with Inanna (crossing is entailed by the cutscene)
      if (v >= 36) s.add(6);   // Continue east and speak to Sorrel next to the well
      if (v >= 40) s.add(7);   // Talk to Inanna, north of the well, to obtain deduction notes
      if (at40(q(AMBER_Q.rowan))) s.add(8);    // Talk to Rowan, next to the pumpkin patch
      if (at40(q(AMBER_Q.heather))) s.add(9);  // Talk to Heather, in the house to the southwest
      if (at40(q(AMBER_Q.fern))) s.add(10);    // Talk to Fern, to the north-east of the well
      if (at40(q(AMBER_Q.heywood))) s.add(11); // Talk to Heywood, who roams around Amberfell
      if (at40(q(AMBER_Q.ash))) s.add(12);     // Talk to Ash at the barricade to the south-east
      if (at40(q(AMBER_Q.sorrel))) s.add(13);  // Talk to Sorrel (after the interviews)
      // Deduction steps: 14 = open the notes + check (all ten selections right), 15-19 = one per
      // suspect (both of that suspect's selection varbits at their id).
      const dedOk = AMBER_DEDUCTIONS.map(d => (vb[d[0]] | 0) === d[2] && (vb[d[1]] | 0) === d[2]);
      dedOk.forEach((ok, i) => { if (at40(ok)) s.add(15 + i); });
      if (at40((vb[AMBER_CHECKED] | 0) === 1)) s.add(14);   // notes opened, deductions selected AND checked
      if (v >= 45) s.add(20);  // Talk to Ash (start of Fighting the vampyres)
      if (v >= 50) s.add(21);  // Go north to the temple, continue on the quest marker
      if (v >= 70) s.add(22);  // Talk to Ash and progress through the dialogue (whole in-instance chain, done when the fight step opens)
      if (v >= 90) s.add(23);  // Talk to Inanna and fight the waves (90 = final wave down)
      if (v >= 100) s.add(24); // Talk to Inanna after the fight (cutscene at 95, talk moves it to 100)
      if (v >= 105) s.add(25); // Go back and talk to Sorrel in Amberfell
      if (v >= 110) s.add(26); // Go to Anya's camp and talk to Anya
      if (v >= 115) s.add(27); // Talk to Shrine Tender Joanna (blessed rabbit snare + spirit jar)
      if (v > 115 || (v === 115 && (vb[AMBER_MOSS] | 0) >= 1)) s.add(28); // Talk to Mossbrain at the goblin camp
      if (v > 115 || (v === 115 && (vb[AMBER_MOSS] | 0) >= 2)) s.add(29); // Talk to him again with 5 cooked fish (offering bowl)
      if (v > 115 || (v === 115 && (vb[AMBER_MOTHS] | 0) >= 1)) s.add(30); // Search the hollow tree (first catch implies it)
      if (v > 115 || (v === 115 && (vb[AMBER_MOTHS] | 0) >= 5)) s.add(31); // Catch 5 spirit moths
      if (v > 115 || (v === 115 && inv && inv.has(AMBER_MEAT))) s.add(32); // Jackalope caught (blessed raw rabbit meat held)
      if (v >= 120) s.add(33); // Go back and talk to Shrine Tender Joanna
      if (v > 120 || (v === 120 && amberDen)) s.add(34); // Pray at the Altar of Inanna (Inanna in scene = inside the Den)
      if (v >= 125) s.add(35); // Talk to Inanna in the Den (Move on > Let's do it)
      if (v >= 140) s.add(36); // Talk to Anya to receive the vial of blood (61626)
      if (v >= 145) s.add(37); // Go to the top of the Wendlewick lighthouse and talk to Esther
      if (v > 145 || ((v === 143 || v === 145) && inv && inv.has(AMBER_EMPOWERED))) s.add(38); // Combine the antisanguine with the vial of blood
      if (v >= 150) s.add(39); // Use the empowered antisanguine on Inanna in the Den
      if (v >= 155) s.add(40); // Talk to Inanna (Any > Yes), leave the Den via Anya
      if (v >= AMBER_DONE) { s.add(41); s.add(42); } // Go back and talk to Sorrel + Quest complete (170)
      return s;
    },
  };

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { amberItems, amberStep });
})();
