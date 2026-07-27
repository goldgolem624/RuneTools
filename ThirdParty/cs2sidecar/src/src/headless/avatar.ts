// Pure-node: build worn-item models posed on rsmv's skeleton+idle anim, export glTF. No GL renderer.
import { EngineCache, ThreejsSceneCache } from "3d/modeltothree";
import { GameCacheLoader } from "cache/sqlite";
import { RSModel } from "3d/scene/model";
import { exportThreeJsGltf } from "viewer/threejsrender";
import { animGroupToAnims } from "3d/scene/avatar";
import { parse } from "opdecoder";
import * as fs from "fs";

// browser polyfills (via @napi-rs/canvas) so rsmv can decode textures AND GLTFExporter can encode them into the glb
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
(globalThis as any).FileReader = class FileReader {
    result: any = null; onloadend: (() => void) | null = null; onload: (() => void) | null = null;
    readAsArrayBuffer(blob: any) { blob.arrayBuffer().then((b: ArrayBuffer) => { this.result = b; this.onloadend && this.onloadend(); this.onload && this.onload(); }); }
    readAsDataURL(blob: any) { blob.arrayBuffer().then((b: ArrayBuffer) => { this.result = "data:application/octet-stream;base64," + Buffer.from(b).toString("base64"); this.onloadend && this.onloadend(); this.onload && this.onload(); }); }
};

const CACHE = "C:\\ProgramData\\Jagex\\RuneScape";

(async () => {
    let src = new GameCacheLoader(CACHE, false);
    let engine = await EngineCache.create(src);
    let scene = await ThreejsSceneCache.create(engine);

    let equipped: number[] = JSON.parse(fs.readFileSync("equipped.json", "utf8"));
    console.log("equipped:", JSON.stringify(equipped));

    let models: any[] = [];
    if (process.env.MODELS) {
        // optional RECOLOR="src:dst,.." packed-HSL pairs applied to every model (e.g. hair-color recolor)
        let recol: [number, number][] = process.env.RECOLOR
            ? process.env.RECOLOR.split(",").filter(Boolean).map(p => { let s = p.split(":"); return [+s[0], +s[1]] as [number, number]; })
            : [];
        models = process.env.MODELS.split(",").filter(Boolean).map(id => ({ modelid: +id, mods: recol.length ? { replaceColors: recol } : {} }));
        console.log("rendering raw MODELS:", JSON.stringify(models.map((m: any) => m.modelid)), "recolor:", JSON.stringify(recol));
    } else {
    for (let itemid of equipped) {
        let file = await engine.getGameFile("items", itemid).catch(() => null);
        if (!file) { console.log("item", itemid, "no file"); continue; }
        let item = parse.item.read(file, engine.rawsource);
        let mods: any = {};
        if (item.color_replacements) mods.replaceColors = item.color_replacements;
        if (item.material_replacements) mods.replaceMaterials = item.material_replacements;
        let mids: number[] = [];
        if (item.maleModels_0?.id) mids.push(item.maleModels_0.id);
        if (item.maleModels_1) mids.push(item.maleModels_1);
        if (item.maleModels_2) mids.push(item.maleModels_2);
        console.log("item", itemid, JSON.stringify(item.name), "worn:", JSON.stringify(mids), "recol:", item.color_replacements?.length || 0, "retex:", item.material_replacements?.length || 0);
        for (let mid of mids) models.push({ modelid: mid, mods });
    }
    }
    console.log("total worn models:", models.length);

    let animgroup = 2699;  // default player anim group (idle)

    let anims = await animGroupToAnims(engine, animgroup);
    console.log("anims keys:", JSON.stringify(Object.keys(anims)), "default:", (anims as any).default);

    let model = new RSModel(scene, models);
    await model.model; // build mesh
    let animid = (anims as any).default ?? -1;
    if (process.env.NOANIM) { console.log("NOANIM: bind pose"); }
    else { console.log("using anim:", animid); await model.setAnimation(animid); }
    await new Promise(r => setTimeout(r, 50));

    // name each mesh by its materialId (meshes are 1:1 with modeldata.meshes, in order),
    // strip texture maps (node can't encode them), and dump materialId->{diffuse,frac} so we
    // can re-apply our own correct textures in the viewer.
    let md: any = (model as any).loaded.modeldata;
    let matset: any = {};
    let mi = 0;
    model.rootnode.traverse((o: any) => {
        if (o.isMesh) {
            let matId = md.meshes[mi]?.materialId ?? -1;
            o.name = "mat" + matId;
            if (!(matId in matset)) {
                let info: any = { diffuse: null, frac: 1 };
                if (matId >= 0) { try { let g: any = engine.getMaterialData(matId); info = { diffuse: g.textures?.diffuse ?? null, normal: g.textures?.normal ?? null, compound: g.textures?.compound ?? null, frac: g.baseColorFraction, alphamode: g.alphamode, wrapS: g.texmodes, wrapT: g.texmodet, stripAlpha: g.stripDiffuseAlpha }; } catch (e) { } }
                matset[matId] = info;
            }
            mi++;  // keep rsmv's full materials/textures (exported into the glb directly)
        }
    });
    fs.writeFileSync("avatar_mats.json", JSON.stringify(matset));
    console.log("matset:", JSON.stringify(matset));

    let glb = await exportThreeJsGltf(model.rootnode);
    fs.writeFileSync("avatar.glb", Buffer.from(glb));
    console.log("WROTE avatar.glb", glb.byteLength, "skeletontype:", (model as any).skeletontype);
    console.log("DONE");
})().catch(e => { console.error("AVATAR ERROR", e); process.exit(1); });
