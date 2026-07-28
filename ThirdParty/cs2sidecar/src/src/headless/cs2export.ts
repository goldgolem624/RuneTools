// Full CS2 extraction for the RuneToolsX "CS2 Scripts" panel.
// Decompiles every js5-12 clientscript from the LIVE cache, applies ground-truth
// identifier names and inserts ground-truth /* name */ annotations where the
// decompiler's own type system declares an id-space:
//   <n> as obj/npc/loc/quest/stat/achievement/dbrow/struct  -> /* <name> */
//   enum_getvalue(a, b, <enumId>, <literalKey>)             -> /* = <value> */
//   IF_*(..., <packed>)                                     -> /* if G:C 'label' */
// Rename sources, in priority order (first writer wins):
//   quest trackers > npc/loc morph vars > clientscript switch tables (slayer/boss
//   kill logs, collection logs, toolbelt, shop caps, unlockables, death prices,
//   curated varcs -- see buildScriptNames) > var_reference params (ref_<owner>)
//   > achievement requirements (ach_<name>)
// Every name is a string read from the cache itself (or, for the script tables, a
// literal read of the named script's decompiled switch); ids without a cache name
// stay untouched. Formats: items = build-949 table, achievements = 949 table,
// enums = js5-17 with op209 as a flag, dbrows = smart rowcount + row-major,
// structs = op249 only.
//
// Usage: node dist/cs2export.js <outdir>
// Output tree:
//   <outdir>/scripts/clientscript-<id>.ts
//   <outdir>/progress.json   {stage, done, total, startedAt}  (updated ~1/s)
//   <outdir>/meta.json       {date, buildnr, total, ok, failed[], names, annotations, notes[]}
//   <outdir>/names.json      generated rename tables
import { EngineCache, iterateConfigFiles } from "3d/modeltothree";
import { GameCacheLoader } from "cache/sqlite";
import { renderClientScript } from "clientscript/index";
import { parse } from "opdecoder";
import { cacheMajors } from "../constants";
import * as fs from "fs";
import * as path from "path";

const CACHE = "C:\\ProgramData\\Jagex\\RuneScape";
const CLIENTSCRIPT_MAJOR = 12;

function slug(nm: string) {
    return nm.toLowerCase().replace(/[^a-z0-9]+/g, "_").replace(/^_+|_+$/g, "").slice(0, 36);
}

// ---- little big-endian reader over a Buffer ----
class R {
    p = 0;
    constructor(public d: Buffer) { }
    get len() { return this.d.length; }
    u8() { return this.d[this.p++]; }
    i8() { const v = this.u8(); return v > 127 ? v - 256 : v; }
    u16() { const v = this.d.readUInt16BE(this.p); this.p += 2; return v; }
    i16() { const v = this.d.readInt16BE(this.p); this.p += 2; return v; }
    u24() { const v = (this.d[this.p] << 16) | (this.d[this.p + 1] << 8) | this.d[this.p + 2]; this.p += 3; return v; }
    i32() { const v = this.d.readInt32BE(this.p); this.p += 4; return v; }
    varuint() {                              // items: u16, high bit extends to u32
        const fw = this.u16();
        if ((fw & 0x8000) === 0) { return fw; }
        return ((fw & 0x7fff) << 16) | this.u16();
    }
    usmart() {                               // 1-2 byte unsigned smart
        if (this.d[this.p] & 0x80) { return this.u16() & 0x7FFF; }
        return this.u8();
    }
    smart32() {                              // 2-or-4 byte smart
        if (this.d[this.p] & 0x80) { return this.i32() & 0x7FFFFFFF; }
        return this.u16();
    }
    cstr() {                                 // null-terminated cp1252 (latin1-compatible read)
        const s = this.p;
        while (this.p < this.d.length && this.d[this.p]) { this.p++; }
        const v = this.d.toString("latin1", s, this.p);
        this.p++;
        return v;
    }
}

type NameTables = { varbit: Map<number, string>, varp: Map<number, string>, varc: Map<number, string> };
type CastTables = { [kind: string]: Map<number, string> };

// ---- quest configs (js5-2 archive 35): op1 = name, op3/op4 = progress vars ----
function decodeQuest(buf: Buffer): { name: string | null, varps: number[], varbits: number[] } {
    const r = new R(buf);
    let name: string | null = null;
    const varps: number[] = [], varbits: number[] = [];
    while (r.p < r.len) {
        const op = r.u8();
        if (op === 0) break;
        else if (op === 1) { r.u8(); name = r.cstr(); }
        else if (op === 2) { r.u8(); r.cstr(); }
        else if (op === 3 || op === 4) {
            const n = r.u8();
            for (let i = 0; i < n; i++) { const v = r.u16(); r.p += 8; (op === 3 ? varps : varbits).push(v); }
        }
        else if (op === 5) r.p += 2;
        else if (op === 6 || op === 7 || op === 9) r.p += 1;
        else if (op === 8) { }
        else if (op === 10) { const n = r.u8(); r.p += n * 4; }
        else if (op === 12) r.p += 4;
        else if (op === 13 || op === 14) { const n = r.u8(); r.p += n * 2; }
        else if (op === 15) r.p += 2;
        else if (op === 17) { r.p += (buf[r.p] & 0x80) ? 4 : 2; }
        else if (op === 18 || op === 19) {
            const n = r.u8();
            for (let i = 0; i < n; i++) { r.p += 12; r.cstr(); }
        }
        else if (op === 249) {
            const n = r.u8();
            for (let i = 0; i < n; i++) { const t = r.u8(); r.p += 3; if (t === 1) r.cstr(); else r.p += 4; }
        }
        else break;
    }
    return { name, varps, varbits };
}

// ---- items (js5-19), build-949 opcode table (validated 61590/61590) ----
// refKeys: param ids typed var_reference (209); matching int params are returned so the
// owner's name can annotate the referenced var ((v >>> 24) == 1 -> varbit in low 24 bits).
function decodeItemName(buf: Buffer, refKeys?: Set<number>): { name: string | null, refs: Array<[number, number]> } {
    const r = new R(buf);
    let name: string | null = null;
    const refs: Array<[number, number]> = [];
    try {
    while (r.p < r.len) {
        const op = r.u8();
        if (op === 0) break;
        else if (op === 1) r.varuint();
        else if (op === 2) name = r.cstr();
        else if (op === 3) r.cstr();
        else if ([4, 5, 6, 0x0A, 0x5F, 0x6E, 0x6F, 0x70].includes(op)) r.u16();
        else if (op === 7 || op === 8) r.i16();
        else if (op === 9) { }
        else if ([0x0B, 0x0F, 0x10, 0x41, 0x9C, 0x9D, 0xA5, 0xA7, 0xA8, 0xB2].includes(op)) { }
        else if ([0x0C, 0x2B, 0x45].includes(op)) r.i32();
        else if ([0x0D, 0x0E, 0x1B, 0x60, 0x73, 0x86].includes(op)) r.u8();
        else if (op === 0x12) r.u16();
        else if ([0x17, 0x18, 0x19, 0x1A, 0x4E, 0x4F, 0x5A, 0x5B, 0x5C, 0x5D].includes(op)) r.varuint();
        else if (op >= 0x1E && op <= 0x22) r.cstr();
        else if (op >= 0x23 && op <= 0x27) r.cstr();
        else if (op === 0x28 || op === 0x29) { const n = r.u8(); for (let i = 0; i < n; i++) { r.u16(); r.u16(); } }
        else if (op === 0x2A) { const n = r.u8(); for (let i = 0; i < n; i++) { r.u8(); r.i8(); } }
        else if (op === 0x2C || op === 0x2D) r.u16();
        else if (op === 0x5E) r.u16();
        else if (op === 0x61 || op === 0x62) r.u16();
        else if (op >= 0x64 && op <= 0x6D) { r.u16(); r.u16(); }
        else if (op === 0x71 || op === 0x72) r.i8();
        else if ([0x79, 0x7A, 0x8B, 0x8C, 0xA1, 0xA2, 0xA3].includes(op)) r.u16();
        else if (op === 0x7D || op === 0x7E) { r.u8(); r.u8(); r.u8(); }
        else if (op >= 0x7F && op <= 0x82) { r.u8(); r.u16(); }
        else if (op === 0x84) { const n = r.u8(); for (let i = 0; i < n; i++) { r.u8(); r.u16(); } }
        else if (op >= 0x8E && op <= 0x92) r.u16();
        else if (op >= 0x96 && op <= 0x9A) r.u16();
        else if (op === 0xA4) r.cstr();
        else if (op === 0xB5) { r.i32(); r.i32(); }
        else if (op >= 0xBE && op <= 0xC7) { r.u24(); r.u16(); }
        else if (op === 0xC9 || op === 0xCA) r.u24();
        else if (op >= 0xCB && op <= 0xD0) r.u24();
        else if (op === 0xF9) {
            const n = r.u8();
            for (let i = 0; i < n; i++) {
                const t = r.u8(); const key = r.u24();
                if (t === 1) { r.cstr(); }
                else {
                    const v = r.i32();
                    if (refKeys && refKeys.has(key)) { refs.push([key, v]); }
                }
            }
        }
        else break;   // unknown op: keep whatever name was already read
    }
    } catch (e) { }   // partial decode: keep any name already read
    return { name, refs };
}

