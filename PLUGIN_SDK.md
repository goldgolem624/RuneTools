# RuneTools Plugin SDK

Build your own tools inside RuneTools -- custom tabs and overlays -- using plain HTML, CSS,
and JS, or in Lua. A plugin reads live game state and draws overlays through the `window.rtx.plugin`
API (HTML plugins) or the `rtx` table (Lua plugins, see "Lua plugins" below); both runtimes expose
the same methods, scopes and event kinds, so everything documented here applies to both.

## Quick start

A plugin is a folder with a `manifest.json` and an entry HTML file:

```
my-plugin/
  manifest.json
  index.html
```

1. Put the folder in `%USERPROFILE%\RuneToolsX\plugins-dev\<your-id>\` so its manifest lives at `...\plugins-dev\com.yourname.tool\manifest.json`.
2. Open the **Plugins** tab in RuneTools; your plugin appears within a couple of seconds, no client restart needed. Enable it and approve the permission prompt.
3. Edit your files and save; an open plugin window reloads itself automatically. No signing required for local development.

## manifest.json

```json
{
  "rtxPluginManifest": 1,
  "apiVersion": "1.0",
  "id": "com.yourname.tool",
  "name": "Your Tool",
  "version": "1.0.0",
  "author": "Your Name",
  "description": "What it does.",
  "entry": "index.html",
  "icon": "icon.png",
  "scopes": ["state.read", "overlay", "storage"],
  "minHostVersion": "1.1.3"
}
```

| Field               | Type     | Notes                                                            |
|---------------------|----------|-----------------------------------------------------------------|
| `rtxPluginManifest` | number   | Must be `1`.                                                     |
| `apiVersion`        | string   | API contract; currently `"1.0"`.                                |
| `id`                | string   | Reverse-DNS, globally unique, immutable (also your storage key).|
| `name`              | string   | Shown as the tab title.                                         |
| `version`           | string   | Semver.                                                         |
| `author`            | string   | Shown on the plugin page.                                       |
| `description`       | string   | Short summary.                                                  |
| `entry`             | string   | Entry HTML file inside the bundle (HTML plugins).               |
| `runtime`           | string   | Optional. `"lua"` runs the plugin in the Lua runtime; omit for HTML. |
| `main`              | string   | Lua plugins: the entry chunk, a bare `.lua` filename (default `main.lua`). |
| `icon`              | string   | Optional. Bare filename of a plugin icon inside the bundle.     |
| `background`        | boolean  | Optional. `true` keeps the plugin running with no window open once its scopes are granted: it gets ticks, state and events off-screen and its overlays stay up. Opening the window restarts it there; closing the window restarts it off-screen. Give such a plugin its own enabled setting. |
| `scopes`            | string[] | Permissions you request (see below). Request only what you use. |
| `minHostVersion`    | string   | Optional minimum RuneTools version.                            |

### Scopes

| Scope        | Unlocks                                          |
|--------------|--------------------------------------------------|
| `state.read` | `rtx.plugin.state.*` (live state, current account) |
| `cache.read` | `rtx.plugin.cache.*` (static cache data: items, sprites, enums) |
| `overlay`    | `rtx.plugin.overlay.*` (overlay visuals)         |
| `sound`      | `rtx.plugin.sound.play`                           |
| `storage`    | `rtx.plugin.storage.*` (per-plugin settings)     |
| `notify.os`  | `rtx.plugin.notify.windows` (Windows notifications; 1 per 10s) |
| `notify.discord` | `rtx.plugin.notify.discord` (the user's own Discord webhook; 1 per 10s) |
| `clipboard`  | `rtx.plugin.clipboard.copy` (copy-only; nothing is read back) |
| `clipboard.read` | `rtx.plugin.clipboard.paste` (reads clipboard TEXT on user action; ask only if you truly need it) |

`rtx.plugin.ui.*` and the meta/event helpers are always available. The user approves scopes
on first enable, and the host enforces them on every call regardless of the manifest.

**Consent is per character, and covers only the scopes it was given for.** Two consequences
for you:

- The same plugin enabled on one character is **not** enabled on another. Each character
  approves it separately, and grants are stored per account. Expect `grantedScopes()` to be
  empty on a character that has not approved you yet.
- **Adding a scope in an update re-prompts.** The host compares your manifest's scopes against
  what that character actually approved; anything new triggers the permission card again, with
  the added items highlighted, and your plugin does not mount until it is approved. Removing a
  scope needs no re-prompt, and the removed scope stops working immediately (the host serves
  the intersection of what was granted and what the current manifest asks for).

So request the scopes you need up front rather than adding them later, and always branch on
`hasScope()` instead of assuming a call will succeed.

## How calls work

The host injects the SDK into your frame -- **do not** ship `plugin-sdk.js` yourself, and do
not add `<script src>` or other remote references (the frame runs under a strict CSP). Just
use `window.rtx.plugin`.

- **Every data call returns a `Promise`** -- `await` it.
- **Wait for the handshake first.** `await rtx.plugin.ready()` resolves once your granted scopes and identity have arrived (before that, `grantedScopes()`/`id()` return defaults).
- **Don't poll in a loop.** The host pushes a `tick` event on its refresh cadence (about 4x/second); do your reads in that handler.
- **Handle rejections and nulls.** A call rejects when a scope is not granted (`scope not granted: <scope>`), you exceed a rate limit (`rate limited`), or it times out after 15s. A state call that can't read (not logged in / not in-game) resolves to `null` -- check before using it.

Rate limits per method, per plugin (token bucket): `storage.*` 4/s, `overlay.*` 6/s,
`notify.*` 1 per 10s, `clipboard.*` 1/s, everything else 20/s.

```js
await rtx.plugin.ready();

// identity / scopes
rtx.plugin.id();              // "com.yourname.tool"
rtx.plugin.apiVersion();      // "1.0"
rtx.plugin.grantedScopes();   // ["state.read","overlay","storage"]
rtx.plugin.hasScope("overlay"); // true

