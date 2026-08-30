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

  function evOnEvent(ev) {
    if (!ev || ev.kind === 'gameTick') return;
    const k = ev.kind || 'raw';
    evCounts[k] = (evCounts[k] || 0) + 1;
    if (evPaused) return;
    evLog.unshift(ev);
    if (evLog.length > EV_KEEP) evLog.length = EV_KEEP;
    evDirty = true;
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
  // A buff-bar struct describes itself in param 2794 (see script 10623 in the docs).
  function evStructName(id) {
    if (!(id > 0)) return "";
    if (EV_STRUCT.has(id)) return EV_STRUCT.get(id) || "";
    EV_STRUCT.set(id, "");
    (async () => {
      const ps = await rtxData.call("cache.structParams", id);
      const strs = (ps && ps.strs) || {};
      const t = strs["2794"] || strs[2794];
      if (typeof t === "string" && t) {
        EV_STRUCT.set(id, t.length > 46 ? (t.slice(0, 45) + "\u2026") : t);
        evPaintSoon();
      }
    })();
    return "";
  }
  // A name that arrives late marks the list dirty; the panel's own 250 ms tick repaints it.
  function evPaintSoon() { evDirty = true; }

  // Scripts whose arguments we understand, so the line says what happened rather than which
  // script ran. 10623(struct, mode) adds or removes one buff bar entry: see the call chain
  // 10624 -> 9101 (tier by level) -> 15426/10625 -> 15428 (find by param 8106).
  function evScriptText(ev) {
    const a = ev.args || [];
    if (ev.script === 10623 && a.length >= 2) {
      const nm = evStructName(a[0]);
      return "buff bar " + (a[1] ? "add" : "remove") + ": " + (nm || ("struct " + a[0]));
    }
    // 1264(title, when, what, extra, where, world, who, fc, link, ticks): the Community Event
    // notice. It assembles the blurb, writes it into 1234:11 or 1465:36 depending on which
    // layout is active, and hands 1269 a countdown that blanks it again.
    if (ev.script === 1264 && a.length >= 3) {
      const txt = a.filter(x => typeof x === "string" && x.length);
      return "community event notice: " + txt.slice(0, 3).join(" | ") + (txt.length > 3 ? " ..." : "");
    }
    return "script " + ev.script + "(" + a.map(x => typeof x === "string" ? JSON.stringify(x) : x).join(", ") + ")";
  }

  function evFields(ev) {
    switch (ev.kind) {
      case 'skill_update': return (ev.name || ('skill ' + ev.skill)) + ' Lv' + ev.level + ' xp=' + Number(ev.xp).toLocaleString('en-US');
      case 'container_update': {
        const s = (ev.slots || []).slice(0, 8).map(x => {
          if (x.item < 0) return 'slot ' + x.slot + ' emptied';
          const nm = evItemName(x.item);
          return 'slot ' + x.slot + ' = ' + (nm ? nm : ('item ' + x.item)) + ' x' + Number(x.qty).toLocaleString('en-US');
        }).join(', ');
        return evContainer(ev.container) + ': ' + (s || '(no slots)') + ((ev.slots || []).length > 8 ? ', ...' : '') + (ev.partial ? ' [partial]' : '');
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
      default: return 'len ' + ev.len + ' hex ' + (ev.hex || '');
    }
  }
  function evEsc(s) { return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); }
  function evOp(op) { return '0x' + (op < 16 ? '0' : '') + op.toString(16).toUpperCase(); }

  function renderEvents() {
    const c = $('content');
    let wrap = $('evWrap');
    if (!wrap) {
      injectStyle('evCss', `
          .ev-head { display: flex; align-items: center; gap: 8px; padding: 12px 14px 8px; flex-wrap: wrap; }
          .ev-title { flex: 1; font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: .05em; color: var(--accent-hi); }
          .ev-btn { font-size: 11px; padding: 4px 10px; border-radius: 6px; border: 1px solid var(--border); background: var(--bg-elev); color: var(--text); cursor: pointer; }
          .ev-btn.on { border-color: var(--accent-hi); color: var(--accent-hi); }
          .ev-card { margin: 0 12px 10px; background: var(--bg-elev); border: 1px solid var(--border); border-radius: 10px; padding: 8px 12px; font-size: 12px; }
          .ev-stats { display: flex; flex-wrap: wrap; gap: 6px 14px; }
          .ev-stat b { color: var(--accent-hi); font-weight: 600; }
          .ev-stat span { color: var(--text-dim); }
          .ev-mask { display: flex; gap: 6px; align-items: center; }
          .ev-mask input { flex: 1; font-size: 11px; padding: 4px 8px; border-radius: 6px; border: 1px solid var(--border); background: var(--bg); color: var(--text); font-family: monospace; }
          .ev-list { margin: 0 12px 12px; font-family: monospace; font-size: 11px; }
          .ev-row { display: flex; gap: 8px; padding: 3px 6px; border-bottom: 1px solid var(--border); align-items: baseline; }
          .ev-row .t { color: var(--text-dim); flex: 0 0 62px; }
          .ev-row .k { flex: 0 0 120px; color: var(--accent-hi); }
          .ev-row .o { flex: 0 0 38px; color: var(--text-dim); }
          .ev-row .f { flex: 1; color: var(--text); word-break: break-all; }
          .ev-row.raw .k { color: var(--text-dim); }
      `);
      wrap = document.createElement('div'); wrap.id = 'evWrap';
      wrap.innerHTML = `
        <div class="ev-head"><div class="ev-title">Event channel</div>
          <button class="ev-btn" id="evPause">Pause</button>
          <button class="ev-btn" id="evClear">Clear</button></div>
        <div class="ev-card" id="evTickCard"></div>
        <div class="ev-card"><div class="ev-stats" id="evCounts"></div></div>
        <div class="ev-card"><div class="ev-mask">
          <span>Opcode mask (decimal, csv)</span><input id="evMask" spellcheck="false">
          <button class="ev-btn" id="evMaskSet">Set</button>
          <button class="ev-btn" id="evMaskDef">Default</button></div>
          <div id="evMaskMsg" style="font-size:11px;color:var(--text-dim);margin-top:4px"></div></div>
        <div class="ev-list" id="evList"></div>`;
      c.appendChild(wrap);
      $('evPause').onclick = () => { evPaused = !evPaused; $('evPause').textContent = evPaused ? 'Resume' : 'Pause'; $('evPause').classList.toggle('on', evPaused); };
      $('evClear').onclick = () => { evLog.length = 0; for (const k in evCounts) delete evCounts[k]; evTick.count = 0; evTick.dts.length = 0; evDirty = true; };
      $('evMask').value = EV_DEFAULT_MASK;
      $('evMaskDef').onclick = () => { $('evMask').value = EV_DEFAULT_MASK; evMaskApply(); };
      $('evMaskSet').onclick = evMaskApply;
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
      + '<div class="ev-stat"><span>cursor</span> <b>' + (P.seq || 0) + '</b></div>'
      + '<div class="ev-stat"><span>polls</span> <b>' + (P.polls || 0) + '</b> <span>fails ' + (P.fails || 0) + '</span></div>'
      + '</div>';
    const kinds = EV_KINDS.concat(Object.keys(evCounts).filter(k => EV_KINDS.indexOf(k) === -1));
    $('evCounts').innerHTML = kinds.map(k => '<div class="ev-stat"><span>' + evEsc(k) + '</span> <b>' + (evCounts[k] || 0) + '</b></div>').join('');
    $('evList').innerHTML = evLog.map(ev => {
      // wall is epoch ms low 32 bits: rebuild it relative to now (records are at most minutes old).
      let t = '';
      if (typeof ev.wall === 'number') { let d = (Date.now() % 4294967296) - ev.wall; if (d < 0) d += 4294967296; t = new Date(Date.now() - d).toTimeString().slice(0, 8); }
      return '<div class="ev-row' + (ev.kind === 'raw' ? ' raw' : '') + '"><span class="t">' + t + '</span><span class="k">' + evEsc(ev.kind || 'raw')
        + '</span><span class="o">' + evOp(ev.op) + '</span><span class="f">' + evEsc(evFields(ev)) + '</span></div>';
    }).join('');
  }

  async function evMaskApply() {
    const csv = $('evMask').value;
    const ok = await rtxData.call('host.eventsMask', csv);
    $('evMaskMsg').textContent = ok ? 'Mask written: ' + csv + ' (0x15 is never recorded)' : 'Mask write failed: companion not loaded?';
  }

  function evOpen() { evDirty = true; if (!evTimer) evTimer = setInterval(() => { try { paneRun('events', renderEvents); } catch (e) {} }, 250); }
  function evClose() { if (evTimer) { clearInterval(evTimer); evTimer = null; } }

registerTab({ id: 'events', render: renderEvents, open: evOpen, close: evClose });
})();
