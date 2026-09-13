// RuneToolsX panel: Bank (searchable, paged item grid; state stays declared in client.html).
// Adds Grand Exchange and high alchemy values: per item, as totals, as a sort order, and optionally as a
// label drawn in the game next to the bank title while the bank is open.
(function () {

  // ---- values: GE from the price relay the GE Prices tab already caches, HA from the item definition ----
  const bankHa = Object.create(null);         // item id -> high alch coins (value * 0.6), null = no definition
  let bankGe = null, bankGeAt = 0;            // id -> { high, low }
  function bankPrices() {
    const now = Date.now();
    if (bankGe && now - bankGeAt < 30000) return bankGe;
    try {
      const d = JSON.parse(bridge().pricesCached() || '{}');
      const data = d && d.data ? d.data : d;
      if (data && Object.keys(data).length) bankGe = data;
    } catch (e) {}
    bankGeAt = now;
    return bankGe;
  }
  function bankHaOf(id) {
    if (bankHa[id] !== undefined) return bankHa[id];
    let ha = null;
    try { const info = JSON.parse(bridge().itemInfo(id) || '{}'); if (info && info.value > 0) ha = Math.floor(info.value * 0.6); } catch (e) {}
    bankHa[id] = ha;
    return ha;
  }
  // Items without a price of their own are priced as the tradeable item of the same name: the untradeable
  // copy the game hands out (same name, different id), "Augmented X" as X, and the broken / damaged /
  // degraded / used / new / uncharged forms of degradable gear as the plain name. Resolved by name through
  // the price mapping, which is the tradeable item list.
  let bankNameToId = null, bankMapLen = -1;
  const bankBaseOf = Object.create(null);     // item id -> base item id for pricing (or the id itself)
  function bankBaseId(id, name) {
    if (bankBaseOf[id] !== undefined) return bankBaseOf[id];
    let base = id;
    try {
      const raw = bridge().pricesMapping() || '[]';
      if (raw.length !== bankMapLen) {
        bankMapLen = raw.length; bankNameToId = Object.create(null);
        for (const it of JSON.parse(raw)) if (it && it.name) bankNameToId[String(it.name).toLowerCase()] = it.id;
      }
      if (bankNameToId) {
        const own = String(name || '').toLowerCase();
        // strip "Augmented", then any trailing dye or condition tags, repeatedly ("Augmented X (Soul)" -> X)
        let plain = own.replace(/^augmented\s+/, '').trim(), prevPlain = '';
        while (plain !== prevPlain) { prevPlain = plain; plain = plain.replace(/\s+\((?:augmented|broken|damaged|degraded|used|new|uncharged|shadow|barrows|third age|blood|ice|soul|aurora|sun|jungle)\)$/, '').trim(); }
        const hit = bankNameToId[own] != null ? bankNameToId[own] : (plain && bankNameToId[plain] != null ? bankNameToId[plain] : null);
        if (hit != null && hit !== id) base = hit;
        // a dyed item is worth the base item plus the dye: remember which dye so the price can include it
        const dye = own.match(/\((shadow|barrows|third age|blood|ice|soul|aurora|sun|jungle)\)/);
        bankDyeOf[id] = dye && bankNameToId[dye[1] + ' dye'] != null ? bankNameToId[dye[1] + ' dye'] : null;
      }
    } catch (e) {}
    if (bankNameToId) bankBaseOf[id] = base;   // only memoise once the mapping is in
    return base;
  }
  const bankDyeOf = Object.create(null);      // item id -> the dye item's id when the name carries a dye tag
  // Which Grand Exchange figure a bank is valued at: instant-buy (high), instant-sell (low) or their average.
  let bankBasis = prefGet('rtxBankPriceBasis', 'buy');
  const BANK_BASES = { buy: 'Instant buy', sell: 'Instant sell', avg: 'Buy/sell average' };
  function bankPriceAt(p) {
    if (!p) return null;
    const hi = p.high > 0 ? p.high : null, lo = p.low > 0 ? p.low : null;
    let v;
    if (bankBasis === 'sell') v = lo != null ? lo : hi;
    else if (bankBasis === 'avg') v = hi != null && lo != null ? Math.round((hi + lo) / 2) : (hi != null ? hi : lo);
    else v = hi != null ? hi : lo;
    return v > 0 ? v : null;
  }
  function bankGeOf(id, name) {
    const prices = bankPrices();
    let v = bankPriceAt(prices && prices[id]);
    if (v == null && name) {
      const b = bankBaseId(id, name);
      if (b !== id) v = bankPriceAt(prices && prices[b]);
      const dyeId = bankDyeOf[id];                      // dyed: the item is worth the base piece plus the dye
      if (v != null && dyeId != null) { const dv = bankPriceAt(prices && prices[dyeId]); if (dv != null) v += dv; }
    }
    return v;
  }
  // per-item valuation for a row [slot, id, stack, name]
  function bankValue(it) {
    const id = it[1], stack = it[2] | 0;
    const ge = bankGeOf(id, it[3]), ha = bankHaOf(id);
    return { ge: ge, ha: ha, geTotal: ge != null ? ge * stack : 0, haTotal: ha != null ? ha * stack : 0 };
  }
  function fmtGp(n) {
    n = Math.round(n || 0);
    const a = Math.abs(n);
    if (a >= 1e9) return (n / 1e9).toFixed(a >= 1e10 ? 1 : 2) + 'b';
    if (a >= 1e6) return (n / 1e6).toFixed(a >= 1e7 ? 1 : 2) + 'm';
    if (a >= 1e4) return Math.round(n / 1e3) + 'k';
    return n.toLocaleString();
  }
  function bankTotals(items) {
    let ge = 0, ha = 0, priced = 0;
    for (const it of items) { const v = bankValue(it); ge += v.geTotal; ha += v.haTotal; if (v.ge != null) ++priced; }
    return { ge: ge, ha: ha, priced: priced };
  }

  // ---- sort + filter ----
  let bankSort = prefGet('rtxBankSort', 'slot');
  const BANK_SORTS = {
    slot:   { label: 'Bank order',         cmp: (a, b) => a[0] - b[0] },
    name:   { label: 'Name',               cmp: (a, b) => String(a[3] || '').localeCompare(String(b[3] || '')) },
    qty:    { label: 'Quantity',           cmp: (a, b) => (b[2] | 0) - (a[2] | 0) },
    getot:  { label: 'GE value (stack)',   cmp: (a, b) => bankValue(b).geTotal - bankValue(a).geTotal },
    geunit: { label: 'GE price (each)',    cmp: (a, b) => (bankValue(b).ge || 0) - (bankValue(a).ge || 0) },
    hatot:  { label: 'High alch (stack)',  cmp: (a, b) => bankValue(b).haTotal - bankValue(a).haTotal },
    haunit: { label: 'High alch (each)',   cmp: (a, b) => (bankValue(b).ha || 0) - (bankValue(a).ha || 0) },
  };
  function bankFilter() {
    const items = (bankData && bankData.items) ? bankData.items : [];
    const t = bankTerm.trim().toLowerCase();
    const list = !t ? items.slice() : items.filter(it => String(it[1]).indexOf(t) !== -1 ||
                              (it[3] || '').toLowerCase().indexOf(t) !== -1);
    const s = BANK_SORTS[bankSort] || BANK_SORTS.slot;
    // placeholders (quantity 0) hold no value, so every value or quantity sort sinks them to the end
    if (bankSort !== 'slot') list.sort((a, b) => ((b[2] | 0) > 0) - ((a[2] | 0) > 0) || s.cmp(a, b) || a[0] - b[0]);
    return list;
  }

  // ---- in-game label beside the bank title: "GE 1.23b | HA 456m" ----
  let bankOverlayOn = prefGet('rtxBankOverlay', '0') === '1';
  let bankOverlayShown = false, bankOverlayTimer = 0, bankOverlayBusy = false;
  function bankOverlayClear() {
    if (!bankOverlayShown) return;
    bankOverlayShown = false;
    try { rtxData.sync('overlay.uiLabels', ''); } catch (e) {}
  }
  async function bankOverlayTick() {
    if (!bankOverlayOn || !bridge() || !bridge().uiLabels || bankOverlayBusy) return;
    bankOverlayBusy = true;
    try {
      if (!paneVisible('bank')) {                          // the tab's own fetch is idle: pull the bank ourselves
        try { const d = JSON.parse(await bridge().bankItems(myPid())); if (d && Array.isArray(d.items)) bankData = d; } catch (e) {}
      }
      if (!bankData || !bankData.open || !bankData.items || !bankData.items.length) { bankOverlayClear(); return; }
      // Anchor on the bank window itself (517's root is the frame's content rect): the title bar sits in the
      // 32 px above the content, and the close button takes the right-most 32 px of it.
      let frame = null;
      try {
        const d = JSON.parse(rtxData.sync('state.interface', 517, '0') || '{}');
        if (d && d.open && d.hasAbs && Array.isArray(d.comps)) frame = d.comps.find(c => c.sub === -1 && c.w > 200) || null;
      } catch (e) {}
      if (!frame) { bankOverlayClear(); return; }
      const t = bankTotals(bankData.items);
      const text = 'GE ' + fmtGp(t.ge) + '  |  HA ' + fmtGp(t.ha);
      const x = frame.x + frame.w - 40 - Math.round(text.length * 7.2);   // right-aligned in the title bar, clear of the close button
      const y = frame.y - 16;                                              // vertical centre of the 32 px title bar
      rtxData.sync('overlay.uiLabels', x + '\x1f' + y + '\x1f-1\x1f13\x1f' + text);
      bankOverlayShown = true;
    } catch (e) {} finally { bankOverlayBusy = false; }
  }
  function bankOverlaySet(on) {
    bankOverlayOn = !!on; prefSet('rtxBankOverlay', on ? '1' : '0');
    if (bankOverlayTimer) { clearInterval(bankOverlayTimer); bankOverlayTimer = 0; }
    if (on) { bankOverlayTimer = setInterval(bankOverlayTick, 2000); bankOverlayTick(); }
    else bankOverlayClear();
  }
  if (bankOverlayOn) setTimeout(() => bankOverlaySet(true), 2500);

  function bankMenuClose() { const m = document.getElementById('bankMenu'); if (m) { m.remove(); try { wmRectsSoon(); } catch (e) {} } }
  function bankMenuOpen(anchor, items, current, onPick) {
    bankMenuClose();
    const pop = document.createElement('div'); pop.className = 'sndmenu bank-menu'; pop.id = 'bankMenu';
    for (const it of items) {
      const el = document.createElement('div'); el.className = 'sndmenu-it' + (it.v === current ? ' sel' : '');
      const nm = document.createElement('span'); nm.textContent = it.label; el.appendChild(nm);
      el.addEventListener('click', () => { onPick(it.v); bankMenuClose(); });
      pop.appendChild(el);
    }
    document.body.appendChild(pop);
    const r = anchor.getBoundingClientRect(), W = window.innerWidth, H = window.innerHeight, SB = 20;
    const below = H - r.bottom - 8, above = r.top - 8, openUp = above > below, avail = Math.max(80, openUp ? above : below);
    if (pop.offsetHeight > avail) pop.style.maxHeight = avail + 'px';
    const pw = pop.offsetWidth, ph = Math.min(pop.offsetHeight, avail);
    let left = r.left; if (left + pw > W - SB) left = r.right - pw; left = Math.max(8, Math.min(left, W - pw - SB));
    let top = openUp ? (r.top - ph - 4) : (r.bottom + 4); top = Math.max(8, Math.min(top, H - ph - 8));
    pop.style.left = left + 'px'; pop.style.top = top + 'px';
    try { wmRectsSoon(); } catch (e) {}
  }
  const bankInsideMenu = e => !!(e && e.target && e.target.nodeType === 1 && e.target.closest && e.target.closest('#bankMenu'));
  document.addEventListener('click', e => { if (!bankInsideMenu(e)) bankMenuClose(); });
  document.addEventListener('scroll', e => { if (!bankInsideMenu(e)) bankMenuClose(); }, true);
  document.addEventListener('keydown', e => { if (e.key === 'Escape') bankMenuClose(); });

  function renderBank() {
    const c = $('content');
    let wrap = document.getElementById('bankWrap');
    if (!wrap) {
      c.innerHTML = '';
      wrap = document.createElement('div'); wrap.id = 'bankWrap'; wrap.className = 'bank-wrap';

      const top = document.createElement('div'); top.className = 'bank-top';
      const inp = document.createElement('input');
      inp.id = 'bankSearch'; inp.className = 'bank-search';
      inp.type = 'text'; inp.placeholder = 'Search bank by name or id...';
      inp.value = bankTerm; inp.spellcheck = false;
      inp.addEventListener('input', () => { bankTerm = inp.value; bankPage = 0; paintBankPage(); });
      const meta = document.createElement('div'); meta.id = 'bankMeta'; meta.className = 'bank-meta';
      top.appendChild(inp); top.appendChild(meta);

      const tools = document.createElement('div'); tools.className = 'bank-tools';
      const sortBtn = document.createElement('button'); sortBtn.type = 'button'; sortBtn.className = 'al-sndsel bank-pick'; sortBtn.id = 'bankSortSel';
      const sortLab = document.createElement('span'); sortLab.className = 'lab'; sortLab.textContent = 'Sort';
      const sortCur = document.createElement('span'); sortCur.className = 'cur';
      const sortCar = document.createElement('span'); sortCar.className = 'cv'; sortCar.textContent = '\u25BE';
      sortBtn.appendChild(sortLab); sortBtn.appendChild(sortCur); sortBtn.appendChild(sortCar);
      const sortRefresh = () => { sortCur.textContent = (BANK_SORTS[bankSort] || BANK_SORTS.slot).label; };
      sortBtn.addEventListener('click', e => {
        e.stopPropagation();
        if (document.getElementById('bankMenu')) { bankMenuClose(); return; }
        bankMenuOpen(sortBtn, Object.keys(BANK_SORTS).map(k => ({ v: k, label: BANK_SORTS[k].label })), bankSort,
                     v => { bankSort = v; prefSet('rtxBankSort', bankSort); bankPage = 0; bankPaintSig = ''; sortRefresh(); paintBankPage(); });
      });
      sortRefresh();
      const basisBtn = document.createElement('button'); basisBtn.type = 'button'; basisBtn.className = 'al-sndsel bank-pick'; basisBtn.id = 'bankBasisSel';
      basisBtn.title = 'Which Grand Exchange price values the bank: instant-buy, instant-sell, or the average of the two';
      const basisLab = document.createElement('span'); basisLab.className = 'lab'; basisLab.textContent = 'GE';
      const basisCur = document.createElement('span'); basisCur.className = 'cur';
      const basisCar = document.createElement('span'); basisCar.className = 'cv'; basisCar.textContent = '\u25BE';
      basisBtn.appendChild(basisLab); basisBtn.appendChild(basisCur); basisBtn.appendChild(basisCar);
      const basisRefresh = () => { basisCur.textContent = BANK_BASES[bankBasis] || BANK_BASES.buy; };
      basisBtn.addEventListener('click', e => {
        e.stopPropagation();
        if (document.getElementById('bankMenu')) { bankMenuClose(); return; }
        bankMenuOpen(basisBtn, Object.keys(BANK_BASES).map(k => ({ v: k, label: BANK_BASES[k] })), bankBasis,
                     v => { bankBasis = v; prefSet('rtxBankPriceBasis', bankBasis); bankPaintSig = ''; basisRefresh(); paintBankPage(); if (bankOverlayOn) bankOverlayTick(); });
      });
      basisRefresh();
      const totals = document.createElement('div'); totals.id = 'bankTotals'; totals.className = 'bank-totals';
      const ovSeg = document.createElement('div'); ovSeg.className = 'al-seg bank-seg'; ovSeg.title = 'Draw the GE and high alch totals in game, in the bank window title bar, while the bank is open';
      const ovBtn = document.createElement('button'); ovBtn.type = 'button'; ovBtn.id = 'bankOverlayBtn'; ovBtn.textContent = 'Totals in game';
      ovBtn.classList.toggle('on', bankOverlayOn);
      ovBtn.addEventListener('click', () => { bankOverlaySet(!bankOverlayOn); ovBtn.classList.toggle('on', bankOverlayOn); });
      ovSeg.appendChild(ovBtn);
      tools.appendChild(sortBtn); tools.appendChild(basisBtn); tools.appendChild(totals); tools.appendChild(ovSeg);

      const grid = document.createElement('div'); grid.id = 'bankGrid'; grid.className = 'bank-grid';

      const empty = document.createElement('div'); empty.id = 'bankEmpty'; empty.className = 'bank-empty';

      const pager = document.createElement('div'); pager.className = 'bank-pager';
      const prev = document.createElement('button'); prev.id = 'bankPrev'; prev.textContent = '<';
      const pg   = document.createElement('span');   pg.id = 'bankPg'; pg.className = 'pg';
      const next = document.createElement('button'); next.id = 'bankNext'; next.textContent = '>';
      prev.addEventListener('click', () => { bankPage--; paintBankPage(); });
      next.addEventListener('click', () => { bankPage++; paintBankPage(); });
      pager.appendChild(prev); pager.appendChild(pg); pager.appendChild(next);

      wrap.appendChild(top); wrap.appendChild(tools); wrap.appendChild(grid); wrap.appendChild(empty); wrap.appendChild(pager);
      c.appendChild(wrap);
      bankPaintSig = '';
      try { bridge().pricesCached(); bridge().pricesMapping(); } catch (e) {}   // kick the price fetches so values fill in on the first paint
    }
    paintBankPage();
  }

  function paintBankPage() {
    const grid = document.getElementById('bankGrid');
    if (!grid) return;
    const emptyBox = document.getElementById('bankEmpty');
    const pager = grid.parentElement.querySelector('.bank-pager');
    const total = (bankData && bankData.count) ? bankData.count : 0;
    const filtered = bankFilter();
    const pages = Math.max(1, Math.ceil(filtered.length / BANK_PER_PAGE));
    if (bankPage >= pages) bankPage = pages - 1;
    if (bankPage < 0) bankPage = 0;

    const priceGen = bankGe ? bankGeAt : 0;
    const sig = bankTerm + '|' + bankPage + '|' + filtered.length + '|' + bankSort + '|' + bankBasis + '|' + priceGen + '|' +
                (bankData ? bankData.cached_at + '|' + bankData.open : 'x');
    if (sig === bankPaintSig) { topUpBankIcons(); return; }
    bankPaintSig = sig;

    const totalsEl = document.getElementById('bankTotals');
    if (totalsEl) {
      const all = (bankData && bankData.items) ? bankData.items : [];
      if (!all.length) totalsEl.innerHTML = '';
      else {
        const t = bankTotals(all);
        totalsEl.innerHTML = '<span title="Grand Exchange value of every priced item at the ' + (BANK_BASES[bankBasis] || 'instant buy').toLowerCase() + ' price, times quantity">GE <b>' + fmtGp(t.ge) + '</b></span>'
          + '<span title="High alchemy value of every item, 60% of the item value times quantity">HA <b class="ha">' + fmtGp(t.ha) + '</b></span>'
          + (t.priced < all.length ? '<span title="Items without a Grand Exchange price count as 0 in the GE total">' + (all.length - t.priced) + ' unpriced</span>' : '');
      }
    }

    if (total === 0 || filtered.length === 0) {
      grid.style.display = 'none';
      if (pager) pager.style.display = 'none';
      if (emptyBox) {
        emptyBox.style.display = 'flex';
        if (total === 0) {
          emptyBox.innerHTML =
            '<div class="warn"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" ' +
            'stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">' +
            '<path d="M12 9v4M12 17h.01M10.3 3.9 1.8 18a2 2 0 0 0 1.7 3h17a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0z"/></svg>' +
            '<div><b>No bank data cached yet.</b><br>Open your bank in game to populate this window.</div></div>';
        } else {
          emptyBox.innerHTML = '<div class="warn" style="background:rgba(255,255,255,0.04);' +
            'border-color:var(--border);color:var(--text-dim)">No items match "' +
            bankTerm.replace(/[<>&"]/g, '') + '".</div>';
        }
      }
      const meta0 = document.getElementById('bankMeta');
      if (meta0) {
        const liveTxt0 = (bankData && bankData.open) ? '<span class="live">live</span>'
          : (bankData && bankData.cached_at
              ? 'cached ' + new Date(bankData.cached_at * 1000).toLocaleString() : '');
        meta0.innerHTML = (total ? (total + ' items') : '') + (liveTxt0 ? '<br>' + liveTxt0 : '');
      }
      return;
    }
    grid.style.display = '';
    if (pager) pager.style.display = '';
    if (emptyBox) emptyBox.style.display = 'none';

    const showHa = bankSort === 'hatot' || bankSort === 'haunit';
    const start = bankPage * BANK_PER_PAGE;
    const slice = filtered.slice(start, start + BANK_PER_PAGE);
    grid.innerHTML = '';
    for (let i = 0; i < slice.length; ++i) {
      const cell = document.createElement('div'); cell.className = 'bank-cell';
      const it = slice[i];
      const [slot, id, stack, name] = it;
      if (stack === 0) cell.classList.add('placeholder');
      const ico = document.createElement('div'); ico.className = 'bank-icon';
      ico.dataset.itemId = String(id);
      attachBankIcon(ico, id);
      cell.appendChild(ico);
      const amt = fmtAmt(stack);
      if (amt.t) {
        const a = document.createElement('span'); a.className = 'bank-amt' + (amt.c ? ' ' + amt.c : '');
        a.textContent = amt.t; cell.appendChild(a);
      }
      const v = bankValue(it);
      const badgeVal = showHa ? v.haTotal : v.geTotal;
      if (badgeVal > 0 && stack > 0) {
        const b = document.createElement('span'); b.className = 'bank-val' + (showHa ? ' ha' : '');
        b.textContent = fmtGp(badgeVal); cell.appendChild(b);
      }
      cell.dataset.tip = (name || ('Item #' + id)) + '\nID ' + id + '\nx' + stack.toLocaleString() +
        (stack === 0 ? ' (placeholder)' : '') + '\nSlot ' + slot +
        '\nGE ' + (v.ge != null ? v.ge.toLocaleString() + ' ea' + (stack > 1 ? ' · ' + fmtGp(v.geTotal) + ' total' : '') + (bankBaseOf[id] !== undefined && bankBaseOf[id] !== id ? (bankDyeOf[id] != null ? ' (base item + dye)' : ' (base item)') : '') : 'no price') +
        '\nHA ' + (v.ha != null ? v.ha.toLocaleString() + ' ea' + (stack > 1 ? ' · ' + fmtGp(v.haTotal) + ' total' : '') : 'n/a');
      grid.appendChild(cell);
    }

    const meta = document.getElementById('bankMeta');
    if (meta) {
      const liveTxt = (bankData && bankData.open) ? '<span class="live">live</span>'
        : (bankData && bankData.cached_at
            ? 'cached ' + new Date(bankData.cached_at * 1000).toLocaleString() : '');
      const shown = filtered.length !== total ? (filtered.length + ' of ' + total) : (total + ' items');
      meta.innerHTML = shown + (liveTxt ? '<br>' + liveTxt : '');
    }
    const pg = document.getElementById('bankPg');
    if (pg) pg.textContent = filtered.length ? ('Page ' + (bankPage + 1) + ' / ' + pages) : 'No items';
    const prev = document.getElementById('bankPrev'); if (prev) prev.disabled = bankPage <= 0;
    const next = document.getElementById('bankNext'); if (next) next.disabled = bankPage >= pages - 1;
    if (bankOverlayOn) bankOverlayTick();
  }

Object.assign(window, { paintBankPage });
registerTab({ id: 'bank', render: renderBank, open: function () { bankFetchKey = ''; fetchBank(); } });
})();