// events (host-pushed; no busy-polling)
rtx.plugin.on("tick",  () => { /* refresh here */ });
rtx.plugin.on("state", (snapshot) => { /* changed snapshot; requires state.read */ });
```

### Game events

With `state.read` the host also pushes game events captured from the server packet stream (the
event channel, docs/event-channel.md). They arrive batched on the same cadence as `tick`, one
callback per event in capture order; `tick` itself is unchanged.

```js
rtx.plugin.events.on("skill_update", (ev) => { /* {seq,t,wall,op,len,kind,skill,name,level,xp} */ });
rtx.plugin.events.on("container_update", (ev) => { /* {container,flags,slots:[{slot,item,qty}],partial} */ });
rtx.plugin.events.on("runclientscript", (ev) => { /* {script,sig,args} */ });
rtx.plugin.events.on("varp_set", (ev) => { /* {id,value}: the server changed a player variable */ });
rtx.plugin.events.on("varbit_set", (ev) => { /* {id,value}: the server set a varbit directly */ });
rtx.plugin.events.on("varc_set", (ev) => { /* {id,value}: the server changed a client variable */ });
rtx.plugin.events.on("buff_update", (ev) => { /* {struct,active,name}: a buff-bar entry was added or removed */ });
rtx.plugin.events.on("obj_add", (ev) => { /* {x,y,plane,item,qty,owner?}: an item appeared on a tile (drops, spawns) */ });
rtx.plugin.events.on("obj_del", (ev) => { /* {x,y,plane,item}: an item left a tile (picked up, despawned) */ });
rtx.plugin.events.on("obj_count", (ev) => { /* {x,y,plane,item,from,qty}: a ground stack changed quantity */ });
rtx.plugin.events.on("loc_add", (ev) => { /* {x,y,plane,loc,type,rot}: a map object was placed or replaced */ });
rtx.plugin.events.on("loc_del", (ev) => { /* {x,y,plane,type,rot}: a map object was removed */ });
rtx.plugin.events.on("spotanim", (ev) => { /* {x,y,plane,gfx,height,delay}: a graphic played on a tile */ });
rtx.plugin.events.on("spotanim_actor", (ev) => { /* {target:"player"|"npc"|"tile",index?,x?,y?,gfx,height,delay,slot}: a graphic on an actor */ });
rtx.plugin.events.on("projectile", (ev) => { /* {form,gfx,...}: a projectile launched (fields still being confirmed live) */ });
rtx.plugin.events.on("sound", (ev) => { /* {id,...} */ }); rtx.plugin.events.on("area_sound", (ev) => { /* {x,y,plane,id,loops,radius} */ });
rtx.plugin.events.on("zone_update", (ev) => { /* {x,y,plane,items:[...]}: several of the above batched for one 8x8 zone */ });
rtx.plugin.events.on("gameTick", (ev) => { /* {tick, dtMs}: one per 600 ms server tick */ });
rtx.plugin.events.on("*", (ev) => { /* every kind */ });
rtx.plugin.events.off("skill_update", fn);
```

Kinds: `obj_add`, `obj_del`, `obj_count` (ground items by world tile, item id and quantity; the
drop log every plugin has wanted), `loc_add`, `loc_del` (map objects appearing and vanishing),
`spotanim`, `spotanim_actor` (graphics on tiles and on players or NPCs, with the target's index),
`projectile`, `sound`, `area_sound`, `zone_base`, `zone_clear`, `zone_update` (a batch of the
tile kinds for one zone, in `items`), `skill_update`, `container_update`, `runclientscript`, `buff_update` (`struct`, `active`,
`name`: the server bound or cleared a buff-bar entry; pair with `state.buffs()` for its timer), `varp_set`, `varbit_set` and `varc_set` (`id`,
`value`; every server-driven variable change the moment it arrives, so you can react to a varp or
varbit changing without polling: a varbit is a bit range of its varp, see `cache.varbitDomains`),
`run_energy` (`value`), `run_weight` (`value`), `ping` (`a`, `b`), `ge_offer` and other undocumented opcodes as `raw`
(`{op,len,hex}`), and `gameTick`. Chat never appears here. Which opcodes are captured is a host
setting (Developer > Events); plugins cannot change it.

## API reference

All `state.*` reads act on the **current account** shown in the panel. Shapes below are the
exact JSON the host returns; log a call's result during development to see every field.

### state.read

```js
await rtx.plugin.state.player();   // alias of state.info()
await rtx.plugin.state.info();
// -> { in:true, x:3221, y:3218, trueTile:{ x:3221, y:3218 }, plane:0, region:12850, lx:33, ly:18,
//      anim:-1, moving:false, interact: { type:1, id:3079, uid:12345, name:"Goblin" } | null }
// -> { in:false }                       when not in-game / unreadable
//    type: 1 = NPC, 2 = player. x/y are world tile coords. anim -1 = none.
//    x/y is the visible position, which interpolates between tiles while moving. trueTile is the
//    tile the game currently holds for the actor, read from its movement route; while moving it
//    leads x/y by up to two tiles, and when stationary it equals x/y. The Overlay tab's "True tile"
//    toggle draws the same value in the game view for debugging.
//    Also present: splats (hitsplats landing on you, same shape as scene npcs[].splats), bar (your
//    first head bar fill 0..255 or -1), world (current world id), mouse { x, y, buttons } in client
//    pixels with buttons bits 1 left 2 right 4 middle, keys { shift, alt, ctrl }, and
//    loading { pct, screen } (map load percent and whether the loading screen is up).
//    region = (x>>6)<<8 | (y>>6); lx/ly = local tile within the region (0..63) --
//    the instance-stable coordinate the tile-marker feature stores by.

await rtx.plugin.state.inventory();
await rtx.plugin.state.equipment();
await rtx.plugin.state.bank();        // only populated while the bank is open
// -> { present:true, count:3, cap:28,
//      items: [ [slot, id, stack, name], ... ] }
//    Each item is a 4-tuple: slot (int), id (int), stack (int), name (string).
//    present:false means the container could not be read.

await rtx.plugin.state.groupBank();   // Group Ironman shared bank (container 963)
await rtx.plugin.state.metalBank();   // Metal bank (smithing ores + bars)
await rtx.plugin.state.materials();   // Archaeology material storage
await rtx.plugin.state.baitBox();     // Anachronia Big Game Hunter bait box (container 867)
await rtx.plugin.state.nexus();       // Necromancy nexus necrotic runes (container 953), cached like the bank
// -> { open, character, cached_at, count, items:[ [slot, id, stack, name], ... ] }
//    Same shape as bank(): live while that storage UI is open, otherwise the
//    per-character disk cache (open:false).

await rtx.plugin.state.social();
// -> { in:true, world:70, friendsLoaded:true, online:3, friends:[ { name, world }, ... ] }
//    world 0 on a friend means offline. Names are display names.

await rtx.plugin.state.walkable(3221, 3218, 0, 2);
// -> { x, y, plane, r, rows:[ "00100", "00100", ... ] }   the (2r+1)^2 tiles around x,y, r 1..8;
//    rows[i] is world row y-r+i, character j is column x-r+j; '0' walkable, '1' blocked (scenery
//    footprint, wall tile or void), from the map cache's collision data. Instance tiles are unknown.

await rtx.plugin.state.groundItems();
// -> [ { id, x, y, plane }, ... ]   dropped item stacks lying on the ground
//    id = item id (resolve the name with cache.itemInfo); x/y are world tile coords.
//    Empty [] when there are none / you're not in-game.

await rtx.plugin.state.combatLog(since, max);   // since = last seq you have seen (0 = from the start), max <= 2000
// -> { seq, gap, events:[ { seq, t, type, uid, id, name, x, y, plane, hitmark, kind, other, value,
//                            cycle, dur, lp, lpMax }, ... ] }
//    The combat log: every hitsplat the game drew on any actor near you, one event each, in the
//    order the hits landed. The host polls the actors' hitsplat rings five times a second and logs
//    each record exactly once, so you never need to dedupe. Keep the returned seq and pass it back
//    as since on the next call to get only new events; gap is true when you asked for events the
//    ring has already dropped (it keeps the last 4096).
//      seq      monotonic id per client session
//      t        wall clock, ms since the Unix epoch, when the host first saw the record
//      type     "npc", "player" or "self" (the local player: a hit taken)
//      uid/id   actor uid; id is the NPC config id (-1 for players); name as shown in game
//      x/y/plane the actor's tile when the hit was read
//      hitmark  raw hitmark id; kind names it ("melee", "ranged crit", "necromancy", "typeless",
//               "poison", "heal", "absorbed", "blocked", "deflect", "text", ...); other is true for
//               the game's "Other Hitsplats" set, meaning a hit between other players and NPCs.
//               Hits involving you (dealt or taken) always use the personal set (other false).
//               Heals are the exception: a heal on any player uses 143 (other false), so read
//               other on damage kinds only. A heal of 0 is drawn on you at the first hit of a
//               burst you start; ignore it.
//      value    damage (or heal amount); 0 for blocked/absorbed
//      cycle/dur the record's start on the actor's 20 ms cycle clock and its lifetime (60)
//      lp/lpMax the actor's life points at the poll: NPCs from the actor, "self" from your own
//               varps (13537 / 13538); -1 for other players, whose life points are not sent
//    DPM: sum value over events with type "npc" or "player", other false and a damage kind; damage
//    taken is type "self". The DPM Meter sample plugin is built on this call; the client's own
//    Combat Log panel (Combat category) is a live searchable, filterable viewer of the same stream.

