// RuneToolsX panel: Sounds (cache audio browser + player).
// Spliced inline into client.html; IIFE (window exports + registerTab; see the RTX registry in client.html).
//
// js5-14 holds sound EFFECTS and js5-40 holds MUSIC, both as JAGA containers wrapping ordinary
// Ogg Vorbis. The ARCHIVE ID IS THE SOUND ID - the same id CS2 passes to SOUND_SYNTH /
// SOUND_VORBIS_VOLUME - so anything here cross-references with the CS2 panel.
//
// The companion hooks the engine's shared play function, so "Heard in game" reports ids from
// this same space: a listed row can be muted, and anything heard can be played straight back.
//
// Playback is in-process (Audio.cpp: stb_vorbis decode + waveOut). Windows ships no Vorbis codec
// and the renderer has no media element, so both halves live in the client. Decode and mixing
// run on a worker; nothing here blocks the game.
(function () {

  const SND_IDX = { sfx: 14, music: 40 };
  // Track names come from enum 1345 (1347 is the lowercase copy the game's own search uses).
  // Unlock state is clientscript-837, named `music_getvar`: bit (index & 31) of the varp for
  // block (index / 32), in THIS order - 35 consecutive varps then a scattered tail as the list
  // outgrew its original range. 50 blocks x 32 = 1600 slots.
  const MUSIC_NAME_ENUM = 1345;
  const MUSIC_VARPS = [37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,
                       61,62,63,64,65,66,67,68,69,70,71,3551,3691,4263,5019,5868,6221,6448,6920,
                       7035,7923,8282,8661,9681,10620,11995];
  let musicNames = null;         // {index: name}
  let musicUnlocked = null;      // Set of unlocked indexes
  let musicFetchAt = 0;
  let sndKind = 'sfx';
  let sndRows = [];              // [{id,rate,ch,ms,bytes}, ...] for the page on screen
  let sndStart = 0;              // paging cursor (archive id to read from)
  // Each row costs a read + inflate of that archive, so keep a page small enough that listing
  // never feels like a stall.
  let sndPageSize = 60;
  let sndBusy = false, sndSig = '', sndNote = '';
  // Cache effects are mastered loud, hence the 70 default. soundVolume is a setter only, so
  // nothing remembered a change: the slider snapped back to 70 on every reload AND the
  // companion was not re-told until the slider was touched again.
  sndVol = 70;
  // localStorage directly, NOT prefGet: panel files are spliced before client.html's script,
  // so prefGet does not exist yet and calling it would throw out of this whole file.
  // sndApplyDurablePrefs re-applies the durable value once prefsInit has loaded it.
  try { const v = parseInt(localStorage.getItem('rtxSoundVol'), 10); if (v >= 0 && v <= 100) sndVol = v; } catch (e) {}
  sndNow = -1;               // id currently loaded in the player
  let sndSt = { state: 'idle', pos: 0, dur: 0 };
  let sndFilter = '';
  let sndSeeking = false;        // suppress status-driven slider updates mid-drag
  const sndPageStack = [];
  let sndSort = 'id';            // 'id' | 'dur'
  // In-game sound filter (companion channel, separate from the launcher-side player above).
  // sndMuted holds ids the GAME should not play; sndLive is what the game was observed playing.
  // Mute keys pack the js5 index with the id exactly as the companion does, so an effect and a
  // music track that share a number stay distinct.
  const sndKey = function (idx, id) { return (idx * 0x1000000) + (id & 0xFFFFFF); };
  let sndMuted = new Set();
  sndLive = new Map();       // key -> {id, idx, ms, hits, heardAt}; keyed so a repeat never re-adds
  let sndLiveSig = '';           // last painted chip set, so the strip only repaints when it changes
  let sndLiveSeq = 0;            // highest companion sequence merged into sndLive
  const SND_HOT_MS = 1200;       // a chip pulses this long after its sound fires (wall clock)
  let sndFx = {};                // last soundFilterStatus payload
  let sndFxOn = false;           // observation currently requested of the companion
  let sndScanRows = null;        // results of a whole-index scan, when one has been run
  let sndScanning = false, sndScanAt = 0, sndScanStop = false;

  function sndFmtTime(ms) {
    if (!ms || ms < 0) return '0:00';
    const s = Math.floor(ms / 1000);
    return Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0');
  }
  function sndFmtMs(ms) {
    if (!ms) return '-';
    return ms < 1000 ? ms + ' ms'
         : (ms / 1000 < 10 ? (ms / 1000).toFixed(1) : Math.round(ms / 1000)) + ' s';
  }
  function sndFmtBytes(b) {
    return b >= 1048576 ? (b / 1048576).toFixed(1) + ' MB'
         : b >= 1024 ? Math.round(b / 1024) + ' KB' : b + ' B';
  }
  // The filter box takes either an ID substring or a DURATION expression. Durations are what
  // you actually hunt by ("the long ones"), and an id search cannot express that.
  //   >5s   <500ms   >=2s   <=1.5s   2s-10s   0.5-2s
  // A bare number stays an id search, so the old behaviour is untouched.
  function sndParseDur(q) {
    if (!q) return null;
    const unit = function (txt, val) { return /ms$/.test(txt) ? val : val * 1000; };
    let m = q.match(/^(>=|<=|>|<)\s*([0-9]*\.?[0-9]+)\s*(ms|s)?$/);
    if (m) {
      if (!m[3]) return null;                      // ">5" alone is ambiguous; require a unit
      const v = unit(m[3], parseFloat(m[2]));
      if (m[1] === '>')  return { min: v + 0.001, max: Infinity };
      if (m[1] === '>=') return { min: v, max: Infinity };
      if (m[1] === '<')  return { min: 0, max: v - 0.001 };
      return { min: 0, max: v };
    }
    m = q.match(/^([0-9]*\.?[0-9]+)\s*(ms|s)?\s*-\s*([0-9]*\.?[0-9]+)\s*(ms|s)$/);
    if (m) {
      const hi = unit(m[4], parseFloat(m[3]));
      const lo = unit(m[2] || m[4], parseFloat(m[1]));
      return { min: Math.min(lo, hi), max: Math.max(lo, hi) };
    }
    return null;
  }
  function sndMatch(r) {
    if (!sndFilter) return true;
    const d = sndParseDur(sndFilter);
    if (d) return (r.ms | 0) >= d.min && (r.ms | 0) <= d.max;
    return String(r.id).indexOf(sndFilter.replace(/[^0-9]/g, '')) >= 0;
  }
  function sndVisible() {
    const rows = (sndScanRows || sndRows).filter(sndMatch);
    if (sndSort === 'dur') rows.sort(function (a, b) { return (b.ms | 0) - (a.ms | 0); });
    else rows.sort(function (a, b) { return a.id - b.id; });
    return rows;
  }

  // The named music list. It is NOT joined to the audio archives: no enum, struct or interface
  // maps a track index to a js5-40 archive (checked), and the in-game player's own Play handler
  // is a stub, so the client probably never resolves that itself. Names + ownership are real;
  // playing a named track is not possible yet, and the UI says so rather than implying it.
  async function musicFetch(force) {
    if (!bridge() || !bridge().enumInfo) return;
    const now = Date.now();
    if (!force && musicNames && now - musicFetchAt < 5000) return;
    musicFetchAt = now;
    if (!musicNames) {
      try { musicNames = JSON.parse(await rtxData.raw('cache.enumInfo', MUSIC_NAME_ENUM) || '{}') || {}; }
      catch (e) { musicNames = {}; }
    }
    try {
      const d = JSON.parse(await rtxData.raw('state.varps', MUSIC_VARPS.join(',')) || '{}') || {};
      const set = new Set();
      for (let b = 0; b < MUSIC_VARPS.length; b++) {
        const v = (d[String(MUSIC_VARPS[b])] || 0) >>> 0;
        if (!v) continue;
        for (let bit = 0; bit < 32; bit++) if ((v >>> bit) & 1) set.add(b * 32 + bit);
      }
      musicUnlocked = set;
    } catch (e) {}
    sndSig = '';
    paneRun('sounds', renderSounds);
  }

  async function sndFetch(reset) {
    if (!bridge() || !bridge().soundList || sndBusy) return;
    sndBusy = true; sndSig = ''; renderSounds();
    if (reset) { sndStart = 0; sndPageStack.length = 0; }
    try {
      sndRows = JSON.parse(await rtxData.raw('host.soundList', SND_IDX[sndKind], sndStart, sndPageSize) || '[]') || [];
    } catch (e) { sndRows = []; }
    sndBusy = false; sndSig = '';
    paneRun('sounds', renderSounds);
  }

  // Filtering the 60 rows on screen cannot answer "which effects are long?" - there are 7427.
  // This walks the whole index a page at a time, keeping every row, so the filter and sort then
  // apply across all of it. Paged deliberately: each page is one bridge call, so the panel stays
  // responsive and it can be stopped part-way.
  async function sndScanAll() {
    if (!bridge() || !bridge().soundList || sndScanning) return;
    sndScanning = true; sndScanStop = false; sndScanRows = [];
    let from = 0;
    const PAGE = 200;
    while (!sndScanStop) {
      let page = [];
      try {
        page = JSON.parse(await rtxData.raw('host.soundList', SND_IDX[sndKind], from, PAGE) || '[]') || [];
      } catch (e) { break; }
      if (!page.length) break;
      sndScanRows = sndScanRows.concat(page);
      from = page[page.length - 1].id + 1;
      sndScanAt = sndScanRows.length;
      sndSig = ''; renderSounds();
      await new Promise(function (r) { setTimeout(r, 0); });   // yield between pages
    }
    sndScanning = false;
    sndSig = ''; renderSounds();
  }

  async function sndPlay(id) {
    if (!bridge() || !bridge().soundPlay) return;
    sndNow = id; sndNote = ''; sndSig = ''; renderSounds();
    try { await rtxData.raw('act.soundPlay', SND_IDX[sndKind], id, sndVol); }
    catch (e) { sndNote = 'Sound ' + id + ' did not decode'; }
  }
  async function sndToggle() {
    if (!bridge()) return;
    try {
      if (sndSt.state === 'playing') await rtxData.raw('act.soundPause');
      else if (sndSt.state === 'paused') await rtxData.raw('act.soundResume');
      else if (sndNow >= 0) await sndPlay(sndNow);
    } catch (e) {}
  }
  async function sndExport(id) {
    if (!bridge() || !bridge().soundExport) return;
    let p = '';
    try { p = await rtxData.raw('act.soundExport', SND_IDX[sndKind], id); } catch (e) {}
    sndNote = p ? ('Saved ' + p) : ('Could not export ' + id);
    sndSig = ''; renderSounds();
  }

  // The transport polls its own state, so the bar tracks playback without re-rendering the list.
  // The mute list is hand-collected id by id, so it is the one setting in this panel that
  // would genuinely hurt to lose. It goes through prefGet/prefSet (prefs.json on disk), not
  // straight to localStorage, which is wiped on every app update.
  function sndLoadMuted() {
    try {
      const raw = prefGet('rtxSoundMuted', null);
      if (raw) sndMuted = new Set(JSON.parse(raw).map(Number).filter(function (v) { return v > 0; }));
    } catch (e) {}
  }
  // Re-read once the durable store has loaded (it is async, and this panel reads at open).
  // Only ADDS: a mute made in the meantime is not thrown away by the disk copy arriving.
  function sndApplyDurablePrefs() {
    try {
      const raw = prefGet('rtxSoundMuted', null);
      if (!raw) return;
      let changed = false;
      for (const v of JSON.parse(raw).map(Number)) if (v > 0 && !sndMuted.has(v)) { sndMuted.add(v); changed = true; }
      if (!changed) return;
      try { rtxData.sync('act.soundMute', Array.from(sndMuted).join(',')); } catch (e) {}
      sndSig = ''; paneRun('sounds', renderSounds);
    } catch (e) {}
    try { const v = parseInt(prefGet('rtxSoundVol', ''), 10); if (v >= 0 && v <= 100) { sndVol = v; rtxData.sync('act.soundVolume', sndVol); } } catch (e) {}
  }

  // Push the list to the companion AND persist it. Always both: the companion's copy dies with
  // the client process, so the panel is the durable owner of the list.
  function sndPushMuted() {
    prefSet('rtxSoundMuted', JSON.stringify(Array.from(sndMuted)));
    try { rtxData.sync('act.soundMute', Array.from(sndMuted).join(',')); } catch (e) {}
  }

  function sndToggleMute(key) {
    if (sndMuted.has(key)) sndMuted.delete(key); else sndMuted.add(key);
    sndPushMuted();
    renderLiveStrip();
  }

  async function sndTick() {
    if (!bridge()) return;
    // Observation runs on the game's audio thread, so it follows the tab: on while the panel is
    // open, off the moment it is not. Re-push the mute list on every enable - the companion's
    // copy lives in the client process and is gone if that process restarted.
    const want = paneVisible('sounds');
    if (want !== sndFxOn && bridge().soundFilterEnable) {
      sndFxOn = want;
      try { await rtxData.raw('act.soundFilterEnable', want); } catch (e) {}
      if (want) sndPushMuted();
    }
    if (!want || !bridge().soundStatus) return;
    try { sndSt = JSON.parse(await rtxData.raw('host.soundStatus') || '{}') || sndSt; } catch (e) { return; }
    if (bridge().soundFilterStatus) {
      try {
        const fx = JSON.parse(await rtxData.raw('host.soundFilterStatus') || '{}') || {};
        sndFx = fx;             // assign even when absent, so the strip reports a client that went away
        if (fx.ok) {
          // `n` is the companion's absolute observation count, so a gap here means the game
          // played more than the ring holds between two polls - the ids are lost, not silently
          // renumbered.
          const fresh = (fx.recent || []).filter(function (e2) { return e2.n >= sndLiveSeq; });
          if (fresh.length) {
            sndLiveSeq = fresh[fresh.length - 1].n + 1;
            const nowWall = Date.now();
            for (const ev of fresh) {
              const k = sndKey(ev.idx, ev.id);
              const prev = sndLive.get(k);
              if (prev) { prev.ms = ev.ms; prev.hits++; prev.heardAt = nowWall; }
              else sndLive.set(k, { id: ev.id, idx: ev.idx, ms: ev.ms, hits: 1, heardAt: nowWall });
            }
            // Cap by dropping the least recently heard, so a long session cannot grow the strip
            // without bound. Note this does NOT dirty sndSig: the row list must not repaint
            // just because a sound played.
            if (sndLive.size > 24) {
              const byAge = Array.from(sndLive.values()).sort(function (a, b) { return a.ms - b.ms; });
              for (let i = 0; i < byAge.length - 24; i++) sndLive.delete(sndKey(byAge[i].idx, byAge[i].id));
            }
          }
        }
      } catch (e) {}
    }
    const bar = $('sndSeek'), cur = $('sndCur'), dur = $('sndDur'), pp = $('sndPlayPause');
    if (bar && !sndSeeking) {
      bar.max = String(Math.max(1, sndSt.dur | 0));
      bar.value = String(sndSt.pos | 0);
    }
    if (cur) cur.textContent = sndFmtTime(sndSt.pos);
    if (dur) dur.textContent = sndFmtTime(sndSt.dur);
    if (pp) pp.textContent = sndSt.state === 'playing' ? 'Pause'
                           : sndSt.state === 'loading' ? '…' : 'Play';
    const nw = $('sndNow');
    if (nw) nw.textContent = sndNow >= 0
      ? ((sndKind === 'music' ? 'Music ' : 'Effect ') + sndNow
         + (sndSt.state === 'loading' ? ' (decoding)' : ''))
      : 'Nothing loaded';
    renderLiveStrip();
  }

  // "Heard in game" strip. Owns its own element and its own repaint decision - it must never
  // go through renderSounds(), which rebuilds all 60 rows and would destroy the button under the
  // cursor several times a second.
  //
  // Chips are ordered by ID, not by recency: a recency order re-shuffles on every observation,
  // which is what made these impossible to click. Position is therefore stable, and a recurring
  // sound only bumps its hit count.
  function renderLiveStrip() {
    const live = $('sndLive');
    if (!live) return;
    if (!sndFx.ok) {
      if (sndLiveSig !== 'none') { sndLiveSig = 'none'; live.textContent = 'In-game filter: companion not loaded in this client'; }
      return;
    }
    if (!sndFx.hooked) {
      if (sndLiveSig !== 'nohook') { sndLiveSig = 'nohook'; live.textContent = 'In-game sound: play function not found in this game build'; }
      return;
    }
    const keys = Array.from(sndLive.keys()).sort(function (a, b) { return a - b; });
    const d = sndFx.diag || [];
    // DOM REBUILD only when the chip SET or the mute set changes - rebuilding mid-click
    // destroys the button under the cursor. Everything that moves faster than that
    // (hit counts, the just-fired pulse, the played counter) is patched IN PLACE on
    // every tick below, so the strip reads as live without ever re-shuffling.
    const sig = keys.join(',') + '|' + Array.from(sndMuted).sort().join(',');
    if (sig !== sndLiveSig) {
      sndLiveSig = sig;
      let h = '<div style="opacity:0.75;margin-bottom:4px">Heard in game'
            + '<span class="snd-live-dot" title="observing the game\'s audio in real time"></span>'
            + (sndMuted.size ? '  ·  ' + sndMuted.size + ' muted' : '')
            + '<span id="sndLiveCounts"></span>'
            + '</div>';
      if (!keys.length) {
        h += '<div style="opacity:0.55">Nothing heard yet - sounds appear the moment the game plays them</div>';
      } else {
        h += '<div style="display:flex;flex-wrap:wrap;gap:4px;align-items:center">';
        for (const k of keys) {
          const e = sndLive.get(k);
          const m = sndMuted.has(k);
          const tag = (e.idx === SND_IDX.music ? 'M' : 'E') + e.id;
          h += '<button class="pet-chip snd-heard" data-liveplay="' + e.idx + ':' + e.id + '" data-tag="' + tag + '">'
             + tag + (e.hits > 1 ? ' &times;' + e.hits : '') + ' &#9654;</button>'
             + '<button class="pet-chip' + (m ? ' on' : '') + '" data-live="' + k + '">'
             + (m ? 'Unmute' : 'Mute') + '</button>';
        }
        h += '</div>';
      }
      live.innerHTML = h;
    }
    // In-place patch pass, every tick: counts and the "just fired" pulse. Touching
    // textContent/classList never destroys an element, so clicks stay safe.
    const cts = $('sndLiveCounts');
    if (cts && d.length >= 2) cts.textContent = '  ·  ' + d[0] + ' played, ' + d[1] + ' silenced';
    const nowWall = Date.now();
    live.querySelectorAll('.snd-heard').forEach(function (btn) {
      const pp2 = (btn.getAttribute('data-liveplay') || ':').split(':');
      const e = sndLive.get(sndKey(Number(pp2[0]), Number(pp2[1])));
      if (!e) return;
      const want = btn.getAttribute('data-tag') + (e.hits > 1 ? ' ×' + e.hits : '') + ' ▶';
      if (btn.textContent !== want) btn.textContent = want;
      btn.classList.toggle('snd-hot', nowWall - (e.heardAt || 0) < SND_HOT_MS);
    });
  }

  function renderSounds() {
    const c = paneRoot('sounds'); if (!c) return;
    let wrap = $('sndWrap');
    if (!wrap) {
      c.innerHTML = '';
      // Live-strip styling: a breathing "observing" dot and a glow pulse on the chip
      // whose sound just fired, so the strip visibly reacts the moment the game plays.
      if (!document.getElementById('sndFxCss')) {
        const st = document.createElement('style'); st.id = 'sndFxCss';
        st.textContent =
            '.snd-live-dot{display:inline-block;width:7px;height:7px;border-radius:50%;'
          +   'background:var(--ok,#4dd28a);margin-left:8px;vertical-align:1px;'
          +   'animation:sndLivePulse 2s ease-in-out infinite}'
          + '@keyframes sndLivePulse{0%,100%{opacity:.4}50%{opacity:1}}'
          + '.pet-chip.snd-heard{transition:box-shadow 250ms,border-color 250ms,color 250ms}'
          + '.pet-chip.snd-hot{border-color:var(--accent-hi);color:var(--accent-hi);'
          +   'box-shadow:0 0 9px rgba(var(--accent-rgb),.5)}';
        document.head.appendChild(st);
      }
      wrap = document.createElement('div'); wrap.id = 'sndWrap'; wrap.className = 'pk-wrap';
      c.appendChild(wrap);
      wrap.innerHTML =
          '<div class="pet-toolbar">'
        +   '<div class="pet-chips">'
        +     '<button class="pet-chip" data-kind="sfx">Sound effects</button>'
        +     '<button class="pet-chip" data-kind="music">Music</button>'
        +     '<button class="pet-chip" data-kind="tracks">Track list</button>'
        +   '</div>'
        +   '<input id="sndFind" class="pet-search" placeholder="Filter: id, or a duration like &gt;5s, &lt;500ms, 2s-10s" autocomplete="off">'
        +   '<button class="pet-chip" id="sndSortBtn">Sort: id</button>'
        +   '<button class="pet-chip" id="sndScanBtn">Search all</button>'
        + '</div>'
        + '<div class="pet-count" style="display:flex;align-items:center;gap:8px;flex-wrap:wrap">'
        +   '<button class="pet-chip" id="sndPlayPause">Play</button>'
        +   '<button class="pet-chip" id="sndStopBtn">Stop</button>'
        +   '<span id="sndCur" style="font-variant-numeric:tabular-nums;opacity:0.7">0:00</span>'
        +   '<input id="sndSeek" type="range" min="0" max="1" value="0" style="flex:1;min-width:120px">'
        +   '<span id="sndDur" style="font-variant-numeric:tabular-nums;opacity:0.7">0:00</span>'
        +   '<span style="opacity:0.55">Vol</span>'
        +   '<input id="sndVolIn" type="range" min="0" max="100" value="' + sndVol + '" style="width:90px">'
        + '</div>'
        + '<div id="sndNow" class="pet-count" style="opacity:0.7">Nothing loaded</div>'
        + '<div id="sndInfo" class="pet-count"></div>'
        + '<div id="sndLive" class="pet-count"></div>'
        + '<div id="sndList" class="pet-list"></div>'
        + '<div id="sndNav" class="pet-toolbar"></div>';

      wrap.addEventListener('click', function (e) {
        const k = e.target.closest('button[data-kind]');
        if (k) {
          sndKind = k.dataset.kind; sndNote = ''; sndSig = '';
          sndScanStop = true; sndScanRows = null;      // a scan belongs to one index
          if (sndKind === 'tracks') musicFetch(true); else sndFetch(true);
          return;
        }
        if (e.target.closest('#sndSortBtn')) {
          sndSort = sndSort === 'id' ? 'dur' : 'id'; sndSig = ''; renderSounds(); return;
        }
        if (e.target.closest('#sndScanBtn')) {
          if (sndScanning) { sndScanStop = true; }
          else if (sndScanRows) { sndScanRows = null; sndSig = ''; renderSounds(); }
          else sndScanAll();
          return;
        }
        if (e.target.closest('#sndPlayPause')) { sndToggle(); return; }
        if (e.target.closest('#sndStopBtn')) {
          try { rtxData.sync('act.soundStop'); } catch (e2) {}
          sndNow = -1; sndSig = ''; renderSounds(); return;
        }
        const lm = e.target.closest('button[data-live]');
        if (lm) { sndToggleMute(+lm.dataset.live); return; }
        const lp = e.target.closest('button[data-liveplay]');
        if (lp) {
          // The companion reports js5 archive ids, so the panel can play back exactly what the
          // game just played, out of the same index.
          const parts = lp.dataset.liveplay.split(':');
          sndKind = (+parts[0] === SND_IDX.music) ? 'music' : 'sfx';
          sndPlay(+parts[1]);
          return;
        }
        const mb = e.target.closest('button[data-mute]');
        if (mb) { sndToggleMute(+mb.dataset.mute); return; }
        const p = e.target.closest('button[data-play]');
        if (p) { sndPlay(+p.dataset.play); return; }
        const x = e.target.closest('button[data-export]');
        if (x) { sndExport(+x.dataset.export); return; }
        const n = e.target.closest('button[data-page]');
        if (n) {
          if (n.dataset.page === 'next' && sndRows.length) {
            sndPageStack.push(sndStart); sndStart = sndRows[sndRows.length - 1].id + 1;
          } else if (n.dataset.page === 'prev' && sndPageStack.length) {
            sndStart = sndPageStack.pop();
          } else return;
          sndFetch(false);
        }
      });
      const find = $('sndFind');
      if (find) find.addEventListener('input', function () {
        // Keep the raw text: a duration expression (">5s") must survive, and sndMatch strips to
        // digits itself when it decides the input is an id search.
        sndFilter = find.value.trim().toLowerCase();
        sndSig = ''; renderSounds();
      });
      const seek = $('sndSeek');
      if (seek) {
        seek.addEventListener('mousedown', function () { sndSeeking = true; });
        seek.addEventListener('change', function () {
          sndSeeking = false;
          try { rtxData.sync('act.soundSeek', +seek.value); } catch (e2) {}
        });
      }
      const vin = $('sndVolIn');
      if (vin) vin.addEventListener('input', function () {
        sndVol = +vin.value;
        prefSet('rtxSoundVol', String(sndVol));
        try { rtxData.sync('act.soundVolume', sndVol); } catch (e2) {}
      });
      // Push the restored volume once on open, so the companion matches what the slider shows
      // instead of staying at its own default until the slider is dragged.
      try { rtxData.sync('act.soundVolume', sndVol); } catch (e) {}
      sndLoadMuted();
      sndFetch(true);
    }

    if (sndKind === 'tracks') { renderTrackList(wrap); return; }
    const vis = sndVisible();
    // Sort and scan state belong in the signature: without them, toggling the sort or finishing a
    // scan produces the same row COUNT and the repaint would be skipped.
    const sig = sndKind + '|' + sndStart + '|' + vis.length + '|' + sndBusy + '|' + sndNote
              + '|' + sndFilter + '|' + sndNow + '|' + sndSort
              + '|' + (sndScanRows ? sndScanRows.length : -1) + '|' + (sndScanning ? 1 : 0)
              + '|' + sndMuted.size + '|' + (sndFx.hooked ? 1 : 0);
    if (sig === sndSig) return;
    sndSig = sig;

    Array.prototype.forEach.call(wrap.querySelectorAll('button[data-kind]'), function (b) {
      b.className = 'pet-chip' + (b.dataset.kind === sndKind ? ' on' : '');
    });
    const sortBtn = $('sndSortBtn');
    if (sortBtn) sortBtn.textContent = 'Sort: ' + (sndSort === 'dur' ? 'longest' : 'id');
    const scanBtn = $('sndScanBtn');
    if (scanBtn) scanBtn.textContent = sndScanning ? ('Stop (' + sndScanAt + ')')
                                     : sndScanRows ? 'Clear scan' : 'Search all';

    const info = $('sndInfo');
    if (info) {
      info.textContent = sndBusy ? 'Reading cache…'
        : vis.length ? (vis.length + ' of ' + (sndScanRows || sndRows).length + ' shown'
                        + (sndScanRows ? '  ·  whole index' + (sndScanning ? ' (scanning…)' : '')
                                       : '  ·  ids ' + sndRows[0].id + '–' + sndRows[sndRows.length - 1].id)
                        + (sndNote ? '   ·   ' + sndNote : ''))
        : (sndNote || (sndRows.length ? 'No id matches that filter'
                                      : 'No sounds decoded (is the cache readable?)'));
    }

    const list = $('sndList');
    if (list) {
      let h = '';
      for (const r of vis) {
        // The meta column carries min-width:0 + ellipsis and is the ONLY flexible child:
        // without it the row's floor exceeded a narrow panel and pushed Export (the last
        // child) outside the pane, where overflow-x:hidden made it unclickable. The
        // detail also rides the row title, so nothing is lost when it truncates.
        const meta = r.rate + ' Hz · ' + (r.ch === 2 ? 'stereo' : 'mono')
                   + ' · ' + sndFmtMs(r.ms) + ' · ' + sndFmtBytes(r.bytes);
        h += '<div class="pet-row" title="' + htmlEsc(String(r.id) + ' — ' + meta) + '"'
           + (r.id === sndNow ? ' style="background:rgba(120,180,255,0.10)"' : '') + '>'
           + '<div style="flex:0 0 auto;min-width:44px;font-variant-numeric:tabular-nums">' + r.id + '</div>'
           + '<div class="snd-meta">' + meta + '</div>'
           + '<button class="pet-chip" data-play="' + r.id + '">Play</button>'
           + (sndFx.hooked
                ? '<button class="pet-chip' + (sndMuted.has(sndKey(SND_IDX[sndKind], r.id)) ? ' on' : '')
                  + '" data-mute="' + sndKey(SND_IDX[sndKind], r.id) + '">'
                  + (sndMuted.has(sndKey(SND_IDX[sndKind], r.id)) ? 'Muted' : 'Mute') + '</button>'
                : '')
           + '<button class="pet-chip" data-export="' + r.id + '">Export</button>'
           + '</div>';
      }
      list.innerHTML = h;
    }

    const nav = $('sndNav');
    if (nav && sndScanRows) { nav.innerHTML = ''; return; }
    if (nav) {
      nav.innerHTML = '<button class="pet-chip" data-page="prev"' + (sndPageStack.length ? '' : ' disabled')
                    + '>&#8249; Previous</button><button class="pet-chip" data-page="next"'
                    + (sndRows.length ? '' : ' disabled') + '>Next &#8250;</button>';
    }
  }

  // Named track list: enum 1345 titles with live unlock state from the music_getvar varps.
  // Deliberately has no Play button - a title is not linked to an audio archive (see musicFetch),
  // and offering playback that cannot work would be worse than saying so.
  function renderTrackList(wrap) {
    musicFetch(false);
    const names = musicNames || {};
    const keys = Object.keys(names).map(Number).sort(function (a, b) { return a - b; });
    const q = sndFilter;
    const rows = keys.filter(function (i) {
      const nm = names[i];
      return nm && (!q || nm.toLowerCase().indexOf(q) >= 0);
    });
    const have = musicUnlocked ? rows.filter(function (i) { return musicUnlocked.has(i); }).length : 0;
    const sig = 'tracks|' + rows.length + '|' + have + '|' + q + '|' + (musicUnlocked ? 1 : 0);
    if (sig === sndSig) return;
    sndSig = sig;

    Array.prototype.forEach.call(wrap.querySelectorAll('button[data-kind]'), function (b) {
      b.className = 'pet-chip' + (b.dataset.kind === sndKind ? ' on' : '');
    });

    const info = $('sndInfo');
    if (info) {
      info.textContent = !musicNames ? 'Reading track names…'
        : (musicUnlocked
            ? ('Unlocked ' + have + ' / ' + rows.length + (q ? ' matching' : '')
               + '   ·   titles are not linked to audio archives yet')
            : (rows.length + ' tracks   ·   unlock state unavailable (be in-game)'));
    }
    const nowEl = $('sndNow');
    if (nowEl) nowEl.textContent = 'Track list is informational: names come from the cache, audio '
                                 + 'archives are browsed under Music';

    const list = $('sndList');
    if (list) {
      let h = '';
      for (const i of rows) {
        const on = musicUnlocked ? musicUnlocked.has(i) : null;
        const col = on === null ? 'inherit' : (on ? '#78d98a' : '#e06c6c');
        h += '<div class="pet-row">'
           + '<div style="flex:0 0 64px;opacity:0.5;font-variant-numeric:tabular-nums">' + i + '</div>'
           + '<div style="flex:1;color:' + col + '">' + htmlEsc(names[i]) + '</div>'
           + '<div style="opacity:0.55">' + (on === null ? '' : (on ? 'unlocked' : 'locked')) + '</div>'
           + '</div>';
      }
      list.innerHTML = h;
    }
    const nav = $('sndNav');
    if (nav) nav.innerHTML = '';
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { renderSounds, sndApplyDurablePrefs, sndTick });
registerTab({ id: 'sounds', render: renderSounds });
})();
