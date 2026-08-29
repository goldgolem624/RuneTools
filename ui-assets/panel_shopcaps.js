// RuneToolsX panel: Shop Limits (capped shop purchases with reset countdowns).
// Spliced inline into client.html at load; IIFE (window exports + registerTab; see the RTX registry in client.html).
//
// Purchase counts live in per-item varbits; the shop struct holds the cap (param 4166), the
// member item (param 4851, icon/name) and the display name (param 4849). The four grouped
// ware slots (City of Um / Thalmund) share one bought-counter per group, paired with a LIMIT
// varbit, and reset weekly on Wednesday.
// MTX-adjacent caps (lamps, stars, cores, protean packs, TH keys, ...) are not shown: the
// script-17845 daily list was tried and dropped as outdated Treasure Hunter stock.
(function () {

  // Grouped ware slots: [boughtVb, limitVb, [member struct ids]]
  const SC_GROUPS = [
    [55086, 55087, [47447, 47448, 47449, 47985, 48315, 48316]],
    [55088, 55089, [48317, 48318, 48319, 48320, 48321]],
    [55090, 55091, [48322, 48323, 48477, 48481, 48482, 48505, 48506, 48507]],
    [55092, 55093, [48508, 48779, 48780, 48781]]];

  function scVbIds() { return SC_GROUPS.flatMap(g => [g[0], g[1]]).join(','); }

  let scVb = null, scFetching = false, scFetchAt = 0, scSig = '';
  let scStructs = null;         // struct id -> {name, item, cap}
  async function scLoadStructs() {
    if (scStructs || !bridge().structParams) return;
    const out = {};
    for (const sid of SC_GROUPS.flatMap(g => g[2])) {
      try {
        const sp = JSON.parse(await rtxData.raw('cache.structParams', sid) || 'null');
        const I = (sp && sp.ints) || {}, S = (sp && sp.strs) || {};
        let name = S['4849'] || '';
        const item = I['4851'] | 0;
        if (!name && item > 0 && bridge().itemInfo) {
          try { const d = JSON.parse(await rtxData.raw('cache.itemInfo', item) || 'null'); name = (d && d.name) || ''; } catch (e) {}
        }
        out[sid] = { name: name || ('Ware #' + sid), item, cap: I['4166'] != null ? I['4166'] | 0 : null };
      } catch (e) { return; }   // cache hiccup: retry whole load next fetch
    }
    scStructs = out;
  }
  async function fetchShopCaps() {
    if (!bridge() || !bridge().varbits || scFetching) return;
    const t = Date.now();
    if (t - scFetchAt < 2000) return;
    scFetchAt = t; scFetching = true;
    try {
      const vb = JSON.parse(await rtxData.raw('state.varbitsCsv', scVbIds()) || 'null');
      if (vb && typeof vb === 'object' && Object.keys(vb).length) scVb = vb;
      await scLoadStructs();
    } catch (e) { /* keep previous */ }
    scFetching = false;
    paneRun('shopcaps', renderShopCaps);
  }
  const scV = k => ((scVb && scVb[String(k)]) | 0);
  // Reset clocks: daily = next 00:00 UTC; weekly = next Wednesday 00:00 UTC.
  function scDailyMs() { return 86400000 - (Date.now() % 86400000); }   // used by the weekly clock
  function scWeeklyMs() {
    const now = new Date();
    const day = now.getUTCDay();                       // Wed = 3
    let days = (3 - day + 7) % 7;
    const ms = scDailyMs();
    if (days === 0 && ms < 86400000) days = 7;         // it is Wednesday already: next week
    return (days - 1) * 86400000 + ms;
  }
  function shcFmt(ms) {
    const m = Math.floor(ms / 60000), d = Math.floor(m / 1440), h = Math.floor((m % 1440) / 60), mm = m % 60;
    return d > 0 ? d + 'd ' + h + 'h' : h > 0 ? h + 'h ' + mm + 'm' : mm + 'm';
  }

  function renderShopCaps() {
    const c = $('content');
    let wrap = $('shcWrap');
    if (!wrap) {
      injectStyle('shcCss', `
          .shc-sec { margin: 2px 12px 0; }
          .shc-st { display: flex; align-items: center; gap: 8px; padding: 10px 2px 6px; }
          .shc-tt { flex: 1; color: var(--accent-hi); font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: .05em; }
          .shc-timer { color: var(--text); font-size: 11px; font-variant-numeric: tabular-nums; background: var(--bg-elev); border: 1px solid var(--border); border-radius: 6px; padding: 2px 8px; }
          .shc-card { background: var(--bg-elev); border: 1px solid var(--border); border-radius: 10px; }
          .shc-row { display: flex; align-items: center; gap: 10px; padding: 7px 12px; }
          .shc-row + .shc-row { border-top: 1px solid var(--border); }
          .shc-ico { flex: 0 0 auto; width: 22px; height: 22px; background-repeat: no-repeat; background-position: center; }
          .shc-nm { flex: 1; min-width: 0; color: var(--text); font-size: 12px; }
          .shc-sub { color: var(--text-mute); font-size: 10.5px; margin-top: 1px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
          .shc-pill { font-size: 10.5px; padding: 2px 9px; border-radius: 999px; border: 1px solid var(--border); color: var(--text-dim); white-space: nowrap; font-variant-numeric: tabular-nums; }
          .shc-pill.ok { color: var(--ok); border-color: rgba(77, 210, 138, .45); }
          .shc-pill.no { color: #e05656; border-color: rgba(224, 86, 86, .45); }
          .shc-empty { margin: 10px 14px; color: var(--text-dim); font-size: 12px; }`);
      c.innerHTML = ''; scSig = '';
      wrap = document.createElement('div'); wrap.id = 'shcWrap'; wrap.className = 'pane'; c.appendChild(wrap);
    }
    if (!scVb || !scStructs) {
      wrap.innerHTML = '<div class="shc-empty">Reading shop limits... (be in-world)</div>';
      scSig = ''; return;
    }
    const sig = scVbIds().split(',').map(k => scV(k)).join(',') + '|' + Math.floor(Date.now() / 60000);
    if (sig === scSig) return;
    scSig = sig;
    wrap.innerHTML = '';

    const sec = (title, timer) => {
      const s = document.createElement('div'); s.className = 'shc-sec';
      const h = document.createElement('div'); h.className = 'shc-st';
      const t = document.createElement('div'); t.className = 'shc-tt'; t.textContent = title; h.appendChild(t);
      if (timer) { const tm = document.createElement('span'); tm.className = 'shc-timer'; tm.textContent = timer; h.appendChild(tm); }
      s.appendChild(h);
      const card = document.createElement('div'); card.className = 'shc-card'; s.appendChild(card);
      wrap.appendChild(s);
      return card;
    };
    const row = (card, item, name, sub, used, cap) => {
      const r = document.createElement('div'); r.className = 'shc-row';
      const ico = document.createElement('div'); ico.className = 'shc-ico';
      if (item > 0) { const u = resolveIcon(item); if (u) setIconBg(ico, u); }
      r.appendChild(ico);
      const nm = document.createElement('div'); nm.className = 'shc-nm';
      const t = document.createElement('div'); t.textContent = name; nm.appendChild(t);
      if (sub) { const sb = document.createElement('div'); sb.className = 'shc-sub'; sb.textContent = sub; nm.appendChild(sb); }
      r.appendChild(nm);
      const p = document.createElement('span');
      const capped = cap > 0 && used >= cap;
      p.className = 'shc-pill' + (capped ? ' no' : used > 0 ? ' ok' : '');
      p.textContent = cap > 0 ? used + ' / ' + cap : String(used);
      r.appendChild(p);
      return card.appendChild(r);
    };

    // ---- weekly ware slots (shared bought-counter per slot; limit is the pair varbit) ----
    {
      const wk = sec('Weekly ware slots', 'resets Wed · ' + shcFmt(scWeeklyMs()));
      SC_GROUPS.forEach((g, i) => {
        const [boughtVb, limitVb, members] = g;
        const first = scStructs[members[0]] || {};
        const names = members.map(sid => (scStructs[sid] || {}).name).filter(Boolean);
        const r = row(wk, first.item | 0, 'Ware slot ' + (i + 1), names.join(' · '),
                      scV(boughtVb), scV(limitVb));
        r.dataset.tip = 'Possible wares for this slot:\n' +
          members.map(sid => {
            const s = scStructs[sid] || {};
            return '  ' + (s.name || sid) + (s.cap != null ? ' (cap ' + s.cap + ')' : '');
          }).join('\n') +
          // The limit varbit reads 0 until the game syncs it on visiting the shop.
          '\n\nBought this week: ' + scV(boughtVb)
          + (scV(limitVb) > 0 ? ' of ' + scV(limitVb) + ' available'
                              : ' (weekly limit not synced yet -- visit the shop once)');
      });
    }

    sizeAllIcons();
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { fetchShopCaps });
registerTab({ id: 'shopcaps', render: renderShopCaps, open: function () { scSig = ''; fetchShopCaps(); } });
})();
