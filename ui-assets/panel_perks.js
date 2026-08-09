// RuneToolsX panel: Perks (augmented items: level + gizmo perks).
// Spliced inline into client.html at load; shares its global scope (no IIFE).
  let perksData = null, perksSig = '', perksFetching = false, _perksAt = 0;
  async function fetchPerks() {
    if (!bridge() || !bridge().perks || perksFetching) return;
    // Throttle: the read is heavy (DBRow perk-name lookups) and the data changes slowly.
    const now = Date.now();
    if (now - _perksAt < 1000) return;
    _perksAt = now;
    perksFetching = true;
    try { const d = JSON.parse(await bridge().perks(myPid())); perksData = (d && Array.isArray(d.items)) ? d : { items: [] }; }
    catch (e) { /* keep previous */ }
    perksFetching = false;
    paneRun('perks', renderPerks);
  }
  // Annotate a "per rank" description with the computed total: "1.5% per rank" at rank 6 ->
  // "1.5% per rank <col=ffffff>(9% at rank 6)</col>". Shapes handled:
  //   "A% + B% per rank"  -> A + B*rank
  //   "N% ... per rank"   -> N*rank   (same sentence)
  //   "N per rank"        -> N*rank   (bare number directly before the phrase)
  // Plain shapes are skipped at rank 1; an unparseable template is left untouched.
  function perkScaleDesc(desc, rank) {
    if (!rank || rank < 1) return desc;
    const fmt = n => String(Math.round(n * 1000) / 1000);
    const HOLD = 'per\x01rank';                                // guards a handled phrase from re-matching
    const tag = v => ' <col=ffffff>(' + v + ' at rank ' + rank + ')</col>';
    let s = String(desc);
    s = s.replace(/(\d+(?:\.\d+)?)%\s*\+\s*(\d+(?:\.\d+)?)%((?:(?!per rank)[^.])*?)per rank/gi,
      (m, a, b) => m.replace(/per rank/i, HOLD) + tag(fmt(+a + +b * rank) + '%'));
    s = s.replace(/(\d+(?:\.\d+)?)\s*%((?:(?!per rank)[^.])*?)per rank/gi,
      (m, a) => rank === 1 ? m : m.replace(/per rank/i, HOLD) + tag(fmt(+a * rank) + '%'));
    s = s.replace(/(\d+(?:\.\d+)?)\s+per rank/gi,
      (m, a) => rank === 1 ? m : m.replace(/per rank/i, HOLD) + tag(fmt(+a * rank)));
    return s.replace(/per\x01rank/g, 'per rank');
  }
  function renderPerks() {
    const c = $('content');
    let w = document.getElementById('pkWrap');
    if (!w) {
      c.innerHTML = '';
      w = document.createElement('div'); w.id = 'pkWrap'; w.className = 'pk-wrap';
      const hdr = document.createElement('div'); hdr.className = 'scene-hdr';
      const t = document.createElement('div'); t.textContent = 'Augmented items';
      const cnt = document.createElement('span'); cnt.className = 'cnt'; cnt.id = 'pkCnt'; cnt.textContent = '...';
      hdr.appendChild(t); hdr.appendChild(cnt);
      const list = document.createElement('div'); list.id = 'pkList'; list.className = 'pk-list scene-list';
      w.appendChild(hdr); w.appendChild(list); c.appendChild(w); perksSig = '';
    }
    const list = document.getElementById('pkList');
    const items = (perksData && Array.isArray(perksData.items)) ? perksData.items : null;
    $('pkCnt').textContent = (items === null) ? '...' : items.length;
    const sig = (items === null) ? 'null'
      : items.map(i => i.container + ':' + i.slot + ':' + i.id + ':' + i.xp + ':' + (i.perks || []).map(p => p.id + '.' + p.rank).join('|')).join(';');
    if (sig === perksSig) return; perksSig = sig;
    list.innerHTML = '';
    if (items === null) { list.innerHTML = '<div class="empty">Reading...</div>'; return; }
    if (!items.length) { list.innerHTML = '<div class="empty">No augmented items found (or not in-world).</div>'; return; }
    const GROUP = [[94, 'Equipment'], [93, 'Inventory']];
    for (const [gid, label] of GROUP) {
      const gi = items.filter(i => i.container === gid).sort((a, b) => (b.level - a.level) || (a.slot - b.slot));
      if (!gi.length) continue;
      const gh = document.createElement('div'); gh.className = 'pk-grp'; gh.textContent = label; list.appendChild(gh);
      for (const it of gi) {
        const card = document.createElement('div'); card.className = 'pk-item';
        const head = document.createElement('div'); head.className = 'pk-head';
        const left = document.createElement('div'); left.className = 'pk-head-l';
        const ico = document.createElement('div'); ico.className = 'pk-icon'; ico.dataset.itemId = it.id; attachBankIcon(ico, it.id);
        const nm = document.createElement('div'); nm.className = 'pk-name'; nm.textContent = it.name || ('Item ' + it.id);
        left.appendChild(ico); left.appendChild(nm);
        const lv = document.createElement('div'); lv.className = 'pk-lvl'; lv.textContent = 'Lvl ' + it.level;
        head.appendChild(left); head.appendChild(lv); card.appendChild(head);
        const perks = (it.perks || []).filter(p => p.id > 0);
        const pg = document.createElement('div'); pg.className = 'pk-giz';
        if (perks.length) {
          // The cache effect text carries Jagex <col=..> markup: opt the tooltip into HTML.
          const attr = s => String(s).replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;');
          pg.innerHTML = perks.map(p => '<span class="perk"' +
            (p.desc ? ' data-tip="' + attr(perkScaleDesc(p.desc, p.rank)) + '" data-tip-html="1"' : '') + '>' +
            (p.name ? String(p.name).replace(/[<>&]/g, '') : ('#' + p.id)) + '</span>').join('');
        } else {
          pg.innerHTML = '<span class="pk-none">No perks</span>';
        }
        card.appendChild(pg);
        const xp = document.createElement('div'); xp.className = 'pk-xp';
        const gz = it.gizmos || 0;
        xp.textContent = 'XP ' + (it.xp || 0).toLocaleString() + (gz ? ' · ' + gz + ' gizmo' + (gz > 1 ? 's' : '') : '');
        card.appendChild(xp);
        list.appendChild(card);
      }
    }
  }
