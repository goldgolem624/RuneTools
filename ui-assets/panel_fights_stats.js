// RuneToolsX: Fight Logs statistics over a recorded combat log (format 1). Pure functions, no DOM:
// the panel and the website page call the same code so their numbers agree. Registers no tab.
// Exposed as window.combatStats and as module.exports when loaded by node.
(function (root) {

  const CYCLE_MS = 20, TICK = 30;
  // Hitmark kind -> damage style bucket. Kinds come from the log's dict.hitmarks (Hitmarks.h names).
  const STYLE_OF = { melee: 'melee', 'melee crit': 'melee', ranged: 'ranged', 'ranged crit': 'ranged', magic: 'magic', 'magic crit': 'magic',
                     necromancy: 'necromancy', 'necromancy crit': 'necromancy', conjure: 'conjure', 'conjure crit': 'conjure',
                     typeless: 'typeless', poison: 'poison', deflect: 'typeless', cannon: 'typeless', 'split soul': 'typeless', blight: 'typeless',
                     'pierced shield': 'typeless', pool: 'typeless', 'shielded boss': 'typeless', 'instant kill': 'typeless', 'instant kill (soft)': 'typeless' };
  const STYLES = ['melee', 'ranged', 'magic', 'necromancy', 'conjure', 'typeless', 'poison'];
  const ZERO_KINDS = { blocked: 1, absorbed: 1, hidden: 1 };
  const HEAL_KINDS = { heal: 1, 'uber heal': 1 };
  // Ticks after the cast on which an ability's hits land (observed on live traces); abilities with no
  // row get a shape from the dictionary (channel, dot) or the default {0, 1, 2}. An empty set = no damage.
  const SHAPES = {
    CONCENTRATED_BLAST: [0, 1, 2], CHAIN: [0, 1, 2, 3], DRAGON_BREATH: [1, 0], CORRUPTION_BLAST: [0, 1, 2, 3, 4, 5], CORRUPTION_SHOT: [0, 1, 2, 3, 4, 5],
    MAGMA_TEMPEST: [0, 1, 2, 3, 4, 5, 6, 7, 8], WILD_MAGIC: [0, 1], TSUNAMI: [0, 1, 2], OMNIPOWER: [3, 0, 1, 2], ASPHYXIATE: [0, 1, 2, 3],
    SNIPE: [1, 2], RAPID_FIRE: [0, 1, 2, 3, 4], SMOKE_TENDRILS: [0, 1, 2, 3], SUNSHINE: [], METAMORPHOSIS: [], DEATHS_SWIFTNESS: [],
    ANTICIPATION: [], FREEDOM: [], PREPARATION: [], RESONANCE: [], REFLECT: [], DEVOTION: [], BARRICADE: [], IMMORTALITY: [],
    SURGE: [], ESCAPE: [], DIVE: [], BLADED_DIVE: [0, 1], LIMITLESS: [], NATURAL_INSTINCT: [], BERSERK: [], REJUVENATE: [], GUTHIXS_BLESSING: [],
    INCITE: [], PROVOKE: [], REGENERATE: [], DEBILITATE: [0, 1], GLOBAL_COOLDOWN: [],
    // necromancy, from the 2026-10-09 dummy log (ticks after the cast row, 3 cycles of slack)
    TOUCH_OF_DEATH: [1], SOUL_SAP: [1], BLOAT: [2], VOLLEY_OF_SOULS: [2], LIVING_DEATH: [], CONJURE: [], COMMAND: [],
    // necromancy, not seen yet: confirm on a log that has them
    FINGER_OF_DEATH: [1], SOUL_STRIKE: [1], DEATH_SKULLS: [1, 2, 3, 4, 5, 6], SPECTRAL_SCYTHE: [1], SPECTRAL_SCYTHE_RECAST_1: [1], SPECTRAL_SCYTHE_RECAST_2: [1]
  };
  // Hits one cast can own at its shape ticks (Volley of Souls: one per residual soul, all in one cycle).
  const CAP = { TOUCH_OF_DEATH: 1, SOUL_SAP: 1, FINGER_OF_DEATH: 1, SOUL_STRIKE: 1, BLOAT: 1, VOLLEY_OF_SOULS: 5 };
  // Damage over time with a fixed value per application, ticking on the target every 3 ticks: the ticks after
  // the cast it can run for. A later cast of the same ability on the target replaces the value.
  const DOT = { BLOAT: 36 };
  // Hit kinds that are never the player's own cast: one row each, under a negative struct.
  const KIND_ROW = { conjure: -1, 'conjure crit': -1, poison: -2 };
  const KIND_NAME = { '-1': 'Conjures', '-2': 'Poison' }, KIND_ICON = { '-1': 31336, '-2': 0 };   // 31336 = the conjure hitsplat skull
  // Cast animation -> ability [struct, name, icon] (struct params 2914, 2794, 2802), for cast rows that are only a
  // global cooldown stamp. Conjure (35502) and Command (35505) are shared animations; the conjures' reaction names
  // the member.
  const NECRO_SEQ = {
    35456: [48296, 'Touch of Death', 30076], 35458: [48297, 'Finger of Death', 30077], 35461: [48298, 'Soul Sap', 30080],
    35466: [48299, 'Soul Strike', 30082], 35469: [48301, 'Volley of Souls', 30088], 35477: [48308, 'Bloat', 30016],
    35482: [48309, 'Blood Siphon', 30017], 35489: [48311, 'Spectral Scythe', 30084], 35472: [48314, 'Death Skulls', 30074],
    35475: [48324, 'Living Death', 30078], 35502: [33965, 'Conjure Undead Army', 32988], 35505: [-3, 'Command', 0]
  };
  const SEQ_CONJURE = 35502, SEQ_COMMAND = 35505;
  // The conjure NPCs, the animation each plays one tick after a Command, and their spawn animations.
  const CONJURE_NPC = { 30265: 'SKELETON', 30266: 'ZOMBIE', 30267: 'GHOST', 31142: 'PHANTOM' };
  const COMMAND_BY = { 35219: [48303, 'Command Skeleton Warrior', 34165], 35243: [48303, 'Command Skeleton Warrior', 34165],
                       24731: [48307, 'Command Vengeful Ghost', 34166], 36215: [32342, 'Command Phantom Guardian', 34168],
                       35251: [48305, 'Command Putrid Zombie', 34167] };
  const CONJURE_SPAWN = { 35216: 1, 35240: 1, 35256: 1, 24724: 1, 24725: 1, 36213: 1 };
  const CONJURE_BY = { SKELETON: [48302, 'Conjure Skeleton Warrior', 34169], ZOMBIE: [48304, 'Conjure Putrid Zombie', 34173],
                       GHOST: [48306, 'Conjure Vengeful Ghost', 34171], PHANTOM: [31820, 'Conjure Phantom Guardian', 34175] };
  const FALLBACK_AB = {};
  for (const k in NECRO_SEQ) FALLBACK_AB[NECRO_SEQ[k][0]] = { name: NECRO_SEQ[k][1], icon: NECRO_SEQ[k][2], style: 'necromancy' };
  for (const k in COMMAND_BY) FALLBACK_AB[COMMAND_BY[k][0]] = { name: COMMAND_BY[k][1], icon: COMMAND_BY[k][2], style: 'necromancy' };
  for (const k in CONJURE_BY) FALLBACK_AB[CONJURE_BY[k][0]] = { name: CONJURE_BY[k][1], icon: CONJURE_BY[k][2], style: 'necromancy' };
  const EVENT_NAMES = ['hit', 'cast', 'anim', 'target', 'lp', 'adren', 'prayer', 'buff', 'channel', 'tracker', 'death', 'actor', 'encounter', 'gfx', 'proj', 'xp', 'mark', 'bar', 'stat', 'sound'];
  // Boss mechanic rows: ["mech", c, boss, key, kind, id, actor]; kind indexes this list.
  const MECH_KINDS = ['', 'animation', 'graphic', 'tile graphic', 'projectile', 'sound', 'hint arrow', 'var', 'spawn'];

  function token(name) {
    return String(name || '').toUpperCase().replace(/^GREATER\s+/, '').replace(/\(.*?\)/g, '').replace(/[^A-Z0-9]+/g, '_').replace(/^_+|_+$/g, '');
  }
  function seqHasToken(seq, tok) {
    if (!seq || !tok) return false;
    return new RegExp('(^|_)' + tok + '(_|$)').test(seq);
  }

  // Per-log context, built once and kept on the object.
  function ctx(log) {
    if (log.__cs && log.__cs.n === log.events.length) return log.__cs;
    const actors = log.actors || [];
    let self = actors.findIndex(a => a && a.type === 'self');
    if (self < 0) self = 0;
    const hm = {}, dh = (log.dict && log.dict.hitmarks) || {};
    for (const k in dh) hm[k] = dh[k];
    const c = { n: log.events.length, self, actors, hm, abilities: (log.dict && log.dict.abilities) || {}, buffs: (log.dict && log.dict.buffs) || {},
                seqs: (log.dict && log.dict.seqs) || {}, attr: null, mechT: {}, mechN: -1, bossN: {} };
    const sc = log.schema || {};
    for (const k in sc) if (Array.isArray(sc[k]) && sc[k][0] === 'mech') c.mechT[k] = 1;
    log.__cs = c;
    return c;
  }
  function isMech(log, e) {
    const t = e[0];
    return t === 'mech' || !!ctx(log).mechT[t];
  }
  function typeName(log, e) { return isMech(log, e) ? 'mech' : (EVENT_NAMES[e[0]] || String(e[0])); }
  function hmInfo(log, id) {
    const c = ctx(log);
    return c.hm[id] || { kind: 'unknown', other: false, crit: false, name: '' };
  }
  function actorOf(log, i) { const a = ctx(log).actors[i]; return a || { i, type: 'npc', uid: -1, id: -1, name: 'Unknown' }; }
  function actorLabel(log, i) {
    const a = actorOf(log, i);
    if (a.type === 'self') return 'You';
    const n = a.name || (a.type === 'npc' ? 'NPC ' + a.id : 'Player');
    return (a.type === 'npc' && a.id >= 0) ? n + ' (' + a.id + ')' : n;
  }
  // 'dealt' = your hit on another actor, 'taken' = a hit on you, 'blocked' = a zero mark on you,
  // 'heal' = a heal on you, 'npcheal' = an NPC healing itself, 'other' = the other-players set.
  function hitRole(log, ev) {
    const c = ctx(log), h = hmInfo(log, ev[3]);
    if (h.other) return 'other';
    const onSelf = ev[2] === c.self;
    if (HEAL_KINDS[h.kind]) return onSelf ? 'heal' : 'npcheal';
    if (ZERO_KINDS[h.kind] || h.kind === 'text' || h.kind === 'unknown' || h.kind === 'other style' || h.kind === 'classic') return onSelf ? 'blocked' : 'none';
    if (!STYLE_OF[h.kind]) return 'none';
    return onSelf ? 'taken' : 'dealt';
  }
  function hitStyle(log, ev) { return STYLE_OF[hmInfo(log, ev[3]).kind] || ''; }

  function cycleMs(log, c) { return (log.clock ? log.clock.wall0 : 0) + (c - (log.clock ? log.clock.c0 : 0)) * CYCLE_MS; }
  function cycleTick(log, c) { return Math.floor((c - ((log.clock && log.clock.phase) || 0)) / TICK); }

  function range(log, n) {
    const ev = log.events;
    if (n != null && n >= 0 && log.fights && log.fights[n]) return { n, start: log.fights[n].start, end: log.fights[n].end };
    const start = ev.length ? ev[0][1] : 0, end = ev.length ? ev[ev.length - 1][1] : 0;
    return { n: -1, start, end: Math.max(end, start + TICK) };
  }
  function inRange(ev, r) { return ev[1] >= r.start && ev[1] <= r.end; }

  // Ability dictionary helpers.
  function ability(log, struct) {
    const a = ctx(log).abilities[struct];
    if (a) return a;
    // a Putrid Zombie in the log: its attacks are what poisons the target (34179 = its buff icon)
    if (struct === -2 && ctx(log).actors.some(x => x && x.type === 'npc' && x.id === 30266)) return { name: 'Poison (Putrid Zombie)', icon: 34179, style: '', kind: 1 };
    if (KIND_NAME[struct]) return { name: KIND_NAME[struct], icon: KIND_ICON[struct], style: '', kind: 1 };
    if (FALLBACK_AB[struct]) return FALLBACK_AB[struct];
    return { name: struct === 14881 || struct === 14882 ? 'Global cooldown' : 'Ability ' + struct, icon: 0, style: '' };
  }
  function shapeOf(log, struct) {
    const a = ability(log, struct), t = token(a.name);
    if (SHAPES[t]) return SHAPES[t];
    if (/^(CONJURE|COMMAND)_/.test(t)) return [];
    if (a.channel) { const n = Math.max(1, (a.channel[0] || 1) * (a.channel[1] || 1)) + 1; const s = []; for (let i = 0; i <= n; i++) s.push(i); return s; }
    if (a.dot != null) { const n = Math.max(a.dot || 0, 5); const s = []; for (let i = 0; i <= n; i++) s.push(i); return s; }
    return [0, 1, 2];
  }

  // Attribution (names.md rules A1..A8): every hit you dealt gets the struct of the cast it belongs to,
  // or 0. Computed over the whole log once; the result is an array aligned with log.events.
  function attribute(log) {
    const c = ctx(log);
    if (c.attr) return c.attr;
    const ev = log.events, out = new Array(ev.length).fill(0), reason = new Array(ev.length).fill('');
    // casts with the family member resolved by the cast tick's animation (A2/A3)
    const anims = [];
    for (let i = 0; i < ev.length; i++) if (ev[i][0] === 2 && ev[i][2] === c.self && ev[i][3] >= 0) anims.push([ev[i][1], ev[i][3]]);
    function animAt(cc) {
      let best = -1;
      for (const [ac, seq] of anims) { if (ac >= cc && ac < cc + TICK) return seq; if (ac < cc) best = seq; else break; }
      return -1;
    }
    // the conjures' own animations: a Command's reaction one tick later, a Conjure's spawns
    const reacts = [];
    for (let i = 0; i < ev.length; i++) {
      const e = ev[i];
      if (e[0] !== 2 || e[3] < 0 || e[2] === c.self) continue;
      const who = CONJURE_NPC[actorOf(log, e[2]).id];
      if (who) reacts.push([e[1], who, e[3]]);
    }
    // a cast row that is only a global cooldown stamp: the cast tick's animation names the ability
    function fromStamp(cc, seqId) {
      const hit = NECRO_SEQ[seqId];
      if (!hit) return 0;
      if (seqId === SEQ_COMMAND) {
        for (const [rc, , s] of reacts) { if (rc <= cc) continue; if (rc > cc + 2 * TICK + 15) break; if (COMMAND_BY[s]) return COMMAND_BY[s][0]; }
      } else if (seqId === SEQ_CONJURE) {
        const kinds = {};
        for (const [rc, who, s] of reacts) { if (rc <= cc) continue; if (rc > cc + 4 * TICK) break; if (CONJURE_SPAWN[s]) kinds[who] = 1; }
        const k = Object.keys(kinds);
        if (k.length === 1) return CONJURE_BY[k[0]][0];
      }
      return hit[0];
    }
    // a log with real ability rows: a stamp in the same tick as one is that cast, never a second one
    const abC = [];
    for (const x of ev) if (x[0] === 1 && x[2] !== 14881 && x[2] !== 14882) abC.push(x[1]);
    const casts = [];
    for (let i = 0; i < ev.length; i++) {
      if (ev[i][0] !== 1) continue;
      let struct = ev[i][2], resolved = false;
      const a = c.abilities[struct];
      const seqId = animAt(ev[i][1]), seq = c.seqs[seqId] || '';
      if (a && a.family && seq) {
        for (const m of a.family) { const ma = c.abilities[m]; if (ma && seqHasToken(seq, token(ma.name))) { struct = m; break; } }
      }
      if ((struct === 14881 || struct === 14882) && !abC.some(x => Math.abs(x - ev[i][1]) <= TICK)) {
        const s = fromStamp(ev[i][1], seqId);
        if (s) { struct = s; resolved = true; }
      }
      const tok = token(ability(log, struct).name);
      casts.push({ i, c: ev[i][1], struct, src: ev[i][4], resolved, seq, shape: shapeOf(log, struct), style: ability(log, struct).style || '',
                   cap: CAP[tok] || 0, dot: DOT[tok] || 0, used: 0 });
    }
    // two cooldown stamps in one tick (Corruption Blast and Shot move together): keep the one the animation
    // names; a pair with no animation sample follows the choice made for the same pair elsewhere in the log
    const pref = {};
    for (let pass = 0; pass < 2; pass++) {
      for (let k = casts.length - 1; k > 0; k--) {
        if (casts[k].c !== casts[k - 1].c) continue;
        if (token(ability(log, casts[k].struct).name).split('_')[0] !== token(ability(log, casts[k - 1].struct).name).split('_')[0]) continue;
        const key = [casts[k - 1].struct, casts[k].struct].sort().join(':');
        const a = seqHasToken(casts[k].seq, token(ability(log, casts[k].struct).name)), b = seqHasToken(casts[k - 1].seq, token(ability(log, casts[k - 1].struct).name));
        if (a && !b) { pref[key] = casts[k].struct; casts.splice(k - 1, 1); }
        else if (b && !a) { pref[key] = casts[k - 1].struct; casts.splice(k, 1); }
        else if (pass && pref[key] != null) casts.splice(pref[key] === casts[k].struct ? k - 1 : k, 1);
      }
    }
    const castStruct = casts.map(k => k.struct);
    // damage over time: two hits of one hitmark on one target, 2 to 4 ticks apart, with the same value
    const dot = new Array(ev.length).fill(false), byT = {};
    for (let i = 0; i < ev.length; i++) {
      const e = ev[i];
      if (e[0] === 0 && hitRole(log, e) === 'dealt' && KIND_ROW[hmInfo(log, e[3]).kind] == null) (byT[e[2]] = byT[e[2]] || []).push(i);
    }
    const same = (x, y) => Math.abs(x - y) <= 3;
    for (const t in byT) {
      const L = byT[t];
      for (let a = 0; a < L.length; a++) for (let b = a + 1; b < L.length; b++) {
        const ea = ev[L[a]], eb = ev[L[b]], gap = eb[1] - ea[1];
        if (gap > 4 * TICK + 3) break;
        if (gap >= 2 * TICK - 3 && ea[3] === eb[3] && ea[4] === eb[4]) dot[L[a]] = dot[L[b]] = true;
      }
    }
    // a lone tick whose value matches a series on the same target within 12 ticks (the first or last tick)
    for (const t in byT) for (const i of byT[t]) {
      if (dot[i]) continue;
      if (byT[t].some(j => dot[j] && j !== i && Math.abs(ev[j][1] - ev[i][1]) <= 12 * TICK && same(ev[j][4], ev[i][4]))) dot[i] = 'tail';
    }
    let ci = 0;
    for (let i = 0; i < ev.length; i++) {
      const e = ev[i];
      if (e[0] !== 0 || hitRole(log, e) !== 'dealt') continue;
      const S = e[1], style = hitStyle(log, e), kr = KIND_ROW[hmInfo(log, e[3]).kind];
      if (kr != null) { out[i] = kr; reason[i] = 'kind'; continue; }
      while (ci < casts.length && casts[ci].c <= S) ci++;
      if (dot[i]) {
        let owner = null;
        for (let k = ci - 1; k >= 0 && !owner; k--) { const K = casts[k]; if (K.dot && S - K.c >= 3 * TICK - 3 && S - K.c <= K.dot * TICK) owner = K; }
        if (owner) { out[i] = owner.struct; reason[i] = 'dot'; continue; }
      }
      let exact = null, loose = null;
      for (let k = ci - 1; k >= 0 && S - casts[k].c <= 9 * TICK; k--) {
        const K = casts[k], L = Math.floor((S - K.c + 3) / TICK);
        if (!K.shape.length || (K.cap && K.used >= K.cap)) continue;
        if (K.style && style && style !== 'typeless' && style !== 'poison' && K.style !== style && K.style !== 'typeless') continue;
        if (style === 'typeless' && !(ability(log, K.struct).dot != null)) continue;
        if (K.shape.indexOf(L) >= 0) {
          // most recent cast first; two casts stamped in one tick (a shared cooldown pair) are told apart by the animation
          if (!exact) exact = K;
          else if (K.c !== exact.c) break;
          else if (!seqHasToken(exact.seq, token(ability(log, exact.struct).name)) && seqHasToken(K.seq, token(ability(log, K.struct).name))) exact = K;
        } else if (!loose && L <= 2 && !K.cap) loose = K;
      }
      const pick = exact || loose;
      if (pick) { out[i] = pick.struct; reason[i] = exact ? 'cast' : 'near'; pick.used++; }
      else reason[i] = style === 'typeless' ? 'proc' : 'none';
    }
    c.attr = { struct: out, reason, casts, castStruct };
    return c.attr;
  }

  // Who hit you: the NPC that targeted you at that tick (A8), else one with an attack animation then.
  function sourceOf(log, i) {
    const c = ctx(log), ev = log.events, S = ev[i][1];
    const tgt = {}, anim = {};
    for (let k = 0; k < ev.length && ev[k][1] <= S; k++) {
      const e = ev[k];
      if (e[0] === 3 && e[2] !== c.self) tgt[e[2]] = e[3];
      if (e[0] === 2 && e[2] !== c.self && S - e[1] <= 2 * TICK) anim[e[2]] = e[3];
    }
    const targeting = Object.keys(tgt).filter(a => tgt[a] === c.self).map(Number);
    if (targeting.length === 1) return targeting[0];
    const attackers = Object.keys(anim).filter(a => /ATTACK/.test(c.seqs[anim[a]] || '')).map(Number);
    if (attackers.length === 1) return attackers[0];
    if (targeting.length > 1) { const both = targeting.filter(a => attackers.indexOf(a) >= 0); return both.length ? both[0] : targeting[0]; }
    return -1;
  }

  function summary(log, n) {
    const r = range(log, n), c = ctx(log), at = attribute(log);
    let dealt = 0, taken = 0, healed = 0, hits = 0, crits = 0, maxHit = 0, blocked = 0, deaths = 0, kills = 0, casts = 0;
    const byAb = {}, byT = {};
    for (let i = 0; i < log.events.length; i++) {
      const e = log.events[i];
      if (!inRange(e, r)) continue;
      if (e[0] === 0) {
        const role = hitRole(log, e), v = e[4] > 0 ? e[4] : 0;
        if (role === 'dealt') { dealt += v; hits++; if (hmInfo(log, e[3]).crit) crits++; if (v > maxHit) maxHit = v;
          byAb[at.struct[i]] = (byAb[at.struct[i]] || 0) + v; byT[e[2]] = (byT[e[2]] || 0) + v; }
        else if (role === 'taken') { taken += v; if (v === 0) blocked++; }
        else if (role === 'blocked') blocked++;
        else if (role === 'heal') healed += v;
      } else if (e[0] === 10) { if (e[3] === 2) deaths++; else kills++; }
    }
    for (const k of at.casts) if (k.c >= r.start && k.c <= r.end && (k.src !== 3 || k.resolved)) casts++;
    const durMs = Math.max(1, (r.end - r.start) * CYCLE_MS);
    const top = Object.keys(byAb).filter(k => k !== '0').map(k => [Number(k), byAb[k]]).sort((a, b) => b[1] - a[1]).slice(0, 5);
    const targets = Object.keys(byT).map(k => [Number(k), byT[k]]).sort((a, b) => b[1] - a[1]);
    return { durMs, dealt, taken, healed, dps: dealt / durMs * 1000, dpm: dealt / durMs * 60000, hits, crits, maxHit, blocked, deaths, kills, casts,
             topAbilities: top, targets, unattributed: byAb[0] || 0 };
  }

  function byAbility(log, n) {
    const r = range(log, n), at = attribute(log), rows = {};
    let total = 0;
    for (let i = 0; i < log.events.length; i++) {
      const e = log.events[i];
      if (e[0] !== 0 || !inRange(e, r) || hitRole(log, e) !== 'dealt') continue;
      const s = at.struct[i], v = e[4] > 0 ? e[4] : 0;
      const row = rows[s] || (rows[s] = { struct: s, name: s ? ability(log, s).name : 'Unattributed', icon: s ? ability(log, s).icon : 0, hits: 0, crits: 0, max: 0, total: 0, casts: 0 });
      row.hits++; row.total += v; if (v > row.max) row.max = v; if (hmInfo(log, e[3]).crit) row.crits++;
      total += v;
    }
    for (const k of at.casts) if (k.c >= r.start && k.c <= r.end && (k.src !== 3 || k.resolved) && rows[k.struct]) rows[k.struct].casts++;
    const out = Object.keys(rows).map(k => rows[k]);
    for (const row of out) { row.avg = row.hits ? row.total / row.hits : 0; row.share = total ? row.total / total : 0; row.perCast = row.casts ? row.total / row.casts : 0; }
    out.sort((a, b) => (a.struct === 0) - (b.struct === 0) || b.total - a.total);
    return { rows: out, total };
  }

  function bySource(log, n) {
    const r = range(log, n), rows = {};
    let total = 0;
    for (let i = 0; i < log.events.length; i++) {
      const e = log.events[i];
      if (e[0] !== 0 || !inRange(e, r)) continue;
      const role = hitRole(log, e);
      if (role !== 'taken' && role !== 'blocked') continue;
      const src = sourceOf(log, i), a = actorOf(log, src);
      const row = rows[src] || (rows[src] = { actor: src, name: src >= 0 ? (a.name || 'NPC ' + a.id) : 'Unknown', id: src >= 0 ? a.id : -1, hits: 0, blocked: 0, max: 0, total: 0 });
      const v = e[4] > 0 ? e[4] : 0;
      if (role === 'blocked' || v === 0) row.blocked++; else { row.hits++; row.total += v; if (v > row.max) row.max = v; }
      total += v;
    }
    const out = Object.keys(rows).map(k => rows[k]);
    for (const row of out) { row.avg = row.hits ? row.total / row.hits : 0; row.share = total ? row.total / total : 0; }
    out.sort((a, b) => b.total - a.total);
    return { rows: out, total };
  }

  // Time series for the Health chart. LP resets (to 0 and back to max with no death row) start a new segment.
  function series(log, n) {
    const r = range(log, n), c = ctx(log), at = attribute(log);
    const out = { lp: [], lpMax: 0, targets: {}, adren: [], prayer: [], dealt: [], taken: [], kills: [], deaths: [], resets: [], range: r };
    const last = { lp: null, ad: null, pr: null, t: {} };
    const deathAt = {};
    for (const e of log.events) if (e[0] === 10 && e[3] !== 2) (deathAt[e[2]] = deathAt[e[2]] || []).push(e[1]);
    for (let i = 0; i < log.events.length; i++) {
      const e = log.events[i];
      if (e[1] > r.end) break;
      const before = e[1] < r.start;
      if (e[0] === 4) {
        if (e[2] === c.self) { last.lp = [r.start > e[1] ? r.start : e[1], e[3], e[4]]; if (!before) out.lp.push([e[1], e[3], e[4]]); else out.lp[0] = last.lp; if (e[4] > out.lpMax) out.lpMax = e[4]; }
        else {
          const a = actorOf(log, e[2]);
          if (a.type !== 'npc') continue;
          const t = out.targets[e[2]] || (out.targets[e[2]] = { actor: e[2], name: a.name, id: a.id, lpMax: a.lpMax || e[4], segs: [[]] });
          const seg = t.segs[t.segs.length - 1];
          const prev = seg.length ? seg[seg.length - 1] : null;
          const reset = prev && prev[1] > 0 && e[3] === 0 && !(deathAt[e[2]] || []).some(d => Math.abs(d - e[1]) <= 4 * TICK);
          const respawn = prev && e[3] >= e[4] && e[4] > 0 && prev[1] < e[4] && (prev[1] === 0 || e[3] - prev[1] > e[4] / 2);
          if (reset) { out.resets.push([e[1], e[2]]); t.segs.push([]); continue; }
          if (respawn) { t.segs.push([]); }
          const cur = t.segs[t.segs.length - 1];
          if (before) { cur.length = 0; cur.push([r.start, e[3], e[4]]); } else cur.push([e[1], e[3], e[4]]);
          if (e[4] > t.lpMax) t.lpMax = e[4];
        }
      } else if (e[0] === 5) { if (before) out.adren[0] = [r.start, e[2]]; else out.adren.push([e[1], e[2]]); }
      else if (e[0] === 6) { if (before) out.prayer[0] = [r.start, e[2], e[3]]; else out.prayer.push([e[1], e[2], e[3]]); }
      else if (before) continue;
      else if (e[0] === 0) {
        const role = hitRole(log, e);
        if (role === 'dealt') out.dealt.push([e[1], e[4] > 0 ? e[4] : 0, at.struct[i], e[2], hmInfo(log, e[3]).crit ? 1 : 0, i]);
        else if (role === 'taken' || role === 'blocked') out.taken.push([e[1], e[4] > 0 ? e[4] : 0, e[2], i]);
      } else if (e[0] === 10) { if (e[3] === 2) out.deaths.push([e[1]]); else out.kills.push([e[1], e[2]]); }
    }
    for (const k in out.targets) out.targets[k].segs = out.targets[k].segs.filter(s => s.length);
    return out;
  }

  function shortName(name) {
    let s = String(name || '').replace(/[.:]\s*$/, '');
    const cut = s.search(/ - |\. |: /);
    if (cut > 0 && s.length > 36) s = s.slice(0, cut);
    return s.length > 44 ? s.slice(0, 42) + '..' : s;
  }

  // Buff and debuff spans within the fight. An on row with end e covers [c, e]; a later on row for the
  // same struct extends it; an off row closes it at its c.
  function uptimes(log, n) {
    const r = range(log, n), c = ctx(log), open = {}, spans = {};
    function close(s, at) {
      const o = open[s]; if (!o) return;
      const a = Math.max(o.start, r.start), b = Math.min(at, r.end);
      if (b > a) (spans[s] = spans[s] || []).push([a, b]);
      delete open[s];
    }
    for (const e of log.events) {
      if (e[0] !== 7) continue;
      if (e[1] > r.end) break;
      const s = e[2];
      if (e[3]) {
        if (open[s]) { if (e[5] > 0) open[s].end = e[5]; }
        else open[s] = { start: e[1], end: e[5] > 0 ? e[5] : r.end };
      } else close(s, e[5] > 0 && e[5] < e[1] ? e[5] : e[1]);
    }
    for (const s in open) close(s, Math.min(open[s].end, r.end));
    const dur = Math.max(1, r.end - r.start), rows = [];
    for (const s in spans) {
      const list = spans[s].sort((a, b) => a[0] - b[0]), merged = [];
      for (const sp of list) { const m = merged[merged.length - 1]; if (m && sp[0] <= m[1]) m[1] = Math.max(m[1], sp[1]); else merged.push(sp.slice()); }
      const total = merged.reduce((t, sp) => t + sp[1] - sp[0], 0);
      const b = c.buffs[s] || {};
      rows.push({ struct: Number(s), name: shortName(b.name || 'Buff ' + s), fullName: b.name || '', type: b.type || 0, icon: b.icon || 0, spans: merged, uptime: total / dur });
    }
    rows.sort((a, b) => b.uptime - a.uptime || a.name.localeCompare(b.name));
    return { rows, range: r };
  }

  // Rotation: one entry per cast with its tick index from the fight start; idle ticks after the GCD.
  function casts(log, n) {
    const r = range(log, n), at = attribute(log), ab = byAbility(log, n);
    const ticks = Math.max(1, Math.ceil((r.end - r.start) / TICK));
    const list = [];
    for (const k of at.casts) {
      if (k.c < r.start || k.c > r.end) continue;
      const a = ability(log, k.struct), shape = k.shape;
      list.push({ c: k.c, tick: Math.floor((k.c - r.start) / TICK), struct: k.struct, name: a.name, icon: a.icon || 0, src: k.src, resolved: k.resolved, style: a.style || '',
                  span: a.channel ? Math.max(1, (a.channel[0] || 1) * (a.channel[1] || 1) + 1) : (shape.length > 3 ? shape[shape.length - 1] + 1 : 1) });
    }
    let idle = 0, prev = null;
    for (const k of list) { if (prev != null && k.tick - prev > 3) idle += k.tick - prev - 3; prev = k.tick; }
    const table = ab.rows.filter(x => x.struct).map(x => ({ struct: x.struct, name: x.name, icon: x.icon, casts: x.casts, avg: x.avg, total: x.total, perCast: x.perCast }));
    for (const k of list) if ((k.src !== 3 || k.resolved) && !table.some(t => t.struct === k.struct)) table.push({ struct: k.struct, name: k.name, icon: k.icon, casts: 0, avg: 0, total: 0, perCast: 0 });
    for (const t of table) t.casts = list.filter(k => k.struct === t.struct && (k.src !== 3 || k.resolved)).length;
    for (const t of table) t.perCast = t.casts ? t.total / t.casts : 0;
    table.sort((a, b) => b.total - a.total || b.casts - a.casts);
    return { list, ticks, idle, table, range: r };
  }

  // The game's own Combat tracker cells (group 2) against the recorded totals: cumulative damage (col 22)
  // as last minus first value in the range, max hit (col 11) as the largest value.
  function trackerCheck(log, n) {
    const r = range(log, n), s = summary(log, n);
    let first = null, last = null, max = 0, cells = 0;
    for (const e of log.events) {
      if (e[0] !== 9 || e[2] !== 2 || e[3] !== 0) continue;
      if (e[1] > r.end) break;
      if (e[4] === 22) { if (e[1] < r.start || first === null) first = e[5]; last = e[5]; cells++; }
      if (e[4] === 11 && e[1] >= r.start && e[5] > max && e[5] < 2147483647) max = e[5];
    }
    return { dealt: s.dealt, maxHit: s.maxHit, trackerDealt: (first !== null && last !== null) ? last - first : null, trackerMax: max || null, cells };
  }

  // Style split of the damage dealt.
  function styleSplit(log, n) {
    const r = range(log, n), out = {};
    let total = 0;
    for (const s of STYLES) out[s] = 0;
    for (const e of log.events) {
      if (e[0] !== 0 || !inRange(e, r) || hitRole(log, e) !== 'dealt') continue;
      const st = hitStyle(log, e) || 'typeless', v = e[4] > 0 ? e[4] : 0;
      out[st] = (out[st] || 0) + v; total += v;
    }
    return { split: out, total };
  }

  function fmtNum(v) { v = Math.round(v || 0); return v >= 1000000 ? (v / 1000000).toFixed(2) + 'M' : v >= 10000 ? (v / 1000).toFixed(1) + 'K' : String(v); }
  function fmtMs(ms) {
    const neg = ms < 0; ms = Math.abs(ms);
    const s = Math.floor(ms / 1000), m = Math.floor(s / 60), h = Math.floor(m / 60);
    const t = h ? h + ':' + String(m % 60).padStart(2, '0') + ':' + String(s % 60).padStart(2, '0') : m + ':' + String(s % 60).padStart(2, '0');
    return (neg ? '-' : '') + t;
  }
  function fmtMsTenths(ms) { return fmtMs(ms) + '.' + String(Math.floor((Math.abs(ms) % 1000) / 100)); }

  // One text row per event for the Events tab and the TSV copy.
  function describe(log, i) {
    const c = ctx(log), e = log.events[i], at = attribute(log);
    const row = { i, c: e[1], type: EVENT_NAMES[e[0]] || String(e[0]), actor: '', text: '', kind: '', value: '', ability: '', hitmark: '' };
    if (isMech(log, e)) {
      const m = mechInfo(log, e[2], e[3], e[4]), a = e[6] == null ? -1 : e[6];
      row.type = 'mech'; row.actor = a >= 0 ? actorLabel(log, a) : ''; row.kind = MECH_KINDS[m.kind] || 'kind ' + m.kind;
      row.value = e[5] == null ? '' : String(e[5]); row.text = bossName(log, e[2], a) + ': ' + m.label;
      return row;
    }
    switch (e[0]) {
      case 0: {
        const role = hitRole(log, e), h = hmInfo(log, e[3]);
        row.actor = actorLabel(log, e[2]); row.kind = h.kind; row.value = e[4]; row.hitmark = e[3];
        row.ability = at.struct[i] ? ability(log, at.struct[i]).name : (role === 'dealt' ? 'Unattributed' : '');
        row.text = role === 'dealt' ? 'you hit ' + row.actor : role === 'taken' ? 'hit on you' : role === 'blocked' ? 'blocked on you' : role === 'heal' ? 'you healed'
                 : role === 'npcheal' ? row.actor + ' healed' : role === 'other' ? row.actor + ' hit by others' : row.actor + ' ' + h.kind;
        if (role === 'taken' || role === 'blocked') { const s = sourceOf(log, i); if (s >= 0) row.text += ' by ' + actorLabel(log, s); }
        if (e[5] >= 0) row.text += ' (+' + e[6] + ' soaked)';
        break;
      }
      case 1: { const ki = at.casts.findIndex(k => k.i === i), s = ki >= 0 ? at.casts[ki].struct : e[2]; row.ability = ability(log, s).name; row.kind = ki < 0 ? 'paired stamp' : (['exact', 'cooldown', 'animation', 'gcd'][e[4]] || ''); row.text = (ki < 0 ? 'paired stamp ' + row.ability : e[4] === 3 && !at.casts[ki].resolved ? 'global cooldown' : 'cast ' + row.ability) + (e[3] > 0 ? ', ready in ' + fmtMs((e[3] - e[1]) * CYCLE_MS) : ''); break; }
      case 2: row.actor = actorLabel(log, e[2]); row.value = e[3]; row.text = e[3] < 0 ? 'animation ends' : 'animation ' + (c.seqs[e[3]] || e[3]); break;
      case 3: row.actor = actorLabel(log, e[2]); row.text = e[3] < -1 ? 'targets index ' + (-e[3] - 2) : e[3] < 0 ? 'no target' : 'targets ' + actorLabel(log, e[3]); break;
      case 4: row.actor = actorLabel(log, e[2]); row.value = e[3]; row.text = 'life points ' + e[3] + ' / ' + e[4]; break;
      case 5: row.value = e[2]; row.text = 'adrenaline ' + (e[2] / 10).toFixed(1) + '%'; break;
      case 6: row.value = e[2]; row.text = 'prayer ' + (e[2] / 10).toFixed(1) + ' (level ' + e[3] + ')'; break;
      case 7: { const b = c.buffs[e[2]] || {}; row.ability = shortName(b.name || 'Buff ' + e[2]); row.kind = b.type ? 'debuff' : 'buff'; row.text = (e[3] ? 'gained ' : 'lost ') + row.ability + (e[3] && e[5] > 0 ? ', ' + fmtMs((e[5] - e[1]) * CYCLE_MS) : ''); break; }
      case 8: row.text = 'channel ' + e[4] + ' ' + e[3] + ' ticks'; row.ability = e[4]; break;
      case 9: { const tr = (log.dict && log.dict.trackers && log.dict.trackers[e[2]]) || {}; row.kind = tr.name || 'group ' + e[2]; row.value = e[5]; row.text = 'tracker ' + ((tr.cols || {})[e[4]] || 'col ' + e[4]) + ' row ' + e[3] + ' = ' + e[5]; break; }
      case 10: row.actor = actorLabel(log, e[2]); row.text = e[3] === 2 ? 'you died' : row.actor + ' died' + (e[3] === 1 ? ' (death animation)' : ''); break;
      case 11: row.actor = actorLabel(log, e[2]); row.text = row.actor + (e[3] ? ' appears' : ' leaves'); break;
      case 12: row.value = e[2]; row.text = e[2] < 0 ? 'encounter ends' : 'encounter ' + ((log.dict && log.dict.encounters && log.dict.encounters[e[2]]) || e[2]); break;
      case 13: row.actor = e[2] >= 0 ? actorLabel(log, e[2]) : ''; row.value = e[3]; row.text = 'gfx ' + e[3] + (e[2] >= 0 ? ' on ' + row.actor : ''); break;
      case 14: row.value = e[4]; row.text = 'projectile ' + e[4] + (e[2] >= 0 ? ' from ' + actorLabel(log, e[2]) : '') + (e[3] >= 0 ? ' to ' + actorLabel(log, e[3]) : ''); break;
      case 15: row.value = e[3]; row.text = 'xp skill ' + e[2] + ' +' + e[3]; break;
      case 16: row.kind = ['fight start', 'fight end', 'gap', 'rotation', 'logout'][e[2]] || 'mark'; row.text = row.kind + (e[3] ? ': ' + e[3] : ''); break;
      case 17: row.actor = actorLabel(log, e[2]); row.value = e[4]; row.text = 'head bar ' + e[3] + ' fill ' + e[4]; break;
      case 18: row.actor = actorLabel(log, e[2]); row.value = e[4]; row.text = 'stat ' + e[3] + ' ' + e[4] + ' / ' + e[5]; break;
      case 19: row.value = e[2]; row.text = 'sound ' + e[2] + (e[3] ? ' on a tile' : ''); break;
      default: row.text = JSON.stringify(e.slice(2));
    }
    return row;
  }

  // Per-fight summary rows for an index (section 1.4 of the design).
  function fightSummaries(log) {
    return (log.fights || []).map(f => Object.assign({ n: f.n, start: f.start, end: f.end, kind: f.kind, boss: f.boss }, summary(log, f.n)));
  }

  // Boss mechanics. Label, tactic struct and kind come from dict.mechs[boss][key]; the key stands in for a
  // missing label.
  function mechInfo(log, boss, key, kind) {
    const d = (log.dict && log.dict.mechs) || {}, b = d[boss] || {}, m = b[key] || {};
    return { label: m.label ? String(m.label) : String(key), tactic: (m.tactic | 0) > 0 ? m.tactic | 0 : 0, kind: m.kind != null ? m.kind | 0 : kind | 0 };
  }
  function mechCount(log) {
    const c = ctx(log);
    if (c.mechN < 0) { let k = 0; for (const e of log.events) if (isMech(log, e)) k++; c.mechN = k; }
    return c.mechN;
  }
  // The boss's name: an NPC actor with the boss id, else the actor the cue came from.
  function bossName(log, boss, actor) {
    const c = ctx(log);
    if (c.bossN[boss]) return c.bossN[boss];
    const a = c.actors.find(x => x && x.type === 'npc' && x.id === boss && x.name);
    let nm = a ? a.name : '';
    if (!nm && actor != null && actor >= 0) { const b = actorOf(log, actor); if (b.type === 'npc' && b.name) nm = b.name; }
    if (!nm) return 'NPC ' + boss;
    c.bossN[boss] = nm;
    return nm;
  }
  // Mechanics in the range. A mechanic is a boss and label; its cues (animation, graphic, sound...) that fall
  // within MECH_USE cycles of a use's first cue are that one use. Returns every cue row (list), every use
  // (uses) and one summary per mechanic (rows) with its cues, use times and the gaps between uses (ms).
  const MECH_USE = 90;
  function mechs(log, n) {
    const r = range(log, n), list = [], uses = [], rows = {}, bosses = [];
    for (let i = 0; i < log.events.length; i++) {
      const e = log.events[i];
      if (e[1] > r.end) break;
      if (e[1] < r.start || !isMech(log, e)) continue;
      const boss = Number(e[2]) || 0, key = String(e[3]), id = e[5] == null ? -1 : Number(e[5]), actor = e[6] == null ? -1 : Number(e[6]);
      const m = mechInfo(log, boss, key, e[4]), k = boss + ':' + m.label;
      const it = { i, c: e[1], boss, key, kind: m.kind, id, actor, label: m.label, tactic: m.tactic, row: k };
      list.push(it);
      let row = rows[k];
      if (!row) { row = rows[k] = { id: k, boss, label: m.label, key, tactic: m.tactic, kind: m.kind, cues: [], ids: [], uses: [] }; if (bosses.indexOf(boss) < 0) bosses.push(boss); }
      if (!row.tactic && m.tactic) row.tactic = m.tactic;
      let q = row.cues.find(x => x.key === key && x.id === id);
      if (!q) { row.cues.push(q = { key, kind: m.kind, id, n: 0 }); if (row.ids.indexOf(id) < 0) row.ids.push(id); }
      q.n++;
      const u = row.uses[row.uses.length - 1];
      if (u && it.c - u.c <= MECH_USE) { u.cues.push(it); if (u.actor < 0) u.actor = actor; }
      else { const nu = { c: it.c, boss, row: k, label: m.label, tactic: 0, kind: m.kind, id, actor, cues: [it] }; row.uses.push(nu); uses.push(nu); }
    }
    const out = Object.keys(rows).map(k => rows[k]);
    for (const u of uses) u.tactic = rows[u.row].tactic;
    for (const row of out) {
      row.times = row.uses.map(u => u.c); row.count = row.times.length;
      row.first = row.times[0]; row.last = row.times[row.times.length - 1]; row.gaps = [];
      for (let j = 1; j < row.times.length; j++) row.gaps.push((row.times[j] - row.times[j - 1]) * CYCLE_MS);
      row.avgGap = row.gaps.length ? row.gaps.reduce((t, g) => t + g, 0) / row.gaps.length : 0;
      row.minGap = row.gaps.length ? Math.min.apply(null, row.gaps) : 0;
      row.maxGap = row.gaps.length ? Math.max.apply(null, row.gaps) : 0;
    }
    out.sort((a, b) => a.first - b.first || a.label.localeCompare(b.label));
    return { list, uses, rows: out, bosses, range: r };
  }
  // Game text to plain text: <br> becomes a line break, other tags are dropped.
  function plain(s) { return String(s == null ? '' : s).replace(/<br\s*\/?>/gi, '\n').replace(/<[^>]*>/g, '').replace(/[ \t]+\n/g, '\n').replace(/\n{3,}/g, '\n\n').trim(); }

  const api = { version: 1, CYCLE_MS, TICK, STYLES, EVENT_NAMES, MECH_KINDS, token, ctx, hmInfo, hitRole, hitStyle, actorOf, actorLabel, cycleMs, cycleTick, range, inRange,
                ability, shapeOf, attribute, sourceOf, summary, byAbility, bySource, series, uptimes, casts, trackerCheck, styleSplit, describe, fightSummaries,
                isMech, typeName, mechInfo, mechCount, bossName, mechs, plain, shortName, fmtNum, fmtMs, fmtMsTenths };
  root.combatStats = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof window !== 'undefined' ? window : globalThis);
