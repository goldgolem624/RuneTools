// Pure-node rsmv data dump (no GL/electron). Uses rsmv's own decode to emit ground-truth JSON.
import { EngineCache, ThreejsSceneCache } from "3d/modeltothree";
import { GameCacheLoader } from "cache/sqlite";
import { itemToModel } from "3d/scene";
import { modifyMesh } from "3d/mapsquare";
import { parse } from "opdecoder";
import * as fs from "fs";

const CACHE = "C:\\ProgramData\\Jagex\\RuneScape";

async function dumpModelMats(scene: ThreejsSceneCache, engine: EngineCache, modelid: number, mods: any, label: string) {
    let md = await scene.getModelData(modelid);
    let out: any[] = [];
    for (let mesh of md.meshes) {
        let mod = modifyMesh(mesh, mods);
        let matid = mod.materialId; // already materialArgument-1 from rt7model
        let diffuse: number | null = null;
        if (matid >= 0) {
            try { let m = engine.getMaterialData(matid); diffuse = (m.textures as any)?.diffuse ?? null; } catch (e) { }
        }
        out.push({ rawMaterialId: mesh.materialId, modifiedMaterialId: matid, diffuse, verts: mod.attributes.pos.count, indices: mod.indices.count });
    }
    console.log(`${label} model ${modelid}:`, JSON.stringify(out));
    return out;
}

(async () => {
    let src = new GameCacheLoader(CACHE, false);
    let engine = await EngineCache.create(src);
    let scene = await ThreejsSceneCache.create(engine);

    let itemid = +(process.env.ITEMID || "20822");
    let itemfile = await engine.getGameFile("items", itemid);
    let item = parse.item.read(itemfile, engine.rawsource);
    console.log("ITEM", itemid, item.name);
    console.log("baseModel(op1):", item.baseModel);
    console.log("maleModels:", item.maleModels_0?.id, item.maleModels_1, item.maleModels_2);
    console.log("color_replacements(op40):", JSON.stringify(item.color_replacements));
    console.log("material_replacements(op41):", JSON.stringify(item.material_replacements));

    let mods: any = {};
    if (item.color_replacements) mods.replaceColors = item.color_replacements;
    if (item.material_replacements) mods.replaceMaterials = item.material_replacements;

    // what rsmv's item viewer renders:
    let mm = await itemToModel(scene, itemid);
    console.log("itemToModel models:", JSON.stringify(mm.models.map((m: any) => m.modelid)));

    if (item.baseModel) await dumpModelMats(scene, engine, item.baseModel, mods, "BASE");
    if (item.maleModels_0?.id) await dumpModelMats(scene, engine, item.maleModels_0.id, mods, "WORN0");
    if (item.maleModels_1) await dumpModelMats(scene, engine, item.maleModels_1, mods, "WORN1");

    let matsToInspect = (process.env.MATS || "16510,16511,22953,904").split(",").map(Number);
    for (let mid of matsToInspect) {
        let m: any = engine.getMaterialData(mid);
        let out: any = {};
        for (let k of Object.keys(m)) { let v = m[k]; out[k] = (v && typeof v == "object" && !Array.isArray(v)) ? JSON.stringify(v) : v; }
        console.log("MATERIAL", mid, JSON.stringify(out));
    }
    fs.writeFileSync("dump_done.txt", "ok");
    console.log("DONE");
})().catch(e => { console.error("DUMP ERROR", e); process.exit(1); });
