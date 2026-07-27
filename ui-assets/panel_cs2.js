// RuneToolsX panel: CS2 Scripts (Developer). Runs the cs2export sidecar against this PC's live
// cache (bridge cs2Extract/cs2Status), then searches/views the extracted clientscript-<id>.ts
// files (bridge cs2Search/cs2Script).
// Spliced inline into client.html; bare classic script sharing one global scope.

  let cs2Status = null, cs2StatusAt = 0, cs2View = null, cs2LastQuery = '';
  function cs2Bridge(fn) {
    try { const b = bridge(); if (b && b[fn]) return b[fn]; } catch (e) {}
    return null;
  }
  function cs2Poll(force) {
    const now = Date.now();
    if (!force && now - cs2StatusAt < 1000) return;
    cs2StatusAt = now;
    const f = cs2Bridge('cs2Status');
    if (!f) return;
    try { cs2Status = JSON.parse(f() || 'null'); } catch (e) { cs2Status = null; }
    cs2PaintStatus();
  }
  function cs2Pill(box, label, value, cls, tip) {
    const pill = document.createElement('span'); pill.className = 'cs2-pill' + (cls ? ' ' + cls : '');
    if (tip) pill.title = tip;
    if (label) { const lb = document.createElement('span'); lb.className = 'lb'; lb.textContent = label; pill.appendChild(lb); }
    const vl = document.createElement('span'); vl.className = 'vl'; vl.textContent = value;
    pill.appendChild(vl);
    box.appendChild(pill);
  }
  function cs2PaintStatus() {
    const s = cs2Status, box = $('cs2Meta');
    if (!box || !s) return;
    const m = s.meta, p = s.progress;
    const outdated = !!(m && s.clientVer && s.extractVer && s.clientVer !== s.extractVer);
    const sig = JSON.stringify([m && m.date, s.extractVer, s.clientVer, s.sidecar, outdated]);
    if (box._sig !== sig) {
      box._sig = sig;
      box.innerHTML = '';
      if (m && m.date) {
        const d = new Date(m.date);
        cs2Pill(box, 'extracted', d.toLocaleDateString() + ' ' + d.toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' }));
        cs2Pill(box, 'client', 'v' + (s.extractVer || '?'), outdated ? 'warn' : '',
          outdated ? 'Installed client is v' + s.clientVer + ' - re-extract to refresh' : 'Game revision at extraction time');
        cs2Pill(box, 'scripts', m.ok.toLocaleString(), '',
          m.ok.toLocaleString() + ' of ' + m.total.toLocaleString() + ' decompiled' +
          (m.failed && m.failed.length ? ' (' + m.failed.length + ' failed)' : ''));
        cs2Pill(box, 'named vars', ((m.names && (m.names.varbit + m.names.varp)) || 0).toLocaleString(), '',
          'Ground-truth renames: quest trackers + npc/loc morph vars');
        if (m.annotations && m.annotations.total)
          cs2Pill(box, 'annotations', m.annotations.total.toLocaleString(), '',
            'Inline /* name */ comments: items, npcs, locs, quests, stats, achievements, dbrows, structs, enum values');
        if (outdated) cs2Pill(box, '', 'game updated - re-extract', 'warn');
      } else {
        cs2Pill(box, '', 'No extraction yet', 'mut');
      }
      if (!s.sidecar) cs2Pill(box, '', 'sidecar not installed', 'warn', 'Set RTX_CS2_SIDECAR to the folder containing dist\\cs2export.js');
    }
    const btn = $('cs2Btn'), bar = $('cs2BarWrap'), fill = $('cs2BarFill'), lab = $('cs2BarLab');
    if (!btn) return;
    if (s.running) {
      btn.textContent = 'Cancel extraction';
      btn.classList.add('danger');
      if (bar) bar.style.display = '';
      const done = (p && p.done) || 0, total = (p && p.total) || 0;
      const pct = total ? Math.floor(done * 100 / total) : 0;
      if (fill) fill.style.width = pct + '%';
      if (lab) lab.textContent = (p && p.stage === 'names') ? 'Building name tables from the cache…'
        : (p && p.stage === 'xref') ? 'Cross-referencing cache names (' + done + '/' + total + ')…'
        : (p && p.stage === 'init') ? 'Opening cache…'
        : total ? ('Decompiling ' + done.toLocaleString() + ' / ' + total.toLocaleString() + '  (' + pct + '%)')
        : 'Starting…';
    } else {
      btn.textContent = m ? 'Re-extract CS2 scripts' : 'Start extraction of CS2 scripts';
      btn.classList.remove('danger');
      btn.disabled = !s.sidecar;
      if (bar) bar.style.display = 'none';
    }
  }
  function cs2DoExtract() {
    const s = cs2Status;
    const fn = cs2Bridge(s && s.running ? 'cs2Cancel' : 'cs2Extract');
    if (!fn) return;
    try { fn(); } catch (e) {}
    cs2Poll(true);
  }
  function cs2Row(r) {
    const row = document.createElement('div'); row.className = 'cs2-row';
    const idc = document.createElement('span'); idc.className = 'cs2-id';
    idc.textContent = r.id + ':' + r.line;
    const tx = document.createElement('span'); tx.className = 'cs2-line';
    tx.textContent = r.text.trim();
    row.appendChild(idc); row.appendChild(tx);
    row.addEventListener('click', () => cs2Open(r.id, r.line));
    return row;
  }
  function cs2DoSearch() {
    const inp = $('cs2Search'), res = $('cs2Results');
    if (!inp || !res) return;
    const q = inp.value.trim();
    if (!q) return;
    cs2LastQuery = q;
    cs2View = null;
    const view = $('cs2Viewer'); if (view) view.style.display = 'none';
    res.style.display = '';
    res.textContent = 'Searching…';
    setTimeout(() => {                       // paint "Searching..." before the sync bridge call
      const f = cs2Bridge('cs2Search');
      if (!f) { res.textContent = 'Bridge unavailable.'; return; }
      let out = null;
      try { out = JSON.parse(f(q, 300) || 'null'); } catch (e) {}
      res.textContent = '';
      if (!out || out.err) { res.textContent = (out && out.err) || 'Search failed.'; return; }
      const hd = document.createElement('div'); hd.className = 'cs2-reshead';
      hd.textContent = out.results.length + ' match' + (out.results.length === 1 ? '' : 'es')
        + (out.truncated ? ' (capped — refine the query)' : '') + ' across ' + out.files.toLocaleString() + ' scripts';
      res.appendChild(hd);
      out.results.forEach(r => res.appendChild(cs2Row(r)));
      if (!out.results.length) {
        const e = document.createElement('div'); e.className = 'empty'; e.textContent = 'No matches.';
        res.appendChild(e);
      }
    }, 30);
  }
  function cs2Open(id, line) {
    const res = $('cs2Results'), view = $('cs2Viewer');
    if (!view) return;
    const f = cs2Bridge('cs2Script');
    if (!f) return;
    let out = null;
    try { out = JSON.parse(f(id, 0) || 'null'); } catch (e) {}
    if (!out || out.err) return;
    let text = out.text, more = !!out.more;
    // Big scripts arrive in 200KB chunks: keep fetching until the target line is actually loaded.
    while (more && text.split('\n').length < line) {
      let nx = null;
      try { nx = JSON.parse(f(id, text.length) || 'null'); } catch (e) {}
      if (!nx || nx.err || !nx.text) break;
      text += nx.text; more = !!nx.more;
    }
    cs2View = { id, size: out.size, got: text.length };
    if (res) res.style.display = 'none';
    view.style.display = '';
    $('cs2ViewTitle').textContent = 'clientscript-' + id + '.ts  (' + (out.size / 1024).toFixed(1) + ' KB)';
    $('cs2More').style.display = more ? '' : 'none';
    const pre = $('cs2Pre');
    const scroll = pre.parentElement;
    // Land on the SOURCE line by scrolling to a real element wrapped around it. With pre-wrap a long
    // decompiled line renders as several visual rows, so any guess from scrollHeight fractions
    // drifts far off target. The marker span doubles as the search-hit highlight.
    let startOff = (line > 1) ? -1 : 0;
    if (line > 1) {
      let p = 0, n = 1;
      while (n < line) {
        p = text.indexOf('\n', p);
        if (p < 0) break;
        ++p; ++n;
      }
      if (n === line) startOff = p;
    }
    pre.textContent = '';
    if (startOff >= 0) {
      let endOff = text.indexOf('\n', startOff);
      if (endOff < 0) endOff = text.length;
      pre.appendChild(document.createTextNode(text.slice(0, startOff)));
      const mk = document.createElement('span'); mk.id = 'cs2Mark'; mk.className = 'cs2-mark';
      mk.textContent = text.slice(startOff, endOff);
      pre.appendChild(mk);
      pre.appendChild(document.createTextNode(text.slice(endOff)));
      requestAnimationFrame(() => {
        const m = $('cs2Mark');
        if (!m) return;
        const y = m.getBoundingClientRect().top - pre.getBoundingClientRect().top;
        scroll.scrollTop = Math.max(0, y - scroll.clientHeight / 3);
      });
    } else {
      pre.textContent = text;
      scroll.scrollTop = 0;
    }
  }
  function cs2LoadMore() {
    if (!cs2View) return;
    const f = cs2Bridge('cs2Script');
    if (!f) return;
    let out = null;
    try { out = JSON.parse(f(cs2View.id, cs2View.got) || 'null'); } catch (e) {}
    if (!out || out.err) return;
    // Append as a sibling text node: assigning textContent would flatten the children and drop the
    // cs2Mark highlight span.
    $('cs2Pre').appendChild(document.createTextNode(out.text));
    cs2View.got += out.text.length;
    $('cs2More').style.display = out.more ? '' : 'none';
  }
  function renderCs2() {
    const c = $('content');
    if ($('cs2Wrap')) { cs2Poll(false); return; }
    c.innerHTML = '';
    const wrap = document.createElement('div'); wrap.id = 'cs2Wrap'; wrap.className = 'cs2-wrap';

    const meta = document.createElement('div'); meta.id = 'cs2Meta'; meta.className = 'cs2-meta';
    wrap.appendChild(meta);

    const ctl = document.createElement('div'); ctl.className = 'cs2-ctl';
    const btn = document.createElement('button'); btn.id = 'cs2Btn'; btn.className = 'cs2-btn';
    btn.textContent = 'Start extraction of CS2 scripts';
    btn.addEventListener('click', cs2DoExtract);
    ctl.appendChild(btn);
    const barWrap = document.createElement('div'); barWrap.id = 'cs2BarWrap'; barWrap.className = 'cs2-barwrap';
    barWrap.style.display = 'none';
    const bar = document.createElement('div'); bar.className = 'cs2-bar';
    const fill = document.createElement('div'); fill.id = 'cs2BarFill'; fill.className = 'cs2-barfill';
    bar.appendChild(fill);
    const lab = document.createElement('div'); lab.id = 'cs2BarLab'; lab.className = 'cs2-barlab';
    barWrap.appendChild(bar); barWrap.appendChild(lab);
    ctl.appendChild(barWrap);
    wrap.appendChild(ctl);

    const srch = document.createElement('div'); srch.className = 'cs2-searchrow';
    const inp = document.createElement('input'); inp.id = 'cs2Search'; inp.className = 'vw-search';
    inp.placeholder = 'Search all scripts (text, varbitplayer_53549, vb9664_, script2158…)';
    inp.addEventListener('keydown', e => { if (e.key === 'Enter') cs2DoSearch(); });
    const go = document.createElement('button'); go.className = 'cs2-btn'; go.textContent = 'Search';
    go.addEventListener('click', cs2DoSearch);
    srch.appendChild(inp); srch.appendChild(go);
    wrap.appendChild(srch);

    const res = document.createElement('div'); res.id = 'cs2Results'; res.className = 'cs2-results';
    wrap.appendChild(res);

    const view = document.createElement('div'); view.id = 'cs2Viewer'; view.className = 'cs2-viewer';
    view.style.display = 'none';
    const vh = document.createElement('div'); vh.className = 'cs2-viewhead';
    const back = document.createElement('button'); back.className = 'cs2-btn'; back.textContent = '← Results';
    back.addEventListener('click', () => {
      view.style.display = 'none';
      const r = $('cs2Results'); if (r) r.style.display = '';
    });
    const vt = document.createElement('span'); vt.id = 'cs2ViewTitle'; vt.className = 'cs2-viewtitle';
    vh.appendChild(back); vh.appendChild(vt);
    view.appendChild(vh);
    const scroll = document.createElement('div'); scroll.className = 'cs2-scroll';
    const pre = document.createElement('pre'); pre.id = 'cs2Pre'; pre.className = 'cs2-pre';
    scroll.appendChild(pre);
    const moreBtn = document.createElement('button'); moreBtn.id = 'cs2More'; moreBtn.className = 'cs2-btn';
    moreBtn.textContent = 'Load more'; moreBtn.style.display = 'none';
    moreBtn.addEventListener('click', cs2LoadMore);
    scroll.appendChild(moreBtn);
    view.appendChild(scroll);
    wrap.appendChild(view);

    c.appendChild(wrap);
    cs2Poll(true);
  }
