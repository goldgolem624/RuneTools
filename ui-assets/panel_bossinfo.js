// RuneToolsX panel: Boss Info (daily encounter rotations + minigame spotlight).
// Ground truth: clientscript-11074 + helpers 19823/19825 (Araxxi), 3293/14605 + enum 7211
// (Vorago), 19847 + enum 7655 (Barrows: Rise of the Six), 6747 + enum 10016 (spotlight).
// Base rotations are pure DATE_RUNEDAY() math and need no game read. With a client attached
// the PERSONAL rotation-override varbits also apply (War's Retreat unlock): 57662 Araxxi /
// 57663 Barrows / 57664 Vorago (via enum 7211 = value-1), each only when varp 12121 >= 3
// (Vorago >= 4) and the varbit is nonzero.
// Spotlight: varps 5420-23 = enum-10016 indices (current + next three), varp 5419 = days left;
// the rotation steps every 3 days and the active minigame pays thaler at 500%.
// Spliced inline into client.html; bare classic script sharing one global scope. $ used only at RUNTIME.

  let biSig = '';
  let biVb = null, biVp = null, biFetching = false, biFetchAt = 0;
  const BI_VB_IDS = '57662,57663,57664';
  const BI_VP_IDS = '12121,5419,5420,5421,5422,5423';
  let biSpotEnum = null;                // enum 10016: index -> activity struct id
  const biSpotNames = {};               // struct id -> minigame name (param 1266)

  // DATE_RUNEDAY(): the game's day counter = (UTC days since the Unix epoch) + 15.
  // Daily reset is 00:00 UTC.
  function biRuneDay() { return Math.floor(Date.now() / 86400000) + 15; }

  const biV = k => ((biVb && biVb[String(k)]) | 0);
  const biP = k => ((biVp && biVp[String(k)]) | 0);
  // script19823/19847 gate: varp 12121 >= 3; script3293 (Vorago): >= 4.
  const biOverridesOn = min => biP(12121) >= min;

  function biAraxxiPath() {             // script19823 -> script19825 (12-day cycle)
    if (biOverridesOn(3) && biV(57662) > 0)
      return { txt: ['', 'Spider Minions', 'Acid Pool', 'Darkness'][biV(57662)] || '?', ovr: true };
    const d = biRuneDay() % 12;
    return { txt: d <= 3 ? 'Spider Minions' : (d <= 7 ? 'Acid Pool' : 'Darkness'), ovr: false };
  }
  const BI_VORAGO = ['Ceiling Collapse', 'Scopulus', 'Vitalis', 'Green Bomb', 'Team Split', 'The End'];
  function biVoragoRotation() {         // script3293 -> script14605 (weekly, 6-week cycle)
    if (biOverridesOn(4) && biV(57664) > 0)
      return { txt: BI_VORAGO[biV(57664) - 1] || '?', ovr: true };   // enum 7211: 1..6 -> 0..5
    const r = Math.floor((biRuneDay() + 21) / 7) % 6;
    return { txt: BI_VORAGO[r], ovr: false };
  }
  // Barrows: Rise of the Six brother sides. enum 7655[runeday % 20] = bitmask, bit i set -> WEST,
  // clear -> EAST; script19847's override (vb 57663) returns a value in the same mask space. The
  // 20-day mask table is read from the cache at runtime (enumInfo 7655); the brother bit order is
  // CS2-only.
  const BI_BROTHERS = ['Ahrim', 'Dharok', 'Guthan', 'Karil', 'Torag', 'Verac'];
  let biRotsEnum = null;
  async function biLoadRotsEnum() {
    if (biRotsEnum || !bridge() || !bridge().enumInfo) return;
    try {
      const e = JSON.parse(await bridge().enumInfo(7655) || 'null');
      if (e && Object.keys(e).length) biRotsEnum = e;
    } catch (e) {}
  }
  function biBarrowsSides() {
    let mask = null, ovr = false;
    if (biOverridesOn(3) && biV(57663) > 0) { mask = biV(57663); ovr = true; }
    else if (biRotsEnum) mask = biRotsEnum[String(biRuneDay() % 20)] | 0;
    if (mask == null) return null;                 // enum not readable yet
    const west = [], east = [];
    BI_BROTHERS.forEach((nm, i) => ((mask >> i) & 1 ? west : east).push(nm));
    return { west: west.join(', ') || 'none', east: east.join(', ') || 'none', ovr };
  }

  // Spotlight slot -> minigame name: enum 10016 index -> struct, name = param 1266. Both lookups
  // are cache-static, so memoise forever once resolved.
  async function biSpotName(idx) {
    if (!biSpotEnum) {
      try { biSpotEnum = JSON.parse(await bridge().enumInfo(10016) || 'null'); } catch (e) {}
      if (!biSpotEnum || !Object.keys(biSpotEnum).length) { biSpotEnum = null; return ''; }
    }
    const sid = biSpotEnum[String(idx)] | 0;
    if (sid <= 0) return '';
    if (biSpotNames[sid] === undefined) {
      try {
        const sp = JSON.parse(await bridge().structParams(sid) || 'null');
        const nm = sp && sp.strs && sp.strs['1266'];
        if (nm) biSpotNames[sid] = nm;
      } catch (e) {}
    }
    return biSpotNames[sid] || '';
  }

  async function fetchBossInfo() {
    if (!paneVisible('bossinfo')) return;
    await biLoadRotsEnum();
    if (bridge() && bridge().varbits && !biFetching) {
      const t = Date.now();
      if (t - biFetchAt >= 2000) {
        biFetchAt = t; biFetching = true;
        try {
          const vb = JSON.parse(await bridge().varbits(myPid(), BI_VB_IDS) || 'null');
          if (vb && typeof vb === 'object' && Object.keys(vb).length) biVb = vb;
          const vp = JSON.parse(await bridge().varps(myPid(), BI_VP_IDS) || 'null');
          if (vp && typeof vp === 'object' && Object.keys(vp).length) biVp = vp;
        } catch (e) { /* keep previous; date math still renders */ }
        biFetching = false;
      }
    }
    paneRun('bossinfo', renderBossInfo);
  }

  function renderBossInfo() {
    const c = $('content');
    let wrap = $('biWrap');
    if (!wrap) {
      c.innerHTML = '';
      wrap = document.createElement('div'); wrap.id = 'biWrap'; wrap.className = 'pk-wrap';
      c.appendChild(wrap); biSig = '';
    }
    const arax = biAraxxiPath(), vor = biVoragoRotation(), rots = biBarrowsSides();
    // varps read {} while out of world; 5421-23 can never all be 0 together in real data.
    const spotOk = biVp && (biP(5419) > 0 || biP(5420) > 0 || biP(5421) > 0 || biP(5422) > 0 || biP(5423) > 0);
    const sig = JSON.stringify([biRuneDay(), arax, vor, rots, spotOk,
                                biP(5419), biP(5420), biP(5421), biP(5422), biP(5423)]);
    if (sig === biSig) return;
    biSig = sig;
    wrap.innerHTML = '';

    const section = (title) => {
      const h = document.createElement('div'); h.className = 'bi-sec'; h.textContent = title;
      wrap.appendChild(h);
    };
    const row = (name, lines) => {
      const r = document.createElement('div'); r.className = 'bi-row';
      const nm = document.createElement('div'); nm.className = 'bi-nm'; nm.textContent = name;
      r.appendChild(nm);
      for (const [label, value, cls] of lines) {
        const l = document.createElement('div'); l.className = 'bi-line';
        l.innerHTML = '<span class="bi-lbl">' + label + '</span><span class="bi-val' +
          (cls ? ' ' + cls : '') + '">' + value + '</span>';
        r.appendChild(l);
      }
      wrap.appendChild(r);
      return r;
    };
    const ovrTag = o => (o ? ' (personal override)' : '');
    const d = biRuneDay();

    section('Daily rotations');
    row('Araxxi', [['Blocked path', arax.txt + ovrTag(arax.ovr), 'bi-hi'],
                   ['Changes in', arax.ovr ? 'while overridden: never' : (4 - d % 4) + 'd', '']]);
    row('Vorago', [['Rotation (weekly)', vor.txt + ovrTag(vor.ovr), 'bi-hi'],
                   ['Changes in', vor.ovr ? 'while overridden: never' : (7 - ((d + 21) % 7)) + 'd', '']]);
    if (rots) row('Barrows: Rise of the Six', [['West', rots.west + ovrTag(rots.ovr), 'bi-hi'],
                                               ['East', rots.east, '']]);
    else row('Barrows: Rise of the Six', [['Sides', 'reading rotation table from the cache...', '']]);

    section('Minigame Spotlight');
    if (!spotOk) {
      row('Spotlight', [['Status', 'attach a logged-in client to read the rotation', '']]);
    } else {
      // varp 5420 = current (varp 5419 days left, 500% thaler), 5421 next (in 5419d), 5422 (+3d),
      // 5423 (+6d); step = 3 days, switching at 00:00 UTC.
      const dayName = days => {
        const d = new Date(Date.now() + days * 86400000);
        return ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'][d.getUTCDay()] + ' ' + d.getUTCDate() + ' ' +
               ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'][d.getUTCMonth()];
      };
      const left = biP(5419);
      const slots = [
        [5420, [['Spotlight active', '500% thaler', 'bi-hi'],
                ['Ends', 'in ' + left + 'd (' + dayName(left) + ', 00:00 UTC)', '']]],
        [5421, [['Starts', 'in ' + left + 'd · ' + dayName(left), '']]],
        [5422, [['Starts', 'in ' + (left + 3) + 'd · ' + dayName(left + 3), '']]],
        [5423, [['Starts', 'in ' + (left + 6) + 'd · ' + dayName(left + 6), '']]]];
      for (const [vpid, lines] of slots) {
        const r = row('Resolving...', lines);
        (async () => {
          const nm = await biSpotName(biP(vpid));
          const el = r.querySelector('.bi-nm');
          if (el) el.textContent = nm || ('Spotlight slot #' + biP(vpid));
        })();
      }
    }
  }
