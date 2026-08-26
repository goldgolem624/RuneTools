// RuneToolsX panel: Wardrobe (cosmetic override ownership across the whole catalogue).
// Spliced inline into client.html; bare classic script sharing one global scope.
//
// Ground truth is dbtable 163 (5,313 rows on build 949): col 2 = display name, col 9 = the
// game's own how-to-obtain wording (present on ~half the rows), col 26 = ownership
// var_reference (high byte 1 = varbit, all rows), col 27 = ownership threshold (present on
// ~212 rows; dbrows omits table defaults, so absent means the default of 1 - a naive 0 would
// mark everything owned), col 28 = members gate. The game's own test is script6252:
// value(col 26) < threshold(col 27) means not owned. Consuming UI in game is iface 1843,
// which has no cross-category search and no "what am I missing" view - this panel is both.
//
// No appearance previews: the game itself draws these as posed 3D MODELS (script18202 ->
// script13146, IF_SETMODELANGLE) and table 163 carries no sprite or item-icon column, so there
// is nothing 2D to render. Each row gets a wiki button instead, which shows the look in the
// built-in wiki browser.
//
// No per-row itemInfo calls: the catalogue is name + text + one varbit each, and 5,313 serial
// awaits is the mistake panel_currencies already demonstrates. Rows load once (the table is
// version-static), ownership varbits refresh as a batch.

  let wdRows = null, wdLoading = false, wdVb = null, wdVbAt = 0, wdSig = '';
  let wdQuery = '', wdFilter = 'all';   // all | owned | missing
  let wdShowMax = 250;

  async function wdLoadRows() {
    if (wdRows || wdLoading || !bridge() || !bridge().dbRows) return;
    wdLoading = true;
    try {
      const rows = JSON.parse(await bridge().dbRows(163) || 'null');
      if (!Array.isArray(rows) || rows.length < 100) return;   // cache not open yet; retry
      const out = [];
      for (const r of rows) {
        const name = r.s && r.s['2'] && r.s['2'][0];
        const ref = (r.i && r.i['26'] && r.i['26'][0]) | 0;
        if (!name || !ref) continue;
        if ((ref >>> 24) !== 1) continue;                      // varbit refs only (all rows on 949)
        out.push({
          name: name,
          how: (r.s && r.s['9'] && r.s['9'][0]) || '',
          vb: ref & 0xFFFFFF,
          thr: (r.i && r.i['27'] && r.i['27'].length) ? (r.i['27'][0] | 0) : 1,
          members: !!(r.i && r.i['28'] && (r.i['28'][0] | 0)),
        });
      }
      if (out.length) { out.sort((a, b) => a.name.localeCompare(b.name)); wdRows = out; }
    } catch (e) {}
    finally { wdLoading = false; }
  }
  async function wdLoadVb() {
    if (!wdRows || typeof readVarbitValues !== 'function') return;
    const t = Date.now();
    if (wdVb && t - wdVbAt < 30000) return;                    // ownership moves rarely
    wdVbAt = t;
    const ids = wdRows.map(r => r.vb);
    const out = {};
    for (let i = 0; i < ids.length; i += 400) {
      try { Object.assign(out, await readVarbitValues(ids.slice(i, i + 400))); } catch (e) {}
    }
    if (Object.keys(out).length) { wdVb = out; paneRun('wardrobe', renderWardrobe); }
  }
  async function fetchWardrobe() {
    await wdLoadRows();
    await wdLoadVb();
    paneRun('wardrobe', renderWardrobe);
  }

  function wdOwned(r) { return wdVb ? ((wdVb[r.vb] | 0) >= r.thr) : null; }

  function renderWardrobe() {
    const c = $('content');
    let wrap = $('wdWrap');
    if (!wrap) {
      c.innerHTML = ''; wdSig = '';
      injectStyle('wdCss', `
        .wd-bar { display: flex; align-items: center; gap: 8px; padding: 2px 0 8px; }
        .wd-tabs { display: flex; gap: 4px; }
        .wd-tab { appearance: none; background: transparent; color: var(--text-dim); border: 1px solid transparent; border-radius: 999px; padding: 3px 10px; font: inherit; font-size: 11px; cursor: pointer; }
        .wd-tab.on { color: #fff; border-color: var(--accent-hi); background: rgba(140,111,253,0.12); }
        .wd-cnt { margin-left: auto; color: var(--accent-hi); font-size: 11px; font-variant-numeric: tabular-nums; }
        .wd-card { background: var(--bg-elev); border: 1px solid var(--border); border-radius: 10px; }
        .wd-row { display: flex; align-items: center; gap: 10px; padding: 6px 12px; }
        .wd-row + .wd-row { border-top: 1px solid var(--border); }
        .wd-nm { flex: 1; min-width: 0; color: var(--text); font-size: 12px; }
        .wd-how { color: var(--text-mute); font-size: 10.5px; margin-top: 1px; }
        .wd-pill { font-size: 10.5px; padding: 2px 9px; border-radius: 999px; border: 1px solid var(--border); color: var(--text-dim); white-space: nowrap; flex: none; }
        .wd-pill.ok { color: var(--ok); border-color: rgba(77, 210, 138, .45); }
        .wd-pill.no { color: #e0b34c; border-color: rgba(224, 179, 76, .4); }
        .wd-mem { color: var(--text-mute); font-size: 9.5px; text-transform: uppercase; letter-spacing: .05em; flex: none; }
        .wd-more { padding: 8px 12px; color: var(--text-mute); font-size: 11px; }
        .wd-wiki { appearance: none; background: transparent; border: 1px solid var(--border); border-radius: 6px; color: var(--text-dim); font: inherit; font-size: 10.5px; padding: 2px 8px; cursor: pointer; flex: none; }
        .wd-wiki:hover { border-color: var(--accent-hi); color: var(--text); }`);
      wrap = document.createElement('div'); wrap.id = 'wdWrap'; wrap.className = 'pane'; c.appendChild(wrap);
      const bar = document.createElement('div'); bar.className = 'wd-bar';
      const inp = document.createElement('input'); inp.type = 'text'; inp.className = 'bank-search'; inp.id = 'wdSearch';
      inp.placeholder = 'Search 5,000+ cosmetics by name or how to obtain...'; inp.value = wdQuery; inp.spellcheck = false;
      inp.addEventListener('input', () => { wdQuery = inp.value; wdShowMax = 250; wdSig = ''; renderWardrobe(); });
      inp.addEventListener('keydown', e => e.stopPropagation());
      bar.appendChild(inp);
      const tabs = document.createElement('div'); tabs.className = 'wd-tabs'; tabs.id = 'wdTabs';
      for (const [k, lab] of [['all', 'All'], ['owned', 'Owned'], ['missing', 'Missing']]) {
        const b = document.createElement('button'); b.type = 'button'; b.className = 'wd-tab' + (wdFilter === k ? ' on' : '');
        b.textContent = lab; b.dataset.k = k;
        b.addEventListener('click', () => { wdFilter = k; wdShowMax = 250; wdSig = ''; renderWardrobe(); });
        tabs.appendChild(b);
      }
      bar.appendChild(tabs);
      const cnt = document.createElement('span'); cnt.className = 'wd-cnt'; cnt.id = 'wdCnt';
      bar.appendChild(cnt);
      wrap.appendChild(bar);
      const list = document.createElement('div'); list.className = 'wd-card'; list.id = 'wdList';
      wrap.appendChild(list);
    }
    const list = $('wdList'), cnt = $('wdCnt');
    document.querySelectorAll('#wdTabs .wd-tab').forEach(b => b.classList.toggle('on', b.dataset.k === wdFilter));
    if (!wdRows) {
      if (wdSig !== 'wait') { wdSig = 'wait'; list.innerHTML = '<div class="wd-more">Reading the cosmetics catalogue from the cache... (be in-world; first read decodes ~5,300 rows)</div>'; }
      return;
    }
    const q = wdQuery.trim().toLowerCase();
    let rows = wdRows;
    if (q) rows = rows.filter(r => r.name.toLowerCase().indexOf(q) >= 0 || r.how.toLowerCase().indexOf(q) >= 0);
    let owned = 0;
    if (wdVb) for (const r of wdRows) if (wdOwned(r)) owned++;
    if (wdFilter !== 'all' && wdVb) rows = rows.filter(r => wdOwned(r) === (wdFilter === 'owned'));
    const sig = rows.length + '|' + q + '|' + wdFilter + '|' + owned + '|' + wdShowMax + '|' + (wdVb ? 1 : 0);
    if (sig === wdSig) return;
    wdSig = sig;
    cnt.textContent = wdVb ? (owned.toLocaleString() + ' / ' + wdRows.length.toLocaleString() + ' owned') : (wdRows.length.toLocaleString() + ' cosmetics');
    const esc = s => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
    let html = '';
    const shown = rows.slice(0, wdShowMax);
    for (const r of shown) {
      const o = wdOwned(r);
      html += '<div class="wd-row"><div class="wd-nm">' + esc(r.name)
        + (r.how ? '<div class="wd-how">' + esc(r.how) + '</div>' : '') + '</div>'
        + (r.members ? '<span class="wd-mem">members</span>' : '')
        + '<button type="button" class="wd-wiki" data-nm="' + esc(r.name).replace(/"/g, '&quot;') + '" title="Show this cosmetic in the wiki browser">wiki</button>'
        + (o === null ? '' : '<span class="wd-pill ' + (o ? 'ok">Owned' : 'no">Missing') + '</span>')
        + '</div>';
    }
    if (!rows.length) html = '<div class="wd-more">Nothing matches' + (q ? ' "' + esc(q) + '"' : '') + '.</div>';
    else if (rows.length > shown.length) html += '<div class="wd-more" id="wdMore" style="cursor:pointer">' + (rows.length - shown.length).toLocaleString() + ' more... click to show</div>';
    list.innerHTML = html;
    const more = document.getElementById('wdMore');
    if (more) more.addEventListener('click', () => { wdShowMax += 500; wdSig = ''; renderWardrobe(); });
    list.querySelectorAll('.wd-wiki').forEach(b => b.addEventListener('click', e => {
      e.stopPropagation();
      try { if (bridge() && bridge().wikiOpen) bridge().wikiOpen(myPid(), b.dataset.nm || ''); } catch (err) {}
    }));
  }
