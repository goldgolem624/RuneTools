// RuneToolsX panel: Ports (slim, read-only reference over the Player-Owned Ports STATE).
// The full Ports experience (voyage optimiser, crew, visitors, alerts) is the marketplace
// plugin com.runetools.ports; this panel is the client-sided data layer's reference UI,
// and portsStateRead() below is what the plugin broker serves as state.ports - one decode,
// two consumers. Tables mirror the plugin's (var ground truth unchanged since the panel era).
(function () {

  // [name, varp, sprite]
  const PI_RES = [
    ['Chimes', 3441, 15673], ['Bamboo', 3442, 15674], ['Gunpowder', 3443, 15677],
    ['Black slate', 3444, 15675], ['Cherrywood', 3445, 15678], ['Jade', 3446, 15679],
    ['Stainless steel', 3447, 15676], ['Terracotta', 4038, 21916], ['Azure', 4039, 21917]];
  // [name, varbit, itemId]
  const PI_TRADE = [
    ['Ancient bones', 17415, 26317], ['Plate', 17416, 26320], ['Spices', 17417, 26318],
    ['Lacquer', 17418, 26319], ['Chi', 17419, 26321], ['Pearls', 21334, 30566],
    ['Koi scales', 21335, 30567]];
  // [label, currentLevelVarbit]
  const PI_BUILDINGS = [
    ['Bar', 26825], ['Office', 17398], ['Workshop', 17399], ['Lodgings', 26830],
    ['Shipwright', 26828], ['Warehouse', 26829], ['Portal', 17401],
    ['Totem 1', 17405], ['Totem 2', 17406], ['Totem 3', 17407], ['Totem 4', 17408],
    ['Icon 1', 21336], ['Icon 2', 21337], ['Icon 3', 21338]];
  // Per-ship varbits: name parts x3, voyage id, returned flag, return-timer index.
  const PI_SHIPS = [
    { nm: [17040, 17041, 17042], voy: 17071, ret: 17072, idx: 17081 },
    { nm: [17048, 17049, 17050], voy: 17082, ret: 17083, idx: 17092 },
    { nm: [17056, 17057, 17058], voy: 17093, ret: 17094, idx: 17103 },
    { nm: [17064, 17065, 17066], voy: 17104, ret: 17105, idx: 17114 }];
  const PI_RETVBS = [16962, 16966, 16970, 16974, 16978, 16982, 16986, 16990];   // timer idx 1-8
  const PI_DAMAGED = { 32722: 1, 32723: 1, 32724: 1 };   // voyage-id sentinels = ship damaged
  // Scroll-piece counters, summed as the game's "Scroll pieces found" stat.
  const PI_SCROLLS = (() => {
    const a = [];
    for (let i = 17465; i <= 17474; i++) a.push(i);
    for (let i = 17477; i <= 17486; i++) a.push(i);
    for (let i = 17488; i <= 17494; i++) a.push(i);
    a.push(21319, 21320, 21321, 26782, 26783, 26784, 26785);
    return a;
  })();
  // Zone-unlock thresholds of vb 17458 (cumulative distance explored), highest first.
  const PI_ZONES = [[4100000, 8], [2300000, 7], [1200000, 6], [450000, 5], [140000, 4], [40000, 3], [5000, 2]];

  const PI_VB_IDS = (() => {
    const ids = new Set([17457, 17458, 17495, 17033, 17034]);
    PI_TRADE.forEach(t => ids.add(t[1]));
    PI_BUILDINGS.forEach(b => ids.add(b[1]));
    for (const s of PI_SHIPS) { s.nm.forEach(x => ids.add(x)); ids.add(s.voy); ids.add(s.ret); ids.add(s.idx); }
    PI_RETVBS.forEach(x => ids.add(x));
    PI_SCROLLS.forEach(x => ids.add(x));
    return [...ids];
  })();
  const PI_VP_IDS = PI_RES.map(r => r[1]).join(',');

  // The state.ports payload (also this panel's data): decoded fresh per call, ~1.5s cached
  // so the broker's 20/s ceiling can't hammer the reader.
  let piCache = null, piCacheAt = 0;
  async function portsStateRead() {
    if (piCache && Date.now() - piCacheAt < 1500) return piCache;
    if (!bridge()) return null;
    let vb = {}, vp = {};
    try { vb = (await readVarbitValues(PI_VB_IDS)) || {}; } catch (e) {}
    try { vp = JSON.parse(await bridge().varps(myPid(), PI_VP_IDS)) || {}; } catch (e) {}
    const v = k => (vb[String(k)] | 0), p = k => (vp[String(k)] | 0);
    // script4186: office level / tutorial state -> how many ships the port has.
    const shipCount = v(17398) > 2 ? 4 : v(17398) > 1 ? 3 : v(17495) >= 63 ? 2 : (v(17457) === 1 ? 1 : 0);
    const distance = v(17458);
    let zone = v(17495) >= 63 ? 1 : 0;
    for (const [th, n] of PI_ZONES) if (distance >= th) { zone = n; break; }
    const nowMin = Math.floor(Date.now() / 60000);
    const ships = PI_SHIPS.slice(0, shipCount).map(s => {
      const voy = v(s.voy), idx = v(s.idx);
      let status = 'ready', etaMinutes = null;
      if (v(s.ret) > 0) status = 'returned';
      else if (idx >= 1 && idx <= 8) {
        if (PI_DAMAGED[voy]) status = 'damaged';
        else { status = 'sailing'; etaMinutes = Math.max(0, (22000000 + v(PI_RETVBS[idx - 1])) - nowMin); }
      }
      return { nameParts: s.nm.map(x => v(x)), voyageId: voy, status, etaMinutes };
    });
    piCache = {
      resources: PI_RES.map(r => ({ name: r[0], qty: p(r[1]), sprite: r[2] })),
      tradeGoods: PI_TRADE.map(t => ({ name: t[0], qty: v(t[1]), item: t[2] })),
      buildings: PI_BUILDINGS.map(b => ({ name: b[0], level: v(b[1]) })),
      ships, shipCount,
      scrollPieces: PI_SCROLLS.reduce((s, id) => s + v(id), 0),
      distance, zone,
    };
    piCacheAt = Date.now();
    return piCache;
  }

  function piEta(m) {
    if (m == null) return '';
    return m >= 60 ? Math.floor(m / 60) + 'h ' + (m % 60) + 'm' : m + 'm';
  }
  const PI_SHIP_STATUS = { ready: ['q-ready', 'Ready to sail'], sailing: ['q-prog', 'Sailing'],
                           returned: ['q-done', 'Returned - collect!'], damaged: ['m2', 'Damaged'] };
  let piSig = '';
  function piIconRow(iconAttr, name, qty) {
    return '<div class="pi-row"><span class="pi-ic" ' + iconAttr + '></span>'
      + '<span class="pi-nm" data-tip="' + name + '">' + name + '</span>'
      + '<b class="pi-qty">' + qty.toLocaleString() + '</b></div>';
  }
  async function renderPortsInfo() {
    const c = $('content'); if (!c) return;
    const d = await portsStateRead();
    if (!paneVisible('portsinfo')) return;   // await raced the window closing
    if (!d) { c.innerHTML = '<div class="empty">Reading... (be in-world)</div>'; piSig = ''; return; }
    injectStyle('piCss',
      '.pi-wrap{display:flex;flex-direction:column;gap:8px}'
      + '.pi-title{font-weight:700;font-size:14px}'
      + '.pi-sub{font-size:11.5px;color:var(--text-dim);margin-top:2px}'
      + '.pi-sub b{color:var(--text)}'
      + '.pi-sec{font-weight:700;font-size:13px;margin-bottom:6px}'
      + '.pi-ship{display:flex;align-items:center;gap:8px;padding:5px 0;border-bottom:1px solid rgba(255,255,255,0.05)}'
      + '.pi-ship:last-child{border-bottom:none}'
      + '.pi-shipn{flex:1;font-size:12.5px;font-weight:600}'
      + '.pi-voy{color:var(--text-dim);font-size:11px}'
      + '.pi-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:4px 14px}'
      + '.pi-row{display:flex;align-items:center;gap:6px;font-size:12px;min-width:0}'
      + '.pi-ic{width:18px;height:18px;flex:none;background-size:contain;background-repeat:no-repeat;background-position:center}'
      + '.pi-nm{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}'
      + '.pi-qty{font-variant-numeric:tabular-nums}'
      + '.pi-brow{display:flex;justify-content:space-between;gap:8px;font-size:12px}'
      + '.pi-brow b{font-variant-numeric:tabular-nums}'
      + '.pi-brow .off{color:var(--text-mute);font-weight:400}'
      + '.pi-note{color:var(--text-dim);font-size:11px;line-height:1.5}');
    const sig = JSON.stringify(d);
    if (sig === piSig && $('piWrap')) return;
    piSig = sig;
    let h = '<div id="piWrap" class="pi-wrap">';
    h += '<div class="card"><div class="pi-title">Player-Owned Ports</div>'
      + '<div class="pi-sub">Zone <b>' + d.zone + '</b> &nbsp;-&nbsp; distance explored <b>'
      + d.distance.toLocaleString() + '</b> &nbsp;-&nbsp; scroll pieces <b>' + d.scrollPieces + '</b></div></div>';
    h += '<div class="card"><div class="pi-sec">Ships</div>';
    if (!d.shipCount) h += '<div class="empty">No port yet (or not read).</div>';
    d.ships.forEach((s, i) => {
      const st = PI_SHIP_STATUS[s.status] || ['lg', s.status];
      h += '<div class="pi-ship"><span class="pi-shipn">Ship ' + (i + 1) + '</span>'
        + (s.voyageId > 0 && s.status === 'sailing' ? '<span class="pi-voy">voyage ' + s.voyageId + '</span>' : '')
        + '<span class="pet-st ' + st[0] + '">' + st[1] + (s.etaMinutes != null ? ' - ' + piEta(s.etaMinutes) : '') + '</span>'
        + '</div>';
    });
    h += '</div>';
    h += '<div class="card"><div class="pi-sec">Resources</div><div class="pi-grid">';
    for (const r of d.resources) h += piIconRow('data-spr="' + r.sprite + '"', r.name, r.qty);
    h += '</div></div>';
    h += '<div class="card"><div class="pi-sec">Trade goods</div><div class="pi-grid">';
    for (const t of d.tradeGoods) h += piIconRow('data-item="' + t.item + '"', t.name, t.qty);
    h += '</div></div>';
    h += '<div class="card"><div class="pi-sec">Buildings</div><div class="pi-grid">';
    for (const b of d.buildings)
      h += '<div class="pi-brow"><span>' + b.name + '</span>'
        + (b.level > 0 ? '<b>Tier ' + b.level + '</b>' : '<b class="off">-</b>') + '</div>';
    h += '</div></div>';
    h += '<div class="pi-note">Voyage planning, crew, visitors and alerts live in the Player-Owned Ports plugin (Plugin Hub).</div>';
    h += '</div>';
    if (c.firstElementChild && c.firstElementChild.id === 'piWrap') { c.firstElementChild.outerHTML = h; }
    else c.innerHTML = h;
    c.querySelectorAll('.pi-ic[data-spr]').forEach(el => loadSpriteIcon(el, +el.dataset.spr));
    c.querySelectorAll('.pi-ic[data-item]').forEach(el => { const u = resolveIcon(+el.dataset.item); if (u) el.style.backgroundImage = "url('" + u + "')"; });
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { portsStateRead });
registerTab({ id: 'portsinfo', render: renderPortsInfo });
})();
