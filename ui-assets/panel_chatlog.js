// RuneToolsX panel: Chat Log (searchable, colour-rendered chatbox history).
// Spliced inline into client.html; bare classic script sharing one global scope.

  // TWO sources merged into one log:
  //  - packets: op-0x15 message_game from the companion's always-on capture ring; arrives
  //    regardless of which chat tab is open (or whether the chatbox is visible at all),
  //    with a structured channel type id and the sender split out. Deduped by seq.
  //  - interface: the chatbox widget walk (group 137), the pre-packet source; still covers
  //    any channel that rides a different opcode, and only sees what the chatbox renders.
  //    Deduped by raw string (the rotating chatbox pool re-sends lines).
  // A game message lands in BOTH, so each side suppresses a line whose plain text the other
  // side delivered within the last 2 minutes. RS3 encodes literal angle brackets as
  // <lt>/<gt>, so every real "<..>" is an engine tag: whitelist <col>, drop the rest,
  // HTML-escape all text.
  const chatLogs = {};   // pid -> { seen:Set, pseq, pkPlain:Map, ifPlain:Map, lines:[{raw, ts, tokens, plain, chan, src}] }
  let chatFetching = false, chatFetchAt = 0, chatSearch = '', chatSig = '', chatChan = 'All';
  let chatPkHook = false;
  const CHAT_CHANS = ['All', 'Game', 'Public', 'Private', 'Friends', 'Clan', 'Guest', 'Group'];
  function chatStore() {
    const p = myPid();
    if (!chatLogs[p]) chatLogs[p] = { seen: new Set(), pseq: 0, pkPlain: new Map(), ifPlain: new Map(), lines: [] };
    return chatLogs[p];
  }
  // Cross-source dedup ledgers: plain text -> ms shown by that source. Consume-once with a
  // short window: each shown line suppresses exactly ONE copy of the same text from the
  // other source (delivery skew between the sources is ~2s), so a message the player
  // GENUINELY repeats moments later still shows. Bounded by Map insertion order.
  function chatMark(map, plain) { map.set(plain, Date.now()); if (map.size > 600) map.delete(map.keys().next().value); }
  function chatConsume(map, plain) {
    const t = map.get(plain);
    if (t === undefined || Date.now() - t >= 6000) return false;
    map.delete(plain);
    return true;
  }
  // Channel from the wire type id. Verified live: 109 = game/spam, 138 = broadcast news,
  // 96/98/99 = specials. 0/2/3/6 are the long-standing engine message-type ids (game,
  // public, private-from, private-to). Anything unknown falls back on structure: a line
  // that carried a channel name is clan-flavoured, a bare sender is public, else system.
  const CHAT_PKT_TYPES = { 0:'Game', 96:'Game', 98:'Game', 99:'Game', 109:'Game', 138:'Game', 2:'Public', 3:'Private', 6:'Private' };
  function chatClassifyPkt(type, name, chan) {
    if (CHAT_PKT_TYPES[type]) return CHAT_PKT_TYPES[type];
    if (chan) return 'Clan';
    return name ? 'Public' : 'Game';
  }
  function chatFmtTime(ms) {
    const d = new Date(ms), p2 = n => (n < 10 ? '0' : '') + n;
    return p2(d.getHours()) + ':' + p2(d.getMinutes()) + ':' + p2(d.getSeconds());
  }
  // Classify a line by the fixed label the engine writes per channel (abbreviation display mode,
  // RS3's default):
  //   Public: "Name: msg"; Private: "From/To Name:"; Friends: "[FC] Name:";
  //   Clan: "[CC] Name:" ([GIM] for group-ironman); Guest clan: "[GC] Name:";
  //   Group: "[Group] Name:" / "[Group (Team)] Name:"; Game/system: plain text, NO sender.
  // `name` (sender, widget +0x90) separates Public from Game. A custom channel label
  // "[X] Name:" cannot be disambiguated by text and falls to Public.
  function chatClassify(plain, name) {
    if (!name || !name.trim()) return 'Game';        // no sender -> system/game/broadcast
    if (/^(From|To)\b/.test(plain)) return 'Private';
    const lab = plain.match(/^\[([^\]]{1,16})\]\s/);
    if (lab) {
      const t = lab[1];
      if (t === 'FC') return 'Friends';
      if (t === 'CC' || t === 'GIM') return 'Clan';
      if (t === 'GC') return 'Guest';
      if (/^Group/.test(t)) return 'Group';
    }
    return 'Public';
  }
  // RS3 uses exotic whitespace inside names (U+00A0 etc.); normalize it to plain spaces so names
  // both display and search correctly.
  function chatNormSpace(s) { let o = ""; for (let i = 0; i < s.length; i++) { const c = s.charCodeAt(i); o += (c === 0xA0 || c === 0x1680 || (c >= 0x2000 && c <= 0x200B) || c === 0x202F || c === 0x205F || c === 0x3000 || c === 0xFEFF) ? " " : s[i]; } return o; }
  function chatEsc(s) { return htmlEsc(s); }
  // Parse one raw chatbox line -> { ts:"HH:MM:SS"|"", plain:"<text>", tokens:[{text,color}] }.
  // RS3 colour markup is NOT nested: <col=hex> SETS the colour; </col> RESETS to the line's BASE
  // colour (widget +0x80, the per-channel colour; `base` RGB int from the bridge), NOT to white.
  // <lt>/<gt> are literal brackets; <img=N>/<shad>/... are dropped.
  function chatParse(raw, base) {
    let ts = '';
    const baseHex = (typeof base === 'number' && base >= 0) ? '#' + ('000000' + base.toString(16)).slice(-6) : null;
    const tokens = []; let plain = ''; let curColor = baseHex;
    let i = 0; const n = raw.length;
    const emit = (text) => {
      if (!text) return;
      const t = chatNormSpace(text);
      tokens.push({ text: t, color: curColor });
      plain += t;
    };
    while (i < n) {
      const lt = raw.indexOf('<', i);
      if (lt < 0) { emit(raw.slice(i)); break; }
      if (lt > i) emit(raw.slice(i, lt));
      const gt = raw.indexOf('>', lt);
      if (gt < 0) { emit(raw.slice(lt)); break; }
      const tag = raw.slice(lt + 1, gt);
      const low = tag.toLowerCase();
      if (low.startsWith('col=')) {
        const hex = tag.slice(4).trim();
        curColor = /^[0-9a-fA-F]{3}([0-9a-fA-F]{3})?$/.test(hex) ? '#' + hex : baseHex;
      } else if (low === '/col') { curColor = baseHex; }   // reset to the line's base colour, not white
      else if (low === 'lt') emit('<');
      else if (low === 'gt') emit('>');
      else if (low === 'br') emit(' ');
      else if (low.startsWith('img=')) {
        // Chat icon: index into the game's "modicons" sprite strip (ironman badges, leagues,
        // broadcasts). Rendered as an inline image by chatHtml; nothing added to `plain` so
        // search still matches the words.
        const n = parseInt(tag.slice(4), 10);
        if (n >= 0) tokens.push({ img: n, color: curColor });
      }
      // <shad=..>, <str>, <u=..>, etc. -> dropped
      i = gt + 1;
    }
    const m = plain.match(/^\s*\[(\d{1,2}:\d{2}:\d{2})\]\s*/);
    if (m) {
      ts = m[1];
      let drop = m[0].length;
      while (drop > 0 && tokens.length) {
        const t = tokens[0];
        if (t.text.length <= drop) { drop -= t.text.length; tokens.shift(); }
        else { t.text = t.text.slice(drop); drop = 0; }
      }
      plain = plain.slice(m[0].length);
    }
    return { ts, plain: plain.trim(), tokens };
  }
  // <img=N> icons: frames of the "modicons" sprite group, resolved by name once per session
  // and cached per frame as data URLs. Older launchers lack spriteByName: icons stay dropped.
  // Verified against the live cache (sprites index scan, build 949): archive 1455 is the
  // 25-frame 13x11 strip of chat icons (mod crowns, ironman helms, skulls, league trophies).
  // The name lookup is tried first so a renamed cache still resolves; 1455 is the fallback.
  const CHAT_ICONS_SPRITE = 1455;
  let chatIconsId = null, chatIconsResolving = false;
  const chatIconUrl = new Map(), chatIconPending = new Set();
  function chatIconSrc(n) {
    if (chatIconsId === null) {
      if (!chatIconsResolving && bridge() && bridge().spriteByName) {
        chatIconsResolving = true;
        (async () => {
          try { const id = await bridge().spriteByName('modicons'); chatIconsId = (id >= 0) ? id : CHAT_ICONS_SPRITE; } catch (e) { chatIconsId = CHAT_ICONS_SPRITE; }
          chatIconsResolving = false;
          if (chatIconsId >= 0) { try { chatRepaint(); } catch (e) {} }
        })();
      } else if (bridge() && bridge().sprite) chatIconsId = CHAT_ICONS_SPRITE;   // launcher without spriteByName: frames still need the frame arg
      return '';
    }
    if (chatIconsId < 0) return '';
    if (chatIconUrl.has(n)) return chatIconUrl.get(n);
    if (!chatIconPending.has(n)) {
      chatIconPending.add(n);
      (async () => {
        try { const u = await bridge().sprite(chatIconsId, 32, n); if (u) chatIconUrl.set(n, u); } catch (e) {}
        chatIconPending.delete(n);
        if (chatIconUrl.has(n)) { try { chatRepaint(); } catch (e) {} }
      })();
    }
    return '';
  }
  function chatHtml(tokens, query) {
    const q = query ? query.toLowerCase() : '';
    let html = '';
    for (const t of tokens) {
      if (t.img !== undefined) {
        const src = chatIconSrc(t.img);
        if (src) html += '<img class="chat-img" src="' + src + '" alt="">';
        continue;
      }
      let body;
      if (q && t.text.toLowerCase().indexOf(q) >= 0) {
        body = ''; let s = t.text, lc = s.toLowerCase(), pos = 0, idx;
        while ((idx = lc.indexOf(q, pos)) >= 0) {
          body += chatEsc(s.slice(pos, idx)) + '<span class="chat-hit">' + chatEsc(s.slice(idx, idx + q.length)) + '</span>';
          pos = idx + q.length;
        }
        body += chatEsc(s.slice(pos));
      } else {
        body = chatEsc(t.text);
      }
      html += t.color ? '<span style="color:' + t.color + '">' + body + '</span>' : body;
    }
    return html;
  }
  async function fetchChat(force) {
    if (!bridge() || !bridge().chat || chatFetching) return;
    const _t = Date.now(); if (!force && _t - chatFetchAt < 700) return; chatFetchAt = _t;
    chatFetching = true;
    try {
      const j = JSON.parse(await bridge().chat(myPid()));
      const lines = (j && Array.isArray(j.lines)) ? j.lines : [];
      const pkts = (j && Array.isArray(j.packets)) ? j.packets : [];
      chatPkHook = !!(j && j.phook);
      const store = chatStore();
      const fresh = [];
      // First fill only: the packet backlog and the chatbox scrollback overlap far outside
      // the rolling dedup window, so reconcile the whole initial batch against each other.
      const bootPk = (store.pseq === 0 && store.lines.length === 0) ? new Set() : null;
      // Packet lines first (newest-first, deduped by ring seq). The sender is split out on
      // the wire, so rebuild the familiar "Name: msg" plain form -- it is also what makes
      // the cross-source dedup line up with the interface rendering of the same message.
      for (const pk of pkts) {
        if (!pk || !(pk.seq > store.pseq)) continue;
        const p = chatParse(String(pk.raw || ''), null);
        if (!p.plain) continue;
        const name = chatNormSpace(String(pk.name || '').replace(/<[^>]*>/g, '')).trim();
        if (name) {
          p.tokens.unshift({ text: name + ': ', color: null });
          p.plain = name + ': ' + p.plain;
        }
        if (bootPk) bootPk.add(p.plain);
        if (chatConsume(store.ifPlain, p.plain)) continue;  // chatbox walk already delivered it
        chatMark(store.pkPlain, p.plain);
        fresh.push({ raw: 'pk:' + pk.seq, ts: pk.t ? chatFmtTime(pk.t) : '', tokens: p.tokens,
                     plain: p.plain, chan: chatClassifyPkt(pk.type, name, String(pk.chan || '')), src: 'pk' });
      }
      if (pkts.length) for (const pk of pkts) if (pk && pk.seq > store.pseq) store.pseq = pk.seq;
      // Interface lines (newest-first as {raw, base, name}); prepend to stay newest-first.
      for (const ln of lines) {
        const raw = ln && ln.raw; if (!raw || store.seen.has(raw)) continue;
        store.seen.add(raw);
        const p = chatParse(raw, ln.base);
        if (bootPk && bootPk.has(p.plain)) continue;        // first fill: packet backlog wins
        if (chatConsume(store.pkPlain, p.plain)) continue;  // packet capture already delivered it
        chatMark(store.ifPlain, p.plain);
        const name = chatNormSpace(String(ln.name || '').replace(/<[^>]*>/g, '')).trim();
        fresh.push({ raw, ts: p.ts, tokens: p.tokens, plain: p.plain, chan: chatClassify(p.plain, name), src: 'if' });
      }
      if (fresh.length) {
        store.lines = fresh.concat(store.lines);
        if (store.lines.length > 5000) {            // cap session log; drop oldest
          const drop = store.lines.splice(5000);
          for (const d of drop) store.seen.delete(d.raw);
        }
      }
    } catch (e) {} finally { chatFetching = false; }
    paneRun('chatlog', renderChatList);
  }
  function renderChat() {
    const c = $('content');
    let wrap = $('chatWrap');
    if (!wrap) {
      c.innerHTML = ''; chatSig = '';
      wrap = document.createElement('div'); wrap.id = 'chatWrap'; wrap.className = 'pk-wrap'; c.appendChild(wrap);
      const tb = document.createElement('div'); tb.className = 'chat-toolbar';
      const search = document.createElement('input'); search.className = 'pet-search'; search.id = 'chatSearch';
      search.placeholder = 'Search chat...'; search.value = chatSearch;
      const clr = document.createElement('button'); clr.className = 'pet-chip'; clr.textContent = 'Clear log';
      clr.dataset.tip = 'Empties the captured log for this character (does not touch the game).';
      tb.appendChild(search); tb.appendChild(clr); wrap.appendChild(tb);
      const chips = document.createElement('div'); chips.id = 'chatChips'; chips.className = 'pet-chips';
      chips.style.marginBottom = '6px'; wrap.appendChild(chips);
      const cnt = document.createElement('div'); cnt.id = 'chatCnt'; cnt.className = 'chat-count'; wrap.appendChild(cnt);
      const list = document.createElement('div'); list.id = 'chatList'; list.className = 'chat-list'; wrap.appendChild(list);
      search.addEventListener('input', () => { chatSearch = search.value; chatSig = ''; renderChatList(); });
      chips.addEventListener('click', e => {
        const b = e.target.closest('.pet-chip'); if (!b) return;
        chatChan = b.dataset.chan; chatSig = ''; renderChatList();
      });
      // Clear empties the displayed log but re-snapshots the visible chatbox lines into `seen`, so
      // the next poll does not re-add everything still on screen.
      clr.addEventListener('click', async () => {
        const s = chatStore();
        s.lines = [];
        try {
          const j = JSON.parse(await bridge().chat(myPid()));
          if (j && Array.isArray(j.lines)) { s.seen.clear(); for (const ln of j.lines) if (ln && ln.raw) s.seen.add(ln.raw); }
        } catch (e) {}
        chatSig = ''; renderChatList();
      });
    }
    renderChatList();
  }
  function chatRepaint() { chatSig = ''; paneRun('chatlog', renderChatList); }
  function renderChatList() {
    const list = $('chatList'); if (!list) return;
    const store = chatStore();
    const q = chatSearch.trim().toLowerCase();
    // Signature FIRST: this runs every 250ms poll, so when nothing shown has changed it must do
    // ZERO DOM work or the chips + list rebuild every tick.
    const sig = q + '|' + chatChan + '|' + (chatPkHook ? 1 : 0) + '|' + store.lines.length + '|' + (store.lines[0] ? store.lines[0].raw : '');
    if (sig === chatSig) return;
    chatSig = sig;
    const counts = {}; for (const l of store.lines) counts[l.chan] = (counts[l.chan] || 0) + 1;
    const chipBar = $('chatChips');
    if (chipBar) {
      chipBar.innerHTML = '';
      for (const ch of CHAT_CHANS) {
        const n = ch === 'All' ? store.lines.length : (counts[ch] || 0);
        if (ch !== 'All' && ch !== chatChan && n === 0) continue;   // hide empty channels
        const b = document.createElement('button');
        b.className = 'pet-chip' + (ch === chatChan ? ' on' : '');
        b.dataset.chan = ch; b.textContent = ch + (ch !== 'All' && n ? ' ' + n : '');
        chipBar.appendChild(b);
      }
    }
    const shown = store.lines.filter(l =>
      (chatChan === 'All' || l.chan === chatChan) &&
      (!q || l.plain.toLowerCase().indexOf(q) >= 0));
    const cnt = $('chatCnt');
    if (cnt) cnt.textContent = store.lines.length + ' lines captured' +
      (chatChan !== 'All' || q ? '  ·  ' + shown.length + ' shown' : '') +
      (chatPkHook ? '  ·  live packet capture' :
        (store.lines.length === 0 ? '  ·  open the game chat to capture' : ''));
    if (!shown.length) {
      list.innerHTML = '<div class="chat-empty">' + (store.lines.length ? 'No lines match.' :
        (chatPkHook ? 'No chat captured yet. Messages are logged as they arrive, even with the chatbox closed.'
                    : 'No chat captured yet. Be in-world with the chatbox visible.')) + '</div>';
      return;
    }
    const MAX = 1500;   // cap DOM nodes; search narrows beyond this
    const frag = document.createDocumentFragment();
    for (let i = 0; i < shown.length && i < MAX; i++) {
      const l = shown[i];
      const row = document.createElement('div'); row.className = 'chat-line';
      if (l.ts) { const t = document.createElement('span'); t.className = 'chat-ts'; t.textContent = l.ts; row.appendChild(t); }
      const ch = document.createElement('span'); ch.className = 'chat-ch chat-ch-' + l.chan.toLowerCase();
      ch.textContent = l.chan; row.appendChild(ch);
      const m = document.createElement('span'); m.className = 'chat-msg';
      m.innerHTML = chatHtml(l.tokens, q);
      // Lines carrying game markup expose it on hover (icon indices, colour tags): this is how
      // an unmapped <img=N> index gets identified without a debugger.
      if (l.raw && l.raw.indexOf('<img=') >= 0) row.dataset.tip = 'Raw markup:' + String.fromCharCode(10) + l.raw;
      row.appendChild(m);
      frag.appendChild(row);
    }
    list.innerHTML = '';
    list.appendChild(frag);
    if (shown.length > MAX) {
      const more = document.createElement('div'); more.className = 'chat-empty';
      more.textContent = (shown.length - MAX) + ' older lines hidden. Search to narrow.';
      list.appendChild(more);
    }
  }
