# RuneTools Plugin SDK

Build your own tools inside RuneTools -- custom tabs and overlays -- using plain HTML, CSS,
and JS. A plugin reads live game state and draws overlays through the `window.rtx.plugin` API.

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
| `entry`             | string   | Entry HTML file inside the bundle.                              |
| `icon`              | string   | Optional. Bare filename of a plugin icon inside the bundle.     |
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
| `clipboard`  | `rtx.plugin.clipboard.copy` (copy-only; nothing is read back) |

`rtx.plugin.ui.*` and the meta/event helpers are always available. The user approves scopes
on first enable, and the host enforces them on every call regardless of the manifest.

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

## API reference

All `state.*` reads act on the **current account** shown in the panel. Shapes below are the
exact JSON the host returns; log a call's result during development to see every field.

### state.read

```js
await rtx.plugin.state.player();   // alias of state.info()
await rtx.plugin.state.info();
// -> { in:true, x:3221, y:3218, plane:0, region:12850, lx:33, ly:18, anim:-1, moving:false,
//      interact: { type:1, id:3079, uid:12345, name:"Goblin" } | null }
// -> { in:false }                       when not in-game / unreadable
//    type: 1 = NPC, 2 = player. x/y are world tile coords. anim -1 = none.
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
// -> { open, character, cached_at, count, items:[ [slot, id, stack, name], ... ] }
//    Same shape as bank(): live while that storage UI is open, otherwise the
//    per-character disk cache (open:false).

await rtx.plugin.state.groundItems();
// -> [ { id, x, y, plane }, ... ]   dropped item stacks lying on the ground
//    id = item id (resolve the name with cache.itemInfo); x/y are world tile coords.
//    Empty [] when there are none / you're not in-game.

await rtx.plugin.state.scene(range);  // range = 1..64 tiles (clamped)
// -> nearby entities within range, each with type ('player'|'npc'|'object'),
//    id, name, dist, and (where applicable) combat level and plane, plus a count.

await rtx.plugin.state.varps("659,3274");  // comma-separated varp ids (string capped at 200 chars)
// -> { "659": 990, "3274": 120 }           map of id -> raw value

await rtx.plugin.state.varbits([46468, 46463]); // array of varbit ids (<=64)
// -> { "46468": 1, "46463": 100 }          map of id -> live value (backing varp+bits
//                                          resolved from the cache automatically)

await rtx.plugin.state.interface(1184, [4, 10, 15]); // group id + component ids
// -> { group:1184, open:true, hasAbs:true,
//      comps:[ { comp:4, text:"Acting Guildmaster Reiniger", x, y, w, h },
//              { comp:10, text:"Are you here to sign up...", x, y, w, h },
//              { comp:15, x, y, w, h } ] }
//    Live text + absolute screen rect of named components of an open interface. e.g. the NPC chat
//    box (1184): comp 4 = NPC name, comp 10 = message, comp 15 = the continue button. open:false
//    when that interface isn't showing; x/y/w/h present only when hasAbs (movable-panel origin known).

await rtx.plugin.state.interfaceGroup(919); // one OPEN interface group id
// -> { widgets:[ { t:[group,comp,sub], d:<depth>, r:[x,y,w,h], ty, x:<text>, s:<sprite>,
//                  it:<itemId>, n:<amount>, a:[absX,absY]? }, ... ] }
//    The FULL live widget tree of one open group (what the Interfaces tab shows) --
//    use when you need every component rather than a few named ones. Heavier than
//    state.interface; poll it sparingly.

await rtx.plugin.state.varcs([1118, 1119]); // array of varc-int ids (<=64)
// -> { "1118": 384, "1119": 2 }            map of id -> live varc value (0 when absent)

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
// -> { buffs:[ ... ], debuffs:[ ... ] }     active buff/debuff entries

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
await rtx.plugin.cache.enumInfo(id);  // -> { "<key>": value, ... }  (id->name/value roster)
await rtx.plugin.cache.paramDef(id);  // -> { type[, int][, str] }  param definition
//    ({} while the host's param reader is unavailable)
await rtx.plugin.cache.modelIcon(id); // -> string: PNG data URL for an interface type-6
//    MODEL comp, keyed by MODEL id ("" if not in the pack)
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
await rtx.plugin.overlay.highlightRects([]);   // clear

// Draw ground markers on world tiles (the same primitive the clue/quest guides use). Up to 16
// tiles, each { x, y, plane, label }. Replaces the previous set; pass [] to clear.
rtx.plugin.overlay.guideTiles([{ x:3221, y:3218, plane:0, label:"dig here" }]);

// Optional x2/y2 (x/y = SW corner, x2/y2 = NE corner) turns a mark into one flat ground
// rect spanning those tiles -- use for long walkways or zones whose interactable loc is
// only a 1x1 end piece (e.g. an agility log).
rtx.plugin.overlay.guideTiles([{ x:2474, y:3430, x2:2474, y2:3435, plane:0, label:"Log balance" }]);

rtx.plugin.overlay.clearHighlight();         // clear both highlight layers

// Big centre-screen banner text (the Dungeoneering boss-warning channel). '' clears.
rtx.plugin.overlay.centerText("DODGE - icicles!");
```

### notify.os (Windows notifications)

```js
rtx.plugin.notify.windows("RuneToolsX", "A ship has returned");
```

An OS-level toast outside the game window. Hard-capped at one per 10 seconds -- send it
on real events (a ship returned, a rare drop), never on a timer.

### clipboard (copy-only)

```js
rtx.plugin.clipboard.copy(JSON.stringify(plan));
```

Copies text to the user's clipboard (capped 64 KB). Nothing can be read back; to accept
pasted data, give the user a textarea to paste into.

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

### ui (always available)

```js
rtx.plugin.ui.setHeight(420);     // resize the plugin frame (60..4000 px)
rtx.plugin.ui.setTitle("My Tool");// reserved (no-op for now)
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

## The sandbox

Plugins run in a sandboxed frame and use the `rtx.plugin` APIs documented above:

- A plugin sees only the data its manifest scopes grant, and has no access to the host page, other plugins, or other accounts.
- The frame CSP blocks network and dynamic code (`fetch`/XHR/WebSocket/`eval`/`Function`), so bundles must be fully self-contained (local `.html/.css/.js/.svg/.png/.woff2`).

## Packaging

A submission is one `.zip` with `manifest.json` at its root. Upload limits: at most 200 files,
2 MB per file, 5 MB uncompressed, 8 MB zip. Allowed types: `.html`, `.htm`, `.css`, `.js`, `.mjs`,
`.json`, `.svg`, `.png`, `.jpg`, `.jpeg`, `.gif`, `.webp`, `.woff`, `.woff2`, `.wav`. A bundle is
rejected if it isn't a valid zip, the manifest is missing or invalid, a file type isn't allowed, a
path is unsafe (`..` or a leading `/`), the `entry`/`icon` file is missing, or anything references a
remote resource. (`eval` / `fetch` / `WebSocket` / dynamic `import` are flagged for the reviewer but
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
