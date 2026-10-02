// Group Finder cards: one renderer for the website and the client plugin. Takes the API's listing and
// snapshot shapes and an icon source (a URL or a Promise of one per item id) and builds DOM.
(function () {
    'use strict';
    var iconSource = function (id) { return ['/icons/items/' + id + '.png']; };
    var skillIcon = function (name) { return '/icons/skills/' + name + '.png'; };
    var artSource = function (key) { return '/icons/bosses/' + key + '.png'; };
    var SLOT_NAMES = { 0: 'Head', 1: 'Cape', 2: 'Neck', 3: 'Weapon', 4: 'Body', 5: 'Off-hand', 7: 'Legs', 9: 'Gloves', 10: 'Boots', 12: 'Ring', 13: 'Ammo', 17: 'Pocket' };
    var DOLL = [[null, 0, null], [1, 2, 13], [3, 4, 5], [null, 7, 17], [9, 10, 12]];
    var LEVELS = ['Attack', 'Strength', 'Defence', 'Ranged', 'Magic', 'Necromancy', 'Prayer', 'Constitution', 'Summoning', 'Herblore'];
    var SHORT = { Attack: 'Att', Strength: 'Str', Defence: 'Def', Ranged: 'Rng', Magic: 'Mag', Necromancy: 'Nec', Prayer: 'Pry', Constitution: 'HP', Summoning: 'Sum', Herblore: 'Hrb' };
    var CAP120 = { Herblore: 1, Necromancy: 1, Invention: 1, Archaeology: 1, Dungeoneering: 1, Farming: 1, Slayer: 1 };

    function esc(s) { return String(s == null ? '' : s).replace(/[&<>"']/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]; }); }
    function el(tag, cls, html) { var e = document.createElement(tag); if (cls) e.className = cls; if (html != null) e.innerHTML = html; return e; }
    function fmt(n) { n = Number(n) || 0; return n.toLocaleString('en-US'); }
    function ago(ms) {
        var d = Math.max(0, Date.now() - Number(ms || 0)) / 1000;
        if (d < 50) return 'just now';
        if (d < 3600) return Math.round(d / 60) + ' min ago';
        if (d < 86400) return Math.round(d / 3600) + ' h ago';
        return Math.round(d / 86400) + ' d ago';
    }
    function stale(ms) { return Date.now() - Number(ms || 0) > 15 * 60000; }

    function setIcon(img, id) {
        var src = iconSource(id);
        var apply = function (v) {
            var list = Array.isArray(v) ? v.slice() : [v];
            var next = function () {
                var u = list.shift();
                if (!u) { img.classList.add('gf-noicon'); return; }
                img.onerror = next; img.src = u;
            };
            next();
        };
        if (src && typeof src.then === 'function') src.then(apply, function () { img.classList.add('gf-noicon'); });
        else apply(src);
    }

    function pips(rank, ranks) {
        if (!(ranks > 1)) return '';
        var s = '<span class="gf-pips">';
        for (var i = 1; i <= ranks; i++) s += '<i' + (i <= rank ? ' class="on"' : '') + '></i>';
        return s + '</span>';
    }

    function paperdoll(snap) {
        var bySlot = {};
        (snap.equipment || []).forEach(function (e) { bySlot[e.slot] = e; });
        var wrap = el('div', 'gf-doll');
        var grid = el('div', 'gf-doll-grid');
        DOLL.forEach(function (row) {
            row.forEach(function (slot) {
                if (slot === null) { grid.appendChild(el('span', 'gf-slot gf-slot-gap')); return; }
                grid.appendChild(slotCell(slot, bySlot[slot]));
            });
        });
        wrap.appendChild(grid);
        return wrap;
    }
    function slotCell(slot, item) {
        var c = el('span', 'gf-slot' + (item ? ' filled' : ''));
        c.title = item ? (item.name || ('Item ' + item.id)) : (SLOT_NAMES[slot] || 'Slot');
        if (item) {
            var img = el('img', 'gf-icon'); img.alt = item.name || ''; img.loading = 'lazy';
            setIcon(img, item.id);
            c.appendChild(img);
            if (item.perks && item.perks.length) c.appendChild(el('i', 'gf-aug'));
        } else c.appendChild(el('span', 'gf-slot-name', esc(SLOT_NAMES[slot] || '')));
        return c;
    }

    function perkList(snap) {
        var items = (snap.equipment || []).filter(function (e) { return e.perks && e.perks.length; });
        var box = el('div', 'gf-perks');
        if (!items.length) { box.appendChild(el('div', 'gf-dim', 'No perks read')); return box; }
        items.forEach(function (e) {
            var row = el('div', 'gf-perk-row');
            var img = el('img', 'gf-icon gf-icon-sm'); img.alt = ''; setIcon(img, e.id);
            row.appendChild(img);
            var chips = el('div', 'gf-chips');
            e.perks.forEach(function (p) { chips.appendChild(el('span', 'gf-chip', esc(p.name) + pips(p.rank, p.ranks))); });
            row.appendChild(chips);
            box.appendChild(row);
        });
        return box;
    }

    function levelStrip(snap) {
        var lv = snap.levels || {};
        var box = el('div', 'gf-levels');
        box.appendChild(el('span', 'gf-combat', '<b>' + (snap.combat | 0) + '</b><small>Combat</small>'));
        LEVELS.forEach(function (k) {
            if (lv[k] == null) return;
            var v = lv[k] | 0, cls = v >= 120 ? ' l120' : (v >= 99 ? ' l99' : '');
            var pill = el('span', 'gf-lv' + cls);
            pill.title = k + ' ' + v;
            var src = skillIcon(k);
            if (src) { var img = el('img', 'gf-skill'); img.alt = SHORT[k]; img.src = src; img.onerror = function () { img.replaceWith(el('small', null, SHORT[k])); }; pill.appendChild(img); }
            else pill.appendChild(el('small', null, SHORT[k]));
            pill.appendChild(el('b', null, String(v)));
            box.appendChild(pill);
        });
        return box;
    }

    function worldBadge(world, viewerWorld) {
        var same = viewerWorld && world && viewerWorld === world;
        return '<span class="gf-world' + (same ? ' same' : '') + '" title="' + (same ? 'Your world' : 'World') + '">W' + (world || '?') + '</span>';
    }

    function killsBadge(kills, label, minKills, totalKills, reported) {
        var meets = minKills == null ? null : (kills >= minKills);
        return '<div class="gf-kills' + (meets === false ? ' short' : '') + '">' +
            '<b>' + fmt(kills) + '</b><small>' + esc(label || 'kills') + (reported ? ' <i class="gf-rep" title="Entered by the player, not read from the game">reported</i>' : '') + '</small>' +
            (meets === null ? '' : '<i class="gf-req ' + (meets ? 'ok' : 'no') + '" title="' + (meets ? 'Meets the minimum' : 'Below the minimum of ' + fmt(minKills)) + '"></i>') +
            (totalKills != null ? '<em>' + fmt(totalKills) + ' boss kills</em>' : '') + '</div>';
    }

    // host or applicant card: paperdoll, perks, kills, levels
    function playerCard(snap, o) {
        o = o || {};
        var card = el('div', 'gf-player' + (o.compact ? ' compact' : ''));
        var web = snap.source === 'web';
        var head = el('div', 'gf-player-head');
        head.innerHTML = '<span class="gf-rsn">' + esc(snap.rsn || '?') + '</span>' + worldBadge(snap.world, o.viewerWorld) +
            (web ? '<span class="gf-tag gf-src-web" title="Posted from the website: levels from the official hiscores, kills as reported, no gear">Browser card</span>'
                 : '<span class="gf-tag gf-src-client" title="Read from the game by the RuneTools client">Verified</span>') +
            (o.badge ? '<span class="gf-tag gf-tag-' + esc(o.badge.kind || 'muted') + '">' + esc(o.badge.text) + '</span>' : '') +
            '<span class="gf-taken' + (stale(snap.takenAt) ? ' old' : '') + '" title="When the client read this">taken ' + ago(snap.takenAt) + '</span>';
        card.appendChild(head);
        var body = el('div', 'gf-player-body');
        if (web) body.appendChild(el('div', 'gf-webnote', '<b>No gear shown</b>Cards posted from the RuneTools client show worn items and perks read from the game.'));
        else body.appendChild(paperdoll(snap));
        var right = el('div', 'gf-player-right');
        right.innerHTML = killsBadge(snap.kills | 0, o.killLabel, o.minKills, web ? null : snap.totalKills, web);
        right.appendChild(levelStrip(snap));
        body.appendChild(right);
        card.appendChild(body);
        if (!web) card.appendChild(perkList(snap));
        if (o.actions) card.appendChild(o.actions);
        return card;
    }

    // one arc per seat: host, accepted, ready, joined, empty
    function rosterRing(seats, size) {
        var r = 16, c = 20, circ = 2 * Math.PI * r, n = Math.max(1, size | 0), gap = n > 1 ? 3 : 0;
        var seg = circ / n, dash = Math.max(1, seg - gap);
        var s = '<svg class="gf-ring" viewBox="0 0 40 40" width="40" height="40">';
        for (var i = 0; i < n; i++) {
            var st = seats[i] || 'empty';
            s += '<circle class="seat ' + st + '" cx="' + c + '" cy="' + c + '" r="' + r + '" stroke-dasharray="' + dash.toFixed(2) + ' ' + (circ - dash).toFixed(2) +
                 '" stroke-dashoffset="' + (-(i * seg) + circ / 4).toFixed(2) + '"/>';
        }
        var filled = seats.filter(function (x) { return x !== 'empty'; }).length;
        s += '<text x="20" y="24" text-anchor="middle">' + filled + '/' + n + '</text></svg>';
        return s;
    }
    function seatsOf(listing) {
        var seats = ['host'];
        (listing.applicants || []).forEach(function (a) { if (a.status === 'accepted') seats.push(a.joined ? 'joined' : (a.ready ? 'ready' : 'accepted')); });
        if (!listing.applicants) for (var i = 0; i < (listing.accepted | 0); i++) seats.push('accepted');
        while (seats.length < listing.size) seats.push('empty');
        return seats.slice(0, listing.size);
    }

    function srcDot(host) {
        return host && host.source === 'web' ? '<i class="gf-dot web" title="Browser card"></i>' : '<i class="gf-dot ok" title="Verified by the RuneTools client"></i>';
    }

    function tagChips(tags) {
        return (tags || []).map(function (t) { return '<span class="gf-tag">' + esc(t) + '</span>'; }).join('');
    }

    function modeLabel(act, modeKey) {
        if (!act) return modeKey || '';
        var m = (act.modes || []).filter(function (x) { return x.key === modeKey; })[0];
        return m ? m.label : modeKey;
    }

    function listingCard(l, act, o) {
        o = o || {};
        var card = el('div', 'gf-listing' + (o.selected ? ' selected' : '') + (o.mine ? ' mine' : '') + ' st-' + l.status);
        card.style.setProperty('--hue', String(act && act.hue != null ? act.hue : 200));
        card.dataset.id = l.id;
        card.innerHTML =
            artHtml() +
            '<div class="gf-listing-main">' +
              '<div class="gf-listing-top"><span class="gf-act">' + esc(act ? act.name : l.activity) + '</span><span class="gf-mode">' + esc(modeLabel(act, l.mode)) + '</span>' +
                (l.status === 'forming' ? '<span class="gf-tag gf-tag-warn">Forming</span>' : '') + (o.mine ? '<span class="gf-tag gf-tag-ok">' + esc(o.mine) + '</span>' : '') + '</div>' +
              '<div class="gf-listing-host"><span class="gf-rsn">' + esc(l.host.rsn) + '</span>' + worldBadge(l.host.world, o.viewerWorld) + srcDot(l.host) +
                '<span class="gf-k">' + fmt(l.host.kills) + ' kills</span>' + (l.minKills ? '<span class="gf-min">min ' + fmt(l.minKills) + '</span>' : '') + (l.verifiedOnly ? '<span class="gf-min" title="Takes RuneTools client cards only">client cards</span>' : '') + '</div>' +
              '<div class="gf-listing-tags">' + tagChips(l.tags) + '<span class="gf-taken' + (stale(l.host.takenAt) ? ' old' : '') + '">' + ago(l.createdAt) + '</span></div>' +
            '</div>' +
            '<div class="gf-listing-ring">' + rosterRing(seatsOf(l), l.size) + '<small>' + l.slots + ' open</small></div>';
        mountArt(card, act, l.activity);
        return card;
    }

    function listingTile(l, act, o) {
        o = o || {};
        var card = el('div', 'gf-listing gf-tilecard' + (o.selected ? ' selected' : '') + (o.mine ? ' mine' : '') + ' st-' + l.status);
        card.style.setProperty('--hue', String(act && act.hue != null ? act.hue : 200));
        card.dataset.id = l.id;
        var seats = seatsOf(l);
        card.innerHTML =
            '<div class="gf-tc-band">' + artHtml() +
              '<div class="gf-tc-title"><span class="gf-act">' + esc(act ? act.name : l.activity) + '</span><span class="gf-mode">' + esc(modeLabel(act, l.mode)) + '</span></div>' +
              '<div class="gf-listing-ring">' + rosterRing(seats, l.size) + '</div></div>' +
            '<div class="gf-tc-body">' +
              '<div class="gf-tc-host"><span class="gf-rsn">' + esc(l.host.rsn) + '</span>' + worldBadge(l.host.world, o.viewerWorld) + srcDot(l.host) +
                (l.status === 'forming' ? '<span class="gf-tag gf-tag-warn">Forming</span>' : '') + (o.mine ? '<span class="gf-tag gf-tag-ok">' + esc(o.mine) + '</span>' : '') + '</div>' +
              '<div class="gf-tc-stats"><span><b>' + fmt(l.host.kills) + '</b> kills</span><span><b>' + l.slots + '</b> open of ' + l.size + '</span>' +
                (l.minKills ? '<span>min <b>' + fmt(l.minKills) + '</b></span>' : '<span class="gf-dim">no minimum</span>') + (l.verifiedOnly ? '<span title="Takes RuneTools client cards only">client cards only</span>' : '') + '</div>' +
              '<div class="gf-listing-tags">' + tagChips(l.tags) + '<span class="gf-taken' + (stale(l.host.takenAt) ? ' old' : '') + '">' + ago(l.createdAt) + '</span></div>' +
            '</div>';
        mountArt(card, act, l.activity);
        return card;
    }

    function artBlock(act, keyFallback) {
        var key = act ? act.key : keyFallback, name = act ? act.name : keyFallback;
        var wrap = el('div', 'gf-art');
        wrap.appendChild(el('span', null, esc(initials(name))));
        var src = key ? artSource(key) : '';
        var put = function (u) {
            if (!u) return;
            var img = el('img', 'gf-art-img'); img.alt = ''; img.loading = 'lazy';
            img.onload = function () { wrap.classList.add('has-img'); };
            img.onerror = function () { img.remove(); };
            img.src = u; wrap.appendChild(img);
        };
        if (src && typeof src.then === 'function') src.then(put, function () {}); else put(src);
        return wrap;
    }
    // markup cannot carry the image's load handler, so cards hold a slot that mountArt fills with a live block
    function artHtml() { return '<span class="gf-art-slot"></span>'; }
    function mountArt(root, act, keyFallback) {
        var slots = root.querySelectorAll('.gf-art-slot');
        for (var i = 0; i < slots.length; i++) slots[i].replaceWith(artBlock(act, keyFallback));
    }

    function initials(name) {
        var w = String(name || '').replace(/[^A-Za-z0-9 ]/g, ' ').split(/\s+/).filter(function (x) { return x && !/^(the|of|and|lord|king)$/i.test(x); });
        return (w.length > 1 ? w[0][0] + w[1][0] : (w[0] || '??').slice(0, 2)).toUpperCase();
    }

    function activityTile(act, o) {
        o = o || {};
        var t = el('button', 'gf-tile' + (o.selected ? ' selected' : ''));
        t.type = 'button'; t.dataset.key = act.key;
        t.style.setProperty('--hue', String(act.hue != null ? act.hue : 200));
        t.appendChild(artBlock(act));
        t.insertAdjacentHTML('beforeend', '<span class="gf-tile-name">' + esc(act.name) + '</span>' +
            '<span class="gf-tile-meta">' + (o.count ? '<b>' + o.count + '</b> open' : '<span class="gf-dim">no groups</span>') +
            (o.myKills != null ? '<em>' + fmt(o.myKills) + ' kc</em>' : '') + '</span>');
        return t;
    }

    window.GF = {
        setIconSource: function (fn) { if (typeof fn === 'function') iconSource = fn; },
        setSkillIcon: function (fn) { if (typeof fn === 'function') skillIcon = fn; },
        setArtSource: function (fn) { if (typeof fn === 'function') artSource = fn; },
        artBlock: artBlock,
        listingTile: listingTile,
        esc: esc, fmt: fmt, ago: ago, stale: stale, el: el,
        playerCard: playerCard, listingCard: listingCard, activityTile: activityTile,
        rosterRing: rosterRing, seatsOf: seatsOf, tagChips: tagChips, modeLabel: modeLabel, initials: initials, worldBadge: worldBadge,
    };
})();
