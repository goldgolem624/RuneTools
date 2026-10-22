// Rows of RuneTools' own in the game's right-click menu. A panel registers a row kind: its wording,
// the target types it belongs under, an optional name list that flips the wording (Highlight to
// Unhighlight for a name on the list), how many verbs of the game's own the target needs, and a
// click handler. This keeps the companion's row table in step and hands clicks back. Every kind can
// be switched off in the Menu swaps panel, and the companion adds at most `perTarget` rows under one
// target (the Hide row counts), so a menu never grows by more than a line or two.
const rtxMenuRows = (() => {
  const TYPE_BITS  = { ground: 1 << 0, interface: 1 << 1, grounditem: 1 << 2, loc: 1 << 3, npc: 1 << 4, player: 1 << 7 };
  const TYPE_NAMES = { 0: 'ground', 1: 'interface', 2: 'grounditem', 3: 'loc', 4: 'npc', 7: 'player' };
  const PREF = 'rtxMenuRows';
  const kinds = [];                      // registration order; a kind's id on the wire is its index + 1
  let lastTable = '', lastPid = 0, lastSeq = -1, seqPid = 0;

  function settings() {
    try { const s = JSON.parse(prefGet(PREF, '{}')); return (s && typeof s === 'object') ? s : {}; } catch (e) { return {}; }
  }
  function save(s) { try { prefSet(PREF, JSON.stringify(s)); } catch (e) {} lastTable = ''; }
  function enabled(id) { const k = kinds.find(x => x.id === id); return !!k && settings()['off_' + id] !== true; }
  function setEnabled(id, on) { const s = settings(); if (on) delete s['off_' + id]; else s['off_' + id] = true; save(s); }
  function perTarget() { const n = settings().perTarget | 0; return (n >= 1 && n <= 3) ? n : 2; }
  function setPerTarget(n) { const s = settings(); s.perTarget = Math.max(1, Math.min(3, n | 0)); save(s); }
  function plainName(t) {
    return String(t || '').replace(/<[^>]*>/g, '').replace(/\s*\((?:level|skill|tier)\s*:[^)]*\)\s*$/i, '').trim();
  }
  // spec: { label, labelOn, types: ['npc', ...], names: () => [...], minVerbs, title, note, onClick(d) }
  function register(id, spec) {
    const i = kinds.findIndex(x => x.id === id);
    const k = Object.assign({ id: id, kind: i >= 0 ? kinds[i].kind : kinds.length + 1 }, spec);
    if (i >= 0) kinds[i] = k; else kinds.push(k);
    lastTable = '';
  }
  function table() {
    const lines = [];
    for (const k of kinds) {
      if (!enabled(k.id) || !k.label) continue;
      let types = 0;
      for (const t of k.types || []) types |= TYPE_BITS[t] || 0;
      if (!types) continue;
      let names = [];
      try { names = typeof k.names === 'function' ? (k.names() || []) : []; } catch (e) { names = []; }
      names = names.map(n => String(n).replace(/[\t\n|]/g, ' ').trim()).filter(Boolean).slice(0, 96);
      lines.push([k.label, k.labelOn || '', types, (k.minVerbs | 0) || 1, k.kind, names.join('|')].join('\t'));
    }
    return lines.join('\n');
  }
  // The game client's side starts empty (and starts empty again whenever it re-attaches), and a page can
  // load before it exists: a push only counts once it landed, and the companion's row count is checked
  // every few seconds so the table is sent again whenever it no longer matches.
  let pushedOk = false, retryAt = 0, checkAt = 0;
  function tick() {
    const b = bridge(), pid = myPid();
    if (!b || !b.menuRowsSet || !b.menuRowClick || !pid) return;
    const t = table();
    const want = t ? t.split('\n').length : 0;
    const now = Date.now();
    let resend = t !== lastTable || pid !== lastPid || (!pushedOk && now >= retryAt);
    if (!resend && now >= checkAt && b.menuStatus) {
      checkAt = now + 3000;
      try { const s = JSON.parse(b.menuStatus(pid) || '{}'); if (s.ok && (s.rows | 0) !== want) resend = true; } catch (e) {}
    }
    if (resend) {
      let ok = false;
      try { ok = !!b.menuRowsSet(pid, t, perTarget()); } catch (e) { ok = false; }
      lastTable = t; lastPid = pid; pushedOk = ok; retryAt = now + 2000; checkAt = now + 3000;
    }
    let d = null;
    try { d = JSON.parse(b.menuRowClick(pid) || '{}'); } catch (e) { return; }
    if (!d || typeof d.seq !== 'number') return;
    if (pid !== seqPid) { seqPid = pid; lastSeq = d.seq; return; }   // clicks from before this page looked are not acted on
    if (d.seq === lastSeq) return;
    lastSeq = d.seq;
    const k = kinds.find(x => x.kind === d.kind);
    if (!k || typeof k.onClick !== 'function') return;
    d.plain = plainName(d.target);
    d.typeName = TYPE_NAMES[d.type] || 'other';
    try { k.onClick(d); } catch (e) { console.error('menu row ' + k.id + ': ' + (e && e.message ? e.message : e)); }
  }
  setInterval(tick, 400);
  // Every wording a row of ours can have, the fixed rows included: these are never a target's options.
  function labels() {
    const out = new Set(['Hide', 'Show', 'Mark tile', 'Unmark tile', 'Customize tile']);
    for (const k of kinds) { if (k.label) out.add(k.label); if (k.labelOn) out.add(k.labelOn); }
    return out;
  }
  // A published menu entry that is a row of ours: our rows carry the "no target type" class, which the
  // game gives only to Walk here and Cancel, and those never read one of our wordings.
  function isOurs(e) { return !!e && (e.type | 0) === 0 && labels().has(String(e.verb || '')); }
  return {
    register, enabled, setEnabled, perTarget, setPerTarget, plainName, tick, labels, isOurs,
    refresh: () => { lastTable = ''; },
    list: () => kinds.map(k => ({ id: k.id, title: k.title || k.label, note: k.note || '', on: enabled(k.id) }))
  };
})();
