// rtx-settings.js: Preferences page (renderUiSettings, reflectUiSettings) and its widgets (uisPill, uisSlider, uisChipRow, hotkey capture).
// Loads after: rtx-notify.js, rtx-ui.js, rtx-wm.js.
// Plain script, page globals by design: every top-level name here is a page global that panels and the other core files use.
  // ---- UI Settings panel (Developer): live placement of in-game chrome ----
  const TOAST_ANCHORS = [
    ['top-left', 'Top left'], ['top-center', 'Top centre'], ['top-right', 'Top right'],
    ['bottom-left', 'Bottom left'], ['bottom-center', 'Bottom centre'], ['bottom-right', 'Bottom right'],
  ];
  // BUILD-ONCE. renderPane() runs this every 250 ms for a visible window, and the
  // placement drag calls it per mousemove -- rebuilding the DOM either time destroyed
  // the control the user was actively dragging, so slider input went nowhere. Build
  // the markup once, then only reflect values into it.
  // Shared row builders for the Preferences page (values reflected by reflectUiSettings,
  // structure built once: renderPane re-runs this at 4 Hz while the window is open).
  function uisPill(label, id, get, set, hint) {
    const r = document.createElement('div'); r.className = 'row';
    const k = document.createElement('span'); k.className = 'k'; k.textContent = label;
    if (hint) { const sub = document.createElement('span'); sub.className = 'pf-sub'; sub.textContent = hint; k.appendChild(sub); }
    const pill = document.createElement('div'); pill.id = id; pill.className = 'al-pill' + (get() ? ' on' : '');
    pill.innerHTML = '<span></span>'; if (hint) pill.title = hint;
    pill.addEventListener('click', () => { set(!get()); pill.classList.toggle('on', !!get()); });
    pill._get = get;
    r.appendChild(k); r.appendChild(pill);
    return r;
  }
  // commitOnRelease: the setter runs on release only (input still previews the label). Needed
  // for anything that re-lays-out the page while dragging (UI scale resizes the viewport under
  // the slider, so a live setter moves the thumb away from the cursor and feeds back).
  function uisSlider(label, id, min, max, step, get, set, fmt, commitOnRelease) {
    const r = document.createElement('div'); r.className = 'row';
    const k = document.createElement('span'); k.className = 'k'; k.textContent = label;
    const v = document.createElement('span'); v.className = 'v uis-row';
    const inp = document.createElement('input'); inp.type = 'range'; inp.id = id;
    inp.min = String(min); inp.max = String(max); inp.step = String(step); inp.value = String(get());
    const out = document.createElement('b'); out.id = id + 'v'; out.textContent = fmt(get());
    inp.addEventListener('input', () => { if (!commitOnRelease) set(Number(inp.value)); out.textContent = fmt(Number(inp.value)); });
    if (commitOnRelease) inp.addEventListener('change', () => { set(Number(inp.value)); out.textContent = fmt(Number(inp.value)); });
    inp._get = inp._get || get; inp._fmt = fmt;
    v.appendChild(inp); v.appendChild(out); r.appendChild(k); r.appendChild(v);
    return r;
  }
  function uisChipRow(label, id, chips, isOn, onPick) {
    const r = document.createElement('div'); r.className = 'row';
    const k = document.createElement('span'); k.className = 'k'; k.textContent = label;
    const v = document.createElement('span'); v.className = 'v pf-full';
    const g = document.createElement('div'); g.className = 'uis-chips'; g.id = id;
    for (const [text, val, swatch] of chips) {
      const b = document.createElement('button'); b.type = 'button'; b.className = 'uis-chip'; b.dataset.val = val;
      if (swatch) { const d = document.createElement('i'); d.style.cssText = 'display:inline-block;width:9px;height:9px;border-radius:50%;margin-right:5px;background:' + swatch; b.appendChild(d); }
      b.appendChild(document.createTextNode(text));
      b.addEventListener('click', () => { onPick(val); reflectUiSettings(); });
      g.appendChild(b);
    }
    g._isOn = isOn;
    v.appendChild(g); r.appendChild(k); r.appendChild(v);
    return r;
  }
  // Hide/show-all-panels hotkey state (launcher-side binding, page-side capture).
  let uisHpVk = 0, uisHpCapturing = false, uisHpPaint = null;
  function uisHpSave() { try { if (bridge().hidePanelsKeybindSet) bridge().hidePanelsKeybindSet(uisHpVk); } catch (e) {} }
  document.addEventListener('keydown', e => {
    if (!uisHpCapturing) return;
    e.preventDefault(); e.stopPropagation();
    const vk = e.keyCode || e.which || 0;
    if (vk && vk !== 27) { uisHpVk = vk; uisHpSave(); }
    uisHpCapturing = false;
    if (typeof kbGrab === 'function') kbGrab(false);
    if (uisHpPaint) uisHpPaint();
  }, true);
  function uisSep(rows) { /* sections are cards now; kept for call-site compatibility */ }
  function uisNote(rows, text) { const d = document.createElement('div'); d.className = 'pf-hint'; d.textContent = text; rows.appendChild(d); }

  function renderUiSettings() {
    const c = $('content'); if (!c) return;
    if ($('uisWrap')) { reflectUiSettings(); return; }
    c.innerHTML = '';
    const wrap = document.createElement('div'); wrap.id = 'uisWrap'; wrap.className = 'pane';
    let rows = null;
    const sec = (title) => {
      const card = document.createElement('div'); card.className = 'pf-card';
      const h = document.createElement('div'); h.className = 'pf-title'; h.textContent = title;
      card.appendChild(h); wrap.appendChild(card); rows = card;
    };
    // Never hold the cfg object: the durable copy can replace it after this page is built
    // (prefsInit), and a toggle mutating a stale object is then silently not saved.
    const cfg = new Proxy({}, { get: (_, k) => uiCfg()[k], set: (_, k, v) => { uiCfg()[k] = v; return true; } });
    const save = () => { uiCfgSave(); uiApply(); };

    // ---------------- Appearance ----------------
    sec('Appearance');
    // Presets rather than a slider: applying a scale reflows the very panel being edited, so a
    // continuous control fights the cursor. One click, one value.
    rows.appendChild(uisChipRow('UI scale', 'uis_scale', [75, 90, 100, 110, 125, 150, 175, 200].map(v => [v + '%', String(v)]),
      v => String(cfg.scale || 100) === v, v => { cfg.scale = Number(v); save(); }));
    if (!(bridge() && bridge().uiScale)) uisNote(rows, 'UI scale needs the updated launcher; the value is saved and applies after the next update.');
    rows.appendChild(uisChipRow('Accent', 'uis_accent', UI_ACCENTS.map(a => [a[0], a[1], a[1]]),
      v => v.toLowerCase() === String(cfg.accent).toLowerCase(), v => { cfg.accent = v; save(); }));
    {
      const r = document.createElement('div'); r.className = 'row';
      const k = document.createElement('span'); k.className = 'k'; k.textContent = 'Custom accent';
      const v = document.createElement('span'); v.className = 'v';
      v.id = 'uis_customAccent';
      if (typeof auraColor === 'function') {
        v.appendChild(auraColor(cfg.accent, hex => { cfg.accent = hex; save(); reflectUiSettings(); }));
      } else {
        const inp = document.createElement('input'); inp.className = 'bank-search'; inp.style.width = '90px'; inp.value = cfg.accent; inp.spellcheck = false;
        inp.addEventListener('change', () => { if (/^#[0-9a-f]{6}$/i.test(inp.value)) { cfg.accent = inp.value; save(); reflectUiSettings(); } });
        v.appendChild(inp);
      }
      r.appendChild(k); r.appendChild(v); rows.appendChild(r);
    }
    rows.appendChild(uisSlider('Panel opacity', 'uis_opacity', 30, 100, 1, () => cfg.opacity, v => { cfg.opacity = v; save(); }, v => v + '%'));
    rows.appendChild(uisPill('High contrast', 'uis_hc', () => !!cfg.contrast, v => { cfg.contrast = v; save(); }, 'Brighter secondary text and borders'));
    rows.appendChild(uisPill('Animations', 'uis_motion', () => cfg.motion !== false, v => { cfg.motion = v; save(); }, 'Off disables pulses, slides and fades everywhere'));
    rows.appendChild(uisChipRow('Numbers', 'uis_numfmt', [['Compact (12.3m)', 'compact'], ['Full (12,345,678)', 'full']],
      v => (cfg.numFmt || 'compact') === v, v => { cfg.numFmt = v; save(); uiRepaintAll(); }));
    rows.appendChild(uisChipRow('Font', 'uis_font', UI_FONTS.map(f => [f[0], f[1]]),
      v => (cfg.font || 'variable') === v, v => { cfg.font = v; save(); }));
    rows.appendChild(uisSlider('Font size', 'uis_fontsize', 11, 17, 1, () => cfg.fontSize || 13, v => { cfg.fontSize = v; save(); }, v => v + 'px'));
    rows.appendChild(uisPill('Tabular digits', 'uis_tabular', () => !!cfg.tabular, v => { cfg.tabular = v; save(); }, 'Fixed-width numerals so columns of numbers line up'));
    rows.appendChild(uisPill('Compact tooltips', 'uis_ctips', () => !!cfg.compactTips, v => { cfg.compactTips = v; save(); try { hideTip(); } catch (e) {} }, 'Hides provenance (Source, ID), explanatory lines and parenthetical asides in tooltips'));

    // ---------------- Bar ----------------
    sec('Bar');
    uisNote(rows, 'Chips shown in the menu bar beside the player status.');
    rows.appendChild(uisPill('Clock', 'uis_barclock', () => !!cfg.barClock, v => { cfg.barClock = v; save(); }));
    rows.appendChild(uisPill('24-hour clock', 'uis_bar24', () => !!cfg.bar24h, v => { cfg.bar24h = v; save(); }));
    rows.appendChild(uisPill('Session timer', 'uis_barsess', () => !!cfg.barSession, v => { cfg.barSession = v; save(); }, 'Time logged into the game this session (lobby and in-game)'));
    rows.appendChild(uisPill('XP per hour', 'uis_barxp', () => !!cfg.barXp, v => { cfg.barXp = v; save(); }, 'Total XP/h from the XP tracker session'));
    rows.appendChild(uisPill('Player status', 'uis_barstatus', () => cfg.barStatus !== false,
      v => { cfg.barStatus = v; save(); try { renderMenubar(); } catch (e) {} },
      'The name / world block in the bar; off hides it entirely (see also Privacy to mask just the name or world)'));
    rows.appendChild(uisPill('Minimized panel chips', 'uis_barmin', () => cfg.barMinChips !== false,
      v => { cfg.barMinChips = v; save(); try { renderMenubar(); } catch (e) {} },
      'Off hides minimized panels from the bar; reopen them from their menu category'));

    // ---------------- Menu ----------------
    sec('Menu');
    uisNote(rows, 'Hide categories you never open. Settings always stays.');
    rows.appendChild(uisChipRow('Show categories', 'uis_cats', CAT_META.filter(m => m.id !== 'Settings').map(m => [m.id, m.id]),
      v => !uiCatHidden(v), v => {
        const i = cfg.hiddenCats.indexOf(v);
        if (i >= 0) cfg.hiddenCats.splice(i, 1); else cfg.hiddenCats.push(v);
        save(); try { renderMenubar(); } catch (e) {}
      }));

    // ---------------- Startup ----------------
    sec('Startup');
    rows.appendChild(uisPill('Restore open panels on launch', 'uis_restore', () => cfg.restore !== false, v => { cfg.restore = v; save(); },
      'Off starts with no panels open; bar position and alert placement are still remembered'));

    // ---------------- Notifications ----------------
    sec('Notifications');
    rows.appendChild(uisPill('Alerts enabled', 'uis_almaster', () => !!(alertCfg && alertCfg.master), v => { if (alertCfg) { alertCfg.master = v; saveAlertCfg(); } }));
    rows.appendChild(uisPill('Only alert when tabbed out', 'uis_alfocus', () => !!(alertCfg && alertCfg.unfocusedOnly), v => { if (alertCfg) { alertCfg.unfocusedOnly = v; saveAlertCfg(); } },
      'Applies to every alert source: idle, custom, auras'));
    rows.appendChild(uisSlider('Sound volume', 'uis_vol', 0, 100, 1, () => (typeof sndVol === 'number' ? sndVol : 70),
      v => { try { sndVol = v; prefSet('rtxSoundVol', String(v)); bridge().soundVolume(v); } catch (e) {} }, v => v + '%'));
    rows.appendChild(uisSlider('Popup duration', 'uis_ttl', 2000, 15000, 500, () => cfg.toastTtl || 5000, v => { cfg.toastTtl = v; save(); }, v => (v / 1000).toFixed(1) + 's'));

    // Popup placement (unchanged mechanics: stored with the per-account layout).
    const aRow = document.createElement('div'); aRow.className = 'row';
    const aLab = document.createElement('span'); aLab.className = 'k'; aLab.textContent = 'Popup anchor';
    const aWrap = document.createElement('span'); aWrap.className = 'v pf-full';
    const seg = document.createElement('div');
    seg.className = 'uis-chips'; seg.id = 'uis_anchor';
    for (const [id, label] of TOAST_ANCHORS) {
      const b = document.createElement('button');
      b.type = 'button'; b.textContent = label; b.className = 'uis-chip'; b.dataset.anchor = id;
      b.addEventListener('click', () => {
        toastCfg.anchor = id;
        toastCfg.dx = 0;
        toastCfg.dy = (id.indexOf('top') === 0) ? 58 : 24;
        applyToastPos(); wmSaveSoon(); reflectUiSettings();
      });
      seg.appendChild(b);
    }
    aWrap.appendChild(seg); aRow.appendChild(aLab); aRow.appendChild(aWrap);
    rows.appendChild(aRow);
    const num = (label, key) => {
      const r = document.createElement('div'); r.className = 'row';
      const k = document.createElement('span'); k.className = 'k'; k.textContent = label;
      const v = document.createElement('span'); v.className = 'v uis-row';
      const inp = document.createElement('input');
      inp.type = 'range'; inp.id = 'uis_' + key;
      const out = document.createElement('b'); out.id = 'uis_' + key + 'v';
      inp.addEventListener('input', () => {
        toastCfg[key] = Number(inp.value) | 0;
        out.textContent = inp.value + 'px';
        applyToastPos(); wmSaveSoon();
      });
      v.appendChild(inp); v.appendChild(out);
      r.appendChild(k); r.appendChild(v);
      return r;
    };
    rows.appendChild(num('Offset X', 'dx'));
    rows.appendChild(num('Offset Y', 'dy'));
    rows.appendChild(num('Width', 'w'));
    const btnRow = (label, id, buttons) => {
      const r = document.createElement('div'); r.className = 'row';
      const k = document.createElement('span'); k.className = 'k'; k.textContent = label;
      const v = document.createElement('span'); v.className = 'v pf-full';
      const g = document.createElement('div');
      g.className = 'uis-chips'; g.id = id;
      for (const [text, fn, bid] of buttons) {
        const b = document.createElement('button');
        b.type = 'button'; b.className = 'uis-chip'; b.textContent = text;
        if (bid) b.id = bid;
        b.addEventListener('click', fn);
        g.appendChild(b);
      }
      v.appendChild(g); r.appendChild(k); r.appendChild(v);
      return r;
    };
    rows.appendChild(btnRow('Placement', 'uis_place', [
      ['Position on screen', () => { toastPlacing = !toastPlacing; applyToastPos(); reflectUiSettings(); }, 'uis_placeBtn'],
      ['Reset', () => { toastCfg = Object.assign({}, TOAST_DEF); applyToastPos(); wmSaveSoon(); reflectUiSettings(); }],
    ]));
    rows.appendChild(btnRow('Preview', 'uis_prev', [
      ['Timed', () => uiNotify('Test alert - this is how an in-game alert looks.', {})],
      ['Sticky', () => uiNotify('Sticky alert - stays until dismissed.', { sticky: true })],
      ['Clear', () => { for (const t of toasts.slice()) toastClose(t); }],
    ]));

    // ---------------- Hotkeys ----------------
    sec('Hotkeys');
    uisNote(rows, 'Keys are read by the launcher while the game has focus, so they work without clicking a panel first. Screenshot and Wiki keys are set on their own panels.');
    {
      const r = document.createElement('div'); r.className = 'row';
      const k = document.createElement('span'); k.className = 'k'; k.textContent = 'Hide / show all panels';
      const v = document.createElement('span'); v.className = 'v pf-full';
      const g = document.createElement('div'); g.className = 'uis-chips';
      const chip = document.createElement('button'); chip.type = 'button'; chip.className = 'uis-chip'; chip.id = 'uis_hpKey';
      const clr = document.createElement('button'); clr.type = 'button'; clr.className = 'uis-chip'; clr.textContent = 'Clear';
      const paint = () => {
        chip.textContent = uisHpCapturing ? 'Press a key...' : (uisHpVk ? (typeof ssVkName === 'function' ? ssVkName(uisHpVk) : ('VK ' + uisHpVk)) : 'Set key');
        chip.classList.toggle('on', !!uisHpCapturing);
      };
      chip.addEventListener('click', () => { uisHpCapturing = !uisHpCapturing; if (typeof kbGrab === 'function') kbGrab(uisHpCapturing); paint(); });
      clr.addEventListener('click', () => { uisHpVk = 0; if (uisHpCapturing && typeof kbGrab === 'function') kbGrab(false); uisHpCapturing = false; uisHpSave(); paint(); });
      uisHpPaint = paint;
      (async () => { try { if (bridge().hidePanelsKeybindGet) uisHpVk = (await bridge().hidePanelsKeybindGet()) | 0; } catch (e) {} paint(); })();
      if (!(bridge() && bridge().hidePanelsKeybindGet)) { chip.disabled = true; chip.textContent = 'Needs updated launcher'; }
      g.appendChild(chip); g.appendChild(clr); v.appendChild(g); r.appendChild(k); r.appendChild(v); rows.appendChild(r);
    }

    // ---------------- Plugins (dynamic card) ----------------
    // Content is declared by plugins over the SDK (rtx.plugin.ui.settings), which can
    // happen any time after this page is built, so the card owns its own rebuild:
    // uisBuildPluginSettings() runs here and again whenever the broker registers a
    // schema. Values reflect through the same generic _get machinery as every other
    // control.
    {
      const card = document.createElement('div'); card.className = 'pf-card'; card.id = 'uisPluginsCard';
      wrap.appendChild(card);
      uisBuildPluginSettings(card);
    }

    // ---------------- Privacy ----------------
    sec('Privacy');
    rows.appendChild(uisPill('Hide player name', 'uis_hidename', () => !!nameHidden, v => setNameHidden(v), 'Masks the name in the bar header'));
    rows.appendChild(uisPill('Hide world number', 'uis_hideworld', () => !!cfg.hideWorld, v => { cfg.hideWorld = v; save(); _hdrSig = ''; try { renderHeader(); } catch (e) {} }));

    // ---------------- Data ----------------
    sec('Data');
    uisNote(rows, 'Export copies every preference (sounds, overlay, auras, appearance) to the clipboard as one block; Import reads such a block back from the clipboard.');
    rows.appendChild(btnRow('Preferences', 'uis_data', [
      ['Export to clipboard', async () => {
        try {
          const out = { rtxPrefsExport: 1, prefs: {} };
          for (const k of PREF_DURABLE) { const v = prefGet(k, null); if (v !== null) out.prefs[k] = v; }
          await bridge().copyClipboard(JSON.stringify(out));
          uiNotify('Preferences copied to the clipboard.', {});
        } catch (e) { uiNotify('Export failed.', {}); }
      }],
      ['Import from clipboard', async () => {
        try {
          const txt = await bridge().pasteClipboard();
          const v = JSON.parse(txt || 'null');
          if (!v || v.rtxPrefsExport !== 1 || !v.prefs) { uiNotify('Clipboard does not hold an exported preferences block.', {}); return; }
          let n = 0;
          for (const k of PREF_DURABLE) if (Object.prototype.hasOwnProperty.call(v.prefs, k)) { prefSet(k, v.prefs[k]); n++; }
          _uiCfg = null; uiApply();
          try { sndApplyDurablePrefs(); ovApplyDurablePrefs(); sceneApplyDurablePrefs(); if (typeof auraApplyDurablePrefs === 'function') auraApplyDurablePrefs(); } catch (e) {}
          const w = $('uisWrap'); if (w) w.remove();
          uiNotify('Imported ' + n + ' preference' + (n === 1 ? '' : 's') + '.', {});
        } catch (e) { uiNotify('Import failed.', {}); }
      }],
      ['Reset appearance', () => {
        _uiCfg = Object.assign({}, UI_DEF, { hiddenCats: [] }); uiCfgSave(); uiApply();
        try { renderMenubar(); } catch (e) {}
        const w = $('uisWrap'); if (w) w.remove();
      }],
    ]));

    // First row of every card carries no divider.
    for (const card of wrap.querySelectorAll('.pf-card')) { const f = card.querySelector('.row'); if (f) f.classList.add('pf-first'); }
    c.appendChild(wrap);
    reflectUiSettings();
  }

  // Plugin settings card: built here, rebuilt by the broker (rtx-plugins.js) whenever a
  // plugin declares its schema. Renders the standard widgets for each declared control;
  // a change goes through pluginSettingsSet, which clamps, persists and pushes the new
  // values to the plugin as a 'settings' event.
  function uisBuildPluginSettings(hostEl) {
    const host = hostEl || $('uisPluginsCard');
    if (!host) return;
    host.innerHTML = '';
    const h = document.createElement('div'); h.className = 'pf-title'; h.textContent = 'Plugins';
    host.appendChild(h);
    const reg = (typeof pluginSettingsReg !== 'undefined') ? pluginSettingsReg : null;
    if (!reg || !reg.size) { uisNote(host, 'Plugins that declare settings appear here once opened.'); return; }
    let firstRow = true;
    for (const [id, r] of reg) {
      const sub = document.createElement('div'); sub.className = 'pf-hint'; sub.style.fontWeight = '600'; sub.textContent = r.name;
      host.appendChild(sub);
      for (const c of r.schema) {
        const eid = 'uisp_' + id.replace(/[^A-Za-z0-9_-]/g, '_') + '_' + c.key;
        const val = () => r.values[c.key];
        let row;
        if (c.type === 'toggle') {
          row = uisPill(c.label, eid, () => !!val(), v => pluginSettingsSet(id, c.key, v), c.hint);
        } else if (c.type === 'slider') {
          row = uisSlider(c.label, eid, c.min, c.max, c.step, () => Number(val()), v => pluginSettingsSet(id, c.key, v), v => String(v));
        } else if (c.type === 'select') {
          row = uisChipRow(c.label, eid, c.options.map(o => [o.label, o.v]), v => val() === v, v => pluginSettingsSet(id, c.key, v));
        } else {
          row = document.createElement('div'); row.className = 'row';
          const k = document.createElement('span'); k.className = 'k'; k.textContent = c.label;
          if (c.hint) { const s2 = document.createElement('span'); s2.className = 'pf-sub'; s2.textContent = c.hint; k.appendChild(s2); }
          const v2 = document.createElement('span'); v2.className = 'v';
          const inp = document.createElement('input'); inp.className = 'bank-search'; inp.style.width = '160px';
          inp.value = String(val() == null ? '' : val()); inp.spellcheck = false;
          inp.addEventListener('change', () => pluginSettingsSet(id, c.key, inp.value));
          v2.appendChild(inp); row.appendChild(k); row.appendChild(v2);
        }
        if (firstRow) { row.classList.add('pf-first'); firstRow = false; }
        host.appendChild(row);
      }
      firstRow = true;
    }
  }

  // Values only. Never rewrites structure, and never fights a control the user is
  // currently dragging (that input keeps its own value until released).
  function reflectUiSettings() {
    const wrapEl = $('uisWrap'); if (!wrapEl) return;
    // Generic controls: pills mirror their getter, sliders their getter unless being dragged,
    // chip groups their isOn predicate.
    for (const p of wrapEl.querySelectorAll('.al-pill')) if (p._get) p.classList.toggle('on', !!p._get());
    for (const inp of wrapEl.querySelectorAll('input[type=range]')) {
      if (!inp._get) continue;
      if (document.activeElement !== inp) inp.value = String(inp._get());
      const out = $(inp.id + 'v'); if (out && inp._fmt) out.textContent = inp._fmt(Number(inp.value));
    }
    for (const g of wrapEl.querySelectorAll('.uis-chips')) if (g._isOn) for (const b of Array.from(g.children)) b.classList.toggle('on', !!g._isOn(b.dataset.val));
    const ca = $('uis_customAccent');
    if (ca) {
      const sw = ca.querySelector('.au-swatch'), inp = ca.querySelector('input');
      if (sw) sw.style.background = uiCfg().accent;
      if (inp && document.activeElement !== inp) inp.value = uiCfg().accent;
    }
    const seg = $('uis_anchor');
    if (seg) for (const b of Array.from(seg.children))
      b.classList.toggle('on', b.dataset.anchor === toastCfg.anchor);
    const vw = window.innerWidth || 1280, vh = window.innerHeight || 720;
    const range = {
      dx: [-Math.round(vw / 2), Math.round(vw / 2)],
      dy: [0, Math.max(40, vh - 80)],
      w:  [220, Math.max(260, vw - 40)],
    };
    for (const key of ['dx', 'dy', 'w']) {
      const inp = $('uis_' + key), out = $('uis_' + key + 'v');
      if (!inp) continue;
      inp.min = String(range[key][0]);
      inp.max = String(range[key][1]);
      const val = Number(toastCfg[key]) || 0;
      if (document.activeElement !== inp) inp.value = String(val);
      if (out) out.textContent = val + 'px';
    }
    const pb = $('uis_placeBtn');
    if (pb) {
      pb.textContent = toastPlacing ? 'Done positioning' : 'Position on screen';
      pb.classList.toggle('on', !!toastPlacing);
    }
  }

