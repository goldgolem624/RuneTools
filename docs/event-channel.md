# Event channel (rtxEvents)

Motivation: docs/gap-audit-2026-08-29.md, A3. Panels and plugins used to poll at 4 Hz for things
that are events (an xp drop, a container change, a script run, a game tick). This channel carries
them from the game's inbound packet framer to the page with ~100 ms latency, and exposes the
game tick as a subscription.

## Pipeline

1. Companion `companion/EventShare.h` + `SceneData.cpp` (`EventRecord`, called from the
   existing framer detour right before the chat-ring branch). A named section
   `Local\RuneToolsXEvents_v2_<pid>` holds a 2048-record ring. Each record is a per-record
   odd/even seqlock (`seq = (index+1)*2`, `| 1` while the body is being filled, `MemoryBarrier()`
   on both sides), `written` is published last. A 256-bit opcode mask (`mask[8]`, bit `op & 31`
   of word `op >> 5`) chooses what is recorded; the companion seeds it with the default set
   unless the launcher already wrote one (`maskSet`). `0x15` message_game is never recorded
   (the chat ring owns it). Payload is truncated to 1024 bytes, `length` keeps the wire size,
   `truncated` counts those records. Counters: `written`, `truncated`, `inbound`.
   Layout: Record = 5 x u32/i32 + 1024 = 1044 bytes; Share = 80-byte header + 2048 x 1044
   = 565,328 bytes.

   Default mask: 0x00 run_weight, 0x04 skill_update, 0x05 and 0x51 ge_offer, 0x2B
   container_update, 0x52 runclientscript, 0x5C run_energy, 0x8D ping_echo
   (decimal csv `0,4,5,43,81,82,92,141`).

2. Reader `src/reader/Reader.cpp`: `EventsJson(pid, since)` returns
   `{"ok":true,"seq":<cursor>,"tick":<reader game tick or -1>,"from","inbound","truncated","hook","mask":[8],"events":[...]}`
   with every record after `since` (cap 512, oldest first). `EventsMaskSet(pid, mask[8])` writes
   the mask (creating the section if the companion has not, so a mask set before injection
   survives it).

   Decoded kinds (layouts already documented in this repo, in
   `docs/server_packets_handler_map.md` and the `npDecoders` table of `ui-assets/panel_netprobe.js`):

   | op | kind | fields |
   |----|------|--------|
   | 0x04 | `skill_update` | `skill`, `name`, `level`, `xp` |
   | 0x2B | `container_update` | `container`, `flags`, `slots:[{slot,item,qty}]` (`item` -1 = emptied), `partial` (true when the 1024-byte window cut the slot list) |
   | 0x52 | `runclientscript` | `script`, `sig`, `args` (ints and strings in signature order) |
   | 0x5C | `run_energy` | `value` |
   | 0x00 | `run_weight` | `value` (i16) |
   | 0x8D | `ping` | `a`, `b` (the two u32 the client echoes) |

   Raw (no field layout documented in the repo, so nothing is guessed): 0x05 / 0x51 `ge_offer`
   and any other opcode enabled through the mask. Shape: `{"kind":"raw","op":N,"len":L,"hex":"..."}`.
   A decoded kind falls back to raw when its payload is shorter than its layout needs.

3. Bridge `src/launcher/Bridge.cpp`: `events(pid, sinceSeq)` (served_obj, key `events:<pid>`,
   fail_json on noclient/noargs) and `eventsMask(pid, csv)` (bool). Broker
   (`ui-assets/core/rtx-plugins.js`): `state.events` (scope state.read), `host.eventsMask`
   (scope actuator, panel-only).

4. Page bus `ui-assets/core/rtx-data.js`: `rtxEvents.on(kind, fn)`, `off`, `emit`; `'*'` hears
   everything. `ui-assets/core/rtx-boot.js`: `rtxEventsTick` runs every 100 ms from attachBridge
   (not inside refresh, which stays at 4 Hz), busy-guarded like refresh, cursor reset on pid
   change, dedups on `seq` (the served cache may replay the previous span). Emits one event per
   record keyed by `kind`, and `gameTick` `{tick, dtMs}` whenever the reader's tick counter moves.
   `window.rtxEventsPoll` exposes cursor / poll / fail counters for the panel.

