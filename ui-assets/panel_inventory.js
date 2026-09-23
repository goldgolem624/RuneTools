// RuneToolsX panel: Inventory & Equipment.
(function () {

  // ---- Inventory (container 93) + Equipment (container 94) ----
  const INV_CAP = 28;
  // container-94 slot index; null = an empty grid position (visual gap). Rows:
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
      const d = JSON.parse(await rtxData.raw('state.inventory'));
      invData = (d && Array.isArray(d.items)) ? d : { present: false, count: 0, cap: 0, items: [] };
    } catch (e) { /* keep previous */ }
    invFetching = false;
    paneRun('inventory', renderInventory);
  }
  async function fetchEquip() {
    if (!bridge() || !bridge().equipment || equipFetching) return;
    equipFetching = true;
    try {
      const d = JSON.parse(await rtxData.raw('state.equipment'));
      equipData = (d && Array.isArray(d.items)) ? d : { present: false, count: 0, cap: 0, items: [] };
    } catch (e) { /* keep previous */ }
    if (bridge().containerItems) {
      try {
        const cm = JSON.parse(await rtxData.raw('state.container', 670));
        const ov = {}; for (const it of ((cm && Array.isArray(cm.items)) ? cm.items : [])) ov[it[0]] = it;
        equipCosmetics = ov;
      } catch (e) { /* keep previous */ }
    }
    equipFetching = false;
    paneRun('equipment', renderEquipment);
  }

  function invPresent() { return !!(invData && invData.present); }
  function invItems()   { return (invData && Array.isArray(invData.items)) ? invData.items : []; }
  function invUsed()    { return invItems().length; }
  function invCapacity(){ return (invData && invData.cap) ? invData.cap : INV_CAP; }

  // The game's own inventory slot gets a frame (the same highlight the quest guides use), so the
  // slot geometry and the game-drawn frames can be checked from here. Click again to clear.
  let invHlSlots = [];
  async function invHighlightSlot(slot, cell) {
    if (!bridge() || !bridge().invSlotRect || !bridge().uiHighlights) return;
    const i = invHlSlots.indexOf(slot);
    if (i >= 0) invHlSlots.splice(i, 1); else invHlSlots.push(slot);
    const recs = [];
    for (const sl of invHlSlots) {
      let r = null; try { r = JSON.parse(await bridge().invSlotRect(myPid(), sl)); } catch (e) {}
      if (r && r.w > 0) recs.push([r.x, r.y, r.w, r.h].join(','));
    }
    try { bridge().uiHighlights(myPid(), recs.join(';')); } catch (e) {}
    document.querySelectorAll('#invGrid .bank-cell').forEach(c => c.classList.toggle('inv-hl', invHlSlots.indexOf(c.dataset.slot | 0) >= 0));
  }
  // A text label the game draws over the slot, through the same channel as the plugin labels.
  let invLblSlots = {};
  async function invLabelSlot(slot) {
    if (!bridge() || !bridge().invSlotRect || !bridge().uiLabels) return;
    if (invLblSlots[slot]) delete invLblSlots[slot]; else invLblSlots[slot] = true;
    const list = [];
    for (const k in invLblSlots) {
      let r = null; try { r = JSON.parse(await bridge().invSlotRect(myPid(), k | 0)); } catch (e) {}
      if (r && r.w > 0) list.push([Math.round(r.x + r.w / 2), Math.round(r.y + r.h / 2), -1, 12, 'S' + k, 1].join(''));
    }
    try { bridge().uiLabels(myPid(), list.join(';')); } catch (e) {}
  }
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
      const hint = document.createElement('span'); hint.className = 'cnt'; hint.style.marginLeft = '10px'; hint.textContent = 'click a slot to highlight it in the game, shift-click for a label';
      hdr.appendChild(hint);
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
      const box = document.createElement('div'); box.className = 'inv-ico-box';
      const nm = document.createElement('div'); nm.className = 'inv-name';
      if (it) {
        const [slot, id, stack, name] = it;
        if (stack === 0) cell.classList.add('placeholder');
        const ico = document.createElement('div'); ico.className = 'bank-icon';
        ico.dataset.itemId = String(id); attachBankIcon(ico, id);
        box.appendChild(ico);
        const amt = fmtAmt(stack);
        if (amt.t) { const a = document.createElement('span'); a.className = 'bank-amt' + (amt.c ? ' ' + amt.c : ''); a.textContent = amt.t; box.appendChild(a); }
        nm.textContent = name || ('Item #' + id);
        cell.dataset.tip = (name || ('Item #' + id)) + '\nID ' + id + '\nx' + stack.toLocaleString() + '\nSlot ' + slot;
        cell.dataset.ei = '93:' + id + ':' + slot;
      }
      cell.appendChild(box); cell.appendChild(nm);
      cell.dataset.slot = String(s);
      cell.style.cursor = 'pointer';
      cell.addEventListener('click', (ev) => ev.shiftKey ? invLabelSlot(s) : invHighlightSlot(s, cell));
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
          cell.dataset.ei = '94:' + id + ':' + slot;
        } else {
          cell.classList.add('eq-empty');
          cell.dataset.tip = label + '\n(empty)';
        }
        const ov = equipCosmetics[sid];
        if (ov && !(it && ov[1] === it[1])) { cell.classList.add('eq-override'); cell.dataset.tip += '\nOverride: ' + (ov[3] || ('Cosmetic #' + ov[1])); }
        grid.appendChild(cell);
      }
    }
  }

Object.assign(window, { fetchEquip, fetchInv, invCapacity, invItems, invPresent, invUsed });
registerTab({ id: 'inventory', render: renderInventory, open: function () { invSig = ''; fetchInv(); } });
registerTab({ id: 'equipment', render: renderEquipment, open: function () { equipSig = ''; fetchEquip(); } });
})();