await rtx.plugin.state.scene(range);  // range = 1..64 tiles (clamped)
// -> { players:[..], npcs:[..], objects:[..], specials:[..], walk, ... }
//    Entities are grouped by kind, NOT a single flat list:
//      npcs:    { id, uid, x, y, trueTile:{x,y}, plane, combat, anim, face, size, name, actions[],
//                 lp, lpMax, target, bar, splats[] }
//      players: { uid, x, y, trueTile:{x,y}, plane, combat, anim, self, name, bar, splats[] }
//      projectiles: [ { sx, sy, dx, dy, fsx, fsy, fdx, fdy } ]   in flight this frame; tiles + fine units
//      effects:     [ { gfx, x, y, fx, fy } ]                     world spot animations this frame
//    x/y is the visible (interpolated) tile; trueTile is the tile the game holds for the actor,
//    from its movement route (same rule as state.info()).
//    lp/lpMax are the NPC's current and max life points (-1 unknown); target is the player index
//    the NPC is attacking (-1 none); bar is the fill 0..255 of the actor's first head bar (-1 none),
//    which is a health bar on most NPCs and a lifetime timer on helper NPCs such as the Eternal
//    magic tree's (config 31500). splats are the hitsplat records the game is drawing on the actor:
//    [hitmark, value, startCycle, durationCycles]. A record lives durationCycles x 20 ms (usually
//    1.2 s) and is reused only after it expires, but an expired record stays in memory until then, so
//    the last hits of a fight linger for as long as the actor exists. Poll at 4 Hz or faster and
//    count a record only when it was absent from that actor's list on your previous poll (key on
//    ring slot, startCycle, value, hitmark); never forget records on a timer or you will count them
//    twice. Hitmark ids resolve through cache config archive 46, and the id alone says whose hit it
//    is: the game draws every hit that involves you with its "Personal Hitsplats" set and every
//    hit between other players and NPCs with its "Other Hitsplats" set (the one players can hide).
//      personal: 133 melee, 134 melee critical, 136 ranged, 137 ranged critical, 139 magic,
//                140 magic critical, 477 necromancy, 478 necromancy critical, 480 conjured spirit,
//                481 conjured spirit critical, 144 typeless, 142 poison, 145 cannon, 146 and 238
//                deflect (reflected damage), 248 split soul, 346 blight, 416 pierced shield,
//                435 shadow pool, 143 heal, 148 absorbed (crystal shield), 482 blocked (value 0)
//      other:    150 melee, 151 melee critical, 153 ranged, 154 ranged critical, 156 magic,
//                157 magic critical, 487 necromancy, 488 necromancy critical, 490 conjured spirit,
//                491 conjured spirit critical, 161 typeless, 159 poison, 162 cannon, 352 deflect,
//                353 split soul, 351 blight, 415 pierced shield, 160 heal, 165 absorbed,
//                163 blocked, 492 hidden zero splat
//    Text marks (Dodged 141, Immune 347, Executed 407, Perfect cut! 48, ...) are not damage. For
//    DPS count personal damage marks on the actor you hit and ignore heals and zero marks; personal
//    marks on your own player are damage taken. state.combatLog() delivers all of this pre-classified.
//    A projectile whose dx/dy is your tile is an incoming ranged or magic attack.
//      objects: { id, x, y, plane, type, dist, name, actions[] }
//    `anim` is the live animation id (-1 = none), which is how attack telegraphs are
//    read (see the Jad Prayer Helper plugin). `walk` is the click-to-walk destination
//    as { x, y, fx, fy, src } in tiles plus raw fine units, or null.

await rtx.plugin.state.varps("659,3274");  // comma-separated varp ids (string capped at 200 chars)
await rtx.plugin.state.varpsLong("12932,12933"); // long-typed varps: the full 64-bit value of each, as a decimal
//    string ("{"12932":"13019417610"}"); varps() would give only the low 32 bits of these
await rtx.plugin.state.varDomainStores(); // -> { stores: { "<domain>": { src, ptr, live, div, count, vt } },
//    vars: { "6:<id>": v, "9:<id>": v } }: the live var store of each script-visible domain, resolved the
//    way the client binds them for scripts; clan (6) and player-group (9) values are listed when those
//    stores exist. World (3), region (4) and campaign (8) have no live store: the client never binds them.
// -> { "659": 990, "3274": 120 }           map of id -> raw value

await rtx.plugin.state.varbits([46468, 46463]); // array of varbit ids (<=64)
// -> { "46468": 1, "46463": 100 }          map of id -> live value (backing varp+bits
//                                          resolved from the cache automatically)

await rtx.plugin.state.interface(1184, [4, 10, 15]); // group id + component ids
// -> { group:1184, open:true, hasAbs:true, exact:true,
//      comps:[ { comp:4, sub:-1, text:"Acting Guildmaster Reiniger", vis:1, x, y, w, h },
//              { comp:10, sub:-1, text:"Are you here to sign up...", vis:1, x, y, w, h },
//              { comp:15, sub:-1, vis:1, x, y, w, h } ] }
//    Live text + absolute screen rect of named components of an open interface. e.g. the NPC chat
//    box (1184): comp 4 = NPC name, comp 10 = message, comp 15 = the continue button. open:false
//    when that interface isn't showing. x/y/w/h are screen coordinates: the origin comes from the
//    engine's own sub-interface table (exact:true), so every attached group resolves without any
//    per-interface knowledge. vis is 1 only when the component is actually drawn (neither it nor
//    any ancestor is hidden); a hidden comp still reports its rect, so skip vis:0 before highlighting.
//    Every comp also carries obj (item id of an item slot, 0 otherwise), amt (its stack size), spr
//    (sprite id) and col (fill / text / tint colour as an RGB int). A templated grid such as a trade
//    offer lists one entry per filled slot with sub = the slot index, so
//    state.interface(335, [14, 17]) gives both sides of a trade as {sub, obj, amt} rows.

await rtx.plugin.state.interfaceGroup(919); // one OPEN interface group id
// -> { widgets:[ { t:[group,comp,sub], d:<depth>, p:<parentComp>, r:[x,y,w,h], a:[absX,absY]?,
//                  ty, x:<text>, s:<sprite>, it:<itemId>, n:<amount>, col:"RRGGBB"?, v:1? }, ... ] }
//    The FULL live widget tree of one open group (what the Interfaces tab shows) --
//    use when you need every component rather than a few named ones. Heavier than
//    state.interface; poll it sparingly. ty is the component class as the engine defines it
//    (layer, rect, text, graphic, model, line; item for an item icon). v:1 marks widgets that are
//    drawn right now; widgets without v are hidden (their own entry or an ancestor). col is the
//    fill / text / tint colour of rect, text and graphic widgets.

await rtx.plugin.state.varcs([1118, 1119]); // array of varc-int ids (<=64)
// -> { "1118": 384, "1119": 2 }            map of id -> live varc value (0 when absent)

await rtx.plugin.state.gameTick();
// -> number: the server tick counter, advancing once per 600ms game tick
// -> null    when the client cannot be read (not logged in / not tracked)
//    Tick-aligned timing: sample it and act on the CHANGE, never on the absolute value,
//    which is not zeroed at login and is not comparable between clients.

await rtx.plugin.state.ports();
// -> { resources:[ { name:"Chimes", qty, sprite }, ...9 ],
//      tradeGoods:[ { name:"Plate", qty, item }, ...7 ],
//      buildings:[ { name:"Bar", level }, ...14 ],
//      ships:[ { nameParts:[a,b,c], voyageId,
//                status:'ready'|'sailing'|'returned'|'damaged', etaMinutes|null }, ... ],
//      shipCount, scrollPieces, distance, zone }
//    Player-Owned Ports account state, decoded by the host (one shared decode) so any
//    plugin can act on it. Ship/voyage NAMES are enum lookups left to the consumer.
//    null until readable (not in-world / port not started).

