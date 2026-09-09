// RuneToolsX Plugin SDK (rtx.plugin): shim the host loads into every sandboxed plugin

(function () {
  'use strict';
  if (window.rtx && window.rtx.plugin) return;

  var PROTO = 'rtx.plugin/1';
  var CALL_TIMEOUT_MS = 15000;

  var seq = 1;
  var pending = Object.create(null);     // id -> { resolve, reject, timer }
  var listeners = { tick: [], state: [], events: [], settings: [] };
  var eventSubs = {};                    // kind -> [cb]; '*' = every kind
  var readyCbs = [];
  var session = { ready: false, scopes: [], apiVersion: null, pluginId: null };

  function call(method, args) {
    return new Promise(function (resolve, reject) {
      var id = seq++;
      var timer = setTimeout(function () {
        if (pending[id]) {
          delete pending[id];
          reject(new Error('rtx.plugin: timeout calling ' + method));
        }
      }, CALL_TIMEOUT_MS);
      pending[id] = { resolve: resolve, reject: reject, timer: timer };
      try {
        parent.postMessage(
          { __rtxPlugin: PROTO, kind: 'call', id: id, method: method, args: args || [] },
          '*'
        );
      } catch (e) {
        clearTimeout(timer);
        delete pending[id];
        reject(e);
      }
    });
  }

  window.addEventListener('message', function (ev) {
    var m = ev.data;
    if (!m || m.__rtxPlugin !== PROTO) return;
    // Only the host (the parent frame) is a legitimate sender.
    if (ev.source !== parent) return;

    if (m.kind === 'reply') {
      var p = pending[m.id];
      if (!p) return;
      clearTimeout(p.timer);
      delete pending[m.id];
      if (m.ok) p.resolve(m.result);
      else p.reject(new Error(m.error || 'rtx.plugin: call failed'));
      return;
    }
    if (m.kind === 'event') {
      if (m.event === 'ready') {
        var d = m.data || {};
        session.ready = true;
        session.scopes = Array.isArray(d.scopes) ? d.scopes.slice() : [];
        session.apiVersion = d.apiVersion || null;
        session.pluginId = d.pluginId || null;
        var cbs = readyCbs.slice(); readyCbs.length = 0;
        cbs.forEach(function (cb) { try { cb(); } catch (e) {} });
        return;
      }
      if (m.event === 'events') {
        var arr = Array.isArray(m.data) ? m.data : [];
        arr.forEach(function (ev) {
          var fs = (eventSubs[ev && ev.kind] || []).concat(eventSubs['*'] || []);
          fs.forEach(function (f) { try { f(ev); } catch (e) {} });
        });
      }
      var ls = listeners[m.event];
      if (ls) ls.forEach(function (cb) { try { cb(m.data); } catch (e) {} });
    }
  });

  var api = {
    apiVersion: function () { return session.apiVersion; },
    id: function () { return session.pluginId; },
    grantedScopes: function () { return session.scopes.slice(); },
    hasScope: function (s) { return session.scopes.indexOf(s) !== -1; },
    // Resolve once the host handshake (granted scopes, identity) has arrived.
    ready: function (cb) {
      if (typeof cb !== 'function') {
        return new Promise(function (res) { session.ready ? res() : readyCbs.push(res); });
      }
      session.ready ? cb() : readyCbs.push(cb);
    },
    // ---- events (host-pushed; no free-polling) ----
    on: function (evt, cb) {
      if (listeners[evt] && typeof cb === 'function') listeners[evt].push(cb);
    },
    // Game events (requires state.read). kind: skill_update, container_update, runclientscript, ge_offer,
    // run_energy, run_weight, ping, raw, gameTick; '*' for all. Batched on the host push cadence, one callback per event, capture order.
    events: {
      on:  function (kind, cb) { if (typeof cb !== 'function') return; (eventSubs[kind] = eventSubs[kind] || []).push(cb); },
      off: function (kind, cb) { var a = eventSubs[kind]; if (!a) return; var i = a.indexOf(cb); if (i !== -1) a.splice(i, 1); }
    },

    // ---- scope: state.read (current account only) ----
    state: {
      player:     function () { return call('state.player', []); },
      info:       function () { return call('state.info', []); },
      inventory:  function () { return call('state.inventory', []); },
      equipment:  function () { return call('state.equipment', []); },
      bank:       function () { return call('state.bank', []); },
      scene:      function (range) { return call('state.scene', [range]); },
      varps:      function (ids) { return call('state.varps', [ids]); },
      // Live var stores per domain, resolved the way the client binds them for scripts:
      // Clan (6) and player-group (9) values are listed when those stores exist.
      varDomainStores: function () { return call('state.varDomainStores', []); },
      buffs:      function () { return call('state.buffs', []); },
      cooldowns:  function () { return call('state.cooldowns', []); },
      perks:      function () { return call('state.perks', []); },
      actionBar:  function () { return call('state.actionBar', []); },
      container:  function (containerId) { return call('state.container', [containerId]); },
      itemExtra:  function (containerId, itemId) { return call('state.itemExtra', [containerId, itemId]); },
      pets:       function () { return call('state.pets', []); },
      bosses:     function () { return call('state.bosses', []); },
      hideyHoles: function () { return call('state.hideyHoles', []); },
      groupBank:  function () { return call('state.groupBank', []); },
      varbits:    function (ids) { return call('state.varbits', [ids]); },
      interface:  function (g, comps) { return call('state.interface', [g, Array.isArray(comps) ? comps.join(',') : String(comps)]); },
      achievements: function () { return call('state.achievements', []); },
      achievement:  function (id) { return call('state.achievement', [id]); },
      metalBank:    function () { return call('state.metalBank', []); },
      materials:    function () { return call('state.materials', []); },
      baitBox:      function () { return call('state.baitBox', []); },
      groundItems:  function () { return call('state.groundItems', []); },
      skillBonus:   function () { return call('state.skillBonus', []); },
      dailies:      function () { return call('state.dailies', []); },
      quests:       function () { return call('state.quests', []); },
      quest:        function (id) { return call('state.quest', [id]); },
      mysteries:    function () { return call('state.mysteries', []); },
      interfaceGroup: function (groupId) { return call('state.interfaceGroup', [groupId]); },
      varcs:        function (ids) { return call('state.varcs', [ids]); },
      ports:        function () { return call('state.ports', []); },
      gameTick:     function () { return call('state.gameTick', []); }
    },

    cache: {
      itemInfo:  function (id) { return call('cache.itemInfo', [id]); },
      itemIcon:  function (id) { return call('cache.itemIcon', [id]); },
      modelIcon: function (id) { return call('cache.modelIcon', [id]); },
      sprite:    function (id) { return call('cache.sprite', [id]); },
      varbitMap: function () { return call('cache.varbitMap', []); },
      varbitDomainMap: function () { return call('cache.varbitDomainMap', []); },
      varbitDomains: function () { return call('cache.varbitDomains', []); },
      varDefs:   function (archive) { return call('cache.varDefs', [archive]); },
      enumInfo:  function (id) { return call('cache.enumInfo', [id]); },
      paramDef:  function (id) { return call('cache.paramDef', [id]); },
      structParams: function (id) { return call('cache.structParams', [id]); },
      itemParams: function (id) { return call('cache.itemParams', [id]); },
      mapWindow: function (cx, cy, plane, half, ts) { return call('cache.mapWindow', [cx, cy, plane, half, ts]); }
    },

    overlay: {
      toast:     function (text) { return call('overlay.toast', [text]); },
      notify:    function (text, ttlMs) { return call('overlay.notify', [text, ttlMs]); },
      highlight: function (names) { return call('overlay.highlight', [names]); },
      highlightNpc: function (name, label, tileX, tileY) { return call('overlay.highlightNpc', [name, label, tileX, tileY]); },
      highlightOption: function (texts) { return call('overlay.highlightOption', Array.isArray(texts) ? texts : [texts]); },
      highlightItem: function (itemId, label) { return call('overlay.highlightItem', [itemId, label]); },
      highlightRect: function (x, y, w, h) { return call('overlay.highlightRect', [x, y, w, h]); },
      highlightRects: function (list) { return call('overlay.highlightRects', [list || []]); },
      guideTiles: function (marks) { return call('overlay.guideTiles', [marks]); },
      wikiSearch: function (term) { return call('overlay.wikiSearch', [term || '']); },
      clearHighlight: function () { return call('overlay.clearHighlight', []); },
      centerText: function (text, slot, rgb) { return call('overlay.centerText', [text, slot, rgb]); },
      flashGame: function () { return call('overlay.flashGame', []); }
    },

    notify: {
      windows: function (title, body) { return call('notify.windows', [title, body]); },
      // Posts to the user's own Discord webhook (scope notify.discord). Text only, pings stripped, 1 per 10s.
      discord: function (text) { return call('notify.discord', [text]); }
    },

    clipboard: {
      copy: function (text) { return call('clipboard.copy', [text]); },
      paste: function () { return call('clipboard.paste', []); }
    },

    sound: {
      play: function (name) { return call('sound.play', [name]); }
    },

    storage: {
      get:  function (key) { return call('storage.get', [key]); },
      set:  function (key, value) { return call('storage.set', [key, value]); },
      keys: function () { return call('storage.keys', []); }
    },

    ui: {
      setHeight: function (px) { return call('ui.setHeight', [px]); },
      setTitle:  function (s) { return call('ui.setTitle', [s]); },
      settings:  function (schema) { return call('ui.settings', [schema]); }
    },

    prices: {
      latest:  function () { return call('prices.latest', []); },
      mapping: function () { return call('prices.mapping', []); },
      item: function (ids) { return call('prices.item', [ids]); }
    },

    settings: {
      get: function () { return call('settings.get', []); },
      on:  function (cb) { if (typeof cb === 'function') listeners.settings.push(cb); }
    }
  };

  window.rtx = window.rtx || {};
  window.rtx.plugin = api;

  try { parent.postMessage({ __rtxPlugin: PROTO, kind: 'hello' }, '*'); } catch (e) {}
})();
