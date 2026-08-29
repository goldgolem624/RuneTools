// RuneToolsX panel: Tasks (Slayer + Reaper) and the Ticks/Info tabs.
// Spliced inline into client.html at load; shares its global scope (no IIFE).

  // Varps: 183 = slayer count, 185 = slayer creature -> enum 1563 name, 10077 = task streak.
  // 4519 packs reaper: bits[0:5] boss -> enum 9197 name, bits[6:10] count.
  let taskVp = null;
  let slayerCreatures = null;
  let reaperBosses = null;
  // Codex varps (CS2 script13488: creature id c -> varp[c/32] bit c%32, cases 0-6 in this
  // order; its default branch literally asks for "another slayer_codex_x var"). 7*32 = 224 bits
  // of "soul claimed", joined to names through enum 1563, which this panel already loads.
  const ST_CODEX_VARPS = [7020, 7021, 7022, 7023, 7024, 7025, 12309];
  const TASK_VARP_IDS = '183,185,4519,10077,' + ST_CODEX_VARPS.join(',');
  // Block/prefer slot varbits (clientscript-6410); slot values are task-category ids in varp
  // 185's id space, resolved through enum 1563.
  const ST_BLOCK_VBS = [9073, 9075, 9076, 9077, 9081, 9082, 22707, 42839];
  const ST_PREFER_VBS = [24948, 24949, 24950, 24951, 24952, 24953, 24954, 42535];
  // Standing vars: slayer points vb 9071, reaper points vb 22905, streak vb 23260. The reaper
  // ASSIGNMENT itself belongs to the task card, read from packed varp 4519. Gates:
  // vb 9072 == 0 means "complete one task"; special assignment = vb 525 > 0 or vb 24968 == 1
  // or vb 44233 == 1, and vb 24968 == 1 also means the cancel is free. Costs (clientscript-6410
  // button texts): cancel 30 / extend 30 / block 100 / prefer 100 points.
  const ST_STANDING_VBS = [9071, 9072, 525, 24968, 44233, 22905, 23260];
  // Mask kill counters (clientscript-5828): [mask item id, kills varbit, force-cooldown varbit
  // or 0]. The cooldown varbit holds a DATE_RUNEDAY deadline; the wyrm masks share vb 21951.
  const ST_MASKS = [
    [27616, 18238, 0],     [27618, 18242, 0],     [27620, 18246, 0],     [27622, 18250, 0],
    [27624, 18254, 0],     [28686, 18611, 0],     [28688, 18615, 0],     [28690, 18619, 0],
    [28692, 18623, 0],     [28694, 18627, 0],     [31089, 21930, 0],     [31091, 21934, 0],
    [31093, 21938, 21951], [31095, 21942, 21951], [31097, 21946, 21951], [31099, 21950, 0],
    [35277, 26736, 28874], [35279, 26740, 0],     [35281, 26744, 28875], [35283, 28861, 28876],
    [35285, 28865, 28877], [35287, 28869, 28878], [35289, 28873, 28879]];
  const ST_EXTRA_VB_IDS = ST_BLOCK_VBS.concat(ST_PREFER_VBS, ST_STANDING_VBS,
    ST_MASKS.map(m => m[1]), ST_MASKS.map(m => m[2]).filter(Boolean)).join(',');
  const stMaskNames = {};
  let stStandingSig = '', stSlotsSig = '', stMasksSig = '', stCodexSig = '';
  // [name, killsVarbit, prestigeVarbit], baked from CS2 script5511's switch: the name<->varbit
  // pairing exists only there, and enum 10555's order does not match it.
  const SLAYER_LOG = [
    ['Aberrant spectres', 22924, 29825], ['Abyssal demons', 22935, 29836],
    ['Acheron mammoths', 29821, 29856], ['Adamant dragons', 28374, 29850],
    ['Airut', 22937, 29838], ['Aquanites', 22929, 29830],
    ['Ascension creatures', 22932, 29833], ['Automatons', 22926, 29827],
    ["Beastmaster's hounds", 58206, 58207], ['Bloodvelds', 22922, 29823],
    ['Camel warriors', 29820, 29855], ['Celestial dragons', 30017, 30018],
    ['Corrupted creatures', 36174, 36176], ['Crystal shapeshifters', 29381, 29852],
    ['Dark beasts', 22936, 29837], ['Desert strykewyrms', 22945, 29846],
    ['Dinosaurs', 44238, 44241], ['Dust devils', 22925, 29826],
    ['Edimmu', 25877, 29847], ['Gargoyles', 22942, 29843],
    ['Gemstone dragons', 35273, 35274], ['Glacors', 22940, 29841],
    ['Grove creatures', 30277, 30278], ['Ice strykewyrms', 22938, 29839],
    ['Jungle strykewyrms', 22928, 29829], ["Kal'gerion demons", 22939, 29840],
    ['Lava strykewyrms', 27007, 29849], ['Living Wyverns', 29818, 29853],
    ['Muspah', 22943, 29844], ['Mutated jadinkos', 22931, 29832],
    ['Nechryael', 22930, 29831], ['Nightmare creatures', 34339, 34340],
    ['Nihil', 22944, 29845], ['Nodon dragonkin', 49725, 49726],
    ['Polypore creatures', 22933, 29834], ['Profane scabarites', 56209, 56222],
    ['Revenants', 43665, 43666], ['Ripper Demons', 29819, 29854],
    ['Rune dragons', 28375, 29851], ['Sanguine crawlers', 60740, 60741],
    ['Shadow creatures', 683, 29848], ['Skeletal wyverns', 22927, 29828],
    ['Soul devourers', 36175, 36177], ['Spiritual mages', 22934, 29835],
    ['Stalker creatures', 38935, 38936], ['Terror dogs', 22921, 29822],
    ['Tormented demons', 22941, 29842], ['Vile blooms', 44239, 44240],
    ['Warped tortoises', 22923, 29824], ["Zemouregal's undead", 52486, 52487]];
  // Collection rows come from dbrows master 84 (region col 4, collection struct col 11, unique-drop
  // items col 13); the struct gives name (param 6410), title (2533), description (6411).
  // Struct -> status varbit (0 not started, 1 in progress, 2 complete), baked from CS2 script14503.
  const SLAYER_COL_VBS = {
    15023: 45092, 15024: 45093, 15026: 45094, 15027: 45095, 15028: 45096, 15029: 45097,
    15030: 45098, 15025: 45099, 15031: 45100, 15032: 45101, 15033: 45102, 15034: 45103,
    45440: 49797, 52981: 60807 };
  // Per-item found flags are client varbits; the item-id -> varbit table lives in panel_bosses.js.
  let slayerCols = null;        // [{region, name, title, vb|null, items:[{id,name,vb|[vb,vb]|null}]}]
  let slayerColsLoading = false;
  let slayerColItemIds = '';
  const SLAYER_LOG_IDS = SLAYER_LOG.map(e => e[1]).concat(SLAYER_LOG.map(e => e[2]))
                                   .concat(Object.values(SLAYER_COL_VBS)).join(',');
  let slayerLogVb = null, slayerLogSig = '';
  async function slayerColLoad() {
    if (slayerCols || slayerColsLoading) return;
    if (!bridge().dbRows || !bridge().structParams || !bridge().itemInfo) return;
    slayerColsLoading = true;
    try {
      // Shares panel_bosses' baked-and-extracted item->varbit tables; adopt before reading.
      if (typeof bcAdoptSwitches === 'function') await bcAdoptSwitches();
      const rows = await bridgeJson('dbRows', 84);
      if (!Array.isArray(rows) || !rows.length) return;   // cache not open yet; retry next fetch
      const out = [];
      for (const r of rows) {
        const sid = (r.i && r.i['11'] && r.i['11'][0]) | 0;
        const region = r.s && r.s['4'] && r.s['4'][0];
        if (sid <= 0 || !region) continue;
        let sp = null;
        sp = await bridgeJson('structParams', sid);
        const S = (sp && sp.strs) || {};
        if (!/Slayer monsters/i.test(S['6411'] || '')) continue;   // boss logs live in the same table
        const items = [];
        for (const id of ((r.i && r.i['13']) || [])) {
          let nm = '';
          { const d = await bridgeJson('itemInfo', id); nm = (d && d.name) || ''; }
          // Found varbit: baked map, else item param 8994 var_reference ((v >>> 24) == 1 -> low 24 bits).
          let vb = COLLECTION_ITEM_VBS[id] || COLLECTION_ITEM_VB_PAIR[id] || null;
          if (!vb && bridge().itemParams) {
            try {
              const sp = await bridgeJson('itemParams', id);
              const ref = (sp && sp.ints && sp.ints['8994']) | 0;
              if ((ref >>> 24) === 1) vb = ref & 0xFFFFFF;
            } catch (e) {}
          }
          items.push({ id, name: nm || ('Item ' + id), vb });
        }
        out.push({ region, name: S['6410'] || region, title: S['2533'] || '',
                   vb: SLAYER_COL_VBS[sid] || null, items });
      }
      if (out.length) {
        out.sort((a, b) => a.region.localeCompare(b.region));
        const ids = new Set();
        for (const col of out) for (const it of col.items) {
          if (Array.isArray(it.vb)) it.vb.forEach(x => ids.add(x));
          else if (it.vb) ids.add(it.vb);
        }
        slayerColItemIds = Array.from(ids).join(',');
        slayerCols = out;
      }
    } catch (e) { /* retry next fetch */ }
    slayerColsLoading = false;
  }
  async function fetchTasks() {
    if (!bridge() || !bridge().varps) return;
    { const d = await bridgeJson('varps', myPid(), TASK_VARP_IDS); if (d && typeof d === 'object') taskVp = d; }
    if (bridge().varbits) {
      try {
        const ids = SLAYER_LOG_IDS + ',' + ST_EXTRA_VB_IDS + (slayerColItemIds ? ',' + slayerColItemIds : '');
        const vb = await bridgeJson('varbits', myPid(), ids);
        if (vb && typeof vb === 'object' && Object.keys(vb).length) slayerLogVb = vb;
      } catch (e) {}
    }
    await slayerColLoad();
    if (bridge().enumInfo) {
      if (!slayerCreatures) { { const m = await bridgeJson('enumInfo', 1563); if (m && Object.keys(m).length) slayerCreatures = m; } }
      if (!reaperBosses)    { { const m = await bridgeJson('enumInfo', 9197); if (m && Object.keys(m).length) reaperBosses    = m; } }
    }
    if (slayerLogVb && bridge().itemInfo) {
      for (const [item, kv, cv] of ST_MASKS) {
        const active = ((slayerLogVb[String(kv)] | 0) > 0) || (cv && (slayerLogVb[String(cv)] | 0) > 0);
        if (!active || stMaskNames[item] !== undefined) continue;
        try { stMaskNames[item] = ((await bridgeJson('itemInfo', item)) || {}).name || ''; }
        catch (e) { stMaskNames[item] = ''; }
      }
    }
  }
  const SLAYER_ICON = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="8"/><circle cx="12" cy="12" r="2.6"/><path d="M12 1.5v3.5M12 19v3.5M1.5 12h3.5M19 12h3.5"/></svg>';
  const REAPER_ICON = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M12 3a6 6 0 0 0-6 6v2.6c0 1 .4 1.9 1 2.6V18a1 1 0 0 0 1 1h1v-2h1.2v2h1.6v-2H14v2h1a1 1 0 0 0 1-1v-3.8c.6-.7 1-1.6 1-2.6V9a6 6 0 0 0-6-6z"/><circle cx="9.6" cy="10.6" r="1.25" fill="currentColor" stroke="none"/><circle cx="14.4" cy="10.6" r="1.25" fill="currentColor" stroke="none"/></svg>';
  function taskCardHtml(kind, title, icon) {
    return '<div class="task-card tc-' + kind + '">' +
             '<div class="tc-head"><span class="tc-ic">' + icon + '</span><span class="tc-kind">' + title + '</span></div>' +
             '<div class="tc-name" id="tc-' + kind + '-name">-</div>' +
             '<div class="tc-foot"><span class="tc-num" id="tc-' + kind + '-num">-</span><span class="tc-rem" id="tc-' + kind + '-rem">remaining</span></div>' +
           '</div>';
  }
  function setTask(kind, name, count) {
    const nEl = document.getElementById('tc-' + kind + '-name');
    const numEl = document.getElementById('tc-' + kind + '-num');
    const remEl = document.getElementById('tc-' + kind + '-rem');
    if (!nEl || !numEl || !remEl) return;
    if (!name) {
      nEl.textContent = taskVp ? 'No task' : 'Loading...'; nEl.classList.add('tc-none');
      numEl.textContent = '-'; remEl.style.visibility = 'hidden';
    } else {
      nEl.textContent = name; nEl.classList.remove('tc-none');
      numEl.textContent = (count == null) ? '-' : Number(count).toLocaleString();
      remEl.style.visibility = '';
    }
  }
  function renderTasks() {
    const c = $('content');
    if (!document.getElementById('tasksWrap')) {
      injectStyle('stCss', `
          .st-sec { color: var(--accent-hi); font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: .05em; padding: 12px 2px 6px; }
          .st-card { background: var(--bg-elev); border: 1px solid var(--border); border-radius: 10px; }
          .st-row { display: flex; align-items: center; gap: 10px; padding: 6px 12px; }
          .st-row + .st-row { border-top: 1px solid var(--border); }
          .st-nm { flex: 1; min-width: 0; color: var(--text); font-size: 12px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
          .st-k, .st-p { width: 64px; text-align: right; font-size: 12px; font-variant-numeric: tabular-nums; }
          .st-k { color: var(--text); }
          .st-p { color: var(--text-dim); }
          .st-hdr .st-nm, .st-hdr .st-k, .st-hdr .st-p { color: var(--text-mute); font-size: 10.5px; text-transform: uppercase; letter-spacing: .04em; }
          .st-more { padding: 7px 12px; color: var(--text-mute); font-size: 10.5px; }
          .st-sub { color: var(--text-mute); font-size: 10.5px; margin-top: 1px; }
          .st-pill { font-size: 10.5px; padding: 2px 9px; border-radius: 999px; border: 1px solid var(--border); color: var(--text-dim); white-space: nowrap; }
          .st-pill.ok { color: var(--ok); border-color: rgba(77, 210, 138, .45); }
          .st-pill.go { color: var(--accent-hi); border-color: var(--accent-lo); }
          .st-pill.no { color: #e05656; border-color: rgba(224, 86, 86, .45); }`);
      c.innerHTML = '<div class="pane"><div class="tasks-wrap" id="tasksWrap">' +
        taskCardHtml('slayer', 'Slayer Task', SLAYER_ICON) +
        taskCardHtml('reaper', 'Reaper Task', REAPER_ICON) +
        '</div>' +
        '<div class="st-sec">Points & streaks</div>' +
        '<div class="st-card" id="stStanding"></div>' +
        '<div class="st-sec" id="stSlotsHead">Blocked & preferred tasks</div>' +
        '<div class="st-card" id="stSlots"></div>' +
        '<div class="st-sec" id="stColHead">Slayer collection log</div>' +
        '<div class="st-card" id="stCollections"></div>' +
        '<div class="st-sec">Slayer creature log</div>' +
        '<div class="st-card" id="stLog"></div>' +
        '<div class="st-sec" id="stCodexHead">Slayer codex souls</div>' +
        '<div class="st-card" id="stCodex"></div>' +
        '<div class="st-sec" id="stMaskHead">Slayer mask kill counters</div>' +
        '<div class="st-card" id="stMasks"></div></div>';
      // Section sigs must reset when the DOM is rebuilt (a tab switch wipes #content), or a stale
      // sig leaves the card empty until a varbit changes.
      slayerLogSig = '';
      slayerColSig = '';
      stStandingSig = ''; stSlotsSig = ''; stMasksSig = ''; stCodexSig = '';
    }
    let sName = null, sCount = null;
    if (taskVp) {
      const cid = taskVp['185'] | 0;
      if (cid) {
        sCount = taskVp['183'] | 0;
        sName = (slayerCreatures && slayerCreatures[String(cid)]) || ('#' + cid);
      }
    }
    setTask('slayer', sName, sCount);
    let rName = null, rCount = null;
    if (taskVp) {
      const packed = taskVp['4519'] | 0;
      const bossId = packed & 0x3f;
      if (bossId) {
        rCount = (packed >> 6) & 0x1f;
        rName = (reaperBosses && reaperBosses[String(bossId)]) || ('#' + bossId);
      }
    }
    setTask('reaper', rName, rCount);
    renderSlayerStanding();
    renderSlayerSlots();
    renderSlayerCollections();
    renderSlayerLog();
    renderSlayerCodex();
    renderSlayerMasks();
  }
  // Slayer codex: which creature souls are claimed. One bit per creature across the seven
  // codex varps; enum 1563 supplies the name for each id. Ids the enum does not name are
  // counted but not listed (a future creature's bit set before the cache knows it).
  function renderSlayerCodex() {
    const box = document.getElementById('stCodex');
    const head = document.getElementById('stCodexHead');
    if (!box) return;
    if (!taskVp || !slayerCreatures) {
      if (stCodexSig !== 'wait') { stCodexSig = 'wait'; box.innerHTML = '<div class="st-more">Reading codex... (be in-world)</div>'; }
      return;
    }
    const have = id => {
      const vp = taskVp[String(ST_CODEX_VARPS[id >> 5])];
      return typeof vp === 'number' ? ((vp >>> (id & 31)) & 1) === 1 : false;
    };
    const ids = Object.keys(slayerCreatures).map(Number).filter(n => n >= 0 && n < ST_CODEX_VARPS.length * 32);
    const sig = ST_CODEX_VARPS.map(v => taskVp[String(v)] | 0).join(',') + '|' + ids.length;
    if (sig === stCodexSig) return;
    stCodexSig = sig;
    const claimed = ids.filter(have), missing = ids.filter(id => !have(id));
    if (head) head.textContent = 'Slayer codex souls (' + claimed.length + ' / ' + ids.length + ' claimed)';
    let html = '';
    if (!missing.length) {
      html = '<div class="st-row"><div class="st-nm">Every codex soul is claimed.</div></div>';
    } else {
      html = '<div class="st-row st-hdr"><div class="st-nm">Missing souls (' + missing.length + ')</div></div>';
      const names = missing.map(id => slayerCreatures[String(id)] || ('#' + id)).sort((a, b) => a.localeCompare(b));
      for (const nm of names) html += '<div class="st-row"><div class="st-nm">' + nm + '</div></div>';
    }
    box.innerHTML = html;
  }
  function renderSlayerStanding() {
    const box = document.getElementById('stStanding');
    if (!box) return;
    if (!slayerLogVb) {
      if (stStandingSig !== 'wait') { stStandingSig = 'wait'; box.innerHTML = '<div class="st-more">Reading... (be in-world)</div>'; }
      return;
    }
    const v = id => (slayerLogVb[String(id)] | 0);
    const vp = id => ((taskVp && taskVp[String(id)]) | 0);
    const special = v(525) > 0 || v(24968) === 1 || v(44233) === 1;
    const sig = [v(9071), v(9072), vp(10077), v(22905), v(23260),
                 special ? 1 : 0, v(24968)].join(',');
    if (sig === stStandingSig) return;
    stStandingSig = sig;
    box.innerHTML = '';
    const row = (name, sub, pill, pillCls, tip) => {
      const r = document.createElement('div'); r.className = 'st-row';
      const nm = document.createElement('div'); nm.className = 'st-nm';
      const t = document.createElement('div'); t.textContent = name; nm.appendChild(t);
      if (sub) { const sb = document.createElement('div'); sb.className = 'st-sub'; sb.textContent = sub; nm.appendChild(sb); }
      r.appendChild(nm);
      if (pill != null) {
        const p = document.createElement('span');
        p.className = 'st-pill' + (pillCls ? ' ' + pillCls : ''); p.textContent = pill;
        r.appendChild(p);
      }
      if (tip) r.dataset.tip = tip;
      box.appendChild(r);
      return r;
    };
    // The game hides its whole points UI while vb 9072 == 0.
    if (v(9072) === 0) {
      row('Slayer points', null, 'complete one task first', 'no',
          'The game unlocks the Slayer points shop after your first completed task (varbit 9072).');
    } else {
      row('Slayer points', 'Cancel 30 · Extend 30 · Block 100 · Prefer 100',
          v(9071).toLocaleString(), null,
          'Varbit 9071. Costs from the game\'s own reward-shop buttons (clientscript-6410):' +
          '\nCancel task 30 · Extend task 30 · Block task 100 · Prefer task 100 points.');
    }
    row('Slayer task streak', null, vp(10077).toLocaleString(), null, 'Varp 10077.');
    if (special) {
      row('Special assignment active', v(24968) === 1 ? 'cancelling it is free right now' : null,
          v(24968) === 1 ? 'free cancel' : 'active', v(24968) === 1 ? 'ok' : 'go',
          'Flags: vb 525 = ' + v(525) + ', vb 24968 (free cancel) = ' + v(24968) + ', vb 44233 = ' + v(44233) + '.');
    }
    row('Reaper points', null, v(22905).toLocaleString(), null, 'Varbit 22905.');
    row('Reaper streak', null, v(23260).toLocaleString(), null, 'Varbit 23260.');
  }
  function renderSlayerSlots() {
    const box = document.getElementById('stSlots');
    if (!box) return;
    if (!slayerLogVb) {
      if (stSlotsSig !== 'wait') { stSlotsSig = 'wait'; box.innerHTML = '<div class="st-more">Reading... (be in-world)</div>'; }
      return;
    }
    const v = id => (slayerLogVb[String(id)] | 0);
    const name = id => (slayerCreatures && slayerCreatures[String(id)]) || ('#' + id);
    const blocks = ST_BLOCK_VBS.map(v).filter(x => x > 0);
    const prefers = ST_PREFER_VBS.map(v).filter(x => x > 0);
    const sig = blocks.join(',') + '|' + prefers.join(',') + '|' + (slayerCreatures ? 'e' : '-');
    if (sig === stSlotsSig) return;
    stSlotsSig = sig;
    const head = document.getElementById('stSlotsHead');
    if (head) head.textContent = 'Blocked & preferred tasks · Block ' + blocks.length + '/' +
      ST_BLOCK_VBS.length + ' · Prefer ' + prefers.length + '/' + ST_PREFER_VBS.length;
    box.innerHTML = '';
    const line = (label, ids, cost) => {
      const r = document.createElement('div'); r.className = 'st-row';
      const nm = document.createElement('div'); nm.className = 'st-nm';
      const t = document.createElement('div'); t.textContent = label; nm.appendChild(t);
      const sb = document.createElement('div'); sb.className = 'st-sub';
      sb.textContent = ids.length ? ids.map(name).join(' · ') : 'no slots used';
      nm.appendChild(sb);
      r.appendChild(nm);
      const p = document.createElement('span');
      p.className = 'st-pill' + (ids.length ? ' go' : '');
      p.textContent = ids.length + ' / 8';
      r.appendChild(p);
      r.dataset.tip = label + ' (' + cost + ' Slayer points per slot):\n' +
        (ids.length ? ids.map(id => '  ' + name(id)).join('\n') : '  (empty)');
      box.appendChild(r);
    };
    line('Blocked tasks', blocks, 100);
    line('Preferred tasks', prefers, 100);
  }
  // Only masks with a nonzero counter or a pending force deadline render: the vars carry no
  // ownership signal, so an owned-but-unused mask looks like an unowned one.
  function renderSlayerMasks() {
    const box = document.getElementById('stMasks');
    const head = document.getElementById('stMaskHead');
    if (!box) return;
    if (!slayerLogVb) {
      if (stMasksSig !== 'wait') { stMasksSig = 'wait'; box.innerHTML = '<div class="st-more">Reading... (be in-world)</div>'; }
      return;
    }
    const v = id => (slayerLogVb[String(id)] | 0);
    const today = biRuneDay();
    const active = ST_MASKS.filter(([it, kv, cv]) => v(kv) > 0 || (cv && v(cv) > 0));
    const sig = active.map(([it, kv, cv]) => it + ':' + v(kv) + ':' + (cv ? v(cv) : 0)).join(',') +
                '|' + today + '|' + Object.keys(stMaskNames).length;
    if (sig === stMasksSig) return;
    stMasksSig = sig;
    box.innerHTML = '';
    if (head) head.textContent = 'Slayer mask kill counters · ' + active.length + ' / ' + ST_MASKS.length;
    if (!active.length) {
      box.innerHTML = '<div class="st-more">No mask kills recorded yet. Masks with zero kills stay hidden ' +
        '(the vars carry no ownership signal).</div>';
      return;
    }
    for (const [item, kv, cv] of active) {
      const r = document.createElement('div'); r.className = 'st-row';
      const ico = document.createElement('div');
      ico.style.cssText = 'flex:0 0 auto;width:22px;height:22px;background-repeat:no-repeat;background-position:center;';
      const u = resolveIcon(item); if (u) setIconBg(ico, u);
      r.appendChild(ico);
      const nm = document.createElement('div'); nm.className = 'st-nm';
      const nameTxt = stMaskNames[item] || ('Item ' + item);
      const t = document.createElement('div'); t.textContent = nameTxt; nm.appendChild(t);
      let cdTxt = '';
      if (cv) {
        const dl = v(cv);
        // The deadline varbit is a DATE_RUNEDAY; 0 = never set, future = cooling down.
        if (dl > today) cdTxt = 'task force in ' + (dl - today) + 'd';
        else if (dl > 0) cdTxt = 'task forceable';
      }
      if (cdTxt) { const sb = document.createElement('div'); sb.className = 'st-sub'; sb.textContent = cdTxt; nm.appendChild(sb); }
      r.appendChild(nm);
      const p = document.createElement('span');
      const cooling = cv && v(cv) > today;
      p.className = 'st-pill' + (cooling ? '' : cv && v(cv) > 0 ? ' ok' : '');
      p.textContent = v(kv).toLocaleString() + ' kills';
      r.appendChild(p);
      r.dataset.tip = nameTxt + '\nKills: varbit ' + kv +
        (cv ? '\nForce deadline: varbit ' + cv + ' (runeday ' + v(cv) + ', today ' + today + ')'
            : '\nThis mask has no task-force cooldown var.');
      box.appendChild(r);
    }
    const foot = document.createElement('div'); foot.className = 'st-more';
    foot.textContent = 'Masks with zero recorded kills are hidden (no ownership signal in the vars).';
    box.appendChild(foot);
    sizeAllIcons();
  }
  // Region collections: tri-state status per varbit, with the unique-drop roster in the tooltip.
  let slayerColSig = '';
  function renderSlayerCollections() {
    const box = document.getElementById('stCollections');
    if (!box) return;
    if (!slayerCols || !slayerLogVb) {
      if (slayerColSig !== 'wait') {
        slayerColSig = 'wait';
        box.innerHTML = '<div class="st-more">Reading collections from the cache... (be in-world)</div>';
      }
      return;
    }
    const v = id => (slayerLogVb[String(id)] | 0);
    // An item is collected when its found varbit reads >= 1 (a paired composite sums both).
    const owned = it => it.vb == null ? null
                      : Array.isArray(it.vb) ? (it.vb.reduce((n, x) => n + v(x), 0) >= 1)
                      : v(it.vb) >= 1;
    const sig = slayerCols.map(c => c.items.map(it => { const o = owned(it); return o == null ? 'x' : o ? 1 : 0; }).join('')).join(',');
    if (sig === slayerColSig) return;
    slayerColSig = sig;
    let doneCols = 0;
    box.innerHTML = '';
    for (const col of slayerCols) {
      const have = col.items.filter(it => owned(it) === true);
      const missing = col.items.filter(it => owned(it) === false);
      const n = have.length, m = col.items.length;
      const complete = m > 0 && n === m;
      if (complete) doneCols++;
      const r = document.createElement('div'); r.className = 'st-row';
      const nm = document.createElement('div'); nm.className = 'st-nm';
      const t = document.createElement('div'); t.textContent = col.region; nm.appendChild(t);
      const sb = document.createElement('div'); sb.className = 'st-sub';
      sb.textContent = col.name + (col.title ? ' · Title: ' + col.title : '');
      nm.appendChild(sb);
      r.appendChild(nm);
      const p = document.createElement('span');
      p.className = 'st-pill' + (complete ? ' ok' : n > 0 ? ' go' : '');
      p.textContent = n + ' / ' + m;
      r.appendChild(p);
      // Drop lists render as two grids of item ICONS under the text (setTipGrids, client.html).
      r.dataset.tip = col.name + (col.title ? '\nReward: the title \'' + col.title + '\'' : '');
      if (typeof setTipGrids === 'function')
        setTipGrids(r, [{ label: 'Missing', cls: 'miss', items: missing },
                        { label: 'Collected', cls: 'have', items: have }]);
      box.appendChild(r);
    }
    const head = document.getElementById('stColHead');
    if (head) head.textContent = 'Slayer collection log · ' + doneCols + ' / ' + slayerCols.length + ' complete';
  }
  function renderSlayerLog() {
    const log = document.getElementById('stLog');
    if (!log) return;
    if (!slayerLogVb) {
      if (slayerLogSig !== 'wait') {
        slayerLogSig = 'wait';
        log.innerHTML = '<div class="st-more">Reading kill counts... (be in-world)</div>';
      }
      return;
    }
    const v = id => (slayerLogVb[String(id)] | 0);
    const sig = SLAYER_LOG.map(e => v(e[1]) + ':' + v(e[2])).join(',');
    if (sig === slayerLogSig) return;
    slayerLogSig = sig;
    const rows = SLAYER_LOG.map(([nm, kv, pv]) => ({ nm, k: v(kv), p: v(pv) }));
    rows.sort((a, b) => (b.k - a.k) || (b.p - a.p) || a.nm.localeCompare(b.nm));
    const seen = rows.filter(r => r.k > 0 || r.p > 0);
    const totK = rows.reduce((n, r) => n + r.k, 0), totP = rows.reduce((n, r) => n + r.p, 0);
    let html = '<div class="st-row st-hdr"><div class="st-nm">Creature</div>' +
               '<div class="st-k">Kills</div><div class="st-p">Prestige</div></div>' +
               '<div class="st-row"><div class="st-nm">Total</div>' +
               '<div class="st-k">' + totK.toLocaleString() + '</div>' +
               '<div class="st-p">' + totP.toLocaleString() + '</div></div>';
    for (const r of seen) {
      html += '<div class="st-row"><div class="st-nm">' + r.nm + '</div>' +
              '<div class="st-k">' + r.k.toLocaleString() + '</div>' +
              '<div class="st-p">' + (r.p > 0 ? r.p.toLocaleString() : '·') + '</div></div>';
    }
    if (seen.length < rows.length)
      html += '<div class="st-more">' + (rows.length - seen.length) + ' creatures with no recorded kills</div>';
    log.innerHTML = html;
  }
  function renderTicks() {
    const c = $('content'); const s = lastSnap;
    let wrap = $('tkWrap');
    if (!wrap) {
      c.innerHTML = '';
      wrap = document.createElement('div'); wrap.id = 'tkWrap'; wrap.className = 'pane';
      const rows = document.createElement('div'); rows.id = 'tkRows'; rows.className = 'rows'; wrap.appendChild(rows);
      const m = document.createElement('div'); m.className = 'metro';
      const lab = document.createElement('div'); lab.className = 'metro-next'; lab.textContent = 'Tick metronome';
      const modes = document.createElement('div'); modes.className = 'metro-modes';
      METRO_MODES.forEach(([v, t]) => {
        const b = document.createElement('button'); b.textContent = t; b.dataset.m = v;
        b.addEventListener('click', () => setMetroMode(v));
        modes.appendChild(b);
      });
      const ivLab = document.createElement('div'); ivLab.className = 'metro-next'; ivLab.textContent = 'Beat every (ticks)';
      const ivs = document.createElement('div'); ivs.className = 'metro-modes';
      for (let n = 1; n <= 6; n++) {
        const b = document.createElement('button'); b.textContent = String(n); b.dataset.iv = String(n);
        b.addEventListener('click', () => setMetroInterval(n));
        ivs.appendChild(b);
      }
      const lockRow = document.createElement('div'); lockRow.className = 'metro-modes';
      const lockBtn = document.createElement('button'); lockBtn.id = 'metroLockBtn';
      lockBtn.addEventListener('click', () => setMetroLock(!metroLocked));
      lockRow.appendChild(lockBtn);
      const hint = document.createElement('div'); hint.className = 'metro-next';
      hint.innerHTML = 'Visual opens the <b>Metronome</b> window over the game - drag its title bar to move, ' +
                       'resize from any edge. Audio clicks on the tick itself, with or without the dial.';
      m.appendChild(lab); m.appendChild(modes); m.appendChild(ivLab); m.appendChild(ivs); m.appendChild(lockRow); m.appendChild(hint);
      wrap.appendChild(m);
      c.appendChild(wrap);
    }
    const rows = $('tkRows'); rows.innerHTML = '';
    rows.appendChild(row('Tick #', (s && s.tick_count || 0).toLocaleString()));
    rows.appendChild(row('Last tick', (s && s.last_tick_ms > 0) ? s.last_tick_ms.toFixed(0) + ' ms' : '--'));
    wrap.querySelectorAll('.metro-modes button[data-m]').forEach(b => b.classList.toggle('on', b.dataset.m === metroMode));
    wrap.querySelectorAll('.metro-modes button[data-iv]').forEach(b => b.classList.toggle('on', Number(b.dataset.iv) === metroInterval));
    const lb = $('metroLockBtn');
    if (lb) {
      lb.textContent = metroLocked ? 'Locked (click-through)' : 'Lock position';
      lb.classList.toggle('on', metroLocked);
      // A locked window publishes no consume rect, so its own lock button is unreachable:
      // this is the only way back. Disabling it while the dial is closed would strand that.
      lb.title = metroLocked ? 'Unlock the Metronome window so it can be moved again'
                             : 'Make the Metronome window click-through';
    }
  }

  // No always-on UI-thread loop: the stopwatch is on-demand and the metronome runs in the
  // overlay, so an idle panel never wakes the render thread.

  let _infoSig = '';
  function renderInfo() {
    const c = $('content');
    let wrap = $('infoWrap');
    if (!wrap) { c.innerHTML = ''; wrap = document.createElement('div'); wrap.id = 'infoWrap'; wrap.className = 'pane'; c.appendChild(wrap); _infoSig = ''; }
    // Rebuild only when an input actually changed (same dedup the Skills grid uses):
    // this pane used to tear down and rebuild its whole DOM 4x/s, one of the largest
    // steady-state raster costs in the UI. The idle countdown is quantized to seconds
    // in the signature so it still ticks visibly without forcing 4 rebuilds/s.
    try {
      const im = infoMember && typeof infoMember.idleMs === 'number'
        ? Object.assign({}, infoMember, { idleMs: Math.floor(infoMember.idleMs / 1000) })
        : infoMember;
      const sig = JSON.stringify([infoData, playerVp, playerVb, im, infoCam, infoHover, infoHoverHeld]);
      if (sig === _infoSig && wrap.firstChild) return;
      _infoSig = sig;
    } catch (e) { /* unstringifiable input: fall through and rebuild */ }
    const d = infoData;
    const rows = document.createElement('div'); rows.className = 'rows';
    // Grouped, not one long column. A heading is only emitted once something actually
    // lands under it, so a section whose every value is unreadable stays hidden.
    let secPending = null;
    const sec = (name) => { secPending = name; };
    const add = (r) => {
      if (!r) return;
      if (secPending) { rows.appendChild(secRow(secPending)); secPending = null; }
      rows.appendChild(r);
    };
    if (!d || !d.in) {
      rows.appendChild(row('Status', 'not in-world'));
    } else {
      const vp = playerVp || {};
      const u = (k) => (vp[k] || 0) >>> 0;
      if (playerVp) {
        const hp = u('13537'), pr = u('3274'), su = u('8040');   // 13537 = current lifepoints, raw
        const L = (n) => n.toLocaleString();
        // Account status varp 4818: bit0 ironman, bit1 hardcore, bit19 in-GIM, bit25 unranked group,
        // bits21-23 GIM type via enum 5733 {0 Regular, 1 Competitive}; unranked wins over the type.
        const am = u(typeof VP !== 'undefined' ? VP.ACCOUNT_MODE : 4818), iron = am & 1, hc = (am >>> 1) & 1, inGim = (am >>> 19) & 1,
              gimType = (am >>> 21) & 7, gimUnranked = (am >>> 25) & 1;
        let acct = 'Main';
        if (iron) acct = inGim ? (gimUnranked ? 'Group Ironman (Unranked)'
                                              : gimType === 1 ? 'Competitive Group Ironman'
                                                              : 'Group Ironman')
                               : (hc ? 'Hardcore Ironman' : 'Ironman');
        sec('Account');
        add(row('Account', acct));
        // Membership: engine state (PLAYERMEMBER op) for free-vs-member, varbit 50572 for
        // premier. `resolved` is false before the account object exists, when every other
        // field would read as free rather than as unknown - so skip the row entirely then.
        if (infoMember && infoMember.resolved) {
          add(row('Membership', infoMember.tier === 2 ? 'Premier'
                              : infoMember.member ? 'Member' : 'Free'));
          // Idle logout: the budget is additive (5 base +5 member +5 Jagex account, max 15) and
          // enforced server-side. idleMs IS live though - the client stamps when it last
          // reported input to the server, which is what that timer runs on - so show the real
          // remaining time and fall back to the bare budget only when it is unavailable (-1).
          // Cursor movement inside the game window resets it even unfocused; keys only count
          // while the game window is active. NOTE it does not run at all while in combat.
          const idleBudget = Math.floor(infoMember.idleLogoutSeconds / 60) + ' min';
          if (typeof infoMember.idleMs === 'number' && infoMember.idleMs >= 0) {
            const left = Math.max(0, infoMember.idleLogoutSeconds - Math.floor(infoMember.idleMs / 1000));
            const mm = Math.floor(left / 60), ss = left % 60;
            add(row('Idle logout', mm + ':' + (ss < 10 ? '0' : '') + ss + ' left of ' + idleBudget));
          } else {
            add(row('Idle logout', idleBudget));
          }
        }
        // Leagues account identity: varp 12314 = league number (script20117 matches it
        // against db 326.0; 0 = normal character). Points varp is per league (db 326.28
        // var_reference: L1 12426, L2 13521); vb 58389 = league tasks completed
        // (script21081). The XP multiplier is NOT a var: the game prints db 328.4 of
        // the tier row (script20255), so it is derived here as the highest tier whose
        // points cost is reached, using the leagues panel's shared table data.
        {
          const lg = u(typeof VP !== 'undefined' ? VP.LEAGUE : 12314);
          if (lg > 0) {
            const LG_NAMES = { 1: 'Leagues I (Catalyst)', 2: 'Leagues II (Equilibrium)' };
            sec('League');
            add(row('League', LG_NAMES[lg] || ('League ' + lg)));
            const ptsVp = { 1: '12426', 2: '13521' }[lg];
            const pts = (ptsVp && vp[ptsVp] !== undefined) ? u(ptsVp) : null;
            if (pts !== null) add(row('League points', L(pts)));
            const lgRec = (typeof lgData === 'object' && lgData && lgData.leagues)
              ? lgData.leagues.find(x => x.num === lg) : null;
            if (!lgRec && typeof fetchLeagues === 'function') fetchLeagues();   // fills on a later poll
            if (lgRec && pts !== null) {
              let mult = 0;
              for (const t of lgRec.tiers) if (pts >= t.cost && t.xp > mult) mult = t.xp;
              if (mult > 0) add(row('XP multiplier', mult + 'x'));
            }
            const td = playerVb && playerVb['58389'];
            if (typeof td === 'number' && td > 0) add(row('League tasks done', L(td)));
          }
        }
        sec('Vitals');
        // Combat level comes from the player entity section (psec+0x10BC); -1 = unavailable.
        if (typeof d.combat === 'number' && d.combat >= 3)
          add(row('Combat level', String(d.combat)));
        // varp 13537 = current lifepoints, varp 13538 = MAX lifepoints (both raw). The max
        // was located live 2026-08-17 by value-searching the varp map at 1,068/1,500 (only
        // 13538 held 1500) and cross-checked on a second account at 5,600/5,600 (13537 and
        // 13538 both 5600).
        // 13537 is now CONFIRMED in CS2 (build 940, 11 scripts): the game moved current
        // lifepoints off varbit 1668 (varp 659 bits 1-15, hence the old 32767 ceiling) onto
        // this varp, and script16860 lifts the cap to 2^31-1 while varp 12314 > 0, so
        // in-league values above 32000 are legitimate. 13538 stays live-observation only:
        // it is referenced by zero clientscripts, which read max via script2915 instead.
        // Do NOT reintroduce varbit 1668 as HP; leagues reuses it (see LEAGUES2.md).
        // Max shown only when it reads sane, so a missing read never paints "x / 0".
        {
          const mx = playerVp['13538'];
          add(row('Hitpoints', playerVp['13537'] === undefined ? 'unknown'
                 : (typeof mx === 'number' && mx > 0 ? L(hp) + ' / ' + L(mx) : L(hp))));
        }
        add(row('Prayer',     L(Math.floor((pr & 0x7fff) / 10)) + ' / ' + L(((pr >>> 16) & 0x7f) * 10)));
        add(row('Summoning',  L(Math.floor((su & 0x7fff) / 10)) + ' / ' + L(((su >>> 16) & 0x7f) * 10)));
        // Familiar special-move points: varp 1787 raw current, fixed max 60.
        add(row('Spell points', L(u(typeof VP !== 'undefined' ? VP.SPELL_POINTS : 1787)) + ' / 60'));
        add(row('Adrenaline', Math.floor(u('679') / 10) + '%'));
        add(row('Run',        u('463') ? 'on' : 'off'));
        // Server-packet values off the skill block, not vars; weight is a signed i16 whose
        // "missing" sentinel is -100000.
        if (typeof d.energy === 'number' && d.energy >= 0)
          add(row('Run energy', d.energy + '%'));
        if (typeof d.weight === 'number' && d.weight > -32769)
          add(row('Weight', d.weight + ' kg'));
        // The overhead action bar (harvest / search / clue-scan progress): the reader walks the
        // player's head-bar list (psec+0xF08 -> +0x28, fill byte +0x34, HP bar excluded) and
        // reports the fill 0-255; -1 when no action bar is showing.
        if (typeof d.progress === 'number' && d.progress >= 0)
          add(row('Action progress', Math.round(d.progress * 100 / 255) + '%'));
        sec('Progress');
        add(row('Quest points', L(vp['1297'] | 0)));
        // Total penguin points: varbit 4163, hard cap 250.
        const pengVb = typeof VB !== 'undefined' ? VB.PENGUIN_POINTS : 4163;
        if (infoVb && infoVb[pengVb] !== undefined)
          add(row('Penguin points', L(infoVb[pengVb] | 0) + ' / 250'));
        // All-time Dungeoneering floors (varbit 39152, bits 0-15); omitted when unreadable, since 0 would read as "never".
        {
          const fv = playerVb && (playerVb['39152'] !== undefined ? playerVb['39152'] : playerVb[39152]);
          if (typeof fv === 'number' && fv >= 0)
            add(row('Dungeoneering floors', L(fv)));
        }
        sec('Charge pack');
        // Charge pack: varp 5984 raw charge is in 1/3000 units; max tier = bits 19-23 of varp 5982.
        const cv = u('5982'), tb = b => (cv >>> b) & 1;
        const maxRaw = !tb(19) ? 600000000 : !tb(20) ? 750000000 : !tb(21) ? 900000000
                     : !tb(22) ? 1050000000 : !tb(23) ? 1200000000 : 1500000000;
        add(row('Charge pack', L(Math.floor(u('5984') / 3000)) + ' / ' + L(Math.floor(maxRaw / 3000))));
        // Combat drain rate = varp 5991 / 1800 charges/s; drain is 0 without augmented gear.
        const drainPerSec = (u('5991') * 10 / 6) / 3000;
        add(row('Drain rate', drainPerSec.toFixed(2) + '/s'));
        if (drainPerSec > 0) {
          const t = Math.floor((u('5984') / 3000) / drainPerSec);
          const dd = Math.floor(t / 86400), hh = Math.floor(t % 86400 / 3600), mm = Math.floor(t % 3600 / 60), ss = t % 60;
          add(row('Time remaining', dd > 0 ? (dd + 'd ' + hh + 'h') : hh > 0 ? (hh + 'h ' + mm + 'm') : mm > 0 ? (mm + 'm ' + ss + 's') : (ss + 's')));
        } else {
          add(row('Time remaining', 'n/a'));
        }
      }
      sec('Location');
      add(row('Position', d.x + ', ' + d.y));
      if (d.region != null) add(row('Region', d.region + ' (' + (d.region >> 8) + ', ' + (d.region & 0xFF) + ')'));
      if (d.lx != null) add(row('Local tile', d.lx + ', ' + d.ly));
      add(row('Plane', String(d.plane)));
      sec('Activity');
      add(row('Animation', d.anim === -1 ? 'none' : String(d.anim)));
      add(row('Moving', d.moving ? 'yes' : 'no'));
      // Engine-reported render rate (MainData+0x550, the FPS_STATS op's own counter); absent
      // on an older host build.
      if (typeof d.fps === 'number' && d.fps >= 0) add(row('Client FPS', String(d.fps)));
      // Scenery/objects aren't in the live entity list, so they show as none here.
      const it = d.interact;
      add(row('Interacting',
        it ? (it.name || '?') + (it.type === 1 && it.id >= 0 ? ' (' + it.id + ')' : '') : 'none'));
      // Camera varcs: 5115 yaw (0..16284 = full circle), 5114 pitch, 1971 zoom (raw engine units).
      if (infoCam) {
        sec('Camera');
        if (typeof infoCam.yaw === 'number')
          add(row('Yaw', Math.round(((infoCam.yaw % 16284) + 16284) % 16284 / 16284 * 360) + '° (' + infoCam.yaw + ')'));
        if (typeof infoCam.pitch === 'number') add(row('Pitch', String(infoCam.pitch)));
        if (typeof infoCam.zoom === 'number') add(row('Zoom', String(infoCam.zoom)));
      }
      // Whatever the engine hover slot resolves right now, one subrow per decoded field.
      // STICKY: moving the mouse from the NPC to THIS panel clears the engine's hover
      // slot, which used to blank the section the instant you tried to read it. The
      // last real target is therefore held and shown (tagged "last") until a new one
      // replaces it, so hover something, then come read the details at leisure.
      {
        let hv = infoHover;
        const isReal = hv && hv.ok && (hv.kind || hv.name);
        if (isReal) infoHoverHeld = hv;
        let held = false;
        if (!isReal && infoHoverHeld) { hv = infoHoverHeld; held = true; }
        const kind = hv && hv.ok ? (hv.kind || (hv.name ? 'unresolved' : 'no target')) : 'none';
        sec('Mouse hover');
        add(row('Target', kind + (held ? '  (last)' : '')));
        if (hv && hv.ok) {
          if (hv.verb) add(row('· Action', hv.verb));
          if (hv.name) add(row('· Name', hv.name));
          if (hv.id != null && hv.id >= 0) add(row('· Id', String(hv.id)));
          if (hv.uid != null) add(row('· Uid', String(hv.uid)));
          if (hv.x != null) add(row('· Tile', hv.x + ', ' + hv.y + (hv.p != null ? ' · floor ' + hv.p : '')));
          if (hv.slot != null) add(row('· Slot', hv.slot + ' · in ' + hv.iface + ':' + hv.comp));
          else if (hv.kind === 'iface') add(row('· Component', hv.iface + ':' + hv.comp + (hv.comp2 ? ' (' + hv.comp2 + ')' : '')));
        }
      }
    }
    wrap.innerHTML = ''; wrap.appendChild(rows);
  }
