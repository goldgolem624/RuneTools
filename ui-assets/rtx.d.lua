---@meta
-- RuneTools Lua plugin SDK: editor stubs (LuaLS / EmmyLua annotations). Not loaded by the host.
-- Every method mirrors the JavaScript SDK (PLUGIN_SDK.md) with the same name, arguments and JSON
-- shape; calls are synchronous and return the value, or nil plus a reason string on failure.

---@alias rtx.Value table|string|number|boolean|nil

---@class rtx.plugin
local plugin = {}
---@return string
function plugin.id() end
---@return string
function plugin.apiVersion() end
---@return "lua"
function plugin.runtime() end
---@return string  # e.g. "Lua 5.4.7"
function plugin.runtimeVersion() end
---@return string[]
function plugin.grantedScopes() end
---@param scope string
---@return boolean
function plugin.hasScope(scope) end
---@param fn fun()
function plugin.ready(fn) end
--- Reads a text file inside the plugin folder ("data/rows.json"); nil when missing.
---@param relativePath string
---@return string|nil
function plugin.readFile(relativePath) end

---@class rtx.state
local state = {}
---@return table|nil player  # {name,x,y,plane,in_world,...}
---@return string|nil err
function state.player() end
---@return table|nil
function state.info() end
---@return table|nil inventory  # {items:[[slot,item,qty],...],count}
function state.inventory() end
---@return table|nil
function state.equipment() end
---@return table|nil
function state.bank() end
---@return table|nil
function state.metalBank() end
---@return table|nil
function state.materials() end
---@return table|nil
function state.groupBank() end
---@return table|nil
function state.baitBox() end
---@param range? integer  # tiles, 1..64
---@return table|nil scene  # {npcs:[{id,uid,x,y,plane,anim,face,name,lp,lpMax,...}],players:[...],objects:[...]}
function state.scene(range) end
---@return table|nil
function state.groundItems() end
---@return table|nil
function state.social() end
---@param since? integer
---@param max? integer
---@return table|nil
function state.combatLog(since, max) end
---@param x integer
---@param y integer
---@param plane integer
---@param radius integer
---@return table|nil
function state.walkable(x, y, plane, radius) end
---@param idsCsv string
---@return table<string, integer>|nil
function state.varps(idsCsv) end
---@param ids integer[]
---@return table<string, integer>|nil  # asynchronous: nil,"pending" on the first call
function state.varbits(ids) end
---@param group integer
---@param compsCsv string
---@return table|nil
function state.interface(group, compsCsv) end
---@return table|nil buffs  # {buffs:[{slot,struct,name,remaining,...}]}
function state.buffs() end
---@return table|nil
function state.cooldowns() end
---@return table|nil
function state.perks() end
---@return table|nil
function state.actionBar() end
---@param containerId integer
---@return table|nil
function state.container(containerId) end
---@param containerId integer
---@param slot integer
---@return table|nil
function state.itemExtra(containerId, slot) end
---@return table|nil  # asynchronous
function state.pets() end
---@return table|nil  # asynchronous
function state.bosses() end
--- The instance you are in right now, or nil when you are not in one.
--- { struct, name, mode, modeValue, health, healthMax }
---@return table|nil  # asynchronous
function state.encounter() end
---@return table|nil  # asynchronous
function state.hideyHoles() end
---@param sinceSeq? integer
---@return table|nil events  # {seq,tick,events:[{seq,t,wall,op,len,kind,...}]}
function state.events(sinceSeq) end
---@return table|nil  # {cutscene,inCutscene,options:[44 ints]}
function state.clientState() end
---@return integer|nil
function state.gameTick() end
---@param group integer
---@return table|nil
function state.interfaceGroup(group) end
---@return table|nil
function state.ports() end
---@param ids integer[]
---@return table<string, integer>|nil  # asynchronous
function state.varcs(ids) end
---@return table|nil  # asynchronous
function state.achievements() end
---@param id integer
---@return table|nil  # asynchronous
function state.achievement(id) end
---@return table|nil  # asynchronous
function state.skillBonus() end
---@return table|nil  # asynchronous
function state.dailies() end
---@return table|nil  # asynchronous
function state.quests() end
---@param id integer
---@return table|nil  # asynchronous
function state.quest(id) end
---@return table|nil  # asynchronous
function state.mysteries() end
---@return table|nil
function state.varDomainStores() end
---@return table|nil
function state.dialog() end
---@return table|nil
function state.gameTickState() end
---@return table|nil
function state.hoverEntity() end
---@return table|nil
function state.interfaceGroups() end
---@param slot integer
---@return table|nil
function state.invSlotRect(slot) end
---@return table|nil
function state.openContainers() end
---@return table|nil
function state.pof() end
---@return boolean|nil
function state.gameFocused() end

