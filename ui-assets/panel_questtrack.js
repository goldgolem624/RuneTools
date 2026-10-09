// RuneToolsX panel: quest step tracking from data (quest_tracks.js). Ticks guide steps and draws marks for
// quests that have no step function of their own. Loaded on the first guide tick through QG_LATE.
(function () {

  // Quests with a step function or checklist of their own are never driven from data.
  const QT_HAND = ['Visions of Havenhythe', 'Hearts of Sanguine', 'Hermit Permits', 'Secrets of Amberfell', 'Wiz Kid',
    'Necromancy!', 'The Restless Ghost', 'Making History', 'New Foundations', "There's No Place Like Home...",
    'Murder on the Border', 'Heralds of Crimson', 'Death Plateau'];
  const QT_INST_X = 6400;                                  // instanced copies sit at x >= 6400
  const QT_CHAT = [1184, 1191, 1186, 1187, 1189];          // npc chat, player chat, message box, pair, item box
  const QT_WHERE = { inv: ['inv'], worn: ['worn'], bank: ['bank'], held: ['inv', 'worn'], any: ['inv', 'worn', 'bank'] };
  const QT_LATCH_PREF = 'rtxQtLatch', QT_OBSERVE_PREF = 'rtxQtObserve';
  const QT_RETRY_MS = 15000;                               // a failed data load is tried again after this

  const qtHas = (o, k) => !!o && typeof o === 'object' && Object.prototype.hasOwnProperty.call(o, k);
  function qtInt(x) { const n = Number(x); return isFinite(n) ? Math.trunc(n) : 0; }
  function qtIds(v) { return v == null ? [] : (Array.isArray(v) ? v : [v]); }
  function qtNorm(s) { return String(s == null ? '' : s).toLowerCase().replace(/[^a-z0-9 ]+/g, ' ').replace(/\s+/g, ' ').trim(); }
  function qtPlane(e) { return qtHas(e, 'p') ? e.p : 0; }
  function qtPid() { try { return myPid(); } catch (e) { return 0; } }
  function qtJson(s, d) { try { const v = (typeof s === 'string') ? JSON.parse(s) : s; return (v == null) ? d : v; } catch (e) { return d; } }
  // game text without its markup: <br> is a space, other tags go
  function qtText(s) { return String(s == null ? '' : s).replace(/<br\s*\/?>/gi, ' ').replace(/<[^>]*>/g, '').replace(/\s+/g, ' ').trim(); }

  // ---- data
  let qtData = null, qtLoadState = 0, qtLoadP = null, qtLoadFailAt = 0;   // 0 untried, 1 loading, 2 done
  function qtTracks() {
    const w = (typeof window !== 'undefined') ? window.QUEST_TRACKS : null;
    return (w && typeof w === 'object' && !Array.isArray(w)) ? w : qtData;
  }
  function qtLoad() {
    if (qtLoadState === 2 && (qtTracks() || Date.now() - qtLoadFailAt < QT_RETRY_MS)) return Promise.resolve(!!qtTracks());
    if (qtLoadState === 1 && qtLoadP) return qtLoadP;
    qtLoadState = 1;
    qtLoadP = (async () => {
      let data = null;
      try {
        const w = window.QUEST_TRACKS;
        if (w && typeof w === 'object' && !Array.isArray(w)) data = w;
        else if (bridge() && bridge().uiAsset) {
          const txt = String((await rtxData.raw('host.uiAsset', 'quest_tracks.js')) || '');
          const a = txt.indexOf('{'), b = txt.lastIndexOf('}');
          if (a >= 0 && b > a) data = JSON.parse(txt.slice(a, b + 1));
        }
      } catch (e) { data = null; }
      if (data && typeof data === 'object' && !Array.isArray(data)) {
        qtData = data;
        if (!window.QUEST_TRACKS) window.QUEST_TRACKS = data;
      } else qtLoadFailAt = Date.now();
      qtLoadState = 2; qtLoadP = null;
      return !!qtTracks();
    })();
    return qtLoadP;
  }

  // ---- conditions: one kind per node, an unknown kind is false
  function qtRects(z) {
    const zs = (Array.isArray(z) && z.length && Array.isArray(z[0])) ? z : [z];
    const out = [];
    for (const r of zs) {
      if (!Array.isArray(r) || r.length < 4) continue;
      out.push([Math.min(r[0], r[2]), Math.min(r[1], r[3]), Math.max(r[0], r[2]), Math.max(r[1], r[3]), r.length > 4 ? r[4] : null]);
    }
    return out;
  }
  // absolute zones mean nothing inside an instance: only a zone that itself lies there can hold
  function qtInZone(z, x, y, p, playerInst) {
    for (const r of qtRects(z)) {
      if (playerInst && r[0] < QT_INST_X) continue;
      if (r[0] <= x && x <= r[2] && r[1] <= y && y <= r[3] && (r[4] == null || r[4] === p)) return true;
    }
    return false;
  }
  function qtSame(a, b) {
    if (!Array.isArray(b) || a.length !== b.length) return false;
    for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
    return true;
  }
  function qtCond(c, snap, latches) {
    if (!c || typeof c !== 'object' || Array.isArray(c)) return false;
    snap = snap || {}; latches = latches || {};
    if (qtHas(c, 'all')) return Array.isArray(c.all) && c.all.every(x => qtCond(x, snap, latches));
    if (qtHas(c, 'any')) return Array.isArray(c.any) && c.any.some(x => qtCond(x, snap, latches));
    if (qtHas(c, 'not')) return !qtCond(c.not, snap, latches);
    const P = snap.P, hasP = !!(P && typeof P === 'object' && Object.keys(P).length);
    const inst = hasP && P.x >= QT_INST_X;
    if (qtHas(c, 'v')) {
      const vars = snap.vars || {};
      const v = qtInt(qtHas(vars, c.v) ? vars[c.v] : 0);
      if (qtHas(c, 'eq') && !(v === c.eq)) return false;
      if (qtHas(c, 'ne') && !(v !== c.ne)) return false;
      if (qtHas(c, 'ge') && !(v >= c.ge)) return false;
      if (qtHas(c, 'gt') && !(v > c.gt)) return false;
      if (qtHas(c, 'le') && !(v <= c.le)) return false;
      if (qtHas(c, 'lt') && !(v < c.lt)) return false;
      return !qtHas(c, 'in') || (Array.isArray(c.in) && c.in.indexOf(v) >= 0);
    }
    if (qtHas(c, 'item')) {
      const srcs = QT_WHERE[qtHas(c, 'where') ? c.where : 'held'];
      if (!srcs) return false;
      let n = 0;
      for (const i of qtIds(c.item)) for (const s of srcs) n += qtInt((snap[s] || {})[String(i)] || 0);
      return n >= (qtHas(c, 'n') ? c.n : 1);
    }
    if (qtHas(c, 'npc') || qtHas(c, 'ground')) {
      const k = qtHas(c, 'npc') ? 'npc' : 'ground';
      const want = qtIds(c[k]), list = snap[k === 'npc' ? 'npcs' : 'ground'];
      for (const e of (Array.isArray(list) ? list : [])) {
        if (e && want.indexOf(e.id) >= 0 && (!qtHas(c, 'zone') || qtInZone(c.zone, e.x, e.y, qtPlane(e), inst))) return true;
      }
      return false;
    }
    if (qtHas(c, 'zone')) return hasP && qtInZone(c.zone, P.x, P.y, qtPlane(P), inst);
    if (qtHas(c, 'tile')) return hasP && qtSame([P.x, P.y, qtPlane(P)], c.tile);
    if (qtHas(c, 'inst')) return inst === !!c.inst;
    if (qtHas(c, 'loc')) {
      const want = qtIds(c.loc);
      for (const e of (Array.isArray(snap.locs) ? snap.locs : [])) {
        if (e && want.indexOf(e.id) >= 0 && (!qtHas(c, 'at') || inst || qtSame([e.x, e.y, qtPlane(e)], c.at))) return true;
      }
      return false;
    }
    if (qtHas(c, 'iface')) {
      const g = (snap.iface || {})[String(c.iface)];
      return typeof g === 'string' && (!qtHas(c, 'text') || g.toLowerCase().indexOf(String(c.text).toLowerCase()) >= 0);
    }
    if (qtHas(c, 'dlg')) {
      const t = (snap.dlg || {}).text;
      return String(t == null ? '' : t).toLowerCase().indexOf(String(c.dlg).toLowerCase()) >= 0;
    }
    if (qtHas(c, 'opt')) {
      const w = qtNorm(c.opt), opts = (snap.dlg || {}).opts;
      return !!w && (Array.isArray(opts) ? opts : []).some(o => qtNorm(o).indexOf(w) >= 0);
    }
    if (qtHas(c, 'skill')) {
      const s = (snap.skills || {})[String(c.skill)];
      const lvl = (Array.isArray(s) && s.length) ? qtInt(c.boost ? s[0] : s[1]) : 0;   // [current, base]
      return lvl >= (qtHas(c, 'ge') ? c.ge : 1);
    }
    if (qtHas(c, 'quest')) {
      const st = qtInt((snap.quests || {})[String(c.quest)] || 0);
      return qtHas(c, 'st') ? st === c.st : st >= (qtHas(c, 'ge') ? c.ge : 2);
    }
    if (qtHas(c, 'latch')) return qtHas(latches, c.latch) && !!latches[c.latch];
    if (qtHas(c, 'freeinv')) return qtInt(qtHas(snap, 'freeinv') ? snap.freeinv : 0) >= c.freeinv;
    if (qtHas(c, 'buff')) return (Array.isArray(snap.buffs) ? snap.buffs : []).indexOf(c.buff) >= 0;
    return false;   // unknown kind
  }
  function qtKeyCmp(a, b) {
    const x = String(a).split('.').map(qtInt), y = String(b).split('.').map(qtInt);
    for (let i = 0; i < Math.min(x.length, y.length); i++) if (x[i] !== y[i]) return x[i] - y[i];
    return x.length - y.length;
  }
  // One evaluation: latches update first (a set latch whose keep fails clears, an unset one sets when set and
  // keep both hold), then every tick on its own, then the first state whose condition holds.
  function qtEval(t, snap, latches) {
    const lat = Object.assign({}, latches || {});
    for (const lt of ((t && t.latches) || [])) {
      if (!lt || typeof lt.id !== 'string') continue;
      const on = qtHas(lat, lt.id) && !!lat[lt.id];
      const keep = qtCond(lt.keep, snap, lat);
      if (on && !keep) delete lat[lt.id];
      else if (!on && keep && qtCond(lt.set, snap, lat)) lat[lt.id] = true;
    }
    const ticks = [], seen = new Set();
    for (const tk of ((t && t.ticks) || [])) {
      if (!tk || seen.has(tk.step)) continue;
      if (qtCond(tk.when, snap, lat)) { seen.add(tk.step); ticks.push(tk.step); }
    }
    ticks.sort(qtKeyCmp);
    let state = null;
    for (const st of ((t && t.states) || [])) if (st && qtCond(st.when, snap, lat)) { state = st.id; break; }
    return { state: state, ticks: ticks, latches: lat };
  }

  // ---- what a track reads
  const qtNeedsCache = new WeakMap();
  function qtNeeds(t) {
    let n = qtNeedsCache.get(t);
    if (n) return n;
    n = { vb: [], vp: [], vc: [], inv: false, worn: false, bank: false, scene: false, ground: false, dlgText: false,
          opts: false, ifaces: [], skills: false, quests: [], freeinv: false, buffs: false, npcIds: [] };
    const add = (a, v) => { if (a.indexOf(v) < 0) a.push(v); };
    const vars = (t && t.vars) || {};
    for (const a in vars) {
      const d = vars[a] || {};
      if (d.vb != null) add(n.vb, d.vb | 0); else if (d.vp != null) add(n.vp, d.vp | 0); else if (d.vc != null) add(n.vc, d.vc | 0);
    }
    const walk = c => {
      if (!c || typeof c !== 'object') return;
      if (Array.isArray(c.all)) c.all.forEach(walk);
      if (Array.isArray(c.any)) c.any.forEach(walk);
      if (c.not) walk(c.not);
      if (qtHas(c, 'item')) for (const s of (QT_WHERE[qtHas(c, 'where') ? c.where : 'held'] || [])) n[s] = true;
      if (qtHas(c, 'npc') || qtHas(c, 'loc')) n.scene = true;
      if (qtHas(c, 'ground')) n.ground = true;
      if (qtHas(c, 'iface')) add(n.ifaces, c.iface | 0);
      if (qtHas(c, 'dlg')) n.dlgText = true;
      if (qtHas(c, 'opt')) n.opts = true;
      if (qtHas(c, 'skill')) n.skills = true;
      if (qtHas(c, 'quest')) add(n.quests, c.quest | 0);
      if (qtHas(c, 'freeinv')) { n.freeinv = true; n.inv = true; }
      if (qtHas(c, 'buff')) n.buffs = true;
    };
    for (const x of ((t && t.ticks) || [])) walk(x && x.when);
    for (const x of ((t && t.latches) || [])) { walk(x && x.set); walk(x && x.keep); }
    for (const s of ((t && t.states) || [])) {
      if (!s) continue;
      walk(s.when);
      for (const m of (Array.isArray(s.marks) ? s.marks : [])) {
        if (!m) continue;
        if (m.npc != null) { n.scene = true; for (const id of qtIds(m.npc)) add(n.npcIds, id); }
        if (m.loc != null) n.scene = true;
        if (m.ground != null) n.ground = true;
        if (m.item != null) n.inv = true;
      }
    }
    qtNeedsCache.set(t, n);
    return n;
  }

  // ---- one snapshot of game state, only what the track reads. _ok (not enumerable) is false when a read the
  // track depends on failed: the tick then changes nothing.
  function qtItemsOf(d) {
    const counts = {}, slots = {};
    for (const it of ((d && Array.isArray(d.items)) ? d.items : [])) {
      if (!Array.isArray(it) || !(it[1] >= 0)) continue;
      const k = String(it[1]);
      counts[k] = (counts[k] || 0) + (it[2] > 0 ? it[2] : 1);
      if (!qtHas(slots, k)) slots[k] = it[0];
    }
    return { counts, slots, n: (d && Array.isArray(d.items)) ? d.items.length : 0 };
  }
  async function qtGroupText(g) {
    const d = qtJson(await rtxData.raw('state.interfaceGroup', g), null);
    const ws = (d && Array.isArray(d.widgets)) ? d.widgets.filter(w => w && w.v === 1) : [];
    if (!ws.length) return null;
    return ws.map(w => qtText(w.x)).filter(Boolean).join('\n').toLowerCase();
  }
  async function qtReadOpts() {
    let d = null;
    try { d = qtJson(await bridge().dialog(qtPid()), null); } catch (e) {}
    const os = (d && Array.isArray(d.options)) ? d.options : [];
    return os.filter(o => o && String(o.text || '').toLowerCase().indexOf('<str') < 0).map(o => qtText(o.text)).filter(Boolean);
  }
  async function qtSnapshot(t) {
    const N = qtNeeds(t), snap = { vars: {} };
    let ok = true;
    const vars = (t && t.vars) || {};
    if (N.vb.length) {
      let r = null; try { r = await readVarbitValues(N.vb); } catch (e) {}
      if (!r || r._ok === false) ok = false;
      for (const a in vars) if (vars[a] && vars[a].vb != null) snap.vars[a] = r ? qtInt(r[vars[a].vb]) : 0;
    }
    if (N.vp.length) {
      const r = qtJson(await rtxData.raw('state.varps', N.vp.join(',')), null);
      if (!r || typeof r !== 'object' || N.vp.some(id => !qtHas(r, String(id)))) ok = false;
      for (const a in vars) if (vars[a] && vars[a].vp != null) snap.vars[a] = r ? qtInt(r[String(vars[a].vp)]) : 0;
    }
    if (N.vc.length) {
      let r = null; try { r = await PLUGIN_API['state.varcs'].run([N.vc], qtPid()); } catch (e) {}
      if (!r || typeof r !== 'object') ok = false;
      for (const a in vars) if (vars[a] && vars[a].vc != null) snap.vars[a] = r ? qtInt(r[vars[a].vc]) : 0;
    }
    // an absent container ("present": false) is not loaded yet: a failed read, not an empty one
    let slots = {}, invHere = null;
    const invLoaded = d => !!d && d.present !== false && Array.isArray(d.items);
    if (N.inv) {
      const d = qtJson(await rtxData.raw('state.inventory'), null);
      invHere = invLoaded(d);
      if (!invHere) ok = false;
      const r = qtItemsOf(d);
      snap.inv = r.counts; slots = r.slots; snap.freeinv = Math.max(0, 28 - r.n);
    }
    if (N.worn) {
      const d = qtJson(await rtxData.raw('state.equipment'), null);
      if (!d || !Array.isArray(d.items)) ok = false;
      else if (d.present === false) {   // nothing worn may never have been sent: only absent with the backpack fails
        if (invHere === null) invHere = invLoaded(qtJson(await rtxData.raw('state.inventory'), null));
        if (!invHere) ok = false;
      }
      snap.worn = qtItemsOf(d).counts;
    }
    if (N.bank) {   // the bank as last seen; null when never seen
      const d = qtJson(await rtxData.raw('state.bank'), null);
      snap.bank = (d && Array.isArray(d.items) && d.items.length) ? qtItemsOf(d).counts : null;
    }
    let P = (typeof qgP !== 'undefined' && qgP) ? qgP : null;
    if (!P) { try { P = await scanPlayerTile(); } catch (e) {} }
    snap.P = P ? { x: P.x | 0, y: P.y | 0, p: (P.p | 0) } : null;
    if (N.scene) {
      const d = qtJson(await rtxData.raw('state.scene', 64), null);
      if (!d || typeof d !== 'object') ok = false;
      const ent = e => ({ id: e.id, x: e.x, y: e.y, p: e.plane | 0 });
      snap.npcs = (d && Array.isArray(d.npcs)) ? d.npcs.filter(e => e && typeof e.id === 'number').map(ent) : [];
      snap.locs = (d && Array.isArray(d.objects)) ? d.objects.filter(e => e && typeof e.id === 'number').map(ent) : [];
    }
    if (N.ground) {
      const d = qtJson(await rtxData.raw('state.groundItems'), null);
      snap.ground = Array.isArray(d) ? d.filter(e => e && typeof e.id === 'number').map(e => ({ id: e.id, x: e.x, y: e.y, p: e.plane | 0 })) : [];
    }
    if (N.dlgText || N.opts) {
      snap.dlg = { text: '', opts: [] };
      if (N.dlgText) {
        const parts = [];
        for (const g of QT_CHAT) { const s = await qtGroupText(g); if (s) parts.push(s); }
        snap.dlg.text = parts.join('\n');
      }
      if (N.opts) snap.dlg.opts = await qtReadOpts();
    }
    if (N.ifaces.length) {
      snap.iface = {};
      for (const g of N.ifaces) snap.iface[String(g)] = await qtGroupText(g);
    }
    if (N.skills) {   // the client gives [base, current, xp]; conditions read [current, base]
      snap.skills = {};
      const sk = (typeof lastSnap !== 'undefined' && lastSnap && Array.isArray(lastSnap.skills)) ? lastSnap.skills : [];
      sk.forEach((s, i) => { if (Array.isArray(s)) snap.skills[String(i)] = [s[1] | 0, s[0] | 0]; });
    }
    if (N.quests.length) {
      snap.quests = {};
      const st = (typeof questsData !== 'undefined' && questsData && questsData.st) ? questsData.st : {};
      for (const q of N.quests) snap.quests[String(q)] = qtHas(st, q) ? (st[q] | 0) : 0;
    }
    if (N.buffs) {
      const d = qtJson(await rtxData.raw('state.buffs'), null);
      const all = [].concat((d && Array.isArray(d.buffs)) ? d.buffs : [], (d && Array.isArray(d.debuffs)) ? d.debuffs : []);
      snap.buffs = all.filter(b => b && typeof b.struct === 'number').map(b => b.struct);
    }
    Object.defineProperty(snap, '_ok', { value: ok, enumerable: false });
    Object.defineProperty(snap, '_slots', { value: slots, enumerable: false });
    return snap;
  }

  // ---- drawing. Each channel is written once per tick with what it should show, never cleared and redrawn.
  const qtOv = (cmd, ...a) => { try { return PLUGIN_API[cmd].run(a, qtPid()); } catch (e) {} };
  const qtClrNpc = () => qtOv('overlay.highlight', []);
  const qtClrTiles = () => qtOv('overlay.guideTiles', []);
  const qtClrDlg = () => qtOv('overlay.highlightRect', 0, 0, 0, 0);
  const qtClrItem = () => qtOv('overlay.highlightItem', 0, '');
  function qtClearAll() {
    if (typeof qgClearAll === 'function') { try { qgClearAll(); return; } catch (e) {} }
    qtClrNpc(); qtClrTiles(); qtClrDlg(); qtClrItem();
  }
  function qtNearest(list, at) {
    if (!at) return list[0];
    let best = list[0], bd = Infinity;
    for (const e of list) { const d = Math.max(Math.abs(e.x - at[0]), Math.abs(e.y - at[1])); if (d < bd) { bd = d; best = e; } }
    return best;
  }
  async function qtDraw(t, state, snap) {
    const st = (typeof state === 'string') ? (((t && t.states) || []).find(s => s && s.id === state) || null) : state;
    if (!st) { qtClearAll(); return; }
    snap = snap || {};
    const marks = (Array.isArray(st.marks) ? st.marks : []).filter(m => m && typeof m === 'object');
    const P = snap.P || null, inst = !!(P && P.x >= QT_INST_X);
    // 1. an open option list: the option to pick wins over every mark
    if (Array.isArray(st.opts) && st.opts.length) {
      let boxed = false;
      try { boxed = await PLUGIN_API['overlay.highlightOption'].run(st.opts, qtPid()); } catch (e) {}
      if (boxed) { qtClrNpc(); qtClrTiles(); qtClrItem(); return; }
    } else qtClrDlg();
    // 2. lodestone: while far from it the teleport is the whole step
    const lode = marks.find(m => typeof m.lode === 'string');
    if (lode) { if (typeof qgLodestone === 'function' && await qgLodestone(lode.lode)) return; }
    else if (typeof hudShown !== 'undefined' && hudShown && typeof hudSet === 'function') hudSet(0, '', false);
    // 3. NPCs: the first one in view is boxed, the others become tile marks
    const tiles = [], npcTiles = [];
    const npcs = Array.isArray(snap.npcs) ? snap.npcs : [];
    let boxed = false;
    for (const m of marks) {
      if (m.npc == null) continue;
      const ids = qtIds(m.npc), label = String(m.label || '');
      const at = (Array.isArray(m.at) && m.at.length === 3 && !inst) ? m.at : null;
      const seen = npcs.filter(e => e && ids.indexOf(e.id) >= 0);
      if (seen.length && !boxed) {
        const e = qtNearest(seen, at || (P ? [P.x, P.y] : null));
        qtOv('overlay.highlightNpc', '#' + e.id, label, at ? at[0] : 0, at ? at[1] : 0);
        boxed = true;
      } else if (seen.length) {
        const e = qtNearest(seen, at || (P ? [P.x, P.y] : null));
        npcTiles.push({ x: e.x, y: e.y, plane: e.p | 0, label: label });
      } else if (at) npcTiles.push({ x: at[0], y: at[1], plane: at[2] | 0, label: label });
    }
    if (!boxed) qtClrNpc();
    // 4. tiles: locs on their exact tile (every copy in view inside an instance), tiles and areas, ground items
    const locs = Array.isArray(snap.locs) ? snap.locs : [], ground = Array.isArray(snap.ground) ? snap.ground : [];
    for (const m of marks) {
      const label = String(m.label || '');
      if (m.loc != null) {
        const id = m.loc | 0;
        if (inst || !(Array.isArray(m.at) && m.at.length === 3)) {
          for (const o of locs) if (o && o.id === id) tiles.push({ x: o.x, y: o.y, plane: o.p | 0, label: label, snapId: id });
        } else tiles.push({ x: m.at[0], y: m.at[1], plane: m.at[2] | 0, label: label, snapId: id });
      } else if (Array.isArray(m.tile) && m.tile.length === 3) {
        if (inst !== (m.tile[0] >= QT_INST_X)) continue;   // an absolute tile means nothing inside an instance
        const r = { x: m.tile[0], y: m.tile[1], plane: m.tile[2] | 0, label: label };
        if (Array.isArray(m.to) && m.to.length === 2) { r.x2 = m.to[0]; r.y2 = m.to[1]; }
        tiles.push(r);
      } else if (m.ground != null) {
        const ids = qtIds(m.ground);
        for (const g of ground) if (g && ids.indexOf(g.id) >= 0) tiles.push({ x: g.x, y: g.y, plane: g.p | 0, label: label });
      }
    }
    const all = tiles.concat(npcTiles).slice(0, 64);
    if (all.length) qtOv('overlay.guideTiles', all); else qtClrTiles();
    // 5. backpack items and interface components share one channel
    const parts = [], slots = snap._slots || {};
    const lab = s => String(s || '').replace(/[,|\n]/g, ' ').replace(/\s+/g, ' ').trim();
    for (const m of marks) {
      if (m.item != null) {
        const id = qtIds(m.item).find(i => qtHas(slots, String(i)));
        if (id == null) continue;
        let r = null; try { r = qtJson(await rtxData.raw('state.invSlotRect', slots[String(id)]), null); } catch (e) {}
        if (r && r.w > 0) parts.push(r.x + ',' + r.y + ',' + r.w + ',' + r.h + ',' + lab(m.label));
      } else if (Array.isArray(m.iface) && m.iface.length === 2) {
        let cr = null; try { cr = qtJson(rtxData.sync('state.ifaceCompRects', m.iface[0], String(m.iface[1]), 0), null); } catch (e) {}
        const r = (cr && cr.abs && cr.comps) ? cr.comps[String(m.iface[1])] : null;
        if (Array.isArray(r) && r[2] > 0) parts.push(r[0] + ',' + r[1] + ',' + r[2] + ',' + r[3] + ',' + lab(m.label));
      }
    }
    if (parts.length) { try { rtxData.sync('overlay.panelViz', parts.join('|')); } catch (e) {} }
    else qtClrItem();
  }

  // ---- latches, kept in the prefs so a reload keeps them
  function qtLatchAll() {
    const o = qtJson((typeof prefGet === 'function') ? prefGet(QT_LATCH_PREF, '{}') : '{}', {});
    return (o && typeof o === 'object' && !Array.isArray(o)) ? o : {};
  }
  function qtLatchGet(name) {
    const q = qtLatchAll()[name], out = {};
    if (q && typeof q === 'object') for (const k in q) if (q[k]) out[k] = true;
    return out;
  }
  function qtLatchPut(name, t, lat, before) {
    const ids = ((t && t.latches) || []).map(l => l && l.id);
    const now = {};
    for (const k in lat) if (lat[k] && ids.indexOf(k) >= 0) now[k] = 1;
    const a = Object.keys(now).sort().join(','), b = Object.keys(before).sort().join(',');
    if (a === b) return;
    const all = qtLatchAll();
    if (a) all[name] = now; else delete all[name];
    try { prefSet(QT_LATCH_PREF, JSON.stringify(all)); } catch (e) {}
  }

  // ---- guide ticks
  function qtSetTicks(name, ticks) {
    if (typeof qgAutoDone === 'undefined' || !qgAutoDone) return;
    const sig = ticks.join(',');
    const cur = qgAutoDone[name];
    if (cur && cur.sig === sig) return;
    const set = new Set(ticks); set.sig = sig;
    qgAutoDone[name] = set;
    try { paneRun('questfocus', () => { qgSig = ''; renderQuestFocus(); }); paneRun('quests', () => { questDetailSig = ''; renderQuests(); }); } catch (e) {}
  }

  // ---- observe mode: one log line per event, the raw material for live evidence
  const qtObs = {};
  function qtObserving() { try { return String(prefGet(QT_OBSERVE_PREF, '0')) === '1'; } catch (e) { return false; } }
  async function qtObserve(name, t, snap, r) {
    const o = qtObs[name] || (qtObs[name] = { vars: {}, npcs: [], opts: '', dlg: '', st: undefined });
    const log = x => { try { console.log('[qt] ' + JSON.stringify(Object.assign({ q: name }, x))); } catch (e) {} };
    for (const a in snap.vars) {
      const v = snap.vars[a];
      if (!qtHas(o.vars, a) || o.vars[a] !== v) { log({ a: a, from: qtHas(o.vars, a) ? o.vars[a] : null, to: v }); o.vars[a] = v; }
    }
    if (r.state !== o.st) { log({ st: r.state, from: o.st === undefined ? null : o.st, P: snap.P }); o.st = r.state; }
    for (const id of qtNeeds(t).npcIds) {
      if (o.npcs.indexOf(id) >= 0) continue;
      const e = (snap.npcs || []).find(n => n.id === id);
      if (e) { o.npcs.push(id); log({ npc: id, x: e.x, y: e.y, p: e.p | 0 }); }
    }
    if (r.state) {
      const opts = (snap.dlg && qtNeeds(t).opts) ? snap.dlg.opts : await qtReadOpts();
      const sig = opts.join('|');
      if (sig && sig !== o.opts) log({ s: r.state, opts: opts });
      o.opts = sig;
    }
    if (qtNeeds(t).dlgText) {
      const d = (snap.dlg && snap.dlg.text) || '';
      if (d && d !== o.dlg) log({ dlg: d });
      o.dlg = d;
    }
  }

  // ---- one tick for one quest
  const qtBusy = new Set(), qtWarned = new Set(), qtFns = new Map();
  function qtFocusNow() {
    if (typeof qgFocusName !== 'function') return '';
    try { return String(qgFocusName() || ''); } catch (e) { return ''; }
  }
  function qtQuestDone(t) {
    return typeof questsData !== 'undefined' && !!questsData && !!questsData.st && t.qid != null && questsData.st[t.qid] === 2;
  }
  async function qtTick(name) {
    if (qtBusy.has(name)) return;   // the guide loop does not wait for a slow tick
    qtBusy.add(name);
    const focus0 = qtFocusNow();
    try {
      const all = qtTracks(), t = (all && qtHas(all, name)) ? all[name] : null;
      if (!t) return;
      if (qtQuestDone(t)) { qtClearAll(); return; }
      const snap = await qtSnapshot(t);
      if (!snap || snap._ok === false) return;   // a failed read changes nothing
      if (qtFocusNow() !== focus0) return;       // the focus moved while this tick was reading
      const before = qtLatchGet(name);
      const r = qtEval(t, snap, before);
      qtLatchPut(name, t, r.latches, before);
      qtSetTicks(name, r.ticks);
      const st = r.state ? ((t.states || []).find(s => s && s.id === r.state) || null) : null;
      if (st) await qtDraw(t, st, snap); else qtClearAll();
      if (qtObserving()) await qtObserve(name, t, snap, r);
    } catch (e) {
      if (!qtWarned.has(name)) { qtWarned.add(name); try { console.warn('[qt] ' + name + ': ' + ((e && e.message) || e)); } catch (e2) {} }
    } finally { qtBusy.delete(name); }
  }
  function qtStepFor(name) {
    if (!name || typeof name !== 'string' || QT_HAND.indexOf(name) >= 0) return null;
    if (typeof QG_AUTO !== 'undefined' && QG_AUTO && qtHas(QG_AUTO, name)) return null;
    const t = qtTracks();
    if (!t) { qtLoad(); return null; }
    if (!t || !qtHas(t, name) || !t[name] || typeof t[name] !== 'object') return null;
    let fn = qtFns.get(name);
    if (!fn) { fn = () => qtTick(name); qtFns.set(name, fn); }
    return fn;
  }

Object.assign(window, { qtCond, qtDraw, qtEval, qtLoad, qtNeeds, qtSnapshot, qtStepFor });
})();
