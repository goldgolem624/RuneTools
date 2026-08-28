// RuneToolsX panel: Abilities (live action-bar slots, keybinds, cooldowns).
// Spliced inline into client.html; bare classic script sharing one global scope.

  // Main bar = interface group 1430; secondary bars 1670-1673 are gated on varbits
  // 29138-29141 (>0 = visible, value = displayed preset). s.cd = the cooldown text the
  // engine prints on the slot ("" = none); s.castable false = engine-greyed.
  let abarData = null, abarFetching = false, abarSig = '';
  const ABAR_GATE = { 1: 29138, 2: 29139, 3: 29140, 4: 29141 };
  // Adrenaline = varplayer 679 (0..1000 = 0..100.0%). Doubles as special-attack energy
  // while a spec weapon is wielded.
  const AB_ADREN_VARP = 679;
  // Live ability configs from the cache (bridge abilityConfigs -> struct index 22, keyed by
  // name: {t:tier, d:description, u:unlock}). AB_TIER below is only a bootstrap fallback.
  let abCfg = null, abCfgTried = false;
  async function abCfgLoad() {
    if (abCfg || abCfgTried || !bridge() || !bridge().abilityConfigs) return;
    abCfgTried = true;
    try {
      const d = JSON.parse(await bridge().abilityConfigs() || '{}');
      if (d && Object.keys(d).length) abCfg = d; else abCfgTried = false;   // empty = cache not open yet, retry
    } catch (e) { abCfgTried = false; }
  }
  function abInfo(name) { return (abCfg && abCfg[name]) || null; }
  // Bootstrap tier fallback until abCfg loads; the cache value wins in abTier().
  const AB_TIER = {"Adaptive Strike":1,"Aggression":7,"Aggressive Stance":7,"Aimed Shot":5,"Anticipation":1,"Asphyxiate":2,"Assault":2,"Backhand":1,"Balance by Force":5,"Balanced Stance":7,"Balanced Strike":4,"Barge":1,"Barricade":4,"Bash":1,"Berserk":4,"Binding Shot":1,"Bladed Dive":1,"Bloat":2,"Blood Siphon":2,"Blood Tendrils":2,"Bombardment":2,"Cease":7,"Chain":1,"Chaos Roar":1,"City of Um Teleport":2,"Cleave":1,"Combat Monolith":1,"Combust":1,"Command Phantom Guardian":2,"Command Putrid Zombie":2,"Command Skeleton Warrior":2,"Command Vengeful Ghost":2,"Concentrated Blast":1,"Conjure Phantom Guardian":2,"Conjure Putrid Zombie":2,"Conjure Skeleton Warrior":2,"Conjure Undead Army":2,"Conjure Vengeful Ghost":2,"Corruption Blast":2,"Corruption Shot":2,"Darkness":2,"Dazing Shot":1,"Deadshot":4,"Death Skulls":4,"Death's Swiftness":4,"Debilitate":3,"Decimate":1,"Deep Burn":5,"Deep Impact":2,"Defensive Stance":7,"Demon Slayer":7,"Demoralise":1,"Destroy":2,"Detonate":2,"Devotion":3,"Dismember":2,"Dive":7,"Divert":1,"Dragon Breath":1,"Dragon Slayer":7,"Eat Food":7,"Escape":7,"Essence of Finality":5,"Finger of Death":2,"Flurry":2,"Fragmentation Shot":1,"Freedom":1,"Frenzy":4,"Fury":1,"Galeshot":1,"Get Over Here!":5,"Golden Touch":7,"Greater Barge":1,"Greater Bone Shield":2,"Greater Chain":1,"Greater Concentrated Blast":1,"Greater Dazing Shot":1,"Greater Death's Swiftness":4,"Greater Flurry":2,"Greater Fury":1,"Greater Ricochet":1,"Greater Sonic Wave":1,"Greater Sunshine":4,"Guthix's Blessing":4,"Havoc":1,"Horror":2,"Hurricane":2,"Ice Asylum":4,"Imbue: Shadows":2,"Immortality":4,"Impact":1,"Incendiary Shot":4,"Incite":7,"Ingenuity of the Humans":7,"Invoke Death":2,"Invoke Lord of Bones":2,"Kuradal's Favour":7,"Lesser Bone Shield":2,"Life Transfer":2,"Limitless":7,"Living Death":4,"Magma Tempest":2,"Magma Tempest (Targeted)":2,"Magma Tempest - Area of effect damage":2,"Massacre":4,"Metamorphosis":4,"Meteor Strike":4,"Natural Instinct":4,"Omnipower":4,"Onslaught":4,"Overpower":4,"Phantom Strike.":5,"Piercing Shot":1,"Preparation":1,"Provoke":1,"Pulverise":4,"Punish":1,"Quake":2,"Rapid Fire":2,"Reflect":3,"Regenerate":7,"Rejuvenate":4,"Rend":1,"Reprisal":3,"Resonance":1,"Revenge":3,"Revolution":1,"Ricochet":1,"Rout":2,"Runic Charge":7,"Sacrifice":1,"Salt the Wound":2,"Sanguine Charge":2,"Shadow Tendrils":2,"Shatter":3,"Shock":1,"Siphon":1,"Slaughter":2,"Slayer's Insight":7,"Slice":1,"Smash":1,"Smoke Tendrils":2,"Snap Shot":2,"Snipe":2,"Sonic Wave":1,"Soul Sap":1,"Soul Strike":2,"Spectral Scythe":2,"Split Soul":2,"Storm Shards":1,"Sunfall Slam":5,"Sunshine":4,"Surge":7,"Tempest of Armadyl":5,"Threads of Fate":2,"Touch of Death":1,"Transfigure":4,"Tsunami":4,"Tuska's Wrath":1,"Undead Slayer":7,"Ungael Teleport":2,"Unload":4,"Unsullied":7,"Volley of Souls":2,"Weapon Special Attack":5,"Wild Magic":2};
  const AB_TIER_META = { 1: { n: 'Basic', c: '#7f9fbf' }, 2: { n: 'Threshold', c: '#e0b34c' }, 3: { n: 'Defensive', c: '#4cc0c0' }, 4: { n: 'Ultimate', c: '#e06c6c' }, 5: { n: 'Special', c: '#c98cf0' }, 7: { n: 'Utility', c: '#67c07a' } };
  // Adrenaline required to CAST, by tier: threshold >= 500 (50%), ultimate >= 1000 (100%).
  const AB_ADREN_REQ = { 2: 500, 4: 1000 };
  function abTier(name) { const i = abInfo(name); return (i && i.t) || AB_TIER[name] || 0; }
  // Ability cooldowns, from the live cache via bridge abilityConfigs:
  //   "c" = cooldown in GAME TICKS (param 2796, 0.6s each)
  //   "i" = the ability id the slot carries at widget+0x188 (param 2802)
  //   "v" = the cooldown-clock varc pair [castClock, readyClock] in CLIENTCLOCK cycles
  //         (50/s), WRITTEN ONCE at cast and then static -- the countdown derives from
  //         the clock, so the pair does not tick.
  let abCd = null;
  function abCdMap() {
    if (abCd || !abCfg) return abCd;
    const map = {};
    for (const n in abCfg) {
      const c = abCfg[n];
      if (!c.i) continue;                      // older launcher build: no cooldown fields
      const rec = { c: c.c || 0 };
      if (c.v && c.v.length === 2 && !c.g) rec.v = c.v;
      if (rec.c || rec.v) map[c.i] = rec;
    }
    if (Object.keys(map).length) abCd = map;
    return abCd;
  }
  // Remaining cooldown in whole seconds from the varc clock pair, or null when the cooldown
  // is not provably running. The engine clock is MILLISECONDS; CLIENTCLOCK cycles are 50/s
  // -> cycles = ms / 20. Rounding matches the game: remaining = (dur + 50 - elapsed) / 50.
  function abCdLive(s) {
    const r = abCdMap() && abCd[s.id], d = abarData;
    if (!r || !r.v || !d || !d.vc || !d.clock) return null;
    const a = d.vc['5:' + r.v[0]], b = d.vc['5:' + r.v[1]];
    if (typeof a !== 'number' || typeof b !== 'number' || a <= 0 || b <= a) return null;
    // The varc stamps are in CLIENTCLOCK cycles, so use that counter directly when the host
    // reports it. clock/20 was always an approximation of this exact value, derived from a
    // hardcoded module offset; it stays as the fallback for an older host.
    const cyc = (d.cycles > 0) ? d.cycles : Math.floor(d.clock / 20);
    const dur = b - a, el = cyc - a;
    if (el < 0 || el >= dur || dur > 360000) return null;
    return Math.floor((dur + 50 - el) / 50);
  }
  async function fetchAbilities() {
    if (!bridge() || abarFetching) return; abarFetching = true;
    abCfgLoad();
    try {
      let bars = [], clock = 0;
      try { const j = JSON.parse(await bridge().actionBar(myPid())); bars = j.bars || []; clock = j.clock || 0; } catch (e) {}
      // varc-int snapshot for the cooldown clock pairs (keys are scope-prefixed "5:<id>")
      let vc = null;
      try { if (bridge().varcsDumpAll) vc = JSON.parse(await bridge().varcsDumpAll(myPid()) || 'null'); } catch (e) {}
      await ensureVbMap();
      const vps = new Set([AB_ADREN_VARP]);
      for (const bi in ABAR_GATE) { const r = storageVbMap && storageVbMap[ABAR_GATE[bi]]; if (r) vps.add(r.varp); }
      let vp = {}; if (vps.size && bridge().varps) { try { vp = JSON.parse(await bridge().varps(myPid(), [...vps].join(','))); } catch (e) {} }
      const preset = {}; for (const bi in ABAR_GATE) preset[bi] = readVb(ABAR_GATE[bi], vp) || 0;
      const adren = (vp[AB_ADREN_VARP] !== undefined) ? vp[AB_ADREN_VARP] : null;
      abarData = { bars, preset, clock, adren, vc };
    } finally { abarFetching = false; }
    paneRun('abilities', renderAbilities);
  }
  // Ability slots render the cache sprite whose id == the ability id (cache idx 8).
  function attachAbilityIcon(el, s) {
    if (s.item) { const url = resolveIcon(s.item); if (url) { setIconBg(el, url); return; } }
    const sid = s.id; if (!sid) return;
    const cached = SPRITES.get(sid);
    if (cached) { setIconBg(el, cached); return; }
    el.dataset.spr = sid;
    if (!bridge() || !bridge().sprite || SPRITE_PENDING.has(sid)) return;
    SPRITE_PENDING.add(sid);
    (async () => {
      try {
        const url = await bridge().sprite(sid); SPRITE_PENDING.delete(sid);
        if (url) { SPRITES.set(sid, url); document.querySelectorAll('.ab-icon[data-spr="' + sid + '"]').forEach(n => setIconBg(n, url)); }
      } catch (e) { SPRITE_PENDING.delete(sid); }
    })();
  }
  function renderAbilities() {
    const c = $('content');
    let wrap = $('abWrap');
    if (!wrap) { c.innerHTML = ''; wrap = document.createElement('div'); wrap.id = 'abWrap'; wrap.className = 'pane stor-wrap'; c.appendChild(wrap); abarSig = ''; }
    const d = abarData;
    if (!d || !d.bars.length) { wrap.innerHTML = '<div class="stor-empty">No action bar data (open the game / be in-world).</div>'; abarSig = ''; return; }
    const MOD = ['', 'Shift', 'Ctrl', 'Alt'], BARNAME = ['Main bar', 'Bar 2', 'Bar 3', 'Bar 4', 'Bar 5'];
    const adren = d.adren;
    const adrenPct = (adren != null) ? Math.floor(adren / 10) : null;
    // Name the adrenaline shortfall when the ability tier (cache param 2799) and varp 679
    // explain the greying; other gates (level / weapon style / target / lock) are not asserted.
    const abReason = (s) => {
      const req = AB_ADREN_REQ[abTier(s.name)];
      if (req && adren != null && adren < req) return 'needs ' + (req / 10) + '%';
      return 'unavailable';
    };
    // Slot state priority: s.cd text -> the varc cooldown clock (covers the in-game icon text
    // being disabled) -> engine-greyed -> ready.
    const fmtSec = (n) => n > 59 ? Math.floor(n / 60) + ':' + ('0' + (n % 60)).slice(-2) : n + 's';
    const slotState = (s) => {
      if (s.cd) return { cls: 'ab-cd', txt: s.cd.includes(':') ? s.cd : s.cd + 's' };
      const live = abCdLive(s);
      if (live != null) return { cls: 'ab-cd', txt: fmtSec(live) };
      return s.castable === false ? { cls: 'ab-unavail', txt: abReason(s) } : { cls: 'ab-ready', txt: 'ready' };
    };
    const slotTip = (s, st) => {
      const t = abTier(s.name), tm = AB_TIER_META[t], info = abInfo(s.name);
      const lines = [s.name + (tm ? '  [' + tm.n + ']' : ''), 'Ability id ' + s.id + (s.item ? ' · item ' + s.item : '')];
      if (info && info.d) lines.push(info.d);
      lines.push('State: ' + st.txt);
      const req = AB_ADREN_REQ[t];
      if (req) lines.push('Needs ' + (req / 10) + '% adrenaline' + (adrenPct != null ? ' (have ' + adrenPct + '%)' : ''));
      if (info && info.u) lines.push('Unlock: ' + info.u);
      lines.push('Enabled byte (widget+0x80): ' + (s.en !== undefined ? s.en : '?') +
                 (s.en === 255 ? ' (castable)' : ' (greyed by the game)'));
      const cdMap = abCdMap();
      const cdRec = cdMap && abCd[s.id];
      if (cdRec && cdRec.c) {
        const sec = cdRec.c * 0.6;
        lines.push('Cooldown: ' + (sec % 1 ? sec.toFixed(1) : sec) + 's (' + cdRec.c + ' game ticks)');
      }
      const live = abCdLive(s);
      // The idle case prints its raw inputs: the two varc stamps and both clock candidates.
      // "Freedom says ready while on cooldown" could be a missing varc in the dump, a stale
      // stamp, or our CLIENTCLOCK running past the stamps; the numbers tell which.
      const cdRaw = () => {
        if (!cdRec || !cdRec.v || !d) return '';
        const a2 = d.vc ? d.vc['5:' + cdRec.v[0]] : undefined;
        const b2 = d.vc ? d.vc['5:' + cdRec.v[1]] : undefined;
        return String.fromCharCode(10) + '  raw: cast=' + a2 + ' ready=' + b2 + ' cycles=' + d.cycles + ' clock/20=' + (d.clock > 0 ? Math.floor(d.clock / 20) : '?');
      };
      const cdLine = s.cd ? 'On cooldown: ' + s.cd + (s.cd.includes(':') ? '' : 's') + ' left (printed on the icon)'
                   : live != null ? 'On cooldown: ' + fmtSec(live) + ' left (varc ' + cdRec.v[0] + '/' + cdRec.v[1] + ' clock)'
                   : cdRec && cdRec.v ? 'Not on cooldown (clock varc ' + cdRec.v[0] + '/' + cdRec.v[1] + ' idle)' + cdRaw()
                   : null;
      if (cdLine) lines.push(cdLine);
      if (s.key) lines.push('Keybind: ' + (s.mod ? MOD[s.mod] + '+' : '') + s.key);
      return lines.join('\n');
    };
    const abAdrenHtml = (adren) => {
      if (adren == null) return '';
      const disp = (adren % 10) ? (adren / 10).toFixed(1) : String(adren / 10);
      const fill = Math.min(100, adren / 10);
      return '<div style="display:flex;align-items:center;gap:8px;padding:4px 2px 8px">'
        + '<span style="font-size:11px;color:var(--text-dim,#999)">Adrenaline</span>'
        + '<div style="flex:1;height:8px;background:var(--bg-2,#1c1c22);border-radius:4px;position:relative;overflow:hidden">'
        + '<div style="position:absolute;left:0;top:0;bottom:0;width:' + fill + '%;background:linear-gradient(90deg,#e0b34c,#e06c6c)"></div>'
        + '<div style="position:absolute;left:50%;top:0;bottom:0;width:1px;background:rgba(255,255,255,.35)"></div>'
        + '</div>'
        + '<span style="font-size:12px;color:var(--text,#ddd);min-width:40px;text-align:right">' + disp + '%</span></div>';
    };
    // Five combat buff/debuff timers that live in single absolute-expiry varc-ints rather than
    // the cast/ready pairs abCdLive handles (CS2: setter script4252, getter script11073; decode
    // confirmed in script10886): remaining ticks = 1 + (v - CLIENTCLOCK)/50. The varcs are NEVER
    // cleared by the game, so an expired value must be rejected, not displayed.
    const AB_TIMERS = [
      [8477, 'Light Strike', true], [8478, 'Lord of Light', true], [8479, 'Avernic Rampage', false],
      [8480, 'Sliver of Edicts', false], [8481, 'Sliver of Edicts cooldown', true],
    ];
    const abTimersHtml = () => {
      if (!d.vc) return '';
      const cyc = (d.cycles > 0) ? d.cycles : (d.clock > 0 ? Math.floor(d.clock / 20) : 0);
      if (!cyc) return '';
      let rows = '';
      for (const [id, name, isCd] of AB_TIMERS) {
        const v = d.vc['5:' + id];
        if (typeof v !== 'number' || v <= 0) continue;
        const secs = (1 + (v - cyc) / 50) * 0.6;          // ticks are 0.6s
        if (v - cyc <= 0 || secs > 3600) continue;         // expired, or a torn read
        const t = secs >= 60 ? Math.floor(secs / 60) + ':' + ('0' + Math.round(secs % 60)).slice(-2) : Math.round(secs) + 's';
        rows += '<span style="font-size:11px;padding:2px 9px;border-radius:999px;border:1px solid '
          + (isCd ? 'rgba(224,108,108,.5)' : 'rgba(77,210,138,.5)') + ';color:' + (isCd ? '#e06c6c' : '#4dd28a') + '">'
          + name + ' ' + t + '</span>';
      }
      return rows ? '<div style="display:flex;flex-wrap:wrap;gap:6px;padding:2px 2px 8px">' + rows + '</div>' : '';
    };
    const ensureTimers = () => {
      const html = abTimersHtml();
      let el = wrap.querySelector('#ab-timers');
      if (!html) { if (el) el.remove(); return; }
      if (!el) {
        el = document.createElement('div'); el.id = 'ab-timers';
        const ad = wrap.querySelector('#ab-adren');
        wrap.insertBefore(el, ad ? ad.nextSibling : wrap.firstChild);
      }
      el.innerHTML = html;
    };
    const ensureAdren = () => {
      const html = abAdrenHtml(d.adren);
      let el = wrap.querySelector('#ab-adren');
      if (!html) { if (el) el.remove(); return; }
      if (!el) { el = document.createElement('div'); el.id = 'ab-adren'; wrap.insertBefore(el, wrap.firstChild); }
      el.innerHTML = html;
    };
    const vis = d.bars.filter(b => b.bar === 0 || d.preset[b.bar] > 0);
    const byBS = {}; vis.forEach(b => b.slots.forEach(s => { byBS[b.bar * 100 + s.slot] = s; }));
    // Layout-only signature (not live cd/castable): the same layout updates state in place and
    // never rebuilds the DOM, so the list cannot flicker or lose scroll on a poll.
    const sig = vis.map(b => b.bar + ':' + (d.preset[b.bar] || 0) + ':' + b.slots.map(s => s.slot + '/' + s.id + '/' + s.item + '/' + s.key + '/' + s.mod).join(',')).join(';');
    if (sig === abarSig) {
      wrap.querySelectorAll('.ab-cdcell').forEach(el => {
        const s = byBS[+el.dataset.bs]; if (!s) return;
        const st = slotState(s); el.className = st.cls + ' ab-cdcell'; el.textContent = st.txt;
        if (el.parentElement) {
          el.parentElement.classList.toggle('ab-dim', st.cls !== 'ab-ready');
          el.parentElement.dataset.tip = slotTip(s, st);
          el.parentElement.dataset.tipHtml = '1';
        }
      });
      ensureAdren();
      ensureTimers();
      return;
    }
    abarSig = sig;
    wrap.innerHTML = '';
    if (!vis.length) { wrap.innerHTML = '<div class="stor-empty">No visible action bars.</div>'; return; }
    for (const bar of vis) {
      const sec = document.createElement('div'); sec.className = 'stor-box';
      const h = document.createElement('div'); h.className = 'stor-h';
      h.innerHTML = BARNAME[bar.bar] + (bar.bar > 0 ? ' · <span class="stor-tier">Preset ' + d.preset[bar.bar] + '</span>' : '');
      sec.appendChild(h);
      if (!bar.slots.length) { const e = document.createElement('div'); e.className = 'stor-empty'; e.textContent = 'no bound abilities'; sec.appendChild(e); }
      else for (const s of bar.slots) {
        const st = slotState(s);
        const row = document.createElement('div'); row.className = 'ab-row' + (st.cls !== 'ab-ready' ? ' ab-dim' : '');
        row.dataset.tip = slotTip(s, st);
        row.dataset.tipHtml = '1';   // description carries <col=..>/<br> game markup
        const ico = document.createElement('div'); ico.className = 'ab-icon'; attachAbilityIcon(ico, s); row.appendChild(ico);
        const kb = document.createElement('span'); kb.className = 'ab-key'; kb.textContent = s.key ? ((s.mod ? MOD[s.mod] + '+' : '') + s.key) : '·'; row.appendChild(kb);
        const nm = document.createElement('span'); nm.className = 'ab-n'; nm.textContent = s.name; row.appendChild(nm);
        const t = abTier(s.name), tm = AB_TIER_META[t];
        if (tm) {
          const tag = document.createElement('span');
          tag.textContent = tm.n;
          tag.style.cssText = 'font-size:10px;padding:1px 5px;margin-left:6px;border:1px solid ' + tm.c
            + ';color:' + tm.c + ';border-radius:3px;opacity:.85;white-space:nowrap';
          nm.appendChild(tag);
        }
        const right = document.createElement('span'); right.dataset.bs = bar.bar * 100 + s.slot;
        right.className = st.cls + ' ab-cdcell'; right.textContent = st.txt;
        row.appendChild(right);
        sec.appendChild(row);
      }
      wrap.appendChild(sec);
    }
    ensureAdren();
    ensureTimers();
  }
