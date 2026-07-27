// Decompile CS2 clientscripts (idx12) to readable TS. Usage: node dist/cs2decomp.js <id,id,...>
import { EngineCache } from "3d/modeltothree";
import { GameCacheLoader } from "cache/sqlite";
import { renderClientScript } from "clientscript/index";
import * as fs from "fs";

const CLIENTSCRIPT_MAJOR = 12;

const CACHE = "C:\\ProgramData\\Jagex\\RuneScape";

(async () => {
    let engine = await EngineCache.create(new GameCacheLoader(CACHE, false));
    let source = engine.rawsource;
    for (let id of process.argv[2].split(",").map(Number)) {
        let buf = await source.getFileById(CLIENTSCRIPT_MAJOR, id).catch(() => null);
        if (!buf) { console.log("// script " + id + " NO FILE"); continue; }
        let text: string;
        try {
            text = await renderClientScript(source, buf as Buffer, id);
        } catch (e) {
            // the first renderClientScript call in a fresh process always throws
            // "not compatible" (lazy init inside rsmv); retry once now that it's warm
            try {
                text = await renderClientScript(source, buf as Buffer, id);
            } catch (e2) {
                console.log("// script " + id + " DECOMPILE ERROR: " + (e2 as Error).message);
                console.log((e2 as Error).stack);
                continue;
            }
        }
        fs.writeFileSync("cs2_" + id + ".txt", text);
        console.log("// ===== script " + id + " (" + text.length + " chars) -> cs2_" + id + ".txt =====");
    }
    process.exit(0);
})().catch(e => { console.error("ERR", e); process.exit(1); });
