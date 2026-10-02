(function () {
    'use strict';
    var P = window.rtx.plugin, G = P.groups;
    var S = { acts: [], actMap: {}, tags: [], listings: [], counts: {}, mine: null, sel: null, selData: null, filter: null, q: '',
              view: 'lobby', linked: null, world: 0, me: null, cfg: { notify: true, sound: true, hideUnmet: false }, lastNames: '', lastJoinedAt: 0,
              post: { activity: '', mode: '', size: 0, minKills: 0, tags: [] }, inGroup: null, stream: true };
    var $ = function (id) { return document.getElementById(id); };
    var esc = GF.esc;
    var KINDS = [['boss', 'Bosses'], ['raid', 'Raids'], ['dungeon', 'Dungeons']];

    function toast(msg, kind, loud) {
        var box = $('toast'); var d = document.createElement('div'); d.className = kind || ''; d.textContent = msg; box.appendChild(d);
        setTimeout(function () { d.remove(); }, 4500);
        if (loud && S.cfg.notify) P.overlay.notify(msg, 6000).catch(function () {});
        if (loud && S.cfg.sound) P.sound.play('alert3').catch(function () {});
    }
    async function api(path, body) {
        var r = await G.call(body === undefined ? 'GET' : 'POST', '/api/groups' + path, body === undefined ? null : (body || {}));
        if (!r || !r.ok || !r.body || !r.body.success) {
            var msg = (r && r.body && r.body.error) || (r && r.text) || ('Request failed' + (r && r.status ? ' (' + r.status + ')' : ''));
            var e = new Error(msg); e.code = r && r.body && r.body.code; e.status = r ? r.status : 0; throw e;
        }
        return r.body;
    }
    function isLinked() { return !!(S.linked && S.linked.state === 'linked'); }
    async function uploadIcons(ids) {
        for (var i = 0; i < (Array.isArray(ids) ? ids.length : 0) && i < 20; i++) {
            var id = ids[i], url = '';
            try { url = await P.cache.itemIcon(id); } catch (e) { url = ''; }
            if (!url || url.indexOf('data:image/png;base64,') !== 0 || url.length > 48000) continue;
            try { await api('/icons', { id: id, png: url }); } catch (e) { break; }
        }
    }
    function killKey(l) { var a = S.actMap[l.activity]; var m = a && a.modes.filter(function (x) { return x.key === l.mode; })[0]; return m ? m.kills : ''; }
    function myKills(key) { return (S.me && S.me.kills && S.me.kills[key]) | 0; }

    // ---- data
    async function loadActivities() {
        var cached = null;
        try { cached = await P.storage.get('activities'); } catch (e) {}
        if (cached && cached.at && Date.now() - cached.at < 6 * 3600000 && cached.activities) { S.acts = cached.activities; S.tags = cached.tags; }
        else {
            var j = await api('/activities');
            S.acts = j.activities; S.tags = j.tags;
            P.storage.set('activities', { at: Date.now(), activities: S.acts, tags: S.tags }).catch(function () {});
        }
        S.actMap = {}; S.acts.forEach(function (a) { S.actMap[a.key] = a; });
    }
    async function loadListings() { var j = await api('/listings'); S.listings = j.listings; S.counts = j.counts; }
    async function loadMine() { if (!isLinked()) { S.mine = null; return; } try { S.mine = await api('/mine'); } catch (e) { S.mine = null; } }
    async function loadDetail(id) {
        if (!id) { S.selData = null; return; }
        try { S.selData = (await api('/listings/' + id)).listing; }
        catch (e) { S.selData = null; S.sel = null; toast(e.message, 'err'); }
    }
    async function loadMe() { try { S.me = await G.snapshot(); } catch (e) { S.me = null; } if (S.me) S.world = S.me.world | 0; }
    function upsert(l) { var i = S.listings.findIndex(function (x) { return x.id === l.id; }); if (i < 0) S.listings.unshift(l); else S.listings[i] = l; recount(); }
    function remove(id) { S.listings = S.listings.filter(function (x) { return x.id !== id; }); recount(); }
    function recount() { S.counts = {}; S.listings.forEach(function (l) { S.counts[l.activity] = (S.counts[l.activity] || 0) + 1; }); }
    async function refreshDetail() { await loadDetail(S.sel); render(); }
    async function refreshMine() { await loadMine(); render(); }

    async function pollEvents() {
        var evs = [];
        try { evs = await G.events(); } catch (e) { return; }
        if (!Array.isArray(evs) || !evs.length) return;
        var mineDirty = false, detailDirty = false, lobbyDirty = false, helloSeen = false;
        evs.forEach(function (ev) {
            var d = ev.data || {};
            if (ev.event === 'listing') { upsert(d); lobbyDirty = true; if (S.sel === d.id) detailDirty = true; }
            else if (ev.event === 'removed') { remove(d.id); lobbyDirty = true; if (S.sel === d.id) detailDirty = true; }
            else if (ev.event === 'stream') { if (!d.connected && S.stream) S.streamLostAt = Date.now(); S.stream = !!d.connected; S.streamDrops = (S.streamDrops | 0) + 1; S.streamAt = Date.now(); lobbyDirty = true; }
            else if (ev.event === 'hello') { S.stream = true; S.streamHellos = (S.streamHellos | 0) + 1; S.streamAt = Date.now(); mineDirty = true; lobbyDirty = true; helloSeen = true; }
            else {
                note(ev.event, d); mineDirty = true;
                if (S.sel && d.listingId === S.sel) detailDirty = true;
            }
        });
        if (mineDirty) await loadMine();
        if (helloSeen) { try { await loadListings(); } catch (e) {} }
        if (detailDirty) await loadDetail(S.sel);
        if (mineDirty || lobbyDirty || detailDirty) render();
    }
    function note(k, d) {
        if (k === 'applicant') toast((d.rsn || 'Someone') + ' applied to your group', 'ok', true);
        else if (k === 'decision') toast(d.status === 'accepted' ? 'You were accepted' : d.status === 'declined' ? 'The host declined' : d.status === 'removed' ? 'You were removed from the roster' : 'Application ' + d.status, d.status === 'accepted' ? 'ok' : '', true);
        else if (k === 'form') toast('The group is forming. Ready up.', 'ok', true);
        else if (k === 'reopen') toast('The host reopened the listing', '', true);
        else if (k === 'ready' && d.all) toast('Everyone is ready', 'ok', true);
        else if (k === 'ready' && d.userId) toast((d.ready ? 'A member is ready' : 'A member is no longer ready'), '', false);
        else if (k === 'joined' && d.joined) toast('A member joined the in-game group', 'ok', false);
        else if (k === 'closed') toast(d.status === 'in_progress' ? 'Everyone joined the in-game group' : 'The listing ' + (d.status === 'expired' ? 'expired' : 'closed'), '', true);
    }

    // ---- background duties: heartbeat, snapshot refresh, the joined tracker
    async function heartbeat() {
        var h = S.mine && S.mine.hosting;
        if (!h || (h.status !== 'open' && h.status !== 'forming')) return;
        try { await api('/listings/' + h.id + '/heartbeat', {}); } catch (e) {}
    }
    async function refreshSnapshots() {
        if (!isLinked() || !S.mine) return;
        await loadMe();
        if (cardProblem()) return;
        var ids = [];
        if (S.mine.hosting && S.mine.hosting.status !== 'in_progress') ids.push(S.mine.hosting.id);
        (S.mine.applications || []).forEach(function (l) { if (l.status !== 'in_progress') ids.push(l.id); });
        for (var i = 0; i < ids.length; i++) { try { var j = await api('/listings/' + ids[i] + '/snapshot', { snapshot: S.me }); uploadIcons(j.missingIcons); } catch (e) {} }
    }
    async function trackGroup() {
        var h = S.mine && S.mine.hosting;
        var member = (S.mine && (S.mine.applications || []).filter(function (l) { return l.status === 'forming' && l.viewer.status === 'accepted'; })[0]) || null;
        if (!h && !member) { S.inGroup = null; return; }
        var g = null;
        try { g = await P.state.playerGroup(); } catch (e) { g = null; }
        S.inGroup = g && g.in ? g : null;
        if (!h || h.status !== 'forming') return;
        var names = S.inGroup ? S.inGroup.members.map(function (m) { return m.name; }) : [];
        var sig = names.join('|');
        var now = Date.now();
        if (sig === S.lastNames && now - S.lastJoinedAt < 15000) return;
        S.lastNames = sig; S.lastJoinedAt = now;
        try {
            var r = await api('/listings/' + h.id + '/joined', { names: names });
            if (r.status === 'in_progress') { await loadMine(); await loadDetail(S.sel); render(); }
        } catch (e) {}
    }

    // ---- render
    function render() { renderTop(); renderTiles(); if (S.view === 'post') renderPost(); else if (S.view === 'detail') renderDetail(); else renderLobby(); }
    function renderTop() {
        var pill = $('link');
        if (isLinked()) { pill.className = 'pill on'; pill.innerHTML = '<i></i>' + esc(S.linked.displayName || S.linked.username || 'Linked'); pill.title = 'Linked RuneTools account'; }
        else { pill.className = 'pill off'; pill.innerHTML = '<i></i>Not linked'; pill.title = 'Link this PC in Settings to post and apply'; }
        var hosting = S.mine && S.mine.hosting;
        var b = $('postBtn');
        b.textContent = hosting ? 'My listing' : 'Post a group';
        b.disabled = !isLinked();
        b.title = isLinked() ? '' : 'Link this PC to your RuneTools account in Settings';
        $('lobbyBtn').hidden = S.view === 'lobby';
        $('streamWarn').hidden = S.stream || Date.now() - (S.streamLostAt || 0) < 8000;
        $('streamWarn').title = 'Live lobby stream dropped ' + (S.streamDrops | 0) + ' times, connected ' + (S.streamHellos | 0) + ' times' + (S.streamAt ? ', last change ' + GF.ago(S.streamAt) : '');
        $('link').title = (isLinked() ? 'Linked RuneTools account' : 'Link this PC in Settings to post and apply') + '. Stream: ' + (S.stream ? 'connected' : 'down') + ', ' + (S.streamHellos | 0) + ' connects, ' + (S.streamDrops | 0) + ' drops';
    }
    function renderTiles() {
        var host = $('tiles'); host.innerHTML = '';
        var q = S.q.toLowerCase();
        KINDS.forEach(function (k) {
            var acts = S.acts.filter(function (a) { return a.kind === k[0] && (!q || a.name.toLowerCase().indexOf(q) >= 0); });
            if (!acts.length) return;
            host.appendChild(GF.el('div', 'gf-kind', esc(k[1])));
            acts.forEach(function (a) {
                var t = GF.activityTile(a, { count: S.counts[a.key] || 0, selected: S.filter === a.key, myKills: S.me ? myKills(a.modes[0].kills) : null });
                t.addEventListener('click', function () { S.filter = S.filter === a.key ? null : a.key; S.view = 'lobby'; $('main').classList.remove('showtiles'); render(); });
                host.appendChild(t);
            });
        });
    }
    function myStatusFor(l) {
        if (!S.mine) return '';
        if (S.mine.hosting && S.mine.hosting.id === l.id) return 'Hosting';
        var a = (S.mine.applications || []).filter(function (x) { return x.id === l.id; })[0];
        return a ? (a.viewer.status === 'accepted' ? 'Accepted' : 'Applied') : '';
    }
    function qualifies(l) { return !l.minKills || myKills(killKey(l)) >= l.minKills; }
    function renderLobby() {
        var r = $('right'); r.innerHTML = '';
        if (!isLinked()) r.appendChild(GF.el('div', 'status', '<b>Not linked.</b> Browse freely. Link this PC to your RuneTools account in Settings to post a group or apply.'));
        if (S.mine && (S.mine.hosting || (S.mine.applications || []).length)) {
            var box = GF.el('div', 'mine', '<h3>My groups</h3>');
            var rows = [];
            if (S.mine.hosting) rows.push([S.mine.hosting, 'Hosting']);
            (S.mine.applications || []).forEach(function (l) { rows.push([l, l.viewer.status === 'accepted' ? 'Accepted' : 'Applied']); });
            rows.forEach(function (row) {
                var c = GF.listingCard(row[0], S.actMap[row[0].activity], { mine: row[1], viewerWorld: S.world });
                c.addEventListener('click', function () { select(row[0].id); });
                box.appendChild(c);
            });
            r.appendChild(box);
        }
        var list = S.listings.filter(function (l) { return (!S.filter || l.activity === S.filter) && (!S.cfg.hideUnmet || qualifies(l) || myStatusFor(l)); });
        var bar = GF.el('div', 'bar', '<h2>' + esc(S.filter && S.actMap[S.filter] ? S.actMap[S.filter].name : 'All groups') + '</h2><span class="muted">' + (list.length ? list.length + (list.length === 1 ? ' group' : ' groups') : '') + '</span><span class="spacer"></span>');
        if (S.filter) { var clr = GF.el('button', 'btn btn-xs', 'Show all'); clr.addEventListener('click', function () { S.filter = null; render(); }); bar.appendChild(clr); }
        r.appendChild(bar);
        var lst = GF.el('div', 'list');
        if (!list.length) lst.appendChild(GF.el('div', 'empty', S.filter ? 'No groups for this activity right now.' : 'No groups right now. Post the first one.'));
        list.forEach(function (l) {
            var c = GF.listingCard(l, S.actMap[l.activity], { selected: S.sel === l.id, mine: myStatusFor(l), viewerWorld: S.world });
            c.addEventListener('click', function () { select(l.id); });
            lst.appendChild(c);
        });
        r.appendChild(lst);
    }
    async function select(id) { S.sel = id; S.view = 'detail'; await loadDetail(id); render(); }

    function btn(label, cls, fn) {
        var b = document.createElement('button'); b.type = 'button'; b.className = 'btn btn-xs ' + (cls || ''); b.textContent = label;
        b.addEventListener('click', async function () {
            b.disabled = true;
            try { await fn(); } catch (e) { toast(e.message, 'err'); }
            b.disabled = false;
        });
        return b;
    }
    function act(path, body) { return async function () { await api('/listings/' + S.sel + path, body || {}); await loadMine(); await loadDetail(S.sel); render(); }; }

    function renderDetail() {
        var r = $('right'); r.innerHTML = '';
        var l = S.selData;
        if (!l) { r.appendChild(GF.el('div', 'empty', 'This group is gone.')); return; }
        var a = S.actMap[l.activity] || { name: l.activity, modes: [] };
        var killLabel = a.name + ' kills';
        var d = GF.el('div', 'detail');
        var head = GF.el('div', 'detail-head');
        head.innerHTML = '<div class="gf-art" style="--hue:' + (a.hue || 200) + '"><span>' + esc(GF.initials(a.name)) + '</span></div><div><h2>' + esc(a.name) + '</h2><span class="gf-mode">' + esc(GF.modeLabel(a, l.mode)) + '</span></div>' +
            '<div class="gf-listing-ring" style="margin-left:auto">' + GF.rosterRing(GF.seatsOf(l), l.size) + '<small>' + l.slots + ' open</small></div>';
        d.appendChild(head);
        var req = GF.el('div', 'req-row');
        req.innerHTML = '<span class="gf-tag gf-tag-accent">Group of ' + l.size + '</span>' + (l.minKills ? '<span class="gf-tag">min ' + GF.fmt(l.minKills) + ' kills</span>' : '<span class="gf-tag">no minimum</span>') + GF.tagChips(l.tags) +
            '<span class="gf-taken" style="margin-left:auto">posted ' + GF.ago(l.createdAt) + '</span>';
        d.appendChild(req);
        d.appendChild(statusBox(l));

        var v = l.viewer;
        var acts = GF.el('div', 'actions');
        if (v.isHost) {
            if (l.status === 'open') { if (l.accepted > 0) acts.appendChild(btn('Form group', 'btn-primary', act('/form'))); acts.appendChild(btn('Close listing', 'btn-danger', act('/close'))); }
            else if (l.status === 'forming') { acts.appendChild(btn('Reopen', '', act('/reopen'))); acts.appendChild(btn('Close listing', 'btn-danger', act('/close'))); }
            else if (l.status === 'in_progress') acts.appendChild(btn('Close listing', 'btn-danger', act('/close')));
            acts.appendChild(btn('Refresh my card', '', async function () { await loadMe(); var why = cardProblem(); if (why) throw new Error(why); var j = await api('/listings/' + l.id + '/snapshot', { snapshot: S.me }); uploadIcons(j.missingIcons); await refreshDetail(); }));
        } else if (isLinked()) {
            if (v.status === 'applied') acts.appendChild(btn('Withdraw', 'btn-danger', act('/withdraw')));
            else if (v.status === 'accepted') {
                if (l.status === 'forming') acts.appendChild(btn(v.ready ? 'Not ready' : 'Ready', v.ready ? '' : 'btn-primary', act('/ready', { ready: !v.ready })));
                if (l.status !== 'in_progress') acts.appendChild(btn('Leave', 'btn-danger', act('/withdraw')));
                acts.appendChild(btn('Refresh my card', '', async function () { await loadMe(); var why = cardProblem(); if (why) throw new Error(why); var j = await api('/listings/' + l.id + '/snapshot', { snapshot: S.me }); uploadIcons(j.missingIcons); await refreshDetail(); }));
            } else if (l.status === 'open' && !v.status) {
                var can = qualifies(l), hosting = S.mine && S.mine.hosting;
                var ap = btn(hosting ? 'Close your listing to apply' : (can ? 'Apply' : 'Below the minimum kills'), 'btn-primary', async function () {
                    await loadMe();
                    var why = cardProblem();
                    if (why) throw new Error(why);
                    var j = await api('/listings/' + l.id + '/apply', { snapshot: S.me });
                    toast('Applied. The host sees your card now.', 'ok');
                    uploadIcons(j.missingIcons);
                    await loadMine(); await loadDetail(S.sel); render();
                });
                ap.disabled = !can || !!hosting;
                acts.appendChild(ap);
            }
            acts.appendChild(btn('Block host', '', async function () { await api('/block', { userId: l.host.userId }); toast('Host blocked'); remove(l.id); S.sel = null; S.selData = null; S.view = 'lobby'; render(); }));
            acts.appendChild(btn('Report', '', async function () { await api('/report', { listingId: l.id }); toast('Reported. Thank you.'); }));
        }
        d.appendChild(acts);

        d.appendChild(GF.el('div', 'sec', 'Host'));
        d.appendChild(GF.playerCard(l.host, { killLabel: killLabel, minKills: l.minKills || null, viewerWorld: S.world }));

        var pending = (l.applicants || []).filter(function (x) { return x.status === 'applied'; });
        var roster = (l.applicants || []).filter(function (x) { return x.status === 'accepted'; });
        if (v.isHost) {
            d.appendChild(GF.el('div', 'sec', 'Applicants <small>' + pending.length + ' waiting</small>'));
            if (!pending.length) d.appendChild(GF.el('div', 'empty', l.status === 'open' ? 'Nobody waiting. Applicants appear here live.' : 'The listing is not taking applications.'));
            pending.forEach(function (p) {
                var ra = GF.el('div', 'rowacts');
                if (l.status === 'open' && l.slots > 0) ra.appendChild(btn('Accept', 'btn-primary', act('/decide', { userId: p.userId, action: 'accept' })));
                ra.appendChild(btn('Decline', 'btn-danger', act('/decide', { userId: p.userId, action: 'decline' })));
                ra.appendChild(btn('Block', '', async function () { await api('/block', { userId: p.userId }); await act('/decide', { userId: p.userId, action: 'decline' })(); }));
                d.appendChild(GF.playerCard(p, { killLabel: killLabel, minKills: l.minKills || null, viewerWorld: S.world, actions: ra }));
            });
        }
        d.appendChild(GF.el('div', 'sec', 'Roster <small>' + (roster.length + 1) + ' of ' + l.size + '</small>'));
        if (!roster.length) d.appendChild(GF.el('div', 'empty', 'No one accepted yet.'));
        roster.forEach(function (p) {
            var badge = p.joined ? { kind: 'ok', text: 'In group' } : (l.status === 'forming' ? (p.ready ? { kind: 'ok', text: 'Ready' } : { kind: 'warn', text: 'Not ready' }) : { kind: 'accent', text: 'Accepted' });
            var ra = null;
            if (v.isHost && l.status !== 'in_progress') { ra = GF.el('div', 'rowacts'); ra.appendChild(btn('Remove', 'btn-danger', act('/decide', { userId: p.userId, action: 'remove' }))); }
            d.appendChild(GF.playerCard(p, { killLabel: killLabel, minKills: l.minKills || null, viewerWorld: S.world, badge: badge, actions: ra, compact: !p.equipment }));
        });
        r.appendChild(d);
    }
    function statusBox(l) {
        var v = l.viewer, cls = '', text = '';
        if (l.status === 'open') { text = v.isHost ? 'Open. Accept applicants, then press Form group when the roster is right.' : (v.status === 'applied' ? 'Applied. The host is reviewing your card.' : v.status === 'accepted' ? 'Accepted. Waiting for the host to form the group.' : v.status === 'declined' ? 'The host declined your application.' : 'Open for applications.'); cls = v.status === 'declined' ? 'err' : (v.status === 'accepted' ? 'ok' : ''); }
        else if (l.status === 'forming') {
            var ready = (l.applicants || []).filter(function (x) { return x.status === 'accepted' && x.ready; }).length;
            text = v.isHost ? 'Forming. ' + ready + ' of ' + l.accepted + ' ready. Create the group in the Grouping System and invite each name below; they tick as they join.' :
                   (v.status === 'accepted' ? 'Forming. Press Ready, then accept ' + esc(l.host.rsn) + '\'s invite in the Grouping System' + (l.host.world ? ' on world ' + l.host.world : '') + '.' : 'Forming. The roster is set.');
            cls = 'warn';
        }
        else if (l.status === 'in_progress') { text = 'Everyone is in the in-game group. Good luck.'; cls = 'ok'; }
        else { text = l.status === 'expired' ? 'This listing expired.' : 'This listing is closed.'; cls = 'err'; }
        var b = GF.el('div', 'status ' + cls, text);
        if (l.status === 'forming' && v.isHost) {
            var inNames = {};
            if (S.inGroup) S.inGroup.members.forEach(function (m) { inNames[String(m.name).toLowerCase().replace(/[\s_ ]+/g, ' ').trim()] = 1; });
            var row = GF.el('div', 'names');
            (l.applicants || []).filter(function (x) { return x.status === 'accepted'; }).forEach(function (x) {
                var key = String(x.rsn).toLowerCase().replace(/[\s_ ]+/g, ' ').trim();
                var c = GF.el('span', 'name' + (x.joined || inNames[key] ? ' in' : ''), esc(x.rsn));
                c.appendChild(btn('Copy', '', async function () { await P.clipboard.copy(x.rsn); toast('Copied ' + x.rsn); }));
                row.appendChild(c);
            });
            b.appendChild(row);
            if (!S.inGroup) b.appendChild(GF.el('div', 'hint', 'No in-game group found on this client yet.'));
        }
        if (l.status === 'forming' && v.status === 'accepted') {
            var hostIn = S.inGroup && S.inGroup.members.some(function (m) { return String(m.name).toLowerCase().replace(/[\s_ ]+/g, ' ').trim() === String(l.host.rsn).toLowerCase().replace(/[\s_ ]+/g, ' ').trim(); });
            b.appendChild(GF.el('div', 'hint', hostIn ? 'You are in the host\'s in-game group.' : (S.inGroup ? 'You are in a group, but not the host\'s.' : 'Not in an in-game group yet.')));
        }
        return b;
    }

    // ---- post form
    // A long list opens as a searchable sheet over the pane, so there is one scrollbar on screen, never two.
    function picker(options, value, onChange, title) {
        var root = GF.el('div', 'picker');
        var scrim = GF.el('div', 'picker-scrim');
        var panel = GF.el('div', 'picker-panel');
        var head = GF.el('div', 'picker-head', '<b>' + esc(title || 'Choose') + '</b>');
        var q = document.createElement('input'); q.type = 'search'; q.placeholder = 'Type to filter'; q.className = 'picker-q';
        var list = GF.el('div', 'picker-list');
        function close() { root.remove(); document.removeEventListener('keydown', onKey); }
        function fill() {
            list.innerHTML = '';
            var t = q.value.trim().toLowerCase(), lastGroup = null, n = 0;
            options.forEach(function (o) {
                if (t && o.label.toLowerCase().indexOf(t) < 0) return;
                if (o.group && o.group !== lastGroup) { list.appendChild(GF.el('div', 'dd-group', esc(o.group))); lastGroup = o.group; }
                var row = GF.el('div', 'picker-row' + (o.v === value ? ' on' : ''), esc(o.label));
                row.addEventListener('click', function () { close(); if (o.v !== value) onChange(o.v); });
                list.appendChild(row); n++;
            });
            if (!n) list.appendChild(GF.el('div', 'empty', 'Nothing matches'));
        }
        function onKey(e) {
            if (e.key === 'Escape') { close(); return; }
            if (e.key === 'Enter') { var first = list.querySelector('.picker-row'); if (first) first.click(); }
        }
        q.addEventListener('input', fill);
        scrim.addEventListener('click', close);
        document.addEventListener('keydown', onKey);
        head.appendChild(q);
        panel.appendChild(head); panel.appendChild(list);
        root.appendChild(scrim); root.appendChild(panel);
        fill();
        document.body.appendChild(root);
        setTimeout(function () { q.focus(); }, 0);
        var on = list.querySelector('.picker-row.on'); if (on && on.scrollIntoView) on.scrollIntoView({ block: 'center' });
    }
    function dropdown(options, value, onChange, title) {
        var dd = GF.el('div', 'dd');
        var cur = options.filter(function (o) { return o.v === value; })[0];
        var btn = GF.el('button', 'dd-btn', esc(cur ? cur.label : 'Choose')); btn.type = 'button';
        if (options.length > 8) {
            btn.addEventListener('click', function (e) { e.stopPropagation(); picker(options, value, onChange, title); });
            dd.appendChild(btn);
            return dd;
        }
        var pop = GF.el('div', 'dd-pop');
        var lastGroup = null;
        options.forEach(function (o) {
            if (o.group && o.group !== lastGroup) { pop.appendChild(GF.el('div', 'dd-group', esc(o.group))); lastGroup = o.group; }
            var el = GF.el('div', 'dd-opt' + (o.v === value ? ' on' : ''), esc(o.label));
            el.addEventListener('click', function (e) { e.stopPropagation(); dd.classList.remove('open'); if (o.v !== value) onChange(o.v); });
            pop.appendChild(el);
        });
        btn.addEventListener('click', function (e) {
            e.stopPropagation();
            var open = dd.classList.contains('open');
            document.querySelectorAll('.dd.open').forEach(function (x) { x.classList.remove('open'); });
            if (!open) { dd.classList.add('open'); var on = pop.querySelector('.dd-opt.on'); if (on && on.scrollIntoView) on.scrollIntoView({ block: 'nearest' }); }
        });
        dd.appendChild(btn); dd.appendChild(pop);
        return dd;
    }
    document.addEventListener('click', function () { document.querySelectorAll('.dd.open').forEach(function (x) { x.classList.remove('open'); }); });

    function cardProblem() {
        if (!S.me) return 'Could not read your character. Log in to the game first.';
        if (S.me.in === false) return 'You are not in the game world yet. Log in, then refresh the card.';
        if (!(S.me.equipment || []).length) return 'No worn items were read. Open your Worn Equipment once, then refresh the card.';
        return '';
    }

    function renderPost() {
        var r = $('right'); r.innerHTML = '';
        var hosting = S.mine && S.mine.hosting;
        if (hosting) { S.sel = hosting.id; S.view = 'detail'; loadDetail(S.sel).then(render); return; }
        if (!isLinked()) { r.appendChild(GF.el('div', 'status err', 'Link this PC to your RuneTools account in Settings to post a group.')); return; }
        var p = S.post;
        if (!p.activity || !S.actMap[p.activity]) { p.activity = S.filter || S.acts[0].key; p.mode = ''; p.size = 0; }
        var a = S.actMap[p.activity];
        if (!p.mode || !a.modes.some(function (m) { return m.key === p.mode; })) p.mode = a.modes[0].key;
        if (!p.size || p.size > a.maxSize) p.size = Math.min(a.maxSize, Math.max(2, p.size || Math.min(a.maxSize, 3)));
        var mode = a.modes.filter(function (m) { return m.key === p.mode; })[0];
        var mine = myKills(mode.kills);

        var d = GF.el('div', 'detail');
        d.appendChild(GF.el('div', 'bar', '<h2>Post a group</h2><span class="muted">Everyone sees the card below exactly as it is.</span>'));
        var f = GF.el('div', 'form');
        var fa = GF.el('div', 'field full', '<label>Activity</label>');
        var actOpts = [];
        KINDS.forEach(function (k) { S.acts.filter(function (x) { return x.kind === k[0]; }).forEach(function (x) { actOpts.push({ v: x.key, label: x.name, group: k[1] }); }); });
        fa.appendChild(dropdown(actOpts, p.activity, function (v) { p.activity = v; p.mode = ''; p.size = 0; renderPost(); }, 'Activity'));
        f.appendChild(fa);
        var fm = GF.el('div', 'field', '<label>Mode</label>');
        fm.appendChild(dropdown(a.modes.map(function (m) { return { v: m.key, label: m.label }; }), p.mode, function (v) { p.mode = v; renderPost(); }));
        f.appendChild(fm);
        var fs = GF.el('div', 'field', '<label>Group size</label>');
        var sz = GF.el('div', 'sizes');
        for (var n = 2; n <= a.maxSize; n++) (function (n) { var b = document.createElement('button'); b.type = 'button'; b.textContent = n; b.className = n === p.size ? 'on' : ''; b.addEventListener('click', function () { p.size = n; renderPost(); }); sz.appendChild(b); })(n);
        fs.appendChild(sz); f.appendChild(fs);
        var fk = GF.el('div', 'field full', '<label>Minimum kills</label>');
        var kr = GF.el('div', 'kills-row');
        var ki = document.createElement('input'); ki.type = 'number'; ki.min = 0; ki.max = 100000000; ki.value = p.minKills || 0;
        ki.addEventListener('input', function () { p.minKills = Math.max(0, parseInt(ki.value, 10) || 0); });
        kr.appendChild(ki); kr.appendChild(GF.el('span', 'hint', 'You have ' + GF.fmt(mine) + ' for this mode. Applicants below the minimum cannot apply.'));
        fk.appendChild(kr); f.appendChild(fk);
        var ft = GF.el('div', 'field full', '<label>Tags <span class="muted">(up to 3)</span></label>');
        var tp = GF.el('div', 'tagpick');
        S.tags.forEach(function (t) {
            var c = GF.el('span', 'gf-tag' + (p.tags.indexOf(t) >= 0 ? ' on' : ''), esc(t));
            c.addEventListener('click', function () { var i = p.tags.indexOf(t); if (i >= 0) p.tags.splice(i, 1); else if (p.tags.length < 3) p.tags.push(t); renderPost(); });
            tp.appendChild(c);
        });
        ft.appendChild(tp); f.appendChild(ft);
        d.appendChild(f);
        d.appendChild(GF.el('div', 'sec', 'Your card'));
        var problem = cardProblem();
        if (problem) d.appendChild(GF.el('div', 'status warn', esc(problem)));
        if (S.me) {
            var snap = Object.assign({}, S.me, { takenAt: Date.now(), kills: mine, totalKills: Object.keys(S.me.kills || {}).reduce(function (t, k) { return t + (S.me.kills[k] | 0); }, 0) });
            d.appendChild(GF.playerCard(snap, { killLabel: a.name + ' kills', viewerWorld: S.world }));
        }
        var row = GF.el('div', 'actions');
        row.appendChild(btn('Refresh card', '', async function () { await loadMe(); renderPost(); }));
        var post = btn('Post listing', 'btn-primary', async function () {
            await loadMe();
            var why = cardProblem();
            if (why) throw new Error(why);
            var j = await api('/listings', { activity: p.activity, mode: p.mode, size: p.size, minKills: p.minKills, tags: p.tags, snapshot: S.me });
            toast('Listing posted', 'ok');
            uploadIcons(j.missingIcons);
            await loadMine(); S.sel = j.listing.id; S.view = 'detail'; await loadDetail(S.sel); render();
        });
        post.disabled = !!problem;
        row.appendChild(post);
        var back = btn('Back', '', async function () { S.view = 'lobby'; render(); });
        row.appendChild(back);
        d.appendChild(row);
        r.appendChild(d);
    }

    // ---- boot
    function layout() { var m = $('main'); if (window.innerWidth < 560) m.classList.add('narrow'); else { m.classList.remove('narrow'); m.classList.remove('showtiles'); } }
    async function boot() {
        await P.ready();
        GF.setIconSource(function (id) { return P.cache.itemIcon(id).then(function (u) { return u ? [u] : []; }, function () { return []; }); });
        GF.setSkillIcon(function (name) { return (window.GF_SKILL_ICONS || {})[name] || ''; });
        try {
            await P.ui.settings([
                { key: 'notify', type: 'toggle', label: 'Toasts over the game for group events', default: true },
                { key: 'sound', type: 'toggle', label: 'Sound on applicant, accept and ready', default: true },
                { key: 'hideUnmet', type: 'toggle', label: 'Hide listings I do not qualify for', default: false },
            ]);
            var cfg = await P.settings.get(); if (cfg) Object.assign(S.cfg, cfg);
            P.settings.on(function (c) { Object.assign(S.cfg, c || {}); render(); });
        } catch (e) {}
        $('q').addEventListener('input', function () { S.q = this.value.trim(); renderTiles(); });
        $('postBtn').addEventListener('click', function () { S.view = 'post'; $('main').classList.remove('showtiles'); render(); });
        $('lobbyBtn').addEventListener('click', function () { S.view = 'lobby'; render(); });
        $('tilesBtn').addEventListener('click', function () { $('main').classList.toggle('showtiles'); });
        window.addEventListener('resize', layout); layout();
        try { S.linked = await G.linked(); } catch (e) { S.linked = null; }
        try { await loadActivities(); } catch (e) { $('right').innerHTML = '<div class="empty">Could not reach runetools.io. Retrying.</div>'; setTimeout(boot2, 5000); return; }
        await boot2();
    }
    async function boot2() {
        if (!S.acts.length) { try { await loadActivities(); } catch (e) { setTimeout(boot2, 5000); return; } }
        await loadMe();
        try { await loadListings(); } catch (e) { toast('Could not load groups', 'err'); }
        await loadMine();
        if (S.mine && S.mine.hosting) { S.sel = S.mine.hosting.id; S.view = 'detail'; await loadDetail(S.sel); }
        render();
        try { await G.subscribe(true); } catch (e) {}
        setInterval(pollEvents, 1000);
        setInterval(heartbeat, 30000);
        setInterval(trackGroup, 2000);
        setInterval(refreshSnapshots, 10 * 60000);
        setInterval(async function () { try { var ls = await G.linked(); var was = isLinked(); S.linked = ls; if (isLinked() !== was) { await loadMine(); render(); } } catch (e) {} }, 15000);
        setInterval(function () { if (S.view === 'lobby') render(); else renderTop(); }, 10000);
    }
    boot();
})();
