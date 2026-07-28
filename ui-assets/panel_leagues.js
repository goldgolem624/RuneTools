// RuneToolsX panel: Leagues (tiers, relics, task catalogue).
// Spliced inline into client.html; bare classic script sharing one global scope.
// Everything renders from the live cache DBTables (326 header, 327 tier order, 328 tiers,
// 329 relics, 334 tasks, 336 categories) so the next league's data flows through the same
// tables on release with no client change. Task difficulty -> points mapping is not yet
// known from the cache, so tasks show their difficulty band until that is confirmed.

  let lgData = null, lgFetching = false, lgFetchAt = 0, lgSig = '';
  const LG_T = { header: 326, tierOrder: 327, tiers: 328, relics: 329, tasks: 334, cats: 336 };
  const lgCol = (r, m, k) => { const t = (r && r[m]) || {}; return (t[k] && t[k][0] !== undefined) ? t[k][0] : null; };
  const lgList = (r, m, k) => { const t = (r && r[m]) || {}; return Array.isArray(t[k]) ? t[k] : []; };

  async function fetchLeagues() {
    if (!bridge() || !bridge().dbRows || lgFetching) return;
    const t = Date.now();
    if (t - lgFetchAt < 5000) return;
    lgFetchAt = t; lgFetching = true;
    try {
      const grab = async id => { try { return JSON.parse(await bridge().dbRows(id) || 'null') || []; } catch (e) { return []; } };
      const [hdr, order, tiers, relics, tasks, cats] = await Promise.all(
        [LG_T.header, LG_T.tierOrder, LG_T.tiers, LG_T.relics, LG_T.tasks, LG_T.cats].map(grab));
      if (!tiers.length && !relics.length) return;        // cache not open yet; retry next poll
      const relicById = {};
      for (const r of relics) relicById[r.id] = { name: lgCol(r, 's', '0') || ('#' + r.id), desc: lgCol(r, 's', '1') || '' };
      const tierIds = order.length ? lgList(order[0], 'i', '0') : tiers.map(r => r.id);
      const tierRows = tierIds.map(id => tiers.find(r => r.id === id)).filter(Boolean);
      // categories: flat records of [?, sprite, name, categoryId]
      const catName = {};
      if (cats.length) {
        const flatI = lgList(cats[0], 'i', '1'), flatS = lgList(cats[0], 's', '1');
        // ints and strings are split per type but share record order: 3 ints + 1 string per record
        for (let r = 0; r * 3 + 2 < flatI.length && r < flatS.length; r++) catName[flatI[r * 3 + 2]] = flatS[r];
      }
      const perCat = {}, perAct = {}, perDiff = {};
      for (const r of tasks) {
        const cat = lgCol(r, 'i', '5'), act = lgCol(r, 'i', '6'), diff = lgCol(r, 'i', '4');
        perCat[cat] = (perCat[cat] || 0) + 1;
        perAct[act] = (perAct[act] || 0) + 1;
        perDiff[diff] = (perDiff[diff] || 0) + 1;
      }
      lgData = {
        name: hdr.length ? (lgCol(hdr[0], 's', '1') || 'Leagues') : 'Leagues',
        sub: hdr.length ? (lgCol(hdr[0], 's', '2') || '') : '',
        taskTotal: tasks.length,
        tiers: tierRows.map(r => ({
          cost: lgCol(r, 'i', '2') | 0,
          relics: lgList(r, 'i', '1').map(id => relicById[id] || { name: '#' + id, desc: '' }),
          passives: lgList(r, 's', '4')
        })),
        perCat: Object.keys(perCat).map(k => [catName[k] || ('Category ' + k), perCat[k]]).sort((a, b) => b[1] - a[1]),
        perAct: perAct, perDiff: perDiff
      };
    } finally { lgFetching = false; }
    if (activeTab === 'leagues') renderLeagues();
  }

  function renderLeagues() {
    const c = $('content');
    let wrap = $('lgWrap');
    if (!wrap) {
      c.innerHTML = ''; lgSig = '';
      injectStyle('lgCss', `
        .lg-sec { color: var(--accent-hi); font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: .05em; padding: 14px 2px 6px; }
        .lg-wrap { margin: 2px 12px 0; }
        .lg-card { background: var(--bg-elev); border: 1px solid var(--border); border-radius: 10px; }
        .lg-row { display: flex; align-items: flex-start; gap: 10px; padding: 8px 12px; }
        .lg-row + .lg-row { border-top: 1px solid var(--border); }
        .lg-nm { flex: 1; min-width: 0; color: var(--text); font-size: 12px; }
        .lg-sub { color: var(--text-mute); font-size: 10.5px; margin-top: 2px; }
        .lg-pill { font-size: 10.5px; padding: 2px 9px; border-radius: 999px; border: 1px solid var(--border); color: var(--text-dim); white-space: nowrap; font-variant-numeric: tabular-nums; flex: 0 0 auto; }
        .lg-empty { margin: 10px 14px; color: var(--text-dim); font-size: 12px; }`);
      wrap = document.createElement('div'); wrap.id = 'lgWrap'; wrap.className = 'lg-wrap'; c.appendChild(wrap);
    }
    const d = lgData;
    if (!d) {
      if (lgSig !== 'wait') { lgSig = 'wait'; wrap.innerHTML = '<div class="lg-empty">Reading league data from the cache... (be in-world). Before a league is live the cache holds placeholder data only.</div>'; }
      return;
    }
    const sig = d.name + '|' + d.taskTotal + '|' + d.tiers.map(t => t.cost + ':' + t.relics.length).join(',');
    if (sig === lgSig) return;
    lgSig = sig;
    wrap.innerHTML = '';
    const sec = t => { const e = document.createElement('div'); e.className = 'lg-sec'; e.textContent = t; wrap.appendChild(e); };
    const card = () => { const e = document.createElement('div'); e.className = 'lg-card'; wrap.appendChild(e); return e; };
    const row = (box, name, sub, pill, tip) => {
      const r = document.createElement('div'); r.className = 'lg-row';
      const nm = document.createElement('div'); nm.className = 'lg-nm';
      const t = document.createElement('div'); t.textContent = name; nm.appendChild(t);
      if (sub) { const sb = document.createElement('div'); sb.className = 'lg-sub'; sb.textContent = sub; nm.appendChild(sb); }
      r.appendChild(nm);
      if (pill != null) { const p = document.createElement('span'); p.className = 'lg-pill'; p.textContent = pill; r.appendChild(p); }
      if (tip) r.dataset.tip = tip;
      box.appendChild(r);
    };
    sec(d.name + (d.sub ? ' · ' + d.sub : ''));
    const tc = card();
    d.tiers.forEach((t, i) => {
      row(tc, 'Tier ' + (i + 1) + ' · ' + t.relics.map(x => x.name).join(', '),
          (t.passives.length ? t.passives.length + ' tier passives' : ''),
          t.cost.toLocaleString() + ' pts',
          t.relics.map(x => x.name + ': ' + x.desc).join('\n'));
    });
    sec('Tasks · ' + d.taskTotal + ' total');
    const kc = card();
    row(kc, 'By act', Object.keys(d.perAct).sort().map(a => 'Act ' + a + ': ' + d.perAct[a]).join(' · '), null, null);
    row(kc, 'By difficulty', Object.keys(d.perDiff).sort().map(k => k + '★ ' + d.perDiff[k]).join(' · '), null,
        'Difficulty band from the task table; the points value per band is not yet confirmed from the cache.');
    sec('Task categories');
    const cc = card();
    for (const [nm, n] of d.perCat.slice(0, 40)) row(cc, nm, null, String(n), null);
  }
