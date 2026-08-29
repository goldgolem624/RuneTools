// RuneToolsX panel: Reputation (Heart of Gielinor, Menaphos, Farming Guild, Mazcab).
// Heart of Gielinor: per-faction reputation varbits plus the bounty-accepted / bounty-target
// selectors. Faction index 1=Seren 2=Sliske 3=Zamorak 4=Zaros.
// IIFE (window exports + registerTab; see the RTX registry in client.html) IIFE (window exports + registerTab; see the RTX registry in client.html); $ used only at RUNTIME.
(function () {

  let repData = null, repFetching = false, repSig = '';
  // Faction cap is 2000 (enum 10999 tops out there); the interface script renders it as 2001.
  const REP_MAX = 2000, REP_TOTAL_MAX = REP_MAX * 4;
  const REP_VB_ACCEPTED = 30930, REP_VB_TARGET = 30931;
  const REP_FACTIONS = [
    { idx: 1, name: 'Seren',   vb: 30864, cls: 'rep-seren',   comp: 'Cywir',     toy: 'A Daystone, to visually change the world around you.', cos: "Helwyr's Claws" },
    { idx: 2, name: 'Sliske',  vb: 30865, cls: 'rep-sliske',  comp: 'Shadow',    toy: 'A mysterious bottle - how quick can you catch the swirling mist?', cos: 'Shadow glaive dagger' },
    { idx: 3, name: 'Zamorak', vb: 30871, cls: 'rep-zamorak', comp: 'Avernic',   toy: 'A seemingly innocent ball of lava.', cos: 'Fury wings' },
    { idx: 4, name: 'Zaros',   vb: 30870, cls: 'rep-zaros',   comp: 'Ilujankan', toy: 'A Nightstone, to visually change the world around you.', cos: 'Dragon Rider blade' },
  ];
  // Reward names from enum 10993, thresholds from enum 10999; identical per faction, with
  // desc(f) filling in its flavour. Cache tiers 1/4/5/6/10/13 are dead entries and omitted.
  const REP_TIERS = [
    { th: 250,  rw: 'Champion summon',          desc: f => 'Summon powerful champions of ' + f.name + ' on the battlefield.' },
    { th: 500,  rw: 'Pocket slot item',         desc: f => 'A pocket slot item protecting from the ' + f.name + ' faction.' },
    { th: 750,  rw: 'Armour upgrading',         desc: f => 'Craft Refined ' + f.name + ' Anima Core armour.' },
    { th: 1000, rw: 'Invention materials',      desc: f => 'Blueprints for ' + f.comp + ' components (Invention workbench).' },
    { th: 1250, rw: 'Faction specific toy',     desc: f => f.toy },
    { th: 1500, rw: 'Cosmetic override',        desc: f => f.cos + ' cosmetic override.' },
    { th: 2000, rw: '50% kill count reduction', desc: f => 'Reduce required ' + f.name + ' kill count by 50%.' },
  ];
  // Enum 10993 tier 14 is the only cross-faction reward, keyed off the combined total.
  const REP_TITLE_TIER = {
    rw: 'Title unlock',
    desc: 'The Venerated title, for reaching maximum reputation with all four factions.',
  };

  // --- Menaphos: rank names enum 12670, thresholds enum 12669 (overall City Reputation) and
  // 12668 (districts). Reputation is a raw VARPLAYER, displayed at 1/10 to match the in-game
  // number; rank = highest tier whose threshold the raw value reached. ---
  const MENA_ACTIVE_VB = 35973, MENA_DIV = 10;
  const MENA_RANK_NAMES = ['Unknown', 'Acquainted', 'Known', 'Familiar', 'Appreciated', 'Liked',
    'Trusted', 'Adored', 'Loved', 'Revered', 'Worshipped'];
  const MENA_CITY_THR = [0, 40000, 66000, 198000, 396000, 660000, 990000, 1419000, 1947000, 2541000, 3300000];      // enum 12669
  const MENA_DISTRICT_THR = [0, 10000, 60000, 180000, 360000, 600000, 900000, 1290000, 1770000, 2310000, 3000000];  // enum 12668
  const MENA_CITY = { name: 'City Reputation', vp: 7002, cls: 'rep-mena-city', thr: MENA_CITY_THR };
  const MENA_DISTRICTS = [
    { idx: 1, name: 'Workers District',  leader: 'Batal',   vp: 6998, cls: 'rep-mena-work',  thr: MENA_DISTRICT_THR },
    { idx: 2, name: 'Imperial District', leader: 'Akhomet', vp: 6999, cls: 'rep-mena-imp',   thr: MENA_DISTRICT_THR },
    { idx: 3, name: 'Merchant District', leader: 'Ehsan',   vp: 7000, cls: 'rep-mena-merch', thr: MENA_DISTRICT_THR },
    { idx: 4, name: 'Port District',     leader: 'Wadud',   vp: 7001, cls: 'rep-mena-port',   thr: MENA_DISTRICT_THR },
  ];
  const MENA_CITY_REWARDS = {
    1: 'Grand Exchange · unlock the Menaphos Grand Exchange & bank',
    2: 'Tumeken Standard cosmetic · city-quest cooldown reduction I',
    3: 'Overall reputation: Familiar',
    4: 'Surface skilling buff (impling/butterfly) · cooldown reduction II',
    5: 'Shifting Tombs feather & gold buff · Tumeken Standard tier 2',
    6: 'City-quest cooldown reduction III',
    7: "Slayer: chance to save a Feather of Ma'at · VIP skilling area",
    8: 'Blessed Tumeken Standard · instant city quests (no cooldown)',
    9: 'Overall reputation: Revered',
    10: 'Magic carpet skilling buff (bonus XP everywhere)',
  };
  const MENA_DISTRICT_REWARDS = {
    1: { 2: 'Bank deposit box', 3: 'Faction head', 4: 'District teleport (PoH tablets)', 5: 'Faction legs',
         6: 'Bank chest', 7: 'Faction body', 8: 'Mining boost: sandstone', 9: "'of the Worker District' title", 10: 'Magic carpet pet' },
    2: { 2: 'Bank deposit box', 3: 'Faction head', 4: 'District teleport (PoH tablets)', 5: 'Faction legs · 2nd Thieving safe',
         6: 'Bank chest', 7: 'Faction body', 8: 'Woodcutting boost: acadia trees', 9: "'of the Imperial District' title", 10: 'Magic carpet pet' },
    3: { 2: 'Bank deposit box', 3: 'Faction head', 4: 'District teleport (PoH tablets)', 5: 'Faction legs',
         6: 'Bank chest', 7: 'Faction body', 8: 'Thieving boost: Menaphite Marketeers', 9: "'of the Merchant District' title", 10: 'Magic carpet pet' },
    4: { 2: 'Bank deposit box', 3: 'Faction head', 4: 'District teleport (PoH tablets)', 5: 'Faction legs',
         6: 'Bank chest', 7: 'Faction body', 8: 'Fishing boost: beltfish/catfish/desert sole', 9: "'of the Port District' title", 10: 'Magic carpet pet' },
  };
  function menaRank(rep, thr) {
    let r = 0;
    for (let i = 0; i < thr.length; i++) { if (rep >= thr[i]) r = i; }
    return r;
  }

  // --- Liberation of Mazcab / Goebie reputation: a single VARBIT out of 5000, displayed raw. ---
  const MAZ_VB = 28733, MAZ_MAX = 5000;
  const MAZ_TIERS = [
    { th: 100,  rw: 'Supply discount',          ds: '10% discount at the Mazcab Emergency Merchants.' },
    { th: 500,  rw: 'Bank access (Canal)',      ds: 'A Goebie banker stationed in the canal area.' },
    { th: 750,  rw: 'Fast travel I',            ds: 'Goebie guides to Nemi Forest and the Mazcab entrance.' },
    { th: 1000, rw: 'Supply discount+',         ds: '25% discount at the Mazcab Emergency Merchants.' },
    { th: 1250, rw: "'The Reputable' title",    ds: "Unlocks the '[Name] the Reputable' title." },
    { th: 1500, rw: 'Bank access (Waterfall)',  ds: 'A Goebie banker stationed in the waterfall area.' },
    { th: 1750, rw: 'House location: Otot',     ds: 'Move your house to Otot (60 Construction) and make Otot tablets.' },
    { th: 2000, rw: 'Fast travel II',           ds: 'Goebie guides to Otot and Kanatah.' },
    { th: 2500, rw: 'Pet Goebie',               ds: 'Unlocks the Goebie spawnling pet.' },
    { th: 3000, rw: "'of the Goebies' title",   ds: "Unlocks the '[Name] of the Goebies' title." },
    { th: 5000, rw: '10% rare drop chance',     ds: '10% increased chance at a rare drop from a raid encounter.' },
  ];

  // --- Farming Guild: thresholds enum 15779, rank names enum 15780. Same struct-based system
  // as Menaphos - raw varplayer displayed at 1/10, so thresholds below are raw. ---
  const FARM_VP = 9019;
  const FARM_RANK_NAMES = ['Beginner Farmer', 'Apprentice Farmer', 'Practiced Farmer', 'Competent Farmer',
    'Expert Farmer', 'Master Farmer', 'Senior Master Farmer', 'Grandmaster Farmer'];
  const FARM_THR = [0, 40000, 100000, 250000, 750000, 2250000, 11000000, 25000000];   // raw (displayed x10)
  const FARM_REWARDS = {
    1: 'Increased chance to get unchecked animals.',
    2: 'Hard farming requests unlocked · Limpwurt root yield doubled.',
    3: 'Build the Remote Farming Device · shiny animals sell for 10% more beans.',
    4: 'Animals have less risk of becoming diseased · small animal buyers buy twice as many.',
    5: 'Permanent bloodwood tree at the Guild patch · medium animal buyers buy twice as many.',
    6: '+5% chance of a shiny animal being born · large animal buyers buy twice as many.',
    7: "All animals sell for 10% more beans · unlocks the '[Name] the Very Good Farmer' title.",
  };

  // Shared "Next unlock" line + expandable tier list, used by every reputation system.
  // tiers = [{th (formatted string), done, title, desc}]; word = 'rewards' | 'ranks'.
  function repRewardBlock(row, nextHtml, tiers, word) {
    const rew = document.createElement('div'); rew.className = 'rep-rewards';
    const nextLine = document.createElement('div'); nextLine.className = 'rep-next';
    nextLine.innerHTML = nextHtml;
    rew.appendChild(nextLine);
    const toggle = document.createElement('span'); toggle.className = 'rep-toggle';
    toggle.textContent = 'Show all ' + word + ' ▾';
    const list = document.createElement('div'); list.className = 'rep-list';
    for (const t of tiers) {
      const el = document.createElement('div'); el.className = 'rep-t' + (t.done ? ' done' : '');
      el.innerHTML =
        '<div class="rep-t-mk">' + (t.done ? '✓' : '○') + '</div>' +
        '<div class="rep-t-th">' + t.th + '</div>' +
        '<div class="rep-t-body"><div class="rep-t-title">' + t.title + '</div>' +
        (t.desc ? '<div class="rep-t-desc">' + t.desc + '</div>' : '') + '</div>';
      list.appendChild(el);
    }
    toggle.addEventListener('click', () => {
      const open = list.classList.toggle('open');
      toggle.textContent = open ? ('Hide ' + word + ' ▴') : ('Show all ' + word + ' ▾');
    });
    rew.appendChild(toggle); rew.appendChild(list);
    row.appendChild(rew);
  }

  async function fetchReputation() {
    if (!bridge() || repFetching) return;
    repFetching = true;
    try {
      const ids = REP_FACTIONS.map(f => f.vb).concat([REP_VB_ACCEPTED, REP_VB_TARGET, MENA_ACTIVE_VB, MAZ_VB]);
      const vb = await readVarbitValues(ids);
      const vps = [MENA_CITY.vp].concat(MENA_DISTRICTS.map(d => d.vp)).concat([FARM_VP]);
      let vp = {};
      try { vp = JSON.parse(await rtxData.raw('state.varps', vps.join(','))); } catch (e) {}
      repData = {
        rep: REP_FACTIONS.map(f => ({ name: f.name, idx: f.idx, cls: f.cls, val: vb[f.vb] | 0 })),
        accepted: vb[REP_VB_ACCEPTED] | 0,
        target: vb[REP_VB_TARGET] | 0,
        menaActive: vb[MENA_ACTIVE_VB] | 0,
        menaCity: vp[MENA_CITY.vp] | 0,
        mena: MENA_DISTRICTS.map(d => ({ idx: d.idx, name: d.name, leader: d.leader, cls: d.cls, val: vp[d.vp] | 0 })),
        maz: vb[MAZ_VB] | 0,
        farm: vp[FARM_VP] | 0,
      };
    } finally {
      repFetching = false;
    }
    paneRun('reputation', renderReputation);
  }

  function renderReputation() {
    const c = $('content');
    let wrap = $('repWrap');
    if (!wrap) {
      c.innerHTML = '';
      wrap = document.createElement('div'); wrap.id = 'repWrap'; wrap.className = 'pk-wrap';
      c.appendChild(wrap); repSig = '';
    }
    const d = repData;
    if (!d) { wrap.innerHTML = '<div class="stor-empty">Reading...</div>'; repSig = ''; return; }
    const sig = JSON.stringify([d.rep.map(r => r.val), d.accepted, d.target,
                                d.menaCity, d.mena.map(m => m.val), d.menaActive, d.maz, d.farm]);
    if (sig === repSig) return;
    repSig = sig;
    wrap.innerHTML = '';

    const head = document.createElement('div'); head.className = 'rep-head';
    head.innerHTML = '<span class="rep-title">Heart of Gielinor</span>' +
                     '<span class="rep-sub">Faction reputation</span>';
    wrap.appendChild(head);

    const repTotal = d.rep.reduce((sum, x) => sum + x.val, 0);
    for (const r of d.rep) {
      const row = document.createElement('div'); row.className = 'rep-row';
      const h = document.createElement('div'); h.className = 'rep-row-h';
      let tags = '';
      if (d.accepted === r.idx) { tags += '<span class="rep-tag rep-accepted">Bounty Accepted</span>'; }
      if (d.target === r.idx) { tags += '<span class="rep-tag rep-target">Bounty Target</span>'; }
      h.innerHTML = '<span class="rep-nm">' + r.name + '</span>' + tags +
        '<b class="rep-val">' + r.val.toLocaleString() + ' / ' + REP_MAX.toLocaleString() + '</b>';
      const bar = document.createElement('div'); bar.className = 'rep-bar';
      const fill = document.createElement('div'); fill.className = 'rep-fill ' + r.cls;
      fill.style.width = Math.max(0, Math.min(100, r.val * 100 / REP_MAX)) + '%';
      bar.appendChild(fill);
      row.appendChild(h); row.appendChild(bar);

      const f = REP_FACTIONS.find(x => x.idx === r.idx) || {};
      const next = REP_TIERS.find(t => t.th > r.val);
      // Past a faction's own tiers only the cross-faction Title remains (combined total).
      const nextHtml = next
        ? 'Next: <b>' + next.th.toLocaleString() + '</b> · ' + next.rw +
          ' (' + (next.th - r.val).toLocaleString() + ' to go)'
        : repTotal >= REP_TOTAL_MAX
          ? 'All rewards unlocked.'
          : 'Faction maxed. <b>' + REP_TITLE_TIER.rw + '</b> needs ' +
            (REP_TOTAL_MAX - repTotal).toLocaleString() + ' more across all four factions.';
      const tiers = REP_TIERS.map(t => ({
        th: t.th.toLocaleString(), done: r.val >= t.th, title: t.rw, desc: t.desc(f),
      }));
      tiers.push({
        th: REP_TOTAL_MAX.toLocaleString(), done: repTotal >= REP_TOTAL_MAX,
        title: REP_TITLE_TIER.rw, desc: REP_TITLE_TIER.desc,
      });
      repRewardBlock(row, nextHtml, tiers, 'rewards');
      wrap.appendChild(row);
    }

    if (!d.accepted && !d.target) {
      const note = document.createElement('div'); note.className = 'rep-note';
      note.textContent = 'No active bounty. Accept one from a faction commander in the Heart of Gielinor.';
      wrap.appendChild(note);
    }

    const mhead = document.createElement('div'); mhead.className = 'rep-head rep-head2';
    mhead.innerHTML = '<span class="rep-title">Menaphos</span>' +
                      '<span class="rep-sub">City &amp; district reputation</span>';
    wrap.appendChild(mhead);

    const disp = v => Math.round(v / MENA_DIV).toLocaleString();
    const menaRow = (name, sub, val, thr, rewards, active, rankNames, fillCls) => {
      rankNames = rankNames || MENA_RANK_NAMES;
      const rank = menaRank(val, thr);
      const max = thr[thr.length - 1];
      const nextThr = rank < thr.length - 1 ? thr[rank + 1] : null;
      const row = document.createElement('div'); row.className = 'rep-row';
      const h = document.createElement('div'); h.className = 'rep-row-h';
      h.innerHTML = '<span class="rep-nm">' + name + '</span>' +
        (sub ? '<span class="rep-sub2">' + sub + '</span>' : '') +
        (active ? '<span class="rep-tag rep-active">Active</span>' : '') +
        '<b class="rep-val">' + rankNames[rank] + '</b>';
      const bar = document.createElement('div'); bar.className = 'rep-bar';
      const fill = document.createElement('div'); fill.className = 'rep-fill ' + (fillCls || 'rep-mena');
      fill.style.width = Math.max(0, Math.min(100, val * 100 / max)) + '%';
      bar.appendChild(fill);
      row.appendChild(h); row.appendChild(bar);

      const nextHtml = nextThr !== null
        ? 'Next: <b>' + rankNames[rank + 1] + '</b> at ' + disp(nextThr) +
          ' (' + disp(nextThr - val) + ' to go) · ' + disp(val) + ' / ' + disp(max)
        : 'Max rank (' + rankNames[rank] + ') · ' + disp(val) + ' / ' + disp(max);
      const tiers = [];
      for (let i = 1; i < thr.length; i++) {
        tiers.push({ th: disp(thr[i]), done: val >= thr[i], title: rankNames[i], desc: rewards[i] || '' });
      }
      repRewardBlock(row, nextHtml, tiers, 'ranks');
      wrap.appendChild(row);
    };

    menaRow(MENA_CITY.name, '', d.menaCity, MENA_CITY.thr, MENA_CITY_REWARDS, false);
    for (const m of d.mena) {
      menaRow(m.name, m.leader, m.val, MENA_DISTRICT_THR, MENA_DISTRICT_REWARDS[m.idx], d.menaActive === m.idx);
    }

    const fhead = document.createElement('div'); fhead.className = 'rep-head rep-head2';
    fhead.innerHTML = '<span class="rep-title">Farming Guild</span>' +
                      '<span class="rep-sub">Guild reputation</span>';
    wrap.appendChild(fhead);
    menaRow('Farming Guild', '', d.farm, FARM_THR, FARM_REWARDS, false, FARM_RANK_NAMES, 'rep-farm');

    const zhead = document.createElement('div'); zhead.className = 'rep-head rep-head2';
    zhead.innerHTML = '<span class="rep-title">Liberation of Mazcab</span>' +
                      '<span class="rep-sub">Goebie reputation</span>';
    wrap.appendChild(zhead);

    const mv = d.maz;
    const zrow = document.createElement('div'); zrow.className = 'rep-row';
    const zh = document.createElement('div'); zh.className = 'rep-row-h';
    zh.innerHTML = '<span class="rep-nm">Goebie Reputation</span>' +
      '<b class="rep-val">' + Math.min(MAZ_MAX, mv).toLocaleString() + ' / ' + MAZ_MAX.toLocaleString() + '</b>';
    const zbar = document.createElement('div'); zbar.className = 'rep-bar';
    const zfill = document.createElement('div'); zfill.className = 'rep-fill rep-mazcab';
    zfill.style.width = Math.max(0, Math.min(100, mv * 100 / MAZ_MAX)) + '%';
    zbar.appendChild(zfill);
    zrow.appendChild(zh); zrow.appendChild(zbar);

    const znext = MAZ_TIERS.find(t => t.th > mv);
    const znextHtml = znext
      ? 'Next: <b>' + znext.th.toLocaleString() + '</b> · ' + znext.rw +
        ' (' + (znext.th - mv).toLocaleString() + ' to go)'
      : 'All rewards unlocked.';
    repRewardBlock(zrow, znextHtml, MAZ_TIERS.map(t => ({
      th: t.th.toLocaleString(), done: mv >= t.th, title: t.rw, desc: t.ds,
    })), 'rewards');
    wrap.appendChild(zrow);
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { fetchReputation });
registerTab({ id: 'reputation', render: renderReputation, open: function () { repSig = ''; fetchReputation(); } });
})();
