// RuneToolsX panel: Chat Log (searchable, colour-rendered chatbox history).
// Spliced inline into client.html; bare classic script sharing one global scope.

  // Lines come from the chatbox interface (group 137) via bridge.chat(); raw lines keep RS3
  // markup + a "[HH:MM:SS]" timestamp. Dedupe by raw string (the rotating chatbox pool re-sends
  // lines). RS3 encodes literal angle brackets as <lt>/<gt>, so every real "<..>" is an engine
  // tag: whitelist <col>, drop the rest, HTML-escape all text.
  const chatLogs = {};   // pid -> { seen:Set, lines:[{raw, ts, tokens, plain, chan}] }
  let chatFetching = false, chatFetchAt = 0, chatSearch = '', chatSig = '', chatChan = 'All';
  const CHAT_CHANS = ['All', 'Game', 'Public', 'Private', 'Friends', 'Clan', 'Guest', 'Group'];
  function chatStore() {
    const p = myPid();
    if (!chatLogs[p]) chatLogs[p] = { seen: new Set(), lines: [] };
    return chatLogs[p];
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
      // <img=N>, <shad=..>, <str>, <u=..>, etc. -> dropped
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
  function chatHtml(tokens, query) {
    const q = query ? query.toLowerCase() : '';
    let html = '';
    for (const t of tokens) {
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
      const store = chatStore();
      // Bridge returns newest-first as {raw, base, name}; prepend new lines to stay newest-first.
      const fresh = [];
      for (const ln of lines) {
        const raw = ln && ln.raw; if (!raw || store.seen.has(raw)) continue;
        store.seen.add(raw);
        const p = chatParse(raw, ln.base);
        const name = chatNormSpace(String(ln.name || '').replace(/<[^>]*>/g, '')).trim();
        fresh.push({ raw, ts: p.ts, tokens: p.tokens, plain: p.plain, chan: chatClassify(p.plain, name) });
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
  function renderChatList() {
    const list = $('chatList'); if (!list) return;
    const store = chatStore();
    const q = chatSearch.trim().toLowerCase();
    // Signature FIRST: this runs every 250ms poll, so when nothing shown has changed it must do
    // ZERO DOM work or the chips + list rebuild every tick.
    const sig = q + '|' + chatChan + '|' + store.lines.length + '|' + (store.lines[0] ? store.lines[0].raw : '');
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
      (store.lines.length === 0 ? '  ·  open the game chat to capture' : '');
    if (!shown.length) {
      list.innerHTML = '<div class="chat-empty">' + (store.lines.length ? 'No lines match.' : 'No chat captured yet. Be in-world with the chatbox visible.') + '</div>';
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