// ---- achievements (js5-57), build-949 table (validated 5009/5009) ----
// Also captures the requirement VARBITS (ops 9/10 single, 13/14 multi) so achievement
// names can annotate otherwise-unnamed varbits.
function decodeAchievementName(buf: Buffer): { name: string | null, reqVbs: number[] } {
    const r = new R(buf);
    let name: string | null = null;
    const reqVbs: number[] = [];
    try {
    while (r.p < r.len) {
        const op = r.u8();
        if (op === 0) break;
        else if (op === 1) { r.u8(); name = r.cstr(); }
        else if (op === 2) {
            const isi = r.u8(); r.u8(); r.u8(); r.cstr();
            for (let i = 0; i < Math.max(0, isi - 1); i++) { r.u8(); r.u8(); r.cstr(); }
        }
        else if (op === 3) r.u16();
        else if (op === 4) r.smart32();
        else if (op === 5) r.u8();
        else if (op === 6) r.u16();
        else if (op === 7) { r.u8(); r.cstr(); }
        else if (op === 9 || op === 10) {
            const n = r.u8();
            for (let i = 0; i < n; i++) { r.u8(); r.smart32(); r.u8(); r.cstr(); r.u8(); reqVbs.push(r.u16()); }
        }
        else if (op === 13 || op === 14) {
            const n = r.usmart();
            for (let i = 0; i < n; i++) {
                r.u8(); r.smart32(); r.u8(); r.cstr();
                const m = r.u8();
                for (let j = 0; j < m; j++) { reqVbs.push(r.u16()); }
            }
        }
        else if (op === 23 || op === 25) {
            const n = r.usmart();
            for (let i = 0; i < n; i++) { r.u8(); r.u16(); r.u8(); r.u8(); r.cstr(); r.u8(); }
        }
        else if (op === 8 || op === 12) {
            const n = r.usmart();
            for (let i = 0; i < n; i++) { r.u8(); r.u16(); r.cstr(); r.u8(); r.u16(); }
        }
        else if (op === 11) { const n = r.u8(); for (let i = 0; i < n; i++) { r.u24(); } }
        else if (op === 15) { const n = r.usmart(); for (let i = 0; i < n; i++) { r.u24(); } }
        else if (op === 16) r.u16();
        else if ([17, 19, 27, 35].includes(op)) { }
        else if ([18, 29, 31, 37, 38].includes(op)) r.u8();
        else if (op === 20 || op === 21) { const n = r.u8(); for (let i = 0; i < n; i++) { r.u24(); } }
        else if (op === 26) { r.u16(); r.u8(); r.u8(); name = r.cstr(); }
        else if (op === 28) { const n = r.u8(); for (let i = 0; i < n; i++) { r.u8(); } }
        else if (op === 30) { const n = r.u8(); for (let i = 0; i < n; i++) { r.usmart(); } }
        else if (op === 32) { r.u8(); r.u8(); r.u8(); }
        else break;
    }
    } catch (e) { }   // partial decode: keep any name already read
    return { name, reqVbs };
}

// ---- structs (js5-22): op249 params only; label = first string param by lowest key ----
function decodeStructLabel(buf: Buffer, refKeys?: Set<number>):
        { label: string | null, refs: Array<[number, number]>, strs: Map<number, string>, ints: Map<number, number> } {
    const r = new R(buf);
    let best: { key: number, val: string } | null = null;
    const refs: Array<[number, number]> = [];
    const strs = new Map<number, string>();
    const ints = new Map<number, number>();
    while (r.p < r.len) {
        const op = r.u8();
        if (op === 0) break;
        else if (op === 249) {
            const n = r.u8();
            for (let i = 0; i < n; i++) {
                const t = r.u8(); const key = r.u24();
                if (t === 1) {
                    const v = r.cstr();
                    strs.set(key, v);
                    if (!best || key < best.key) { best = { key, val: v }; }
                } else {
                    const v = r.i32();
                    ints.set(key, v);
                    if (refKeys && refKeys.has(key)) { refs.push([key, v]); }
                }
            }
        }
        else break;
    }
    return { label: best ? best.val.slice(0, 60) : null, refs, strs, ints };
}

// ---- enums (js5-17), build-949: op209 is a payload-less flag ----
function decodeEnum(buf: Buffer): { keyType: number, valType: number, entries: Map<number, string> } | null {
    const r = new R(buf);
    let keyType = -1, valType = -1;
    const entries = new Map<number, string>();
    while (r.p < r.len) {
        const op = r.u8();
        if (op === 0) break;
        else if (op === 1 || op === 101) keyType = r.u8();
        else if (op === 2 || op === 102) valType = r.u8();
        else if (op === 3) r.cstr();
        else if (op === 4) r.i32();
        else if (op === 5) { const n = r.u16(); for (let i = 0; i < n; i++) { const k = r.i32(); entries.set(k, r.cstr()); } }
        else if (op === 6) { const n = r.u16(); for (let i = 0; i < n; i++) { r.i32(); r.i32(); } }
        else if (op === 7) { r.u16(); const n = r.u16(); for (let i = 0; i < n; i++) { const k = r.u16(); entries.set(k, r.cstr()); } }
        else if (op === 8) { r.u16(); const n = r.u16(); for (let i = 0; i < n; i++) { r.u16(); r.i32(); } }
        else if (op === 131 || op === 209) { }
        else return null;
    }
    return { keyType, valType, entries };
}

// Full-row form of the same format, for the analysis dumps: every column's values,
// strings and ints alike (0x24 = string, else i32).
function decodeDbrowFull(buf: Buffer): { [col: number]: (number | string)[] } | null {
    const r = new R(buf);
    const cols: { [col: number]: (number | string)[] } = {};
    let any = false;
    while (r.p < r.len) {
        const op = r.u8();
        if (op === 0) break;
        else if (op === 3) {
            r.u8();
            for (;;) {
                const b = r.u8();
                if (b === 0xFF) break;
                const colid = b & 0x3F;
                const nsub = r.u8();
                const types: number[] = [];
                for (let i = 0; i < nsub; i++) { types.push(r.usmart()); }
                const rowcount = r.usmart();
                for (let row = 0; row < rowcount; row++) {
                    for (let sIx = 0; sIx < nsub; sIx++) {
                        const v = types[sIx] === 0x24 ? r.cstr() : r.i32();
                        const key = colid + sIx;
                        (cols[key] = cols[key] || []).push(v);
                        any = true;
                    }
                }
            }
        } else break;
    }
    return any ? cols : null;
}
// ---- dbrows (js5-2 arch 41): smart rowcount + row-major (validated 18394/18394) ----
function decodeDbrowLabel(buf: Buffer): string | null {
    const r = new R(buf);
    let best: { col: number, val: string } | null = null;
    while (r.p < r.len) {
        const op = r.u8();
        if (op === 0) break;
        else if (op === 3) {
            r.u8();   // totalCols
            for (;;) {
                const b = r.u8();
                if (b === 0xFF) break;
                const colid = b & 0x3F;
                const nsub = r.u8();
                const types: number[] = [];
                for (let i = 0; i < nsub; i++) { types.push(r.usmart()); }
                const rowcount = r.usmart();
                for (let row = 0; row < rowcount; row++) {
                    for (let s = 0; s < nsub; s++) {
                        if (types[s] === 0x24) {
                            const v = r.cstr();
                            if (row === 0 && (!best || colid + s < best.col)) { best = { col: colid + s, val: v }; }
                        } else r.i32();
                    }
                }
            }
        }
        else if (op === 4) r.usmart();
        else return null;
    }
    return best && best.val ? best.val.slice(0, 60) : null;
}

