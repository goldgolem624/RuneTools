// Software item-icon renderer: the live-cache replacement for the prebuilt items.pack.
// Pure node, no GL. Pipeline per item:
//   item def (baseModel op1, icon camera ops 4-8: model_zoom / rotation_0 / rotation_1 /
//   modelTranslate_0/1, op40 recolours + op41 retextures via modifyMesh)
//   -> RT7 model meshes (rsmv decode, validated) -> textured z-buffered software
//   rasterizer with the classic 2d-icon projection -> supersampled -> outline + drop
//   shadow -> PNG. Noted items composite the parent icon (82%) over the note template.
// usage: node dist/itemicon.js out=<dir> ids=4151,61835[,..] [size=36] [ss=4]
//        node dist/itemicon.js out=<dir> all [start=0] [size=36] [ss=4]
import { EngineCache, ThreejsSceneCache } from "3d/modeltothree";
import { GameCacheLoader } from "cache/sqlite";
import { modifyMesh } from "3d/mapsquare";
import { packedHSL2RGBfloat } from "utils";
import { parse } from "opdecoder";
import * as fs from "fs";
import * as path from "path";

// browser polyfills so rsmv's texture decode works in node (same block as avatar.ts)
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

type Wrap = "repeat" | "clamp" | "mirror";
type Mat = {
    tex: ImageData | null, compound: ImageData | null,
    texMips: ImageData[],                 // [0] = tex, then successive 2x box-filtered levels
    texMean: [number, number, number],    // measured mean of the texture (its own average)
    frac: number, base: [number, number, number],
    wrapU: Wrap, wrapV: Wrap,
    mode: "opaque" | "cutoff" | "blend", cutoff: number,
};

// Successive 2x box-filter chain. The game samples mip-mapped textures; at icon scale a
// 128px texture lands on ~25px of sprite, i.e. mip 2-3. Sampling mip 0 nearest kept every
// bark line and grain stripe the game averages away (logs were the visible case).
const mipCache = new WeakMap<ImageData, ImageData[]>();
function buildMips(img: ImageData): ImageData[] {
    const hit = mipCache.get(img);
    if (hit) return hit;
    const mips = [img];
    let cur = img;
    while (cur.width > 4 && cur.height > 4 && mips.length < 7) {
        const w = cur.width >> 1, h = cur.height >> 1;
        const dst = new Uint8ClampedArray(w * h * 4);
        for (let y = 0; y < h; y++) {
            for (let x = 0; x < w; x++) {
                for (let c = 0; c < 4; c++) {
                    dst[(y * w + x) * 4 + c] = (
                        cur.data[((2 * y) * cur.width + 2 * x) * 4 + c] +
                        cur.data[((2 * y) * cur.width + 2 * x + 1) * 4 + c] +
                        cur.data[((2 * y + 1) * cur.width + 2 * x) * 4 + c] +
                        cur.data[((2 * y + 1) * cur.width + 2 * x + 1) * 4 + c] + 2) >> 2;
                }
            }
        }
        cur = { width: w, height: h, data: dst } as any;
        mips.push(cur);
    }
    mipCache.set(img, mips);
    return mips;
}

// Bilinear sample of one mip with per-axis wrapping. Returns rgba 0..1.
function sampleBilinear(img: ImageData, u: number, v: number, wrapU: Wrap, wrapV: Wrap,
    out: [number, number, number, number]) {
    const fx = sampleWrap(u, wrapU) * img.width - 0.5;
    const fy = sampleWrap(v, wrapV) * img.height - 0.5;
    let x0 = Math.floor(fx), y0 = Math.floor(fy);
    const dx = fx - x0, dy = fy - y0;
    const wrap = (i: number, n: number, m: Wrap) => {
        if (i < 0) return m === "repeat" ? n - 1 : 0;
        if (i >= n) return m === "repeat" ? 0 : n - 1;
        return i;
    };
    const x1 = wrap(x0 + 1, img.width, wrapU), y1 = wrap(y0 + 1, img.height, wrapV);
    x0 = wrap(x0, img.width, wrapU); y0 = wrap(y0, img.height, wrapV);
    const d = img.data;
    const i00 = (y0 * img.width + x0) * 4, i10 = (y0 * img.width + x1) * 4;
    const i01 = (y1 * img.width + x0) * 4, i11 = (y1 * img.width + x1) * 4;
    for (let c = 0; c < 4; c++) {
        const top = d[i00 + c] * (1 - dx) + d[i10 + c] * dx;
        const bot = d[i01 + c] * (1 - dx) + d[i11 + c] * dx;
        out[c] = (top * (1 - dy) + bot * dy) / 255;
    }
}

// read one attribute value, honouring integer normalization (BufferAttribute.getX does
// not denormalize in this three version)
function attrVal(attr: any, i: number, c: number): number {
    const v = attr.array[i * attr.itemSize + c];
    if (!attr.normalized) return v;
    if (attr.array instanceof Uint8Array) return v / 255;
    if (attr.array instanceof Uint16Array) return v / 65535;
    if (attr.array instanceof Int8Array) return Math.max(-1, v / 127);
    if (attr.array instanceof Int16Array) return Math.max(-1, v / 32767);
    return v;
}

