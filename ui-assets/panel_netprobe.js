// RuneToolsX panel: Server Packets (server -> client protocol + live inbound feed).
(function () {

  let npEnum = null;                 // {op -> {len, kind, handler}} from serverPackets()
  let npEnumTried = false, npEnumErr = '';
  let npRecords = [];
  let npCounts = {}, npBytes = {};
  let npCursor = 0;
  let npMeta = null;
  let npPaused = false, npView = 'live', npAscii = false;
  let npFilter = '';                 // hex/dec opcode, "0x10-0x20" range, or name substring
  let npAt = 0;
  const NP_CAP = 5000;
  // the seed is 950-1. Opcodes not in it are unnamed until re-derived (see docs/cs2_opcodes.md).
  const SOPS = { message_game: 0x21, skill_update: 0x5C, container_update: 0x32, runclientscript: 0x23,
                 ge_offer: 0x54, run_energy: 0x15, run_weight: 0x07, ping_echo: 0xBE, server_tick: 0xA0,
                 varp_int: 0x04, varp_byte: 0x4F, varc_int: 0x77, varc_byte: 0x7E, varp_long: 0xA5, varbit_varint: 0x74 };
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
  // RS3 inv-definition container ids seen in 0x2B updates.
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
  const npDecodersByName = {
    // 950-1: [skill: -b0][level: -b1][xp: u32 BE] -> skill block +0x7618  (949 was [xp LE][level b4+0x80][skill -b5])
    skill_update: function (b) {
      if (b.length < 6) return '';
      const sk = (256 - b[0]) & 0xFF, level = (256 - b[1]) & 0xFF;
      const xp = (((b[2] << 24) | (b[3] << 16) | (b[4] << 8) | b[5]) >>> 0);
      return (NP_SKILLS[sk] || ('skill ' + sk)) + ' Lv' + level + ' xp=' + xp.toLocaleString('en-US');
    },
    // 0x06: [value: -b0 signed][compId16: b1<<8 | (b2+0x80)] -> property store mgr +0x19888.
    iface_prop_i8: function (b) {
      if (b.length < 3) return '';
      const v = -(b[0] << 24 >> 24);
      const comp = (b[1] << 8) | ((b[2] + 0x80) & 0xFF);
      return 'ns-comp ' + comp + ' = ' + v;
    },
    // 0x2B: [containerId:u16 BE][flags:u8] then per changed slot: [slot: smart 1B<0x80 else
    // 2B(+0x8000)][itemId+1: 3B BE][qty: u8, or 0xFF then u32 BE][+1 variant byte if flags&2].
    container_update: function (b) {
      if (b.length < 3) return '';
      let p = 0;
      const cont = (b[p] << 8) | b[p + 1]; p += 2;
      const flags = b[p++];
      const slots = [];
      while (p < b.length && slots.length < 32) {
        let slot;
        if (b[p] < 0x80) { slot = b[p]; p += 1; }
        else { slot = (((b[p] << 8) | b[p + 1]) + 0x8000) & 0xFFFF; p += 2; }
        if (p + 3 > b.length) break;
        const item1 = (b[p] << 16) | (b[p + 1] << 8) | b[p + 2]; p += 3;
        if (item1 === 0) { slots.push('#' + slot + ' empty'); continue; }
        if (p >= b.length) break;
        let qty = b[p++];
        if (qty === 0xFF) { if (p + 4 > b.length) break; qty = ((b[p] << 24) | (b[p + 1] << 16) | (b[p + 2] << 8) | b[p + 3]) >>> 0; p += 4; }
        if (flags & 2) { if (p < b.length) p++; }    // per-slot variant/charge byte
        slots.push('#' + slot + ' ' + npItemName(item1 - 1) + ' ×' + qty.toLocaleString('en-US'));
      }
      return npContainerName(cont) + ': ' + (slots.join(', ') || '(no slots)');
    },
    // 0x15: [type: smart 1B if <0x80 else 2B BE + 0x8000][u32 uid][flags:1]; flags&1 adds a NUL
    // sender string (flags&2 a second one), then the NUL text. type 109 = game, 138 = broadcast.
    message_game: function (b) {
      if (b.length < 6) return '';
      let p, type;
      if (b[0] < 0x80) { type = b[0]; p = 1; }
      else { type = (((b[0] << 8) | b[1]) + 0x8000) & 0xFFFF; p = 2; }
      p += 4;                                    // u32 field
      if (p >= b.length) return '';
      const flags = b[p++];
      const readStr = function () { let s = ''; while (p < b.length && b[p] !== 0) s += String.fromCharCode(b[p++]); p++; return s; };
      let name = '';
      if (flags & 1) { name = readStr(); if (flags & 2) readStr(); }
      const text = readStr();
      return 'type ' + type + (name ? ' <' + name + '>' : '') + ' “' + text + '”';
    },
    // 0x1C: [plane:u8][zoneX:i8][zoneY: u8-0x80]; zone coords are index*8 tiles off the scene base.
    update_zone: function (b) {
      if (b.length < 3) return '';
      const plane = b[0], zx = (b[1] << 24 >> 24), zy = b[2] - 0x80;
      return 'plane ' + plane + ' zone x=' + zx + ' y=' + zy;
    },
    // 0x4E: the component id maps to a varp id, the value is the new varp value; value byte order is 1,0,3,2.
    iface_set: function (b) {
      if (b.length < 6) return '';
      const comp = (b[4] << 8) | b[5];
      const val = ((b[1] << 24) | (b[0] << 16) | (b[3] << 8) | b[2]) >>> 0;
      return npVarLabel(comp) + ' = ' + npVarVal(comp, val);
    },
    rebuild_scene_dyn: function (b) {
      if (b.length < 14 || b[2] !== 5) return '';
      const bx = ((b[8] << 8) | b[9]), by = ((b[10] << 8) | b[11]);
      const A = b[12], B = b[13];
      let bit = 14 * 8; const total = b.length * 8;
      const rd = function (n) { let v = 0; while (n--) { v = v * 2 + ((b[bit >> 3] >> (7 - (bit & 7))) & 1); bit++; } return v; };
      const rooms = {}; let refs = 0, trunc = false;
      let rxLo = 1e9, rxHi = -1, ryLo = 1e9, ryHi = -1;
      for (let pl = 0; pl < 4 && !trunc; pl++) {
        for (let i = 0; i < A && !trunc; i++) {
          for (let j = 0; j < B; j++) {
            if (bit + 1 > total) { trunc = true; break; }
            if (!rd(1)) continue;
            if (bit + 26 > total) { trunc = true; break; }
            const r = rd(26); refs++;
            const zx = (r >> 14) & 0x3FF, zy = (r >> 3) & 0x7FF;
            const rx = zx >> 3, ry = zy >> 3;
            if (rx < rxLo) rxLo = rx; if (rx > rxHi) rxHi = rx;
            if (ry < ryLo) ryLo = ry; if (ry > ryHi) ryHi = ry;
            if (pl === 0) rooms[(i >> 1) + ',' + (j >> 1)] = 1;
          }
        }
      }
      const nRooms = Object.keys(rooms).length;
      return 'dyn map t' + b[0] + ' ' + A + 'x' + B + ' zones base(' + bx * 8 + ',' + by * 8 + ') · '
        + nRooms + ' room' + (nRooms === 1 ? '' : 's') + ' / ' + refs + ' refs'
        + (refs ? ' · tpl rx' + rxLo + (rxHi !== rxLo ? '-' + rxHi : '')
                + ' ry' + ryLo + (ryHi !== ryLo ? '-' + ryHi : '') : '')
        + (trunc ? ' · TRUNCATED at ' + b.length + 'B (raise kSnip in NetProbeShare.h)' : '');
    },
    runclientscript: function (b) {
      let p = 0, sig = '';
      while (p < b.length && b[p] !== 0 && sig.length < 16) sig += String.fromCharCode(b[p++]);
      if (!sig || p >= b.length || !/^[is]+$/.test(sig)) return '';
      p++;
      const wire = [];
      for (let i = sig.length - 1; i >= 0; i--) {
        if (sig[i] === 's') {
          let s = '';
          while (p < b.length && b[p] !== 0) s += String.fromCharCode(b[p++]);
          if (p >= b.length) return 'sig ' + sig + ' (truncated)';
          p++; wire.push('“' + s + '”');
        } else {
          if (p + 4 > b.length) return 'sig ' + sig + ' (truncated)';
          const v = (b[p] << 24) | (b[p + 1] << 16) | (b[p + 2] << 8) | b[p + 3]; p += 4;
          wire.push(String(v));
        }
      }
      if (p + 4 > b.length) return 'sig ' + sig + ' (truncated)';
      const script = ((b[p] << 24) | (b[p + 1] << 16) | (b[p + 2] << 8) | b[p + 3]) >>> 0;
      return 'script ' + script + '(' + wire.reverse().join(', ') + ')';
    },
    zone_update: function (b) {
      if (b.length < 3) return '';
      const FIXED = { 0: 10, 3: 7, 4: 20, 5: 21, 6: 11, 7: 3, 8: 2, 9: 5, 10: 14, 11: 8,
        12: 7, 13: 6, 14: 7, 15: 11, 16: 29, 17: 4, 18: 5, 19: 28, 20: 8 };
      const neg = function (v) { return (256 - v) & 0xFF; };
      const zi = function (v) { return ((-0x80 - v) << 24 >> 24); };   // zone index off scene base
      const out = ['plane ' + neg(b[0]) + ' zone(' + zi(b[1]) + ',' + zi(b[2]) + ')'];
      let p = 3;
      while (p < b.length && out.length < 12) {
        const op = b[p++];
        if (op === 1) {
          if (p + 6 > b.length) { out.push('loc+ (truncated)'); break; }
          const sr = (b[p] + 0x80) & 0xFF;
          const id = (b[p + 1] | (b[p + 2] << 8) | (b[p + 3] << 16) | (b[p + 4] << 24)) >>> 0;
          const t = neg(b[p + 5]); p += 6;
          out.push('loc+ ' + id + ' @(' + ((t >> 4) & 7) + ',' + (t & 7) + ') s' + ((sr >> 2) & 0x1f) + 'r' + (sr & 3));
          if (sr & 0x80) { out.push('…ext'); break; }
        } else if (op === 3) {
          if (p + 7 > b.length) { out.push('anim (truncated)'); break; }
          const anim = ((b[p + 4] << 24) | (b[p + 5] << 16) | (b[p + 2] << 8) | b[p + 3]) >>> 0;
          const sr = (b[p + 6] + 0x80) & 0xFF, t = b[p + 1]; p += 7;
          out.push('anim ' + anim + ' @(' + ((t >> 4) & 7) + ',' + (t & 7) + ') s' + ((sr >> 2) & 0x1f) + 'r' + (sr & 3));
          if (sr & 0x80) { out.push('…ext'); break; }
        } else if (op === 8) {
          if (p + 2 > b.length) { out.push('del (truncated)'); break; }
          const t = neg(b[p]), sr = (b[p + 1] + 0x80) & 0xFF; p += 2;
          out.push('del @(' + ((t >> 4) & 7) + ',' + (t & 7) + ') s' + ((sr >> 2) & 0x1f) + 'r' + (sr & 3));
          if (sr & 0x80) { out.push('…ext'); break; }
        } else if (FIXED[op] !== undefined) {
          out.push('sub' + op + '(+' + FIXED[op] + 'B)'); p += FIXED[op];
        } else {
          out.push('sub' + op + '(var) …'); break;
        }
      }
      return out.join(' | ');
    },
    iface_set_short: function (b) {
      if (b.length < 3) return '';
      const comp = (b[2] << 8) | ((b[1] + 0x80) & 0xFF);
      return npVarLabel(comp) + ' = ' + (b[0] << 24 >> 24);
    },
    iface_prop_bool: function (b) {
      if (b.length < 5) return '';
      const key = (((b[1] << 24) | (b[0] << 16) | (b[3] << 8) | b[2]) >>> 0);
      return 'comp ' + (key >>> 16) + ':' + (key & 0xFFFF) + ' = ' + (b[4] === 0x81 ? 1 : 0);
    },
    telemetry_cell_clear: function (b) {                                // 0x9F: clear one cell
      if (b.length < 3) return '';
      return '[' + b[2] + ',' + ((256 - b[0]) & 0xFF) + ',' + ((b[1] + 0x80) & 0xFF) + '] = none';
    },
    telemetry_edit: function (b) {                                // 0xC7: edit
      if (b.length < 3) return '';
      return 'group ' + ((256 - b[2]) & 0xFF) + ' edit(' + ((-0x80 - b[0]) << 24 >> 24)
        + ', ' + ((b[1] - 0x80) << 24 >> 24) + ')';
    },
    telemetry_value: function (b) {                                // 0xDC: cell value
      if (b.length < 6) return '';
      const v = ((b[4] << 24) | (b[5] << 16) | (b[2] << 8) | b[3]) >>> 0;
      return 'group ' + b[0] + ' idx ' + ((-0x80 - b[1]) << 24 >> 24) + ' value ' + v.toLocaleString('en-US');
    },
    telemetry_reindex: function (b) {                                // 0xDF: edit + reindex rows
      if (b.length < 3) return '';
      return 'group ' + ((0x80 - b[1]) & 0xFF) + ' reindex(' + ((256 - b[0]) & 0xFF)
        + ', ' + ((256 - b[2]) & 0xFF) + ')';
    },
    telemetry_row_slot: function (b) {                                // 0xE0: row-order slot
      if (b.length < 3) return '';
      const idx = (256 - b[0]) & 0xFF;
      return 'group ' + ((256 - b[2]) & 0xFF) + ' slot ' + idx + ' = ' + (b[1] === 0x7F ? idx : -1);
    },
    ping_echo: function (b) {
      if (b.length < 8) return '';
      const a = ((b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]) >>> 0;
      const c = ((b[4] << 24) | (b[5] << 16) | (b[6] << 8) | b[7]) >>> 0;
      return 'echo ' + a + ' / ' + c + ' (client replies 9B)';
    },
    iface_set_10: function (b) {                                // 0x9E: comp:u16(BE)@0 + two u32
      if (b.length < 10) return '';
      const comp = (b[0] << 8) | b[1];
      const a = ((b[3] << 24) | (b[2] << 16) | (b[5] << 8) | b[4]) >>> 0;
      const c = ((b[7] << 24) | (b[6] << 16) | (b[9] << 8) | b[8]) >>> 0;
      return npVarLabel(comp) + ' = (' + a + ', ' + c + ')';
    },
    telemetry_grid: function (b) {
      let p = 0; const out = [];
      while (p < b.length) {
        const i = b[p++]; if (i === 0xFF) break;
        while (p < b.length) {
          const j = b[p++]; if (j === 0xFF) break;
          while (p < b.length) {
            const k = b[p++]; if (k === 0xFF) break;
            if (p + 4 > b.length) return out.join('  ');
            const v = ((b[p] << 24) | (b[p + 1] << 16) | (b[p + 2] << 8) | b[p + 3]) >>> 0; p += 4;
            out.push('[' + i + ',' + j + ',' + k + ']=' + v.toLocaleString('en-US'));
          }
        }
      }
      return out.join('  ');
    },
  };
  const NP_VARNAMES = {
    5984: { name: 'invention_charge', fmt: function (v) { return Math.floor(v / 3000).toLocaleString('en-US') + ' charge (raw ' + v.toLocaleString('en-US') + ')'; } }
  };
  function npVarLabel(id) { const e = NP_VARNAMES[id]; return e ? (e.name + '(' + id + ')') : ('varp ' + id); }
  function npVarVal(id, v) { const e = NP_VARNAMES[id]; return (e && e.fmt) ? e.fmt(v) : v.toLocaleString('en-US'); }
  function npPayload(r) {
    if (!r.kept) return r.len === 0 ? '(empty)' : '';
    const body = npAscii ? npAsciiOf(r.hex) : npHexSpaced(r.hex);
    const more = (r.len > r.kept) ? ' …(+' + (r.len - r.kept) + 'B)' : '';
    const dec = npDecoders[r.op] ? npDecoders[r.op](npParseBytes(r)) : '';
    const hint = dec ? ('<span style="color:#7ec699">' + htmlEsc(dec) + '</span>  ') : npNumHint(r);
    return hint + htmlEsc(body) + more;
  }

  function npHealthHtml() {
    if (!npMeta) return '<span style="color:var(--text-dim)">waiting…</span>';
    if (npMeta.ok === false) {
      return '<span style="color:#e6a23c">' + htmlEsc(npMeta.reason || 'feed unavailable')
           + '</span> <span style="color:var(--text-dim)">— build the companion (VS Release|x64) then relaunch the client</span>';
    }
    const hooked = (npMeta.flags & 1) ? 'hooked' : 'NOT hooked';
    const hookCol = (npMeta.flags & 1) ? 'var(--good, #67c23a)' : '#f56c6c';
    const d = npMeta.diag || [0, 0, 0, 0];
    return '<span style="color:' + hookCol + '">framer ' + hooked + '</span>'
      + ' <span style="color:var(--text-dim)">rva ' + htmlEsc(npMeta.framerRva || '0x0')
      + ' · seen ' + (npMeta.seen | 0) + ' · written ' + (npMeta.written | 0)
      + ' · calls ' + (d[0] | 0) + ' · rec ' + (d[1] | 0)
      + ' · skip(range ' + (d[2] | 0) + '/dup ' + (d[3] | 0) + ')</span>';
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
      html += npBtn('np-pause', npPaused ? '▶ Resume' : '⏸ Pause');
      html += npBtn('np-clear', 'Clear');
      html += npBtn('np-ascii', npAscii ? 'Hex' : 'ASCII');
    }
    html += '<input id="np-filter" placeholder="filter opcode: 0x5C, 92, 0x10-0x20, name…" '
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
        : 'Waiting for inbound packets… (move around / open interfaces to generate traffic)';
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
      const msg = npEnumErr ? ('Opcode table: ' + npEnumErr) : 'Reading opcode table from the client…';
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
        + htmlEsc(nm || '—') + '</span></td>'
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
