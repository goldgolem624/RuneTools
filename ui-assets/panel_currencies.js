// RuneToolsX panel: Currencies (the currency pouch as a ledger: balance vs cap per currency).
// Spliced inline into client.html; IIFE (window exports + registerTab; see the RTX registry in client.html).
// DBTable 66 (master id 66, so dbRows(66) directly) is the game's own currency registry.
// Column meanings, per the CS2 scripts that read them:
//   col 0 currency id (scripts 14966/12774), col 1 backing type (0 = var via script14966,
//   1 = item via script14967), col 2 cap (script3517 fallback), col 3 graphic, col 4 obj item
//   (script14967 INV_TOTAL), col 5 name override (script14963: override else OC_NAME(obj)),
//   col 6 in-pouch flag (script12774 lists rows via db_find_with_count(270432, 1)), col 7 pouch
//   tab 1-3 (script12774's section switch; anything else lands in the default section).
// Item-backed balances = container 889 total + backpack 93 total (script14964 adds
// INV_TOTAL(93, obj) on top of script14967's INV_TOTAL(889, obj)).
// BAKED (these mappings exist ONLY in clientscript switches, not in any cache config):
//   CY_VARS: the dbrow -> backing-var switch of clientscript-14966 (currency_pouch_value_get),
//     all 76 cases verbatim ('p' = varp, 'b' = varbit). Regenerate from that script after game
//     updates; a new currency row missing here degrades to a "Status unknown" pill.
//   Hidden cap overrides from clientscript-3517: dbrow 2079 cap = enum 11420[varbit 33143]
//     (Waiko upgrades); dbrow 2254 cap = 4 * (10 + BaseThieving / 9). Every other cap is db 66.2.
//   Livid Farm produce (dbrow 2274, varbit 16372) is STORED divided by 10; the game multiplies
//     by 10 at display time (clientscript-12774, clientscript-3519).
(function () {

  // dbrow id -> backing var, verbatim from clientscript-14966. 'pN' = varplayer N, 'bN' = varbit N.
  const CY_VARS = {
    2065: 'b41706', 2066: 'p6526', 2067: 'p6528', 2068: 'p6529', 2069: 'p6533', 2070: 'p6535',
    2071: 'b22905', 2072: 'b9071', 2073: 'b520', 2074: 'b16530', 2075: 'b40241', 2076: 'b16394',
    2078: 'b30174', 2079: 'b33126', 2080: 'b44353', 2081: 'b44178', 2082: 'b44559',
    2083: 'b34327', 2084: 'b34328', 2085: 'b35115', 2086: 'b36270', 2087: 'b38991', 2088: 'b41472',
    2251: 'b41707', 2252: 'b4662', 2253: 'b4163', 2254: 'b9047', 2255: 'b34807', 2256: 'p7794',
    2257: 'p5427', 2258: 'p6527', 2259: 'p6530', 2260: 'p6531', 2261: 'p6532', 2262: 'p6534',
    2263: 'b4518', 2264: 'b4526', 2265: 'b4524', 2266: 'b4525', 2267: 'b5544', 2268: 'b5213',
    2269: 'p1628', 2270: 'b3881', 2271: 'b3882', 2272: 'b3884', 2273: 'b3885', 2274: 'b16372',
    2275: 'b5047', 2276: 'b5608', 2277: 'b4861', 2278: 'b5136', 2279: 'b15398', 2280: 'b15399',
    2281: 'b15400', 2282: 'b15401', 2283: 'b15402', 2284: 'b28157', 2285: 'p5835', 2286: 'b22200',
    2287: 'b21658', 2288: 'p6520', 2290: 'b35842', 2291: 'p6985', 2426: 'b45689', 3580: 'b48602',
    4084: 'b49371', 4971: 'b50552', 5311: 'b50819', 6943: 'b52229', 12987: 'b54788',
    12994: 'p11584', 13286: 'b55169', 13287: 'b55275', 14411: 'b56081', 18102: 'p1097',
    18385: 'b61090' };

  // Outfit-piece "created" flags, parsed verbatim from clientscript-6831: the gate behind the
  // Create-Item interface (group 1371) returns 2 ("already created") for these item ids when the
  // varbit reads >= 1 (both its '== 1' and '> 0' case forms; the three '== 2' cases and the ones
  // gated on helper scripts are deliberately left out). BAKED because the item -> varbit pairing
  // exists only in that switch. Which pieces belong to which fragment currency is NOT baked: it
  // is derived at runtime by cache-name association (see cyLoadPieces).
  // Regenerate from clientscript-6831 after game updates.
  const CY_CREATED = {
    23665:4370, 23671:4382, 23672:4381, 23673:4380, 23674:26892, 23675:4376, 23676:4377, 23677:4378,
    23678:4379, 23679:34974, 23680:34977, 23681:34976, 23682:34975, 23683:34978, 23684:34987, 23685:34988,
    23686:34989, 23687:34991, 23688:34992, 23689:35002, 23690:34979, 23691:35003, 23692:34999, 23693:35001,
    23694:35000, 23695:35004, 23696:35005, 23697:35007, 23698:35009, 23699:35008, 23700:35006, 24294:4383,
    24296:4384, 24297:4385, 24298:4386, 24317:4553, 24324:4392, 24325:4393, 24326:4394, 24327:4395,
    24328:4396, 24329:4387, 24330:4388, 24331:4389, 24332:4390, 24333:4391, 24713:18232, 24714:18232,
    24715:18232, 24716:18232, 24717:18232, 24718:18232, 24719:18232, 24720:18232, 24721:18232, 24722:18232,
    24723:18232, 24724:18232, 24725:18232, 24726:18232, 24727:18232, 24728:18232, 24729:18232, 24730:18232,
    24731:18232, 24732:18232, 24733:18232, 24734:18232, 24735:18232, 24736:18232, 24737:18232, 24738:18232,
    24739:18232, 24740:18232, 24741:18232, 24742:18232, 24743:18232, 24744:18232, 24745:18232, 24746:18232,
    24747:18232, 24748:18232, 24749:18232, 24750:18232, 24751:18232, 24752:18232, 24753:18232, 24754:18232,
    24755:18232, 24756:18232, 24757:18232, 24758:18232, 24759:18232, 24760:18232, 24761:18232, 24762:18232,
    24847:1187, 24848:4401, 24849:18231, 24850:18231, 24851:18231, 24852:18231, 24924:4400, 24925:4400,
    24926:4397, 24927:4397, 24928:4398, 24929:4398, 24930:4399, 24931:4399, 25056:18400, 25180:4403,
    25181:4404, 25182:4405, 25183:4406, 25184:4407, 25185:4408, 25186:4409, 25187:4410, 25188:4411,
    25189:4412, 25190:4413, 25191:4414, 25192:4415, 25193:4416, 25194:4417, 25195:4418, 25196:4419,
    25197:4420, 25198:4421, 25199:4422, 25370:4423, 25371:4424, 25952:35004, 26192:18232, 26193:18232,
    26194:18232, 26195:18232, 26196:18232, 26197:18232, 26198:18232, 26491:17559, 27148:17831, 27149:17832,
    27150:17833, 27151:17834, 27152:17835, 27366:17980, 27367:17981, 27368:18233, 27369:18233, 27370:18233,
    27371:18233, 27372:18233, 27373:18233, 27375:18234, 27376:18234, 27377:18234, 27378:18234, 27379:18234,
    27380:18234, 27381:18234, 27382:18234, 27383:18234, 27384:18234, 27385:18234, 27386:18234, 27586:18183,
    27587:12649, 27588:18179, 27589:18180, 27590:18181, 27591:18182, 27616:18235, 27618:18239, 27620:18243,
    27622:18247, 27624:18251, 27626:18257, 27627:18259, 27628:18258, 27629:18260, 27630:18261, 27631:18262,
    27632:18264, 27633:18263, 27634:18265, 27635:18266, 28017:18292, 28018:18293, 28019:18294, 28020:18295,
    28021:18296, 28022:18297, 28023:18298, 28024:18299, 28025:18300, 28026:18301, 28027:18302, 28028:18303,
    28083:35004, 28095:18348, 28099:18349, 28103:18350, 28145:18376, 28165:18377, 28166:18378, 28167:18379,
    28168:18380, 28169:18381, 28170:18382, 28171:18383, 28172:18384, 28173:18385, 28174:18386, 28186:18398,
    28384:18461, 28388:18462, 28392:18463, 28396:18464, 28400:18465, 28402:18466, 28404:18467, 28408:18468,
    28410:18469, 28412:18470, 28414:18471, 28416:18472, 28420:18473, 28422:18474, 28424:18475, 28483:18488,
    28488:18489, 28493:18490, 28498:18492, 28503:18491, 28508:18493, 28686:18608, 28688:18612, 28690:18616,
    28692:18620, 28694:18624, 28859:19942, 28889:19946, 28995:20086, 28996:20087, 28997:20088, 28998:20089,
    28999:20090, 29000:20093, 29553:18232, 29554:18232, 29641:17560, 29865:20696, 29866:20697, 29867:20698,
    29868:20700, 29869:20699, 29970:20792, 29971:20793, 29972:20792, 29973:20793, 30750:21397, 30755:21399,
    30760:21398, 30813:21471, 30814:34990, 30815:34993, 30816:34994, 30817:34995, 30818:34973, 30819:34993,
    30820:34994, 30821:34995, 30822:34990, 30823:34973, 30839:21618, 30840:21619, 30841:21625, 30842:21625,
    30843:21625, 30844:21625, 30845:21625, 30846:21625, 30847:21625, 30848:21625, 30849:21625, 30850:21625,
    30851:21625, 30852:21625, 30920:21712, 31089:21927, 31091:21931, 31093:21935, 31095:21939, 31097:21943,
    31099:21947, 31199:22261, 31200:22262, 31201:22263, 31338:22410, 31339:22411, 31340:22412, 31341:22413,
    31342:22414, 31343:22415, 31344:22416, 31345:22417, 31346:22418, 31347:22419, 31348:22420, 31392:22474,
    31393:22475, 31394:22476, 31395:22477, 31553:22668, 31559:22672, 31567:22673, 31575:22677, 31576:22678,
    31577:22679, 31578:22680, 31579:22681, 31580:22682, 31581:22683, 31582:22684, 31583:22685, 31584:22686,
    31585:22687, 31586:22688, 31587:22689, 31588:22690, 31589:22691, 31590:22692, 31591:22693, 31592:22694,
    31593:22695, 31594:22696, 31612:22773, 31759:34974, 31760:34977, 31761:34976, 31762:34975, 32097:24846,
    32098:24846, 32099:24851, 32100:24851, 32101:24845, 32102:24847, 32103:24847, 32104:24847, 32105:24847,
    32106:24847, 32107:24847, 32108:24847, 32109:24850, 32110:24850, 32111:24850, 32112:24850, 32274:25192,
    32275:25204, 32276:25200, 32277:25196, 32278:25193, 32279:25205, 32280:25201, 32281:25197, 32342:25424,
    32343:25425, 32344:25426, 32345:25427, 32346:25428, 32347:25429, 32348:25430, 32349:25431, 32350:25432,
    32351:25433, 32352:25434, 32353:25435, 32354:25436, 32355:25437, 32356:25438, 32357:25439, 32358:25440,
    32359:25441, 32360:25442, 32361:25443, 32441:25495, 32442:25495, 32443:25495, 32444:25495, 32445:25495,
    32446:25489, 32447:25489, 32448:25489, 32449:25489, 32450:25489, 32451:25486, 32452:25486, 32453:25486,
    32454:25486, 32455:25486, 32456:25487, 32457:25487, 32458:25487, 32459:25487, 32460:25487, 32461:25488,
    32462:25488, 32463:25488, 32464:25488, 32465:25488, 32466:25485, 32467:25485, 32468:25485, 32469:25485,
    32470:25485, 33969:26852, 33975:26853, 33981:26854, 33993:26855, 34005:26856, 34006:26857, 34009:26858,
    34028:26884, 34029:26885, 34030:26886, 34037:26887, 34038:26888, 34039:26889, 34049:26890, 34059:26891,
    34069:34984, 34071:35011, 34073:34985, 34075:35012, 34077:34981, 34079:35013, 34081:34980, 34083:34986,
    34085:34983, 34087:34982, 34089:35014, 34091:34996, 34093:34998, 34095:34997, 34200:27093, 34201:27094,
    34202:27095, 34203:27096, 34204:27097, 34205:27098, 34206:27099, 34207:27100, 34208:27101, 34209:27102,
    34210:27103, 34211:27104, 34212:27105, 34213:27106, 34214:27107, 34215:27108, 34216:27109, 34217:27110,
    34218:27111, 34219:27112, 34859:35010, 34919:28297, 34920:28301, 34921:28305, 34922:28309, 34923:28298,
    34924:28302, 34925:28306, 34926:28310, 35277:26733, 35279:26737, 35281:26741, 35283:26745, 35285:28862,
    35287:28866, 35289:28870, 35357:29032, 35358:29031, 35359:29033, 35360:29034, 35361:29035, 35362:29037,
    35363:29036, 35364:29038, 35365:29039, 35366:29040, 35587:29355, 35588:29342, 35589:29344, 35590:29343,
    35591:29340, 35592:29341, 35593:29347, 35594:29349, 35595:29348, 35596:29345, 35597:29346, 35598:29352,
    35599:29354, 35600:29353, 35601:29350, 35602:29351, 35766:29512, 35963:29767, 35964:29768, 35965:29769,
    35966:29770, 35967:29771, 35968:29777, 35969:29778, 35970:29779, 35971:29780, 35972:29781, 35973:29772,
    35974:29773, 35975:29774, 35976:29775, 35977:29776, 35978:29782, 35979:29783, 35980:29784, 35981:29785,
    35982:29786, 36117:29969, 36118:29970, 36119:29971, 36890:30607, 36891:30663, 36892:30660, 36893:30662,
    36894:30661, 36895:30664, 36896:30668, 36897:30665, 36898:30667, 36899:30666, 37119:30967, 37123:30968,
    37125:30969, 37129:30970, 37130:30971, 37131:30972, 37132:30973, 37134:30975, 37343:31305, 37344:31306,
    37345:31307, 37346:31308, 37347:31309, 37348:31315, 37349:31316, 37350:31317, 37351:31318, 37352:31319,
    37353:31310, 37354:31311, 37355:31312, 37356:31313, 37357:31314, 37358:31320, 37359:31321, 37360:31322,
    37361:31323, 37362:31324, 37837:18232, 37838:18232, 37841:33347, 37842:33346, 37843:33348, 37844:33349,
    37845:33350, 37846:33352, 37847:33351, 37848:33353, 37849:33354, 37850:33355, 38139:33676, 38141:33677,
    38143:33678, 38145:33679, 38147:33680, 38149:33681, 38151:33682, 38521:33829, 38522:33830, 38523:33832,
    38524:33831, 38525:33833, 38526:33834, 38527:33835, 38528:33837, 38529:33836, 38530:33838, 38531:33839,
    38532:33840, 38533:33842, 38534:33841, 38535:33843, 38536:33844, 38537:33845, 38538:33847, 38539:33846,
    38540:33848, 38541:33849, 38542:33850, 38543:33852, 38544:33851, 38545:33853, 39162:35085, 39730:35085,
    39731:35086, 39732:35087, 39733:35088, 39734:35089, 39735:35090, 39736:35091, 39737:35092, 39738:35093,
    39739:35094, 39740:35095, 39741:35096, 39742:35097, 39743:35098, 39744:35099, 39745:35100, 39746:35101,
    39747:35102, 39748:35103, 39749:35104, 39869:35230, 39871:35231, 39873:35232, 39875:35233, 39877:35234,
    39879:35235, 39881:35236, 39883:35237, 39885:35238, 40517:36400, 40518:36401, 40519:36402, 40520:36406,
    40521:36408, 40522:36434, 40523:36403, 40524:36405, 40527:36409, 40530:36412, 40533:36410, 40536:36411,
    40539:36413, 40542:36416, 40545:36414, 40548:36415, 40551:36417, 40554:36420, 40557:36418, 40560:36419,
    40563:36421, 40566:36424, 40569:36422, 40572:36423, 40575:36425, 40578:36428, 40581:36426, 40584:36427,
    40587:36429, 40590:36432, 40593:36430, 40596:36431, 40730:36871, 41008:37008, 41009:37009, 41010:37011,
    41011:37010, 41012:37012, 41013:37013, 41014:37014, 41015:37016, 41016:37015, 41017:37017, 41018:37018,
    41019:37019, 41020:37021, 41021:37020, 41022:37022, 41023:37023, 41024:37024, 41025:37026, 41026:37025,
    41027:37027, 41240:38552, 41241:38552, 41242:38552, 41243:38552, 41244:38552, 41245:38553, 41246:38553,
    41247:38553, 41248:38553, 41249:38553, 41250:38554, 41251:38554, 41252:38554, 41253:38554, 41254:38554,
    41255:38546, 41256:38546, 41257:38546, 41258:38546, 41259:38546, 41260:38547, 41261:38547, 41262:38547,
    41263:38547, 41264:38547, 41265:38548, 41266:38548, 41267:38548, 41268:38548, 41269:38548, 41270:38549,
    41271:38549, 41272:38549, 41273:38549, 41274:38549, 41275:38550, 41276:38550, 41277:38550, 41278:38550,
    41279:38550, 41280:38551, 41281:38551, 41282:38551, 41283:38551, 41284:38551, 42641:40377, 42642:40378,
    42643:40379, 42644:40380, 42645:40381, 42646:40382, 42647:40383, 42648:40384, 42649:40385, 42650:40386,
    42651:40387, 42652:40388, 42653:40389, 42654:40390, 42655:40391, 42695:40520, 42696:40520, 42697:40520,
    44002:41669, 44003:41670, 44004:41671, 44005:41672, 44006:41673, 44007:41664, 44008:41665, 44009:41666,
    44010:41667, 44011:41668, 44012:41659, 44013:41660, 44014:41661, 44015:41662, 44016:41663, 44017:41674,
    44018:41675, 44019:41676, 44020:41677, 44021:41678, 44376:33809, 44377:41276, 44378:42340, 44379:42341,
    44380:42342, 44381:42343, 44486:42869, 44490:42872, 44491:42872, 44492:42872, 44493:42872, 44494:42872,
    48408:1002, 48411:1003, 48414:20968, 48417:20969, 48420:20970, 48517:45244, 48518:45245, 48519:45246,
    48520:45247, 48521:45249, 48522:45248, 48523:45250, 48545:45294, 48546:45295, 50200:47392, 50201:47393,
    50202:47394, 50203:47395, 50204:47397, 50205:47396, 50206:47398, 51017:48606, 51018:48607, 51019:48608,
    51020:48609, 51021:48610, 57695:56238, 57696:56239, 57697:56233, 57698:56234, 57699:56235, 57700:56236,
    57701:56237 };

  const CY_VP_IDS = Object.values(CY_VARS).filter(d => d[0] === 'p').map(d => d.slice(1)).join(',');
  // + 33143: the Waiko-upgrade tier that indexes the dbrow-2079 cap enum (script3517).
  const CY_VB_IDS = Object.values(CY_VARS).filter(d => d[0] === 'b').map(d => d.slice(1)).concat(['33143']).join(',');

  cyRows = null; let cyRowsLoading = false;   // in-pouch registry rows from dbRows(66)
  let cyVp = null, cyVb = null;               // backing var values
  let cyCapEnum = null;                       // enum 11420: vb33143 tier -> dbrow-2079 cap
  let cyPouchInv = null, cyPackInv = null;    // containerItems 889 / 93 (item-backed balances)
  let cyFetching = false, cyFetchAt = 0, cySig = '';
  const cySprites = {};                       // graphic id -> data-url ('' = pending/none)
  // Fragment currency -> outfit pieces, derived at runtime over the CY_CREATED piece list: a
  // piece belongs to a fragment currency when the fragment's registry obj appears in its
  // materials (item params 2650-2654, or struct params 2655-2664 of the material-group structs
  // in 2990-2994).
  let cyPieces = null, cyPiecesLoading = false;   // registry ROW id -> [{item, vb}]
  let cyPieceVbIds = '';                          // created-state varbits joined to the batch read
  const cyPieceNames = {};                        // piece item id -> cache name

  async function cyLoadPieces() {
    if (cyPieces || cyPiecesLoading || !cyRows) return;
    if (!bridge().itemInfo) return;
    cyPiecesLoading = true;
    try {
      // The item-param recipe walk (params 2650-54 / 2990-94, clientscript-7113/7114) is DEAD DATA in
      // the live game: a full offline scan of every js5-19 item found ZERO items carrying param 2650
      // or 2990, so the create-panel materials are server-driven and no client table ties a fragment
      // currency to its pieces. The association is therefore derived from CACHE NAMES: each fragment
      // row's name stem (the words before "fragments") is matched into the cache names of the 825
      // gate-listed pieces (CY_CREATED). Both sides are cache ground-truth strings.
      // stem = the words before "fragments"; word = its last word alone, the fallback for possessive
      // or prefixed currency names ("Nature's sentinel" pieces are named "Oaken sentinel helm" etc.).
      const stems = [];   // [{rowId, stem, word}]
      for (const r of cyRows) {
        const m = /^(.*?)\s+fragments?\b/i.exec(r.name || '');
        if (!m || !m[1].trim()) continue;
        let stem = m[1].trim().toLowerCase();
        // The Dungeoneering fragments outfit is the Gorajan trailblazer: the one family whose outfit
        // name shares no token with its fragment name.
        if (stem === 'dungeoneering') stem = 'trailblazer';
        const w = stem.split(/\s+/);
        stems.push({ rowId: r.row, stem, word: w[w.length - 1] });
      }
      const out = {};
      if (stems.length) {
        // One-time scan (~825 cached itemInfo reads, async, off the render path).
        for (const key of Object.keys(CY_CREATED)) {
          const piece = +key;
          let nm = '';
          try { nm = (JSON.parse(await rtxData.raw('cache.itemInfo', piece) || 'null') || {}).name || ''; }
          catch (e) {}
          if (!nm) continue;
          cyPieceNames[piece] = nm;
          // Skill pendants sit in the same creation gate but are not outfit pieces.
          if (/\bpendant of\b/i.test(nm)) continue;
          // Gate-listed but NOT part of the obtainable sets: the Shark fist wraps and the Ghostly farmer
          // variant are unobtainable legacy entries.
          if (/^shark fist\b/i.test(nm) || /^ghostly farmer\b/i.test(nm)) continue;
          const low = ' ' + nm.toLowerCase().replace(/[^a-z0-9']+/g, ' ') + ' ';
          // full-phrase stems get first claim on a piece; last-word fallback second
          let hit = stems.find(s => low.indexOf(' ' + s.stem + ' ') >= 0)
                 || stems.find(s => low.indexOf(' ' + s.word + ' ') >= 0);
          if (hit) (out[hit.rowId] = out[hit.rowId] || []).push({ item: piece, vb: CY_CREATED[key] });
        }
      }
      // Zero matches with fragment rows present means an itemInfo hiccup mid-scan -- leave null so
      // the next fetch retries instead of latching the fallback note forever.
      if (stems.length && !Object.keys(out).length) { cyPiecesLoading = false; return; }
      cyPieces = out;
      const ids = new Set();
      for (const k in out) for (const pc of out[k]) ids.add(pc.vb);
      cyPieceVbIds = Array.from(ids).join(',');
      cySig = '';
    } catch (e) { /* retry next fetch */ }
    cyPiecesLoading = false;
  }

  async function cyLoadRegistry() {
    if (cyRows || cyRowsLoading) return;
    if (!bridge().dbRows || !bridge().itemInfo) return;
    cyRowsLoading = true;
    try {
      const rows = JSON.parse(await rtxData.raw('cache.dbRows', 66) || 'null');
      if (!Array.isArray(rows) || !rows.length) return;   // cache not open yet; retry next fetch
      const out = [];
      for (const r of rows) {
        const I = r.i || {}, S = r.s || {};
        const col = (m, k) => (m[k] && m[k][0] !== undefined) ? m[k][0] : null;
        if ((col(I, '6') | 0) !== 1) continue;            // not in the pouch (66.6 flag)
        const obj = col(I, '4');
        let name = col(S, '5') || '';
        if (!name && obj > 0) {                           // script14963: override else OC_NAME(obj)
          try { const d = JSON.parse(await rtxData.raw('cache.itemInfo', obj) || 'null'); name = (d && d.name) || ''; } catch (e) {}
        }
        out.push({
          row: r.f | 0,                                   // dbrow id = the CY_VARS key
          type: col(I, '1') | 0,                          // 0 var-backed, 1 item-backed
          cap: col(I, '2') | 0,
          graphic: col(I, '3') | 0,
          obj: obj > 0 ? obj : 0,
          tab: col(I, '7') | 0,
          name: name || ('Currency #' + (r.f | 0)),
        });
      }
      if (out.length) cyRows = out;
    } catch (e) { /* retry next fetch */ }
    cyRowsLoading = false;
  }

  async function fetchCurrencies() {
    if (!bridge() || !bridge().varps || !bridge().varbits || cyFetching) return;
    const t = Date.now();
    if (t - cyFetchAt < 2000) return;
    cyFetchAt = t; cyFetching = true;
    try {
      await cyLoadRegistry();
      cyLoadPieces();   // deliberately not awaited: the ~825-read name scan fills in async
      try { const d = JSON.parse(await rtxData.raw('state.varps', CY_VP_IDS) || 'null');
            if (d && typeof d === 'object' && Object.keys(d).length) cyVp = d; } catch (e) {}
      try { const d = JSON.parse(await rtxData.raw('state.varbitsCsv', CY_VB_IDS + (cyPieceVbIds ? ',' + cyPieceVbIds : '')) || 'null');
            if (d && typeof d === 'object' && Object.keys(d).length) cyVb = d; } catch (e) {}
      if (!cyCapEnum && bridge().enumInfo) {
        try { const m = JSON.parse(await rtxData.raw('cache.enumInfo', 11420) || 'null');
              if (m && Object.keys(m).length) cyCapEnum = m; } catch (e) {}
      }
      if (bridge().containerItems) {
        try { cyPouchInv = JSON.parse(await rtxData.raw('state.container', 889) || 'null'); } catch (e) {}
        try { cyPackInv = JSON.parse(await rtxData.raw('state.container', 93) || 'null'); } catch (e) {}
      }
    } catch (e) { /* keep previous */ }
    cyFetching = false;
    paneRun('currencies', renderCurrencies);
  }

  const cyInvTotal = (inv, obj) => {
    if (!inv || !inv.present || !Array.isArray(inv.items) || !obj) return 0;
    let n = 0;
    for (const it of inv.items) if ((it[1] | 0) === obj) n += it[2] | 0;   // [slot,id,stack,name]
    return n;
  };
  // Balance: number, or null = item-backed but container 889 not readable yet, or undefined =
  // var-backed with no baked mapping (Status unknown).
  function cyBalance(r) {
    if (r.type === 1) {
      if (!cyPouchInv || !cyPouchInv.present) return null;
      return cyInvTotal(cyPouchInv, r.obj) + cyInvTotal(cyPackInv, r.obj);
    }
    const d = CY_VARS[r.row];
    if (!d) return undefined;
    const v = d[0] === 'p' ? (cyVp && cyVp[d.slice(1)]) : (cyVb && cyVb[d.slice(1)]);
    let n = v | 0;
    if (r.row === 2274) n *= 10;   // Livid Farm produce stored /10 (scripts 12774/3519)
    return n;
  }
  // Cap: db 66.2, except the two script3517 overrides. null = no cap known.
  function cyCap(r) {
    // Livid Farm (2274): the balance is displayed x10 (stored /10) but no script scales or even
    // renders the db 66.2 cap for this row, so the cap's units are unverifiable and a live balance
    // can exceed the raw column. Show the bare balance rather than claim a cap.
    if (r.row === 2274) return null;
    if (r.row === 2079) {
      const tier = (cyVb && cyVb['33143']) | 0;
      const c = cyCapEnum && (cyCapEnum[String(tier)] | 0);
      return c > 0 ? c : null;
    }
    if (r.row === 2254) {
      const th = (lastSnap && Array.isArray(lastSnap.skills) && lastSnap.skills[17])
                 ? (lastSnap.skills[17][0] | 0) : 0;   // base Thieving (STAT_BASE(17))
      return th > 0 ? 4 * (10 + Math.floor(th / 9)) : null;
    }
    return r.cap > 0 ? r.cap : null;
  }
  // Neutral placeholder (dashed ring) for rows with no renderable icon: an empty box reads as a
  // rendering bug.
  const CY_PLACEHOLDER = 'data:image/svg+xml;utf8,' + encodeURIComponent(
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="22" height="22">' +
    '<circle cx="12" cy="12" r="8.5" fill="none" stroke="#666a72" stroke-width="1.6" stroke-dasharray="3 2.6"/></svg>');
  // Sprite fetch states: undefined = never asked, '' = in flight, 'x' = decode failed
  // (placeholder stays), else the data-url.
  function cySpriteFetch(gid) {
    if (cySprites[gid] !== undefined || !bridge() || !bridge().sprite) return;
    cySprites[gid] = '';
    (async () => {
      try {
        // 44px = 2x the 22px box, the same clean-rescale convention as the other panels.
        const url = await bridge().sprite(gid, 44);
        cySprites[gid] = url || 'x';
        if (url) document.querySelectorAll('.cy-ico[data-spr="' + gid + '"]').forEach(n => setIconBg(n, url));
        else console.log('[currencies] sprite ' + gid + ' has no renderable image (js5-8 decode empty)');
      } catch (e) { cySprites[gid] = 'x'; }
    })();
  }

  function renderCurrencies() {
    const c = $('content');
    let wrap = $('cyWrap');
    if (!wrap) {
      injectStyle('cyCss', `
          .cy-sec { margin: 2px 12px 0; }
          .cy-st { display: flex; align-items: center; gap: 8px; padding: 10px 2px 6px; }
          .cy-tt { flex: 1; color: var(--accent-hi); font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: .05em; }
          .cy-card { background: var(--bg-elev); border: 1px solid var(--border); border-radius: 10px; }
          .cy-row { display: flex; align-items: center; gap: 10px; padding: 6px 12px; }
          .cy-row + .cy-row { border-top: 1px solid var(--border); }
          .cy-ico { flex: 0 0 auto; width: 22px; height: 22px; background-repeat: no-repeat; background-position: center; background-size: contain; }
          .cy-nm { flex: 1; min-width: 0; color: var(--text); font-size: 12px; }
          .cy-nm > div:first-child { white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
          .cy-sub { color: var(--text-mute); font-size: 10.5px; margin-top: 1px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
          .cy-pill { font-size: 10.5px; padding: 2px 9px; border-radius: 999px; border: 1px solid var(--border); color: var(--text-dim); white-space: nowrap; font-variant-numeric: tabular-nums; }
          .cy-pill.no { color: #e05656; border-color: rgba(224, 86, 86, .45); }
          .cy-pill.ok { color: var(--ok); border-color: rgba(77, 210, 138, .45); }
          .cy-empty { margin: 10px 14px; color: var(--text-dim); font-size: 12px; }`);
      c.innerHTML = ''; cySig = '';
      wrap = document.createElement('div'); wrap.id = 'cyWrap'; wrap.className = 'pane'; c.appendChild(wrap);
    }
    if (!cyRows) {
      wrap.innerHTML = '<div class="cy-empty">Reading the currency registry from the cache... (be in-world)</div>';
      cySig = ''; return;
    }
    if (!cyVp && !cyVb) {
      wrap.innerHTML = '<div class="cy-empty">Reading currency balances... (be in-world)</div>';
      cySig = ''; return;
    }
    const sig = cyRows.map(r => {
      const b = cyBalance(r);
      const pcs = cyPieces ? cyPieces[r.row] : null;
      const md = pcs ? pcs.filter(pc => ((cyVb && cyVb[String(pc.vb)]) | 0) >= 1).length + '/' + pcs.length : '';
      return r.row + ':' + (b === undefined ? 'u' : b === null ? 'n' : b) + ':' + (cyCap(r) || 0) + ':' + md;
    }).join(',');
    if (sig === cySig) return;
    cySig = sig;
    wrap.innerHTML = '';

    const sec = title => {
      const s = document.createElement('div'); s.className = 'cy-sec';
      const h = document.createElement('div'); h.className = 'cy-st';
      const t = document.createElement('div'); t.className = 'cy-tt'; t.textContent = title; h.appendChild(t);
      s.appendChild(h);
      const card = document.createElement('div'); card.className = 'cy-card'; s.appendChild(card);
      wrap.appendChild(s);
      return card;
    };
    // The pouch UI's own section order (script12774 renders default, then tabs 1..3). The game gives
    // these sections no derivable caption, so the labels stay literal.
    const groups = [
      ['Pouch', cyRows.filter(r => r.tab < 1 || r.tab > 3)],
      ['Pouch tab 1', cyRows.filter(r => r.tab === 1)],
      ['Pouch tab 2', cyRows.filter(r => r.tab === 2)],
      ['Pouch tab 3', cyRows.filter(r => r.tab === 3)],
    ];
    for (const [label, rows] of groups) {
      if (!rows.length) continue;
      rows.sort((a, b) => a.name.localeCompare(b.name));
      const card = sec(label + ' · ' + rows.length);
      for (const r of rows) {
        const bal = cyBalance(r);
        let cap = cyCap(r);
        // A cap the balance already exceeds is a false claim (wrong units or stale column): drop it and
        // show the bare balance.
        if (cap != null && typeof bal === 'number' && bal > cap) cap = null;
        const el = document.createElement('div'); el.className = 'cy-row';
        const ico = document.createElement('div'); ico.className = 'cy-ico';
        // Icon chain: packed item icon -> cache sprite (col 3 graphic) -> neutral placeholder.
        let iconed = false;
        if (r.obj > 0) { const u = resolveIcon(r.obj); if (u) { setIconBg(ico, u); iconed = true; } }
        if (!iconed && r.graphic > 0) {
          ico.dataset.spr = String(r.graphic);
          const s = cySprites[r.graphic];
          if (s && s !== 'x') { setIconBg(ico, s); iconed = true; }
          else if (s === undefined) { cySpriteFetch(r.graphic); iconed = true; }   // fills async
          else if (s === '') iconed = true;                                        // in flight
        }
        if (!iconed) setIconBg(ico, CY_PLACEHOLDER);
        el.appendChild(ico);
        const nm = document.createElement('div'); nm.className = 'cy-nm';
        const t = document.createElement('div'); t.textContent = r.name; nm.appendChild(t);
        // Fragment currencies: outfit-piece created state from baked CY_CREATED (clientscript-6831)
        // crossed with the runtime name match (cyLoadPieces). A 0 balance with every piece created
        // renders a green Constructed pill; fragments whose scan finds no pieces keep the "may already
        // be constructed" note instead of a guessed claim.
        const isFrag = /fragment/i.test(r.name);
        const pieces = cyPieces ? (cyPieces[r.row] || null) : null;
        const made = pieces ? pieces.filter(pc => ((cyVb && cyVb[String(pc.vb)]) | 0) >= 1).length : 0;
        const allMade = pieces && pieces.length > 0 && made === pieces.length;
        const maybeConstructed = bal === 0 && isFrag && !(pieces && pieces.length);
        let sub = '';
        if (r.row === 2274) sub = 'shown ×10 (the game stores this /10)';
        else if (r.row === 2079) sub = 'cap grows with Waiko upgrades';
        else if (r.row === 2254) sub = 'cap grows with Thieving (4 × (10 + lvl/9))';
        else if (pieces && pieces.length && !allMade) sub = 'outfit pieces created ' + made + ' / ' + pieces.length;
        else if (maybeConstructed) sub = 'may already be constructed';
        else if (r.type === 1) sub = 'item-backed (pouch + backpack)';
        if (sub) { const sb = document.createElement('div'); sb.className = 'cy-sub'; sb.textContent = sub; nm.appendChild(sb); }
        el.appendChild(nm);
        const p = document.createElement('span'); p.className = 'cy-pill';
        if (allMade && bal === 0) { p.textContent = 'Constructed ' + made + ' / ' + pieces.length; p.classList.add('ok'); }
        else if (bal === undefined) p.textContent = 'Status unknown';
        else if (bal === null) p.textContent = 'not synced';
        else if (cap != null) {
          p.textContent = bal.toLocaleString() + ' / ' + cap.toLocaleString();
          if (bal >= cap * 0.9) p.classList.add('no');   // near/at cap
        } else p.textContent = bal.toLocaleString();
        el.appendChild(p);
        const d = CY_VARS[r.row];
        el.dataset.tip = r.name +
          '\nSource: ' + (r.type === 1
            ? 'items in the pouch container (889) + backpack (93)'
            : d ? (d[0] === 'p' ? 'varp ' + d.slice(1) : 'varbit ' + d.slice(1))
                : 'no known var (not in clientscript-14966; shows after a mapping update)') +
          (cap != null ? '\nCap: ' + cap.toLocaleString() +
            (r.row === 2079 ? ' (enum 11420 at Waiko tier ' + ((cyVb && cyVb['33143']) | 0) + ')'
             : r.row === 2254 ? ' (from base Thieving)' : ' (registry column 2)') : '') +
          (r.row === 2274 ? '\nNo cap shown: the registry cap for this row does not match the x10 display units.' : '') +
          (pieces && pieces.length
            ? '\n\nOutfit pieces (created state from the game\'s Create-Item gate, clientscript-6831):\n' +
              pieces.map(pc => '  ' + (cyPieceNames[pc.item] || ('Item ' + pc.item)) +
                ' - ' + (((cyVb && cyVb[String(pc.vb)]) | 0) >= 1 ? 'created' : 'not created')).join('\n')
            : '') +
          (maybeConstructed ? '\n0 can also mean the outfit was already fully constructed;\nno cache name ties this fragment to gate-listed pieces.' : '') +
          (bal === null ? '\nContainer 889 is not readable yet on this client.' : '');
        card.appendChild(el);
      }
    }
    sizeAllIcons();
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { CY_VARS, cyLoadRegistry, fetchCurrencies });
registerTab({ id: 'currencies', render: renderCurrencies, open: function () { cySig = ''; fetchCurrencies(); } });
})();