async function materialFor(scene: any, engine: any, matId: number, texCache: Map<number, ImageData | null>): Promise<Mat> {
    const out: Mat = { tex: null, compound: null, texMips: [], texMean: [1, 1, 1], frac: 0, base: [1, 1, 1], wrapU: "repeat", wrapV: "repeat", mode: "opaque", cutoff: 0.5 };
    if (matId < 0) return out;
    let mat: any = null;
    try { mat = engine.getMaterialData(matId); } catch (e) { return out; }
    if (!mat) return out;
    // the REAL alpha mode + threshold (the raster used to hardcode cutoff 0.5 for everything,
    // and had no blend path at all: glass drew opaque and hid whatever sat behind it)
    if (mat.alphamode === "cutoff" || mat.alphamode === "blend") out.mode = mat.alphamode;
    if (typeof mat.alphacutoff === "number") out.cutoff = mat.alphacutoff;
    // The engine has no per-material "baseColorFraction" at draw time. It bakes TWO vertex
    // colour streams and a per-vertex flag picks between them (ModelGeometry.vs):
    //   vVertexAlbedo = mix(unwhitenedRGB, aVertexColour, step(.5, fract(batchFlags*8)))
    // and SampleTexturesRT7 gates the texture fetch on the same bit. So "whitened" is the
    // stream used by textured vertices, and rsmv's baseColorFraction models that whitening.
    //   v0: extra.baseColorFraction / baseColor are real, use them.
    //   v1/v2: carry no such field. rsmv hardcodes frac=1 (jmat.ts:94 "this is very wrong"),
    //   which resolves to white and discards the model's tint. Keep the tint (frac=0).
    // v1's `ignore_vertexcol_17` looks like it should be the whitening flag, but honouring it
    // makes primal platebody (20822, the only v1 item in the reference set) worse, 40.5 vs
    // 25.7 mean |dRGB|. The bit's meaning is a guess in rsmv's opcode table, so do not use it.
    // Inverting the fraction is also wrong: it costs 32.75 vs 21.34 across the whole set.
    const rawMat: any = mat.raw;
    const v12: any = rawMat && (rawMat.v1 || rawMat.v2);
    if (ICON_WHITEN === "base") {
        // legacy behaviour: mix toward the material baseColor; v1/v2 keep the tint
        if (v12) { out.frac = 0; out.base = [1, 1, 1]; }
        else {
            if (typeof mat.baseColorFraction == "number") out.frac = mat.baseColorFraction;
            if (Array.isArray(mat.baseColor)) out.base = mat.baseColor as any;
        }
    } else if (ICON_WHITEN === "white") {
        // uniform whiten-to-white (an earlier hypothesis, superseded by "auto")
        out.frac = (v12 ? 1 : (typeof mat.baseColorFraction == "number" ? mat.baseColorFraction : 0));
        out.base = [1, 1, 1];
    } else {
        // DEFAULT ("auto"): derived by reconciling items that demonstrably KEEP the vertex
        // tint (Support cape 32055, Elder Trove 51800, Ek-ZekKil 52527, EoF amulet 51056,
        // primal 20822) against items that demonstrably DROP it (AGS 11694, Queen help
        // book 10562, Aegis 33971, Ripper 48802):
        //   v0:  the mix always applies with the material's fraction; extra.unk00_flags&1
        //        switches the TARGET from baseColor to white (AGS/book/aegis have 1 and
        //        mixing toward their saturated baseColor turned the book mustard where the
        //        game shows bright gold; cape has 0 and its dark baseColor is exactly what
        //        the game renders; Starfire coif has 0 with the baseColor==0 sentinel,
        //        which already decodes to white).
        //   v1+: whiten fully iff the material has NO compound (metalness) map. With a
        //        compound, the shader itself drops the tint per pixel where metalness is
        //        high (albedo = mix(texel*vcol, texel, metalness)), so CPU whitening would
        //        double-apply; without one, the whiten bit stands in for metalness.
        if (v12) {
            out.base = [1, 1, 1];
            out.frac = (mat.textures?.compound ? 0 : 1);
        } else {
            const towhite = ((rawMat?.v0?.extra?.unk00_flags ?? 1) & 1) !== 0;
            if (typeof mat.baseColorFraction == "number") out.frac = mat.baseColorFraction;
            out.base = (towhite || !Array.isArray(mat.baseColor)) ? [1, 1, 1] : mat.baseColor as any;
        }
    }
    // Texture wrapping is per axis, clamp/repeat/mirror (getTexelBias_inner). rsmv already
    // decodes it; we used to hardcode repeat, which mirrors the logs' V axis incorrectly.
    if (mat.texmodes) out.wrapU = mat.texmodes;
    if (mat.texmodet) out.wrapV = mat.texmodet;
    if (process.env.ICONDEBUG) console.error("mat", matId, "v", v12 ? (rawMat.v1 ? 1 : 2) : 0, "frac", out.frac, "base", JSON.stringify(out.base), "difftex", mat.textures?.diffuse, "compound", mat.textures?.compound, "wrap", out.wrapU, out.wrapV);

    const fetchTex = async (kind: "diffuse" | "compound", id: number | undefined) => {
        if (!id) return null;
        const key = (kind === "diffuse" ? 1 : 2) * 1e7 + id;
        if (!texCache.has(key)) {
            let img: ImageData | null = null;
            try {
                const tf = await scene.getTextureFile(kind, id, kind === "diffuse" && !!mat.stripDiffuseAlpha);
                img = await tf.toImageData();
            } catch (e) { img = null; }
            texCache.set(key, img);
        }
        return texCache.get(key) || null;
    };
    out.tex = await fetchTex("diffuse", mat.textures?.diffuse);
    if (out.tex) {
        out.texMips = buildMips(out.tex);
        // the texture's own average, from the smallest mip: v0 baseColor TRACKS this value
        // on every material sampled, but measuring it directly is exact for all of them
        const last = out.texMips[out.texMips.length - 1];
        let r = 0, g = 0, b = 0;
        const n = last.width * last.height;
        for (let i = 0; i < n; i++) { r += last.data[i * 4]; g += last.data[i * 4 + 1]; b += last.data[i * 4 + 2]; }
        out.texMean = [Math.max(0.02, r / n / 255), Math.max(0.02, g / n / 255), Math.max(0.02, b / n / 255)];
    }
    // compound = the RT7 metalness/roughness map (shader: e.s = metalness, e.t = roughness).
    // Only v1/v2 materials can carry one; v0 runs on the shader defaults.
    out.compound = await fetchTex("compound", mat.textures?.compound);
    return out;
}
// ===================== SHADING =====================
// The ModelSprite pass compiles with GAMMA_CORRECT_INPUTS, so every transfer function
// is gamma 2.0:
//     SRGBToLinear(c) = c*c            LinearToSRGBRunescape(v) = sqrt(v)
//
//   albedo  i = mix(texel*vcol, texel, metalness)                     (all linear)
//   specCol I = mix(vec3(nonMetalSpecular), i, metalness)
//   F_rough   = FresnelSchlickRoughness(I, NdotV, 1-rough)
//   energy  c = (1 - F_rough) * (1 - metalness)
//   sun.Diffuse  = sunColour * max(0, dot(N, L))
//   sun.Specular = CookTorrance(GGX, Schlick-Smith vis, Schlick fresnel) * sunColour * NdotL
//   amb.Diffuse  = ambientColour * c        (EvaluateIBL already applies energy once)
//   amb.Specular = envColour * (F_rough*brdf.x + brdf.y)
//   out = (amb.Diffuse*c + sun.Diffuse*c) * i  +  amb.Specular + sun.Specular
// Note the albedo multiplies the DIFFUSE ONLY and the speculars are ADDED afterwards. That
// is why a diffuse-only model can never match: the residual is signed differently for glossy
// and matte items, so no brightness constant fixes both.
//
// Free parameters below are the engine-side uniforms, which are NOT in the cache and are the
// same for every item. They come from a single global least-squares solve against the reference
// icon set (fit_icon_light.py), not from per-item fudges.
//
// STATUS: this path is OPT-IN (ICON_PBR=1) and does NOT yet beat the legacy approximation.
// The form is right; the ALBEDO feeding it is not. Freeing the albedo exponents in the fit
// drops the residual from 21.3 to 18.8 AND collapses the ambient colour from a physically
// absurd blue cast (0.52, 0.68, 1.06) to neutral (0.38, 0.36, 0.39) -- i.e. the light fit was
// absorbing an albedo error. The exponents land near vcol^1.10 and texel^1.57 where the shader
// says vcol^2 and texel^2, so our decoded vertex colours and texels are systematically darker
// than the engine's. Suspect rsmv's HSL->RGB vertex-colour decode and/or the DXT diffuse
// decode. Fix that before trusting these constants.
const V3 = (s: string | undefined, d: [number, number, number]): [number, number, number] => {
    if (!s) return d;
    const p = s.split(",").map(Number);
    return (p.length === 3 && p.every(n => isFinite(n))) ? [p[0], p[1], p[2]] : d;
};
const ICON_SUN_COL = V3(process.env.ICON_SUN_COL, [0.4345, 0.5494, 0.3892]);
const ICON_AMB_COL = V3(process.env.ICON_AMB_COL, [0.4228, 0.3629, 0.3635]);
const ICON_ENV_COL = V3(process.env.ICON_ENV_COL, [0.4662, 0.4471, 0.0418]);
const _ldir = V3(process.env.ICON_LDIR, [-0.8502, -0.5265, 0.0]);   // model space, points AT the light
const _ldl = Math.hypot(_ldir[0], _ldir[1], _ldir[2]) || 1;
const LX = _ldir[0] / _ldl, LY = _ldir[1] / _ldl, LZ = _ldir[2] / _ldl;
const ICON_NMSPEC   = +(process.env.ICON_NMSPEC ?? 0.0129);   // nonMetalSpecular
const ICON_FRESNEL  = +(process.env.ICON_FRESNEL ?? 11.98);   // fresnelExponent
const ICON_ROUGH_UNTEX = +(process.env.ICON_ROUGH_UNTEX ?? 0.9);  // shader default e=(0,.9,0,0)
const ICON_ROUGH_TEX   = +(process.env.ICON_ROUGH_TEX ?? 0.98);   // v0 (no compound map)
// Fraction of a final pixel's supersamples that must pass the engine's alpha > 0.02
// visibility rule (OutlineAndShadow.fs) for the pixel to be part of the sprite.
const ICON_ALPHA_THRESH = +(process.env.ICON_ALPHA_THRESH ?? 0.5);
const ICON_GBUF = process.env.ICON_GBUF || "";                // dump a G-buffer instead of shading
const ICON_FRAC_INV = process.env.ICON_FRAC_INV === "1";      // flip the whitening fraction
const ICON_WHITEN = process.env.ICON_WHITEN || "";            // "white": whiten toward white, v1/v2 frac=1

const PI = Math.PI;
const clamp01 = (v: number) => v < 0 ? 0 : v > 1 ? 1 : v;

// lighting/brdf/*.inc, verbatim
const fresnelSchlick = (F0: number, u: number, expo: number) => F0 + (1 - F0) * Math.pow(1 - u, expo);
const fresnelSchlickRoughness = (F0: number, NdotV: number, smooth: number) =>
    F0 + (Math.max(smooth, F0) - F0) * Math.pow(1 - NdotV, 5);
const ggxNDF = (a: number, NdotH: number) => {           // GGXTrowbridgeReitzNDF
    const P = a * a, I = NdotH * NdotH, T = I * (P - 1) + 1;
    return P / (PI * (T * T + 1e-4));
};
const schlickSmithVis = (o: number, NdotL: number, NdotV: number) => {   // SchlickSmithVis
    const P = 1 / Math.sqrt((PI / 4) * o + PI / 2), d = 1 - P;
    return 1 / ((NdotL * d + P) * (NdotV * d + P) + 1e-4);
};
// PerceptualRoughnessToRoughness is not in the extracted MathUtils.inc; the engine's GGX takes
// the squared perceptual roughness, which is the universal convention.
const perceptualToAlpha = (r: number) => r * r;

// The IBL split-sum BRDF term. The engine samples uGlobalBRDFLUT (generated at runtime by
// BRDFIntegrationLut.fs). Karis' analytic fit reproduces that table to within ~1/255, which is
// far below the noise floor of a 36x32 sprite.
function envBRDF(NdotV: number, rough: number): [number, number] {
    const c0 = [-1, -0.0275, -0.572, 0.022], c1 = [1, 0.0425, 1.04, -0.04];
    const rx = rough * c0[0] + c1[0], ry = rough * c0[1] + c1[1];
    const rz = rough * c0[2] + c1[2], rw = rough * c0[3] + c1[3];
    const a004 = Math.min(rx * rx, Math.pow(2, -9.28 * NdotV)) * rx + ry;
    return [a004 * -1.04 + rz, a004 * 1.04 + rw];
}

/** The full RT7 fragment shader for one pixel. Inputs linear, output linear. */
function shadeRT7(
    albedo: [number, number, number], N: [number, number, number], V: [number, number, number],
    metal: number, rough: number,
): [number, number, number] {
    const NdotV = Math.max(0, N[0] * V[0] + N[1] * V[1] + N[2] * V[2]);
    const NdotL = Math.max(0, N[0] * LX + N[1] * LY + N[2] * LZ);
    const smooth = 1 - rough;

    // half vector between the view dir and the light dir
    let hx = V[0] + LX, hy = V[1] + LY, hz = V[2] + LZ;
    const hl = Math.hypot(hx, hy, hz) || 1;
    hx /= hl; hy /= hl; hz /= hl;
    const NdotH = Math.max(0, N[0] * hx + N[1] * hy + N[2] * hz);
    const LdotH = clamp01(LX * hx + LY * hy + LZ * hz);

    const D = ggxNDF(perceptualToAlpha(rough), NdotH);
    const Vis = schlickSmithVis(Math.pow(8192, smooth), NdotL, NdotV);
    const [bx, by] = envBRDF(NdotV, rough);

    const out: [number, number, number] = [0, 0, 0];
    for (let ch = 0; ch < 3; ch++) {
        const I = ICON_NMSPEC * (1 - metal) + albedo[ch] * metal;   // specular colour
        const Fr = fresnelSchlickRoughness(I, NdotV, smooth);
        const c = (1 - Fr) * (1 - metal);                            // energy conservation
        const sunD = ICON_SUN_COL[ch] * NdotL;
        const sunS = fresnelSchlick(I, LdotH, ICON_FRESNEL) * D * Vis * ICON_SUN_COL[ch] * NdotL;
        const ambD = ICON_AMB_COL[ch] * c;      // EvaluateIBL applies energy once...
        const ambS = ICON_ENV_COL[ch] * (Fr * bx + by);
        const x = (ambD + sunD) * c;            // ...and the fs multiplies by it again
        out[ch] = x * albedo[ch] + ambS + sunS;
    }
    return out;
}

