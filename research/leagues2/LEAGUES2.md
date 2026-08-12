# Leagues II: Equilibrium (cache research)

Extracted from the live cache via the CS2 sidecar dumps (dbrows.json, enums.json, scripts).
Full machine readable data: leagues2.json (same folder). Regenerate with the CS2 panel
extraction plus scratch script gen.js (see git history of this file).

## Core identifiers
- League record: db table 326, row 19883 ("Leagues II: Equilibrium")
- Interface group: 1152
- League points: read via var reference in db 326.28 -> varp 13521
- Active league selector: varp 12314 (script20117 maps it to the league dbrow)
- Tasks completed counter: varbit 58389 (script21081)
- Region unlock thresholds: enum 9287, tasks required per region (cap 450 in script21081)
- New UI flag: varbit 61493 (script20243 routes to script21082 when 1)

## Relic tiers (db 327 tier list -> db 328 tiers -> db 329 relics)
- Tier 1 (row 19890): 10 pts, 5x XP: Endless Harvest (row 19560, spr 36323/36324), Golden Touch (row 19562, spr 36325/36326), Survivalist (row 19561, spr 36327/36328)
- Tier 2 (row 19891): 750 pts, 8x XP: Animal Wrangler (row 19563, spr 36329/36330), Superheated (row 19564, spr 36331/36332), Divine Druid (row 19565, spr 36333/36334)
- Tier 3 (row 19892): 1750 pts, 8x XP: Nature's Network (row 19566, spr 35226/35227), Assassin's Insight (row 19567, spr 35228/35229), Voidwalker (row 19568, spr 35224/35225)
- Tier 4 (row 19893): 3500 pts, 12x XP: Crystal Grace (row 19571, spr 36339/36340), Transmutation (row 19569, spr 36335/36336), Antiquarian (row 19570, spr 36337/36338)
- Tier 5 (row 19894): 6000 pts, 12x XP: Clue Connoisseur (row 19572, spr 36341/36342), Production Master (row 19573, spr 36343/36344), Devout (row 19574, spr 36345/36346)
- Tier 6 (row 19895): 12000 pts, 16x XP: Perkfection (row 19575, spr 36347/36348), Rejuvenated (row 19576, spr 36349/36350)
- Tier 7 (row 19896): 20000 pts, 16x XP: Infernal Fire (row 19577, spr 36351/36352), Naragi Edict (row 19578, spr 36353/36354), Icyenic Faith (row 19579, spr 36355/36356)

## Blessings (8 tiers of 3, alignment 1=order? 2=balance? 3=chaos?, see json)
- Tier 1 (row 19580, cost 1): Adrenaline Junkie (spr 36357/36358), Big Boned (spr 36359/36360), Teragard's Aegis (spr 36361/36362)
- Tier 2 (row 19581, cost 3): Abyssal Cinders (spr 36363/36364), Barkscales (spr 36365/36366), Striking Light (spr 36367/36368)
- Tier 3 (row 19582, cost 5): Avernic Rampage (spr 36369/36370), Eternal Sustenance (spr 36371/36372), Steadfast Will (spr 36373/36374)
- Tier 4 (row 19583, cost 9): Demon's Mark (spr 36375/36376), Splash Zone (spr 36377/36378), Sacred Fervor (spr 36379/36380)
- Tier 5 (row 19584, cost 12): Havoc Born (spr 36381/36382), True Equilibrium (spr 36383/36384), Higher Power (spr 36385/36386)
- Tier 6 (row 19585, cost 16): Unholy Critual (spr 36387/36388), Tearing Thorns (spr 36389/36390), Lord of Light (spr 36391/36392)
- Tier 7 (row 19586, cost 20): Perfidious (spr 36393/36394), Envenomed (spr 36395/36396), Tempered Heart (spr 36397/36398)
- Tier 8 (row 19587, cost 26): Chaotic Insight (spr 36399/36400), Power Archive (spr 36401/36402), Genesis Essence (spr 36403/36404)

