// RuneToolsX panel: Server Packets (server -> client protocol + live inbound feed).
(function () {

  let npEnum = null;                 // {op -> {len, kind, handler}} from the client's descriptor table (host.serverPackets)
  let npEnumTried = false, npEnumErr = '';
  let npRecords = [];
  let npCounts = {}, npBytes = {};
  let npCursor = 0;
  let npMeta = null;
  let npPaused = false, npView = 'live', npAscii = false;
  let npFilter = '';                 // hex/dec opcode, "0x10-0x20" range, or name substring
  let npAt = 0;
  const NP_CAP = 5000;
  // {name: opcode} comes from the host (state.serverOps, the table the reader itself decodes with),
  // so the panel never carries a copy of the opcodes that can go stale. Empty until the host answers.
  const SOPS = {};
  let npNames = {};
  function npApplySops(m) {
    Object.assign(SOPS, m || {});
    for (const [name, op] of Object.entries(SOPS)) if (name !== 'op_max' && typeof op === 'number') npNames[op] = name;
    npDecoders = {};
    for (const [name, fn] of Object.entries(npDecodersByName)) if (typeof SOPS[name] === 'number') npDecoders[SOPS[name]] = fn;
  }
  try {
    const saved = JSON.parse(localStorage.getItem('rtxNetNames') || 'null');
    if (saved && typeof saved === 'object') npNames = Object.assign(npNames, saved);
  } catch (e) {}
  function npNamesSave() { try { localStorage.setItem('rtxNetNames', JSON.stringify(npNames)); } catch (e) {} }
  (async () => { try { npApplySops(await rtxData.call('state.serverOps')); } catch (e) { npApplySops(null); } })();

  function npHex2(n) { return '0x' + (n & 0xFF).toString(16).toUpperCase().padStart(2, '0'); }
  function npKind(len) {
    if (len === -1) return 'var-byte';
    if (len === -2) return 'var-short';
    if (len < 0) return 'var';
    return 'fixed(' + len + ')';
  }
  function npName(op) { return npNames[op] || ''; }

  function npArm(on) {
    try { if (bridge() && bridge().serverPacketArm) rtxData.sync('act.serverPacketArm', on); } catch (e) {}
  }

  async function npFetchEnum() {
    if (npEnum || npEnumTried || !bridge() || !bridge().serverPackets) return;
    npEnumTried = true;
    try {
      const d = JSON.parse(await rtxData.raw('host.serverPackets') || '{}');
      if (d.ok && Array.isArray(d.packets)) {
        npEnum = {};
        for (const p of d.packets) npEnum[p.op] = { len: p.len, kind: p.kind, handler: p.handler };
      } else { npEnumErr = d.reason || 'unavailable'; npEnumTried = false; }   // retry next poll
    } catch (e) { npEnumTried = false; }
  }

  async function npFetch() {
    if (npPaused) return;
    if (!bridge() || !bridge().serverPacketFeed) return;
    const now = Date.now();
    if (now - npAt < 120) return;    // cap fetch rate independent of the 250ms paint tick
    npAt = now;
    npArm(true);
    npFetchEnum();
    let d = null;
    try { d = JSON.parse(await rtxData.raw('host.serverPacketFeed', npCursor) || 'null'); } catch (e) { return; }
    if (!d || !d.ok) { npMeta = d || null; paneRun('netprobe', npUpdateDynamic); return; }
    npMeta = d;
    if (Array.isArray(d.packets)) {
      for (const r of d.packets) {
        if (r.seq <= npCursor) continue;
        npCursor = r.seq;
        npRecords.push(r);
        npCounts[r.op] = (npCounts[r.op] || 0) + 1;
        npBytes[r.op] = (npBytes[r.op] || 0) + (r.len > 0 ? r.len : r.kept);
      }
      if (npRecords.length > NP_CAP) npRecords.splice(0, npRecords.length - NP_CAP);
    }
    paneRun('netprobe', npUpdateDynamic);
  }



  function npParseFilter() {
    const f = npFilter.trim().toLowerCase();
    if (!f) return null;
    const m = f.match(/^(0x[0-9a-f]+|\d+)\s*-\s*(0x[0-9a-f]+|\d+)$/);
    if (m) { const a = parseInt(m[1]), b = parseInt(m[2]); return { lo: Math.min(a, b), hi: Math.max(a, b), text: '' }; }
    if (/^(0x[0-9a-f]+|\d+)$/.test(f)) { const v = parseInt(f); return { lo: v, hi: v, text: '' }; }
    return { lo: -1, hi: 0x7fffffff, text: f };
  }
  function npMatch(op, flt) {
    if (!flt) return true;
    if (op < flt.lo || op > flt.hi) return false;
    if (flt.text) { return (npName(op).toLowerCase().indexOf(flt.text) >= 0); }
    return true;
  }

  function npAsciiOf(hex) {
    let s = '';
    for (let i = 0; i + 1 < hex.length; i += 2) {
      const c = parseInt(hex.substr(i, 2), 16);
      s += (c >= 0x20 && c < 0x7f) ? String.fromCharCode(c) : '.';
    }
    return s;
  }
  function npHexSpaced(hex) {
    let s = '';
    for (let i = 0; i + 1 < hex.length; i += 2) { s += hex.substr(i, 2).toUpperCase() + ' '; }
    return s.trim();
  }
  function npNumHint(r) {
    const n = r.kept;
    if (n !== 1 && n !== 2 && n !== 4) return '';
    let be = 0;
    for (let i = 0; i < n; i++) be = be * 256 + parseInt(r.hex.substr(i * 2, 2), 16);
    const bits = n * 8;
    const s = (be >= Math.pow(2, bits - 1)) ? be - Math.pow(2, bits) : be;
    const txt = (be === s) ? ('=' + be) : ('=' + be + ' / ' + s);
    return '<span style="color:#e0c060">' + txt + '</span>  ';
  }
  function npParseBytes(r) {
    const b = [];
    for (let i = 0; i + 1 < r.hex.length; i += 2) b.push(parseInt(r.hex.substr(i, 2), 16));
    return b;
  }
  const NP_SKILLS = ['Attack', 'Defence', 'Strength', 'Constitution', 'Ranged', 'Prayer', 'Magic',
    'Cooking', 'Woodcutting', 'Fletching', 'Fishing', 'Firemaking', 'Crafting', 'Smithing', 'Mining',
    'Herblore', 'Agility', 'Thieving', 'Slayer', 'Farming', 'Runecrafting', 'Hunter', 'Construction',
    'Summoning', 'Dungeoneering', 'Divination', 'Invention', 'Archaeology', 'Necromancy'];
  // Inv-definition ids of the containers named in the feed.
  const NP_CONTAINERS = {
    93: 'inventory', 94: 'equipment', 95: 'bank', 623: 'money pouch', 858: 'metal bank',
    867: 'bait box', 885: 'arch materials', 963: 'group bank', 1008: 'workbench'
  };
  function npContainerName(id) { return NP_CONTAINERS[id] ? (NP_CONTAINERS[id] + '(' + id + ')') : ('container ' + id); }
  const npItemCache = { 995: 'Coins' };
  function npItemName(id) {
    const c = npItemCache[id];
    if (typeof c === 'string') return c;
    if (c === undefined && bridge() && bridge().itemInfo) {
      npItemCache[id] = true;                       // in flight: ask only once
      (async () => {
        try { const d = JSON.parse(await rtxData.raw('cache.itemInfo', id) || '{}'); npItemCache[id] = (d && d.name) || ('item ' + id); }
        catch (e) { npItemCache[id] = 'item ' + id; }
      })();
    }
    return 'item ' + id;
  }
  let npDecoders = {};   // opcode -> decoder, rebuilt from npDecodersByName whenever the opcode table arrives
  const npU32 = (b, p) => (((b[p] << 24) | (b[p + 1] << 16) | (b[p + 2] << 8) | b[p + 3]) >>> 0);
  const npI32 = (b, p) => ((b[p] << 24) | (b[p + 1] << 16) | (b[p + 2] << 8) | b[p + 3]);
  const npU16 = (b, p) => ((b[p] << 8) | b[p + 1]);
  const npI8 = v => ((v & 0xFF) << 24 >> 24);
  const npNum = v => Number(v).toLocaleString('en-US');
  const npPos = v => '@(' + ((v >> 4) & 7) + ',' + (v & 7) + ')';   // zone-local position byte
  // Item encoding shared by the container packets: [itemId+1: u24 BE][qty u8, or 0xFF then u32 BE][variant byte if flags&2].
  function npReadItem(b, p, flags) {
    if (p + 3 > b.length) return null;
    const item1 = (b[p] << 16) | (b[p + 1] << 8) | b[p + 2]; p += 3;
    if (item1 === 0) return { p: p, item: -1, qty: 0 };
    if (p >= b.length) return null;
    let qty = b[p++];
    if (qty === 0xFF) { if (p + 4 > b.length) return null; qty = npU32(b, p); p += 4; }
    if (flags & 2) { if (p < b.length) p++; }
    return { p: p, item: item1 - 1, qty: qty };
  }
  // Zone sub-packet body lengths (sub id -> bytes; -1 = one length byte then the body) and the top-level names of the same bodies.
  const NP_ZONE_SUB = { 0: 6, 1: 11, 2: 14, 3: 5, 4: 10, 5: 21, 6: 8, 7: -1, 8: -1, 9: 8, 10: 4, 11: 11, 12: 2, 13: 7, 14: -1, 15: 20, 16: 28, 17: 29 };
  const NP_ZONE_NAME = { 0: 'obj_add', 1: 'area_sound', 2: 'spotanim', 4: 'area_sound', 5: 'projectile', 6: 'obj_count', 9: 'obj_add', 10: 'obj_del', 11: 'spotanim', 12: 'loc_del', 13: 'loc_add', 15: 'projectile', 16: 'projectile', 17: 'projectile' };
  // Decoders by the host's opcode name (the layouts the launcher's event decoder uses).
  const npDecodersByName = {
    skill_update: function (b) {                                   // [skill: -b0][level: -b1][xp: u32 BE]
      if (b.length < 6) return '';
      const sk = (256 - b[0]) & 0xFF, level = (256 - b[1]) & 0xFF;
      return (NP_SKILLS[sk] || ('skill ' + sk)) + ' Lv' + level + ' xp=' + npNum(npU32(b, 2));
    },
    container_update: function (b) {                               // [container: u16 BE][flags: u8] then per slot [slot: smart][item][qty][variant]
      if (b.length < 3) return '';
      const cont = npU16(b, 0), flags = b[2];
      let p = 3; const slots = [];
      while (p < b.length && slots.length < 32) {
        let slot;
        if (b[p] < 0x80) { slot = b[p]; p += 1; }
        else { if (p + 2 > b.length) break; slot = (npU16(b, p) + 0x8000) & 0xFFFF; p += 2; }
        const it = npReadItem(b, p, flags); if (!it) break; p = it.p;
        slots.push('#' + slot + (it.item < 0 ? ' empty' : ' ' + npItemName(it.item) + ' x' + npNum(it.qty)));
      }
      return npContainerName(cont) + ': ' + (slots.join(', ') || '(no slots)');
    },
    container_full: function (b) {                                 // [container: u16 BE][flags: u8][count: u16 BE] then count x [item][qty][variant]; flags&1 = another player's
      if (b.length < 5) return '';
      const cont = npU16(b, 0), flags = b[2], count = npU16(b, 3);
      let p = 5, filled = 0; const slots = [];
      for (let slot = 0; slot < count; slot++) {
        const it = npReadItem(b, p, flags); if (!it) break; p = it.p;
        if (it.item < 0) continue;
        filled++;
        if (slots.length < 24) slots.push('#' + slot + ' ' + npItemName(it.item) + ' x' + npNum(it.qty));
      }
      return npContainerName(cont) + (flags & 1 ? ' (other player)' : '') + ' full: ' + count + ' slots, ' + filled + ' filled' + (slots.length ? ': ' + slots.join(', ') : '');
    },
    container_reset: function (b) {                                // [other: -b0 & 1][container: (b2-0x80)&0xFF | b1<<8]
      if (b.length < 3) return '';
      return npContainerName(((b[2] - 0x80) & 0xFF) | (b[1] << 8)) + ' reset' + (((256 - b[0]) & 1) ? ' (other player)' : '');
    },
    message_game: function (b) {                                   // [type: smart][u32][flags]; flags&1 adds a NUL sender (flags&2 a second one), then the NUL text
      if (b.length < 6) return '';
      let p, type;
      if (b[0] < 0x80) { type = b[0]; p = 1; }
      else { type = (npU16(b, 0) + 0x8000) & 0xFFFF; p = 2; }
      p += 4;
      if (p >= b.length) return '';
      const flags = b[p++];
      const readStr = function () { let s = ''; while (p < b.length && b[p] !== 0) s += String.fromCharCode(b[p++]); p++; return s; };
      let name = '';
      if (flags & 1) { name = readStr(); if (flags & 2) readStr(); }
      const text = readStr();
      return 'type ' + type + (name ? ' <' + name + '>' : '') + ' "' + text + '"';
    },
    runclientscript: function (b) {                                // [sig NUL][args in reverse sig order: s NUL string, i i32 BE, l i64 BE][script i32 BE]
      let p = 0, sig = '';
      while (p < b.length && b[p] !== 0 && sig.length < 16) sig += String.fromCharCode(b[p++]);
      if (!sig || p >= b.length || !/^[isl]+$/.test(sig)) return '';
      p++;
      const wire = [];
      for (let i = sig.length - 1; i >= 0; i--) {
        if (sig[i] === 's') {
          let s = '';
          while (p < b.length && b[p] !== 0) s += String.fromCharCode(b[p++]);
          if (p >= b.length) return 'sig ' + sig + ' (truncated)';
          p++; wire.push('"' + s + '"');
        } else if (sig[i] === 'l') {
          if (p + 8 > b.length) return 'sig ' + sig + ' (truncated)';
          wire.push(String(npI32(b, p) * 4294967296 + npU32(b, p + 4))); p += 8;
        } else {
          if (p + 4 > b.length) return 'sig ' + sig + ' (truncated)';
          wire.push(String(npI32(b, p))); p += 4;
        }
      }
      if (p + 4 > b.length) return 'sig ' + sig + ' (truncated)';
      return 'script ' + npU32(b, p) + '(' + wire.reverse().join(', ') + ')';
    },
    zone_base: function (b) {                                      // [y: i8 zones][-x: i8 zones][plane: b2+0x80], tiles off the loaded map's base
      if (b.length < 3) return '';
      return 'zone y' + (npI8(b[0]) * 8) + ' x' + (-npI8(b[1]) * 8) + ' plane ' + ((b[2] + 0x80) & 0xFF) + ' (off the map base)';
    },
    zone_clear: function (b) {                                     // [plane: b0+0x80][x: i8 zones][y: i8 zones]
      if (b.length < 3) return '';
      return 'clear zone x' + (npI8(b[1]) * 8) + ' y' + (npI8(b[2]) * 8) + ' plane ' + ((b[0] + 0x80) & 0xFF);
    },
    zone_update: function (b) {                                    // [plane: 0x80-b0][-x: i8 zones][y: i8 zones] then [sub id][body]...
      if (b.length < 3) return '';
      const out = ['plane ' + ((0x80 - b[0]) & 0xFF) + ' zone x' + (-npI8(b[1]) * 8) + ' y' + (npI8(b[2]) * 8)];
      let p = 3;
      while (p < b.length && out.length < 16) {
        const sub = b[p++]; let len = NP_ZONE_SUB[sub];
        if (len === undefined) { out.push('sub ' + sub + ' (unknown) ...'); break; }
        if (len === -1) { if (p >= b.length) { out.push('sub ' + sub + ' (cut)'); break; } len = b[p++]; }
        if (p + len > b.length) { out.push('sub ' + sub + ' (cut)'); break; }
        const body = b.slice(p, p + len); p += len;
        const nm = NP_ZONE_NAME[sub];
        const dec = (nm && [0, 6, 10, 12, 13].indexOf(sub) >= 0) ? npDecodersByName[nm](body) : '';   // subs whose body equals the top-level packet
        out.push(dec || ('sub ' + sub + (nm ? ' ' + nm : '') + ' +' + len + 'B'));
      }
      return out.join(' | ');
    },
    obj_add: function (b) {                                        // [item: u24, b0 low][position: 0x80-b3][qty: u16 BE]
      if (b.length < 6) return '';
      return npItemName((b[2] << 16) | (b[1] << 8) | b[0]) + ' x' + npNum(npU16(b, 4)) + ' ' + npPos((0x80 - b[3]) & 0xFF);
    },
    obj_del: function (b) {                                        // [position: 0x80-b0][item: b3 b2 b1]
      if (b.length < 4) return '';
      return npItemName((b[3] << 16) | (b[2] << 8) | b[1]) + ' gone ' + npPos((0x80 - b[0]) & 0xFF);
    },
    obj_count: function (b) {                                      // [position b0][item: u24 BE][old qty: u16 BE][new qty: u16 BE]
      if (b.length < 8) return '';
      return npItemName((b[1] << 16) | (b[2] << 8) | b[3]) + ' ' + npNum(npU16(b, 4)) + ' -> ' + npNum(npU16(b, 6)) + ' ' + npPos(b[0]);
    },
    loc_add: function (b) {                                        // [-b0: type bits 2-6, rotation bits 0-1][id: b3 b4 b1 b2][b5][position: -b6]
      if (b.length < 7) return '';
      const k = (256 - b[0]) & 0xFF;
      const id = (((b[3] << 24) | (b[4] << 16) | (b[1] << 8) | b[2]) >>> 0);
      return 'loc ' + id + ' type ' + ((k >> 2) & 0x1F) + ' rot ' + (k & 3) + ' ' + npPos((256 - b[6]) & 0xFF);
    },
    loc_del: function (b) {                                        // [position: 0x80-b0][b1+0x80: type bits 2-6, rotation bits 0-1]
      if (b.length < 2) return '';
      const k = (b[1] + 0x80) & 0xFF;
      return 'loc removed type ' + ((k >> 2) & 0x1F) + ' rot ' + (k & 3) + ' ' + npPos((0x80 - b[0]) & 0xFF);
    },
    varp_int: function (b) {                                       // [id: (b0-0x80)&0xFF | b1<<8][value: b4 b5 b2 b3]
      if (b.length < 6) return '';
      const id = ((b[0] - 0x80) & 0xFF) | (b[1] << 8);
      return npVarLabel(id) + ' = ' + npVarVal(id, (b[4] << 24) | (b[5] << 16) | (b[2] << 8) | b[3]);
    },
    varp_byte: function (b) {                                      // [value: i8][id: (b2-0x80)&0xFF | b1<<8]
      if (b.length < 3) return '';
      const id = ((b[2] - 0x80) & 0xFF) | (b[1] << 8);
      return npVarLabel(id) + ' = ' + npI8(b[0]);
    },
    varp_long: function (b) {                                      // [i64: hi = b1 b0 b3 b2, lo = b5 b4 b7 b6][id: u16 BE]
      if (b.length < 10) return '';
      const hi = (b[1] << 24) | (b[0] << 16) | (b[3] << 8) | b[2], lo = (((b[5] << 24) | (b[4] << 16) | (b[7] << 8) | b[6]) >>> 0);
      return 'varp ' + npU16(b, 8) + ' = ' + String(hi * 4294967296 + lo) + ' (64-bit)';
    },
    varc_int: function (b) {                                       // [id: (b1-0x80)&0xFF | b0<<8][value: b3 b2 b5 b4]
      if (b.length < 6) return '';
      return 'varc ' + (((b[1] - 0x80) & 0xFF) | (b[0] << 8)) + ' = ' + ((b[3] << 24) | (b[2] << 16) | (b[5] << 8) | b[4]);
    },
    varc_byte: function (b) {                                      // [id: u16 LE][value: (0x80-b2) as i8]
      if (b.length < 3) return '';
      return 'varc ' + (b[0] | (b[1] << 8)) + ' = ' + npI8(0x80 - b[2]);
    },
    varc_long: function (b) {                                      // [hi: u32 LE][lo: u32 LE][id: u16 LE]
      if (b.length < 10) return '';
      const hi = (b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24)), lo = ((b[4] | (b[5] << 8) | (b[6] << 16) | (b[7] << 24)) >>> 0);
      return 'varc ' + (b[8] | (b[9] << 8)) + ' = ' + String(hi * 4294967296 + lo) + ' (64-bit)';
    },
    varbit_set: function (b) {                                     // two LEB128 varints: varbit id, value
      let p = 0; const v = [0, 0];
      for (let k = 0; k < 2; k++) {
        let sh = 0;
        for (;;) {
          if (p >= b.length || sh > 28) return '';
          const c = b[p++];
          v[k] += (c & 0x7F) * Math.pow(2, sh); sh += 7;
          if (c < 0x80) break;
        }
      }
      return 'varbit ' + v[0] + ' = ' + (v[1] | 0);
    },
    varbit_byte: function (b) {                                    // [value: (b0+0x80)&0xFF][varbit id: u16 LE at 1]
      if (b.length < 3) return '';
      return 'varbit ' + (b[1] | (b[2] << 8)) + ' = ' + ((b[0] + 0x80) & 0xFF);
    },
    varbit_int: function (b) {                                     // [value: i32 BE][varbit id: u16 LE at 4]
      if (b.length < 6) return '';
      return 'varbit ' + (b[4] | (b[5] << 8)) + ' = ' + npI32(b, 0);
    },
    ping_echo: function (b) {                                      // two u32 BE the client echoes back
      if (b.length < 8) return '';
      return 'echo ' + npU32(b, 0) + ' / ' + npU32(b, 4) + ' (client replies 9B)';
    },
    ge_offer: function (b) {                                       // [market][slot][header: 0 clears, status = h&7, buy/sell bit 3; 7 = extended: w, status/type, item, price, count, filled, gold]
      if (b.length < 3) return '';
      const market = b[0], slot = b[1], hdr = b[2];
      if (hdr === 0) return 'market ' + market + ' slot ' + slot + ' cleared';
      let status = hdr & 7, type = (hdr >> 3) & 1;
      if (status !== 7) return 'market ' + market + ' slot ' + slot + ' status ' + status + (type ? ' sell' : ' buy');
      if (b.length < 5) return '';
      const w = b[3]; status = b[4] & 7; type = (b[4] >> 3) & 1;
      let p = 5, item, price, gold;
      if (w >= 3) { if (p + 3 > b.length) return ''; item = (b[p] << 16) | (b[p + 1] << 8) | b[p + 2]; p += 3; }
      else { if (p + 2 > b.length) return ''; item = npU16(b, p); p += 2; }
      if (w >= 2) { if (p + 8 > b.length) return ''; price = npI32(b, p) * 4294967296 + npU32(b, p + 4); p += 8; }
      else { if (p + 4 > b.length) return ''; price = npU32(b, p); p += 4; }
      if (p + 8 > b.length) return '';
      const count = npI32(b, p), filled = npI32(b, p + 4); p += 8;
      if (w >= 2) { if (p + 8 > b.length) return ''; gold = npI32(b, p) * 4294967296 + npU32(b, p + 4); }
      else { if (p + 4 > b.length) return ''; gold = npU32(b, p); }
      return 'market ' + market + ' slot ' + slot + (type ? ' sell ' : ' buy ') + npItemName(item) + ' x' + npNum(count) + ' @ ' + npNum(price) + ' filled ' + npNum(filled) + ' (' + npNum(gold) + ' gp) status ' + status;
    },
    run_energy: function (b) { return b.length < 1 ? '' : 'energy ' + b[0]; },
    run_weight: function (b) { return b.length < 2 ? '' : 'weight ' + ((npU16(b, 0) << 16) >> 16); },
    tracker_group: function (b) {                                  // [group id: b2 b3 b0 b1][slot: (int8)(b4+0x80)]
      if (b.length < 5) return '';
      return 'tracker group ' + ((b[2] << 24) | (b[3] << 16) | (b[0] << 8) | b[1]) + ' at slot ' + npI8(b[4] + 0x80);
    },
    tracker_values: function (b) {                                 // { group (0xFF ends) { row (0xFF ends) { column (0xFF ends), value i32 BE } } }
      let p = 0; const out = [];
      while (p < b.length) {
        const g = b[p++]; if (g === 0xFF) break;
        while (p < b.length) {
          const r = b[p++]; if (r === 0xFF) break;
          while (p < b.length) {
            const c = b[p++]; if (c === 0xFF) break;
            if (p + 4 > b.length) return out.join('  ') + ' (cut)';
            const v = npI32(b, p); p += 4;
            if (out.length < 40) out.push('[' + g + ',' + r + ',' + c + ']=' + (v === -2147483648 ? 'none' : npNum(v)));
          }
        }
      }
      return out.join('  ');
    },
    tracker_remove: function (b) { return b.length < 1 ? '' : 'tracker slot ' + npI8(256 - b[0]) + ' removed'; },
    tracker_clear: function (b) {                                  // [group b0][column: -b1][row b2]
      if (b.length < 3) return '';
      return 'tracker [' + b[0] + ',' + b[2] + ',' + ((256 - b[1]) & 0xFF) + '] cleared';
    },
    tracker_column: function (b) {                                 // [group: 0x80-b0][column b1][shown: b2 == 0x81]
      if (b.length < 3) return '';
      return 'tracker group ' + ((0x80 - b[0]) & 0xFF) + ' column ' + b[1] + (b[2] === 0x81 ? ' shown' : ' hidden');
    },
    system_update: function (b) { return b.length < 2 ? '' : 'system update in ' + npU16(b, 0) + ' s'; },
    camera_target: function (b) {                                  // [packed tile: b1 b0 b3 b2]; 0xFFFFFFFF clears
      if (b.length < 4) return '';
      const v = (((b[1] << 24) | (b[0] << 16) | (b[3] << 8) | b[2]) >>> 0);
      return v === 0xFFFFFFFF ? 'camera target cleared' : 'camera target ' + ((v >> 14) & 0x3FFF) + ',' + (v & 0x3FFF) + ' plane ' + ((v >> 28) & 3);
    },
    cutscene: function (b) { return b.length < 2 ? '' : 'cutscene ' + npU16(b, 0); },
    private_filter: function (b) { return b.length < 1 ? '' : 'private chat filter ' + b[0]; },
    minimap_state: function (b) { return b.length < 1 ? '' : 'minimap state ' + b[0] + ' (mode ' + (b[0] % 3) + (b[0] < 3 ? ', shown)' : ', hidden)'); },
  };
  const NP_VARNAMES = {
    5984: { name: 'invention_charge', fmt: function (v) { return Math.floor(v / 3000).toLocaleString('en-US') + ' charge (raw ' + v.toLocaleString('en-US') + ')'; } }
  };
  function npVarLabel(id) { const e = NP_VARNAMES[id]; return e ? (e.name + '(' + id + ')') : ('varp ' + id); }
  function npVarVal(id, v) { const e = NP_VARNAMES[id]; return (e && e.fmt) ? e.fmt(v) : v.toLocaleString('en-US'); }
  function npPayload(r) {
    if (!r.kept) return r.len === 0 ? '(empty)' : '';
    const body = npAscii ? npAsciiOf(r.hex) : npHexSpaced(r.hex);
    const more = (r.len > r.kept) ? ' ...(+' + (r.len - r.kept) + 'B)' : '';
    const dec = npDecoders[r.op] ? npDecoders[r.op](npParseBytes(r)) : '';
    const hint = dec ? ('<span style="color:#7ec699">' + htmlEsc(dec) + '</span>  ') : npNumHint(r);
    return hint + htmlEsc(body) + more;
  }

  function npHealthHtml() {
    if (!npMeta) return '<span style="color:var(--text-dim)">waiting</span>';
    if (npMeta.ok === false) {
      return '<span style="color:#e6a23c">' + htmlEsc(npMeta.reason || 'feed unavailable')
           + '</span> <span style="color:var(--text-dim)">(companion feed off or not loaded)</span>';
    }
    const hooked = (npMeta.flags & 1) ? 'hooked' : 'NOT hooked';
    const hookCol = (npMeta.flags & 1) ? 'var(--good, #67c23a)' : '#f56c6c';
    const d = npMeta.diag || [0, 0, 0, 0];
    return '<span style="color:' + hookCol + '">framer ' + hooked + '</span>'
      + ' <span style="color:var(--text-dim)">rva ' + htmlEsc(npMeta.framerRva || '0x0')
      + ' | seen ' + (npMeta.seen | 0) + ' | written ' + (npMeta.written | 0)
      + ' | calls ' + (d[0] | 0) + ' | rec ' + (d[1] | 0)
      + ' | skip(range ' + (d[2] | 0) + '/dup ' + (d[3] | 0) + ')</span>';
  }

  function npRename(op) {
    const cur = npNames[op] || '';
    const v = prompt('Name for opcode ' + npHex2(op) + ' (blank to clear):', cur);
    if (v === null) return;
    if (v.trim()) npNames[op] = v.trim().replace(/\s+/g, '_'); else delete npNames[op];
    npNamesSave();
    npPaint();
  }
  window.npRename = npRename;

  function renderNetProbe() {
    npFetch();
    if ($('np-body')) npUpdateDynamic(); else npPaint();
  }
  function npUpdateDynamic() {
    const h = $('np-health'); if (h) h.innerHTML = npHealthHtml();
    const b = $('np-body'); if (b) b.innerHTML = (npView === 'protocol') ? npProtocolRows(npParseFilter()) : npLiveRows(npParseFilter());
  }

  function npPaint() {
    const c = paneRoot('netprobe'); if (!c) return;   // also reached from event handlers, outside shell dispatch
    const flt = npParseFilter();

    let html = '';
    html += '<div class="pane" style="display:flex;flex-direction:column;height:100%;min-height:0">';
    html += '<div style="display:flex;gap:6px;align-items:center;flex-wrap:wrap;padding:2px 0 6px">';
    html += npTab('live', 'Live feed') + npTab('protocol', 'Protocol');
    html += '<span style="flex:0 0 10px"></span>';
    if (npView === 'live') {
      html += npBtn('np-pause', npPaused ? 'Resume' : 'Pause');
      html += npBtn('np-clear', 'Clear');
      html += npBtn('np-ascii', npAscii ? 'Hex' : 'ASCII');
    }
    html += '<input id="np-filter" placeholder="filter opcode: 0x5C, 92, 0x10-0x20, name" '
          + 'value="' + htmlEsc(npFilter) + '" '
          + 'style="flex:1;min-width:140px;background:var(--bg-2,#1c1c22);border:1px solid var(--border,#333);'
          + 'color:var(--text,#ddd);border-radius:4px;padding:3px 6px;font:inherit">';
    html += '</div>';
    html += '<div id="np-health" style="font-size:11px;padding:0 0 6px;line-height:1.5">' + npHealthHtml() + '</div>';

    if (npView === 'protocol') html += npProtocolHtml(flt);
    else html += npLiveHtml(flt);

    html += '</div>';
    c.innerHTML = html;

    const bind = (id, fn) => { const e = $(id); if (e) e.onclick = fn; };
    bind('np-tab-live', () => { npView = 'live'; npPaint(); });
    bind('np-tab-protocol', () => { npView = 'protocol'; npPaint(); });
    bind('np-pause', () => { npPaused = !npPaused; npPaint(); });
    bind('np-clear', () => { npRecords = []; npCounts = {}; npBytes = {}; npPaint(); });
    bind('np-ascii', () => { npAscii = !npAscii; npPaint(); });
    const fe = $('np-filter');
    if (fe) fe.oninput = () => { npFilter = fe.value; npPaintBodyOnly(flt); };
  }

  function npPaintBodyOnly() {
    const holder = $('np-body'); if (!holder) { npPaint(); return; }
    const flt = npParseFilter();
    holder.innerHTML = (npView === 'protocol') ? npProtocolRows(flt) : npLiveRows(flt);
  }

  function npTab(id, label) {
    const on = npView === id;
    return '<button id="np-tab-' + id + '" style="background:' + (on ? 'var(--accent,#3a6df0)' : 'var(--bg-2,#1c1c22)')
      + ';color:' + (on ? '#fff' : 'var(--text,#ddd)') + ';border:1px solid var(--border,#333);'
      + 'border-radius:4px;padding:3px 10px;cursor:pointer;font:inherit">' + label + '</button>';
  }
  function npBtn(id, label) {
    return '<button id="' + id + '" style="background:var(--bg-2,#1c1c22);color:var(--text,#ddd);'
      + 'border:1px solid var(--border,#333);border-radius:4px;padding:3px 8px;cursor:pointer;font:inherit">'
      + htmlEsc(label) + '</button>';
  }
  const NP_TH = 'text-align:left;padding:3px 8px;position:sticky;top:0;background:var(--bg-1,#141418);'
              + 'border-bottom:1px solid var(--border,#333);font-weight:600;color:var(--text-dim,#999)';
  const NP_TD = 'padding:2px 8px;border-bottom:1px solid var(--border,#26262c);vertical-align:top';

  function npLiveHtml(flt) {
    return '<div style="flex:1;min-height:0;overflow-y:auto;overflow-x:hidden;border:1px solid var(--border,#26262c);border-radius:4px">'
      + '<table style="width:100%;table-layout:fixed;border-collapse:collapse;font:12px/1.4 ui-monospace,Consolas,monospace">'
      + '<colgroup><col style="width:52px"><col style="width:56px"><col style="width:112px"><col style="width:44px"><col></colgroup>'
      + '<thead><tr>'
      + '<th style="' + NP_TH + '">#</th><th style="' + NP_TH + '">t(ms)</th>'
      + '<th style="' + NP_TH + '">opcode</th><th style="' + NP_TH + '">len</th>'
      + '<th style="' + NP_TH + '">payload</th></tr></thead>'
      + '<tbody id="np-body">' + npLiveRows(flt) + '</tbody></table></div>';
  }
  function npLiveRows(flt) {
    if (!npRecords.length) {
      const msg = (npMeta && npMeta.ok === false) ? 'Companion feed not available (see above).'
        : 'Waiting for inbound packets (move around or open interfaces to generate traffic)';
      return '<tr><td colspan="5" style="padding:14px;color:var(--text-dim,#999)">' + htmlEsc(msg) + '</td></tr>';
    }
    const t0 = npRecords.length ? npRecords[npRecords.length - 1].t : 0;
    let rows = '', shown = 0;
    for (let i = npRecords.length - 1; i >= 0 && shown < 600; i--) {
      const r = npRecords[i];
      if (!npMatch(r.op, flt)) continue;
      shown++;
      const nm = npName(r.op);
      const opCell = '<span style="color:var(--accent,#7aa2ff)">' + npHex2(r.op) + '</span>'
        + (nm ? ' <span style="color:var(--text,#ddd)">' + htmlEsc(nm) + '</span>' : '');
      rows += '<tr>'
        + '<td style="' + NP_TD + ';color:var(--text-dim,#777)">' + r.seq + '</td>'
        + '<td style="' + NP_TD + ';color:var(--text-dim,#777)">-' + Math.max(0, t0 - r.t) + '</td>'
        + '<td style="' + NP_TD + ';overflow-wrap:anywhere">' + opCell + '</td>'
        + '<td style="' + NP_TD + ';color:var(--text-dim,#999)">' + r.len + '</td>'
        + '<td style="' + NP_TD + ';white-space:pre-wrap;overflow-wrap:anywhere;word-break:break-all;color:#cdd6a5">' + npPayload(r) + '</td>'
        + '</tr>';
    }
    return rows || '<tr><td colspan="5" style="padding:14px;color:var(--text-dim,#999)">No packets match the filter.</td></tr>';
  }

  function npProtocolHtml(flt) {
    return '<div style="flex:1;min-height:0;overflow:auto;border:1px solid var(--border,#26262c);border-radius:4px">'
      + '<table style="width:100%;border-collapse:collapse;font:12px/1.4 ui-monospace,Consolas,monospace">'
      + '<thead><tr>'
      + '<th style="' + NP_TH + '">opcode</th><th style="' + NP_TH + '">name</th>'
      + '<th style="' + NP_TH + '">length</th><th style="' + NP_TH + '">handler</th>'
      + '<th style="' + NP_TH + '">count</th><th style="' + NP_TH + '">bytes</th></tr></thead>'
      + '<tbody id="np-body">' + npProtocolRows(flt) + '</tbody></table></div>';
  }
  function npProtocolRows(flt) {
    if (!npEnum) {
      const msg = npEnumErr ? ('Opcode table: ' + npEnumErr) : 'Reading the opcode table from the client';
      return '<tr><td colspan="6" style="padding:14px;color:var(--text-dim,#999)">' + htmlEsc(msg) + '</td></tr>';
    }
    const ops = Object.keys(npEnum).map(Number).sort((a, b) => a - b);
    let rows = '';
    for (const op of ops) {
      if (!npMatch(op, flt)) continue;
      const e = npEnum[op];
      const cnt = npCounts[op] || 0;
      const nm = npName(op);
      rows += '<tr' + (cnt ? ' style="background:rgba(90,130,240,0.06)"' : '') + '>'
        + '<td style="' + NP_TD + '"><span style="color:var(--accent,#7aa2ff)">' + npHex2(op) + '</span>'
        + ' <span style="color:var(--text-dim,#777)">' + op + '</span></td>'
        + '<td style="' + NP_TD + '"><span onclick="npRename(' + op + ')" title="click to name" '
        + 'style="cursor:pointer;color:' + (nm ? 'var(--text,#ddd)' : 'var(--text-dim,#666)') + '">'
        + htmlEsc(nm || '-') + '</span></td>'
        + '<td style="' + NP_TD + ';color:var(--text-dim,#999)">' + npKind(e.len) + '</td>'
        + '<td style="' + NP_TD + ';color:var(--text-dim,#777)">' + htmlEsc(e.handler || '') + '</td>'
        + '<td style="' + NP_TD + '">' + (cnt || '') + '</td>'
        + '<td style="' + NP_TD + ';color:var(--text-dim,#999)">' + (npBytes[op] || '') + '</td>'
        + '</tr>';
    }
    return rows || '<tr><td colspan="6" style="padding:14px;color:var(--text-dim,#999)">No opcodes match the filter.</td></tr>';
  }

Object.assign(window, { npRename });
registerTab({ id: 'netprobe', render: renderNetProbe, open: function () { try { npArm(true); npFetch(); } catch (e) {} }, close: function () { try { npArm(false); } catch (e) {} } });
})();
