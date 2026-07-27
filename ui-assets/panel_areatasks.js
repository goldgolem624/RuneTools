// RuneToolsX panel: Area Tasks (achievement-diary "Set Tasks", cache category 4766).
// Spliced inline into client.html; bare classic script sharing one global scope.
// Data = the achievements cache: each parent's `subach` list gives the task rows, completion
// via the same varbit tracking the Achievements tab uses. The generic engine
// (buildCategoryTasks + renderTaskScaffold) is reused by panel_gimtasks.js (category 5483).

  let areaTasksData = null;   // null = not read yet, [] = read but none found
  const areaState = { search: '', status: 0, tier: -1, group: '', sig: '' };
  const AREA_TIERS = ['Beginner', 'Easy', 'Medium', 'Hard', 'Elite'];   // diary tasks stop at Elite (no Master)
  const AREA_CFG = {
    state: areaState, wrapId: 'areaWrap', listId: 'areaList', cntId: 'areaCnt', groupSelId: 'areaGroup',
    searchPh: 'Search task...', groupLabel: 'Region', tiers: AREA_TIERS, noun: 'task', cats: [4766],
    // "Lumbridge Set Tasks - Easy" -> { group:'Lumbridge', tierIdx: 1 }
    parse: (name) => {
      const m = (name || '').match(/^(.*?)\s+Set Tasks\b.*?-\s*([A-Za-z]+)/);
      return { group: m ? m[1].trim() : (name || '').trim(),
               tierIdx: m ? AREA_TIERS.findIndex(t => t.toLowerCase() === m[2].toLowerCase()) : -1 };
    },
    data: () => areaTasksData, render: () => renderAreaTasks(),
    emptyMsg: 'No area tasks found in the cache.',
  };

  async function fetchAreaTasks(force) {
    await fetchAchievements(force);
    areaTasksData = buildCategoryTasks(AREA_CFG);
    if (activeTab === 'areatasks') renderAreaTasks();
  }
  function renderAreaTasks() { renderTaskScaffold(AREA_CFG); }

  function buildCategoryTasks(cfg) {
    if (!achDefs) return null;
    const byId = achDefById(); const st = achState || { done: new Set() };
    const out = [];
    for (const p of achDefs) {
      if (cfg.cats.indexOf(p.cat) < 0 || !p.subach || !p.subach.length) continue;
      const pg = cfg.parse(p.name || '');
      for (const cid of p.subach) {
        const ch = byId[cid]; if (!ch) continue;
        let nm = ch.name || ('#' + cid);
        if (p.name && nm.indexOf(p.name + ':') === 0) nm = nm.slice(p.name.length + 1).trim();
        out.push({ id: cid, name: nm, desc: (typeof ch.desc === 'string' ? ch.desc : ''),
                   group: pg.group, tierIdx: (pg.tierIdx != null ? pg.tierIdx : -1),
                   done: st.done.has(cid), parent: p.name || '' });
      }
    }
    return out;
  }

  function renderTaskScaffold(cfg) {
    const s = cfg.state, c = $('content');
    let wrap = $(cfg.wrapId);
    if (!wrap) {
      c.innerHTML = ''; s.sig = '';
      wrap = document.createElement('div'); wrap.id = cfg.wrapId; wrap.className = 'pk-wrap'; c.appendChild(wrap);
      const tb = document.createElement('div'); tb.className = 'pet-toolbar';
      const stRow = document.createElement('div'); stRow.className = 'pet-chips';
      ['All', 'Complete', 'Incomplete'].forEach((nm, i) => {
        const b = document.createElement('button'); b.className = 'pet-chip' + (i === s.status ? ' on' : '');
        b.textContent = nm; b.dataset.val = i; stRow.appendChild(b);
      });
      const search = document.createElement('input'); search.className = 'pet-search'; search.placeholder = cfg.searchPh; search.value = s.search;
      tb.appendChild(stRow); tb.appendChild(search); wrap.appendChild(tb);
      if (cfg.tiers && cfg.tiers.length) {
        const tierRow = document.createElement('div'); tierRow.className = 'pet-toolbar';
        const tchips = document.createElement('div'); tchips.className = 'pet-chips';
        const mkTier = (label, val) => { const b = document.createElement('button'); b.className = 'pet-chip' + (val === s.tier ? ' on' : ''); b.textContent = label; b.dataset.tier = val; return b; };
        tchips.appendChild(mkTier('All tiers', -1));
        cfg.tiers.forEach((nm, i) => tchips.appendChild(mkTier(nm, i)));
        tierRow.appendChild(tchips); wrap.appendChild(tierRow);
        tchips.addEventListener('click', e => { const b = e.target.closest('.pet-chip'); if (!b) return; s.tier = +b.dataset.tier; tchips.querySelectorAll('.pet-chip').forEach(x => x.classList.toggle('on', x === b)); s.sig = ''; cfg.render(); });
      }
      // group dropdown (custom dark dropdown; native <select> popups render OS-white in Ultralight)
      const grpRow = document.createElement('div'); grpRow.className = 'pet-toolbar';
      const gdd = mkDropdown(s.group, (v) => { s.group = v; s.sig = ''; cfg.render(); }); gdd.id = cfg.groupSelId;
      grpRow.appendChild(gdd); wrap.appendChild(grpRow);
      const cnt = document.createElement('div'); cnt.id = cfg.cntId; cnt.className = 'pet-count'; wrap.appendChild(cnt);
      const list = document.createElement('div'); list.id = cfg.listId; list.className = 'pet-list'; wrap.appendChild(list);
      stRow.addEventListener('click', e => { const b = e.target.closest('.pet-chip'); if (!b) return; s.status = +b.dataset.val; stRow.querySelectorAll('.pet-chip').forEach(x => x.classList.toggle('on', x === b)); s.sig = ''; cfg.render(); });
      search.addEventListener('input', () => { s.search = search.value.toLowerCase(); s.sig = ''; cfg.render(); });
    }
    populateTaskGroups(cfg);
    renderTaskList(cfg);
  }

  function populateTaskGroups(cfg) {
    const dd = $(cfg.groupSelId); if (!dd || !dd.setItems) return;
    const data = cfg.data() || [];
    const groups = [...new Set(data.map(t => t.group))].filter(Boolean).sort((a, b) => a.localeCompare(b));
    if (dd.dataset.n === String(groups.length)) return;
    dd.dataset.n = String(groups.length);
    const items = [{ value: '', label: 'All ' + cfg.groupLabel.toLowerCase() + 's' }];
    for (const g of groups) items.push({ value: g, label: g });
    dd.setItems(items, cfg.state.group);
  }

  function taskRowEl(t, cfg) {
    const row = document.createElement('div'); row.className = 'pet-row' + (t.done ? '' : ' pet-locked');
    const tip = [t.name];
    if (t.desc) tip.push(t.desc);
    if (t.group) tip.push(cfg.groupLabel + ': ' + t.group);
    if (cfg.tiers && cfg.tiers[t.tierIdx]) tip.push('Difficulty: ' + cfg.tiers[t.tierIdx]);
    tip.push('Status: ' + (t.done ? 'complete' : 'incomplete'));
    row.dataset.tip = tip.join('\n');
    const info = document.createElement('div'); info.className = 'bs-info';
    const top = document.createElement('div'); top.className = 'bs-top';
    const nm = document.createElement('div'); nm.className = 'bs-nm'; nm.textContent = t.name || ('#' + t.id);
    top.appendChild(nm);
    info.appendChild(top);
    if (t.desc || t.group) {
      const sub = document.createElement('div'); sub.className = 'bs-chips';
      if (t.group) { const g = document.createElement('span'); g.className = 'bs-chip'; g.textContent = t.group; sub.appendChild(g); }
      if (t.desc) { const d = document.createElement('span'); d.className = 'ach-desc'; d.textContent = t.desc; sub.appendChild(d); }
      info.appendChild(sub);
    }
    row.appendChild(info);
    // fixed-width difficulty + status columns so they line up across rows
    if (cfg.tiers && cfg.tiers[t.tierIdx]) { const tb = document.createElement('span'); tb.className = 'clue-tier t' + t.tierIdx; tb.textContent = cfg.tiers[t.tierIdx]; row.appendChild(tb); }
    const st2 = document.createElement('div'); st2.className = 'pet-st task-st ' + (t.done ? 'pet-yes' : 'pet-no');
    st2.textContent = t.done ? 'Complete' : 'Incomplete';
    row.appendChild(st2);
    return row;
  }

  function renderTaskList(cfg) {
    const s = cfg.state, list = $(cfg.listId); if (!list) return;
    const data = cfg.data(); const cnt = $(cfg.cntId);
    if (data === null) { list.innerHTML = '<div class="empty">Reading achievement cache...</div>'; if (cnt) cnt.textContent = ''; s.sig = ''; return; }
    if (!data.length) { list.innerHTML = '<div class="empty">' + cfg.emptyMsg + '</div>'; if (cnt) cnt.textContent = ''; s.sig = ''; return; }
    const items = data.filter(t => {
      if (s.tier >= 0 && t.tierIdx !== s.tier) return false;
      if (s.group && t.group !== s.group) return false;
      if (s.status === 1 && !t.done) return false;
      if (s.status === 2 && t.done) return false;
      if (s.search && (t.name || '').toLowerCase().indexOf(s.search) < 0 &&
          (t.desc || '').toLowerCase().indexOf(s.search) < 0) return false;
      return true;
    });
    items.sort((a, b) => (a.done ? 1 : 0) - (b.done ? 1 : 0) || (a.tierIdx - b.tierIdx) ||
      (a.group || '').localeCompare(b.group || '') || (a.name || '').localeCompare(b.name || ''));
    const doneN = data.filter(t => t.done).length;
    if (cnt) cnt.textContent = doneN + ' / ' + data.length + ' complete' +
      (items.length !== data.length ? '  ·  ' + items.length + ' shown' : '');
    const sig = s.status + '|' + s.tier + '|' + s.group + '|' + s.search + '|' +
      items.map(t => t.id + (t.done ? 'D' : '')).join(',');
    if (sig === s.sig) return;
    s.sig = sig;
    list.innerHTML = '';
    if (!items.length) { list.innerHTML = '<div class="empty">No ' + cfg.noun + 's match.</div>'; return; }
    for (const t of items) list.appendChild(taskRowEl(t, cfg));
  }
