  // ---- rtxConsole (core/rtx-console.js): the client-wide log bus behind the Console panel ----
  // One ring of log entries fed by the page's own console.* calls, uncaught errors, the launcher's
  // launcher.log (tailed by the panel), and every plugin, HTML or Lua, through the console.* methods
  // of PLUGIN_API. Plugins can only write: the host stamps the source and plugin id, clamps every
  // field, rate limits each plugin, and the panel renders text nodes only. Nothing here evaluates
  // or re-emits a line anywhere a plugin could read it back.
  const rtxConsole = (function () {
    const MAX = 5000, TEXT_MAX = 4000, TAG_MAX = 24;
    const LEVELS = { debug: 0, info: 1, warn: 2, error: 3 };
    const entries = [];
    const listeners = [];
    let seq = 0, dropped = 0;
    const buckets = new Map();      // plugin id -> token bucket for plugin lines

    function clampTag(t) {
      t = String(t == null ? '' : t).slice(0, TAG_MAX);
      return t.replace(/[^A-Za-z0-9_.:-]/g, '_');
    }
    function fmt(args) {
      const out = [];
      for (let i = 0; i < args.length; i++) {
        const a = args[i];
        if (typeof a === 'string') out.push(a);
        else if (a instanceof Error) out.push(a.stack || a.message || String(a));
        else if (a === undefined) out.push('undefined');
        else { try { out.push(JSON.stringify(a)); } catch (e) { out.push(String(a)); } }
      }
      return out.join(' ');
    }
    function push(e) {
      const level = (e && LEVELS[e.level] !== undefined) ? e.level : 'info';
      let text = String(e && e.text != null ? e.text : '');
      if (text.length > TEXT_MAX) text = text.slice(0, TEXT_MAX) + ' [+' + (text.length - TEXT_MAX) + ' chars]';
      const rec = {
        seq: ++seq, t: Date.now(), level,
        source: String((e && e.source) || 'client').slice(0, 16),
        plugin: (e && e.plugin) ? String(e.plugin).slice(0, 80) : '',
        runtime: (e && e.runtime) ? String(e.runtime).slice(0, 8) : '',
        tag: clampTag(e && e.tag), text,
      };
      entries.push(rec);
      if (entries.length > MAX) { entries.splice(0, entries.length - MAX); dropped++; }
      for (const f of listeners.slice()) { try { f(rec); } catch (x) {} }
      return rec;
    }
    // Plugin lines: 60 per second per plugin with a burst of 120; past that, lines are dropped and
    // one notice per 5 s says so. The plugin id comes from the mount, never from the plugin.
    function pushPlugin(id, runtime, level, text, tag) {
      const key = String(id || '');
      if (!key) return false;
      const now = Date.now();
      let b = buckets.get(key);
      if (!b) { b = { tokens: 120, at: now, notice: 0 }; buckets.set(key, b); }
      b.tokens = Math.min(120, b.tokens + (now - b.at) * 0.06); b.at = now;
      if (b.tokens < 1) {
        if (now - b.notice > 5000) { b.notice = now; push({ level: 'warn', source: 'plugin', plugin: key, runtime, tag: 'host', text: 'console rate limit reached: lines dropped (60 per second)' }); }
        return false;
      }
      b.tokens -= 1;
      push({ level, source: 'plugin', plugin: key, runtime, tag, text });
      return true;
    }
    function on(fn) { if (typeof fn === 'function') listeners.push(fn); }
    function off(fn) { const i = listeners.indexOf(fn); if (i !== -1) listeners.splice(i, 1); }
    function clear() { entries.length = 0; dropped = 0; }
    function all() { return entries; }
    function format(rec) {
      const d = new Date(rec.t);
      const p = n => String(n).padStart(2, '0');
      const ts = p(d.getHours()) + ':' + p(d.getMinutes()) + ':' + p(d.getSeconds()) + '.' + String(d.getMilliseconds()).padStart(3, '0');
      const src = rec.source === 'plugin' ? ('plugin ' + rec.plugin + (rec.runtime ? ' (' + rec.runtime + ')' : '')) : rec.source;
      return '[' + ts + '] ' + rec.level.toUpperCase().padEnd(5) + ' ' + src + (rec.tag ? ' [' + rec.tag + ']' : '') + ' ' + rec.text;
    }

    // Capture the page's own console: the originals still run (dev tools, launcher log capture).
    const orig = {};
    for (const m of ['debug', 'log', 'info', 'warn', 'error']) {
      orig[m] = (typeof console[m] === 'function') ? console[m].bind(console) : null;
      console[m] = function () {
        try { if (orig[m]) orig[m].apply(null, arguments); } catch (e) {}
        try { push({ level: m === 'log' ? 'info' : m, source: 'client', text: fmt(arguments) }); } catch (e) {}
      };
    }
    try {
      window.addEventListener('unhandledrejection', ev => {
        const r = ev && ev.reason;
        push({ level: 'error', source: 'client', tag: 'promise', text: 'unhandled rejection: ' + (r && (r.stack || r.message) || String(r)) });
      });
    } catch (e) {}

    return { push, pushPlugin, on, off, clear, all, format, fmt, LEVELS, dropped: () => dropped };
  })();
