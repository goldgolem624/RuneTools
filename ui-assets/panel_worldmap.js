// RuneToolsX panel: World Map (full-world terrain browser: pan/zoom/search/layers).
// Spliced inline into client.html at load; shares its global scope (no IIFE); shared helpers
// ($, bridge, myPid, ...) and the clue-map data tables (MAP_LABELS, MAP_TELEPORTS, LODESTONES,
// HIDEY, nearLabel, teleReqMet) live in other spliced files and resolve at call time.

  // Terrain comes from bridge().mapWindow in fixed CHUNKS so pan/zoom never re-renders the
  // world server-side: one chunk = one cached offscreen canvas. Every level renders 512 px
  // images; higher px-per-tile just covers fewer tiles, so the bridge payload stays constant.
  // upto = the level's own ts, so the drawn image is always at native scale or minified,
  // never magnified (magnifying is what made the terrain look pixelated).
  const WM_LEVELS = [
    { ts: 2,  ch: 256, upto: 2 },
    { ts: 4,  ch: 128, upto: 4 },
    { ts: 8,  ch: 64,  upto: 8 },
    { ts: 16, ch: 32,  upto: 1e9 },  // WM_ZMAX 12 < 16: still minified
  ];
  const WM_ZMIN = 0.3, WM_ZMAX = 12, WM_CACHE_MAX = 130, WM_INFLIGHT_MAX = 4;
  let wmCam = { x: 3213.5, y: 3429.5, z: 1.4, p: 0 };   // world tile centre + px/tile + floor
  let wmLayer = { labels: true, lodes: true, teles: true, hidey: false, marks: true, grid: false, icons: true };
  let wmRO = null;          // stage ResizeObserver, replaced on each mount
  let wmCamSaved = false;   // a persisted camera skips the centre-on-player of the first open
  try {
    const c = JSON.parse(localStorage.getItem('rtxWmCam') || 'null');
    if (c && isFinite(c.x) && isFinite(c.y) && isFinite(c.z)) {
      wmCam = { x: +c.x, y: +c.y, z: Math.max(WM_ZMIN, Math.min(WM_ZMAX, +c.z)), p: (c.p | 0) || 0 };
      wmCamSaved = true;
    }
    const l = JSON.parse(localStorage.getItem('rtxWmLayers') || 'null');
    if (l) for (const k in wmLayer) if (typeof l[k] === 'boolean') wmLayer[k] = l[k];
  } catch (e) {}
  let wmCamSaveT = 0;
  function wmSaveCam() {
    clearTimeout(wmCamSaveT);
    wmCamSaveT = setTimeout(function () {
      try { localStorage.setItem('rtxWmCam', JSON.stringify({ x: Math.round(wmCam.x * 10) / 10, y: Math.round(wmCam.y * 10) / 10, z: Math.round(wmCam.z * 1000) / 1000, p: wmCam.p })); } catch (e) {}
    }, 400);
    wmCamSaved = true;
  }
  function wmSaveLayers() { try { localStorage.setItem('rtxWmLayers', JSON.stringify(wmLayer)); } catch (e) {} }
  function wmLevel(z) { for (const L of WM_LEVELS) if (z <= L.upto) return L; return WM_LEVELS[WM_LEVELS.length - 1]; }

  // ---- chunk cache (LRU by Map insertion order) + bounded fetch queue ----
  const wmCache = new Map();    // "ts|plane|ix|iy" -> {cv: canvas | null (no map data)}
  const wmQueued = new Map();   // same key -> {key, ts, plane, ix, iy, half, cx, cy, pri, busy}
  let wmInflight = 0, wmFailAt = 0;
  function wmKey(ts, plane, ix, iy) { return ts + '|' + plane + '|' + ix + '|' + iy; }
  function wmGet(ts, plane, ix, iy, peek) {
    const k = wmKey(ts, plane, ix, iy), r = wmCache.get(k);
    if (r && !peek) { wmCache.delete(k); wmCache.set(k, r); }   // touch = move to LRU tail
    return r;
  }
  function wmSet(key, rec) {
    wmCache.delete(key); wmCache.set(key, rec);
    while (wmCache.size > WM_CACHE_MAX) { const first = wmCache.keys().next().value; wmCache.delete(first); }
  }
  function wmEnqueue(ts, plane, ix, iy, pri) {
    const k = wmKey(ts, plane, ix, iy);
    if (wmCache.has(k)) return;
    const j = wmQueued.get(k);
    if (j) { if (pri < j.pri) j.pri = pri; return; }
    const L = WM_LEVELS.find(v => v.ts === ts), ch = L.ch;
    wmQueued.set(k, { key: k, ts: ts, plane: plane, ix: ix, iy: iy, half: ch / 2, cx: ix * ch + ch / 2, cy: iy * ch + ch / 2, pri: pri, busy: false });
  }
  // The reader sends terrain as a PNG (`png`); older builds sent raw RGBA (`b64`). Both are
  // handled so a panel reload works either side of a rebuild. The PNG decodes natively, which
  // also skips the per-character base64 walk the raw form needs.
  async function wmDecode(meta) {
    const W = meta.w | 0;
    const cv = document.createElement('canvas'); cv.width = W; cv.height = W;
    if (meta.png) {
      const img = await new Promise((res, rej) => {
        const im = new Image();
        im.onload = () => res(im);
        im.onerror = rej;
        im.src = 'data:image/png;base64,' + meta.png;
      });
      cv.getContext('2d').drawImage(img, 0, 0);
      return cv;
    }
    const bin = atob(meta.b64), a = new Uint8ClampedArray(bin.length);
    for (let i = 0; i < bin.length; i++) a[i] = bin.charCodeAt(i);
    cv.getContext('2d').putImageData(new ImageData(a, W, W), 0, 0);
    return cv;
  }
  // Map ELEMENT pins: 10 bytes each - wtx u16 LE, wty u16 LE, element id u16 LE, loc id u32 LE.
  // Fixed stride, no header. Tile coords are WINDOW-relative; the chunk's ox/oy turn them into
  // world tiles. The loc id is per-PLACEMENT and only used as the tooltip's name of last resort.
  function wmIcons(b64) {
    if (!b64) return null;
    const bin = atob(b64), n = (bin.length / 10) | 0;
    if (!n) return null;
    const out = new Array(n);
    for (let i = 0; i < n; i++) {
      const o = i * 10;
      out[i] = { tx: bin.charCodeAt(o)     | (bin.charCodeAt(o + 1) << 8),
                 ty: bin.charCodeAt(o + 2) | (bin.charCodeAt(o + 3) << 8),
                 ml: bin.charCodeAt(o + 4) | (bin.charCodeAt(o + 5) << 8),
                 // >>> 0: loc ids run past 2^31 and a signed shift would wrap them negative.
                 id: (bin.charCodeAt(o + 6)        | (bin.charCodeAt(o + 7) << 8) |
                     (bin.charCodeAt(o + 8) << 16) | (bin.charCodeAt(o + 9) << 24)) >>> 0 };
    }
    return out;
  }
  // The per-ELEMENT tables the pins index into: sprite, category, tooltip text. ~5,800 entries
  // for the whole game, so they come down ONCE rather than riding along in every chunk.
  let WM_ML = null, WM_CAT = null, WM_LOCNM = null, wmMlPend = false;
  function wmLoadElements() {
    if (WM_ML || wmMlPend || !bridge() || !bridge().mapLabels) return;
    wmMlPend = true;
    (async function () {
      try {
        const a = JSON.parse((await bridge().mapLabels()) || '{}');
        if (a && Object.keys(a).length) WM_ML = a;
      } catch (e) {}
      try {
        const c = JSON.parse((await bridge().mapCategories()) || '{}');
        if (c) WM_CAT = c;
      } catch (e) {}
      // Names of the locs that PLACE the elements, for the 22 elements that have no name by any
      // other route. A few KB; loaded here so the tooltip stays synchronous on hover.
      try {
        if (bridge().mapLocNames) {
          const ln = JSON.parse((await bridge().mapLocNames()) || '{}');
          if (ln) WM_LOCNM = ln;
        }
      } catch (e) {}
      wmMlPend = false;
      wmKick();
      wmPrefetchDb();   // DB tables for the structured tooltips, one gated slice at a time
    })();
  }
  // ================= structured element tooltips =================
  // ~24% of map tooltips are built by per-category LAYOUT scripts rather than the plain
  // "param 4149 else category name" rule: a willow shows "WILLOW TREE / Willow logs / Lvl 20",
  // not "Trees - Uncommon". clientscript-7590 maps category -> KIND, clientscript-9566 maps
  // KIND -> layout. Both are tables here, so a new kind is a row rather than a branch. Every
  // structured layout keys off element param 4147; with no 4147 the game falls through to the
  // plain rule, and so do we.
  const WM_KIND = { 1159: 7, 4551: 9, 4624: 10, 4625: 11, 1184: 14, 5121: 14,
                    1176: 15, 3032: 16, 1205: 17, 5699: 21 };
  // Kind 19 = skill-training spots. 4387 and 4391 are deliberately absent: they appear in
  // neither the 7590 switch nor enum 8586.
  '1199 2579 4383 4384 4385 4386 4388 4389 4390 4392 4393 4394 4395 4396 4397 4398 4399 4400 4401 4402'
    .split(' ').forEach(function (c) { WM_KIND[c | 0] = 19; });

  const WM_DB  = Object.create(null);   // cs2 table id -> rows
  const WM_DBI = Object.create(null);   // "table:col"  -> Map(colValue -> first row)
  const WM_DBF = Object.create(null);   // table        -> Map(fileId -> row)
  const WM_ST  = new Map();             // struct id -> {ints,strs}; null while in flight
  const WM_EN  = new Map();             // enum id   -> {k:v};       null while in flight
  const WM_IN  = new Map();             // item id   -> name;        null while in flight
  let WM_SKSPR = null, wmSkPend = false;   // enum 8548: skill index -> sprite id

  // DbRowsJson has NO cache: it decodes all ~19,560 files of configs archive 41 per call, holds
  // the same mutex MapWindowJson takes, and is bound SYNCHRONOUSLY - so `await` does not yield,
  // it blocks. Pull each table exactly once, one per slice, and never while the user is panning
  // or a terrain chunk is in flight. Order is the "which kind arms first" knob: trees lead so
  // the common case works within a slice.
  const WM_DB_ORDER = [172, 171, 34, 170, 180, 169, 358, 86, 93, 53, 88, 89];
  let wmDbAt = 0, wmDbBusy = false;
  function wmPrefetchDb() {
    if (wmDbBusy || wmDbAt >= WM_DB_ORDER.length) return;
    if (!bridge() || !bridge().dbRows) return;
    if (wmDrag || wmFly || wmInflight || wmQueued.size) { setTimeout(wmPrefetchDb, 300); return; }
    wmDbBusy = true;
    const t = WM_DB_ORDER[wmDbAt++];
    (async function () {
      try { WM_DB[t] = JSON.parse((await bridge().dbRows(t)) || '[]') || []; } catch (e) { WM_DB[t] = []; }
      try { wmSymProvInvalidate(); } catch (e) {}   // a table landed: provisional search haystacks rebuild once
      wmDbBusy = false;
      setTimeout(wmPrefetchDb, 60);      // one archive walk per slice, never two in a frame
    })();
  }
  // DB_FIND takes the FIRST match and dbRows emits in ascending file id, so first-wins is right.
  function wmDbIdx(t, colKey) {
    const ck = t + ':' + colKey;
    let m = WM_DBI[ck]; if (m) return m;
    const rows = WM_DB[t]; if (!rows) return null;
    m = new Map();
    for (const r of rows) {
      const v = r.i && r.i[colKey];
      if (v && v.length && !m.has(v[0])) m.set(v[0], r);
    }
    WM_DBI[ck] = m; return m;
  }

  // ---- lazy, memoised cache reads. A miss repaints the tooltip when it lands. ----
  function wmStruct(id) {
    if (id == null || id < 0) return null;
    if (WM_ST.has(id)) return WM_ST.get(id);
    WM_ST.set(id, null);
    (async function () {
      let v = null; try { v = JSON.parse((await bridge().structParams(id)) || 'null'); } catch (e) {}
      WM_ST.set(id, v || { ints: {}, strs: {} }); wmTipRefresh();
    })();
    return null;
  }
  function wmEnum(id) {
    if (id == null || id < 0) return null;
    if (WM_EN.has(id)) return WM_EN.get(id);
    WM_EN.set(id, null);
    (async function () {
      let v = null; try { v = JSON.parse((await bridge().enumInfo(id)) || 'null'); } catch (e) {}
      WM_EN.set(id, v || {}); wmTipRefresh();
    })();
    return null;
  }
  // undefined = still loading, '' = genuinely nameless. Eighteen referenced items really have no
  // name, and the game prints "XP training method - no resource" for exactly those, so the two
  // states must not collapse.
  function wmItemName(id) {
    if (!id || id < 0) return '';
    if (WM_IN.has(id)) { const v = WM_IN.get(id); return v === null ? undefined : v; }
    WM_IN.set(id, null);
    (async function () {
      let n = '';
      try { const o = JSON.parse((await bridge().itemInfo(id)) || 'null'); n = (o && o.name) || ''; } catch (e) {}
      WM_IN.set(id, n); wmTipRefresh();
    })();
    return undefined;
  }
  // Skill icons come from enum 8548[skill] -> a real sprite. NOT struct param 1998, which is an
  // animation sequence: using it would put a shield beside "Raw mackerel".
  //
  // `sk` is the game's 1-BASED skill id, which is NOT the SKILL_NAMES index: 8548 is keyed 1..29
  // and 10 there is Thieving, not Fishing. Enum 681 is the translation (skill id -> SKILL_NAMES
  // index) if you ever need to print a name next to one of these.
  function wmSkillSprite(sk) {
    if (!sk || sk <= 0) return '';
    if (!WM_SKSPR) {
      if (!wmSkPend) {
        wmSkPend = true;
        (async function () {
          try { WM_SKSPR = JSON.parse((await bridge().enumInfo(8548)) || '{}') || {}; } catch (e) { WM_SKSPR = {}; }
          wmTipRefresh();
        })();
      }
      return '';
    }
    return wmSpriteUrl(WM_SKSPR[sk] | 0);
  }

  // Kinds 14/16/17/19/21 share one shape: rows in STORED order, no sort and no merge. (Table 172
  // stores 1,10,20,70,68,90 for one row - ivy 68 after yew 70 - so any level sort would be wrong.)
  function wmStructRows(list, skill, useAlt) {
    const rows = [];
    for (const sid of list) {
      const p = wmStruct(sid);
      if (!p) { rows.push(null); continue; }          // placeholder until it lands
      let obj = -1;
      if (useAlt && p.ints['9417'] != null && p.ints['9417'] !== -1) obj = p.ints['9417'] | 0;
      else if (p.ints['2213'] != null) obj = p.ints['2213'] | 0;
      const nm = obj > 0 ? wmItemName(obj) : '';
      rows.push({ sk: (p.ints['2215'] != null ? p.ints['2215'] | 0 : skill),
                  lvl: (p.ints['2212'] != null ? p.ints['2212'] | 0 : null),
                  name: p.strs['2210'] || '',
                  obj: (obj > 0 && nm) ? obj : 0,
                  objName: (obj > 0 && nm) ? nm : '',
                  note: (obj > 0 && nm === '') ? 'XP training method' : '' });
    }
    return rows;
  }
  function wmLayStructList(t, key, skill, useAlt) {
    const ix = wmDbIdx(t, '0'); if (!ix) return null;
    const r = ix.get(key); if (!r) return { head: '', rows: [] };
    const list = ((r.i && r.i['2']) || []).filter(function (v) { return v !== -1; });
    let head = (r.s && r.s['1'] && r.s['1'][0]) || '';
    if (!head && list.length) { const p = wmStruct(list[list.length - 1]); head = p ? (p.strs['2210'] || '') : ''; }
    return { head: head, rows: wmStructRows(list, skill, useAlt), one: list.length <= 1 };
  }
  // Farming differs only in that col 2 is an ENUM id whose values are the structs.
  function wmLayFarm(key) {
    const ix = wmDbIdx(170, '0'); if (!ix) return null;
    const r = ix.get(key); if (!r) return { head: '', rows: [] };
    let head = (r.s && r.s['1'] && r.s['1'][0]) || '';
    const eid = r.i && r.i['2'] && r.i['2'][0];
    const en = (eid != null) ? wmEnum(eid) : null;
    if (!en) return { head: head, rows: [] };
    const list = Object.keys(en).map(Number).sort(function (a, b) { return a - b; })
                       .map(function (i) { return en[i]; });
    if (!head && list.length) { const p = wmStruct(list[list.length - 1]); head = p ? (p.strs['2210'] || '') : ''; }
    return { head: head, rows: wmStructRows(list, 21, false), one: list.length <= 1 };
  }
  // Mining: the header is the element's OWN param 4149 passed through VERBATIM - it is not split
  // on <br>/": " the way the plain rule splits, and it is not the DB col 1 (that is the rock name).
  function wmLayMine(key, el) {
    const ix = wmDbIdx(34, '0'); if (!ix) return null;
    const p = (el && el.p) || {}, keys = [key], rows = [];
    ['7774', '7775', '7776', '7777', '7778', '7779', '7780'].forEach(function (k2) {
      const v = p[k2]; if (v != null && v !== -1) keys.push(v | 0);
    });
    for (const kk of keys) {
      const r = ix.get(kk); if (!r) continue;          // unmatched keys are silently skipped
      const objs = (r.i && r.i['3']) || [], lv = (r.i && r.i['4']) || [];
      const first = objs.length ? objs[0] : 0;
      const nm = first ? wmItemName(first) : '';
      rows.push({ sk: 13, lvl: lv.length ? lv[0] : null,
                  name: (r.s && r.s['1'] && r.s['1'][0]) || '',
                  obj: (first && nm) ? first : 0, objName: (first && nm) ? nm : '',
                  more: objs.length > 1 ? objs.length - 1 : 0, note: '' });
    }
    return { head: (el && el.n) || '', rows: rows, rawHead: true, alwaysName: true,
             reqs: wmReqs(el) };
  }
  // ---- element REQUIREMENTS ("Requirements:" under a map tooltip) ----
  // Element param 7781 names a STRUCT holding up to 20 (type, value) pairs. The game reads them in
  // clientscripts 13327 (text) / 13290 (met) and prints them under the tooltip. Param 478 is NOT
  // the source - it is a constant 1 and only drives the membership fallback line below.
  //
  // Only the Mining Site layout (kind 7) has a real requirements block: the other kinds route
  // through script 16461, which emits nothing but the membership line. So this is wired into
  // wmLayMine, not into every layout.
  const WM_REQ_SLOTS = [
    [1294, 1295], [1296, 1297], [1298, 1299], [1300, 1301], [1302, 1303], [1304, 1305],
    [1306, 1307], [1308, 1309], [1310, 1311], [1312, 1313], [2227, 2228], [2229, 2230],
    [4474, 4475], [6434, 6435], [6436, 6437], [6438, 6439], [6440, 6441], [6442, 6443],
    [6444, 6445], [6446, 6447]
  ];
  let WM_QJ = null;   // quest JOURNAL id -> quest row; the struct names quests by journal id
  function wmQuestByJournal(j) {
    if (typeof QUESTS === 'undefined' || !QUESTS) return null;
    if (!WM_QJ) {
      WM_QJ = {};
      for (const q of QUESTS) if (q && q.j != null) WM_QJ[q.j] = q;
    }
    return WM_QJ[j] || null;
  }
  // Returns [{text, ok}] with ok true/false/null (null = we cannot tell), or null while a cache
  // read is still in flight - the tooltip repaints itself when it lands (wmTipRefresh).
  function wmReqs(el) {
    const p = (el && el.p) || {};
    const sid = p['7781'];
    const skills = [], other = [];
    if (sid != null && sid !== -1) {
      const st = wmStruct(sid);
      if (!st) return null;
      const stat = wmEnum(681);             // requirement type -> SKILL_NAMES index
      if (!stat) return null;
      const sk = (typeof lastSnap !== 'undefined' && lastSnap && Array.isArray(lastSnap.skills))
                 ? lastSnap.skills : null;
      const qst = (typeof questsData !== 'undefined' && questsData) ? questsData.st : null;
      for (const pr of WM_REQ_SLOTS) {
        const ty = st.ints[String(pr[0])];
        const v  = st.ints[String(pr[1])];
        if (ty == null || ty === -1) continue;
        if (ty >= 1 && ty <= 59) {
          const idx = stat[String(ty)];
          if (idx == null) continue;
          const nm = (typeof SKILL_NAMES !== 'undefined' && SKILL_NAMES[idx]) || ('skill ' + idx);
          const have = (sk && sk[idx]) ? (sk[idx][0] | 0) : null;
          skills.push({ text: 'Level ' + v + ' ' + nm, ok: have == null ? null : have >= v });
        } else if (ty === 60) {             // achievement: enum 12251 id -> struct, param 6410 = name
          const en = wmEnum(12251);
          if (!en) return null;
          const asid = en[String(v)];
          if (asid == null) continue;
          const ast = wmStruct(asid | 0);
          if (!ast) return null;
          const an = ast.strs['6410'];
          if (an) other.push({ text: an, ok: null });
        } else if (ty === 61) {             // quest, named by JOURNAL id (see the "j" field)
          const q = wmQuestByJournal(v | 0);
          if (!q) continue;
          other.push({ text: q.n, ok: qst ? (qst[q.id] === 2) : null });
        } else if (ty === 63) {
          other.push({ text: 'Combat level ' + v, ok: null });
        } else if (ty === 64) {
          const have = (typeof questsData !== 'undefined' && questsData) ? (questsData.qp | 0) : null;
          other.push({ text: v + ' Quest points', ok: have == null ? null : have >= v });
        } else if (ty === 65) {
          other.push({ text: v + ' Varrock Museum kudos', ok: null });
        }
        // type 62 routes through clientscript 13302, which is not decoded. Skipping it prints a
        // short list rather than a wrong one; it affects one struct (Ancient Cavern Mine).
      }
    }
    // Non-skill requirements first, then skills - the order the game's own two passes produce.
    const out = other.concat(skills);
    // Members fallback: only when the struct said nothing at all, matching the game's guard.
    if (!out.length && p['478']) out.push({ text: 'Membership', ok: null });
    return out;
  }
  function wmDbByFile(t) {
    let m = WM_DBF[t]; if (m) return m;
    const rows = WM_DB[t]; if (!rows) return null;
    m = new Map(); for (const r of rows) m.set(r.f, r);
    WM_DBF[t] = m; return m;
  }
  function wmGrp(label, ids) {
    return { label: label, items: (ids || []).filter(function (i) { return i > 0; })
      .map(function (i) { const n = wmItemName(i); return { id: i, name: (n == null ? '' : n) || ('#' + i) }; }) };
  }
  function wmLayExcav(key) {
    const ix = wmDbIdx(86, '0'), by88 = wmDbByFile(88), by89 = wmDbByFile(89);
    if (!ix || !by88 || !by89) return null;
    const r = ix.get(key); if (!r) return { head: '', rows: [] };
    if (((r.i['1'] || [0])[0]) !== 2) return { head: '', rows: [] };     // type guard: 2 = excavation
    const rows = [];
    for (const cf of (r.i['17'] || [])) {
      const c = by88.get(cf); if (!c) continue;
      // dbRows flattens tuple members onto CONSECUTIVE column indices, and table 88 has three
      // adjacent tuple columns (15,16,17), so i[16] and i[17] each hold two concatenated lists.
      // Peel by length.
      const m15 = c.i['15'] || [], m16 = c.i['16'] || [], m17 = c.i['17'] || [];
      const arts = m17.slice(Math.max(0, m16.length - m15.length));
      let nm = (c.s && c.s['2'] && c.s['2'][0]) || '';
      if (!nm && ((c.i['1'] || [0])[0]) === 2) {
        const f = m15.length ? wmItemName(m15[0]) : '';
        nm = 'Material cache' + (m15.length && f ? ' (' + f + ')' : '');
      }
      rows.push({ sk: 28, lvl: (c.i['4'] || [null])[0], name: nm, groups: [
        wmGrp('Materials', m15),
        wmGrp('Artefacts', arts.map(function (a) { const x = by89.get(a);
                 return (x && x.i['6']) ? x.i['6'][0] : 0; })) ] });
    }
    return { head: (r.s['2'] || [''])[0], rows: rows, alwaysName: true, wide: true };
  }
  // Dig-site manager names are HARDCODED in CS2, switched on the row's archive-41 FILE ID rather
  // than any column, so a cache renumber breaks this silently.
  const WM_DIG_MGR = { 2802: 'Dr Nabanik', 2803: 'Vanescula Drakan', 2804: 'Movario',
                       2805: "Gee'ka", 2806: 'Zanik', 3703: 'Mr Mordaut', 4408: 'Jimmy',
                       13665: 'Skaldrun', 18292: 'Utu' };
  function wmLayDig(key) {
    const ix = wmDbIdx(86, '0'), q = wmDbIdx(93, '0');
    if (!ix || !q) return null;
    const r = ix.get(key); if (!r) return { head: '', rows: [] };
    if (((r.i['1'] || [0])[0]) !== 1) return { head: '', rows: [] };     // type guard: 1 = dig site
    const facts = [], mgr = WM_DIG_MGR[r.f];
    if (mgr) facts.push([r.f === 4408 ? 'Assistant Manager' : 'Manager', mgr]);
    if (r.i['6']) facts.push(['Archaeology Level', String(r.i['6'][0])]);
    if (r.i['7']) { const qr = q.get(r.i['7'][0]); if (qr && qr.s['1']) facts.push(['Qualification', qr.s['1'][0]]); }
    if (r.i['13']) { const s = wmItemName(r.i['13'][0]); if (s) facts.push(['Soil', s]); }
    if (((r.i['4'] || [0])[0]) === 1) facts.push(['Status', 'Members-only']);
    return { head: ((r.s['2'] || [''])[0]) + ' Dig Site',
             desc: (r.s['3'] || [''])[0], facts: facts, rows: [] };
  }
  // Big Game Hunter keys off column 3, NOT 0. The sub-1 junk rows carry no col 3, which
  // conveniently filters them out.
  function wmLayHunt(key) {
    const ix = wmDbIdx(53, '3'); if (!ix) return null;
    const r = ix.get(key); if (!r) return { head: '', rows: [] };
    const rows = [];
    if (r.i['10']) rows.push({ sk: 23, lvl: r.i['10'][0], name: 'Hunter', objs: [] });
    if (r.i['11']) rows.push({ sk: 20, lvl: r.i['11'][0], name: 'Slayer', objs: [] });
    return { head: (r.s['2'] || [''])[0], rows: rows, alwaysName: true,
             groups: [wmGrp('Bait', r.i['9'] || []), wmGrp('Rewards', r.i['13'] || [])] };
  }
  // Fishing spots. Each column of the game's table is one METHOD (Net / Harpoon / ...): col 3 is
  // a (struct, methodCode) list whose length IS the column count. Each method struct carries up
  // to 8 catches on a 7-key stride - level at 2001+7k, item at 2003+7k. Method icons are
  // HARDCODED item ids switched on the method code; struct param 1998 is an animation, not an
  // icon, and using it would put a shield beside "Raw mackerel".
  const WM_FISH_TOOL = { 1: 52868, 2: 52869, 3: 307, 4: 309, 5: 309, 6: 311, 7: 13431, 8: 301 };
  function wmLayFish(key) {
    const ix = wmDbIdx(171, '0'); if (!ix) return null;
    const r = ix.get(key); if (!r) return { head: '', rows: [] };
    const a = r.i['3'] || [], b = r.i['4'] || [];
    let structs = a, codes = b;
    if (!b.length && a.length > 1 && (a.length % 2) === 0) {      // both members on one column
      structs = a.slice(0, a.length / 2); codes = a.slice(a.length / 2);
    }
    const rows = [];
    for (let i = 0; i < structs.length; i++) {
      const p = wmStruct(structs[i]);
      if (!p) { rows.push(null); continue; }
      const catches = [];
      for (let k = 0; k < 8; k++) {
        const lv = p.ints[String(2001 + 7 * k)], it = p.ints[String(2003 + 7 * k)];
        if (it == null || it <= 0 || it === 3157) continue;        // 3157 is skipped by the game
        catches.push({ lvl: lv == null ? null : (lv | 0), id: it | 0 });
      }
      catches.sort(function (x, y) { return (x.lvl || 0) - (y.lvl || 0); });
      const tool = WM_FISH_TOOL[codes[i] | 0] || 0;
      // 15, not 10. `sk` is the game's 1-BASED skill id (the key space of enum 8548 and enum 681),
      // not the 0-based SKILL_NAMES index - and 10 in that space is Thieving, which is what every
      // fishing spot drew. Fishing is 15. Every other layout here was already in the right space;
      // the Hunter (23) and Slayer (20) rows below self-check, since they carry their own labels.
      rows.push({ sk: 15, lvl: catches.length ? catches[0].lvl : null,
                  name: (tool ? (wmItemName(tool) || '') : '') || 'Fishing',
                  groups: [wmGrp('', catches.map(function (c) { return c.id; }))] });
    }
    return { head: (r.s['1'] || [''])[0], rows: rows, alwaysName: true, wide: rows.length > 1 };
  }
  const WM_LAY = {
    7:  { t: [34],         b: wmLayMine },
    9:  { t: [53],         b: wmLayHunt },
    10: { t: [86, 93],     b: wmLayDig },
    11: { t: [86, 88, 89], b: wmLayExcav },
    14: { t: [172], b: function (k) { return wmLayStructList(172, k, 18, false); } },
    15: { t: [171],        b: wmLayFish },
    16: { t: [169], b: function (k) { return wmLayStructList(169, k, 26, false); } },
    17: { t: [170],        b: wmLayFarm },
    19: { t: [180], b: function (k) { return wmLayStructList(180, k,  0, false); } },
    21: { t: [358], b: function (k) { return wmLayStructList(358, k, 23, true); } }
  };
  // The structured payload for an element, or null to use the plain rule.
  function wmElemRich(ml) {
    const e = WM_ML && WM_ML[ml]; if (!e) return null;
    const kind = WM_KIND[e.c]; if (!kind) return null;
    const lay = WM_LAY[kind]; if (!lay) return null;
    const key = e.p && e.p['4147'];
    if (key == null || key === -1) return null;        // no 4147 -> the plain rule is correct
    for (const t of lay.t) if (!WM_DB[t]) { wmPrefetchDb(); return null; }
    let r = null;
    try { r = lay.b(key | 0, e); } catch (err) { r = null; }
    if (!r || !r.rows || !r.rows.length) return null;
    return r;
  }

  // ---- searchable symbol index ----
  // Search must reach symbols in chunks that were never fetched, so placements come from one
  // world-wide walk (bridge().mapSymbols) rather than the loaded chunks. The walk is expensive,
  // so it is memoised in C++ for the session and persisted here per client build.
  let WM_SYM = null, wmSymPend = false;       // [{ml,x,y,p}]
  const WM_SYM_TXT = new Map();               // element id -> lowercase haystack
  // Skill names for type search: "woodcutting", "fishing" and friends map to the skill each
  // layout reports, so a type query finds every spot of that kind even before its rows resolve.
  const WM_SKILL_NM = { 0: 'attack', 1: 'defence', 2: 'strength', 3: 'constitution', 4: 'ranged',
    5: 'prayer', 6: 'magic', 7: 'cooking', 8: 'woodcutting', 9: 'fletching', 10: 'fishing',
    11: 'firemaking', 12: 'crafting', 13: 'mining', 14: 'smithing', 15: 'herblore', 16: 'agility',
    17: 'thieving', 18: 'slayer', 19: 'farming', 20: 'runecrafting', 21: 'hunter', 22: 'construction',
    23: 'summoning', 24: 'dungeoneering', 25: 'divination', 26: 'invention', 27: 'archaeology',
    28: 'necromancy' };
  // The layout each kind reports its rows against, so "woodcutting" matches trees even when the
  // per-row skill has not been resolved yet.
  const WM_KIND_SKILL = { 7: 13, 9: 21, 10: 27, 11: 27, 14: 8, 15: 10, 16: 25, 17: 19, 21: 23 };
  function wmLoadSymbols() {
    if (WM_SYM || wmSymPend || !bridge() || !bridge().mapSymbols) return;
    wmSymPend = true;
    (async function () {
      let o = null;
      try {
        const k = 'wmsym';
        if (bridge().cacheStoreLoad) {                       // persisted from a previous session
          const raw = await bridge().cacheStoreLoad(k);
          if (raw) { const j = JSON.parse(raw); if (j && j.v === wmBuild() && j.b) o = j; }
        }
        if (!o) {
          o = JSON.parse((await bridge().mapSymbols()) || 'null');
          if (o && o.b && bridge().cacheStoreSave)
            try { await bridge().cacheStoreSave(k, JSON.stringify({ v: wmBuild(), b: o.b })); } catch (e) {}
        }
      } catch (e) { o = null; }
      if (o && o.b) {
        const bin = atob(o.b), n = (bin.length / 7) | 0, out = new Array(n);
        for (let i = 0; i < n; i++) {
          const q = i * 7;
          out[i] = { ml: bin.charCodeAt(q)     | (bin.charCodeAt(q + 1) << 8),
                     x:  bin.charCodeAt(q + 2) | (bin.charCodeAt(q + 3) << 8),
                     y:  bin.charCodeAt(q + 4) | (bin.charCodeAt(q + 5) << 8),
                     p:  bin.charCodeAt(q + 6) };
        }
        WM_SYM = out;
      }
      wmSymPend = false;
    })();
  }
  const wmBuild = function () {
    return (typeof lastSnap !== 'undefined' && lastSnap && lastSnap.client_version) || '';
  };
  // Everything an element can be found by: its category ("fishing spot"), its own tooltip text,
  // its skill name ("woodcutting"), and every resource its layout lists ("willow", "yew"). Built
  // lazily per element and rebuilt while its rows are still resolving, so it sharpens as the
  // structured data lands rather than caching a half-empty haystack.
  const WM_SYM_TXT_PROV = new Map();   // haystacks built before the DB tables settled: reused until they do
  let wmSymProvGen = 0;
  function wmSymText(ml) {
    const e = WM_ML && WM_ML[ml]; if (!e) return '';
    const done = WM_SYM_TXT.get(ml);
    if (done) return done;
    const prov = WM_SYM_TXT_PROV.get(ml);
    if (prov && prov.g === wmSymProvGen) return prov.t;
    const bits = [];
    if (e.n) bits.push(e.n);
    if (e.t) bits.push(e.t);
    if (WM_CAT && WM_CAT[e.c] && WM_CAT[e.c].n) bits.push(WM_CAT[e.c].n);
    const kind = WM_KIND[e.c];
    if (kind != null && WM_SKILL_NM[WM_KIND_SKILL[kind]]) bits.push(WM_SKILL_NM[WM_KIND_SKILL[kind]]);
    let settled = true;
    const r = wmElemRich(ml);
    if (r) {
      if (r.head) bits.push(r.head);
      for (const row of r.rows) {
        if (!row) { settled = false; continue; }             // still resolving
        if (row.name) bits.push(row.name);
        if (row.objName) bits.push(row.objName);
        if (row.sk && WM_SKILL_NM[row.sk]) bits.push(WM_SKILL_NM[row.sk]);
        for (const g of (row.groups || [])) for (const it of g.items) if (it.name) bits.push(it.name);
      }
    } else if (kind != null) {
      settled = false;                                       // tables not in yet
    }
    const txt = bits.join(' ').toLowerCase();
    if (settled) { WM_SYM_TXT.set(ml, txt); WM_SYM_TXT_PROV.delete(ml); }   // only freeze a COMPLETE haystack
    else WM_SYM_TXT_PROV.set(ml, { g: wmSymProvGen, t: txt });              // provisional: valid until a table lands
    return txt;
  }
  // Called when a DB table arrives so provisional haystacks are rebuilt once, not per keystroke.
  function wmSymProvInvalidate() { wmSymProvGen++; }

  // "Heist: Asuran Arsenal" -> ["HEIST", "Asuran Arsenal"]. The game splits on the first <br>,
  // and only if there is none, on the first ": " (clientscript 16446). Order matters:
  // "Dungeon link<br>To Dig Site: Orthen" must split at the <br> so the colon inside the
  // destination stays in the body.
  function wmElemTip(ml) {
    const e = WM_ML && WM_ML[ml];
    if (!e) return null;
    const catNm = (WM_CAT && WM_CAT[e.c] && WM_CAT[e.c].n) || '';
    let s = '';
    // SHORTCUTS build their string as category + "<br>" + requirement, so the header stays
    // "Shortcut" and the requirement drops to the body. Taking 4149 alone (the generic rule)
    // promoted the requirement INTO the header, which is why one read "LEVEL 37 RANGED".
    // Of 169 shortcut elements only 53 carry a text requirement (4149) and 4 a bare level
    // (4150); the remaining 112 genuinely record none, and header-only is correct for those.
    if (e.c === 1217) {
      const lv = e.p && e.p['4150'];
      const req = e.n || ((lv != null && lv !== -1) ? ('Level ' + lv) : '');
      s = catNm ? (req ? catNm + '<br>' + req : catNm) : req;
    } else {
      s = e.n || e.t || '';
      if (!s) s = catNm;
    }
    if (!s) return null;
    let i = s.indexOf('<br>'), sep = 4;
    if (i < 0) { i = s.indexOf(': '); sep = 2; }
    const head = i < 0 ? s : s.slice(0, i);
    const body = i < 0 ? '' : s.slice(i + sep);
    return { head: head, body: body };
  }
  async function wmPump() {
    if (wmInflight >= WM_INFLIGHT_MAX) return;
    if (Date.now() - wmFailAt < 1500) return;                  // bridge hiccup: brief backoff, the next draw retries
    let best = null;
    for (const j of wmQueued.values()) if (!j.busy && (!best || j.pri < best.pri)) best = j;
    if (!best || !bridge() || !bridge().mapWindow) return;
    best.busy = true; wmInflight++;
    let rec = null;
    try {
      // want = 3: terrain + icons only. The collision grids and objs (~175 KB of base64 per
      // chunk at the finest level) were shipped and discarded on every window before.
      const meta = JSON.parse((await bridge().mapWindow(best.cx, best.cy, best.plane, best.half, best.ts, 3)) || '{}');
      if (meta && meta.pending) {
        // Built off the UI thread now: poll again shortly; the job stays queued and un-busy so
        // the priority order is re-evaluated against the current view.
        best.busy = false; wmInflight--;
        setTimeout(wmPump, 45);
        return;
      }
      // ox/oy = the window's SW origin in world tiles: pin coords arrive window-relative.
      rec = { cv: (meta && (meta.png || meta.b64)) ? await wmDecode(meta) : null,     // "{}" = genuinely no map data here
              icons: wmIcons(meta && meta.icons),
              ox: best.cx - best.half, oy: best.cy - best.half };
    } catch (e) {
      rec = null; wmFailAt = Date.now();
      // A static view only pumps from wmDraw; without this a transient bridge failure
      // would leave the coarse stand-in on screen until the next pan/zoom.
      setTimeout(wmKick, 1600);
    }
    wmInflight--; wmQueued.delete(best.key);
    if (rec) wmSet(best.key, rec);
    wmKick(); wmPump();
  }

  // ---- draw scheduling: one rAF per invalidation, idle when the tab is closed ----
  let wmRafOn = false;
  function wmKick() {
    if (wmRafOn) return; wmRafOn = true;
    (typeof requestAnimationFrame === 'function' ? requestAnimationFrame : function (cb) { setTimeout(cb, 16); })(function () {
      wmRafOn = false;
      if (!paneVisible('worldmap')) return;
      try { wmDraw(); } catch (e) {}
      if (wmFly) wmKick();
    });
  }

  // ---- icon images for canvas markers (item icons + cache sprites are data URLs) ----
  const wmImgs = new Map();        // url -> {img, ok}
  const wmSpritePend = new Set();
  function wmImg(url) {
    if (!url) return null;
    let r = wmImgs.get(url);
    if (!r) {
      r = { img: new Image(), ok: false };
      r.img.onload = function () { r.ok = true; wmKick(); };
      r.img.src = url;
      wmImgs.set(url, r);
    }
    return r.ok ? r.img : null;
  }
  function wmSpriteUrl(id) {
    if (!id || id <= 0) return '';
    if (SPRITES.has(id)) return SPRITES.get(id) || '';
    if (!wmSpritePend.has(id) && bridge() && bridge().sprite) {
      wmSpritePend.add(id);
      try {
        const r = bridge().sprite(id);
        if (r && typeof r.then === 'function')
          r.then(function (u) { SPRITES.set(id, (typeof u === 'string') ? u : ''); wmSpritePend.delete(id); wmKick(); },
                 function () { SPRITES.set(id, ''); wmSpritePend.delete(id); });
        else { SPRITES.set(id, (typeof r === 'string') ? r : ''); wmSpritePend.delete(id); }
      } catch (e) { SPRITES.set(id, ''); wmSpritePend.delete(id); }
    }
    return SPRITES.get(id) || '';
  }

  // ---- live state pulled on open ----
  let wmPlayer = null, wmFollow = false, wmSel = null, wmFly = null;
  let wmHideyVals = {};            // varp id -> value, for the 2-bit hidey-hole state fields
  let wmMarks = [];                // screen-space hover targets: {sx, sy, r, tip}
  // External pin groups (other panels plot sets of places, e.g. resource dungeons):
  // group -> [{x, y, p, nm, sub, col}]. Replaced whole per group; drawn under the selection pin.
  const wmExtPins = new Map();
  function wmSetExtPins(group, pins) { wmExtPins.set(group, Array.isArray(pins) ? pins : []); try { wmKick(); } catch (e) {} }
  let wmHover = { x: -1, y: -1 };  // cursor tile (status readout)
  async function wmOpen() {
    try { if (typeof fetchLodestones === 'function') fetchLodestones(true); } catch (e) {}
    try { if (typeof fetchMarkers === 'function') fetchMarkers(true); } catch (e) {}
    try { if (typeof clueMapTelePrefetch === 'function') clueMapTelePrefetch(); } catch (e) {}
    try {
      if (bridge() && bridge().varps && typeof HIDEY_VARPS !== 'undefined')
        wmHideyVals = JSON.parse(await bridge().varps(myPid(), HIDEY_VARPS.join(','))) || {};
    } catch (e) {}
    if (!wmCamSaved) {             // first ever open: land on the player rather than Varrock
      try { const P = await scanPlayerTile(); if (P) { wmCam.x = P.x + 0.5; wmCam.y = P.y + 0.5; wmCam.p = P.p | 0; } } catch (e) {}
    }
    wmKick();
  }
  // Player marker + follow. Cheap and self-guarding, so a plain interval is fine.
  let wmTickN = 0;
  setInterval(async function () {
    // typeof-guard: this can fire before the main script has run (paneVisible not yet defined)
    if (typeof paneVisible === 'undefined' || !paneVisible('worldmap') || !document.getElementById('wmStage')) return;
    // Teleport gate varbits (spellbook etc.) can change while the map is open; refresh
    // every ~5s so "wrong spellbook" clears right after switching books.
    if (++wmTickN % 10 === 0) { try { await clueMapTelePrefetch(); wmKick(); } catch (e) {} }
    try {
      const P = await scanPlayerTile();
      const moved = !!P !== !!wmPlayer || (P && wmPlayer && (P.x !== wmPlayer.x || P.y !== wmPlayer.y || P.p !== wmPlayer.p));
      wmPlayer = P;
      if (P && wmFollow) { wmCam.x = P.x + 0.5; wmCam.y = P.y + 0.5; if (wmCam.p !== (P.p | 0)) { wmCam.p = P.p | 0; wmPaintBar(); } wmSaveCam(); }
      if (moved || wmFollow) wmKick();
    } catch (e) {}
  }, 500);

  function wmSetPlane(p) { if (wmCam.p === p) return; wmCam.p = p; wmSaveCam(); wmPaintBar(); wmKick(); }
  // Screen-to-stage factor: 1 normally; under the Preferences content zoom the screen rect is
  // scaled while clientWidth stays in CSS px, which put the cursor off by the zoom factor.
  function wmZoomK(el, r) { const cw = el.clientWidth || 0; return (cw > 0 && r && r.width > 0) ? cw / r.width : 1; }
  // Pointer position in the stage's own CSS pixels. offsetX/Y come from the engine's hit test
  // (already in the target's coordinate space, whatever zoom / device scale is in force);
  // the rect arithmetic is only the fallback when the event targets a child element.
  // Effective CSS zoom on an element: the product of `zoom` up its ancestors (Preferences font
  // size zooms window bodies). The engine reports pointer offsets in SCREEN pixels inside a
  // zoomed subtree while layout works in the element's own CSS pixels, which put the hover
  // tile at ~92% of the cursor position at 12 px (owner screenshots); dividing by the zoom
  // maps them back.
  function wmZoomOf(el) {
    let z = 1;
    for (let n = el; n && n.nodeType === 1; n = n.parentNode) {
      const v = parseFloat(getComputedStyle(n).zoom);
      if (v && v > 0 && v !== 1) z *= v;
    }
    return z;
  }
  function wmPt(e, stage) {
    const Z = wmZoomOf(stage), t = e.target;
    if (t === stage && typeof e.offsetX === 'number') return { x: e.offsetX / Z, y: e.offsetY / Z };
    if (t && t.parentNode === stage && typeof e.offsetX === 'number') return { x: e.offsetX / Z + (t.offsetLeft || 0), y: e.offsetY / Z + (t.offsetTop || 0) };
    const r = stage.getBoundingClientRect();
    return { x: e.clientX / Z - r.left, y: e.clientY / Z - r.top };
  }
  function wmFlyTo(x, y, p, sel) {
    if (p != null && (p | 0) !== wmCam.p) wmSetPlane(p | 0);
    const tz = Math.max(wmCam.z, 2.5);
    wmFly = { t0: Date.now(), dur: 380, fx: wmCam.x, fy: wmCam.y, fz: wmCam.z, tx: x + 0.5, ty: y + 0.5, tz: tz };
    wmFollow = false; wmSel = sel || null;
    wmPaintBar(); wmKick();
  }

  // ---- search across every location source the panels already carry ----
  function wmScore(name, s) {
    const l = String(name || '').toLowerCase();
    if (l.startsWith(s)) return 0;
    if (l.split(/[\s\-\/(),']+/).some(function (w) { return w.startsWith(s); })) return 1;
    if (l.indexOf(s) >= 0) return 2;
    return -1;
  }
  // Search index: the static sources (labels, lodestones, teleports, hidey-holes, markers) are
  // folded ONCE into lowercase records; every keystroke used to rebuild their display strings
  // and lowercase every candidate name. Rebuilt only when the marker list changes.
  let wmIdx = null, wmIdxMarkSig = '';
  function wmIndex() {
    const msig = ((typeof markerList !== 'undefined') ? markerList.length + ':' + (markerList[0] ? markerList[0].label : '') : '') + (wmAreas ? '|A' : '');
    if (wmIdx && msig === wmIdxMarkSig) return wmIdx;
    const out = [];
    const add = (lc, rec) => { rec.lc = lc.toLowerCase(); rec.words = rec.lc.split(/[\s\-\/(),']+/); out.push(rec); };
    for (const d of MAP_LABELS) add(d.n, { ty: 'Place', nm: d.n, dt: d.x + ', ' + d.y + (d.p ? ' f' + d.p : ''), x: d.x, y: d.y, p: d.p | 0 });
    if (typeof LODESTONES !== 'undefined') for (const l of LODESTONES) add(l.n + ' lodestone', { ty: 'Lodestone', nm: l.n, dt: l.kb ? 'key ' + l.kb : '', x: l.x, y: l.y, p: l.p | 0 });
    for (const T of MAP_TELEPORTS) {
      const rq = teleRqText(T);
      const tkb = (typeof teleKeySeq === 'function') ? teleKeySeq(T) : teleKb(T);
      add(T.n, { ty: 'Teleport', nm: T.n, alt: String(T.src || '').toLowerCase(), dt: (T.src || '') + (tkb ? ' [' + tkb + ']' : '') + (rq ? ' - req ' + rq : ''), x: T.x, y: T.y, p: T.p | 0 });
    }
    if (typeof HIDEY !== 'undefined') for (const hh of HIDEY) add(hh.loc + ' ' + hh.n + ' hidey', { ty: 'Hidey-hole', nm: hh.loc, dt: hh.n + ' - ' + (HIDEY_TIERS[hh.t] || ''), x: hh.x, y: hh.y, p: hh.p | 0 });
    if (typeof markerList !== 'undefined') for (const m of markerList) {
      const gx = (m.region >> 8) * 64 + m.lx, gy = (m.region & 0xFF) * 64 + m.ly;
      const nm = m.label || ('Marker (' + gx + ', ' + gy + ')');
      add(nm + ' marker', { ty: 'Marker', nm: nm, dt: gx + ', ' + gy + (m.plane ? ' f' + m.plane : ''), x: gx, y: gy, p: m.plane | 0 });
    }
    if (wmAreas) for (const id in wmAreas) {
      const A = wmAreas[id]; if (!A.z || !A.z.length) continue;
      // Fly target: the centre of the area's source squares (world space), plane 0.
      let sxm = 0, sym = 0; for (const zz of A.z) { sxm += zz[1]; sym += zz[2]; }
      const cxw = Math.round(sxm / A.z.length * 64 + 32), cyw = Math.round(sym / A.z.length * 64 + 32);
      add((A.dn || A.n) + ' map', { ty: 'Map area', nm: A.dn || A.n, dt: A.z.length + ' squares', x: cxw, y: cyw, p: 0 });
    }
    wmIdx = out; wmIdxMarkSig = msig;
    return out;
  }
  function wmScoreLc(rec, s) {
    if (rec.lc.startsWith(s)) return 0;
    if (rec.words.some(w => w.startsWith(s))) return 1;
    if (rec.lc.indexOf(s) >= 0) return 2;
    return -1;
  }
  // Placements grouped per element once, so a query walks elements (hundreds) and only then
  // the placements of the elements that matched, instead of every placement in the world.
  let wmSymByMl = null, wmSymByMlN = 0;
  function wmSymGroups() {
    if (!WM_SYM) return null;
    if (wmSymByMl && wmSymByMlN === WM_SYM.length) return wmSymByMl;
    const m = new Map();
    for (const sy of WM_SYM) { let a = m.get(sy.ml); if (!a) { a = []; m.set(sy.ml, a); } a.push(sy); }
    wmSymByMl = m; wmSymByMlN = WM_SYM.length;
    return m;
  }
  function wmSearchEntries(q) {
    const s = q.trim().toLowerCase(), out = [];
    if (!s) return out;
    const co = s.match(/^(\d{1,5})[,\s]+(\d{1,5})(?:[,\s]+(\d))?$/);   // "3213 3429" or "x,y,plane"
    if (co) out.push({ sc: 0, ty: 'Tile', nm: '(' + co[1] + ', ' + co[2] + ')' + (co[3] ? ' floor ' + co[3] : ''), dt: 'go to coordinate', x: +co[1], y: +co[2], p: +(co[3] || 0) });
    for (const rec of wmIndex()) {
      let sc = wmScoreLc(rec, s);
      if (rec.alt) { const ss = rec.alt.startsWith(s) ? 0 : rec.alt.indexOf(s) >= 0 ? 2 : -1; if (ss >= 0 && (sc < 0 || ss + 1 < sc)) sc = ss + 1; }
      if (sc < 0) continue;
      out.push({ sc: sc, ty: rec.ty, nm: rec.nm, dt: rec.dt, x: rec.x, y: rec.y, p: rec.p });
    }
    // ---- map symbols: by TYPE ("fishing", "woodcutting", "bank") and by RESOURCE ("willow") ----
    // Grouped per element so one query does not return four hundred identical willow pins; the
    // nearest placement to the view centre wins and the rest are reported as a count.
    wmLoadSymbols();
    const groups = wmSymGroups();
    if (groups && WM_ML) {
      const best = new Map();
      for (const [ml, list] of groups) {
        const hay = wmSymText(ml);
        if (!hay || hay.indexOf(s) < 0) continue;
        let at = null, dBest = Infinity;
        for (const sy of list) { const d = Math.abs(sy.x - wmCam.x) + Math.abs(sy.y - wmCam.y); if (d < dBest) { dBest = d; at = sy; } }
        best.set(ml, { at: at, d: dBest, n: list.length });
      }
      for (const [ml, b] of best) {
        const e = WM_ML[ml];
        const cat = (WM_CAT && WM_CAT[e.c] && WM_CAT[e.c].n) || '';
        const r = wmElemRich(ml);
        // Score by the sharpest field that matched, so "willow" ranks the willow tree above a
        // generic "Trees" spot that merely lists one.
        const nm = (r && r.head) || (e.n ? String(e.n).split('<br>')[0].split(': ')[0] : '') || cat || 'Map symbol';
        let sc = wmScore(nm, s);
        if (sc < 0) sc = wmScore(cat, s);
        if (sc < 0) sc = 6;                                   // matched only deeper in the rows
        out.push({ sc: sc + 1, ty: cat || 'Symbol', nm: nm,
                   dt: b.at.x + ', ' + b.at.y + (b.at.p ? ' f' + b.at.p : '')
                       + (b.n > 1 ? '  -  ' + b.n + ' locations' : ''),
                   x: b.at.x, y: b.at.y, p: b.at.p | 0 });
      }
    }
    out.sort(function (a, b) { return (a.sc - b.sc) || (a.nm < b.nm ? -1 : 1); });
    return out.slice(0, 40);
  }
  let wmResSel = 0;
  function wmRenderResults() {
    const box = $('wmResults'), inp = $('wmSearch'); if (!box || !inp) return;
    const es = wmSearchEntries(inp.value || '');
    box._es = es;
    if (!es.length) { box.style.display = 'none'; box.innerHTML = ''; return; }
    if (wmResSel >= es.length) wmResSel = es.length - 1;
    box.style.display = '';
    box.innerHTML = es.map(function (e, i) {
      return '<div class="wm-res' + (i === wmResSel ? ' sel' : '') + '" data-i="' + i + '">'
        + '<span class="ty">' + e.ty + '</span><span class="nm">' + htmlEsc(e.nm) + '</span>'
        + (e.dt ? '<span class="dt">' + htmlEsc(e.dt) + '</span>' : '') + '</div>';
    }).join('');
  }
  function wmPickResult(i) {
    const box = $('wmResults'), inp = $('wmSearch');
    const es = (box && box._es) || [];
    const e = es[i]; if (!e) return;
    if (box) { box.style.display = 'none'; box.innerHTML = ''; }
    if (inp) inp.blur();
    wmFlyTo(e.x, e.y, e.p, { x: e.x, y: e.y, p: e.p | 0, nm: e.nm, dt: (e.ty === 'Tile' ? '' : e.ty + (e.dt ? ' - ' + e.dt : '')) });
  }

  // ---- overlay painters (screen space; each registers hover targets in wmMarks) ----
  // Marker keybinds come from teleKb() (panel_towers.js, next to the teleport data): the
  // explicit kb field, else the bracketed tokens many sheet rows carry in the name.
  // Icon draw that keeps the sprite's own aspect ratio, never upscales past native size,
  // and snaps to whole pixels - forcing a square box at fractional coords distorted them.
  function wmDrawIcon(cx, img, mx, my, box) {
    const w = img.naturalWidth || img.width || box, h = img.naturalHeight || img.height || box;
    const s = Math.min(1, box / Math.max(w, h));
    const dw = Math.max(1, Math.round(w * s)), dh = Math.max(1, Math.round(h * s));
    cx.drawImage(img, Math.round(mx - dw / 2), Math.round(my - dh / 2), dw, dh);
  }
  // Place-label styling, shared by both map surfaces.
  // A halo alone cannot hold up here: the basemap's brightness changes under every glyph, so
  // legibility swings wildly. Instead each label gets its own PLATE - a dark translucent pill
  // with no border - which fixes text contrast at a constant, high value while the plate
  // itself is dark enough to recede into the map. Type stays small and light so the plates
  // read as chrome, not content.
  function wmLabelPlate(cx, x, y, w, h, r, fill) {
    cx.beginPath();
    cx.moveTo(x + r, y); cx.lineTo(x + w - r, y); cx.quadraticCurveTo(x + w, y, x + w, y + r);
    cx.lineTo(x + w, y + h - r); cx.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
    cx.lineTo(x + r, y + h); cx.quadraticCurveTo(x, y + h, x, y + h - r);
    cx.lineTo(x, y + r); cx.quadraticCurveTo(x, y, x + r, y);
    cx.closePath(); cx.fillStyle = fill; cx.fill();
  }
  function wmLabelText(cx, text, x, y, hot, bx, by, bw, bh, dim) {
    cx.save();
    if (dim != null) cx.globalAlpha = dim;
    if (bw) {
      cx.shadowColor = 'rgba(0,0,0,0.45)'; cx.shadowBlur = 4; cx.shadowOffsetY = 1;
      wmLabelPlate(cx, bx, by, bw, bh, 3, hot ? 'rgba(10,26,24,0.86)' : 'rgba(9,12,17,0.72)');
      cx.shadowBlur = 0; cx.shadowOffsetY = 0;
      if (hot) {
        cx.lineWidth = 1; cx.strokeStyle = 'rgba(126,240,216,0.55)';
        wmLabelPlate(cx, bx + 0.5, by + 0.5, bw - 1, bh - 1, 3, 'rgba(0,0,0,0)');
        cx.stroke();
      }
    }
    cx.fillStyle = hot ? '#8ef0d8' : 'rgba(232,237,244,0.96)';
    cx.fillText(text, x, y);
    cx.restore();
  }
  // Small map badge (teleport keybind, "+N more"). Same plate language as the place
  // labels - dark translucent, rounded, no heavy fill - with a coloured hairline and
  // coloured type so it reads as chrome instead of a flat sticker. Returns its rect so
  // the label pass can route around it.
  function wmBadge(cx, text, cxp, topY, accent) {
    cx.save();
    cx.font = '700 9.5px system-ui, "Segoe UI", sans-serif';
    cx.textAlign = 'center'; cx.textBaseline = 'middle';
    const w = Math.ceil(cx.measureText(text).width) + 11, h = 14;
    const x = Math.round(cxp - w / 2), y = Math.round(topY);
    cx.shadowColor = 'rgba(0,0,0,0.5)'; cx.shadowBlur = 4; cx.shadowOffsetY = 1;
    wmLabelPlate(cx, x, y, w, h, 4, 'rgba(9,12,17,0.90)');
    cx.shadowBlur = 0; cx.shadowOffsetY = 0;
    cx.lineWidth = 1; cx.strokeStyle = accent.line;
    wmLabelPlate(cx, x + 0.5, y + 0.5, w - 1, h - 1, 4, 'rgba(0,0,0,0)');
    cx.stroke();
    cx.fillStyle = accent.text;
    cx.fillText(text, x + w / 2, y + h / 2 + 0.5);
    cx.restore();
    return { x: x, y: y, w: w, h: h };
  }
  const WM_ACC_KEY = { line: 'rgba(77,210,138,0.75)', text: '#8ef0c0' };   // keybind
  const WM_ACC_MORE = { get line() { return (typeof accentRgba === 'function') ? accentRgba(0.75) : 'rgba(var(--accent-rgb),0.75)'; }, get text() { return (typeof uiCfg === 'function') ? uiCfg().accent : '#cfc2ff'; } }; // "+N more"
  const wmTextW = new Map();   // label -> measured px width (font is constant)
  // Candidate offsets, in preference order: on the anchor, then the four sides, then the
  // diagonals. A label that collides used to just VANISH, which got much worse once the
  // teleport/lodestone icons started claiming space - the name you wanted was the one that
  // disappeared. Nudging it a plate-width away keeps it on the map; a moved plate gets a
  // small dot back at its true tile so it still reads as belonging there.
  const WM_CHIP_TRY = [[0, 0], [0, -1], [0, 1], [1, 0], [-1, 0], [1, -1], [-1, 1], [1, 1], [-1, -1]];
  // Returns the plate CENTRE {x,y} (so the caller can hit-test where the label actually
  // landed), or null when every candidate was blocked.
  function wmChip(cx, sxp, syp, text, placed, hot, w, h) {
    cx.font = '500 10.5px system-ui, "Segoe UI", sans-serif'; cx.textAlign = 'center'; cx.textBaseline = 'middle';
    try { cx.letterSpacing = '0.35px'; } catch (e) {}
    let tw = wmTextW.get(text);
    if (tw === undefined) { tw = Math.ceil(cx.measureText(text).width); wmTextW.set(text, tw); }
    const bw = tw + 9, bh = 14, stepX = bw / 2 + 8, stepY = bh + 4;
    for (let t = 0; t < WM_CHIP_TRY.length; t++) {
      const o = WM_CHIP_TRY[t];
      let px = sxp + o[0] * stepX, py = syp + o[1] * stepY;
      if (w) {                                  // keep edge-of-view places fully on screen
        px = Math.max(bw / 2 + 2, Math.min(px, w - bw / 2 - 2));
        py = Math.max(bh / 2 + 2, Math.min(py, h - bh / 2 - 2));
      }
      const x0 = px - bw / 2, y0 = py - bh / 2;
      if (placed.some(function (p) { return x0 < p.x + p.w + 2 && x0 + bw + 2 > p.x && y0 < p.y + p.h + 2 && y0 + bh + 2 > p.y; })) continue;
      placed.push({ x: x0, y: y0, w: bw, h: bh });
      if (t) {
        cx.save(); cx.fillStyle = 'rgba(232,237,244,0.55)';
        cx.beginPath(); cx.arc(sxp, syp, 1.6, 0, 6.2832); cx.fill(); cx.restore();
      }
      wmLabelText(cx, text, px, py, hot, x0, y0, bw, bh);
      return { x: px, y: py };
    }
    return null;
  }
  // ---- World-map areas (js5-23): the game's own composited map ----
  // wmAreas: area id -> {n, dn, zoom, bg, x0, y0, x1, y1, w, h, z:[[planes,sx,sy,dp,dx,dy]], r}.
  // The area images are what the in-game map shows: every mapsquare the game places on a map,
  // drawn from the same art, and NOTHING else (no instance spaces, no dream worlds, no staging
  // islands). We draw them back in WORLD space: each display square's 64x64 px block is
  // painted at its SOURCE square's world position, so every overlay (labels, symbols, player,
  // pins) keeps its world coordinates and dungeons sit at their true underground coords rather
  // than floating in the sea as the composed surface shows them. Squares absent from every
  // zone table stay void, which is the fix for the raw-grid rendering of instanced areas.
  let wmAreas = null, wmAreasAt = 0, wmZoneIdx = null;
  // The area images are 1 px per tile: native or downsampled at <= 1 px/tile, but upscaled mush
  // above it (owner: 'absolutely terrible' at 2 px/tile), so the chunk renderer takes over there.

  function wmLoadAreas() {
    if (wmAreas || !bridge() || !bridge().mapAreas) return;
    const t = Date.now(); if (t - wmAreasAt < 1500) return; wmAreasAt = t;
    (async () => {
      try {
        const d = JSON.parse(await bridge().mapAreas() || '{}');
        // Only trust a launcher that emits the 8-field (source -> display) rect records; the
        // first build misparsed archive 0 and its rects would place squares wrongly. Without
        // usable areas the map simply keeps the plain chunk renderer.
        const good = d && Object.keys(d).some(id => (d[id].r || []).some(r => r.length >= 8));
        if (good) {
          wmAreas = d;
          // Zone index: source square -> {a: area id, dx, dy}. The surface (28) claims a square
          // first; other areas fill in what the surface does not place.
          // Placement = the area's membership RECTS (archive 0, tile coords) at identity
          // positions, then the composite table (archive 1) overriding the relocated squares.
          // The zone table alone is only the relocations (dungeons floated into the sea);
          // treating it as the full placement wiped everything else off the map (Karamja).
          const idx = new Map();
          const order = Object.keys(d).sort((a, b) => (a === '28' ? -1 : b === '28' ? 1 : (+a) - (+b)));
          for (const id of order) {
            const A = d[id], sqW = A.w >> 6, sqH = A.h >> 6;
            const inImg = (dx, dy) => sqW > 0 && dx >= A.x0 && dx < A.x0 + sqW && dy >= A.y0 && dy < A.y0 + sqH;
            const zoneOf = new Map();
            for (const z of (A.z || [])) zoneOf.set((z[1] << 8) | z[2], z);
            for (const r of (A.r || [])) {
              // r = source rect -> display rect (tiles): a block moves as a whole, so a square's
              // display square is its source square shifted by the rect offset.
              const offX = (r.length >= 8 ? (r[4] >> 6) - (r[0] >> 6) : 0), offY = (r.length >= 8 ? (r[5] >> 6) - (r[1] >> 6) : 0);
              for (let qx = r[0] >> 6; qx <= (r[2] >> 6); qx++) for (let qy = r[1] >> 6; qy <= (r[3] >> 6); qy++) {
                const k = (qx << 8) | qy;
                if (idx.has(k)) continue;
                const z = zoneOf.get(k);
                const dx = z ? z[4] : qx + offX, dy = z ? z[5] : qy + offY;
                if (!inImg(dx, dy)) continue;                  // outside this area's image: not shown on it
                idx.set(k, { a: +id, dx: dx, dy: dy, pl: z ? z[0] : 0 });
              }
            }
            for (const z of (A.z || [])) {                     // relocations not covered by a rect
              const k = (z[1] << 8) | z[2];
              if (!idx.has(k) && inImg(z[4], z[5])) idx.set(k, { a: +id, dx: z[4], dy: z[5], pl: z[0] });
            }
          }
          wmZoneIdx = idx;
          wmKick();
        } else if (d && Object.keys(d).length) { wmAreasAt = Date.now() + 3600000; }   // old launcher: stop asking
      } catch (e) {}
    })();
  }
  // Does any mapsquare of a chunk (ch tiles wide at tile origin tx,ty) belong to a map area?
  function wmChunkPlaced(tx, ty, ch) {
    if (!wmZoneIdx) return true;                               // no data yet: draw everything
    const s0x = tx >> 6, s0y = ty >> 6, n = Math.max(1, ch >> 6);
    for (let i = 0; i < n; i++) for (let j = 0; j < n; j++) if (wmZoneIdx.has(((s0x + i) << 8) | (s0y + j))) return true;
    return false;
  }
  function wmDraw() {
    const stage = $('wmStage'), cv = $('wmCanvas'); if (!stage || !cv) return;
    wmLoadAreas();
    wmLoadElements();   // self-guarding: fetches the element tables once, then no-ops
    const w = stage.clientWidth | 0, h = stage.clientHeight | 0; if (!w || !h) return;
    // Back the canvas at DEVICE resolution: a CSS-resolution store gets stretched by display
    // scaling and everything (icons, text, terrain) renders soft. All drawing below stays in
    // CSS units via the transform; wmMarks hit-testing is CSS-space too, so nothing else moves.
    const dpr = (typeof devicePixelRatio === 'number' && devicePixelRatio > 0) ? devicePixelRatio : 1;
    const bwpx = Math.round(w * dpr), bhpx = Math.round(h * dpr);
    if (cv.width !== bwpx) cv.width = bwpx;
    if (cv.height !== bhpx) cv.height = bhpx;
    if (wmFly) {                                                // fly-to animation (ease-out cubic)
      const t = Math.min(1, (Date.now() - wmFly.t0) / wmFly.dur), e = 1 - Math.pow(1 - t, 3);
      wmCam.x = wmFly.fx + (wmFly.tx - wmFly.fx) * e;
      wmCam.y = wmFly.fy + (wmFly.ty - wmFly.fy) * e;
      wmCam.z = wmFly.fz + (wmFly.tz - wmFly.fz) * e;
      if (t >= 1) { wmFly = null; wmSaveCam(); }
    }
    const z = wmCam.z, plane = wmCam.p;
    const sx = function (tx) { return (tx - wmCam.x) * z + w / 2; };
    const sy = function (ty) { return (wmCam.y - ty) * z + h / 2; };
    const cx = cv.getContext('2d');
    cx.setTransform(dpr, 0, 0, dpr, 0, 0);
    cx.fillStyle = '#0b0d12'; cx.fillRect(0, 0, w, h);
    wmMarks = [];
    const vx0 = Math.floor(wmCam.x - w / 2 / z), vx1 = Math.ceil(wmCam.x + w / 2 / z);
    const vy0 = Math.floor(wmCam.y - h / 2 / z), vy1 = Math.ceil(wmCam.y + h / 2 / z);
    // terrain chunks (exact level, with the finest cached coarser level as a stand-in while
    // loading). Destination rects are snapped to shared integer edges so adjacent chunks
    // tile without hairline seams under the always-on bilinear smoothing.
    const L = wmLevel(z * dpr), ch = L.ch, Lidx = WM_LEVELS.indexOf(L);   // level picked in DEVICE px/tile so scaled displays stay native-sharp
    const ix0 = Math.max(0, Math.floor(vx0 / ch)), ix1 = Math.min(Math.floor(16383 / ch), Math.floor(vx1 / ch));
    const iy0 = Math.max(0, Math.floor(vy0 / ch)), iy1 = Math.min(Math.floor(16383 / ch), Math.floor(vy1 / ch));
    cx.imageSmoothingEnabled = true;
    try { cx.imageSmoothingQuality = 'high'; } catch (e) {}
    const ccx = (vx0 + vx1) / 2, ccy = (vy0 + vy1) / 2;
    let drawn = 0, emptyKnown = 0, loading = 0;
    // One renderer only: the detailed chunk renderer at every zoom (the game's 1 px/tile area
    // image is not used for terrain). Area data is used solely for PLACEMENT: which squares
    // any map shows.
    const imgMode = false;
    for (let ix = ix0; ix <= ix1; ix++) {
      for (let iy = iy0; iy <= iy1; iy++) {
        const dx0 = Math.round(sx(ix * ch)), dx1 = Math.round(sx((ix + 1) * ch));
        const dy0 = Math.round(sy((iy + 1) * ch)), dy1 = Math.round(sy(iy * ch));
        // A chunk with no placed square is not part of any map the game shows: void, and never
        // fetched (this is what put instance spaces and staging islands beside real land).
        if (!wmChunkPlaced(ix * ch, iy * ch, ch)) { emptyKnown++; continue; }
        const rec = wmGet(L.ts, plane, ix, iy);
        if (rec && rec.cv) { drawn++; cx.drawImage(rec.cv, dx0, dy0, dx1 - dx0, dy1 - dy0); continue; }
        if (!rec) { loading++; wmEnqueue(L.ts, plane, ix, iy, Math.abs(ix * ch + ch / 2 - ccx) + Math.abs(iy * ch + ch / 2 - ccy)); }
        if (rec && !rec.cv) { emptyKnown++; continue; }         // known-empty: leave the void dark
        for (let li = Lidx - 1; li >= 0; li--) {                // loading: nearest coarser cached level
          const B = WM_LEVELS[li], bch = B.ch;
          const bix = Math.floor(ix * ch / bch), biy = Math.floor(iy * ch / bch);
          const b = wmGet(B.ts, plane, bix, biy, true);
          if (b && b.cv) {
            const px = (ix * ch - bix * bch) * B.ts, py = (biy * bch + bch - ch - iy * ch) * B.ts;
            cx.drawImage(b.cv, px, py, ch * B.ts, ch * B.ts, dx0, dy0, dx1 - dx0, dy1 - dy0);
            break;
          }
        }
      }
    }
    wmPump();
    // Every chunk in view is KNOWN empty: say so instead of showing a silent black void. This is
    // the honest state for instanced spaces (uncharted isles, boss and skilling instances, POH),
    // which are assembled from template chunks at runtime and have no static map data.
    if (drawn === 0 && loading === 0 && emptyKnown > 0) {
      cx.save();
      cx.font = '600 13px ' + (getComputedStyle(document.body).fontFamily || 'sans-serif');
      cx.textAlign = 'center'; cx.textBaseline = 'middle';
      cx.fillStyle = 'rgba(238,240,245,0.75)';
      cx.fillText('No static map here', w / 2, h / 2 - 12);
      cx.font = '11.5px ' + (getComputedStyle(document.body).fontFamily || 'sans-serif');
      cx.fillStyle = 'rgba(170,176,191,0.7)';
      cx.fillText('Instanced area (uncharted isle, boss or skilling instance, house): built at runtime, not in the map cache.', w / 2, h / 2 + 8);
      cx.restore();
    }
    // World tile grid. Spacing ADAPTS rather than the grid vanishing when zoomed out: at a few
    // px/tile a per-tile lattice would be mostly lines, so the step grows to the next power of
    // two that clears ~7 px and those coarser lines draw fainter, reading as a reference grid
    // instead of noise. Drawn over the terrain but under every marker.
    if (wmLayer.grid) {
      let step = 1;
      while (step * z < 7) step *= 2;                 // in world tiles
      const gx0 = Math.floor(vx0 / step) * step, gy0 = Math.floor(vy0 / step) * step;
      cx.save();
      cx.strokeStyle = (step === 1) ? 'rgba(255,255,255,0.11)' : 'rgba(255,255,255,0.07)';
      cx.lineWidth = 1;
      cx.beginPath();
      for (let tx = gx0; tx <= vx1; tx += step) {
        const px = Math.round(sx(tx)) + 0.5;          // half-pixel: a 1px line lands on one row
        cx.moveTo(px, 0); cx.lineTo(px, h);
      }
      for (let ty = gy0; ty <= vy1; ty += step) {
        const py = Math.round(sy(ty)) + 0.5;
        cx.moveTo(0, py); cx.lineTo(w, py);
      }
      cx.stroke();
      // Region edges (64 tiles) stay readable at any zoom - they are the unit the coordinates
      // in a clue or a marker are actually grouped by.
      if (z >= 0.35) {
        cx.strokeStyle = 'rgba(120,190,255,0.22)';
        cx.beginPath();
        for (let tx = Math.floor(vx0 / 64) * 64; tx <= vx1; tx += 64) {
          const px = Math.round(sx(tx)) + 0.5; cx.moveTo(px, 0); cx.lineTo(px, h);
        }
        for (let ty = Math.floor(vy0 / 64) * 64; ty <= vy1; ty += 64) {
          const py = Math.round(sy(ty)) + 0.5; cx.moveTo(0, py); cx.lineTo(w, py);
        }
        cx.stroke();
      }
      cx.restore();
    }
    // hidey-holes (built state from the 2-bit varbit: 0 none, 1 built, 2 stocked)
    if (wmLayer.hidey && z >= 1.1 && typeof HIDEY !== 'undefined') {
      for (const hh of HIDEY) {
        if ((hh.p | 0) !== plane || hh.x < vx0 || hh.x > vx1 || hh.y < vy0 || hh.y > vy1) continue;
        const mx = sx(hh.x + 0.5), my = sy(hh.y + 0.5);
        const st = ((wmHideyVals[hh.vp] || 0) >>> hh.b) & 3;
        cx.lineWidth = 1.5; cx.strokeStyle = 'rgba(0,0,0,0.8)';
        cx.fillStyle = st === 2 ? '#46e0c0' : st === 1 ? 'rgba(70,224,192,0.45)' : 'rgba(120,130,150,0.55)';
        cx.beginPath(); cx.rect(mx - 5, my - 4, 10, 8); cx.fill(); cx.stroke();
        wmMarks.push({ sx: mx, sy: my, r: 7, tip: '<b>Hidey-hole</b> ' + htmlEsc(hh.loc) + '<br>' + htmlEsc(hh.n) + ' - ' + (HIDEY_TIERS[hh.t] || '') + '<br><span style="opacity:.65">' + (st === 2 ? 'built + stocked' : st === 1 ? 'built, empty' : 'not built') + ' - ' + hh.x + ', ' + hh.y + '</span>' });
      }
    }
    // user tile markers
    if (wmLayer.marks && typeof markerList !== 'undefined') {
      for (const m of markerList) {
        if ((m.plane | 0) !== plane) continue;
        const gx = (m.region >> 8) * 64 + m.lx, gy = (m.region & 0xFF) * 64 + m.ly;
        if (gx < vx0 || gx > vx1 || gy < vy0 || gy > vy1) continue;
        const s0 = Math.max(4, Math.min(14, z));
        cx.fillStyle = mkHex(mkParseColor(m.color)); cx.strokeStyle = 'rgba(0,0,0,0.8)'; cx.lineWidth = 1.2;
        cx.beginPath(); cx.rect(sx(gx + 0.5) - s0 / 2, sy(gy + 0.5) - s0 / 2, s0, s0); cx.fill(); cx.stroke();
        wmMarks.push({ sx: sx(gx + 0.5), sy: sy(gy + 0.5), r: s0 / 2 + 3, tip: '<b>' + htmlEsc(m.label || 'Tile marker') + '</b><br>' + gx + ', ' + gy });
      }
    }
    // Occupancy for the label pass. Icons and badges draw BEFORE the place labels, so
    // without this the labels would paint straight over them (the keybind badge was the
    // usual casualty). Seeding their rects here makes wmChip's existing overlap-culling
    // route labels around the icons instead of on top of them.
    const placed = [];
    // ---- world-map ELEMENT symbols (bank, altar, shortcut, fishing spot, dungeon, ...) ----
    // Drawn HERE rather than baked into the terrain PNG by the cache reader, which is what lets
    // them hover, hold a constant screen size at every zoom, and keep place labels off them.
    // Below ~1.5 px/tile the view is a whole continent and several hundred symbols is noise, so
    // they drop out alongside the other dense layers.
    if (wmLayer.icons && z >= 1.5 && WM_ML) {
      // The reader exports a symbol from EVERY plane (the game shows an upper-floor bank on the
      // ground view), so the same element can arrive several times on one tile. Collapse those.
      const seenPin = new Set();
      for (let ix = ix0; ix <= ix1; ix++) for (let iy = iy0; iy <= iy1; iy++) {
        const r0 = wmGet(L.ts, plane, ix, iy, true);
        if (!r0 || !r0.icons) continue;
        for (const p of r0.icons) {
          const gx = r0.ox + p.tx, gy = r0.oy + p.ty;
          if (gx < vx0 || gx > vx1 || gy < vy0 || gy > vy1) continue;
          const pinKey = gx + ',' + gy + ',' + p.ml;
          if (seenPin.has(pinKey)) continue;
          seenPin.add(pinKey);
          const e = WM_ML[p.ml];
          if (!e || e.s < 0) continue;
          const mx = sx(gx + 0.5), my = sy(gy + 0.5);
          // Try the preferred sprite, then the alternate: an id can resolve to an empty URL
          // (missing or undecodable in the sprite index) while the element's other candidate
          // is perfectly good, which left most symbols blank.
          let url = wmSpriteUrl(e.s);
          if (!url && e.s2 != null && e.s2 >= 0) url = wmSpriteUrl(e.s2);
          const img = url ? wmImg(url) : null;
          if (img) wmDrawIcon(cx, img, mx, my, 18);
          placed.push({ x: mx - 9, y: my - 9, w: 18, h: 18 });
          wmMarks.push({ sx: mx, sy: my, r: 10, ml: p.ml, id: p.id, x: gx, y: gy });
        }
      }
    }
    // teleport destinations (dense: only at close zoom, distance-clustered so stacks stay
    // readable - grid-cell bucketing split same-spot stacks landing across a cell border)
    if (wmLayer.teles && z >= 1.6) {
      const buckets = [];
      for (const T of MAP_TELEPORTS) {
        if ((T.p | 0) !== plane || T.x < vx0 || T.x > vx1 || T.y < vy0 || T.y > vy1) continue;
        const mx = sx(T.x + 0.5), my = sy(T.y + 0.5);
        let b = null;
        for (let i = 0; i < buckets.length; i++) {
          const q = buckets[i], dx = q.mx - mx, dy = q.my - my;
          if (dx * dx + dy * dy <= 289) { b = q; break; }   // within 17px of a group anchor
        }
        if (!b) { b = { mx: mx, my: my, ts: [] }; buckets.push(b); }
        b.ts.push(T);
      }
      for (const b of buckets) {
        // Stacked groups show EACH member's own icon (up to three, fanned around the
        // anchor) instead of one icon hiding the rest; bigger stacks add a +N badge.
        const show = b.ts.slice(0, 3), n = show.length, sp = 13;
        for (let i = 0; i < n; i++) {
          const T2 = show[i];
          cx.globalAlpha = !teleWhyCached(T2) ? 1 : 0.45;
          const url = T2.iconUrl || (T2.ico ? resolveIcon(T2.ico) : (T2.item ? resolveIcon(T2.item) : (T2.sp ? wmSpriteUrl(T2.sp) : '')));
          const img = url ? wmImg(url) : null;
          const ox = b.mx + (i - (n - 1) / 2) * sp;
          if (img) wmDrawIcon(cx, img, ox, b.my, n === 1 ? 20 : 17);
          else {
            cx.fillStyle = '#4dd28a'; cx.strokeStyle = '#0b0d12'; cx.lineWidth = 2;
            cx.beginPath(); cx.arc(ox, b.my, 5, 0, 6.2832); cx.fill(); cx.stroke();
          }
        }
        cx.globalAlpha = 1;
        const iw = n === 1 ? 20 : 17, half = (n - 1) / 2 * sp + iw / 2;
        placed.push({ x: b.mx - half, y: b.my - iw / 2, w: half * 2, h: iw });
        const kb = (typeof teleKeySeq === 'function') ? teleKeySeq(b.ts[0]) : teleKb(b.ts[0]);
        if (b.ts.length > 3)
          placed.push(wmBadge(cx, '+' + (b.ts.length - 3), b.mx + half + 2, b.my - iw / 2 - 4, WM_ACC_MORE));
        else if (b.ts.length === 1 && kb && z >= 2.4)
          placed.push(wmBadge(cx, kb, b.mx, b.my + iw / 2 + 1, WM_ACC_KEY));
        wmMarks.push({ sx: b.mx, sy: b.my, r: 10 + (n - 1) * sp / 2 + 3, ts: b.ts });   // tooltip built lazily on hover
      }
    }
    // lodestones (sparse: always drawn; grey until the unlock varbit says otherwise)
    if (wmLayer.lodes && typeof LODESTONES !== 'undefined') {
      const lookup = (typeof lodeData !== 'undefined' && lodeData) ? lodeData : null;
      for (const l of LODESTONES) {
        if ((l.p | 0) !== plane) continue;   // Prifddinas / City of Um are plane-1 destinations
        if (l.x < vx0 || l.x > vx1 || l.y < vy0 || l.y > vy1) continue;
        const mx = sx(l.x + 0.5), my = sy(l.y + 0.5);
        let unlocked = true;
        if (lookup) { const d = lookup.find(function (q) { return q.n === l.n; }); if (d) unlocked = !!d.unlocked; }
        cx.globalAlpha = unlocked ? 1 : 0.4;
        const img = z >= 0.7 ? wmImg(wmSpriteUrl(l.sp)) : null;
        if (img) wmDrawIcon(cx, img, mx, my, 22);
        else {
          const r0 = 6;
          cx.beginPath(); cx.moveTo(mx, my - r0); cx.lineTo(mx + r0, my); cx.lineTo(mx, my + r0); cx.lineTo(mx - r0, my); cx.closePath();
          cx.fillStyle = 'rgba(140,110,255,0.95)'; cx.fill();
          cx.lineWidth = 1.5; cx.strokeStyle = '#fff'; cx.stroke();
        }
        cx.globalAlpha = 1;
        placed.push({ x: mx - 11, y: my - 11, w: 22, h: 22 });
        wmMarks.push({ sx: mx, sy: my, r: 11, tip: '<b>' + htmlEsc(l.n) + ' lodestone</b><br><span style="opacity:.75">' + (l.kb ? 'key ' + htmlEsc(l.kb) + ' - ' : '') + (unlocked ? 'unlocked' : 'locked') + '</span>' });
      }
    }
    // place labels (nearest-to-centre first, overlap-culled like the clue maps)
    if (wmLayer.labels) {
      const inview = MAP_LABELS.filter(function (d) { return (d.p || 0) === plane && d.x >= vx0 && d.x <= vx1 && d.y >= vy0 && d.y <= vy1; });
      inview.sort(function (a, b) { return (Math.abs(a.x - ccx) + Math.abs(a.y - ccy)) - (Math.abs(b.x - ccx) + Math.abs(b.y - ccy)); });
      cx.save();
      for (const d of inview.slice(0, 34)) {
        // Hit-test where the plate LANDED, not the anchor: a nudged label is hoverable.
        const at = wmChip(cx, sx(d.x + 0.5), sy(d.y + 0.5), d.n, placed, false, w, h);
        if (at) wmMarks.push({ sx: at.x, sy: at.y, r: 8, tip: '<b>' + htmlEsc(d.n) + '</b><br>' + d.x + ', ' + d.y });
      }
      cx.restore();
    }
    // external pin groups
    for (const pins of wmExtPins.values()) {
      for (const pn of pins) {
        if ((pn.p | 0) !== plane) continue;
        const mx = sx(pn.x + 0.5), my = sy(pn.y + 0.5);
        if (mx < -20 || my < -20 || mx > w + 20 || my > h + 20) continue;
        cx.beginPath(); cx.arc(mx, my, 6.5, 0, 6.2832);
        cx.fillStyle = 'rgba(0,0,0,0.6)'; cx.fill();
        cx.lineWidth = 2.2; cx.strokeStyle = pn.col || '#f5b241'; cx.stroke();
        cx.beginPath(); cx.arc(mx, my, 2.2, 0, 6.2832); cx.fillStyle = pn.col || '#f5b241'; cx.fill();
        wmMarks.push({ sx: mx, sy: my, r: 9, tip: '<b>' + htmlEsc(pn.nm || 'Pin') + '</b>' + (pn.sub ? '<br>' + htmlEsc(pn.sub) : '') + '<br>' + pn.x + ', ' + pn.y });
      }
    }
    // selection pin
    if (wmSel && (wmSel.p | 0) === plane) {
      const mx = sx(wmSel.x + 0.5), my = sy(wmSel.y + 0.5);
      cx.lineCap = 'round';
      const gap = 9, len = 10;
      const tick = function (dx, dy) {
        cx.beginPath(); cx.moveTo(mx + dx * gap, my + dy * gap); cx.lineTo(mx + dx * (gap + len), my + dy * (gap + len));
        cx.lineWidth = 4; cx.strokeStyle = 'rgba(0,0,0,0.55)'; cx.stroke();
        cx.lineWidth = 2; cx.strokeStyle = 'rgba(255,255,255,0.95)'; cx.stroke();
      };
      tick(0, -1); tick(0, 1); tick(-1, 0); tick(1, 0);
      cx.lineWidth = 2.6; cx.strokeStyle = '#ff2d95'; cx.beginPath(); cx.arc(mx, my, 10, 0, 6.2832); cx.stroke();
      cx.lineWidth = 1.2; cx.strokeStyle = 'rgba(255,255,255,0.85)'; cx.beginPath(); cx.arc(mx, my, 12.5, 0, 6.2832); cx.stroke();
      if (wmSel.nm) { const placed = []; cx.save(); wmChip(cx, mx, my - 24, wmSel.nm, placed, true); cx.restore(); }
      wmMarks.push({ sx: mx, sy: my, r: 12, tip: '<b>' + htmlEsc(wmSel.nm || 'Pinned tile') + '</b><br>' + wmSel.x + ', ' + wmSel.y + '<br><span style="opacity:.65">' + (nearLabel(wmSel.x, wmSel.y, plane) || '') + '</span>' });
    }
    // player
    if (wmPlayer && (wmPlayer.p | 0) === plane) {
      const mx = sx(wmPlayer.x + 0.5), my = sy(wmPlayer.y + 0.5);
      if (mx >= -20 && mx <= w + 20 && my >= -20 && my <= h + 20) {
        cx.beginPath(); cx.arc(mx, my, 5.5, 0, 6.2832);
        cx.fillStyle = '#ffd23f'; cx.fill();
        cx.lineWidth = 2; cx.strokeStyle = '#fff'; cx.stroke();
        wmMarks.push({ sx: mx, sy: my, r: 8, tip: '<b>You</b><br>' + wmPlayer.x + ', ' + wmPlayer.y });
      }
    }
    wmPaintStatus();
    wmHoverBox();
  }

  function wmTeleTip(ts) {
    return ts.map(function (t2) {
      const why = teleWhyCached(t2);
      const ci = (typeof teleChargeInfo === 'function') ? teleChargeInfo(t2) : null;
      // green = usable now, red = a requirement is unmet (named below)
      const unk = (typeof teleTaskSetWhy === 'function') && teleTaskSetWhy(t2) === '?';
      const dot = '<span style="color:' + (why ? '#ff6b6b' : unk ? '#fbbf24' : '#4dd28a') + '">●</span> ';
      return dot + '<b>' + htmlEsc(t2.n) + '</b><br><span style="opacity:.75">' + htmlEsc(t2.src || '')
        + ((typeof teleKeySeq === 'function' ? teleKeySeq(t2) : teleKb(t2)) ? ' [' + htmlEsc(typeof teleKeySeq === 'function' ? teleKeySeq(t2) : teleKb(t2)) + ']' : '')
        + (ci && ci.used < ci.max ? '<br>daily teleports ' + ci.used + '/' + ci.max + ' · ' + teleResetIn() : '')
        + ((typeof teleInPassage === 'function' && teleInPassage(t2) && telePassage && telePassage.free)
             ? '<br>unlimited charges (passage' + ((typeof telePassageSlot === 'function' && telePassageSlot(t2)) ? ' slot ' + telePassageSlot(t2) : '') + ')'
             : (typeof teleItemChargeVal === 'function' && teleItemChargeVal(t2) !== null
                 ? '<br>' + teleItemChargeVal(t2).toLocaleString()
                   + (teleItemChargeMax(t2) ? '/' + teleItemChargeMax(t2) : '') + ' charges'
                   + ((typeof teleInPassage === 'function' && teleInPassage(t2)) ? ' (passage' + ((typeof telePassageSlot === 'function' && telePassageSlot(t2)) ? ' slot ' + telePassageSlot(t2) : '') + ')' : '') : ''))
        // One shared builder, so the requirement block cannot drift from the clue map's.
        + ((typeof teleReqBlock === 'function')
             ? teleReqBlock(t2, 'html').map(function (l) {
                 return '<br>' + (l === 'req: unverified' ? 'req: <span style="color:#fbbf24">unverified</span>' : l);
               }).join('') : '') + '</span>';
    }).join('<hr style="border:none;border-top:1px solid rgba(255,255,255,0.15);margin:3px 0">');
  }
  // A map symbol's tooltip, in the game's own shape: a caps CATEGORY header over a body line.
  // A horizontal icon strip: the shape for a set of drops/materials where the LEVEL is not the
  // point. Built from the same .tg-cell plate the collection-log grids use, so map tooltips and
  // panel tooltips share one vocabulary.
  function wmStripNode(g) {
    const s = document.createElement('div'); s.className = 'wm-strip';
    if (g.label) { const h = document.createElement('span'); h.className = 'wm-glab'; h.textContent = g.label; s.appendChild(h); }
    for (const it of g.items.slice(0, 4)) {
      const c = document.createElement('div'); c.className = 'tg-cell wm-cell';
      const u = resolveIcon(it.id);
      if (u) setIconBg(c, u);
      else { const t = document.createElement('span'); t.className = 'tg-tn';
             t.textContent = String(it.name || '?').slice(0, 2).toUpperCase(); c.appendChild(t); }
      s.appendChild(c);
    }
    if (g.items.length === 1 && g.items[0].name) {
      const n = document.createElement('span'); n.className = 'wm-inm'; n.textContent = g.items[0].name; s.appendChild(n);
    }
    const extra = g.more || Math.max(0, g.items.length - 4);
    if (extra) { const p = document.createElement('span'); p.className = 'wm-glab'; p.textContent = '+' + extra; s.appendChild(p); }
    return s;
  }
  const WM_ROW_CAP = 12;   // #wmStage is overflow:hidden -- an uncapped 20-row farming patch clips
  function wmRowsNode(m) {
    const box = document.createElement('div');
    box.className = 'wm-rows' + (m.wide ? ' wide' : '');
    for (const r of m.rows.slice(0, WM_ROW_CAP)) {
      const row = document.createElement('div'); row.className = 'wm-row';
      if (!r) { row.className += ' wait'; box.appendChild(row); continue; }
      const lv = document.createElement('div'); lv.className = 'wm-lv';
      const su = wmSkillSprite(r.sk);
      if (su) { const i = document.createElement('div'); i.className = 'wm-sk'; setIconBg(i, su); lv.appendChild(i); }
      const lt = document.createElement('span'); lt.textContent = (r.lvl == null) ? '' : ('Lvl ' + r.lvl);
      lv.appendChild(lt); row.appendChild(lv);
      const mid = document.createElement('div'); mid.className = 'wm-mid';
      if (r.name && (m.alwaysName || !m.one)) {
        const n = document.createElement('div'); n.className = 'wm-nm'; n.textContent = r.name; mid.appendChild(n);
      }
      if (r.groups) { for (const g of r.groups) if (g.items.length) mid.appendChild(wmStripNode(g)); }
      else if (r.obj) mid.appendChild(wmStripNode({ items: [{ id: r.obj, name: r.objName }], more: r.more }));
      else if (r.note) { const n = document.createElement('div'); n.className = 'wm-note'; n.textContent = r.note; mid.appendChild(n); }
      row.appendChild(mid); box.appendChild(row);
    }
    if (m.rows.length > WM_ROW_CAP) {
      const mo = document.createElement('div'); mo.className = 'tg-more';
      mo.textContent = '+ ' + (m.rows.length - WM_ROW_CAP) + ' more'; box.appendChild(mo);
    }
    return box;
  }
  let wmTipHit = null;
  function wmTipPaintElem(tip, hit) {
    const m = wmElemRich(hit.ml);
    tip.classList.toggle('wide', !!(m && m.wide));
    if (!m || (!m.head && !(m.rows || []).length)) { wmTipHit = null; tip.innerHTML = wmElemTipHtml(hit); return; }
    wmTipHit = hit;
    tip.innerHTML = '';
    const h = document.createElement('b'); h.className = 'wm-h';
    h.textContent = m.head || wmLocName(hit) || 'Map symbol';
    tip.appendChild(h);
    if (m.desc) { const d = document.createElement('div'); d.className = 'wm-desc'; d.textContent = m.desc; tip.appendChild(d); }
    if (m.facts) for (const f of m.facts) {
      const fr = document.createElement('div'); fr.className = 'wm-fact';
      const a = document.createElement('span'); a.textContent = f[0];
      const b = document.createElement('span'); b.className = 'v'; b.textContent = f[1];
      fr.appendChild(a); fr.appendChild(b); tip.appendChild(fr);
    }
    {
      if (WM_ML[hit.ml] && WM_ML[hit.ml].c === WM_TRANSPORT_CAT) {
        const rt = wmRouteTipLines(hit.x, hit.y);
        if (rt) { const d = document.createElement('div'); d.innerHTML = rt; tip.appendChild(d); }
      }
      const lk = wmElemLink(hit.ml, hit);
      if (lk) {
        const fr = document.createElement('div'); fr.className = 'wm-fact'; fr.style.marginTop = '4px';
        const a = document.createElement('span'); a.textContent = lk.guess ? ('Click to view ' + (lk.y >= 6400 ? 'underground' : 'the surface')) : 'Click to follow';
        const b = document.createElement('span'); b.className = 'v'; b.textContent = lk.x + ', ' + lk.y + (lk.p ? ' f' + lk.p : '');
        fr.appendChild(a); fr.appendChild(b); tip.appendChild(fr);
      }
    }
    // Requirements sit between the header and the resource rows, as they do in game. Satisfied
    // entries are dimmed rather than hidden - the game marks them <str=FFFFFE> and still shows
    // them, because "what does this need" is the question even once you meet it.
    if (m.reqs && m.reqs.length) {
      const rq = document.createElement('div'); rq.className = 'wm-reqs';
      const rh = document.createElement('div'); rh.className = 'wm-reqh';
      rh.textContent = 'Requirements:'; rq.appendChild(rh);
      for (const r of m.reqs) {
        const li = document.createElement('div');
        li.className = 'wm-req' + (r.ok === true ? ' met' : r.ok === false ? ' unmet' : '');
        li.textContent = r.text;
        rq.appendChild(li);
      }
      tip.appendChild(rq);
    }
    if (m.rows && m.rows.length) tip.appendChild(wmRowsNode(m));
    // Genuine icon SETS go through the collection-log grid builder rather than a bespoke one.
    // Note buildTipGrids, NOT setTipGrids: the latter targets the global #global-tip, and the
    // map owns its own #wmTip.
    if (m.groups && typeof buildTipGrids === 'function') {
      const live = m.groups.filter(function (g) { return g.items.length; });
      if (live.length) tip.appendChild(buildTipGrids(live));
    }
    const c = document.createElement('div'); c.className = 'wm-xy'; c.textContent = hit.x + ', ' + hit.y;
    tip.appendChild(c);
  }
  // A build that had to go to the cache repaints in place once the data lands, but only while
  // the pointer is still on the same symbol.
  function wmTipRefresh() {
    const tip = document.getElementById('wmTip');
    if (!tip || tip.style.display === 'none' || !wmTipHit) return;
    wmTipPaintElem(tip, wmTipHit);
  }
  // Last-resort header: the name of the loc that PLACED this pin. 22 elements have no name by any
  // other route (no param 4149, no op-3 text, no category in enum 8586) and would otherwise all
  // read "MAP SYMBOL" - 597 pins. This names 142 of them and gets none wrong. The rest stay
  // placeholder because their locs are genuinely nameless in the cache, not because of this code.
  function wmLocName(m) {
    return (m && WM_LOCNM && WM_LOCNM[m.id]) || '';
  }
  // ---- Transportation routes (curated) ----
  // Topology (which stops a network connects) is curated from the RuneScape Wiki; every
  // COORDINATE shown is the game's map-icon placement: a stop carries a confirmed position and
  // binds only when a Transportation (or Fairy ring) icon sits within 6 tiles of it; there is
  // no name-based guessing (that picked the wrong icon north of Edgeville for the canoe).
  const WM_ROUTES = {"source":"Game world coordinates of each stop (boarding points and destinations) with wiki-verified topology and requirements.","networks":[{"id":"charter","name":"Charter ship","kind":"all-to-all","wiki":"https://runescape.wiki/w/Charter_ship","stops":[{"name":"Port Sarim","alt":[],"note":"","coord":[3042,3191,0],"src":"game"},{"name":"Catherby","alt":[],"note":"","coord":[2794,3408,0],"src":"game"},{"name":"Port Tyras","alt":[],"note":"Regicide","coord":[2145,3122,0],"src":"game"},{"name":"Brimhaven","alt":[],"note":"","coord":[2760,3238,0],"src":"game"},{"name":"Port Khazard","alt":[],"note":"","coord":[2674,3146,0],"src":"game"},{"name":"Oo'glog","alt":[],"note":"As a First Resort","coord":[2621,2857,0],"src":"game"},{"name":"Musa Point","alt":[],"note":"","coord":[2954,3158,0],"src":"game"},{"name":"Shipyard","alt":[],"note":"partial Monkey Madness","coord":[3001,3033,0],"src":"game"},{"name":"Port Phasmatys","alt":[],"note":"Morytania access (Priest in Peril)","coord":[3702,3502,0],"src":"game"},{"name":"Menaphos","alt":[],"note":"The Jack of Spades","coord":[3144,2662,0],"src":"game"}],"links":[]},{"id":"glider","name":"Gnome glider","kind":"all-to-all","wiki":"https://runescape.wiki/w/Gnome_glider","stops":[{"name":"Tree Gnome Stronghold","alt":[],"note":"The Grand Tree","coord":[2464,3502,3],"src":"game"},{"name":"White Wolf Mountain","alt":[],"note":"","coord":[2850,3493,1],"src":"game"},{"name":"Al Kharid","alt":[],"note":"","coord":[3283,3212,0],"src":"game"},{"name":"Karamja","alt":[],"note":"","coord":[2970,2973,0],"src":"game"},{"name":"Feldip Hills","alt":[],"note":"One Small Favour progress","coord":[2545,2972,0],"src":"game"},{"name":"Tree Gnome Village","alt":[],"note":"The Prisoner of Glouphrie","coord":[2496,3190,0],"src":"game"},{"name":"Prifddinas","alt":[],"note":"Plague's End","coord":[2207,3452,1],"src":"game"},{"name":"Tuai Leit","alt":[],"note":"rescue Azalea Oakheart (The Arc)","coord":[1773,11919,0],"src":"game"},{"name":"Digsite","alt":[],"note":"crash landing: arrival only, no flights back","coord":[3319,3438,0],"src":"game"}],"links":[]},{"id":"spirit_tree","name":"Spirit tree","kind":"all-to-all","wiki":"https://runescape.wiki/w/Spirit_tree","stops":[{"name":"Tree Gnome Village","alt":[],"note":"Tree Gnome Village quest","coord":[2542,3169,0],"src":"game"},{"name":"Tree Gnome Stronghold","alt":[],"note":"The Grand Tree","coord":[2462,3444,0],"src":"game"},{"name":"Battlefield of Khazard","alt":[],"note":"","coord":[2557,3259,0],"src":"game"},{"name":"Grand Exchange","alt":[],"note":"","coord":[3187,3507,0],"src":"game"},{"name":"South Feldip Hills","alt":[],"note":"","coord":[2416,2851,0],"src":"game"},{"name":"Port Sarim","alt":[],"note":"player-grown","coord":[3058,3257,0],"src":"game"},{"name":"Etceteria","alt":[],"note":"player-grown","coord":[2613,3855,0],"src":"game"},{"name":"Brimhaven","alt":[],"note":"player-grown","coord":[2800,3203,0],"src":"game"},{"name":"Poison Waste","alt":[],"note":"The Path of Glouphrie","coord":[2338,3109,0],"src":"game"},{"name":"Prifddinas","alt":[],"note":"Plague's End + three player-grown trees","coord":[2275,3371,1],"src":"game"}],"links":[]},{"id":"fairy_ring","name":"Fairy ring","kind":"all-to-all","cat":"fairy","wiki":"https://runescape.wiki/w/Fairy_ring","stops":[{"name":"AIP - Zanaris","alt":[],"note":"A Fairy Tale II started","coord":[2412,4434,0],"src":"game"},{"name":"AIQ - Asgarnia: Mudskipper Point","alt":[],"note":"","coord":[2996,3114,0],"src":"game"},{"name":"AIR - Islands: South of Witchhaven","alt":[],"note":"","coord":[2700,3247,0],"src":"game"},{"name":"AIS - Other realms: Naragi homeworld","alt":[],"note":"special access","coord":[2030,5982,0],"src":"game"},{"name":"AJQ - Dungeons: Dark cave south of Dorgesh-Kaan","alt":[],"note":"Death to the Dorgeshuun","coord":[2735,5221,0],"src":"game"},{"name":"AJR - Kandarin: Slayer cave south-east of Relekka","alt":[],"note":"","coord":[2780,3613,0],"src":"game"},{"name":"AJS - Islands: Penguins near Miscellania","alt":[],"note":"special access","coord":[2500,3896,0],"src":"game"},{"name":"AKQ - Piscatoris Hunter area","alt":[],"note":"","coord":[2319,3619,0],"src":"game"},{"name":"AKS - Feldip Hills: Jungle Hunter area","alt":[],"note":"","coord":[2571,2956,0],"src":"game"},{"name":"ALP - Feldip Hills: Near Gu\u00b4Tanoth","alt":[],"note":"partial A Fairy Tale III","coord":[2468,4189,0],"src":"game"},{"name":"ALQ - Morytania: Haunted Woods east of Canifis","alt":[],"note":"","coord":[3597,3495,0],"src":"game"},{"name":"ALR - Other realmms: Abyss","alt":[],"note":"special access","coord":[3059,4875,0],"src":"game"},{"name":"ALS - Kandarin: McGrubor\u00b4s Wood","alt":[],"note":"","coord":[2644,3495,0],"src":"game"},{"name":"BIP - Islands: Polypore Dungeon","alt":[],"note":"","coord":[3410,3324,0],"src":"game"},{"name":"BIQ - Kharidian Desert: Near Kalphite Hive","alt":[],"note":"","coord":[3251,3095,0],"src":"game"},{"name":"BIS - Sparse Plane","alt":[],"note":"special access","coord":[2455,4396,0],"src":"game"},{"name":"BIS - Kandarin: Ardougne Zoo unicorns","alt":[],"note":"special access","coord":[2635,3266,0],"src":"game"},{"name":"BJP - Fort Forinthry","alt":[],"note":"Grove tier 2","coord":[3347,3540,0],"src":"game"},{"name":"BJQ - Dungeons: Ancient Cavern","alt":[],"note":"Barbarian Training","coord":[1737,5342,0],"src":"game"},{"name":"BJR - Other realms: Realm of the fisher king","alt":[],"note":"Holy Grail","coord":[2650,4730,0],"src":"game"},{"name":"BJS - The Lost Grove","alt":[],"note":"rebuild with bittercap mushrooms","coord":[1359,5635,0],"src":"game"},{"name":"BKP - Feldip Hills: South of Castle Wars","alt":[],"note":"","coord":[2385,3035,0],"src":"game"},{"name":"BKQ - Other realms: Enchanted Valley","alt":[],"note":"","coord":[3041,4532,0],"src":"game"},{"name":"BKR - Morytania: Mort Myre, south of Canifis","alt":[],"note":"","coord":[3469,3431,0],"src":"game"},{"name":"BLP - Dungeons: TzHaar area","alt":[],"note":"","coord":[4622,5147,0],"src":"game"},{"name":"BLR - Kandarin: Legends' Guild","alt":[],"note":"","coord":[2740,3351,0],"src":"game"},{"name":"CIP - Islands: Miscellania","alt":[],"note":"The Fremennik Trials","coord":[2513,3884,0],"src":"game"},{"name":"CIQ - Kandarin: North-west of Yanille","alt":[],"note":"","coord":[2528,3127,0],"src":"game"},{"name":"CIS - Other realms: ScapeRune (Evil Bob\u00b4s island)","alt":[],"note":"special access","coord":[3419,4772,0],"src":"game"},{"name":"CJR - Kandarin: Sinclair Mansion (east)","alt":[],"note":"","coord":[2705,3576,0],"src":"game"},{"name":"CJS - Karamja: Kharazi Jungle","alt":[],"note":"Legends' Quest started","coord":[2901,2930,0],"src":"game"},{"name":"CKP - Other realms: Cosmic entity\u00b4s plane","alt":[],"note":"special access","coord":[2075,4848,0],"src":"game"},{"name":"CKQ - Menaphos: Imperial District","alt":[],"note":"","coord":[3086,2704,0],"src":"game"},{"name":"CKR - Karamja: South of Tai Bwo Wannai Village","alt":[],"note":"","coord":[2801,3003,0],"src":"game"},{"name":"CKS - Morytania: Canifis","alt":[],"note":"","coord":[3447,3470,0],"src":"game"},{"name":"CLP - Islands: South of Draynor Village","alt":[],"note":"special access","coord":[3082,3206,0],"src":"game"},{"name":"CLS - Islands: Jungle spiders near Yanille","alt":[],"note":"","coord":[2682,3081,0],"src":"game"},{"name":"CLR - Islands: Ape Atoll","alt":[],"note":"partial A Fairy Tale III","coord":[2735,2742,0],"src":"game"},{"name":"DIP - Islands: Mos Le\u00b4Harmless","alt":[],"note":"partial A Fairy Tale III","coord":[3763,2930,0],"src":"game"},{"name":"DIR - Other realms: Gorak`s Plane","alt":[],"note":"special access","coord":[3038,5348,0],"src":"game"},{"name":"DIR AKS - Kethsi","alt":[],"note":"","coord":[4026,5699,0],"src":"game"},{"name":"DIS - Misthalin: Wizard\u00b4s Tower","alt":[],"note":"","coord":[3092,3137,0],"src":"game"},{"name":"DJP - Kandarin: Tower of Life","alt":[],"note":"","coord":[2658,3230,0],"src":"game"},{"name":"DJR - Kandarin: Sinclair Mansion (west)","alt":[],"note":"","coord":[2676,3587,0],"src":"game"},{"name":"DJS - Tirannwn: Prifddinas (Clan Amlodd)","alt":[],"note":"Plague's End","coord":[2130,3369,0],"src":"game"},{"name":"DKP - Karamja: South of Musa Point","alt":[],"note":"","coord":[2900,3111,0],"src":"game"},{"name":"DKQ - Dungeons: Glacor Cave","alt":[],"note":"Ritual of the Mahjarrat","coord":[4183,5726,0],"src":"game"},{"name":"DKR - Misthalin: Edgeville","alt":[],"note":"","coord":[3129,3496,0],"src":"game"},{"name":"DKS - Kandarin: Snowy Hunter area","alt":[],"note":"","coord":[2744,3719,0],"src":"game"},{"name":"DLP - Havenhythe: North of Amberfell","alt":[],"note":"partial Secrets of Amberfell","coord":[3742,1601,0],"src":"game"},{"name":"DLQ - Kharidian Desert: North of Nardah","alt":[],"note":"","coord":[3423,3016,0],"src":"game"},{"name":"DLR - Islands: Poison Waste south of Isafdar","alt":[],"note":"special access","coord":[2213,3099,0],"src":"game"},{"name":"DLS - Dungeons: Myreque Hideout under The Hollows","alt":[],"note":"In Search of the Myreque","coord":[3501,9821,3],"src":"game"},{"name":"RESISTANCE - Fairy Resistance HQ","alt":[],"note":"","coord":[2254,4426,0],"src":"game"},{"name":"BIR DIP CLR ALP - Ork\u00b4s Rift","alt":[],"note":"","coord":[1626,4176,0],"src":"game"},{"name":"BLQ - Yu\u00b4biusk","alt":[],"note":"special access","coord":[2229,4244,1],"src":"game"},{"name":"BKS - Inanna's shrine","alt":[],"note":"Visions of Havenhythe","coord":[3599,1410,0],"src":"game"}],"links":[]},{"id":"arc","name":"Arc ferries (Quartermaster Gully)","kind":"all-to-all","wiki":"https://runescape.wiki/w/Quartermaster_Gully","stops":[{"name":"Port Sarim","alt":[],"note":"Impressing the Locals","coord":[3054,3247,0],"src":"game"},{"name":"Menaphos","alt":[],"note":"","coord":[3231,2664,0],"src":"game"},{"name":"Tuai Leit","alt":[],"note":"","coord":[1760,12010,0],"src":"game"},{"name":"Whale's Maw","alt":[],"note":"","coord":[2012,11781,0],"src":"game"},{"name":"Waiko","alt":[],"note":"","coord":[1809,11652,0],"src":"game"},{"name":"Turtle Islands","alt":[],"note":"","coord":[2245,11423,0],"src":"game"},{"name":"Aminishi","alt":[],"note":"","coord":[2061,11270,0],"src":"game"},{"name":"Cyclosis","alt":[],"note":"","coord":[2251,11182,0],"src":"game"},{"name":"Goshima","alt":[],"note":"","coord":[2448,11593,0],"src":"game"}],"links":[]},{"id":"kags","name":"Menaphos ferry (Portmaster Kags)","kind":"hub","wiki":"https://runescape.wiki/w/Portmaster_Kags","stops":[{"name":"Menaphos","alt":[],"note":"Crocodile Tears","coord":[3122,2631,0],"src":"game"},{"name":"Sunken Pyramid","alt":[],"note":"","coord":[3032,2672,0],"src":"game"},{"name":"Crondis' Pyramid","alt":[],"note":"","coord":[3273,2644,0],"src":"game"},{"name":"Jaldraocht","alt":[],"note":"one-way","coord":[3254,2882,0],"src":"game"},{"name":"Nardah","alt":[],"note":"one-way","coord":[3373,2931,0],"src":"game"},{"name":"Pollnivneach","alt":[],"note":"one-way","coord":[3377,2962,0],"src":"game"},{"name":"Dominion Tower","alt":[],"note":"one-way","coord":[3373,3080,0],"src":"game"}],"links":[["Menaphos","Sunken Pyramid"],["Menaphos","Crondis' Pyramid"],["Menaphos","Jaldraocht"],["Menaphos","Nardah"],["Menaphos","Pollnivneach"],["Menaphos","Dominion Tower"]]},{"id":"canoe","name":"Canoe","kind":"chain","wiki":"https://runescape.wiki/w/Canoe","stops":[{"name":"Lumbridge","alt":[],"note":"hatchet; Woodcutting 12-57 by canoe"},{"name":"Champions' Guild","alt":[],"note":""},{"name":"Barbarian Village","alt":["Gunnarsgrunn"],"note":"","coord":[3112,3409,0],"src":"game"},{"name":"Edgeville","alt":[],"note":"","coord":[3131,3509,0],"src":"game"},{"name":"Wilderness","alt":["Wilderness Pond"],"note":"Waka only (57 Woodcutting); one-way"}],"links":[["Lumbridge","Champions' Guild"],["Champions' Guild","Barbarian Village"],["Barbarian Village","Edgeville"],["Edgeville","Wilderness"]]},{"id":"carpet","name":"Magic carpet","kind":"all-to-all","wiki":"https://runescape.wiki/w/Magic_carpet","stops":[{"name":"Shantay Pass","alt":["South of Shantay Pass"],"note":"members","coord":[3312,3107,0],"src":"game"},{"name":"North Pollnivneach","alt":["Pollnivneach"],"note":"","coord":[3352,3001,0],"src":"game"},{"name":"South Pollnivneach","alt":["Pollnivneach"],"note":"","coord":[3346,2943,0],"src":"game"},{"name":"Nardah","alt":[],"note":"","coord":[3399,2920,0],"src":"game"},{"name":"Bedabin Camp","alt":[],"note":"","coord":[3184,3042,0],"src":"game"},{"name":"Uzer","alt":[],"note":"The Golem","coord":[3469,3111,0],"src":"game"},{"name":"Menaphos","alt":[],"note":"Icthlarin's Little Helper"},{"name":"Sophanem","alt":[],"note":"Icthlarin's Little Helper"},{"name":"Monkey colony","alt":[],"note":"Do No Evil; South Pollnivneach and Shantay Pass only"}],"links":[]},{"id":"balloon","name":"Hot air balloon","kind":"all-to-all","wiki":"https://runescape.wiki/w/Hot_air_balloon","stops":[{"name":"Entrana","alt":[],"note":"Enlightened Journey; 20 Firemaking"},{"name":"Taverley","alt":[],"note":"20 Firemaking"},{"name":"Crafting Guild","alt":[],"note":"30 Firemaking","coord":[2922,3301,0],"src":"game"},{"name":"Varrock","alt":[],"note":"40 Firemaking"},{"name":"Castle Wars","alt":[],"note":"50 Firemaking","coord":[2461,3108,0],"src":"game"},{"name":"Grand Tree","alt":["Tree Gnome Stronghold"],"note":"60 Firemaking","coord":[2479,3459,0],"src":"game"}],"links":[]},{"id":"minecart","name":"Keldagrim minecart","kind":"hub","wiki":"https://runescape.wiki/w/Keldagrim_minecart_system","stops":[{"name":"Keldagrim","alt":[],"note":"must have visited Keldagrim"},{"name":"Grand Exchange","alt":[],"note":""},{"name":"White Wolf Mountain","alt":["Dwarven Tunnel"],"note":"Fishing Contest + The Giant Dwarf"},{"name":"Ice Mountain","alt":["Dwarven Mine"],"note":""}],"links":[["Keldagrim","Grand Exchange"],["Keldagrim","White Wolf Mountain"],["Keldagrim","Ice Mountain"]]},{"id":"train","name":"Dorgesh-Kaan to Keldagrim train","kind":"pairs","wiki":"https://runescape.wiki/w/Dorgesh-Kaan%E2%80%93Keldagrim_train_system","stops":[{"name":"Dorgesh-Kaan","alt":[],"note":"Another Slice of H.A.M."},{"name":"Keldagrim","alt":[],"note":""}],"links":[["Dorgesh-Kaan","Keldagrim"]]},{"id":"eagle","name":"Eagle transport","kind":"hub","wiki":"https://runescape.wiki/w/Eagle_transport_system","stops":[{"name":"Eagles' Peak","alt":["Eagles' Peak Cave"],"note":"Eagles' Peak quest; rope"},{"name":"Rellekka Hunter area","alt":["Trollweiss"],"note":"35 Agility"},{"name":"Feldip Hills","alt":["Feldip Hunter area"],"note":"grow young vine"},{"name":"Uzer","alt":[],"note":"45 Strength"},{"name":"Karamja","alt":["Jade Vine Maze"],"note":"maze access"}],"links":[["Eagles' Peak","Rellekka Hunter area"],["Eagles' Peak","Feldip Hills"],["Eagles' Peak","Uzer"],["Eagles' Peak","Karamja"]]},{"id":"ships","name":"Ships and ferries","kind":"pairs","wiki":"https://runescape.wiki/w/Boat_network","stops":[{"name":"Port Sarim","alt":[],"note":"","coord":[3029,3219,0],"src":"game"},{"name":"Musa Point","alt":["Karamja"],"note":"30 coins","coord":[2958,3143,0],"src":"game"},{"name":"Entrana","alt":[],"note":"no weapons or armour","coord":[2834,3334,0],"src":"game"},{"name":"Crandor","alt":[],"note":"Dragon Slayer only"},{"name":"Void Knights' Outpost","alt":["Pest Control"],"note":"free"},{"name":"East Ardougne","alt":["Ardougne"],"note":"","coord":[2682,3273,0],"src":"game"},{"name":"Brimhaven","alt":[],"note":"30 coins"},{"name":"Tree Gnome Stronghold","alt":[],"note":""},{"name":"Piscatoris","alt":["Piscatoris Fishing Colony"],"note":""},{"name":"Tai Bwo Wannai","alt":[],"note":"ogre boat","coord":[2764,2956,0],"src":"game"},{"name":"Feldip Hills","alt":[],"note":"ogre boat","coord":[2654,2964,0],"src":"game"},{"name":"Rellekka","alt":[],"note":"","coord":[2628,3692,0],"src":"game"},{"name":"Miscellania","alt":[],"note":"The Fremennik Trials","coord":[2580,3846,0],"src":"game"},{"name":"Etceteria","alt":[],"note":"The Fremennik Trials"},{"name":"Waterbirth Island","alt":[],"note":"Jarvald","coord":[2550,3758,0],"src":"game"},{"name":"Jatizso","alt":[],"note":"","coord":[2421,3780,0],"src":"game"},{"name":"Neitiznot","alt":[],"note":"","coord":[2311,3779,0],"src":"game"},{"name":"Pirates' Cove","alt":[],"note":"The Fremennik Trials","coord":[2209,3794,0],"src":"game"},{"name":"Lunar Isle","alt":[],"note":""},{"name":"Iceberg","alt":[],"note":""},{"name":"Port Phasmatys","alt":[],"note":"","coord":[3703,3503,0],"src":"game"},{"name":"Mos Le'Harmless","alt":[],"note":"Cabin Fever","coord":[3683,2951,0],"src":"game"},{"name":"Dragontooth Island","alt":[],"note":""},{"name":"Burgh de Rott","alt":[],"note":""},{"name":"Meiyerditch","alt":[],"note":"The Darkness of Hallowvale started"},{"name":"The Hollows","alt":[],"note":""},{"name":"Mort'ton","alt":[],"note":""},{"name":"Taverley","alt":[],"note":"free-to-play"},{"name":"Al Kharid","alt":[],"note":"free-to-play"},{"name":"Daemonheim","alt":[],"note":"free"},{"name":"Digsite","alt":["Varrock Dig Site","Dig Site"],"note":"The Stormbreaker"},{"name":"Anachronia","alt":[],"note":"first trip: base camp tutorial"}],"links":[["Port Sarim","Musa Point"],["Port Sarim","Entrana"],["Port Sarim","Crandor"],["Port Sarim","Void Knights' Outpost"],["East Ardougne","Brimhaven"],["Tree Gnome Stronghold","Piscatoris"],["Tai Bwo Wannai","Feldip Hills"],["Rellekka","Miscellania"],["Rellekka","Etceteria"],["Rellekka","Waterbirth Island"],["Rellekka","Jatizso"],["Rellekka","Neitiznot"],["Rellekka","Pirates' Cove"],["Rellekka","Iceberg"],["Pirates' Cove","Lunar Isle"],["Port Phasmatys","Mos Le'Harmless"],["Port Phasmatys","Dragontooth Island"],["Burgh de Rott","Meiyerditch"],["The Hollows","Mort'ton"],["Taverley","Daemonheim"],["Al Kharid","Daemonheim"],["Digsite","Anachronia"]]}]};
  const WM_TRANSPORT_CAT = 1206;
  let wmStopIdx = null;              // stop key -> {net, stop, x, y, p, ml, dist}
  let wmStopByPin = null;            // "x,y" of a transport placement -> [stop keys]
  function wmNorm(n) { return String(n || '').toLowerCase().replace(/[^a-z0-9]/g, ''); }
  function wmBuildStops() {
    if (wmStopIdx || !WM_ML || !MAP_LABELS || !MAP_LABELS.length) return;
    wmLoadSymbols();
    if (!WM_SYM) return;
    const catName = (c) => ((WM_CAT && WM_CAT[c] && WM_CAT[c].n) || '').toLowerCase();
    const pinsOf = (net) => WM_SYM.filter(p => { const e = WM_ML[p.ml]; if (!e) return false; return net.cat ? catName(e.c).indexOf(net.cat) >= 0 : e.c === WM_TRANSPORT_CAT; });
    const pinsTransport = pinsOf({});
    if (!pinsTransport.length) return;
    const labels = MAP_LABELS.map(d => ({ k: wmNorm(d.n), x: d.x, y: d.y, p: d.p | 0, n: d.n }));
    const idx = new Map(), byPin = new Map();
    const bind = (key, net, stop, x, y, p, dist, ml) => {
      idx.set(key, { net, stop, x, y, p, dist, ml });
      const pk = x + ',' + y; if (!byPin.has(pk)) byPin.set(pk, []); byPin.get(pk).push(key);
    };
    for (const net of WM_ROUTES.networks) {
      const pins = pinsOf(net);
      for (const st of net.stops) {
        const key = net.id + '|' + st.name;
        // A stop binds ONLY to a real map icon: its confirmed coordinate must sit within 6 tiles
        // of a placement of the network's element (Transportation icon 612/3701, or a Fairy ring
        // icon). No coordinate, or no icon there, means the stop stays unbound and says so.
        if (!st.coord) continue;
        let best = null, bd = 1e9;
        for (const pn of pins) { const d = Math.max(Math.abs(pn.x - st.coord[0]), Math.abs(pn.y - st.coord[1])); if (d < bd) { bd = d; best = pn; } }
        // Surface stops fly on floor 0 whatever plane the icon record carries (charter icons are
        // stored on plane 1 yet belong to the ground view); the stop's own plane wins if it has one.
        if (best && bd <= 6) bind(key, net, st, best.x, best.y, (st.coord[2] | 0), bd, best.ml);
        else bind(key, net, st, st.coord[0], st.coord[1], (st.coord[2] | 0), -1, -1);   // authoritative coordinate: flyable even without a nearby icon
      }
    }
    wmStopIdx = idx; wmStopByPin = byPin;
  }
  // Destinations reachable from a stop, per the network's kind.
  function wmStopDests(key) {
    const rec = wmStopIdx.get(key); if (!rec) return [];
    const net = rec.net, out = [];
    const push = (nm) => { const k2 = net.id + '|' + nm; const r2 = wmStopIdx.get(k2); if (r2 && k2 !== key) out.push({ key: k2, name: nm, x: r2.x, y: r2.y, p: r2.p, note: r2.stop.note || '' }); else if (!r2) out.push({ key: k2, name: nm, unbound: true, note: (net.stops.find(s => s.name === nm) || {}).note || '' }); };
    if (net.kind === 'all-to-all') { for (const st of net.stops) if (st.name !== rec.stop.name) push(st.name); }
    else {
      for (const l of (net.links || [])) {
        if (l[0] === rec.stop.name) push(l[1]);
        else if (l[1] === rec.stop.name) push(l[0]);
      }
      if (net.kind === 'chain' && !(net.links || []).length) {
        const i = net.stops.findIndex(s => s.name === rec.stop.name);
        if (i > 0) push(net.stops[i - 1].name); if (i >= 0 && i + 1 < net.stops.length) push(net.stops[i + 1].name);
      }
    }
    return out;
  }
  function wmStopsAt(x, y) { wmBuildStops(); return (wmStopByPin && wmStopByPin.get(x + ',' + y)) || []; }
  // Destination picker: a body-level menu (same chrome as the sound / aura pickers) listing
  // every destination of every network at this stop; a click flies there with a named pin.
  function wmOpenRoutePicker(anchorX, anchorY, keys) {
    if (typeof closeSoundMenu === 'function') closeSoundMenu();
    const pop = document.createElement('div'); pop.className = 'sndmenu'; pop.id = 'sndMenu';
    pop.style.cssText = 'position:fixed;z-index:2147483000;max-height:340px;overflow:auto;min-width:220px';
    for (const key of keys) {
      const rec = wmStopIdx.get(key); if (!rec) continue;
      const h = document.createElement('div'); h.className = 'sndmenu-it'; h.style.cssText = 'font-weight:700;opacity:.8;cursor:default';
      h.textContent = rec.net.name + ' - ' + rec.stop.name; h.style.textTransform = 'none'; pop.appendChild(h);
      for (const d of wmStopDests(key)) {
        const it = document.createElement('div'); it.className = 'sndmenu-it';
        it.textContent = '  ' + d.name + (d.note ? '  (' + d.note + ')' : '') + (d.unbound ? '  [position not confirmed yet]' : '');
        it.style.textTransform = 'none';
        if (d.unbound) { it.style.opacity = '.45'; it.style.cursor = 'default'; it.title = 'This stop has no verified map position yet, so it cannot be flown to.'; }
        else it.addEventListener('mousedown', ev => { ev.preventDefault(); ev.stopPropagation(); if (typeof closeSoundMenu === 'function') closeSoundMenu(); wmFlyTo(d.x, d.y, d.p, { x: d.x, y: d.y, p: d.p, nm: d.name }); });
        pop.appendChild(it);
      }
    }
    document.body.appendChild(pop);
    pop.style.left = Math.min(anchorX, (window.innerWidth || 1280) - 240) + 'px';
    pop.style.top = Math.min(anchorY, (window.innerHeight || 720) - 350) + 'px';
    try { wmRectsSoon(); } catch (e) {}
  }
  function wmRouteTipLines(x, y) {
    const keys = wmStopsAt(x, y); if (!keys.length) return '';
    let html = '';
    for (const key of keys) {
      const rec = wmStopIdx.get(key), dests = wmStopDests(key);
      html += '<div class="wm-fact" style="margin-top:4px"><span>' + htmlEsc(rec.net.name) + '</span><span class="v">' + htmlEsc(rec.stop.name) + '</span></div>';
      html += '<div style="opacity:.8">' + htmlEsc(dests.map(d => d.name).join(', ')) + '</div>';
    }
    html += '<div style="opacity:.6;margin-top:3px">Click to choose a destination</div>';
    return html;
  }
  // Link destination of a map element: param 4148 is a packed coordgrid (plane<<28 | x<<14 | y),
  // the value the game's click handler (script 7592 -> 304) jumps to for dungeon links, stairs
  // and the like. null when the element has no link.
  function wmElemLink(ml, at) {
    const e = WM_ML && WM_ML[ml]; if (!e) return null;
    const v = e.p ? e.p['4148'] : null;
    if (v != null && v !== -1 && v >= 0) return { x: (v >> 14) & 16383, y: v & 16383, p: (v >> 28) & 3 };
    // Dungeon ENTRANCE icons carry no link param (the server moves the player). The interior
    // of nearly every surface dungeon is the same x at y + 6400; offer that when the map has
    // a placed square there (and the reverse for an entrance seen from below).
    if (at && wmZoneIdx) {
      const cat = (WM_CAT && WM_CAT[e.c] && WM_CAT[e.c].n) || '';
      if (/dungeon|cave|mine|entrance|ladder|stairs|trapdoor/i.test(cat + ' ' + (e.t || ''))) {
        const ty = at.y >= 6400 ? at.y - 6400 : at.y + 6400;
        if (wmZoneIdx.has(((at.x >> 6) << 8) | (ty >> 6))) return { x: at.x, y: ty, p: 0, guess: true };
      }
    }
    return null;
  }
  function wmLinkLine(ml, at) {
    const lk = wmElemLink(ml, at); if (!lk) return '';
    return '<div class="wm-fact" style="margin-top:4px"><span>' + (lk.guess ? 'Click to view ' + (lk.y >= 6400 ? 'underground' : 'the surface') : 'Click to follow') + '</span><span class="v">' + lk.x + ', ' + lk.y + (lk.p ? ' f' + lk.p : '') + '</span></div>';
  }
  function wmElemTipHtml(m) {
    const t = wmElemTip(m.ml);
    const head = (t ? t.head : '') || wmLocName(m);
    const body = t ? t.body : '';
    // Only the FIRST <br> is the header/body split; a body legitimately carries more of them
    // ("Level 19 Strength<br>Level 8 Agility<br>Requires a grapple"). Escape everything first,
    // then re-allow just <br>, so cache text can never inject markup but still breaks lines.
    const esc = function (s) { return htmlEsc(s).replace(/&lt;br\s*\/?&gt;/gi, '<br>'); };
    const lkLine = wmLinkLine(m.ml, m) + ((WM_ML[m.ml] && WM_ML[m.ml].c === WM_TRANSPORT_CAT) ? wmRouteTipLines(m.x, m.y) : '');
    return '<b style="text-transform:uppercase;letter-spacing:.5px">' + esc(head || 'Map symbol') + '</b>'
         + (body ? '<br>' + esc(body) : '')
         + '<br><span style="opacity:.6">' + m.x + ', ' + m.y + '</span>' + lkLine;
  }
  // The hovered TILE outlined at the map's own scale. A positioned div, so pointing at
  // tiles never forces a canvas repaint; wmDraw re-syncs it after pan/zoom.
  function wmHoverBox() {
    const stage = document.getElementById('wmStage'); if (!stage) return;
    let box = document.getElementById('wmHoverTile');
    if (!box) {
      box = document.createElement('div'); box.id = 'wmHoverTile';
      box.style.cssText = 'position:absolute;pointer-events:none;z-index:6;display:none;'
        + 'border:1.5px solid rgba(255,170,60,0.95);box-shadow:0 0 0 1px rgba(0,0,0,0.55);border-radius:1px';
      stage.appendChild(box);
    }
    const z = wmCam.z;
    if (wmHover.x < 0 || z < 2) { box.style.display = 'none'; return; }
    const px = (wmHover.x - wmCam.x) * z + stage.clientWidth / 2;
    const py = (wmCam.y - (wmHover.y + 1)) * z + stage.clientHeight / 2;
    box.style.left = px + 'px'; box.style.top = py + 'px';
    box.style.width = z + 'px'; box.style.height = z + 'px';
    box.style.display = 'block';
  }
  // ---- chrome: bar chips + status line ----
  function wmPaintBar() {
    const bar = $('wmBar'); if (!bar) return;
    const chips = bar.querySelectorAll('[data-wm]');
    for (let i = 0; i < chips.length; i++) {
      const c = chips[i], k = c.dataset.wm;
      if (k.indexOf('f') === 0 && k.length === 2) c.classList.toggle('on', wmCam.p === +k[1]);
      else if (k === 'follow') c.classList.toggle('on', wmFollow);
      else if (wmLayer[k] !== undefined) c.classList.toggle('on', !!wmLayer[k]);
    }
  }
  // nearLabel walks every label; it ran on every mousemove. One answer per 8x8-tile cell.
  const wmNearCache = new Map();
  function wmNearMemo(x, y, p) {
    const k = ((x >> 3) << 16) | ((y >> 3) << 3) | (p & 7);
    let v = wmNearCache.get(k);
    if (v === undefined) { v = nearLabel(x, y, p) || ''; if (wmNearCache.size > 4000) wmNearCache.clear(); wmNearCache.set(k, v); }
    return v;
  }
  function wmPaintStatus() {
    const st = $('wmStatus'); if (!st) return;
    const near = (wmHover.x >= 0) ? wmNearMemo(wmHover.x, wmHover.y, wmCam.p) : '';
    let html = (wmHover.x >= 0 ? '<b>' + wmHover.x + ', ' + wmHover.y + '</b>' + (near ? ' <span class="wm-near">' + htmlEsc(near) + '</span>' : '') : '<span>hover the map for coordinates</span>')
      + '<span style="margin-left:auto">floor ' + wmCam.p + ' - ' + (Math.round(wmCam.z * 100) / 100) + ' px/tile</span>';
    if (wmSel) html += '<span class="wm-x" id="wmSelClear" title="Clear pin">&times;</span>';
    setHTML(st, html);
    const xb = $('wmSelClear');
    if (xb && !xb._b) { xb._b = 1; xb.addEventListener('click', function () { wmSel = null; wmKick(); }); }
  }

  // ---- panel ----
  const WM_CSS = ''
    + '.wm-wrap{display:flex;flex-direction:column;gap:6px;height:100%;min-height:0}'
    + '.wm-searchrow{position:relative;flex:0 0 auto}'
    + '.wm-search{width:100%;box-sizing:border-box;height:30px;padding:0 10px;background:var(--bg-elev);border:1px solid var(--border);border-radius:8px;color:var(--text);font-size:12px;outline:none}'
    + '.wm-search:focus{border-color:var(--border-hi)}'
    + '.wm-results{position:absolute;top:32px;left:0;right:0;z-index:30;max-height:260px;overflow-y:auto;background:#12151d;border:1px solid var(--border-hi);border-radius:8px;box-shadow:0 8px 22px rgba(0,0,0,0.55)}'
    + '.wm-res{display:flex;gap:7px;align-items:center;padding:5px 8px;cursor:pointer;font-size:11.5px}'
    + '.wm-res:hover,.wm-res.sel{background:rgba(var(--accent-rgb),0.14)}'
    + '.wm-res .ty{flex:0 0 auto;font-size:9px;font-weight:800;text-transform:uppercase;letter-spacing:0.4px;padding:1px 6px;border-radius:7px;background:rgba(255,255,255,0.07);color:var(--text-dim)}'
    + '.wm-res .nm{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}'
    + '.wm-res .dt{flex:0 1 auto;color:var(--text-mute);font-size:10px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}'
    + '.wm-bar{display:flex;flex-wrap:wrap;gap:4px;align-items:center;flex:0 0 auto}'
    + '.wm-chip{padding:2px 9px;border-radius:9px;border:1px solid var(--border);background:var(--bg-elev);color:var(--text-dim);font-size:10.5px;font-weight:600;cursor:pointer;user-select:none}'
    + '.wm-chip:hover{border-color:var(--border-hi);color:var(--text)}'
    + '.wm-chip.on{color:#46e0c0;border-color:rgba(70,224,192,0.55);background:rgba(70,224,192,0.12)}'
    + '.wm-sep{width:1px;height:16px;background:var(--border);margin:0 2px}'
    + '.wm-stage{position:relative;flex:1 1 auto;min-height:220px;overflow:hidden;border:1px solid var(--border);border-radius:8px;background:#0b0d12;cursor:grab}'
    + '.wm-stage.grabbing{cursor:grabbing}'
    + '#wmCanvas{position:absolute;left:0;top:0;width:100%;height:100%}'
    + '.wm-tip{position:absolute;z-index:20;display:none;pointer-events:none;background:rgba(9,11,17,0.94);border:1px solid var(--border-hi);border-radius:8px;padding:7px 9px;font-size:11px;line-height:1.45;max-width:320px;box-shadow:0 10px 28px rgba(0,0,0,0.55)}'
    /* Structured symbol tooltip: same vocabulary as the panels - caps accent header over a
       hairline rule, then dark row plates. Wider than the plain tooltip because a resource table
       needs the room; `.wide` again for two-column sets. .tg-body's white-space rule is scoped to
       #global-tip, so the grid builder's node needs it re-declared here. */
    + '.wm-tip.wide{max-width:392px}'
    + '.wm-tip .tg-body{white-space:normal}'
    + '.wm-h{display:block;font-size:10px;font-weight:800;letter-spacing:.6px;text-transform:uppercase;'
    +   'color:var(--accent-hi);padding-bottom:5px;margin-bottom:6px;border-bottom:1px solid var(--border)}'
    + '.wm-desc{color:var(--text-dim);margin-bottom:6px;white-space:normal}'
    + '.wm-fact{display:flex;justify-content:space-between;gap:12px;padding:2px 0;color:var(--text-mute)}'
    + '.wm-fact .v{color:var(--text);font-weight:600}'
    + '.wm-reqs{margin:2px 0 7px}'
    + '.wm-reqh{color:var(--text-mute);font-size:10px;font-weight:700;letter-spacing:.4px;'
    +   'text-transform:uppercase;margin-bottom:3px}'
    /* The bullet is drawn with a pseudo-element so the text wraps flush under itself. */
    + '.wm-req{position:relative;padding-left:11px;color:var(--text);white-space:normal}'
    + '.wm-req:before{content:"";position:absolute;left:2px;top:7px;width:4px;height:4px;'
    +   'border-radius:50%;background:currentColor;opacity:.55}'
    + '.wm-req.met{color:var(--text-mute)}'
    + '.wm-req.unmet{color:var(--warn,#e0a44a)}'
    + '.wm-rows{display:flex;flex-direction:column;gap:3px}'
    /* minmax(0,1fr), NOT 1fr: a plain 1fr track is minmax(auto,1fr), so its floor is the item's
       min-content and a long cache name (.wm-nm is white-space:nowrap) forces the track wider than
       its half, pushing the second card out through the tip's own border. minmax(0,1fr) pins both
       tracks to an exact half; min-width:0 lets the row shrink into it and overflow:hidden clips at
       the card's rounded edge, so the worst case is a truncated chip instead of a broken box. */
    + '.wm-rows.wide{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:3px 5px}'
    + '.wm-row{display:flex;align-items:center;gap:8px;min-height:26px;padding:3px 8px 3px 5px;'
    +   'min-width:0;overflow:hidden;'
    +   'border-radius:7px;background:rgba(255,255,255,0.035);border:1px solid var(--border)}'
    + '.wm-row.wait{opacity:.3}'
    + '.wm-lv{flex:0 0 auto;display:flex;align-items:center;gap:5px;min-width:52px;'
    +   'font-size:10px;font-weight:800;color:var(--ok);font-variant-numeric:tabular-nums}'
    + '.wm-sk{width:15px;height:15px;background:no-repeat center/contain}'
    + '.wm-mid{flex:1 1 auto;min-width:0;display:flex;flex-direction:column;gap:3px}'
    + '.wm-nm{color:var(--text);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}'
    + '.wm-note{color:var(--text-mute);font-size:10px}'
    + '.wm-strip{display:flex;align-items:center;gap:4px;min-width:0}'
    + '.wm-cell{width:22px;height:22px;flex:0 0 auto}'
    + '.wm-glab{font-size:9.5px;font-weight:700;color:var(--text-mute);white-space:nowrap}'
    + '.wm-inm{min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;color:var(--text-dim)}'
    + '.wm-xy{margin-top:6px;padding-top:5px;border-top:1px solid var(--border);'
    +   'font-size:10px;color:var(--text-mute);font-variant-numeric:tabular-nums}'
    + '.wm-status{flex:0 0 18px;height:18px;overflow:hidden;font-size:10.5px;color:var(--text-dim);display:flex;gap:8px;align-items:center}'
    + '.wm-status span{white-space:nowrap}'
    + '.wm-status .wm-near{flex:0 1 auto;min-width:0;overflow:hidden;text-overflow:ellipsis}'
    + '.wm-status b{color:var(--text);white-space:nowrap}'
    + '.wm-x{cursor:pointer;color:var(--text-mute);font-weight:800;padding:0 4px;font-size:13px}';
  let wmDrag = null, wmWinBound = false;
  function renderWorldMap() {
    const c = $('content');
    if ($('wmWrap')) { wmKick(); return; }        // already mounted: the 250ms poll must not rebuild (kills drag/search focus)
    injectStyle('wm-style', WM_CSS);
    c.innerHTML = '';
    const wrap = document.createElement('div'); wrap.id = 'wmWrap'; wrap.className = 'wm-wrap';
    wrap.innerHTML = ''
      + '<div class="wm-searchrow">'
      + '  <input id="wmSearch" class="wm-search" type="text" placeholder="Search places, teleports, lodestones, hidey-holes, markers, or x,y..." autocomplete="off" spellcheck="false">'
      + '  <div id="wmResults" class="wm-results" style="display:none"></div>'
      + '</div>'
      + '<div id="wmBar" class="wm-bar">'
      + '  <div class="wm-chip" data-wm="f0">0</div><div class="wm-chip" data-wm="f1">1</div>'
      + '  <div class="wm-chip" data-wm="f2">2</div><div class="wm-chip" data-wm="f3">3</div>'
      + '  <div class="wm-sep"></div>'
      + '  <div class="wm-chip" data-wm="follow" title="Keep the view centred on you">Follow</div>'
      + '  <div class="wm-chip" id="wmYou" title="Centre on your position">You</div>'
      + '  <div class="wm-sep"></div>'
      + '  <div class="wm-chip" data-wm="icons" title="Map symbols (banks, altars, shortcuts...)">Symbols</div>'
      + '  <div class="wm-chip" data-wm="labels" title="Place names">Labels</div>'
      + '  <div class="wm-chip" data-wm="lodes" title="Lodestones">Lodes</div>'
      + '  <div class="wm-chip" data-wm="teles" title="Teleport destinations (zoom in)">Teles</div>'
      + '  <div class="wm-chip" data-wm="hidey" title="Hidey-holes (zoom in)">Hidey</div>'
      + '  <div class="wm-chip" data-wm="marks" title="Your tile markers">Marks</div>'
      + '  <div class="wm-chip" data-wm="grid" title="World tile grid">Grid</div>'
      + '  <div class="wm-sep"></div>'
      + '  <div class="wm-chip" id="wmZoomOut" title="Zoom out">&minus;</div>'
      + '  <div class="wm-chip" id="wmZoomIn" title="Zoom in">+</div>'
      + '</div>'
      + '<div id="wmStage" class="wm-stage"><canvas id="wmCanvas"></canvas><div id="wmTip" class="wm-tip"></div></div>'
      + '<div id="wmStatus" class="wm-status"></div>';
    c.appendChild(wrap);
    const stage = $('wmStage'), cvEl = $('wmCanvas'), inp = $('wmSearch'), res = $('wmResults');
    const zoomAt = function (mx, my, factor) {
      const nz = Math.max(WM_ZMIN, Math.min(WM_ZMAX, wmCam.z * factor));
      if (nz === wmCam.z) return;
      const wx = wmCam.x + (mx - stage.clientWidth / 2) / wmCam.z;
      const wy = wmCam.y - (my - stage.clientHeight / 2) / wmCam.z;
      wmCam.z = nz;
      wmCam.x = wx - (mx - stage.clientWidth / 2) / nz;
      wmCam.y = wy + (my - stage.clientHeight / 2) / nz;
      wmSaveCam(); wmKick();
    };
    stage.addEventListener('wheel', function (e) {
      e.preventDefault();
      const pt = wmPt(e, stage);
      zoomAt(pt.x, pt.y, e.deltaY < 0 ? 1.22 : 1 / 1.22);
    }, { passive: false });
    stage.addEventListener('mousedown', function (e) {
      const p0 = wmPt(e, stage);
      wmDrag = { x: e.clientX, y: e.clientY, px: p0.x, py: p0.y, cx: wmCam.x, cy: wmCam.y, moved: false };
      stage.classList.add('grabbing'); e.preventDefault();
    });
    stage.addEventListener('mousemove', function (e) {
      const r = stage.getBoundingClientRect();
      const pt = wmPt(e, stage), mx = pt.x, my = pt.y;
      const tx = Math.floor(wmCam.x + (mx - stage.clientWidth / 2) / wmCam.z);
      const ty = Math.floor(wmCam.y - (my - stage.clientHeight / 2) / wmCam.z);
      if (tx !== wmHover.x || ty !== wmHover.y) { wmHover = { x: tx, y: ty }; wmPaintStatus(); wmHoverBox(); }
      const tip = $('wmTip');
      if (tip && !wmDrag) {
        let hit = null, hd = Infinity;
        for (const m of wmMarks) { const d = Math.hypot(m.sx - mx, m.sy - my); if (d <= m.r + 5 && d < hd) { hd = d; hit = m; } }
        if (hit) {
          // Rebuild the CONTENT only when the hovered mark changes; the position still follows
          // the cursor every move. Repainting per mousemove also broke icon sizing: setIconBg ->
          // sizeIcon picks native-vs-contain from the element's clientWidth, and a freshly built
          // node has none yet, so every rebuild reset the icons to native and the next poll's
          // sizeAllIcons pushed them back to contain - they visibly oscillated while the mouse
          // moved.
          const hsig = hit.ts ? ('ts:' + hit.sx + ',' + hit.sy)
                     : (hit.ml != null ? ('ml:' + hit.ml + ',' + hit.x + ',' + hit.y)
                                       : ('tp:' + (hit.tip || '')));
          if (tip._hsig !== hsig) {
            tip._hsig = hsig;
            if (hit.ts) { wmTipHit = null; tip.classList.remove('wide'); tip.innerHTML = wmTeleTip(hit.ts); }
            else if (hit.ml != null) wmTipPaintElem(tip, hit);
            else { wmTipHit = null; tip.classList.remove('wide'); tip.innerHTML = hit.tip; }
          }
          tip.style.display = 'block';
          let lx = mx + 12, ty2 = my + 10;
          const tw = tip.offsetWidth, th = tip.offsetHeight;
          if (lx + tw > r.width - 2) lx = mx - tw - 12;
          if (lx < 2) lx = 2;
          if (ty2 + th > r.height - 2) ty2 = r.height - th - 2;
          if (ty2 < 2) ty2 = 2;
          tip.style.left = lx + 'px'; tip.style.top = ty2 + 'px';
        } else { tip.style.display = 'none'; tip._hsig = ''; wmTipHit = null; }
      }
    });
    stage.addEventListener('mouseleave', function () { const tip = $('wmTip'); if (tip) { tip.style.display = 'none'; tip._hsig = ''; } wmTipHit = null; wmHover = { x: -1, y: -1 }; wmPaintStatus(); wmHoverBox(); });
    // The pane resizes on its own now (floating window), so a viewport resize listener
    // alone misses it: watch the stage itself and redraw at the new size.
    // One observer per mount: drop the previous one or every remount adds another redraw.
    if (typeof ResizeObserver === 'function') { if (wmRO) wmRO.disconnect(); wmRO = new ResizeObserver(function () { wmKick(); }); wmRO.observe(stage); }
    if (!wmWinBound) {
      wmWinBound = true;
      window.addEventListener('mousemove', function (e) {
        if (!wmDrag) return;
        const st2 = document.getElementById('wmStage'); if (!st2) { wmDrag = null; return; }
        // Same coordinate source as the hover/click paths: engine offsets in stage CSS px.
        const pt = wmPt(e, st2);
        const dx = pt.x - wmDrag.px, dy = pt.y - wmDrag.py;
        if (Math.abs(dx) + Math.abs(dy) > 3) { wmDrag.moved = true; if (wmFollow) { wmFollow = false; wmPaintBar(); } }
        wmCam.x = Math.max(64, Math.min(16320, wmDrag.cx - dx / wmCam.z));
        wmCam.y = Math.max(64, Math.min(16320, wmDrag.cy + dy / wmCam.z));
        wmKick();
      });
      window.addEventListener('mouseup', function (e) {
        const st2 = document.getElementById('wmStage');
        if (wmDrag && st2 && !wmDrag.moved) {                   // click (no drag) = pin the tile
          const r = st2.getBoundingClientRect();
          const pt = wmPt(e, st2), mx = pt.x, my = pt.y;
          if (mx >= 0 && my >= 0 && mx <= r.width && my <= r.height) {
            // A click on a linked element (dungeon link, stairs, ladders...) follows it the way
            // the game does: fly to the destination and pin it, plane switch included.
            let hitM = null, hdM = Infinity;
            for (const m of wmMarks) { if (m.ml == null) continue; const d = Math.hypot(m.sx - mx, m.sy - my); if (d <= m.r + 4 && d < hdM) { hdM = d; hitM = m; } }
            if (hitM && WM_ML[hitM.ml] && WM_ML[hitM.ml].c === WM_TRANSPORT_CAT) {
              const keys = wmStopsAt(hitM.x, hitM.y);
              if (keys.length) {
                wmOpenRoutePicker(e.clientX + 8, e.clientY + 8, keys);
                wmDrag = null; if (st2) st2.classList.remove('grabbing');
                return;
              }
            }
            const lk = hitM ? wmElemLink(hitM.ml, hitM) : null;
            if (lk) {
              const t = wmElemTip(hitM.ml), nm = (t && t.head) ? String(t.head).replace(/<[^>]*>/g, '') : 'Link';
              wmFlyTo(lk.x, lk.y, lk.p, { x: lk.x, y: lk.y, p: lk.p, nm: nm });
              wmDrag = null; if (st2) st2.classList.remove('grabbing');
              return;
            }
            const tx = Math.floor(wmCam.x + (mx - st2.clientWidth / 2) / wmCam.z);
            const ty = Math.floor(wmCam.y - (my - st2.clientHeight / 2) / wmCam.z);
            wmSel = { x: tx, y: ty, p: wmCam.p, nm: '', dt: '' };
            wmKick();
          }
        }
        if (wmDrag) { wmDrag = null; if (st2) st2.classList.remove('grabbing'); wmSaveCam(); }
      });
      window.addEventListener('resize', function () { paneRun('worldmap', wmKick); });
    }
    $('wmBar').addEventListener('click', function (e) {
      const chip = e.target.closest ? e.target.closest('.wm-chip') : null; if (!chip) return;
      const k = chip.dataset.wm;
      if (k && k.indexOf('f') === 0 && k.length === 2) wmSetPlane(+k[1]);
      else if (k === 'follow') { wmFollow = !wmFollow; wmPaintBar(); wmKick(); }
      else if (k && wmLayer[k] !== undefined) { wmLayer[k] = !wmLayer[k]; wmSaveLayers(); wmPaintBar(); wmKick(); }
      else if (chip.id === 'wmYou') { if (wmPlayer) wmFlyTo(wmPlayer.x, wmPlayer.y, wmPlayer.p, null); }
      else if (chip.id === 'wmZoomOut') zoomAt(stage.clientWidth / 2, stage.clientHeight / 2, 1 / 1.5);
      else if (chip.id === 'wmZoomIn') zoomAt(stage.clientWidth / 2, stage.clientHeight / 2, 1.5);
    });
    let wmSearchT = 0;
    inp.addEventListener('input', function () { wmResSel = 0; clearTimeout(wmSearchT); wmSearchT = setTimeout(wmRenderResults, 90); });
    inp.addEventListener('focus', function () { wmRenderResults(); });
    inp.addEventListener('blur', function () { setTimeout(function () { const b = $('wmResults'); if (b) b.style.display = 'none'; }, 150); });   // let a result click land first
    inp.addEventListener('keydown', function (e) {
      const es = (res && res._es) || [];
      if (e.key === 'ArrowDown') { wmResSel = Math.min(es.length - 1, wmResSel + 1); wmRenderResults(); e.preventDefault(); }
      else if (e.key === 'ArrowUp') { wmResSel = Math.max(0, wmResSel - 1); wmRenderResults(); e.preventDefault(); }
      else if (e.key === 'Enter') { wmPickResult(wmResSel); e.preventDefault(); }
      else if (e.key === 'Escape') { res.style.display = 'none'; inp.blur(); }
    });
    res.addEventListener('mousedown', function (e) {           // mousedown beats the input blur timer
      const row = e.target.closest ? e.target.closest('.wm-res') : null;
      if (row) { wmPickResult(+row.dataset.i); e.preventDefault(); }
    });
    wmPaintBar();
    wmKick();
  }