await rtx.plugin.state.buffs();
// -> { cycles, buffs:[ { struct, name, sprite, item, kind, timer, secs, exact,
//                        endCycle, remainMs, count }, ... ], debuffs:[ ... ] }
//    One entry per buff-bar slot the game has bound to a buff STRUCT (`struct`, the
//    definition the client drew the slot from; `name` is its display name). `exact` is
//    true when the countdown came from the game's own end-cycle variable: `endCycle` is
//    in CLIENTCLOCK cycles (50/s, `cycles` = now), `remainMs` the exact time left, and
//    `secs` matches the number the bar draws (1 + remaining/50). With `exact` false,
//    `secs` is parsed from the bar text ("2m" rounds) and `timer` holds that text.
//    `count` is the stack count var (e.g. Bloodlust stacks) when the game has one.

await rtx.plugin.state.clientState();
// -> { cutscene, inCutscene, options:[44 ints] }
//    cutscene: the running cutscene id, -1 when none. options: the client's 44 option values
//    (graphics, audio and interface settings) by id; names are not carried by the client.

await rtx.plugin.state.cooldowns();   // -> { cooldowns:[ ... ] } (legacy engine registry; may be empty)
await rtx.plugin.state.perks();       // -> { items:[ ... ] } augmented gear + perks

await rtx.plugin.state.actionBar();
// -> { bars:[ { bar, group, slots:[ { slot, id, item, name, key, mod,
//      castable, cd }, ... ] }, ... ] }
//    One entry per visible action bar (main 1430 + secondaries). Per slot:
//    name + bound keybind (key/mod 0 none|1 shift|2 ctrl|3 alt), castable
//    (false = greyed/can't cast), and cd = the on-slot cooldown text
//    ("" = ready, e.g. "44s" / "1:23"). This is the reliable cooldown source.

await rtx.plugin.state.container(containerId);  // e.g. 93 backpack, 95 bank, 623 money pouch
// -> { present, count, cap, items:[ [slot, id, stack, name], ... ] }
//    Generic container read (powers the Storage tab: rune pouch, quiver, nexus, etc.).

await rtx.plugin.state.itemExtra(containerId, itemId);
// -> { present, key:{ <k>:<v>, ... }, pos:[ ... ] }
//    An item's Extra_ints (key->value), e.g. rune-pouch / quiver / massive-pouch packing.

await rtx.plugin.state.pets();
// -> [ { name, category:'Skilling'|'Boss'|'Other', skill:<name>|null,
//        obtained:bool, source:<how-to-unlock text>, item:<iconItemId>, icon:<dataURL|''> }, ... ]
//    Every pet (skilling + boss + other) with live obtained status (varp bitfield).

await rtx.plugin.state.bosses();
// -> [ { name, mode:'Normal'|'Solo'|..., kills, mode2:<label>|null, kills2:<int>|null, total }, ... ]
//    Per-boss kill counts (the same permanent vars the in-game Beasts kill log
//    reads). mode2/kills2 are the boss's second tracked mode (hard/duo/group)
//    where one exists; total = kills + kills2.

await rtx.plugin.state.encounter();
// -> { struct, name, mode, modeValue, health, healthMax }  or null when not in an instance
//    The boss instance you are in right now, already decoded. mode is the same label the
//    game shows: 'Normal', 'Hard', 'Challenge', 'Story', 'Solo', 'Duo', 'Trio',
//    'Enrage 250%', '4 player', 'Barrier 60%'. It is null for a mode the game itself
//    leaves blank. health/healthMax are live and do move with enrage, so healthMax is the
//    figure to compare against, not any fixed per-boss number. struct/modeValue are the raw
//    varp 10946 / 10950 values if you need to special-case an encounter yourself.

await rtx.plugin.state.hideyHoles();
// -> [ { name, tier:'Easy'|'Medium'|'Hard'|'Master', location, build,
//        fillItems:[ ... ], state, built, filled }, ... ]
//    All 58 Treasure Trail hidey-holes. state: 0 = not built, 1 = built/empty,
//    2 = built/filled (built = state>=1, filled = state===2). build = the per-tier
//    construction materials text; fillItems = the 3 emote items it stores.

await rtx.plugin.state.achievements();
// -> [ { id, name, description, reward, points, complete:bool, requirementsNeeded:int,
//        combatMasteryTier:'Easy'|'Medium'|'Hard'|'Elite'|'Master'|'Grandmaster'|null,
//        requirements:[ { description, current, target, complete, varbits:[...], varps? } ] }, ... ]
//    complete = at least requirementsNeeded of the requirements are satisfied. combatMasteryTier
//    is set only for combat achievements.
//    Every trackable achievement from the live cache, with completion computed from the
//    live vars: a requirement is complete when current >= target; the achievement is
//    complete when all requirements are. `varbits` lists the source varbit ids. Bit-flag
//    requirements (unlock checklists) read one bit of a varbit and report target 1;
//    varp requirements sum the listed `varps` (varbits is [] for those).

await rtx.plugin.state.achievement(id);
// -> the single achievement record above for `id`, or null if it isn't trackable.
//    e.g. const a = await rtx.plugin.state.achievement(385);
//         if (a && a.complete) { ... }   // "Shattering Worlds I" done?

await rtx.plugin.state.skillBonus();
// -> [ { skill:"Attack", bonus:1033436.5 }, ... ]
//    Unspent Bonus XP per skill (skills with none are omitted). Values match the
//    in-game skill tooltips (the game stores tenths; this is already /10).

await rtx.plugin.state.dailies();
// -> { available: { star, etree, dmob, sink, chin, ff, goebie, famil },  // booleans
//      resets: { now, daily, weekly, monthly },                          // epoch ms, 00:00 UTC
//      varbits: { "<id>": value, ... },                                  // raw tracker values
//      vos: { a, b, hour, src:'live'|'community', n? } | null }          // Voice of Seren
//    D&D tracker state. `available` uses the same tests as the D&D Tracker tab's
//    notification bells: shooting star window, evil tree, demon flashmob, sinkhole,
//    Big Chinchompa, Fish Flingers, goebie supply run, familiarisation. `varbits` is
//    the full raw read the tab derives everything else from. null when unreadable.
//    `vos` is this hour's Voice of Seren clan pair (codes 1 Iorwerth, 2 Trahaearn,
//    3 Crwys, 4 Cadarn, 5 Amlodd, 6 Meilyr, 7 Hefin, 8 Ithell): read live when the
//    player is in Prifddinas, otherwise the community-reported value (n = reports);
//    null when neither source has this hour's pair.

await rtx.plugin.state.quests();
// -> [ { id, name, difficulty:'Novice'|...|'Special'|null, status, statusText }, ... ]
//    Every listed quest with live progress. status: 0 = not started, 1 = in progress,
//    2 = complete, -1 = no tracker in the game data (statusText 'Unknown').

await rtx.plugin.state.quest(id);
// -> { id, name, difficulty, status, statusText,
//      requirements: { questPoints:{need,have,ok}|null,
//                      skills:[ { skill, need, have, ok }, ... ],
//                      quests:[ { id, name, complete }, ... ], missing },
//      journal: { description, startPoint, requiredItems, combat, xpRewards,
//                 otherRewards, length, age, area } }   // each string|null
//    One quest in full: requirements judged against the live account, plus the
//    journal's own info straight from the game cache (strings may contain <br>
//    line breaks, as the journal stores them). null when `id` is unknown.

