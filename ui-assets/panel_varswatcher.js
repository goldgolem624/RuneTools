// RuneToolsX panel: Vars watcher (live varp/varc feed for RE).
// Spliced inline into client.html at load; shares its global scope (no IIFE).

  function vwHex(v)   { return '0x' + ((v >>> 0).toString(16).toUpperCase().padStart(8, '0')); }
  function vwBin(v)   { return (v >>> 0).toString(2).padStart(32, '0').match(/.{4}/g).join(' '); }
  function vwShort(v) { return String(v); }
  function vwEscHtml(s) { return htmlEsc(s); }
  // String varc values (scope 2) arrive as JS strings, ints as numbers.
  function vwVal(v) { return typeof v === 'string' ? vwEscHtml(v) : vwShort(v); }

  // Scopes: 4 = varp, 5 = varc-int, 2 = varc-string. varps poll the hashmap directly; varc also
  // comes from the companion observer, gated on this tab being open.
  let varDump = null, varLast = {}, varRowEls = new Map(), varFilter = '', varPaused = false, varFetching = false, _varAt = 0, varcAvail = false;
  // 64-bit varps: offer price 137, market price 140, offer snapshot 9458, recent trading price
  // 13483, plus probe regions 12788-12795 (GE-slot shaped, 12788 = unix seconds) and 10853-10896.
  const VW_LONG_VARPS = '137,140,9458,13483,5149,5150,6437,6438,12325,12326,12327,' +
    Array.from({ length: 8 }, (_, i) => 12788 + i).join(',') + ',' +
    Array.from({ length: 44 }, (_, i) => 10853 + i).join(',');
  let varLongs = null;   // id -> decimal string (may exceed double-exact range)
  let varPrev = {}, varChangeAt = {};
  let varPopTimer = 0;
  let varPopKey = '', varPopAnchor = null;
  let varNoise = {}, varNoisy = new Set(), varBlock = new Set(), varTimerAllow = new Set(), varWatchOn = false, varTypeFilter = '', varbitMapData = null;
  let varFeedHover = false;                            // pointer inside the feed -> freeze row ORDER
  // Pinned watches sit at the top in stable order, bypassing every filter / cap / timer-hide.
  let varPinned = new Set();
  try { const vp = JSON.parse(localStorage.getItem('rtxVarPins') || '[]'); if (Array.isArray(vp)) varPinned = new Set(vp); } catch (e) {}
  // Pins live in the durable store (%USERPROFILE%/RuneToolsX/varpins.json) because localStorage is
  // wiped when the ui-assets are re-extracted on update; the mirror is read before bridge() exists.
  function varPinsSave() {
    const s = JSON.stringify([...varPinned]);
    try { if (bridge() && bridge().varPinsSave) bridge().varPinsSave(s); } catch (e) {}
    try { localStorage.setItem('rtxVarPins', s); } catch (e) {}
  }
  let varPinsSynced = false;
  function varPinsSync() {
    if (varPinsSynced || !bridge() || !bridge().varPinsLoad) return;
    varPinsSynced = true;
    let arr = null;
    try { arr = JSON.parse(bridge().varPinsLoad() || '[]'); } catch (e) {}
    if (Array.isArray(arr) && arr.length) varPinned = new Set(arr);                       // durable store wins
    else if (varPinned.size) { try { bridge().varPinsSave(JSON.stringify([...varPinned])); } catch (e) {} }   // migrate localStorage -> durable
  }
  // Ground-truth var names from the CS2 extraction (%USERPROFILE%\RuneToolsX\cs2\names.json).
  let varNamesData = null, varNamesLower = null, varNamesTried = false;
  function varNamesLoad() {
    if (varNamesTried || !bridge() || !bridge().cs2Names) return;
    varNamesTried = true;
    try {
      const d = JSON.parse(bridge().cs2Names() || '{}');
      varNamesData = { varbit: d.varbit || {}, varp: d.varp || {}, varc: d.varc || {} };
      varNamesLower = { varbit: {}, varp: {}, varc: {} };
      for (const k in varNamesData.varbit) varNamesLower.varbit[k] = varNamesData.varbit[k].toLowerCase();
      for (const k in varNamesData.varp) varNamesLower.varp[k] = varNamesData.varp[k].toLowerCase();
      for (const k in varNamesData.varc) varNamesLower.varc[k] = varNamesData.varc[k].toLowerCase();
      varRowEls.forEach(el => { el._sig = ''; });
      paneRun('vars', paintVars);
    } catch (e) { varNamesData = null; }
  }
  function varpName(id)   { return (varNamesData && varNamesData.varp[id]) || (varAchData && varAchData.vp[id]) || ''; }
  function varbitName(id) { return (varNamesData && varNamesData.varbit[id]) || (varAchData && varAchData.vb[id]) || ''; }
  function varcName(id)   { return (varNamesData && varNamesData.varc[id]) || ''; }
  // Achievement-derived meanings from the cache (js5-57): op-14 reqs name the varbit(s) they track,
  // op-25 single bits WITHIN a varbit's value, op-23 single bits of a VARP, op-13 whole varps.
  // CS2 source names win on overlap.
  let varAchData = null, varAchTried = false;   // { vb, vp, bits, vpbits, *L lowercased for search }
  function varAchLoad() {
    if (varAchTried || !bridge() || !bridge().achievements) return;
    varAchTried = true;
    (async () => {
      try {
        if (!achDefs) { try { achDefs = JSON.parse(await bridge().achievements()) || []; } catch (e) { achDefs = []; } }
        // achBitReqs classifies legacy merged bit reqs via storageVbMap, so it must be loaded first
        await ensureVbMap();
        const vb = {}, vp = {}, bits = {}, vpbits = {};
        for (const a of (achDefs || [])) {
          if (achIsLeagues(a)) continue;
          for (const q of (a.reqs || [])) {
            const lbl = q.desc ? q.desc + ' (' + a.name + ')' : a.name;
            for (const v of q.varbits) if (vb[v] === undefined) vb[v] = lbl;
          }
          const br = achBitReqs(a);
          for (const q of br.vbits) {
            (bits[q.vb] = bits[q.vb] || []).push({ bit: q.bit, label: q.n ? q.n + ' (' + a.name + ')' : a.name });
            if (vb[q.vb] === undefined) vb[q.vb] = a.name;   // varbit-level fallback: the achievement it belongs to
          }
          for (const q of br.vpbits)
            (vpbits[q.vp] = vpbits[q.vp] || []).push({ bit: q.bit, label: q.n ? q.n + ' (' + a.name + ')' : a.name });
          for (const q of achVarpReqs(a))
            for (const id of q.vps) if (vp[id] === undefined) vp[id] = q.n ? q.n + ' (' + a.name + ')' : a.name;
        }
        for (const k in bits) bits[k].sort((x, y) => x.bit - y.bit);
        for (const k in vpbits) vpbits[k].sort((x, y) => x.bit - y.bit);
        const low = (o) => { const r = {}; for (const k in o) r[k] = o[k].toLowerCase(); return r; };
        const joinL = (o) => { const r = {}; for (const k in o) r[k] = o[k].map(b => b.label.toLowerCase()).join('\n'); return r; };
        varAchData = { vb, vp, bits, vpbits, vbL: low(vb), vpL: low(vp), bitsL: joinL(bits), vpbitsL: joinL(vpbits) };
        varRowEls.forEach(el => { el._sig = ''; });
        paneRun('vars', paintVars);
      } catch (e) { varAchData = null; }
    })();
  }
  const VAR_NOISE_THRESH = 5.0, VAR_FLASH_MS = 1400, VAR_RECENT_MS = 30000;
  function varTypeLabel(t) {
    if (t === 4) return { label: 'varp', cls: 'varp' };
    if (t === 5) return { label: 'varc', cls: 'varc' };
    if (t === 2) return { label: 'varc·s', cls: 'varc' };
    return { label: 't' + t, cls: 'other' };
  }
  function varsWatchSet(on) {
    if (varWatchOn === on) return; varWatchOn = on;
    try { if (bridge() && bridge().varsWatch) bridge().varsWatch(myPid(), on); } catch (e) {}
    if (on) { varNamesLoad(); varAchLoad(); varPinsSync(); }
    if (on && !varbitMapData && bridge() && bridge().varbitMap) {     // for the varp -> varbit hover decode
      try { Promise.resolve(bridge().varbitMap()).then(j => {
        try { varbitMapData = JSON.parse(j); varRowEls.forEach(el => { el._sig = ''; }); paneRun('vars', paintVars); } catch (e) {}
      }); } catch (e) {}
    }
  }
  // varbits that live in varp `varpId`, each decoded from `value` -> [{id, lsb, msb, val}]
  function varpVarbits(varpId, value) {
    const defs = varbitMapData && varbitMapData[varpId]; if (!defs) return [];
    return defs.map(d => { const lsb = d[1], w = d[2] - d[1], mask = w >= 31 ? 0xffffffff : ((1 << (w + 1)) - 1);
      return { id: d[0], lsb, msb: d[2], val: (value >>> lsb) & mask }; });
  }
  async function fetchVars() {
    if (!bridge() || varFetching || varPaused) return;
    if (!bridge().varpsDumpAll && !bridge().varsDump && !bridge().varcsDumpAll && !bridge().varcStringsDumpAll) return;
    const now = Date.now(); if (now - _varAt < 250) return; _varAt = now;
    varFetching = true;
    let dv = null, dc = null, dx = null, dxs = null;
    try { if (bridge().varpsDumpAll) dv = JSON.parse(await bridge().varpsDumpAll(myPid())); } catch (e) {}
    // The flat dump truncates 64-bit varps to their low 32 bits, so long ids are re-read full width.
    try { if (bridge().varpsLong) varLongs = JSON.parse(await bridge().varpsLong(myPid(), VW_LONG_VARPS)); } catch (e) {}
    try { if (bridge().varcsDumpAll) dx = JSON.parse(await bridge().varcsDumpAll(myPid())); } catch (e) {}
    try { if (bridge().varcStringsDumpAll) dxs = JSON.parse(await bridge().varcStringsDumpAll(myPid())); } catch (e) {}  // varc-string, keys "2:<id>"
    try { if (bridge().varsDump)     dc = JSON.parse(await bridge().varsDump(myPid())); } catch (e) {}
    varFetching = false;
    varcAvail = !!(dx && typeof dx === 'object' && Object.keys(dx).length) ||
                !!(dxs && typeof dxs === 'object' && Object.keys(dxs).length) ||
                !!(dc && typeof dc === 'object' && Object.keys(dc).length);
    // The companion walk (dc) is the only source for varc-strings and the getStorage-only varps the
    // flat hashmap misses (172/2017), but its type-4 captures include script-local scopes whose ids
    // collide with player varps, so the direct polls must win on overlap.
    let d = null;
    if ((dv && typeof dv === 'object') || (dc && typeof dc === 'object') || (dx && typeof dx === 'object') || (dxs && typeof dxs === 'object')) {
      d = {};
      if (dc && typeof dc === 'object') Object.assign(d, dc);
      if (dxs && typeof dxs === 'object') Object.assign(d, dxs);
      if (dv && typeof dv === 'object') Object.assign(d, dv);
      if (dx && typeof dx === 'object') Object.assign(d, dx);
    }
    if (!d || typeof d !== 'object') return;
    for (const k in varNoise) { varNoise[k] *= 0.82; if (varNoise[k] < 0.05) delete varNoise[k]; }
    for (const k in d) {
      if (varLast[k] !== undefined && varLast[k] !== d[k]) {
        varPrev[k] = varLast[k];
        varChangeAt[k] = now;
        varNoise[k] = (varNoise[k] || 0) + 1;
        if (varNoise[k] >= VAR_NOISE_THRESH) varNoisy.add(k);
      }
    }
    varLast = d; varDump = d;
    paneRun('vars', renderVars);
  }
  function renderVars() {
    const c = $('content');
    let wrap = $('vrWrap');
    if (!wrap) {
      c.innerHTML = '';
      wrap = document.createElement('div'); wrap.id = 'vrWrap'; wrap.className = 'vw-wrap';
      const top = document.createElement('div'); top.className = 'vw-top';
      const inp = document.createElement('input'); inp.id = 'vrSearch'; inp.className = 'vw-search';
      inp.type = 'text'; inp.placeholder = 'Search id, value, name, or =N for value-only (=1500 finds where 1500 lives)'; inp.spellcheck = false; inp.value = varFilter;
      inp.addEventListener('input', () => { varFilter = inp.value; paintVars(); });
      const clearBtn = (fn) => { varRowEls.forEach(el => el.remove()); varRowEls.clear(); fn(); };
      const pause = document.createElement('button'); pause.id = 'vrPause'; pause.className = 'vw-btn';
      pause.title = 'Freeze updates';
      pause.addEventListener('click', () => { varPaused = !varPaused; paintVars(); });
      top.appendChild(inp); top.appendChild(pause);
      const flt = document.createElement('div'); flt.className = 'vw-flt';
      [['All', ''], ['varp', '4'], ['varc', '5']].forEach(([lbl, v]) => {
        const ch = document.createElement('button'); ch.className = 'vw-chip'; ch.textContent = lbl;
        ch.setAttribute('data-tf', v);
        ch.addEventListener('click', () => { varTypeFilter = v; clearBtn(paintVars); });
        flt.appendChild(ch);
      });
      const meta = document.createElement('div'); meta.id = 'vrMeta'; meta.className = 'vw-meta';
      const feed = document.createElement('div'); feed.id = 'vrFeed'; feed.className = 'vw-feed';
      feed.addEventListener('scroll', hideVarbitPop);
      // Freeze row ORDER while the pointer is in the feed: values still update in place.
      feed.addEventListener('mouseenter', () => { varFeedHover = true; });
      feed.addEventListener('mouseleave', () => { varFeedHover = false; paintVars(); });
      feed.addEventListener('click', (e) => {
        const pn = e.target.closest && e.target.closest('[data-pin]');
        if (pn) {
          const k = pn.getAttribute('data-pin');
          if (varPinned.has(k)) varPinned.delete(k); else varPinned.add(k);
          varPinsSave();
          const el = varRowEls.get(k); if (el) el._sig = '';
          varFeedHover = false;                   // let the row jump to / from the pinned block now
          paintVars();
          return;
        }
        const b = e.target.closest && e.target.closest('[data-blk]'); if (!b) return;
        const k = b.getAttribute('data-blk'); varBlock.add(k);
        varPinned.delete(k); varPinsSave();
        const el = varRowEls.get(k); if (el) { el.remove(); varRowEls.delete(k); }
        hideVarbitPop(); paintVars();
      });
      wrap.appendChild(top); wrap.appendChild(flt); wrap.appendChild(meta); wrap.appendChild(feed);
      c.appendChild(wrap);
    }
    paintVars();
  }
  function paintVars() {
    const feed = $('vrFeed'); if (!feed) return;
    const set = (id, on, a, b) => { const e = $(id); if (e) { e.textContent = on ? a : b; e.classList.toggle('on', on); } };
    set('vrPause', varPaused, 'Paused', 'Pause');
    document.querySelectorAll('.vw-flt .vw-chip').forEach(c => c.classList.toggle('on', c.getAttribute('data-tf') === varTypeFilter));
    const now = Date.now();
    const f = varFilter.trim();
    const rows = [], pinnedRows = []; let timerCount = 0;
    if (varDump) {
      for (const k in varDump) {
        if (varBlock.has(k)) continue;
        // Pinned watches bypass every filter and cap: an explicit watch must never disappear.
        if (varPinned.has(k)) {
          const pp = k.split(':');
          const at2 = varChangeAt[k] || 0, ago2 = at2 ? (now - at2) : Infinity;

          pinnedRows.push({ key: k, scope: +pp[0], id: +pp[1], cur: varDump[k], prev: varPrev[k], at: at2, ago: ago2 });
          continue;
        }
        // Auto-hidden timer noise, bypassed by a search so a filtered var shows even if it flips fast.
        if (!f && varNoisy.has(k) && !varTimerAllow.has(k)) { timerCount++; continue; }
        const p = k.split(':'); const scope = +p[0], id = +p[1];
        if (varTypeFilter) {                          // "varc" groups varc-int (5) + varc-string (2)
          const want = +varTypeFilter;
          if (want === 5 ? (scope !== 5 && scope !== 2) : (scope !== want)) continue;
        }
        if (f && f[0] === '=') {
          // STRICT VALUE SEARCH: '=1500' matches by VALUE only, never by id. Covers the
          // raw value, either 16-bit half, x10/x100 scalings (prayer-style packings) and
          // every varbit decoded out of a varp. For hunting where an on-screen number
          // lives: search '=N', change the number in game, search again, intersect.
          const want = parseInt(f.slice(1), 10);
          if (!Number.isFinite(want)) continue;
          const v = varDump[k] | 0;
          let m2 = v === want || (v & 0xffff) === want || ((v >>> 16) & 0xffff) === want
                || v === want * 10 || v === want * 100;
          if (!m2 && scope === 4 && varbitMapData)
            m2 = varpVarbits(id, v).some(b => b.val === want || b.val === want * 10);
          if (!m2) continue;
        } else if (f) {
          // Match by id substring, or (for a varp) an exact varbit # it owns.
          let m = String(id).indexOf(f) >= 0;
          if (!m && scope === 4 && varbitMapData) { const defs = varbitMapData[id]; if (defs && defs.some(d => d[0] === +f)) m = true; }
          // Name substring: the varp's name, a named varbit in it, or a named achievement bit of it.
          if (!m && (scope === 5 || scope === 2) && varNamesLower) {
            const cn = varNamesLower.varc[id];
            if (cn && cn.indexOf(f.toLowerCase()) >= 0) m = true;
          }
          if (!m && scope === 4 && (varNamesLower || varAchData)) {
            const fl = f.toLowerCase();
            const pn = (varNamesLower && varNamesLower.varp[id]) || (varAchData && varAchData.vpL[id]);
            if (pn && pn.indexOf(fl) >= 0) m = true;
            if (!m && varAchData && varAchData.vpbitsL[id] && varAchData.vpbitsL[id].indexOf(fl) >= 0) m = true;
            if (!m && varbitMapData) {
              const defs = varbitMapData[id];
              if (defs && defs.some(d => {
                const vn = (varNamesLower && varNamesLower.varbit[d[0]]) || (varAchData && varAchData.vbL[d[0]]);
                if (vn && vn.indexOf(fl) >= 0) return true;
                const bl = varAchData && varAchData.bitsL[d[0]];   // named bits inside the varbit
                return !!(bl && bl.indexOf(fl) >= 0);
              })) m = true;
            }
          }
          // A numeric query also matches by VALUE: exact, or a component of a (plane<<28)|(x<<14)|y coord.
          if (!m && /^\d+$/.test(f)) {
            const fn = +f, v = varDump[k] | 0;
            if (v === fn || (v & 0x7FFFFFFF) === fn) m = true;                 // exact value (flag bit ignored)
            else if (((v >> 14) & 16383) === fn || (v & 16383) === fn) m = true;   // coord x / y component
          }
          if (!m) continue;
        }
        const at = varChangeAt[k] || 0, ago = at ? (now - at) : Infinity;

        rows.push({ key: k, scope, id, cur: varDump[k], prev: varPrev[k], at, ago });
      }
    }
    // Recently changed float to the top, everything else in id order; pinned rows sit above both.
    rows.sort((a, b) => (b.at - a.at) || (a.id - b.id));
    pinnedRows.sort((a, b) => (a.scope - b.scope) || (a.id - b.id));
    const cap = 500;
    let capped = false;
    if (rows.length > cap) { rows.length = cap; capped = true; }
    const meta = $('vrMeta');
    if (meta) {
      meta.innerHTML = '';
      const parts = [];
      if (varDump === null) parts.push('no var data - are you logged in?');
      if (capped) parts.push('top ' + cap + ' (search to narrow)');
      if (varDump !== null && !varcAvail) parts.push('varc off');
      if (varPaused) parts.push('paused');
      meta.appendChild(document.createTextNode(parts.join(' · ')));
      const hiddenCount = varBlock.size + timerCount;
      if (varDump !== null && hiddenCount) {
        const un = document.createElement('span');
        un.textContent = (parts.length ? ' · ' : '') + hiddenCount + ' ignored';
        un.style.cursor = 'pointer'; un.style.textDecoration = 'underline';
        un.title = 'View / unhide ignored vars';
        un.addEventListener('click', showVarIgnoredModal);
        meta.appendChild(un);
      }
    }
    const all = pinnedRows.concat(rows);
    let empty = $('vrEmpty');
    if (!all.length) {
      varRowEls.forEach(el => el.remove()); varRowEls.clear();
      if (!empty) { empty = document.createElement('div'); empty.id = 'vrEmpty'; empty.className = 'vw-empty'; feed.appendChild(empty); }
      empty.textContent = varDump === null ? 'No var data - are you logged in?' : 'No vars match this search.';
      return;
    }
    if (empty) empty.remove();
    // Order freeze: refresh visible rows in place, no reordering or add/remove, until the pointer leaves.
    if (varFeedHover) {
      for (const r of all) { const el = varRowEls.get(r.key); if (el) renderRow(el, r); }
      updateVarPop();
      return;
    }
    const seen = new Set(); let prev = null;
    for (const r of all) {
      seen.add(r.key);
      let el = varRowEls.get(r.key);
      if (!el) {
        el = document.createElement('div'); el.className = 'vw-row'; el._sig = ''; el._flashAt = 0;
        if (r.scope === 4) {
          el.addEventListener('mouseenter', () => { clearTimeout(varPopTimer); showVarbitPop(el, r.key); });
          el.addEventListener('mouseleave', () => { varPopTimer = setTimeout(hideVarbitPop, 260); });   // grace period to move into the popover
        }
        varRowEls.set(r.key, el);
      }
      renderRow(el, r);
      const want = prev ? prev.nextSibling : feed.firstChild;
      if (want !== el) feed.insertBefore(el, want);
      prev = el;
    }
    for (const [k, el] of varRowEls) { if (!seen.has(k)) { el.remove(); varRowEls.delete(k); } }
    updateVarPop();
  }
  function renderRow(el, r) {
    const changed = r.ago < VAR_RECENT_MS && r.prev !== undefined && r.prev !== r.cur;
    if (r.ago < VAR_FLASH_MS) {                    // (re)trigger the flash on each distinct change
      if (el._flashAt !== r.at) { el.classList.remove('flash'); void el.offsetWidth; el.classList.add('flash'); el._flashAt = r.at; }
    } else { el.classList.remove('flash'); el._flashAt = 0; }
    el.classList.toggle('changed', changed);
    const pinned = varPinned.has(r.key);
    el.classList.toggle('pinned', pinned);
    const agoTxt = changed ? (r.ago < 1000 ? 'now' : Math.round(r.ago / 1000) + 's') : '';
    // A long varp disagreeing with the truncated dump shows full width; it joins the sig so the
    // row repaints when only the high bytes move.
    const long64 = (r.scope === 4 && varLongs && varLongs[String(r.id)] !== undefined
                    && varLongs[String(r.id)] !== String(r.cur | 0)) ? varLongs[String(r.id)] : null;
    const sig = r.cur + '|' + (long64 || '') + '|' + (changed ? r.prev : '~') + '|' + agoTxt + '|' + (pinned ? 'p' : '');
    if (el._sig === sig) return;
    el._sig = sig;
    const tl = varTypeLabel(r.scope);
    let nm = r.scope === 4 ? varpName(r.id) :
             (r.scope === 5 || r.scope === 2) ? varcName(r.id) : '';
    // Unnamed varp that just changed: label it with the first named varbit or achievement bit that differs.
    if (!nm && r.scope === 4 && changed && typeof r.cur === 'number' && typeof r.prev === 'number') {
      // named single bit of the varp itself (op-23 reqs)
      const pb = varAchData && varAchData.vpbits[r.id];
      const fp = pb ? pb.find(b => ((r.cur >>> b.bit) & 1) !== ((r.prev >>> b.bit) & 1)) : null;
      if (fp) nm = 'bit ' + fp.bit + ': ' + fp.label;
      if (!nm) {
        const was = {}; varpVarbits(r.id, r.prev).forEach(v => { was[v.id] = v.val; });
        for (const v of varpVarbits(r.id, r.cur)) {
          const w = was[v.id];
          if (v.val === w) continue;
          // a named single bit inside the varbit (op-25) beats the varbit-level label
          const ab = varAchData && varAchData.bits[v.id];
          const fb = (ab && w !== undefined) ? ab.find(b => achBitFromVbVal(v.id, v.val, b.bit) !== achBitFromVbVal(v.id, w, b.bit)) : null;
          if (fb) { nm = 'vb ' + v.id + ' bit ' + fb.bit + ': ' + fb.label; break; }
          const vn = varbitName(v.id); if (vn) { nm = 'vb ' + v.id + ': ' + vn; break; }
        }
      }
    }
    el.innerHTML =
      '<span class="vw-tag ' + tl.cls + '">' + tl.label + '</span>' +
      '<span class="vw-id">' + r.id + '</span>' +
      (nm ? '<span class="vw-name" title="' + vwEscHtml(nm) + '">' + vwEscHtml(nm) + '</span>' : '') +
      '<span class="vw-vals"' + (typeof r.cur === 'string' ? ' title="' + vwEscHtml(r.cur) + '"' : '') + '>' +
        (changed ? '<span class="vw-from">' + vwVal(r.prev) + '</span><span class="vw-arrow">→</span>' : '') +
        '<span class="vw-val">' + (long64 !== null
            ? (+long64).toLocaleString() + ' <span style="color:var(--accent-hi);font-size:9px">64b</span>'
            : vwVal(r.cur)) + '</span>' +
      '</span>' +
      (changed ? '<span class="vw-ago">' + agoTxt + '</span>' : '') +
      '<button class="vw-pin' + (pinned ? ' on' : '') + '" data-pin="' + r.key + '" title="' + (pinned ? 'Unpin' : 'Pin to top (survives filters; never reorders)') + '">' + (pinned ? '✦' : '✧') + '</button>' +
      '<button class="vw-ig" data-blk="' + r.key + '" title="Hide this var">×</button>';
  }
  // Popover content, rebuilt from the current dump. Returns false when the var can't be shown.
  function fillVarbitPop(pop, key) {
    if (!varDump) return false;
    const p = key.split(':'); const scope = +p[0], id = +p[1];
    if (scope !== 4) return false;
    const cur = varDump[key];
    if (typeof cur !== 'number') return false;
    const recent = (Date.now() - (varChangeAt[key] || 0)) < VAR_RECENT_MS;
    const base = (recent && varPrev[key] !== undefined) ? varPrev[key] : cur;
    const vbs = varpVarbits(id, cur), was = {};
    varpVarbits(id, base).forEach(v => { was[v.id] = v.val; });
    // 32-bit breakdown, set bits highlighted; shown even for varps with no named varbits.
    let bits = '';
    for (let b = 31; b >= 0; b--) { if (b !== 31 && (b + 1) % 4 === 0) bits += ' '; bits += (((cur >>> b) & 1) ? '<b>1</b>' : '0'); }
    const pn = varpName(id);
    const html = '<div class="vw-pop-h">varp ' + id + (pn ? ' <span class="vw-pop-nm">' + vwEscHtml(pn) + '</span>' : '') + ' = ' + cur +
      (cur < 0 ? ' (u32 ' + (cur >>> 0) + ')' : '') +    // bit-31 bitfields read negative as int32; not overflow
      ' &nbsp;0x' + (cur >>> 0).toString(16).toUpperCase() + '</div>' +
      '<div class="vw-pop-bits">' + bits + '</div>' +
      // Named single bits of the VARP itself (achievement op-23 reqs).
      ((varAchData && varAchData.vpbits[id]) || []).map(b => {
        const bv = (cur >>> b.bit) & 1, wv = (base >>> b.bit) & 1;
        return '<div class="vw-pl' + (wv !== bv ? ' chg' : '') + '">' +
          '<span class="vw-pl-id">bit ' + b.bit + '</span><span class="vw-pl-b">[' + b.bit + ']</span>' +
          '<span class="vw-pl-nm" title="' + vwEscHtml(b.label) + '">' + vwEscHtml(b.label) + '</span><b>' + bv + '</b></div>';
      }).join('') +
      vbs.map(v => {
        const vn = varbitName(v.id), wasV = was[v.id];
        let h = '<div class="vw-pl' + (wasV !== undefined && wasV !== v.val ? ' chg' : '') + '">' +
          '<span class="vw-pl-id">vb ' + v.id + '</span><span class="vw-pl-b">[' + v.lsb + (v.msb !== v.lsb ? '-' + v.msb : '') + ']</span>' +
          (vn ? '<span class="vw-pl-nm" title="' + vwEscHtml(vn) + '">' + vwEscHtml(vn) + '</span>' : '') + '<b>' + v.val + '</b></div>';
        // Named single bits INSIDE this varbit (op-25 reqs), indexed against the varbit's value.
        const ab = varAchData && varAchData.bits[v.id];
        if (ab) for (const b of ab) {
          const bv = achBitFromVbVal(v.id, v.val, b.bit), wv = achBitFromVbVal(v.id, wasV === undefined ? v.val : wasV, b.bit);
          h += '<div class="vw-pl' + (wv !== bv ? ' chg' : '') + '">' +
            '<span class="vw-pl-id"></span><span class="vw-pl-b">bit ' + b.bit + '</span>' +
            '<span class="vw-pl-nm" title="' + vwEscHtml(b.label) + '">' + vwEscHtml(b.label) + '</span><b>' + bv + '</b></div>';
        }
        return h;
      }).join('') +
      (vbs.length || (varAchData && varAchData.vpbits[id]) ? '' :
        '<div class="vw-pop-e">' + (varbitMapData ? 'no named varbits in this varp' : 'loading varbit names...') + '</div>');
    if (pop._html !== html) { pop._html = html; pop.innerHTML = html; }   // no scroll-position reset on identical content
    return true;
  }
  function showVarbitPop(rowEl, key) {
    if (!varbitMapData && bridge() && bridge().varbitMap) {
      try { Promise.resolve(bridge().varbitMap()).then(j => { try { varbitMapData = JSON.parse(j); } catch (e) {} }); } catch (e) {}
    }
    let pop = $('vrPop');
    if (!pop) {
      pop = document.createElement('div'); pop.id = 'vrPop'; pop.className = 'vw-pop'; document.body.appendChild(pop);
      pop.addEventListener('mouseenter', () => clearTimeout(varPopTimer));   // keep it open so it can be scrolled
      pop.addEventListener('mouseleave', hideVarbitPop);
    }
    if (!fillVarbitPop(pop, key)) return;
    varPopKey = key; varPopAnchor = rowEl;
    pop.style.display = 'block';
    const rc = rowEl.getBoundingClientRect();
    let top = rc.bottom + 4;
    if (top + pop.offsetHeight + 8 > window.innerHeight) top = Math.max(6, rc.top - pop.offsetHeight - 4);
    pop.style.top = top + 'px';
    pop.style.left = Math.max(6, Math.min(rc.left, window.innerWidth - pop.offsetWidth - 8)) + 'px';
    try { wmRectsSoon(); } catch (e) {}
  }
  // Re-render the open popover on every paint so its varbit values stay live without mouse motion.
  function updateVarPop() {
    const pop = $('vrPop');
    if (!pop || pop.style.display === 'none' || !varPopKey) return;
    if (!varPopAnchor || !varPopAnchor.isConnected) { hideVarbitPop(); return; }
    fillVarbitPop(pop, varPopKey);
  }
  function hideVarbitPop() { const pop = $('vrPop'); if (pop) { pop.style.display = 'none'; try { wmRectsSoon(); } catch (e) {} } varPopKey = ''; varPopAnchor = null; }
  // Session-scoped list of hidden vars (manual or auto-hidden timer noise), with unhide controls.
  function showVarIgnoredModal() {
    hideVarIgnoredModal();
    const rowsM = [];
    for (const k of varBlock) rowsM.push({ key: k, kind: 'hidden' });
    if (varDump) for (const k in varDump) {
      if (varBlock.has(k) || varPinned.has(k)) continue;
      if (varNoisy.has(k) && !varTimerAllow.has(k)) rowsM.push({ key: k, kind: 'timer' });
    }
    rowsM.sort((a, b) => { const pa = a.key.split(':'), pb = b.key.split(':'); return (+pa[0] - +pb[0]) || (+pa[1] - +pb[1]); });
    const ov = document.createElement('div'); ov.id = 'vrModalOv'; ov.className = 'vw-modal-ov';
    ov.addEventListener('click', (e) => { if (e.target === ov) hideVarIgnoredModal(); });
    const box = document.createElement('div'); box.className = 'vw-modal';
    const h = document.createElement('div'); h.className = 'vw-modal-h';
    const title = document.createElement('span'); title.textContent = rowsM.length + ' ignored';
    const unAll = document.createElement('button'); unAll.className = 'vw-btn'; unAll.textContent = 'Unhide all';
    unAll.addEventListener('click', () => {
      for (const r of rowsM) { varBlock.delete(r.key); if (r.kind === 'timer') varTimerAllow.add(r.key); }
      hideVarIgnoredModal(); paintVars();
    });
    const close = document.createElement('button'); close.className = 'vw-btn'; close.textContent = 'Close';
    close.addEventListener('click', hideVarIgnoredModal);
    h.appendChild(title); h.appendChild(unAll); h.appendChild(close);
    const list = document.createElement('div'); list.className = 'vw-modal-list';
    if (!rowsM.length) {
      const e2 = document.createElement('div'); e2.className = 'vw-pop-e'; e2.textContent = 'Nothing ignored.';
      list.appendChild(e2);
    }
    for (const r of rowsM) {
      const row = document.createElement('div'); row.className = 'vw-modal-row';
      const p = r.key.split(':'); const tl = varTypeLabel(+p[0]);
      const cur = varDump ? varDump[r.key] : undefined;
      row.innerHTML = '<span class="vw-tag ' + tl.cls + '">' + tl.label + '</span>'
        + '<span class="vw-id">' + (+p[1]) + '</span>'
        + '<span class="vw-mk">' + (r.kind === 'timer' ? 'auto · timer' : 'hidden') + '</span>'
        + '<span class="vw-mv">' + (cur === undefined ? '' : vwVal(cur)) + '</span>';
      const un = document.createElement('button'); un.className = 'vw-btn'; un.textContent = 'Unhide';
      un.addEventListener('click', () => {
        varBlock.delete(r.key);
        if (r.kind === 'timer') varTimerAllow.add(r.key);
        row.remove();
        const left = list.querySelectorAll('.vw-modal-row').length;
        title.textContent = left + ' ignored';
        paintVars();
      });
      row.appendChild(un);
      list.appendChild(row);
    }
    box.appendChild(h); box.appendChild(list);
    ov.appendChild(box);
    document.body.appendChild(ov);
    try { wmRectsSoon(); } catch (e) {}
  }
  function hideVarIgnoredModal() { const ov = $('vrModalOv'); if (ov) { ov.remove(); try { wmRectsSoon(); } catch (e) {} } }
