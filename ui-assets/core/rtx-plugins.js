// rtx-plugins.js: Plugin SDK host broker: PLUGIN_API, PLUGIN_SDK_SHIM, pluginBrokerInit, permission grants, plugin windows, marketplace browse/install, allTabs.
// Loads after: rtx-wm.js, rtx-bridge.js, rtx-notify.js; starts the GIM membership poll interval at load.
// Plain script, page globals by design: every top-level name here is a page global that panels and the other core files use.
  // ===================== Plugin SDK (host broker) =====================
  // Plugins run in a sandboxed <iframe sandbox="allow-scripts"> (opaque origin,
  // no allow-same-origin) so they cannot reach window.rtx, the host DOM, or each
  // other. Their only channel is postMessage to this broker, which exposes a
  // curated rtx.plugin API gated by manifest scopes + rate limits.
  // See PLUGIN_SDK.md. A plugin only ever reaches the read, overlay, sound, and
  // storage methods its granted scopes allow.
  const PLUGIN_PROTO  = 'rtx.plugin/1';
  const PLUGIN_SCOPES = new Set(['state.read', 'cache.read', 'overlay', 'sound', 'storage', 'notify.os', 'clipboard', 'clipboard.read']);
  let   pluginTabs    = [];     // [{ id, manifest:{id,name,version,author,entry,description}, scopes:[] }]
  const pluginMounts  = new Map();   // plugin id -> { id, frame, scopes }; one per open plugin window
  const pluginBuckets = {};     // "id|method" -> token bucket
  // ---- per-plugin settings, declared by the plugin and rendered on the host's own
  // Preferences page (rtx-settings.js) so every plugin gets standard controls for
  // free. The plugin calls rtx.plugin.ui.settings(schema) once at boot; values are
  // stored host-side in the plugin's storage bucket under the reserved key
  // '~settings' (host-mediated: no 'storage' scope needed) and pushed back to the
  // plugin as a 'settings' event on declare and on every change.
  const pluginSettingsReg = new Map();   // plugin id -> { name, schema, values }
  function pluginSettingsSchema(raw) {
    if (!Array.isArray(raw)) return null;
    const out = [];
    for (const c of raw.slice(0, 24)) {
      if (!c || typeof c !== 'object') continue;
      const key = String(c.key || '');
      if (!/^[A-Za-z0-9_.-]{1,32}$/.test(key)) continue;
      const type = String(c.type || '');
      if (['toggle', 'select', 'slider', 'text'].indexOf(type) < 0) continue;
      const ctl = { key, type, label: pClampStr(c.label || key, 48), hint: pClampStr(c.hint || '', 120) };
      if (type === 'select') {
        ctl.options = (Array.isArray(c.options) ? c.options : []).slice(0, 12)
          .map(o => ({ v: pClampStr(o && o.v, 40), label: pClampStr((o && o.label) || (o && o.v), 32) }))
          .filter(o => o.v !== '');
        if (!ctl.options.length) continue;
      }
      if (type === 'slider') {
        ctl.min = pClampNum(c.min, -1e6, 1e6); ctl.max = pClampNum(c.max, -1e6, 1e6);
        if (!(ctl.max > ctl.min)) continue;
        ctl.step = pClampNum(c.step || 1, 1e-3, ctl.max - ctl.min);
      }
      ctl.def = pluginSettingClamp(ctl, c.default);
      out.push(ctl);
    }
    return out.length ? out : null;
  }
  function pluginSettingClamp(ctl, v) {
    if (ctl.type === 'toggle') return !!v;
    if (ctl.type === 'slider') return pClampNum(v, ctl.min, ctl.max);
    if (ctl.type === 'select') return ctl.options.some(o => o.v === v) ? v : ctl.options[0].v;
    return pClampStr(v == null ? '' : v, 200);
  }
  // Change one value (Preferences page calls this): clamp, persist, push to the plugin.
  function pluginSettingsSet(id, key, v) {
    const reg = pluginSettingsReg.get(id);
    if (!reg) return;
    const ctl = reg.schema.find(c => c.key === key);
    if (!ctl) return;
    reg.values[key] = pluginSettingClamp(ctl, v);
    try { bridge().pluginStoreSave(myPid(), id, '~settings', JSON.stringify(reg.values)); } catch (e) {}
    const m = pluginMounts.get(id);
    if (m) pluginSendEvent(m, 'settings', Object.assign({}, reg.values));
  }
  function pluginUnmount(id) {
    // Release keyboard capture a frame claimed: an unmounted frame fires no focusout, and a
    // leaked grab would eat every keystroke meant for the game from then on.
    const m = pluginMounts.get(id);
    if (m && m.kbFocus) kbGrab(false);
    pluginMounts.delete(id);
  }

  function pluginEsc(s) { const d = document.createElement('div'); d.textContent = String(s == null ? '' : s); return d.innerHTML; }
  // Canonical HTML-text escape shared by the panels (global scope). Escapes & < > and ".
  function htmlEsc(s) { return String(s == null ? '' : s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;'); }

  // Canonical SDK shim (mirror of ui-assets/plugin-sdk.js) spliced into each plugin
  // frame; inlined because LoadHTML gives the panel no base URL to fetch from.
  const PLUGIN_SDK_SHIM = `(function(){'use strict';if(window.rtx&&window.rtx.plugin)return;
var P='rtx.plugin/1',T=15000,seq=1,pending=Object.create(null),L={tick:[],state:[],events:[],settings:[]},EV={},rcb=[],S={ready:false,scopes:[],apiVersion:null,pluginId:null};
function call(method,args){return new Promise(function(res,rej){var id=seq++;var tm=setTimeout(function(){if(pending[id]){delete pending[id];rej(new Error('rtx.plugin: timeout '+method));}},T);pending[id]={res:res,rej:rej,tm:tm};try{parent.postMessage({__rtxPlugin:P,kind:'call',id:id,method:method,args:args||[]},'*');}catch(e){clearTimeout(tm);delete pending[id];rej(e);}});}
window.addEventListener('message',function(ev){var m=ev.data;if(!m||m.__rtxPlugin!==P)return;if(ev.source!==parent)return;
if(m.kind==='edit'){var c=String(m.cmd||''),ok=false;try{ok=document.execCommand(c);}catch(e){}
// execCommand can refuse inside a sandboxed frame; fall back to the broker clipboard
// (scope-gated like any other call) and edit the focused field directly.
if(!ok){var t=document.activeElement,ed=!!(t&&(t.tagName==='INPUT'||t.tagName==='TEXTAREA'));
var fire=function(){try{t.dispatchEvent(new Event('input',{bubbles:true}));}catch(e){}};
if(c==='paste'&&ed){call('clipboard.paste').then(function(txt){txt=String(txt==null?'':txt);var s=t.selectionStart|0,en=t.selectionEnd|0;t.value=t.value.slice(0,s)+txt+t.value.slice(en);t.selectionStart=t.selectionEnd=s+txt.length;fire();}).catch(function(){});}
else if((c==='copy'||c==='cut')&&ed){var s2=t.selectionStart|0,e2=t.selectionEnd|0,sel=t.value.slice(s2,e2);if(sel){call('clipboard.copy',[sel]).catch(function(){});if(c==='cut'){t.value=t.value.slice(0,s2)+t.value.slice(e2);t.selectionStart=t.selectionEnd=s2;fire();}}}}
return;}
if(m.kind==='reply'){var p=pending[m.id];if(!p)return;clearTimeout(p.tm);delete pending[m.id];if(m.ok)p.res(m.result);else p.rej(new Error(m.error||'rtx.plugin: failed'));return;}
if(m.kind==='event'){if(m.event==='ready'){var d=m.data||{};S.ready=true;S.scopes=Array.isArray(d.scopes)?d.scopes.slice():[];S.apiVersion=d.apiVersion||null;S.pluginId=d.pluginId||null;var c=rcb.slice();rcb.length=0;c.forEach(function(f){try{f();}catch(e){}});return;}if(m.event==='events'){var arr=Array.isArray(m.data)?m.data:[];arr.forEach(function(ev){var k=ev&&ev.kind;var fs=(EV[k]||[]).concat(EV['*']||[]);fs.forEach(function(f){try{f(ev);}catch(e){}});});}var ls=L[m.event];if(ls)ls.forEach(function(f){try{f(m.data);}catch(e){}});}});
function g(o){return o;}
var api={apiVersion:function(){return S.apiVersion;},id:function(){return S.pluginId;},grantedScopes:function(){return S.scopes.slice();},hasScope:function(s){return S.scopes.indexOf(s)!==-1;},
ready:function(cb){if(typeof cb!=='function'){return new Promise(function(r){S.ready?r():rcb.push(r);});}S.ready?cb():rcb.push(cb);},
on:function(e,cb){if(L[e]&&typeof cb==='function')L[e].push(cb);},
events:{on:function(k,cb){if(typeof cb!=='function')return;(EV[k]=EV[k]||[]).push(cb);},off:function(k,cb){var a=EV[k];if(!a)return;var i=a.indexOf(cb);if(i!==-1)a.splice(i,1);}},
state:{player:function(){return call('state.player',[]);},info:function(){return call('state.info',[]);},inventory:function(){return call('state.inventory',[]);},equipment:function(){return call('state.equipment',[]);},bank:function(){return call('state.bank',[]);},scene:function(r){return call('state.scene',[r]);},varps:function(i){return call('state.varps',[i]);},varbits:function(ids){return call('state.varbits',[ids]);},interface:function(g,comps){return call('state.interface',[g,Array.isArray(comps)?comps.join(','):String(comps)]);},buffs:function(){return call('state.buffs',[]);},cooldowns:function(){return call('state.cooldowns',[]);},perks:function(){return call('state.perks',[]);},actionBar:function(){return call('state.actionBar',[]);},container:function(c){return call('state.container',[c]);},itemExtra:function(c,i){return call('state.itemExtra',[c,i]);},pets:function(){return call('state.pets',[]);},bosses:function(){return call('state.bosses',[]);},hideyHoles:function(){return call('state.hideyHoles',[]);},groupBank:function(){return call('state.groupBank',[]);},achievements:function(){return call('state.achievements',[]);},achievement:function(id){return call('state.achievement',[id]);},metalBank:function(){return call('state.metalBank',[]);},materials:function(){return call('state.materials',[]);},baitBox:function(){return call('state.baitBox',[]);},groundItems:function(){return call('state.groundItems',[]);},skillBonus:function(){return call('state.skillBonus',[]);},dailies:function(){return call('state.dailies',[]);},quests:function(){return call('state.quests',[]);},quest:function(id){return call('state.quest',[id]);},mysteries:function(){return call('state.mysteries',[]);},interfaceGroup:function(g){return call('state.interfaceGroup',[g]);},varcs:function(ids){return call('state.varcs',[ids]);},ports:function(){return call('state.ports',[]);},gameTick:function(){return call('state.gameTick',[]);}},
cache:{itemInfo:function(i){return call('cache.itemInfo',[i]);},itemIcon:function(i){return call('cache.itemIcon',[i]);},modelIcon:function(i){return call('cache.modelIcon',[i]);},sprite:function(i){return call('cache.sprite',[i]);},varbitMap:function(){return call('cache.varbitMap',[]);},enumInfo:function(i){return call('cache.enumInfo',[i]);},paramDef:function(i){return call('cache.paramDef',[i]);},structParams:function(i){return call('cache.structParams',[i]);},itemParams:function(i){return call('cache.itemParams',[i]);},mapWindow:function(cx,cy,p,half,ts){return call('cache.mapWindow',[cx,cy,p,half,ts]);},abilityConfigs:function(){return call('cache.abilityConfigs',[]);},abilityTips:function(){return call('cache.abilityTips',[]);}},
notify:{windows:function(title,body){return call('notify.windows',[title,body]);}},
clipboard:{copy:function(t){return call('clipboard.copy',[t]);},paste:function(){return call('clipboard.paste',[]);}},
overlay:{toast:function(t){return call('overlay.toast',[t]);},notify:function(t,ms){return call('overlay.notify',[t,ms]);},centerText:function(t,s,c){return call('overlay.centerText',[t,s,c]);},highlight:function(n){return call('overlay.highlight',[n]);},highlightNpc:function(n,l,tx,ty){return call('overlay.highlightNpc',[n,l,tx,ty]);},flashGame:function(){return call('overlay.flashGame',[]);},highlightOption:function(t){return call('overlay.highlightOption',Array.isArray(t)?t:[t]);},highlightItem:function(i,l){return call('overlay.highlightItem',[i,l]);},guideTiles:function(m){return call('overlay.guideTiles',[m]);},highlightRect:function(x,y,w,h){return call('overlay.highlightRect',[x,y,w,h]);},highlightRects:function(l){return call('overlay.highlightRects',[l||[]]);},clearHighlight:function(){return call('overlay.clearHighlight',[]);},hudAbilities:function(p){return call('overlay.hudAbilities',[p||null]);}},
sound:{play:function(n){return call('sound.play',[n]);}},
storage:{get:function(k){return call('storage.get',[k]);},set:function(k,v){return call('storage.set',[k,v]);},keys:function(){return call('storage.keys',[]);}},
ui:{setHeight:function(px){return call('ui.setHeight',[px]);},setTitle:function(s){return call('ui.setTitle',[s]);},settings:function(schema){return call('ui.settings',[schema]);}},
settings:{get:function(){return call('settings.get',[]);},on:function(cb){if(typeof cb==='function')L.settings.push(cb);}}};
window.rtx=window.rtx||{};window.rtx.plugin=api;
// Keyboard focus publishing: the frame is a sandboxed opaque origin, so the host page cannot
// see our activeElement; without this every keystroke aimed at a plugin text field went to the
// game. Mirrors the host's own focus test; the 1s interval self-heals a field removed by a
// rebuild (no focusout fires for that).
var kbOn=false;function kbSync(){var t=document.activeElement;var on=!!(t&&(t.tagName==='INPUT'||t.tagName==='TEXTAREA'||t.tagName==='SELECT'||t.isContentEditable));if(on===kbOn)return;kbOn=on;try{parent.postMessage({__rtxPlugin:P,kind:'kb',on:on},'*');}catch(e){}}
document.addEventListener('focusin',function(){setTimeout(kbSync,0);});
document.addEventListener('focusout',function(){setTimeout(kbSync,0);});
setInterval(kbSync,1000);
try{parent.postMessage({__rtxPlugin:P,kind:'hello'},'*');}catch(e){}})();`;

  // ---- arg clamps (defence: every plugin-supplied value is bounded) ----
  const pClampId   = v => { const n = parseInt(v, 10); return (isFinite(n) && n >= 0) ? n : 0; };
  const pClampNum  = (v, lo, hi) => { let n = Number(v); if (!isFinite(n)) n = lo; return Math.max(lo, Math.min(hi, n)); };
  const pClampStr  = (v, max) => String(v == null ? '' : v).slice(0, max);
  // Trusted-arg pass-through for the promoted panel bindings below: shape is kept, size is bounded.
  const pArgs      = a => (Array.isArray(a) ? a : []).slice(0, 8).map(x => (typeof x === 'number' || typeof x === 'boolean' || x == null) ? x : pClampStr(x, 65536));
  const pClampList = v => (Array.isArray(v) ? v : []).slice(0, 50).map(x => pClampStr(x, 40).replace(/[^A-Za-z0-9 _'\-]/g, '')).filter(Boolean).join(',');

  // method -> { scope, run(args,pid,id,frame), json }. Only safe read/overlay/
  // storage/sound calls; everything actuating/privileged is simply absent.
  const PLUGIN_API = {
    'state.player':     { scope: 'state.read', json: true,    run: (a, pid) => bridge().playerInfo(pid) },
    'state.info':       { scope: 'state.read', json: true,    run: (a, pid) => bridge().playerInfo(pid) },
    'state.inventory':  { scope: 'state.read', json: true,    run: (a, pid) => bridge().inventory(pid) },
    'state.equipment':  { scope: 'state.read', json: true,    run: (a, pid) => bridge().equipment(pid) },
    'state.bank':       { scope: 'state.read', json: true,    run: (a, pid) => bridge().bankItems(pid) },
    'state.metalBank':  { scope: 'state.read', json: true,    run: (a, pid) => bridge().metalBankItems(pid) },
    'state.materials':  { scope: 'state.read', json: true,    run: (a, pid) => bridge().materialItems(pid) },
    'state.groupBank':  { scope: 'state.read', json: true,    run: (a, pid) => bridge().groupBankItems(pid) },
    'state.baitBox':    { scope: 'state.read', json: true,    run: (a, pid) => bridge().baitBoxItems(pid) },
    'state.scene':      { scope: 'state.read', json: true,    run: (a, pid) => bridge().sceneEntities(pid, pClampNum(a[0], 1, 64)) },
    // Dropped ground items (item stacks lying on tiles): [{id,x,y,plane},..]. Read-only scene data.
    'state.groundItems':{ scope: 'state.read', json: true,    run: (a, pid) => bridge().groundItems(pid) },
    'state.varps':      { scope: 'state.read', json: true,    run: (a, pid) => bridge().varps(pid, pClampStr(a[0], 200)) },
    // Live VARBIT values by id -> { "<id>": value }. Resolves each varbit's backing varp+bits from
    // the cache and reads them live (most QoL state is varbit-driven, so this is the clean primitive).
    'state.varbits':    { scope: 'state.read', json: false,   run: async (a) => {
                            const ids = (Array.isArray(a[0]) ? a[0] : []).slice(0, 64)
                              .map(x => parseInt(x, 10)).filter(n => isFinite(n) && n >= 0 && n < 200000);
                            const vals = await readVarbitValues(ids);
                            const out = {}; for (const id of ids) out[id] = (vals && vals[id]) || 0; return out; } },
    // Live text + absolute screen rect of specific components of an open interface group. e.g. the
    // NPC chat box (group 1184): comp 4 = NPC name, comp 10 = message, comp 15 = continue button.
    'state.interface':  { scope: 'state.read', json: true,    run: (a, pid) => bridge().interfaceComps(pid, pClampId(a[0]), pClampStr(a[1], 200)) },
    'state.buffs':      { scope: 'state.read', json: true,    run: (a, pid) => bridge().buffs(pid) },
    'state.cooldowns':  { scope: 'state.read', json: true,    run: (a, pid) => bridge().cooldowns(pid) },
    'state.perks':      { scope: 'state.read', json: true,    run: (a, pid) => bridge().perks(pid) },
    'state.actionBar':  { scope: 'state.read', json: true,    run: (a, pid) => bridge().actionBar(pid) },
    'state.container':  { scope: 'state.read', json: true,    run: (a, pid) => bridge().containerItems(pid, pClampId(a[0])) },
    'state.itemExtra':  { scope: 'state.read', json: true,    run: (a, pid) => bridge().itemExtraInts(pid, pClampId(a[0]), pClampId(a[1])) },
    'state.pets':       { scope: 'state.read', json: false,   run: async (a, pid) => {
                            let vp = {}; try { vp = JSON.parse(await bridge().varps(pid, PET_VARPS.join(','))); } catch (e) {}
                            return PETS.map(p => ({ name: p.n, category: PET_CATS[p.c], skill: p.si >= 0 ? SKILL_NAMES[p.si] : null,
                              obtained: petOwned(p, vp), source: p.s || '', item: p.ic || 0, icon: PET_ICON_URL[p.n] || '' })); } },
    'state.bosses':     { scope: 'state.read', json: false,   run: async (a, pid) => {
                            let vp = {}; try { vp = JSON.parse(await bridge().varps(pid, BOSS_VARPS.join(','))); } catch (e) {}
                            return BOSSES.map(b => {
                              const k1 = bossField(vp, b.kc) + 60000 * bossField(vp, b.pr);
                              const k2 = b.kc2 ? bossField(vp, b.kc2) + 60000 * bossField(vp, b.pr2) : null;
                              return { name: b.n, mode: b.m1 || 'Normal', kills: k1,
                                       mode2: b.kc2 ? (b.m2 || 'Hard mode') : null, kills2: k2, total: k1 + (k2 || 0),
                                       collectionLog: b.log ? (!!(((vp[b.log[0]] || 0) >>> b.log[1]) & 1)) : null }; }); } },
    'state.hideyHoles': { scope: 'state.read', json: false,   run: async (a, pid) => {
                            let vp = {}; try { vp = JSON.parse(await bridge().varps(pid, HIDEY_VARPS.join(','))); } catch (e) {}
                            return HIDEY.map(h => { const v = (((vp[h.vp] || 0) >>> 0) >>> h.b) & 3;
                              return { name: h.n, tier: HIDEY_TIERS[h.t], location: h.loc, build: HIDEY_BUILD[h.t],
                                       fillItems: h.fi || [], state: v, built: v >= 1, filled: v === 2 }; }); } },
    // Server tick counter. Returns a NUMBER, not JSON, hence json:false; the host returns -1
    // when the client is not tracked, which is normalised to null so a plugin sees the same
    // "cannot read" shape every other state call uses rather than a magic number.
    // Event channel: {seq,tick,events:[{seq,t,wall,op,len,kind,...}]} after sinceSeq (host ring, 512 cap).
    'state.events':     { scope: 'state.read', json: true,    run: (a, pid) => bridge().events(pid, pClampNum(a[0], 0, 4294967295)) },
    'state.gameTick':   { scope: 'state.read', json: false,
                          run: (a, pid) => { const t = bridge().gameTick(pid); return (typeof t === 'number' && t >= 0) ? t : null; } },
    'cache.itemInfo':   { scope: 'cache.read', json: true,    run: (a) => bridge().itemInfo(pClampId(a[0])) },
    'cache.itemIcon':   { scope: 'cache.read', json: false,   run: (a) => bridge().itemIcon(pClampId(a[0])) },
    // Pre-rendered interface type-6 MODEL comp icons, keyed by MODEL id (from cacheIfaceGroup
    // defs). cacheIfaceGroup itself is host-only and not brokered, and state.interfaceGroup
    // carries no model field, so a plugin can only use this with a model id it already has.
    'cache.modelIcon':  { scope: 'cache.read', json: false,   run: (a) => (bridge().modelIcon ? bridge().modelIcon(pClampId(a[0])) : '') },
    'cache.sprite':     { scope: 'cache.read', json: false,   run: (a) => bridge().sprite(pClampId(a[0])) },
    'cache.varbitMap':  { scope: 'cache.read', json: true,    run: () => bridge().varbitMap() },
    'cache.enumInfo':   { scope: 'cache.read', json: true,    run: (a) => bridge().enumInfo(pClampId(a[0])) },
    // Param definition from the cache: {type[,int][,str]} ({} until the reader ships).
    'cache.paramDef':   { scope: 'cache.read', json: true,    run: (a) => (bridge().paramDef ? bridge().paramDef(pClampId(a[0])) : '{}') },
    // Raw param map of one StructType (js5-22): {"ints":{k:v},"strs":{k:"v"}}.
    'cache.structParams':{ scope: 'cache.read', json: true,   run: (a) => (bridge().structParams ? bridge().structParams(pClampId(a[0])) : '{}') },
    // Raw op-249 param map of one item (js5-19): {"ints":{k:v},"strs":{k:"v"}}.
    'cache.itemParams': { scope: 'cache.read', json: true,    run: (a) => (bridge().itemParams ? bridge().itemParams(pClampId(a[0])) : '{}') },
    // Top-down cache terrain render centred on a world tile (the World Map / Zygomites imagery):
    // {"w","t","h","b64"} RGBA. Pure cache data; clamps keep single calls bounded.
    'cache.mapWindow':  { scope: 'cache.read', json: true,    run: (a) => (bridge().mapWindow
                            ? bridge().mapWindow(pClampNum(a[0], 0, 16000), pClampNum(a[1], 0, 16000),
                                                 pClampNum(a[2], 0, 3), pClampNum(a[3], 0, 96), pClampNum(a[4], 0, 8), 15, true)
                            : '{}') },
    // Full live widget tree of ONE open interface group (id/type/rect/text/sprite per node) --
    // what the Interfaces tab shows. Heavier than state.interface; same read-only data.
    'state.interfaceGroup':{ scope: 'state.read', json: true, run: (a, pid) => bridge().interfaceGroup(pid, pClampId(a[0])) },
    // Player-Owned Ports account STATE, decoded host-side (panel_portsinfo.js is the one
    // decode; this serves it to every plugin): resources, trade goods, building levels,
    // ship statuses with ETA, scroll pieces, exploration distance/zone. Names for
    // ships/voyages stay consumer-side (enum lookups). null until readable.
    'state.ports':      { scope: 'state.read', json: false,   run: async () => (typeof portsStateRead === 'function' ? await portsStateRead() : null) },
    // Live VARC-int values by id -> {"<id>": value}. Reads the full varc hashmap dump and
    // filters to the requested ids (<=64), mirroring state.varbits' shape.
    'state.varcs':      { scope: 'state.read', json: false,   run: async (a, pid) => {
                            const ids = (Array.isArray(a[0]) ? a[0] : []).slice(0, 64)
                              .map(x => parseInt(x, 10)).filter(n => isFinite(n) && n >= 0 && n < 200000);
                            let all = {}; try { all = JSON.parse(await bridge().varcsDumpAll(pid) || '{}'); } catch (e) {}
                            // varcsDumpAll keys are scope-prefixed "5:<id>" (5 = varc-int); a bare-id
                            // lookup always missed and served 0 for every varc (owner-caught via the
                            // Hefin lap timer reading 0:00 while the watcher showed varc 7416 counting).
                            const out = {}; for (const id of ids) out[id] = (all['5:' + id] | 0) || 0; return out; } },
    // Achievements: full roster with live completion + per-requirement progress, or a single
    // achievement by id (state.achievement(id) -> {id,name,complete,...}). state.read scope.
    'state.achievements':{ scope: 'state.read', json: false,  run: (a, pid) => pluginAchievements(pid) },
    'state.achievement': { scope: 'state.read', json: false,  run: async (a, pid) => {
                            const id = pClampId(a[0]); const all = await pluginAchievements(pid);
                            return all.find(x => x.id === id) || null; } },
    // Unspent Bonus XP per skill -> [{skill,bonus}] (skills with none omitted; values
    // are already /10, matching the in-game skill tooltips).
    'state.skillBonus': { scope: 'state.read', json: false,   run: async (a, pid) => {
                            let vp = {}; try { vp = JSON.parse(await bridge().varps(pid, SKILL_BONUS_CSV)); } catch (e) {}
                            const out = [];
                            for (let i = 0; i < SKILL_BONUS_VARP.length; i++) {
                              const raw = (vp[SKILL_BONUS_VARP[i]] | 0);
                              if (raw > 0) out.push({ skill: SKILL_NAMES[i] || ('Skill ' + i), bonus: raw / 10 });
                            }
                            return out; } },
    // D&D tracker: available = the same tests the D&D Tracker's notification bells use
    // (star/etree/dmob/sink/chin/ff/goebie/famil); resets = epoch ms of the next daily/
    // weekly/monthly 00:00 UTC reset; varbits = the raw tracker values keyed by id for
    // everything else the tab derives.
    'state.dailies':    { scope: 'state.read', json: false,   run: async (a, pid) => {
                            let vb = null; try { vb = JSON.parse(await bridge().varbits(pid, DW_IDS)); } catch (e) {}
                            if (!vb || typeof vb !== 'object') return null;
                            return { available: dwAvail(vb), resets: dwNextResets(), varbits: vb,
                                     vos: dwVosState(vb) }; } },
    // Quest list with live status: [{id,name,difficulty,status,statusText}].
    // status: 0 not started / 1 in progress / 2 complete / -1 no tracker in the cache.
    'state.quests':     { scope: 'state.read', json: false,   run: async (a, pid) => {
                            if (!await questEnsureDefs()) return null;
                            let vp = {}; try { vp = JSON.parse(await bridge().varps(pid, QUEST_VARPS.join(','))); } catch (e) {}
                            if (!vp || !Object.keys(vp).length) return null;
                            return QUESTS.map(q => { const st = questStatus(q, vp);
                              return { id: q.id, name: q.n, difficulty: q.d !== undefined ? (QDIFF[q.d] || null) : null,
                                       status: st, statusText: st >= 0 ? QSTATUS[st] : 'Unknown' }; }); } },
    // One quest in full: live status, requirements judged against the live account
    // (skills / quest points / prereq quests), and the journal info from the cache
    // (strings may contain <br> line breaks, as the journal stores them).
    'state.quest':      { scope: 'state.read', json: false,   run: async (a, pid) => {
                            if (!await questEnsureDefs()) return null;
                            const q = QUEST_BY_ID.get(pClampId(a[0])); if (!q) return null;
                            await questEnsureEnums();
                            let vp = {}; try { vp = JSON.parse(await bridge().varps(pid, QUEST_VARPS.join(','))); } catch (e) {}
                            const st = questStatus(q, vp);
                            const reqs = questReqs(q, vp, id2 => { const r = QUEST_BY_ID.get(id2); return r ? questStatus(r, vp) : -1; });
                            const E = QUEST_ENUMS || { ln: {}, ag: {}, ar: {} };
                            return { id: q.id, name: q.n, difficulty: q.d !== undefined ? (QDIFF[q.d] || null) : null,
                                     status: st, statusText: st >= 0 ? QSTATUS[st] : 'Unknown',
                                     requirements: {
                                       questPoints: reqs.qp,
                                       skills: reqs.skills.map(s => ({ skill: SKILL_NAMES[s.id] || ('Skill ' + s.id), need: s.need, have: s.have, ok: s.ok })),
                                       quests: reqs.quests.map(r => ({ id: r.id, name: r.name, complete: r.ok })),
                                       missing: reqs.missing },
                                     journal: { description: q.ds || null, startPoint: q.sp || null,
                                                requiredItems: q.it || null, combat: q.cb || null,
                                                xpRewards: q.rx || null, otherRewards: q.rw || null,
                                                length: (q.ln !== undefined && QUEST_ENUMS ? E.ln[q.ln] : null) || null,
                                                age: (q.ag !== undefined && QUEST_ENUMS ? E.ag[q.ag] : null) || null,
                                                area: (q.ar !== undefined && QUEST_ENUMS ? E.ar[q.ar] : null) || null } }; } },
    // Archaeology mysteries: [{site,name,points,solved,stage:{value,max}|null}].
    // stage mirrors the in-game journal's own progress dispatcher; null = a page/
    // collection-driven mystery with no stage var.
    'state.mysteries':  { scope: 'state.read', json: false,   run: async (a, pid) => {
                            const stVps = Object.values(MYST_STAGE).filter(s => s && s.vp !== undefined).map(s => s.vp);
                            let vp = {}; try { vp = JSON.parse(await bridge().varps(pid, ['9302', '9303'].concat(stVps).join(','))); } catch (e) {}
                            if (!vp || !Object.keys(vp).length) return null;
                            const vbIds = Object.keys(MYST_STAGE).map(mystStageVb).filter(v => v != null);
                            const vb = await readVarbitValues(vbIds);
                            return ARCH_MYSTERIES.flatMap(g => g.m.map(([name, varp, bit, pts]) => {
                              const st = mystStageVal(name, vb, vp);
                              return { site: g.site, name, points: pts, solved: mystDone(varp, bit, vp),
                                       stage: st ? { value: st[0], max: st[1] } : null }; })); } },
    // Both render through the in-game UI layer now (same contract: notify's second
    // arg is a ttl in ms, 0 = sticky until dismissed).
    'overlay.toast':    { scope: 'overlay',    json: false,   run: (a) => uiNotify(pClampStr(a[0], 200), { ttl: 5000 }) },
    // A floating HUD strip over the game showing ability icons (rotation playback etc.).
    // One per plugin, host-rendered from a validated payload; null/empty closes it.
    'overlay.hudAbilities': { scope: 'overlay', json: false, run: (a, pid, id) => { pluginHudSet(id, a[0]); return true; } },
    'overlay.notify':   { scope: 'overlay',    json: false,   run: (a) => uiNotify(pClampStr(a[0], 200), { ttl: pClampNum(a[1], 0, 60000) }) },
    // Big centre-screen banner text ('' clears). Same in-frame channel the Dungeoneering
    // boss warnings use.
    // a[1] = banner slot (0..2), a[2] = 0xRRGGBB accent. Slots stack upward and are independent,
    // so a plugin can hold a prayer call and a mechanic cue on screen at once instead of having
    // to rank one over the other. Both optional: omitted is the original single red banner.
    'overlay.centerText':{ scope: 'overlay',   json: false,   run: (a, pid) => (bridge().centerText ? bridge().centerText(pid, pClampStr(a[0], 80), pClampNum(a[1], 0, 2), pClampNum(a[2], -1, 0xFFFFFF)) : null) },
    // OS toast (Windows notification). Own manifest scope + a 1-per-10s rate cap.
    'notify.windows':   { scope: 'notify.os',  json: false,   run: (a) => (bridge().notifyWindows ? bridge().notifyWindows(pClampStr(a[0], 60), pClampStr(a[1], 200)) : null) },
    // Copy text to the user's clipboard. Own manifest scope; sized for plan exports.
    'clipboard.copy':   { scope: 'clipboard',  json: false,   run: (a) => (bridge().copyClipboard ? bridge().copyClipboard(pClampStr(a[0], 65536)) : null) },
    // Read-back is its own scope: reading the clipboard is more sensitive than writing it,
    // so a plugin that only exports never sees clipboard contents.
    'clipboard.paste': { scope: 'clipboard.read', json: false, run: () => String(bridge().pasteClipboard() || '').slice(0, 65536) },
    'overlay.highlight':{ scope: 'overlay',    json: false,   run: (a, pid) => bridge().overlayHighlight(pid, pClampList(a[0])) },
    // Box a single NPC by name (case-insensitive) with an optional pill label. Unlike
    // overlay.highlight (which strips punctuation), this keeps the label intact and only
    // strips '|' and ',' (protocol separators). Empty name clears the box.
    'overlay.highlightNpc':{ scope: 'overlay', json: false, run: (a, pid) => {
                            const name = pClampStr(a[0], 48).replace(/[|,]/g, '').replace(/\s+/g, ' ').trim();
                            if (!name) { try { bridge().overlayHighlight(pid, ''); } catch (e) {} return false; }
                            const label = pClampStr(a[1] == null ? '' : a[1], 96).replace(/[|,]/g, '').replace(/\s+/g, ' ').trim();
                            const tx = pClampNum(a[2], 0, 16383) | 0, ty = pClampNum(a[3], 0, 16383) | 0;   // optional: box the instance nearest THIS tile, not the player
                            let needle = (label || (tx > 0 && ty > 0)) ? (name + '|' + label) : name;
                            if (tx > 0 && ty > 0) needle += '|' + tx + ',' + ty;
                            try { bridge().overlayHighlight(pid, needle); } catch (e) {} return true; } },
    'overlay.flashGame':{ scope: 'overlay',    json: false,   run: (a, pid) => bridge().flashGame(pid) },
    // Highlight one chat-option box matching `text` (substring, both ways). The plugin owns the
    // decision of WHICH option + when (its own progress checks); the host just locates + draws it.
    'overlay.highlightOption':{ scope: 'overlay', json: false, run: async (a, pid) => {
                            const norm = s => String(s == null ? '' : s).toLowerCase().replace(/[^a-z0-9 ]+/g, ' ').replace(/\s+/g, ' ').trim();
                            const wants = (Array.isArray(a) ? a : [a]).map(x => norm(pClampStr(x, 120))).filter(Boolean);   // match an option against ANY of these texts in ONE read
                            if (!wants.length) { try { bridge().uiHighlight(pid, 0, 0, 0, 0); } catch (e) {} return false; }
                            let dlg = null; try { dlg = JSON.parse(await bridge().dialog(pid)); } catch (e) {}
                            if (dlg && dlg.hasAbs && Array.isArray(dlg.options)) {
                              for (const o of dlg.options) {
                                if ((o.text || '').toLowerCase().indexOf('<str') >= 0) continue;   // struck-through (already-picked) option -> skip to the next
                                const ot = norm(o.text);
                                if (ot && o.w > 0 && wants.some(want => ot.indexOf(want) >= 0 || want.indexOf(ot) >= 0)) {
                                  try { bridge().uiHighlight(pid, o.x, o.y, o.w, o.h); } catch (e) {}
                                  return true;
                                }
                              }
                            }
                            try { bridge().uiHighlight(pid, 0, 0, 0, 0); } catch (e) {}
                            return false; } },
    // Highlight the backpack slot holding item `id`, only if present AND visible
    // (scrolled out -> nothing drawn). Optional label a[1] replaces "Item <id>".
    'overlay.highlightItem':{ scope: 'overlay', json: false, run: async (a, pid) => {
                            const itemId = pClampId(a[0]);
                            if (!itemId) { try { bridge().panelViz(pid, ''); } catch (e) {} return false; }
                            let inv = null; try { inv = JSON.parse(await bridge().inventory(pid)); } catch (e) {}
                            let slot = -1;
                            if (inv && Array.isArray(inv.items)) for (const it of inv.items) if (it[1] === itemId) { slot = it[0]; break; }
                            if (slot < 0) { try { bridge().panelViz(pid, ''); } catch (e) {} return false; }
                            let r = null; try { r = JSON.parse(await bridge().invSlotRect(pid, slot)); } catch (e) {}
                            if (!r || !(r.w > 0)) { try { bridge().panelViz(pid, ''); } catch (e) {} return false; }
                            const label = (a[1] == null || a[1] === '') ? ('Item ' + itemId) : pClampStr(a[1], 96).replace(/[,|]/g, ' ');   // 96 = the same label budget as overlay.highlightNpc
                            try { bridge().panelViz(pid, r.x + ',' + r.y + ',' + r.w + ',' + r.h + ',' + label); } catch (e) {}
                            return true; } },
    // Mark world tiles as guide objectives: the host boxes the loc footprint at each tile (when the
    // label's FIRST line names a loc near it) + a label, and points the ground arrow at the first.
    // marks = [{x, y, plane, label}]; [] clears. Used to guide to an excavation hotspot / object.
    'overlay.guideTiles':{ scope: 'overlay', json: false, run: (a, pid) => {
                            const marks = Array.isArray(a[0]) ? a[0] : [];
                            const recs = [];
                            // 64, raised from 16. Nothing in the host required 16: GuideMarksFn
                            // parses an uncapped list, the overlay's push() is bounded by
                            // kMaxCmds (8192) and drops the overflow rather than corrupting the
                            // buffer, and the host-trusted clue guide already sends 24.
                            // The binding cost is not memory but the per-frame label placement
                            // pass in Overlay.cpp, which is O(n^2): each label is tested against
                            // every already-placed one for duplicates and again inside a
                            // 12-step collision walk. A simple tile costs roughly a dozen draw
                            // commands, so 64 marks is about 900 of the 8192 budget and a few
                            // tens of thousands of rect tests per frame -- comfortable. Going to
                            // several hundred is where that pass would start to bite, so this is
                            // deliberately a bounded raise and not a removal.
                            for (const m of marks.slice(0, 64)) {
                              if (!m || typeof m !== 'object') continue;
                              const x = pClampNum(m.x, 0, 16383) | 0, y = pClampNum(m.y, 0, 16383) | 0, plane = pClampNum(m.plane, 0, 3) | 0;
                              if (x < 1 || y < 1) continue;
                              const label = pClampStr(m.label == null ? '' : m.label, 95).replace(/[\x1e\x1f]/g, ' ');   // 95 = the overlay's kTextMax (the loc name + '\n' share the same budget)
                              let rec = x + '\x1f' + y + '\x1f' + plane + '\x1f' + label;
                              // Optional tail (field order fixed by the host parser): snap, rgb,
                              // areaX2, areaY2, region, rgb2. Emitted whenever a colour or an
                              // AREA extent (x/y = SW corner, x2/y2 = NE corner: one flat ground
                              // rect spanning the tiles) is present. color/color2 accept
                              // '#rrggbb' or a packed int; color2 makes a two-tone tile split
                              // diagonally, exactly like two-tone user markers.
                              const x2 = pClampNum(m.x2, 0, 16383) | 0, y2 = pClampNum(m.y2, 0, 16383) | 0;
                              const hasArea = x2 >= x && y2 >= y && (x2 > x || y2 > y);
                              const pcol = (v) => { if (typeof v === 'number') return v & 0xFFFFFF;
                                if (typeof v === 'string') { const n = parseInt(String(v).replace('#', ''), 16); return isNaN(n) ? 0 : (n & 0xFFFFFF); }
                                return 0; };
                              const col = pcol(m.color), col2 = pcol(m.color2);
                              if (hasArea || col || col2)
                                rec += '\x1f0\x1f' + col + '\x1f' + (hasArea ? x2 : 0) + '\x1f' + (hasArea ? y2 : 0) + '\x1f0\x1f' + col2;
                              recs.push(rec);
                            }
                            return bridge().guideMarks(pid, recs.join('\x1e')); } },
    'overlay.clearHighlight':{ scope: 'overlay', json: false, run: (a, pid) => {
                            try { bridge().uiHighlight(pid, 0, 0, 0, 0); } catch (e) {}
                            try { bridge().panelViz(pid, ''); } catch (e) {}
                            return true; } },
    // Highlight an arbitrary screen rect (e.g. a component rect from state.interface -- the continue
    // button, a slot, etc.). w/h <= 0 clears. Overlay-only; the plugin computes the rect.
    // Several boxes at once. Array of [x,y,w,h] (or {x,y,w,h}); an empty array clears. The set
    // REPLACES the previous one, and it is shared with highlightRect - there is one highlight
    // set per client, not one per plugin.
    'overlay.highlightRects':{ scope: 'overlay', json: false, run: (a, pid) => {
                            const list = Array.isArray(a[0]) ? a[0] : [];
                            const recs = [];
                            for (const r of list.slice(0, 64)) {
                              const q = Array.isArray(r) ? r : [r && r.x, r && r.y, r && r.w, r && r.h];
                              const x = pClampNum(q[0], -9999, 99999), y = pClampNum(q[1], -9999, 99999);
                              const w = pClampNum(q[2], 0, 9999), h = pClampNum(q[3], 0, 9999);
                              if (w > 0 && h > 0) recs.push(x + ',' + y + ',' + w + ',' + h);
                            }
                            try { bridge().uiHighlights(pid, recs.join(';')); } catch (e) {}
                            return recs.length > 0; } },
    'overlay.highlightRect':{ scope: 'overlay', json: false, run: (a, pid) => {
                            const x = pClampNum(a[0], -9999, 99999), y = pClampNum(a[1], -9999, 99999);
                            const w = pClampNum(a[2], 0, 9999), h = pClampNum(a[3], 0, 9999);
                            if (w > 0 && h > 0) { try { bridge().uiHighlight(pid, x, y, w, h); } catch (e) {} return true; }
                            try { bridge().uiHighlight(pid, 0, 0, 0, 0); } catch (e) {} return false; } },
    'sound.play':       { scope: 'sound',      json: false,   run: (a, pid) => bridge().playSound(pClampStr(a[0], 64)) },
    // Open the wiki-locked in-client browser on a search term (empty = wiki home). A
    // visual surface, so it shares the overlay scope and rate bucket.
    'overlay.wikiSearch':{ scope: 'overlay',   json: false,   run: (a, pid) => (bridge().wikiOpen ? bridge().wikiOpen(pid, pClampStr(a[0], 200)) : null) },
    'storage.get':      { scope: 'storage',    json: 'maybe', run: (a, pid, id) => bridge().pluginStoreLoad(pid, id, pClampStr(a[0], 64)) },
    'storage.set':      { scope: 'storage',    json: false,   run: (a, pid, id) => bridge().pluginStoreSave(pid, id, pClampStr(a[0], 64), JSON.stringify(a[1] === undefined ? null : a[1])) },
    'storage.keys':     { scope: 'storage',    json: true,    run: (a, pid, id) => bridge().pluginStoreKeys(pid, id) },
    // ===== Panel bindings promoted 2026-08-29 (docs/panel-plugin-migration.md, "rtxData") =====
    // One-liners over the existing bindings; args are forwarded as given (pArgs: <=8 args, strings
    // capped at 64K) so argc reaches the host unchanged. Method names are <ns>.<binding>.
    // Scopes actuator / host / solver / party / chat.read are NOT in PLUGIN_SCOPES: a manifest cannot
    // request them and the grant UI never offers them, so plugins cannot reach these; in-tree
    // panels reach them through rtxData (core/rtx-data.js). actuator = acts on the game, the
    // host or the filesystem. host = host-private stores and diagnostics.
    // -- state (state.read)
    // CSV twin of state.varbits: {"<id>":value} straight from the reader (no cache resolve), same shape as state.varps
    'state.varbitsCsv':       { scope: 'state.read',  json: true,  run: (a, pid) => bridge().varbits(pid, ...pArgs(a)) },
    'state.varcsAll':         { scope: 'state.read',  json: true,  run: (a, pid) => bridge().varcsDumpAll(pid, ...pArgs(a)) },
    'state.varpsAll':         { scope: 'state.read',  json: true,  run: (a, pid) => bridge().varpsDumpAll(pid, ...pArgs(a)) },
    'state.varcStringsAll':   { scope: 'state.read',  json: true,  run: (a, pid) => bridge().varcStringsDumpAll(pid, ...pArgs(a)) },
    'state.varpsLong':        { scope: 'state.read',  json: true,  run: (a, pid) => bridge().varpsLong(pid, ...pArgs(a)) },
    'state.dialog':           { scope: 'state.read',  json: true,  run: (a, pid) => bridge().dialog(pid, ...pArgs(a)) },
    'state.gameTickState':    { scope: 'state.read',  json: true,  run: (a, pid) => bridge().gameTickState(pid, ...pArgs(a)) },
    'state.hoverEntity':      { scope: 'state.read',  json: true,  run: (a, pid) => bridge().hoverEntity(pid, ...pArgs(a)) },
    'state.interfaceGroups':  { scope: 'state.read',  json: true,  run: (a, pid) => bridge().interfaceGroups(pid, ...pArgs(a)) },
    'state.invSlotRect':      { scope: 'state.read',  json: true,  run: (a, pid) => bridge().invSlotRect(pid, ...pArgs(a)) },
    'state.ifaceCompRects':   { scope: 'state.read',  json: true,  run: (a, pid) => bridge().ifaceCompRects(pid, ...pArgs(a)) },
    'state.interfaceSizeSearch':{ scope: 'state.read',  json: true,  run: (a, pid) => bridge().interfaceSizeSearch(pid, ...pArgs(a)) },
    'state.openContainers':   { scope: 'state.read',  json: true,  run: (a, pid) => bridge().openContainers(pid, ...pArgs(a)) },
    'state.pof':              { scope: 'state.read',  json: true,  run: (a, pid) => bridge().pof(pid, ...pArgs(a)) },
    'state.gameFocused':      { scope: 'state.read',  json: false, run: (a, pid) => bridge().gameFocused(pid, ...pArgs(a)) },
    // -- cache (cache.read)
    'cache.achievements':     { scope: 'cache.read',  json: true,  run: (a) => bridge().achievements(...pArgs(a)) },
    'cache.abilityConfigs':   { scope: 'cache.read',  json: true,  run: (a) => bridge().abilityConfigs(...pArgs(a)) },
    // Plain-text tooltip bullets per ability id, flattened once from AB_TIPS (the interpreted
    // CS2 tooltip builders panel_abilities renders). Damage placeholders become "a%-b% damage".
    'cache.abilityTips':      { scope: 'cache.read',  json: true,  run: () => pluginAbilityTipsJson() },
    'cache.archResearch':     { scope: 'cache.read',  json: true,  run: (a) => bridge().archResearch(...pArgs(a)) },
    'cache.quests':           { scope: 'cache.read',  json: true,  run: (a) => bridge().quests(...pArgs(a)) },
    // sync, capped at 2000 rows host-side
    'cache.dbRows':           { scope: 'cache.read',  json: true,  run: (a) => bridge().dbRows(...pArgs(a)) },
    'cache.npcInfo':          { scope: 'cache.read',  json: true,  run: (a) => bridge().npcInfo(...pArgs(a)) },
    'cache.cs2Names':         { scope: 'cache.read',  json: true,  run: (a) => bridge().cs2Names(...pArgs(a)) },
    'cache.mystPages':        { scope: 'cache.read',  json: true,  run: (a) => bridge().mystPages(...pArgs(a)) },
    'cache.mapAreas':         { scope: 'cache.read',  json: true,  run: (a) => bridge().mapAreas(...pArgs(a)) },
    'cache.mapCategories':    { scope: 'cache.read',  json: false, run: (a) => bridge().mapCategories(...pArgs(a)) },
    'cache.mapLabels':        { scope: 'cache.read',  json: false, run: (a) => bridge().mapLabels(...pArgs(a)) },
    'cache.mapLocNames':      { scope: 'cache.read',  json: false, run: (a) => bridge().mapLocNames(...pArgs(a)) },
    'cache.mapSymbols':       { scope: 'cache.read',  json: false, run: (a) => bridge().mapSymbols(...pArgs(a)) },
    'cache.spriteByName':     { scope: 'cache.read',  json: false, run: (a) => bridge().spriteByName(...pArgs(a)) },
    'cache.ifaceGroup':       { scope: 'cache.read',  json: true,  run: (a) => bridge().cacheIfaceGroup(...pArgs(a)) },
    'cache.isLeaguesWorld':   { scope: 'cache.read',  json: false, run: (a) => bridge().isLeaguesWorld(...pArgs(a)) },
    'cache.buffCatalog':      { scope: 'cache.read',  json: false, run: (a) => bridge().buffCatalog(...pArgs(a)) },
    // -- overlay (overlay)
    'overlay.guideMarks':     { scope: 'overlay',     json: false, run: (a, pid) => bridge().guideMarks(pid, ...pArgs(a)) },
    'overlay.panelViz':       { scope: 'overlay',     json: false, run: (a, pid) => bridge().panelViz(pid, ...pArgs(a)) },
    'overlay.uiHighlight':    { scope: 'overlay',     json: false, run: (a, pid) => bridge().uiHighlight(pid, ...pArgs(a)) },
    'overlay.hudSprite':      { scope: 'overlay',     json: false, run: (a, pid) => bridge().hudSprite(pid, ...pArgs(a)) },
    'overlay.outlineObject':  { scope: 'overlay',     json: false, run: (a, pid) => bridge().outlineObject(pid, ...pArgs(a)) },
    // -- chat (chat.read)
    // chat.read is NOT in PLUGIN_SCOPES (PMs, friends/clan chat need their own consent label), so only the panel path reaches it
    'chat.messages':          { scope: 'chat.read',   json: true,  run: (a, pid) => bridge().chat(pid, ...pArgs(a)) },
    // -- solver (solver)
    'solver.puzzleCells':     { scope: 'solver',      json: false, run: (a, pid) => bridge().puzzleCells(pid, ...pArgs(a)) },
    'solver.puzzleCellRects': { scope: 'solver',      json: true,  run: (a, pid) => bridge().puzzleCellRects(pid, ...pArgs(a)) },
    'solver.puzzleState':     { scope: 'solver',      json: true,  run: (a, pid) => bridge().puzzleState(pid, ...pArgs(a)) },
    'solver.knotCells':       { scope: 'solver',      json: false, run: (a, pid) => bridge().knotCells(pid, ...pArgs(a)) },
    'solver.scanSolution':    { scope: 'solver',      json: true,  run: (a, pid) => bridge().scanSolution(pid, ...pArgs(a)) },
    'solver.clueSearchTarget':{ scope: 'solver',      json: true,  run: (a) => bridge().clueSearchTarget(...pArgs(a)) },
    'solver.compassHeading':  { scope: 'solver',      json: false, run: (a, pid) => bridge().compassHeading(pid, ...pArgs(a)) },
    'solver.compassTarget':   { scope: 'solver',      json: false, run: (a, pid) => bridge().compassTarget(pid, ...pArgs(a)) },
    // -- party (party)
    'party.partyData':        { scope: 'party',       json: true,  run: (a) => bridge().partyData(...pArgs(a)) },
    'party.partyGetCode':     { scope: 'party',       json: false, run: (a) => bridge().partyGetCode(...pArgs(a)) },
    'party.partySetCode':     { scope: 'party',       json: false, run: (a) => bridge().partySetCode(...pArgs(a)) },
    'party.partyReport':      { scope: 'party',       json: false, run: (a) => bridge().partyReport(...pArgs(a)) },
    // -- host (host)
    'host.alertsLoad':        { scope: 'host',        json: false, run: (a, pid) => bridge().alertsLoad(pid, ...pArgs(a)) },
    'host.counterLoad':       { scope: 'host',        json: false, run: (a, pid) => bridge().counterLoad(pid, ...pArgs(a)) },
    'host.notesLoad':         { scope: 'host',        json: false, run: (a, pid) => bridge().notesLoad(pid, ...pArgs(a)) },
    'host.sidebarLoad':       { scope: 'host',        json: false, run: (a, pid) => bridge().sidebarLoad(pid, ...pArgs(a)) },
    'host.mystLoad':          { scope: 'host',        json: true,  run: (a, pid) => bridge().mystLoad(pid, ...pArgs(a)) },
    'host.questLoad':         { scope: 'host',        json: true,  run: (a, pid) => bridge().questLoad(pid, ...pArgs(a)) },
    'host.menuRulesLoad':     { scope: 'host',        json: true,  run: (a) => bridge().menuRulesLoad(...pArgs(a)) },
    'host.menuStatus':        { scope: 'host',        json: true,  run: (a, pid) => bridge().menuStatus(pid, ...pArgs(a)) },
    'host.menuSearch':        { scope: 'host',        json: true,  run: (a) => bridge().menuSearch(...pArgs(a)) },
    'host.markersGet':        { scope: 'host',        json: true,  run: (a, pid) => bridge().markersGet(pid, ...pArgs(a)) },
    'host.markersVersion':    { scope: 'host',        json: false, run: (a, pid) => bridge().markersVersion(pid, ...pArgs(a)) },
    'host.markerKeybindsGet': { scope: 'host',        json: true,  run: (a) => bridge().markerKeybindsGet(...pArgs(a)) },
    'host.screenshotKeybindGet':{ scope: 'host',        json: false, run: (a) => bridge().screenshotKeybindGet(...pArgs(a)) },
    'host.serverPacketFeed':  { scope: 'host',        json: true,  run: (a, pid) => bridge().serverPacketFeed(pid, ...pArgs(a)) },
    'host.serverPackets':     { scope: 'host',        json: true,  run: (a, pid) => bridge().serverPackets(pid, ...pArgs(a)) },
    'host.soundFilterStatus': { scope: 'host',        json: true,  run: (a, pid) => bridge().soundFilterStatus(pid, ...pArgs(a)) },
    'host.soundList':         { scope: 'host',        json: true,  run: (a) => bridge().soundList(...pArgs(a)) },
    'host.soundStatus':       { scope: 'host',        json: true,  run: (a) => bridge().soundStatus(...pArgs(a)) },
    'host.varPinsLoad':       { scope: 'host',        json: true,  run: (a) => bridge().varPinsLoad(...pArgs(a)) },
    'host.varsDump':          { scope: 'host',        json: true,  run: (a, pid) => bridge().varsDump(pid, ...pArgs(a)) },
    'host.vosCached':         { scope: 'host',        json: true,  run: (a) => bridge().vosCached(...pArgs(a)) },
    'host.obeliskCached':     { scope: 'host',        json: false, run: (a) => bridge().obeliskCached(...pArgs(a)) },
    'host.scarabCached':      { scope: 'host',        json: false, run: (a) => bridge().scarabCached(...pArgs(a)) },
    'host.cacheStoreLoad':    { scope: 'host',        json: false, run: (a) => bridge().cacheStoreLoad(...pArgs(a)) },
    'host.xpPanelState':      { scope: 'host',        json: false, run: (a, pid) => bridge().xpPanelState(pid, ...pArgs(a)) },
    'host.readerHealth':      { scope: 'host',        json: true,  run: (a, pid) => bridge().readerHealth(pid, ...pArgs(a)) },
    'cache.iconSource':   { scope: 'cache.read', json: false, run: (a) => bridge().iconSource(pClampId(a[0])) },   // pack | rendered | none
    'host.iconMisses':        { scope: 'host',        json: true,  run: () => bridge().iconMisses() },
    'host.iconCoverage':      { scope: 'host',        json: true,  run: () => bridge().iconCoverage() },
    'host.uiAsset':           { scope: 'host',        json: false, run: (a) => bridge().uiAsset(...pArgs(a)) },
    'host.hiscores':          { scope: 'host',        json: true,  run: (a) => bridge().hiscores(...pArgs(a)) },
    // -- act (actuator)
    'act.alertsSave':         { scope: 'actuator',    json: false, run: (a, pid) => bridge().alertsSave(pid, ...pArgs(a)) },
    'act.counterSave':        { scope: 'actuator',    json: false, run: (a, pid) => bridge().counterSave(pid, ...pArgs(a)) },
    'act.notesSave':          { scope: 'actuator',    json: false, run: (a, pid) => bridge().notesSave(pid, ...pArgs(a)) },
    'act.sidebarSave':        { scope: 'actuator',    json: false, run: (a, pid) => bridge().sidebarSave(pid, ...pArgs(a)) },
    'act.mystSave':           { scope: 'actuator',    json: false, run: (a, pid) => bridge().mystSave(pid, ...pArgs(a)) },
    'act.questSave':          { scope: 'actuator',    json: false, run: (a, pid) => bridge().questSave(pid, ...pArgs(a)) },
    'act.menuRulesSave':      { scope: 'actuator',    json: false, run: (a) => bridge().menuRulesSave(...pArgs(a)) },
    'act.menuEnable':         { scope: 'actuator',    json: false, run: (a, pid) => bridge().menuEnable(pid, ...pArgs(a)) },
    'act.menuPins':           { scope: 'actuator',    json: false, run: (a, pid) => bridge().menuPins(pid, ...pArgs(a)) },
    'act.markerAdd':          { scope: 'actuator',    json: false, run: (a, pid) => bridge().markerAdd(pid, ...pArgs(a)) },
    'act.markerRemove':       { scope: 'actuator',    json: false, run: (a, pid) => bridge().markerRemove(pid, ...pArgs(a)) },
    'act.markerSetColor':     { scope: 'actuator',    json: false, run: (a, pid) => bridge().markerSetColor(pid, ...pArgs(a)) },
    'act.markerSetLabel':     { scope: 'actuator',    json: false, run: (a, pid) => bridge().markerSetLabel(pid, ...pArgs(a)) },
    'act.markersClear':       { scope: 'actuator',    json: false, run: (a, pid) => bridge().markersClear(pid, ...pArgs(a)) },
    'act.markerKeybindsSet':  { scope: 'actuator',    json: false, run: (a) => bridge().markerKeybindsSet(...pArgs(a)) },
    'act.screenshotKeybindSet':{ scope: 'actuator',    json: false, run: (a) => bridge().screenshotKeybindSet(...pArgs(a)) },
    'act.captureScreenshot':  { scope: 'actuator',    json: false, run: (a, pid) => bridge().captureScreenshot(pid, ...pArgs(a)) },
    'act.openScreenshots':    { scope: 'actuator',    json: false, run: (a) => bridge().openScreenshots(...pArgs(a)) },
    'act.renderToggle':       { scope: 'actuator',    json: false, run: (a, pid) => bridge().renderToggle(pid, ...pArgs(a)) },
    'act.serverPacketArm':    { scope: 'actuator',    json: false, run: (a, pid) => bridge().serverPacketArm(pid, ...pArgs(a)) },
    'host.eventsMask':        { scope: 'actuator',    json: false, run: (a, pid) => bridge().eventsMask(pid, pClampStr(a[0], 2000)) },
    'act.soundExport':        { scope: 'actuator',    json: false, run: (a) => bridge().soundExport(...pArgs(a)) },
    'act.soundFilterEnable':  { scope: 'actuator',    json: false, run: (a, pid) => bridge().soundFilterEnable(pid, ...pArgs(a)) },
    'act.soundMute':          { scope: 'actuator',    json: false, run: (a, pid) => bridge().soundMute(pid, ...pArgs(a)) },
    'act.soundPause':         { scope: 'actuator',    json: false, run: (a) => bridge().soundPause(...pArgs(a)) },
    'act.soundPlay':          { scope: 'actuator',    json: false, run: (a) => bridge().soundPlay(...pArgs(a)) },
    'act.soundResume':        { scope: 'actuator',    json: false, run: (a) => bridge().soundResume(...pArgs(a)) },
    'act.soundSeek':          { scope: 'actuator',    json: false, run: (a) => bridge().soundSeek(...pArgs(a)) },
    'act.soundStop':          { scope: 'actuator',    json: false, run: (a) => bridge().soundStop(...pArgs(a)) },
    'act.soundVolume':        { scope: 'actuator',    json: false, run: (a) => bridge().soundVolume(...pArgs(a)) },
    'act.varPinsSave':        { scope: 'actuator',    json: false, run: (a) => bridge().varPinsSave(...pArgs(a)) },
    'act.varsWatch':          { scope: 'actuator',    json: false, run: (a, pid) => bridge().varsWatch(pid, ...pArgs(a)) },
    'act.vosReport':          { scope: 'actuator',    json: false, run: (a) => bridge().vosReport(...pArgs(a)) },
    'act.worldEventVote':     { scope: 'actuator',    json: false, run: (a) => bridge().worldEventVote(...pArgs(a)) },
    'act.cacheStoreSave':     { scope: 'actuator',    json: false, run: (a) => bridge().cacheStoreSave(...pArgs(a)) },
    'act.xpPanelReset':       { scope: 'actuator',    json: false, run: (a, pid) => bridge().xpPanelReset(pid, ...pArgs(a)) },
    'act.ifaceOffset':        { scope: 'actuator',    json: false, run: (a) => bridge().ifaceOffset(...pArgs(a)) },
    'ui.setHeight':     { scope: null,         json: false,   run: (a, pid, id, fr) => { if (fr) fr.style.height = pClampNum(a[0], 60, 4000) + 'px'; return true; } },
    'ui.setTitle':      { scope: null,         json: false,   run: () => true },
    // Declare the plugin's settings schema; returns the current values. Host-mediated,
    // so no scope is needed: the plugin only ever sees its own values.
    'ui.settings':      { scope: null,         json: false,   run: async (a, pid, id) => {
      const schema = pluginSettingsSchema(a[0]);
      if (!schema) throw new Error('invalid settings schema');
      let stored = null;
      try { stored = JSON.parse(await bridge().pluginStoreLoad(pid, id, '~settings') || 'null'); } catch (e) {}
      const values = {};
      for (const c of schema) values[c.key] = pluginSettingClamp(c, (stored && stored[c.key] !== undefined) ? stored[c.key] : c.def);
      const tab = pluginTabs.find(p => p.id === id);
      pluginSettingsReg.set(id, { name: (tab && tab.manifest.name) || id, schema, values });
      try { if (typeof uisBuildPluginSettings === 'function') uisBuildPluginSettings(); } catch (e) {}
      const m = pluginMounts.get(id);
      if (m) pluginSendEvent(m, 'settings', Object.assign({}, values));
      return Object.assign({}, values);
    } },
    'settings.get':     { scope: null,         json: false,   run: (a, pid, id) => {
      const reg = pluginSettingsReg.get(id);
      return reg ? Object.assign({}, reg.values) : {};
    } }
  };

  function pluginRateOk(id, method) {
    // rate = tokens refilled per second; capacity = max burst (>=1 so slow rates still fire).
    const rate = method.indexOf('storage.') === 0 ? 4
               : method.indexOf('overlay.') === 0 ? 6
               : method.indexOf('notify.') === 0 ? 0.1     // OS toasts: 1 per 10s
               : method.indexOf('clipboard.') === 0 ? 1
               : 20;
    const cap = Math.max(1, rate);
    const k = id + '|' + method, now = Date.now();
    let b = pluginBuckets[k];
    if (!b) { b = { t: cap, ts: now }; pluginBuckets[k] = b; }
    b.t = Math.min(cap, b.t + ((now - b.ts) / 1000) * rate); b.ts = now;
    if (b.t < 1) return false;
    b.t -= 1; return true;
  }

  function pluginSendEvent(m, event, data) {
    if (!m || !m.frame || !m.frame.contentWindow) return;
    try { m.frame.contentWindow.postMessage({ __rtxPlugin: PLUGIN_PROTO, kind: 'event', event, data }, '*'); } catch (e) {}
  }

  // Push the host poll cadence to every mounted plugin (no free-polling in plugins).
  // Game events collected by rtxEvents since the last push ride along as ONE 'events'
  // batch per plugin with state.read; `tick` stays the 4 Hz heartbeat it always was.
  const pluginEventQueue = [];
  function pluginPush() {
    const batch = pluginEventQueue.length ? pluginEventQueue.splice(0) : null;
    for (const m of pluginMounts.values()) {
      if (!m.frame) continue;
      pluginSendEvent(m, 'tick', null);
      if (m.scopes.indexOf('state.read') !== -1) {
        pluginSendEvent(m, 'state', lastSnap);
        if (batch) pluginSendEvent(m, 'events', batch);
      }
    }
  }

  // ---- plugin ability HUD: a small floating strip over the game, host-rendered ----
  // Payload: { title, sub, cur:[{id,key}], next:[{id,gap}] }. Lives in the in-game UI layer
  // (this page composites into the game frame), draggable by its header, one per plugin.
  const pluginHuds = new Map();          // plugin id -> { el, body, ttl, sub }
  // Dragged position, kept across close/recreate: an empty tick or a loop wrap closes the
  // strip and the next push rebuilds it, which must not teleport it back to the default spot.
  const pluginHudPos = new Map();        // plugin id -> { left, top }
  const pluginHudIcons = new Map();      // ability id -> data URL promise result ('' = none)
  function pluginHudIcon(id, el) {
    if (pluginHudIcons.has(id)) { const u = pluginHudIcons.get(id); if (u) el.style.backgroundImage = 'url(' + u + ')'; return; }
    try {
      Promise.resolve(bridge().sprite(id)).then(u => {
        pluginHudIcons.set(id, u || '');
        if (u && el.isConnected) el.style.backgroundImage = 'url(' + u + ')';
      }).catch(() => pluginHudIcons.set(id, ''));
    } catch (e) {}
  }
  function pluginHudClose(slug) {
    const h = pluginHuds.get(slug);
    if (h) { try { h.el.remove(); } catch (e) {} pluginHuds.delete(slug); wmRectsSoon(); }
  }
  function pluginHudSet(slug, payload) {
    const cur = payload && Array.isArray(payload.cur) ? payload.cur.slice(0, 6) : [];
    // Only an explicit null closes the strip. An empty tick (nothing firing right now, e.g.
    // the tail before a loop wraps) keeps it alive showing the title/sub and upcoming icons,
    // instead of blinking out and back.
    if (!payload) { pluginHudClose(slug); return; }
    let h = pluginHuds.get(slug);
    if (!h) {
      const el = document.createElement('div');
      el.style.cssText = 'position:fixed;z-index:950000;top:110px;left:50%;transform:translateX(-50%);' +
        'background:rgba(12,13,18,0.92);border:1px solid rgba(140,111,253,0.45);border-radius:10px;' +
        'padding:6px 10px 8px;min-width:180px;box-shadow:0 8px 24px rgba(0,0,0,0.5);user-select:none';
      const p0 = pluginHudPos.get(slug);
      if (p0) {
        el.style.transform = 'none';
        el.style.left = Math.max(0, Math.min(p0.left, (window.innerWidth || 1280) - 60)) + 'px';
        el.style.top  = Math.max(0, Math.min(p0.top,  (window.innerHeight || 720) - 40)) + 'px';
      }
      const head = document.createElement('div');
      head.style.cssText = 'display:flex;align-items:center;gap:8px;cursor:grab;margin-bottom:5px';
      const ttl = document.createElement('span');
      ttl.style.cssText = 'flex:1;font-size:11px;font-weight:700;color:#e8edf4;white-space:nowrap;overflow:hidden;text-overflow:ellipsis';
      const sub = document.createElement('span');
      sub.style.cssText = 'font-size:10px;color:#99a0b3;white-space:nowrap';
      const x = document.createElement('span');
      x.textContent = '\u00d7';
      x.style.cssText = 'cursor:pointer;color:#99a0b3;font-size:13px;padding:0 2px';
      x.addEventListener('click', () => pluginHudClose(slug));
      head.appendChild(ttl); head.appendChild(sub); head.appendChild(x);
      const body = document.createElement('div');
      body.style.cssText = 'display:flex;align-items:center;gap:5px';
      el.appendChild(head); el.appendChild(body);
      // header drag (screen px both sides; this layer is unzoomed)
      head.addEventListener('mousedown', (e) => {
        if (e.target === x) return;
        e.preventDefault();
        const r = el.getBoundingClientRect();
        el.style.transform = 'none'; el.style.left = r.left + 'px'; el.style.top = r.top + 'px';
        const sx = e.clientX - r.left, sy = e.clientY - r.top;
        const mv = ev => { el.style.left = Math.max(0, ev.clientX - sx) + 'px'; el.style.top = Math.max(0, ev.clientY - sy) + 'px'; wmPushRects(); };
        const up = () => {
          document.removeEventListener('mousemove', mv); document.removeEventListener('mouseup', up);
          pluginHudPos.set(slug, { left: parseFloat(el.style.left) || 0, top: parseFloat(el.style.top) || 0 });
          wmPushRects();
        };
        document.addEventListener('mousemove', mv); document.addEventListener('mouseup', up);
      });
      document.body.appendChild(el);
      h = { el, body, ttl, sub };
      pluginHuds.set(slug, h);
    }
    wmRectsSoon();   // size follows the payload (icon count), so refresh the consume rect every set
    h.ttl.textContent = pClampStr(payload.title, 40) || 'Rotation';
    h.sub.textContent = pClampStr(payload.sub, 60);
    h.body.innerHTML = '';
    const isNow = payload.now !== false;   // absent = old plugin builds -> green as before
    const tile = (id, big, key, dim) => {
      const d = document.createElement('div');
      const sz = big ? 44 : 26;
      d.style.cssText = 'position:relative;width:' + sz + 'px;height:' + sz + 'px;border-radius:6px;flex:none;' +
        'border:1px solid ' + (big ? (isNow ? 'rgba(77,210,138,0.8)' : 'rgba(245,178,65,0.75)') : 'rgba(255,255,255,0.12)') + ';' +
        'background:#20222c center/' + (sz - 6) + 'px no-repeat;' + (dim ? 'opacity:0.62;' : '');
      pluginHudIcon(pClampId(id), d);
      if (key) {
        const k = document.createElement('span');
        k.textContent = pClampStr(key, 6);
        k.style.cssText = 'position:absolute;bottom:-3px;right:-3px;background:#0c0d12;border:1px solid rgba(255,255,255,0.18);' +
          'border-radius:4px;font-size:10px;font-weight:700;padding:0 3px;color:#9d83ff';
        d.appendChild(k);
      }
      return d;
    };
    for (const it of cur) h.body.appendChild(tile(it && it.id, true, it && it.key, false));
    const next = Array.isArray(payload.next) ? payload.next.slice(0, 6) : [];
    for (const it of next) {
      const gap = document.createElement('span');
      gap.textContent = '+' + Math.max(0, Math.min(99, (it && it.gap) | 0));
      gap.style.cssText = 'font-size:9px;color:#5e6580;flex:none';
      h.body.appendChild(gap);
      h.body.appendChild(tile(it && it.id, false, '', true));
    }
  }

  let _pluginTipsJson = null;
  function pluginAbilityTipsJson() {
    if (_pluginTipsJson) return _pluginTipsJson;
    const out = {};
    const tips = (typeof AB_TIPS !== 'undefined') ? AB_TIPS : {};
    const clean = t => String(t).replace(/<col=[0-9a-fA-F]+>/g, '').replace(/<\/col>/g, '')
                                .replace(/<sprite=\d+>/g, '').replace(/<nbsp>/g, ' ')
                                .replace(/<br>/g, ' ').replace(/\s+/g, ' ').trim();
    for (const id in tips) {
      const rec = tips[id];
      if (!rec || !Array.isArray(rec.l)) continue;
      const lines = [];
      for (const parts of rec.l) {
        let ln = '';
        for (const p of parts) {
          if (typeof p === 'string') ln += p;
          else if (p && typeof p === 'object') ln += (p.a != null ? p.a + '%-' + p.b + '% damage' : '');
        }
        ln = clean(ln);
        if (ln) lines.push(ln);
        if (lines.length >= 10) break;
      }
      if (lines.length) out[id] = lines;
    }
    _pluginTipsJson = JSON.stringify(out);
    return _pluginTipsJson;
  }

  function pluginBrokerInit() {
    // rtxEvents (core/rtx-data.js) loads after this file, so subscribe here, at attach time.
    rtxEvents.on('*', function (ev) { if (pluginMounts.size && pluginEventQueue.length < 4096) pluginEventQueue.push(ev); });
    window.addEventListener('message', async (ev) => {
      const m = ev.data;
      if (!m || m.__rtxPlugin !== PLUGIN_PROTO) return;
      // Route by SOURCE frame: several plugin windows can be mounted at once, and
      // an unknown source is ignored exactly like before.
      let mounted = null;
      for (const pm of pluginMounts.values())
        if (pm.frame && ev.source === pm.frame.contentWindow) { mounted = pm; break; }
      if (!mounted) return;
      if (m.kind === 'hello') {
        pluginSendEvent(mounted, 'ready', { scopes: mounted.scopes, apiVersion: '1.0', pluginId: mounted.id });
        return;
      }
      if (m.kind === 'kb') {
        // A text field inside the sandboxed frame gained/lost focus (the SDK shim watches;
        // this page cannot see across the opaque origin). Refcounted per mount so several
        // open plugins cannot unbalance each other.
        if (!!m.on !== !!mounted.kbFocus) { mounted.kbFocus = !!m.on; kbGrab(mounted.kbFocus); }
        return;
      }
      if (m.kind !== 'call') return;
      const reply = (ok, payload) => {
        try {
          mounted.frame.contentWindow.postMessage({
            __rtxPlugin: PLUGIN_PROTO, kind: 'reply', id: m.id, ok,
            result: ok ? payload : undefined, error: ok ? undefined : String(payload)
          }, '*');
        } catch (e) {}
      };
      try {
        const def = PLUGIN_API[m.method];
        if (!def) return reply(false, 'unknown method');
        if (def.scope && mounted.scopes.indexOf(def.scope) === -1) return reply(false, 'scope not granted: ' + def.scope);
        if (!pluginRateOk(mounted.id, m.method)) return reply(false, 'rate limited');
        if (!bridge()) return reply(false, 'host unavailable');
        const args = Array.isArray(m.args) ? m.args : [];
        const raw = await def.run(args, myPid(), mounted.id, mounted.frame);
        let result = raw;
        if (def.json === true) { try { result = JSON.parse(raw); } catch (e) { result = null; } }
        else if (def.json === 'maybe') { try { result = raw ? JSON.parse(raw) : null; } catch (e) { result = null; } }
        reply(true, result);
      } catch (e) { reply(false, 'call failed'); }
    });
  }

  // ---- plugin permission grants ----
  // PER ACCOUNT, and deliberately not migrated from the old machine-wide localStorage key.
  // Consent is a security decision about "this plugin may read this character's data", and
  // the old store answered it once for every character on the machine. Carrying those grants
  // forward would re-create exactly that: silent authorisation on accounts that never
  // consented. Users consent once more per character instead, which is the point.
  //
  // Grants live in memory for the session as well (localStorage was unreliable in the
  // LoadHTML panel, and the per-account file cannot be written until the character name
  // resolves), so a grant is never lost mid-session just because the store is not ready.
  let pluginGrants = Object.create(null);   // plugin id -> granted scope array
  // The account the in-memory map belongs to. NOT the pid: logging into a different
  // character reuses the same client (same pid) while changing which account's grants apply,
  // so a pid check would happily keep the previous character's consents in memory.
  let pluginGrantsAcct = null;              // null = never bound to an account yet
  let _pgSaveT = 0, _pgSaveTries = 0, _pgLoading = false;
  function pluginAcctKey() { return (lastSnap && lastSnap.display_name) || ''; }
  async function pluginGrantsEnsure() {
    const acct = pluginAcctKey();
    if (!acct || _pgLoading || pluginGrantsAcct === acct) return;
    _pgLoading = true;
    let loaded = null;
    try {
      const s = await bridge().pluginGrantsLoad(myPid());
      const v = JSON.parse(s || '{}');
      if (v && typeof v === 'object') loaded = v;
    } catch (e) {}
    _pgLoading = false;
    if (!loaded) return;                    // store not readable yet: retry on the next call
    const first = (pluginGrantsAcct === null);
    const merged = Object.create(null);
    for (const k in loaded) if (Array.isArray(loaded[k])) merged[k] = loaded[k];
    // Only on the FIRST binding do session grants survive: those were made moments earlier by
    // this user, before the character name resolved, and dropping them would re-prompt for a
    // consent just given. On an account CHANGE the in-memory map belongs to the previous
    // character and must not carry over -- that is the whole bug being fixed.
    if (first) for (const k in pluginGrants) merged[k] = pluginGrants[k];
    pluginGrants = merged;
    pluginGrantsAcct = acct;
    if (first) pluginGrantsSaveSoon();
    // A different character's grants are now in force, so anything mounted under the old
    // ones must be re-evaluated rather than left running.
    if (!first) for (const w of wm.wins.values())
      if (String(w.tab).indexOf('plugin:') === 0) { pluginUnmount(w.tab.slice(7)); renderPaneFor(w); }
  }
  function pluginGrantsSaveSoon(delay) {
    if (_pgSaveT) return;
    _pgSaveT = setTimeout(() => {
      _pgSaveT = 0;
      let ok = false;
      try { ok = !!bridge().pluginGrantsSave(myPid(), JSON.stringify(pluginGrants)); } catch (e) {}
      // Refused until the account resolves (the file is per-character). Retry rather than
      // silently losing the consent, backing off to 2s so we are not spinning every 400ms.
      if (ok) { _pgSaveTries = 0; return; }
      if (_pgSaveTries < 10) { _pgSaveTries++; pluginGrantsSaveSoon(2000); }
    }, delay || 400);
  }
  function pluginGetGranted(id) { return pluginGrants[id] || null; }
  function pluginSetGranted(id, scopes) {
    pluginGrants[id] = scopes || [];
    pluginGrantsSaveSoon();
  }
  function pluginClearGranted(id) {
    delete pluginGrants[id];
    pluginGrantsSaveSoon();
  }
  // A grant only covers the scopes it was given for. If a plugin update asks for anything
  // new, the old consent does NOT cover it: re-prompt with the new list. (The per-call gate
  // already refuses an ungranted scope, so this was never an escalation -- but the user was
  // never asked, and the plugin just failed silently.)
  function pluginGrantCovers(granted, want) {
    if (!Array.isArray(granted)) return false;
    for (const s of (want || [])) if (granted.indexOf(s) === -1) return false;
    return true;
  }

  const PLUGIN_TAB_ICON = '<rect x="4" y="4" width="7" height="7" rx="1"/><rect x="13" y="4" width="7" height="7" rx="1"/><rect x="4" y="13" width="7" height="7" rx="1"/><path d="M13.5 17h6.5M16.75 13.75v6.5"/>';
  const PLUGIN_BROWSE_ICON = '<circle cx="11" cy="11" r="7"/><path d="M20.5 20.5l-4.2-4.2"/><path d="M11 8v6M8 11h6"/>';
  // Group Bank only applies to group-ironman accounts: varp 4818 bit 19 (varbit 56612,
  // "in a GIM group") flips to 1 for every GIM variant. Unknown state (null, nothing read
  // yet) keeps the tab visible - hiding is only ever done on a confirmed non-GIM read.
  let gimInGroup = null;
  setInterval(async () => {
    try {
      if (!bridge() || !bridge().varps || !myPid()) return;
      const amVp = typeof VP !== 'undefined' ? VP.ACCOUNT_MODE : 4818;
      const vp = JSON.parse(await bridge().varps(myPid(), String(amVp))) || {};
      if (vp[amVp] === undefined) return;
      const inG = ((vp[amVp] >> 19) & 1) === 1;
      if (inG !== gimInGroup) { gimInGroup = inG; try { renderSidebar(); } catch (e) {} }
    } catch (e) {}
  }, 10000);
  function allTabs() {
    const extra = [{ id: 'pluginbrowse', label: 'Browse plugins', cat: 'Plugins', icon: PLUGIN_BROWSE_ICON }];
    pluginTabs.forEach(p => extra.push({ id: 'plugin:' + p.id, label: p.manifest.name, cat: 'Plugins', icon: PLUGIN_TAB_ICON }));
    // Aura groups: one HUD tab each (hidden from the menus; the Auras panel shows/hides them).
    if (typeof auraGroupTabs === 'function') { try { auraGroupTabs().forEach(t => extra.push(t)); } catch (e) {} }
    return TABS.filter(t => (t.id !== 'dung' || DUNG_ENABLED) && (t.id !== 'groupbank' || gimInGroup !== false))
      .map(t => { const g = TAB_GROUP_OF[t.id]; return (g && g.cat && g.cat !== t.cat) ? Object.assign({}, t, { cat: g.cat }) : t; })
      .concat(extra);
  }

  // Splice the host CSP + the SDK shim into the plugin's entry HTML head.
  function pluginBuildSrcdoc(entryHtml) {
    const csp = '<meta http-equiv="Content-Security-Policy" content="' +
      "default-src 'none'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; " +
      "img-src 'self' data:; font-src 'self' data:; connect-src 'none'; frame-src 'none'; " +
      "form-action 'none'; base-uri 'none'" + '">';
    const inject = csp + '<scr' + 'ipt>' + PLUGIN_SDK_SHIM + '</scr' + 'ipt>';
    let html = String(entryHtml || '');
    const head = html.match(/<head[^>]*>/i);
    if (head) { const i = head.index + head[0].length; return html.slice(0, i) + inject + html.slice(i); }
    const htm = html.match(/<html[^>]*>/i);
    if (htm) { const i = htm.index + htm[0].length; return html.slice(0, i) + '<head>' + inject + '</head>' + html.slice(i); }
    return '<!DOCTYPE html><html><head>' + inject + '</head><body>' + html + '</body></html>';
  }

  function renderPluginPermission(id, tab, prev) {
    const c = $('content'); c.innerHTML = '';
    const m = tab.manifest;
    // prev = scopes already consented to for THIS character, when the prompt is a re-ask
    // because the plugin now wants more than it did.
    const isUpdate = Array.isArray(prev) && prev.length > 0;
    const card = document.createElement('div');
    card.dataset.pluginPerm = id;
    card.style.cssText = 'max-width:420px;margin:24px auto;background:#1a1b23;border:1px solid rgba(255,255,255,.08);border-radius:12px;padding:20px;';
    const labels = { 'state.read': 'Read your live game state', 'cache.read': 'Read game cache data (items, sprites, enums)', 'overlay': 'Draw overlays on the game', 'sound': 'Play alert sounds', 'storage': 'Store its own settings', 'notify.os': 'Show Windows notifications', 'clipboard': 'Copy text to your clipboard', 'clipboard.read': 'Read your clipboard contents' };
    let h = '<div style="font-size:1.1em;font-weight:600;">' + pluginEsc(m.name) + '</div>';
    h += '<div style="color:#8b8b9e;font-size:.85em;margin:2px 0 6px;">v' + pluginEsc(m.version) + (m.author ? (' - ' + pluginEsc(m.author)) : '') + '</div>';
    if (tab.source === 'dev')
        h += '<div style="color:#e0b000;font-size:.8em;margin-bottom:12px;">Unsigned developer plugin - only enable plugins you trust.</div>';
    else
        h += '<div style="color:#34d399;font-size:.8em;margin-bottom:12px;">Verified - signed by RuneTools.</div>';
    if (m.description) h += '<div style="font-size:.9em;margin-bottom:12px;">' + pluginEsc(m.description) + '</div>';
    if (isUpdate)
      h += '<div style="color:#e0b000;font-size:.82em;margin-bottom:10px;">This plugin now asks for more access than you allowed. Review the new items before enabling it.</div>';
    h += '<div style="font-size:.82em;color:#8b8b9e;margin-bottom:6px;">This plugin can:</div><ul style="margin:0 0 14px 18px;font-size:.88em;line-height:1.6;">';
    (tab.scopes.length ? tab.scopes : ['(no special access)']).forEach(s => {
      const isNew = isUpdate && prev.indexOf(s) === -1;
      h += '<li' + (isNew ? ' style="color:#e0b000;font-weight:600;"' : '') + '>' + pluginEsc(labels[s] || s) +
           (isNew ? ' <span style="font-weight:400;font-size:.85em;">(new)</span>' : '') + '</li>';
    });
    h += '</ul>';
    // Consent is per character, so name the one being authorised: the same plugin on another
    // character is a separate decision, and the user should not be surprised by that.
    { const acct = pluginAcctKey();
      h += '<div style="font-size:.78em;color:#8b8b9e;margin:-6px 0 12px;">Applies to ' +
           (acct ? pluginEsc(acct) : 'this character') + ' only.</div>'; }
    card.innerHTML = h;
    const row = document.createElement('div');
    row.style.cssText = 'display:flex;gap:8px;';
    const btn = document.createElement('button');
    btn.textContent = 'Enable plugin';
    btn.style.cssText = 'background:var(--accent-hi);color:#fff;border:none;border-radius:8px;padding:9px 18px;font-weight:600;cursor:pointer;';
    btn.addEventListener('click', () => {
      pluginSetGranted(id, tab.scopes);
      const w = wmWinOf('plugin:' + id);
      if (w && !w.min && w.tab === ('plugin:' + id)) renderPaneFor(w);
    });
    const deny = document.createElement('button');
    deny.textContent = 'Deny';
    deny.style.cssText = 'background:rgba(255,255,255,.06);color:#f0f0f5;border:1px solid rgba(255,255,255,.1);border-radius:8px;padding:9px 18px;font-weight:600;cursor:pointer;';
    deny.addEventListener('click', () => {
      pluginClearGranted(id);
      wmCloseTabId('plugin:' + id);
      const bt = allTabs().find(x => x.id === 'pluginbrowse');
      if (bt) openTab(bt);
    });
    row.appendChild(btn); row.appendChild(deny);
    card.appendChild(row);
    c.appendChild(card);
  }

  function renderPlugin(id) {
    const c = $('content');
    if (!c) return;
    const tab = pluginTabs.find(p => p.id === id);
    if (!tab) { c.innerHTML = '<div class="empty">Plugin not found.</div>'; pluginUnmount(id); return; }
    pluginGrantsEnsure();               // async; a not-yet-loaded store just prompts once more
    const granted = pluginGetGranted(id);
    // Consent must cover everything the CURRENT manifest asks for: an update that adds a
    // scope has not been consented to, even though an older grant exists.
    if (!granted || !pluginGrantCovers(granted, tab.scopes)) {
      const cur = c.firstElementChild;
      if (cur && cur.dataset && cur.dataset.pluginPerm === id) return;   // build-once
      renderPluginPermission(id, tab, granted); pluginUnmount(id); return;
    }
    const cur = c.firstElementChild;
    if (cur && cur.dataset && cur.dataset.pluginHost === id) return;      // already mounted
    c.innerHTML = '';
    const wrap = document.createElement('div'); wrap.dataset.pluginHost = id;
    // Fill the window; the plugin scrolls inside itself. A plugin can opt into a
    // fixed height via rtx.plugin.ui.setHeight(px).
    wrap.style.cssText = 'padding:0;flex:1 1 auto;min-height:0;display:flex;flex-direction:column;';
    const fr = document.createElement('iframe');
    fr.setAttribute('sandbox', 'allow-scripts');     // NO allow-same-origin -> opaque origin
    fr.setAttribute('referrerpolicy', 'no-referrer');
    fr.style.cssText = 'border:0;display:block;width:100%;flex:1 1 auto;min-height:0;background:#14151c;';
    wrap.appendChild(fr); c.appendChild(wrap);
    // Least privilege: the mount carries the INTERSECTION of what was granted and what the
    // manifest still asks for, so a scope dropped from a later manifest stops working even
    // though the older grant still lists it. The per-call gate reads this list.
    pluginMounts.set(id, { id, frame: fr, scopes: granted.filter(s => tab.scopes.indexOf(s) !== -1) });
    (async () => {
      let entry = '';
      const entryFn = (tab.source === 'installed') ? bridge().pluginInstalledEntry : bridge().pluginDevEntry;
      try { entry = await entryFn.call(bridge(), id, tab.manifest.entry); } catch (e) {}
      const m = pluginMounts.get(id);
      if (!m || m.frame !== fr) return;             // window closed / remounted while loading
      if (!entry) { c.innerHTML = '<div class="empty">Failed to load plugin entry file.</div>'; pluginUnmount(id); return; }
      fr.srcdoc = pluginBuildSrcdoc(entry);
    })();
  }

  async function readPluginSet(listFn, manifestFn, source) {
    const out = [];
    let ids = [];
    try { ids = JSON.parse(await listFn.call(bridge())); } catch (e) { ids = []; }
    for (const id of (Array.isArray(ids) ? ids : [])) {
      let man = null;
      try { man = JSON.parse(await manifestFn.call(bridge(), id)); } catch (e) {}
      if (!man || man.rtxPluginManifest !== 1) continue;
      const scopes = Array.isArray(man.scopes) ? man.scopes.filter(s => PLUGIN_SCOPES.has(s)) : [];
      // Strip HTML-significant chars from author-supplied strings shown in host DOM.
      const clean = s => String(s == null ? '' : s).replace(/[<>&"']/g, '');
      const entry = (typeof man.entry === 'string' && man.entry && !/[\\/]|\.\./.test(man.entry)) ? man.entry : 'index.html';
      out.push({
        id, source,
        manifest: { id: clean(man.id).slice(0, 80) || id, name: clean(man.name).slice(0, 48) || id, version: clean(man.version).slice(0, 20), author: clean(man.author).slice(0, 60), entry, description: clean(man.description).slice(0, 280) },
        scopes
      });
    }
    return out;
  }

  async function loadPlugins() {
    if (!bridge()) return;
    const byId = {};
    if (bridge().pluginInstalledList) {
      const inst = await readPluginSet(bridge().pluginInstalledList, bridge().pluginInstalledManifest, 'installed');
      inst.forEach(p => { byId[p.id] = p; });
    }
    if (bridge().pluginDevList) {
      const dev = await readPluginSet(bridge().pluginDevList, bridge().pluginDevManifest, 'dev');
      dev.forEach(p => { if (!byId[p.id]) byId[p.id] = p; });   // installed wins over a dev copy
    }
    pluginTabs = Object.keys(byId).map(k => byId[k]);
    renderSidebar();
    // Windows restored (or opened) before this list existed now have real manifests:
    // re-render each open plugin tab; drop any whose plugin no longer exists. Scans
    // w.tabs, since a plugin can be docked alongside anything.
    for (const w of Array.from(wm.wins.values())) {
      for (const tid of w.tabs.slice()) {
        if (String(tid).indexOf('plugin:') !== 0) continue;
        const id = String(tid).slice('plugin:'.length);
        if (pluginTabs.some(p => p.id === id)) { if (!w.min && w.tab === tid) renderPaneFor(w); }
        else {
          console.log('plugin window ' + id + ' not in loaded list [' + pluginTabs.map(p => p.id).join(',') + '] - closing');
          wmCloseTabIn(w, tid);
        }
      }
    }
  }

  // Dev-plugin hot reload: poll a cheap host-side fingerprint of plugins-dev so a
  // folder dropped in (or an edited entry file) shows up without a client restart.
  // The first poll only seeds the baseline; later changes reload the plugin list
  // and remount any open dev plugin whose own folder changed.
  let pluginDevStampLast = null;
  async function pluginDevWatch() {
    try {
      if (!bridge() || typeof bridge().pluginDevStamp !== 'function') return;
      const stamp = String(await bridge().pluginDevStamp());
      if (pluginDevStampLast === null) { pluginDevStampLast = stamp; return; }
      if (stamp === pluginDevStampLast) return;
      const prev = pluginDevStampLast; pluginDevStampLast = stamp;
      await loadPlugins();
      const tok = s => {
        const m = {};
        String(s).split(';').forEach(t => { const i = t.indexOf(':'); if (i > 0) m[t.slice(0, i)] = t.slice(i + 1); });
        return m;
      };
      const before = tok(prev), after = tok(stamp);
      for (const id of Array.from(pluginMounts.keys())) {
        const tab = pluginTabs.find(p => p.id === id);
        if (!tab || tab.source !== 'dev' || before[id] === after[id]) continue;
        pluginUnmount(id);
        const w = wmWinOf('plugin:' + id);
        if (w && !w.min && w.tab === ('plugin:' + id)) { w.pane.innerHTML = ''; renderPaneFor(w); }
      }
    } catch (e) {}
  }

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

