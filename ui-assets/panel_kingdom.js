// RuneToolsX panel: Miscellania kingdom management (Throne of Miscellania / Royal Trouble).
// Spliced inline into client.html; IIFE (window exports + registerTab; see the RTX registry in client.html).
// CS2 ground truth:
//   script10838 - approval = vb 10057 stored 0-127, shown as (v*100)/127 %, coloured green
//                 >= 75, red <= 25, orange between; last visit = varp 6987 (DATE_MINUTES
//                 timestamp, -1 = never), shown as "<t> ago"; coffer = vb 10059 coins.
//   script1117/5770 - coffer cap 5,000,000 -> 7,500,000 and worker cap 10 -> 15 pips when
//                 vb 12425 >= 30 (Royal Trouble complete).
//   script1395  - the six 10-pip resource bars in interface 391, in component order:
//                 comp 56 block = vb 10070, comp 71 = vb 10067, comp 84 = vb 10068,
//                 comp 96 = vb 10069, comp 127 = vb 10085, comp 147 = vb 10086; total
//                 assigned = varp 2242 (15-pip bar). Toggles written by the same script:
//                 vb 10088 = Herbs/Flax (comps 158/160), vb 10087 = Mahogany/Teak/Both
//                 (comps 137/139/141), vb 10060 = Cooked/Raw fish (comp 123).
// The six bars carry NO labels in CS2 - the label texts (Fishing, Wood, Mining, Herbs, Hardwood,
// Farm) are static text comps of interface 391, so this panel decodes the group via
// cacheIfaceGroup(391) and pairs each bar with the label comp on its row by geometry.
// No label is ever guessed: an unpaired bar renders as "Resource".
(function () {

  // Resource bar labels, verified against interface 391's component tree: each resource is one
  // subtree, and its label text, its 10-pip block (script1395's varbit writes) and its toggle live
  // under the same parent - Fishing card (par 29) = block vb 10069 + the cooked/raw toggle
  // comp 123 (vb 10060), Herbs card (par 16) = vb 10068 + the herbs/flax toggle (vb 10088),
  // Hardwood card (par 43) = vb 10085 + the mahogany/teak toggle (vb 10087); Wood (par 27) =
  // vb 10067, Mining (par 10) = vb 10070, Farm (par 47) = vb 10086.
  const KD_BARS = [['Fishing', 10069], ['Wood', 10067], ['Mining', 10070],
                   ['Herbs', 10068], ['Hardwood', 10085], ['Farm', 10086]];
  const KD_VB_IDS = '10057,10059,12425,10060,10087,10088,' +
                    KD_BARS.map(b => b[1]).join(',');
  const KD_VP_IDS = '6987';

  let kdVb = null, kdVp = null, kdFetching = false, kdFetchAt = 0, kdSig = '';

  // Kingdom management is gated on Throne of Miscellania; the quest's progress tracker is resolved
  // from the shared quest defs (panel_quests.js globals) and judged live.
  let kdQuest = null, kdQuestVp = null;
  async function kdEnsureQuest() {
    if (kdQuest || typeof questEnsureDefs !== 'function') return;
    try {
      if (!(await questEnsureDefs())) return;
      const q = QUESTS.find(q2 => q2.n === 'Throne of Miscellania');
      if (q) kdQuest = q;
    } catch (e) {}
  }
  function kdUnlocked() {
    if (!kdQuest || typeof questStatus !== 'function') return null;   // unknown yet
    if (!kdQuestVp) return null;
    try { return questStatus(kdQuest, kdQuestVp) === 2; } catch (e) { return null; }
  }

  async function fetchKingdom() {
    if (!bridge() || !bridge().varbits || kdFetching) return;
    const t = Date.now();
    if (t - kdFetchAt < 2000) return;
    kdFetchAt = t; kdFetching = true;
    try {
      const vb = JSON.parse(await rtxData.raw('state.varbitsCsv', KD_VB_IDS) || 'null');
      if (vb && typeof vb === 'object' && Object.keys(vb).length) kdVb = vb;
      const vp = JSON.parse(await rtxData.raw('state.varps', KD_VP_IDS) || 'null');
      if (vp && typeof vp === 'object' && Object.keys(vp).length) kdVp = vp;
      await kdEnsureQuest();
      // The quest tracker varp (plus any special-case varps questStatus reads) is fetched separately
      // so KD_VP_IDS stays static.
      if (kdQuest) {
        const ids = new Set();
        if (kdQuest.v) ids.add(kdQuest.v[0]);
        if (kdQuest.b) ids.add(kdQuest.b[0]);
        if (ids.size) {
          const qvp = JSON.parse(await rtxData.raw('state.varps', Array.from(ids).join(',')) || 'null');
          if (qvp && typeof qvp === 'object' && Object.keys(qvp).length) kdQuestVp = qvp;
        }
      }
    } catch (e) { /* keep previous */ }
    kdFetching = false;
    paneRun('kingdom', renderKingdom);
  }

  const kdV = k => ((kdVb && kdVb[String(k)]) | 0);
  const kdP = k => ((kdVp && kdVp[String(k)]) | 0);

  function kdAgo(mins) {
    if (mins < 60) return mins + 'm';
    if (mins < 1440) return Math.floor(mins / 60) + 'h ' + (mins % 60) + 'm';
    return Math.floor(mins / 1440) + 'd ' + Math.floor((mins % 1440) / 60) + 'h';
  }

  function renderKingdom() {
    const c = $('content');
    let wrap = $('kdWrap');
    if (!wrap) {
      injectStyle('kdCss', `
          .kd-sec { margin: 2px 12px 0; }
          .kd-st { color: var(--accent-hi); font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: .05em; padding: 10px 2px 6px; }
          .kd-card { background: var(--bg-elev); border: 1px solid var(--border); border-radius: 10px; }
          .kd-row { display: flex; align-items: center; gap: 10px; padding: 8px 12px; }
          .kd-row + .kd-row { border-top: 1px solid var(--border); }
          .kd-nm { flex: 1; min-width: 0; color: var(--text); font-size: 12.5px; }
          .kd-sub { color: var(--text-mute); font-size: 10.5px; margin-top: 1px; }
          .kd-val { color: var(--text); font-size: 12px; font-variant-numeric: tabular-nums; }
          .kd-val.ok { color: var(--ok); }
          .kd-val.warn { color: #e0a856; }
          .kd-val.bad { color: #e05656; }
          .kd-bar { flex: 0 0 120px; height: 6px; border-radius: 3px; background: var(--bg); border: 1px solid var(--border); overflow: hidden; }
          .kd-fill { height: 100%; background: linear-gradient(90deg, var(--accent-lo), var(--accent-hi)); }
          .kd-empty { margin: 10px 14px; color: var(--text-dim); font-size: 12px; }`);
      c.innerHTML = ''; kdSig = '';
      wrap = document.createElement('div'); wrap.id = 'kdWrap'; wrap.className = 'pane'; c.appendChild(wrap);
    }
    if (!kdVb) {
      wrap.innerHTML = '<div class="kd-empty">Reading kingdom state... (be in-world; values sync on login)</div>';
      kdSig = ''; return;
    }
    // Gate on Throne of Miscellania: before it the management varbits are meaningless.
    if (kdUnlocked() === false) {
      wrap.innerHTML = '<div class="kd-empty">Kingdom management unlocks on completing the quest Throne of Miscellania.</div>';
      kdSig = ''; return;
    }
    const sig = KD_VB_IDS.split(',').map(k => kdV(k)).join(',') + '|' +
                KD_VP_IDS.split(',').map(k => kdP(k)).join(',') + '|' +
                JSON.stringify(kdQuestVp || 0) + '|' + Math.floor(Date.now() / 60000);
    if (sig === kdSig) return;
    kdSig = sig;
    wrap.innerHTML = '';

    const sec = title => {
      const s = document.createElement('div'); s.className = 'kd-sec';
      const t = document.createElement('div'); t.className = 'kd-st'; t.textContent = title; s.appendChild(t);
      const card = document.createElement('div'); card.className = 'kd-card'; s.appendChild(card);
      wrap.appendChild(s);
      return card;
    };
    const row = (card, name, sub, val, valCls, barFrac) => {
      const r = document.createElement('div'); r.className = 'kd-row';
      const nm = document.createElement('div'); nm.className = 'kd-nm';
      const t = document.createElement('div'); t.textContent = name; nm.appendChild(t);
      if (sub) { const sb = document.createElement('div'); sb.className = 'kd-sub'; sb.textContent = sub; nm.appendChild(sb); }
      r.appendChild(nm);
      if (barFrac != null) {
        const b = document.createElement('div'); b.className = 'kd-bar';
        const f = document.createElement('div'); f.className = 'kd-fill';
        f.style.width = Math.max(0, Math.min(100, barFrac * 100)) + '%';
        b.appendChild(f); r.appendChild(b);
      }
      if (val != null) {
        const v = document.createElement('span'); v.className = 'kd-val' + (valCls ? ' ' + valCls : '');
        v.textContent = val; r.appendChild(v);
      }
      card.appendChild(r);
      return r;
    };

    const royal = kdV(12425) >= 30;                    // Royal Trouble complete
    const cofferCap = royal ? 7500000 : 5000000;
    const workerCap = royal ? 15 : 10;

    {
      const ov = sec('Kingdom');
      // Approval: stored 0-127, displayed exactly as script10838 does, with its colour bands.
      const appr = Math.floor((kdV(10057) * 100) / 127);
      row(ov, 'Approval', appr < 75 ? 'Do favours for the citizens to raise it' : null,
          appr + '%', appr >= 75 ? 'ok' : appr <= 25 ? 'bad' : 'warn', appr / 100);
      const coffer = kdV(10059);
      row(ov, 'Coffer', royal ? 'Cap 7.5M (Royal Trouble complete)' : 'Cap 5M (7.5M after Royal Trouble)',
          coffer.toLocaleString(), coffer >= 750000 ? null : 'warn', coffer / cofferCap);
      const lastVisit = kdP(6987);
      const nowMin = Math.floor(Date.now() / 60000);
      row(ov, 'Last visited', 'Approval decays daily until you visit',
          lastVisit <= 0 ? 'Never' : kdAgo(Math.max(0, nowMin - lastVisit)) + ' ago',
          lastVisit > 0 && (nowMin - lastVisit) < 1440 ? 'ok' : null);
    }

    {
      const wk = sec('Workers');
      // Total assigned = the sum of the six bars. varp 2242 reads 0 live even with workers assigned
      // (it only drives the interface's pip strip), so it is not trusted.
      const total = KD_BARS.reduce((s, b) => s + kdV(b[1]), 0);
      row(wk, 'Assigned', null, total + ' / ' + workerCap, total >= workerCap ? 'ok' : 'warn', total / workerCap);
      for (const [nm, vbid] of KD_BARS) {
        const n = kdV(vbid);
        // The three collection toggles say WHICH resource that bar's workers gather. In script1395
        // graphic 699 marks the SELECTED radio (exactly one per state): vb 10088 == 1 -> Flax,
        // vb 10087 0/1/2 -> Mahogany/Teak/Both, vb 10060 == 1 -> Cooked.
        let sub = null;
        if (nm === 'Herbs') sub = 'Collecting: ' + (kdV(10088) === 1 ? 'Flax' : 'Herbs');
        else if (nm === 'Hardwood') sub = 'Collecting: ' + (['Mahogany', 'Teak', 'Both'][kdV(10087)] || '?');
        else if (nm === 'Fishing') sub = 'Collecting: ' + (kdV(10060) === 1 ? 'Cooked fish' : 'Raw fish');
        row(wk, nm, sub, n + ' / 10', null, n / 10);
      }
    }
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { fetchKingdom });
registerTab({ id: 'kingdom', render: renderKingdom, open: function () { kdSig = ''; fetchKingdom(); } });
})();
