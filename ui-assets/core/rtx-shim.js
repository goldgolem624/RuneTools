  window.onerror = function (msg, src, line, col) {
    try { console.error('uncaught: ' + msg + ' @' + line + ':' + col); } catch (e) {}
  };
  // localStorage SHIM. Without a WebCore cache_path (launcher builds before 2026-07-30)
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
