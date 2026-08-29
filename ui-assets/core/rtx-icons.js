// rtx-icons.js: Item icon + info caches (ICONS/INFO), icon-grid tooltips, showTipFor/hideTip, attachInfo, attachIcon, geOfferAlert, fmtStatus.
// Loads after: rtx-bridge.js (bridge/bridgeJson at call time); installs document mouseover/mouseout listeners at load.
// Plain script, page globals by design: every top-level name here is a page global that panels and the other core files use.
  const ICONS = new Map();
  const ICON_PENDING = new Map();
  const INFO = new Map();           // id -> {name, ge_limit, value}
  const INFO_PENDING = new Set();

  const tipEl = (() => {
    const el = document.createElement('div');
    el.id = 'global-tip';
    document.body.appendChild(el);
    return el;
  })();

  // Jagex markup support for tooltips, OPT-IN via data-tip-html="1" on the same node.
  // Game strings (ability descriptions etc.) carry <br> and <col=RRGGBB>..</col>.
  // The whole string is HTML-ESCAPED FIRST and only those two (already escaped) tags
  // are translated back -- so a panel can never inject arbitrary markup through a tip.
  function tipHtml(text) {
    return text
      .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;')
      .replace(/&lt;br&gt;/gi, '<br>')
      .replace(/&lt;col=([0-9a-fA-F]{6})&gt;/g, '<span style="color:#$1">')
      .replace(/&lt;\/col&gt;/gi, '</span>');
  }
  // ---- icon-grid tooltips (collection logs) ----
  // A panel registers STRUCTURED data, never markup: [{label, cls, items:[{id,name}]}]. The
  // cells are built below with createElement/textContent/setIconBg, so the no-panel-markup
  // guarantee documented on tipHtml() is untouched -- item names now land in textContent
  // instead of being concatenated into a tip string, which is strictly tighter than before.
  const TIP_GRIDS = new WeakMap();   // row node -> groups[]
  function setTipGrids(node, groups) {
    if (groups && groups.length) TIP_GRIDS.set(node, groups); else TIP_GRIDS.delete(node);
  }
  // The bubble is pointer-events:none and therefore unscrollable, and overflow:hidden would
  // eat the tail SILENTLY. So budget the cells to what actually fits and say what was elided.
  // Capped at six rows regardless of screen height: a 55-item log is a reference list, not
  // something to read at a glance, and a tooltip spanning the whole display is unusable even
  // when it technically fits. The remainder is reported as "+N more".
  const TIP_GRID_COLS = 9;                  // 356px inner width / (34px cell + 4px gap)
  function tipGridBudget() {
    const rows = Math.floor((window.innerHeight - 170) / 38);
    return Math.max(TIP_GRID_COLS, Math.min(TIP_GRID_COLS * 6, rows * TIP_GRID_COLS));
  }
  function buildTipGrids(groups) {
    const body = document.createElement('div');
    body.className = 'tg-body';
    const live = groups.filter(g => g && g.items && g.items.length);
    const total = live.reduce((n, g) => n + g.items.length, 0);
    const budget = tipGridBudget();
    for (const g of live) {
      // Split the budget in proportion to each group's size, so the longer list (usually
      // Missing - the one you act on) keeps the bigger share without a hardcoded 20/12 rule.
      const cap = total <= budget ? g.items.length
                                  : Math.max(6, Math.round(budget * g.items.length / total));
      const h = document.createElement('div');
      h.className = 'tg-h' + (g.cls ? ' ' + g.cls : '');
      h.textContent = g.label + ' (' + g.items.length + ')';
      body.appendChild(h);
      const grid = document.createElement('div');
      grid.className = 'tg-grid';
      for (const it of g.items.slice(0, cap)) {
        const cell = document.createElement('div');
        cell.className = 'tg-cell' + (g.cls ? ' ' + g.cls : '');
        const url = (typeof resolveIcon === 'function') ? resolveIcon(it.id) : '';
        if (url) setIconBg(cell, url);
        else {
          const ini = document.createElement('span');
          ini.className = 'tg-tn';
          ini.textContent = String(it.name || '?').slice(0, 2).toUpperCase();
          cell.appendChild(ini);
        }
        grid.appendChild(cell);
      }
      body.appendChild(grid);
      if (g.items.length > cap) {
        const more = document.createElement('div');
        more.className = 'tg-more';
        more.textContent = '+ ' + (g.items.length - cap) + ' more';
        body.appendChild(more);
      }
    }
    return body;
  }

  function uiTipText(text) {
    // Preferences "compact tooltips": drop the Source:/ID provenance lines that only a
    // developer or a sceptic wants, keep everything the player reads.
    if (!text || !uiCfg().compactTips) return text;
    // Drop provenance lines (Source/ID), explanatory lines (Shown because / Note / Tip / Why /
    // Hint / Because), and parenthetical asides inside a line that still says something
    // without them. The first line (the name) is always kept.
    const lines = String(text).split('\n');
    const out = [];
    lines.forEach((l, i) => {
      if (i > 0 && /^\s*(Source|Sources|ID|Note|Tip|Hint|Why|Because|Shown because|Diagnostic|Debug)\b/i.test(l)) return;
      if (i > 0) {
        const stripped = l.replace(/\s*\([^()]*\)/g, '').replace(/\s{2,}/g, ' ').trimEnd();
        if (stripped.replace(/[\s:;,.-]/g, '').length) l = stripped;
      }
      out.push(l);
    });
    return out.join('\n').replace(/\n{3,}/g, '\n\n');
  }
  function showTipFor(node) {
    const text = uiTipText(node.dataset.tip);
    const grids = TIP_GRIDS.get(node);
    if (!text && !grids) { hideTip(); return; }
    if (grids) {
      tipEl.classList.add('tg');
      tipEl.textContent = text || '';            // head line stays plain text
      tipEl.appendChild(buildTipGrids(grids));
    } else {
      tipEl.classList.remove('tg');
      if (node.dataset.tipHtml === '1') tipEl.innerHTML = tipHtml(text);
      else tipEl.textContent = text;
    }
    tipEl.style.opacity = '1';
    const r = node.getBoundingClientRect();
    // MEASURE AT THE ORIGIN FIRST. #global-tip is position:fixed with only `left` set, so its
    // shrink-to-fit width is capped at (viewport width - left). Measuring while it still sat at the
    // PREVIOUS position leaves a tooltip near the right edge only a sliver of room: it wraps into a
    // tall narrow column and reports that squeezed offsetWidth, which then feeds the clamp below.
    // Parking it at left:0 gives the full viewport to shrink-to-fit against, so the measurement is
    // the bubble's NATURAL width (bounded by its own max-width).
    tipEl.style.width = '';        // natural size for the measurement
    tipEl.style.left  = '0px';
    tipEl.style.top   = '0px';
    const tipW = tipEl.offsetWidth  || 0;
    const tipH = tipEl.offsetHeight || 24;
    // PIN the measured width before moving it. Otherwise the final `left` re-applies the same
    // shrink-to-fit cap and the bubble collapses again on the right-hand side -- parking at the
    // origin alone only fixes the measurement, not the render.
    tipEl.style.boxSizing = 'border-box';
    const margin = 6;
    // A panel narrower than the bubble would make minCx > maxCx below, and since the max test runs
    // last the bubble would be shoved off the LEFT edge. Cap the width to the available space
    // instead so the clamp always has a valid range.
    const avail = Math.max(40, window.innerWidth - 2 * margin);
    const useW = Math.min(tipW, avail);
    tipEl.style.width = useW + 'px';

    // Clamp the centre so the (translateX(-50%)) bubble stays on screen.
    let cx = r.left + r.width / 2;
    const minCx = useW / 2 + margin;
    const maxCx = window.innerWidth - useW / 2 - margin;
    if (cx < minCx) cx = minCx;
    if (cx > maxCx) cx = maxCx;

    // Prefer above the anchor, else below -- then CLAMP into the viewport, or a tall bubble runs off
    // the bottom. A bubble taller than the window is pinned to the top margin so at least its head
    // is visible; panels cap their own list lengths.
    let top = r.top - tipH - margin;
    if (top < margin) top = r.bottom + margin;
    if (top + tipH > window.innerHeight - margin) top = window.innerHeight - tipH - margin;
    if (top < margin) top = margin;

    tipEl.style.left = cx + 'px';
    tipEl.style.top  = top + 'px';
  }
  function hideTip() { tipEl.style.opacity = '0'; }

  const TIP_SEL = '[data-tip]';
  document.addEventListener('mouseover', e => {
    const cell = e.target.closest(TIP_SEL);
    if (cell) showTipFor(cell); else hideTip();
  });
  document.addEventListener('mouseout', e => {
    if (!e.relatedTarget || !e.relatedTarget.closest(TIP_SEL))
      hideTip();
  });

  function tipText(info, itemId, slotIndex, stateLabel) {
    const head = (info && info.name) ? info.name : ('Item #' + itemId);
    const lines = [head, 'ID ' + itemId, 'Slot ' + (slotIndex + 1), stateLabel];
    if (info && info.ge_limit > 0) lines.push('Limit ' + info.ge_limit.toLocaleString() + '/4h');
    // Provenance of the picture: bundled pack, rendered offline from the cache, or none yet.
    try { if (typeof rtxData === 'object') { const src = rtxData.sync('cache.iconSource', itemId); lines.push(src === 'rendered' ? 'Icon: rendered' : src === 'pack' ? 'Icon: pack' : 'Icon: none yet'); } } catch (e) {}
    return lines.join('\n');
  }

  function attachInfo(cell, itemId, slotIndex, stateLabel) {
    const cached = INFO.get(itemId);
    cell.dataset.tip = tipText(cached, itemId, slotIndex, stateLabel);
    if (cached !== undefined) return;
    if (!bridge() || INFO_PENDING.has(itemId)) return;
    INFO_PENDING.add(itemId);
    (async () => {
      try {
        const info = JSON.parse(await bridge().itemInfo(itemId));
        INFO.set(itemId, info || {});
        INFO_PENDING.delete(itemId);
        document.querySelectorAll('.ge-cell[data-item-id="' + itemId + '"]')
          .forEach(node => {
            const slot  = Number(node.dataset.slot) - 1;
            const state = node.dataset.state || '';
            node.dataset.tip = tipText(info, itemId, slot, state)
                             + (node.dataset.collectTip ? '\n' + node.dataset.collectTip : '');
          });
      } catch (e) { INFO_PENDING.delete(itemId); }
    })();
  }

  // GE offer-complete alert; resolves the item name (fetching if not cached) so the alert names it.
  function geOfferAlert(g) {
    const verb  = (g.type === 0) ? 'Bought' : 'Sold';
    const amt   = (g.filled || g.quantity || 0).toLocaleString();
    const fire  = (name) => fireAlert('ge', verb + ' ' + amt + ' ' + name + ' @ ' + fmtGp(g.price));
    const cached = INFO.get(g.item_id);
    if (cached && cached.name) { fire(cached.name); return; }
    (async () => {
      let name = 'item ' + g.item_id;
      try {
        const info = JSON.parse(await bridge().itemInfo(g.item_id));
        if (info) { INFO.set(g.item_id, info); if (info.name) name = info.name; }
      } catch (e) {}
      fire(name);
    })();
  }

  function attachIcon(el, itemId) {
    if (!itemId) return;
    const cached = ICONS.get(itemId);
    if (cached) { setIconBg(el, cached); return; }
    if (!bridge()) return;
    const now = Date.now();
    const next = ICON_PENDING.get(itemId);
    if (next && now < next) return;
    ICON_PENDING.set(itemId, now + 1500);
    (async () => {
      try {
        const url = await bridge().itemIcon(itemId);
        if (url) {
          ICONS.set(itemId, url);
          ICON_PENDING.delete(itemId);
          document.querySelectorAll('.ge-icon[data-item-id="' + itemId + '"]')
            .forEach(node => setIconBg(node, url));
        }
      } catch (e) {}
    })();
  }

  function fmtStatus(s) {
    if (!s || s.status < 0) return '--';
    return s.status_label ? s.status_label : 'Unknown';
  }

