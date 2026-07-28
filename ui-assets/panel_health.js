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
  function renderHealth() {
    const c = $('content');
    let wrap = $('hcWrap');
    if (!wrap) {
      injectStyle('hcCss', `
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
          .hc-btnrow { display: flex; flex-wrap: wrap; gap: 6px; margin: 0 14px 8px; align-items: center; }
          .hc-idin { flex: 1 1 190px; min-width: 0; background: var(--bg-elev); border: 1px solid var(--border);
              border-radius: 7px; color: var(--text); font-size: 12px; padding: 6px 9px; outline: none; }
          .hc-idin:focus { border-color: var(--accent); }
          .hc-hint { margin: 10px 14px; font-size: 11.5px; color: var(--text-dim); line-height: 1.5; }`);
      c.innerHTML = '';
      wrap = document.createElement('div'); wrap.id = 'hcWrap'; wrap.className = 'pane'; c.appendChild(wrap);
    }
    // The chrome (buttons + the id field) is built ONCE and then only updated in place.
    // renderHealth() runs on the 250 ms poll, so rebuilding it every tick would blow away
    // the text field mid-keystroke and drop focus. Only #hcBody is re-rendered.
    let body = document.getElementById('hcBody');
    if (!body) {
      wrap.innerHTML = '';
      const head = document.createElement('div'); head.className = 'hc-head';
      const t = document.createElement('span'); t.className = 'hc-title'; t.textContent = 'Reader health check';
      const btn = document.createElement('button'); btn.className = 'vw-btn'; btn.id = 'hcRunBtn';
      btn.dataset.tip = 'Verify every feature can read the game correctly (useful after a game update)';
      btn.addEventListener('click', hcRun);
      head.appendChild(t); head.appendChild(btn);
      wrap.appendChild(head);

      body = document.createElement('div'); body.id = 'hcBody'; wrap.appendChild(body);
    }
    // in-place label updates, so no element is replaced while it may hold focus
    const runBtn = document.getElementById('hcRunBtn');
    if (runBtn) runBtn.textContent = hcBusy ? 'Checking...' : 'Run check';
    body.innerHTML = '';

    if (!hcData || !Array.isArray(hcData.checks)) {
      const e2 = document.createElement('div'); e2.className = 'hc-hint';
      e2.textContent = 'Checks that every feature can read the game correctly. Worth running after a game update if something looks off.';
      body.appendChild(e2);
      return;
    }
    if (hcData.version) {
      const v = document.createElement('div'); v.className = 'hc-ver';
      v.textContent = 'client ' + hcData.version; body.appendChild(v);
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
    body.appendChild(card);
    const bad = hcData.checks.filter(x => x.ok === 0).length;
    const sum = document.createElement('div'); sum.className = 'hc-sum';
    sum.textContent = bad === 0 ? 'Everything is working.'
      : bad + ' check' + (bad === 1 ? '' : 's') + ' failing - a recent game update may be the cause. An app update will fix this.';
    sum.style.color = bad === 0 ? '#4dd28a' : '#e05656';
    body.appendChild(sum);
  }