## Trophies (row 19898)
- Bronze: 2000 pts, item 63625
- Iron: 4000 pts, item 63626
- Steel: 10000 pts, item 63627
- Mithril: 17000 pts, item 63628
- Adamant: 25000 pts, item 63629
- Rune: 35000 pts, item 63630
- Dragon: 48000 pts, item 63631

## Table map (CS2-space ids = sub*128 + master; dbrows.json carries `table`)
- 326 league headers: 17039 (Leagues: Catalyst, num 1), 19883 (Equilibrium, num 2)
- 327 tier-order rows: 17040 (L1), 19884 (L2 relics), 19885 (L2 blessings)
- 328 tier rows: L1 17041-17047, L2 relics 19890-19896, L2 blessings 19580-19587
- 329 relic AND blessing defs (blessings are relics hung off a second order row)
- 333 trophies (17057 L1, 19898 L2); 335 config (17048/19886); 336 localities (17055/19889)
- 334 tasks, SHARED across leagues: cols 1..N are per-league membership booleans
  (script20981 reads db 334.<leagueNum> with the number compiled in). 1272 rows:
  1113 Catalyst-era (most re-flagged into L2) + 156 new L2 rows (19612-19767).
- Task schema changed with the L2 update: col 0 component, col 3 achievement,
  col 4 completion var_reference, col 5 type, col 6 locality, col 7 tier, col 8 members.
- Header col 0 = league number; col 28 = points var_reference (L1 varp 12426,
  L2 varp 13521); col 22 = region enum 9287 on BOTH rows.
- Tier -> task points enum 9332 = {1:10, 2:30, 3:80, 4:200, 5:400} (both leagues).
- Relic pick vars: enum 9082 -> varbits 58410+ per 0-based tier; vb 58460 = bonus
  pick on tiers 1-3.
- Account identity (CS2-reviewed 2026-08-11): varp 12314 = league NUMBER, matched
  against db 326.0 by script20117; 0 = normal character, gates read '> 0' for any
  league and '== 2' for Equilibrium paths. vb 58421 = server XP multiplier used
  ONLY by the archaeology restoration preview (script14688 -> script20151); it is
  NOT the headline tier multiplier (observed 10 while the tier said 12x). The
  displayed 'N x XP' is db 328.4 printed verbatim (script20255), so the current
  multiplier is derived: highest tier row whose col-2 cost <= live points.
  vb 58531 = a percentage
  boost folded into script17524's SCALE over enum 16990 (chance modifier, exact
  subject unverified). vb 61685 = Equilibrium flag that zeroes a cost path in
  script17445/18542 when vp 12314 == 2 (likely free deaths/services).
- Regions: table 382 (19861-19871, alphabetical Anachronia..Wilderness): col 1 =
  locality-bit enum, col 2 = table-383 info row (name parsed from 'The X region
  covers'). Unlock state = varp 12327 locality BIT ARRAY (script20136 tests one
  bit; script20133 requires all of a region's bits; bits run 0-40 so the array
  continues into varp 12328). First unlock slot is always Karamja (script21081).
  Pick ORDER is not stored anywhere client-visible, only the accumulated mask.
- The in-app Leagues panel (ui-assets/panel_leagues.js) renders all of this per
  league with a league picker; updated 2026-08-10 for the multi-league schema.

## Notes
- Relic/blessing effect lists live in db 329.10 as (var_reference, value, toggleable, description)
  tuples; packed refs decode as varbit = value - 16777216 when >= 1<<24, else varp.
- db 329.7/329.8 are each relic's normal/selected icon SPRITES (graphic per script20257),
  not varbits. Active state = script20144 over the db 329.10 effect quads only.
- Root row cols 30-39 declare extra varbits (see json rootDeclared), semantics not yet pinned.
- Alignment names order/chaos/balance from the True Equilibrium blessing text; the per
  blessing alignment int and its varbit ref are in the json, mapping unverified in game.
