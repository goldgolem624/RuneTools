// rtx-skillbars.js: In-game Skills XP bars (interface 1466 cells), SKILL_SPRITES/SKILL_LAYOUT, combatLevel, sprite icon loader (setSpriteIcon, attachSkillIcon).
// Loads after: rtx-registry.js (XP tables).
// Plain script, page globals by design: every top-level name here is a page global that panels and the other core files use.
  // ---- in-game Skills XP bars (RuneLite-style) --------------------------------
  // A thin progress bar along the bottom of every cell of the game's own Skills panel
  // (interface 1466, comp 2 subs 0..28). Sub order is row-major, which is exactly the
  // order SKILL_LAYOUT is written in, so cell k is SKILL_LAYOUT[k] with no inference.
  //
  // Gated on varc 3165 == 1 (panel open) so a stale rect can never paint bars over a
  // closed panel; positioned from varcs 3166/3167 via the group's panel-origin entry.
  const SK_GROUP = 1466, SK_OPEN_VARC = 3165, SK_CELL_COMP = 2;
  // The panel CHROME is not part of 1466: the brown title bar and the tab strip are
  // layers in the game frame (1477), so the position varcs point at the window's outer
  // top-left and everything inside 1466 lands that much too high. Rather than a constant,
  // find the tab layer by the skills-tab sprite and take its BOTTOM as the content top --
  // that measures whatever chrome is actually present, which matters because the brown
  // bar is hidden while the UI is locked (with "hide title bars when locked",
  // varbit 19928). Frame widgets are positioned from screen 0,0, so accumulating their
  // relative rects down the tree gives absolute pixels directly.
  // CHROME FROM GEOMETRY, not from hunting the tab strip. Live capture of the running
  // client settled it: the window record reads 224x291 while group 1466's root -- the
  // content -- is 216x243. The content is inset 4px each side, and the 48px height
  // difference is the chrome: 44 above (title bar + tab strip) and a 4px bottom border
  // matching the sides. So:
  //     inset  = (panelW - contentW) / 2
  //     offset = panelH - contentH - inset
  // That is self-correcting for every state we chased separately before -- tabbed,
  // untabbed, and title-bar-hidden-while-locked all change the panel/content height
  // difference, and the formula follows. It also needs no sprite, no component id and no
  // visibility flag, each of which turned out to be unreliable: the tab strip is comp 109
  // in one layout and 306 in another, its sprite appears twice, and it reports vis=0 even
  // while the tabs are on screen.
  // The panel's own window record (it decodes as w,h,?,x,y -- see the Leagues research
  // notes on script8701). All four are needed: the position seeds the walk, the size gives
  // the chrome.
  const SK_POS_VARC_X = 3166, SK_POS_VARC_Y = 3167;
  const SK_SIZE_VARC_W = 3162, SK_SIZE_VARC_H = 3163;
  let skContentDx = 0, skContentDy = 0, skChromeAt = 0, skChromeSeen = '';
  let skChromeOk = false;   // a varc-derived chrome measurement from SANE inputs exists
  // Returns {dx,dy}, or null if either rect was unreadable -- in which case the caller
  // keeps its last good value rather than snapping the bars to a wrong place.
  // panelW/panelH from the window record, contentW/contentH from group 1466's root node.
  // The side border is the same 4px top-to-bottom, so the width gap gives it directly and
  // the leftover height is the top chrome. Two live captures at different panel sizes
  // (224x291/216x243 and 206x291/198x243) both yield inset 4 and dy 44, and 44 is also what
  // measuring the tab strip's bottom edge gave for the first of them.
  function skMeasureChrome(panelW, panelH, contentW, contentH) {
    if (!(panelW > 0 && panelH > 0 && contentW > 0 && contentH > 0)) return null;
    if (contentW > panelW || contentH > panelH) return null;      // torn read
    const inset = Math.max(0, Math.round((panelW - contentW) / 2));
    skChromeSeen = panelW + 'x' + panelH + ' vs content ' + contentW + 'x' + contentH;
    return { dx: inset, dy: panelH - contentH - inset };
  }
  let skBarsOn = false, skBarsDrawn = false, skBarsAt = 0, skBarsLoaded = false;
  let skBarsDiag = null;                       // last tick's findings (see skBarsWhy)
  function skBarsWhy() { return JSON.stringify(skBarsDiag); }
  // One sentence naming the stage that failed, so "nothing is drawn" is never silent.
  function skBarsWhyText() {
    const d = skBarsDiag;
    if (!skBarsOn || !d) return 'Progress to the next level, under each skill in the game\'s own panel';
    const tail = '  [open var ' + (d.openVar === null ? '?' : d.openVar) + ']';
    if (d.openVar === 0) return 'Skills panel not on screen - bars hidden' + tail;
    if (!d.skills)       return 'Waiting for skill data' + tail;
    if (!d.widgets)      return 'Skills interface (group ' + SK_GROUP + ') not open / not found' + tail;
    if (!d.comp2)        return d.widgets + ' widgets, but none are comp ' + SK_CELL_COMP + ' cells' + tail;
    if (d.visible === 0 && !(d.openVar !== null && d.openVar > 0))
      return 'Skills panel tabbed away (0 of ' + d.comp2 + ' cells visible) - bars hidden' + tail;
    if (!d.withAbs)      return d.comp2 + ' cells found, but no screen position (panel origin unresolved)' + tail;
    return d.comp2 + ' cells · ' + d.visible + ' vis · chrome ' + skContentDy + 'px · '
         + (d.anchor ? ('frame ' + d.anchor) : 'no 1477 frame anchor')
         + (d.ui ? (' · ui ' + d.ui.sc + ' (pw ' + d.ui.pw + ' root ' + d.ui.rootW
                    + ' rd ' + d.ui.rd + ' ' + d.ui.vw + '/' + d.ui.gw + ')') : '')
         + (skChromeSeen ? (' · ' + skChromeSeen) : '') + tail;
  }
  function skBarsPaintWhy() {
    const el = document.querySelector('#skBarsTgl .ov-sub');
    if (el) el.textContent = skBarsWhyText();
  }
  function skBarsLoad() {
    if (skBarsLoaded) return; skBarsLoaded = true;
    try { skBarsOn = localStorage.getItem('rtxSkillBars') === '1'; } catch (e) {}
  }
  function skBarsSet(on) {
    skBarsOn = !!on;
    try { localStorage.setItem('rtxSkillBars', skBarsOn ? '1' : '0'); } catch (e) {}
    if (!skBarsOn) skBarsClear();
    else { skBarsAt = 0; skBarsTick(); }
  }
  let skBarsBootCleared = false;   // one unconditional wipe per page life (see below)
  function skBarsClear(force) {
    // `force` clears even when THIS page never drew: the overlay keeps the last
    // published commands, so after a UI reload (dev hot-reload, panel restart) a
    // previous page's bars would otherwise survive with the toggle off forever.
    if (!skBarsDrawn && !force) return;
    try { if (bridge() && bridge().skillBars) bridge().skillBars(myPid(), ''); } catch (e) {}
    skBarsDrawn = false;
  }
  // Red -> yellow -> green across the level. Ramped through yellow at the halfway
  // point rather than blending red to green directly, which passes through a muddy
  // brown and reads as "broken" rather than "half way".
  function skBarColour(pct) {
    let f = pct / 1000; if (f < 0) f = 0; if (f > 1) f = 1;
    const r = f < 0.5 ? 235 : Math.round(235 - 190 * ((f - 0.5) / 0.5));
    const g = f < 0.5 ? Math.round(70 + 165 * (f / 0.5)) : 235;
    return (r << 16) | (g << 8) | 45;      // a little blue keeps it from going neon
  }
  // Progress to the NEXT level as tenths of a percent. Uses the same caps the grid
  // uses (120, or 150 for Invention's elite curve); a maxed skill reads full.
  function skBarPct(xp, elite) {
    if (!(xp >= 0)) return -1;
    const t = xpTable(elite), cap = elite ? 150 : 120;
    let lv = levelFromXp(xp, elite);
    if (lv >= cap || lv + 1 >= t.length) return 1000;
    const a = t[lv], b = t[lv + 1];
    if (!(b > a)) return 1000;
    let f = (xp - a) / (b - a);
    if (f < 0) f = 0; if (f > 1) f = 1;
    return Math.round(f * 1000);
  }
  async function skBarsTick() {
    skBarsLoad();
    if (!bridge() || !bridge().skillBars || !bridge().interfaceGroup) return;
    if (!skBarsOn) {
      skBarsClear(!skBarsBootCleared);   // first off-tick wipes unconditionally
      skBarsBootCleared = true;
      return;
    }
    skBarsBootCleared = true;   // drawing path owns the channel from here on
    const now = Date.now(); if (now - skBarsAt < 400) return; skBarsAt = now;
    // Open state is the REAL gate. Group 1466's widgets stay in the tree when the panel
    // is tabbed away - they are hidden, not removed - so "has widgets" is not a test for
    // visible, and relying on it drew bars over whatever tab replaced Skills. An
    // unreadable var still does NOT disable the feature: only an explicit 0 closes us.
    // SANITY-GATED: on some game builds these varcs hold packed or garbage 64-bit values
    // (seen live: 19-digit numbers where w/h should be), so a value only counts when it
    // is a plausible small int. Anything else reads as "unknown", never as pixels.
    let openVar = null, originX = null, originY = null, panelW = 0, panelH = 0;
    try {
      // varcInts, NOT varcLongs: these are int-typed varcs, and the 64-bit read widens
      // adjacent union bytes into the value (seen live: 3165's clean 0/1 arriving as
      // 19-digit garbage, which blinded the open gate). Fall back to the longs read
      // only while running against an older host without varcInts.
      const rd = bridge().varcInts || bridge().varcLongs;
      const d = JSON.parse(await rd(myPid(),
                  SK_OPEN_VARC + ',' + SK_POS_VARC_X + ',' + SK_POS_VARC_Y
                  + ',' + SK_SIZE_VARC_W + ',' + SK_SIZE_VARC_H) || '{}');
      const sane = v => (typeof v === 'number' && isFinite(v) && Number.isInteger(v) && v >= 0 && v < 16384);
      const ov = Number(d[String(SK_OPEN_VARC)]);
      if (sane(ov) && ov <= 8) openVar = ov;
      const rx = Number(d[String(SK_POS_VARC_X)]);  if (sane(rx)) originX = rx;
      const ry = Number(d[String(SK_POS_VARC_Y)]);  if (sane(ry)) originY = ry;
      const rw = Number(d[String(SK_SIZE_VARC_W)]); if (sane(rw)) panelW = rw;
      const rh = Number(d[String(SK_SIZE_VARC_H)]); if (sane(rh)) panelH = rh;
    } catch (e) {}
    const sk = (lastSnap && Array.isArray(lastSnap.skills)) ? lastSnap.skills : null;
    // uiSc = interface->pixel scale (the game's Interface Scaling setting, e.g. 1.35 at
    // 135%). Widget-tree coords are in the UNSCALED interface space; the overlay draws in
    // pixels, so the anchor-path output below multiplies through by this. Two sources:
    //  - PRIMARY (set in the anchor block): companion client width / 1477 root frame
    //    width. Both span the full client, one in the overlay's pixel space and one in
    //    tree space, so their ratio IS the tree->pixel factor by construction.
    //  - FALLBACK: the reader's gameview-varc / gameview-tree ratio ("ui"). Seen live
    //    reading 2/3 after a runtime Windows-DPI change while bars were anchored fine,
    //    so it only fills in when the 1477 root is unavailable.
    let ws = [], uiSc = 1, gj = null;
    try {
      gj = JSON.parse(bridge().interfaceGroup(myPid(), SK_GROUP) || '{}');
      ws = gj.widgets || [];
      if (typeof gj.ui === 'number' && gj.ui > 0.2 && gj.ui < 5) uiSc = gj.ui;
    } catch (e) {}
    // Diagnostics are recorded BEFORE any bail-out, and painted live onto the toggle
    // row -- the skills pane is signature-deduped, so leaving this to the next pane
    // render meant the row kept showing its default text and explained nothing.
    skBarsDiag = { widgets: ws.length, openVar: openVar, skills: sk ? sk.length : 0,
                   px: originX, py: originY, pw: panelW,
                   comp2: ws.filter(w => w.t && w.t[1] === SK_CELL_COMP && w.t[2] >= 0).length,
                   visible: ws.filter(w => w.v === 1).length,
                   withAbs: ws.filter(w => w.a).length };
    skBarsPaintWhy();
    if (openVar === 0) { skBarsClear(); return; }        // tabbed away / closed
    if (!sk || !sk.length) { skBarsClear(); return; }
    // The skill cells are comp 2's SUBS (owner-captured: 2:0..2:28, 60x27, three per
    // row at x 7/79/151). Sub order is already row-major, which is the order
    // SKILL_LAYOUT is written in, so no sorting by position and no guessing.
    // Only widgets the reader resolved to an absolute position can be drawn on.
    // VISIBILITY IS PART OF THE GATE. When the Skills panel is tabbed away, its 1466
    // widgets stay in the tree (hidden, not removed) and the frame anchor still finds
    // the interface slot, which is now hosting whatever tab replaced Skills; on builds
    // where the open varc reads as garbage (openVar unknown) that drew bars over the
    // backpack. The reader flags each rendered widget with v:1, so require it on every
    // cell: zero visible cells reads as "tabbed away", and if the flag ever breaks on
    // a future build the failure is hidden bars, never bars over the wrong panel.
    // VISIBILITY: per-cell v:1 exists to stop bars drawing over whatever tab replaced Skills
    // on builds where the open varc reads as garbage. But the v flag itself is layout-dependent:
    // in classic fullscreen layouts the +0x50 render bytes read differently and every cell
    // reports not-visible even with the panel plainly open (reported live: "0 of 29 cells
    // visible [open var 1]"). So the varc is the primary gate whenever it is READABLE: a sane
    // nonzero open varc means the panel is open and cells are accepted without the flag; the
    // per-cell flag only becomes a requirement when the varc is unreadable. openVar === 0
    // (tabbed away) already cleared above in both worlds.
    const openTrusted = openVar !== null && openVar > 0;
    const ok = w => w && w.r && w.a && w.r[2] > 8 && w.r[3] > 8 && (openTrusted || w.v === 1);
    let cells = ws.filter(w => ok(w) && w.t && w.t[1] === SK_CELL_COMP && w.t[2] >= 0)
                  .sort((p, q) => p.t[2] - q.t[2]);
    // Primary path: each cell CARRIES its skill slot (the sub index). Pairing cells[k]
    // with SKILL_LAYOUT[k] positionally instead broke as soon as ONE mid-list cell was
    // filtered out (no resolved rect on that tick): every bar after it shifted onto the
    // wrong skill. The fallback path has no sub index, so only it stays positional.
    let bySub = true;
    if (cells.length < 20) {
      // Fallback if the interface is ever restructured: the one widget size that
      // repeats ~29 times is the cell grid. Ordered by position, not by sub.
      const bySize = {};
      for (const w of ws) { if (!ok(w)) continue; const k = w.r[2] + 'x' + w.r[3]; (bySize[k] = bySize[k] || []).push(w); }
      let best = null;
      for (const k in bySize) if (!best || bySize[k].length > best.length) best = bySize[k];
      if (!best || best.length < 20) { skBarsClear(); return; }
      cells = best.sort((p, q) => (p.a[1] - q.a[1]) || (p.a[0] - q.a[0]));
      bySub = false;
    }
    cells = cells.slice(0, SKILL_LAYOUT.length);
    // TRUE ORIGIN + CHROME FROM THE LIVE 1477 FRAME, mirroring the C++ inventory-slot
    // solution (Reader.cpp InvSlotRectJson). The window-record varcs proved unreliable
    // across game builds, so instead: group 1477's walk seeds at 0,0 (true screen px),
    // and the frame window that OWNS this grid is the 1477 widget slightly larger than
    // 1466's content root, nearest the varc origin hint. border = (frameW - contentW)/2,
    // header = the vertical rest. Cells position as frame + (cell - contentRoot), so the
    // 1466 seed cancels out and even a stale or garbage origin varc cannot displace bars.
    const root = ws.find(w => w.d === 0 && w.r && w.r[2] > 0 && w.r[3] > 0);
    let anchor = null;
    if (root && root.a) {
      const cw = root.r[2], ch = root.r[3];
      let fr = [];
      try { fr = (JSON.parse(bridge().interfaceGroup(myPid(), 1477) || '{}').widgets) || []; } catch (e) {}
      // PRIMARY interface->pixel scale: companion client width (the overlay's pixel
      // space) over the 1477 ROOT frame width (tree space). 1477 hosts the whole HUD at
      // screen 0,0, so its widest depth-0 frame spans the full client and the ratio is
      // exactly the factor the bar coords below need. Overrides the reader's varc-based
      // guess whenever both measures resolve sanely.
      let rootW = 0;
      for (const f of fr)
        if (f && f.d === 0 && f.r && f.r[2] > rootW) rootW = f.r[2];
      let pw = 0;
      try {
        const ci = JSON.parse(bridge().uiClientInfo(myPid()) || '{}');
        if (typeof ci.pw === 'number' && ci.pw > 0) pw = ci.pw;
      } catch (e) {}
      if (pw > 0 && rootW > 200) {
        const r = pw / rootW;
        if (r > 0.2 && r < 5) uiSc = r;
      }
      skBarsDiag.ui = { sc: Math.round(uiSc * 1000) / 1000, pw: pw, rootW: rootW,
                        rd: gj ? gj.ui : null, vw: gj ? gj.uiw : null, gw: gj ? gj.uig : null };
      // 1477's walk seeds at screen 0,0, but the group has no panel-origin spec, so its
      // widgets can arrive WITHOUT `a` (seen live: "no 1477 frame anchor" with bars
      // hidden). The JSON is a strict depth-first walk with parent-relative rects, so
      // absolutes reconstruct from a depth stack: abs = parent's abs + own r offset.
      const stk = [];
      for (const f of fr) {
        if (!f || !f.r) continue;
        const par = (f.d > 0 && stk[f.d - 1]) ? stk[f.d - 1] : [0, 0];
        const fax = par[0] + f.r[0], fay = par[1] + f.r[1];
        stk[f.d] = f.a ? [f.a[0], f.a[1]] : [fax, fay];
        if (!f.a) f.a = [fax, fay];
      }
      let best = null, bestD = 1e9, sizeMatches = 0;
      for (const f of fr) {
        if (!f || !f.a || !f.r) continue;
        const fw = f.r[2], fh = f.r[3];
        if (!(fw > cw && fw < cw + 120 && fh > ch && fh < ch + 300)) continue;
        sizeMatches++;
        const dist = Math.abs(f.a[0] - root.a[0]) + Math.abs(f.a[1] - root.a[1]);
        if (dist > bestD) continue;
        if (dist === bestD && best && fw * fh >= best.r[2] * best.r[3]) continue;
        bestD = dist; best = f;
      }
      // The distance gate disambiguates when SEVERAL frames match the size; a unique
      // match is trusted at any distance (a stale origin hint can drift far past 200px,
      // e.g. a docked panel whose drag varc kept its old spot).
      if (best && bestD > 200 && sizeMatches > 1) best = null;
      if (best) {
        const bd = Math.round((best.r[2] - cw) / 2);
        const hd = best.r[3] - ch - bd;
        if (hd >= 0 && hd < 400) {
          anchor = { x: best.a[0] + bd, y: best.a[1] + hd, rx: root.a[0], ry: root.a[1] };
          skContentDx = bd; skContentDy = hd;   // reported by the diagnostic sub-line
        }
      }
    }
    skBarsDiag.anchor = anchor ? (anchor.x + ',' + anchor.y) : null;
    skBarsPaintWhy();
    // Fallback: varc-measured chrome, accepted only from SANE inputs. With garbage varcs
    // (openVar unknown) the live frame is also the presence gate: no frame, no bars --
    // strictly better than drawing them displaced or over another tab.
    if (!anchor) {
      if (now - skChromeAt > 1500) {
        skChromeAt = now;
        const c = root ? skMeasureChrome(panelW, panelH, root.r[2], root.r[3]) : null;
        if (c && c.dy >= 0 && c.dy < 400) { skContentDx = c.dx; skContentDy = c.dy; skChromeOk = true; }
      }
      if (!skChromeOk || openVar === null) { skBarsClear(); return; }
    }
    const segs = [];
    for (let n = 0; n < cells.length; n++) {
      const c = cells[n];
      const k = bySub ? c.t[2] : n;               // skill slot: the cell's own sub index when known
      if (k >= SKILL_LAYOUT.length) continue;
      const i = SKILL_LAYOUT[k], t = sk[i];
      if (!t) continue;
      const pct = skBarPct(t[2] === undefined ? -1 : t[2], i === 26);
      if (pct < 0) continue;
      // Anchor path: every term is a pure widget-TREE coordinate (the 1477 frame walk
      // seeds at 0,0 and the 1466 varc origin cancels in c.a - anchor.rx), so the whole
      // rect converts to pixels by uiSc. The varc-chrome fallback mixes a pixel origin
      // with tree offsets and cannot be converted exactly -> left unscaled, as before.
      const bx = anchor ? (anchor.x + (c.a[0] - anchor.rx)) : (c.a[0] + skContentDx);
      const by = anchor ? (anchor.y + (c.a[1] - anchor.ry)) : (c.a[1] + skContentDy);
      const s = anchor ? uiSc : 1;
      segs.push(Math.round(bx * s) + ',' + Math.round(by * s) + ','
              + Math.round(c.r[2] * s) + ',' + Math.round(c.r[3] * s) + ','
              + pct + ',' + skBarColour(pct));
    }
    try { bridge().skillBars(myPid(), segs.join(';')); skBarsDrawn = segs.length > 0; } catch (e) {}
  }

  // Skill -> cache sprite id (from the game's SkillData table).
  const SKILL_SPRITES = [16040,16045,16160,16041,16058,16057,16055,16043,
    16197,16051,16050,16049,16044,16061,16056,16052,16038,16196,16060,16048,
    16059,16053,16042,16195,16047,16046,16054,16039,30936];

  // In-game skills-tab order (3 per row), as reader indices.
  const SKILL_LAYOUT = [
     0, 3,14,   // Attack       Hitpoints   Mining
     2,16,13,   // Strength     Agility     Smithing
     1,15,10,   // Defence      Herblore    Fishing
     4,17, 7,   // Ranged       Thieving    Cooking
     5,12,11,   // Prayer       Crafting    Firemaking
     6, 9, 8,   // Magic        Fletching   Woodcutting
    20,18,19,   // Runecrafting Slayer      Farming
    22,21,23,   // Construction Hunter      Summoning
    24,25,26,   // Dungeoneering Divination Invention
    27,28];     // Archaeology  Necromancy

  // RS3 combat level (post-Necromancy). Necromancy counts as its own style.
  function combatLevel(sk) {
    const L = i => (sk[i] ? sk[i][0] : 0);
    const att=L(0), str=L(2), rng=L(4), mag=L(6), nec=L(28);
    const def=L(1), hp=L(3), pray=L(5), summ=L(23);
    const styles = Math.max(att + str, 2*rng, 2*mag, 2*nec);
    const base = def + hp + Math.floor(pray/2) + Math.floor(summ/2);
    return Math.floor((1.3 * styles + base) / 4);
  }

  const SPRITES = new Map();
  const SPRITE_PENDING = new Set();
  // Frame-budgeted sprite loader (~8ms/frame): long icon lists render instantly and
  // icons fill in progressively instead of stalling on N synchronous decodes.
  const SPRITE_QUEUE = [];
  let spriteDraining = false;
  const rafSchedule = (typeof requestAnimationFrame === 'function') ? requestAnimationFrame : (cb => setTimeout(cb, 16));
  const nowMs = () => ((typeof performance !== 'undefined' && performance.now) ? performance.now() : Date.now());
  // Background-image, NOT <img>: oversized cache sprites hit Ultralight's
  // replaced-element intrinsic-size bug and render blank in a small box.
  // No Node.isConnected guard: Ultralight doesn't implement it (undefined -> falsy
  // would skip every paint); styling a detached element is harmless.
  function setSpriteIcon(el, url) {
    if (!url || !el) return;
    el.style.backgroundImage = 'url("' + url + '")';
    el.style.backgroundSize = 'contain';
    el.style.backgroundRepeat = 'no-repeat';
    el.style.backgroundPosition = 'center';
  }
  // Optional `px` asks the C++ for an area-average downscale capped to px on the longest
  // side (pass ~2x the CSS box for a clean 2:1 in-browser rescale). Cached per (id,px);
  // an older bridge without the arg just serves the default-cap sprite.
  function loadSpriteIcon(el, id, px) {
    if (!id || id <= 0 || !el) return;
    const k = px ? (id + '|' + px) : id;
    if (SPRITES.has(k)) { setSpriteIcon(el, SPRITES.get(k)); return; }
    SPRITE_QUEUE.push([id, el, px || 0]);
    if (!spriteDraining) { spriteDraining = true; rafSchedule(drainSprites); }
  }
  function drainSprites() {
    const t0 = nowMs();
    while (SPRITE_QUEUE.length && nowMs() - t0 < 8) {
      const it = SPRITE_QUEUE.shift(), id = it[0], el = it[1], px = it[2] || 0;
      const k = px ? (id + '|' + px) : id;
      if (SPRITES.has(k)) { setSpriteIcon(el, SPRITES.get(k)); continue; }
      if (SPRITE_PENDING.has(k)) continue;
      let r = '';
      try { r = (bridge() && bridge().sprite) ? (px ? bridge().sprite(id, px) : bridge().sprite(id)) : ''; } catch (e) { r = ''; }
      if (r && typeof r.then === 'function') {     // async bridge: resolve out of band
        SPRITE_PENDING.add(k);
        r.then(u => { const url = (typeof u === 'string') ? u : ''; SPRITE_PENDING.delete(k); SPRITES.set(k, url); setSpriteIcon(el, url); },
               () => { SPRITE_PENDING.delete(k); SPRITES.set(k, ''); });
      } else {                                     // sync bridge: cache + paint now
        const url = (typeof r === 'string') ? r : '';
        SPRITES.set(k, url); setSpriteIcon(el, url);
      }
    }
    if (SPRITE_QUEUE.length) rafSchedule(drainSprites); else spriteDraining = false;
  }
  function attachSkillIcon(el, skillIndex) {
    const sid = SKILL_SPRITES[skillIndex];
    if (!sid) return;
    const cached = SPRITES.get(sid);
    if (cached) { el.innerHTML = '<img src="' + cached + '">'; return; }
    if (!bridge() || !bridge().sprite || SPRITE_PENDING.has(sid)) return;
    SPRITE_PENDING.add(sid);
    (async () => {
      try {
        const url = await bridge().sprite(sid);
        SPRITE_PENDING.delete(sid);
        if (url) {
          SPRITES.set(sid, url);
          document.querySelectorAll('.sk-icon[data-skill="' + skillIndex + '"]')
            .forEach(n => { n.innerHTML = '<img src="' + url + '">'; });
        }
      } catch (e) { SPRITE_PENDING.delete(sid); }
    })();
  }

