// rtx-pane.js: Scan-clue orb helpers, row/secRow, renderPane (the per-tab render dispatch) and renderHeader.
// Loads after: rtx-menubar.js and every core file before it; the panels it dispatches to are resolved at call time.
// Plain script, page globals by design: every top-level name here is a page global that panels and the other core files use.
  // ---- Scan elimination from the orb (interface 1752) + player tile. The ring's blue band == "too far"
  // (sprite 113): destination is > 2*range, so spots within 2*range of you are dropped. When the orb glows
  // (sprite 131, within detection): spots beyond 2*range are dropped. Chebyshev distance; safe + convergent.
  function scanOrbRead() {
    try {
      const ws = (JSON.parse(bridge().interfaceGroup(myPid(), 1752) || '{}').widgets) || [];
      if (ws.some(w => w.s === 131 && w.v)) return 'in';     // "the orb glows as you..."
      if (ws.some(w => w.s === 113 && w.v)) return 'far';    // "you are too far away and nothing scans"
    } catch (e) {}
    return 'unknown';
  }
  function scanLiveRange() {
    try {
      for (const w of ((JSON.parse(bridge().interfaceGroup(myPid(), 1752) || '{}').widgets) || [])) {
        if (w.x) { const m = String(w.x).replace(/<[^>]*>/g, '').match(/range of this orb is (\d+)/i); if (m) return +m[1]; }
      }
    } catch (e) {}
    return 0;
  }
  async function scanPlayerTile() {
    try { const d = JSON.parse(await bridge().playerInfo(myPid())); if (d && typeof d.x === 'number') return { x: d.x | 0, y: d.y | 0, p: d.plane | 0 }; } catch (e) {}
    return null;
  }


  let _playerPaneSig = '';   // Skills-grid dedup (see the guard in renderPane)
  // (The consolidated-panel sub-tab strip moved into each window's title bar --
  // see wmRenderTabs. renderPane runs with activeTab/__paneRoot pointed at ONE
  // window by renderPaneFor/withPane; it must not touch shell chrome.)

  function renderPane() {
    // Plugins own their (sandboxed iframe) DOM; mount before any content wipe.
    if (activeTab === 'pluginbrowse') { renderPluginBrowse(); return; }
    if (activeTab.indexOf('plugin:') === 0) { renderPlugin(activeTab.slice(7)); return; }
    if (activeTab.indexOf('aura:') === 0) { if (typeof renderAuraGroup === 'function') renderAuraGroup(activeTab.slice(5)); return; }
    // Registered panels (registerTab in each panel_*.js IIFE) render here, before the
    // content wipe below: panels such as Bank manage their own DOM incrementally (search
    // box keeps focus across the 250 ms refresh), so they must run before it is wiped.
    { const P = RTX.panels[activeTab]; if (P && typeof P.render === 'function') { P.render(); return; } }
    // Legacy if-chain: tabs rendered by this script itself, or gated on more than the id.
    if (activeTab === 'materials') { renderMaterials(); return; }
    if (activeTab === 'baitbox') { renderBaitBox(); return; }
    if (activeTab === 'artefacts') { renderWorkbench(); return; }
    if (activeTab === 'archshop') { renderArchShop(); return; }
    if (activeTab === 'dung') { if (DUNG_ENABLED) renderDungeoneering(); return; }
    if (activeTab === 'fullscreen') { renderFullscreen(); return; }
    if (activeTab === 'wiki') { renderWikiPane(); return; }
    if (activeTab === 'uisettings') { renderUiSettings(); return; }
    // Skills tab (the default landing tab): renderPane runs on every 250ms poll tick, but the grid
    // only depends on the skill array, the goal targets and the virtual-levels setting -- skip the
    // full ~29-cell rebuild (and the icon re-attachment churn) when none of that changed.
    if (activeTab === 'player' && lastSnap) {
      fetchSkillBonus();   // async, self-throttled; re-renders when bonus XP changes
      const psig = JSON.stringify(lastSnap.skills || null) + '|' +
                   (virtualLevelsOn() ? 1 : 0) + '|' + JSON.stringify(gameGoals) + '|' + skillBonusSig;
      if (psig === _playerPaneSig && $('content').querySelector('.skill-grid')) return;
      _playerPaneSig = psig;
    }
    const c = $('content'); c.innerHTML = '';
    if (!lastSnap) {
      const e = document.createElement('div'); e.className = 'empty';
      e.textContent = 'Waiting for game data...';
      c.appendChild(e); return;
    }
    const s = lastSnap;
    const wrap = document.createElement('div'); wrap.className = 'pane';

    const rows = document.createElement('div'); rows.className = 'rows';

    if (activeTab === 'player') {
      const sk = Array.isArray(s.skills) ? s.skills : [];
      if (sk.length === 0) {
        const e = document.createElement('div'); e.className = 'empty';
        e.textContent = 'Skill data appears once in-world.';
        wrap.appendChild(e); c.appendChild(wrap); return;
      }
      // In-game XP bars toggle: draws a progress bar on each cell of the GAME's Skills
      // panel (not this grid), so it is useful with this window closed.
      if (bridge() && bridge().skillBars) {
        skBarsLoad();
        // The shared toggle row (same control the Rendering panel uses), and it flips its
        // OWN class rather than re-rendering the pane -- the grid rebuild is signature-
        // deduped, so asking it to repaint just to show a toggle state did nothing.
        // The sub-line doubles as the diagnostic when nothing appears: it names which
        // stage failed rather than leaving a silent toggle. Repainted live by the tick.
        const row = ovToggleRow('skBarsTgl', 'XP bars on the in-game skills panel', skBarsWhyText());
        row.classList.toggle('on', skBarsOn);
        row.addEventListener('click', () => {
          skBarsSet(!skBarsOn);
          row.classList.toggle('on', skBarsOn);
        });
        wrap.appendChild(row);
      }
      const grid = document.createElement('div'); grid.className = 'skill-grid';
      let totalLevel = 0, totalXp = 0, virtualTotal = 0, totalBonus = 0;
      SKILL_LAYOUT.forEach(i => {
        const t = sk[i]; if (!t) return;
        const real = t[0], boost = t[1], xp = (t[2] === undefined ? -1 : t[2]);
        totalLevel += real;
        if (xp >= 0) totalXp += xp;
        // Virtual level: every skill up to 120, Invention (elite, idx 26) up to 150.
        let vlev = real;
        if (xp >= 0) { const elite = (i === 26); vlev = Math.min(elite ? 150 : 120, levelFromXp(xp, elite)); if (vlev < real) vlev = real; }
        virtualTotal += vlev;

        const cell = document.createElement('div'); cell.className = 'skill-cell';
        if (boost > real) cell.classList.add('buffed');
        if (boost < real) cell.classList.add('drained');

        const name = SKILL_NAMES[i] || ('#' + i);
        const ico  = document.createElement('div'); ico.className = 'sk-icon';
        ico.dataset.skill = String(i);
        attachSkillIcon(ico, i);
        const lvl  = document.createElement('span'); lvl.className = 'sk-val';
        lvl.textContent = (boost !== real) ? (boost + '/' + real) : String(real);
        cell.appendChild(ico);
        cell.appendChild(lvl);

        // xp = -1 only when the read failed (e.g. not in-world yet).
        const lines = [
          name,
          'ID ' + i,
          'Level ' + real + (boost !== real ? ' (boosted ' + boost + ')' : ''),
        ];
        if (xp >= 0) {
          lines.push('XP ' + xp.toLocaleString());
          const nx = xpToNext(xp, i === 26);   // Invention uses the elite table
          lines.push(nx.next > 0 ? (nx.next.toLocaleString() + ' to level ' + nx.level)
                                 : 'Max XP');
        } else {
          lines.push('XP unavailable');
        }
        const gp = skillGoalProgress(i, xp);
        if (gp) {
          cell.classList.add('has-goal');
          const bar = document.createElement('div'); bar.className = 'sk-goal' + (gp.done ? ' done' : '');
          const fill = document.createElement('div'); fill.className = 'sk-goal-fill';
          fill.style.flexGrow = gp.pct.toFixed(4);
          const pad = document.createElement('div'); pad.className = 'sk-goal-pad';
          pad.style.flexGrow = (1 - gp.pct).toFixed(4);
          bar.appendChild(fill); bar.appendChild(pad); cell.appendChild(bar);
          lines.push('In-game target: ' + (gp.mode === 'xp' ? (gp.target.toLocaleString() + ' XP') : ('level ' + gp.target)) +
                     ' - ' + Math.floor(gp.pct * 100) + '%' + (gp.done ? ' (reached)' : ''));
        }
        // Bonus XP (from the game's own bonus-xp varps; value already /10). The TOTAL must truncate per
        // skill BEFORE summing -- the game's own total (script9202) does integer int/10 per skill, so
        // summing the raw tenths and flooring once drifts a few XP above the in-game figure.
        const bonus = skillBonusFor(i);
        if (bonus > 0) {
          totalBonus += Math.floor(bonus);
          cell.classList.add('has-bonus');
          const bp = document.createElement('span'); bp.className = 'sk-bonus';
          bp.textContent = skillBonusBadge(bonus);
          cell.appendChild(bp);
          lines.push('Bonus XP: ' + skillBonusFmt(bonus));
        }
        cell.dataset.tip = lines.join('\n');
        grid.appendChild(cell);
      });
      wrap.appendChild(grid);

      const stats = document.createElement('div'); stats.className = 'skill-stats';
      // Long values shrink a step (or two) so they never get clipped in a narrow tile.
      const stat = (label, val) => {
        const d = document.createElement('div'); d.className = 'skill-stat';
        const s = String(val);
        const cls = s.length >= 13 ? ' ss-long2' : s.length >= 10 ? ' ss-long' : '';
        d.innerHTML = '<span class="ss-label">' + label + '</span>' +
                      '<span class="ss-val' + cls + '">' + s + '</span>';
        d.dataset.tip = label + ': ' + s;
        return d;
      };
      stats.appendChild(stat('Combat', combatLevel(sk)));
      if (virtualLevelsOn()) {
        const d = document.createElement('div'); d.className = 'skill-stat';
        d.innerHTML = '<span class="ss-label">Total</span>' +
                      '<span class="ss-val">' + totalLevel.toLocaleString() + '</span>' +
                      '<span class="ss-virtual">' + virtualTotal.toLocaleString() + ' virtual</span>';
        d.dataset.tip = 'Total level: ' + totalLevel.toLocaleString() +
                        '\nVirtual total: ' + virtualTotal.toLocaleString() +
                        ' (levels past 99/120 derived from XP; Invention to 150)' +
                        '\nShown because the in-game "virtual levels" setting is on' +
                        '\nSource: varbit 19007 = varp 458 bit 30';
        stats.appendChild(d);
      } else {
        stats.appendChild(stat('Total', totalLevel.toLocaleString()));
      }
      stats.appendChild(stat('Total XP', totalXp.toLocaleString()));
      if (totalBonus > 0) {
        const bd = document.createElement('div'); bd.className = 'skill-stat';
        const bs = String(skillBonusFmt(totalBonus));
        bd.innerHTML = '<span class="ss-label">Bonus XP</span>' +
                       '<span class="ss-val ss-bonus' + (bs.length >= 13 ? ' ss-long2' : bs.length >= 10 ? ' ss-long' : '') + '">' + bs + '</span>';
        bd.dataset.tip = 'Total unspent bonus XP across all skills\nSource: the game\'s bonus-xp varps (script11151);\ntotal matches the in-game figure (script9202: per-skill truncation)';
        stats.appendChild(bd);
      }
      wrap.appendChild(stats);
      c.appendChild(wrap);
      return;
    } else if (activeTab === 'exchange') {
      // GE COLLECTION BOX, integrated per offer cell: slots 1-8 = containers 523-528,783,784 (enum
      // 1079 order). Fetched async into a cache; the exchange tab repaints every poll, so cells pick
      // it up on the next pass. Coins render as a gp figure (the packed coins icon is the 999Q
      // max-stack art), items as icon + name.
      const GE_COLL_IDS = [523, 524, 525, 526, 527, 528, 783, 784];
      window.geCollCache = window.geCollCache || { slots: null, at: 0 };
      async function geCollFetch() {
        if (!bridge() || !bridge().containerItems) return;
        const t = Date.now();
        if (t - geCollCache.at < 2000) return;
        geCollCache.at = t;
        const out = [];
        for (let i = 0; i < 8; i++) {
          let e = null;
          try {
            const ci = JSON.parse(await bridge().containerItems(myPid(), GE_COLL_IDS[i]) || 'null');
            const items = (ci && Array.isArray(ci.items)) ? ci.items : [];
            for (const it of items) {
              const iid = it[1], stack = it[2] || 0, nm = it[3] || ('Item #' + it[1]);
              e = e || { coins: 0, items: [] };
              if (iid === 995) e.coins += stack;
              else e.items.push({ id: iid, nm: nm, n: stack });
            }
          } catch (err) {}
          out.push(e);
        }
        geCollCache.slots = out;
      }
      function geCollLine(cell, i) {
        const e = geCollCache.slots && geCollCache.slots[i];
        if (!e) return false;
        const d = document.createElement('div'); d.className = 'ge-collect';
        // ONE tooltip per cell: the collect details fold into the cell's own tip instead of nesting
        // [data-tip] spans -- two anchors make the bubble flip position/size while sweeping the row.
        let txt = [], tipParts = [];
        if (e.coins > 0) { txt.push(fmtGp(e.coins) + ' gp'); tipParts.push(e.coins.toLocaleString() + ' coins'); }
        for (const it of e.items) {
          const sp = document.createElement('span'); sp.className = 'ge-collect-it';
          const ico = document.createElement('span'); ico.className = 'ge-collect-ico';
          attachIcon(ico, it.id);
          sp.appendChild(ico);
          if (it.n > 1) { const nn = document.createElement('span'); nn.textContent = '×' + (it.n >= 100000 ? fmtGp(it.n) : it.n.toLocaleString()); sp.appendChild(nn); }
          d.appendChild(sp);
          tipParts.push(it.nm + (it.n > 1 ? ' × ' + it.n.toLocaleString() : ''));
        }
        const lbl = document.createElement('span');
        lbl.textContent = 'Collect' + (txt.length ? ': ' + txt.join(' + ') : '');
        d.insertBefore(lbl, d.firstChild);
        cell.appendChild(d);
        const extra = 'Collect: ' + tipParts.join(', ');
        cell.dataset.collectTip = extra;
        cell.dataset.tip = (cell.dataset.tip ? cell.dataset.tip + '\n' : '') + extra;
        return true;
      }
      injectStyle('geCollCss', '.ge-collect{display:flex;align-items:center;gap:6px;margin-top:5px;padding-top:5px;border-top:1px dashed var(--border);color:var(--ok);font-size:10.5px;font-variant-numeric:tabular-nums}' +
          '.ge-collect-it{display:inline-flex;align-items:center;gap:2px;color:var(--text)}' +
          '.ge-collect-ico{width:18px;height:18px;flex:none;display:inline-block;background-size:contain;background-repeat:no-repeat;background-position:center}');
      geCollFetch();
      const slots = Array.isArray(s.ge_slots) ? s.ge_slots : [];
      const active = slots.filter(g => g && g.status !== 0).length;
      const anyColl = !!(geCollCache.slots && geCollCache.slots.some(x => x));
      if (active === 0 && !anyColl) {
        const e = document.createElement('div'); e.className = 'empty';
        e.textContent = 'No active Grand Exchange offers.';
        wrap.appendChild(e); c.appendChild(wrap); return;
      }
      const grid = document.createElement('div');
      grid.className = 'ge-grid';
      for (let i = 0; i < 8; ++i) {
        const cell = document.createElement('div');
        cell.className = 'ge-cell';
        const g = slots[i];
        if (!g || g.status === 0) {
          cell.classList.add('is-empty');
          cell.textContent = 'Slot ' + (i + 1);
          if (geCollLine(cell, i)) cell.classList.remove('is-empty');
          grid.appendChild(cell);
          continue;
        }
        const type = (g.type === 0) ? 'buy' : 'sell';
        cell.classList.add('ge-' + type);
        const isComplete = (g.status === 5);
        cell.classList.add(isComplete ? 'is-complete' : 'is-active');
        const stateLabel = isComplete ? 'Complete' : 'Active';
        cell.dataset.itemId = String(g.item_id);
        cell.dataset.slot   = String(i + 1);
        cell.dataset.state  = stateLabel;
        attachInfo(cell, g.item_id, i, stateLabel);

        const row1 = document.createElement('div'); row1.className = 'ge-row1';
        const dot  = document.createElement('span'); dot.className = 'ge-state-dot';
        const ico  = document.createElement('div'); ico.className = 'ge-icon';
        ico.dataset.itemId = String(g.item_id);
        attachIcon(ico, g.item_id);
        const t    = document.createElement('span'); t.className = 'ge-type';
        t.textContent = type.toUpperCase();
        row1.appendChild(dot);
        row1.appendChild(ico);
        row1.appendChild(t);
        cell.appendChild(row1);

        const price = document.createElement('div'); price.className = 'ge-price';
        price.textContent = fmtGp(g.price) + ' gp';
        cell.appendChild(price);

        const prog  = document.createElement('div'); prog.className = 'ge-progress';
        prog.innerHTML =
          '<span class="filled">' + g.filled.toLocaleString() + '</span>' +
          '<span> / ' + g.quantity.toLocaleString() + '</span>';
        cell.appendChild(prog);

        if (g.filled_value > 0) {
          const val = document.createElement('div'); val.className = 'ge-value';
          const verb = (g.type === 0) ? 'Spent' : 'Earned';
          val.textContent = verb + ' ' + fmtGp(g.filled_value) + ' gp';
          cell.appendChild(val);
        }

        geCollLine(cell, i);
        grid.appendChild(cell);
      }
      wrap.appendChild(grid);
      c.appendChild(wrap);
      return;
    } else {
      // Display preference: mask the player name in the header (persists; also toggled by clicking the name).
      {
        const pr = document.createElement('div'); pr.className = 'row'; pr.style.alignItems = 'center';
        const pk = document.createElement('span'); pk.className = 'k'; pk.textContent = 'Hide player name';
        const pill = document.createElement('div'); pill.id = 'sysHideNamePill'; pill.className = 'al-pill' + (nameHidden ? ' on' : '');
        pill.title = 'Mask the name shown in the panel header'; pill.innerHTML = '<span></span>';
        pill.addEventListener('click', () => setNameHidden(!nameHidden));
        pr.appendChild(pk); pr.appendChild(pill); rows.appendChild(pr);
        const psep = document.createElement('div'); psep.style.cssText = 'border-top:1px solid var(--border); margin:8px 0 4px;';
        rows.appendChild(psep);
      }
      rows.appendChild(row('PID',         String(s.pid)));
      rows.appendChild(row('Version',     s.client_version || '--'));
      rows.appendChild(row('Process RAM', s.working_set_mb ? (s.working_set_mb + ' MB') : '--'));
      rows.appendChild(row('Process CPU', s.cpu_pct > 0.05 ? (s.cpu_pct.toFixed(1) + ' %') : '0.0 %'));
      if (host) {
        const sep = document.createElement('div');
        sep.style.cssText = 'border-top:1px solid var(--border); margin:8px 0 4px;';
        rows.appendChild(sep);
        rows.appendChild(row('CPU',      host.cpu_name + ' (' + host.cpu_logical_cores + ' cores)'));
        rows.appendChild(row('GPU',      host.gpu_name));
        rows.appendChild(row('System RAM',
          (host.ram_used_mb >= 1024 ? (host.ram_used_mb/1024).toFixed(1) + ' GB' : host.ram_used_mb + ' MB')
          + ' / ' +
          (host.ram_total_mb >= 1024 ? (host.ram_total_mb/1024).toFixed(0) + ' GB' : host.ram_total_mb + ' MB')));
      }
    }
    wrap.appendChild(rows);
    c.appendChild(wrap);
  }

  let _hdrSig = '';
  function renderHeader() {
    // Write the DOM only on change: runs at 4 Hz per client and is static most polls.
    const s = lastSnap;
    let inWorld, nameTxt, metaTxt;
    if (!s) {
      inWorld = false; nameTxt = 'PID ' + myPid(); metaTxt = 'Connecting...';
    } else {
      inWorld = !!s.in_world;
      nameTxt = nameHidden ? 'Hidden' : (s.display_name || ('PID ' + s.pid));
      // Leagues worlds are flagged: their Voice of Seren / world events are a separate
      // pool, so knowing which one you are on explains why the crowdsourced values differ.
      let wtag = '';
      if (s.world > 0) {
        let lg = false;
        try { lg = !!(bridge() && bridge().isLeaguesWorld && bridge().isLeaguesWorld(s.world)); } catch (e) {}
        wtag = (uiCfg().hideWorld ? 'World hidden' : ('World ' + s.world + (lg ? ' (Leagues)' : ''))) + ' • ';
      }
      metaTxt = wtag + fmtStatus(s);
    }
    const sig = (inWorld ? '1' : '0') + '|' + nameTxt + '|' + metaTxt;
    if (sig === _hdrSig) return;                 // unchanged -> no DOM writes this poll
    _hdrSig = sig;
    $('hdr-title').classList.toggle('is-in', inWorld);
    $('hdr-name').textContent = nameTxt;
    $('hdr-meta').textContent = metaTxt;
    // The status text just changed the bar's width: re-clamp it inside the frame (the
    // rebuild measured the bar before this text existed) and republish its consume rect.
    try { positionMenubar(); wmRectsSoon(); } catch (e) {}
  }