// Per-pixel G-buffer: vcol(3, sRGB, post-whitening) | texel(3, sRGB) | normal(3, classic model
// space) | view(3) | metal | rough | alpha. Shading reads only this, so the offline fitter and
// the renderer cannot drift. vcol and texel stay separate so the fit can probe the albedo term
// (the vertex-colour decode and the whitening semantics) independently of the light.
const GB = 15;

function sampleWrap(v: number, mode: Wrap): number {
    if (mode === "repeat") return v - Math.floor(v);
    if (mode === "clamp") return clamp01(v);
    const t = Math.abs(v) % 2;                    // mirror
    return t > 1 ? 2 - t : t;
}

// The legacy shading, kept as the DEFAULT until the albedo term is resolved. It is a fitted
// approximation (see ICON_SHADING_GROUND_TRUTH.md section 4), not the engine's equation, but it
// currently scores better end to end than the faithful equation does with an unresolved albedo.
// Turn the real one on with ICON_PBR=1.
// PBR is OPT-IN. It is the engine's real equation, but its light uniforms are runtime scene
// state (uSunColour / uAmbientColour / uIrradianceSHCoefs), not stored in the local cache
// (js5-31 LIGHT is not downloaded; the bake copies the engine's live light), so the values
// here are a global fit, not recovered constants. With a constant ambient the fit cannot place
// the sun highlight correctly and metals/gold come out too dark, so the legacy path stays the
// default until the real light is captured from a running client. See ICON_SHADING_GROUND_TRUTH.
const ICON_PBR = process.env.ICON_PBR === "1";
// The legacy path historically applied pow(vcol, 0.8) to a textbook HSL decode. The decode is
// now Jagex's real palette, which already bakes a 0.6 brightness power, so the legacy display
// exponent becomes 0.8/0.6 = 1.333 to preserve that same tuned brightness. This is not a new
// fudge: it re-expresses the original's single constant against the corrected palette. Measured
// slightly better than the original on the reference set (18.7 vs 19.1 mean |dRGB|).
const LEG_VCOL_POW = +(process.env.ICON_VCOL_POW ?? (0.8 / 0.6));
const LEG_AMB = +(process.env.ICON_AMB ?? 0.75);
const LEG_SUN = +(process.env.ICON_SUN ?? 0.4);
const _lleg = V3(process.env.ICON_LEG_LDIR, [-50, -10, -50]);
const _lll = Math.hypot(_lleg[0], _lleg[1], _lleg[2]) || 1;
const GX = _lleg[0] / _lll, GY = _lleg[1] / _lll, GZ = _lleg[2] / _lll;
// ICON_CLASSIC=1: the classic engine's item-sprite lighting (RuneLite ItemSpriteFactory.light,
// fetched verbatim): lightness = ambient + dot(N, L)/scale applied to the PALETTE
// lightness (so ~pow 0.6 into RGB), clamped 2..126, light (-50,-10,-50), per-item def
// ambient/contrast offsets honoured (ambient+64, contrast+768). Unlike the legacy term it
// darkens faces pointing away from the light (no max(0,cos)) and saturates highlights at
// 126/128. The one constant classic does not define for RS3 is the dot scale: classic divides
// by the area-scaled normal magnitudes its own computeNormals produces, which RS3's shipped
// unit normals do not have. ICON_CL_K is that scale (lightness units per unit cosine).
// DEFAULT (ICON_CLASSIC=0 restores the fitted legacy term). Measured over a
// 249-item wiki sample: mean |dRGB| 36.0 -> 33.2, median 34.1 -> 30.4; gold bar 39.2 -> 20.1
// (the signed cosine darkens away-facing metal faces, which max(0,cos) never could). K/AMB
// swept: flat optimum around K 40-48, AMB 64 (the classic default ambient confirms itself).
const ICON_CLASSIC = process.env.ICON_CLASSIC !== "0";
const ICON_CL_K = +(process.env.ICON_CL_K ?? 44);
const ICON_CL_AMB = +(process.env.ICON_CL_AMB ?? 64);
// Which raw compound channel is e.s (metalness). The shader's albedo drops the vertex tint
// per pixel by metalness: i = mix(texel*vcol, texel, e.s) (ModelGeometryRT7.fs:40, re-read). Channel under test: Elite Dracolich coif/chaps + Deathwarden (tint visibly
// dropped in game) read 0.00 in R but 0.67-0.79 in G, and G-as-metal predicts the game's
// icon means within ~3/255 on all three.
const ICON_METAL_CH = +(process.env.ICON_METAL_CH ?? 1);
// Metalness DARKENS in this renderer (albedo -> raw texel, and metals have no diffuse) but
// we have no environment specular to give them back their brightness, so high-metal cloth/
// armour comes out near-black where the game renders it bright. ICON_METAL_SCALE dials the
// mix down; 0 disables it entirely (treat everything as dielectric).
const ICON_METAL_SCALE = +(process.env.ICON_METAL_SCALE ?? 1);
// Additive env/specular term (the missing brightness on metals + a flat studio ambient).
// Opt-in until fitted; the fit writes the constants back here. All linear-space.
const ICON_ENV = process.env.ICON_ENV === "1";
const ICON_ENV_METAL = +(process.env.ICON_ENV_METAL ?? 0.35);   // metal reflection strength
const _envc = V3(process.env.ICON_ENV_COL, [0.9, 0.92, 1.0]);   // env reflection colour (cool studio)
const ICON_ENV_COLR = _envc[0], ICON_ENV_COLG = _envc[1], ICON_ENV_COLB = _envc[2];
const ICON_ENV_AMB = +(process.env.ICON_ENV_AMB ?? 0.06);       // flat dielectric ambient lift
// Cast shadows from the icon light (software shadow map over the opaque geometry). The
// engine's bake runs with sun shadows on; the ingot-bar top-face shadow is a cast shadow.
const ICON_SHADOWMAP = process.env.ICON_SHADOWMAP !== "0";
const ICON_SM_RES = +(process.env.ICON_SM_RES ?? 224);
const ICON_SM_BIAS = +(process.env.ICON_SM_BIAS ?? 2.0);   // model units after /4
// Texture-average normalization of the texel (the vertex colour carries the hue).
// 0 = off; 1 (default) = divide by the material's stored baseColor, whiten premix as-is
// (measured best: trusted median 17.5); 2 = experiment: divide by the texture's own
// measured mean and let a white-target frac select the RAW texel (regressed to 19.3).
const ICON_TEXNORM = +(process.env.ICON_TEXNORM ?? 1);
// Palette-space classic lighting (default ON, 0 = old RGB-space multiply): method2608
// scales the packed-HSL LIGHTNESS before the palette's pow-0.6 decode, which preserves
// saturation on vivid hues; multiplying decoded RGB by the light muted every bright
// orange/teal/red toward grey.
const ICON_HSL_LIGHT = process.env.ICON_HSL_LIGHT !== "0";
// RuneLite ItemSpriteFactory.method2608/bound2to126, verbatim semantics
function litPackedRGB(hsl: number, t: number): [number, number, number] {
    let l = ((hsl & 127) * t) >> 7;
    l = l < 2 ? 2 : l > 126 ? 126 : l;
    return packedHSL2RGBfloat((hsl & 0xff80) + l);
}

/** Apply the shader to a G-buffer, producing premultiplied-alpha LINEAR rgba.
 *  `cl` carries the per-item classic light params (ICON_CLASSIC mode). */
function shadeGBuffer(gb: Float32Array, W: number, H: number, cl?: { amb: number, k: number }): Float32Array {
    const out = new Float32Array(W * H * 4);
    for (let i = 0; i < W * H; i++) {
        const o = i * GB;
        const a = gb[o + 14];
        if (a <= 0) continue;
        const vc: [number, number, number] = [gb[o], gb[o + 1], gb[o + 2]];
        const tx: [number, number, number] = [gb[o + 3], gb[o + 4], gb[o + 5]];
        const N: [number, number, number] = [gb[o + 6], gb[o + 7], gb[o + 8]];
        const V: [number, number, number] = [gb[o + 9], gb[o + 10], gb[o + 11]];
        let lin: [number, number, number];
        const metal = gb[o + 12];
        if (ICON_PBR) {
            // shader albedo: i = mix(texel*vcol, texel, metalness), all linear (squared sRGB)
            const albedo: [number, number, number] = [0, 0, 0];
            for (let c = 0; c < 3; c++) {
                const t2 = tx[c] * tx[c];
                albedo[c] = vc[c] * vc[c] * t2 * (1 - metal) + t2 * metal;
            }
            lin = shadeRT7(albedo, N, V, metal, gb[o + 13]);
        } else if (ICON_CLASSIC && cl) {
            // Classic sprite light: lightness = ambient + k*cos applied to the palette L
            // channel. Scaling L by t scales the palette's decoded RGB by ~t^0.6 (the
            // palette bakes pow 0.6), so the vcol needs NO extra display exponent here.
            // The albedo drops the vertex tint per pixel by metalness, exactly as the RT7
            // fs does: mix(texel*vcol, texel, e.s). This is what pales metal armour
            // (Dracolich, Deathwarden) whose tinted vcol x texel is far too dark.
            const cos = N[0] * GX + N[1] * GY + N[2] * GZ;
            let t = cl.amb + cl.k * cos;
            t = t < 2 ? 2 : t > 126 ? 126 : t;
            const li = Math.pow(t / 128, 0.6);
            lin = [0, 0, 0] as any;
            for (let c = 0; c < 3; c++) {
                const s = (vc[c] * tx[c] * (1 - metal) + tx[c] * metal) * li;
                lin[c] = s * s;
            }
        } else {
            const d = Math.max(0, N[0] * GX + N[1] * GY + N[2] * GZ);
            const li = LEG_AMB + LEG_SUN * d;
            // legacy multiplied in sRGB byte space and did not encode; square it here so the
            // shared linear resolve + sqrt encode below round-trips it unchanged.
            lin = [0, 0, 0] as any;
            for (let c = 0; c < 3; c++) {
                const s = Math.pow(Math.max(0, vc[c]), LEG_VCOL_POW) * tx[c] * li;
                lin[c] = s * s;
            }
        }
        // premultiplied, like the forward renderer (a is 0/1 here so this is a no-op today)
        out[i * 4] = lin[0] * a; out[i * 4 + 1] = lin[1] * a; out[i * 4 + 2] = lin[2] * a; out[i * 4 + 3] = a;
    }
    return out;
}

