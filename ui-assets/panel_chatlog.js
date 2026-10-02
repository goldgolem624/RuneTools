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
  // Lines are matched across the sources by body and sender. The packet carries the bare name and
  // the store the display name with its title ("Xajek" and "Xajek the Druid"), so one sender name
  // containing the other counts as the same sender.
  const chatSameName = (a, b) => { a = (a || '').toLowerCase(); b = (b || '').toLowerCase(); return !a || !b || a === b || a.indexOf(b) >= 0 || b.indexOf(a) >= 0; };
  function chatMark(map, body, name) {
    const list = map.get(body) || []; list.push({ name: name || '', t: Date.now() }); map.set(body, list);
    if (map.size > 600) map.delete(map.keys().next().value);
  }
  function chatConsume(map, body, name) {
    const list = map.get(body); if (!list) return false;
    const now = Date.now();
    const i = list.findIndex(e => now - e.t < 6000 && chatSameName(e.name, name));
    if (i < 0) return false;
    list.splice(i, 1); if (!list.length) map.delete(body);
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
  // One line that cannot be parsed costs that line, never the fetch: it shows as its raw text.
  function chatParse(raw, base) {
    try { return chatParseInner(String(raw == null ? '' : raw), base); }
    catch (e) { const t = String(raw == null ? '' : raw); return { ts: '', plain: t.trim(), tokens: [{ text: t, color: null }] }; }
  }
  function chatParseInner(raw, base) {
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
      // the stamp's characters come off the leading text tokens; an icon token among them has no
      // characters and stays (it used to stop the whole fetch with a throw)
      let drop = m[0].length, ti = 0;
      while (drop > 0 && ti < tokens.length) {
        const t = tokens[ti];
        if (typeof t.text !== 'string') { ti++; continue; }
        if (t.text.length <= drop) { drop -= t.text.length; tokens.splice(ti, 1); }
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
        const gbody = pr.plain;
        if (nm) {
          pr.tokens.unshift({ text: nm + ': ', color: null });
          pr.plain = nm + ': ' + pr.plain;
        }
        if (bootPk) bootPk.add(gbody);
        if (chatConsume(store.pkPlain, gbody, nm)) {          // the packet capture already had it: this one has the title and the colours
          const hit = fresh.concat(store.lines.slice(0, 300)).find(l => l.src === 'pk' && l.body === gbody && chatSameName(l.pkname, nm) && !l.gmDone);
          if (hit) { hit.tokens = pr.tokens; hit.plain = pr.plain; hit.gmDone = true; hit.baseDone = true; chatSig = ''; }
          continue;
        }
        if (chatConsume(store.ifPlain, gbody, nm)) continue;   // the chatbox walk already had it
        const gts = m.t ? chatFmtTime(m.t * 1000) : '';
        if (!keep(gts, pr.plain)) continue;
        chatMark(store.gmPlain, gbody, nm);
        fresh.push({ raw: key, ts: gts, tokens: pr.tokens, body: gbody,
                     plain: pr.plain, chan: chatClassifyPkt(m.type, nm, String(m.clan || '')), src: 'gm' });
        ++chatFromStore;
      }
      for (const pk of pkts) {
        if (!pk || !(pk.seq > store.pseq)) continue;
        const p = chatParse(String(pk.raw || ''), null);
        if (!p.plain) continue;
        const name = chatNormSpace(String(pk.name || '').replace(/<[^>]*>/g, '')).trim();
        const pbody = p.plain;
        if (name) {
          p.tokens.unshift({ text: name + ': ', color: null });
          p.plain = name + ': ' + p.plain;
        }
        if (bootPk) bootPk.add(pbody);
        if (chatConsume(store.gmPlain, pbody, name)) continue;  // the game's own log already delivered it
        if (chatConsume(store.ifPlain, pbody, name)) continue;  // chatbox walk already delivered it
        const pts = pk.t ? chatFmtTime(pk.t) : '';
        if (!keep(pts, p.plain)) continue;
        chatMark(store.pkPlain, pbody, name);
        fresh.push({ raw: 'pk:' + pk.seq, ts: pts, tokens: p.tokens, body: pbody,
                     plain: p.plain, chan: chatClassifyPkt(pk.type, name, String(pk.chan || '')), src: 'pk',
                     pkraw: String(pk.raw || ''), pkname: name });
      }
      if (pkts.length) for (const pk of pkts) if (pk && pk.seq > store.pseq) store.pseq = pk.seq;
      for (const ln of lines) {
        const raw = ln && ln.raw; if (!raw || store.seen.has(raw)) continue;
        store.seen.add(raw);
        const p = chatParse(raw, ln.base);
        const name = chatNormSpace(String(ln.name || '').replace(/<[^>]*>/g, '')).trim();
        // the chat box writes the display name ("Arcs1, the H'oarder") while its sender field holds
        // the bare name: the prefix before ": " is the sender when it names the same player
        let ibody = p.plain, iname = name;
        { const ci = p.plain.indexOf(': ');
          if (ci > 0 && ci < 64) { const pre = p.plain.slice(0, ci); if (!name || chatSameName(pre, name)) { ibody = p.plain.slice(ci + 2); iname = pre; } } }
        if (bootPk && bootPk.has(ibody)) continue;          // first fill: packet backlog wins
        if (chatConsume(store.pkPlain, ibody, iname)) {      // packet capture already delivered it: this one has the display name, icons and colours
          const hit = fresh.concat(store.lines.slice(0, 300)).find(l => l.src === 'pk' && l.body === ibody && chatSameName(l.pkname, iname) && !l.gmDone);
          if (hit) { hit.tokens = p.tokens; hit.plain = p.plain; hit.baseDone = true; chatSig = ''; }
          continue;
        }
        if (chatConsume(store.gmPlain, ibody, iname)) continue;  // the game's own log already delivered it
        chatMark(store.ifPlain, ibody, iname);
        if (!keep(p.ts, p.plain)) continue;
        fresh.push({ raw, ts: p.ts, tokens: p.tokens, body: ibody, plain: p.plain, chan: chatClassify(p.plain, name), src: 'if' });
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
    } catch (e) { try { console.log('[chatsrc] fetch failed: ' + (e && e.message) + ' | ' + String(e && e.stack || '').split('\n').join(' << ').slice(0, 400)); } catch (e2) {} } finally { chatFetching = false; }
    paneRun('chatlog', renderChatList);
  }
  // Muted NPCs: their chatter never reaches the chat window. Kept in the shared prefs; pushed to the
  // client whenever it changes and every few seconds, so a client that starts later gets it too.
  let chatMuteCfg = null, chatMutePushedAt = 0, chatMutePushedKey = '', chatMuteStatus = null;
  // presets: the NPCs that chatter through an activity, muted or unmuted together
  const CHAT_MUTE_PRESETS = { 'Petrified woodcutting': ['dampknees', 'slugtoes', 'moldfoot', 'bogolin woodcutter'] };
  function chatMuteLoad() {
    if (chatMuteCfg) return chatMuteCfg;
    try { const o = JSON.parse(prefGet('rtxChatMute', '') || '{}'); chatMuteCfg = { names: Array.isArray(o.names) ? o.names : [], all: !!o.all }; }
    catch (e) { chatMuteCfg = { names: [], all: false }; }
    return chatMuteCfg;
  }
  function chatMuteSave() { try { prefSet('rtxChatMute', JSON.stringify(chatMuteLoad())); } catch (e) {} chatMutePushedKey = ''; chatMuteTick(); chatMuteRepaint(); }
  function chatMuteTick() {
    const pid = myPid(); if (!pid || !bridge() || !bridge().chatMute) return;
    const cfg = chatMuteLoad(), now = Date.now();
    const names = (cfg.all ? ['*'] : []).concat(cfg.names);
    const key = pid + '|' + names.join('|');
    if (key === chatMutePushedKey && now - chatMutePushedAt < 5000) return;
    chatMutePushedKey = key; chatMutePushedAt = now;
    try { rtxData.sync('act.chatMute', 2, names.join('|')); } catch (e) {}
  }
  // the box's status line follows the client: refreshed on the same tick
  setInterval(() => { const b = $('chatMuteBox'); if (b) chatMutePaint(b); }, 2000);
  setInterval(chatMuteTick, 1500);
  function chatMuteRepaint() { paneRun('chatlog', () => { const b = $('chatMuteBox'); if (b) chatMutePaint(b); }); }
  function chatMutePaint(box) {
    const cfg = chatMuteLoad();
    let st = null; try { st = JSON.parse(rtxData.sync('host.chatMuteStatus') || '{}'); } catch (e) {}
    chatMuteStatus = st;
    const esc = htmlEsc;
    let h = '<div class="stor-h" style="margin-top:4px">Muted NPCs'
      + (st && st.ok ? '<span class="chat-mute-st">' + (st.hooked ? (st.diag && st.diag[2] ? st.diag[2] + ' lines hidden' + (st.diag[5] ? ' (' + st.diag[5] + ' not removed)' : '') : 'ready') : 'not available in this client') + '</span>' : '')
      + '</div>';
    h += '<div class="pet-chips chat-mute-row">';
    h += '<button class="pet-chip' + (cfg.all ? ' on' : '') + '" data-cmall="1">All NPC chatter</button>';
    for (const pn in CHAT_MUTE_PRESETS) { const on = CHAT_MUTE_PRESETS[pn].every(n => cfg.names.indexOf(n) >= 0); h += '<button class="pet-chip' + (on ? ' on' : '') + '" data-cmpre="' + esc(pn) + '" title="' + esc(CHAT_MUTE_PRESETS[pn].join(', ')) + '">' + esc(pn) + '</button>'; }
    for (const n of cfg.names) h += '<button class="pet-chip on" data-cmdel="' + esc(n) + '" title="Click to unmute">' + esc(n) + ' <span class="chat-mute-x">x</span></button>';
    h += '<input class="pet-search chat-mute-in" id="chatMuteIn" placeholder="NPC name..." maxlength="38"><button class="pet-chip" data-cmadd="1">Mute</button>';
    h += '</div>';
    const heard = (st && Array.isArray(st.recent)) ? st.recent.filter(n => n && cfg.names.indexOf(n) < 0).reverse().slice(0, 12) : [];
    if (heard.length) {
      h += '<div class="chat-mute-heard">Heard: ' + heard.map(n => '<button class="pet-chip" data-cmadd="' + esc(n) + '">' + esc(n) + '</button>').join(' ') + '</div>';
    }
    if (box._h === h) return;
    box._h = h; box.innerHTML = h;
  }
  function chatMuteClick(e) {
    const t = e.target.closest('[data-cmall],[data-cmdel],[data-cmadd],[data-cmpre]'); if (!t) return;
    const cfg = chatMuteLoad();
    if (t.dataset.cmpre) {
      const set = CHAT_MUTE_PRESETS[t.dataset.cmpre] || [];
      const on = set.every(n => cfg.names.indexOf(n) >= 0);
      cfg.names = on ? cfg.names.filter(n => set.indexOf(n) < 0) : cfg.names.concat(set.filter(n => cfg.names.indexOf(n) < 0));
      chatMuteSave(); return;
    }
    if (t.dataset.cmall) { cfg.all = !cfg.all; chatMuteSave(); return; }
    if (t.dataset.cmdel) { cfg.names = cfg.names.filter(n => n !== t.dataset.cmdel); chatMuteSave(); return; }
    let name = t.dataset.cmadd;
    if (name === '1') { const inp = $('chatMuteIn'); name = inp ? inp.value : ''; }
    name = String(name || '').trim().toLowerCase().slice(0, 38);
    if (!name || cfg.names.indexOf(name) >= 0) return;
    cfg.names.push(name); chatMuteSave();
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
      const mute = document.createElement('div'); mute.id = 'chatMuteBox'; mute.className = 'stor-box chat-mute'; wrap.appendChild(mute);
      mute.addEventListener('click', chatMuteClick);
      mute.addEventListener('keydown', e => { if (e.key === 'Enter' && e.target.id === 'chatMuteIn') { e.preventDefault(); chatMuteClick({ target: mute.querySelector('[data-cmadd="1"]') }); } });
      chatMutePaint(mute);
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
  function chatRepaint() { chatSig = ''; paneRun('chatlog', renderChatList); const b = $('chatMuteBox'); if (b) chatMutePaint(b); }
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
