// RuneToolsX panel: Combat Log (live, searchable, filterable hitsplat stream).
(function () {

  // Source: bridge().combatLog(pid, since, max), the launcher's own event ring. The launcher polls
  // every actor's hitsplat ring five times a second and hands out each hit once, classified by
  // kind ("melee", "ranged crit", "necromancy", "poison", "heal", "blocked", ...) and ownership
  // (other = the game's "Other Hitsplats" set, a hit between other players and NPCs), with the
  // actor, its life points at the time and a wall-clock timestamp. This panel keeps a session log
  // per character, filters it by free text and category, pauses without losing hits, and copies
  // the visible rows out as tab-separated text.
  const CL_DAMAGE = { melee: 1, 'melee crit': 1, ranged: 1, 'ranged crit': 1, magic: 1, 'magic crit': 1, necromancy: 1,
                      'necromancy crit': 1, conjure: 1, 'conjure crit': 1, typeless: 1, poison: 1, deflect: 1, cannon: 1,
                      'split soul': 1, blight: 1, 'pierced shield': 1, pool: 1, 'shielded boss': 1, 'instant kill': 1,
                      'instant kill (soft)': 1 };
  const CL_ZERO = { blocked: 1, absorbed: 1, hidden: 1 };
  const CL_CATS = [['own', 'Your hits'], ['self', 'Hits on you'], ['other', "Others' hits"], ['zero', 'Zero / blocked'], ['heal', 'Heals / text']];
  const CL_MAX = 5000, CL_ROWS = 400;
  const clLogs = {};   // pid -> { since, events:[], dropped }
  let clFetching = false, clPaused = false, clSearch = '', clSig = '';
  const clFilt = { own: true, self: true, other: false, zero: true, heal: true };

  function clStore() {
    const p = myPid();
    if (!clLogs[p]) clLogs[p] = { since: 0, events: [], dropped: 0, gap: false };
    return clLogs[p];
  }
  function clCategory(ev) {
    const k = ev.kind || 'unknown';
    if (CL_ZERO[k] || (ev.value === 0 && CL_DAMAGE[k])) return 'zero';
    if (!CL_DAMAGE[k]) return 'heal';
    if (ev.type === 'self') return 'self';
    return ev.other ? 'other' : 'own';
  }
  function clActor(ev) {
    if (ev.type === 'self') return 'You';
    const n = ev.name || (ev.type === 'npc' ? ('NPC ' + ev.id) : 'Player');
    return ev.type === 'npc' && ev.id >= 0 ? (n + ' (' + ev.id + ')') : n;
  }
  // What happened, in words: who was hit and whether it was you dealing it, you taking it, or
  // somebody else's fight. The hit record names no source, so "hit on you" never says by whom.
  function clDirection(ev) {
    const cat = clCategory(ev);
    if (ev.type === 'self') return cat === 'heal' ? 'you healed' : cat === 'zero' ? 'blocked on you' : 'hit on you';
    if (cat === 'heal') return 'healed';
    if (ev.other) return cat === 'zero' ? 'missed by others' : 'hit by others';
    return cat === 'zero' ? 'you missed' : 'you hit';
  }
  function clEvent(ev) {
    const d = clDirection(ev);
    if (ev.type === 'self') return d;
    if (d === 'you hit' || d === 'you missed') return d + ' ' + clActor(ev);
    return clActor(ev) + ' ' + d;
  }
  function clFmt(n) { return n >= 1000000 ? (n / 1000000).toFixed(2) + 'M' : n >= 10000 ? (n / 1000).toFixed(1) + 'K' : String(n); }
  function clTime(ms) {
    const d = new Date(ms), p2 = n => (n < 10 ? '0' : '') + n;
    return p2(d.getHours()) + ':' + p2(d.getMinutes()) + ':' + p2(d.getSeconds()) + '.' + String(d.getMilliseconds()).padStart(3, '0');
  }
  function clMatches(ev, q) {
    if (!q) return true;
    if (q.charAt(0) === '#') return String(ev.uid) === q.slice(1);
    return (clEvent(ev) + ' ' + (ev.kind || '') + ' ' + ev.value + ' ' + ev.hitmark + ' ' + ev.type).toLowerCase().indexOf(q) >= 0;
  }
  function clVisible(store) {
    const q = clSearch.trim().toLowerCase(), out = [];
    for (let i = store.events.length - 1; i >= 0 && out.length < CL_ROWS; i--) {
      const ev = store.events[i];
      if (!clFilt[clCategory(ev)] || !clMatches(ev, q)) continue;
      out.push(ev);
    }
    return out;
  }

  // Called from the boot refresh loop (every 250 ms while in world); cheap when nothing is new.
  async function fetchCombatLog() {
    if (clFetching) return;
    const b = bridge(); if (!b || !b.combatLog) return;
    clFetching = true;
    try {
      const store = clStore();
      const j = JSON.parse(await b.combatLog(myPid(), store.since, 2000));
      if (j && Array.isArray(j.events)) {
        store.gap = !!j.gap;
        if (store.since === 0 && j.seq > 0 && !j.events.length) { store.since = j.seq; }
        if (j.events.length) {
          for (const ev of j.events) store.events.push(ev);
          if (store.events.length > CL_MAX) store.events.splice(0, store.events.length - CL_MAX);
          if (!clPaused) clSig = '';
        }
        if (typeof j.seq === 'number' && j.seq > store.since) store.since = j.seq;
      }
    } catch (e) {} finally { clFetching = false; }
    paneRun('combatlog', renderCombatList);
  }

  function renderCombatLog() {
    const c = $('content');
    let wrap = $('clWrap');
    if (!wrap) {
      c.innerHTML = ''; clSig = '';
      wrap = document.createElement('div'); wrap.id = 'clWrap'; wrap.className = 'pk-wrap'; c.appendChild(wrap);
      const tb = document.createElement('div'); tb.className = 'chat-toolbar';
      const search = document.createElement('input'); search.className = 'pet-search'; search.id = 'clSearch';
      search.placeholder = 'Search: name, kind, value, mark id, or #uid'; search.value = clSearch;
      const pause = document.createElement('button'); pause.className = 'pet-chip' + (clPaused ? ' on' : ''); pause.id = 'clPause';
      pause.textContent = clPaused ? 'Resume' : 'Pause'; pause.dataset.tip = 'Freeze the view. Hits keep being recorded.';
      const copy = document.createElement('button'); copy.className = 'pet-chip'; copy.textContent = 'Copy rows';
      copy.dataset.tip = 'Copy the visible rows as tab-separated text (every field, ISO timestamps).';
      const clr = document.createElement('button'); clr.className = 'pet-chip'; clr.textContent = 'Clear';
      clr.dataset.tip = 'Empty the log for this character (does not touch the game).';
      tb.appendChild(search); tb.appendChild(pause); tb.appendChild(copy); tb.appendChild(clr); wrap.appendChild(tb);
      const chips = document.createElement('div'); chips.id = 'clChips'; chips.className = 'pet-chips'; chips.style.marginBottom = '6px'; wrap.appendChild(chips);
      const cnt = document.createElement('div'); cnt.id = 'clCnt'; cnt.className = 'chat-count'; wrap.appendChild(cnt);
      const head = document.createElement('div'); head.className = 'cl-line cl-head';
      head.innerHTML = '<span class="cl-ts">Time</span><span class="cl-actor">Event</span><span class="cl-kind">Kind</span><span class="cl-val">Value</span><span class="cl-lp">Life points</span><span class="cl-mark">Mark</span>';
      wrap.appendChild(head);
      const list = document.createElement('div'); list.id = 'clList'; list.className = 'chat-list'; wrap.appendChild(list);
      search.addEventListener('input', () => { clSearch = search.value; clSig = ''; renderCombatList(); });
      chips.addEventListener('click', e => {
        const b = e.target.closest('.pet-chip'); if (!b) return;
        clFilt[b.dataset.cat] = !clFilt[b.dataset.cat]; clSig = ''; renderCombatList();
      });
      pause.addEventListener('click', () => { clPaused = !clPaused; pause.textContent = clPaused ? 'Resume' : 'Pause'; pause.classList.toggle('on', clPaused); clSig = ''; renderCombatList(); });
      clr.addEventListener('click', () => { const s = clStore(); s.events = []; clSig = ''; renderCombatList(); });
      copy.addEventListener('click', () => {
        const rows = clVisible(clStore()).slice().reverse();
        const lines = ['time\tevent\tactor\ttype\tuid\tid\tkind\tother\tvalue\thitmark\tlp\tlpMax\tx\ty\tplane\tcycle'];
        for (const ev of rows)
          lines.push([new Date(ev.t).toISOString(), clEvent(ev), ev.name || '', ev.type, ev.uid, ev.id, ev.kind || '', ev.other ? 1 : 0, ev.value,
                      ev.hitmark, ev.lp, ev.lpMax, ev.x, ev.y, ev.plane, ev.cycle].join('\t'));
        try { bridge().copyClipboard(lines.join('\n')); } catch (e) {}
        const cnt2 = $('clCnt'); if (cnt2) cnt2.textContent = 'Copied ' + rows.length + ' rows as tab-separated text.';
      });
    }
    renderCombatList();
  }

  function renderCombatList() {
    const list = $('clList'); if (!list) return;
    const store = clStore();
    const q = clSearch.trim().toLowerCase();
    const last = store.events[store.events.length - 1];
    const sig = q + '|' + JSON.stringify(clFilt) + '|' + (clPaused ? 1 : 0) + '|' + store.events.length + '|' + (last ? last.seq : 0);
    if (sig === clSig) return;
    clSig = sig;
    const counts = {}; for (const ev of store.events) { const k = clCategory(ev); counts[k] = (counts[k] || 0) + 1; }
    const chipBar = $('clChips');
    if (chipBar) {
      chipBar.innerHTML = '';
      for (const [cat, label] of CL_CATS) {
        const b = document.createElement('button');
        b.className = 'pet-chip' + (clFilt[cat] ? ' on' : ''); b.dataset.cat = cat;
        b.textContent = label + (counts[cat] ? ' ' + counts[cat] : '');
        chipBar.appendChild(b);
      }
    }
    const shown = clVisible(store);
    const cnt = $('clCnt');
    if (cnt) cnt.textContent = store.events.length + ' hits this session' + (shown.length !== store.events.length ? '  ·  ' + shown.length + ' shown' : '') +
      (clPaused ? '  ·  paused, still recording' : '') + (store.gap ? '  ·  gap: the launcher dropped hits before they were read' : '');
    if (!shown.length) {
      list.innerHTML = '<div class="chat-empty">' + (store.events.length ? 'No hits match.' :
        (bridge() && bridge().combatLog ? 'No hits yet. Every hitsplat drawn near you will appear here.' : 'This launcher build has no combat log.')) + '</div>';
      return;
    }
    const frag = document.createDocumentFragment();
    for (const ev of shown) {
      const cat = clCategory(ev);
      const row = document.createElement('div'); row.className = 'cl-line cl-' + cat;
      const ts = document.createElement('span'); ts.className = 'cl-ts'; ts.textContent = clTime(ev.t);
      const actor = document.createElement('span'); actor.className = 'cl-actor';
      const d = clDirection(ev), dEl = document.createElement('b'); dEl.className = 'cl-dir';
      if (ev.type === 'self') { dEl.textContent = d; actor.appendChild(dEl); }
      else if (d === 'you hit' || d === 'you missed') { dEl.textContent = d + ' '; actor.appendChild(dEl); actor.appendChild(document.createTextNode(clActor(ev))); }
      else { actor.appendChild(document.createTextNode(clActor(ev) + ' ')); dEl.textContent = d; actor.appendChild(dEl); }
      const kind = document.createElement('span'); kind.className = 'cl-kind'; kind.textContent = ev.kind || 'unknown';
      if (ev.other) { const o = document.createElement('span'); o.className = 'cl-oth'; o.textContent = 'other'; kind.appendChild(o); }
      const val = document.createElement('span'); val.className = 'cl-val'; val.textContent = clFmt(ev.value);
      const lp = document.createElement('span'); lp.className = 'cl-lp'; lp.textContent = ev.lpMax > 0 && ev.lp >= 0 ? (clFmt(ev.lp) + ' / ' + clFmt(ev.lpMax)) : '';
      const mark = document.createElement('span'); mark.className = 'cl-mark'; mark.textContent = String(ev.hitmark);
      row.dataset.tip = 'uid ' + ev.uid + '  tile ' + ev.x + ', ' + ev.y + ' plane ' + ev.plane + '  cycle ' + ev.cycle + '  seq ' + ev.seq;
      row.appendChild(ts); row.appendChild(actor); row.appendChild(kind); row.appendChild(val); row.appendChild(lp); row.appendChild(mark);
      frag.appendChild(row);
    }
    list.innerHTML = '';
    list.appendChild(frag);
    if (shown.length >= CL_ROWS) {
      const more = document.createElement('div'); more.className = 'chat-empty';
      more.textContent = 'Only the newest ' + CL_ROWS + ' matching hits are drawn. Search or filter to narrow.';
      list.appendChild(more);
    }
  }

Object.assign(window, { fetchCombatLog });
registerTab({ id: 'combatlog', render: renderCombatLog, open: function () { clSig = ''; fetchCombatLog(); } });
})();
