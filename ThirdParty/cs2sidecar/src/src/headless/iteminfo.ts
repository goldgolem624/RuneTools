// Decode item defs: name, worn (male) models, equip slot. Usage: node dist/iteminfo.js <id,id,...>
import { EngineCache } from "3d/modeltothree";
import { GameCacheLoader } from "cache/sqlite";
import { parse } from "opdecoder";

const CACHE = "C:\\ProgramData\\Jagex\\RuneScape";

(async () => {
    let engine = await EngineCache.create(new GameCacheLoader(CACHE, false));
    for (let id of process.argv[2].split(",").map(Number)) {
        let file = await engine.getGameFile("items", id).catch(() => null);
        if (!file) { console.log(id, "NO FILE"); continue; }
        let item: any = parse.item.read(file, engine.rawsource);
        let mids: number[] = [];
        if (item.maleModels_0?.id) mids.push(item.maleModels_0.id);
        if (item.maleModels_1) mids.push(item.maleModels_1);
        if (item.maleModels_2) mids.push(item.maleModels_2);
        console.log(id, JSON.stringify(item.name || null), "worn:", JSON.stringify(mids),
            "slot:", item.wearpos ?? item.equipSlotId ?? item.equipSlot ?? "?",
            "recol:", item.color_replacements?.length || 0, "retex:", item.material_replacements?.length || 0);
    }
})().catch(e => { console.error("ERR", e); process.exit(1); });
