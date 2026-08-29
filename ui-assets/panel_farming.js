// RuneToolsX panel: Farming patch tracker + tool leprechaun.
// Spliced inline into client.html; IIFE (window exports + registerTab; see the RTX registry in client.html).
(function () {

  // Each patch is a varbit (bit-slice of a varp) whose value indexes a per-patch-type state enum;
  // defs/enums/locations come from the cache, varp values read live via rtx.varps.
  const FARM = {"types":[["Bush",87,[76,77,78,79]],["Cactus",88,[18416,36097,45321,45633]],["Flower",91,[72,73,74,75,60628]],["Fruit tree",93,[48,49,50,51,16082,25874,60621]],["Herb",94,[124,125,126,127,133,24961,33382,52218,60629]],["Hops",95,[60,62,63]],["Mushroom",96,[45332,53267]],["Tree",99,[44,45,46,47,136,24963,52984,60617]],["Allotment",100,[52,53,54,55,56,57,58,59,60625,60626]]],"vb":{"36097":[4334,18,18],"24961":[4794,9,16],"24963":[4794,19,26],"133":[25,0,7],"136":[25,11,18],"45321":[6461,22,29],"25874":[4795,8,15],"53267":[10904,12,19],"45332":[9058,23,30],"44":[10,0,7],"45":[10,8,15],"46":[10,16,23],"47":[10,24,31],"48":[11,0,7],"49":[11,8,15],"50":[11,16,23],"51":[11,24,31],"52":[12,0,7],"53":[12,8,15],"54":[12,16,23],"55":[12,24,31],"56":[13,0,7],"57":[13,8,15],"58":[13,16,23],"59":[13,24,31],"60":[14,0,7],"62":[14,16,23],"63":[14,24,31],"45633":[9059,8,15],"72":[16,0,7],"73":[16,8,15],"74":[16,16,23],"75":[16,24,31],"76":[17,0,7],"77":[17,8,15],"78":[17,16,23],"79":[17,24,31],"16082":[3118,18,25],"33382":[6461,0,7],"18416":[28,5,12],"52984":[10904,0,7],"52218":[9059,19,26],"124":[23,0,7],"125":[23,8,15],"126":[23,16,23],"127":[23,24,31]},"loc":{"124":"South of Falador","125":"West of Catherby","126":"North of Ardougne","127":"East of Canifis","133":"Trollheim","24961":"Prifddinas","33382":"Wilderness Bloodweed","52218":"Het's Oasis","52":"South of Falador","53":"South of Falador","54":"West of Catherby","55":"West of Catherby","56":"North of Ardougne","57":"North of Ardougne","58":"East of Canifis","59":"East of Canifis","72":"South of Falador","73":"West of Catherby","74":"North of Ardougne","75":"East of Canifis","76":"West of Champions' Guild","77":"Rimmington","78":"Etceteria","79":"South of Ardougne","24955":"East of Prifddinas","47":"East of Lumbridge","46":"Varrock Palace","45":"Falador Park","44":"Taverley","136":"Tree Gnome Stronghold","24963":"Prifddinas","52984":"Woodcutters' Grove","48":"Tree Gnome Stronghold","49":"Tree Gnome Village","50":"Brimhaven","51":"East of Catherby","16082":"Herblore Habitat","25874":"Prifddinas","60":"Yanille","62":"North of Lumbridge","63":"North of Seers' Village","45321":"Anachronia","18416":"Al Kharid","36097":"Menaphos","45633":"Het's Oasis","45332":"West of Canifis","53267":"City of Um","60629":"Havenhythe","60628":"Havenhythe","60617":"Havenhythe","60621":"Havenhythe","60625":"Havenhythe","60626":"Havenhythe"},"state":{"1":"Empty","2":"Weeds","3":"Growing","4":"Harvestable","5":"Stump","6":"Scarecrow","7":"Needs Watering","32":"Fully Grown","33":"Diseased","34":"Dead"},"enums":{"87":{"0":2,"1":2,"2":2,"3":1,"5":3,"6":3,"7":3,"8":3,"9":3,"10":32,"11":4,"12":4,"13":4,"14":4,"15":3,"16":3,"17":3,"18":3,"19":3,"20":3,"21":32,"22":4,"23":4,"24":4,"25":4,"26":3,"27":3,"28":3,"29":3,"30":3,"31":3,"32":3,"33":32,"34":4,"35":4,"36":4,"37":4,"38":3,"39":3,"40":3,"41":3,"42":3,"43":3,"44":3,"45":3,"46":32,"47":4,"48":4,"49":4,"50":4,"51":3,"52":3,"53":3,"54":3,"55":3,"56":3,"57":3,"58":3,"59":32,"60":4,"61":4,"62":4,"63":4,"64":3,"65":3,"66":3,"67":3,"68":3,"69":32,"70":33,"71":33,"72":33,"73":33,"74":33,"75":3,"76":3,"77":3,"78":3,"79":3,"80":33,"81":33,"82":33,"83":33,"84":33,"85":33,"86":3,"87":3,"88":3,"89":3,"90":3,"91":33,"92":33,"93":33,"94":33,"95":33,"96":33,"97":33,"98":32,"99":4,"100":4,"101":4,"102":4,"103":33,"104":33,"105":33,"106":33,"107":33,"108":33,"109":33,"110":33,"111":32,"112":4,"113":4,"114":4,"115":4,"116":33,"117":33,"118":33,"119":33,"120":33,"121":33,"122":33,"123":33,"124":33,"125":33,"126":33,"127":33,"128":3,"129":3,"130":3,"131":3,"132":3,"133":32,"134":34,"135":34,"136":34,"137":34,"139":4,"140":33,"141":33,"142":33,"143":33,"144":34,"145":34,"146":34,"147":34,"148":34,"150":33,"151":33,"152":33,"153":33,"154":33,"155":34,"156":34,"157":34,"158":34,"159":34,"160":34,"162":34,"163":34,"164":34,"165":34,"166":34,"167":34,"168":34,"169":34,"170":34,"171":34,"172":34,"173":34,"175":32,"176":4,"177":4,"178":4,"179":4,"180":34,"181":34,"182":34,"183":34,"184":34,"185":34,"186":34,"188":33,"189":33,"190":33,"191":33,"192":34,"193":34,"194":34,"195":34,"197":3,"198":3,"199":3,"200":3,"201":3,"202":3,"203":3,"204":3,"205":32,"206":4,"207":4,"208":4,"209":4,"210":33,"211":33,"212":33,"213":33,"214":33,"215":33,"216":33,"217":34,"218":34,"219":34,"220":34,"221":34,"222":34,"223":34,"225":33,"226":34,"227":34,"228":34,"229":34,"230":34,"231":34,"232":34,"233":34,"246":32,"247":32,"248":32,"249":32,"250":32,"251":32,"252":32,"253":32,"254":32,"255":32},"88":{"0":2,"1":2,"2":2,"3":1,"8":3,"9":3,"10":3,"11":3,"12":3,"13":3,"14":3,"15":32,"16":4,"17":4,"18":4,"19":33,"20":33,"21":33,"22":33,"23":33,"24":33,"25":34,"26":34,"27":34,"28":34,"29":34,"30":34,"31":32,"32":3,"33":3,"34":3,"35":3,"36":3,"37":3,"38":3,"39":32,"40":4,"41":33,"42":33,"43":33,"44":33,"45":33,"46":33,"47":34,"48":34,"49":34,"50":34,"51":34,"52":34,"53":32,"54":3,"55":3,"56":3,"57":3,"58":3,"59":3,"60":3,"61":32,"62":4,"63":4,"64":4,"65":33,"66":33,"67":33,"68":33,"69":33,"70":33,"71":34,"72":34,"73":34,"74":34,"75":34,"76":34,"77":32,"78":3,"79":3,"80":3,"81":3,"82":3,"83":3,"84":3,"85":32,"86":4,"87":4,"88":4,"89":33,"90":33,"91":33,"92":33,"93":33,"94":33,"95":34,"96":34,"97":34,"98":34,"99":34,"100":34,"101":32,"102":3,"103":3,"104":3,"105":3,"106":3,"107":3,"108":3,"109":32,"110":4,"111":4,"112":4,"113":33,"114":33,"115":33,"116":33,"117":33,"118":33,"119":34,"120":34,"121":34,"122":34,"123":34,"124":34,"125":32},"91":{"0":2,"1":2,"2":2,"3":1,"8":7,"9":7,"10":7,"11":7,"12":32,"13":7,"14":7,"15":7,"16":7,"17":32,"18":7,"19":7,"20":7,"21":7,"22":32,"23":7,"24":7,"25":7,"26":7,"27":32,"28":7,"29":7,"30":7,"31":7,"32":32,"33":6,"34":6,"35":6,"36":6,"37":3,"38":3,"39":3,"40":3,"41":32,"42":7,"43":7,"44":7,"45":7,"46":32,"47":7,"48":7,"49":7,"50":7,"51":32,"72":3,"73":3,"74":3,"75":3,"76":10,"77":3,"78":3,"79":3,"80":3,"82":3,"83":3,"84":3,"85":3,"87":3,"88":3,"89":3,"90":3,"92":3,"93":3,"94":3,"95":3,"106":3,"107":3,"108":3,"109":3,"111":3,"112":3,"113":3,"114":3,"137":33,"138":33,"139":33,"142":33,"143":33,"144":33,"147":33,"148":33,"149":33,"152":33,"153":33,"154":33,"157":33,"158":33,"159":33,"171":33,"172":33,"173":33,"176":33,"177":33,"178":33,"201":34,"202":34,"203":34,"204":34,"206":34,"207":34,"208":34,"209":34,"211":34,"212":34,"213":34,"214":34,"216":34,"217":34,"218":34,"219":34,"221":34,"222":34,"223":34,"224":34,"235":34,"236":34,"237":34,"238":34,"240":34,"241":34,"242":34,"243":34},"93":{"0":2,"1":2,"2":2,"3":1,"4":3,"5":3,"6":3,"7":3,"8":3,"9":3,"10":3,"11":3,"12":3,"13":3,"14":32,"15":4,"16":4,"17":4,"18":4,"19":4,"20":4,"21":33,"22":33,"23":33,"24":33,"25":33,"26":33,"27":34,"28":34,"29":34,"30":34,"31":34,"32":34,"33":5,"34":32,"35":3,"36":3,"37":3,"38":3,"39":3,"40":3,"41":32,"42":4,"43":4,"44":4,"45":4,"46":4,"47":4,"48":33,"49":33,"50":33,"51":33,"52":33,"53":33,"54":34,"55":34,"56":34,"57":34,"58":34,"59":34,"60":5,"61":32,"62":32,"63":4,"64":4,"65":4,"66":33,"67":33,"68":33,"69":33,"70":3,"71":5,"72":3,"73":3,"74":3,"75":3,"76":3,"77":3,"78":32,"79":4,"80":4,"81":4,"82":4,"83":4,"84":4,"85":33,"86":33,"87":33,"88":33,"89":33,"90":33,"91":34,"92":34,"93":34,"94":34,"95":34,"96":34,"97":5,"98":32,"99":3,"100":3,"101":3,"102":3,"103":3,"104":3,"105":32,"106":4,"107":4,"108":4,"109":4,"110":4,"111":4,"112":33,"113":33,"114":33,"115":33,"116":33,"117":33,"118":34,"119":34,"120":34,"121":34,"122":34,"123":34,"124":5,"125":32,"126":3,"127":3,"128":3,"129":3,"130":32,"131":4,"132":4,"133":4,"134":3,"135":5,"136":3,"137":3,"138":3,"139":3,"140":3,"141":3,"142":32,"143":4,"144":4,"145":4,"146":4,"147":4,"148":4,"149":33,"150":33,"151":33,"152":33,"153":33,"154":33,"155":34,"156":34,"157":34,"158":34,"159":34,"160":34,"161":5,"162":32,"163":3,"164":3,"165":3,"166":3,"167":3,"168":3,"169":32,"170":4,"171":4,"172":4,"173":4,"174":4,"175":4,"176":33,"177":33,"178":33,"179":33,"180":33,"181":33,"182":34,"183":34,"184":34,"185":34,"186":34,"187":34,"188":5,"189":32,"190":33,"191":33,"192":33,"193":33,"194":3,"195":3,"196":3,"197":3,"198":3,"199":5,"200":3,"201":3,"202":3,"203":3,"204":3,"205":3,"206":32,"207":4,"208":4,"209":4,"210":4,"211":4,"212":4,"213":33,"214":33,"215":33,"216":33,"217":33,"218":33,"219":34,"220":34,"221":34,"222":34,"223":34,"224":34,"225":5,"226":32,"227":32,"228":4,"229":4,"230":4,"231":33,"232":33,"233":33,"234":33,"235":34,"236":34,"237":34,"238":34,"239":34,"240":34,"241":34,"242":34,"243":34,"244":34,"245":34,"246":34,"247":32,"248":32,"249":32},"94":{"0":2,"1":2,"2":2,"3":1,"4":3,"5":3,"6":3,"7":3,"8":32,"9":32,"10":32,"11":3,"12":3,"13":3,"14":3,"15":32,"16":32,"17":32,"18":3,"19":3,"20":3,"21":3,"22":32,"23":32,"24":32,"25":3,"26":3,"27":3,"28":3,"29":32,"30":32,"31":32,"32":3,"33":3,"34":3,"35":3,"36":32,"37":32,"38":32,"39":3,"40":3,"41":3,"42":3,"43":32,"44":32,"45":32,"46":3,"47":3,"48":3,"49":3,"50":32,"51":32,"52":32,"53":3,"54":3,"55":3,"56":3,"57":32,"58":32,"59":32,"60":3,"61":3,"62":3,"63":3,"64":32,"65":32,"66":32,"67":3,"68":3,"69":3,"70":3,"71":3,"72":32,"73":32,"74":32,"75":3,"76":3,"77":3,"78":3,"79":32,"80":32,"81":32,"82":3,"83":3,"84":3,"85":3,"86":32,"87":32,"88":32,"89":3,"90":3,"91":3,"92":3,"93":32,"94":32,"95":32,"96":3,"97":3,"98":3,"99":3,"100":32,"101":32,"102":32,"103":3,"104":3,"105":3,"106":3,"107":32,"108":32,"109":32,"110":3,"111":3,"112":3,"113":32,"114":32,"115":32,"128":33,"129":33,"130":33,"131":33,"132":33,"133":33,"134":33,"135":33,"136":33,"137":33,"138":33,"139":33,"140":33,"141":33,"142":33,"143":33,"144":33,"145":33,"146":33,"147":33,"148":33,"149":33,"150":33,"151":33,"152":33,"153":33,"154":33,"155":33,"156":33,"157":33,"158":33,"159":33,"160":33,"161":33,"162":33,"163":33,"164":33,"165":33,"166":33,"167":33,"168":33,"169":33,"170":34,"171":34,"172":34,"173":33,"174":33,"175":33,"176":33,"177":33,"178":33,"179":3,"180":3,"181":3,"182":3,"183":32,"184":32,"185":32,"186":33,"187":33,"188":33,"192":3,"193":3,"194":3,"195":3,"196":32,"197":32,"198":33,"199":33,"200":33,"201":34,"202":34,"203":34,"204":3,"205":3,"206":3,"207":3,"208":32,"209":32,"210":32,"211":33,"212":33,"213":33,"214":3,"215":3,"216":3,"217":3,"218":32,"219":32,"220":32,"221":33,"222":33,"223":33},"95":{"0":2,"1":2,"2":2,"3":1,"4":7,"5":7,"6":7,"7":7,"8":32,"9":32,"10":32,"11":7,"12":7,"13":7,"14":7,"15":7,"16":32,"17":32,"18":32,"19":7,"20":7,"21":7,"22":7,"23":7,"24":7,"25":32,"26":32,"27":32,"28":7,"29":7,"30":7,"31":7,"32":7,"33":7,"34":7,"35":32,"36":32,"37":32,"38":7,"39":7,"40":7,"41":7,"42":7,"43":7,"44":7,"45":7,"46":32,"47":32,"48":32,"49":7,"50":7,"51":7,"52":7,"53":32,"54":32,"55":32,"56":7,"57":7,"58":7,"59":7,"60":7,"61":32,"62":32,"63":32,"64":9,"65":7,"66":7,"67":7,"68":3,"69":3,"70":3,"71":3,"72":7,"73":7,"74":7,"75":3,"76":3,"77":3,"78":3,"79":3,"80":7,"81":7,"82":7,"83":3,"84":3,"85":3,"86":3,"87":3,"88":3,"89":33,"90":33,"91":33,"92":3,"93":3,"94":3,"95":3,"96":3,"97":3,"98":3,"99":33,"100":33,"101":33,"102":3,"103":3,"104":3,"105":3,"106":3,"107":3,"108":3,"109":3,"110":7,"111":7,"112":7,"113":3,"114":3,"115":3,"116":3,"117":33,"118":33,"119":33,"120":3,"121":3,"122":3,"123":3,"124":3,"129":3,"130":3,"131":3,"133":33,"134":33,"135":33,"136":3,"137":3,"138":3,"140":33,"141":33,"142":33,"143":33,"144":3,"145":3,"146":3,"148":33,"149":33,"150":33,"151":33,"152":33,"153":34,"154":34,"155":34,"157":33,"158":33,"159":33,"160":33,"161":33,"162":33,"163":34,"164":34,"165":34,"167":33,"168":33,"169":33,"170":33,"171":33,"172":33,"173":33,"174":3,"175":3,"176":3,"178":33,"179":33,"180":33,"185":33,"186":33,"187":33,"188":33,"189":32,"190":32,"191":32,"192":32,"193":32,"194":32,"195":32,"196":32,"197":34,"198":34,"199":34,"204":34,"205":34,"206":34,"207":34,"208":32,"209":32,"210":32,"212":34,"213":34,"214":34,"215":34,"216":34,"217":32,"218":32,"219":32,"220":32,"221":34,"222":34,"223":34,"224":34,"225":34,"226":34,"231":34,"232":34,"233":34,"234":34,"235":34,"236":34,"237":34,"242":34,"243":34,"244":34,"245":34,"249":34,"250":34,"251":34,"252":34},"96":{"0":2,"1":2,"2":2,"3":1,"4":3,"5":3,"6":3,"7":3,"8":3,"9":3,"10":32,"11":32,"12":32,"13":32,"14":32,"15":32,"16":33,"17":33,"18":33,"19":33,"20":33,"21":34,"22":34,"23":34,"24":34,"25":34,"26":3,"27":3,"28":3,"29":3,"30":3,"31":3,"32":32,"33":32,"34":32,"35":32,"36":32,"37":32,"38":32,"39":32,"40":32,"41":33,"42":33,"43":33,"44":33,"45":33,"46":34,"47":34,"48":34,"49":34,"50":34,"51":3,"52":3,"53":3,"54":3,"55":3,"56":3,"57":32,"58":33,"59":33,"60":33,"61":33,"62":33,"63":34,"64":34,"65":34,"66":34,"67":34,"68":3,"69":3,"70":3,"71":3,"72":3,"73":3,"74":32,"75":33,"76":33,"77":33,"78":33,"79":33,"80":34,"81":34,"82":34,"83":34,"84":34},"99":{"0":2,"1":2,"2":2,"3":1,"8":3,"9":3,"10":3,"11":3,"12":32,"13":32,"14":5,"15":3,"16":3,"17":3,"18":3,"19":3,"20":3,"21":32,"22":32,"23":5,"24":3,"25":3,"26":3,"27":3,"28":3,"29":3,"30":3,"31":3,"32":32,"33":32,"34":5,"35":3,"36":3,"37":3,"38":3,"39":3,"40":3,"41":3,"42":3,"43":3,"44":3,"45":32,"46":32,"47":5,"48":3,"49":3,"50":3,"51":3,"52":3,"53":3,"54":3,"55":3,"56":3,"57":3,"58":3,"59":3,"60":32,"61":32,"62":5,"73":33,"74":33,"75":33,"80":33,"81":33,"82":33,"83":33,"84":33,"89":33,"90":33,"91":33,"92":33,"93":33,"94":33,"95":33,"100":33,"101":33,"102":33,"103":33,"104":33,"105":33,"106":33,"107":33,"108":33,"113":33,"114":33,"115":33,"116":33,"117":33,"118":33,"119":33,"120":33,"121":33,"122":33,"123":33,"137":34,"138":34,"139":34,"144":34,"145":34,"146":34,"147":34,"148":34,"153":34,"154":34,"155":34,"156":34,"157":34,"158":34,"159":34,"164":34,"165":34,"166":34,"167":34,"168":34,"169":34,"170":34,"171":34,"172":34,"177":34,"178":34,"179":34,"180":34,"181":34,"182":34,"183":34,"184":34,"185":34,"186":34,"187":34,"192":4,"193":4,"194":4,"195":4,"196":4,"197":4},"100":{"0":2,"1":2,"2":2,"3":1,"6":7,"7":7,"8":7,"9":7,"10":32,"11":32,"12":32,"13":7,"14":7,"15":7,"16":7,"17":32,"18":32,"19":32,"20":7,"21":7,"22":7,"23":7,"24":32,"25":32,"26":32,"27":7,"28":7,"29":7,"30":7,"31":32,"32":32,"33":32,"34":7,"35":7,"36":7,"37":7,"38":7,"39":7,"40":32,"41":32,"42":32,"43":7,"44":7,"45":7,"46":7,"47":7,"48":7,"49":32,"50":32,"51":32,"52":7,"53":7,"54":7,"55":7,"56":7,"57":7,"58":7,"59":7,"60":32,"61":32,"62":32,"64":7,"65":7,"66":7,"70":3,"71":3,"72":3,"73":3,"74":32,"77":3,"78":3,"79":3,"80":3,"81":32,"82":32,"83":32,"84":3,"85":3,"86":3,"87":3,"88":32,"91":3,"92":3,"93":3,"94":3,"98":3,"99":3,"100":3,"101":3,"102":3,"103":3,"107":3,"108":3,"109":3,"110":3,"111":3,"112":3,"116":3,"117":3,"118":3,"119":3,"120":3,"121":3,"122":3,"123":3,"128":3,"129":3,"130":3,"135":33,"136":33,"137":33,"138":7,"139":7,"140":7,"141":7,"142":33,"143":33,"144":33,"145":7,"146":7,"147":7,"148":7,"149":33,"150":33,"151":33,"152":7,"153":7,"154":7,"155":7,"156":33,"157":33,"158":33,"159":33,"160":33,"161":33,"162":33,"163":33,"164":33,"165":33,"166":33,"167":33,"168":33,"169":33,"170":33,"171":33,"172":33,"173":33,"174":33,"175":33,"176":33,"177":33,"178":33,"179":33,"180":33,"181":33,"182":33,"183":33,"184":33,"185":33,"186":33,"187":33,"199":34,"200":34,"201":34,"202":3,"203":3,"204":3,"205":3,"206":34,"207":34,"208":34,"209":3,"210":3,"211":3,"212":3,"213":34,"214":34,"215":34,"216":3,"217":3,"218":3,"219":3,"220":34,"221":34,"222":34,"223":34,"224":34,"225":34,"226":34,"227":34,"228":34,"229":34,"230":34,"231":34,"232":34,"233":34,"234":34,"235":34,"236":34,"237":34,"238":34,"239":34,"240":34,"241":34,"242":34,"243":34,"244":34,"245":34,"246":34,"247":34,"248":34,"249":34,"250":34,"251":34}}};
  const FARM_BADGE = { 4: 'fm-ready', 32: 'fm-ready', 33: 'fm-disease', 34: 'fm-dead', 7: 'fm-water', 3: 'fm-grow', 6: 'fm-grow', 10: 'fm-grow', 2: 'fm-idle', 1: 'fm-idle', 5: 'fm-idle' };
  const FARM_ACTIONABLE = { 4: 1, 32: 1, 33: 1, 34: 1, 7: 1 };   // harvestable / fully grown / diseased / dead / needs water
  const FARM_VARP_IDS = (() => { const s = new Set(); for (const kk in FARM.vb) s.add(FARM.vb[kk][0]); return Array.from(s); })();
  // varbit id -> enum id, and the flat list of every patch varbit (for alerts).
  const FARM_VB_ENUM = (() => { const m = {}; for (const t of FARM.types) for (const vb of t[2]) m[vb] = t[1]; return m; })();
  const FARM_ALL_VB = Object.keys(FARM_VB_ENUM).map(Number);

  // Tool leprechaun storage: each stored tool/consumable is a persistent player varbit. Tools are
  // 0/1 flags; consumables are counts; Bucket/Neem oil split high*32 + low across two varbits;
  // the watering can stores its charge (0-8).
  const LEPRECHAUN = {
    tools: [['Rake', 5341, 176], ['Seed dibber', 5343, 177], ['Spade', 952, 178],
            ['Gardening trowel', 5325, 181], ['Magic secateurs', 7409, 179]],
    can: ['Watering can', 6797, 180],   // value = charge / waterings (0-8)
    items: [['Bucket', 1925, [5061, 182]], ['Compost', 6032, 183], ['Supercompost', 6034, 36900],
            ['Ultracompost', 43966, 41580], ['Plank', 960, 36901], ['Scarecrow', 6059, 5054],
            ['Plant cure', 6036, 5062],
            ['Juju hunter potion (3)', 20024, 16128], ['Juju farming potion (3)', 20012, 16129],
            ['Scentless potion (3)', 20028, 16130], ["Saradomin's blessing (3)", 20032, 16132],
            ["Zamorak's favour (3)", 20040, 16133], ["Guthix's gift (3)", 20036, 16131],
            ['Corrupt vine', 19979, 16134], ['Marble vine', 19980, 16135], ['Shadow vine', 19977, 16136],
            ['Saradomin vine', 19981, 16137], ['Zamorak vine', 19983, 16138],
            ['Fungal flake', 22449, 510], ['Grifolic flake', 22450, 511], ['Ganodermic flake', 22451, 512],
            ['Polypore spore', 22448, 509], ['Neem oil', 22444, [514, 513]],
            ['Crystal weapon seed', 32206, 25943], ['Crystal armour seed', 32623, 25944],
            ['Crystal tool seed', 32208, 25945], ['Attuned crystal weapon seed', 32625, 25948],
            ['Attuned crystal armour seed', 32626, 25949], ['Crystal teleport seed', 6103, 25950],
            ['Liquid patch bomb', 41087, 43922]]
  };
  // every varbit the leprechaun reads (flat), for resolving the varps to fetch
  const LEPRECHAUN_VBS = (() => {
    const s = new Set();
    LEPRECHAUN.tools.forEach(t => s.add(t[2]));
    s.add(LEPRECHAUN.can[2]);
    LEPRECHAUN.items.forEach(it => { const v = it[2]; Array.isArray(v) ? v.forEach(x => s.add(x)) : s.add(v); });
    return Array.from(s);
  })();
  // count for one leprechaun entry: single varbit, or high*32 + low for [hi, lo] (Bucket/Neem oil)
  function lepCount(vbref, vp) {
    if (Array.isArray(vbref)) return (readVb(vbref[0], vp) || 0) * 32 + (readVb(vbref[1], vp) || 0);
    return readVb(vbref, vp) || 0;
  }

  farmData = null; let farmFetching = false; let farmSig = ''; let _farmAt = 0;
  function farmPatchLabel(vbid) {
    const enumId = FARM_VB_ENUM[vbid];
    const t = FARM.types.find(x => x[1] === enumId);
    return (t ? t[0] + ' · ' : '') + (FARM.loc[vbid] || ('Patch ' + vbid));
  }
  // True when the patch state matches the alert condition; `cond` defaults to ready.
  function farmStateMatch(vbid, cond) {
    const enumId = FARM_VB_ENUM[vbid]; if (enumId == null) return false;
    const p = farmPatch(vbid, enumId); if (!p) return false;
    const codes = FARM_COND_CODES[cond] || FARM_COND_CODES.ready;
    return codes.indexOf(p.code) >= 0;
  }
  function farmCondLabel(cond) { const t = FARM_CONDS.find(c => c[0] === cond); return t ? t[1] : 'Ready'; }

  async function fetchFarming() {
    if (!bridge() || !bridge().varps || farmFetching) return;
    const now = Date.now(); if (now - _farmAt < 1000) return; _farmAt = now;
    farmFetching = true;
    try {
      // Patches are varps; the tool-leprechaun store is varbits -> resolve those to their varps and
      // read everything in one call so farmData carries both.
      await ensureVbMap();
      const vps = new Set(FARM_VARP_IDS);
      for (const vb of LEPRECHAUN_VBS) { const r = storageVbMap && storageVbMap[vb]; if (r) vps.add(r.varp); }
      // patches without a baked FARM.vb triple (Havenhythe) resolve their varp live
      for (const vb of FARM_ALL_VB) if (!FARM.vb[vb]) { const r = storageVbMap && storageVbMap[vb]; if (r) vps.add(r.varp); }
      const d = JSON.parse(await rtxData.raw('state.varps', Array.from(vps).join(',')));
      if (d && typeof d === 'object') farmData = d;
    }
    catch (e) { /* keep previous */ }
    farmFetching = false;
    paneRun('farming', renderFarming);
  }
  // Decode one patch varbit -> {key, code, name, badge, actionable, locked}. A value with no entry
  // in the patch enum means the patch is not built/unlocked -> "Locked".
  function farmPatch(vbid, enumId) {
    // Havenhythe patches (vb 60617+) carry no baked [varp,lo,hi] triple; resolve
    // them through the live varbit map, the same source the leprechaun store uses.
    let def = FARM.vb[vbid];
    if (!def) {
      const r = storageVbMap && storageVbMap[vbid];
      if (!r) return null;
      def = [r.varp, r.lsb, r.msb];
    }
    const raw = (farmData && farmData[def[0]]) || 0;
    const lo = def[1], hi = def[2], mask = (1 << (hi - lo + 1)) - 1;
    const key = (raw >>> lo) & mask;
    const code = (FARM.enums[enumId] || {})[key];
    const locked = (code == null);
    const name = locked ? 'Locked' : (FARM.state[code] || ('State ' + code));
    return { key: key, code: code, name: name, locked: locked,
             badge: locked ? 'fm-lock' : (FARM_BADGE[code] || 'fm-idle'),
             actionable: !locked && !!FARM_ACTIONABLE[code],
             vp: def[0], lo: lo, hi: hi, enumId: enumId };
  }
  // Tool leprechaun storage section; repaints only when contents change (lepSig).
  let lepSig = '';
  function paintLeprechaun() {
    const box = document.getElementById('fmLep'); if (!box) return;
    if (!farmData) { box.innerHTML = ''; return; }
    const vp = farmData;
    const stored = LEPRECHAUN.items
      .map(it => ({ name: it[0], id: it[1], n: lepCount(it[2], vp), vb: it[2] }))
      .filter(x => x.n > 0);
    const canCharge = readVb(LEPRECHAUN.can[2], vp) || 0;
    const tools = LEPRECHAUN.tools.filter(t => (readVb(t[2], vp) || 0) > 0);
    const sig = stored.map(x => x.name + ':' + x.n).join(',') + '|can' + canCharge +
                '|' + tools.map(t => t[0]).join(',');
    if (sig === lepSig) return; lepSig = sig;
    box.innerHTML = '';
    const hdr = document.createElement('div'); hdr.className = 'fm-grp'; hdr.textContent = 'Tool leprechaun';
    box.appendChild(hdr);
    if (!stored.length && !tools.length && canCharge <= 0) {
      const e = document.createElement('div'); e.className = 'fm-lep-empty'; e.textContent = 'Empty - nothing stored.';
      box.appendChild(e); return;
    }
    const vbSrc = (vb) => {
      if (Array.isArray(vb)) return 'varbits ' + vb[0] + ' (×32) + ' + vb[1];
      const r = storageVbMap && storageVbMap[vb];
      return r ? ('varbit ' + vb + ' = varp ' + r.varp + ' bits ' + r.lsb + '-' + r.msb) : ('varbit ' + vb);
    };
    if (stored.length) {
      const grid = document.createElement('div'); grid.className = 'stor-grid';
      for (const x of stored) {
        const url = x.id ? resolveIcon(x.id) : '';
        const cell = document.createElement('div'); cell.className = 'bank-cell stor-cell';
        cell.dataset.tip = x.name + '\n' + x.n.toLocaleString() + ' stored\n' + vbSrc(x.vb);
        if (url) { const ico = document.createElement('div'); ico.className = 'bank-icon'; ico.dataset.itemId = String(x.id); setIconBg(ico, url); cell.appendChild(ico); }
        else { const tn = document.createElement('div'); tn.className = 'stor-tn'; tn.textContent = x.name; cell.appendChild(tn); }
        const fa = fmtAmt(x.n);
        const amt = document.createElement('span'); amt.className = 'bank-amt' + (fa.c ? ' ' + fa.c : ''); amt.textContent = fa.t || x.n; cell.appendChild(amt);
        grid.appendChild(cell);
      }
      box.appendChild(grid);
    }
    // tools + watering can are 0/1 (or charge) flags -> compact chip row
    const flags = tools.map(t => t[0]);
    if (canCharge > 0) flags.push(LEPRECHAUN.can[0] + ' · ' + canCharge + 'w');
    if (flags.length) {
      const row = document.createElement('div'); row.className = 'fm-lep-tools';
      row.innerHTML = flags.map(f => '<span class="fm-lep-chip">' + f + '</span>').join('');
      box.appendChild(row);
    }
  }

  function renderFarming() {
    const c = $('content');
    let w = document.getElementById('fmWrap');
    if (!w) {
      c.innerHTML = '';
      w = document.createElement('div'); w.id = 'fmWrap'; w.className = 'fm-wrap';
      const hdr = document.createElement('div'); hdr.className = 'scene-hdr';
      const t = document.createElement('div'); t.textContent = 'Farming patches';
      const cnt = document.createElement('span'); cnt.className = 'cnt'; cnt.id = 'fmCnt'; cnt.textContent = '...';
      hdr.appendChild(t); hdr.appendChild(cnt);
      const sum = document.createElement('div'); sum.id = 'fmSum'; sum.className = 'fm-sum';
      const lep = document.createElement('div'); lep.id = 'fmLep'; lep.className = 'fm-lep';
      const list = document.createElement('div'); list.id = 'fmList'; list.className = 'fm-list';
      w.appendChild(hdr); w.appendChild(sum); w.appendChild(lep); w.appendChild(list); c.appendChild(w); farmSig = ''; lepSig = '';
    }
    paintLeprechaun();
    paintFarming();
  }
  function paintFarming() {
    const list = document.getElementById('fmList'); if (!list) return;
    if (!farmData) { list.innerHTML = '<div class="empty">Reading varps... (open in-world)</div>'; return; }
    const groups = []; const counts = { 4: 0, 32: 0, 33: 0, 34: 0, 7: 0 }; let sig = '';
    for (const tdef of FARM.types) {
      const name = tdef[0], enumId = tdef[1], vbids = tdef[2]; const rows = [];
      for (const vbid of vbids) {
        const p = farmPatch(vbid, enumId); if (!p) continue;
        rows.push({ loc: FARM.loc[vbid] || ('vb' + vbid), p: p, vbid: vbid }); sig += vbid + ':' + p.key + ';';
        if (counts[p.code] != null) counts[p.code]++;
      }
      groups.push([name, rows]);
    }
    const total = groups.reduce(function (a, g) { return a + g[1].length; }, 0);
    const cnt = document.getElementById('fmCnt'); if (cnt) cnt.textContent = total;
    if (sig === farmSig) return; farmSig = sig;
    const sum = document.getElementById('fmSum');
    const chips = [['Harvestable', counts[4] + counts[32], 'var(--ok)'], ['Diseased', counts[33], '#f0b03c'], ['Dead', counts[34], '#e0564e'], ['Needs water', counts[7], '#57c6e0']];
    if (sum) sum.innerHTML = chips.filter(function (x) { return x[1] > 0; }).map(function (x) { return '<span class="fm-chip"><b style="color:' + x[2] + '">' + x[1] + '</b> ' + x[0] + '</span>'; }).join('') || '<span class="fm-chip">Nothing needs attention</span>';
    list.innerHTML = '';
    for (const g of groups) {
      const name = g[0], rows = g[1]; if (!rows.length) continue;
      rows.sort(function (a, b) { return (b.p.actionable - a.p.actionable) || (a.p.locked - b.p.locked); });
      const gh = document.createElement('div'); gh.className = 'fm-grp'; gh.textContent = name; list.appendChild(gh);
      for (const r of rows) {
        const row = document.createElement('div'); row.className = 'fm-row' + (r.p.actionable ? ' is-actionable' : '') + (r.p.locked ? ' is-locked' : '');
        row.dataset.tip = name + ' patch · ' + r.loc +
          '\nStatus: ' + r.p.name +
          '\nSource: varbit ' + r.vbid + ' = varp ' + r.p.vp +
          (r.p.lo === r.p.hi ? ' bit ' + r.p.lo : ' bits ' + r.p.lo + '-' + r.p.hi) +
          '\nRead value: ' + r.p.key + ' → ' +
          (r.p.locked
            ? 'no entry in patch enum ' + r.p.enumId + ' (patch not built/unlocked)'
            : 'state ' + r.p.code + ' via enum ' + r.p.enumId);
        const l = document.createElement('div'); l.className = 'fm-loc'; l.textContent = r.loc;
        const b = document.createElement('div'); b.className = 'fm-badge ' + r.p.badge; b.textContent = r.p.name;
        row.appendChild(l); row.appendChild(b); list.appendChild(row);
      }
    }
  }

  // Legacy dock-collapse compat: panels are floating windows now, so there is no dock to
  // collapse. Other panels still read sideCollapsed, so it stays permanently false.
  var sideCollapsed = false;
  function toggleSidebar() {}

  // Privacy: mask the player name in the panel header; persisted (rtxHideName).
  nameHidden = false;
  try { nameHidden = localStorage.getItem('rtxHideName') === '1'; } catch (e) {}
  function setNameHidden(v) {
    nameHidden = !!v;
    try { if (typeof prefSet === 'function') prefSet('rtxHideName', nameHidden ? '1' : '0'); else localStorage.setItem('rtxHideName', nameHidden ? '1' : '0'); } catch (e) {}
    renderHeader();
    const p = $('sysHideNamePill'); if (p) p.classList.toggle('on', nameHidden);   // keep the System-tab toggle in sync if it's showing
    const p2 = $('uis_hidename'); if (p2) p2.classList.toggle('on', nameHidden);
  }
  (function () {
    // Runs at panel-load, BEFORE the main script defines `$` (panels are spliced ahead of it), so
    // document.getElementById is used directly; `$` here would throw. The hdr-name element is in the
    // HTML above the splice point, so it already exists.
    const hn = document.getElementById('hdr-name');
    if (hn) { hn.style.cursor = 'pointer'; hn.title = 'Click to hide or show your name'; hn.addEventListener('click', () => setNameHidden(!nameHidden)); }
  })();

  // Sidebar categories persisted PER CHARACTER (sidebarLoad/sidebarSave); no saved state -> all
  // collapsed.
  let collapsedCats = null;
  function allCatSet() { const s = new Set(); for (const t of TABS) s.add(t.cat || 'General'); return s; }
  function loadCats() {
    let arr = null;
    try {
      const raw = (bridge() && bridge().sidebarLoad) ? rtxData.sync('host.sidebarLoad') : '';
      if (raw) { const o = JSON.parse(raw); if (o && Array.isArray(o.collapsed)) arr = o.collapsed; }
    } catch (e) {}
    collapsedCats = (arr === null) ? allCatSet() : new Set(arr);  // no saved state -> minimized
  }
  function saveCats() {
    try { rtxData.sync('act.sidebarSave', JSON.stringify({ collapsed: [...collapsedCats] })); } catch (e) {}
  }
  function toggleCat(cat) {
    if (!collapsedCats) loadCats();
    if (collapsedCats.has(cat)) collapsedCats.delete(cat); else collapsedCats.add(cat);
    saveCats();
    renderSidebar();
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { FARM, FARM_ALL_VB, farmCondLabel, farmPatchLabel, farmStateMatch, fetchFarming, setNameHidden });
registerTab({ id: 'farming', render: renderFarming, open: function () { farmSig = ''; _farmAt = 0; fetchFarming(); } });
})();
