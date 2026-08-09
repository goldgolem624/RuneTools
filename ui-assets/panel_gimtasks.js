// RuneToolsX panel: Group Ironman Tasks (the "Journey" achievements, cache category 5483).
// Spliced inline into client.html; bare classic script sharing one global scope.
// Uses the generic task engine from panel_areatasks.js; the Journey tier is the group
// (cfg.tiers empty -> no difficulty chip row).

  let gimTasksData = null;
  const gimState = { search: '', status: 0, tier: -1, group: '', sig: '' };
  const GIM_CFG = {
    state: gimState, wrapId: 'gimWrap', listId: 'gimList', cntId: 'gimCnt', groupSelId: 'gimGroup',
    searchPh: 'Search task...', groupLabel: 'Tier', tiers: [], noun: 'task', cats: [5483],
    // "Journey Tier 0" -> { group:'Journey Tier 0' } (parent name is the group; no separate tier)
    parse: (name) => ({ group: (name || '').replace(/:.*$/, '').trim(), tierIdx: -1 }),
    data: () => gimTasksData, render: () => renderGimTasks(),
    emptyMsg: 'No Group Ironman tasks found in the cache.',
  };

  async function fetchGimTasks(force) {
    await fetchAchievements(force);
    gimTasksData = buildCategoryTasks(GIM_CFG);
    paneRun('gimtasks', renderGimTasks);
  }
  function renderGimTasks() { renderTaskScaffold(GIM_CFG); }
