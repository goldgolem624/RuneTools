  const TOAST_MAX = 4;
  const toasts = [];               // { el, msg, until, sticky, n, barEl, countEl }
  let _toastTimer = 0;
  // w and h are the alert card's size: the placement box is one card. h = 0 keeps the card's natural height.
  const TOAST_DEF = { anchor: 'top-center', dx: 0, dy: 58, w: 420, h: 0 };
  let toastCfg = Object.assign({}, TOAST_DEF);
  let toastPlacing = false;        // UI Settings "position on screen" mode

  function applyToastPos() {
    const el = $('toaster');
    if (!el) return;
    const c = toastCfg, a = String(c.anchor || 'top-center');
    const vw = window.innerWidth || 1280, vh = window.innerHeight || 720;
    const w = Math.max(220, Math.min(Number(c.w) || TOAST_DEF.w, Math.max(220, vw - 20)));
    const dx = Number(c.dx) || 0, dy = Number(c.dy) || 0;
    el.style.width = w + 'px';
    el.style.top = el.style.bottom = el.style.left = el.style.right = 'auto';
    el.style.transform = 'none';
    if (a.indexOf('bottom') === 0) el.style.bottom = Math.max(0, dy) + 'px';
    else el.style.top = Math.max(0, dy) + 'px';
    if (a.indexOf('left') > 0) el.style.left = Math.max(0, dx) + 'px';
    else if (a.indexOf('right') > 0) el.style.right = Math.max(0, dx) + 'px';
    else { el.style.left = '50%'; el.style.transform = 'translateX(calc(-50% + ' + dx + 'px))'; }
    el.style.flexDirection = (a.indexOf('bottom') === 0) ? 'column-reverse' : 'column';
    el.classList.toggle('placing', !!toastPlacing);
    const hh = Number(c.h) || 0;
    el.classList.toggle('sized', hh > 0);
    el.style.setProperty('--toast-h', (hh > 0 ? hh : 42) + 'px');
    let g = $('toastGhost');
    if (toastPlacing && !g) {
      g = document.createElement('div');
      g.id = 'toastGhost';
      const lab = document.createElement('span');
      lab.textContent = 'Drag to position alerts';
      g.appendChild(lab);
      // Edge grips: left and right resize the width with the opposite edge held still (whatever the
      // anchor), top and bottom slide the stack vertically. The body of the box moves it freely.
      const grips = [['l', 'Drag to set width'], ['r', 'Drag to set width'], ['t', 'Drag to set height'], ['b', 'Drag to set height'],
                     ['tl', 'Drag to set size'], ['tr', 'Drag to set size'], ['bl', 'Drag to set size'], ['br', 'Drag to set size']];
      for (const [side, title] of grips) {
        const rz = document.createElement('i');
        rz.className = 'ghost-rz ghost-rz-' + side; rz.title = title;
        rz.addEventListener('mousedown', (e) => {
          e.preventDefault(); e.stopPropagation();          // not a move-drag
          const sx = e.clientX, sy = e.clientY;
          // Start from the box as rendered, not from the stored numbers: the first drag of a fresh
          // box (or one clamped to the screen) must grow from the size actually on screen.
          const gr = g.getBoundingClientRect();
          const ow = Math.round(gr.width) || Number(toastCfg.w) || TOAST_DEF.w;
          const oh = Math.round(gr.height) || 42;
          const odx = Number(toastCfg.dx) || 0, ody = Number(toastCfg.dy) || 0;
          const a = String(toastCfg.anchor || 'top-center');
          const left = a.indexOf('left') > 0, right = a.indexOf('right') > 0, bottom = a.indexOf('bottom') === 0;
          const mv = (ev) => {
            const vwNow = window.innerWidth || 1280, vhNow = window.innerHeight || 720;
            const dxm = ev.clientX - sx, dym = ev.clientY - sy;
            const hz = side.indexOf('l') >= 0 ? 'l' : side.indexOf('r') >= 0 ? 'r' : '';   // corners carry both axes
            const vt = side.indexOf('t') >= 0 || side.indexOf('b') >= 0;
            if (hz) {
              // width change, then the offset that keeps the far edge where it was
              let nw = hz === 'r' ? ow + dxm : ow - dxm;
              nw = Math.max(220, Math.min(vwNow - 20, nw));
              const d = nw - ow;                            // effective growth after clamping
              let ndx = odx;
              if (left)       ndx = hz === 'l' ? odx - d : odx;
              else if (right) ndx = hz === 'r' ? odx - d : odx;
              else            ndx = hz === 'l' ? odx - d / 2 : odx + d / 2;
              toastCfg.w = nw; toastCfg.dx = Math.round(ndx);
            }
            if (vt) {
              // height change with the far edge held still: on a top-anchored stack the bottom edge
              // grows downward as is, the top edge grows upward by moving the stack up; mirrored below.
              const topEdge = side.indexOf('t') >= 0;
              let nh = topEdge ? oh - dym : oh + dym;
              nh = Math.max(42, Math.min(vhNow - 40, nh));
              const d = nh - oh;
              let ndy = ody;
              if (!bottom && topEdge) ndy = ody - d;
              if (bottom && !topEdge) ndy = ody - d;
              toastCfg.h = Math.round(nh);
              toastCfg.dy = Math.round(Math.max(0, Math.min(vhNow - 40, ndy)));
            }
            applyToastPos(); reflectUiSettings();
          };
          const up = () => {
            document.removeEventListener('mousemove', mv);
            document.removeEventListener('mouseup', up);
            wmSaveSoon();
          };
          document.addEventListener('mousemove', mv);
          document.addEventListener('mouseup', up);
        });
        g.appendChild(rz);
      }
      el.appendChild(g);
    } else if (!toastPlacing && g) {
      g.parentNode.removeChild(g);
      g = null;
    }
    if (g) g.style.minHeight = (hh > 0 ? hh : 42) + 'px';   // after creation too, so a reopened box shows its saved height
    wmRectsSoon();
  }

  (function toastDragInit() {
    document.addEventListener('mousedown', (e) => {
      if (!toastPlacing) return;
      const el = $('toaster');
      if (!el || !e.target.closest || !e.target.closest('#toaster')) return;
      if (e.target.closest('.ghost-rz')) return;
      e.preventDefault();
      // Pointer deltas from the grab point, applied to the stored offsets: the grabbed spot stays
      // under the cursor whatever the anchor, without depending on how the stack is laid out.
      const r = el.getBoundingClientRect();
      const sx = e.clientX, sy = e.clientY;
      const odx = Number(toastCfg.dx) || 0, ody = Number(toastCfg.dy) || 0;
      const a = String(toastCfg.anchor || 'top-center');
      const signX = a.indexOf('right') > 0 ? -1 : 1, signY = a.indexOf('bottom') === 0 ? -1 : 1;
      const mv = (ev) => {
        const vw = window.innerWidth || 1280, vh = window.innerHeight || 720;
        let ndx = odx + (ev.clientX - sx) * signX, ndy = ody + (ev.clientY - sy) * signY;
        ndy = Math.max(0, Math.min(Math.max(0, vh - r.height), ndy));
        if (a.indexOf('left') > 0 || a.indexOf('right') > 0) ndx = Math.max(0, Math.min(Math.max(0, vw - r.width), ndx));
        else { const half = Math.max(0, (vw - r.width) / 2); ndx = Math.max(-half, Math.min(half, ndx)); }
        toastCfg.dx = Math.round(ndx); toastCfg.dy = Math.round(ndy);
        applyToastPos();
        reflectUiSettings();      // values only: a rebuild here would kill this drag
      };
      const up = () => {
        document.removeEventListener('mousemove', mv);
        document.removeEventListener('mouseup', up);
        wmSaveSoon();
      };
      document.addEventListener('mousemove', mv);
      document.addEventListener('mouseup', up);
    }, true);
  })();

  function toastIcon(sticky) { return '<i class="g g-dot"></i>'; }

  function uiNotify(msg, opts) {
    msg = String(msg == null ? '' : msg).slice(0, 300);
    if (!msg) return;
    opts = opts || {};
    const sticky = !!opts.sticky || opts.ttl === 0;
    const ttl = sticky ? 0 : Math.max(1200, Number(opts.ttl) || Number(uiCfg().toastTtl) || 5000);
    const now = Date.now();
    const dup = toasts.find(t => t.msg === msg && !t.closing);
    if (dup) {
      dup.n++;
      dup.until = ttl ? now + ttl : 0;
      dup.ttl = ttl;
      dup.start = now;
      if (dup.countEl) { dup.countEl.textContent = '×' + dup.n; dup.countEl.style.display = ''; }
      toastTick();
      return dup;
    }
    const el = document.createElement('div');
    el.className = 'toast' + (sticky ? ' sticky' : '');
    const ico = document.createElement('span');
    ico.className = 'toast-ico'; ico.innerHTML = toastIcon(sticky);
    const body = document.createElement('div');
    body.className = 'toast-msg';
    if (opts.title) {
      // structured card: a bold title, an optional detail line and a muted footer; msg stays the dedupe key
      const add = (cls, text) => { if (!text) return; const d = document.createElement('div'); d.className = cls; d.textContent = String(text).slice(0, 160); body.appendChild(d); };
      add('toast-t', opts.title); add('toast-s', opts.sub); add('toast-f', opts.foot);
      if (opts.accent) el.style.borderLeftColor = opts.accent;
    } else body.textContent = msg;
    const cnt = document.createElement('span');
    cnt.className = 'toast-n'; cnt.style.display = 'none';
    el.appendChild(ico); el.appendChild(body); el.appendChild(cnt);
    let barEl = null;
    if (sticky) {
      const x = document.createElement('button');
      x.className = 'toast-x'; x.title = 'Dismiss';
      x.innerHTML = '<i class="g g-x"></i>';
      x.addEventListener('click', () => toastClose(rec));
      el.appendChild(x);
    } else {
      const bar = document.createElement('div'); bar.className = 'toast-bar';
      barEl = document.createElement('i'); bar.appendChild(barEl);
      el.appendChild(bar);
    }
    const rec = { el, msg, sticky, n: 1, ttl, start: now,
                  until: ttl ? now + ttl : 0, barEl, countEl: cnt, closing: false };
    $('toaster').appendChild(el);
    toasts.push(rec);
    while (toasts.filter(t => !t.closing).length > TOAST_MAX) {
      const victim = toasts.find(t => !t.closing && !t.sticky) ||
                     toasts.find(t => !t.closing);
      if (!victim) break;
      toastClose(victim);
    }
    toastTick();
    wmRectsSoon();                 // a sticky card is clickable -> claim its rect
    return rec;
  }

  function toastClose(rec) {
    if (!rec || rec.closing) return;
    rec.closing = true;
    rec.el.classList.add('out');
    setTimeout(() => {
      const i = toasts.indexOf(rec);
      if (i >= 0) toasts.splice(i, 1);
      if (rec.el.parentNode) rec.el.parentNode.removeChild(rec.el);
      wmRectsSoon();
    }, 200);
  }

  function toastTick() {
    const now = Date.now();
    for (const t of toasts.slice()) {
      if (t.closing || t.sticky) continue;
      if (now >= t.until) { toastClose(t); continue; }
      if (t.barEl && t.ttl > 0)
        t.barEl.style.width = Math.max(0, Math.min(100, (t.until - now) / t.ttl * 100)) + '%';
    }
    const live = toasts.some(t => !t.closing && !t.sticky);
    if (live && !_toastTimer) _toastTimer = setInterval(toastTick, 100);
    else if (!live && _toastTimer) { clearInterval(_toastTimer); _toastTimer = 0; }
  }