await rtx.plugin.state.mysteries();
// -> [ { site:"Kharid-et", name:"Breaking the Seal", points:5, solved:bool,
//        stage: { value, max } | null }, ... ]
//    Every Archaeology mystery grouped by dig site with live solved status. stage
//    mirrors the in-game journal's own progress dispatcher (e.g. value 3 of max 6);
//    null = a page/collection-driven mystery with no stage var.
```

### cache.read (static game data)

```js
await rtx.plugin.cache.itemInfo(id);  // -> object: item metadata (name, value, ...)
await rtx.plugin.cache.itemIcon(id);  // -> string: PNG data URL ("" if none)
await rtx.plugin.cache.sprite(id);    // -> string: PNG data URL
await rtx.plugin.cache.varbitMap();   // -> { "<varpId>": [[varbitId, lsb, msb], ...], ... }
await rtx.plugin.cache.varbitDomainMap(); // -> { "<domain>": { "<var>": [[varbitId, lsb, msb], ...] } }
//    the non-player domains: 1 npc, 2 client (bit fields over varc ints), 3 world, 4 region,
//    5 object (item instance keys, see state.itemExtraInts), 6 clan, 7 clan settings, 8 campaign
await rtx.plugin.cache.varbitDomains();   // -> { "<domain>": { n, var: [min, max], vb: [min, max], sample } }
await rtx.plugin.cache.varDefs(archive);  // -> { archive, n, types: { "<varId>": subtype }, flags: { "<varId>": bits } }
//    archive 60 player, 61 npc, 62 client, 63 world, 64 region, 65 object, 66 clan, 67 clan
//    settings, 68 campaign, 75 player group; `types` lists only vars whose value type is not int
//    (CS2 subtype ids: 1 boolean, 33 obj, 36 string, 39 inv, 71 hash64, 73 struct, 110 long, ...)
await rtx.plugin.cache.enumInfo(id);  // -> { "<key>": value, ... }  (id->name/value roster)
await rtx.plugin.cache.paramDef(id);  // -> { type[, int][, str] }  param definition
//    ({} while the host's param reader is unavailable)
await rtx.plugin.cache.modelIcon(id); // -> string: PNG data URL for an interface type-6
//    MODEL comp, keyed by MODEL id ("" if not in the pack)
//    Model ids come only from the host-side cacheIfaceGroup defs, which are NOT brokered;
//    state.interfaceGroup carries no model field. So this is usable only with a model id you
//    already hold (e.g. one you baked into the plugin), not one you can look up at runtime.
await rtx.plugin.cache.abilityConfigs();
// -> { "<Ability Name>": { t, st, l, ag, ac, c, i, s, d, ... }, ..., "_byId": { "<abilityId>": {...} } }
//    Every combat ability from the live cache. t = tier (0 auto-attack, 1 basic, 2 threshold,
//    3 defensive threshold, 4 ultimate, 5 special, 7 utility), st = combat style (1/2 melee,
//    3 ranged, 4 magic, 5 defence, 6 constitution, 29 necromancy), l = level req,
//    ag = adrenaline gain in tenths of a percent, ac = adrenaline cost, c = cooldown in game
//    ticks (0.6 s), i = the ability id action-bar slots carry, d = description. "_byId" is the
//    same set keyed by that ability id; cache.sprite(abilityId) is the ability's icon.
await rtx.plugin.cache.abilityTips();
// -> { "<abilityId>": ["bullet line", ...], ... }
//    Plain-text tooltip bullets per ability (flattened from the game's own CS2 tooltip
//    builders; damage placeholders read "75%-95% damage"). Static; fetch once.
await rtx.plugin.cache.structParams(id); // -> { ints:{ k:v }, strs:{ k:"v" } } one StructType's params
await rtx.plugin.cache.itemParams(id);   // -> { ints:{ k:v }, strs:{ k:"v" } } one item's op-249 params
await rtx.plugin.cache.mapWindow(cx, cy, plane, half, ts);
// -> { w, t, h, wt, cx, cy, p, png, blk, nomove, objs }  top-down terrain render centred on
//    world tile (cx,cy). w = image px, t = px per tile, h = half-size in tiles.
//    png = base64 PNG (RGB, no alpha) - set img.src = 'data:image/png;base64,' + png and
//    drawImage it. half <= 384 tiles each side, ts <= 32 px/tile.
//    CHANGED: this used to return `b64`, base64 RAW RGBA for putImageData. A PNG is ~4x
//    smaller and the browser decodes it natively instead of you walking the string. If you
//    support both client versions, prefer `png` and fall back to `b64` when it is absent.
```

`itemIcon`/`sprite` return data-URL strings you can put straight in `img.src` or a CSS
`background-image` -- not JSON.

`enumInfo(id)` decodes a game **enum** (an id->name or id->value table) to a plain
object keyed by string. Rosters are static, so fetch each one once and cache it.
Combined with `state.varps`, this is how you resolve coded values to names -- e.g. a
Slayer/Reaper task readout:

```js
const [vp, creatures, bosses] = await Promise.all([
  rtx.plugin.state.varps("183,185,4519"),  // 183=slayer count, 185=creature, 4519=reaper packed
  rtx.plugin.cache.enumInfo(1563),          // slayer creatures: id -> name
  rtx.plugin.cache.enumInfo(9197),          // reaper bosses:    id -> name
]);
const slayer = { name: creatures[vp["185"]], left: vp["183"] };
const packed = vp["4519"] | 0;
const reaper = { name: bosses[packed & 0x3f], left: (packed >> 6) & 0x1f };
```

### overlay (visuals only; fire-and-forget)

```js
rtx.plugin.overlay.toast("Saved");           // brief toast
rtx.plugin.overlay.notify("Heads up", 4000); // notification, ttl in ms (0..60000)
rtx.plugin.overlay.highlight(["Goblin","Banker"]); // outline scene entities by name
rtx.plugin.overlay.flashGame();              // flash the game window

// Box ONE NPC by name with an OPTIONAL custom pill label (the plain highlight() above strips
// punctuation, so use this when you need a label such as a tutorial step). Name matches
// case-insensitively; a non-empty label replaces the NPC name on the pill. Empty name clears it.
// Optional tileX/tileY (world coords) box the instance nearest that tile instead of the player.
rtx.plugin.overlay.highlightNpc("Acting Guildmaster Reiniger", "Step 1: talk to me");

// Highlight ONE open chat-option box whose text matches (substring, case-insensitive).
// Pass a string, or an array of candidate texts to match any of them in one read.
// Returns true if an option was matched + boxed. The plugin decides which option and when
// (do your own quest/step validation first); the host just finds the live box and draws it.
await rtx.plugin.overlay.highlightOption("I want to talk about mysteries");

// Highlight the backpack slot holding an item id, if present AND its slot is on-screen
// (scrolled out / panel closed -> nothing drawn, returns false).
await rtx.plugin.overlay.highlightItem(995);

// Highlight an arbitrary screen rect -- e.g. a component rect from state.interface (the NPC
// continue button, etc.). w/h <= 0 clears.
const d = await rtx.plugin.state.interface(1184, [15]);
if (d.hasAbs && d.comps[0]) { const c = d.comps[0]; rtx.plugin.overlay.highlightRect(c.x, c.y, c.w, c.h); }

// SEVERAL boxes at once. Accepts [[x,y,w,h], ...] or [{x,y,w,h}, ...], max 64; [] clears.
// The call REPLACES the whole set, so redraw your own rects each update rather than adding
// to them. Note there is ONE highlight set per game client, shared with highlightRect and
// with the panel's own guides - the last caller wins, and clearing clears everything.
await rtx.plugin.overlay.highlightRects(d.comps.map(c => [c.x, c.y, c.w, c.h]));

// Text drawn over the game in the same coordinates (up to 32 labels, 90 chars each). style 0 is bare
// text with its left edge at x, centred on y; style 1 is a pill (dark rounded box) centred on x,y.
// rgb is an RGB int (-1 = the game's yellow). Each call replaces the previous set; [] clears.
rtx.plugin.overlay.uiLabels([{ x: c.x + 18, y: c.y - 12, text: 'Buy 30.0m | Sell 29.5m', style: 1 }]);
rtx.plugin.overlay.uiLabels([]);
await rtx.plugin.overlay.highlightRects([]);   // clear

// Draw ground markers on world tiles (the same primitive the clue/quest guides use). Up to 64
// tiles, each { x, y, plane, label }. Replaces the previous set; pass [] to clear.
// Anything beyond 64 is dropped silently, so keep a set you send within the cap rather than
// relying on the tail being rendered.
rtx.plugin.overlay.guideTiles([{ x:3221, y:3218, plane:0, label:"dig here" }]);

