// rtx-plugin-market.js: the in-client marketplace: browse the signed catalogue served by runetools.io, install / uninstall, and re-prompt consent on every (re)install
// Loads after: rtx-plugins.js (pluginTabs, grants, loadPlugins) and rtx-wm.js.
// Plain script, page globals by design: every top-level name here is a page global that panels and the other core files use.
  // ---- in-client marketplace (browse + install signed plugins) ----
  let pluginBrowseList = [];
  async function renderPluginBrowse() {
    const c = $('content');
    if (c.firstElementChild && c.firstElementChild.dataset && c.firstElementChild.dataset.pluginBrowse === '1') return; // build-once
    c.innerHTML = '';
    const wrap = document.createElement('div'); wrap.dataset.pluginBrowse = '1'; wrap.style.cssText = 'padding:4px;';
    wrap.innerHTML =
      '<div style="position:relative;margin-bottom:12px;">' +
        '<svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="#5a5a6e" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="position:absolute;left:10px;top:50%;transform:translateY(-50%);"><circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/></svg>' +
        '<input id="pbSearch" type="text" placeholder="Search plugins..." autocomplete="off" style="width:100%;background:#14151c;border:1px solid rgba(255,255,255,0.1);border-radius:9px;padding:8px 12px 8px 32px;color:#f0f0f5;font-size:0.9em;outline:none;box-sizing:border-box;">' +
      '</div>' +
      '<div id="pbList"><div class="empty">Loading plugins...</div></div>';
    c.appendChild(wrap);
    const se = $('pbSearch'); if (se) se.addEventListener('input', renderBrowseList);
    // pluginMarketList() is served ASYNC (the HTTP fetch runs off the UI thread): it returns
    // "{}" until the worker has the response, {"error":...} if the last fetch failed, and the
    // catalog once it lands. Poll until one of the latter two arrives; the first fetch can
    // outlive a short window (proxy autodetect), so never give up while the panel is open.
    // An empty plugins ARRAY is a real answer (catalog empty), unlike a missing one.
    pluginBrowseList = [];
    for (let attempt = 0; ; attempt++) {
      let got = null, err = '', diag = '';
      try {
        if (!bridge() || typeof bridge().pluginMarketList !== 'function') {
          diag = 'bridge missing pluginMarketList (launcher build predates the marketplace?)';
        } else {
          const raw = JSON.parse(String((await bridge().pluginMarketList()) || '{}'));
          const d = (raw && raw.data) ? raw.data : raw;
          if (d && Array.isArray(d.plugins)) got = d.plugins;
          else if (d && d.error) err = String(d.error);
        }
      } catch (e) { diag = 'exception: ' + String((e && e.message) || e); }
      const host = $('pbList');
      if (!host) return;                        // user navigated away
      if (got) { pluginBrowseList = got; break; }
      if (err && attempt >= 4)
        host.innerHTML = '<div class="empty">Couldn\'t reach runetools.io: ' + err.replace(/[<>&"']/g, '') + '. Retrying...</div>';
      else if (diag && attempt >= 8)
        host.innerHTML = '<div class="empty">Loading plugins...<br>' + diag.replace(/[<>&"']/g, '') + '</div>';
      await new Promise(r => setTimeout(r, attempt < 20 ? 250 : 2000));
    }
    renderBrowseList();
  }

  function renderBrowseList() {
    const host = $('pbList'); if (!host) return;
    const installed = new Set(pluginTabs.filter(p => p.source === 'installed').map(p => p.id));
    const q = (($('pbSearch') && $('pbSearch').value) || '').trim().toLowerCase();
    let list = pluginBrowseList.filter(p => !q ||
      ((p.name || '') + ' ' + (p.author || '') + ' ' + (p.slug || '') + ' ' + (p.description || '') + ' ' + (p.scopes || []).join(' ')).toLowerCase().includes(q));
    list.sort((a, b) => ((b.votes || 0) - (a.votes || 0)) || String(a.name || a.slug).localeCompare(String(b.name || b.slug)));
    host.innerHTML = '';
    if (!pluginBrowseList.length) { host.innerHTML = '<div class="empty">No plugins available yet.</div>'; return; }
    if (!list.length) { host.innerHTML = '<div class="empty">No plugins match your search.</div>'; return; }
    list.forEach(p => host.appendChild(pluginBrowseCard(p, installed.has(p.slug))));
  }

  function pluginBrowseCard(p, isInstalled) {
    const el = document.createElement('div');
    el.style.cssText = 'background:#1a1b23;border:1px solid rgba(255,255,255,0.06);border-radius:10px;padding:12px 14px;margin-bottom:10px;';
    const scopes = (p.scopes || []).map(s => '<span style="background:rgba(var(--accent-rgb),0.12);color:var(--accent-hi);border-radius:999px;padding:1px 8px;font-size:0.72em;margin-right:4px;">' + pluginEsc(s) + '</span>').join('');
    el.innerHTML =
      '<div style="display:flex;flex-wrap:wrap;justify-content:space-between;align-items:center;gap:8px 10px;">' +
        '<div style="min-width:0;"><div style="font-weight:600;">' + pluginEsc(p.name) + '</div>' +
        '<div style="color:#8b8b9e;font-size:0.8em;">v' + pluginEsc(p.version) + (p.author ? (' - ' + pluginEsc(p.author)) : '') + '</div></div>' +
        '<div class="acts" style="display:flex;align-items:center;margin-left:auto;"></div></div>' +
      (p.description ? ('<div style="color:#c7c7d4;font-size:0.85em;margin:8px 0;">' + pluginEsc(p.description) + '</div>') : '') +
      (scopes ? ('<div style="margin-top:4px;">' + scopes + '</div>') : '');
    const acts = el.querySelector('.acts');

    // Read-only community score; voting is a logged-in website action, the client
    // runs account-less.
    if (typeof p.votes === 'number') {
      const sc = document.createElement('span');
      sc.title = 'Community score (vote on runetools.io)';
      sc.style.cssText = 'font-weight:700;font-size:0.82em;min-width:24px;text-align:center;margin-right:10px;color:' + (p.votes > 0 ? '#34d399' : (p.votes < 0 ? '#ef4444' : '#c7c7d4')) + ';';
      sc.textContent = (p.votes > 0 ? '+' : '') + (p.votes || 0);
      acts.appendChild(sc);
    }

    const details = document.createElement('button');
    details.textContent = 'Details';
    details.title = 'View source, author & details on runetools.io';
    details.style.cssText = 'border:1px solid rgba(255,255,255,0.1);background:rgba(255,255,255,0.06);color:#f0f0f5;border-radius:8px;padding:7px 12px;font-weight:600;cursor:pointer;font-size:0.85em;margin-right:6px;';
    details.addEventListener('click', () => { try { bridge().openExternal('https://runetools.io/plugins/' + encodeURIComponent(p.slug)); } catch (e) {} });
    acts.appendChild(details);
    const btn = document.createElement('button');
    btn.style.cssText = 'border:1px solid rgba(255,255,255,0.1);border-radius:8px;padding:7px 14px;font-weight:600;cursor:pointer;font-size:0.85em;';
    if (isInstalled) {
      btn.textContent = 'Uninstall';
      btn.style.background = 'rgba(239,68,68,0.15)'; btn.style.borderColor = 'rgba(239,68,68,0.4)'; btn.style.color = '#ef4444';
      btn.addEventListener('click', () => marketUninstall(p.slug, btn));
    } else {
      btn.textContent = 'Install';
      btn.style.background = 'var(--accent-hi)'; btn.style.borderColor = 'var(--accent-hi)'; btn.style.color = '#fff';
      btn.addEventListener('click', () => marketInstall(p.slug, btn));
    }
    acts.appendChild(btn);
    return el;
  }

  async function marketInstall(slug, btn) {
    btn.disabled = true; btn.textContent = 'Installing...';
    let err = 'error';
    try {
      // Install runs on a background thread now: kick it, then poll the status so the download
      // never blocks the UI thread. '' = success (matches the pre-async contract below).
      const start = await bridge().pluginMarketInstall(slug);
      if (start === 'busy') { btn.disabled = false; btn.textContent = 'Install'; return; }
      err = 'pending';
      for (let i = 0; i < 120 && err === 'pending'; i++) {   // up to ~30s
        await new Promise(r => setTimeout(r, 250));
        let st = 'pending'; try { st = await bridge().pluginInstallStatus(); } catch (e) {}
        if (st === 'ok') err = '';
        else if (st !== 'pending' && st !== 'idle') err = st;
      }
      if (err === 'pending') err = 'timed out';
    } catch (e) { err = String(e); }
    if (!err) {
      pluginClearGranted(slug);       // require a fresh permission consent on every (re)install
      await loadPlugins();
      // Open the freshly installed plugin's window (shows its permission prompt).
      const pt = allTabs().find(x => x.id === 'plugin:' + slug);
      if (pt) openTab(pt);
    } else {
      btn.disabled = false; btn.textContent = 'Install';
      const c = paneRoot('pluginbrowse');   // async continuation: resolve the browse window's pane
      if (c) {
        const note = document.createElement('div');
        note.style.cssText = 'color:#ef4444;font-size:0.82em;margin:8px 4px;';
        note.textContent = 'Install failed: ' + err;
        if (c.firstChild) c.insertBefore(note, c.firstChild); else c.appendChild(note);
      }
    }
  }

  async function marketUninstall(slug, btn) {
    btn.disabled = true; btn.textContent = '...';
    try { await bridge().pluginUninstallLocal(slug); } catch (e) {}
    pluginClearGranted(slug);         // forget consent so a reinstall re-prompts
    wmCloseTabId('plugin:' + slug);
    await loadPlugins();
    const bw = wmWinOf('pluginbrowse');
    if (bw && !bw.min && bw.tab === 'pluginbrowse') { bw.pane.innerHTML = ''; renderPaneFor(bw); }
  }
