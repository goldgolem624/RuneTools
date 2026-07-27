// Dump the full raw parsed material struct so we can see which PBR inputs the cache carries.
// usage: node dist/matraw.js 1435 9365 19012
import { EngineCache } from "3d/modeltothree";
import { GameCacheLoader } from "../cache/sqlite";

(async () => {
    const engine = await EngineCache.create(new GameCacheLoader());
    for (const s of process.argv.slice(2)) {
        const id = Number(s);
        const m: any = engine.getMaterialData(id);
        console.log("\n=== material " + id);
        console.log("  derived: " + JSON.stringify({
            textures: m.textures, baseColorFraction: m.baseColorFraction, baseColor: m.baseColor,
            alphamode: m.alphamode, alphacutoff: m.alphacutoff,
            texmodes: m.texmodes, texmodet: m.texmodet,
        }));
        const raw: any = m.raw || {};
        for (const ver of ["v0", "v1", "v2"]) {
            if (!raw[ver]) continue;
            console.log("  " + ver + ": " + JSON.stringify(raw[ver], null, 1).slice(0, 2500));
        }
    }
})().catch(e => { console.error(e); process.exit(1); });
