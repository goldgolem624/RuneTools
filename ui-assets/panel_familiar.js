// RuneToolsX panel: Familiar (summoned familiar stats + beast-of-burden storage).
// Spliced inline into client.html; bare classic script sharing one global scope.

  // Pouch = varp 1831 (-1/0 = none); LP = varbits 19034 cur / 27403 max; spell points = varp 1787
  // (max 60); time = varbits 6055 min + 6054 half (alt-mode 27747 -> 27749/27750);
  // beast-of-burden storage = live container 530.
  let famData = null, famFetching = false, famSig = '';
  const FAM_NAMES = {};   // pouch item id -> cache name (memoised)
  async function fetchFamiliar() {
    if (!bridge() || famFetching) return; famFetching = true;
    try {
      let vp = {}; try { vp = JSON.parse(await bridge().varps(myPid(), '1831,1787')); } catch (e) {}
      const vb = await readVarbitValues([19034, 27403, 6055, 6054, 27747, 27749, 27750]);
      let items = null; try { items = JSON.parse(await bridge().containerItems(myPid(), 530)); } catch (e) {}
      let pouch = vp['1831'] | 0; if (pouch <= 0 || pouch >= 0x7FFFFFFF) pouch = 0;
      if (pouch && FAM_NAMES[pouch] === undefined) {
        try { FAM_NAMES[pouch] = (JSON.parse(await bridge().itemInfo(pouch)).name) || ('Item #' + pouch); }
        catch (e) { FAM_NAMES[pouch] = 'Item #' + pouch; }
      }
      const alt = vb[27747] === 1;
      famData = {
        pouch: pouch, name: pouch ? FAM_NAMES[pouch] : '',
        sp: vp['1787'] | 0, lp: vb[19034] | 0, lpMax: vb[27403] | 0,
        min: (alt ? vb[27749] : vb[6055]) | 0, half: (alt ? vb[27750] : vb[6054]) | 0,
        inv: (items && items.present) ? items : { present: false, count: 0, cap: 0, items: [] },
      };
    } finally { famFetching = false; }
    paneRun('familiar', renderFamiliar);
  }
  function renderFamiliar() {
    const c = $('content');
    let wrap = $('famWrap');
    if (!wrap) { c.innerHTML = ''; wrap = document.createElement('div'); wrap.id = 'famWrap'; wrap.className = 'pk-wrap'; c.appendChild(wrap); famSig = ''; }
    const d = famData;
    if (!d) { wrap.innerHTML = '<div class="stor-empty">Reading...</div>'; famSig = ''; return; }
    if (!d.pouch) { wrap.innerHTML = '<div class="stor-empty">No familiar summoned.</div>'; famSig = ''; return; }
    const sig = JSON.stringify([d.pouch, d.lp, d.lpMax, d.sp, d.min, d.half, d.inv.count, d.inv.cap,
                                d.inv.items.map(it => it[1] + 'x' + it[2]).join(',')]);
    if (sig === famSig) return; famSig = sig;
    wrap.innerHTML = '';
    const head = document.createElement('div'); head.className = 'fam-head';
    const ico = document.createElement('div'); ico.className = 'fam-ico'; attachBankIcon(ico, d.pouch);
    const nm = document.createElement('span'); nm.className = 'fam-nm';
    nm.textContent = d.name.replace(/ pouch$/i, '');
    const tm = document.createElement('span'); tm.className = 'fam-time';
    tm.textContent = d.min + ':' + (d.half ? '30' : '00');
    tm.title = 'Time remaining until your familiar desummons';
    head.appendChild(ico); head.appendChild(nm); head.appendChild(tm); wrap.appendChild(head);
    const bar = (label, cur, max, cls) => {
      const s = document.createElement('div'); s.className = 'fam-stat';
      const h = document.createElement('div'); h.className = 'fam-stat-h';
      h.innerHTML = '<span>' + label + '</span><b>' + cur.toLocaleString() + ' / ' + max.toLocaleString() + '</b>';
      const b = document.createElement('div'); b.className = 'fam-bar';
      const f = document.createElement('div'); f.className = cls;
      f.style.width = (max > 0 ? Math.max(0, Math.min(100, cur * 100 / max)) : 0) + '%';
      b.appendChild(f); s.appendChild(h); s.appendChild(b); return s;
    };
    wrap.appendChild(bar('Life points', d.lp, d.lpMax, 'hp'));
    wrap.appendChild(bar('Spell points', d.sp, 60, 'sp'));
    // beast-of-burden storage (live container 530)
    if (d.inv.cap > 0) {
      const grp = document.createElement('div'); grp.className = 'fm-grp';
      grp.textContent = 'Storage · ' + d.inv.count + ' / ' + d.inv.cap;
      wrap.appendChild(grp);
      const grid = document.createElement('div'); grid.className = 'stor-grid';
      for (const it of d.inv.items) {
        const [slot, id, stack, name] = it;
        grid.appendChild(bankCell(id, stack, name, '\nFamiliar storage (container 530)'));
      }
      wrap.appendChild(grid);
    }
  }
