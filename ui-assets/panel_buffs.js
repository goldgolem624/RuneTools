// RuneToolsX panel: Buffs (active buff/debuff timers from the buff-bar widgets), each with the
// detail line the game itself shows for it, computed by the game's own tooltip script over live
// vars and cache lookups (core/rtx-gametext.js + core/rtx-gametext-rt.js).
(function () {

  buffsData = null; let buffsSig = ''; let buffsFetching = false; let _buffsAt = 0;
  async function fetchBuffs() {
    if (!bridge() || !bridge().buffs || buffsFetching) return;
    const now = Date.now();
    if (now - _buffsAt < 600) return;
    _buffsAt = now;
    buffsFetching = true;
    try { const d = JSON.parse(await rtxData.raw('state.buffs')); buffsData = (d && Array.isArray(d.buffs)) ? d : { buffs: [], debuffs: [] }; }
    catch (e) { /* keep previous */ }
    buffsFetching = false;
    paneRun('buffs', renderBuffs);
    if (typeof gameText === 'object') gameText.refresh();   // vars behind the details move while a buff is up
  }
  function attachBuffIcon(el, b) {
    if (b.item) { const url = resolveIcon(b.item); if (url) { setIconBg(el, url); return; } }
    if (b.sprite) setSprite(el, b.sprite);
  }
  // Sprite by id onto an element's background, shared by the buff icon and inline text sprites.
  function setSprite(el, sid) {
    const cached = SPRITES.get(sid);
    if (cached) { setIconBg(el, cached); return; }
    el.dataset.spr = sid;
    if (!bridge() || !bridge().sprite || SPRITE_PENDING.has(sid)) return;
    SPRITE_PENDING.add(sid);
    (async () => {
      try {
        const url = await rtxData.raw('cache.sprite', sid);
        SPRITE_PENDING.delete(sid);
        if (url) {
          SPRITES.set(sid, url);
          document.querySelectorAll('.bf-icon[data-spr="' + sid + '"], .gt-spr[data-spr="' + sid + '"]').forEach(n => setIconBg(n, url));
        }
      } catch (e) { SPRITE_PENDING.delete(sid); }
    })();
  }

  // The game's detail text for a buff: '' when the game has none, null while data is on its way.
  function detailFor(b) {
    if (typeof gameText !== 'object' || !b.struct) return '';
    const r = gameText.evaluate('buff', [b.struct | 0], { count: (typeof b.count === 'number') ? b.count : 0 });
    return r.pending ? null : r.text;
  }

  // Evaluate every row's detail against what is held now; rows still waiting keep their last text.
  function paintDetails() {
    const list = document.getElementById('bfList');
    if (!list || !buffsData) return;
    const all = (buffsData.buffs || []).concat(buffsData.debuffs || []);
    const rows = list.querySelectorAll('.bf-row');
    for (let i = 0; i < rows.length && i < all.length; i++) {
      const b = all[i], row = rows[i], det = row.querySelector('.bf-detail');
      if (!det) continue;
      const text = detailFor(b);
      if (text === null) continue;
      if (row.dataset.detail === text) continue;
      row.dataset.detail = text;
      gameText.render(det, text, setSprite);
      row.dataset.tip = row.dataset.tipBase + (text ? '\n\n' + gameText.plain(text) : '');
    }
    gameText.fill();
  }
  if (typeof gameText === 'object') gameText.onChange(() => { if (document.getElementById('bfList')) paintDetails(); });

  function renderBuffs() {
    const c = $('content');
    let w = document.getElementById('bfWrap');
    if (!w) {
      c.innerHTML = '';
      w = document.createElement('div'); w.id = 'bfWrap'; w.className = 'pk-wrap';
      const hdr = document.createElement('div'); hdr.className = 'scene-hdr';
      const t = document.createElement('div'); t.textContent = 'Active effects';
      const cnt = document.createElement('span'); cnt.className = 'cnt'; cnt.id = 'bfCnt'; cnt.textContent = '...';
      hdr.appendChild(t); hdr.appendChild(cnt);
      const list = document.createElement('div'); list.id = 'bfList'; list.className = 'bf-list';
      w.appendChild(hdr); w.appendChild(list); c.appendChild(w); buffsSig = '';
    }
    const list = document.getElementById('bfList');
    const buffs = (buffsData && Array.isArray(buffsData.buffs)) ? buffsData.buffs : null;
    const debuffs = (buffsData && Array.isArray(buffsData.debuffs)) ? buffsData.debuffs : [];
    $('bfCnt').textContent = (buffs === null) ? '...' : (buffs.length + debuffs.length);
    // Exact countdown when the host read the game's own end cycle (b.exact); the bar text otherwise.
    const fmtTime = b => {
      if (b && b.exact && typeof b.remainMs === 'number') {
        const t = Math.max(0, Math.floor(b.remainMs / 1000)), h = Math.floor(t / 3600), m = Math.floor((t % 3600) / 60), sec = t % 60;
        const base = h ? (h + ':' + String(m).padStart(2, '0') + ':' + String(sec).padStart(2, '0')) : (m + ':' + String(sec).padStart(2, '0'));
        return (typeof b.count === 'number' && b.count > 0) ? base + ' (' + b.count + ')' : base;
      }
      if (b && typeof b.count === 'number' && b.count > 0 && !b.timer) return String(b.count);
      return (b && b.timer) || '';
    };
    const idOf = b => (b.item ? 'i' + b.item : 's' + b.sprite) + '|' + (b.name || '') + '|' + (b.kind || '') + '|' + (b.struct || '');
    const inWorld = !!(lastSnap && lastSnap.in_world);
    const structSig = (buffs === null) ? 'null'
      : buffs.map(idOf).join(',') + '#' + debuffs.map(idOf).join(',') + (inWorld ? '' : '#lobby');
    if (structSig !== buffsSig) {
      buffsSig = structSig;
      list.innerHTML = '';
      if (buffs === null) { list.innerHTML = '<div class="empty">Reading...</div>'; return; }
      if (!buffs.length && !debuffs.length) { list.innerHTML = '<div class="empty">' + (inWorld ? 'No active buffs or debuffs.' : 'Not in world. Buffs and debuffs appear once you are logged in.') + '</div>'; return; }
      const section = (label, arr) => {
        if (!arr.length) return;
        const gh = document.createElement('div'); gh.className = 'bf-grp'; gh.textContent = label; list.appendChild(gh);
        for (const b of arr) {
          const row = document.createElement('div'); row.className = 'bf-row';
          const ico = document.createElement('div'); ico.className = 'bf-icon'; attachBuffIcon(ico, b);
          const txt = document.createElement('div'); txt.className = 'bf-text';
          const nm = document.createElement('div'); nm.className = 'bf-name';
          nm.textContent = b.name || (b.item ? ('Item ' + b.item) : ('Sprite ' + b.sprite));
          const det = document.createElement('div'); det.className = 'bf-detail';
          txt.appendChild(nm); txt.appendChild(det);
          const tm = document.createElement('div');
          tm.className = 'bf-time' + (b.kind && b.kind !== 'timer' ? ' bf-static' : '');
          tm.textContent = fmtTime(b);
          row.dataset.tipBase = (b.name || '(unnamed)') + '\n' +
                                (b.item ? ('item ' + b.item) : ('sprite ' + b.sprite)) +
                                (b.struct ? ('\nstruct ' + b.struct + (b.exact ? ' (exact timer)' : '')) : '');
          row.dataset.tip = row.dataset.tipBase;
          row.appendChild(ico); row.appendChild(txt); row.appendChild(tm); list.appendChild(row);
        }
      };
      section('Buffs', buffs);
      section('Debuffs', debuffs);
      paintDetails();
    }
    if (buffs !== null) {
      const order = buffs.map(fmtTime)
        .concat(debuffs.map(fmtTime));
      const timeEls = list.querySelectorAll('.bf-time');
      for (let i = 0; i < timeEls.length && i < order.length; i++)
        if (timeEls[i].textContent !== order[i]) timeEls[i].textContent = order[i];
      // a stack count feeds some details
      const rows = list.querySelectorAll('.bf-row');
      const all = buffs.concat(debuffs);
      let countMoved = false;
      for (let i = 0; i < rows.length && i < all.length; i++) {
        const cnt = String((typeof all[i].count === 'number') ? all[i].count : '');
        if (rows[i].dataset.count !== cnt) { rows[i].dataset.count = cnt; countMoved = true; }
      }
      if (countMoved) paintDetails();
    }
  }

Object.assign(window, { fetchBuffs });
registerTab({ id: 'buffs', render: renderBuffs });
})();
