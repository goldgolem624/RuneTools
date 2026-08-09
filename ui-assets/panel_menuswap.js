// RuneToolsX panel: Right-click menu (Developer).
// Spliced inline into client.html; bare classic script sharing one global scope.
//
// Reorder the options on a right-click menu. Rules are ORDERED and SCOPED TO ONE TARGET, so
// moving "Teleport" up on an Event noticeboard leaves every other object alone. A rule can be
// widened to "any object" deliberately, but that is never the default - matching on the verb
// alone silently reordered everything that shared it.
//
// The list is LATCHED, not live: a right-click menu closes as soon as the cursor leaves it, so
// a live view would blank before it could be clicked. Right-click in game, let the menu go,
// then reorder here at leisure. Only menus with a REAL TARGET replace the latch - empty ground
// still builds a valid two-entry menu (Cancel + Walk here) that would otherwise wipe it.
//
// "Cancel" and "Walk here" have no target and the game fixes their position, so they are listed
// for reference but cannot be moved.
//
// Entries arrive in DISPLAY order. The game stores them reversed (index 0 is the bottom line)
// and the companion flips them once on publish, so nothing here has to know that.
//
// Reordering permutes whole 16-byte records, which is refcount-neutral: every pointer inside
// stays owned exactly once, just at a different index. Nothing is allocated or freed. The
// client rebuilds the array every tick, so the companion reapplies the order from inside the
// builder rather than writing once.
//
// The order is applied to every parallel list the client keeps, not just the drawn menu, because
// LEFT-CLICK is fed by a different one: reordering the menu alone moved the menu while hover
// still read the old verb. The status line reports which lists a change landed in.

  // mnuOn is the companion's publish mode (rtx::menu::kEnable*), -1 until the first tick has told
  // it one - the companion boots at 0, but so does "off", and the first push has to happen.
  let mnuData = null, mnuSig = '', mnuListSig = '', mnuOn = -1, mnuNote = '', mnuPinCtx = '';
  let mnuLoaded = false;    // durable store read once, before any rule is written back
  let mnuView = 'menu';     // 'menu' = the latched menu | 'rules' = every saved rule | 'add'
  let mnuNames = {};        // rule key -> display name, so a saved rule reads as more than an id
  // name -> id, ACCUMULATED across polls and never forgotten. The launcher resolves whatever is
  // under the cursor AT THE MOMENT IT IS ASKED, but the capture-then-pick workflow means the
  // cursor has usually left for the panel by the time that poll lands - so the id arrived on a
  // different poll than the capture. Remembering every id the launcher ever reported lets a
  // capture pick its id up afterwards instead of being stuck at "id not resolved".
  let mnuIdSeen = {};
  // The add-by-id form: pick what kind of thing it is, type its id, and the CACHE supplies the
  // name and the option list - no need to find the thing in game and hover it.
  let mnuAdd = { name: '', order: [], editKey: '' };   // the rule editor's working copy
  // CAPTURE, then PICK. A single latch was unusable in practice: the menu you want is replaced
  // the moment the cursor crosses anything else on its way to the panel, so the options vanished
  // before they could be edited. Every distinct thing hovered is kept here instead, and the
  // editor works on a CHOSEN capture that live hovering cannot disturb.
  let mnuRecent = [];       // [{key, name, ents, ts}], newest first
  let mnuSel = '';          // rule key being edited; '' = show the list
  let mnuConfirmForget = '';     // rule key awaiting a Forget confirmation
  // entry type (class ordinal) -> cache kind
  const mnuKindOf = ty => (ty === 1 ? 0 : ty === 3 ? 1 : ty === 4 ? 2 : -1);
  // Two different strings, deliberately. The KEY prefix is what every consumer matches on
  // (/^(item|loc|npc):/), the LABEL is what the user reads. They were the same function, so a loc
  // was keyed "object:109059" and matched nowhere - items and NPCs worked because their two words
  // happen to be identical. That is why locs alone never resolved.
  const mnuKeyKind = k => (k === 0 ? 'item' : k === 1 ? 'loc' : 'npc');
  let mnuRules = {};        // target -> [verb, ...] desired order ('*' = any object)
  let mnuOrder = null;      // working order for the latched menu; null = as the game has it
  // localStorage is only a fast local mirror. The dock's Ultralight storage is wiped whenever
  // the ui-assets are re-extracted, and on a build without the WebCore cache path it is a silent
  // no-op that does not even survive a reload - which is why rules kept disappearing. The
  // authoritative copy is menurules.json in the user profile, read once via the bridge.
  try { const raw = localStorage.getItem('rtxMenuRules'); if (raw) mnuRules = JSON.parse(raw) || {}; } catch (e) {}

  // Disk shape is {v:2, rules:{key:[verbs]}, names:{key:"display name"}}. A bare map is the v1
  // format (rules only) and is migrated on read, so nothing set before this existed is lost.
  function mnuAdopt(o) {
    if (!o || typeof o !== 'object') return false;
    if (o.v && o.rules) {
      mnuRules = o.rules || {};
      mnuNames = o.names || {};
      return !!Object.keys(mnuRules).length;
    }
    mnuRules = o;
    return !!Object.keys(o).length;
  }

  async function mnuLoadRules() {
    if (mnuLoaded || !bridge() || !bridge().menuRulesLoad) return;
    mnuLoaded = true;
    let disk = null;
    try { disk = JSON.parse(await bridge().menuRulesLoad() || '{}'); } catch (e) { return; }
    const had = Object.keys(mnuRules).length;
    if (!mnuAdopt(disk) && had) mnuSaveRules();   // first run after the fix: migrate the mirror
    mnuSig = ''; mnuListSig = '';
    renderMenuSwap();
  }

  function mnuSaveRules() {
    const blob = JSON.stringify({ v: 2, rules: mnuRules, names: mnuNames });
    try { localStorage.setItem('rtxMenuRules', blob); } catch (e) {}
    try { if (bridge() && bridge().menuRulesSave) bridge().menuRulesSave(blob); } catch (e) {}
  }

  // (The cache name -> id search is gone. hoverEntity gives the exact id of what is under the
  // cursor, so inferring one from a display name was both redundant and lossy - charge and colour
  // variants share a name, and it declined on those.)

  const mnuPlain = s => String(s || '').replace(/<[^>]*>/g, '').trim();

  // The entries the panel is acting on: the chosen capture, else whatever is live.
  function mnuActiveEnts() {
    if (mnuSel) {
      for (const r of mnuRecent) if (r.key === mnuSel) return r.ents;
    }
    return (mnuData && mnuData.entries) || [];
  }

  // A rule saved before a config id could be resolved sits under a WEAKER key: an instance
  // handle (one tile / one spawn) or an option set. Once the id resolves, move the rule up to
  // it - otherwise the same object accumulates duplicates that shadow each other, which is
  // exactly how a "Shifting tombs - one instance only" rule ended up beside the id-keyed one.
  function mnuUpgradeKeys(ents) {
    for (const t of mnuTargets(ents)) {
      const keys = mnuVarKeys(ents, t);
      const best = keys[0];
      if (!/^(item|loc|npc):/.test(best) || mnuRules[best]) continue;
      for (let i = 1; i < keys.length; i++) {
        const k = keys[i];
        if (!mnuRules[k]) continue;
        mnuRules[best] = mnuRules[k];
        mnuNames[best] = mnuNames[k] || t;
        delete mnuRules[k];
        delete mnuNames[k];
        mnuSaveRules();
        mnuSig = ''; mnuListSig = '';
        break;
      }
    }
  }

  // Record the live menu. Keyed exactly like a rule, so re-hovering the same thing refreshes its
  // capture instead of stacking duplicates.
  // ONE CAPTURE PER ENTITY, never per menu.
  //
  // Overlapping things share a single menu - a Shifting tombs entrance and a market guard standing
  // on it - but they are separate entities with separate rules. Capturing the blended menu meant
  // reordering it produced an order for the COMBINATION, which is not a thing that exists: it only
  // matches while those two happen to overlap, and it attributed one entity's id to the other's
  // rows. Each entity is captured with ONLY its own rows, so a rule is always "these are the
  // guard's options" or "these are the tombs' options".
  function mnuNoteRecent() {
    const ents = (mnuData && mnuData.entries) || [];
    for (const t of mnuTargets(ents)) {
      const rows = ents.filter(e => mnuPlain(e.target) === t);
      if (!rows.length) continue;
      const rec = {
        name: t,
        ents: rows,
        handle: (mnuData && mnuData.handle) | 0,
        ts: Date.now()
      };
      // No id yet: still list it (so it is visible and can resolve later) under a key that is
      // clearly not a rule key. mnuReconcileRecent promotes it once the id lands.
      rec.key = mnuRuleKey(rows, t, rec) || ('noid:' + t);
      for (let i = 0; i < mnuRecent.length; i++)
        if (mnuRecent[i].key === rec.key) { mnuRecent.splice(i, 1); break; }
      mnuRecent.unshift(rec);
    }
    if (mnuRecent.length > 12) mnuRecent.length = 12;
  }

  // Id resolution is ASYNC, so a capture taken before the lookup returns is keyed on a fallback
  // and the same thing then appears twice - once "no id resolved", once with its id (seen live:
  // two rows each for the Slayer cape and the Pollnivneach teleport). Re-key every capture against
  // its own stored context whenever ids may have landed, and drop the older duplicate.
  function mnuReconcileRecent() {
    // Captures still without an id keep asking the scene; it is memoized per name so this is a
    // dictionary lookup once resolved.
    mnuResolveFromScene(mnuRecent.map(r => r.name));
    // Captures made before their lookup ran keep asking until it lands.
    const seen = {};
    const wasSel = mnuSel ? mnuRecent.find(function (x) { return x.key === mnuSel; }) : null;
    for (let i = 0; i < mnuRecent.length; i++) {
      const r = mnuRecent[i];
      const t = mnuTargets(r.ents)[0];
      if (t) r.key = mnuRuleKey(r.ents, t, r) || ('noid:' + t);
      if (seen[r.key]) { mnuRecent.splice(i--, 1); continue; }   // newest-first: keep the first
      seen[r.key] = 1;
    }
    // Follow the open capture across a re-key, or the editor would snap back to the list.
    if (wasSel && wasSel.key !== mnuSel) mnuSel = wasSel.key;
  }

  // A rule is scoped to the target's OPTION SET, not just its name. Several distinct objects share
  // one name while offering different options - the Shifting tombs entrances differ per district,
  // one offering "Travel Merchant District" where another offers "Travel Imperial District". Keyed
  // on the name alone, a rule learned from one variant was applied to another whose menu does not
  // contain those verbs, so the resulting order did not match what the panel showed.
  //
  // The set is sorted, so it identifies the variant regardless of the order we have imposed on it.
  const mnuVarSep = '\u001e';   // record separator: cannot occur in a verb or target
  // Prefer the LOC ID: the launcher resolves the companion's tile handle against the map cache
  // (the placement at that tile whose name matches the target), so two entrances sharing one id
  // share one rule. Locs 109059 / 109062 / 109065 are all named "Shifting tombs" with different
  // option sets, so the name alone is not an identity.
  //
  // Next the instance HANDLE (the hover-target block's +0x000: packed (x<<16)|y for a world
  // object, an index for an actor) - narrower than the id (per-instance) but never wrong, and
  // what a morphed loc falls back to when its cache name misses the live variant's. The option
  // set stays as the last fallback because same id implies same options.
  // Every key generation for a target, best first: loc id, instance handle, option set, bare
  // name. New rules SAVE under the first; lookups walk the list so a rule saved before a richer
  // key existed keeps working instead of being silently shadowed the moment that key resolves.
  // An INTERFACE row's handle is the packed (group<<16)|comp of the widget, and every backpack
  // slot is its own comp - so keying an item rule on the handle binds it to the SLOT, and the
  // rule disappears the moment the item is moved (seen live: the same potato cactus reordered in
  // one slot and not in another). Items are keyed on their NAME, which is what actually
  // identifies them; only world entities get the handle/loc-id generations.
  const mnuTypeOf = (ents, target) => {
    for (const e of ents || []) if (mnuPlain(e.target) === target) return (e.type | 0);
    return -1;
  };
  // A rule is keyed on the CONFIG ID of what is under the cursor, and on nothing else. Every
  // softer identity was tried and each misapplied rules: an instance handle bound a rule to one
  // tile or one spawn, a widget handle bound an item rule to a backpack SLOT, and a name matched
  // every same-named thing. Ids come from `hoverEntity` (see mnuSelfResolve) and accumulate in
  // mnuIdSeen, so a capture made while the cursor was already moving away picks its id up on a
  // later poll.
  // A MENU target carries a suffix the entity's own name does not: "Mr Da1Nonly (level: 152)",
  // "OS Xela the Stuffed (skill: 1791)". hoverEntity reports the bare name, so the two must be
  // compared with the suffix removed or every levelled NPC fails to resolve.
  const mnuBaseName = s => String(s || '').replace(/\s*\((?:level|skill|tier)\s*:[^)]*\)\s*$/i, '').trim();

  function mnuTgtId(target) {
    const seen = mnuIdSeen[target] || mnuIdSeen[mnuBaseName(target)];
    return seen ? (seen.id | 0) : 0;
  }

  // Resolve what is under the cursor, for the RULE KEY only. `hoverEntity` is the client's own
  // answer and returns ids for items, NPCs and locs alike (Player State shows the same values).
  // Nothing downstream needs this: the companion matches by name because only one entity's rules
  // are ever loaded.
  function mnuSelfResolve() {
    if (!bridge() || !bridge().hoverEntity) return;
    const t0 = mnuTargets((mnuData && mnuData.entries) || [])[0];
    if (!t0) return;
    let hv = null;
    try { hv = JSON.parse(bridge().hoverEntity(myPid()) || '{}'); } catch (e) { return; }
    if (!hv || !hv.ok || !(hv.id > 0)) return;
    const kind = hv.kind === 'item' ? 0 : hv.kind === 'loc' ? 1 : hv.kind === 'npc' ? 2 : -1;
    if (kind < 0) return;
    // TRUST hoverEntity's OWN kind. Deriving the kind from the menu row's action class was wrong
    // and broke locs specifically: a loc's first row is a content verb (Enter, Travel ...) whose
    // action class carries an assorted ordinal (the seven special classes use 0/2/7/8), not the
    // entity type 3 - so the cross-check rejected every loc and left it "id not resolved", while
    // items and NPCs passed because their rows use the generic classes.
    // Record it under the name the CLIENT gave for the entity it resolved, not the menu's first
    // target: in a merged menu those can be different things, and attributing an id to the wrong
    // entity is how one entity's rule ended up keyed to another's.
    const who = mnuBaseName(mnuPlain(hv.name || '')) || mnuBaseName(t0);
    const prev = mnuIdSeen[who];
    if (!prev || prev.id !== (hv.id | 0)) {
      mnuIdSeen[who] = { kind: kind, id: hv.id | 0 };
      mnuSig = ''; mnuListSig = '';
    }
  }

  // Resolve anything the hover slot did not name, from the LIVE SCENE the reader already builds
  // (the same lists the Scene panel's NPCS and OBJECTS tabs print, each row carrying an id).
  //
  // The hover slot only ever describes the TOPMOST entity, so in a merged menu - a marketeer
  // standing on a stall - the others never got an id and listed as unruleable. The scene knows
  // them all. A name matching exactly one nearby entity is taken; several distinct ids sharing
  // the name are left alone rather than guessing which one the menu meant.
  function mnuResolveFromScene(names) {
    if (!bridge() || !bridge().sceneEntities) return;
    const want = names.filter(n => n && !mnuIdSeen[n] && !mnuIdSeen[mnuBaseName(n)]);
    if (!want.length) return;
    let sc = null;
    try { sc = JSON.parse(bridge().sceneEntities(myPid(), 20) || '{}'); } catch (e) { return; }
    if (!sc) return;
    const pools = [{ list: sc.npcs || [], kind: 2 }, { list: sc.objects || [], kind: 1 }];
    let got = false;
    for (const n of want) {
      const base = mnuBaseName(n);
      for (const pool of pools) {
        const ids = {};
        for (const e of pool.list)
          if (mnuBaseName(mnuPlain(e.name || '')) === base && (e.id | 0) > 0) ids[e.id | 0] = 1;
        const uniq = Object.keys(ids);
        if (!uniq.length) continue;
        if (uniq.length === 1) { mnuIdSeen[base] = { kind: pool.kind, id: uniq[0] | 0 }; got = true; }
        break;
      }
    }
    if (got) { mnuSig = ''; mnuListSig = ''; }
  }

  function mnuVarKeys(ents, target, ctx) {
    // The KIND comes from hoverEntity too, never from the row's action class: a loc's content
    // verbs carry assorted class ordinals, so deriving it from the menu rejected every loc.
    const seen = mnuIdSeen[target] || mnuIdSeen[mnuBaseName(target)];
    if (!seen || !(seen.id > 0)) return [];
    const kind = seen.kind >= 0 ? seen.kind : mnuKindOf(mnuTypeOf(ents, target));
    if (kind < 0) return [];
    return [mnuKeyKind(kind) + ':' + seen.id];
  }

  function mnuVariant(ents, target) { return mnuVarKeys(ents, target)[0]; }
  function mnuRuleKey(ents, target, ctx) {
    const keys = mnuVarKeys(ents, target, ctx);
    for (const k of keys) if (mnuRules[k]) return k;
    return keys[0];
  }

  // Every distinct entity in the menu. Overlapping entities merge into one list, so a menu can
  // carry three NPCs at once and naming only the first misreports whose options these are.
  function mnuTargets(ents) {
    const out = [];
    for (const e of ents || []) {
      const t = mnuPlain(e.target);
      if (t && out.indexOf(t) < 0) out.push(t);
    }
    return out;
  }

  // Push only the rules that apply to the menu on screen, and push the PLAIN target name - the
  // companion matches a pin on verb + target and knows nothing about variants. Sending every
  // stored rule would put two same-named variants' pins in the list at once, where the first
  // match wins per verb and the wrong variant's order gets applied.
  // ONLY the rules for what is under the cursor right now.
  //
  // This is why the companion needs no ids: with a single entity's rules loaded there is nothing
  // for an id to disambiguate, and the panel - which knows the entity's id - decides WHICH rule
  // to send. Preloading every rule is what forced id matching into the companion, and that whole
  // path was removed after it kept resolving inconsistently.
  //
  // Cost: the first tick of a newly hovered thing can draw the game's own order before the next
  // poll pushes its rule. That is a visible flicker, not a wrong menu.
  function mnuFlatten(ents) {
    const out = [];
    const seen = {};
    for (const t of mnuTargets(ents)) {
      if (seen[t]) continue;
      seen[t] = 1;
      const key = mnuRuleKey(ents, t);
      for (const verb of (key && mnuRules[key]) || []) out.push(verb + '	' + t);
    }
    return out.join('\n');
  }

  async function mnuPush() {
    mnuSaveRules();
    // Send the rules for every entity under the cursor, PLUS the one being edited. A merged menu
    // needs all of them: pins carry the target name, so the companion applies each entity's rule
    // to its own rows and the two never blend into one order. The edited capture is included so a
    // change takes effect even while the cursor is elsewhere.
    const live = (mnuData && mnuData.entries) || [];
    const parts = [mnuFlatten(live)];
    // The edited capture only counts while the panel is on screen. It survives the panel closing,
    // and a latch left on one same-named variant would then keep leaking its order onto whatever
    // is actually hovered, for as long as the client runs.
    if (mnuSel && paneVisible('menuswap')) {
      const sel = mnuFlatten(mnuActiveEnts());
      if (sel && parts.indexOf(sel) < 0) parts.push(sel);
    }
    const pins = parts.filter(Boolean).join('\n');
    try { await bridge().menuPins(myPid(), pins); } catch (e) {}
  }

  async function mnuTick() {
    if (!bridge() || !bridge().menuStatus) return;
    await mnuLoadRules();          // once, before anything can overwrite the durable copy
    const vis = paneVisible('menuswap');
    // PUBLISHING IS NOT A UI CONCERN. mnuPush sends only the rules for the entities in the
    // published menu, so with publishing off the panel is choosing from a frozen snapshot: a
    // newly hovered thing never gets its rule pushed and the game draws its own order. That is
    // the "rules only work while the panel is open" report. Keep the feed alive whenever a rule
    // could apply; kEnablePanel vs kEnableBackground only tells the probe whether to re-arm its
    // dump budget.
    const want = vis || !!Object.keys(mnuRules).length;
    const mode = !want ? 0 : vis ? 1 : 2;
    if (mode !== mnuOn && bridge().menuEnable) {
      // Re-arm on every transition INTO publishing (including the first tick, mnuOn -1): the
      // companion's pin copy dies with the client. Panel<->background is not such a transition,
      // and re-pushing there would send the closed panel's stale selection.
      const wasOff = mnuOn <= 0;
      mnuOn = mode;
      try { await bridge().menuEnable(myPid(), mode); } catch (e) {}
      if (wasOff && want) mnuPush();
    }
    // KEEP POLLING WITH THE TAB CLOSED, and keep acting on what comes back. Which rule to send is
    // decided from the entities in this payload, so a tick that skips the decision leaves the
    // companion holding the previous menu's pins and the game draws its own order - reported as
    // "the reordering isn't taking place without the panel open", and before that as "went to
    // lobby, came back, none of the changes stayed until I clicked the panel a few times".
    // Rules also have to be re-pushed after the client rebuilds its menu state (a lobby trip
    // clears the companion's copy), which the pin-context check below detects via seq.
    let d = null;
    try { d = JSON.parse(await bridge().menuStatus(myPid()) || '{}'); } catch (e) { return; }
    if (!want) return;               // no rules stored: nothing to resolve, nothing to push
    // A new menu discards the working order - it belonged to the menu it was built from.
    if (d && mnuData && d.seq !== mnuData.seq) mnuOrder = null;
    mnuData = d || {};
    // Pins FOLLOW THE LATCH. mnuFlatten pushes only the rules that apply to the captured menu,
    // so the companion's pin list is wrong the moment a different menu is latched: stale pins
    // leak one same-named variant's order onto another, and a tab re-arm while an unruled
    // object was latched clears them for good. Seen live: rule set at one Shifting tombs
    // entrance, scene tab visited at another, and back at the first the panel showed the rule
    // while the companion held no pins - "reordered" tag on, menu drawn in game order. locId
    // joins seq because it resolves against the cache after the capture and the rule may be
    // keyed on it.
    mnuSelfResolve();
    // Everything else in the menu the hover slot did not name - in a merged menu that is every
    // entity except the topmost one.
    mnuResolveFromScene(mnuTargets((mnuData.entries) || []));
    mnuUpgradeKeys((mnuData.entries) || []);
    // Everything above resolves the rule KEY and so has to run either way. The recent-menu list
    // and the render exist only for the panel, and mnuNoteRecent would otherwise accumulate every
    // menu opened all session with nobody to read it.
    if (vis) { mnuNoteRecent(); mnuReconcileRecent(); }
    const ctx = (mnuData.seq | 0) + '|' + (mnuData.locId | 0) + '|' + (mnuData.handle | 0);
    if (ctx !== mnuPinCtx) { mnuPinCtx = ctx; mnuPush(); }
    if (vis) renderMenuSwap();
  }

  // Fixed = the bottom row, plus any TARGETLESS row in a menu that HAS targets. Must match the
  // companion's rule exactly, or the panel offers a move the companion refuses and the two orders
  // drift apart.
  //
  // Neither half alone works. "Has a target" made interface menus (World Map / Lodestone Network)
  // entirely unmovable, since those options carry no target. "Bottom row only" let "Walk here" be
  // moved in a world menu - but the game REPOSITIONS it when drawing, so the drawn order stopped
  // matching the dispatch order and clicking a row ran a different one.
  //
  // In a world menu the targetless rows are exactly the ones the game repositions; in an interface
  // menu nothing has a target, so only the bottom row is fixed.
  const mnuAnyTargeted = ents => (ents || []).some(e => !!mnuPlain(e.target));
  function mnuMovable(e, ents) {
    if ((e.slot | 0) === 0) return false;
    // A capture holds ONE entity's rows, so every row here belongs to it; it is reorderable as
    // long as that entity resolved an id.
    const tg = mnuPlain(e.target);
    if (tg && !mnuVarKeys(ents, tg).length) return false;
    return !(mnuAnyTargeted(ents) && !mnuPlain(e.target));
  }

  // The order on screen: the working order if one is being edited, else the saved rule, else
  // whatever the game produced.
  // A row is identified by verb + target: a merged menu repeats verbs across entities (three
  // NPCs each offering Follow / Examine), so the verb alone is not unique.
  const mnuSep = '\u001f';   // unit separator: cannot occur in a menu verb or target
  const mnuKey = e => e.verb + mnuSep + mnuPlain(e.target);   // target may be empty; verb still distinguishes
  function mnuSplit(k) {
    const i = k.indexOf(mnuSep);
    return i < 0 ? { verb: k, target: '' } : { verb: k.slice(0, i), target: k.slice(i + 1) };
  }

  function mnuCurrent(ents) {
    const keys = ents.filter(function (e) { return mnuMovable(e, ents); }).map(mnuKey);
    // The working order belongs to the menu it was edited on. It is reset when seq changes,
    // but seq only moves on a NEW capture - a stale order surviving a tab switch (mnuData
    // starts null, so the seq comparison never fires) would relabel rows with another menu's
    // verbs. Keep it only while its key set still matches the latch.
    if (mnuOrder) {
      const cur = keys.slice().sort().join(mnuVarSep);
      const old = mnuOrder.slice().sort().join(mnuVarSep);
      if (cur === old) return mnuOrder;
      mnuOrder = null;
    }
    // Rank each row by where its verb sits in its own target's rule.
    const rankOf = function (k) {
      const p = mnuSplit(k);
      const rule = mnuRules[mnuRuleKey(ents, p.target)] || mnuRules['*'];
      const i = rule ? rule.indexOf(p.verb) : -1;
      return i < 0 ? 9999 : i;
    };
    const idx = {};
    keys.forEach(function (k, i) { idx[k] = i; });
    return keys.slice().sort(function (a, b) {
      const d = rankOf(a) - rankOf(b);
      return d !== 0 ? d : idx[a] - idx[b];      // stable: unruled rows keep game order
    });
  }

  // Scope a rule to the MOVED ROW's own target, not the menu's. Overlapping entities merge into
  // one menu - three NPCs in a single list - so "the menu's target" would attach a rule for
  // ZyroFett to the market guard.
  function mnuMove(key, dir) {
    // The CAPTURE being edited, not whatever is live: with the live list the row key was not
    // found and the arrows silently did nothing.
    const ents = mnuActiveEnts();
    const cur = mnuCurrent(ents).slice();
    const i = cur.indexOf(key), j = i + dir;
    if (i < 0 || j < 0 || j >= cur.length) return;
    cur[i] = cur[j]; cur[j] = key;
    mnuOrder = cur;
    // Rebuild one rule list per target present, in the new display order.
    const byTgt = {};
    for (const k of cur) {
      const p = mnuSplit(k);
      // A targetless row (interface menus carry no target) falls back to the any-object rule.
      // The companion matches a pin on verb + target and an empty target means "any", so this is
      // the scope that can actually be expressed; dropping the row instead silently discarded the
      // rule and the menu never reordered.
      // Always scoped to the thing itself. A rule that applied to "any object" reordered
      // everything sharing a verb, which was never what anyone meant by moving one row.
      // The rule belongs to the id of the row's own target. A row whose target has no id
      // cannot hold a rule at all, rather than falling back to a name or a wildcard.
      const t = p.target ? mnuVariant(ents, p.target) : '';
      if (!t) continue;
      (byTgt[t] = byTgt[t] || []).push(p.verb);
    }
    // Saving under the best key retires the older generations, or a stale option-set rule would
    // resurrect the old order whenever the richer key fails to resolve.
    for (const t of mnuTargets(ents))
        for (const k of mnuVarKeys(ents, t)) delete mnuRules[k];
    for (const t in byTgt) {
      mnuRules[t] = byTgt[t];
      // Remember what the key refers to: an id-keyed rule is otherwise just a number in the
      // Saved rules list.
      const m = /^(item|loc|npc):/.exec(t);
      if (m) {
        for (const tg of mnuTargets(ents))
          if (mnuVarKeys(ents, tg)[0] === t) { mnuNames[t] = tg; break; }
      }
    }
    mnuPush();
    mnuNote = '';
    mnuSig = ''; mnuListSig = ''; renderMenuSwap();
  }

  // Every saved rule, whatever is under the cursor. A rule key is "<target><sep><generation>"
  // (or bare target / '*'), so the display name is everything before the separator; the
  // generation is shown as a short qualifier because two same-named objects can hold different
  // rules and the list must not look like a duplicate.
  function mnuAllRules() {
    const out = [];
    for (const key in mnuRules) {
      const verbs = mnuRules[key] || [];
      if (!verbs.length) continue;
      const m = /^(item|loc|npc):(\d+)$/.exec(key);
      if (m) {
        const kindLabel = m[1] === 'loc' ? 'object' : m[1];
        out.push({
          key: key,
          name: mnuNames[key] || ('(' + kindLabel + ' ' + m[2] + ')'),
          scope: kindLabel + ' id ' + m[2],
          solid: true,                       // keyed on a config id: the durable kind
          verbs: verbs
        });
        continue;
      }
      const i = key.indexOf(mnuVarSep);
      const name = i < 0 ? key : key.slice(0, i);
      const gen = i < 0 ? '' : key.slice(i + 1);
      out.push({
        key: key,
        name: key === '*' ? 'Any object' : (mnuNames[key] || name || '(unnamed)'),
        scope: gen.charAt(0) === 'L' ? 'object id ' + gen.slice(1)
             : gen.charAt(0) === 'h' ? 'only this one spawn or tile - hover it again to upgrade'
             : gen ? 'matched by its options - hover it again to upgrade'
             : (key === '*' ? 'every object with these options' : 'matched by name'),
        solid: gen.charAt(0) === 'L',
        verbs: verbs
      });
    }
    out.sort((a, b) => a.name.localeCompare(b.name));
    return out;
  }

  // ---- add by id ---------------------------------------------------------------------------
  // Edit a saved rule: the same ordering UI, working on the rule's own verbs. The cache is
  // deliberately NOT consulted for extra options - its option order is not the order the game
  // draws, so offering it here produced lists that disagreed with the real menu.
  function mnuEditRule(key) {
    mnuAdd = {
      name: mnuNames[key] || key.split(mnuVarSep)[0] || key,
      order: (mnuRules[key] || []).slice(),
      editKey: key
    };
    mnuView = 'edit';
    mnuSig = ''; mnuListSig = ''; renderMenuSwap();
  }

  function mnuAddMove(i, dir) {
    const j = i + dir;
    if (j < 0 || j >= mnuAdd.order.length) return;
    const t = mnuAdd.order[i]; mnuAdd.order[i] = mnuAdd.order[j]; mnuAdd.order[j] = t;
    mnuSig = ''; mnuListSig = ''; renderMenuSwap();
  }

  function mnuAddSave() {
    if (!mnuAdd.editKey) return;
    mnuRules[mnuAdd.editKey] = mnuAdd.order.slice();
    mnuPush();
    mnuAdd = { name: '', order: [], editKey: '' };
    mnuView = 'rules';
    mnuSig = ''; mnuListSig = ''; renderMenuSwap();
  }

  function mnuForget(key) {
    delete mnuRules[key];
    mnuPush();
    mnuSig = ''; mnuListSig = ''; renderMenuSwap();
  }

  function renderMenuSwap() {
    const c = paneRoot('menuswap'); if (!c) return;
    let wrap = $('mnuWrap');
    if (!wrap) {
      c.innerHTML = '';
      wrap = document.createElement('div');
      wrap.id = 'mnuWrap';
      // Layout: the two views on the left, Back on the right, everything explanatory below the
      // list so the controls stay in one place and the reading order is title -> rows -> status.
      wrap.innerHTML =
          '<div class="pet-toolbar" style="gap:6px;align-items:center">'
        + '  <button class="pet-chip" data-view="menu">Hovered</button>'
        + '  <button class="pet-chip" data-view="rules">Saved rules</button>'
        + '  <button class="pet-chip" id="mnuBack" style="margin-left:auto">Back</button>'
        + '</div>'
        + '<div id="mnuTgt" class="pet-count" style="font-weight:600;margin:2px 0 4px"></div>'
        + '<div id="mnuList" class="pet-list"></div>'
        + '<div id="mnuPromo" class="pet-count" style="opacity:.75;margin-top:6px;display:none"></div>'
        + '<div id="mnuNote" class="pet-count" style="opacity:.55;margin-top:2px"></div>'
        + '<div id="mnuInfo" class="pet-count" style="opacity:.35;margin-top:2px"></div>';
      c.appendChild(wrap);
      wrap.addEventListener('click', function (e) {
        const vb = e.target.closest('[data-view]');
        if (vb) {
          mnuConfirmForget = '';
          if (vb.dataset.view !== 'menu') mnuSel = '';
          mnuView = vb.dataset.view; mnuSig = ''; mnuListSig = ''; renderMenuSwap(); return;
        }
        const fg = e.target.closest('[data-forget]');
        if (fg) { mnuConfirmForget = fg.dataset.forget;
                  mnuSig = ''; mnuListSig = ''; renderMenuSwap(); return; }
        if (e.target.closest('[data-forgetno]')) {
          mnuConfirmForget = ''; mnuSig = ''; mnuListSig = ''; renderMenuSwap(); return;
        }
        const fy = e.target.closest('[data-forgetyes]');
        if (fy) { mnuConfirmForget = ''; mnuForget(fy.dataset.forgetyes); return; }
        const ed = e.target.closest('[data-edit]');
        if (ed) { mnuEditRule(ed.dataset.edit); return; }
        if (e.target.closest('#mnuAddSave')) { mnuAddSave(); return; }
        const au = e.target.closest('[data-aup]');
        if (au) { mnuAddMove(au.dataset.aup | 0, -1); return; }
        const ad = e.target.closest('[data-adown]');
        if (ad) { mnuAddMove(ad.dataset.adown | 0, 1); return; }
        // The latched menu's own arrows. These MUST stay in the click handler - they were
        // accidentally moved into keydown while the add form was being wired up, which left the
        // rows unmovable by mouse.
        const sl = e.target.closest('[data-sel]');
        if (sl) { mnuSel = sl.dataset.sel; mnuOrder = null; mnuSig = ''; mnuListSig = '';
                  renderMenuSwap(); return; }
        if (e.target.closest('#mnuBack')) {
          mnuConfirmForget = '';
          if (mnuView === 'edit') { mnuView = 'rules'; mnuAdd = { name: '', order: [], editKey: '' }; }
          mnuSel = ''; mnuOrder = null;
          mnuSig = ''; mnuListSig = ''; renderMenuSwap(); return;
        }
        const up = e.target.closest('[data-up]');
        if (up) { mnuMove(up.dataset.up, -1); return; }
        const dn = e.target.closest('[data-down]');
        if (dn) { mnuMove(dn.dataset.down, 1); return; }
      });
    }

    const d = mnuData || {};
    const ents = mnuActiveEnts();
    const tgts = mnuTargets(ents);
    const tgt = tgts[0] || '';
    const order = mnuCurrent(ents);
    const ruled = !!(mnuRules['*'] || tgts.some(function (t) {
      return mnuRules[mnuRuleKey(ents, t)];
    }));
    // TWO signatures, because the two halves of this panel change at completely different rates.
    //
    // The status line carries counters that tick up every frame the hook runs (reorders, diag,
    // lanes), so a single signature covering both meant the ROW LIST was rebuilt on every poll.
    // Rebuilding innerHTML destroys the buttons under the cursor - they visibly flashed, and a
    // click whose mousedown and mouseup landed on either side of a rebuild was swallowed.
    //
    // Text updates are textContent and harmless at any rate; only the list is torn down. So the
    // list gets a CONTENT-ONLY signature and is left alone while nothing it displays has changed.
    const statusSig = (d.ok ? 1 : 0) + '|' + (d.hooked ? 1 : 0) + '|' + (d.reordered | 0)
              + '|' + (d.diag || []).join(',') + '|' + (d.stage || []).join(',')
              + '|' + (d.lane || []).join(',') + '|' + mnuNote
              + '|' + (d.promo | 0) + '|' + (d.promoPrio | 0) + '|' + (d.promoPartner | 0)
              + '|' + (d.promoSrc | 0) + '|' + (d.promoVerb || '') + '|' + mnuConfirmForget + '|' + ruled;
    // Content only: what the rows actually draw. Deliberately NOT seq - a fresh capture of an
    // identical menu is not a visible change.
    const listSig = order.join(',') + '|' + tgts.join('|') + '|' + (d.handle | 0)
              + '|' + JSON.stringify(mnuIdSeen) + '|' + mnuView
              // The saved-rules view renders from mnuRules, so its contents belong here or an
              // edit made while it is open would not repaint.
              + '|' + JSON.stringify(mnuRules)
              + '|' + mnuAdd.editKey + '|' + mnuAdd.order.join(',')
              // The Forget confirmation swaps buttons inside a row, so it is list content.
              + '|' + mnuConfirmForget + '|' + mnuSel
              // Targets belong in the signature: a merged menu's rows can differ only by target,
              // and on verbs alone such a change would read as identical and never repaint.
              + '|' + ents.map(mnuKey).join(',')
              + '|' + ents.map(e => (e.slot | 0) + ':' + (e.type | 0)).join(',');
    if (statusSig === mnuSig && listSig === mnuListSig) return;
    mnuSig = statusSig;

    const nRules = mnuAllRules().length;
    wrap.querySelectorAll('[data-view]').forEach(function (b) {
      const on = b.dataset.view === mnuView;
      b.className = 'pet-chip' + (on ? ' on' : '');
      if (b.dataset.view === 'rules') b.textContent = 'Saved rules (' + nRules + ')';
    });
    // Scope and Reset act on the latched menu, so they only belong to that view.
    const menuOnly = mnuView === 'menu' ? '' : 'none';
    // Back / Clear only mean anything with something open.
    const openCap = ((mnuView === 'menu' && mnuSel) || mnuView === 'edit') ? '' : 'none';
    const bk = $('mnuBack');
    if (bk) bk.style.display = openCap;
    const tg = $('mnuTgt');
    if (tg) tg.style.display = (mnuView === 'menu') ? '' : 'none';
    // Name every entity in the menu, so a merged one reads as three targets and not as one.
    if (tg) tg.textContent = tgts.length
      ? (tgts.join('   ·   ') + (ruled ? '   ·   reordered' : '')) : '';

    const info = $('mnuInfo');
    if (info) {
      const g = d.diag || [], st = d.stage || [];
      // When nothing is reordered, name the stage that stopped it rather than leaving the
      // reader to guess between the builder not firing, rules not arriving, and no match.
      let why = '';
      if (ents.length && ruled && !(d.reordered | 0)) {
        why = !(g[2] | 0)  ? 'builder hook never fired'
            : !(g[3] | 0)  ? 'companion has no rules'
            : !(st[0] | 0) ? 'entry vector unusable at build time'
            : !(st[1] | 0) ? 'no match (last verb: "' + (d.lastVerb || '') + '")'
            : !(st[2] | 0) ? 'permutation incomplete'
            : !(st[3] | 0) ? 'order already correct'
                           : 'write failed';
      }
      // Which parallel lists a reorder actually landed in. The menu comes from the first;
      // left-click is fed by one of the others, so this is what identifies it.
      const ln = d.lane || [];
      const lanes = ln.length
        // [5] counts CLASS BOOSTS, not a lane: how often the pinned row had to be lent the
        // promoted action class so the snapshot's re-sort could not sink it (Drop / Examine).
        ? ['menu', 'target', 'nocanc', 'misc', 'iface', 'class']
            .map(function (nm, i) { return (ln[i] | 0) ? nm : null; })
            .filter(Boolean).join('+')
        : '';
      // Problems read as sentences; the healthy state is a quiet counter, not a status report.
      info.textContent = !d.ok     ? 'Companion not loaded'
                       : !d.hooked ? 'Menu hook not installed for this build'
                       : why       ? why
                       : ents.length ? (d.reordered | 0) + ' reorders'
                                       + (lanes ? '  ·  ' + lanes : '')
                                     : '';
    }
    // Why the LEFT-CLICK default could or could not follow the pin. The game sorts menu rows by
    // an action CLASS and only promoted classes can be the default, so pinning Drop or Examine
    // needs the companion to hand the row a promoted class. Each refusal names itself here
    // rather than in a log: every one of these was a real dead end while this was built.
    const promo = $('mnuPromo');
    if (promo) {
      const st = d.promo | 0, pv = d.promoVerb || 'that row';
      // Which source found the promoted class. "learned" is read off the live menus (the class
      // several verbs share) and survives game updates; "derived" is the cold-start fallback and
      // rests on an address adjacency, so it is worth re-checking after an update.
      const src = (d.promoSrc | 0) === 1 ? ' (matched by behaviour, not structure)' : '';
      promo.textContent =
          st === 2 ? 'Left-click: "' + pv + '" promoted to the default' + src
        : st === 3 ? 'Left-click: "' + pv + '" is class ' + (d.promoPrio | 0)
                     + ', which has no proven promoted form - the game keeps its own default'
        : st === 4 ? 'Left-click: no promoted class found for "' + pv + '" (found '
                     + (d.promoPartner | 0) + ', expected 57) - a game update likely moved them'
        : st === 5 ? 'Left-click: the class write failed'
        : '';
      promo.style.display = promo.textContent ? '' : 'none';
    }

    const note = $('mnuNote');
    if (note) {
      note.textContent = mnuNote
        || (mnuView === 'rules' ? 'Edit reorders a saved rule. Forget removes it.'
          : mnuView === 'edit'  ? 'Arrows set the order, then Save changes.'
          : mnuSel              ? 'Arrows set the order. It applies to every future menu for this object.'
                                : 'Hover things in game to collect them here, then pick one to reorder.');
    }

    const list = $('mnuList');
    if (!list) return;
    if (listSig === mnuListSig) return;      // nothing the rows draw has changed: leave the DOM
    mnuListSig = listSig;

    // Add-by-id: author a rule for something you are not standing next to. The cache holds the
    // name and the exact option list the client builds its menu from, so nothing has to be
    // hovered - and the rule is keyed on the id, which is the durable identity.
    // The rule editor, reached from Saved rules -> Edit. Reorders a rule for something that is
    // not in front of you; anything you can hover is better done from the Hovered list, where
    // the options are the game's own.
    if (mnuView === 'edit') {
      let ah = '<div class="pet-row" style="font-weight:600">' + htmlEsc(mnuAdd.name) + '</div>';
      if (!mnuAdd.order.length) {
        ah += '<div class="pet-row" style="opacity:.5">This rule has no options saved.</div>';
      } else {
        for (let i = 0; i < mnuAdd.order.length; i++) {
          ah += '<div class="pet-row">'
              + '<div style="flex:0 0 24px;opacity:.4">' + (i + 1) + '</div>'
              + '<div style="flex:1"><b>' + htmlEsc(mnuAdd.order[i]) + '</b></div>'
              + '<button class="pet-chip" data-aup="' + i + '"'
              + (i === 0 ? ' style="opacity:.2"' : '') + '>&#9650;</button>'
              + '<button class="pet-chip" data-adown="' + i + '"'
              + (i === mnuAdd.order.length - 1 ? ' style="opacity:.2"' : '') + '>&#9660;</button>'
              + '</div>';
        }
        ah += '<div class="pet-row"><button class="pet-chip on" id="mnuAddSave">'
            + 'Save changes</button></div>';
      }
      list.innerHTML = ah;
      return;
    }

    // The saved-rules view: every rule that exists, independent of what is under the cursor.
    if (mnuView === 'rules') {
      const all = mnuAllRules();
      let ah = '';
      for (const r of all) {
        ah += '<div class="pet-row">'
            + '<div style="flex:1">'
            + '<b>' + htmlEsc(r.name) + '</b>'
            + (r.scope ? '<span style="opacity:' + (r.solid ? '.55' : '.45') + '"> &middot; '
                       + htmlEsc(r.scope) + '</span>' : '')
            + '<div style="opacity:.6;font-size:11px">Menu order: '
            + htmlEsc(r.verbs.join('  →  ')) + '</div>'
            + '</div>'
            // Forget asks first: a rule is deliberate work and there is no undo.
            + (mnuConfirmForget === r.key
                ? '<span style="opacity:.7;font-size:11px;margin-right:6px">Delete it?</span>'
                  + '<button class="pet-chip on" data-forgetyes="' + htmlEsc(r.key) + '">Yes</button>'
                  + '<button class="pet-chip" data-forgetno="1">Cancel</button>'
                : '<button class="pet-chip" data-edit="' + htmlEsc(r.key) + '">Edit</button>'
                  + '<button class="pet-chip" data-forget="' + htmlEsc(r.key) + '">Forget</button>')
            + '</div>';
      }
      list.innerHTML = ah || '<div class="pet-row" style="opacity:.5">No saved reorderings yet.</div>';
      return;
    }
    // Rows render at their TRUE drawn positions. A structural row keeps its exact place in the
    // game's array - the companion never moves one, because the game imposes its own placement
    // for them on the DISPATCH side and shifting one makes a drawn row run a different action
    // (Examine ran Walk here, live). So the movable rows fill the positions AROUND them, in rule
    // order. The old movable-then-fixed listing promised "Examine above Walk here", an order the
    // reorder can never produce.
    // No capture chosen: show what has been hovered recently and let one be picked. This is the
    // fix for the vanishing menu - the live capture keeps updating this list in the background
    // while whatever you selected stays put.
    if (mnuView === 'menu' && !mnuSel) {
      let rh = '';
      if (!mnuRecent.length) {
        rh = '<div class="pet-row" style="opacity:.5">Hover an object, NPC or item in game and'
           + ' it appears here to reorder.</div>';
      } else {
        for (const r of mnuRecent) {
          const ruledHere = !!mnuRules[r.key];
          // Another rule sharing this name exists: it is NOT applied here (the companion would
          // match it by name), but say so, because two same-named entrances behaving differently
          // is otherwise baffling.
          let twin = false;
          for (const k in mnuRules)
            if (k !== r.key && (mnuNames[k] || k.split(mnuVarSep)[0]) === r.name) twin = true;
          // Show the identity the rule is keyed on. Two same-named things are otherwise
          // indistinguishable here, which is exactly the case that needs checking when one
          // reorders and the other does not.
          const km = /^(item|loc|npc):(\d+)$/.exec(r.key);
          let idTag;
          if (km) {
            idTag = (km[1] === 'loc' ? 'object' : km[1]) + ' id ' + km[2];
          } else {
            // Name the SOURCE of the failure: the id comes from the launcher, so distinguish
            // "the launcher answered 0" from "the launcher is not answering at all".
            idTag = (mnuData && mnuData.ok) ? 'id not resolved'
                                            : 'companion/launcher version mismatch';
          }
          rh += '<div class="pet-row">'
              + '<div style="flex:1"><b>' + htmlEsc(r.name) + '</b>'
              + '<span style="opacity:.45"> &middot; ' + htmlEsc(idTag) + '</span>'
              + (ruledHere ? '<span style="opacity:.5"> &middot; reordered</span>'
                           : twin ? '<span style="opacity:.5"> &middot; another '
                                  + htmlEsc(r.name) + ' has its own rule</span>' : '')
              + '<div style="opacity:.5;font-size:11px">'
              + htmlEsc(r.ents.filter(e => mnuMovable(e, r.ents)).map(e => e.verb).join('  →  '))
              + '</div></div>'
              + (km ? '<button class="pet-chip" data-sel="' + htmlEsc(r.key) + '">Reorder</button>'
                    : '<button class="pet-chip" style="opacity:.3">Reorder</button>')
              + '</div>';
        }
      }
      list.innerHTML = rh;
      return;
    }

    let h = '';
    let mi = 0;
    for (let i = 0; i < ents.length; i++) {
      // Diagnostic title on every row: the game's storage slot and the RAW target. Two rows
      // claiming the same slot, or a target that is pure colour-tag, are latch bugs that are
      // invisible once the tags are stripped - the tooltip is how they get reported.
      const diag = 'slot ' + (ents[i].slot | 0) + ' · raw "' + (ents[i].target || '') + '"';
      if (!mnuMovable(ents[i], ents)) {
        h += '<div class="pet-row" style="opacity:.45" title="' + htmlEsc(diag) + '">'
           + '<div style="flex:0 0 24px;font-variant-numeric:tabular-nums">' + (i + 1) + '</div>'
           + '<div style="flex:1">' + htmlEsc(ents[i].verb || '') + '</div>'
           + '<div style="font-size:11px">' + (function () {
               const tg = mnuPlain(ents[i].target), tl = mnuTargets(ents);
               return (tg && !mnuVarKeys(ents, tg).length)
                 ? 'no id for ' + htmlEsc(tg) + ' - cannot be reordered'
                 : 'fixed by the game';
             })() + '</div>'
           + '</div>';
        continue;
      }
      if (mi >= order.length) continue;
      const key = order[mi++];
      const p = mnuSplit(key);
      h += '<div class="pet-row" title="' + htmlEsc(diag) + '">'
         + '<div style="flex:0 0 24px;opacity:.4;font-variant-numeric:tabular-nums">' + (i + 1) + '</div>'
         + '<div style="flex:1"><b>' + htmlEsc(p.verb || '(none)') + '</b>'
         + (p.target ? '<span style="opacity:.45"> &middot; ' + htmlEsc(p.target) + '</span>' : '')
         + '</div>'
         + '<button class="pet-chip" data-up="' + htmlEsc(key) + '"'
         + (mi === 1 ? ' style="opacity:.2"' : '') + '>&#9650;</button>'
         + '<button class="pet-chip" data-down="' + htmlEsc(key) + '"'
         + (mi === order.length ? ' style="opacity:.2"' : '') + '>&#9660;</button>'
         + '</div>';
    }
    list.innerHTML = h;
  }
