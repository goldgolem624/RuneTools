// rtx-storage.js: Bank, metal bank materials, Archaeology guild shop, bait box and workbench renderers (fetchBank, paintMaterials, renderBaitBox, paintWorkbench...).
// Loads after: rtx-icons.js (attachIcon/attachInfo) and rtx-bridge.js.
// Plain script, page globals by design: every top-level name here is a page global that panels and the other core files use.
  // ---- Bank (paginated, searchable) ----
  const BANK_PER_PAGE = 50;          // 10 cols x 5 rows
  let bankData     = null;           // {open, character, cached_at, count, items:[[slot,id,stack,name]]}
  let bankTerm     = '';
  let bankPage     = 0;
  let bankFetchKey = '';             // last (open|cached_at) fetched for
  let mbankFetchKey = '';            // metal bank equivalent
  let matFetchKey = '';              // archaeology material storage equivalent
  let gbankFetchKey = '';            // group ironman shared bank equivalent
  let baitFetchKey = '';             // anachronia bait box equivalent
  let wbFetchKey = '';               // archaeologist's workbench (damaged artefacts) equivalent
  let bankPaintSig = '';             // last painted signature, to avoid flicker
  let bankFetching = false;

  // RS item-amount tiers (game thresholds + colours): <100K full yellow, 100K-9.999M "K" white,
  // 10M-9.999B "M" green, 10B-9.999T "B" blue, 10T-9.999Q "T", >=10Q "Q". Integer units (the game floors).
  // Item amounts. Below 100K the game shows the exact count, and so do we. Above that the
  // unit comes from the MAGNITUDE, not from the game's shifted tiers: the game calls
  // 2.3 billion "2325M", so two decimals there would read "2325.18M" and run off the cell.
  // Choosing the unit by magnitude keeps every label between 1.00 and 999.99 plus a suffix.
  const AMT_UNITS = [[1e15, "Q", "q"], [1e12, "T", "t"], [1e9, "B", "b"], [1e6, "M", "m"], [1e3, "K", "k"]];
  function fmtAmt(n) {
    if (!(n > 1)) return { t: n === 1 ? "" : "", c: "" };
    if (n < 1e5) return { t: String(n), c: "" };          // exact count, RS yellow
    for (let i = 0; i < AMT_UNITS.length; i++) {
      const [div, suffix, cls] = AMT_UNITS[i];
      if (n < div) continue;
      let m = (n / div).toFixed(2);
      // 999.999 rounds to "1000.00": promote it rather than print four digits.
      if (parseFloat(m) >= 1000 && i > 0) {
        const up = AMT_UNITS[i - 1];
        return { t: (n / up[0]).toFixed(2) + up[1], c: up[2] };
      }
      return { t: m + suffix, c: cls };
    }
    return { t: String(n), c: "" };
  }

  // Icons render as CSS background-image, NOT <img> (a replaced <img> with %-sizing
  // resolves to intrinsic size in Ultralight's WebKit -> uneven). Sizing is
  // DOWNSCALE-ONLY: true pixel size, shrunk only when bigger than the box;
  // `contain` blew tightly-cropped small icons up to fill the slot.
  const ICON_NAT = new Map();   // data-url -> {w,h} | null (not a decodable PNG)
  // Read width/height straight from the PNG IHDR (offsets 16/20, big-endian) of
  // the data URL -- synchronous + reliable, no Image() load race.
  function pngSize(url) {
    if (ICON_NAT.has(url)) return ICON_NAT.get(url);
    let s = null;
    const c = url.indexOf(',');
    if (c >= 0 && url.lastIndexOf('image/png', c) >= 0) {
      try {
        const bin = atob(url.slice(c + 1, c + 1 + 40));
        if (bin.length >= 24 && bin.charCodeAt(0) === 0x89) {
          const u = n => ((bin.charCodeAt(n) << 24 | bin.charCodeAt(n + 1) << 16 |
                           bin.charCodeAt(n + 2) << 8 | bin.charCodeAt(n + 3)) >>> 0);
          s = { w: u(16), h: u(20) };
        }
      } catch (e) { s = null; }
    }
    ICON_NAT.set(url, s); return s;
  }
  function sizeIcon(el) {
    const url = el.dataset.icoUrl; if (!url) return;
    const nat = pngSize(url);
    const commit = (want) => {
      if (el.dataset.icoSized !== want) { el.style.backgroundSize = want; el.dataset.icoSized = want; }
    };
    if (!nat) { commit('contain'); return; }              // not a decodable PNG -> fit the box
    const bw = el.clientWidth, bh = el.clientHeight;
    if (!bw || !bh) {
      // Not laid out yet: committing native size here painted oversized sprites for a
      // beat before the next sizing pass shrank them (visible on every panel as icons
      // flashing huge). Paint NOTHING for the pre-layout instant and decide with real
      // box dimensions on the next frame, which fires before paint in this renderer.
      // A box that stays unmeasurable (hidden tab) settles on contain, which is the
      // correct rendering for any box size.
      commit('0px 0px');
      const tries = (parseInt(el.dataset.icoTries || '0', 10) || 0) + 1;
      el.dataset.icoTries = String(tries);
      if (tries <= 3) requestAnimationFrame(() => sizeIcon(el));
      else commit('contain');
      return;
    }
    el.dataset.icoTries = '';
    // Native size when it fits (pixel-crisp); fit down only when confirmed larger.
    // The box this was decided against is recorded, because the decision only holds
    // for that box: a pixel size committed for a wide cell CROPS once the cell narrows.
    el.dataset.icoBox = bw + 'x' + bh;
    commit((nat.w > bw || nat.h > bh) ? 'contain' : (nat.w + 'px ' + nat.h + 'px'));
  }
  // Re-size when the image OR its box changed. The box is not fixed: cell widths
  // follow the grid, which follows the window, and a wrapping name changes the
  // height. Sizing only on URL change left a stale pixel size behind, which shows
  // as a cropped icon once the box is smaller than the size that was committed.
  function sizeAllIcons() {
    document.querySelectorAll('[data-ico-url]').forEach(el => {
      const box = el.clientWidth + 'x' + el.clientHeight;
      if (el.dataset.icoSizedUrl === el.dataset.icoUrl && el.dataset.icoBox === box) return;
      sizeIcon(el);
      el.dataset.icoSizedUrl = el.dataset.icoUrl || '';
    });
    fitNames();
  }
  // Item names must never be split mid-word: "Stormberr / y seed" is unreadable and looks
  // broken. Wrapping alone cannot deliver that, because a grid cell can be narrower than a
  // single word, and then every wrapping mode that is permitted to break will break one.
  // So the text is shrunk instead: step the size down until the widest word fits, floor at
  // 7px. Below the floor the name is clipped by overflow:hidden and the tooltip carries it
  // in full, which is still better than a word cut across two lines.
  const FIT_MAX = 9, FIT_MIN = 7, FIT_STEP = 0.5;
  function fitName(el) {
    const sig = el.textContent + '|' + el.clientWidth;
    if (el.dataset.fitSig === sig) return;          // same text in the same box
    let px = FIT_MAX;
    el.style.fontSize = px + 'px';
    // scrollWidth exceeds clientWidth exactly when some line, i.e. some word, is too wide.
    while (px > FIT_MIN && el.scrollWidth > el.clientWidth) {
      px -= FIT_STEP;
      el.style.fontSize = px + 'px';
    }
    el.dataset.fitSig = sig;
  }
  function fitNames() { document.querySelectorAll('.inv-name').forEach(fitName); }

  // Resizing the window relayouts every grid without necessarily repainting the
  // panels, so the icons have to be told to re-decide.
  let icoResizeQ = 0;
  window.addEventListener('resize', () => {
    if (icoResizeQ) return;
    icoResizeQ = requestAnimationFrame(() => { icoResizeQ = 0; sizeAllIcons(); });
  });

  function setHTML(el, html) {
    if (el._h === html) return false;
    el._h = html; el.innerHTML = html;
    return true;
  }

  function injectStyle(id, css) {
    if (document.getElementById(id)) return;
    const st = document.createElement('style'); st.id = id;
    st.textContent = css;
    document.head.appendChild(st);
  }

  function setIconBg(el, url) {
    el.style.backgroundImage = "url('" + url + "')";
    el.dataset.icoUrl = url;
    el.dataset.icoSized = '';
    sizeIcon(el);
  }

  // itemIcon() reads the bundled pack SYNCHRONOUSLY; memoized, misses cached as ''.
  // Icon overrides for items whose bundled icon is wrong: coins (995) bakes amount
  // text into the sprite, so use a clean coin-pile image.
  const CUSTOM_ICON = {
    995: 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAB8AAAAeCAIAAABbkFLLAAAABnRSTlMAAAAAAABupgeRAAADg0lEQVR42p2WP28TQRDF3+7ZXjvGVhJ0BISAAELib0MBPVBDCRWiRoKOggpRIfEBgJIPQAMNFQiJhlCABAUCChJRBMmIIIfgrG9vl7zVXZY9DHEYbXGRZn7zZnZ2YoGNTSIyO76b2BA9d78LBDt5qQ/YMd3kv9GLj1NsbESPU07VagmO7lcHdqupDlYGePoSI21nqo7tV/D2bp5uY9GTBFJAANbC5H8XL+EcDuxS9RqybDPaE++SW+jh3+kCDkw/zGDMuHSiZUJdmfmdLnli7SCdaGMRxG3YGQHkOem7tvPMneiOHB7niLaWzuNol/MPUykZkHlRFbt1uVuGF1diHQAIMYIuwymtXoNgDNFDg8N71ZF9anaHarfw4m2UyRhCk4QhqhFy16KBjUtu1Blj8+K6nPMfht8/BzE9h+Alkd5sRNqrb4F/+gpqNdLZGaOHhnR+Z38ODx1AOkNaKqLj4B6WvDatAOYXoycjqKscmBK0qgEZ03M4jsAI7cgti12LaTexvILSKDa3RBvDeOuortlQTYVtU9HCOnq+76zXnpCuVKCT4hwTDDQpIcgVwk3OObv3QEsJ1WDtU53K2FBHjdoVHRqBbree6fkwljYztR4GZ8nlscxE41VTXbc9YhlIgVpCh3q9OpGkd1qY7qLVDJPgDy/T2bAbWkq1m5jsRHRRlMsijI3pzjFsy4TaMoEd5dLVGRti2bdCuxS+9YpnshvTJRyKt5qbiG6nT/fW6BNNdCawtQybPdvjlVjtUHonnDm+MkGNFe0UYXXQXnmZ7Ral2RBGBFU74gAkgm4oQIjxxYxRe17dYhTFuuix/qaw8EWXOzbscZ+yuu4FpZBA7Saiy3wuhR9K6xt96kQU+fw1rt/pA7h2MfVPl5SlfkwvtRsmiDsjE2ZfWsaPAd584Otdv+13n8oRepFKgdUh+it6DT0cwqcMDbKsybfFBbp0r1IAi1+xsKjfLxQdODSrZqbVqg4Li9vYUXJvCa8/IDYCrfOlx/89rDjeg7f7N7qXbrLgqxdSnUMP9UBj+WcAfF9G77v+8pUUCo+svLY8omO9OqL5LYWAMWzCQAct8mSvskqLdR3oPMZV6MFCVVnm9/gqgLi/YWPHORyF+76HEIHRJj89TPee64WsweS3J+nHzxqFcWPHXaqGbPaHlOw/m1l4NPP07uTtK5P/8BT4T5PjyPwF8Qz3jm5wM2gAAAAASUVORK5CYII='
  };
  function resolveIcon(itemId) {
    if (CUSTOM_ICON[itemId]) return CUSTOM_ICON[itemId];
    let url = ICONS.get(itemId);
    if (url === undefined) {
      if (!bridge()) return '';   // bridge not up yet: do NOT memoize the miss (it froze icons as placeholders)
      url = '';
      try { const r = bridge().itemIcon(itemId); url = (typeof r === 'string') ? r : ''; } catch (e) { url = ''; }
      ICONS.set(itemId, url);
    }
    return url;
  }
  function attachBankIcon(el, itemId) {
    const url = resolveIcon(itemId);
    if (url) setIconBg(el, url);
  }

  function bankCell(id, stack, name, tipTail) {
    const cell = document.createElement('div'); cell.className = 'bank-cell stor-cell';
    const ico = document.createElement('div'); ico.className = 'bank-icon'; ico.dataset.itemId = String(id);
    attachBankIcon(ico, id); cell.appendChild(ico);
    const amt = fmtAmt(stack);
    if (amt.t) { const a = document.createElement('span'); a.className = 'bank-amt' + (amt.c ? ' ' + amt.c : ''); a.textContent = amt.t; cell.appendChild(a); }
    cell.dataset.tip = (name || ('Item #' + id)) + '\nID ' + id + '\nx' + stack.toLocaleString() + (tipTail || '');
    return cell;
  }

  // Re-attach icons for cells that haven't painted yet, so freshly-arrived icons
  // appear without a page switch.
  function topUpBankIcons() {
    const grid = document.getElementById('bankGrid');
    if (!grid) return;
    grid.querySelectorAll('.bank-icon').forEach(el => {
      if (el.style.backgroundImage) return;  // already showing an icon
      const id = Number(el.dataset.itemId);
      if (id > 0) attachBankIcon(el, id);
    });
  }

  async function fetchBank() {
    if (!bridge() || bankFetching) return;
    bankFetching = true;
    try {
      const d = JSON.parse(await bridge().bankItems(myPid()));
      bankData = d && Array.isArray(d.items) ? d : { open:false, items:[], count:0, cached_at:0 };
    } catch (e) { /* keep previous bankData */ }
    bankFetching = false;
    paneRun('bank', paintBankPage);
  }

  // ---- Archaeology material storage (container 885) -- same cache-while-open model as the bank ----
  async function fetchMaterials() {
    if (!bridge() || matFetching) return;
    matFetching = true;
    try {
      const d = JSON.parse(await bridge().materialItems(myPid()));
      matData = d && Array.isArray(d.items) ? d : { open: false, items: [], count: 0, cached_at: 0 };
    } catch (e) { /* keep previous */ }
    matFetching = false;
    paneRun('materials', renderMaterials);
  }
  // ---- Guild Shop tab: Archaeology Guild permanent-unlock states (own tab). ----
  let ashopFetching = false;
  async function fetchArchShop() {
    if (!bridge() || ashopFetching) return; ashopFetching = true;
    try {
      matUnlockVp = await readVarbitValues(ARCH_SHOP_VBS);
      // Entries with no unlock varbit (the free soil box claim) are proven by possession: scan
      // the cached bank -- readable with the bank closed -- and the live backpack.
      if (ARCH_SHOP_HAVE.length) {
        const found = {};
        const scan = rows => { for (const it of (rows || [])) if (ARCH_SHOP_HAVE.indexOf(it[1]) >= 0) found[it[1]] = true; };
        try { const b = JSON.parse(await bridge().bankItems(myPid())); scan(b && b.items); } catch (e) {}
        try { const v = JSON.parse(await bridge().inventory(myPid())); scan(v && v.items); } catch (e) {}
        archShopHave = found;
      }
    } catch (e) { /* keep previous unlock state */ }
    ashopFetching = false;
    paneRun('archshop', renderArchShop);
  }
  function renderArchShop() {
    const c = $('content');
    let wrap = $('ashopWrap');
    if (!wrap) {
      c.innerHTML = '';
      wrap = document.createElement('div'); wrap.id = 'ashopWrap'; wrap.className = 'pk-wrap';
      const list = document.createElement('div'); list.id = 'matUnlocks'; list.className = 'arch-unlocks';
      wrap.appendChild(list); c.appendChild(wrap); matUnlockSig = '';
    }
    paintUnlocks();
  }
  // ---- Familiar tab -> panel_familiar.js (spliced inline at load) ----
  // Derive one shop entry's live state -> { done, badge, detail }. Tiered upgrades share a single
  // LEVEL varbit, so a tier is owned once that varbit reaches the tier's level; single purchases
  // are owned at >= 1. Entries with no `own` spec are consumables with no ownership to read.
  function archUnlockState(it) {
    const lvl = (matUnlockVp && it.own && matUnlockVp[it.own.vb]) || 0;
    const detail = it.effect || it.note || '';
    if (it.have) {
      // Possession-proven, not varbit-proven: say "Have it" rather than "Owned" so the weaker evidence
      // is visible -- a claimed box that was destroyed would read as not held.
      const got = !!archShopHave[it.have];
      return { done: got, badge: got ? 'Have it' : 'Not held', detail: detail || 'Free, one-time claim' };
    }
    if (!it.own) return { done: false, badge: '', detail: detail, plain: true };
    const need = it.own.lvl || 1;
    if (lvl >= need) return { done: true, badge: 'Owned', detail: detail };
    // The shop sells tiers in order ("...and previous upgrades"), so a tier you cannot buy yet
    // because the one below it is unbought is LOCKED, not merely unbought -- matching the game's
    // three states. Buyable means the varbit sits exactly one tier short.
    const locked = lvl < need - 1;
    return { done: false, locked: locked, badge: locked ? 'Locked' : 'Not bought', detail: detail };
  }
  function renderMaterials() {
    const c = $('content');
    let wrap = $('matWrap');
    if (!wrap) {
      c.innerHTML = '';
      wrap = document.createElement('div'); wrap.id = 'matWrap'; wrap.className = 'bank-wrap';
      const top = document.createElement('div'); top.className = 'bank-top';
      const inp = document.createElement('input'); inp.id = 'matSearch'; inp.className = 'bank-search';
      inp.type = 'text'; inp.placeholder = 'Search material by name or id...'; inp.value = matTerm; inp.spellcheck = false;
      inp.addEventListener('input', () => { matTerm = inp.value; matSig = ''; paintMaterials(); });
      const meta = document.createElement('div'); meta.id = 'matMeta'; meta.className = 'bank-meta';
      top.appendChild(inp); top.appendChild(meta);
      const body = document.createElement('div'); body.id = 'matBody'; body.className = 'mbank-body';
      wrap.appendChild(top); wrap.appendChild(body); c.appendChild(wrap); matSig = '';
    }
    paintMaterials();
  }
  let matUnlockSig = '';
  // Archaeology Guild shop, grouped by qualification rank exactly as the in-game shop tabs are.
  // Only trackable entries (those with an `own` spec) count toward a rank's owned tally; the
  // consumables listed alongside them have no ownership state to read.
  function paintUnlocks() {
    const host = $('matUnlocks'); if (!host) return;
    const groups = ARCH_SHOP.map(g => ({
      rank: g.rank,
      rows: g.items.map(it => ({ it: it, st: archUnlockState(it) })),
    }));
    const sig = groups.map(g => g.rows.map(r => r.st.badge + '|' + r.st.detail).join(';')).join('||');
    if (sig === matUnlockSig) return; matUnlockSig = sig;
    let html = '';
    for (const g of groups) {
      const track = g.rows.filter(r => !r.st.plain);
      const done = track.filter(r => r.st.done).length;
      html += '<div class="arch-head"><span>' + g.rank + '</span><span class="arch-tot">' +
              (track.length ? done + ' / ' + track.length : '&mdash;') + '</span></div><div class="arch-rows">';
      for (const { it, st } of g.rows) {
        html += '<div class="arch-row' + (st.done ? ' is-done' : '') + '">' +
                '<div class="arch-nx"><span class="arch-n">' + it.name + '</span><span class="arch-d">' +
                (st.detail ? st.detail + ' &middot; ' : '') +
                (it.price ? it.price.toLocaleString() + ' chronotes' : 'Free') + '</span></div>' +
                (st.plain ? '' : '<span class="arch-b' + (st.done ? ' ok' : (st.locked ? ' lk' : '')) + '">' + st.badge + '</span>') +
                '</div>';
      }
      html += '</div>';
    }
    host.innerHTML = html;
  }
  function paintMaterials() {
    const body = $('matBody'); if (!body) return;
    const items = (matData && matData.items) ? matData.items : [];
    const t = matTerm.trim().toLowerCase();
    const shown = !t ? items : items.filter(it => String(it[1]).indexOf(t) !== -1 || (it[3] || '').toLowerCase().indexOf(t) !== -1);
    const sig = t + '|' + (matData ? matData.cached_at + '|' + matData.open + '|' + items.length : 'x');
    if (sig === matSig) return; matSig = sig;
    const meta = $('matMeta');
    if (meta) {
      const liveTxt = (matData && matData.open) ? '<span class="live">live</span>'
        : (matData && matData.cached_at ? 'cached ' + new Date(matData.cached_at * 1000).toLocaleString() : '');
      const total = (matData && matData.count) ? matData.count : 0;
      meta.innerHTML = (total ? total + ' materials' : '') + (liveTxt ? '<br>' + liveTxt : '');
    }
    body.innerHTML = '';
    if (!matData) { body.innerHTML = '<div class="bank-empty" style="display:flex"><div class="warn" style="background:rgba(255,255,255,0.04);border-color:var(--border);color:var(--text-dim)">Reading...</div></div>'; return; }
    if (!items.length) {
      body.innerHTML = '<div class="bank-empty" style="display:flex"><div class="warn">' +
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M12 9v4M12 17h.01M10.3 3.9 1.8 18a2 2 0 0 0 1.7 3h17a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0z"/></svg>' +
        '<div><b>No material storage data cached yet.</b><br>Open your Archaeology material storage in game to populate this window.</div></div></div>';
      return;
    }
    if (!shown.length) { body.innerHTML = '<div class="bank-empty" style="display:flex"><div class="warn" style="background:rgba(255,255,255,0.04);border-color:var(--border);color:var(--text-dim)">No material matches "' + matTerm.replace(/[<>&"]/g, '') + '".</div></div>'; return; }
    const grid = document.createElement('div'); grid.className = 'stor-grid';
    for (const it of shown) {
      const [slot, id, stack, name] = it;
      grid.appendChild(bankCell(id, stack, name, '\nArchaeology material storage (container 885)'));
    }
    body.appendChild(grid);
  }
  // ---- Bait box tab: Anachronia Big Game Hunter bait box (container 867). Same model as the metal
  // bank / materials tabs -- live while the box UI is open, else the per-character disk cache. ----
  async function fetchBaitBox() {
    if (!bridge() || baitFetching) return;
    baitFetching = true;
    try {
      const d = JSON.parse(await bridge().baitBoxItems(myPid()));
      baitData = d && Array.isArray(d.items) ? d : { open: false, items: [], count: 0, cached_at: 0 };
    } catch (e) { /* keep previous */ }
    baitFetching = false;
    paneRun('baitbox', renderBaitBox);
  }
  function renderBaitBox() {
    const c = $('content');
    let wrap = $('baitWrap');
    if (!wrap) {
      c.innerHTML = '';
      wrap = document.createElement('div'); wrap.id = 'baitWrap'; wrap.className = 'bank-wrap';
      const top = document.createElement('div'); top.className = 'bank-top';
      const inp = document.createElement('input'); inp.id = 'baitSearch'; inp.className = 'bank-search';
      inp.type = 'text'; inp.placeholder = 'Search bait by name or id...'; inp.value = baitTerm; inp.spellcheck = false;
      inp.addEventListener('input', () => { baitTerm = inp.value; baitSig = ''; paintBaitBox(); });
      const meta = document.createElement('div'); meta.id = 'baitMeta'; meta.className = 'bank-meta';
      top.appendChild(inp); top.appendChild(meta);
      const body = document.createElement('div'); body.id = 'baitBody'; body.className = 'mbank-body';
      wrap.appendChild(top); wrap.appendChild(body); c.appendChild(wrap); baitSig = '';
    }
    paintBaitBox();
  }
  function paintBaitBox() {
    const body = $('baitBody'); if (!body) return;
    const items = (baitData && baitData.items) ? baitData.items : [];
    const t = baitTerm.trim().toLowerCase();
    const shown = !t ? items : items.filter(it => String(it[1]).indexOf(t) !== -1 || (it[3] || '').toLowerCase().indexOf(t) !== -1);
    const sig = t + '|' + (baitData ? baitData.cached_at + '|' + baitData.open + '|' + items.length : 'x');
    if (sig === baitSig) return; baitSig = sig;
    const meta = $('baitMeta');
    if (meta) {
      const liveTxt = (baitData && baitData.open) ? '<span class="live">live</span>'
        : (baitData && baitData.cached_at ? 'cached ' + new Date(baitData.cached_at * 1000).toLocaleString() : '');
      const total = (baitData && baitData.count) ? baitData.count : 0;
      meta.innerHTML = (total ? total + ' items' : '') + (liveTxt ? '<br>' + liveTxt : '');
    }
    body.innerHTML = '';
    if (!baitData) { body.innerHTML = '<div class="bank-empty" style="display:flex"><div class="warn" style="background:rgba(255,255,255,0.04);border-color:var(--border);color:var(--text-dim)">Reading...</div></div>'; return; }
    if (!items.length) {
      body.innerHTML = '<div class="bank-empty" style="display:flex"><div class="warn">' +
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M12 9v4M12 17h.01M10.3 3.9 1.8 18a2 2 0 0 0 1.7 3h17a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0z"/></svg>' +
        '<div><b>No bait box data cached yet.</b><br>Open your Anachronia bait box in game to populate this window.</div></div></div>';
      return;
    }
    if (!shown.length) { body.innerHTML = '<div class="bank-empty" style="display:flex"><div class="warn" style="background:rgba(255,255,255,0.04);border-color:var(--border);color:var(--text-dim)">No item matches "' + baitTerm.replace(/[<>&"]/g, '') + '".</div></div>'; return; }
    const grid = document.createElement('div'); grid.className = 'stor-grid';
    for (const it of shown) {
      const [slot, id, stack, name] = it;
      grid.appendChild(bankCell(id, stack, name, '\nAnachronia bait box (container 867)'));
    }
    body.appendChild(grid);
  }

  // ---- Workbench Storage tab: the Archaeologist's damaged-artefact store (container 1008). Same model
  // as the bait box -- live while the workbench is open, else the per-character disk cache. The
  // container holds nothing until the first guild-shop workbench upgrade (varbit 61463). ----
  async function fetchWorkbench() {
    if (!bridge() || wbFetching) return;
    wbFetching = true;
    const empty = { open: false, items: [], count: 0, cached_at: 0 };
    try {
      if (bridge().workbenchItems) {
        const d = JSON.parse(await bridge().workbenchItems(myPid()));
        wbData = d && Array.isArray(d.items) ? d : empty;
      } else if (bridge().containerItems) {
        // Pre-rebuild fallback: the generic container read still shows the workbench while it is OPEN.
        // No disk cache on that path, so it empties when you walk away; the cached view needs the
        // launcher build that adds the workbenchItems bridge.
        const d = JSON.parse(await bridge().containerItems(myPid(), 1008));
        const items = (d && Array.isArray(d.items)) ? d.items : [];
        wbData = { open: items.length > 0, items: items, count: items.length, cached_at: 0, liveOnly: true };
      } else {
        wbData = empty;
      }
    } catch (e) { if (!wbData) wbData = empty; }   // never leave the tab stuck on "Reading..."
    wbFetching = false;
    paneRun('artefacts', renderWorkbench);
  }
  function renderWorkbench() {
    const c = $('content');
    let wrap = $('wbWrap');
    if (!wrap) {
      c.innerHTML = '';
      wrap = document.createElement('div'); wrap.id = 'wbWrap'; wrap.className = 'bank-wrap';
      const top = document.createElement('div'); top.className = 'bank-top';
      const inp = document.createElement('input'); inp.id = 'wbSearch'; inp.className = 'bank-search';
      inp.type = 'text'; inp.placeholder = 'Search artefact by name or id...'; inp.value = wbTerm; inp.spellcheck = false;
      inp.addEventListener('input', () => { wbTerm = inp.value; wbSig = ''; paintWorkbench(); });
      const meta = document.createElement('div'); meta.id = 'wbMeta'; meta.className = 'bank-meta';
      top.appendChild(inp); top.appendChild(meta);
      const body = document.createElement('div'); body.id = 'wbBody'; body.className = 'mbank-body';
      wrap.appendChild(top); wrap.appendChild(body); c.appendChild(wrap); wbSig = '';
    }
    paintWorkbench();
  }
  function paintWorkbench() {
    const body = $('wbBody'); if (!body) return;
    const items = (wbData && wbData.items) ? wbData.items : [];
    const t = wbTerm.trim().toLowerCase();
    const shown = !t ? items : items.filter(it => String(it[1]).indexOf(t) !== -1 || (it[3] || '').toLowerCase().indexOf(t) !== -1);
    const sig = t + '|' + (wbData ? wbData.cached_at + '|' + wbData.open + '|' + items.length : 'x');
    if (sig === wbSig) return; wbSig = sig;
    const meta = $('wbMeta');
    if (meta) {
      const liveTxt = (wbData && wbData.open) ? '<span class="live">live</span>'
        : (wbData && wbData.cached_at ? 'cached ' + new Date(wbData.cached_at * 1000).toLocaleString() : '')
          || (wbData && wbData.liveOnly ? 'live only until the launcher is rebuilt' : '');
      const total = (wbData && wbData.count) ? wbData.count : 0;
      // Capacity comes from the workbench-upgrade level varbit, which the Guild Shop tab reads.
      const cap = WB_CAP[(matUnlockVp && matUnlockVp[61463]) || 0] || 0;
      meta.innerHTML = (total ? total + (cap ? ' / ' + cap : '') + ' artefacts' : '') + (liveTxt ? '<br>' + liveTxt : '');
    }
    body.innerHTML = '';
    if (!wbData) { body.innerHTML = '<div class="bank-empty" style="display:flex"><div class="warn" style="background:rgba(255,255,255,0.04);border-color:var(--border);color:var(--text-dim)">Reading...</div></div>'; return; }
    if (!items.length) {
      body.innerHTML = '<div class="bank-empty" style="display:flex"><div class="warn">' +
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M12 9v4M12 17h.01M10.3 3.9 1.8 18a2 2 0 0 0 1.7 3h17a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0z"/></svg>' +
        '<div><b>No workbench storage cached yet.</b><br>Open your Archaeologist\'s workbench in game to populate this window.' +
        ((wbData && wbData.liveOnly) ? '<br><span class="dg-hint">Live container only &mdash; rebuild the launcher for the cached view.</span>' : '') +
        '</div></div></div>';
      return;
    }
    if (!shown.length) { body.innerHTML = '<div class="bank-empty" style="display:flex"><div class="warn" style="background:rgba(255,255,255,0.04);border-color:var(--border);color:var(--text-dim)">No item matches "' + wbTerm.replace(/[<>&"]/g, '') + '".</div></div>'; return; }
    const grid = document.createElement('div'); grid.className = 'stor-grid';
    for (const it of shown) {
      const [slot, id, stack, name] = it;
      grid.appendChild(bankCell(id, stack, name, '\nWorkbench storage (container 1008)'));
    }
    body.appendChild(grid);
  }

