// rtx-plugin-api.js: the brokered plugin API surface: PLUGIN_SDK_SHIM (spliced into every plugin frame), the argument clamps, the PLUGIN_API method table, the per-plugin rate limiter, and the prices / ability-tip caches it serves from
// Loads after: rtx-pane.js; nothing here runs at load (pluginBrokerInit in rtx-plugins.js dispatches into PLUGIN_API at attach time).
// Plain script, page globals by design: every top-level name here is a page global that panels and the other core files use.
  // Plugin SDK API surface. The sandbox / broker / permission model is documented at the top of
  // rtx-plugins.js; this file is the METHOD TABLE the broker dispatches into. Every entry is
  // { scope, json, run(args, pid, pluginId, frame) }; args are plugin-supplied and clamped here.
  const PLUGIN_PROTO  = 'rtx.plugin/1';
  const PLUGIN_SCOPES = new Set(['state.read', 'cache.read', 'overlay', 'sound', 'storage', 'notify.os', 'clipboard', 'clipboard.read']);
  const pluginBuckets = {};     // "id|method" -> token bucket (pluginRateOk)

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
if(m.kind==='event'){if(m.event==='theme'){var td=m.data||{};for(var tk in td)document.documentElement.style.setProperty(tk,td[tk]);return;}if(m.event==='ready'){var d=m.data||{};S.ready=true;S.scopes=Array.isArray(d.scopes)?d.scopes.slice():[];S.apiVersion=d.apiVersion||null;S.pluginId=d.pluginId||null;var c=rcb.slice();rcb.length=0;c.forEach(function(f){try{f();}catch(e){}});return;}if(m.event==='events'){var arr=Array.isArray(m.data)?m.data:[];arr.forEach(function(ev){var k=ev&&ev.kind;var fs=(EV[k]||[]).concat(EV['*']||[]);fs.forEach(function(f){try{f(ev);}catch(e){}});});}var ls=L[m.event];if(ls)ls.forEach(function(f){try{f(m.data);}catch(e){}});}});
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
settings:{get:function(){return call('settings.get',[]);},on:function(cb){if(typeof cb==='function')L.settings.push(cb);}},
prices:{latest:function(){return call('prices.latest',[]);},mapping:function(){return call('prices.mapping',[]);},item:function(ids){return call('prices.item',[ids]);}}};
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
  // Overlay-record clamp: keeps the \x1e-record / \x1f-field structure the native
  // parsers expect but bounds it (64 records, 16 fields, 256 chars each), so a
  // plugin cannot flood the per-frame overlay pass or smuggle oversized payloads.
  const pOverlayArgs = a => (Array.isArray(a) ? a : []).slice(0, 8).map(x => {
    if (typeof x === 'number' || typeof x === 'boolean' || x == null) return x;
    return String(x).slice(0, 65536).split('\x1e').slice(0, 64)
      .map(r => r.split('\x1f').slice(0, 16).map(f => f.slice(0, 256)).join('\x1f'))
      .join('\x1e');
  });
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
    // NOT plain pArgs: these carry record lists with \x1e/\x1f separators, and the
    // 64KB pass-through let a plugin feed unbounded records straight into the native
    // overlay parsers (a few hundred labels is a render-thread DoS; arbitrary rects
    // are on-screen spoofing fuel). Records and fields are bounded here; the panels'
    // own calls don't route through the broker and are unaffected.
    'overlay.guideMarks':     { scope: 'overlay',     json: false, run: (a, pid) => bridge().guideMarks(pid, ...pOverlayArgs(a)) },
    'overlay.panelViz':       { scope: 'overlay',     json: false, run: (a, pid) => bridge().panelViz(pid, ...pOverlayArgs(a)) },
    'overlay.uiHighlight':    { scope: 'overlay',     json: false, run: (a, pid) => bridge().uiHighlight(pid, ...pOverlayArgs(a)) },
    'overlay.hudSprite':      { scope: 'overlay',     json: false, run: (a, pid) => bridge().hudSprite(pid, ...pOverlayArgs(a)) },
    'overlay.outlineObject':  { scope: 'overlay',     json: false, run: (a, pid) => bridge().outlineObject(pid, ...pOverlayArgs(a)) },
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
    } },
    // RS3 GE prices: the launcher caches relay copies from the RuneTools server, which is
    // the ONLY consumer of the upstream price API -- a plugin call never leaves the machine
    // beyond that relay. latest: {itemId: {high, highTime, low, lowTime}}; mapping: item
    // metadata array (name, id, limit, alch values). item: one id or an array of up to 50,
    // answered from a parsed broker-side cache so watching a few prices costs neither the
    // half-megabyte payload nor a JSON parse per call.
    'prices.latest':    { scope: 'cache.read', json: true,    run: () => bridge().pricesCached() },
    'prices.mapping':   { scope: 'cache.read', json: true,    run: () => bridge().pricesMapping() },
    'prices.item':      { scope: 'cache.read', json: false,   run: (a) => {
      const data = pluginPricesData();
      const ids = Array.isArray(a[0]) ? a[0] : [a[0]];
      const out = {};
      for (const v of ids.slice(0, 50)) { const n = pClampId(v); if (data[n] !== undefined) out[n] = data[n]; }
      return out;
    } }
  };

  // Parsed copy of the launcher's cached latest-prices payload, refreshed at most every
  // 30 s (the relay itself refreshes every 90 s), shared by every prices.item call.
  let _pricesParsed = null, _pricesParsedAt = 0;
  function pluginPricesData() {
    const now = Date.now();
    if (!_pricesParsed || now - _pricesParsedAt > 30000) {
      try {
        const d = JSON.parse(bridge().pricesCached() || '{}');
        const data = (d && d.data) ? d.data : d;
        if (data && typeof data === 'object') _pricesParsed = data;
      } catch (e) {}
      if (!_pricesParsed) _pricesParsed = {};
      _pricesParsedAt = now;
    }
    return _pricesParsed;
  }

  function pluginRateOk(id, method) {
    // rate = tokens refilled per second; capacity = max burst (>=1 so slow rates still fire).
    const rate = method.indexOf('storage.') === 0 ? 4
               : method.indexOf('overlay.') === 0 ? 6
               : method.indexOf('notify.') === 0 ? 0.1     // OS toasts: 1 per 10s
               : method.indexOf('clipboard.') === 0 ? 1
               : method === 'prices.item' ? 4             // tiny answers from a local parsed cache
               : method.indexOf('prices.') === 0 ? 0.5    // half-MB payloads; data changes every 90s anyway
               : 20;
    const cap = Math.max(1, rate);
    const k = id + '|' + method, now = Date.now();
    let b = pluginBuckets[k];
    if (!b) { b = { t: cap, ts: now }; pluginBuckets[k] = b; }
    b.t = Math.min(cap, b.t + ((now - b.ts) / 1000) * rate); b.ts = now;
    if (b.t < 1) return false;
    b.t -= 1; return true;
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
