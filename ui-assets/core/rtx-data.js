// rtx-data.js: rtxData, the panels' single data path over the plugin broker (PLUGIN_API in rtx-plugins.js).
// Loads after: rtx-plugins.js (PLUGIN_API), rtx-bridge.js (bridge, bridgeJson, myPid). Nothing runs at load.
// Plain script, page globals by design: rtxData is the only name this file publishes.
//
// Why: docs/panel-plugin-migration.md. Core panels are reference UIs over the broker, so a panel
// asks for 'state.varps' the same way a marketplace plugin does, and the broker table is the one
// place that knows which host binding serves it. Panels are trusted (same page, same origin), so
// this path applies no scope grant and no rate bucket; it does apply the broker's json rule.
//
//   rtxData.call(method, ...args)  -> parsed value, null on any failure (bridgeJson rules: JSON
//                                     error, {"ok":false,"why"} envelope, no client, unknown
//                                     binding). json:false methods resolve to run()'s own result.
//   rtxData.raw(method, ...args)   -> whatever run() returned, unparsed. Throws (rejects) exactly
//                                     where bridge().name(...) would have, so
//                                     JSON.parse(await rtxData.raw(...)) is a drop-in for
//                                     JSON.parse(await bridge().name(myPid(), ...)).
//   rtxData.sync(method, ...args)  -> run()'s result, synchronously, no coalescing. The host
//                                     bindings are synchronous, and many panel sites depend on
//                                     that (JSON.parse(bridge().x(..)), draw-then-flag sequences),
//                                     so this is the same call routed through the broker table.
//   rtxData.bindingOf(method)      -> the host binding a broker method's run() calls, or null.
//
// Coalescer (state.* and cache.* only): within one 100 ms window (a refresh() pass is 250 ms)
// identical (method, args) calls share one bridge round-trip, and the id-list reads (state.varps,
// state.varbitsCsv, state.varpsLong) issued in the same turn are merged into ONE bridge call whose
// answer is split back per caller. The host's served() cache keys on the id list, and ReadAsync
// builds a cold key inline, so a merged list returns real data on its first request too.
// Overlay, sound and actuator methods are never coalesced: every call reaches the host.
const rtxData = (function () {
  const HOT = /^(state|cache)\./;
  const WINDOW_MS = 100;
  // method -> host binding for the merged id-list reads. These call the binding directly with the
  // merged CSV instead of run(): the per-plugin clamp on run() (pClampStr 200) is a plugin
  // boundary limit and panel lists are longer; each caller still only sees its own ids.
  const BATCH = { 'state.varps': 'varps', 'state.varbitsCsv': 'varbits', 'state.varpsLong': 'varpsLong' };
  const memo = new Map();       // key -> { t, p }: shared in-flight/just-finished promise per (method, args)
  const batches = new Map();    // method -> [{ ids, csv, resolve, reject }] awaiting the flush
  const bindCache = new Map();  // method -> binding name (from run()'s source text, computed once)
  const _errAt = new Map();

  function bindingOf(method) {
    if (bindCache.has(method)) return bindCache.get(method);
    const def = PLUGIN_API[method];
    let b = null;
    if (def && typeof def.run === 'function') {
      const m = /bridge\(\)\.(\w+)\s*\(/.exec(String(def.run));
      if (m) b = m[1];
    }
    bindCache.set(method, b);
    return b;
  }

  function memoKey(method, args) {
    if (!HOT.test(method)) return null;
    let a = '';
    try { a = JSON.stringify(args); } catch (e) { return null; }
    return method + ' ' + a;
  }

  // Merged id-list read. The reader parses digit runs out of the CSV, so the split here mirrors
  // that: an id the caller asked for appears in its answer only when the host answered it.
  function batched(method, args) {
    const csv = String(args[0] == null ? '' : args[0]);
    const ids = csv.split(/[^0-9]+/).filter(Boolean);
    return new Promise(function (resolve, reject) {
      let q = batches.get(method);
      if (!q) { q = []; batches.set(method, q); setTimeout(function () { flush(method); }, 0); }
      q.push({ ids: ids, csv: csv, resolve: resolve, reject: reject });
    });
  }
  async function flush(method) {
    const q = batches.get(method); batches.delete(method);
    if (!q || !q.length) return;
    const binding = BATCH[method];
    if (q.length === 1) {   // nothing to merge: the caller's own CSV goes through untouched
      const it = q[0];
      try { it.resolve(await bridge()[binding](myPid(), it.csv)); } catch (e) { it.reject(e); }
      return;
    }
    const all = new Set();
    for (const it of q) for (const id of it.ids) all.add(id);
    const merged = Array.from(all).map(Number).sort(function (a, b) { return a - b; }).join(',');
    let text;
    try { text = await bridge()[binding](myPid(), merged); }
    catch (e) { for (const it of q) it.reject(e); return; }
    let obj = null;
    try { obj = JSON.parse(text); } catch (e) {}
    // Empty default ("{}" when the pid is not tracked) or a failure envelope: every caller sees
    // the host's own text, exactly as its own call would have.
    if (!obj || typeof obj !== 'object' || Array.isArray(obj) || obj.ok === false || !Object.keys(obj).length) {
      for (const it of q) it.resolve(text); return;
    }
    for (const it of q) {
      const sub = {};
      for (const id of it.ids) if (Object.prototype.hasOwnProperty.call(obj, id)) sub[id] = obj[id];
      it.resolve(JSON.stringify(sub));
    }
  }

  function invoke(method, args) {
    if (BATCH[method]) return batched(method, args);
    return PLUGIN_API[method].run(args, myPid());
  }

  function raw(method) {
    const args = Array.prototype.slice.call(arguments, 1);
    const def = PLUGIN_API[method];
    if (!def) return Promise.reject(new Error('rtxData: unknown method ' + method));
    if (!bridge()) return Promise.reject(new TypeError('rtxData: no bridge for ' + method));
    const key = memoKey(method, args), now = Date.now();
    if (key) { const m = memo.get(key); if (m && now - m.t < WINDOW_MS) return m.p; }
    let p;
    try { p = Promise.resolve(invoke(method, args)); }   // run() itself executes synchronously here
    catch (e) { p = Promise.reject(e); }
    if (key) {
      memo.set(key, { t: now, p: p });
      p.catch(function () { if (memo.get(key) && memo.get(key).p === p) memo.delete(key); });
      if (memo.size > 512) for (const [k, v] of memo) if (now - v.t >= WINDOW_MS) memo.delete(k);
    }
    return p;
  }

  function sync(method) {
    const args = Array.prototype.slice.call(arguments, 1);
    const def = PLUGIN_API[method];
    if (!def) throw new Error('rtxData: unknown method ' + method);
    if (BATCH[method]) return bridge()[BATCH[method]](myPid(), ...args);   // unclamped, like flush()
    return def.run(args, myPid());
  }

  async function call(method) {
    const args = Array.prototype.slice.call(arguments, 1);
    const def = PLUGIN_API[method];
    if (!def) {
      const now = Date.now();
      if (now - (_errAt.get(method) || 0) >= 30000) { _errAt.set(method, now); console.error('rtxData: unknown method ' + method); }
      return null;
    }
    const binding = bindingOf(method) || method;
    const b = bridge();
    if (!b || (binding !== method && typeof b[binding] !== 'function')) {
      bridgeJson.lastWhy = b ? 'unsupported' : 'noclient';
      bridgeJson.whyByMethod[binding] = bridgeJson.lastWhy;
      return null;
    }
    try {
      const v = await raw.apply(null, [method].concat(args));
      if (def.json === true) return bridgeJson.parse(binding, v);
      if (def.json === 'maybe') return v ? bridgeJson.parse(binding, v) : null;
      return v;
    } catch (e) { return bridgeJson.fail(binding, e); }
  }

  return { call: call, raw: raw, sync: sync, bindingOf: bindingOf };
})();

// rtxEvents: the in-page event bus fed by the 100 ms poll loop in rtx-boot.js (rtxEventsPoll).
// Kinds: skill_update, container_update, runclientscript, ge_offer, run_energy, run_weight,
// ping, raw (undocumented opcode: {op,len,hex}) and gameTick ({tick, dtMs}). '*' hears every
// kind. Listeners run synchronously in emit(); a throwing listener never stops the others.
const rtxEvents = (function () {
  const subs = new Map();
  function on(kind, fn) { if (typeof fn !== 'function') return; let a = subs.get(kind); if (!a) { a = []; subs.set(kind, a); } a.push(fn); }
  function off(kind, fn) { const a = subs.get(kind); if (!a) return; const i = a.indexOf(fn); if (i !== -1) a.splice(i, 1); }
  function emit(kind, ev) {
    const a = subs.get(kind), b = subs.get('*');
    if (a) for (const f of a.slice()) { try { f(ev); } catch (e) {} }
    if (b) for (const f of b.slice()) { try { f(ev); } catch (e) {} }
  }
  return { on: on, off: off, emit: emit };
})();

