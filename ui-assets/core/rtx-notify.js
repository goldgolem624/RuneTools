// rtx-notify.js: In-game notifications: toast queue, applyToastPos, toast drag placement, uiNotify, toastClose, toastTick.
// Loads after: rtx-ui.js (uiCfg) and rtx-wm.js (wm state) at call time; toastDragInit installs document listeners at load.
// Plain script, page globals by design: every top-level name here is a page global that panels and the other core files use.
  // ==================== In-game notifications ====================
  // Replaces the companion's GL-primitive notification cards (marker command list,
  // fixed-cell ASCII atlas). Same delivery contract as before -- a message and a
  // time-to-live, 0 = sticky until dismissed -- but rendered in the UI layer, so
  // it gets real type, wrapping and the window design language.
  const TOAST_MAX = 4;
  const toasts = [];               // { el, msg, until, sticky, n, barEl, countEl }
  let _toastTimer = 0;
  // Placement, persisted with the layout (per account). Anchor + offset rather than a
  // raw x/y so the stack keeps its corner when the game window is resized.
  const TOAST_DEF = { anchor: 'top-center', dx: 0, dy: 58, w: 420 };
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
    // Vertical edge
    if (a.indexOf('bottom') === 0) el.style.bottom = Math.max(0, dy) + 'px';
    else el.style.top = Math.max(0, dy) + 'px';
    // Horizontal edge; centre keeps its own translate so dx nudges from the middle.
    if (a.indexOf('left') > 0) el.style.left = Math.max(0, dx) + 'px';
    else if (a.indexOf('right') > 0) el.style.right = Math.max(0, dx) + 'px';
    else { el.style.left = '50%'; el.style.transform = 'translateX(calc(-50% + ' + dx + 'px))'; }
    // Newest-first at the top edge, newest-nearest-the-edge at the bottom.
    el.style.flexDirection = (a.indexOf('bottom') === 0) ? 'column-reverse' : 'column';
    el.classList.toggle('placing', !!toastPlacing);
    // The placement frame is a real element so it can carry a width grip.
    let g = $('toastGhost');
    if (toastPlacing && !g) {
      g = document.createElement('div');
      g.id = 'toastGhost';
      const lab = document.createElement('span');
      lab.textContent = 'Drag to position alerts';
      const rz = document.createElement('i');
      rz.className = 'ghost-rz'; rz.title = 'Drag to set width';
      rz.addEventListener('mousedown', (e) => {
        e.preventDefault(); e.stopPropagation();          // not a move-drag
        const sx = e.clientX, ow = Number(toastCfg.w) || TOAST_DEF.w;
        const a = String(toastCfg.anchor || 'top-center');
        // Right-anchored stacks grow leftwards, so the grip reads inverted there.
        const dir = (a.indexOf('right') > 0) ? -1 : 1;
        const mv = (ev) => {
          const vwNow = window.innerWidth || 1280;
          toastCfg.w = Math.max(220, Math.min(vwNow - 20, ow + (ev.clientX - sx) * dir));
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
      g.appendChild(lab); g.appendChild(rz);
      el.appendChild(g);
    } else if (!toastPlacing && g) {
      g.parentNode.removeChild(g);
    }
    wmRectsSoon();
  }

  // Drag the stack itself while in placement mode; converts the drop point back into
  // an offset from whichever edge the current anchor uses.
  (function toastDragInit() {
    document.addEventListener('mousedown', (e) => {
      if (!toastPlacing) return;
      const el = $('toaster');
      if (!el || !e.target.closest || !e.target.closest('#toaster')) return;
      // The width grip is inside the stack, and THIS listener is on the capture
      // phase, so it would otherwise start a move-drag before the grip ever sees
      // the press and stopPropagation could not save it.
      if (e.target.closest('.ghost-rz')) return;
      e.preventDefault();
      const r = el.getBoundingClientRect();
      const ox = e.clientX - r.left, oy = e.clientY - r.top;
      const mv = (ev) => {
        const vw = window.innerWidth || 1280, vh = window.innerHeight || 720;
        const left = Math.max(0, Math.min(vw - r.width, ev.clientX - ox));
        const top = Math.max(0, Math.min(vh - r.height, ev.clientY - oy));
        const a = String(toastCfg.anchor || 'top-center');
        toastCfg.dy = Math.round((a.indexOf('bottom') === 0) ? (vh - top - r.height) : top);
        toastCfg.dx = Math.round(a.indexOf('left') > 0 ? left
                              : a.indexOf('right') > 0 ? (vw - left - r.width)
                              : (left + r.width / 2 - vw / 2));
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

  // A colour-coded dot rather than a drawn icon: amber = needs dismissing, accent =
  // it will clear itself. Reads instantly and cannot mis-render.
  function toastIcon(sticky) { return '<i class="g g-dot"></i>'; }

  // msg: text. opts: { ttl: ms (0/omitted-with-sticky = until dismissed), sticky: bool }
  function uiNotify(msg, opts) {
    msg = String(msg == null ? '' : msg).slice(0, 300);
    if (!msg) return;
    opts = opts || {};
    const sticky = !!opts.sticky || opts.ttl === 0;
    const ttl = sticky ? 0 : Math.max(1200, Number(opts.ttl) || Number(uiCfg().toastTtl) || 5000);
    const now = Date.now();
    // Repeat of a message already on screen: bump its count and restart its clock
    // instead of stacking duplicates (alert rules can re-fire quickly).
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
    body.className = 'toast-msg'; body.textContent = msg;
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
    // Cap the stack: retire the oldest NON-sticky first so a dismissible warning
    // is never silently pushed out by routine chatter.
    while (toasts.filter(t => !t.closing).length > TOAST_MAX) {
      const victim = toasts.find(t => !t.closing && !t.sticky) ||
                     toasts.find(t => !t.closing);
      if (!victim) break;
      toastClose(victim);
    }
    toastTick();
    wmRectsSoon();                 // a sticky card is clickable -> claim its rect
    // The record is returned so callers (pinned league tasks) can live-update a
    // sticky card's text in place instead of stacking new toasts.
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

  // Drives expiry + the remaining-time rule. Runs ONLY while toasts are on screen.
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

