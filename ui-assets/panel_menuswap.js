// RuneToolsX panel: Right-click menu (Developer).
(function () {

  let mnuData = null; let mnuSig = ''; let mnuListSig = ''; let mnuOn = -1; mnuNote = ''; let mnuPinCtx = '';
  let mnuLoaded = false;    // durable store read once, before any rule is written back
  let mnuView = 'menu';     // 'menu' = the latched menu | 'rules' = every saved rule | 'add'
  let mnuNames = {};        // rule key -> display name, so a saved rule reads as more than an id
  let mnuIdSeen = {};
  let mnuIfaceDiag = '';   // last unresolved interface hover (diagnostic line in the footer)
  let mnuAdd = { name: '', order: [], editKey: '' };   // the rule editor's working copy
  let mnuRecent = [];       // [{key, name, ents, ts}], newest first
  let mnuSel = '';          // rule key being edited; '' = show the list
  let mnuConfirmForget = '';     // rule key awaiting a Forget confirmation
  const mnuKindOf = ty => (ty === 1 ? 0 : ty === 3 ? 1 : ty === 4 ? 2 : -1);
  // was keyed "object:109059" and matched nowhere - items and NPCs worked because their two words
  const mnuKeyKind = k => (k === 0 ? 'item' : k === 1 ? 'loc' : 'npc');
  let mnuRules = {};        // target -> [verb, ...] desired order ('*' = any object)
  let mnuOrder = null;      // working order for the latched menu; null = as the game has it
  try { const raw = localStorage.getItem('rtxMenuRules'); if (raw) mnuRules = JSON.parse(raw) || {}; } catch (e) {}

  function mnuAdopt(o) {
    if (!o || typeof o !== 'object') return false;
    if (o.v && o.rules) {
      mnuRules = o.rules || {};
      mnuNames = o.names || {};
      return !!Object.keys(mnuRules).length;
    }
    mnuRules = o;
    return !!Object.keys(o).length;
  }

  async function mnuLoadRules() {
    if (mnuLoaded || !bridge() || !bridge().menuRulesLoad) return;
    mnuLoaded = true;
    let disk = null;
    try { disk = JSON.parse(await rtxData.raw('host.menuRulesLoad') || '{}'); } catch (e) { return; }
    const had = Object.keys(mnuRules).length;
    if (!mnuAdopt(disk) && had) mnuSaveRules();   // first run after the fix: migrate the mirror
    mnuSig = ''; mnuListSig = '';
    renderMenuSwap();
  }

  function mnuSaveRules() {
    const blob = JSON.stringify({ v: 2, rules: mnuRules, names: mnuNames });
    try { localStorage.setItem('rtxMenuRules', blob); } catch (e) {}
    try { if (bridge() && bridge().menuRulesSave) rtxData.sync('act.menuRulesSave', blob); } catch (e) {}
  }


  const mnuPlain = s => String(s || '').replace(/<[^>]*>/g, '').trim();

  function mnuActiveEnts() {
    if (mnuSel) {
      for (const r of mnuRecent) if (r.key === mnuSel) return r.ents;
    }
    return (mnuData && mnuData.entries) || [];
  }

  function mnuUpgradeKeys(ents) {
    for (const t of mnuTargets(ents)) {
      const keys = mnuVarKeys(ents, t);
      const best = keys[0];
      if (!/^(item|loc|npc):/.test(best) || mnuRules[best]) continue;
      for (let i = 1; i < keys.length; i++) {
        const k = keys[i];
        if (!mnuRules[k]) continue;
        mnuRules[best] = mnuRules[k];
        mnuNames[best] = mnuNames[k] || t;
        delete mnuRules[k];
        delete mnuNames[k];
        mnuSaveRules();
        mnuSig = ''; mnuListSig = '';
        break;
      }
    }
  }

  function mnuNoteRecent() {
    const ents = (mnuData && mnuData.entries) || [];
    for (const t of mnuTargets(ents)) {
      const rows = ents.filter(e => mnuPlain(e.target) === t);
      if (!rows.length) continue;
      const rec = {
        name: t,
        ents: rows,
        handle: (mnuData && mnuData.handle) | 0,
        ts: Date.now()
      };
      rec.key = mnuRuleKey(rows, t, rec) || ('noid:' + t + '@' + mnuSetSig(rows, t));
      for (let i = 0; i < mnuRecent.length; i++)
        if (mnuRecent[i].key === rec.key) { mnuRecent.splice(i, 1); break; }
      mnuRecent.unshift(rec);
    }
    if (mnuRecent.length > 12) mnuRecent.length = 12;
  }

  function mnuReconcileRecent() {
    mnuResolveFromScene(mnuRecent.map(r => r.name));
    const seen = {};
    const wasSel = mnuSel ? mnuRecent.find(function (x) { return x.key === mnuSel; }) : null;
    for (let i = 0; i < mnuRecent.length; i++) {
      const r = mnuRecent[i];
      const t = mnuTargets(r.ents)[0];
      if (t && /^noid:/.test(r.key)) r.key = mnuRuleKey(r.ents, t, r) || ('noid:' + t + '@' + mnuSetSig(r.ents, t));
      if (seen[r.key]) { mnuRecent.splice(i--, 1); continue; }   // newest-first: keep the first
      seen[r.key] = 1;
    }
    if (wasSel && wasSel.key !== mnuSel) mnuSel = wasSel.key;
  }

  const mnuVarSep = '\u001e';   // record separator: cannot occur in a verb or target
  // Locs 109059 / 109062 / 109065 are all "Shifting tombs" and share one rule.
  // Handle = hover-target block +0x000: packed (x<<16)|y for a world target, (group<<16)|comp for an INTERFACE row.
  const mnuTypeOf = (ents, target) => {
    for (const e of ents || []) if (mnuPlain(e.target) === target) return (e.type | 0);
    return -1;
  };
  // A MENU target carries a suffix the entity's own name does not: "Mr Da1Nonly (level: 152)",
  // "OS Xela the Stuffed (skill: 1791)"; hoverEntity reports the bare name.
  const mnuBaseName = s => String(s || '').replace(/\s*\((?:level|skill|tier)\s*:[^)]*\)\s*$/i, '').trim();

  function mnuTgtId(target) {
    const seen = mnuIdSeen[target] || mnuIdSeen[mnuBaseName(target)];
    return seen ? (seen.id | 0) : 0;
  }

  function mnuSelfResolve() {
    if (!bridge() || !bridge().hoverEntity) return;
    const t0 = mnuTargets((mnuData && mnuData.entries) || [])[0];
    if (!t0) return;
    let hv = null;
    try { hv = JSON.parse(rtxData.sync('state.hoverEntity') || '{}'); } catch (e) { return; }
    if (hv && hv.ok && hv.kind === 'iface') mnuIfaceDiag = 'iface ' + hv.iface + ':' + hv.comp + ' slot ' + hv.slot + ' walk ' + JSON.stringify(hv.dbg || []);
    if (!hv || !hv.ok || !(hv.id > 0)) return;
    const kind = hv.kind === 'item' ? 0 : hv.kind === 'loc' ? 1 : hv.kind === 'npc' ? 2 : -1;
    if (kind < 0) return;
    const who = mnuBaseName(mnuPlain(hv.name || '')) || mnuBaseName(t0);
    const prev = mnuIdSeen[who];
    if (!prev || prev.id !== (hv.id | 0)) {
      mnuIdSeen[who] = { kind: kind, id: hv.id | 0 };
      mnuSig = ''; mnuListSig = '';
    }
  }

  function mnuResolveFromScene(names) {
    if (!bridge() || !bridge().sceneEntities) return;
    const want = names.filter(n => n && !mnuIdSeen[n] && !mnuIdSeen[mnuBaseName(n)]);
    if (!want.length) return;
    let sc = null;
    try { sc = JSON.parse(bridge().sceneEntities(myPid(), 20) || '{}'); } catch (e) { return; }
    if (!sc) return;
    const pools = [{ list: sc.npcs || [], kind: 2 }, { list: sc.objects || [], kind: 1 }];
    let got = false;
    for (const n of want) {
      const base = mnuBaseName(n);
      for (const pool of pools) {
        const ids = {};
        for (const e of pool.list)
          if (mnuBaseName(mnuPlain(e.name || '')) === base && (e.id | 0) > 0) ids[e.id | 0] = 1;
        const uniq = Object.keys(ids);
        if (!uniq.length) continue;
        if (uniq.length === 1) { mnuIdSeen[base] = { kind: pool.kind, id: uniq[0] | 0 }; got = true; }
        break;
      }
    }
    if (got) { mnuSig = ''; mnuListSig = ''; }
  }

  function mnuSetSig(ents, target) {
    const verbs = [];
    for (const e of ents || []) if (mnuPlain(e.target) === target && e.verb) verbs.push(e.verb);
    verbs.sort();
    const str = verbs.join('|');
    let h = 5381;
    for (let i = 0; i < str.length; i++) h = ((h * 33) ^ str.charCodeAt(i)) >>> 0;
    return h.toString(36);
  }
  function mnuVarKeys(ents, target, ctx) {
    const seen = mnuIdSeen[target] || mnuIdSeen[mnuBaseName(target)];
    if (!seen || !(seen.id > 0)) return [];
    const kind = seen.kind >= 0 ? seen.kind : mnuKindOf(mnuTypeOf(ents, target));
    if (kind < 0) return [];
    if (kind === 0) {
      const sig = mnuSetSig(ents, target);
      const bare = 'item:' + seen.id;
      return sig ? [bare + '@' + sig, bare] : [bare];
    }
    return [mnuKeyKind(kind) + ':' + seen.id];
  }

  function mnuVariant(ents, target) { return mnuVarKeys(ents, target)[0]; }
  function mnuRuleKey(ents, target, ctx) {
    const keys = mnuVarKeys(ents, target, ctx);
    for (const k of keys) if (mnuRules[k]) return k;
    return keys[0];
  }

  function mnuTargets(ents) {
    const out = [];
    for (const e of ents || []) {
      const t = mnuPlain(e.target);
      if (t && out.indexOf(t) < 0) out.push(t);
    }
    return out;
  }

  // Pins go to the companion as rules: each rule is one group of "verb<TAB>name" lines, groups
  // separated by a marker line. Same-NAMED objects with different ids (npc 321 "Fishing spot"
  // Harpoon/Cage, npc 322 Harpoon/Net) look identical in the menu, so the companion applies a group
  // only when the menu offers every option it names; the first group that qualifies wins.
  const mnuGroupMark = '\u001d';
  function mnuGroupsFor(ents) {
    const out = [];
    const seen = {};
    for (const t of mnuTargets(ents)) {
      if (seen[t]) continue;
      seen[t] = 1;
      const key = mnuRuleKey(ents, t);
      const verbs = (key && mnuRules[key]) || [];
      if (verbs.length) out.push(verbs.map(v => v + '\t' + t));
    }
    return out;
  }
  function mnuPrearm() {
    const out = [];
    for (const k in mnuRules) {
      if (k === '*') continue;
      const nm = mnuPlain(mnuNames[k] || (/^(item|loc|npc):/.test(k) ? '' : k.split(mnuVarSep)[0]));
      const verbs = mnuRules[k] || [];
      if (nm && verbs.length) out.push(verbs.map(v => v + '\t' + nm));
    }
    const any = mnuRules['*'] || [];
    if (any.length) out.push(any.map(v => v + '\t'));
    return out;
  }

  async function mnuPush() {
    mnuSaveRules();
    const live = (mnuData && mnuData.entries) || [];
    let groups = mnuGroupsFor(live);
    if (mnuSel && paneVisible('menuswap')) groups = groups.concat(mnuGroupsFor(mnuActiveEnts()));
    groups = groups.concat(mnuPrearm());
    // One copy of each rule, duplicate options inside a rule dropped, capped to the share buffer
    // (kMaxPins = 256, markers included).
    const seenGroup = {};
    const lines = [];
    for (const g of groups) {
      const inner = {};
      const rows = g.filter(ln => ln && (inner[ln] ? false : (inner[ln] = 1)));
      const sig = rows.join('\n');
      if (!rows.length || seenGroup[sig]) continue;
      seenGroup[sig] = 1;
      if (lines.length + rows.length + 1 > 256) break;
      lines.push(mnuGroupMark);
      for (const r of rows) lines.push(r);
    }
    try { await rtxData.raw('act.menuPins', lines.join('\n')); } catch (e) {}
  }

  let mnuBusy = false;
  async function mnuTick() {
    if (mnuBusy) return;           // 300 ms timer vs awaits: no overlapping ticks
    mnuBusy = true;
    try {
    if (!bridge() || !bridge().menuStatus) return;
    await mnuLoadRules();          // once, before anything can overwrite the durable copy
    const vis = paneVisible('menuswap');
    const want = vis || !!Object.keys(mnuRules).length;
    const mode = !want ? 0 : vis ? 1 : 2;
    if (mode !== mnuOn && bridge().menuEnable) {
      const wasOff = mnuOn <= 0;
      mnuOn = mode;
      try { await rtxData.raw('act.menuEnable', mode); } catch (e) {}
      if (wasOff && want) mnuPush();
    }
    let d = null;
    try { d = JSON.parse(await rtxData.raw('host.menuStatus') || '{}'); } catch (e) { return; }
    if (!want) return;               // no rules stored: nothing to resolve, nothing to push
    // Self-heal: the companion resets its switch and rule list whenever it (re)attaches to a client
    // (new client, relog, companion reload). Our own mode does not change then, so re-send both.
    if (d && d.ok && d.hooked) {
      if (!d.enabled && mnuOn > 0) { try { await rtxData.raw('act.menuEnable', mnuOn); } catch (e) {} }
      if (!(d.pinCount | 0) && Object.keys(mnuRules).length && Date.now() - (mnuTick._lastRepush || 0) > 2000) {
        mnuTick._lastRepush = Date.now(); mnuPinCtx = '';
      }
    }
    if (d && mnuData && d.seq !== mnuData.seq) mnuOrder = null;
    mnuData = d || {};
    mnuSelfResolve();
    mnuResolveFromScene(mnuTargets((mnuData.entries) || []));
    mnuUpgradeKeys((mnuData.entries) || []);
    if (vis) { mnuNoteRecent(); mnuReconcileRecent(); }
    const ctx = (mnuData.seq | 0) + '|' + (mnuData.locId | 0) + '|' + (mnuData.handle | 0);
    if (ctx !== mnuPinCtx) { mnuPinCtx = ctx; mnuPush(); }
    if (vis) renderMenuSwap();
    } finally { mnuBusy = false; }
  }

  const mnuAnyTargeted = ents => (ents || []).some(e => !!mnuPlain(e.target));
  function mnuMovable(e, ents) {
    if ((e.slot | 0) === 0) return false;
    const tg = mnuPlain(e.target);
    if (tg && !mnuVarKeys(ents, tg).length) return false;
    return !(mnuAnyTargeted(ents) && !mnuPlain(e.target));
  }

  const mnuSep = '\u001f';   // unit separator: cannot occur in a menu verb or target
  const mnuKey = e => e.verb + mnuSep + mnuPlain(e.target);   // target may be empty; verb still distinguishes
  function mnuSplit(k) {
    const i = k.indexOf(mnuSep);
    return i < 0 ? { verb: k, target: '' } : { verb: k.slice(0, i), target: k.slice(i + 1) };
  }

  function mnuCurrent(ents) {
    const keys = ents.filter(function (e) { return mnuMovable(e, ents); }).map(mnuKey);
    if (mnuOrder) {
      const cur = keys.slice().sort().join(mnuVarSep);
      const old = mnuOrder.slice().sort().join(mnuVarSep);
      if (cur === old) return mnuOrder;
      mnuOrder = null;
    }
    const rankOf = function (k) {
      const p = mnuSplit(k);
      const rule = mnuRules[mnuRuleKey(ents, p.target)] || mnuRules['*'];
      const i = rule ? rule.indexOf(p.verb) : -1;
      return i < 0 ? 9999 : i;
    };
    const idx = {};
    keys.forEach(function (k, i) { idx[k] = i; });
    return keys.slice().sort(function (a, b) {
      const d = rankOf(a) - rankOf(b);
      return d !== 0 ? d : idx[a] - idx[b];      // stable: unruled rows keep game order
    });
  }

  function mnuMove(key, dir) {
    const ents = mnuActiveEnts();
    const cur = mnuCurrent(ents).slice();
    const i = cur.indexOf(key), j = i + dir;
    if (i < 0 || j < 0 || j >= cur.length) return;
    cur[i] = cur[j]; cur[j] = key;
    mnuOrder = cur;
    const byTgt = {};
    for (const k of cur) {
      const p = mnuSplit(k);
      const t = p.target ? mnuVariant(ents, p.target) : '';
      if (!t) continue;
      (byTgt[t] = byTgt[t] || []).push(p.verb);
    }
    for (const t of mnuTargets(ents))
        for (const k of mnuVarKeys(ents, t)) delete mnuRules[k];
    for (const t in byTgt) {
      mnuRules[t] = byTgt[t];
      const m = /^(item|loc|npc):/.exec(t);
      if (m) {
        for (const tg of mnuTargets(ents))
          if (mnuVarKeys(ents, tg)[0] === t) { mnuNames[t] = tg; break; }
      }
    }
    mnuPush();
    mnuNote = '';
    mnuSig = ''; mnuListSig = ''; renderMenuSwap();
  }

  function mnuAllRules() {
    const out = [];
    for (const key in mnuRules) {
      const verbs = mnuRules[key] || [];
      if (!verbs.length) continue;
      const m = /^(item|loc|npc):(\d+)(?:@[0-9a-z]+)?$/.exec(key);
      if (m) {
        const kindLabel = m[1] === 'loc' ? 'object' : m[1];
        out.push({
          key: key,
          name: mnuNames[key] || ('(' + kindLabel + ' ' + m[2] + ')'),
          scope: kindLabel + ' id ' + m[2],
          solid: true,                       // keyed on a config id: the durable kind
          verbs: verbs
        });
        continue;
      }
      const i = key.indexOf(mnuVarSep);
      const name = i < 0 ? key : key.slice(0, i);
      const gen = i < 0 ? '' : key.slice(i + 1);
      out.push({
        key: key,
        name: key === '*' ? 'Any object' : (mnuNames[key] || name || '(unnamed)'),
        scope: gen.charAt(0) === 'L' ? 'object id ' + gen.slice(1)
             : gen.charAt(0) === 'h' ? 'only this one spawn or tile - hover it again to upgrade'
             : gen ? 'matched by its options - hover it again to upgrade'
             : (key === '*' ? 'every object with these options' : 'matched by name'),
        solid: gen.charAt(0) === 'L',
        verbs: verbs
      });
    }
    out.sort((a, b) => a.name.localeCompare(b.name));
    return out;
  }

  function mnuEditRule(key) {
    mnuAdd = {
      name: mnuNames[key] || key.split(mnuVarSep)[0] || key,
      order: (mnuRules[key] || []).slice(),
      editKey: key
    };
    mnuView = 'edit';
    mnuSig = ''; mnuListSig = ''; renderMenuSwap();
  }

  function mnuAddMove(i, dir) {
    const j = i + dir;
    if (j < 0 || j >= mnuAdd.order.length) return;
    const t = mnuAdd.order[i]; mnuAdd.order[i] = mnuAdd.order[j]; mnuAdd.order[j] = t;
    mnuSig = ''; mnuListSig = ''; renderMenuSwap();
  }

  function mnuAddSave() {
    if (!mnuAdd.editKey) return;
    mnuRules[mnuAdd.editKey] = mnuAdd.order.slice();
    mnuPush();
    mnuAdd = { name: '', order: [], editKey: '' };
    mnuView = 'rules';
    mnuSig = ''; mnuListSig = ''; renderMenuSwap();
  }

  function mnuForget(key) {
    delete mnuRules[key];
    mnuPush();
    mnuSig = ''; mnuListSig = ''; renderMenuSwap();
  }

  function renderMenuSwap() {
    const c = paneRoot('menuswap'); if (!c) return;
    let wrap = $('mnuWrap');
    if (!wrap) {
      c.innerHTML = '';
      wrap = document.createElement('div');
      wrap.id = 'mnuWrap';
      wrap.innerHTML =
          '<div class="pet-toolbar" style="gap:6px;align-items:center">'
        + '  <button class="pet-chip" data-view="menu">Hovered</button>'
        + '  <button class="pet-chip" data-view="rules">Saved rules</button>'
        + '  <button class="pet-chip" id="mnuBack" style="margin-left:auto">Back</button>'
        + '</div>'
        + '<div id="mnuTgt" class="pet-count" style="font-weight:600;margin:2px 0 4px"></div>'
        + '<div id="mnuList" class="pet-list"></div>'
        + '<div id="mnuPromo" class="pet-count" style="opacity:.75;margin-top:6px;display:none"></div>'
        + '<div id="mnuNote" class="pet-count" style="opacity:.55;margin-top:2px"></div>'
        + '<div id="mnuInfo" class="pet-count" style="opacity:.35;margin-top:2px"></div>';
      c.appendChild(wrap);
      wrap.addEventListener('click', function (e) {
        const vb = e.target.closest('[data-view]');
        if (vb) {
          mnuConfirmForget = '';
          if (vb.dataset.view !== 'menu') mnuSel = '';
          mnuView = vb.dataset.view; mnuSig = ''; mnuListSig = ''; renderMenuSwap(); return;
        }
        const fg = e.target.closest('[data-forget]');
        if (fg) { mnuConfirmForget = fg.dataset.forget;
                  mnuSig = ''; mnuListSig = ''; renderMenuSwap(); return; }
        if (e.target.closest('[data-forgetno]')) {
          mnuConfirmForget = ''; mnuSig = ''; mnuListSig = ''; renderMenuSwap(); return;
        }
        const fy = e.target.closest('[data-forgetyes]');
        if (fy) { mnuConfirmForget = ''; mnuForget(fy.dataset.forgetyes); return; }
        const ed = e.target.closest('[data-edit]');
        if (ed) { mnuEditRule(ed.dataset.edit); return; }
        if (e.target.closest('#mnuAddSave')) { mnuAddSave(); return; }
        const au = e.target.closest('[data-aup]');
        if (au) { mnuAddMove(au.dataset.aup | 0, -1); return; }
        const ad = e.target.closest('[data-adown]');
        if (ad) { mnuAddMove(ad.dataset.adown | 0, 1); return; }
        const sl = e.target.closest('[data-sel]');
        if (sl) { mnuSel = sl.dataset.sel; mnuOrder = null; mnuSig = ''; mnuListSig = '';
                  renderMenuSwap(); return; }
        if (e.target.closest('#mnuBack')) {
          mnuConfirmForget = '';
          if (mnuView === 'edit') { mnuView = 'rules'; mnuAdd = { name: '', order: [], editKey: '' }; }
          mnuSel = ''; mnuOrder = null;
          mnuSig = ''; mnuListSig = ''; renderMenuSwap(); return;
        }
        const up = e.target.closest('[data-up]');
        if (up) { mnuMove(up.dataset.up, -1); return; }
        const dn = e.target.closest('[data-down]');
        if (dn) { mnuMove(dn.dataset.down, 1); return; }
      });
    }

    const d = mnuData || {};
    const ents = mnuActiveEnts();
    const tgts = mnuTargets(ents);
    const tgt = tgts[0] || '';
    const order = mnuCurrent(ents);
    const ruled = !!(mnuRules['*'] || tgts.some(function (t) {
      return mnuRules[mnuRuleKey(ents, t)];
    }));
    const statusSig = (d.ok ? 1 : 0) + '|' + (d.hooked ? 1 : 0) + '|' + (d.reordered | 0)
              + '|' + (d.diag || []).join(',') + '|' + (d.stage || []).join(',')
              + '|' + (d.lane || []).join(',') + '|' + mnuNote
              + '|' + (d.promo | 0) + '|' + (d.promoPrio | 0) + '|' + (d.promoPartner | 0)
              + '|' + (d.promoSrc | 0) + '|' + (d.promoVerb || '') + '|' + mnuConfirmForget + '|' + ruled;
    const listSig = order.join(',') + '|' + tgts.join('|') + '|' + (d.handle | 0)
              + '|' + JSON.stringify(mnuIdSeen) + '|' + mnuView
              + '|' + JSON.stringify(mnuRules)
              + '|' + mnuAdd.editKey + '|' + mnuAdd.order.join(',')
              + '|' + mnuConfirmForget + '|' + mnuSel
              + '|' + ents.map(mnuKey).join(',')
              + '|' + ents.map(e => (e.slot | 0) + ':' + (e.type | 0)).join(',');
    if (statusSig === mnuSig && listSig === mnuListSig) return;
    mnuSig = statusSig;

    const nRules = mnuAllRules().length;
    wrap.querySelectorAll('[data-view]').forEach(function (b) {
      const on = b.dataset.view === mnuView;
      b.className = 'pet-chip' + (on ? ' on' : '');
      if (b.dataset.view === 'rules') b.textContent = 'Saved rules (' + nRules + ')';
    });
    const menuOnly = mnuView === 'menu' ? '' : 'none';
    const openCap = ((mnuView === 'menu' && mnuSel) || mnuView === 'edit') ? '' : 'none';
    const bk = $('mnuBack');
    if (bk) bk.style.display = openCap;
    const tg = $('mnuTgt');
    if (tg) tg.style.display = (mnuView === 'menu') ? '' : 'none';
    if (tg) tg.textContent = tgts.length
      ? (tgts.join('   ·   ') + (ruled ? '   ·   reordered' : '')) : '';

    const info = $('mnuInfo');
    if (info) {
      const g = d.diag || [], st = d.stage || [];
      let why = '';
      if (ents.length && ruled && !(d.reordered | 0)) {
        why = !(g[2] | 0)  ? 'builder hook never fired'
            : !(g[3] | 0)  ? 'companion has no rules'
            : !(st[0] | 0) ? 'entry vector unusable at build time'
            : !(st[1] | 0) ? 'no match (last verb: "' + (d.lastVerb || '') + '")'
            : !(st[2] | 0) ? 'permutation incomplete'
            : !(st[3] | 0) ? 'order already correct'
            : d.unverified ? 'menu records did not pass the safety check on this game build, so nothing was moved'
                           : 'write failed';
      }
      const ln = d.lane || [];
      const lanes = ln.length
        ? ['menu', 'target', 'nocanc', 'misc', 'iface', 'class']
            .map(function (nm, i) { return (ln[i] | 0) ? nm : null; })
            .filter(Boolean).join('+')
        : '';
      info.textContent = !d.ok     ? 'Companion not loaded'
                       : !d.hooked ? 'Menu hook not installed for this build'
                       : why       ? why
                       : ents.length ? (d.reordered | 0) + ' reorders'
                                       + (lanes ? '  ·  ' + lanes : '')
                                     : '';
    }
    const promo = $('mnuPromo');
    if (promo) {
      const st = d.promo | 0, pv = d.promoVerb || 'that row';
      const src = (d.promoSrc | 0) === 1 ? ' (matched by behaviour, not structure)' : '';
      promo.textContent =
          st === 2 ? 'Left-click: "' + pv + '" promoted to the default' + src
        : st === 3 ? 'Left-click: "' + pv + '" is class ' + (d.promoPrio | 0)
                     + ', which has no proven promoted form - the game keeps its own default'
        : st === 4 ? 'Left-click: no promoted class found for "' + pv + '" (found '
                     + (d.promoPartner | 0) + ', expected 57) - a game update likely moved them'
        : st === 5 ? 'Left-click: the class write failed'
        : st === 6 ? 'Left-click: the row did not carry the expected class on this game build, so the default was left alone'
        : st === 7 ? 'Left-click: "' + pv + '" is now the default'
        : st === 8 ? 'Left-click: this game build does not expose the helper the default needs, so the game keeps its own'
        : '';
      promo.style.display = promo.textContent ? '' : 'none';
    }

    const note = $('mnuNote');
    if (note) {
      note.textContent = mnuNote
        || (mnuView === 'rules' ? 'Edit reorders a saved rule. Forget removes it.'
          : mnuView === 'edit'  ? 'Arrows set the order, then Save changes.'
          : mnuSel              ? 'Arrows set the order. It applies to every future menu for this object.'
                                : (mnuIfaceDiag || 'Hover things in game to collect them here, then pick one to reorder.'));
    }

    const list = $('mnuList');
    if (!list) return;
    if (listSig === mnuListSig) return;      // nothing the rows draw has changed: leave the DOM
    mnuListSig = listSig;

    if (mnuView === 'edit') {
      let ah = '<div class="pet-row" style="font-weight:600">' + htmlEsc(mnuAdd.name) + '</div>';
      if (!mnuAdd.order.length) {
        ah += '<div class="pet-row" style="opacity:.5">This rule has no options saved.</div>';
      } else {
        for (let i = 0; i < mnuAdd.order.length; i++) {
          ah += '<div class="pet-row">'
              + '<div style="flex:0 0 24px;opacity:.4">' + (i + 1) + '</div>'
              + '<div style="flex:1"><b>' + htmlEsc(mnuAdd.order[i]) + '</b></div>'
              + '<button class="pet-chip" data-aup="' + i + '"'
              + (i === 0 ? ' style="opacity:.2"' : '') + '>&#9650;</button>'
              + '<button class="pet-chip" data-adown="' + i + '"'
              + (i === mnuAdd.order.length - 1 ? ' style="opacity:.2"' : '') + '>&#9660;</button>'
              + '</div>';
        }
        ah += '<div class="pet-row"><button class="pet-chip on" id="mnuAddSave">'
            + 'Save changes</button></div>';
      }
      list.innerHTML = ah;
      return;
    }

    if (mnuView === 'rules') {
      const all = mnuAllRules();
      let ah = '';
      for (const r of all) {
        ah += '<div class="pet-row">'
            + '<div style="flex:1">'
            + '<b>' + htmlEsc(r.name) + '</b>'
            + (r.scope ? '<span style="opacity:' + (r.solid ? '.55' : '.45') + '"> &middot; '
                       + htmlEsc(r.scope) + '</span>' : '')
            + '<div style="opacity:.6;font-size:11px">Menu order: '
            + htmlEsc(r.verbs.map(mnuPlain).join('  →  ')) + '</div>'
            + '</div>'
            + (mnuConfirmForget === r.key
                ? '<span style="opacity:.7;font-size:11px;margin-right:6px">Delete it?</span>'
                  + '<button class="pet-chip on" data-forgetyes="' + htmlEsc(r.key) + '">Yes</button>'
                  + '<button class="pet-chip" data-forgetno="1">Cancel</button>'
                : '<button class="pet-chip" data-edit="' + htmlEsc(r.key) + '">Edit</button>'
                  + '<button class="pet-chip" data-forget="' + htmlEsc(r.key) + '">Forget</button>')
            + '</div>';
      }
      list.innerHTML = ah || '<div class="pet-row" style="opacity:.5">No saved reorderings yet.</div>';
      return;
    }
    if (mnuView === 'menu' && !mnuSel) {
      let rh = '';
      if (!mnuRecent.length) {
        rh = '<div class="pet-row" style="opacity:.5">Hover an object, NPC or item in game and'
           + ' it appears here to reorder.</div>';
      } else {
        for (const r of mnuRecent) {
          const ruledHere = !!mnuRules[r.key];
          let twin = false;
          for (const k in mnuRules)
            if (k !== r.key && (mnuNames[k] || k.split(mnuVarSep)[0]) === r.name) twin = true;
          const km = /^(item|loc|npc):(\d+)(?:@[0-9a-z]+)?$/.exec(r.key);
          let idTag;
          if (km) {
            idTag = (km[1] === 'loc' ? 'object' : km[1]) + ' id ' + km[2];
          } else {
            idTag = (mnuData && mnuData.ok) ? 'id not resolved'
                                            : 'companion/launcher version mismatch';
          }
          rh += '<div class="pet-row">'
              + '<div style="flex:1"><b>' + htmlEsc(r.name) + '</b>'
              + '<span style="opacity:.45"> &middot; ' + htmlEsc(idTag) + '</span>'
              + (ruledHere ? '<span style="opacity:.5"> &middot; reordered</span>'
                           : twin ? '<span style="opacity:.5"> &middot; another '
                                  + htmlEsc(r.name) + ' has its own rule</span>' : '')
              + '<div style="opacity:.5;font-size:11px">'
              + htmlEsc(r.ents.filter(e => mnuMovable(e, r.ents)).map(e => mnuPlain(e.verb)).join('  →  '))
              + '</div></div>'
              + (km ? '<button class="pet-chip" data-sel="' + htmlEsc(r.key) + '">Reorder</button>'
                    : '<button class="pet-chip" style="opacity:.3">Reorder</button>')
              + '</div>';
        }
      }
      list.innerHTML = rh;
      return;
    }

    let h = '';
    let mi = 0;
    for (let i = 0; i < ents.length; i++) {
      const diag = 'slot ' + (ents[i].slot | 0) + ' · raw "' + (ents[i].target || '') + '"';
      if (!mnuMovable(ents[i], ents)) {
        h += '<div class="pet-row" style="opacity:.45" title="' + htmlEsc(diag) + '">'
           + '<div style="flex:0 0 24px;font-variant-numeric:tabular-nums">' + (i + 1) + '</div>'
           + '<div style="flex:1">' + htmlEsc(mnuPlain(ents[i].verb) || '') + '</div>'
           + '<div style="font-size:11px">' + (function () {
               const tg = mnuPlain(ents[i].target), tl = mnuTargets(ents);
               return (tg && !mnuVarKeys(ents, tg).length)
                 ? 'no id for ' + htmlEsc(tg) + ' - cannot be reordered'
                 : 'fixed by the game';
             })() + '</div>'
           + '</div>';
        continue;
      }
      if (mi >= order.length) continue;
      const key = order[mi++];
      const p = mnuSplit(key);
      h += '<div class="pet-row" title="' + htmlEsc(diag) + '">'
         + '<div style="flex:0 0 24px;opacity:.4;font-variant-numeric:tabular-nums">' + (i + 1) + '</div>'
         + '<div style="flex:1"><b>' + htmlEsc(mnuPlain(p.verb) || '(none)') + '</b>'
         + (p.target ? '<span style="opacity:.45"> &middot; ' + htmlEsc(p.target) + '</span>' : '')
         + '</div>'
         + '<button class="pet-chip" data-up="' + htmlEsc(key) + '"'
         + (mi === 1 ? ' style="opacity:.2"' : '') + '>&#9650;</button>'
         + '<button class="pet-chip" data-down="' + htmlEsc(key) + '"'
         + (mi === order.length ? ' style="opacity:.2"' : '') + '>&#9660;</button>'
         + '</div>';
    }
    list.innerHTML = h;
  }

Object.assign(window, { mnuAddSave, mnuTick });
registerTab({ id: 'menuswap', render: renderMenuSwap });
})();
