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

  function tipHtml(text) {
    return text
      .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;')
      .replace(/&lt;br&gt;/gi, '<br>')
      .replace(/&lt;col=([0-9a-fA-F]{6})&gt;/g, '<span style="color:#$1">')
      .replace(/&lt;\/col&gt;/gi, '</span>');
  }
  const TIP_GRIDS = new WeakMap();   // row node -> groups[]
  function setTipGrids(node, groups) {
    if (groups && groups.length) TIP_GRIDS.set(node, groups); else TIP_GRIDS.delete(node);
  }
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
    if (!text || !uiCfg().compactTips) return text;
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
    tipEl.style.width = '';        // natural size for the measurement
    tipEl.style.left  = '0px';
    tipEl.style.top   = '0px';
    const tipW = tipEl.offsetWidth  || 0;
    const tipH = tipEl.offsetHeight || 24;
    tipEl.style.boxSizing = 'border-box';
    const margin = 6;
    const avail = Math.max(40, window.innerWidth - 2 * margin);
    const useW = Math.min(tipW, avail);
    tipEl.style.width = useW + 'px';

    let cx = r.left + r.width / 2;
    const minCx = useW / 2 + margin;
    const maxCx = window.innerWidth - useW / 2 - margin;
    if (cx < minCx) cx = minCx;
    if (cx > maxCx) cx = maxCx;

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

