// RuneToolsX panel: Markers (persistent, shareable tile markers drawn into the game's 3D view).
// Spliced inline into client.html; bare classic script sharing one global scope.

  // Markers are stored by region + local tile (instance-stable). The C++ store is the source of
  // truth; this edits through the bridge, and a configurable keybind marks the live tile.
  const MK_COLORS = [0x46E0C0, 0x57C6E0, 0x8BE05A, 0xF0C04A, 0xE0564E, 0xC07AE0, 0xE0903C, 0xECEFF3];
  let markerKb = { mark: 65, remove: 68, color: 0x46E0C0 };   // A = add, D = delete (default keys)
  let markerList = [], markerVer = -1, markerLoadedPid = -1, markerCapturing = null, markerKbLoaded = false;
  let _mkLabelT = 0, _mkListSig = '';
  // Render markers (and spin up the overlay thread) when the show toggle is on and at least one
  // marker exists. The add/delete keys themselves are always live (handled in the launcher), so
  // the first keybind-placed marker also turns rendering on from the launcher side.
  function markersActive() { return !!overlayState.markers && markerList.length > 0; }
  function mkHex(n) { return '#' + ('000000' + ((n | 0) & 0xFFFFFF).toString(16)).slice(-6); }
  function mkParseColor(c) {
    if (typeof c === 'number') return c & 0xFFFFFF;
    if (typeof c === 'string') { const n = parseInt(c.replace('#', ''), 16); return isNaN(n) ? 0x46E0C0 : (n & 0xFFFFFF); }
    return 0x46E0C0;
  }
  function mkVkName(vk) {
    if (!vk) return 'Unbound';
    if ((vk >= 65 && vk <= 90) || (vk >= 48 && vk <= 57)) return String.fromCharCode(vk);
    if (vk >= 112 && vk <= 123) return 'F' + (vk - 111);
    if (vk >= 96 && vk <= 105) return 'Num ' + (vk - 96);
    const m = { 32: 'Space', 9: 'Tab', 13: 'Enter', 8: 'Backspace', 192: '`', 189: '-', 187: '=', 219: '[', 221: ']', 220: '\\', 186: ';', 222: "'", 188: ',', 190: '.', 191: '/', 45: 'Insert', 46: 'Delete', 36: 'Home', 35: 'End', 33: 'Page Up', 34: 'Page Down' };
    return m[vk] || ('Key ' + vk);
  }
  function loadMarkerKb() {
    if (markerKbLoaded || !bridge() || !bridge().markerKeybindsGet) return;
    markerKbLoaded = true;
    try { const o = JSON.parse(bridge().markerKeybindsGet()); if (o) markerKb = { mark: (o.mark | 0) || 65, remove: (o.remove | 0) || 68, color: (o.color | 0) || 0x46E0C0 }; } catch (e) {}
  }
  function saveMarkerKb() { try { bridge().markerKeybindsSet(markerKb.mark | 0, markerKb.remove | 0, markerKb.color | 0); } catch (e) {} }
  function fetchMarkers(force) {
    if (!bridge() || !bridge().markersGet) return;
    const pid = myPid();
    if (markerLoadedPid !== pid) { markerLoadedPid = pid; markerVer = -1; }
    let ver = -1; try { ver = bridge().markersVersion(pid); } catch (e) {}
    if (!force && ver === markerVer) return;
    markerVer = ver;
    try { const a = JSON.parse(bridge().markersGet(pid)); if (Array.isArray(a)) markerList = a; } catch (e) {}
    paneRun('markers', paintMarkerList);
  }
  function mkBtn(label, fn, cls) {
    const b = document.createElement('button'); b.className = 'mk-btn' + (cls ? ' ' + cls : '');
    b.textContent = label; b.addEventListener('click', fn); return b;
  }
  function mkGrp(t) { const d = document.createElement('div'); d.className = 'mk-grp'; d.textContent = t; return d; }
  function mkPalette(id, onPick) {
    const p = document.createElement('div'); p.className = 'mk-pal'; p.id = id;
    MK_COLORS.forEach(col => {
      const sw = document.createElement('div'); sw.className = 'mk-sw'; sw.style.background = mkHex(col); sw.dataset.col = col;
      sw.addEventListener('click', () => onPick(col));
      p.appendChild(sw);
    });
    return p;
  }
  function paintDefPal() {
    const p = $('mk_defpal'); if (!p) return;
    Array.from(p.children).forEach(sw => sw.classList.toggle('sel', (sw.dataset.col | 0) === (markerKb.color | 0)));
  }
  function mkKbRow(name, which) {
    const r = document.createElement('div'); r.className = 'mk-kbrow';
    const n = document.createElement('div'); n.className = 'mk-kbname'; n.textContent = name;
    const k = document.createElement('div'); k.className = 'mk-key'; k.id = 'mk_key_' + which; k.textContent = mkVkName(markerKb[which]);
    k.addEventListener('click', () => { markerCapturing = (markerCapturing === which) ? null : which; paintKbKeys(); });
    r.appendChild(n); r.appendChild(k); return r;
  }
  function paintKbKeys() {
    ['mark', 'remove'].forEach(w => {
      const k = $('mk_key_' + w); if (!k) return;
      k.textContent = markerCapturing === w ? 'Press a key...' : mkVkName(markerKb[w]);
      k.classList.toggle('capturing', markerCapturing === w);
    });
  }
  // The label lives on the input field, decoupled from the overlay sync. Each keystroke updates
  // the local model and debounce-saves; the list never re-renders a focused input.
  function mkSaveLabel(m, val) {
    m.label = (val || '').slice(0, 64);
    clearTimeout(_mkLabelT);
    _mkLabelT = setTimeout(function () {
      try { bridge().markerSetLabel(myPid(), m.region, m.lx, m.ly, m.plane, m.label); } catch (e) {}
    }, 350);
  }
  function mkCycleColor(m) {
    const cur = mkParseColor(m.color);
    const next = MK_COLORS[(MK_COLORS.indexOf(cur) + 1 + MK_COLORS.length) % MK_COLORS.length];
    // Preserve the second tone across primary-colour cycles.
    const c2 = m.color2 ? mkParseColor(m.color2) : 0;
    try { bridge().markerSetColor(myPid(), m.region, m.lx, m.ly, m.plane, next, c2); } catch (e) {}
    fetchMarkers(true); pushOverlay();
  }
  // Second tone: cycles OFF -> each palette colour -> OFF. Off = a classic single-colour
  // tile; set = the tile splits diagonally between the two colours in-world.
  function mkCycleColor2(m) {
    const cur2 = m.color2 ? mkParseColor(m.color2) : 0;
    const ring = [0].concat(MK_COLORS);
    const next2 = ring[(ring.indexOf(cur2) + 1 + ring.length) % ring.length];
    try { bridge().markerSetColor(myPid(), m.region, m.lx, m.ly, m.plane, mkParseColor(m.color), next2); } catch (e) {}
    fetchMarkers(true); pushOverlay();
  }
  function mkDelete(m) {
    try { bridge().markerRemove(myPid(), m.region, m.lx, m.ly, m.plane); } catch (e) {}
    fetchMarkers(true); pushOverlay();
  }
  function paintMarkerList() {
    const list = $('mk_list'); if (!list) return;
    const g = $('mk_lgrp'); if (g) g.textContent = 'Markers (' + markerList.length + ')';
    // Only rebuild when the marker SET changes (tiles + colours). Labels are edited live on the
    // input, so the 250ms refresh never wipes what is being typed.
    const sig = markerList.map(m => m.region + '/' + m.lx + '/' + m.ly + '/' + m.plane + ':' + mkParseColor(m.color)
      + (m.color2 ? '~' + mkParseColor(m.color2) : '')).join(',');
    if (sig === _mkListSig && list.childElementCount) return;
    _mkListSig = sig;
    const act = document.activeElement;
    const focusKey = (act && act.classList && act.classList.contains('mk-lbl')) ? act.dataset.k : null;
    const caret = focusKey ? act.selectionStart : 0;
    list.innerHTML = '';
    if (!markerList.length) {
      const e = document.createElement('div'); e.className = 'mk-empty';
      e.textContent = 'No markers yet. Hover a tile in-game and press your add key.';
      list.appendChild(e); return;
    }
    markerList.forEach(m => {
      const key = m.region + '/' + m.lx + '/' + m.ly + '/' + m.plane;
      const row = document.createElement('div'); row.className = 'mk-item';
      const sw = document.createElement('div'); sw.className = 'mk-sw'; sw.title = 'Click to change colour';
      // Two-tone markers show their diagonal split right on the swatch.
      sw.style.background = m.color2
        ? 'linear-gradient(135deg, ' + mkHex(mkParseColor(m.color)) + ' 50%, ' + mkHex(mkParseColor(m.color2)) + ' 50%)'
        : mkHex(mkParseColor(m.color));
      sw.addEventListener('click', () => mkCycleColor(m));
      const sw2 = document.createElement('div'); sw2.className = 'mk-sw';
      sw2.title = 'Second colour (two-tone tile) - click to cycle, back to off';
      sw2.style.cssText = 'width:11px;height:11px;flex:0 0 auto;';
      if (m.color2) sw2.style.background = mkHex(mkParseColor(m.color2));
      else { sw2.style.background = 'transparent'; sw2.style.border = '1px dashed rgba(255,255,255,0.35)'; }
      sw2.addEventListener('click', () => mkCycleColor2(m));
      const lbl = document.createElement('input'); lbl.className = 'mk-lbl'; lbl.value = m.label || ''; lbl.placeholder = 'Label...'; lbl.dataset.k = key; lbl.maxLength = 64;
      lbl.addEventListener('input', () => mkSaveLabel(m, lbl.value));
      const co = document.createElement('div'); co.className = 'mk-co';
      co.textContent = 'R' + m.region + ' (' + m.lx + ',' + m.ly + ')' + (m.plane ? ' p' + m.plane : '');
      const del = document.createElement('button'); del.className = 'mk-del'; del.innerHTML = '&times;'; del.title = 'Delete';
      del.addEventListener('click', () => mkDelete(m));
      row.appendChild(sw); row.appendChild(sw2); row.appendChild(lbl); row.appendChild(co); row.appendChild(del);
      list.appendChild(row);
      if (focusKey === key) { lbl.focus(); try { lbl.setSelectionRange(caret, caret); } catch (e) {} }
    });
  }
  function mkExport() {
    // Element 6 = the optional second tone; older builds' import reads a[0..5] and
    // simply ignores it, so shared codes stay cross-version compatible.
    const payload = { v: 1, m: markerList.map(x => [x.region, x.lx, x.ly, x.plane, mkParseColor(x.color), x.label || '',
                                                    x.color2 ? mkParseColor(x.color2) : 0]) };
    let code;
    try { code = 'RTXM1:' + btoa(unescape(encodeURIComponent(JSON.stringify(payload)))); } catch (e) { code = JSON.stringify(payload); }
    const ta = $('mk_share'); if (ta) { ta.value = code; ta.focus(); ta.select(); }
  }
  function mkImport() {
    const ta = $('mk_share'); if (!ta) return;
    let s = (ta.value || '').trim(); if (!s) return;
    let obj = null;
    try {
      if (s.indexOf('RTXM1:') === 0) s = s.slice(6);
      let json; try { json = decodeURIComponent(escape(atob(s))); } catch (e) { json = s; }
      obj = JSON.parse(json);
    } catch (e) { obj = null; }
    let rows = [];
    if (obj && Array.isArray(obj.m)) rows = obj.m.map(a => ({ region: a[0] | 0, lx: a[1] | 0, ly: a[2] | 0, plane: a[3] | 0, color: (a[4] | 0) || 0x46E0C0, label: a[5] || '', color2: a[6] | 0 }));
    else if (Array.isArray(obj)) rows = obj.map(a => ({ region: a.region | 0, lx: a.lx | 0, ly: a.ly | 0, plane: a.plane | 0, color: mkParseColor(a.color), label: a.label || '' }));
    if (!rows.length) { ta.value = '(nothing to import)'; return; }
    const pid = myPid();
    rows.forEach(r => { try {
      bridge().markerAdd(pid, r.region, r.lx, r.ly, r.plane, r.color, r.label);
      if (r.color2) bridge().markerSetColor(pid, r.region, r.lx, r.ly, r.plane, r.color, r.color2);
    } catch (e) {} });
    fetchMarkers(true); pushOverlay(); ta.value = '';
  }
  function mkClearAll() {
    try { bridge().markersClear(myPid()); } catch (e) {}
    fetchMarkers(true); pushOverlay();
  }
  function renderMarkers() {
    const c = $('content');
    if ($('mkWrap')) { paintMarkerList(); return; }
    loadMarkerKb(); fetchMarkers(true);
    c.innerHTML = '';
    const wrap = document.createElement('div'); wrap.id = 'mkWrap'; wrap.className = 'ov-wrap';
    wrap.appendChild(ovToggleRow('mk_show', 'Show markers', 'Draw your tile markers into the game\'s 3D view'));

    wrap.appendChild(mkGrp('Keys'));
    wrap.appendChild(mkKbRow('Add tile at cursor', 'mark'));
    wrap.appendChild(mkKbRow('Delete tile at cursor', 'remove'));
    const dcl = document.createElement('div'); dcl.className = 'mk-grp'; dcl.textContent = 'Default marker colour'; wrap.appendChild(dcl);
    wrap.appendChild(mkPalette('mk_defpal', col => { markerKb.color = col; saveMarkerKb(); paintDefPal(); }));

    const lg = mkGrp('Markers'); lg.id = 'mk_lgrp'; wrap.appendChild(lg);
    const list = document.createElement('div'); list.className = 'mk-list'; list.id = 'mk_list'; wrap.appendChild(list);
    const tbtns = document.createElement('div'); tbtns.className = 'mk-btns';
    tbtns.appendChild(mkBtn('Export', mkExport));
    tbtns.appendChild(mkBtn('Import', mkImport));
    tbtns.appendChild(mkBtn('Clear all', mkClearAll));
    wrap.appendChild(tbtns);
    const ta = document.createElement('textarea'); ta.className = 'mk-share'; ta.id = 'mk_share';
    ta.placeholder = 'Share code. Export writes it here to copy; paste one here then Import.';
    wrap.appendChild(ta);
    c.appendChild(wrap);

    $('mk_show').classList.toggle('on', !!overlayState.markers);
    $('mk_show').addEventListener('click', () => { overlayState.markers = !overlayState.markers; $('mk_show').classList.toggle('on', overlayState.markers); pushOverlay(); });
    paintDefPal(); paintKbKeys(); paintMarkerList();
  }
