// RuneToolsX panel: Clue scrolls map + emote/cryptic guide.
// Spliced inline into client.html; bare classic script sharing one global scope.

  // CLUE_DATA maps every clue-scroll item id to the action its item def encodes (decoded from
  // the cache). "Held" = that clue item is in your inventory/bank right now (live container read).
  let clueData = null, clueFetching = false, clueListSig = '', clueFetchAt = 0;
  let clueHeld = new Set();
  let clueFTier = -1, clueFAction = '', clueFHeld = false, clueFSearch = '';
  let clueHeldInv = new Set();   // clues held in the BACKPACK (container 93) only, for auto-map
  let clueAutoMap = false; try { clueAutoMap = localStorage.getItem('rtxClueAuto') === '1'; } catch (e) {}
  // OPT-IN (default OFF): read the scan-clue solution tile the server transmits each tick via the
  // companion; on = the solver resolves the exact dig tile instantly. Persisted per device.
  let clueBrowse = false, clueFocusIdx = 0, clueFocusSig = '', clueLiveTier = -1;   // clueLiveTier: live difficulty tab (-1 = all held)
  // Held puzzle boxes are real items but NOT clue-database entries: they get synthetic carousel
  // entries ({i:itemId, t:tier-from-name, a:'puzzle'}) rebuilt each fetch.
  let cluePuzzleHeld = [];
  let tetraHeldEntry = null;   // synthetic held entry for a carried tetracompass (same idea as above)
  let cluePuzzleOpen = false, cluePuzzleWasOpen = false;
  function clueHeldList() {   // held clues + held puzzle boxes, ordered by tier (easy -> master -> special)
    const base = clueData ? clueData.filter(c => clueHeld.has(c.i)) : [];
    return base.concat(cluePuzzleHeld, tetraHeldEntry || []).sort((a, b) => a.t - b.t || a.i - b.i);
  }
  let activeClueId = -1;   // the clue whose tile is pinned on the map + highlighted in-world (-1 = none)
  let clueEmoteSel = -1, clueEmoteSearch = '', clueEmoteTier = -1;
  let clueHidey = true; try { clueHidey = localStorage.getItem('rtxClueHidey') !== '0'; } catch (e) {}
  // Target world tile for a clue: coordinate/map carry x/y/p directly; npc clues via CLUE_NPC_INFO.
