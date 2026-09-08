// RuneToolsX panel: Artefact Collections (Archaeology faction artefact sets).
(function () {

  let acFetching = false, acFetchAt = 0, acSig = '';
  async function fetchArchCol() {
    if (!bridge() || acFetching) return;
    const t = Date.now();
    if (t - acFetchAt < 2000) return;
    acFetchAt = t; acFetching = true;
    try { await bcLoadCols(); await bcLoadVarps(); } catch (e) {}
    acFetching = false;
    paneRun('archcol', renderArchCol);
  }
  function renderArchCol() {
    const c = $('content');
    let wrap = $('acWrap');
    if (!wrap) {
      c.innerHTML = ''; acSig = '';
      bcEnsureCss();
      wrap = document.createElement('div'); wrap.id = 'acWrap'; wrap.className = 'pane'; c.appendChild(wrap);
      const sec = document.createElement('div'); sec.className = 'bc-wrap'; wrap.appendChild(sec);
      const head = document.createElement('div'); head.id = 'acHead'; head.className = 'bc-sec';
      head.textContent = 'Artefact collections'; sec.appendChild(head);
      const list = document.createElement('div'); list.id = 'acList'; list.className = 'bc-card'; sec.appendChild(list);
    }
    const box = $('acList');
    if (!bcCols || !bcVarps || !bcVbIndex) {
      if (acSig !== 'wait') {
        acSig = 'wait';
        box.innerHTML = '<div class="bc-more">Reading collections from the cache... (be in-world)</div>';
      }
      return;
    }
    const cols = bcCols.filter(col => col.cat === 'arch');
    const sig = bcColSig(cols);
    if (sig === acSig) return;
    acSig = sig;
    const done = bcPaintList(box, cols);
    const head = $('acHead');
    if (head) head.textContent = 'Artefact collections · ' + done + ' / ' + cols.length + ' complete';
  }

Object.assign(window, { fetchArchCol });
registerTab({ id: 'archcol', render: renderArchCol, open: function () { acSig = ''; fetchArchCol(); } });
})();