// Per-item classic light params from the item def: RuneLite lights inventory models with
// (item.ambient + 64, item.contrast + 768); the contrast offset scales the dot term down.
function classicParams(def: any): { amb: number, k: number } {
    return {
        amb: ICON_CL_AMB + (def?.ambiance | 0),
        k: ICON_CL_K * 768 / (768 + (def?.contrast | 0)),
    };
}

// Render one item's baseModel into a G-buffer of W x H (already supersampled). Returns
// null when the item has no renderable model.
async function renderModelGBuf(scene: any, engine: any, def: any, W: number, H: number,
    texCache: Map<number, ImageData | null>): Promise<Float32Array | null> {
    if (!def.baseModel) return null;
    const mods: any = {};
    if (def.color_replacements) mods.replaceColors = def.color_replacements;
    if (def.material_replacements) mods.replaceMaterials = def.material_replacements;
    let md: any;
    try { md = await scene.getModelData(def.baseModel); }
    catch (e) { if (process.env.ICONDEBUG) console.error("model", def.baseModel, "ERROR:", (e as Error)?.message); return null; }
    if (!md || !md.meshes || !md.meshes.length) { if (process.env.ICONDEBUG) console.error("model", def.baseModel, "no meshes"); return null; }
    const meshes = md.meshes.map((m: any) => modifyMesh(m, mods));

    // VERBATIM classic icon pipeline (RuneLite cache ItemSpriteFactory + Model.projectAndDraw,
    // fetched as ground truth):
    //   sprite 36x32, raster centre (16,16), projection x*512/z
    //   var17 = zoom*sin(xan2d)>>16 ; var18 = zoom*cos(xan2d)>>16
    //   projectAndDraw(0, yan2d, zan2d, xan2d, xOffset2d,
    //                  modelHeight/2 + var17 + yOffset2d,   // y offset
    //                  var18 + yOffset2d)                    // z offset -- yoff in BOTH
    //   per vertex: roll(zan2d) -> pitchless yaw(yan2d) about Y -> translate ->
    //               camera pitch(xan2d): y' = y*cos - z*sin ; z' = y*sin + z*cos ->
    //               sx = cx + x*512/z' ; sy = cy + y'*512/z'
    //   modelHeight = -min(y) in classic y-DOWN space; models are NOT re-centred.
    // RS3 deltas: RT7 positions are y-UP and 4x classic units (divide by 4, negate y);
    // items carry no zan2d. Angles are 2048ths of a turn.
    const POSDIV = 4;
    const zoom = def.model_zoom || 2000;
    const pitch = ((def.rotation_0 || 0) / 2048) * Math.PI * 2;
    const yaw = ((def.rotation_1 || 0) / 2048) * Math.PI * 2;
    // rotation_2 (op 0x5F) = roll about Z, applied BEFORE yaw (RuneLite Model.java does
    // xyRotation first, then xzRotation). Thousands of items carry it (grimy herbs etc.)
    // and rendered mis-rotated while it was ignored.
    const roll = ((def.rotation_2 || 0) / 2048) * Math.PI * 2;
    const sinR = Math.sin(roll), cosR = Math.cos(roll);
    const xoff = def.modelTranslate_0 || 0, yoff = def.modelTranslate_1 || 0;
    const sinP = Math.sin(pitch), cosP = Math.cos(pitch), sinY = Math.sin(yaw), cosY = Math.cos(yaw);
    let rawMaxY = 0;
    for (const m of meshes) {
        const pp = m.attributes.pos;
        for (let i = 0; i < pp.count; i++) { const v = attrVal(pp, i, 1); if (v > rawMaxY) rawMaxY = v; }
    }
    const modelHeight = rawMaxY / POSDIV;                 // classic: -min(y_down)
    const transY = modelHeight / 2 + zoom * sinP + yoff;
    const transZ = zoom * cosP + yoff;
    const su = W / 36;                                    // supersample unit scale
    const scale = 512 * su;
    const cxp = 16 * su, cyp = 16 * su;                   // graphics.setOffset(16, 16)

    // The engine keeps the model still and moves the camera (FUN_1400c4b90 builds a view
    // matrix from eye/target/up), so uModelMatrix is identity: normals and uInvSunDirection
    // both live in model space. Our classic projection instead rotates the model, so we shade
    // with the UNROTATED normal and derive the camera position by inverting the transform.
    // Camera sits at the origin of the post-pitch space; a rotation about X fixes the origin,
    // so in pre-translate space it is at -T, and in model space at Rroll^-1 Ryaw^-1 (-T).
    const cTx = -xoff, cTy = -transY, cTz = -transZ;
    const cYx = cTx * cosY - cTz * sinY, cYz = cTx * sinY + cTz * cosY;   // Ryaw^-1
    const camX = cYx * cosR - cTy * sinR;                                  // Rroll^-1
    const camY = cTy * cosR + cYx * sinR;
    const camZ = cYz;

    const gbuf = new Float32Array(W * H * GB);
    const zbuf = new Float32Array(W * H).fill(1e30);

    for (const m of meshes) {
        const mat = await materialFor(scene, engine, m.materialId, texCache);
        const pos = m.attributes.pos, col = m.attributes.color, uv = m.attributes.texuvs;
        const nrm = m.attributes.normals;
        if (process.env.ICONDEBUG) console.error("  mesh matId", m.materialId, "verts", pos.count, "hasNormals", !!nrm, "hasCol", !!col, "hasUV", !!uv, "tex", !!mat.tex, "compound", !!mat.compound);
        const idx = m.indices;
        const vcount = pos.count;
        const sx = new Float32Array(vcount), sy = new Float32Array(vcount), sz = new Float32Array(vcount);
        // model-space position + normal, kept for per-pixel shading
        const mx = new Float32Array(vcount), my = new Float32Array(vcount), mz = new Float32Array(vcount);
        const nxs = new Float32Array(vcount), nys = new Float32Array(vcount), nzs = new Float32Array(vcount);
        for (let i = 0; i < vcount; i++) {
            let x = attrVal(pos, i, 0) / POSDIV;
            let y = -attrVal(pos, i, 1) / POSDIV;          // classic y-down
            let z = attrVal(pos, i, 2) / POSDIV;
            mx[i] = x; my[i] = y; mz[i] = z;
            if (nrm) {
                const nx = attrVal(nrm, i, 0), ny = -attrVal(nrm, i, 1), nz = attrVal(nrm, i, 2);
                const len = Math.hypot(nx, ny, nz) || 1;
                nxs[i] = nx / len; nys[i] = ny / len; nzs[i] = nz / len;
            }
            if (roll !== 0) { const t = y * cosR - x * sinR; x = y * sinR + x * cosR; y = t; }
            // yaw (xzRotation in the deob) about Y
            const x1 = z * sinY + x * cosY, z1 = z * cosY - x * sinY;
            // translate
            const xt = x1 + xoff, yt = y + transY, zt = z1 + transZ;
            // camera pitch (orientation) about X, applied AFTER translation
            const y3 = yt * cosP - zt * sinP, z3 = yt * sinP + zt * cosP;
            sz[i] = z3;
            sx[i] = cxp + (xt * scale) / z3;
            sy[i] = cyp + (y3 * scale) / z3;
        }
        // Face normal in model space, used when the mesh carries no normal attribute.
        const faceN = (a: number, b: number, c: number): [number, number, number] => {
            const ux = mx[b] - mx[a], uy = my[b] - my[a], uz = mz[b] - mz[a];
            const vx = mx[c] - mx[a], vy = my[c] - my[a], vz = mz[c] - mz[a];
            let nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
            const l = Math.hypot(nx, ny, nz) || 1;
            return [nx / l, ny / l, nz / l];
        };

        const tri = (a: number, b: number, c: number) => {
            if (sz[a] < 50 || sz[b] < 50 || sz[c] < 50) return;
            const ax = sx[a], ay = sy[a], bx = sx[b], by = sy[b], cx2 = sx[c], cy2 = sy[c];
            const area = (bx - ax) * (cy2 - ay) - (by - ay) * (cx2 - ax);
            if (area === 0) return;                   // degenerate triangle
            const fn = nrm ? null : faceN(a, b, c);
            // NO BACK-FACE CULL. RT7 item models are not consistently wound, and a
            // one-sided cull erased every thin/flat model outright: dragon scimitar
            // (117 of 118 tris culled -> 0 px), fishing net (12/12 -> 0 px), rune
            // platebody, AGS and coins were slivers. The z-buffer resolves occlusion on
            // its own, and the barycentric weights below are normalized by the SIGNED
            // area (inv = 1/area), so the inside test is orientation-independent. Closed
            // models render pixel-identically with the cull gone (logs/shark verified).
            const minX = Math.max(0, Math.floor(Math.min(ax, bx, cx2)));
            const maxX = Math.min(W - 1, Math.ceil(Math.max(ax, bx, cx2)));
            const minY = Math.max(0, Math.floor(Math.min(ay, by, cy2)));
            const maxY = Math.min(H - 1, Math.ceil(Math.max(ay, by, cy2)));
            if (minX > maxX || minY > maxY) return;
            const inv = 1 / area;
            for (let py = minY; py <= maxY; py++) {
                for (let px = minX; px <= maxX; px++) {
                    const w0 = ((bx - ax) * (py + 0.5 - ay) - (by - ay) * (px + 0.5 - ax)) * inv;
                    const w1 = ((cx2 - bx) * (py + 0.5 - by) - (cy2 - by) * (px + 0.5 - bx)) * inv;
                    const w2 = 1 - w0 - w1;
                    // w1 = weight of a, w0 = weight of c, w2 = weight of b (edge-function order)
                    if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                    const wa = w1, wb = w2, wc = w0;
                    const z = wa * sz[a] + wb * sz[b] + wc * sz[c];
                    const pi = py * W + px;
                    if (z >= zbuf[pi]) continue;
                    // vertex colour, whitened toward the material base colour
                    let r = 1, g = 1, bl = 1, al = 1;
                    if (col) {
                        r = wa * attrVal(col, a, 0) + wb * attrVal(col, b, 0) + wc * attrVal(col, c, 0);
                        g = wa * attrVal(col, a, 1) + wb * attrVal(col, b, 1) + wc * attrVal(col, c, 1);
                        bl = wa * attrVal(col, a, 2) + wb * attrVal(col, b, 2) + wc * attrVal(col, c, 2);
                        if (col.itemSize >= 4) al = wa * attrVal(col, a, 3) + wb * attrVal(col, b, 3) + wc * attrVal(col, c, 3);
                    }
                    if (al < 0.2) continue;                  // vertex-alpha cutoff
                    // Whitening: textured vertices use the whitened stream (aVertexColour),
                    // which the engine bakes as the model colour pushed toward the material's
                    // base colour. ICON_FRAC_INV flips the sense of the fraction, which is one
                    // of the two readings of the v0 `baseColorFraction` field still in dispute.
                    const f = ICON_FRAC_INV ? 1 - mat.frac : mat.frac;
                    r = r * (1 - f) + mat.base[0] * f;
                    g = g * (1 - f) + mat.base[1] * f;
                    bl = bl * (1 - f) + mat.base[2] * f;

                    // The diffuse slot is either an sRGB sampler or gets SRGBToLinear applied
                    // (TextureAtlasLookup.inc), so the texel is linear in the shader. We store
                    // it sRGB and let the shading step square it. Untextured faces keep
                    // texel = 1 and the shader's default metal/rough (e = vec4(0,.9,0,0)).
                    let metal = 0, rough = ICON_ROUGH_UNTEX;
                    let tr = 1, tg = 1, tb = 1;
                    if (mat.tex && uv) {
                        let u = wa * attrVal(uv, a, 0) + wb * attrVal(uv, b, 0) + wc * attrVal(uv, c, 0);
                        let v = wa * attrVal(uv, a, 1) + wb * attrVal(uv, b, 1) + wc * attrVal(uv, c, 1);
                        u = sampleWrap(u, mat.wrapU); v = sampleWrap(v, mat.wrapV);
                        const tx2 = Math.min(mat.tex.width - 1, Math.max(0, Math.floor(u * mat.tex.width)));
                        const ty = Math.min(mat.tex.height - 1, Math.max(0, Math.floor(v * mat.tex.height)));
                        const ti = (ty * mat.tex.width + tx2) * 4;
                        if (mat.tex.data[ti + 3] / 255 < 0.5) continue;   // alpha cutoff
                        tr = mat.tex.data[ti] / 255; tg = mat.tex.data[ti + 1] / 255; tb = mat.tex.data[ti + 2] / 255;
                        rough = ICON_ROUGH_TEX;
                        if (mat.compound) {                   // e.s = metalness, e.t = roughness
                            const cx3 = Math.min(mat.compound.width - 1, Math.max(0, Math.floor(u * mat.compound.width)));
                            const cy3 = Math.min(mat.compound.height - 1, Math.max(0, Math.floor(v * mat.compound.height)));
                            const ci = (cy3 * mat.compound.width + cx3) * 4;
                            metal = (mat.compound.data[ci + ICON_METAL_CH] / 255) * ICON_METAL_SCALE;
                            rough = mat.compound.data[ci + 1] / 255;
                        }
                    }
                    // interpolate the model-space normal + position for per-pixel shading
                    let nx: number, ny: number, nz: number;
                    if (fn) { nx = fn[0]; ny = fn[1]; nz = fn[2]; }
                    else {
                        nx = wa * nxs[a] + wb * nxs[b] + wc * nxs[c];
                        ny = wa * nys[a] + wb * nys[b] + wc * nys[c];
                        nz = wa * nzs[a] + wb * nzs[b] + wc * nzs[c];
                        const nl = Math.hypot(nx, ny, nz) || 1;
                        nx /= nl; ny /= nl; nz /= nl;
                    }
                    const wx = wa * mx[a] + wb * mx[b] + wc * mx[c];
                    const wy = wa * my[a] + wb * my[b] + wc * my[c];
                    const wz = wa * mz[a] + wb * mz[b] + wc * mz[c];
                    let vx = camX - wx, vy = camY - wy, vz = camZ - wz;
                    const vl = Math.hypot(vx, vy, vz) || 1;
                    vx /= vl; vy /= vl; vz /= vl;

                    zbuf[pi] = z;
                    const o = pi * GB;
                    gbuf[o] = r; gbuf[o + 1] = g; gbuf[o + 2] = bl;          // vcol, sRGB
                    gbuf[o + 3] = tr; gbuf[o + 4] = tg; gbuf[o + 5] = tb;    // texel, sRGB
                    gbuf[o + 6] = nx; gbuf[o + 7] = ny; gbuf[o + 8] = nz;
                    gbuf[o + 9] = vx; gbuf[o + 10] = vy; gbuf[o + 11] = vz;
                    gbuf[o + 12] = metal; gbuf[o + 13] = rough; gbuf[o + 14] = 1;
                }
            }
        };
        for (let i = 0; i + 2 < idx.count; i += 3) tri(idx.array[i], idx.array[i + 1], idx.array[i + 2]);
    }
    return gbuf;
}

