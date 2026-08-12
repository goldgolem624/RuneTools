// Build the Leagues II: Equilibrium research dataset from the live cache extraction.
// Reads ~\RuneToolsX\cs2\{dbrows,enums}.json, writes leagues2.json + LEAGUES2.md
// into the RuneToolsX research folder.
const fs = require("fs");
const os = require("os");
const path = require("path");
const dir = path.join(os.homedir(), "RuneToolsX", "cs2");
const outdir = process.argv[2];
fs.mkdirSync(outdir, { recursive: true });
const rows = new Map(JSON.parse(fs.readFileSync(path.join(dir, "dbrows.json"), "utf8")).map(r => [r.id, r.cols]));
const enums = new Map(JSON.parse(fs.readFileSync(path.join(dir, "enums.json"), "utf8")).map(e => [e.id, e]));

// packed var reference: >= 1<<24 is a varbit, else a varp
function refVar(v) { return v >= 16777216 ? { varbit: v - 16777216 } : { varp: v }; }
function col(r, c) { return (r && r[c]) || []; }
function one(r, c) { return col(r, c)[0]; }

const root = rows.get(19883);

function effects(r) {
    // blessing rows have an (alignment ref, int) tuple at col 9 whose 2nd sub
    // flattens into col 10 ahead of the effect refs; right-align to shed it
    const refsRaw = col(r, "10"), vals = col(r, "11"), toggle = col(r, "12"), descs = col(r, "13");
    const refs = refsRaw.slice(Math.max(0, refsRaw.length - vals.length));
    return refs.map((ref, i) => ({ ...refVar(ref), value: vals[i], toggleable: toggle[i] === 1, desc: descs[i] }));
}

function relic(id) {
    const r = rows.get(id);
    // 329.7/329.8 are the normal/selected icon sprites (graphic per script20257)
    return {
        row: id, name: one(r, "0"), desc: one(r, "1"), itemId: one(r, "5") ?? null,
        sprites: { normal: one(r, "7"), selected: one(r, "8") }, effects: effects(r),
    };
}
const relicTiers = col(rows.get(one(root, "9")), "0").map((tierRow, i) => {
    const t = rows.get(tierRow);
    return {
        tier: i + 1, row: tierRow, pointsRequired: one(t, "2"), xpMultiplier: one(t, "4"),
        relics: col(t, "1").map(relic),
        tierPassives: col(t, "5").map((ref, j) => ({ ...refVar(ref), value: col(t, "6")[j], desc: col(t, "7")[j] })),
        unlockNotes: col(t, "10"),
    };
});

function blessing(id) {
    const r = rows.get(id);
    return {
        row: id, name: one(r, "0"), desc: one(r, "1"), cost: one(r, "2"),
        alignment: one(r, "3"), flag4: one(r, "4"), itemId: one(r, "5") ?? null,
        sprites: { normal: one(r, "7"), selected: one(r, "8") }, alignmentVar: refVar(one(r, "9")),
        effects: effects(r),
    };
}
const blessRoot = rows.get(one(root, "10"));
const blessingTiers = col(blessRoot, "0").map((tierRow, i) => {
    const t = rows.get(tierRow);
    return { tier: i + 1, row: tierRow, unlockCost: one(t, "2"), flag3: one(t, "3"), flag4: one(t, "4"),
             blessings: col(t, "1").map(blessing), notes: col(t, "10") };
});

const troph = rows.get(one(root, "12"));
const trophies = col(troph, "0").map((pts, i) => ({ points: pts, itemId: col(troph, "1")[i], name: col(troph, "2")[i] }));

const filt = rows.get(one(root, "13"));
const filters = {
    categoriesRow: 19887, categories: col(rows.get(19887), "0"),
    regionsRow: 19888, regions: col(rows.get(19888), "0"),
    localitiesRow: 19889, localities: col(rows.get(19889), "3"),
};

function enumEntries(id) {
    const e = enums.get(id);
    return e ? e.entries : null;
}

