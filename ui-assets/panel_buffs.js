// RuneToolsX panel: Buffs (active buff/debuff timers from the buff-bar widgets).
// Spliced inline into client.html; IIFE (window exports + registerTab; see the RTX registry in client.html).
(function () {

  // Ability cooldowns are NOT shown here; they live on the Ability Bar tab.
  buffsData = null; let buffsSig = ''; let buffsFetching = false; let _buffsAt = 0;
  async function fetchBuffs() {
    if (!bridge() || !bridge().buffs || buffsFetching) return;
    // Timers tick by the second; 600ms keeps the countdown smooth. The first call also builds the
    // struct-cache name map, so it costs more.
    const now = Date.now();
    if (now - _buffsAt < 600) return;
    _buffsAt = now;
    buffsFetching = true;
    try { const d = JSON.parse(await rtxData.raw('state.buffs')); buffsData = (d && Array.isArray(d.buffs)) ? d : { buffs: [], debuffs: [] }; }
    catch (e) { /* keep previous */ }
    buffsFetching = false;
    paneRun('buffs', renderBuffs);
  }
  // Item buffs use the bundled item pack, sprite buffs the cache sprite; an item buff missing
  // from the pack falls through to its sprite.
  function attachBuffIcon(el, b) {
    if (b.item) { const url = resolveIcon(b.item); if (url) { setIconBg(el, url); return; } }
    if (!b.sprite) return;
    const sid = b.sprite, cached = SPRITES.get(sid);
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
          document.querySelectorAll('.bf-icon[data-spr="' + sid + '"]').forEach(n => setIconBg(n, url));
        }
      } catch (e) { SPRITE_PENDING.delete(sid); }
    })();
  }
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
    // Structural signature = icon identity + name + kind, NOT live timer values: rebuilding on every
    // timer tick makes sprites visibly reload, so timer text is updated in place below.
    const idOf = b => (b.item ? 'i' + b.item : 's' + b.sprite) + '|' + (b.name || '') + '|' + (b.kind || '');
    const structSig = (buffs === null) ? 'null'
      : buffs.map(idOf).join(',') + '#' + debuffs.map(idOf).join(',');
    if (structSig !== buffsSig) {
      buffsSig = structSig;
      list.innerHTML = '';
      if (buffs === null) { list.innerHTML = '<div class="empty">Reading...</div>'; return; }
      if (!buffs.length && !debuffs.length) { list.innerHTML = '<div class="empty">No active buffs or debuffs (or not in-world).</div>'; return; }
      const section = (label, arr) => {
        if (!arr.length) return;
        const gh = document.createElement('div'); gh.className = 'bf-grp'; gh.textContent = label; list.appendChild(gh);
        for (const b of arr) {
          const row = document.createElement('div'); row.className = 'bf-row';
          const ico = document.createElement('div'); ico.className = 'bf-icon'; attachBuffIcon(ico, b);
          const nm = document.createElement('div'); nm.className = 'bf-name';
          nm.textContent = b.name || (b.item ? ('Item ' + b.item) : ('Sprite ' + b.sprite));
          const tm = document.createElement('div');
          tm.className = 'bf-time' + (b.kind && b.kind !== 'timer' ? ' bf-static' : '');
          tm.textContent = b.timer || '';
          row.dataset.tip = (b.name || '(unnamed)') + '\n' +
                            (b.item ? ('item ' + b.item) : ('sprite ' + b.sprite));
          row.appendChild(ico); row.appendChild(nm); row.appendChild(tm); list.appendChild(row);
        }
      };
      section('Buffs', buffs);
      section('Debuffs', debuffs);
    }
    // Timer text updated in place every poll, so icons do not reload.
    if (buffs !== null) {
      const order = buffs.map(b => b.timer || '')
        .concat(debuffs.map(b => b.timer || ''));
      const timeEls = list.querySelectorAll('.bf-time');
      for (let i = 0; i < timeEls.length && i < order.length; i++)
        if (timeEls[i].textContent !== order[i]) timeEls[i].textContent = order[i];
    }
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { fetchBuffs });
registerTab({ id: 'buffs', render: renderBuffs });
})();
