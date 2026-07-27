// RuneToolsX panel: Inventory & Equipment.
// Spliced inline into client.html; bare classic script sharing one global scope.

  // ---- Inventory (container 93) + Equipment (container 94) ----
  // Live containers (no disk cache like the bank): read every poll while the tab is open, or in
  // the background when an inventory alert needs them.
  const INV_CAP = 28;
  // Worn-equipment paperdoll, mirroring the in-game layout. Each entry is a
  // container-94 slot index; null = an empty grid position (visual gap). Rows:
  //   .       Head  Pocket
  //   Cape    Neck  Ammo
  //   Weapon  Torso Off-hand
  //   .       Legs  .
  //   Hands   Feet  Ring
  const EQUIP_LAYOUT = [
    [null, 0,    17],
    [1,    2,    13],
    [3,    4,    5],
    [null, 7,    null],
    [9,    10,   12],
  ];
  const EQUIP_SLOT_NAMES = { 0: 'Head', 1: 'Cape', 2: 'Neck', 3: 'Main hand', 4: 'Torso',
                             5: 'Off hand', 7: 'Legs', 9: 'Hands', 10: 'Feet', 12: 'Ring',
                             13: 'Ammo', 17: 'Pocket' };
  let invData = null, invFetching = false, invSig = '';
  let equipData = null, equipFetching = false, equipSig = '', equipCosmetics = {};

  // Re-attach icons for cells that have not painted one yet, so freshly-loaded icons appear
  // without a page switch.
  function topUpContIcons(gridId, sel) {
    const grid = document.getElementById(gridId); if (!grid) return;
    grid.querySelectorAll(sel || '.bank-icon').forEach(el => {
      if (el.style.backgroundImage) return;
      const id = Number(el.dataset.itemId);
      if (id > 0) attachBankIcon(el, id);
    });
  }

  async function fetchInv() {
    if (!bridge() || !bridge().inventory || invFetching) return;
    invFetching = true;
    try {
      const d = JSON.parse(await bridge().inventory(myPid()));
      invData = (d && Array.isArray(d.items)) ? d : { present: false, count: 0, cap: 0, items: [] };
    } catch (e) { /* keep previous */ }
    invFetching = false;
    if (activeTab === 'inventory') renderInventory();
  }
  async function fetchEquip() {
    if (!bridge() || !bridge().equipment || equipFetching) return;
    equipFetching = true;
    try {
      const d = JSON.parse(await bridge().equipment(myPid()));
      equipData = (d && Array.isArray(d.items)) ? d : { present: false, count: 0, cap: 0, items: [] };
    } catch (e) { /* keep previous */ }
    // Cosmetic overrides live in container 670, indexed by the same worn-slot number.
    if (bridge().containerItems) {
      try {
        const cm = JSON.parse(await bridge().containerItems(myPid(), 670));
        const ov = {}; for (const it of ((cm && Array.isArray(cm.items)) ? cm.items : [])) ov[it[0]] = it;
        equipCosmetics = ov;
      } catch (e) { /* keep previous */ }
    }
    equipFetching = false;
    if (activeTab === 'equipment') renderEquipment();
  }

  function invPresent() { return !!(invData && invData.present); }
  function invItems()   { return (invData && Array.isArray(invData.items)) ? invData.items : []; }
  function invUsed()    { return invItems().length; }
  function invCapacity(){ return (invData && invData.cap) ? invData.cap : INV_CAP; }

  function renderInventory() {
    const c = $('content');
    let w = document.getElementById('invWrap');
    if (!w) {
      c.innerHTML = '';
      w = document.createElement('div'); w.id = 'invWrap'; w.className = 'inv-wrap';
      const hdr = document.createElement('div'); hdr.className = 'scene-hdr';
      const t = document.createElement('div'); t.textContent = 'Inventory';
      const cnt = document.createElement('span'); cnt.className = 'cnt'; cnt.id = 'invCnt'; cnt.textContent = '...';
      hdr.appendChild(t); hdr.appendChild(cnt);
      const grid = document.createElement('div'); grid.id = 'invGrid'; grid.className = 'inv-grid';
      const empty = document.createElement('div'); empty.id = 'invEmpty'; empty.className = 'cont-empty';
      w.appendChild(hdr); w.appendChild(grid); w.appendChild(empty); c.appendChild(w); invSig = '';
    }
    paintInv();
  }
  function paintInv() {
    const grid = document.getElementById('invGrid'); if (!grid) return;
    const empty = document.getElementById('invEmpty');
    const present = invPresent();
    const items = invItems();
    const cap = invCapacity();
    const cnt = document.getElementById('invCnt');
    if (cnt) cnt.textContent = present ? (items.length + ' / ' + cap) : '...';
    const sig = present + '|' + cap + '|' + items.map(it => it[0] + ':' + it[1] + ':' + it[2]).join(',');
    if (sig === invSig) { topUpContIcons('invGrid'); return; }
    invSig = sig;
    grid.innerHTML = '';
    if (!present) {
      grid.style.display = 'none';
      if (empty) { empty.style.display = 'flex'; empty.textContent = 'Inventory appears once in-world.'; }
      return;
    }
    grid.style.display = ''; if (empty) empty.style.display = 'none';
    const bySlot = {}; for (const it of items) bySlot[it[0]] = it;
    for (let s = 0; s < cap; s++) {
      const cell = document.createElement('div'); cell.className = 'bank-cell';
      const it = bySlot[s];
      if (it) {
        const [slot, id, stack, name] = it;
        if (stack === 0) cell.classList.add('placeholder');
        const ico = document.createElement('div'); ico.className = 'bank-icon';
        ico.dataset.itemId = String(id); attachBankIcon(ico, id);
        cell.appendChild(ico);
        const amt = fmtAmt(stack);
        if (amt.t) { const a = document.createElement('span'); a.className = 'bank-amt' + (amt.c ? ' ' + amt.c : ''); a.textContent = amt.t; cell.appendChild(a); }
        cell.dataset.tip = (name || ('Item #' + id)) + '\nID ' + id + '\nx' + stack.toLocaleString() + '\nSlot ' + slot;
      }
      grid.appendChild(cell);
    }
  }

  function renderEquipment() {
    const c = $('content');
    let w = document.getElementById('eqWrap');
    if (!w) {
      c.innerHTML = '';
      w = document.createElement('div'); w.id = 'eqWrap'; w.className = 'inv-wrap';
      const hdr = document.createElement('div'); hdr.className = 'scene-hdr';
      const t = document.createElement('div'); t.textContent = 'Worn equipment';
      const cnt = document.createElement('span'); cnt.className = 'cnt'; cnt.id = 'eqCnt'; cnt.textContent = '...';
      hdr.appendChild(t); hdr.appendChild(cnt);
      const grid = document.createElement('div'); grid.id = 'eqGrid'; grid.className = 'eq-grid';
      const empty = document.createElement('div'); empty.id = 'eqEmpty'; empty.className = 'cont-empty';
      w.appendChild(hdr); w.appendChild(grid); w.appendChild(empty); c.appendChild(w); equipSig = '';
    }
    paintEquip();
  }
  function paintEquip() {
    const grid = document.getElementById('eqGrid'); if (!grid) return;
    const empty = document.getElementById('eqEmpty');
    const present = !!(equipData && equipData.present);
    const items = (equipData && Array.isArray(equipData.items)) ? equipData.items : [];
    const cnt = document.getElementById('eqCnt'); if (cnt) cnt.textContent = present ? String(items.length) : '...';
    const sig = present + '|' + items.map(it => it[0] + ':' + it[1] + ':' + it[2]).join(',') +
                '|' + Object.keys(equipCosmetics).map(s => s + ':' + equipCosmetics[s][1]).join(',');
    if (sig === equipSig) { topUpContIcons('eqGrid'); return; }
    equipSig = sig;
    grid.innerHTML = '';
    if (!present) {
      grid.style.display = 'none';
      if (empty) { empty.style.display = 'flex'; empty.textContent = 'Equipment appears once in-world.'; }
      return;
    }
    grid.style.display = ''; if (empty) empty.style.display = 'none';
    const bySlot = {}; for (const it of items) bySlot[it[0]] = it;
    for (const rowDef of EQUIP_LAYOUT) {
      for (const sid of rowDef) {
        if (sid === null) { const sp = document.createElement('div'); sp.className = 'eq-gap'; grid.appendChild(sp); continue; }
        const cell = document.createElement('div'); cell.className = 'bank-cell eq-cell';
        const label = EQUIP_SLOT_NAMES[sid] || ('Slot ' + sid);
        const it = bySlot[sid];
        if (it) {
          const [slot, id, stack, name] = it;
          const ico = document.createElement('div'); ico.className = 'bank-icon';
          ico.dataset.itemId = String(id); attachBankIcon(ico, id);
          cell.appendChild(ico);
          const amt = fmtAmt(stack);
          if (amt.t) { const a = document.createElement('span'); a.className = 'bank-amt' + (amt.c ? ' ' + amt.c : ''); a.textContent = amt.t; cell.appendChild(a); }
          cell.dataset.tip = (name || ('Item #' + id)) + '\nID ' + id + (stack > 1 ? '\nx' + stack.toLocaleString() : '') + '\n' + label;
        } else {
          cell.classList.add('eq-empty');
          cell.dataset.tip = label + '\n(empty)';
        }
        // A cosmetic entry matching the worn item is the item itself, not an override.
        const ov = equipCosmetics[sid];
        if (ov && !(it && ov[1] === it[1])) { cell.classList.add('eq-override'); cell.dataset.tip += '\nOverride: ' + (ov[3] || ('Cosmetic #' + ov[1])); }
        grid.appendChild(cell);
      }
    }
  }
