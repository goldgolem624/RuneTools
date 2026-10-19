// rtx-gameevents.js: the game events plugins receive. The host captures server packets (the Events panel
// shows them as they arrive); this turns each one into events named and shaped like the official client
// API's (SkillTransmit, VarTransmit, InventoryUpdated, LocAdded, ObjStackEntityAdded, ...). Packets the
// official API has no event for come out as RuneTools events in the same style (SpotAnimAdded,
// ProjectileAdded, ClientScriptRun, ...). Coordinates are coordGrid {x, z, level}; values the official
// API names by enumeration (SkillType, LocShapeType, RotationType, GrandExchangeOfferStage) carry the
// number plus the name beside it.
//
// One converter per stream: it remembers the last value of each var, skill, run energy and weight,
// inventory and ground stack, so old values and counts can be given. A value not seen before has old
// value null.

function rtxGameEventsMake() {
  const vars = new Map();      // 'p:id' | 'b:id' | 'c:id' -> last value
  const skills = new Map();    // skill -> { xp, level }
  const invs = new Set();      // inventory ids seen since the stream started
  const stacks = new Map();    // 'x,z,level,obj' -> { count, isOwner }
  let runEnergy = null, runWeight = null;

  const coord = (ev, xk, yk, pk) => {
    const x = ev[xk || 'x'], z = ev[yk || 'y'], level = ev[pk || 'plane'];
    return typeof x === 'number' && typeof z === 'number' ? { x, z, level: typeof level === 'number' ? level : 0 } : null;
  };
  const named = (e, v) => officialName(e, v) || null;
  const contentNames = new Map();   // 'kind:id' -> the content's own name (cache index 67), '' none
  const contentName = (kind, id) => {
    const k = kind + ':' + id;
    if (contentNames.has(k)) return contentNames.get(k);
    let n = '';
    try { const b = typeof bridge === 'function' ? bridge() : null; if (b && b.officialName) n = String(b.officialName(kind, id) || ''); } catch (e) {}
    if (n || contentNames.size > 20000) contentNames.set(k, n);   // not cached while the table is still building
    return n;
  };
  const base = (ev, kind) => ({ kind, seq: ev.seq, t: ev.t, wall: ev.wall });

  function varEvent(ev, domain, key, isVarBit) {
    const k = key + ':' + ev.id;
    const newValue = ev.long ? String(ev.value) : ev.value;
    const oldValue = vars.has(k) ? vars.get(k) : null;
    vars.set(k, newValue);
    const name = contentName(domain === 'VarClient' ? 'varc' : isVarBit ? 'varbit' : 'varp', ev.id) || null;
    return Object.assign(base(ev, 'VarTransmit'), { var: { domain, id: ev.id, name, isVarBit }, oldValue, newValue });
  }

  function stackKey(c, obj) { return c ? c.x + ',' + c.z + ',' + c.level + ',' + obj : null; }

  function one(ev, out) {
    if (!ev || typeof ev !== 'object') return;
    switch (ev.kind) {
      case 'skill_update': {
        const prev = skills.get(ev.skill) || null;
        skills.set(ev.skill, { xp: ev.xp, level: ev.level });
        out.push(Object.assign(base(ev, 'SkillTransmit'), {
          skill: ev.skill, skillName: named('SkillType', ev.skill),
          oldXP: prev ? prev.xp : null, newXP: ev.xp,
          oldGameplayLevel: prev ? prev.level : null, newGameplayLevel: ev.level }));
        return;
      }
      case 'varp_set':   out.push(varEvent(ev, 'VarPlayer', 'p', false)); return;
      case 'varbit_set': out.push(varEvent(ev, 'VarPlayer', 'b', true)); return;
      case 'varc_set':   out.push(varEvent(ev, 'VarClient', 'c', false)); return;
      case 'run_energy':
        out.push(Object.assign(base(ev, 'RunEnergyTransmit'), { oldRunEnergy: runEnergy, newRunEnergy: ev.value }));
        runEnergy = ev.value; return;
      case 'run_weight':
        out.push(Object.assign(base(ev, 'RunWeightTransmit'), { oldRunWeight: runWeight, newRunWeight: ev.value }));
        runWeight = ev.value; return;
      case 'container_update': {
        const slots = (ev.slots || []).map(s => ({ slot: s.slot, obj: s.item, count: s.qty }));
        invs.add(ev.container);
        out.push(Object.assign(base(ev, 'InventoryUpdated'), { inv: ev.container, other: !!(ev.flags & 1),
          slotsUpdated: slots.map(s => s.slot), slots, partial: !!ev.partial }));
        return;
      }
      case 'container_full': {
        const slots = (ev.slots || []).map(s => ({ slot: s.slot, obj: s.item, count: s.qty }));
        const kind = invs.has(ev.container) ? 'InventoryUpdated' : 'InventoryAdded';
        invs.add(ev.container);
        out.push(Object.assign(base(ev, kind), { inv: ev.container, other: !!ev.other, full: true,
          slotsUpdated: slots.map(s => s.slot), slots, partial: !!ev.partial }));
        return;
      }
      case 'container_reset':
        invs.delete(ev.container);
        out.push(Object.assign(base(ev, 'InventoryRemoved'), { inv: ev.container, other: !!ev.other }));
        return;
      case 'obj_add': {
        // the packet names the owning player by index, not whether that is you: isOwner stays unknown
        const c = coord(ev), k = stackKey(c, ev.item), ownerIndex = typeof ev.owner === 'number' ? ev.owner : null;
        if (k) stacks.set(k, { count: ev.qty, ownerIndex });
        out.push(Object.assign(base(ev, 'ObjStackEntityAdded'), { objType: ev.item, count: ev.qty, isOwner: null, ownerIndex, coordGrid: c }));
        return;
      }
      case 'obj_del': {
        const c = coord(ev), k = stackKey(c, ev.item), s = k ? stacks.get(k) : null;
        if (k) stacks.delete(k);
        out.push(Object.assign(base(ev, 'ObjStackEntityRemoved'), { objType: ev.item, count: s ? s.count : null,
          isOwner: null, ownerIndex: s ? s.ownerIndex : null, coordGrid: c }));
        return;
      }
      case 'obj_count': {
        const c = coord(ev), k = stackKey(c, ev.item), s = k ? stacks.get(k) : null;
        if (k) stacks.set(k, { count: ev.qty, ownerIndex: s ? s.ownerIndex : null });
        out.push(Object.assign(base(ev, 'ObjStackEntityChanged'), { objType: ev.item, oldCount: ev.from, newCount: ev.qty,
          isOwner: null, ownerIndex: s ? s.ownerIndex : null, coordGrid: c }));
        return;
      }
      case 'zone_clear': {
        const c = coord(ev);
        if (c) for (const k of Array.from(stacks.keys())) {   // the zone's 8x8 tiles drop their stacks
          const p = k.split(',').map(Number);
          if (p[2] === c.level && p[0] >= c.x && p[0] < c.x + 8 && p[1] >= c.z && p[1] < c.z + 8) stacks.delete(k);
        }
        out.push(Object.assign(base(ev, 'ZoneCleared'), { coordGrid: c }));
        return;
      }
      case 'zone_update':
        for (const it of ev.items || []) one(Object.assign({ seq: ev.seq, t: ev.t, wall: ev.wall }, it), out);
        return;
      case 'zone_base': return;   // context for the tile events that follow; their coordinates already carry it
      case 'loc_add':
        out.push(Object.assign(base(ev, 'LocAdded'), { config: ev.loc, shapeType: ev.type, shapeTypeName: named('LocShapeType', ev.type),
          rotationType: ev.rot, rotationTypeName: named('RotationType', ev.rot), coordGrid: coord(ev) }));
        return;
      case 'loc_del':
        out.push(Object.assign(base(ev, 'LocRemoved'), { shapeType: ev.type, shapeTypeName: named('LocShapeType', ev.type),
          rotationType: ev.rot, rotationTypeName: named('RotationType', ev.rot), coordGrid: coord(ev) }));
        return;
      case 'ge_offer': {
        const listing = ev.cleared ? null : {
          stage: ev.status, stageName: named('GrandExchangeOfferStage', ev.status),
          type: ev.type, typeName: ev.type >= 0 ? named('GrandExchangeOfferType', ev.type) : null,
          item: typeof ev.item === 'number' ? ev.item : null, price: ev.price != null ? ev.price : null,
          itemCount: ev.qty != null ? ev.qty : null, soldCount: ev.filled != null ? ev.filled : null,
          goldFromSale: ev.filledValue != null ? ev.filledValue : null };
        out.push(Object.assign(base(ev, 'GrandExchangeOfferUpdated'), { market: ev.market, slot: ev.slot, listing }));
        return;
      }
      case 'gameTick':
        out.push({ kind: 'ServerLogic', tick: ev.tick, dtMs: ev.dtMs });
        return;
      // RuneTools events: no official event carries these
      case 'spotanim':
        out.push(Object.assign(base(ev, 'SpotAnimAdded'), { spotAnim: ev.gfx, target: 'tile', coordGrid: coord(ev),
          height: ev.height, delay: ev.delay }));
        return;
      case 'spotanim_actor': {
        const t = ev.target;
        out.push(Object.assign(base(ev, 'SpotAnimAdded'), { spotAnim: ev.gfx,
          target: t === 'npc' ? 'NPC' : t === 'player' ? 'PLAYER' : 'tile',
          index: typeof ev.index === 'number' ? ev.index : null, coordGrid: t === 'tile' ? coord(ev) : null,
          height: ev.height, delay: ev.delay, slot: ev.slot }));
        return;
      }
      case 'projectile': {
        const r = Object.assign({}, ev); delete r.kind; delete r.gfx; delete r.x; delete r.y; delete r.plane;
        out.push(Object.assign(base(ev, 'ProjectileAdded'), r, { spotAnim: ev.gfx, coordGrid: coord(ev) || coord(ev, 'zx', 'zy', 'zplane') }));
        return;
      }
      case 'sound':
        out.push(Object.assign(base(ev, 'SoundPlayed'), { sound: ev.id }));
        return;
      case 'area_sound':
        out.push(Object.assign(base(ev, 'AreaSoundPlayed'), { sound: ev.id, coordGrid: coord(ev),
          loops: ev.loops != null ? ev.loops : null, radius: ev.radius != null ? ev.radius : null,
          volume: ev.volume != null ? ev.volume : null }));
        return;
      case 'runclientscript':
        out.push(Object.assign(base(ev, 'ClientScriptRun'), { script: ev.script, signature: ev.sig, args: ev.args || [], partial: !!ev.partial }));
        return;
      case 'buff_update':
        out.push(Object.assign(base(ev, 'BuffChanged'), { struct: ev.struct, active: !!ev.active, name: ev.name || '' }));
        return;
      case 'tracker_group':
        out.push(Object.assign(base(ev, 'TrackerGroupAdded'), { groupId: ev.groupId, slot: ev.slot }));
        return;
      case 'tracker_values':
        out.push(Object.assign(base(ev, 'TrackerValuesChanged'), { cells: ev.cells || [], partial: !!ev.partial }));
        return;
      case 'tracker_remove':
        out.push(Object.assign(base(ev, 'TrackerGroupRemoved'), { slot: ev.slot, groupId: ev.groupId != null ? ev.groupId : null }));
        return;
      case 'tracker_clear':
        out.push(Object.assign(base(ev, 'TrackerCellCleared'), { group: ev.group, row: ev.row, column: ev.column,
          groupId: ev.groupId != null ? ev.groupId : null, rowId: ev.rowId != null ? ev.rowId : null, columnId: ev.columnId != null ? ev.columnId : null }));
        return;
      case 'tracker_column':
        out.push(Object.assign(base(ev, 'TrackerColumnChanged'), { group: ev.group, column: ev.column, shown: !!ev.shown,
          groupId: ev.groupId != null ? ev.groupId : null, columnId: ev.columnId != null ? ev.columnId : null }));
        return;
      case 'system_update':  out.push(Object.assign(base(ev, 'SystemUpdate'), { seconds: ev.seconds })); return;
      case 'camera_target':  out.push(Object.assign(base(ev, 'CameraTargetChanged'), { coordGrid: ev.cleared ? null : coord(ev), cleared: !!ev.cleared })); return;
      case 'cutscene':       out.push(Object.assign(base(ev, 'CutsceneStarted'), { cutscene: ev.id })); return;
      case 'friends_loaded': out.push(base(ev, 'FriendsListLoaded')); return;
      case 'private_filter': out.push(Object.assign(base(ev, 'PrivateFilterChanged'), { value: ev.value })); return;
      case 'minimap_state':  out.push(Object.assign(base(ev, 'MiniMapStateChanged'), { value: ev.value, mode: ev.mode, shown: !!ev.shown })); return;
      case 'ping':           out.push(Object.assign(base(ev, 'ServerPing'), { a: ev.a, b: ev.b })); return;
      case 'zone_sub':       out.push(Object.assign(base(ev, 'UnknownZoneItem'), { sub: ev.sub, hex: ev.hex || '' })); return;
      default:               out.push(Object.assign(base(ev, 'UnknownPacket'), { op: ev.op, len: ev.len, hex: ev.hex || '' })); return;
    }
  }

  return {
    // The events for one captured packet record (zero, one or several).
    convert(ev) { const out = []; try { one(ev, out); } catch (e) {} return out; },
    reset() { vars.clear(); skills.clear(); invs.clear(); stacks.clear(); contentNames.clear(); runEnergy = runWeight = null; },
  };
}

// The host's stream: every captured packet in arrival order, kept as converted events (512 newest) so
// plugins can also ask for the events after a sequence number (state.events).
const rtxGameEvents = (function () {
  const conv = rtxGameEventsMake();
  const ring = [];
  let seq = 0, tick = -1;
  const subs = [];
  function push(packet) {
    if (packet && packet.kind === 'gameTick' && typeof packet.tick === 'number') tick = packet.tick;
    for (const ev of conv.convert(packet)) {
      ev.seq = ++seq;
      ring.push(ev); if (ring.length > 512) ring.shift();
      for (const f of subs) { try { f(ev); } catch (e) {} }
    }
  }
  return {
    push,
    on(fn) { if (typeof fn === 'function') subs.push(fn); },
    since(after) { const a = Number(after) || 0; return { seq, tick, events: ring.filter(e => e.seq > a) }; },
    reset() { conv.reset(); ring.length = 0; },
  };
})();
