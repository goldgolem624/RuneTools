// RuneToolsX panel: Soul Obelisk (community world+district tracker, loc 109495).
// Spliced inline into client.html at load; IIFE (window exports + registerTab; see the RTX registry in client.html).
// Detection + reporting are C++ (Bridge.cpp scarab_scan_pass): the obelisk is matched at
// its known fixed district spawn tiles and reported as world+district; the server keeps
// each world for 7.5 minutes from the FIRST sighting. This panel only displays the list
// ({worlds:[[world,district,expiresAtMs],..], now}), expiry-filtered here (skew-corrected).
(function () {

  // The payload's `now` is a snapshot, so the clock is re-anchored per payload off `rxAt`.
  let obSkew = 0;
  let obRaw = '';
  let obListSig = '';
  const OB_DISTRICTS = { 1: 'Imperial District', 2: 'Worker District', 3: 'Merchant District', 4: 'Port District' };
  // Fixed spawn tiles per district (mirror of Bridge.cpp kObeliskSpawns); the obelisk never
  // moves, so voting requires standing within 50 tiles of ITS spawn.
  const OB_SPAWNS = { 1: [3191, 2709], 2: [3167, 2797], 3: [3217, 2783], 4: [3141, 2644] };
  const obAreaOk = (district) => {
    const sp = OB_SPAWNS[district];
    return sp ? scNear(sp[0], sp[1], 50) : scInMenaphos();
  };

  function obNow() { return Date.now() + obSkew; }
  function obRead() {
    if (!bridge() || !bridge().obeliskCached) return null;
    const parse = (raw) => {
      if (!raw) return null;
      if (raw !== obRaw) {                     // a NEW payload: re-anchor the server clock
        obRaw = raw;
        try {
          const p = JSON.parse(raw);
          if (p && typeof p.now === 'number')
            obSkew = p.now - (typeof p.rxAt === 'number' ? p.rxAt : Date.now());
        } catch (e) {}
      }
      try { return JSON.parse(raw); } catch (e) { return null; }
    };
    // The bridge gates the HTTP fallback itself: no-op while SSE is alive, else one GET per 120s.
    return parse(rtxData.sync('host.obeliskCached', 1));
  }

  function obActiveWorlds() {
    const d = obRead();
    const now = obNow();
    const rows = (d && Array.isArray(d.worlds) ? d.worlds : [])
      .map(r => ({ world: r[0] | 0, district: r[1] | 0, left: r[2] - now }))
      .filter(r => r.world > 0 && r.left > 0)
      .sort((a, b) => b.left - a.left);
    return { rows, d, now };
  }

  function renderObelisks() {
    const c = $('content');
    let wrap = $('obWrap');
    if (!wrap) {
      scEnsureCss();   // shared .sc-* styles from panel_scarabs.js
      c.innerHTML = '';
      wrap = document.createElement('div'); wrap.id = 'obWrap'; wrap.className = 'pane sc-wrap'; c.appendChild(wrap);
      const h = document.createElement('div'); h.className = 'sc-head';
      const ht = document.createElement('span'); ht.textContent = 'Soul Obelisk tracker'; h.appendChild(ht);
      h.appendChild(scBell('obelisk'));
      wrap.appendChild(h);
      const card = document.createElement('div'); card.className = 'sc-card'; card.id = 'obCard'; wrap.appendChild(card);
      const empty = document.createElement('div'); empty.className = 'sc-empty'; empty.id = 'obEmpty';
      empty.textContent = 'No active worlds.'; wrap.appendChild(empty);
      obListSig = '';
    }
    scRefreshSeen();
    const { rows, d, now } = obActiveWorlds();
    const myWorld = scMyWorld();
    const card = $('obCard'), empty = $('obEmpty');
    if (!card || !empty) return;
    const sig = rows.map(r => {
      const v = scVoteFor(d, r.world);
      return r.world + ':' + r.district + (v ? ':' + v[1] + '/' + v[2] : '')
        + (scCanVote(r.world, 'obelisks', obAreaOk(r.district)) ? '+' : '')
        + (scCanVoteNo(r.world, 'obelisks', obAreaOk(r.district)) ? '-' : '');
    }).join(',') + '|' + myWorld;
    if (sig !== obListSig) {
      obListSig = sig;
      card.innerHTML = '';
      for (const r of rows) {
        // Two lines: the panel is ~250px wide, so one line ellipsises the district away.
        const box = document.createElement('div');
        const row = document.createElement('div'); row.className = 'sc-row';
        const cell = document.createElement('div'); cell.style.cssText = 'flex:1;min-width:0';
        const w = document.createElement('div'); w.className = 'sc-w';
        w.textContent = 'World ' + r.world;
        cell.appendChild(w);
        const sub = document.createElement('div'); sub.className = 'sc-sub';
        sub.textContent = OB_DISTRICTS[r.district] || 'Unknown district';
        cell.appendChild(sub);
        if (r.world === myWorld) {
          const you = document.createElement('div'); you.className = 'sc-sub you';
          you.textContent = 'your world'; cell.appendChild(you);
        }
        row.appendChild(cell);
        const t = document.createElement('span'); t.className = 'sc-t'; t.id = 'obt-' + r.world;
        row.appendChild(t);
        box.appendChild(row);
        const vote = scVoteFor(d, r.world);
        if (vote || scCanVote(r.world, 'obelisks', obAreaOk(r.district))) box.appendChild(scVoteBlock('obelisks', r.world, vote, now, obAreaOk(r.district)));
        card.appendChild(box);
      }
    }
    card.style.display = rows.length ? '' : 'none';
    empty.style.display = rows.length ? 'none' : '';
    for (const r of rows) {
      const el = $('obt-' + r.world);
      if (el) {
        const txt = scFmt(r.left) + ' left';
        if (el.textContent !== txt) el.textContent = txt;
        el.classList.toggle('low', r.left < 60000);
      }
      scVoteTick('obelisks', r.world, scVoteFor(d, r.world), now);
    }
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { OB_DISTRICTS, obActiveWorlds, renderObelisks });
registerTab({ id: 'obelisks', render: renderObelisks });
})();
