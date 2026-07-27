// Dump the raw parsed item definition (all opcodes) for the given ids.
// usage: node dist/itemdef.js 995 1511
import { EngineCache } from "3d/modeltothree";
import { parse } from "../opdecoder";
import { GameCacheLoader } from "../cache/sqlite";

(async () => {
    const engine = await EngineCache.create(new GameCacheLoader());
    for (const s of process.argv.slice(2)) {
        const id = Number(s);
        const def: any = parse.item.read(await engine.getGameFile("items", id), engine.rawsource);
        console.log("\n=== item " + id + " " + JSON.stringify(def.name));
        for (const [k, v] of Object.entries(def)) {
            if (v === null || v === undefined) continue;
            console.log("   " + k + " = " + JSON.stringify(v).slice(0, 200));
        }
    }
})().catch(e => { console.error(e); process.exit(1); });