const data = {
    generated: new Date().toISOString(),
    league: {
        dbrow: 19883, dbtable: 326, name: one(root, "1"), shortName: one(root, "2"),
        interfaceGroup: one(root, "7"),
        maxPointsBar: one(root, "6"),
        pointsVar: refVar(one(root, "28")),
        varps: { activeLeagueSelector: 12314 },
        varbits: {
            tasksCompleted: 58389, newUiFlag: 61493,
            rootDeclared: {
                col30to34: ["30", "31", "32", "33", "34"].map(c => one(root, c)),
                col35to39: ["35", "36", "37", "38", "39"].map(c => one(root, c)),
            },
        },
        enums: {
            regionTaskThresholds: { id: one(root, "22"), entries: enumEntries(one(root, "22")) },
            enum9321: { id: one(root, "23"), entries: enumEntries(one(root, "23")) },
            enum9278: { id: one(root, "24"), entries: enumEntries(one(root, "24")) },
        },
    },
    relicTiers, blessingTiers, trophies, filters,
    scripts: {
        pointsPanel: 20243, regionPanel: 21081, relicVarLookup: 15144,
        toggleableEffects: 20265, leagueRowFromVarp: 20117, trophyRow: 20250, leagueGraphic: 20914,
    },
};
fs.writeFileSync(path.join(outdir, "leagues2.json"), JSON.stringify(data, null, 1));