5. SDK: plugins with `state.read` get one `events` push per host cycle (batched, in capture
   order) alongside the unchanged 4 Hz `tick` heartbeat; `rtx.plugin.events.on(kind, fn)` fans
   it out per kind. See PLUGIN_SDK.md, "Game events".

6. Panel `ui-assets/panel_events.js` (Developer > Events): last 200 events newest first,
   per-kind counters, gameTick counter with measured interval (mean / min / max of the last 20),
   mask field, Pause and Clear.

## Live verification (after a rebuild and a fresh inject)

Open Developer > Events. The tick card is the first check:

1. Standing still, logged in: `gameTick` increases by about 100 per minute and `interval` reads
   about 600 ms (min/max within roughly 500 to 700 ms; the reader samples the tick counter,
   so single outliers of one poll period, 100 ms, are expected). `polls` grows ~10 per second,
   `fails` stays flat. If `fails` grows instead, the companion is not loaded or the section is
   from an older build (`EventsJson` returns ok:false).
2. Gain xp (any skill action, e.g. one pickpocket, one log cut): one `skill_update` row per xp
   drop with the skill name, level and the new total xp. The `skill_update` counter goes up by
   exactly the number of xp drops seen in the chat/xp popups.
3. Pick up an item, or drop one: a `container_update` row for container 93 (inventory) listing
   the changed slot with the item id and quantity; drop shows the slot as `empty`. Opening the
   bank produces a large one with `[partial]` (the 256-byte window) and counts as one event.
4. Open the Grand Exchange: `ge_offer` rows (op 0x05 / 0x51) appear as raw hex, one per offer
   slot the server sends; interface scripts fired by the GE open show as `runclientscript`
   rows with a script id and arguments. Closing it fires more `runclientscript` rows.
5. Walk or run: `run_energy` rows while running (value counts down), `run_weight` after an
   equipment/inventory change. `ping` rows appear on their own about once a minute.
6. Pause: click Pause, do any of the above; the list freezes while the counters keep going.
   Resume shows the new rows.
7. Mask: set the field to `4` and Set; only `skill_update` rows arrive from then on (counters
   for the others stop). Set `4,21`: still no chat lines (0x15 is refused). Click Default to
   restore.
8. Plugin path (optional): any plugin with `state.read` and
   `rtx.plugin.events.on('skill_update', console.log)` in its code logs the same records the
   panel shows, batched per 4 Hz push.

Record the measured interval from step 1 in the report of the run.


runclientscript records cut by the window decode the arguments they have and report `script: -1, partial: true` (the script id is the last field on the wire).

ge_offer (0x05 / 0x51), verified live on 2026-08-29 with two offers in two slots: bytes 0-3 are a fixed `00 02 07 03` header, byte 4 is the slot index, bytes 5-6 the item id (big-endian); the remaining fields are not pinned down, so the event carries the live offer read from the GE slot array in memory (`offer`) plus the packet item (`item`) for cross-checking.

## Reading a runclientscript line

Script 10623(struct, mode) is the buff bar: it adds or removes ONE entry. Traced through the
decompilation on 2026-08-30:

- 10624 checks param 2961 on the struct; when set, 9101 swaps in the tier variant for the
  player, choosing by STAT_BASE(5) against the thresholds in params 2965 and 2967 and returning
  the struct in 2964, 2966 or 2968. So a tiered effect resolves to its current rank here.
- mode 0 removes (15426 -> 15427), mode 1 adds (10625 -> 15425).
- Both call 15428, which walks the bar's component list from param 8100, following the next
  link in param 8105 for up to 50 entries, and matches on param 8106.
  **Param 8106 is the identity of a buff bar slot**: it holds the struct id the slot shows.
- Remove: found -> 10785 clears it. Add: present -> return, else 10626 builds it (11423 create,
  10819 dress, 10818 value, 11426 set, 10822 refresh).

A struct describes itself in **param 2794**, which is a DESCRIPTION and not a title: these
structs carry no name anywhere in the cache (their other params are sprite ids and
thresholds; following them lands on unrelated structs). The panel therefore shows the name
when the description leads with one ("Wise - ...", "Pulse Core - 2% XP Boost") and the
opening clause otherwise, with the full text on the row as a tooltip.
Observed live: 35826 Wise, 35804 the explosion-at-100% perk, 6196 Quiver ammo, 30925 Pulse Core.

