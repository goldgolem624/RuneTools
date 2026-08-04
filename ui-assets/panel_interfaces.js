// RuneToolsX panel: Interfaces inspector.
// Spliced inline into client.html; bare classic script sharing one global scope.

  // Groups are listed cheaply (collapsed); a group's widget tree is fetched only on expand, so the
  // whole tree is never walked or polled.
  // Hand-curated group names; ids without a confident name stay bare numbers.
  const IFACE_NAMES = {
    13:'Bank PIN', 1253:'Treasure Hunter (removed)',
    919:'Fish Flingers scoreboard', 922:'Fish Flingers results', 923:'Fish Flingers details',
    1286:'Fish Flingers discovered ratings', 37:'Smithing / Smelting', 91:'Dungeoneering party', 105:'Grand Exchange',
    107:'Grand Exchange inventory', 109:'GE collection box', 137:'Chat box', 190:'Quest list',
    284:'Buff bar', 291:'Debuff bar', 345:'Mysterious clue scroll', 364:'Treasure trails',
    517:'Bank', 590:'Emote', 660:'Archaeology material storage', 662:'Familiar / Summoning',
    720:'Teleports', 906:'Lobby frame', 907:'Lobby news', 910:'Lobby world select',
    945:'Dungeoneering', 1029:'Community',
    1030:'Murder on the Border investigation',
    1049:'Dungeoneering select', 1092:'Lodestone network', 1117:'Citadel work', 1145:'Spirit tree map',
    1179:'Select tool', 1184:'NPC dialogue', 1186:'Server message dialogue', 1188:'Dialogue option select',
    1189:'Clue scroll continue', 1191:'Player dialogue', 1213:'XP circle', 1219:'Necromancy spellbook',
    1222:'Well of Souls talent tree', 1223:'Active ritual', 1224:'Ritual selection',
    1234:'Time counter', 1251:'Progress bar', 1265:'Shop', 1299:'Group Ironman list', 1308:'Rewards shop',
    1317:'Group Ironman chat', 1370:'Crafting menu', 1371:'Crafting menu',
    1416:'Music player', 1417:'Notes', 1430:'Main action bar', 1431:'Ribbon bar',
    1433:'Main options menu', 1446:'Hero', 1449:'Defence abilities', 1452:'Ranged abilities', 1454:'Powers',
    1458:'Prayer book', 1460:'Melee abilities', 1461:'Magic spellbook', 1464:'Equipment', 1465:'Minimap',
    1466:'Skills', 1467:'Private chat', 1470:'Guest clan chat', 1471:'Clan chat', 1472:'Friends chat',
    1473:'Inventory', 1494:'Marketplace', 1514:'Fullscreen window frame', 1516:'Furniture Construction',
    1518:'Furniture Storage', 1519:'Group list', 1529:'Group chat',
    1536:'Hefin hour buffs', 1550:'Hefin velocity HUD', 1551:'Serenity correct pose', 1552:'Serenity pose select', 1572:'Quiver map',
    1587:'World select', 1588:'RuneMetrics', 1591:'Boss instance', 1622:'Loot window', 1639:'Slayer counter',
    1665:'House Controls',
    1670:'Second action bar', 1671:'Third action bar', 1672:'Fourth action bar', 1673:'Fifth action bar',
    1708:'Invention discovery', 1843:'Customisations', 1854:'Activity tracker', 1882:'Constitution abilities',
    1883:'Defensive abilities', 1894:'Achievement path', 1929:'Aura manager',
  };
  let ifaceGroups = null;          // [{id, n}, ..]  (n = top-level widget count)
  const ifaceWidgets = {};         // gid -> [{t,d,r}, ..]  (cached on first expand)
  const ifaceDefModels = {};       // gid -> {compId: modelId} for type-6 MODEL comps (js5-3 defs).
                                   // A model comp's live node only holds a runtime render handle
                                   // ("dynamic"); the cache def carries the model id, which keys
                                   // the pre-rendered modelicons.pack (bridge modelIcon).
  const ifaceOpen = {};            // gid -> bool (expanded)
  let ifaceOff = {};               // gid -> {x,y}  live origin nudge (persisted; pushed to the reader)
  let ifaceOffLoaded = false;
  let ifaceMonSeen = null;         // Set of currently-open gids (for the open/close diff), null = not primed
  let ifaceMon = [];               // [{ts, gid, op}] recent open/close events (newest first)
  let ifaceMonOn = true;           // monitor running (rescan every 0.6s while the tab is active)
  function loadIfaceOff() {
    if (ifaceOffLoaded) return; ifaceOffLoaded = true;
    try { ifaceOff = JSON.parse(localStorage.getItem('rtxIfaceOff') || '{}') || {}; } catch (e) { ifaceOff = {}; }
    pushAllIfaceOff();
  }
  function saveIfaceOff() { try { localStorage.setItem('rtxIfaceOff', JSON.stringify(ifaceOff)); } catch (e) {} }
  function pushIfaceOff(gid) {
    const o = ifaceOff[gid] || { x: 0, y: 0 };
    try { if (bridge() && bridge().ifaceOffset) bridge().ifaceOffset(gid, o.x | 0, o.y | 0); } catch (e) {}
  }
  function pushAllIfaceOff() { for (const k in ifaceOff) pushIfaceOff(parseInt(k, 10)); }
  // ---- component watch ---------------------------------------------------------
  // Clicking a row's id samples THAT component ~5x/s and logs every change to its rect or
  // sprite. Built for hover-driven state: the swap only shows while the mouse is on the
  // widget, so a still inspector never catches it.
  let ifWatch = null;   // {gid, key, label, until, seen:[], last:'', t}
  function ifWatchKey(w) {
    const t = w.t || [0, 0, 0];
    return t[0] + ':' + t[1] + (t[2] >= 0 ? ':' + t[2] : '');
  }
  function ifWatchSig(w) {
    const r = w.r || [0, 0, 0, 0];
    return r[0] + ',' + r[1] + ' ' + r[2] + 'x' + r[3] + ' spr:' + (w.s || '-')
         + (w.x ? ' txt:' + String(w.x).replace(/<[^>]*>/g, '').trim() : '');
  }
  function ifWatchStart(gid, key, label) {
    ifWatchStop();
    ifWatch = { gid: gid, key: key, label: label, until: Date.now() + 30000, seen: [], last: '' };
    ifWatch.t = setInterval(ifWatchTick, 200);
    ifWatchTick();
  }
  function ifWatchStop() {
    if (ifWatch && ifWatch.t) clearInterval(ifWatch.t);
    if (ifWatch) ifWatch.t = null;
    ifWatchPaint();
  }
  function ifWatchTick() {
    if (!ifWatch || !bridge()) return;
    let ws = [];
    try { ws = (JSON.parse(bridge().interfaceGroup(myPid(), ifWatch.gid) || '{}').widgets) || []; } catch (e) {}
    for (const w of ws) {
      if (ifWatchKey(w) !== ifWatch.key) continue;
      const sig = ifWatchSig(w);
      if (sig !== ifWatch.last) {
        ifWatch.last = sig;
        ifWatch.seen.push({ at: Date.now(), sig: sig, s: w.s || 0 });
      }
      break;
    }
    if (Date.now() > ifWatch.until) { clearInterval(ifWatch.t); ifWatch.t = null; }
    ifWatchPaint();
  }
  function ifWatchPaint() {
    const el = document.getElementById('ifWatchBox');
    if (!el) return;
    if (!ifWatch) { el.style.display = 'none'; el.innerHTML = ''; return; }
    el.style.display = 'block';
    const live = !!ifWatch.t;
    const left = Math.max(0, Math.ceil((ifWatch.until - Date.now()) / 1000));
    let html = '<div class="ifw-head"><b>' + ifWatch.label + '</b>'
      + '<span class="ifw-st">' + (live ? 'watching, ' + left + 's left' : 'finished') + '</span>'
      + '<button id="ifWatchEnd">' + (live ? 'stop' : 'clear') + '</button></div>';
    if (!ifWatch.seen.length) html += '<div class="ifw-none">no sample yet</div>';
    else {
      const t0 = ifWatch.seen[0].at;
      html += '<div class="ifw-list">';
      for (const e of ifWatch.seen) {
        html += '<div class="ifw-row"><span class="ifw-t">+' + ((e.at - t0) / 1000).toFixed(1) + 's</span>'
          + (e.s ? '<span class="if-sprico" data-spr="' + e.s + '"></span>' : '')
          + '<span class="ifw-sig">' + e.sig + '</span></div>';
      }
      html += '</div><div class="ifw-sum">' + ifWatch.seen.length + ' distinct states</div>';
    }
    el.innerHTML = html;
    el.querySelectorAll('.if-sprico').forEach(function (n) { loadSpriteIcon(n, +n.dataset.spr); });
    const b = document.getElementById('ifWatchEnd');
    if (b) b.onclick = function () { if (ifWatch && ifWatch.t) ifWatchStop(); else { ifWatch = null; ifWatchPaint(); } };
  }

  function refetchIfaceGroup(gid) {   // re-pull widget rects so the inspector hover-highlight reflects the new offset
    if (!bridge()) return;
    try { ifaceWidgets[gid] = (JSON.parse(bridge().interfaceGroup(myPid(), gid) || '{}').widgets) || []; }
    catch (e) {}
    if (activeTab === 'interfaces') renderIfaceList();
  }
  function ifaceStep(st) {
    const gid = parseInt(st.dataset.gid, 10);
    let o = ifaceOff[gid] || { x: 0, y: 0 };
    if (st.dataset.reset) o = { x: 0, y: 0 };
    else o = { x: (o.x | 0) + (parseInt(st.dataset.dx, 10) || 0), y: (o.y | 0) + (parseInt(st.dataset.dy, 10) || 0) };
    if (!o.x && !o.y) delete ifaceOff[gid]; else ifaceOff[gid] = o;
    saveIfaceOff();
    pushIfaceOff(gid);                // live: plugin highlights re-read each tick; inspector needs a re-fetch
    refetchIfaceGroup(gid);
  }
  function fetchInterfaceGroups() {
    if (!bridge()) return;
    try { ifaceGroups = (JSON.parse(bridge().interfaceGroups(myPid()) || '{}').groups) || []; }
    catch (e) { ifaceGroups = []; }
    for (const k in ifaceWidgets) delete ifaceWidgets[k];   // re-scan invalidates cached widgets
    if (activeTab === 'interfaces') renderIfaceList();
  }
  // Open/close monitor: every 0.6s, diff the set of loaded interface groups and log what opened or
  // closed, so an in-game action immediately shows which interface it toggled.
  function ifaceMonTick() {
    if (!ifaceMonOn || activeTab !== 'interfaces' || !bridge()) return;
    let cur;
    try { cur = (JSON.parse(bridge().interfaceGroups(myPid()) || '{}').groups) || []; } catch (e) { return; }
    const now = new Set(cur.map(g => g.id));
    if (ifaceMonSeen === null) { ifaceMonSeen = now; return; }   // prime silently on first scan
    const ts = nowClock();
    now.forEach(id => { if (!ifaceMonSeen.has(id)) ifaceMon.unshift({ ts: ts, gid: id, op: 'open' }); });
    ifaceMonSeen.forEach(id => { if (!now.has(id)) ifaceMon.unshift({ ts: ts, gid: id, op: 'close' }); });
    if (ifaceMon.length > 200) ifaceMon.length = 200;
    ifaceMonSeen = now;
    renderIfaceMon();
  }
  function nowClock() { const d = new Date(), p = n => (n < 10 ? '0' : '') + n; return p(d.getHours()) + ':' + p(d.getMinutes()) + ':' + p(d.getSeconds()); }
  function renderIfaceMon() {
    const host = document.getElementById('ifMon'); if (!host) return;
    if (!ifaceMon.length) { host.innerHTML = '<div class="if-empty">No interface opened/closed yet. Do something in-game.</div>'; return; }
    host.innerHTML = ifaceMon.map(e => {
      const nm = IFACE_NAMES[e.gid] || '';
      return '<div class="ifm-row ifm-' + e.op + '"><span class="ifm-ts">' + e.ts + '</span>'
        + '<span class="ifm-op">' + (e.op === 'open' ? 'opened' : 'closed') + '</span>'
        + '<span class="ifm-id">Group ' + e.gid + '</span>'
        + (nm ? '<span class="ifm-nm">' + nm + '</span>' : '') + '</div>';
    }).join('');
  }
  function toggleIfaceGroup(gid) {
    ifaceOpen[gid] = !ifaceOpen[gid];
    if (ifaceOpen[gid] && !ifaceWidgets[gid] && bridge()) {      // fetch this group's widgets once, on expand
      try { ifaceWidgets[gid] = (JSON.parse(bridge().interfaceGroup(myPid(), gid) || '{}').widgets) || []; }
      catch (e) { ifaceWidgets[gid] = []; }
    }
    if (ifaceOpen[gid] && !ifaceDefModels[gid] && bridge() && bridge().cacheIfaceGroup) {
      // model ids live only in the cache DEFS (type 6), never in the live tree
      const m = {};
      try { for (const c of (JSON.parse(bridge().cacheIfaceGroup(gid) || '{}').comps || []))
              if (c.t === 6 && c.model > 0) m[c.id] = c.model; }
      catch (e) {}
      ifaceDefModels[gid] = m;
    }
    renderIfaceList();
  }
  function renderInterfaces() {
    // Re-entered every poll tick; once built it bails so rows are not rebuilt each tick (hover
    // flicker). The list re-renders only on demand: search, expand/collapse, or Refresh.
    if (document.getElementById('ifaceWrap')) return;
    const c = $('content');
    {
      c.innerHTML = '';
      injectStyle('ifaceCss', '#ifaceWrap{display:flex;flex-direction:column;height:100%;gap:8px;padding:2px}'
          + '.if-bar{display:flex;gap:8px;align-items:center}'
          + '.if-bar input{flex:1;min-width:0;background:var(--bg-elev,#1a1c24);border:1px solid var(--border,#333);border-radius:6px;color:var(--text,#f0f0f5);padding:6px 8px;font-size:12px}'
          + '.if-bar button{flex:none;height:30px;padding:0 12px;border:1px solid var(--border,#333);border-radius:6px;background:var(--bg-elev,#1a1c24);color:var(--text,#f0f0f5);cursor:pointer;font-size:12px}'
          + '.if-bar button:hover{border-color:rgba(240,190,90,.7)}'
          + '.if-meta{font-size:11px;color:#9a9aa6;white-space:nowrap}'
          + '.if-list{flex:1;overflow-y:auto;overflow-x:hidden;font-family:Consolas,ui-monospace,monospace;font-size:11px}'
          + '.if-list::-webkit-scrollbar{width:9px}'
          + '.if-list::-webkit-scrollbar-thumb{background:rgba(255,255,255,.18);border-radius:5px;border:2px solid transparent;background-clip:padding-box}'
          + '.if-list::-webkit-scrollbar-track{background:transparent}'
          + '.if-group{display:flex;align-items:center;gap:6px;cursor:pointer;background:rgba(240,190,90,.14);color:#f0d28a;padding:4px 12px 4px 6px;margin-top:4px;border-radius:4px;font-weight:600;user-select:none}'
          + '.if-group:hover{background:rgba(240,190,90,.22)}'
          + '.if-caret{display:inline-block;width:10px;color:#caa85a}'
          + '.if-gname{font-weight:400;color:#bfe3c8;font-size:11px}'
          + '.if-n{margin-left:auto;font-weight:400;color:#9a9aa6;font-size:10px}'
          + '.if-row{display:flex;align-items:center;gap:7px;padding:2px 12px 2px 8px;border-bottom:1px solid rgba(255,255,255,.04)}'
          + '.if-row:hover{background:rgba(255,255,255,.05)}'
          + '.if-id{color:#8fb6ff;flex:none}'
          + '.if-ty{flex:none;font-size:9px;font-weight:700;text-transform:uppercase;letter-spacing:.3px;padding:1px 5px;border-radius:4px;opacity:.9}'
          + '.t-layer{color:#9aa0b5;background:rgba(154,160,181,.14)}'
          + '.t-text{color:#e3d5a2;background:rgba(227,213,162,.14)}'
          + '.t-graphic{color:#79c6d6;background:rgba(121,198,214,.14)}'
          + '.t-item{color:#79d6a6;background:rgba(121,214,166,.14)}'
          + '.t-rect{color:#b59ad0;background:rgba(181,154,208,.14)}'
          + '.if-amt{color:#e8c06a;flex:none;font-size:10px;font-weight:700}'
          + '.if-spr{color:#79c6d6;flex:none;font-size:10px;opacity:.85}'
          + '.if-sprico{flex:none;width:18px;height:18px;border-radius:3px;background:rgba(255,255,255,.05)}'
          + '.if-itemico{flex:none;width:18px;height:18px;border-radius:3px;background-color:rgba(255,255,255,.05);background-size:contain;background-repeat:no-repeat;background-position:center;image-rendering:-webkit-optimize-contrast}'
          + '.if-dyn{flex:none;font-size:10px;font-style:italic;color:#8a8a96}'
          + '.if-modelico{flex:none;width:18px;height:18px;border-radius:3px;background-color:rgba(255,255,255,.05);background-size:contain;background-repeat:no-repeat;background-position:center;image-rendering:-webkit-optimize-contrast}'
          + '.if-mdl{color:#d6a679;flex:none;font-size:10px;opacity:.9}'
          + '.if-item{color:#79d6a6;flex:none;font-size:10px;opacity:.9}'
          + '.if-txt{color:#e3d5a2;flex:0 1 auto;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}'
          + '.if-rect{color:#8a8a96;flex:none;margin-left:auto;padding-left:12px;white-space:nowrap}'
          + '.if-watchable{cursor:pointer;text-decoration:underline dotted rgba(255,255,255,.25)}'
          + '.if-watchable:hover{color:#7c6df2}'
          + '#ifWatchBox{margin:6px 8px;padding:8px 10px;border:1px solid rgba(124,109,242,.35);'
          + 'border-radius:6px;background:rgba(124,109,242,.07);font-size:12px}'
          + '.ifw-head{display:flex;align-items:center;gap:8px}'
          + '.ifw-st{margin-left:auto;color:#8a8a96;font-size:11px}'
          + '.ifw-head button{font:inherit;font-size:11px;padding:2px 8px;border-radius:4px;cursor:pointer;'
          + 'border:1px solid rgba(255,255,255,.15);background:rgba(255,255,255,.05);color:inherit}'
          + '.ifw-list{margin-top:5px;display:flex;flex-direction:column;gap:2px;max-height:220px;overflow:auto}'
          + '.ifw-row{display:flex;align-items:center;gap:7px}'
          + '.ifw-t{color:#8a8a96;width:46px;flex:none;font-variant-numeric:tabular-nums}'
          + '.ifw-sig{font-family:ui-monospace,Menlo,monospace}'
          + '.ifw-none,.ifw-sum{color:#8a8a96;font-size:11px;margin-top:4px}'
          + '.if-empty{color:#9a9aa6;padding:6px}'
          + '.if-off{display:flex;align-items:center;gap:5px;padding:4px 12px 5px 24px;color:#9a9aa6;font-size:11px}'
          + '.if-off b{color:#cfcfe0;font-weight:600}'
          + '.if-st{flex:none;height:20px;min-width:20px;padding:0 6px;border:1px solid var(--border,#333);border-radius:4px;background:var(--bg-elev,#1a1c24);color:var(--text,#f0f0f5);cursor:pointer;font-size:11px;line-height:1}'
          + '.if-st:hover{border-color:rgba(240,190,90,.7)}'
          + '.if-ov{min-width:26px;text-align:center;color:#8fb6ff}'
          + '.if-mon{flex:none;max-height:128px;overflow-y:auto;font-family:Consolas,ui-monospace,monospace;font-size:11px;border:1px solid rgba(255,255,255,.06);border-radius:6px;background:rgba(0,0,0,.18)}'
          + '.ifm-row{display:flex;align-items:center;gap:8px;padding:2px 10px;border-bottom:1px solid rgba(255,255,255,.04)}'
          + '.ifm-ts{color:#8a8a96;flex:none}'
          + '.ifm-op{flex:none;width:52px;font-weight:600}'
          + '.ifm-open .ifm-op{color:#4dd28a}.ifm-close .ifm-op{color:#fc7c7c}'
          + '.ifm-id{color:#8fb6ff;flex:none}.ifm-nm{color:#bfe3c8}');
      const wrap = document.createElement('div'); wrap.id = 'ifaceWrap';
      wrap.innerHTML =
        '<div class="if-bar"><input id="ifaceSearch" placeholder="filter by id or name, e.g. inventory">'
        + '<span class="if-meta" id="ifaceMeta"></span>'
        + '<button id="ifaceRefresh" title="Re-scan the group list">Refresh</button></div>'
        + '<div class="if-bar"><input id="ifaceSizeIn" placeholder="find by size, e.g. 512x352 (pair a panel to its 1477 slot)">'
        + '<span class="if-meta" id="ifaceSizeMeta"></span>'
        + '<button id="ifaceSizeBtn" title="Find components of this size in every open group">Find</button></div>'
        + '<div class="if-list" id="ifaceSizeRes" style="flex:none;max-height:170px"></div>'
        + '<div class="if-bar"><span class="if-meta">Open/close monitor (every 0.6s)</span>'
        + '<button id="ifMonTog" title="Pause/resume the open/close scan">Monitor: on</button>'
        + '<button id="ifMonClear" title="Clear the monitor log">Clear</button></div>'
        + '<div class="if-mon" id="ifMon"></div>'
        + '<div id="ifWatchBox" style="display:none"></div>'
        + '<div class="if-list" id="ifaceList"></div>';
      c.appendChild(wrap);
      $('ifaceSearch').addEventListener('input', renderIfaceList);
      $('ifaceRefresh').addEventListener('click', fetchInterfaceGroups);
      $('ifaceSizeBtn').addEventListener('click', renderIfaceSize);
      $('ifaceSizeIn').addEventListener('keydown', (e) => { if (e.key === 'Enter') renderIfaceSize(); });
      $('ifMonTog').addEventListener('click', () => { ifaceMonOn = !ifaceMonOn; $('ifMonTog').textContent = 'Monitor: ' + (ifaceMonOn ? 'on' : 'off'); if (ifaceMonOn) ifaceMonSeen = null; });
      $('ifMonClear').addEventListener('click', () => { ifaceMon = []; renderIfaceMon(); });
      loadIfaceOff();      // restore + push saved calibration nudges to the reader
      renderIfaceMon();
      // Group headers are rebuilt every render, so the expand click is delegated. Clicks on the offset
      // steppers are ignored (they live below the header, not inside it).
      $('ifaceList').addEventListener('click', (e) => {
        const st = e.target.closest('.if-st');
        if (st) { e.stopPropagation(); ifaceStep(st); return; }
        // Click a widget row with a size -> seed the size search with it (find its mount-slot counterpart).
        const row = e.target.closest('.if-row');
        if (row && row.dataset.w !== undefined) { $('ifaceSizeIn').value = row.dataset.w + 'x' + row.dataset.h; renderIfaceSize(); return; }
        const hdr = e.target.closest('.if-group');
        if (hdr && hdr.dataset.gid) toggleIfaceGroup(parseInt(hdr.dataset.gid, 10));
      });
      // Hovering a widget row with a known absolute rect boxes it in-game; clears on leave. Shared by
      // the group list and the size-search results.
      let _ifHi = '';
      const ifHi = (sig, x, y, w, h) => { if (sig === _ifHi) return; _ifHi = sig; try { bridge().uiHighlight(myPid(), x, y, w, h); } catch (e) {} };
      const ifHover = (e) => {
        const row = e.target.closest('.if-row');
        if (row && row.dataset.ax !== undefined) {
          ifHi(row.dataset.ax + ',' + row.dataset.ay + ',' + row.dataset.aw + ',' + row.dataset.ah,
               +row.dataset.ax, +row.dataset.ay, +row.dataset.aw, +row.dataset.ah);
        } else ifHi('', 0, 0, 0, 0);
      };
      $('ifaceList').addEventListener('mouseover', ifHover);
      $('ifaceList').addEventListener('mouseleave', () => ifHi('', 0, 0, 0, 0));
      $('ifaceSizeRes').addEventListener('mouseover', ifHover);
      $('ifaceSizeRes').addEventListener('mouseleave', () => ifHi('', 0, 0, 0, 0));
      if (!ifaceGroups) fetchInterfaceGroups();      // one cheap fetch on first open
    }
    renderIfaceList();
  }
  function renderIfaceList() {
    const host = document.getElementById('ifaceList'); if (!host) return;
    const groups = ifaceGroups || [];
    const q = (($('ifaceSearch') || {}).value || '').trim();
    const ql = q.toLowerCase();
    let html = '', shown = 0;
    for (const g of groups) {
      const nm = IFACE_NAMES[g.id] || '';
      if (q && String(g.id).indexOf(q) < 0 && nm.toLowerCase().indexOf(ql) < 0) continue;
      shown++;
      const open = !!ifaceOpen[g.id];
      html += '<div class="if-group" data-gid="' + g.id + '">'
        + '<span class="if-caret">' + (open ? '▾' : '▸') + '</span>Group ' + g.id
        + (nm ? '<span class="if-gname">' + nm + '</span>' : '')
        + '<span class="if-n">' + g.n + ' widget' + (g.n === 1 ? '' : 's') + '</span></div>';
      if (open) {
        const o = ifaceOff[g.id] || { x: 0, y: 0 };
        html += '<div class="if-off">offset <b>x</b>'
          + '<button class="if-st" data-gid="' + g.id + '" data-dx="-1">-</button>'
          + '<span class="if-ov">' + (o.x | 0) + '</span>'
          + '<button class="if-st" data-gid="' + g.id + '" data-dx="1">+</button>'
          + '<b>y</b>'
          + '<button class="if-st" data-gid="' + g.id + '" data-dy="-1">-</button>'
          + '<span class="if-ov">' + (o.y | 0) + '</span>'
          + '<button class="if-st" data-gid="' + g.id + '" data-dy="1">+</button>'
          + '<button class="if-st" data-gid="' + g.id + '" data-reset="1">reset</button></div>';
        const ws = ifaceWidgets[g.id];
        if (!ws) html += '<div class="if-empty" style="padding-left:24px">loading...</div>';
        else if (!ws.length) html += '<div class="if-empty" style="padding-left:24px">(no widgets)</div>';
        else for (const w of ws) {
          const t = w.t || [0, 0, 0], r = w.r || [0, 0, 0, 0];
          const pad = 8 + (w.d || 0) * 12;
          const idtxt = t[1] + (t[2] >= 0 ? ':' + t[2] : '');    // component[:sub]; group is in the header
          const full = t[0] + ':' + t[1] + (t[2] >= 0 ? ':' + t[2] : '');  // full path on hover
          // text: strip RS3 colour tags, then HTML-escape for safe display
          const tx = (w.x || '').replace(/<[^>]*>/g, '').replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').trim();
          // absolute screen rect (only known for movable panels whose position varc is resolved, e.g.
          // dialogues / inventory) -> hovering the row boxes the widget in-game
          const a = w.a;
          const absAttr = (a && r[2] > 0 && r[3] > 0) ? ' data-ax="' + a[0] + '" data-ay="' + a[1] + '" data-aw="' + r[2] + '" data-ah="' + r[3] + '"' : '';
          const sizeAttr = (r[2] > 0 && r[3] > 0) ? ' data-w="' + r[2] + '" data-h="' + r[3] + '"' : '';
          html += '<div class="if-row"' + absAttr + sizeAttr + ' title="' + full + (sizeAttr ? '  (click: find this size everywhere' + (absAttr ? '; hover: highlight in-game' : '') + ')' : '') + '" style="padding-left:' + pad + 'px">'
            + '<span class="if-id if-watchable" data-wg="' + t[0] + '" data-wk="' + full + '"'
            + ' title="click: watch this component for 30s">' + idtxt + '</span>'
            + (w.ty ? '<span class="if-ty t-' + w.ty + '">' + w.ty + '</span>' : '')
            + (w.it ? '<span class="if-itemico" data-item="' + w.it + '"></span><span class="if-item">#' + w.it + '</span>' : '')
            + (w.n ? '<span class="if-amt">×' + Number(w.n).toLocaleString() + '</span>' : '')
            // "s" >= 131072 is the reader's synthetic encoding (131072 + item id) of an obj-icon graphic:
            // the widget references an ITEM whose icon the game bakes from its model at runtime, so no such
            // sprite exists in the js5 sprite cache.
            + (w.s ? (w.s >= 131072
                   ? '<span class="if-itemico" data-item="' + (w.s - 131072) + '"></span><span class="if-spr">item icon ' + (w.s - 131072) + '</span>'
                   : '<span class="if-sprico" data-spr="' + w.s + '"></span><span class="if-spr">spr ' + w.s + '</span>')
                   // a graphic with no sprite id: if the cache DEF says this comp is a type-6 MODEL,
                   // show the model id + its pre-rendered icon (modelicons.pack); else plain "dynamic"
                   : (w.ty === 'graphic'
                   ? (((ifaceDefModels[g.id] || {})[t[1]])
                      ? '<span class="if-modelico" data-model="' + ifaceDefModels[g.id][t[1]] + '"></span><span class="if-mdl">model ' + ifaceDefModels[g.id][t[1]] + '</span>'
                      : '<span class="if-dyn">dynamic</span>')
                   : ''))
            + (tx ? '<span class="if-txt">' + tx + '</span>' : '')
            + '<span class="if-rect">' + r[0] + ',' + r[1] + ' · ' + r[2] + '×' + r[3] + '</span></div>';
        }
      }
    }
    host.innerHTML = html || '<div class="if-empty">No interface groups - be in-world, then hit Refresh.</div>';
    host.querySelectorAll('.if-watchable').forEach(el => el.addEventListener('click', ev => {
      ev.stopPropagation();
      ifWatchStart(+el.dataset.wg, el.dataset.wk, el.dataset.wk);
    }));
    host.querySelectorAll('.if-sprico').forEach(el => loadSpriteIcon(el, +el.dataset.spr));
    host.querySelectorAll('.if-itemico').forEach(el => { const u = resolveIcon(+el.dataset.item); if (u) el.style.backgroundImage = "url('" + u + "')"; });
    host.querySelectorAll('.if-modelico').forEach(el => {
      let u = ''; try { u = bridge() && bridge().modelIcon ? bridge().modelIcon(+el.dataset.model) : ''; } catch (e) {}
      if (u) el.style.backgroundImage = "url('" + u + "')";
    });
    const meta = $('ifaceMeta');
    if (meta) meta.textContent = groups.length + ' groups' + (q ? ' · ' + shown + ' shown' : '');
  }

  // Size search: find every component sized WxH across all open groups; pairs a panel to the
  // game-frame slot it mounts into.
  async function renderIfaceSize() {
    const res = document.getElementById('ifaceSizeRes'); if (!res) return;
    const meta = $('ifaceSizeMeta');
    const v = (($('ifaceSizeIn') || {}).value || '').trim();
    const m = v.match(/^(\d+)\s*[x×,\s]\s*(\d+)$/);
    if (!m) { res.innerHTML = ''; if (meta) meta.textContent = v ? 'use WxH' : ''; return; }
    const w = +m[1], h = +m[2];
    if (meta) meta.textContent = 'searching...';
    let data = { matches: [] };
    try { data = JSON.parse(await bridge().interfaceSizeSearch(myPid(), w, h, 0) || '{}'); } catch (e) {}
    const matches = data.matches || [];
    const byG = {};
    for (const mt of matches) (byG[mt.g] = byG[mt.g] || []).push(mt);
    const gids = Object.keys(byG).map(Number).sort((a, b) => a - b);
    let html = '';
    for (const g of gids) {
      const nm = IFACE_NAMES[g] || '';
      html += '<div class="if-group" style="cursor:default;margin-top:2px">Group ' + g
        + (nm ? '<span class="if-gname">' + nm + '</span>' : '')
        + '<span class="if-n">' + byG[g].length + '</span></div>';
      for (const mt of byG[g]) {
        const idtxt = mt.c + (mt.s >= 0 ? ':' + mt.s : '');
        const abs = (mt.ax !== undefined);
        const absAttr = abs ? ' data-ax="' + mt.ax + '" data-ay="' + mt.ay + '" data-aw="' + mt.w + '" data-ah="' + mt.h + '"' : '';
        html += '<div class="if-row"' + absAttr + (abs ? ' title="hover to highlight in-game"' : '') + '>'
          + '<span class="if-id">' + idtxt + '</span>'
          + '<span class="if-rect">' + mt.w + '×' + mt.h + (abs ? '  @ ' + mt.ax + ',' + mt.ay : '') + '</span></div>';
      }
    }
    res.innerHTML = html || '<div class="if-empty">No components of ' + w + '×' + h + ' open right now.</div>';
    if (meta) meta.textContent = matches.length + ' match' + (matches.length === 1 ? '' : 'es') + ' · ' + gids.length + ' group' + (gids.length === 1 ? '' : 's');
  }