// THE FIRST LINE OF A LABEL IS A MATCH KEY, NOT JUST TEXT. If it names a cache loc near
// the tile, the mark takes that object's 3D footprint prism; otherwise it stays a flat
// 1x1 tile. A leading '-' makes that line MATCH-ONLY: it still resolves the footprint but
// is not drawn, and every line after it still shows. That is how you put the ACTION first
// while keeping the prism -- the house style for guide tiles:
//
//   label: "-Cliffside\nClimb\nCliffside"   ->  renders "Climb" over "Cliffside"
//   label: "-Cliffside\nClimb"              ->  renders "Climb" alone
//   label: "Cliffside\nClimb"               ->  renders "Cliffside" over "Climb" (old order)
//
// Labels are capped at 95 characters and 4 drawn lines; compose accordingly.

// Optional x2/y2 (x/y = SW corner, x2/y2 = NE corner) turns a mark into one flat ground
// rect spanning those tiles -- use for long walkways or zones whose interactable loc is
// only a 1x1 end piece (e.g. an agility log).
rtx.plugin.overlay.guideTiles([{ x:2474, y:3430, x2:2474, y2:3435, plane:0, label:"Log balance" }]);

// Optional color tints a mark ('#rrggbb' or a packed int; omit for the default accent).
// Optional color2 makes a TWO-TONE tile: the fill splits along the tile's SW->NE diagonal,
// color keeping the north-west half -- the same rendering as two-tone user tile markers.
// (Two-tone applies to single tiles; an area rect uses color alone.)
rtx.plugin.overlay.guideTiles([{ x:3221, y:3218, plane:0, label:"swap", color:"#57C6E0", color2:"#C07AE0" }]);
rtx.plugin.overlay.guideTiles([{ x:3221, y:3218, plane:0, label:"stand", merge:true },
                               { x:3222, y:3218, plane:0, label:"stand", merge:true }]);   // same label + merge: one zone, one label

// Open the in-client wiki browser on a search term ('' = the wiki home page). The pane is
// hard-locked to runescape.wiki: a plugin chooses the page, never the site.
rtx.plugin.overlay.wikiSearch("Abyssal whip");

// Have the game itself point the way to one target: its arrow over the target, its chevrons
// at the player's feet turning towards it and its trail of markers on the ground. The target
// is looked up around the player once a second and the nearest match is used; nothing shows
// while it is out of view. One request per plugin; it ends when the plugin is closed.
rtx.plugin.overlay.pointAt("npc", "Banker");        // a name, part of one, or an id
rtx.plugin.overlay.pointAt("object", "Bank chest");
rtx.plugin.overlay.pointAt("tile", "3221, 3218");    // or "x, y, plane"
rtx.plugin.overlay.pointClear();

rtx.plugin.overlay.clearHighlight();         // clear both highlight layers

// A small floating HUD strip over the game showing ability icons -- rotation playback,
// switch reminders, and the like. One per plugin, host-rendered and draggable; call again
// to update it (each call replaces the content), pass null (or an empty cur) to close it.
// cur = the abilities to press NOW (drawn large, optional keybind badge, max 6);
// next = upcoming (small + dimmed, gap = ticks until it, max 6). Icons come from the
// ability id (the same id cache.sprite serves).
rtx.plugin.overlay.hudAbilities({
  title: "Zamorak opener", sub: "Tick 4 - dive out",
  cur:  [{ id: 30331, key: "S+3" }],
  next: [{ id: 23727, gap: 3 }, { id: 23729, gap: 6 }],
});
rtx.plugin.overlay.hudAbilities(null);       // close the strip

