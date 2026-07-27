// Trace rsmv's opcode parse of a single RT7 model to find where the new format over-reads.
import { EngineCache } from "3d/modeltothree";
import { GameCacheLoader } from "cache/sqlite";
import { cacheMajors } from "../constants";
import { parse } from "opdecoder";
import { ushortToHalf } from "../utils";

const CACHE = "C:\\ProgramData\\Jagex\\RuneScape";

(async () => {
    let engine = await EngineCache.create(new GameCacheLoader(CACHE, false));
    let mid = +process.argv[2];
    let buf = await engine.getFileById(cacheMajors.models, mid);
    console.error("MODEL", mid, "len", buf.length, "buildnr", (engine.rawsource as any).getBuildNr?.());
    try {
        let m: any = parse.models.read(buf, engine.rawsource);
        let md = m.meshdata;
        console.error("vc", md.vertexCount, "f2", buf[2].toString(16));
        let pb = md.positionBuffer;
        console.error("pos[0..6] s16:", [...pb.slice(0, 18)]);
        let xs: number[] = []; for (let i = 0; i < pb.length; i += 3) xs.push(pb[i]);
        let mn = xs[0], mx = xs[0]; for (let v of xs) { if (v < mn) mn = v; if (v > mx) mx = v; }
        console.error("pos bbox x:", mn, "..", mx);
        console.error("renders:", md.renders.map((r: any) => { let m = 0; for (let v of r.buf) if (v > m) m = v; return { mat: r.materialArgument, n: r.buf.length, max: m }; }));
        let uv = md.uvBuffer;
        if (uv) {
            console.error("uv raw[0..4] half:", [0, 1, 2, 3].map(i => +ushortToHalf(uv[i]).toFixed(3)));
            console.error("uv swapped half:", [0, 1, 2, 3].map(i => +ushortToHalf(((uv[i] & 0xff) << 8) | (uv[i] >> 8)).toFixed(3)));
        }
    } catch (e: any) { console.error("THREW:", String(e).slice(0, 160)); }
})().catch(e => { console.error("ERR", e); process.exit(1); });
