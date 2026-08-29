// RuneToolsX panel: Compass clue solver.
// Spliced inline into client.html; IIFE (window exports + registerTab; see the RTX registry in client.html).
(function () {

  // Needle rotation read live via bridge().compassHeading (group 996 / comp 5 / node+0x180). It
  // encodes the world bearing to the dig spot, NOT linearly: 4x-per-revolution (~5deg) distortion
  // from the isometric camera. Calibrated model:
  //     raw = COMPASS_S*bearingN + c_clue + COMPASS_H4A*sin(4*bearingN) + COMPASS_H4B*cos(4*bearingN)
  // bearingN = bearing from north, CW. c_clue is a per-clue north-zero offset, cancelled by the
  // solver's mean-subtraction. Spots are scored by mean-removed RMS residual.
  const COMPASS_S = 5.7361;         // raw per degree of bearing (true scale; FULL = S*360)
  const COMPASS_H4A = -28.984;      // 4th-harmonic sin coeff (raw): the needle's 90deg-period distortion
  const COMPASS_H4B = -0.470;       // 4th-harmonic cos coeff (raw)
  const COMPASS_FULL = 2065;        // raw per full circle (wrap period, = round(S*360))
  const COMPASS_FIT_OK = 7;         // raw RMS ceiling for a confident fit (true spot RMS is ~quantization)
  // Runner-up separation (raw RMS) required to COMMIT, by reading count: relaxed as captures
  // accumulate, tight enough that a wrong dig effectively never happens.
  function compassMinGap(n) { return n >= 4 ? 3 : n === 3 ? 4 : 6; }
  // Needle model: forward (bearingN -> raw, sans per-clue offset) and inverse (the 4H term is
  // small, so the fixed-point iteration converges fast).
  function compassModel(b) { const r = 4 * b * Math.PI / 180; return COMPASS_S * b + COMPASS_H4A * Math.sin(r) + COMPASS_H4B * Math.cos(r); }
  function compassInvert(raw) {
    let b = raw / COMPASS_S;
    for (let i = 0; i < 5; i++) { const r = 4 * b * Math.PI / 180; b = (raw - COMPASS_H4A * Math.sin(r) - COMPASS_H4B * Math.cos(r)) / COMPASS_S; }
    b %= 360; if (b < 0) b += 360; return b;
  }
  const COMPASS_CAP_DIST = 5;       // min tile gap between auto-logged readings
  const COMPASS_SPOTS_G = [
    [2413,2818],[2398,2862],[2476,2845],[2493,2875],[2546,2872],[2547,2826],[2617,2876],[2598,2838],[2445,2917],[2458,2893],[2541,2926],[2511,2936],
    [2578,2930],[2589,2898],[2482,2977],[2490,2949],[2511,2980],[2535,2958],[2590,2994],[2592,2956],[2640,2987],[2638,2948],[2330,3057],[2355,3038],
    [2393,3046],[2418,3040],[2481,3011],[2439,3057],[2564,3041],[2590,3063],[2629,3123],[2671,3109],[2579,3130],[2610,3076],[2519,3115],[2559,3102],
    [2449,3110],[2486,3134],[2433,3144],[2468,3177],[2498,3175],[2557,3164],[2619,3153],[2595,3192],[2651,3148],[2627,3172],[2457,3221],[2478,3254],
    [2543,3211],[2502,3249],[2583,3223],[2592,3251],[2634,3208],[2646,3251],[2492,3270],[2451,3283],[2506,3276],[2582,3266],[2582,3314],[2665,3266],
    [2650,3302],[2723,3277],[2700,3311],[2381,3388],[2410,3388],[2457,3331],[2485,3377],[2541,3389],[2513,3351],[2581,3340],[2576,3370],[2651,3351],
    [2662,3338],[2724,3360],[2739,3388],[2359,3448],[2392,3398],[2410,3419],[2438,3398],[2470,3404],[2522,3429],[2531,3405],[2583,3449],[2565,3395],
    [2677,3400],[2657,3451],[2726,3399],[2705,3418],[2765,3445],[2797,3428],[2835,3423],[2834,3451],[2926,3402],[2908,3431],[2950,3406],[2995,3429],
    [3055,3396],[3027,3454],[3089,3409],[3134,3423],[3158,3439],[3176,3400],[3228,3409],[3233,3446],[3276,3444],[3318,3426],[3390,3421],[3362,3403],
    [3411,3410],[3439,3454],[3473,3401],[3461,3432],[3538,3449],[3563,3408],[2356,3498],[2325,3510],[2394,3513],[2401,3458],[2489,3487],[2457,3511],
    [2504,3484],[2550,3475],[2588,3506],[2609,3470],[2649,3473],[2683,3491],[2707,3475],[2733,3502],[2777,3501],[2808,3474],[2832,3473],[2859,3508],
    [2937,3511],[2900,3501],[2956,3505],[2983,3473],[3035,3490],[3021,3470],[3103,3488],[3127,3469],[3145,3510],[3190,3464],[3254,3505],[3260,3460],
    [3317,3507],[3288,3460],[3329,3518],[3373,3483],[3420,3479],[3452,3476],[3487,3492],[3516,3473],[3529,3501],[3576,3481],[3598,3509],[3619,3485],
    [3649,3475],[3678,3518],[2336,3540],[2351,3572],[2418,3525],[2394,3574],[2491,3529],[2542,3562],[2677,3539],[2653,3578],[2699,3537],[2721,3577],
    [2757,3548],[2757,3581],[2878,3529],[2860,3575],[2928,3573],[2919,3535],[2979,3546],[2986,3583],[3052,3548],[3030,3520],[3096,3534],[3125,3578],
    [3153,3545],[3184,3559],[3229,3541],[3255,3576],[3289,3528],[3292,3574],[3376,3567],[3339,3572],[3418,3544],[3432,3544],[3460,3553],[3510,3530],
    [3571,3534],[3547,3524],[3624,3524],[3596,3541],[3655,3537],[2262,3605],[2295,3639],[2332,3610],[2347,3594],[2392,3599],[2374,3630],[2509,3634],
    [2559,3619],[2606,3635],[2586,3605],[2684,3612],[2669,3637],[2708,3589],[2737,3626],[2779,3594],[2780,3635],[2999,3606],[2965,3632],[3059,3620],
    [3028,3632],[3092,3605],[3127,3597],[3179,3612],[3146,3642],[3254,3624],[3247,3597],[3304,3605],[3278,3615],[3338,3638],[3342,3591],[3433,3639],
    [3403,3640],[2309,3669],[2332,3659],[2612,3658],[2643,3679],[2680,3676],[2712,3653],[2696,3695],[2756,3651],[2972,3682],[2999,3671],[2956,3705],
    [2965,3740],[3088,3678],[3107,3653],[3187,3661],[3142,3656],[3220,3667],[3210,3695],[3280,3664],[3315,3704],[3353,3664],[3378,3699],[3437,3682],
    [3423,3706],[3511,3670],[3470,3710],[2666,3717],[2686,3727],[2725,3751],[2744,3735],[2975,3762],[2985,3715],[3060,3767],[3046,3752],[3104,3745],
    [3116,3769],[3153,3744],[3193,3739],[3213,3756],[3255,3740],[3342,3732],[3367,3766],[3432,3754],[3430,3717],[3506,3734],[2737,3796],[2185,3637],
    [2960,3824],[2999,3791],[3059,3833],[3051,3804],[3101,3783],[3128,3822],[3151,3795],[3193,3783],[3250,3821],[3208,3792],[3363,3807],[3349,3823],
    [2958,3858],[2990,3883],[3037,3870],[3055,3895],[3088,3891],[3127,3891],[3157,3865],[3195,3877],[3250,3843],[3252,3887],[3294,3865],[3317,3896],
    [3383,3893],[3336,3898],[2971,3929],[2990,3948],[3055,3915],[3060,3941],[3128,3913],[3110,3954],[3155,3924],[3181,3944],[3244,3907],[3242,3956],
    [3306,3947],[3275,3915],[3388,3930],[3429,3924],[2901,3356],[2915,3335],[2972,3342],[2976,3386],[3027,3365],[3050,3348],[3125,3371],[3081,3363],
    [3158,3376],[3140,3343],[3243,3378],[3240,3351],[3312,3376],[3304,3335],[3352,3381],[3358,3353],[3438,3349],[3408,3390],[3474,3385],[3485,3362],
    [2915,3302],[2917,3271],[2976,3274],[2976,3316],[3033,3286],[3066,3322],[3128,3310],[3129,3268],[3188,3317],[3146,3312],[3210,3305],[3242,3268],
    [3265,3300],[3309,3298],[3347,3273],[3349,3312],[3450,3276],[3422,3302],[3488,3270],[3478,3292],[3538,3311],[3582,3312],[2920,3249],[2932,3216],
    [2982,3203],[2997,3241],[3042,3251],[3019,3242],[3091,3258],[3122,3227],[3137,3212],[3135,3257],[3210,3205],[3263,3253],[3271,3204],[3323,3225],
    [3486,3245],[3519,3246],[2986,3191],[2997,3149],[3028,3171],[3011,3195],[3119,3167],[3083,3155],[3146,3170],[3195,3163],[3240,3161],[3235,3191],
    [3272,3156],[3293,3183],[3391,3180],[3350,3162],[3418,3164],[3394,3149],[3465,3140],[3210,3092],[3257,3117],[3304,3126],[3303,3077],[3378,3078],
    [3344,3112],[3429,3106],[3410,3079],[3483,3108],[3509,3088],[3171,3028],[3189,3047],[3235,3048],[3215,3020],[3303,3050],[3320,3013],[3355,3044],
    [3383,3019],[3419,3017],[3422,3046],[3468,3044],[3509,3025],[3170,2982],[3142,3003],[3225,2986],[3256,2953],[3289,2957],[3319,2990],[3376,2988],
    [3346,2958],[3404,2992],[3442,2955],[3170,2914],[3177,2888],[3245,2935],[3205,2895],[3296,2891],[3316,2930],[3361,2921],[3373,2898],[3423,2916],
    [3434,2888],[3218,2843],[3247,2866],[3290,2840],[3321,2848],[3390,2841],[3341,2836],[3376,2790],[3351,2781],[3401,2758],[3359,2753],[2703,3204],
    [2746,3232],[2772,3217],[2805,3204],[2743,3174],[2734,3144],[2802,3156],[2762,3172],[2843,3153],[2874,3173],[2912,3168],[2906,3141],[2763,3125],
    [2786,3075],[2846,3078],[2831,3119],[2912,3116],[2881,3073],[2761,3057],[2779,3023],[2869,3026],[2835,3025],[2930,3029],[2901,3031],[2791,2969],
    [2757,2949],[2916,2992],[2881,2968],[2970,2979],[2958,2996],[2804,2924],[2793,2893],[2858,2890],[2841,2934],[2920,2888],[2932,2933],[2960,2928],
    [2856,3455],[3082,3475],[2778,3583],[3265,3270],[3508,3673],[2864,3589],
  ];
  let compassReads = [];          // [{x,y,raw}] one per captured standing position (manual capture)
  let compassResult = null;
  let compassBusy = false;
  let compassLastRaw = -1;        // most recent needle reading, for the manual "Use this coordinate" button
  compassMapSig = null;       // signature of the last-drawn compass map (skip redundant terrain refetch)
  let compassNote = '';
  function compassBearingN(px, py, sx, sy) {       // bearing from north, clockwise, degrees [0,360)
    let a = Math.atan2(sy - py, sx - px) * 180 / Math.PI;   // 0=east, CCW
    let b = 90 - a; b %= 360; if (b < 0) b += 360; return b;
  }
  // Offset-corrected RMS residual: std-dev of per-reading implied north-zeros after removing
  // their mean (cancels any constant per-clue offset; RMS absorbs quantization). Wrap-safe vs
  // reading 0. n<2 -> 0: one reading cannot constrain a spot; the separation gap handles it.
  function compassErrOn(reads, spot) {
    const n = reads.length; if (!n) return 1e9; if (n === 1) return 0;
    const imp = reads.map(r => r.raw - compassModel(compassBearingN(r.x, r.y, spot[0], spot[1])));
    const ref = imp[0]; let sum = 0;
    const adj = imp.map(v => { let d = v - ref; while (d > COMPASS_FULL / 2) d -= COMPASS_FULL; while (d < -COMPASS_FULL / 2) d += COMPASS_FULL; return ref + d; });
    for (const v of adj) sum += v; const mean = sum / n;
    let s2 = 0; for (const v of adj) { const e = v - mean; s2 += e * e; }
    return Math.sqrt(s2 / n);
  }
  function compassErr(spot) { return compassErrOn(compassReads, spot); }
  // Evaluate after every capture: the lowest-RMS spot is the dig site IF it is a good fit AND
  // clearly better than the runner-up; never commit on an ambiguous near-collinear pair.
  function compassSolve() {
    if (compassReads.length < 2) return { state: 'need', cands: 0, best: compassReads.length ? COMPASS_SPOTS_G[0] : null, err: 1e9 };
    let best = COMPASS_SPOTS_G[0], bestErr = 1e9, secErr = 1e9;
    for (const s of COMPASS_SPOTS_G) {
      const e = compassErrOn(compassReads, s);
      if (e < bestErr) { secErr = bestErr; bestErr = e; best = s; }
      else if (e < secErr) { secErr = e; }
    }
    if (bestErr < COMPASS_FIT_OK && (secErr - bestErr) >= compassMinGap(compassReads.length))
      return { state: 'solved', spot: best, best: best, err: bestErr, cands: 1 };
    let near = 0; for (const s of COMPASS_SPOTS_G) if (compassErrOn(compassReads, s) < COMPASS_FIT_OK) near++;
    return { state: 'need', cands: near, best: best, err: bestErr };
  }
  // EXACT dig spot: varc 1323 stores the compass target packed (compassTarget bridge). Cross-
  // checked against the known spot list so a stale post-update memory offset is rejected and the
  // solver falls back to needle triangulation.
  function compassTargetSpot() {
    let s = ''; try { s = bridge().compassTarget(myPid()) || ''; } catch (e) {}
    if (!s) return null;
    const p = s.split(',').map(Number);
    if (p.length < 2 || !(p[0] > 0 && p[1] > 0)) return null;
    let best = null, bd = 1e9;                                  // snap to the nearest known gielinor spot
    for (const sp of COMPASS_SPOTS_G) { const d = Math.abs(sp[0] - p[0]) + Math.abs(sp[1] - p[1]); if (d < bd) { bd = d; best = sp; } }
    return bd <= 2 ? best : null;                               // garbage won't land within 2 tiles of a real spot
  }
  // ---- Tetracompass ------------------------------------------------------------------
  // A powered tetracompass drives the SAME engine field as a compass clue: varc 1323 holds its
  // dig tile packed (plane<<28)|(x<<14)|y, so the spot reads exactly, with no hint text and no
  // needle triangulation. Because one varc serves both, the TILE is what says which of the two
  // is live: a tetracompass site is never a compass-clue spot and vice versa, so the spot lists
  // are the discriminator. That doubles as the guard the compass path already gets -- a stale
  // post-update memory offset lands on a tile in neither list, and is dropped.
  // The site list is reference data, NOT the answer: the tile actually dug always comes from the
  // varc. The list only has to say which KIND of target is loaded, so an entry being a tile out
  // costs nothing but a missed match. A plain threshold cannot do that job -- tetra (2722, 3577)
  // and compass spot (2721, 3577) are one tile apart, so any tolerance wide enough to absorb a
  // stale entry also merges those two. Instead the lists COMPETE: whichever has the nearer entry
  // claims the target. Being a comparison rather than a cutoff, it splits the adjacent pair
  // correctly from either side AND tolerates drift, and where it cannot tell it returns nothing,
  // so an unrecognised tile goes unmarked rather than mismarked.
  const TETRA_SITES = [
    [2935,3489],[2946,3462],[2945,3274],[2896,3550],[2595,2932],[2623,3001],
    [2462,2897],[2573,2922],[2600,2934],[2553,2985],[2614,3624],[2713,3628],
    [2674,3710],[2729,3606],[2720,3468],[2704,3453],[2708,3449],[2722,3577],
    [2632,3489],[2764,3478],[2590,3263],[2680,3370],[2468,3278],[2557,3370],
    [2752,3394],[2482,3357],[2609,3383],[2554,3467],[2546,3522],[2534,3429],
    [2425,3187],[2460,3198],[2305,3544],[2322,3548],[2269,3537],[2363,3574],
    [2357,3551],[2365,3545],[2315,3548],[2450,3516],[2453,3554],[2358,3483],
    [2419,3395],[2516,3187],[2504,3205],[2578,3118],[2578,3139],[2587,3076],
    [2857,2970],[2779,3100],[2910,3075],[2705,3157],[2726,3223],[2835,2927],
    [3256,3195],[3142,3251],[3356,3396],[3200,3200],[3731,3164],[2196,3101],
    [2255,3119],[2207,3156],[2281,3190],[2893,3656],[2860,3662],[3162,3542],
    [3430,3649],[2961,3821],[3236,3665],[2992,3686],[2965,3746],
    [2606,2956],                                // Feldip Hills; read live from varc 1323
  ];
  const TETRA_POWERED = 49957;                  // Tetracompass (powered) -- confirmed in-game
  const TETRA_SNAP = 3;                         // furthest a target may sit from its listed site
  let tetraLast = null;
  // Held state rides on the clue panel's existing backpack scan (clueHeldInv, filled by
  // fetchClues) rather than a second container read: the tetracompass is only ever solved from
  // that same panel, so a separate poll would duplicate the round trip.
  const tetraHeld = () => typeof clueHeldInv !== 'undefined' && clueHeldInv.has(TETRA_POWERED);
  function tetraNearest(list, x, y) {
    let d = 1e9;
    for (const sp of list) { const e = Math.abs(sp[0] - x) + Math.abs(sp[1] - y); if (e < d) d = e; }
    return d;
  }
  function tetraIsSite(x, y) {
    const dt = tetraNearest(TETRA_SITES, x, y);
    if (dt > TETRA_SNAP) return false;                            // near no dig site -> not ours
    return dt < tetraNearest(COMPASS_SPOTS_G, x, y);              // else the nearer list claims it
  }
  function tetraRead() {                                          // varc 1323 -> {x,y,p}, or null
    let s = ''; try { s = bridge().compassTarget(myPid()) || ''; } catch (e) {}
    if (!s) return null;
    const p = s.split(',').map(Number);
    if (p.length < 2 || !(p[0] > 0 && p[1] > 0)) return null;
    return { x: p[0], y: p[1], p: p[2] | 0 };
  }
  // varc 1323 KEEPS its last value after the interface is closed, so the varc alone cannot say
  // whether a target is still live -- read on its own it leaves a stale tile marked long after the
  // compass is put away. compassHeading returns -1 unless interface group 996 is open AND its
  // needle component is present, so it doubles as the open-probe. Memoised just under the poll
  // interval: it walks the interface group list, and several callers want it each tick.
  let tetraOpenAt = 0, tetraOpenV = false;
  function tetraOpen() {
    const now = Date.now();
    if (now - tetraOpenAt < 200) return tetraOpenV;
    tetraOpenAt = now;
    let v = false;
    try { v = parseInt(bridge().compassHeading(myPid()), 10) >= 0; } catch (e) {}
    tetraOpenV = v;
    return v;
  }
  function tetraTarget() {
    if (!tetraOpen()) return null;                      // closed -> whatever the varc still holds is stale
    const t = tetraRead();
    return (t && tetraIsSite(t.x, t.y)) ? t : null;
  }
  // Polled alongside compassTick. Only the SELECTED tetracompass drives the mark + map, on the
  // same rule the compass follows: whatever is picked in the held list owns them, so a carried
  // tetracompass cannot overwrite the tile of the clue you are actually solving. Deselection is
  // handled by selectClue, which redraws for whatever took its place.
  async function tetraTick() {
    if (!bridge() || !bridge().compassTarget) return;
    if (activeClueId !== TETRA_POWERED || !tetraHeld()) { tetraLast = null; return; }
    const t = tetraTarget();
    if (!t) { if (tetraLast) { tetraLast = null; clueGuide(null); drawClueMap(null); } return; }
    // Standing on the tile, the label has done its job and is only blocking the view, so it drops
    // to an empty string -- the overlay draws the marker alone for those (Overlay.cpp skips the
    // text plate when the label is empty).
    const pos = await scanPlayerTile();
    const onSpot = !!pos && pos.x === t.x && pos.y === t.y && (pos.p | 0) === (t.p | 0);
    // Re-mark when the target moves, when you step on or off it, and when the shared clue map has
    // been hidden by another path without the target changing, else the dig map stays blank.
    const tileSig = t.x + ',' + t.y + ',' + t.p;
    const sig = tileSig + (onSpot ? '|on' : '');
    const mw = $('clueMapWrap');
    const mapHidden = !mw || mw.style.display === 'none';
    if (sig === tetraLast && !mapHidden) return;
    const moved = !tetraLast || tetraLast.split('|')[0] !== tileSig;
    tetraLast = sig;
    clueGuide(t, onSpot ? '' : 'TETRACOMPASS DIG HERE - (' + t.x + ', ' + t.y + ')');
    if (moved || mapHidden) drawClueMap({ x: t.x, y: t.y, p: t.p });   // stepping on/off needs no redraw
  }

  // Both solvers publish through varc 1323, and only the one it points at can actually be acted
  // on -- so the varc, not the click, decides which of the two is showing. It is checked every
  // poll rather than once on change: an edge-triggered version let a click sit on the inert entry
  // until the target happened to move. Returns the pinned held-entry id, or -1 for neither.
  function clueVarcPinned() {
    if (!bridge() || !bridge().compassTarget || !tetraOpen()) return -1;   // nothing open -> nothing pinned
    const t = tetraRead(); if (!t) return -1;
    const held = clueHeldList();
    if (tetraIsSite(t.x, t.y)) return held.some(c => c.i === TETRA_POWERED) ? TETRA_POWERED : -1;
    const cc = held.find(c => isCompassClue(c));
    return cc ? cc.i : -1;
  }
  // Is this entry one the varc arbitrates? Only those two are pinned; every other clue stays
  // freely selectable, since the varc says nothing about them.
  function clueVarcIsPair(id) {
    if (id === TETRA_POWERED) return true;
    const c = (id >= 0) ? CLUE_DATA.find(z => z.i === id) : null;
    return !!(c && isCompassClue(c));
  }
  function clueVarcRoute() {
    if (clueBrowse) return;                               // browse mode owns no selection
    const want = clueVarcPinned();
    if (want < 0 || want === activeClueId) return;
    if (activeClueId >= 0 && !clueVarcIsPair(activeClueId)) return;   // deliberate pick elsewhere; leave it
    const e = clueHeldList().find(c => c.i === want);
    // A tier filter that hides the entry would defeat the whole point, so widen to All in that case.
    if (e && clueLiveTier >= 0 && e.t !== clueLiveTier) clueLiveTier = -1;
    activeClueId = want; clueMapZoom = 1; clueMapPin = null; compassMapSig = '';
    selectClue();
  }

  async function compassTick() {
    if (compassBusy) return;
    // Only the SELECTED compass clue drives the in-world mark + panel map, else a held compass clue
    // would override the picked clue and mark the wrong spot every tick.
    const ac = (activeClueId >= 0) ? CLUE_DATA.find(z => z.i === activeClueId) : null;
    if (!ac || !isCompassClue(ac)) { const cb = $('clueCompass'); if (cb) cb.style.display = 'none'; return; }
    compassBusy = true;
    try {
      let raw = -1; try { raw = parseInt(bridge().compassHeading(myPid()), 10); } catch (e) {}
      let open = raw >= 0;
      if (open) compassLastRaw = raw;
      // PRIMARY: exact target from varc 1323 (instant). It does NOT need the needle, and
      // the row only exists while the clue is HELD, so a broken or lagging heading reader
      // must never blank the solver (user-caught: needle spinning, no answer shown). The
      // spot-list snap already rejects stale/garbage varc values; a valid snap implies the
      // target is loaded, so it also counts as "open".
      const tgt = compassTargetSpot();
      if (tgt) open = true;
      const pos = open ? await scanPlayerTile() : null;         // current tile (for the player marker only; capture is manual)
      const res = tgt ? { state: 'solved', spot: tgt, best: tgt, err: 0, cands: 1, via: 'varc' }
                      : (open || compassReads.length) ? compassSolve() : null;
      // Selection can change DURING the awaits above (the scroll turns into a puzzle box
      // and the held list reselects): a stale tick finishing late must not re-mark the
      // world or redraw the map the new clue just cleared (user-caught 2026-08-01).
      const ac2 = (activeClueId >= 0) ? CLUE_DATA.find(z => z.i === activeClueId) : null;
      if (!ac2 || !isCompassClue(ac2)) return;
      compassResult = { open: open, res: res, pos: pos };
      if (res && res.state === 'solved') {                      // locked to one tile -> mark it in-world like a dig
        clueGuide({ x: res.spot[0], y: res.spot[1], p: 0 }, 'COMPASS DIG HERE');
      } else if (open) {                                        // ambiguous, on a compass clue -> clear the in-world tile
        try { if (bridge() && bridge().guideMarks) bridge().guideMarks(myPid(), ''); } catch (e) {}
      }
      const sig = (res ? res.state : 'x') + '|' + (res && res.best ? res.best.join(',') : '') + '|' + compassReads.map(r => r.x + ',' + r.y + ',' + r.raw).join(';');
      // Redraw on sig change, OR when solved but the shared clue map got hidden by another path
      // without the sig changing, else the dig map silently stays blank. Self-limiting: once shown
      // it is no longer hidden.
      const _mw = $('clueMapWrap'); const _mapHidden = !_mw || _mw.style.display === 'none';
      if (sig !== compassMapSig || (res && res.state === 'solved' && _mapHidden)) { compassMapSig = sig;
        if (res && res.state === 'solved') drawClueMap({ x: res.spot[0], y: res.spot[1], p: 0 });
        else drawCompassMap();
      }
      compassPaint();
    } catch (e) {} finally { compassBusy = false; }
  }
  function compassReset() {
    compassReads = []; compassResult = null; compassMapSig = null;
    try { if (bridge() && bridge().guideMarks) bridge().guideMarks(myPid(), ''); } catch (e) {}
    drawCompassMap(); compassPaint();
  }
  // Manual capture: record the CURRENT player tile + current needle reading as one point.
  async function compassCapture() {
    try {
      // The needle must be read LIVE. compassLastRaw is whatever the needle said at some EARLIER
      // tile (:262 only writes it while open), so falling back to it logs that old bearing against
      // the tile you are standing on now -- a fabricated reading, with no warning. One is enough to
      // take the true spot from RMS 0.24 / 1 candidate to RMS 394 / 0 candidates, and since the
      // reads array is sticky the user has no way back except the Clear button. This leaves
      // compassLastRaw written at :262 and never read; kept as-is rather than widening the diff.
      let raw = -1;
      try { const v = parseInt(bridge().compassHeading(myPid()), 10); if (v >= 0) raw = v; } catch (e) {}
      if (raw < 0) { compassFlash('open the compass clue first'); return; }
      const pos = await scanPlayerTile();
      if (!pos) { compassFlash('be in-world, then capture'); return; }
      const dup = compassReads.find(r => r.x === pos.x && r.y === pos.y);
      if (dup) { dup.raw = raw; compassFlash('updated this spot'); }     // same tile -> refresh its reading
      else if (compassReads.length < 16) { compassReads.push({ x: pos.x, y: pos.y, raw: raw }); }
      else { compassFlash('16 readings max'); }
      compassMapSig = null;                                              // force the map to redraw with the new ray
      await compassTick();
    } catch (e) {}
  }
  function compassFlash(m) { compassNote = m; compassPaint(); setTimeout(() => { compassNote = ''; compassPaint(); }, 2500); }
  // Plausible candidate spots (RMS within the fit ceiling); evaluated from the first reading.
  function compassCandidates() {
    if (!compassReads.length) return [];
    const out = []; for (const s of COMPASS_SPOTS_G) if (compassErr(s) < COMPASS_FIT_OK) out.push(s); return out;
  }
  async function drawCompassMap() {
    const wrap = $('clueMapWrap'), cv = $('clueMapCanvas'), cap = $('clueMapCap');
    if (!wrap || !cv) return;
    const reads = compassReads, cands = compassCandidates();
    const pz = $('clueMapPulse'); if (pz) pz.style.display = 'none';
    const lz = $('clueMapLode'); if (lz) lz.style.display = 'none'; const tz = $('clueMapTele'); if (tz) tz.style.display = 'none';
    const fz = $('clueMapFloors'); if (fz) { fz.style.display = 'none'; fz._sig = ''; }
    if (!reads.length) { wrap.style.display = 'none'; return; }
    wrap.style.display = '';
    const myseq = ++clueMapDrawSeq;                                      // race guard: a newer draw wins
    let focus = (compassResult && compassResult.res && compassResult.res.best) || null;   // the solver's best-fit spot (so the ring matches the banner)
    if (!focus && reads.length) { let fe = 1e9; for (const s of COMPASS_SPOTS_G) { const e = compassErr(s); if (e < fe) { fe = e; focus = s; } } }
    const pts = reads.map(r => [r.x, r.y]).concat(cands.map(s => [s[0], s[1]]));
    if (focus) pts.push([focus[0], focus[1]]);
    let minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
    for (const p of pts) { if (p[0] < minx) minx = p[0]; if (p[0] > maxx) maxx = p[0]; if (p[1] < miny) miny = p[1]; if (p[1] > maxy) maxy = p[1]; }
    const cx0 = Math.round((minx + maxx) / 2), cy0 = Math.round((miny + maxy) / 2), ext = Math.max(maxx - minx, maxy - miny);
    const plane = (reads[0] && reads[0].p) || 0;
    let W = 384, g, projX, projY, drewTerrain = false;
    if (ext <= 160 && bridge() && bridge().mapWindow) {
      const half = Math.max(12, Math.min(80, Math.ceil(ext / 2) + 10));
      // Resolution follows the DISPLAY, like the scan/dig maps: a fixed 6 px per tile over a
      // small window is a ~144 px image stretched across the whole panel, which is what left a
      // solved compass looking like a mosaic. mapRes also sizes for device pixels.
      clueMapSpan = 2 * half;
      const R = mapRes(clueMapSpan, half);
      const ts = R.ts;
      let meta = await mapWindowCached(cx0, cy0, plane, R.half, ts);
      if (myseq !== clueMapDrawSeq) return;                              // newer draw started during the terrain fetch -> bail
      W = (meta && meta.w) || 384; const TS = (meta && meta.t) || ts, H = (meta && meta.h) || R.half;
      // Scale from the window we actually GOT: mapRes may widen it past the requested half to
      // reach display resolution, and sizing against the requested span would push the extra
      // ground off the stage instead of showing it.
      clueMapSpan = 2 * H; clueMapWinCx = cx0; clueMapWinCy = cy0; clueMapHalfGot = H; applyMapZoom();
      // Backing store at display resolution so labels/markers are not upscaled with the terrain.
      g = clueMapCtx(cv, W);
      if (meta && (meta.png || meta.b64 || meta._k)) { try { clueMapBlit(g, meta, W, cv); clueDrawNomove(g, meta); clueDrawObjects(g, meta); clueDrawTeleports(g, meta); clueDrawLabelsWindow(g, meta); drewTerrain = true; } catch (e) {} }
      projX = sx => (sx - (cx0 - H)) * TS + TS / 2;
      projY = sy => ((2 * H - 1) - (sy - (cy0 - H))) * TS + TS / 2;
    }
    if (!drewTerrain) {
      W = 384;
      g = clueMapCtx(cv, W); g.fillStyle = '#0b0d12'; g.fillRect(0, 0, W, W);
      const pad = 30, sc = (W - 2 * pad) / Math.max(1, ext || 1);
      projX = sx => W / 2 + (sx - cx0) * sc;
      projY = sy => W / 2 - (sy - cy0) * sc;
    }
    clueMapProj = { projX: projX, projY: projY, W: W, plane: plane };
    // Heading ray per reading at the FIXED north-zero, extended to the panel edge; each ray depends
    // only on its own (tile, raw) reading, so it draws identically every redraw.
    const rays = [];
    {
      for (const r of reads) {
        let b = compassInvert(r.raw);   // calibrated raw -> bearing (4H-corrected); per-clue offset omitted (stable rays)
        const rad = b * Math.PI / 180, x0 = projX(r.x), y0 = projY(r.y);
        let ux = projX(r.x + Math.sin(rad)) - x0, uy = projY(r.y + Math.cos(rad)) - y0;
        const mag = Math.hypot(ux, uy) || 1; ux /= mag; uy /= mag;
        let tmax = Infinity;
        if (ux > 1e-9) tmax = Math.min(tmax, (W - x0) / ux); else if (ux < -1e-9) tmax = Math.min(tmax, -x0 / ux);
        if (uy > 1e-9) tmax = Math.min(tmax, (W - y0) / uy); else if (uy < -1e-9) tmax = Math.min(tmax, -y0 / uy);
        if (!isFinite(tmax) || tmax < 0) tmax = 0;
        g.lineWidth = 2; g.strokeStyle = 'rgba(120,180,255,0.85)';
        g.beginPath(); g.moveTo(x0, y0); g.lineTo(x0 + ux * tmax, y0 + uy * tmax); g.stroke();
        rays.push({ x: x0, y: y0, ux: ux, uy: uy });
      }
    }
    if (rays.length >= 2) {                                              // X at the forward crossing of the widest-baseline pair
      let mark = null, bestSep = -1;
      for (let i = 0; i < rays.length; i++) for (let j = i + 1; j < rays.length; j++) {
        const A = rays[i], B = rays[j], den = A.ux * B.uy - A.uy * B.ux;
        if (Math.abs(den) < 1e-6) continue;
        const t = ((B.x - A.x) * B.uy - (B.y - A.y) * B.ux) / den, s = ((B.x - A.x) * A.uy - (B.y - A.y) * A.ux) / den;
        if (t < 0 || s < 0) continue;
        const sep = Math.hypot(A.x - B.x, A.y - B.y);
        if (sep > bestSep) { bestSep = sep; mark = [A.x + A.ux * t, A.y + A.uy * t]; }
      }
      if (mark) { g.lineWidth = 2; g.strokeStyle = 'rgba(255,255,255,0.9)';
        g.beginPath(); g.moveTo(mark[0] - 6, mark[1] - 6); g.lineTo(mark[0] + 6, mark[1] + 6);
        g.moveTo(mark[0] - 6, mark[1] + 6); g.lineTo(mark[0] + 6, mark[1] - 6); g.stroke(); }
    }
    cands.forEach(s => {
      if (focus && s[0] === focus[0] && s[1] === focus[1]) return;
      const px = projX(s[0]), py = projY(s[1]);
      g.beginPath(); g.arc(px, py, 3, 0, 6.2832); g.fillStyle = 'rgba(255,45,149,0.5)'; g.fill();
      g.lineWidth = 1.2; g.strokeStyle = 'rgba(0,0,0,0.7)'; g.stroke();
    });
    if (focus) {
      const px = projX(focus[0]), py = projY(focus[1]);
      g.beginPath(); g.arc(px, py, 5, 0, 6.2832); g.fillStyle = '#ff2d95'; g.fill();
      g.lineWidth = 2; g.strokeStyle = '#fff'; g.beginPath(); g.arc(px, py, 8.5, 0, 6.2832); g.stroke();
    }
    reads.forEach(r => {
      const px = projX(r.x), py = projY(r.y);
      g.beginPath(); g.arc(px, py, 4, 0, 6.2832); g.fillStyle = '#19d2ff'; g.fill();
      g.lineWidth = 1.4; g.strokeStyle = 'rgba(0,0,0,0.8)'; g.stroke();
    });
    try { clueMapPlayerDraw(await scanPlayerTile()); } catch (e) {}
    if (cap) {
      const txt = cands.length === 1
            ? ('Compass  ·  DIG (' + cands[0][0] + ', ' + cands[0][1] + ')')
            : ('Compass  ·  ' + reads.length + ' reading' + (reads.length === 1 ? '' : 's') + '  ·  ' + cands.length + ' possible  ·  take another capture');
      cap.textContent = txt + (drewTerrain ? '' : '  ·  (overview)');
    }
    applyMapZoom();
  }
  function compassPaint() {
    const el = $('clueCompass'); if (!el) return;
    const r = compassResult;
    if (!r || (!r.open && !compassReads.length)) { el.style.display = 'none'; return; }
    el.style.display = '';
    const s = r.res;
    const n = compassReads.length, faint = t => ' <span style="opacity:0.6">(' + t + ')</span>';
    let msg;
    if (s && s.state === 'solved') msg = 'DIG HERE: <b>' + s.spot[0] + ', ' + s.spot[1] + '</b>' + (s.via === 'varc' ? faint('exact, from clue') : '');
    else if (!r.open) msg = 'closed.' + faint(n + ' reading' + (n === 1 ? '' : 's') + ' kept');
    else if (!n) msg = 'stand on a tile and press Use this coordinate';
    else msg = 'please take another capture' + faint(n + ' reading' + (n === 1 ? '' : 's') + ', ' + (s ? s.cands : 0) + ' possible');
    if (compassNote) msg += ' <span style="opacity:0.7">(' + compassNote + ')</span>';
    const btn = (id, label) => ' <button id="' + id + '" style="margin-left:8px;padding:2px 9px;border-radius:5px;border:1px solid rgba(255,255,255,0.14);background:rgba(255,255,255,0.05);color:inherit;cursor:pointer;font-size:11px">' + label + '</button>';
    const solved = s && s.state === 'solved';
    const html = '<span style="opacity:0.65;text-transform:uppercase;font-size:10px;letter-spacing:0.6px;margin-right:7px">Compass</span>' + msg
      + (solved ? '' : btn('ccCapture', 'Use this coordinate') + btn('ccReset', 'Clear'));
    if (el._ccHtml !== html) { el._ccHtml = html; el.innerHTML = html; }   // rebuild only when the text changes (keeps the buttons stable)
    if (!el._ccBound) { el._ccBound = true; el.addEventListener('click', function (ev) {   // delegated: survives innerHTML rebuilds, so clicks never get dropped
      if (ev.target.closest && ev.target.closest('#ccCapture')) compassCapture();
      else if (ev.target.closest && ev.target.closest('#ccReset')) compassReset();
    }); }
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { TETRA_POWERED, clueVarcIsPair, clueVarcPinned, clueVarcRoute, compassTick, tetraOpen, tetraTarget, tetraTick });
})();
