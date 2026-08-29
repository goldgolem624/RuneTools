// rtx-shim.js: window.onerror (first statement on purpose) and the localStorage shim for builds where Ultralight storage is a no-op.
// Loads after: nothing: this is the first script the page runs.
// Plain script, page globals by design: every top-level name here is a page global that panels and the other core files use.
  // First statement on purpose: uncaught errors anywhere in the panel (including
  // later top-level statements) reach the client log via the console listener.
  window.onerror = function (msg, src, line, col) {
    try { console.error('uncaught: ' + msg + ' @' + line + ':' + col); } catch (e) {}
  };
  // localStorage SHIM. Without a WebCore cache_path (launcher builds before 2026-07-30)
  // Ultralight's localStorage is a SILENT NO-OP: setItem "succeeds" but getItem always
  // returns null, so every panel's "persisted" state (alert bells, crew snapshots, ports
  // plan/gear, var pins, notes) silently evaporated between repaints. Probe it once and,
  // when broken, replace it with an in-memory store - session-lifetime persistence, which
  // is what the panels actually rely on within a run. Remove-safe once every deployed
  // launcher sets config.cache_path (main.cpp).
  (function () {
    try {
      localStorage.setItem('__rtxProbe', '1');
      if (localStorage.getItem('__rtxProbe') === '1') { localStorage.removeItem('__rtxProbe'); return; }
    } catch (e) {}
    const mem = {};
    const shim = {
      getItem: k => (Object.prototype.hasOwnProperty.call(mem, k) ? mem[k] : null),
      setItem: (k, v) => { mem[String(k)] = String(v); },
      removeItem: k => { delete mem[k]; },
      clear: () => { for (const k in mem) delete mem[k]; },
      key: i => Object.keys(mem)[i] || null,
      get length() { return Object.keys(mem).length; },
    };
    try { Object.defineProperty(window, 'localStorage', { value: shim, configurable: true }); } catch (e) {}
    try { console.error('localStorage is a no-op in this build - using the in-memory shim'); } catch (e) {}
  })();
