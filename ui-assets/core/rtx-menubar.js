// rtx-menubar.js: Menu bar (positionMenubar, renderMenubar, dropdowns), renderSidebar alias, wiki pane, fullscreen, tab search palette (Ctrl+K), copySelection/contextmenu.
// Loads after: rtx-layout.js, rtx-wm.js, rtx-registry.js; installs mousedown/keydown/contextmenu listeners at load.
// Plain script, page globals by design: every top-level name here is a page global that panels and the other core files use.
  // ==================== Menu bar ====================
  // Slim glass bar: category launchers + player status + search. Draggable to any
  // x position along the top or bottom edge; collapsible to a pill. State rides
  // the layout store. renderSidebar stays as an alias -- panel code and the GIM
  // visibility poll still call it.
  function positionMenubar() {
    const bar = $('menubar');
    if (!bar) return;
    const vw = window.innerWidth || 1280;
    const bw = bar.offsetWidth || 320;
    let x = (typeof wm.barX === 'number') ? Math.round(vw * wm.barX - bw / 2) : Math.round((vw - bw) / 2);
    x = Math.max(6, Math.min(vw - bw - 6, x));
    bar.style.left = x + 'px';
    bar.style.transform = 'none';
    if (typeof wm.barYf === 'number') {
      // Free placement: any vertical position, always clamped inside the game frame.
      const vh = window.innerHeight || 720, bh = bar.offsetHeight || 38;
      let y = Math.round(vh * wm.barYf - bh / 2);
      y = Math.max(6, Math.min(vh - bh - 6, y));
      bar.style.bottom = 'auto'; bar.style.top = y + 'px';
      wm.barY = (y + bh / 2 > vh / 2) ? 'bottom' : 'top';   // dropdown and toast sides follow the half it sits in
    } else if (wm.barY === 'bottom') { bar.style.top = 'auto'; bar.style.bottom = '10px'; }
    else { bar.style.bottom = 'auto'; bar.style.top = '10px'; }
    // The toast stack sits under the bar when it is docked top, and at the top of
    // the screen when the bar moves to the bottom.
    document.body.classList.toggle('bar-bottom', wm.barY === 'bottom');
  }
  function closeMbDrop() {
    wm.mbDrop = null;
    const dd = $('mbdrop');
    if (dd) dd.remove();
    const bar = $('menubar');
    if (bar) bar.querySelectorAll('.mb-cat.open').forEach(b => b.classList.remove('open'));
    wmRectsSoon();
  }
  // The rows a category's menu shows. A TAB_GROUP collapses to ONE row, so this is what the user
  // actually sees rather than the raw tab list. Shared by the dropdown and by the single-entry
  // shortcut in renderMenubar, so a click behaves identically whichever path it arrives through.
  // Recomputed per click, never cached: Plugins grows and shrinks as plugins are installed.
  function mbCatEntries(catId) {
    const out = [], emitted = {};
    for (const t of allTabs()) {
      if (t.hidden) continue;
      if ((t.cat || 'General') !== catId) continue;
      const g = TAB_GROUP_OF[t.id];
      if (g && emitted[g.id]) continue;
      if (g) emitted[g.id] = true;
      out.push({ label: g ? g.label : t.label,
                 win:    g ? wmWinOfAny(g.tabs) : wmWinOf(t.id),
                 target: g ? groupEntryTab(g)   : t,
                 // Every tab this row owns, so closing can scope itself to them. Copied: closing
                 // splices tab arrays, and g.tabs is the shared TAB_GROUPS definition.
                 ids:    g ? g.tabs.slice()     : [t.id] });
    }
    return out;
  }
  // Route through the TAB, not the window: after docking, w may be showing an UNRELATED tab, and
  // acting on it there hits the host instead of what was asked for. Only toggle when the requested
  // tab is ALREADY shown.
  function mbActivate(e) {
    const w = e.win, target = e.target;
    if (w && (!target || w.tabs.indexOf(target.id) >= 0)) {
      const wasMin = w.min;
      if (wasMin) wmRestore(w);
      if (target && w.tab !== target.id) { wmSwitchTab(w, target); wmFocus(w); closeMbDrop(); return; }
      if (!wasMin) {
        // Open and showing: the menu entry is a toggle and it CLOSES. This used to minimize, which
        // left the window alive behind a restore chip and read as the click having done nothing.
        // Scope it to the tabs this ROW owns: a window can hold panels docked in from elsewhere,
        // and those must survive. Only when the row accounts for every tab does the window go.
        const mine = e.ids || (target ? [target.id] : []);
        if (mine.length && w.tabs.every(id => mine.indexOf(id) >= 0)) wmClose(w.wid);
        else mine.forEach(id => { if (w.tabs.indexOf(id) >= 0) wmCloseTabIn(w, id); });
        closeMbDrop();
        return;
      }
      wmFocus(w); closeMbDrop(); return;
    }
    if (target) openTab(target);   // lives in another window (or nowhere): openTab finds it
  }
  // The categories that did not fit, as one menu. Rows behave exactly like the bar buttons:
  // a category holding a single panel opens it, anything else opens that category menu.
  function openMbMore(btn, hidden) {
    closeMbDrop();
    wm.mbDrop = "__more";
    btn.classList.add("open");
    const dd = document.createElement("div");
    dd.id = "mbdrop";
    for (const c of hidden) {
      const b = document.createElement("button");
      b.className = "tab";
      b.innerHTML = '<span class="tab-label"></span><span class="tab-more">&rsaquo;</span>';
      b.querySelector(".tab-label").textContent = c.id;
      b.addEventListener("click", () => {
        const es = mbCatEntries(c.id);
        if (es.length === 1) { closeMbDrop(); mbActivate(es[0]); return; }
        openMbDrop(c.id, btn);
      });
      dd.appendChild(b);
    }
    document.body.appendChild(dd);
    const br = btn.getBoundingClientRect();
    const vw = window.innerWidth || 1280, vh = window.innerHeight || 720;
    let x = Math.round(br.right - (dd.offsetWidth || 200));
    x = Math.max(6, Math.min(vw - (dd.offsetWidth || 200) - 6, x));
    dd.style.left = x + "px";
    if (wm.barY === "bottom") dd.style.bottom = Math.round(vh - br.top + 8) + "px";
    else dd.style.top = Math.round(br.bottom + 8) + "px";
    wmRectsSoon();
  }

  // Fit the category strip to the bar: hide from the end until the row fits, and put what
  // was hidden behind one More button. Runs after layout, and again whenever the bar or the
  // client is resized, so it is always measured rather than assumed.
  function mbFitCats() {
    const bar = $("menubar"); if (!bar || bar.classList.contains("pill")) return;
    const cats = bar.querySelector(".mb-cats"); if (!cats) return;
    const all = Array.prototype.slice.call(cats.querySelectorAll(".mb-cat:not(.mb-more)"));
    if (!all.length) return;
    let more = cats.querySelector(".mb-more");
    if (!more) {
      more = document.createElement("button");
      more.className = "mb-cat mb-more";
      more.title = "More categories";
      more.textContent = "More";
      more.addEventListener("click", () => {
        if (wm.mbDrop === "__more") { closeMbDrop(); return; }
        const hidden = mbHiddenCats();
        if (hidden.length) openMbMore(more, hidden);
      });
      cats.appendChild(more);
    }
    for (const b of all) b.style.display = "";
    more.style.display = "none";
    // Overflow is measured on the BAR: the strip is allowed to shrink, so its own
    // scrollWidth reports a fit that the bar as a whole does not have.
    const fits = () => cats.scrollWidth <= cats.clientWidth + 1;
    if (fits()) { mbHiddenCatIds = []; return; }
    more.style.display = "";
    const hiddenIds = [];
    for (let i = all.length - 1; i >= 0 && !fits(); i--) {
      all[i].style.display = "none";
      hiddenIds.unshift(all[i].dataset.cat);
    }
    mbHiddenCatIds = hiddenIds;
    if (!hiddenIds.length) more.style.display = "none";
  }
  let mbHiddenCatIds = [];
  function mbHiddenCats() {
    const ids = mbHiddenCatIds.slice();
    return railCategories().filter(c => ids.indexOf(c.id) >= 0);
  }

  function openMbDrop(catId, btn) {
    closeMbDrop();
    wm.mbDrop = catId;
    btn.classList.add('open');
    const dd = document.createElement('div');
    dd.id = 'mbdrop';
    // No category heading: the bar button above it is the label, and it stays lit
    // while its menu is open. (It earned its place when the button was an icon.)
    for (const e of mbCatEntries(catId)) {
      const w = e.win, target = e.target;
      const b = document.createElement('button');
      // Lit only when the requested TAB is on screen -- after docking, w can hold it while
      // showing something else, and rolled hides the body just like minimized does.
      b.className = 'tab' + ((w && !w.min && !w.rolled && target && w.tab === target.id) ? ' is-active' : '');
      b.innerHTML = '<span class="tab-label"></span>' + (w ? '<span class="tab-on"></span>' : '');
      b.querySelector('.tab-label').textContent = e.label;
      b.addEventListener('click', () => mbActivate(e));
      dd.appendChild(b);
    }
    document.body.appendChild(dd);
    const br = btn.getBoundingClientRect();
    const vw = window.innerWidth || 1280, vh = window.innerHeight || 720;
    let x = Math.round(br.left - 8);
    x = Math.max(6, Math.min(vw - (dd.offsetWidth || 220) - 6, x));
    dd.style.left = x + 'px';
    if (wm.barY === 'bottom') dd.style.bottom = Math.round(vh - br.top + 8) + 'px';
    else dd.style.top = Math.round(br.bottom + 8) + 'px';
    wmRectsSoon();
  }
  document.addEventListener('mousedown', (e) => {
    if (!wm.mbDrop) return;
    if (e.target && e.target.closest && (e.target.closest('#mbdrop') || e.target.closest('#menubar'))) return;
    closeMbDrop();
  }, true);

  function renderMenubar() {
    const bar = $('menubar');
    if (!bar) return;
    const keepDrop = wm.mbDrop;
    bar.classList.toggle('pill', !!wm.barPill);
    bar.innerHTML = '';
    // Drag handle (grip dots): move along the top/bottom edges.
    const drag = document.createElement('div');
    drag.className = 'mb-drag';
    drag.title = 'Move bar';
    drag.innerHTML = '<i class="g g-grip"></i>';
    drag.addEventListener('mousedown', (e) => {
      if (e.button !== 0) return;
      e.preventDefault();
      bar.classList.add('dragging');
      // Keep the grab point under the cursor: the bar is positioned by its centre, so carry
      // the offset from where it was grabbed to its centre through the whole drag.
      const r0 = bar.getBoundingClientRect();
      const offX = (r0.left + r0.width / 2) - e.clientX, offY = (r0.top + r0.height / 2) - e.clientY;
      const mv = (ev) => {
        const vw = window.innerWidth || 1280, vh = window.innerHeight || 720;
        wm.barX = Math.min(0.98, Math.max(0.02, (ev.clientX + offX) / Math.max(1, vw)));
        wm.barYf = Math.min(1, Math.max(0, (ev.clientY + offY) / Math.max(1, vh)));
        positionMenubar();
        wmPushRects();
      };
      const up = () => {
        document.removeEventListener('mousemove', mv);
        document.removeEventListener('mouseup', up);
        bar.classList.remove('dragging');
        wmSaveSoon();
      };
      document.addEventListener('mousemove', mv);
      document.addEventListener('mouseup', up);
    });
    bar.appendChild(drag);
    // Category launchers (a dot marks categories with open windows).
    const cats = document.createElement('nav');
    cats.className = 'mb-cats';
    // A vertical wheel over an x-only scroller is not reliably translated for us, and the
    // categories only overflow on a small client -- exactly when the user most needs to
    // reach them. Map the wheel by hand, and only consume it when there is somewhere to go.
    cats.addEventListener('wheel', (e) => {
      const max = cats.scrollWidth - cats.clientWidth;
      if (max <= 0) return;
      const d = (Math.abs(e.deltaX) > Math.abs(e.deltaY)) ? e.deltaX : e.deltaY;
      if (!d) return;
      const before = cats.scrollLeft;
      cats.scrollLeft = Math.max(0, Math.min(max, before + d));
      if (cats.scrollLeft !== before) e.preventDefault();
    }, { passive: false });
    const openCats = new Set();
    for (const w of wm.wins.values()) for (const id of w.tabs) {
      const t = allTabs().find(x => x.id === id);   // every docked tab, not just the shown one
      if (t) openCats.add(t.cat || 'General');
    }
    for (const c of railCategories()) {
      const b = document.createElement('button');
      b.className = 'mb-cat' + (openCats.has(c.id) ? ' has-open' : '');
      b.title = c.id;
      b.dataset.cat = c.id;
      b.textContent = c.id;          // a legible word beats a 17px line glyph
      b.addEventListener('click', () => {
        if (wm.mbDrop === c.id) { closeMbDrop(); return; }
        // A category holding one panel would open a menu of one row repeating the button's own
        // label (Quests > Quests), which is a question with a single answer. Open it directly.
        // Five of the ten categories are like this, and Plugins joins them when nothing is
        // installed, so the count is measured here per click rather than declared in CAT_META.
        const es = mbCatEntries(c.id);
        if (es.length === 1) { closeMbDrop(); mbActivate(es[0]); return; }
        openMbDrop(c.id, b);
      });
      cats.appendChild(b);
    }
    bar.appendChild(cats);
    // Minimized-window chips: a minimized window stays visibly PRESENT here (it
    // looked "completely closed" without an affordance); click restores it. Built in the
    // COLLAPSED bar too -- collapsing is about reclaiming the category rail, and dropping
    // the chips with it stranded every minimized window with nothing left to click.
    const minimized = Array.from(wm.wins.values()).filter(w => w.min);
    if (minimized.length) {
      const chips = document.createElement('div');
      chips.className = 'mb-chips';
      for (const w of minimized) {
        const t = allTabs().find(x => x.id === w.tab) || { label: w.tab, icon: '' };
        const lbl = wmIsWholeGroup(w) ? w.group.label : t.label;   // same rule as the title bar
        const chip = document.createElement('button');
        chip.className = 'mb-chip';
        chip.title = 'Restore ' + lbl;
        chip.innerHTML = '<span></span>';
        chip.querySelector('span').textContent = lbl;
        chip.addEventListener('click', () => { wmRestore(w); wmFocus(w); });
        chips.appendChild(chip);
      }
      bar.appendChild(chips);
    }
    // Player status: renderHeader() writes these ids (kept from the old header).
    const st = document.createElement('div');
    st.className = 'mb-status';
    st.innerHTML = '<div class="title" id="hdr-title"><span class="dot"></span>' +
                   '<span id="hdr-name">--</span></div><div class="meta" id="hdr-meta">--</div>';
    bar.appendChild(st);
    const info = document.createElement('div'); info.className = 'mb-info'; info.id = 'hdr-info';
    bar.appendChild(info);
    try { uiBarTick(); } catch (e) {}
    // Search + pill toggle.
    const search = document.createElement('button');
    search.className = 'mb-btn mb-search';
    search.title = 'Search panels (Ctrl+K)';
    search.textContent = 'Search';
    search.addEventListener('click', openTabSearch);
    bar.appendChild(search);
    // Borderless fullscreen moved off the bar into its own panel (Utility > Fullscreen,
    // renderFullscreen) -- the bar keeps only window chrome: search, chips, collapse.
    const pill = document.createElement('button');
    pill.className = 'mb-btn';
    pill.title = wm.barPill ? 'Expand bar' : 'Collapse bar';
    // Guillemets: plain Latin-1 text, so they cannot mis-render the way paths did.
    pill.textContent = wm.barPill ? '»' : '«';
    pill.addEventListener('click', () => { wm.barPill = !wm.barPill; closeMbDrop(); renderMenubar(); wmSaveSoon(); });
    bar.appendChild(pill);
    positionMenubar();
    _hdrSig = '';                       // status ids were rebuilt: force a header repaint
    try { renderHeader(); } catch (e) {}
    // Keep an open dropdown alive across rebuilds (open-state dots change under it).
    // Fit before restoring any open menu, so the button it anchors to is in its final place.
    try { mbFitCats(); } catch (e) {}
    if (keepDrop) {
      const btn = bar.querySelector('.mb-cat[data-cat="' + keepDrop + '"]');
      wm.mbDrop = null;
      if (btn) openMbDrop(keepDrop, btn);
    }
    wmRectsSoon();
  }
  // Measured after the bar exists, and again on any resize: the bar is content sized, so a
  // narrower client is exactly when categories start to fall off the end.
  window.addEventListener("resize", () => { try { mbFitCats(); } catch (e) {} });

  function renderSidebar() { renderMenubar(); }

  // Fullscreen panel (Utility): borderless fullscreen for the embedded client. It was a
  // button on the main bar; it lives in its own panel now. The GAME's own Fullscreen
  // option cannot work while embedded (its window is our child, clipped to the host),
  // so this drives the HOST: it takes the whole monitor and the game comes with it.
  // ---- Wiki pane: launcher + settings for the in-client wiki browser. The browser
  //      itself is a native pane locked to runescape.wiki; this panel just starts it,
  //      binds the palette hotkey, and explains the feature. ----
  function renderWikiPane() {
    const c = $('content');
    if ($('wkWrap')) { return; }
    c.innerHTML = '';
    const wrap = document.createElement('div'); wrap.id = 'wkWrap'; wrap.className = 'ov-wrap';
    if (!bridge() || !bridge().wikiOpen) {
      const e = document.createElement('div'); e.className = 'empty';
      e.textContent = 'The wiki browser needs an updated host.';
      wrap.appendChild(e); c.appendChild(wrap); return;
    }
    const desc = document.createElement('div'); desc.className = 'ov-hint';
    desc.textContent = 'Browse the RuneScape Wiki in a pane docked inside the client. '
      + 'Locked to runescape.wiki: navigation anywhere else is blocked. '
      + 'The hotkey opens a search overlay from anywhere in game.';
    wrap.appendChild(desc);
    const row = document.createElement('div');
    row.style.cssText = 'display:flex;gap:8px;margin:10px 0';
    const inp = document.createElement('input');
    inp.className = 'pet-search'; inp.placeholder = 'Search the wiki…';
    inp.style.flex = '1'; inp.spellcheck = false;
    const go = document.createElement('button'); go.className = 'dg-sync-btn'; go.textContent = 'Open';
    const open = () => { try { bridge().wikiOpen(myPid(), (inp.value || '').trim()); } catch (e) {} };
    go.addEventListener('click', open);
    inp.addEventListener('keydown', (e) => { if (e.key === 'Enter') { e.preventDefault(); open(); } });
    row.appendChild(inp); row.appendChild(go);
    wrap.appendChild(row);
    // Hotkey row, same interaction as the screenshot keybind: click, press a key.
    const kbRow = document.createElement('div');
    kbRow.style.cssText = 'display:flex;align-items:center;gap:10px;margin:6px 0';
    const kbLbl = document.createElement('span'); kbLbl.className = 'ov-hint'; kbLbl.style.margin = '0';
    const kbBtn = document.createElement('button'); kbBtn.className = 'dg-sync-btn';
    let listening = false;
    const paint = () => {
      let vk = 0; try { vk = (bridge().wikiKeybindGet() | 0); } catch (e) {}
      kbLbl.textContent = 'Search hotkey:'; kbBtn.textContent = vk ? ssVkName(vk) : 'Not set';
    };
    kbBtn.addEventListener('click', () => {
      if (listening) return; listening = true; kbGrab(true);
      kbBtn.textContent = 'Press a key… (Esc clears)';
      const cap = (e) => {
        e.preventDefault(); e.stopPropagation();
        document.removeEventListener('keydown', cap, true);
        listening = false; kbGrab(false);
        try { bridge().wikiKeybindSet(e.key === 'Escape' ? 0 : (e.keyCode | 0)); } catch (e2) {}
        paint();
        // Release focus + capture NOW: with either still held, the freshly-bound key
        // reads as "typing" on its next press and feeds the UI instead of the keybind
        // (seen live as "I had to press the hotkey twice").
        try { if (document.activeElement) document.activeElement.blur(); } catch (e3) {}
        syncKbCapture();
      };
      document.addEventListener('keydown', cap, true);
    });
    paint();
    kbRow.appendChild(kbLbl); kbRow.appendChild(kbBtn);
    wrap.appendChild(kbRow);
    const closeBtn = document.createElement('button'); closeBtn.className = 'dg-sync-btn';
    closeBtn.textContent = 'Close wiki pane';
    closeBtn.style.marginTop = '4px';
    closeBtn.addEventListener('click', () => { try { bridge().wikiClose(myPid()); } catch (e) {} });
    wrap.appendChild(closeBtn);
    c.appendChild(wrap);
  }

  function renderFullscreen() {
    const c = $('content');
    if ($('fsWrap')) {
      const r = $('fsTgl'); if (r) r.classList.toggle('on', wm.fullscreen);
      return;
    }
    c.innerHTML = '';
    const wrap = document.createElement('div'); wrap.id = 'fsWrap'; wrap.className = 'ov-wrap';
    if (!bridge() || !bridge().hostFullscreen) {
      const e = document.createElement('div'); e.className = 'empty';
      e.textContent = 'Fullscreen needs an embedded client window.';
      wrap.appendChild(e); c.appendChild(wrap); return;
    }
    const row = ovToggleRow('fsTgl', 'Borderless fullscreen',
                            'The client window takes the whole monitor, title bar hidden');
    row.classList.toggle('on', wm.fullscreen);
    row.addEventListener('click', () => {
      try { wm.fullscreen = !!bridge().hostFullscreen(myPid()); } catch (e) {}
      row.classList.toggle('on', wm.fullscreen);
      wmRectsSoon();
    });
    wrap.appendChild(row);
    const hint = document.createElement('div'); hint.className = 'ov-hint';
    hint.textContent = 'Per-session view state: it is not saved, and the game\'s own Fullscreen setting stays unavailable while embedded.';
    wrap.appendChild(hint);
    c.appendChild(wrap);
  }

  // ---- Tab search palette (Ctrl+K / '/' / the rail magnifier): type to jump to any
  //      panel, plugin tabs included. Arrow keys + Enter select; Esc / backdrop close. ----
  let tspSel = 0;
  function tspEntries(q) {
    const all = allTabs().filter(t => !t.hidden);
    if (!q) return all;
    const s = q.toLowerCase();
    const score = t => {
      const l = t.label.toLowerCase(), c = (t.cat || '').toLowerCase();
      if (l.startsWith(s)) return 0;
      if (l.split(/\s+/).some(w => w.startsWith(s))) return 1;
      if (l.includes(s)) return 2;
      if (c.startsWith(s)) return 3;
      if (c.includes(s)) return 4;
      return -1;
    };
    return all.map(t => [score(t), t]).filter(p => p[0] >= 0)
              .sort((a, b) => a[0] - b[0]).map(p => p[1]);
  }
  function tspJump(t) {
    closeTabSearch();
    openTab(t);
  }
  function tspRenderList() {
    const list = $('tspList'); if (!list) return;
    const inp = $('tspIn');
    const es = tspEntries(inp ? inp.value.trim() : '');
    if (tspSel >= es.length) tspSel = es.length ? es.length - 1 : 0;
    list.innerHTML = '';
    if (!es.length) {
      const d = document.createElement('div'); d.className = 'tsp-empty'; d.textContent = 'No matching panel';
      list.appendChild(d); return;
    }
    es.forEach((t, i) => {
      const r = document.createElement('div');
      r.className = 'tsp-row' + (i === tspSel ? ' sel' : '');
      r.innerHTML = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" ' +
        'stroke-linecap="round" stroke-linejoin="round">' + (t.icon || '') + '</svg>' +
        '<span></span><span class="tsp-cat"></span>';
      r.children[1].textContent = t.label;
      r.children[2].textContent = t.cat || '';
      r.addEventListener('click', () => tspJump(t));
      r.addEventListener('mousemove', () => { if (tspSel !== i) { tspSel = i; tspRenderList(); } });
      list.appendChild(r);
    });
    const sel = list.children[tspSel];
    if (sel && sel.scrollIntoView) sel.scrollIntoView({ block: 'nearest' });
  }
  function closeTabSearch() {
    // Blur any focused field first, or the host keeps keyboard capture forever
    // (focusout never fires for removed nodes in Ultralight).
    const ov = $('tspOv');
    if (ov) {
      try { const a = document.activeElement; if (a && ov.contains(a)) a.blur(); } catch (e) {}
      ov.remove();
    }
    wmRectsSoon();
  }
  function openTabSearch() {
    if ($('tspOv')) { closeTabSearch(); return; }     // second Ctrl+K toggles it away
    tspSel = 0;
    const ov = document.createElement('div'); ov.className = 'tsp-ov'; ov.id = 'tspOv';
    ov.addEventListener('click', e => { if (e.target === ov) closeTabSearch(); });
    const box = document.createElement('div'); box.className = 'tsp-box';
    const inp = document.createElement('input');
    inp.className = 'tsp-in'; inp.id = 'tspIn'; inp.type = 'text';
    inp.placeholder = 'Jump to panel…';
    inp.autocomplete = 'off'; inp.spellcheck = false;
    const list = document.createElement('div'); list.className = 'tsp-list'; list.id = 'tspList';
    inp.addEventListener('input', () => { tspSel = 0; tspRenderList(); });
    inp.addEventListener('keydown', e => {
      const es = tspEntries(inp.value.trim());
      if (e.key === 'ArrowDown')      { e.preventDefault(); if (es.length) { tspSel = (tspSel + 1) % es.length; tspRenderList(); } }
      else if (e.key === 'ArrowUp')   { e.preventDefault(); if (es.length) { tspSel = (tspSel + es.length - 1) % es.length; tspRenderList(); } }
      else if (e.key === 'Enter')     { e.preventDefault(); if (es[tspSel]) tspJump(es[tspSel]); }
      else if (e.key === 'Escape')    { e.preventDefault(); closeTabSearch(); }
      else if (e.key === 'Backspace' && (e.ctrlKey || e.altKey)) {
        // Delete the previous word. The host drops the control character this combo produces
        // (it would otherwise be typed as a box), so without this the key would do nothing.
        e.preventDefault();
        const end = inp.selectionStart === inp.selectionEnd ? inp.selectionStart : inp.selectionEnd;
        const head = inp.value.slice(0, end);
        const cut = head.replace(/[^\s]*\s*$/, '');
        inp.value = cut + inp.value.slice(end);
        inp.selectionStart = inp.selectionEnd = cut.length;
        tspSel = 0; tspRenderList();
      }
      e.stopPropagation();
    });
    box.appendChild(inp); box.appendChild(list);
    ov.appendChild(box);
    document.body.appendChild(ov);
    tspRenderList();
    inp.focus();
    wmRectsSoon();   // the palette is modal: claim the whole client for input
  }
  document.addEventListener('keydown', e => {
    if (typeof markerCapturing !== 'undefined' && markerCapturing) return;  // a keybind field is armed
    if (typeof ssCapturing !== 'undefined' && ssCapturing) return;          // screenshot keybind armed
    const k = (e.key || '').toLowerCase();
    if (k === 'escape' && $('tspOv')) { closeTabSearch(); return; }
    if (k === 'escape') {
      // Esc in a text field: drop focus so keyboard capture returns to the game.
      const t0 = e.target;
      if (t0 && (t0.tagName === 'INPUT' || t0.tagName === 'TEXTAREA' || t0.isContentEditable)) {
        t0.blur();
        return;
      }
      if (wm.mbDrop) { closeMbDrop(); return; }
    }
    if ((e.ctrlKey || e.metaKey) && k === 'k') { e.preventDefault(); openTabSearch(); return; }
    if ((e.ctrlKey || e.metaKey) && k === 'c') { if (copySelection()) e.preventDefault(); return; }
    if (k === '/' && !e.ctrlKey && !e.altKey && !e.metaKey) {
      const t = e.target;
      const typing = t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.isContentEditable);
      if (!typing) { e.preventDefault(); openTabSearch(); }
    }
  });

  // Copy-out support. Ultralight has NO clipboard integration (no navigator.clipboard, no
  // native Ctrl+C, no context menu), so the current selection is forwarded to the OS
  // clipboard through the bridge (copyClipboard). Returns false when there is nothing
  // selected or the bridge predates the function.
  function copySelection() {
    let sel = '';
    const t = document.activeElement;
    if (t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA') && t.selectionStart !== t.selectionEnd)
      sel = String(t.value).substring(t.selectionStart, t.selectionEnd);
    if (!sel) { try { sel = String(window.getSelection() || ''); } catch (e) {} }
    if (!sel) return false;
    try { const b = bridge(); if (b && b.copyClipboard) { b.copyClipboard(sel); return true; } } catch (e) {}
    return false;
  }
  document.addEventListener('contextmenu', e => {
    const old = $('ctxCopy'); if (old) { old.remove(); try { wmRectsSoon(); } catch (e2) {} }
    let sel = '';
    try { sel = String(window.getSelection() || ''); } catch (e2) {}
    if (!sel.trim()) return;
    e.preventDefault();
    const m = document.createElement('button'); m.id = 'ctxCopy'; m.className = 'ctx-copy'; m.textContent = 'Copy';
    m.style.left = Math.min(e.clientX, window.innerWidth - 70) + 'px';
    m.style.top = Math.min(e.clientY, window.innerHeight - 34) + 'px';
    const onDown = (ev) => { if (ev.target !== m) { m.remove(); try { wmRectsSoon(); } catch (e2) {} document.removeEventListener('mousedown', onDown, true); } };
    // mousedown (not click): fires before the page mousedown clears the selection
    m.addEventListener('mousedown', ev => {
      ev.preventDefault(); ev.stopPropagation();
      copySelection(); m.remove();
      try { wmRectsSoon(); } catch (e2) {}
      document.removeEventListener('mousedown', onDown, true);
    });
    document.body.appendChild(m);
    try { wmRectsSoon(); } catch (e2) {}
    document.addEventListener('mousedown', onDown, true);
  });

  function row(k, v) {
    const r = document.createElement('div'); r.className = 'row';
    const ks = document.createElement('span'); ks.className = 'k'; ks.textContent = k;
    const vs = document.createElement('span'); vs.className = 'v'; vs.textContent = v;
    r.appendChild(ks); r.appendChild(vs);
    return r;
  }
  // Section heading for a .rows list, so a long readout can be grouped instead of
  // reading as one undifferentiated column.
  function secRow(label) {
    const d = document.createElement('div'); d.className = 'sec'; d.textContent = label;
    return d;
  }

  // ---- Storage tab -> panel_storage.js (spliced inline at load) ----
  // ---- Containers tab -> panel_containers.js (spliced inline at load) ----
  // ---- POF tab -> panel_pof.js (spliced inline at load) ----
  // ---- Abilities tab -> panel_abilities.js (spliced inline at load) ----
  // ---- Pets tab -> panel_pets.js (spliced inline at load) ----
  // ---- Bosses tab -> panel_bosses.js (spliced inline at load) ----
  // ---- Quests tab -> panel_quests.js (spliced inline at load) ----
  // ---- Quest Guides tab -> panel_questguides.js (spliced inline at load) ----
  // ---- XP Tracker tab -> panel_xptracker.js (spliced inline at load) ----
  // ---- Chat Log tab -> panel_chatlog.js (spliced inline at load) ----
  // ---- Achievements tab -> panel_achievements.js (spliced inline at load) ----