// Forward renderer (the DEFAULT path): shades during rasterization so
// transparency can composite. Two passes:
//   1. opaque + cutoff faces, z-buffered, per-material alpha threshold;
//   2. blend faces (material alphamode=blend, or per-vertex alpha < 1) sorted far-to-near,
//      z-TESTED against the opaque depth and alpha-composited over the result.
// This is what draws a potion's liquid THROUGH its glass: the single-layer G-buffer
// renderer drew the glass opaque (or discarded it) and the liquid could never show.
// Colour per pixel = mix(vcol, white, metal) x texel x classicLight, all sRGB, squared to
// linear at store (same convention as shadeGBuffer's classic branch).
async function renderModelForward(scene: any, engine: any, def: any, W: number, H: number,
    texCache: Map<number, ImageData | null>): Promise<Float32Array | null> {
    if (!def.baseModel) return null;
    const mods: any = {};
    if (def.color_replacements) mods.replaceColors = def.color_replacements;
    if (def.material_replacements) mods.replaceMaterials = def.material_replacements;
    let md: any;
    try { md = await scene.getModelData(def.baseModel); }
    catch (e) { return null; }
    if (!md || !md.meshes || !md.meshes.length) return null;
    const meshes = md.meshes.map((m: any) => modifyMesh(m, mods));
    const cl = classicParams(def);

    // camera: verbatim classic icon projection (see renderModelGBuf for the derivation)
    const POSDIV = 4;
    const zoom = def.model_zoom || 2000;
    const pitch = ((def.rotation_0 || 0) / 2048) * Math.PI * 2;
    const yaw = ((def.rotation_1 || 0) / 2048) * Math.PI * 2;
    const roll = ((def.rotation_2 || 0) / 2048) * Math.PI * 2;
    const sinR = Math.sin(roll), cosR = Math.cos(roll);
    const xoff = def.modelTranslate_0 || 0, yoff = def.modelTranslate_1 || 0;
    const sinP = Math.sin(pitch), cosP = Math.cos(pitch), sinY = Math.sin(yaw), cosY = Math.cos(yaw);
    let rawMaxY = 0;
    for (const m of meshes) {
        const pp = m.attributes.pos;
        for (let i = 0; i < pp.count; i++) { const v = attrVal(pp, i, 1); if (v > rawMaxY) rawMaxY = v; }
    }
    const modelHeight = rawMaxY / POSDIV;
    const transY = modelHeight / 2 + zoom * sinP + yoff;
    const transZ = zoom * cosP + yoff;
    const su = W / 36;
    const scale = 512 * su;
    const cxp = 16 * su, cyp = 16 * su;

    const out = new Float32Array(W * H * 4);          // premultiplied LINEAR rgba
    const zbuf = new Float32Array(W * H).fill(1e30);

    type Tri = { a: number, b: number, c: number, meshi: number, z: number, pri: number };
    const opaque: Tri[] = [], blend: Tri[] = [];
    const proj: any[] = [];

    // light-space basis for the cast-shadow map (model space, same space as the normals)
    let ux = -GZ, uy = 0, uz = GX;                      // L x (0,1,0), then normalized
    const ul = Math.hypot(ux, uy, uz) || 1; ux /= ul; uz /= ul;
    const vx2 = GY * uz - GZ * uy, vy2 = GZ * ux - GX * uz, vz2 = GX * uy - GY * ux;

    for (let mi = 0; mi < meshes.length; mi++) {
        const m = meshes[mi];
        const mat = await materialFor(scene, engine, m.materialId, texCache);
        const pos = m.attributes.pos, col = m.attributes.color, uv = m.attributes.texuvs;
        const nrm = m.attributes.normals;
        const vcount = pos.count;
        const sx = new Float32Array(vcount), sy = new Float32Array(vcount), sz = new Float32Array(vcount);
        const vcos = new Float32Array(vcount);          // per-vertex dot(N, L) for the classic light
        const lu = new Float32Array(vcount), lv = new Float32Array(vcount), ld = new Float32Array(vcount);
        const hslAttr = ICON_HSL_LIGHT ? m.attributes.colorHsl : undefined;
        // palette-space lighting: per-vertex LIT and SHADOWED colours (method2608 on the
        // packed lightness, then the Jagex palette). tLit/tSh are the linear lightness
        // fractions the texel needs (classic textured faces use bound2to126(t) linearly).
        const litC = hslAttr ? new Float32Array(vcount * 3) : null;
        const shC = hslAttr ? new Float32Array(vcount * 3) : null;
        const tLit = new Float32Array(vcount), tSh = new Float32Array(vcount);
        for (let i = 0; i < vcount; i++) {
            let x = attrVal(pos, i, 0) / POSDIV;
            let y = -attrVal(pos, i, 1) / POSDIV;
            let z = attrVal(pos, i, 2) / POSDIV;
            let cos = 0;
            if (nrm) {
                const nx = attrVal(nrm, i, 0), ny = -attrVal(nrm, i, 1), nz = attrVal(nrm, i, 2);
                const len = Math.hypot(nx, ny, nz) || 1;
                cos = (nx * GX + ny * GY + nz * GZ) / len;
            }
            vcos[i] = cos;
            let tl = cl.amb + cl.k * cos;
            tl = tl < 2 ? 2 : tl > 126 ? 126 : tl;
            let ts = cl.amb + cl.k * Math.min(0, cos);  // shadow removes only the positive sun term
            ts = ts < 2 ? 2 : ts > 126 ? 126 : ts;
            tLit[i] = tl; tSh[i] = ts;
            if (hslAttr) {
                const hsl = hslAttr.getX(i);
                const cl3 = litPackedRGB(hsl, tl | 0);
                const cs3 = litPackedRGB(hsl, ts | 0);
                litC![i * 3] = cl3[0]; litC![i * 3 + 1] = cl3[1]; litC![i * 3 + 2] = cl3[2];
                shC![i * 3] = cs3[0]; shC![i * 3 + 1] = cs3[1]; shC![i * 3 + 2] = cs3[2];
            }
            lu[i] = x * ux + y * uy + z * uz;           // light-space coords for the shadow map
            lv[i] = x * vx2 + y * vy2 + z * vz2;
            ld[i] = x * GX + y * GY + z * GZ;
            if (roll !== 0) { const tv = y * cosR - x * sinR; x = y * sinR + x * cosR; y = tv; }
            const x1 = z * sinY + x * cosY, z1 = z * cosY - x * sinY;
            const xt = x1 + xoff, yt = y + transY, zt = z1 + transZ;
            const y3 = yt * cosP - zt * sinP, z3 = yt * sinP + zt * cosP;
            sz[i] = z3;
            sx[i] = cxp + (xt * scale) / z3;
            sy[i] = cyp + (y3 * scale) / z3;
        }
        proj[mi] = { mat, sx, sy, sz, vcos, lu, lv, ld, col, uv, litC, shC, tLit, tSh, hasA: !!col && col.itemSize >= 4 };
        if (process.env.ICONDEBUG) console.error("  fwd mesh", mi, "mat", m.materialId, "pri", m.priority | 0, "unkint", ((m as any).unkint >>> 0).toString(16), "tris", (m.indices.count / 3) | 0, "mode", mat.mode);
        const idx = m.indices;
        for (let i = 0; i + 2 < idx.count; i += 3) {
            const a = idx.array[i], b = idx.array[i + 1], c = idx.array[i + 2];
            if (sz[a] < 50 || sz[b] < 50 || sz[c] < 50) continue;
            // face is translucent when its material blends or any of its vertices carries
            // partial alpha; fully invisible faces (all alpha 0) are dropped outright
            let translucent = mat.mode === "blend";
            if (proj[mi].hasA) {
                const aa = attrVal(col, a, 3), ab = attrVal(col, b, 3), ac = attrVal(col, c, 3);
                if (aa <= 0 && ab <= 0 && ac <= 0) continue;
                if (aa < 1 || ab < 1 || ac < 1) translucent = true;
            }
            const t: Tri = { a, b, c, meshi: mi, z: (sz[a] + sz[b] + sz[c]) / 3, pri: m.priority | 0 };
            (translucent ? blend : opaque).push(t);
        }
    }

    // ---- cast-shadow map: opaque geometry rasterized in light space, keeping the depth
    // closest to the light. The engine's icon bake runs with sun shadows; the ingot bars'
    // top-face shadow is a cast shadow, unreachable by any per-vertex lighting term.
    const SM = ICON_SM_RES;
    let sminU = 1e30, smaxU = -1e30, sminV = 1e30, smaxV = -1e30;
    if (ICON_SHADOWMAP) {
        for (const P of proj) {
            for (let i = 0; i < P.lu.length; i++) {
                if (P.lu[i] < sminU) sminU = P.lu[i];
                if (P.lu[i] > smaxU) smaxU = P.lu[i];
                if (P.lv[i] < sminV) sminV = P.lv[i];
                if (P.lv[i] > smaxV) smaxV = P.lv[i];
            }
        }
    }
    const smSpanU = Math.max(1e-6, smaxU - sminU), smSpanV = Math.max(1e-6, smaxV - sminV);
    const smx = (u: number) => (u - sminU) / smSpanU * (SM - 1);
    const smy = (v: number) => (v - sminV) / smSpanV * (SM - 1);
    const smbuf = new Float32Array(SM * SM).fill(-1e30);
    if (ICON_SHADOWMAP) {
        for (const t of opaque) {
            const P = proj[t.meshi];
            const a = t.a, b = t.b, c = t.c;
            const ax = smx(P.lu[a]), ay = smy(P.lv[a]), bx = smx(P.lu[b]), by = smy(P.lv[b]),
                cx = smx(P.lu[c]), cy = smy(P.lv[c]);
            const area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
            if (area === 0) continue;
            const inv = 1 / area;
            const minX = Math.max(0, Math.floor(Math.min(ax, bx, cx)));
            const maxX = Math.min(SM - 1, Math.ceil(Math.max(ax, bx, cx)));
            const minY = Math.max(0, Math.floor(Math.min(ay, by, cy)));
            const maxY = Math.min(SM - 1, Math.ceil(Math.max(ay, by, cy)));
            for (let py = minY; py <= maxY; py++) {
                for (let px = minX; px <= maxX; px++) {
                    const w0 = ((bx - ax) * (py + 0.5 - ay) - (by - ay) * (px + 0.5 - ax)) * inv;
                    const w1 = ((cx - bx) * (py + 0.5 - by) - (cy - by) * (px + 0.5 - bx)) * inv;
                    const w2 = 1 - w0 - w1;
                    if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                    const d = w1 * P.ld[a] + w2 * P.ld[b] + w0 * P.ld[c];
                    const pi = py * SM + px;
                    if (d > smbuf[pi]) smbuf[pi] = d;
                }
            }
        }
    }
    // 2x2 PCF: fraction of taps that see the light
    const shadowAt = (u: number, v: number, d: number): number => {
        if (!ICON_SHADOWMAP) return 1;
        const x = smx(u), y = smy(v);
        let litn = 0, n = 0;
        for (let oy = 0; oy <= 1; oy++) {
            for (let ox = 0; ox <= 1; ox++) {
                const xi = Math.min(SM - 1, Math.max(0, Math.round(x - 0.5 + ox)));
                const yi = Math.min(SM - 1, Math.max(0, Math.round(y - 0.5 + oy)));
                n++;
                if (d >= smbuf[yi * SM + xi] - ICON_SM_BIAS) litn++;
            }
        }
        return litn / n;
    };

    // one triangle raster, shared by both passes. write=true -> z-write (opaque pass)
    const drawTri = (t: Tri, write: boolean) => {
        const P = proj[t.meshi];
        const { mat, sx, sy, sz, vcos, lu, lv, ld, col, uv, litC, shC, tLit, tSh } = P;
        const a = t.a, b = t.b, c = t.c;
        const ax = sx[a], ay = sy[a], bx = sx[b], by = sy[b], cx2 = sx[c], cy2 = sy[c];
        const area = (bx - ax) * (cy2 - ay) - (by - ay) * (cx2 - ax);
        if (area === 0) return;
        const minX = Math.max(0, Math.floor(Math.min(ax, bx, cx2)));
        const maxX = Math.min(W - 1, Math.ceil(Math.max(ax, bx, cx2)));
        const minY = Math.max(0, Math.floor(Math.min(ay, by, cy2)));
        const maxY = Math.min(H - 1, Math.ceil(Math.max(ay, by, cy2)));
        if (minX > maxX || minY > maxY) return;
        // per-triangle mip level: texels covered per screen pixel (the resolve box filter
        // over the supersample grid supplies the trilinear-ish smoothing)
        let mip: ImageData | null = mat.tex;
        if (mat.tex && uv && mat.texMips.length > 1) {
            const ua = attrVal(uv, a, 0), va2 = attrVal(uv, a, 1);
            const ub2 = attrVal(uv, b, 0), vb3 = attrVal(uv, b, 1);
            const uc2 = attrVal(uv, c, 0), vc3 = attrVal(uv, c, 1);
            const texArea = Math.abs((ub2 - ua) * (vc3 - va2) - (vb3 - va2) * (uc2 - ua))
                * mat.tex.width * mat.tex.height;
            const scrArea = Math.abs(area);
            if (texArea > 0 && scrArea > 0) {
                const lod = Math.max(0, Math.min(mat.texMips.length - 1,
                    Math.round(0.5 * Math.log2(texArea / scrArea))));
                mip = mat.texMips[lod];
            }
        }
        const texel: [number, number, number, number] = [1, 1, 1, 1];
        const inv = 1 / area;
        for (let py = minY; py <= maxY; py++) {
            for (let px = minX; px <= maxX; px++) {
                const w0 = ((bx - ax) * (py + 0.5 - ay) - (by - ay) * (px + 0.5 - ax)) * inv;
                const w1 = ((cx2 - bx) * (py + 0.5 - by) - (cy2 - by) * (px + 0.5 - bx)) * inv;
                const w2 = 1 - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                const wa = w1, wb = w2, wc = w0;
                const z = wa * sz[a] + wb * sz[b] + wc * sz[c];
                const pi = py * W + px;
                // opaque: z-buffered, but the (priority, depth) paint order lets a
                // same-depth later face overwrite -- coplanar decals resolve by priority.
                // blend faces z-test against the closest opaque surface.
                if (z > zbuf[pi]) continue;
                let va = 1;
                if (col && col.itemSize >= 4) va = wa * attrVal(col, a, 3) + wb * attrVal(col, b, 3) + wc * attrVal(col, c, 3);
                if (va <= 0.01) continue;
                // shadow factor first: with palette-space lighting the light lives in the
                // per-vertex lit/shadowed colour pair, lerped by it
                let sfac = 1;
                if (ICON_SHADOWMAP) {
                    const cosl0 = wa * vcos[a] + wb * vcos[b] + wc * vcos[c];
                    if (cosl0 > 0) sfac = shadowAt(
                        wa * lu[a] + wb * lu[b] + wc * lu[c],
                        wa * lv[a] + wb * lv[b] + wc * lv[c],
                        wa * ld[a] + wb * ld[b] + wc * ld[c]);
                }
                const tmix = (wa * tSh[a] + wb * tSh[b] + wc * tSh[c]) * (1 - sfac)
                    + (wa * tLit[a] + wb * tLit[b] + wc * tLit[c]) * sfac;
                const lightLin = tmix / 128;                       // classic textured-face light (linear)
                const lightPow = Math.pow(lightLin, 0.6);          // palette-curve equivalent for RGB targets
                let r = 1, g = 1, bl = 1;
                if (litC) {
                    for (let ch = 0; ch < 3; ch++) {
                        const lit = wa * litC[a * 3 + ch] + wb * litC[b * 3 + ch] + wc * litC[c * 3 + ch];
                        const sh = wa * shC[a * 3 + ch] + wb * shC[b * 3 + ch] + wc * shC[c * 3 + ch];
                        const v = sh * (1 - sfac) + lit * sfac;
                        if (ch === 0) r = v; else if (ch === 1) g = v; else bl = v;
                    }
                } else if (col) {
                    // no packed HSL on this mesh: old RGB-space light multiply
                    r = (wa * attrVal(col, a, 0) + wb * attrVal(col, b, 0) + wc * attrVal(col, c, 0)) * lightPow;
                    g = (wa * attrVal(col, a, 1) + wb * attrVal(col, b, 1) + wc * attrVal(col, c, 1)) * lightPow;
                    bl = (wa * attrVal(col, a, 2) + wb * attrVal(col, b, 2) + wc * attrVal(col, c, 2)) * lightPow;
                } else {
                    r = g = bl = lightPow;
                }
                const f = ICON_FRAC_INV ? 1 - mat.frac : mat.frac;
                const textured = !!(mat.tex && uv && mip);
                // white-target materials treat frac as the whiten-bit strength: it selects
                // the RAW texel (texture supplies the colour, AGS-style). base-target
                // materials keep the legacy premix toward baseColor over the raw texel
                // (Support cape). Untextured faces premix as always.
                const whiteT = mat.base[0] === 1 && mat.base[1] === 1 && mat.base[2] === 1;
                if (!textured || !whiteT || ICON_TEXNORM !== 2) {
                    r = r * (1 - f) + mat.base[0] * lightPow * f;
                    g = g * (1 - f) + mat.base[1] * lightPow * f;
                    bl = bl * (1 - f) + mat.base[2] * lightPow * f;
                }
                let metal = 0;
                let tr = 1, tg = 1, tb = 1, ta = 1;
                if (textured) {
                    const u = wa * attrVal(uv, a, 0) + wb * attrVal(uv, b, 0) + wc * attrVal(uv, c, 0);
                    const v = wa * attrVal(uv, a, 1) + wb * attrVal(uv, b, 1) + wc * attrVal(uv, c, 1);
                    sampleBilinear(mip!, u, v, mat.wrapU, mat.wrapV, texel);
                    ta = texel[3];
                    if (mat.mode !== "blend" && ta < mat.cutoff) continue;   // the material's REAL threshold
                    tr = texel[0]; tg = texel[1]; tb = texel[2];
                    if (mat.compound) {
                        const uw = sampleWrap(u, mat.wrapU), vw = sampleWrap(v, mat.wrapV);
                        const cx3 = Math.min(mat.compound.width - 1, Math.max(0, Math.floor(uw * mat.compound.width)));
                        const cy3 = Math.min(mat.compound.height - 1, Math.max(0, Math.floor(vw * mat.compound.height)));
                        const ci = (cy3 * mat.compound.width + cx3) * 4;
                        metal = (mat.compound.data[ci + ICON_METAL_CH] / 255) * ICON_METAL_SCALE;
                    }
                }
                // Tinted faces use the texture as DETAIL (texel normalized by the texture's
                // own measured average; the vertex colour carries the hue -- Ferocious sigil
                // 40146: red vcols + teal texture render RED in game, not mud). The RAW
                // texel feeds only the whitened/metal term, mirroring the shader's
                // albedo = mix(texel*vcol, texel, metalness).
                let rawW = metal;
                let tnr = tr, tng = tg, tnb = tb;
                if (textured && ICON_TEXNORM === 2 && whiteT) {
                    rawW = Math.max(f, metal);
                    tnr = Math.min(2, tr / mat.texMean[0]);
                    tng = Math.min(2, tg / mat.texMean[1]);
                    tnb = Math.min(2, tb / mat.texMean[2]);
                } else if (textured && ICON_TEXNORM === 1) {
                    tnr = Math.min(2, tr / Math.max(0.02, mat.base[0]));
                    tng = Math.min(2, tg / Math.max(0.02, mat.base[1]));
                    tnb = Math.min(2, tb / Math.max(0.02, mat.base[2]));
                }

                const alpha = (mat.mode === "blend" ? va * ta : va);
                const o = pi * 4;
                // sRGB compose then square to linear (same convention as the gbuf path)
                // tinted term x normalized texel, raw/whitened term by rawW. In mode 1 the
                // "raw" term is the normalized texel too, which reduces to the proven
                // (vcol*(1-m)+m) x texelNorm form; mode 2 feeds the true raw texel.
                // the tint term is already lit (palette-space); the raw/whitened texel
                // term gets the classic linear texture light
                const rwr = ICON_TEXNORM === 2 ? tr : tnr,
                    rwg = ICON_TEXNORM === 2 ? tg : tng,
                    rwb = ICON_TEXNORM === 2 ? tb : tnb;
                let lr = (r * tnr * (1 - rawW) + rwr * lightLin * rawW) ** 2;
                let lg = (g * tng * (1 - rawW) + rwg * lightLin * rawW) ** 2;
                let lb = (bl * tnb * (1 - rawW) + rwb * lightLin * rawW) ** 2;
                // ADDITIVE environment/specular term (the shader ADDS amb.Specular after the
                // albedo multiply; it is what our multiplicative light structurally cannot
                // produce -- see the plateau note in memory). Two parts, both in LINEAR space:
                //   metal reflection: metals have no diffuse, their brightness IS this term,
                //     tinted by the surface albedo so a red metal reflects red-ish;
                //   dielectric ambient: a small flat lift on everything (the icon studio is
                //     not a black void). Constants fit on the trusted set (fit not yet run;
                //     defaults are a first guess). All behind ICON_ENV so it is opt-in.
                if (ICON_ENV) {
                    // metal reflection is tinted by the surface's OWN albedo (a red metal
                    // reflects red), times a slightly cool studio env colour; dielectric
                    // ambient is a small flat lift. albedo here is the linear lit albedo.
                    const aR = (r * tnr) ** 2, aG = (g * tng) ** 2, aB = (bl * tnb) ** 2;
                    const em = metal * ICON_ENV_METAL;
                    lr += em * ICON_ENV_COLR * aR + ICON_ENV_AMB * aR;
                    lg += em * ICON_ENV_COLG * aG + ICON_ENV_AMB * aG;
                    lb += em * ICON_ENV_COLB * aB + ICON_ENV_AMB * aB;
                }
                if (write && alpha >= 1) {
                    if (z < zbuf[pi]) zbuf[pi] = z;   // closest opaque depth, for the blend z-test
                    out[o] = lr; out[o + 1] = lg; out[o + 2] = lb; out[o + 3] = 1;
                } else {
                    // composite over what is already there (premultiplied linear)
                    out[o] = lr * alpha + out[o] * (1 - alpha);
                    out[o + 1] = lg * alpha + out[o + 1] * (1 - alpha);
                    out[o + 2] = lb * alpha + out[o + 2] * (1 - alpha);
                    out[o + 3] = alpha + out[o + 3] * (1 - alpha);
                }
            }
        }
    };

    // The classic item rasterizer has NO z-buffer: faces paint in (priority asc, depth
    // far-to-near) order, higher priority on top regardless of depth. drawTri's opaque
    // mode writes the closest depth per pixel (for the blend pass's z-test) but does not
    // z-reject, so this order IS the visibility.
    opaque.sort((p, q) => (p.pri - q.pri) || (q.z - p.z));
    for (const t of opaque) drawTri(t, true);
    blend.sort((p, q) => q.z - p.z);                   // far to near
    for (const t of blend) drawTri(t, false);
    return out;
}

