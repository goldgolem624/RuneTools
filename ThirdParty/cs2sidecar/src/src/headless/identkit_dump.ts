// Dump the live identity-kit (config page 3) via rsmv's decoder. Shows body-part models +
// recolours for every kit entry (the player base body + default clothing options).
import { EngineCache } from "3d/modeltothree";
import { GameCacheLoader } from "cache/sqlite";
import { cacheMajors, cacheConfigPages } from "../constants";
import { parse } from "opdecoder";

const CACHE = "C:\\ProgramData\\Jagex\\RuneScape";
const BODYPART = ["head", "jaw/beard", "body/torso", "arms", "hands", "legs", "feet"];

(async () => {
    let src = new GameCacheLoader(CACHE, false);
    let engine = await EngineCache.create(src);
    let arch = await engine.getArchiveById(cacheMajors.config, cacheConfigPages.identityKit);
    console.log("IDENTKIT total entries:", arch.length);
    let bad = 0;
    for (let f of arch) {
        try {
            let kit: any = parse.identitykit.read(f.buffer, engine.rawsource);
            let bp = (typeof kit.bodypart == "number") ? (BODYPART[kit.bodypart] || ("bp" + kit.bodypart)) : "-";
            let leftover = (kit.$opcode_undecoded || "");
            console.log("KIT", f.fileid, "bodypart=" + bp, "models=" + JSON.stringify(kit.models || null), "head=" + (kit.headmodel ?? "-"), "recol=" + (kit.recolor ? kit.recolor.length : 0), "raw=" + JSON.stringify(kit));
        } catch (e: any) {
            bad++;
            if (bad <= 8) console.log("KIT", f.fileid, "DECODE-ERR", String(e).slice(0, 120), "bytes=" + Buffer.from(f.buffer).slice(0, 24).toString("hex"));
        }
    }
    // hair colors: enum 2343 (key->RGB) + enum 2345 (key->display index = the varp 78 value). avatar.ts kitcolors.hair.
    try {
        let cd: any = parse.enums.read(await engine.getFileById(cacheMajors.enums, 2343), engine.rawsource);
        let od: any = parse.enums.read(await engine.getFileById(cacheMajors.enums, 2345), engine.rawsource);
        console.log("HAIRCOLORS (varp 78 value -> color):");
        for (let q of od.intArrayValue2.values) {
            let f = cd.intArrayValue2.values.find((w: any) => w[0] == q[0]);
            if (!f) continue;
            let rgb = (f[1] >>> 0) & 0xffffff;
            console.log("HAIRCOL idx=" + q[1] + " rgb=#" + rgb.toString(16).padStart(6, "0") + " (" + ((rgb >> 16) & 0xff) + "," + ((rgb >> 8) & 0xff) + "," + (rgb & 0xff) + ")");
        }
    } catch (e: any) { console.log("HAIRCOLORS ERR", String(e).slice(0, 200)); }
    // FINDCATALOG: scan every enum (major 17, paged 256) for the make-over hairstyle catalog,
    // i.e. an int->int enum that maps the varp 280 index to the varp 22 kit fileid. Anchors from
    // user ground truth: index 0 -> kit 45 (bald), index 338 -> kit 1025 (hime).
    if (process.env.FINDCATALOG) {
        console.log("FINDCATALOG: scanning enums for index<->fileid catalog (0->45, 338->1025)");
        for (let page = 0; page < 64; page++) {
            let carch: any;
            try { carch = await engine.getArchiveById(cacheMajors.enums, page); } catch (e) { continue; }
            if (!carch) continue;
            for (let cf of carch) {
                let eid = page * 256 + cf.fileid; let en: any;
                try { en = parse.enums.read(cf.buffer, engine.rawsource); } catch (e) { continue; }
                let vals = en.intArrayValue2 && en.intArrayValue2.values;
                if (!vals || vals.length < 40) continue;
                let fwd: any = {}, rev: any = {};
                for (let kv of vals) { fwd[kv[0]] = kv[1]; rev[kv[1]] = kv[0]; }
                // hime is at index 338, so the catalog has >=339 entries -- list all big enums + their anchor values
                if (vals.length >= 330) console.log("  BIG enum " + eid + " len=" + vals.length + " [0]=" + fwd[0] + " [18]=" + fwd[18] + " [338]=" + fwd[338]);
                // and any enum that has both kit fileids (45 & 1025) as values, regardless of size
                if (rev[45] !== undefined && rev[1025] !== undefined) console.log("  HAS-BOTH enum " + eid + " len=" + vals.length + " 45@key" + rev[45] + " 1025@key" + rev[1025] + " [0]=" + fwd[0]);
            }
        }
        console.log("FINDCATALOG done");
    }
    if (process.env.DBSCAN) {
        // hunt the make-over hairstyle catalog in dbrows: a row holding an array of kit fileids
        // indexed by varp 280. Fingerprint fileids (must co-occur in one row if it's a list):
        // 45(idx0) 52(23) 140(45) 722(206) 1025(338).
        let walk = (v: any, out: number[]) => {
            if (typeof v === "number") out.push(v);
            else if (Array.isArray(v)) { for (let x of v) walk(x, out); }
            else if (v && typeof v === "object") { for (let kk in v) walk(v[kk], out); }
        };
        // female head kit fileids (bp7) from the identity kit archive
        let femHeads = new Set<number>();
        for (let kf of arch) { try { let k: any = parse.identitykit.read(kf.buffer, engine.rawsource); if (k.bodypart === 7) femHeads.add(kf.fileid); } catch (e) {} }
        let rows = await engine.getArchiveById(cacheMajors.config, cacheConfigPages.dbrows);
        console.log("DBSCAN: dbrows =", rows.length, " femHeads =", femHeads.size);
        // distinctive fingerprint kits (large -> not coincidental). Find the table whose rows
        // collectively contain all of them -> that's the hairstyle catalog table.
        let fp = [140, 722, 1025, 52];
        let tableKits: any = {};
        for (let f of rows) {
            let r: any; try { r = parse.dbrows.read(f.buffer, engine.rawsource); } catch (e) { continue; }
            let ints: number[] = []; walk(r, ints); let s = new Set(ints); let t = (r as any).table;
            for (let kk of fp) if (s.has(kk)) (tableKits[t] = tableKits[t] || new Set()).add(kk);
        }
        for (let t in tableKits) if (tableKits[t].size >= 3) console.log("TABLE " + t + " has fp kits: " + [...tableKits[t]]);
        console.log("DBSCAN done");
    }
    if (process.env.TABLEDUMP) {
        let tid = +process.env.TABLEDUMP;
        let fh = new Set<number>();
        for (let kf of arch) { try { let k: any = parse.identitykit.read(kf.buffer, engine.rawsource); if (k.bodypart === 7) fh.add(kf.fileid); } catch (e) {} }
        let rows = await engine.getArchiveById(cacheMajors.config, cacheConfigPages.dbrows);
        let mine: any[] = [];
        for (let f of rows) {
            let r: any; try { r = parse.dbrows.read(f.buffer, engine.rawsource); } catch (e) { continue; }
            if ((r as any).table !== tid) continue;
            let kit = -1, col = -1;
            for (let cd of r.unk01.columndata) for (let c of cd.columns) for (let v of c.value) if (typeof v === "number" && fh.has(v)) { kit = v; col = cd.id; }
            mine.push({ fid: f.fileid, kit, col });
        }
        mine.sort((a, b) => a.fid - b.fid);
        console.log("TABLE " + tid + " rows=" + mine.length + " kit-seq[0..40]=" + JSON.stringify(mine.slice(0, 41).map(m => m.kit)));
        for (let i of [0, 23, 45, 206, 338]) if (mine[i]) console.log("  pos" + i + " kit=" + mine[i].kit + " col=" + mine[i].col + " fid=" + mine[i].fid);
    }
    if (process.env.STRUCTPROBE) {
        // probe structs at the varp 280 ids; look for a param equal to the matching kit fileid
        for (let sid of process.env.STRUCTPROBE.split(",").map(Number)) {
            try {
                let st: any = parse.structs.read(await engine.getFileById(cacheMajors.structs, sid), engine.rawsource);
                console.log("STRUCT " + sid + " = " + JSON.stringify(st).slice(0, 400));
            } catch (e: any) { console.log("STRUCT " + sid + " ERR " + String(e).slice(0, 80)); }
        }
    }
    if (process.env.DUMPENUM) {
        let eid = +process.env.DUMPENUM;
        let en: any = parse.enums.read(await engine.getFileById(cacheMajors.enums, eid), engine.rawsource);
        let vals = en.intArrayValue2 && en.intArrayValue2.values;
        console.log("ENUM " + eid + " len=" + (vals ? vals.length : 0) + " raw=" + JSON.stringify({ ...en, intArrayValue2: undefined }).slice(0, 200));
        if (vals) console.log("PAIRS " + eid + ": " + JSON.stringify(vals));
    }
    console.log("DECODE FAILURES:", bad, "/", arch.length);
})().catch(e => { console.error("ERR", e); process.exit(1); });
