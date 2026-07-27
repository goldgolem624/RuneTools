// Per-channel stats (and optional PNG dump) for any texture kind, straight through the same
// loader the icon renderer uses. usage: node dist/texstat.js kind=compound ids=24640,31652 [png=dir]
import { EngineCache, ThreejsSceneCache } from "3d/modeltothree";
import { GameCacheLoader } from "cache/sqlite";
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
    const kind = (args.kind || "diffuse") as any;
    const ids = (args.ids || "").split(",").map(Number).filter(n => n > 0);
    const src = new GameCacheLoader(CACHE, false);
    const engine = await EngineCache.create(src);
    const scene = await ThreejsSceneCache.create(engine);
    for (const id of ids) {
        try {
            const tf = await scene.getTextureFile(kind, id, false);
            const img = await tf.toImageData();
            const n = img.width * img.height;
            const sums = [0, 0, 0, 0];
            for (let i = 0; i < n; i++) for (let c = 0; c < 4; c++) sums[c] += img.data[i * 4 + c];
            console.log(kind, id, `${img.width}x${img.height}`,
                "RGBA mean", sums.map(s => (s / n / 255).toFixed(3)).join(" "));
            if (args.png) {
                fs.mkdirSync(args.png, { recursive: true });
                const cv: any = napi.createCanvas(img.width, img.height);
                cv.getContext("2d").putImageData(new napi.ImageData(new Uint8ClampedArray(img.data), img.width, img.height), 0, 0);
                fs.writeFileSync(path.join(args.png, `${kind}_${id}.png`), cv.toBuffer("image/png"));
            }
        } catch (e) {
            console.log(kind, id, "ERR", (e as Error).message);
        }
    }
})();