// Resolve the supersampled LINEAR buffer down to icon size, then encode. The engine renders
// into a linear render target and the sRGB conversion happens on store, so the average must be
// taken in linear space. Encode is LinearToSRGBRunescape = sqrt (gamma 2.0).
function resolve(src: Float32Array, W: number, Hh: number, ss: number): Uint8ClampedArray {
    const out = new Uint8ClampedArray(W * Hh * 4);
    for (let y = 0; y < Hh; y++) {
        for (let x = 0; x < W; x++) {
            let r = 0, g = 0, b = 0, a = 0;
            for (let dy = 0; dy < ss; dy++) {
                for (let dx = 0; dx < ss; dx++) {
                    const si = ((y * ss + dy) * W * ss + x * ss + dx) * 4;
                    // inputs are PREMULTIPLIED linear: average as-is (engine convention;
                    // the render target holds premultiplied colour, and the outline pass
                    // clamps alpha to 1 without unpremultiplying -- that is how a 20%-alpha
                    // grey disc shows up as a solid dark disc in the game's icon)
                    r += src[si]; g += src[si + 1]; b += src[si + 2];
                    a += src[si + 3];
                }
            }
            const oi = (y * W + x) * 4;
            const n = ss * ss;
            out[oi] = Math.sqrt(Math.max(0, r / n)) * 255;
            out[oi + 1] = Math.sqrt(Math.max(0, g / n)) * 255;
            out[oi + 2] = Math.sqrt(Math.max(0, b / n)) * 255;
            out[oi + 3] = (a / n) * 255;
        }
    }
    return out;
}

