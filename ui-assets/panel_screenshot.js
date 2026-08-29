// RuneToolsX panel: Screenshot capture. A user-defined keybind (no default) grabs the game
// window and saves a PNG under %USERPROFILE%\RuneToolsX\screenshots, named by date/time.
// The keypress is detected launcher-side (Dock host proc -> captureScreenshot) and the
// result raises the in-game toast (Bridge capture_for_pid), so this panel only configures
// the key and offers a manual capture / open-folder.
// Built ONCE and repainted only on events: renderPane() re-enters every 250ms poll tick, so
// an unconditional DOM write here would churn the panel under the user's cursor.
// Spliced inline into client.html at load; IIFE (window exports + registerTab; see the RTX registry in client.html).
(function () {

  let ssKeybind = 0;          // bound virtual-key code, 0 = unbound
  ssCapturing = false;    // armed "press a key" state (the global keydown guard checks this)
  let ssKbLoaded = false;
  let ssStatusTimer = 0;      // pending auto-hide of the status line

  // VK -> readable label (mirrors the marker panel's mapping).
  function ssVkName(vk) {
    if (!vk) return 'Not set';
    if ((vk >= 65 && vk <= 90) || (vk >= 48 && vk <= 57)) return String.fromCharCode(vk);
    if (vk >= 112 && vk <= 123) return 'F' + (vk - 111);
    if (vk >= 96 && vk <= 105) return 'Num ' + (vk - 96);
    const m = { 32: 'Space', 9: 'Tab', 13: 'Enter', 8: 'Backspace', 192: '`', 189: '-', 187: '=',
      219: '[', 221: ']', 220: '\\', 186: ';', 222: "'", 188: ',', 190: '.', 191: '/',
      45: 'Insert', 46: 'Delete', 36: 'Home', 35: 'End', 33: 'Page Up', 34: 'Page Down',
      44: 'Print Screen', 145: 'Scroll Lock', 19: 'Pause' };
    return m[vk] || ('Key ' + vk);
  }
  function ssLoadKb() {
    if (ssKbLoaded || !bridge() || !bridge().screenshotKeybindGet) return;
    ssKbLoaded = true;
    try { ssKeybind = (rtxData.sync('host.screenshotKeybindGet') | 0) || 0; } catch (e) {}
  }
  function ssSaveKb() { try { if (bridge().screenshotKeybindSet) rtxData.sync('act.screenshotKeybindSet', ssKeybind | 0); } catch (e) {} }

  function ssSetStatus(msg, tone) {
    const s = $('ssStatus'); if (!s) return;
    if (ssStatusTimer) { clearTimeout(ssStatusTimer); ssStatusTimer = 0; }
    s.textContent = msg || '';
    s.className = 'ss-status' + (tone ? ' ' + tone : '') + (msg ? ' show' : '');
    if (msg) ssStatusTimer = setTimeout(() => {
      ssStatusTimer = 0;
      const el = $('ssStatus'); if (el) el.classList.remove('show');
    }, 4500);
  }

  function ssPaint() {
    const chip = $('ssKey'); if (!chip) return;
    chip.textContent = ssCapturing ? 'Press any key' : ssVkName(ssKeybind);
    chip.classList.toggle('capturing', ssCapturing);
    chip.classList.toggle('unbound', !ssCapturing && !ssKeybind);
    const set = $('ssSet'), clr = $('ssClear');
    if (set) set.textContent = ssCapturing ? 'Cancel' : (ssKeybind ? 'Change keybind' : 'Set keybind');
    if (clr) clr.disabled = !ssKeybind || ssCapturing;
  }

  function ssCaptureNow() {
    if (!bridge() || !bridge().captureScreenshot) { ssSetStatus('Capture unavailable.', 'err'); return; }
    ssSetStatus('Capturing...');
    // Defer one frame so the status paints before the synchronous bridge grab.
    setTimeout(() => {
      let path = '';
      try { path = rtxData.sync('act.captureScreenshot') || ''; } catch (e) {}
      if (path) ssSetStatus('Saved ' + path.split(/[\\/]/).pop(), 'ok');
      else ssSetStatus('Capture failed. Is the game window open?', 'err');
    }, 30);
  }

  function renderScreenshot() {
    // The keybind can load late (no bridge on the first paint); refresh the chip when it lands.
    if (!ssKbLoaded) { ssLoadKb(); if (ssKbLoaded && $('ssWrap')) ssPaint(); }
    if ($('ssWrap')) return;   // static panel: every later repaint is event-driven

    injectStyle('ssCss', `
        .ss-card { margin: 6px 12px 0; padding: 12px; background: var(--bg-elev); border: 1px solid var(--border); border-radius: 10px; display: flex; flex-direction: column; gap: 10px; }
        .ss-desc { color: var(--text-dim); font-size: 11.5px; line-height: 1.5; }
        .ss-row { display: flex; align-items: center; gap: 8px; }
        .ss-label { color: var(--text); font-size: 13px; flex: 1; }
        .ss-chip { min-width: 96px; text-align: center; padding: 5px 12px; border-radius: 7px; background: var(--bg); border: 1px solid var(--border-hi); color: var(--text); font-size: 12px; font-weight: 600; }
        .ss-chip.unbound { color: var(--text-mute); font-weight: 400; border-color: var(--border); }
        .ss-chip.capturing { border-color: var(--accent-hi); color: var(--accent-hi); }
        .ss-btn { flex: 1; padding: 7px 12px; border-radius: 7px; background: var(--bg-elev-2); border: 1px solid var(--border); color: var(--text); font-size: 12px; cursor: pointer; transition: border-color var(--tx-fast), color var(--tx-fast); }
        .ss-btn:hover { border-color: var(--border-hi); }
        .ss-btn:disabled { opacity: .45; cursor: default; }
        .ss-btn:disabled:hover { border-color: var(--border); }
        .ss-btn.accent { border-color: var(--accent-lo); color: var(--accent-hi); }
        .ss-btn.accent:hover { border-color: var(--accent-hi); }
        .ss-hr { height: 1px; background: var(--border); margin: 2px 0; }
        .ss-status { margin: 8px 14px 12px; min-height: 17px; font-size: 11.5px; color: var(--text-dim); opacity: 0; transition: opacity 200ms var(--ease); }
        .ss-status.show { opacity: 1; }
        .ss-status.ok { color: var(--ok); }
        .ss-status.err { color: #f87171; }`);

    const c = $('content');
    c.innerHTML = '';
    const wrap = document.createElement('div'); wrap.id = 'ssWrap'; wrap.className = 'pane';
    c.appendChild(wrap);

    const grp = document.createElement('div'); grp.className = 'mk-grp'; grp.textContent = 'Screenshot';
    wrap.appendChild(grp);

    const card = document.createElement('div'); card.className = 'ss-card';
    wrap.appendChild(card);

    const desc = document.createElement('div'); desc.className = 'ss-desc';
    desc.textContent = 'Saves the game window as a PNG in your RuneToolsX screenshots folder, named by date and time. A capture shows an in-game notification when it saves.';
    card.appendChild(desc);

    const krow = document.createElement('div'); krow.className = 'ss-row';
    const lbl = document.createElement('div'); lbl.className = 'ss-label'; lbl.textContent = 'Capture key';
    const chip = document.createElement('div'); chip.className = 'ss-chip'; chip.id = 'ssKey';
    krow.appendChild(lbl); krow.appendChild(chip); card.appendChild(krow);

    const kbtns = document.createElement('div'); kbtns.className = 'ss-row';
    const set = document.createElement('button'); set.className = 'ss-btn'; set.id = 'ssSet';
    set.addEventListener('click', () => {
      // Claim the game's keystrokes while armed: this is a BUTTON, so the focus-based capture
      // in client.html cannot see it and every key would go to the game instead of here.
      ssCapturing = !ssCapturing;
      if (typeof kbGrab === 'function') kbGrab(ssCapturing);
      ssPaint();
    });
    const clr = document.createElement('button'); clr.className = 'ss-btn'; clr.id = 'ssClear'; clr.textContent = 'Clear keybind';
    clr.addEventListener('click', () => {
      if (!ssKeybind) return;
      ssKeybind = 0;
      if (ssCapturing && typeof kbGrab === 'function') kbGrab(false);
      ssCapturing = false; ssSaveKb(); ssPaint(); ssSetStatus('Keybind cleared.');
    });
    kbtns.appendChild(set); kbtns.appendChild(clr); card.appendChild(kbtns);

    const hr = document.createElement('div'); hr.className = 'ss-hr'; card.appendChild(hr);

    const arow = document.createElement('div'); arow.className = 'ss-row';
    const cap = document.createElement('button'); cap.className = 'ss-btn accent'; cap.textContent = 'Capture now';
    cap.addEventListener('click', ssCaptureNow);
    const open = document.createElement('button'); open.className = 'ss-btn'; open.textContent = 'Open folder';
    open.addEventListener('click', () => { try { if (bridge().openScreenshots) rtxData.sync('act.openScreenshots'); } catch (e) {} });
    arow.appendChild(cap); arow.appendChild(open); card.appendChild(arow);

    const status = document.createElement('div'); status.id = 'ssStatus'; status.className = 'ss-status';
    wrap.appendChild(status);

    ssPaint();
  }

  // While armed, the next keypress becomes the binding; Esc cancels the arm without clearing
  // an existing binding. Capture-phase so it wins before the panel's other handlers.
  document.addEventListener('keydown', e => {
    if (!ssCapturing) return;
    e.preventDefault(); e.stopPropagation();
    const vk = e.keyCode || e.which || 0;
    if (vk && vk !== 27) { ssKeybind = vk; ssSaveKb(); ssSetStatus('Bound to ' + ssVkName(vk) + '.', 'ok'); }
    ssCapturing = false;
    if (typeof kbGrab === 'function') kbGrab(false);
    ssPaint();
  }, true);

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { ssVkName });
registerTab({ id: 'screenshot', render: renderScreenshot });
})();