// short markdown summary
const md = [];
md.push("# Leagues II: Equilibrium (cache research)");
md.push("");
md.push("Extracted from the live cache via the CS2 sidecar dumps (dbrows.json, enums.json, scripts).");
md.push("Full machine readable data: leagues2.json (same folder). Regenerate with the CS2 panel");
md.push("extraction plus scratch script gen.js (see git history of this file).");
md.push("");
md.push("## Core identifiers");
md.push("- League record: db table 326, row 19883 (\"Leagues II: Equilibrium\")");
md.push("- Interface group: 1152");
md.push("- League points: read via var reference in db 326.28 -> varp 13521");
md.push("- Active league selector: varp 12314 (script20117 maps it to the league dbrow)");
md.push("- Tasks completed counter: varbit 58389 (script21081)");
md.push("- Region unlock thresholds: enum 9287, tasks required per region (cap 450 in script21081)");
md.push("- New UI flag: varbit 61493 (script20243 routes to script21082 when 1)");
md.push("");
md.push("## Relic tiers (db 327 tier list -> db 328 tiers -> db 329 relics)");
for (const t of relicTiers) {
    md.push(`- Tier ${t.tier} (row ${t.row}): ${t.pointsRequired} pts, ${t.xpMultiplier}x XP: ` +
            t.relics.map(r => `${r.name} (row ${r.row}, spr ${r.sprites.normal}/${r.sprites.selected})`).join(", "));
}
md.push("");
md.push("## Blessings (8 tiers of 3, alignment 1=order? 2=balance? 3=chaos?, see json)");
for (const t of blessingTiers) {
    md.push(`- Tier ${t.tier} (row ${t.row}, cost ${t.unlockCost}): ` +
            t.blessings.map(b => `${b.name} (spr ${b.sprites.normal}/${b.sprites.selected})`).join(", "));
}
md.push("");
md.push("## Trophies (row 19898)");
for (const t of trophies) { md.push(`- ${t.name}: ${t.points} pts, item ${t.itemId}`); }
md.push("");
md.push("## Table map (CS2-space ids = sub*128 + master; dbrows.json carries `table`)");
md.push("- 326 league headers: 17039 (Leagues: Catalyst, num 1), 19883 (Equilibrium, num 2)");
md.push("- 327 tier-order rows: 17040 (L1), 19884 (L2 relics), 19885 (L2 blessings)");
md.push("- 328 tier rows: L1 17041-17047, L2 relics 19890-19896, L2 blessings 19580-19587");
md.push("- 329 relic AND blessing defs (blessings are relics hung off a second order row)");
md.push("- 333 trophies (17057 L1, 19898 L2); 335 config (17048/19886); 336 localities (17055/19889)");
md.push("- 334 tasks, SHARED across leagues: cols 1..N are per-league membership booleans");
md.push("  (script20981 reads db 334.<leagueNum> with the number compiled in). 1272 rows:");
md.push("  1113 Catalyst-era (most re-flagged into L2) + 156 new L2 rows (19612-19767).");
md.push("- Task schema changed with the L2 update: col 0 component, col 3 achievement,");
md.push("  col 4 completion var_reference, col 5 type, col 6 locality, col 7 tier, col 8 members.");
md.push("- Header col 0 = league number; col 28 = points var_reference (L1 varp 12426,");
md.push("  L2 varp 13521); col 22 = region enum 9287 on BOTH rows.");
md.push("- Tier -> task points enum 9332 = {1:10, 2:30, 3:80, 4:200, 5:400} (both leagues).");
md.push("- Relic pick vars: enum 9082 -> varbits 58410+ per 0-based tier; vb 58460 = bonus");
md.push("  pick on tiers 1-3.");
md.push("- Account identity (CS2-reviewed 2026-08-11): varp 12314 = league NUMBER, matched");
md.push("  against db 326.0 by script20117; 0 = normal character, gates read '> 0' for any");
md.push("  league and '== 2' for Equilibrium paths. vb 58421 = server XP multiplier used");
md.push("  ONLY by the archaeology restoration preview (script14688 -> script20151); it is");
md.push("  NOT the headline tier multiplier (observed 10 while the tier said 12x). The");
md.push("  displayed 'N x XP' is db 328.4 printed verbatim (script20255), so the current");
md.push("  multiplier is derived: highest tier row whose col-2 cost <= live points.");
md.push("  vb 58531 = a percentage");
md.push("  boost folded into script17524's SCALE over enum 16990 (chance modifier, exact");
md.push("  subject unverified). vb 61685 = Equilibrium flag that zeroes a cost path in");
md.push("  script17445/18542 when vp 12314 == 2 (likely free deaths/services).");
md.push("- Regions: table 382 (19861-19871, alphabetical Anachronia..Wilderness): col 1 =");
md.push("  locality-bit enum, col 2 = table-383 info row (name parsed from 'The X region");
md.push("  covers'). Unlock state = varp 12327 locality BIT ARRAY (script20136 tests one");
md.push("  bit; script20133 requires all of a region's bits; bits run 0-40 so the array");
md.push("  continues into varp 12328). First unlock slot is always Karamja (script21081).");
md.push("  Pick ORDER is not stored anywhere client-visible, only the accumulated mask.");
md.push("- The in-app Leagues panel (ui-assets/panel_leagues.js) renders all of this per");
md.push("  league with a league picker; updated 2026-08-10 for the multi-league schema.");
md.push("");
md.push("## Notes");
md.push("- Relic/blessing effect lists live in db 329.10 as (var_reference, value, toggleable, description)");
md.push("  tuples; packed refs decode as varbit = value - 16777216 when >= 1<<24, else varp.");
md.push("- db 329.7/329.8 are each relic's normal/selected icon SPRITES (graphic per script20257),");
md.push("  not varbits. Active state = script20144 over the db 329.10 effect quads only.");
md.push("- Root row cols 30-39 declare extra varbits (see json rootDeclared), semantics not yet pinned.");
md.push("- Alignment names order/chaos/balance from the True Equilibrium blessing text; the per");
md.push("  blessing alignment int and its varbit ref are in the json, mapping unverified in game.");
md.push("");
fs.writeFileSync(path.join(outdir, "LEAGUES2.md"), md.join("\n"));
console.log("wrote", path.join(outdir, "leagues2.json"), "and LEAGUES2.md");
console.log("relic tiers:", relicTiers.length, "blessing tiers:", blessingTiers.length, "trophies:", trophies.length);
console.log("enum 9287 entries:", data.league.enums.regionTaskThresholds.entries ? data.league.enums.regionTaskThresholds.entries.length : "MISSING");