// Coverage per final pixel: fraction of supersamples that pass the engine's alpha > 0.02
// visibility rule. The engine tests at native res, where a pixel is either covered by
// geometry or not; testing 0.02 on the AVERAGED alpha instead bloated every silhouette by
// the supersample fringe (IoU 0.956 -> 0.891). Binarize per sample, then demand half-pixel
// coverage: translucent surfaces (all their samples pass 0.02) stay visible.
function coverage(src: Float32Array, W: number, Hh: number, ss: number): Float32Array {
    const cov = new Float32Array(W * Hh);
    for (let y = 0; y < Hh; y++) {
        for (let x = 0; x < W; x++) {
            let cnt = 0;
            for (let dy = 0; dy < ss; dy++) {
                for (let dx = 0; dx < ss; dx++) {
                    if (src[((y * ss + dy) * W * ss + x * ss + dx) * 4 + 3] > 0.02) cnt++;
                }
            }
            cov[y * W + x] = cnt / (ss * ss);
        }
    }
    return cov;
}

// OutlineAndShadow.fs: visible(px) = px.a > 0.02, the CLAMP path emits vec4(rgb, 1) for
// visible pixels and vec4(0) otherwise (hence the reference's 1-bit alpha), and any pixel
// with a visible 4-neighbour is painted uOutlineAndShadowColour. Our supersampled coverage is
// not the engine's alpha, so the threshold is tunable; 0.5 is the coverage-equivalent.
// The drop shadow is OFF by default: the reference sprite carries none. ICON_SHADOW=1
// restores it (the classic engine's drawShadow, offset up-left, for callers that want it).
const ICON_SHADOW = process.env.ICON_SHADOW === "1";

