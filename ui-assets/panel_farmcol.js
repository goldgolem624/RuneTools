// RuneToolsX panel: Farm Collections (Player-Owned Farm animal breed collections).
// Spliced inline into client.html; bare classic script sharing one global scope.
// Data + per-item found varbits come from the shared collection loader in panel_bosses.js
// (dbrows master 84, category 'pof'; found flags via COLLECTION_ITEM_VBS / item param 8994,
// values sliced from varpsDumpAll).

  let fcFetching = false, fcFetchAt = 0, fcSig = '';
  async function fetchFarmCol() {
    if (!bridge() || fcFetching) return;
    const t = Date.now();
    if (t - fcFetchAt < 2000) return;
    fcFetchAt = t; fcFetching = true;
    try { await bcLoadCols(); await bcLoadVarps(); } catch (e) {}
    fcFetching = false;
    if (activeTab === 'farmcol') renderFarmCol();
  }
  function renderFarmCol() {
    const c = $('content');
    let wrap = $('fcWrap');
    if (!wrap) {
      c.innerHTML = ''; fcSig = '';
      bcEnsureCss();
      wrap = document.createElement('div'); wrap.id = 'fcWrap'; wrap.className = 'pane'; c.appendChild(wrap);
      const sec = document.createElement('div'); sec.className = 'bc-wrap'; wrap.appendChild(sec);
      const head = document.createElement('div'); head.id = 'fcHead'; head.className = 'bc-sec';
      head.textContent = 'Farm collections'; sec.appendChild(head);
      const list = document.createElement('div'); list.id = 'fcList'; list.className = 'bc-card'; sec.appendChild(list);
    }
    const box = $('fcList');
    if (!bcCols || !bcVarps || !bcVbIndex) {
      if (fcSig !== 'wait') {
        fcSig = 'wait';
        box.innerHTML = '<div class="bc-more">Reading collections from the cache... (be in-world)</div>';
      }
      return;
    }
    const cols = bcCols.filter(col => col.cat === 'pof');
    const sig = bcColSig(cols);
    if (sig === fcSig) return;
    fcSig = sig;
    const done = bcPaintList(box, cols);
    const head = $('fcHead');
    if (head) head.textContent = 'Farm collections · ' + done + ' / ' + cols.length + ' complete';
  }
