// RuneToolsX panel: Containers (every live interface container; most exist in memory only
// while their window is open).
// Spliced inline into client.html; bare classic script sharing one global scope.

  // tab: link straight to that container's own panel. No tab: named, contents shown inline here.
  const KNOWN_CONTAINERS = {
    93:  { name: 'Backpack',          tab: 'inventory' },
    94:  { name: 'Worn equipment',    tab: 'equipment' },
    95:  { name: 'Bank',              tab: 'bank' },
    530: { name: 'Beast of Burden',   tab: 'familiar' },
    623: { name: 'Money pouch',       tab: 'storage' },
    670: { name: 'Cosmetic overrides',  tab: 'equipment' }, // surfaced on the Equipment slots (dots + tooltip)
    // GE collection box slots 1-8 (enum 1079 order): coins + items per slot
    523: { name: 'GE Collection: slot 1', tab: 'exchange' },
    524: { name: 'GE Collection: slot 2', tab: 'exchange' },
    525: { name: 'GE Collection: slot 3', tab: 'exchange' },
    526: { name: 'GE Collection: slot 4', tab: 'exchange' },
    527: { name: 'GE Collection: slot 5', tab: 'exchange' },
    528: { name: 'GE Collection: slot 6', tab: 'exchange' },
    783: { name: 'GE Collection: slot 7', tab: 'exchange' },
    784: { name: 'GE Collection: slot 8', tab: 'exchange' },
    890: { name: 'GE Favourites', tab: 'exchange' },   // matches script20898/20900 INV_TOTAL(890) star logic
    // Heartments 52860 + H'oddments 52555: the TH oddments-family currency store
    // (scripts 14481-14483); counted as owned by 13275/18491
    795: { name: 'Oddments storage (TH currencies)' },
    676: { name: 'Death: reclaim items' },
    930: { name: 'Death: overflow storage' },
    // Examine-player windows (populated while examining another player)
    742: { name: 'Examined player: equipment' },
    743: { name: 'Examined player: cosmetic overrides' },
    787: { name: 'Loot log' },
    // Player-Owned Farm pens + storage
    851: { name: 'Farm: Small pen (south)' },
    852: { name: 'Farm: Small pen (west)' },
    853: { name: 'Farm: Medium pen (east)' },
    854: { name: 'Farm: Medium pen (north)' },
    855: { name: 'Farm: Large pen (north)' },
    856: { name: 'Farm: Large pen (south)' },
    857: { name: 'Farm: Breeding pen' },
    859: { name: 'Farm: Storm barn storage' },
    858: { name: 'Metal bank',        tab: 'metalbank' },
    866: { name: 'Herb bag' },
    867: { name: 'Anachronia bait box', tab: 'baitbox' },
    885: { name: 'Material storage',  tab: 'materials' },
    891: { name: 'Brooch of the Gods', tab: 'storage' },
    895: { name: 'Plank box' },                          // inline: Storage tab's separate read is empty while the box is closed
    937: { name: 'Wood box',          tab: 'storage' },
    953: { name: 'Necromancy nexus',  tab: 'storage' },
    963: { name: 'Group bank',        tab: 'groupbank' },
    964: { name: 'Bank inventory' },                     // the bank's own inventory (NOT the group shared storage)
    974: { name: 'Nodon spike harness', tab: 'storage' },
    // Archaeologist's workbench storage. Capacity is gated by the guild-shop workbench upgrades
    // (varbit 61463 -> 125/175/225, CS2 script1020); it holds 0 until the first upgrade is bought.
    1008: { name: 'Workbench storage', tab: 'artefacts' },
  };
  let containersData = null, containersFetching = false, containersSig = '';
  const contUnknownItems = {};   // container id -> { items, cap } pulled for unknown containers

  async function fetchContainers() {
    if (!bridge() || !bridge().openContainers || containersFetching) return;
    containersFetching = true;
    try {
      const d = JSON.parse(await bridge().openContainers(myPid()));
      containersData = (d && Array.isArray(d.containers)) ? d.containers : [];
      // pull contents only for containers without a linked tab (shown inline)
      if (bridge().containerItems) {
        for (const cc of containersData) {
          const k = KNOWN_CONTAINERS[cc.id];
          if ((k && k.tab) || cc.count <= 0) continue;   // linked containers show no inline contents
          try {
            const ci = JSON.parse(await bridge().containerItems(myPid(), cc.id));
            contUnknownItems[cc.id] = { items: (ci && Array.isArray(ci.items)) ? ci.items : [], cap: (ci && ci.cap) || cc.cap };
          } catch (e) { /* keep previous */ }
        }
      }
    } catch (e) { /* keep previous */ }
    containersFetching = false;
    if (activeTab === 'containers') renderContainers();
  }

  function renderContainers() {
    const c = $('content');
    let wrap = $('contWrap');
    if (!wrap) { c.innerHTML = ''; wrap = document.createElement('div'); wrap.id = 'contWrap'; wrap.className = 'pane stor-wrap'; c.appendChild(wrap); containersSig = ''; }
    // Dedupe by id (the manager can briefly hold a duplicate entry); keep the highest count seen.
    const byId = {};
    for (const cc of (containersData || [])) { const p = byId[cc.id]; if (!p || cc.count > p.count) byId[cc.id] = cc; }
    const linked = id => { const k = KNOWN_CONTAINERS[id]; return !!(k && k.tab); };
    const ids = Object.keys(byId).map(Number).sort((a, b) =>
      (linked(a) ? 0 : 1) - (linked(b) ? 0 : 1) || a - b);
    const sig = ids.map(id => id + ':' + byId[id].count + ':' + byId[id].cap +
        (linked(id) ? '' : ':' + ((contUnknownItems[id] && contUnknownItems[id].items.length) || 0))).join(',');
    if (sig === containersSig) { sizeAllIcons(); return; }
    containersSig = sig;
    wrap.innerHTML = '';
    if (!ids.length) { wrap.innerHTML = '<div class="stor-empty">No open containers. They appear here while their window is open in-game.</div>'; return; }
    for (const id of ids) {
      const cc = byId[id], known = KNOWN_CONTAINERS[id];
      const box = document.createElement('div'); box.className = 'stor-box';
      const hdr = document.createElement('div'); hdr.className = 'scene-hdr';
      const t = document.createElement('div');
      t.innerHTML = (known ? known.name : 'Container') + ' <span class="cont-id">#' + id + '</span>';
      const cnt = document.createElement('span'); cnt.className = 'cnt';
      cnt.textContent = cc.count + (cc.cap ? ' / ' + cc.cap : '');
      hdr.appendChild(t); hdr.appendChild(cnt); box.appendChild(hdr);
      if (known && known.tab) {
        const link = document.createElement('button'); link.className = 'cont-link';
        link.textContent = 'Open ' + known.name + ' ›';
        link.addEventListener('click', () => { const tt = TABS.find(x => x.id === known.tab); if (tt) openTab(tt); });
        box.appendChild(link);
      } else {
        const ui = contUnknownItems[id];
        const items = (ui && ui.items) || [];
        if (!items.length) {
          const e = document.createElement('div'); e.className = 'stor-empty'; e.textContent = cc.count > 0 ? 'loading...' : 'empty'; box.appendChild(e);
        } else {
          const grid = document.createElement('div'); grid.className = 'stor-grid';
          for (const it of items) {
            const slot = it[0], iid = it[1], stack = it[2] || 0, name = it[3] || ('Item #' + it[1]);
            const cell = document.createElement('div'); cell.className = 'bank-cell stor-cell';
            cell.dataset.tip = name + '\nID ' + iid + '\nx' + stack.toLocaleString() + '\nSlot ' + slot;
            const url = iid ? resolveIcon(iid) : '';
            if (url) { const ico = document.createElement('div'); ico.className = 'bank-icon'; ico.dataset.itemId = String(iid); setIconBg(ico, url); cell.appendChild(ico); }
            else { const tn = document.createElement('div'); tn.className = 'stor-tn'; tn.textContent = name; cell.appendChild(tn); }
            const fa = fmtAmt(stack);
            const amt = document.createElement('span'); amt.className = 'bank-amt' + (fa.c ? ' ' + fa.c : ''); amt.textContent = fa.t || stack; cell.appendChild(amt);
            grid.appendChild(cell);
          }
          box.appendChild(grid);
        }
      }
      wrap.appendChild(box);
    }
    sizeAllIcons();
  }
