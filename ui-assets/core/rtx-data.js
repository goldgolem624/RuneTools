// Coalescer (state.* and cache.* only): within one 200 ms window (a refresh() pass is 250 ms,
// so two panels reading the same thing in one pass share a single bridge call)
const rtxData = (function () {
  const HOT = /^(state|cache)\./;
  const WINDOW_MS = 200;
  // merged CSV instead of run(): the per-plugin clamp on run() (pClampStr 200) is a plugin
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

  // Timing reads are never coalesced: the metronome interpolates from the age it gets back and a
  // memoised answer would make it jump.
  const NOMEMO = { 'state.gameTickState': 1, 'state.gameTick': 1 };
  function memoKey(method, args) {
    if (!HOT.test(method) || NOMEMO[method]) return null;
    let a = '';
    try { a = JSON.stringify(args); } catch (e) { return null; }
    return method + ' ' + a;
  }

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