const CLUE_SCAN_AREAS = {"the deepest levels of the wilderness":{"r":25,"t":"elite","s":[[2958,3917,0],[2979,3962,0],[2998,3914,0],[2956,3908,0],[2944,3909,0],[2990,3924,0],[3055,3914,0],[3060,3941,0],[3029,3949,0],[3012,3959,0],[3057,3948,0],[3048,3926,0],[3037,3925,0],[3031,3926,0],[3021,3926,0],[3110,3954,0],[3125,3909,0],[3080,3911,0],[3160,3943,0],[3182,3926,0],[3193,3950,0],[3179,3943,0],[3175,3962,0],[3242,3956,0],[3241,3944,0],[3210,3910,0],[3216,3944,0],[3266,3936,0],[3307,3916,0],[3306,3947,0],[3281,3942,0],[3315,3914,0],[3269,3914,0],[3380,3960,0],[3342,3894,0],[3334,3906,0],[3363,3960,0],[3346,3976,0],[3408,3937,0]]},"the desert east of the elid and north of nardah":{"r":27,"t":"elite","s":[[3421,2949,0],[3444,2952,0],[3447,2967,0],[3438,2960,0],[3417,2959,0],[3427,2970,0],[3442,2974,0],[3408,2986,0],[3426,2984,0],[3436,2989,0],[3406,3003,0],[3393,2997,0],[3419,3017,0],[3382,3015,0],[3385,3024,0],[3383,3018,0],[3423,3020,0],[3448,3019,0],[3411,3048,0],[3422,3051,0],[3448,3063,0],[3401,3064,0],[3460,3022,0],[3476,3018,0],[3462,3047,0],[3465,3034,0],[3502,3050,0],[3510,3041,0],[3476,3057,0],[3480,3090,0],[3473,3082,0],[3482,3108,0],[3499,3104,0],[3505,3093,0],[3401,3099,0],[3396,3110,0],[3406,3126,0],[3433,3122,0],[3444,3085,0],[3446,3128,0],[3360,3095,0],[3387,3123,0],[3373,3126,0],[3384,3081,0],[3405,3136,0],[3409,3119,0],[3432,3105,0],[3427,3141,0],[3444,3141,0],[3435,3129,0],[3456,3140,0]]},"isafdar and lletya":{"r":22,"t":"elite","s":[[2173,3125,0],[2283,3265,0],[2217,3130,0],[2253,3118,0],[2247,3143,0],[2271,3165,0],[2287,3141,0],[2310,3187,0],[2331,3171,0],[2322,3190,0],[2348,3183,0],[2225,3145,0],[2186,3146,0],[2205,3155,0],[2216,3159,0],[2182,3192,0],[2226,3158,0],[2176,3201,0],[2225,3212,0],[2219,3221,0],[2194,3220,0],[2194,3237,0],[2229,3248,0],[2203,3254,0],[2258,3212,0],[2289,3220,0],[2302,3230,0],[2282,3262,0],[2232,3223,0]]},"varrock and the grand exchange":{"r":16,"t":"elite","s":[[3197,3383,0],[3211,3385,0],[3228,3383,0],[3240,3383,0],[3175,3404,0],[3175,3415,0],[3196,3415,0],[3197,3423,0],[3253,3393,0],[3228,3409,0],[3231,3439,0],[3220,3407,0],[3204,3409,0],[3273,3398,0],[3284,3378,0],[3185,3472,0],[3188,3488,0],[3180,3510,0],[3141,3488,0],[3213,3484,0],[3230,3494,0],[3241,3480,0],[3213,3462,0],[3248,3454,0]]},"east or west ardougne":{"r":22,"t":"elite","s":[[2442,3310,0],[2440,3319,0],[2462,3282,0],[2483,3313,0],[2467,3319,0],[2496,3282,0],[2517,3281,0],[2512,3267,0],[2529,3270,0],[2537,3306,0],[2520,3318,0],[2500,3290,0],[2540,3331,0],[2509,3330,0],[2475,3331,0],[2583,3265,0],[2589,3319,0],[2582,3314,0],[2623,3311,0],[2570,3321,0],[2662,3304,0],[2625,3292,0],[2635,3313,0],[2662,3338,0],[2633,3339,0],[2613,3337,0],[2589,3330,0],[2569,3340,0]]},"keldagrim":{"r":11,"t":"elite","s":[[2841,10189,0],[2856,10192,0],[2822,10193,0],[2873,10194,0],[2872,10181,0],[2846,10233,0],[2860,10215,0],[2837,10209,0],[2905,10162,0],[2924,10162,0],[2938,10162,0],[2936,10206,0],[2906,10202,0],[2937,10191,0],[2904,10193,0],[2924,10191,0],[2922,10179,0],[2938,10179,0]]},"menaphos":{"r":30,"t":"elite","s":[[3099,2677,0],[3127,2643,0],[3095,2730,0],[3096,2692,0],[3108,2742,0],[3131,2791,0],[3135,2775,0],[3146,2659,0],[3155,2641,0],[3180,2669,0],[3180,2700,0],[3199,2750,0],[3145,2759,0],[3153,2799,0],[3165,2814,0],[3193,2797,0],[3237,2664,0],[3200,2709,0],[3210,2770,0],[3231,2770,0],[3238,2792,0]]},"piscatoris hunter area":{"r":14,"t":"elite","s":[[2310,3518,0],[2336,3540,0],[2364,3547,0],[2353,3543,0],[2324,3553,0],[2361,3567,0],[2308,3560,0],[2313,3576,0],[2331,3574,0],[2342,3575,0],[2358,3557,0],[2363,3527,0],[2358,3580,0],[2388,3586,0],[2398,3582,0],[2392,3591,0],[2373,3612,0],[2372,3626,0],[2318,3601,0],[2339,3589,0],[2362,3612,0],[2347,3609,0],[2332,3632,0],[2320,3625,0],[2344,3645,0],[2310,3587,0]]},"brimhaven dungeon":{"r":14,"t":"elite","s":[[2703,9439,0],[2738,9456,0],[2737,9433,0],[2722,9444,0],[2729,9427,0],[2695,9457,0],[2739,9506,0],[2745,9488,0],[2707,9486,0],[2699,9481,0],[2730,9513,0],[2717,9517,0],[2712,9524,0],[2701,9516,0],[2713,9503,0],[2679,9479,0],[2650,9488,0],[2649,9499,0],[2665,9504,0],[2681,9505,0],[2647,9511,0],[2664,9522,0],[2672,9514,0],[2652,9528,0],[2640,9533,0],[2641,9541,0],[2680,9542,0],[2669,9573,0],[2675,9582,0],[2660,9589,0],[2642,9575,0],[2638,9594,2],[2630,9581,2],[2638,9566,2],[2634,9551,2],[2627,9530,2],[2631,9516,2],[2639,9504,2],[2628,9489,2],[2655,9475,2],[2703,9564,0],[2697,9563,0]]},"taverley dungeon":{"r":22,"t":"elite","s":[[2858,9788,0],[2870,9791,0],[2875,9805,0],[2832,9813,0],[2835,9819,0],[2822,9826,0],[2892,9783,0],[2895,9769,0],[2914,9757,0],[2936,9764,0],[2905,9734,0],[2907,9718,0],[2907,9705,0],[2926,9692,0],[2968,9786,0],[2952,9786,0],[2949,9773,0],[2938,9812,0],[2904,9809,0],[2884,9799,0],[2895,9831,0],[2933,9848,0],[2907,9842,0],[2888,9846,0],[2945,9796,0]]},"mos le harmless":{"r":27,"t":"elite","s":[[3702,2972,0],[3657,2955,0],[3699,2996,0],[3651,2991,0],[3692,2976,0],[3676,2981,0],[3773,2960,0],[3728,2973,0],[3730,2996,0],[3765,2995,0],[3717,2969,0],[3758,2956,0],[3752,3006,0],[3669,3055,0],[3695,3063,0],[3697,3052,0],[3687,3046,0],[3679,3018,0],[3702,3027,0],[3757,3063,0],[3742,3063,0],[3736,3041,0],[3726,3038,0],[3765,3030,0],[3769,3015,0],[3740,3018,0],[3802,3035,0],[3791,3023,0],[3818,3027,0],[3831,3031,0],[3843,2987,0],[3850,3010,0]]},"haunted woods":{"r":11,"t":"elite","s":[[3534,3470,0],[3523,3460,0],[3551,3514,0],[3575,3511,0],[3562,3509,0],[3583,3484,0],[3583,3466,0],[3552,3483,0],[3573,3484,0],[3529,3501,0],[3544,3465,0],[3567,3475,0],[3609,3499,0],[3596,3501,0],[3637,3486,0],[3623,3476,0],[3604,3507,0],[3624,3508,0],[3616,3512,0],[3606,3465,0],[3590,3475,0]]},"kharazi jungle":{"r":14,"t":"elite","s":[[2775,2891,0],[2786,2914,0],[2804,2924,0],[2762,2918,0],[2775,2936,0],[2766,2932,0],[2815,2887,0],[2859,2891,0],[2848,2907,0],[2827,2934,0],[2832,2935,0],[2852,2934,0],[2857,2919,0],[2841,2915,0],[2872,2901,0],[2920,2888,0],[2892,2907,0],[2931,2920,0],[2921,2937,0],[2892,2937,0],[2929,2894,0],[2927,2925,0],[2936,2917,0],[2944,2902,0],[2932,2935,0],[2942,2934,0]]},"zanaris":{"r":16,"t":"elite","s":[[2414,4378,0],[2423,4372,0],[2420,4381,0],[2389,4405,0],[2377,4410,0],[2404,4406,0],[2372,4467,0],[2402,4466,0],[2396,4457,0],[2410,4460,0],[2400,4441,0],[2380,4421,0],[2406,4428,0],[2429,4431,0],[2417,4444,0],[2417,4470,0],[2385,4447,0],[2439,4460,0],[2441,4428,0],[2468,4439,0],[2457,4443,0],[2453,4471,0]]},"the caves beneath lumbridge swamp":{"r":11,"t":"elite","s":[[3172,9570,0],[3167,9546,0],[3179,9559,0],[3191,9555,0],[3170,9557,0],[3210,9557,0],[3233,9547,0],[3246,9566,0],[3252,9577,0],[3227,9575,0],[3209,9587,0],[3210,9571,0]]},"fremennik isles of jatizso and neitiznot":{"r":16,"t":"elite","s":[[2354,3790,0],[2360,3799,0],[2324,3808,0],[2322,3787,0],[2311,3801,0],[2340,3803,0],[2342,3809,0],[2330,3829,0],[2311,3835,0],[2376,3800,0],[2381,3789,0],[2402,3789,0],[2421,3792,0],[2397,3801,0],[2419,3833,0],[2395,3812,0],[2373,3834,0],[2314,3851,0],[2326,3866,0],[2326,3850,0],[2354,3853,0],[2349,3880,0],[2312,3894,0],[2352,3892,0],[2414,3848,0],[2418,3870,0],[2377,3850,0],[2400,3870,0],[2368,3870,0],[2417,3893,0],[2399,3888,0],[2389,3899,0]]},"falador":{"r":22,"t":"elite","s":[[2938,3322,0],[2976,3316,0],[2947,3316,0],[3005,3326,0],[2958,3379,0],[2972,3342,0],[2945,3339,0],[2948,3390,0],[2942,3388,0],[2939,3355,0],[3039,3331,0],[3050,3348,0],[3015,3339,0],[3027,3365,0],[3025,3379,0],[3059,3384,0],[3031,3379,0],[3011,3382,0]]},"dorgesh kaan":{"r":16,"t":"elite","s":[[2747,5263,0],[2731,5266,0],[2740,5273,0],[2723,5279,0],[2711,5271,0],[2729,5295,0],[2711,5284,0],[2730,5315,0],[2717,5311,0],[2739,5253,1],[2738,5301,1],[2700,5284,1],[2704,5321,0],[2732,5327,0],[2704,5349,0],[2701,5343,1],[2704,5357,1],[2734,5370,1],[2747,5327,1],[2698,5316,1]]},"fremennik slayer dungeons":{"r":16,"t":"elite","s":[[2720,9969,0],[2741,9977,0],[2701,9978,0],[2724,9977,0],[2714,9990,0],[2731,9998,0],[2718,10000,0],[2722,10025,0],[2705,10027,0],[2745,10024,0],[2743,9986,0],[2751,9995,0],[2754,10009,0],[2804,10004,0],[2808,10018,0],[2789,10042,0],[2772,10030,0],[2757,10029,0],[2767,10002,0]]},"the crater in the wilderness":{"r":11,"t":"elite","s":[[3112,3678,0],[3124,3698,0],[3130,3672,0],[3120,3665,0],[3104,3704,0],[3087,3712,0],[3133,3717,0],[3118,3724,0],[3110,3735,0],[3096,3738,0],[3134,3741,0],[3169,3745,0],[3180,3713,0],[3163,3724,0],[3146,3738,0],[3174,3705,0],[3152,3698,0],[3145,3681,0],[3146,3698,0]]},"prifddinas":{"r":30,"t":"master","s":[[2175,3291,1],[2133,3379,1],[2145,3381,1],[2148,3351,1],[2174,3398,1],[2180,3322,1],[2199,3268,1],[2212,3272,1],[2227,3295,1],[2224,3328,1],[2197,3433,1],[2222,3429,1],[2228,3424,1],[2247,3267,1],[2234,3265,1],[2275,3382,1],[2292,3361,1],[2268,3397,1]]},"darkmeyer":{"r":16,"t":"master","s":[[3599,3328,0],[3590,3344,0],[3588,3361,0],[3596,3375,0],[3631,3358,0],[3611,3328,0],[3606,3391,0],[3639,3390,0],[3651,3405,0],[3670,3373,0],[3632,3344,0],[3654,3335,0],[3666,3344,0],[3654,3357,0],[3671,3366,0],[3661,3374,0],[3676,3392,0]]},"the islands that once were turtles":{"r":27,"t":"master","s":[[2103,11425,0],[2092,11427,0],[2100,11432,0],[2101,11440,0],[2099,11448,0],[2286,11452,0],[2285,11440,0],[2266,11455,0],[2221,11497,0],[2198,11481,0],[2183,11500,0],[2194,11500,0],[2200,11501,0],[2251,11428,0],[2267,11441,0],[2275,11453,0],[2283,11499,0],[2272,11428,0],[2286,11473,0]]},"the heart of gielinor":{"r":49,"t":"master","s":[[3119,6905,1],[3124,6905,1],[3148,6913,1],[3131,6921,1],[3131,6932,1],[3156,6925,1],[3166,6910,1],[3165,6932,1],[3236,6924,1],[3257,6927,1],[3258,6913,1],[3248,6903,1],[3227,6982,1],[3217,7004,1],[3219,7023,1],[3180,6985,1],[3129,6998,1],[3141,7018,1],[3183,7015,1],[3194,7029,1],[3210,7028,1],[3229,6980,1],[3241,7039,1],[3147,7046,1],[3156,7040,1],[3274,7045,1]]},"the lost grove":{"r":14,"t":"master","s":[[1374,5550,0],[1390,5616,0],[1431,5596,0],[1421,5627,0],[1426,5718,0],[1417,5684,0],[1405,5651,0],[1392,5730,0],[1349,5716,0],[1361,5733,0],[1307,5622,0],[1375,5687,0],[1332,5651,0],[1373,5634,0],[1351,5659,0],[1368,5609,0],[1370,5670,0]]},"wendlewick":{"r":12,"t":"elite","s":[[3485,1538,0],[3471,1555,0],[3514,1588,0],[3477,1578,0],[3506,1549,0],[3494,1517,0],[3487,1504,0],[3466,1523,0],[3442,1550,0],[3421,1525,0],[3402,1510,0],[3437,1494,0],[3423,1509,0],[3471,1498,0],[3483,1475,0],[3529,1500,0],[3570,1478,0],[3554,1470,0],[3559,1504,0],[3564,1532,0]]}};
  const scanKey = t => String(t || '').toLowerCase().replace(/[^a-z0-9]+/g, ' ').trim();
  const SCAN_SPOT_INDEX = (() => { const m = {}; for (const k in CLUE_SCAN_AREAS) for (const s of CLUE_SCAN_AREAS[k].s) m[s[0] + ',' + s[1] + ',' + (s[2] || 0)] = k; return m; })();
  const CLUE_SCAN_RESOLVED = new Map();   // scan-enum id -> {key, r:range, spots:[[x,y,p],...]} (filled by resolveClueNames)
  let scanElimByArea = {};                // areaKey -> Set(eliminated spot index); per session
  let scanElimOwner = {};                 // areaKey -> the scan-clue item id its eliminations belong to (so a new clue resets them)
  let clueHeldPrev = new Set(), clueHeldPrevInit = false;   // previous poll's held ids: a re-appearing clue id = a NEW instance
  // The 'seen a poll yet' flag must stay separate from prev.size: after a dig the held set is
  // legitimately EMPTY, and gating on size would skip the reset in exactly the case that matters
  // (empty -> new clue).
  // Clear the narrowing for whatever area this clue id scans. A clue id re-entering the inventory
  // is the only reliable "new instance" signal: the item id is shared by every clue of that area.
  function scanElimResetFor(itemId) {
    const c = CLUE_DATA && CLUE_DATA.find(z => z.i === itemId);
    if (!c || c.a !== 'scan') return;
    const rec = CLUE_SCAN_RESOLVED.get(c.en);
    const key = (rec && rec.key) || ('en' + c.en);
    if (scanElimByArea[key]) scanElimByArea[key].clear();
    delete scanElimOwner[key];
  }
  let scanElimLoaded = false;
  function scanElimGet(key) { if (!scanElimByArea[key]) scanElimByArea[key] = new Set(); return scanElimByArea[key]; }
  function scanSpotsFor(c) { return (c && c.a === 'scan') ? (CLUE_SCAN_RESOLVED.get(c.en) || null) : null; }
  // A "scan" clue whose resolved set is the whole compass dig-spot field (hundreds of spots) is
  // really a COMPASS clue, solved by the needle (compassTick): it must bypass the scan renderer
  // and the O(n^2) next-scan optimiser, which would freeze on ~486 spots.
  const COMPASS_FIELD_MIN = 100;
  // Compass clues are normally recognised by their huge candidate field, but the Eastern
  // Lands compass has only ~31 sites, which is scan-sized. So also treat a clue as a compass
  // once the compass interface has actually been seen open while it was the active clue -
  // remembered for the session, since the needle is only open while you are using it.
  const clueCompassSeen = new Set();
  function isCompassClue(c) {
    if (!c) return false;
    if (clueCompassSeen.has(c.i)) return true;
    const r = scanSpotsFor(c);
    return !!(r && r.spots && r.spots.length > COMPASS_FIELD_MIN);
  }
  // Called from the clue tick: the needle being open identifies the held clue as a compass.
  function clueNoteCompassOpen() {
    if (activeClueId < 0 || clueCompassSeen.has(activeClueId)) return;
    const c = CLUE_DATA.find(z => z.i === activeClueId);
    if (!c || c.a !== 'scan') return;                  // only the scan/compass family can be one
    try { if (typeof tetraOpen === 'function' && tetraOpen()) clueCompassSeen.add(activeClueId); }
    catch (e) {}
  }
  function scanCentroid(rec) {
    if (!rec || !rec.spots || !rec.spots.length) return null;
    let sx = 0, sy = 0; for (const s of rec.spots) { sx += s[0]; sy += s[1]; }
    return { x: Math.round(sx / rec.spots.length), y: Math.round(sy / rec.spots.length), p: rec.spots[0][2] || 0 };
  }
  function clueTile(c) {
    if (!c) return null;
    if (c.a === 'coordinate' || c.a === 'map' || c.a === 'search') return c.x > 0 ? { x: c.x, y: c.y, p: c.p || 0 } : null;
    if (c.a === 'npc') { const ni = CLUE_NPC_INFO[c.npc]; return (ni && ni.x > 0) ? { x: ni.x, y: ni.y, p: ni.p || 0 } : null; }
    if (c.a === 'scan') return scanCentroid(scanSpotsFor(c));   // centroid of candidate spots (selectable once resolved)
    return null;   // emote/keyitem have no single tile yet
  }
  // In-world label: ASCII only (the companion glyph atlas is 32..126).
  function clueLabel(c) {
    const t = CLUE_TIERS[c.t] + ' clue';
    if (c.a === 'coordinate' || c.a === 'map') return t + ' - dig - (' + c.x + ', ' + c.y + ')';
    if (c.a === 'search') return t + ' - search here - (' + c.x + ', ' + c.y + ')';   // clue_tele_location: go + search the object (not dig)
    if (c.a === 'npc') return t + ' - talk: ' + (c.nn || ('NPC #' + c.npc));
    return t;
  }
  // Push (or clear, when t is null) the transient in-world guide tile via the guideMarks bridge.
  function clueGuide(t, label) {
    if (!bridge() || !bridge().guideMarks) return;
    try {
      if (!t) { bridge().guideMarks(myPid(), ''); return; }
      const lab = String(label || '').replace(/[\x1e\x1f]/g, ' ').slice(0, 80);
      const x = Math.max(0, Math.min(16383, t.x | 0)), y = Math.max(0, Math.min(16383, t.y | 0)), p = Math.max(0, Math.min(3, t.p | 0));
      bridge().guideMarks(myPid(), x + '\x1f' + y + '\x1f' + p + '\x1f' + lab);
    } catch (e) {}
  }
  function scanTitle(rec) { return rec.key ? rec.key.replace(/\b\w/g, c => c.toUpperCase()) : 'Scan'; }
  // Emote + cryptic clue guide. No cache enum/struct maps a clue STEP -> text (server-pushed);
  // the rendered text lives in the clue-scroll interface (group 345, comps 1-8), so it is read
  // live and auto-identified against the clue reference data. Scroll closed -> searchable picker
  // fallback (item ids are generic "Clue scroll (tier)").
  let clueAuto = null, clueScrollSig = '-', cluePbSig = '';   // clueAuto = {kind:'emote'|'cryptic', i} identified from the scroll (STICKY: kept after you close it)
  // clueAuto is cached for the session, keyed to the held clue-scroll set at decode time. Each
  // clue STEP is a distinct item id, so a changed held set means the decode is stale -> cleared.
  let clueHeldScrollSig = '', clueAutoHeldSig = '';
  // Sliding puzzle-box item ids across tiers. Holding one means the talk-to step of a
  // "talk -> puzzle box" cryptic is already done -> skip straight to solving it.
  const PUZZLE_BOX_IDS = new Set([2795, 2798, 2800, 3565, 3567, 3569, 3571, 3576, 3578, 13011, 13013, 13015, 13017, 13019, 13021, 13023, 13025, 13027, 13029, 13031, 13033, 13035, 19624, 19625, 19042, 41789]);
  function holdingPuzzleBox() { for (const id of PUZZLE_BOX_IDS) if (clueHeld.has(id)) return true; return false; }
  const CLUE_NORM = s => String(s).toLowerCase().replace(/<[^>]*>/g, '').replace(/[^a-z0-9 ]/g, '').replace(/\s+/g, ' ').trim();
  const CLUE_MATCH = (() => { const m = [];
    EMOTE_CLUES.forEach((c, i) => m.push({ kind: 'emote', i, n: CLUE_NORM(c.tx) }));
    CRYPTIC_CLUES.forEach((c, i) => m.push({ kind: 'cryptic', i, n: CLUE_NORM(c.tx) }));
    return m; })();
  // An anagram clue prints the scramble in ALL CAPS, and a scramble is by definition a
  // permutation of the solution's letters. Matching those letter multisets identifies the
  // clue EXACTLY, with no dependence on our stored wording - which drifts (Jagex adds and
  // removes lead-ins like "He often preaches of", and our sheet's casing is inconsistent).
  const CLUE_LETTERS = s2 => String(s2).toUpperCase().replace(/[^A-Z]/g, '').split('').sort().join('');
  const CLUE_ANAGRAMS = (() => {
    const m = {};
    CRYPTIC_CLUES.forEach((c, i) => {
      if (!c.tgt || !/anagram/i.test(c.tx || '')) return;
      const k = CLUE_LETTERS(c.tgt);
      if (k.length >= 4 && m[k] === undefined) m[k] = i;
    });
    return m;
  })();
  function matchAnagramText(text) {
    if (!/anagram/i.test(text || '')) return null;
    // the scramble = every ALL-CAPS word in the scroll (the lead-in is sentence case)
    const caps = String(text).replace(/<[^>]*>/g, '').split(/\s+/)
      .filter(w => /[A-Z]/.test(w) && !/[a-z]/.test(w)).join('');
    const k = CLUE_LETTERS(caps);
    if (k.length < 4) return null;
    const i = CLUE_ANAGRAMS[k];
    return i === undefined ? null : { kind: 'cryptic', i: i };
  }
  function matchClueText(text) {
    const n = CLUE_NORM(text); if (n.length < 8) return null;
    for (const e of CLUE_MATCH) if (e.n === n) return e;
    const ana = matchAnagramText(text); if (ana) return ana;
    for (const e of CLUE_MATCH) if (e.n.length > 14 && (e.n.indexOf(n) === 0 || n.indexOf(e.n) === 0)) return e;
    for (const e of CLUE_MATCH) if (e.n.length > 20 && n.indexOf(e.n) >= 0) return e;   // scroll WRAPS the riddle (skill clues: "Complete the action to solve the clue: <riddle>")
    return null;
  }
  // Read the clue text from the open clue-scroll interface (group 345: the 8 text-line components, ids 1-8).
  async function clueScrollText() {
    try {
      const g = JSON.parse((await bridge().interfaceGroup(myPid(), 345)) || '{}');
      const lines = (g.widgets || []).filter(w => w.x && w.t && w.t[1] >= 1 && w.t[1] <= 8 && w.t[2] === -1)
        .sort((a, b) => ((a.r && a.r[1]) || 0) - ((b.r && b.r[1]) || 0)).map(w => String(w.x).replace(/<[^>]*>/g, '').trim()).filter(Boolean);
      return lines.join(' ');
    } catch (e) { return ''; }
  }
  // Poll the open scroll each clues-tab tick. A new identified clue replaces the shown one;
  // closing the scroll KEEPS it (sticky) so the solution stays up while you travel.
  async function clueScrollTick() {
    let m = null; try { m = matchClueText(await clueScrollText()); } catch (e) {}
    const sig = m ? (m.kind + m.i) : '';
    // Also refresh when the puzzle-box-held state flips for a slider cryptic; the scroll text
    // itself does not change.
    let pbSig = '';
    if (clueAuto && clueAuto.kind === 'cryptic') { const cc = CRYPTIC_CLUES[clueAuto.i]; if (cc && cc.ch) pbSig = holdingPuzzleBox() ? 'P' : 'p'; }
    if (sig === clueScrollSig && pbSig === cluePbSig) return;
    clueScrollSig = sig; cluePbSig = pbSig;
    // Only paint the emote panel when an emote/cryptic clue is in focus, else the open scroll would
    // re-show it on top of a focused compass clue.
    const _ac = activeClueId >= 0 ? CLUE_DATA.find(z => z.i === activeClueId) : null;
    const showEmote = !!(_ac && _ac.a === 'emote');
    if (m && (!clueAuto || clueAuto.kind !== m.kind || clueAuto.i !== m.i)) { clueAuto = m; clueAutoHeldSig = '?'; if (showEmote) renderEmotePanel(); }   // '?' = capture the held-scroll sig on the next fetchClues
    else if (clueAuto && showEmote) renderEmotePanel();                         // same clue, but puzzle-held state changed -> refresh
  }
  function clueDismissAuto() { clueAuto = null; clueAutoHeldSig = ''; clueScrollSig = '-'; clueSetNpc('');
    const c = (activeClueId >= 0) ? CLUE_DATA.find(z => z.i === activeClueId) : null;
    if (c && c.a === 'emote') renderEmotePanel(); else { const el = $('clueEmote'); if (el) { el.style.display = 'none'; el._h = ''; } clueGuide(null); } }
  // Outline the talk-to target NPC by name via the shared overlay-highlight channel.
  // Skill-clue instructions are full sentences whose first word is frequently an adverb or
  // article ("Completely use up a cleansing crystal on...", "Carefully search..."), so the
  // marker verb is the first recognised ACTION word, not blindly word[0].
  const CLUE_VERBS = ['use', 'mine', 'chop', 'cut', 'catch', 'fish', 'search', 'dig', 'talk',
    'smith', 'smelt', 'craft', 'burn', 'cook', 'fletch', 'pickpocket', 'steal', 'kill', 'slay',
    'pray', 'cast', 'climb', 'enter', 'open', 'plant', 'harvest', 'pick', 'clean', 'cleanse',
    'repair', 'build', 'light', 'fill', 'drink', 'eat', 'wear', 'equip', 'teleport', 'craft'];
  function clueVerbOf(text, fallback) {
    const ws = String(text || '').trim().split(/\s+/);
    for (const w of ws) {
      const t = w.toLowerCase().replace(/[^a-z]/g, '');
      if (CLUE_VERBS.indexOf(t) >= 0) return t.toUpperCase();
    }
    return fallback;
  }
  // Some clue NPCs relocate permanently once a quest is done (Philipe Carnillean moves out of
  // the Carnillean household after Carnillean Rising). A row's optional `alt` carries the
  // post-quest tile; it is applied only when the quest system CONFIRMS completion, so an
  // unresolved quest state always leaves the pre-quest location in place.
  function clueApplyAlt(c) {
    if (!c || !c.alt || !c.alt.q) return c;
    try {
      if (typeof QUESTS === 'undefined' || typeof questStatus !== 'function') return c;
      const qd = QUESTS.find(z => (z.name || '').toLowerCase() === c.alt.q.toLowerCase());
      if (!qd) return c;
      if (questStatus(qd, (typeof teleQuestVp !== 'undefined' && teleQuestVp) || {}) !== 2) return c;
      const o = Object.assign({}, c);
      o.x = c.alt.x; o.y = c.alt.y; o.p = c.alt.p || 0;
      if (c.alt.d) o.d = c.alt.d;
      return o;
    } catch (e) { return c; }
  }
  // Challenge scroll: some clue NPCs ask a counting/maths question before handing over the
  // next step. The answer is fixed per NPC (a few vary with quest state, noted inline).
  function clueChallengeHtml(c) {
    if (!c || !c.ca) return '';
    return '<div style="margin-top:6px;padding:6px 8px;border-radius:6px;background:rgba(255,196,64,0.08);'
         + 'border:1px solid rgba(255,196,64,0.25)">'
         + '<div style="font-size:10px;text-transform:uppercase;letter-spacing:0.5px;opacity:0.65">Challenge scroll</div>'
         + (c.cq ? '<div style="font-size:12px;opacity:0.85;margin-top:2px">' + esc(c.cq) + '</div>' : '')
         + '<div style="margin-top:3px;font-size:14px"><b style="color:#ffd479">' + esc(c.ca) + '</b></div>'
         + '</div>';
  }
  function clueSetNpc(name) { name = name || ''; if (name === clueHighlightNpc) return; clueHighlightNpc = name; try { syncOverlayHighlight(); } catch (e) {} }
  // Match an emote clue to its HIDEY entry by fill-item set (a hidey hole stores exactly the
  // clue's equipped items). Spelling differs slightly between the sources: compare normalized
  // word tokens (>=2 shared per item) and require a 2-of-3 majority.
  function hideyMatchForEmote(c) {
    if (!c || !Array.isArray(c.it)) return null;
    const toks = s => new Set(String(s).toLowerCase().replace(/[^a-z0-9 ]/g, ' ').split(/\s+/).filter(w => w.length > 2));
    const want = c.it.map(toks).filter(set => set.size);
    if (want.length < 2) return null;                                // "Nothing"/empty -> no hidey link
    let best = null, bestScore = 0;
    for (const h of HIDEY) {
      const have = (h.fi || []).map(toks);
      let score = 0;
      for (const w of want) {
        for (const hv of have) { let inter = 0; w.forEach(t => { if (hv.has(t)) inter++; }); if (inter >= Math.min(2, w.size)) { score++; break; } }
      }
      if (score > bestScore) { bestScore = score; best = h; }
    }
    return bestScore >= 2 ? best : null;
  }
  // Live hidey-hole status from hideyData: 0 = not built, 1 = empty, 2 = filled.
  function hideyStatePill(h) {
    if (!h || !hideyData) return null;
    const d = hideyData.find(x => x.vp === h.vp && x.b === h.b);
    if (!d) return null;
    if (d.state === 2) return { label: 'Filled', col: '#5fd07a', bg: 'rgba(95,208,122,0.16)' };
    if (d.state === 1) return { label: 'Empty', col: '#c9a253', bg: 'rgba(201,162,83,0.16)' };
    return { label: 'Not built', col: '#9aa0ad', bg: 'rgba(255,255,255,0.06)' };
  }
  function renderEmotePanel() {
    const el = $('clueEmote'); if (!el) return; el.style.display = '';
    const esc = s => String(s).replace(/[&<>"]/g, m => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[m]));
    let kind = null, c = null;                                                  // auto-identified (open scroll) wins; else the picked emote
    // Drop a stale sticky decode synchronously: if the held clue-scrolls changed since decode it is
    // no longer this clue. Otherwise the previous clue flashes for a frame before the deferred
    // fetchClues validation clears it.
    if (clueAuto && clueAutoHeldSig !== '' && clueAutoHeldSig !== '?' && clueAutoHeldSig !== clueHeldScrollSig) { clueAuto = null; clueAutoHeldSig = ''; clueScrollSig = '-'; }
    // Show the auto-read on a focused slot only when its tier lines up. Exact match for lower
    // tiers, but the reference data lumps Elite + Master at tier 3, so a tier-3 entry is valid
    // for an Elite OR Master item slot.
    let fromAuto = false;
    const _autoC = clueAuto ? (clueAuto.kind === 'emote' ? EMOTE_CLUES : CRYPTIC_CLUES)[clueAuto.i] : null;
    const _tierOk = _autoC && (clueEmoteTier < 0 || _autoC.t === clueEmoteTier || (_autoC.t >= 3 && clueEmoteTier >= 3));
    if (_tierOk) { kind = clueAuto.kind; c = clueApplyAlt(_autoC); fromAuto = true; }
    else if (clueEmoteSel >= 0) { kind = 'emote'; c = EMOTE_CLUES[clueEmoteSel]; }
    if (c) {
      const head = fromAuto
        ? '<div style="display:flex;justify-content:space-between;align-items:center"><span style="opacity:0.6;font-size:10px;text-transform:uppercase;letter-spacing:0.5px">' + (kind === 'emote' ? 'Emote' : (c.act === 'skill' ? 'Skill' : 'Cryptic')) + ' clue · auto-read (kept)</span><button id="emClear" title="clear" style="padding:0 6px;border-radius:5px;border:1px solid rgba(255,255,255,0.14);background:rgba(255,255,255,0.05);color:inherit;cursor:pointer;font-size:12px;line-height:18px">✕</button></div>'
        : '<button id="emBack" style="padding:1px 8px;border-radius:5px;border:1px solid rgba(255,255,255,0.14);background:rgba(255,255,255,0.05);color:inherit;cursor:pointer;font-size:11px">‹ list</button>';
      let html;
      if (kind === 'emote') {
        const hh = c.hh, on = clueHidey;
        if (hh) fetchHidey();                                         // load live hidey-hole state (throttled); re-renders this panel when it lands
        // Emote spots are an AREA; the stored (x,y) centre can land on an off-edge/void tile on platform
        // maps. Anchor guidance to the hidey-hole tile when the clue has one.
        const gx = (hh && hh[1] > 0) ? hh[1] : c.x, gy = (hh && hh[1] > 0) ? hh[2] : c.y, gpl = (hh && hh[1] > 0) ? (hh[3] || 0) : (c.p || 0);
        const _hm = hh ? hideyMatchForEmote(c) : null, _st = hideyStatePill(_hm);
        html = head + '<div style="margin-top:6px;font-size:14px"><b>' + esc(c.em) + '</b>' + (c.da ? ' <span style="color:#ff9b54;font-size:11px">⚠ double agent appears</span>' : '') + '</div>'
          + '<div style="opacity:0.8;margin-top:2px">' + esc(c.tx) + '</div>'
          + '<div style="margin-top:6px"><span style="opacity:0.6">at</span> <b>' + gx + ', ' + gy + '</b>' + (gpl ? ' <span style="opacity:0.6">floor ' + gpl + '</span>' : '') + '</div>'
          + '<div style="margin-top:3px"><span style="opacity:0.6">equip</span> ' + c.it.map(esc).join(', ') + '</div>'
          + (hh ? '<div style="margin-top:6px;display:flex;align-items:center;gap:7px"><button id="emHidey" title="' + (on ? 'Hide' : 'Show') + ' the hidey hole on the map" style="padding:1px 9px;border-radius:5px;border:1px solid ' + (_st ? _st.col : 'rgba(255,255,255,0.14)') + ';background:' + (_st ? _st.bg : 'rgba(255,255,255,0.05)') + ';color:' + (_st ? _st.col : 'inherit') + ';cursor:pointer;font-size:11px;font-weight:600' + (on ? ';box-shadow:0 0 0 1px ' + (_st ? _st.col : '#46e0c0') + ' inset' : '') + '">' + esc(_st ? _st.label : 'hidey hole') + '</button><span style="opacity:0.85">' + esc(hh[0]) + ' <span style="opacity:0.6">(' + hh[1] + ', ' + hh[2] + (hh[3] ? ', f' + hh[3] : '') + ')</span></span></div>' : '');
        setHTML(el, html);
        clueGuide({ x: gx, y: gy, p: gpl }, 'Emote: ' + c.em);
        clueSetNpc('');                                          // emote clues have no talk-to NPC
        drawClueMap({ x: gx, y: gy, p: gpl, mark: 'EMOTE HERE' }, (clueHidey && hh) ? { hidey: [hh[1], hh[2], hh[3]] } : null);
      } else {
        const ACT = { talk: 'Talk to', search: 'Search', dig: 'Dig at' };
        const FOLLOW = { slider: 'puzzle box (sliding)', lockbox: 'lockbox', celticknot: 'celtic knot', towers: 'towers' };
        if (c.act === 'skill') {
          const hasLoc = c.x > 0;
          // c.lvl is like "80 Fishing" or "76 Agility, 76 Fishing": skill icon + "<level> <skill>" each.
          const reqHtml = (c.lvl || '').split(',').map(function (part) {
            const mm = part.trim().match(/^(\d+)\s+(.+)$/);
            if (!mm) return '<span style="margin-right:12px">' + esc(part.trim()) + '</span>';
            const sidx = SKILL_NAMES.findIndex(n => n.toLowerCase() === mm[2].trim().toLowerCase());
            // fixed 16px overflow:hidden box so the async-filled sprite cannot spill onto the text
            const ic = sidx >= 0 ? '<span class="qr-ico sk-icon" data-skill="' + sidx + '" style="display:inline-block;width:16px;height:16px;overflow:hidden;vertical-align:-3px;margin-right:3px"></span>' : '';
            return '<span style="margin-right:12px;white-space:nowrap">' + ic + esc(mm[1]) + ' ' + esc(mm[2].trim()) + '</span>';
          }).join('');
          html = head + '<div style="margin-top:6px;font-size:14px"><b>' + esc(c.tgt) + '</b></div>'
            + (c.lvl ? '<div style="margin-top:4px;font-size:12px;opacity:0.9;line-height:18px">' + reqHtml + '</div>' : '')
            + '<div style="opacity:0.8;margin-top:4px">' + esc(c.tx) + '</div>'
            + (hasLoc ? '<div style="margin-top:6px"><span style="opacity:0.6">at</span> <b>' + c.x + ', ' + c.y + '</b>' + (c.p ? ' <span style="opacity:0.6">floor ' + c.p + '</span>' : '') + '</div>' : '')
            + (c.m ? '<div style="margin-top:5px;opacity:0.78;font-size:12px;line-height:1.4">' + esc(c.m) + '</div>' : '')
            + clueChallengeHtml(c);
          if (setHTML(el, html)) { el.querySelectorAll('.sk-icon[data-skill]').forEach(s => attachSkillIcon(s, +s.dataset.skill)); }
          if (hasLoc) {
            const verb = clueVerbOf(c.tgt, 'GO');
            clueGuide({ x: c.x, y: c.y, p: c.p || 0 }, c.tgt); drawClueMap({ x: c.x, y: c.y, p: c.p || 0, mark: verb + ' HERE' });
          } else { clueGuide(null); drawClueMap(null); }
          // Once in range the reader drops the tile and moves the arrow + label onto the NPC.
          clueSetNpc(c.npc ? (c.npc + '|' + clueVerbOf(c.tgt, 'USE').charAt(0)
                     + clueVerbOf(c.tgt, 'USE').slice(1).toLowerCase() + ' ' + c.npc) : '');
        } else if (c.ch && holdingPuzzleBox()) {
          // Holding the puzzle box means the talk-to/travel step is done; the puzzle panel reads the live
          // board (interface 1931) and highlights the tile to click.
          html = head + '<div style="margin-top:6px;font-size:14px"><b>Solve the ' + esc(FOLLOW[c.ch] || c.ch) + '</b></div>'
            + '<div style="opacity:0.8;margin-top:2px">' + esc(c.tx) + '</div>'
            + '<div style="margin-top:6px;color:#46e0c0">You\'re holding the puzzle box' + (c.tgt ? ' (already spoke to ' + esc(c.tgt) + ')' : '') + ' - the puzzle panel above highlights the tile to click.</div>';
          setHTML(el, html);
          clueGuide(null); clueSetNpc(''); drawClueMap(null);    // no travel/NPC -- the puzzle is solved in the interface
        } else {
          html = head + '<div style="margin-top:6px;font-size:14px"><b>' + esc(ACT[c.act] || c.act) + (c.tgt ? ' ' + esc(c.tgt) : '') + '</b></div>'
            + '<div style="opacity:0.8;margin-top:2px">' + esc(c.tx) + '</div>'
            + '<div style="margin-top:6px"><span style="opacity:0.6">at</span> <b>' + c.x + ', ' + c.y + '</b>' + (c.p ? ' <span style="opacity:0.6">floor ' + c.p + '</span>' : '') + (c.d ? ' <span style="opacity:0.6">· ' + esc(c.d) + '</span>' : '') + '</div>'
            + (c.ch ? '<div style="margin-top:4px;color:#46e0c0">→ then solve a ' + esc(FOLLOW[c.ch] || c.ch) + (c.ch === 'slider' ? ' (the panel auto-solves it)' : '') + '</div>' : '')
            + clueChallengeHtml(c);
          setHTML(el, html);
          const guideLabel = (ACT[c.act] || c.act) + (c.tgt ? ' ' + c.tgt : '');
          clueGuide({ x: c.x, y: c.y, p: c.p || 0 }, guideLabel);   // static destination tile (shown until the NPC is in scene)
          // talk-to: outline the target NPC; once in range the reader drops the tile above and moves the
          // arrow + label onto the NPC.
          clueSetNpc(c.act === 'talk' && c.tgt ? (c.tgt + '|' + guideLabel) : '');
          drawClueMap({ x: c.x, y: c.y, p: c.p || 0, mark: ({ talk: 'TALK HERE', search: 'SEARCH HERE', dig: 'DIG HERE' }[c.act] || 'GO HERE') });
        }
      }
      return;
    }
    clueSetNpc('');                                              // picker / no clue -> no NPC outline
    const q = clueEmoteSearch;
    const rows = EMOTE_CLUES.map((e, i) => ({ e, i })).filter(o => (clueEmoteTier < 0 || o.e.t === clueEmoteTier)
      && (!q || o.e.tx.toLowerCase().indexOf(q) >= 0 || o.e.em.toLowerCase().indexOf(q) >= 0 || o.e.it.join(' ').toLowerCase().indexOf(q) >= 0));
    let html = '<div style="margin-bottom:7px;padding:7px 9px;border-radius:7px;background:rgba(255,255,255,0.05);border:1px solid rgba(255,255,255,0.12);font-size:12px;line-height:1.45">'
      + '<b>' + (clueEmoteTier >= 0 ? CLUE_TIERS[clueEmoteTier] + ' ' : '') + 'emote / cryptic clue</b><br>'
      + '<span style="opacity:0.8">Open the scroll in-game and it fills in automatically, or pick it from the list below (' + rows.length + ').</span></div>';
    html += '<input id="emSearch" value="' + esc(q) + '" placeholder="search location / emote / item" style="width:100%;box-sizing:border-box;padding:4px 8px;border-radius:6px;border:1px solid rgba(255,255,255,0.14);background:rgba(0,0,0,0.25);color:inherit;font-size:12px">';
    html += '<div style="max-height:230px;overflow:auto;margin-top:5px">';
    for (const { e, i } of rows.slice(0, 80)) { const loc = e.tx.replace(/\.?\s*Equip.*$/i, '');
      html += '<div data-ei="' + i + '" style="padding:4px 6px;border-radius:5px;cursor:pointer;border-bottom:1px solid rgba(255,255,255,0.05)"><b>' + esc(e.em) + '</b> <span style="opacity:0.55">·</span> ' + esc(loc) + '</div>'; }
    if (!rows.length) html += '<div style="opacity:0.5;padding:6px">No emote clue in the guide for this tier - open the scroll in-game to read it.</div>';
    html += '</div>';
    setHTML(el, html);   // cache: only rebuild when the picker content changes (stops the per-tick flash)
    clueGuide(null); drawClueMap(null);
  }
  function selectClue() {
    clueNoteCompassOpen();          // a live needle identifies an Eastern Lands compass clue
    const c = (activeClueId >= 0) ? CLUE_DATA.find(z => z.i === activeClueId) : null;
    const eel = $('clueEmote'); if (eel && !(c && c.a === 'emote')) { eel.style.display = 'none'; eel._h = ''; }
    if (c && isCompassClue(c)) { compassTick(); return; }   // compass field: needle-driven; the compass owns the map + in-world tile
    // Synthetic entry: not in CLUE_DATA, so c is null here and the fallthrough would clear the
    // mark tetraTick just set.
    if (activeClueId === TETRA_POWERED) { tetraTick(); return; }
    if (c && c.a === 'emote') {                                  // emote clue -> the hidey-hole reference + map
      if (clueEmoteTier !== c.t) { clueEmoteTier = c.t; clueEmoteSel = -1; clueEmoteSearch = ''; }
      renderEmotePanel(); return;
    }
    if (c && c.a === 'scan') {
      const rec = scanSpotsFor(c);
      if (rec && rec.spots && rec.spots.length) {
        const key = rec.key || ('en' + c.en);
        // Eliminations belong to ONE scan-clue instance; a different clue id (even in the same area)
        // starts from scratch.
        if (scanElimOwner[key] !== c.i) { scanElimGet(key).clear(); scanElimOwner[key] = c.i; }
        const elim = scanElimGet(key);
        // 0 candidates is impossible (the treasure is always at one spot) -> over-eliminated, reset.
        if (elim.size >= rec.spots.length) { elim.clear(); }
        const remain = [], idx = [];
        rec.spots.forEach((s, i) => { if (!elim.has(i)) { remain.push(s); idx.push(i); } });
        (async () => { await drawClueMap(scanCentroid(rec), { rec, elim, name: scanTitle(rec) }); scanGuide(remain, rec, idx); })();
      } else { clueGuide(null); drawClueMap(null); }   // enum not resolved yet
      return;
    }
    const t = clueTile(c);
    clueGuide(t, c ? clueLabel(c) : null);
    drawClueMap((t && c && c.a === 'search') ? { x: t.x, y: t.y, p: t.p || 0, mark: 'SEARCH HERE' } : t);   // clue_tele_location = search, not dig
  }
  // In-world guidance: >1 candidate -> mark the remaining candidate spots; exactly one left ->
  // mark that tile as the dig spot.
  async function scanGuide(spots, rec, idx) {
    if (!bridge() || !bridge().guideMarks) return;
    try {
      if (!spots || !spots.length) { bridge().guideMarks(myPid(), ''); return; }
      // Only candidates on the player's CURRENT floor reach the in-world view: in multi-floor scan
      // areas the other floor's spots would linger as tiles drawn at the wrong height.
      const P = await scanPlayerTile();
      if (P) {
        const fs = [], fi = [];
        spots.forEach((s, k) => { if ((s[2] || 0) === P.p) { fs.push(s); fi.push(idx ? idx[k] : k); } });
        if (fs.length) { spots = fs; idx = fi; }
      }
      const cl = (v, hi) => Math.max(0, Math.min(hi, v | 0));
      const enc = (s, lab, rgb) => cl(s[0], 16383) + '\x1f' + cl(s[1], 16383) + '\x1f' + cl(s[2] || 0, 3) + '\x1f' + String(lab).replace(/[\x1e\x1f]/g, ' ').slice(0, 80) + (rgb ? '\x1f0\x1f' + rgb : '');
      let recs;
      if (spots.length === 1) {                              // solved -> mark the actual dig tile (default mark = gets the direction arrow)
        recs = enc(spots[0], scanTitle(rec) + ' DIG HERE');
      } else {                                               // solving -> mark the remaining candidates, COLOURED so none of them takes the direction arrow
// (an arrow to "spot 1" reads as the answer)
        recs = spots.slice(0, 24).map((s, k) => enc(s, 'Scan spot ' + ((idx ? idx[k] : k) + 1) + (rec.key ? ' - ' + scanTitle(rec) : ''), 0xFF2D95)).join('\x1e');
      }
      bridge().guideMarks(myPid(), recs);
    } catch (e) {}
  }