// Big centre-screen banner text (the Dungeoneering boss-warning channel). '' clears.
rtx.plugin.overlay.centerText("DODGE - icicles!");
```

### notify.os (Windows notifications)

```js
rtx.plugin.notify.windows("RuneToolsX", "A ship has returned");
```

An OS-level toast outside the game window. Hard-capped at one per 10 seconds -- send it
on real events (a ship returned, a rare drop), never on a timer.

### notify.discord (the user's Discord webhook)

```js
const r = await rtx.plugin.notify.discord("Ship 3 returned with 120 chimes");
// r = { queued: true } or { error: "not configured" | "rate limited" }
```

Posts plain text to the webhook the user entered in Settings. The plugin never sees the URL:
the host holds it sealed, prefixes every message with the plugin's id, strips all mentions so
nothing can ping a user or role, caps the text, and allows one message per 10 seconds per
plugin. Not configured is a normal state; handle it quietly.

### clipboard (copy-only)

```js
rtx.plugin.clipboard.copy(JSON.stringify(plan));
const code = await rtx.plugin.clipboard.paste();  // requires the clipboard.read scope
```

`copy` puts text on the user's clipboard (capped 64 KB). `paste` reads the clipboard's text
back and needs the separate `clipboard.read` scope (approved like any other) -- call it only
from a direct user action (an Import button), never on a timer, and expect any text at all.
Keyboard paste inside the in-game view is unreliable, which is what this call is for.

### sound

```js
rtx.plugin.sound.play("alert1");   // play a built-in RuneTools alert sound by name
```

### storage (per-plugin, per-account)

```js
await rtx.plugin.storage.set("key", value);  // value is any JSON-serializable value
await rtx.plugin.storage.get("key");         // -> the stored value, or null
await rtx.plugin.storage.keys();             // -> ["key", ...]
```

Keys are namespaced to your plugin id and the active account; another plugin cannot read
them. Key names are capped at 64 chars and each value at ~256 KB.

### prices (scope: cache.read)

Real-time RS3 Grand Exchange prices. The data is relayed through the RuneTools
server (the single consumer of the upstream price API) and cached by the launcher,
so calling these never generates upstream traffic. `latest` refreshes about every
90 seconds; call it at most that often.

```js
const prices = await rtx.plugin.prices.latest();   // { "2": { high, highTime, low, lowTime }, ... }
const items  = await rtx.plugin.prices.mapping();  // [{ id, name, limit, value, lowalch, highalch, members, ... }]
const one    = await rtx.plugin.prices.item(2);        // just that item: { "2": { high, ... } }
const some   = await rtx.plugin.prices.item([2, 6]);   // up to 50 ids per call
```

Watching a handful of items? Use `item` -- it answers from a local parsed cache, so it
is cheap and allowed 4 calls/s. `latest` and `mapping` return the full payloads (large;
cache them) and are limited to 1 call per 2 s.

### ui (always available)

```js
rtx.plugin.ui.setHeight(420);     // resize the plugin frame (60..4000 px)
rtx.plugin.ui.setTitle("My Tool");// reserved (no-op for now)
```

### console (always available)

```js
rtx.plugin.console.info("loaded", { version: 3 });   // objects are printed as JSON
rtx.plugin.console.debug(...) / .warn(...) / .error(...)
const log = rtx.plugin.console.scoped("combat");       // lines carry a tag the panel can filter on
log.warn("no target");
```

Lines land in the client's **Console** panel (Developer), stamped by the host with your plugin id
and runtime, alongside the client's own messages and the launcher log. The panel filters by level,
source and tag, searches, and copies single lines or the whole view. Console is write-only: a
plugin never reads the console, other plugins' lines, or the launcher log. Limits: 4000 characters
per line, 60 lines per second per plugin (a burst of 120), past which lines are dropped and the
panel says so. The plugin's own `console.log` still goes to its frame only; use `rtx.plugin.console`
for anything you want to see in the panel.

### Theming (automatic)

Your plugin runs in its own document, so it inherits none of the client's CSS. The
host injects its live theme as custom properties and pushes updates when the user
changes their appearance settings, so styling against these keeps you in step with
the rest of the client (accent colour included):

```css
.button   { background: var(--rtx-accent, #8c6ffd); }
.card     { background: var(--rtx-panel, #1a1b23); border: 1px solid var(--rtx-border, rgba(255,255,255,.08)); }
.subtle   { color: var(--rtx-text-mute, #8b8b9e); }
```

Available: `--rtx-accent`, `--rtx-accent-hi`, `--rtx-accent-lo`, `--rtx-accent-rgb`,
`--rtx-accent-ring`, `--rtx-bg`, `--rtx-bg-elev`, `--rtx-bg-elev-2`, `--rtx-panel`,
`--rtx-panel-2`, `--rtx-win-bg`, `--rtx-border`, `--rtx-border-hi`, `--rtx-text`,
`--rtx-text-dim`, `--rtx-text-mute`, `--rtx-ok`, `--rtx-warn`, `--rtx-err`,
`--rtx-font-ui`, `--rtx-font-size`. Always pass a fallback: a value can be absent on
an older client. Your `<head>` is spliced in after these, so anything you define wins.

### settings (always available)

Declare a settings schema once at boot and RuneTools renders standard controls for your
plugin on its own Preferences page (a "Plugins" card). No scope is needed: values are
stored host-side per plugin and per account, and only your plugin sees them.

```js
const values = await rtx.plugin.ui.settings([
  { key: 'compact',  type: 'toggle', label: 'Compact layout', default: false,
    hint: 'Smaller rows and icons' },
  { key: 'style',    type: 'select', label: 'Combat style', default: 'melee',
    options: [{ v: 'melee', label: 'Melee' }, { v: 'ranged', label: 'Ranged' }] },
  { key: 'volume',   type: 'slider', label: 'Alert volume', min: 0, max: 100, step: 5, default: 70 },
  { key: 'nickname', type: 'text',   label: 'Display name', default: '' },
]);

rtx.plugin.settings.on(v => applySettings(v));   // fires on declare and on every change
const now = await rtx.plugin.settings.get();     // current values on demand
```

Limits: 24 controls, key `[A-Za-z0-9_.-]` up to 32 chars, labels 48 / hints 120 chars,
select up to 12 options, text values 200 chars. Values are clamped to the schema on
every write. The storage key `~settings` in your plugin store is reserved for this.

## Lua plugins

RuneTools also runs plugins written in Lua 5.4. A Lua plugin is the same product as an HTML
plugin from the user's side: it is discovered from the same folders, installed from the same
Browse tab, asks for the same scopes on the same consent card, keeps its settings on the same
Preferences page and hot-reloads the same way. What changes is the runtime: instead of a sandboxed
frame the launcher runs your `main.lua` in its own sandboxed Lua state, on the host's refresh
cadence, and renders the panel you describe.

Why offer both: the official RuneScape client is adding a Lua plugin API. Its shape is not public
yet, so the RuneTools Lua SDK mirrors the JavaScript SDK one to one rather than guessing at
Jagex's namespaces. When that API ships, plugins written against `rtx.*` keep working unchanged
(the host can map an official data source under the same method names, and an API profile can
alias another namespace onto `rtx` without touching plugin code).

### Layout

```
my-plugin/
  manifest.json
  main.lua
  util.lua          (optional modules, loaded with require)
  data/rows.json    (optional data files, read with rtx.plugin.readFile)
```

```json
{
  "rtxPluginManifest": 1,
  "apiVersion": "1.0",
  "runtime": "lua",
  "id": "com.yourname.tool",
  "name": "Your Tool",
  "version": "1.0.0",
  "author": "Your Name",
  "description": "What it does.",
  "main": "main.lua",
  "scopes": ["state.read", "overlay", "storage"],
  "minHostVersion": "2.5.0"
}
```

`runtime: "lua"` selects the Lua runtime and `main` names the entry chunk (default `main.lua`,
a bare filename). `entry` is not used. Everything else is identical to an HTML plugin, including
the `id` rules, the scopes and the consent flow. Put the folder in
`%USERPROFILE%\RuneToolsX\plugins-dev\<your-id>\` for development; saving any file in it reloads
the plugin.

### The `rtx` table

Your chunk runs once at load with a global `rtx` table. It is generated from the host's method
table, so it has exactly the namespaces and method names of the JavaScript SDK (`rtx.plugin.state.*`
becomes `rtx.state.*`, and so on for `cache`, `overlay`, `notify`, `clipboard`, `sound`, `storage`,
`prices`, `ui`, `settings`). Scopes gate the same methods, and the same rate limits apply.

The one deliberate difference: **calls are synchronous**. There are no promises.

```lua
local inv = rtx.state.inventory()          -- table or nil
if inv then print(#inv.items .. " items") end

local v, err = rtx.state.player()          -- err is a string when the call failed
if not v then rtx.warn("player: " .. tostring(err)) end
```

- A method returns its value (tables for JSON objects and arrays, `nil` for JSON null).
- On failure it returns `nil, reason` and sets `rtx.lastError`. Reasons are the same strings the
  JavaScript SDK rejects with: `scope not granted: <scope>`, `rate limited`, `unknown method`.
- A few methods are computed asynchronously by the host (`state.quests`, `state.quest`,
  `state.pets`, `state.bosses`, `state.encounter`, `state.dailies`, `state.mysteries`, `state.varbits`, `state.varcs`,
  `state.achievements`, `ui.settings`, `overlay.highlightOption`, `overlay.highlightItem`). The first
  call with a given argument list returns `nil, "pending"`; the value arrives on a later call, then
  stays fresh as you keep calling. Read them in your `tick` handler and treat `nil` as "not yet".

```lua
rtx.plugin.id()               -- "com.yourname.tool"
rtx.plugin.apiVersion()       -- "1.0"
rtx.plugin.runtime()          -- "lua"
rtx.plugin.runtimeVersion()   -- "Lua 5.4.7"
rtx.plugin.grantedScopes()    -- { "state.read", "overlay", "storage" }
rtx.plugin.hasScope("overlay")
rtx.plugin.readFile("data/rows.json")   -- a text file inside your plugin folder, or nil
rtx.json.encode(value) / rtx.json.decode(text)
rtx.call("state.scene", { 20 })         -- the generic form every namespace method uses
```

### Events, timers and logging

```lua
rtx.on("ready", function() end)             -- once, on the first host tick after load
rtx.on("tick", function() end)              -- the host refresh, about 4 times a second
rtx.on("state", function(snapshot) end)     -- the same snapshot the JavaScript "state" event carries (state.read)
rtx.on("settings", function(values) end)    -- a settings value changed on the Preferences page
rtx.events.on("skill_update", function(ev) end)   -- game events, same kinds and fields as the JavaScript SDK
rtx.events.on("*", function(ev) end)
rtx.events.off("skill_update", fn)

local id = rtx.timer.after(5, function() end)     -- seconds; resolved on the host tick
local id2 = rtx.timer.every(60, function() end)
rtx.timer.cancel(id2)

print("hello", 42, { a = 1 })   -- objects are printed as JSON
rtx.console.debug(...) / .info(...) / .warn(...) / .error(...)
local log = rtx.console.scoped("combat")   -- tagged lines, same as rtx.plugin.console.scoped in JavaScript
rtx.log / rtx.debug / rtx.warn / rtx.error  -- shorthands for rtx.console.*
```

Every line goes to the plugin's own console strip under its panel and to the client's Console panel
(Developer), stamped with the plugin id and the Lua runtime; the same 60 lines per second budget as
HTML plugins applies.

Handlers run inside the host's tick. An error in a handler is logged to the plugin console and
the plugin keeps running; an error in the main chunk stops the plugin. Each tick has an execution
budget (about 40 million instructions or 1.5 seconds) and each plugin a 64 MB memory limit; a
plugin that keeps failing eight ticks in a row is stopped until it is saved (developer folder) or
reinstalled.

### Panels

A Lua plugin describes its panel as a tree of widgets and the host renders it with the RuneTools
theme. Publish a new tree whenever your data changes; the host only re-renders when the tree
differs.

```lua
rtx.ui.render({
  { type = "heading", text = "Drop log" },
  { type = "text", text = "Ground items seen this session.", muted = true },
  { type = "card", title = "Totals", children = {
    { type = "kv", items = { { "Items", tostring(n) }, { "Value", fmt(gp) } } },
    { type = "progress", value = n, max = 100, label = "Progress" },
  } },
  { type = "row", children = {
    { type = "button", id = "clear", label = "Clear", onClick = function() reset() end },
    { type = "toggle", id = "alerts", label = "Alert on rare drops", value = alerts,
      onChange = function(v) alerts = v end },
  } },
  { type = "table", columns = { "Item", "Qty" }, rows = rows },
})
rtx.ui.clear()
rtx.ui.settings({ { key = "alerts", type = "toggle", label = "Alerts", default = true } })   -- same schema as ui.settings
rtx.settings.get()
```

Widgets: `heading{text}`, `text{text, muted, color}`, `card{title, children}`, `row{children}`,
`col{children}`, `button{id, label, primary, disabled, onClick}`, `toggle{id, label, value, onChange}`,
`input{id, label, value, placeholder, onChange}`, `select{id, label, value, options = {{v, label}, ...}, onChange}`,
`progress{value, max, label, text}`, `table{columns, rows}`, `kv{items = {{k, v}, ...}}`,
`badge{text, tone = "ok" | "warn" | "err"}`, `sep`, `spacer{h}`. A tree holds at most 500 widgets,
eight levels deep; text is plain (never HTML). Callbacks receive the new value for `toggle`,
`input` and `select`. Below the panel the host shows a collapsible console with everything the
plugin printed and every error it raised.

### Modules and the sandbox

`require("./util")`, `require("lib.colors")` and `require("sub/mod")` load `.lua` files inside the
plugin folder, once, and cache the returned value. Paths never leave the folder. The standard
`string`, `table`, `math`, `utf8` and `coroutine` libraries are available, plus `os.time`,
`os.clock`, `os.date` and `os.difftime`. There is no `io`, `debug`, `package`, `dofile` or
`loadfile`, `load` only compiles text, and nothing in the plugin can reach the file system,
the network, other plugins or the host page. Everything reaches the game through `rtx.*`, under the
scopes the user approved.

### Porting an HTML plugin

- `await rtx.plugin.state.inventory()` becomes `rtx.state.inventory()`.
- `rtx.plugin.on("tick", fn)` becomes `rtx.on("tick", fn)`; `rtx.plugin.events.on` becomes `rtx.events.on`.
- Replace the page markup with `rtx.ui.render(tree)`; replace DOM event handlers with widget callbacks.
- `manifest.json`: add `"runtime": "lua"` and `"main": "main.lua"`, drop `entry`. Scopes and `id` stay.
- The editor stubs in `rtx.d.lua` (shipped next to `plugin-sdk.js`) give completion and types for the whole API in any editor that understands LuaLS annotations.

### Reference plugin

```lua
-- main.lua
local invCount, alertFull = -1, rtx.storage.get("alertFull") == true

local function render()
  rtx.ui.render({
    { type = "heading", text = "Sample Lua Plugin" },
    { type = "card", children = {
      { type = "kv", items = { { "Items in inventory", invCount >= 0 and tostring(invCount) or "--" } } },
      { type = "toggle", id = "alertFull", label = "Toast when the inventory is full", value = alertFull,
        onChange = function(v) alertFull = v; rtx.storage.set("alertFull", v); render() end },
      { type = "button", id = "test", label = "Test overlay toast", primary = true,
        onClick = function() rtx.overlay.toast("Hello from Lua") end },
    } },
  })
end

rtx.on("ready", render)
rtx.on("tick", function()
  local inv = rtx.state.inventory()
  if not inv then return end
  local n = #(inv.items or {})
  if n ~= invCount then invCount = n; render() end
  if n >= 28 and alertFull then rtx.overlay.toast("Inventory full") end
end)
```

## Complete example

```html
<!DOCTYPE html>
<html>
<head><meta charset="UTF-8">
  <style>
    body { font-family: 'Segoe UI', sans-serif; background:#14151c; color:#e8e8ef; padding:14px; }
    .count { font-size:1.6em; font-weight:700; color:#7c5cfc; }
  </style>
</head>
<body>
  <div>Inventory: <span class="count" id="n">--</span></div>
  <script>
    async function init() {
      await rtx.plugin.ready();
      rtx.plugin.on('tick', refresh);   // refresh on the host cadence, not a setInterval
      refresh();
    }
    async function refresh() {
      try {
        const inv = await rtx.plugin.state.inventory();   // null when not in-game
        const count = inv ? inv.count : 0;
        document.getElementById('n').textContent = count;
        if (inv && inv.count >= 28) rtx.plugin.overlay.toast('Inventory full!');
      } catch (e) { /* scope missing or transient read error */ }
    }
    init();
  </script>
</body>
</html>
```

## Pointer coordinates in panel UI

Never compute element-local mouse coordinates with raw `e.clientX - rect.left` (or `offsetX`)
arithmetic. Panel bodies render under a CSS zoom (the user's font-size preference) and the page
under the launcher's device scale, so raw coordinates land up-left of the cursor by the zoom
factor. Use the page globals `uiEvPt(e, el)` (pointer position in `el`'s own CSS pixels) or
`uiZoomOf(el)` (the effective zoom to divide by), provided by the launcher core.

## The sandbox

Plugins run in a sandboxed frame and use the `rtx.plugin` APIs documented above:

- A plugin sees only the data the scopes it was granted allow, and has no access to the host page, other plugins, or other accounts. Consent is recorded per character, so being enabled on one character grants nothing on another.
- The frame CSP blocks network and dynamic code (`fetch`/XHR/WebSocket/`eval`/`Function`), so bundles must be fully self-contained (local `.html/.css/.js/.svg/.png/.woff2`).

## Packaging

A submission is one `.zip` with `manifest.json` at its root. Upload limits: at most 200 files,
2 MB per file, 5 MB uncompressed, 8 MB zip. Allowed types: `.html`, `.htm`, `.css`, `.js`, `.mjs`,
`.lua`, `.json`, `.txt`, `.md`, `.svg`, `.png`, `.jpg`, `.jpeg`, `.gif`, `.webp`, `.woff`, `.woff2`, `.wav`.
A bundle is rejected if it isn't a valid zip, the manifest is missing or invalid, a file type isn't
allowed, a path is unsafe (`..` or a leading `/`), the `entry`/`main`/`icon` file is missing, or
anything references a remote resource. A Lua bundle (`runtime: "lua"`) is installed whole: every
allowed file lands in the plugin folder so `require` and `rtx.plugin.readFile` can reach it. (`eval` / `fetch` / `WebSocket` / dynamic `import` are flagged for the reviewer but
are already blocked by the frame CSP.)

## Submission and signing

Plugins are free, and so is the optional account that submitting and voting need. Browsing the
catalog, reading any plugin's full source, and downloading the SDK or a signed plugin are public.

1. **Submit** (login). Upload your zip in the developer area. The server runs the static checks and
   creates a **pending** version. A plugin `id` belongs to the first account that submits it.
2. **Review.** A RuneTools admin reads the full source. Plugins are human-readable, so review is
   transparency-based, not opaque scanning.
3. **Approve or reject.** On approval the server signs the exact zip bytes and the version goes
   **live** in the public catalog and the in-client Browse tab. Rejections include notes, and a live
   version can be revoked later.

## Signature verification

Approved bundles are signed with **ECDSA P-256 over SHA-256**, over the exact bytes of the `.zip`.
The signature and metadata ship in the download headers (`X-Plugin-Signature`, `-Alg`, `-Hash`,
`-Version`, `-Slug`), and the public key is served at `GET /api/plugins/pubkey`. The desktop client
pins that key and refuses any bundle whose signature does not verify, so only reviewed, signed
bundles install in production. Local `plugins-dev` bundles are unsigned and labelled "Unsigned
developer plugin"; catalog installs show "Verified, signed by RuneTools".