function outlineShadow(img: Uint8ClampedArray, cov: Float32Array, W: number, Hh: number): Uint8ClampedArray {
    // 1. binarize by geometric COVERAGE -> crisp silhouette (colour stays premultiplied,
    //    which is how the engine's CLAMP path shows translucent surfaces as solid darks)
    const src = new Uint8ClampedArray(img);
    for (let i = 0; i < W * Hh; i++) src[i * 4 + 3] = cov[i] >= ICON_ALPHA_THRESH ? 255 : 0;

    const out = new Uint8ClampedArray(src);
    const solid = (x: number, y: number) => x >= 0 && y >= 0 && x < W && y < Hh && src[(y * W + x) * 4 + 3] === 255;
    for (let y = 0; y < Hh; y++) {
        for (let x = 0; x < W; x++) {
            const i = (y * W + x) * 4;
            if (src[i + 3] === 255) continue;
            if (ICON_SHADOW && solid(x - 1, y - 1)) {
                out[i] = 0; out[i + 1] = 0; out[i + 2] = 0; out[i + 3] = 160;
            }
            if (solid(x - 1, y) || solid(x + 1, y) || solid(x, y - 1) || solid(x, y + 1)) {
                out[i] = 0; out[i + 1] = 0; out[i + 2] = 0; out[i + 3] = 255;   // outline wins
            }
        }
    }
    return out;
}

function toPng(img: Uint8ClampedArray, W: number, Hh: number): Buffer {
    const cv: any = napi.createCanvas(W, Hh);
    const ctx = cv.getContext("2d");
    ctx.putImageData(new napi.ImageData(img, W, Hh), 0, 0);
    return cv.toBuffer("image/png");
}

// composite b (scaled) centred over a, both W x H linear-rgba -- used for noted items
function compositeNoted(note: Float32Array, item: Float32Array, W: number, Hh: number, factor: number): Float32Array {
    const out = new Float32Array(note);
    const S = Math.round(W * factor), off = Math.floor((W - S) / 2);
    const SV = Math.round(Hh * factor), offv = Math.floor((Hh - SV) / 2);
    for (let y = 0; y < SV; y++) {
        for (let x = 0; x < S; x++) {
            const sx2 = Math.min(W - 1, Math.floor(x / factor)), sy2 = Math.min(Hh - 1, Math.floor(y / factor));
            const si = (sy2 * W + sx2) * 4, di = ((y + offv) * W + x + off) * 4;
            const a = item[si + 3];
            if (a <= 0.05) continue;
            // inputs are premultiplied: composite is src + dst*(1-srcA)
            out[di] = item[si] + out[di] * (1 - a);
            out[di + 1] = item[si + 1] + out[di + 1] * (1 - a);
            out[di + 2] = item[si + 2] + out[di + 2] * (1 - a);
            out[di + 3] = a + out[di + 3] * (1 - a);
        }
    }
    return out;
}

(async () => {
    const args: Record<string, string> = {};
    for (const a of process.argv.slice(2)) {
        const eq = a.indexOf("=");
        if (eq > 0) args[a.slice(0, eq)] = a.slice(eq + 1); else args[a] = "1";
    }
    const outdir = args.out || "icons_out";
    const size = +(args.size || 36);
    const ss = +(args.ss || 4);
    fs.mkdirSync(outdir, { recursive: true });

    const src = new GameCacheLoader(CACHE, false);
    const engine = await EngineCache.create(src);
    const scene = await ThreejsSceneCache.create(engine);
    const texCache = new Map<number, ImageData | null>();

    // ICON_GBUF=<dir>: write the raw per-pixel G-buffer instead of a PNG, so the offline light
    // fit (fit_icon_light.py) evaluates exactly the shading this renderer will apply.
    const gbufDir = ICON_GBUF;
    if (gbufDir) fs.mkdirSync(gbufDir, { recursive: true });

    const renderIcon = async (itemid: number): Promise<Buffer | null> => {
        let def: any;
        try { def = parse.item.read(await engine.getGameFile("items", itemid), engine.rawsource); }
        catch (e) { if (process.env.ICONDEBUG) console.error("id", itemid, "def ERROR:", (e as Error)?.message); return null; }
        if (process.env.ICONDEBUG) console.error("id", itemid, "name", def.name, "baseModel", def.baseModel, "zoom", def.model_zoom, "rot", def.rotation_0, def.rotation_1);
        const W = size * ss;
        const hsize = Math.round(size * 32 / 36);
        const H = hsize * ss;
        // noted: note template background + parent icon composited at 82% (game look).
        // 949 moved the note links to u24 ops (noteData_big/noteTemplate_big); take either.
        const noteData = def.noteData_big ?? def.noteData;
        const noteTemplate = def.noteTemplate_big ?? def.noteTemplate;
        let lin: Float32Array | null = null;
        if (!def.baseModel && noteTemplate && noteData) {
            let tpl: any, parent: any;
            try {
                tpl = parse.item.read(await engine.getGameFile("items", noteTemplate), engine.rawsource);
                parent = parse.item.read(await engine.getGameFile("items", noteData), engine.rawsource);
            } catch (e) { return null; }
            if (ICON_PBR) {
                const noteGb = await renderModelGBuf(scene, engine, tpl, W, H, texCache);
                const itemGb = await renderModelGBuf(scene, engine, parent, W, H, texCache);
                if (!noteGb) return null;
                const noteImg = shadeGBuffer(noteGb, W, H, classicParams(tpl));
                lin = itemGb ? compositeNoted(noteImg, shadeGBuffer(itemGb, W, H, classicParams(parent)), W, H, 0.82) : noteImg;
            } else {
                const noteImg = await renderModelForward(scene, engine, tpl, W, H, texCache);
                const itemImg = await renderModelForward(scene, engine, parent, W, H, texCache);
                if (!noteImg) return null;
                lin = itemImg ? compositeNoted(noteImg, itemImg, W, H, 0.82) : noteImg;
            }
        } else if (gbufDir || ICON_PBR) {
            // the deferred G-buffer path stays for the offline fitters + the PBR equation
            const gb = await renderModelGBuf(scene, engine, def, W, H, texCache);
            if (!gb) return null;
            if (gbufDir) {
                fs.writeFileSync(path.join(gbufDir, itemid + ".gbuf"), Buffer.from(gb.buffer, gb.byteOffset, gb.byteLength));
                // The model->camera rotation, so the fit can also test a camera-fixed light.
                fs.writeFileSync(path.join(gbufDir, itemid + ".json"), JSON.stringify({
                    id: itemid, name: def.name, W, H, ss, size, hsize, gb: GB,
                    roll: ((def.rotation_2 || 0) / 2048) * Math.PI * 2,
                    yaw: ((def.rotation_1 || 0) / 2048) * Math.PI * 2,
                    pitch: ((def.rotation_0 || 0) / 2048) * Math.PI * 2,
                }));
                return null;   // dump only
            }
            lin = shadeGBuffer(gb, W, H, classicParams(def));
        } else {
            lin = await renderModelForward(scene, engine, def, W, H, texCache);
            if (!lin) return null;
        }
        if (!lin) return null;
        return toPng(outlineShadow(resolve(lin, size, hsize, ss), coverage(lin, size, hsize, ss), size, hsize), size, hsize);
    };

    let ids: number[] = [];
    if (args.all) {
        const start = +(args.start || 0);
        for (let i = start; i < 200000; i++) ids.push(i);   // getGameFile throws past the end; loop breaks on a long miss-run
    } else {
        ids = (args.ids || "").split(",").map(Number).filter(n => n >= 0);
    }
    let done = 0, missrun = 0;
    for (const id of ids) {
        let png: Buffer | null = null;
        try { png = await renderIcon(id); }
        catch (e) { png = null; if (process.env.ICONDEBUG) console.error("id", id, "ERROR:", (e as Error)?.stack || e); }
        if (png) {
            fs.writeFileSync(path.join(outdir, id + ".png"), png);
            done++; missrun = 0;
        } else if (args.all && ++missrun > 2000) break;      // ran off the end of the item index
        if (args.all && id % 1000 === 0) console.log("at", id, "rendered", done);
    }
    console.log("rendered", done, "icons ->", outdir);
})();
