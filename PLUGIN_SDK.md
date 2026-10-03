# RuneTools Plugin SDK

Build tools inside RuneTools as custom tabs and overlays, in HTML, CSS and JS or in Lua. A plugin
reads live game state and draws overlays through `window.rtx.plugin` (HTML) or the `rtx` table
([Lua plugins](#lua-plugins)); both runtimes expose the same methods, scopes, events and limits.

## Quick start

1. Create the folder `%USERPROFILE%\RuneToolsX\plugins-dev\com.yourname.tool\`. The folder name is
   the plugin id.

```
com.yourname.tool/
  manifest.json
  index.html
```

2. `manifest.json`, required fields only.

```json
{
  "rtxPluginManifest": 1,
  "apiVersion": "1.0",
  "id": "com.yourname.tool",
  "name": "Your Tool",
  "version": "1.0.0",
  "entry": "index.html",
  "scopes": ["state.read", "overlay"]
}
```

3. `index.html`: wait for the handshake, read on the host `tick`, treat `null` as not in game.

```html
<!DOCTYPE html>
<html>
<head><meta charset="UTF-8">
  <style>
    body { font-family: var(--rtx-font-ui, 'Segoe UI', sans-serif); padding: 14px;
           background: var(--rtx-bg, #14151c); color: var(--rtx-text, #e8e8ef); }
    .count { font-size: 1.6em; font-weight: 700; color: var(--rtx-accent, #7c5cfc); }
  </style>
</head>
<body>
  <div>Inventory: <span class="count" id="n">--</span></div>
  <script>
    async function init() {
      await rtx.plugin.ready();
      rtx.plugin.console.info('loaded');
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

4. Enable: open the client's tab picker, category Plugins (next to Browse plugins), open the
   plugin's tab and click Enable plugin on the consent card. Not listed: the manifest is not valid
   JSON, lacks `"rtxPluginManifest": 1`, or the folder is not directly under `plugins-dev`.

5. Reload and output: saving any file in the folder reloads an open plugin window.
   `rtx.plugin.console.*` lines appear in Developer > Console, filtered by plugin id. The frame's
   own `console.log` and uncaught errors are shown nowhere; see [Debugging](#debugging).

> No account or signing is needed for local development. Plugins appear within a couple of
> seconds, no client restart.

## Manifest

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
  "scopes": ["state.read", "overlay", "storage"]
}
```

### Fields

| Field | Type | Required | Notes |
|---|---|---|---|
| `rtxPluginManifest` | number | yes | Must be `1`. |
| `apiVersion` | string | yes | API contract; currently `"1.0"`. |
| `id` | string | yes | Reverse DNS, globally unique, immutable. Also the storage key and the dev folder name (`plugins-dev\<your-id>\`). |
| `name` | string | yes | Tab title. |
| `version` | string | yes | Semver. |
| `author` | string | no | Shown on the plugin page. |
| `description` | string | no | Short summary. |
| `entry` | string | HTML | Entry HTML file inside the bundle. |
| `runtime` | string | Lua | `"lua"` selects the Lua runtime; omit for HTML. |
| `main` | string | Lua | Entry chunk, a bare `.lua` filename; default `main.lua`. |
| `icon` | string | no | Bare filename of an icon inside the bundle. Submission fails if the file is missing. |
| `background` | boolean | no | Runs with no window once granted; overlays stay up. Give it an enabled setting. |
| `scopes` | string[] | yes | Permissions requested; may be `[]`. Request only what you use. |
| `minHostVersion` | string | no | Minimum RuneTools version. |

- `background`: a background plugin gets ticks, state and events off-screen. Opening its window
  restarts it in the window; closing the window restarts it off-screen.

### Scopes

| Scope | Unlocks | Rate |
|---|---|---|
| `state.read` | `state.*`, `text.*`, `events.*` (live state of the current account) | 20/s |
| `cache.read` | `cache.*`, `prices.*` (static game data, Grand Exchange prices) | 20/s; `prices.item` 4/s; `prices.latest`, `mapping` 1 per 2 s |
| `overlay` | `overlay.*` | 6/s |
| `sound` | `sound.play` | 20/s |
| `storage` | `storage.*` (per plugin, per account) | 4/s |
| `notify.os` | `notify.windows` | 1 per 10 s |
| `notify.discord` | `notify.discord` (the user's own webhook) | 1 per 10 s |
| `clipboard` | `clipboard.copy` (nothing is read back) | 1/s |
| `clipboard.read` | `clipboard.paste` (reads clipboard text; ask only if you need it) | 1/s |
| `telemetry` | `telemetry.*` (log files in the plugin's own folder; nothing is read back) | `append` 60/s; `appendMany` 10/s; `export`, `open` 1 per 5 s |
| none | `ui.*`, `settings.*`, `console.*`, `ready()`, `id()`, `apiVersion()`, `grantedScopes()`, `hasScope()`, `on()` | 20/s |

- Approved per character on first enable; the host enforces scopes on every call.
  `grantedScopes()` is empty on a character that has not approved the plugin.
- Adding a scope in an update re-prompts with the new items highlighted; the plugin does not
  mount until approved.
- Removing a scope needs no prompt; the removed scope stops working at once (the host serves the
  intersection of granted and requested).
- Request everything up front and branch on `hasScope()`; a call without its scope rejects with
  `scope not granted: <scope>`.

## How calls work

- The host injects the SDK into your frame. Do not ship `plugin-sdk.js` and do not add
  `<script src>` or other remote references. Use `window.rtx.plugin`.
- Every data call returns a Promise; await it.
- Wait for the handshake: `await rtx.plugin.ready()` resolves once granted scopes and identity
  have arrived. Before that `grantedScopes()` and `id()` return defaults.
- Do not poll in a loop. The host pushes a `tick` event about 4 times a second; read in that
  handler.
- A call rejects with `scope not granted: <scope>`, `rate limited`, or times out after 15 s. A
  state call that cannot read (not logged in, not in game) resolves to `null`.

### Meta calls

#### ready(), id(), apiVersion(), grantedScopes(), hasScope(scope)

| Scope | Arguments | Returns |
|---|---|---|
| none | `hasScope(scope)`: `scope` string | `ready()` a Promise; `id()`, `apiVersion()` strings; `grantedScopes()` a string array; `hasScope()` boolean |

```js
await rtx.plugin.ready();
rtx.plugin.id();                 // "com.yourname.tool"
rtx.plugin.apiVersion();         // "1.0"
rtx.plugin.grantedScopes();      // ["state.read", "overlay", "storage"]
rtx.plugin.hasScope("overlay");  // true
```

### Sandbox

- A plugin sees only what its granted scopes allow; no host page, other plugins or other
  accounts.
- Consent is per character; enabled on one character grants nothing on another.
- The frame CSP blocks network and dynamic code: `fetch`, XHR, WebSocket, `eval`, `Function`,
  dynamic `import`, remote `<script>`.
- Bundles are self-contained local files (`.html`, `.css`, `.js`, `.svg`, `.png`, `.woff2` and
  the other allowed types under [Packaging](#packaging)).

### Limits

Token bucket per method, per plugin.

| Area | Rate | Caps |
|---|---|---|
| Any call not listed | 20/s | times out after 15 s |
| `state.varps`, `varpsLong` | 20/s | id string 200 chars |
| `state.varbits`, `varcs` | 20/s | 64 ids per call |
| `state.combatLog` | 20/s | `max` 2000 per call (default 500); the ring keeps 4096 |
| `state.scene` | 20/s | `range` 1..64 |
| `state.walkable` | 20/s | `r` 1..8 |
| `cache.mapWindow` | 20/s | `half` 384 tiles; `ts` 32 px per tile |
| `prices.item` | 4/s | 50 ids per call |
| `prices.latest`, `mapping` | 1 per 2 s | refreshes about every 90 s |
| `overlay.*` | 6/s | 64 rects; 32 labels of 90 chars; 64 tiles; tile labels 95 chars and 4 lines; `hudAbilities` 6 `cur` and 6 `next`; `notify` ttl 0..60000 ms |
| `notify.*` | 1 per 10 s | |
| `clipboard.copy` | 1/s | 64 KB |
| `clipboard.paste` | 1/s | |
| `storage.*` | 4/s | key 64 chars; value 256 KB |
| `telemetry.append` | 60/s | record 64 KB |
| `telemetry.appendMany` | 10/s | 1000 records per call |
| `telemetry.export`, `open` | 1 per 5 s | `export` 16 MB |
| telemetry files | | 64 MB per file; 512 MB and 200 files per plugin; names 48 chars |
| `console.*` | 60 lines/s, burst 120 | 4000 chars per line |
| `ui.setHeight` | 20/s | 60..4000 px |
| `ui.settings` | 20/s | 24 controls; key 32 chars; label 48; hint 120; 12 select options; text values 200 chars |
| Lua tick | | 40 M instructions or 1.5 s per tick; 64 MB per plugin; stopped after 8 failing ticks |
| Lua panel tree | | 500 widgets; 8 levels |
| Bundle | | 200 files; 2 MB per file; 5 MB unzipped; 8 MB zip |

### Debugging

- Output: `rtx.plugin.console.*` lines land in Developer > Console, stamped with the plugin id and
  runtime; filter by plugin id or by a `console.scoped(tag)` tag.
- Not captured: the frame's own `console.log` and uncaught exceptions. Wrap handlers in try/catch
  and log with `rtx.plugin.console.error`.
- Missing from the list: the manifest is not valid JSON, lacks `"rtxPluginManifest": 1`, or the
  folder is not directly under `plugins-dev`. Nothing is logged for a skipped manifest.
- Rejections: `scope not granted: <scope>`, `rate limited`, a 15 s timeout; state reads resolve
  `null` when the client cannot be read.
- Lua: every line also goes to the console strip under the panel; a handler error is logged and
  the plugin keeps running; an error in the main chunk stops it; eight failing ticks in a row stop
  it until the file is saved (dev folder) or the plugin is reinstalled.

## Events

- Game events require `state.read`.
- Events arrive batched on the tick cadence, one callback per event in capture order; `tick`
  itself is unchanged.
- The capture set is a host setting (Developer > Events); plugins cannot change it. Chat never
  appears.

### Host events

```js
rtx.plugin.on("tick", fn);                // about 4 times a second
rtx.plugin.on("state", fn);               // changed snapshot; requires state.read
rtx.plugin.settings.on(fn);               // settings values changed
rtx.plugin.events.on(kind, fn);           // game events, kinds below
rtx.plugin.events.on("*", fn);            // every kind
rtx.plugin.events.off(kind, fn);
```

| Event | Payload | When |
|---|---|---|
| `tick` | none | the host refresh, about 4 times a second |
| `state` | snapshot | the state snapshot changed; requires `state.read` |
| `settings` | values | a settings value changed (via `rtx.plugin.settings.on`) |
| `ready` | none | Lua only: the first host tick after load; HTML uses `await rtx.plugin.ready()` |

### Game events

| Kind | Fields | Fires when |
|---|---|---|
| `obj_add` | `x, y, plane, item, qty, owner?` | an item appeared on a tile (drop, spawn) |
| `obj_del` | `x, y, plane, item` | an item left a tile (picked up, despawned) |
| `obj_count` | `x, y, plane, item, from, qty` | a ground stack changed quantity |
| `loc_add` | `x, y, plane, loc, type, rot` | a map object was placed or replaced |
| `loc_del` | `x, y, plane, type, rot` | a map object was removed |
| `spotanim` | `x, y, plane, gfx, height, delay` | a graphic played on a tile |
| `spotanim_actor` | `target` (player, npc or tile), `index?, x?, y?, gfx, height, delay, slot` | a graphic played on an actor |
| `projectile` | `form, gfx` and more; fields still being confirmed live | a projectile launched |
| `sound` | `id` and more | a sound effect |
| `area_sound` | `x, y, plane, id, loops, radius` | a positional sound |
| `zone_base` | as captured | the zone the following tile events refer to |
| `zone_clear` | as captured | a zone was cleared |
| `zone_update` | `x, y, plane, items[]` | several tile events batched for one 8x8 zone |
| `skill_update` | `seq, t, wall, op, len, kind, skill, name, level, xp` | a skill level or xp changed |
| `container_update` | `container, flags, slots[] (slot, item, qty), partial` | a container changed |
| `runclientscript` | `script, sig, args` | the server ran a client script |
| `buff_update` | `struct, active, name` | a buff-bar entry was bound or cleared; pair with `state.buffs()` for its timer |
| `varp_set` | `id, value` | the server changed a player variable |
| `varbit_set` | `id, value` | the server set a varbit directly (a varbit is a bit range of its varp, see `cache.varbitDomains`) |
| `varc_set` | `id, value` | the server changed a client variable |
| `run_energy` | `value` | run energy changed |
| `run_weight` | `value` | carried weight changed |
| `ping` | `a, b` | a server ping |
| `ge_offer` | `op, len, hex` | a Grand Exchange offer packet, undecoded |
| `raw` | `op, len, hex` | any other captured opcode |
| `gameTick` | `tick, dtMs` | one per 600 ms server tick |
| `*` | the event | every kind |

## API reference

All `state.*` reads act on the current account shown in the panel. Shapes are the exact JSON the
host returns; log a result during development to see every field.

### state

Scope: `state.read`.

#### state.info()

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | none | object below; `{ in:false }` when not in game |

```js
await rtx.plugin.state.info();
await rtx.plugin.state.player();   // alias of info()
// -> { in:true, x, y, trueTile:{ x, y }, plane, region, lx, ly, anim, moving, interact }
// -> { in:false }
// region = (x>>6)<<8 | (y>>6)
```

| Field | Meaning |
|---|---|
| `x, y` | visible world tile, interpolated between tiles while moving |
| `trueTile` | the tile the game holds for you, from the movement route; leads `x, y` by up to two tiles while moving, equals it when still; the Overlay tab's True tile toggle draws it |
| `region` | `(x>>6)<<8 OR (y>>6)` |
| `lx, ly` | local tile within the region, 0..63; the instance-stable coordinate tile markers store by |
| `anim` | live animation id; -1 none |
| `interact` | `{ type, id, uid, name }` or `null`; `type` 1 NPC, 2 player |
| `splats` | hitsplats landing on you, same shape as `npcs[].splats` in `state.scene()` |
| `bar` | your first head bar fill 0..255, -1 none |
| `world` | current world id |
| `mouse` | `{ x, y, buttons }` in client pixels; `buttons` bits 1 left, 2 right, 4 middle |
| `keys` | `{ shift, alt, ctrl }` |
| `loading` | `{ pct, screen }`: map load percent and whether the loading screen is up |

#### state.inventory(), equipment(), bank()

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | none | object below; `null` when not in game |

```js
await rtx.plugin.state.inventory();
await rtx.plugin.state.equipment();
await rtx.plugin.state.bank();
// -> { present:true, count, cap, items:[ [slot, id, stack, name], ... ] }
```

- Each item is a 4-tuple: `slot`, `id`, `stack` (ints) and `name` (string).
- `bank()` is populated only while the bank is open.
- `present:false` means the container could not be read.

#### state.groupBank(), metalBank(), materials(), baitBox(), nexus()

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | none | object below; `null` when not in game |

```js
await rtx.plugin.state.groupBank();
await rtx.plugin.state.metalBank();
await rtx.plugin.state.materials();
await rtx.plugin.state.baitBox();
await rtx.plugin.state.nexus();
// -> { open, character, cached_at, count, items:[ [slot, id, stack, name], ... ] }
```

- Live while that storage UI is open, otherwise the per-character disk cache (`open:false`).
- Container ids: group bank 963 (Group Ironman shared bank), bait box 867 (Anachronia Big Game
  Hunter), nexus 953 (Necromancy necrotic runes).
- `metalBank()` holds smithing ores and bars; `materials()` the Archaeology material storage.

#### state.container(containerId)

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | `containerId` int | object as `inventory()`; `null` when not in game |

```js
await rtx.plugin.state.container(93);
// -> { present, count, cap, items:[ [slot, id, stack, name], ... ] }
```

- Generic container read: 93 backpack, 95 bank, 623 money pouch.
- Rune pouch, quiver and nexus are containers too; the Storage tab reads them this way.

#### state.itemExtra(containerId, itemId)

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | `containerId` int; `itemId` int | object below; `null` when not in game |

```js
await rtx.plugin.state.itemExtra(93, itemId);
// -> { present, key:{ k:v, ... }, pos:[ ... ] }
```

- An item's Extra_ints as key to value pairs: rune pouch, quiver and massive pouch packing.

#### state.social()

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | none | object below; `null` when not in game |

```js
await rtx.plugin.state.social();
// -> { in:true, world, friendsLoaded, online, friends:[ { name, world }, ... ] }
```

- `world` 0 on a friend means offline.
- Names are display names.

#### state.walkable(x, y, plane, r)

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | `x, y, plane` world tile; `r` int, 1..8 | object below; `null` when not in game |

```js
await rtx.plugin.state.walkable(3221, 3218, 0, 2);
// -> { x, y, plane, r, rows:[ "00100", "00100", ... ] }
```

- The `(2r+1)^2` tiles around `x, y`: `rows[i]` is world row `y-r+i`, character `j` is column
  `x-r+j`.
- `0` walkable, `1` blocked (scenery footprint, wall tile or void), from the map cache's collision
  data.
- Instance tiles are unknown.

#### state.groundItems()

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | none | array below; `[]` when none or not in game |

```js
await rtx.plugin.state.groundItems();
// -> [ { id, x, y, plane }, ... ]
```

- Dropped item stacks on the ground; `id` is the item id, resolve the name with `cache.itemInfo`.

#### state.combatLog(since, max)

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | `since` int, the last `seq` seen (0 from the start); `max` int, up to 2000, default 500 | object below; `null` when not in game |

```js
await rtx.plugin.state.combatLog(since, max);
// -> { seq, gap, events:[ { seq, t, type, uid, id, name, x, y, plane, hitmark, kind, other,
//                            value, cycle, dur, lp, lpMax }, ... ] }
```

| Field | Meaning |
|---|---|
| `seq` | monotonic id per client session |
| `t` | wall clock, ms since the Unix epoch, when the host first saw the record |
| `type` | `npc`, `player` or `self` (the local player: a hit taken) |
| `uid, id` | actor uid; `id` is the NPC config id, -1 for players |
| `name` | as shown in game |
| `x, y, plane` | the actor's tile when the hit was read |
| `hitmark` | raw hitmark id, see [Hitmark ids](#hitmark-ids) |
| `kind` | names the hitmark: `melee`, `ranged crit`, `necromancy`, `typeless`, `poison`, `heal`, `absorbed`, `blocked`, `deflect`, `text` and more |
| `other` | true for the game's Other Hitsplats set, a hit between other players and NPCs; hits involving you (dealt or taken) use the personal set |
| `value` | damage or heal amount; 0 for blocked and absorbed |
| `cycle, dur` | the record's start on the actor's 20 ms cycle clock and its lifetime (60) |
| `lp, lpMax` | the actor's life points at the poll: NPCs from the actor, `self` from varps 13537 and 13538, -1 for other players |

- Every hitsplat the game drew on any actor near you, one event each, in the order the hits
  landed.
- The host polls the hitsplat rings 5 times a second and logs each record once; no dedupe needed.
- Pass the returned `seq` back as `since` to get only new events; `gap` is true when you asked
  for events the ring has already dropped (it keeps the last 4096).
- Heals use 143 on any player (`other` false), so read `other` on damage kinds only.
- A heal of 0 drawn on you at the first hit of a burst you start is noise; ignore it.
- DPM: sum `value` over `type` `npc` or `player` with `other` false and a damage kind; damage
  taken is `type` `self`.

```js
const log = await rtx.plugin.state.combatLog(since, 500);   // since = the last seq you saw
since = log.seq; if (log.gap) rtx.plugin.console.warn("missed events");
```

#### state.scene(range)

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | `range` int, 1..64 tiles, clamped | object below; `null` when not in game |

```js
await rtx.plugin.state.scene(20);
// -> { players:[...], npcs:[...], objects:[...], projectiles:[...], effects:[...], walk }
```

| Field | Meaning |
|---|---|
| `npcs[]` | `id, uid, x, y, trueTile, plane, combat, anim, face, size, name, actions[], lp, lpMax, target, bar, splats[]` |
| `players[]` | `uid, x, y, trueTile, plane, combat, anim, self, name, bar, splats[]` |
| `objects[]` | `id, x, y, plane, type, dist, name, actions[]` |
| `projectiles[]` | `sx, sy, dx, dy, fsx, fsy, fdx, fdy`: in flight this frame, tiles plus fine units |
| `effects[]` | `gfx, x, y, fx, fy`: world spot animations this frame |
| `walk` | `{ x, y, fx, fy, src }` or `null`: the click-to-walk destination in tiles plus raw fine units |

- Entities are grouped by kind, not a flat list; `x, y` and `trueTile` follow the same rule as
  `state.info()`.
- `lp, lpMax` are the NPC's life points (-1 unknown); `target` is the player index the NPC attacks
  (-1 none); `anim` is the live animation id (-1 none), which is how attack telegraphs are read.
- `bar` is the fill 0..255 of the actor's first head bar (-1 none): health on most NPCs, a lifetime
  timer on helpers such as the Eternal magic tree (config 31500).
- `splats[]` records are `[hitmark, value, startCycle, durationCycles]`; a record lives
  `durationCycles` x 20 ms (usually 1.2 s) and is reused only after expiry, but stays in memory
  until then, so the last hits of a fight linger as long as the actor exists.
- Poll at 4 Hz or faster and count a record only when it was absent from that actor's list on the
  previous poll (key on ring slot, `startCycle`, `value`, `hitmark`); never forget records on a
  timer or you count them twice.
- A projectile whose `dx, dy` is your tile is an incoming ranged or magic attack;
  `state.combatLog()` delivers hits pre-classified.

#### Hitmark ids

Hitmark ids resolve through cache config archive 46. Every hit involving you uses the personal
set; hits between other players and NPCs use the other set (the one players can hide).

| Kind | Personal id | Other id |
|---|---|---|
| melee | 133 | 150 |
| melee critical | 134 | 151 |
| ranged | 136 | 153 |
| ranged critical | 137 | 154 |
| magic | 139 | 156 |
| magic critical | 140 | 157 |
| necromancy | 477 | 487 |
| necromancy critical | 478 | 488 |
| conjured spirit | 480 | 490 |
| conjured spirit critical | 481 | 491 |
| typeless | 144 | 161 |
| poison | 142 | 159 |
| cannon | 145 | 162 |
| deflect (reflected damage) | 146, 238 | 352 |
| split soul | 248 | 353 |
| blight | 346 | 351 |
| pierced shield | 416 | 415 |
| shadow pool | 435 | |
| heal | 143 | 160 |
| absorbed (crystal shield) | 148 | 165 |
| blocked (value 0) | 482 | 163 |
| hidden zero splat | | 492 |

- Text marks are not damage: Dodged 141, Immune 347, Executed 407, Perfect cut! 48. For DPS count
  personal damage marks on the actor you hit and ignore heals and zero marks; personal marks on
  your own player are damage taken.

#### state.varps(ids), varpsLong(ids)

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | `ids` string, comma-separated varp ids, 200 chars | object below; `null` when not in game |

```js
await rtx.plugin.state.varps("659,3274");
// -> { "659": 990, "3274": 120 }
await rtx.plugin.state.varpsLong("12932,12933");
// -> {"12932":"13019417610", "12933":"0"}
```

- `varps` maps each id to its raw value, the low 32 bits.
- `varpsLong` returns the full 64-bit value of long-typed varps as a decimal string; `varps`
  gives only the low 32 bits of these.

#### state.varbits(ids), varcs(ids)

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | `ids` int array, up to 64 | object below; `null` when not in game |

```js
await rtx.plugin.state.varbits([46468, 46463]);
// -> { "46468": 1, "46463": 100 }
await rtx.plugin.state.varcs([1118, 1119]);
// -> { "1118": 384, "1119": 2 }
```

- `varbits` resolves the backing varp and bit range from the cache automatically.
- `varcs` reads varc ints; 0 when absent.

#### state.varDomainStores()

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | none | object below; `null` when not in game |

```js
await rtx.plugin.state.varDomainStores();
// -> { stores:{ "<domain>":{ src, ptr, live, div, count, vt } },
//      vars:{ "6:<id>": v, "9:<id>": v } }
```

- The live var store of each script-visible domain, resolved the way the client binds them for
  scripts.
- Clan (6) and player group (9) values are listed when those stores exist.
- World (3), region (4) and campaign (8) have no live store; the client never binds them.

#### state.interface(group, comps)

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | `group` int, interface group id; `comps` int array, component ids | object below; `null` when not in game |

```js
await rtx.plugin.state.interface(1184, [4, 10, 15]);
// -> { group:1184, open:true, hasAbs:true, exact:true,
//      comps:[ { comp, sub, text, vis, x, y, w, h, obj, amt, spr, col }, ... ] }
```

| Field | Meaning |
|---|---|
| `open` | false when the interface is not showing |
| `hasAbs`, `exact` | absolute screen rects are available; the origin comes from the engine's own sub-interface table, so every attached group resolves without per-interface knowledge |
| `comp` | component id |
| `sub` | slot index in a templated grid (one entry per filled slot), -1 otherwise |
| `text` | live text |
| `vis` | 1 only when drawn (neither it nor an ancestor hidden); a hidden comp still reports its rect, so skip `vis:0` before highlighting |
| `x, y, w, h` | screen coordinates |
| `obj`, `amt` | item id of an item slot (0 otherwise) and its stack size |
| `spr` | sprite id |
| `col` | fill, text or tint colour as an RGB int |

- NPC chat box 1184: comp 4 the NPC name, 10 the message, 15 the continue button.
- Trade 335 comps 14 and 17 give both sides of a trade as `{ sub, obj, amt }` rows.

```js
const chat = await rtx.plugin.state.interface(1184, [4, 10, 15]);   // NPC chat box
const trade = await rtx.plugin.state.interface(335, [14, 17]);      // trade offer
```

#### state.interfaceGroup(group)

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | `group` int, one open interface group id | object below; `null` when not in game |

```js
await rtx.plugin.state.interfaceGroup(919);
// -> { widgets:[ { t:[group, comp, sub], d, p, r:[x, y, w, h], a:[absX, absY]?,
//                  ty, x, s, it, n, col?, v? }, ... ] }
```

- The full live widget tree of one open group, what the Interfaces tab shows; use it when you
  need every component rather than a few named ones.
- Heavier than `state.interface`; poll it sparingly.
- `d` depth, `p` parent comp, `r` rect, `a` absolute position when known.
- `ty` is the component class as the engine defines it: layer, rect, text, graphic, model, line,
  item (an item icon).
- `x` text, `s` sprite, `it` item id, `n` amount; `col` the fill, text or tint colour of rect,
  text and graphic widgets.
- `v:1` marks widgets drawn right now; absent means hidden (its own entry or an ancestor).

#### state.gameTick()

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | none | number; `null` when the client cannot be read |

```js
await rtx.plugin.state.gameTick();
// -> number, or null
```

- The server tick counter, advancing once per 600 ms game tick.
- Act on the change, never on the absolute value: it is not zeroed at login and not comparable
  between clients.

#### state.clientState()

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | none | object below; `null` when not in game |

```js
await rtx.plugin.state.clientState();
// -> { cutscene, inCutscene, options:[44 ints] }
```

- `cutscene` is the running cutscene id, -1 when none.
- `options` holds the client's 44 option values (graphics, audio and interface settings) by id;
  names are not carried by the client.

#### state.ports()

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | none | object below; `null` until readable |

```js
await rtx.plugin.state.ports();
// -> { resources:[ { name, qty, sprite } x9 ], tradeGoods:[ { name, qty, item } x7 ],
//      buildings:[ { name, level } x14 ], ships:[ { nameParts, voyageId, status, etaMinutes } ],
//      shipCount, scrollPieces, distance, zone }
```

- Player-Owned Ports account state, decoded once by the host so any plugin can act on it.
- `status` is `ready`, `sailing`, `returned` or `damaged`; `etaMinutes` is `null` when not
  sailing.
- `nameParts` holds the three name parts; ship and voyage names are enum lookups left to the
  consumer.
- `null` until readable (not in world, or port not started).

#### state.buffs()

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | none | object below; `null` when not in game |

```js
await rtx.plugin.state.buffs();
// -> { cycles, buffs:[ { struct, name, desc?, sprite, item, kind, timer, secs, exact, endCycle,
//                        remainMs, count }, ... ], debuffs:[ ... ] }
```

| Field | Meaning |
|---|---|
| `struct` | the buff struct the client drew the slot from |
| `name` | display name as plain text; a few buffs are named by their whole description, `name` is then its first line |
| `desc` | the lines after the first, joined by `\n`; absent otherwise |
| `exact` | true when the countdown came from the game's own end-cycle variable |
| `endCycle` | in CLIENTCLOCK cycles, 50/s; `cycles` is now |
| `remainMs` | the exact time left when `exact` |
| `secs` | 1 + remaining/50 when `exact`, matching the number the bar draws; otherwise parsed from the bar text ("2m" rounds) |
| `timer` | the bar text when not `exact` |
| `count` | the stack count var (Bloodlust stacks) when the game has one |

- One entry per buff-bar slot the game has bound to a buff struct; `debuffs` has the same shape.

#### state.actionBar()

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | none | object below; `null` when not in game |

```js
await rtx.plugin.state.actionBar();
// -> { bars:[ { bar, group,
//               slots:[ { slot, id, item, name, key, mod, castable, cd }, ... ] }, ... ] }
```

- One entry per visible action bar: main 1430 plus secondaries.
- `key, mod` is the bound keybind; `mod` 0 none, 1 shift, 2 ctrl, 3 alt.
- `castable` false means greyed, cannot cast.
- `cd` is the on-slot cooldown text: `""` ready, `"44s"`, `"1:23"`; the reliable cooldown source.

#### state.cooldowns(), perks()

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | none | objects below; `null` when not in game |

```js
await rtx.plugin.state.cooldowns();   // -> { cooldowns:[ ... ] }
await rtx.plugin.state.perks();       // -> { items:[ ... ] }
```

- `cooldowns` is the legacy engine registry and may be empty; `state.actionBar()` carries the
  reliable cooldown text.
- `perks` lists augmented gear and perks.

#### state.pets()

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | none | array below; `null` when not in game |

```js
await rtx.plugin.state.pets();
// -> [ { name, category, skill, obtained, source, item, icon }, ... ]
```

- Every pet with live obtained status (varp bitfield).
- `category` is `Skilling`, `Boss` or `Other`; `skill` the skill name or `null`; `source` the
  how-to-unlock text; `item` the icon item id; `icon` a data URL or `""`.

#### state.bosses()

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | none | array below; `null` when not in game |

```js
await rtx.plugin.state.bosses();
// -> [ { name, mode, kills, mode2, kills2, total }, ... ]
```

- Per-boss kill counts from the same permanent vars the in-game Beasts kill log reads.
- `mode2, kills2` are the boss's second tracked mode (hard, duo, group) where one exists,
  otherwise `null`; `total = kills + kills2`.

#### state.encounter()

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | none | object below; `null` when not in an instance |

```js
await rtx.plugin.state.encounter();
// -> { struct, name, mode, modeValue, health, healthMax }
```

- The boss instance you are in right now, already decoded.
- `mode` is the label the game shows: Normal, Hard, Challenge, Story, Solo, Duo, Trio,
  Enrage 250%, 4 player, Barrier 60%; `null` for a mode the game itself leaves blank.
- `health, healthMax` are live and move with enrage, so compare against `healthMax`, not a fixed
  per-boss number.
- `struct, modeValue` are the raw varp 10946 and 10950 values.

#### state.hideyHoles()

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | none | array below; `null` when not in game |

```js
await rtx.plugin.state.hideyHoles();
// -> [ { name, tier, location, build, fillItems:[ ... ], state, built, filled }, ... ]
```

- All 58 Treasure Trail hidey-holes; `tier` is `Easy`, `Medium`, `Hard` or `Master`.
- `state` 0 not built, 1 built and empty, 2 built and filled; `built` is `state >= 1`, `filled`
  is `state === 2`.
- `build` is the per-tier construction materials text; `fillItems` the three emote items it
  stores.

#### state.achievements(), achievement(id)

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | `achievement(id)`: `id` int | array of records below, or one record; `null` when not trackable |

```js
await rtx.plugin.state.achievements();
// -> [ { id, name, description, reward, points, complete, requirementsNeeded, combatMasteryTier,
//        requirements:[ { description, current, target, complete, varbits, varps?, achievement?,
//                         unlock? } ] }, ... ]
await rtx.plugin.state.achievement(385);
// -> one record as above, or null
```

- Every trackable achievement from the live cache, judged as the game judges it: a requirement is
  complete when `current >= target`.
- Requirements and child achievements sit in groups; a group is met when enough of its entries
  are, the achievement is complete when enough groups are met, and `requirementsNeeded` is that
  number.
- `varbits` lists the source varbit ids; bit-flag requirements read one bit and report `target`
  1; varp requirements sum the listed `varps` (`varbits` is `[]` for those).
- A line with `achievement` is a child achievement (its id); lines with `unlock:true` name an
  achievement that unlocks this one and never count toward completion.
- `combatMasteryTier` (`Easy`, `Medium`, `Hard`, `Elite`, `Master`, `Grandmaster` or `null`) is
  set only for combat achievements.
- `achievement(id)` returns `null` when the id is not trackable.

```js
const a = await rtx.plugin.state.achievement(385);   // "Shattering Worlds I"
if (a && a.complete) { ... }
```

#### state.skillBonus()

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | none | array below; `null` when not in game |

```js
await rtx.plugin.state.skillBonus();
// -> [ { skill:"Attack", bonus:1033436.5 }, ... ]
```

- Unspent bonus XP per skill; skills with none are omitted.
- Values match the in-game skill tooltips: the game stores tenths, this is already divided by 10.

#### state.dailies()

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | none | object below; `null` when unreadable |

```js
await rtx.plugin.state.dailies();
// -> { available:{ star, etree, dmob, sink, chin, ff, goebie, famil },
//      resets:{ now, daily, weekly, monthly }, varbits:{ "<id>": value },
//      vos:{ a, b, hour, src, n? } }
```

| Field | Meaning |
|---|---|
| `available` | booleans with the same tests as the D&D Tracker tab's bells: shooting star window, evil tree, demon flashmob, sinkhole, Big Chinchompa, Fish Flingers, goebie supply run, familiarisation |
| `resets` | epoch ms; daily, weekly and monthly reset at 00:00 UTC |
| `varbits` | the full raw read the tab derives everything else from |
| `vos` | this hour's Voice of Seren clan pair or `null`; codes 1 Iorwerth, 2 Trahaearn, 3 Crwys, 4 Cadarn, 5 Amlodd, 6 Meilyr, 7 Hefin, 8 Ithell |
| `vos.src` | `live` when read in Prifddinas, otherwise `community` with `n` reports; `null` when neither source has this hour's pair |

#### state.quests(), quest(id)

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | `quest(id)`: `id` int | array below, or one quest in full; `null` when `id` is unknown |

```js
await rtx.plugin.state.quests();
// -> [ { id, name, difficulty, status, statusText }, ... ]
await rtx.plugin.state.quest(id);
// -> { id, name, difficulty, status, statusText,
//      requirements:{ questPoints:{ need, have, ok }, skills:[ { skill, need, have, ok } ],
//                     quests:[ { id, name, complete } ], missing },
//      journal:{ description, startPoint, requiredItems, combat, xpRewards, otherRewards,
//                length, age, area } }
```

- `difficulty` is `Novice` through `Special`, or `null`.
- `status` 0 not started, 1 in progress, 2 complete, -1 no tracker in the game data
  (`statusText` `Unknown`).
- `quest(id)` judges requirements against the live account; `questPoints` is `null` when the
  quest has none.
- Journal fields are strings or `null` straight from the game cache and may contain `<br>` line
  breaks.

#### state.mysteries()

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | none | array below; `null` when not in game |

```js
await rtx.plugin.state.mysteries();
// -> [ { site:"Kharid-et", name:"Breaking the Seal", points:5, solved,
//        stage:{ value, max } }, ... ]
```

- Every Archaeology mystery grouped by dig site with live solved status.
- `stage` mirrors the journal's own progress (value 3 of max 6); `null` for a page or
  collection-driven mystery with no stage var.

### text

Scope: `state.read`.

The game's own descriptive text, computed by the client's tooltip scripts over the live account:
the same words and numbers the tooltip shows, without hovering. Each call returns `{ text, plain }`.

| Field | Meaning |
|---|---|
| `text` | with game markup: `<col=RRGGBB>`, `<br>`, `<sprite=N>`, `<nbsp>` |
| `plain` | the same with markup removed |
| empty `text` | the game has no detail, or the script needs something the host cannot supply |

#### text.buff(structId, count)

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | `structId` int, from `state.buffs()`; `count` int, its stack count when it has one | `{ text, plain }` |

```js
await rtx.plugin.text.buff(structId, count);   // Runic attuner
// -> { text:"Absorbed energy: <col=00ff00>0<br>Current attunement: <col=00ff00>Air altar", plain }
```

#### text.item(itemId, containerId, slot)

| Scope | Arguments | Returns |
|---|---|---|
| `state.read` | `itemId` int; `containerId`, `slot` optional ints | `{ text, plain }` |

```js
await rtx.plugin.text.item(itemId, 93, slot);
// -> { text, plain }
```

- The item tooltip lines: charges, augment level, degradation, examine.
- `containerId` and `slot` name the instance to read item vars from (93 backpack, 94 worn); leave
  them out for the item's static lines only.

### cache

Scope: `cache.read`.

#### cache.itemInfo(id), itemIcon(id), sprite(id), modelIcon(id)

| Scope | Arguments | Returns |
|---|---|---|
| `cache.read` | `id` int: item id, item id, sprite id, model id | `itemInfo` object; the others a PNG data URL string, `""` when none |

```js
await rtx.plugin.cache.itemInfo(id);    // -> { name, value, ... }
await rtx.plugin.cache.itemIcon(id);    // -> "data:image/png;base64,..."
await rtx.plugin.cache.sprite(id);      // -> "data:image/png;base64,..."
await rtx.plugin.cache.modelIcon(id);   // -> "data:image/png;base64,..."
```

- `itemInfo` returns item metadata (name, value and more).
- Icons and sprites are data URL strings for `img.src` or a CSS `background-image`, not JSON.
- `modelIcon` renders an interface type-6 MODEL component, keyed by model id. Model ids are not
  brokered and `state.interfaceGroup` carries no model field, so it is usable only with a model
  id you already hold (one baked into the plugin), not one looked up at runtime.

#### cache.enumInfo(id)

| Scope | Arguments | Returns |
|---|---|---|
| `cache.read` | `id` int, enum id | object below |

```js
await rtx.plugin.cache.enumInfo(id);
// -> { "<key>": value, ... }
```

- Decodes a game enum (an id to name or id to value table) to a plain object keyed by string.
- Rosters are static: fetch each one once and cache it.
- Combined with `state.varps`, this resolves coded values to names; the Slayer and Reaper task
  readout below.

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

#### cache.varbitMap(), varbitDomainMap(), varbitDomains()

| Scope | Arguments | Returns |
|---|---|---|
| `cache.read` | none | objects below |

```js
await rtx.plugin.cache.varbitMap();
// -> { "<varpId>": [ [varbitId, lsb, msb], ... ], ... }
await rtx.plugin.cache.varbitDomainMap();
// -> { "<domain>": { "<var>": [ [varbitId, lsb, msb], ... ] } }
await rtx.plugin.cache.varbitDomains();
// -> { "<domain>": { n, var:[min, max], vb:[min, max], sample } }
```

- `varbitMap` maps player varps to their varbits.
- `varbitDomainMap` covers the non-player domains: 1 npc, 2 client (bit fields over varc ints),
  3 world, 4 region, 5 object (item instance keys, see `state.itemExtra`), 6 clan,
  7 clan settings, 8 campaign.
- `varbitDomains` gives each domain's var and varbit id ranges with a sample.

#### cache.varDefs(archive)

| Scope | Arguments | Returns |
|---|---|---|
| `cache.read` | `archive` int | object below |

```js
await rtx.plugin.cache.varDefs(archive);
// -> { archive, n, types:{ "<varId>": subtype }, flags:{ "<varId>": bits } }
```

- Archives: 60 player, 61 npc, 62 client, 63 world, 64 region, 65 object, 66 clan,
  67 clan settings, 68 campaign, 75 player group.
- `types` lists only vars whose value type is not int, with CS2 subtype ids: 1 boolean, 33 obj,
  36 string, 39 inv, 71 hash64, 73 struct, 110 long.

#### cache.paramDef(id), structParams(id), itemParams(id)

| Scope | Arguments | Returns |
|---|---|---|
| `cache.read` | `id` int: param id, struct id, item id | objects below |

```js
await rtx.plugin.cache.paramDef(id);        // -> { type, int?, str? }
await rtx.plugin.cache.structParams(id);    // -> { ints:{ k:v }, strs:{ k:"v" } }
await rtx.plugin.cache.itemParams(id);      // -> { ints:{ k:v }, strs:{ k:"v" } }
```

- `paramDef` is a param definition; `{}` while the host's param reader is unavailable.
- `structParams` returns one struct's params; `itemParams` one item's op-249 params.

#### cache.abilityConfigs(), abilityTips()

| Scope | Arguments | Returns |
|---|---|---|
| `cache.read` | none | objects below |

```js
await rtx.plugin.cache.abilityConfigs();
// -> { "<Ability Name>": { t, st, l, ag, ac, c, i, s, d }, ..., _byId:{ "<abilityId>": { ... } } }
await rtx.plugin.cache.abilityTips();
// -> { "<abilityId>": [ "bullet line", ... ], ... }
```

| Field | Meaning |
|---|---|
| `t` | tier: 0 auto-attack, 1 basic, 2 threshold, 3 defensive threshold, 4 ultimate, 5 special, 7 utility |
| `st` | combat style: 1 and 2 melee, 3 ranged, 4 magic, 5 defence, 6 constitution, 29 necromancy |
| `l` | level requirement |
| `ag` | adrenaline gain in tenths of a percent |
| `ac` | adrenaline cost |
| `c` | cooldown in game ticks of 0.6 s |
| `i` | the ability id action-bar slots carry |
| `d` | description |
| `_byId` | the same set keyed by that ability id |

- Every combat ability from the live cache; `cache.sprite(abilityId)` is the ability's icon.
- `abilityTips` gives plain-text tooltip bullets per ability, flattened from the game's own
  tooltip builders; damage placeholders read "75%-95% damage".
- Both are static: fetch once.

#### cache.mapWindow(cx, cy, plane, half, ts)

| Scope | Arguments | Returns |
|---|---|---|
| `cache.read` | `cx, cy` world tile centre; `plane`; `half` tiles each side, up to 384; `ts` px per tile, up to 32 | object below |

```js
await rtx.plugin.cache.mapWindow(cx, cy, plane, half, ts);
// -> { w, t, h, wt, cx, cy, p, png, blk, nomove, objs }
```

- A top-down terrain render centred on world tile `cx, cy`; `w` image px, `t` px per tile, `h`
  half-size in tiles.
- `png` is a base64 PNG (RGB, no alpha): set `img.src = 'data:image/png;base64,' + png` and draw
  it.
- Older clients return `b64` (raw RGBA for `putImageData`) instead of `png`; prefer `png` when
  present.

### prices

Scope: `cache.read`.

- Real-time RS3 Grand Exchange prices, relayed through the RuneTools server and cached by the
  launcher, so plugin calls never generate upstream traffic.
- `latest` refreshes about every 90 s; call it at most that often.

#### prices.latest(), mapping()

| Scope | Arguments | Returns |
|---|---|---|
| `cache.read`, 1 per 2 s | none | objects below |

```js
await rtx.plugin.prices.latest();
// -> { "2": { high, highTime, low, lowTime }, ... }
await rtx.plugin.prices.mapping();
// -> [ { id, name, limit, value, lowalch, highalch, members }, ... ]
```

- Full payloads, large; cache them.

#### prices.item(ids)

| Scope | Arguments | Returns |
|---|---|---|
| `cache.read`, 4/s | `ids` int or int array, up to 50 | `{ "<id>": { high, highTime, low, lowTime } }` |

```js
await rtx.plugin.prices.item(2);        // -> { "2": { high, highTime, low, lowTime } }
await rtx.plugin.prices.item([2, 6]);   // -> { "2": { ... }, "6": { ... } }
```

- Answers from a local parsed cache, so it is cheap; use it when watching a handful of items.

### overlay

Scope: `overlay`. Fire-and-forget unless a boolean is listed.

| Method | Arguments | Replaces previous set | Cap | Returns |
|---|---|---|---|---|
| `toast` | `text` | no | | nothing |
| `notify` | `text, ttlMs` 0..60000 | no | | nothing |
| `flashGame` | none | no | | nothing |
| `highlight` | `names[]` | yes | | nothing |
| `highlightNpc` | `name, label?, tileX?, tileY?` | yes, one NPC | | nothing |
| `highlightOption` | `text` or `texts[]` | yes | | boolean |
| `highlightItem` | `itemId, label?` | yes | | boolean |
| `highlightRect` | `x, y, w, h` | yes, shared set | | nothing |
| `highlightRects` | `rects[]` | yes, shared set | 64 | nothing |
| `uiLabels` | `labels[]` | yes | 32 labels, 90 chars | nothing |
| `guideTiles` | `marks[]` | yes | 64 tiles, 95 chars, 4 lines | nothing |
| `pointAt` | `kind, target` | yes, one per plugin | | nothing |
| `pointClear` | none | no | | nothing |
| `clearHighlight` | none | no | | nothing |
| `hudAbilities` | `spec` or `null` | yes, one per plugin | 6 `cur`, 6 `next` | nothing |
| `centerText` | `text, slot?, rgb?` | yes | | nothing |
| `wikiSearch` | `term` | no | | nothing |

#### overlay.toast(text), notify(text, ttlMs), flashGame()

| Scope | Arguments | Returns |
|---|---|---|
| `overlay`, 6/s | `text` string; `ttlMs` int, 0..60000 | nothing |

```js
rtx.plugin.overlay.toast("Saved");
rtx.plugin.overlay.notify("Heads up", 4000);
rtx.plugin.overlay.flashGame();
```

- `toast` shows a brief toast; `notify` a notification that lasts `ttlMs`; `flashGame` flashes
  the game window.

#### overlay.highlight(names), highlightNpc(name, label, tileX, tileY), clearHighlight()

| Scope | Arguments | Returns |
|---|---|---|
| `overlay`, 6/s | `names` string array; `name` string; `label` string, `tileX`, `tileY` world tile, all optional | nothing |

```js
rtx.plugin.overlay.highlight(["Goblin", "Banker"]);
rtx.plugin.overlay.highlightNpc("Acting Guildmaster Reiniger", "Step 1: talk to me");
rtx.plugin.overlay.clearHighlight();
```

- `highlight` outlines scene entities by name and strips punctuation.
- `highlightNpc` boxes one NPC, matched case-insensitively; a non-empty `label` replaces the name
  on the pill (a tutorial step, for example); an empty `name` clears it.
- `tileX, tileY` box the instance nearest that tile instead of the one nearest the player.
- `clearHighlight` clears both highlight layers.

#### overlay.highlightOption(texts), highlightItem(itemId, label)

| Scope | Arguments | Returns |
|---|---|---|
| `overlay`, 6/s | `texts` string or string array; `itemId` int; `label` optional string | boolean: matched and boxed |

```js
await rtx.plugin.overlay.highlightOption("I want to talk about mysteries");   // -> true
await rtx.plugin.overlay.highlightItem(995);                                   // -> true
```

- `highlightOption` boxes one open chat-option box whose text matches (substring,
  case-insensitive); an array matches any of its texts in one read.
- The plugin decides which option and when (do your own quest or step validation first); the
  host finds the live box and draws it.
- `highlightItem` boxes the backpack slot holding the item id, only when that slot is on screen;
  scrolled out or panel closed draws nothing and returns false.

#### overlay.highlightRect(x, y, w, h), highlightRects(rects)

| Scope | Arguments | Returns |
|---|---|---|
| `overlay`, 6/s | `x, y, w, h` screen px; `rects` array of `[x, y, w, h]` or `{ x, y, w, h }`, up to 64 | nothing |

```js
rtx.plugin.overlay.highlightRect(x, y, w, h);
rtx.plugin.overlay.highlightRects([[x, y, w, h], ...]);
rtx.plugin.overlay.highlightRects([]);
```

- `highlightRect` draws one screen rect, such as a component rect from `state.interface`; `w` or
  `h` <= 0 clears it.
- `highlightRects` replaces the whole set, so redraw your own rects each update rather than
  adding to them; `[]` clears.
- There is one highlight set per game client, shared with `highlightRect` and the panel's own
  guides: the last caller wins, and clearing clears everything.

```js
const d = await rtx.plugin.state.interface(1184, [15]);   // the NPC chat continue button
const c = d.hasAbs && d.comps[0];
if (c) rtx.plugin.overlay.highlightRect(c.x, c.y, c.w, c.h);
```

#### overlay.uiLabels(labels)

| Scope | Arguments | Returns |
|---|---|---|
| `overlay`, 6/s | `labels` array of `{ x, y, text, style, rgb, px }`, up to 32 | nothing |

```js
rtx.plugin.overlay.uiLabels([
  { x: c.x + 18, y: c.y - 12, text: 'Buy 30.0m | Sell 29.5m', style: 1 },
]);
rtx.plugin.overlay.uiLabels([]);
```

| Field | Meaning |
|---|---|
| `x, y` | game-view coordinates, the same space as `state.interface` rects |
| `text` | up to 90 chars |
| `style` | 0 bare text with its left edge at `x`, centred on `y`; 1 a pill (dark rounded box) centred on `x, y` |
| `rgb` | RGB int; -1 the game's yellow |
| `px` | font size, default 13 |

- Each call replaces the previous set; `[]` clears.

#### overlay.guideTiles(marks)

| Scope | Arguments | Returns |
|---|---|---|
| `overlay`, 6/s | `marks` array of `{ x, y, plane, label, x2, y2, color, color2, merge, snapId, snap }`, up to 64 | nothing |

| Field | Meaning |
|---|---|
| `x, y, plane` | world tile; the SW corner when `x2, y2` is given |
| `x2, y2` | NE corner: one flat ground rect spanning the tiles (walkways, zones whose loc is a 1x1 end piece such as an agility log) |
| `label` | lines separated by `\n`; the first line is a match key (table below) |
| `color` | `'#rrggbb'` or packed int; default accent |
| `color2` | second tone: the fill splits along the SW to NE diagonal, `color` keeps the north-west half; single tiles only, an area rect uses `color` alone |
| `merge` | `true`: adjacent marks with the same label become one zone with one label |
| `snapId` | loc id: box the live entity on the tile instead of the tile, so the label rides above the model and clears the game's own overhead bar; only that loc is considered and the tile must fall inside its model; pair with a plain footprint mark carrying no label when you want both |
| `snap` | `true` without an id: the nearest live entity within one tile; only for a mark whose object you cannot name |

| Label | Renders | Footprint |
|---|---|---|
| `-Cliffside\nClimb\nCliffside` | Climb over Cliffside | Cliffside prism |
| `-Cliffside\nClimb` | Climb alone | Cliffside prism |
| `Cliffside\nClimb` | Cliffside over Climb | Cliffside prism |
| `dig here` | dig here | flat 1x1 tile |

- A first line that names a cache loc near the tile takes that object's 3D footprint prism;
  otherwise the mark is a flat 1x1 tile.
- A leading `-` makes the line match-only (resolved, not drawn), which puts the action first
  while keeping the prism.
- Each call replaces the previous set; `[]` clears; marks beyond 64 are dropped silently.

```js
rtx.plugin.overlay.guideTiles([{ x:3221, y:3218, plane:0, label:"dig here" }]);
rtx.plugin.overlay.guideTiles([{ x:2474, y:3430, x2:2474, y2:3435, plane:0, label:"Log balance" }]);
rtx.plugin.overlay.guideTiles([{ x:3221, y:3218, plane:0, label:"swap",
                                 color:"#57C6E0", color2:"#C07AE0" }]);
rtx.plugin.overlay.guideTiles([{ x:3221, y:3218, plane:0, label:"stand", merge:true },
                               { x:3222, y:3218, plane:0, label:"stand", merge:true }]);
rtx.plugin.overlay.guideTiles([
  { x:2330, y:3595, x2:2332, y2:3597, plane:0, color:"#63DD9B" },
  { x:2331, y:3596, plane:0, snapId:131907, label:"4:12", color:"#63DD9B" },
]);
```

#### overlay.pointAt(kind, target), pointClear()

| Scope | Arguments | Returns |
|---|---|---|
| `overlay`, 6/s | `kind` string: `npc`, `object` or `tile`; `target` string | nothing |

```js
rtx.plugin.overlay.pointAt("npc", "Banker");
rtx.plugin.overlay.pointAt("object", "Bank chest");
rtx.plugin.overlay.pointAt("tile", "3221, 3218");
rtx.plugin.overlay.pointClear();
```

| Kind | Target | Example |
|---|---|---|
| `npc` | a name, part of one, or an id | `"Banker"` |
| `object` | a name, part of one, or an id | `"Bank chest"` |
| `tile` | `"x, y"` or `"x, y, plane"` | `"3221, 3218"` |

- The game's own pointer: its arrow over the target, its chevrons at the player's feet turning
  towards it and its trail of markers on the ground.
- The target is looked up around the player once a second and the nearest match is used; nothing
  shows while it is out of view.
- One request per plugin; it ends on `pointClear()` or when the plugin closes.

#### overlay.wikiSearch(term)

| Scope | Arguments | Returns |
|---|---|---|
| `overlay`, 6/s | `term` string; `''` the home page | nothing |

```js
rtx.plugin.overlay.wikiSearch("Abyssal whip");
```

- Opens the in-client wiki browser on a search term; the pane is locked to runescape.wiki, so a
  plugin chooses the page, never the site.

#### overlay.hudAbilities(spec)

| Scope | Arguments | Returns |
|---|---|---|
| `overlay`, 6/s | `spec` object below, or `null` | nothing |

```js
rtx.plugin.overlay.hudAbilities({
  title: "Zamorak opener", sub: "Tick 4: dive out",
  cur:  [{ id: 30331, key: "S+3" }],
  next: [{ id: 23727, gap: 3 }, { id: 23729, gap: 6 }],
});
rtx.plugin.overlay.hudAbilities(null);
```

| Field | Meaning |
|---|---|
| `title`, `sub` | strip title and subtitle |
| `cur[]` | `{ id, key }`: abilities to press now, drawn large with a keybind badge; up to 6 |
| `next[]` | `{ id, gap }`: upcoming, small and dimmed; `gap` is ticks until it; up to 6 |

- A floating HUD strip over the game showing ability icons: rotation playback, switch reminders
  and the like.
- One per plugin, host-rendered and draggable; each call replaces the content.
- `null` or an empty `cur` closes it.
- Icons come from the ability id, the same id `cache.sprite` serves.

#### overlay.centerText(text, slot, rgb)

| Scope | Arguments | Returns |
|---|---|---|
| `overlay`, 6/s | `text` string, `''` clears; `slot`, `rgb` optional | nothing |

```js
rtx.plugin.overlay.centerText("DODGE - icicles!");
```

- A large centre-screen banner, the Dungeoneering boss-warning channel.

### notify

Scope: `notify.os` or `notify.discord`.

#### notify.windows(title, body)

| Scope | Arguments | Returns |
|---|---|---|
| `notify.os`, 1 per 10 s | `title`, `body` strings | nothing |

```js
rtx.plugin.notify.windows("RuneToolsX", "A ship has returned");
```

- An OS toast outside the game window; send it on real events (a ship returned, a rare drop),
  never on a timer.

#### notify.discord(text)

| Scope | Arguments | Returns |
|---|---|---|
| `notify.discord`, 1 per 10 s | `text` string | `{ queued: true }`, `{ error: "not configured" }` or `{ error: "rate limited" }` |

```js
await rtx.plugin.notify.discord("Ship 3 returned with 120 chimes");
// -> { queued: true }
```

- Posts plain text to the webhook the user entered in Settings; the plugin never sees the URL,
  the host holds it sealed.
- The host prefixes every message with the plugin id, strips all mentions so nothing can ping a
  user or role, and caps the text.
- Not configured is a normal state; handle it quietly.

### clipboard

Scope: `clipboard`; `paste` needs `clipboard.read`.

#### clipboard.copy(text), paste()

| Scope | Arguments | Returns |
|---|---|---|
| `clipboard`, 1/s; `paste` `clipboard.read`, 1/s | `text` string, up to 64 KB | `copy` nothing; `paste` the clipboard text |

```js
rtx.plugin.clipboard.copy(JSON.stringify(plan));
const code = await rtx.plugin.clipboard.paste();
```

- `copy` puts text on the user's clipboard.
- `paste` reads clipboard text; call it only from a direct user action (an Import button), never
  on a timer, and expect any text at all.
- Keyboard paste inside the game view is unreliable, which is what `paste` is for.

### sound

Scope: `sound`.

#### sound.play(name)

| Scope | Arguments | Returns |
|---|---|---|
| `sound` | `name` string | nothing |

```js
rtx.plugin.sound.play("alert1");
```

- Plays a built-in RuneTools alert sound by name.

### storage

Scope: `storage`.

#### storage.set(key, value), get(key), keys()

| Scope | Arguments | Returns |
|---|---|---|
| `storage`, 4/s | `key` string, 64 chars; `value` any JSON-serialisable value, 256 KB | `get` the value or `null`; `keys` `["key", ...]` |

```js
await rtx.plugin.storage.set("key", value);
await rtx.plugin.storage.get("key");    // -> value or null
await rtx.plugin.storage.keys();        // -> ["key", ...]
```

- Keys are namespaced to the plugin id and the active account; another plugin cannot read them.
- `~settings` is reserved for `ui.settings`.

### telemetry

Scope: `telemetry`.

#### telemetry.append(name, record), appendMany(name, records), export(name, data)

| Scope | Arguments | Returns |
|---|---|---|
| `telemetry`; `append` 60/s, `appendMany` 10/s, `export` 1 per 5 s | `name` file name below; `record` up to 64 KB; `records` array, up to 1000; `data` up to 16 MB | `{ ok: true, size }` or `{ ok: false, error }` |

```js
await rtx.plugin.telemetry.append("encounter.jsonl", { tick, kind: "anim", npc, anim });
await rtx.plugin.telemetry.appendMany("encounter.jsonl", bufferedRecords);
await rtx.plugin.telemetry.export("kill-42.json", encounter);
// -> { ok: true, size }
```

- Files go to `%USERPROFILE%\RuneToolsX\plugin-logs\<plugin id>\`, one folder per plugin shared
  by every character, so the user can zip it and send it to you; the host owns the folder, you
  name files.
- Names: letters, digits, `-`, `_` and `.`, up to 48 chars, ending in `.jsonl`, `.json`, `.csv`,
  `.txt` or `.log` (no extension means `.jsonl`); device names such as `con` or `nul` are
  refused.
- Records: `append` writes one line, a string as is (line breaks become spaces), anything else as
  compact JSON, so a `.jsonl` file stays one record per line; `export` replaces the whole file, a
  string as is, anything else as JSON.
- Results: `size` is the file's new size; `error` is one of `bad name`, `bad record`,
  `too large`, `file full`, `folder full`, `too many files`, `bad file`, `write failed`.
- On `file full` start a new file (`encounter-2.jsonl`); on `folder full` ask the user to send
  and clear the folder.
- Buffer records and flush with `appendMany` once per tick or two instead of one call per event.

#### telemetry.list(), remove(name), open()

| Scope | Arguments | Returns |
|---|---|---|
| `telemetry`; `open` 1 per 5 s | `name` file name | `list` the object below |

```js
await rtx.plugin.telemetry.list();
// -> { files:[ { name, size, modified } ], bytes, limit }
await rtx.plugin.telemetry.remove("kill-41.json");
await rtx.plugin.telemetry.open();
```

- `open` shows the folder in Explorer (from a button in your window); it works only while your
  window is showing, never from a background plugin.
- There is no read call and no other path; a plugin cannot see or touch any other file.
- Tell the user what you record: logs can include their character name and location, and they
  decide what to send.

### ui

Scope: none.

#### ui.setHeight(px), setTitle(text)

| Scope | Arguments | Returns |
|---|---|---|
| none | `px` int, 60..4000; `text` string | nothing |

```js
rtx.plugin.ui.setHeight(420);
rtx.plugin.ui.setTitle("My Tool");
```

- `setHeight` resizes the plugin frame.
- `setTitle` is reserved, a no-op for now.

#### ui.settings(schema)

| Scope | Arguments | Returns |
|---|---|---|
| none | `schema` array of controls below, up to 24 | the current values |

```js
const values = await rtx.plugin.ui.settings([
  { key: 'compact',  type: 'toggle', label: 'Compact layout', default: false,
    hint: 'Smaller rows and icons' },
  { key: 'style',    type: 'select', label: 'Combat style', default: 'melee',
    options: [{ v: 'melee', label: 'Melee' }, { v: 'ranged', label: 'Ranged' }] },
  { key: 'volume',   type: 'slider', label: 'Alert volume', default: 70,
    min: 0, max: 100, step: 5 },
  { key: 'nickname', type: 'text',   label: 'Display name', default: '' },
]);
```

| Field | Meaning |
|---|---|
| `key` | `[A-Za-z0-9_.-]`, up to 32 chars |
| `type` | `toggle`, `select`, `slider` or `text` |
| `label` | up to 48 chars |
| `default` | initial value |
| `hint` | up to 120 chars |
| `options[]` | `{ v, label }` for `select`, up to 12 |
| `min, max, step` | `slider` range; `text` values up to 200 chars |

- Declare once at boot; RuneTools renders the controls on its Preferences page (Plugins card).
- Values are stored host-side per plugin and account; only your plugin sees them.
- Values are clamped to the schema on every write.

### settings

Scope: none.

#### settings.on(fn), get()

| Scope | Arguments | Returns |
|---|---|---|
| none | `fn` function receiving the values | `get()` the current values |

```js
rtx.plugin.settings.on(v => applySettings(v));
const now = await rtx.plugin.settings.get();
```

- `on` fires on declare and on every change.

### console

Scope: none.

#### console.info(), debug(), warn(), error(), scoped(tag)

| Scope | Arguments | Returns |
|---|---|---|
| none, 60 lines/s, burst 120 | values; objects are printed as JSON; `tag` string | `scoped` a tagged logger |

```js
rtx.plugin.console.info("loaded", { version: 3 });
rtx.plugin.console.debug(...) / .warn(...) / .error(...)
const log = rtx.plugin.console.scoped("combat");
log.warn("no target");
```

- Lines land in Developer > Console, stamped by the host with the plugin id and runtime, next to
  the client's own messages and the launcher log.
- The panel filters by level, source and tag, searches, and copies single lines or the whole
  view.
- Write-only: a plugin never reads the console, other plugins' lines or the launcher log.
- 4000 chars per line, 60 lines/s per plugin with a burst of 120; past that lines are dropped and
  the panel says so.
- The frame's own `console.log` stays in the frame.

### Theme

Scope: none.

HTML plugins only. The host injects its live theme as custom properties and updates them when the
user changes appearance settings; your `<head>` is spliced in after them, so your own definitions
win. Always pass a fallback: a value can be absent on an older client.

```css
.button { background: var(--rtx-accent, #8c6ffd); }
.card   { background: var(--rtx-panel, #1a1b23);
          border: 1px solid var(--rtx-border, rgba(255,255,255,.08)); }
.subtle { color: var(--rtx-text-mute, #8b8b9e); }
```

| Group | Variables |
|---|---|
| Accent | `--rtx-accent`, `--rtx-accent-hi`, `--rtx-accent-lo`, `--rtx-accent-rgb`, `--rtx-accent-ring` |
| Surfaces | `--rtx-bg`, `--rtx-bg-elev`, `--rtx-bg-elev-2`, `--rtx-panel`, `--rtx-panel-2`, `--rtx-win-bg` |
| Borders | `--rtx-border`, `--rtx-border-hi` |
| Text | `--rtx-text`, `--rtx-text-dim`, `--rtx-text-mute` |
| Status | `--rtx-ok`, `--rtx-warn`, `--rtx-err` |
| Font | `--rtx-font-ui`, `--rtx-font-size` |

## Lua plugins

- Same folders, Browse tab, consent card, Preferences page and hot reload as HTML plugins.
- `main.lua` runs in its own sandboxed Lua 5.4 state on the host tick; there is no frame.
- The panel is a widget tree the host renders.

Lua mirrors the JavaScript SDK one to one, so plugins keep working if an official Lua API is
mapped under the same names.

### Layout

```
com.yourname.tool/
  manifest.json
  main.lua
  util.lua          optional modules, loaded with require
  data/rows.json    optional data files, read with rtx.plugin.readFile
```

Manifest: as for HTML with two changes; `entry` is not used.

```json
"runtime": "lua",
"main": "main.lua"
```

### Differences

| JavaScript | Lua |
|---|---|
| `await rtx.plugin.x.y()` returns a Promise | `rtx.x.y()` returns the value synchronously |
| a failed call rejects with a reason string | returns `nil, reason` and sets `rtx.lastError` |
| reasons: `scope not granted: <scope>`, `rate limited` | same strings, plus `unknown method` and `pending` |
| JSON `null` | `nil` |
| `rtx.plugin.on(event, fn)` | `rtx.on(event, fn)`; adds `ready` (first host tick after load) |
| `rtx.plugin.events.on(kind, fn)` | `rtx.events.on(kind, fn)`, same kinds and fields |
| `rtx.plugin.settings.on(fn)` | `rtx.on("settings", fn)` |
| `rtx.plugin.console.*` | `rtx.console.*`, `print(...)`, `rtx.log`, `rtx.debug`, `rtx.warn`, `rtx.error` |
| an HTML document | `rtx.ui.render(tree)` |

```lua
local inv = rtx.state.inventory()          -- table or nil
if inv then print(#inv.items .. " items") end

local v, err = rtx.state.player()          -- err is a string when the call failed
if not v then rtx.warn("player: " .. tostring(err)) end
```

### Namespaces

The `rtx` table is generated from the host's method table: every method in the API reference
exists under its Lua name with the same arguments, scopes and limits.

| JavaScript | Lua |
|---|---|
| `rtx.plugin.state.*` | `rtx.state.*` |
| `rtx.plugin.text.*` | `rtx.text.*` |
| `rtx.plugin.cache.*` | `rtx.cache.*` |
| `rtx.plugin.prices.*` | `rtx.prices.*` |
| `rtx.plugin.overlay.*` | `rtx.overlay.*` |
| `rtx.plugin.notify.*` | `rtx.notify.*` |
| `rtx.plugin.clipboard.*` | `rtx.clipboard.*` |
| `rtx.plugin.sound.*` | `rtx.sound.*` |
| `rtx.plugin.storage.*` | `rtx.storage.*` |
| `rtx.plugin.telemetry.*` | `rtx.telemetry.*` |
| `rtx.plugin.ui.*` | `rtx.ui.*` |
| `rtx.plugin.settings.*` | `rtx.settings.*` |
| `rtx.plugin.console.*` | `rtx.console.*` |
| `rtx.plugin.events.*` | `rtx.events.*` |
| `rtx.plugin.id()`, `apiVersion()`, `grantedScopes()`, `hasScope()` | `rtx.plugin.id()`, `apiVersion()`, `grantedScopes()`, `hasScope()` |

Pending methods:

- These are computed asynchronously by the host: `state.quests`, `state.quest`, `state.pets`,
  `state.bosses`, `state.encounter`, `state.dailies`, `state.mysteries`, `state.varbits`,
  `state.varcs`, `state.achievements`, `ui.settings`, `overlay.highlightOption`,
  `overlay.highlightItem`.
- The first call with a given argument list returns `nil, "pending"`; the value arrives on a
  later call and stays fresh as you keep calling.
- Read them in `tick` and treat `nil` as not yet.

### Lua-only API

| Method | Arguments | Returns |
|---|---|---|
| `rtx.plugin.runtime()` | none | `"lua"` |
| `rtx.plugin.runtimeVersion()` | none | `"Lua 5.4.7"` |
| `rtx.plugin.readFile(path)` | a text file inside the plugin folder | its text, or `nil` |
| `rtx.json.encode(value)` | any value | JSON string |
| `rtx.json.decode(text)` | JSON string | value |
| `rtx.call(method, args)` | method name, args table (`rtx.call("state.scene", { 20 })`) | the method's result; the generic form every namespace method uses |
| `rtx.timer.after(seconds, fn)` | seconds, function | timer id; resolved on the host tick |
| `rtx.timer.every(seconds, fn)` | seconds, function | timer id |
| `rtx.timer.cancel(id)` | timer id | nothing |
| `rtx.ui.render(tree)` | widget tree | nothing; re-renders only when the tree differs |
| `rtx.ui.clear()` | none | nothing |
| `rtx.ui.settings(schema)` | same schema as `ui.settings` | values |
| `rtx.console.scoped(tag)` | tag | tagged logger |
| `rtx.lastError` | field | the last failure reason |
| `print(...)` | values; objects printed as JSON | nothing |

```lua
rtx.plugin.id()               -- "com.yourname.tool"
rtx.plugin.apiVersion()       -- "1.0"
rtx.plugin.runtime()          -- "lua"
rtx.plugin.runtimeVersion()   -- "Lua 5.4.7"
rtx.plugin.grantedScopes()    -- { "state.read", "overlay", "storage" }
rtx.plugin.hasScope("overlay")
rtx.plugin.readFile("data/rows.json")   -- a text file inside the plugin folder, or nil
rtx.json.encode(value) / rtx.json.decode(text)
rtx.call("state.scene", { 20 })         -- the generic form every namespace method uses
```

```lua
rtx.on("ready", function() end)             -- the first host tick after load
rtx.on("tick", function() end)              -- about 4 times a second
rtx.on("state", function(snapshot) end)     -- changed snapshot; state.read
rtx.on("settings", function(values) end)    -- a settings value changed
rtx.events.on("skill_update", function(ev) end)   -- same kinds and fields as JavaScript
rtx.events.on("*", function(ev) end)
rtx.events.off("skill_update", fn)

local id = rtx.timer.after(5, function() end)     -- seconds; resolved on the host tick
local id2 = rtx.timer.every(60, function() end)
rtx.timer.cancel(id2)

print("hello", 42, { a = 1 })   -- objects are printed as JSON
rtx.console.debug(...) / .info(...) / .warn(...) / .error(...)
local log = rtx.console.scoped("combat")   -- tagged lines
rtx.log / rtx.debug / rtx.warn / rtx.error  -- shorthands for rtx.console.*
```

### Panels

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
rtx.ui.settings({ { key = "alerts", type = "toggle", label = "Alerts", default = true } })
rtx.settings.get()
```

| Widget | Props | Callback value |
|---|---|---|
| `heading` | `text` | |
| `text` | `text, muted, color` | |
| `card` | `title, children` | |
| `row` | `children` | |
| `col` | `children` | |
| `button` | `id, label, primary, disabled, onClick` | none |
| `toggle` | `id, label, value, onChange` | new boolean |
| `input` | `id, label, value, placeholder, onChange` | new text |
| `select` | `id, label, value, options = {{v, label}, ...}, onChange` | selected `v` |
| `progress` | `value, max, label, text` | |
| `table` | `columns, rows` | |
| `kv` | `items = {{k, v}, ...}` | |
| `badge` | `text, tone` (ok, warn or err) | |
| `sep` | none | |
| `spacer` | `h` | |

- Publish a new tree whenever data changes; the host re-renders only when it differs.
- At most 500 widgets, 8 levels deep; text is plain, never HTML.
- The host shows a collapsible console under the panel with everything printed and every error
  raised.

### Lua sandbox

- Available: `string`, `table`, `math`, `utf8`, `coroutine`; `os.time`, `os.clock`, `os.date`,
  `os.difftime`.
- `require("./util")`, `require("lib.colors")` and `require("sub/mod")` load `.lua` files inside
  the plugin folder once and cache the returned value; `load` compiles text only.
- Not available: `io`, `debug`, `package`, `dofile`, `loadfile`; paths outside the folder; the
  file system, the network, other plugins, the host page.
- Everything reaches the game through `rtx.*` under the approved scopes.
- Execution: handlers run inside the host tick; a handler error is logged and the plugin keeps
  running; an error in the main chunk stops the plugin.
- Budget: about 40 M instructions or 1.5 s per tick and 64 MB memory; eight failing ticks in a
  row stop the plugin until it is saved (dev folder) or reinstalled.

### Porting an HTML plugin

- `await rtx.plugin.state.inventory()` becomes `rtx.state.inventory()`.
- `rtx.plugin.on("tick", fn)` becomes `rtx.on("tick", fn)`; `rtx.plugin.events.on` becomes
  `rtx.events.on`.
- Replace the page markup with `rtx.ui.render(tree)`; replace DOM event handlers with widget
  callbacks.
- `manifest.json`: add `"runtime": "lua"` and `"main": "main.lua"`, drop `entry`.
- `rtx.d.lua` (downloadable above) gives completion and types for the whole API in any editor
  that understands LuaLS annotations.

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

## Publishing

Plugins are free; submitting and voting need a free account. Browsing the catalog, reading any
plugin's source and downloading the SDK or a signed plugin are public.

### Packaging

- One `.zip` with `manifest.json` at its root.
- Limits: 200 files, 2 MB per file, 5 MB unzipped, 8 MB zip.
- Allowed types: `.html`, `.htm`, `.css`, `.js`, `.mjs`, `.lua`, `.json`, `.txt`, `.md`, `.svg`,
  `.png`, `.jpg`, `.jpeg`, `.gif`, `.webp`, `.woff`, `.woff2`, `.wav`.
- Rejected when: not a valid zip; manifest missing or invalid; a file type not allowed; an unsafe
  path (`..` or a leading `/`); the `entry`, `main` or `icon` file missing; any remote reference.
- A Lua bundle installs whole: every allowed file lands in the plugin folder for `require` and
  `rtx.plugin.readFile`.

### Submission

1. Submit (login): upload the zip in the developer area; the static checks run and a pending
   version is created. A plugin `id` belongs to the first account that submits it.
2. Review: a RuneTools admin reads the full source.
3. Approve or reject: on approval the exact zip bytes are signed and the version goes live in the
   public catalog and the in-client Browse tab. Rejections include notes; a live version can be
   revoked later.

### Signing

- Algorithm: ECDSA P-256 over SHA-256, over the exact bytes of the `.zip`.
- Headers on the download: `X-Plugin-Signature`, `X-Plugin-Alg`, `X-Plugin-Hash`,
  `X-Plugin-Version`, `X-Plugin-Slug`.
- Public key: `GET /api/plugins/pubkey`; the client pins it and refuses a bundle whose signature
  does not verify.
- Labels: `plugins-dev` bundles show "Unsigned developer plugin"; catalog installs show
  "Verified, signed by RuneTools".