Consequence: hooking 10623 gives exact buff add and remove events, with the tier already
resolved by 9101, instead of polling the bar.

### Other scripts seen on this channel

- **1264(title, when, what, extra, where, world, who, fc, link, ticks)**: the Community Event
  notice. It assembles the blurb (When / What / Where / World / Who / FC / Link, each only when
  non empty), writes it to component 1234:11 or 1465:36 depending on varbits 26696 and 27169,
  unhides 1234:4 and 1465:30, and starts script 1269 as a countdown that blanks the notice and
  hides it again. The trailing int is that countdown length in timer ticks.
- **ping (0x8D)**: an 8 byte fixed packet carrying two 32 bit big endian values. Observed values
  are uniformly spread across the full range with no monotonic component across a session, so
  they read as a server challenge the client echoes back rather than a timestamp or a counter.
  The reply is outbound and this channel captures inbound only, so the echo is not verified
  here: the two numbers are reported verbatim rather than being given a meaning we cannot prove.

### Naming a buff bar entry

The buff struct itself has no title, but Invention perks do: **dbtable 8** holds the perk name in
column 1 and, in column 2, the same id the buff struct carries in **param 2802**. So
struct 35804 (2802 = 26466) is Aftershock and struct 35826 (2802 = 26341) is Wise, confirmed
against rows 567 and 497. Buffs with no perk link fall back to the name their description leads
with (Pulse Core, Quiver ammo), with the full description on the row.

Sprite joins were tried and rejected: the icon id in param 4677 or 2802 is shared by several
named structs (34918 matches Pulse Core, Advanced pulse core and Boon of Plenty I), so it cannot
identify one on its own.

#### How a buff bar entry is named

A buff struct has no title of its own, but it always carries a link to something that does:

| param | meaning | example |
| --- | --- | --- |
| 2802 | Invention perk: dbtable 8 column 2 holds this id, column 1 the name | 26466 = Aftershock, 26341 = Wise |
| 4677 | the source item | 51490 = Sign of the porter VII, 33719 = Tirannwn quiver 1, 34918 = Advanced pulse core |
| 2794 | the description, which for short entries IS the name | "Quiver ammo", "Pulse Core - 2% XP Boost" |

The panel resolves in that order: perk name, then the description when it is short or leads with
a name before a dash, then the source item with its tier suffix removed (the status is Sign of
the porter, the item is Sign of the porter VII), and only then the opening clause. The full
description stays on the row as a tooltip.

## What a full capture shows (mask = every opcode, 2026-08-30)

Traffic arrives in bursts about 600 ms apart, one per game tick. A typical burst is:

| packet | name | note |
| --- | --- | --- |
| 0x2D | player_info | 200 to 300 bytes every tick, the GPI bitmask block |
| 0x5A | npc_info | the GNI block, length follows how many NPCs are near |
| 0x6D | zone_update | 45 bytes per record, several records when more than one zone changed |
| 0xBE | telemetry_grid | repeating id/value triples ending ff ff ff; telemetry, not game state |
| 0xB4 | server_tick | ZERO length, always last in the burst: the tick boundary |
| 0xC8 | (unnamed) | zero length, first in some bursts |
| 0x4E, 0x5F | iface_set | 6 and 3 byte component writes, often in runs |
| 0x06 | iface_prop_i8 | 3 bytes, payload constant at ff14a1 in this capture |
| 0x02, 0x43 | (unnamed) | 10 and 12 bytes, payload constant across every occurrence |

Two things worth acting on:

1. **0xB4 is an exact tick edge.** The panel now counts it and reports its interval separately
   from the polled counter, and it is the right source for anything tick sensitive, since it
   does not depend on our 100 ms sampling.
2. **0xBE is telemetry**, so its constant chatter can be ignored when reading a capture.

Still unidentified: the layouts of 0x4E, 0x5F, 0x6D and 0xBE, and the meaning of the two 32 bit
values in ping (0x8D), which stay uniformly random across every sample.
