// RuneToolsX panel: Events (Developer) -- live view of the event channel (rtxEvents, fed by the
// companion EventShare ring through state.events) and the gameTick channel. This panel is the
// verification instrument for docs/event-channel.md: it must show ~600 ms between ticks and one
// decoded record per game event (xp drop, container change, script run, ...).
// Spliced inline into client.html; IIFE (registerTab; see the RTX registry in core/rtx-registry.js).
(function () {

  const EV_KEEP = 200;
  const evLog = [];                       // newest first, capped at EV_KEEP
  const evCounts = {};                    // kind -> count since page load
  const evTick = { count: 0, last: -1, dts: [], lastAt: 0 };
  let evPaused = false, evDirty = false, evTimer = null;
  const EV_KINDS = ['skill_update', 'container_update', 'runclientscript', 'ge_offer', 'run_energy', 'run_weight', 'ping', 'raw'];
  const EV_DEFAULT_MASK = '0,4,5,43,81,82,92,141';   // mirrors kDefaultMask in companion/EventShare.h
  // What the channel can record, in the words a reader thinks in. `op` is the packet opcode the
  // companion filters on; everything not listed here is simply not captured, which is why the
  // panel states its coverage rather than implying it shows the whole wire.
  // Names for packets we do not decode yet, from docs/server_packets_handler_map.md. A named
  // row beats "raw": it says whether a line is worth reading at all.
  const EV_OPNAMES = {
    0x02: 'unknown (10 bytes, constant)', 0x06: 'iface_prop_i8', 0x1F: 'unknown (10 bytes)',
    0x2D: 'player_info', 0x43: 'unknown (12 bytes, constant)', 0x4E: 'iface_set',
    0x5A: 'npc_info', 0x5F: 'iface_set (short)', 0x6D: 'zone_update',
    0xB4: 'server_tick', 0xBE: 'telemetry_grid', 0xC8: 'unknown (empty)',
  };
  const EV_TYPES = [
    { op: 4,   kind: 'skill_update',     label: 'XP and levels',    note: 'every xp drop' },
    { op: 43,  kind: 'container_update', label: 'Inventory and bank', note: 'slot changes in any container' },
    { op: 82,  kind: 'runclientscript',  label: 'Interface scripts', note: 'buff bar, notices, popups' },
    { op: 5,   kind: 'ge_offer',         label: 'Grand Exchange',   note: 'offer changes', with: [81] },
    { op: 92,  kind: 'run_energy',       label: 'Run energy',       note: '' },
    { op: 0,   kind: 'run_weight',       label: 'Weight',           note: '' },
    { op: 141, kind: 'ping',             label: 'Ping',             note: 'server keepalive' },
  ];

  function evOnEvent(ev) {
    if (!ev || ev.kind === 'gameTick') return;
    const k = ev.kind || 'raw';
    evCounts[k] = (evCounts[k] || 0) + 1;
    if (evPaused) return;
    evLog.unshift(ev);
    if (evLog.length > EV_KEEP) evLog.length = EV_KEEP;
    evDirty = true;
  }
  // 0xB4 is the server's tick boundary (zero length, once per burst, about 600 ms apart in a
  // full capture). It is an exact edge, unlike the polled counter, so measure it separately.
  const evSrvTick = { count: 0, last: 0, dts: [] };
  function evOnServerTick(ev) {
    const now = (typeof performance === 'object' && performance.now) ? performance.now() : Date.now();
    if (evSrvTick.last) { evSrvTick.dts.push(now - evSrvTick.last); if (evSrvTick.dts.length > 20) evSrvTick.dts.shift(); }
    evSrvTick.last = now; evSrvTick.count++;
  }

  function evOnTick(ev) {
    evTick.count++;
    if (ev.dtMs > 0) { evTick.dts.push(ev.dtMs); if (evTick.dts.length > 20) evTick.dts.shift(); }
    evTick.last = ev.tick; evTick.lastAt = Date.now();
    evDirty = true;
  }
  // Subscribe once at load: counters run whether or not the tab is open, like the chat log.
  rtxEvents.on('*', evOnEvent);
  rtxEvents.on('gameTick', evOnTick);
  rtxEvents.on('*', ev => { if (ev && ev.op === 0xB4) evOnServerTick(ev); });

  // Containers seen on this channel. The full list lives in panel_containers; these are the
  // ones that actually appear in packet traffic, so a line reads "Backpack" not "93".
  const EV_CONTAINERS = {
    93: "Backpack", 94: "Worn equipment", 95: "Bank", 96: "Bank (tab)", 530: "Beast of Burden",
    623: "Money pouch", 670: "Cosmetic overrides", 676: "Death reclaim", 787: "Loot log",
    795: "Oddments storage", 890: "GE favourites", 930: "Death overflow",
    523: "GE collect 1", 524: "GE collect 2", 525: "GE collect 3", 526: "GE collect 4",
    527: "GE collect 5", 528: "GE collect 6", 783: "GE collect 7", 784: "GE collect 8",
  };
  function evContainer(id) { return EV_CONTAINERS[id] ? (EV_CONTAINERS[id] + " (" + id + ")") : ("container " + id); }

  // Item and struct names are resolved once each and cached; a miss repaints when it lands.
  const EV_ITEM = new Map(), EV_STRUCT = new Map();
  function evItemName(id) {
    if (!(id > 0)) return "";
    if (EV_ITEM.has(id)) return EV_ITEM.get(id) || "";
    EV_ITEM.set(id, "");
    (async () => {
      const info = await rtxData.call("cache.itemInfo", id);
      if (info && info.name) { EV_ITEM.set(id, info.name); evPaintSoon(); }
    })();
    return "";
  }
  // A buff-bar struct carries only param 2794, and that is a DESCRIPTION, not a title: the cache
  // holds no separate name for these (the other params are sprite ids and thresholds, and
  // following them lands on unrelated structs). Some descriptions lead with the name, as in
  // "Wise - Grants you extra experience" or "Pulse Core - 2% XP Boost", so take that when it is
  // there and otherwise show the opening clause. The full text goes in the row title.
  // Invention perks: dbtable 8 column 1 is the name and column 2 is the id a buff struct
  // carries in param 2802, so a perk buff can be named properly rather than by the first words
  // of its description ("After reaching 100%" is Aftershock).
  let EV_PERKS = null, _evPerksAsked = false;
  function evPerkName(link) {
    if (!(link > 0)) return "";
    if (EV_PERKS) return EV_PERKS[link] || "";
    if (!_evPerksAsked) {
      _evPerksAsked = true;
      (async () => {
        // Our dbRows emits { f, i: { col: [ints] }, s: { col: [strings] } }: the name is string
        // column 1 and the id the buff struct points at is int column 2.
        const rows = await rtxData.call("cache.dbRows", 8);
        const map = {};
        for (const r of (Array.isArray(rows) ? rows : [])) {
          const nm = r && r.s && r.s["1"] && r.s["1"][0];
          const key = r && r.i && r.i["2"] && r.i["2"][0];
          if (typeof nm === "string" && key > 0) map[key] = nm;
        }
        EV_PERKS = map;
        if (Object.keys(map).length) { EV_STRUCT.clear(); evPaintSoon(); }
      })();
    }
    return "";
  }

  // "Sign of the porter VII" is the item; the status is "Sign of the porter". Tiers appear as a
  // trailing roman numeral or digit, so drop that and nothing else.
  function evBaseItemName(nm) {
    return String(nm || "").replace(/\s+(?:[IVXLC]{1,6}|\d{1,2})$/, "").trim();
  }

  function evStructLabel(text) {
    const t = String(text || "").replace(/<[^>]*>/g, "").trim();
    if (!t) return "";
    const dash = t.match(/^([^-]{2,28}) - /);
    if (dash) return dash[1].trim();
    const stop = t.search(/[.,;:]/);
    const head = stop > 2 ? t.slice(0, stop) : t;
    return head.length > 38 ? head.slice(0, 37) + "\u2026" : head;
  }
  function evStructName(id) {
    if (!(id > 0)) return null;
    if (EV_STRUCT.has(id)) return EV_STRUCT.get(id);
    EV_STRUCT.set(id, null);
    (async () => {
      const ps = await rtxData.call("cache.structParams", id);
      const strs = (ps && ps.strs) || {};
      const ints = (ps && ps.ints) || {};
      const t = (strs["2794"] || strs[2794] || "").replace(/<[^>]*>/g, "");
      // 1. an Invention perk names itself in dbtable 8
      let label = evPerkName(ints["2802"] || ints[2802]);
      // 2. a short description IS the name ("Quiver ammo"), and a dashed one leads with it
      if (!label && t && (t.length <= 26 || / - /.test(t))) label = evStructLabel(t);
      // 3. otherwise the source item names it, without its tier
      if (!label) {
        const item = ints["4677"] || ints[4677];
        if (item > 0) {
          const info = await rtxData.call("cache.itemInfo", item);
          if (info && info.name) label = evBaseItemName(info.name);
        }
      }
      // 4. last resort: the opening clause of the description
      if (!label) label = evStructLabel(t);
      if (label) {
        EV_STRUCT.set(id, { label: label, full: t && t.indexOf(label) !== 0 ? (label + ": " + t) : t });
        evPaintSoon();
      }
    })();
    return null;
  }
  // A name that arrives late marks the list dirty; the panel's own 250 ms tick repaints it.
  function evPaintSoon() { evDirty = true; }

  // Scripts whose arguments we understand, so the line says what happened rather than which
  // script ran. 10623(struct, mode) adds or removes one buff bar entry: see the call chain
  // 10624 -> 9101 (tier by level) -> 15426/10625 -> 15428 (find by param 8106).
  function evScriptText(ev) {
    const a = ev.args || [];
    if (ev.script === 10623 && a.length >= 2) {
      const st = evStructName(a[0]);
      ev._full = st ? st.full : "";
      return "buff bar " + (a[1] ? "add" : "remove") + ": " + (st ? st.label : "struct " + a[0]);
    }
    // 1264(title, when, what, extra, where, world, who, fc, link, ticks): the Community Event
    // notice. The script labels each field in exactly this order and skips the empty ones, so
    // the row shows the whole notice rather than a truncated head of it. The trailing int is the
    // countdown in timer ticks that script 1269 uses to blank the notice again.
    if (ev.script === 1264) {
      const strs = a.filter(x => typeof x === "string");
      const ticks = a.find(x => typeof x === "number");
      const F = ["", "When", "What", "", "Where", "World", "Who", "FC", "Link"];
      const parts = [];
      strs.forEach((v, i) => {
        if (!v) return;
        parts.push(F[i] ? (F[i] + ": " + evWire(v)) : evWire(v));
      });
      return "community event: " + parts.join(" | ") + (ticks ? "  (" + ticks + " ticks)" : "");
    }
    return "script " + ev.script + "(" + a.map(x => typeof x === "string" ? JSON.stringify(evWire(x)) : x).join(", ") + ")";
  }

  function evFields(ev) {
    switch (ev.kind) {
      case 'skill_update': return (ev.name || ('skill ' + ev.skill)) + ' Lv' + ev.level + ' xp=' + Number(ev.xp).toLocaleString('en-US');
      case 'container_update': {
        // Every slot, not a head of them: the packet is already bounded by the capture window
        // (the reader marks a cut list [partial]), and a truncated line hides the change the
        // reader is being consulted about. Runs of emptied slots collapse so a bank clear does
        // not print two hundred identical clauses.
        const sl = ev.slots || [];
        const parts = [];
        for (let i = 0; i < sl.length; i++) {
          const x = sl[i];
          if (x.item < 0) {
            let j = i;
            while (j + 1 < sl.length && sl[j + 1].item < 0 && sl[j + 1].slot === sl[j].slot + 1) j++;
            parts.push(j > i ? ('slots ' + x.slot + '-' + sl[j].slot + ' emptied') : ('slot ' + x.slot + ' emptied'));
            i = j;
          } else {
            const nm = evItemName(x.item);
            parts.push('slot ' + x.slot + ' = ' + (nm || ('item ' + x.item)) + ' x' + Number(x.qty).toLocaleString('en-US'));
          }
        }
        return evContainer(ev.container) + ': ' + (parts.join(', ') || '(no slots)') + (ev.partial ? ' [partial]' : '');
      }
      case 'runclientscript': return evScriptText(ev);
      case 'ge_offer': {
        const o = ev.offer || {};
        if (o.item == null) return 'slot ' + ev.slot + ' (offer unreadable) hex ' + (ev.hex || '');
        const kind = o.type === 0 ? 'buy' : o.type === 1 ? 'sell' : ('type ' + o.type);
        const inm = evItemName(o.item);
        return 'slot ' + ev.slot + ' ' + kind + ' ' + (inm || ('item ' + o.item)) + ' x' + o.qty + ' @ ' + Number(o.price).toLocaleString('en-US')
             + ' filled ' + o.filled + '/' + o.qty + ' (' + Number(o.filledValue).toLocaleString('en-US') + ' gp) status ' + o.status;
      }
      case 'run_energy': return 'energy ' + ev.value;
      case 'run_weight': return 'weight ' + ev.value;
      case 'ping': return 'echo ' + ev.a + ' / ' + ev.b;
      default: {
        const nm = EV_OPNAMES[ev.op];
        const body = 'len ' + ev.len + (ev.hex ? ' hex ' + ev.hex : '');
        return nm ? (nm + ': ' + body) : body;
      }
    }
  }
  function evEsc(s) { return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); }
  // Wire text verbatim, with the bytes that do not print shown as what they are. RS strings
  // carry CP-1252 codes the client renders itself: 0xA0 stands in for a space inside a name
  // and 0x92 for an apostrophe, and passing those straight through paints glyphs that look
  // like stray letters. Control codes and the C1 range become <a0> so the row shows the
  // actual content; ordinary Unicode is left alone.
  function evWire(s) {
    return String(s == null ? '' : s).replace(/[\u0000-\u001f\u007f-\u00a0]/g, ch =>
      '<' + ch.charCodeAt(0).toString(16).padStart(2, '0') + '>');
  }
  function evOp(op) { return '0x' + (op < 16 ? '0' : '') + op.toString(16).toUpperCase(); }

  function renderEvents() {
    const c = $('content');
    let wrap = $('evWrap');
    if (!wrap) {
      injectStyle('evCss', `
          .ev-head { display: flex; align-items: center; gap: 8px; padding: 12px 14px 8px; flex-wrap: wrap; }
          .ev-title { flex: 1; font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: .05em; color: var(--accent-hi); }
          .ev-btn { font-size: 11px; padding: 4px 10px; border-radius: 6px; border: 1px solid var(--border); background: var(--bg-elev); color: var(--text); cursor: pointer; white-space: nowrap; flex: none; }
          .ev-btn.on { border-color: var(--accent-hi); color: var(--accent-hi); }
          .ev-card { margin: 0 12px 10px; background: var(--bg-elev); border: 1px solid var(--border); border-radius: 10px; padding: 8px 12px; font-size: 12px; }
          .ev-stats { display: flex; flex-wrap: wrap; gap: 6px 14px; }
          .ev-stat b { color: var(--accent-hi); font-weight: 600; }
          .ev-stat span { color: var(--text-dim); }
          /* wrap: the type toggles are a row of unknown length and must not run off the panel */
          .ev-mask { display: flex; gap: 6px; align-items: center; flex-wrap: wrap; min-width: 0; }
          /* Any text in this panel can be arbitrary game or packet data, so it wraps by default;
             a long unbroken token (a hex payload, a comma separated mask) has nothing to break on
             otherwise and runs off the edge. */
          .ev-card, .ev-card > div { min-width: 0; max-width: 100%; }
          .ev-card, .ev-card * { overflow-wrap: break-word; }
          .ev-mask input { flex: 1; font-size: 11px; padding: 4px 8px; border-radius: 6px; border: 1px solid var(--border); background: var(--bg); color: var(--text); font-family: monospace; }
          .ev-list { margin: 0 12px 12px; font-family: monospace; font-size: 11px; }
          .ev-row { display: flex; gap: 8px; padding: 3px 6px; border-bottom: 1px solid var(--border); align-items: baseline; }
          .ev-row .t { color: var(--text-dim); flex: 0 0 62px; }
          .ev-row .k { flex: 0 0 120px; color: var(--accent-hi); }
          .ev-row .o { flex: 0 0 38px; color: var(--text-dim); }
          /* break-word, not break-all: break-all split 'UTC' across lines mid-word. Long hex
             payloads still wrap because they have no spaces to break on. */
          .ev-row .f { flex: 1; color: var(--text); word-break: break-word; overflow-wrap: anywhere; }
          .ev-row.raw .k { color: var(--text-dim); }
      `);
      wrap = document.createElement('div'); wrap.id = 'evWrap';
      wrap.innerHTML = `
        <div class="ev-head"><div class="ev-title">Event channel</div>
          <button class="ev-btn" id="evCopy">Copy</button>
          <button class="ev-btn" id="evCopyJson">Copy JSON</button>
          <button class="ev-btn" id="evPause">Pause</button>
          <button class="ev-btn" id="evClear">Clear</button></div>
        <div class="ev-card" id="evTickCard"></div>
        <div class="ev-card"><div class="ev-stats" id="evCounts"></div></div>
        <div class="ev-card">
          <div id="evWhat" style="font-size:11px;color:var(--text-dim);margin-bottom:6px"></div>
          <div class="ev-mask" id="evToggles"></div>
          <div class="ev-mask" style="margin-top:6px">
            <button class="ev-btn" id="evMaskAll">Everything</button>
            <button class="ev-btn" id="evMaskDef">Reset to default</button>
            <button class="ev-btn" id="evAdvBtn">Advanced</button>
          </div>
          <div id="evAdv" style="display:none;margin-top:6px">
            <div class="ev-mask"><span>Opcode mask (decimal, csv)</span><input id="evMask" spellcheck="false">
              <button class="ev-btn" id="evMaskSet">Set</button></div>
          </div>
          <div id="evMaskMsg" style="font-size:11px;color:var(--text-dim);margin-top:4px"></div></div>
        <div class="ev-list" id="evList"></div>`;
      c.appendChild(wrap);
      $('evPause').onclick = () => { evPaused = !evPaused; $('evPause').textContent = evPaused ? 'Resume' : 'Pause'; $('evPause').classList.toggle('on', evPaused); };
      $('evClear').onclick = () => { evLog.length = 0; for (const k in evCounts) delete evCounts[k]; evTick.count = 0; evTick.dts.length = 0; evDirty = true; };
      $('evCopy').onclick = () => evCopy(false);
      $('evCopyJson').onclick = () => evCopy(true);
      $('evMask').value = EV_DEFAULT_MASK;
      $('evMaskDef').onclick = () => { $('evMask').value = EV_DEFAULT_MASK; evMaskApply(); evPaintToggles(); };
      // Every opcode, not just the named ones. Undecoded packets arrive as raw hex, which is how
      // a new one gets identified in the first place.
      $('evMaskAll').onclick = () => {
        const all = []; for (let i = 0; i <= 255; i++) all.push(i);
        $('evMask').value = all.join(',');
        evMaskApply(); evPaintToggles();
      };
      $('evMaskSet').onclick = () => { evMaskApply(); evPaintToggles(); };
      $('evAdvBtn').onclick = () => {
        const a = $('evAdv'); const show = a.style.display === 'none';
        a.style.display = show ? '' : 'none'; $('evAdvBtn').classList.toggle('on', show);
      };
      evPaintToggles();
      evDirty = true;
    }
    if (!evDirty) return;
    evDirty = false;
    const P = window.rtxEventsPoll || {};
    const dts = evTick.dts;
    const mean = dts.length ? dts.reduce((a, b) => a + b, 0) / dts.length : 0;
    const mn = dts.length ? Math.min.apply(null, dts) : 0, mx = dts.length ? Math.max.apply(null, dts) : 0;
    $('evTickCard').innerHTML = '<div class="ev-stats">'
      + '<div class="ev-stat"><span>gameTick</span> <b>' + evTick.count + '</b></div>'
      + '<div class="ev-stat"><span>tick</span> <b>' + evTick.last + '</b></div>'
      + '<div class="ev-stat"><span>interval</span> <b>' + (mean ? mean.toFixed(0) + ' ms' : 'n/a') + '</b> <span>(min ' + mn.toFixed(0) + ' / max ' + mx.toFixed(0) + ', last ' + dts.length + ')</span></div>'
      + (evSrvTick.count ? ('<div class="ev-stat"><span>server tick</span> <b>' + evSrvTick.count + '</b> <span>every '
          + (evSrvTick.dts.length ? (evSrvTick.dts.reduce((a, b) => a + b, 0) / evSrvTick.dts.length).toFixed(0) : '?')
          + ' ms</span></div>') : '')
      + '<div class="ev-stat"><span>cursor</span> <b>' + (P.seq || 0) + '</b></div>'
      + '<div class="ev-stat"><span>polls</span> <b>' + (P.polls || 0) + '</b> <span>fails ' + (P.fails || 0) + '</span></div>'
      + '</div>';
    const kinds = EV_KINDS.concat(Object.keys(evCounts).filter(k => EV_KINDS.indexOf(k) === -1));
    $('evCounts').innerHTML = kinds.map(k => '<div class="ev-stat"><span>' + evEsc(k) + '</span> <b>' + (evCounts[k] || 0) + '</b></div>').join('');
    $('evList').innerHTML = evLog.map(ev => {
      // wall is epoch ms low 32 bits: rebuild it relative to now (records are at most minutes old).
      let t = '';
      if (typeof ev.wall === 'number') { let d = (Date.now() % 4294967296) - ev.wall; if (d < 0) d += 4294967296; t = new Date(Date.now() - d).toTimeString().slice(0, 8); }
      const body = evFields(ev);                       // sets ev._full for the rows that have more to say
      const tip = ev._full ? ' data-tip="' + evEsc(ev._full).replace(/"/g, '&quot;') + '"' : '';
      return '<div class="ev-row' + (ev.kind === 'raw' ? ' raw' : '') + '"' + tip + '><span class="t">' + t + '</span><span class="k">' + evEsc(ev.kind || 'raw')
        + '</span><span class="o">' + evOp(ev.op) + '</span><span class="f">' + evEsc(body) + '</span></div>';
    }).join('');
  }

  // Current mask as a set of opcodes.
  function evMaskSet() {
    const out = {};
    for (const p of String(($('evMask') || {}).value || EV_DEFAULT_MASK).split(',')) {
      const n = parseInt(p, 10); if (!isNaN(n)) out[n] = true;
    }
    return out;
  }
  function evPaintToggles() {
    const host = $('evToggles'); if (!host) return;
    const on = evMaskSet();
    const known = {};
    for (const t of EV_TYPES) { known[t.op] = true; for (const o of (t.with || [])) known[o] = true; }
    const extra = Object.keys(on).filter(o => !known[o]);
    host.innerHTML = '';
    for (const t of EV_TYPES) {
      const b = document.createElement('button');
      b.className = 'ev-btn' + (on[t.op] ? ' on' : '');
      b.textContent = t.label;
      b.dataset.tip = t.note ? (t.note + '\n(packet 0x' + t.op.toString(16).toUpperCase().padStart(2, '0') + ')')
                             : ('packet 0x' + t.op.toString(16).toUpperCase().padStart(2, '0'));
      b.onclick = () => {
        const cur = evMaskSet();
        const ops = [t.op].concat(t.with || []);
        const want = !cur[t.op];
        for (const o of ops) { if (want) cur[o] = true; else delete cur[o]; }
        $('evMask').value = Object.keys(cur).map(Number).sort((a, b2) => a - b2).join(',');
        evMaskApply(); evPaintToggles();
      };
      host.appendChild(b);
    }
    const n = EV_TYPES.filter(t => on[t.op]).length;
    const total = Object.keys(on).length;
    // Be explicit that the named types are a selection, not the wire: the game sends far more.
    $('evWhat').textContent = total >= 250
      ? ('Recording every packet type (' + total + '). Types without a decoder arrive as raw hex. '
         + 'This is a lot of traffic; Reset to default when you are done.')
      : ('Recording ' + n + ' of ' + EV_TYPES.length + ' named event types'
         + (extra.length ? ' and ' + extra.length + ' extra opcode' + (extra.length === 1 ? '' : 's') : '')
         + ', out of 256 the game can send. Anything not switched on is not captured at all, and '
         + 'chat has its own channel. Use Everything to see the rest as raw hex.');
    $('evMaskAll').classList.toggle('on', total >= 250);
  }

  // Oldest first, so a pasted log reads in the order things happened.
  function evCopyText() {
    const head = 'RuneToolsX event log  ' + new Date().toISOString()
      + '  client ' + (window.rtxEventsPoll ? window.rtxEventsPoll.pid : '?')
      + '  mask ' + (($('evMask') || {}).value || '')
      + '  ' + evLog.length + ' events' + nlChar;
    const rows = evLog.slice().reverse().map(ev => {
      let t = '';
      if (typeof ev.wall === 'number') { let d = (Date.now() % 4294967296) - ev.wall; if (d < 0) d += 4294967296; t = new Date(Date.now() - d).toTimeString().slice(0, 8); }
      return [t, evOp(ev.op), ev.kind || 'raw', evFields(ev)].join('\t');
    });
    return head + rows.join(nlChar);
  }
  const nlChar = String.fromCharCode(10);
  async function evCopy(asJson) {
    let text;
    if (asJson) {
      const out = evLog.slice().reverse().map(ev => {
        const o = {}; for (const k in ev) if (k.charAt(0) !== '_') o[k] = ev[k];
        return o;
      });
      text = JSON.stringify({ at: new Date().toISOString(), pid: window.rtxEventsPoll ? window.rtxEventsPoll.pid : 0,
                              mask: (($('evMask') || {}).value || ''), events: out }, null, 1);
    } else {
      text = evCopyText();
    }
    const cap = 65536, over = text.length > cap;
    if (over) text = text.slice(0, cap - 80) + nlChar + '... trimmed at 65536 characters (' + evLog.length + ' events captured)';
    const ok = await rtxData.call('clipboard.copy', text);
    const msg = $('evMaskMsg');
    if (msg) msg.textContent = (ok === null || ok === false)
      ? 'Copy failed: the host has no clipboard binding.'
      : ('Copied ' + evLog.length + ' events' + (asJson ? ' as JSON' : '') + (over ? ', trimmed to fit the clipboard limit' : '') + '.');
  }

  async function evMaskApply() {
    const csv = $('evMask').value;
    const ok = await rtxData.call('host.eventsMask', csv);
    const n = csv.split(',').filter(x => x.trim() !== '').length;
    // Never print the raw list: 256 comma separated numbers have no spaces to wrap on and run
    // off the panel. Say how many, and name them only when the list is short enough to read.
    const what = n >= 250 ? 'every packet type (' + n + ')'
               : n > 12   ? (n + ' packet types')
               : ('packets ' + csv);
    $('evMaskMsg').textContent = ok ? ('Applied: ' + what + '. Chat is never recorded here.')
                                    : 'Could not apply: the companion is not loaded in this client.';
  }

  function evOpen() { evDirty = true; if (!evTimer) evTimer = setInterval(() => { try { paneRun('events', renderEvents); } catch (e) {} }, 250); }
  function evClose() { if (evTimer) { clearInterval(evTimer); evTimer = null; } }

registerTab({ id: 'events', render: renderEvents, open: evOpen, close: evClose });
})();
