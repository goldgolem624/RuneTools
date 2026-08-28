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
    paneRun('containers', renderContainers);
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
            cell.dataset.ei = id + ':' + iid + ':' + slot;
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

  // Live Extra_ints inspector: hovering any tagged item cell (data-ei = "container:item")
  // fetches bridge().itemExtraInts and appends the raw key/value pairs to the tooltip -
  // the discovery surface for unmapped charge/state storage on items.
  document.addEventListener('mouseover', async e => {
    const cell = e.target.closest ? e.target.closest('[data-ei]') : null;
    if (!cell || cell._eiBusy) return;
    cell._eiBusy = 1;
    try {
      const parts = cell.dataset.ei.split(':');
      if (!bridge() || !bridge().itemExtraInts || !myPid()) return;
      // parts[2] = the SLOT. Two stacks of one id carry different instance vars, so an
      // id-only read showed the FIRST slot's values on every one of them (owner-caught
      // hovering two Passages of the abyss). -1 keeps the old first-match behaviour.
      const eiSlot = (parts.length > 2 && parts[2] !== '') ? +parts[2] : -1;
      const r = JSON.parse(await bridge().itemExtraInts(myPid(), +parts[0], +parts[1], eiSlot)) || {};
      const k = r.key || {};
      // A wall of "0=0 1=0 2=0 ..." buries the one key that carries anything. Lead with the
      // keys that HOLD a value, one per line, and fold the empty ones into a single tail.
      const keys = Object.keys(k).sort((a, b) => a - b);
      const set = keys.filter(x => (k[x] | 0) !== 0);
      const zero = keys.filter(x => (k[x] | 0) === 0);
      let line;
      if (!keys.length) line = 'Instance vars (Extra_ints): none';
      else if (!set.length) line = 'Instance vars (Extra_ints): all ' + keys.length + ' keys 0';
      else {
        line = 'Instance vars (Extra_ints):\n' + set.map(x => '   key ' + x + ' = ' + k[x]).join('\n');
        if (zero.length) line += '\n   (' + zero.length + ' other key' + (zero.length === 1 ? '' : 's') + ' 0)';
      }
      // Essence of Finality decode (CS2 scripts 5828/15097/670): instance key 0 = wear
      // count (varobj 18550), key 3 = stored-spec index (varobj 47702) resolved through
      // enum 15970 -> weapon obj. Charge permille = 1000 - wear/(max/1000), max = item
      // param 3385 (100,000 on EoF). Owner-validated: wear 669 + idx 76 = 99.4% Zamorak staff.
      // Degradable items (CS2 script 5828, item param 4563 = 1): instance key 0 is the WEAR
      // counter, item param 3385 the wear at which the item is fully degraded. The game shows
      // "Item Charge: X.Y%" = 1000 - wear/(max/1000) permille (never 100.0 once worn, floor 0.1
      // while any charge remains); items flagged 9308 count down "Charges remaining" instead.
      let charge = '';
      try {
        const pp = JSON.parse(await bridge().itemParams(+parts[1]) || 'null');
        const pi = (pp && pp.ints) || {};
        const mx = pi['3385'] | 0, wear = k[0] | 0;
        const ps = (pp && pp.strs) || {};
        // Fillables (urns, param 369 = 4): key 0 = stored XP, param 368 = capacity, label param
        // 6170; the game clamps the percentage to 1..99 until the urn is teleported.
        if ((pi['369'] | 0) === 4 && (pi['368'] | 0) > 0) {
          const cap = pi['368'] | 0, pct = Math.min(99, Math.max(1, Math.floor(wear * 100 / cap)));
          charge = '\n' + (ps['6170'] || 'Urn') + ' filled: ' + pct + '% (' + wear.toLocaleString() + ' / ' + cap.toLocaleString() + ' xp)';
        } else if (mx > 0 && ((pi['4563'] | 0) === 1 || (pi['1324'] | 0) === 1)) {   // both degrade paths of script 5828
          if ((pi['9308'] | 0) === 1) {
            charge = '\nCharges remaining: ' + Math.max(0, mx - wear).toLocaleString() + ' of ' + mx.toLocaleString();
          } else {
            let pm = wear === 0 ? 1000 : 1000 - Math.floor(wear / (mx / 1000));
            if (pm === 0 && (pi['5772'] | 0) === 0) pm = 1;
            if (pm === 1000 && wear !== 0) pm = 999;
            pm = Math.max(0, Math.min(1000, pm));
            charge = '\nItem charge: ' + (pm / 10).toFixed(1) + '% (' + wear.toLocaleString() + ' / ' + mx.toLocaleString() + ' wear used)';
          }
        }
      } catch (e7) {}
      let eof = '';
      if (/essence of finality/i.test(cell.dataset.tip)) {
        try {
          const wear = k[0] | 0, idx = k[3] | 0;
          let pct = '';
          try {
            const pp = JSON.parse(await bridge().itemParams(+parts[1]) || 'null');
            const mx = (pp && pp.ints && pp.ints['3385']) | 0;
            if (mx >= 1000) {
              const pm = wear === 0 ? 1000 : Math.max(0, Math.min(999, 1000 - Math.floor(wear / (mx / 1000))));
              pct = (pm / 10).toFixed(1) + '% (' + wear.toLocaleString() + '/' + mx.toLocaleString() + ' wear)';
            }
          } catch (e5) {}
          let wname = '';
          if (idx > 0 && bridge().enumInfo) {
            try {
              const en = JSON.parse(await bridge().enumInfo(15970) || '{}');
              const wid = en[idx] | 0;
              if (wid > 0) {
                const ii = JSON.parse(await bridge().itemInfo(wid) || 'null');
                wname = (ii && ii.name) ? ii.name : ('item ' + wid);
              }
            } catch (e6) {}
          }
          eof = '\nEoF: ' + (wname ? 'stored ' + wname : idx > 0 ? 'stored spec index ' + idx : 'no spec stored')
              + (pct ? '\nCharge: ' + pct : '');
        } catch (e4) {}
      }
      const base = cell.dataset.tip.split(/\n[A-Za-z ]+ filled: /)[0].split('\nItem charge:')[0].split('\nCharges remaining:')[0].split('\nEoF:')[0].split('\nInstance vars')[0].split('\nExtra_ints')[0];
      cell.dataset.tip = base + charge + eof + '\n' + line;
      if (typeof showTipFor === 'function' && cell.matches(':hover')) showTipFor(cell);
    } catch (e2) {} finally { setTimeout(() => { cell._eiBusy = 0; }, 800); }
  });
