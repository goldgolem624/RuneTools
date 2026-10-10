// RuneToolsX: Fight Logs statistics over a recorded combat log (format 1). Pure functions, no DOM:
// the panel and the website page call the same code so their numbers agree. Registers no tab.
// Exposed as window.combatStats and as module.exports when loaded by node.
(function (root) {

  const CYCLE_MS = 20, TICK = 30;
  // Hitmark kind -> damage style bucket. Kinds come from the log's dict.hitmarks.
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
    // necromancy, from a training dummy log (ticks after the cast row, 3 cycles of slack)
    TOUCH_OF_DEATH: [1], SOUL_SAP: [1], BLOAT: [2], VOLLEY_OF_SOULS: [2], LIVING_DEATH: [], CONJURE: [], COMMAND: [],
    // necromancy, not seen yet: confirm on a log that has them
    // Death Skulls, from the 2026-10-09 K'ril logs: the skull bounces every 2 ticks from tick 3, 7 bounces
    DEATH_SKULLS: [3, 5, 7, 9, 11, 13, 15],
    FINGER_OF_DEATH: [1], SOUL_STRIKE: [1], SPECTRAL_SCYTHE: [1], SPECTRAL_SCYTHE_RECAST_1: [1], SPECTRAL_SCYTHE_RECAST_2: [1]
  };
  // Hits one cast can own at its shape ticks (Volley of Souls: one per residual soul, all in one cycle).
  // How far back a hit looks for its cast, in ticks: the longest shape any ability has (Death Skulls' last bounce
  // lands 15 ticks after the cast), and never less than the 9 ticks the loose match needs.
  let LOOKBACK = 9;
  for (const k in SHAPES) for (const t of SHAPES[k]) if (t + 1 > LOOKBACK) LOOKBACK = t + 1;
  const CAP = { TOUCH_OF_DEATH: 1, SOUL_SAP: 1, FINGER_OF_DEATH: 1, SOUL_STRIKE: 1, BLOAT: 1, VOLLEY_OF_SOULS: 5, DEATH_SKULLS: 8 };
  // Damage over time with a fixed value per application, ticking on the target every 3 ticks: the ticks after
  // the cast it can run for. A later cast of the same ability on the target replaces the value.
  const DOT = { BLOAT: 36 }, DOT_MAX = 36;
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
  const CONJURE_NAME = { 30265: 'Skeleton Warrior', 30266: 'Putrid Zombie', 30267: 'Vengeful Ghost', 31142: 'Phantom Guardian' };
  const CONJURE_IDS = new Set([30265, 30266, 30267, 31142]);
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
  // Structs a seq id can name when the log carries ids only: the fallback rows plus the two global cooldown structs.
  const SEQ_FALLBACK = [];
  for (const k in FALLBACK_AB) SEQ_FALLBACK.push([Number(k), FALLBACK_AB[k].name]);
  SEQ_FALLBACK.push([14881, 'Global cooldown'], [14882, 'Global cooldown']);
  const EVENT_NAMES = ['hit', 'cast', 'anim', 'target', 'lp', 'adren', 'prayer', 'buff', 'channel', 'tracker', 'death', 'actor', 'encounter', 'gfx', 'proj', 'xp', 'mark', 'bar', 'stat', 'sound', 'item'];
  // Boss mechanic rows: ["mech", c, boss, key, kind, id, actor]; kind indexes this list.
  const MECH_KINDS = ['', 'animation', 'graphic', 'tile graphic', 'projectile', 'sound', 'hint arrow', 'var', 'spawn'];

  // Ranking rules shared by the launcher, the site and its pages.
  const STYLE_LINE = 0.8, MIN_FIGHT_MS = 10000, MAX_FIGHT_MS = 3600000, DPS_CAP = 60000, MAX_HIT_CAP = 250000, PLAUSIBLE_RATIO = 1.05;
  const SUMMON_BUFFS = [48335, 48336, 48337, 32349];
  const PARSE_HEX = { grey: '#9d9d9d', green: '#1eff00', blue: '#0070ff', purple: '#a335ee', orange: '#ff8000', pink: '#e268a8', gold: '#e5cc80' };
  const REASON_TEXT = {
    not_kill: 'Only confirmed boss kills are ranked.',
    gap: 'The recording had a gap during this fight.',
    read_fails: 'Too many failed reads in this log.',
    too_short: 'Fights under 10 seconds are not ranked.',
    too_long: 'Fights over 60 minutes are not ranked.',
    no_lp: 'The boss\'s life points were not recorded.',
    implausible_damage: 'Damage is higher than the life points the boss lost.',
    dps_cap: 'Damage is outside the expected range.',
    max_hit: 'A hit is outside the expected range.',
    launcher_old: 'Recorded by a launcher older than 3.6.0.',
    character_claimed: 'Another account holds this character.',
    claim_limit: 'This account holds as many characters as it can for now.',
    not_public: 'Only public logs are ranked.',
    live: 'Live logs are ranked once the log is finished.',
    hidden: 'Removed from rankings.',
    held: 'Held for an admin check.',
    unknown_boss: 'This boss is not ranked.',
    duplicate: 'This fight was already uploaded.',
    implausible_lp: 'The boss\'s life points do not match other kills.',
    account_new: 'Accounts are ranked from 7 days old.',
    account_unverified: 'Verify your email or link Discord to be ranked.',
    not_ranked: 'This kill is not on the rankings.'
  };

  const MARK = {};   // tags the per-log context so a stray property is never taken for one
  function own(o, k) { return o != null && Object.prototype.hasOwnProperty.call(o, k); }
  function isObj(o) { return o !== null && typeof o === 'object' && !Array.isArray(o); }
  function isNum(x) { return typeof x === 'number' && isFinite(x); }

  function token(name) {
    return String(name || '').toUpperCase().replace(/^GREATER\s+/, '').replace(/\(.*?\)/g, '').replace(/[^A-Z0-9]+/g, '_').replace(/^_+|_+$/g, '');
  }
  // tok occurs in seq with start or '_' on its left and end or '_' on its right.
  function seqHasToken(seq, tok) {
    if (!seq || !tok) return false;
    const s = String(seq), n = tok.length;
    let p = s.indexOf(tok);
    while (p >= 0) {
      if ((p === 0 || s.charCodeAt(p - 1) === 95) && (p + n === s.length || s.charCodeAt(p + n) === 95)) return true;
      p = s.indexOf(tok, p + 1);
    }
    return false;
  }
  // Seq tag bits: 1 = an attack animation, 2 = a death animation.
  function tagOf(name) {
    const s = name == null ? '' : String(name);
    if (!s) return 0;
    return (s.indexOf('ATTACK') >= 0 ? 1 : 0) | (s.split('_').indexOf('DEATH') >= 0 ? 2 : 0);
  }

  // Per-log context, built once and kept on the object (not enumerable, so JSON never carries it).
  function ctx(log) {
    const old = log.__cs;
    if (old && old.mark === MARK && old.n === log.events.length) return old;
    const actors = log.actors || [];
    let self = actors.findIndex(a => a && a.type === 'self');
    if (self < 0) self = 0;
    const hm = Object.create(null), dh = (log.dict && log.dict.hitmarks) || {};
    for (const k in dh) hm[k] = dh[k];
    const si = log.dict && log.dict.seqinfo;
    const c = { n: log.events.length, self, actors, hm, abilities: (log.dict && log.dict.abilities) || {}, buffs: (log.dict && log.dict.buffs) || {},
                seqs: (log.dict && log.dict.seqs) || {}, attr: null, mechT: {}, mechN: -1, bossN: {} };
    Object.defineProperty(c, 'mark', { value: MARK });
    c.seqinfo = isObj(si) ? si : null;
    const sc = log.schema || {};
    for (const k in sc) if (Array.isArray(sc[k]) && sc[k][0] === 'mech') c.mechT[k] = 1;
    for (const a of actors) if (a && a.type === 'npc' && /_/.test(a.name || '') && !/ /.test(a.name || '')) a.name = CONJURE_NAME[a.id] || '';
    let sorted = true;
    const ev = log.events;
    for (let i = 0; i < ev.length; i++) {
      const e = ev[i];
      if (!Array.isArray(e) || !isNum(e[1]) || (i && e[1] < ev[i - 1][1])) { sorted = false; break; }
    }
    c.sorted = sorted;
    Object.defineProperty(log, '__cs', { value: c, writable: true, configurable: true, enumerable: false });
    return c;
  }
  // Drops the per-log cache (after actors, dict or events were replaced in place).
  function reset(log) { if (log && own(log, '__cs')) delete log.__cs; }

  // Actors that can be a kill: an NPC you hit or targeted. Summons (yours or anyone's) dying or expiring are not kills.
  function isFoe(log, i) {
    const c = ctx(log);
    if (!c.foes) {
      c.foes = {};
      for (const e of log.events) {
        if (e[0] === 0 && hitRole(log, e) === 'dealt') c.foes[e[2]] = 1;
        else if (e[0] === 3 && e[2] === c.self && e[3] >= 0) c.foes[e[3]] = 1;
      }
    }
    return !!c.foes[i];
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
  function actorName(log, i) {
    const a = actorOf(log, i);
    if (a.type === 'self') return 'You';
    return a.name || (a.type === 'npc' ? npcName(a.id) : 'Player');
  }
  function npcName(id) { return isNum(id) && id < 0 ? 'NPC' : 'NPC ' + id; }
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

  // ---- Selections. A number is a fight index (-1 or a bad index = the whole log span); an object is
  // { fights: 'all' | n | [n], from, to, target, ability, enemy } and resolves to spans plus filters.
  function range(log, n) {
    if (n !== null && typeof n === 'object') return resolve(log, n);
    const ev = log.events;
    if (n != null && n >= 0 && log.fights && log.fights[n]) return { n, start: log.fights[n].start, end: log.fights[n].end };
    const start = ev.length ? ev[0][1] : 0, end = ev.length ? ev[ev.length - 1][1] : 0;
    return { n: -1, start, end: Math.max(end, start + TICK) };
  }
  function lastCycle(log) {
    const ev = log.events || [];
    if (!ev.length) return 0;
    if (ctx(log).sorted) return ev[ev.length - 1][1];
    let m = -Infinity;
    for (const e of ev) if (Array.isArray(e) && isNum(e[1]) && e[1] > m) m = e[1];
    return isFinite(m) ? m : 0;
  }
  // A fight's end; an open (live) fight ends at the last event.
  function fightEnd(log, f) { return isNum(f.end) && f.end >= f.start ? f.end : Math.max(f.start, lastCycle(log)); }
  function toSet(x) {
    if (x == null) return null;
    const s = new Set();
    for (const v of (Array.isArray(x) ? x : [x])) if (isNum(v)) s.add(v);
    return s.size ? s : null;
  }
  function resolve(log, sel) {
    sel = sel || {};
    const fights = Array.isArray(log.fights) ? log.fights : [];
    const hasFrom = isNum(sel.from), hasTo = isNum(sel.to);
    let fs = sel.fights;
    if (fs == null) fs = hasFrom || hasTo ? null : 'all';
    let spans = [], one = -1;
    if (fs === null || (fs === 'all' && !fights.length)) {
      const w = range(log, -1);
      spans.push([w.start, w.end]);
    } else {
      const list = fs === 'all' ? fights.map((f, k) => k) : (Array.isArray(fs) ? fs : [fs]);
      for (const k of list) {
        const f = Number.isInteger(k) && k >= 0 ? fights[k] : null;
        if (!f || !isNum(f.start)) continue;
        spans.push([f.start, fightEnd(log, f)]);
        if (list.length === 1) one = k;
      }
    }
    if (hasFrom || hasTo) {
      spans = spans.map(sp => {
        const a = hasFrom ? Math.max(sp[0], sel.from) : sp[0], b = hasTo ? Math.min(sp[1], sel.to) : sp[1];
        if (a !== sp[0] || b !== sp[1]) one = -1;
        return [a, b];
      });
    }
    spans = spans.filter(sp => sp[1] >= sp[0]).sort((a, b) => a[0] - b[0] || a[1] - b[1]);
    const merged = [];
    for (const sp of spans) { const m = merged[merged.length - 1]; if (m && sp[0] <= m[1]) m[1] = Math.max(m[1], sp[1]); else merged.push(sp.slice()); }
    const empty = !merged.length;
    if (empty) { const f0 = hasFrom ? sel.from : range(log, -1).start; merged.push([f0, f0]); one = -1; }
    let dur = 0;
    for (const sp of merged) dur += (sp[1] - sp[0]) * CYCLE_MS;
    const R = { n: one, start: merged[0][0], end: merged[merged.length - 1][1], spans: merged, durMs: empty ? 1 : Math.max(1, dur),
                targets: toSet(sel.target), abilities: toSet(sel.ability), enemies: toSet(sel.enemy) };
    Object.defineProperty(R, 'empty', { value: empty });
    return R;
  }
  function inR(c, r) {
    const sp = r.spans;
    if (!sp) return c >= r.start && c <= r.end;
    if (r.empty) return false;
    let lo = 0, hi = sp.length - 1;
    while (lo <= hi) { const m = (lo + hi) >> 1; if (c < sp[m][0]) hi = m - 1; else if (c > sp[m][1]) lo = m + 1; else return true; }
    return false;
  }
  function spanAt(c, r) {
    const sp = r.spans;
    if (!sp) return c >= r.start && c <= r.end ? 0 : -1;
    let lo = 0, hi = sp.length - 1;
    while (lo <= hi) { const m = (lo + hi) >> 1; if (c < sp[m][0]) hi = m - 1; else if (c > sp[m][1]) lo = m + 1; else return m; }
    return -1;
  }
  function inRange(ev, r) { return inR(ev[1], r); }
  function durOf(r) { return r.durMs != null ? r.durMs : Math.max(1, (r.end - r.start) * CYCLE_MS); }
  function spansOf(r) { return r.spans ? (r.empty ? [] : r.spans) : [[r.start, r.end]]; }
  // Event index window [lo, hi) holding every event of [r.start, r.end] (the whole log when not sorted by c).
  function lowerBound(ev, x) { let lo = 0, hi = ev.length; while (lo < hi) { const m = (lo + hi) >> 1; if (ev[m][1] < x) lo = m + 1; else hi = m; } return lo; }
  function upperBound(ev, x) { let lo = 0, hi = ev.length; while (lo < hi) { const m = (lo + hi) >> 1; if (ev[m][1] <= x) lo = m + 1; else hi = m; } return lo; }
  function win(log, r) {
    const ev = log.events;
    if (!ctx(log).sorted) return [0, ev.length];
    return [lowerBound(ev, r.start), upperBound(ev, r.end)];
  }
  function passDealt(r, e, struct) { return (!r.targets || r.targets.has(e[2])) && (!r.abilities || r.abilities.has(struct)); }
  // Fights that overlap the selection.
  function fightsIn(log, r) {
    if (r.n >= 0) return [r.n];
    const out = [], fights = log.fights || [];
    for (let k = 0; k < fights.length; k++) {
      const f = fights[k];
      if (!f || !isNum(f.start)) continue;
      const e = fightEnd(log, f);
      if (spansOf(r).some(sp => sp[0] <= e && sp[1] >= f.start)) out.push(k);
    }
    return out;
  }

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

  // ---- Seq ids. A log from the launcher's own folder names its animations in dict.seqs; an uploaded log
  // carries dict.seqinfo instead: per seq id the structs it belongs to and a tag. Both answer the same questions.
  function seqEntry(c, seqId) { return own(c.seqinfo, seqId) && isObj(c.seqinfo[seqId]) ? c.seqinfo[seqId] : null; }
  function seqIs(log, seqId, struct) {
    if (!isNum(seqId) || seqId < 0) return false;
    const c = ctx(log), a = c.abilities[struct];
    if (a && a.anim === seqId) return true;
    if (c.seqinfo) { const s = seqEntry(c, seqId); return !!(s && Array.isArray(s.ab) && s.ab.indexOf(struct) >= 0); }
    return seqHasToken(c.seqs[seqId], token(ability(log, struct).name));
  }
  function seqTag(log, seqId) {
    const c = ctx(log);
    if (c.seqinfo) { const s = seqEntry(c, seqId); return s ? (s.tag | 0) : 0; }
    return tagOf(c.seqs[seqId]);
  }
  // The id-only seq table built from the names: { "<seq>": { ab: [struct, ...], tag } }.
  function seqInfoFrom(log) {
    const d = isObj(log.dict) ? log.dict : {}, abs = isObj(d.abilities) ? d.abilities : {}, seqs = isObj(d.seqs) ? d.seqs : {};
    const byTok = new Map(), byAnim = new Map();
    let maxParts = 0;
    function add(S, name, anim) {
      const t = token(name);
      if (t) { let l = byTok.get(t); if (!l) byTok.set(t, l = []); l.push(S); const p = t.split('_').length; if (p > maxParts) maxParts = p; }
      if (isNum(anim)) { let l = byAnim.get(anim); if (!l) byAnim.set(anim, l = []); l.push(S); }
    }
    for (const k of Object.keys(abs)) {
      const S = Number(k);
      if (!Number.isInteger(S)) continue;
      const a = abs[k];
      add(S, isObj(a) && typeof a.name === 'string' ? a.name : '', isObj(a) ? a.anim : undefined);
    }
    for (const fb of SEQ_FALLBACK) if (!own(abs, String(fb[0]))) add(fb[0], fb[1], undefined);
    const out = {};
    for (const s of Object.keys(seqs)) {
      if (s === '__proto__') continue;
      const name = typeof seqs[s] === 'string' ? seqs[s] : '';
      const set = new Set(), an = byAnim.get(Number(s));
      if (an) for (const x of an) set.add(x);
      if (name && byTok.size) {
        const parts = name.split('_');
        for (let i = 0; i < parts.length; i++) {
          if (!parts[i]) continue;
          let run = '';
          for (let j = i; j < parts.length && j - i < maxParts; j++) {
            if (!parts[j]) break;
            run = j === i ? parts[i] : run + '_' + parts[j];
            const hit = byTok.get(run);
            if (hit) for (const x of hit) set.add(x);
          }
        }
      }
      const tag = tagOf(name);
      if (set.size || tag) out[s] = { ab: Array.from(set).sort((a, b) => a - b), tag };
    }
    return out;
  }

  // Attribution: every hit you dealt gets the struct of the cast it belongs to, or 0. Computed over the
  // whole log once; the result is an array aligned with log.events.
  function attribute(log) {
    const c = ctx(log);
    if (c.attr) return c.attr;
    const ev = log.events, out = new Array(ev.length).fill(0), reason = new Array(ev.length).fill(''), sorted = c.sorted;
    // first index of a list of [c, ...] rows (sorted by c) whose c is above x
    const after = (L, x) => { let lo = 0, hi = L.length; while (lo < hi) { const m = (lo + hi) >> 1; if (L[m][0] <= x) lo = m + 1; else hi = m; } return lo; };
    // casts with the family member resolved by the cast tick's animation
    const anims = [];
    for (let i = 0; i < ev.length; i++) if (ev[i][0] === 2 && ev[i][2] === c.self && ev[i][3] >= 0) anims.push([ev[i][1], ev[i][3]]);
    function animAt(cc) {
      if (sorted) { let k = 0, hi = anims.length; while (k < hi) { const m = (k + hi) >> 1; if (anims[m][0] < cc) k = m + 1; else hi = m; } return k < anims.length && anims[k][0] < cc + TICK ? anims[k][1] : -1; }
      for (const [ac, seq] of anims) { if (ac >= cc && ac < cc + TICK) return seq; if (ac >= cc) break; }
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
      const k0 = sorted ? after(reacts, cc) : 0;
      if (seqId === SEQ_COMMAND) {
        for (let q = k0; q < reacts.length; q++) { const [rc, , s] = reacts[q]; if (rc <= cc) continue; if (rc > cc + 2 * TICK + 15) break; if (COMMAND_BY[s]) return COMMAND_BY[s][0]; }
      } else if (seqId === SEQ_CONJURE) {
        const kinds = {};
        for (let q = k0; q < reacts.length; q++) { const [rc, who, s] = reacts[q]; if (rc <= cc) continue; if (rc > cc + 4 * TICK) break; if (CONJURE_SPAWN[s]) kinds[who] = 1; }
        const k = Object.keys(kinds);
        if (k.length === 1) return CONJURE_BY[k[0]][0];
      }
      return hit[0];
    }
    // a log with real ability rows: a stamp in the same tick as one is that cast, never a second one
    const abC = [];
    for (const x of ev) if (x[0] === 1 && x[2] !== 14881 && x[2] !== 14882) abC.push(x[1]);
    const nearAb = cc => {
      if (!sorted) return abC.some(x => Math.abs(x - cc) <= TICK);
      let lo = 0, hi = abC.length;
      while (lo < hi) { const m = (lo + hi) >> 1; if (abC[m] < cc - TICK) lo = m + 1; else hi = m; }
      return lo < abC.length && abC[lo] <= cc + TICK;
    };
    const casts = [];
    for (let i = 0; i < ev.length; i++) {
      if (ev[i][0] !== 1) continue;
      let struct = ev[i][2], resolved = false;
      const a = c.abilities[struct];
      const seqId = animAt(ev[i][1]), seq = c.seqs[seqId] || '';
      if (a && a.family && seqId >= 0) {
        for (const m of a.family) { const ma = c.abilities[m]; if (ma && seqIs(log, seqId, m)) { struct = m; break; } }
      }
      if ((struct === 14881 || struct === 14882) && !nearAb(ev[i][1])) {
        const s = fromStamp(ev[i][1], seqId);
        if (s) { struct = s; resolved = true; }
      }
      const tok = token(ability(log, struct).name);
      casts.push({ i, c: ev[i][1], struct, src: ev[i][4], resolved, seq, seqId, shape: shapeOf(log, struct), style: ability(log, struct).style || '',
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
        const a = seqIs(log, casts[k].seqId, casts[k].struct), b = seqIs(log, casts[k - 1].seqId, casts[k - 1].struct);
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
    // (a tail found earlier on the same target counts as part of a series for the later hits)
    for (const t in byT) {
      const L = byT[t];
      if (!sorted) {
        for (const i of L) {
          if (dot[i]) continue;
          if (L.some(j => dot[j] && j !== i && Math.abs(ev[j][1] - ev[i][1]) <= 12 * TICK && same(ev[j][4], ev[i][4]))) dot[i] = 'tail';
        }
        continue;
      }
      const D = L.filter(j => dot[j]), T = [];
      if (!D.length) continue;
      let lo = 0, tlo = 0;
      for (const i of L) {
        if (dot[i]) continue;
        const ci = ev[i][1];
        let hit = false;
        while (lo < D.length && ev[D[lo]][1] < ci - 12 * TICK) lo++;
        for (let q = lo; q < D.length && ev[D[q]][1] <= ci + 12 * TICK; q++) if (same(ev[D[q]][4], ev[i][4])) { hit = true; break; }
        if (!hit) {
          while (tlo < T.length && ev[T[tlo]][1] < ci - 12 * TICK) tlo++;
          for (let q = tlo; q < T.length; q++) if (same(ev[T[q]][4], ev[i][4])) { hit = true; break; }
        }
        if (hit) { dot[i] = 'tail'; T.push(i); }
      }
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
        for (let k = ci - 1; k >= 0 && !owner; k--) { const K = casts[k]; if (sorted && S - K.c > DOT_MAX * TICK) break; if (K.dot && S - K.c >= 3 * TICK - 3 && S - K.c <= K.dot * TICK) owner = K; }
        if (owner) { out[i] = owner.struct; reason[i] = 'dot'; continue; }
      }
      let exact = null, loose = null;
      for (let k = ci - 1; k >= 0 && S - casts[k].c <= LOOKBACK * TICK; k--) {
        const K = casts[k], L = Math.floor((S - K.c + 3) / TICK);
        if (!K.shape.length || (K.cap && K.used >= K.cap)) continue;
        if (K.style && style && style !== 'typeless' && style !== 'poison' && K.style !== style && K.style !== 'typeless') continue;
        if (style === 'typeless' && !(ability(log, K.struct).dot != null)) continue;
        if (K.shape.indexOf(L) >= 0) {
          // most recent cast first; two casts stamped in one tick (a shared cooldown pair) are told apart by the animation
          if (!exact) exact = K;
          else if (K.c !== exact.c) break;
          else if (!seqIs(log, exact.seqId, exact.struct) && seqIs(log, K.seqId, K.struct)) exact = K;
        } else if (!loose && L <= 2 && !K.cap) loose = K;
      }
      const pick = exact || loose;
      if (pick) { out[i] = pick.struct; reason[i] = exact ? 'cast' : 'near'; pick.used++; }
      else reason[i] = style === 'typeless' ? 'proc' : 'none';
    }
    c.attr = { struct: out, reason, casts, castStruct };
    return c.attr;
  }

  // Who hit you: the NPC that targeted you at that tick, else one with an attack animation then.
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
    const attackers = Object.keys(anim).filter(a => seqTag(log, anim[a]) & 1).map(Number);
    if (attackers.length === 1) return attackers[0];
    if (targeting.length > 1) { const both = targeting.filter(a => attackers.indexOf(a) >= 0); return both.length ? both[0] : targeting[0]; }
    return -1;
  }
  // sourceOf for every hit on you in one forward sweep: an array aligned with events, -1 elsewhere.
  function sources(log) {
    const c = ctx(log);
    if (c.src) return c.src;
    const ev = log.events, n = ev.length, out = new Array(n).fill(-1);
    const onYou = e => { if (e[0] !== 0) return false; const r = hitRole(log, e); return r === 'taken' || r === 'blocked'; };
    if (!c.sorted) {
      for (let i = 0; i < n; i++) if (Array.isArray(ev[i]) && onYou(ev[i])) out[i] = sourceOf(log, i);
      c.src = out;
      return out;
    }
    const self = c.self, onSelf = new Set(), last = new Map(), animQ = [];
    let qh = 0, i = 0;
    const byNum = (a, b) => a - b;
    while (i < n) {
      const S = ev[i][1];
      let j = i;
      while (j < n && ev[j][1] === S) j++;
      for (let k = i; k < j; k++) {
        const e = ev[k];
        if (e[0] === 3 && e[2] !== self) { if (e[3] === self) onSelf.add(e[2]); else onSelf.delete(e[2]); }
        if (e[0] === 2 && e[2] !== self) { last.set(e[2], e[3]); animQ.push([e[1], e[2]]); }
      }
      let res = null;
      for (let k = i; k < j; k++) {
        if (!onYou(ev[k])) continue;
        if (res === null) {
          const targeting = Array.from(onSelf).sort(byNum);
          if (targeting.length === 1) res = targeting[0];
          else {
            while (qh < animQ.length && S - animQ[qh][0] > 2 * TICK) qh++;
            const seen = new Set(), attackers = [];
            for (let q = qh; q < animQ.length; q++) {
              const a = animQ[q][1];
              if (seen.has(a)) continue;
              seen.add(a);
              if (seqTag(log, last.get(a)) & 1) attackers.push(a);
            }
            attackers.sort(byNum);
            if (attackers.length === 1) res = attackers[0];
            else if (targeting.length > 1) { const both = targeting.filter(a => attackers.indexOf(a) >= 0); res = both.length ? both[0] : targeting[0]; }
            else res = -1;
          }
        }
        out[k] = res;
      }
      i = j;
    }
    c.src = out;
    return out;
  }

  function summary(log, n) {
    const r = range(log, n), c = ctx(log), at = attribute(log), ev = log.events, src = r.enemies ? sources(log) : null;
    let dealt = 0, taken = 0, healed = 0, hits = 0, crits = 0, maxHit = 0, blocked = 0, deaths = 0, kills = 0, casts = 0;
    const byAb = {}, byT = {}, hitT = new Set(), dead = [];
    const [lo, hi] = win(log, r);
    for (let i = lo; i < hi; i++) {
      const e = ev[i];
      if (!inR(e[1], r)) continue;
      if (e[0] === 0) {
        const role = hitRole(log, e), v = e[4] > 0 ? e[4] : 0;
        if (role === 'dealt') {
          hitT.add(e[2]);
          if (!passDealt(r, e, at.struct[i])) continue;
          dealt += v; hits++; if (hmInfo(log, e[3]).crit) crits++; if (v > maxHit) maxHit = v;
          byAb[at.struct[i]] = (byAb[at.struct[i]] || 0) + v; byT[e[2]] = (byT[e[2]] || 0) + v;
        } else if (role === 'taken' || role === 'blocked') {
          if (src && !r.enemies.has(src[i])) continue;
          if (role === 'taken') { taken += v; if (v === 0) blocked++; } else blocked++;
        } else if (role === 'heal') healed += v;
      } else if (e[0] === 10) { if (e[3] === 2) deaths++; else { if (isFoe(log, e[2])) kills++; dead.push(e[2]); } }
    }
    let killsConfirmed = 0;
    for (const a of dead) if (confirmedKill(log, a, hitT)) killsConfirmed++;
    for (const k of at.casts) if (inR(k.c, r) && (k.src !== 3 || k.resolved) && (!r.abilities || r.abilities.has(k.struct))) casts++;
    const durMs = durOf(r);
    const top = Object.keys(byAb).filter(k => k !== '0').map(k => [Number(k), byAb[k]]).sort((a, b) => b[1] - a[1]).slice(0, 5);
    const targets = Object.keys(byT).map(k => [Number(k), byT[k]]).sort((a, b) => b[1] - a[1]);
    return { durMs, dealt, taken, healed, dps: dealt / durMs * 1000, dpm: dealt / durMs * 60000, hits, crits, maxHit, blocked, deaths, kills, killsConfirmed, casts,
             topAbilities: top, targets, unattributed: byAb[0] || 0 };
  }
  // An NPC death that counts: it took a dealt hit in the selection and is not a conjure.
  function confirmedKill(log, a, hitT) {
    if (!hitT.has(a)) return false;
    const x = actorOf(log, a);
    return x.type === 'npc' && !CONJURE_IDS.has(x.id);
  }

  function byAbility(log, n) {
    const r = range(log, n), at = attribute(log), rows = {}, ev = log.events;
    let total = 0;
    const [lo, hi] = win(log, r);
    for (let i = lo; i < hi; i++) {
      const e = ev[i];
      if (e[0] !== 0 || !inR(e[1], r) || hitRole(log, e) !== 'dealt') continue;
      const s = at.struct[i], v = e[4] > 0 ? e[4] : 0;
      if (!passDealt(r, e, s)) continue;
      const row = rows[s] || (rows[s] = { struct: s, name: s ? ability(log, s).name : 'Unattributed', icon: s ? ability(log, s).icon : 0, hits: 0, crits: 0, max: 0, total: 0, casts: 0 });
      row.hits++; row.total += v; if (v > row.max) row.max = v; if (hmInfo(log, e[3]).crit) row.crits++;
      total += v;
    }
    for (const k of at.casts) if (inR(k.c, r) && (k.src !== 3 || k.resolved) && rows[k.struct]) rows[k.struct].casts++;
    const out = Object.keys(rows).map(k => rows[k]);
    for (const row of out) { row.avg = row.hits ? row.total / row.hits : 0; row.share = total ? row.total / total : 0; row.perCast = row.casts ? row.total / row.casts : 0; }
    out.sort((a, b) => (a.struct === 0) - (b.struct === 0) || b.total - a.total);
    return { rows: out, total };
  }

  function bySource(log, n) {
    const r = range(log, n), rows = {}, ev = log.events, srcs = sources(log);
    let total = 0;
    const [lo, hi] = win(log, r);
    for (let i = lo; i < hi; i++) {
      const e = ev[i];
      if (e[0] !== 0 || !inR(e[1], r)) continue;
      const role = hitRole(log, e);
      if (role !== 'taken' && role !== 'blocked') continue;
      const src = srcs[i];
      if (r.enemies && !r.enemies.has(src)) continue;
      const a = actorOf(log, src);
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
    const r = range(log, n), c = ctx(log), at = attribute(log), src = r.enemies ? sources(log) : null;
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
      else if (before || !inR(e[1], r)) continue;
      else if (e[0] === 0) {
        const role = hitRole(log, e);
        if (role === 'dealt') { if (passDealt(r, e, at.struct[i])) out.dealt.push([e[1], e[4] > 0 ? e[4] : 0, at.struct[i], e[2], hmInfo(log, e[3]).crit ? 1 : 0, i]); }
        else if (role === 'taken' || role === 'blocked') { if (!src || r.enemies.has(src[i])) out.taken.push([e[1], e[4] > 0 ? e[4] : 0, e[2], i]); }
      } else if (e[0] === 10) { if (e[3] === 2) out.deaths.push([e[1]]); else if (isFoe(log, e[2])) out.kills.push([e[1], e[2]]); }
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
  // A timer struct the game never draws on its buff bar: the dictionary row has neither a name nor an icon.
  function hiddenBuff(b) { return !!b && b.name === '' && !b.icon; }
  // ---- Inventory (container 93) and equipment (94): [20, c, container, slot, item, count], one row per slot change.
  const EQUIP_SLOTS = { 0: 'Head', 1: 'Back', 2: 'Neck', 3: 'Main hand', 4: 'Body', 5: 'Off-hand', 7: 'Legs', 9: 'Hands',
                        10: 'Feet', 12: 'Ring', 13: 'Ammo', 14: 'Aura', 17: 'Pocket' };
  function slotName(slot) { return EQUIP_SLOTS[slot] || 'Slot ' + slot; }
  function itemName(log, id) {
    const d = (log.dict && log.dict.items) || {}, x = own(d, id) ? d[id] : null;
    return x && typeof x.name === 'string' && x.name ? x.name : 'Item ' + id;
  }
  // Gear over a selection: what each equipment slot held and when (spans), every equipment change (swaps, with the
  // item it replaced), and what left the inventory (eaten, drunk, dropped or equipped). An item that leaves the
  // inventory in the tick it is equipped is part of the swap, not used up.
  function gear(log, n) {
    const r = range(log, n), ev = log.events, cur = {}, spans = {}, swaps = [], moves = [], invChanges = [];
    let startInv = null;
    const snapInv = () => {   // the 28 inventory slots as they stand now
      const out = [];
      for (let s = 0; s < 28; s++) { const o = cur[93 * 256 + s]; out.push(o && o.item >= 0 ? { slot: s, item: o.item, count: o.count, name: itemName(log, o.item) } : { slot: s, item: -1, count: 0, name: '' }); }
      return out;
    };
    const close = (slot, at) => { const o = cur[94 * 256 + slot]; if (o && o.item >= 0) { const a = Math.max(o.since, r.start), b = Math.min(at, r.end); if (b > a) (spans[slot] = spans[slot] || []).push({ from: a, to: b, item: o.item, name: itemName(log, o.item) }); } };
    for (const e of ev) {
      if (!Array.isArray(e) || e[0] !== 20) continue;
      if (e[1] > r.end) break;
      const cont = e[2], slot = e[3], key = cont * 256 + slot, prev = cur[key];
      if (e[1] > r.start && !startInv) startInv = snapInv();
      if (e[1] > r.start && cont === 93) {
        const was = prev ? prev.item : -1, wasN = prev ? prev.count : 0;
        if (was !== e[4] || wasN !== e[5]) invChanges.push({ c: e[1], slot, from: was, fromName: was >= 0 ? itemName(log, was) : '', fromCount: wasN,
                                                             to: e[4], toName: e[4] >= 0 ? itemName(log, e[4]) : '', toCount: e[5] });
      }
      if (e[1] > r.start) {
        if (cont === 94) {
          close(slot, e[1]);
          const was = prev ? prev.item : -1;   // a slot with no row yet was empty
          if (was !== e[4]) swaps.push({ c: e[1], slot, slotName: slotName(slot), from: was, fromName: was >= 0 ? itemName(log, was) : '', to: e[4], toName: e[4] >= 0 ? itemName(log, e[4]) : '' });
        } else if (cont === 93 && prev && prev.item >= 0) {
          // a stack going down; a potion or other dosed item losing a dose (Super restore (4) -> (3)); an item leaving
          const stem = id => itemName(log, id).replace(/ \(\d\)$/, '');
          if (prev.item === e[4] && e[5] < prev.count) moves.push({ c: e[1], item: prev.item, n: prev.count - e[5] });
          else if (prev.item !== e[4] && e[4] >= 0 && stem(prev.item) === stem(e[4]) && stem(e[4]) !== itemName(log, e[4])) moves.push({ c: e[1], item: prev.item, n: 1, dose: true });
          else if (prev.item !== e[4]) moves.push({ c: e[1], item: prev.item, n: Math.max(1, prev.count) });
        }
      }
      cur[key] = { item: e[4], count: e[5], since: e[1] };
    }
    for (const k in cur) if (Math.floor(Number(k) / 256) === 94) close(Number(k) % 256, r.end);
    // items that left the inventory: the ones equipped in the same tick are swaps
    const usedBy = {};
    for (const m of moves) {
      if (swaps.some(w => w.to === m.item && Math.abs(w.c - m.c) <= TICK)) continue;
      const nm = m.dose ? itemName(log, m.item).replace(/ \(\d\)$/, '') : itemName(log, m.item), key = m.dose ? 'dose:' + nm : m.item;
      const u = usedBy[key] || (usedBy[key] = { item: m.item, name: nm, unit: m.dose ? 'doses' : '', used: 0, times: [] });
      u.used += m.n; u.times.push(m.c);
    }
    const slots = Object.keys(spans).map(Number).sort((a, b) => a - b).map(slot => ({ slot, name: slotName(slot), spans: spans[slot] }));
    const used = Object.keys(usedBy).map(k => usedBy[k]).sort((a, b) => b.used - a.used || a.times[0] - b.times[0]);
    if (!startInv) startInv = snapInv();
    return { range: r, slots, swaps, used, startInv, invChanges, has: ev.some(e => Array.isArray(e) && e[0] === 20) };
  }

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
    const many = r.spans && r.spans.length > 1;
    let dur = Math.max(1, r.end - r.start);
    if (r.spans) { dur = 0; for (const sp of spansOf(r)) dur += sp[1] - sp[0]; dur = Math.max(1, dur); }
    const rows = [];
    for (const s in spans) {
      const list = spans[s].sort((a, b) => a[0] - b[0]);
      let merged = [];
      for (const sp of list) { const m = merged[merged.length - 1]; if (m && sp[0] <= m[1]) m[1] = Math.max(m[1], sp[1]); else merged.push(sp.slice()); }
      if (many) merged = clipSpans(merged, r.spans);
      if (!merged.length) continue;
      const total = merged.reduce((t, sp) => t + sp[1] - sp[0], 0);
      const b = c.buffs[s] || {};
      if (hiddenBuff(c.buffs[s])) continue;
      rows.push({ struct: Number(s), name: shortName(b.name || 'Buff ' + s), fullName: b.name || '', type: b.type || 0, icon: b.icon || 0, item: b.item || 0, spans: merged, uptime: total / dur });
    }
    rows.sort((a, b) => b.uptime - a.uptime || a.name.localeCompare(b.name));
    return { rows, range: r };
  }
  // Intersection of sorted spans with sorted selection spans; pieces of zero length are dropped.
  function clipSpans(list, sel) {
    const out = [];
    for (const sp of list) for (const w of sel) {
      const a = Math.max(sp[0], w[0]), b = Math.min(sp[1], w[1]);
      if (b > a) out.push([a, b]);
    }
    return out;
  }

  // Rotation: one entry per cast with its tick index from the fight start; idle ticks after the GCD.
  function casts(log, n) {
    const r = range(log, n), at = attribute(log), ab = byAbility(log, n);
    let ticks = Math.max(1, Math.ceil((r.end - r.start) / TICK));
    if (r.spans) { ticks = 0; for (const sp of spansOf(r)) ticks += Math.ceil((sp[1] - sp[0]) / TICK); ticks = Math.max(1, ticks); }
    const list = [];
    for (const k of at.casts) {
      if (!inR(k.c, r) || (r.abilities && !r.abilities.has(k.struct))) continue;
      const a = ability(log, k.struct), shape = k.shape;
      list.push({ c: k.c, tick: Math.floor((k.c - r.start) / TICK), struct: k.struct, name: a.name, icon: a.icon || 0, src: k.src, resolved: k.resolved, style: a.style || '',
                  span: a.channel ? Math.max(1, (a.channel[0] || 1) * (a.channel[1] || 1) + 1) : (shape.length > 3 ? shape[shape.length - 1] + 1 : 1) });
    }
    let idle = 0, prev = null, prevSpan = -1;
    for (const k of list) {
      const sp = r.spans ? spanAt(k.c, r) : 0;
      if (prev != null && sp === prevSpan && k.tick - prev > 3) idle += k.tick - prev - 3;
      prev = k.tick; prevSpan = sp;
    }
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
    const r = range(log, n), out = {}, ev = log.events, at = r.targets || r.abilities ? attribute(log) : null;
    let total = 0;
    for (const s of STYLES) out[s] = 0;
    const [lo, hi] = win(log, r);
    for (let i = lo; i < hi; i++) {
      const e = ev[i];
      if (e[0] !== 0 || !inR(e[1], r) || hitRole(log, e) !== 'dealt') continue;
      if (at && !passDealt(r, e, at.struct[i])) continue;
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

  // Event index -> position in attribute().casts (the first cast of that row).
  function castPos(log) {
    const c = ctx(log);
    if (c.castAt) return c.castAt;
    const m = new Map(), L = attribute(log).casts;
    for (let k = 0; k < L.length; k++) if (!m.has(L[k].i)) m.set(L[k].i, k);
    c.castAt = m;
    return m;
  }

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
        if (role === 'taken' || role === 'blocked') { const s = sources(log)[i]; if (s >= 0) row.text += ' by ' + actorLabel(log, s); }
        if (e[5] >= 0) row.text += ' (+' + e[6] + ' soaked)';
        break;
      }
      case 1: { const p = castPos(log).get(i), ki = p == null ? -1 : p, s = ki >= 0 ? at.casts[ki].struct : e[2]; row.ability = ability(log, s).name; row.kind = ki < 0 ? 'paired stamp' : (['exact', 'cooldown', 'animation', 'gcd'][e[4]] || ''); row.text = (ki < 0 ? 'paired stamp ' + row.ability : e[4] === 3 && !at.casts[ki].resolved ? 'global cooldown' : 'cast ' + row.ability) + (e[3] > 0 ? ', ready in ' + fmtMs((e[3] - e[1]) * CYCLE_MS) : ''); break; }
      case 2: { const nm = c.seqs[e[3]]; row.actor = actorLabel(log, e[2]); row.value = e[3]; row.text = e[3] < 0 ? 'animation ends' : 'animation ' + (nm || e[3]) + (!nm && (seqTag(log, e[3]) & 1) ? ' (attack)' : ''); break; }
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
      case 20: row.value = e[4]; row.ability = e[4] >= 0 ? itemName(log, e[4]) : ''; row.kind = e[2] === 94 ? 'equipment' : 'inventory';
        row.text = (e[2] === 94 ? slotName(e[3]) + ': ' : 'inventory slot ' + (e[3] + 1) + ': ') + (e[4] >= 0 ? row.ability + (e[5] > 1 ? ' x' + e[5] : '') : 'empty'); break;
      default: row.text = JSON.stringify(e.slice(2));
    }
    return row;
  }

  // Per-fight summary rows for an index.
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
      if (!inR(e[1], r) || !isMech(log, e)) continue;
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

  // ---- Per-actor indexes (event indexes in log order), built once per log.
  function actorIdx(log) {
    const c = ctx(log);
    if (c.byA) return c.byA;
    const lp = new Map(), anim = new Map(), tgt = new Map(), lpTop = new Map(), ev = log.events;
    const push = (m, a, i) => { let l = m.get(a); if (!l) m.set(a, l = []); l.push(i); };
    for (let i = 0; i < ev.length; i++) {
      const e = ev[i];
      if (!Array.isArray(e)) continue;
      if (e[0] === 4) { push(lp, e[2], i); if (isNum(e[4]) && e[4] > (lpTop.get(e[2]) || 0)) lpTop.set(e[2], e[4]); }
      else if (e[0] === 2) push(anim, e[2], i);
      else if (e[0] === 3) push(tgt, e[2], i);
    }
    c.byA = { lp, anim, tgt, lpTop };
    return c.byA;
  }
  // An actor's life point maximum: the actor row's, else the largest LP row maximum.
  function lpMaxOf(log, a) {
    const x = actorOf(log, a);
    return isNum(x.lpMax) && x.lpMax > 0 ? x.lpMax : (actorIdx(log).lpTop.get(a) || 0);
  }
  // Life points the actors lost in the spans: positive drops between consecutive LP rows, the baseline being
  // the row before each span.
  function lpLostIn(log, actors, spans, per) {
    const ev = log.events, idx = actorIdx(log).lp, end = spans.length ? spans[spans.length - 1][1] : -Infinity;
    const r = { spans, start: spans.length ? spans[0][0] : 0, end };
    let total = 0;
    for (const a of actors) {
      let prev = null, lost = 0;
      for (const i of idx.get(a) || []) {
        const e = ev[i];
        if (e[1] > end) break;
        if (!isNum(e[3]) || e[3] < 0) continue;
        if (prev != null && prev > e[3] && inR(e[1], r)) lost += prev - e[3];
        prev = e[3];
      }
      if (per) per.set(a, lost);
      total += lost;
    }
    return total;
  }

  // ---- Fights: boss, result, pull, phases and the ranking metrics.
  function bossGroupKey(name) {
    if (name == null) return '';
    let s = String(name);
    const bar = s.indexOf('|');
    if (bar >= 0) s = s.slice(0, bar);
    s = s.toLowerCase();
    const cut = s.indexOf(', ');
    if (cut >= 0) s = s.slice(0, cut);
    return s.trim();
  }
  function fightResult(kind) { return kind === 'boss' ? 'kill' : kind === 'encounter' ? 'wipe' : kind === 'kills' ? 'trash' : 'ended'; }
  // NPC actors you dealt a hit to inside fight n (ascending index).
  function hitNpcs(log, n) {
    const f = log.fights[n], ev = log.events, r = { start: f.start, end: fightEnd(log, f) }, H = new Set();
    const [lo, hi] = win(log, r);
    for (let i = lo; i < hi; i++) {
      const e = ev[i];
      if (e[0] !== 0 || !inR(e[1], r) || hitRole(log, e) !== 'dealt') continue;
      if (actorOf(log, e[2]).type === 'npc') H.add(e[2]);
    }
    return H;
  }
  function bossActors(log, n) {
    const fights = log.fights || [], f = Number.isInteger(n) ? fights[n] : null;
    if (!f) return { actors: [], by: '' };
    const c = ctx(log);
    c.bossA = c.bossA || new Map();
    const hit = c.bossA.get(n);
    if (hit) return { actors: hit.actors.slice(), by: hit.by };
    const H = Array.from(hitNpcs(log, n)).sort((a, b) => a - b);
    let res = { actors: [], by: '' };
    if (H.length) {
      const ev = log.events, r = { start: f.start, end: fightEnd(log, f) }, mb = new Set();
      const [lo, hi] = win(log, r);
      for (let i = lo; i < hi; i++) if (inR(ev[i][1], r) && isMech(log, ev[i])) mb.add(Number(ev[i][2]));
      const nm = a => { const x = actorOf(log, a); return typeof x.name === 'string' ? x.name : ''; };
      let pick = H.filter(a => mb.has(actorOf(log, a).id));
      if (pick.length) {
        const names = new Set(pick.map(nm).filter(Boolean));
        res = { actors: H.filter(a => pick.indexOf(a) >= 0 || names.has(nm(a))), by: 'mech' };
      } else {
        if (typeof f.boss === 'string') {
          const keys = new Set();
          for (const p of String(f.boss).split('|')[0].split(' & ')) {
            keys.add(p.trim().toLowerCase());
            const cut = p.indexOf(', ');
            if (cut >= 0) keys.add(p.slice(0, cut).trim().toLowerCase());
          }
          keys.delete('');
          const M = H.filter(a => keys.has(nm(a).trim().toLowerCase()));
          if (M.length) {
            const ids = new Set(M.map(a => actorOf(log, a).id));
            res = { actors: H.filter(a => M.indexOf(a) >= 0 || ids.has(actorOf(log, a).id)), by: 'name' };
          }
        }
        if (!res.actors.length) {
          let top = -Infinity;
          for (const a of H) top = Math.max(top, lpMaxOf(log, a));
          res = { actors: H.filter(a => lpMaxOf(log, a) === top), by: 'lp' };
        }
      }
    }
    c.bossA.set(n, res);
    return { actors: res.actors.slice(), by: res.by };
  }
  // Boss actors of every fight the selection touches.
  function bossesOf(log, r) {
    const s = new Set();
    for (const k of fightsIn(log, r)) for (const a of bossActors(log, k).actors) s.add(a);
    return Array.from(s).sort((a, b) => a - b);
  }
  // The kill as the game times it: from the boss's spawn (its last arrival in the scene before it died) to its
  // death (life points at 0, or its death row). Several boss actors: the first spawn to the last death. Null
  // when the log saw neither, and the fight's own span is used instead.
  function killWindow(log, n, actors) {
    const f = (log.fights || [])[n];
    if (!f || !actors || !actors.length) return null;
    const ev = log.events, end = fightEnd(log, f), set = new Set(actors), death = new Map(), arrive = new Map();
    for (const e of ev) {
      if (!Array.isArray(e) || !set.has(e[2])) continue;
      if (e[1] > end) break;
      if (e[0] === 11 && e[3]) { if (!death.has(e[2])) arrive.set(e[2], e[1]); }
      else if (e[1] >= f.start && !death.has(e[2]) && ((e[0] === 4 && e[3] === 0) || (e[0] === 10 && e[3] !== 2))) death.set(e[2], e[1]);
    }
    let from = Infinity, to = -Infinity;
    for (const [a, d] of death) {
      let s0 = arrive.get(a);
      if (!isNum(s0)) { const x = actorOf(log, a); s0 = isNum(x.first) ? x.first : null; }
      if (!isNum(s0) || s0 > d) continue;
      from = Math.min(from, s0); to = Math.max(to, d);
    }
    return isFinite(from) && isFinite(to) && to > from ? { from, to } : null;
  }
  // A fight's result: a boss fight is a kill, an encounter fight a wipe, unless the boss itself (by name or by its
  // mechanics) never took part, as with the adds killed after the boss died: then it is trash.
  function resultOf(log, k) {
    const f = (log.fights || [])[k];
    if (!f) return 'trash';
    let r = fightResult(f.kind);
    if (r === 'wipe') { const b0 = bossActors(log, k); if (b0.by !== 'name' && b0.by !== 'mech') r = 'trash'; }
    return r;
  }
  function fightInfo(log, n) {
    const fights = log.fights || [], f = Number.isInteger(n) ? fights[n] : null;
    if (!f) return null;
    const c = ctx(log);
    if (!c.groups) c.groups = fights.map(x => bossGroupKey(x && x.boss));
    const kind = f.kind, boss = f.boss == null ? null : String(f.boss);
    const result = resultOf(log, n);
    const bar = boss == null ? -1 : boss.indexOf('|');
    const bossName = boss == null ? null : (bar >= 0 ? boss.slice(0, bar) : boss), mode = bar >= 0 ? boss.slice(bar + 1) : '';
    const group = c.groups[n];
    let pull = 0;
    if (result !== 'trash' && group) for (let k = 0; k <= n; k++) if (c.groups[k] === group && resultOf(log, k) !== 'trash') pull++;
    const end = fightEnd(log, f), ba = bossActors(log, n);
    let bossPct = null;
    if (result === 'kill') bossPct = 0;
    else if (result !== 'trash') {
      const ev = log.events, idx = actorIdx(log).lp;
      let best = -1;
      for (const a of ba.actors) {
        const L = idx.get(a) || [];
        let lo = 0, hi = L.length;
        while (lo < hi) { const m = (lo + hi) >> 1; if (ev[L[m]][1] <= end) lo = m + 1; else hi = m; }
        if (lo > 0 && (best < 0 || ev[L[lo - 1]][1] > ev[best][1] || (ev[L[lo - 1]][1] === ev[best][1] && L[lo - 1] > best))) best = L[lo - 1];
      }
      if (best >= 0) {
        const e = ev[best], mx = isNum(e[4]) && e[4] > 0 ? e[4] : lpMaxOf(log, e[2]);
        if (mx > 0 && isNum(e[3]) && e[3] >= 0) bossPct = Math.round(e[3] / mx * 1000) / 10;
      }
    }
    const kw = result === 'kill' ? killWindow(log, n, ba.actors) : null;
    return { n, kind, result, boss, bossName, group, mode, pull, startMs: cycleMs(log, f.start), durMs: Math.max(1, (end - f.start) * CYCLE_MS),
             killMs: result === 'kill' ? (kw ? Math.max(1, Math.round((kw.to - kw.from) / TICK)) * TICK * CYCLE_MS : Math.max(1, (end - f.start) * CYCLE_MS)) : null, killWindow: kw,   // whole ticks, as the game counts
             bossActors: ba.actors, bossBy: ba.by, bossPct, live: f.live === true };
  }
  function phases(log, n) {
    const fights = log.fights || [], f = Number.isInteger(n) ? fights[n] : null;
    if (!f) return [];
    const start = f.start, end = fightEnd(log, f), one = [{ k: 1, start, end, label: 'Phase 1' }];
    if (f.kind === 'kills') return one;
    const ev = log.events, ba = new Set(bossActors(log, n).actors), cuts = [];
    let prev = null;
    const [lo, hi] = win(log, { start: -Infinity, end });
    const seenIds = new Set();
    for (let i = lo; i < hi; i++) {
      const e = ev[i];
      if (e[1] > end) continue;
      if (e[0] === 12 && isNum(e[2]) && e[2] >= 0) {
        if (e[1] > start && prev != null && e[2] !== prev) cuts.push(e[1]);
        prev = e[2];
      } else if (e[0] === 0 && e[1] >= start && ba.has(e[2]) && hitRole(log, e) === 'dealt') {
        const id = actorOf(log, e[2]).id;
        if (!seenIds.has(id)) { if (seenIds.size && e[1] > start + 3 * TICK) cuts.push(e[1]); seenIds.add(id); }
      }
    }
    const pts = Array.from(new Set(cuts)).filter(x => x > start && x < end).sort((a, b) => a - b);
    if (!pts.length) return one;
    const segs = [];
    let s0 = start;
    for (const p of pts.concat([end])) { segs.push([s0, p]); s0 = p; }
    const out = [];
    for (const sg of segs) {
      if (out.length && sg[1] - sg[0] < 3 * TICK) out[out.length - 1][1] = sg[1];
      else out.push(sg.slice());
    }
    if (out.length > 1 && out[0][1] - out[0][0] < 3 * TICK) { out[1][0] = out[0][0]; out.shift(); }
    return out.map((sg, k) => ({ k: k + 1, start: sg[0], end: sg[1], label: 'Phase ' + (k + 1) }));
  }
  function round4(x) { return Math.round(x * 10000) / 10000; }
  function styleOf(log, sel) {
    const ss = styleSplit(log, sel), t = ss.total, sp = ss.split;
    const raw = { melee: sp.melee, ranged: sp.ranged, magic: sp.magic, necromancy: sp.necromancy + sp.conjure, typeless: sp.typeless, poison: sp.poison };
    const shares = {};
    for (const k in raw) shares[k] = t ? round4(raw[k] / t) : 0;
    const styled = raw.melee + raw.ranged + raw.magic + raw.necromancy;
    let style = 'hybrid';
    if (styled > 0) { for (const k of ['melee', 'ranged', 'magic', 'necromancy']) if (raw[k] >= STYLE_LINE * styled) style = k; }
    else {
      const r = range(log, sel), cnt = { melee: 0, ranged: 0, magic: 0, necromancy: 0 };
      for (const k of attribute(log).casts) {
        if (!inR(k.c, r) || !(k.src !== 3 || k.resolved) || (r.abilities && !r.abilities.has(k.struct))) continue;
        const st = ability(log, k.struct).style;
        if (own(cnt, st)) cnt[st]++;
      }
      let best = 0;
      for (const k of ['melee', 'ranged', 'magic', 'necromancy']) if (cnt[k] > best) { best = cnt[k]; style = k; }
    }
    return { style, shares };
  }
  // Failed memory reads as a share of read attempts. A log without the attempt count estimates it from its length
  // (the recorder reads 5 times a second).
  function readFailShare(L) {
    if (!isObj(L) || !isNum(L.readFails) || L.readFails <= 0) return 0;
    if (isNum(L.reads) && L.reads > 0) return L.readFails / L.reads;
    const span = isNum(L.endedAt) && isNum(L.startedAt) ? L.endedAt - L.startedAt : 0;
    return L.readFails / Math.max(1, span / 200);
  }
  function metrics(log, n) {
    const fi = fightInfo(log, n);
    if (!fi) return null;
    // a kill is measured over its kill window (spawn to death), so damage on adds before the boss appears or
    // after it died does not count toward the kill's DPS
    const kill = fi.result === 'kill', kw = kill ? fi.killWindow : null;
    const f = log.fights[n], sel = kw ? { fights: [n], from: kw.from, to: kw.to } : { fights: [n] };
    const s = summary(log, sel), rot = rotation(log, sel), so = styleOf(log, sel);
    const start = kw ? Math.max(f.start, kw.from) : f.start, end = kw ? Math.min(fightEnd(log, f), kw.to) : fightEnd(log, f);
    const r = { start, end }, ev = log.events;
    const durMs = fi.durMs, killMs = kill ? fi.killMs : null, dps = s.dealt / (kill ? Math.max(1, s.durMs) : durMs) * 1000;
    const ba = new Set(fi.bossActors);
    let bossDealt = 0, bossMaxHit = 0, otherHits = 0, gap = false, bossLp = false;
    const [lo, hi] = win(log, r);
    for (let i = lo; i < hi; i++) {
      const e = ev[i];
      if (!inR(e[1], r)) continue;
      if (e[0] === 0) {
        const role = hitRole(log, e), v = e[4] > 0 ? e[4] : 0;
        if (role === 'other') otherHits++;
        else if (role === 'dealt' && ba.has(e[2])) { bossDealt += v; if (v > bossMaxHit) bossMaxHit = v; }
      } else if (e[0] === 16 && e[2] === 2) gap = true;
      else if (e[0] === 4 && ba.has(e[2])) bossLp = true;
    }
    let players = 0;
    for (const a of log.actors || []) if (a && a.type === 'player' && (!isNum(a.first) || a.first <= end) && (!isNum(a.last) || a.last >= start)) players++;
    const solo = players === 0 && otherHits === 0, size = solo ? 'solo' : players === 1 ? 'duo' : 'group';
    const out = { n, result: fi.result, boss: fi.boss, bossName: fi.bossName, mode: fi.mode, style: so.style, styleShares: so.shares, durMs, killMs,
                  dealt: s.dealt, dps, bossDealt: null, bossDps: null, lpLost: null, bossShare: null, bossLpMax: null, maxHit: s.maxHit, deaths: s.deaths,
                  players, otherHits, solo, size, cpm: rot.cpm, activePct: rot.activePct, eligible: false, reasons: [] };
    if (!kill) { out.reasons = ['not_kill']; return out; }
    const lpLost = lpLostIn(log, fi.bossActors, [[start, end]]);
    let bossLpMax = 0;
    for (const a of fi.bossActors) bossLpMax = Math.max(bossLpMax, lpMaxOf(log, a));
    out.bossDealt = bossDealt; out.bossDps = bossDealt / killMs * 1000; out.lpLost = lpLost;
    out.bossShare = lpLost > 0 ? Math.min(1, bossDealt / lpLost) : null; out.bossLpMax = bossLpMax;
    const L = log.log || {}, reasons = [];
    if (gap) reasons.push('gap');
    if (readFailShare(L) > 0.01) reasons.push('read_fails');
    if (durMs < MIN_FIGHT_MS) reasons.push('too_short');
    if (durMs > MAX_FIGHT_MS) reasons.push('too_long');
    if (!bossLp) reasons.push('no_lp');
    if (bossDealt > PLAUSIBLE_RATIO * lpLost + bossMaxHit) reasons.push('implausible_damage');
    if (dps > DPS_CAP) reasons.push('dps_cap');
    if (s.maxHit > MAX_HIT_CAP) reasons.push('max_hit');
    out.reasons = reasons;
    out.eligible = !reasons.length;
    return out;
  }
  // Your dealt hits in fight n: [[c - start, hitmark, value], ...] (the input of a fight fingerprint).
  function fightHits(log, n) {
    const f = (log.fights || [])[n];
    if (!f) return [];
    const ev = log.events, r = { start: f.start, end: fightEnd(log, f) }, out = [];
    const [lo, hi] = win(log, r);
    for (let i = lo; i < hi; i++) {
      const e = ev[i];
      if (e[0] === 0 && inR(e[1], r) && hitRole(log, e) === 'dealt') out.push([e[1] - f.start, e[3], e[4]]);
    }
    return out;
  }

  // ---- Tables for the report page.
  function byTarget(log, sel, opts) {
    const r = range(log, sel), at = attribute(log), ev = log.events, byId = !!(opts && opts.byId);
    const rows = new Map(), hitT = new Set(), dead = [];
    let total = 0;
    const [lo, hi] = win(log, r);
    for (let i = lo; i < hi; i++) {
      const e = ev[i];
      if (!inR(e[1], r)) continue;
      if (e[0] === 10) { if (e[3] !== 2) dead.push(e[2]); continue; }
      if (e[0] !== 0 || hitRole(log, e) !== 'dealt') continue;
      hitT.add(e[2]);
      const s = at.struct[i];
      if (!passDealt(r, e, s)) continue;
      const a = actorOf(log, e[2]), key = byId && a.type === 'npc' && isNum(a.id) && a.id >= 0 ? 'id' + a.id : 'a' + e[2];
      let row = rows.get(key);
      if (!row) rows.set(key, row = { actor: key.charAt(0) === 'i' ? -1 : e[2], name: actorName(log, e[2]), id: isNum(a.id) ? a.id : -1, hits: 0, crits: 0, max: 0, total: 0,
                                       share: 0, activeMs: 0, dps: 0, kills: 0, abilities: [], first: e[1], last: e[1], ab: new Map(), members: new Set() });
      const v = e[4] > 0 ? e[4] : 0;
      row.hits++; row.total += v; if (v > row.max) row.max = v; if (hmInfo(log, e[3]).crit) row.crits++;
      row.last = e[1]; row.members.add(e[2]);
      row.ab.set(s, (row.ab.get(s) || 0) + v);
      total += v;
    }
    const kills = new Map();
    for (const a of dead) if (confirmedKill(log, a, hitT)) kills.set(a, (kills.get(a) || 0) + 1);
    const out = [];
    for (const row of rows.values()) {
      row.activeMs = Math.max(600, (row.last - row.first) * CYCLE_MS);
      row.dps = row.total / row.activeMs * 1000;
      row.share = total ? row.total / total : 0;
      for (const m of row.members) row.kills += kills.get(m) || 0;
      row.abilities = Array.from(row.ab.entries()).sort((a, b) => b[1] - a[1]).slice(0, 5);
      out.push({ actor: row.actor, name: row.name, id: row.id, hits: row.hits, crits: row.crits, max: row.max, total: row.total, share: row.share,
                 activeMs: row.activeMs, dps: row.dps, kills: row.kills, abilities: row.abilities });
    }
    out.sort((a, b) => b.total - a.total);
    return { rows: out, total };
  }
  function bossShare(log, sel) {
    const r = range(log, sel), ev = log.events, actors = bossesOf(log, r), per = new Map(), sp = spansOf(r);
    const lpLost = lpLostIn(log, actors, sp, per), yours = new Map(), set = new Set(actors);
    const [lo, hi] = win(log, r);
    for (let i = lo; i < hi; i++) {
      const e = ev[i];
      if (e[0] === 0 && set.has(e[2]) && inR(e[1], r) && hitRole(log, e) === 'dealt') yours.set(e[2], (yours.get(e[2]) || 0) + (e[4] > 0 ? e[4] : 0));
    }
    let you = 0;
    const rows = actors.map(a => {
      const l = per.get(a) || 0, y = yours.get(a) || 0;
      you += y;
      return { actor: a, name: actorName(log, a), lpLost: l, yours: y, others: Math.max(0, l - y) };
    });
    return { rows, lpLost, yours: you, others: Math.max(0, lpLost - you), share: lpLost > 0 ? you / lpLost : null };
  }
  // Labels for attack animations: the mechanic label of an animation cue with that id, else numbered per source.
  function mechSeqLabels(log) {
    const c = ctx(log);
    if (c.mechSeq) return c.mechSeq;
    const m = new Map();
    for (const e of log.events) {
      if (!isMech(log, e)) continue;
      const info = mechInfo(log, e[2], e[3], e[4]), id = Number(e[5]);
      if (info.kind === 1 && !m.has(id)) m.set(id, info.label);
    }
    c.mechSeq = m;
    return m;
  }
  // The latest animation of actor a within 2 ticks before cycle S (-1 none).
  function animBefore(log, a, S) {
    const L = actorIdx(log).anim.get(a), ev = log.events;
    if (!L) return -1;
    let lo = 0, hi = L.length;
    while (lo < hi) { const m = (lo + hi) >> 1; if (ev[L[m]][1] <= S) lo = m + 1; else hi = m; }
    if (!lo) return -1;
    const e = ev[L[lo - 1]];
    return S - e[1] <= 2 * TICK && isNum(e[3]) && e[3] >= 0 ? e[3] : -1;
  }
  function takenBy(log, sel) {
    const r = range(log, sel), ev = log.events, srcs = sources(log), labels = mechSeqLabels(log), rows = new Map();
    let total = 0;
    const [lo, hi] = win(log, r);
    for (let i = lo; i < hi; i++) {
      const e = ev[i];
      if (e[0] !== 0 || !inR(e[1], r)) continue;
      const role = hitRole(log, e);
      if (role !== 'taken' && role !== 'blocked') continue;
      const src = srcs[i];
      if (r.enemies && !r.enemies.has(src)) continue;
      let row = rows.get(src);
      if (!row) {
        const a = actorOf(log, src);
        rows.set(src, row = { actor: src, name: src >= 0 ? actorName(log, src) : 'Unknown', id: src >= 0 && isNum(a.id) ? a.id : -1, hits: 0, blocked: 0, max: 0, total: 0, share: 0, subs: [], subM: new Map(), k: 0 });
      }
      const seq = src >= 0 ? animBefore(log, src, e[1]) : -1;
      let sub = row.subM.get(seq);
      if (!sub) {
        const label = seq < 0 ? 'Unknown' : (labels.get(seq) || (row.name + ' attack ' + (++row.k)));
        row.subM.set(seq, sub = { seq, label, hits: 0, blocked: 0, total: 0, max: 0 });
        row.subs.push(sub);
      }
      const v = e[4] > 0 ? e[4] : 0;
      if (role === 'blocked' || v === 0) { row.blocked++; sub.blocked++; }
      else { row.hits++; row.total += v; if (v > row.max) row.max = v; sub.hits++; sub.total += v; if (v > sub.max) sub.max = v; }
      total += v;
    }
    const out = [];
    for (const row of rows.values()) {
      row.share = total ? row.total / total : 0;
      out.push({ actor: row.actor, name: row.name, id: row.id, hits: row.hits, blocked: row.blocked, max: row.max, total: row.total, share: row.share,
                 subs: row.subs.slice().sort((a, b) => b.total - a.total || b.hits - a.hits) });
    }
    out.sort((a, b) => b.total - a.total);
    return { rows: out, total };
  }
  function enemyCasts(log, sel) {
    const r = range(log, sel), ev = log.events, boss = new Set(bossesOf(log, r)), labels = mechSeqLabels(log);
    const known = new Map(), next = new Map();
    for (const row of takenBy(log, sel).rows) {
      let k = 0;
      for (const s of row.subs) { known.set(row.actor + ':' + s.seq, s.label); if (s.seq >= 0 && !labels.has(s.seq)) k++; }
      next.set(row.actor, k);
    }
    const out = [];
    const [lo, hi] = win(log, r);
    for (let i = lo; i < hi; i++) {
      const e = ev[i];
      if (e[0] !== 2 || !boss.has(e[2]) || !inR(e[1], r) || !isNum(e[3]) || e[3] < 0 || !(seqTag(log, e[3]) & 1)) continue;
      const key = e[2] + ':' + e[3];
      let label = labels.get(e[3]) || known.get(key);
      if (!label) { const k = (next.get(e[2]) || 0) + 1; next.set(e[2], k); label = actorName(log, e[2]) + ' attack ' + k; known.set(key, label); }
      out.push({ c: e[1], actor: e[2], seq: e[3], label });
    }
    for (const u of mechs(log, sel).uses) out.push({ c: u.c, actor: u.actor, seq: -1, label: u.label });
    return out.sort((a, b) => a.c - b.c);
  }
  function targetsOf(log, sel, actors) {
    const r = range(log, sel), ev = log.events, self = ctx(log).self, idx = actorIdx(log).tgt, sp = spansOf(r);
    const list = Array.isArray(actors) ? actors : bossesOf(log, r);
    const who = t => t === self ? 'self' : (isNum(t) && t >= 0 && actorOf(log, t).type === 'player' ? t : -1);
    return list.map(a => {
      const rows = (idx.get(a) || []).map(i => ev[i]).filter(e => e[1] <= r.end);
      const out = [];
      for (let k = 0; k < rows.length; k++) {
        const c0 = rows[k][1], c1 = k + 1 < rows.length ? rows[k + 1][1] : r.end, w = who(rows[k][3]);
        for (const s of sp) {
          const a0 = Math.max(c0, s[0]), b0 = Math.min(c1, s[1]);
          if (b0 <= a0) continue;
          const m = out[out.length - 1];
          if (m && m[2] === w && m[1] === a0) m[1] = b0; else out.push([a0, b0, w]);
        }
      }
      return { actor: a, spans: out };
    });
  }
  function healing(log, sel) {
    const r = range(log, sel), ev = log.events, c = ctx(log), rows = new Map(), series = [], healTicks = new Set();
    let total = 0, count = 0, max = 0;
    for (const e of ev) if (e[0] === 0 && hitRole(log, e) === 'heal') healTicks.add(cycleTick(log, e[1]));
    const [lo, hi] = win(log, r);
    for (let i = lo; i < hi; i++) {
      const e = ev[i];
      if (e[0] !== 0 || !inR(e[1], r) || hitRole(log, e) !== 'heal') continue;
      const v = e[4] > 0 ? e[4] : 0;
      total += v; count++; if (v > max) max = v;
      series.push([e[1], v]);
      let row = rows.get(e[3]);
      if (!row) rows.set(e[3], row = { hitmark: e[3], kind: hmInfo(log, e[3]).kind, count: 0, total: 0, max: 0 });
      row.count++; row.total += v; if (v > row.max) row.max = v;
    }
    let regen = 0, prev = null;
    for (const i of actorIdx(log).lp.get(c.self) || []) {
      const e = ev[i];
      if (e[1] > r.end) break;
      if (!isNum(e[3]) || e[3] < 0) continue;
      if (prev != null && prev > 0 && e[3] > prev && inR(e[1], r)) {
        const t = cycleTick(log, e[1]);
        if (!healTicks.has(t) && !healTicks.has(t - 1)) regen += e[3] - prev;
      }
      prev = e[3];
    }
    return { total, count, max, rows: Array.from(rows.values()).sort((a, b) => b.total - a.total), series, regen };
  }
  function deaths(log, sel) {
    const r = range(log, sel), ev = log.events, hitT = new Set(), rows = [];
    const [lo, hi] = win(log, r);
    for (let i = lo; i < hi; i++) {
      const e = ev[i];
      if (!inR(e[1], r)) continue;
      if (e[0] === 0 && hitRole(log, e) === 'dealt') hitT.add(e[2]);
      else if (e[0] === 10) rows.push(i);
    }
    const out = [];
    for (const i of rows) {
      const e = ev[i], self = e[3] === 2;
      if (!self && !confirmedKill(log, e[2], hitT)) continue;
      out.push({ i, c: e[1], actor: e[2], self, how: e[3], name: self ? 'You' : actorName(log, e[2]) });
    }
    return out;
  }
  function deathRecap(log, i, windowCycles) {
    const ev = log.events, e0 = ev[i];
    if (!Array.isArray(e0)) return null;
    const w = isNum(windowCycles) && windowCycles > 0 ? windowCycles : 500, c = e0[1], from = c - w, cx = ctx(log), srcs = sources(log), at = attribute(log);
    const uses = mechs(log, -1).uses.map(u => [u.c, u.label]);
    const mechNear = hc => { let best = null; for (const u of uses) { if (u[0] > hc) break; if (hc - u[0] <= 3 * TICK) best = u[1]; } return best; };
    const out = { c, from, killingBlow: null, hits: [], heals: [], lp: [], adren: [], prayer: [], buffs: [], casts: [] };
    const base = { lp: null, ad: null, pr: null };
    const open = new Map();
    const [, hi] = win(log, { start: from, end: c });
    for (let k = 0; k < hi; k++) {
      const e = ev[k];
      if (e[1] > c) break;
      const inside = e[1] >= from;
      if (e[0] === 7) {
        const s = e[2];
        if (e[3]) { const o = open.get(s); if (o) { if (e[5] > 0) o.end = e[5]; } else open.set(s, { start: e[1], end: e[5] > 0 ? e[5] : Infinity }); }
        else open.delete(s);
      }
      if (e[0] === 4 && e[2] === cx.self) { if (inside) out.lp.push([e[1], e[3], e[4]]); else base.lp = [from, e[3], e[4]]; }
      else if (e[0] === 5) { if (inside) out.adren.push([e[1], e[2]]); else base.ad = [from, e[2]]; }
      else if (e[0] === 6) { if (inside) out.prayer.push([e[1], e[2], e[3]]); else base.pr = [from, e[2], e[3]]; }
      else if (e[0] === 0 && inside) {
        const role = hitRole(log, e);
        if (role === 'taken' || role === 'blocked') {
          const s = srcs[k], h = hmInfo(log, e[3]);
          out.hits.push({ i: k, c: e[1], value: e[4] > 0 ? e[4] : 0, source: s, sourceName: s >= 0 ? actorName(log, s) : 'Unknown', hitmark: e[3], kind: h.kind, mech: mechNear(e[1]) });
        } else if (role === 'heal') out.heals.push({ i: k, c: e[1], value: e[4] > 0 ? e[4] : 0 });
      }
    }
    if (base.lp) out.lp.unshift(base.lp);
    if (base.ad) out.adren.unshift(base.ad);
    if (base.pr) out.prayer.unshift(base.pr);
    for (let k = out.hits.length - 1; k >= 0; k--) if (out.hits[k].value > 0) { out.killingBlow = out.hits[k]; break; }
    for (const [s, o] of open) {
      if (o.end < c) continue;
      const b = own(cx.buffs, s) && isObj(cx.buffs[s]) ? cx.buffs[s] : {};
      if (hiddenBuff(own(cx.buffs, s) ? cx.buffs[s] : null)) continue;
      out.buffs.push({ struct: Number(s), name: shortName(b.name || 'Buff ' + s), type: isNum(b.type) ? b.type : 0, since: o.start });
    }
    for (const k of at.casts) if (k.c >= from && k.c <= c && (k.src !== 3 || k.resolved)) out.casts.push({ c: k.c, struct: k.struct, name: ability(log, k.struct).name });
    return out;
  }
  // Bin layout over the selection: bins of binMs from r.start; at most 50000 bins.
  function binsOf(r, binMs) {
    let b = isNum(binMs) && binMs > 0 ? binMs : 1000;
    const span = Math.max(0, r.end - r.start) * CYCLE_MS;
    if (span / b > 50000) b = Math.ceil(span / 50000);
    const binC = b / CYCLE_MS, bins = Math.max(1, Math.floor((r.end - r.start) / binC) + 1);
    const live = new Array(bins).fill(false);
    for (const sp of spansOf(r)) {
      const a = Math.max(0, Math.floor((sp[0] - r.start) / binC)), z = Math.min(bins - 1, Math.floor((sp[1] - r.start) / binC));
      for (let k = a; k <= z; k++) live[k] = true;
    }
    return { binMs: b, binC, bins, live };
  }
  function resources(log, sel, binMs) {
    const r = range(log, sel), ev = log.events, self = ctx(log).self, B = binsOf(r, binMs), sp = spansOf(r);
    const pts = { adren: [], prayer: [], lp: [], pct: [] };
    for (const e of ev) {
      if (e[1] > r.end) break;
      let key = null, v = null, p = null;
      if (e[0] === 5) { key = 'adren'; v = e[2] / 10; }
      else if (e[0] === 6) { key = 'prayer'; v = e[2] / 10; }
      else if (e[0] === 4 && e[2] === self && isNum(e[3]) && e[3] >= 0) { key = 'lp'; v = e[3]; p = isNum(e[4]) && e[4] > 0 ? e[3] / e[4] * 100 : null; }
      if (key === null || !isNum(v)) continue;
      const L = pts[key];
      if (e[1] < r.start && L.length) L[0] = [e[1], v]; else L.push([e[1], v]);
      if (key === 'lp') { const P = pts.pct; if (e[1] < r.start && P.length) P[0] = [e[1], p]; else P.push([e[1], p]); }
    }
    // integrate a step function over the spans, per bin
    function integ(L, test) {
      const sum = new Array(B.bins).fill(0), cov = new Array(B.bins).fill(0);
      let tot = 0, totC = 0, hitC = 0;
      for (let k = 0; k < L.length; k++) {
        if (L[k][1] == null) continue;
        const a = L[k][0], z = k + 1 < L.length ? L[k + 1][0] : r.end + 1, v = L[k][1];
        for (const s of sp) {
          let x = Math.max(a, s[0]);
          const y = Math.min(z, s[1] + 1);
          while (x < y) {
            const b = Math.floor((x - r.start) / B.binC), bEnd = r.start + (b + 1) * B.binC, y2 = Math.min(y, bEnd), d = y2 - x;
            if (b >= 0 && b < B.bins) { sum[b] += v * d; cov[b] += d; }
            tot += v * d; totC += d; if (test && test(v)) hitC += d;
            x = y2;
          }
        }
      }
      const bins = sum.map((s, b) => B.live[b] && cov[b] > 0 ? s / cov[b] : null);
      return { bins, avg: totC ? tot / totC : null, hitMs: hitC * CYCLE_MS };
    }
    const ad = integ(pts.adren, v => v >= 100), ad0 = integ(pts.adren, v => v <= 0), pr = integ(pts.prayer), lp = integ(pts.lp), pc = integ(pts.pct);
    let lpMin = null;
    for (let k = 0; k < pts.lp.length; k++) {
      const a = pts.lp[k][0], z = k + 1 < pts.lp.length ? pts.lp[k + 1][0] : r.end + 1;
      if (sp.some(s => a <= s[1] && z > s[0]) && (lpMin == null || pts.lp[k][1] < lpMin)) lpMin = pts.lp[k][1];
    }
    return { binMs: B.binMs, start: r.start, bins: B.bins, adren: ad.bins, prayer: pr.bins, lp: lp.bins, adrenAvg: ad.avg, adrenFullMs: ad.hitMs, adrenZeroMs: ad0.hitMs,
             prayerAvg: pr.avg, lpMin, lpAvgPct: pc.avg };
  }
  function dpsSeries(log, sel, binMs, smoothMs) {
    const r = range(log, sel), ev = log.events, B = binsOf(r, binMs), at = attribute(log), src = r.enemies ? sources(log) : null;
    const mk = () => B.live.map(x => x ? 0 : null);
    const dealt = mk(), taken = mk(), healed = mk();
    const [lo, hi] = win(log, r);
    for (let i = lo; i < hi; i++) {
      const e = ev[i];
      if (e[0] !== 0 || !inR(e[1], r)) continue;
      const b = Math.floor((e[1] - r.start) / B.binC);
      if (b < 0 || b >= B.bins || dealt[b] === null) continue;
      const role = hitRole(log, e), v = e[4] > 0 ? e[4] : 0;
      if (role === 'dealt') { if (passDealt(r, e, at.struct[i])) dealt[b] += v; }
      else if (role === 'taken' || role === 'blocked') { if (!src || r.enemies.has(src[i])) taken[b] += v; }
      else if (role === 'heal') healed[b] += v;
    }
    const k = Math.max(1, Math.round(Math.max(B.binMs, isNum(smoothMs) && smoothMs > 0 ? smoothMs : 0) / B.binMs)), back = Math.floor((k - 1) / 2), fwd = k - 1 - back;
    const ps = [0], pc = [0];
    for (let b = 0; b < B.bins; b++) { ps.push(ps[b] + (dealt[b] || 0)); pc.push(pc[b] + (dealt[b] === null ? 0 : 1)); }
    const dps = dealt.map((v, b) => {
      if (v === null) return null;
      const a = Math.max(0, b - back), z = Math.min(B.bins - 1, b + fwd), n = pc[z + 1] - pc[a];
      return n ? (ps[z + 1] - ps[a]) / (n * B.binMs) * 1000 : 0;
    });
    return { binMs: B.binMs, start: r.start, bins: B.bins, dealt, taken, healed, dps };
  }
  function rotation(log, sel) {
    const r = range(log, sel), cs = casts(log, sel), sp = spansOf(r), tickOf = c => Math.floor((c - r.start) / TICK);
    const counted = cs.list.filter(k => k.src !== 3 || k.resolved);
    const gcd = r.abilities ? [] : cs.list.filter(k => k.src === 3 && !k.resolved).map(k => k.tick);
    const tsp = [];
    for (const s of sp) { const a = tickOf(s[0]), z = tickOf(s[1]), m = tsp[tsp.length - 1]; if (m && a <= m[1] + 1) m[1] = Math.max(m[1], z); else tsp.push([a, z]); }
    const maxT = tsp.length ? tsp[tsp.length - 1][1] : -1;
    const cov = new Uint8Array(Math.max(0, maxT + 1));
    const mark = (t, len) => { for (let k = Math.max(0, t); k < t + len && k <= maxT; k++) cov[k] = 1; };
    for (const k of counted) mark(k.tick, Math.max(3, ability(log, k.struct).channel ? k.span : 3));
    for (const t of gcd) mark(t, 3);
    let ticks = 0, covered = 0;
    const gaps = [];
    for (const [a, z] of tsp) {
      let run = 0;
      for (let t = a; t <= z; t++) {
        ticks++;
        if (cov[t]) { covered++; if (run > 3) gaps.push([t - run, run]); run = 0; } else run++;
      }
      if (run > 3) gaps.push([z + 1 - run, run]);
    }
    const lanes = [], laneOf = new Map();
    for (const k of counted) {
      let L = laneOf.get(k.struct);
      if (!L) { const a = ability(log, k.struct); laneOf.set(k.struct, L = { struct: k.struct, name: a.name, icon: a.icon || 0, style: a.style || '', cd: isNum(a.cd) ? a.cd : 0, ticks: [], count: 0 }); lanes.push(L); }
      L.ticks.push(k.tick); L.count++;
    }
    const minutes = durOf(r) / 60000;
    return { casts: counted.length, minutes, cpm: minutes > 0 ? counted.length / minutes : 0, activePct: ticks ? covered / ticks : 0, ticks, idleTicks: ticks - covered,
             gaps, lanes, gcd, list: cs.list };
  }
  function buffGroup(log, struct) {
    if (SUMMON_BUFFS.indexOf(struct) >= 0) return 'summon';
    const bs = log.dict && log.dict.buffs, b = own(bs, struct) && isObj(bs[struct]) ? bs[struct] : {};
    if (b.type === 1) return 'debuff';
    if (/overload|prayer renew|anti-?fire|super antifire|perfect plus|adrenaline renewal|aggression|weapon poison/i.test(String(b.name || ''))) return 'consumable';
    return 'buff';
  }
  // A fight or selection reduced to numbers keyed by struct ids, comparable across logs.
  function profile(log, sel) {
    const r = range(log, sel), s = summary(log, sel), rot = rotation(log, sel), so = styleOf(log, sel), ab = byAbility(log, sel), up = uptimes(log, sel);
    const fi = r.n >= 0 ? fightInfo(log, r.n) : null, killMs = fi && fi.result === 'kill' ? fi.durMs : null;
    const abilities = {}, buffs = {};
    for (const row of ab.rows) abilities[row.struct] = { name: row.name, icon: row.icon || 0, total: row.total, hits: row.hits, casts: row.casts, share: row.share, perCast: row.perCast };
    for (const row of up.rows) buffs[row.struct] = { name: row.name, uptime: row.uptime };
    return { v: api.version, durMs: s.durMs, killMs, dealt: s.dealt, dps: s.dealt / (killMs || s.durMs) * 1000, taken: s.taken, deaths: s.deaths, casts: rot.casts,
             cpm: rot.cpm, activePct: rot.activePct, style: so.style, styleShares: so.shares, abilities, buffs, bins: { binMs: 5000, dps: dpsSeries(log, sel, 5000).dps } };
  }
  function compare(pa, pb) {
    pa = pa || {}; pb = pb || {};
    const num = x => isNum(x) ? x : null;
    const strip = {};
    for (const k of ['durMs', 'dps', 'dealt', 'taken', 'deaths', 'casts', 'cpm', 'activePct']) {
      const a = num(pa[k]), b = num(pb[k]), d = a != null && b != null ? b - a : null;
      strip[k] = [a, b, d, a ? (d == null ? null : d / a) : null];
    }
    const pick = (o, k) => isObj(o) && own(o, k) && isObj(o[k]) ? o[k] : null;
    const keys = (o, p) => { const m = new Map(); for (const x of [o, p]) if (isObj(x)) for (const k of Object.keys(x)) m.set(k, 1); return Array.from(m.keys()); };
    const abilities = keys(pa.abilities, pb.abilities).map(k => {
      const a = pick(pa.abilities, k), b = pick(pb.abilities, k), f = x => x ? { total: num(x.total) || 0, share: num(x.share) || 0, casts: num(x.casts) || 0, perCast: num(x.perCast) || 0 } : null;
      return { struct: Number(k), name: String((b || a).name || ''), a: f(a), b: f(b), d: (b ? num(b.total) || 0 : 0) - (a ? num(a.total) || 0 : 0) };
    }).sort((x, y) => Math.max(y.a ? y.a.total : 0, y.b ? y.b.total : 0) - Math.max(x.a ? x.a.total : 0, x.b ? x.b.total : 0));
    const buffs = keys(pa.buffs, pb.buffs).map(k => {
      const a = pick(pa.buffs, k), b = pick(pb.buffs, k), ua = a ? num(a.uptime) : null, ub = b ? num(b.uptime) : null;
      return { struct: Number(k), name: String((b || a).name || ''), a: ua, b: ub, d: (ub || 0) - (ua || 0) };
    }).sort((x, y) => Math.max(y.a || 0, y.b || 0) - Math.max(x.a || 0, x.b || 0));
    const ba = isObj(pa.bins) ? pa.bins : {}, bb = isObj(pb.bins) ? pb.bins : {};
    return { strip, abilities, buffs, dps: { binMs: num(ba.binMs) || num(bb.binMs) || 5000, a: Array.isArray(ba.dps) ? ba.dps : [], b: Array.isArray(bb.dps) ? bb.dps : [] } };
  }

  // ---- Events filter language: field op value joined by and, or, not and parentheses.
  const FILTER_FIELDS = ['type', 'role', 'actor', 'target', 'source', 'ability', 'kind', 'value', 'hitmark', 'mech', 'crit', 'time'];
  const FILTER_OPS = ['!=', '>=', '<=', '=', '>', '<', '~'];
  function parseFilter(text) {
    const src = String(text == null ? '' : text);
    if (src.length > 500) return { ok: false, error: 'The filter is longer than 500 characters.', at: 500 };
    const toks = [];
    let i = 0;
    while (i < src.length) {
      const ch = src[i];
      if (ch === ' ' || ch === '\t' || ch === '\n' || ch === '\r') { i++; continue; }
      if (ch === '(' || ch === ')' || ch === ',') { toks.push({ t: ch, at: i }); i++; continue; }
      const op = FILTER_OPS.find(o => src.startsWith(o, i));
      if (op) { toks.push({ t: 'op', v: op, at: i }); i += op.length; continue; }
      if (ch === '\'' || ch === '"') {
        const z = src.indexOf(ch, i + 1);
        if (z < 0) return { ok: false, error: 'A quote is not closed.', at: i };
        toks.push({ t: 'str', v: src.slice(i + 1, z), at: i }); i = z + 1; continue;
      }
      let j = i;
      while (j < src.length && /[A-Za-z0-9_.\-]/.test(src[j])) j++;
      if (j === i) return { ok: false, error: 'Unexpected character.', at: i };
      toks.push({ t: 'word', v: src.slice(i, j), at: i }); i = j;
    }
    if (!toks.length) return { ok: true, ast: null };
    let p = 0, nodes = 0;
    const fail = (error, at) => { const e = new Error(error); e.at = at; throw e; };
    const peek = () => toks[p], endAt = src.length;
    const kw = (w) => { const t = toks[p]; return t && t.t === 'word' && t.v.toLowerCase() === w; };
    const node = o => { if (++nodes > 64) fail('The filter has too many parts.', toks[Math.min(p, toks.length - 1)].at); return o; };
    function value() {
      const t = toks[p];
      if (!t || (t.t !== 'word' && t.t !== 'str')) fail('Expected a value.', t ? t.at : endAt);
      p++;
      const n = t.t === 'word' && /^-?\d+(\.\d+)?$/.test(t.v) ? Number(t.v) : NaN;
      return { s: t.v.toLowerCase(), n, num: !isNaN(n) };
    }
    function cmp() {
      const t = toks[p];
      if (!t || t.t !== 'word') fail('Expected a field.', t ? t.at : endAt);
      const f = t.v.toLowerCase();
      if (FILTER_FIELDS.indexOf(f) < 0) fail('Unknown field.', t.at);
      p++;
      if (kw('in')) {
        p++;
        if (!peek() || peek().t !== '(') fail('Expected (.', peek() ? peek().at : endAt);
        p++;
        const vals = [value()];
        while (peek() && peek().t === ',') { p++; vals.push(value()); }
        if (!peek() || peek().t !== ')') fail('Expected ).', peek() ? peek().at : endAt);
        p++;
        return node({ k: 'in', f, v: vals });
      }
      const o = toks[p];
      if (!o || o.t !== 'op') fail('Expected an operator.', o ? o.at : endAt);
      p++;
      return node({ k: 'cmp', f, op: o.v, v: value() });
    }
    function unary() {
      if (kw('not')) { p++; return node({ k: 'not', a: unary() }); }
      if (peek() && peek().t === '(') {
        p++;
        const e = expr();
        if (!peek() || peek().t !== ')') fail('Expected ).', peek() ? peek().at : endAt);
        p++;
        return e;
      }
      return cmp();
    }
    function and() { const a = [unary()]; while (kw('and')) { p++; a.push(unary()); } return a.length > 1 ? node({ k: 'and', a }) : a[0]; }
    function expr() { const a = [and()]; while (kw('or')) { p++; a.push(and()); } return a.length > 1 ? node({ k: 'or', a }) : a[0]; }
    try {
      const ast = expr();
      if (p < toks.length) fail('Unexpected text.', toks[p].at);
      return { ok: true, ast };
    } catch (e) {
      return { ok: false, error: e.message, at: isNum(e.at) ? e.at : 0 };
    }
  }
  // Field values of one query row: { s: lowercased text, n: number or NaN }.
  function rowField(row, f) {
    const x = row._x;
    switch (f) {
      case 'type': return { s: row.type, n: NaN };
      case 'role': return { s: row.role, n: NaN };
      case 'actor': return { s: String(row.actor).toLowerCase(), n: x.actor };
      case 'target': return { s: x.target >= 0 ? String(x.targetLabel).toLowerCase() : '', n: x.target };
      case 'source': return { s: String(row.source).toLowerCase(), n: x.source };
      case 'ability': return { s: String(row.ability).toLowerCase(), n: x.struct };
      case 'kind': return { s: String(row.kind).toLowerCase(), n: NaN };
      case 'value': return { s: String(row.value).toLowerCase(), n: row.value === '' ? NaN : Number(row.value) };
      case 'hitmark': return { s: String(row.hitmark), n: row.hitmark === '' ? NaN : Number(row.hitmark) };
      case 'mech': return { s: String(row.mech).toLowerCase(), n: NaN };
      case 'crit': return { s: x.crit ? 'true' : 'false', n: NaN };
      case 'time': return { s: String(row.time), n: row.time };
    }
    return { s: '', n: NaN };
  }
  function evalFilter(ast, row) {
    if (!ast) return true;
    switch (ast.k) {
      case 'or': return ast.a.some(a => evalFilter(a, row));
      case 'and': return ast.a.every(a => evalFilter(a, row));
      case 'not': return !evalFilter(ast.a, row);
      case 'in': { const fv = rowField(row, ast.f); return ast.v.some(v => same(fv, v)); }
      case 'cmp': {
        const fv = rowField(row, ast.f), v = ast.v;
        switch (ast.op) {
          case '=': return same(fv, v);
          case '!=': return !same(fv, v);
          case '~': return fv.s.indexOf(v.s) >= 0;
          default: {
            let a, b;
            if (v.num) { if (!isNum(fv.n)) return false; a = fv.n; b = v.n; } else { a = fv.s; b = v.s; }
            return ast.op === '>' ? a > b : ast.op === '>=' ? a >= b : ast.op === '<' ? a < b : a <= b;
          }
        }
      }
    }
    return false;
    function same(fv, v) { return v.num && isNum(fv.n) ? fv.n === v.n : fv.s === v.s; }
  }
  const CHIP_ROLES = { dealt: 1, taken: 1, blocked: 1, heal: 1, npcheal: 1 }, CHIP_TYPES = { cast: 1, buff: 1, mech: 1 };
  function query(log, sel, q) {
    q = q || {};
    const r = range(log, sel), ev = log.events, at = attribute(log), srcs = sources(log), self = ctx(log).self;
    const types = Array.isArray(q.types) ? new Set(q.types.map(String)) : null, roles = Array.isArray(q.roles) ? new Set(q.roles.map(String)) : null, other = !!q.other;
    const chips = !!(types || roles || other), text = String(q.text || '').toLowerCase(), hl = q.mode === 'highlight';
    let ast = null;
    if (typeof q.expr === 'string') { const pf = parseFilter(q.expr); if (!pf.ok) return { total: 0, offset: 0, limit: 0, rows: [], error: pf.error, at: pf.at }; ast = pf.ast; }
    else if (q.expr && typeof q.expr === 'object') ast = q.expr;
    const limit = Math.max(1, Math.min(1000, isNum(q.limit) ? Math.floor(q.limit) : 400)), offset = Math.max(0, isNum(q.offset) ? Math.floor(q.offset) : 0);
    const uses = mechs(log, -1).uses.map(u => [u.c, u.label]);
    let ui = 0, lastUse = null;
    const rows = [];
    let total = 0;
    const [lo, hi] = win(log, r);
    for (let i = lo; i < hi; i++) {
      const e = ev[i];
      if (!inR(e[1], r)) continue;
      while (ui < uses.length && uses[ui][0] <= e[1]) lastUse = uses[ui++];
      const isHit = e[0] === 0 && !isMech(log, e), role = isHit ? hitRole(log, e) : '';
      if (role === 'dealt' && !passDealt(r, e, at.struct[i])) continue;
      if ((role === 'taken' || role === 'blocked') && r.enemies && !r.enemies.has(srcs[i])) continue;
      const row = describe(log, i), src = (role === 'taken' || role === 'blocked') ? srcs[i] : -1;
      row.role = role;
      row.source = src >= 0 ? actorLabel(log, src) : '';
      row.mech = row.type === 'mech' ? mechInfo(log, e[2], e[3], e[4]).label : (src !== -1 || role === 'taken' || role === 'blocked') && lastUse && e[1] - lastUse[0] <= 3 * TICK ? lastUse[1] : '';
      row.time = (e[1] - (isNum(q.base) ? q.base : r.start)) * CYCLE_MS;
      let pass = true;
      if (chips) {
        const inChip = !!CHIP_ROLES[role] || !!CHIP_TYPES[row.type];
        pass = (types && types.has(row.type)) || (roles && roles.has(role)) || (other && !inChip);
      }
      if (pass && text) pass = [row.type, row.actor, row.text, row.ability, row.kind, row.value, row.source, row.mech].join('\n').toLowerCase().indexOf(text) >= 0;
      if (pass && ast) {
        const tgt = isHit ? e[2] : (e[0] === 3 && !isMech(log, e) ? e[3] : -1);
        Object.defineProperty(row, '_x', { value: { actor: isMech(log, e) ? (isNum(e[6]) ? e[6] : NaN) : (isNum(e[2]) && e[0] !== 5 && e[0] !== 6 && e[0] !== 12 && e[0] !== 19 && e[0] !== 15 && e[0] !== 16 && e[0] !== 9 && e[0] !== 7 && e[0] !== 1 && e[0] !== 8 ? e[2] : NaN),
                                                   target: isNum(tgt) && tgt >= 0 ? tgt : NaN, targetLabel: isNum(tgt) && tgt >= 0 ? (tgt === self ? 'You' : actorLabel(log, tgt)) : '',
                                                   source: src >= 0 ? src : NaN, struct: isHit ? at.struct[i] : (e[0] === 1 ? (castPos(log).has(i) ? at.casts[castPos(log).get(i)].struct : e[2]) : e[0] === 7 ? e[2] : NaN),
                                                   crit: isHit && !!hmInfo(log, e[3]).crit }, configurable: true });
        pass = evalFilter(ast, row);
      }
      if (hl) { row.hit = !!pass; pass = true; }
      if (!pass) continue;
      if (total >= offset && rows.length < limit) rows.push(row);
      total++;
    }
    return { total, offset, limit, rows };
  }

  // ---- Log level.
  function logSummary(log) {
    const sel = { fights: 'all' }, s = summary(log, sel), so = styleOf(log, sel), fights = log.fights || [];
    const out = {};
    for (const k of Object.keys(s)) if (k !== 'topAbilities' && k !== 'targets') out[k] = s[k];
    let killsBoss = 0, wipes = 0;
    const groups = [], gm = new Map();
    for (let n = 0; n < fights.length; n++) {
      const fi = fightInfo(log, n);
      if (!fi) continue;
      if (fi.result === 'kill') killsBoss++;
      else if (fi.result === 'wipe') wipes++;
      if (!fi.group || fi.result === 'trash') continue;
      let g = gm.get(fi.group);
      if (!g) { gm.set(fi.group, g = { kills: 0, fights: 0, killKey: null, killName: null, keys: [], encName: null, name: fi.bossName }); groups.push(g); }
      g.fights++;
      if (fi.result === 'kill') { g.kills++; if (g.killKey == null) { g.killKey = fi.boss; g.killName = fi.bossName; } if (fi.boss != null && g.keys.indexOf(fi.boss) < 0) g.keys.push(fi.boss); }
      else if (fi.result === 'wipe' && g.encName == null) g.encName = fi.boss;
    }
    out.fights = fights.length; out.killsBoss = killsBoss; out.wipes = wipes; out.style = so.style; out.styleShares = so.shares;
    out.top = s.topAbilities.map(t => { const a = ability(log, t[0]); return [t[0], t[1], a.name, a.icon || 0]; });
    out.bosses = [];
    for (const g of groups) {
      const list = g.keys.length ? g.keys : [g.encName != null ? g.encName : g.name];
      for (const x of list) if (x != null && out.bosses.indexOf(x) < 0) out.bosses.push(x);
    }
    let title = '';
    const killed = groups.filter(g => g.kills > 0);
    if (killed.length) {
      let best = killed[0];
      for (const g of killed) if (g.kills > best.kills) best = g;
      title = best.killName + ' x' + best.kills + (killed.length > 1 ? ' +' + (killed.length - 1) : '');
    } else if (groups.length) {
      let best = groups[0];
      for (const g of groups) if (g.fights > best.fights) best = g;
      title = best.name + ' attempts';
    } else {
      const bt = byTarget(log, sel, { byId: true }).rows.filter(x => x.actor < 0 || actorOf(log, x.actor).type === 'npc');
      if (bt.length) title = bt[0].name + (bt[0].kills > 0 ? ' x' + bt[0].kills : '');
      else title = 'Combat log';
    }
    out.autoTitle = title;
    return out;
  }
  // A live head (or a log assembled from live chunks) made whole: the open fight ends at the last event.
  function liveClose(log) {
    const ev = Array.isArray(log.events) ? log.events : [];
    let lastC = null;
    for (const e of ev) if (Array.isArray(e) && isNum(e[1]) && (lastC == null || e[1] > lastC)) lastC = e[1];
    for (const f of Array.isArray(log.fights) ? log.fights : []) {
      if (!isObj(f) || f.end !== -1) continue;
      f.end = lastC != null && lastC >= f.start ? lastC : f.start;
      f.live = true;
    }
    for (const a of Array.isArray(log.actors) ? log.actors : []) {
      if (!isObj(a) || a.last != null) continue;
      a.last = lastC != null ? (isNum(a.first) ? Math.max(a.first, lastC) : lastC) : (isNum(a.first) ? a.first : 0);
    }
    const L = isObj(log.log) ? log.log : null, ck = isObj(log.clock) ? log.clock : {};
    if (L) {
      if (L.endedAt == null) {
        const c = lastC != null ? lastC : ck.c0;
        let t = (isNum(ck.wall0) ? ck.wall0 : 0) + ((isNum(c) ? c : 0) - (isNum(ck.c0) ? ck.c0 : 0)) * CYCLE_MS;
        if (isNum(L.startedAt) && t < L.startedAt) t = L.startedAt;
        L.endedAt = t;
      }
      if (L.endBy == null) L.endBy = 'live';
    }
    reset(log);
    return log;
  }
  function parseColor(pct) {
    if (pct == null || !isNum(pct)) return 'none';
    return pct >= 100 ? 'gold' : pct >= 99 ? 'pink' : pct >= 95 ? 'orange' : pct >= 75 ? 'purple' : pct >= 50 ? 'blue' : pct >= 25 ? 'green' : 'grey';
  }

  // ---- What leaves the PC and what the site keeps: names the game developers use internally never pass,
  // other players become Player N unless keepNames, and the character can be hidden.
  const DEV_SHAPE = /^[A-Z][A-Z0-9]*(_[A-Z0-9]+)+$/, NPC_DEV = /^[a-z0-9_]+$/, PLAYER_N = /^Player \d+$/, CLEAN_NAME = /^[a-z0-9]+( [a-z0-9]+)*$/;
  function isDevNpc(name) { return typeof name === 'string' && name.indexOf('_') >= 0 && NPC_DEV.test(name); }
  function normName(s) { return String(s).toLowerCase().replace(/[ _\-\u00a0]+/g, ' '); }
  function alnum(code) { return (code >= 48 && code <= 57) || (code >= 97 && code <= 122); }
  // A test for "this string holds one of these names": the name with no letter or digit right before or after it.
  function nameMatcher(P) {
    const clean = new Set(), loose = [];
    let maxW = 0;
    for (const p of P) {
      const q = normName(p);
      if (!q) continue;
      if (CLEAN_NAME.test(q)) { clean.add(q); maxW = Math.max(maxW, q.split(' ').length); } else loose.push(q);
    }
    if (!clean.size && !loose.length) return null;
    return function (str) {
      if (typeof str !== 'string' || !str) return false;
      const s = normName(str);
      if (clean.size) {
        const w = [];
        for (let i = 0; i < s.length;) {
          if (!alnum(s.charCodeAt(i))) { i++; continue; }
          let j = i;
          while (j < s.length && alnum(s.charCodeAt(j))) j++;
          w.push([i, j]); i = j;
        }
        for (let a = 0; a < w.length; a++) {
          let run = '';
          for (let b = a; b < w.length && b - a < maxW; b++) {
            if (b > a && (w[b][0] !== w[b - 1][1] + 1 || s.charCodeAt(w[b - 1][1]) !== 32)) break;
            run = b === a ? s.slice(w[a][0], w[a][1]) : run + ' ' + s.slice(w[b][0], w[b][1]);
            if (clean.has(run)) return true;
          }
        }
      }
      for (const q of loose) {
        let p = s.indexOf(q);
        while (p >= 0) {
          if ((p === 0 || !alnum(s.charCodeAt(p - 1))) && (p + q.length >= s.length || !alnum(s.charCodeAt(p + q.length)))) return true;
          p = s.indexOf(q, p + 1);
        }
      }
      return false;
    };
  }
  function scrubNames(log, P) {
    const m = nameMatcher(P);
    if (!m) return;
    for (const a of Array.isArray(log.actors) ? log.actors : []) if (isObj(a) && a.type === 'npc' && m(a.name)) a.name = npcName(a.id);
    for (const e of Array.isArray(log.events) ? log.events : []) {
      if (!Array.isArray(e)) continue;
      if (e[0] === 16 && m(e[3])) e[3] = '';
      else if (e[0] === 8 && m(e[4])) e[4] = '';
    }
  }
  function sanitize(log, opts) {
    opts = opts || {};
    const warnings = [];
    if (!isObj(log.dict)) log.dict = {};
    const d = log.dict, actors = Array.isArray(log.actors) ? log.actors : [], ev = Array.isArray(log.events) ? log.events : [];
    const orig = actors.map(a => isObj(a) ? a.name : undefined);   // names as they came in, for the names-inside-strings pass
    // 1. seq names -> id-only seq table
    const seqs = isObj(d.seqs) ? d.seqs : {};
    if (Object.keys(seqs).some(k => typeof seqs[k] === 'string' && seqs[k] !== '')) { d.seqinfo = seqInfoFrom(log); warnings.push('seq_names_dropped'); }
    d.seqs = {};
    if (!isObj(d.seqinfo)) d.seqinfo = {};
    // 2. hitmark names
    let hn = false;
    if (isObj(d.hitmarks)) for (const k of Object.keys(d.hitmarks)) { const h = d.hitmarks[k]; if (isObj(h) && own(h, 'name')) { delete h.name; hn = true; } }
    if (hn) warnings.push('hitmark_names_dropped');
    // 3. anything shaped like a developer name
    let dev = false;
    const guard = (o, k) => { if (o != null && typeof o[k] === 'string' && DEV_SHAPE.test(o[k])) { o[k] = ''; dev = true; } };
    for (const a of actors) if (isObj(a)) guard(a, 'name');
    for (const tbl of [d.abilities, d.buffs, d.trackers]) if (isObj(tbl)) for (const k of Object.keys(tbl)) if (isObj(tbl[k])) guard(tbl[k], 'name');
    if (isObj(d.encounters)) for (const k of Object.keys(d.encounters)) guard(d.encounters, k);
    if (isObj(d.trackers)) for (const k of Object.keys(d.trackers)) { const t = d.trackers[k]; if (isObj(t) && isObj(t.cols)) for (const j of Object.keys(t.cols)) guard(t.cols, j); }
    if (isObj(d.mechs)) for (const b of Object.keys(d.mechs)) { const B = d.mechs[b]; if (isObj(B)) for (const k of Object.keys(B)) if (isObj(B[k])) guard(B[k], 'label'); }
    for (const e of ev) { if (!Array.isArray(e)) continue; if (e[0] === 16) guard(e, 3); else if (e[0] === 8) guard(e, 4); }
    // a fight's boss text is a copy of an encounter name: an empty or developer-shaped one becomes null
    for (const f of Array.isArray(log.fights) ? log.fights : []) {
      if (!isObj(f) || typeof f.boss !== 'string') continue;
      const b = f.boss, bar = b.indexOf('|');
      const bad = (bar >= 0 && DEV_SHAPE.test(b.slice(bar + 1))) ||
                  (bar >= 0 ? b.slice(0, bar) : b).split(' & ').some(p => DEV_SHAPE.test(p.replace(/^[ \t\n\v\f\r]+|[ \t\n\v\f\r]+$/g, '')));
      if (bad) { f.boss = null; dev = true; } else if (b === '') f.boss = null;
    }
    if (dev) warnings.push('dev_names_dropped');
    // 4. NPC names in the internal style
    const good = new Map();
    for (const a of actors) if (isObj(a) && a.type === 'npc' && typeof a.name === 'string' && a.name !== '' && !isDevNpc(a.name) && !good.has(a.id)) good.set(a.id, a.name);
    let fixed = false;
    for (const a of actors) {
      if (!isObj(a) || a.type !== 'npc' || !isDevNpc(a.name)) continue;
      a.name = good.has(a.id) ? good.get(a.id) : (own(CONJURE_NAME, a.id) ? CONJURE_NAME[a.id] : npcName(a.id));
      fixed = true;
    }
    if (fixed) warnings.push('npc_names_fixed');
    // 5. other players
    let P = [];
    if (!opts.keepNames) {
      actors.forEach((a, k) => { const nm = orig[k]; if (isObj(a) && a.type === 'player' && typeof nm === 'string' && nm.length >= 3 && !PLAYER_N.test(nm)) P.push(nm); });
      scrubNames(log, P);
      let k = 0;
      for (const a of actors) if (isObj(a) && a.type === 'player') a.name = 'Player ' + (++k);
      if (isObj(log.log)) log.log.anonymised = true;
    }
    // 6. the character
    if (typeof opts.character === 'string') {
      const L = isObj(log.log) ? log.log : null, ch = L ? L.character : undefined;
      if (opts.character === '') {
        if (typeof ch === 'string' && ch.length >= 3) P = P.concat([ch]);
        scrubNames(log, P);
        let w = 0, dropped = false;
        for (let i = 0; i < ev.length; i++) { if (Array.isArray(ev[i]) && ev[i][0] === 15) { dropped = true; continue; } ev[w++] = ev[i]; }
        ev.length = w;
        if (dropped) warnings.push('xp_dropped');
      }
      if (L) L.character = opts.character;
      for (const a of actors) if (isObj(a) && a.type === 'self') a.name = opts.character;
    }
    // 7. launcher summaries never pass
    for (const f of Array.isArray(log.fights) ? log.fights : []) if (isObj(f)) delete f.summary;
    reset(log);
    return { log, warnings };
  }

  const api = { version: 6, killWindow, CYCLE_MS, TICK, STYLES, EVENT_NAMES, MECH_KINDS, STYLE_LINE, MIN_FIGHT_MS, MAX_FIGHT_MS, DPS_CAP, MAX_HIT_CAP, PLAUSIBLE_RATIO, SUMMON_BUFFS,
                PARSE_HEX, REASON_TEXT, token, ctx, reset, hmInfo, hitRole, isFoe, hitStyle, actorOf, actorLabel, cycleMs, cycleTick, range, resolve, inRange,
                ability, shapeOf, seqIs, seqTag, seqInfoFrom, attribute, sourceOf, sources, summary, byAbility, bySource, series, uptimes, gear, slotName, itemName, casts, trackerCheck, styleSplit,
                describe, fightSummaries, isMech, typeName, mechInfo, mechCount, bossName, mechs, plain, shortName, fmtNum, fmtMs, fmtMsTenths,
                bossGroupKey, fightInfo, bossActors, phases, styleOf, metrics, fightHits, byTarget, bossShare, takenBy, enemyCasts, targetsOf, healing, deaths,
                deathRecap, resources, rotation, dpsSeries, buffGroup, profile, compare, query, parseFilter, logSummary, liveClose, parseColor, sanitize };
  root.combatStats = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof window !== 'undefined' ? window : globalThis);
