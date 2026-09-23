// RuneToolsX panel: Chat Log (searchable, colour-rendered chatbox history).
(function () {

  //  - packets: op-0x15 message_game from the companion's always-on capture ring; arrives
  //  - interface: the chatbox widget walk (group 137), the pre-packet source; still covers
  //  - store: the game's own message store, read straight from the client: every message it has,
  //    with its id, type, sender and clan, whether or not the chatbox is open or filtered
  const chatLogs = {};   // pid -> { seen:Set, pseq, gid, pkPlain:Map, ifPlain:Map, lines:[{raw, ts, tokens, plain, chan, src}] }
  let chatFetching = false; let chatFetchAt = 0; chatSearch = ''; let chatSig = ''; let chatChan = 'All';
  let chatPkHook = false; let chatFromStore = 0;
  const CHAT_CHANS = ['All', 'Game', 'Public', 'Private', 'Friends', 'Clan', 'Guest', 'Group'];
  function chatStore() {
    const p = myPid();
    if (!chatLogs[p]) chatLogs[p] = { seen: new Set(), pseq: 0, gid: 0, pkPlain: new Map(), ifPlain: new Map(), gmPlain: new Map(), lines: [] };
    return chatLogs[p];
  }
  function chatMark(map, plain) { map.set(plain, Date.now()); if (map.size > 600) map.delete(map.keys().next().value); }
  function chatConsume(map, plain) {
    const t = map.get(plain);
    if (t === undefined || Date.now() - t >= 6000) return false;
    map.delete(plain);
    return true;
  }
  // Channel from the wire type id. Verified live: 109 = game/spam, 138 = broadcast news,
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
  // `name` (sender, widget +0x90) separates Public from Game. A custom channel label
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
  function chatNormSpace(s) { let o = ""; for (let i = 0; i < s.length; i++) { const c = s.charCodeAt(i); o += (c === 0xA0 || c === 0x1680 || (c >= 0x2000 && c <= 0x200B) || c === 0x202F || c === 0x205F || c === 0x3000 || c === 0xFEFF) ? " " : s[i]; } return o; }
  function chatEsc(s) { return htmlEsc(s); }
  // colour (widget +0x80, the per-channel colour; `base` RGB int from the bridge), NOT to white.
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
        const n = parseInt(tag.slice(4), 10);
        if (n >= 0) tokens.push({ img: n, color: curColor });
      }
      else if (low.startsWith('sprite=')) {
        const n = parseInt(tag.slice(7), 10);
        if (n >= 0) tokens.push({ spr: n, color: curColor });
      }
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
  const CHAT_ICONS_SPRITE = 1455;
  let chatIconsId = null, chatIconsResolving = false;
  const chatIconUrl = new Map(), chatIconPending = new Set();
  function chatIconSrc(n) {
    if (chatIconsId === null) {
      if (!chatIconsResolving && bridge() && bridge().spriteByName) {
        chatIconsResolving = true;
        (async () => {
          try { const id = await rtxData.raw('cache.spriteByName', 'modicons'); chatIconsId = (id >= 0) ? id : CHAT_ICONS_SPRITE; } catch (e) { chatIconsId = CHAT_ICONS_SPRITE; }
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
  const chatSprUrl = new Map(), chatSprPending = new Set();
  function chatSpriteSrc(id) {
    if (chatSprUrl.has(id)) return chatSprUrl.get(id);
    if (!chatSprPending.has(id) && bridge() && bridge().sprite) {
      chatSprPending.add(id);
      (async () => {
        try { const u = await bridge().sprite(id, 32); if (u) chatSprUrl.set(id, u); } catch (e) {}
        chatSprPending.delete(id);
        if (chatSprUrl.has(id)) { try { chatRepaint(); } catch (e) {} }
      })();
    }
    return '';
  }
  function chatHtml(tokens, query) {
    const q = query ? query.toLowerCase() : '';
    let html = '';
    for (const t of tokens) {
      if (t.img !== undefined || t.spr !== undefined) {
        const src = (t.img !== undefined) ? chatIconSrc(t.img) : chatSpriteSrc(t.spr);
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
      const j = JSON.parse(await rtxData.raw('chat.messages'));
      const lines = (j && Array.isArray(j.lines)) ? j.lines : [];
      const pkts = (j && Array.isArray(j.packets)) ? j.packets : [];
      const gmsgs = (j && Array.isArray(j.store)) ? j.store : [];
      chatPkHook = !!(j && j.phook);
      const store = chatStore();
      const fresh = [];
      const bootPk = (store.pseq === 0 && store.lines.length === 0) ? new Set() : null;
      // The same message can reach us from the store, from the packet capture and from the chatbox
      // walk. The short-lived text match below only covers lines arriving together, so history
      // carries its own key: the time it is shown at and the text, which every source agrees on.
      const already = new Set(store.lines.slice(0, 1200).map(l => (l.ts || '') + '|' + l.plain));
      const keep = (ts, plain) => {
        const k = (ts || '') + '|' + plain;
        if (already.has(k)) return false;
        already.add(k);
        return true;
      };
      // The game's own store, oldest first. Every message it holds is taken the first time, which
      // fills in what happened before the panel was opened; after that only ids we have not seen.
      for (let gi = gmsgs.length - 1; gi >= 0; --gi) {
        const m = gmsgs[gi];
        if (!m || typeof m.id !== 'number') continue;
        const key = 'gm:' + m.id;
        if (store.seen.has(key)) continue;
        store.seen.add(key);
        if (m.id > store.gid) store.gid = m.id;
        const pr = chatParse(String(m.raw || ''), null);
        if (!pr.plain) continue;
        const nm = chatNormSpace(String(m.name || '').replace(/<[^>]*>/g, '')).trim();
        if (nm) {
          pr.tokens.unshift({ text: nm + ': ', color: null });
          pr.plain = nm + ': ' + pr.plain;
        }
        if (bootPk) bootPk.add(pr.plain);
        if (chatConsume(store.pkPlain, pr.plain)) continue;   // the packet capture already had it
        if (chatConsume(store.ifPlain, pr.plain)) continue;   // the chatbox walk already had it
        const gts = m.t ? chatFmtTime(m.t * 1000) : '';
        if (!keep(gts, pr.plain)) continue;
        chatMark(store.gmPlain, pr.plain);
        fresh.push({ raw: key, ts: gts, tokens: pr.tokens,
                     plain: pr.plain, chan: chatClassifyPkt(m.type, nm, String(m.clan || '')), src: 'gm' });
        ++chatFromStore;
      }
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
        if (chatConsume(store.gmPlain, p.plain)) continue;  // the game's own log already delivered it
        if (chatConsume(store.ifPlain, p.plain)) continue;  // chatbox walk already delivered it
        const pts = pk.t ? chatFmtTime(pk.t) : '';
        if (!keep(pts, p.plain)) continue;
        chatMark(store.pkPlain, p.plain);
        fresh.push({ raw: 'pk:' + pk.seq, ts: pts, tokens: p.tokens,
                     plain: p.plain, chan: chatClassifyPkt(pk.type, name, String(pk.chan || '')), src: 'pk',
                     pkraw: String(pk.raw || ''), pkname: name });
      }
      if (pkts.length) for (const pk of pkts) if (pk && pk.seq > store.pseq) store.pseq = pk.seq;
      for (const ln of lines) {
        const raw = ln && ln.raw; if (!raw || store.seen.has(raw)) continue;
        store.seen.add(raw);
        const p = chatParse(raw, ln.base);
        if (bootPk && bootPk.has(p.plain)) continue;        // first fill: packet backlog wins
        if (chatConsume(store.pkPlain, p.plain)) {           // packet capture already delivered it:
          if (typeof ln.base === 'number' && ln.base >= 0) {
            const hit = fresh.concat(store.lines.slice(0, 300)).find(l => l.src === 'pk' && l.plain === p.plain && !l.baseDone);
            if (hit) { const q = chatParse(hit.pkraw || '', ln.base); if (hit.pkname) q.tokens.unshift({ text: hit.pkname + ': ', color: null }); hit.tokens = q.tokens; hit.baseDone = true; chatSig = ""; }
          }
          continue;
        }
        if (chatConsume(store.gmPlain, p.plain)) continue;   // the game's own log already delivered it
        chatMark(store.ifPlain, p.plain);
        const name = chatNormSpace(String(ln.name || '').replace(/<[^>]*>/g, '')).trim();
        if (!keep(p.ts, p.plain)) continue;
        fresh.push({ raw, ts: p.ts, tokens: p.tokens, plain: p.plain, chan: chatClassify(p.plain, name), src: 'if' });
      }
      if (fresh.length) {
        const gm = fresh.filter(l => l.src === 'gm').reverse();   // the store gave them oldest first
        const rest = fresh.filter(l => l.src !== 'gm');
        store.lines = rest.concat(gm, store.lines);
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
      clr.addEventListener('click', async () => {
        const s = chatStore();
        s.lines = [];
        try {
          const j = JSON.parse(await rtxData.raw('chat.messages'));
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
      (chatFromStore ? '  ·  from the game\'s own log' :
        (chatPkHook ? '  ·  live packet capture' :
          (store.lines.length === 0 ? '  ·  open the game chat to capture' : '')));
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
      if (l.raw && (l.raw.indexOf('<img=') >= 0 || l.raw.indexOf('<sprite=') >= 0)) row.dataset.tip = 'Raw markup:' + String.fromCharCode(10) + l.raw;
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

Object.assign(window, { fetchChat });
registerTab({ id: 'chatlog', render: renderChat, open: function () { chatSig = ''; fetchChat(true); } });
})();