---@class rtx.text
local text = {}
--- The game's own detail text for a buff, as its tooltip script computes it over live vars.
---@param structId integer  # `struct` from state.buffs()
---@param count? integer    # the buff's stack count when it has one
---@return table|nil  # { text = <with the game's markup>, plain = <without> }
function text.buff(structId, count) end
--- The game's own tooltip lines for an item (charges, augment level, degradation, examine ...).
---@param itemId integer
---@param containerId? integer  # 93 backpack, 94 worn ...: the instance to read item vars from
---@param slot? integer
---@return table|nil  # { text, plain }
function text.item(itemId, containerId, slot) end

---@class rtx.cache
local cache = {}
---@param itemId integer
---@return table|nil
function cache.itemInfo(itemId) end
---@param itemId integer
---@return string|nil  # data URL
function cache.itemIcon(itemId) end
---@param modelId integer
---@return string|nil
function cache.modelIcon(modelId) end
---@param spriteId integer
---@return string|nil
function cache.sprite(spriteId) end
---@return table|nil
function cache.varbitMap() end
---@return table|nil
function cache.varbitDomainMap() end
---@return table|nil
function cache.varbitDomains() end
---@param domain integer
---@return table|nil
function cache.varDefs(domain) end
---@param enumId integer
---@return table|nil
function cache.enumInfo(enumId) end
---@param paramId integer
---@return table|nil
function cache.paramDef(paramId) end
---@param structId integer
---@return table|nil
function cache.structParams(structId) end
---@param itemId integer
---@return table|nil
function cache.itemParams(itemId) end
---@param x integer
---@param y integer
---@param plane integer
---@param w integer
---@param h integer
---@return table|nil
function cache.mapWindow(x, y, plane, w, h) end
---@return table|nil
function cache.achievements() end
---@return table|nil
function cache.abilityConfigs() end
---@return table|nil
function cache.abilityTips() end
---@return table|nil
function cache.archResearch() end
---@return table|nil
function cache.quests() end
---@return table|nil
function cache.dbRows(...) end
---@param npcId integer
---@return table|nil
function cache.npcInfo(npcId) end
---@return table|nil
function cache.cs2Names() end
---@return table|nil
function cache.mystPages() end
---@return table|nil
function cache.mapAreas() end
---@return table|nil
function cache.ifaceGroup(...) end
---@return boolean|nil
function cache.isLeaguesWorld() end
---@return table|nil
function cache.buffCatalog() end

---@class rtx.overlay
local overlay = {}
---@param text string
function overlay.toast(text) end
---@param text string
---@param ttlMs? integer
function overlay.notify(text, ttlMs) end
---@param text string
---@param style? integer
---@param color? integer
function overlay.centerText(text, style, color) end
---@param names string[]
function overlay.highlight(names) end
---@param name string
---@param label? string
---@param x? integer
---@param y? integer
function overlay.highlightNpc(name, label, x, y) end
function overlay.flashGame() end
---@param ... string
---@return boolean|nil  # asynchronous
function overlay.highlightOption(...) end
---@param itemId integer
---@param label? string
---@return boolean|nil  # asynchronous
function overlay.highlightItem(itemId, label) end
---@param marks table[]  # {x,y,plane,label,x2,y2,color,color2,merge}
function overlay.guideTiles(marks) end
function overlay.clearHighlight() end
--- The game itself points the way to one target: its arrow, chevrons and ground trail.
---@param kind string    # "npc" | "object" | "tile"
---@param target string  # a name, part of one, or an id; for a tile "x, y" or "x, y, plane"
function overlay.pointAt(kind, target) end
function overlay.pointClear() end
---@param rects table[]  # {x,y,w,h} or {{x,y,w,h},...}
function overlay.highlightRects(rects) end
---@param x number
---@param y number
---@param w number
---@param h number
function overlay.highlightRect(x, y, w, h) end
---@param payload table  # {title,sub,now,cur:[{id,key}],next:[{id,gap}]}
function overlay.hudAbilities(payload) end
---@param query string
function overlay.wikiSearch(query) end

---@class rtx.notify
local notify = {}
---@param title string
---@param body string
function notify.windows(title, body) end
---@param text string
function notify.discord(text) end