// ---- CS2 subtype id -> name (rsmv src/clientscript/definitions.ts `subtypes`) ----
const SUBTYPES: { [k: number]: string } = {
    0: "int", 1: "boolean", 2: "type_2", 3: "quest", 4: "questhelp", 5: "cursor",
    6: "seq", 7: "colour", 8: "loc_shape", 9: "component", 10: "idkit", 11: "midi",
    12: "npc_mode", 13: "namedobj", 14: "synth", 16: "area", 17: "stat", 18: "npc_stat",
    19: "writeinv", 20: "mesh", 21: "maparea", 22: "coordgrid", 23: "graphic",
    24: "chatphrase", 25: "fontmetrics", 26: "enum", 28: "jingle", 29: "chatcat",
    30: "loc", 31: "model", 32: "npc", 33: "obj", 34: "player_uid", 36: "string",
    37: "spotanim", 38: "npc_uid", 39: "inv", 40: "texture", 41: "category", 42: "char",
    43: "laser", 44: "bas", 50: "coordfine", 55: "mapsceneicon", 59: "mapelement",
    62: "hitmark", 64: "particle_effector", 66: "particle_emitter", 68: "unsigned_int",
    69: "skybox", 70: "skydecor", 71: "hash64", 72: "inputtype", 73: "struct", 74: "dbrow",
    97: "interface", 98: "toplevelinterface", 99: "overlayinterface", 100: "clientinterface",
    101: "movespeed", 102: "material", 103: "seqgroup", 108: "audiogroup",
    109: "audiomixbuss", 110: "long", 117: "pointlight", 118: "player_group",
    131: "achievement", 133: "stylesheet", 209: "var_reference",
};
// Types worth tagging in scripts (exclude int/boolean/string/long primitives = noise).
const MEANINGFUL_TYPES = new Set<number>([
    3, 6, 8, 9, 10, 13, 16, 17, 18, 20, 21, 22, 23, 24, 26, 28, 30, 31, 32, 33, 37,
    39, 40, 41, 43, 44, 55, 59, 62, 64, 66, 69, 70, 73, 74, 97, 98, 99, 100, 102,
    103, 108, 117, 118, 131, 133,
]);
function typeName(t: number): string { return SUBTYPES[t] ?? `type_${t}`; }

// ---- paramDefs (js5-2/11): the type byte (op1/101); default int/str consumed past ----
function decodeParamType(buf: Buffer): number {
    const r = new R(buf);
    let type = 0;   // no explicit type op => int (cache default)
    while (r.p < r.len) {
        const op = r.u8();
        if (op === 0) break;
        else if (op === 1 || op === 101) type = r.u8();
        else if (op === 2) r.i32();
        else if (op === 4) { }
        else if (op === 5) r.cstr();
        else if (op === 131 || op === 207 || op === 209) { }
        else break;
    }
    return type;
}

// ---- dbtables (js5-2/40): table schema -> Map<colId, typeId[]> (ground truth) ----
function decodeDbtable(buf: Buffer): Map<number, number[]> {
    const r = new R(buf);
    const cols = new Map<number, number[]>();
    const op = r.u8();
    if (op === 0) return cols;
    if (op === 2) r.i32();
    r.u8(); // column-count hint
    for (;;) {
        const oc = r.u8();
        if (oc === 0xFF) break;
        const cid = oc & 0x3F;
        if (op === 2) r.u8();          // per-column unkbyte (op-2 layout only)
        const n = r.u8();
        const types: number[] = [];
        for (let i = 0; i < n; i++) { types.push(r.usmart()); }
        if (op === 1) {
            if (oc & 0x80) {
                for (let ci = 0; ci < types.length; ci++) {
                    if (ci === 0) r.u8();
                    if (types[ci] === 0x24) r.cstr(); else r.i32();
                }
            }
        } else {                       // op === 2
            let hasdefault = 0;
            for (let ci = 0; ci < types.length; ci++) {
                if (ci === 0) hasdefault = r.u8();
                if (hasdefault & 0x02) {
                    if (ci === 0) r.u8();
                    if (types[ci] === 0x24) r.cstr(); else r.i32();
                    if (ci === 0) r.u8();
                }
            }
        }
        cols.set(cid, types);
    }
    return cols;
}

// morphs blocks parsed by rsmv: unk1 = (varbit << 16) | varp, 0xFFFF = none.
function morphVars(m: any): { vb: number, vp: number, kids: number[] } | null {
    if (!m || typeof m.unk1 != "number") { return null; }
    const vb = (m.unk1 >>> 16) & 0xFFFF, vp = m.unk1 & 0xFFFF;
    const kids: number[] = [];
    for (const key of ["unk2", "unk3", "unk4"]) {
        const v = (m as any)[key];
        if (Array.isArray(v)) { for (const x of v) { if (typeof x == "number") kids.push(x); } }
        else if (typeof v == "number") { kids.push(v); }
    }
    return { vb: vb === 0xFFFF ? -1 : vb, vp: vp === 0xFFFF ? -1 : vp, kids: kids.filter(k => k >= 0 && k < 0x7FFFFF) };
}

