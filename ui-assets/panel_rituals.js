// RuneToolsX panel: Necromancy - ritual site HUD + City of Um talents / vessel souls.
// Spliced inline into client.html at load; IIFE (window exports + registerTab; see the RTX registry in client.html).
//
// Ritual HUD: varp 11130 = ritual SITE dbrow (master 106), varp 11131 = selected RECIPE
//   dbrow (master 107; name = string col 0, size req 0/1/2 = int col 7). vb 53833 = site
//   tier 0/1/2, vb 53861 = soul attraction %, vb 53849 = light sources placed.
// Talents: vb 53597 points earned, vb 53598 spent, vb 53627 vessel souls, vb 53546 = Rune
//   Mythos progress (tier 3 needs >= 30).
(function () {

  const rtGlyphNames = {};
  async function rtGlyphName(npc) {
    if (npc <= 0) return '';
    if (rtGlyphNames[npc] === undefined && bridge().npcInfo) {
      try {
        const d = JSON.parse(await rtxData.raw('cache.npcInfo', npc) || 'null');
        if (d && d.name) rtGlyphNames[npc] = d.name;
      } catch (e) {}
    }
    return rtGlyphNames[npc] || '';
  }
  // Tier ladder: [Necromancy level, vessel souls] for tiers 1-7.
  const RT_TIERS = [[1, 0], [20, 50], [40, 400], [60, 2000], [70, 4500], [80, 8500], [90, 35000]];
  // Talents come from the cache: tree dbrow 7599 (master 103, col 6 = tier rows) -> tier rows
  // (master 104, col 1 = talent rows in display order) -> talent rows (master 105, col 8 =
  // struct id, col 0 = name override) -> struct params 2794 name / 2795 description.
  // Only the struct -> unlock-varbit map below is baked; 48352-54 are a chain on vb 53587 >= n.
  const RT_TALENT_VBS = {
    48302: [53571, 0], 48303: [53572, 0], 48304: [53573, 0], 48305: [53574, 0],
    48306: [53575, 0], 48307: [53576, 0], 48311: [53577, 0], 48312: [53578, 0],
    48313: [53579, 0], 48327: [53581, 0], 48328: [53582, 0], 48329: [53583, 0],
    48330: [53584, 0], 48331: [53585, 0], 48332: [53586, 0], 31820: [55973, 0],
    32342: [55974, 0], 33965: [54631, 0], 48352: [53587, 1], 48353: [53587, 2], 48354: [53587, 3] };
  const RT_TREE_DBROW = 7599;

  const RT_VB_IDS = '53833,53861,53849,53850,53851,53852,53853,53854,53855,' +
                    '53597,53598,53627,53546,' +
                    Array.from(new Set(Object.values(RT_TALENT_VBS).map(e => e[0]))).join(',');
  const RT_VP_IDS = '11130,11131,11175,11176,11177';
  let rtTalents = null, rtTalentsLoading = false;   // [{tier, struct, name, desc, vb, chain}]
  const rtPlain = s => String(s || '').replace(/<br>/gi, ' ').replace(/<[^>]*>/g, '').trim();
  async function rtLoadTalents() {
    if (rtTalents || rtTalentsLoading) return;
    if (!bridge().dbRows || !bridge().structParams) return;
    rtTalentsLoading = true;
    try {
      const trees = JSON.parse(await rtxData.raw('cache.dbRows', 103) || 'null');
      const tierRows = JSON.parse(await rtxData.raw('cache.dbRows', 104) || 'null');
      const talRows = JSON.parse(await rtxData.raw('cache.dbRows', 105) || 'null');
      if (Array.isArray(trees) && trees.length && Array.isArray(tierRows) && Array.isArray(talRows)) {
        const tree = trees.find(r => r.f === RT_TREE_DBROW);
        const tierList = (tree && tree.i && tree.i['6']) || [];
        const byFid = rows => { const m = {}; for (const r of rows) m[r.f] = r; return m; };
        const tierByFid = byFid(tierRows), talByFid = byFid(talRows);
        const out = [];
        for (let ti = 0; ti < tierList.length; ti++) {
          const tr = tierByFid[tierList[ti]];
          for (const talFid of ((tr && tr.i && tr.i['1']) || [])) {
            const tal = talByFid[talFid];
            const sid = (tal && tal.i && tal.i['8'] && tal.i['8'][0]) | 0;
            if (sid <= 0) continue;
            let name = (tal.s && tal.s['0'] && tal.s['0'][0]) || '', desc = '';
            try {
              const sp = JSON.parse(await rtxData.raw('cache.structParams', sid) || 'null');
              const S = (sp && sp.strs) || {};
              if (!name) name = S['2794'] || '';
              desc = rtPlain(S['2795']);
            } catch (e) {}
            const vbi = RT_TALENT_VBS[sid];
            out.push({ tier: ti + 1, struct: sid, name: name || ('Talent (struct ' + sid + ')'),
                       desc, vb: vbi ? vbi[0] : 0, chain: vbi ? vbi[1] : 0 });
          }
        }
        if (out.length) rtTalents = out;
      }
    } catch (e) { /* retry next fetch */ }
    rtTalentsLoading = false;
  }

  let rtVb = null, rtVp = null, rtFetching = false, rtFetchAt = 0, rtSig = '';
  let rtRecipes = null;         // master 107: fid -> {name, size}
  let rtSites = null;           // master 106: fid -> name (first string col)

  async function rtLoadTables() {
    if (!bridge().dbRows) return;
    if (!rtRecipes) {
      try {
        const rows = JSON.parse(await rtxData.raw('cache.dbRows', 107) || 'null');
        if (Array.isArray(rows) && rows.length) {
          rtRecipes = {};
          for (const r of rows) {
            const nm = r.s && r.s['0'] && r.s['0'][0];
            const sz = (r.i && r.i['7'] && r.i['7'][0]) | 0;
            if (nm) rtRecipes[r.f] = { name: nm, size: sz };
          }
        }
      } catch (e) {}
    }
    if (!rtSites) {
      try {
        const rows = JSON.parse(await rtxData.raw('cache.dbRows', 106) || 'null');
        if (Array.isArray(rows) && rows.length) {
          rtSites = {};
          for (const r of rows) {
            if (!r.s) continue;
            const cols = Object.keys(r.s).map(Number).sort((a, b) => a - b);
            for (const col of cols) {
              const nm = r.s[String(col)] && r.s[String(col)][0];
              if (nm) { rtSites[r.f] = nm; break; }
            }
          }
        }
      } catch (e) {}
    }
  }

  async function fetchRituals() {
    if (!bridge() || !bridge().varbits || rtFetching) return;
    const t = Date.now();
    if (t - rtFetchAt < 2000) return;
    rtFetchAt = t; rtFetching = true;
    try {
      const vb = JSON.parse(await rtxData.raw('state.varbitsCsv', RT_VB_IDS) || 'null');
      if (vb && typeof vb === 'object' && Object.keys(vb).length) rtVb = vb;
      const vp = JSON.parse(await rtxData.raw('state.varps', RT_VP_IDS) || 'null');
      if (vp && typeof vp === 'object' && Object.keys(vp).length) rtVp = vp;
      await rtLoadTables();
      await rtLoadTalents();
      for (const vpid of [11175, 11176, 11177]) await rtGlyphName(rtP(vpid));
    } catch (e) { /* keep previous */ }
    rtFetching = false;
    paneRun('rituals', renderRituals);
  }

  const rtV = k => ((rtVb && rtVb[String(k)]) | 0);
  const rtP = k => ((rtVp && rtVp[String(k)]) | 0);
  function rtAttrPips(v) {
    return v <= 0 ? 0 : v <= 100 ? 1 : v <= 200 ? 2 : v <= 300 ? 3 : v <= 500 ? 4 : 5;
  }

  function renderRituals() {
    const c = $('content');
    let wrap = $('rtWrap');
    if (!wrap) {
      injectStyle('rtCss', `
          .rt-sec { margin: 2px 12px 0; }
          .rt-st { color: var(--accent-hi); font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: .05em; padding: 10px 2px 6px; }
          .rt-card { background: var(--bg-elev); border: 1px solid var(--border); border-radius: 10px; }
          .rt-row { display: flex; align-items: center; gap: 10px; padding: 8px 12px; }
          .rt-row + .rt-row { border-top: 1px solid var(--border); }
          .rt-nm { flex: 1; min-width: 0; color: var(--text); font-size: 12.5px; }
          .rt-sub { color: var(--text-mute); font-size: 10.5px; margin-top: 1px; }
          .rt-val { color: var(--text); font-size: 12px; font-variant-numeric: tabular-nums; }
          .rt-val.ok { color: var(--ok); }
          .rt-val.warn { color: #e0a856; }
          .rt-bar { flex: 0 0 120px; height: 6px; border-radius: 3px; background: var(--bg); border: 1px solid var(--border); overflow: hidden; }
          .rt-fill { height: 100%; background: linear-gradient(90deg, var(--accent-lo), var(--accent-hi)); }
          .rt-fill.full { background: var(--ok); }
          .rt-empty { margin: 10px 14px; color: var(--text-dim); font-size: 12px; }
          .rt-chips { display: flex; gap: 5px; flex-wrap: wrap; }
          .rt-chip { font-size: 10px; padding: 1px 8px; border-radius: 999px; border: 1px solid var(--border); color: var(--text-mute); white-space: nowrap; }
          .rt-chip.ok { color: var(--ok); border-color: rgba(77, 210, 138, .45); }
          .rt-chip.no { color: #e05656; border-color: rgba(224, 86, 86, .45); }`);
      c.innerHTML = ''; rtSig = '';
      wrap = document.createElement('div'); wrap.id = 'rtWrap'; wrap.className = 'pane'; c.appendChild(wrap);
    }
    if (!rtVb) {
      wrap.innerHTML = '<div class="rt-empty">Reading Necromancy state... (be in-world)</div>';
      rtSig = ''; return;
    }
    const sig = RT_VB_IDS.split(',').map(k => rtV(k)).join(',') + '|' +
                RT_VP_IDS.split(',').map(k => rtP(k)).join(',') + '|' +
                (rtRecipes ? 'r' : '-') + (rtSites ? 's' : '-') +
                (rtTalents ? rtTalents.length : 0) + Object.keys(rtGlyphNames).length;
    if (sig === rtSig) return;
    rtSig = sig;
    wrap.innerHTML = '';

    const sec = (title, chips) => {
      const s = document.createElement('div'); s.className = 'rt-sec';
      const h = document.createElement('div'); h.className = 'rt-st';
      h.style.display = 'flex'; h.style.alignItems = 'center'; h.style.gap = '8px';
      const t = document.createElement('span'); t.textContent = title; h.appendChild(t);
      if (chips && chips.length) {
        const cw = document.createElement('span'); cw.className = 'rt-chips';
        for (const ch of chips) {
          const el = document.createElement('span');
          el.className = 'rt-chip ' + (ch.ok ? 'ok' : 'no');
          el.textContent = ch.txt;
          cw.appendChild(el);
        }
        h.appendChild(cw);
      }
      s.appendChild(h);
      const card = document.createElement('div'); card.className = 'rt-card'; s.appendChild(card);
      wrap.appendChild(s);
      return card;
    };
    const row = (card, name, sub, val, valCls, frac) => {
      const r = document.createElement('div'); r.className = 'rt-row';
      const nm = document.createElement('div'); nm.className = 'rt-nm';
      const t = document.createElement('div'); t.textContent = name; nm.appendChild(t);
      if (sub) { const sb = document.createElement('div'); sb.className = 'rt-sub'; sb.textContent = sub; nm.appendChild(sb); }
      r.appendChild(nm);
      if (frac != null) {
        const b = document.createElement('div'); b.className = 'rt-bar';
        const f = document.createElement('div'); f.className = 'rt-fill' + (frac >= 1 ? ' full' : '');
        f.style.width = Math.max(0, Math.min(100, frac * 100)) + '%';
        b.appendChild(f); r.appendChild(b);
      }
      if (val != null) {
        const v = document.createElement('span'); v.className = 'rt-val' + (valCls ? ' ' + valCls : '');
        v.textContent = val; r.appendChild(v);
      }
      card.appendChild(r);
      return r;
    };

    // ---- ritual site HUD ----
    {
      const site = rtP(11130), recipe = rtP(11131);
      const hud = sec('Ritual site');
      if (site <= 0 && recipe <= 0) {
        const e = document.createElement('div'); e.className = 'rt-empty';
        e.textContent = 'No ritual site active. Values populate at a ritual site in the City of Um.';
        hud.appendChild(e);
      } else {
        const siteName = (rtSites && rtSites[site]) || (site > 0 ? 'Site #' + site : 'Unknown site');
        row(hud, siteName, 'Ritual site', 'Tier ' + (rtV(53833) + 1), null, null);
        const rec = rtRecipes && rtRecipes[recipe];
        row(hud, rec ? rec.name : (recipe > 0 ? 'Recipe #' + recipe : 'No recipe selected'), 'Selected ritual', null, null, null);
        const attr = rtV(53861);
        row(hud, 'Soul attraction', 'Tier ' + rtAttrPips(attr) + ' of 5', attr + '%', attr > 500 ? 'ok' : null,
            Math.min(1, attr / 500));
        // Required light sources are 4/6/10 by the recipe's size.
        const lightReq = rec ? [4, 6, 10][rec.size] || 0 : 0;
        if (lightReq > 0) {
          const lit = rtV(53849);
          row(hud, 'Light sources', null, lit + ' / ' + lightReq, lit >= lightReq ? 'ok' : 'warn', lit / lightReq);
        }
        // Glyph slots: [current, required, npc id]; a slot with required == 0 is unused.
        const slots = [[53850, 53853, 11175], [53851, 53854, 11176], [53852, 53855, 11177]];
        for (const [curId, reqId, npcId] of slots) {
          const req = rtV(reqId);
          if (req <= 0) continue;
          const cur = rtV(curId), npc = rtP(npcId);
          row(hud, rtGlyphNames[npc] || (npc > 0 ? 'Glyph (npc ' + npc + ')' : 'Glyph'), 'Glyphs drawn',
              cur + ' / ' + req, cur >= req ? 'ok' : 'warn', cur / req);
        }
      }
    }

    // ---- talents + vessel souls ----
    {
      const tl = sec('City of Um talents');
      // vb 53597 is a LIFETIME earned counter that overshoots the cap, so it is clamped to
      // the spendable total = the number of tier-listed talents (1 point each).
      const spendable = rtTalents ? rtTalents.length : 21;
      const earned = Math.min(spendable, rtV(53597)), spent = rtV(53598);
      const avail = Math.max(0, earned - spent);
      row(tl, 'Talent points', spent + ' spent of ' + earned + ' / ' + spendable + ' earned',
          avail + ' available', avail > 0 ? 'ok' : null, null);
      const souls = rtV(53627);
      let next = null;
      for (let i = 0; i < RT_TIERS.length; i++) {
        if (souls < RT_TIERS[i][1]) { next = [i + 1, RT_TIERS[i][0], RT_TIERS[i][1]]; break; }
      }
      row(tl, 'Vessel souls', next
            ? (next[2] - souls).toLocaleString() + ' more to tier ' + next[0]
            : 'All tier soul thresholds met',
          souls.toLocaleString(), next ? null : 'ok',
          next ? souls / next[2] : 1);
    }

    // ---- individual talents, grouped by tier ----
    {
      if (!rtTalents) {
        const e = document.createElement('div'); e.className = 'rt-empty';
        e.textContent = 'Reading talents from the cache...';
        wrap.appendChild(e);
      } else {
        const souls = rtV(53627);
        const necroLvl = (lastSnap && lastSnap.skills && lastSnap.skills[28] && lastSnap.skills[28][0]) | 0;
        const unlockedOf = e => e.vb > 0 && (e.chain > 0 ? rtV(e.vb) >= e.chain : rtV(e.vb) === 1);
        const tierHasUnlock = t => rtTalents.some(e => e.tier === t && unlockedOf(e));
        const maxTier = rtTalents.reduce((m, e) => Math.max(m, e.tier), 0);
        for (let tier = 1; tier <= maxTier; tier++) {
          const entries = rtTalents.filter(e => e.tier === tier);
          if (!entries.length) continue;
          const [lvl, soulReq] = RT_TIERS[tier - 1] || [0, 0];
          const chips = [];
          if (lvl > 1) chips.push({ txt: 'Lvl ' + lvl, ok: !(necroLvl > 0 && necroLvl < lvl) });
          if (soulReq > 0) chips.push({ txt: soulReq.toLocaleString() + ' souls', ok: souls >= soulReq });
          if (tier > 1) chips.push({ txt: 'Tier ' + (tier - 1) + ' talent', ok: tierHasUnlock(tier - 1) });
          if (tier === 3) chips.push({ txt: 'Rune Mythos', ok: rtV(53546) >= 30 });
          const tierOpen = chips.every(ch => ch.ok);
          const card = sec('Tier ' + tier, chips.filter(ch => !ch.ok));
          for (const e of entries) {
            const unlocked = unlockedOf(e);
            row(card, e.name, e.desc || null,
                e.vb <= 0 ? 'Unlock unknown' : unlocked ? 'Unlocked' : tierOpen ? 'Available' : 'Locked',
                unlocked ? 'ok' : tierOpen || e.vb <= 0 ? null : 'warn', null);
          }
        }
      }
    }
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { fetchRituals });
registerTab({ id: 'rituals', render: renderRituals, open: function () { rtSig = ''; fetchRituals(); } });
})();
