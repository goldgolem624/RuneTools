// RuneToolsX panel: Corrupted Scarabs (community world tracker).
// Spliced inline into client.html at load; shares its global scope (no IIFE).
// Displays the SSE-fed cache {worlds:[[world,expiresAtMs],..], now:serverNowMs}; detection and reporting live in C++ (Bridge.cpp scarab_scan_pass).

  // Skew is anchored to the payload's ARRIVAL time (rxAt), so a stale payload never inflates the countdown.
  let scSkew = 0;          // serverNow - localNow
  let scRaw = '';
  let scListSig = '';

  function scRead() {
    if (!bridge() || !bridge().scarabCached) return null;
    const parse = (raw) => {
      if (!raw) return null;
      if (raw !== scRaw) {
        scRaw = raw;
        try {
          const p = JSON.parse(raw);
          if (p && typeof p.now === 'number')
            scSkew = p.now - (typeof p.rxAt === 'number' ? p.rxAt : Date.now());
        } catch (e) {}
      }
      try { return JSON.parse(raw); } catch (e) { return null; }
    };
    // The bridge gates the network fallback (SSE alive -> no-op, else one GET per 120s).
    return parse(bridge().scarabCached(1));
  }
  function scNow() { return Date.now() + scSkew; }

  function scActiveWorlds() {
    const d = scRead();
    const now = scNow();
    const rows = (d && Array.isArray(d.worlds) ? d.worlds : [])
      .map(r => ({ world: r[0] | 0, left: r[1] - now }))
      .filter(r => r.world > 0 && r.left > 0)
      .sort((a, b) => b.left - a.left);
    return { rows, d, now };
  }

  function scFmt(ms) {
    const s = Math.max(0, Math.floor(ms / 1000));
    return Math.floor(s / 60) + 'm ' + (s % 60 < 10 ? '0' : '') + (s % 60) + 's';
  }

  // Shared tracker styles; panel_obelisks.js uses them too, so whichever renders first injects.
  function scEnsureCss() {
    injectStyle('scCss', `
          .sc-wrap { margin: 2px 12px 0; }
          .sc-head { display: flex; align-items: center; gap: 8px; color: var(--accent-hi); font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: .05em; padding: 10px 2px 6px; }
          .sc-head span { flex: 1; min-width: 0; }
          .sc-bell { flex: 0 0 auto; display: flex; align-items: center; justify-content: center; width: 22px; height: 22px; border-radius: 6px; background: none; border: 1px solid var(--border); color: var(--text-mute); cursor: pointer; padding: 0; }
          .sc-bell:hover { border-color: var(--border-hi); color: var(--text-dim); }
          .sc-bell.on { color: var(--accent-hi); border-color: var(--accent-lo); }
          .sc-card { background: var(--bg-elev); border: 1px solid var(--border); border-radius: 10px; }
          .sc-row { display: flex; align-items: center; gap: 8px; padding: 9px 12px; }
          .sc-row + .sc-row { border-top: 1px solid var(--border); }
          /* title line + optional dim subtitle; the countdown pill never shrinks */
          .sc-w { flex: 1; min-width: 0; color: var(--text); font-size: 12.5px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
          .sc-sub { color: var(--text-mute); font-size: 10.5px; margin-top: 1px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
          .sc-sub.you { color: var(--ok); }
          .sc-t { flex: 0 0 auto; white-space: nowrap; font-size: 11px; color: var(--text); font-variant-numeric: tabular-nums; background: var(--bg); border: 1px solid var(--border); border-radius: 999px; padding: 2px 9px; }
          .sc-t.low { color: #e2b93d; border-color: rgba(226,185,61,.4); }
          .sc-empty { margin: 10px 2px; color: var(--text-dim); font-size: 12px; }
          .sc-note { margin: 10px 2px 0; color: var(--text-mute); font-size: 10.5px; line-height: 1.5; }
          /* "no longer here" vote: gone-button, then live tally + 1-minute progress bar */
          .sc-vote { padding: 8px 12px 10px; border-top: 1px solid var(--border); }
          .sc-vbtn { background: none; border: 1px solid var(--border); color: var(--text-dim); font-size: 10.5px; border-radius: 6px; padding: 3px 9px; cursor: pointer; }
          .sc-vbtn:hover { border-color: var(--border-hi); color: var(--text); }
          .sc-vbtn.yes.on { color: var(--ok); border-color: rgba(77,210,138,.45); }
          .sc-vbtn.no.on { color: #e2b93d; border-color: rgba(226,185,61,.45); }
          .sc-vrow { display: flex; align-items: center; gap: 8px; }
          .sc-vq { flex: 1; min-width: 0; color: var(--text-dim); font-size: 10.5px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
          .sc-vbar { height: 4px; border-radius: 2px; background: var(--bg); overflow: hidden; margin-top: 7px; }
          .sc-vfill { height: 100%; width: 100%; border-radius: 2px; background: var(--accent-hi); transition: width 500ms linear; }
          .sc-vtal { margin-top: 5px; color: var(--text-mute); font-size: 10px; font-variant-numeric: tabular-nums; }`);
  }

  // Armed per tracker; driven by the background hook in client.html, so it fires panel-closed.
  let scNotify = {};
  try { scNotify = JSON.parse(localStorage.getItem('rtxWeNotify') || '{}') || {}; } catch (e) { scNotify = {}; }
  function scNotifySave() { try { localStorage.setItem('rtxWeNotify', JSON.stringify(scNotify)); } catch (e) {} }
  function scNotifyAny() { for (const k in scNotify) if (scNotify[k]) return true; return false; }

  // Worlds already seen per kind; the first pass only seeds the baseline (no login spam).
  const scNotifySeen = { scarabs: null, obelisks: null };
  function scNotifyCheck(kind, rows, districtName) {
    const key = kind === 'scarabs' ? 'scarab' : 'obelisk';
    const live = new Set(rows.map(r => r.world));
    const prev = scNotifySeen[kind];
    if (prev === null) { scNotifySeen[kind] = live; return; }
    if (scNotify[key]) {
      for (const r of rows) {
        if (prev.has(r.world)) continue;
        const what = kind === 'scarabs' ? 'Corrupted Scarabs' : 'Soul Obelisk';
        const where = (kind === 'obelisks' && districtName) ? ' (' + districtName(r.district) + ')' : '';
        const msg = what + ' active on world ' + r.world + where;
        try { bridge().overlayNotify(myPid(), msg, 10000); } catch (e) {}
        try { if (bridge().notifyWindows) bridge().notifyWindows('RuneToolsX', msg); } catch (e) {}
      }
    }
    scNotifySeen[kind] = live;
  }
  // Called from the 250ms poll, so self-throttled: the caches only change on an SSE push.
  let scNotifyAt = 0;
  function scNotifyPoll() {
    if (!scNotifyAny() || !bridge()) return;
    const t = Date.now(); if (t - scNotifyAt < 2000) return; scNotifyAt = t;
    if (scNotify.scarab) { try { scNotifyCheck('scarabs', scActiveWorlds().rows); } catch (e) {} }
    if (scNotify.obelisk && typeof obActiveWorlds === 'function') {
      try { scNotifyCheck('obelisks', obActiveWorlds().rows, (d) => (OB_DISTRICTS[d] || 'Unknown district')); } catch (e) {}
    }
  }
  function scBell(key) {
    const b = document.createElement('button'); b.className = 'sc-bell' + (scNotify[key] ? ' on' : '');
    b.dataset.tip = 'Notify when a new world is reported';
    b.innerHTML = '<svg viewBox="0 0 24 24" width="12" height="12" fill="none" stroke="currentColor" stroke-width="2"><path d="M18 16v-5a6 6 0 1 0-12 0v5l-1.5 2h15z"/><path d="M10 20a2 2 0 0 0 4 0"/></svg>';
    b.addEventListener('click', () => {
      scNotify[key] = scNotify[key] ? 0 : 1;
      scNotifySave();
      b.classList.toggle('on', !!scNotify[key]);
    });
    return b;
  }

  // Menaphos bounds: top-left 3081,2836 -> bottom-right 3249,2610.
  const MENAPHOS = { x0: 3081, x1: 3249, y0: 2610, y1: 2836 };
  let scPlayer = null;   // {x, y, plane} or null
  const scPos = () => scPlayer;
  function scInMenaphos() {
    const s = scPos();
    return !!s && s.x >= MENAPHOS.x0 && s.x <= MENAPHOS.x1 && s.y >= MENAPHOS.y0 && s.y <= MENAPHOS.y1;
  }
  // Chebyshev distance, matching the scene reader's own range semantics.
  function scNear(x, y, r) {
    const s = scPos();
    return !!s && Math.max(Math.abs(s.x - x), Math.abs(s.y - y)) <= r;
  }
  const scMyWorld = () => ((typeof lastSnap !== 'undefined' && lastSnap && lastSnap.world) ? lastSnap.world : 0);

  // The 50-tile detection radius matches the C++ scanner's reach (SceneJson clamps at 64).
  // absentAt = game tick the object was FIRST observed missing (-1 while visible); the 3-tick
  // vote gate stops a scene rebuild (loading screen, plane change) offering a vote on a live one.
  const scSeen = { scarab: false, obelisk: false, at: 0, tick: -1, absentAt: { scarab: -1, obelisk: -1 } };
  const SCARAB_LOCS = [109473, 109475, 109477];
  async function scRefreshSeen() {
    if (!bridge() || !myPid()) return;
    const t = Date.now(); if (t - scSeen.at < 2000) return; scSeen.at = t;
    if (bridge().playerInfo) {
      try {
        const p = JSON.parse(await bridge().playerInfo(myPid()) || 'null');
        scPlayer = (p && p.in && p.x && p.y) ? { x: p.x, y: p.y, plane: p.plane | 0 } : null;
      } catch (e) { scPlayer = null; }
    }
    if (!bridge().sceneEntities) return;
    let sc = null;
    try { sc = JSON.parse(await bridge().sceneEntities(myPid(), 50) || 'null'); } catch (e) { return; }
    if (!sc) return;
    const objs = sc.objects || [], npcs = sc.npcs || [];
    // Either list counts as seeing it: the swarm may be an npc or a runtime loc.
    scSeen.scarab = objs.some(o => SCARAB_LOCS.indexOf(o.id) >= 0) || npcs.some(o => SCARAB_LOCS.indexOf(o.id) >= 0);
    scSeen.obelisk = objs.some(o => o.id === 109495);
    let tick = -1;
    try { if (bridge().gameTick) tick = bridge().gameTick(myPid()) | 0; } catch (e) {}
    scSeen.tick = tick;
    for (const k of ['scarab', 'obelisk']) {
      if (scSeen[k] || tick < 0) scSeen.absentAt[k] = -1;
      else if (scSeen.absentAt[k] < 0 || scSeen.absentAt[k] > tick) scSeen.absentAt[k] = tick;   // first absent read (or tick counter reset: relog)
    }
  }
  function scGoneFor(kind) {
    const k = kind === 'scarabs' ? 'scarab' : 'obelisk';
    if (scSeen[k]) return false;
    if (scSeen.tick < 0) return true;
    return scSeen.absentAt[k] >= 0 && scSeen.tick - scSeen.absentAt[k] >= 3;
  }

  // `areaOk`: anywhere in Menaphos for the roaming scarabs, within 50 tiles of the fixed obelisk.
  // The server cannot verify a position, so this is a convenience gate, not a trust boundary.
  function scCanVote(world, kind, areaOk) {
    return world === scMyWorld() && !!areaOk && scGoneFor(kind);
  }
  function scCanVoteNo(world, kind, areaOk) {
    return world === scMyWorld() && !!areaOk && scSeen[kind === 'scarabs' ? 'scarab' : 'obelisk'];
  }
  const scVoteCast = {};     // "kind:world" -> 'y' | 'n' (this session, for button state)
  const scVoteSentAt = {};   // "kind:world" -> last post time (click guard)
  const SC_YES_TO_REMOVE = 3;   // mirrors world-events.js YES_TO_REMOVE

  // `vote` = [world, yes, no, endsAt] or undefined; `areaOk` = the caller's area test.
  function scVoteBlock(kind, world, vote, now, areaOk) {
    const wrap = document.createElement('div'); wrap.className = 'sc-vote';
    const row = document.createElement('div'); row.className = 'sc-vrow';
    const q = document.createElement('div'); q.className = 'sc-vq';
    const key = kind + ':' + world;
    // One post per 1.5s per row: a rebuild swaps the buttons out, so repeat clicks re-post.
    const send = (yes) => {
      const t = Date.now();
      if (t - (scVoteSentAt[key] || 0) < 1500) return;
      scVoteSentAt[key] = t;
      // myPid() stamps the voter id per account: two accounts on one PC are two observers.
      try { if (bridge().worldEventVote) bridge().worldEventVote(kind, world, yes, myPid()); } catch (e) {}
      scVoteCast[key] = yes ? 'y' : 'n';
    };
    if (!vote) {
      q.textContent = 'No longer here?';
      row.appendChild(q);
      const b = document.createElement('button'); b.className = 'sc-vbtn'; b.textContent = 'Report gone';
      b.addEventListener('click', () => send(true));
      row.appendChild(b);
      wrap.appendChild(row);
      return wrap;
    }
    const canYes = scCanVote(world, kind, areaOk);
    const canNo = scCanVoteNo(world, kind, areaOk);
    q.textContent = (canYes || canNo) ? 'Still here?' : ('Vote from world ' + world);
    row.appendChild(q);
    if (canYes) {
      const yes = document.createElement('button'); yes.className = 'sc-vbtn yes' + (scVoteCast[key] === 'y' ? ' on' : '');
      yes.textContent = 'Gone'; yes.addEventListener('click', () => send(true)); row.appendChild(yes);
    }
    if (canNo) {
      const no = document.createElement('button'); no.className = 'sc-vbtn no' + (scVoteCast[key] === 'n' ? ' on' : '');
      no.textContent = 'Still here'; no.addEventListener('click', () => send(false)); row.appendChild(no);
    }
    wrap.appendChild(row);
    const bar = document.createElement('div'); bar.className = 'sc-vbar';
    const fill = document.createElement('div'); fill.className = 'sc-vfill'; fill.id = 'scvf-' + key;
    bar.appendChild(fill); wrap.appendChild(bar);
    const tal = document.createElement('div'); tal.className = 'sc-vtal'; tal.id = 'scvt-' + key;
    wrap.appendChild(tal);
    scVoteTick(kind, world, vote, now);
    return wrap;
  }
  function scVoteTick(kind, world, vote, now) {
    if (!vote) return;
    const key = kind + ':' + world;
    const left = Math.max(0, vote[3] - now);
    const fill = $('scvf-' + key);
    if (fill) fill.style.width = (left / 600) + '%';        // 60_000ms window -> percent
    const tal = $('scvt-' + key);
    if (tal) {
      // One "still here" ends the vote, so the tally reads as progress toward 3 "gone".
      const txt = vote[1] + ' of ' + SC_YES_TO_REMOVE + ' say gone · ' + Math.ceil(left / 1000) + 's left';
      if (tal.textContent !== txt) tal.textContent = txt;
    }
  }
  const scVoteFor = (d, world) => (d && Array.isArray(d.votes) ? d.votes.find(v => v[0] === world) : null);

  function renderScarabs() {
    const c = $('content');
    let wrap = $('scWrap');
    if (!wrap) {
      scEnsureCss();
      c.innerHTML = '';
      wrap = document.createElement('div'); wrap.id = 'scWrap'; wrap.className = 'pane sc-wrap'; c.appendChild(wrap);
      const h = document.createElement('div'); h.className = 'sc-head';
      const ht = document.createElement('span'); ht.textContent = 'Corrupted Scarabs tracker'; h.appendChild(ht);
      h.appendChild(scBell('scarab'));
      wrap.appendChild(h);
      const card = document.createElement('div'); card.className = 'sc-card'; card.id = 'scCard'; wrap.appendChild(card);
      const empty = document.createElement('div'); empty.className = 'sc-empty'; empty.id = 'scEmpty';
      empty.textContent = 'No active worlds.'; wrap.appendChild(empty);
      scListSig = '';
    }
    scRefreshSeen();                                   // fire-and-forget; gates the vote UI
    const { rows, d, now } = scActiveWorlds();
    const myWorld = scMyWorld();
    const card = $('scCard'), empty = $('scEmpty');
    if (!card || !empty) return;
    // Rebuild on structural change only; the pill and vote bar tick every paint in place.
    const sig = rows.map(r => {
      const v = scVoteFor(d, r.world);
      return r.world + (v ? ':' + v[1] + '/' + v[2] : '')
        + (scCanVote(r.world, 'scarabs', scInMenaphos()) ? '+' : '')
        + (scCanVoteNo(r.world, 'scarabs', scInMenaphos()) ? '-' : '');
    }).join(',') + '|' + myWorld;
    if (sig !== scListSig) {
      scListSig = sig;
      card.innerHTML = '';
      for (const r of rows) {
        const box = document.createElement('div');
        const row = document.createElement('div'); row.className = 'sc-row';
        // "your world" needs its own line: inline, the row ellipsis clips it under the pill.
        const cell = document.createElement('div'); cell.style.cssText = 'flex:1;min-width:0';
        const w = document.createElement('div'); w.className = 'sc-w';
        w.textContent = 'World ' + r.world;
        cell.appendChild(w);
        if (r.world === myWorld) {
          const you = document.createElement('div'); you.className = 'sc-sub you';
          you.textContent = 'your world'; cell.appendChild(you);
        }
        row.appendChild(cell);
        const t = document.createElement('span'); t.className = 'sc-t'; t.id = 'sct-' + r.world;
        row.appendChild(t);
        box.appendChild(row);
        const vote = scVoteFor(d, r.world);
        if (vote || scCanVote(r.world, 'scarabs', scInMenaphos())) box.appendChild(scVoteBlock('scarabs', r.world, vote, now, scInMenaphos()));
        card.appendChild(box);
      }
    }
    card.style.display = rows.length ? '' : 'none';
    empty.style.display = rows.length ? 'none' : '';
    for (const r of rows) {
      const el = $('sct-' + r.world);
      if (el) {
        const txt = scFmt(r.left) + ' left';
        if (el.textContent !== txt) el.textContent = txt;
        el.classList.toggle('low', r.left < 60000);
      }
      scVoteTick('scarabs', r.world, scVoteFor(d, r.world), now);
    }
  }