async function buildTables(engine: EngineCache, notes: string[],
                           progress: (stage: string, done: number, total: number) => void) {
    const names: NameTables = { varbit: new Map(), varp: new Map(), varc: new Map() };
    const cast: CastTables = {
        obj: new Map(), npc: new Map(), loc: new Map(), quest: new Map(),
        stat: new Map(), achievement: new Map(), dbrow: new Map(), struct: new Map(),
        category: new Map(),
    };
    const enumTables = new Map<number, Map<number, string>>();

    // 1. quest trackers (rename priority 1) + quest names
    const questArch = await engine.getArchiveById(cacheMajors.config, 35);
    let questVars = 0;
    for (const sub of questArch) {
        const q = decodeQuest(sub.buffer);
        if (!q.name) { continue; }
        cast.quest.set(sub.fileid, q.name);
        const s = slug(q.name);
        for (const vp of q.varps) { if (!names.varp.has(vp)) { names.varp.set(vp, `q_${s}_progress`); questVars++; } }
        for (const vb of q.varbits) { if (!names.varbit.has(vb)) { names.varbit.set(vb, `q_${s}_progress`); questVars++; } }
    }
    notes.push(`quest tracker vars: ${questVars}, quest names: ${cast.quest.size}`);

    // 2/3. npc then loc: names + morph vars (rename priority 2/3)
    for (const { major, kind, castKey } of [
        { major: cacheMajors.npcs, kind: "npcmorph", castKey: "npc" },
        { major: cacheMajors.objects, kind: "locmorph", castKey: "loc" },
    ]) {
        const cfgs = new Map<number, any>();
        for await (const { id, file } of iterateConfigFiles(engine, major)) {
            try {
                cfgs.set(id, major === cacheMajors.npcs ? parse.npc.read(file, engine.rawsource) : parse.object.read(file, engine.rawsource));
            } catch (e) { }
        }
        for (const [id, cfg] of cfgs) {
            if (cfg.name && cfg.name !== "null") { cast[castKey].set(id, cfg.name); }
        }
        const tied = { varbit: new Map<number, Map<string, number>>(), varp: new Map<number, Map<string, number>>() };
        for (const [id, cfg] of cfgs) {
            for (const key of ["morphs_1", "morphs_2"]) {
                const mv = morphVars(cfg[key]);
                if (!mv) { continue; }
                const nms: string[] = [];
                if (cfg.name) { nms.push(cfg.name); }
                for (const kid of mv.kids) { const knm = cfgs.get(kid)?.name; if (knm) { nms.push(knm); } }
                if (!nms.length) { continue; }
                for (const [vk, vid] of [["varbit", mv.vb], ["varp", mv.vp]] as const) {
                    if (vid < 0) { continue; }
                    let cnt = tied[vk].get(vid);
                    if (!cnt) { cnt = new Map(); tied[vk].set(vid, cnt); }
                    for (const n of nms) { cnt.set(n, (cnt.get(n) ?? 0) + 1); }
                }
            }
        }
        let added = 0;
        for (const vk of ["varbit", "varp"] as const) {
            for (const [vid, cnt] of tied[vk]) {
                if (names[vk].has(vid)) { continue; }
                const top = [...cnt.entries()].sort((a, b) => b[1] - a[1])[0][0];
                names[vk].set(vid, `${kind}_${slug(top)}${cnt.size > 1 ? "_etc" : ""}`);
                added++;
            }
        }
        notes.push(`${kind} vars: ${added}, ${castKey} names: ${cast[castKey].size}`);
        if (major === cacheMajors.npcs) {
            const ranger = cfgs.get(32560)?.name;
            if (ranger !== "Ranger") { notes.push(`WARN npc 32560 name = ${JSON.stringify(ranger)} (expected Ranger)`); }
        }
    }

    // 4. param types FIRST (moved ahead of items/structs so var_reference-typed params
    //    can be captured while their owners are decoded), then items.
    const paramtypes = new Map<number, number>();
    for (const sub of await engine.getArchiveById(cacheMajors.config, 11)) {
        try { paramtypes.set(sub.fileid, decodeParamType(sub.buffer)); } catch (e) { }
    }
    // var-reference params carry type byte 0x80 (proven on param 8994, the collection-log
    // found-varbit param: raw 04 65 80 d1 02 ffffffff 00 = op101 type 0x80 + op209 flag;
    // its values pack (1 << 24) | varbitId).
    const refKeys = new Set<number>();
    for (const [k, t] of paramtypes) { if (t === 128) { refKeys.add(k); } }
    notes.push(`param types: ${paramtypes.size}, var_reference params: ${refKeys.size}`);
    // varbit -> "owner" name for vars referenced from item/struct params (packing
    // (1 << 24) | varbitId, proven against the collection-log param 8994 family).
    const refVarbits = new Map<number, string>();
    progress("xref", 1, 6);
    for await (const { id, file } of iterateConfigFiles(engine, cacheMajors.items)) {
        try {
            const d = decodeItemName(file, refKeys);
            if (d.name) { cast.obj.set(id, d.name); }
            if (d.name) {
                for (const [, v] of d.refs) {
                    if ((v >>> 24) === 1) {
                        const vb = v & 0xFFFFFF;
                        if (!refVarbits.has(vb)) { refVarbits.set(vb, d.name); }
                    }
                }
            }
        } catch (e) { }
    }
    notes.push(`item names: ${cast.obj.size}`);

    // 5. achievements (+ requirement varbits for the low-priority ach_* rename pass)
    progress("xref", 2, 6);
    let achFiles = 0;
    const achReqVbs = new Map<number, string>();   // varbit -> achievement name (first wins)
    for await (const { id, file } of iterateConfigFiles(engine, cacheMajors.achievements)) {
        achFiles++;
        try {
            const a = decodeAchievementName(file);
            if (a.name) {
                cast.achievement.set(id, a.name);
                for (const vb of a.reqVbs) {
                    if (vb > 0 && !achReqVbs.has(vb)) { achReqVbs.set(vb, a.name); }
                }
            }
        } catch (e) { }
    }
    notes.push(`achievement names: ${cast.achievement.size} of ${achFiles} files, req varbits: ${achReqVbs.size}`);

    // 6. structs (label + var_reference params + selected name-params kept for renames)
    progress("xref", 3, 6);
    const structDump: { id: number, ints: [number, number][], strs: [number, string][] }[] = [];
    const structStrs = new Map<number, Map<number, string>>();   // struct id -> string params
    for await (const { id, file } of iterateConfigFiles(engine, cacheMajors.structs)) {
        try {
            const d = decodeStructLabel(file, refKeys);
            if (d.label) { cast.struct.set(id, d.label); }
            if (d.strs.size) { structStrs.set(id, d.strs); }
            if (d.strs.size || d.ints.size) {
                structDump.push({ id, ints: Array.from(d.ints), strs: Array.from(d.strs) });
            }
            const ownerName = d.strs.get(2794) || d.strs.get(6410) || d.strs.get(4849) || d.label;
            if (ownerName) {
                for (const [, v] of d.refs) {
                    if ((v >>> 24) === 1) {
                        const vb = v & 0xFFFFFF;
                        if (!refVarbits.has(vb)) { refVarbits.set(vb, ownerName); }
                    }
                }
            }
        } catch (e) { }
    }
    notes.push(`struct labels: ${cast.struct.size}, ref varbits: ${refVarbits.size}`);

    // 7. enums: string-valued tables for enum_getvalue + official names for the
    //    key-typed id-spaces the game itself declares (keyType 17 = stat, 41 =
    //    category; valType 36 = string; most-entries enum wins on key conflicts).
    progress("xref", 4, 6);
    const enumDump: { id: number, keyType: number, valType: number, entries: [number, any][] }[] = [];
    const keyed: { [k: string]: Map<number, string>[] } = { stat: [], category: [] };
    const KEYTYPE: { [n: number]: string } = { 17: "stat", 41: "category" };
    for await (const { id, file } of iterateConfigFiles(engine, cacheMajors.enums)) {
        try {
            const e = decodeEnum(file);
            if (!e || !e.entries.size) { continue; }
            enumDump.push({ id, keyType: e.keyType, valType: e.valType, entries: Array.from(e.entries) });
            if (e.valType === 36) {
                enumTables.set(id, e.entries);
                const dest = KEYTYPE[e.keyType];
                if (dest) { keyed[dest].push(e.entries); }
            }
        } catch (e) { }
    }
    for (const dest of Object.keys(keyed)) {
        keyed[dest].sort((a, b) => b.size - a.size);
        for (const t of keyed[dest]) {
            for (const [k, v] of t) { if (!cast[dest].has(k)) { cast[dest].set(k, v); } }
        }
    }
    notes.push(`string enums: ${enumTables.size}, stat names: ${cast.stat.size}, category names: ${cast.category.size}`);

    const dbrowDump: { id: number, cols: { [col: number]: (number | string)[] } }[] = [];
    // 8. dbrows
    progress("xref", 5, 6);
    const dbArch = await engine.getArchiveById(cacheMajors.config, 41);
    for (const sub of dbArch) {
        try {
            const nm = decodeDbrowLabel(sub.buffer);
            if (nm) { cast.dbrow.set(sub.fileid, nm); }
            const full = decodeDbrowFull(sub.buffer);
            if (full) { dbrowDump.push({ id: sub.fileid, cols: full }); }
        } catch (e) { }
    }
    notes.push(`dbrow labels: ${cast.dbrow.size}`);

    // 9. dbtable schemas (js5-2/40) for the type-tag rules (param types moved to step 4)
    const dbschema = new Map<number, Map<number, number[]>>();
    for (const sub of await engine.getArchiveById(cacheMajors.config, 40)) {
        try { dbschema.set(sub.fileid, decodeDbtable(sub.buffer)); } catch (e) { }
    }
    notes.push(`dbtables: ${dbschema.size}`);

    // 10. interface components (js5-3) for the packed if-ref annotation rule: IF_* ops
    //     address components as (group << 16) | comp int literals. Labels are the comp's
    //     dev name (header type byte & 0x80) or its static text (type 4); most comps have
    //     neither and annotate as bare group:comp.
    const ifaceCounts = new Map<number, number>();          // group -> component count
    const ifaceComps = new Map<number, string>();           // (group<<16)|comp -> label
    let ifaceGroups = 0;
    for (const e of await engine.rawsource.getCacheIndex(cacheMajors.interfaces)) {
        if (!e) { continue; }
        try {
            const subs = await engine.getArchiveById(cacheMajors.interfaces, e.minor);
            let maxid = -1;
            for (const sub of subs) {
                if (sub.fileid > maxid) { maxid = sub.fileid; }
                try {
                    const c = parse.interfaces.read(sub.buffer, engine.rawsource) as any;
                    const label = (c.name && String(c.name).trim()) ||
                                  (c.textdata && c.textdata.text && String(c.textdata.text).trim()) || null;
                    if (label) { ifaceComps.set((e.minor << 16) | sub.fileid, label); }
                } catch (err) { }
            }
            if (maxid >= 0) { ifaceCounts.set(e.minor, maxid + 1); ifaceGroups++; }
        } catch (err) { }
    }
    notes.push(`interface groups: ${ifaceGroups}, labeled comps: ${ifaceComps.size}`);
    progress("xref", 6, 6);

    return { names, cast, enumTables, paramtypes, dbschema, achReqVbs, refVarbits, structStrs, ifaceCounts, ifaceComps, dbrowDump, enumDump, structDump };
}

