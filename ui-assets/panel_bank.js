// RuneToolsX panel: Bank (searchable, paged item grid; state stays declared in client.html).
// Spliced inline into client.html; IIFE (window exports + registerTab; see the RTX registry in client.html).
(function () {

  function bankFilter() {
    const items = (bankData && bankData.items) ? bankData.items : [];
    const t = bankTerm.trim().toLowerCase();
    if (!t) return items;
    return items.filter(it => String(it[1]).indexOf(t) !== -1 ||
                              (it[3] || '').toLowerCase().indexOf(t) !== -1);
  }

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

      const grid = document.createElement('div'); grid.id = 'bankGrid'; grid.className = 'bank-grid';

      const empty = document.createElement('div'); empty.id = 'bankEmpty'; empty.className = 'bank-empty';

      const pager = document.createElement('div'); pager.className = 'bank-pager';
      const prev = document.createElement('button'); prev.id = 'bankPrev'; prev.textContent = '<';
      const pg   = document.createElement('span');   pg.id = 'bankPg'; pg.className = 'pg';
      const next = document.createElement('button'); next.id = 'bankNext'; next.textContent = '>';
      prev.addEventListener('click', () => { bankPage--; paintBankPage(); });
      next.addEventListener('click', () => { bankPage++; paintBankPage(); });
      pager.appendChild(prev); pager.appendChild(pg); pager.appendChild(next);

      wrap.appendChild(top); wrap.appendChild(grid); wrap.appendChild(empty); wrap.appendChild(pager);
      c.appendChild(wrap);
      bankPaintSig = '';
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

    const sig = bankTerm + '|' + bankPage + '|' + filtered.length + '|' +
                (bankData ? bankData.cached_at + '|' + bankData.open : 'x');
    if (sig === bankPaintSig) { topUpBankIcons(); return; }
    bankPaintSig = sig;

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

    const start = bankPage * BANK_PER_PAGE;
    const slice = filtered.slice(start, start + BANK_PER_PAGE);
    grid.innerHTML = '';
    // No empty filler cells: with fixed-size square cells they add invisible rows that push the
    // pager far below the content on partial pages.
    for (let i = 0; i < slice.length; ++i) {
      const cell = document.createElement('div'); cell.className = 'bank-cell';
      const it = slice[i];
      const [slot, id, stack, name] = it;
      // stack 0 = bank placeholder (slot reserved, item not actually present)
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
      cell.dataset.tip = (name || ('Item #' + id)) + '\nID ' + id + '\nx' + stack.toLocaleString() +
        (stack === 0 ? ' (placeholder)' : '') + '\nSlot ' + slot;
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
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { paintBankPage });
registerTab({ id: 'bank', render: renderBank, open: function () { bankFetchKey = ''; fetchBank(); } });
})();
