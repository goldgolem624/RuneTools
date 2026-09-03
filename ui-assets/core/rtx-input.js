// rtx-input.js: Input-region + keyboard publishing to the host (wmPushRects, wmRectsSoon, kbGrab, syncKbCapture, __rtxGameClick) and the wiki search palette (wikiPaletteOpen).
// Loads after: rtx-wm.js; installs focusin/focusout listeners and a 1 s syncKbCapture interval at load.
// Plain script, page globals by design: every top-level name here is a page global that panels and the other core files use.
  // ---- input-region + keyboard publishing to the host ----
  // null, not '': an empty first payload (menubar not laid out yet) must still
  // reach the host, or the dedup below swallows the very first publish.
  let _rectsQueued = false, _lastRectsPayload = null;
  function wmPushRects() {
    _rectsQueued = false;
    const b = bridge();
    if (!b || !b.uiRects) return;
    const rects = [];
    // Grow each rect by a hairline margin. Windows carry a soft drop shadow, so the
    // panel READS as bigger than its border box; without this, a click aimed at the
    // visual edge falls outside the consume region and the GAME acts on it (camera
    // spin / walk) instead of the panel. Inside the margin nothing happens, which is
    // the right outcome for a near-miss on a window edge.
    const PAD = 3;
    const add = (el) => {
      if (!el) return;
      const r = el.getBoundingClientRect();
      if (r.width > 0 && r.height > 0)
        rects.push(Math.floor(r.left - PAD) + ',' + Math.floor(r.top - PAD) + ',' +
                   Math.ceil(r.width + PAD * 2) + ',' + Math.ceil(r.height + PAD * 2));
    };
    if (!menubarHidden) add($('menubar'));
    add($('mbdrop'));
    // A locked HUD is deliberately absent: no consume rect means the companion never
    // swallows the click, so it lands in the game underneath. It keeps drawing regardless.
    for (const w of wm.wins.values()) if (!w.min && !w.locked) add(w.el);
    // Body-level popovers: they float outside any window rect, so they need their own.
    add($('sndMenu')); add($('vrPop'));
    add($('lgMenu'));
    add($('ctxCopy'));
    // Plugin ability HUD strips advertise header dragging and an x-to-close, so unlike the
    // locked alerts HUD they must claim their clicks. try/catch: pluginHuds lives in
    // rtx-plugins.js, which loads after this file; an early publish just skips it.
    try { for (const h of pluginHuds.values()) add(h.el); } catch (e) {}
    // Only DISMISSIBLE toasts claim input. A transient toast stays click-through so
    // it can never swallow a click meant for the game underneath it. Placement mode
    // is the exception: the whole stack must be grabbable, empty or not.
    if (toastPlacing) add($('toaster'));
    else for (const t of toasts) if (t.sticky && !t.closing) add(t.el);
    // Modal surfaces (tab search, tooltips are transient/hover-only) claim the whole
    // client: they dim everything and must swallow every click.
    if ($('tspOv') || $('vrModalOv') || $('wikiPal')) {
      rects.length = 0;
      rects.push('0,0,' + (window.innerWidth || 1280) + ',' + (window.innerHeight || 720));
    }
    const payload = rects.join(';');
    if (payload === _lastRectsPayload) return;
    _lastRectsPayload = payload;
    try { b.uiRects(myPid(), payload, !menubarHidden || rects.length > 0); } catch (e) {}
  }
  // Coalesce rect pushes to one per task; drags call wmPushRects directly per move.
  function wmRectsSoon() {
    if (_rectsQueued) return;
    _rectsQueued = true;
    setTimeout(wmPushRects, 0);
  }
  let menubarHidden = false;   // reserved: a future hide-UI toggle

  // Keyboard capture: claim the game's keystrokes only while a text field has focus.
  let _kbCaptured = false, _kbGrabs = 0;
  // Explicit claim, for panels that need the keystroke WITHOUT a focused text field -- a
  // "press any key" keybind listener is a button, so the focus test below can never see it and
  // every key went to the game instead of being captured. Refcounted: two listeners armed at
  // once must not have the first one to finish drop capture for both.
  function kbGrab(on) {
    _kbGrabs = Math.max(0, _kbGrabs + (on ? 1 : -1));
    syncKbCapture();
  }
  function syncKbCapture() {
    const t = document.activeElement;
    const typing = _kbGrabs > 0 || !!(t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' ||
                            t.tagName === 'SELECT' || t.isContentEditable));
    if (typing === _kbCaptured) return;
    _kbCaptured = typing;
    try { if (bridge() && bridge().uiKeyboard) bridge().uiKeyboard(myPid(), typing); } catch (e) {}
  }
  document.addEventListener('focusin', () => setTimeout(syncKbCapture, 0));
  document.addEventListener('focusout', () => setTimeout(syncKbCapture, 0));
  // Self-heal: a focused input REMOVED by a panel rebuild fires no focusout, which left the
  // capture flag stuck on and silently ate every keystroke meant for the game. Re-derive the
  // truth from the live activeElement once a second; syncKbCapture dedups, so this is free.
  setInterval(syncKbCapture, 1000);
  // Host-driven editing shortcuts: GameUi.cpp swallows Ctrl+A/C/X/V and calls this. Walk into
  // same-origin iframes as before; a sandboxed plugin frame is an opaque origin whose
  // contentDocument is null from here, so the command travels over postMessage instead and the
  // plugin SDK shim runs execCommand on the field that actually has focus.
  window.__rtxEditCmd = function (cmd) {
    let d = document;
    for (;;) {
      const ae = d.activeElement;
      if (!ae || ae.tagName !== 'IFRAME') break;
      let inner = null;
      try { inner = ae.contentDocument; } catch (e) {}
      if (inner) { d = inner; continue; }
      try { ae.contentWindow.postMessage({ __rtxPlugin: 'rtx.plugin/1', kind: 'edit', cmd: String(cmd) }, '*'); } catch (e) {}
      return;
    }
    try { d.execCommand(cmd); } catch (e) {}
  };

  // Companion-signalled: a click went to the GAME while we held keyboard capture --
  // drop focus so typing returns to the game immediately.
  window.__rtxGameClick = function () {
    try { if (document.activeElement && document.activeElement.blur) document.activeElement.blur(); } catch (e) {}
    setTimeout(syncKbCapture, 0);
  };

  // ---- Wiki palette: dimmed search overlay -> the wiki-locked in-client browser. ------
  // Opened by the host on the user's hotkey (gameui::OpenWikiPalette evaluates
  // wikiPaletteOpen()) or from any panel. Enter / the button opens the term in the
  // dedicated wiki pane; exact titles land directly on their page. The chip in the
  // footer binds the hotkey inline: click it, press a key (Esc unbinds).
  let _wkKbListen = false;
  function wikiPaletteOpen() {
    const existing = $('wikiPal');
    if (existing) { const i0 = $('wkIn'); if (i0) i0.focus(); return; }
    const ov = document.createElement('div'); ov.id = 'wikiPal';
    ov.innerHTML =
        '<div class="wk-card">'
      +   '<div class="wk-row">'
      +     '<input id="wkIn" class="wk-in" placeholder="Search the RuneScape Wiki..." autocomplete="off" spellcheck="false">'
      +     '<button id="wkGo" class="wk-go">Search</button>'
      +   '</div>'
      +   '<div class="wk-hint">'
      +     '<span>Enter opens in-game &middot; exact titles open directly &middot; Esc closes</span>'
      +     '<span class="wk-kb" id="wkKb" title="Click, then press the key to bind (Esc unbinds)">Hotkey: &hellip;</span>'
      +   '</div>'
      + '</div>';
    document.body.appendChild(ov);
    kbGrab(true);
    wmRectsSoon();
    // The opening hotkey's WM_CHAR arrives AFTER the palette focuses its input (the host
    // consumes the key-down, but TranslateMessage already queued the character), so the
    // bound key used to type itself into the search box. Swallow input for the first beat.
    const openedAt = performance.now();
    const done = () => {
      if (!$('wikiPal')) return;
      _wkKbListen = false;
      kbGrab(false);
      ov.remove();
      wmRectsSoon();
    };
    const go = () => {
      const t = ($('wkIn').value || '').trim();
      done();
      try { if (bridge() && bridge().wikiOpen) bridge().wikiOpen(myPid(), t); } catch (e) {}
    };
    const kbEl = $('wkKb');
    const kbPaint = () => {
      let vk = 0;
      try { vk = (bridge() && bridge().wikiKeybindGet) ? (bridge().wikiKeybindGet() | 0) : 0; } catch (e) {}
      kbEl.textContent = 'Hotkey: ' + (vk ? ssVkName(vk) : 'not set');
    };
    kbEl.addEventListener('click', () => { _wkKbListen = true; kbEl.textContent = 'Press a key…'; });
    ov.addEventListener('mousedown', (e) => { if (e.target === ov) done(); });
    $('wkGo').addEventListener('click', go);
    ov.addEventListener('keydown', (e) => {
      if (_wkKbListen) {
        e.preventDefault(); e.stopPropagation();
        _wkKbListen = false;
        const vk = e.key === 'Escape' ? 0 : (e.keyCode | 0);
        try { if (bridge() && bridge().wikiKeybindSet) bridge().wikiKeybindSet(vk); } catch (e2) {}
        kbPaint();
        $('wkIn').focus();   // palette stays open; capture is still correctly held here
        return;
      }
      if (e.key === 'Enter') { e.preventDefault(); go(); }
      else if (e.key === 'Escape') { e.preventDefault(); done(); }
    }, true);
    const wkInp = $('wkIn');
    const swallow = (e) => { if (performance.now() - openedAt < 180) e.preventDefault(); };
    wkInp.addEventListener('beforeinput', swallow);
    wkInp.addEventListener('keypress', swallow);   // WebKit char path, in case beforeinput is absent
    kbPaint();
    wkInp.focus();
  }

