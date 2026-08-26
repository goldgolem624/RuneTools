// RuneToolsX panel: Toolbelt (stored tools, missing tools, tool tiers).
// Baked from CS2 script7090, the game's "is this tool on the belt" resolver; regenerate
// after game updates. Most tools are one varbit each; tier slots index a cache enum:
// hatchet vb 18522 -> enum 6397 (bronze 1351 baseline), pickaxe vb 18521 -> enum 2433
// (bronze 1265), mattock enum 12936 indexed by vb 45999 as a PROGRESSIVE counter
// (current best = index vb45999 - 1). Names and icons resolve from the cache at runtime.

  // [itemId, varbit, min (stored when value >= min; default 1)]
  const TB_TOOLS = [
    [946, 2968], [1735, 2969], [8794, 2988], [4, 2989], [59903, 16445], [1595, 2970],
    [9434, 2990], [11065, 2991], [1755, 2971], [1785, 2992], [1599, 2972], [1597, 2973],
    [1733, 2974], [1592, 2975], [2976, 2993], [5523, 2976], [1594, 2994], [5343, 2995],
    [5325, 2996], [5341, 2997], [5329, 2998], [7409, 27430], [18682, 27431], [13431, 2977],
    [307, 2978], [309, 2979], [311, 2980], [305, 3001], [301, 2981], [303, 2982],
    [233, 2999], [2347, 2984], [590, 2986], [952, 3000], [975, 3002], [11323, 3003],
    [2575, 3004], [2576, 3005], [2574, 685], [13153, 3006], [7649, 30998], [10150, 3007],
    [20565, 28410], [42453, 40075], [42617, 40234], [42618, 40235], [4446, 40074],
    [28, 42126], [53443, 52348], [17883, 3010], [17678, 3011], [17794, 3012],
    [17754, 3013], [17446, 3014], [17444, 3015], [4162, 28219], [34960, 28220],
    [34961, 28221], [34962, 28222], [10952, 28223], [32644, 28224], [18337, 28226],
    [31188, 28227], [27996, 28228], [19675, 28229], [41375, 38660], [21451, 28230],
    [36367, 30224], [36389, 30225], [49533, 46463], [49880, 47423], [49902, 47424],
    [49808, 46000], [49657, 46001], [49659, 46002], [50118, 46003], [56988, 55525]];
  const TB_DG_PICK = Array.from({ length: 11 }, (x, i) => 16295 + 2 * i);
  const TB_DG_HATCHET = Array.from({ length: 11 }, (x, i) => 16361 + 2 * i);
  const TB_FAM_28225 = [34963, 34964];
  const TB_FAM_46004 = [50120, 50121, 50122];
  const TB_MACHETES = [6313, 6315, 6317];   // above base machete 975; tier = vb 4935

  // Mutable: the CS2 switches extraction (script7090, the same resolver this table was baked
  // from) tops the list up with tools added after the bake. Additive only, see cs2SwitchEntries.
  let tbTools = TB_TOOLS.slice();
  function tbVbIds() {
    const ids = new Set([18521, 18522, 45999, 3008, 3009, 4935, 28225, 46004]);
    tbTools.forEach(t => ids.add(t[1]));
    return Array.from(ids).join(',');
  }
  let tbSwAdopted = false;
  async function tbAdoptSwitches() {
    if (tbSwAdopted || typeof cs2SwitchEntries !== 'function') return;
    const ents = await cs2SwitchEntries(7090);
    if (!ents) return;                        // no extraction yet; keep asking
    tbSwAdopted = true;
    const have = new Set(tbTools.map(t => t[0]));
    let added = 0;
    for (const k in ents) {
      const item = k | 0, rec = ents[k];
      const vb = Array.isArray(rec) ? (rec[0] | 0) : (rec | 0);
      if (!(item > 0) || !(vb > 0) || have.has(item)) continue;
      tbTools.push(Array.isArray(rec) && rec.length > 1 ? [item, vb, rec[1] | 0] : [item, vb]);
      added++;
    }
    if (added) tbSig = '';
  }

  let tbVb = null, tbFetching = false, tbFetchAt = 0, tbSig = '';
  let tbEnums = null;           // [enum 6397 hatchets, enum 2433 pickaxes, enum 12936]
  // Toolbelt entries whose cache item config has no name (the belt UI labels them itself).
  const TB_NAME_OVERRIDES = { 47718: 'Incense burner' };
  const tbNames = {};           // item id -> name (cache-static)
  async function tbName(id) {
    if (TB_NAME_OVERRIDES[id]) return (tbNames[id] = TB_NAME_OVERRIDES[id]);
    if (tbNames[id] === undefined) {
      try {
        const d = JSON.parse(await bridge().itemInfo(id) || 'null');
        if (d && d.name) tbNames[id] = d.name;
      } catch (e) {}
    }
    return tbNames[id] || ('Item ' + id);
  }
  async function fetchToolbelt() {
    if (!bridge() || !bridge().varbits || tbFetching) return;
    const t = Date.now();
    if (t - tbFetchAt < 2000) return;
    tbFetchAt = t; tbFetching = true;
    try {
      await tbAdoptSwitches();
      const vb = JSON.parse(await bridge().varbits(myPid(), tbVbIds()) || 'null');
      if (vb && typeof vb === 'object' && Object.keys(vb).length) tbVb = vb;
      if (!tbEnums && bridge().enumInfo) {
        const grab = async id => { try { return JSON.parse(await bridge().enumInfo(id) || 'null'); } catch (e) { return null; } };
        const a = await grab(6397), b = await grab(2433), c = await grab(12936);
        if (a && Object.keys(a).length && b && Object.keys(b).length) tbEnums = [a, b, c || {}];
      }
      for (const [id] of tbTools) await tbName(id);
      for (const id of [1351, 1265, 975, 47718].concat(TB_DG_PICK, TB_DG_HATCHET, TB_FAM_28225, TB_FAM_46004, TB_MACHETES)) await tbName(id);
      if (tbEnums) for (const e of tbEnums) for (const k in e) await tbName(e[k] | 0);
    } catch (e) { /* keep previous */ }
    tbFetching = false;
    paneRun('toolbelt', renderToolbelt);
  }
  const tbV = k => ((tbVb && tbVb[String(k)]) | 0);

  function renderToolbelt() {
    const c = $('content');
    let wrap = $('tbWrap');
    if (!wrap) {
      injectStyle('tbCss', `
          .tb-sec { margin: 2px 12px 0; }
          .tb-st { display: flex; align-items: center; gap: 8px; color: var(--accent-hi); font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: .05em; padding: 10px 2px 6px; }
          .tb-card { background: var(--bg-elev); border: 1px solid var(--border); border-radius: 10px; }
          .tb-row { display: flex; align-items: center; gap: 10px; padding: 6px 12px; }
          .tb-row + .tb-row { border-top: 1px solid var(--border); }
          .tb-ico { flex: 0 0 auto; width: 22px; height: 22px; background-repeat: no-repeat; background-position: center; }
          .tb-nm { flex: 1; min-width: 0; color: var(--text); font-size: 12px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
          .tb-sub { color: var(--text-mute); font-size: 10.5px; margin-top: 1px; }
          .tb-pill { font-size: 10.5px; padding: 2px 9px; border-radius: 999px; border: 1px solid var(--border); color: var(--text-dim); white-space: nowrap; font-variant-numeric: tabular-nums; }
          .tb-pill.ok { color: var(--ok); border-color: rgba(77, 210, 138, .45); }
          .tb-pill.no { color: #e05656; border-color: rgba(224, 86, 86, .45); }
          .tb-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 1px; }
          .tb-grid > .tb-row { min-width: 0; }
          .tb-row .tb-pill { flex: 0 0 auto; }
          .tb-empty { margin: 10px 14px; color: var(--text-dim); font-size: 12px; }`);
      c.innerHTML = ''; tbSig = '';
      wrap = document.createElement('div'); wrap.id = 'tbWrap'; wrap.className = 'pane'; c.appendChild(wrap);
    }
    if (!tbVb) {
      wrap.innerHTML = '<div class="tb-empty">Reading toolbelt state... (be in-world)</div>';
      tbSig = ''; return;
    }
    const sig = tbVbIds().split(',').map(k => tbV(k)).join(',') + '|' +
                (tbEnums ? 'e' : '-') + Object.keys(tbNames).length;
    if (sig === tbSig) return;
    tbSig = sig;
    wrap.innerHTML = '';

    const sec = title => {
      const s = document.createElement('div'); s.className = 'tb-sec';
      const h = document.createElement('div'); h.className = 'tb-st'; h.textContent = title; s.appendChild(h);
      const card = document.createElement('div'); card.className = 'tb-card'; s.appendChild(card);
      wrap.appendChild(s);
      return card;
    };
    const row = (card, itemId, name, sub, pill, pillCls) => {
      const r = document.createElement('div'); r.className = 'tb-row';
      const ico = document.createElement('div'); ico.className = 'tb-ico';
      if (itemId > 0) { const u = resolveIcon(itemId); if (u) setIconBg(ico, u); }
      r.appendChild(ico);
      const nm = document.createElement('div'); nm.className = 'tb-nm';
      const t = document.createElement('div'); t.textContent = name; nm.appendChild(t);
      if (sub) { const sb = document.createElement('div'); sb.className = 'tb-sub'; sb.textContent = sub; nm.appendChild(sb); }
      r.appendChild(nm);
      if (pill != null) { const p = document.createElement('span'); p.className = 'tb-pill' + (pillCls ? ' ' + pillCls : ''); p.textContent = pill; r.appendChild(p); }
      r.dataset.tip = name + (sub ? '\n' + sub : '');   // full text survives ellipsis truncation
      card.appendChild(r);
      return r;
    };
    const nm = id => tbNames[id] || ('Item ' + id);
    const enumMax = e => { let m = -1; for (const k in e) if ((e[k] | 0) > 0) m = Math.max(m, +k); return m; };

    {
      const tt = sec('Tool tiers');
      if (!tbEnums) {
        const e = document.createElement('div'); e.className = 'tb-empty';
        e.textContent = 'Reading tier tables from the cache...';
        tt.appendChild(e);
      } else {
        const [hat, pick, third] = tbEnums;
        // Next populated tier above an index (tier enums can be sparse, e.g. pickaxes).
        const nextIn = (e, cur) => {
          let best = -1;
          for (const k in e) { const i = +k; if (i > cur && (e[k] | 0) > 0 && (best === -1 || i < best)) best = i; }
          return best;
        };
        const nextTxt = (e, cur) => {
          const i = nextIn(e, cur);
          return i === -1 ? 'Maxed' : 'Next: ' + nm(e[String(i)] | 0);
        };
        const hatItem = (hat[String(tbV(18522))] | 0) || 1351;
        const pickItem = (pick[String(tbV(18521))] | 0) || 1265;
        row(tt, hatItem, nm(hatItem), 'Hatchet slot · ' + nextTxt(hat, tbV(18522)),
            'tier ' + (tbV(18522) + 1) + ' / ' + (enumMax(hat) + 1),
            tbV(18522) >= enumMax(hat) ? 'ok' : null);
        row(tt, pickItem, nm(pickItem), 'Pickaxe slot · ' + nextTxt(pick, tbV(18521)),
            'tier ' + (tbV(18521) + 1) + ' / ' + (enumMax(pick) + 1),
            tbV(18521) >= enumMax(pick) ? 'ok' : null);
        if (Object.keys(third).length) {
          // enum 12936 is all mattocks; current = index vb45999 - 1, next = index vb45999.
          const cur = tbV(45999) > 0 ? (third[String(tbV(45999) - 1)] | 0) : 0;
          row(tt, cur, cur > 0 ? nm(cur) : 'None stored',
              'Mattock slot · ' + nextTxt(third, tbV(45999) - 1),
              tbV(45999) + ' / ' + (enumMax(third) + 1), tbV(45999) > enumMax(third) ? 'ok' : null);
        }
      }
      const ladderRow = (ladder, tier, label, baseItem) => {
        const t = Math.min(tier, ladder.length);
        const cur = t > 0 ? ladder[t - 1] : (baseItem || 0);
        const next = t < ladder.length ? 'Next: ' + nm(ladder[t]) : 'Maxed';
        row(tt, cur, cur > 0 ? nm(cur) : 'None stored', label + ' · ' + next,
            t + ' / ' + ladder.length, t >= ladder.length ? 'ok' : null);
      };
      ladderRow(TB_DG_PICK, tbV(3008), 'Daemonheim pickaxe slot');
      ladderRow(TB_DG_HATCHET, tbV(3009), 'Daemonheim hatchet slot');
      {   // machete: base 975 (vb 3002) then vb 4935 upgrades, which need the base stored
        const base = tbV(3002) === 1;
        const t = base ? Math.min(tbV(4935), TB_MACHETES.length) : 0;
        const cur = t > 0 ? TB_MACHETES[t - 1] : (base ? 975 : 0);
        const next = !base ? 'Next: ' + nm(975)
                   : t < TB_MACHETES.length ? 'Next: ' + nm(TB_MACHETES[t]) : 'Maxed';
        row(tt, cur, cur > 0 ? nm(cur) : 'None stored', 'Machete slot · ' + next,
            (base ? t + 1 : 0) + ' / ' + (TB_MACHETES.length + 1), base && t >= TB_MACHETES.length ? 'ok' : null);
      }
      ladderRow(TB_FAM_28225, tbV(28225), 'Explosive shaker slot');
      ladderRow(TB_FAM_46004, tbV(46004), 'Wingsuit slot');
    }

    {
      const stored = [], missing = [];
      for (const [id, vbid, min] of tbTools) {
        (tbV(vbid) >= (min || 1) ? stored : missing).push(id);
      }
      stored.push(47718);   // hardcoded always-stored in script7090
      if (missing.length) {
        const ms = sec('Missing from toolbelt · ' + missing.length);
        const grid = document.createElement('div'); grid.className = 'tb-grid';
        for (const id of missing) {
          const cell = document.createElement('div'); cell.className = 'tb-row';
          cell.dataset.tip = nm(id);
          const ico = document.createElement('div'); ico.className = 'tb-ico';
          const u = resolveIcon(id); if (u) setIconBg(ico, u);
          cell.appendChild(ico);
          const n = document.createElement('div'); n.className = 'tb-nm'; n.textContent = nm(id);
          cell.appendChild(n);
          const p = document.createElement('span'); p.className = 'tb-pill no'; p.textContent = 'Missing';
          cell.appendChild(p);
          grid.appendChild(cell);
        }
        ms.appendChild(grid);
      }
      const stc = sec('Stored · ' + stored.length + ' / ' + (TB_TOOLS.length + 1));
      const grid = document.createElement('div'); grid.className = 'tb-grid';
      for (const id of stored) {
        const cell = document.createElement('div'); cell.className = 'tb-row';
        cell.dataset.tip = nm(id);
        const ico = document.createElement('div'); ico.className = 'tb-ico';
        const u = resolveIcon(id); if (u) setIconBg(ico, u);
        cell.appendChild(ico);
        const n = document.createElement('div'); n.className = 'tb-nm'; n.textContent = nm(id);
        cell.appendChild(n);
        grid.appendChild(cell);
      }
      stc.appendChild(grid);
    }
    sizeAllIcons();
  }
