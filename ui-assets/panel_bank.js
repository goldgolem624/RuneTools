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
  let bankNameToId = null, bankIdToName = null, bankMapLen = -1, bankMapCount = 0, bankMapAt = 0;
  // (Re)build the name maps from the price mapping. Returns how many names are known: 0 until the relay's
  // mapping has arrived, at which point every memoised base id is dropped so items resolve again. Once the
  // mapping is in, it is only re-read once a minute (the mapping string is large).
  function bankMapping() {
    const now = Date.now();
    if (bankMapCount > 0 && now - bankMapAt < 60000) return bankMapCount;
    bankMapAt = now;
    let raw = '';
    try { raw = bridge().pricesMapping() || ''; } catch (e) { raw = ''; }
    if (raw.length === bankMapLen) return bankMapCount;
    bankMapLen = raw.length; bankMapCount = 0;
    bankNameToId = Object.create(null); bankIdToName = Object.create(null);
    try {
      for (const it of JSON.parse(raw || '[]')) if (it && it.name) { bankNameToId[String(it.name).toLowerCase()] = it.id; bankIdToName[it.id] = it.name; ++bankMapCount; }
    } catch (e) {}
    for (const k in bankBaseOf) delete bankBaseOf[k];
    for (const k in bankExtrasOf) delete bankExtrasOf[k];
    return bankMapCount;
  }
  // Display name for a bank row. Tiered gear (Deathwarden robe bottom, tier 30) is one item id per tier, but
  // the game's item definition names them all alike; the tradeable listing has the full name, so prefer it
  // whenever it extends the plain name.
  function bankRowName(id, name) {
    const own = String(name || '');
    if (!bankIdToName) bankMapping();
    const full = bankIdToName && bankIdToName[id];
    if (full && full.length > own.length && full.toLowerCase().indexOf(own.toLowerCase()) === 0)
      return full.replace(/\((tier|level) (\d+)\)/i, (m, w, n) => '(' + w.charAt(0).toUpperCase() + w.slice(1).toLowerCase() + ' ' + n + ')');
    return own;
  }
  function bankItemName(id) {
    if (bankIdToName && bankIdToName[id]) return bankIdToName[id];
    try { const info = JSON.parse(bridge().itemInfo(id) || '{}'); if (info && info.name) return info.name; } catch (e) {}
    return 'item #' + id;
  }
  const bankBaseOf = Object.create(null);     // item id -> base item id for pricing (or the id itself)
  function bankBaseId(id, name) {
    if (bankBaseOf[id] !== undefined) return bankBaseOf[id];
    let base = id;
    try {
      if (bankMapping() > 0) {
        const own = String(name || '').toLowerCase();
        // strip "Augmented", then any trailing dye or condition tags, repeatedly ("Augmented X (Soul)" -> X)
        let plain = own.replace(/^augmented\s+/, '').trim(), prevPlain = '';
        const TAG = /\s*\((?:augmented|broken|damaged|degraded|used|new|uncharged|shadow|barrows|third age|blood|ice|soul|aurora|sun|jungle|or|sp|red|blue|green|yellow|purple|white|black|orange|pink|cyan|grey|gray|brown)\)$/;
        while (plain !== prevPlain) { prevPlain = plain; plain = plain.replace(TAG, '').trim(); }
        // the tradeable listing may carry a state the bank item has grown out of: "The Devourer's Nexus" trades as
        // "The Devourer's Nexus (unattuned)", charged tools as "(uncharged)", and so on
        const cands = [own, plain, plain + ' (unattuned)', plain + ' (uncharged)', plain + ' (inactive)', plain + ' (empty)', plain + ' (unpowered)'];
        let hit = null;
        for (const c of cands) if (c && bankNameToId[c] != null) { hit = bankNameToId[c]; break; }
        if (hit != null && hit !== id) base = hit;
        // add-ons the item carries on top of the base piece: a dye ("(blood)") and an ornament kit ("(or)" / "(sp)")
        const extras = [];
        const dye = own.match(/\((shadow|barrows|third age|blood|ice|soul|aurora|sun|jungle)\)/);
        if (dye && bankNameToId[dye[1] + ' dye'] != null) extras.push({ label: dye[1].replace(/\b\w/g, c => c.toUpperCase()) + ' dye', id: bankNameToId[dye[1] + ' dye'] });
        const orn = own.match(/\((or|sp)\)/);
        if (orn) {
          const tag = orn[1], kitCands = [plain + ' ornament kit (' + tag + ')', plain + ' ornament kit'];
          const m1 = plain.match(/^amulet of (.+)$/); if (m1) kitCands.push(m1[1] + ' ornament kit');           // Amulet of fury -> Fury ornament kit
          const m2 = plain.match(/^(.+?) (?:necklace|amulet|ring|bracelet)$/); if (m2) kitCands.push(m2[1] + ' ornament kit');   // Reaper necklace -> Reaper ornament kit
          if (/^dragon plate(?:legs|skirt)$/.test(plain)) kitCands.push('dragon platelegs/skirt ornament kit (' + tag + ')');
          if (/^essence of finality amulet$/.test(plain)) kitCands.push('essence of finality ornament kit');
          for (const c of kitCands) if (bankNameToId[c] != null) { extras.push({ label: 'Ornament kit', id: bankNameToId[c] }); break; }
        }
        bankExtrasOf[id] = extras;
      }
    } catch (e) {}
    if (bankMapCount > 0) bankBaseOf[id] = base;   // only memoise once the mapping is in
    return base;
  }
  const bankExtrasOf = Object.create(null);   // item id -> [{ label, id }] add-ons (dye, ornament kit) priced on top of the base
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
  // An Essence of Finality amulet holds one weapon's special attack, chosen per amulet, so it is read per bank
  // slot rather than per item id: the amulet's own item var 3 is an index into enum 15970, which maps that
  // index to the weapon the special came from. The weapon's GE price is then counted on top of the amulet.
  // The per-item vars ride on each bank row as its fifth element ([[key, value], ...]); the launcher captures
  // them with the items and keeps them in the bank cache, so this works with the bank closed too.
  const BANK_EOF_ENUM = 15970, BANK_EOF_VAR = 3;
  let bankEofTable = null, bankEofLoading = false, bankEofSig = '';
  const bankEofOf = Object.create(null);      // 'slot:id' -> { weaponId, weaponName } or null when nothing is stored
  function bankIsEof(name) { return /^(?:augmented\s+)?essence of finality amulet/i.test(name || ''); }
  function bankVarOf(it, key) {               // value of per-item var `key` on a bank row, or null when the row has none
    const vars = it[4];
    if (!Array.isArray(vars)) return null;
    for (const kv of vars) if (kv && kv[0] === key) return kv[1] | 0;
    return 0;
  }
  async function bankEofRefresh(items) {
    if (!items) return;
    const sig = (bankData ? bankData.cached_at + '|' + bankData.open + '|' + items.length : 'x');
    if (sig === bankEofSig || bankEofLoading) return;
    bankEofLoading = true;
    let changed = false;
    try {
      if (!bankEofTable) { try { bankEofTable = JSON.parse(await rtxData.raw('cache.enumInfo', BANK_EOF_ENUM) || 'null'); } catch (e) {} }
      if (bankEofTable) {
        for (const it of items) {
          if (!bankIsEof(it[3])) continue;
          const key = it[0] + ':' + it[1];
          const idx = bankVarOf(it, BANK_EOF_VAR);
          let next = null;
          if (idx === null) next = undefined;                 // no vars on this row (older cache file): unknown
          else { const wid = bankEofTable[idx] > 0 ? bankEofTable[idx] : null; if (wid) next = { weaponId: wid, weaponName: bankItemName(wid) }; }
          const prevJ = key in bankEofOf ? JSON.stringify(bankEofOf[key]) : 'unknown', nextJ = next === undefined ? 'unknown' : JSON.stringify(next);
          if (prevJ !== nextJ) changed = true;
          if (next === undefined) delete bankEofOf[key]; else bankEofOf[key] = next;
        }
        bankEofSig = sig;
      }
    } catch (e) {}
    bankEofLoading = false;
    if (changed) { bankPaintSig = ''; paintBankPage(); if (bankOverlayOn) bankOverlayTick(); }
  }
  // Rune pouches carry their runes in the pouch's own item vars: var 1 packs four 6-bit rune slots (bits 0-5 =
  // first slot; 0 = empty, otherwise a key of enum 11885, which maps it to the rune item), and vars 0, 2, 3, 4
  // hold the four quantities in that order. Each pouch has its own runes, so this is per bank slot as well.
  const BANK_RUNE_ENUM = 11885, BANK_POUCH_QTY_VARS = [0, 2, 3, 4];
  let bankRuneTable = null, bankRuneLoading = false;
  // Pernix's quivers carry two ammo stacks in their item vars: var 0 = (second type << 23) | (first count << 8)
  // | first type, var 1 = second count (low 16 bits); the type is a key of enum 16608 (ammo item ids).
  const BANK_AMMO_ENUM = 16608;
  let bankAmmoTable = null, bankAmmoLoading = false;
  function bankIsQuiver(name) { return /\bquiver\b/i.test(name || ''); }
  function bankQuiverAmmo(it) {              // [{ id, name, qty }] for a quiver row, [] when empty or unknown
    if (!bankAmmoTable || !Array.isArray(it[4])) return [];
    let k0 = 0, k1 = 0;
    for (const kv of it[4]) { if (!kv) continue; if (kv[0] === 0) k0 = kv[1] >>> 0; else if (kv[0] === 1) k1 = kv[1] >>> 0; }
    const pType = k0 & 0xFF, pCount = (k0 >>> 8) & 0x7FFF, sType = (k0 >>> 23) & 0x1FF, sCount = k1 & 0xFFFF;
    const out = [];
    if (pType && bankAmmoTable[pType] > 0 && pCount > 0) out.push({ id: bankAmmoTable[pType], name: bankItemName(bankAmmoTable[pType]), qty: pCount });
    if (sType && bankAmmoTable[sType] > 0 && sCount > 0) out.push({ id: bankAmmoTable[sType], name: bankItemName(bankAmmoTable[sType]), qty: sCount });
    return out;
  }
  function bankAmmoTableLoad() {
    if (bankAmmoTable || bankAmmoLoading) return;
    bankAmmoLoading = true;
    Promise.resolve().then(async () => {
      try { bankAmmoTable = JSON.parse(await rtxData.raw('cache.enumInfo', BANK_AMMO_ENUM) || 'null'); } catch (e) {}
      bankAmmoLoading = false;
      if (bankAmmoTable) { bankPaintSig = ''; paintBankPage(); if (bankOverlayOn) bankOverlayTick(); }
    });
  }
  function bankIsRunePouch(name) { return /rune pouch/i.test(name || ''); }
  function bankPouchRunes(it) {                // [{ id, name, qty }] for a rune pouch row, [] when empty or unknown
    if (!bankRuneTable || !Array.isArray(it[4])) return [];
    let packed = 0; const qty = [0, 0, 0, 0];
    for (const kv of it[4]) {
      if (!kv) continue;
      if (kv[0] === 1) packed = kv[1] >>> 0;
      const qi = BANK_POUCH_QTY_VARS.indexOf(kv[0]); if (qi >= 0) qty[qi] = kv[1] | 0;
    }
    const out = [];
    for (let i = 0; i < 4; ++i) {
      const idx = (packed >>> (6 * i)) & 63, rid = idx > 0 ? bankRuneTable[idx] : null;
      if (rid > 0 && qty[i] > 0) out.push({ id: rid, name: bankItemName(rid), qty: qty[i] });
    }
    return out;
  }
  function bankRuneTableLoad() {
    if (bankRuneTable || bankRuneLoading) return;
    bankRuneLoading = true;
    Promise.resolve().then(async () => {
      try { bankRuneTable = JSON.parse(await rtxData.raw('cache.enumInfo', BANK_RUNE_ENUM) || 'null'); } catch (e) {}
      bankRuneLoading = false;
      if (bankRuneTable) { bankPaintSig = ''; paintBankPage(); if (bankOverlayOn) bankOverlayTick(); }
    });
  }
  // Necromancy nexus: every nexus item shares the account's necrotic runes (container 953, cached by the
  // launcher like the bank), so the runes are counted once, on the first nexus in bank order; the other nexus
  // rows point at it. Items with "Fill / Check contents / Retrieve contents" on a Necromancy piece are the nexus.
  let bankNexusData = null, bankNexusAt = 0, bankNexusBusy = false;
  function bankIsNexus(name) { return /\bnexus$/i.test(name || ''); }
  function bankNexusRefresh() {
    if (bankNexusBusy || !bridge() || !bridge().nexusItems || !myPid()) return;
    if (Date.now() - bankNexusAt < 5000) return;
    bankNexusBusy = true;
    Promise.resolve().then(async () => {
      try {
        const d = JSON.parse(await bridge().nexusItems(myPid()));
        if (d && Array.isArray(d.items)) {
          const sig = JSON.stringify(d.items);
          if (!bankNexusData || JSON.stringify(bankNexusData.items) !== sig) { bankNexusData = d; bankPaintSig = ''; paintBankPage(); if (bankOverlayOn) bankOverlayTick(); }
          else bankNexusData = d;
        }
      } catch (e) {}
      bankNexusAt = Date.now(); bankNexusBusy = false;
    });
  }
  function bankNexusHost() {                // slot of the nexus row that carries the shared runes
    const items = (bankData && bankData.items) ? bankData.items : [];
    let host = null;
    for (const it of items) if (bankIsNexus(it[3]) && (it[2] | 0) > 0 && (host === null || it[0] < host)) host = it[0];
    return host;
  }
  function bankNexusRunes() {               // [{ id, name, qty }] from the cached container
    const out = [];
    for (const it of ((bankNexusData && bankNexusData.items) || [])) if (it[1] > 0 && (it[2] | 0) > 0) out.push({ id: it[1], name: it[3] || bankItemName(it[1]), qty: it[2] | 0 });
    return out;
  }
  // GE value of one bank row and, when it is built from more than one price, the parts behind it (for the tooltip):
  // { item, extras: [{ label, price }] }. slot picks up the per-amulet Essence of Finality special.
  function bankGeOf(id, name, slot, row) {
    const prices = bankPrices();
    let v = bankPriceAt(prices && prices[id]), parts = null;
    const b = (v == null && name) ? bankBaseId(id, name) : id;
    if (v == null && b !== id) v = bankPriceAt(prices && prices[b]);
    if (v != null && b !== id) {
      parts = { item: v, extras: [] };
      for (const ex of (bankExtrasOf[id] || [])) {     // dyes and ornament kits sit on top of the base piece
        const pv = bankPriceAt(prices && prices[ex.id]);
        if (pv != null) { parts.extras.push({ label: ex.label, price: pv }); v += pv; }
      }
    }
    if (bankIsNexus(name) && slot != null && bankNexusHost() === slot) {
      bankNexusRefresh();
      for (const rn of bankNexusRunes()) {      // the account's necrotic runes, counted on this nexus only
        const pv = bankPriceAt(prices && prices[rn.id]);
        if (pv == null) continue;
        if (!parts) parts = { item: v, extras: [] };
        parts.extras.push({ label: rn.qty.toLocaleString() + ' ' + rn.name + (rn.qty === 1 ? '' : 's'), price: pv * rn.qty, kind: 'nexus', id: rn.id });
        v = (v || 0) + pv * rn.qty;
      }
    }
    if (bankIsQuiver(name) && row) {
      if (!bankAmmoTable) bankAmmoTableLoad();
      for (const am of bankQuiverAmmo(row)) {    // the ammo inside counts at its own GE price times quantity
        const pv = bankPriceAt(prices && prices[am.id]);
        if (pv == null) continue;
        if (!parts) parts = { item: v, extras: [] };
        parts.extras.push({ label: am.qty.toLocaleString() + ' ' + am.name, price: pv * am.qty, kind: 'ammo', id: am.id });
        v = (v || 0) + pv * am.qty;
      }
    }
    if (bankIsRunePouch(name) && row) {
      if (!bankRuneTable) bankRuneTableLoad();
      for (const rn of bankPouchRunes(row)) {   // the runes inside count at their own GE price times quantity
        const pv = bankPriceAt(prices && prices[rn.id]);
        if (pv == null) continue;
        if (!parts) parts = { item: v, extras: [] };
        parts.extras.push({ label: rn.qty.toLocaleString() + ' ' + rn.name + (rn.qty === 1 ? '' : 's'), price: pv * rn.qty, kind: 'rune', id: rn.id });
        v = (v || 0) + pv * rn.qty;
      }
    }
    const eof = slot != null ? bankEofOf[slot + ':' + id] : null;
    if (eof && eof.weaponId) {
      let pv = bankPriceAt(prices && prices[eof.weaponId]);
      if (pv == null) { const wb = bankBaseId(eof.weaponId, eof.weaponName); if (wb !== eof.weaponId) pv = bankPriceAt(prices && prices[wb]); }
      if (pv != null) {
        if (!parts) parts = { item: v, extras: [] };
        parts.extras.push({ label: 'Stored special attack (' + eof.weaponName + ')', price: pv, kind: 'eof' });
        v = (v || 0) + pv;
      }
    }
    return { v: v, parts: parts };
  }
  // per-item valuation for a row [slot, id, stack, name]
  function bankValue(it) {
    const id = it[1], stack = it[2] | 0;
    const g = bankGeOf(id, it[3], it[0], it), ge = g.v, ha = bankHaOf(id);
    return { ge: ge, ha: ha, geTotal: ge != null ? ge * stack : 0, haTotal: ha != null ? ha * stack : 0, parts: g.parts };
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

  // ---- bank tabs ----
  // The game keeps the tab layout in vars: varbit 45143+i is the size of the i-th tab block in container
  // order (creation order), and varbit 45175+(t-2) is the creation id (block index + 2) of the tab shown
  // at display position t. The main tab (position 1) is every slot after the blocks. Names ("Tab N - Name")
  // and icons come from the bank interface while it is open (517:203 headers, 517:170 tab bar whose child
  // sub is the display position; the all-tabs button is not one of them), keyed by creation id so they
  // survive reordering, and are cached per character in
  // the durable prefs. Slot membership is therefore always live: it follows every deposit and withdrawal.
  const BANK_VB_SIZE0 = 45143, BANK_VB_ORDER0 = 45175, BANK_TABS_MAX = 32;
  let bankTabsData = null, bankTabsMeta = null, bankTabsVb = null, bankTabsVbAt = 0, bankTabsVbBusy = false, bankTab = 0;
  let bankTabsMetaAt = -1, bankTabsMetaBusy = false;
  const bankSprUrl = Object.create(null);
  function bankTabsKey() { return (bankData && bankData.character) ? String(bankData.character).toLowerCase() : ''; }
  function bankTabsMetaLoad() {
    const key = bankTabsKey(); if (!key) return;
    if (bankTabsMeta && bankTabsMeta.character === key) return;
    bankTabsMeta = { character: key, byCid: {} };
    try {
      const all = JSON.parse(prefGet('rtxBankTabs', '{}') || '{}');
      if (all && all[key] && all[key].byCid && all[key].v === 2) bankTabsMeta.byCid = all[key].byCid;
    } catch (e) {}
  }
  function bankTabsMetaSave() {
    const key = bankTabsKey(); if (!key || !bankTabsMeta) return;
    let all = {};
    try { all = JSON.parse(prefGet('rtxBankTabs', '{}') || '{}') || {}; } catch (e) { all = {}; }
    all[key] = { v: 2, byCid: bankTabsMeta.byCid };
    prefSet('rtxBankTabs', JSON.stringify(all));
  }
  function bankTabsVbRefresh() {                 // the layout vars, at most every 2 s
    if (bankTabsVbBusy || Date.now() - bankTabsVbAt < 2000 || !bridge() || !bridge().varbits || !myPid()) return;
    bankTabsVbBusy = true;
    const ids = [];
    for (let i = 0; i < BANK_TABS_MAX; ++i) { ids.push(BANK_VB_SIZE0 + i); ids.push(BANK_VB_ORDER0 + i); }
    Promise.resolve().then(async () => {
      try {
        const v = JSON.parse(await bridge().varbits(myPid(), ids.join(',')) || '{}');
        const sizes = [], order = [];
        for (let i = 0; i < BANK_TABS_MAX; ++i) { sizes.push(v[BANK_VB_SIZE0 + i] | 0); order.push(v[BANK_VB_ORDER0 + i] | 0); }
        const sig = sizes.join(',') + '|' + order.join(',');
        if (!bankTabsVb || bankTabsVb.sig !== sig) { bankTabsVb = { sizes: sizes, order: order, sig: sig }; bankTabsBuild(); bankPaintSig = ''; paintBankPage(); }
      } catch (e) {}
      bankTabsVbAt = Date.now(); bankTabsVbBusy = false;
    });
  }
  function bankTabsBuild() {
    const vb = bankTabsVb; if (!vb) { bankTabsData = null; return; }
    bankTabsMetaLoad();
    const starts = []; let acc = 0;
    for (let i = 0; i < vb.sizes.length; ++i) { starts.push(acc); acc += vb.sizes[i]; }
    const tabs = [{ n: 1, cid: 1, from: acc, count: -1 }];       // main tab: every slot from the end of the blocks
    let nBlocks = 0; vb.sizes.forEach((sz, i) => { if (sz > 0) nBlocks = i + 1; });   // blocks in use (trailing zeros are spare)
    const seen = new Set();                                        // the order table carries stray values past the last tab
    for (let t = 2; t < 2 + nBlocks; ++t) {
      const cid = vb.order[t - 2]; if (!cid) break;
      const b = cid - 2; if (b < 0 || b >= nBlocks || seen.has(cid)) break;
      seen.add(cid);
      tabs.push({ n: t, cid: cid, from: starts[b], count: vb.sizes[b] });
    }
    const slotTab = [], orderIdx = [];               // orderIdx: position on the game's own scroll (main tab first)
    tabs.forEach((t, p) => { if (t.count > 0) for (let s = t.from; s < t.from + t.count; ++s) { slotTab[s] = t.n; orderIdx[s] = 1e7 + p * 1e5 + (s - t.from); } });
    for (const t of tabs) { const m = bankTabsMeta && bankTabsMeta.byCid[t.cid]; t.name = m && m.name ? m.name : ''; t.icon = m && m.icon ? m.icon : null; }
    bankTabsData = { tabs: tabs, slotTab: slotTab, orderIdx: orderIdx, mainFrom: acc };
  }
  function bankSlotTab(slot) {
    const td = bankTabsData; if (!td) return 0;
    if (td.slotTab[slot] !== undefined) return td.slotTab[slot];
    return slot >= td.mainFrom ? 1 : 0;
  }
  function bankOrderIndex() {
    const td = bankTabsData; if (!td) return null;
    return s => td.orderIdx[s] !== undefined ? td.orderIdx[s] : (s >= td.mainFrom ? s - td.mainFrom : 5e7 + s);
  }
  function bankTabName(n) {
    const t = bankTabsData && bankTabsData.tabs ? bankTabsData.tabs.find(tt => tt.n === n) : null;
    if (n === 1) return 'Main tab';
    return 'Tab ' + n + (t && t.name ? ' - ' + t.name : '');
  }
  function bankTabLine(slot) {
    const t = bankSlotTab(slot);
    return t ? '\n' + bankTabName(t) : '';
  }
  // Names and icons from the open bank window; any view will do, headers are read only when drawn.
  function bankTabsMetaDiscover() {
    if (bankTabsMetaBusy || !bankData || !bankData.open || !bankTabsVb) return;
    if (bankTabsMetaAt === bankData.cached_at) return;
    bankTabsMetaBusy = true;
    try {
      const d = JSON.parse(rtxData.sync('state.interface', 517, '170,203') || '{}');
      if (!d || !d.open || !Array.isArray(d.comps)) return;
      bankTabsMetaLoad();
      if (!bankTabsMeta) return;
      const cidAt = t => t === 1 ? 1 : (bankTabsVb.order[t - 2] || 0);
      let changed = false;
      for (const c of d.comps) {
        if (c.comp === 203 && c.sub >= 0 && c.vis && c.text) {
          const m = /^Tab (\d+)(?:\s*-\s*(.*))?$/.exec(String(c.text).replace(/<[^>]*>/g, '').trim());
          if (!m) continue;
          const cid = cidAt(+m[1]); if (!cid) continue;
          const name = (m[2] || '').trim();
          const e = bankTabsMeta.byCid[cid] || (bankTabsMeta.byCid[cid] = {});
          if (e.name !== name) { e.name = name; changed = true; }
        } else if (c.comp === 170 && c.sub >= 2 && (c.obj > 0 || c.spr > 0)) {
          const cid = cidAt(c.sub); if (!cid) continue;
          const icon = c.obj > 0 ? { item: c.obj } : { spr: c.spr };
          const e = bankTabsMeta.byCid[cid] || (bankTabsMeta.byCid[cid] = {});
          if (JSON.stringify(e.icon) !== JSON.stringify(icon)) { e.icon = icon; changed = true; }
        }
      }
      bankTabsMetaAt = bankData.cached_at;
      if (changed) { bankTabsMetaSave(); bankTabsBuild(); bankPaintSig = ''; }
    } catch (e) {} finally { bankTabsMetaBusy = false; }
  }
  function bankSpriteInto(el, sid) {
    if (bankSprUrl[sid]) { el.style.backgroundImage = 'url(' + bankSprUrl[sid] + ')'; return; }
    if (bankSprUrl[sid] === '') return;
    bankSprUrl[sid] = '';
    Promise.resolve().then(async () => {
      try { const u = await rtxData.raw('cache.sprite', sid); if (u) { bankSprUrl[sid] = u; el.style.backgroundImage = 'url(' + u + ')'; } } catch (e) {}
    });
  }
  // The strip mirrors the game's tab bar: the all-tabs button, then one icon per tab in display order.
  function bankTabsStrip() {
    const el = document.getElementById('bankTabs'); if (!el) return;
    const td = bankTabsData;
    if (!td || td.tabs.length < 2) { el.innerHTML = ''; el.hidden = true; if (bankTab) bankTab = 0; return; }
    el.hidden = false;
    const counts = {}; let total = 0;
    for (const it of ((bankData && bankData.items) || [])) { const t = bankSlotTab(it[0]); if (t) counts[t] = (counts[t] || 0) + 1; ++total; }
    if (bankTab && !td.tabs.some(t => t.n === bankTab)) bankTab = 0;
    el.innerHTML = '';
    const strip = document.createElement('div'); strip.className = 'bank-tabstrip';
    const mk = (n, icon, label, count) => {
      const b = document.createElement('button'); b.type = 'button'; b.className = 'bank-tab' + (n === bankTab ? ' on' : '');
      b.title = label + ' (' + count + ' item' + (count === 1 ? '' : 's') + ')';
      if (!n) { b.classList.add('all'); b.textContent = String.fromCharCode(8734); }
      else if (icon && icon.item) { b.dataset.itemId = String(icon.item); attachBankIcon(b, icon.item); }
      else if (icon && icon.spr) bankSpriteInto(b, icon.spr);
      else b.textContent = String(n);
      b.addEventListener('click', () => { bankTab = n; bankPage = 0; bankPaintSig = ''; paintBankPage(); });
      return b;
    };
    strip.appendChild(mk(0, null, 'All tabs', total));
    for (const t of td.tabs) if (t.n !== 1) strip.appendChild(mk(t.n, t.icon, bankTabName(t.n), counts[t.n] || 0));   // the game has no button for the main tab: the all view shows it first
    // One row like the game's bar: cells shrink to fit the width down to a floor, past which the row scrolls
    // behind a pair of arrows.
    const row = document.createElement('div'); row.className = 'bank-tabrow';
    const left = document.createElement('button'); left.type = 'button'; left.className = 'bank-tabarrow'; left.textContent = String.fromCharCode(8249); left.title = 'Earlier tabs';
    const right = document.createElement('button'); right.type = 'button'; right.className = 'bank-tabarrow'; right.textContent = String.fromCharCode(8250); right.title = 'Later tabs';
    row.appendChild(left); row.appendChild(strip); row.appendChild(right);
    el.appendChild(row);
    const fit = () => {
      const n = strip.children.length, gap = 2, avail = row.clientWidth - 2;
      let w = Math.floor((avail + gap) / Math.max(1, n)) - gap;
      const scrolls = w < 30;
      w = Math.max(30, Math.min(52, w));                        // grow into a wide panel, shrink into a narrow one
      strip.style.setProperty('--tab-w', w + 'px');
      strip.style.setProperty('--tab-h', Math.round(w * 0.9) + 'px');
      row.classList.toggle('scrolls', scrolls);
      const sync = () => { left.disabled = strip.scrollLeft <= 0; right.disabled = strip.scrollLeft + strip.clientWidth >= strip.scrollWidth - 1; };
      left.onclick = () => { strip.scrollBy({ left: -strip.clientWidth, behavior: 'smooth' }); setTimeout(sync, 350); };
      right.onclick = () => { strip.scrollBy({ left: strip.clientWidth, behavior: 'smooth' }); setTimeout(sync, 350); };
      strip.onscroll = sync;
      const on = strip.querySelector('.bank-tab.on'); if (on && scrolls) { try { on.scrollIntoView({ inline: 'nearest', block: 'nearest' }); } catch (e) {} }
      sync();
    };
    fit();
    if (!el._fitBound) { el._fitBound = true; window.addEventListener('resize', () => { const r = el.querySelector('.bank-tabrow'); if (r) fit(); }); }
    const cap = document.createElement('div'); cap.className = 'bank-tabcap';
    const cur = bankTab ? td.tabs.find(t => t.n === bankTab) : null;
    cap.textContent = cur ? bankTabName(cur.n) + ' (' + (counts[cur.n] || 0) + ' items)' : 'All tabs (' + total + ' items)';
    el.appendChild(cap);
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
    let list = !t ? items.slice() : items.filter(it => String(it[1]).indexOf(t) !== -1 ||
                              bankRowName(it[1], it[3]).toLowerCase().indexOf(t) !== -1);
    if (bankTab && bankTabsData) list = list.filter(it => bankSlotTab(it[0]) === bankTab);
    const s = BANK_SORTS[bankSort] || BANK_SORTS.slot;
    // placeholders (quantity 0) hold no value, so every value or quantity sort sinks them to the end
    if (bankSort !== 'slot') list.sort((a, b) => ((b[2] | 0) > 0) - ((a[2] | 0) > 0) || s.cmp(a, b) || a[0] - b[0]);
    else { const idx = bankOrderIndex(); if (idx) list.sort((a, b) => idx(a[0]) - idx(b[0]) || a[0] - b[0]); }
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
  // Why the in-game label is or is not showing, shown next to the toggle so a blank title bar can be read.
  let bankOverlayWhy = '';
  function bankOverlayStatus(msg) {
    bankOverlayWhy = msg || '';
    const el = document.getElementById('bankOvWhy');
    if (el) { el.textContent = bankOverlayOn ? bankOverlayWhy : ''; el.title = bankOverlayOn ? bankOverlayWhy : ''; }
  }
  async function bankOverlayTick() {
    if (!bankOverlayOn || !bridge() || bankOverlayBusy) return;
    if (!bridge().uiLabels) { bankOverlayStatus('needs the newer launcher build'); return; }
    bankOverlayBusy = true;
    try {
      if (!paneVisible('bank')) {                          // the tab's own fetch is idle: pull the bank ourselves
        try { const d = JSON.parse(await bridge().bankItems(myPid())); if (d && Array.isArray(d.items)) bankData = d; } catch (e) {}
      }
      if (!bankData || !bankData.open || !bankData.items || !bankData.items.length) { bankOverlayClear(); bankOverlayStatus('bank is closed'); return; }
      bankEofRefresh(bankData.items);
      // The title bar is part of the bank group itself: layer 517:311 holds the "Bank of Gielinor" text as a
      // dynamic child (723x40 across the top). Right-align the totals inside that bar, clear of the info icon.
      let title = null, why = '';
      try {
        const d = JSON.parse(rtxData.sync('state.interface', 517, '311') || '{}');
        if (!d || !d.open) why = 'bank window 517 not open';
        else if (!d.hasAbs) why = 'bank window position unknown';
        else if (!Array.isArray(d.comps) || !d.comps.length) why = 'title layer 517:311 not found';
        else {
          title = d.comps.find(c => c.sub >= 0 && c.w > 300 && /bank/i.test(c.text || '')) || d.comps.find(c => c.sub === -1 && c.w > 300) || null;
          if (!title) why = 'title bar not found among ' + d.comps.length + ' comps (' + d.comps.slice(0, 3).map(c => c.sub + ':' + c.w + 'x' + c.h + ' ' + (c.text || '').slice(0, 12)).join(', ') + ')';
        }
      } catch (e) { why = 'interface read failed: ' + (e && e.message ? e.message : e); }
      if (!title) { bankOverlayClear(); bankOverlayStatus(why); return; }
      const t = bankTotals(bankData.items);
      const text = '( GE ' + fmtGp(t.ge) + ' | HA ' + fmtGp(t.ha) + ' )';
      // same colour as the title text itself (the game's interface orange when the text comp carries none)
      const rgb = (title.sub >= 0 && title.col > 0) ? title.col : 0xFF981F;
      // on the title line, at its left end: the centred "Bank of Gielinor" text and the search suffix the
      // game appends to it both sit to the right
      const x = title.x + 14, y = title.y + Math.round((title.h || 40) / 2);
      const ok = rtxData.sync('overlay.uiLabels', x + '\x1f' + y + '\x1f' + rgb + '\x1f13\x1f' + text + '\x1f0');
      bankOverlayShown = true;
      bankOverlayStatus((ok === false ? 'label rejected by the launcher' : 'drawing') + ' at ' + x + ',' + y + ' (title ' + title.x + ',' + title.y + ' ' + title.w + 'x' + title.h + ')');
    } catch (e) { bankOverlayStatus('failed: ' + (e && e.message ? e.message : e)); } finally { bankOverlayBusy = false; }
  }
  function bankOverlaySet(on) {
    bankOverlayOn = !!on; prefSet('rtxBankOverlay', on ? '1' : '0');
    if (on && bridge() && !bridge().uiLabels) { try { uiNotify('In-game totals need the newer launcher build: rebuild and relaunch RuneToolsX', { ttl: 8000 }); } catch (e) {} }
    if (bankOverlayTimer) { clearInterval(bankOverlayTimer); bankOverlayTimer = 0; }
    if (on) { bankOverlayTimer = setInterval(bankOverlayTick, 2000); bankOverlayTick(); }
    else { bankOverlayClear(); bankOverlayStatus(''); }
  }
  if (bankOverlayOn) setTimeout(() => bankOverlaySet(true), 2500);
  function bankApplyDurablePrefs() {
    bankBasis = prefGet('rtxBankPriceBasis', 'buy');
    bankSort = prefGet('rtxBankSort', 'slot');
    const on = prefGet('rtxBankOverlay', '0') === '1';
    if (on !== bankOverlayOn) bankOverlaySet(on);
    bankTabsMeta = null; bankPaintSig = '';
    const w = document.getElementById('bankWrap');          // the tools row was built from the old values
    if (w) { w.remove(); try { if (paneVisible('bank')) renderBank(); } catch (e) {} }
  }

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
    const r = uiScreenRect(anchor), W = window.innerWidth, H = window.innerHeight, SB = 20;   // zoom-corrected
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
      tools.appendChild(sortBtn); tools.appendChild(basisBtn); tools.appendChild(ovSeg); tools.appendChild(totals);
      const ovWhy = document.createElement('span'); ovWhy.id = 'bankOvWhy'; ovWhy.className = 'bank-ovwhy'; tools.appendChild(ovWhy);

      const grid = document.createElement('div'); grid.id = 'bankGrid'; grid.className = 'bank-grid';

      const empty = document.createElement('div'); empty.id = 'bankEmpty'; empty.className = 'bank-empty';

      const pager = document.createElement('div'); pager.className = 'bank-pager';
      const prev = document.createElement('button'); prev.id = 'bankPrev'; prev.textContent = String.fromCharCode(8249); prev.title = 'Previous page';
      const pg   = document.createElement('span');   pg.id = 'bankPg'; pg.className = 'pg';
      const next = document.createElement('button'); next.id = 'bankNext'; next.textContent = String.fromCharCode(8250); next.title = 'Next page';
      prev.addEventListener('click', () => { bankPage--; paintBankPage(); });
      next.addEventListener('click', () => { bankPage++; paintBankPage(); });
      pager.appendChild(prev); pager.appendChild(pg); pager.appendChild(next);

      const tabsEl = document.createElement('div'); tabsEl.id = 'bankTabs'; tabsEl.className = 'bank-tabs'; tabsEl.hidden = true;
      wrap.appendChild(top); wrap.appendChild(tabsEl); wrap.appendChild(tools); wrap.appendChild(grid); wrap.appendChild(empty); wrap.appendChild(pager);
      c.appendChild(wrap);
      bankPaintSig = '';
      try { bridge().pricesCached(); bridge().pricesMapping(); } catch (e) {}   // kick the price fetches so values fill in on the first paint
    }
    paintBankPage();
  }

  // The GE block of a tooltip: the figure, then one line per part when the price is built from several
  // (the item itself, a dye, an ornament kit, the runes inside a pouch, the special stored in an amulet).
  function bankGeLines(v, stack, name, slot, id, it) {
    let s = '\nGE ' + (v.ge != null ? v.ge.toLocaleString() + ' ea' + (stack > 1 ? ' · ' + fmtGp(v.geTotal) + ' total' : '') : 'no price');
    if (v.parts) {
      if (!v.parts.extras.length) s += ' (priced as the tradeable base item)';
      else {
        s += '\n- ' + (bankIsRunePouch(name) ? 'Pouch' : bankIsEof(name) ? 'Amulet' : bankIsNexus(name) ? 'Nexus' : bankIsQuiver(name) ? 'Quiver' : 'Item') + ': ' + (v.parts.item != null ? fmtGp(v.parts.item) : 'no price');
        for (const e of v.parts.extras) s += '\n- ' + e.label + ': ' + fmtGp(e.price);
      }
    }
    if (bankIsEof(name)) { const l = bankEofLine(slot, id, v); if (l) s += '\n- ' + l; }
    if (bankIsRunePouch(name)) { const l = bankPouchLine(it, v); if (l) s += '\n- ' + l; }
    if (bankIsNexus(name)) { const l = bankNexusLine(slot, v); if (l) s += '\n- ' + l; }
    if (bankIsQuiver(name)) { const l = bankQuiverLine(it, v); if (l) s += '\n- ' + l; }
    return s;
  }
  // Quiver note: only what the per-line breakdown could not say (unknown, empty, or ammo without a price).
  function bankQuiverLine(it, v) {
    if (!Array.isArray(it[4])) return 'Ammo inside: not known yet (open the bank once so the quiver is read)';
    if (!bankAmmoTable) return 'Ammo inside: reading the ammo table';
    const ammo = bankQuiverAmmo(it);
    if (!ammo.length) return 'Ammo inside: none';
    const priced = new Set(((v && v.parts) ? v.parts.extras : []).filter(e => e.kind === 'ammo').map(e => e.id));
    const missing = ammo.filter(am => !priced.has(am.id));
    return missing.length ? 'No GE price for: ' + missing.map(am => am.qty.toLocaleString() + ' ' + am.name).join(', ') : '';
  }
  // Nexus note: where the shared necrotic runes are counted, or why they are not.
  function bankNexusLine(slot, v) {
    const host = bankNexusHost();
    if (host !== null && host !== slot) return 'Necrotic runes: shared by every nexus, counted on the nexus in slot ' + host;
    if (!bankNexusData) { bankNexusRefresh(); return 'Necrotic runes: not known yet (open a nexus once so they are read)'; }
    const runes = bankNexusRunes();
    if (!runes.length) return 'Necrotic runes: none stored';
    const priced = new Set(((v && v.parts) ? v.parts.extras : []).filter(e => e.kind === 'nexus').map(e => e.id));
    const missing = runes.filter(rn => !priced.has(rn.id));
    return (missing.length ? 'No GE price for: ' + missing.map(rn => rn.qty.toLocaleString() + ' ' + rn.name).join(', ') + '. ' : '') + 'Shared by every nexus, counted once here';
  }
  // Rune pouch note: only what the per-line breakdown could not say (unknown, empty, or runes without a price).
  function bankPouchLine(it, v) {
    if (!Array.isArray(it[4])) return 'Runes inside: not known yet (open the bank once so the pouch is read)';
    if (!bankRuneTable) return 'Runes inside: reading the rune table';
    const runes = bankPouchRunes(it);
    if (!runes.length) return 'Runes inside: none';
    const priced = new Set(((v && v.parts) ? v.parts.extras : []).filter(e => e.kind === 'rune').map(e => e.id));
    const missing = runes.filter(rn => !priced.has(rn.id));
    return missing.length ? 'No GE price for: ' + missing.map(rn => rn.qty.toLocaleString() + ' ' + rn.name).join(', ') : '';
  }
  // Tooltip line for an Essence of Finality: which special attack it holds and where that price comes from.
  function bankEofLine(slot, id, v) {
    const key = slot + ':' + id;
    if (bankEofOf[key] === undefined) return 'Stored special attack: not known yet (open the bank once so the amulet is read)';
    const eof = bankEofOf[key];
    if (!eof) return 'Stored special attack: none';
    if (v && v.parts && v.parts.extras.some(e => e.kind === 'eof')) return '';   // already a line of the breakdown
    return 'Stored special attack: ' + eof.weaponName + ' (no GE price for the weapon, nothing added)';
  }
  // Items per page = whole cells that fit the grid's area (it takes the space left under the tools, and the
  // pager sits below it), so a page fills the panel instead of stopping at a fixed count.
  let bankPer = BANK_PER_PAGE;
  // The estimate is checked after painting: if the grid still overflows, a row is dropped and the page
  // repainted, and that corrected size is remembered for the grid's current dimensions.
  let bankCols = 1, bankPerFit = { key: '', per: 0 };
  // The bottom edge the bank is actually allowed to reach on screen: the nearest of the pane, the window
  // body and the window. The grid's own box can run past it (the pane scrolls or clips), so the grid's
  // clientHeight is not the truth; the screen rects are. Screen px throughout (uiScreenRect).
  function bankVisibleBottom(grid) {
    let b = Infinity;
    for (let el = grid.parentElement; el; el = el.parentElement) {
      if (el === document.body) break;
      if (el.classList && (el.classList.contains('pane') || el.classList.contains('win-body') || el.classList.contains('win') || el.id === 'content')) {
        try { b = Math.min(b, uiScreenRect(el).bottom); } catch (e) {}
      }
      if (el.classList && el.classList.contains('win')) break;
    }
    if (!isFinite(b)) b = window.innerHeight;
    return b;
  }
  function bankPerPage(grid, pager) {
    const W = grid.clientWidth;
    if (W < 40) return bankPer;
    let Z = 1; try { Z = uiZoomOf(grid) || 1; } catch (e) {}
    let H = grid.clientHeight;
    try {
      const gr = uiScreenRect(grid), pr = pager ? uiScreenRect(pager) : { height: 0 };
      H = Math.floor((bankVisibleBottom(grid) - gr.top - pr.height - 8 * Z) / Z);   // css px left for the grid above the pager
    } catch (e) {}
    if (H < 40) H = 40;
    const gap = 4, cols = Math.max(1, Math.floor((W + gap) / (40 + gap)));
    const cell = (W - (cols - 1) * gap) / cols;
    const rows = Math.max(1, Math.floor((H - 2) / (cell + gap)));   // every row wants its full cell plus gap
    bankCols = cols;
    let per = cols * rows;
    const key = W + 'x' + H;
    if (bankPerFit.key === key && bankPerFit.per > 0) per = Math.min(per, bankPerFit.per);
    return per;
  }
  function bankOverflows(grid, pager) {                 // anything below the grid past the visible bottom?
    try {
      const bottom = pager ? uiScreenRect(pager).bottom : uiScreenRect(grid).bottom;
      return bottom > bankVisibleBottom(grid) + 1 || grid.scrollHeight > grid.clientHeight + 1;
    } catch (e) { return grid.scrollHeight > grid.clientHeight + 1; }
  }
  function paintBankPage() {
    const grid = document.getElementById('bankGrid');
    if (!grid) return;
    bankPer = bankPerPage(grid, grid.parentElement.querySelector('.bank-pager'));
    if (bankData && bankData.items) bankEofRefresh(bankData.items);
    bankTabsVbRefresh();
    if (!bankTabsData && bankTabsVb) bankTabsBuild();
    if (bankData && bankData.open) bankTabsMetaDiscover();
    const emptyBox = document.getElementById('bankEmpty');
    const pager = grid.parentElement.querySelector('.bank-pager');
    const total = (bankData && bankData.count) ? bankData.count : 0;
    const filtered = bankFilter();
    const pages = Math.max(1, Math.ceil(filtered.length / bankPer));
    if (bankPage >= pages) bankPage = pages - 1;
    if (bankPage < 0) bankPage = 0;

    const priceGen = (bankGe ? bankGeAt : 0) + '/' + bankMapping();   // repaint when prices or the name mapping arrive
    const sig = bankTerm + '|' + bankTab + '|' + (bankTabsVb ? bankTabsVb.sig : '') + '|' + (bankTabsMeta ? JSON.stringify(bankTabsMeta.byCid).length : 0) + '|' + bankPage + '|' + bankPer + '|' + filtered.length + '|' + bankSort + '|' + bankBasis + '|' + priceGen + '|' +
                (bankData ? bankData.cached_at + '|' + bankData.open : 'x');
    if (sig === bankPaintSig) { topUpBankIcons(); return; }
    bankPaintSig = sig;
    bankTabsStrip();

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
    const start = bankPage * bankPer;
    const slice = filtered.slice(start, start + bankPer);
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
      cell.dataset.tip = (bankRowName(id, name) || ('Item #' + id)) + '\nID ' + id + '\nx' + stack.toLocaleString() +
        (stack === 0 ? ' (placeholder)' : '') + '\nSlot ' + slot + bankTabLine(slot) +
        bankGeLines(v, stack, name, slot, id, it) +
        '\nHA ' + (v.ha != null ? v.ha.toLocaleString() + ' ea' + (stack > 1 ? ' · ' + fmtGp(v.haTotal) + ' total' : '') : 'n/a');
      grid.appendChild(cell);
    }
    // the grid is measured before the tools and pager settle; if the last row does not fit, drop it and repaint
    if (slice.length > bankCols && bankOverflows(grid, pager)) {
      const per = Math.max(bankCols, bankPer - bankCols);
      if (per < bankPer) {
        // remember the corrected size under the same key bankPerPage will compute next time
        let Z = 1; try { Z = uiZoomOf(grid) || 1; } catch (e) {}
        let H = grid.clientHeight;
        try { const gr = uiScreenRect(grid), pr = pager ? uiScreenRect(pager) : { height: 0 }; H = Math.floor((bankVisibleBottom(grid) - gr.top - pr.height - 8 * Z) / Z); } catch (e) {}
        if (H < 40) H = 40;
        bankPerFit = { key: grid.clientWidth + 'x' + H, per: per }; bankPaintSig = ''; paintBankPage(); return;
      }
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
    if (pager) pager.style.visibility = pages <= 1 ? 'hidden' : '';   // a single page needs no paging; keep its space so the grid never reflows
    const prev = document.getElementById('bankPrev'); if (prev) prev.disabled = bankPage <= 0;
    const next = document.getElementById('bankNext'); if (next) next.disabled = bankPage >= pages - 1;
    if (bankOverlayOn) bankOverlayTick();
  }

Object.assign(window, { paintBankPage });
registerTab({ id: 'bank', render: renderBank, open: function () { bankFetchKey = ''; fetchBank(); } });
// Prices and the name mapping arrive from the relay after the first paint; repaint while the tab is showing
// (a no-op when nothing changed) so values fill in without waiting for the bank itself to change.
setInterval(function () { try { if (paneVisible('bank')) paintBankPage(); } catch (e) {} }, 2000);
})();
