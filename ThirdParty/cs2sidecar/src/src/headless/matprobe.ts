// Probe the RT7 material inputs that ModelGeometryRT7.fs actually consumes, to
// confirm they are cache-resident: compound (metalness/roughness) map,
// nonMetalSpecular, fresnelExponent, roughnessMultiplier, wrapping modes.
//
// usage: node dist/matprobe.js ids=995,1511,1129,4151
import { EngineCache, ThreejsSceneCache } from "3d/modeltothree";
import { parse } from "../opdecoder";
import { CacheFileSource } from "../cache";
import { GameCacheLoader } from "../cache/sqlite";

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
    const ids = (args.ids || "995,1511,1129,4151,20822").split(",").map(Number);
    const src: CacheFileSource = new GameCacheLoader();
    const engine = await EngineCache.create(src);
    const scene = await ThreejsSceneCache.create(engine);

    for (const id of ids) {
        let def: any;
        try {
            def = parse.item.read(await engine.getGameFile("items", id), engine.rawsource);
        } catch (e) {
            console.log(id, "ITEM READ FAIL", String(e));
            continue;
        }
        const modelId = def.baseModel;
        console.log(`\n=== item ${id} "${def.name ?? ""}" baseModel=${modelId}`);
        if (modelId == null) { console.log("   (no base model)"); continue; }

        let md: any;
        try {
            md = await (scene as any).getModelData(modelId);
        } catch (e) {
            console.log("   model load fail", String(e));
            continue;
        }
        if (!md || !md.meshes) { console.log("   no meshes"); continue; }
        const mats = new Set<number>(md.meshes.map((m: any) => m.materialId).filter((m: number) => m >= 0));
        console.log("   meshes:", md.meshes.length, "materialIds:", [...mats].join(","));
        for (const matId of mats) {
            let m: any;
            try { m = engine.getMaterialData(matId); } catch (e) { console.log("   mat", matId, "FAIL"); continue; }
            const raw = m.raw ?? {};
            console.log(
                `   mat ${matId}: v=${raw.v1 ? 1 : raw.v2 ? 2 : 0}` +
                ` diffuse=${m.textures?.diffuse ?? "-"}` +
                ` normal=${m.textures?.normal ?? "-"}` +
                ` compound=${m.textures?.compound ?? "-"}` +
                ` baseColorFraction=${m.baseColorFraction}` +
                ` alphamode=${m.alphamode}` +
                ` alphacutoff=${m.alphacutoff}`
            );
            const extra = raw.extra ?? {};
            const keys = Object.keys(extra);
            if (keys.length) console.log("      extra:", JSON.stringify(extra).slice(0, 300));
            // any field that smells like the shader's TextureSettings inputs
            for (const k of Object.keys(raw)) {
                if (/spec|rough|fresnel|metal|emiss|wrap|texmode/i.test(k)) {
                    console.log(`      raw.${k} =`, JSON.stringify((raw as any)[k]));
                }
            }
        }
    }
})().catch(e => { console.error(e); process.exit(1); });
