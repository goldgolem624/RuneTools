// RuneToolsX panel: Group Ironman Tasks (the "Journey" achievements, cache category 5483).
(function () {

  let gimTasksData = null;
  const gimState = { search: '', status: 0, tier: -1, group: '', sig: '' };
  const GIM_CFG = {
    state: gimState, wrapId: 'gimWrap', listId: 'gimList', cntId: 'gimCnt', groupSelId: 'gimGroup',
    searchPh: 'Search task...', groupLabel: 'Tier', tiers: [], noun: 'task', cats: [5483],
    parse: (name) => ({ group: (name || '').replace(/:.*$/, '').trim(), tierIdx: -1 }),
    data: () => gimTasksData, render: () => renderGimTasks(),
    emptyMsg: 'No Group Ironman tasks found in the cache.',
  };

  let gimBuiltFor;                  // the evaluation the rows were built from
  async function fetchGimTasks(force) {
    await fetchAchievements(force);
    if (!gimTasksData || gimBuiltFor !== achState) { gimTasksData = buildCategoryTasks(GIM_CFG); gimBuiltFor = achState; }
    paneRun('gimtasks', renderGimTasks);
  }
  function renderGimTasks() { renderTaskScaffold(GIM_CFG); }

Object.assign(window, { fetchGimTasks });
registerTab({ id: 'gimtasks', render: renderGimTasks, open: function () { gimState.sig = ''; fetchGimTasks(true); } });
})();
