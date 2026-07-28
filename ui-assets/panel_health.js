// RuneToolsX panel: Health check (Developer) -- on-demand diagnostic of every major read chain,
// so after a GAME UPDATE one glance shows which anchor/offset chain broke.
// Spliced inline into client.html; bare classic script sharing one global scope.

  let hcData = null, hcBusy = false;
  async function hcRun() {
    if (hcBusy || !bridge() || !bridge().readerHealth) return;
    hcBusy = true; renderHealth();
    try { hcData = JSON.parse(await bridge().readerHealth(myPid())); } catch (e) { hcData = null; }
    hcBusy = false;
    if (activeTab === 'health') renderHealth();
  }
  // Developer aid: raw Extra_ints for backpack items that pack state into them (rune pouch,
  // quiver, jewellery boxes). Prints id, name and every key -> value so a new container's
  // layout can be read off a known in-game state.
  let hcXBusy = false, hcXOut = '';
  async function hcDumpExtras() {
    if (hcXBusy || !bridge() || !bridge().itemExtraInts) return;
    hcXBusy = true; hcXOut = ''; renderHealth();
    const lines = [];
    try {
      let inv = null;
      try { inv = JSON.parse(await bridge().inventory(myPid())); } catch (e) {}
      const items = (inv && inv.items) || [];
      if (!items.length) lines.push('backpack empty or unreadable');
      const seen = new Set();
      for (const it of items) {
        const id = it[1] | 0, nm = it[3] || ('Item #' + id);
        if (!id || seen.has(id)) continue;
        seen.add(id);
        let ei = null;
        try { ei = JSON.parse(await bridge().itemExtraInts(myPid(), 93, id)); } catch (e) {}
        let ip = null;
        if (bridge().itemParams) { try { ip = JSON.parse(await bridge().itemParams(id) || 'null'); } catch (e) {} }
        const keys = (ei && ei.key) ? Object.keys(ei.key) : [];
        const ipInts = (ip && ip.ints) || (ip && ip.int) || null;
        const ipStrs = (ip && ip.strs) || (ip && ip.str) || null;
        const hasParams = (ipInts && Object.keys(ipInts).length) || (ipStrs && Object.keys(ipStrs).length);
        if (!keys.length && !hasParams) continue;
        lines.push(nm + '  (id ' + id + ')');
        if (ipInts) for (const k of Object.keys(ipInts).sort((a, b) => (+a) - (+b))) lines.push('    param ' + k + ' = ' + ipInts[k]);
        if (ipStrs) for (const k of Object.keys(ipStrs).sort((a, b) => (+a) - (+b))) lines.push('    param ' + k + ' = "' + ipStrs[k] + '"');
        for (const k of keys.sort((a, b) => (+a) - (+b))) {
          const v = ei.key[k] | 0;
          lines.push('    key ' + k + ' = ' + v + '   0x' + (v >>> 0).toString(16) + '   ' + (v >>> 0).toString(2));
        }
        if (ei && ei.pos) lines.push('    pos: ' + JSON.stringify(ei.pos));
      }
      if (!lines.length) lines.push('no backpack item carries Extra_ints');
    } catch (e) { lines.push('error: ' + e); }
    hcXOut = lines.join(String.fromCharCode(10));
    hcShowDump(hcXOut);
    hcXBusy = false; renderHealth();
  }
  // The dump is shown in a body-level overlay rather than inside the panel: renderHealth()
  // runs on the 250 ms poll and rebuilds its whole subtree, which would destroy the element
  // mid-scroll and cancel any in-progress text selection. A readonly textarea keeps its own
  // scroll and supports native select-all + copy.
  function hcShowDump(text) {
    let ov = document.getElementById('hcXOv');
    if (!ov) {
      ov = document.createElement('div'); ov.id = 'hcXOv'; ov.className = 'hcx-ov';
      const box = document.createElement('div'); box.className = 'hcx-box';
      const hd = document.createElement('div'); hd.className = 'hcx-hd';
      hd.innerHTML = '<span>Item Extra_ints + cache params</span>';
      const cp = document.createElement('button'); cp.className = 'vw-btn'; cp.textContent = 'Copy';
      const cl = document.createElement('button'); cl.className = 'vw-btn'; cl.textContent = 'Close';
      hd.appendChild(cp); hd.appendChild(cl);
      const ta = document.createElement('textarea'); ta.id = 'hcXTa'; ta.className = 'hcx-ta'; ta.readOnly = true; ta.spellcheck = false;
      box.appendChild(hd); box.appendChild(ta); ov.appendChild(box); document.body.appendChild(ov);
      cl.addEventListener('click', () => { ov.style.display = 'none'; });
      cp.addEventListener('click', () => {
        ta.focus(); ta.select();
        let ok = false; try { ok = document.execCommand('copy'); } catch (e) {}
        cp.textContent = ok ? 'Copied' : 'Select + Ctrl+C';
        setTimeout(() => { cp.textContent = 'Copy'; }, 1600);
      });
      ov.addEventListener('click', (e) => { if (e.target === ov) ov.style.display = 'none'; });
    }
    ov.style.display = 'flex';
    document.getElementById('hcXTa').value = text;
  }
  function renderHealth() {
    const c = $('content');
    let wrap = $('hcWrap');
    if (!wrap) {
      injectStyle('hcCss', `
        .hcx-ov { position: fixed; inset: 0; z-index: 90; background: rgba(6,8,13,.72); display: none;
            align-items: center; justify-content: center; }
        .hcx-box { width: min(720px, calc(100vw - 40px)); height: min(70vh, 560px); display: flex; flex-direction: column;
            background: var(--bg-elev); border: 1px solid var(--border-hi); border-radius: 10px; }
        .hcx-hd { display: flex; align-items: center; gap: 8px; padding: 10px 12px; border-bottom: 1px solid var(--border);
            color: var(--accent-hi); font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: .05em; }
        .hcx-hd span { flex: 1; }
        .hcx-ta { flex: 1; margin: 0; padding: 10px 12px; border: 0; background: transparent; resize: none;
            color: var(--text); font-family: Consolas, monospace; font-size: 11px; line-height: 1.5;
            white-space: pre; overflow: auto; outline: none; }
        .hc-xdump { margin: 10px 14px; padding: 10px 12px; background: var(--bg-elev); border: 1px solid var(--border);
            border-radius: 8px; color: var(--text); font-family: Consolas, monospace; font-size: 11px;
            line-height: 1.5; white-space: pre; overflow-x: auto; user-select: text; }
          .hc-head { display: flex; align-items: center; gap: 8px; padding: 12px 14px 8px; }
          .hc-title { flex: 1; font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: .05em; color: var(--accent-hi); }
          .hc-ver { padding: 0 14px 8px; font-size: 11px; color: var(--text-dim); }
          .hc-card { margin: 0 12px; background: var(--bg-elev); border: 1px solid var(--border); border-radius: 10px; }
          .hc-row { display: flex; align-items: center; gap: 8px; padding: 7px 12px; font-size: 12px; }
          .hc-row + .hc-row { border-top: 1px solid var(--border); }
          .hc-dot { width: 8px; height: 8px; border-radius: 50%; flex: 0 0 auto; }
          .hc-dot.ok { background: #4dd28a; box-shadow: 0 0 6px rgba(77,210,138,.35); }
          .hc-dot.warn { background: var(--text-dim); }
          .hc-dot.bad { background: #e05656; box-shadow: 0 0 6px rgba(224,86,86,.35); }
          .hc-k { flex: 0 0 auto; color: var(--text); }
          .hc-d { margin-left: auto; color: var(--text-dim); font-size: 11px; text-align: right; overflow: hidden; text-overflow: ellipsis; }
          .hc-sum { margin: 10px 14px 0; font-size: 11.5px; font-weight: 600; }
          .hc-hint { margin: 10px 14px; font-size: 11.5px; color: var(--text-dim); line-height: 1.5; }`);
      c.innerHTML = '';
      wrap = document.createElement('div'); wrap.id = 'hcWrap'; wrap.className = 'pane'; c.appendChild(wrap);
    }
    wrap.innerHTML = '';
    const head = document.createElement('div'); head.className = 'hc-head';
    const t = document.createElement('span'); t.className = 'hc-title'; t.textContent = 'Reader health check';
    const btn = document.createElement('button'); btn.className = 'vw-btn';
    btn.textContent = hcBusy ? 'Checking...' : 'Run check';
    btn.dataset.tip = 'Verify every feature can read the game correctly (useful after a game update)';
    btn.addEventListener('click', hcRun);
    head.appendChild(t); head.appendChild(btn);
    const xbtn = document.createElement('button'); xbtn.className = 'vw-btn';
    xbtn.textContent = hcXBusy ? 'Reading...' : 'Dump item Extra_ints';
    xbtn.dataset.tip = 'Read the packed Extra_ints of every backpack item that has any (pouches, quivers, jewellery boxes)';
    xbtn.addEventListener('click', hcDumpExtras);
    if (hcXOut) {
      const vbtn = document.createElement('button'); vbtn.className = 'vw-btn'; vbtn.textContent = 'View last dump';
      vbtn.addEventListener('click', () => hcShowDump(hcXOut));
      head.appendChild(vbtn);
    }
    head.appendChild(xbtn); wrap.appendChild(head);

    if (!hcData || !Array.isArray(hcData.checks)) {
      const e2 = document.createElement('div'); e2.className = 'hc-hint';
      e2.textContent = 'Checks that every feature can read the game correctly. Worth running after a game update if something looks off.';
      wrap.appendChild(e2);
      return;
    }
    if (hcData.version) {
      const v = document.createElement('div'); v.className = 'hc-ver';
      v.textContent = 'client ' + hcData.version; wrap.appendChild(v);
    }
    const card = document.createElement('div'); card.className = 'hc-card';
    for (const chk of hcData.checks) {
      const r = document.createElement('div'); r.className = 'hc-row';
      const dot = document.createElement('span');
      dot.className = 'hc-dot ' + (chk.ok === 1 ? 'ok' : chk.ok === 2 ? 'warn' : 'bad');
      const nm = document.createElement('span'); nm.className = 'hc-k'; nm.textContent = chk.k;
      const dt = document.createElement('span'); dt.className = 'hc-d'; dt.textContent = chk.d || '';
      r.appendChild(dot); r.appendChild(nm); r.appendChild(dt);
      card.appendChild(r);
    }
    wrap.appendChild(card);
    const bad = hcData.checks.filter(x => x.ok === 0).length;
    const sum = document.createElement('div'); sum.className = 'hc-sum';
    sum.textContent = bad === 0 ? 'Everything is working.'
      : bad + ' check' + (bad === 1 ? '' : 's') + ' failing - a recent game update may be the cause. An app update will fix this.';
    sum.style.color = bad === 0 ? '#4dd28a' : '#e05656';
    wrap.appendChild(sum);
  }
