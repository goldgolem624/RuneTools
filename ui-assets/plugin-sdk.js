// RuneToolsX Plugin SDK (rtx.plugin): shim the host loads into every sandboxed plugin
// iframe. Exposes `window.rtx.plugin`, brokering every call to the host over postMessage;
// the frame's opaque origin has no access to the host Bridge/DOM. Must stay in sync with
// the host broker's method allowlist in client.html.
// Frame CSP blocks fetch/XHR/WebSocket/eval, so bundles must be self-contained; every
// method requires the user-granted manifest scope named in its section below.

(function () {
  'use strict';
  if (window.rtx && window.rtx.plugin) return;

  var PROTO = 'rtx.plugin/1';
  var CALL_TIMEOUT_MS = 15000;

  var seq = 1;
  var pending = Object.create(null);     // id -> { resolve, reject, timer }
  var listeners = { tick: [], state: [] };
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

    // ---- scope: state.read (current account only) ----
    state: {
      player:     function () { return call('state.player', []); },
      info:       function () { return call('state.info', []); },
      inventory:  function () { return call('state.inventory', []); },
      equipment:  function () { return call('state.equipment', []); },
      bank:       function () { return call('state.bank', []); },
      scene:      function (range) { return call('state.scene', [range]); },
      varps:      function (ids) { return call('state.varps', [ids]); },
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
      // Unspent Bonus XP per skill: [{skill,bonus}] (skills with none omitted).
      skillBonus:   function () { return call('state.skillBonus', []); },
      // D&D tracker: {available:{star,etree,dmob,sink,chin,ff,goebie,famil}, resets:{daily,weekly,monthly}, varbits:{id:value}}.
      dailies:      function () { return call('state.dailies', []); },
      // Quest list with live status: [{id,name,difficulty,status,statusText}].
      quests:       function () { return call('state.quests', []); },
      // One quest in full: status + requirements (judged live) + cache journal info.
      quest:        function (id) { return call('state.quest', [id]); },
      // Archaeology mysteries: [{site,name,points,solved,stage:{value,max}|null}].
      mysteries:    function () { return call('state.mysteries', []); },
      // Full live widget tree of one open interface group (the Interfaces-tab walk):
      // {widgets:[{t:[group,comp,sub],d,r:[x,y,w,h],ty,...}]}. Heavier than state.interface.
      interfaceGroup: function (groupId) { return call('state.interfaceGroup', [groupId]); },
      // Live VARC-int values by id -> { "<id>": value } (max 64 ids), like state.varbits.
      varcs:        function (ids) { return call('state.varcs', [ids]); },
      // Player-Owned Ports account state (host-decoded): {resources:[{name,qty,sprite}],
      // tradeGoods:[{name,qty,item}], buildings:[{name,level}], ships:[{nameParts,voyageId,
      // status:'ready'|'sailing'|'returned'|'damaged', etaMinutes|null}], shipCount,
      // scrollPieces, distance, zone}. null until readable.
      ports:        function () { return call('state.ports', []); },
      // Server tick counter (advances once per 600ms game tick). null when the client
      // is not readable (not logged in / not tracked).
      gameTick:     function () { return call('state.gameTick', []); }
    },

    // ---- scope: cache.read (static game data) ----
    cache: {
      itemInfo:  function (id) { return call('cache.itemInfo', [id]); },
      itemIcon:  function (id) { return call('cache.itemIcon', [id]); },
      // Interface type-6 MODEL comp icon by MODEL id. NOTE: the only source of a model id is
      // the host-side cacheIfaceGroup defs "model" field, which is NOT brokered to plugins, so
      // this is usable only with a model id you already hold. state.interfaceGroup does not
      // carry one.
      modelIcon: function (id) { return call('cache.modelIcon', [id]); },
      sprite:    function (id) { return call('cache.sprite', [id]); },
      varbitMap: function () { return call('cache.varbitMap', []); },
      enumInfo:  function (id) { return call('cache.enumInfo', [id]); },
      // Param definition: {type[,int][,str]} ({} until the reader is available).
      paramDef:  function (id) { return call('cache.paramDef', [id]); },
      // Raw param map of one StructType (js5-22): {ints:{key:val}, strs:{key:"val"}}.
      structParams: function (id) { return call('cache.structParams', [id]); },
      // Raw op-249 param map of one item (js5-19): {ints:{key:val}, strs:{key:"val"}}.
      itemParams: function (id) { return call('cache.itemParams', [id]); },
      // Top-down cache terrain render centred on world tile (cx,cy): {w,t,h,png} where png is
      // a base64 PNG (RGB). Older clients returned `b64` = raw RGBA; handle both if you must.
      // (putImageData it into a canvas). half = tiles each side (<=96), ts = px/tile (<=8).
      mapWindow: function (cx, cy, plane, half, ts) { return call('cache.mapWindow', [cx, cy, plane, half, ts]); }
    },

    // ---- scope: overlay (non-interactive visuals only) ----
    overlay: {
      toast:     function (text) { return call('overlay.toast', [text]); },
      notify:    function (text, ttlMs) { return call('overlay.notify', [text, ttlMs]); },
      highlight: function (names) { return call('overlay.highlight', [names]); },
      // Box ONE NPC by name with an optional pill label; optional tileX/tileY boxes the
      // instance nearest that tile instead of the player. Empty name clears the box.
      highlightNpc: function (name, label, tileX, tileY) { return call('overlay.highlightNpc', [name, label, tileX, tileY]); },
      // Highlight the dialogue option matching any of the given texts (string or array).
      highlightOption: function (texts) { return call('overlay.highlightOption', Array.isArray(texts) ? texts : [texts]); },
      // Box the backpack slot holding item `itemId` (optional label); 0 clears.
      highlightItem: function (itemId, label) { return call('overlay.highlightItem', [itemId, label]); },
      // Highlight an arbitrary screen rect (e.g. from state.interface); w/h <= 0 clears.
      highlightRect: function (x, y, w, h) { return call('overlay.highlightRect', [x, y, w, h]); },
      // Several boxes at once: [[x,y,w,h], ...] or [{x,y,w,h}, ...]. Replaces the whole set;
      // [] clears. Shares one set per client with highlightRect, so the last caller wins.
      highlightRects: function (list) { return call('overlay.highlightRects', [list || []]); },
      // Mark world tiles as guide objectives: [{x, y, plane, label}, ...]; [] clears.
      // Optional x2/y2 on a mark (x/y = SW corner, x2/y2 = NE corner) draws one flat
      // ground rect over the whole span instead of a single-tile mark.
      // Optional color ('#rrggbb' or packed int) tints the mark; optional color2 makes
      // a TWO-TONE tile split diagonally between the two colours (single tiles only).
      guideTiles: function (marks) { return call('overlay.guideTiles', [marks]); },
      // Open the in-client wiki browser on a search term ('' = wiki home). The pane is
      // hard-locked to runescape.wiki; a plugin can only pick the page, never the site.
      wikiSearch: function (term) { return call('overlay.wikiSearch', [term || '']); },
      clearHighlight: function () { return call('overlay.clearHighlight', []); },
      // Big centre-screen banner text ('' clears) - the Dungeoneering boss-warning channel.
      // centerText(text, [slot], [rgb]) -- slot 0..2 stack upward from the gameview centre and
      // are cleared independently (pass '' for that slot); rgb is 0xRRGGBB, default warning red.
      // Use separate slots for cues that can be live at the same time, so neither hides the other.
      centerText: function (text, slot, rgb) { return call('overlay.centerText', [text, slot, rgb]); },
      flashGame: function () { return call('overlay.flashGame', []); }
    },

    // ---- scope: notify.os (Windows notifications; rate-capped to 1 per 10s) ----
    notify: {
      windows: function (title, body) { return call('notify.windows', [title, body]); }
    },

    // ---- scope: clipboard (copy-only; nothing is read back) ----
    clipboard: {
      copy: function (text) { return call('clipboard.copy', [text]); }
    },

    // ---- scope: sound (bundled WAV allowlist) ----
    sound: {
      play: function (name) { return call('sound.play', [name]); }
    },

    // ---- scope: storage (per-plugin, per-account, quota-capped) ----
    storage: {
      get:  function (key) { return call('storage.get', [key]); },
      set:  function (key, value) { return call('storage.set', [key, value]); },
      keys: function () { return call('storage.keys', []); }
    },

    // ---- ui (always available) ----
    ui: {
      setHeight: function (px) { return call('ui.setHeight', [px]); },
      setTitle:  function (s) { return call('ui.setTitle', [s]); }
    }
  };

  window.rtx = window.rtx || {};
  window.rtx.plugin = api;

  // Announce the frame is alive so the host sends the 'ready' handshake.
  try { parent.postMessage({ __rtxPlugin: PROTO, kind: 'hello' }, '*'); } catch (e) {}
})();