// ---- CS2 cross-reference tables (ver 3) --------------------------------------------------
// The pairing between many var ids and what they track exists ONLY inside specific
// clientscripts' switch statements. Those scripts are decompiled first and parsed
// mechanically; every rename below is a literal read of that code plus cache names:
//   5511  slayer kill log        [name string in-script] -> kills vb / prestige vb
//   6732  boss kill log          struct -> [int2,int4]=[kills,prestige] (+ optional
//                                [int3,int5] extension counters the script ADDS on top);
//                                boss name = struct param 1348
//   14503 collection complete    struct -> tri-state vb; collection name = struct 6410
//   14500 collection item found  item -> vb; item names from js5-19
//   7090  toolbelt stored flags  item -> vb (+ the tier vbs it compares against enums)
//   17845 daily shop caps        struct -> bought-count vb; name = struct 4849
//   15411 unlockables            id -> vb (name = item name, else struct param 2794)
//   11472 death reclaim prices   slot -> varclient (server-pushed long prices)
async function renderWithRetry(engine: EngineCache, id: number): Promise<string | null> {
    try {
        const buf = await engine.rawsource.getFileById(CLIENTSCRIPT_MAJOR, id);
        try { return await renderClientScript(engine.rawsource, buf, id); }
        catch (e) { return await renderClientScript(engine.rawsource, buf, id); }
    } catch (e) { return null; }
}
// grouped-case switch bodies "case a: case b: { return varbitplayer_N; }". The label
// group must NOT carry a leading \s* -- "(?:\s*case...\s*)+" backtracks catastrophically
// (the whitespace between labels splits two ways per label) and OOM'd V8 on 15411.
function parseCaseVb(text: string): Map<number, number> {
    const out = new Map<number, number>();
    for (const m of text.matchAll(/((?:case \d+:\s*)+)\{\s*return (?:script5566\()?varbitplayer_(\d+)[,;)]/g)) {
        for (const c of m[1].matchAll(/case (\d+):/g)) { out.set(parseInt(c[1], 10), parseInt(m[2], 10)); }
    }
    return out;
}
function slug36(nm: string) { return slug(nm); }
async function buildScriptNames(engine: EngineCache, names: NameTables, cast: CastTables,
                                structStrs: Map<number, Map<number, string>>, notes: string[]) {
    const setVb = (vb: number, nm: string) => {
        if (vb > 0 && nm && !names.varbit.has(vb)) { names.varbit.set(vb, nm); return 1; }
        return 0;
    };
    let n = 0;
    // slayer kill log (5511): name strings live in the script itself
    const t5511 = await renderWithRetry(engine, 5511);
    if (t5511) {
        for (const m of t5511.matchAll(/string0 = "([^"]+)";\s*int2 = varbitplayer_(\d+);\s*int3 = varbitplayer_(\d+);/g)) {
            n += setVb(parseInt(m[2], 10), `slayerkc_${slug36(m[1])}`);
            n += setVb(parseInt(m[3], 10), `slayerprestige_${slug36(m[1])}`);
        }
    }
    // boss kill log (6732): per boss struct, [int2, int4] = [kills vb, prestige vb] and
    // an optional [int3, int5] second line whose counters the script adds on top of the
    // first pair (extension words once a count outgrows its varbit).
    const t6732 = await renderWithRetry(engine, 6732);
    if (t6732) {
        for (const m of t6732.matchAll(/case (\d+):\s*\{\s*\[int2, int4\] = \[varbitplayer_(\d+), varbitplayer_(\d+)\];(?:\s*\[int3, int5\] = \[varbitplayer_(\d+), varbitplayer_(\d+)\];)?/g)) {
            const nm = structStrs.get(parseInt(m[1], 10))?.get(1348);
            if (!nm) { continue; }
            const s = slug36(nm);
            n += setVb(parseInt(m[2], 10), `bosskc_${s}`);
            n += setVb(parseInt(m[3], 10), `bossprestige_${s}`);
            if (m[4]) { n += setVb(parseInt(m[4], 10), `bosskc2_${s}`); }
            if (m[5]) { n += setVb(parseInt(m[5], 10), `bossprestige2_${s}`); }
        }
    }
    // collection complete (14503): first varbit compared inside each case block
    const t14503 = await renderWithRetry(engine, 14503);
    if (t14503) {
        for (const m of t14503.matchAll(/case (\d+): \{\s*if \(\(varbitplayer_(\d+)/g)) {
            const strs = structStrs.get(parseInt(m[1], 10));
            const nm = strs?.get(6410) || cast.struct.get(parseInt(m[1], 10));
            if (nm) { n += setVb(parseInt(m[2], 10), `clogdone_${slug36(nm)}`); }
        }
    }
    // collection item found (14500)
    const t14500 = await renderWithRetry(engine, 14500);
    if (t14500) {
        for (const [item, vb] of parseCaseVb(t14500)) {
            const nm = cast.obj.get(item);
            if (nm) { n += setVb(vb, `clogitem_${slug36(nm)}`); }
        }
    }
    // toolbelt (7090): per-item flags + the tier counters it compares against enums
    const t7090 = await renderWithRetry(engine, 7090);
    if (t7090) {
        for (const m of t7090.matchAll(/case (\d+): \{\s*if \(\(\(?varbitplayer_(\d+)/g)) {
            const nm = cast.obj.get(parseInt(m[1], 10));
            if (nm) { n += setVb(parseInt(m[2], 10), `toolbelt_${slug36(nm)}`); }
        }
        // the tier counters 7090 compares against its ladder enums (panel_toolbelt.js
        // carries the same ids; ladders proven in-game)
        n += setVb(18522, "toolbelt_hatchet_tier");     // enum 6397 index (7090 cat 35)
        n += setVb(18521, "toolbelt_pickaxe_tier");     // enum 2433 index (7090 cat 67)
        n += setVb(45999, "toolbelt_mattock_tier");     // enum 12936 progressive (cat 4604)
        n += setVb(3008, "toolbelt_dg_pickaxe_tier");   // Daemonheim ladder (items 16295+2i)
        n += setVb(3009, "toolbelt_dg_hatchet_tier");   // Daemonheim ladder (items 16361+2i)
        n += setVb(3002, "toolbelt_machete_stored");
        n += setVb(4935, "toolbelt_machete_tier");      // 975/6313/6315/6317 ladder
        n += setVb(28225, "toolbelt_explosive_shaker"); // items 34963/34964
        n += setVb(46004, "toolbelt_wingsuit_tier");    // items 50120-50122
    }
    // shop caps (17845): assignment form "case S: { varbitplayer_N = int1; }"
    const t17845 = await renderWithRetry(engine, 17845);
    if (t17845) {
        for (const m of t17845.matchAll(/((?:case \d+:\s*)+)\{\s*varbitplayer_(\d+) = /g)) {
            const first = /case (\d+):/.exec(m[1]);
            const strs = first && structStrs.get(parseInt(first[1], 10));
            const nm = strs && strs.get(4849);
            if (nm) { n += setVb(parseInt(m[2], 10), `shopcap_${slug36(nm)}`); }
        }
    }
    // unlockables (15411): name via item, else struct param 2794
    const t15411 = await renderWithRetry(engine, 15411);
    if (t15411) {
        for (const [id, vb] of parseCaseVb(t15411)) {
            const nm = cast.obj.get(id) || structStrs.get(id)?.get(2794);
            if (nm) { n += setVb(vb, `unlock_${slug36(nm)}`); }
        }
    }
    // death reclaim prices (11472): slot -> varclient long (server-pushed prices)
    const t11472 = await renderWithRetry(engine, 11472);
    if (t11472) {
        for (const m of t11472.matchAll(/case (\d+):\s*\{\s*return varclient_(\d+);/g)) {
            const vc = parseInt(m[2], 10);
            if (!names.varc.has(vc)) { names.varc.set(vc, `death_price_slot${m[1]}`); }
        }
    }
    // curated varcs, each verified against its live decompiled usage:
    //   1971 clamped to the CAM2 zoom bounds (1860..7600) and fed through the zoom scripts
    //   5114/5115 written together as the CAM2_SETPOSITIONENTITY_PLAYER angle pair
    //   2251 CC_SETTEXT'd into the server-sent tooltip overlay (interface 1177)
    //   7157 script19219 stores its title arg here then IF_SETTEXTs the encounter header
    //   4485 compared against boss struct ids (the Beasts selection)
    //   4486/4487 kill counters rendered with the 60000 -> "Lots!" cap
    //   4488/4489 fastest-kill times fed to the duration formatter (script15908)
    const CURATED_VARCS: Array<[number, string]> = [
        [1971, "camera_zoom"], [5114, "camera_pitch"], [5115, "camera_yaw"],
        [2251, "tooltip_text"], [7157, "boss_encounter_name"],
        [4485, "boss_select_struct"], [4486, "boss_kills"], [4487, "boss_kills2"],
        [4488, "boss_best_time"], [4489, "boss_best_time2"],
    ];
    for (const [vc, nm] of CURATED_VARCS) { if (!names.varc.has(vc)) { names.varc.set(vc, nm); } }
    notes.push(`script-table varbit names: ${n}, varc names: ${names.varc.size}`);
}

function applyNames(text: string, names: NameTables) {
    const PREFIX: { [k: string]: string } = { varbitplayer: "vb", varplayer: "vp", varclient: "vc" };
    return text.replace(/\b(varbitplayer|varplayer|varclient)_(\d+)\b/g, (m, kind, ids) => {
        const id = parseInt(ids, 10);
        const nm = (kind === "varbitplayer" ? names.varbit : kind === "varplayer" ? names.varp : names.varc).get(id);
        if (!nm) { return m; }
        return `${PREFIX[kind]}${id}_${nm}`;
    });
}

// ---- annotation pass (ports the verified offline cs2_annotate.py rules 1-6) ----
const SWITCH_TYPES = ["obj", "npc", "loc", "quest", "stat", "achievement", "dbrow", "struct"];
const TYPE_ALT = SWITCH_TYPES.join("|");
const CAST_RE = /(?<![\w.\-])(\d+) as (obj|npc|loc|quest|stat|achievement|dbrow|struct|category)\b/g;
const ENUM_RE = /enum_getvalue\(\s*-?\d+\s*,\s*-?\d+\s*,\s*(\d+)(?:\s+as\s+cs2enum)?\s*,\s*(-?\d+)\s*\)/g;
const FUNC_RE = /\bfunction\s+\w+\(([^)]*)\)/g;
const SWITCH_RE = new RegExp(String.raw`\bswitch\s*\(\s*([A-Za-z_]\w*)\s*\)\s*\{`, "g");
const SWITCH_ANY_RE = /\bswitch\s*\(/g;
const CASE_RE = /^[ \t]*case\s+(\d+)(:)/gm;
const VAR_TYPED_CMP_RE = new RegExp(String.raw`\b([A-Za-z_]\w*)\s*(?:==|!=|=)\s*-?\d+\s+as\s+(` + TYPE_ALT + String.raw`)\b`, "g");
const VAR_TYPED_CAST_RE = new RegExp(String.raw`\b([A-Za-z_]\w*)\s+as\s+(` + TYPE_ALT + String.raw`)\b`, "g");
const DB_CALL_RE = /\b(dbrow_getfield|DB_GETFIELDCOUNT)\(/g;
const PARAM_GET_2ARG = ["struct_getparam", "item_getparam", "npc_getparam", "quest_getparam", "lc_getparam", "mec_getparam", "loc_getparam"];
const PARAM_CALL_RE = new RegExp(String.raw`\b(` + [...PARAM_GET_2ARG, "cc_setparam", "cc_getparam"].join("|") + String.raw`)\(`, "g");
const OBJ_SETTER_ARG: { [k: string]: number } = { IF_SETOBJECT: 1, IF_SETOBJECT_NONUM: 1, CC_SETOBJECT: 0, CC_SETOBJECT_NONUM: 0 };
const OBJ_CALL_RE = new RegExp(String.raw`\b(` + Object.keys(OBJ_SETTER_ARG).join("|") + String.raw`)\(`, "g");
const INT_LIT_RE = /^(\s*)(\d+)\s*$/;

function cleanComment(text: string | undefined | null, maxlen: number): string | null {
    if (text == null) { return null; }
    let s = String(text).replace(/[\r\n]/g, " ");
    if (s.length > maxlen) { s = s.slice(0, maxlen); }
    s = s.split("*/").join("*\\/");
    return s.trim() ? s : null;
}

function maskStrings(text: string): string {
    const out = text.split("");
    let inStr = false;
    for (let i = 0; i < text.length; i++) {
        const c = text[i];
        if (inStr) {
            if (c === "\\" && i + 1 < text.length) { out[i] = " "; out[i + 1] = " "; i++; continue; }
            if (c === '"') { inStr = false; }
            else if (c !== "\n") { out[i] = " "; }
        } else if (c === '"') { inStr = true; }
    }
    return out.join("");
}

function braceBlocks(masked: string): Map<number, number> {
    const blocks = new Map<number, number>();
    const stack: number[] = [];
    for (let i = 0; i < masked.length; i++) {
        const c = masked[i];
        if (c === "{") { stack.push(i); }
        else if (c === "}") { const o = stack.pop(); if (o !== undefined) { blocks.set(o, i); } }
    }
    return blocks;
}

// (closeIdx, [[start,end], ...]) for a call's top-level comma-separated arg spans.
function callArgs(masked: string, openIdx: number): [number, Array<[number, number]>] | null {
    let depth = 0;
    const args: Array<[number, number]> = [];
    let argStart = openIdx + 1;
    for (let i = openIdx; i < masked.length; i++) {
        const c = masked[i];
        if (c === "(") { depth++; }
        else if (c === ")") {
            depth--;
            if (depth === 0) { if (i > argStart || args.length) { args.push([argStart, i]); } return [i, args]; }
        } else if (c === "," && depth === 1) { args.push([argStart, i]); argStart = i + 1; }
    }
    return null;
}

// If an arg span is a bare non-negative int literal: [valueStr, insertOffset].
function argInt(masked: string, span: [number, number]): [string, number] | null {
    const m = INT_LIT_RE.exec(masked.slice(span[0], span[1]));
    if (!m) { return null; }
    return [m[2], span[0] + m[1].length + m[2].length];
}

function dbComment(packed: number, dbschema: Map<number, Map<number, number[]>>): string | null {
    const table = packed >>> 12, col = (packed >>> 4) & 0xFF, sub = packed & 0xF;
    const types = dbschema.get(table)?.get(col);
    if (!types) { return null; }
    if (sub === 0) { return `db ${table}.${col} = (${types.map(typeName).join(",")})`; }
    if (sub - 1 >= types.length) { return null; }
    return `db ${table}.${col}[${sub}] = ${typeName(types[sub - 1])}`;
}

// Rule 2: annotate case labels of switches whose bareword scrutinee is typed by a cast.
function switchCaseInsertions(masked: string, blocks: Map<number, number>,
                              cast: CastTables, counts: { [k: string]: number },
                              already: (at: number) => boolean): Array<[number, string]> {
    const ins: Array<[number, string]> = [];
    const funcSpans: Array<[number, number, Map<string, string>, Set<string>]> = [];
    for (const m of masked.matchAll(FUNC_RE)) {
        const open = masked.indexOf("{", m.index! + m[0].length);
        if (open === -1 || !blocks.has(open)) { continue; }
        const vt = new Map<string, string>();
        for (const part of m[1].split(",")) {
            const p = part.trim();
            const ci = p.indexOf(":");
            if (ci >= 0) {
                const pname = p.slice(0, ci).trim();
                const ptype = p.slice(ci + 1).trim();
                if (SWITCH_TYPES.includes(ptype)) { vt.set(pname, ptype); }
            }
        }
        funcSpans.push([open, blocks.get(open)!, vt, new Set<string>()]);
    }
    for (const rx of [VAR_TYPED_CMP_RE, VAR_TYPED_CAST_RE]) {
        for (const m of masked.matchAll(rx)) {
            const varn = m[1], typ = m[2];
            for (const fs of funcSpans) {
                if (fs[0] < m.index! && m.index! < fs[1]) {
                    const vt = fs[2], conflict = fs[3];
                    if (conflict.has(varn)) { break; }
                    if (vt.has(varn) && vt.get(varn) !== typ) { vt.delete(varn); conflict.add(varn); }
                    else if (!vt.has(varn)) { vt.set(varn, typ); }
                    break;
                }
            }
        }
    }
    const funcs = funcSpans.filter(fs => fs[2].size > 0);
    if (!funcs.length) { return ins; }
    const typedMap = new Map<number, string>();
    for (const m of masked.matchAll(SWITCH_RE)) {
        const varn = m[1];
        const ob = m.index! + m[0].length - 1;   // the '{'
        if (!blocks.has(ob)) { continue; }
        for (const [fo, fc, vt] of funcs) {
            if (fo < m.index! && m.index! < fc && vt.has(varn)) { typedMap.set(ob, vt.get(varn)!); break; }
        }
    }
    if (!typedMap.size) { return ins; }
    // ALL switch blocks (incl. switch(f(x))) so cases in an untyped inner switch are not stolen.
    const allSwitches: Array<[number, number]> = [];
    for (const m of masked.matchAll(SWITCH_ANY_RE)) {
        const ca = callArgs(masked, m.index! + m[0].length - 1);
        if (!ca) { continue; }
        const close = ca[0];
        const bo = masked.indexOf("{", close);
        if (bo !== -1 && blocks.has(bo) && masked.slice(close + 1, bo).trim() === "") { allSwitches.push([bo, blocks.get(bo)!]); }
    }
    for (const cm of masked.matchAll(CASE_RE)) {
        const pos = cm.index!;
        let owner: number | null = null;
        for (const [ob, cb] of allSwitches) {
            if (ob < pos && pos < cb && (owner === null || ob > owner)) { owner = ob; }
        }
        if (owner === null || !typedMap.has(owner)) { continue; }
        const typ = typedMap.get(owner)!;
        const name = (cast as any)[typ].get(parseInt(cm[1], 10));
        const cc = cleanComment(name, 60);
        if (cc == null) { continue; }
        const at = cm.index! + cm[0].length;      // right after the ':'
        if (already(at)) { continue; }
        ins.push([at, ` /* ${cc} */`]);
        counts["case_" + typ] = (counts["case_" + typ] || 0) + 1;
    }
    return ins;
}

const IF_CALL_RE = /\bIF_[A-Z0-9_]+\(/g;
// DB query comparison operators, read out of the client's operator registry
// (DAT_140c6c6e0; each operator object carries its id at vtable+0x30 and its name at
// +0x28). The enum is SHARED by DBQUERY_FIND_VALUE and DBQUERY_FILTER_FIELD_OP -- both
// resolve through FUN_1400a1920 -- but NOT by DBQUERY_FIND_STRING, which takes a
// separate string path, so that one is deliberately not annotated. There is no "not
// equal" operator: scripts build it as DBQUERY_NOT(... Equal To ...).
const DBQUERY_OPS: { [k: string]: string } = {
    "1": "Less Than", "2": "Less Than or Equal To", "3": "Equal To",
    "4": "Greater Than or Equal To", "5": "Greater Than",
};
const DBQUERY_OP_CALL_RE = /\b(DBQUERY_FIND_VALUE|DBQUERY_FILTER_FIELD_OP)\(/g;

function annotate(text: string, cast: CastTables, enumTables: Map<number, Map<number, string>>,
                  paramtypes: Map<number, number>, dbschema: Map<number, Map<number, number[]>>,
                  ifaceCounts: Map<number, number>, ifaceComps: Map<number, string>,
                  counts: { [k: string]: number }): string {
    const masked = maskStrings(text);
    const already = (at: number) => text.slice(at, at + 3) === " /*";
    const ins: Array<[number, string]> = [];
    // Rule 1: typed cast literals
    for (const m of masked.matchAll(CAST_RE)) {
        const cc = cleanComment((cast as any)[m[2]].get(parseInt(m[1], 10)), 60);
        if (cc == null) { continue; }
        ins.push([m.index! + m[0].length, ` /* ${cc} */`]);
        counts["cast_" + m[2]] = (counts["cast_" + m[2]] || 0) + 1;
    }
    // Rule 3: enum_getvalue string values
    for (const m of masked.matchAll(ENUM_RE)) {
        const e = enumTables.get(parseInt(m[1], 10));
        const cc = cleanComment(e?.get(parseInt(m[2], 10)), 50);
        if (cc == null) { continue; }
        ins.push([m.index! + m[0].length, ` /* = ${cc} */`]);
        counts["enum_value"] = (counts["enum_value"] || 0) + 1;
    }
    // Rule 2: typed switch cases
    const blocks = braceBlocks(masked);
    for (const it of switchCaseInsertions(masked, blocks, cast, counts, already)) { ins.push(it); }
    // Rule 4: db column-type tags
    for (const m of masked.matchAll(DB_CALL_RE)) {
        const ca = callArgs(masked, m.index! + m[0].length - 1);
        if (!ca || ca[1].length < 2) { continue; }
        const got = argInt(masked, ca[1][1]);
        if (!got) { continue; }
        const cc = dbComment(parseInt(got[0], 10), dbschema);
        if (cc == null || already(got[1])) { continue; }
        ins.push([got[1], ` /* ${cc} */`]);
        counts["db_col"] = (counts["db_col"] || 0) + 1;
    }
    // Rule 5: param type tags
    for (const m of masked.matchAll(PARAM_CALL_RE)) {
        const name = m[1];
        const ca = callArgs(masked, m.index! + m[0].length - 1);
        if (!ca || !ca[1].length) { continue; }
        const span = (name === "cc_setparam" || name === "cc_getparam") ? ca[1][0] : ca[1][ca[1].length - 1];
        const got = argInt(masked, span);
        if (!got) { continue; }
        const t = paramtypes.get(parseInt(got[0], 10));
        if (t === undefined || !MEANINGFUL_TYPES.has(t)) { continue; }
        if (already(got[1])) { continue; }
        ins.push([got[1], ` /* ${typeName(t)} */`]);
        counts["param_type"] = (counts["param_type"] || 0) + 1;
    }
    // Rule 6: literal obj ids in object setters
    for (const m of masked.matchAll(OBJ_CALL_RE)) {
        const ca = callArgs(masked, m.index! + m[0].length - 1);
        const idx = OBJ_SETTER_ARG[m[1]];
        if (!ca || ca[1].length <= idx) { continue; }
        const got = argInt(masked, ca[1][idx]);
        if (!got) { continue; }
        const cc = cleanComment(cast.obj.get(parseInt(got[0], 10)), 60);
        if (cc == null || already(got[1])) { continue; }
        ins.push([got[1], ` /* ${cc} */`]);
        counts["objset"] = (counts["objset"] || 0) + 1;
    }
    // Rule 7: packed interface refs -- the LAST bare int arg of an IF_* call addresses a
    // component as (group << 16) | comp. Only the last arg is tested (colour/value args
    // ride earlier positions) and only when the group exists and comp < its file count.
    for (const m of masked.matchAll(IF_CALL_RE)) {
        const ca = callArgs(masked, m.index! + m[0].length - 1);
        if (!ca || !ca[1].length) { continue; }
        const got = argInt(masked, ca[1][ca[1].length - 1]);
        if (!got) { continue; }
        const packed = parseInt(got[0], 10);
        if (packed < 0x10000) { continue; }
        const group = packed >>> 16, comp = packed & 0xFFFF;
        const cnt = ifaceCounts.get(group);
        if (cnt === undefined || comp >= cnt) { continue; }
        if (already(got[1])) { continue; }
        const label = cleanComment(ifaceComps.get(packed), 40);
        ins.push([got[1], ` /* if ${group}:${comp}${label ? ` '${label}'` : ""} */`]);
        counts["ifcomp"] = (counts["ifcomp"] || 0) + 1;
    }
    // Rule 8: the operator argument (3rd) of the DB query ops renders as a name.
    for (const m of masked.matchAll(DBQUERY_OP_CALL_RE)) {
        const ca = callArgs(masked, m.index! + m[0].length - 1);
        if (!ca || ca[1].length < 3) { continue; }
        const got = argInt(masked, ca[1][2]);
        if (!got) { continue; }
        const nm = DBQUERY_OPS[got[0]];
        if (!nm || already(got[1])) { continue; }
        ins.push([got[1], ` /* ${nm} */`]);
        counts["dbquery_op"] = (counts["dbquery_op"] || 0) + 1;
    }
    if (!ins.length) { return text; }
    ins.sort((a, b) => b[0] - a[0]);
    let out = text;
    for (const [at, comment] of ins) { out = out.slice(0, at) + comment + out.slice(at); }
    return out;
}

(async () => {
    const outdir = process.argv[2] || path.join(process.env.USERPROFILE || ".", "RuneToolsX", "cs2");
    const scriptsdir = path.join(outdir, "scripts");
    fs.mkdirSync(scriptsdir, { recursive: true });
    const startedAt = new Date().toISOString();
    const t0 = Date.now();
    const notes: string[] = [];
    const writeProgress = (stage: string, done: number, total: number) => {
        const tmp = path.join(outdir, "progress.json.tmp");
        fs.writeFileSync(tmp, JSON.stringify({ stage, done, total, startedAt }));
        fs.renameSync(tmp, path.join(outdir, "progress.json"));
    };

    writeProgress("init", 0, 0);
    const engine = await EngineCache.create(new GameCacheLoader(CACHE, false));
    const buildnr = engine.getBuildNr();

    writeProgress("names", 0, 0);
    const { names, cast, enumTables, paramtypes, dbschema, achReqVbs, refVarbits, structStrs, dbrowDump, enumDump, structDump,
            ifaceCounts, ifaceComps } = await buildTables(engine, notes, writeProgress);

    // script-table renames (fills gaps only, so quest/morph names keep priority)...
    writeProgress("scriptnames", 0, 0);
    await buildScriptNames(engine, names, cast, structStrs, notes);
    // ...then the two low-confidence-owner passes fill whatever is still unnamed:
    // ref_<owner> = a config's var_reference param points at the varbit; ach_<name> =
    // an achievement declares the varbit among its requirements.
    let refN = 0, achN = 0;
    for (const [vb, owner] of refVarbits) {
        if (!names.varbit.has(vb)) { names.varbit.set(vb, `ref_${slug(owner)}`); refN++; }
    }
    for (const [vb, nm] of achReqVbs) {
        if (!names.varbit.has(vb)) { names.varbit.set(vb, `ach_${slug(nm)}`); achN++; }
    }
    notes.push(`ref_ fills: ${refN}, ach_ fills: ${achN}`);

    // Analysis dumps: the full dbrow columns and every decoded enum, regenerated from the
    // LIVE cache on every extraction so league/table analysis never relies on stale files.
    fs.writeFileSync(path.join(outdir, "dbrows.json"), JSON.stringify(dbrowDump));
    fs.writeFileSync(path.join(outdir, "enums.json"), JSON.stringify(enumDump));
    fs.writeFileSync(path.join(outdir, "structs.json"), JSON.stringify(structDump));
    fs.writeFileSync(path.join(outdir, "names.json"), JSON.stringify({
        varbit: Object.fromEntries(names.varbit), varp: Object.fromEntries(names.varp),
        varc: Object.fromEntries(names.varc),
    }));

    const indices = await engine.rawsource.getCacheIndex(CLIENTSCRIPT_MAJOR);
    const ids: number[] = [];
    for (const e of indices) { if (e) { ids.push(e.minor); } }
    const failed: number[] = [];
    const annCounts: { [k: string]: number } = {};
    let ok = 0, lastProg = 0;
    writeProgress("decompile", 0, ids.length);
    for (let i = 0; i < ids.length; i++) {
        const id = ids[i];
        try {
            const buf = await engine.rawsource.getFileById(CLIENTSCRIPT_MAJOR, id);
            let text: string;
            try {
                text = await renderClientScript(engine.rawsource, buf, id);
            } catch (e) {
                // the first renderClientScript call in a fresh process always throws
                // "not compatible" (lazy init inside rsmv); retry once now that it's warm
                text = await renderClientScript(engine.rawsource, buf, id);
            }
            text = applyNames(text, names);
            text = annotate(text, cast, enumTables, paramtypes, dbschema, ifaceCounts, ifaceComps, annCounts);
            fs.writeFileSync(path.join(scriptsdir, `clientscript-${id}.ts`), text);
            ok++;
        } catch (e) {
            failed.push(id);
        }
        if (Date.now() - lastProg > 800 || i === ids.length - 1) {
            lastProg = Date.now();
            writeProgress("decompile", i + 1, ids.length);
        }
    }

    const annTotal = Object.values(annCounts).reduce((a, b) => a + b, 0);
    const meta = {
        ver: 3, date: startedAt, buildnr, total: ids.length, ok,
        failed, names: { varbit: names.varbit.size, varp: names.varp.size, varc: names.varc.size },
        annotations: { total: annTotal, ...annCounts },
        xref: {
            items: cast.obj.size, npcs: cast.npc.size, locs: cast.loc.size,
            quests: cast.quest.size, stats: cast.stat.size, achievements: cast.achievement.size,
            dbrows: cast.dbrow.size, structs: cast.struct.size, categories: cast.category.size,
            string_enums: enumTables.size, iface_groups: ifaceCounts.size, iface_comps: ifaceComps.size,
        },
        durationMs: Date.now() - t0, notes,
    };
    const tmp = path.join(outdir, "meta.json.tmp");
    fs.writeFileSync(tmp, JSON.stringify(meta, null, 1));
    fs.renameSync(tmp, path.join(outdir, "meta.json"));
    writeProgress("done", ids.length, ids.length);
    console.log(`cs2export done: ${ok}/${ids.length} scripts, ${annTotal} annotations, build ${buildnr}, ${meta.durationMs}ms`);
    process.exit(0);
})().catch(e => { console.error("ERR", e); process.exit(1); });
