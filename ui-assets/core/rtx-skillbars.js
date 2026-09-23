// rtx-skillbars.js: In-game Skills XP bars (interface 1466 cells), SKILL_SPRITES/SKILL_LAYOUT, combatLevel, sprite icon loader (setSpriteIcon, attachSkillIcon). Loads after rtx-registry.js (XP tables).
  // Bar along the bottom of each cell of the game's Skills panel (iface 1466, comp 2 subs 0..28, row-major = SKILL_LAYOUT order). Gated on varc 3165 == 1 (panel open); positioned from varcs 3166/3167.
  const SK_GROUP = 1466, SK_OPEN_VARC = 3165, SK_CELL_COMP = 2;
  // Panel chrome (title bar + tab strip) is in frame group 1477, not 1466, so the position varcs point at the window's outer top-left. Chrome is derived from geometry: inset = (panelW - contentW) / 2, offset = panelH - contentH - inset (live: window 224x291, content 216x243 -> inset 4, top chrome 44). The tab strip comp id and visibility flag are unreliable across layouts.
  const SK_POS_VARC_X = 3166, SK_POS_VARC_Y = 3167;
  const SK_SIZE_VARC_W = 3162, SK_SIZE_VARC_H = 3163;
  let skContentDx = 0, skContentDy = 0, skChromeAt = 0, skChromeSeen = '';
  let skChromeOk = false;   // a varc-derived chrome measurement from SANE inputs exists
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
    if (!skBarsDrawn && !force) return;
    try { if (bridge() && bridge().skillBars) bridge().skillBars(myPid(), ''); } catch (e) {}
    skBarsDrawn = false;
  }
  function skBarColour(pct) {
    let f = pct / 1000; if (f < 0) f = 0; if (f > 1) f = 1;
    const r = f < 0.5 ? 235 : Math.round(235 - 190 * ((f - 0.5) / 0.5));
    const g = f < 0.5 ? Math.round(70 + 165 * (f / 0.5)) : 235;
    return (r << 16) | (g << 8) | 45;      // a little blue keeps it from going neon
  }
  // Progress to the next level in tenths of a percent (caps 120, or 150 for Invention); maxed reads full.
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
    let openVar = null, originX = null, originY = null, panelW = 0, panelH = 0;
    try {
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
    let ws = [], uiSc = 1, gj = null;
    try {
      gj = JSON.parse(bridge().interfaceGroup(myPid(), SK_GROUP) || '{}');
      ws = gj.widgets || [];
      if (typeof gj.ui === 'number' && gj.ui > 0.2 && gj.ui < 5) uiSc = gj.ui;
    } catch (e) {}
    skBarsDiag = { widgets: ws.length, openVar: openVar, skills: sk ? sk.length : 0,
                   px: originX, py: originY, pw: panelW,
                   comp2: ws.filter(w => w.t && w.t[1] === SK_CELL_COMP && w.t[2] >= 0).length,
                   visible: ws.filter(w => w.v === 1).length,
                   withAbs: ws.filter(w => w.a).length };
    skBarsPaintWhy();
    if (openVar === 0) { skBarsClear(); return; }        // tabbed away / closed
    if (!sk || !sk.length) { skBarsClear(); return; }
    const openTrusted = openVar !== null && openVar > 0;
    const ok = w => w && w.r && w.a && w.r[2] > 8 && w.r[3] > 8 && (openTrusted || w.v === 1);
    let cells = ws.filter(w => ok(w) && w.t && w.t[1] === SK_CELL_COMP && w.t[2] >= 0)
                  .sort((p, q) => p.t[2] - q.t[2]);
    let bySub = true;
    if (cells.length < 20) {
      const bySize = {};
      for (const w of ws) { if (!ok(w)) continue; const k = w.r[2] + 'x' + w.r[3]; (bySize[k] = bySize[k] || []).push(w); }
      let best = null;
      for (const k in bySize) if (!best || bySize[k].length > best.length) best = bySize[k];
      if (!best || best.length < 20) { skBarsClear(); return; }
      cells = best.sort((p, q) => (p.a[1] - q.a[1]) || (p.a[0] - q.a[0]));
      bySub = false;
    }
    cells = cells.slice(0, SKILL_LAYOUT.length);
    const root = ws.find(w => w.d === 0 && w.r && w.r[2] > 0 && w.r[3] > 0);
    let anchor = null;
    if (root && root.a) {
      const cw = root.r[2], ch = root.r[3];
      let fr = [];
      try { fr = (JSON.parse(bridge().interfaceGroup(myPid(), 1477) || '{}').widgets) || []; } catch (e) {}
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
      const bx = anchor ? (anchor.x + (c.a[0] - anchor.rx)) : (c.a[0] + skContentDx);
      const by = anchor ? (anchor.y + (c.a[1] - anchor.ry)) : (c.a[1] + skContentDy);
      const s = anchor ? uiSc : 1;
      // the cell as the game's own component too (parent id, sub, rectangle inside the parent), so the
      // module can have the game draw the bar inside the panel; the launcher falls back to the overlay
      const cc = (bySub && c.r) ? (',' + ((SK_GROUP << 16) | SK_CELL_COMP) + ',' + k + ',' + Math.round(c.r[0]) + ',' + Math.round(c.r[1]) + ',' + Math.round(c.r[2]) + ',' + Math.round(c.r[3])) : '';
      segs.push(Math.round(bx * s) + ',' + Math.round(by * s) + ','
              + Math.round(c.r[2] * s) + ',' + Math.round(c.r[3] * s) + ','
              + pct + ',' + skBarColour(pct) + cc);
    }
    try { bridge().skillBars(myPid(), segs.join(';')); skBarsDrawn = segs.length > 0; } catch (e) {}
  }

  const SKILL_SPRITES = [16040,16045,16160,16041,16058,16057,16055,16043,
    16197,16051,16050,16049,16044,16061,16056,16052,16038,16196,16060,16048,
    16059,16053,16042,16195,16047,16046,16054,16039,30936];

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
  const SPRITE_QUEUE = [];
  let spriteDraining = false;
  const rafSchedule = (typeof requestAnimationFrame === 'function') ? requestAnimationFrame : (cb => setTimeout(cb, 16));
  const nowMs = () => ((typeof performance !== 'undefined' && performance.now) ? performance.now() : Date.now());
  function setSpriteIcon(el, url) {
    if (!url || !el) return;
    el.style.backgroundImage = 'url("' + url + '")';
    el.style.backgroundSize = 'contain';
    el.style.backgroundRepeat = 'no-repeat';
    el.style.backgroundPosition = 'center';
  }
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