---@class rtx.clipboard
local clipboard = {}
---@param text string
function clipboard.copy(text) end
---@return string|nil
function clipboard.paste() end

---@class rtx.sound
local sound = {}
---@param name string
function sound.play(name) end

---@class rtx.storage
local storage = {}
---@param key string
---@return rtx.Value
function storage.get(key) end
---@param key string
---@param value rtx.Value
function storage.set(key, value) end
---@return string[]|nil
function storage.keys() end

---@class rtx.TelemetryResult
---@field ok boolean
---@field size integer|nil the file's new size in bytes
---@field error string|nil

---@class rtx.telemetry
local telemetry = {}
---@param name string e.g. "encounter.jsonl"
---@param record rtx.Value one line: a string as is, anything else as JSON
---@return rtx.TelemetryResult|nil
function telemetry.append(name, record) end
---@param name string
---@param records rtx.Value[] up to 1000
---@return rtx.TelemetryResult|nil
function telemetry.appendMany(name, records) end
---@param name string
---@param data rtx.Value replaces the whole file
---@return rtx.TelemetryResult|nil
function telemetry.export(name, data) end
---@return {files: {name: string, size: integer, modified: integer}[], bytes: integer, limit: integer}|nil
function telemetry.list() end
---@param name string
---@return boolean|nil
function telemetry.remove(name) end
---@return boolean|nil
function telemetry.open() end

---@class rtx.prices
local prices = {}
---@return table|nil
function prices.latest() end
---@return table|nil
function prices.mapping() end
---@param ids integer|integer[]
---@return table|nil
function prices.item(ids) end

---@class rtx.ui
local ui = {}
--- Publishes the panel: a widget node or a list of nodes. See PLUGIN_SDK.md, "Panels".
---@param tree table|nil
function ui.render(tree) end
function ui.clear() end
---@param title string
function ui.setTitle(title) end
--- Declares settings shown on the Preferences page; same schema as the JavaScript SDK.
---@param schema table[]  # {{key,type="toggle"|"select"|"slider"|"text",label,hint,default,options,min,max,step},...}
---@return table|nil values  # asynchronous on the first call
function ui.settings(schema) end

---@class rtx.settings
local settings = {}
---@return table
function settings.get() end
---@param fn fun(values: table)
function settings.on(fn) end

---@class rtx.events
local events = {}
---@param kind string  # "skill_update", "obj_add", ..., "*"
---@param fn fun(ev: table)
---@return fun(ev: table)
function events.on(kind, fn) end
---@param kind string
---@param fn fun(ev: table)
---@return boolean
function events.off(kind, fn) end

---@class rtx.timer
local timer = {}
---@param seconds number
---@param fn fun()
---@return integer id
function timer.after(seconds, fn) end
---@param seconds number
---@param fn fun()
---@return integer id
function timer.every(seconds, fn) end
---@param id integer
function timer.cancel(id) end

---@class rtx.console
local console = {}
function console.debug(...) end
function console.info(...) end
function console.log(...) end
function console.warn(...) end
function console.error(...) end
--- A logger whose lines carry `tag` (up to 24 characters) for filtering in the Console panel.
---@param tag string
---@return rtx.console
function console.scoped(tag) end

---@class rtx.json
local json = {}
---@param value rtx.Value
---@return string
function json.encode(value) end
---@param text string
---@return rtx.Value
function json.decode(text) end

---@class rtx
---@field plugin rtx.plugin
---@field state rtx.state
---@field text rtx.text
---@field cache rtx.cache
---@field overlay rtx.overlay
---@field notify rtx.notify
---@field clipboard rtx.clipboard
---@field sound rtx.sound
---@field storage rtx.storage
---@field telemetry rtx.telemetry
---@field prices rtx.prices
---@field ui rtx.ui
---@field settings rtx.settings
---@field events rtx.events
---@field timer rtx.timer
---@field json rtx.json
---@field console rtx.console
---@field lastError string|nil
rtx = {}

---@param event "ready"|"tick"|"state"|"events"|"settings"|"theme"
---@param fn function
---@return function
function rtx.on(event, fn) end
---@param event string
---@param fn function
---@return boolean
function rtx.off(event, fn) end
--- Generic form: rtx.call("state.scene", { 20 })
---@param method string
---@param args? table
---@return rtx.Value value
---@return string|nil err
function rtx.call(method, args) end
function rtx.log(...) end
function rtx.debug(...) end
function rtx.warn(...) end
function rtx.error(...) end
