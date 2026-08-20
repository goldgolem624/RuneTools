// RuneToolsX panel: Havenhythe visual quest guides (Visions / Hearts of Sanguine / Hermit Permits).
// Spliced inline into client.html at load; shares its global scope (no IIFE).

  // Visions of Havenhythe (quest 526): runs while progress varbit 60595 is 1..50.
  const VOH_PROG = 60595, VOH_KILLS = 60596, VOH_LIMESTONE = 3211, VOH_BRICK = 3420, VOH_MEAD = 60403;
  const HOS_PROG = 60597, HOS_SUB = 60598, HOS_ALE = 60599, HOS_GIVE = 60600, HOS_MIRIAM = 60601, HOS_JACOB = 60603, HOS_GEFEN2 = 60602, HOS_ALE_ITEM = 60512, HOS_POPPY = 60408, HOS_SPINES = 60409, HOS_WOLFTONGUE = 60407, HOS_VIAL = 229, HOS_OILVIAL = 60420, HOS_STERVIAL = 60421, HOS_CRUSHED = 60414, HOS_MIXVIAL = 60422, HOS_PRESSED = 60413, HOS_MIXVIAL2 = 60423, HOS_WEIGHED = 60410, HOS_ANTISANG = 60424;   // Hearts of Sanguine sub-varbits + item ids
  // Starts true: overlay marks survive a panel reload, so the first idle tick must sweep them.
  let qgOn = true, qgP = null, hosTrail = false, hosDown = false, hosDown27 = false, hosTrust = 0, hosTumourPhase = 0, hosFarewell = 0;
  let hosOreMined = false;   // v=54 latch: 4 havensilver ore held
  let hosBarsSmelted = false; // v=60 latch: 4 havensilver bars across inventory + metal bank
  let hosSwordWorn = false;   // v=63 latch: greatsword 60298 equipped
  let hosBuffDrunk = false;   // v=66 latch: antisanguine buff varbit 60615 seen nonzero (survives buff expiry)
  const HOS_TRUST_OPTS = ['i have no reason not to trust', "i don't know if i can trust", "i don't trust her either"];   // v=63 trust dialogue: any option works
  const HOS_FAREWELL_OPTS = ['keeping an eye on you', 'some of my blood', 'farewell'];   // v=69 Anya farewell: any option works
  function qgRand(n) {
    try { return crypto.getRandomValues(new Uint32Array(1))[0] % n; } catch (e) {}
    try { return ((performance.now() * 1000) | 0) % n; } catch (e) {}
    return Date.now() % n;
  }
  // Four independent overlay channels; each helper sets one and clears the others.
  const qgOv = (cmd, ...a) => { try { return PLUGIN_API[cmd].run(a, myPid()); } catch (e) {} };
  const qgClrNpc = () => qgOv('overlay.highlight', []);
  const qgClrTiles = () => qgOv('overlay.guideTiles', []);
  const qgClrDlg = () => qgOv('overlay.highlightRect', 0, 0, 0, 0);
  const qgClrItem = () => qgOv('overlay.highlightItem', 0, '');
  function qgClearAll() { qgClrNpc(); qgClrTiles(); qgClrDlg(); qgClrItem(); if (hudShown) hudSet(0, '', false); }
  async function qgNpc(name, label, tx, ty, tp) {
    qgClrDlg(); qgClrItem();
    const wantId = (typeof name === 'string' && name[0] === '#') ? (parseInt(name.slice(1), 10) || 0) : 0;   // "#<id>" -> match by NPC model id, not name
    let loaded = false;
    try { const s = JSON.parse((await PLUGIN_API['state.scene'].run([60], myPid())) || '{}');
          if (s && Array.isArray(s.npcs)) loaded = wantId ? s.npcs.some(n => n && n.id === wantId) : s.npcs.some(n => n && n.name && String(n.name).toLowerCase() === name.toLowerCase()); } catch (e) {}
    if (loaded || !tx) { qgClrTiles(); qgOv('overlay.highlightNpc', name, label, tx, ty); }   // pass the target tile -> box the focused instance, not the nearest to the player
    else { qgClrNpc(); qgOv('overlay.guideTiles', [{ x: tx, y: ty, plane: tp || 0, label: label }]); }
  }
  // Compose a guide-tile label that reads ACTION first, then the object.
  //
  // The first line is NOT free text: the host matches it against the cache loc near the tile
  // to give the mark that object's 3D footprint prism, falling back to a flat tile when it
  // does not resolve (Reader.cpp, "when the label's first line names a cache loc..."). So the
  // name has to stay on line 1 -- but a leading '-' makes that line MATCH-ONLY and hides it,
  // and every later line still draws. Emitting '-name\naction\nname' therefore renders
  //     Climb
  //     Cliffside
  // while keeping the prism. Over the 95-char label cap we drop the repeated name rather than
  // let the host truncate mid-word; the box itself still identifies the object.
  function qgLabel(locName, action) {
    const n = String(locName || ''), a = String(action || '');
    if (!n) return a;
    if (!a) return n;
    const full = '-' + n + '\n' + a + '\n' + n;
    return full.length <= 95 ? full : ('-' + n + '\n' + a);
  }
  function qgObject(locName, label, x, y, p) {
    qgClrNpc(); qgClrDlg(); qgClrItem();
    const ax = (x != null) ? x : (qgP ? qgP.x : 0), ay = (x != null) ? y : (qgP ? qgP.y : 0), ap = (x != null) ? (p || 0) : (qgP ? (qgP.p || 0) : 0);
    if (ax) qgOv('overlay.guideTiles', [{ x: ax, y: ay, plane: ap, label: qgLabel(locName, label) }]); else qgClrTiles();
  }
  function qgTile(x, y, p, label) { qgClrNpc(); qgClrDlg(); qgClrItem(); qgOv('overlay.guideTiles', [{ x: x, y: y, plane: p || 0, label: label }]); }
  async function qgScene(range) {
    try { const sc = JSON.parse((await PLUGIN_API['state.scene'].run([range || 60], myPid())) || '{}');
          return { npcs: (sc && Array.isArray(sc.npcs)) ? sc.npcs : [], objects: (sc && Array.isArray(sc.objects)) ? sc.objects : [] }; }
    catch (e) { return { npcs: [], objects: [] }; }
  }
  function qgSceneNpc(sc, name) { const n = String(name).toLowerCase(); return sc.npcs.some(x => x && x.name && String(x.name).toLowerCase() === n); }
  async function qgGround() {
    try { const g = JSON.parse((await PLUGIN_API['state.groundItems'].run([], myPid())) || '[]'); return Array.isArray(g) ? g : []; }
    catch (e) { return []; }
  }
  function qgIfaceComp(group, comp, label) {
    let cr = null;
    try { cr = JSON.parse(bridge().ifaceCompRects(myPid(), group, String(comp), 0) || '{}'); } catch (e) {}
    const r = cr && cr.abs && cr.comps ? cr.comps[String(comp)] : null;
    if (!(r && r[2] > 0)) return false;
    qgClrNpc(); qgClrTiles(); qgClrDlg();
    try { bridge().panelViz(myPid(), r[0] + ',' + r[1] + ',' + r[2] + ',' + r[3] + ',' + label); } catch (e) {}
    return true;
  }
  function qgIdMarks(objs, ids, label) {
    const want = Array.isArray(ids) ? ids : [ids];
    return objs.filter(o => o && want.includes(o.id)).map(o => ({ x: o.x, y: o.y, plane: o.plane || 0, label: label }));
  }
  // False = no live instance in range and tiles cleared; the caller may draw a walk-to marker.
  async function qgObjectById(ids, label, objs) {
    if (!objs) objs = (await qgScene()).objects;
    qgClrNpc(); qgClrDlg(); qgClrItem();
    const marks = qgIdMarks(objs, ids, label);
    if (marks.length) qgOv('overlay.guideTiles', marks); else qgClrTiles();
    return marks.length > 0;
  }
  // Instanced content sits at x >= 6400; static underground areas only shift y, so x alone identifies an instance.
  function qgInInstance(P) { return !!(P && P.x >= 6400); }
  function qgItem(id, label) { qgClrNpc(); qgClrTiles(); qgClrDlg(); qgOv('overlay.highlightItem', id, label); }
  // Boxes multiple backpack slots at once: pairs = [[itemId, label],..].
  async function qgItems(pairs) {
    let inv = null; try { inv = JSON.parse(await bridge().inventory(myPid())); } catch (e) {}
    const slotOf = id => { if (inv && Array.isArray(inv.items)) for (const it of inv.items) if (it[1] === id) return it[0]; return -1; };
    const parts = [];
    for (const [id, label] of pairs) {
      const s = slotOf(id); if (s < 0) continue;
      let r = null; try { r = JSON.parse(await bridge().invSlotRect(myPid(), s)); } catch (e) {}
      if (r && r.w > 0) parts.push(r.x + ',' + r.y + ',' + r.w + ',' + r.h + ',' + String(label).replace(/[,|]/g, ' '));
    }
    qgClrNpc(); qgClrTiles(); qgClrDlg();
    try { bridge().panelViz(myPid(), parts.join('|')); } catch (e) {}
    return parts.length;
  }
  async function qgDialogOpt(text) { qgClrNpc(); qgClrTiles(); qgClrItem(); try { return await PLUGIN_API['overlay.highlightOption'].run([text], myPid()); } catch (e) { return false; } }
  // Extra action button (group 743, positioned by varc 6423/6424): component 7 = 38x38 button layer, root layer 0 = 150x56 with the hotkey label.
  function qgExtraAction(label) {
    qgClrNpc(); qgClrTiles();
    let cr = null;
    try { cr = JSON.parse(bridge().ifaceCompRects(myPid(), 743, '7', 0) || '{}'); } catch (e) {}
    const r = cr && cr.abs && cr.comps ? cr.comps['7'] : null;
    if (r && r[2] > 0) {
      if (label) { qgClrDlg(); try { bridge().panelViz(myPid(), r[0] + ',' + r[1] + ',' + r[2] + ',' + r[3] + ',' + label); } catch (e) {} }
      else { qgClrItem(); qgOv('overlay.highlightRect', r[0], r[1], r[2], r[3]); }
      return true;
    }
    qgClrDlg(); qgClrItem(); return false;
  }
  async function qgDialogNpc(name, label, tx, ty, tp, ...opts) {
    let boxed = false; try { boxed = await PLUGIN_API['overlay.highlightOption'].run(opts, myPid()); } catch (e) {}
    if (boxed) { qgClrNpc(); qgClrTiles(); qgClrItem(); return; }   
    await qgNpc(name, label, tx, ty, tp);                            // qgNpc overwrites rather than clear-then-redraw, so no flicker
  }
  async function qgTalkNpc(name, label, tx, ty, tp, ...extra) {
    return qgDialogNpc(name, label, tx, ty, tp, 'hearts of sanguine', ...extra);
  }
  async function hpTalkNpc(name, label, tx, ty, tp, ...extra) {
    return qgDialogNpc(name, label, tx, ty, tp, 'hermit permits', ...extra);
  }
  let hudShown = true;   // starts true: a panel reload resets it while the HUD may still be lit, so the first clear must go through
  function hudSet(sprite, caption, on) {
    try { if (bridge() && bridge().hudSprite) bridge().hudSprite(myPid(), on ? sprite : 0, on ? caption : '', !!on); } catch (e) {}
    hudShown = !!on;
  }
  // Lodestone-teleport HUD reminder while the player is >60 tiles from the named lodestone; true while it is up.
  async function qgLodestone(name) {
    const lo = (typeof LODESTONES !== 'undefined') ? LODESTONES.find(l => l.n === name) : null;
    if (!lo) return false;
    const P = qgP;
    const far = !(P && Math.abs(P.x - lo.x) <= 60 && Math.abs(P.y - lo.y) <= 60);   // X/Y only -- the lighthouse sits on the lodestone but is plane 1
    hudSet(lo.sp, 'Teleport to the ' + name + ' lodestone', far);
    return far;
  }
  async function qgInvCount(id) {
    let inv = null; try { inv = JSON.parse((await PLUGIN_API['state.inventory'].run([], myPid())) || '{}'); } catch (e) {}
    let n = 0; if (inv && Array.isArray(inv.items)) for (const it of inv.items) if (it[1] === id) n += (it[2] > 0 ? it[2] : 1);
    return n;
  }
  // Metal bank (container 858): deposited contents only, and only loaded at a furnace/forge/anvil.
  async function qgMetalBankCount(id) {
    let d = null; try { d = JSON.parse(await bridge().metalBankItems(myPid())); } catch (e) {}
    let n = 0; if (d && Array.isArray(d.items)) for (const it of d.items) if (it[1] === id) n += (it[2] > 0 ? it[2] : 1);
    return n;
  }
  async function qgEquipCount(id) {   // worn items (container 94)
    let eq = null; try { eq = JSON.parse(await bridge().equipment(myPid())); } catch (e) {}
    let n = 0; if (eq && Array.isArray(eq.items)) for (const it of eq.items) if (it[1] === id) n += (it[2] > 0 ? it[2] : 1);
    return n;
  }
  async function questGuideTick() {
    if (!bridge() || typeof PLUGIN_API === 'undefined' || activeClueId >= 0) return;
    if (bghGuideOn) {
      qgOn = true;
      qgP = await scanPlayerTile();
      try { await fetchBgh(); await bghGuideStep(); } catch (e) {}
      return;
    }
    if (typeof havenBghGuideOn !== 'undefined' && havenBghGuideOn) {
      qgOn = true;
      qgP = await scanPlayerTile();
      try { await fetchHavenBgh(); await havenBghGuideStep(); } catch (e) {}
      return;
    }
    let focused = ''; try { focused = qgFocusName(); } catch (e) {}
    const step = QUEST_GUIDES[focused];
    if (!step) { if (qgOn) { qgClearAll(); qgOn = false; } return; }
    qgOn = true;
    qgP = await scanPlayerTile();
    if (!qgP) { qgClearAll(); return; }
    await step();
    qgAutoTick(focused).catch(function () {});   
  }
  // Per quest: the varbits to read + done(vb) -> Set of completed step indices (i = step order across sections).
  let qgAutoDone = {};
  let vohMeadDown = false;   // v=9 "go downstairs" latch: plane 0 reached (no varbit for this step)
  let vohSailed = false;     // v=18 "Lorris / I'm ready" latch: arrived at the Havenhythe dock (3388,1530 +-40)
  let vohEstherUp = false;   // v=42 lighthouse latch: plane 1 reached with Esther in scene
  const QG_AUTO = {
    'Visions of Havenhythe': {
      vbs: [VOH_PROG, VOH_KILLS],
      done: (vb) => {
        const v = vb[VOH_PROG] | 0, kills = vb[VOH_KILLS] | 0;
        const s = new Set();
        // "Go downstairs" has no varbit: it completes on reaching plane 0 at v=9, latched.
        if (v < 9) vohMeadDown = false;
        if (v === 9 && qgP && (qgP.p | 0) === 0) vohMeadDown = true;
        if (v >= 3) s.add(0);                  // Talk to the Bartender (start)
        if (v >= 9) s.add(1);                  // Drink the dragonfire mead
        if (v > 9 || vohMeadDown) s.add(2);    // After the cutscene, go downstairs (east door)
        if (v >= 12) s.add(3);                 // Talk to the Bartender
        if (v >= 15) s.add(4);                 // Talk to 'Shady' Sullivan
        // Boarding with Lorris has no varbit: it completes on arrival at the dock (3388,1530 +-40), latched.
        if (v < 18) vohSailed = false;
        if (v >= 18 && qgP && (qgP.p | 0) === 0 &&
            Math.abs(qgP.x - 3388) <= 40 && Math.abs(qgP.y - 1530) <= 40) vohSailed = true;
        if (v > 18 || vohSailed) s.add(5);     // Talk to Captain Lorris ("I'm ready.") -> sailed
        if (v >= 21) s.add(6);                 // Talk to Old Man Jocko on the dock
        if (v >= 24) s.add(7);                 // Head east to Wendlewick, speak to Adam
        if (v >= 27) s.add(8);                 // Head south-east to the Shrine of Inanna, talk to Joanna
        if (v >= 30) s.add(9);                 // Pray at the Altar of Inanna
        if (v >= 33) s.add(10);                // Talk to Shrine Tender Joanna
        if (v >= 36) s.add(11);                // Return and talk to Adam
        if (v >= 39) s.add(12);                // Talk to Zeke in the building east of the well
        if (v >= 42 || kills >= 10) s.add(13); // Kill 10 juvenile boulder crabs (60596; v=42 keeps it done if the counter resets)
        if (v >= 42) s.add(14);                // Return to Zeke (limestone handed in)
        // The lighthouse climb has no varbit: plane 1 with Esther in scene (latch set in vohStep case 42).
        if (v < 42) vohEstherUp = false;
        if (v > 42 || vohEstherUp) s.add(15);  // Through the lighthouse, up the stairs
        if (v >= 45) s.add(16);                // Talk to Esther
        if (v >= 48) s.add(17);                // Go back downstairs, build at the Lodestone hotspot
        if (v >= 51) s.add(18);                // Quest complete!
        return s;
      }
    },
    'Hearts of Sanguine': {
      vbs: [60597, 60598, 60599, 60600, 60601, 60602, 60603, 60615], inv: true,
      done: (vb, inv) => {
        const v = vb[60597] | 0, sub = vb[60598] | 0, ale = vb[60599] | 0, give = vb[60600] | 0,
              miriam = vb[60601] | 0, gefen2 = vb[60602] | 0, jacob = vb[60603] | 0;
        if ((vb[60615] | 0) > 0) hosBuffDrunk = true;   // antisanguine buff up -> the drink step is done (latched past expiry)
        const at6 = (cond) => v > 6 || (v === 6 && cond);   
        const s = new Set();
        // Creepy behind
        if (v >= 6) s.add(0);            // Talk to Adam (start)
        if (at6(sub >= 1)) s.add(1);     // Go NE, talk to Raz in her cabin
        if (at6(sub >= 2)) s.add(2);     // Inspect the dead bear
        if (at6(sub >= 3)) s.add(3);     // Talk to Raz
        // Local gossip
        if (at6(ale >= 1)) s.add(4);     // Talk to Bartender Gefen (Burnt Lobster)
        if (at6(give >= 1)) s.add(5);    // Buy a Wendlewick ale
        if (at6(miriam >= 1)) s.add(6);  // Talk to Matthew (give him the ale)
        if (at6(jacob >= 1)) s.add(7);   // Talk to Miriam
        if (at6(gefen2 >= 1)) s.add(8);  // Talk to Jacob
        if (at6(ale >= 2)) s.add(9);     // Talk to Bartender Gefen again
        if (v >= 9) s.add(10);           // Go back and talk to Adam
        // To the farm
        if (v >= 12) s.add(11);   // Talk to Farmer Rachel
        if (v >= 15 || hosTrail) s.add(12);   // Investigate the bloodsplatter
        if (v >= 15) s.add(13);   // Follow the blood trail, enter the cave
        if (v >= 18) s.add(14);   // Talk to Anya
        if (v >= 21) s.add(15);   // Teleport, go to the lighthouse, talk to Esther
        // Gathering ingredients
        const has = (id) => inv.has(id);
        if (v >= 24 || has(60408)) s.add(16);   // pick poppies -> Wendlewick poppy
        if (v >= 24 || has(60409)) s.add(17);   // kill a hedgehog, pick up the spines
        if (v >= 24 || has(60407)) s.add(18);   // pick wolf's tongue mushrooms
        if (v >= 24) s.add(19);                 // Return to Esther
        const brew = [229, 60420, 60421, 60422, 60423, 60424];   // vial -> oil -> sterilised -> +crushed spines -> +pressed poppy -> antisanguine
        let bs = 0; for (let k = 0; k < brew.length; k++) if (has(brew[k])) bs = k + 1;
        if (v >= 27 || bs >= 1) s.add(20);   // take a vial
        if (v >= 27 || bs >= 2) s.add(21);   // fill it with oil
        if (v >= 27 || bs >= 3) s.add(22);   // sterilise the vial of oil
        if (v >= 27 || bs >= 4) s.add(23);   // hedgehog spines: grind + mix
        if (v >= 27 || bs >= 5) s.add(24);   // Wendlewick poppy: press + mix
        if (v >= 27 || bs >= 6) s.add(25);   // wolf's tongue: weigh + mix
        if (v >= 27 || bs >= 6) s.add(26);   // antisanguine is ready
        if (v >= 27) s.add(27);              // Talk to Esther
        if (v >= 30) s.add(28);   // test antisanguine on the hedgehog
        if (v >= 33) s.add(29);   // test on the duck
        if (v >= 36) s.add(30);   // test on the badger
        // Getting a havensilver weapon
        if (v >= 42) s.add(31);   // Liat in the smithy
        if (v >= 45) s.add(32);   // go to the mine, talk to Gidon (+ mine copper/tin)
        if (v >= 48) s.add(33);   // give Liat the copper + tin (un-noted)
        if (v >= 51) s.add(34);   // return to the mine, enter the cave
        if (v >= 54) s.add(35);   // talk to Gidon
        if (v >= 57 || hosOreMined) s.add(36);   // mine 4 havensilver ore (latched at 4x 60295)
        if (v >= 60) s.add(37);   // return and talk to Liat
        if (v >= 63 || hosBarsSmelted) s.add(38);   // smelt the ores (latched at 4x bar 60296 across inventory + metal bank)
        if (v >= 63) s.add(39);   // smith the havensilver greatsword
        // Fight
        if (v >= 66 || hosSwordWorn) s.add(40);   // equip the Havensilver greatsword (latched once 60298 is worn)
        if (v >= 66) s.add(41);   // return to the Blighted Cave, talk to Anya
        if (v >= 69 || hosBuffDrunk) s.add(42);   // enter the cave, drink the antisanguine (buff varbit 60615, latched)
        if (v >= 69) s.add(43);   // attack the Sanguine heart
        if (v >= 69) s.add(44);   // once at 1 life point, attack it again
        if (v >= 72) s.add(45);   // leave the cave exit, talk to Anya again
        if (v >= 75) s.add(46);   // return to Wendlewick, talk to Adam
        if (v >= 75) s.add(47);   // quest complete
        return s;
      }
    },
    'Hermit Permits': {
      vbs: [60814, 60816, 60817, 60819, 60820, 60821, 60822],
      done: (vb) => {
        const v = vb[60814] | 0, sub = vb[60816] | 0, sub17 = vb[60817] | 0, sigs = vb[60819] | 0;
        const s = new Set();
        if (v >= 10) s.add(0);   // Read the Village Noticeboard
        if (v >= 20) s.add(1);   // Talk to Grocer Hannah
        if (v >= 25) s.add(2);   // Talk to Shelly (the crab in the store)
        if (v >= 30) s.add(3);   // Follow the crab
        if (v >= 35) s.add(4);   // Enter the cave + talk to Hermit
        if (v >= 40) s.add(5);   // Kill a crab, craft the crab amulet
        if (v >= 40) s.add(6);   // Talk to Hermit again
        if (v >= 45) s.add(7);   // Equip the crabulet + talk to Shelly
        if (v >= 50) s.add(8);   // Return to Wendlewick, talk to Shelly
        if (v >= 55) s.add(9);   // Beach, talk to a crab (Finn/Sandy/Kai)
        if (v > 55 || (v === 55 && sub >= 1)) s.add(10);   // Return to Wendlewick, talk to Adam
        if (v > 55 || (v === 55 && sub17 >= 1)) s.add(11);   // Talk to Zeke
        if (v >= 60) s.add(12);   // Talk to Esther in the lighthouse
        if (v > 60 || (v === 60 && sigs >= 5)) s.add(13);   // Sign 5 villagers
        if (v >= 65) s.add(14);   // Report to Adam / Esther / Zeke
        if (v >= 70) s.add(15);   // Southern beach, talk to a crab leader
        if ((vb[60820] | 0) === 2 && (vb[60821] | 0) === 2 && (vb[60822] | 0) === 2) s.add(16);   // all 3 driftwood huts built
        if (v >= 80) s.add(17);   // Talk to Finn / Sandy / Kai
        if (v >= 80) s.add(18);   // Quest complete (v=80)
        return s;
      }
    }
  };
  async function qgAutoTick(nm) {
    const a = QG_AUTO[nm]; if (!a) return;
    let vb = {}; try { vb = await readVarbitValues(a.vbs || []); } catch (e) { return; }
    // Old-generation quests track progress in a whole varp: `vps` are read raw into done()'s third argument.
    let vp = {};
    if (a.vps && a.vps.length) { try { vp = JSON.parse(await bridge().varps(myPid(), a.vps.join(','))) || {}; } catch (e) { return; } }
    let inv = new Set();
    if (a.inv) { try { const d = JSON.parse((await PLUGIN_API['state.inventory'].run([], myPid())) || '{}'); if (d && Array.isArray(d.items)) for (const it of d.items) if (it[1] > 0) inv.add(it[1]); } catch (e) {} }
    const set = a.done(vb, inv, vp), sig = [...set].sort((x, y) => x - y).join(',');
    if (qgAutoDone[nm] && qgAutoDone[nm].sig === sig) return;   
    set.sig = sig; qgAutoDone[nm] = set;
    try { paneRun('questfocus', () => { qgSig = ''; renderQuestFocus(); }); paneRun('quests', () => { questDetailSig = ''; renderQuests(); }); } catch (e) {}
  }
  async function hosStep() {
    let vbm = {}; try { vbm = await readVarbitValues([HOS_PROG, HOS_SUB, HOS_ALE, HOS_GIVE, HOS_MIRIAM, HOS_JACOB, HOS_GEFEN2]); } catch (e) { return; }
    const v = vbm[HOS_PROG] | 0, sub = vbm[HOS_SUB] | 0, ale = vbm[HOS_ALE] | 0, give = vbm[HOS_GIVE] | 0, miriam = vbm[HOS_MIRIAM] | 0, jacob = vbm[HOS_JACOB] | 0, gefen2 = vbm[HOS_GEFEN2] | 0;
    if (v !== 12) hosTrail = false;
    if (v < 54) hosOreMined = false;
    if (v < 60) hosBarsSmelted = false;
    if (v < 63) hosSwordWorn = false;
    if (v < 66) hosBuffDrunk = false;
    if (v !== 21) hosDown = false;
    if (v !== 27) hosDown27 = false;  
    if (v !== 18 && hudShown) hudSet(0, '', false);
    const P = qgP;
    const near = (x, y, p, r) => !!(P && (P.p || 0) === p && Math.abs(P.x - x) <= r && Math.abs(P.y - y) <= r);
    const lighthouseEsther = async () => {
      if (near(3454, 1495, 1, 20)) { await qgTalkNpc('Esther', 'Talk to Esther', 3457, 1500, 1, 'continue quest'); return; }   
      if (!(await qgObjectById(136506, 'Stairs\nClimb up to Esther')))
        qgOv('overlay.guideTiles', [{ x: 3452, y: 1494, plane: 0, label: 'Go to the lighthouse, then climb the stairs' }]);
    };
    // Boss arena bounds derive from the Sanguine heart's live tile (x +-10, y -25..+15): the instance region changes every fight.
    {
      let boss = null, tumours = [];
      try { const s = JSON.parse((await PLUGIN_API['state.scene'].run([60], myPid())) || '{}'); if (s && Array.isArray(s.npcs)) {
        boss = s.npcs.find(n => n && n.name && /sanguine heart/i.test(n.name));
        tumours = s.npcs.filter(n => n && n.name && /sanguine tumour/i.test(n.name));
      } } catch (e) {}
      if (!boss) hosTumourPhase = 0;   
      if (v >= 66 && v < 69 && boss && P && (P.p | 0) === 0 && P.x >= boss.x - 10 && P.x <= boss.x + 10 && P.y >= boss.y - 25 && P.y <= boss.y + 15) {
        if (tumours.length > hosTumourPhase) hosTumourPhase = tumours.length;   // phase = how many tumours spawned (1/2/3); latched as they are killed
        let buff = 0; try { buff = (await readVarbitValues([60615]))[60615] | 0; } catch (e) {}
        if (buff === 0) { qgItem(60424, 'Drink the Antisanguine'); return; }   
        if (tumours.length) { const t = tumours[0]; await qgNpc('Sanguine tumour', 'Attack the Sanguine tumour (Phase ' + hosTumourPhase + ')', t.x, t.y, 0); return; }
        await qgNpc('Sanguine heart', 'Attack the Sanguine heart', boss.x, boss.y, 0); return;   
      }
    }
    if (v === 0 || v === 3) { await qgTalkNpc('Adam', 'Talk to Adam', 3483, 1573, 0); return; }   
    if (v === 6) {
      // sub-steps within v=6, latest first.
      if (ale === 2) { await qgTalkNpc('Adam', 'Talk to Adam', 3483, 1573, 0); return; }   
      if (gefen2 === 1) { await qgTalkNpc('Bartender Gefen', 'Talk to Bartender Gefen', 3499, 1498, 0); return; }   
      if (jacob === 1) { await qgTalkNpc('Jacob', 'Talk to Jacob', 3490, 1507, 0); return; }      
      if (miriam === 1) { await qgNpc('Miriam', 'Talk to Miriam', 3492, 1507, 0); return; }   
      if (give === 1) { await qgDialogOpt('yes'); return; }   // Matthew's "give him the ale?" dialogue (60600=1) -> choose "Yes."
      if (ale === 1) {
        if ((await qgInvCount(HOS_ALE_ITEM)) <= 0) { await qgNpc('Bartender Gefen', 'Trade Gefen, buy Wendlewick ale', 3499, 1498, 0); return; }   
        await qgTalkNpc('Matthew', 'Talk to Matthew', 3496, 1505, 0, 'yes'); return;                                                               
      }
      if (sub === 1) { await qgNpc('Dead bear', 'Inspect the Dead bear', 3572, 1675, 0); return; }            
      if (sub === 3) { await qgTalkNpc('Bartender Gefen', 'Talk to Bartender Gefen', 3499, 1498, 0); return; }    
      await qgTalkNpc('Raz', 'Talk to Raz', 3574, 1641, 0); return;                                           // sub 0 and sub 2: talk to Raz
    }
    if (v === 9) { await qgTalkNpc('Farmer Rachel', 'Talk to Farmer Rachel', 3611, 1438, 0); return; }   
    if (v === 12) {
      if (!hosTrail) { try { const dt = await bridge().interfaceGroup(myPid(), 1191); if (dt && dt.toLowerCase().indexOf('follow it and see where it goes') >= 0) hosTrail = true; } catch (e) {} }   // group 1191 = player dialogue
      if (hosTrail) {   // Blighted Cave entrance = loc 136477
        if (!(await qgObjectById(136477, 'Cave entrance\nEnter the Cave entrance')))
          qgTile(3639, 1477, 0, 'Go here, then enter the Cave entrance');
        return;
      }
      qgObject('Bloodsplatter', 'Investigate the Bloodsplatter', 3616, 1453, 0); return;
    }
    if (v === 15) { await qgDialogNpc('Anya', 'Talk to Anya', 3424, 7895, 0, 'dealing with', 'what do i need'); return; }
    if (v === 18) {                                              
      if (await qgLodestone('Wendlewick')) return;
      await lighthouseEsther(); return;
    }
    if (v === 21) {
      if (!hosDown && P && (P.p | 0) === 0) hosDown = true;
      if (hosDown) {
        if ((await qgInvCount(HOS_WOLFTONGUE)) > 0) { await lighthouseEsther(); return; }   
        if ((await qgInvCount(HOS_SPINES)) > 0) { qgObject("Wolf's tongue mushrooms", "Pick the Wolf's tongue mushrooms", 3590, 1417, 0); return; }   
        if ((await qgInvCount(HOS_POPPY)) > 0) {   
          const marks = qgIdMarks(await qgGround(), HOS_SPINES, 'Hedgehog spines\nTake');
          if (marks.length) { qgClrNpc(); qgClrDlg(); qgClrItem(); qgOv('overlay.guideTiles', marks); return; }
          await qgNpc('Hedgehog', 'Attack and loot Hedgehog spines', 3529, 1396, 0); return;
        }
        qgObject('Poppies', 'Pick the Wendlewick poppy', 3454, 1454, 0); return;
      }
      await qgObjectById(136507, 'Stairs\nClimb down'); return;
    }
    if (v === 24) {
      if (qgIfaceComp(1370, 29, 'Make')) return;   // Item Production window open -> its Make button (group 1370 comp 29)
      if ((await qgInvCount(HOS_ANTISANG)) > 0) {   
        if (await qgDialogOpt('hearts of sanguine')) return;   
        await qgNpc('Esther', 'Talk to Esther', 3457, 1500, 1); return;
      }
      if ((await qgInvCount(HOS_WEIGHED)) > 0) { qgItem(HOS_MIXVIAL2, "Mix in the weighed wolf's tongue"); return; }   
      if ((await qgInvCount(HOS_WOLFTONGUE)) > 0) {   
        if (await qgDialogOpt('weigh wolf')) return;       
        if (await qgDialogOpt('weigh out 30')) return;     
        qgObject('Desk', 'Weigh ingredient at the Desk', 3453, 1502, 1); return;
      }
      if ((await qgInvCount(HOS_MIXVIAL2)) > 0) { qgObject('Sack of mushrooms', 'Take from the Sack of mushrooms', 3456, 1497, 1); return; }   
      if ((await qgInvCount(HOS_PRESSED)) > 0) { qgItem(HOS_MIXVIAL, 'Mix in the pressed Wendlewick poppy'); return; }   
      if ((await qgInvCount(HOS_POPPY)) > 0) {   
        if (await qgDialogOpt('press wendlewick poppy')) return;
        qgObject('Table', 'Press ingredient at the Table', 3454, 1499, 1); return;
      }
      if ((await qgInvCount(HOS_MIXVIAL)) > 0) { qgObject('Sack of flowers', 'Take from the Sack of flowers', 3457, 1498, 1); return; }   
      if ((await qgInvCount(HOS_CRUSHED)) > 0) { qgItem(HOS_STERVIAL, 'Mix in the crushed hedgehog spines'); return; }   
      if ((await qgInvCount(HOS_SPINES)) > 0) { qgItem(HOS_SPINES, 'Grind the hedgehog spines'); return; }   
      if ((await qgInvCount(HOS_STERVIAL)) > 0) { qgObject('Bucket of hedgehog spines', 'Take from the Bucket of hedgehog spines', 3458, 1499, 1); return; }   
      if ((await qgInvCount(HOS_OILVIAL)) > 0) {
        if (await qgDialogOpt('yes')) return;
        qgObject('Fireplace', 'Sterilise at the Fireplace', 3457, 1501, 1); return;
      }
      if ((await qgInvCount(HOS_VIAL)) > 0) {
        if (await qgDialogOpt('yes')) return;
        qgObject('Vat of oil', 'Fill the Vial at the Vat of oil', 3454, 1497, 1); return;
      }
      qgObject('Vial cabinet', 'Take from the Vial cabinet', 3456, 1503, 1); return;
    }
    if (v === 27) {   // 60607=1: climb back down the lighthouse, then test the antisanguine on the Hedgehog
      if (!hosDown27 && P && (P.p | 0) === 0) hosDown27 = true;   
      if (hosDown27) { await qgNpc('#32693', 'Test antisanguine on the Hedgehog', 3572, 1380, 0); return; }   // match by id 32693: the name is ambiguous
      await qgObjectById(136507, 'Stairs\nClimb down'); return;
    }
    if (v === 30) { await qgNpc('#32696', 'Test antisanguine on the Duck', 3590, 1363, 0); return; }   // Duck (id 32696)
    if (v === 33) { await qgNpc('#32701', 'Test antisanguine on the Badger', 3612, 1387, 0); return; }   // Badger (id 32701)
    if (v === 36) { await qgNpc('Anya', 'Talk to Anya'); return; }
    if (v === 39) { await qgNpc('Liat', 'Talk to Liat', 3509, 1541, 0); return; }   
    if (v === 42) { await qgNpc('Gidon', 'Talk to Gidon', 3517, 1691, 0); return; }   
    if (v === 45) {
      const copper = await qgInvCount(436), tin = await qgInvCount(438);
      if (copper >= 10 && tin >= 10) { await qgNpc('Liat', 'Talk to Liat', 3509, 1541, 0); return; }   
      if (copper < 10) { qgObject('Copper rock', 'Mine copper ore (' + copper + '/10), or withdraw it UNNOTED from the bank', 3519, 1681, 0); return; }
      qgObject('Tin rock', 'Mine tin ore (' + tin + '/10), or withdraw it UNNOTED from the bank', 3526, 1681, 0); return;
    }
    if (v === 48) { await qgNpc('Liat', 'Talk to Liat', 3509, 1541, 0); return; }
    if (v === 51) {
      // inside the cave interior (instanced region: x 3460-3520, y 3480-8130, plane 0) -> talk to Gidon
      if (P && (P.p | 0) === 0 && P.x >= 3460 && P.x <= 3520 && P.y >= 3480 && P.y <= 8130) { await qgNpc('Gidon', 'Talk to Gidon', 3487, 8097, 0); return; }
      // still outside -> the mine's cave entrance (loc 136468)
      if (!(await qgObjectById(136468, 'Cave entrance\nEnter the Cave entrance')))
        qgTile(3518, 1690, 0, 'Go here, then enter the Cave entrance');
      return;
    }
    if (v === 54) {
      const ore = await qgInvCount(60295);
      if (ore >= 4) hosOreMined = true;
      if (ore < 4) { qgObject('Havensilver rock', 'Mine havensilver ore (' + ore + '/4)', 3482, 8089, 0); return; }
      await qgNpc('Liat', 'Talk to Liat', 3509, 1541, 0); return;   
    }
    if (v === 57) { await qgNpc('Liat', 'Talk to Liat', 3509, 1541, 0); return; }   
    if (v === 60) {
      if ((await qgInvCount(60298)) > 0) { qgClearAll(); return; }   // completed greatsword (60298) in pack -> done; varbit advances to 63
      const inv = await qgInvCount(60296), mb = await qgMetalBankCount(60296);
      const bars = inv + mb;   
      if (bars >= 4) hosBarsSmelted = true;
      const smithing = (await qgInvCount(47068)) > 0;   // unfinished greatsword in pack -> already smithing, don't revert to smelting
      if (bars >= 4 || smithing) { qgObject('Anvil', 'Smith the Greatsword at the Anvil', 3514, 1536, 0); return; }
      qgObject('Furnace', 'Smelt havensilver bars (' + bars + '/4)', 3513, 1540, 0); return;
    }
    if (v === 63) {
      if ((await qgEquipCount(60298)) > 0) {
        hosSwordWorn = true;
        let picked = 0; try { picked = (await readVarbitValues([60608]))[60608] | 0; } catch (e) {}
        if (picked > 0) { qgClearAll(); return; }   // trust option chosen (60608>0) -> done; varbit advances to 66
        if (!hosTrust) hosTrust = qgRand(3) + 1;
        if (await qgDialogOpt(HOS_TRUST_OPTS[hosTrust - 1])) return;  
        await qgNpc('Anya', 'Talk to Anya', 3633, 1475, 0); return;
      }
      qgItem(60298, 'Equip the Greatsword'); return;   
    }
    if (v === 66) {   // Blighted Cave = loc 136477; the drink + fight all happen at v=66
      if (!(await qgObjectById(136477, 'Cave entrance\nEnter the Cave entrance')))
        qgTile(3639, 1477, 0, 'Go here, then enter the Cave entrance');
      return;
    }
    if (v === 69) {
      if (near(3638, 1475, 0, 25)) {
        let picked = 0; try { picked = (await readVarbitValues([60609]))[60609] | 0; } catch (e) {}
        if (picked > 0) { qgClearAll(); return; }   // farewell choice made (60609>0) -> done; varbit advances to 72
        if (!hosFarewell) hosFarewell = qgRand(3) + 1;
        if (await qgDialogOpt(HOS_FAREWELL_OPTS[hosFarewell - 1])) return;
        await qgNpc('Anya', 'Talk to Anya', 3633, 1475, 0); return;
      }
      qgObject('Cave exit', 'Exit the cave', 3423, 7875, 0); return;   
    }
    if (v === 72) { await qgTalkNpc('Adam', 'Talk to Adam', 3483, 1573, 0); return; }   
    qgClearAll();   // v=75 = quest complete (completion flag varbit 60605 == 1)
  }
  async function vohStep() {
    let vbm = {}; try { vbm = await readVarbitValues([VOH_PROG, VOH_KILLS]); } catch (e) { return; }
    const v = vbm[VOH_PROG] | 0, kills = vbm[VOH_KILLS] | 0;
    if (v === 0) { await qgDialogNpc('Bartender', 'Talk to the Bartender', 3045, 3257, 0, 'visions of havenhythe'); return; }   // Bartender at the Rusty Anchor, Port Sarim
    if (v < 1 || v > 50) { qgClearAll(); return; }   
    const P = qgP;
    const near = (x, y, p, r) => !!(P && (P.p || 0) === p && Math.abs(P.x - x) <= r && Math.abs(P.y - y) <= r);
    switch (v) {
      case 3:  await qgDialogNpc('Bartender', 'Talk to the Bartender for the mead', 3045, 3257, 0, 'visions of havenhythe'); break;
      case 6:  qgItem(VOH_MEAD, 'Drink the Dragonfire mead'); break;
      case 9:   // loc 136586 = the east door down; on plane 0 the target is the Bartender
        if (P && (P.p | 0) === 1) await qgObjectById(136586, 'Door\nGo downstairs');
        else await qgDialogNpc('Bartender', 'Talk to the Bartender', 3045, 3257, 0, 'visions of havenhythe');
        break;
      case 12: await qgNpc("'Shady' Sullivan", "Talk to 'Shady' Sullivan", 3052, 3255, 0); break;
      case 15: await qgDialogNpc('Captain Lorris', 'Talk to Captain Lorris', 3028, 3221, 0, 'visions of havenhythe', "i'm ready"); break;
      case 18: {   // Lorris sails with the player, so after arrival the target becomes Jocko
        const arrived = vohSailed || (P && (P.p | 0) === 0 && Math.abs(P.x - 3388) <= 40 && Math.abs(P.y - 1530) <= 40);
        if (arrived) await qgDialogNpc('Old Man Jocko', 'Talk to Old Man Jocko', 3388, 1530, 0, 'visions of havenhythe', "i'm ready");
        else await qgDialogNpc('Captain Lorris', 'Talk to Captain Lorris', 3028, 3221, 0, 'visions of havenhythe', "i'm ready");
        break;
      }
      case 21: await qgDialogNpc('Adam', 'Talk to Adam', 3484, 1574, 0, 'visions of havenhythe', 'continue quest'); break;
      case 24: await qgNpc('Shrine Tender Joanna', 'Talk to Shrine Tender Joanna', 3542, 1421, 0); break;
      case 27: await qgObjectById(136467, 'Altar of Inanna\nPray'); break;
      case 30: await qgNpc('Shrine Tender Joanna', 'Talk to Shrine Tender Joanna', 3542, 1421, 0); break;
      case 33: await qgNpc('Adam', 'Talk to Adam', 3484, 1574, 0); break;
      case 36: await qgNpc('Zeke', 'Talk to Zeke', 3496, 1565, 0); break;
      case 39:
        if (kills < 10) { await qgNpc('Juvenile boulder crab', 'Kill the Juvenile boulder crabs (' + kills + '/10)', 3413, 1530, 0); }
        else { const lime = await qgInvCount(VOH_LIMESTONE);
          await qgNpc('Zeke', lime >= 10 ? 'Talk to Zeke' : ('Get ' + (10 - lime) + ' more Limestone, then talk to Zeke'), 3496, 1565, 0); }
        break;
      case 42: {   // through the lighthouse, up the stairs (loc 136506) -> Esther on plane 1
        const sc = await qgScene();
        if (P && (P.p | 0) === 1 && qgSceneNpc(sc, 'Esther')) {
          vohEstherUp = true;   // step-15 completion latch (read by QG_AUTO)
          await qgNpc('Esther', 'Talk to Esther', 3456, 1500, 1);
          break;
        }
        if (!(await qgObjectById(136506, 'Stairs\nClimb up to Esther', sc.objects)))
          qgOv('overlay.guideTiles', [{ x: 3452, y: 1494, plane: 0, label: 'Go here, then climb up to Esther' }]);   
        break;
      }
      case 45: {
        if (!(P && (P.p | 0) === 0)) { await qgObjectById(136507, 'Stairs\nClimb down'); break; }   
        const bricks = await qgInvCount(VOH_BRICK);
        if (bricks >= 10) {
          if (!(await qgObjectById(136454, 'Lodestone hotspot\nBuild')))
            qgOv('overlay.guideTiles', [{ x: 3461, y: 1520, plane: 0, label: 'Lodestone hotspot\nBuild' }]);
          break;
        }
        const lime = await qgInvCount(VOH_LIMESTONE);
        if (lime >= 1) qgItem(VOH_LIMESTONE, 'Craft Limestone into bricks (' + bricks + '/10)');
        else qgTile(3461, 1520, 0, 'Need 10 Limestone bricks - get Limestone first');
        break;
      }
      default: qgClearAll(); break;                        // 48 cutscene / others -> no highlight
    }
  }
  // Hermit Permits: progress varbit 60814 is 0 until the quest is accepted.
  const HP_PROG = 60814;
  const HP_HERMIT_OPTS = ['crab brethren', 'no thanks'];   // v=35 Hermit chooser: any option works
  let hpDown = false;   // latched once the player is back on plane 0 at v=60
  let hpHermitPick = 0;
  async function hpStep() {
    let v = 0; try { v = (await readVarbitValues([HP_PROG]))[HP_PROG] | 0; } catch (e) {}
    const P = qgP;
    const near = (x, y, p, r) => !!(P && (P.p || 0) === p && Math.abs(P.x - x) <= r && Math.abs(P.y - y) <= r);
    if (v !== 45 && v !== 55 && hudShown) hudSet(0, '', false);
    if (v !== 60) hpDown = false;
    if (v === 0) {
      let boxed = false; try { boxed = await PLUGIN_API['overlay.highlightOption'].run(['crabs running amok', 'accept'], myPid()); } catch (e) {}
      if (boxed) { qgClrNpc(); qgClrTiles(); qgClrItem(); return; }
      qgObject('Noticeboard', 'Read the Village Noticeboard', 3490, 1516, 1); return;
    }
    if (v === 10) {
      let boxed = false; try { boxed = await PLUGIN_API['overlay.highlightOption'].run(['talk about hermit permits'], myPid()); } catch (e) {}
      if (boxed) { qgClrNpc(); qgClrTiles(); qgClrItem(); return; }
      await qgNpc('Grocer Hannah', 'Talk to Grocer Hannah', 3492, 1545, 0); return;
    }
    if (v === 15) {   // Grocer Hannah dialogue chain (60815=1)
      let boxed = false; try { boxed = await PLUGIN_API['overlay.highlightOption'].run(['talk about hermit permits', 'quite a lot of crabs', 'more crabs', 'herding them out', 'threatened them', 'kill them all'], myPid()); } catch (e) {}
      if (boxed) { qgClrNpc(); qgClrTiles(); qgClrItem(); return; }
      await qgNpc('Grocer Hannah', 'Talk to Grocer Hannah', 3492, 1545, 0); return;
    }
    if (v === 20 || v === 25) {   // Shelly (the crab): 60827 = position (0 = shop, 1-4 = follow stops), 60828 = 1 stopped / 0 moving
      const SHELLY_AT = { 0: [3492, 1545], 1: [3477, 1563], 2: [3455, 1591], 3: [3440, 1597], 4: [3450, 1622] };
      let pos = 0, stopped = 1; try { const sv = await readVarbitValues([60827, 60828]); pos = sv[60827] | 0; stopped = sv[60828] | 0; } catch (e) {}
      const at = SHELLY_AT[pos] || SHELLY_AT[0];
      await qgNpc('Shelly', (pos === 0 || stopped === 1) ? 'Talk to Shelly' : 'Follow Shelly', at[0], at[1], 0); return;
    }
    if (v === 30) {
      if (near(3426, 8011, 0, 50)) { await qgNpc('Hermit', 'Talk to Hermit', 3414, 8029, 0); return; }
      qgObject('Cave entrance', 'Enter the Cave entrance', 3458, 1619, 0); return;
    }
    if (v === 40) {
      if ((await qgEquipCount(60398)) > 0) { await qgNpc('Shelly', 'Talk to Shelly', 3412, 8028, 0); return; }   
      qgItem(60398, 'Wear the Crabulet'); return;   
    }
    if (v === 35) {   // crab amulet chain: kill Crab -> claw (7536) -> craft -> unstrung (60396) -> string -> crab amulet (60397) -> Hermit again
      if ((await qgInvCount(60397)) > 0) {
        if (!hpHermitPick) hpHermitPick = qgRand(HP_HERMIT_OPTS.length) + 1;
        if (await qgDialogOpt(HP_HERMIT_OPTS[hpHermitPick - 1])) return;
        await qgNpc('Hermit', 'Talk to Hermit', 3414, 8029, 0); return;
      }
      if ((await qgInvCount(60396)) > 0) { qgItem(60396, 'Use ball of wool to string'); return; }   // needs a ball of wool (1759)
      if ((await qgInvCount(7536)) > 0) { qgItem(7536, 'Craft Unstrung Amulet'); return; }   
      await qgNpc('Crab', 'Kill and Loot Crab Claw', P ? P.x : 3426, P ? P.y : 8011, 0); return;   
    }
    if (v === 45) {
      if (await qgLodestone('Wendlewick')) return;   
      await qgNpc('Shelly', 'Talk to Shelly', 3492, 1545, 0); return;   
    }
    if (v === 50) {
      let crab = null;
      try { const s = JSON.parse((await PLUGIN_API['state.scene'].run([60], myPid())) || '{}'); if (s && Array.isArray(s.npcs)) crab = s.npcs.find(n => n && n.name && /^(kai|sandy|finn)$/i.test(n.name)); } catch (e) {}
      if (crab) { await qgNpc(crab.name, 'Talk to ' + crab.name, crab.x, crab.y, 0); return; }
      qgTile(3569, 1340, 0, 'Talk to a beach crab (Kai, Sandy or Finn)'); return;
    }
    if (v === 60) {   // 60818=1: climb down, then collect signatures near 3483,1556
      if (P && (P.p || 0) === 0) hpDown = true;   
      if (!hpDown) { await qgObjectById(136507, 'Stairs\nClimb down'); return; }   
      let sigs = 0; try { sigs = (await readVarbitValues([60819]))[60819] | 0; } catch (e) {}   // 60819 = signatures collected (0-5); same NPC may be re-used
      if (sigs >= 5) {
        const adamD = P ? (Math.abs(P.x - 3483) + Math.abs(P.y - 1573)) : 1e9, zekeD = P ? (Math.abs(P.x - 3496) + Math.abs(P.y - 1565)) : 1e9;
        if (adamD <= zekeD) { await hpTalkNpc('Adam', 'Talk to Adam', 3483, 1573, 0); return; }
        await hpTalkNpc('Zeke', 'Talk to Zeke', 3496, 1565, 0); return;
      }
      let npc = null, best = 1e9;
      try { const s = JSON.parse((await PLUGIN_API['state.scene'].run([60], myPid())) || '{}'); if (s && Array.isArray(s.npcs)) for (const n of s.npcs) {
        if (n && n.name && (/^(baker|fishmonger|grocer).?s assistant$/i.test(n.name) || /^villager$/i.test(n.name))) { const d = P ? (Math.abs(n.x - P.x) + Math.abs(n.y - P.y)) : 0; if (d < best) { best = d; npc = n; } }
      } } catch (e) {}
      if (npc) { await hpTalkNpc(npc.name, 'Talk to ' + npc.name + ' (' + sigs + '/5 signatures)', npc.x, npc.y, 0); return; }   
      qgTile(3483, 1556, 0, 'Collect 5 signatures (' + sigs + '/5)'); return;
    }
    if (v === 65) {   // rope = 954, seaweed = 401
      let rope = 0, weed = 0; try { rope = await qgInvCount(954); weed = await qgInvCount(401); } catch (e) {}
      const lack = (rope < 3 || weed < 6) ? ' (rope ' + rope + '/3, seaweed ' + weed + '/6)' : '';
      let crab = null, best = 1e9;
      try { const s = JSON.parse((await PLUGIN_API['state.scene'].run([60], myPid())) || '{}'); if (s && Array.isArray(s.npcs)) for (const n of s.npcs) {
        if (n && n.name && /^(kai|sandy|finn)$/i.test(n.name)) { const d = P ? (Math.abs(n.x - P.x) + Math.abs(n.y - P.y)) : 0; if (d < best) { best = d; crab = n; } }
      } } catch (e) {}
      if (crab) { await qgNpc(crab.name, 'Talk to ' + crab.name + lack, crab.x, crab.y, 0); return; }
      qgTile(3569, 1340, 0, 'Talk to a beach crab (Finn/Sandy/Kai)' + lack); return;
    }
    if (v === 55) {   // Adam (60816=0) -> Zeke (60816=1) -> Esther in the lighthouse (60817=1)
      if (await qgLodestone('Wendlewick')) return;
      let s16 = 0, s17 = 0; try { const sv = await readVarbitValues([60816, 60817]); s16 = sv[60816] | 0; s17 = sv[60817] | 0; } catch (e) {}
      if (s17 === 1) {
        if (near(3454, 1495, 1, 20)) { await hpTalkNpc('Esther', 'Talk to Esther', 3457, 1500, 1); return; }   
        if (!(await qgObjectById(136506, 'Stairs\nClimb up to Esther')))
          qgOv('overlay.guideTiles', [{ x: 3452, y: 1494, plane: 0, label: 'Go to the lighthouse, then climb the stairs' }]);
        return;
      }
      if (s16 === 1) { await hpTalkNpc('Zeke', 'Talk to Zeke', 3496, 1565, 0); return; }   
      await hpTalkNpc('Adam', 'Talk to Adam', 3483, 1573, 0); return;   
    }
    if (v === 70) {   // per hut: state varbit (1 unbuilt -> 2 built) + progress varbit (0..5)
      const HUTS = [[60820, 60823, 3579, 1338], [60821, 60824, 3575, 1338], [60822, 60825, 3583, 1334]];   // [state, progress, x, y] per hut
      let sv = {}; try { sv = await readVarbitValues([60820, 60821, 60822, 60823, 60824, 60825]); } catch (e) {}
      for (const [st, pr, hx, hy] of HUTS) { if ((sv[st] | 0) === 1 && hx) { qgObject('Pile of driftwood', 'Build the Pile of driftwood (' + (sv[pr] | 0) + '/5)', hx, hy, 0); return; } }
      let crab = null, best = 1e9;
      try { const s = JSON.parse((await PLUGIN_API['state.scene'].run([60], myPid())) || '{}'); if (s && Array.isArray(s.npcs)) for (const n of s.npcs) { if (n && n.name && /^(kai|sandy|finn)$/i.test(n.name)) { const d = P ? (Math.abs(n.x - P.x) + Math.abs(n.y - P.y)) : 0; if (d < best) { best = d; crab = n; } } } } catch (e) {}
      if (crab) { await qgNpc(crab.name, 'Talk to ' + crab.name, crab.x, crab.y, 0); return; }
      qgTile(3569, 1340, 0, 'Talk to a beach crab (Finn/Sandy/Kai)'); return;
    }
    qgClearAll();   // v=80 = quest complete
  }
  const QUEST_GUIDES = { 'Visions of Havenhythe': vohStep, 'Hearts of Sanguine': hosStep, 'Hermit Permits': hpStep, 'Secrets of Amberfell': () => amberStep(), 'Wiz Kid': () => wizkidStep(), 'Necromancy!': () => necroStep(), 'The Restless Ghost': () => rgStep(), 'Making History': () => mhStep(), 'New Foundations': () => nfStep(), "There's No Place Like Home...": () => tnpStep(), 'Murder on the Border': () => motbStep() };   // focused quest name -> step fn (later-spliced panels' steps are called via lazy arrows)
  (function () { function guideLoop() { questGuideTick().catch(function () {}); setTimeout(guideLoop, 700); } setTimeout(guideLoop, 900); })();   // first tick deferred so PLUGIN_API (declared later) is ready

