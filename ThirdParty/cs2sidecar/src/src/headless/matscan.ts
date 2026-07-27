// Scan items for materials with an INTERMEDIATE baseColorFraction and/or a saturated
// baseColor. Those items discriminate the whitening target (baseColor vs white): if the
// engine mixes the vertex colour toward baseColor, the render hue shifts toward baseColor;
// if toward white, it only desaturates. Output: one line per (item, material).
// usage: node dist/matscan.js start=0 end=30000
import { EngineCache, ThreejsSceneCache } from "3d/modeltothree";
import { GameCacheLoader } from "cache/sqlite";
import { parse } from "opdecoder";

const CACHE = process.env.RTX_GAME_CACHE || "C:\\ProgramData\\Jagex\\RuneScape";

function argmap() {
    const a: Record<string, string> = {};
    for (const s of process.argv.slice(2)) {
        const i = s.indexOf("=");
        if (i > 0) a[s.slice(0, i)] = s.slice(i + 1);
        else a[s] = "1";
    }
    return a;
}

(async () => {
    const args = argmap();
    const start = +(args.start || 0), end = +(args.end || 30000);
    const src = new GameCacheLoader(CACHE, false);
    const engine = await EngineCache.create(src);
    const scene = await ThreejsSceneCache.create(engine);
    let missrun = 0;
    for (let id = start; id < end; id++) {
        let def: any;
        try { def = parse.item.read(await engine.getGameFile("items", id), engine.rawsource); missrun = 0; }
        catch (e) { if (++missrun > 3000) break; continue; }
        if (!def.baseModel) continue;
        let md: any;
        try { md = await scene.getModelData(def.baseModel); } catch (e) { continue; }
        if (!md?.meshes) continue;
        const mats = new Set<number>(md.meshes.map((m: any) => m.materialId).filter((m: number) => m >= 0));
        for (const matId of mats) {
            let mat: any;
            try { mat = engine.getMaterialData(matId); } catch (e) { continue; }
            if (!mat?.raw?.v0 || !mat.textures?.diffuse) continue;
            const f = mat.baseColorFraction;
            const b = mat.baseColor;
            const sat = Math.max(...b) - Math.min(...b);
            const mid = f > 0.05 && f < 0.95;
            if (mid || (f > 0.05 && sat > 0.2)) {
                console.log(JSON.stringify({ id, name: def.name, matId, frac: +f.toFixed(3), base: b.map((x: number) => +x.toFixed(3)), sat: +sat.toFixed(3) }));
            }
        }
    }
})();
