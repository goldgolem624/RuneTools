// RuneToolsX panel: Rendering (hide NPCs/players/scene via the companion).
(function () {

  // Client options as the game holds them (name, value label, range), from the clientState bridge.
  let rndOptSig = '';
  const rndWords = n => String(n).replace(/([a-z])([A-Z])/g, '$1 $2');
  function rndPaintOptions(cs) {
    const box = $('rndOpts'); if (!box) return;
    const named = cs && Array.isArray(cs.optionsNamed) ? cs.optionsNamed : null;
    if (!named || !named.length) { box.style.display = 'none'; rndOptSig = ''; return; }
    const cells = [];
    if (cs.windowModeLabel) cells.push(['Window mode', cs.windowModeLabel, '']);
    if (cs.presetLabel) cells.push(['Preset', cs.presetLabel, '']);
    if (Array.isArray(cs.fullscreenSize) && cs.fullscreenSize[0] > 0) cells.push(['Fullscreen size', cs.fullscreenSize[0] + ' x ' + cs.fullscreenSize[1], '']);
    for (const o of named) {
      if (!o || !o.name) continue;
      cells.push([rndWords(o.name), o.label || String(o.value), 'option ' + o.id + (o.label ? ', value ' + o.value : '') + (o.min != null && o.max != null ? ', range ' + o.min + '..' + o.max : '')]);
    }
    const sig = JSON.stringify(cells);
    if (sig === rndOptSig) return;
    rndOptSig = sig;
    box.innerHTML = '';
    const h = document.createElement('div'); h.className = 'stor-h'; h.textContent = 'Client options'; box.appendChild(h);
    const g = document.createElement('div'); g.className = 'cs-grid';
    for (const c of cells) {
      const cell = document.createElement('div'); cell.className = 'cs-cell'; if (c[2]) cell.title = c[2];
      const k = document.createElement('span'); k.className = 'k'; k.textContent = c[0];
      const v = document.createElement('span'); v.className = 'v'; v.textContent = c[1];
      cell.appendChild(k); cell.appendChild(v); g.appendChild(cell);
    }
    box.appendChild(g); box.style.display = '';
  }
  // Which NPCs and players are drawn. An actor is hidden when it is on its hide list, or when its whole
  // class is hidden and it is not on the keep list. Kept per character, and sent to the launcher again
  // every few seconds so a client that attached late still gets it.
  const HIDE_LISTS = ['npcHide', 'npcKeep', 'plHide', 'plKeep'];
  let hideRules = null, hideFor = '', hidePid = 0, hideSent = '', hideSentAt = 0;
  const HIDE_NUMS = ['maxDist', 'crowdN', 'crowdR'];   // tiles past which a player is hidden; more than N players within R tiles hides them all
  function hideBlank() { return { npcs: false, players: false, npcHide: [], npcKeep: [], plHide: [], plKeep: [], maxDist: 0, crowdN: 0, crowdR: 0, menu: true }; }
  let hideClickSeq = -1, hideClickPid = 0, hideMenuOk = null;   // the last "Hide" row click seen; whether the game client takes rows
  function hideNum(v) { v = parseInt(v, 10); return v > 0 ? Math.min(v, 200) : 0; }
  function hideClean(v) { return String(v == null ? '' : v).replace(/[\u00a0_]/g, ' ').replace(/[^\x20-\x7e]/g, '').replace(/\s+/g, ' ').trim().slice(0, 40); }
  function hideLoadFor(name) {
    const r = hideBlank();
    try {
      const j = JSON.parse(prefGet('rtxHide:' + name, '') || '{}') || {};
      r.npcs = !!j.npcs; r.players = !!j.players;
      for (const k of HIDE_LISTS) if (Array.isArray(j[k])) r[k] = j[k].map(hideClean).filter(Boolean).slice(0, 200);
      for (const k of HIDE_NUMS) r[k] = hideNum(j[k]);
      r.menu = j.menu !== false;
    } catch (e) {}
    return r;
  }
  function hideSave() { if (hideFor && hideRules) { try { prefSet('rtxHide:' + hideFor, JSON.stringify(hideRules)); } catch (e) {} } }
  function hidePush(force) {
    const b = bridge(), pid = myPid();
    if (!b || !hideRules || !pid) return;
    if (!b.renderRules) {                    // a launcher from before the lists: the two switches alone
      const sw = (hideRules.npcs ? 1 : 0) + ':' + (hideRules.players ? 1 : 0);
      if (b.renderToggle && (force || pid !== hidePid || sw !== hideSent)) {
        try { b.renderToggle(pid, 0, !!hideRules.npcs); b.renderToggle(pid, 1, !!hideRules.players); } catch (e) {}
        hidePid = pid; hideSent = sw;
      }
      return;
    }
    const msg = JSON.stringify(hideRules), now = Date.now();
    if (!force && pid === hidePid && msg === hideSent && now - hideSentAt < 10000) return;
    try {
      b.renderRules(pid, !!hideRules.npcs, !!hideRules.players, hideRules.npcHide.join('\n'), hideRules.npcKeep.join('\n'),
                    hideRules.plHide.join('\n'), hideRules.plKeep.join('\n'), hideRules.maxDist | 0, hideRules.crowdN | 0, hideRules.crowdR | 0);
    } catch (e) {}
    hidePid = pid; hideSent = msg; hideSentAt = now;
    renderHide.npcs = !!hideRules.npcs; renderHide.players = !!hideRules.players;
  }
  function hideRulesTick() {
    const name = (lastSnap && lastSnap.display_name) || '';
    if (!name) return;                       // which character this is, is not known yet
    if (name !== hideFor) { hideFor = name; hideRules = hideLoadFor(name); hideSent = ''; hidePaint(); }
    hidePush(false);
    hideMenuTick();
  }
  // The "Hide" row in the game's right-click menu: asked for on every pass (a client that attached
  // late still gets it), and a click on one puts that NPC or player on its hidden list.
  function hideMenuTick() {
    const b = bridge(), pid = myPid();
    if (!b || !b.menuHideRows || !b.menuHideClick || !hideRules || !pid) return;
    let ok = false;
    try { ok = !!b.menuHideRows(pid, hideRules.menu !== false); } catch (e) {}
    if (ok !== hideMenuOk) { hideMenuOk = ok; const el = document.querySelector('#rh_menu .ov-sub'); if (el) el.textContent = hideMenuSub(); }
    let d = null;
    try { d = JSON.parse(b.menuHideClick(pid) || '{}'); } catch (e) {}
    if (!d || typeof d.seq !== 'number') return;
    if (pid !== hideClickPid) { hideClickPid = pid; hideClickSeq = d.seq; return; }   // clicks from before this page looked are not acted on
    if (d.seq === hideClickSeq) return;
    hideClickSeq = d.seq;
    if (!d.name) return;
    const kind = d.kind === 'player' ? 'player' : 'npc', pre = kind === 'player' ? 'pl' : 'npc';
    if (!d.show) { hideEntity(kind, d.name, false); return; }
    // "Show": off the hidden list. Still hidden after that means a class switch, the distance or the
    // crowd rule has it, and only the shown list gets it past those.
    const listed = hideHas(pre + 'Hide', d.name);
    const ruled = kind === 'player' ? (hideRules.players || hideRules.maxDist > 0 || (hideRules.crowdN > 0 && hideRules.crowdR > 0)) : hideRules.npcs;
    if (listed && !ruled) hideUnlist(pre + 'Hide', d.name);
    else hideEntity(kind, d.name, true);
  }
  function hideMenuSub() { return hideMenuOk === false ? 'Not available on this game client' : 'Adds Hide, or Show, to the menu of every NPC and player'; }
  function hideHas(list, name) { const k = name.toLowerCase(); return hideRules[list].some(x => x.toLowerCase() === k); }
  // kind 'npc' or 'player'; keep = onto the keep list. A name goes onto one list of its kind only.
  function hideEntity(kind, name, keep) {
    name = hideClean(name);
    if (!hideRules || !name) return false;
    const to = (kind === 'player' ? 'pl' : 'npc') + (keep ? 'Keep' : 'Hide');
    const other = (kind === 'player' ? 'pl' : 'npc') + (keep ? 'Hide' : 'Keep');
    hideUnlist(other, name, true);
    if (!hideHas(to, name) && hideRules[to].length < 200) hideRules[to].push(name);
    hideSave(); hidePush(true); hidePaint();
    return true;
  }
  function hideUnlist(list, name, quiet) {
    if (!hideRules) return;
    const k = name.toLowerCase();
    hideRules[list] = hideRules[list].filter(x => x.toLowerCase() !== k);
    if (!quiet) { hideSave(); hidePush(true); hidePaint(); }
  }
  function hideListCol(list, label, hint) {
    const col = document.createElement('div'); col.className = 'rh-col';
    const h = document.createElement('div'); h.className = 'rh-h'; h.textContent = label;
    const inp = document.createElement('input'); inp.type = 'text'; inp.className = 'al-input'; inp.placeholder = hint; inp.maxLength = 40;
    inp.addEventListener('keydown', (e) => {
      if (e.key !== 'Enter') return;
      const kind = list.indexOf('pl') === 0 ? 'player' : 'npc';
      for (const part of inp.value.split(',')) hideEntity(kind, part, list.indexOf('Keep') > 0);
      inp.value = '';
    });
    const chips = document.createElement('div'); chips.className = 'rh-chips'; chips.id = 'rh_' + list;
    col.appendChild(h); col.appendChild(inp); col.appendChild(chips);
    return col;
  }
  function hideLists(kind, hint) {
    const g = document.createElement('div'); g.className = 'rh-grid';
    g.appendChild(hideListCol(kind + 'Hide', 'Always hidden', hint));
    g.appendChild(hideListCol(kind + 'Keep', 'Always shown', hint));
    return g;
  }
  // "Hide past [n] tiles": words and number boxes in a row. A box names the rule field it edits.
  function hideNumRow(parts) {
    const row = document.createElement('div'); row.className = 'rh-num';
    for (const part of parts) {
      if (HIDE_NUMS.indexOf(part) < 0) { const t = document.createElement('span'); t.textContent = part; row.appendChild(t); continue; }
      const inp = document.createElement('input'); inp.type = 'text'; inp.className = 'al-input'; inp.id = 'rh_' + part; inp.maxLength = 3; inp.placeholder = 'off';
      inp.addEventListener('change', () => {
        hideRulesTick();
        if (!hideRules) return;
        hideRules[part] = hideNum(inp.value);
        inp.value = hideRules[part] ? String(hideRules[part]) : '';
        hideSave(); hidePush(true);
      });
      row.appendChild(inp);
    }
    return row;
  }
  function hidePaint() {
    if (!$('rndWrap') || !hideRules) return;
    const tg = [['rh_npcs', hideRules.npcs], ['rh_players', hideRules.players], ['rh_menu', hideRules.menu !== false]];
    for (const [id, on] of tg) { const el = $(id); if (el) el.classList.toggle('on', !!on); }
    for (const list of HIDE_LISTS) {
      const el = $('rh_' + list); if (!el) continue;
      el.innerHTML = '';
      if (!hideRules[list].length) { const n = document.createElement('span'); n.className = 'rh-none'; n.textContent = 'None'; el.appendChild(n); continue; }
      for (const name of hideRules[list]) {
        const b = document.createElement('button'); b.className = 'pet-chip'; b.textContent = name + '  x'; b.title = 'Remove';
        b.addEventListener('click', () => hideUnlist(list, name));
        el.appendChild(b);
      }
    }
    for (const k of HIDE_NUMS) {
      const el = $('rh_' + k);
      if (el && document.activeElement !== el) el.value = hideRules[k] ? String(hideRules[k]) : '';
    }
  }
  window.hideRulesTick = hideRulesTick;
  window.hideEntity = hideEntity;

  function renderRendering() {
    const c = $('content');
    if ($('rndWrap')) return;          // built once; toggle state lives in renderHide
    c.innerHTML = '';
    const wrap = document.createElement('div'); wrap.id = 'rndWrap'; wrap.className = 'ov-wrap';
    wrap.appendChild(ovToggleRow('rh_npcs', 'Hide NPCs', 'Stop drawing NPC models'));
    wrap.appendChild(hideLists('npc', 'Name or id, Enter'));
    wrap.appendChild(ovToggleRow('rh_players', 'Hide other players', 'Stop drawing other player models (keeps you)'));
    wrap.appendChild(hideLists('pl', 'Name, Enter'));
    {
      const g = document.createElement('div'); g.className = 'rh-grid';
      const far = document.createElement('div'); far.className = 'rh-col';
      const fh = document.createElement('div'); fh.className = 'rh-h'; fh.textContent = 'By distance';
      far.appendChild(fh); far.appendChild(hideNumRow(['Hide past', 'maxDist', 'tiles']));
      const crowd = document.createElement('div'); crowd.className = 'rh-col';
      const ch = document.createElement('div'); ch.className = 'rh-h'; ch.textContent = 'In a crowd';
      crowd.appendChild(ch); crowd.appendChild(hideNumRow(['Hide all when over', 'crowdN', 'within', 'crowdR', 'tiles']));
      g.appendChild(far); g.appendChild(crowd);
      wrap.appendChild(g);
    }
    if (bridge() && bridge().menuHideRows) {
      const row = ovToggleRow('rh_menu', 'Right-click Hide', hideMenuSub());
      row.addEventListener('click', () => {
        hideRulesTick();
        if (!hideRules) return;
        hideRules.menu = hideRules.menu === false;
        row.classList.toggle('on', hideRules.menu);
        hideSave(); hideMenuTick();
      });
      wrap.appendChild(row);
    }
    wrap.appendChild(ovToggleRow('rh_all', 'Hide everything', 'Blank the whole 3D scene (legacy Insert key)'));
    const hint = document.createElement('div'); hint.className = 'ov-hint';
    hint.textContent = 'Only changes what this client draws. Saved per character. "Always shown" wins over every rule.';
    wrap.appendChild(hint);
    const opts = document.createElement('div'); opts.id = 'rndOpts'; opts.className = 'stor-box'; opts.style.cssText = 'display:none;margin:4px 8px 8px';
    wrap.appendChild(opts);
    const gpuTitle = document.createElement('div'); gpuTitle.className = 'section-title'; gpuTitle.textContent = 'GPU frame (Vulkan)';
    const gpu = document.createElement('div'); gpu.id = 'rndGpu'; gpu.className = 'ov-hint'; gpu.textContent = 'No timing data. The Vulkan client publishes per-pass GPU times once its companion is armed.';
    wrap.appendChild(gpuTitle); wrap.appendChild(gpu);
    c.appendChild(wrap);
    const tick = async () => {
      const el = $('rndGpu');
      if (!el || !document.body.contains(el)) { clearInterval(timer); return; }
      try { if (bridge() && bridge().clientState) rndPaintOptions(await rtxData.call('state.clientState')); } catch (e) {}
      let d = null;
      try { if (bridge() && bridge().gpuTiming) d = JSON.parse(await Promise.resolve(bridge().gpuTiming(myPid())) || '{}'); } catch (e) {}
      if (!d || !Array.isArray(d.passes) || !d.passes.length) return;
      const rows = d.passes.map((p, i) => '<div style="display:flex;gap:8px;font:11px var(--font-mono)"><span style="flex:none;width:22px;color:var(--text-mute)">' + i + '</span>'
        + '<span style="flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap">' + String(p.desc).replace(/</g, '&lt;') + '</span>'
        + '<span style="flex:none;width:52px;text-align:right;color:var(--text-mute)">' + p.draws + '</span>'
        + '<span style="flex:none;width:64px;text-align:right">' + (p.us / 1000).toFixed(2) + ' ms</span></div>');
      el.innerHTML = '<div style="margin-bottom:4px">Frame ' + d.frame + ': GPU ' + (d.total_us / 1000).toFixed(2) + ' ms across ' + d.passes.length + ' passes, present interval ' + (d.frame_us / 1000).toFixed(1) + ' ms</div>' + rows.join('');
    };
    const timer = setInterval(tick, 1000); tick();
    [['rh_npcs', 'npcs', 0], ['rh_players', 'players', 1], ['rh_all', 'all', 2]].forEach(([id, key, which]) => {
      const el = $(id); if (!el) return;
      el.classList.toggle('on', !!renderHide[key]);
      el.addEventListener('click', () => {
        if (which < 2) {
          hideRulesTick();
          if (!hideRules) return;            // no character yet: nothing to save it under
          hideRules[key] = !hideRules[key];
          el.classList.toggle('on', hideRules[key]);
          hideSave(); hidePush(true);
          return;
        }
        renderHide[key] = !renderHide[key];
        el.classList.toggle('on', renderHide[key]);
        try { if (bridge() && bridge().renderToggle) rtxData.sync('act.renderToggle', which, renderHide[key]); } catch (e) {}
      });
    });
    hideRulesTick(); hidePaint();
  }

registerTab({ id: 'rendering', render: renderRendering });
})();
