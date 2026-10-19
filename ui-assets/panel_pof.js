// RuneToolsX panel: Player-Owned Farm pens (Manor Farm and Anachronia).
// Everything is looked up by the game's own names: the pens are the inventories enum 1943
// (POF_PADDOCK_INVENTORIES) lists, and each pen's built flag, animal slots and feeding trough are the
// varbits whose names match the pen's (POF_PADDOCK_1 <-> POF_FARM_PADDOCK_1_BUILT,
// POF_ANIMAL_PADDOCK_1_SLOT_n, POF_FEEDING_TROUGH_1_GENERAL_*). A slot holds an animal id (0 empty;
// dinosaur ids are 1000 + the value, the slots being 8 bits): enum 14314 names it with its stage.
// Traits name through enum 14339 (14340 Manor Farm, 14341 dinosaurs), food through enum 14286.
(function () {
  const E_PEN_INVS = 1943, E_ANIMAL_NAME = 14314, E_TRAIT_TABLES = 14339, E_FOOD = 14286;

  let meta = null, metaBusy = false;            // pens and their varbits, from the names and enums
  let pofData = null, pofVb = {}, pofFetching = false, pofSig = '';

  // "POF_PADDOCK_1" -> { farm: 'manor', tag: '1' }; dinosaur pens keep their size in the tag
  function penOfInv(n) {
    let m = /^POF_PADDOCK_(\d|BREEDING)$/.exec(n); if (m) return { farm: 'manor', tag: m[1] };
    m = /^POF_DINOSAUR_PADDOCK_((?:LARGE|MEDIUM|SMALL)_\d)$/.exec(n); if (m) return { farm: 'dino', tag: m[1] };
    if (n === 'POF_DINOSAUR_BREEDING') return { farm: 'dino', tag: 'BREEDING' };
    return null;
  }
  // a varbit name -> [key, role, index] for the pen it belongs to
  function roleOfVarbit(n) {
    let m;
    if ((m = /^POF_FARM_PADDOCK_(\d|BREEDING)_BUILT$/.exec(n))) return ['manor:' + m[1], 'built'];
    if ((m = /^POF_PADDOCK_DINOSAUR_(LARGE_\d|MEDIUM_\d|SMALL_\d|BREEDING)_BUILT$/.exec(n))) return ['dino:' + m[1], 'built'];
    if ((m = /^POF_ANIMAL_PADDOCK_(\d)_SLOT_(\d+)$/.exec(n))) return ['manor:' + m[1], 'slot', +m[2]];
    if ((m = /^POF_ANIMAL_BREEDING_PADDOCK_SLOT_(\d+)$/.exec(n))) return ['manor:BREEDING', 'slot', +m[1]];
    if ((m = /^POF_ANIMAL_PADDOCK_DINOSAUR_(LARGE_\d|MEDIUM_\d|SMALL_\d|BREEDING)_(\d+)$/.exec(n))) return ['dino:' + m[1], 'slot', +m[2]];
    if ((m = /^POF_FEEDING_TROUGH_(\d|BREEDING)_GENERAL_(CATEGORY|AMOUNT)$/.exec(n))) return ['manor:' + m[1], m[2] === 'CATEGORY' ? 'food' : 'amount'];
    if ((m = /^POF_FEEDING_TROUGH_DINOSAUR_(LARGE_\d|MEDIUM|SMALL|BREEDING)_(CATEGORY|AMOUNT)$/.exec(n))) {
      const t = /^(MEDIUM|SMALL)$/.test(m[1]) ? m[1] + '_1' : m[1];   // the one medium and small dinosaur trough carry no number
      return ['dino:' + t, m[2] === 'CATEGORY' ? 'food' : 'amount'];
    }
    return null;
  }
  async function enumOf(id) {
    try { return JSON.parse(await bridge().enumInfo(id) || '{}') || {}; } catch (e) { return {}; }
  }
  function cap(s) { return s ? s.charAt(0).toUpperCase() + s.slice(1) : s; }
  function animalName(e, id) {
    const n = e[String(id)];
    return n ? cap(String(n).replace(/^(an?|the) /i, '')) : 'Animal ' + id;
  }

  async function loadMeta() {
    if (meta || metaBusy || !bridge() || !bridge().enumInfo) return;
    metaBusy = true;
    try {
      const names = await rtxData.call('host.officialNames', 'varbit,inv');
      if (!names || !names.ready) { metaBusy = false; return; }   // the name table is still building; asked again on the next refresh
      const invOrder = await enumOf(E_PEN_INVS);
      const tables = await enumOf(E_TRAIT_TABLES);
      const pens = [], byKey = {};
      Object.keys(invOrder).map(Number).sort((a, b) => a - b).forEach(k => {
        const inv = invOrder[k], p = penOfInv((names.inv || {})[inv] || '');
        if (!p) return;
        const pen = { inv, farm: p.farm, tag: p.tag, key: p.farm + ':' + p.tag, built: 0, slots: [], food: 0, amount: 0 };
        pens.push(pen); byKey[pen.key] = pen;
      });
      for (const [id, n] of Object.entries(names.varbit || {})) {
        if (n.indexOf('POF_') !== 0) continue;
        const r = roleOfVarbit(n); if (!r) continue;
        const pen = byKey[r[0]]; if (!pen) continue;
        if (r[1] === 'slot') pen.slots[r[2] - 1] = +id; else pen[r[1]] = +id;
      }
      for (const p of pens) p.slots = p.slots.filter(Boolean);
      meta = {
        pens,
        ids: pens.flatMap(p => [p.built, p.food, p.amount, ...p.slots]).filter(Boolean),
        names: await enumOf(E_ANIMAL_NAME),
        food: await enumOf(E_FOOD),
        traits: { manor: await enumOf(tables['0'] || 14340), dino: await enumOf(tables['1'] || 14341) },
      };
    } catch (e) { /* asked again on the next refresh */ }
    metaBusy = false;
  }

  async function fetchPof() {
    if (!bridge() || !bridge().pof || pofFetching) return; pofFetching = true;
    try {
      await loadMeta();
      const d = JSON.parse(await rtxData.raw('state.pof'));
      pofData = (d && Array.isArray(d.pens)) ? d.pens : [];
      if (meta && meta.ids.length && bridge().varbits) pofVb = JSON.parse(await bridge().varbits(myPid(), meta.ids.join(',')) || '{}') || {};
    } catch (e) { /* keep previous */ }
    pofFetching = false;
    paneRun('pof', renderPof);
  }

  function penTitle(p) {
    if (p.tag === 'BREEDING') return 'Breeding pen';
    const m = /^(LARGE|MEDIUM|SMALL)_(\d)$/.exec(p.tag);
    if (m) return cap(m[1].toLowerCase()) + ' pen ' + m[2];
    // Manor Farm pens are numbered 1..6; their size is how many animals they hold
    const n = p.slots.length;
    const size = n >= 6 ? 'Small' : n === 4 ? 'Medium' : n === 3 ? 'Large' : '';
    return (size ? size + ' pen ' : 'Pen ') + p.tag;
  }

  function renderPof() {
    const c = $('content');
    let wrap = $('pofWrap');
    if (!wrap) { c.innerHTML = ''; wrap = document.createElement('div'); wrap.id = 'pofWrap'; wrap.className = 'pane pof-wrap'; c.appendChild(wrap); pofSig = ''; }
    if (!meta) { wrap.innerHTML = '<div class="stor-empty">Reading the farm’s names from the game cache…</div>'; pofSig = ''; return; }
    const inv = {}; for (const p of (pofData || [])) inv[p.id] = p;
    const vb = id => (id && pofVb[id] != null) ? +pofVb[id] : 0;
    const sig = meta.pens.map(p => { const q = inv[p.inv] || {}; return p.inv + ':' + vb(p.built) + ':' + vb(p.food) + ':' + vb(p.amount) + ':' + p.slots.map(vb).join('.') + ':' + (q.open ? 1 : 0) + ':' + ((q.animals || []).map(a => a[0] + '/' + a[1]).join(',')); }).join(';');
    if (sig === pofSig) { sizeAllIcons(); return; }
    pofSig = sig;
    wrap.innerHTML = '';
    for (const farm of ['manor', 'dino']) {
      const pens = meta.pens.filter(p => p.farm === farm);
      if (!pens.length) continue;
      const gh = document.createElement('div'); gh.className = 'pof-group';
      gh.textContent = farm === 'manor' ? 'Manor Farm' : 'Anachronia';
      wrap.appendChild(gh);
      const grid = document.createElement('div'); grid.className = 'pof-pens';
      for (const p of pens) grid.appendChild(penBox(p, inv[p.inv] || { animals: [], open: false, cached_at: 0 }, vb));
      wrap.appendChild(grid);
    }
    sizeAllIcons();
  }

  function penBox(p, q, vb) {
    const built = !p.built || vb(p.built) > 0;
    const box = document.createElement('div'); box.className = 'stor-box' + (built ? '' : ' pof-unbuilt');
    const hdr = document.createElement('div'); hdr.className = 'scene-hdr';
    const t = document.createElement('div');
    t.innerHTML = penTitle(p) + (q.open ? ' <span class="pof-live">live</span>' : (q.cached_at ? ' <span class="pof-cached">cached</span>' : ''));
    const animals = q.animals || [];
    const slotIds = p.slots.map(vb).map(v => v > 0 ? (p.farm === 'dino' ? v + 1000 : v) : 0);
    const inSlots = slotIds.filter(Boolean).length;
    const n = animals.length || inSlots;
    const cnt = document.createElement('span'); cnt.className = 'cnt';
    cnt.textContent = !built ? 'not built' : (n + '/' + (p.slots.length || '?'));
    hdr.appendChild(t); hdr.appendChild(cnt); box.appendChild(hdr);
    if (!built) return box;
    const food = vb(p.food), amount = vb(p.amount);
    if (p.food || p.amount) {
      const tr = document.createElement('div'); tr.className = 'stor-row';
      tr.innerHTML = '<span class="stor-n">Trough</span><span class="stor-c"></span>';
      tr.lastChild.textContent = amount ? ((meta.food[String(food)] || 'Food') + ' × ' + amount) : 'empty';
      box.appendChild(tr);
    }
    if (animals.length) {
      // the pen's inventory, captured while its interface was open: the animals with their traits
      const traits = meta.traits[p.farm] || {};
      const g = document.createElement('div'); g.className = 'pof-grid';
      for (const a of animals) {
        const item = a[0], traitId = a[1], species = a[2] || ('Item ' + a[0]);
        const trait = traitId > 0 ? (traits[String(traitId)] || ('Trait ' + traitId)) : '';
        const cell = document.createElement('div'); cell.className = 'bank-cell pof-cell';
        cell.dataset.tip = species + (trait ? '\nTrait: ' + trait : '');
        const ico = document.createElement('div'); ico.className = 'bank-icon'; ico.dataset.itemId = String(item);
        attachBankIcon(ico, item);
        cell.appendChild(ico);
        if (trait) { const tb = document.createElement('span'); tb.className = 'pof-trait'; tb.textContent = trait; cell.appendChild(tb); }
        g.appendChild(cell);
      }
      box.appendChild(g);
    } else if (inSlots) {
      // the slot vars the game sets while you are at the farm: species, breed and stage, no traits
      for (const id of slotIds) {
        if (!id) continue;
        const r = document.createElement('div'); r.className = 'stor-row';
        r.innerHTML = '<span class="stor-n"></span>';
        r.firstChild.textContent = animalName(meta.names, id);
        box.appendChild(r);
      }
    } else {
      const e = document.createElement('div'); e.className = 'stor-empty';
      e.textContent = q.cached_at ? 'Empty' : 'Visit the farm or open the pen to read it.';
      box.appendChild(e);
    }
    return box;
  }

Object.assign(window, { fetchPof });
registerTab({ id: 'pof', render: renderPof });
})();
