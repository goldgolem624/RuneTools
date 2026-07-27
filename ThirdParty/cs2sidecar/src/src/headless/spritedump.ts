// Dump js5-8 sprites to PNG for inspection: are any of them item icons?
// usage: node dist/spritedump.js out=<dir> ids=1,2,3 | sample=40
import { EngineCache } from "3d/modeltothree";
import { GameCacheLoader } from "cache/sqlite";
import { parseSprite } from "3d/materials/sprite";
import { cacheMajors } from "../constants";
import * as fs from "fs";
import * as path from "path";

const napi = require("@napi-rs/canvas");
(globalThis as any).ImageData = napi.ImageData;
function makeCanvas(w?: number, h?: number) {
    const cv: any = napi.createCanvas(w || 1, h || 1);
    cv.toBlob = (cb: any, type?: string) => { cb(new Blob([cv.toBuffer(type || "image/png")], { type: type || "image/png" })); };
    cv.convertToBlob = (opts: any) => { const t = (opts && opts.type) || "image/png"; return Promise.resolve(new Blob([cv.toBuffer(t)], { type: t })); };
    return cv;
}
(globalThis as any).OffscreenCanvas = function (w: number, h: number) { return makeCanvas(w, h); };
(globalThis as any).document = { createElementNS: (ns: string, tag: string) => tag === "canvas" ? makeCanvas() : {}, createElement: (tag: string) => tag === "canvas" ? makeCanvas() : {} };

const CACHE = process.env.RTX_GAME_CACHE || "C:\\ProgramData\\Jagex\\RuneScape";

(async () => {
    const args: Record<string, string> = {};
    for (const a of process.argv.slice(2)) {
        const eq = a.indexOf("=");
        if (eq > 0) args[a.slice(0, eq)] = a.slice(eq + 1); else args[a] = "1";
    }
    const outdir = args.out || "sprites_out";
    fs.mkdirSync(outdir, { recursive: true });
    const src = new GameCacheLoader(CACHE, false);
    const engine = await EngineCache.create(src);

    let ids: number[] = [];
    if (args.ids) ids = args.ids.split(",").map(Number);
    else {
        // spread a sample across the whole index
        const index = await engine.getCacheIndex(cacheMajors.sprites);
        const present = index.filter(q => q).map(q => q.minor);
        const n = +(args.sample || 40);
        for (let i = 0; i < n; i++) ids.push(present[Math.floor(i * (present.length - 1) / (n - 1))]);
        console.log("index entries:", present.length, "min", present[0], "max", present[present.length - 1]);
    }
    for (const id of ids) {
        try {
            const buf = await engine.getFileById(cacheMajors.sprites, id);
            const subs = parseSprite(buf);
            for (let s = 0; s < subs.length; s++) {
                const img = subs[s].img;
                const cv: any = napi.createCanvas(img.width, img.height);
                cv.getContext("2d").putImageData(new napi.ImageData(new Uint8ClampedArray(img.data), img.width, img.height), 0, 0);
                fs.writeFileSync(path.join(outdir, `${id}_${s}_${img.width}x${img.height}.png`), cv.toBuffer("image/png"));
            }
            console.log(id, "subs:", subs.length, subs.map(s => s.img.width + "x" + s.img.height).join(","));
        } catch (e) {
            console.log(id, "ERR", (e as Error).message);
        }
    }
})();
