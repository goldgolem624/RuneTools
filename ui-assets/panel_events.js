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

  function evFields(ev) {
    switch (ev.kind) {
      case 'skill_update': return (ev.name || ('skill ' + ev.skill)) + ' Lv' + ev.level + ' xp=' + Number(ev.xp).toLocaleString('en-US');
      case 'container_update': {
        const s = (ev.slots || []).slice(0, 8).map(x => '#' + x.slot + (x.item < 0 ? ' empty' : ' item ' + x.item + ' x' + x.qty)).join(', ');
        return 'container ' + ev.container + ' flags ' + ev.flags + ': ' + (s || '(no slots)') + ((ev.slots || []).length > 8 ? ', ...' : '') + (ev.partial ? ' [partial]' : '');
      }
      case 'runclientscript': return 'script ' + ev.script + '(' + (ev.args || []).map(a => typeof a === 'string' ? JSON.stringify(a) : a).join(', ') + ')';
      case 'ge_offer': {
        const o = ev.offer || {};
        if (o.item == null) return 'slot ' + ev.slot + ' (offer unreadable) hex ' + (ev.hex || '');
        const kind = o.type === 0 ? 'buy' : o.type === 1 ? 'sell' : ('type ' + o.type);
        return 'slot ' + ev.slot + ' ' + kind + ' item ' + o.item + ' x' + o.qty + ' @ ' + Number(o.price).toLocaleString('en-US')
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
