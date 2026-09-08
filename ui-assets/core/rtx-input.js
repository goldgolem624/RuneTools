  let _rectsQueued = false, _lastRectsPayload = null;
  function wmPushRects() {
    _rectsQueued = false;
    const b = bridge();
    if (!b || !b.uiRects) return;
    const rects = [];
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
    add($('mbdrop2'));   // the More menu's side flyout claims its clicks too
    for (const w of wm.wins.values()) if (!w.min && !w.locked) add(w.el);
    add($('sndMenu')); add($('vrPop'));
    add($('lgMenu'));
    add($('ctxCopy'));
    try { for (const h of pluginHuds.values()) add(h.el); } catch (e) {}
    if (toastPlacing) add($('toaster'));
    else for (const t of toasts) if (t.sticky && !t.closing) add(t.el);
    if ($('tspOv') || $('vrModalOv') || $('wikiPal')) {
      rects.length = 0;
      rects.push('0,0,' + (window.innerWidth || 1280) + ',' + (window.innerHeight || 720));
    }
    const payload = rects.join(';');
    if (payload === _lastRectsPayload) return;
    _lastRectsPayload = payload;
    try { b.uiRects(myPid(), payload, !menubarHidden || rects.length > 0); } catch (e) {}
  }
  function wmRectsSoon() {
    if (_rectsQueued) return;
    _rectsQueued = true;
    setTimeout(wmPushRects, 0);
  }
  let menubarHidden = false;   // reserved: a future hide-UI toggle

  let _kbCaptured = false, _kbGrabs = 0;
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
  setInterval(syncKbCapture, 1000);
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

  window.__rtxGameClick = function () {
    try { if (document.activeElement && document.activeElement.blur) document.activeElement.blur(); } catch (e) {}
    setTimeout(syncKbCapture, 0);
  };

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

