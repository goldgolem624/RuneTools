// RuneToolsX panel: Dailies & Weeklies (reset-capped activity tracker). Reads the activity
// varbits live and shows progress toward each daily / weekly / monthly cap with live countdowns
// to the resets (daily 00:00 UTC, weekly Wednesday 00:00 UTC, monthly 1st 00:00 UTC).
// Every rule below ports the game's own D&D tracker CS2: the list is enums 6452/8014-8017
// (idx -> activity struct, js5-22), each struct's param 1268 is a timer key, and script3723
// dispatches the key to a per-activity handler returning [state, minutes] (states per
// script9135: 0 available, 1/7 resets in, 2 begins in, 3 play again in, 4 ends in, 5 lobby
// opens, 6 no upcoming stars). Reset clocks: script9170 = vb20736 (daily), script10889 =
// vb20743*1440+vb20736 (weekly), script9169 = vb20737*1440+vb20736 (monthly); the panel derives
// the same countdowns from the wall clock (UTC).
// Build-once DOM: countdown/timer pills update per render pass with idempotent writes; value
// rows repaint only when a varbit actually changed (dwSig).
// Spliced inline into client.html; IIFE (window exports + registerTab; see the RTX registry in client.html).
(function () {

  let dwData = null, dwVp = null, dwFetching = false, dwFetchAt = 0, dwSig = '';
  // The VoS varbits (25158/25159 pair + 26416 hour stamp) only read LIVE while the player is in
  // Prifddinas; they freeze at the last in-Priff values on leaving, so the hour stamp (0-23 only)
  // re-matches the wall-clock hour a day later and a stale pair would look "live". Gate the live
  // trust on the player being IN the Priff region box (region = tile>>6; X 32-35, Y 51-54),
  // refreshed each dailies tick. Matches the C++ vos_report_loop gate.
  let dwInPriff = false;
  function dwRegionInPriff(p) {
    if (!p) return false;
    const rx = (p.x | 0) >> 6, ry = (p.y | 0) >> 6;
    return rx >= 32 && rx <= 35 && ry >= 51 && ry <= 54;
  }
  // Availability notifications: per-event bell toggles (persisted) fire an in-game card + native
  // Windows notification on the edge where an armed event becomes available. dwPrev is the
  // previous availability snapshot; the first fetch only sets the baseline.
  let dwNotify = {};
  try { dwNotify = JSON.parse(localStorage.getItem('rtxDwNotify') || '{}') || {}; } catch (e) {}
  function dwNotifySave() { try { prefSet('rtxDwNotify', JSON.stringify(dwNotify)); } catch (e) {} }   // durable pref
  function dwNotifyAny() { for (const k in dwNotify) if (dwNotify[k]) return true; return false; }
  let dwPrev = null;

  // Varbit map (CS2 ground truth; handler script in parentheses):
  //  penguins: 4164 spied /10, 4165 == 1 spying unlocked (Larry/Chuck, Ardougne Zoo),
  //    4163 total points, hard cap 250
  //  sinkholes (9150): 17933 played /2, 20747 hourly cycle (< 15 = open, collapses in
  //    15 - v, else next begins in 60 - v)
  //  big chinchompa (9144): 4882 catch count (cap 2/day), 20742 hourly cycle (< 20 =
  //    open, ends in 20 - v, else next in 60 - v)
  //  fish flingers (9155): 20740 played today, 20744 (<= 5 = competition ends in v,
  //    else lobby opens in v - 5)
  //  runesphere (6416): 16526 == 1 = siphoned today
  //  guthixian cache (2026-03 rework, read via varp 4846):
  //    25543 = points this cache, cap 100 PER INSTANCE (script10706's "of 200 today"
  //    text is stale); 25551 = memories converted;
  //    25552 = automatons subdued. Rifts are player-driven (800 deposited memories
  //    open one) -- no spawn schedule exists. 25533/25534 (tracker handler 10705)
  //    25533/25534 (tracker handler 10705) are also polled.
  //  goebie supply runs (13784): 28796 == 10 = done today; 12h wall-clock windows
  //    (720 - DATE_MINUTES%720 >= 700 = run open, ends in rem - 700, else next in rem)
  //  shooting star (9166): 20739 window active, 20750 minutes (0+active = star up now,
  //    0+inactive = no upcoming stars)
  //  evil tree (9158): 20745 == 1 = tree up now, else next in 20746 minutes (no cap)
  //  demon flashmob (11604): next mob in 28370 minutes
  //  familiarisation (10539/10538): 39271 > 0 done weekly, 20738 == 1 session active,
  //    20749 session minutes (ends in / begins in)
  //  meg (11375/9146): 17439 == 1 sent this week, 17445 == 0 never visited
  //  nomad's bounty (12783/12782): 33780 == 1 looted this week
  //  thalmund (4298/3872): gate Kili Row = quest vb 53496 >= 35; Wednesdays only
  //    (DATE_RUNEDAY-7847 mod 7); stock pairs purchased/stock 55086/87 55088/89 55090/91
  //    55092/93 (any stock > 0 viewed, all purchased >= stock bought out)
  //  herby werby (7226/7225): 44349 == 1 done weekly, 44351 spirit points /100
  //  jadinkos (7363/7362): 16096 regular state (0 catching / 1 XP+clothing / 2 clothing /
  //    3 claimed) species 16101-16110; 16127 god state (0..4) species 16111-16113
  //  tears of guthix (9152): 26630 == 1 done weekly
  //  wisps of the grove (12168): 30284 == 1 done weekly
  //  agoroth (9088/9089/9091): 22285 kills, weekly target 2 members / 1 free-to-play
  //  rush of blood (10606/10607): 25048 != 0 done weekly
  //  skeletal horror (11548/11549): 26628 done weekly, gate vb 9902 >= 10 (Fur 'n Seek)
  //  champion's challenge (11551/11552): VARPS 5441 == 5442 = done weekly
  //  shattered worlds (14930): 35817/35818/35819 all == 1 = weekly challenges done
  //  circus (9140/9141): 5479/5480/5481 agility/magic/ranged, all == 1 = done
  //  troll invasion (9153/9154): 20741 done monthly
  //  effigy incubator (4720): 15893 == 1 powered this month
  //  giant oyster (11959): DONE = 30084 >= 2 && 30087 == 30 && 30088 == 30 (two feed
  //    counters /30); 30084 == 1 available to feed; gate Beneath Cursed Tides = vb 30071 >= 200
  //  god statues (9163): built = 17687,60099,17689,17690,24942 each > 0; prayed =
  //    17691+60100+17693+17694+24943
  //  daily challenges (18249/16319/16442): five slots, stride 4 from 16574 --
  //    per slot: category (16574), index (16575), progress (16576); challenge struct =
  //    enum 17112[category] -> sub-enum[index]; struct params 1266 name (+4940 suffix,
  //    script17039), 1273 description, 2235 target, 1271 icon sprite
  const DW_IDS = '16574,16575,16576,16578,16579,16580,16582,16583,16584,' +
                 '16586,16587,16588,16590,16591,16592,' +
                 '25543,25548,25533,25534,25551,25552,52328,' +
                 '4164,4165,' + (typeof VB !== 'undefined' ? VB.PENGUIN_POINTS : 4163) + ',4882,20742,5479,5480,5481,15893,' +
                 '30084,30087,30088,30071,' +
                 '17687,60099,17689,17690,24942,17691,60100,17693,17694,24943,' +
                 '20739,20750,17933,20747,20745,20746,28370,20740,20744,16526,28796,' +
                 '39271,20738,20749,17439,17445,33780,44349,44351,' +
                 '16096,16101,16102,16103,16104,16105,16106,16107,16108,16109,16110,' +
                 '16127,16111,16112,16113,' +
                 '53496,55086,55087,55088,55089,55090,55091,55092,55093,' +
                 '26630,30284,22285,25048,26628,9902,35817,35818,35819,20741,' +
                 '4662,4649,4673,4693,4694,4695,4696,' +   // Fish Flingers economy (script6263)
                 '25158,25159,26416';   // Voice of Seren: clan pair + hour stamp (varp 4783, Priff-only transmit)

  // Fish Flingers tackle-box upgrade costs, from the cache (enum 5886 tokens / 5887 medals).
  let dwFFCosts = null;
  async function dwLoadFFCosts() {
    if (dwFFCosts || !bridge().enumInfo) return;
    try {
      const a = await rtxData.call('cache.enumInfo', 5886);
      const b = await rtxData.call('cache.enumInfo', 5887);
      if (a && b && Object.keys(a).length && Object.keys(b).length) dwFFCosts = [a, b];
    } catch (e) {}
  }
  async function fetchDailies(force, bg) {
    if (!bridge() || !bridge().varbits || dwFetching) return;
    // bg = armed-notification background poll while the tab is closed: the activity varbits are
    // minute-granular, so 10s is plenty there.
    const t = Date.now(); if (!force && t - dwFetchAt < (bg ? 10000 : 2000)) return; dwFetchAt = t;
    dwFetching = true;
    try {
      const vb = await rtxData.call('state.varbitsCsv', DW_IDS);
      // Refresh the in-Priff gate BEFORE dwVosMaybeReport so the tab-report path never posts (and the
      // panel never labels 'live') a stale out-of-Priff varbit read.
      try { dwInPriff = dwRegionInPriff(await scanPlayerTile()); } catch (e) { dwInPriff = false; }
      if (vb && typeof vb === 'object') { dwData = vb; dwCheckNotify(); dwVosMaybeReport(); }
      await dwLoadFFCosts();
      // Varp-based rows: Champion's Challenge (CS2 script11552: varp 5441 == 5442 = done) + the
      // Menaphos journal collections (CS2 script13412: found-bits packed into varps 6989/6990).
      if (bridge().varps) {
        {
          const vp = await rtxData.call('state.varps', '5441,5442,6989,6990,3079,6601');
          if (vp && typeof vp === 'object') dwVp = vp;
        }
      }
    } catch (e) { /* keep previous */ }
    dwFetching = false;
    paneRun('dailies', renderDailies);
  }

  // Next reset instants (all 00:00 UTC): daily = tomorrow, weekly = next Wednesday, monthly = the
  // 1st of next month.
  function dwNextResets() {
    const now = new Date();
    const y = now.getUTCFullYear(), mo = now.getUTCMonth(), d = now.getUTCDate();
    let ahead = (3 - now.getUTCDay() + 7) % 7;   // Wednesday = 3
    if (!ahead) ahead = 7;
    return { now: now.getTime(),
             daily: Date.UTC(y, mo, d + 1),
             weekly: Date.UTC(y, mo, d + ahead),
             monthly: Date.UTC(y, mo + 1, 1) };
  }
  function dwFmt(ms) {   // "22d 10h 50m 25s", dropping leading zero units
    let s = Math.max(0, Math.floor(ms / 1000));
    const dd = Math.floor(s / 86400); s -= dd * 86400;
    const hh = Math.floor(s / 3600), mm = Math.floor(s % 3600 / 60), ss = s % 60;
    const p2 = n => (n < 10 ? '0' : '') + n;
    if (dd > 0) return dd + 'd ' + hh + 'h ' + p2(mm) + 'm ' + p2(ss) + 's';
    if (hh > 0) return hh + 'h ' + p2(mm) + 'm ' + p2(ss) + 's';
    if (mm > 0) return mm + 'm ' + p2(ss) + 's';
    return ss + 's';
  }
  function dwM2S(m) {    // minutes -> "1h 05m" / "42m" (activity varbits are minute-granular)
    const h = Math.floor(m / 60), mm = m % 60;
    return h > 0 ? h + 'h ' + (mm < 10 ? '0' : '') + mm + 'm' : mm + 'm';
  }

  // Bell toggle: arm/disarm the availability notification for one event key.
  function dwBell(key) {
    const b = document.createElement('button'); b.className = 'dw-bell' + (dwNotify[key] ? ' on' : '');
    b.dataset.tip = 'Notify when available';
    b.innerHTML = '<svg viewBox="0 0 24 24" width="12" height="12" fill="none" stroke="currentColor" stroke-width="2"><path d="M18 16v-5a6 6 0 1 0-12 0v5l-1.5 2h15z"/><path d="M10 20a2 2 0 0 0 4 0"/></svg>';
    b.addEventListener('click', () => {
      dwNotify[key] = dwNotify[key] ? 0 : 1;
      dwNotifySave();
      b.classList.toggle('on', !!dwNotify[key]);
    });
    return b;
  }
  function dwRow(id, name, sub, bellKey) {
    const r = document.createElement('div'); r.className = 'dw-row';
    const nm = document.createElement('div'); nm.className = 'dw-nm';
    const t = document.createElement('div'); t.textContent = name; nm.appendChild(t);
    if (sub) { const sb = document.createElement('div'); sb.className = 'dw-sub'; sb.id = 'dws-' + id; sb.textContent = sub; nm.appendChild(sb); }
    r.appendChild(nm);
    if (bellKey) r.appendChild(dwBell(bellKey));
    const bar = document.createElement('div'); bar.className = 'dw-bar';
    const fill = document.createElement('div'); fill.className = 'dw-fill'; fill.id = 'dwb-' + id;
    bar.appendChild(fill); r.appendChild(bar);
    const val = document.createElement('div'); val.className = 'dw-val'; val.id = 'dwv-' + id;
    r.appendChild(val);
    return r;
  }
  function dwPillRow(id, name, sub, bellKey) {
    const r = document.createElement('div'); r.className = 'dw-row';
    const nm = document.createElement('div'); nm.className = 'dw-nm';
    const t = document.createElement('div'); t.textContent = name; nm.appendChild(t);
    if (sub) { const sb = document.createElement('div'); sb.className = 'dw-sub'; sb.id = 'dws-' + id; sb.textContent = sub; nm.appendChild(sb); }
    r.appendChild(nm);
    if (bellKey) r.appendChild(dwBell(bellKey));
    const p = document.createElement('span'); p.className = 'dw-pill'; p.id = 'dwp-' + id;
    r.appendChild(p);
    return r;
  }

  // Edge-triggered availability notifications (fired from fetchDailies, so armed bells work with
  // the tab closed via the background poll). Done-states suppress events the player can no longer
  // benefit from today/this week.
  // Availability per event key from a tracker-varbit map (dwData-shaped). Shared by the
  // notification bells and the plugin SDK's state.dailies.
  function dwAvail(vb) {
    const v = k => (vb[k] | 0);
    const now = new Date();
    const rem = 720 - ((now.getUTCHours() * 60 + now.getUTCMinutes()) % 720);
    return {
      star:   v('20739') === 1,
      etree:  v('20745') === 1,
      dmob:   v('28370') === 0,
      sink:   v('20747') < 15 && v('17933') < 2,
      chin:   v('20742') < 20 && v('4882') < 2,
      ff:     v('20744') <= 5 && !v('20740'),
      goebie: rem >= 700 && v('28796') !== 10,
      famil:  v('20738') === 1 && !(v('39271') > 0),
      gcache: v('52328') >= 800,
    };
  }
  // ---- Voice of Seren (Prifddinas hourly buffs) ----
  // Clan codes from CS2 script10599; effect lines quoted from script10600.
  const VOS_CLANS = { 1: 'Iorwerth', 2: 'Trahaearn', 3: 'Crwys', 4: 'Cadarn',
                      5: 'Amlodd', 6: 'Meilyr', 7: 'Hefin', 8: 'Ithell' };
  const VOS_FX = {
    1: '+20% Thieving XP from pickpocketing Iorwerth; +200 XP split between Attack and Strength from killing Iorwerth elves; Rush of Blood gives Slayer XP for all monsters',
    2: '+20% Thieving XP from pickpocketing Trahaearn; +20% Farming XP from Trahaearn patches; +20% Smithing XP smelting corrupted ore; +20% chance of extra ore in the district',
    3: '+20% Thieving XP from pickpocketing Crwys; +20% Woodcutting XP from Crwys trees and ivy; +20% Farming XP from Crwys patches; nests become Crystal geodes',
    4: '+20% Thieving XP from pickpocketing Cadarn; +200 Ranged XP from Cadarn ranged elf warriors; +200 Magic XP from Cadarn magic elf warriors',
    5: '+20% Thieving XP from pickpocketing Amlodd; +20% Divination XP converting shadow cores; +20% Summoning XP and +20% scrolls made in the Amlodd district',
    6: '+20% Thieving XP from pickpocketing Meilyr; +20% Farming XP from Meilyr patches; +20% Herblore XP making combination potions in the district',
    7: '+20% Thieving XP from pickpocketing Hefin; +20% Agility XP on the Hefin course; +20% Prayer XP cleansing Seren Stones',
    8: '+20% Thieving XP from pickpocketing Ithell; +20% Crafting XP playing harps; +20% Construction XP tuning harps; +20% Crafting XP on crystal flasks',
  };
  // Clan district sprites (CS2 script10599 code -> sprite id, shown by interface 1536).
  const VOS_SPRITES = { 1: 24205, 2: 24211, 3: 24206, 4: 24209, 5: 24204, 6: 24210, 7: 24207, 8: 24208 };
  let dwVosReportedHour = '';   // 'YYYY-M-D-H' already reported (once per hour)
  // The HOUR STAMP (vb 26416) is what makes stale pairs safe to reject: the varbits (pair AND
  // stamp) do NOT clear on leaving Prifddinas, they keep their last in-Priff value. So the stamp
  // only equals the current UTC hour when the player is genuinely in Priff THIS hour, which makes
  // `stamp === hr` valid at hour 0 too.
  const VOS_TRUST_HOUR0 = true;
  // Community value for hour `hr`, or null. The cache is fed passively by the SSE `vos` push (plus
  // the connect handshake); pass allowRefresh ONLY when displaying with a stale cache, which lets
  // the C++ kick its slow GET fallback (floored to 1 per 5 min).
  // True while this client sits on a leagues world. Leagues rotates its OWN Voice of Seren,
  // so both the value we read and the value we report belong to a separate pool, served
  // under `lg` in the same payload.
  function dwOnLeaguesWorld() {
    try {
      return !!(lastSnap && lastSnap.world && bridge() && bridge().isLeaguesWorld
                && rtxData.sync('cache.isLeaguesWorld', lastSnap.world));
    } catch (e) { return false; }
  }
  function dwVosCommunity(hr, allowRefresh) {
    if (!bridge() || !bridge().vosCached) return null;
    const lg = dwOnLeaguesWorld();
    const read = (refresh) => {
      try {
        const j = JSON.parse(rtxData.sync('host.vosCached', refresh ? 1 : 0) || '{}');
        return lg ? (j && j.lg) : j;      // leagues value rides under `lg`
      } catch (e) { return null; }
    };
    // `c.d` (UTC day number) rejects a value from a previous day at the same hour -- the hour stamp
    // alone (0-23) cannot. `c.d == null` = a server without the date stamp (accept on the hour
    // check alone).
    const today = Math.floor(Date.now() / 86400000);
    const valid = c => c && c.a >= 1 && c.a <= 8 && c.b >= 1 && c.b <= 8 && c.h === hr && (c.d == null || c.d === today);
    let c = read(false);
    if (!valid(c) && allowRefresh) c = read(true);   // kicks the background GET; value lands on a later pass
    return valid(c) ? c : null;
  }
  function dwVosState(vb) {
    const now = new Date(), hr = now.getUTCHours();
    const hourKey = now.getUTCFullYear() + '-' + now.getUTCMonth() + '-' + now.getUTCDate() + '-' + hr;
    const a = vb['25158'] | 0, b = vb['25159'] | 0, stamp = vb['26416'] | 0;
    // stamp==hr is necessary but NOT sufficient: a stale varbit (frozen on leaving Priff) re-matches
    // the same hour a day later. Require the player to be IN Priff right now.
    const stampOk = stamp === hr && (stamp !== 0 || VOS_TRUST_HOUR0) && dwInPriff;
    if (a >= 1 && a <= 8 && b >= 1 && b <= 8 && stampOk) {
      return { a, b, hour: hr, hourKey, src: 'live' };
    }
    const c = dwVosCommunity(hr, true);
    if (c) return { a: c.a, b: c.b, hour: hr, hourKey, src: 'community', n: c.n | 0 };
    return null;
  }
  // The always-on background reporter is C++ (Bridge.cpp vos_report_loop, view-free); this view
  // only runs once a panel has been opened. The path below is the tab-open complement: it also
  // CORRECTS a mismatched community value, which the background reporter deliberately does not do.
  // Report a live reading only when the community value already held (SSE-fed; no network read
  // here) is missing or DISAGREES -- so normally exactly one client posts per hour, and a wrong
  // value gets consensus-correcting reports. At most once per hour.
  function dwVosMaybeReport() {
    if (!dwData || !bridge() || !bridge().vosReport) return;
    const s = dwVosState(dwData);
    if (!s || s.src !== 'live') return;
    const c = dwVosCommunity(s.hour, false);
    if (c && c.a === s.a && c.b === s.b) { dwVosReportedHour = s.hourKey; return; }   // server already has it
    // RE-ARM: reported earlier but the served value vanished (website restart) -> report again. A
    // present-but-DIFFERENT value keeps the once-per-hour gate (one correction).
    if (!c && dwVosReportedHour === s.hourKey) dwVosReportedHour = '';
    if (dwVosReportedHour === s.hourKey) return;
    dwVosReportedHour = s.hourKey;
    // Reported INTO the pool this world belongs to, so leagues readings build the leagues
    // consensus rather than contradicting the main one.
    try { rtxData.sync('act.vosReport', s.a, s.b, dwOnLeaguesWorld()); } catch (e) {}
  }

  function dwCheckNotify() {
    if (!dwData) return;
    const avail = dwAvail(dwData);
    if (dwPrev) {
      const MSG = {
        star:   'A shooting star window is active',
        etree:  'An evil tree is up',
        dmob:   'A demon flashmob is starting',
        sink:   'A sinkhole is open',
        chin:   'Big Chinchompa is open',
        ff:     'Fish Flingers is open',
        goebie: 'A goebie supply run is open',
        famil:  'A familiarisation session is active',
        gcache: 'A Guthixian Cache rift is open (800 memories deposited)',
      };
      for (const k in avail) {
        if (avail[k] && !dwPrev[k] && dwNotify[k]) {
          try { uiNotify(MSG[k], { ttl: 8000 }); } catch (e) {}
          try { if (bridge().notifyWindows) bridge().notifyWindows('RuneToolsX', MSG[k]); } catch (e) {}
        }
      }
    }
    dwPrev = avail;
  }

  // ---- Daily challenges (CS2 18249/16319/16442) ----
  // Slot vars stride 4 from 16574: category / index / progress. Challenge struct id =
  // enum 17112[category] -> sub-enum[index] (script16318); display per script17039 = param 1266
  // (+ ": " + 4940), target = 2235, icon sprite = 1271, description = 1273.
  // Enum/struct lookups are cache-static -> memoized for the session.
  let dwChalEnum17112 = null;            // category -> sub-enum id
  const dwChalSubEnums = {};             // sub-enum id -> { index: structId }
  const dwChalStructs = {};              // struct id -> {name, desc, target, icon}
  let dwChalBusy = false;
  async function dwPaintChallenges(v) {
    const box = $('dwChalBox');
    if (!box || dwChalBusy || !bridge() || !bridge().enumInfo) return;
    if (!bridge().structParams) {          // older launcher build: say so instead of "Reading..." forever
      box.innerHTML = '<div class="dw-row"><div class="dw-nm">Needs a newer launcher build<div class="dw-sub">challenge names come from the cache struct reader</div></div></div>';
      return;
    }
    const slots = [];
    for (let i = 0; i < 5; i++) {
      const b = 16574 + i * 4;
      slots.push({ cat: v(String(b)), idx: v(String(b + 1)), prog: v(String(b + 2)) });
    }
    const sig = slots.map(s => s.cat + '.' + s.idx + '.' + s.prog).join('|');
    // The signature is stamped ON THE BOX ELEMENT, not in a module variable: renderDailies rebuilds
    // the whole panel (a fresh "Reading..." box) on every tab switch, and a module-level sig would
    // then match the old state and skip the repaint. A new element has no stamp, so it always
    // repaints.
    if (box.dataset.chalSig === sig) return;
    dwChalBusy = true;
    try {
      if (!dwChalEnum17112) {
        dwChalEnum17112 = await rtxData.call('cache.enumInfo', 17112);
        if (!dwChalEnum17112) return;                 // cache not ready -> retry next pass
      }
      const rows = [];
      for (const s of slots) {
        if (!s.cat || !s.idx) continue;               // empty slot (script16318: either 0 -> none)
        const sub = dwChalEnum17112[s.cat] | 0;
        if (sub <= 0) continue;
        if (!dwChalSubEnums[sub]) {
          dwChalSubEnums[sub] = (await rtxData.call('cache.enumInfo', sub)) || {};
        }
        const st = dwChalSubEnums[sub][s.idx] | 0;
        if (st <= 0) continue;
        if (!dwChalStructs[st]) {
          let sp = null;
          sp = await rtxData.call('cache.structParams', st);
          const ints = (sp && sp.ints) || {}, strs = (sp && sp.strs) || {};
          dwChalStructs[st] = {
            name: (strs['1266'] || ('Challenge #' + st)) + (strs['4940'] ? ': ' + strs['4940'] : ''),
            desc: strs['1273'] || '',
            target: ints['2235'] | 0,
            icon: ints['1271'] | 0,
          };
        }
        rows.push({ def: dwChalStructs[st], prog: s.prog, diag: 'cat ' + s.cat + ' · idx ' + s.idx + ' · struct ' + st });
      }
      box.dataset.chalSig = sig;                      // resolved -> stamp AFTER the lookups succeed
      box.innerHTML = '';
      if (!rows.length) {
        box.innerHTML = '<div class="dw-row"><div class="dw-nm">No active challenges<div class="dw-sub">Challenges appear here once assigned</div></div></div>';
        return;
      }
      for (const r0 of rows) {
        const row = document.createElement('div'); row.className = 'dw-row';
        row.dataset.tip = (r0.def.desc ? r0.def.desc + '\n' : '') + r0.diag;
        const ico = document.createElement('div'); ico.className = 'dw-chalico';
        if (r0.def.icon > 0) loadSpriteIcon(ico, r0.def.icon, 44);
        row.appendChild(ico);
        const nm = document.createElement('div'); nm.className = 'dw-nm';
        const t = document.createElement('div'); t.textContent = r0.def.name; nm.appendChild(t);
        row.appendChild(nm);
        const target = Math.max(1, r0.def.target);
        const done = r0.prog >= target;
        const bar = document.createElement('div'); bar.className = 'dw-bar';
        const fill = document.createElement('div'); fill.className = 'dw-fill' + (done ? ' done' : '');
        fill.style.width = Math.min(100, Math.round(r0.prog / target * 100)) + '%';
        bar.appendChild(fill); row.appendChild(bar);
        const val = document.createElement('div'); val.className = 'dw-val' + (done ? ' done' : '');
        val.textContent = r0.prog + ' / ' + target;
        row.appendChild(val);
        box.appendChild(row);
      }
    } finally { dwChalBusy = false; }
  }

  // ---- Meg's cases (CS2 script12480/12477/12475) ----
  // DBTable 9 (43 rows): col 0 = case number, col 2 = case name, col 9 = the case's DAY KEY.
  // script12480 joins col 9 against script12477(), which reads the LIVE key from varp 3079
  // (group-1477 context) / varp 6601 (group 906) -- NOT today's runeday (col-9 values are the
  // original release runedays). Completion flag = varbit 31222 + caseNumber (script12475's
  // 1->31223, 2->31224, ... switch).
  let dwMegRows = null, dwMegKey = -1, dwMegBusy = false;
  async function dwMegCase() {
    if (dwMegBusy || !bridge() || !bridge().dbRows || !dwVp) return;
    const key = (dwVp['3079'] | 0) || (dwVp['6601'] | 0);
    if (key <= 0 || key === dwMegKey) return;
    dwMegBusy = true;
    try {
      if (!dwMegRows) {
        dwMegRows = await rtxData.call('cache.dbRows', 9);
        if (!Array.isArray(dwMegRows) || !dwMegRows.length) { dwMegRows = null; return; }   // cache not ready -> retry
      }
      const row = dwMegRows.find(r0 => r0.i && r0.i['9'] && r0.i['9'][0] === key);
      let text = "Today's case: speak to Meg for information";
      if (row) {
        const caseNum = (row.i['0'] && row.i['0'][0]) | 0;
        const caseName = (row.s && row.s['2'] && row.s['2'][0]) || '';
        let done = false;
        if (caseNum > 0) {
          {
            const cb = (await rtxData.call('state.varbitsCsv', String(31222 + caseNum))) || {};
            done = (cb[String(31222 + caseNum)] | 0) === 1;
          }
        }
        text = done ? "Today's case: complete!" : ("Today's case: " + (caseName || ('#' + caseNum)));
      }
      dwMegKey = key;
      dwSetSub('meg', text);
    } finally { dwMegBusy = false; }
  }

  function dwSetBar(id, v, max, forceDone) {
    const f = $('dwb-' + id), val = $('dwv-' + id);
    if (!f || !val) return;
    const done = forceDone !== undefined ? forceDone : v >= max;
    f.style.width = Math.min(100, Math.round(v / max * 100)) + '%';
    f.classList.toggle('done', done);
    val.textContent = v + ' / ' + max;
    val.classList.toggle('done', done);
  }
  function dwSetPill(id, text, cls) {
    const p = $('dwp-' + id); if (!p) return;
    // Idempotent writes: timer pills repaint every render pass (live countdowns), so identical
    // values must not touch the DOM.
    if (p.textContent !== text) p.textContent = text;
    const cn = 'dw-pill' + (cls ? ' ' + cls : '');
    if (p.className !== cn) p.className = cn;
  }
  function dwSetSub(id, text) {
    const sb = $('dws-' + id); if (!sb) return;
    if (sb.textContent !== text) sb.textContent = text;
  }

  function renderDailies() {
    const c = $('content');
    let wrap = $('dwWrap');
    if (!wrap) {
      injectStyle('dwCss', `
          .dw-sec { margin: 2px 12px 0; }
          .dw-schead { display: flex; align-items: baseline; gap: 8px; padding: 10px 2px 6px; }
          .dw-st { flex: 1; color: var(--accent-hi); font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: .05em; }
          .dw-tlab { color: var(--text-mute); font-size: 10.5px; }
          .dw-timer { color: var(--text); font-size: 11px; font-variant-numeric: tabular-nums; background: var(--bg-elev); border: 1px solid var(--border); border-radius: 6px; padding: 2px 8px; }
          .dw-card { background: var(--bg-elev); border: 1px solid var(--border); border-radius: 10px; }
          .dw-row { display: flex; align-items: center; gap: 10px; padding: 8px 12px; }
          .dw-row + .dw-row { border-top: 1px solid var(--border); }
          .dw-nm { flex: 1; min-width: 0; color: var(--text); font-size: 12.5px; }
          .dw-sub { color: var(--text-mute); font-size: 10.5px; margin-top: 1px; }
          .dw-bar { flex: 0 0 84px; height: 5px; border-radius: 3px; background: var(--bg); overflow: hidden; }
          .dw-fill { width: 0; height: 100%; border-radius: 3px; background: var(--accent-hi); transition: width 300ms var(--ease); }
          .dw-fill.done { background: var(--ok); }
          .dw-val { flex: 0 0 auto; min-width: 42px; text-align: right; font-size: 12px; color: var(--text-dim); font-variant-numeric: tabular-nums; }
          .dw-val.done { color: var(--ok); }
          .dw-pill { font-size: 10.5px; padding: 2px 9px; border-radius: 999px; border: 1px solid var(--border); color: var(--text-dim); white-space: nowrap; }
          .dw-pill.ok { color: var(--ok); border-color: rgba(77, 210, 138, .45); }
          .dw-pill.go { color: var(--accent-hi); border-color: var(--accent-lo); }
          .dw-chips { display: flex; gap: 6px; flex-wrap: wrap; justify-content: flex-end; }
          .dw-chip { font-size: 10.5px; padding: 2px 8px; border-radius: 999px; border: 1px solid var(--border); color: var(--text-mute); }
          .dw-chip.on { color: var(--ok); border-color: rgba(77, 210, 138, .45); }
          .dw-chip.dw-vosic { display: inline-block; width: 32px; height: 32px; padding: 3px; border-radius: 8px; }
          .dw-bell { flex: 0 0 auto; display: flex; align-items: center; justify-content: center; width: 22px; height: 22px; border-radius: 6px; background: none; border: 1px solid var(--border); color: var(--text-mute); cursor: pointer; padding: 0; transition: border-color var(--tx-fast), color var(--tx-fast); }
          .dw-bell:hover { border-color: var(--border-hi); color: var(--text-dim); }
          .dw-bell.on { color: var(--accent-hi); border-color: var(--accent-lo); }
          .dw-empty { margin: 10px 14px; color: var(--text-dim); font-size: 12px; }
          .dw-chalico { flex: 0 0 auto; width: 22px; height: 22px; }`);
      c.innerHTML = ''; dwSig = '';
      wrap = document.createElement('div'); wrap.id = 'dwWrap'; wrap.className = 'pane'; c.appendChild(wrap);
      const sec = (title, timerId) => {
        const s = document.createElement('div'); s.className = 'dw-sec';
        const h = document.createElement('div'); h.className = 'dw-schead';
        const t = document.createElement('div'); t.className = 'dw-st'; t.textContent = title; h.appendChild(t);
        if (timerId) {
          const lab = document.createElement('span'); lab.className = 'dw-tlab'; lab.textContent = 'resets in'; h.appendChild(lab);
          const tm = document.createElement('span'); tm.className = 'dw-timer'; tm.id = timerId; h.appendChild(tm);
        }
        s.appendChild(h);
        const card = document.createElement('div'); card.className = 'dw-card'; s.appendChild(card);
        wrap.appendChild(s);
        return card;
      };
      {   // Daily challenges: five live slots painted by dwPaintChallenges; names change daily, so the
// card holds one rebuildable box.
        const chal = sec('Daily challenges', 'dwt-chal');
        const box = document.createElement('div'); box.id = 'dwChalBox';
        box.innerHTML = '<div class="dw-row"><div class="dw-nm">Reading...</div></div>';
        chal.appendChild(box);
      }
      {   // Voice of Seren: two clan chips + a source line; header timer = next hour flip
        const vos = sec('Voice of Seren', 'dwt-vos');
        const r0 = document.createElement('div'); r0.className = 'dw-row';
        const nm = document.createElement('div'); nm.className = 'dw-nm';
        const t = document.createElement('div'); t.textContent = 'Active clans'; nm.appendChild(t);
        const sb = document.createElement('div'); sb.className = 'dw-sub'; sb.id = 'dws-vos';
        sb.textContent = 'Reading...'; nm.appendChild(sb);
        r0.appendChild(nm);
        const chips = document.createElement('div'); chips.className = 'dw-chips';
        ['vosa', 'vosb'].forEach(id => {
          const ch = document.createElement('span'); ch.className = 'dw-chip'; ch.id = 'dwc-' + id;
          ch.textContent = '?'; chips.appendChild(ch);
        });
        r0.appendChild(chips);
        vos.appendChild(r0);
        const lab = vos.parentNode.querySelector('.dw-tlab');
        if (lab) lab.textContent = 'changes in';   // hour flip, not a reset
      }
      // Uncapped spawn-loop activities: timing only.
      const spawns = sec('Spawns', null);
      spawns.appendChild(dwPillRow('star', 'Shooting star', 'Spawns roughly every 90 minutes', 'star'));
      spawns.appendChild(dwPillRow('etree', 'Evil tree', 'No daily limit', 'etree'));
      spawns.appendChild(dwPillRow('dmob', 'Demon flashmob', null, 'dmob'));
      const daily = sec('Daily', 'dwt-daily');
      daily.appendChild(dwRow('sink', 'Sinkholes', 'Daemonheim camp', 'sink'));
      daily.appendChild(dwRow('chin', 'Plutonial chinchompas', 'Big Chinchompa', 'chin'));
      daily.appendChild(dwPillRow('ff', 'Fish Flingers', 'Competitions', 'ff'));
      daily.appendChild(dwPillRow('rsphere', 'Runesphere', 'Runespan'));
      daily.appendChild(dwRow('gcache', 'Guthixian Cache', 'Rift progress', 'gcache'));
      daily.appendChild(dwPillRow('goebie', 'Goebie supply runs', 'Bring supplies to Otot', 'goebie'));
      const weekly = sec('Weekly', 'dwt-weekly');
      weekly.appendChild(dwRow('peng', 'Penguins spied', 'Penguin points'));
      {   // circus: three performance chips instead of a bar
        const r = document.createElement('div'); r.className = 'dw-row';
        const nm = document.createElement('div'); nm.className = 'dw-nm';
        const t = document.createElement('div'); t.textContent = 'Circus performances'; nm.appendChild(t);
        const sb = document.createElement('div'); sb.className = 'dw-sub';
        sb.textContent = "Balthazar Beauregard's Big Top Bonanza"; nm.appendChild(sb);
        r.appendChild(nm);
        const chips = document.createElement('div'); chips.className = 'dw-chips';
        [['agi', 'Agility'], ['mag', 'Magic'], ['rng', 'Ranged']].forEach(cd => {
          const ch = document.createElement('span'); ch.className = 'dw-chip'; ch.id = 'dwc-' + cd[0];
          ch.textContent = cd[1]; chips.appendChild(ch);
        });
        r.appendChild(chips);
        weekly.appendChild(r);
      }
      weekly.appendChild(dwPillRow('famil', 'Familiarisation', 'Help Pikkenmix', 'famil'));
      weekly.appendChild(dwPillRow('meg', 'Meg', 'Send on an adventure from the port'));
      weekly.appendChild(dwPillRow('nomad', "Nomad's bounty"));
      weekly.appendChild(dwPillRow('thal', "Thalmund's Wares", 'City of Um smithy, Wednesdays'));
      weekly.appendChild(dwPillRow('tears', 'Tears of Guthix', 'Beneath Lumbridge'));
      weekly.appendChild(dwPillRow('wisps', 'Wisps of the Grove', 'Help the tree spirits'));
      weekly.appendChild(dwPillRow('rush', 'Rush of Blood', "Morvran's arena below Prifddinas"));
      weekly.appendChild(dwPillRow('skel', 'Skeletal Horror'));
      weekly.appendChild(dwPillRow('champ', "Champion's Challenge"));
      weekly.appendChild(dwRow('agoroth', 'Agoroth', 'Weekly kills'));
      weekly.appendChild(dwRow('sworlds', 'Shattered Worlds challenges', 'Abyssal Knights'));
      weekly.appendChild(dwRow('herby', 'Herby Werby', 'Spirit points'));
      weekly.appendChild(dwRow('jad', 'Jadinkos', 'Herblore Habitat'));
      weekly.appendChild(dwRow('gjad', 'God jadinkos', 'Herblore Habitat'));
      const monthly = sec('Monthly', 'dwt-monthly');
      monthly.appendChild(dwPillRow('troll', 'Troll Invasion', 'Burthorpe gatehouse'));
      monthly.appendChild(dwPillRow('effigy', 'Effigy incubator'));
      monthly.appendChild(dwPillRow('oyster', 'Giant oyster'));
      monthly.appendChild(dwRow('gsb', 'Statues built', 'God Statues'));
      monthly.appendChild(dwRow('gsp', 'Statues prayed at', 'God Statues'));
      // Menaphos journal collections: one-time, no reset timer. Hover a row for the found list.
      const colls = sec('Collections', null);
      colls.appendChild(dwRow('mins', 'Insects of the Desert', 'Menaphos journal'));
      colls.appendChild(dwRow('mjew', 'Jewels of the Elid', 'Menaphos journal'));
      colls.appendChild(dwRow('mcat', 'Cats of Menaphos', 'Menaphos journal'));
      const empty = document.createElement('div'); empty.className = 'dw-empty'; empty.id = 'dwEmpty';
      empty.textContent = 'Reading... (be in-world)';
      wrap.appendChild(empty);
    }
    // ---- every-pass zone: wall-clock countdowns (write only on a visible change) ----
    const r = dwNextResets();
    const tick = (id, ms) => {
      const el = $(id); if (!el) return;
      const s = dwFmt(ms);
      if (el.textContent !== s) el.textContent = s;
    };
    tick('dwt-daily', r.daily - r.now);
    tick('dwt-chal', r.daily - r.now);
    // Daily challenges paint EVERY pass, not inside the varbit-sig gate: they carry their own
    // change-signature + busy guard, and a failed resolve does not stamp its sig, so this retries
    // each pass until the cache serves the enum/struct lookups.
    if (dwData) dwPaintChallenges(k => (dwData[k] | 0));
    tick('dwt-weekly', r.weekly - r.now);
    tick('dwt-monthly', r.monthly - r.now);
    // Voice of Seren: painted every pass (a community report can arrive between varbit changes) with
    // idempotent writes; runs even with no varbit data (community-only).
    {
      const now = new Date();
      const nextHr = Date.UTC(now.getUTCFullYear(), now.getUTCMonth(), now.getUTCDate(), now.getUTCHours() + 1);
      tick('dwt-vos', nextHr - now.getTime());
      const s = dwVosState(dwData || {});
      // District sprite chips (CSS background-image, per the Ultralight icon rule); the clan name +
      // effect lines live in the tooltip. dataset.vos makes writes idempotent.
      const setChip = (id, code) => {
        const el = $('dwc-' + id); if (!el) return;
        code = code | 0;
        if ((el.dataset.vos | 0) === code && el.dataset.vos !== undefined) return;
        el.dataset.vos = code;
        el.classList.toggle('on', !!code);
        el.classList.toggle('dw-vosic', !!code);
        if (code) {
          el.textContent = '';
          loadSpriteIcon(el, VOS_SPRITES[code] || 0, 64);   // 2x the 32px box: clean 2:1 rescale
          el.dataset.tip = 'Voice of ' + (VOS_CLANS[code] || ('clan ' + code)) + ': ' + (VOS_FX[code] || '');
        } else {
          el.style.backgroundImage = '';
          el.textContent = '?';
          delete el.dataset.tip;
        }
      };
      setChip('vosa', s ? s.a : 0); setChip('vosb', s ? s.b : 0);
      let src = 'Unknown - visit Prifddinas, or wait for a community report';
      if (s && s.src === 'live') src = 'Read live from your client';
      else if (s) src = 'Community reported' + (s.n > 1 ? ' (' + s.n + ' reports)' : '');
      const sub = $('dws-vos');
      if (sub && sub.textContent !== src) sub.textContent = src;
    }
    const empty = $('dwEmpty');
    if (empty) empty.style.display = dwData ? 'none' : '';
    if (!dwData) return;
    const v = k => (dwData[k] | 0);
    // Thalmund's Wares (CS2 script4298/3872 + reqs case 3087). Painted every pass: the away-state
    // pill counts down live to his Wednesday return (= the weekly reset instant). All-bought wins
    // over the Wednesday check: the purchase state persists through the week.
    {
      const viewed = v('55087') > 0 || v('55089') > 0 || v('55091') > 0 || v('55093') > 0;
      const allBought = viewed &&
        v('55086') >= v('55087') && v('55088') >= v('55089') &&
        v('55090') >= v('55091') && v('55092') >= v('55093');
      if (v('53496') < 35)                   dwSetPill('thal', 'Requires Kili Row');
      else if (allBought)                    dwSetPill('thal', 'All wares bought this week', 'ok');
      else if (new Date().getUTCDay() !== 3) dwSetPill('thal', 'Returns in ' + dwFmt(r.weekly - r.now));
      else if (viewed)                       dwSetPill('thal', 'Wares remaining', 'go');
      else                                   dwSetPill('thal', 'In the Um smithy now', 'go');
    }
    // Goebie supply runs (CS2 script13784): wall-clock 12h cycle (windows open for the last 20
    // minutes of each half-day); vb 28796 == 10 = both runs done today.
    {
      const now = new Date();
      const rem = 720 - ((now.getUTCHours() * 60 + now.getUTCMinutes()) % 720);
      if (v('28796') === 10) dwSetPill('goebie', 'Done today', 'ok');
      else if (rem >= 700)   dwSetPill('goebie', 'Run open, ends in ' + (rem - 700) + 'm', 'go');
      else                   dwSetPill('goebie', 'Next run in ' + dwM2S(rem), 'go');
    }
    // ---- varbit-driven zone: repaint only when a var actually changed ----
    const sig = DW_IDS.split(',').map(k => dwData[k] | 0).join(',') + '|' +
                (dwVp ? (dwVp['5441'] | 0) + ',' + (dwVp['5442'] | 0) + ',' +
                        (dwVp['6989'] | 0) + ',' + (dwVp['6990'] | 0) : 'x') +
                '|' + Math.floor(Date.now() / 86400000);   // collections "today" tags flip at UTC midnight
    if (sig === dwSig) return;
    dwSig = sig;
    // -- spawns --
    // Shooting star (CS2 script9166; no mined-count cap).
    {
      const act = v('20739') === 1, t = v('20750');
      if (act && t === 0)  dwSetPill('star', 'Available now', 'ok');
      else if (act)        dwSetPill('star', 'Resets in ' + dwM2S(t), 'go');
      else if (t === 0)    dwSetPill('star', 'No upcoming stars');
      else                 dwSetPill('star', 'Begins in ' + dwM2S(t), 'go');
    }
    // Evil tree (CS2 script9158): timing only, no cap.
    if (v('20745') === 1) dwSetPill('etree', 'A tree is up now', 'ok');
    else if (v('20746') > 0) dwSetPill('etree', 'Next tree in ' + dwM2S(v('20746')), 'go');
    else dwSetPill('etree', 'Spawning soon', 'go');
    // Demon flashmob (CS2 script11604): always "begins in vb 28370".
    if (v('28370') === 0) dwSetPill('dmob', 'Starting now', 'ok');
    else dwSetPill('dmob', 'Next mob in ' + dwM2S(v('28370')), 'go');
    // -- daily --
    // Sinkholes (CS2 script9150): vb 17933 = played today of 2; vb 20747 = minutes into the hourly
    // cycle (< 15 = open, collapses in 15 - v; else next in 60 - v).
    dwSetBar('sink', v('17933'), 2);
    if (v('17933') >= 2)       dwSetSub('sink', 'Done for today');
    else if (v('20747') < 15)  dwSetSub('sink', 'Open now, collapses in ' + (15 - v('20747')) + 'm');
    else                       dwSetSub('sink', 'Next sinkhole in ' + (60 - v('20747')) + 'm');
    // Big Chinchompa (CS2 script9144): daily catches (bar, cap 2) + the hourly session from
    // vb 20742 (< 20 = open, ends in 20 - v; else next in 60 - v).
    dwSetBar('chin', v('4882'), 2);
    if (v('20742') < 20) dwSetSub('chin', 'Open now, ends in ' + (20 - v('20742')) + 'm');
    else                 dwSetSub('chin', 'Next opens in ' + (60 - v('20742')) + 'm');
    // Fish Flingers (CS2 script9155): vb 20740 = played today; vb 20744 <= 5 = a competition is
    // running (ends in v), else the next lobby opens in v - 5.
    if (v('20740'))            dwSetPill('ff', 'Played today', 'ok');
    else if (v('20744') <= 5)  dwSetPill('ff', 'Competition ends in ' + v('20744') + 'm', 'go');
    else                       dwSetPill('ff', 'Lobby opens in ' + dwM2S(v('20744') - 5), 'go');
    // Fish Flingers economy (CS2 script6263): vb 4662 tokens, 4649 medals, 4673 tackle box tier 0-5
    // (names from the script's own switch); next-tier costs read from the cache at runtime
    // (enum 5886 tokens / 5887 medals); outfit purchase flags vb 4693-96 at 140 tokens each.
    {
      const tk = ["Beginner's", 'Basic', 'Standard', 'Professional', "Champion's", "Champion's"];
      const t = v('4673');
      const outfit = ['4693', '4694', '4695', '4696'].reduce((n, k) => n + (v(k) === 1 ? 1 : 0), 0);
      dwSetSub('ff', v('4662') + ' tokens · ' + v('4649') + ' medals · ' + (tk[t] || '?') + ' tackle box');
      const ffSub = $('dws-ff');
      if (ffSub && ffSub.parentElement) {
        const up = t >= 5 ? 'Tackle box maxed'
                 : dwFFCosts ? 'Next tackle box: ' + (dwFFCosts[0][String(t + 1)] | 0) + ' tokens + ' +
                               (dwFFCosts[1][String(t + 1)] | 0) + ' medals'
                 : '';
        ffSub.parentElement.dataset.tip = (up ? up + '\n' : '') +
          'Outfit pieces purchased: ' + outfit + ' / 4 (140 tokens each)';
      }
    }
    // Runesphere (CS2 script6416): vb 16526 == 1 = siphoned today.
    if (v('16526') === 1) dwSetPill('rsphere', 'Done today', 'ok');
    else dwSetPill('rsphere', 'Available', 'go');
    // Guthixian Cache (2026-03 rework): vb 52328 (bits 0-13 of varp 12668, locmorph of the energy
    // rift) = memories deposited toward opening the rift, 800 = open. vb 25543 = points this cache,
    // cap 100 PER INSTANCE; vb 25551 = memories converted; vb 25552 = automatons subdued. Rifts are
    // player-driven, so the rift progress IS the schedule; the bell fires when it reaches 800.
    // Inside a cache (points > 0) the bar switches to the run's points instead.
    {
      const pts = v('25543'), rift = v('52328');
      if (pts > 0) {
        dwSetBar('gcache', pts, 100);
        const bits = [];
        if (v('25551')) bits.push(v('25551') + ' memor' + (v('25551') === 1 ? 'y' : 'ies') + ' converted');
        if (v('25552')) bits.push(v('25552') + ' automaton' + (v('25552') === 1 ? '' : 's') + ' subdued');
        dwSetSub('gcache', bits.length ? bits.join(' · ') : 'Points this cache (100 max per instance)');
      } else {
        dwSetBar('gcache', rift, 800);
        dwSetSub('gcache', rift >= 800 ? 'Rift open - ready to enter'
                                       : 'Deposit memories to open the rift (' + rift + ' / 800)');
      }
    }
    // -- weekly --
    dwSetBar('peng', v('4164'), 10);
    // Penguin spying gates (D&D-reqs CS2 case 3002): vb 4165 == 1 = unlocked (else visit Larry or
    // Chuck in Ardougne Zoo), total points vb 4163 cap at 250.
    if (v('4165') !== 1)       dwSetSub('peng', 'Visit Larry or Chuck in Ardougne Zoo to unlock');
    else if (v(String(typeof VB !== 'undefined' ? VB.PENGUIN_POINTS : 4163)) >= 250) dwSetSub('peng', 'Penguin points at the 250 cap');
    else                       dwSetSub('peng', v(String(typeof VB !== 'undefined' ? VB.PENGUIN_POINTS : 4163)) + ' / 250 penguin points');
    [['agi', '5479'], ['mag', '5480'], ['rng', '5481']].forEach(cd => {
      const ch = $('dwc-' + cd[0]); if (!ch) return;
      ch.classList.toggle('on', v(cd[1]) > 0);
      ch.title = 'varbit ' + cd[1] + ' = ' + v(cd[1]);
    });
    // Familiarisation (CS2 script10539): vb 39271 > 0 = helped Pikkenmix this week; sessions run on
    // a schedule -> vb 20738 == 1 = active (ends in vb 20749 minutes), else the next session begins
    // in vb 20749 minutes.
    if (v('39271') > 0)       dwSetPill('famil', 'Helped this week', 'ok');
    else if (v('20738') === 1) dwSetPill('famil', 'Active, ends in ' + dwM2S(v('20749')), 'go');
    else                       dwSetPill('famil', 'Begins in ' + dwM2S(v('20749')), 'go');
    // Meg (CS2 script9146): sent this week / never visited / ready again.
    if (v('17439') === 1) dwSetPill('meg', 'Sent this week', 'ok');
    else if (v('17445') === 0) dwSetPill('meg', 'Visit Meg in the port', 'go');
    else dwSetPill('meg', 'Ready for an adventure', 'go');
    dwMegCase();   // async: today's case name from DBTable 9 into the Meg sub-line
    // Nomad's bounty (CS2 script12782): vb 33780 == 1 = already looted this week.
    if (v('33780') === 1) dwSetPill('nomad', 'Looted this week', 'ok');
    else dwSetPill('nomad', 'Available to loot', 'go');
    // Tears of Guthix (CS2 script9152): vb 26630 == 1 = done this week.
    if (v('26630') === 1) dwSetPill('tears', 'Done this week', 'ok');
    else dwSetPill('tears', 'Available', 'go');
    // Wisps of the Grove (CS2 script12168): vb 30284 == 1 = done this week.
    if (v('30284') === 1) dwSetPill('wisps', 'Done this week', 'ok');
    else dwSetPill('wisps', 'Available', 'go');
    // Rush of Blood (CS2 script10607): vb 25048 != 0 = done this week.
    if (v('25048') !== 0) dwSetPill('rush', 'Done this week', 'ok');
    else dwSetPill('rush', 'Available', 'go');
    // Skeletal Horror (CS2 script11548): gate = Fur 'n Seek: the wish list (vb 9902 >= 10);
    // vb 26628 = killed this week.
    if (v('9902') < 10) dwSetPill('skel', "Requires Fur 'n Seek: the wish list");
    else if (v('26628')) dwSetPill('skel', 'Done this week', 'ok');
    else dwSetPill('skel', 'Available', 'go');
    // Champion's Challenge (CS2 script11552): varp 5441 == varp 5442 = nothing left this week. Both
    // 0 = no champion scrolls banked at all, where the equality is trivially true and the game's
    // rule would misread as "done", so that case shows as nothing-available instead.
    if (dwVp) {
      const c1 = dwVp['5441'] | 0, c2 = dwVp['5442'] | 0;
      if (c1 === 0 && c2 === 0) dwSetPill('champ', 'No challenges available');
      else if (c1 === c2) dwSetPill('champ', 'Done this week', 'ok');
      else dwSetPill('champ', 'Challenge available', 'go');
    }
    // Agoroth (CS2 script9089/9091): vb 22285 kills vs weekly target 2 (members) / 1 (F2P).
    dwSetBar('agoroth', v('22285'), 2);
    {
      const rowEl = $('dws-agoroth');
      if (rowEl) {
        const host = rowEl.closest('.dw-row');
        if (host) host.dataset.tip = 'Weekly cap: 2 kills (members), 1 (free-to-play)';
      }
    }
    // Shattered Worlds weekly challenges (CS2 script14930): 35817/35818/35819 each == 1.
    dwSetBar('sworlds', (v('35817') === 1 ? 1 : 0) + (v('35818') === 1 ? 1 : 0) + (v('35819') === 1 ? 1 : 0), 3);
    // Herby Werby (CS2 script7225): vb 44349 == 1 -> completed this week (green regardless of
    // points); vb 44351 = spirit points collected this week, of 100.
    {
      const hwDone = v('44349') === 1;
      dwSetBar('herby', v('44351'), 100, hwDone || v('44351') >= 100);
      dwSetSub('herby', hwDone ? 'Completed this week' : 'Spirit points');
    }
    // Herblore Habitat jadinkos (CS2 script7362). Once the weekly state advances past 0 every
    // species counts as caught, so the bar fills from the state, not the species varbits; the sub
    // shows which Papa Mambo claim is still outstanding.
    {
      const JADS = [['16101', 'Common'], ['16103', 'Amphibious'], ['16102', 'Aquatic'],
                    ['16108', 'Shadow'], ['16105', 'Carrion'], ['16106', 'Cannibal'],
                    ['16104', 'Camouflaged'], ['16109', 'Diseased'], ['16107', 'Draconic'],
                    ['16110', 'Igneous']];
      const GODS = [['16111', 'Guthix'], ['16112', 'Saradomin'], ['16113', 'Zamorak']];
      const paintJads = (id, list, st, doneSt, claims, subIdle) => {
        const caught = st > 0 ? list.length : list.reduce((n, j) => n + (v(j[0]) > 0 ? 1 : 0), 0);
        dwSetBar(id, caught, list.length, st > 0);
        const sb = $('dws-' + id);
        if (sb) {
          sb.textContent = st === 0 ? subIdle
            : st >= doneSt ? 'Completed and claimed'
            : 'Claim ' + claims[st - 1] + ' from Papa Mambo';
          const rowEl = sb.closest('.dw-row');
          if (rowEl) rowEl.dataset.tip =
            list.map(j => (st > 0 || v(j[0]) > 0 ? '[caught] ' : '[  --  ] ') + j[1] + ' Jadinko').join('\n');
        }
      };
      paintJads('jad', JADS, v('16096'), 3, ['XP + clothing', 'clothing'], 'Herblore Habitat');
      paintJads('gjad', GODS, v('16127'), 4, ['XP + clothing', 'clothing', 'farming boost'], 'Herblore Habitat');
    }
    // -- monthly --
    // Troll Invasion (CS2 script9154): vb 20741 = done this month.
    if (v('20741')) dwSetPill('troll', 'Done this month', 'ok');
    else dwSetPill('troll', 'Available', 'go');
    // Effigy incubator (CS2 script4720): vb 15893 == 1 -> already powered this month.
    if (v('15893') === 1) dwSetPill('effigy', 'Powered this month', 'ok');
    else dwSetPill('effigy', 'Available', 'go');
    // Giant oyster: gated by Beneath Cursed Tides (D&D-reqs case 3074: quest vb 30071 >= 200). Full
    // completion per the timer handler (CS2 script11959): oyster state vb 30084 >= 2 AND both feed
    // counters vb 30087 / vb 30088 at 30.
    {
      const oy = v('30084'), fed = v('30087') === 30 && v('30088') === 30;
      if (v('30071') < 200)     dwSetPill('oyster', 'Requires Beneath Cursed Tides');
      else if (oy >= 2 && fed)  dwSetPill('oyster', 'Fed this month', 'ok');
      else if (oy >= 1)         dwSetPill('oyster', 'Feeding: ' + v('30087') + '/30 and ' + v('30088') + '/30', 'go');
      else                      dwSetPill('oyster', 'Not available');
    }
    // God Statues (CS2 script9163): built = statue locmorph varbits > 0, prayed = sum.
    const built = ['17687', '60099', '17689', '17690', '24942'].reduce((n, k) => n + (v(k) > 0 ? 1 : 0), 0);
    const prayed = ['17691', '60100', '17693', '17694', '24943'].reduce((n, k) => n + v(k), 0);
    dwSetBar('gsb', built, 5);
    dwSetBar('gsp', prayed, 5);
    // -- Menaphos journal collections (CS2 script13412 via 13411/13421) --
    // found = TESTBIT(varp, bit): Insects of the Desert = varp 6989 bits 0-15, Jewels of the Elid =
    // varp 6989 bits 16-31, Cats of Menaphos = varp 6990 bits 0-15. Item order and names from the
    // cache (enum 12605 -> 12606/12607/12608, struct param 6063; the bit index is each struct's
    // param 6059 = its enum index). Locations + schedules are NOT in the cache (cat spawns are
    // server-side, no morph vars): they come from the RS Wiki. Six cats wander Menaphos on fixed
    // UTC weekdays; the Scabarite crystal / Apmeken amethyst houses follow a 200-day rotation.
    if (dwVp) {
      // Jewel-house rotation (wiki Module:Rotations/RsRandom): one Java-Random step on seed
      // (runedate*2^32) ^ 0x5DEECE66D, then (seed >> 17) mod 5 -- slot 0 = Scabarite crystal open,
      // 2 = Apmeken amethyst open, else neither. Runedate = days since 27 Feb 2002 UTC. 16-bit limb
      // arithmetic keeps every product exact in doubles. The wiki notes the pattern "slightly breaks"
      // every ~3 years, presumably a reseed, so a far-future prediction can drift.
      const jewelSlot = (rd) => {
        const x0 = 0xE66D, x1 = 0xDEEC, x2 = ((rd & 0xFFFF) ^ 5) & 0xFFFF;
        const r0 = x0 * 0xE66D + 11;
        const r1 = x1 * 0xE66D + x0 * 0xDEEC + Math.floor(r0 / 65536);
        const r2 = x2 * 0xE66D + x1 * 0xDEEC + x0 * 5 + Math.floor(r1 / 65536);
        return Math.floor(((r2 % 65536) * 4294967296 + (r1 % 65536) * 65536 + (r0 % 65536)) / 131072) % 5;
      };
      const rdToday = Math.floor((Date.now() - Date.UTC(2002, 1, 27)) / 86400000);
      const utcDay = new Date().getUTCDay();
      const COLLS = [
        ['mins', '6989', 0, [
          ['Menaphite honey bee in amber', 'mining, Worker district / VIP area'],
          ['Pygmy giant scarab in amber', 'mining, Worker district / VIP area'],
          ['Clicker kalphite in amber', 'fishing, Port district / VIP area'],
          ['Desert locust in amber', 'fishing, Port district / VIP area'],
          ['Kalphite wanderer in amber', 'pickpocket the Gullible tourist / Menaphite marketeers'],
          ['Hornless unicornfly in amber', 'pickpocket the Gullible tourist / Menaphite marketeers'],
          ['Fly dragon in amber', 'steal from the lamp or silk stall, Merchant district'],
          ['Fruit fly in amber', 'burning acadia logs (1/800; anywhere)'],
          ['Menaphite honey bee', 'burning acadia logs (anywhere)'],
          ['Pygmy giant scarab', 'hunting plover birds'],
          ['Clicker kalphite', 'hunting plover birds (1/200)'],
          ['Desert locust', 'woodcutting acadia, Imperial district / VIP area'],
          ['Kalphite wanderer', 'woodcutting acadia, Imperial district / VIP area'],
          ['Hornless unicornfly', 'woodcutting, VIP area'],
          ['Fly dragon', 'harvesting luminous memories'],
          ['Fruit fly', 'harvesting luminous memories']]],
        ['mjew', '6989', 16, [
          ['Phenakite', 'mining mineral deposits, VIP area'],
          ['Menaphyrite', 'Pyramid Plunder urns'],
          ['Waikonite', 'pickpocket the Gullible tourist, Port dock'],
          ['Corundum', 'crystalline corruption / Shifting Tombs chests (~1/750)'],
          ['Wavecrest opal', 'fishing, Port district / VIP area'],
          ['Maw coral', 'fishing, Port district / VIP area'],
          ['Crondite', 'mining mineral deposits, Worker district / VIP area (1/3000)'],
          ['Alaeite', 'riddler crab riddle, Port dock'],
          ['Hetite stone', 'Sophanem Slayer Dungeon (corrupted creatures / soul devourers task)'],
          ['Aminishi miststone', 'Aminishi gem trader, Port dock (1 spices)'],
          ['Hauyne', 'complete any City Quest'],
          ['Scabarite crystal', 'locked house west of Grand Vizier Ehsan, Merchant district'],
          ['Tuai Leit sparkle', 'Tuai Leit gem trader, Port dock (5 bamboo)'],
          ['Idocrase', 'complete any City Quest'],
          ['Apmeken amethyst', 'locked house, north-west Merchant district'],
          ['Umesco arpos', 'mining mineral deposits, Worker district / VIP area']]],
        ['mcat', '6990', 0, [
          ['Anukat', 'VIP skilling area, Imperial district'],
          ['Bestopet', 'Shifting Tombs; may appear after smashing any pot'],
          ['Oedipuss', 'locked house by the Port district musician (tier 5 Port rep)'],
          ['Hetepheres', ''],
          ['Lucifurr', ''],
          ['Jennifurr', 'Pyramid Plunder sarcophagi'],
          ['Katarina', 'Sophanem Slayer Dungeon kills'],
          ['Shebit', 'locked house by Khamud, north-east Worker district (tier 5 Worker rep)'],
          ['Takhuit', ''],
          ['Catsanova', 'locked house, north-east Merchant district (tier 5 Merchant rep)'],
          ['The Postcat', 'walks a loop: Menaphos gates, Pollnivneach, Shantay Pass, Mining Camp, Bandit Camp'],
          ['Fenekh', ''],
          ['Tefimhet', 'Soul Altar'],
          ['Nodjmet', ''],
          ['Blanchy', ''],
          ['Qat', 'locked house near Banafrit, north-west Imperial district (tier 5 Imperial rep)']]],
      ];
      // Weekday wanderers (cat enum idx -> UTC day; game days flip at 00:00 UTC) and the
      // rotation-locked jewel houses (jewel enum idx -> LCG slot code).
      const CAT_DAYS = { 3: 0, 4: 2, 8: 3, 11: 4, 13: 5, 14: 6 };
      const DAY_NAMES = ['Sundays', 'Mondays', 'Tuesdays', 'Wednesdays', 'Thursdays', 'Fridays', 'Saturdays'];
      const JEWEL_ROT = { 11: 0, 14: 2 };
      COLLS.forEach(cl => {
        const bits = dwVp[cl[1]] | 0, base = cl[2], items = cl[3];
        const got = items.map((it, i) => ((bits >>> (base + i)) & 1) === 1);
        dwSetBar(cl[0], got.reduce((n, g) => n + (g ? 1 : 0), 0), items.length);
        const today = [];   // missing items specifically obtainable today
        const tip = items.map((it, i) => {
          let note = it[1];
          if (cl[0] === 'mcat' && CAT_DAYS[i] !== undefined) {
            note = 'wanders Menaphos on ' + DAY_NAMES[CAT_DAYS[i]];
            if (!got[i]) {
              if (CAT_DAYS[i] === utcDay) { note += ' - TODAY'; today.push(it[0]); }
              else note += ' (in ' + ((CAT_DAYS[i] - utcDay + 7) % 7) + 'd)';
            }
          } else if (cl[0] === 'mjew' && JEWEL_ROT[i] !== undefined && !got[i]) {
            if (jewelSlot(rdToday) === JEWEL_ROT[i]) { note += ' - open TODAY'; today.push(it[0]); }
            else {
              let d = 1;
              while (d < 200 && jewelSlot(rdToday + d) !== JEWEL_ROT[i]) d++;
              note += ' (opens in ' + d + 'd)';
            }
          }
          return (got[i] ? '[found] ' : '[  --  ] ') + it[0] + ' - ' + note;
        }).join('\n');
        const sb = $('dws-' + cl[0]);
        if (sb) {
          const rowEl = sb.closest('.dw-row');
          if (rowEl && rowEl.dataset.tip !== tip) rowEl.dataset.tip = tip;
          const subTxt = today.length ? today.join(' + ') + ' available today' : 'Menaphos journal';
          if (sb.textContent !== subTxt) sb.textContent = subTxt;
        }
      });
    }
  }

  // ---- Meg's weekly questions: highlight the best answer ----
  // Meg's answer quality is rolled once per session (wiki-sourced table): every question offers
  // Excellent/Good/Neutral/Bad/Terrible options. The dialogue (group 1188) shows 4-5 options;
  // WHICH question is identified by matching the visible option texts against the table (answer
  // texts repeat across questions with DIFFERENT ratings, so the question is chosen by most
  // matches, never by a single option), then the best-rated visible option is boxed via
  // uiHighlight. Data: '#Skill' starts a question; 'R|answer' lines with R = E/G/N/B/T.
  const MEG_QA_RAW = "\n" +
    "#Hunter\nE|They'll probably use sound to track prey.\nE|Dance on the ground to attract them.\nG|Use kebbits as bait.\nG|I think your friend is pulling your leg, Meg.\nN|Dig a moat to trap them\nB|Stay on rock. They won't swim through that\nB|Dig underground to find them.\nB|You'll need a strong harpoon.\nT|Keep above the ground.\nT|Use fire to trap them in one place.\n" +
    "#Thieving\nE|Take the most valuable pieces with you.\nG|Don't get carried away by greed. Take what you need.\nN|Get a big, sturdy bag.\nB|Throw the loot into the sea for safekeeping.\nT|Ask the seadogs to carry it for you.\n" +
    "#Thieving\nE|Keep to the shadows and cover yourself in dark colours.\nG|Keep to the shadows.\nN|Move slowly and carefully.\nB|Move quickly.\nT|Make yourself stand out, they won't expect that.\n" +
    "#Runecrafting\nE|Research them carefully\nG|Try setting them off remotely.\nN|Warding spells are your friend.\nB|Shoot them first.\nT|Just use brute force.\n" +
    "#Herblore\nE|The jungles would be a good place to hide it.\nE|Look in the caves around Karamja.\nG|Sneak into the Legends' Guild and see if they know.\nG|Investigate sources of magic.\nN|Try the villages.\nN|Ask the local populace.\nB|Interrogate a broodoo victim.\nB|Interrogate a jogre.\nT|Look in the heart of the volcano.\nT|Try the broodoo ritual yourself for inspiration.\n" +
    "#Prayer\nE|I hear it's near Port Phasmatys.\nE|Work on your agility - it'll help.\nG|It's called the Ectofuntus, Meg.\nG|You'll need bones, pots and buckets.\nN|I think you should do more research before going.\nN|Morytania is dangerous. Be careful.\nB|Fungus grows in dark damp areas.\nB|Follow the ghasts. They'll lead you there.\nT|Beat up some werewolves. They'll tell you where it is.\nT|I hear it's in the heart of the vampyre city.\n" +
    "#Cooking\nE|They're just burnt lobsters, Meg.\nE|Someone was trying to trick you, Meg.\nG|Don't go Meg. They're lies.\nG|Were they very crispy?\nN|Bring a lobster net.\nN|Be prepared for disappointment.\nB|They'll hide in deep ponds.\nB|They'll be near ghasts.\nT|Sure, but first, let me cut your gems for you.\nT|I hear they taste particularly good.\n" +
    "#Magic\nE|Reconsider. Witches can be unpredictable.\nE|Make sure you have a strong magic defence.\nG|Bring some allies for support.\nG|Be stealthy; wait till she leaves.\nN|Offer to trade for it.\nN|Ask another witch first.\nB|Set her house on fire.\nB|Throw rocks at her.\nT|Research life as a toad.\nT|Poke her with spoons. Witches hate spoons.\n" +
    "#Magic\nE|Learn the enchantment that makes it work.\nG|You could help the needy during droughts.\nN|It could keep you hydrated in the desert.\nB|Put it on its side and gradually wear a fortress wall away.\nT|Handy for washing your clothes.\n" +
    "#Hunter\nE|Set a trap beforehand.\nG|Get yourself the homeground advantage.\nN|Get help.\nB|Rig explosive traps around you.\nT|Take the fight to them.\n" +
    "#Herblore\nE|Make sure your farming skill is up to scratch.\nE|Look out for the rare breeds of jadinko.\nG|Make juju hunter flasks.\nG|Bring hunter potions.\nN|Set up lots of the traps.\nN|Wear camouflage.\nB|Use normal box traps.\nB|Use deadfall traps.\nT|Cut off bits of the vine to snare them.\nT|That guy's crazy. Just trap the things with fire.\n" +
    "#Slayer\nE|Necromancy seems to have nasty side effects. Be careful.\nE|Don't go alone.\nG|Bring equipment that's good against undead.\nG|Keep teleport runes handy.\nN|Note what you find in a journal.\nN|Don't let zombies bite you.\nB|Drink any potions you find.\nB|Go in secret, so no one can betray you.\nT|If a zombie bites you, bite back.\nT|If you find anything, cast the biggest spell you can.\n" +
    "#Thieving\nE|Look for a vampyre resistance group.\nE|Find a good disguise.\nG|Have a quick escape plan.\nG|Vampyres are hard to fool.\nN|Reconsider. Vampyres are deadly.\nN|Create a distraction.\nB|Keep to the shadows.\nB|Stick to the high ground.\nT|It's dark, so bring a light source.\nT|Mark yourself with blood. They won't investigate you.\n" +
    "#Runecrafting\nE|Leave that to the Wizards' Tower.\nG|Try and bargain with one, rather than bind it\nN|Use the opposite element to control one.\nB|Force one into submission with violence.\nT|Invoke demonic magic.\n" +
    "#Summoning\nE|None that I know of.\nG|I guess unicorns, if you can tame them.\nN|I'd stick to going on foot.\nB|Bears.\nT|Fire elementals.\n" +
    "#Attack\nE|Treat the sword as an extension of your arm.\nG|Remember - the sword can do more than just stab.\nN|Keep the pointy end away from you.\nB|Throw it at your enemy.\nT|Juggle with them to scare your foe.\n" +
    "#Summoning\nE|Dogs are loyal and have great noses.\nG|Birds have a great range of travel.\nN|Ferrets can fit through small gaps.\nB|Cats are cute.\nT|Bears are cuddly and friendly.\n" +
    "#Thieving\nE|Get some good quality lockpicks.\nG|Locate alternative entrances.\nN|Make sure the place is empty first.\nB|Just kick the door down.\nT|Stand outside and shout 'fire!'\n" +
    "#Ranged\nE|Aim for their arm rather than their hand.\nG|Use a crossbow for greater accuracy.\nN|Try throwing daggers.\nB|Use a cannon.\nT|Aim for their head. That'll disarm them.\n" +
    "#Attack\nE|Keep your distance\nG|Carefully - treat the hook as a weapon\nN|Disarm him quickly.\nB|Put a cork on the hook.\nT|Ignore it. focus on their main weapon.\n" +
    "#Woodcutting\nE|A strong machete.\nG|A sharp sword.\nN|A good knife.\nB|A bladed whip.\nT|A warhammer.\n" +
    "#Firemaking\nE|Get a mining helmet.\nG|Speak to the dwarves for suggestions.\nN|Bring rope. Lots of rope.\nB|Oil yourself so you can slip through tight spaces.\nT|Dive straight in.\n" +
    "#Runecrafting\nE|Enchanting doesn't work that way.\nG|You'll need an enchantment spell.\nN|Go to the Wizards' Tower for help.\nB|Make a sword out of rune essence.\nT|Eat rune essence so you're in tune with your sword.\n" +
    "#Hunter\nE|Insects don't like smoke.\nG|When in a tent, cover the gaps with netting.\nN|Avoid walking near hives.\nB|Ignore them. You need to toughen up.\nT|Coat yourself in honey.\n" +
    "#Slayer\nE|Scope out the arena first.\nG|Research your opponent.\nN|Check your weapons first.\nB|Sabotage their weapon beforehand.\nT|Don't turn up.\n" +
    "#Slayer\nE|Bring a group.\nG|Arm yourself with powerful weapons.\nN|Bring food to heal.\nB|Undead are weak. Just charge in.\nT|Use the Bones to Peaches spell on the skeletons.\n" +
    "#Agility\nE|Find a path nearby.\nG|Scale the cliffside, not the fall.\nN|Tie a rope to the top to control your descent.\nB|Test a raft first.\nT|Close your eyes and hope.\n" +
    "#Prayer\nE|Find out why the ghosts are haunting it.\nG|Talk to an exorcist first.\nN|Bring salt.\nB|Go alone. You'll draw less attention.\nT|Stab the ghosts.\n" +
    "#Prayer\nE|Father Urhney is a skilled exorcist. He can help.\nE|Try to find out what's keeping the spirits trapped.\nG|An amulet of ghostspeak would be useful.\nG|Give them a proper burial.\nN|Salt and holy water.\nN|Get a priest to help.\nB|Just ignore them.\nB|Destroy them with magic.\nT|Give them to a demon.\nT|Bind them to serve you.\n" +
    "#Ranged\nE|Distract them with something shiny first.\nE|Try attacking from a distance. They tend to be melee-based.\nG|Goblins are fairly weak. Just be brave.\nG|Strike quickly, before they can strike you.\nN|A quick strike to the stomach often works.\nN|Make yourself seem intimidating.\nB|Work on counter-attacks, not attacks.\nB|Throw rocks at them.\nT|They're harmless. Just punch them.\nT|Just remember to mock Bandos.\n" +
    "#Slayer\nE|Fighting rock crabs is good combat training.\nE|Their rock forms can be identified, if you're observant.\nG|Pickaxes are good against them.\nG|They have a strong defence. Be careful.\nN|Use a dwarf multicannon.\nN|Take care not to get too injured.\nB|Disguise yourself as a rock.\nB|Just run away screaming.\nT|They spit fire!\nT|They're highly poisonous. FLEE, MEG!\n" +
    "#Slayer\nE|I hear there is a magic weapon that will help.\nE|Attack from a distance.\nG|Try diplomacy first.\nG|Bring food. They can hit hard.\nN|They're scary, but mortal.\nN|Try and kill them before they change.\nB|Stamp on their tail.\nB|Swat them on the nose.\nT|Poke them repeatedly; they can't fight if angry.\nT|Insult their fur.\n" +
    "#Slayer\nE|Vampyres don't like silver.\nE|I hear you need a potion to defeat them.\nG|Be careful. Vampyres are very dangerous.\nG|You'll need a special weapon to defeat them.\nN|Stake them.\nN|I think holy water is effective.\nB|Drag them into sunlight.\nB|Coat your sword in garlic first.\nT|Just stab them.\nT|Fight them bare-handed.\n" +
    "#Ranged\nE|You'd be better off using a crossbow.\nG|Arrows aren't generally good at holding weight.\nN|Use the arrows as footholds instead.\nB|Aim the arrow at a downward angle.\nT|Use flaming arrows, so they melt into the surface a bit.\n" +
    "#Dungeoneering\nE|Be careful. A weapon like that will be guarded.\nE|Be sure you can trust your friend.\nG|Saradomin's followers may know something.\nG|Ask around. Rumours are occasionally based on truth.\nN|Look in ancient ruins.\nN|It's just a rumour.\nB|Leave it to me. I'll find it eventually.\nB|It's clearly a lie, Meg. Why bother looking?\nT|Keep buying flails until you get the right one.\nT|Never liked flails. Too easy to hit yourself.\n" +
    "#Thieving\nE|Study them carefully. It'll come back to you.\nG|Work backwards.\nN|Set them off remotely.\nB|Just charge through.\nT|Move house.\n" +
    "#Hunter\nE|Watch out for ghasts.\nE|Look for trees that aren't rotten.\nG|Don't wear anything flammable.\nG|Set several traps to be sure.\nN|Wear camouflage.\nN|Be very quiet to avoid spooking them.\nB|Box traps might work.\nB|Deadfall traps are good for them.\nT|Stamp on them.\nT|Douse yourself in oil first.\n" +
    "#Prayer\nE|Ask Father Aereck about a ghost for a good reward.\nE|The Salvation auras can be useful.\nG|Imps drop ashes that you can scatter.\nG|There are lots of goblins to get bones from.\nN|Just buy a bunch of bones from the Grand Exchange.\nN|Sit in on local sermons.\nB|Just keep praying. You'll improve it that way.\nB|Duel random people to show your devotion to the gods.\nT|Ignore Lumbridge and go straight to the frost dragons.\nT|Greater demons drop some nice ashes. You can defeat them, right?\n" +
    "#Fishing\nE|I think you should start smaller, Meg.\nE|Why not get some help on this one?\nG|I'm not sure that fish is real, Meg.\nG|Be calm and don't panic.\nN|Make sure the rod is secure.\nN|Find out what the right bait is first.\nB|If you fire several harpoons at once, you won't miss.\nB|Buy the adventurer a drink and bring him along.\nT|Wrestle it into submission. You'll be legendary!\nT|Dam up the river so it can't escape!\n" +
    "#Thieving\nE|You might want to have a chat with the Thieves' Guild.\nE|The castle's fairly open. Scope it out first.\nG|A disguise is always a good idea.\nG|Take the wealth in small batches over time.\nN|Plan carefully.\nN|Go at night, when it's easier to stay unseen.\nB|Enlist the goblins for help.\nB|Dig under the tower from the swamp.\nT|No subtlety. Attack directly - show them who's boss!\nT|Borrow a cannon and lay siege to the castle as a distraction.\n" +
    "#Ranged\nE|Take them out from a distance.\nE|Run past them and bring food.\nG|Collect their bones for summoning.\nG|Wear strong armour to reduce the danger.\nN|You could just teleport past it.\nN|Travel in a group.\nB|Go at night, for stealth.\nB|Dig through the mountain.\nT|Remember - wolves always work alone.\nT|Cover yourself in blood first. That scares them.\n" +
    "#Runecrafting\nE|Find the Wizard's Tower. They can help.\nE|Ask the wizards about the Runespan.\nG|Get yourself a wicked hood.\nG|Goblins sometimes drop talismans when slain. Worth a look.\nN|You'll need some essence first.\nN|The air altar can be found east of the River Lum.\nB|Runecrafting is lame. Magic is for sissies.\nB|Fletching is more useful.\nT|Find runes, then just paint the symbols on rocks.\nT|Crush runes up and use the dust.\n" +
    "#Dungeoneering\nE|I think you should let Thok overcome his own fears.\nE|I think its natural enemy is the heim crab.\nG|Set a trap for it.\nG|I suspect it's not as scary as he claims.\nN|Sneak up on it from behind.\nN|Use stealth; stick to the shadows.\nB|Collapse the dungeon on it.\nB|Go alone; just be quick.\nT|Just tell Thok that he's a big coward.\nT|Wait it out. Ferrets don't live that long.\n" +
    "#Magic\nE|They're weak against magic.\nE|Bring a White Knight as assistance.\nG|Keep your nerves steady.\nG|You can heal at the Monastery.\nN|Make sure you're wearing good armour.\nN|Bring food to heal.\nB|Wear black armour to infiltrate them.\nB|Bring a Black Knight as assistance.\nT|Dress in white armour so they don't suspect you.\nT|Use ranged weapons. They're weak against those.\n" +
    "#Woodcutting\nE|Bring a machete. The jungle is thick.\nE|Might want to find someone who's been there before.\nG|Mark where you've been so you can get back.\nG|Arm yourself. The locals might not be friendly.\nN|Remember that it's just a rumour, Meg.\nN|Climb a tree to get a better view of the area.\nB|If attacked, wave your swords around while screaming.\nB|Flies can be a great source of nutrition.\nT|Ask a monkey.\nT|Tame a deathwing and survey the area from above.\n" +
    "#Fishing\nE|Just remember that they're harmless.\nE|Use a small fishing net and grab the shrimp quickly.\nG|Cook them immediately so they don't freak you out.\nG|Wear gloves so you don't have to feel them.\nN|Just look for the right fishing spots.\nN|Get a net and shove it in the water.\nB|Get a fishing rod and be patient.\nB|Just buy them from the Grand Exchange.\nT|Harpoon the shrimp, they'll know who's boss.\nT|Stick your head in and catch them with your teeth.\n" +
    "#Hunter\nE|Set up several traps and be patient.\nE|Watch for favoured flight patterns.\nG|Disguise the traps as best you can.\nG|Consider putting seeds on the trap.\nN|Not really my area of expertise.\nN|Wear camouflage.\nB|Shoot them down with an arrow.\nB|Sing at them. Birds love song.\nT|Just whack the birds with a whip.\nT|Set fire to the trees so they can only land on your trap.\n" +
    "#Hunter\nE|Try baiting the traps with beads.\nE|Watch out for their teleports.\nG|Be quiet and keep still. You don't want to spook them.\nG|Make sure you're using the right trap.\nN|Dress as a Zamorakian monk so they don't suspect you.\nN|You'll need to use a magic box.\nB|Shoot at them first.\nB|Throw beads at them to confuse them.\nT|Use a simple box trap. They're cheaper.\nT|Use holy water on them. They hate it.\n" +
    "#Slayer\nE|Whittle them down with cannonfire.\nG|Don't panic. Undead are slow and stupid.\nN|Blast them with magic.\nB|Sneak past them.\nT|Fight them with fists - they're weak to fists.\n" +
    "#Prayer\nE|Most demons can be banished. Find the ritual.\nG|Look for a binding spell.\nN|Be strong in your faith.\nB|Stabbing it normally works.\nT|Make a bargain with it. Should be fine.\n" +
    "#Hunter\nE|Wear polar camouflage.\nE|Stock up on hunter potions.\nG|Keep quiet. They're easily spooked.\nG|Just keep focused and prepare to lose a few.\nN|Wear white clothes to blend in.\nN|Stand upwind so they can't smell you.\nB|Blast the burrows with Earth Bolt spells.\nB|Block up all the exits with snow.\nT|Smoke them out with fire.\nT|Shout loudly to scare them.\n" +
    "#Prayer\nE|Demons lie, Meg. Don't trust them.\nE|Make sure you have a powerful wizard with you.\nG|Whatever you do, don't agree to anything.\nG|Be careful.\nN|Make sure it's bound properly.\nN|I think you should reconsider.\nB|Summon it in a church.\nB|Make sure you summon it in a place of power.\nT|Don't bother with any sort of binding circle.\nT|Offer it your soul for information.\n" +
    "#Thieving\nE|Trick the guard.\nE|Fake illness, then escape.\nG|Dig a tunnel with a spoon.\nG|Hide under the bed.\nN|Just don't get caught.\nN|Remember that it's just a rumour, Meg.\nB|Knock the guard out.\nB|Kill the guard.\nT|Kill everyone you meet.\nT|Set fire to the castle.\n" +
    "#Prayer\nE|Father Aereck is a good man to talk to.\nE|Talk to Xenia in the nearby graveyard. She is an experienced adventurer.\nG|Kill some goblins and bury their bones.\nG|Be quiet and polite. It is a place of worship, after all.\nN|Be sure to go in disguise.\nN|It's just a church, Meg. You can just go inside.\nB|Go find Father Urhney instead. He loves company.\nB|Break in through the northern window.\nT|It's secretly a Zamorakian temple.\nT|Be careful - Father Aereck is clearly a Mahjarrat.\n" +
    "#Fishing\nE|Use a long rod.\nE|Don't go in the water.\nG|Set a trap using meat and a net.\nG|Find a way to trap them.\nN|Wear sturdy armour.\nN|Use meat as bait.\nB|Use fish as bait.\nB|Use fruit as bait.\nT|Wade in and catch them close up.\nT|Dive in and catch them with your teeth.\n" +
    "#Prayer\nE|Find a loophole in the contract.\nG|Bind the demon to your service.\nN|Destroy the demon.\nB|You can't.\nT|Bargain with other demons.\n" +
    "#Thieving\nE|I've heard it's near a furnace.\nE|Get on Darren Lightfinger's good side first.\nG|Accept any 'job' offers the guild offers you.\nG|Consider the art of disguise.\nN|Steal from stalls to get some practice.\nN|Practice pickpocketing on H.A.M members. They deserve it.\nB|Remember - don't touch the handkerchiefs.\nB|Ask the guards about where to find them.\nT|Remember to shout at people before you rob them.\nT|The secret to stealth is to drink a lot of alcohol first.\n" +
    "#Woodcutting\nE|Just use one of the trees at the canoe station.\nG|Try oak - sturdy, and not too expensive.\nN|A tree's a tree.\nB|Cursed magic trees, for extra speed.\nT|Bloodwood trees. You can get to those, right?\n" +
    "#Runecrafting\nE|Very special circumstances create rune essence. Look for those.\nE|Honestly, it's unlikely you'll find one.\nG|Rune essence absorbs magic. Look for weird side effects.\nG|Look for high concentrations of magic.\nN|Maybe the Wizards' Tower can help.\nN|I'd consider a more fruitful quest.\nB|Just take control of an existing mine.\nB|Give up, Meg.\nT|Just keep saying different words in the teleport spell.\nT|Randomly teleport around the world.\n" +
    "#Slayer\nE|Sneak up on them from behind.\nE|Draw a line in front of their beaks to hypnotise them.\nG|Try not to show fear\nG|Strike quickly. It gets it over with faster.\nN|Just stab them.\nN|Throw a rock at them first.\nB|Make loud noises to scare them.\nB|Chase them around to tire them out.\nT|Use fire to trap them.\nT|Summon the Evil Chicken. He'll take care of them.\n" +
    "#Hunter\nE|Make sure you're good enough at summoning to keep one.\nE|A squirrel is for life, not just for a whim.\nG|Nuts. I don't really need to say more.\nG|Be careful. They can scratch.\nN|Make sure your trap is secure.\nN|Be patient and use more than one trap.\nB|Try to catch one with a hawk.\nB|Scare them into submission first.\nT|Deadfall traps work better than nets.\nT|Pitfall traps are the best way.\n" +
    "#Dungeoneering\nE|Tread carefully. It might be a trap.\nE|It's never wise to pay money up-front like that.\nG|You need to find better friends.\nG|I think your 'friend' may have tricked you.\nN|Bring a light source.\nN|Scout out the area first.\nB|Use fire. Golems hate fire.\nB|You're an idiot, Meg.\nT|Fold up your shopping list and put it into the golem's head.\nT|I'll tell you for 1000 gold...\n" +
    "#Defence\nE|Something durable, which still allows freedom of movement.\nG|Something that provides all-round protection.\nN|Something comfortable.\nB|A full suit of chainmail.\nT|A full suit of heavy plate.\n" +
    "#Fishing\nE|Fish are generally easy to come by.\nG|Rabbits are tasty and easy to catch.\nN|Fruits and berries.\nB|Rats are numerous.\nT|Crocodiles.\n" +
    "#Ranged\nE|Use a powerful bow to take him out from range.\nG|Take him out from the shadows.\nN|Use a ranged weapon.\nB|Fight Fire Bolts with Fire bolts.\nT|Run up and punch him in the face.\n" +
    "#Prayer\nE|Only you can answer that.\nG|Why follow just one god?\nN|Don't follow any.\nB|Depends on what your favourite colour is.\nT|Defy all of the gods, openly.\n";
  // Parse into: MEG_ANSWERS[normText] -> [[questionIdx, rank]...], rank 0=Excellent..4=Terrible.
  const MEG_RANK = { E: 0, G: 1, N: 2, B: 3, T: 4 };
  const MEG_RANK_NAME = ['Excellent', 'Good', 'Neutral', 'Bad', 'Terrible'];
  const megNorm = s => String(s).toLowerCase().replace(/[‘’]/g, "'")
                        .replace(/\s+/g, ' ').replace(/[.!?]+$/, '').trim();
  const MEG_ANSWERS = {};
  {
    let qi = -1;
    for (const line of MEG_QA_RAW.split('\n')) {
      const t = line.trim();
      if (!t) continue;
      if (t[0] === '#') { qi++; continue; }
      const rank = MEG_RANK[t[0]];
      if (rank === undefined || t[1] !== '|') continue;
      const key = megNorm(t.slice(2));
      (MEG_ANSWERS[key] = MEG_ANSWERS[key] || []).push([qi, rank]);
    }
  }
  // Tab-independent tick: when the option dialogue (group 1188) shows Meg answers, box the
  // best-rated one. Shares the single uiHighlight box with the Dungeoneering option highlighter;
  // Meg questions and Daemonheim dialogs cannot be open at the same time.
  // MUST stay <= 64 ids: InterfaceCompsJson rejects a longer list outright ("{}"). Live comps for
  // a 4-option Meg dialogue are 6 / 33 / 35 / 37, so 0-63 covers every observed row.
  const MEG_OPT_COMPS = Array.from({ length: 64 }, (_, i) => i).join(',');
  let megHlLast = '';
  function megHlClear() {
    if (!megHlLast) return;
    megHlLast = '';
    try { rtxData.sync('overlay.uiHighlight', 0, 0, 0, 0); } catch (e) {}
  }
  function megAnswerTick() {
    try {
      if (!bridge() || !bridge().interfaceComps) return;
      const d = JSON.parse(bridge().interfaceComps(myPid(), 1188, MEG_OPT_COMPS) || '{}');
      if (!d || !d.open || !Array.isArray(d.comps)) { megHlClear(); return; }
      // Visible answer options: text comps wide enough to be rows, numbers filtered out.
      // Comps for the rune-golem question are 6/33/35/37, each w=344.
      const opts = [];
      for (const c2 of d.comps) {
        if (!c2.text || !(c2.w > 40)) continue;
        const t = String(c2.text).trim();
        if (/^\d+\.?$/.test(t) || /^select an option/i.test(t) || /^please select/i.test(t)) continue;
        opts.push({ c: c2, key: megNorm(t) });
      }
      if (opts.length < 2) { megHlClear(); return; }
      // Identify the question: most visible options matched (>= 2, so a single shared answer text can
      // never mis-pick a question).
      const score = {};
      for (const o of opts) for (const [qi] of (MEG_ANSWERS[o.key] || [])) score[qi] = (score[qi] || 0) + 1;
      let q = -1, best = 1;
      for (const k in score) if (score[k] > best) { best = score[k]; q = +k; }
      if (q < 0) {   // options visible but none matched the table -> say so, don't fail silently
        megHlClear();
        dwSetSub('meg', 'Unknown question (' + opts.length + ' options) - not in the answer table');
        return;
      }
      let target = null, tr = 99;
      for (const o of opts) {
        for (const [qi, rank] of (MEG_ANSWERS[o.key] || []))
          if (qi === q && rank < tr) { tr = rank; target = o; }
      }
      if (!target) { megHlClear(); return; }
      const c2 = target.c;
      // ALWAYS report the answer in the panel; the in-game box needs absolute screen coords, which
      // only resolve when the dialogue's panel origin is published.
      dwSetSub('meg', 'Best answer (' + MEG_RANK_NAME[tr] + '): ' + String(c2.text).trim()
                      + (d.hasAbs ? '' : ' [no screen origin - panel only]'));
      if (!d.hasAbs || !bridge().uiHighlight) { megHlClear(); return; }
      const rect = c2.x + ',' + c2.y + ',' + c2.w + ',' + c2.h;
      if (rect !== megHlLast) { rtxData.sync('overlay.uiHighlight', c2.x, c2.y, c2.w, c2.h); megHlLast = rect; }
    } catch (e) {}
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { DW_IDS, dwAvail, dwNextResets, dwNotifyAny, dwVosState, fetchDailies, megAnswerTick });
registerTab({ id: 'dailies', render: renderDailies, open: function () { fetchDailies(true); } });
})();
