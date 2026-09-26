// Generated from the client scripts that build the game's own descriptive text (buff: script 11088,
// item: script 5828) and the helpers they reach.
// Do not edit by hand; regenerate after a game update. Each translated script becomes S[id](args);
// the runtime (core/rtx-gametext-rt.js) supplies vars, inventories, cache lookups and formatting, and
// throws its Pending when it needs data it does not hold yet.
// Coverage, buff: 94 of 94 arms have text
// Coverage, item: translated; 2 reachable gap(s):
(function () {
  let R = null;   // the runtime, handed in through GAME_TEXT.bind
  const S = {};
  const V = {};   // script id -> { vb: [...], vp: [...], vc: [...], calls: [...] }
  V[42] = { vb: [], vp: [], vc: [], calls: [] };
  S[42] = function (int0) {
    if (R.eq(int0, 1)) {
      return 1;
    }
    return 0;
  };
  V[134] = { vb: [], vp: [], vc: [], calls: [13403] };
  S[134] = function () {
    return Math.imul(((1) + (R.call(13403, [])) | 0), 2);
  };
  V[178] = { vb: [21603, 26492, 26638, 26640, 26642, 26644, 26646, 26648, 26650, 26652, 26654, 26656, 26658, 26660, 26662, 26664, 26666, 26668, 26670, 26672, 26674, 26676, 26678, 26680, 26682, 26684, 26686, 26688, 30991, 30992, 30993, 30994, 30995, 30996, 32673, 33701, 34122, 34123, 34124, 34125, 34126, 34127, 34900, 35117, 36799, 40660, 41066, 43433, 43434, 48716, 49306, 49307, 49308, 49798, 50209, 50370, 51548, 53457, 53458, 54706, 55490, 55816, 55999, 58243, 60360, 60808, 60809], vp: [], vc: [], calls: [10881] };
  S[178] = function () {
    var int0 = ((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((R.vb(26638)) + (R.vb(26640)) | 0)) + (R.vb(26642)) | 0)) + (R.vb(26644)) | 0)) + (R.vb(26646)) | 0)) + (R.vb(26648)) | 0)) + (R.vb(26650)) | 0)) + (R.vb(26652)) | 0)) + (R.vb(26654)) | 0)) + (R.vb(26656)) | 0)) + (R.vb(26658)) | 0)) + (R.vb(26660)) | 0)) + (R.vb(26662)) | 0)) + (R.vb(26664)) | 0)) + (R.vb(26666)) | 0)) + (R.vb(26668)) | 0)) + (R.vb(26670)) | 0)) + (R.vb(26672)) | 0)) + (R.vb(26674)) | 0)) + (R.vb(26676)) | 0)) + (R.vb(26678)) | 0)) + (R.vb(26680)) | 0)) + (R.vb(26682)) | 0)) + (R.vb(26684)) | 0)) + (R.vb(26686)) | 0)) + (R.vb(26688)) | 0)) + (R.vb(33701)) | 0)) + (R.vb(26492)) | 0)) + (R.vb(34122)) | 0)) + (R.vb(34123)) | 0)) + (R.vb(34124)) | 0)) + (R.vb(34125)) | 0)) + (R.vb(34126)) | 0)) + (R.vb(34127)) | 0)) + (R.vb(35117)) | 0)) + (R.vb(30991)) | 0)) + (R.vb(30992)) | 0)) + (R.vb(30993)) | 0)) + (R.vb(30994)) | 0)) + (R.vb(30995)) | 0)) + (R.vb(30996)) | 0)) + (R.vb(32673)) | 0)) + (R.vb(34900)) | 0)) + (R.call(10881, [35426])) | 0)) + (R.call(10881, [35428])) | 0)) + (R.vb(36799)) | 0)) + (R.vb(40660)) | 0)) + (R.vb(41066)) | 0)) + (R.vb(43433)) | 0)) + (R.vb(43434)) | 0)) + (R.vb(48716)) | 0)) + (R.vb(49308)) | 0)) + (R.vb(49306)) | 0)) + (R.vb(49307)) | 0)) + (R.vb(55490)) | 0)) + (R.vb(49798)) | 0)) + (R.vb(50209)) | 0)) + (R.vb(21603)) | 0)) + (R.vb(50370)) | 0)) + (R.vb(51548)) | 0)) + (R.vb(53457)) | 0)) + (R.vb(53458)) | 0)) + (R.vb(54706)) | 0)) + (R.vb(55816)) | 0)) + (R.vb(55999)) | 0)) + (R.vb(58243)) | 0)) + (R.vb(60360)) | 0)) + (R.vb(60808)) | 0)) + (R.vb(60809)) | 0);
    return int0;
  };
  V[247] = { vb: [4407, 4422, 18179, 30607], vp: [], vc: [], calls: [13040] };
  S[247] = function (int0, int1, int2, int3, string0, string1) {
    var int4 = 0;
    switch (R.key(int1)) {
      case 32568:
      {
        if (((R.eq(int0, 2) && R.eq(R.vb(18179), 0)) && (!R.eq(R.invObj(94, 1), 27588)))) {
          int4 = 1;
        } else {
          if ((((R.eq(R.itemParam(R.invObj(94, 1), 6295), 1) || R.eq(R.itemParam(R.invObj(94, 1), 4552), 1)) || R.eq(R.invObj(94, 1), 32053)) && R.eq(R.call(13040, [7, 0]), 1))) {
            var int3 = 1;
            return [string0, int2, int3, int4];
          }
        }
        break;
      }
      case 32563:
      {
        if (((R.eq(int0, 3) && R.eq(R.vb(4407), 0)) && R.eq(R.invObj(94, 9), 775))) {
          int4 = 1;
        }
        break;
      }
      case 32569:
      {
        if (((R.eq(int0, 3) && R.eq(R.vb(4422), 0)) && R.eq(R.invObj(94, 9), 776))) {
          int4 = 1;
        }
        break;
      }
      case 33146:
      {
        if (((R.eq(int0, 5) && R.eq(R.vb(30607), 0)) && R.eq(R.itemCategory(R.invObj(94, 10)), 3168))) {
          int4 = 1;
        }
        break;
      }
    }
    if (R.eq(int4, 1)) {
      var string0 = string1;
      var int2 = 1;
    }
    return [string0, int2, int3, int4];
  };
  V[249] = { vb: [], vp: [], vc: [], calls: [1017, 16861] };
  S[249] = function () {
    if ((R.eq(R.call(16861, [R.mapWorld()]), 1) && R.eq(R.call(1017, []), 1))) {
      return 1;
    }
    return 0;
  };
  V[259] = { vb: [], vp: [], vc: [], calls: [] };
  S[259] = function (int0) {
    return ((((((((((((((((((((((((((((R.invTotal(93, int0)) + (R.invTotal(95, int0)) | 0)) + (R.invTotal(94, int0)) | 0)) + (R.invTotal(530, int0)) | 0)) + (R.invTotal(675, int0)) | 0)) + (R.invTotal(662, int0)) | 0)) + (R.invTotal(748, int0)) | 0)) + (R.invTotal(747, int0)) | 0)) + (R.invTotal(777, int0)) | 0)) + (R.invTotal(778, int0)) | 0)) + (R.invTotal(930, int0)) | 0)) + (R.invTotal(795, int0)) | 0)) + (R.invTotal(850, int0)) | 0)) + (R.invTotal(993, int0)) | 0)) + (R.invTotal(994, int0)) | 0);
  };
  V[273] = { vb: [], vp: [], vc: [], calls: [] };
  S[273] = function () {
    if (((R.eq(R.itemParam(R.invObj(94, 10), 2881), 2) && (!R.eq(R.invObj(94, 3), (-1 | 0)))) && (R.eq(R.invTotalParam(94, 2825), 1) || R.eq(R.invTotalParam(94, 8569), 1)))) {
      return 1;
    }
    return (-1 | 0);
  };
  V[362] = { vb: [], vp: [], vc: [], calls: [] };
  S[362] = function (int0, int1) {
    if (((int1 > 8) || (int1 < 1))) {
      return [int0, 0];
    }
    if (R.eq(int0, (-2147483648 | 0))) {
      var int0 = ((int0) + (1) | 0);
    }
    var int2 = R.pow(10, int1);
    if ((int1 > 0)) {
      return [R.idiv(int0, int2), R.mod(R.abs(int0), int2)];
    }
    return [0, 0];
  };
  V[470] = { vb: [34961, 34962, 34963, 41367, 41368, 41369, 41371, 41372, 41373, 41374, 41375, 41376, 41377, 41378, 41379, 41380, 41381, 41382, 41383, 41384, 41385, 41386], vp: [8004], vc: [], calls: [] };
  S[470] = function (int0, int1) {
    if ((R.eq(int0, (-1 | 0)) && (!R.eq(R.vp(8004), (-1 | 0))))) {
      var int0 = R.vp(8004);
    }
    switch (R.key(int0)) {
      case 32052:
      {
        switch (R.key(int1)) {
          case 1:
          {
            return [R.vb(41371), R.vb(41379)];
          }
          case 2:
          {
            return [R.vb(41372), R.vb(41380)];
          }
        }
        break;
      }
      case 32053:
      {
        switch (R.key(int1)) {
          case 1:
          {
            return [R.vb(41373), R.vb(41381)];
          }
          case 2:
          {
            return [R.vb(41374), R.vb(41382)];
          }
        }
        break;
      }
      case 32054:
      {
        switch (R.key(int1)) {
          case 1:
          {
            return [R.vb(41375), R.vb(41383)];
          }
          case 2:
          {
            return [R.vb(41376), R.vb(41384)];
          }
        }
        break;
      }
      case 32055:
      {
        switch (R.key(int1)) {
          case 1:
          {
            return [R.vb(41377), R.vb(41385)];
          }
          case 2:
          {
            return [R.vb(41378), R.vb(41386)];
          }
        }
        break;
      }
      default:
      {
        switch (R.key(int1)) {
          case 1:
          {
            return [R.vb(34961), R.vb(41367)];
          }
          case 2:
          {
            return [R.vb(34962), R.vb(41368)];
          }
          case 3:
          {
            return [R.vb(34963), R.vb(41369)];
          }
        }
        break;
      }
    }
    return [0, 0];
  };
  V[471] = { vb: [], vp: [8005, 8006], vc: [], calls: [] };
  S[471] = function (int0, int1) {
    if (R.eq(int1, 0)) {
      varplayer_8005 = R.setbit(R.vp(8005), int0);
      varplayer_8005 = R.clearbit(R.vp(8005), int0);
    } else {
      varplayer_8006 = R.setbit(R.vp(8006), int0);
      varplayer_8006 = R.clearbit(R.vp(8006), int0);
    }
    return;
  };
  V[519] = { vb: [], vp: [], vc: [], calls: [] };
  S[519] = function (int0) {
    switch (R.key(int0)) {
      case 3787:
      {
        return "Adds 45 minutes of charge to a Saradomin's Book of Wisdom or an Illuminated Book of Wisdom.";
      }
      case 3789:
      {
        return "Adds 45 minutes of charge to a Guthix's Book of Balance or an Illuminated Book of Balance.";
      }
      case 3788:
      {
        return "Adds 45 minutes of charge to a Zamorak's Book of Chaos or an Illuminated Book of Chaos.";
      }
      case 3790:
      {
        return "Adds 45 minutes of charge to a Bandos's Book of War or an Illuminated Book of War.";
      }
      case 3791:
      {
        return "Adds 45 minutes of charge to an Armadyl's Book of Law or an Illuminated Book of Law.";
      }
      case 3792:
      {
        return "Adds 45 minutes of charge to an Ancient Book or an Illuminated Ancient Book.";
      }
    }
    return "";
  };
  V[611] = { vb: [30336, 30347, 30356, 30364, 61480], vp: [], vc: [], calls: [] };
  S[611] = function (int0) {
    switch (R.key(int0)) {
      case 52501:
      {
        return R.vb(30336);
      }
      case 52503:
      {
        return R.vb(30347);
      }
      case 52502:
      {
        return R.vb(30356);
      }
      case 55189:
      {
        return R.vb(30364);
      }
      case 52504:
      {
        return R.vb(61480);
      }
    }
    return 0;
  };
  V[649] = { vb: [57763, 57764, 57765], vp: [], vc: [], calls: [12477, 19887] };
  S[649] = function () {
    if ((R.eq(R.call(19887, [8426, 900]), 1) || (R.call(12477, []) > 8440))) {
      return 0;
    }
    if (((R.eq(R.vb(57763), 1) && R.eq(R.vb(57764), 1)) && R.eq(R.vb(57765), 1))) {
      return 0;
    }
    return 1;
  };
  V[670] = { vb: [], vp: [], vc: [], calls: [] };
  S[670] = function (int0, int1, int2) {
    if (R.eq(R.itemParam(int2, 5772), 1)) {
      return R.invVar(int0, int1, 32459);
    }
    return R.invVar(int0, int1, 18550);
  };
  V[734] = { vb: [], vp: [], vc: [], calls: [] };
  S[734] = function (int0) {
    if ((int0 >= 1)) {
      return 1;
    }
    return 0;
  };
  V[756] = { vb: [], vp: [], vc: [], calls: [1379] };
  S[756] = function () {
    var int0 = R.call(1379, []);
    if ((int0 < 10)) {
      return 0;
    }
    return 1;
  };
  V[859] = { vb: [], vp: [], vc: [], calls: [16843, 16854] };
  S[859] = function () {
    return ((R.call(16854, [])) + (R.call(16843, [2])) | 0);
  };
  V[891] = { vb: [], vp: [], vc: [], calls: [] };
  S[891] = function (int0) {
    if ((int0 > 0)) {
      return 1;
    }
    return 0;
  };
  V[930] = { vb: [], vp: [], vc: [], calls: [1524] };
  S[930] = function (int0) {
    var int1 = R.itemParam(int0, 743);
    if (R.eq(int1, (-1 | 0))) {
      return 1;
    }
    if (R.eq(R.call(1524, [int1]), 0)) {
      return 0;
    }
    int1 = R.itemParam(int0, 744);
    if (R.eq(int1, (-1 | 0))) {
      return 1;
    }
    if (R.eq(R.call(1524, [int1]), 0)) {
      return 0;
    }
    int1 = R.itemParam(int0, 745);
    if (R.eq(int1, (-1 | 0))) {
      return 1;
    }
    if (R.eq(R.call(1524, [int1]), 0)) {
      return 0;
    }
    int1 = R.itemParam(int0, 746);
    if (R.eq(int1, (-1 | 0))) {
      return 1;
    }
    if (R.eq(R.call(1524, [int1]), 0)) {
      return 0;
    }
    int1 = R.itemParam(int0, 747);
    if (R.eq(int1, (-1 | 0))) {
      return 1;
    }
    if (R.eq(R.call(1524, [int1]), 0)) {
      return 0;
    }
    int1 = R.itemParam(int0, 748);
    if (R.eq(int1, (-1 | 0))) {
      return 1;
    }
    if (R.eq(R.call(1524, [int1]), 0)) {
      return 0;
    }
    return 1;
  };
  V[933] = { vb: [], vp: [], vc: [], calls: [11490] };
  S[933] = function (int0) {
    if (R.eq(int0, (-1 | 0))) {
      return 0;
    }
    if (R.eq(R.call(11490, [R.itemParam(int0, 741), R.itemParam(int0, 742)]), 0)) {
      return 0;
    }
    if (R.eq(R.call(11490, [R.itemParam(int0, 5094), R.itemParam(int0, 5095)]), 0)) {
      return 0;
    }
    return 1;
  };
  V[950] = { vb: [], vp: [], vc: [], calls: [951, 952] };
  S[950] = function (int0) {
    var string0 = "";
    var string1 = "";
    var string2 = "";
    var int1 = 0;
    var int2 = 0;
    var int3 = 0;
    var int4 = 0;
    var int5 = (-1 | 0);
    var int6 = R.enumCount(681);
    while (((int1 = (int1 + (1)) | 0) <= int6)) {
      int5 = R.enumValue(0, 17, 681, int1);
      if (R.eq(R.call(951, [int0, int5]), 1)) {
        int4 = R.call(952, [int0, int5]);
        if ((int4 > 0)) {
          string2 = R.enumValue(17, 36, 680, int5);
          if ((int2 > 0)) {
            string0 = R.cat(string0, "<br>");
          }
          string0 = R.cat(string0, `<col=00ff00>${R.s(string2)} +${R.s(R.str(int4, 10))}`);
          int2 = ((int2) + (1) | 0);
        } else {
          if ((int4 < 0)) {
            string2 = R.enumValue(0, 36, 108, int1);
            if ((int3 > 0)) {
              string1 = R.cat(string1, "<br>");
            }
            string1 = R.cat(string1, `<col=ff0000>${R.s(string2)} ${R.s(R.str(int4, 10))}`);
            int3 = ((int3) + (1) | 0);
          }
        }
      }
      int4 = 0;
      int5 = (-1 | 0);
    }
    if ((int2 > 1)) {
      string0 = R.cat("Temporarily increases:<br>", string0);
    } else {
      if ((int2 > 0)) {
        string0 = R.cat("Temporarily increases ", string0);
      }
    }
    if ((int3 > 1)) {
      string1 = R.cat("Temporarily reduces:<br>", string1);
    } else {
      if ((int3 > 0)) {
        string1 = R.cat("Temporarily reduces ", string1);
      }
    }
    var string3 = "";
    if ((R.len(string0) > 0)) {
      string3 = string0;
    }
    if ((R.len(string1) > 0)) {
      if ((R.len(string0) > 0)) {
        string3 = R.cat(string3, "<br>");
      }
      string3 = R.cat(string3, string1);
    }
    return string3;
  };
  V[951] = { vb: [], vp: [], vc: [], calls: [] };
  S[951] = function (int0, int1) {
    switch (R.key(int1)) {
      case 16:
      {
        return R.structParam(int0, 7595);
      }
      case 23:
      {
        return R.structParam(int0, 7710);
      }
      case 0:
      {
        return R.structParam(int0, 7711);
      }
      case 3:
      {
        return R.structParam(int0, 7712);
      }
      case 22:
      {
        return R.structParam(int0, 7713);
      }
      case 7:
      {
        return R.structParam(int0, 7714);
      }
      case 12:
      {
        return R.structParam(int0, 7715);
      }
      case 1:
      {
        return R.structParam(int0, 7716);
      }
      case 25:
      {
        return R.structParam(int0, 7717);
      }
      case 24:
      {
        return R.structParam(int0, 7718);
      }
      case 19:
      {
        return R.structParam(int0, 7719);
      }
      case 11:
      {
        return R.structParam(int0, 7720);
      }
      case 10:
      {
        return R.structParam(int0, 7721);
      }
      case 9:
      {
        return R.structParam(int0, 7722);
      }
      case 15:
      {
        return R.structParam(int0, 7723);
      }
      case 21:
      {
        return R.structParam(int0, 7724);
      }
      case 26:
      {
        return R.structParam(int0, 7725);
      }
      case 6:
      {
        return R.structParam(int0, 7726);
      }
      case 14:
      {
        return R.structParam(int0, 7727);
      }
      case 28:
      {
        return R.structParam(int0, 8938);
      }
      case 5:
      {
        return R.structParam(int0, 7728);
      }
      case 4:
      {
        return R.structParam(int0, 7729);
      }
      case 20:
      {
        return R.structParam(int0, 7730);
      }
      case 18:
      {
        return R.structParam(int0, 7731);
      }
      case 13:
      {
        return R.structParam(int0, 7732);
      }
      case 2:
      {
        return R.structParam(int0, 7733);
      }
      case 17:
      {
        return R.structParam(int0, 7735);
      }
      case 8:
      {
        return R.structParam(int0, 7736);
      }
    }
    return 0;
  };
  V[952] = { vb: [], vp: [], vc: [], calls: [12377] };
  S[952] = function (int0, int1) {
    if (R.eq(int0, (-1 | 0))) {
      return 0;
    }
    switch (R.key(int1)) {
      case 16:
      {
        return R.call(12377, [R.idiv(Math.imul(R.statXp(int1), 10), 1500000), 3, 17]);
      }
      case 23:
      {
        return R.call(12377, [R.idiv(Math.imul(R.statXp(int1), 10), 1500000), 3, 17]);
      }
      case 0:
      {
        return R.call(12377, [R.idiv(Math.imul(R.statXp(int1), 10), 1500000), 3, 17]);
      }
      case 3:
      {
        return R.call(12377, [R.idiv(Math.imul(R.statXp(int1), 10), 1500000), 3, 17]);
      }
      case 22:
      {
        return R.call(12377, [R.idiv(Math.imul(R.statXp(int1), 10), 1500000), 3, 17]);
      }
      case 7:
      {
        return R.call(12377, [R.idiv(Math.imul(R.statXp(int1), 10), 1500000), 3, 17]);
      }
      case 12:
      {
        return R.call(12377, [R.idiv(Math.imul(R.statXp(int1), 10), 1500000), 3, 17]);
      }
      case 1:
      {
        return R.call(12377, [R.idiv(Math.imul(R.statXp(int1), 10), 1500000), 3, 17]);
      }
      case 25:
      {
        return R.call(12377, [R.idiv(Math.imul(R.statXp(int1), 10), 1500000), 3, 17]);
      }
      case 24:
      {
        return R.call(12377, [R.idiv(Math.imul(R.statXp(int1), 10), 1500000), 3, 17]);
      }
      case 19:
      {
        return R.call(12377, [R.idiv(Math.imul(R.statXp(int1), 10), 1500000), 3, 17]);
      }
      case 11:
      {
        return R.call(12377, [R.idiv(Math.imul(R.statXp(int1), 10), 1500000), 3, 17]);
      }
      case 10:
      {
        return R.call(12377, [R.idiv(Math.imul(R.statXp(int1), 10), 1500000), 3, 17]);
      }
      case 9:
      {
        return R.call(12377, [R.idiv(Math.imul(R.statXp(int1), 10), 1500000), 3, 17]);
      }
      case 15:
      {
        return R.call(12377, [R.idiv(Math.imul(R.statXp(int1), 10), 1500000), 3, 17]);
      }
      case 21:
      {
        return R.call(12377, [R.idiv(Math.imul(R.statXp(int1), 10), 1500000), 3, 17]);
      }
      case 26:
      {
        return R.call(12377, [R.idiv(Math.imul(R.statXp(int1), 10), 1500000), 3, 17]);
      }
      case 6:
      {
        return R.call(12377, [R.idiv(Math.imul(R.statXp(int1), 10), 1500000), 3, 17]);
      }
      case 14:
      {
        return R.call(12377, [R.idiv(Math.imul(R.statXp(int1), 10), 1500000), 3, 17]);
      }
      case 28:
      {
        return R.call(12377, [R.idiv(Math.imul(R.statXp(int1), 10), 1500000), 3, 17]);
      }
      case 5:
      {
        return R.call(12377, [R.idiv(Math.imul(R.statXp(int1), 10), 1500000), 3, 17]);
      }
      case 4:
      {
        return R.call(12377, [R.idiv(Math.imul(R.statXp(int1), 10), 1500000), 3, 17]);
      }
      case 20:
      {
        return R.call(12377, [R.idiv(Math.imul(R.statXp(int1), 10), 1500000), 3, 17]);
      }
      case 18:
      {
        return R.call(12377, [R.idiv(Math.imul(R.statXp(int1), 10), 1500000), 3, 17]);
      }
      case 13:
      {
        return R.call(12377, [R.idiv(Math.imul(R.statXp(int1), 10), 1500000), 3, 17]);
      }
      case 2:
      {
        return R.call(12377, [R.idiv(Math.imul(R.statXp(int1), 10), 1500000), 3, 17]);
      }
      case 17:
      {
        return R.call(12377, [R.idiv(Math.imul(R.statXp(int1), 10), 1500000), 3, 17]);
      }
      case 8:
      {
        return R.call(12377, [R.idiv(Math.imul(R.statXp(int1), 10), 1500000), 3, 17]);
      }
    }
    return 0;
  };
  V[999] = { vb: [], vp: [], vc: [5121, 5122], calls: [] };
  S[999] = function (int0) {
    var int1 = R.itemParam(int0, 7452);
    if (R.eq(int1, (-1 | 0))) {
      return "Unknown";
    }
    var int2 = (-1 | 0);
    var int3 = (-1 | 0);
    [int2, int3] = R.dbField(int1, 119456, 0);
    if (R.eq(R.invVar(R.vc(5121), R.vc(5122), 41803), 0)) {
      return R.enumValue(0, 36, int2, R.invVar(R.vc(5121), R.vc(5122), 41802));
    }
    return R.enumValue(0, 36, int3, R.invVar(R.vc(5121), R.vc(5122), 41802));
  };
  V[1000] = { vb: [], vp: [], vc: [5121, 5122], calls: [] };
  S[1000] = function (int0) {
    var int1 = R.itemParam(int0, 7452);
    if (R.eq(int1, (-1 | 0))) {
      return "";
    }
    var int2 = R.invVar(R.vc(5121), R.vc(5122), 41804);
    if ((int2 < R.dbField(int1, 118912, 0))) {
      return "Egg";
    }
    if ((int2 < R.dbField(int1, 118928, 0))) {
      return "Child";
    }
    if ((int2 < R.dbField(int1, 118944, 0))) {
      return "Adolescent";
    }
    if ((int2 < R.dbField(int1, 118896, 0))) {
      return "Adult";
    }
    return "Elder";
  };
  V[1001] = { vb: [], vp: [], vc: [5121, 5122], calls: [] };
  S[1001] = function (int0) {
    var int1 = R.itemParam(int0, 7452);
    if (R.eq(int1, (-1 | 0))) {
      return "";
    }
    var int2 = R.enumValue(0, 73, 14342, R.invVar(R.vc(5121), R.vc(5122), 41807));
    if (R.eq(int2, 41261)) {
      int2 = R.dbField(int1, 119408, 0);
    } else {
      if (R.eq(int2, 41262)) {
        int2 = R.dbField(int1, 119424, 0);
      } else {
        if (R.eq(int2, 41263)) {
          int2 = R.dbField(int1, 119440, 0);
        }
      }
    }
    return `Trait: ${R.s(R.structParam(int2, 7456))}`;
  };
  V[1002] = { vb: [], vp: [], vc: [5121, 5122], calls: [] };
  S[1002] = function (int0) {
    var int1 = R.itemParam(int0, 7452);
    if (R.eq(int1, (-1 | 0))) {
      return "";
    }
    var int2 = R.enumValue(0, 73, 14343, R.invVar(R.vc(5121), R.vc(5122), 41808));
    if (R.eq(int2, 41261)) {
      int2 = R.dbField(int1, 119408, 0);
    } else {
      if (R.eq(int2, 41262)) {
        int2 = R.dbField(int1, 119424, 0);
      } else {
        if (R.eq(int2, 41263)) {
          int2 = R.dbField(int1, 119440, 0);
        }
      }
    }
    return `Trait: ${R.s(R.structParam(int2, 7456))}`;
  };
  V[1003] = { vb: [], vp: [], vc: [5121, 5122], calls: [] };
  S[1003] = function (int0) {
    var int1 = R.itemParam(int0, 7452);
    if (R.eq(int1, (-1 | 0))) {
      return "";
    }
    var int2 = R.enumValue(0, 73, 14344, R.invVar(R.vc(5121), R.vc(5122), 41809));
    if (R.eq(int2, 41261)) {
      int2 = R.dbField(int1, 119408, 0);
    } else {
      if (R.eq(int2, 41262)) {
        int2 = R.dbField(int1, 119424, 0);
      } else {
        if (R.eq(int2, 41263)) {
          int2 = R.dbField(int1, 119440, 0);
        }
      }
    }
    return `Trait: ${R.s(R.structParam(int2, 7456))}`;
  };
  V[1004] = { vb: [], vp: [], vc: [5121, 5122], calls: [] };
  S[1004] = function (int0) {
    var int1 = R.itemParam(int0, 7452);
    if (R.eq(int1, (-1 | 0))) {
      return "Unknown";
    }
    var int2 = (-1 | 0);
    var int3 = (-1 | 0);
    [int2, int3] = R.dbField(int1, 119456, 0);
    if (R.eq(R.invOtherVar(R.vc(5121), R.vc(5122), 41803), 0)) {
      return R.enumValue(0, 36, int2, R.invOtherVar(R.vc(5121), R.vc(5122), 41802));
    }
    return R.enumValue(0, 36, int3, R.invOtherVar(R.vc(5121), R.vc(5122), 41802));
  };
  V[1005] = { vb: [], vp: [], vc: [5121, 5122], calls: [] };
  S[1005] = function (int0) {
    var int1 = R.itemParam(int0, 7452);
    if (R.eq(int1, (-1 | 0))) {
      return "";
    }
    var int2 = R.invOtherVar(R.vc(5121), R.vc(5122), 41804);
    if ((int2 < R.dbField(int1, 118912, 0))) {
      return "Egg";
    }
    if ((int2 < R.dbField(int1, 118928, 0))) {
      return "Child";
    }
    if ((int2 < R.dbField(int1, 118944, 0))) {
      return "Adolescent";
    }
    if ((int2 < R.dbField(int1, 118896, 0))) {
      return "Adult";
    }
    return "Elder";
  };
  V[1006] = { vb: [], vp: [], vc: [5121, 5122], calls: [] };
  S[1006] = function (int0) {
    var int1 = R.itemParam(int0, 7452);
    if (R.eq(int1, (-1 | 0))) {
      return "";
    }
    var int2 = R.enumValue(0, 73, 14342, R.invOtherVar(R.vc(5121), R.vc(5122), 41807));
    if (R.eq(int2, 41261)) {
      int2 = R.dbField(int1, 119408, 0);
    } else {
      if (R.eq(int2, 41262)) {
        int2 = R.dbField(int1, 119424, 0);
      } else {
        if (R.eq(int2, 41263)) {
          int2 = R.dbField(int1, 119440, 0);
        }
      }
    }
    return `Trait: ${R.s(R.structParam(int2, 7456))}`;
  };
  V[1007] = { vb: [], vp: [], vc: [5121, 5122], calls: [] };
  S[1007] = function (int0) {
    var int1 = R.itemParam(int0, 7452);
    if (R.eq(int1, (-1 | 0))) {
      return "";
    }
    var int2 = R.enumValue(0, 73, 14343, R.invOtherVar(R.vc(5121), R.vc(5122), 41808));
    if (R.eq(int2, 41261)) {
      int2 = R.dbField(int1, 119408, 0);
    } else {
      if (R.eq(int2, 41262)) {
        int2 = R.dbField(int1, 119424, 0);
      } else {
        if (R.eq(int2, 41263)) {
          int2 = R.dbField(int1, 119440, 0);
        }
      }
    }
    return `Trait: ${R.s(R.structParam(int2, 7456))}`;
  };
  V[1008] = { vb: [], vp: [], vc: [5121, 5122], calls: [] };
  S[1008] = function (int0) {
    var int1 = R.itemParam(int0, 7452);
    if (R.eq(int1, (-1 | 0))) {
      return "";
    }
    var int2 = R.enumValue(0, 73, 14344, R.invOtherVar(R.vc(5121), R.vc(5122), 41809));
    if (R.eq(int2, 41261)) {
      int2 = R.dbField(int1, 119408, 0);
    } else {
      if (R.eq(int2, 41262)) {
        int2 = R.dbField(int1, 119424, 0);
      } else {
        if (R.eq(int2, 41263)) {
          int2 = R.dbField(int1, 119440, 0);
        }
      }
    }
    return `Trait: ${R.s(R.structParam(int2, 7456))}`;
  };
  V[1017] = { vb: [51775], vp: [], vc: [], calls: [16847] };
  S[1017] = function () {
    if ((R.eq(R.vb(51775), 1) && R.eq(R.call(16847, [7621]), 1))) {
      return 1;
    }
    if ((R.eq(R.call(16847, [7516]), 0) && R.eq(R.call(16847, [7621]), 1))) {
      return 1;
    }
    return 0;
  };
  V[1347] = { vb: [685, 2968, 2969, 2970, 2971, 2972, 2973, 2974, 2975, 2976, 2977, 2978, 2979, 2980, 2981, 2982, 2983, 2984, 2985, 2986, 2988, 2989, 2990, 2991, 2992, 2993, 2994, 2995, 2996, 2997, 2998, 2999, 3000, 3001, 3002, 3003, 3004, 3005, 3006, 3007, 16445, 27430, 27431, 28219, 28220, 28221, 28222, 28223, 28224, 28225, 28226, 28227, 28228, 28229, 28230, 28410, 30998, 38660, 40075, 40234, 40235, 42126, 43690, 45999, 46000, 46001, 46002, 46003, 46004, 46463, 47423, 47424, 52348, 55525], vp: [], vc: [], calls: [] };
  S[1347] = function () {
    var int0 = 0;
    int0 = ((((((((((((((((((((((((((((((R.vb(2983)) + (R.vb(2985)) | 0)) + (R.vb(2968)) | 0)) + (R.vb(2969)) | 0)) + (R.vb(2971)) | 0)) + (R.vb(2984)) | 0)) + (R.vb(2986)) | 0)) + (R.vb(2988)) | 0)) + (R.vb(2999)) | 0)) + (R.vb(3002)) | 0)) + (R.vb(28410)) | 0)) + (R.vb(40075)) | 0)) + (R.vb(40234)) | 0)) + (R.vb(40235)) | 0)) + (R.vb(42126)) | 0)) + (R.vb(52348)) | 0);
    int0 = ((((((((((((((((((((((((int0) + (R.vb(3004)) | 0)) + (R.vb(3005)) | 0)) + (R.vb(3007)) | 0)) + (R.vb(685)) | 0)) + (R.vb(2978)) | 0)) + (R.vb(2979)) | 0)) + (R.vb(2977)) | 0)) + (R.vb(2980)) | 0)) + (R.vb(2981)) | 0)) + (R.vb(2982)) | 0)) + (R.vb(3001)) | 0)) + (R.vb(3003)) | 0);
    int0 = ((((((((((((((((((((((((((((((int0) + (R.vb(2970)) | 0)) + (R.vb(2972)) | 0)) + (R.vb(2973)) | 0)) + (R.vb(2974)) | 0)) + (R.vb(2975)) | 0)) + (R.vb(2976)) | 0)) + (R.vb(2989)) | 0)) + (R.vb(16445)) | 0)) + (R.vb(2990)) | 0)) + (R.vb(2991)) | 0)) + (R.vb(2993)) | 0)) + (R.vb(2994)) | 0)) + (R.vb(3006)) | 0)) + (R.vb(30998)) | 0)) + (R.vb(2992)) | 0);
    int0 = ((((((((((int0) + (R.vb(3000)) | 0)) + (R.vb(2995)) | 0)) + (R.vb(2996)) | 0)) + (R.vb(2997)) | 0)) + (R.vb(27431)) | 0);
    int0 = ((((((((((((((((((((((((int0) + (R.vb(28219)) | 0)) + (R.vb(28220)) | 0)) + (R.vb(28221)) | 0)) + (R.vb(28222)) | 0)) + (R.vb(28223)) | 0)) + (R.vb(28224)) | 0)) + (R.vb(28226)) | 0)) + (R.vb(28227)) | 0)) + (R.vb(28228)) | 0)) + (R.vb(28229)) | 0)) + (R.vb(28230)) | 0)) + (R.vb(38660)) | 0);
    if ((R.vb(46463) > 0)) {
      int0 = ((int0) + (1) | 0);
    }
    int0 = ((((((((((((((((((int0) + (R.min(R.vb(45999), 1)) | 0)) + (R.vb(47423)) | 0)) + (R.vb(47424)) | 0)) + (R.vb(46000)) | 0)) + (R.vb(46001)) | 0)) + (R.vb(46002)) | 0)) + (R.vb(46003)) | 0)) + (R.min(R.vb(46004), 1)) | 0)) + (R.vb(55525)) | 0);
    if ((R.eq(R.vb(2998), 1) || R.eq(R.vb(27430), 1))) {
      int0 = ((int0) + (1) | 0);
    }
    if ((R.vb(28225) > 0)) {
      int0 = ((int0) + (1) | 0);
    }
    if ((R.vb(43690) >= 0)) {
      int0 = ((int0) + (1) | 0);
    }
    if (R.eq(int0, ((76) - (3) | 0))) {
      return 1;
    }
    return 0;
  };
  V[1379] = { vb: [28762, 28763, 28764, 28765, 28766, 28767, 28768, 28769, 28770, 28771], vp: [], vc: [], calls: [] };
  S[1379] = function () {
    var int0 = ((((((((((((((((((R.vb(28762)) + (R.vb(28763)) | 0)) + (R.vb(28764)) | 0)) + (R.vb(28765)) | 0)) + (R.vb(28766)) | 0)) + (R.vb(28767)) | 0)) + (R.vb(28768)) | 0)) + (R.vb(28769)) | 0)) + (R.vb(28770)) | 0)) + (R.vb(28771)) | 0);
    return int0;
  };
  V[1432] = { vb: [], vp: [], vc: [], calls: [] };
  S[1432] = function () {
    var int0 = ((R.statBase(0)) + (R.statBase(2)) | 0);
    var int1 = Math.imul(R.statBase(4), 2);
    var int2 = Math.imul(R.statBase(6), 2);
    var int3 = Math.imul(R.statBase(28), 2);
    var int4 = R.max(R.max(R.max(int0, int1), int2), int3);
    int4 = R.idiv(Math.imul(int4, 13), 10);
    var int5 = R.idiv(((((((((int4) + (R.statBase(1)) | 0)) + (R.statBase(3)) | 0)) + (R.idiv(R.statBase(5), 2)) | 0)) + (R.idiv(R.statBase(23), 2)) | 0), 4);
    var int6 = R.idiv(((((((((int4) + (R.statBase(1)) | 0)) + (R.statBase(3)) | 0)) + (R.idiv(R.statBase(5), 2)) | 0)) + (1) | 0), 4);
    if (R.eq(R.mapMembers(), 1)) {
      return int5;
    }
    return int6;
  };
  V[1524] = { vb: [], vp: [], vc: [], calls: [7073] };
  S[1524] = function (int0) {
    var int1 = R.call(7073, [int0]);
    if (R.eq(int1, 2)) {
      return 1;
    }
    return 0;
  };
  V[1569] = { vb: [], vp: [3217], vc: [], calls: [] };
  S[1569] = function () {
    if ((R.vp(3217) >= 800)) {
      return 0;
    }
    if ((R.vp(3217) >= 400)) {
      return 3;
    }
    return 6;
  };
  V[1764] = { vb: [], vp: [3218], vc: [], calls: [] };
  S[1764] = function () {
    if ((R.vp(3218) >= 1000)) {
      return 0;
    }
    if ((R.vp(3218) >= 800)) {
      return 3;
    }
    if ((R.vp(3218) >= 600)) {
      return 5;
    }
    if ((R.vp(3218) >= 400)) {
      return 7;
    }
    return 9;
  };
  V[2109] = { vb: [], vp: [], vc: [], calls: [] };
  S[2109] = function (int0, int1) {
    var int2 = 0;
    switch (R.key(int1)) {
      case 3:
      {
        int2 = R.itemParam(int0, 3);
        break;
      }
      case 1:
      {
        int2 = R.itemParam(int0, 3267);
        break;
      }
      case 2:
      {
        int2 = R.itemParam(int0, 4);
        break;
      }
      case 7:
      {
        int2 = R.itemParam(int0, 8879);
        break;
      }
    }
    return int2;
  };
  V[2156] = { vb: [], vp: [], vc: [], calls: [2193] };
  S[2156] = function (int0) {
    var int1 = R.call(2193, [int0]);
    if (R.eq(int1, 2)) {
      return 1;
    }
    return 0;
  };
  V[2157] = { vb: [], vp: [], vc: [], calls: [2158] };
  S[2157] = function (int0, int1) {
    return R.call(2158, [int0, 1, int1]);
  };
  V[2158] = { vb: [], vp: [], vc: [], calls: [] };
  S[2158] = function (int0, int1, int2) {
    if ((int0 < int1)) {
      return 0;
    }
    if ((int0 < int2)) {
      return 1;
    }
    return 2;
  };
  V[2178] = { vb: [], vp: [], vc: [], calls: [6802, 6804] };
  S[2178] = function (int0) {
    var int1 = R.call(6802, []);
    var int2 = R.call(6804, []);
    switch (R.key(int0)) {
      case 1:
      {
        if ((int1 >= 5)) {
          return 1;
        }
        break;
      }
      case 2:
      {
        if ((int1 >= 19)) {
          return 1;
        }
        break;
      }
      case 3:
      {
        if ((int2 >= 3)) {
          return 1;
        }
        break;
      }
      case 4:
      {
        if ((int2 >= 10)) {
          return 1;
        }
        break;
      }
      case 5:
      {
        if ((((int1) + (int2) | 0) >= 29)) {
          return 1;
        }
        break;
      }
    }
    return 0;
  };
  V[2193] = { vb: [10848, 12334, 12349, 12894, 13366], vp: [2339, 2427, 2695], vc: [], calls: [259, 2157, 12478] };
  S[2193] = function (int0) {
    if (R.eq(int0, (-1 | 0))) {
      R.call(12478, ["Quest .quest missing. Cannot determine quest_id."]);
      return 0;
    }
    switch (R.key(int0)) {
      case 92:
      {
        if ((vp2615_q_creature_of_fenkenstrain_progress > 6)) {
          return 2;
        }
        if ((R.eq(vp2615_q_creature_of_fenkenstrain_progress, 0) && R.eq(R.vb(12894), 0))) {
          return 0;
        }
        return 1;
      }
      case 140:
      {
        if ((R.vp(2339) >= 80)) {
          return 2;
        }
        return R.call(2157, [R.vb(10848), 65]);
      }
      case 111:
      {
        if ((R.vp(2695) >= 4)) {
          return 2;
        }
        return R.call(2157, [R.vb(13366), 8]);
      }
      case 275:
      {
        if (R.eq(R.testbit(vp2675_q_elemental_workshop_i_progress, 20), 1)) {
          return 2;
        }
        if (R.eq(R.testbit(vp2675_q_elemental_workshop_i_progress, 1), 0)) {
          return 0;
        }
        return 1;
      }
      case 131:
      {
        return R.call(2157, [vp1295_q_unstable_foundations_progress, 1000]);
      }
      case 70:
      {
        if ((R.eq(vp2793_q_shilo_village_progress, 0) && (R.call(259, [625]) > 0))) {
          return 1;
        }
        return R.call(2157, [vp2793_q_shilo_village_progress, 15]);
      }
      case 270:
      {
        if (R.eq(vp2426_q_underground_pass_progress, 10)) {
          return 2;
        }
        if (R.eq(R.testbit(R.vp(2427), 11), 0)) {
          return 0;
        }
        return 1;
      }
      case 324:
      {
        if ((R.vb(12349) >= 110)) {
          return 2;
        }
        return R.call(2157, [R.vb(12334), 35]);
      }
    }
    if (R.eq(R.questFinished(int0), 1)) {
      return 2;
    }
    if (R.eq(R.questStarted(int0), 1)) {
      return 1;
    }
    return 0;
  };
  V[2258] = { vb: [], vp: [3219], vc: [], calls: [] };
  S[2258] = function () {
    if ((R.vp(3219) >= 1200)) {
      return 0;
    }
    if ((R.vp(3219) >= 1000)) {
      return 3;
    }
    if ((R.vp(3219) >= 800)) {
      return 5;
    }
    if ((R.vp(3219) >= 600)) {
      return 6;
    }
    if ((R.vp(3219) >= 400)) {
      return 7;
    }
    if ((R.vp(3219) >= 300)) {
      return 8;
    }
    if ((R.vp(3219) >= 200)) {
      return 9;
    }
    return 12;
  };
  V[2475] = { vb: [], vp: [], vc: [], calls: [] };
  S[2475] = function (int0) {
    return R.idiv(((Math.imul(int0, 6)) + (9) | 0), 10);
  };
  V[2532] = { vb: [], vp: [], vc: [], calls: [18309] };
  S[2532] = function () {
    var int0 = 0;
    var int1 = 0;
    var int2 = (-1 | 0);
    int1 = R.enumCount(17159);
    while ((int0 < int1)) {
      int2 = R.enumValue(0, 33, 17159, int0);
      if ((R.call(18309, [int2]) > 0)) {
        return 1;
      }
      int0 = ((int0) + (1) | 0);
    }
    return 0;
  };
  V[2535] = { vb: [], vp: [], vc: [], calls: [20007] };
  S[2535] = function (int0, int1) {
    if ((!R.eq(R.itemUncert(int1), (-1 | 0)))) {
      var int1 = R.itemUncert(int1);
    }
    if (((R.eq(R.itemParam(int0, 2640), 14) && R.eq(R.itemParam(int1, 770), 13)) && (R.itemParam(int1, 771) <= R.call(20007, [int0])))) {
      return 1;
    }
    return 0;
  };
  V[2544] = { vb: [43240, 43241], vp: [], vc: [], calls: [] };
  S[2544] = function () {
    var int0 = 20;
    var int1 = 0;
    if (((R.vb(43240) <= R.vb(43241)) || (R.vb(43240) < 10))) {
      int1 = R.vb(43240);
    } else {
      int1 = R.max(R.scale(R.vb(43240), 100, int0), R.vb(43241));
    }
    return int1;
  };
  V[2546] = { vb: [], vp: [], vc: [], calls: [2616] };
  S[2546] = function (int0) {
    var int1 = R.stat(13);
    var int2 = ((((300) + (Math.imul(R.stat(13), 3)) | 0)) + (Math.imul(R.stat(11), 3)) | 0);
    var int3 = R.idiv(int2, 3);
    var int4 = Math.imul(int3, 2);
    var int5 = 10;
    var int6 = 6;
    switch (R.key(R.call(2616, [int0]))) {
      case 18:
      {
        if ((int1 >= 7)) {
          int6 = 2;
        } else {
          if ((int1 >= 3)) {
            int6 = 4;
          }
        }
        if ((int1 >= 4)) {
          int2 = ((int2) + (50) | 0);
        }
        break;
      }
      case 19:
      {
        if ((int1 >= 18)) {
          int6 = 2;
        } else {
          if ((int1 >= 12)) {
            int6 = 4;
          }
        }
        if ((int1 >= 11)) {
          int2 = ((int2) + (50) | 0);
        }
        if ((int1 >= 15)) {
          int2 = ((int2) + (50) | 0);
        }
        break;
      }
      case 20:
      {
        if ((int1 >= 26)) {
          int6 = 2;
        } else {
          if ((int1 >= 22)) {
            int6 = 4;
          }
        }
        if ((int1 >= 24)) {
          int2 = ((int2) + (50) | 0);
        }
        if ((int1 >= 29)) {
          int2 = ((int2) + (50) | 0);
        }
        break;
      }
      case 22:
      {
        if ((int1 >= 39)) {
          int6 = 2;
        } else {
          if ((int1 >= 35)) {
            int6 = 4;
          }
        }
        if ((int1 >= 32)) {
          int2 = ((int2) + (50) | 0);
        }
        if ((int1 >= 33)) {
          int2 = ((int2) + (50) | 0);
        }
        break;
      }
      case 23:
      {
        if ((int1 >= 44)) {
          int6 = 2;
        } else {
          if ((int1 >= 42)) {
            int6 = 4;
          }
        }
        if ((int1 >= 43)) {
          int2 = ((int2) + (50) | 0);
        }
        if ((int1 >= 45)) {
          int2 = ((int2) + (50) | 0);
        }
        break;
      }
      case 25:
      {
        if ((int1 >= 58)) {
          int6 = 2;
        } else {
          if ((int1 >= 54)) {
            int6 = 4;
          }
        }
        if ((int1 >= 52)) {
          int2 = ((int2) + (50) | 0);
        }
        if ((int1 >= 55)) {
          int2 = ((int2) + (50) | 0);
        }
        break;
      }
      case 26:
      {
        if ((int1 >= 69)) {
          int6 = 2;
        } else {
          if ((int1 >= 67)) {
            int6 = 4;
          }
        }
        if ((int1 >= 64)) {
          int2 = ((int2) + (50) | 0);
        }
        if ((int1 >= 66)) {
          int2 = ((int2) + (50) | 0);
        }
        break;
      }
      case 27:
      {
        if ((int1 >= 78)) {
          int6 = 2;
        } else {
          if ((int1 >= 73)) {
            int6 = 4;
          }
        }
        if ((int1 >= 71)) {
          int2 = ((int2) + (50) | 0);
        }
        if ((int1 >= 79)) {
          int2 = ((int2) + (50) | 0);
        }
        break;
      }
      case 28:
      {
        if ((int1 >= 85)) {
          int6 = 2;
        } else {
          if ((int1 >= 82)) {
            int6 = 4;
          }
        }
        if ((int1 >= 84)) {
          int2 = ((int2) + (50) | 0);
        }
        if ((int1 >= 88)) {
          int2 = ((int2) + (50) | 0);
        }
        break;
      }
      case 29:
      {
        if ((int1 >= 96)) {
          int6 = 2;
        } else {
          if ((int1 >= 93)) {
            int6 = 4;
          }
        }
        if ((int1 >= 95)) {
          int2 = ((int2) + (50) | 0);
        }
        if ((int1 >= 97)) {
          int2 = ((int2) + (50) | 0);
        }
        break;
      }
      case 44:
      {
        if ((int1 >= 107)) {
          int6 = 2;
        } else {
          if ((int1 >= 104)) {
            int6 = 4;
          }
        }
        if ((int1 >= 102)) {
          int2 = ((int2) + (50) | 0);
        }
        if ((int1 >= 109)) {
          int2 = ((int2) + (50) | 0);
        }
        break;
      }
    }
    var int7 = ((R.idiv(int2, int6)) + (R.mod(int2, int6)) | 0);
    return [int2, int3, int4, int5, int7];
  };
  V[2547] = { vb: [], vp: [], vc: [], calls: [2546] };
  S[2547] = function (int0) {
    var int1 = 0;
    var int2 = 0;
    [int2, int1, int1, int1, int1] = R.call(2546, [int0]);
    return int2;
  };
  V[2579] = { vb: [], vp: [], vc: [], calls: [7235, 7960, 14090] };
  S[2579] = function (int0, int1, int2, int3, int4, int5) {
    var int6 = 0;
    int6 = R.call(7960, [int0, 14]);
    var int7 = R.itemParam(int0, 7796);
    var int8 = 0;
    var int9 = 0;
    var int10 = 0;
    var int11 = 0;
    if ((!R.eq(int7, (-1 | 0)))) {
      int8 = R.dbField(int7, 147472, 0);
      int9 = ((int8) + (R.dbField(int7, 147488, 0)) | 0);
      int10 = R.dbField(int7, 147456, 0);
      int11 = R.itemParam(int0, 7804);
      if ((int11 > 0)) {
        int8 = ((int8) + (int11) | 0);
        int9 = ((int9) + (int11) | 0);
      }
    }
    var int12 = R.call(14090, [1265]);
    var int13 = R.invObj(94, 3);
    if ((((!R.eq(int13, (-1 | 0))) && (!R.eq(int12, (-1 | 0)))) && (R.call(7960, [int13, 14]) > R.call(7960, [int12, 14])))) {
      int12 = int13;
    }
    var int14 = R.call(7960, [int12, 14]);
    var int15 = 0;
    var int16 = 0;
    var int17 = 0;
    var int18 = 0;
    if (((!R.eq(int12, (-1 | 0))) && (!R.eq(R.itemParam(int12, 7796), (-1 | 0))))) {
      int15 = R.dbField(R.itemParam(int12, 7796), 147472, 0);
      int16 = ((int15) + (R.dbField(R.itemParam(int12, 7796), 147488, 0)) | 0);
      int17 = R.dbField(R.itemParam(int12, 7796), 147456, 0);
      int18 = R.itemParam(int12, 7804);
      if ((int18 > 0)) {
        int15 = ((int15) + (int18) | 0);
        int16 = ((int16) + (int18) | 0);
      }
    }
    var string0 = "";
    if (R.eq(int12, (-1 | 0))) {
      string0 = "<col=00ff00>";
    } else {
      if ((int6 > int14)) {
        string0 = "<col=00ff00>";
      } else {
        if ((int6 < int14)) {
          string0 = "<col=ff0000>";
        } else {
          string0 = R.colTag(int1);
        }
      }
    }
    var int5 = R.call(7235, [int2, int3, int4, int5, `Level : ${R.s(string0)}${R.s(R.str(int6, 10))}</col>`, ""]);
    var string1 = "";
    if ((!R.eq(int7, (-1 | 0)))) {
      string1 = "Mining damage : ";
      if (R.eq(int12, (-1 | 0))) {
        string0 = "<col=00ff00>";
      } else {
        if ((int8 > int15)) {
          string0 = "<col=00ff00>";
        } else {
          if ((int8 < int15)) {
            string0 = "<col=ff0000>";
          } else {
            string0 = R.colTag(int1);
          }
        }
      }
      string1 = R.cat(string1, `${R.s(string0)}${R.s(R.str(int8, 10))}</col> - `);
      if (R.eq(int12, (-1 | 0))) {
        string0 = "<col=00ff00>";
      } else {
        if ((int9 > int16)) {
          string0 = "<col=00ff00>";
        } else {
          if ((int9 < int16)) {
            string0 = "<col=ff0000>";
          } else {
            string0 = R.colTag(int1);
          }
        }
      }
      string1 = R.cat(string1, `${R.s(string0)}${R.s(R.str(int9, 10))}</col>`);
      int5 = R.call(7235, [int2, int3, int4, int5, string1, ""]);
      if (R.eq(int12, (-1 | 0))) {
        string0 = "<col=00ff00>";
      } else {
        if ((int10 > int17)) {
          string0 = "<col=00ff00>";
        } else {
          if ((int10 < int17)) {
            string0 = "<col=ff0000>";
          } else {
            string0 = R.colTag(int1);
          }
        }
      }
      int5 = R.call(7235, [int2, int3, int4, int5, `Rock penetration : ${R.s(string0)}${R.s(R.str(int10, 10))}</col>`, ""]);
    }
    return int5;
  };
  V[2615] = { vb: [], vp: [], vc: [], calls: [] };
  S[2615] = function (int0, int1) {
    var int2 = (-1 | 0);
    var int3 = 0;
    var int4 = 0;
    switch (R.key(int1)) {
      case 1:
      {
        int2 = R.structParam(int0, 2655);
        int3 = R.structParam(int0, 5456);
        int4 = R.structParam(int0, 2665);
        break;
      }
      case 2:
      {
        int2 = R.structParam(int0, 2656);
        int3 = R.structParam(int0, 5457);
        int4 = R.structParam(int0, 2666);
        break;
      }
      case 3:
      {
        int2 = R.structParam(int0, 2657);
        int3 = R.structParam(int0, 5458);
        int4 = R.structParam(int0, 2667);
        break;
      }
      case 4:
      {
        int2 = R.structParam(int0, 2658);
        int3 = R.structParam(int0, 5459);
        int4 = R.structParam(int0, 2668);
        break;
      }
      case 5:
      {
        int2 = R.structParam(int0, 2659);
        int3 = R.structParam(int0, 5460);
        int4 = R.structParam(int0, 2669);
        break;
      }
      case 6:
      {
        int2 = R.structParam(int0, 2660);
        int3 = R.structParam(int0, 5461);
        int4 = R.structParam(int0, 2670);
        break;
      }
      case 7:
      {
        int2 = R.structParam(int0, 2661);
        int3 = R.structParam(int0, 5462);
        int4 = R.structParam(int0, 2671);
        break;
      }
      case 8:
      {
        int2 = R.structParam(int0, 2662);
        int3 = R.structParam(int0, 5463);
        int4 = R.structParam(int0, 2672);
        break;
      }
      case 9:
      {
        int2 = R.structParam(int0, 2663);
        int3 = R.structParam(int0, 5464);
        int4 = R.structParam(int0, 2673);
        break;
      }
      case 10:
      {
        int2 = R.structParam(int0, 2664);
        int3 = R.structParam(int0, 5465);
        int4 = R.structParam(int0, 2674);
        break;
      }
      case 11:
      {
        int2 = R.structParam(int0, 5451);
        int3 = R.structParam(int0, 5466);
        int4 = R.structParam(int0, 5471);
        break;
      }
      case 12:
      {
        int2 = R.structParam(int0, 5452);
        int3 = R.structParam(int0, 5467);
        int4 = R.structParam(int0, 5472);
        break;
      }
      case 13:
      {
        int2 = R.structParam(int0, 5453);
        int3 = R.structParam(int0, 5468);
        int4 = R.structParam(int0, 5473);
        break;
      }
      case 14:
      {
        int2 = R.structParam(int0, 5454);
        int3 = R.structParam(int0, 5469);
        int4 = R.structParam(int0, 5474);
        break;
      }
      case 15:
      {
        int2 = R.structParam(int0, 5455);
        int3 = R.structParam(int0, 5470);
        int4 = R.structParam(int0, 5475);
        break;
      }
      case 16:
      {
        int2 = R.structParam(int0, 4347);
        int3 = R.structParam(int0, 4367);
        int4 = R.structParam(int0, 8906);
        break;
      }
      case 17:
      {
        int2 = R.structParam(int0, 4351);
        int3 = R.structParam(int0, 4371);
        int4 = R.structParam(int0, 8910);
        break;
      }
      case 18:
      {
        int2 = R.structParam(int0, 4363);
        int3 = R.structParam(int0, 8284);
        int4 = R.structParam(int0, 8917);
        break;
      }
    }
    return [int2, int3, int4];
  };
  V[2616] = { vb: [], vp: [], vc: [], calls: [2617] };
  S[2616] = function (int0) {
    if (R.eq(R.itemParam(int0, 7802), 1)) {
      if ((!R.eq(R.itemParam(int0, 7806), (-1 | 0)))) {
        var int0 = R.itemParam(int0, 7806);
      } else {
        int0 = R.itemParam(int0, 2655);
      }
    }
    var int1 = 0;
    var int2 = 0;
    [int1, int2] = R.call(2617, [int0]);
    return int1;
  };
  V[2617] = { vb: [], vp: [], vc: [], calls: [2615, 14490] };
  S[2617] = function (int0) {
    var int1 = 0;
    var int2 = 0;
    var int3 = (-1 | 0);
    var int4 = (-1 | 0);
    if (R.eq(R.itemParam(int0, 2655), 47066)) {
      int1 = R.itemParam(int0, 5456);
      int2 = R.call(14490, [int0, 1]);
      return [int1, int2];
    }
    int3 = R.itemParam(int0, 2675);
    if ((!R.eq(int3, (-1 | 0)))) {
      [int4, int1, int2] = R.call(2615, [int3, 1]);
      if (R.eq(int4, 47066)) {
        return [int1, int2];
      }
      [int4, int1, int2] = R.call(2615, [int3, 2]);
      if (R.eq(int4, 47066)) {
        return [int1, int2];
      }
    }
    if (R.eq(R.itemParam(int0, 2656), 47066)) {
      int1 = R.itemParam(int0, 5457);
      int2 = R.call(14490, [int0, 2]);
      return [int1, int2];
    }
    int3 = R.itemParam(int0, 2676);
    if ((!R.eq(int3, (-1 | 0)))) {
      [int4, int1, int2] = R.call(2615, [int3, 1]);
      if (R.eq(int4, 47066)) {
        return [int1, int2];
      }
      [int4, int1, int2] = R.call(2615, [int3, 2]);
      if (R.eq(int4, 47066)) {
        return [int1, int2];
      }
    }
    return [0, 0];
  };
  V[2659] = { vb: [50818], vp: [], vc: [], calls: [] };
  S[2659] = function () {
    var int0 = 3;
    if (R.eq(R.vb(50818), 1)) {
      int0 = 5;
    }
    return int0;
  };
  V[2759] = { vb: [47015, 47016, 47017, 47018, 47019], vp: [], vc: [], calls: [] };
  S[2759] = function () {
    if (R.eq(((((((((R.vb(47015)) + (R.vb(47016)) | 0)) + (R.vb(47017)) | 0)) + (R.vb(47018)) | 0)) + (R.vb(47019)) | 0), 5)) {
      return 1;
    }
    return 0;
  };
  V[2829] = { vb: [9902], vp: [], vc: [], calls: [] };
  S[2829] = function () {
    if ((R.vb(9902) >= 11)) {
      return 1;
    }
    return 0;
  };
  V[2915] = { vb: [], vp: [], vc: [], calls: [16856, 16860, 21118, 21120] };
  S[2915] = function () {
    var int0 = R.call(16856, []);
    var int1 = R.call(21118, []);
    var int2 = R.scale(((int0) + (int1) | 0), 1000, R.call(21120, []));
    return R.call(16860, [R.max(1, int2)]);
  };
  V[2916] = { vb: [], vp: [], vc: [1533], calls: [16856, 16860, 21118, 21120] };
  S[2916] = function () {
    if (R.eq(R.vc(1533), 1)) {
      return R.max(Math.imul(R.stat(3), 100), 0);
    }
    var int0 = R.call(16856, []);
    var int1 = R.call(21118, []);
    var int2 = R.scale(((int0) + (int1) | 0), 1000, R.call(21120, []));
    return R.call(16860, [R.max(1, int2)]);
  };
  V[3111] = { vb: [21682, 30332, 30338, 30340, 30341, 30354, 30359, 30360, 30361, 30362, 30980, 30982, 30983, 33705, 39926, 44244, 44245, 55115, 58286, 58288], vp: [12289], vc: [], calls: [891, 3726, 6592, 7653, 9681, 9715, 10238, 11772, 12202, 12544, 12602, 13065, 13107, 13240, 13789, 14945, 14975, 15077, 15732, 15738, 15973, 16255, 16279, 16841, 17141, 17444, 17452, 17697, 17700, 17708, 17710, 17723, 17728, 17729, 17730, 17731, 18295, 18552, 18553, 18555, 18557, 18566, 18569, 18585, 18586, 18587, 18588, 18589, 18590, 18591, 18592, 18593, 18594, 18595, 18596, 18597, 18598, 18599, 18600, 18601, 18602, 18603, 18604, 18605, 18606, 18607, 18608, 18609, 18610, 18611, 18612, 18613, 18614, 18615, 18616, 18617, 18618, 18619, 18620, 18621, 18622, 18623, 18625, 18626, 18627, 18628, 18629, 18630, 18631, 18632, 18633, 18634, 18635, 18636, 18637, 18638, 18639, 18640, 18641, 18642, 18643, 18644, 18645, 18647, 18648, 18649, 18650, 18651, 18652, 18653, 18654, 18655, 18656, 18657, 18658, 18659, 18660, 18661, 18662, 18663, 18664, 18665, 18666, 18667, 18668, 18669, 18670, 18671, 18672, 18673, 18674, 18675, 18676, 18677, 18678, 18680, 18681, 18682, 18683, 18684, 18685, 18686, 18687, 18688, 18689, 18690, 18691, 18692, 18693, 18694, 18697, 19866, 19868, 19869, 20066, 20085, 20096, 20977, 21113, 21114, 21115, 21116, 21117] };
  S[3111] = function (int0, int1, int2, string0) {
    switch (R.key(int0)) {
      case 14719:
      {
        if ((R.vb(30340) > 0)) {
          var string0 = R.call(12202, [528, R.vb(30340), string0]);
        }
        string0 = R.call(10238, [int0, string0]);
        break;
      }
      case 14720:
      {
        if ((R.vb(30341) > 0)) {
          string0 = R.call(12202, [529, R.vb(30341), string0]);
        }
        break;
      }
      case 14717:
      {
        if ((R.vb(30354) > 0)) {
          string0 = R.call(12202, [516, R.vb(30354), string0]);
        }
        if ((R.vb(30361) > 0)) {
          string0 = R.call(12202, [535, R.vb(30361), string0]);
        }
        break;
      }
      case 14710:
      {
        if ((R.vb(30360) > 0)) {
          string0 = R.call(12202, [534, (-1 | 0), string0]);
        }
        if ((R.vb(30359) > 0)) {
          string0 = R.call(12202, [520, R.vb(30359), string0]);
        }
        break;
      }
      case 14712:
      {
        if ((R.vb(30332) > 0)) {
          string0 = R.call(12202, [511, (-1 | 0), string0]);
        }
        break;
      }
      case 43909:
      {
        string0 = R.call(14975, [string0]);
        break;
      }
      case 45801:
      case 45802:
      {
        string0 = R.call(9715, [int0, int1, string0]);
        break;
      }
      case 14725:
      case 44900:
      {
        string0 = R.call(18606, [int0, int1, string0]);
        break;
      }
      case 14727:
      {
        string0 = R.call(18607, [int0, int1, string0]);
        break;
      }
      case 28431:
      {
        string0 = R.call(18608, [int0, int1, string0]);
        break;
      }
      case 14728:
      case 45046:
      {
        string0 = R.call(18609, [int0, int1, string0]);
        break;
      }
      case 14729:
      {
        string0 = R.call(18610, [int0, int1, string0]);
        break;
      }
      case 14730:
      {
        string0 = R.call(18611, [int0, int1, string0]);
        break;
      }
      case 14731:
      {
        string0 = R.call(18612, [int0, int1, string0]);
        break;
      }
      case 14733:
      {
        string0 = R.call(18613, [int0, int1, string0]);
        break;
      }
      case 39530:
      {
        string0 = R.call(18614, [int0, int1, string0]);
        break;
      }
      case 39531:
      {
        string0 = R.call(18615, [int0, int1, string0]);
        break;
      }
      case 14735:
      {
        string0 = R.call(18616, [int0, int1, string0]);
        break;
      }
      case 14736:
      {
        string0 = R.call(18617, [int0, int1, string0]);
        break;
      }
      case 19254:
      case 46275:
      {
        string0 = R.call(18618, [int0, int1, string0]);
        break;
      }
      case 31985:
      {
        string0 = R.call(18619, [int1, string0]);
        break;
      }
      case 14734:
      {
        string0 = R.call(18620, [int0, int1, string0]);
        break;
      }
      case 14726:
      {
        string0 = R.call(18621, [int1, string0]);
        break;
      }
      case 19343:
      case 45450:
      {
        string0 = R.call(18622, [int0, int1, string0]);
        break;
      }
      case 19342:
      case 47221:
      {
        string0 = R.call(18623, [int0, int1, string0]);
        break;
      }
      case 28180:
      {
        string0 = R.call(18625, [int0, int1, string0]);
        break;
      }
      case 14732:
      {
        string0 = R.call(18626, [int0, int1, string0]);
        break;
      }
      case 28927:
      case 45800:
      {
        string0 = R.call(18627, [int0, int1, string0]);
        break;
      }
      case 44946:
      {
        string0 = R.call(3726, [int0, int1, string0]);
        break;
      }
      case 44947:
      {
        string0 = R.call(13065, [int0, int1, string0]);
        break;
      }
      case 44950:
      {
        string0 = R.call(13789, [int0, int1, string0]);
        break;
      }
      case 51271:
      {
        string0 = R.call(19866, [int0, int1, string0]);
        break;
      }
      case 51273:
      {
        string0 = R.call(19868, [int0, int1, string0]);
        break;
      }
      case 51280:
      {
        string0 = R.call(19869, [string0]);
        break;
      }
      case 14822:
      case 14810:
      case 14816:
      case 14804:
      {
        string0 = R.call(16841, [int0, int1, string0]);
        break;
      }
      case 14823:
      case 14811:
      case 14817:
      case 14805:
      {
        string0 = R.call(17141, [int0, int1, string0]);
        break;
      }
      case 14819:
      case 14807:
      case 14813:
      case 14801:
      {
        string0 = R.call(18634, [int0, int1, string0]);
        break;
      }
      case 14820:
      case 14808:
      case 14814:
      case 14802:
      {
        string0 = R.call(18645, [int0, int1, string0]);
        break;
      }
      case 53079:
      {
        string0 = R.call(11772, [int0, int1, string0]);
        break;
      }
      case 53080:
      {
        string0 = R.call(16255, [int0, int1, string0]);
        break;
      }
      case 14677:
      {
        string0 = R.call(18628, [int0, int1, string0]);
        break;
      }
      case 14678:
      case 40935:
      {
        string0 = R.call(18629, [int0, int1, string0]);
        break;
      }
      case 14679:
      {
        string0 = R.call(18630, [int0, int1, string0]);
        break;
      }
      case 52781:
      {
        string0 = R.call(15732, [int0, int1, string0]);
        break;
      }
      case 44223:
      {
        string0 = R.call(18631, [int0, int1, string0]);
        break;
      }
      case 14681:
      {
        string0 = R.call(18632, [int0, int1, string0]);
        break;
      }
      case 14682:
      {
        string0 = R.call(18633, [int0, int1, string0]);
        break;
      }
      case 14683:
      {
        string0 = R.call(18635, [int0, int1, string0]);
        break;
      }
      case 14684:
      case 40936:
      case 52782:
      case 52785:
      {
        string0 = R.call(18636, [int0, int1, string0]);
        break;
      }
      case 14685:
      case 52787:
      {
        string0 = R.call(18637, [int0, int1, string0]);
        break;
      }
      case 14686:
      {
        string0 = R.call(18639, [int0, int1, string0]);
        break;
      }
      case 14688:
      {
        string0 = R.call(18640, [int0, int1, string0]);
        break;
      }
      case 14687:
      {
        string0 = R.call(18641, [int0, int1, string0]);
        break;
      }
      case 28178:
      {
        string0 = R.call(18642, [int0, int1, string0]);
        break;
      }
      case 19255:
      {
        string0 = R.call(18643, [int0, int1, string0]);
        break;
      }
      case 47129:
      case 1488:
      {
        string0 = R.call(18644, [int0, int1, string0]);
        break;
      }
      case 44244:
      case 52788:
      case 52789:
      {
        string0 = R.call(18638, [int0, int1, string0]);
        break;
      }
      case 14700:
      {
        string0 = R.call(18647, [int0, int1, string0]);
        break;
      }
      case 14701:
      case 40941:
      {
        string0 = R.call(18648, [int0, int1, string0]);
        break;
      }
      case 14702:
      {
        string0 = R.call(18649, [int0, int1, string0]);
        break;
      }
      case 14703:
      {
        string0 = R.call(18650, [int0, int1, string0]);
        break;
      }
      case 14704:
      case 52790:
      {
        string0 = R.call(18651, [int0, int1, string0]);
        break;
      }
      case 14705:
      {
        string0 = R.call(18652, [int0, int1, string0]);
        break;
      }
      case 14706:
      {
        string0 = R.call(18653, [int0, int1, string0]);
        break;
      }
      case 14707:
      {
        string0 = R.call(18654, [int0, int1, string0]);
        break;
      }
      case 14708:
      {
        string0 = R.call(18655, [int0, int1, string0]);
        break;
      }
      case 14709:
      {
        string0 = R.call(18656, [int0, int1, string0]);
        break;
      }
      case 46279:
      {
        string0 = R.call(18657, [int0, int1, string0]);
        break;
      }
      case 14663:
      {
        string0 = R.call(18677, [int1, string0]);
        break;
      }
      case 14664:
      {
        string0 = R.call(18678, [int1, string0]);
        break;
      }
      case 39532:
      {
        string0 = R.call(18680, [int1, string0]);
        break;
      }
      case 39533:
      {
        string0 = R.call(18681, [int1, string0]);
        break;
      }
      case 14665:
      {
        string0 = R.call(18682, [int1, string0]);
        break;
      }
      case 14666:
      {
        string0 = R.call(18691, [int0, int1, string0]);
        break;
      }
      case 14667:
      {
        string0 = R.call(18692, [int0, int1, string0]);
        break;
      }
      case 14668:
      case 45048:
      {
        string0 = R.call(18693, [int0, int1, string0]);
        break;
      }
      case 14669:
      {
        string0 = R.call(18683, [int1, string0]);
        break;
      }
      case 14670:
      {
        string0 = R.call(18684, [int0, int1, string0]);
        break;
      }
      case 14671:
      {
        string0 = R.call(18685, [int1, string0]);
        break;
      }
      case 31986:
      {
        string0 = R.call(18686, [int1, string0]);
        break;
      }
      case 14672:
      {
        string0 = R.call(18687, [int1, string0]);
        break;
      }
      case 14673:
      {
        string0 = R.call(18688, [int1, string0]);
        break;
      }
      case 14674:
      {
        string0 = R.call(18694, [int1, string0]);
        break;
      }
      case 52799:
      {
        string0 = R.call(15738, [int0, int1, string0]);
        break;
      }
      case 28177:
      {
        string0 = R.call(18689, [int1, string0]);
        break;
      }
      case 1491:
      {
        string0 = R.call(18690, [int1, string0]);
        break;
      }
      case 19251:
      case 46276:
      {
        string0 = R.call(18697, [int0, int1, string0]);
        break;
      }
      case 52796:
      {
        string0 = R.call(16279, [int0, int1, string0]);
        break;
      }
      case 48296:
      {
        string0 = R.call(18658, [int0, int1, string0]);
        break;
      }
      case 48297:
      {
        string0 = R.call(18659, [int0, int1, string0]);
        break;
      }
      case 48298:
      {
        string0 = R.call(18660, [int0, int1, string0]);
        break;
      }
      case 48299:
      {
        string0 = R.call(18661, [int0, int1, string0]);
        break;
      }
      case 48301:
      {
        string0 = R.call(18662, [int0, int1, string0]);
        break;
      }
      case 48302:
      {
        string0 = R.call(18663, [int0, int1, string0]);
        break;
      }
      case 48303:
      {
        string0 = R.call(18664, [int0, int1, string0]);
        break;
      }
      case 48304:
      {
        string0 = R.call(18665, [int0, int1, string0]);
        break;
      }
      case 48305:
      {
        string0 = R.call(18666, [int0, int1, string0]);
        break;
      }
      case 48306:
      {
        string0 = R.call(18667, [int0, int1, string0]);
        break;
      }
      case 48307:
      {
        string0 = R.call(18668, [int0, int1, string0]);
        break;
      }
      case 31820:
      {
        string0 = R.call(13107, [int0, int1, string0]);
        break;
      }
      case 32342:
      {
        string0 = R.call(13240, [int0, int1, string0]);
        break;
      }
      case 48308:
      {
        string0 = R.call(18669, [int0, int1, string0]);
        break;
      }
      case 48309:
      {
        string0 = R.call(18670, [int0, int1, string0]);
        break;
      }
      case 48311:
      case 48312:
      case 48313:
      {
        string0 = R.call(17731, [int0, int1, int2, string0]);
        break;
      }
      case 48314:
      {
        string0 = R.call(17730, [int0, int1, string0]);
        break;
      }
      case 48324:
      {
        string0 = R.call(18671, [int0, int1, string0]);
        break;
      }
      case 33965:
      {
        string0 = R.call(18295, [int1, string0]);
        break;
      }
      case 48326:
      case 48327:
      {
        string0 = R.call(17728, [int0, int1, string0]);
        break;
      }
      case 48328:
      {
        string0 = R.call(18672, [int0, int1, string0]);
        break;
      }
      case 48329:
      {
        string0 = R.call(18673, [int0, int1, string0]);
        break;
      }
      case 48330:
      {
        string0 = R.call(18674, [int0, int1, string0]);
        break;
      }
      case 48331:
      {
        string0 = R.call(18675, [int0, int1, string0]);
        break;
      }
      case 48332:
      {
        string0 = R.call(18676, [int0, int1, string0]);
        break;
      }
      case 49072:
      {
        string0 = `${R.s(string0)}<br>- <col=ffffff>Skeletal spirit attacks</col> apply <sprite=33146><nbsp><col=ffffff>${R.s(R.structParam(49074, 2794))}</col> to the target.`;
        string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [R.call(17444, [49072])]))}.`;
        break;
      }
      case 48352:
      case 48353:
      case 48354:
      {
        string0 = R.call(17729, [int0]);
        break;
      }
      case 14690:
      {
        string0 = R.call(18587, [int0, int1, string0]);
        break;
      }
      case 14691:
      {
        string0 = R.call(18588, [int0, int1, string0]);
        break;
      }
      case 19253:
      {
        string0 = R.call(18589, [int0, int1, string0]);
        break;
      }
      case 24188:
      {
        string0 = R.call(18590, [int0, int1, string0]);
        break;
      }
      case 24189:
      {
        string0 = R.call(18591, [int0, int1, string0]);
        break;
      }
      case 28179:
      {
        string0 = R.call(18592, [int0, int1, string0]);
        break;
      }
      case 28429:
      {
        string0 = R.call(18593, [int0, int1, string0]);
        break;
      }
      case 31649:
      {
        string0 = R.call(18594, [int0, int1, string0]);
        break;
      }
      case 31982:
      {
        string0 = R.call(18595, [int0, int1, string0]);
        break;
      }
      case 31983:
      {
        string0 = R.call(18596, [int0, int1, string0]);
        break;
      }
      case 31984:
      {
        string0 = R.call(18597, [int0, int1, string0]);
        break;
      }
      case 33650:
      {
        string0 = R.call(18598, [int0, int1, string0]);
        break;
      }
      case 37199:
      {
        string0 = R.call(18599, [int0, int1, string0]);
        break;
      }
      case 37200:
      {
        string0 = R.call(18600, [int0, int1, string0]);
        break;
      }
      case 37201:
      {
        string0 = R.call(18601, [int0, int1, string0]);
        break;
      }
      case 37202:
      {
        string0 = R.call(18602, [int0, int1, string0]);
        break;
      }
      case 37203:
      {
        string0 = R.call(18603, [int0, int1, string0]);
        break;
      }
      case 37204:
      {
        string0 = R.call(18604, [int0, int1, string0]);
        break;
      }
      case 37205:
      {
        string0 = R.call(18605, [int0, int1, string0]);
        break;
      }
      case 14715:
      {
        string0 = R.call(21113, [int0, int1, string0]);
        break;
      }
      case 14716:
      {
        string0 = R.call(21114, [int0, int1, string0]);
        break;
      }
      case 14714:
      {
        string0 = R.call(21115, [int0, int1, string0]);
        break;
      }
      case 14718:
      {
        string0 = R.call(21116, [int0, int1, string0]);
        break;
      }
    }
    var int3 = 0;
    var int4 = 0;
    var string1 = "";
    if ((!R.eq(int0, (-1 | 0)))) {
      if (R.eq(int0, 1488)) {
        int3 = 1;
      }
      int4 = R.call(17452, [int0]);
      if ((int4 > 0)) {
        if (((((((R.eq(R.structParam(int0, 2806), 6) || R.eq(R.structParam(int0, 2806), 1)) || R.eq(R.structParam(int0, 2806), 2)) || R.eq(R.structParam(int0, 2806), 3)) || R.eq(R.structParam(int0, 2806), 4)) || R.eq(R.structParam(int0, 2806), 29)) || R.eq(R.structParam(int0, 2806), 8))) {
          string0 = `${R.s(string0)}<br>- ${R.s(R.call(18555, [int4]))}.`;
        }
      } else {
        switch (R.key(R.structParam(int0, 2799))) {
          case 3:
          {
            string0 = `${R.s(string0)}<br>- ${R.s(R.call(18557, [150]))}.`;
            break;
          }
        }
      }
      switch (R.key(int0)) {
        case 40935:
        {
          string1 = `${R.s(string1)}<br><col=969696>Maximum additional damage duration: ${R.s(R.call(15973, [Math.imul(1, 10), 1]))}.</col>`;
          break;
        }
        case 14682:
        {
          if ((R.statBase(0) >= 54)) {
            string1 = `${R.s(string1)}<br><col=969696>${R.s(R.call(18569, [2]))}.</col>`;
          }
          if (R.eq(R.vb(39926), 1)) {
            string1 = `${R.s(string1)}<br><col=969696>${R.s(R.call(12544, []))}.</col>`;
          }
          break;
        }
        case 14665:
        {
          if (R.eq(R.vb(44245), 1)) {
            string1 = `${R.s(string1)}<br><col=969696>${R.s(R.call(18569, [2]))}.</col>`;
          }
          string1 = `${R.s(string1)}<br><col=969696>Distance varies based on the attack range of your main-hand ranged weapon.</col>`;
          break;
        }
        case 14664:
        {
          if ((R.statBase(4) >= 54)) {
            string1 = `${R.s(string1)}<br><col=969696>${R.s(R.call(18569, [2]))}.</col>`;
          }
          if (R.eq(R.vb(39926), 1)) {
            string1 = `${R.s(string1)}<br><col=969696>${R.s(R.call(12544, []))}.</col>`;
          }
          break;
        }
        case 14726:
        {
          if (R.eq(R.vb(44244), 1)) {
            string1 = `${R.s(string1)}<br><col=969696>${R.s(R.call(18569, [2]))}.</col>`;
          }
          break;
        }
        case 14727:
        {
          if ((R.statBase(6) >= 54)) {
            string1 = `${R.s(string1)}<br><col=969696>${R.s(R.call(18569, [2]))}.</col>`;
          }
          if (R.eq(R.vb(39926), 1)) {
            string1 = `${R.s(string1)}<br><col=969696>${R.s(R.call(12544, []))}.</col>`;
          }
          break;
        }
        case 48314:
        {
          string1 = `${R.s(string1)}<br><col=969696>Prioritises enemies with higher maximum life points. If there are no enemies nearby it will bounce to the caster dealing no damage.</col>`;
          break;
        }
        case 28179:
        {
          string1 = `${R.s(string1)}<br><col=969696>Healing is increased the closer you are to the area.`;
          break;
        }
        case 31649:
        {
          string1 = `${R.s(string1)}<br><col=969696>Empowered effect cooldown: ${R.s(R.call(15973, [200, 1]))}.</col>`;
          break;
        }
        case 31982:
        {
          string1 = `${R.s(string1)}<br><col=969696>Maximum stacks: ${R.s(R.str(10, 10))}.</col>`;
          break;
        }
        case 33650:
        {
          string1 = `${R.s(string1)}<br><col=969696>Damage dealt has diminishing returns in PvP.`;
          break;
        }
        case 14718:
        {
          string1 = `${R.s(string1)}<br><col=969696>Maximum stacks: ${R.s(R.str(R.call(21117, []), 10))}.</col>`;
          break;
        }
      }
      if (((R.eq(R.structParam(int0, 2806), 4) && (!R.eq(R.structParam(int0, 2880), 5))) && (R.structParam(int0, 2879) > 0))) {
        string1 = `${R.s(string1)}<br><col=969696>Damage scales up to level ${R.s(R.str(R.structParam(int0, 2879), 10))}.</col>`;
      }
      if ((!R.eq(R.structParam(int0, 9090), 100))) {
        string1 = `${R.s(string1)}<br><col=969696>${R.s(R.call(18586, [R.structParam(int0, 9090)]))}.</col>`;
      }
      if ((((R.eq(int0, 39532) || R.eq(int0, 39533)) || R.eq(int0, 39530)) || R.eq(int0, 39531))) {
        string1 = `${R.s(string1)}<br><col=969696>Only 1x1 enemies are affected by knock-back.</col>`;
      }
      if (R.eq(R.structParam(int0, 9091), 0)) {
        string1 = `${R.s(string1)}<br><col=969696>Cannot critically strike.</col>`;
      } else {
        if (R.eq(R.structParam(int0, 9092), 1)) {
          string1 = `${R.s(string1)}<br><col=969696>Guaranteed to critically strike.</col>`;
        }
      }
      if (R.eq(R.structParam(int0, 5550), 1)) {
        string1 = `${R.s(string1)}<br><col=969696>${R.s(R.call(18585, [R.call(891, [int4]), int3]))}.</col>`;
      }
      if ((R.eq(R.structParam(int0, 2799), 0) && R.eq(R.vb(55115), 0))) {
        string1 = `${R.s(string1)}<br><col=969696>Automatically triggered during combat.</col>`;
      }
      if (R.eq(R.structParam(int0, 9089), 1)) {
        string1 = `${R.s(string1)}<br><col=969696>Area targeting varies based on the attack range of your main-hand weapon.</col>`;
      }
      if (R.eq(R.structParam(int0, 9407), 1)) {
        string1 = `${R.s(string1)}<br>${R.s(R.call(12602, []))}`;
      }
      if (R.eq(R.structParam(int0, 9087), 1)) {
        string1 = `${R.s(string1)}<br><col=969696>Can move while channelling.</col>`;
      }
      if ((!R.eq(R.structParam(int0, 8929), 0))) {
        string1 = `${R.s(string1)}<br><col=969696>Aspect of ${R.s(R.call(17700, [int0]))}: You may only have one aspect active at once.</col>`;
      }
      if ((R.structParam(int0, 6608) > 0)) {
        string1 = `${R.s(string1)}<br><col=969696>${R.s(R.call(17723, [R.structParam(int0, 6608)]))}.</col>`;
      }
      if ((R.eq(R.structParam(int0, 6528), 1) && R.eq(R.vb(21682), 1))) {
        string1 = `${R.s(string1)}<br><col=969696>Must be manually triggered during revolution combat.</col>`;
      }
      if ((!R.eq(R.structParam(int0, 9405), 3))) {
        string1 = `${R.s(string1)}<br><col=969696>Applies a ${R.s(R.call(14945, [R.structParam(int0, 9405), 1]))} global cooldown.</col>`;
      }
      if ((R.len(string1) > 0)) {
        string0 = `${R.s(string0)}<br>${R.s(string1)}`;
      }
    }
    if (R.eq(R.structParam(int0, 2799), 0)) {
      string0 = `${R.s(string0)}${R.s(R.call(17697, [int0]))}`;
    }
    switch (R.key(int0)) {
      case 14709:
      {
        string0 = R.call(17708, [51665, string0]);
        break;
      }
      case 14679:
      {
        string0 = R.call(15077, [string0]);
        break;
      }
      case 52796:
      {
        string0 = R.call(17708, [52802, string0]);
        break;
      }
      case 52799:
      {
        string0 = R.call(17708, [52801, string0]);
        break;
      }
      case 19342:
      {
        string0 = R.call(17708, [46308, string0]);
        break;
      }
      case 47221:
      {
        string0 = R.call(17708, [46309, string0]);
        break;
      }
      case 14725:
      {
        string0 = R.call(17708, [52778, string0]);
        break;
      }
      case 14731:
      {
        string0 = R.call(17708, [45563, string0]);
        break;
      }
      case 44947:
      {
        string0 = R.call(17708, [44820, string0]);
        string0 = `${R.s(string0)}<br><br><col=969696>Maximum stacks: ${R.s(R.strLoc(12, 1))}.</col>`;
        break;
      }
      case 44950:
      {
        string0 = R.call(17708, [44066, string0]);
        string0 = `${R.s(string0)}<br><br><col=969696>Maximum stacks: ${R.s(R.strLoc(5, 1))}.</col>`;
        break;
      }
      case 51271:
      {
        string0 = R.call(17708, [51272, string0]);
        string0 = `${R.s(string0)}<br><br><col=969696>Maximum chance: ${R.s(R.call(7653, [200, 1, 1, 0, 1]))}%.</col>`;
        break;
      }
      case 53079:
      {
        string0 = R.call(17708, [53077, string0]);
        break;
      }
      case 53080:
      {
        string0 = R.call(17708, [53078, string0]);
        break;
      }
      case 44946:
      {
        string0 = R.call(17708, [44040, string0]);
        break;
      }
      case 48308:
      {
        string0 = R.call(17708, [48338, string0]);
        break;
      }
      case 48307:
      {
        string0 = R.call(17708, [48344, string0]);
        string0 = `${R.s(string0)}<br>- Takes up to <col=ffffff>${R.s(R.str(R.call(20066, []), 10))}%</col> bonus damage from all attacks, capped at <col=ffffff>${R.s(R.call(18566, [48307, R.call(20085, []), int1]))}.`;
        break;
      }
      case 48326:
      case 48327:
      {
        string0 = R.call(17708, [48340, string0]);
        break;
      }
      case 48330:
      {
        string0 = R.call(17708, [48345, string0]);
        if (R.eq(R.call(20977, []), 0)) {
          string0 = `${R.s(string0)}<br><br><col=969696>Life points cap: ${R.s(R.strLoc(30000, 1))}.</col>`;
        }
        break;
      }
      case 48331:
      {
        string0 = R.call(17708, [48343, string0]);
        break;
      }
      case 49072:
      {
        string0 = R.call(17708, [49074, string0]);
        string0 = `${R.s(string0)}<br><br><col=969696>Maximum stacks: ${R.s(R.strLoc(200, 1))}.</col>`;
        break;
      }
      case 14718:
      {
        string0 = R.call(17708, [3636, string0]);
        break;
      }
    }
    var string2 = "";
    switch (R.key(int0)) {
      case 14682:
      {
        if ((R.vb(33705) > 0)) {
          string2 = `${R.s(string2)}<br>${R.s(R.call(18553, [778, R.vb(33705)]))}`;
        }
        break;
      }
      case 14678:
      case 40935:
      {
        if ((R.vb(30338) > 0)) {
          string2 = `${R.s(string2)}<br>${R.s(R.call(18553, [512, (-1 | 0)]))}`;
        }
        break;
      }
      case 44244:
      {
        if ((R.vb(30982) > 0)) {
          string2 = `${R.s(string2)}<br>${R.s(R.call(18553, [569, R.vb(30982)]))}`;
        }
        break;
      }
      case 14686:
      {
        if (R.eq(R.call(9681, [1]), 1)) {
          string2 = `${R.s(string2)}<br>${R.s(R.call(18552, [1]))}`;
        }
        break;
      }
      case 52781:
      {
        if (R.eq(R.call(6592, []), 1)) {
          string2 = `${R.s(string2)}<br>${R.s(R.call(18552, [9]))}`;
        }
        break;
      }
      case 14688:
      {
        break;
      }
      case 47129:
      case 1488:
      {
        if ((R.vb(30338) > 0)) {
          string2 = `${R.s(string2)}<br>${R.s(R.call(18553, [512, (-1 | 0)]))}`;
        }
        break;
      }
      case 14664:
      {
        if ((R.vb(33705) > 0)) {
          string2 = `${R.s(string2)}<br>${R.s(R.call(18553, [778, R.vb(33705)]))}`;
        }
        break;
      }
      case 14668:
      case 45048:
      {
        if ((R.vb(30980) > 0)) {
          string2 = `${R.s(string2)}<br>${R.s(R.call(18553, [568, R.vb(30980)]))}`;
        }
        break;
      }
      case 14667:
      {
        if ((R.vb(30982) > 0)) {
          string2 = `${R.s(string2)}<br>${R.s(R.call(18553, [569, R.vb(30982)]))}`;
        }
        break;
      }
      case 14674:
      {
        if (R.eq(R.call(9681, [2]), 1)) {
          string2 = `${R.s(string2)}<br>${R.s(R.call(18552, [1]))}`;
        }
        break;
      }
      case 19251:
      {
        if ((R.vb(30983) > 0)) {
          string2 = `${R.s(string2)}<br>${R.s(R.call(18553, [570, (-1 | 0)]))}`;
        }
        break;
      }
      case 14665:
      {
        if ((R.vb(30338) > 0)) {
          string2 = `${R.s(string2)}<br>${R.s(R.call(18553, [512, (-1 | 0)]))}`;
        }
        break;
      }
      case 14727:
      case 28431:
      {
        if ((R.vb(33705) > 0)) {
          string2 = `${R.s(string2)}<br>${R.s(R.call(18553, [778, R.vb(33705)]))}`;
        }
        break;
      }
      case 14728:
      case 45046:
      {
        if ((R.vb(30980) > 0)) {
          string2 = `${R.s(string2)}<br>${R.s(R.call(18553, [568, R.vb(30980)]))}`;
        }
        break;
      }
      case 14729:
      {
        if ((R.vb(30982) > 0)) {
          string2 = `${R.s(string2)}<br>${R.s(R.call(18553, [569, R.vb(30982)]))}`;
        }
        break;
      }
      case 14736:
      {
        if (R.eq(R.call(9681, [3]), 1)) {
          string2 = `${R.s(string2)}<br>${R.s(R.call(18552, [1]))}`;
        }
        break;
      }
      case 19254:
      {
        if ((R.vb(30983) > 0)) {
          string2 = `${R.s(string2)}<br>${R.s(R.call(18553, [570, (-1 | 0)]))}`;
        }
        if ((R.vb(58286) >= 3)) {
          string2 = `${R.s(string2)}<br>${R.s(R.call(20096, [52071]))}`;
        }
        break;
      }
      case 46275:
      {
        if ((R.vb(58286) >= 3)) {
          string2 = `${R.s(string2)}<br>${R.s(R.call(20096, [52071]))}`;
        }
        break;
      }
      case 14726:
      {
        if ((R.vb(30338) > 0)) {
          string2 = `${R.s(string2)}<br>${R.s(R.call(18553, [512, (-1 | 0)]))}`;
        }
        break;
      }
      case 14731:
      {
        if ((R.vb(58286) >= 4)) {
          string2 = `${R.s(string2)}<br>${R.s(R.call(20096, [52071]))}`;
        }
        break;
      }
      case 48299:
      {
        if ((R.vb(33705) > 0)) {
          string2 = `${R.s(string2)}<br>${R.s(R.call(18553, [778, R.vb(33705)]))}`;
        }
        break;
      }
      case 48314:
      {
        if (R.eq(R.call(9681, [7]), 1)) {
          string2 = `${R.s(string2)}<br>${R.s(R.call(18552, [1]))}`;
        }
        break;
      }
      case 48307:
      {
        if ((R.eq(R.vb(58288), 2) && R.eq(R.vp(12289), 1))) {
          string2 = `${R.s(string2)}<br>${R.s(R.call(18552, [13]))}`;
        }
        break;
      }
      case 19253:
      {
        if ((R.vb(30341) > 0)) {
          string2 = `${R.s(string2)}<br>${R.s(R.call(18553, [529, R.vb(30341)]))}`;
        }
        break;
      }
      case 14714:
      {
        if ((R.vb(30362) > 0)) {
          string2 = `${R.s(string2)}<br>${R.s(R.call(18553, [536, R.vb(30362)]))}`;
        }
        break;
      }
    }
    if ((R.len(string2) > 0)) {
      string0 = `${R.s(string0)}<br>${R.s(string2)}`;
    }
    return string0;
  };
  V[3354] = { vb: [], vp: [], vc: [], calls: [3382, 3386, 3985] };
  S[3354] = function (int0, int1, string0) {
    var int2 = R.call(3386, [int0, int1]);
    var int3 = 0;
    var int4 = 0;
    var int5 = 0;
    [int3, int4, int5] = R.call(3985, [int0, int1]);
    if ((int2 > 0)) {
      if ((int3 > 0)) {
        var string0 = R.call(3382, [int3, int4, int5, (-1 | 0), 1, 0]);
      } else {
        if ((int4 > 0)) {
          string0 = `<col=DD4400>${R.s(R.call(3382, [(-1 | 0), int4, int5, (-1 | 0), 3, 0]))}</col>`;
        } else {
          if ((int5 > 0)) {
            string0 = `<col=DD0000>${R.s(R.call(3382, [(-1 | 0), (-1 | 0), int5, (-1 | 0), 0, 0]))}</col>`;
          }
        }
      }
    }
    return string0;
  };
  V[3381] = { vb: [], vp: [], vc: [], calls: [] };
  S[3381] = function (int0) {
    var int1 = 0;
    var int2 = 0;
    var int3 = 0;
    [int1, int2, int3] = R.dateRunedayToDate(int0);
    var string0 = R.enumValue(0, 36, 10036, int2);
    return `${R.s(R.str(int1, 10))}-${R.s(string0)}-${R.s(R.str(int3, 10))}`;
  };
  V[3382] = { vb: [], vp: [], vc: [], calls: [15908] };
  S[3382] = function (int0, int1, int2, int3, int4, int5) {
    return R.call(15908, [int0, (-1 | 0), (-1 | 0), int1, int2, int3, 0, int4, (-1 | 0), int5, int5, int5, int5, int5, int5]);
  };
  V[3386] = { vb: [], vp: [], vc: [], calls: [] };
  S[3386] = function (int0, int1) {
    var int2 = 0;
    if (R.eq(int1, 0)) {
      int2 = ((R.dateMinutesFromRuneday(int0)) - (R.dateMinutes()) | 0);
    } else {
      int2 = ((R.dateMinutesFromRuneday(((int0) + (1) | 0))) - (R.dateMinutes()) | 0);
    }
    return int2;
  };
  V[3509] = { vb: [], vp: [], vc: [], calls: [] };
  S[3509] = function (int0) {
    if (R.eq(int0, (-1 | 0))) {
      return "";
    }
    return R.itemName(int0);
  };
  V[3721] = { vb: [60643], vp: [], vc: [], calls: [20699, 20700] };
  S[3721] = function () {
    var int0 = R.call(20699, []);
    var int1 = R.call(20700, []);
    var string0 = "Pool Synergy:";
    var string1 = "2+ pools : Lobsters and swordfish grow 0.6 seconds faster.";
    var string2 = "3+ pools : Sharks grow 0.6 seconds faster.";
    var string3 = "4 pools : Giant crayfish grow 1.2 seconds faster.";
    if ((R.vb(60643) < 4)) {
      string3 = `<col=969696>${R.s(string3)}</col>`;
    }
    if ((R.vb(60643) < 3)) {
      string2 = `<col=969696>${R.s(string2)}</col>`;
    }
    if ((R.vb(60643) < 2)) {
      string1 = `<col=969696>${R.s(string1)}</col>`;
    }
    string0 = `${R.s(string0)}<br><br>${R.s(string1)}<br>${R.s(string2)}<br>${R.s(string3)}`;
    return string0;
  };
  V[3726] = { vb: [], vp: [], vc: [], calls: [15973, 17444, 17710] };
  S[3726] = function (int0, int1, string0) {
    var int2 = R.call(17444, [int0]);
    var string0 = `${R.s(string0)}<br>- Applies <sprite=14764><nbsp><col=ffffff>${R.s(R.structParam(int0, 2794))}</col> to self.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [int2]))}.`;
    string0 = `${R.s(string0)}<br>- Can be recast to extend duration by <col=ffffff>${R.s(R.call(15973, [int2, 1]))}</col> per cast to a maximum of <col=ffffff>${R.s(R.call(15973, [6000, 1]))}</col>.`;
    return string0;
  };
  V[3862] = { vb: [39460, 39461, 39462, 39463, 39464], vp: [], vc: [], calls: [] };
  S[3862] = function () {
    if ((((((((((R.vb(39460)) + (R.vb(39461)) | 0)) + (R.vb(39464)) | 0)) + (R.vb(39462)) | 0)) + (R.vb(39463)) | 0) >= 5)) {
      return 1;
    }
    return 0;
  };
  V[3899] = { vb: [], vp: [], vc: [], calls: [930, 933, 1432] };
  S[3899] = function (int0) {
    if (R.eq(R.call(930, [int0]), 0)) {
      return 0;
    }
    if (R.eq(R.call(933, [int0]), 0)) {
      return 0;
    }
    if ((R.call(1432, []) < R.itemParam(int0, 761))) {
      return 0;
    }
    return 1;
  };
  V[3939] = { vb: [], vp: [], vc: [], calls: [] };
  S[3939] = function (string0) {
    var string0 = `${R.s(string0)}<br>For each piece of magic tank equipment worn:<br>- Gain <col=ffffff>10%</col> of its armour value as flat damage reduction.<br>- Gain <col=ffffff>25%</col> of your defence level as flat damage reduction.<br>- Only core damage types can be reduced by this effect.<br>- Damage cannot be reduced by more than <col=ffffff>60%</col> due to this effect.<br>- Effectiveness reduced by <col=ffffff>${R.s(R.str(66, 10))}%</col> in PvP.`;
    return string0;
  };
  V[3940] = { vb: [], vp: [], vc: [], calls: [] };
  S[3940] = function (int0, int1) {
    var string0 = "";
    switch (R.key(int1)) {
      case 1:
      {
        string0 = "Cruor";
        break;
      }
      case 2:
      {
        string0 = "Glacies";
        break;
      }
      case 3:
      {
        string0 = "Umbra";
        break;
      }
      case 4:
      {
        string0 = "Fumus";
        break;
      }
    }
    switch (R.key(int0)) {
      case 1875:
      {
        string0 = R.cat(string0, ", the first to fall.");
        break;
      }
      case 1876:
      {
        string0 = R.cat(string0, ", the second to fall.");
        break;
      }
      case 1877:
      {
        string0 = R.cat(string0, ", the third to fall.");
        break;
      }
      case 1878:
      {
        string0 = R.cat(string0, ", the last to fall.");
        break;
      }
    }
    return string0;
  };
  V[3956] = { vb: [], vp: [], vc: [], calls: [14945] };
  S[3956] = function (int0) {
    var string0 = "";
    string0 = R.cat(string0, `Summoning level: ${R.s(R.str(R.dbField(int0, 348176, 0), 10))}`);
    string0 = R.cat(string0, `<br>Duration: ${R.s(R.str(R.dbField(int0, 348192, 0), 10))} minutes`);
    string0 = R.cat(string0, `<br>${R.s(R.dbField(int0, 348208, 0))}`);
    var int1 = R.dbField(int0, 348256, 0);
    if ((!R.eq(int1, (-1 | 0)))) {
      if (R.eq(R.itemMembers(int1), 1)) {
        string0 = R.cat(string0, `<br><br><col=F8D56B>${R.s(R.itemName(int1))}</col>`);
      } else {
        string0 = R.cat(string0, `<br><br><col=B8D1D1>${R.s(R.itemName(int1))}</col>`);
      }
      string0 = R.cat(string0, `<br>${R.s(R.dbField(int0, 348272, 0))}`);
      string0 = R.cat(string0, `<br>${R.s(R.call(14945, [R.dbField(int0, 348368, 0), 1]))} cooldown.</col>`);
    }
    var string1 = "";
    switch (R.key(R.dbField(int0, 348448, 0))) {
      case 1:
      {
        string1 = "Passive";
        break;
      }
      case 2:
      {
        string1 = "Cannot fight";
        break;
      }
      default:
      {
        string1 = "Aggressive";
        break;
      }
    }
    string0 = R.cat(string0, `<br><br>Default combat mode: ${R.s(string1)}`);
    return string0;
  };
  V[3985] = { vb: [], vp: [], vc: [], calls: [3386] };
  S[3985] = function (int0, int1) {
    var int2 = R.call(3386, [int0, int1]);
    var int3 = R.idiv(int2, 1440);
    var int4 = R.idiv(R.mod(int2, 1440), 60);
    var int5 = R.mod(int2, 60);
    return [int3, int4, int5];
  };
  V[4000] = { vb: [43336], vp: [], vc: [], calls: [7081] };
  S[4000] = function (int0) {
    var int1 = R.itemParam(int0, 7863);
    var int2 = R.itemParam(int0, 7864);
    if ((int2 > 0)) {
      if (((R.invTotal(93, 6714) > 0) || (R.invTotal(94, 6714) > 0))) {
        int2 = ((int2) + (2) | 0);
      }
      int1 = ((int1) + (R.scale(int2, 100, R.call(7081, []))) | 0);
    }
    switch (R.key(int0)) {
      case 21386:
      case 21387:
      case 21388:
      {
        if (R.eq(R.vb(43336), 0)) {
          return 0;
        }
        break;
      }
    }
    return int1;
  };
  V[4034] = { vb: [15176, 15177, 15178], vp: [], vc: [], calls: [7167] };
  S[4034] = function (int0) {
    if (R.eq(int0, (-1 | 0))) {
      return 0;
    }
    if ((!R.eq(R.itemWearpos(int0), 1))) {
      return 0;
    }
    var int1 = R.itemParam(int0, 7897);
    if (R.eq(int1, 0)) {
      return 0;
    }
    if (((!R.eq(int1, 1)) && R.eq(R.itemParam(int0, 258), 1))) {
      return 0;
    }
    if ((((R.eq(R.vb(15176), 1) || R.eq(R.vb(15177), 1)) || R.eq(R.vb(15178), 1)) && R.eq(R.call(7167, [99, 1]), 1))) {
      return 1;
    }
    return 0;
  };
  V[4148] = { vb: [20806], vp: [], vc: [], calls: [] };
  S[4148] = function () {
    if (R.eq(R.vb(20806), 1)) {
      return 1;
    }
    return 0;
  };
  V[4229] = { vb: [], vp: [], vc: [], calls: [] };
  S[4229] = function (int0, int1, int2, string0) {
    if ((int0 <= int1)) {
      return "<col=FF0000>";
    }
    if ((int0 < int2)) {
      return string0;
    }
    return "<col=00FF00>";
  };
  V[4344] = { vb: [707, 712, 718, 720, 732, 734, 735, 737, 751, 758, 766, 767, 773, 789, 791, 792, 814, 871, 903, 907, 909, 910, 912, 925, 929, 933, 934, 935, 937, 938, 950, 952, 960, 17532, 17533, 18036, 18037, 18038, 18039, 18227, 18322, 18323, 18324, 18325, 20206, 20207, 20208, 20209, 20210, 20211, 20212, 20213, 20214, 20215, 20216, 20217, 20218, 20219, 20220, 20221, 20222, 20223, 20224, 20225, 20226, 20227, 20228, 20229, 20230, 20231, 20232, 20233, 20234, 20235, 20236, 20237, 20238, 20239, 20240, 20241, 20242, 20243, 20244, 20245, 20247, 20249, 20251, 20252, 20253, 20256, 20258, 20259, 20260, 20261, 20262, 20263, 20264, 20265, 20266, 20267, 20268, 20269, 20270, 20271, 20273, 20274, 20275, 20277, 20278, 20279, 20280, 20281, 20282, 20283, 21001, 21002, 21362, 21363, 21365, 21366, 22138, 22264, 22265, 22266, 22267, 22828, 22870, 22897, 22898, 26210, 26478, 26479, 26480, 26481, 26482, 26483], vp: [1246], vc: [], calls: [10881] };
  S[4344] = function () {
    if ((R.vp(1246) < 16383)) {
      return 0;
    }
    if (R.eq(R.vb(22870), 0)) {
      return 0;
    }
    if (R.eq(R.vb(22897), 0)) {
      return 0;
    }
    if (R.eq(R.vb(18324), 0)) {
      return 0;
    }
    if (R.eq(R.vb(18323), 0)) {
      return 0;
    }
    if (R.eq(R.vb(18322), 0)) {
      return 0;
    }
    if (R.eq(R.vb(18325), 0)) {
      return 0;
    }
    if (R.eq(R.vb(18036), 0)) {
      return 0;
    }
    if (R.eq(R.vb(18037), 0)) {
      return 0;
    }
    if (R.eq(R.vb(18038), 0)) {
      return 0;
    }
    if (R.eq(R.vb(18039), 0)) {
      return 0;
    }
    if (R.eq(R.vb(792), 0)) {
      return 0;
    }
    if (R.eq(R.vb(871), 0)) {
      return 0;
    }
    if (R.eq(R.vb(26478), 0)) {
      return 0;
    }
    if (R.eq(R.vb(26479), 0)) {
      return 0;
    }
    if (R.eq(R.vb(26480), 0)) {
      return 0;
    }
    if (R.eq(R.vb(26481), 0)) {
      return 0;
    }
    if (R.eq(R.vb(26482), 0)) {
      return 0;
    }
    if (R.eq(R.vb(26483), 0)) {
      return 0;
    }
    if (R.eq(R.vb(22267), 0)) {
      return 0;
    }
    if (R.eq(R.vb(22265), 0)) {
      return 0;
    }
    if (R.eq(R.vb(22264), 0)) {
      return 0;
    }
    if (R.eq(R.vb(22267), 0)) {
      return 0;
    }
    if (R.eq(R.vb(22265), 0)) {
      return 0;
    }
    if (R.eq(R.vb(21362), 0)) {
      return 0;
    }
    if (R.eq(R.vb(21366), 0)) {
      return 0;
    }
    if (R.eq(R.vb(21365), 0)) {
      return 0;
    }
    if (R.eq(R.vb(21002), 0)) {
      return 0;
    }
    if (R.eq(R.vb(21001), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20206), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20207), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20208), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20209), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20211), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20210), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20212), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20213), 0)) {
      return 0;
    }
    if (R.eq(R.vb(767), 0)) {
      return 0;
    }
    if (R.eq(R.vb(766), 0)) {
      return 0;
    }
    if (R.eq(R.vb(18227), 0)) {
      return 0;
    }
    if (R.eq(R.vb(22898), 0)) {
      return 0;
    }
    if (R.eq(R.vb(21363), 0)) {
      return 0;
    }
    if (R.eq(R.vb(22266), 0)) {
      return 0;
    }
    if (R.eq(R.vb(26210), 0)) {
      return 0;
    }
    if (R.eq(R.vb(720), 0)) {
      return 0;
    }
    if (R.eq(R.vb(712), 0)) {
      return 0;
    }
    if (R.eq(R.vb(718), 0)) {
      return 0;
    }
    if (R.eq(R.vb(960), 0)) {
      return 0;
    }
    if (R.eq(R.vb(952), 0)) {
      return 0;
    }
    if (R.eq(R.vb(925), 0)) {
      return 0;
    }
    if (R.eq(R.vb(950), 0)) {
      return 0;
    }
    if (R.eq(R.vb(929), 0)) {
      return 0;
    }
    if (R.eq(R.vb(789), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20214), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20218), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20233), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20217), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20231), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20216), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20219), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20230), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20228), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20236), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20232), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20223), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20229), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20226), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20227), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20235), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20242), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20220), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20234), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20221), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20241), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20222), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20240), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20238), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20215), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20239), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20225), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20224), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20243), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20237), 0)) {
      return 0;
    }
    if (R.eq(R.vb(22138), 0)) {
      return 0;
    }
    if (R.eq(R.vb(938), 0)) {
      return 0;
    }
    if (R.eq(R.vb(934), 0)) {
      return 0;
    }
    if (R.eq(R.vb(773), 0)) {
      return 0;
    }
    if (R.eq(R.vb(937), 0)) {
      return 0;
    }
    if (R.eq(R.vb(935), 0)) {
      return 0;
    }
    if (R.eq(R.vb(791), 0)) {
      return 0;
    }
    if (R.eq(R.vb(933), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20245), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20244), 0)) {
      return 0;
    }
    if (R.eq(R.vb(912), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20247), 0)) {
      return 0;
    }
    if (R.eq(R.vb(910), 0)) {
      return 0;
    }
    if (R.eq(R.vb(907), 0)) {
      return 0;
    }
    if (R.eq(R.vb(814), 0)) {
      return 0;
    }
    if (R.eq(R.vb(735), 0)) {
      return 0;
    }
    if (R.eq(R.vb(734), 0)) {
      return 0;
    }
    if (R.eq(R.vb(737), 0)) {
      return 0;
    }
    if (R.eq(R.vb(732), 0)) {
      return 0;
    }
    if (R.eq(R.vb(909), 0)) {
      return 0;
    }
    if (R.eq(R.vb(903), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20249), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20251), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20252), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20253), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20261), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20264), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20263), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20258), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20256), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20260), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20265), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20269), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20270), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20266), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20268), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20267), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20271), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20262), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20259), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20273), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20274), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20277), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20275), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20279), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20278), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20280), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20281), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20282), 0)) {
      return 0;
    }
    if (R.eq(R.vb(20283), 0)) {
      return 0;
    }
    if (R.eq(R.vb(22828), 0)) {
      return 0;
    }
    if (R.eq(R.call(10881, [30146]), 0)) {
      return 0;
    }
    if (R.eq(R.vb(17532), 0)) {
      return 0;
    }
    if (R.eq(R.vb(707), 0)) {
      return 0;
    }
    if (R.eq(R.vb(17533), 0)) {
      return 0;
    }
    if (R.eq(R.vb(751), 0)) {
      return 0;
    }
    if (R.eq(R.vb(758), 0)) {
      return 0;
    }
    return 1;
  };
  V[4356] = { vb: [], vp: [], vc: [], calls: [] };
  S[4356] = function () {
    return R.mapMembers();
  };
  V[4583] = { vb: [], vp: [], vc: [], calls: [] };
  S[4583] = function (int0, string0, string1) {
    if (R.eq(int0, 1)) {
      return string0;
    }
    return string1;
  };
  V[4705] = { vb: [], vp: [], vc: [], calls: [] };
  S[4705] = function (int0) {
    var int1 = 0;
    var int2 = 0;
    var int3 = 0;
    int3 = R.idiv(Math.imul(int0, 6), 10);
    int2 = R.idiv(int3, 60);
    int3 = R.mod(int3, 60);
    if ((int2 > 59)) {
      int1 = R.idiv(int2, 60);
      int2 = R.mod(int2, 60);
    }
    return [int1, int2, int3];
  };
  V[4743] = { vb: [], vp: [], vc: [], calls: [] };
  S[4743] = function (int0) {
    var int1 = 0;
    var int2 = 0;
    var int3 = 0;
    var int4 = 0;
    int1 = R.mod(int0, 256);
    var int0 = R.idiv(int0, 256);
    int2 = R.mod(int0, 256);
    int0 = R.idiv(int0, 256);
    int3 = R.mod(int0, 256);
    int0 = R.idiv(int0, 256);
    int4 = int0;
    return [int1, int2, int3, int4];
  };
  V[4744] = { vb: [], vp: [], vc: [], calls: [950, 4924, 5208, 5236, 5240, 5254, 5381, 5396, 5412, 5413, 5414, 5509, 5515, 5516, 5517, 5518, 10240, 13399] };
  S[4744] = function (int0, int1) {
    var string0 = "";
    var string1 = "";
    string1 = R.call(5254, [int0]);
    if ((R.len(string1) > 0)) {
      if ((R.len(string0) > 0)) {
        string0 = R.cat(string0, `<br>${R.s(string1)}`);
      } else {
        string0 = R.cat(string0, string1);
      }
    }
    string1 = R.call(5236, [int0]);
    if ((R.len(string1) > 0)) {
      if ((R.len(string0) > 0)) {
        string0 = R.cat(string0, `<br>${R.s(string1)}`);
      } else {
        string0 = R.cat(string0, string1);
      }
    } else {
      string1 = R.call(5208, [int0]);
      if ((R.len(string1) > 0)) {
        if ((R.len(string0) > 0)) {
          string0 = R.cat(string0, `<br>${R.s(string1)}`);
        } else {
          string0 = R.cat(string0, string1);
        }
      }
    }
    string1 = R.call(5240, [int0, int1]);
    if ((R.len(string1) > 0)) {
      if ((R.len(string0) > 0)) {
        string0 = R.cat(string0, `<br>${R.s(string1)}`);
      } else {
        string0 = R.cat(string0, string1);
      }
    }
    string1 = R.call(5381, [int0, int1]);
    if ((R.len(string1) > 0)) {
      if ((R.len(string0) > 0)) {
        string0 = R.cat(string0, `<br>${R.s(string1)}`);
      } else {
        string0 = R.cat(string0, string1);
      }
    }
    string1 = R.call(13399, [int0, int1]);
    if ((R.len(string1) > 0)) {
      if ((R.len(string0) > 0)) {
        string0 = R.cat(string0, `<br>${R.s(string1)}`);
      } else {
        string0 = R.cat(string0, string1);
      }
    }
    string1 = R.call(5396, [int0]);
    if ((R.len(string1) > 0)) {
      if ((R.len(string0) > 0)) {
        string0 = R.cat(string0, `<br>${R.s(string1)}`);
      } else {
        string0 = R.cat(string0, string1);
      }
    }
    string1 = R.call(5412, [int0]);
    if ((R.len(string1) > 0)) {
      if ((R.len(string0) > 0)) {
        string0 = R.cat(string0, `<br>${R.s(string1)}`);
      } else {
        string0 = R.cat(string0, string1);
      }
    }
    string1 = R.call(5413, [int0]);
    if ((R.len(string1) > 0)) {
      if ((R.len(string0) > 0)) {
        string0 = R.cat(string0, `<br>${R.s(string1)}`);
      } else {
        string0 = R.cat(string0, string1);
      }
    }
    string1 = R.call(5414, [int0]);
    if ((R.len(string1) > 0)) {
      if ((R.len(string0) > 0)) {
        string0 = R.cat(string0, `<br>${R.s(string1)}`);
      } else {
        string0 = R.cat(string0, string1);
      }
    }
    string1 = R.call(5509, [int0]);
    if ((R.len(string1) > 0)) {
      if ((R.len(string0) > 0)) {
        string0 = R.cat(string0, `<br>${R.s(string1)}`);
      } else {
        string0 = R.cat(string0, string1);
      }
    }
    string1 = R.call(5516, [int0]);
    if ((R.len(string1) > 0)) {
      if ((R.len(string0) > 0)) {
        string0 = R.cat(string0, `<br>${R.s(string1)}`);
      } else {
        string0 = R.cat(string0, string1);
      }
    }
    string1 = R.call(5517, [int0]);
    if ((R.len(string1) > 0)) {
      if ((R.len(string0) > 0)) {
        string0 = R.cat(string0, `<br>${R.s(string1)}`);
      } else {
        string0 = R.cat(string0, string1);
      }
    }
    string1 = R.call(5515, [int0]);
    if ((R.len(string1) > 0)) {
      if ((R.len(string0) > 0)) {
        string0 = R.cat(string0, `<br>${R.s(string1)}`);
      } else {
        string0 = R.cat(string0, string1);
      }
    }
    string1 = R.call(4924, [int0, int1]);
    if ((R.len(string1) > 0)) {
      if ((R.len(string0) > 0)) {
        string0 = R.cat(string0, `<br>${R.s(string1)}`);
      } else {
        string0 = R.cat(string0, string1);
      }
    }
    string1 = R.call(950, [int0]);
    if ((R.len(string1) > 0)) {
      if ((R.len(string0) > 0)) {
        string0 = R.cat(string0, `<br>${R.s(string1)}`);
      } else {
        string0 = R.cat(string0, string1);
      }
    }
    string1 = R.structParam(int0, 7686);
    if ((R.len(string1) > 0)) {
      if ((R.len(string0) > 0)) {
        string0 = R.cat(string0, `<br>${R.s(string1)}`);
      } else {
        string0 = R.cat(string0, string1);
      }
    }
    string1 = R.call(10240, [int0]);
    if ((R.len(string1) > 0)) {
      if ((R.len(string0) > 0)) {
        string0 = R.cat(string0, `<br>${R.s(string1)}`);
      } else {
        string0 = R.cat(string0, string1);
      }
    }
    string1 = R.call(5518, [int0]);
    if ((R.len(string1) > 0)) {
      if ((R.len(string0) > 0)) {
        string0 = R.cat(string0, `<br>${R.s(string1)}`);
      } else {
        string0 = R.cat(string0, string1);
      }
    }
    return string0;
  };
  V[4924] = { vb: [], vp: [], vc: [], calls: [951, 5003, 17272] };
  S[4924] = function (int0, int1) {
    var string0 = "";
    var string1 = "";
    var string2 = "";
    var int2 = 0;
    var int3 = 0;
    var int4 = 0;
    var int5 = 0;
    var string3 = "";
    var int6 = (-1 | 0);
    var int7 = R.enumCount(681);
    while (((int2 = (int2 + (1)) | 0) <= int7)) {
      int6 = R.enumValue(0, 17, 681, int2);
      if (R.eq(R.call(951, [int0, int6]), 0)) {
        if (R.eq(int1, 1)) {
          int5 = R.call(5003, [int0, int6]);
          if ((int5 > 0)) {
            string2 = R.enumValue(17, 36, 680, int6);
            if ((int3 > 0)) {
              string0 = R.cat(string0, "<br>");
            }
            string0 = R.cat(string0, `<col=00ff00>${R.s(string2)} +${R.s(R.str(int5, 10))}`);
            int3 = ((int3) + (1) | 0);
          } else {
            if ((int5 < 0)) {
              string2 = R.enumValue(0, 36, 108, int2);
              if ((int4 > 0)) {
                string1 = R.cat(string1, "<br>");
              }
              string1 = R.cat(string1, `<col=ff0000>${R.s(string2)} ${R.s(R.str(int5, 10))}`);
              int4 = ((int4) + (1) | 0);
            }
          }
        } else {
          [int5, string3] = R.call(17272, [int0, int6]);
          if ((int5 > 0)) {
            string2 = R.enumValue(17, 36, 680, int6);
            if ((int3 > 0)) {
              string0 = R.cat(string0, "<br>");
            }
            string0 = R.cat(string0, `<col=00ff00>${R.s(string2)} by ${R.s(string3)}`);
            int3 = ((int3) + (1) | 0);
          } else {
            if ((int5 < 0)) {
              string2 = R.enumValue(0, 36, 108, int2);
              if ((int4 > 0)) {
                string1 = R.cat(string1, "<br>");
              }
              string1 = R.cat(string1, `<col=ff0000>${R.s(string2)} by ${R.s(string3)}`);
              int4 = ((int4) + (1) | 0);
            }
          }
        }
      }
      int5 = 0;
      string3 = "";
      int6 = (-1 | 0);
    }
    if ((int3 > 1)) {
      string0 = R.cat("Temporarily increases:<br>", string0);
    } else {
      if ((int3 > 0)) {
        string0 = R.cat("Temporarily increases ", string0);
      }
    }
    if ((int4 > 1)) {
      string1 = R.cat("Temporarily reduces:<br>", string1);
    } else {
      if ((int4 > 0)) {
        string1 = R.cat("Temporarily reduces ", string1);
      }
    }
    var string4 = "";
    if ((R.len(string0) > 0)) {
      string4 = string0;
    }
    if ((R.len(string1) > 0)) {
      if ((R.len(string0) > 0)) {
        string4 = R.cat(string4, "<br>");
      }
      string4 = R.cat(string4, string1);
    }
    return string4;
  };
  V[5003] = { vb: [], vp: [], vc: [], calls: [17268, 17269] };
  S[5003] = function (int0, int1) {
    if (R.eq(int0, (-1 | 0))) {
      return 0;
    }
    return ((R.call(17268, [int1, int0])) + (R.scale(R.statBase(int1), 100, R.call(17269, [int1, int0]))) | 0);
  };
  V[5208] = { vb: [], vp: [], vc: [], calls: [] };
  S[5208] = function (int0) {
    var int1 = R.structParam(int0, 7660);
    var int2 = R.structParam(int0, 7661);
    var string0 = "";
    if ((int1 > 0)) {
      string0 = `Restores negative stat changes in <col=00ffff>Attack, Constitution, Defense, Magic, Necromancy, Prayer, Ranged</col> and <col=00ffff>Strength</col> by <col=00ffff>${R.s(R.str(int1, 10))}</col>`;
    }
    if ((int2 > 0)) {
      if ((int1 > 0)) {
        string0 = R.cat(string0, ` + <col=00ffff>${R.s(R.str(int2, 10))}%</col> of total level`);
      } else {
        string0 = `Restores negative stat changes in <col=00ffff>Attack, Constitution, Defense, Magic, Necromancy, Prayer, Ranged</col> and <col=00ffff>Strength</col> by <col=00ffff>${R.s(R.str(int2, 10))}%</col> of total level`;
      }
    }
    if (((int1 > 0) || (int2 > 0))) {
      string0 = R.cat(string0, ".");
    }
    return string0;
  };
  V[5236] = { vb: [], vp: [], vc: [], calls: [] };
  S[5236] = function (int0) {
    var int1 = R.structParam(int0, 7662);
    var int2 = R.structParam(int0, 7663);
    var string0 = "";
    if ((int1 > 0)) {
      string0 = `Restores negative stat changes in <col=00ffff>all stats</col> by <col=00ffff>${R.s(R.str(int1, 10))}</col>`;
    }
    if ((int2 > 0)) {
      if ((int1 > 0)) {
        string0 = R.cat(string0, ` + <col=00ffff>${R.s(R.str(int2, 10))}%</col> of total level`);
      } else {
        string0 = `<col=EBE0BC>Restores negative stat changes in <col=00ffff>all stats</col> by <col=00ffff>${R.s(R.str(int2, 10))}%</col> of total level`;
      }
    }
    if (((int1 > 0) || (int2 > 0))) {
      string0 = R.cat(string0, ".");
    }
    return string0;
  };
  V[5240] = { vb: [], vp: [], vc: [], calls: [5241, 5242, 17274, 17275] };
  S[5240] = function (int0, int1) {
    var string0 = "";
    var int2 = R.call(5241, [int0]);
    var int3 = R.call(5242, [int0]);
    var string1 = "";
    var string2 = "";
    if (R.eq(int1, 1)) {
      string1 = R.str(R.abs(int2), 10);
      string2 = R.str(R.abs(int3), 10);
    } else {
      string1 = R.call(17274, [int0]);
      string2 = R.call(17275, [int0]);
    }
    if ((int2 > 0)) {
      string0 = `Restores <col=00ff00>${R.s(string1)}</col> prayer points`;
    } else {
      if ((int2 < 0)) {
        string0 = `Removes <col=ff0000>${R.s(string1)}</col> prayer points`;
      }
    }
    if ((int3 > 0)) {
      if ((R.len(string0) > 0)) {
        string0 = R.cat(string0, `, and renews <col=00ff00>${R.s(string2)} prayer points</col> over the duration`);
      } else {
        string0 = R.cat(string0, `Renews <col=00ff00>${R.s(string2)} prayer points</col> over the duration`);
      }
    } else {
      if ((int3 < 0)) {
        if ((R.len(string0) > 0)) {
          string0 = R.cat(string0, `, and drains <col=ff0000>${R.s(string2)} prayer points</col> over the duration`);
        } else {
          string0 = R.cat(string0, `Drains <col=ff0000>${R.s(string2)} prayer points</col> over the duration`);
        }
      }
    }
    if ((R.len(string0) > 0)) {
      string0 = R.cat(string0, ".");
    }
    return string0;
  };
  V[5241] = { vb: [], vp: [], vc: [], calls: [7081] };
  S[5241] = function (int0) {
    var int1 = R.structParam(int0, 7670);
    var int2 = R.structParam(int0, 7671);
    if (((int2 > 0) && ((R.invTotal(93, 6714) > 0) || (R.invTotal(94, 6714) > 0)))) {
      int2 = ((int2) + (2) | 0);
    }
    return ((int1) + (R.scale(R.call(7081, []), 100, int2)) | 0);
  };
  V[5242] = { vb: [], vp: [], vc: [], calls: [7081] };
  S[5242] = function (int0) {
    var int1 = R.structParam(int0, 7672);
    var int2 = R.structParam(int0, 7673);
    if (((int2 > 0) && ((R.invTotal(93, 6714) > 0) || (R.invTotal(94, 6714) > 0)))) {
      int2 = ((int2) + (2) | 0);
    }
    return ((int1) + (R.scale(R.call(7081, []), 100, int2)) | 0);
  };
  V[5254] = { vb: [], vp: [], vc: [], calls: [5275, 5335] };
  S[5254] = function (int0) {
    var string0 = "";
    var int1 = R.call(5275, [int0]);
    var int2 = R.call(5335, [int0]);
    if ((int1 > 0)) {
      string0 = `Restores <col=00ff00>${R.s(R.str(int1, 10))} health</col>`;
    } else {
      if ((int1 < 0)) {
        string0 = `Deals <col=ff0000>${R.s(R.str(R.abs(int1), 10))} damage</col>`;
      }
    }
    if ((int2 > 0)) {
      if ((R.len(string0) > 0)) {
        string0 = R.cat(string0, `, and renews <col=00ff00>${R.s(R.str(int2, 10))} health</col> over the duration`);
      } else {
        string0 = R.cat(string0, `Renews <col=00ff00>${R.s(R.str(int2, 10))} health</col> over the duration`);
      }
    } else {
      if ((int2 < 0)) {
        if ((R.len(string0) > 0)) {
          string0 = R.cat(string0, `, and deals <col=ff0000>${R.s(R.str(R.abs(int2), 10))} damage</col> over the duration`);
        } else {
          string0 = R.cat(string0, `Deals <col=ff0000>${R.s(R.str(R.abs(int2), 10))} damage</col> over the duration`);
        }
      }
    }
    if ((R.len(string0) > 0)) {
      string0 = R.cat(string0, ".");
    }
    return string0;
  };
  V[5275] = { vb: [27168], vp: [13537], vc: [], calls: [2916] };
  S[5275] = function (int0) {
    var int1 = R.structParam(int0, 7665);
    var int2 = R.structParam(int0, 7666);
    var int3 = R.structParam(int0, 7667);
    var int4 = ((((int1) + (R.scale(R.call(2916, []), 100, int2)) | 0)) + (R.scale(R.vp(13537), 100, int3)) | 0);
    if (R.eq(R.vb(27168), 1)) {
      int4 = R.idiv(int4, 10);
    }
    return int4;
  };
  V[5335] = { vb: [27168], vp: [], vc: [], calls: [2916] };
  S[5335] = function (int0) {
    var int1 = R.structParam(int0, 7668);
    var int2 = R.structParam(int0, 7669);
    var int3 = ((int1) + (R.scale(R.call(2916, []), 100, int2)) | 0);
    if (R.eq(R.vb(27168), 1)) {
      int3 = R.idiv(int3, 10);
    }
    return int3;
  };
  V[5359] = { vb: [], vp: [], vc: [], calls: [] };
  S[5359] = function (string0) {
    var string0 = `${R.s(string0)}- Increases your base ability damage by <col=ffffff>12%</col> when fighting against <col=ffffff>Creatures of Daemonheim</col>, when holding ruinous equipment in both hands.`;
    return string0;
  };
  V[5381] = { vb: [], vp: [], vc: [], calls: [5382, 5383, 17276, 17277] };
  S[5381] = function (int0, int1) {
    var string0 = "";
    var int2 = R.call(5382, [int0]);
    var int3 = R.call(5383, [int0]);
    var string1 = "";
    var string2 = "";
    if (R.eq(int1, 1)) {
      string1 = R.str(R.abs(int2), 10);
      string2 = R.str(R.abs(int3), 10);
    } else {
      string1 = R.call(17276, [int0]);
      string2 = R.call(17277, [int0]);
    }
    if ((int2 > 0)) {
      string0 = `Restores <col=00ff00>${R.s(string1)}</col> summoning points`;
    } else {
      if ((int2 < 0)) {
        string0 = `Removes <col=ff0000>${R.s(string1)}</col> summoning points`;
      }
    }
    if ((int3 > 0)) {
      if ((R.len(string0) > 0)) {
        string0 = R.cat(string0, `, and renews <col=00ff00>${R.s(string2)}</col> summoning points over the duration`);
      } else {
        string0 = R.cat(string0, `Renews <col=00ff00>${R.s(string2)}</col> summoning points over the duration`);
      }
    } else {
      if ((int3 < 0)) {
        if ((R.len(string0) > 0)) {
          string0 = R.cat(string0, `, and drains <col=ff0000>${R.s(string2)}</col> summoning points over the duration`);
        } else {
          string0 = R.cat(string0, `Drains <col=ff0000>${R.s(string2)}</col> summoning points over the duration`);
        }
      }
    }
    if ((R.len(string0) > 0)) {
      string0 = R.cat(string0, ".");
    }
    return string0;
  };
  V[5382] = { vb: [], vp: [], vc: [], calls: [859] };
  S[5382] = function (int0) {
    var int1 = R.structParam(int0, 7674);
    var int2 = R.structParam(int0, 7675);
    return ((Math.imul(int1, 10)) + (R.scale(R.idiv(R.call(859, []), 10), 100, int2)) | 0);
  };
  V[5383] = { vb: [], vp: [], vc: [], calls: [859] };
  S[5383] = function (int0) {
    var int1 = R.structParam(int0, 7676);
    var int2 = R.structParam(int0, 7677);
    return ((int1) + (R.scale(R.idiv(R.call(859, []), 10), 100, int2)) | 0);
  };
  V[5396] = { vb: [], vp: [], vc: [], calls: [] };
  S[5396] = function (int0) {
    var int1 = R.structParam(int0, 7678);
    if ((int1 > 0)) {
      return `Restores <col=00ff00>${R.s(R.str(int1, 10))}% run energy</col>.`;
    }
    if ((int1 < 0)) {
      return `Removes <col=ff0000>${R.s(R.str(R.abs(int1), 10))}% run energy</col>.`;
    }
    return "";
  };
  V[5412] = { vb: [27168], vp: [], vc: [], calls: [] };
  S[5412] = function (int0) {
    var int1 = R.structParam(int0, 7680);
    var string0 = "adrenaline";
    if (R.eq(R.vb(27168), 1)) {
      string0 = "special attack points";
    }
    if ((int1 > 0)) {
      return `</col>Provides <col=00ff00>${R.s(R.str(int1, 10))}% ${R.s(string0)}</col>.`;
    }
    if ((int1 < 0)) {
      return `</col>Removes <col=ff0000>${R.s(R.str(R.abs(int1), 10))}% ${R.s(string0)}</col>.`;
    }
    return "";
  };
  V[5413] = { vb: [], vp: [], vc: [], calls: [] };
  S[5413] = function (int0) {
    if (R.eq(R.structParam(int0, 7681), 1)) {
      return "Causes nearby enemies to <col=00ffff>attack the player</col>.";
    }
    return "";
  };
  V[5414] = { vb: [], vp: [], vc: [], calls: [] };
  S[5414] = function (int0) {
    if (R.eq(R.structParam(int0, 7682), 1)) {
      return "Immediately <col=00ffff>removes poison</col>, and grants <col=00ffff>immunity</col> to <col=00ffff>poison</col>.";
    }
    return "";
  };
  V[5509] = { vb: [], vp: [], vc: [], calls: [] };
  S[5509] = function (int0) {
    if ((R.structParam(int0, 7684) > 0)) {
      return "Coats your weapons with poison, giving a <col=00ffff>12.5% chance per hit</col> to deal extra damage to the enemy.";
    }
    return "";
  };
  V[5515] = { vb: [], vp: [], vc: [], calls: [] };
  S[5515] = function (int0) {
    if (R.eq(R.structParam(int0, 7685), 1)) {
      return "Stops you from appearing on other player's minimaps.";
    }
    return "";
  };
  V[5516] = { vb: [], vp: [], vc: [], calls: [] };
  S[5516] = function (int0) {
    switch (R.key(R.structParam(int0, 7679))) {
      case 1:
      {
        return "Grants <col=00ffff>slight protection</col> from <col=00ffff>Dragonfire</col>.";
      }
      case 2:
      {
        return "Grants <col=00ffff>immunity</col> from <col=00ffff>Dragonfire</col>.";
      }
      case 3:
      {
      }
      default:
      {
        return "";
      }
    }
    return "Grants <col=00ffff>immunity</col> from <col=00ffff>Dragonfire</col> and <col=00ffff>Wyvernfire</col>.";
  };
  V[5517] = { vb: [], vp: [], vc: [], calls: [] };
  S[5517] = function (int0) {
    var int1 = R.structParam(int0, 7683);
    if ((int1 <= 0)) {
      return "";
    }
    return `Grants tier ${R.s(R.str(int1, 10))} <col=00ffff>luck</col>, which improves your chances to receive rare rewards from various activities.`;
  };
  V[5518] = { vb: [], vp: [], vc: [], calls: [5641] };
  S[5518] = function (int0) {
    var string0 = "";
    var int1 = R.structParam(int0, 7658);
    var int2 = R.structParam(int0, 7664);
    var int3 = R.structParam(int0, 7659);
    if ((int1 > 0)) {
      string0 = R.cat(string0, `Lasts ${R.s(R.call(5641, [int0]))}`);
    }
    switch (R.key(int3)) {
      case 1:
      {
        if ((R.len(string0) > 0)) {
          string0 = R.cat(string0, ", stat changes are <col=00ffff>diminished</col> by 1 point per minute");
        } else {
          string0 = R.cat(string0, "Stat changes are <col=00ffff>diminished</col> by 1 point per minute");
        }
        break;
      }
      case 2:
      {
        if ((R.len(string0) > 0)) {
          string0 = R.cat(string0, ", stat changes are <col=00ffff>static</col> for the duration");
        } else {
          string0 = R.cat(string0, "Stat changes are <col=00ffff>static</col> for the duration");
        }
        break;
      }
    }
    if ((R.len(string0) > 0)) {
      string0 = R.cat(string0, ".");
    }
    return string0;
  };
  V[5520] = { vb: [20418, 20419, 20420, 20421, 20422, 20423, 20424, 20425, 20426, 20427, 20428, 20429, 20430, 20431, 20432, 20433, 20434, 20435, 20436, 20437, 20438, 20439, 20440, 20441, 20442, 20443, 20444, 20445, 20446, 20447, 20448, 20449, 20450, 20451, 20452, 20453, 20454, 20455, 20456, 20457, 20458, 20459, 20460, 20461, 20462, 20463, 20464, 20465, 20466, 20467, 20468, 20469, 20470, 20471, 20472, 20473, 20474, 20475, 20476, 20477], vp: [], vc: [], calls: [] };
  S[5520] = function () {
    var int0 = ((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((R.vb(20418)) + (R.vb(20419)) | 0)) + (R.vb(20420)) | 0)) + (R.vb(20421)) | 0)) + (R.vb(20422)) | 0)) + (R.vb(20423)) | 0)) + (R.vb(20424)) | 0)) + (R.vb(20425)) | 0)) + (R.vb(20426)) | 0)) + (R.vb(20427)) | 0)) + (R.vb(20428)) | 0)) + (R.vb(20429)) | 0)) + (R.vb(20430)) | 0)) + (R.vb(20431)) | 0)) + (R.vb(20432)) | 0)) + (R.vb(20433)) | 0)) + (R.vb(20434)) | 0)) + (R.vb(20435)) | 0)) + (R.vb(20436)) | 0)) + (R.vb(20437)) | 0)) + (R.vb(20438)) | 0)) + (R.vb(20439)) | 0)) + (R.vb(20440)) | 0)) + (R.vb(20441)) | 0)) + (R.vb(20442)) | 0)) + (R.vb(20443)) | 0)) + (R.vb(20444)) | 0)) + (R.vb(20445)) | 0)) + (R.vb(20446)) | 0)) + (R.vb(20447)) | 0)) + (R.vb(20448)) | 0)) + (R.vb(20449)) | 0)) + (R.vb(20450)) | 0)) + (R.vb(20451)) | 0)) + (R.vb(20452)) | 0)) + (R.vb(20453)) | 0)) + (R.vb(20454)) | 0)) + (R.vb(20455)) | 0)) + (R.vb(20456)) | 0)) + (R.vb(20457)) | 0)) + (R.vb(20458)) | 0)) + (R.vb(20459)) | 0)) + (R.vb(20460)) | 0)) + (R.vb(20461)) | 0)) + (R.vb(20462)) | 0)) + (R.vb(20463)) | 0)) + (R.vb(20464)) | 0)) + (R.vb(20465)) | 0)) + (R.vb(20466)) | 0)) + (R.vb(20467)) | 0)) + (R.vb(20468)) | 0)) + (R.vb(20469)) | 0)) + (R.vb(20470)) | 0)) + (R.vb(20471)) | 0)) + (R.vb(20472)) | 0)) + (R.vb(20473)) | 0)) + (R.vb(20474)) | 0)) + (R.vb(20475)) | 0)) + (R.vb(20476)) | 0)) + (R.vb(20477)) | 0);
    if ((int0 < 60)) {
      return 0;
    }
    return 1;
  };
  V[5521] = { vb: [], vp: [], vc: [], calls: [8251, 14945] };
  S[5521] = function (int0) {
    var string0 = "";
    var int1 = R.dbField(int0, 348240, 0);
    if (R.eq(R.itemMembers(int1), 1)) {
      string0 = R.cat(string0, `Familiar: <col=F8D56B>${R.s(R.itemName(int1))}</col>`);
    } else {
      string0 = R.cat(string0, `Familiar: <col=B8D1D1>${R.s(R.itemName(int1))}</col>`);
    }
    string0 = R.cat(string0, "<br>");
    string0 = R.cat(string0, "Familiar spell points");
    string0 = R.cat(string0, `: ${R.s(R.str(R.call(8251, [R.dbField(int0, 348256, 0)]), 10))}`);
    string0 = R.cat(string0, `<br>${R.s(R.dbField(int0, 348272, 0))}`);
    string0 = R.cat(string0, `<br>${R.s(R.call(14945, [R.dbField(int0, 348368, 0), 1]))} cooldown.</col>`);
    return string0;
  };
  V[5566] = { vb: [], vp: [], vc: [], calls: [] };
  S[5566] = function (int0, int1, int2) {
    if (R.eq(int2, 1)) {
      if ((int0 >= int1)) {
        return 1;
      }
      return 0;
    }
    if ((int0 > int1)) {
      return 1;
    }
    return 0;
  };
  V[5641] = { vb: [], vp: [], vc: [], calls: [] };
  S[5641] = function (int0) {
    var int1 = R.structParam(int0, 7658);
    if (R.eq(int1, 0)) {
      return "";
    }
    var int2 = 0;
    var int3 = 0;
    var string0 = "";
    if ((int1 >= 3600)) {
      int3 = R.idiv(int1, 3600);
      string0 = `<col=00ffff>${R.s(R.str(int3, 10))}`;
      int2 = R.scale(R.mod(int1, 3600), 3600, 100);
      if ((int2 > 0)) {
        string0 = R.cat(string0, `.${R.s(R.str(int2, 10))}`);
      }
      if (((int3 > 1) || (int2 > 0))) {
        string0 = R.cat(string0, " hours");
      } else {
        string0 = R.cat(string0, " hour");
      }
      return string0;
    }
    if ((int1 >= 60)) {
      int3 = R.idiv(int1, 60);
      string0 = `<col=00ffff>${R.s(R.str(int3, 10))}`;
      int2 = R.scale(R.mod(int1, 60), 60, 100);
      if ((int2 > 0)) {
        string0 = R.cat(string0, `.${R.s(R.str(int2, 10))}`);
      }
      if (((int3 > 1) || (int2 > 0))) {
        string0 = R.cat(string0, " minutes");
      } else {
        string0 = R.cat(string0, " minute");
      }
      return string0;
    }
    return `<col=00ffff>${R.s(R.str(int1, 10))} seconds`;
  };
  V[5729] = { vb: [], vp: [], vc: [], calls: [] };
  S[5729] = function (int0, int1, int2, int3, int4) {
    var string0 = "";
    if ((!R.eq(int0, (-1 | 0)))) {
      if ((int0 < 10)) {
        string0 = `${R.s(R.textSwitch(int3, "0", ""))}${R.s(R.str(int0, 10))}${R.s(R.textSwitch(int4, " : ", ":"))}`;
      } else {
        string0 = `${R.s(R.str(int0, 10))}${R.s(R.textSwitch(int4, " : ", ":"))}`;
      }
    }
    if ((int1 < 10)) {
      string0 = `${R.s(string0)}0${R.s(R.str(int1, 10))}${R.s(R.textSwitch(int4, " : ", ":"))}`;
    } else {
      string0 = `${R.s(string0)}${R.s(R.str(int1, 10))}${R.s(R.textSwitch(int4, " : ", ":"))}`;
    }
    if ((int2 < 10)) {
      string0 = `${R.s(string0)}0${R.s(R.str(int2, 10))}`;
    } else {
      string0 = `${R.s(string0)}${R.s(R.str(int2, 10))}`;
    }
    return string0;
  };
  V[5803] = { vb: [], vp: [], vc: [], calls: [5839, 19652] };
  S[5803] = function (int0) {
    switch (R.key(int0)) {
      case 94:
      case 742:
      {
        break;
      }
      default:
      {
        return 0;
      }
    }
    var int1 = R.call(19652, [int0]);
    if (R.eq(R.call(5839, []), 1)) {
      int1 = ((int1) + (200) | 0);
    }
    return int1;
  };
  V[5828] = { vb: [1899, 13266, 15705, 15960, 17234, 18238, 18242, 18246, 18250, 18254, 18611, 18615, 18619, 18623, 18627, 21930, 21934, 21938, 21942, 21946, 21950, 21951, 22901, 22902, 26736, 26740, 26744, 27168, 27394, 28861, 28865, 28869, 28873, 28874, 28875, 28876, 28877, 28878, 28879, 30604, 35985, 37578, 37579, 37583, 37584, 39465, 39466, 39467, 39468, 39469, 39470, 42023, 42160, 42534, 45288, 45291, 49361, 49710, 49720, 49721, 49722, 51808, 51871, 54795, 54893, 54894, 54895, 54896, 54897, 54898, 54899, 54900, 54901, 54902, 54903, 54904, 55172, 55320, 56949, 57039, 58116, 58117, 60144, 61611], vp: [183, 185, 3079, 3216, 3217, 3218, 3219, 7253, 11842, 13505], vc: [1533, 2239, 4645, 5121, 5122, 6492], calls: [519, 670, 734, 999, 1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007, 1008, 1569, 1764, 2109, 2258, 2475, 2547, 2579, 3354, 3381, 3862, 3956, 4000, 4034, 4148, 4229, 4583, 4705, 4744, 5521, 5729, 6345, 6554, 7235, 7236, 7237, 7239, 7241, 7244, 7245, 7246, 7653, 8002, 8240, 8755, 9716, 10005, 10081, 10184, 10290, 10495, 10761, 11942, 12070, 12071, 12074, 12080, 12197, 12422, 12477, 12676, 13258, 13424, 13776, 13825, 14608, 14793, 15086, 15097, 15935, 16823, 17012, 17172, 17461, 17467, 17493, 17662, 17663, 18556, 18986, 19653, 19680, 20005, 20006, 20152, 20487, 20988, 21028, 21105] };
  S[5828] = function (int0, int1, int2, int3, int4, int5, int6, string0) {
    var int7 = 16777215;
    var string1 = "";
    var string2 = "";
    var string3 = "";
    var string4 = "";
    var string5 = "";
    var string6 = "";
    if (R.eq(int1, 28977)) {
      int7 = R.call(10495, [3]);
      string3 = R.colTag(int7);
      string1 = R.colTag(0);
      string2 = R.colTag(R.call(10495, [0]));
      string4 = R.colTag(16777215);
      string5 = string3;
      string6 = string2;
    } else {
      int7 = 14931919;
      string3 = R.colTag(int7);
      string1 = string3;
      string2 = string3;
      string4 = string3;
      string5 = "<col=969696>";
      string6 = string3;
    }
    var string7 = string2;
    var int8 = 0;
    var int9 = 0;
    var int10 = 0;
    var int11 = 0;
    var int12 = 0;
    var int13 = 0;
    var int14 = 0;
    var int15 = 0;
    var int16 = 0;
    var int17 = 0;
    var int18 = 0;
    var int19 = 0;
    var int20 = 0;
    var int21 = 0;
    var int22 = 0;
    var int23 = 0;
    var int24 = 0;
    var int25 = 0;
    var int26 = 0;
    var int27 = 0;
    var int28 = 0;
    var int29 = 0;
    var int30 = 0;
    var int31 = 0;
    var int32 = 0;
    var int33 = 0;
    var int34 = 0;
    var int35 = 0;
    var int36 = 0;
    var int37 = (-1 | 0);
    var int38 = (-1 | 0);
    var int39 = 0;
    var string8 = "0";
    var int40 = (-1 | 0);
    var string9 = "";
    var string10 = "";
    var string11 = "";
    var string12 = "";
    var int41 = (-1 | 0);
    var int42 = 0;
    var int43 = 0;
    if (R.eq(R.itemParam(int0, 3793), 1)) {
      string11 = "When repaired: ";
    }
    int34 = R.itemParam(int0, 963);
    R.ifClear(int4);
    var int44 = 0;
    var int45 = (-1 | 0);
    if ((((!R.eq(int0, (-1 | 0))) && (!R.eq(int1, (-1 | 0)))) && (R.len(string0) > 0))) {
      int44 = R.call(7235, [int1, int3, int4, int44, string0, string7]);
      if ((R.eq(int3, 96797558) && R.eq(int4, 96797561))) {
        int44 = R.call(20487, [int3, int4, int44]);
        int45 = int44;
      }
    }
    if ((!R.eq(R.itemWearpos(int0), (-1 | 0)))) {
      varclient_2239 = R.invObj(94, R.itemWearpos(int0));
      int8 = R.call(7241, [int0]);
      int9 = R.call(17172, [int0, int8]);
      int18 = R.call(7245, [int0, int8]);
      if ((R.itemParam(int0, 2853) > 0)) {
        int10 = R.itemParam(int0, 2853);
      } else {
        if ((!R.eq(R.itemParam(int0, 686), (-1 | 0)))) {
          int10 = R.structParam(R.itemParam(int0, 686), 2853);
        }
      }
      int11 = R.call(13825, [int0, int8]);
      int12 = R.call(7244, [int0]);
      int14 = R.itemParam(int0, 2870);
      int15 = R.call(17467, [int0]);
      int17 = R.itemParam(int0, 2946);
      int16 = R.call(19653, [int0]);
      int35 = R.itemParam(int0, 13);
      if (R.eq(int11, 0)) {
        int19 = R.call(10005, [int0, 1]);
        int20 = R.call(10005, [int0, 2]);
        int21 = R.call(10005, [int0, 3]);
        int22 = R.call(10005, [int0, 7]);
      }
      int23 = R.call(7241, [R.vc(2239)]);
      if ((R.itemParam(R.vc(2239), 2853) > 0)) {
        int24 = R.itemParam(R.vc(2239), 2853);
      } else {
        if ((!R.eq(R.itemParam(R.vc(2239), 686), (-1 | 0)))) {
          int24 = R.structParam(R.itemParam(R.vc(2239), 686), 2853);
        }
      }
      int25 = R.call(13825, [R.vc(2239), int23]);
      int26 = R.itemParam(R.vc(2239), 2870);
      int27 = R.call(17467, [R.vc(2239)]);
      int29 = R.itemParam(R.vc(2239), 2946);
      int28 = R.call(19653, [R.vc(2239)]);
      int36 = R.itemParam(R.vc(2239), 13);
      if (R.eq(int25, 0)) {
        int30 = R.call(10005, [R.vc(2239), 1]);
        int31 = R.call(10005, [R.vc(2239), 2]);
        int32 = R.call(10005, [R.vc(2239), 3]);
        int33 = R.call(10005, [R.vc(2239), 7]);
      }
      if (((int11 > 0) || (((int25 > 0) && (!R.eq(R.itemWearpos(R.vc(2239)), 5))) && (!R.eq(R.itemWearpos(R.vc(2239)), 3))))) {
        if (R.eq(R.vc(2239), (-1 | 0))) {
          string7 = "<col=00ff00>";
        } else {
          if ((!R.eq(int8, int23))) {
            string7 = "<col=00ffff>";
          } else {
            if ((int11 > int25)) {
              string7 = "<col=00ff00>";
            } else {
              if ((int11 < int25)) {
                string7 = "<col=ff0000>";
              } else {
                string7 = string4;
              }
            }
          }
        }
        int44 = R.call(7235, [int2, int3, int4, int44, `${R.s(string3)}Damage:</col> ${R.s(string7)}${R.s(R.call(7653, [int11, 1, 0, 0, 1]))}</col>`, string7]);
      }
      if (((!R.eq(R.itemWearpos(int0), 5)) && (int12 > 0))) {
        if (R.eq(R.vc(2239), (-1 | 0))) {
          string7 = "<col=00ff00>";
        } else {
          if ((!R.eq(int8, int23))) {
            string7 = "<col=00ffff>";
          } else {
            if ((int12 > R.call(7244, [R.vc(2239)]))) {
              string7 = "<col=00ff00>";
            } else {
              if ((int12 < R.call(7244, [R.vc(2239)]))) {
                string7 = "<col=ff0000>";
              } else {
                string7 = string4;
              }
            }
          }
        }
        if ((!R.eq(R.enumValue(0, 0, 7338, int12), int18))) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Accuracy: ${R.s(string7)}${R.s(R.strLoc(int12, 1))} (Tier ${R.s(R.str(R.enumValue(0, 0, 7338, int12), 10))})</col> `, string3]);
        } else {
          int44 = R.call(7236, [int12, int7, int2, int3, int4, int44, "Accuracy", string7]);
        }
      }
      if ((int13 > 0)) {
        if (R.eq(R.vc(2239), (-1 | 0))) {
          string7 = "<col=00ff00>";
        } else {
          if ((!R.eq(int8, int23))) {
            string7 = "<col=00ffff>";
          } else {
            if ((int13 > 0)) {
              string7 = "<col=00ff00>";
            } else {
              if ((int13 < 0)) {
                string7 = "<col=ff0000>";
              } else {
                string7 = string4;
              }
            }
          }
        }
        int44 = R.call(7236, [int13, int7, int2, int3, int4, int44, "Skill Bonus", string7]);
      }
      if ((int10 > 0)) {
        if (R.eq(R.vc(2239), (-1 | 0))) {
          string7 = "<col=00ff00>";
        } else {
          if ((!R.eq(int8, int23))) {
            string7 = "<col=00ffff>";
          } else {
            if ((!R.eq(int10, int24))) {
              string7 = "<col=00ffff>";
            } else {
              string7 = string4;
            }
          }
        }
        if (R.eq(int8, 2)) {
          if ((!R.eq(R.itemParam(R.vc(2239), 21), 106))) {
            int44 = R.call(7237, [int10, 6744, int7, int2, int3, int4, int44, "Ammo", string7]);
          }
        } else {
          if (R.eq(int8, 1)) {
            int44 = R.call(7237, [int10, 6744, int7, int2, int3, int4, int44, "Style", string7]);
          }
        }
      }
      if ((int14 > 0)) {
        if (R.eq(R.vc(2239), (-1 | 0))) {
          string7 = "<col=00ff00>";
        } else {
          if ((!R.eq(int8, int23))) {
            string7 = "<col=00ffff>";
          } else {
            if ((int14 > int26)) {
              string7 = "<col=00ff00>";
            } else {
              if ((int14 < int26)) {
                string7 = "<col=ff0000>";
              } else {
                string7 = string4;
              }
            }
          }
        }
        int41 = R.enumValue(0, 26, 8289, R.itemWearpos(int0));
        if (((((int18 > 1) && (!R.eq(int41, (-1 | 0)))) && (!R.eq(R.enumValue(0, 0, int41, int18), int14))) && R.eq(R.enumHas(0, int41, int14), 1))) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Armour: ${R.s(string7)}${R.s(R.strLoc(R.max(1, R.idiv(int14, 10)), 1))} (Tier ${R.s(R.str(R.enumReverse(0, 0, int41, int14, 0), 10))})</col>`, string3]);
        } else {
          int44 = R.call(7236, [R.max(1, R.idiv(int14, 10)), int7, int2, int3, int4, int44, "Armour", string7]);
        }
      }
      if ((!R.eq(int15, 0))) {
        if (R.eq(R.vc(2239), (-1 | 0))) {
          string7 = "<col=00ff00>";
        } else {
          if ((!R.eq(int8, int23))) {
            string7 = "<col=00ffff>";
          } else {
            if ((int15 > int27)) {
              string7 = "<col=00ff00>";
            } else {
              if ((int15 < int27)) {
                string7 = "<col=ff0000>";
              } else {
                string7 = string4;
              }
            }
          }
        }
        int44 = R.call(7235, [int2, int3, int4, int44, `Damage Reduction: ${R.s(string7)}${R.s(R.call(7653, [int15, 1, 1, 1, 1]))}%</col>`, string3]);
      }
      if ((int19 > 0)) {
        if (R.eq(R.vc(2239), (-1 | 0))) {
          string7 = "<col=00ff00>";
        } else {
          if ((!R.eq(int8, int23))) {
            string7 = "<col=00ffff>";
          } else {
            if ((int19 > int30)) {
              string7 = "<col=00ff00>";
            } else {
              if ((int19 < int30)) {
                string7 = "<col=ff0000>";
              } else {
                string7 = string4;
              }
            }
          }
        }
        int44 = R.call(7235, [int2, int3, int4, int44, `Melee Damage Bonus: ${R.s(string7)}${R.s(R.call(7653, [int19, 1, 1, 0, 1]))}</col>`, string3]);
      }
      if ((int20 > 0)) {
        if (R.eq(R.vc(2239), (-1 | 0))) {
          string7 = "<col=00ff00>";
        } else {
          if ((!R.eq(int8, int23))) {
            string7 = "<col=00ffff>";
          } else {
            if ((int20 > int31)) {
              string7 = "<col=00ff00>";
            } else {
              if ((int20 < int31)) {
                string7 = "<col=ff0000>";
              } else {
                string7 = string4;
              }
            }
          }
        }
        int44 = R.call(7235, [int2, int3, int4, int44, `Ranged Damage Bonus: ${R.s(string7)}${R.s(R.call(7653, [int20, 1, 1, 0, 1]))}</col>`, string3]);
      }
      if ((int21 > 0)) {
        if (R.eq(R.vc(2239), (-1 | 0))) {
          string7 = "<col=00ff00>";
        } else {
          if ((!R.eq(int8, int23))) {
            string7 = "<col=00ffff>";
          } else {
            if ((int21 > int32)) {
              string7 = "<col=00ff00>";
            } else {
              if ((int21 < int32)) {
                string7 = "<col=ff0000>";
              } else {
                string7 = string4;
              }
            }
          }
        }
        int44 = R.call(7235, [int2, int3, int4, int44, `Magic Damage Bonus: ${R.s(string7)}${R.s(R.call(7653, [int21, 1, 1, 0, 1]))}</col>`, string3]);
      }
      if ((int22 > 0)) {
        if (R.eq(R.vc(2239), (-1 | 0))) {
          string7 = "<col=00ff00>";
        } else {
          if ((!R.eq(int8, int23))) {
            string7 = "<col=00ffff>";
          } else {
            if ((int22 > int33)) {
              string7 = "<col=00ff00>";
            } else {
              if ((int22 < int33)) {
                string7 = "<col=ff0000>";
              } else {
                string7 = string4;
              }
            }
          }
        }
        int44 = R.call(7235, [int2, int3, int4, int44, `Necromancy Damage Bonus: ${R.s(string7)}${R.s(R.call(7653, [int22, 1, 1, 0, 1]))}</col>`, string3]);
      }
      if (((!R.eq(R.itemWearpos(int0), 5)) && (!R.eq(R.itemWearpos(int0), 3)))) {
        int19 = R.call(2109, [int0, 1]);
        int20 = R.call(2109, [int0, 2]);
        int21 = R.call(2109, [int0, 3]);
        int22 = R.call(2109, [int0, 7]);
        int30 = R.call(2109, [R.vc(2239), 1]);
        int31 = R.call(2109, [R.vc(2239), 2]);
        int32 = R.call(2109, [R.vc(2239), 3]);
        int33 = R.call(2109, [R.vc(2239), 7]);
        if ((int19 > 0)) {
          if (R.eq(R.vc(2239), (-1 | 0))) {
            string7 = "<col=00ff00>";
          } else {
            if ((!R.eq(int8, int23))) {
              string7 = "<col=00ffff>";
            } else {
              if ((int19 > int30)) {
                string7 = "<col=00ff00>";
              } else {
                if ((int19 < int30)) {
                  string7 = "<col=ff0000>";
                } else {
                  string7 = string4;
                }
              }
            }
          }
          if (R.eq(R.vb(27168), 0)) {
            int44 = R.call(7235, [int2, int3, int4, int44, `Melee Accuracy Bonus: ${R.s(string7)}${R.s(R.str(int19, 10))}</col>`, string3]);
          } else {
            int44 = R.call(7235, [int2, int3, int4, int44, `Melee Accuracy Bonus: ${R.s(string7)}${R.s(R.str(R.idiv(int19, 10), 10))}${R.s(R.call(8002, []))}${R.s(R.str(R.mod(int19, 10), 10))}</col>`, string3]);
          }
        }
        if ((int20 > 0)) {
          if (R.eq(R.vc(2239), (-1 | 0))) {
            string7 = "<col=00ff00>";
          } else {
            if ((!R.eq(int8, int23))) {
              string7 = "<col=00ffff>";
            } else {
              if ((int20 > int31)) {
                string7 = "<col=00ff00>";
              } else {
                if ((int20 < int31)) {
                  string7 = "<col=ff0000>";
                } else {
                  string7 = string4;
                }
              }
            }
          }
          if (R.eq(R.vb(27168), 0)) {
            int44 = R.call(7235, [int2, int3, int4, int44, `Ranged Accuracy Bonus: ${R.s(string7)}${R.s(R.str(int20, 10))}</col>`, string3]);
          } else {
            int44 = R.call(7235, [int2, int3, int4, int44, `Ranged Accuracy Bonus: ${R.s(string7)}${R.s(R.str(R.idiv(int20, 10), 10))}${R.s(R.call(8002, []))}${R.s(R.str(R.mod(int20, 10), 10))}</col>`, string3]);
          }
        }
        if ((int21 > 0)) {
          if (R.eq(R.vc(2239), (-1 | 0))) {
            string7 = "<col=00ff00>";
          } else {
            if ((!R.eq(int8, int23))) {
              string7 = "<col=00ffff>";
            } else {
              if ((int21 > int32)) {
                string7 = "<col=00ff00>";
              } else {
                if ((int21 < int32)) {
                  string7 = "<col=ff0000>";
                } else {
                  string7 = string4;
                }
              }
            }
          }
          if (R.eq(R.vb(27168), 0)) {
            int44 = R.call(7235, [int2, int3, int4, int44, `Magic Accuracy Bonus: ${R.s(string7)}${R.s(R.str(int21, 10))}</col>`, string3]);
          } else {
            int44 = R.call(7235, [int2, int3, int4, int44, `Magic Accuracy Bonus: ${R.s(string7)}${R.s(R.str(R.idiv(int21, 10), 10))}${R.s(R.call(8002, []))}${R.s(R.str(R.mod(int21, 10), 10))}</col>`, string3]);
          }
        }
        if ((int22 > 0)) {
          if (R.eq(R.vc(2239), (-1 | 0))) {
            string7 = "<col=00ff00>";
          } else {
            if ((!R.eq(int8, int23))) {
              string7 = "<col=00ffff>";
            } else {
              if ((int22 > int33)) {
                string7 = "<col=00ff00>";
              } else {
                if ((int22 < int33)) {
                  string7 = "<col=ff0000>";
                } else {
                  string7 = string4;
                }
              }
            }
          }
          if (R.eq(R.vb(27168), 0)) {
            int44 = R.call(7235, [int2, int3, int4, int44, `Necromancy Accuracy Bonus: ${R.s(string7)}${R.s(R.str(int22, 10))}</col>`, string3]);
          } else {
            int44 = R.call(7235, [int2, int3, int4, int44, `Necromancy Accuracy Bonus: ${R.s(string7)}${R.s(R.str(R.idiv(int22, 10), 10))}${R.s(R.call(8002, []))}${R.s(R.str(R.mod(int22, 10), 10))}</col>`, string3]);
          }
        }
      }
      if ((int16 > 0)) {
        if (R.eq(R.vc(2239), (-1 | 0))) {
          string7 = "<col=00ff00>";
        } else {
          if ((int16 > int28)) {
            string7 = "<col=00ff00>";
          } else {
            if ((int16 < int28)) {
              string7 = "<col=ff0000>";
            } else {
              string7 = string4;
            }
          }
        }
        if (R.eq(R.vb(27168), 1)) {
          int16 = R.idiv(int16, 10);
        }
        int44 = R.call(7236, [int16, int7, int2, int3, int4, int44, "Life Points Bonus", string7]);
      }
      if ((!R.eq(int17, 0))) {
        if (R.eq(R.vc(2239), (-1 | 0))) {
          if ((int17 > 0)) {
            string7 = "<col=00ff00>";
          } else {
            string7 = "<col=ff0000>";
          }
        } else {
          if ((int17 > int29)) {
            string7 = "<col=00ff00>";
          } else {
            if ((int17 < int29)) {
              string7 = "<col=ff0000>";
            } else {
              string7 = string4;
            }
          }
        }
        int44 = R.call(7236, [int17, int7, int2, int3, int4, int44, "Prayer Bonus", string7]);
      }
      if ((int8 > 0)) {
        if (R.eq(R.vc(2239), (-1 | 0))) {
          string7 = "<col=00ff00>";
        } else {
          if ((!R.eq(int8, int23))) {
            string7 = "<col=00ffff>";
          } else {
            string7 = string4;
          }
        }
        if ((!R.eq(int9, 0))) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Type: ${R.s(string7)}${R.s(R.enumValue(0, 36, 6742, int8))} (${R.s(R.enumValue(0, 36, 16975, int9))})</col>`, string3]);
        } else {
          int44 = R.call(7237, [int8, 6742, int7, int2, int3, int4, int44, "Type", string7]);
        }
      }
      if (R.eq(R.call(7239, [int8]), 1)) {
        int35 = R.max(int35, 1);
        int36 = R.max(int36, 1);
        if ((int35 > int36)) {
          string7 = "<col=00ff00>";
        } else {
          if ((int35 < int36)) {
            string7 = "<col=ff0000>";
          } else {
            string7 = string4;
          }
        }
        int44 = R.call(7236, [int35, int7, int2, int3, int4, int44, "Range", string7]);
      }
      if ((int18 > 0)) {
        string7 = string4;
        if (R.eq(R.itemParam(int0, 8563), 1)) {
          int18 = R.itemParam(int0, 750);
        }
        switch (R.key(R.itemCategory(int0))) {
          case 35:
          {
            string12 = "Level";
            break;
          }
          default:
          {
            string12 = "Tier";
            break;
          }
        }
        int44 = R.call(7236, [int18, int7, int2, int3, int4, int44, string12, string7]);
      }
      if (((!R.eq(R.vc(5121), (-1 | 0))) && (!R.eq(R.vc(5122), (-1 | 0))))) {
        if ((R.eq(R.call(20988, []), 1) && R.eq(R.call(7239, [int8]), 1))) {
          int44 = R.call(7235, [int2, int3, int4, int44, `<sprite=20806> Genesis Essence: Tier ${R.s(R.str(120, 10))} damage and accuracy.`, string3]);
        } else {
          int42 = R.call(11942, [int0]);
          if ((int42 > 0)) {
            int44 = R.call(7235, [int2, int3, int4, int44, `<sprite=30343> Innate Mastery: +${R.s(R.str(Math.imul(5, int42), 10))} damage and accuracy tiers`, string3]);
          }
        }
      }
      if (R.eq(R.itemParam(int0, 2832), 1)) {
        int44 = R.call(7235, [int2, int3, int4, int44, "Shield", string3]);
      }
      if (R.eq(R.itemParam(int0, 5416), 1)) {
        int44 = R.call(7235, [int2, int3, int4, int44, "Acts as a shield", string3]);
      }
      if (R.eq(R.itemParam(int0, 8899), 1)) {
        int44 = R.call(7235, [int2, int3, int4, int44, "Underworld Conduit", string3]);
      } else {
        if (R.eq(R.itemParam(int0, 8571), 1)) {
          int44 = R.call(7235, [int2, int3, int4, int44, "Underworld Connection", string3]);
        }
      }
      if ((!R.eq(R.itemParam(int0, 8928), (-1 | 0)))) {
        int44 = R.call(7235, [int2, int3, int4, int44, `Passive: ${R.s(R.structParam(R.itemParam(int0, 8928), 2794))}<br>${R.s(R.call(17663, [int0]))}`, string3]);
      }
      if ((!R.eq(R.itemParam(int0, 4338), (-1 | 0)))) {
        if (R.eq(R.itemParam(R.itemParam(int0, 4338), 4329), 1)) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Special Attack: ${R.s(R.call(17461, [R.itemParam(int0, 4338)]))}`, string3]);
        }
      } else {
        if (R.eq(R.itemParam(int0, 4329), 1)) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Special Attack: ${R.s(R.call(17461, [int0]))}`, string3]);
        } else {
          if (((R.eq(R.itemCategory(int0), 4700) && (!R.eq(R.vc(5121), (-1 | 0)))) && (!R.eq(R.vc(5122), (-1 | 0))))) {
            int44 = R.call(7235, [int2, int3, int4, int44, `Special Attack: ${R.s(R.call(15097, [R.vc(5121), R.vc(5122)]))}`, string3]);
          }
        }
      }
      if ((((!R.eq(R.vc(5121), (-1 | 0))) && (R.vc(5122) >= 0)) && R.eq(R.call(12070, [int0]), 1))) {
        int16 = R.invVar(R.vc(5121), R.vc(5122), 30212);
        int17 = R.call(12071, [int16]);
        int44 = R.call(7236, [int17, int7, int2, int3, int4, int44, "Item Level", ""]);
        if ((int17 < 20)) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Item XP: ${R.s(R.strLoc(int16, 1))}/${R.s(R.strLoc(R.call(12074, [((int17) + (1) | 0)]), 1))}`, string3]);
        } else {
          int44 = R.call(7235, [int2, int3, int4, int44, `Item XP: ${R.s(R.strLoc(int16, 1))}`, string3]);
        }
        if (R.eq(R.call(12080, [R.itemParam(int0, 5524)]), 1)) {
          if ((R.eq(R.vb(49710), 0) || R.eq(R.vb(49710), 1))) {
            int44 = R.call(12197, [int0, R.vc(5121), R.vc(5122), int2, int3, int4, int44]);
          }
          if ((R.eq(R.vb(49710), 0) || R.eq(R.vb(49710), 2))) {
            int44 = R.call(15935, [int0, R.vc(5121), R.vc(5122), int2, int3, int4, int44, string3]);
          }
        }
      }
      if (R.eq(R.itemParam(int0, 5387), 1)) {
        int44 = R.call(7235, [int2, int3, int4, int44, "Has a chance to double a drop.", string3]);
      }
      int38 = R.itemParam(int0, 6186);
      if ((!R.eq(int38, (-1 | 0)))) {
        if ((R.eq(R.itemParam(int0, 3793), 1) && (!R.eq(R.itemParam(int0, 3203), (-1 | 0))))) {
          int44 = R.call(7235, [int2, int3, int4, int44, "When repaired:", string4]);
          string11 = "";
        }
        string9 = R.call(8240, [int38, (-1 | 0)]);
        string10 = R.call(17662, [int38, (-1 | 0)]);
        if ((R.len(string10) > 0)) {
          string10 = R.cat("<br>", string10);
        }
        if ((!R.eq(R.strcmp(R.structParam(int38, 2794), ""), 0))) {
          if ((R.eq(R.vb(27168), 1) && (!R.eq(R.strcmp(R.structParam(int38, 7998), ""), 0)))) {
            int44 = R.call(7235, [int2, int3, int4, int44, `${R.s(string11)}${R.s(R.structParam(int38, 2794))}: ${R.s(string9)}${R.s(R.structParam(int38, 7998))}${R.s(string10)}`, string3]);
          } else {
            int44 = R.call(7235, [int2, int3, int4, int44, `${R.s(string11)}${R.s(R.structParam(int38, 2794))}: ${R.s(string9)}${R.s(R.structParam(int38, 2795))}${R.s(string10)}`, string3]);
          }
        } else {
          if ((R.eq(R.vb(27168), 1) && (!R.eq(R.strcmp(R.structParam(int38, 7998), ""), 0)))) {
            int44 = R.call(7235, [int2, int3, int4, int44, `${R.s(string11)}${R.s(string9)}${R.s(R.structParam(int38, 7998))}${R.s(string10)}`, string3]);
          } else {
            int44 = R.call(7235, [int2, int3, int4, int44, `${R.s(string11)}${R.s(string9)}${R.s(R.structParam(int38, 2795))}${R.s(string10)}`, string3]);
          }
        }
      }
      int38 = R.itemParam(int0, 3203);
      if ((!R.eq(int38, (-1 | 0)))) {
        if ((int44 > int45)) {
          int44 = R.call(13258, [1, R.structParam(int2, 4205), int3, int4, int44]);
        }
        int44 = R.call(7235, [int2, int3, int4, int44, `Item Set: ${R.s(R.structParam(int38, 2794))}`, string3]);
        int44 = R.call(6554, [int44, int2, int3, int4, int38, int18, int0, string8, string4, string5]);
      }
      if (R.eq(R.itemParam(int0, 6845), 1)) {
        if (R.eq(R.call(12676, [int0]), 1)) {
          int44 = R.call(7235, [int2, int3, int4, int44, "Enchantment Effect:", string3]);
          int44 = R.call(7235, [int2, int3, int4, int44, R.structParam(R.itemParam(int0, 6846), 2795), string3]);
        } else {
          int44 = R.call(7235, [int2, int3, int4, int44, "Enchantment Effect:", string5]);
          int44 = R.call(7235, [int2, int3, int4, int44, `Requires: ${R.s(R.itemName(R.itemParam(int0, 6848)))}.`, string5]);
          int44 = R.call(7235, [int2, int3, int4, int44, R.structParam(R.itemParam(int0, 6847), 2795), string5]);
        }
      }
    } else {
      int38 = R.itemParam(int0, 6186);
      if ((!R.eq(int38, (-1 | 0)))) {
        string9 = R.call(8240, [int38, (-1 | 0)]);
        string10 = R.call(17662, [int38, (-1 | 0)]);
        if ((R.len(string10) > 0)) {
          string10 = R.cat("<br>", string10);
        }
        if ((!R.eq(R.strcmp(R.structParam(int38, 2794), ""), 0))) {
          if ((R.eq(R.vb(27168), 1) && (!R.eq(R.strcmp(R.structParam(int38, 7998), ""), 0)))) {
            int44 = R.call(7235, [int2, int3, int4, int44, `${R.s(string11)}${R.s(R.structParam(int38, 2794))}: ${R.s(string9)}${R.s(R.structParam(int38, 7998))}${R.s(string10)}`, string3]);
          } else {
            int44 = R.call(7235, [int2, int3, int4, int44, `${R.s(string11)}${R.s(R.structParam(int38, 2794))}: ${R.s(string9)}${R.s(R.structParam(int38, 2795))}${R.s(string10)}`, string3]);
          }
        } else {
          if ((R.eq(R.vb(27168), 1) && (!R.eq(R.strcmp(R.structParam(int38, 7998), ""), 0)))) {
            int44 = R.call(7235, [int2, int3, int4, int44, `${R.s(string11)}${R.s(string9)}${R.s(R.structParam(int38, 7998))}${R.s(string10)}`, string3]);
          } else {
            int44 = R.call(7235, [int2, int3, int4, int44, `${R.s(string11)}${R.s(string9)}${R.s(R.structParam(int38, 2795))}${R.s(string10)}`, string3]);
          }
        }
      }
    }
    if (((int34 > 0) && (R.eq(R.itemParam(int0, 2281), (-1 | 0)) || (R.itemParam(int0, 2970) > 1)))) {
      string7 = "<col=00ff00>";
      int34 = R.call(7246, [int0]);
      if ((R.eq(int0, 20429) && R.eq(R.vb(1899), 0))) {
        int34 = R.scale(int34, 2475, 2800);
      }
      if ((R.eq(R.vb(27168), 1) && (!R.eq(R.vc(1533), 1)))) {
        int34 = R.idiv(int34, 10);
      }
      if ((R.itemParam(int0, 2972) > 0)) {
        int44 = R.call(7235, [int2, int3, int4, int44, "Heals: <col=00ff00>???</col>", string3]);
      } else {
        int44 = R.call(7236, [int34, int7, int2, int3, int4, int44, "Heals", string7]);
      }
      if ((R.itemParam(int0, 6924) > 0)) {
        int44 = R.call(7235, [int2, int3, int4, int44, `Allows overhealing up to <col=00ff00>${R.s(R.str(R.itemParam(int0, 6924), 10))}%</col> of your maximum <sprite=18851><nbsp><col=ED705A>Life<nbsp>Points</col>.`, string3]);
      }
      if (((R.itemParam(int0, 4653) > 0) && (R.len(R.itemParam(int0, 4797)) > 0))) {
        int44 = R.call(7235, [int2, int3, int4, int44, `Regenerates <col=00ff00>${R.s(R.str(R.itemParam(int0, 4653), 10))} health</col> every <col=00ffff>1.2 seconds</col> over <col=00ffff>${R.s(R.itemParam(int0, 4797))} seconds</col>.`, string3]);
      }
      int34 = R.call(4000, [int0]);
      if ((int34 > 0)) {
        int44 = R.call(7236, [int34, int7, int2, int3, int4, int44, "Prayer points", string7]);
      }
      if (R.eq(R.itemParam(int0, 4342), 0)) {
        int44 = R.call(7235, [int2, int3, int4, int44, `${R.s(R.call(18556, [R.call(21028, [])]))} while in combat.`, string3]);
      }
    }
    if ((!R.eq(R.itemParam(int0, 3698), 0))) {
      string7 = "<col=00ff00>";
      int34 = R.itemParam(int0, 3698);
      if ((R.eq(R.vb(27168), 1) && (!R.eq(R.vc(1533), 1)))) {
        int34 = R.idiv(int34, 10);
      }
      int44 = R.call(7236, [int34, int7, int2, int3, int4, int44, "Heals", string7]);
    }
    if ((!R.eq(R.itemParam(int0, 3702), 0))) {
      int44 = R.call(7235, [int2, int3, int4, int44, `Constitution requirement: ${R.s(R.str(R.itemParam(int0, 3702), 10))}`, string3]);
    }
    if ((!R.eq(R.itemParam(int0, 4245), 0))) {
      int11 = R.itemParam(int0, 4245);
      if ((R.eq(R.vb(27168), 1) && (!R.eq(R.vc(1533), 1)))) {
        int11 = R.idiv(int11, 10);
      }
      int44 = R.call(7235, [int2, int3, int4, int44, `Deals ${R.s(R.str(int11, 10))} damage to the last opponent that attacked you.`, string3]);
    }
    if ((!R.eq(R.itemParam(int0, 3674), 0))) {
      string7 = "<col=00ff00>";
      int34 = R.itemParam(int0, 3674);
      if ((R.eq(R.vb(27168), 1) && (!R.eq(R.vc(1533), 1)))) {
        int34 = R.idiv(int34, 10);
      }
      int44 = R.call(7236, [int34, int7, int2, int3, int4, int44, "Heals", string7]);
    }
    if ((!R.eq(R.itemParam(int0, 3683), 0))) {
      int44 = R.call(7235, [int2, int3, int4, int44, `Constitution level requirement: ${R.s(R.str(R.itemParam(int0, 3683), 10))}`, string3]);
    }
    if ((!R.eq(R.itemParam(int0, 3675), 0))) {
      int44 = R.call(7235, [int2, int3, int4, int44, `Will offer to open a Daemonheim skill door which has requirements you don't meet, within ${R.s(R.str(R.itemParam(int0, 3675), 10))} skill levels.`, string3]);
    }
    var int46 = (-1 | 0);
    if ((!R.eq(R.itemParam(int0, 3697), 0))) {
      int10 = R.scale(R.itemParam(int0, 3697), 100, 60);
      int44 = R.call(7235, [int2, int3, int4, int44, `Extends the duration of your gravestone by ${R.s(R.str(int10, 10))} seconds, up to a maximum of 5 minutes.`, string3]);
    }
    if ((!R.eq(R.itemParam(int0, 3699), 0))) {
      int44 = R.call(7235, [int2, int3, int4, int44, `When equipped, will transport up to ${R.s(R.str(R.itemParam(int0, 3699), 10))} received items to the bank before depleting.`, string3]);
    }
    R.dbFind(348240, int0, 0);
    int46 = R.dbNext();
    if ((!R.eq(int46, (-1 | 0)))) {
      int44 = R.call(7235, [int2, int3, int4, int44, R.call(3956, [int46]), string3]);
      int46 = (-1 | 0);
    }
    R.dbFind(348256, int0, 0);
    int46 = R.dbNext();
    if ((!R.eq(int46, (-1 | 0)))) {
      int44 = R.call(7235, [int2, int3, int4, int44, R.call(5521, [int46]), string3]);
      int46 = (-1 | 0);
    }
    R.dbFind(1515584, int0, 0);
    int46 = R.dbNext();
    if ((!R.eq(int46, (-1 | 0)))) {
      int44 = R.call(7235, [int2, int3, int4, int44, R.call(15086, [int46]), string3]);
      int46 = (-1 | 0);
    }
    if (((!R.eq(R.vc(5121), (-1 | 0))) && (R.vc(5122) >= 0))) {
      if ((!R.eq(R.itemParam(int0, 5553), 0))) {
        if ((R.eq(R.vb(49710), 0) || R.eq(R.vb(49710), 1))) {
          int44 = R.call(12197, [int0, R.vc(5121), R.vc(5122), int2, int3, int4, int44]);
        }
        if ((R.eq(R.vb(49710), 0) || R.eq(R.vb(49710), 2))) {
          int44 = R.call(15935, [int0, R.vc(5121), R.vc(5122), int2, int3, int4, int44, string3]);
        }
      }
      if (((R.eq(R.itemParam(int0, 6810), 1) && (!R.eq(R.vc(5121), (-1 | 0)))) && (R.vc(5122) >= 0))) {
        if (R.eq(R.itemCategory(int0), 4430)) {
          if ((R.invVar(R.vc(5121), R.vc(5122), 42932) > 0)) {
            int44 = R.call(7235, [int2, int3, int4, int44, `<col=00ff00>${R.s(R.enumValue(0, 36, 15018, R.invVar(R.vc(5121), R.vc(5122), 42932)))}`, string3]);
          } else {
            int44 = R.call(7235, [int2, int3, int4, int44, "<col=ff0000>Empty slot", string3]);
          }
          if ((R.invVar(R.vc(5121), R.vc(5122), 42933) > 0)) {
            int44 = R.call(7235, [int2, int3, int4, int44, `<col=00ff00>${R.s(R.enumValue(0, 36, 15018, R.invVar(R.vc(5121), R.vc(5122), 42933)))}`, string3]);
          } else {
            int44 = R.call(7235, [int2, int3, int4, int44, "<col=ff0000>Empty slot", string3]);
          }
          if ((R.invVar(R.vc(5121), R.vc(5122), 42934) > 0)) {
            int44 = R.call(7235, [int2, int3, int4, int44, `<col=00ff00>${R.s(R.enumValue(0, 36, 15018, R.invVar(R.vc(5121), R.vc(5122), 42934)))}`, string3]);
          } else {
            int44 = R.call(7235, [int2, int3, int4, int44, "<col=ff0000>Empty slot", string3]);
          }
          if ((R.invVar(R.vc(5121), R.vc(5122), 42935) > 0)) {
            int44 = R.call(7235, [int2, int3, int4, int44, `<col=00ff00>${R.s(R.enumValue(0, 36, 15018, R.invVar(R.vc(5121), R.vc(5122), 42935)))}`, string3]);
          } else {
            int44 = R.call(7235, [int2, int3, int4, int44, "<col=ff0000>Empty slot", string3]);
          }
          if ((R.invVar(R.vc(5121), R.vc(5122), 42936) > 0)) {
            int44 = R.call(7235, [int2, int3, int4, int44, `<col=00ff00>${R.s(R.enumValue(0, 36, 15018, R.invVar(R.vc(5121), R.vc(5122), 42936)))}`, string3]);
          } else {
            int44 = R.call(7235, [int2, int3, int4, int44, "<col=ff0000>Empty slot", string3]);
          }
          if ((R.invVar(R.vc(5121), R.vc(5122), 42937) > 0)) {
            int44 = R.call(7235, [int2, int3, int4, int44, `<col=00ff00>${R.s(R.enumValue(0, 36, 15018, R.invVar(R.vc(5121), R.vc(5122), 42937)))}`, string3]);
          } else {
            int44 = R.call(7235, [int2, int3, int4, int44, "<col=ff0000>Empty slot", string3]);
          }
        }
        if (((((!R.eq(int0, 44540)) && (!R.eq(R.itemCategory(int0), 4058))) && (!R.eq(R.itemCategory(int0), 4431))) && ((!R.eq(R.itemCategory(int0), 4430)) || R.eq(R.call(12676, [44542]), 0)))) {
          int34 = R.invVar(R.vc(5121), R.vc(5122), 30214);
          int44 = R.call(7236, [int34, int7, int2, int3, int4, int44, "Charges remaining", ""]);
        }
      }
      if (R.eq(int0, 41081)) {
        if ((!R.eq(R.vp(7253), (-1 | 0)))) {
          int38 = R.itemParam(R.vp(7253), 2281);
          string1 = R.structParam(int38, 2524);
          int44 = R.call(7235, [int2, int3, int4, int44, `Potion stored: ${R.s(string1)} (${R.s(R.strLoc(R.vb(37584), 1))})`, string3]);
          if (R.eq(R.vb(37583), 0)) {
            int44 = R.call(7235, [int2, int3, int4, int44, "Device status: Off", string3]);
          } else {
            int44 = R.call(7235, [int2, int3, int4, int44, "Device status: On", string3]);
          }
        } else {
          int44 = R.call(7235, [int2, int3, int4, int44, "Potion stored: Nothing!", string3]);
        }
      }
      if (R.eq(R.vb(42534), 0)) {
        int38 = R.itemParam(int0, 2281);
        if ((R.eq(int0, 41081) && (!R.eq(R.vp(7253), (-1 | 0))))) {
          int38 = R.itemParam(R.vp(7253), 2281);
        }
        if ((!R.eq(int38, (-1 | 0)))) {
          int38 = R.structParam(int38, 7601);
          if ((!R.eq(int38, (-1 | 0)))) {
            int44 = R.call(7235, [int2, int3, int4, int44, R.call(4744, [int38, 1]), string3]);
          }
          int38 = (-1 | 0);
        } else {
          int38 = R.itemParam(int0, 7601);
          if ((R.eq(int0, 41081) && (!R.eq(R.vp(7253), (-1 | 0))))) {
            int38 = R.itemParam(R.vp(7253), 7601);
          }
          if ((!R.eq(int38, (-1 | 0)))) {
            int44 = R.call(7235, [int2, int3, int4, int44, R.call(4744, [int38, 1]), string3]);
            int38 = (-1 | 0);
          }
        }
      }
      if (((!R.eq(R.itemParam(int0, 7796), (-1 | 0))) || (R.eq(R.itemCategory(int0), 67) && R.eq(R.itemParam(int0, 1047), 1)))) {
        int44 = R.call(2579, [int0, int7, int2, int3, int4, int44]);
      }
      if ((!R.eq(R.itemParam(int0, 6663), (-1 | 0)))) {
        int44 = R.call(14793, [int0, int7, int2, int3, int4, int44]);
      }
      if (R.eq(R.itemParam(int0, 8229), 1)) {
        if (R.eq(R.invVar(R.vc(5121), R.vc(5122), 27398), 1)) {
          if (R.eq(int0, 50682)) {
            int44 = R.call(7235, [int2, int3, int4, int44, "Discovery mode", string3]);
          } else {
            int44 = R.call(7235, [int2, int3, int4, int44, "XP mode", string3]);
          }
        } else {
          if (R.eq(R.invVar(R.vc(5121), R.vc(5122), 27398), 2)) {
            if (R.eq(int0, 50682)) {
              int44 = R.call(7235, [int2, int3, int4, int44, "Material mode", string3]);
            } else {
              if (R.eq(int0, 34519)) {
                int44 = R.call(7235, [int2, int3, int4, int44, "Damage mode", string3]);
              } else {
                int44 = R.call(7235, [int2, int3, int4, int44, "Resource mode", string3]);
              }
            }
          }
        }
      }
      if (R.eq(R.itemCategory(int0), 5694)) {
        [int11, int12, int14] = R.call(4705, [R.itemParam(int0, 9432)]);
        int44 = R.call(7235, [int2, int3, int4, int44, `Seeds required: <col=FF00>${R.s(R.str(R.itemParam(int0, 9433), 10))}`, string3]);
        int44 = R.call(7235, [int2, int3, int4, int44, `Time to trap: <col=FF00>${R.s(R.call(5729, [(-1 | 0), int12, int14, 0, 0]))} seconds`, string3]);
      }
      if ((R.eq(R.itemParam(int0, 6833), 1) || R.eq(R.call(4034, [int0]), 1))) {
        int44 = R.call(13776, [int0, int3, int4, int44]);
      }
      if ((!R.eq(R.itemParam(int0, 667), 0))) {
        if ((R.eq(int0, 27616) || R.eq(int0, 27617))) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Kills: ${R.s(R.strLoc(R.vb(18238), 1))}`, string3]);
        }
        if ((R.eq(int0, 27618) || R.eq(int0, 27619))) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Kills: ${R.s(R.strLoc(R.vb(18242), 1))}`, string3]);
        }
        if ((R.eq(int0, 27622) || R.eq(int0, 27623))) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Kills: ${R.s(R.strLoc(R.vb(18250), 1))}`, string3]);
        }
        if ((R.eq(int0, 27620) || R.eq(int0, 27621))) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Kills: ${R.s(R.strLoc(R.vb(18246), 1))}`, string3]);
        }
        if ((R.eq(int0, 27624) || R.eq(int0, 27625))) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Kills: ${R.s(R.strLoc(R.vb(18254), 1))}`, string3]);
        }
        if ((R.eq(int0, 31089) || R.eq(int0, 31090))) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Kills: ${R.s(R.strLoc(R.vb(21930), 1))}`, string3]);
        }
        if ((R.eq(int0, 31091) || R.eq(int0, 31092))) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Kills: ${R.s(R.strLoc(R.vb(21934), 1))}`, string3]);
        }
        if ((R.eq(int0, 31093) || R.eq(int0, 31094))) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Kills: ${R.s(R.strLoc(R.vb(21938), 1))}`, string3]);
          if ((R.dateRuneday() < R.vb(21951))) {
            int44 = R.call(7235, [int2, int3, int4, int44, `Days until task can be forced: ${R.s(R.strLoc(((R.vb(21951)) - (R.dateRuneday()) | 0), 1))}`, string3]);
          }
        }
        if ((R.eq(int0, 31095) || R.eq(int0, 31096))) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Kills: ${R.s(R.strLoc(R.vb(21942), 1))}`, string3]);
          if ((R.dateRuneday() < R.vb(21951))) {
            int44 = R.call(7235, [int2, int3, int4, int44, `Days until task can be forced: ${R.s(R.strLoc(((R.vb(21951)) - (R.dateRuneday()) | 0), 1))}`, string3]);
          }
        }
        if ((R.eq(int0, 31097) || R.eq(int0, 31098))) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Kills: ${R.s(R.strLoc(R.vb(21946), 1))}`, string3]);
          if ((R.dateRuneday() < R.vb(21951))) {
            int44 = R.call(7235, [int2, int3, int4, int44, `Days until task can be forced: ${R.s(R.strLoc(((R.vb(21951)) - (R.dateRuneday()) | 0), 1))}`, string3]);
          }
        }
        if ((R.eq(int0, 31099) || R.eq(int0, 31100))) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Kills: ${R.s(R.strLoc(R.vb(21950), 1))}`, string3]);
        }
        if ((R.eq(int0, 35277) || R.eq(int0, 35278))) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Kills: ${R.s(R.strLoc(R.vb(26736), 1))}`, string3]);
          if ((R.dateRuneday() < R.vb(28874))) {
            int44 = R.call(7235, [int2, int3, int4, int44, `Days until task can be forced: ${R.s(R.strLoc(((R.vb(28874)) - (R.dateRuneday()) | 0), 1))}`, string3]);
          }
        }
        if ((R.eq(int0, 35279) || R.eq(int0, 35280))) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Kills: ${R.s(R.strLoc(R.vb(26740), 1))}`, string3]);
        }
        if ((R.eq(int0, 35281) || R.eq(int0, 35282))) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Kills: ${R.s(R.strLoc(R.vb(26744), 1))}`, string3]);
          if ((R.dateRuneday() < R.vb(28875))) {
            int44 = R.call(7235, [int2, int3, int4, int44, `Days until task can be forced: ${R.s(R.strLoc(((R.vb(28875)) - (R.dateRuneday()) | 0), 1))}`, string3]);
          }
        }
        if ((R.eq(int0, 35285) || R.eq(int0, 35286))) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Kills: ${R.s(R.strLoc(R.vb(28865), 1))}`, string3]);
          if ((R.dateRuneday() < R.vb(28877))) {
            int44 = R.call(7235, [int2, int3, int4, int44, `Days until task can be forced: ${R.s(R.strLoc(((R.vb(28877)) - (R.dateRuneday()) | 0), 1))}`, string3]);
          }
        }
        if ((R.eq(int0, 35287) || R.eq(int0, 35288))) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Kills: ${R.s(R.strLoc(R.vb(28869), 1))}`, string3]);
          if ((R.dateRuneday() < R.vb(28878))) {
            int44 = R.call(7235, [int2, int3, int4, int44, `Days until task can be forced: ${R.s(R.strLoc(((R.vb(28878)) - (R.dateRuneday()) | 0), 1))}`, string3]);
          }
        }
        if ((R.eq(int0, 35289) || R.eq(int0, 35290))) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Kills: ${R.s(R.strLoc(R.vb(28873), 1))}`, string3]);
          if ((R.dateRuneday() < R.vb(28879))) {
            int44 = R.call(7235, [int2, int3, int4, int44, `Days until task can be forced: ${R.s(R.strLoc(((R.vb(28879)) - (R.dateRuneday()) | 0), 1))}`, string3]);
          }
        }
        if ((R.eq(int0, 35283) || R.eq(int0, 35284))) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Kills: ${R.s(R.strLoc(R.vb(28861), 1))}`, string3]);
          if ((R.dateRuneday() < R.vb(28876))) {
            int44 = R.call(7235, [int2, int3, int4, int44, `Days until task can be forced: ${R.s(R.strLoc(((R.vb(28876)) - (R.dateRuneday()) | 0), 1))}`, string3]);
          }
        }
        if ((R.eq(int0, 28686) || R.eq(int0, 28687))) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Kills: ${R.s(R.strLoc(R.vb(18611), 1))}`, string3]);
        }
        if ((R.eq(int0, 28688) || R.eq(int0, 28689))) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Kills: ${R.s(R.strLoc(R.vb(18615), 1))}`, string3]);
        }
        if ((R.eq(int0, 28690) || R.eq(int0, 28691))) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Kills: ${R.s(R.strLoc(R.vb(18619), 1))}`, string3]);
        }
        if ((R.eq(int0, 28692) || R.eq(int0, 28693))) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Kills: ${R.s(R.strLoc(R.vb(18623), 1))}`, string3]);
        }
        if ((R.eq(int0, 28694) || R.eq(int0, 28695))) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Kills: ${R.s(R.strLoc(R.vb(18627), 1))}`, string3]);
        }
      }
      if (R.eq(R.itemHasVarobj(int0), 1)) {
        switch (R.key(int0)) {
          case 20120:
          {
            if (R.eq(int0, 20120)) {
              int17 = R.invVar(R.vc(5121), R.vc(5122), 15191);
              int44 = R.call(7235, [int2, int3, int4, int44, `Charges remaining: ${R.s(R.str(int17, 10))}`, string3]);
            }
            break;
          }
          case 40408:
          case 42892:
          {
            int40 = R.enumValue(0, 32, 12737, R.invVar(R.vc(5121), R.vc(5122), 36325));
            string1 = R.structParam(R.enumValue(32, 73, 12740, int40), 6600);
            int44 = R.call(7235, [int2, int3, int4, int44, `Soul contained: ${R.s(string1)}`, string3]);
            break;
          }
          case 41076:
          {
            int17 = R.invVar(R.vc(5121), R.vc(5122), 37518);
            int44 = R.call(7235, [int2, int3, int4, int44, `Charges remaining: ${R.s(R.str(((10) - (int17) | 0), 10))}`, string3]);
            break;
          }
          case 41078:
          {
            int17 = R.invVar(R.vc(5121), R.vc(5122), 37519);
            int44 = R.call(7235, [int2, int3, int4, int44, `Charges remaining: ${R.s(R.str(((10) - (int17) | 0), 10))}`, string3]);
            break;
          }
          case 41083:
          {
            int17 = R.invVar(R.vc(5121), R.vc(5122), 37521);
            int34 = R.invVar(R.vc(5121), R.vc(5122), 37522);
            int16 = R.invVar(R.vc(5121), R.vc(5122), 37523);
            int44 = R.call(7235, [int2, int3, int4, int44, `Empty divine charges stored: ${R.s(R.str(int17, 10))}`, string3]);
            int44 = R.call(7235, [int2, int3, int4, int44, `Divine charges stored: ${R.s(R.str(int34, 10))}`, string3]);
            int44 = R.call(7235, [int2, int3, int4, int44, `Charge container filled: ${R.s(R.str(R.call(12422, [int16, 30000, 100]), 10))}%`, string3]);
            break;
          }
          case 41085:
          {
            int17 = R.invVar(R.vc(5121), R.vc(5122), 37520);
            int44 = R.call(7235, [int2, int3, int4, int44, `Charges remaining: ${R.s(R.str(((60) - (int17) | 0), 10))}`, string3]);
            break;
          }
          case 27360:
          {
            int38 = R.enumValue(0, 73, 7319, R.invVar(R.vc(5121), R.vc(5122), 17956));
            string1 = R.enumValue(0, 36, 1563, R.structParam(int38, 3248));
            int17 = R.invVar(R.vc(5121), R.vc(5122), 17957);
            int44 = R.call(7235, [int2, int3, int4, int44, `Target to kill: ${R.s(string1)}`, string3]);
            int44 = R.call(7235, [int2, int3, int4, int44, `Kills: ${R.s(R.str(int17, 10))}`, string3]);
            break;
          }
          case 24205:
          {
            int34 = R.invVar(R.vc(5121), R.vc(5122), 16523);
            int44 = R.call(7235, [int2, int3, int4, int44, "Capacity: 18", string3]);
            if (R.eq(R.call(14608, [2896]), 1)) {
              int44 = R.call(7235, [int2, int3, int4, int44, "Charges remaining: <col=00ff00>Infinite - Relic active</col>", string3]);
            } else {
              int44 = R.call(7235, [int2, int3, int4, int44, `Charges remaining: ${R.s(R.str(int34, 10))}/756`, string3]);
            }
            break;
          }
          case 21536:
          {
            int17 = R.invVar(R.vc(5121), R.vc(5122), 5129);
            int44 = R.call(7235, [int2, int3, int4, int44, `Charges remaining: ${R.s(R.str(int17, 10))}`, string3]);
            break;
          }
          case 42519:
          {
            int17 = R.invVar(R.vc(5121), R.vc(5122), 40136);
            int44 = R.call(7235, [int2, int3, int4, int44, `Doses remaining: ${R.s(R.strLoc(int17, 1))}/${R.s(R.strLoc(2000, 1))}`, string3]);
            break;
          }
          case 21576:
          {
            int17 = R.invVar(R.vc(5121), R.vc(5122), 11080);
            int44 = R.call(7235, [int2, int3, int4, int44, `Charges remaining: ${R.s(R.str(int17, 10))}`, string3]);
            break;
          }
          case 21575:
          {
            int17 = R.invVar(R.vc(5121), R.vc(5122), 11080);
            int44 = R.call(7235, [int2, int3, int4, int44, `Charges remaining: ${R.s(R.str(int17, 10))}`, string3]);
            break;
          }
          case 42379:
          {
            int17 = R.invVar(R.vc(5121), R.vc(5122), 11080);
            int44 = R.call(7235, [int2, int3, int4, int44, `Charges remaining: ${R.s(R.str(int17, 10))}`, string3]);
            break;
          }
          case 47068:
          {
            int37 = R.enumValue(0, 33, 15095, R.invVar(R.vc(5121), R.vc(5122), 43222));
            if ((!R.eq(int37, (-1 | 0)))) {
              int44 = R.call(7235, [int2, int3, int4, int44, `Item being created: ${R.s(R.itemName(int37))}`, string3]);
              int44 = R.call(7235, [int2, int3, int4, int44, `Current heat: ${R.s(R.strLoc(R.invVar(R.vc(5121), R.vc(5122), 43225), 1))}/${R.s(R.strLoc(R.call(2547, [int37]), 1))} (${R.s(R.strLoc(R.scale(R.invVar(R.vc(5121), R.vc(5122), 43225), R.call(2547, [int37]), 100), 1))}%)`, string3]);
              int44 = R.call(7235, [int2, int3, int4, int44, `Current progress: ${R.s(R.strLoc(R.invVar(R.vc(5121), R.vc(5122), 43223), 1))}/${R.s(R.strLoc(R.itemParam(int37, 7801), 1))} (${R.s(R.strLoc(R.scale(R.invVar(R.vc(5121), R.vc(5122), 43223), R.itemParam(int37, 7801), 100), 1))}%) `, string3]);
              int44 = R.call(7235, [int2, int3, int4, int44, `Experience left in item: ${R.s(R.strLoc(R.idiv(R.invVar(R.vc(5121), R.vc(5122), 43224), 10), 1))}`, string3]);
            }
            break;
          }
          case 58111:
          {
            int37 = R.enumValue(0, 33, 6535, R.invVar(R.vc(5121), R.vc(5122), 56966));
            if ((!R.eq(int37, (-1 | 0)))) {
              int44 = R.call(7235, [int2, int3, int4, int44, `Item being created: ${R.s(R.itemName(int37))}`, string3]);
              int44 = R.call(7235, [int2, int3, int4, int44, `Current progress: ${R.s(R.strLoc(R.invVar(R.vc(5121), R.vc(5122), 56967), 1))}/${R.s(R.strLoc(R.itemParam(int37, 9218), 1))} (${R.s(R.strLoc(R.scale(R.invVar(R.vc(5121), R.vc(5122), 56967), R.itemParam(int37, 9218), 100), 1))}%) `, string3]);
              int44 = R.call(7235, [int2, int3, int4, int44, `Experience left in item: ${R.s(R.strLoc(R.idiv(R.invVar(R.vc(5121), R.vc(5122), 56968), 10), 1))}`, string3]);
            }
            break;
          }
          case 45990:
          {
            int44 = R.call(7235, [int2, int3, int4, int44, `Folding progress: ${R.s(R.str(R.invVar(R.vc(5121), R.vc(5122), 43229), 10))}/1001`, string3]);
            break;
          }
          case 47719:
          {
            int17 = R.invVar(R.vc(5121), R.vc(5122), 43749);
            int34 = ((10) + (R.invVar(R.vc(5121), R.vc(5122), 47701)) | 0);
            if ((R.invVar(R.vc(5121), R.vc(5122), 43756) > 0)) {
              int34 = ((int34) + (3) | 0);
            }
            int16 = R.invVar(R.vc(5121), R.vc(5122), 43757);
            int19 = R.invVar(R.vc(5121), R.vc(5122), 48186);
            if (R.eq(int19, 1)) {
              if (R.eq(int16, 1)) {
                int44 = R.call(7235, [int2, int3, int4, int44, "Divine Phoenix", string3]);
              } else {
                int44 = R.call(7235, [int2, int3, int4, int44, "Phoenix", string3]);
              }
            } else {
              if (R.eq(int16, 1)) {
                int44 = R.call(7235, [int2, int3, int4, int44, "Divine", string3]);
              }
            }
            if ((R.eq(int16, 0) && R.eq(int19, 0))) {
              int44 = R.call(7235, [int2, int3, int4, int44, `Host item: ${R.s(R.enumValue(0, 36, 12899, int17))}`, string3]);
            }
            int44 = R.call(7235, [int2, int3, int4, int44, `Reward rate: ${R.s(R.str(R.idiv(int34, 10), 10))}.${R.s(R.str(R.mod(int34, 10), 10))}x`, string3]);
            break;
          }
          case 28014:
          {
            int44 = R.call(7235, [int2, int3, int4, int44, `Charges remaining: ${R.s(R.strLoc(((100) - (R.invVar(R.vc(5121), R.vc(5122), 18289)) | 0), 1))}`, string3]);
            break;
          }
          case 48680:
          {
            int44 = R.call(7235, [int2, int3, int4, int44, `Doses remaining: ${R.s(R.str(R.idiv(R.invVar(R.vc(5121), R.vc(5122), 18550), 500), 10))}`, string3]);
            break;
          }
          case 49978:
          {
            int17 = R.invVar(R.vc(5121), R.vc(5122), 47034);
            int44 = R.call(7235, [int2, int3, int4, int44, `Charges: ${R.s(R.strLoc(int17, 1))}/${R.s(R.strLoc(100000, 1))}`, string3]);
            break;
          }
          case 49981:
          {
            int17 = R.invVar(R.vc(5121), R.vc(5122), 47034);
            int44 = R.call(7235, [int2, int3, int4, int44, `Charges left: ${R.s(R.strLoc(int17, 1))}`, string3]);
            break;
          }
          case 37407:
          {
            int44 = R.call(7235, [int2, int3, int4, int44, `Charge: ${R.s(R.str(R.scale(R.invVar(R.vc(5121), R.vc(5122), 31400), 30, 100), 10))}%`, string3]);
            break;
          }
          case 51275:
          {
            int44 = R.call(7235, [int2, int3, int4, int44, `Charges remaining: ${R.s(R.str(R.invVar(R.vc(5121), R.vc(5122), 48985), 10))}`, string3]);
            break;
          }
          case 36619:
          case 36620:
          {
            if (R.eq(R.vb(15960), 1)) {
              int44 = R.call(7235, [int2, int3, int4, int44, "On activation: heals 4% of your maximum life points 10 times over 40 seconds. 5 minute cooldown.", string3]);
            } else {
              int44 = R.call(7235, [int2, int3, int4, int44, "On activation: heals 4% of your maximum life points 5 times over 20 seconds. 5 minute cooldown.", string3]);
            }
            break;
          }
          case 57066:
          {
            int44 = R.call(7235, [int2, int3, int4, int44, `You have performed ${R.s(R.str(R.invVar(R.vc(5121), R.vc(5122), 55665), 10))}/${R.s(R.str(R.invVar(R.vc(5121), R.vc(5122), 55664), 10))} rituals.`, string3]);
            int44 = R.call(7235, [int2, int3, int4, int44, `Preferred ritual type: ${R.s(R.enumValue(0, 36, 17518, R.invVar(R.vc(5121), R.vc(5122), 55663)))} rituals.`, string3]);
            break;
          }
          case 58325:
          case 58326:
          {
            if ((R.vb(57039) < 200)) {
              int44 = R.call(7235, [int2, int3, int4, int44, `Elite profane scabarites killed: ${R.s(R.str(R.vb(57039), 10))} / ${R.s(R.str(200, 10))}`, string3]);
            }
            break;
          }
          case 58493:
          {
            int37 = R.enumValue(0, 33, 7927, R.invVar(R.vc(5121), R.vc(5122), 57697));
            if ((!R.eq(int37, (-1 | 0)))) {
              int44 = R.call(7235, [int2, int3, int4, int44, `Item being created: ${R.s(R.itemName(int37))}`, string3]);
              int44 = R.call(7235, [int2, int3, int4, int44, `Current progress: ${R.s(R.str(R.invVar(R.vc(5121), R.vc(5122), 57698), 10))}/${R.s(R.str(R.itemParam(int37, 9241), 10))} (${R.s(R.str(R.scale(R.invVar(R.vc(5121), R.vc(5122), 57698), R.itemParam(int37, 9241), 100), 10))}%) `, string3]);
              int44 = R.call(7235, [int2, int3, int4, int44, `Experience left in item: ${R.s(R.str(R.idiv(Math.imul(((R.itemParam(int37, 9241)) - (R.invVar(R.vc(5121), R.vc(5122), 57698)) | 0), R.itemParam(int37, 9242)), 10), 10))}`, string3]);
            }
            break;
          }
          case 63595:
          {
            if ((R.eq(R.vb(49710), 0) || R.eq(R.vb(49710), 1))) {
              int44 = R.call(12197, [int0, R.vc(5121), R.vc(5122), int2, int3, int4, int44]);
            }
            if ((R.eq(R.vb(49710), 0) || R.eq(R.vb(49710), 2))) {
              int44 = R.call(15935, [int0, R.vc(5121), R.vc(5122), int2, int3, int4, int44, string3]);
            }
            int17 = R.invVar(R.vc(5121), R.vc(5122), 47034);
            int44 = R.call(7235, [int2, int3, int4, int44, `Charges: ${R.s(R.strLoc(int17, 1))}/${R.s(R.strLoc(1000000, 1))}`, string3]);
            switch (R.key(R.vb(61611))) {
              case 0:
              {
                int44 = R.call(7235, [int2, int3, int4, int44, "Capacitor mode: Disabled", string3]);
                break;
              }
              case 1:
              {
                int44 = R.call(7235, [int2, int3, int4, int44, "Capacitor mode: Charging", string3]);
                break;
              }
              case 2:
              {
                int44 = R.call(7235, [int2, int3, int4, int44, "Capacitor mode: Discharging", string3]);
                break;
              }
            }
            break;
          }
          default:
          {
            switch (R.key(R.itemCategory(int0))) {
              case 5568:
              case 5569:
              case 5570:
              {
                int37 = R.enumValue(0, 33, 8465, R.invVar(R.vc(5121), R.vc(5122), 58104));
                if ((!R.eq(int37, (-1 | 0)))) {
                  int44 = R.call(7235, [int2, int3, int4, int44, `Item being created: ${R.s(R.itemName(int37))}`, string3]);
                  if ((R.invVar(R.vc(5121), R.vc(5122), 58107) >= R.call(20006, [int37]))) {
                    int44 = R.call(7235, [int2, int3, int4, int44, "<col=ff0000>You have damaged this item. Until you repair it, your progress when working on this item is halved.", string3]);
                  } else {
                    int44 = R.call(7235, [int2, int3, int4, int44, `Current miscuts: ${R.s(R.strLoc(R.invVar(R.vc(5121), R.vc(5122), 58107), 1))}/${R.s(R.strLoc(R.call(20006, [int37]), 1))} (${R.s(R.strLoc(R.scale(R.invVar(R.vc(5121), R.vc(5122), 58107), R.call(20006, [int37]), 100), 1))}%)`, string3]);
                  }
                  int44 = R.call(7235, [int2, int3, int4, int44, `Current progress: ${R.s(R.strLoc(R.invVar(R.vc(5121), R.vc(5122), 58105), 1))}/${R.s(R.strLoc(R.call(20005, [R.itemParam(int37, 9300), int37]), 1))} (${R.s(R.strLoc(R.scale(R.invVar(R.vc(5121), R.vc(5122), 58105), R.call(20005, [R.itemParam(int37, 9300), int37]), 100), 1))}%) `, string3]);
                  int44 = R.call(7235, [int2, int3, int4, int44, `Experience left in item: ${R.s(R.strLoc(R.idiv(R.invVar(R.vc(5121), R.vc(5122), 58106), 10), 1))}`, string3]);
                }
                break;
              }
              case 4452:
              {
                if (R.eq(R.itemParam(int0, 7804), 2)) {
                  int44 = R.call(7235, [int2, int3, int4, int44, `${R.s(R.str(R.invVar(R.vc(5121), R.vc(5122), 43230), 10))}/${R.s(R.str(R.itemParam(int0, 2665), 10))} rivets applied.`, string3]);
                }
                break;
              }
              case 5442:
              case 5506:
              case 5572:
              {
                if ((R.itemParam(int0, 9122) > 0)) {
                  if ((!R.eq(R.itemParam(int0, 9123), (-1 | 0)))) {
                    int44 = R.call(7235, [int2, int3, int4, int44, `Item to craft: ${R.s(R.itemName(R.itemParam(int0, 9123)))}`, string3]);
                  }
                  int44 = R.call(7235, [int2, int3, int4, int44, `Current progress: ${R.s(R.str(R.invVar(R.vc(5121), R.vc(5122), 55926), 10))}/${R.s(R.str(R.itemParam(int0, 9122), 10))} (${R.s(R.str(R.scale(R.invVar(R.vc(5121), R.vc(5122), 55926), R.itemParam(int0, 9122), 100), 10))}%) `, string3]);
                }
                break;
              }
              case 2804:
              case 3794:
              {
                if (R.eq(R.itemCategory(int0), 2804)) {
                  if ((R.eq(R.vc(5121), 94) && R.eq(R.invVar(R.vc(5121), R.vc(5122), 17232), 1))) {
                    int17 = R.vb(17234);
                  } else {
                    int17 = R.invVar(R.vc(5121), R.vc(5122), 17233);
                  }
                } else {
                  if (R.eq(R.itemCategory(int0), 3794)) {
                    if (R.eq(R.invVar(R.vc(5121), R.vc(5122), 30602), 1)) {
                      int17 = R.vb(30604);
                    } else {
                      int17 = R.invVar(R.vc(5121), R.vc(5122), 30603);
                    }
                  }
                }
                [int11, int12, int14] = R.call(4705, [int17]);
                if ((R.eq(int11, 0) && (int12 <= 59))) {
                  string6 = "<col=ff0000>";
                }
                if ((!R.eq(R.itemParam(int0, 8727), 0))) {
                  int44 = R.call(7235, [int2, int3, int4, int44, `<col=00ffff>${R.s(R.str(R.call(2475, [R.itemParam(int0, 8727)]), 10))} second cooldown</col>.`, string3]);
                }
                if ((!R.eq(R.itemParam(int0, 9345), 1))) {
                  int44 = R.call(7235, [int2, int3, int4, int44, `${R.s(string6)}${R.s(R.call(5729, [int11, int12, int14, 0, 0]))} remaining`, string3]);
                }
                break;
              }
              case 5087:
              {
                int44 = R.call(9716, [int0, R.vc(5121), R.vc(5122), int3, int4, int44, int7]);
                int44 = R.call(10290, [int0, R.vc(5121), R.vc(5122), int2, int3, int4, int44, string3]);
                break;
              }
              case 2771:
              case 2769:
              case 2770:
              {
                int17 = R.invVar(R.vc(5121), R.vc(5122), 3652);
                int17 = R.scale(int17, 15000, 1000);
                if (R.eq(int17, 0)) {
                  int17 = 1;
                }
                string1 = R.str(R.idiv(int17, 10), 10);
                string1 = R.cat(string1, `.${R.s(R.str(R.mod(int17, 10), 10))}`);
                string2 = "<col=ffffff>";
                if ((int17 <= 250)) {
                  string2 = "<col=ff0000>";
                } else {
                  if ((int17 <= 650)) {
                    string2 = "<col=FFA500>";
                  } else {
                    string2 = "<col=00ff00>";
                  }
                }
                int44 = R.call(7235, [int2, int3, int4, int44, `Item Charge: ${R.s(string2)}${R.s(string1)}%`, string3]);
                break;
              }
              case 3021:
              {
                if (R.eq(int0, 44550)) {
                  int44 = R.call(7235, [int2, int3, int4, int44, R.call(13424, [R.call(734, [R.vb(35985)]), `Stored sign of the porter (disabled) charges: ${R.s(R.str(R.invVar(R.vc(5121), R.vc(5122), 30214), 10))}/${R.s(R.str(R.call(17012, []), 10))}`, `Stored sign of the porter charges: ${R.s(R.str(R.invVar(R.vc(5121), R.vc(5122), 30214), 10))}/${R.s(R.str(R.call(17012, []), 10))}`]), string3]);
                } else {
                  int44 = R.call(7235, [int2, int3, int4, int44, `Charges remaining: ${R.s(R.str(R.invVar(R.vc(5121), R.vc(5122), 20171), 10))}`, string3]);
                }
                break;
              }
              case 3411:
              {
                int17 = R.invVar(R.vc(5121), R.vc(5122), 6114);
                int44 = R.call(7235, [int2, int3, int4, int44, `Charges remaining: ${R.s(R.str(int17, 10))}`, string3]);
                break;
              }
              case 3140:
              {
                int17 = R.invVar(R.vc(5121), R.vc(5122), 20976);
                int34 = R.invVar(R.vc(5121), R.vc(5122), 20977);
                int44 = R.call(7235, [int2, int3, int4, int44, `Charges used: ${R.s(R.str(int17, 10))}/${R.s(R.str(int34, 10))}`, string3]);
                break;
              }
              case 3279:
              {
                int17 = R.invVar(R.vc(5121), R.vc(5122), 25008);
                int44 = R.call(7235, [int2, int3, int4, int44, `Charges remaining: ${R.s(R.str(((100) - (int17) | 0), 10))}`, string3]);
                break;
              }
              case 4058:
              {
                int17 = R.invVar(R.vc(5121), R.vc(5122), 34484);
                int44 = R.call(7235, [int2, int3, int4, int44, `Charges remaining: ${R.s(R.strLoc(int17, 1))}/${R.s(R.strLoc(R.enumValue(33, 0, 11952, int0), 1))}`, string3]);
                break;
              }
              case 4299:
              {
                int17 = R.scale(((((R.invVar(R.vc(5121), R.vc(5122), 40237)) + (R.invVar(R.vc(5121), R.vc(5122), 40238)) | 0)) + (R.invVar(R.vc(5121), R.vc(5122), 40239)) | 0), R.itemParam(int0, 4535), 100);
                int44 = R.call(7235, [int2, int3, int4, int44, `Bag filled: ${R.s(R.str(int17, 10))}%`, string3]);
                break;
              }
              case 4320:
              {
                int17 = R.min(R.max(R.scale(R.invVar(R.vc(5121), R.vc(5122), 40679), R.itemParam(int0, 7176), 100), 0), 99);
                int44 = R.call(7235, [int2, int3, int4, int44, `Jar filled: ${R.s(R.str(int17, 10))}%`, string3]);
                break;
              }
              case 4322:
              {
                int34 = R.itemParam(int0, 7176);
                if (R.eq(int0, 42907)) {
                  if (((((R.eq(R.invVar(R.vc(5121), R.vc(5122), 40680), int34) && R.eq(R.invVar(R.vc(5121), R.vc(5122), 40681), int34)) && R.eq(R.invVar(R.vc(5121), R.vc(5122), 40682), int34)) && R.eq(R.invVar(R.vc(5121), R.vc(5122), 40683), int34)) && R.eq(R.invVar(R.vc(5121), R.vc(5122), 40684), int34))) {
                    int44 = R.call(7235, [int2, int3, int4, int44, "Memories successfully recovered", string3]);
                  } else {
                    int44 = R.call(7235, [int2, int3, int4, int44, `Lustrous data recovered: ${R.s(R.str(R.invVar(R.vc(5121), R.vc(5122), 40680), 10))}/${R.s(R.str(int34, 10))}`, string3]);
                    int44 = R.call(7235, [int2, int3, int4, int44, `Brilliant data recovered: ${R.s(R.str(R.invVar(R.vc(5121), R.vc(5122), 40681), 10))}/${R.s(R.str(int34, 10))}`, string3]);
                    int44 = R.call(7235, [int2, int3, int4, int44, `Radiant data recovered: ${R.s(R.str(R.invVar(R.vc(5121), R.vc(5122), 40682), 10))}/${R.s(R.str(int34, 10))}`, string3]);
                    int44 = R.call(7235, [int2, int3, int4, int44, `Luminous data recovered: ${R.s(R.str(R.invVar(R.vc(5121), R.vc(5122), 40683), 10))}/${R.s(R.str(int34, 10))}`, string3]);
                    int44 = R.call(7235, [int2, int3, int4, int44, `Incandescent data recovered: ${R.s(R.str(R.invVar(R.vc(5121), R.vc(5122), 40684), 10))}/${R.s(R.str(int34, 10))}`, string3]);
                  }
                } else {
                  int17 = R.invVar(R.vc(5121), R.vc(5122), 40679);
                  if (R.eq(int17, int34)) {
                    int44 = R.call(7235, [int2, int3, int4, int44, "Memory successfully recovered", string3]);
                  } else {
                    int44 = R.call(7235, [int2, int3, int4, int44, `Data recovered: ${R.s(R.str(int17, 10))}/${R.s(R.str(int34, 10))}`, string3]);
                  }
                }
                break;
              }
              case 4359:
              {
                if ((R.eq(R.vc(5121), 90) && R.eq(R.vc(6492), 1))) {
                  int44 = R.call(7235, [int2, int3, int4, int44, `Name: ${R.s(R.call(1004, [int0]))}`, string3]);
                  if (R.eq(R.invOtherVar(R.vc(5121), R.vc(5122), 41803), 0)) {
                    int44 = R.call(7235, [int2, int3, int4, int44, "Gender: Male", string3]);
                  } else {
                    int44 = R.call(7235, [int2, int3, int4, int44, "Gender: Female", string3]);
                  }
                  if (R.eq(R.invOtherVar(R.vc(5121), R.vc(5122), 41805), 1)) {
                    int44 = R.call(7235, [int2, int3, int4, int44, "Shiny!", string3]);
                  }
                  int44 = R.call(7235, [int2, int3, int4, int44, `Growth stage: ${R.s(R.call(1005, [int0]))}`, string3]);
                  int44 = R.call(7235, [int2, int3, int4, int44, "", string3]);
                  int44 = R.call(7235, [int2, int3, int4, int44, R.call(1006, [int0]), string3]);
                  if ((R.invOtherVar(R.vc(5121), R.vc(5122), 41808) > 0)) {
                    int44 = R.call(7235, [int2, int3, int4, int44, R.call(1007, [int0]), string3]);
                  }
                  if ((R.invOtherVar(R.vc(5121), R.vc(5122), 41809) > 0)) {
                    int44 = R.call(7235, [int2, int3, int4, int44, R.call(1008, [int0]), string3]);
                  }
                } else {
                  int44 = R.call(7235, [int2, int3, int4, int44, `Name: ${R.s(R.call(999, [int0]))}`, string3]);
                  if (R.eq(R.invVar(R.vc(5121), R.vc(5122), 41803), 0)) {
                    int44 = R.call(7235, [int2, int3, int4, int44, "Gender: Male", string3]);
                  } else {
                    int44 = R.call(7235, [int2, int3, int4, int44, "Gender: Female", string3]);
                  }
                  if (R.eq(R.invVar(R.vc(5121), R.vc(5122), 41805), 1)) {
                    int44 = R.call(7235, [int2, int3, int4, int44, "Shiny!", string3]);
                  }
                  int44 = R.call(7235, [int2, int3, int4, int44, `Growth stage: ${R.s(R.call(1000, [int0]))}`, string3]);
                  int44 = R.call(7235, [int2, int3, int4, int44, "", string3]);
                  int44 = R.call(7235, [int2, int3, int4, int44, R.call(1001, [int0]), string3]);
                  if ((R.invVar(R.vc(5121), R.vc(5122), 41808) > 0)) {
                    int44 = R.call(7235, [int2, int3, int4, int44, R.call(1002, [int0]), string3]);
                  }
                  if ((R.invVar(R.vc(5121), R.vc(5122), 41809) > 0)) {
                    int44 = R.call(7235, [int2, int3, int4, int44, R.call(1003, [int0]), string3]);
                  }
                }
                break;
              }
              case 4431:
              {
                string12 = R.call(10184, []);
                int44 = R.call(7235, [int2, int3, int4, int44, string12, string3]);
                int34 = R.invVar(R.vc(5121), R.vc(5122), 30214);
                int44 = R.call(7236, [int34, int7, int2, int3, int4, int44, "Charges remaining:", ""]);
                break;
              }
              case 5155:
              {
                string12 = "Meals eaten:";
                int44 = R.call(7235, [int2, int3, int4, int44, string12, string3]);
                int42 = R.invVar(R.vc(5121), R.vc(5122), 4333);
                if (R.eq(int42, 0)) {
                  string12 = "None!";
                  int44 = R.call(7235, [int2, int3, int4, int44, string12, string3]);
                } else {
                  if (R.eq(R.testbit(int42, 0), 1)) {
                    string12 = "-Berry mush";
                    int44 = R.call(7235, [int2, int3, int4, int44, string12, string3]);
                  }
                  if (R.eq(R.testbit(int42, 1), 1)) {
                    string12 = "-Beany mush";
                    int44 = R.call(7235, [int2, int3, int4, int44, string12, string3]);
                  }
                  if (R.eq(R.testbit(int42, 2), 1)) {
                    string12 = "-Cerealy mush";
                    int44 = R.call(7235, [int2, int3, int4, int44, string12, string3]);
                  }
                  if (R.eq(R.testbit(int42, 3), 1)) {
                    string12 = "-Rooty mush";
                    int44 = R.call(7235, [int2, int3, int4, int44, string12, string3]);
                  }
                }
                break;
              }
              case 5235:
              {
                int17 = R.invVar(R.vc(5121), R.vc(5122), 18411);
                int44 = R.call(7235, [int2, int3, int4, int44, `Corruption stored: ${R.s(R.str(int17, 10))}`, string3]);
                break;
              }
              case 5292:
              {
                int44 = R.call(7235, [int2, int3, int4, int44, `Mode: ${R.s(R.enumValue(0, 36, 16911, R.invVar(R.vc(5121), R.vc(5122), 52915)))}`, string3]);
                int44 = R.call(7235, [int2, int3, int4, int44, `Charges: ${R.s(R.strLoc(R.invVar(R.vc(5121), R.vc(5122), 52916), 1))}`, string3]);
                break;
              }
              case 5315:
              {
                int44 = R.call(7235, [int2, int3, int4, int44, R.call(17493, [R.vc(5121), int0, R.vc(5122), 1]), string3]);
                break;
              }
              case 5445:
              {
                if (R.eq(R.invVar(R.vc(5121), R.vc(5122), 18550), 0)) {
                  string12 = "<col=FF0000>";
                } else {
                  string12 = "<col=FF00>";
                }
                int44 = R.call(7235, [int2, int3, int4, int44, `Charges remaining: ${R.s(string12)}${R.s(R.strLoc(R.invVar(R.vc(5121), R.vc(5122), 18550), 1))}`, string3]);
                break;
              }
              case 35:
              {
                string12 = R.str(R.enumValue(33, 0, 6560, int0), 10);
                if (((R.eq(R.itemParam(int0, 485), 1265) || R.eq(R.itemParam(int0, 485), 1300)) && (R.statBase(8) >= R.structParam(28972, 2212)))) {
                  string12 = R.str(((R.enumValue(33, 0, 6560, int0)) + (2) | 0), 10);
                }
                int44 = R.call(7235, [int2, int3, int4, int44, `Cutting power: Tier ${R.s(string12)}`, string3]);
                break;
              }
              default:
              {
                if (R.eq(R.itemParam(int0, 369), 4)) {
                  int17 = R.min(R.max(R.scale(R.invVar(R.vc(5121), R.vc(5122), 6139), R.itemParam(int0, 368), 100), 1), 99);
                  int44 = R.call(7235, [int2, int3, int4, int44, `${R.s(R.itemParam(int0, 6170))} filled: ${R.s(R.str(int17, 10))}%`, string3]);
                } else {
                  if (R.eq(R.itemParam(int0, 4563), 1)) {
                    int17 = R.call(670, [R.vc(5121), R.vc(5122), int0]);
                    if (R.eq(int17, 0)) {
                      int17 = 100;
                    } else {
                      int17 = ((100) - (R.idiv(int17, R.idiv(R.itemParam(int0, 3385), 100))) | 0);
                      if (R.eq(int17, 100)) {
                        int17 = 99;
                      }
                    }
                    string1 = R.str(int17, 10);
                    string2 = "<col=ffffff>";
                    if ((int17 <= 25)) {
                      string2 = "<col=ff0000>";
                    } else {
                      if ((int17 <= 65)) {
                        string2 = "<col=FFA500>";
                      } else {
                        string2 = "<col=00ff00>";
                      }
                    }
                    int44 = R.call(7235, [int2, int3, int4, int44, `Item Charge: ${R.s(string2)}${R.s(string1)}%`, string3]);
                  } else {
                    if (R.eq(R.itemParam(int0, 1324), 1)) {
                      if (R.eq(R.itemParam(int0, 5527), (-1 | 0))) {
                        if ((R.itemParam(int0, 3385) >= 1)) {
                          int17 = R.call(670, [R.vc(5121), R.vc(5122), int0]);
                          if (R.eq(R.itemParam(int0, 9308), 1)) {
                            int17 = R.max(0, ((R.itemParam(int0, 3385)) - (int17) | 0));
                            int44 = R.call(7235, [int2, int3, int4, int44, `Charges remaining: ${R.s(R.str(int17, 10))}`, string3]);
                          } else {
                            if (R.eq(int17, 0)) {
                              int17 = 1000;
                            } else {
                              int17 = ((1000) - (R.idiv(int17, R.idiv(R.itemParam(int0, 3385), 1000))) | 0);
                              if ((R.eq(int17, 0) && R.eq(R.itemParam(int0, 5772), 0))) {
                                int17 = 1;
                              }
                              if (R.eq(int17, 1000)) {
                                int17 = 999;
                              }
                            }
                            if (R.eq(int17, 0)) {
                              string1 = "0.0";
                            } else {
                              string1 = R.str(R.idiv(int17, 10), 10);
                              string1 = R.cat(string1, `.${R.s(R.str(R.mod(int17, 10), 10))}`);
                            }
                            string2 = "<col=ffffff>";
                            if ((int17 <= 250)) {
                              string2 = "<col=ff0000>";
                            } else {
                              if ((int17 <= 650)) {
                                string2 = "<col=FFA500>";
                              } else {
                                string2 = "<col=00ff00>";
                              }
                            }
                            int44 = R.call(7235, [int2, int3, int4, int44, `Item Charge: ${R.s(string2)}${R.s(string1)}%`, string3]);
                          }
                        } else {
                          if (R.eq(R.itemParam(int0, 6880), 0)) {
                            if (((!R.eq(R.itemParam(int0, 5777), (-1 | 0))) && (!R.eq(R.itemParam(int0, 5778), (-1 | 0))))) {
                              if (R.eq(R.itemWearpos(int0), 3)) {
                                int17 = R.scale(R.invVar(R.vc(5121), R.vc(5122), 507), 3000, 1000);
                              } else {
                                int17 = R.scale(R.invVar(R.vc(5121), R.vc(5122), 507), 35000, 1000);
                              }
                            } else {
                              if (R.eq(R.itemParam(int0, 3112), 1)) {
                                int17 = R.min(1000, R.scale(R.invVar(R.vc(5121), R.vc(5122), 17233), R.idiv(R.itemParam(int0, 3109), 10), 100));
                              } else {
                                if (R.eq(int0, 24338)) {
                                  int17 = ((1000) - (R.scale(R.invVar(R.vc(5121), R.vc(5122), 16238), 6000, 100)) | 0);
                                } else {
                                  if (R.eq(R.itemCategory(int0), 3847)) {
                                    int17 = R.scale(R.invVar(R.vc(5121), R.vc(5122), 31193), R.itemParam(int0, 5722), 1000);
                                  } else {
                                    int17 = ((1000) - (R.scale(R.invVar(R.vc(5121), R.vc(5122), 15190), 6000, 100)) | 0);
                                  }
                                }
                              }
                            }
                            if (R.eq(int17, 0)) {
                              string1 = "0";
                            } else {
                              string1 = R.str(R.idiv(int17, 10), 10);
                              string1 = R.cat(string1, `.${R.s(R.str(R.mod(int17, 10), 10))}`);
                            }
                            if ((int17 <= 250)) {
                              string2 = "<col=ff0000>";
                            } else {
                              if ((int17 <= 650)) {
                                string2 = "<col=FFA500>";
                              } else {
                                string2 = "<col=00ff00>";
                              }
                            }
                            int44 = R.call(7235, [int2, int3, int4, int44, `Item Charge: ${R.s(string2)}${R.s(string1)}%`, string3]);
                          }
                        }
                      } else {
                        if ((R.itemParam(int0, 1079) >= 1)) {
                          int17 = R.invVar(R.vc(5121), R.vc(5122), 2156);
                          int17 = ((1000) - (R.scale(int17, 6000, 100)) | 0);
                          if (R.eq(int17, 0)) {
                            int17 = 1;
                          }
                          string1 = R.str(R.idiv(int17, 10), 10);
                          string1 = R.cat(string1, `.${R.s(R.str(R.mod(int17, 10), 10))}`);
                          string2 = "<col=ffffff>";
                          if ((int17 <= 250)) {
                            string2 = "<col=ff0000>";
                          } else {
                            if ((int17 <= 650)) {
                              string2 = "<col=FFA500>";
                            } else {
                              string2 = "<col=00ff00>";
                            }
                          }
                          int44 = R.call(7235, [int2, int3, int4, int44, `Item Charge: ${R.s(string2)}${R.s(string1)}%`, string3]);
                        }
                      }
                    } else {
                      if ((R.itemParam(int0, 6834) >= 1)) {
                        int17 = R.min(R.max(R.invVar(R.vc(5121), R.vc(5122), 17713), 1), 60000);
                        int34 = R.scale(((60000) - (int17) | 0), 60000, 1000);
                        string2 = "<col=ffffff>";
                        if ((int34 <= 250)) {
                          string2 = "<col=ff0000>";
                        } else {
                          if ((int34 <= 650)) {
                            string2 = "<col=FFA500>";
                          } else {
                            string2 = "<col=00ff00>";
                          }
                        }
                        int44 = R.call(7235, [int2, int3, int4, int44, `Item Charge: ${R.s(string2)}${R.s(R.str(R.idiv(int34, 10), 10))}${R.s(R.call(8002, []))}${R.s(R.str(R.mod(int34, 10), 10))}%`, string3]);
                      }
                    }
                  }
                }
                break;
              }
            }
            break;
          }
        }
      } else {
        switch (R.key(int0)) {
          case 44155:
          {
            if (((R.vp(3079) >= R.structParam(40722, 7487)) && (R.vp(3079) <= R.structParam(40722, 7489)))) {
              int44 = R.call(7235, [int2, int3, int4, int44, `Charges: ${R.s(R.strLoc(R.vb(42023), 1))}/${R.s(R.strLoc(4000, 1))}<br>Provides a 2% XP boost while equipped or in your inventory.`, string3]);
            }
            break;
          }
          case 44210:
          {
            int44 = R.call(7235, [int2, int3, int4, int44, `Adrenaline stored: ${R.s(R.strLoc(R.idiv(R.vb(42160), 10), 1))}%`, string3]);
            break;
          }
          case 31846:
          {
            string1 = R.enumValue(0, 36, 9197, R.vb(22901));
            int44 = R.call(7235, [int2, int3, int4, int44, `Reaper assignment: ${R.s(string1)}`, string3]);
            if ((R.vb(22902) >= 1)) {
              int17 = R.call(7235, [int2, int3, int4, int44, `Kills remaining: ${R.s(R.strLoc(R.vb(22902), 1))}`, string3]);
            }
            break;
          }
          case 4155:
          {
            string1 = R.enumString(1563, R.vp(185));
            int44 = R.call(7235, [int2, int3, int4, int44, `Slayer assignment: ${R.s(string1)}`, string3]);
            if ((R.vp(183) >= 1)) {
              int17 = R.call(7235, [int2, int3, int4, int44, `Kills remaining: ${R.s(R.strLoc(R.vp(183), 1))}`, string3]);
            }
            break;
          }
          case 41092:
          {
            if ((R.vb(37578) >= 1)) {
              int44 = R.call(7235, [int2, int3, int4, int44, `Siphons stored: ${R.s(R.strLoc(R.vb(37578), 1))}`, string3]);
            }
            if ((R.vb(37579) >= 1)) {
              int44 = R.call(7235, [int2, int3, int4, int44, `Crystal Siphons stored: ${R.s(R.strLoc(R.vb(37579), 1))}`, string3]);
            }
            break;
          }
          case 5521:
          {
            int44 = R.call(7235, [int2, int3, int4, int44, `Charges available: ${R.s(R.str(R.vp(3216), 10))}`, string1]);
            break;
          }
          case 43358:
          {
            int44 = R.call(7235, [int2, int3, int4, int44, "Call upon elvish dark magic to restore 37% of your total prayer points over 30 seconds. 5 Minute cooldown between each use.", string3]);
            break;
          }
          case 28015:
          {
            int44 = R.call(7235, [int2, int3, int4, int44, `Charges remaining: ${R.s(R.str(100, 10))}`, string3]);
            break;
          }
          case 24135:
          case 24136:
          {
            int44 = R.call(7235, [int2, int3, int4, int44, `Charges remaining: ${R.s(R.str(((R.itemParam(int0, 7706)) - (R.vb(15705)) | 0), 10))}`, string3]);
            break;
          }
          case 51276:
          {
            int44 = R.call(7235, [int2, int3, int4, int44, `Charges remaining: ${R.s(R.strLoc(60, 1))}`, string3]);
            break;
          }
          case 51505:
          {
            int44 = R.call(7235, [int2, int3, int4, int44, `Charges remaining: ${R.s(R.str(R.vb(49361), 10))}%`, string3]);
            break;
          }
          case 52169:
          case 52171:
          case 52172:
          case 51791:
          {
            if (R.eq(R.vb(49720), 1)) {
              int44 = R.call(7235, [int2, int3, int4, int44, `Damage: <col=FF00>${R.s(R.str(200, 10))}%</col>`, string3]);
            } else {
              int44 = R.call(7235, [int2, int3, int4, int44, "Damage: <col=FF00>100%</col>", string3]);
            }
            break;
          }
          case 37400:
          {
            if (R.eq(R.vb(49721), 1)) {
              int44 = R.call(7235, [int2, int3, int4, int44, `Damage: <col=FF00>${R.s(R.str(150, 10))}%</col>`, string3]);
            } else {
              int44 = R.call(7235, [int2, int3, int4, int44, "Damage: <col=FF00>100%</col>", string3]);
            }
            break;
          }
          case 37408:
          {
            if (R.eq(R.vb(49722), 1)) {
              int44 = R.call(7235, [int2, int3, int4, int44, `Damage: <col=FF00>${R.s(R.str(150, 10))}%</col>`, string3]);
            } else {
              int44 = R.call(7235, [int2, int3, int4, int44, "Damage: <col=FF00>100%</col>", string3]);
            }
            break;
          }
          case 14632:
          {
            if (R.eq(R.vb(15960), 1)) {
              int44 = R.call(7235, [int2, int3, int4, int44, "On activation: heals 4% of your maximum life points 10 times over 40 seconds. 5 minute cooldown.", string3]);
            } else {
              int44 = R.call(7235, [int2, int3, int4, int44, "On activation: heals 4% of your maximum life points 5 times over 20 seconds. 5 minute cooldown.", string3]);
            }
            break;
          }
          case 2572:
          case 20653:
          case 20655:
          case 20657:
          case 20659:
          case 41069:
          {
            string12 = R.call(10184, []);
            int44 = R.call(7235, [int2, int3, int4, int44, string12, string3]);
            break;
          }
          case 39808:
          case 44558:
          {
            string12 = "Provides unlimited teleports to the Varrock Grand Exchange";
            if ((vp2236_q_throne_of_miscellania_progress >= 100)) {
              string12 = `${R.s(string12)} and Miscellania`;
            }
            int44 = R.call(7235, [int2, int3, int4, int44, `${R.s(string12)}.`, string3]);
            break;
          }
          case 39812:
          case 44559:
          {
            string12 = "Provides unlimited teleports to the Varrock Grand Exchange";
            if ((vp2236_q_throne_of_miscellania_progress >= 100)) {
              if ((R.vb(13266) >= 5)) {
                string12 = `${R.s(string12)}, Miscellania`;
              } else {
                string12 = `${R.s(string12)} and Miscellania`;
              }
            }
            if ((R.vb(13266) >= 5)) {
              string12 = `${R.s(string12)} and Keldagrim`;
            }
            int44 = R.call(7235, [int2, int3, int4, int44, `${R.s(string12)}.`, string3]);
            break;
          }
          case 39814:
          case 44560:
          {
            string12 = "Provides unlimited teleports to the Varrock Grand Exchange";
            if ((vp2236_q_throne_of_miscellania_progress >= 100)) {
              string12 = `${R.s(string12)}, Miscellania`;
            }
            if ((R.vb(13266) >= 5)) {
              string12 = `${R.s(string12)}, Keldagrim`;
            }
            string12 = `${R.s(string12)} and the Tree Gnome Stronghold.`;
            int44 = R.call(7235, [int2, int3, int4, int44, string12, string3]);
            break;
          }
          case 59216:
          {
            if ((R.vb(58116) < 1)) {
              string12 = `Damage remaining: ${R.s(R.strLoc(10000, 1))}`;
            } else {
              string12 = `Damage remaining: ${R.s(R.strLoc(R.vb(58116), 1))}`;
            }
            int44 = R.call(7235, [int2, int3, int4, int44, string12, string3]);
            break;
          }
          case 59218:
          {
            string12 = `Damage remaining: ${R.s(R.strLoc(R.vb(58117), 1))}`;
            int44 = R.call(7235, [int2, int3, int4, int44, string12, string3]);
            break;
          }
          case 59221:
          {
            string12 = `Charges remaining: ${R.s(R.strLoc(50, 1))}`;
            int44 = R.call(7235, [int2, int3, int4, int44, string12, string3]);
            break;
          }
          case 59226:
          {
            string12 = `Charges remaining: ${R.s(R.strLoc(10, 1))}`;
            int44 = R.call(7235, [int2, int3, int4, int44, string12, string3]);
            break;
          }
          case 59233:
          {
            string12 = `Charges remaining: ${R.s(R.strLoc(250, 1))}`;
            int44 = R.call(7235, [int2, int3, int4, int44, string12, string3]);
            break;
          }
          case 59238:
          {
            string12 = `Charges remaining: ${R.s(R.strLoc(10, 1))}`;
            int44 = R.call(7235, [int2, int3, int4, int44, string12, string3]);
            break;
          }
          case 53754:
          {
            string12 = "A box that can be activated once a day, allowing you to deposit items from your backpack to your bank for one hour.";
            if ((R.vb(51871) > 0)) {
              string12 = `${R.s(string12)}<br>Time remaining: ${R.s(R.str(R.max(1, R.idiv(R.vb(51871), 4)), 10))} ${R.s(R.call(4583, [R.max(1, R.idiv(R.vb(51871), 4)), "minute", "minutes"]))}`;
            } else {
              if (R.eq(R.vb(51808), 1)) {
                string12 = `${R.s(string12)}<br>You can activate this again tomorrow.`;
              }
            }
            int44 = R.call(7235, [int2, int3, int4, int44, string12, string3]);
            break;
          }
          case 56169:
          {
            int44 = R.call(7235, [int2, int3, int4, int44, `Progress: ${R.s(R.str(R.vb(54795), 10))} / ${R.s(R.str(50, 10))}`, string3]);
            break;
          }
          case 56552:
          {
            int44 = R.call(7235, [int2, int3, int4, int44, `Progress: ${R.s(R.str(R.vb(55172), 10))} / ${R.s(R.str(50, 10))}`, string3]);
            break;
          }
          case 56630:
          {
            int44 = R.call(7235, [int2, int3, int4, int44, `Charges: ${R.s(R.str(R.vb(55320), 10))}`, string3]);
            break;
          }
          case 57064:
          {
            int44 = R.call(7235, [int2, int3, int4, int44, R.call(18986, []), string3]);
            break;
          }
          case 57517:
          {
            int42 = 2500;
            if (R.eq(R.vp(11842), 1)) {
              int42 = 3000;
            }
            int43 = Math.imul(((R.invTotal(93, 57517)) + (R.invTotal(530, 57517)) | 0), int42);
            int44 = R.call(7235, [int2, int3, int4, int44, `- Striking the Gate of Elidinis cleanses <col=00FFFF>${R.s(R.strLoc(int42, 1))}</col> from her corruption<br>- Striking during <col=ffffff>Icthlarin's aid</col> will use all shards at once, increasing the corruption cleansed by <col=ffffff>5-25%</col>, as well as reducing <col=ffffff>Corruption</col> by <col=ffffff>${R.s(R.str(2, 10))}</col> per piece.<br>- Pieces held cleanses a total of <col=00FFFF>${R.s(R.strLoc(int43, 1))}</col>.`, string3]);
            int44 = R.call(7235, [int2, int3, int4, int44, `- With Icthlarin's Aid speed 4: <col=00FFFF>${R.s(R.strLoc(R.scale(int43, 100, ((100) + (5) | 0)), 1))}</col>.`, string3]);
            int44 = R.call(7235, [int2, int3, int4, int44, `- With Icthlarin's Aid speed 3: <col=00FFFF>${R.s(R.strLoc(R.scale(int43, 100, ((100) + (10) | 0)), 1))}</col>.`, string3]);
            int44 = R.call(7235, [int2, int3, int4, int44, `- With Icthlarin's Aid speed 2: <col=00FFFF>${R.s(R.strLoc(R.scale(int43, 100, ((100) + (15) | 0)), 1))}</col>.`, string3]);
            int44 = R.call(7235, [int2, int3, int4, int44, `- With Icthlarin's Aid speed 1: <col=00FFFF>${R.s(R.strLoc(R.scale(int43, 100, ((100) + (25) | 0)), 1))}</col>.`, string3]);
            break;
          }
          case 58256:
          {
            switch (R.key(R.vb(56949))) {
              case 0:
              {
                string12 = "not attuned";
                break;
              }
              case 1:
              {
                string12 = "attuned to attract nests with seeds";
                break;
              }
              case 2:
              {
                string12 = "attuned to attract nests with rings";
                break;
              }
              case 3:
              {
                string12 = "attuned to attract nests with eggs";
                break;
              }
            }
            int44 = R.call(7235, [int2, int3, int4, int44, `Your bird whistle is ${R.s(string12)}.`, string3]);
            break;
          }
          case 58324:
          {
            if ((R.vb(57039) < 200)) {
              int44 = R.call(7235, [int2, int3, int4, int44, `Elite profane scabarites killed: ${R.s(R.str(R.vb(57039), 10))} / ${R.s(R.str(200, 10))}`, string3]);
            }
            break;
          }
          case 59952:
          {
            string12 = `Loot Stored: ${R.s(R.strLoc(R.vb(60144), 1))}`;
            int44 = R.call(7235, [int2, int3, int4, int44, string12, string3]);
            break;
          }
          case 63583:
          {
            int44 = R.call(21105, [int0, R.vc(5121), R.vc(5122), int2, int3, int4, int44, string3]);
            break;
          }
          case 63589:
          {
            if ((!R.eq(R.vp(13505), (-1 | 0)))) {
              int44 = R.call(7235, [int2, int3, int4, int44, `Transmuting: ${R.s(R.itemName(R.vp(13505)))}`, string3]);
            } else {
              int44 = R.call(7235, [int2, int3, int4, int44, "Transmuting: Nothing", string3]);
            }
            break;
          }
          default:
          {
            switch (R.key(R.itemCategory(int0))) {
              case 5383:
              {
                if (R.eq(R.call(19680, []), 1)) {
                  int44 = R.call(7235, [int2, int3, int4, int44, "Collect all 6 and combine them to unlock the ability to recolour each colour zone.", string3]);
                  if (R.eq(R.vb(54893), 1)) {
                    string4 = "<col=FF00>";
                  } else {
                    string4 = "<col=777777>";
                  }
                  int44 = R.call(7235, [int2, int3, int4, int44, `Winter scarf 1: ${R.s(string4)}Santa's Lodge`, string3]);
                  if (R.eq(R.vb(54894), 1)) {
                    string4 = "<col=FF00>";
                  } else {
                    string4 = "<col=777777>";
                  }
                  int44 = R.call(7235, [int2, int3, int4, int44, `Winter scarf 2: ${R.s(string4)}Santa's Lodge`, string3]);
                  if (R.eq(R.vb(54895), 1)) {
                    string4 = "<col=FF00>";
                  } else {
                    string4 = "<col=777777>";
                  }
                  int44 = R.call(7235, [int2, int3, int4, int44, `Winter scarf 3: ${R.s(string4)}Holly's Christmas Spirit Shop`, string3]);
                  if (R.eq(R.vb(54896), 1)) {
                    string4 = "<col=FF00>";
                  } else {
                    string4 = "<col=777777>";
                  }
                  int44 = R.call(7235, [int2, int3, int4, int44, `Winter scarf 4: ${R.s(string4)}Holly's Christmas Spirit Shop`, string3]);
                  if (R.eq(R.vb(54897), 1)) {
                    string4 = "<col=FF00>";
                  } else {
                    string4 = "<col=777777>";
                  }
                  int44 = R.call(7235, [int2, int3, int4, int44, `Winter scarf 5: ${R.s(string4)}A Christmas Reunion quest`, string3]);
                  if (R.eq(R.vb(54898), 1)) {
                    string4 = "<col=FF00>";
                  } else {
                    string4 = "<col=777777>";
                  }
                  int44 = R.call(7235, [int2, int3, int4, int44, `Winter scarf 6: ${R.s(string4)}It's Snow Bother quest`, string3]);
                }
                break;
              }
              case 5382:
              {
                if (R.eq(R.call(19680, []), 1)) {
                  int44 = R.call(7235, [int2, int3, int4, int44, "Collect all 6 and combine them to unlock the ability to recolour each colour zone.", string3]);
                  if (R.eq(R.vb(54899), 1)) {
                    string4 = "<col=FF00>";
                  } else {
                    string4 = "<col=777777>";
                  }
                  int44 = R.call(7235, [int2, int3, int4, int44, `Winter hat 1: ${R.s(string4)}Santa's Lodge`, string3]);
                  if (R.eq(R.vb(54900), 1)) {
                    string4 = "<col=FF00>";
                  } else {
                    string4 = "<col=777777>";
                  }
                  int44 = R.call(7235, [int2, int3, int4, int44, `Winter hat 2: ${R.s(string4)}Santa's Lodge`, string3]);
                  if (R.eq(R.vb(54901), 1)) {
                    string4 = "<col=FF00>";
                  } else {
                    string4 = "<col=777777>";
                  }
                  int44 = R.call(7235, [int2, int3, int4, int44, `Winter hat 3: ${R.s(string4)}Holly's Christmas Spirit Shop`, string3]);
                  if (R.eq(R.vb(54902), 1)) {
                    string4 = "<col=FF00>";
                  } else {
                    string4 = "<col=777777>";
                  }
                  int44 = R.call(7235, [int2, int3, int4, int44, `Winter hat 4: ${R.s(string4)}Holly's Christmas Spirit Shop`, string3]);
                  if (R.eq(R.vb(54903), 1)) {
                    string4 = "<col=FF00>";
                  } else {
                    string4 = "<col=777777>";
                  }
                  int44 = R.call(7235, [int2, int3, int4, int44, `Winter hat 5: ${R.s(string4)}A Christmas Reunion quest`, string3]);
                  if (R.eq(R.vb(54904), 1)) {
                    string4 = "<col=FF00>";
                  } else {
                    string4 = "<col=777777>";
                  }
                  int44 = R.call(7235, [int2, int3, int4, int44, `Winter hat 6: ${R.s(string4)}It's Snow Bother quest`, string3]);
                }
                break;
              }
              case 4222:
              {
                switch (R.key(int0)) {
                  case 5509:
                  {
                    int34 = 3;
                    int11 = 0;
                    int25 = 0;
                    break;
                  }
                  case 5510:
                  case 5511:
                  {
                    int34 = R.call(1569, []);
                    int11 = R.vp(3217);
                    int25 = 800;
                    break;
                  }
                  case 5512:
                  case 5513:
                  {
                    int34 = R.call(1764, []);
                    int11 = R.vp(3218);
                    int25 = 1000;
                    break;
                  }
                  case 5514:
                  case 5515:
                  {
                    int34 = R.call(2258, []);
                    int11 = R.vp(3219);
                    int25 = 1200;
                    break;
                  }
                  case 58451:
                  {
                    int34 = R.call(8755, [58451]);
                    break;
                  }
                  case 24205:
                  case 24204:
                  {
                    int34 = (-1 | 0);
                    break;
                  }
                }
                if ((!R.eq(int34, (-1 | 0)))) {
                  if ((int34 > 0)) {
                    int44 = R.call(7235, [int2, int3, int4, int44, `Capacity: ${R.s(R.str(int34, 10))}`, string3]);
                    if ((int25 > 0)) {
                      int11 = R.scale(((int25) - (int11) | 0), int25, 100);
                      if (R.eq(R.call(14608, [2896]), 1)) {
                        int44 = R.call(7235, [int2, int3, int4, int44, "Durability: <col=00ff00>100% - Relic active</col>", string3]);
                      } else {
                        int44 = R.call(7235, [int2, int3, int4, int44, `Durability: ${R.s(R.call(4229, [int11, 20, 80, string3]))}${R.s(R.str(int11, 10))}%`, string3]);
                      }
                    }
                  } else {
                    if ((int34 > 0)) {
                      int44 = R.call(7235, [int2, int3, int4, int44, `Capacity: ${R.s(R.str(int34, 10))}`, string3]);
                    }
                  }
                }
                int11 = 0;
                break;
              }
              case 3567:
              {
                string2 = "<col=ffffff>";
                if ((R.vb(27394) <= 50000)) {
                  string2 = "<col=ff0000>";
                } else {
                  if ((R.vb(27394) <= 100000)) {
                    string2 = "<col=FFA500>";
                  } else {
                    string2 = "<col=00ff00>";
                  }
                }
                int44 = R.call(7235, [int2, int3, int4, int44, `XP remaining: ${R.s(string2)}${R.s(R.strLoc(R.idiv(R.vb(27394), 10), 1))}`, string3]);
                break;
              }
              case 4564:
              {
                string2 = "<col=ffffff>";
                if ((R.vb(45288) <= R.idiv(R.vb(45291), 4))) {
                  string2 = "<col=ff0000>";
                } else {
                  if ((R.vb(45288) <= R.idiv(R.vb(45291), 2))) {
                    string2 = "<col=FFA500>";
                  } else {
                    string2 = "<col=00ff00>";
                  }
                }
                int44 = R.call(7235, [int2, int3, int4, int44, `Bonus XP burn remaining: ${R.s(string2)}${R.s(R.strLoc(R.idiv(R.vb(45288), 10), 1))}`, string3]);
                break;
              }
              case 4058:
              {
                int17 = R.enumValue(33, 0, 11952, int0);
                int44 = R.call(7235, [int2, int3, int4, int44, `Charges remaining: ${R.s(R.strLoc(int17, 1))}/${R.s(R.strLoc(int17, 1))}`, string3]);
                break;
              }
              case 2804:
              {
                int17 = R.itemParam(int0, 3109);
                [int11, int12, int14] = R.call(4705, [int17]);
                int44 = R.call(7235, [int2, int3, int4, int44, `${R.s(R.call(5729, [int11, int12, int14, 0, 0]))} remaining`, string3]);
                break;
              }
              case 4452:
              {
                if (R.eq(R.itemParam(int0, 7804), 1)) {
                  int44 = R.call(7235, [int2, int3, int4, int44, `0/${R.s(R.str(R.itemParam(R.itemParam(int0, 7807), 2665), 10))} rivets applied.`, string3]);
                }
                break;
              }
              case 4480:
              {
                int44 = R.call(7235, [int2, int3, int4, int44, R.call(6345, [int0]), string3]);
                break;
              }
              case 3573:
              {
                int44 = R.call(7235, [int2, int3, int4, int44, R.call(10081, [int0]), string3]);
                break;
              }
              case 3787:
              case 3789:
              case 3788:
              case 3790:
              case 3791:
              case 3792:
              {
                int44 = R.call(7235, [int2, int3, int4, int44, R.call(519, [R.itemCategory(int0)]), string3]);
                break;
              }
              case 3017:
              case 3018:
              case 3015:
              {
                int44 = R.call(7235, [int2, int3, int4, int44, "Is automatically consumed when you are damaged to under half your life points.", string3]);
                break;
              }
              case 3025:
              case 3023:
              {
                int44 = R.call(7235, [int2, int3, int4, int44, "Is automatically consumed on death.", string3]);
                break;
              }
              case 3024:
              case 3022:
              {
                int44 = R.call(7235, [int2, int3, int4, int44, "Is automatically consumed on death if equipped.", string3]);
                break;
              }
              case 3019:
              {
                int44 = R.call(7235, [int2, int3, int4, int44, "Is automatically consumed on death if equipped, or if you equip it while you have an active gravestone.", string3]);
                break;
              }
              case 35:
              {
                string12 = R.str(R.enumValue(33, 0, 6560, int0), 10);
                if (((R.eq(R.itemParam(int0, 485), 1265) || R.eq(R.itemParam(int0, 485), 1300)) && (R.statBase(8) >= R.structParam(28972, 2212)))) {
                  string12 = R.str(((R.enumValue(33, 0, 6560, int0)) + (2) | 0), 10);
                }
                int44 = R.call(7235, [int2, int3, int4, int44, `Cutting power: Tier ${R.s(string12)}`, string3]);
                break;
              }
              default:
              {
                if (R.eq(R.itemParam(int0, 3203), 39402)) {
                  int17 = ((3) + (R.call(3862, [])) | 0);
                  switch (R.key(int0)) {
                    case 42111:
                    case 42112:
                    case 42113:
                    case 42114:
                    {
                      int18 = ((5) - (R.call(3862, [])) | 0);
                      if (R.eq(R.call(20152, []), 0)) {
                        int44 = R.call(7235, [int2, int3, int4, int44, `Charges available: ${R.s(R.str(R.vb(39468), 10))}/${R.s(R.str(int17, 10))}`, string1]);
                        int44 = R.call(7235, [int2, int3, int4, int44, `Next charge progress: ${R.s(R.str(R.vb(39465), 10))}/${R.s(R.str(int18, 10))}`, string1]);
                      } else {
                        int44 = R.call(7235, [int2, int3, int4, int44, "Charges available: Unlimited!", string1]);
                      }
                      break;
                    }
                    case 42119:
                    case 42120:
                    case 42121:
                    case 42122:
                    {
                      int18 = ((5) - (R.call(3862, [])) | 0);
                      if (R.eq(R.call(20152, []), 0)) {
                        int44 = R.call(7235, [int2, int3, int4, int44, `Charges available: ${R.s(R.str(R.vb(39469), 10))}/${R.s(R.str(int17, 10))}`, string1]);
                        int44 = R.call(7235, [int2, int3, int4, int44, `Next charge progress: ${R.s(R.str(R.vb(39466), 10))}/${R.s(R.str(int18, 10))}`, string1]);
                      } else {
                        int44 = R.call(7235, [int2, int3, int4, int44, "Charges available: Unlimited!", string1]);
                      }
                      break;
                    }
                    case 42107:
                    case 42108:
                    case 42109:
                    case 42110:
                    {
                      int18 = ((2) - (R.call(3862, [])) | 0);
                      if (R.eq(R.call(20152, []), 0)) {
                        int44 = R.call(7235, [int2, int3, int4, int44, `Charges available: ${R.s(R.str(R.vb(39470), 10))}/${R.s(R.str(int17, 10))}`, string1]);
                        int44 = R.call(7235, [int2, int3, int4, int44, `Next charge progress: ${R.s(R.str(R.vb(39467), 10))}/${R.s(R.str(int18, 10))}`, string1]);
                      } else {
                        int44 = R.call(7235, [int2, int3, int4, int44, "Charges available: Unlimited!", string1]);
                      }
                      break;
                    }
                  }
                }
                break;
              }
            }
            break;
          }
        }
      }
    }
    switch (R.key(int0)) {
      case 59358:
      {
        if ((!R.eq(R.itemParam(int0, 8928), (-1 | 0)))) {
          int44 = R.call(7235, [int2, int3, int4, int44, `Passive: ${R.s(R.structParam(R.itemParam(int0, 8928), 2794))}<br>${R.s(R.call(17663, [int0]))}`, string3]);
        }
        break;
      }
    }
    if ((R.eq(R.itemParam(int0, 8695), 1) && R.eq(R.itemHasVarobj(int0), 1))) {
      int17 = R.invVar(R.vc(5121), R.vc(5122), 51776);
      int16 = R.invVar(R.vc(5121), R.vc(5122), 51777);
      if ((R.eq(int17, 1) || (!R.eq(int16, 1)))) {
        int29 = 1;
        int44 = R.call(7235, [int2, int3, int4, int44, "<col=ff0000>May no longer be traded.", string3]);
      } else {
        if ((R.call(12477, []) <= R.itemParam(int0, 8697))) {
          int29 = 0;
          int44 = R.call(7235, [int2, int3, int4, int44, "<col=ff0000>May only be traded once.", string3]);
        }
      }
    }
    if ((R.eq(int29, 0) && (!R.eq(R.itemParam(int0, 8697), (-1 | 0))))) {
      int17 = R.itemParam(int0, 8697);
      if ((R.call(12477, []) <= int17)) {
        string12 = R.call(3381, [int17]);
        int44 = R.call(7235, [int2, int3, int4, int44, `<col=ff0000>May only be traded until ${R.s(string12)}.`, string3]);
      } else {
        int29 = 1;
        int44 = R.call(7235, [int2, int3, int4, int44, "<col=ff0000>May no longer be traded.", string3]);
      }
    }
    if (((R.eq(int29, 0) && R.eq(R.itemParam(int0, 8696), 1)) && R.eq(R.call(16823, []), 1))) {
      int44 = R.call(7235, [int2, int3, int4, int44, "<col=ff0000>May only be traded outside of Fresh Start Worlds.", string3]);
    }
    if ((!R.eq(R.strcmp(R.itemParam(int0, 3200), ""), 0))) {
      string9 = R.call(8240, [(-1 | 0), int0]);
      if ((R.eq(R.call(4148, []), 1) && (!R.eq(R.strcmp(R.itemParam(int0, 8572), ""), 0)))) {
        int44 = R.call(7235, [int2, int3, int4, int44, `${R.s(string9)}${R.s(R.itemParam(int0, 8572))}`, string3]);
      } else {
        int44 = R.call(7235, [int2, int3, int4, int44, `${R.s(string9)}${R.s(R.itemParam(int0, 3200))}`, string3]);
      }
    }
    if ((!R.eq(R.itemParam(int0, 8839), 0))) {
      int44 = R.call(7235, [int2, int3, int4, int44, `Time until expiry: <col=00ffff>${R.s(R.call(3354, [R.itemParam(int0, 8839), 1, "Expired!"]))} `, string3]);
    }
    if (R.eq(int6, 819)) {
      if ((R.len(R.itemParam(int0, 4085)) > 0)) {
        int44 = R.call(7235, [int2, int3, int4, int44, R.itemParam(int0, 4085), string3]);
      }
      if ((R.len(R.itemParam(int0, 2231)) > 0)) {
        int44 = R.call(7235, [int2, int3, int4, int44, R.itemParam(int0, 2231), string3]);
      }
    }
    if ((!R.eq(R.strcmp(R.vcs(4645), ""), 0))) {
      string7 = "<col=00ff00>";
      int44 = R.call(7235, [int2, int3, int4, int44, R.vcs(4645), string7]);
    }
    if (((!R.eq(int5, (-1 | 0))) && (!R.eq(int5, int0)))) {
      if ((!R.eq(int0, (-1 | 0)))) {
        int44 = R.call(7235, [int2, int3, int4, int44, " ", string1]);
      }
      int44 = R.call(10761, [int5, int6, int1, int2, int3, int4, int44]);
    }
    if ((R.eq(R.itemParam(int0, 3824), 1) && (R.len(R.itemDesc(int0)) > 0))) {
      int44 = R.call(7235, [int2, int3, int4, int44, R.itemDesc(int0), string3]);
    }
    if (((!R.eq(int45, (-1 | 0))) && R.eq(int44, int45))) {
      int44 = ((int44) - (1) | 0);
      if (R.eq(R.ifNone(R.ifNeg(int4), ((R.ifZero(R.ifNeg(int4))) - (1) | 0)), 1)) {
        R.ifNoop();
      }
    }
    return int44;
  };
  V[5839] = { vb: [44135], vp: [], vc: [], calls: [] };
  S[5839] = function () {
    if (R.eq(R.vb(44135), 1)) {
      return 1;
    }
    return 0;
  };
  V[6345] = { vb: [], vp: [], vc: [], calls: [] };
  S[6345] = function (int0) {
    switch (R.key(int0)) {
      case 47699:
      {
        return "Guam Incense: Each potency level provides a <col=ffffff>10%</col> chance to gain an additional log when cutting normal trees.";
      }
      case 47700:
      {
        return "Tarromin Incense: Each potency level provides a <col=ffffff>25%</col> chance to automatically bank ashes when burning logs in new fires.";
      }
      case 47701:
      {
        return "Marrentill Incense: Each potency level provides a <col=ffffff>12.5%</col> reduction to poison damage.";
      }
      case 47702:
      {
        return "Harralander Incense: Each potency level provides a <col=ffffff>25%</col> increase to run energy restore rate while resting.";
      }
      case 47703:
      {
        return "Ranarr Incense: Each potency level provides a <col=ffffff>+1</col> Prayer bonus.";
      }
      case 47704:
      {
        return "Toadflax Incense: Each potency level provides a <col=ffffff>12.5%</col> chance to avoid failing an agility obstacle.";
      }
      case 47705:
      {
        return "Spirit weed Incense: Each potency level provides a <col=ffffff>10%</col> increase to a familiar's summoning spell points recovery rate.";
      }
      case 47706:
      {
        return "Irit Incense: Each potency level provides a <col=ffffff>25%</col> reduction to poison damage.";
      }
      case 47707:
      {
        return "Wergali Incense: Each potency level provides a <col=ffffff>0.5%</col> chance while fletching to automatically string a bow or add feathers to arrow shafts.";
      }
      case 47708:
      {
        return "Avantoe Incense: Each potency level provides a <col=ffffff>5%</col> chance to automatically bait Hunter traps when placed.";
      }
      case 47709:
      {
        return "Kwuarm Incense: Each potency level provides a <col=ffffff>2.5%</col> increase to weapon poison damage.";
      }
      case 47710:
      {
        return "Bloodweed Incense: Each potency level provides a <col=ffffff>2.5%</col> chance to gain an additional log when cutting bloodwood trees.";
      }
      case 47711:
      {
        return "Snapdragon Incense: Each potency level provides a <col=ffffff>50%</col> increase to stat restore rate.";
      }
      case 47712:
      {
        return "Cadantine Incense: Each potency level provides a <col=ffffff>2.5%</col> chance not to deplete trees when cut or archaeology material caches when excavated.";
      }
      case 47713:
      {
        return "Lantadyme Incense: Each potency level provides a <col=ffffff>30</col> seconds increase to the duration of potions with timers lasting over 6 minutes.";
      }
      case 47714:
      {
        return "Dwarf weed Incense: Each potency level provides a <col=ffffff>2%</col> chance to bank items gathered or dropped by monsters.";
      }
      case 47715:
      {
        return "Torstol Incense: Each potency level provides a <col=ffffff>0.5%</col> increase to base XP gain.";
      }
      case 47716:
      {
        return "Fellstalk Incense: Each potency level provides a <col=ffffff>1%</col> increase to the chance of spawning an elite version of creatures which have one.";
      }
    }
    return "";
  };
  V[6430] = { vb: [], vp: [], vc: [], calls: [] };
  S[6430] = function (int0) {
    if (R.eq(int0, 1)) {
      return 0;
    }
    return 1;
  };
  V[6438] = { vb: [], vp: [], vc: [], calls: [] };
  S[6438] = function () {
    return 25;
  };
  V[6506] = { vb: [], vp: [], vc: [2091, 2092, 2093, 2094, 2095, 2096, 2097, 2098, 2099, 2100, 2101, 2102, 2103, 2104, 2105, 2106, 2107, 2108, 2109, 2110, 2111, 2112, 2113, 2114, 2115, 2116, 2121, 2122, 2123, 2124, 2125, 2126, 2127, 2128, 2129, 2130, 2131, 2132, 2133, 2134, 2135, 2136, 2137, 2138, 2139, 2140, 2141, 2142, 2143, 2144, 2145, 2146, 2147, 2148, 2149, 2150, 2151, 2152, 2153, 2154, 2155, 2156, 2157, 2158, 2159, 2160, 2161, 2162, 2163, 2164, 2165, 2166, 2167, 2168, 2169, 2170, 2171, 2172, 2173, 2174, 2175, 2176, 2177, 2178, 2179, 2180, 2181, 2182, 2183, 2184, 2185, 2186, 2187, 2188, 2189, 2190, 2191, 2192, 2193, 2194, 2195, 2196, 2197, 2198, 2199, 2200, 2201, 2202, 2203, 2204, 2205, 2206, 2207, 2208, 2209, 2210, 2211, 2212, 2213, 2214, 2215, 2216, 2713, 2714, 2715, 2716, 2717, 2718, 2719, 2720, 2721, 2722, 2727, 2728, 2729, 2730, 3893, 3894, 3895, 3896, 3897, 3898, 3899, 3900, 4175, 4176, 4177, 4178, 4179, 4180, 4181, 4182, 4235, 4236, 4239, 4240, 4617, 4978, 4979, 4980, 4981, 4992, 4993, 4994, 4995, 6035, 6036, 6037, 6038, 6384, 6385, 6386, 6387, 6388, 6389, 6390, 6391, 6593, 6594, 6595, 6596, 6597, 6598, 6599, 6600, 6601, 6602, 6603, 6604, 6605, 6606, 6607, 6608, 6609, 6610, 6611, 6612, 6866, 6867, 6951, 6952, 6991, 6992, 7059, 7060, 7225, 7226, 7227, 7228, 7229, 7230, 7232, 7233, 7234, 7235, 7237, 7238, 7239, 7240, 7242, 7243, 7244, 7245, 7247, 7248, 7249, 7250, 7251, 7252, 7253, 7254, 7255, 7256, 7257, 7258, 7259, 7260, 7261, 7262, 7263, 7264, 7266, 7267, 7268, 7269, 7270, 7271, 7272, 7273, 7275, 7276, 7278, 7279, 7281, 7282, 7347, 7348, 7772, 7773, 7774, 7775, 7776, 7777, 7778, 7779, 7780, 7781, 7791, 7792, 7793, 7794, 8292, 8293, 8378, 8379, 8380, 8381, 8382, 8383, 8384, 8385, 8390, 8391, 8392, 8393, 8397, 8398], calls: [] };
  S[6506] = function (int0) {
    if (R.eq(R.structParam(int0, 2976), 1)) {
      return [R.vc(2215), R.vc(2216)];
    }
    switch (R.key(int0)) {
      case 14881:
      {
        return [R.vc(2091), R.vc(2092)];
      }
      case 14882:
      {
        return [R.vc(2091), R.vc(2092)];
      }
      case 29145:
      {
        return [R.vc(2091), R.vc(4617)];
      }
      case 14677:
      {
        return [R.vc(2093), R.vc(2094)];
      }
      case 14678:
      case 40935:
      {
        return [R.vc(2095), R.vc(2096)];
      }
      case 14679:
      {
        return [R.vc(2097), R.vc(2098)];
      }
      case 52781:
      {
        return [R.vc(8378), R.vc(8379)];
      }
      case 44223:
      {
        return [R.vc(2099), R.vc(2100)];
      }
      case 14681:
      {
        return [R.vc(2101), R.vc(2102)];
      }
      case 14682:
      {
        return [R.vc(2103), R.vc(2104)];
      }
      case 14683:
      {
        return [R.vc(2105), R.vc(2106)];
      }
      case 14684:
      case 40936:
      case 52782:
      case 52785:
      {
        return [R.vc(2107), R.vc(2108)];
      }
      case 14685:
      case 52787:
      {
        return [R.vc(2109), R.vc(2110)];
      }
      case 14686:
      {
        return [R.vc(2111), R.vc(2112)];
      }
      case 14687:
      {
        return [R.vc(2113), R.vc(2114)];
      }
      case 14688:
      {
        return [R.vc(2115), R.vc(2116)];
      }
      case 19255:
      {
        return [R.vc(2713), R.vc(2714)];
      }
      case 28178:
      {
        return [R.vc(4175), R.vc(4176)];
      }
      case 44244:
      {
        return [R.vc(8380), R.vc(8381)];
      }
      case 52788:
      {
        return [R.vc(8382), R.vc(8383)];
      }
      case 52789:
      {
        return [R.vc(8384), R.vc(8385)];
      }
      case 14700:
      {
        return [R.vc(2121), R.vc(2122)];
      }
      case 14701:
      case 40941:
      {
        return [R.vc(2123), R.vc(2124)];
      }
      case 14702:
      {
        return [R.vc(2125), R.vc(2126)];
      }
      case 14703:
      {
        return [R.vc(2127), R.vc(2128)];
      }
      case 14704:
      case 52790:
      {
        return [R.vc(2129), R.vc(2130)];
      }
      case 14705:
      {
        return [R.vc(2131), R.vc(2132)];
      }
      case 14706:
      {
        return [R.vc(2133), R.vc(2134)];
      }
      case 14707:
      {
        return [R.vc(2135), R.vc(2136)];
      }
      case 14708:
      {
        return [R.vc(2137), R.vc(2138)];
      }
      case 14709:
      {
        return [R.vc(2139), R.vc(2140)];
      }
      case 46279:
      {
        return [R.vc(7059), R.vc(7060)];
      }
      case 14710:
      {
        return [R.vc(2141), R.vc(2142)];
      }
      case 14711:
      {
        return [R.vc(2143), R.vc(2144)];
      }
      case 14712:
      {
        return [R.vc(2145), R.vc(2146)];
      }
      case 14713:
      {
        return [R.vc(2147), R.vc(2148)];
      }
      case 14714:
      {
        return [R.vc(2149), R.vc(2150)];
      }
      case 14715:
      {
        return [R.vc(2151), R.vc(2152)];
      }
      case 14716:
      {
        return [R.vc(2153), R.vc(2154)];
      }
      case 14717:
      {
        return [R.vc(2155), R.vc(2156)];
      }
      case 14718:
      {
        return [R.vc(2157), R.vc(2158)];
      }
      case 14719:
      {
        return [R.vc(2159), R.vc(2160)];
      }
      case 14720:
      {
        return [R.vc(2161), R.vc(2162)];
      }
      case 14721:
      {
        return [R.vc(2163), R.vc(2164)];
      }
      case 19252:
      {
        return [R.vc(2715), R.vc(2716)];
      }
      case 25028:
      {
        return [R.vc(3893), R.vc(3894)];
      }
      case 37206:
      {
        return [R.vc(6593), R.vc(6594)];
      }
      case 37207:
      {
        return [R.vc(6595), R.vc(6596)];
      }
      case 45045:
      {
        return [R.vc(6866), R.vc(6867)];
      }
      case 45340:
      {
        return [R.vc(6951), R.vc(6952)];
      }
      case 14690:
      {
        return [R.vc(2165), R.vc(2166)];
      }
      case 19253:
      {
        return [R.vc(2717), R.vc(2718)];
      }
      case 24188:
      {
        return [R.vc(3895), R.vc(3896)];
      }
      case 24189:
      {
        return [R.vc(3897), R.vc(3898)];
      }
      case 24190:
      {
        return [R.vc(3899), R.vc(3900)];
      }
      case 28179:
      {
        return [R.vc(4177), R.vc(4178)];
      }
      case 31649:
      {
        return [R.vc(7772), R.vc(7773)];
      }
      case 31982:
      {
        return [R.vc(7774), R.vc(7775)];
      }
      case 31983:
      {
        return [R.vc(7776), R.vc(7777)];
      }
      case 28429:
      {
        return [R.vc(4235), R.vc(4236)];
      }
      case 31984:
      {
        return [R.vc(7778), R.vc(7779)];
      }
      case 33650:
      {
        return [R.vc(7780), R.vc(7781)];
      }
      case 37199:
      {
        return [R.vc(6597), R.vc(6598)];
      }
      case 37200:
      {
        return [R.vc(6599), R.vc(6600)];
      }
      case 37201:
      {
        return [R.vc(6601), R.vc(6602)];
      }
      case 37202:
      {
        return [R.vc(6603), R.vc(6604)];
      }
      case 37203:
      {
        return [R.vc(6605), R.vc(6606)];
      }
      case 37204:
      {
        return [R.vc(6607), R.vc(6608)];
      }
      case 37205:
      {
        return [R.vc(6609), R.vc(6610)];
      }
      case 14663:
      {
        return [R.vc(2167), R.vc(2168)];
      }
      case 14664:
      {
        return [R.vc(2169), R.vc(2170)];
      }
      case 14665:
      {
        return [R.vc(2171), R.vc(2172)];
      }
      case 14666:
      {
        return [R.vc(2173), R.vc(2174)];
      }
      case 14667:
      {
        return [R.vc(2175), R.vc(2176)];
      }
      case 31986:
      {
        return [R.vc(4978), R.vc(4979)];
      }
      case 14668:
      case 45048:
      {
        return [R.vc(2177), R.vc(2178)];
      }
      case 14669:
      {
        return [R.vc(2179), R.vc(2180)];
      }
      case 14670:
      {
        return [R.vc(2181), R.vc(2182)];
      }
      case 14671:
      {
        return [R.vc(2183), R.vc(2184)];
      }
      case 14672:
      {
        return [R.vc(2185), R.vc(2186)];
      }
      case 14673:
      {
        return [R.vc(2187), R.vc(2188)];
      }
      case 14674:
      {
        return [R.vc(2189), R.vc(2190)];
      }
      case 52799:
      {
        return [R.vc(8390), R.vc(8391)];
      }
      case 19251:
      case 46276:
      {
        return [R.vc(2719), R.vc(2720)];
      }
      case 28177:
      {
        return [R.vc(4179), R.vc(4180)];
      }
      case 39532:
      {
        return [R.vc(6384), R.vc(6385)];
      }
      case 39533:
      {
        return [R.vc(6386), R.vc(6387)];
      }
      case 52796:
      {
        return [R.vc(8392), R.vc(8393)];
      }
      case 14725:
      case 44900:
      {
        return [R.vc(2191), R.vc(2192)];
      }
      case 14726:
      {
        return [R.vc(2193), R.vc(2194)];
      }
      case 14727:
      {
        return [R.vc(2195), R.vc(2196)];
      }
      case 28431:
      {
        return [R.vc(4239), R.vc(4240)];
      }
      case 14728:
      case 45046:
      {
        return [R.vc(2197), R.vc(2198)];
      }
      case 14729:
      {
        return [R.vc(2199), R.vc(2200)];
      }
      case 31985:
      {
        return [R.vc(4980), R.vc(4981)];
      }
      case 14730:
      {
        return [R.vc(2201), R.vc(2202)];
      }
      case 14731:
      {
        return [R.vc(2203), R.vc(2204)];
      }
      case 14732:
      {
        return [R.vc(2205), R.vc(2206)];
      }
      case 14733:
      {
        return [R.vc(2207), R.vc(2208)];
      }
      case 14734:
      {
        return [R.vc(2209), R.vc(2210)];
      }
      case 14735:
      {
        return [R.vc(2211), R.vc(2212)];
      }
      case 14736:
      {
        return [R.vc(2213), R.vc(2214)];
      }
      case 19342:
      case 47221:
      {
        return [R.vc(2727), R.vc(2728)];
      }
      case 19343:
      case 45450:
      {
        return [R.vc(2729), R.vc(2730)];
      }
      case 19254:
      case 46275:
      {
        return [R.vc(2721), R.vc(2722)];
      }
      case 28180:
      {
        return [R.vc(4181), R.vc(4182)];
      }
      case 39530:
      {
        return [R.vc(6388), R.vc(6389)];
      }
      case 39531:
      {
        return [R.vc(6390), R.vc(6391)];
      }
      case 14870:
      case 14869:
      case 14871:
      {
        return [R.vc(4992), R.vc(4993)];
      }
      case 14865:
      {
        return [R.vc(4994), R.vc(4995)];
      }
      case 37220:
      {
        return [R.vc(6611), R.vc(6612)];
      }
      case 1491:
      {
        return [R.vc(6035), R.vc(6036)];
      }
      case 47129:
      case 1488:
      {
        return [R.vc(6037), R.vc(6038)];
      }
      case 45800:
      case 28927:
      {
        return [R.vc(6991), R.vc(6992)];
      }
      case 51271:
      {
        return [R.vc(8292), R.vc(8293)];
      }
      case 14739:
      case 14749:
      case 14787:
      case 44912:
      case 14792:
      case 14795:
      case 14784:
      case 14745:
      case 14750:
      case 14791:
      case 14766:
      {
        return [R.vc(8397), R.vc(8398)];
      }
      case 48296:
      {
        return [R.vc(7225), R.vc(7226)];
      }
      case 48298:
      {
        return [R.vc(7244), R.vc(7245)];
      }
      case 48302:
      {
        return [R.vc(7227), R.vc(7228)];
      }
      case 48303:
      {
        return [R.vc(7229), R.vc(7230)];
      }
      case 48299:
      {
        return [R.vc(7247), R.vc(7248)];
      }
      case 48308:
      {
        return [R.vc(7251), R.vc(7252)];
      }
      case 48297:
      {
        return [R.vc(7242), R.vc(7243)];
      }
      case 48309:
      {
        return [R.vc(7253), R.vc(7254)];
      }
      case 48301:
      {
        return [R.vc(7249), R.vc(7250)];
      }
      case 48311:
      {
        return [R.vc(7255), R.vc(7256)];
      }
      case 48312:
      {
        return [R.vc(7257), R.vc(7258)];
      }
      case 48313:
      {
        return [R.vc(7259), R.vc(7260)];
      }
      case 48314:
      {
        return [R.vc(7261), R.vc(7262)];
      }
      case 48324:
      {
        return [R.vc(7263), R.vc(7264)];
      }
      case 48304:
      {
        return [R.vc(7232), R.vc(7233)];
      }
      case 48305:
      {
        return [R.vc(7234), R.vc(7235)];
      }
      case 48306:
      {
        return [R.vc(7237), R.vc(7238)];
      }
      case 48307:
      {
        return [R.vc(7239), R.vc(7240)];
      }
      case 31820:
      {
        return [R.vc(7791), R.vc(7792)];
      }
      case 32342:
      {
        return [R.vc(7793), R.vc(7794)];
      }
      case 48326:
      {
        return [R.vc(7268), R.vc(7269)];
      }
      case 48327:
      {
        return [R.vc(7270), R.vc(7271)];
      }
      case 48328:
      {
        return [R.vc(7266), R.vc(7267)];
      }
      case 48329:
      {
        return [R.vc(7272), R.vc(7273)];
      }
      case 48330:
      {
        return [R.vc(7275), R.vc(7276)];
      }
      case 48331:
      {
        return [R.vc(7278), R.vc(7279)];
      }
      case 48332:
      {
        return [R.vc(7281), R.vc(7282)];
      }
      case 49072:
      {
      }
      default:
      {
        return [0, 0];
      }
    }
    return [R.vc(7347), R.vc(7348)];
  };
  V[6553] = { vb: [], vp: [], vc: [], calls: [7235, 8240, 9279, 12478, 13258, 16583] };
  S[6553] = function (int0, int1, int2, int3, int4, int5, int6, string0, string1) {
    var int7 = 0;
    var int8 = 0;
    var int9 = 0;
    var int10 = 0;
    var string2 = "null";
    var string3 = "";
    var string4 = "null";
    var int11 = 0;
    switch (R.key(int3)) {
      case 1:
      {
        int7 = R.structParam(int0, 5818);
        if (R.eq(R.structParam(int0, 9118), 1)) {
          string2 = R.call(16583, [int0, int3]);
        } else {
          string2 = R.structParam(int0, 2795);
        }
        int11 = R.structParam(int0, 3825);
        break;
      }
      case 2:
      {
        int8 = R.structParam(int0, 5818);
        int7 = R.structParam(int0, 5819);
        if (R.eq(R.structParam(int0, 9118), 1)) {
          string2 = R.call(16583, [int0, int3]);
        } else {
          string2 = R.structParam(int0, 5814);
        }
        int11 = R.structParam(int0, 3826);
        break;
      }
      case 3:
      {
        int8 = R.structParam(int0, 5819);
        int7 = R.structParam(int0, 5820);
        if (R.eq(R.structParam(int0, 9118), 1)) {
          string2 = R.call(16583, [int0, int3]);
        } else {
          string2 = R.structParam(int0, 5815);
        }
        int11 = R.structParam(int0, 3827);
        break;
      }
      case 4:
      {
        int8 = R.structParam(int0, 5820);
        int7 = R.structParam(int0, 5821);
        if (R.eq(R.structParam(int0, 9118), 1)) {
          string2 = R.call(16583, [int0, int3]);
        } else {
          string2 = R.structParam(int0, 5816);
        }
        int11 = R.structParam(int0, 3828);
        break;
      }
      case 5:
      {
        int8 = R.structParam(int0, 5821);
        int7 = R.structParam(int0, 5822);
        if (R.eq(R.structParam(int0, 9118), 1)) {
          string2 = R.call(16583, [int0, int3]);
        } else {
          string2 = R.structParam(int0, 5817);
        }
        int11 = R.structParam(int0, 3829);
        break;
      }
    }
    if (R.eq(int11, 1)) {
      string2 = `${R.s(R.call(8240, [int0, (-1 | 0)]))}${R.s(string2)}`;
    }
    if ((int7 > 0)) {
      if ((int7 < int8)) {
        R.call(12478, [`Set effect ${R.s(R.str(int7, 10))} is lower then set effect ${R.s(R.str(int8, 10))}`]);
      }
      if (R.eq(int8, 0)) {
        var int2 = R.call(13258, [1, R.structParam(int4, 4205), int5, int6, int2]);
      }
      if ((int1 >= int7)) {
        string4 = string0;
      } else {
        string4 = string1;
      }
      [int9, int10] = R.call(9279, [int0]);
      if (R.eq(int10, 1)) {
        int7 = ((int7) + (1) | 0);
        string3 = R.structParam(int0, 4798);
        if (R.eq(int9, 0)) {
          string4 = string1;
        }
      }
      int2 = R.call(7235, [int4, int5, int6, int2, `Set Bonus (${R.s(R.str(int7, 10))}) ${R.s(string3)}: ${R.s(string2)}`, string4]);
    }
    return int2;
  };
  V[6554] = { vb: [], vp: [], vc: [], calls: [247, 6553, 7235, 8261, 9279, 15973] };
  S[6554] = function (int0, int1, int2, int3, int4, int5, int6, string0, string1, string2) {
    var int7 = 0;
    var int8 = 0;
    var int9 = 0;
    var string3 = "";
    [int7, int8] = R.call(9279, [int4]);
    if (R.eq(int8, 1)) {
      if (R.eq(int7, 1)) {
        var string0 = string1;
      } else {
        string0 = string2;
      }
      if ((!R.eq(R.strcmp(R.structParam(int4, 4910), ""), 0))) {
        var int0 = R.call(7235, [int1, int2, int3, int0, `- ${R.s(R.structParam(int4, 4910))}`, string0]);
      } else {
        int0 = R.call(7235, [int1, int2, int3, int0, `- ${R.s(R.itemName(R.structParam(int4, 4911)))}`, string0]);
      }
    }
    int7 = 0;
    var int10 = R.structParam(int4, 3205);
    var int11 = R.structParam(int4, 3206);
    var int12 = R.structParam(int4, 3207);
    var int13 = R.structParam(int4, 3208);
    var int14 = R.structParam(int4, 3209);
    var int15 = R.structParam(int4, 5505);
    var int16 = R.structParam(int4, 6620);
    var int17 = R.structParam(int4, 5823);
    var int18 = R.structParam(int4, 5824);
    var int19 = R.structParam(int4, 8375);
    var int20 = R.structParam(int4, 8376);
    var int21 = R.structParam(int4, 8377);
    var int22 = R.structParam(int4, 8378);
    var int23 = R.structParam(int4, 8379);
    var int24 = R.structParam(int4, 8380);
    var int25 = R.structParam(int4, 8381);
    var int26 = R.structParam(int4, 8382);
    var int27 = R.structParam(int4, 8383);
    var int28 = R.structParam(int4, 8384);
    var int29 = R.structParam(int4, 8385);
    var int30 = R.structParam(int4, 8386);
    var int31 = R.structParam(int4, 8387);
    var int32 = R.structParam(int4, 8388);
    var int33 = R.structParam(int4, 8389);
    var int34 = R.structParam(int4, 8390);
    var int35 = R.structParam(int4, 8391);
    var int36 = R.structParam(int4, 8392);
    var string0, int8, int5, int9; [string0, int8, int5, int9] = R.call(247, [1, int4, int8, int5, string0, string2]);
    if (R.eq(int9, 0)) {
      [int7, string0, int8] = R.call(8261, [int7, int10, int11, int12, int13, int14, int15, int16, int17, int18, int19, int20, int21, int22, int23, int24, int25, int26, int27, int28, int29, int30, int31, int32, int33, int34, int35, int36, string1, string2]);
    }
    if (R.eq(int8, 1)) {
      if ((!R.eq(R.strcmp(R.structParam(int4, 8367), ""), 0))) {
        int0 = R.call(7235, [int1, int2, int3, int0, `- ${R.s(R.structParam(int4, 8367))}`, string0]);
      } else {
        int0 = R.call(7235, [int1, int2, int3, int0, `- ${R.s(R.itemName(R.structParam(int4, 3205)))}`, string0]);
      }
    }
    int10 = R.structParam(int4, 3210);
    int11 = R.structParam(int4, 3211);
    int12 = R.structParam(int4, 3212);
    int13 = R.structParam(int4, 3213);
    int14 = R.structParam(int4, 3214);
    int15 = R.structParam(int4, 5506);
    int16 = R.structParam(int4, 6621);
    int17 = R.structParam(int4, 5825);
    int18 = R.structParam(int4, 5826);
    int19 = R.structParam(int4, 8393);
    int20 = R.structParam(int4, 8394);
    int21 = R.structParam(int4, 8395);
    int22 = R.structParam(int4, 8396);
    int23 = R.structParam(int4, 8397);
    int24 = R.structParam(int4, 8398);
    int25 = R.structParam(int4, 8399);
    int26 = R.structParam(int4, 8400);
    int27 = R.structParam(int4, 8401);
    int28 = R.structParam(int4, 8402);
    int29 = R.structParam(int4, 8403);
    int30 = R.structParam(int4, 8404);
    int31 = R.structParam(int4, 8405);
    int32 = R.structParam(int4, 8406);
    int33 = R.structParam(int4, 8407);
    int34 = R.structParam(int4, 8408);
    int35 = R.structParam(int4, 8409);
    int36 = R.structParam(int4, 8410);
    [string0, int8, int5, int9] = R.call(247, [2, int4, int8, int5, string0, string2]);
    if (R.eq(int9, 0)) {
      [int7, string0, int8] = R.call(8261, [int7, int10, int11, int12, int13, int14, int15, int16, int17, int18, int19, int20, int21, int22, int23, int24, int25, int26, int27, int28, int29, int30, int31, int32, int33, int34, int35, int36, string1, string2]);
    }
    if (R.eq(int8, 1)) {
      if ((!R.eq(R.strcmp(R.structParam(int4, 8368), ""), 0))) {
        int0 = R.call(7235, [int1, int2, int3, int0, `- ${R.s(R.structParam(int4, 8368))}`, string0]);
      } else {
        int0 = R.call(7235, [int1, int2, int3, int0, `- ${R.s(R.itemName(R.structParam(int4, 3210)))}`, string0]);
      }
    }
    int10 = R.structParam(int4, 3215);
    int11 = R.structParam(int4, 3216);
    int12 = R.structParam(int4, 3217);
    int13 = R.structParam(int4, 3218);
    int14 = R.structParam(int4, 3219);
    int15 = R.structParam(int4, 5507);
    int16 = R.structParam(int4, 6622);
    int17 = R.structParam(int4, 6046);
    int18 = R.structParam(int4, 6047);
    int19 = R.structParam(int4, 8411);
    int20 = R.structParam(int4, 8412);
    int21 = R.structParam(int4, 8413);
    int22 = R.structParam(int4, 8414);
    int23 = R.structParam(int4, 8415);
    int24 = R.structParam(int4, 8416);
    int25 = R.structParam(int4, 8417);
    int26 = R.structParam(int4, 8418);
    int27 = R.structParam(int4, 8419);
    int28 = R.structParam(int4, 8420);
    int29 = R.structParam(int4, 8421);
    int30 = R.structParam(int4, 8422);
    int31 = R.structParam(int4, 8423);
    int32 = R.structParam(int4, 8424);
    int33 = R.structParam(int4, 8425);
    int34 = R.structParam(int4, 8426);
    int35 = R.structParam(int4, 8427);
    int36 = R.structParam(int4, 8428);
    [string0, int8, int5, int9] = R.call(247, [3, int4, int8, int5, string0, string2]);
    if (R.eq(int9, 0)) {
      [int7, string0, int8] = R.call(8261, [int7, int10, int11, int12, int13, int14, int15, int16, int17, int18, int19, int20, int21, int22, int23, int24, int25, int26, int27, int28, int29, int30, int31, int32, int33, int34, int35, int36, string1, string2]);
    }
    if (R.eq(int8, 1)) {
      if ((!R.eq(R.strcmp(R.structParam(int4, 8369), ""), 0))) {
        int0 = R.call(7235, [int1, int2, int3, int0, `- ${R.s(R.structParam(int4, 8369))}`, string0]);
      } else {
        int0 = R.call(7235, [int1, int2, int3, int0, `- ${R.s(R.itemName(R.structParam(int4, 3215)))}`, string0]);
      }
    }
    int10 = R.structParam(int4, 3220);
    int11 = R.structParam(int4, 3221);
    int12 = R.structParam(int4, 3222);
    int13 = R.structParam(int4, 3223);
    int14 = R.structParam(int4, 3224);
    int15 = R.structParam(int4, 5508);
    int16 = R.structParam(int4, 6623);
    int17 = R.structParam(int4, 6048);
    int18 = R.structParam(int4, 6049);
    int19 = R.structParam(int4, 8429);
    int20 = R.structParam(int4, 8430);
    int21 = R.structParam(int4, 8431);
    int22 = R.structParam(int4, 8432);
    int23 = R.structParam(int4, 8433);
    int24 = R.structParam(int4, 8434);
    int25 = R.structParam(int4, 8435);
    int26 = R.structParam(int4, 8436);
    int27 = R.structParam(int4, 8437);
    int28 = R.structParam(int4, 8438);
    int29 = R.structParam(int4, 8439);
    int30 = R.structParam(int4, 8440);
    int31 = R.structParam(int4, 8441);
    int32 = R.structParam(int4, 8442);
    int33 = R.structParam(int4, 8443);
    int34 = R.structParam(int4, 8444);
    int35 = R.structParam(int4, 8445);
    int36 = R.structParam(int4, 8446);
    [string0, int8, int5, int9] = R.call(247, [4, int4, int8, int5, string0, string2]);
    if (R.eq(int9, 0)) {
      [int7, string0, int8] = R.call(8261, [int7, int10, int11, int12, int13, int14, int15, int16, int17, int18, int19, int20, int21, int22, int23, int24, int25, int26, int27, int28, int29, int30, int31, int32, int33, int34, int35, int36, string1, string2]);
    }
    if (R.eq(int8, 1)) {
      if ((!R.eq(R.strcmp(R.structParam(int4, 8370), ""), 0))) {
        int0 = R.call(7235, [int1, int2, int3, int0, `- ${R.s(R.structParam(int4, 8370))}`, string0]);
      } else {
        int0 = R.call(7235, [int1, int2, int3, int0, `- ${R.s(R.itemName(R.structParam(int4, 3220)))}`, string0]);
      }
    }
    int10 = R.structParam(int4, 3225);
    int11 = R.structParam(int4, 3226);
    int12 = R.structParam(int4, 3227);
    int13 = R.structParam(int4, 3228);
    int14 = R.structParam(int4, 3229);
    int15 = R.structParam(int4, 5509);
    int16 = R.structParam(int4, 6624);
    int17 = R.structParam(int4, 6050);
    int18 = R.structParam(int4, 6051);
    int19 = R.structParam(int4, 8447);
    int20 = R.structParam(int4, 8448);
    int21 = R.structParam(int4, 8449);
    int22 = R.structParam(int4, 8450);
    int23 = R.structParam(int4, 8451);
    int24 = R.structParam(int4, 8452);
    int25 = R.structParam(int4, 8453);
    int26 = R.structParam(int4, 8454);
    int27 = R.structParam(int4, 8455);
    int28 = R.structParam(int4, 8456);
    int29 = R.structParam(int4, 8457);
    int30 = R.structParam(int4, 8458);
    int31 = R.structParam(int4, 8459);
    int32 = R.structParam(int4, 8460);
    int33 = R.structParam(int4, 8461);
    int34 = R.structParam(int4, 8462);
    int35 = R.structParam(int4, 8463);
    int36 = R.structParam(int4, 8464);
    [string0, int8, int5, int9] = R.call(247, [5, int4, int8, int5, string0, string2]);
    if (R.eq(int9, 0)) {
      [int7, string0, int8] = R.call(8261, [int7, int10, int11, int12, int13, int14, int15, int16, int17, int18, int19, int20, int21, int22, int23, int24, int25, int26, int27, int28, int29, int30, int31, int32, int33, int34, int35, int36, string1, string2]);
    }
    if (R.eq(int8, 1)) {
      if ((!R.eq(R.strcmp(R.structParam(int4, 8371), ""), 0))) {
        int0 = R.call(7235, [int1, int2, int3, int0, `- ${R.s(R.structParam(int4, 8371))}`, string0]);
      } else {
        int0 = R.call(7235, [int1, int2, int3, int0, `- ${R.s(R.itemName(R.structParam(int4, 3225)))}`, string0]);
      }
    }
    int10 = R.structParam(int4, 5559);
    int11 = R.structParam(int4, 5560);
    int12 = R.structParam(int4, 5561);
    int13 = R.structParam(int4, 5562);
    int14 = R.structParam(int4, 5563);
    int15 = R.structParam(int4, 5683);
    int16 = R.structParam(int4, 6625);
    int17 = R.structParam(int4, 6052);
    int18 = R.structParam(int4, 6053);
    int19 = R.structParam(int4, 8465);
    int20 = R.structParam(int4, 8466);
    int21 = R.structParam(int4, 8467);
    int22 = R.structParam(int4, 8468);
    int23 = R.structParam(int4, 8469);
    int24 = R.structParam(int4, 8470);
    int25 = R.structParam(int4, 8471);
    int26 = R.structParam(int4, 8472);
    int27 = R.structParam(int4, 8473);
    int28 = R.structParam(int4, 8474);
    int29 = R.structParam(int4, 8475);
    int30 = R.structParam(int4, 8476);
    int31 = R.structParam(int4, 8477);
    int32 = R.structParam(int4, 8478);
    int33 = R.structParam(int4, 8479);
    int34 = R.structParam(int4, 8480);
    int35 = R.structParam(int4, 8481);
    int36 = R.structParam(int4, 8482);
    [string0, int8, int5, int9] = R.call(247, [6, int4, int8, int5, string0, string2]);
    if (R.eq(int9, 0)) {
      [int7, string0, int8] = R.call(8261, [int7, int10, int11, int12, int13, int14, int15, int16, int17, int18, int19, int20, int21, int22, int23, int24, int25, int26, int27, int28, int29, int30, int31, int32, int33, int34, int35, int36, string1, string2]);
    }
    if (R.eq(int8, 1)) {
      if ((!R.eq(R.strcmp(R.structParam(int4, 8372), ""), 0))) {
        int0 = R.call(7235, [int1, int2, int3, int0, `- ${R.s(R.structParam(int4, 8372))}`, string0]);
      } else {
        int0 = R.call(7235, [int1, int2, int3, int0, `- ${R.s(R.itemName(R.structParam(int4, 5559)))}`, string0]);
      }
    }
    int10 = R.structParam(int4, 5564);
    int11 = R.structParam(int4, 5565);
    int12 = R.structParam(int4, 5566);
    int13 = R.structParam(int4, 5567);
    int14 = R.structParam(int4, 5568);
    int15 = R.structParam(int4, 5684);
    int16 = R.structParam(int4, 6626);
    int17 = R.structParam(int4, 6054);
    int18 = R.structParam(int4, 6055);
    int19 = R.structParam(int4, 8483);
    int20 = R.structParam(int4, 8484);
    int21 = R.structParam(int4, 8485);
    int22 = R.structParam(int4, 8486);
    int23 = R.structParam(int4, 8487);
    int24 = R.structParam(int4, 8488);
    int25 = R.structParam(int4, 8489);
    int26 = R.structParam(int4, 8490);
    int27 = R.structParam(int4, 8491);
    int28 = R.structParam(int4, 8492);
    int29 = R.structParam(int4, 8493);
    int30 = R.structParam(int4, 8494);
    int31 = R.structParam(int4, 8495);
    int32 = R.structParam(int4, 8496);
    int33 = R.structParam(int4, 8497);
    int34 = R.structParam(int4, 8498);
    int35 = R.structParam(int4, 8499);
    int36 = R.structParam(int4, 8500);
    [string0, int8, int5, int9] = R.call(247, [7, int4, int8, int5, string0, string2]);
    if (R.eq(int9, 0)) {
      [int7, string0, int8] = R.call(8261, [int7, int10, int11, int12, int13, int14, int15, int16, int17, int18, int19, int20, int21, int22, int23, int24, int25, int26, int27, int28, int29, int30, int31, int32, int33, int34, int35, int36, string1, string2]);
    }
    if (R.eq(int8, 1)) {
      if ((!R.eq(R.strcmp(R.structParam(int4, 8373), ""), 0))) {
        int0 = R.call(7235, [int1, int2, int3, int0, `- ${R.s(R.structParam(int4, 8373))}`, string0]);
      } else {
        int0 = R.call(7235, [int1, int2, int3, int0, `- ${R.s(R.itemName(R.structParam(int4, 5564)))}`, string0]);
      }
    }
    int10 = R.structParam(int4, 5569);
    int11 = R.structParam(int4, 5570);
    int12 = R.structParam(int4, 5571);
    int13 = R.structParam(int4, 5572);
    int14 = R.structParam(int4, 5573);
    int15 = R.structParam(int4, 5685);
    int16 = R.structParam(int4, 6627);
    int17 = R.structParam(int4, 6616);
    int18 = R.structParam(int4, 7828);
    int19 = R.structParam(int4, 8501);
    int20 = R.structParam(int4, 8502);
    int21 = R.structParam(int4, 8503);
    int22 = R.structParam(int4, 8504);
    int23 = R.structParam(int4, 8505);
    int24 = R.structParam(int4, 8506);
    int25 = R.structParam(int4, 8507);
    int26 = R.structParam(int4, 8508);
    int27 = R.structParam(int4, 8509);
    int28 = R.structParam(int4, 8510);
    int29 = R.structParam(int4, 8511);
    int30 = R.structParam(int4, 8512);
    int31 = R.structParam(int4, 8513);
    int32 = R.structParam(int4, 8514);
    int33 = R.structParam(int4, 8515);
    int34 = R.structParam(int4, 8516);
    int35 = R.structParam(int4, 8517);
    int36 = R.structParam(int4, 8518);
    [string0, int8, int5, int9] = R.call(247, [8, int4, int8, int5, string0, string2]);
    if (R.eq(int9, 0)) {
      [int7, string0, int8] = R.call(8261, [int7, int10, int11, int12, int13, int14, int15, int16, int17, int18, int19, int20, int21, int22, int23, int24, int25, int26, int27, int28, int29, int30, int31, int32, int33, int34, int35, int36, string1, string2]);
    }
    if (R.eq(int8, 1)) {
      if ((!R.eq(R.strcmp(R.structParam(int4, 8374), ""), 0))) {
        int0 = R.call(7235, [int1, int2, int3, int0, `- ${R.s(R.structParam(int4, 8374))}`, string0]);
      } else {
        int0 = R.call(7235, [int1, int2, int3, int0, `- ${R.s(R.itemName(R.structParam(int4, 5569)))}`, string0]);
      }
    }
    if ((R.structParam(int4, 7382) > 0)) {
      string3 = R.call(15973, [R.structParam(int4, 7382), 1]);
      if (((!R.eq(R.structParam(int4, 9003), (-1 | 0))) && R.eq(R.varRef(R.structParam(int4, 9003)), 2))) {
        int0 = R.call(7235, [int1, int2, int3, int0, `<col=00ff00>Charged (${R.s(string3)})</col> - Set bonuses active.`, string0]);
      } else {
        int0 = R.call(7235, [int1, int2, int3, int0, `<col=ffffff>Charging (${R.s(string3)})</col> - Set bonuses will be active after equipped for <col=ffffff>${R.s(string3)}</col>.`, string0]);
      }
    }
    if ((R.eq(R.itemParam(int6, 3793), 1) && R.eq(R.itemParam(int6, 6186), (-1 | 0)))) {
      int0 = R.call(7235, [int1, int2, int3, int0, "When repaired:", string1]);
    }
    int0 = R.call(6553, [int4, int7, int0, 1, int1, int2, int3, string1, string2]);
    int0 = R.call(6553, [int4, int7, int0, 2, int1, int2, int3, string1, string2]);
    int0 = R.call(6553, [int4, int7, int0, 3, int1, int2, int3, string1, string2]);
    int0 = R.call(6553, [int4, int7, int0, 4, int1, int2, int3, string1, string2]);
    int0 = R.call(6553, [int4, int7, int0, 5, int1, int2, int3, string1, string2]);
    return int0;
  };
  V[6592] = { vb: [], vp: [], vc: [], calls: [] };
  S[6592] = function () {
    if ((R.eq(R.mapMembers(), 1) && R.eq(R.itemParam(R.invObj(94, 9), 2881), 14))) {
      return 1;
    }
    return 0;
  };
  V[6643] = { vb: [], vp: [8601], vc: [], calls: [] };
  S[6643] = function () {
    return R.vp(8601);
  };
  V[6646] = { vb: [44368, 44369, 44370, 44371, 44372, 44373, 44374, 44375, 44376, 44377, 44378, 44379, 44380, 44381, 44382, 44383, 44384, 44385, 44386], vp: [], vc: [], calls: [] };
  S[6646] = function (int0) {
    switch (R.key(int0)) {
      case 48243:
      {
        return R.vb(44386);
      }
      case 211:
      {
        return R.vb(44377);
      }
      case 37975:
      {
        return R.vb(44379);
      }
      case 215:
      {
        return R.vb(44381);
      }
      case 217:
      {
        return R.vb(44383);
      }
      case 21626:
      {
        return R.vb(44385);
      }
      case 199:
      {
        return R.vb(44368);
      }
      case 205:
      {
        return R.vb(44371);
      }
      case 209:
      {
        return R.vb(44375);
      }
      case 213:
      {
        return R.vb(44378);
      }
      case 2485:
      {
        return R.vb(44382);
      }
      case 201:
      {
        return R.vb(44370);
      }
      case 207:
      {
        return R.vb(44372);
      }
      case 3051:
      {
        return R.vb(44380);
      }
      case 203:
      {
        return R.vb(44369);
      }
      case 3049:
      {
        return R.vb(44373);
      }
      case 219:
      {
        return R.vb(44384);
      }
      case 14836:
      {
        return R.vb(44376);
      }
      case 12174:
      {
        return R.vb(44374);
      }
    }
    return 0;
  };
  V[6666] = { vb: [], vp: [], vc: [], calls: [6646] };
  S[6666] = function () {
    var int0 = 0;
    var int1 = R.enumCount(15289);
    var int2 = (-1 | 0);
    while ((int0 < int1)) {
      int2 = R.enumValue(0, 33, 15289, int0);
      if ((R.call(6646, [int2]) > 0)) {
        return 1;
      }
      int0 = ((int0) + (1) | 0);
    }
    return 0;
  };
  V[6667] = { vb: [44484, 44485, 44486], vp: [], vc: [], calls: [] };
  S[6667] = function (int0) {
    if (((R.eq(R.vb(44484), int0) || R.eq(R.vb(44485), int0)) || R.eq(R.vb(44486), int0))) {
      return 1;
    }
    return 0;
  };
  V[6687] = { vb: [], vp: [], vc: [], calls: [4034, 13041] };
  S[6687] = function (int0, int1, int2) {
    if (R.eq(R.call(4034, [int0]), 1)) {
      var int0 = 20767;
    }
    if (R.eq(R.itemParam(int0, 258), 0)) {
      return 0;
    }
    if ((R.eq(R.itemParam(int0, 6295), 1) || R.eq(R.itemParam(int0, 7393), 1))) {
      return R.call(13041, [int0, int1, int2]);
    }
    if ((R.eq(int2, 1) && R.eq(R.itemParam(int0, 4244), 0))) {
      return 0;
    }
    if ((R.eq(int1, 31) && R.eq(R.itemParam(int0, 6296), 1))) {
      return 1;
    }
    if ((!R.eq(R.enumValue(0, 17, 681, int1), R.itemParam(int0, 277)))) {
      return 0;
    }
    return 1;
  };
  V[6802] = { vb: [33619, 33620, 33621, 33622, 33623, 33624, 33625, 33626, 33627, 33628, 33629, 33630, 33631, 33632, 33633, 33635, 33636, 33637, 46460], vp: [], vc: [], calls: [] };
  S[6802] = function () {
    return ((((((((((((((((((((((((((((((((((((R.vb(33619)) + (R.vb(46460)) | 0)) + (R.vb(33620)) | 0)) + (R.vb(33621)) | 0)) + (R.vb(33622)) | 0)) + (R.vb(33623)) | 0)) + (R.vb(33624)) | 0)) + (R.vb(33625)) | 0)) + (R.vb(33626)) | 0)) + (R.vb(33627)) | 0)) + (R.vb(33628)) | 0)) + (R.vb(33629)) | 0)) + (R.vb(33630)) | 0)) + (R.vb(33631)) | 0)) + (R.vb(33632)) | 0)) + (R.vb(33633)) | 0)) + (R.vb(33635)) | 0)) + (R.vb(33636)) | 0)) + (R.vb(33637)) | 0);
  };
  V[6804] = { vb: [33634, 38766, 38767, 38768, 38769, 38770, 38771, 38772, 38773, 53456], vp: [], vc: [], calls: [] };
  S[6804] = function () {
    return ((((((((((((((((((R.vb(33634)) + (R.vb(38766)) | 0)) + (R.vb(38767)) | 0)) + (R.vb(38768)) | 0)) + (R.vb(38769)) | 0)) + (R.vb(38770)) | 0)) + (R.vb(38771)) | 0)) + (R.vb(38772)) | 0)) + (R.vb(53456)) | 0)) + (R.vb(38773)) | 0);
  };
  V[7051] = { vb: [58286], vp: [], vc: [], calls: [7653] };
  S[7051] = function (int0, string0) {
    var int1 = 150;
    if ((R.vb(58286) >= 5)) {
      int1 = 350;
    }
    var string0 = `<br> - Your <col=3366FF>Magic attacks</col> gain <col=ffffff>+${R.s(R.call(7653, [int1, 1, 1, 0, 1]))}% Critical Strike Damage</col>.`;
    return string0;
  };
  V[7073] = { vb: [], vp: [], vc: [], calls: [2193, 12478, 18842] };
  S[7073] = function (int0) {
    var int1 = R.call(18842, [int0]);
    if (R.eq(int1, (-1 | 0))) {
      R.call(12478, [`Quest #${R.s(R.str(int0, 10))} missing from [ql4_quests]. enum.`]);
      return 0;
    }
    return R.call(2193, [int1]);
  };
  V[7081] = { vb: [], vp: [], vc: [], calls: [16843, 16938] };
  S[7081] = function () {
    return ((R.call(16938, [])) + (R.call(16843, [1])) | 0);
  };
  V[7167] = { vb: [], vp: [], vc: [], calls: [] };
  S[7167] = function (int0, int1) {
    var int2 = 1;
    var int3 = R.enumValue(0, 17, 681, int2);
    while ((!R.eq(int3, (-1 | 0)))) {
      if ((R.statBase(int3) < int0)) {
        if ((!R.eq(int3, 26))) {
          return 0;
        }
        if (R.eq(int1, 1)) {
          return 0;
        }
      }
      int2 = ((int2) + (1) | 0);
      int3 = R.enumValue(0, 17, 681, int2);
    }
    return 1;
  };
  V[7229] = { vb: [], vp: [], vc: [], calls: [] };
  S[7229] = function (int0, int1, int2, int3) {
    var int0 = R.min(int0, 6);
    switch (R.key(R.mod(R.min(int1, int0), 6))) {
      case 1:
      {
        R.ifNoop(0, ((int2) + (Math.imul(35, R.idiv(int3, int0))) | 0), 1, 0);
        break;
      }
      case 2:
      {
        switch (R.key(R.mod(int3, int0))) {
          case 0:
          {
            R.ifNoop(R.idiv(((0) - (36) | 0), 2), ((int2) + (Math.imul(35, R.idiv(int3, int0))) | 0), 1, 0);
            break;
          }
          case 1:
          {
            R.ifNoop(R.idiv(36, 2), ((int2) + (Math.imul(35, R.idiv(int3, int0))) | 0), 1, 0);
            break;
          }
        }
        break;
      }
      case 3:
      {
        switch (R.key(R.mod(int3, int0))) {
          case 0:
          {
            R.ifNoop(((0) - (36) | 0), ((int2) + (Math.imul(35, R.idiv(int3, int0))) | 0), 1, 0);
            break;
          }
          case 1:
          {
            R.ifNoop(0, ((int2) + (Math.imul(35, R.idiv(int3, int0))) | 0), 1, 0);
            break;
          }
          case 2:
          {
            R.ifNoop(36, ((int2) + (Math.imul(35, R.idiv(int3, int0))) | 0), 1, 0);
            break;
          }
        }
        break;
      }
      case 4:
      {
        switch (R.key(R.mod(int3, int0))) {
          case 0:
          {
            R.ifNoop(R.scale(((0) - (36) | 0), 2, 3), ((int2) + (Math.imul(35, R.idiv(int3, int0))) | 0), 1, 0);
            break;
          }
          case 1:
          {
            R.ifNoop(R.idiv(((0) - (36) | 0), 2), ((int2) + (Math.imul(35, R.idiv(int3, int0))) | 0), 1, 0);
            break;
          }
          case 2:
          {
            R.ifNoop(R.idiv(36, 2), ((int2) + (Math.imul(35, R.idiv(int3, int0))) | 0), 1, 0);
            break;
          }
          case 3:
          {
            R.ifNoop(R.scale(36, 2, 3), ((int2) + (Math.imul(35, R.idiv(int3, int0))) | 0), 1, 0);
            break;
          }
        }
        break;
      }
      case 5:
      {
        switch (R.key(R.mod(int3, int0))) {
          case 0:
          {
            R.ifNoop(Math.imul(((0) - (36) | 0), 2), ((int2) + (Math.imul(35, R.idiv(int3, int0))) | 0), 1, 0);
            break;
          }
          case 1:
          {
            R.ifNoop(((0) - (36) | 0), ((int2) + (Math.imul(35, R.idiv(int3, int0))) | 0), 1, 0);
            break;
          }
          case 2:
          {
            R.ifNoop(0, ((int2) + (Math.imul(35, R.idiv(int3, int0))) | 0), 1, 0);
            break;
          }
          case 3:
          {
            R.ifNoop(36, ((int2) + (Math.imul(35, R.idiv(int3, int0))) | 0), 1, 0);
            break;
          }
          case 4:
          {
            R.ifNoop(Math.imul(36, 2), ((int2) + (Math.imul(35, R.idiv(int3, int0))) | 0), 1, 0);
            break;
          }
        }
        break;
      }
      default:
      {
        switch (R.key(R.mod(int3, int0))) {
          case 0:
          {
            R.ifNoop(R.scale(((0) - (36) | 0), 2, 5), ((int2) + (Math.imul(35, R.idiv(int3, int0))) | 0), 1, 0);
            break;
          }
          case 1:
          {
            R.ifNoop(R.scale(((0) - (36) | 0), 2, 3), ((int2) + (Math.imul(35, R.idiv(int3, int0))) | 0), 1, 0);
            break;
          }
          case 2:
          {
            R.ifNoop(R.idiv(((0) - (36) | 0), 2), ((int2) + (Math.imul(35, R.idiv(int3, int0))) | 0), 1, 0);
            break;
          }
          case 3:
          {
            R.ifNoop(R.idiv(36, 2), ((int2) + (Math.imul(35, R.idiv(int3, int0))) | 0), 1, 0);
            break;
          }
          case 4:
          {
            R.ifNoop(R.scale(36, 2, 3), ((int2) + (Math.imul(35, R.idiv(int3, int0))) | 0), 1, 0);
            break;
          }
          case 5:
          {
            R.ifNoop(R.scale(36, 2, 5), ((int2) + (Math.imul(35, R.idiv(int3, int0))) | 0), 1, 0);
            break;
          }
        }
        break;
      }
    }
    return;
  };
  V[7235] = { vb: [], vp: [], vc: [], calls: [] };
  S[7235] = function (int0, int1, int2, int3, string0, string1) {
    return R.appendLine(14931919, int0, int1, int2, int3, `${R.s(string1)}${R.s(string0)}</col>`);
  };
  V[7236] = { vb: [], vp: [], vc: [], calls: [] };
  S[7236] = function (int0, int1, int2, int3, int4, int5, string0, string1) {
    return R.appendLine(int1, int2, int3, int4, int5, `${R.s(string0)}: ${R.s(string1)}${R.s(R.strLoc(int0, 1))}</col>`);
  };
  V[7237] = { vb: [], vp: [], vc: [], calls: [] };
  S[7237] = function (int0, int1, int2, int3, int4, int5, int6, string0, string1) {
    return R.appendLine(int2, int3, int4, int5, int6, `${R.s(string0)}: ${R.s(string1)}${R.s(R.enumValue(0, 36, int1, int0))}`);
  };
  V[7239] = { vb: [], vp: [], vc: [], calls: [] };
  S[7239] = function (int0) {
    switch (R.key(int0)) {
      case 1:
      case 2:
      case 3:
      case 11:
      {
        return 1;
      }
    }
    return 0;
  };
  V[7241] = { vb: [], vp: [], vc: [], calls: [] };
  S[7241] = function (int0) {
    if (R.eq(int0, (-1 | 0))) {
      return 0;
    }
    if ((!R.eq(R.itemParam(int0, 2824), 0))) {
      if ((R.itemParam(int0, 2869) > 0)) {
        return 9;
      }
      return 8;
    }
    if (R.eq(R.itemWearpos(int0), 13)) {
      if ((R.itemParam(int0, 643) > 0)) {
        return 4;
      }
      if (((R.itemParam(int0, 972) > 0) || (R.itemParam(int0, 7596) > 0))) {
        return 10;
      }
      if (R.eq(R.itemCategory(int0), 5368)) {
        return 13;
      }
    } else {
      if ((!R.eq(R.itemParam(int0, 686), (-1 | 0)))) {
        if ((!R.eq(R.itemParam(int0, 2827), 0))) {
          return 3;
        }
        if ((!R.eq(R.itemParam(int0, 2826), 0))) {
          return 2;
        }
        if ((!R.eq(R.itemParam(int0, 2825), 0))) {
          return 1;
        }
        if ((!R.eq(R.itemParam(int0, 8898), 0))) {
          return 11;
        }
      } else {
        if ((!R.eq(R.itemParam(int0, 2823), 0))) {
          return 7;
        }
        if ((!R.eq(R.itemParam(int0, 2822), 0))) {
          return 6;
        }
        if ((!R.eq(R.itemParam(int0, 2821), 0))) {
          return 5;
        }
        if ((!R.eq(R.itemParam(int0, 8897), 0))) {
          return 12;
        }
      }
    }
    return 0;
  };
  V[7244] = { vb: [], vp: [], vc: [], calls: [] };
  S[7244] = function (int0) {
    var int1 = 0;
    var int2 = 0;
    var int3 = 0;
    var int4 = (-1 | 0);
    if (R.eq(R.itemParam(int0, 2827), 1)) {
      int1 = R.itemParam(int0, 3);
      int4 = 6;
    } else {
      if (R.eq(R.itemParam(int0, 2825), 1)) {
        int1 = R.itemParam(int0, 3267);
        int4 = 0;
      } else {
        if (R.eq(R.itemParam(int0, 2826), 1)) {
          int1 = R.itemParam(int0, 4);
          int4 = 4;
        } else {
          if (R.eq(R.itemParam(int0, 8898), 1)) {
            int1 = R.itemParam(int0, 8879);
            int4 = 28;
          }
        }
      }
    }
    if ((!R.eq(int4, (-1 | 0)))) {
      int2 = R.enumValue(0, 0, 7338, int1);
      int1 = R.max(0, R.enumValue(0, 0, 7339, int2));
      int3 = R.max(0, R.min(10, R.scale(((R.stat(int4)) - (int2) | 0), 100, 50)));
      int3 = R.enumValue(0, 0, 7339, ((int3) + (int2) | 0));
      int3 = R.max(0, ((int3) - (R.enumValue(0, 0, 7339, R.min(85, int2))) | 0));
    }
    return int1;
  };
  V[7245] = { vb: [], vp: [], vc: [], calls: [] };
  S[7245] = function (int0, int1) {
    if ((R.itemParam(int0, 4596) > 0)) {
      return R.itemParam(int0, 4596);
    }
    var int2 = (-1 | 0);
    switch (R.key(int1)) {
      case 3:
      case 10:
      {
        int2 = 6;
        break;
      }
      case 2:
      case 4:
      {
        int2 = 4;
        break;
      }
      case 1:
      {
        if (R.eq(R.itemParam(int0, 749), 2)) {
          int2 = 2;
        } else {
          int2 = 0;
        }
        break;
      }
      case 11:
      case 13:
      {
        int2 = 28;
        break;
      }
      case 9:
      case 8:
      case 5:
      case 6:
      case 7:
      case 12:
      {
        int2 = 1;
        break;
      }
      default:
      {
        return 0;
      }
    }
    var int3 = 0;
    if (R.eq(R.itemParam(int0, 749), int2)) {
      int3 = R.itemParam(int0, 750);
    } else {
      if (R.eq(R.itemParam(int0, 751), int2)) {
        int3 = R.itemParam(int0, 752);
      } else {
        if (R.eq(R.itemParam(int0, 753), int2)) {
          int3 = R.itemParam(int0, 754);
        } else {
          if (R.eq(R.itemParam(int0, 755), int2)) {
            int3 = R.itemParam(int0, 756);
          } else {
            if (R.eq(R.itemParam(int0, 757), int2)) {
              int3 = R.itemParam(int0, 758);
            } else {
              if (R.eq(R.itemParam(int0, 759), int2)) {
                int3 = R.itemParam(int0, 760);
              }
            }
          }
        }
      }
    }
    return int3;
  };
  V[7246] = { vb: [], vp: [], vc: [], calls: [2916] };
  S[7246] = function (int0) {
    var int1 = 0;
    var int2 = R.max(1, R.itemParam(int0, 2970));
    if ((R.itemParam(int0, 642) > 0)) {
      int1 = R.scale(R.call(2916, []), 100, R.itemParam(int0, 642));
    }
    if ((R.itemParam(int0, 963) > 0)) {
      int1 = ((int1) + (R.itemParam(int0, 963)) | 0);
    }
    if ((int1 > 0)) {
      return R.idiv(int1, int2);
    }
    if (R.eq(R.itemCategory(int0), 3868)) {
      int1 = R.scale(int1, 100, 120);
    }
    if (R.eq(R.itemParam(int0, 1047), 1)) {
      return R.idiv(int1, int2);
    }
    return R.idiv(R.max(200, int1), int2);
  };
  V[7247] = { vb: [], vp: [], vc: [], calls: [8002] };
  S[7247] = function (int0) {
    if (R.eq(int0, 0)) {
      return "0";
    }
    if ((int0 < 0)) {
      var int0 = ((0) - (int0) | 0);
    }
    return `${R.s(R.str(R.idiv(int0, 10), 10))}${R.s(R.call(8002, []))}${R.s(R.str(R.mod(int0, 10), 10))}`;
  };
  V[7361] = { vb: [], vp: [8681, 8682, 8683, 8684, 8685, 8686, 8687, 8688, 8689, 8690], vc: [], calls: [] };
  S[7361] = function (int0) {
    switch (R.key(int0)) {
      case 42006:
      {
        return R.vp(8681);
      }
      case 42007:
      {
        return R.vp(8682);
      }
      case 42008:
      {
        return R.vp(8683);
      }
      case 42009:
      {
        return R.vp(8684);
      }
      case 42010:
      {
        return R.vp(8685);
      }
      case 42001:
      {
        return R.vp(8686);
      }
      case 42002:
      {
        return R.vp(8687);
      }
      case 42003:
      {
        return R.vp(8688);
      }
      case 42004:
      {
        return R.vp(8689);
      }
      case 42005:
      {
        return R.vp(8690);
      }
    }
    return 0;
  };
  V[7436] = { vb: [], vp: [], vc: [], calls: [] };
  S[7436] = function (int0) {
    return 3;
  };
  V[7460] = { vb: [], vp: [], vc: [], calls: [] };
  S[7460] = function (int0) {
    if (R.eq(int0, (-1 | 0))) {
      return 0;
    }
    if (((R.itemWearpos(int0) > 0) && (R.itemWearpos2(int0) > 0))) {
      return 2;
    }
    if ((R.eq(R.itemParam(int0, 2826), 1) && R.eq(R.invTotalParam(94, 2826), 2))) {
      return 1;
    }
    if ((R.eq(R.itemParam(int0, 2825), 1) && R.eq(R.invTotalParam(94, 2825), 2))) {
      return 1;
    }
    if ((R.eq(R.itemParam(int0, 2827), 1) && R.eq(R.invTotalParam(94, 2827), 2))) {
      return 1;
    }
    if ((R.eq(R.itemParam(int0, 8898), 1) && R.eq(R.invTotalParam(94, 8898), 2))) {
      return 1;
    }
    return 3;
  };
  V[7495] = { vb: [27168], vp: [], vc: [], calls: [] };
  S[7495] = function () {
    if ((R.statBase(0) < 3)) {
      return 0;
    }
    if (R.eq(R.vb(27168), 1)) {
      return 0;
    }
    return 1;
  };
  V[7602] = { vb: [], vp: [], vc: [], calls: [] };
  S[7602] = function (int0) {
    if (R.eq(int0, (-1 | 0))) {
      return (-1 | 0);
    }
    var int1 = R.enumValue(73, 0, 7365, int0);
    if (R.eq(int1, (-1 | 0))) {
      return (-1 | 0);
    }
    return R.invObj(686, int1);
  };
  V[7603] = { vb: [], vp: [], vc: [], calls: [] };
  S[7603] = function (int0) {
    if (R.eq(int0, (-1 | 0))) {
      return (-1 | 0);
    }
    var int1 = R.enumValue(73, 0, 7366, int0);
    if (R.eq(int1, (-1 | 0))) {
      return (-1 | 0);
    }
    return R.invObj(686, int1);
  };
  V[7653] = { vb: [], vp: [], vc: [], calls: [362, 8002] };
  S[7653] = function (int0, int1, int2, int3, int4) {
    if (R.eq(int0, 0)) {
      return "0";
    }
    if (R.eq(int1, 0)) {
      return R.strLoc(int0, 1);
    }
    var int5 = 0;
    var int6 = 0;
    var int7 = 0;
    var int2 = R.min(int2, int1);
    if ((int1 > int2)) {
      if ((R.eq(int3, 0) || R.eq(int3, 2))) {
        int7 = R.pow(10, ((int1) - (int2) | 0));
        var int0 = R.idiv(int0, int7);
        var int1 = int2;
      } else {
        int7 = R.pow(10, ((((int1) - (int2) | 0)) - (1) | 0));
        if ((int0 > 0)) {
          if ((R.mod(R.idiv(int0, int7), 10) >= 5)) {
            int0 = ((int0) + (Math.imul(10, int7)) | 0);
          }
        } else {
          if ((R.mod(R.idiv(int0, int7), 10) <= (-5 | 0))) {
            int0 = ((int0) - (Math.imul(10, int7)) | 0);
          }
        }
        int0 = R.idiv(int0, Math.imul(int7, 10));
        int1 = int2;
      }
    }
    if (((!R.eq(int3, 2)) && (!R.eq(int3, 3)))) {
      while (((int1 > 0) && R.eq(R.mod(int0, 10), 0))) {
        int1 = ((int1) - (1) | 0);
        int0 = R.idiv(int0, 10);
      }
    }
    [int5, int6] = R.call(362, [int0, int1]);
    var string0 = "";
    var int8 = 0;
    int7 = int6;
    if ((int1 > 0)) {
      if (((!R.eq(int3, 2)) && (!R.eq(int3, 3)))) {
        if ((!R.eq(int6, 0))) {
          while ((int7 > 0)) {
            int7 = R.idiv(int7, 10);
            int8 = ((int8) + (1) | 0);
          }
          int8 = ((int1) - (int8) | 0);
          while ((int8 > 0)) {
            string0 = R.cat(string0, "0");
            int8 = ((int8) - (1) | 0);
          }
        }
      } else {
        if (R.eq(int6, 0)) {
          int8 = ((int8) + (1) | 0);
        } else {
          while ((int7 > 0)) {
            int7 = R.idiv(int7, 10);
            int8 = ((int8) + (1) | 0);
          }
        }
        int8 = ((int1) - (int8) | 0);
        while ((int8 > 0)) {
          string0 = R.cat(string0, "0");
          int8 = ((int8) - (1) | 0);
        }
      }
    }
    if ((R.eq(int4, 1) && R.eq(int6, 0))) {
      return R.strLoc(int5, 1);
    }
    return `${R.s(R.strLoc(int5, 1))}${R.s(R.call(8002, []))}${R.s(string0)}${R.s(R.str(int6, 10))}`;
  };
  V[7960] = { vb: [], vp: [], vc: [], calls: [] };
  S[7960] = function (int0, int1) {
    if (R.eq(R.itemParam(int0, 770), int1)) {
      return R.itemParam(int0, 771);
    }
    if (R.eq(R.itemParam(int0, 772), int1)) {
      return R.itemParam(int0, 773);
    }
    if (R.eq(R.itemParam(int0, 774), int1)) {
      return R.itemParam(int0, 775);
    }
    if (R.eq(R.itemParam(int0, 776), int1)) {
      return R.itemParam(int0, 777);
    }
    if (R.eq(R.itemParam(int0, 778), int1)) {
      return R.itemParam(int0, 779);
    }
    if (R.eq(R.itemParam(int0, 780), int1)) {
      return R.itemParam(int0, 781);
    }
    return 0;
  };
  V[8002] = { vb: [], vp: [], vc: [], calls: [] };
  S[8002] = function () {
    switch (R.key(R.lang())) {
      case 0:
      {
        return ".";
      }
      case 1:
      {
        return ",";
      }
      case 2:
      {
        return ",";
      }
      case 3:
      {
        return ",";
      }
      case 6:
      {
        return ",";
      }
    }
    return ".";
  };
  V[8137] = { vb: [], vp: [], vc: [], calls: [] };
  S[8137] = function (int0) {
    switch (R.key(int0)) {
      case 1024:
      case 1003:
      case 1032:
      case 1033:
      case 1034:
      case 1035:
      case 1031:
      case 1042:
      case 1043:
      case 1044:
      case 1048:
      case 1053:
      {
      }
      default:
      {
        return 0;
      }
    }
    return 1;
  };
  V[8240] = { vb: [], vp: [], vc: [], calls: [] };
  S[8240] = function (int0, int1) {
    var int2 = (-1 | 0);
    var int3 = (-1 | 0);
    if ((!R.eq(int0, (-1 | 0)))) {
      int2 = R.structParam(int0, 3830);
    }
    if ((!R.eq(int1, (-1 | 0)))) {
      int3 = R.itemParam(int1, 3830);
    }
    if ((R.eq(int2, 1) || R.eq(int3, 1))) {
      return "On damaging attacks: ";
    }
    if ((!R.eq(int0, (-1 | 0)))) {
      int2 = R.structParam(int0, 3831);
    }
    if ((!R.eq(int1, (-1 | 0)))) {
      int3 = R.itemParam(int1, 3831);
    }
    if ((R.eq(int2, 1) || R.eq(int3, 1))) {
      return "When damaged: ";
    }
    if ((!R.eq(int0, (-1 | 0)))) {
      int2 = R.structParam(int0, 3832);
    }
    if ((!R.eq(int1, (-1 | 0)))) {
      int3 = R.itemParam(int1, 3832);
    }
    if ((R.eq(int2, 1) || R.eq(int3, 1))) {
      return "Upon killing a target: ";
    }
    if ((!R.eq(int0, (-1 | 0)))) {
      int2 = R.structParam(int0, 3999);
    }
    if ((!R.eq(int1, (-1 | 0)))) {
      int3 = R.itemParam(int1, 3999);
    }
    if ((R.eq(int2, 1) || R.eq(int3, 1))) {
      return "On death: ";
    }
    if ((!R.eq(int0, (-1 | 0)))) {
      int2 = R.structParam(int0, 4029);
    }
    if ((!R.eq(int1, (-1 | 0)))) {
      int3 = R.itemParam(int1, 4029);
    }
    if ((R.eq(int2, 1) || R.eq(int3, 1))) {
      return "On activation: ";
    }
    return "";
  };
  V[8243] = { vb: [], vp: [], vc: [], calls: [] };
  S[8243] = function (string0, string1, string2, string3, string4, string5, string6, string7) {
    var string8 = "";
    var string9 = "";
    var int0 = R.len(string0);
    var int1 = R.len(string1);
    var int2 = R.len(string2);
    var int3 = R.len(string3);
    var int4 = R.len(string4);
    var int5 = R.len(string5);
    var int6 = 0;
    var int7 = 0;
    if ((((!R.eq(int1, 0)) || (!R.eq(int2, 0))) && (((!R.eq(int3, 0)) || (!R.eq(int4, 0))) || (!R.eq(int5, 0))))) {
      int6 = 1;
    }
    if ((!R.eq(int1, 0))) {
      int7 = 1;
      if ((!R.eq(int0, 0))) {
        string8 = `${R.s(string0)}.${R.s(string7)}${R.s(string1)}`;
      } else {
        string8 = string1;
      }
      string9 = string6;
    }
    if ((!R.eq(int2, 0))) {
      if (R.eq(R.len(string8), 0)) {
        string8 = string2;
      } else {
        string8 = `${R.s(string8)}${R.s(string7)}${R.s(string2)}`;
      }
      string9 = string6;
    }
    if (R.eq(int6, 1)) {
      string8 = `${R.s(string8)} | `;
    }
    if (((!R.eq(int0, 0)) && R.eq(int7, 0))) {
      string8 = string0;
      string9 = string6;
    }
    if ((!R.eq(int3, 0))) {
      string8 = `${R.s(string8)}${R.s(string9)}${R.s(string3)}`;
      string9 = string6;
    }
    if ((!R.eq(int4, 0))) {
      string8 = `${R.s(string8)}${R.s(string9)}${R.s(string4)}`;
      string9 = string6;
    }
    if ((!R.eq(int5, 0))) {
      string8 = `${R.s(string8)}${R.s(string9)}${R.s(string5)}`;
    }
    return string8;
  };
  V[8247] = { vb: [0, 53572, 53574, 53576, 55974], vp: [10994, 11006, 11018, 11051, 11054, 11820, 12643, 12646], vc: [], calls: [8248, 8744, 20963] };
  S[8247] = function (int0) {
    var int1 = 0;
    var int2 = (-1 | 0);
    var int3 = 9;
    if (((!R.eq(int0, (-1 | 0))) && R.eq(R.structParam(int0, 6527), 1))) {
      switch (R.key(int0)) {
        case 48302:
        {
          if ((R.eq(R.vp(10994), 1) && R.eq(R.vb(53572), 1))) {
            return 48303;
          }
          return 48302;
        }
        case 48304:
        {
          if ((R.eq(R.vp(11006), 1) && R.eq(R.vb(53574), 1))) {
            return 48305;
          }
          return 48304;
        }
        case 48306:
        {
          if ((R.eq(R.vp(11018), 1) && R.eq(R.vb(53576), 1))) {
            return 48307;
          }
          return 48306;
        }
        case 31820:
        {
          if ((R.eq(R.vp(11820), 1) && R.eq(R.vb(55974), 1))) {
            return 32342;
          }
          return 31820;
        }
        case 48311:
        case 48312:
        case 48313:
        {
          if (R.eq(R.vp(11051), 1)) {
            return 48312;
          }
          if (R.eq(R.vp(11054), 1)) {
            return 48313;
          }
          return 48311;
        }
        case 44244:
        case 52788:
        case 52789:
        {
          if (R.eq(R.vp(12643), 1)) {
            return 52788;
          }
          if (R.eq(R.vp(12646), 1)) {
            return 52789;
          }
          return 44244;
        }
        case 14704:
        case 52790:
        {
          if (R.eq(R.call(8744, []), 1)) {
            return 52790;
          }
          return 14704;
        }
        case 14685:
        case 52787:
        {
          if (R.eq(R.call(8744, []), 1)) {
            return 52787;
          }
          return 14685;
        }
      }
      int2 = R.structParam(int0, 2793);
      int3 = R.structParam(int0, 2806);
      int1 = R.call(8248, [int3, int2]);
      switch (R.key(int3)) {
        case 8:
        {
          break;
        }
        case 1:
        {
          switch (R.key(int2)) {
            case 4:
            {
              if (R.eq(int1, 1)) {
                if (R.eq(R.call(8744, []), 1)) {
                  return 52785;
                }
                return 40936;
              }
              if (R.eq(R.call(8744, []), 1)) {
                return 52782;
              }
              return 14684;
            }
            case 1:
            {
              if (R.eq(int1, 1)) {
                return 40935;
              }
              return 14678;
            }
            case 29:
            {
              if (R.eq(int1, 1)) {
                return 1488;
              }
              return 47129;
            }
            case 10:
            {
              if (R.eq(int1, 1)) {
                return 40941;
              }
              return 14701;
            }
          }
          break;
        }
        case 3:
        {
          switch (R.key(int2)) {
            case 5:
            {
              if (R.eq(int1, 1)) {
                return 45048;
              }
              return 14668;
            }
            case 10:
            {
              if (R.eq(int1, 1)) {
                return 46276;
              }
              return 19251;
            }
          }
          break;
        }
        case 4:
        {
          switch (R.key(int2)) {
            case 26:
            {
              if (R.eq(R.call(20963, []), 1)) {
                return 3593;
              }
              return 14751;
            }
            case 47:
            {
              if (R.eq(R.call(20963, []), 1)) {
                return 3594;
              }
              return 14770;
            }
            case 158:
            {
              if (R.eq(int1, 1)) {
                return 37900;
              }
              return 14876;
            }
            case 154:
            {
              if (R.eq(R.vb(0), 1)) {
                return 32261;
              }
              return 14873;
            }
            case 197:
            {
              if (R.eq(R.vb(0), 1)) {
                return 6847;
              }
              return 6845;
            }
            case 185:
            {
              if (R.eq(R.vb(0), 2)) {
                return 14873;
              }
              return 32261;
            }
            case 195:
            {
              if (R.eq(R.vb(0), 2)) {
                return 6845;
              }
              return 6847;
            }
            case 6:
            {
              return 14730;
            }
            case 165:
            {
              switch (R.key(int1)) {
                case 1:
                {
                  return 19342;
                }
                case 2:
                {
                  return 47221;
                }
              }
              break;
            }
            case 166:
            {
              switch (R.key(int1)) {
                case 1:
                {
                  return 19343;
                }
                case 2:
                {
                  return 45450;
                }
              }
              break;
            }
            case 5:
            {
              return 14729;
            }
            case 4:
            {
              if (R.eq(int1, 1)) {
                return 45046;
              }
              return 14728;
            }
            case 1:
            {
              if (R.eq(int1, 1)) {
                return 44900;
              }
              return 14725;
            }
            case 164:
            {
              if (R.eq(int1, 1)) {
                return 46275;
              }
              return 19254;
            }
          }
          break;
        }
      }
    }
    return int0;
  };
  V[8248] = { vb: [36969, 41436, 41437, 41438, 49799, 52857], vp: [], vc: [], calls: [4356, 15411, 16325] };
  S[8248] = function (int0, int1) {
    switch (R.key(int0)) {
      case 8:
      {
        break;
      }
      case 1:
      {
        switch (R.key(int1)) {
          case 4:
          {
            if ((R.eq(R.call(4356, []), 1) && R.eq(R.vb(41436), 1))) {
              return 1;
            }
            break;
          }
          case 1:
          {
            if ((R.eq(R.call(4356, []), 1) && R.eq(R.vb(41438), 1))) {
              return 1;
            }
            break;
          }
          case 10:
          {
            if ((R.eq(R.call(4356, []), 1) && R.eq(R.vb(41437), 1))) {
              return 1;
            }
            break;
          }
          case 29:
          {
            if (R.eq(R.call(15411, [1488]), 0)) {
              return 0;
            }
            if ((R.statBase(0) < R.structParam(1488, 2807))) {
              return 0;
            }
            if (R.eq(R.call(16325, []), 0)) {
              return 0;
            }
            return 1;
          }
        }
        break;
      }
      case 3:
      {
        switch (R.key(int1)) {
          case 5:
          {
            if ((R.eq(R.call(4356, []), 1) && R.eq(R.call(15411, [45048]), 1))) {
              return 1;
            }
            return 0;
          }
          case 10:
          {
            if ((R.eq(R.call(4356, []), 1) && R.eq(R.call(15411, [46276]), 1))) {
              return 1;
            }
            return 0;
          }
        }
        break;
      }
      case 4:
      {
        switch (R.key(int1)) {
          case 158:
          {
            if (R.eq(R.vb(36969), 1)) {
              return 1;
            }
            break;
          }
          case 165:
          {
            if (((R.eq(R.call(4356, []), 1) && R.eq(R.vb(52857), 1)) && (R.statBase(6) >= R.structParam(47221, 2807)))) {
              return 2;
            }
            return 1;
          }
          case 1:
          {
            if ((R.eq(R.call(4356, []), 1) && R.eq(R.call(15411, [44900]), 1))) {
              return 1;
            }
            return 0;
          }
          case 166:
          {
            if ((R.eq(R.call(4356, []), 1) && R.eq(R.vb(49799), 1))) {
              return 2;
            }
            return 1;
          }
          case 4:
          {
            if ((R.eq(R.call(4356, []), 1) && R.eq(R.call(15411, [45046]), 1))) {
              return 1;
            }
            return 0;
          }
          case 164:
          {
            if ((R.eq(R.call(4356, []), 1) && R.eq(R.call(15411, [46275]), 1))) {
              return 1;
            }
            return 0;
          }
        }
        break;
      }
    }
    return 0;
  };
  V[8251] = { vb: [44137, 45522], vp: [], vc: [], calls: [20971] };
  S[8251] = function (int0) {
    if (R.eq(R.vb(45522), 1)) {
      return 0;
    }
    var int1 = R.itemParam(int0, 396);
    int1 = R.call(20971, [int1]);
    if (R.eq(R.vb(44137), 1)) {
      int1 = R.scale(int1, 100, ((100) - (20) | 0));
    }
    return int1;
  };
  V[8261] = { vb: [61667], vp: [12314], vc: [], calls: [18327] };
  S[8261] = function (int0, int1, int2, int3, int4, int5, int6, int7, int8, int9, int10, int11, int12, int13, int14, int15, int16, int17, int18, int19, int20, int21, int22, int23, int24, int25, int26, int27, string0, string1) {
    var int28 = 1;
    var int29 = 0;
    var int30 = (-1 | 0);
    if (((R.eq(R.vp(12314), 2) && (R.vb(61667) > 0)) && (!R.eq(int1, (-1 | 0))))) {
      int30 = R.itemParam(int1, 3203);
      if (((!R.eq(int30, (-1 | 0))) && R.eq(R.structParam(int30, 9473), 1))) {
        int29 = ((int29) + (R.vb(61667)) | 0);
      }
    }
    if (R.eq(R.call(18327, [int1]), 1)) {
      return [((((int0) + (int28) | 0)) + (int29) | 0), string0, 1];
    }
    var string2 = "";
    var int31 = 0;
    if ((((!R.eq(int1, (-1 | 0))) && (R.invTotal(94, int1) > 0)) || (((!R.eq(int2, (-1 | 0))) && (R.invTotal(94, int2) > 0)) || (((!R.eq(int3, (-1 | 0))) && (R.invTotal(94, int3) > 0)) || (((!R.eq(int4, (-1 | 0))) && (R.invTotal(94, int4) > 0)) || (((!R.eq(int5, (-1 | 0))) && (R.invTotal(94, int5) > 0)) || (((!R.eq(int6, (-1 | 0))) && (R.invTotal(94, int6) > 0)) || (((!R.eq(int7, (-1 | 0))) && (R.invTotal(94, int7) > 0)) || (((!R.eq(int8, (-1 | 0))) && (R.invTotal(94, int8) > 0)) || (((!R.eq(int9, (-1 | 0))) && (R.invTotal(94, int9) > 0)) || (((!R.eq(int10, (-1 | 0))) && (R.invTotal(94, int10) > 0)) || (((!R.eq(int11, (-1 | 0))) && (R.invTotal(94, int11) > 0)) || (((!R.eq(int12, (-1 | 0))) && (R.invTotal(94, int12) > 0)) || (((!R.eq(int13, (-1 | 0))) && (R.invTotal(94, int13) > 0)) || (((!R.eq(int14, (-1 | 0))) && (R.invTotal(94, int14) > 0)) || (((!R.eq(int15, (-1 | 0))) && (R.invTotal(94, int15) > 0)) || (((!R.eq(int16, (-1 | 0))) && (R.invTotal(94, int16) > 0)) || (((!R.eq(int17, (-1 | 0))) && (R.invTotal(94, int17) > 0)) || (((!R.eq(int18, (-1 | 0))) && (R.invTotal(94, int18) > 0)) || (((!R.eq(int19, (-1 | 0))) && (R.invTotal(94, int19) > 0)) || (((!R.eq(int20, (-1 | 0))) && (R.invTotal(94, int20) > 0)) || (((!R.eq(int21, (-1 | 0))) && (R.invTotal(94, int21) > 0)) || (((!R.eq(int22, (-1 | 0))) && (R.invTotal(94, int22) > 0)) || (((!R.eq(int23, (-1 | 0))) && (R.invTotal(94, int23) > 0)) || (((!R.eq(int24, (-1 | 0))) && (R.invTotal(94, int24) > 0)) || (((!R.eq(int25, (-1 | 0))) && (R.invTotal(94, int25) > 0)) || (((!R.eq(int26, (-1 | 0))) && (R.invTotal(94, int26) > 0)) || ((!R.eq(int27, (-1 | 0))) && (R.invTotal(94, int27) > 0))))))))))))))))))))))))))))) {
      string2 = string0;
      var int0 = ((int0) + (((int28) + (int29) | 0)) | 0);
    } else {
      string2 = string1;
    }
    if ((((((((((((((((((((((((((((!R.eq(int1, (-1 | 0))) || (!R.eq(int2, (-1 | 0)))) || (!R.eq(int3, (-1 | 0)))) || (!R.eq(int4, (-1 | 0)))) || (!R.eq(int5, (-1 | 0)))) || (!R.eq(int6, (-1 | 0)))) || (!R.eq(int7, (-1 | 0)))) || (!R.eq(int8, (-1 | 0)))) || (!R.eq(int9, (-1 | 0)))) || (!R.eq(int10, (-1 | 0)))) || (!R.eq(int11, (-1 | 0)))) || (!R.eq(int12, (-1 | 0)))) || (!R.eq(int13, (-1 | 0)))) || (!R.eq(int14, (-1 | 0)))) || (!R.eq(int15, (-1 | 0)))) || (!R.eq(int16, (-1 | 0)))) || (!R.eq(int17, (-1 | 0)))) || (!R.eq(int18, (-1 | 0)))) || (!R.eq(int19, (-1 | 0)))) || (!R.eq(int20, (-1 | 0)))) || (!R.eq(int21, (-1 | 0)))) || (!R.eq(int22, (-1 | 0)))) || (!R.eq(int23, (-1 | 0)))) || (!R.eq(int24, (-1 | 0)))) || (!R.eq(int25, (-1 | 0)))) || (!R.eq(int26, (-1 | 0)))) || (!R.eq(int27, (-1 | 0))))) {
      int31 = 1;
    }
    return [int0, string2, int31];
  };
  V[8698] = { vb: [], vp: [], vc: [], calls: [] };
  S[8698] = function (int0) {
    return R.enumValue(0, 26, 7709, int0);
  };
  V[8744] = { vb: [], vp: [12655], vc: [], calls: [7495] };
  S[8744] = function () {
    if (R.eq(R.call(7495, []), 0)) {
      return 0;
    }
    var int0 = R.invObj(94, 3);
    if (R.eq(R.itemParam(int0, 2825), 0)) {
      return 0;
    }
    if ((R.vp(12655) >= 4)) {
      return 1;
    }
    return 0;
  };
  V[8755] = { vb: [], vp: [], vc: [], calls: [1569, 1764, 2258] };
  S[8755] = function (int0) {
    switch (R.key(int0)) {
      case 58451:
      {
        return 70;
      }
      case 5514:
      case 5515:
      {
        return R.call(2258, []);
      }
      case 5512:
      case 5513:
      {
        return R.call(1764, []);
      }
      case 5510:
      case 5511:
      {
        return R.call(1569, []);
      }
      case 5509:
      {
        return 3;
      }
    }
    return 0;
  };
  V[8942] = { vb: [], vp: [], vc: [], calls: [7241, 17172] };
  S[8942] = function () {
    var int0 = 0;
    var int1 = R.invSize(94);
    var int2 = (-1 | 0);
    var int3 = (-1 | 0);
    var int4 = (-1 | 0);
    var int5 = (-1 | 0);
    while (((int5 = (int5 + (1)) | 0) < int1)) {
      int2 = R.invObj(94, int5);
      if ((!R.eq(int2, (-1 | 0)))) {
        int3 = R.call(7241, [int2]);
        if (R.eq(int3, 5)) {
          int4 = R.call(17172, [int2, int3]);
          if (R.eq(int4, 2)) {
            int0 = ((int0) + (R.idiv(R.itemParam(int2, 641), 10)) | 0);
          }
        }
      }
    }
    return int0;
  };
  V[8944] = { vb: [], vp: [], vc: [], calls: [] };
  S[8944] = function () {
    return R.scale(R.stat(2), 100, 5);
  };
  V[8945] = { vb: [], vp: [], vc: [], calls: [] };
  S[8945] = function () {
    return R.scale(R.stat(0), 100, 135);
  };
  V[9279] = { vb: [], vp: [], vc: [], calls: [8261] };
  S[9279] = function (int0) {
    var int1 = R.structParam(int0, 4911);
    var int2 = R.structParam(int0, 4943);
    var int3 = R.structParam(int0, 4944);
    var int4 = R.structParam(int0, 4945);
    var int5 = R.structParam(int0, 4946);
    var int6 = R.structParam(int0, 4947);
    var int7 = R.structParam(int0, 5117);
    var int8 = R.structParam(int0, 5118);
    var int9 = R.structParam(int0, 5119);
    var int10 = R.structParam(int0, 5124);
    var int11 = R.structParam(int0, 5125);
    var int12 = R.structParam(int0, 5126);
    var int13 = R.structParam(int0, 5127);
    var int14 = R.structParam(int0, 5128);
    var int15 = R.structParam(int0, 5129);
    var int16 = R.structParam(int0, 5130);
    var int17 = R.structParam(int0, 5131);
    var int18 = R.structParam(int0, 5132);
    var int19 = R.structParam(int0, 5133);
    var int20 = R.structParam(int0, 5134);
    var int21 = R.structParam(int0, 5135);
    var int22 = R.structParam(int0, 5136);
    var int23 = R.structParam(int0, 5137);
    var int24 = R.structParam(int0, 5138);
    var int25 = R.structParam(int0, 5155);
    var int26 = R.structParam(int0, 5385);
    var int27 = R.structParam(int0, 5386);
    var int28 = 0;
    var string0 = "";
    var int29 = 0;
    [int28, string0, int29] = R.call(8261, [0, int1, int2, int3, int4, int5, int6, int7, int8, int9, int10, int11, int12, int13, int14, int15, int16, int17, int18, int19, int20, int21, int22, int23, int24, int25, int26, int27, string0, string0]);
    return [int28, int29];
  };
  V[9670] = { vb: [], vp: [], vc: [], calls: [] };
  S[9670] = function (int0, int1) {
    if (R.eq(int0, int1)) {
      return 1;
    }
    return 0;
  };
  V[9681] = { vb: [], vp: [], vc: [], calls: [9688] };
  S[9681] = function (int0) {
    if (R.eq(R.mapMembers(), 0)) {
      return 0;
    }
    var int1 = R.invObj(94, 1);
    switch (R.key(int0)) {
      case 1:
      {
        if (R.eq(R.call(9688, [int1, 8]), 1)) {
          return 1;
        }
        break;
      }
      case 3:
      {
        if (R.eq(R.call(9688, [int1, 9]), 1)) {
          return 1;
        }
        break;
      }
      case 2:
      {
        if (R.eq(R.call(9688, [int1, 10]), 1)) {
          return 1;
        }
        break;
      }
      case 7:
      {
        if (R.eq(R.call(9688, [int1, 21]), 1)) {
          return 1;
        }
        break;
      }
    }
    return 0;
  };
  V[9688] = { vb: [], vp: [], vc: [], calls: [] };
  S[9688] = function (int0, int1) {
    if (R.eq(R.itemParam(int0, 2881), int1)) {
      return 1;
    }
    if (R.eq(R.itemParam(int0, 8591), int1)) {
      return 1;
    }
    if (R.eq(R.itemParam(int0, 8592), int1)) {
      return 1;
    }
    if (R.eq(R.itemParam(int0, 8902), int1)) {
      return 1;
    }
    return 0;
  };
  V[9689] = { vb: [], vp: [], vc: [], calls: [9692, 9695] };
  S[9689] = function (int0, int1, int2) {
    return [R.call(9692, [int0, int1, int2]), R.call(9695, [int0, int1, int2])];
  };
  V[9692] = { vb: [], vp: [], vc: [], calls: [9693] };
  S[9692] = function (int0, int1, int2) {
    return R.enumValue(0, 33, 16608, R.call(9693, [int0, int1, int2]));
  };
  V[9693] = { vb: [], vp: [], vc: [], calls: [] };
  S[9693] = function (int0, int1, int2) {
    switch (R.key(int2)) {
      case 0:
      {
        return R.invVar(int0, int1, 50372);
      }
      case 1:
      {
        return R.invVar(int0, int1, 50374);
      }
      case 2:
      {
        return R.invVar(int0, int1, 50378);
      }
    }
    return (-1 | 0);
  };
  V[9695] = { vb: [], vp: [], vc: [], calls: [] };
  S[9695] = function (int0, int1, int2) {
    switch (R.key(int2)) {
      case 0:
      {
        return R.invVar(int0, int1, 50373);
      }
      case 1:
      {
        return R.invVar(int0, int1, 50375);
      }
      case 2:
      {
        return R.invVar(int0, int1, 50376);
      }
    }
    return 0;
  };
  V[9715] = { vb: [], vp: [], vc: [], calls: [9692] };
  S[9715] = function (int0, int1, string0) {
    var int2 = (-1 | 0);
    if (((R.eq(R.itemCategory(R.invObj(94, 13)), 5087) && R.eq(R.itemHasVarobj(R.invObj(94, 13)), 1)) && (R.itemParam(R.invObj(94, 13), 8605) > 1))) {
      switch (R.key(int0)) {
        case 45801:
        {
          int2 = R.call(9692, [94, 13, 0]);
          break;
        }
        case 45802:
        {
          int2 = R.call(9692, [94, 13, 1]);
          break;
        }
      }
      if ((!R.eq(int2, (-1 | 0)))) {
        var string0 = `${R.s(string0)}<br><br><col=ffffff>Ammo:</col> ${R.s(R.itemName(int2))}`;
      }
    }
    return string0;
  };
  V[9716] = { vb: [], vp: [], vc: [], calls: [9689, 9693] };
  S[9716] = function (int0, int1, int2, int3, int4, int5, int6) {
    if (((!R.eq(R.invObj(int1, int2), int0)) || R.eq(R.invNum(int1, int2), 0))) {
      return int5;
    }
    if (R.eq(R.itemHasVarobj(int0), 0)) {
      return int5;
    }
    var int7 = 0;
    if (((int5 > 0) && R.eq(R.ifNone(int4, ((int5) - (1) | 0)), 1))) {
      int7 = ((R.ifZero()) + (R.ifZero()) | 0);
    }
    var int8 = 0;
    var int9 = R.itemParam(int0, 8605);
    var int10 = (-1 | 0);
    while (((int10 = (int10 + (1)) | 0) < int9)) {
      if ((R.call(9693, [int1, int2, int10]) > 0)) {
        int8 = ((int8) + (1) | 0);
      }
    }
    if (R.eq(int8, 0)) {
      return int5;
    }
    var int11 = 2;
    var int12 = R.ifNoop(int3);
    var int13 = ((((Math.imul(int8, ((36) + (int11) | 0))) - (int11) | 0)) + (Math.imul(2, 16)) | 0);
    if ((int12 < int13)) {
      int12 = R.min(int13, 300);
    }
    var int14 = ((((32) + (int7) | 0)) + (16) | 0);
    R.ifNoop(int12, int14, 0, 0, int3);
    var int15 = R.idiv(((((36) - (Math.imul(int8, ((36) + (int11) | 0))) | 0)) + (int11) | 0), 2);
    int7 = ((int7) + (int11) | 0);
    var int16 = (-1 | 0);
    var int17 = 0;
    var int18 = R.invVar(int1, int2, 50377);
    int10 = (-1 | 0);
    while (((int10 = (int10 + (1)) | 0) < int9)) {
      [int16, int17] = R.call(9689, [int1, int2, int10]);
      if ((!R.eq(int16, (-1 | 0)))) {
        R.ifNoop(int4, 5, ((int5 = (int5 + (1)) | 0) - (1)));
        R.ifNoop(36, 32, 0, 0);
        R.ifNoop(int15, int7, 1, 0);
        R.ifNoop(int16, int17);
        if (((int8 > 1) && R.eq(int18, int10))) {
          R.ifNoop(2);
        }
        int15 = ((int15) + (((36) + (int11) | 0)) | 0);
      }
    }
    return int5;
  };
  V[9964] = { vb: [], vp: [], vc: [], calls: [] };
  S[9964] = function (int0) {
    if (R.eq(int0, (-1 | 0))) {
      return (-1 | 0);
    }
    var int1 = R.structParam(int0, 4397);
    if (((int1 < 0) || (int1 >= R.invSize(675)))) {
      return (-1 | 0);
    }
    return R.invObj(675, int1);
  };
  V[10005] = { vb: [], vp: [], vc: [], calls: [] };
  S[10005] = function (int0, int1) {
    var int2 = 0;
    switch (R.key(int1)) {
      case 3:
      {
        int2 = R.itemParam(int0, 965);
        break;
      }
      case 1:
      {
        int2 = R.itemParam(int0, 641);
        break;
      }
      case 2:
      {
        int2 = R.itemParam(int0, 643);
        break;
      }
      case 7:
      {
        int2 = R.itemParam(int0, 8881);
        break;
      }
    }
    return int2;
  };
  V[10021] = { vb: [], vp: [], vc: [], calls: [] };
  S[10021] = function (int0) {
    if ((int0 > 0)) {
      return 1;
    }
    return 0;
  };
  V[10081] = { vb: [], vp: [], vc: [], calls: [] };
  S[10081] = function (int0) {
    switch (R.key(int0)) {
      case 35051:
      {
        return "10% XP bonus to all combat skills for 15 minutes.";
      }
      case 35052:
      {
        return "10% XP bonus to all artisan skills for 15 minutes.";
      }
      case 35053:
      {
        return "10% XP bonus to all gatherer skills for 15 minutes.";
      }
      case 35054:
      {
        return "10% XP bonus to all support skills for 15 minutes.";
      }
      case 51729:
      {
        return "Drinking this will aid your ability to delve into the '<col=FFFFFF>Dungeoneering Hole</col>' activity.<br>XP gain is increased by <col=FFFFFF>+10%</col>.<br>You can now complete '<col=FFFFFF>60 floors</col>' before having to break.<br>Doing the activity won't increase your temperature gauge.<br>This effect lasts <col=FFFFFF>15 mins</col>.";
      }
      case 51732:
      {
        return "Drinking this will aid your ability to fish within the '<col=FFFFFF>Rock Pools</col>'.<br>XP gain is increased by <col=FFFFFF>+10%</col>.<br>This effect lasts <col=FFFFFF>15 mins</col>.";
      }
      case 51730:
      {
        return "Drinking this will aid your prowess at the '<col=FFFFFF>Hook a Duck</col>' activity.<br>XP gain is increased by <col=FFFFFF>+10%</col>.This effect lasts <col=FFFFFF>15 mins</col>.";
      }
      case 51731:
      {
        return "Drinking this will aid your skill at picking coconuts from the '<col=FFFFFF>Palm Tree Farming</col>' activity.<br>XP gain is increased by <col=FFFFFF>+10%</col>.<br>Picking from trees no longer depletes fruit from the tree.<br>This effect lasts <col=FFFFFF>15 mins</col>.";
      }
      case 51733:
      {
        return "Drinking this will aid your skill at the '<col=FFFFFF>Sandcastle Building</col>' activity.<br>XP gain is increased by <col=FFFFFF>+10%</col>.<br>You can shovel sand up to <col=FFFFFF>60 times</col> before you need a rest.<br>This effect lasts <col=FFFFFF>15 mins</col>.";
      }
    }
    return "";
  };
  V[10159] = { vb: [], vp: [], vc: [], calls: [17720] };
  S[10159] = function (string0) {
    var string0 = `${R.s(string0)}- <sprite=23726><nbsp><col=ffffff>${R.s(R.structParam(52795, 2794))}:</col> Deals ${R.s(R.call(17720, [52795, 45, ((45) + (10) | 0), 0, 1]))} per hit.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(2, 10))}</col> hits.`;
    return string0;
  };
  V[10165] = { vb: [], vp: [], vc: [], calls: [15974] };
  S[10165] = function () {
    return R.call(15974, [1, 0]);
  };
  V[10184] = { vb: [], vp: [], vc: [], calls: [] };
  S[10184] = function () {
    var string0 = "Provides limited teleports to the Varrock Grand Exchange";
    if ((vp2236_q_throne_of_miscellania_progress >= 100)) {
      string0 = `${R.s(string0)} and Miscellania`;
    }
    string0 = `${R.s(string0)}. Can be charged at the Fountain of Heroes in the Heroes' Guild, the Tears of Seren in Prifddinas or a geyser titan familiar.`;
    return string0;
  };
  V[10238] = { vb: [45345, 45351, 45353], vp: [], vc: [], calls: [] };
  S[10238] = function (int0, string0) {
    var string1 = "";
    switch (R.key(int0)) {
      case 14688:
      {
        switch (R.key(R.vb(45353))) {
          case 1:
          {
            string1 = `<br><br>No Fear (tier 1) - increases the critical hit chance of Meteor Strike by <col=ffffff>${R.s(R.str(R.idiv(200, 10), 10))}%</col>.`;
            break;
          }
          case 2:
          {
            string1 = `<br><br>No Fear (tier 2) - increases the critical hit chance of Meteor Strike by <col=ffffff>${R.s(R.str(R.idiv(400, 10), 10))}%</col>.`;
            break;
          }
        }
        break;
      }
      case 14719:
      {
        switch (R.key(R.vb(45351))) {
          case 1:
          {
            string1 = "<br><br>Armoured Hide (tier 1) - increases the duration of Barricade by <col=ffffff>1.8</col> seconds.";
            break;
          }
          case 2:
          {
            string1 = "<br><br>Armoured Hide (tier 2) - increases the duration of Barricade by <col=ffffff>3.6</col> seconds.";
            break;
          }
        }
        break;
      }
      case 14690:
      {
        switch (R.key(R.vb(45345))) {
          case 1:
          {
            string1 = `<br><br>Stubborn (tier 1) - increases each heal from Regenerate to <col=ffffff>${R.s(R.str(3, 10))}%</col> of your maximum life points.`;
            break;
          }
          case 2:
          {
            string1 = `<br><br>Stubborn (tier 2) - increases each heal from Regenerate to <col=ffffff>${R.s(R.str(4, 10))}%</col> of your maximum life points.`;
            break;
          }
        }
        break;
      }
    }
    if ((R.len(string1) > 0)) {
      var string0 = `${R.s(string0)}${R.s(string1)}`;
    }
    return string0;
  };
  V[10240] = { vb: [], vp: [], vc: [], calls: [5275, 5335] };
  S[10240] = function (int0) {
    var string0 = "";
    var int1 = R.call(5275, [int0]);
    var int2 = R.call(5335, [int0]);
    if (((int1 > 0) || (int2 > 0))) {
      string0 = "Doesn't drain <col=00ffff>adrenaline</col>.";
    }
    return string0;
  };
  V[10247] = { vb: [], vp: [], vc: [], calls: [15974] };
  S[10247] = function () {
    return R.call(15974, [5, 250]);
  };
  V[10248] = { vb: [], vp: [], vc: [], calls: [15974] };
  S[10248] = function () {
    return R.call(15974, [10, 500]);
  };
  V[10249] = { vb: [], vp: [], vc: [], calls: [15974] };
  S[10249] = function () {
    return R.call(15974, [20, 1000]);
  };
  V[10250] = { vb: [], vp: [], vc: [], calls: [15974] };
  S[10250] = function () {
    return R.call(15974, [30, 1500]);
  };
  V[10251] = { vb: [], vp: [], vc: [], calls: [15974] };
  S[10251] = function () {
    return R.call(15974, [50, 2500]);
  };
  V[10252] = { vb: [], vp: [], vc: [], calls: [15974] };
  S[10252] = function () {
    return R.call(15974, [100, 5000]);
  };
  V[10290] = { vb: [], vp: [], vc: [], calls: [3509, 7235, 9692] };
  S[10290] = function (int0, int1, int2, int3, int4, int5, int6, string0) {
    if (((!R.eq(R.invObj(int1, int2), int0)) || R.eq(R.invNum(int1, int2), 0))) {
      return int6;
    }
    if (R.eq(R.itemHasVarobj(int0), 0)) {
      return int6;
    }
    var int7 = R.itemParam(int0, 8605);
    var int8 = (-1 | 0);
    var int9 = (-1 | 0);
    while (((int9 = (int9 + (1)) | 0) < int7)) {
      int8 = R.call(9692, [int1, int2, int9]);
      if ((!R.eq(int8, (-1 | 0)))) {
        var int6 = R.call(7235, [int3, int4, int5, int6, R.call(3509, [int8]), string0]);
      }
    }
    return int6;
  };
  V[10495] = { vb: [], vp: [], vc: [], calls: [15721] };
  S[10495] = function (int0) {
    var int1 = (-1 | 0);
    var int2 = R.enumValue(0, 0, 9183, int0);
    var int3 = int2;
    var int4 = R.call(15721, []);
    if ((!R.eq(int4, 0))) {
      int1 = R.enumValue(0, 26, 9182, int4);
      if ((!R.eq(int1, (-1 | 0)))) {
        int3 = R.enumValue(0, 0, int1, int0);
        if (R.eq(int3, (-1 | 0))) {
          int3 = int2;
        }
      }
    }
    return int3;
  };
  V[10536] = { vb: [], vp: [], vc: [], calls: [17709, 18560] };
  S[10536] = function (string0) {
    var string0 = `${R.s(string0)}- Attacks deal <col=ffffff>${R.s(R.str(90, 10))}%-${R.s(R.str(((90) + (20) | 0), 10))}%</col> of initial damage to up to ${R.s(R.call(18560, [9]))} within ${R.s(R.call(17709, [1]))} of the target.`;
    return string0;
  };
  V[10537] = { vb: [], vp: [], vc: [], calls: [15973, 18566] };
  S[10537] = function (string0) {
    var string0 = `${R.s(string0)}- Attacks apply a <sprite=30335><nbsp><col=ffffff>${R.s(R.structParam(1489, 2794))}</col> stack with each hit, storing <col=ffffff>${R.s(R.call(18566, [(-1 | 0), R.idiv(10, 10), 0]))}.`;
    string0 = `${R.s(string0)}<br>- <sprite=30335><nbsp><col=ffffff>${R.s(R.structParam(1489, 2794))}</col> deals <col=ffffff>100%</col> of the stored damage over <col=ffffff>${R.s(R.call(15973, [Math.imul(3, 5), 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>Maximum stacks: <col=ffffff>${R.s(R.str(250, 10))}</col>.`;
    string0 = `${R.s(string0)}<br><col=ffffff>${R.s(R.call(15973, [50, 1]))}</col> duration.`;
    return string0;
  };
  V[10622] = { vb: [], vp: [], vc: [], calls: [] };
  S[10622] = function () {
    var int0 = 0;
    if (R.eq(R.statBase(0), 99)) {
      int0 = ((int0) + (1) | 0);
    }
    if (R.eq(R.statBase(2), 99)) {
      int0 = ((int0) + (1) | 0);
    }
    if (R.eq(R.statBase(1), 99)) {
      int0 = ((int0) + (1) | 0);
    }
    if (R.eq(R.statBase(4), 99)) {
      int0 = ((int0) + (1) | 0);
    }
    if (R.eq(R.statBase(5), 99)) {
      int0 = ((int0) + (1) | 0);
    }
    if (R.eq(R.statBase(6), 99)) {
      int0 = ((int0) + (1) | 0);
    }
    if (R.eq(R.statBase(20), 99)) {
      int0 = ((int0) + (1) | 0);
    }
    if (R.eq(R.statBase(21), 99)) {
      int0 = ((int0) + (1) | 0);
    }
    if (R.eq(R.statBase(3), 99)) {
      int0 = ((int0) + (1) | 0);
    }
    if (R.eq(R.statBase(16), 99)) {
      int0 = ((int0) + (1) | 0);
    }
    if ((R.statBase(15) >= 99)) {
      int0 = ((int0) + (1) | 0);
    }
    if (R.eq(R.statBase(17), 99)) {
      int0 = ((int0) + (1) | 0);
    }
    if (R.eq(R.statBase(12), 99)) {
      int0 = ((int0) + (1) | 0);
    }
    if (R.eq(R.statBase(9), 99)) {
      int0 = ((int0) + (1) | 0);
    }
    if ((R.statBase(18) >= 99)) {
      int0 = ((int0) + (1) | 0);
    }
    if (R.eq(R.statBase(22), 99)) {
      int0 = ((int0) + (1) | 0);
    }
    if (R.eq(R.statBase(14), 99)) {
      int0 = ((int0) + (1) | 0);
    }
    if (R.eq(R.statBase(13), 99)) {
      int0 = ((int0) + (1) | 0);
    }
    if (R.eq(R.statBase(10), 99)) {
      int0 = ((int0) + (1) | 0);
    }
    if (R.eq(R.statBase(7), 99)) {
      int0 = ((int0) + (1) | 0);
    }
    if (R.eq(R.statBase(11), 99)) {
      int0 = ((int0) + (1) | 0);
    }
    if (R.eq(R.statBase(8), 99)) {
      int0 = ((int0) + (1) | 0);
    }
    if ((R.statBase(19) >= 99)) {
      int0 = ((int0) + (1) | 0);
    }
    if (R.eq(R.statBase(23), 99)) {
      int0 = ((int0) + (1) | 0);
    }
    if ((R.statBase(24) >= 99)) {
      int0 = ((int0) + (1) | 0);
    }
    if (R.eq(R.statBase(25), 99)) {
      int0 = ((int0) + (1) | 0);
    }
    if ((R.statBase(26) >= 99)) {
      int0 = ((int0) + (1) | 0);
    }
    if ((R.statBase(27) >= 99)) {
      int0 = ((int0) + (1) | 0);
    }
    if ((R.statBase(28) >= 99)) {
      int0 = ((int0) + (1) | 0);
    }
    return int0;
  };
  V[10628] = { vb: [20807], vp: [], vc: [], calls: [] };
  S[10628] = function () {
    if (R.eq(R.vb(20807), 1)) {
      return 1;
    }
    return 0;
  };
  V[10780] = { vb: [26131, 26132, 26133, 26134, 26135, 26136, 26137, 26138, 26139, 26140, 26141, 26142, 26143, 26144, 26145, 26146, 26147, 26148, 26149, 26150, 26151, 26152, 26153, 26154, 26155, 26156, 26157, 26158, 26159, 26160, 26161, 26162, 26163, 26164, 26165, 26166, 26167, 26168], vp: [], vc: [], calls: [10813, 10814] };
  S[10780] = function (int0) {
    var int1 = (-1 | 0);
    var int2 = (-1 | 0);
    switch (R.key(int0)) {
      case 0:
      {
        int1 = R.vb(26131);
        int2 = R.vb(26132);
        break;
      }
      case 1:
      {
        int1 = R.vb(26133);
        int2 = R.vb(26134);
        break;
      }
      case 2:
      {
        int1 = R.vb(26135);
        int2 = R.vb(26136);
        break;
      }
      case 3:
      {
        int1 = R.vb(26137);
        int2 = R.vb(26138);
        break;
      }
      case 4:
      {
        int1 = R.vb(26139);
        int2 = R.vb(26140);
        break;
      }
      case 5:
      {
        int1 = R.vb(26141);
        int2 = R.vb(26142);
        break;
      }
      case 6:
      {
        int1 = R.vb(26143);
        int2 = R.vb(26144);
        break;
      }
      case 7:
      {
        int1 = R.vb(26145);
        int2 = R.vb(26146);
        break;
      }
      case 8:
      {
        int1 = R.vb(26147);
        int2 = R.vb(26148);
        break;
      }
      case 9:
      {
        int1 = R.vb(26149);
        int2 = R.vb(26150);
        break;
      }
      case 10:
      {
        int1 = R.vb(26151);
        int2 = R.vb(26152);
        break;
      }
      case 11:
      {
        int1 = R.vb(26153);
        int2 = R.vb(26154);
        break;
      }
      case 12:
      {
        int1 = R.vb(26155);
        int2 = R.vb(26156);
        break;
      }
      case 13:
      {
        int1 = R.vb(26157);
        int2 = R.vb(26158);
        break;
      }
      case 14:
      {
        int1 = R.vb(26159);
        int2 = R.vb(26160);
        break;
      }
      case 15:
      {
        int1 = R.vb(26161);
        int2 = R.vb(26162);
        break;
      }
      case 16:
      {
        int1 = R.vb(26163);
        int2 = R.vb(26164);
        break;
      }
      case 17:
      {
        int1 = R.vb(26165);
        int2 = R.vb(26166);
        break;
      }
      case 18:
      {
        int1 = R.vb(26167);
        int2 = R.vb(26168);
        break;
      }
    }
    return [R.call(10813, [int1]), R.call(10814, [int2])];
  };
  V[10813] = { vb: [], vp: [], vc: [], calls: [] };
  S[10813] = function (int0) {
    if (R.eq(int0, 4095)) {
      return (-1 | 0);
    }
    return int0;
  };
  V[10814] = { vb: [], vp: [], vc: [], calls: [] };
  S[10814] = function (int0) {
    if (R.eq(int0, 1048575)) {
      return (-1 | 0);
    }
    return int0;
  };
  V[10881] = { vb: [], vp: [5050, 5051, 5052, 5053, 5054, 6537, 6538, 6762, 6763, 7105, 7106, 7645, 7646, 7849, 7850, 7851, 7852, 8170, 8228, 8353, 8550, 8692, 9014, 9111, 9112, 9202, 9203, 9461, 9549, 9550, 9718, 9743, 9780, 9846, 9872, 9873, 10162, 10301, 10312, 10379, 10398, 10399, 10622, 10639, 10640, 10641, 10779, 10780, 11327, 11453, 11454, 11455, 11456, 11471, 11580, 11586, 11587, 11696, 11697, 11698, 11699, 11905, 11976, 11988, 12029, 12085, 12177, 12178, 12209, 12228, 12429, 12436, 12527, 12528, 12796, 12872, 13487], vc: [], calls: [42, 2156, 12478] };
  S[10881] = function (int0) {
    var int1 = R.structParam(int0, 8684);
    if ((!R.eq(int1, (-1 | 0)))) {
      return R.call(42, [R.call(2156, [int1])]);
    }
    var int2 = R.structParam(int0, 823);
    if ((!R.eq(int2, (-1 | 0)))) {
      if (R.eq(R.achievementReqState(int2), (-2 | 0))) {
        return 1;
      }
      return 0;
    }
    var int3 = R.structParam(int0, 4744);
    if ((int3 < 0)) {
      return 1;
    }
    var int4 = R.mod(int3, 32);
    var int5 = R.idiv(int3, 32);
    switch (R.key(int5)) {
      case 0:
      {
        return R.testbit(R.vp(5050), int4);
      }
      case 1:
      {
        return R.testbit(R.vp(5051), int4);
      }
      case 2:
      {
        return R.testbit(R.vp(5052), int4);
      }
      case 3:
      {
        return R.testbit(R.vp(5053), int4);
      }
      case 4:
      {
        return R.testbit(R.vp(5054), int4);
      }
      case 5:
      {
        return R.testbit(R.vp(6537), int4);
      }
      case 6:
      {
        return R.testbit(R.vp(6538), int4);
      }
      case 7:
      {
        return R.testbit(R.vp(6762), int4);
      }
      case 8:
      {
        return R.testbit(R.vp(6763), int4);
      }
      case 9:
      {
        return R.testbit(R.vp(7105), int4);
      }
      case 10:
      {
        return R.testbit(R.vp(7106), int4);
      }
      case 11:
      {
        return R.testbit(R.vp(7645), int4);
      }
      case 12:
      {
        return R.testbit(R.vp(7646), int4);
      }
      case 13:
      {
        return R.testbit(R.vp(7849), int4);
      }
      case 14:
      {
        return R.testbit(R.vp(7850), int4);
      }
      case 15:
      {
        return R.testbit(R.vp(7851), int4);
      }
      case 16:
      {
        return R.testbit(R.vp(7852), int4);
      }
      case 17:
      {
        return R.testbit(R.vp(8170), int4);
      }
      case 18:
      {
        return R.testbit(R.vp(8228), int4);
      }
      case 19:
      {
        return R.testbit(R.vp(8353), int4);
      }
      case 20:
      {
        return R.testbit(R.vp(8550), int4);
      }
      case 21:
      {
        return R.testbit(R.vp(8692), int4);
      }
      case 22:
      {
        return R.testbit(R.vp(9014), int4);
      }
      case 23:
      {
        return R.testbit(R.vp(9111), int4);
      }
      case 24:
      {
        return R.testbit(R.vp(9112), int4);
      }
      case 25:
      {
        return R.testbit(R.vp(9202), int4);
      }
      case 26:
      {
        return R.testbit(R.vp(9203), int4);
      }
      case 27:
      {
        return R.testbit(R.vp(9461), int4);
      }
      case 28:
      {
        return R.testbit(R.vp(9549), int4);
      }
      case 29:
      {
        return R.testbit(R.vp(9550), int4);
      }
      case 30:
      {
        return R.testbit(R.vp(9718), int4);
      }
      case 31:
      {
        return R.testbit(R.vp(9743), int4);
      }
      case 32:
      {
        return R.testbit(R.vp(9780), int4);
      }
      case 33:
      {
        return R.testbit(R.vp(9846), int4);
      }
      case 34:
      {
        return R.testbit(R.vp(9872), int4);
      }
      case 35:
      {
        return R.testbit(R.vp(9873), int4);
      }
      case 36:
      {
        return R.testbit(R.vp(10162), int4);
      }
      case 37:
      {
        return R.testbit(R.vp(10301), int4);
      }
      case 38:
      {
        return R.testbit(R.vp(10312), int4);
      }
      case 39:
      {
        return R.testbit(R.vp(10379), int4);
      }
      case 40:
      {
        return R.testbit(R.vp(10398), int4);
      }
      case 41:
      {
        return R.testbit(R.vp(10399), int4);
      }
      case 42:
      {
        return R.testbit(R.vp(10622), int4);
      }
      case 43:
      {
        return R.testbit(R.vp(10639), int4);
      }
      case 44:
      {
        return R.testbit(R.vp(10640), int4);
      }
      case 45:
      {
        return R.testbit(R.vp(10641), int4);
      }
      case 46:
      {
        return R.testbit(R.vp(10779), int4);
      }
      case 47:
      {
        return R.testbit(R.vp(10780), int4);
      }
      case 48:
      {
        return R.testbit(R.vp(11327), int4);
      }
      case 49:
      {
        return R.testbit(R.vp(11453), int4);
      }
      case 50:
      {
        return R.testbit(R.vp(11454), int4);
      }
      case 51:
      {
        return R.testbit(R.vp(11455), int4);
      }
      case 52:
      {
        return R.testbit(R.vp(11456), int4);
      }
      case 53:
      {
        return R.testbit(R.vp(11471), int4);
      }
      case 54:
      {
        return R.testbit(R.vp(11580), int4);
      }
      case 55:
      {
        return R.testbit(R.vp(11586), int4);
      }
      case 56:
      {
        return R.testbit(R.vp(11587), int4);
      }
      case 57:
      {
        return R.testbit(R.vp(11696), int4);
      }
      case 58:
      {
        return R.testbit(R.vp(11697), int4);
      }
      case 59:
      {
        return R.testbit(R.vp(11698), int4);
      }
      case 60:
      {
        return R.testbit(R.vp(11699), int4);
      }
      case 61:
      {
        return R.testbit(R.vp(11905), int4);
      }
      case 62:
      {
        return R.testbit(R.vp(11976), int4);
      }
      case 63:
      {
        return R.testbit(R.vp(11988), int4);
      }
      case 64:
      {
        return R.testbit(R.vp(12029), int4);
      }
      case 65:
      {
        return R.testbit(R.vp(12085), int4);
      }
      case 66:
      {
        return R.testbit(R.vp(12177), int4);
      }
      case 67:
      {
        return R.testbit(R.vp(12178), int4);
      }
      case 68:
      {
        return R.testbit(R.vp(12209), int4);
      }
      case 69:
      {
        return R.testbit(R.vp(12228), int4);
      }
      case 70:
      {
        return R.testbit(R.vp(12429), int4);
      }
      case 71:
      {
        return R.testbit(R.vp(12436), int4);
      }
      case 72:
      {
        return R.testbit(R.vp(12527), int4);
      }
      case 73:
      {
        return R.testbit(R.vp(12528), int4);
      }
      case 74:
      {
        return R.testbit(R.vp(12796), int4);
      }
      case 75:
      {
        return R.testbit(R.vp(12872), int4);
      }
      case 76:
      {
        return R.testbit(R.vp(13487), int4);
      }
    }
    R.call(12478, [`Unable to get unlocked parent struct ${R.s(R.structParam(int0, 2533))} with ID ${R.s(R.str(int3, 10))} | Var no. ${R.s(R.str(int5, 10))}`]);
    return 1;
  };
  V[10903] = { vb: [], vp: [], vc: [], calls: [] };
  S[10903] = function () {
    if ((R.statBase(28) >= 106)) {
      return 4;
    }
    if ((R.statBase(28) >= 84)) {
      return 3;
    }
    if ((R.statBase(28) >= 52)) {
      return 2;
    }
    return 1;
  };
  V[11034] = { vb: [21558, 21559, 22947, 22948, 22949, 22950, 22951, 22952, 22953, 22954, 22955, 22957, 22959, 22961, 22963, 22964, 22965, 22966, 22967, 22968, 22969, 22971, 28643, 28644, 29858, 29859, 29860, 29861, 29862, 29863, 29864, 29865, 29866, 29868, 29870, 29872, 29874, 29875, 29876, 29877, 29878, 29879, 29880, 29882, 29883, 29884, 30844, 30845, 30846, 30847, 30852, 30853, 30854, 30855, 32624, 32625, 34869, 34870, 36178, 36179, 40566, 40567, 41049, 41050, 41051, 41052, 41053, 41054, 41055, 41056, 41057, 41058, 41059, 41060, 41450, 41451, 41452, 41453, 41454, 41455, 41456, 41457, 41458, 41459, 41460, 41461, 41547, 43415, 43416, 43417, 43418, 43419, 43420, 43421, 43422, 43423, 43424, 43425, 43426, 48687, 48688, 48689, 48690, 49295, 49296, 49728, 49730, 50126, 50333, 50335, 51440, 51441, 51442, 51443, 53283, 53284, 53285, 53286, 54675, 54677, 55731, 55733, 55735, 55737, 55739, 55741, 55987, 55988, 58188, 58190, 60353, 60354], vp: [], vc: [], calls: [11035, 13574] };
  S[11034] = function () {
    var int0 = R.call(13574, []);
    if ((int0 < 5000)) {
      return 0;
    }
    if (R.eq(R.call(11035, []), 0)) {
      return 0;
    }
    var int1 = R.enumCount(9029);
    var int2 = 100;
    var int3 = 1;
    var int4 = 1;
    while (((int4 <= int1) && (!R.eq(int3, 0)))) {
      switch (R.key(int4)) {
        case 1:
        {
          if (((R.vb(22969) < int2) && R.eq(R.vb(29880), 0))) {
            int3 = 0;
          }
          break;
        }
        case 2:
        {
          if (((R.vb(22949) < int2) && R.eq(R.vb(29860), 0))) {
            int3 = 0;
          }
          break;
        }
        case 3:
        {
          if (((R.vb(22968) < int2) && R.eq(R.vb(29879), 0))) {
            int3 = 0;
          }
          break;
        }
        case 4:
        {
          if (((R.vb(22950) < int2) && R.eq(R.vb(29861), 0))) {
            int3 = 0;
          }
          break;
        }
        case 5:
        {
          if (((R.vb(22957) < int2) && R.eq(R.vb(29868), 0))) {
            int3 = 0;
          }
          break;
        }
        case 6:
        {
          if (((R.vb(22953) < int2) && R.eq(R.vb(29864), 0))) {
            int3 = 0;
          }
          break;
        }
        case 7:
        {
          if (((R.vb(22954) < int2) && R.eq(R.vb(29865), 0))) {
            int3 = 0;
          }
          break;
        }
        case 8:
        {
          if (((R.vb(22959) < int2) && R.eq(R.vb(29870), 0))) {
            int3 = 0;
          }
          break;
        }
        case 9:
        {
          if (((R.vb(22947) < int2) && R.eq(R.vb(29858), 0))) {
            int3 = 0;
          }
          break;
        }
        case 10:
        {
          if (((R.vb(22964) < int2) && R.eq(R.vb(29875), 0))) {
            int3 = 0;
          }
          break;
        }
        case 11:
        {
          if (((R.vb(22965) < int2) && R.eq(R.vb(29876), 0))) {
            int3 = 0;
          }
          break;
        }
        case 12:
        {
          if (((R.vb(22951) < int2) && R.eq(R.vb(29862), 0))) {
            int3 = 0;
          }
          break;
        }
        case 13:
        {
          if (((R.vb(22948) < int2) && R.eq(R.vb(29859), 0))) {
            int3 = 0;
          }
          break;
        }
        case 14:
        {
          if (((R.vb(22961) < int2) && R.eq(R.vb(29872), 0))) {
            int3 = 0;
          }
          break;
        }
        case 15:
        {
          if (((R.vb(22963) < int2) && R.eq(R.vb(29874), 0))) {
            int3 = 0;
          }
          break;
        }
        case 16:
        {
          if (((R.vb(22966) < int2) && R.eq(R.vb(29877), 0))) {
            int3 = 0;
          }
          break;
        }
        case 17:
        {
          if (((R.vb(22967) < int2) && R.eq(R.vb(29878), 0))) {
            int3 = 0;
          }
          break;
        }
        case 18:
        {
          if (((R.vb(22955) < int2) && R.eq(R.vb(29866), 0))) {
            int3 = 0;
          }
          break;
        }
        case 19:
        {
          if (((R.vb(22952) < int2) && R.eq(R.vb(29863), 0))) {
            int3 = 0;
          }
          break;
        }
        case 20:
        {
          if (((R.vb(22971) < int2) && R.eq(R.vb(29882), 0))) {
            int3 = 0;
          }
          break;
        }
        case 21:
        {
          if (((R.vb(28643) < int2) && R.eq(R.vb(29883), 0))) {
            int3 = 0;
          }
          break;
        }
        case 22:
        {
          if (((R.vb(28644) < int2) && R.eq(R.vb(29884), 0))) {
            int3 = 0;
          }
          break;
        }
        case 26:
        {
          if (((R.vb(30846) < int2) && R.eq(R.vb(30854), 0))) {
            int3 = 0;
          }
          break;
        }
        case 25:
        {
          if (((R.vb(30847) < int2) && R.eq(R.vb(30855), 0))) {
            int3 = 0;
          }
          break;
        }
        case 23:
        {
          if (((R.vb(30845) < int2) && R.eq(R.vb(30853), 0))) {
            int3 = 0;
          }
          break;
        }
        case 24:
        {
          if (((R.vb(30844) < int2) && R.eq(R.vb(30852), 0))) {
            int3 = 0;
          }
          break;
        }
        case 27:
        {
          if (((R.vb(32624) < int2) && R.eq(R.vb(32625), 0))) {
            int3 = 0;
          }
          break;
        }
        case 28:
        {
          if (((R.vb(34869) < int2) && R.eq(R.vb(34870), 0))) {
            int3 = 0;
          }
          break;
        }
        case 29:
        {
          if (((R.vb(36178) < int2) && R.eq(R.vb(36179), 0))) {
            int3 = 0;
          }
          break;
        }
        case 30:
        {
          if (((R.vb(40566) < int2) && R.eq(R.vb(40567), 0))) {
            int3 = 0;
          }
          break;
        }
        case 41:
        {
          if (((R.vb(49295) < int2) && R.eq(R.vb(49296), 0))) {
            int3 = 0;
          }
          break;
        }
        case 42:
        {
          if (((R.vb(49728) < int2) && R.eq(R.vb(49730), 0))) {
            int3 = 0;
          }
          break;
        }
        case 43:
        {
          if (((R.vb(41547) < int2) && R.eq(R.vb(50126), 0))) {
            int3 = 0;
          }
          break;
        }
        case 44:
        {
          if (((R.vb(21558) < int2) && R.eq(R.vb(21559), 0))) {
            int3 = 0;
          }
          break;
        }
        case 45:
        {
          if (((R.vb(50333) < int2) && R.eq(R.vb(50335), 0))) {
            int3 = 0;
          }
          break;
        }
        case 47:
        {
          if (((R.vb(53283) < int2) && R.eq(R.vb(53285), 0))) {
            int3 = 0;
          }
          break;
        }
        case 48:
        {
          if (((R.vb(53284) < int2) && R.eq(R.vb(53286), 0))) {
            int3 = 0;
          }
          break;
        }
        case 49:
        {
          if (((R.vb(54675) < int2) && R.eq(R.vb(54677), 0))) {
            int3 = 0;
          }
          break;
        }
        case 50:
        {
          if (((R.vb(55731) < int2) && R.eq(R.vb(55737), 0))) {
            int3 = 0;
          }
          break;
        }
        case 51:
        {
          if (((R.vb(55733) < int2) && R.eq(R.vb(55739), 0))) {
            int3 = 0;
          }
          break;
        }
        case 52:
        {
          if (((R.vb(55735) < int2) && R.eq(R.vb(55741), 0))) {
            int3 = 0;
          }
          break;
        }
        case 54:
        {
          if (((R.vb(58188) < int2) && R.eq(R.vb(58190), 0))) {
            int3 = 0;
          }
          break;
        }
        case 55:
        {
          if (((R.vb(60353) < int2) && R.eq(R.vb(60354), 0))) {
            int3 = 0;
          }
          break;
        }
        case 40:
        {
          if ((((((R.vb(48687)) + (R.vb(48688)) | 0) < int2) && R.eq(R.vb(48689), 0)) && R.eq(R.vb(48690), 0))) {
            int3 = 0;
          }
          break;
        }
        case 31:
        {
          if ((((((R.vb(41050)) + (R.vb(41049)) | 0) < int2) && R.eq(R.vb(41056), 0)) && R.eq(R.vb(41055), 0))) {
            int3 = 0;
          }
          break;
        }
        case 32:
        {
          if ((((((R.vb(41052)) + (R.vb(41051)) | 0) < int2) && R.eq(R.vb(41058), 0)) && R.eq(R.vb(41057), 0))) {
            int3 = 0;
          }
          break;
        }
        case 33:
        {
          if ((((((R.vb(41054)) + (R.vb(41053)) | 0) < int2) && R.eq(R.vb(41060), 0)) && R.eq(R.vb(41059), 0))) {
            int3 = 0;
          }
          break;
        }
        case 34:
        {
          if ((((((R.vb(41451)) + (R.vb(41450)) | 0) < int2) && R.eq(R.vb(41457), 0)) && R.eq(R.vb(41456), 0))) {
            int3 = 0;
          }
          break;
        }
        case 35:
        {
          if ((((((R.vb(41453)) + (R.vb(41452)) | 0) < int2) && R.eq(R.vb(41459), 0)) && R.eq(R.vb(41458), 0))) {
            int3 = 0;
          }
          break;
        }
        case 36:
        {
          if ((((((R.vb(41455)) + (R.vb(41454)) | 0) < int2) && R.eq(R.vb(41461), 0)) && R.eq(R.vb(41460), 0))) {
            int3 = 0;
          }
          break;
        }
        case 37:
        {
          if ((((((R.vb(43416)) + (R.vb(43415)) | 0) < int2) && R.eq(R.vb(43422), 0)) && R.eq(R.vb(43421), 0))) {
            int3 = 0;
          }
          break;
        }
        case 38:
        {
          if ((((((R.vb(43418)) + (R.vb(43417)) | 0) < int2) && R.eq(R.vb(43424), 0)) && R.eq(R.vb(43423), 0))) {
            int3 = 0;
          }
          break;
        }
        case 39:
        {
          if ((((((R.vb(43420)) + (R.vb(43419)) | 0) < int2) && R.eq(R.vb(43426), 0)) && R.eq(R.vb(43425), 0))) {
            int3 = 0;
          }
          break;
        }
        case 46:
        {
          if ((((((R.vb(51440)) + (R.vb(51441)) | 0) < int2) && R.eq(R.vb(51442), 0)) && R.eq(R.vb(51443), 0))) {
            int3 = 0;
          }
          break;
        }
        case 53:
        {
          if (((R.vb(55987) < int2) && R.eq(R.vb(55988), 0))) {
            int3 = 0;
          }
          break;
        }
      }
      int4 = ((int4) + (1) | 0);
    }
    return int3;
  };
  V[11035] = { vb: [], vp: [], vc: [], calls: [178, 12478] };
  S[11035] = function () {
    var int0 = 69;
    var int1 = R.call(178, []);
    if (R.eq(int1, int0)) {
      return 1;
    }
    if ((int1 > int0)) {
      R.call(12478, ["Player owns more bpets than expected - if new bpet has been added, $total_pets likely needs updating in [proc,bpets_unlocked_all]"]);
    }
    return 0;
  };
  V[11087] = { vb: [21565, 33710, 36378, 41299, 49291], vp: [2008, 2735, 10936, 11085, 12291], vc: [], calls: [3939, 4583, 6438, 7051, 7653, 11479, 11570, 12618, 13038, 13086, 13790, 14945, 15973, 16254, 16256, 17457, 17458, 17459, 17464, 17717, 17719, 17720, 17722, 18560, 18576, 18624, 19867, 19979, 20086, 20099] };
  S[11087] = function (int0) {
    var string0 = "";
    var int1 = (-1 | 0);
    var int2 = 0;
    switch (R.key(int0)) {
      case 45326:
      {
        string0 = ` - ${R.s(R.str(10, 10))}% Support XP Boost`;
        break;
      }
      case 45325:
      {
        string0 = ` - ${R.s(R.str(10, 10))}% Gatherer XP Boost`;
        break;
      }
      case 45323:
      {
        string0 = ` - ${R.s(R.str(10, 10))}% Combat XP Boost`;
        break;
      }
      case 45324:
      {
        string0 = ` - ${R.s(R.str(10, 10))}% Artisan XP Boost`;
        break;
      }
      case 21215:
      {
        int1 = R.invObj(94, 17);
        if ((!R.eq(int1, (-1 | 0)))) {
          string0 = `${R.s(R.itemName(int1))} Active`;
        }
        break;
      }
      case 33383:
      {
        int1 = R.invObj(94, 17);
        if (R.eq(int1, 37225)) {
          string0 = `${R.s(R.itemName(int1))} is boosting your combat skills`;
        } else {
          string0 = `${R.s(R.itemName(int1))} will activate on your next attack`;
        }
        break;
      }
      case 33384:
      {
        int1 = R.invObj(94, 17);
        if ((!R.eq(int1, (-1 | 0)))) {
          string0 = `${R.s(R.itemName(int1))} is reflecting ${R.s(R.str(R.idiv(100, 20), 10))}% of incoming damage`;
        }
        break;
      }
      case 34189:
      {
        string0 = `Crystal Shield Defence Active - You have <col=00ff00>${R.s(R.strLoc(R.vb(33710), 1))}</col> temporary life points, thanks to your Crystal Shield's absorption.`;
        break;
      }
      case 1626:
      {
        string0 = `The residual energy from the Stone of Jas is empowering you, increasing your damage by <col=33cc33>${R.s(R.str(R.vb(36378), 10))}%</col>.`;
        break;
      }
      case 39037:
      {
        if (R.eq(R.vp(2735), 1)) {
          string0 = "You have 1 cannonball remaining in your cannon.";
        } else {
          string0 = `You have ${R.s(R.str(R.vp(2735), 10))} cannonballs remaining in your cannon.`;
        }
        string0 = R.cat(string0, "<br>Damage: <col=FF00>100%</col>");
        break;
      }
      case 52342:
      {
        if (R.eq(R.vp(2735), 1)) {
          string0 = "You have 1 meteorite cannonball remaining in your cannon.";
        } else {
          string0 = `You have ${R.s(R.str(R.vp(2735), 10))} meteorite cannonballs remaining in your cannon.`;
        }
        string0 = R.cat(string0, `<br>Damage: <col=FF00>${R.s(R.str(150, 10))}%</col>`);
        break;
      }
      case 40798:
      {
        int2 = ((((10080) - (((R.dateMinutes()) - (R.vp(2008)) | 0)) | 0)) + (1) | 0);
        if ((int2 > 0)) {
          string0 = `<br>This boost will expire in ${R.s(R.call(11479, [R.idiv(int2, 1440), R.idiv(R.mod(int2, 1440), 60), R.mod(int2, 60)]))}.`;
        }
        string0 = R.cat(`Attuning to the Avatar Habitat has granted you a <col=33cc33>${R.s(R.str(R.vb(41299), 10))}%</col> boost in resources gathered at Citadel skill plots.`, string0);
        break;
      }
      case 44996:
      {
        string0 = ` - You are taking an additional <col=33cc33>${R.s(R.str(Math.imul(R.vb(49291), 20), 10))}%</col> damage.`;
        break;
      }
      case 45275:
      {
        string0 = `Fungal Shield Active - You are shielded for <col=00ff00>${R.s(R.strLoc(R.vb(21565), 1))}</col> life points, by your Fungal Shield`;
        break;
      }
      case 1624:
      case 6850:
      {
        if (R.eq(R.invObj(94, 2), 44550)) {
          string0 = ` Remaining charges: ${R.s(R.str(R.invVar(94, 2, 30214), 10))}`;
        }
        break;
      }
      case 1489:
      {
        string0 = `<br>- <col=ffffff>${R.s(R.str(((((((((50) + (20) | 0)) + (15) | 0)) + (10) | 0)) + (5) | 0), 10))}%</col> of the total damage applied over <col=ffffff>${R.s(R.call(14945, [Math.imul(3, 5), 1]))}</col>.`;
        string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(5, 10))}</col> hits.`;
        string0 = `${R.s(string0)}<br>- <col=ffffff>Damage over time</col>.`;
        break;
      }
      case 48338:
      {
        string0 = `<br>- <col=ffffff>${R.s(R.str(25, 10))}%</col> of initial damage per hit every <col=ffffff>${R.s(R.call(14945, [3, 1]))}</col>.`;
        string0 = `${R.s(string0)}<br>- <col=ffffff>Damage over time</col>.`;
        string0 = `${R.s(string0)}<br>- <col=ffffff>On-death:</col> Applies <sprite=30098><nbsp><col=ffffff>${R.s(R.structParam(48338, 2794))}</col> to up to ${R.s(R.call(18560, [9]))} within <col=ffffff>${R.s(R.str(1, 10))} ${R.s(R.call(4583, [1, "tile", "tiles"]))}</col> for <col=ffffff>4</col> hits.`;
        break;
      }
      case 48333:
      {
        string0 = `<br><br>Maximum stacks: <col=ffffff>${R.s(R.str(R.call(17458, []), 10))}</col>.`;
        break;
      }
      case 48334:
      {
        string0 = `<br><br>Maximum stacks: <col=ffffff>${R.s(R.str(R.call(17459, []), 10))}</col>.`;
        break;
      }
      case 48335:
      {
        string0 = `<br><br>Maximum stacks: <col=ffffff>${R.s(R.str(R.call(17457, []), 10))}</col>.`;
        break;
      }
      case 32349:
      {
        string0 = `<br><br>Maximum stacks: <col=ffffff>${R.s(R.str(R.call(6438, []), 10))}</col>.`;
        break;
      }
      case 48345:
      {
        string0 = `<br>- <col=A788DD>Necromancy attacks</col> will execute the mark for fatal damage if life points drop below <col=ffffff>${R.s(R.strLoc(20, 1))}%</col>.`;
        break;
      }
      case 48343:
      {
        string0 = `<br>- <col=ffffff>${R.s(R.str(R.idiv(200, 10), 10))}%</col> chance to dodge incoming attacks.`;
        break;
      }
      case 49074:
      {
        string0 = "<br>- Reduces <col=ffffff>armour rating</col> for each stack.";
        break;
      }
      case 48350:
      {
        if (R.eq(R.vp(11085), R.call(17464, []))) {
          string0 = `<br>- Your <sprite=30015><nbsp><col=ffffff>${R.s(R.structParam(48293, 2794))} is empowered and will deal ${R.s(R.call(17717, [200]))}.`;
        } else {
          string0 = `<br>- At <col=ffffff>${R.s(R.str(R.call(17464, []), 10))}</col> stacks your <sprite=30015><nbsp><col=ffffff>${R.s(R.structParam(48293, 2794))} is empowered and deals ${R.s(R.call(17717, [200]))}</col>.`;
        }
        break;
      }
      case 52064:
      {
        if (R.eq(R.vp(12291), R.call(20086, []))) {
          string0 = `<br>- Your <sprite=30015><nbsp><col=ffffff>${R.s(R.structParam(48293, 2794))} is empowered and will generate <col=ffffff>${R.s(R.str(1, 10))} <sprite=30123><nbsp>${R.s(R.structParam(48334, 2794))}</col>.`;
        } else {
          string0 = `<br>- At <col=ffffff>${R.s(R.str(R.call(20086, []), 10))}</col> stacks your <sprite=30015><nbsp><col=ffffff>${R.s(R.structParam(48293, 2794))} is empowered and generates <col=ffffff>${R.s(R.str(1, 10))} <sprite=30123><nbsp><col=ffffff>${R.s(R.structParam(48334, 2794))}</col>.`;
        }
        break;
      }
      case 48351:
      {
        string0 = `<br><br>- <sprite=30076><nbsp><col=ffffff>${R.s(R.structParam(48296, 2794))}</col>, <sprite=30077><nbsp><col=ffffff>${R.s(R.structParam(48297, 2794))}</col>, <sprite=30074><nbsp><col=ffffff>${R.s(R.structParam(48314, 2794))}:</col> Immediately readies <sprite=30127><nbsp><col=ffffff>${R.s(R.structParam(48350, 2794))}</col>.`;
        break;
      }
      case 52065:
      {
        string0 = `<br><br>- <sprite=30080><nbsp><col=ffffff>${R.s(R.structParam(48298, 2794))}</col>, <sprite=30082><nbsp><col=ffffff>${R.s(R.structParam(48299, 2794))}</col>, <sprite=30088><nbsp><col=ffffff>${R.s(R.structParam(48301, 2794))}, <sprite=30084><nbsp><col=ffffff>${R.s(R.structParam(48311, 2794))}:</col> Immediately readies <sprite=35068><nbsp><col=ffffff>${R.s(R.structParam(52064, 2794))}</col>.`;
        break;
      }
      case 47801:
      {
        if (R.eq(R.vp(10936), 6)) {
          string0 = `<br>- Rasial's basic attack is empowered and will deal ${R.s(R.call(17717, [200]))}</col>.`;
        } else {
          string0 = `<br>- At ${R.s(R.str(6, 10))} stacks Rasial's basic attack is empowered and deals ${R.s(R.call(17717, [200]))}</col>.`;
        }
        break;
      }
      case 52802:
      {
        string0 = `<br>- <col=25AD37>Ranged attacks</col> generate ${R.s(R.call(11570, [50]))} with each hit.`;
        break;
      }
      case 52801:
      {
        string0 = `<br>- <col=25AD37>Ranged attacks</col> deal an additional ${R.s(R.call(17719, [49543, (-1 | 0), 20, 1, 1]))} with each hit.`;
        break;
      }
      case 46308:
      case 46309:
      {
        string0 = R.call(18624, [int0, string0]);
        break;
      }
      case 52778:
      {
        string0 = R.call(12618, [int0, string0]);
        break;
      }
      case 52779:
      {
        string0 = R.call(13038, [int0, string0]);
        break;
      }
      case 44820:
      {
        string0 = R.call(13086, [int0, string0]);
        break;
      }
      case 44066:
      {
        string0 = R.call(13790, [int0, string0]);
        break;
      }
      case 3636:
      {
        string0 = `<br>- Deal an additional <col=ffffff>${R.s(R.str(5, 10))}%</col> damage per stack.`;
        break;
      }
      case 49555:
      {
        string0 = `<br>- After <col=ffffff>${R.s(R.str(15, 10))}</col> attacks launch an additional attack that deals <col=ffffff>${R.s(R.call(7653, [100, 1, 1, 0, 1]))}%</col> of the total damage stored.`;
        break;
      }
      case 49563:
      {
        string0 = `<br>- <col=FFA11A>Melee abilities</col> deal ${R.s(R.call(17719, [49532, (-1 | 0), 24, 1, 1]))}.`;
        break;
      }
      case 49562:
      {
        string0 = `<br><br>Maximum stacks: <col=ffffff>${R.s(R.str(10, 10))}</col>.`;
        break;
      }
      case 50065:
      {
        string0 = `<br>- Damage is reduced by <col=ffffff>${R.s(R.str(75, 10))}%</col>.`;
        break;
      }
      case 50083:
      {
        string0 = `<br>- ${R.s(R.str(1, 10))}+ <sprite=33967><nbsp>${R.s(R.structParam(50083, 2794))} stack: Damage over time abilities have a ${R.s(R.str(30, 10))}% chance to become empowered, dealing all of their damage immediately and refreshing their cooldown.`;
        string0 = `${R.s(string0)}<br>- ${R.s(R.str(10, 10))}+ <sprite=33967><nbsp>${R.s(R.structParam(50083, 2794))} stacks: ${R.s(R.call(17722, [3, 1]))} deals ${R.s(R.str(300, 10))}% of <sprite=33967><nbsp>${R.s(R.structParam(50083, 2794))} stacks + ${R.s(R.str(100, 10))}% of your ${R.s(R.call(18576, [6, 1, 1]))} as bonus damage.`;
        string0 = `${R.s(string0)}<br>- ${R.s(R.str(25, 10))}+ <sprite=33967><nbsp>${R.s(R.structParam(50083, 2794))} stacks: Basic abilities generate an additional ${R.s(R.call(11570, [Math.imul(10, 6)]))} over ${R.s(R.call(15973, [6, 1]))}.`;
        break;
      }
      case 50070:
      {
        string0 = `<br>- Your next <sprite=23401><nbsp><col=ffffff>${R.s(R.structParam(14729, 2794))}</col> within <col=ffffff>${R.s(R.call(15973, [25, 1]))}</col> deals <col=ffffff>${R.s(R.str(40, 10))}%</col> increased damage.`;
        break;
      }
      case 51272:
      {
        string0 = R.call(19867, [string0]);
        break;
      }
      case 53077:
      {
        string0 = R.call(16254, [string0]);
        break;
      }
      case 53078:
      {
        string0 = R.call(16256, [string0]);
        break;
      }
      case 44040:
      {
        string0 = R.call(3939, [string0]);
        break;
      }
      case 51665:
      {
        string0 = R.call(19979, [int0, string0]);
        break;
      }
      case 51848:
      {
        string0 = `<br>- Deals damage every <col=ffffff>${R.s(R.call(14945, [3, 1]))}</col> and generates a stack.`;
        string0 = `${R.s(string0)}<br>- Can be transferred to friendly targets.`;
        string0 = `${R.s(string0)}<br>- Damage is increased with each hit, dealing fatal damage at maximum stacks.`;
        string0 = `${R.s(string0)}<br><br>Maximum stacks: <col=ffffff>${R.s(R.str(8, 10))}</col>.`;
        break;
      }
      case 52061:
      {
        string0 = R.call(20099, [string0]);
        break;
      }
      case 45563:
      {
        string0 = R.call(7051, [int0, string0]);
        break;
      }
      case 45447:
      {
        string0 = `<br><col=ffffff>On critical strike:</col> Launch a <col=ffffff>Lightning Surge</col> at your target dealing ${R.s(R.call(17720, [int0, 70, ((70) + (20) | 0), 0, 1]))}.`;
        break;
      }
    }
    return string0;
  };
  V[11088] = { vb: [3305, 24877, 24878, 24879, 27395, 29061, 30395, 35528, 41298, 43240, 45289, 47357, 47856, 48835, 51034, 51872, 51873, 54950, 55991, 55992, 57161, 57163], vp: [1029, 1030, 1031, 1032, 1033, 1034, 1035, 2008, 9666, 10264, 10534, 10537, 10538, 10539, 10540, 10541, 10543, 10545, 11059, 11306, 11307], vc: [4276, 4277, 4278, 6897, 7112], calls: [2544, 2659, 3111, 3721, 3940, 4583, 7247, 7653, 11479, 12476, 13402, 14791, 15408, 15409, 17727, 18429, 18431, 18710, 18713, 19131, 19675, 19979, 20701] };
  S[11088] = function (int0) {
    var string0 = "";
    var int1 = 0;
    switch (R.key(int0)) {
      case 28638:
      {
        string0 = `${R.s(R.itemName(R.invObj(94, 17)))} : ${R.s(R.itemDesc(R.invObj(94, 17)))}`;
        break;
      }
      case 43723:
      {
        string0 = `${R.s(R.structParam(int0, 2794))}<br>Your masterwork armour has stalled ${R.s(R.str(R.vb(43240), 10))} damage. You'll take ${R.s(R.str(R.call(2544, []), 10))} or more damage every <col=ffffff>1.2</col> seconds until the pool is empty.`;
        break;
      }
      case 40797:
      {
        int1 = ((((10080) - (((R.dateMinutes()) - (R.vp(2008)) | 0)) | 0)) + (1) | 0);
        if ((int1 > 0)) {
          string0 = `<br>This boost will expire in ${R.s(R.call(11479, [R.idiv(int1, 1440), R.idiv(R.mod(int1, 1440), 60), R.mod(int1, 60)]))}.`;
        }
        string0 = R.cat(`Attuning to the Avatar Habitat has granted you a <col=33cc33>${R.s(R.str(R.vb(41298), 10))}%</col> XP boost.`, string0);
        break;
      }
      case 1304:
      {
        string0 = `<col=FFFFFF>Cloud of destiny</col> - complete your fortune to get a reward from fate. <col=5A5AFF>${R.s(R.enumValue(0, 36, 12413, R.vb(35528)))}</col>`;
        break;
      }
      case 44226:
      {
        if ((R.vp(1029) >= 0)) {
          string0 = `Melee Attack Rating Boosted (+${R.s(R.str(R.vp(1029), 10))} Melee Accuracy)`;
        } else {
          string0 = `Melee Attack Rating Drained (${R.s(R.str(R.vp(1029), 10))}  Melee Accuracy)`;
        }
        break;
      }
      case 44229:
      {
        if ((R.vp(1031) >= 0)) {
          string0 = `Ranged Attack Rating Boosted (+${R.s(R.str(R.vp(1031), 10))} Ranged Accuracy)`;
        } else {
          string0 = `Ranged Attack Rating Drained (${R.s(R.str(R.vp(1031), 10))} Ranged Accuracy)`;
        }
        break;
      }
      case 44231:
      {
        if ((R.vp(1033) >= 0)) {
          string0 = `Magic Attack Rating Boosted (+${R.s(R.str(R.vp(1033), 10))} Magic Accuracy)`;
        } else {
          string0 = `Magic Attack Rating Drained (${R.s(R.str(R.vp(1033), 10))} Magic Accuracy)`;
        }
        break;
      }
      case 48286:
      {
        if ((R.vp(11306) >= 0)) {
          string0 = `Necromancy Attack Rating Boosted (+${R.s(R.str(R.vp(11306), 10))} Necromancy Accuracy)`;
        } else {
          string0 = `Necromancy Attack Rating Drained (${R.s(R.str(R.vp(11306), 10))} Necromancy Accuracy)`;
        }
        break;
      }
      case 44228:
      {
        if ((R.vp(1035) >= 0)) {
          string0 = `Defence Rating Boosted (${R.s(R.str(R.vp(1035), 10))})`;
        } else {
          string0 = `Defence Rating Drained (${R.s(R.str(R.vp(1035), 10))})`;
        }
        break;
      }
      case 44227:
      {
        if ((R.vp(1030) >= 0)) {
          string0 = `Melee Strength Rating Boosted (+${R.s(R.str(R.vp(1030), 10))}%)`;
        } else {
          string0 = `Melee Strength Rating Drained (${R.s(R.str(R.vp(1030), 10))}%)`;
        }
        break;
      }
      case 44230:
      {
        if ((R.vp(1032) >= 0)) {
          string0 = `Ranged Strength Rating Boosted (+${R.s(R.str(R.vp(1032), 10))}%)`;
        } else {
          string0 = `Ranged Strength Rating Drained (${R.s(R.str(R.vp(1032), 10))}%)`;
        }
        break;
      }
      case 44232:
      {
        if ((R.vp(1034) >= 0)) {
          string0 = `Magic Strength Rating Boosted (+${R.s(R.str(R.vp(1034), 10))}%)`;
        } else {
          string0 = `Magic Strength Rating Drained (${R.s(R.str(R.vp(1034), 10))}%)`;
        }
        break;
      }
      case 48287:
      {
        if ((R.vp(11307) >= 0)) {
          string0 = `Necromancy Strength Rating Boosted (+${R.s(R.str(R.vp(11307), 10))}%)`;
        } else {
          string0 = `Necromancy Strength Rating Drained (${R.s(R.str(R.vp(11307), 10))}%)`;
        }
        break;
      }
      case 33047:
      {
        if (R.eq(R.vb(30395), 4)) {
          string0 = "Valentine's Slam - 4% XP Boost";
        } else {
          if (R.eq(R.vb(30395), 2)) {
            string0 = "Valentine's Slam - 2% XP Boost";
          }
        }
        break;
      }
      case 39803:
      {
        string0 = `Valentine's Flip - ${R.s(R.str(R.vb(51034), 10))}% XP boost`;
        break;
      }
      case 30925:
      {
        string0 = `Pulse Core - ${R.s(R.str(Math.imul(R.vb(27395), 2), 10))}% XP Boost`;
        break;
      }
      case 6938:
      case 6939:
      case 6940:
      case 6941:
      case 6943:
      case 6944:
      case 6945:
      case 6946:
      case 6947:
      case 6948:
      case 6949:
      case 6950:
      case 6951:
      case 6953:
      case 6954:
      case 6955:
      {
        string0 = `${R.s(R.structParam(int0, 2794))} <col=ffffff>${R.s(R.call(7247, [Math.imul(R.stackCount(int0), R.structParam(int0, 5196))]))}%</col> ${R.s(R.structParam(int0, 2795))}`;
        break;
      }
      case 6942:
      {
        string0 = `${R.s(R.structParam(int0, 2794))} <col=ffffff>+${R.s(R.str(Math.imul(R.stackCount(int0), R.structParam(int0, 5196)), 10))}</col> ${R.s(R.structParam(int0, 2795))}`;
        break;
      }
      case 6952:
      {
        string0 = `${R.s(R.structParam(int0, 2794))} <col=ffffff>${R.s(R.str(Math.imul(Math.imul(R.stackCount(int0), R.structParam(int0, 5196)), 15), 10))}</col> ${R.s(R.structParam(int0, 2795))}`;
        break;
      }
      case 44879:
      {
        string0 = `Crafting and Construction experience is increased by <col=ffffff>${R.s(R.call(7247, [Math.imul(R.stackCount(int0), R.structParam(int0, 5196))]))}%</col>.`;
        break;
      }
      case 14709:
      {
        string0 = "<col=ffffff>Pulverised</col> - Damage dealt reduced by <col=ffffff>25%</col>";
        break;
      }
      case 31984:
      {
        string0 = "Onslaught Active";
        break;
      }
      case 34338:
      {
        string0 = `Cinder Core - ${R.s(R.str(Math.imul(R.vb(45289), 2), 10))}% Bonus XP Burn Boost`;
        break;
      }
      case 35990:
      {
        string0 = `Ruthless - grants a ${R.s(R.str(R.idiv(R.vb(47357), 2), 10))}.${R.s(R.str(Math.imul(R.mod(R.vb(47357), 2), 5), 10))}% damage boost per stack. Each kill will add a stack for 20 seconds, up to a maximum of 5.`;
        break;
      }
      case 35991:
      {
        string0 = R.call(14791, []);
        break;
      }
      case 44698:
      {
        switch (R.key(R.vb(47856))) {
          case 1:
          {
            string0 = "Tune bane ore - You have currently tuned bane rocks to abyssalbanite.";
            break;
          }
          case 2:
          {
            string0 = "Tune bane ore - You have currently tuned bane rocks to basiliskbanite.";
            break;
          }
          case 3:
          {
            string0 = "Tune bane ore - You have currently tuned bane rocks to dragonbanite.";
            break;
          }
          case 4:
          {
            string0 = "Tune bane ore - You have currently tuned bane rocks to wallasalkibanite.";
            break;
          }
        }
        break;
      }
      case 44914:
      {
        string0 = R.call(13402, []);
        break;
      }
      case 45033:
      {
        if ((R.call(15408, [R.vp(9666)]) > 0)) {
          string0 = `Shadow infused hide - Damage taken is increased by ${R.s(R.call(7653, [R.call(15408, [R.vp(9666)]), 1, 1, 0, 0]))}%.`;
        } else {
          string0 = `Shadow infused hide - Damage taken is reduced by ${R.s(R.call(7653, [Math.imul((-1 | 0), R.call(15408, [R.vp(9666)])), 1, 1, 0, 0]))}%.`;
        }
        break;
      }
      case 45034:
      {
        if ((R.call(15409, [R.vp(9666)]) >= 0)) {
          string0 = `Shadow infused power - Damage dealt is increased by ${R.s(R.call(7653, [R.call(15409, [R.vp(9666)]), 1, 1, 0, 0]))}%.`;
        } else {
          string0 = `Shadow infused power - Damage dealt is reduced by ${R.s(R.call(7653, [Math.imul((-1 | 0), R.call(15409, [R.vp(9666)])), 1, 1, 0, 0]))}%.`;
        }
        break;
      }
      case 39632:
      {
        string0 = `You are earning ${R.s(R.str(R.vc(6897), 10))}% more XP in this area while the Spring Festival is active.`;
        break;
      }
      case 4574:
      {
        string0 = `Rock of Resilience - ${R.s(R.str(R.vb(29061), 10))}% XP boost`;
        break;
      }
      case 45449:
      {
        string0 = `You have ${R.s(R.str(R.invTotal(93, 51839), 10))} siege engine parts left.`;
        break;
      }
      case 45536:
      {
        string0 = `Yak Track - ${R.s(R.str(R.vb(48835), 10))}% XP Boost.`;
        if (R.eq(R.call(12476, []), 1)) {
          string0 = `${R.s(string0)} This buff is disabled while your DXP is active.`;
        }
        break;
      }
      case 33227:
      {
        string0 = `Spider minions will back up their leader: ${R.s(R.str(R.vc(4276), 10))} left.`;
        break;
      }
      case 34499:
      {
        string0 = `${R.s(R.str(R.vc(4277), 10))} acid is in the pool and should be dealt with via absorption.`;
        break;
      }
      case 41645:
      {
        string0 = `Spider minons on the roof can still heal Araxxor for ${R.s(R.str(R.vc(4278), 10))} health points.`;
        break;
      }
      case 46027:
      {
        string0 = `Stack-catch up to ${R.s(R.str(R.call(2659, []), 10))} extra scarabs whilst your crocodile is in the middle of a catch.`;
        break;
      }
      case 6196:
      {
        if ((!R.eq(R.vp(10264), (-1 | 0)))) {
          string0 = `Quiver ammo: ${R.s(R.itemName(R.vp(10264)))}`;
          if (((!R.eq(R.itemParam(R.vp(10264), 6186), (-1 | 0))) && (R.len(R.structParam(R.itemParam(R.vp(10264), 6186), 2795)) > 0))) {
            string0 = `${R.s(string0)}<br><br>${R.s(R.structParam(R.itemParam(R.vp(10264), 6186), 2795))}`;
          }
        } else {
          string0 = "Your quiver is empty.";
        }
        break;
      }
      case 46197:
      {
        string0 = `You have access to the Anima Flow Edict<br><br>With ${R.s(R.str(R.vp(10539), 10))} ${R.s(R.call(4583, [R.vp(10539), "stack", "stacks"]))}, your basic abilities will gain ${R.s(R.str(Math.imul(R.vp(10539), R.idiv(20, 10)), 10))}% more adrenaline.`;
        break;
      }
      case 46194:
      {
        string0 = `You have access to the Balance of Power Edict<br><br>With ${R.s(R.str(R.vp(10537), 10))} ${R.s(R.call(4583, [R.vp(10537), "stack", "stacks"]))}, while below ${R.s(R.str(60, 10))}% of your maximum life points, outgoing damage is increased by ${R.s(R.str(Math.imul(R.vp(10537), 6), 10))}%.`;
        break;
      }
      case 46195:
      {
        string0 = `You have access to the Guardian's Triumph Edict<br><br>With ${R.s(R.str(R.vp(10538), 10))} ${R.s(R.call(4583, [R.vp(10538), "stack", "stacks"]))}, after casting an ultimate ability, your next basic ability consumes Guardian's Triumph, increasing it's damage by ${R.s(R.str(Math.imul(R.vp(10538), 20), 10))}% and heals you for ${R.s(R.str(Math.imul(R.vp(10538), 8), 10))}%.`;
        break;
      }
      case 46193:
      {
        string0 = `You have access to the Haste Edict<br><br>With ${R.s(R.str(R.vp(10534), 10))} ${R.s(R.call(4583, [R.vp(10534), "stack", "stacks"]))}, your ability cooldowns are reduced by ${R.s(R.str(Math.imul(R.vp(10534), 8), 10))}%.`;
        break;
      }
      case 46199:
      {
        string0 = `Zamorak has access to the Smite Hex<br><br>With ${R.s(R.str(R.vp(10541), 10))} ${R.s(R.call(4583, [R.vp(10541), "stack", "stacks"]))}, if Zamorak's targets are hit below  ${R.s(R.str(Math.imul(R.vp(10541), 5), 10))}% of their total maximum life points, they'll be killed instantly.`;
        break;
      }
      case 46203:
      {
        string0 = `Zamorak has access to the Affliction Hex<br><br>With ${R.s(R.str(R.vp(10545), 10))} ${R.s(R.call(4583, [R.vp(10545), "stack", "stacks"]))}, when Zamorak's targets heal, the amount will be reduced by  ${R.s(R.str(Math.imul(R.vp(10545), 10), 10))}%.`;
        break;
      }
      case 46200:
      {
        string0 = "Zamorak has access to his Coven generals<br><br>Upon activation of this Hex, Zamorak will call forth a Lifeweaver and a Protector to aid him in battle.";
        break;
      }
      case 46201:
      {
        string0 = `Zamorak has access to the Disintegrate Hex<br><br>With ${R.s(R.str(R.vp(10543), 10))} ${R.s(R.call(4583, [R.vp(10543), "stack", "stacks"]))}, Zamorak's attacks will push ${R.s(R.str(Math.imul(R.vp(10543), 7), 10))}% damage through any shielding effects.`;
        break;
      }
      case 46202:
      {
        string0 = "Zamorak has access to Chaos traps<br><br>Upon activation of this Hex, Zamorak will spawn in traps across the arena that will spike magic damage if a target walks over.<br><br>For each subsequent Edict that is restored, Zamorak will send out a new set of traps.";
        break;
      }
      case 46198:
      {
        string0 = `Zamorak has access to the Twinshot Hex<br><br>With ${R.s(R.str(R.vp(10540), 10))} ${R.s(R.call(4583, [R.vp(10540), "stack", "stacks"]))}, Zamorak's attacks deal a second delayed hit at ${R.s(R.str(Math.imul(R.vp(10540), 10), 10))}% total damage of the original hit.`;
        break;
      }
      case 46204:
      {
        string0 = `<col=FF0000>Hex: Twinshot - Zamorak's attacks deal a second hit at ${R.s(R.str(10, 10))}% total damage of the original hit, per stack.<br><br><col=FF00>Edict: Anima Flow - All basic abilities generate ${R.s(R.str(R.idiv(20, 10), 10))}% more adrenaline per stack.`;
        break;
      }
      case 46205:
      {
        string0 = `<col=FF0000>Hex: Smite - If Zamorak's targets are hit below ${R.s(R.str(5, 10))}% of their Constitution level, per stack, they'll be executed instantly.<br><br><col=FF00>Edict: Haste - Your ability cooldowns are reduced by ${R.s(R.str(8, 10))}% of their original cooldown per stack.`;
        break;
      }
      case 46206:
      {
        string0 = "<col=FF0000>Hex: Coven - Zamorak calls forth some of his coven to support him.<br><br><col=FF00>Edict: Inner Chaos - While standing near a Hexed Rune, you deal and take 5% more damage.";
        break;
      }
      case 46207:
      {
        string0 = `<col=FF0000>Hex: Disintegrate - Zamorak's attacks will push ${R.s(R.str(7, 10))}% of damage through any shielding effects, per stack.<br><br><col=FF00>Edict: Guardian's Triumph - After casting an ultimate ability, your next basic ability consumes Guardian's Triumph, increasing the damage it deals by ${R.s(R.str(20, 10))}% extra damage, and heals you for ${R.s(R.str(8, 10))}% of your missing life points per stack.`;
        break;
      }
      case 46208:
      {
        string0 = "<col=FF0000>Hex: Chaos Traps - Upon activation of this hex, Zamorak will spawn in traps across the arena that will spike magic damage if a target walks over them.<br><br><col=FF00>Edict: Sword of Edicts - Temporarily siphon the dwindling defensive power from the Sword of Edicts, dealing and taking 5% less damage when standing near it.";
        break;
      }
      case 46209:
      {
        string0 = `<col=FF0000>Hex: Affliction - When Zamorak's targets heal, the amount will be reduced by ${R.s(R.str(R.vp(10545), 10))}% per stack.<br><br><col=FF00>Edict: Balance of Power - While below ${R.s(R.str(60, 10))}% of your maximum life points, outgoing damage is increased by ${R.s(R.str(6, 10))}% per stack.`;
        break;
      }
      case 35070:
      {
        string0 = `During this week of Fresh Start Worlds, you are earning <col=FF00>+${R.s(R.str(R.idiv(R.vb(51872), 10), 10))}% XP</col>`;
        if ((R.vb(51873) > 0)) {
          string0 = `${R.s(string0)} in skills level 70 and above; and <col=FF00>+${R.s(R.str(R.idiv(((R.vb(51872)) + (R.vb(51873)) | 0), 10), 10))}% XP</col> in skills below level 70`;
        }
        string0 = `${R.s(string0)}.`;
        break;
      }
      case 47182:
      {
        string0 = `Ring of Death - Revive effect last dealt ${R.s(R.str(R.vc(7112), 10))} damage.<br><br>The effect will stop when you die, or return to a safe area such as a bank.`;
        break;
      }
      case 48339:
      {
        string0 = R.call(3111, [R.vp(11059), 1, 0, R.structParam(R.vp(11059), 2795)]);
        break;
      }
      case 48335:
      {
        string0 = R.call(17727, [int0, 1, 1]);
        break;
      }
      case 48336:
      {
        string0 = R.call(17727, [int0, 1, 1]);
        break;
      }
      case 48337:
      {
        string0 = R.call(17727, [int0, 1, 1]);
        break;
      }
      case 32349:
      {
        string0 = R.call(17727, [int0, 1, 1]);
        break;
      }
      case 48341:
      {
        string0 = `${R.s(R.structParam(int0, 2794))}<br>${R.s(R.call(3111, [48329, 1, 0, R.structParam(48329, 2795)]))}`;
        break;
      }
      case 48342:
      {
        string0 = `${R.s(R.structParam(int0, 2794))}<br>${R.s(R.call(3111, [48330, 1, 0, R.structParam(48330, 2795)]))}`;
        break;
      }
      case 48347:
      {
        string0 = `${R.s(R.structParam(int0, 2794))}<br>${R.s(R.call(3111, [48332, 1, 0, R.structParam(48332, 2795)]))}`;
        break;
      }
      case 49073:
      {
        string0 = `${R.s(R.structParam(int0, 2794))}<br>${R.s(R.call(3111, [49072, 1, 0, R.structParam(49072, 2795)]))}`;
        break;
      }
      case 48878:
      {
        string0 = `${R.s(R.call(18429, [R.vb(54950)]))}<br>${R.s(R.call(18431, [R.vb(54950)]))}`;
        break;
      }
      case 4549:
      {
        string0 = `${R.s(R.structParam(int0, 2794))}${R.s(R.call(18710, [49543, 1, string0]))}`;
        break;
      }
      case 34985:
      {
        string0 = `${R.s(R.structParam(int0, 2794))}${R.s(R.call(18713, [49543, 1, string0]))}`;
        break;
      }
      case 50212:
      {
        string0 = `Absorbed energy: <col=00ff00>${R.s(R.strLoc(R.vb(55992), 1))}<br>Current attunement: <col=00ff00>${R.s(R.call(19131, [R.vb(55991)]))}`;
        break;
      }
      case 29054:
      {
        string0 = `Lumberjack's Intuition<br>${R.s(R.call(19675, []))}`;
        break;
      }
      case 51129:
      {
        string0 = `Love Letter - ${R.s(R.str(R.vb(57161), 10))}% XP Boost`;
        break;
      }
      case 51130:
      {
        string0 = `Love Letter - ${R.s(R.str(R.vb(57163), 10))}% Bonus XP Burn Boost`;
        break;
      }
      case 51665:
      {
        string0 = R.call(19979, [int0, string0]);
        break;
      }
      case 53000:
      {
        string0 = R.call(20701, []);
        break;
      }
      case 1869:
      {
        string0 = R.call(3721, []);
        break;
      }
      case 1875:
      {
        string0 = R.call(3940, [int0, R.vb(3305)]);
        break;
      }
      case 1876:
      {
        string0 = R.call(3940, [int0, R.vb(24877)]);
        break;
      }
      case 1877:
      {
        string0 = R.call(3940, [int0, R.vb(24878)]);
        break;
      }
      case 1878:
      {
        string0 = R.call(3940, [int0, R.vb(24879)]);
        break;
      }
    }
    return string0;
  };
  V[11179] = { vb: [], vp: [9903, 10296, 10437, 10438], vc: [], calls: [] };
  S[11179] = function () {
    return [R.vp(10438), R.vp(9903), R.vp(10437), R.vp(10296)];
  };
  V[11216] = { vb: [], vp: [], vc: [], calls: [15973] };
  S[11216] = function (string0) {
    var string0 = `${R.s(string0)}- <sprite=23727><nbsp><col=ffffff>${R.s(R.structParam(14663, 2794))}</col> reduces the cooldown of <sprite=23729><nbsp><col=ffffff>${R.s(R.structParam(14666, 2794))} by an additional <col=ffffff>${R.s(R.call(15973, [2, 1]))}</col> with each hit.`;
    string0 = `${R.s(string0)}<br>- <sprite=23726><nbsp><col=ffffff>${R.s(R.structParam(52795, 2794))}</col> additionally applies the cooldown reduction effect of <sprite=23727><nbsp><col=ffffff>${R.s(R.structParam(14663, 2794))}.`;
    return string0;
  };
  V[11217] = { vb: [], vp: [], vc: [], calls: [17710] };
  S[11217] = function (string0) {
    var string0 = `${R.s(string0)}- After casting <sprite=14239><nbsp><col=ffffff>${R.s(R.structParam(14733, 2794))}</col> apply <col=ffffff>${R.s(R.structParam(52779, 2794))} to self.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [10]))}.`;
    return string0;
  };
  V[11479] = { vb: [], vp: [], vc: [], calls: [] };
  S[11479] = function (int0, int1, int2) {
    if ((int0 > 0)) {
      if ((int1 > 0)) {
        return `${R.s(R.str(int0, 10))}d ${R.s(R.str(int1, 10))}h`;
      }
      return `${R.s(R.str(int0, 10))}d`;
    }
    if ((int1 > 0)) {
      if ((int2 > 0)) {
        return `${R.s(R.str(int1, 10))}h ${R.s(R.str(int2, 10))}m`;
      }
      return `${R.s(R.str(int1, 10))}h`;
    }
    if ((int2 > 0)) {
      return `${R.s(R.str(int2, 10))}m`;
    }
    return "";
  };
  V[11490] = { vb: [1622, 11498, 12170, 12708, 12710, 13650, 15344, 17795, 17797, 17799, 17801, 53990, 53991, 53992, 53993], vp: [], vc: [], calls: [] };
  S[11490] = function (int0, int1) {
    switch (R.key(int0)) {
      case 1:
      {
        if ((R.vb(12710) < int1)) {
          return 0;
        }
        break;
      }
      case 2:
      {
        if ((R.vb(1622) < 2)) {
          return 0;
        }
        break;
      }
      case 3:
      {
        if ((R.vb(15344) < 10)) {
          return 0;
        }
        break;
      }
      case 4:
      {
        if ((R.vb(11498) < 280)) {
          return 0;
        }
        break;
      }
      case 5:
      {
        if ((R.vb(12708) < 4)) {
          return 0;
        }
        break;
      }
      case 6:
      {
        if ((R.vb(13650) < 8)) {
          return 0;
        }
        break;
      }
      case 7:
      {
        if ((R.vb(12170) < 7)) {
          return 0;
        }
        break;
      }
      case 8:
      {
        if ((R.vb(17795) < 1)) {
          return 0;
        }
        break;
      }
      case 9:
      {
        if ((R.vb(17797) < 1)) {
          return 0;
        }
        break;
      }
      case 10:
      {
        if ((R.vb(17799) < 1)) {
          return 0;
        }
        break;
      }
      case 11:
      {
        if ((R.vb(17801) < 1)) {
          return 0;
        }
        break;
      }
      case 94:
      {
        if ((R.vb(53990) < 1)) {
          return 0;
        }
        break;
      }
      case 95:
      {
        if ((R.vb(53991) < 1)) {
          return 0;
        }
        break;
      }
      case 96:
      {
        if ((R.vb(53992) < 1)) {
          return 0;
        }
        break;
      }
      case 97:
      {
        if ((R.vb(53993) < 1)) {
          return 0;
        }
        break;
      }
      default:
      {
        return 1;
      }
    }
    return (-1 | 0);
  };
  V[11502] = { vb: [5855, 5856, 5857, 5858, 5859, 5860, 5861, 5862, 5863, 5864, 5865, 5866, 5867, 5868, 5869, 5870], vp: [], vc: [], calls: [] };
  S[11502] = function () {
    return ((((((((((((((((((((((((((((((R.vb(5855)) + (R.vb(5856)) | 0)) + (R.vb(5857)) | 0)) + (R.vb(5858)) | 0)) + (R.vb(5859)) | 0)) + (R.vb(5860)) | 0)) + (R.vb(5861)) | 0)) + (R.vb(5862)) | 0)) + (R.vb(5863)) | 0)) + (R.vb(5864)) | 0)) + (R.vb(5865)) | 0)) + (R.vb(5866)) | 0)) + (R.vb(5867)) | 0)) + (R.vb(5868)) | 0)) + (R.vb(5869)) | 0)) + (R.vb(5870)) | 0);
  };
  V[11570] = { vb: [], vp: [], vc: [], calls: [7653] };
  S[11570] = function (int0) {
    return `<col=ffffff>${R.s(R.call(7653, [int0, 1, 1, 0, 1]))}%</col> <sprite=14907><nbsp><col=DEAC18>Adrenaline</col>`;
  };
  V[11669] = { vb: [], vp: [], vc: [], calls: [] };
  S[11669] = function (int0, int1) {
    switch (R.key(int0)) {
      case 94:
      {
        switch (R.key(int1)) {
          case 0:
          {
            return "Head";
          }
          case 1:
          {
            return "Back";
          }
          case 2:
          {
            return "Neck";
          }
          case 3:
          {
            return "Main-hand";
          }
          case 4:
          {
            return "Torso";
          }
          case 5:
          {
            return "Off-hand";
          }
          case 7:
          {
            return "Legs";
          }
          case 9:
          {
            return "Hands";
          }
          case 10:
          {
            return "Feet";
          }
          case 12:
          {
            return "Ring";
          }
          case 13:
          {
            return "Quiver";
          }
          case 14:
          {
            return "Aura";
          }
          case 17:
          {
            return "Pocket";
          }
          case 18:
          {
            return "Wings";
          }
        }
        break;
      }
    }
    return "";
  };
  V[11772] = { vb: [], vp: [], vc: [], calls: [15973, 17444, 17710] };
  S[11772] = function (int0, int1, string0) {
    var int2 = R.call(17444, [int0]);
    var string0 = `${R.s(string0)}<br>- Applies <sprite=35768><nbsp><col=ffffff>${R.s(R.structParam(int0, 2794))}</col> to self.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [int2]))}.`;
    string0 = `${R.s(string0)}<br>- Can be recast to extend duration by <col=ffffff>${R.s(R.call(15973, [int2, 1]))}</col> per cast to a maximum of <col=ffffff>${R.s(R.call(15973, [6000, 1]))}</col>.`;
    return string0;
  };
  V[11874] = { vb: [29682, 29692], vp: [], vc: [], calls: [] };
  S[11874] = function () {
    if (((R.vb(29692) >= 10) && R.eq(R.vb(29682), 1))) {
      return 2;
    }
    if (((R.vb(29692) >= 10) || R.eq(R.vb(29682), 1))) {
      return 1;
    }
    return 0;
  };
  V[11927] = { vb: [48190], vp: [], vc: [], calls: [12478] };
  S[11927] = function (int0) {
    var int1 = 1;
    if (R.eq(R.vb(48190), 1)) {
      switch (R.key(int0)) {
        case 27:
        case 14:
        {
          int1 = ((int1) + (1) | 0);
          break;
        }
        default:
        {
          R.call(12478, [`Invalid skill attempting to multiply - ${R.s(R.enumValue(17, 36, 680, int0))}`]);
          break;
        }
      }
    }
    return int1;
  };
  V[11942] = { vb: [], vp: [], vc: [], calls: [11943] };
  S[11942] = function (int0) {
    return R.call(11943, [R.itemParam(int0, 9119)]);
  };
  V[11943] = { vb: [55825, 55826, 55827, 55828, 55829, 55830, 58289, 58290], vp: [], vc: [], calls: [] };
  S[11943] = function (int0) {
    switch (R.key(int0)) {
      case 50071:
      {
        return R.vb(55825);
      }
      case 50072:
      {
        return R.vb(55826);
      }
      case 50073:
      {
        return R.vb(55827);
      }
      case 50074:
      {
        return R.vb(55828);
      }
      case 50075:
      {
        return R.vb(55829);
      }
      case 50076:
      {
        return R.vb(55830);
      }
      case 52069:
      {
        return R.vb(58289);
      }
      case 52070:
      {
        return R.vb(58290);
      }
    }
    return 0;
  };
  V[11975] = { vb: [27915], vp: [], vc: [], calls: [] };
  S[11975] = function () {
    return R.vb(27915);
  };
  V[12039] = { vb: [], vp: [], vc: [], calls: [] };
  S[12039] = function (int0) {
    switch (R.key(int0)) {
      case 26:
      {
        return 32944;
      }
    }
    return (-1 | 0);
  };
  V[12043] = { vb: [], vp: [], vc: [], calls: [12039] };
  S[12043] = function (int0) {
    var int1 = R.call(12039, [int0]);
    if (R.eq(int1, (-1 | 0))) {
      return 1;
    }
    if (((!R.eq(R.structParam(int1, 5510), (-1 | 0))) && (R.statBase(R.structParam(int1, 5510)) < R.structParam(int1, 5511)))) {
      return 0;
    }
    if (((!R.eq(R.structParam(int1, 5512), (-1 | 0))) && (R.statBase(R.structParam(int1, 5512)) < R.structParam(int1, 5513)))) {
      return 0;
    }
    if (((!R.eq(R.structParam(int1, 5514), (-1 | 0))) && (R.statBase(R.structParam(int1, 5514)) < R.structParam(int1, 5515)))) {
      return 0;
    }
    return 1;
  };
  V[12052] = { vb: [30224, 30225], vp: [], vc: [], calls: [] };
  S[12052] = function (int0) {
    var int1 = 0;
    switch (R.key(int0)) {
      case 1:
      case 2:
      {
        int1 = R.vb(30224);
        break;
      }
      case 3:
      {
        int1 = R.vb(30225);
        break;
      }
      default:
      {
        return (-1 | 0);
      }
    }
    if (R.eq(R.mapMembers(), 0)) {
      return 0;
    }
    return int1;
  };
  V[12059] = { vb: [], vp: [5981, 5982, 5983, 6633, 7249, 12212], vc: [], calls: [12478] };
  S[12059] = function (int0) {
    if ((int0 < 0)) {
      return 1;
    }
    switch (R.key(R.idiv(int0, 32))) {
      case 0:
      {
        return R.testbit(R.vp(5981), R.mod(int0, 32));
      }
      case 1:
      {
        return R.testbit(R.vp(5982), R.mod(int0, 32));
      }
      case 2:
      {
        return R.testbit(R.vp(5983), R.mod(int0, 32));
      }
      case 3:
      {
        return R.testbit(R.vp(6633), R.mod(int0, 32));
      }
      case 4:
      {
        return R.testbit(R.vp(7249), R.mod(int0, 32));
      }
      case 5:
      {
        return R.testbit(R.vp(12212), R.mod(int0, 32));
      }
    }
    R.call(12478, [`Blueprint ID: ${R.s(R.str(R.idiv(int0, 32), 10))} is out of bounds - need to add new %invent_blueprint_unlocked_X var`]);
    return 0;
  };
  V[12070] = { vb: [], vp: [], vc: [], calls: [] };
  S[12070] = function (int0) {
    if (R.eq(int0, (-1 | 0))) {
      return 0;
    }
    if (((!R.eq(R.itemParam(int0, 5524), 0)) && R.eq(R.itemHasVarobj(int0), 1))) {
      return 1;
    }
    return 0;
  };
  V[12071] = { vb: [], vp: [], vc: [], calls: [12072, 12073] };
  S[12071] = function (int0) {
    return R.min(R.call(12072, []), R.call(12073, [int0]));
  };
  V[12072] = { vb: [], vp: [], vc: [], calls: [12059] };
  S[12072] = function () {
    if (R.eq(R.call(12059, [65]), 0)) {
      return 1;
    }
    if (R.eq(R.call(12059, [66]), 0)) {
      return 5;
    }
    if (R.eq(R.call(12059, [67]), 0)) {
      return 10;
    }
    if (R.eq(R.call(12059, [68]), 0)) {
      return 15;
    }
    return 20;
  };
  V[12073] = { vb: [], vp: [], vc: [], calls: [] };
  S[12073] = function (int0) {
    if ((int0 < 1160)) {
      return 1;
    }
    if ((int0 < 2607)) {
      return 2;
    }
    if ((int0 < 5176)) {
      return 3;
    }
    if ((int0 < 8285)) {
      return 4;
    }
    if ((int0 < 11760)) {
      return 5;
    }
    if ((int0 < 15835)) {
      return 6;
    }
    if ((int0 < 21152)) {
      return 7;
    }
    if ((int0 < 28761)) {
      return 8;
    }
    if ((int0 < 40120)) {
      return 9;
    }
    if ((int0 < 57095)) {
      return 10;
    }
    if ((int0 < 81960)) {
      return 11;
    }
    if ((int0 < 117397)) {
      return 12;
    }
    if ((int0 < 166496)) {
      return 13;
    }
    if ((int0 < 232755)) {
      return 14;
    }
    if ((int0 < 320080)) {
      return 15;
    }
    if ((int0 < 432785)) {
      return 16;
    }
    if ((int0 < 575592)) {
      return 17;
    }
    if ((int0 < 753631)) {
      return 18;
    }
    if ((int0 < 972440)) {
      return 19;
    }
    return 20;
  };
  V[12074] = { vb: [], vp: [], vc: [], calls: [] };
  S[12074] = function (int0) {
    switch (R.key(int0)) {
      case 1:
      {
        return 0;
      }
      case 2:
      {
        return 1160;
      }
      case 3:
      {
        return 2607;
      }
      case 4:
      {
        return 5176;
      }
      case 5:
      {
        return 8285;
      }
      case 6:
      {
        return 11760;
      }
      case 7:
      {
        return 15835;
      }
      case 8:
      {
        return 21152;
      }
      case 9:
      {
        return 28761;
      }
      case 10:
      {
        return 40120;
      }
      case 11:
      {
        return 57095;
      }
      case 12:
      {
        return 81960;
      }
      case 13:
      {
        return 117397;
      }
      case 14:
      {
        return 166496;
      }
      case 15:
      {
        return 232755;
      }
      case 16:
      {
        return 320080;
      }
      case 17:
      {
        return 432785;
      }
      case 18:
      {
        return 575592;
      }
      case 19:
      {
        return 753631;
      }
      case 20:
      {
        return 972440;
      }
    }
    return 0;
  };
  V[12076] = { vb: [], vp: [], vc: [], calls: [] };
  S[12076] = function (int0) {
    R.dbFind(32768, int0, 0);
    return R.dbNext();
  };
  V[12080] = { vb: [], vp: [], vc: [], calls: [] };
  S[12080] = function (int0) {
    switch (R.key(int0)) {
      case 1:
      case 2:
      case 3:
      case 4:
      case 5:
      {
        return 1;
      }
    }
    return 0;
  };
  V[12198] = { vb: [], vp: [], vc: [], calls: [] };
  S[12198] = function (int0, int1) {
    if (R.eq(int0, 0)) {
      return [(-1 | 0), (-1 | 0)];
    }
    R.dbFind(32768, int0, 0);
    var int2 = R.dbNext();
    if (R.eq(int2, (-1 | 0))) {
      return [(-1 | 0), (-1 | 0)];
    }
    var int3 = (-1 | 0);
    if ((R.dbFieldCount(int2, 32880) > 1)) {
      switch (R.key(int1)) {
        case 1:
        {
          int3 = 24238;
          break;
        }
        case 2:
        {
          int3 = 24239;
          break;
        }
        case 3:
        {
          int3 = 24240;
          break;
        }
        case 4:
        {
          int3 = 24253;
          break;
        }
        case 5:
        {
          int3 = 10564;
          break;
        }
        case 6:
        {
          int3 = 10600;
          break;
        }
      }
    }
    return [R.dbField(int2, 32800, 0), int3];
  };
  V[12202] = { vb: [], vp: [], vc: [], calls: [] };
  S[12202] = function (int0, int1, string0) {
    if (R.eq(int0, (-1 | 0))) {
      return string0;
    }
    if (R.eq(R.dbFieldCount(int0, 32848), 0)) {
      return string0;
    }
    var string1 = `<br><br>Active perk: <col=ffffff>${R.s(R.dbField(int0, 32784, 0))}</col>`;
    if ((int1 >= 1)) {
      string1 = `${R.s(string1)} (rank <col=ffffff>${R.s(R.str(int1, 10))}</col>)`;
    }
    string1 = `${R.s(string1)}<br>${R.s(R.dbField(int0, 32848, 0))}`;
    return `${R.s(string0)}${R.s(string1)}`;
  };
  V[12377] = { vb: [], vp: [], vc: [], calls: [] };
  S[12377] = function (int0, int1, int2) {
    if ((int1 > int2)) {
      var int1, int2; [int1, int2] = [int2, int1];
    }
    return R.min(R.max(int0, int1), int2);
  };
  V[12421] = { vb: [], vp: [], vc: [], calls: [] };
  S[12421] = function (int0) {
    if ((int0 > 0)) {
      return 1;
    }
    if ((int0 < 0)) {
      return (-1 | 0);
    }
    return 0;
  };
  V[12422] = { vb: [], vp: [], vc: [], calls: [12421] };
  S[12422] = function (int0, int1, int2) {
    if (R.eq(int1, 0)) {
      return R.scale(int0, int1, int2);
    }
    if ((R.eq(int0, 0) || R.eq(int2, 0))) {
      return 0;
    }
    if (R.eq(int2, int1)) {
      return int0;
    }
    if (R.eq(int0, int1)) {
      return int2;
    }
    if ((R.eq(int0, (-2147483648 | 0)) && R.eq(int1, (-1 | 0)))) {
      var int0 = 2147483647;
      var int1 = 1;
    }
    if ((Math.imul(Math.imul(R.call(12421, [int0]), R.call(12421, [int1])), R.call(12421, [int2])) > 0)) {
      if ((int2 > 0)) {
        if ((R.idiv(int0, int1) >= R.idiv(2147483647, int2))) {
          return 2147483647;
        }
      } else {
        if ((R.idiv(int0, int1) <= R.idiv(2147483647, int2))) {
          return 2147483647;
        }
      }
    } else {
      if ((int2 > 0)) {
        if ((R.idiv((-2147483648 | 0), int2) >= R.idiv(int0, int1))) {
          return (-2147483648 | 0);
        }
      } else {
        if ((int2 < (-1 | 0))) {
          if ((R.idiv((-2147483648 | 0), int2) <= R.idiv(int0, int1))) {
            return (-2147483648 | 0);
          }
        } else {
          if ((R.idiv(2147483647, 1) <= R.idiv(int0, int1))) {
            return (-2147483648 | 0);
          }
        }
      }
    }
    return R.scale(int0, int1, int2);
  };
  V[12476] = { vb: [223, 47683], vp: [], vc: [], calls: [] };
  S[12476] = function () {
    if (R.eq(R.vb(223), 0)) {
      return 0;
    }
    if (R.eq(R.vb(223), 1)) {
      if (R.eq(R.vb(47683), 0)) {
        return 1;
      }
      return 0;
    }
    return (-1 | 0);
  };
  V[12477] = { vb: [], vp: [3079, 6601], vc: [], calls: [] };
  S[12477] = function () {
    switch (R.key(R.ifNeg())) {
      case 906:
      {
        return R.vp(6601);
      }
      case 1477:
      {
        return R.vp(3079);
      }
      case 744:
      {
        return R.dateRuneday();
      }
    }
    return R.dateRuneday();
  };
  V[12478] = { vb: [], vp: [], vc: [], calls: [] };
  S[12478] = function (string0) {
    return;
  };
  V[12517] = { vb: [], vp: [], vc: [], calls: [] };
  S[12517] = function (int0, int1, int2) {
    if (R.eq(int0, (-1 | 0))) {
      return (-1 | 0);
    }
    var int3 = R.invSize(int0);
    if (((int2 < 0) || (int2 >= int3))) {
      return (-1 | 0);
    }
    if ((R.invTotal(int0, int1) < 1)) {
      return (-1 | 0);
    }
    var int4 = (-1 | 0);
    while ((int2 < int3)) {
      int4 = R.invObj(int0, int2);
      if (R.eq(int4, int1)) {
        return int2;
      }
      var int2 = ((int2) + (1) | 0);
    }
    return (-1 | 0);
  };
  V[12544] = { vb: [], vp: [], vc: [], calls: [] };
  S[12544] = function () {
    return "Customisation options available";
  };
  V[12602] = { vb: [], vp: [], vc: [], calls: [] };
  S[12602] = function () {
    return "<col=969696>This ability changes based on your equipped weapon.</col>";
  };
  V[12618] = { vb: [], vp: [], vc: [], calls: [8247, 17720] };
  S[12618] = function (int0, string0) {
    var int1 = R.call(8247, [19343]);
    var int2 = R.call(8247, [19342]);
    switch (R.key(int0)) {
      default:
      {
        break;
      }
    }
    var string0 = `${R.s(string0)}<br>-Your next <col=3366FF>Magic ability</col> is empowered:`;
    if (R.eq(int2, 47221)) {
      string0 = `${R.s(string0)}<br>- <sprite=27432><nbsp><col=ffffff>${R.s(R.structParam(int2, 2794))}:</col> Your next <col=3366FF>Magic ability</col> costs <col=ffffff>${R.s(R.str(R.idiv(450, 10), 10))}%</col> less <sprite=14907><nbsp><col=DEAC18>Adrenaline</col>.`;
    } else {
      string0 = `${R.s(string0)}<br>- <sprite=27430><nbsp><col=ffffff>${R.s(R.structParam(int2, 2794))}:</col> Your next <col=3366FF>Magic ability</col> costs <col=ffffff>${R.s(R.str(R.idiv(350, 10), 10))}%</col> less <sprite=14907><nbsp><col=DEAC18>Adrenaline</col>.`;
    }
    string0 = `${R.s(string0)}<br>- <sprite=14236><nbsp><col=ffffff>${R.s(R.structParam(14730, 2794))}:</col> Deals ${R.s(R.call(17720, [14730, 260, ((260) + (50) | 0), 0, 0]))}.`;
    if (R.eq(int1, 45450)) {
      string0 = `${R.s(string0)}<br>- <sprite=13310><nbsp><col=ffffff>${R.s(R.structParam(int1, 2794))}:</col> Each attack grants an additional ${R.s(R.str(10, 10))}% <col=ffffff>Critical Strike Chance</col>.`;
    } else {
      string0 = `${R.s(string0)}<br>- <sprite=13309><nbsp><col=ffffff>${R.s(R.structParam(int1, 2794))}:</col> Each attack grants an additional ${R.s(R.str(10, 10))}% <col=ffffff>Critical Strike Chance</col>.`;
    }
    return string0;
  };
  V[12676] = { vb: [], vp: [], vc: [], calls: [12677] };
  S[12676] = function (int0) {
    return R.call(12677, [R.itemParam(int0, 6848)]);
  };
  V[12677] = { vb: [51465, 51466, 51467, 51468, 51469, 51470, 51480, 51481, 51482, 52157, 52158, 52159], vp: [], vc: [], calls: [] };
  S[12677] = function (int0) {
    switch (R.key(int0)) {
      case 53357:
      {
        return R.vb(51465);
      }
      case 53359:
      {
        return R.vb(51466);
      }
      case 53361:
      {
        return R.vb(51467);
      }
      case 53363:
      {
        return R.vb(51468);
      }
      case 53365:
      {
        return R.vb(51469);
      }
      case 53367:
      {
        return R.vb(51470);
      }
      case 53369:
      {
        return R.vb(51480);
      }
      case 53371:
      {
        return R.vb(51481);
      }
      case 53373:
      {
        return R.vb(51482);
      }
      case 53921:
      {
        return R.vb(52157);
      }
      case 53923:
      {
        return R.vb(52158);
      }
      case 53925:
      {
        return R.vb(52159);
      }
    }
    return 0;
  };
  V[12779] = { vb: [], vp: [], vc: [], calls: [13564] };
  S[12779] = function (int0, int1, int2) {
    switch (R.key(int0)) {
      case 37989:
      case 37990:
      case 37991:
      case 37992:
      {
        break;
      }
      default:
      {
        return 1;
      }
    }
    if (R.eq(int1, (-1 | 0))) {
      return 0;
    }
    if (R.eq(R.call(13564, [int2, int1]), int2)) {
      return 1;
    }
    return 0;
  };
  V[12780] = { vb: [1061, 1063, 1065, 1083, 1085, 1087], vp: [], vc: [], calls: [] };
  S[12780] = function () {
    var int0 = ((((((((((R.vb(1085)) + (R.vb(1083)) | 0)) + (R.vb(1065)) | 0)) + (R.vb(1061)) | 0)) + (R.vb(1063)) | 0)) + (R.vb(1087)) | 0);
    if (R.eq(int0, 6)) {
      return 1;
    }
    return 0;
  };
  V[13038] = { vb: [], vp: [], vc: [], calls: [7653] };
  S[13038] = function (int0, string0) {
    var string0 = `${R.s(string0)}<br>- <col=ffffff>Basic</col> <col=3366FF>Magic abilities</col> gain <col=ffffff>+${R.s(R.call(7653, [80, 1, 1, 0, 1]))}%</col> base damage.`;
    return string0;
  };
  V[13040] = { vb: [], vp: [], vc: [], calls: [4356, 6643, 6687, 12478, 13042, 20958] };
  S[13040] = function (int0, int1) {
    if (R.eq(R.call(4356, []), 0)) {
      return 0;
    }
    if ((R.eq(R.call(20958, [int0]), 1) && (!R.eq(int0, 5)))) {
      return R.call(13042, [int0, int1]);
    }
    var int2 = R.invObj(94, 1);
    if ((((int0 <= 0) || (int0 > R.enumCount(681))) && (!R.eq(int0, 31)))) {
      R.call(12478, [`Invalid stat perk attempting to be activated! $stat = ${R.s(R.str(int0, 10))}`]);
      return 0;
    }
    if (((!R.eq(int2, (-1 | 0))) && R.eq(R.call(6687, [int2, int0, int1]), 1))) {
      return R.call(13042, [int0, int1]);
    }
    int2 = R.call(6643, []);
    if (((!R.eq(int2, (-1 | 0))) && R.eq(R.call(6687, [int2, int0, int1]), 1))) {
      return R.call(13042, [int0, int1]);
    }
    return 0;
  };
  V[13041] = { vb: [], vp: [], vc: [], calls: [470, 4034] };
  S[13041] = function (int0, int1, int2) {
    var int3 = 2;
    var int4 = 0;
    var int5 = 0;
    if (R.eq(R.call(4034, [int0]), 1)) {
      var int0 = 20767;
    }
    if (R.eq(R.itemParam(int0, 6295), 1)) {
      int3 = 3;
    }
    while ((int3 > 0)) {
      [int4, int5] = R.call(470, [int0, int3]);
      if (R.eq(int4, int1)) {
        if ((R.eq(int2, 1) && R.eq(int5, 0))) {
          return 0;
        }
        return 1;
      }
      int3 = ((int3) - (1) | 0);
    }
    return 0;
  };
  V[13042] = { vb: [], vp: [], vc: [], calls: [471, 13048] };
  S[13042] = function (int0, int1) {
    var int2 = 0;
    switch (R.key(int0)) {
      case 20:
      {
        if (R.eq(int1, 1)) {
          int2 = 20;
        } else {
          R.call(471, [int0, int1]);
          return 1;
        }
        break;
      }
      case 15:
      {
        int2 = 5;
        break;
      }
      case 21:
      {
        if (R.eq(int1, 1)) {
          R.call(471, [int0, int1]);
          return 1;
        }
        int2 = 5;
        break;
      }
      case 22:
      {
        if (R.eq(int1, 1)) {
          int2 = 7;
        }
        int2 = 5;
        break;
      }
      case 1:
      {
        if (R.eq(int1, 1)) {
          R.call(471, [int0, int1]);
          return 1;
        }
        int2 = 2;
        break;
      }
      case 24:
      {
        int2 = 2;
        break;
      }
      case 19:
      {
        int2 = 1;
        break;
      }
      case 29:
      {
        if (R.eq(int1, 1)) {
          int2 = 10;
        } else {
          R.call(471, [int0, int1]);
          return 1;
        }
        break;
      }
      case 3:
      {
        if (R.eq(int1, 1)) {
          int2 = 10;
        }
        break;
      }
      default:
      {
        R.call(471, [int0, int1]);
        return 1;
      }
    }
    if (((int2 > 0) && ((R.random(100) < int2) || R.eq(R.call(13048, []), 1)))) {
      R.call(471, [int0, int1]);
      return 1;
    }
    return 0;
  };
  V[13048] = { vb: [], vp: [10719], vc: [], calls: [12517] };
  S[13048] = function () {
    if ((R.ifZero() >= 2)) {
      if (((R.vp(10719) >= 0) && R.eq(R.invObj(93, R.vp(10719)), 5733))) {
        return 1;
      }
      varplayer_10719 = R.call(12517, [93, 5733, 0]);
      if ((R.vp(10719) >= 0)) {
        return 1;
      }
    }
    return 0;
  };
  V[13065] = { vb: [], vp: [], vc: [], calls: [15973] };
  S[13065] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- On <col=3366FF>Magic attack</col> gain a stack of <sprite=14765><nbsp><col=ffffff>${R.s(R.structParam(44820, 2794))}</col> for <col=ffffff>${R.s(R.call(15973, [33, 1]))}.`;
    return string0;
  };
  V[13086] = { vb: [], vp: [], vc: [], calls: [7653] };
  S[13086] = function (int0, string0) {
    var string0 = `${R.s(string0)}<br>- <col=ffffff>Basic</col> <col=3366FF>Magic abilities</col> gain <col=ffffff>+${R.s(R.call(7653, [10, 1, 1, 0, 1]))}%</col> base damage per stack.`;
    return string0;
  };
  V[13103] = { vb: [], vp: [], vc: [], calls: [] };
  S[13103] = function (int0, int1) {
    var int2 = (-1 | 0);
    if (((int1 < 0) || (R.invSize(int0) <= int1))) {
      return int2;
    }
    var int3 = R.invObj(int0, int1);
    if (R.eq(int3, (-1 | 0))) {
      return int2;
    }
    if ((!R.eq(R.itemParam(int3, 2500), (-1 | 0)))) {
      int2 = R.invVar(int0, int1, 4289);
    }
    return int2;
  };
  V[13107] = { vb: [], vp: [], vc: [], calls: [17454, 17710, 17727] };
  S[13107] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [R.call(17454, [int0])]))}.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>Spirit</col>.`;
    string0 = `${R.s(string0)}<br><br><col=ffffff>${R.s(R.call(17727, [32349, 0, int1]))}`;
    return string0;
  };
  V[13110] = { vb: [35142, 35143, 35144, 35145, 35146, 35147, 35148, 35149, 35150, 35151, 35152, 35153, 35154, 35155, 35156, 35157, 35158, 35159, 35160, 35161, 35162, 35163, 35164, 35165, 35166, 35167, 35168, 35169, 35170, 35171, 35172, 35173, 35174, 35175, 35176, 35177, 35178, 35179], vp: [], vc: [], calls: [10813, 10814] };
  S[13110] = function (int0) {
    var int1 = (-1 | 0);
    var int2 = (-1 | 0);
    switch (R.key(int0)) {
      case 0:
      {
        int1 = R.vb(35142);
        int2 = R.vb(35143);
        break;
      }
      case 1:
      {
        int1 = R.vb(35144);
        int2 = R.vb(35145);
        break;
      }
      case 2:
      {
        int1 = R.vb(35146);
        int2 = R.vb(35147);
        break;
      }
      case 3:
      {
        int1 = R.vb(35148);
        int2 = R.vb(35149);
        break;
      }
      case 4:
      {
        int1 = R.vb(35150);
        int2 = R.vb(35151);
        break;
      }
      case 5:
      {
        int1 = R.vb(35152);
        int2 = R.vb(35153);
        break;
      }
      case 6:
      {
        int1 = R.vb(35154);
        int2 = R.vb(35155);
        break;
      }
      case 7:
      {
        int1 = R.vb(35156);
        int2 = R.vb(35157);
        break;
      }
      case 8:
      {
        int1 = R.vb(35158);
        int2 = R.vb(35159);
        break;
      }
      case 9:
      {
        int1 = R.vb(35160);
        int2 = R.vb(35161);
        break;
      }
      case 10:
      {
        int1 = R.vb(35162);
        int2 = R.vb(35163);
        break;
      }
      case 11:
      {
        int1 = R.vb(35164);
        int2 = R.vb(35165);
        break;
      }
      case 12:
      {
        int1 = R.vb(35166);
        int2 = R.vb(35167);
        break;
      }
      case 13:
      {
        int1 = R.vb(35168);
        int2 = R.vb(35169);
        break;
      }
      case 14:
      {
        int1 = R.vb(35170);
        int2 = R.vb(35171);
        break;
      }
      case 15:
      {
        int1 = R.vb(35172);
        int2 = R.vb(35173);
        break;
      }
      case 16:
      {
        int1 = R.vb(35174);
        int2 = R.vb(35175);
        break;
      }
      case 17:
      {
        int1 = R.vb(35176);
        int2 = R.vb(35177);
        break;
      }
      case 18:
      {
        int1 = R.vb(35178);
        int2 = R.vb(35179);
        break;
      }
    }
    return [R.call(10813, [int1]), R.call(10814, [int2])];
  };
  V[13240] = { vb: [], vp: [], vc: [], calls: [17709, 17721, 18561] };
  S[13240] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17721, [int0, 38, 45, ((45) + (10) | 0), 0, int1]))} to the target and up to ${R.s(R.call(18561, [4]))} within ${R.s(R.call(17709, [1]))} of the target.`;
    string0 = `${R.s(string0)}<br>- Damage is increased by <col=ffffff>20%</col> for each <col=ffffff>Valour</col> stack.`;
    string0 = `${R.s(string0)}<br>- Consumes <col=ffffff>all Valour</col> stacks.`;
    return string0;
  };
  V[13258] = { vb: [], vp: [], vc: [], calls: [] };
  S[13258] = function (int0, int1, int2, int3, int4) {
    var int5 = 0;
    if (((int4 > 0) && R.eq(R.ifNone(int3, ((int4) - (1) | 0)), 1))) {
      int5 = ((R.ifZero()) + (R.ifZero()) | 0);
    }
    var int6 = R.ifNoop(int2);
    var int7 = 300;
    var int8 = 16;
    if (R.eq(int3, 97386510)) {
      int7 = 350;
      var int0 = R.min(int0, int7);
      int8 = 0;
      if ((int6 < int0)) {
        int6 = R.max(int6, int0);
      }
    } else {
      int0 = R.min(int0, int7);
      int8 = 14;
      if ((int6 < ((int0) + (20) | 0))) {
        int6 = ((int0) + (20) | 0);
      }
    }
    R.ifNoop(int6, ((((int5) + (int1) | 0)) + (int8) | 0), 0, 0, int2);
    R.ifNoop(int3, 4, ((int4 = (int4 + (1)) | 0) - (1)));
    R.ifNoop(int0, int1, 0, 0);
    R.ifNoop(0, int5, 1, 0);
    return int4;
  };
  V[13399] = { vb: [], vp: [], vc: [], calls: [17278, 17279, 17280, 17281] };
  S[13399] = function (int0, int1) {
    var string0 = "";
    var int2 = R.call(17278, [int0]);
    var int3 = R.call(17280, [int0]);
    var string1 = "";
    var string2 = "";
    if (R.eq(int1, 1)) {
      string1 = R.str(R.abs(int2), 10);
      string2 = R.str(R.abs(int3), 10);
    } else {
      string1 = R.call(17279, [int0]);
      string2 = R.call(17281, [int0]);
    }
    if ((int2 > 0)) {
      string0 = `Restores <col=00ff00>${R.s(string1)}</col> special move points`;
    } else {
      if ((int2 < 0)) {
        string0 = `Removes <col=ff0000>${R.s(string1)}</col> special move points`;
      }
    }
    if ((int3 > 0)) {
      if ((R.len(string0) > 0)) {
        string0 = R.cat(string0, `, and renews <col=00ff00>${R.s(string2)}</col> special move points over the duration`);
      } else {
        string0 = R.cat(string0, `Renews <col=00ff00>${R.s(string2)}</col> special move points over the duration`);
      }
    } else {
      if ((int3 < 0)) {
        if ((R.len(string0) > 0)) {
          string0 = R.cat(string0, `, and drains <col=ff0000>${R.s(string2)}</col> special move points over the duration`);
        } else {
          string0 = R.cat(string0, `Drains <col=ff0000>${R.s(string2)}</col> special move points over the duration`);
        }
      }
    }
    if ((R.len(string0) > 0)) {
      string0 = R.cat(string0, ".");
    }
    return string0;
  };
  V[13402] = { vb: [], vp: [], vc: [], calls: [134] };
  S[13402] = function () {
    return `Spooky time is active! You'll receive a ${R.s(R.str(R.call(134, []), 10))}% experience buff while skilling inside the Draynor Manor grounds. (This is increased by an additional ${R.s(R.str(2, 10))}% for each Halloween miniquest you complete!)`;
  };
  V[13403] = { vb: [48221, 48222, 48223, 50295, 50296, 52199], vp: [], vc: [], calls: [] };
  S[13403] = function () {
    var int0 = 0;
    if (R.eq(R.vb(48221), 7)) {
      int0 = ((int0) + (1) | 0);
    }
    if (R.eq(R.vb(48222), 7)) {
      int0 = ((int0) + (1) | 0);
    }
    if (R.eq(R.vb(48223), 7)) {
      int0 = ((int0) + (1) | 0);
    }
    if (R.eq(R.vb(50296), 7)) {
      int0 = ((int0) + (1) | 0);
    }
    if (R.eq(R.vb(50295), 7)) {
      int0 = ((int0) + (1) | 0);
    }
    if (R.eq(R.vb(52199), 7)) {
      int0 = ((int0) + (1) | 0);
    }
    return int0;
  };
  V[13424] = { vb: [], vp: [], vc: [], calls: [] };
  S[13424] = function (int0, string0, string1) {
    switch (R.key(int0)) {
      case 1:
      {
        return string0;
      }
      case 0:
      {
        return string1;
      }
    }
    return "";
  };
  V[13427] = { vb: [], vp: [], vc: [], calls: [] };
  S[13427] = function (int0, int1) {
    if (R.eq(int0, (-1 | 0))) {
      return (-1 | 0);
    }
    var int2 = int0;
    if (R.eq(R.itemParam(int0, 4949), 1)) {
      int2 = R.enumReverse(33, 33, 9904, int0, 0);
    }
    return int2;
  };
  V[13562] = { vb: [12304, 12305, 12306, 12307, 12308, 12309], vp: [], vc: [], calls: [] };
  S[13562] = function () {
    var int0 = ((((((((((R.vb(12307)) + (R.vb(12308)) | 0)) + (R.vb(12305)) | 0)) + (R.vb(12304)) | 0)) + (R.vb(12306)) | 0)) + (R.vb(12309)) | 0);
    if (R.eq(int0, 6)) {
      return 1;
    }
    return 0;
  };
  V[13563] = { vb: [], vp: [], vc: [], calls: [] };
  S[13563] = function (int0) {
    switch (R.key(int0)) {
      case 22421:
      {
        return 1;
      }
      case 22422:
      {
        return 1;
      }
      case 22423:
      {
        return 1;
      }
      case 22425:
      {
        return 1;
      }
      case 22424:
      {
        return 1;
      }
      case 22420:
      {
        return 1;
      }
      case 41032:
      {
        return 1;
      }
      case 41030:
      {
        return 1;
      }
    }
    return 0;
  };
  V[13564] = { vb: [1061, 1063, 1065, 1083, 1085, 1087, 12304, 12305, 12306, 12307, 12308, 12309], vp: [], vc: [], calls: [12780, 13562, 13563] };
  S[13564] = function (int0, int1) {
    if ((R.eq(R.call(12780, []), 1) && R.eq(R.call(13562, []), 1))) {
      return int0;
    }
    if (R.eq(R.call(13563, [int1]), 0)) {
      if (R.eq(R.call(12780, []), 1)) {
        return int0;
      }
      switch (R.key(int0)) {
        case 0:
        {
          if (R.eq(R.vb(1085), 1)) {
            return int0;
          }
          break;
        }
        case 1:
        {
          if (R.eq(R.vb(1083), 1)) {
            return int0;
          }
          break;
        }
        case 2:
        {
          if (R.eq(R.vb(1065), 1)) {
            return int0;
          }
          break;
        }
        case 3:
        {
          if (R.eq(R.vb(1061), 1)) {
            return int0;
          }
          break;
        }
        case 4:
        {
          if (R.eq(R.vb(1063), 1)) {
            return int0;
          }
          break;
        }
        case 5:
        {
          if (R.eq(R.vb(1087), 1)) {
            return int0;
          }
          break;
        }
      }
      if (R.eq(R.vb(1085), 1)) {
        var int0 = 0;
      } else {
        if (R.eq(R.vb(1083), 1)) {
          int0 = 1;
        } else {
          if (R.eq(R.vb(1065), 1)) {
            int0 = 2;
          } else {
            if (R.eq(R.vb(1061), 1)) {
              int0 = 3;
            } else {
              if (R.eq(R.vb(1063), 1)) {
                int0 = 4;
              } else {
                if (R.eq(R.vb(1087), 1)) {
                  int0 = 5;
                }
              }
            }
          }
        }
      }
    } else {
      if (R.eq(R.call(13562, []), 1)) {
        return int0;
      }
      switch (R.key(int0)) {
        case 0:
        {
          if (R.eq(R.vb(12307), 1)) {
            return int0;
          }
          break;
        }
        case 1:
        {
          if (R.eq(R.vb(12308), 1)) {
            return int0;
          }
          break;
        }
        case 2:
        {
          if (R.eq(R.vb(12305), 1)) {
            return int0;
          }
          break;
        }
        case 3:
        {
          if (R.eq(R.vb(12304), 1)) {
            return int0;
          }
          break;
        }
        case 4:
        {
          if (R.eq(R.vb(12306), 1)) {
            return int0;
          }
          break;
        }
        case 5:
        {
          if (R.eq(R.vb(12309), 1)) {
            return int0;
          }
          break;
        }
      }
      if (R.eq(R.vb(12307), 1)) {
        int0 = 0;
      } else {
        if (R.eq(R.vb(12308), 1)) {
          int0 = 1;
        } else {
          if (R.eq(R.vb(12305), 1)) {
            int0 = 2;
          } else {
            if (R.eq(R.vb(12304), 1)) {
              int0 = 3;
            } else {
              if (R.eq(R.vb(12306), 1)) {
                int0 = 4;
              } else {
                if (R.eq(R.vb(12309), 1)) {
                  int0 = 5;
                }
              }
            }
          }
        }
      }
    }
    return int0;
  };
  V[13574] = { vb: [21558, 21559, 22946, 22947, 22948, 22949, 22950, 22951, 22952, 22953, 22954, 22955, 22956, 22957, 22958, 22959, 22960, 22961, 22962, 22963, 22964, 22965, 22966, 22967, 22968, 22969, 22970, 22971, 28643, 28644, 29857, 29858, 29859, 29860, 29861, 29862, 29863, 29864, 29865, 29866, 29867, 29868, 29869, 29870, 29871, 29872, 29873, 29874, 29875, 29876, 29877, 29878, 29879, 29880, 29881, 29882, 29883, 29884, 30840, 30841, 30842, 30843, 30844, 30845, 30846, 30847, 30848, 30849, 30850, 30851, 30852, 30853, 30854, 30855, 32624, 32625, 34869, 34870, 36178, 36179, 40566, 40567, 41049, 41050, 41051, 41052, 41053, 41054, 41055, 41056, 41057, 41058, 41059, 41060, 41450, 41451, 41452, 41453, 41454, 41455, 41456, 41457, 41458, 41459, 41460, 41461, 41546, 41547, 43415, 43416, 43417, 43418, 43419, 43420, 43421, 43422, 43423, 43424, 43425, 43426, 48687, 48688, 48689, 48690, 49295, 49296, 49727, 49728, 49729, 49730, 50125, 50126, 50332, 50333, 50334, 50335, 51440, 51441, 51442, 51443, 53283, 53284, 53285, 53286, 54674, 54675, 54676, 54677, 55730, 55731, 55732, 55733, 55734, 55735, 55736, 55737, 55738, 55739, 55740, 55741, 55987, 55988, 58187, 58188, 58189, 58190, 60353, 60354, 60742, 60743, 60744, 60745], vp: [], vc: [], calls: [] };
  S[13574] = function () {
    var int0 = 0;
    int0 = ((int0) + (((R.vb(22969)) + (Math.imul(R.vb(29880), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(22949)) + (Math.imul(R.vb(29860), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(22968)) + (Math.imul(R.vb(29879), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(22950)) + (Math.imul(R.vb(29861), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(22956)) + (Math.imul(R.vb(29867), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(22957)) + (Math.imul(R.vb(29868), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(22953)) + (Math.imul(R.vb(29864), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(22954)) + (Math.imul(R.vb(29865), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(22958)) + (Math.imul(R.vb(29869), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(22959)) + (Math.imul(R.vb(29870), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(22946)) + (Math.imul(R.vb(29857), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(22947)) + (Math.imul(R.vb(29858), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(22964)) + (Math.imul(R.vb(29875), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(22965)) + (Math.imul(R.vb(29876), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(22951)) + (Math.imul(R.vb(29862), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(22948)) + (Math.imul(R.vb(29859), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(22960)) + (Math.imul(R.vb(29871), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(22961)) + (Math.imul(R.vb(29872), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(22962)) + (Math.imul(R.vb(29873), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(22963)) + (Math.imul(R.vb(29874), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(22966)) + (Math.imul(R.vb(29877), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(22967)) + (Math.imul(R.vb(29878), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(22955)) + (Math.imul(R.vb(29866), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(22952)) + (Math.imul(R.vb(29863), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(22970)) + (Math.imul(R.vb(29881), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(22971)) + (Math.imul(R.vb(29882), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(28643)) + (Math.imul(R.vb(29883), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(28644)) + (Math.imul(R.vb(29884), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(30841)) + (Math.imul(R.vb(30849), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(30845)) + (Math.imul(R.vb(30853), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(30840)) + (Math.imul(R.vb(30848), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(30844)) + (Math.imul(R.vb(30852), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(30843)) + (Math.imul(R.vb(30851), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(30847)) + (Math.imul(R.vb(30855), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(30842)) + (Math.imul(R.vb(30850), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(30846)) + (Math.imul(R.vb(30854), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(32624)) + (Math.imul(R.vb(32625), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(34869)) + (Math.imul(R.vb(34870), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(36178)) + (Math.imul(R.vb(36179), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(40566)) + (Math.imul(R.vb(40567), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(41050)) + (Math.imul(R.vb(41056), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(41049)) + (Math.imul(R.vb(41055), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(41052)) + (Math.imul(R.vb(41058), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(41051)) + (Math.imul(R.vb(41057), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(41054)) + (Math.imul(R.vb(41060), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(41053)) + (Math.imul(R.vb(41059), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(41451)) + (Math.imul(R.vb(41457), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(41450)) + (Math.imul(R.vb(41456), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(41453)) + (Math.imul(R.vb(41459), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(41452)) + (Math.imul(R.vb(41458), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(41455)) + (Math.imul(R.vb(41461), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(41454)) + (Math.imul(R.vb(41460), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(43416)) + (Math.imul(R.vb(43422), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(43415)) + (Math.imul(R.vb(43421), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(43418)) + (Math.imul(R.vb(43424), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(43417)) + (Math.imul(R.vb(43423), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(43420)) + (Math.imul(R.vb(43426), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(43419)) + (Math.imul(R.vb(43425), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(48687)) + (Math.imul(R.vb(48689), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(48688)) + (Math.imul(R.vb(48690), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(49295)) + (Math.imul(R.vb(49296), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(49727)) + (Math.imul(R.vb(49729), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(49728)) + (Math.imul(R.vb(49730), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(41546)) + (Math.imul(R.vb(50125), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(41547)) + (Math.imul(R.vb(50126), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(21558)) + (Math.imul(R.vb(21559), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(50332)) + (Math.imul(R.vb(50334), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(50333)) + (Math.imul(R.vb(50335), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(51440)) + (Math.imul(R.vb(51442), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(51441)) + (Math.imul(R.vb(51443), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(53283)) + (Math.imul(R.vb(53285), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(53284)) + (Math.imul(R.vb(53286), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(54674)) + (Math.imul(R.vb(54676), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(54675)) + (Math.imul(R.vb(54677), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(55730)) + (Math.imul(R.vb(55736), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(55731)) + (Math.imul(R.vb(55737), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(55732)) + (Math.imul(R.vb(55738), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(55733)) + (Math.imul(R.vb(55739), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(55734)) + (Math.imul(R.vb(55740), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(55735)) + (Math.imul(R.vb(55741), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(55987)) + (Math.imul(R.vb(55988), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(58187)) + (Math.imul(R.vb(58189), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(58188)) + (Math.imul(R.vb(58190), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(60353)) + (Math.imul(R.vb(60354), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(60742)) + (Math.imul(R.vb(60744), 60000)) | 0)) | 0);
    int0 = ((int0) + (((R.vb(60743)) + (Math.imul(R.vb(60745), 60000)) | 0)) | 0);
    return int0;
  };
  V[13727] = { vb: [50570, 50571, 50572], vp: [], vc: [], calls: [] };
  S[13727] = function (int0) {
    switch (R.key(int0)) {
      case 25:
      {
        return R.vb(50570);
      }
      case 41:
      {
        return R.vb(50571);
      }
      case 45:
      {
        return R.vb(50572);
      }
    }
    return 0;
  };
  V[13776] = { vb: [16497, 16498, 16499, 16500, 16502, 16503, 16504, 16505, 27718, 44598, 44599, 44600, 44601, 44602, 44603, 48172, 48173, 48174, 48175, 48176, 57685, 57686, 61469, 61470, 61471, 61472, 61473, 61474, 61475, 61476, 61477, 61478, 61479], vp: [], vc: [5121, 5122], calls: [470, 2532, 2535, 4034, 4743, 6646, 6666, 7229, 7361, 10021, 13777, 14695, 16377, 18309, 18330, 21002, 21003] };
  S[13776] = function (int0, int1, int2, int3) {
    var int4 = 16;
    var int5 = 1;
    var int6 = 0;
    var int7 = R.ifNoop(int1);
    var int8 = 32;
    var int9 = R.ifNoop(int1);
    var int10 = (-1 | 0);
    var int11 = 0;
    var int12 = 0;
    var int13 = 0;
    var int14 = 0;
    var int15 = 0;
    var int16 = 0;
    var int17 = 0;
    var int18 = 0;
    var int19 = 0;
    var int20 = 0;
    var int21 = 0;
    var int22 = 0;
    var int23 = int3;
    if (((int3 > 0) && R.eq(R.ifNone(int2, ((int3) - (1) | 0)), 1))) {
      int6 = ((((R.ifZero()) + (R.ifZero()) | 0)) + (R.idiv(16, 4)) | 0);
    }
    if (R.eq(R.itemParam(int0, 6835), 1)) {
      int12 = ((((((R.call(10021, [R.invVar(R.vc(5121), R.vc(5122), 33777)])) + (R.call(10021, [R.invVar(R.vc(5121), R.vc(5122), 33778)])) | 0)) + (R.call(10021, [R.invVar(R.vc(5121), R.vc(5122), 33779)])) | 0)) + (R.call(10021, [R.invVar(R.vc(5121), R.vc(5122), 21605)])) | 0);
      if ((int12 > 0)) {
        R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
        R.ifNoop(36, 32, 0, 0);
        R.ifNoop(((0) - (Math.imul(((int12) - (1) | 0), 20)) | 0), int6, int5, 0);
        R.ifNoop(R.enumValue(0, 33, 11885, R.invVar(R.vc(5121), R.vc(5122), 33774)), R.invVar(R.vc(5121), R.vc(5122), 33777));
        R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
        R.ifNoop(36, 32, 0, 0);
        R.ifNoop(((40) - (Math.imul(((int12) - (1) | 0), 20)) | 0), int6, int5, 0);
        R.ifNoop(R.enumValue(0, 33, 11885, R.invVar(R.vc(5121), R.vc(5122), 33775)), R.invVar(R.vc(5121), R.vc(5122), 33778));
        R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
        R.ifNoop(36, 32, 0, 0);
        R.ifNoop(((80) - (Math.imul(((int12) - (1) | 0), 20)) | 0), int6, int5, 0);
        R.ifNoop(R.enumValue(0, 33, 11885, R.invVar(R.vc(5121), R.vc(5122), 33776)), R.invVar(R.vc(5121), R.vc(5122), 33779));
        R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
        R.ifNoop(36, 32, 0, 0);
        R.ifNoop(((120) - (Math.imul(((int12) - (1) | 0), 20)) | 0), int6, int5, 0);
        R.ifNoop(R.enumValue(0, 33, 11885, R.invVar(R.vc(5121), R.vc(5122), 21604)), R.invVar(R.vc(5121), R.vc(5122), 21605));
      }
    }
    if (R.eq(R.call(4034, [int0]), 1)) {
      var int0 = 20767;
    }
    if ((R.eq(R.itemParam(int0, 6295), 1) || R.eq(R.itemParam(int0, 7393), 1))) {
      [int14, int13] = R.call(470, [int0, 1]);
      [int15, int13] = R.call(470, [int0, 2]);
      if (R.eq(R.itemParam(int0, 6295), 1)) {
        [int16, int13] = R.call(470, [int0, 3]);
      }
      if (R.eq(int15, 0)) {
        int15 = int16;
        int16 = 0;
      }
      if (R.eq(int14, 0)) {
        int14 = int15;
        int15 = int16;
        int16 = 0;
      }
      int12 = ((((R.call(10021, [int14])) + (R.call(10021, [int15])) | 0)) + (R.call(10021, [int16])) | 0);
      if ((!R.eq(int14, 0))) {
        R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
        R.ifNoop(25, 25, 0, 0);
        R.ifNoop(((0) - (Math.imul(((int12) - (1) | 0), 20)) | 0), int6, 1, 0);
        if ((!R.eq(int14, 31))) {
          R.ifNoop(R.enumValue(0, 23, 371, int14));
        } else {
          R.ifNoop(32075);
        }
        int8 = 25;
      }
      if ((!R.eq(int15, 0))) {
        R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
        R.ifNoop(25, 25, 0, 0);
        R.ifNoop(((40) - (Math.imul(((int12) - (1) | 0), 20)) | 0), int6, 1, 0);
        if ((!R.eq(int15, 31))) {
          R.ifNoop(R.enumValue(0, 23, 371, int15));
        } else {
          R.ifNoop(32075);
        }
      }
      if ((!R.eq(int16, 0))) {
        R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
        R.ifNoop(25, 25, 0, 0);
        R.ifNoop(((80) - (Math.imul(((int12) - (1) | 0), 20)) | 0), int6, 1, 0);
        if ((!R.eq(int16, 31))) {
          R.ifNoop(R.enumValue(0, 23, 371, int16));
        } else {
          R.ifNoop(32075);
        }
      }
      int9 = R.max(int9, Math.imul(int12, ((25) + (20) | 0)));
    } else {
      switch (R.key(int0)) {
        case 18338:
        {
          int4 = (-55 | 0);
          int9 = R.max(190, int9);
          [int12, int17, int18, int19] = R.call(4743, [R.invVar(R.vc(5121), R.vc(5122), 2154)]);
          R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
          R.ifNoop(36, 32, 0, 0);
          R.ifNoop(int4, int6, int5, 0);
          R.ifNoop(1623, int12);
          R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
          R.ifNoop(36, 32, 0, 0);
          R.ifNoop(((int4) + (40) | 0), int6, int5, 0);
          R.ifNoop(1621, int17);
          R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
          R.ifNoop(36, 32, 0, 0);
          R.ifNoop(((int4) + (80) | 0), int6, int5, 0);
          R.ifNoop(1619, int18);
          R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
          R.ifNoop(36, 32, 0, 0);
          R.ifNoop(((int4) + (120) | 0), int6, int5, 0);
          R.ifNoop(1617, int19);
          int12 = 4;
          break;
        }
        case 31455:
        {
          int4 = (-135 | 0);
          int9 = R.max(340, int9);
          R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
          R.ifNoop(36, 32, 0, 0);
          R.ifNoop(int4, int6, int5, 0);
          R.ifNoop(1623, R.vb(61469));
          R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
          R.ifNoop(36, 32, 0, 0);
          R.ifNoop(((int4) + (40) | 0), int6, int5, 0);
          R.ifNoop(1621, R.vb(61470));
          R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
          R.ifNoop(36, 32, 0, 0);
          R.ifNoop(((int4) + (80) | 0), int6, int5, 0);
          R.ifNoop(1619, R.vb(61471));
          R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
          R.ifNoop(36, 32, 0, 0);
          R.ifNoop(((int4) + (120) | 0), int6, int5, 0);
          R.ifNoop(1617, R.vb(61472));
          R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
          R.ifNoop(36, 32, 0, 0);
          R.ifNoop(((int4) + (160) | 0), int6, int5, 0);
          R.ifNoop(1631, R.vb(61473));
          R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
          R.ifNoop(36, 32, 0, 0);
          R.ifNoop(((int4) + (200) | 0), int6, int5, 0);
          R.ifNoop(1625, R.vb(61475));
          R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
          R.ifNoop(36, 32, 0, 0);
          R.ifNoop(((int4) + (240) | 0), int6, int5, 0);
          R.ifNoop(1627, R.vb(61474));
          R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
          R.ifNoop(36, 32, 0, 0);
          R.ifNoop(((int4) + (280) | 0), int6, int5, 0);
          R.ifNoop(1629, R.vb(61476));
          int12 = 8;
          break;
        }
        case 50805:
        {
          int4 = (-135 | 0);
          int9 = R.max(340, int9);
          R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
          R.ifNoop(36, 32, 0, 0);
          R.ifNoop(int4, int6, int5, 0);
          R.ifNoop(1623, R.vb(48172));
          R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
          R.ifNoop(36, 32, 0, 0);
          R.ifNoop(((int4) + (40) | 0), int6, int5, 0);
          R.ifNoop(1621, R.vb(48173));
          R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
          R.ifNoop(36, 32, 0, 0);
          R.ifNoop(((int4) + (80) | 0), int6, int5, 0);
          R.ifNoop(1619, R.vb(48174));
          R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
          R.ifNoop(36, 32, 0, 0);
          R.ifNoop(((int4) + (120) | 0), int6, int5, 0);
          R.ifNoop(1617, R.vb(48175));
          R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
          R.ifNoop(36, 32, 0, 0);
          R.ifNoop(((int4) + (160) | 0), int6, int5, 0);
          R.ifNoop(1631, R.vb(48176));
          R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
          R.ifNoop(36, 32, 0, 0);
          R.ifNoop(((int4) + (200) | 0), int6, int5, 0);
          R.ifNoop(1625, R.vb(61477));
          R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
          R.ifNoop(36, 32, 0, 0);
          R.ifNoop(((int4) + (240) | 0), int6, int5, 0);
          R.ifNoop(1627, R.vb(61478));
          R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
          R.ifNoop(36, 32, 0, 0);
          R.ifNoop(((int4) + (280) | 0), int6, int5, 0);
          R.ifNoop(1629, R.vb(61479));
          int12 = 8;
          break;
        }
        case 38934:
        {
          int9 = R.max(260, int9);
          int4 = (-95 | 0);
          R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
          R.ifNoop(36, 32, 0, 0);
          R.ifNoop(int4, int6, int5, 0);
          R.ifNoop(30139, R.vb(44598));
          R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
          R.ifNoop(36, 32, 0, 0);
          R.ifNoop(((int4) + (40) | 0), int6, int5, 0);
          R.ifNoop(30140, R.vb(44599));
          R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
          R.ifNoop(36, 32, 0, 0);
          R.ifNoop(((int4) + (80) | 0), int6, int5, 0);
          R.ifNoop(30141, R.vb(44600));
          R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
          R.ifNoop(36, 32, 0, 0);
          R.ifNoop(((int4) + (120) | 0), int6, int5, 0);
          R.ifNoop(30142, R.vb(44601));
          R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
          R.ifNoop(36, 32, 0, 0);
          R.ifNoop(((int4) + (160) | 0), int6, int5, 0);
          R.ifNoop(30143, R.vb(44602));
          R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
          R.ifNoop(36, 32, 0, 0);
          R.ifNoop(((int4) + (200) | 0), int6, int5, 0);
          R.ifNoop(30144, R.vb(44603));
          int12 = 6;
          break;
        }
        case 47836:
        case 63592:
        {
          int21 = R.enumCount(15320);
          while ((int20 < int21)) {
            int10 = R.enumValue(0, 33, 15320, int20);
            int12 = R.call(7361, [int10]);
            R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
            R.ifNoop(36, 32, 0, 0);
            R.ifNoop(int10, int12);
            int20 = ((int20) + (1) | 0);
          }
          int20 = 0;
          int12 = ((int3) - (int23) | 0);
          while ((int23 < int3)) {
            if (R.eq(R.ifNone(int2, ((int23 = (int23 + (1)) | 0) - (1))), 1)) {
              R.call(7229, [5, int12, int6, int20]);
              if (R.eq(R.mod(int20, 5), 4)) {
                int12 = R.abs((int12 = (int12 + (-1)) | 0));
              }
              int20 = ((int20) + (1) | 0);
            }
          }
          if ((int20 > 0)) {
            int8 = ((35) + (Math.imul(35, R.idiv(((int20) - (1) | 0), 5))) | 0);
            int9 = R.max(int9, ((Math.imul(36, R.min(5, int20))) + (20) | 0));
          }
          int12 = ((int12) + (1) | 0);
          break;
        }
        case 48056:
        {
          if (R.eq(R.call(6666, []), 1)) {
            int21 = R.enumCount(15289);
            while ((int20 < int21)) {
              int10 = R.enumValue(0, 33, 15289, int20);
              int12 = R.call(6646, [int10]);
              if ((int12 > 0)) {
                R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
                R.ifNoop(36, 32, 0, 0);
                R.ifNoop(int10, int12);
              }
              int20 = ((int20) + (1) | 0);
            }
            int20 = 0;
            int12 = ((int3) - (int23) | 0);
            while ((int23 < int3)) {
              if (R.eq(R.ifNone(int2, ((int23 = (int23 + (1)) | 0) - (1))), 1)) {
                R.call(7229, [5, int12, int6, int20]);
                if (R.eq(R.mod(int20, 5), 4)) {
                  int12 = R.abs((int12 = (int12 + (-1)) | 0));
                }
                int20 = ((int20) + (1) | 0);
              }
            }
            if ((int20 > 0)) {
              int8 = ((35) + (Math.imul(35, R.idiv(((int20) - (1) | 0), 5))) | 0);
              int9 = R.max(int9, ((Math.imul(36, R.min(5, int20))) + (20) | 0));
            }
            int12 = ((int12) + (1) | 0);
          }
          break;
        }
        case 63584:
        {
          if (R.eq(R.call(21003, []), 1)) {
            int21 = R.enumCount(13250);
            while ((int20 < int21)) {
              int10 = R.enumValue(0, 33, 13250, int20);
              int12 = R.call(21002, [int10]);
              if ((int12 > 0)) {
                R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
                R.ifNoop(36, 32, 0, 0);
                R.ifNoop(int10, int12);
              }
              int20 = ((int20) + (1) | 0);
            }
            int20 = 0;
            int12 = ((int3) - (int23) | 0);
            while ((int23 < int3)) {
              if (R.eq(R.ifNone(int2, ((int23 = (int23 + (1)) | 0) - (1))), 1)) {
                R.call(7229, [5, int12, int6, int20]);
                if (R.eq(R.mod(int20, 5), 4)) {
                  int12 = R.abs((int12 = (int12 + (-1)) | 0));
                }
                int20 = ((int20) + (1) | 0);
              }
            }
            if ((int20 > 0)) {
              int8 = ((35) + (Math.imul(35, R.idiv(((int20) - (1) | 0), 5))) | 0);
              int9 = R.max(int9, ((Math.imul(36, R.min(5, int20))) + (20) | 0));
            }
            int12 = ((int12) + (1) | 0);
          }
          break;
        }
        case 63588:
        {
          if ((!R.eq(R.invFree(1012), R.invSize(1012)))) {
            while ((int20 < 3)) {
              int10 = R.invObj(1012, int20);
              if ((!R.eq(int10, (-1 | 0)))) {
                int12 = R.invNum(1012, int20);
                if ((int12 > 0)) {
                  R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
                  R.ifNoop(36, 32, 0, 0);
                  R.ifNoop(int10, int12);
                }
              }
              int20 = ((int20) + (1) | 0);
            }
            int20 = 0;
            int12 = ((int3) - (int23) | 0);
            while ((int23 < int3)) {
              if (R.eq(R.ifNone(int2, ((int23 = (int23 + (1)) | 0) - (1))), 1)) {
                R.call(7229, [5, int12, int6, int20]);
                if (R.eq(R.mod(int20, 5), 4)) {
                  int12 = R.abs((int12 = (int12 + (-1)) | 0));
                }
                int20 = ((int20) + (1) | 0);
              }
            }
            if ((int20 > 0)) {
              int8 = ((35) + (Math.imul(35, R.idiv(((int20) - (1) | 0), 5))) | 0);
              int9 = R.max(int9, ((Math.imul(36, R.min(5, int20))) + (20) | 0));
            }
            int12 = ((int12) + (1) | 0);
          }
          break;
        }
        case 49538:
        {
          int21 = R.enumCount(14069);
          int22 = 0;
          int20 = (-1 | 0);
          while (((int20 = (int20 + (1)) | 0) < int21)) {
            int10 = R.enumValue(0, 33, 14069, int20);
            int12 = R.call(14695, [int10]);
            if ((int12 > 0)) {
              R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
              R.ifNoop(36, 32, 0, 0);
              R.ifNoop(int10, int12);
              int22 = ((int22) + (1) | 0);
            }
          }
          if ((int22 > 0)) {
            int20 = 0;
            int9 = R.max(int9, Math.imul(R.min(4, int22), 36));
            int8 = Math.imul(32, ((1) + (R.idiv(((int22) - (1) | 0), 4)) | 0));
            while ((int23 < int3)) {
              if (R.eq(R.ifNone(int2, ((int23 = (int23 + (1)) | 0) - (1))), 1)) {
                R.call(7229, [4, ((int22) - (Math.imul(4, R.idiv(int20, int22))) | 0), int6, int20]);
                int20 = ((int20) + (1) | 0);
              }
            }
            int12 = int22;
          }
          break;
        }
        case 51022:
        {
          int21 = R.enumCount(16107);
          int22 = 0;
          int20 = (-1 | 0);
          while (((int20 = (int20 + (1)) | 0) < int21)) {
            int10 = R.enumValue(0, 33, 16107, int20);
            int12 = R.invTotal(895, int10);
            if ((int12 > 0)) {
              R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
              R.ifNoop(36, 32, 0, 0);
              R.ifNoop(int10, int12);
              int22 = ((int22) + (1) | 0);
            }
          }
          if ((int22 > 0)) {
            int20 = 0;
            int9 = R.max(int9, ((Math.imul(R.min(6, int22), 36)) + (16) | 0));
            int8 = Math.imul(32, ((1) + (R.idiv(((int22) - (1) | 0), 6)) | 0));
            while ((int23 < int3)) {
              if (R.eq(R.ifNone(int2, ((int23 = (int23 + (1)) | 0) - (1))), 1)) {
                R.call(7229, [6, ((int22) - (Math.imul(6, R.idiv(int20, int22))) | 0), int6, int20]);
                int20 = ((int20) + (1) | 0);
              }
            }
            int12 = int22;
          }
          break;
        }
        case 52824:
        {
          int21 = R.enumCount(8487);
          int22 = 0;
          int20 = (-1 | 0);
          int12 = 1;
          while (((int20 = (int20 + (1)) | 0) < int21)) {
            int10 = R.enumValue(0, 33, 8487, int20);
            if (R.eq(R.call(16377, [int10]), 1)) {
              R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
              R.ifNoop(36, 32, 0, 0);
              R.ifNoop(int10, (-1 | 0));
              int22 = ((int22) + (1) | 0);
            }
          }
          if ((int22 > 0)) {
            int20 = 0;
            int9 = R.max(int9, ((Math.imul(R.min(6, int22), 36)) + (16) | 0));
            int8 = Math.imul(32, ((1) + (R.idiv(((int22) - (1) | 0), 6)) | 0));
            while ((int23 < int3)) {
              if (R.eq(R.ifNone(int2, ((int23 = (int23 + (1)) | 0) - (1))), 1)) {
                R.call(7229, [6, ((int22) - (Math.imul(6, R.idiv(int20, int22))) | 0), int6, int20]);
                int20 = ((int20) + (1) | 0);
              }
            }
            int12 = int22;
          }
          break;
        }
        default:
        {
          switch (R.key(R.itemCategory(int0))) {
            case 4222:
            {
              switch (R.key(int0)) {
                case 5509:
                {
                  int11 = R.vb(16502);
                  int12 = R.vb(16497);
                  break;
                }
                case 5510:
                case 5511:
                {
                  int11 = R.vb(16503);
                  int12 = R.vb(16498);
                  break;
                }
                case 5512:
                case 5513:
                {
                  int11 = R.vb(16504);
                  int12 = R.vb(16499);
                  break;
                }
                case 5514:
                case 5515:
                {
                  int11 = R.vb(16505);
                  int12 = R.vb(16500);
                  break;
                }
                case 24205:
                {
                  int11 = R.min(R.invVar(R.vc(5121), R.vc(5122), 53289), 3);
                  int12 = R.invVar(R.vc(5121), R.vc(5122), 16521);
                  break;
                }
                case 58451:
                {
                  int11 = R.vb(57686);
                  int12 = R.vb(57685);
                  break;
                }
              }
              if ((int12 > 0)) {
                switch (R.key(int11)) {
                  case 1:
                  {
                    int10 = 1436;
                    break;
                  }
                  case 0:
                  case 2:
                  {
                    int10 = 7936;
                    break;
                  }
                  case 3:
                  {
                    int10 = 55667;
                    break;
                  }
                }
                R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
                R.ifNoop(36, 32, 0, 0);
                R.ifNoop(0, int6, 1, 0);
                R.ifNoop(int10, int12);
              }
              break;
            }
            case 3542:
            {
              int12 = R.vb(27718);
              if ((int12 > 0)) {
                var int3 = R.call(13777, [int2, int3, int4, int6, 31613, int12]);
              }
              break;
            }
            case 4210:
            {
              int12 = R.invVar(R.vc(5121), R.vc(5122), 25454);
              if ((int12 > 0)) {
                int3 = R.call(13777, [int2, int3, int4, int6, 32341, int12]);
              }
              break;
            }
            case 3847:
            {
              int12 = R.invVar(R.vc(5121), R.vc(5122), 31193);
              if ((int12 > 0)) {
                int3 = R.call(13777, [int2, int3, int4, int6, 37227, int12]);
              }
              break;
            }
            case 4448:
            {
              if (R.eq(R.call(2532, []), 1)) {
                int21 = R.enumCount(17159);
                while ((int20 < int21)) {
                  int10 = R.enumValue(0, 33, 17159, int20);
                  int12 = R.call(18309, [int10]);
                  if (((int12 > 0) && R.eq(R.call(2535, [int0, int10]), 1))) {
                    R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
                    R.ifNoop(36, 32, 0, 0);
                    R.ifNoop(int10, int12);
                  }
                  int20 = ((int20) + (1) | 0);
                }
                int20 = 0;
                int12 = ((int3) - (int23) | 0);
                while ((int23 < int3)) {
                  if (R.eq(R.ifNone(int2, ((int23 = (int23 + (1)) | 0) - (1))), 1)) {
                    switch (R.key(R.mod(R.min(int12, 4), 4))) {
                      case 1:
                      {
                        R.ifNoop(0, ((int6) + (Math.imul(35, R.idiv(int20, 4))) | 0), 1, 0);
                        break;
                      }
                      case 2:
                      {
                        switch (R.key(R.mod(int20, 4))) {
                          case 0:
                          {
                            R.ifNoop(R.idiv(((0) - (36) | 0), 2), ((int6) + (Math.imul(35, R.idiv(int20, 4))) | 0), 1, 0);
                            break;
                          }
                          case 1:
                          {
                            R.ifNoop(R.idiv(36, 2), ((int6) + (Math.imul(35, R.idiv(int20, 4))) | 0), 1, 0);
                            break;
                          }
                        }
                        break;
                      }
                      case 3:
                      {
                        switch (R.key(R.mod(int20, 4))) {
                          case 0:
                          {
                            R.ifNoop(((0) - (36) | 0), ((int6) + (Math.imul(35, R.idiv(int20, 4))) | 0), 1, 0);
                            break;
                          }
                          case 1:
                          {
                            R.ifNoop(0, ((int6) + (Math.imul(35, R.idiv(int20, 4))) | 0), 1, 0);
                            break;
                          }
                          case 2:
                          {
                            R.ifNoop(36, ((int6) + (Math.imul(35, R.idiv(int20, 4))) | 0), 1, 0);
                            break;
                          }
                        }
                        break;
                      }
                      case 0:
                      {
                        switch (R.key(R.mod(int20, 4))) {
                          case 0:
                          {
                            R.ifNoop(R.scale(((0) - (36) | 0), 2, 3), ((int6) + (Math.imul(35, R.idiv(int20, 4))) | 0), 1, 0);
                            break;
                          }
                          case 1:
                          {
                            R.ifNoop(R.idiv(((0) - (36) | 0), 2), ((int6) + (Math.imul(35, R.idiv(int20, 4))) | 0), 1, 0);
                            break;
                          }
                          case 2:
                          {
                            R.ifNoop(R.idiv(36, 2), ((int6) + (Math.imul(35, R.idiv(int20, 4))) | 0), 1, 0);
                            break;
                          }
                          case 3:
                          {
                            R.ifNoop(R.scale(36, 2, 3), ((int6) + (Math.imul(35, R.idiv(int20, 4))) | 0), 1, 0);
                            break;
                          }
                        }
                        break;
                      }
                    }
                    if (R.eq(R.mod(int20, 4), 3)) {
                      int12 = R.abs((int12 = (int12 + (-1)) | 0));
                    }
                    int20 = ((int20) + (1) | 0);
                  }
                }
                if ((int20 > 0)) {
                  int8 = ((35) + (Math.imul(35, R.idiv(((int20) - (1) | 0), 4))) | 0);
                  int9 = R.max(int9, ((Math.imul(36, R.min(4, int20))) + (20) | 0));
                }
                int12 = ((int12) + (1) | 0);
              }
              break;
            }
            case 3168:
            {
              int12 = R.invVar(R.vc(5121), R.vc(5122), 21711);
              if ((int12 > 0)) {
                int3 = R.call(13777, [int2, int3, int4, int6, 30915, int12]);
              }
              break;
            }
            case 4699:
            {
              int21 = R.enumCount(15971);
              int22 = 0;
              int20 = (-1 | 0);
              while (((int20 = (int20 + (1)) | 0) < int21)) {
                int10 = R.enumValue(0, 33, 15971, int20);
                if (R.eq(int0, 59637)) {
                  if (((!R.eq(R.itemCategory(int10), 4707)) && R.eq(R.itemParam(int10, 9304), 1))) {
                    int12 = 2147483647;
                  } else {
                    int12 = 0;
                  }
                } else {
                  int12 = R.invTotal(891, int10);
                }
                if ((int12 > 0)) {
                  R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
                  R.ifNoop(36, 32, 0, 0);
                  R.ifNoop(int10, int12);
                  int22 = ((int22) + (1) | 0);
                }
              }
              if ((int22 > 0)) {
                int20 = 0;
                int9 = R.max(int9, Math.imul(R.min(5, int22), 36));
                int8 = Math.imul(32, ((1) + (R.idiv(((int22) - (1) | 0), 5)) | 0));
                while ((int23 < int3)) {
                  if (R.eq(R.ifNone(int2, ((int23 = (int23 + (1)) | 0) - (1))), 1)) {
                    R.call(7229, [5, ((int22) - (Math.imul(5, R.idiv(int20, int22))) | 0), int6, int20]);
                    int20 = ((int20) + (1) | 0);
                  }
                }
                int12 = int22;
              }
              break;
            }
            case 4359:
            {
              if (R.eq(R.invVar(R.vc(5121), R.vc(5122), 48837), 1)) {
                int6 = ((int6) - (R.idiv(16, 4)) | 0);
                int8 = 16;
                R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
                R.ifNoop(16, int8, 0, 0);
                R.ifNoop(0, int6, 1, 0);
                R.ifNoop(15217);
                int12 = 1;
              }
              break;
            }
            case 5297:
            {
              [int12, int9, int8, int3] = R.call(18330, [937, int9, int8, int2, int3, int23, int6]);
              break;
            }
            case 5368:
            {
              int21 = R.enumCount(17223);
              int22 = 0;
              int20 = (-1 | 0);
              while (((int20 = (int20 + (1)) | 0) < int21)) {
                int10 = R.enumValue(0, 33, 17223, int20);
                int12 = R.invTotal(953, int10);
                if ((int12 > 0)) {
                  R.ifNoop(int2, 5, ((int3 = (int3 + (1)) | 0) - (1)));
                  R.ifNoop(36, 32, 0, 0);
                  R.ifNoop(int10, int12);
                  int22 = ((int22) + (1) | 0);
                }
              }
              if ((int22 > 0)) {
                int20 = 0;
                int9 = R.max(int9, Math.imul(R.min(5, int22), 36));
                int8 = 32;
                while ((int23 < int3)) {
                  if (R.eq(R.ifNone(int2, ((int23 = (int23 + (1)) | 0) - (1))), 1)) {
                    R.call(7229, [5, ((int22) - (Math.imul(5, R.idiv(int20, int22))) | 0), int6, int20]);
                    int20 = ((int20) + (1) | 0);
                  }
                }
                int12 = int22;
              }
              break;
            }
            case 5631:
            {
              [int12, int9, int8, int3] = R.call(18330, [974, int9, int8, int2, int3, int23, int6]);
              break;
            }
          }
          break;
        }
      }
    }
    var int24 = 0;
    if (R.eq(int2, 96797561)) {
      int24 = ((10) + (R.idiv(16, 4)) | 0);
    }
    if ((int12 > 0)) {
      R.ifNoop(int9, ((((int6) + (int8) | 0)) + (int24) | 0), 0, 0, int1);
    }
    return int3;
  };
  V[13777] = { vb: [], vp: [], vc: [], calls: [] };
  S[13777] = function (int0, int1, int2, int3, int4, int5) {
    if (((int1 > 0) && R.eq(R.ifNone(int0, ((int1) - (1) | 0)), 1))) {
      var int3 = ((R.ifZero()) + (R.ifZero()) | 0);
    }
    R.ifNoop(int0, 5, ((int1 = (int1 + (1)) | 0) - (1)));
    R.ifNoop(36, 32, 0, 0);
    R.ifNoop(((int2) - (9) | 0), int3, 1, 0);
    R.ifNoop(int4, int5);
    return int1;
  };
  V[13789] = { vb: [], vp: [], vc: [], calls: [15973] };
  S[13789] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- On <col=3366FF>Magic attack</col> gain a stack of <sprite=14770><nbsp><col=ffffff>${R.s(R.structParam(44066, 2794))}</col> for <col=ffffff>${R.s(R.call(15973, [33, 1]))}.`;
    return string0;
  };
  V[13790] = { vb: [], vp: [], vc: [], calls: [15973, 17709, 17720, 18561] };
  S[13790] = function (int0, string0) {
    var string0 = `${R.s(string0)}<br>- <sprite=14240><nbsp><col=ffffff>${R.s(R.structParam(14735, 2794))}</col> costs <col=ffffff>${R.s(R.str(R.idiv(120, 10), 10))}%</col> less <sprite=14907><nbsp><col=DEAC18>Adrenaline</col> per stack.`;
    string0 = `${R.s(string0)}<br>- At <col=ffffff>${R.s(R.str(5, 10))}</col> stacks: Your <col=3366FF>Magic attacks</col> trigger an area of effect attack dealing ${R.s(R.call(17720, [44066, 10, ((10) + (40) | 0), 0, 0]))} to the target and up to ${R.s(R.call(18561, [8]))} within ${R.s(R.call(17709, [2]))} of the target.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.call(15973, [20, 1]))}</col> cooldown.`;
    return string0;
  };
  V[13825] = { vb: [], vp: [], vc: [], calls: [] };
  S[13825] = function (int0, int1) {
    var int2 = 0;
    switch (R.key(int1)) {
      case 3:
      case 10:
      {
        int2 = R.itemParam(int0, 965);
        break;
      }
      case 2:
      case 4:
      {
        int2 = R.itemParam(int0, 643);
        break;
      }
      case 1:
      {
        int2 = R.itemParam(int0, 641);
        break;
      }
      case 11:
      {
        int2 = R.itemParam(int0, 8881);
        break;
      }
      default:
      {
        return 0;
      }
    }
    if (R.eq(R.itemWearpos(int0), 5)) {
      return R.idiv(int2, 2);
    }
    return int2;
  };
  V[14075] = { vb: [15711, 15712, 15713, 15714, 15715, 15716, 15717, 15718, 15719, 15720], vp: [], vc: [], calls: [] };
  S[14075] = function () {
    return ((((((((((((((((((R.vb(15711)) + (R.vb(15712)) | 0)) + (R.vb(15713)) | 0)) + (R.vb(15714)) | 0)) + (R.vb(15715)) | 0)) + (R.vb(15716)) | 0)) + (R.vb(15717)) | 0)) + (R.vb(15718)) | 0)) + (R.vb(15719)) | 0)) + (R.vb(15720)) | 0);
  };
  V[14076] = { vb: [15721, 15722, 15723, 15724, 15725, 15726, 15727, 15728, 15729, 15730, 15731], vp: [], vc: [], calls: [] };
  S[14076] = function () {
    return ((((((((((((((((((((R.vb(15721)) + (R.vb(15722)) | 0)) + (R.vb(15723)) | 0)) + (R.vb(15724)) | 0)) + (R.vb(15725)) | 0)) + (R.vb(15726)) | 0)) + (R.vb(15727)) | 0)) + (R.vb(15728)) | 0)) + (R.vb(15729)) | 0)) + (R.vb(15730)) | 0)) + (R.vb(15731)) | 0);
  };
  V[14077] = { vb: [15732, 15733, 15734, 15735, 15736, 15737, 15738, 15739, 15740], vp: [], vc: [], calls: [] };
  S[14077] = function () {
    return ((((((((((((((((R.vb(15732)) + (R.vb(15733)) | 0)) + (R.vb(15734)) | 0)) + (R.vb(15735)) | 0)) + (R.vb(15736)) | 0)) + (R.vb(15737)) | 0)) + (R.vb(15738)) | 0)) + (R.vb(15739)) | 0)) + (R.vb(15740)) | 0);
  };
  V[14078] = { vb: [15751, 15752, 15753, 15754, 15755, 15756, 15757, 15758, 15759], vp: [], vc: [], calls: [] };
  S[14078] = function () {
    return ((((((((((((((((R.vb(15751)) + (R.vb(15752)) | 0)) + (R.vb(15753)) | 0)) + (R.vb(15754)) | 0)) + (R.vb(15755)) | 0)) + (R.vb(15756)) | 0)) + (R.vb(15757)) | 0)) + (R.vb(15758)) | 0)) + (R.vb(15759)) | 0);
  };
  V[14079] = { vb: [], vp: [], vc: [], calls: [14075] };
  S[14079] = function () {
    if ((R.call(14075, []) >= 10)) {
      return 1;
    }
    return 0;
  };
  V[14080] = { vb: [], vp: [], vc: [], calls: [14076] };
  S[14080] = function () {
    if ((R.call(14076, []) >= 11)) {
      return 1;
    }
    return 0;
  };
  V[14081] = { vb: [], vp: [], vc: [], calls: [14077] };
  S[14081] = function () {
    if ((R.call(14077, []) >= 9)) {
      return 1;
    }
    return 0;
  };
  V[14082] = { vb: [], vp: [], vc: [], calls: [14078] };
  S[14082] = function () {
    if ((R.call(14078, []) >= 9)) {
      return 1;
    }
    return 0;
  };
  V[14090] = { vb: [3008, 3009, 4935, 18521, 18522, 27430, 28225, 43690, 45999, 46004], vp: [], vc: [], calls: [] };
  S[14090] = function (int0) {
    switch (R.key(int0)) {
      case 1265:
      {
        return R.enumValue(0, 33, 2433, R.vb(18521));
      }
      case 1351:
      {
        return R.enumValue(0, 33, 6397, R.vb(18522));
      }
      case 49539:
      {
        return R.enumValue(0, 33, 12936, R.max(0, ((R.vb(45999)) - (1) | 0)));
      }
      case 50120:
      {
        return R.enumValue(0, 33, 12937, R.max(0, ((R.vb(46004)) - (1) | 0)));
      }
      case 975:
      {
        switch (R.key(R.vb(4935))) {
          case 0:
          {
            return 975;
          }
          case 1:
          {
            return 6313;
          }
          case 2:
          {
            return 6315;
          }
          case 3:
          {
            return 6317;
          }
        }
        break;
      }
      case 5329:
      {
        if (R.eq(R.vb(27430), 1)) {
          return 7409;
        }
        return 5329;
      }
      case 34963:
      {
        if (R.eq(R.vb(28225), 2)) {
          return 34964;
        }
        return 34963;
      }
      case 16295:
      {
        switch (R.key(R.vb(3008))) {
          case 1:
          {
            return 16295;
          }
          case 2:
          {
            return 16297;
          }
          case 3:
          {
            return 16299;
          }
          case 4:
          {
            return 16301;
          }
          case 5:
          {
            return 16303;
          }
          case 6:
          {
            return 16305;
          }
          case 7:
          {
            return 16307;
          }
          case 8:
          {
            return 16309;
          }
          case 9:
          {
            return 16311;
          }
          case 10:
          {
            return 16313;
          }
          case 11:
          {
            return 16315;
          }
        }
        break;
      }
      case 16361:
      {
        switch (R.key(R.vb(3009))) {
          case 1:
          {
            return 16361;
          }
          case 2:
          {
            return 16363;
          }
          case 3:
          {
            return 16365;
          }
          case 4:
          {
            return 16367;
          }
          case 5:
          {
            return 16369;
          }
          case 6:
          {
            return 16371;
          }
          case 7:
          {
            return 16373;
          }
          case 8:
          {
            return 16375;
          }
          case 9:
          {
            return 16377;
          }
          case 10:
          {
            return 16379;
          }
          case 11:
          {
            return 16381;
          }
        }
        break;
      }
      case 47718:
      {
        switch (R.key(R.vb(43690))) {
          case 0:
          {
            return 47718;
          }
        }
        break;
      }
    }
    return int0;
  };
  V[14490] = { vb: [], vp: [], vc: [], calls: [] };
  S[14490] = function (int0, int1) {
    if (R.eq(int0, (-1 | 0))) {
      return 0;
    }
    var int2 = 0;
    switch (R.key(int1)) {
      case 1:
      {
        int2 = R.itemParam(int0, 2665);
        break;
      }
      case 2:
      {
        int2 = R.itemParam(int0, 2666);
        break;
      }
      case 3:
      {
        int2 = R.itemParam(int0, 2667);
        break;
      }
      case 4:
      {
        int2 = R.itemParam(int0, 2668);
        break;
      }
      case 5:
      {
        int2 = R.itemParam(int0, 2669);
        break;
      }
      case 6:
      {
        int2 = R.itemParam(int0, 2670);
        break;
      }
      case 7:
      {
        int2 = R.itemParam(int0, 2671);
        break;
      }
      case 8:
      {
        int2 = R.itemParam(int0, 2672);
        break;
      }
      case 9:
      {
        int2 = R.itemParam(int0, 2673);
        break;
      }
      case 10:
      {
        int2 = R.itemParam(int0, 2674);
        break;
      }
    }
    return int2;
  };
  V[14579] = { vb: [47023], vp: [], vc: [], calls: [] };
  S[14579] = function (int0) {
    var int1 = 0;
    switch (R.key(R.vb(47023))) {
      case 1:
      {
        if (R.eq(R.playerMember(), 1)) {
          int1 = 2;
        }
        break;
      }
      case 2:
      {
        if (R.eq(R.playerMember(), 1)) {
          int1 = 4;
        }
        break;
      }
      case 3:
      {
        if (R.eq(R.playerMember(), 1)) {
          int1 = 6;
        }
        break;
      }
      case 4:
      {
        if (R.eq(R.playerMember(), 1)) {
          int1 = 8;
        }
        break;
      }
    }
    return ((R.dbField(int0, 389120, 0)) + (int1) | 0);
  };
  V[14581] = { vb: [55527], vp: [], vc: [], calls: [11927] };
  S[14581] = function () {
    var int0 = 2;
    var int1 = R.statBase(27);
    if ((int1 >= 101)) {
      int0 = 5;
    } else {
      if ((int1 >= 82)) {
        int0 = 4;
      } else {
        if ((int1 >= 43)) {
          int0 = 3;
        }
      }
    }
    if (R.eq(R.itemCategory(R.invObj(94, 17)), 4699)) {
      int0 = ((int0) + (1) | 0);
    }
    if (R.eq(R.vb(55527), 1)) {
      int0 = ((int0) + (1) | 0);
    }
    int0 = Math.imul(int0, R.call(11927, [27]));
    return int0;
  };
  V[14603] = { vb: [], vp: [9312, 11743], vc: [], calls: [20966] };
  S[14603] = function (int0) {
    if (R.eq(R.call(20966, []), 1)) {
      return 1;
    }
    if ((int0 <= 0)) {
      return 0;
    }
    switch (R.key(R.idiv(int0, 32))) {
      case 0:
      {
        return R.testbit(R.vp(9312), R.mod(int0, 32));
      }
      case 1:
      {
        return R.testbit(R.vp(11743), R.mod(int0, 32));
      }
    }
    return 0;
  };
  V[14606] = { vb: [], vp: [], vc: [], calls: [14603, 19769, 19771] };
  S[14606] = function (int0, int1) {
    if (R.eq(R.call(14603, [int1]), 0)) {
      return 0;
    }
    return R.call(19771, [int0, R.call(19769, [int0, int1])]);
  };
  V[14608] = { vb: [57205], vp: [], vc: [], calls: [14606, 14845] };
  S[14608] = function (int0) {
    if (R.eq(int0, (-1 | 0))) {
      return 0;
    }
    if ((R.eq(R.mapMembers(), 0) && R.eq(R.dbField(int0, 385072, 0), 1))) {
      return 0;
    }
    if (R.eq(R.call(14845, [int0]), 1)) {
      return 0;
    }
    if (R.eq(R.call(14606, [R.vb(57205), R.dbField(int0, 385024, 0)]), 2)) {
      return 1;
    }
    return 0;
  };
  V[14614] = { vb: [57207, 57208, 57209, 57211, 57212, 57213, 57215, 57216, 57217, 57219, 57220, 57221], vp: [], vc: [], calls: [] };
  S[14614] = function (int0) {
    switch (R.key(int0)) {
      case 1:
      {
        return R.vb(57207);
      }
      case 2:
      {
        return R.vb(57208);
      }
      case 3:
      {
        return R.vb(57209);
      }
      case 4:
      {
        return R.vb(57211);
      }
      case 5:
      {
        return R.vb(57212);
      }
      case 6:
      {
        return R.vb(57213);
      }
      case 7:
      {
        return R.vb(57215);
      }
      case 8:
      {
        return R.vb(57216);
      }
      case 9:
      {
        return R.vb(57217);
      }
      case 10:
      {
        return R.vb(57219);
      }
      case 11:
      {
        return R.vb(57220);
      }
      case 12:
      {
        return R.vb(57221);
      }
    }
    return (-1 | 0);
  };
  V[14685] = { vb: [], vp: [], vc: [], calls: [] };
  S[14685] = function (int0) {
    R.dbFind(364544, int0, 0);
    return R.dbNext();
  };
  V[14695] = { vb: [], vp: [9370, 9371, 9372, 9373, 9374, 9578, 12882], vc: [], calls: [] };
  S[14695] = function (int0) {
    switch (R.key(int0)) {
      case 49517:
      {
        return R.vp(9370);
      }
      case 49521:
      {
        return R.vp(9372);
      }
      case 49519:
      {
        return R.vp(9371);
      }
      case 49523:
      {
        return R.vp(9373);
      }
      case 49525:
      {
        return R.vp(9374);
      }
      case 50696:
      {
        return R.vp(9578);
      }
      case 61709:
      {
        return R.vp(12882);
      }
    }
    return 0;
  };
  V[14722] = { vb: [], vp: [], vc: [], calls: [] };
  S[14722] = function (string0) {
    var string0 = `${R.s(string0)}- Counts as <col=ffffff>2</col> pieces of equipment for the '<col=ffffff>Robes of the First Necromancer</col>' set effect.`;
    return string0;
  };
  V[14723] = { vb: [], vp: [], vc: [], calls: [2915, 8942] };
  S[14723] = function (string0) {
    var int0 = R.call(8942, []);
    if (R.eq(int0, 0)) {
      var string0 = `${R.s(string0)}- Your armour spikes gain <col=ffffff>${R.s(R.str(65, 10))}%</col> of your melee power armour damage bonus as bonus damage.`;
      string0 = `${R.s(string0)}<br>- Increase this bonus damage by <col=ffffff>${R.s(R.str(50, 10))}%</col> of melee power armour damage bonus if your <sprite=18851><nbsp><col=ED705A>Life<nbsp>Points</col> are below <col=ffffff>${R.s(R.strLoc(R.scale(60, 100, R.call(2915, [])), 1))}</col> (<col=ffffff>60%</col> <sprite=18851><nbsp><col=ED705A>Maximum<nbsp>Life<nbsp>Points</col>).`;
    } else {
      string0 = `${R.s(string0)}- Your armour spikes gain <col=ffffff>${R.s(R.str(R.scale(int0, 100, 65), 10))}</col> (<col=ffffff>${R.s(R.str(65, 10))}%</col> of melee power armour damage bonus) as bonus damage.`;
      string0 = `${R.s(string0)}<br>- Increase this bonus damage by <col=ffffff>${R.s(R.str(R.scale(int0, 100, 50), 10))}</col> (<col=ffffff>${R.s(R.str(50, 10))}%</col> of melee power armour damage bonus) if your <sprite=18851><nbsp><col=ED705A>Life<nbsp>Points</col> are below <col=ffffff>${R.s(R.strLoc(R.scale(60, 100, R.call(2915, [])), 1))}</col> (<col=ffffff>60%</col> <sprite=18851><nbsp><col=ED705A>Maximum<nbsp>Life<nbsp>Points</col>).`;
    }
    return string0;
  };
  V[14791] = { vb: [], vp: [9307], vc: [], calls: [14581] };
  S[14791] = function () {
    var string0 = "Sprite Focus - A 10% increase to Archaeology XP";
    var string1 = `Bonus (${R.s(R.str(40, 10))}%) : A further 10% increase to Archaeology XP and a 10% increase to your mattock's precision`;
    var string2 = `Bonus (${R.s(R.str(100, 10))}%) : Guaranteed material and ${R.s(R.str(R.call(14581, []), 10))}x mattock precision for a single action. Your focus is then reduced to ${R.s(R.str(60, 10))}%`;
    if ((R.vp(9307) < 40)) {
      string0 = `${R.s(string0)}<br><br><col=969696>${R.s(string1)}</col><br><col=969696>${R.s(string2)}</col>`;
    } else {
      string0 = `${R.s(string0)}<br><br>${R.s(string1)}<br><col=969696>${R.s(string2)}</col>`;
    }
    return string0;
  };
  V[14793] = { vb: [], vp: [], vc: [], calls: [7235, 7960, 14090, 14579] };
  S[14793] = function (int0, int1, int2, int3, int4, int5) {
    var int6 = 0;
    int6 = R.call(7960, [int0, 27]);
    var int7 = R.itemParam(int0, 6663);
    var int8 = 0;
    var int9 = 0;
    if ((!R.eq(int7, (-1 | 0)))) {
      int8 = R.call(14579, [int7]);
      int9 = R.dbField(int7, 389136, 0);
    }
    var int10 = R.call(14090, [49539]);
    var int11 = R.invObj(94, 3);
    if ((((!R.eq(int11, (-1 | 0))) && (!R.eq(int10, (-1 | 0)))) && (R.call(7960, [int11, 27]) > R.call(7960, [int10, 27])))) {
      int10 = int11;
    }
    var int12 = R.call(7960, [int10, 27]);
    var int13 = (-1 | 0);
    var int14 = 0;
    var int15 = 0;
    if ((!R.eq(int10, (-1 | 0)))) {
      int13 = R.itemParam(int10, 6663);
      if ((!R.eq(int13, (-1 | 0)))) {
        int14 = R.call(14579, [int13]);
        int15 = R.dbField(R.itemParam(int10, 6663), 389136, 0);
      }
    }
    var string0 = "";
    if (R.eq(int10, (-1 | 0))) {
      string0 = "<col=FF00>";
    } else {
      if ((int6 > int12)) {
        string0 = "<col=FF00>";
      } else {
        if ((int6 < int12)) {
          string0 = "<col=FF0000>";
        } else {
          string0 = R.colTag(int1);
        }
      }
    }
    if (R.eq(int0, 59630)) {
    } else {
      var int5 = R.call(7235, [int2, int3, int4, int5, `Level : ${R.s(string0)}${R.s(R.str(int6, 10))}</col>`, ""]);
    }
    var string1 = "";
    if ((!R.eq(int7, (-1 | 0)))) {
      if (R.eq(int10, (-1 | 0))) {
        string0 = "<col=FF00>";
      } else {
        if ((int8 > int14)) {
          string0 = "<col=FF00>";
        } else {
          if ((int8 < int14)) {
            string0 = "<col=FF0000>";
          } else {
            string0 = R.colTag(int1);
          }
        }
      }
      int5 = R.call(7235, [int2, int3, int4, int5, `Precision : ${R.s(string0)}${R.s(R.str(int8, 10))}</col>`, ""]);
      if (R.eq(int10, (-1 | 0))) {
        string0 = "<col=FF00>";
      } else {
        if ((int9 > int15)) {
          string0 = "<col=FF00>";
        } else {
          if ((int9 < int15)) {
            string0 = "<col=FF0000>";
          } else {
            string0 = R.colTag(int1);
          }
        }
      }
      int5 = R.call(7235, [int2, int3, int4, int5, `Focus: ${R.s(string0)}${R.s(R.str(int9, 10))}</col>`, ""]);
    }
    return int5;
  };
  V[14845] = { vb: [], vp: [], vc: [], calls: [] };
  S[14845] = function (int0) {
    return 0;
  };
  V[14945] = { vb: [], vp: [], vc: [], calls: [12422, 16472] };
  S[14945] = function (int0, int1) {
    var int2 = R.call(12422, [6, 10, int0]);
    var int3 = R.call(16472, [int0]);
    if ((R.eq(int1, 1) && (int3 > 0))) {
      return `${R.s(R.str(int2, 10))}.${R.s(R.str(int3, 10))}s`;
    }
    return `${R.s(R.str(int2, 10))}s`;
  };
  V[14971] = { vb: [47447], vp: [9563], vc: [], calls: [2759, 20967] };
  S[14971] = function () {
    if (R.eq(R.call(20967, []), 1)) {
      return 3;
    }
    var int0 = 0;
    if (R.eq(R.call(2759, []), 1)) {
      int0 = ((int0) + (((3) - (R.vb(47447)) | 0)) | 0);
    }
    int0 = ((int0) + (R.vp(9563)) | 0);
    return int0;
  };
  V[14973] = { vb: [], vp: [], vc: [], calls: [] };
  S[14973] = function (int0) {
    if (R.eq(int0, (-1 | 0))) {
      return "";
    }
    var int1 = R.dbField(int0, 364640, 0);
    if ((!R.eq(int1, (-1 | 0)))) {
      return R.itemName(int1);
    }
    return "";
  };
  V[14975] = { vb: [47450], vp: [], vc: [], calls: [14685, 14971, 14973, 20967] };
  S[14975] = function (string0) {
    if (R.eq(R.call(20967, []), 1)) {
      var string0 = `${R.s(string0)}<br><br>Energy available: <col=ffffff>Unlimited</col>`;
    } else {
      string0 = `${R.s(string0)}<br><br>Energy available: <col=ffffff>${R.s(R.strLoc(R.call(14971, []), 1))}</col>`;
    }
    if ((R.vb(47450) > 0)) {
      string0 = `${R.s(string0)}<br>Current fixation: <col=ffffff>${R.s(R.call(14973, [R.call(14685, [R.vb(47450)])]))}</col>`;
    }
    return string0;
  };
  V[15077] = { vb: [], vp: [], vc: [], calls: [7460] };
  S[15077] = function (string0) {
    var int0 = R.invObj(94, 3);
    var int1 = 0;
    if (R.eq(R.itemParam(int0, 2825), 1)) {
      int1 = R.call(7460, [int0]);
    }
    switch (R.key(int1)) {
      case 2:
      {
        var string0 = `${R.s(string0)}<br>- <col=ffffff>2h <sprite=14903>:</col> Swipe your weapon in front of you.`;
        string0 = `${R.s(string0)}<br><col=969696>- 2x <sprite=14904>: Strike at the target with both weapons.</col>`;
        break;
      }
      case 1:
      {
        string0 = `${R.s(string0)}<br><col=969696>- 2h <sprite=14903>: Swipe your weapon in front of you.</col>`;
        string0 = `${R.s(string0)}<br><col=ffffff>- 2x <sprite=14904>: Strike at the target with both weapons.</col>`;
        break;
      }
      default:
      {
        string0 = `${R.s(string0)}<br><col=969696>- 2h <sprite=14903>: Attack all targets in front of you.</col>`;
        string0 = `${R.s(string0)}<br><col=969696>- 2x <sprite=14904>: Strike at the target with both weapons.</col>`;
        break;
      }
    }
    return string0;
  };
  V[15086] = { vb: [], vp: [], vc: [], calls: [] };
  S[15086] = function (int0) {
    var string0 = "";
    var int1 = R.dbField(int0, 1515600, 0);
    var int2 = R.dbField(int0, 1515616, 0);
    var int3 = R.dbField(int0, 1515664, 0);
    string0 = R.cat(string0, `Category: ${R.s(R.enumValue(0, 36, 12859, int1))}`);
    if ((!R.eq(int2, (-1 | 0)))) {
      string0 = R.cat(string0, `<br>Subcategory: ${R.s(R.enumValue(0, 36, 12973, int2))}`);
    }
    if ((R.statBase(22) >= int3)) {
      string0 = R.cat(string0, `<br>Construction Level: <col=FF00>${R.s(R.str(int3, 10))}`);
    } else {
      string0 = R.cat(string0, `<br>Construction Level: <col=FF0000>${R.s(R.str(int3, 10))}`);
    }
    return string0;
  };
  V[15097] = { vb: [], vp: [], vc: [], calls: [17461] };
  S[15097] = function (int0, int1) {
    var int2 = 0;
    var int3 = (-1 | 0);
    if ((R.eq(R.itemCategory(R.invObj(int0, int1)), 4700) && R.eq(R.itemHasVarobj(R.invObj(int0, int1)), 1))) {
      int2 = R.invVar(int0, int1, 47702);
      if ((int2 > 0)) {
        int3 = R.enumValue(0, 33, 15970, int2);
        if ((!R.eq(R.itemParam(int3, 4338), (-1 | 0)))) {
          int3 = R.itemParam(int3, 4338);
        }
        return `${R.s(R.call(17461, [int3]))} (${R.s(R.itemName(int3))})`;
      }
    }
    return "None";
  };
  V[15408] = { vb: [], vp: [], vc: [], calls: [] };
  S[15408] = function (int0) {
    return R.idiv(((0) - (R.scale(int0, 1000, 1000)) | 0), 4);
  };
  V[15409] = { vb: [], vp: [], vc: [], calls: [] };
  S[15409] = function (int0) {
    return R.scale(int0, 1000, 1000);
  };
  V[15411] = { vb: [6, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 3901, 4291, 11610, 12772, 15884, 15885, 16374, 18523, 18524, 18525, 18526, 18527, 18528, 18529, 21067, 21068, 21069, 22430, 22464, 24967, 28166, 28742, 28743, 28747, 28754, 32628, 34061, 34065, 34066, 34067, 34068, 34069, 34070, 34071, 34072, 34073, 35826, 36173, 36971, 39926, 44136, 44270, 44469, 45680, 48719, 48720, 48721, 49489, 49490, 49491, 49492, 49568, 49799, 50371, 51565, 51566, 51567, 52518, 52857, 53270, 53546, 53571, 53572, 53573, 53574, 53575, 53576, 53577, 53578, 53579, 53580, 53581, 53582, 53583, 53584, 53585, 53586, 53587, 54631, 54732, 55973, 55974, 60739], vp: [1397], vc: [], calls: [42, 2829, 5566, 6667, 12043, 12052, 18522, 20174, 20176, 20987] };
  S[15411] = function (int0) {
    if (((R.eq(R.structParam(int0, 2806), 29) && (!R.eq(R.structParam(int0, 5542), 11))) && (R.vb(53546) < 30))) {
      return 0;
    }
    switch (R.key(int0)) {
      case 19254:
      case 19251:
      {
        if (R.eq(R.call(20987, []), 0)) {
          return R.call(42, [R.questFinished(361)]);
        }
        return 0;
      }
      case 46276:
      {
        if (R.eq(R.call(20987, []), 0)) {
          if (R.eq(R.vb(51566), 1)) {
            return R.call(42, [R.questFinished(361)]);
          }
          return 0;
        }
        return 0;
      }
      case 46275:
      {
        if (R.eq(R.call(20987, []), 0)) {
          if (R.eq(R.vb(51567), 1)) {
            return R.call(42, [R.questFinished(361)]);
          }
          return 0;
        }
        return 0;
      }
      case 45048:
      {
        return R.vb(48719);
      }
      case 45046:
      {
        return R.vb(48720);
      }
      case 45045:
      {
        return R.vb(48721);
      }
      case 44900:
      {
        return R.vb(49568);
      }
      case 45450:
      {
        return R.vb(49799);
      }
      case 47221:
      {
        return R.vb(52857);
      }
      case 45800:
      case 28927:
      {
        return R.vb(50371);
      }
      case 14707:
      {
        if (R.eq(R.call(20987, []), 0)) {
          return 1;
        }
        return 0;
      }
      case 46279:
      {
        return R.vb(51565);
      }
      case 1488:
      {
        return R.vb(35826);
      }
      case 39530:
      case 39531:
      case 39532:
      case 39533:
      {
        return R.vb(39926);
      }
      case 19255:
      {
        return R.call(42, [R.questFinished(361)]);
      }
      case 19252:
      {
        return R.call(42, [R.questFinished(361)]);
      }
      case 19253:
      {
        return R.call(42, [R.questFinished(361)]);
      }
      case 7120:
      {
        return R.vb(44136);
      }
      case 7121:
      {
        return R.call(42, [R.call(2829, [])]);
      }
      case 7122:
      {
        return R.vb(15884);
      }
      case 7123:
      {
        return R.vb(15885);
      }
      case 11659:
      {
        return R.vb(45680);
      }
      case 14778:
      {
        if ((vp2549_q_eadgar_s_ruse_progress < 110)) {
          return 0;
        }
        break;
      }
      case 28493:
      {
        if ((R.vb(4291) < 200)) {
          return 0;
        }
        break;
      }
      case 14781:
      {
        if ((R.vb(12772) < 50)) {
          return 0;
        }
        break;
      }
      case 14773:
      {
        if ((vp2397_q_watchtower_progress < 14)) {
          return 0;
        }
        break;
      }
      case 14833:
      {
        return R.vb(6);
      }
      case 14768:
      {
        if ((vp2386_q_plague_city_progress < 30)) {
          return 0;
        }
        break;
      }
      case 31921:
      {
        return R.call(42, [R.call(6667, [4])]);
      }
      case 37901:
      {
        return R.vb(36971);
      }
      case 51276:
      case 51277:
      {
        if ((R.vb(44469) < 50)) {
          return 0;
        }
        break;
      }
      case 51274:
      {
        if ((vp2265_q_legends_quest_progress < 50)) {
          return 0;
        }
        break;
      }
      case 53006:
      {
        if (R.eq(R.call(18522, [526]), 0)) {
          return 0;
        }
        break;
      }
      case 37199:
      {
        return R.vb(34061);
      }
      case 37200:
      {
        return R.vb(34072);
      }
      case 37201:
      {
        return R.vb(34071);
      }
      case 37202:
      {
        return R.vb(34073);
      }
      case 37220:
      {
        return R.vb(34065);
      }
      case 37203:
      {
        return R.vb(34066);
      }
      case 37206:
      {
        return R.vb(34067);
      }
      case 37204:
      {
        return R.vb(34068);
      }
      case 37205:
      {
        return R.vb(34069);
      }
      case 37207:
      {
        return R.vb(34070);
      }
      case 44912:
      {
        return R.vb(49489);
      }
      case 44946:
      {
        return R.vb(49490);
      }
      case 44947:
      {
        return R.vb(49491);
      }
      case 44950:
      {
        return R.vb(49492);
      }
      case 24188:
      {
        return R.vb(21067);
      }
      case 25028:
      {
        return R.vb(21068);
      }
      case 24189:
      {
        return R.vb(21069);
      }
      case 28178:
      case 28180:
      case 28177:
      case 28179:
      {
        return R.vb(22464);
      }
      case 31649:
      {
        return R.vb(28166);
      }
      case 31985:
      {
        return R.vb(28743);
      }
      case 31986:
      {
        return R.vb(28742);
      }
      case 31982:
      {
        return R.vb(28747);
      }
      case 31983:
      {
        return R.vb(28747);
      }
      case 31984:
      {
        return R.vb(28754);
      }
      case 33650:
      {
        return R.vb(32628);
      }
      case 32244:
      case 32245:
      case 32246:
      case 32247:
      case 32258:
      case 32250:
      case 32251:
      case 32252:
      case 32253:
      case 32254:
      case 32256:
      case 32257:
      case 32259:
      case 32261:
      case 6848:
      case 6847:
      {
        return R.call(42, [R.call(20176, [])]);
      }
      case 32942:
      case 32943:
      {
        if ((R.eq(R.call(12043, [26]), 0) || R.eq(R.call(12052, [1]), 0))) {
          return 0;
        }
        break;
      }
      case 4374:
      {
        return R.vb(30);
      }
      case 11753:
      {
        return R.vb(28);
      }
      case 11780:
      {
        return R.vb(29);
      }
      case 14967:
      {
        return R.vb(31);
      }
      case 24202:
      {
        return R.vb(32);
      }
      case 24225:
      {
        return R.vb(33);
      }
      case 24274:
      {
        return R.vb(34);
      }
      case 24278:
      {
        return R.vb(35);
      }
      case 24298:
      {
        return R.vb(36);
      }
      case 24299:
      {
        return R.vb(37);
      }
      case 24300:
      {
        return R.vb(38);
      }
      case 24302:
      {
        return R.vb(39);
      }
      case 24308:
      {
        return R.vb(40);
      }
      case 24309:
      {
        return R.call(42, [R.questFinished(24)]);
      }
      case 24310:
      {
        return R.call(42, [R.questFinished(135)]);
      }
      case 24311:
      {
        return R.vb(18523);
      }
      case 24312:
      {
        return R.vb(18524);
      }
      case 24313:
      {
        return R.vb(18525);
      }
      case 24314:
      {
        return R.vb(18526);
      }
      case 24315:
      {
        return R.vb(18527);
      }
      case 24316:
      {
        return R.vb(18528);
      }
      case 24317:
      {
        return R.vb(18529);
      }
      case 24320:
      {
        return R.vb(22430);
      }
      case 24321:
      {
        return R.vb(24967);
      }
      case 24323:
      {
        return R.vb(36173);
      }
      case 24324:
      {
        return R.vb(44270);
      }
      case 39862:
      {
        return R.vb(52518);
      }
      case 48292:
      {
        return R.vb(53270);
      }
      case 53005:
      {
        return R.vb(60739);
      }
      case 14845:
      case 14827:
      case 14830:
      case 14835:
      case 14842:
      case 14849:
      case 39785:
      case 14859:
      case 14873:
      case 6846:
      case 6845:
      {
        return R.call(42, [R.call(20174, [])]);
      }
      case 14836:
      {
        if ((R.vb(16374) < 1)) {
          return 0;
        }
        break;
      }
      case 14840:
      {
        if ((R.vb(16374) < 2)) {
          return 0;
        }
        break;
      }
      case 14843:
      {
        if ((R.vb(16374) < 3)) {
          return 0;
        }
        break;
      }
      case 14846:
      {
        if ((R.vb(16374) < 4)) {
          return 0;
        }
        break;
      }
      case 44699:
      {
        if ((R.vb(16374) < 5)) {
          return 0;
        }
        break;
      }
      case 14854:
      {
        if ((R.vb(16374) < 6)) {
          return 0;
        }
        break;
      }
      case 14865:
      {
        if ((R.vb(16374) < 7)) {
          return 0;
        }
        break;
      }
      case 14871:
      {
        if ((R.vb(16374) < 8)) {
          return 0;
        }
        break;
      }
      case 14867:
      {
        if (((R.vb(16374) < 9) || R.eq(R.questFinished(314), 0))) {
          return 0;
        }
        break;
      }
      case 14868:
      {
        if (((R.vb(16374) < 10) || R.eq(R.questFinished(314), 0))) {
          return 0;
        }
        break;
      }
      case 14874:
      {
        if ((R.vb(16374) < 11)) {
          return 0;
        }
        break;
      }
      case 14861:
      {
        if ((R.vb(11610) < 170)) {
          return 0;
        }
        break;
      }
      case 14790:
      {
        if ((R.vb(11610) < 400)) {
          return 0;
        }
        break;
      }
      case 14777:
      {
        if ((R.vp(1397) < 6)) {
          return 0;
        }
        break;
      }
      case 14776:
      {
        return R.vb(3901);
      }
      case 48325:
      {
        return R.call(42, [R.questFinished(494)]);
      }
      case 49390:
      {
        return R.call(42, [R.questFinished(511)]);
      }
      case 48302:
      {
        return R.vb(53571);
      }
      case 48303:
      {
        return R.vb(53572);
      }
      case 48304:
      {
        return R.vb(53573);
      }
      case 48305:
      {
        return R.vb(53574);
      }
      case 48306:
      {
        return R.vb(53575);
      }
      case 48307:
      {
        return R.vb(53576);
      }
      case 31820:
      {
        return R.vb(55973);
      }
      case 32342:
      {
        return R.vb(55974);
      }
      case 48311:
      {
        return R.vb(53577);
      }
      case 48312:
      {
        return R.vb(53578);
      }
      case 48313:
      {
        return R.vb(53579);
      }
      case 33965:
      {
        return R.vb(54631);
      }
      case 48324:
      {
        if (R.eq(R.call(20987, []), 0)) {
          return 1;
        }
        return 0;
      }
      case 48326:
      {
        return R.vb(53580);
      }
      case 48327:
      {
        return R.vb(53581);
      }
      case 48328:
      {
        return R.vb(53582);
      }
      case 48329:
      {
        return R.vb(53583);
      }
      case 48330:
      {
        return R.vb(53584);
      }
      case 48331:
      {
        return R.vb(53585);
      }
      case 48332:
      {
        return R.vb(53586);
      }
      case 49072:
      {
        return R.vb(54732);
      }
      case 48352:
      {
        return R.call(5566, [R.vb(53587), 1, 1]);
      }
      case 48353:
      {
        return R.call(5566, [R.vb(53587), 2, 1]);
      }
      case 48354:
      {
        return R.call(5566, [R.vb(53587), 3, 1]);
      }
      case 52788:
      {
        return R.call(5566, [R.statBase(0), 60, 1]);
      }
      case 52789:
      {
        return R.call(5566, [R.statBase(0), 75, 1]);
      }
    }
    return 1;
  };
  V[15709] = { vb: [], vp: [], vc: [], calls: [] };
  S[15709] = function (int0) {
    var int1 = R.ifNeg();
    switch (R.key(R.ifNeg())) {
      case 744:
      {
        return 48759153;
      }
      case 906:
      {
        return 59375786;
      }
    }
    if (R.eq(int0, 1)) {
      return 96797492;
    }
    return 96796719;
  };
  V[15721] = { vb: [22875], vp: [], vc: [], calls: [] };
  S[15721] = function () {
    var int0 = R.vb(22875);
    if (R.eq(varbitclient_43686, 1)) {
      int0 = varbitclient_22876;
    }
    return int0;
  };
  V[15732] = { vb: [], vp: [], vc: [], calls: [7460, 7495, 15734, 17720] };
  S[15732] = function (int0, int1, string0) {
    var int2 = 135;
    var int3 = 30;
    var int4 = R.invObj(94, 3);
    var int5 = 0;
    if (R.eq(R.itemParam(int4, 2825), 1)) {
      int5 = R.call(7460, [int4]);
    }
    switch (R.key(int5)) {
      default:
      {
        break;
      }
    }
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int2, ((int2) + (int3) | 0), 0, int1]))}.`;
    if (R.eq(R.call(7495, []), 1)) {
      string0 = `${R.s(string0)}<br>- ${R.s(R.call(15734, [2]))}`;
    }
    return string0;
  };
  V[15734] = { vb: [], vp: [], vc: [], calls: [4583] };
  S[15734] = function (int0) {
    return `Generates <col=ffffff>${R.s(R.str(int0, 10))}</col> <sprite=23875><nbsp><col=ffffff>${R.s(R.structParam(52791, 2794))}</col> ${R.s(R.call(4583, [int0, "stack", "stacks"]))}.`;
  };
  V[15735] = { vb: [], vp: [], vc: [], calls: [7495, 7653] };
  S[15735] = function (string0) {
    if (R.eq(R.call(7495, []), 0)) {
      return string0;
    }
    var string0 = `${R.s(string0)}<br><br><col=ffffff>${R.s(R.structParam(52791, 2794))}</col> (consumes <col=ffffff>${R.s(R.str(4, 10))}</col> <sprite=23875><nbsp><col=ffffff>${R.s(R.structParam(52791, 2794))}</col> stacks)`;
    string0 = `${R.s(string0)}<br>- Deals <col=ffffff>${R.s(R.call(7653, [100, 2, 2, 0, 1]))}%</col> increased damage for each <col=ffffff>1%</col> <sprite=18851><nbsp><col=ED705A>Life<nbsp>Points</col> the target is missing, up to a maximum of <col=ffffff>${R.s(R.call(7653, [650, 1, 1, 0, 1]))}%</col>.`;
    return string0;
  };
  V[15736] = { vb: [], vp: [], vc: [], calls: [7495, 17709, 17720, 18561] };
  S[15736] = function (int0, int1, string0) {
    if (R.eq(R.call(7495, []), 0)) {
      return string0;
    }
    var string0 = `${R.s(string0)}<br><br><col=ffffff>${R.s(R.structParam(52791, 2794))}</col> (consumes <col=ffffff>${R.s(R.str(4, 10))}</col> <sprite=23875><nbsp><col=ffffff>${R.s(R.structParam(52791, 2794))}</col> stacks)`;
    string0 = `${R.s(string0)}<br>- Third hit of ${R.s(R.call(17720, [int0, 75, ((75) + (20) | 0), 0, int1]))} to the target and up to ${R.s(R.call(18561, [9]))} within ${R.s(R.call(17709, [1]))} of you.`;
    return string0;
  };
  V[15737] = { vb: [], vp: [], vc: [], calls: [7495, 17720] };
  S[15737] = function (int0, int1, string0) {
    if (R.eq(R.call(7495, []), 0)) {
      return string0;
    }
    var string0 = `${R.s(string0)}<br><br><col=ffffff>${R.s(R.structParam(52791, 2794))}</col> (consumes <col=ffffff>${R.s(R.str(4, 10))}</col> <sprite=23875><nbsp><col=ffffff>${R.s(R.structParam(52791, 2794))}</col> stacks)`;
    string0 = `${R.s(string0)}<br>- Deals ${R.s(R.call(17720, [52790, 170, ((170) + (20) | 0), 0, int1]))} per hit.`;
    return string0;
  };
  V[15738] = { vb: [], vp: [], vc: [], calls: [17444, 17710, 17720] };
  S[15738] = function (int0, int1, string0) {
    var int2 = 90;
    var int3 = 20;
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int2, ((int2) + (int3) | 0), 0, int1]))}.`;
    string0 = `${R.s(string0)}<br>- Applies <sprite=23877><nbsp><col=ffffff>${R.s(R.structParam(52801, 2794))}</col> to self.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [R.call(17444, [int0])]))}.`;
    return string0;
  };
  V[15769] = { vb: [], vp: [], vc: [], calls: [] };
  S[15769] = function (int0) {
    if (R.eq(int0, (-1 | 0))) {
      return (-1 | 0);
    }
    R.dbFind(668144, int0, 0);
    var int1 = R.dbNext();
    return int1;
  };
  V[15908] = { vb: [], vp: [], vc: [], calls: [4583, 8243] };
  S[15908] = function (int0, int1, int2, int3, int4, int5, int6, int7, int8, int9, int10, int11, int12, int13, int14) {
    var string0 = "";
    var string1 = "";
    var string2 = "";
    var string3 = "";
    var string4 = "";
    var string5 = "";
    var string6 = "";
    var string7 = "";
    switch (R.key(int7)) {
      case 6:
      {
        string0 = " ";
        if (((!R.eq(int0, (-1 | 0))) && ((int0 > 0) || R.eq(int9, 0)))) {
          string4 = `${R.s(R.str(int0, 10))} ${R.s(R.call(4583, [int0, "day", "days"]))}`;
        }
        if (((!R.eq(int3, (-1 | 0))) && ((int3 > 0) || R.eq(int12, 0)))) {
          string5 = `${R.s(R.str(int3, 10))} ${R.s(R.call(4583, [int3, "hour", "hours"]))}`;
        }
        if (((!R.eq(int4, (-1 | 0))) && ((int4 > 0) || R.eq(int13, 0)))) {
          string6 = `${R.s(R.str(int4, 10))} ${R.s(R.call(4583, [int4, "minute", "minutes"]))}`;
        }
        if (((!R.eq(int5, (-1 | 0))) && ((int5 > 0) || R.eq(int14, 0)))) {
          string7 = R.str(int5, 10);
          if ((int6 > 0)) {
            string7 = `${R.s(string7)}.${R.s(R.str(int6, 10))} seconds`;
          } else {
            string7 = `${R.s(string7)} ${R.s(R.call(4583, [int5, "second", "seconds"]))}`;
          }
        }
        break;
      }
      case 0:
      {
        string0 = " ";
        if (((!R.eq(int0, (-1 | 0))) && ((int0 > 0) || R.eq(int9, 0)))) {
          string4 = `${R.s(R.str(int0, 10))} ${R.s(R.call(4583, [int0, "Day", "Days"]))}`;
        }
        if (((!R.eq(int3, (-1 | 0))) && ((int3 > 0) || R.eq(int12, 0)))) {
          string5 = `${R.s(R.str(int3, 10))} ${R.s(R.call(4583, [int3, "Hour", "Hours"]))}`;
        }
        if (((!R.eq(int4, (-1 | 0))) && ((int4 > 0) || R.eq(int13, 0)))) {
          string6 = `${R.s(R.str(int4, 10))} ${R.s(R.call(4583, [int4, "Minute", "Minutes"]))}`;
        }
        if (((!R.eq(int5, (-1 | 0))) && ((int5 > 0) || R.eq(int14, 0)))) {
          string7 = R.str(int5, 10);
          if ((int6 > 0)) {
            string7 = `${R.s(string7)}.${R.s(R.str(int6, 10))} Seconds`;
          } else {
            string7 = `${R.s(string7)} ${R.s(R.call(4583, [int5, "Second", "Seconds"]))}`;
          }
        }
        break;
      }
      case 1:
      {
        string0 = " ";
        if (((!R.eq(int0, (-1 | 0))) && ((int0 > 0) || R.eq(int9, 0)))) {
          string4 = `${R.s(R.str(int0, 10))}d`;
        }
        if (((!R.eq(int3, (-1 | 0))) && ((int3 > 0) || R.eq(int12, 0)))) {
          string5 = `${R.s(R.str(int3, 10))}h`;
        }
        if (((!R.eq(int4, (-1 | 0))) && ((int4 > 0) || R.eq(int13, 0)))) {
          string6 = `${R.s(R.str(int4, 10))}m`;
        }
        if (((!R.eq(int5, (-1 | 0))) && ((int5 > 0) || R.eq(int14, 0)))) {
          string7 = R.str(int5, 10);
          if ((int6 > 0)) {
            string7 = `${R.s(string7)}.${R.s(R.str(int6, 10))}`;
          }
          string7 = `${R.s(string7)}s`;
        }
        break;
      }
      case 2:
      {
        string0 = " ";
        if (((!R.eq(int0, (-1 | 0))) && ((int0 > 0) || R.eq(int9, 0)))) {
          string4 = `${R.s(R.str(int0, 10))}D`;
        }
        if (((!R.eq(int3, (-1 | 0))) && ((int3 > 0) || R.eq(int12, 0)))) {
          string5 = `${R.s(R.str(int3, 10))}H`;
        }
        if (((!R.eq(int4, (-1 | 0))) && ((int4 > 0) || R.eq(int13, 0)))) {
          string6 = `${R.s(R.str(int4, 10))}M`;
        }
        if (((!R.eq(int5, (-1 | 0))) && ((int5 > 0) || R.eq(int14, 0)))) {
          string7 = R.str(int5, 10);
          if ((int6 > 0)) {
            string7 = `${R.s(string7)}.${R.s(R.str(int6, 10))}`;
          }
          string7 = `${R.s(string7)}S`;
        }
        break;
      }
      case 3:
      {
        string0 = " ";
        if (((!R.eq(int0, (-1 | 0))) && ((int0 > 0) || R.eq(int9, 0)))) {
          string4 = `${R.s(R.str(int0, 10))} ${R.s(R.call(4583, [int0, "Day", "Days"]))}`;
        }
        if (((!R.eq(int3, (-1 | 0))) && ((int3 > 0) || R.eq(int12, 0)))) {
          string5 = `${R.s(R.str(int3, 10))} ${R.s(R.call(4583, [int3, "Hour", "Hours"]))}`;
        }
        if (((!R.eq(int4, (-1 | 0))) && ((int4 > 0) || R.eq(int13, 0)))) {
          string6 = `${R.s(R.str(int4, 10))} ${R.s(R.call(4583, [int4, "Min", "Mins"]))}`;
        }
        if (((!R.eq(int5, (-1 | 0))) && ((int5 > 0) || R.eq(int14, 0)))) {
          string7 = R.str(int5, 10);
          if ((int6 > 0)) {
            string7 = `${R.s(string7)}.${R.s(R.str(int6, 10))} Secs`;
          } else {
            string7 = `${R.s(string7)} ${R.s(R.call(4583, [int5, "Sec", "Secs"]))}`;
          }
        }
        break;
      }
      case 4:
      {
        string0 = " ";
        if (((!R.eq(int0, (-1 | 0))) && ((int0 > 0) || R.eq(int9, 0)))) {
          string4 = `${R.s(R.str(int0, 10))} ${R.s(R.call(4583, [int0, "day", "days"]))}`;
        }
        if (((!R.eq(int3, (-1 | 0))) && ((int3 > 0) || R.eq(int12, 0)))) {
          string5 = `${R.s(R.str(int3, 10))} ${R.s(R.call(4583, [int3, "hour", "hours"]))}`;
        }
        if (((!R.eq(int4, (-1 | 0))) && ((int4 > 0) || R.eq(int13, 0)))) {
          string6 = `${R.s(R.str(int4, 10))} ${R.s(R.call(4583, [int4, "min", "mins"]))}`;
        }
        if (((!R.eq(int5, (-1 | 0))) && ((int5 > 0) || R.eq(int14, 0)))) {
          string7 = R.str(int5, 10);
          if ((int6 > 0)) {
            string7 = `${R.s(string7)}.${R.s(R.str(int6, 10))} secs`;
          } else {
            string7 = `${R.s(string7)} ${R.s(R.call(4583, [int5, "sec", "secs"]))}`;
          }
        }
        break;
      }
      case 5:
      {
        string0 = ":";
        if (((!R.eq(int0, (-1 | 0))) && ((int0 > 0) || R.eq(int9, 0)))) {
          if ((int0 < 10)) {
            string4 = "0";
          }
          string4 = `${R.s(string4)}${R.s(R.str(int0, 10))}`;
        }
        if (((!R.eq(int3, (-1 | 0))) && (((int3 > 0) || R.eq(int12, 0)) || (R.eq(int12, 1) && (!R.eq(R.len(string4), 0)))))) {
          if ((int3 < 10)) {
            string5 = "0";
          }
          string5 = `${R.s(string5)}${R.s(R.str(int3, 10))}`;
        }
        if (((!R.eq(int4, (-1 | 0))) && (((int4 > 0) || R.eq(int13, 0)) || (R.eq(int13, 1) && (!R.eq(R.len(string5), 0)))))) {
          if ((int4 < 10)) {
            string6 = "0";
          }
          string6 = `${R.s(string6)}${R.s(R.str(int4, 10))}`;
        }
        if (((!R.eq(int5, (-1 | 0))) && (((int5 > 0) || R.eq(int14, 0)) || (R.eq(int14, 1) && (!R.eq(R.len(string6), 0)))))) {
          if ((int5 < 10)) {
            string7 = "0";
          }
          string7 = `${R.s(string7)}${R.s(R.str(int5, 10))}`;
          if ((int6 > 0)) {
            string7 = `${R.s(string7)}.${R.s(R.str(int6, 10))}`;
          }
        }
        break;
      }
      case 7:
      {
        if ((int2 > 0)) {
          string2 = R.call(4583, [int2, `${R.s(R.str(int2, 10))} year`, `${R.s(R.str(int2, 10))} years`]);
        } else {
          if ((int1 > 0)) {
            string3 = R.call(4583, [int1, `${R.s(R.str(int1, 10))} month`, `${R.s(R.str(int1, 10))} months`]);
          } else {
            if ((int0 > 0)) {
              string4 = R.call(4583, [int0, `${R.s(R.str(int0, 10))} day`, `${R.s(R.str(int0, 10))} days`]);
            } else {
              if ((int3 > 0)) {
                string5 = R.call(4583, [int3, `${R.s(R.str(int3, 10))} hour`, `${R.s(R.str(int3, 10))} hours`]);
              } else {
                if ((int4 > 0)) {
                  string6 = R.call(4583, [int4, `${R.s(R.str(int4, 10))} minute`, `${R.s(R.str(int4, 10))} minutes`]);
                } else {
                  if ((int5 > 0)) {
                    string7 = R.call(4583, [int5, `${R.s(R.str(int5, 10))} second`, `${R.s(R.str(int5, 10))} seconds`]);
                  }
                }
              }
            }
          }
        }
        break;
      }
    }
    if ((!R.eq(int8, (-1 | 0)))) {
      if (((!R.eq(int0, (-1 | 0))) && ((int0 > 0) || R.eq(int9, 0)))) {
        string4 = R.str(int0, 10);
      }
      if (((!R.eq(int2, (-1 | 0))) && ((int2 > 0) || R.eq(int11, 0)))) {
        string2 = R.str(int2, 10);
      }
      switch (R.key(int8)) {
        case 0:
        case 2:
        {
          string1 = "-";
          break;
        }
        case 1:
        case 3:
        {
          string1 = " ";
          break;
        }
      }
      switch (R.key(int8)) {
        case 0:
        case 1:
        {
          if ((((int1 >= 0) && (int1 <= 11)) && R.eq(int10, 0))) {
            string3 = R.enumValue(0, 36, 10036, int1);
          }
          break;
        }
        case 2:
        case 3:
        {
          if ((((int1 >= 0) && (int1 <= 11)) && R.eq(int10, 0))) {
            string3 = R.enumValue(0, 36, 10037, int1);
          }
          break;
        }
      }
    }
    return R.call(8243, [string4, string3, string2, string5, string6, string7, string0, string1]);
  };
  V[15935] = { vb: [], vp: [], vc: [], calls: [7235, 15936] };
  S[15935] = function (int0, int1, int2, int3, int4, int5, int6, string0) {
    if (((!R.eq(R.invObj(int1, int2), int0)) || R.eq(R.invNum(int1, int2), 0))) {
      return int6;
    }
    if (R.eq(R.itemHasVarobj(int0), 0)) {
      return int6;
    }
    R.arrDef(0, 4);
    R.arrDef(65536, 4);
    R.arrSet(0, 0, R.invVar(int1, int2, 30215));
    R.arrSet(0, 1, R.invVar(int1, int2, 30217));
    R.arrSet(0, 2, R.invVar(int1, int2, 30219));
    R.arrSet(0, 3, R.invVar(int1, int2, 30221));
    R.arrSet(1, 0, R.invVar(int1, int2, 30216));
    R.arrSet(1, 1, R.invVar(int1, int2, 30218));
    R.arrSet(1, 2, R.invVar(int1, int2, 30220));
    R.arrSet(1, 3, R.invVar(int1, int2, 30222));
    var int7 = 0;
    var string1 = "";
    while ((int7 < 4)) {
      string1 = R.call(15936, [R.arrGet(0, int7), R.arrGet(1, int7)]);
      if ((!R.eq(R.strcmp(string1, ""), 0))) {
        var int6 = R.call(7235, [int3, int4, int5, int6, string1, string0]);
      }
      int7 = ((int7) + (1) | 0);
    }
    return int6;
  };
  V[15936] = { vb: [], vp: [], vc: [], calls: [] };
  S[15936] = function (int0, int1) {
    if (R.eq(int0, 0)) {
      return "";
    }
    R.dbFind(32768, int0, 0);
    var int2 = R.dbNext();
    if (R.eq(int2, (-1 | 0))) {
      return "";
    }
    var string0 = R.dbField(int2, 32784, 0);
    if ((R.dbFieldCount(int2, 32880) > 1)) {
      string0 = `${R.s(string0)} ${R.s(R.str(int1, 10))}`;
    }
    return string0;
  };
  V[15973] = { vb: [], vp: [], vc: [], calls: [12422, 16472] };
  S[15973] = function (int0, int1) {
    var int2 = R.call(12422, [6, 10, int0]);
    var int3 = R.call(16472, [int0]);
    var int4 = R.idiv(int2, 60);
    if ((int4 > 0)) {
      int2 = R.mod(int2, 60);
      if (((int2 > 0) || (int3 > 0))) {
        if ((R.eq(int1, 1) && (int3 > 0))) {
          return `${R.s(R.str(int4, 10))}m ${R.s(R.str(int2, 10))}.${R.s(R.str(int3, 10))}s`;
        }
        if ((int2 > 0)) {
          return `${R.s(R.str(int4, 10))}m ${R.s(R.str(int2, 10))}s`;
        }
      }
      return `${R.s(R.str(int4, 10))}m`;
    }
    if ((R.eq(int1, 1) && (int3 > 0))) {
      return `${R.s(R.str(int2, 10))}.${R.s(R.str(int3, 10))}s`;
    }
    return `${R.s(R.str(int2, 10))}s`;
  };
  V[15974] = { vb: [21558, 21559, 22946, 22947, 22948, 22949, 22950, 22951, 22952, 22953, 22954, 22955, 22956, 22957, 22958, 22959, 22960, 22961, 22962, 22963, 22964, 22965, 22966, 22967, 22968, 22969, 22970, 22971, 28643, 28644, 29857, 29858, 29859, 29860, 29861, 29862, 29863, 29864, 29865, 29866, 29867, 29868, 29869, 29870, 29871, 29872, 29873, 29874, 29875, 29876, 29877, 29878, 29879, 29880, 29881, 29882, 29883, 29884, 30840, 30841, 30842, 30843, 30844, 30845, 30846, 30847, 30848, 30849, 30850, 30851, 30852, 30853, 30855, 32624, 32625, 34869, 34870, 36178, 36179, 40566, 40567, 41049, 41050, 41051, 41052, 41053, 41054, 41055, 41056, 41057, 41058, 41059, 41060, 41450, 41451, 41452, 41453, 41454, 41455, 41456, 41457, 41458, 41459, 41460, 41461, 41546, 41547, 43415, 43416, 43417, 43418, 43419, 43420, 43421, 43422, 43423, 43424, 43425, 43426, 48687, 48688, 48689, 48690, 49295, 49296, 49727, 49728, 49729, 49730, 50125, 50126, 50332, 50333, 50334, 50335, 51440, 51441, 51442, 51443, 53283, 53284, 53285, 53286, 54674, 54675, 54676, 54677, 55730, 55731, 55732, 55733, 55734, 55735, 55736, 55737, 55738, 55739, 55740, 55741, 55987, 55988, 58187, 58188, 58189, 58190, 60742, 60743, 60744, 60745], vp: [], vc: [], calls: [13574] };
  S[15974] = function (int0, int1) {
    var int2 = R.enumCount(9029);
    var int3 = 1;
    var int4 = 1;
    var int5 = R.call(13574, []);
    if ((int5 < int1)) {
      return 0;
    }
    while (((!R.eq(int4, 0)) && (int3 <= int2))) {
      switch (R.key(int3)) {
        case 1:
        {
          if (((R.vb(22969) < int0) && R.eq(R.vb(29880), 0))) {
            int4 = 0;
          }
          break;
        }
        case 2:
        {
          if (((R.vb(22949) < int0) && R.eq(R.vb(29860), 0))) {
            int4 = 0;
          }
          break;
        }
        case 3:
        {
          if (((R.vb(22968) < int0) && R.eq(R.vb(29879), 0))) {
            int4 = 0;
          }
          break;
        }
        case 4:
        {
          if (((R.vb(22950) < int0) && R.eq(R.vb(29861), 0))) {
            int4 = 0;
          }
          break;
        }
        case 5:
        {
          if ((((((R.vb(22956)) + (R.vb(22957)) | 0) < int0) && R.eq(R.vb(29867), 0)) && R.eq(R.vb(29868), 0))) {
            int4 = 0;
          }
          break;
        }
        case 6:
        {
          if (((R.vb(22953) < int0) && R.eq(R.vb(29864), 0))) {
            int4 = 0;
          }
          break;
        }
        case 7:
        {
          if (((R.vb(22954) < int0) && R.eq(R.vb(29865), 0))) {
            int4 = 0;
          }
          break;
        }
        case 8:
        {
          if ((((((R.vb(22958)) + (R.vb(22959)) | 0) < int0) && R.eq(R.vb(29869), 0)) && R.eq(R.vb(29870), 0))) {
            int4 = 0;
          }
          break;
        }
        case 9:
        {
          if ((((((R.vb(22946)) + (R.vb(22947)) | 0) < int0) && R.eq(R.vb(29857), 0)) && R.eq(R.vb(29858), 0))) {
            int4 = 0;
          }
          break;
        }
        case 10:
        {
          if (((R.vb(22964) < int0) && R.eq(R.vb(29875), 0))) {
            int4 = 0;
          }
          break;
        }
        case 11:
        {
          if (((R.vb(22965) < int0) && R.eq(R.vb(29876), 0))) {
            int4 = 0;
          }
          break;
        }
        case 12:
        {
          if (((R.vb(22951) < int0) && R.eq(R.vb(29862), 0))) {
            int4 = 0;
          }
          break;
        }
        case 13:
        {
          if (((R.vb(22948) < int0) && R.eq(R.vb(29859), 0))) {
            int4 = 0;
          }
          break;
        }
        case 14:
        {
          if ((((((R.vb(22960)) + (R.vb(22961)) | 0) < int0) && R.eq(R.vb(29871), 0)) && R.eq(R.vb(29872), 0))) {
            int4 = 0;
          }
          break;
        }
        case 15:
        {
          if ((((((R.vb(22962)) + (R.vb(22963)) | 0) < int0) && R.eq(R.vb(29873), 0)) && R.eq(R.vb(29874), 0))) {
            int4 = 0;
          }
          break;
        }
        case 16:
        {
          if (((R.vb(22966) < int0) && R.eq(R.vb(29877), 0))) {
            int4 = 0;
          }
          break;
        }
        case 17:
        {
          if (((R.vb(22967) < int0) && R.eq(R.vb(29878), 0))) {
            int4 = 0;
          }
          break;
        }
        case 18:
        {
          if (((R.vb(22955) < int0) && R.eq(R.vb(29866), 0))) {
            int4 = 0;
          }
          break;
        }
        case 19:
        {
          if (((R.vb(22952) < int0) && R.eq(R.vb(29863), 0))) {
            int4 = 0;
          }
          break;
        }
        case 20:
        {
          if ((((((R.vb(22970)) + (R.vb(22971)) | 0) < int0) && R.eq(R.vb(29881), 0)) && R.eq(R.vb(29882), 0))) {
            int4 = 0;
          }
          break;
        }
        case 21:
        {
          if (((R.vb(28643) < int0) && R.eq(R.vb(29883), 0))) {
            int4 = 0;
          }
          break;
        }
        case 22:
        {
          if (((R.vb(28644) < int0) && R.eq(R.vb(29884), 0))) {
            int4 = 0;
          }
          break;
        }
        case 26:
        {
          if ((((((R.vb(30842)) + (R.vb(30846)) | 0) < int0) && R.eq(R.vb(30850), 0)) && R.eq(R.vb(29882), 0))) {
            int4 = 0;
          }
          break;
        }
        case 25:
        {
          if ((((((R.vb(30843)) + (R.vb(30847)) | 0) < int0) && R.eq(R.vb(30851), 0)) && R.eq(R.vb(30855), 0))) {
            int4 = 0;
          }
          break;
        }
        case 23:
        {
          if ((((((R.vb(30841)) + (R.vb(30845)) | 0) < int0) && R.eq(R.vb(30849), 0)) && R.eq(R.vb(30853), 0))) {
            int4 = 0;
          }
          break;
        }
        case 24:
        {
          if ((((((R.vb(30840)) + (R.vb(30844)) | 0) < int0) && R.eq(R.vb(30848), 0)) && R.eq(R.vb(30852), 0))) {
            int4 = 0;
          }
          break;
        }
        case 27:
        {
          if (((R.vb(32624) < int0) && R.eq(R.vb(32625), 0))) {
            int4 = 0;
          }
          break;
        }
        case 28:
        {
          if (((R.vb(34869) < int0) && R.eq(R.vb(34870), 0))) {
            int4 = 0;
          }
          break;
        }
        case 29:
        {
          if (((R.vb(36178) < int0) && R.eq(R.vb(36179), 0))) {
            int4 = 0;
          }
          break;
        }
        case 30:
        {
          if (((R.vb(40566) < int0) && R.eq(R.vb(40567), 0))) {
            int4 = 0;
          }
          break;
        }
        case 31:
        {
          if ((((((R.vb(41050)) + (R.vb(41049)) | 0) < int0) && R.eq(R.vb(41056), 0)) && R.eq(R.vb(41055), 0))) {
            int4 = 0;
          }
          break;
        }
        case 32:
        {
          if ((((((R.vb(41052)) + (R.vb(41051)) | 0) < int0) && R.eq(R.vb(41058), 0)) && R.eq(R.vb(41057), 0))) {
            int4 = 0;
          }
          break;
        }
        case 33:
        {
          if ((((((R.vb(41054)) + (R.vb(41053)) | 0) < int0) && R.eq(R.vb(41060), 0)) && R.eq(R.vb(41059), 0))) {
            int4 = 0;
          }
          break;
        }
        case 34:
        {
          if ((((((R.vb(41451)) + (R.vb(41450)) | 0) < int0) && R.eq(R.vb(41457), 0)) && R.eq(R.vb(41456), 0))) {
            int4 = 0;
          }
          break;
        }
        case 35:
        {
          if ((((((R.vb(41453)) + (R.vb(41452)) | 0) < int0) && R.eq(R.vb(41459), 0)) && R.eq(R.vb(41458), 0))) {
            int4 = 0;
          }
          break;
        }
        case 36:
        {
          if ((((((R.vb(41455)) + (R.vb(41454)) | 0) < int0) && R.eq(R.vb(41461), 0)) && R.eq(R.vb(41460), 0))) {
            int4 = 0;
          }
          break;
        }
        case 37:
        {
          if ((((((R.vb(43416)) + (R.vb(43415)) | 0) < int0) && R.eq(R.vb(43422), 0)) && R.eq(R.vb(43421), 0))) {
            int4 = 0;
          }
          break;
        }
        case 38:
        {
          if ((((((R.vb(43418)) + (R.vb(43417)) | 0) < int0) && R.eq(R.vb(43424), 0)) && R.eq(R.vb(43423), 0))) {
            int4 = 0;
          }
          break;
        }
        case 39:
        {
          if ((((((R.vb(43420)) + (R.vb(43419)) | 0) < int0) && R.eq(R.vb(43426), 0)) && R.eq(R.vb(43425), 0))) {
            int4 = 0;
          }
          break;
        }
        case 40:
        {
          if ((((((R.vb(48687)) + (R.vb(48688)) | 0) < int0) && R.eq(R.vb(48689), 0)) && R.eq(R.vb(48690), 0))) {
            int4 = 0;
          }
          break;
        }
        case 41:
        {
          if (((R.vb(49295) < int0) && R.eq(R.vb(49296), 0))) {
            int4 = 0;
          }
          break;
        }
        case 42:
        {
          if ((((((R.vb(49727)) + (R.vb(49728)) | 0) < int0) && R.eq(R.vb(49729), 0)) && R.eq(R.vb(49730), 0))) {
            int4 = 0;
          }
          break;
        }
        case 43:
        {
          if ((((((R.vb(41546)) + (R.vb(41547)) | 0) < int0) && R.eq(R.vb(50125), 0)) && R.eq(R.vb(50126), 0))) {
            int4 = 0;
          }
          break;
        }
        case 44:
        {
          if (((R.vb(21558) < int0) && R.eq(R.vb(21559), 0))) {
            int4 = 0;
          }
          break;
        }
        case 45:
        {
          if ((((((R.vb(50332)) + (R.vb(50333)) | 0) < int0) && R.eq(R.vb(50334), 0)) && R.eq(R.vb(50335), 0))) {
            int4 = 0;
          }
          break;
        }
        case 46:
        {
          if ((((((R.vb(51440)) + (R.vb(51441)) | 0) < int0) && R.eq(R.vb(51442), 0)) && R.eq(R.vb(51443), 0))) {
            int4 = 0;
          }
          break;
        }
        case 47:
        {
          if (((R.vb(53283) < int0) && R.eq(R.vb(53285), 0))) {
            int4 = 0;
          }
          break;
        }
        case 48:
        {
          if (((R.vb(53284) < int0) && R.eq(R.vb(53286), 0))) {
            int4 = 0;
          }
          break;
        }
        case 49:
        {
          if ((((((R.vb(54674)) + (R.vb(54675)) | 0) < int0) && R.eq(R.vb(54676), 0)) && R.eq(R.vb(54677), 0))) {
            int4 = 0;
          }
          break;
        }
        case 50:
        {
          if ((((((R.vb(55730)) + (R.vb(55731)) | 0) < int0) && R.eq(R.vb(55736), 0)) && R.eq(R.vb(55737), 0))) {
            int4 = 0;
          }
          break;
        }
        case 51:
        {
          if ((((((R.vb(55732)) + (R.vb(55733)) | 0) < int0) && R.eq(R.vb(55738), 0)) && R.eq(R.vb(55739), 0))) {
            int4 = 0;
          }
          break;
        }
        case 52:
        {
          if ((((((R.vb(55734)) + (R.vb(55735)) | 0) < int0) && R.eq(R.vb(55740), 0)) && R.eq(R.vb(55741), 0))) {
            int4 = 0;
          }
          break;
        }
        case 53:
        {
          if (((R.vb(55987) < int0) && R.eq(R.vb(55988), 0))) {
            int4 = 0;
          }
          break;
        }
        case 54:
        {
          if ((((((R.vb(58187)) + (R.vb(58188)) | 0) < int0) && R.eq(R.vb(58189), 0)) && R.eq(R.vb(58190), 0))) {
            int4 = 0;
          }
          break;
        }
        case 56:
        {
          if (((R.vb(60742) < int0) && R.eq(R.vb(60744), 0))) {
            int4 = 0;
          }
          break;
        }
        case 57:
        {
          if (((R.vb(60743) < int0) && R.eq(R.vb(60745), 0))) {
            int4 = 0;
          }
          break;
        }
      }
      int3 = ((int3) + (1) | 0);
    }
    return int4;
  };
  V[16158] = { vb: [45244, 45245, 45246, 45247, 45248, 45249, 45250, 45597, 47392, 47393, 47394, 47395, 47396, 47397, 47398, 47401, 50286, 50287], vp: [], vc: [], calls: [] };
  S[16158] = function (int0) {
    switch (R.key(int0)) {
      case 0:
      {
        return R.vb(45244);
      }
      case 1:
      {
        return R.vb(45245);
      }
      case 2:
      {
        return R.vb(45246);
      }
      case 3:
      {
        return R.vb(45247);
      }
      case 4:
      {
        return R.vb(45249);
      }
      case 5:
      {
        return R.vb(45248);
      }
      case 6:
      {
        return R.vb(45250);
      }
      case 7:
      {
        return R.vb(45597);
      }
      case 8:
      {
        return R.vb(47392);
      }
      case 9:
      {
        return R.vb(47393);
      }
      case 10:
      {
        return R.vb(47394);
      }
      case 11:
      {
        return R.vb(47395);
      }
      case 12:
      {
        return R.vb(47397);
      }
      case 13:
      {
        return R.vb(47396);
      }
      case 14:
      {
        return R.vb(47398);
      }
      case 15:
      {
        return R.vb(47401);
      }
      case 16:
      {
        return R.vb(50286);
      }
      case 17:
      {
        return R.vb(50287);
      }
    }
    return 0;
  };
  V[16254] = { vb: [], vp: [], vc: [], calls: [] };
  S[16254] = function (string0) {
    var string0 = `${R.s(string0)}<br>- Heals you for <col=ffffff>5%</col> of damage dealt, up to <col=ffffff>50</col> <col=ED705A>Life<nbsp>Points</col> per hit.`;
    return string0;
  };
  V[16255] = { vb: [], vp: [], vc: [], calls: [15973, 17444, 17710] };
  S[16255] = function (int0, int1, string0) {
    var int2 = R.call(17444, [int0]);
    var string0 = `${R.s(string0)}<br>- Applies <sprite=35769><nbsp><col=ffffff>${R.s(R.structParam(int0, 2794))}</col> to self.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [int2]))}.`;
    string0 = `${R.s(string0)}<br>- Can be recast to extend duration by <col=ffffff>${R.s(R.call(15973, [int2, 1]))}</col> per cast to a maximum of <col=ffffff>${R.s(R.call(15973, [6000, 1]))}</col>.`;
    return string0;
  };
  V[16256] = { vb: [], vp: [], vc: [], calls: [] };
  S[16256] = function (string0) {
    var string0 = `${R.s(string0)}<br>- <col=ffffff>5%</col> of damage taken is restored as <col=967AC0>Prayer<nbsp>Points</col>, up to <col=ffffff>100</col> per hit.`;
    return string0;
  };
  V[16279] = { vb: [], vp: [], vc: [], calls: [17444, 17710] };
  S[16279] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- Applies <sprite=31925><nbsp><col=ffffff>${R.s(R.structParam(52802, 2794))}</col> to self.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [R.call(17444, [int0])]))}.`;
    return string0;
  };
  V[16284] = { vb: [], vp: [], vc: [], calls: [15973] };
  S[16284] = function (int0) {
    return `<col=ffffff>Binds</col> enemies hit for <col=ffffff>${R.s(R.call(15973, [int0, 1]))}</col>`;
  };
  V[16291] = { vb: [], vp: [], vc: [], calls: [15973] };
  S[16291] = function (int0, int1) {
    if ((int1 > 0)) {
      return `Reduces the damage of enemies hit by <col=ffffff>${R.s(R.str(int0, 10))}%</col> for <col=ffffff>${R.s(R.call(15973, [int1, 1]))}</col>`;
    }
    return `Reduces the damage of enemies hit by <col=ffffff>${R.s(R.str(int0, 10))}%</col>`;
  };
  V[16325] = { vb: [], vp: [], vc: [], calls: [273] };
  S[16325] = function () {
    if (R.eq(R.call(273, []), 1)) {
      return 1;
    }
    if (((((R.itemParam(R.invObj(94, 3), 8569)) + (R.itemParam(R.invObj(94, 3), 2825)) | 0) > 0) && (((R.itemParam(R.invObj(94, 5), 8569)) + (R.itemParam(R.invObj(94, 5), 2825)) | 0) > 0))) {
      return 1;
    }
    return 0;
  };
  V[16377] = { vb: [50915, 50916, 50917, 50918, 50919, 60812], vp: [], vc: [], calls: [] };
  S[16377] = function (int0) {
    switch (R.key(int0)) {
      case 552:
      {
        return 1;
      }
      case 4021:
      {
        return 1;
      }
      case 6707:
      {
        return R.vb(50915);
      }
      case 4677:
      case 6544:
      {
        if (((R.eq(int0, 4677) && R.eq(R.vb(50916), 1)) && R.eq(R.vb(50917), 1))) {
          return 0;
        }
        if (R.eq(int0, 4677)) {
          return R.vb(50916);
        }
        if (R.eq(int0, 6544)) {
          return R.vb(50917);
        }
        break;
      }
      case 39687:
      {
        return R.vb(50918);
      }
      case 37589:
      {
        return R.vb(50919);
      }
      case 60398:
      {
        return R.vb(60812);
      }
      default:
      {
        return 0;
      }
    }
    return 0;
  };
  V[16472] = { vb: [], vp: [], vc: [], calls: [] };
  S[16472] = function (int0) {
    return R.mod(Math.imul(R.mod(int0, 5), 6), 10);
  };
  V[16583] = { vb: [], vp: [], vc: [], calls: [12478, 19021] };
  S[16583] = function (int0, int1) {
    if ((R.eq(int0, (-1 | 0)) || R.eq(int1, 0))) {
      return "null";
    }
    switch (R.key(int0)) {
      case 50082:
      {
      }
      default:
      {
        R.call(12478, ["Item set effect has param: [combatv2_ability_info_has_scripted_text] but no scripted text."]);
        return "null";
      }
    }
    return R.call(19021, [int1]);
  };
  V[16738] = { vb: [], vp: [], vc: [], calls: [15973] };
  S[16738] = function (int0, int1) {
    if ((int1 > 0)) {
      return `Reduces the <col=ffffff>Hit Chance</col> of enemies hit by <col=ffffff>${R.s(R.str(int0, 10))}%</col> for <col=ffffff>${R.s(R.call(15973, [int1, 1]))}</col>`;
    }
    return `Reduces the <col=ffffff>Hit Chance</col> of enemies hit by <col=ffffff>${R.s(R.str(int0, 10))}%</col>`;
  };
  V[16755] = { vb: [], vp: [], vc: [], calls: [] };
  S[16755] = function (string0) {
    var string0 = `${R.s(string0)}- Attacks have a <col=ffffff>${R.s(R.str(25, 10))}%</col> chance to heal <col=ffffff>${R.s(R.str(3, 10))}%</col> of your <sprite=18851><nbsp><col=ED705A>Life<nbsp>Points</col>, and restore <col=ffffff>${R.s(R.str(3, 10))}%</col> of your <sprite=18857><nbsp><col=967AC0>Prayer Points</col>.`;
    return string0;
  };
  V[16756] = { vb: [], vp: [], vc: [], calls: [] };
  S[16756] = function (string0) {
    var string0 = `${R.s(string0)}- Attacks have a <col=ffffff>${R.s(R.str(25, 10))}%</col> chance to reduce the target's stats by <col=ffffff>${R.s(R.str(1, 10))}</col>.`;
    return string0;
  };
  V[16823] = { vb: [43655], vp: [], vc: [], calls: [16825] };
  S[16823] = function () {
    if ((R.eq(R.vb(43655), 1) || R.eq(R.vb(43655), 2))) {
      if ((!R.eq(R.call(16825, []), 1))) {
        return 0;
      }
      return 1;
    }
    return 0;
  };
  V[16825] = { vb: [43656], vp: [], vc: [], calls: [] };
  S[16825] = function () {
    return R.vb(43656);
  };
  V[16841] = { vb: [], vp: [], vc: [], calls: [17709, 17717, 18561] };
  S[16841] = function (int0, int1, string0) {
    if (R.eq(R.structParam(int0, 2842), 1)) {
      var string0 = `${R.s(string0)}<br>- <sprite=23399><nbsp><col=ffffff>${R.s(R.structParam(52777, 2794))}</col>: Hits up to ${R.s(R.call(18561, [9]))} within ${R.s(R.call(17709, [1]))} of the target, dealing ${R.s(R.call(17717, [70]))}.`;
    }
    string0 = `${R.s(string0)}<br>- <sprite=23399><nbsp><col=ffffff>${R.s(R.structParam(52777, 2794))}</col>: Heals you for <col=ffffff>${R.s(R.str(5, 10))}%</col> of the damage dealt.`;
    return string0;
  };
  V[16843] = { vb: [51798], vp: [], vc: [], calls: [249, 16823, 16855, 16858, 16939] };
  S[16843] = function (int0) {
    if ((R.eq(R.call(249, []), 0) || R.eq(R.call(16823, []), 0))) {
      return 0;
    }
    var int1 = 0;
    if (R.eq(R.vb(51798), 1)) {
      int1 = 10;
    } else {
      if (R.eq(R.vb(51798), 2)) {
        int1 = 25;
      }
    }
    switch (R.key(int0)) {
      case 0:
      {
        return R.scale(R.call(16858, []), 100, int1);
      }
      case 1:
      {
        return R.scale(R.call(16939, []), 100, int1);
      }
      case 2:
      {
        return R.scale(R.call(16855, []), 100, int1);
      }
    }
    return 0;
  };
  V[16847] = { vb: [], vp: [], vc: [], calls: [12477] };
  S[16847] = function (int0) {
    if ((R.call(12477, []) < int0)) {
      return 1;
    }
    if ((R.eq(int0, R.call(12477, [])) && (R.mod(R.dateMinutes(), 1440) < 720))) {
      return 1;
    }
    return 0;
  };
  V[16854] = { vb: [], vp: [], vc: [], calls: [] };
  S[16854] = function () {
    return Math.imul(R.stat(23), 100);
  };
  V[16855] = { vb: [], vp: [], vc: [], calls: [] };
  S[16855] = function () {
    return Math.imul(R.statBase(23), 100);
  };
  V[16856] = { vb: [], vp: [], vc: [], calls: [] };
  S[16856] = function () {
    return Math.imul(R.stat(3), 100);
  };
  V[16858] = { vb: [], vp: [], vc: [], calls: [] };
  S[16858] = function () {
    return Math.imul(R.statBase(3), 100);
  };
  V[16860] = { vb: [], vp: [], vc: [], calls: [20917] };
  S[16860] = function (int0) {
    if (R.eq(R.call(20917, []), 1)) {
      return R.min(int0, 2147483647);
    }
    return R.min(int0, 32000);
  };
  V[16861] = { vb: [], vp: [10642], vc: [], calls: [] };
  S[16861] = function (int0) {
    if (R.eq(R.vp(10642), 1)) {
      return 1;
    }
    var int1 = 0;
    var string0 = "";
    var int2 = 0;
    [int1, string0, int2, string0, int2, int2, string0] = R.worldSpecific(int0);
    if (R.eq(R.testbit(int1, 30), 1)) {
      return 1;
    }
    return 0;
  };
  V[16938] = { vb: [], vp: [], vc: [], calls: [] };
  S[16938] = function () {
    return Math.imul(R.stat(5), 10);
  };
  V[16939] = { vb: [], vp: [], vc: [], calls: [] };
  S[16939] = function () {
    return Math.imul(R.statBase(5), 10);
  };
  V[17012] = { vb: [], vp: [], vc: [], calls: [12676] };
  S[17012] = function () {
    if (R.eq(R.call(12676, [44550]), 1)) {
      return 2000;
    }
    return 500;
  };
  V[17141] = { vb: [], vp: [], vc: [], calls: [16284, 17709, 17717, 17725, 18561] };
  S[17141] = function (int0, int1, string0) {
    var int2 = 0;
    switch (R.key(int0)) {
      case 14823:
      {
        int2 = 16;
        break;
      }
      case 14817:
      {
        int2 = 12;
        break;
      }
      case 14811:
      {
        int2 = 8;
        break;
      }
      case 14805:
      {
        int2 = 4;
        break;
      }
    }
    if (R.eq(R.structParam(int0, 2842), 1)) {
      var string0 = `${R.s(string0)}<br>- <sprite=23399><nbsp><col=ffffff>${R.s(R.structParam(52777, 2794))}</col>: Hits up to ${R.s(R.call(18561, [9]))} within ${R.s(R.call(17709, [1]))} of the target, dealing ${R.s(R.call(17717, [70]))}.`;
      string0 = `${R.s(string0)}<br>- <sprite=23399><nbsp><col=ffffff>${R.s(R.structParam(52777, 2794))}</col>: ${R.s(R.call(16284, [int2]))}.`;
    } else {
      string0 = `${R.s(string0)}<br>- <sprite=23399><nbsp><col=ffffff>${R.s(R.structParam(52777, 2794))}</col>: ${R.s(R.call(17725, [int2]))}.`;
    }
    return string0;
  };
  V[17172] = { vb: [], vp: [], vc: [], calls: [] };
  S[17172] = function (int0, int1) {
    switch (R.key(int1)) {
      case 7:
      case 6:
      case 5:
      case 12:
      {
        if (((((((R.eq(R.itemWearpos(int0), 0) || R.eq(R.itemWearpos(int0), 4)) || R.eq(R.itemWearpos(int0), 7)) || R.eq(R.itemWearpos(int0), 9)) || R.eq(R.itemWearpos(int0), 10)) && (R.itemParam(int0, 2870) > 0)) && R.eq(R.itemParam(int0, 2832), 0))) {
          if (((((R.itemParam(int0, 641) > 0) || (R.itemParam(int0, 643) > 0)) || (R.itemParam(int0, 965) > 0)) || (R.itemParam(int0, 8881) > 0))) {
            return 2;
          }
          return 1;
        }
        break;
      }
    }
    return 0;
  };
  V[17268] = { vb: [], vp: [], vc: [], calls: [12478] };
  S[17268] = function (int0, int1) {
    switch (R.key(int0)) {
      case 16:
      {
        return R.structParam(int1, 7602);
      }
      case 23:
      {
        return R.structParam(int1, 7603);
      }
      case 27:
      {
        return R.structParam(int1, 8161);
      }
      case 0:
      {
        return R.structParam(int1, 7604);
      }
      case 3:
      {
        return R.structParam(int1, 7605);
      }
      case 22:
      {
        return R.structParam(int1, 7606);
      }
      case 7:
      {
        return R.structParam(int1, 7607);
      }
      case 12:
      {
        return R.structParam(int1, 7608);
      }
      case 1:
      {
        return R.structParam(int1, 7609);
      }
      case 25:
      {
        return R.structParam(int1, 7610);
      }
      case 24:
      {
        return R.structParam(int1, 7611);
      }
      case 19:
      {
        return R.structParam(int1, 7612);
      }
      case 11:
      {
        return R.structParam(int1, 7613);
      }
      case 10:
      {
        return R.structParam(int1, 7614);
      }
      case 9:
      {
        return R.structParam(int1, 7615);
      }
      case 15:
      {
        return R.structParam(int1, 7616);
      }
      case 21:
      {
        return R.structParam(int1, 7617);
      }
      case 26:
      {
        return R.structParam(int1, 7618);
      }
      case 6:
      {
        return R.structParam(int1, 7619);
      }
      case 14:
      {
        return R.structParam(int1, 7620);
      }
      case 28:
      {
        return R.structParam(int1, 8936);
      }
      case 5:
      {
        return R.structParam(int1, 7621);
      }
      case 4:
      {
        return R.structParam(int1, 7622);
      }
      case 20:
      {
        return R.structParam(int1, 7623);
      }
      case 18:
      {
        return R.structParam(int1, 7624);
      }
      case 13:
      {
        return R.structParam(int1, 7625);
      }
      case 2:
      {
        return R.structParam(int1, 7626);
      }
      case 17:
      {
        return R.structParam(int1, 7628);
      }
      case 8:
      {
        return R.structParam(int1, 7629);
      }
    }
    R.call(12478, ["Unexpected stat in potion stat lookup"]);
    return 0;
  };
  V[17269] = { vb: [], vp: [], vc: [], calls: [12478] };
  S[17269] = function (int0, int1) {
    switch (R.key(int0)) {
      case 16:
      {
        return R.structParam(int1, 7630);
      }
      case 23:
      {
        return R.structParam(int1, 7631);
      }
      case 27:
      {
        return R.structParam(int1, 8162);
      }
      case 0:
      {
        return R.structParam(int1, 7632);
      }
      case 3:
      {
        return R.structParam(int1, 7633);
      }
      case 22:
      {
        return R.structParam(int1, 7634);
      }
      case 7:
      {
        return R.structParam(int1, 7635);
      }
      case 12:
      {
        return R.structParam(int1, 7636);
      }
      case 1:
      {
        return R.structParam(int1, 7637);
      }
      case 25:
      {
        return R.structParam(int1, 7638);
      }
      case 24:
      {
        return R.structParam(int1, 7639);
      }
      case 19:
      {
        return R.structParam(int1, 7640);
      }
      case 11:
      {
        return R.structParam(int1, 7641);
      }
      case 10:
      {
        return R.structParam(int1, 7642);
      }
      case 9:
      {
        return R.structParam(int1, 7643);
      }
      case 15:
      {
        return R.structParam(int1, 7644);
      }
      case 21:
      {
        return R.structParam(int1, 7645);
      }
      case 26:
      {
        return R.structParam(int1, 7646);
      }
      case 6:
      {
        return R.structParam(int1, 7647);
      }
      case 14:
      {
        return R.structParam(int1, 7648);
      }
      case 28:
      {
        return R.structParam(int1, 8937);
      }
      case 5:
      {
        return R.structParam(int1, 7649);
      }
      case 4:
      {
        return R.structParam(int1, 7650);
      }
      case 20:
      {
        return R.structParam(int1, 7651);
      }
      case 18:
      {
        return R.structParam(int1, 7652);
      }
      case 13:
      {
        return R.structParam(int1, 7653);
      }
      case 2:
      {
        return R.structParam(int1, 7654);
      }
      case 17:
      {
        return R.structParam(int1, 7656);
      }
      case 8:
      {
        return R.structParam(int1, 7657);
      }
    }
    R.call(12478, ["Unexpected stat in potion stat lookup"]);
    return 0;
  };
  V[17272] = { vb: [], vp: [], vc: [], calls: [5003, 17268, 17269, 17273] };
  S[17272] = function (int0, int1) {
    if (R.eq(int0, (-1 | 0))) {
      return [0, ""];
    }
    return [R.call(5003, [int0, int1]), R.call(17273, [R.call(17268, [int1, int0]), R.call(17269, [int1, int0])])];
  };
  V[17273] = { vb: [], vp: [], vc: [], calls: [] };
  S[17273] = function (int0, int1) {
    var string0 = "";
    var int2 = 0;
    if ((int1 > 0)) {
      string0 = `${R.s(R.str(int1, 10))}%`;
    }
    if ((!R.eq(int0, 0))) {
      if ((int0 < 0)) {
        int2 = 1;
      }
      if ((R.len(string0) > 0)) {
        string0 = `${R.s(string0)} `;
      }
      string0 = `${R.s(string0)}${R.s(R.textSwitch(int2, "", "+"))}${R.s(R.str(int0, 10))}`;
    }
    return string0;
  };
  V[17274] = { vb: [], vp: [], vc: [], calls: [17273] };
  S[17274] = function (int0) {
    return R.call(17273, [R.structParam(int0, 7670), R.structParam(int0, 7671)]);
  };
  V[17275] = { vb: [], vp: [], vc: [], calls: [17273] };
  S[17275] = function (int0) {
    return R.call(17273, [R.structParam(int0, 7672), R.structParam(int0, 7673)]);
  };
  V[17276] = { vb: [], vp: [], vc: [], calls: [17273] };
  S[17276] = function (int0) {
    return R.call(17273, [Math.imul(R.structParam(int0, 7674), 10), R.structParam(int0, 7675)]);
  };
  V[17277] = { vb: [], vp: [], vc: [], calls: [17273] };
  S[17277] = function (int0) {
    return R.call(17273, [R.structParam(int0, 7676), R.structParam(int0, 7677)]);
  };
  V[17278] = { vb: [], vp: [], vc: [], calls: [859] };
  S[17278] = function (int0) {
    var int1 = R.structParam(int0, 6390);
    var int2 = R.structParam(int0, 8773);
    return ((int1) + (R.scale(R.idiv(R.call(859, []), 10), 100, int2)) | 0);
  };
  V[17279] = { vb: [], vp: [], vc: [], calls: [17273] };
  S[17279] = function (int0) {
    return R.call(17273, [R.structParam(int0, 6390), R.structParam(int0, 8773)]);
  };
  V[17280] = { vb: [], vp: [], vc: [], calls: [859] };
  S[17280] = function (int0) {
    var int1 = R.structParam(int0, 8163);
    var int2 = R.structParam(int0, 8774);
    return ((int1) + (R.scale(R.idiv(R.call(859, []), 10), 100, int2)) | 0);
  };
  V[17281] = { vb: [], vp: [], vc: [], calls: [17273] };
  S[17281] = function (int0) {
    return R.call(17273, [R.structParam(int0, 8163), R.structParam(int0, 8774)]);
  };
  V[17444] = { vb: [], vp: [], vc: [], calls: [] };
  S[17444] = function (int0) {
    var int1 = R.structParam(int0, 3740);
    switch (R.key(int0)) {
      default:
      {
      }
    }
    return int1;
  };
  V[17452] = { vb: [], vp: [], vc: [], calls: [7436] };
  S[17452] = function (int0) {
    var int1 = R.invObj(94, 3);
    var int2 = R.call(7436, [int1]);
    if (R.eq(R.structParam(int0, 2799), 0)) {
      switch (R.key(R.structParam(int0, 2806))) {
        case 29:
        {
          return Math.imul(30, int2);
        }
        case 1:
        {
          return Math.imul(30, int2);
        }
        case 4:
        {
          return Math.imul(30, int2);
        }
        case 3:
        {
          return Math.imul(30, int2);
        }
      }
    }
    return R.structParam(int0, 2800);
  };
  V[17454] = { vb: [53587], vp: [], vc: [], calls: [] };
  S[17454] = function (int0) {
    var int1 = R.structParam(int0, 3740);
    switch (R.key(R.vb(53587))) {
      case 1:
      {
        int1 = ((int1) + (10) | 0);
        break;
      }
      case 2:
      {
        int1 = ((int1) + (20) | 0);
        break;
      }
      case 3:
      {
        int1 = ((int1) + (30) | 0);
        break;
      }
    }
    return int1;
  };
  V[17457] = { vb: [], vp: [], vc: [], calls: [] };
  S[17457] = function () {
    return 25;
  };
  V[17458] = { vb: [], vp: [], vc: [], calls: [] };
  S[17458] = function () {
    return 12;
  };
  V[17459] = { vb: [], vp: [], vc: [], calls: [] };
  S[17459] = function () {
    var int0 = 3;
    if (R.eq(R.itemParam(R.invObj(94, 5), 8928), 48397)) {
      int0 = ((int0) + (2) | 0);
    }
    return int0;
  };
  V[17460] = { vb: [54731], vp: [], vc: [], calls: [] };
  S[17460] = function (int0) {
    var int1 = 0;
    var int2 = 0;
    switch (R.key(int0)) {
      case 48326:
      {
        int1 = 25;
        break;
      }
      case 48327:
      {
        int1 = 50;
        break;
      }
    }
    if ((R.eq(R.itemParam(R.invObj(94, 13), 8928), 49089) && R.eq(R.vb(54731), 2))) {
      int2 = 15;
    }
    return ((R.scale(R.statBase(28), 100, int1)) + (int2) | 0);
  };
  V[17461] = { vb: [], vp: [], vc: [], calls: [] };
  S[17461] = function (int0) {
    switch (R.key(int0)) {
      default:
      {
      }
    }
    return R.itemParam(int0, 4333);
  };
  V[17464] = { vb: [], vp: [], vc: [], calls: [] };
  S[17464] = function () {
    return 5;
  };
  V[17465] = { vb: [], vp: [], vc: [], calls: [] };
  S[17465] = function (int0) {
    if (R.eq(int0, (-1 | 0))) {
      return 0;
    }
    if ((((((!R.eq(R.itemWearpos(int0), 0)) && (!R.eq(R.itemWearpos(int0), 4))) && (!R.eq(R.itemWearpos(int0), 7))) && (!R.eq(R.itemWearpos(int0), 9))) && (!R.eq(R.itemWearpos(int0), 10)))) {
      return 0;
    }
    if (R.eq(R.itemParam(int0, 2870), 0)) {
      return 0;
    }
    if ((!R.eq(R.itemParam(int0, 4328), 0))) {
      return 0;
    }
    if (((((R.itemParam(int0, 641) > 0) || (R.itemParam(int0, 965) > 0)) || (R.itemParam(int0, 643) > 0)) || (R.itemParam(int0, 8881) > 0))) {
      return 0;
    }
    if (R.eq(R.itemParam(int0, 2824), 1)) {
      return 0;
    }
    if (R.eq(R.itemParam(int0, 5541), 1)) {
      return 0;
    }
    return 1;
  };
  V[17466] = { vb: [], vp: [], vc: [], calls: [] };
  S[17466] = function (int0) {
    if ((R.itemParam(int0, 4596) > 0)) {
      return R.idiv(R.itemParam(int0, 4596), 5);
    }
    return R.idiv(R.itemParam(int0, 750), 5);
  };
  V[17467] = { vb: [], vp: [], vc: [], calls: [17465, 17466] };
  S[17467] = function (int0) {
    var int1 = 0;
    if (R.eq(R.call(17465, [int0]), 1)) {
      int1 = ((int1) + (R.call(17466, [int0])) | 0);
    }
    return int1;
  };
  V[17493] = { vb: [], vp: [], vc: [], calls: [4583] };
  S[17493] = function (int0, int1, int2, int3) {
    var int4 = R.invVar(int0, int2, 53600);
    var int5 = R.invVar(int0, int2, 53601);
    var int6 = R.itemParam(int1, 8940);
    var int7 = R.itemParam(int1, 8941);
    var string0 = R.itemParam(int1, 8942);
    var string1 = R.itemParam(int1, 8943);
    var string2 = "";
    if ((int6 > 0)) {
      if (((((int7 > 0) && (int5 < int7)) || R.eq(int7, 0)) || ((int6 > 0) && (int4 >= int6)))) {
        string2 = R.cat(string2, `${R.s(R.str(int4, 10))} / ${R.s(R.str(int6, 10))} ${R.s(string0)} ${R.s(R.call(4583, [int6, "soul", "souls"]))}`);
      }
    } else {
      if (((int6 > 0) && (int4 >= int6))) {
        string2 = R.cat(string2, `${R.s(R.str(int4, 10))} / ${R.s(R.str(int6, 10))} ${R.s(string0)} ${R.s(R.call(4583, [int6, "soul", "souls"]))}`);
      }
    }
    if ((((int6 > 0) && (int7 > 0)) && (((int4 < int6) && (int5 < int7)) || ((int4 >= int6) && (int5 >= int7))))) {
      string2 = R.cat(string2, "<br>");
    }
    if ((int7 > 0)) {
      if (((((int6 > 0) && (int4 < int6)) || R.eq(int6, 0)) || ((int7 > 0) && (int5 >= int7)))) {
        string2 = R.cat(string2, `${R.s(R.str(int5, 10))} / ${R.s(R.str(int7, 10))} ${R.s(string1)} ${R.s(R.call(4583, [int7, "soul", "souls"]))}`);
      }
    } else {
      if (((int7 > 0) && (int5 >= int7))) {
        string2 = R.cat(string2, `${R.s(R.str(int5, 10))} / ${R.s(R.str(int7, 10))} ${R.s(string1)} ${R.s(R.call(4583, [int7, "soul", "souls"]))}`);
      }
    }
    if ((R.eq(int6, 0) && R.eq(int7, 0))) {
      string2 = R.cat(string2, `${R.s(R.str(int4, 10))} ${R.s(string0)} ${R.s(R.call(4583, [int4, "soul", "souls"]))}`);
      string2 = R.cat(string2, `<br>${R.s(R.str(int5, 10))} ${R.s(string1)} ${R.s(R.call(4583, [int5, "soul", "souls"]))}`);
    }
    if ((R.eq(int3, 1) && (((int6 > 0) && (int4 >= int6)) || ((int7 > 0) && (int5 >= int7))))) {
      string2 = R.cat(string2, "<br><col=EB2F2F>This soul urn is full.");
    }
    return string2;
  };
  V[17495] = { vb: [], vp: [], vc: [], calls: [12478] };
  S[17495] = function (int0, string0) {
    if (R.eq(int0, (-1 | 0))) {
      R.call(12478, [`${R.s(string0)} - dbrow passed in as null!`]);
      return 1;
    }
    return 0;
  };
  V[17518] = { vb: [], vp: [], vc: [], calls: [] };
  S[17518] = function (int0) {
    R.dbFind(966674, int0, 0);
    return R.dbNext();
  };
  V[17520] = { vb: [], vp: [], vc: [], calls: [17495, 17518] };
  S[17520] = function (int0) {
    var int1 = R.call(17518, [int0]);
    if (R.eq(R.call(17495, [int1, "necro_ritual_component_id_to_component_npc_get"]), 1)) {
      return (-1 | 0);
    }
    return R.dbField(int1, 966673, 0);
  };
  V[17522] = { vb: [], vp: [], vc: [], calls: [12478] };
  S[17522] = function (int0) {
    switch (R.key(int0)) {
      case 30434:
      {
        return "Elemental I";
      }
      case 30436:
      {
        return "Elemental II";
      }
      case 30438:
      {
        return "Elemental III";
      }
      case 30440:
      {
        return "Reagent I";
      }
      case 30442:
      {
        return "Reagent II";
      }
      case 30444:
      {
        return "Reagent III";
      }
      case 30446:
      {
        return "Commune I";
      }
      case 30448:
      {
        return "Commune II";
      }
      case 30450:
      {
        return "Commune III";
      }
      case 30452:
      {
        return "Change I";
      }
      case 30454:
      {
        return "Change II";
      }
      case 30456:
      {
        return "Change III";
      }
      case 30458:
      {
        return "Multiply I";
      }
      case 30460:
      {
        return "Multiply II";
      }
      case 30462:
      {
        return "Multiply III";
      }
      case 30464:
      {
        return "Speed I";
      }
      case 30466:
      {
        return "Speed II";
      }
      case 30468:
      {
        return "Speed III";
      }
      case 30470:
      {
        return "Protection I";
      }
      case 30472:
      {
        return "Protection II";
      }
      case 30474:
      {
        return "Protection III";
      }
      case 30476:
      {
        return "Attraction I";
      }
      case 30478:
      {
        return "Attraction II";
      }
      case 30480:
      {
      }
      default:
      {
        R.call(12478, ["necro_ritual_component_npc_to_name_get - no NPC name specified!"]);
        return "";
      }
    }
    return "Attraction III";
  };
  V[17662] = { vb: [], vp: [], vc: [], calls: [17675, 19132] };
  S[17662] = function (int0, int1) {
    switch (R.key(int0)) {
      case 48252:
      case 48251:
      {
        return R.call(17675, []);
      }
      case 50208:
      {
        return R.call(19132, []);
      }
    }
    return "";
  };
  V[17663] = { vb: [], vp: [], vc: [], calls: [4583, 5359, 7653, 10159, 10536, 10537, 11216, 11217, 14722, 14723, 15973, 16755, 16756, 17464, 17708, 17717, 18379, 18546, 18547, 18825, 18826, 19638, 19639, 19640, 19641, 19642, 19973, 19974, 19975, 20090, 20091, 20092, 20395, 20396, 20397] };
  S[17663] = function (int0) {
    var int1 = R.itemParam(int0, 8928);
    var string0 = "";
    var string1 = "";
    if ((!R.eq(R.itemParam(int0, 9001), (-1 | 0)))) {
      string1 = R.call(15973, [R.itemParam(int0, 7382), 1]);
      if (R.eq(R.varRef(R.itemParam(int0, 9001)), 2)) {
        string0 = `${R.s(string0)}- <col=00ff00>Charged (${R.s(string1)})</col> - Passive active.<br>`;
      } else {
        string0 = `${R.s(string0)}- <col=ffffff>Charging (${R.s(string1)})</col> - Passive will be active after item has been equipped for <col=ffffff>${R.s(string1)}</col><br>`;
      }
    }
    switch (R.key(int1)) {
      case 48396:
      {
        string0 = `${R.s(string0)}- <sprite=30015><nbsp><col=ffffff>${R.s(R.structParam(48293, 2794))}:</col> Generates <col=ffffff>1 <sprite=30128><nbsp>${R.s(R.structParam(48350, 2794))}</col> stack.`;
        string0 = `${R.s(string0)}<br>- At <col=ffffff>${R.s(R.str(R.call(17464, []), 10))}</col> stacks, your <sprite=30015><nbsp><col=ffffff>${R.s(R.structParam(48293, 2794))}</col> is empowered and deals ${R.s(R.call(17717, [200]))}</col>.`;
        break;
      }
      case 48397:
      {
        string0 = `${R.s(string0)}- Maximum number of <col=ffffff><sprite=30123><nbsp>${R.s(R.structParam(48334, 2794))}</col> stacks is increased by <col=ffffff>${R.s(R.str(2, 10))}</col>.`;
        break;
      }
      case 49089:
      {
        string0 = `${R.s(string0)}- Increases the level of <col=ffffff><sprite=30099><nbsp>${R.s(R.structParam(48340, 2794))}</col> by <col=ffffff>15</col>.`;
        break;
      }
      case 52804:
      {
        string0 = R.call(10159, [string0]);
        break;
      }
      case 52805:
      {
        string0 = R.call(10537, [string0]);
        break;
      }
      case 52807:
      {
        string0 = R.call(11216, [string0]);
        break;
      }
      case 52806:
      {
        string0 = R.call(11217, [string0]);
        string0 = R.call(17708, [52779, string0]);
        break;
      }
      case 49554:
      {
        string0 = `${R.s(string0)}- Stores <col=ffffff>${R.s(R.str(50, 10))}%</col> of damage taken as <col=ffffff>${R.s(R.structParam(49555, 2794))}</col> stacks, up to a maximum of <col=ffffff>${R.s(R.strLoc(80000, 1))}</col>.`;
        string0 = `${R.s(string0)}<br>- After <col=ffffff>${R.s(R.str(15, 10))}</col> attacks launch an additional attack that deals <col=ffffff>${R.s(R.call(7653, [100, 1, 1, 0, 1]))}%</col> of the total damage stored.`;
        break;
      }
      case 49549:
      case 29051:
      {
        string0 = R.call(18379, [int1, string0]);
        break;
      }
      case 49560:
      case 49558:
      {
        string0 = R.call(18547, [int1, string0]);
        break;
      }
      case 49561:
      case 49559:
      {
        string0 = R.call(18546, [int1, string0]);
        break;
      }
      case 49688:
      {
        string0 = R.call(16755, [string0]);
        break;
      }
      case 49689:
      {
        string0 = R.call(16756, [string0]);
        break;
      }
      case 49690:
      {
        string0 = R.call(18825, [string0]);
        break;
      }
      case 49691:
      {
        string0 = R.call(18826, [string0]);
        break;
      }
      case 49394:
      {
        string0 = `${R.s(string0)}- <col=A788DD>Necromancy attacks</col> have a <col=ffffff>${R.s(R.str(5, 10))}%</col> chance to generate <col=ffffff>${R.s(R.str(1, 10))}</col> <col=ffffff><sprite=30123><nbsp>${R.s(R.structParam(48334, 2794))}</col> stack with each hit.`;
        break;
      }
      case 49828:
      {
        string0 = `${R.s(string0)}- <col=A788DD>Necromancy attacks</col> have a <col=ffffff>${R.s(R.str(10, 10))}%</col> chance to generate <col=ffffff>${R.s(R.str(2, 10))}</col> <col=ffffff><sprite=30101><nbsp>${R.s(R.structParam(48333, 2794))}</col> ${R.s(R.call(4583, [2, "stack", "stacks"]))} with each cast.`;
        break;
      }
      case 51046:
      {
        string0 = R.call(19638, [int0, string0]);
        break;
      }
      case 51047:
      {
        string0 = R.call(19639, [string0]);
        break;
      }
      case 51048:
      {
        string0 = R.call(19640, [string0]);
        break;
      }
      case 51049:
      {
        string0 = R.call(19641, [string0]);
        break;
      }
      case 51050:
      {
        string0 = R.call(19642, [string0]);
        break;
      }
      case 51668:
      {
        string0 = R.call(19973, [string0]);
        break;
      }
      case 51671:
      {
        string0 = R.call(19974, [string0]);
        break;
      }
      case 51670:
      {
        string0 = R.call(19975, [string0]);
        break;
      }
      case 52067:
      {
        string0 = R.call(20090, [string0]);
        break;
      }
      case 52076:
      {
        string0 = R.call(20091, [string0]);
        break;
      }
      case 52526:
      {
        string0 = R.call(14722, [string0]);
        break;
      }
      case 52075:
      {
        string0 = R.call(20092, [string0]);
        break;
      }
      case 52522:
      {
        string0 = R.call(14723, [string0]);
        break;
      }
      case 52523:
      {
        string0 = R.call(20395, [string0]);
        break;
      }
      case 52524:
      {
        string0 = R.call(20396, [string0]);
        break;
      }
      case 52525:
      {
        string0 = R.call(20397, [string0]);
        break;
      }
      case 52803:
      {
        string0 = R.call(10536, [string0]);
        break;
      }
      case 2908:
      {
        string0 = R.call(5359, [string0]);
        break;
      }
    }
    return string0;
  };
  V[17675] = { vb: [53463], vp: [], vc: [], calls: [17520, 17522] };
  S[17675] = function () {
    if (R.eq(R.vb(53463), 0)) {
      return "Current alteration: None";
    }
    return `Current alteration: ${R.s(R.call(17522, [R.call(17520, [R.vb(53463)])]))}`;
  };
  V[17676] = { vb: [27168], vp: [], vc: [], calls: [11087, 11088] };
  S[17676] = function (int0) {
    var string0 = "";
    if (R.eq(R.structParam(int0, 8116), 1)) {
      string0 = R.call(11088, [int0]);
    } else {
      string0 = R.structParam(int0, 2794);
      if ((R.eq(R.vb(27168), 1) && (!R.eq(R.strcmp(R.structParam(int0, 7998), ""), 0)))) {
        string0 = R.cat(string0, `<br>${R.s(R.structParam(int0, 7998))}`);
      } else {
        if ((!R.eq(R.strcmp(R.structParam(int0, 2795), ""), 0))) {
          string0 = R.cat(string0, `<br>${R.s(R.structParam(int0, 2795))}`);
        }
      }
    }
    if (R.eq(R.structParam(int0, 8119), 1)) {
      string0 = `${R.s(string0)}${R.s(R.call(11087, [int0]))}`;
    }
    return string0;
  };
  V[17694] = { vb: [], vp: [], vc: [], calls: [6506] };
  S[17694] = function (int0) {
    var int1 = 0;
    var int2 = 0;
    [int1, int2] = R.call(6506, [int0]);
    return int2;
  };
  V[17697] = { vb: [], vp: [], vc: [], calls: [17663] };
  S[17697] = function (int0) {
    var string0 = "";
    var string1 = "";
    var int1 = R.invObj(94, 3);
    var int2 = R.invObj(94, 5);
    switch (R.key(R.structParam(int0, 2806))) {
      case 29:
      {
        if (R.eq(R.itemParam(int1, 8898), 1)) {
          string0 = `${R.s(string0)}<br><br><col=ffffff>Main-hand:</col> ${R.s(R.itemName(int1))}`;
          string1 = R.call(17663, [int1]);
          if ((R.len(string1) > 0)) {
            string0 = `${R.s(string0)}<br>${R.s(string1)}`;
          }
          if (R.eq(R.itemParam(int2, 8898), 1)) {
            string0 = `${R.s(string0)}<br><br><col=ffffff>Off-hand: </col>${R.s(R.itemName(int2))}`;
            string1 = R.call(17663, [int2]);
            if ((R.len(string1) > 0)) {
              string0 = `${R.s(string0)}<br>${R.s(string1)}`;
            }
          }
        }
        break;
      }
    }
    return string0;
  };
  V[17700] = { vb: [], vp: [], vc: [], calls: [17701] };
  S[17700] = function (int0) {
    return R.call(17701, [R.structParam(int0, 8929)]);
  };
  V[17701] = { vb: [], vp: [], vc: [], calls: [] };
  S[17701] = function (int0) {
    switch (R.key(int0)) {
      case 1:
      {
        return "Evasion";
      }
      case 2:
      {
        return "Power";
      }
      case 3:
      {
      }
      default:
      {
        return "None";
      }
    }
    return "Sustain";
  };
  V[17707] = { vb: [], vp: [], vc: [], calls: [] };
  S[17707] = function (int0, int1) {
    switch (R.key(int0)) {
      case 1:
      {
        if (R.eq(int1, 1)) {
          return "<sprite=10431><nbsp>Single-target";
        }
        return "Single-target";
      }
      case 2:
      {
        if (R.eq(int1, 1)) {
          return "<sprite=10429><nbsp>Multi-target";
        }
        return "Multi-target";
      }
      case 3:
      {
        if (R.eq(int1, 1)) {
          return "<sprite=10430><nbsp>Self-target";
        }
        return "Self-target";
      }
      case 4:
      {
        if (R.eq(int1, 1)) {
          return "<sprite=10428><nbsp>Area-target";
        }
        return "Area-target";
      }
    }
    return "";
  };
  V[17708] = { vb: [], vp: [], vc: [], calls: [17676] };
  S[17708] = function (int0, string0) {
    if (R.eq(int0, (-1 | 0))) {
      return string0;
    }
    return `${R.s(string0)}<br><br><col=ffffff>${R.s(R.call(17676, [int0]))}`;
  };
  V[17709] = { vb: [], vp: [], vc: [], calls: [4583] };
  S[17709] = function (int0) {
    return `<col=ffffff>${R.s(R.str(int0, 10))}<nbsp>${R.s(R.call(4583, [int0, "tile", "tiles"]))}</col>`;
  };
  V[17710] = { vb: [], vp: [], vc: [], calls: [15973] };
  S[17710] = function (int0) {
    return `<col=ffffff>${R.s(R.call(15973, [int0, 1]))}</col> duration`;
  };
  V[17711] = { vb: [], vp: [], vc: [], calls: [15973] };
  S[17711] = function (int0, int1) {
    return `Cooldown reduced ${R.s(R.textSwitch(int1, "to", "by"))} <col=ffffff>${R.s(R.call(15973, [int0, 1]))}</col>`;
  };
  V[17712] = { vb: [], vp: [], vc: [], calls: [] };
  S[17712] = function (int0, int1) {
    return `<sprite=14907><nbsp><col=DEAC18>Adrenaline</col> cost is reduced ${R.s(R.textSwitch(int1, "to", "by"))} <col=ffffff>${R.s(R.str(R.idiv(int0, 10), 10))}%</col>`;
  };
  V[17717] = { vb: [], vp: [], vc: [], calls: [7653] };
  S[17717] = function (int0) {
    return `<col=ffffff>${R.s(R.call(7653, [int0, 2, 2, 1, 1]))}x</col> damage`;
  };
  V[17718] = { vb: [], vp: [], vc: [], calls: [17719] };
  S[17718] = function (int0, int1, int2, int3) {
    return R.call(17719, [int0, 0, int1, int2, int3]);
  };
  V[17719] = { vb: [], vp: [], vc: [], calls: [17722, 17726, 18567] };
  S[17719] = function (int0, int1, int2, int3, int4) {
    var int5 = R.call(17726, [int0]);
    if (R.eq(int1, 0)) {
      var int1 = R.call(18567, [int0]);
    }
    var string0 = R.call(17722, [int1, 1]);
    if (R.eq(int3, 1)) {
      string0 = `bonus ${R.s(string0)}`;
    }
    if ((R.eq(int4, 1) && (int5 > 0))) {
      return `<col=ffffff>${R.s(R.strLoc(R.scale(int2, 100, int5), 1))}</col> (<col=ffffff>${R.s(R.str(int2, 10))}%</col><sprite=14904>) ${R.s(string0)}`;
    }
    return `<col=ffffff>${R.s(R.str(int2, 10))}%</col><sprite=14904> ${R.s(string0)}`;
  };
  V[17720] = { vb: [], vp: [], vc: [], calls: [17721] };
  S[17720] = function (int0, int1, int2, int3, int4) {
    return R.call(17721, [int0, 0, int1, int2, int3, int4]);
  };
  V[17721] = { vb: [], vp: [], vc: [], calls: [17722, 17726, 18567] };
  S[17721] = function (int0, int1, int2, int3, int4, int5) {
    var int6 = R.call(17726, [int0]);
    if (R.eq(int1, 0)) {
      var int1 = R.call(18567, [int0]);
    }
    var string0 = R.call(17722, [int1, 1]);
    if (R.eq(int4, 1)) {
      string0 = `bonus ${R.s(string0)}`;
    }
    if ((R.eq(int5, 1) && (int6 > 0))) {
      return `<col=ffffff>${R.s(R.strLoc(R.scale(int2, 100, int6), 1))}-${R.s(R.strLoc(((R.scale(int2, 100, int6)) + (R.scale(((int3) - (int2) | 0), 100, int6)) | 0), 1))}</col> (<col=ffffff>${R.s(R.str(int2, 10))}%-${R.s(R.str(int3, 10))}%</col><sprite=14904>) ${R.s(string0)}`;
    }
    return `<col=ffffff>${R.s(R.str(int2, 10))}%-${R.s(R.str(int3, 10))}%</col><sprite=14904> ${R.s(string0)}`;
  };
  V[17722] = { vb: [], vp: [], vc: [], calls: [] };
  S[17722] = function (int0, int1) {
    var string0 = "damage";
    var string1 = "";
    switch (R.key(int0)) {
      case 1:
      case 2:
      case 3:
      case 4:
      {
        string0 = "Magic damage";
        string1 = "<col=3366FF>";
        break;
      }
      case 6:
      case 7:
      case 5:
      {
        string0 = "Melee damage";
        string1 = "<col=FFA11A>";
        break;
      }
      case 8:
      case 9:
      case 10:
      {
        string0 = "Ranged damage";
        string1 = "<col=25AD37>";
        break;
      }
      case 37:
      {
        string0 = "Necromancy damage";
        string1 = "<col=A788DD>";
        break;
      }
      case 38:
      {
        string0 = "Necromancy Spirit damage";
        string1 = "<col=40BFC6>";
        break;
      }
      case 15:
      {
        string0 = "Poison damage";
        string1 = "<col=979C11>";
        break;
      }
    }
    if ((R.eq(int1, 1) && (R.len(string1) > 0))) {
      string0 = `${R.s(string1)}${R.s(string0)}</col>`;
    }
    return string0;
  };
  V[17723] = { vb: [], vp: [], vc: [], calls: [] };
  S[17723] = function (int0) {
    return `Damage cap: ${R.s(R.strLoc(int0, 1))}`;
  };
  V[17724] = { vb: [], vp: [], vc: [], calls: [15973] };
  S[17724] = function (int0, int1) {
    if (R.eq(int0, int1)) {
      return `<col=ffffff>Stuns</col> and <col=ffffff>Binds</col> the target for <col=ffffff>${R.s(R.call(15973, [int0, 1]))}</col>`;
    }
    return `<col=ffffff>Stuns</col> the target for <col=ffffff>${R.s(R.call(15973, [int0, 1]))}</col>`;
  };
  V[17725] = { vb: [], vp: [], vc: [], calls: [15973] };
  S[17725] = function (int0) {
    return `<col=ffffff>Binds</col> the target for <col=ffffff>${R.s(R.call(15973, [int0, 1]))}</col>`;
  };
  V[17726] = { vb: [], vp: [3531, 3532], vc: [], calls: [7241] };
  S[17726] = function (int0) {
    if (R.eq(int0, (-1 | 0))) {
      return 0;
    }
    var int1 = R.call(7241, [R.invObj(94, 3)]);
    var int2 = R.call(7241, [R.invObj(94, 5)]);
    var int3 = 0;
    switch (R.key(R.structParam(int0, 2806))) {
      case 1:
      case 2:
      {
        int3 = 1;
        if ((R.eq(R.structParam(int0, 2799), 0) && R.eq(int1, 0))) {
          int3 = 0;
        }
        break;
      }
      case 3:
      {
        int3 = 2;
        break;
      }
      case 4:
      {
        int3 = 3;
        break;
      }
      case 29:
      {
        int3 = 11;
        break;
      }
      case 6:
      case 5:
      {
        if ((!R.eq(int1, 0))) {
          int3 = int1;
        }
        break;
      }
    }
    var int4 = 0;
    if (R.eq(int1, int3)) {
      int4 = R.vp(3531);
      if (R.eq(int2, int3)) {
        int4 = ((int4) + (R.scale(R.vp(3532), 100, 50)) | 0);
      }
    }
    return int4;
  };
  V[17727] = { vb: [], vp: [], vc: [], calls: [4583, 14945, 17709, 17721, 18566] };
  S[17727] = function (int0, int1, int2) {
    var string0 = R.structParam(int0, 2794);
    var string1 = R.structParam(int0, 2795);
    switch (R.key(int0)) {
      case 48335:
      {
        string1 = "Filled with uncontrollable anger.";
        string1 = `${R.s(string1)}<br>- Generates <col=ffffff>${R.s(R.str(1, 10))} Rage</col> ${R.s(R.call(4583, [1, "stack", "stacks"]))} with each attack.`;
        string1 = `${R.s(string1)}<br>- Damage is increased by <col=ffffff>3%</col> for each <col=ffffff>Rage</col> stack.`;
        break;
      }
      case 48336:
      {
        string1 = "Emits a fetid stench.";
        string1 = `${R.s(string1)}<br>- ${R.s(R.call(17721, [48336, 15, 8, ((8) + (4) | 0), 0, int2]))} every <col=ffffff>${R.s(R.call(14945, [3, 1]))}</col> to enemies within ${R.s(R.call(17709, [1]))}.`;
        break;
      }
      case 48337:
      {
        string1 = "Drains the vigour of enemies.";
        string1 = `${R.s(string1)}<br>- Heals you for <col=ffffff>${R.s(R.str(140, 10))}%</col> of the damage dealt.`;
        break;
      }
      case 32349:
      {
        string1 = "Protects against harm.";
        string1 = `${R.s(string1)}<br>- Reduces damage taken from core damage types by up to <col=ffffff>${R.s(R.str(5, 10))}%</col>, capped at <col=ffffff>${R.s(R.call(18566, [31820, 10, int2]))}.`;
        string1 = `${R.s(string1)}<br>- Generates <col=ffffff>${R.s(R.str(1, 10))} Valour</col> ${R.s(R.call(4583, [1, "stack", "stacks"]))} with each incoming hit.`;
        break;
      }
    }
    if ((R.len(string1) > 0)) {
      string0 = `${R.s(string0)}<br>${R.s(string1)}`;
    }
    return string0;
  };
  V[17728] = { vb: [], vp: [], vc: [], calls: [17460] };
  S[17728] = function (int0, int1, string0) {
    var int2 = R.call(17460, [int0]);
    var int3 = 0;
    switch (R.key(int0)) {
      case 48326:
      {
        int3 = 25;
        break;
      }
      case 48327:
      {
        int3 = 50;
        break;
      }
    }
    var string0 = `${R.s(string0)}<br>- Applies a level <col=ffffff>${R.s(R.str(int2, 10))}</col> (<col=ffffff>${R.s(R.str(int3, 10))}%</col> <sprite=30930>) <sprite=30099><nbsp><col=ffffff>${R.s(R.structParam(48340, 2794))}</col> to self.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>Togglable</col>.`;
    return string0;
  };
  V[17729] = { vb: [], vp: [], vc: [], calls: [14945] };
  S[17729] = function (int0) {
    var string0 = "A pact for increased support.";
    var int1 = 0;
    switch (R.key(int0)) {
      case 48352:
      {
        int1 = 10;
        break;
      }
      case 48353:
      {
        int1 = 20;
        break;
      }
      case 48354:
      {
        int1 = 30;
        break;
      }
    }
    string0 = `${R.s(string0)}<br>- Increase the duration of <col=ffffff>conjured spirits</col> by <col=ffffff>${R.s(R.call(14945, [int1, 1]))}</col>.`;
    return string0;
  };
  V[17730] = { vb: [], vp: [], vc: [], calls: [9681, 17709, 17720] };
  S[17730] = function (int0, int1, string0) {
    var int2 = 4;
    var int3 = 6;
    var int4 = R.invObj(94, 3);
    if (((!R.eq(int4, (-1 | 0))) && R.eq(R.itemParam(int4, 8898), 1))) {
      int3 = R.max(int3, R.itemParam(int4, 13));
    }
    if (R.eq(R.call(9681, [7]), 1)) {
      int2 = ((int2) + (2) | 0);
    }
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [48314, 225, ((225) + (50) | 0), 0, int1]))} per hit.`;
    string0 = `${R.s(string0)}<br>- Bounces between enemies within ${R.s(R.call(17709, [int3]))} of each other up to <col=ffffff>${R.s(R.str(int2, 10))}</col> times (disabled in <col=ffffff>PvP</col>).`;
    return string0;
  };
  V[17731] = { vb: [53578, 53579], vp: [], vc: [], calls: [14945, 17709, 17720, 18561] };
  S[17731] = function (int0, int1, int2, string0) {
    var string0 = R.structParam(int0, 2795);
    var int3 = ((R.vb(53578)) + (R.vb(53579)) | 0);
    switch (R.key(int0)) {
      case 48311:
      {
        string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [48311, 72, ((72) + (16) | 0), 0, int1]))} to the target and up to ${R.s(R.call(18561, [9]))} in a <col=ffffff>cone</col> in the attack direction.`;
        string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(25, 10))}%</col> chance to generate <col=ffffff>${R.s(R.str(1, 10))} <sprite=30123><nbsp>${R.s(R.structParam(48334, 2794))}</col> stack with each hit.`;
        if (((!R.eq(int2, 2)) && (int3 > 0))) {
          string0 = `${R.s(string0)}<br>- Can be <col=ffffff>recast</col> within <col=ffffff>${R.s(R.call(14945, [25, 1]))}</col> of the previous cast.`;
          string0 = `${R.s(string0)}<br><br><col=ffffff>Second Cast:</col> ${R.s(R.structParam(48312, 2795))}`;
          if ((int3 > 1)) {
            string0 = `${R.s(string0)}<br><col=ffffff>Third Cast:</col> ${R.s(R.structParam(48313, 2795))}`;
          }
        }
        break;
      }
      case 48312:
      {
        string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [48312, 180, ((180) + (40) | 0), 0, int1]))} to the target and up to ${R.s(R.call(18561, [25]))} within ${R.s(R.call(17709, [2]))} of you.`;
        string0 = `${R.s(string0)}<br>- <col=ffffff>On hit:</col> <col=ffffff>${R.s(R.str(25, 10))}%</col> chance to generate <col=ffffff>${R.s(R.str(1, 10))} <sprite=30123><nbsp>${R.s(R.structParam(48334, 2794))}</col> stack.`;
        if (R.eq(int2, 2)) {
          string0 = `${R.s(string0)}<br><br><col=969696>Note: This is an upgrade to ${R.s(R.structParam(48311, 2794))} and will be available as a second cast.</col>`;
        }
        break;
      }
      case 48313:
      {
        string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [48313, 225, ((225) + (50) | 0), 0, int1]))} to the target and up to ${R.s(R.call(18561, [25]))} within ${R.s(R.call(17709, [2]))} of you.`;
        string0 = `${R.s(string0)}<br>- Damage is increased by <col=ffffff>0%-${R.s(R.str(Math.imul(R.idiv(100, 1), 1), 10))}%</col> based on enemy's missing life points.`;
        if (R.eq(int2, 2)) {
          string0 = `${R.s(string0)}<br><br><col=969696>Note: This is an upgrade to ${R.s(R.structParam(48311, 2794))} and will be available as a third cast.</col>`;
        }
        break;
      }
    }
    return string0;
  };
  V[18287] = { vb: [], vp: [10994, 11006, 11018, 11820], vc: [], calls: [] };
  S[18287] = function (int0) {
    switch (R.key(int0)) {
      case 48302:
      {
        if (R.eq(R.vp(10994), 1)) {
          return 1;
        }
        break;
      }
      case 48304:
      {
        if (R.eq(R.vp(11006), 1)) {
          return 1;
        }
        break;
      }
      case 48306:
      {
        if (R.eq(R.vp(11018), 1)) {
          return 1;
        }
        break;
      }
      case 31820:
      {
        if (R.eq(R.vp(11820), 1)) {
          return 1;
        }
        break;
      }
    }
    return 0;
  };
  V[18289] = { vb: [], vp: [11499, 11500, 11501, 11502], vc: [], calls: [] };
  S[18289] = function () {
    return [R.enumValue(0, 73, 17157, R.vp(11499)), R.enumValue(0, 73, 17157, R.vp(11500)), R.enumValue(0, 73, 17157, R.vp(11501)), R.enumValue(0, 73, 17157, R.vp(11502))];
  };
  V[18295] = { vb: [], vp: [], vc: [], calls: [10903, 17694, 18287, 18289] };
  S[18295] = function (int0, string0) {
    var int1 = R.call(10903, []);
    var string0 = `${R.s(string0)}<br>- Casts up to <col=ffffff>${R.s(R.str(int1, 10))}</col> conjure abilities.`;
    string0 = `${R.s(string0)}<br>- Consumes <col=ffffff>2x</col> <col=ffffff>${R.s(R.itemName(55336))}</col> for each ability.`;
    string0 = `${R.s(string0)}<br><br><col=ffffff>Undead Army`;
    var int2 = (-1 | 0);
    var int3 = (-1 | 0);
    var int4 = (-1 | 0);
    var int5 = (-1 | 0);
    [int2, int3, int4, int5] = R.call(18289, []);
    if ((((R.eq(int2, (-1 | 0)) && R.eq(int3, (-1 | 0))) && R.eq(int4, (-1 | 0))) && R.eq(int5, (-1 | 0)))) {
      return `${R.s(string0)}<br>No abilities selected.</col>`;
    }
    var int6 = (-1 | 0);
    var int7 = 0;
    while (((int7 = (int7 + (1)) | 0) <= int1)) {
      switch (R.key(int7)) {
        case 1:
        {
          int6 = int2;
          break;
        }
        case 2:
        {
          int6 = int3;
          break;
        }
        case 3:
        {
          int6 = int4;
          break;
        }
        case 4:
        {
          int6 = int5;
          break;
        }
      }
      if ((!R.eq(int6, (-1 | 0)))) {
        string0 = `${R.s(string0)}<br>- `;
        switch (R.key(int6)) {
          case 48302:
          {
            string0 = `${R.s(string0)}<sprite=34169><nbsp>`;
            break;
          }
          case 48304:
          {
            string0 = `${R.s(string0)}<sprite=34173><nbsp>`;
            break;
          }
          case 48306:
          {
            string0 = `${R.s(string0)}<sprite=34171><nbsp>`;
            break;
          }
          case 31820:
          {
            string0 = `${R.s(string0)}<sprite=34175><nbsp>`;
            break;
          }
        }
        string0 = `${R.s(string0)}<col=ffffff>${R.s(R.structParam(int6, 2794))}</col>`;
        if (R.eq(R.call(18287, [int6]), 1)) {
          string0 = `${R.s(string0)} (active)`;
        } else {
          if ((R.call(17694, [int6]) > R.clientClock())) {
            string0 = `${R.s(string0)} (unavailable)`;
          }
        }
      }
    }
    return string0;
  };
  V[18309] = { vb: [43188, 43190, 43192, 43194, 43196, 43198, 43200, 43202, 43204, 43206, 43208, 43210, 43212, 43214, 43216, 43218, 43220, 55880, 55883, 55886, 55889, 55892, 55895, 55898, 55901, 55904, 55907, 58113], vp: [11514, 11515, 11516, 11517, 11518, 11519, 11520, 11521, 11522, 11523, 11524, 11525, 11526, 11527, 11528, 11529, 11530, 11809, 12234], vc: [], calls: [12478] };
  S[18309] = function (int0) {
    if (R.eq(int0, (-1 | 0))) {
      R.call(12478, ["Attempting to getvar on a null stone spirit."]);
      return 0;
    }
    if ((!R.eq(R.itemUncert(int0), (-1 | 0)))) {
      var int0 = R.itemUncert(int0);
    }
    switch (R.key(int0)) {
      case 436:
      {
        return R.vb(43188);
      }
      case 438:
      {
        return R.vb(43190);
      }
      case 440:
      {
        return R.vb(43192);
      }
      case 453:
      {
        return R.vb(43194);
      }
      case 442:
      {
        return R.vb(43196);
      }
      case 447:
      {
        return R.vb(43198);
      }
      case 449:
      {
        return R.vb(43200);
      }
      case 44820:
      {
        return R.vb(43202);
      }
      case 444:
      {
        return R.vb(43204);
      }
      case 451:
      {
        return R.vb(43206);
      }
      case 44822:
      {
        return R.vb(43208);
      }
      case 44824:
      {
        return R.vb(43210);
      }
      case 44826:
      {
        return R.vb(43212);
      }
      case 44828:
      {
        return R.vb(43214);
      }
      case 21778:
      {
        return R.vb(43216);
      }
      case 44830:
      {
        return R.vb(43218);
      }
      case 44832:
      {
        return R.vb(43220);
      }
      case 57175:
      {
        return R.vb(55880);
      }
      case 57177:
      {
        return R.vb(55883);
      }
      case 57179:
      {
        return R.vb(55886);
      }
      case 57181:
      {
        return R.vb(55889);
      }
      case 57183:
      {
        return R.vb(55892);
      }
      case 57185:
      {
        return R.vb(55895);
      }
      case 57187:
      {
        return R.vb(55898);
      }
      case 57189:
      {
        return R.vb(55901);
      }
      case 57191:
      {
        return R.vb(55904);
      }
      case 57193:
      {
        return R.vb(55907);
      }
      case 59207:
      {
        return R.vb(58113);
      }
      case 44799:
      {
        return R.vp(11514);
      }
      case 44800:
      {
        return R.vp(11515);
      }
      case 44801:
      {
        return R.vp(11516);
      }
      case 44804:
      {
        return R.vp(11517);
      }
      case 44802:
      {
        return R.vp(11518);
      }
      case 44803:
      {
        return R.vp(11519);
      }
      case 44805:
      {
        return R.vp(11520);
      }
      case 44807:
      {
        return R.vp(11521);
      }
      case 44808:
      {
        return R.vp(11522);
      }
      case 44806:
      {
        return R.vp(11523);
      }
      case 44809:
      {
        return R.vp(11524);
      }
      case 44810:
      {
        return R.vp(11525);
      }
      case 44811:
      {
        return R.vp(11526);
      }
      case 44812:
      {
        return R.vp(11527);
      }
      case 44813:
      {
        return R.vp(11528);
      }
      case 44814:
      {
        return R.vp(11529);
      }
      case 44815:
      {
        return R.vp(11530);
      }
      case 57174:
      {
        return R.vp(11809);
      }
      case 59209:
      {
        return R.vp(12234);
      }
    }
    return 0;
  };
  V[18317] = { vb: [54936, 54937, 54938], vp: [], vc: [], calls: [12477] };
  S[18317] = function () {
    if (((R.call(12477, []) < 7950) || (R.call(12477, []) > 7980))) {
      return 0;
    }
    if (((R.eq(R.vb(54936), 1) && R.eq(R.vb(54937), 1)) && R.eq(R.vb(54938), 1))) {
      return 0;
    }
    return 1;
  };
  V[18318] = { vb: [], vp: [], vc: [], calls: [18400] };
  S[18318] = function () {
    if ((R.eq(R.call(18400, [5038]), 0) && R.eq(R.call(18400, [12990]), 0))) {
      return 0;
    }
    return 1;
  };
  V[18319] = { vb: [], vp: [], vc: [], calls: [11179, 13048, 13727] };
  S[18319] = function () {
    if (R.eq(R.call(13048, []), 1)) {
      return 1;
    }
    if (R.eq(R.call(13727, [45]), 1)) {
      return 0;
    }
    if (R.eq(R.playerMember(), 0)) {
      return 0;
    }
    var int0 = 0;
    var int1 = 0;
    var int2 = 0;
    var int3 = 0;
    [int2, int1, int3, int0] = R.call(11179, []);
    if (R.eq(int0, 1)) {
      return 1;
    }
    if ((((int1) - (int3) | 0) > 788400)) {
      return 1;
    }
    return 0;
  };
  V[18327] = { vb: [], vp: [], vc: [], calls: [] };
  S[18327] = function (int0) {
    if ((R.eq(int0, (-1 | 0)) || R.eq(R.itemParam(int0, 3384), 0))) {
      return 0;
    }
    var int1 = 0;
    var int2 = (-1 | 0);
    var int3 = (-1 | 0);
    while ((int1 <= 7)) {
      switch (R.key(int1)) {
        case 0:
        {
          int2 = int0;
          break;
        }
        case 1:
        {
          int2 = R.itemParam(int0, 4688);
          break;
        }
        case 2:
        {
          int2 = R.itemParam(int0, 4689);
          break;
        }
        case 3:
        {
          int2 = R.itemParam(int0, 4690);
          break;
        }
        case 4:
        {
          int2 = R.itemParam(int0, 5439);
          break;
        }
        case 5:
        {
          int2 = R.itemParam(int0, 6894);
          break;
        }
        case 6:
        {
          int2 = R.itemParam(int0, 8739);
          break;
        }
        case 7:
        {
          int2 = R.itemParam(int0, 8840);
          break;
        }
        default:
        {
          return 0;
        }
      }
      if ((!R.eq(int2, (-1 | 0)))) {
        if ((R.invTotal(94, int2) > 0)) {
          return 1;
        }
        int3 = R.itemParam(int2, 3382);
        if (((!R.eq(int3, (-1 | 0))) && (R.invTotal(94, int3) > 0))) {
          return 1;
        }
        int3 = R.itemParam(int2, 5551);
        if (((!R.eq(int3, (-1 | 0))) && (R.invTotal(94, int3) > 0))) {
          return 1;
        }
        int3 = R.itemParam(int2, 7226);
        if (((!R.eq(int3, (-1 | 0))) && (R.invTotal(94, int3) > 0))) {
          return 1;
        }
      }
      int1 = ((int1) + (1) | 0);
    }
    return 0;
  };
  V[18330] = { vb: [], vp: [], vc: [], calls: [] };
  S[18330] = function (int0, int1, int2, int3, int4, int5, int6) {
    var int7 = 0;
    var int8 = 0;
    var int9 = (-1 | 0);
    var int10 = R.invSize(int0);
    if ((!R.eq(int10, 0))) {
      while ((int8 < int10)) {
        int9 = R.invObj(int0, int8);
        int7 = R.invNum(int0, int8);
        if ((int7 > 0)) {
          R.ifNoop(int3, 5, ((int4 = (int4 + (1)) | 0) - (1)));
          R.ifNoop(36, 32, 0, 0);
          R.ifNoop(int9, int7);
        }
        int8 = ((int8) + (1) | 0);
      }
      int8 = 0;
      int7 = ((int4) - (int5) | 0);
      while ((int5 < int4)) {
        if (R.eq(R.ifNone(int3, ((int5 = (int5 + (1)) | 0) - (1))), 1)) {
          switch (R.key(R.mod(R.min(int7, 4), 4))) {
            case 1:
            {
              R.ifNoop(0, ((int6) + (Math.imul(35, R.idiv(int8, 4))) | 0), 1, 0);
              break;
            }
            case 2:
            {
              switch (R.key(R.mod(int8, 4))) {
                case 0:
                {
                  R.ifNoop(R.idiv(((0) - (36) | 0), 2), ((int6) + (Math.imul(35, R.idiv(int8, 4))) | 0), 1, 0);
                  break;
                }
                case 1:
                {
                  R.ifNoop(R.idiv(36, 2), ((int6) + (Math.imul(35, R.idiv(int8, 4))) | 0), 1, 0);
                  break;
                }
              }
              break;
            }
            case 3:
            {
              switch (R.key(R.mod(int8, 4))) {
                case 0:
                {
                  R.ifNoop(((0) - (36) | 0), ((int6) + (Math.imul(35, R.idiv(int8, 4))) | 0), 1, 0);
                  break;
                }
                case 1:
                {
                  R.ifNoop(0, ((int6) + (Math.imul(35, R.idiv(int8, 4))) | 0), 1, 0);
                  break;
                }
                case 2:
                {
                  R.ifNoop(36, ((int6) + (Math.imul(35, R.idiv(int8, 4))) | 0), 1, 0);
                  break;
                }
              }
              break;
            }
            case 0:
            {
              switch (R.key(R.mod(int8, 4))) {
                case 0:
                {
                  R.ifNoop(R.scale(((0) - (36) | 0), 2, 3), ((int6) + (Math.imul(35, R.idiv(int8, 4))) | 0), 1, 0);
                  break;
                }
                case 1:
                {
                  R.ifNoop(R.idiv(((0) - (36) | 0), 2), ((int6) + (Math.imul(35, R.idiv(int8, 4))) | 0), 1, 0);
                  break;
                }
                case 2:
                {
                  R.ifNoop(R.idiv(36, 2), ((int6) + (Math.imul(35, R.idiv(int8, 4))) | 0), 1, 0);
                  break;
                }
                case 3:
                {
                  R.ifNoop(R.scale(36, 2, 3), ((int6) + (Math.imul(35, R.idiv(int8, 4))) | 0), 1, 0);
                  break;
                }
              }
              break;
            }
          }
          if (R.eq(R.mod(int8, 4), 3)) {
            int7 = R.abs((int7 = (int7 + (-1)) | 0));
          }
          int8 = ((int8) + (1) | 0);
        }
      }
      if ((int8 > 0)) {
        var int2 = ((35) + (Math.imul(35, R.idiv(((int8) - (1) | 0), 4))) | 0);
        var int1 = R.max(int1, ((Math.imul(36, R.min(4, int8))) + (20) | 0));
      } else {
        int2 = 0;
      }
      int7 = ((int7) + (1) | 0);
    }
    return [int7, int1, int2, int4];
  };
  V[18362] = { vb: [], vp: [], vc: [], calls: [] };
  S[18362] = function () {
    return 8678;
  };
  V[18364] = { vb: [], vp: [], vc: [], calls: [] };
  S[18364] = function () {
    return 8727;
  };
  V[18379] = { vb: [], vp: [], vc: [], calls: [17720] };
  S[18379] = function (int0, string0) {
    var int1 = 12;
    var int2 = 4;
    var int3 = 33;
    var int4 = 4;
    var int5 = 8;
    switch (R.key(int0)) {
      case 29051:
      {
        int1 = 8;
        int2 = 4;
        int3 = 24;
        int4 = 4;
        int5 = 12;
        break;
      }
    }
    var string0 = `${R.s(string0)}- Attacks generate <col=ffffff>1</col> <sprite=20228><nbsp><col=ffffff>${R.s(R.structParam(29050, 2794))}</col> stack.`;
    string0 = `${R.s(string0)}<br>- At <col=ffffff>${R.s(R.str(int5, 10))}</col> <sprite=20228><nbsp><col=ffffff>${R.s(R.structParam(29050, 2794))}</col> stacks, launch an additional attack that deals ${R.s(R.call(17720, [49543, int1, ((int1) + (int2) | 0), 0, 1]))} plus <col=ffffff>${R.s(R.str(int3, 10))}%-${R.s(R.str(((int3) + (int4) | 0), 10))}%</col> of the damage from the attack that triggered the effect.`;
    return string0;
  };
  V[18400] = { vb: [54816], vp: [], vc: [], calls: [4148, 11975, 12477] };
  S[18400] = function (int0) {
    if (R.eq(R.call(4148, []), 1)) {
      return 0;
    }
    if (R.eq(R.vb(54816), 1)) {
      return 0;
    }
    if (((R.call(12477, []) < 7950) || (R.call(12477, []) > 7980))) {
      return 0;
    }
    switch (R.key(int0)) {
      case 5038:
      {
        switch (R.key(R.call(11975, []))) {
          case 1:
          case 2:
          case 3:
          {
            return 1;
          }
          case 4:
          case 5:
          case 6:
          {
            return 0;
          }
        }
        break;
      }
      case 12990:
      {
        switch (R.key(R.call(11975, []))) {
          case 4:
          case 5:
          {
            return 1;
          }
          case 1:
          case 2:
          case 3:
          case 6:
          {
            return 0;
          }
        }
        break;
      }
    }
    return 0;
  };
  V[18429] = { vb: [], vp: [], vc: [], calls: [] };
  S[18429] = function (int0) {
    R.dbFind(1122304, int0, 0);
    var int1 = R.dbNext();
    if ((!R.eq(int1, (-1 | 0)))) {
      return R.dbField(int1, 1122320, 0);
    }
    return "";
  };
  V[18431] = { vb: [], vp: [], vc: [], calls: [] };
  S[18431] = function (int0) {
    R.dbFind(1122304, int0, 0);
    var int1 = R.dbNext();
    var string0 = "";
    if (R.eq(int1, (-1 | 0))) {
      return "";
    }
    string0 = R.dbField(int1, 1122352, 0);
    if (R.eq(R.len(string0), 0)) {
      string0 = R.dbField(int1, 1122336, 0);
    }
    string0 = `<sprite=21341>${R.s(string0)}`;
    var string1 = "";
    var int2 = R.dbFieldCount(int1, 1122368);
    var int3 = 0;
    while ((int3 < int2)) {
      string1 = R.dbField(int1, 1122368, int3);
      if ((R.len(string1) > 0)) {
        string0 = `${R.s(string0)}<br><sprite=21341>${R.s(string1)}`;
      }
      int3 = ((int3) + (1) | 0);
    }
    return string0;
  };
  V[18522] = { vb: [], vp: [], vc: [], calls: [2156] };
  S[18522] = function (int0) {
    return R.call(2156, [int0]);
  };
  V[18544] = { vb: [], vp: [], vc: [], calls: [] };
  S[18544] = function () {
    var int0 = R.invObj(94, 3);
    if (R.eq(R.itemParam(int0, 2826), 1)) {
      return R.min(10, R.max(3, ((R.itemParam(int0, 13)) - (1) | 0)));
    }
    return 7;
  };
  V[18546] = { vb: [], vp: [], vc: [], calls: [] };
  S[18546] = function (int0, string0) {
    var int1 = 5;
    if (R.eq(int0, 49559)) {
      int1 = 10;
    }
    var string0 = `${R.s(string0)}- Attacks have a <col=ffffff>${R.s(R.str(int1, 10))}%</col> chance to generate <col=ffffff>${R.s(R.str(1, 10))}</col> <sprite=15190><nbsp><col=ffffff>${R.s(R.structParam(49562, 2794))}</col> stack with each hit.`;
    return string0;
  };
  V[18547] = { vb: [], vp: [], vc: [], calls: [15973, 17708] };
  S[18547] = function (int0, string0) {
    var int1 = 1;
    var int2 = 10;
    if (R.eq(int0, 49558)) {
      int1 = 2;
      int2 = 15;
    }
    var string0 = `${R.s(string0)}- Attacks have a <col=ffffff>${R.s(R.str(int1, 10))}%</col> chance to generate <col=ffffff>${R.s(R.str(1, 10))}</col> <sprite=15190><nbsp><col=ffffff>${R.s(R.structParam(49562, 2794))}</col> stack with each hit.`;
    string0 = `${R.s(string0)}<br>- Applying a <sprite=15190><nbsp><col=ffffff>${R.s(R.structParam(49562, 2794))}</col> stack grants <sprite=15184><nbsp><col=ffffff>${R.s(R.structParam(49563, 2794))}</col> for <col=ffffff>${R.s(R.call(15973, [int2, 1]))}</col>.`;
    string0 = R.call(17708, [49563, string0]);
    return string0;
  };
  V[18552] = { vb: [], vp: [], vc: [], calls: [11669] };
  S[18552] = function (int0) {
    var string0 = "";
    var int1 = 94;
    var int2 = R.invObj(94, int0);
    if ((!R.eq(int2, (-1 | 0)))) {
      switch (R.key(int0)) {
        case 0:
        {
          string0 = "<sprite=24920>";
          break;
        }
        case 1:
        {
          string0 = "<sprite=24921>";
          break;
        }
        case 2:
        {
          string0 = "<sprite=24922>";
          break;
        }
        case 3:
        {
          string0 = "<sprite=24923>";
          break;
        }
        case 4:
        {
          string0 = "<sprite=25170>";
          break;
        }
        case 5:
        {
          string0 = "<sprite=25171>";
          break;
        }
        case 7:
        {
          string0 = "<sprite=25172>";
          break;
        }
        case 9:
        {
          string0 = "<sprite=25173>";
          break;
        }
        case 10:
        {
          string0 = "<sprite=25383>";
          break;
        }
        case 12:
        {
          string0 = "<sprite=25169>";
          break;
        }
        case 13:
        {
          string0 = "<sprite=25384>";
          break;
        }
        case 14:
        {
          string0 = "<sprite=25385>";
          break;
        }
        case 17:
        {
          string0 = "<sprite=25386>";
          break;
        }
      }
      string0 = `${R.s(string0)}<nbsp><col=ffffff>${R.s(R.call(11669, [int1, int0]))}:</col> ${R.s(R.itemName(int2))}`;
    }
    return string0;
  };
  V[18553] = { vb: [], vp: [], vc: [], calls: [] };
  S[18553] = function (int0, int1) {
    var string0 = "";
    if ((!R.eq(int0, (-1 | 0)))) {
      string0 = `<col=ffffff>Invention perk:</col> ${R.s(R.dbField(int0, 32784, 0))}`;
      if ((int1 >= 1)) {
        string0 = `${R.s(string0)} (rank ${R.s(R.str(int1, 10))})`;
      }
    }
    return string0;
  };
  V[18554] = { vb: [], vp: [], vc: [], calls: [] };
  S[18554] = function (int0) {
    return `<col=ffffff>${R.s(R.str(int0, 10))}x${R.s(R.str(int0, 10))}</col><nbsp>area</col>`;
  };
  V[18555] = { vb: [], vp: [], vc: [], calls: [7653] };
  S[18555] = function (int0) {
    return `Generates <col=ffffff>${R.s(R.call(7653, [int0, 1, 1, 0, 1]))}%</col> <sprite=14907><nbsp><col=DEAC18>Adrenaline</col>`;
  };
  V[18556] = { vb: [], vp: [], vc: [], calls: [7653] };
  S[18556] = function (int0) {
    return `Consumes up to <col=ffffff>${R.s(R.call(7653, [int0, 1, 1, 0, 1]))}%</col> <sprite=14907><nbsp><col=DEAC18>Adrenaline</col>`;
  };
  V[18557] = { vb: [], vp: [], vc: [], calls: [7653] };
  S[18557] = function (int0) {
    return `Consumes <col=ffffff>${R.s(R.call(7653, [int0, 1, 1, 0, 1]))}%</col> <sprite=14907><nbsp><col=DEAC18>Adrenaline</col>`;
  };
  V[18558] = { vb: [], vp: [], vc: [], calls: [7653] };
  S[18558] = function (int0) {
    return `Generates an additional <col=ffffff>${R.s(R.call(7653, [int0, 1, 1, 0, 1]))}%</col> <sprite=14907><nbsp><col=DEAC18>Adrenaline</col>`;
  };
  V[18559] = { vb: [], vp: [], vc: [], calls: [7653, 15973] };
  S[18559] = function (int0, int1) {
    return `<col=ffffff>Critical Strikes</col> generate an additional <col=ffffff>${R.s(R.call(7653, [int0, 1, 1, 0, 1]))}%</col> <sprite=14907><nbsp><col=DEAC18>Adrenaline</col> for <col=ffffff>${R.s(R.call(15973, [int1, 1]))}</col>`;
  };
  V[18560] = { vb: [], vp: [], vc: [], calls: [4583] };
  S[18560] = function (int0) {
    return `<col=ffffff>${R.s(R.str(int0, 10))}</col> ${R.s(R.call(4583, [int0, "enemy", "enemies"]))}`;
  };
  V[18561] = { vb: [], vp: [], vc: [], calls: [4583] };
  S[18561] = function (int0) {
    return `<col=ffffff>${R.s(R.str(int0, 10))}</col> additional ${R.s(R.call(4583, [int0, "enemy", "enemies"]))}`;
  };
  V[18562] = { vb: [], vp: [], vc: [], calls: [7653] };
  S[18562] = function (int0) {
    return `<col=ffffff>${R.s(R.call(7653, [int0, 2, 2, 1, 1]))}x</col>`;
  };
  V[18563] = { vb: [], vp: [], vc: [], calls: [15973] };
  S[18563] = function (int0, int1) {
    return `<col=ffffff>${R.s(R.call(15973, [((Math.imul(int0, int1)) + (1) | 0), 1]))}`;
  };
  V[18566] = { vb: [], vp: [], vc: [], calls: [17726] };
  S[18566] = function (int0, int1, int2) {
    var int3 = R.call(17726, [int0]);
    if ((R.eq(int2, 1) && (int3 > 0))) {
      return `<col=ffffff>${R.s(R.strLoc(R.scale(int1, 100, int3), 1))}</col> (<col=ffffff>${R.s(R.str(int1, 10))}%</col><sprite=14904>)`;
    }
    return `<col=ffffff>${R.s(R.str(int1, 10))}%</col><sprite=14904>`;
  };
  V[18567] = { vb: [], vp: [], vc: [], calls: [] };
  S[18567] = function (int0) {
    if ((!R.eq(int0, (-1 | 0)))) {
      switch (R.key(R.structParam(int0, 2806))) {
        case 1:
        case 2:
        {
          return 6;
        }
        case 3:
        {
          return 8;
        }
        case 4:
        {
          return 1;
        }
        case 29:
        {
          return 37;
        }
      }
    }
    return 0;
  };
  V[18569] = { vb: [], vp: [], vc: [], calls: [] };
  S[18569] = function (int0) {
    return `Maximum charges: ${R.s(R.strLoc(int0, 1))}`;
  };
  V[18571] = { vb: [], vp: [], vc: [], calls: [17709] };
  S[18571] = function (int0) {
    return `<col=ffffff>Knocks-back</col> the target by ${R.s(R.call(17709, [int0]))}`;
  };
  V[18573] = { vb: [], vp: [], vc: [], calls: [15973] };
  S[18573] = function (int0, int1) {
    if ((int1 > 0)) {
      return `Reduces the target's damage by <col=ffffff>${R.s(R.str(int0, 10))}%</col> for <col=ffffff>${R.s(R.call(15973, [int1, 1]))}</col>`;
    }
    return `Reduces the target's damage by <col=ffffff>${R.s(R.str(int0, 10))}%</col>`;
  };
  V[18574] = { vb: [], vp: [], vc: [], calls: [15973] };
  S[18574] = function (int0, int1) {
    if ((int1 > 0)) {
      return `Reduces the target's <col=ffffff>Hit Chance</col> by <col=ffffff>${R.s(R.str(int0, 10))}%</col> for <col=ffffff>${R.s(R.call(15973, [int1, 1]))}</col>`;
    }
    return `Reduces the target's <col=ffffff>Hit Chance</col> by <col=ffffff>${R.s(R.str(int0, 10))}%</col>`;
  };
  V[18575] = { vb: [], vp: [], vc: [], calls: [15973] };
  S[18575] = function (int0) {
    if ((int0 > 0)) {
      return `Disables the target's <col=ffffff>Protection Prayers</col> for <col=ffffff>${R.s(R.call(15973, [int0, 1]))}</col>`;
    }
    return "Disables the target's <col=ffffff>Protection Prayers</col>";
  };
  V[18576] = { vb: [], vp: [], vc: [], calls: [] };
  S[18576] = function (int0, int1, int2) {
    var string0 = "";
    switch (R.key(int0)) {
      case 3:
      {
        if (R.eq(int1, 1)) {
          string0 = "<sprite=203><nbsp>";
        }
        string0 = `${R.s(string0)}<col=ffffff>Constitution</col>`;
        break;
      }
      case 1:
      {
        if (R.eq(int1, 1)) {
          string0 = "<sprite=199><nbsp>";
        }
        string0 = `${R.s(string0)}<col=ffffff>Defence</col>`;
        break;
      }
      case 0:
      {
        if (R.eq(int1, 1)) {
          string0 = "<sprite=197><nbsp>";
        }
        string0 = `${R.s(string0)}<col=ffffff>Attack</col>`;
        break;
      }
      case 2:
      {
        if (R.eq(int1, 1)) {
          string0 = "<sprite=198><nbsp>";
        }
        string0 = `${R.s(string0)}<col=ffffff>Strength</col>`;
        break;
      }
      case 6:
      {
        if (R.eq(int1, 1)) {
          string0 = "<sprite=202><nbsp>";
        }
        string0 = `${R.s(string0)}<col=ffffff>Magic</col>`;
        break;
      }
      case 4:
      {
        if (R.eq(int1, 1)) {
          string0 = "<sprite=200><nbsp>";
        }
        string0 = `${R.s(string0)}<col=ffffff>Ranged</col>`;
        break;
      }
      case 28:
      {
        if (R.eq(int1, 1)) {
          string0 = "<sprite=30510><nbsp>";
        }
        string0 = `${R.s(string0)}<col=ffffff>Necromancy</col>`;
        break;
      }
      case 5:
      {
        if (R.eq(int1, 1)) {
          string0 = "<sprite=201><nbsp>";
        }
        string0 = `${R.s(string0)}<col=ffffff>Prayer</col>`;
        break;
      }
      case 18:
      {
        if (R.eq(int1, 1)) {
          string0 = "<sprite=198><nbsp>";
        }
        string0 = `${R.s(string0)}<col=ffffff>Slayer</col>`;
        break;
      }
      case 10:
      {
        if (R.eq(int1, 1)) {
          string0 = "<sprite=211><nbsp>";
        }
        string0 = `${R.s(string0)}<col=ffffff>Fishing</col>`;
        break;
      }
    }
    if (R.eq(int2, 1)) {
      string0 = `${R.s(string0)} stat`;
    }
    return string0;
  };
  V[18577] = { vb: [], vp: [], vc: [], calls: [15973, 18576] };
  S[18577] = function (int0, int1) {
    if ((int1 > 0)) {
      return `Reduces the ${R.s(R.call(18576, [1, 1, 1]))} of all enemies hit by <col=ffffff>${R.s(R.str(int0, 10))}%</col> for <col=ffffff>${R.s(R.call(15973, [int1, 1]))}</col>`;
    }
    return `Reduces the ${R.s(R.call(18576, [1, 1, 1]))} of all enemies hit by <col=ffffff>${R.s(R.str(int0, 10))}%</col>`;
  };
  V[18585] = { vb: [], vp: [], vc: [], calls: [] };
  S[18585] = function (int0, int1) {
    if ((R.eq(int0, 1) && R.eq(int1, 1))) {
      return "Can be cast during the global cooldown but will not generate adrenaline or deal damage";
    }
    if (R.eq(int0, 1)) {
      return "Can be cast during the global cooldown but will not generate adrenaline";
    }
    if (R.eq(int1, 1)) {
      return "Can be cast during the global cooldown but will not deal damage";
    }
    return "Can be cast during the global cooldown";
  };
  V[18586] = { vb: [], vp: [], vc: [], calls: [] };
  S[18586] = function (int0) {
    return `Damage is ${R.s(R.str(int0, 10))}% effective in PvP`;
  };
  V[18587] = { vb: [], vp: [], vc: [], calls: [2916, 14945, 18556] };
  S[18587] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- Heals you for <col=ffffff>0-${R.s(R.strLoc(R.scale(2, 100, R.call(2916, [])), 1))}</col> (<col=ffffff>0%-${R.s(R.str(2, 10))}%</col> <sprite=18851><nbsp><col=ED705A>Maximum<nbsp>Life<nbsp>Points</col>) every <col=ffffff>${R.s(R.call(14945, [1, 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(18556, [100]))} every <col=ffffff>${R.s(R.call(14945, [1, 1]))}</col>.`;
    return string0;
  };
  V[18588] = { vb: [], vp: [], vc: [], calls: [] };
  S[18588] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- Increases the likelihood that your attacks will cause the target to attack you.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>Togglable</col>.`;
    return string0;
  };
  V[18589] = { vb: [30341], vp: [], vc: [], calls: [2916, 7653, 14945, 17710] };
  S[18589] = function (int0, int1, string0) {
    var int2 = Math.imul(8, R.idiv(16, 3));
    if ((R.vb(30341) > 0)) {
      int2 = ((int2) - (Math.imul(R.vb(30341), 1)) | 0);
    }
    int2 = R.idiv(Math.imul(int2, 10), R.idiv(16, 3));
    var string0 = `${R.s(string0)}<br>- Heals you for <col=ffffff>${R.s(R.strLoc(R.scale(int2, 1000, R.call(2916, [])), 1))}</col> (<col=ffffff>${R.s(R.call(7653, [int2, 1, 1, 0, 1]))}%</col> <sprite=18851><nbsp><col=ED705A>Maximum<nbsp>Life<nbsp>Points</col>) every <col=ffffff>${R.s(R.call(14945, [3, 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [16]))}.`;
    return string0;
  };
  V[18590] = { vb: [], vp: [], vc: [], calls: [17720, 18575] };
  S[18590] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 65, ((65) + (10) | 0), 0, int1]))}.`;
    string0 = `${R.s(string0)}<br>- Heals you for <col=ffffff>${R.s(R.str(25, 10))}%</col> (<col=ffffff>On killing blow:</col> <col=ffffff>${R.s(R.str(100, 10))}%</col>) of the damage dealt.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>PvP:</col> ${R.s(R.call(18575, [8]))}.`;
    return string0;
  };
  V[18591] = { vb: [], vp: [], vc: [], calls: [15973] };
  S[18591] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- Incapacitates you granting immunity to all damage.`;
    string0 = `${R.s(string0)}<br>- After <col=ffffff>${R.s(R.call(15973, [10, 1]))}</col>, heal for <col=ffffff>250%</col> of the damage mitigated and gain immunity to <col=ffffff>Stuns</col> and <col=ffffff>Binds</col> for <col=ffffff>${R.s(R.call(15973, [25, 1]))}</col>.`;
    return string0;
  };
  V[18592] = { vb: [], vp: [], vc: [], calls: [2916, 14945, 17709, 17710, 18554] };
  S[18592] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- Creates a ${R.s(R.call(18554, [1]))} at your location.`;
    string0 = `${R.s(string0)}<br>- Heals anyone within ${R.s(R.call(17709, [7]))} of the area for <col=ffffff>0%-${R.s(R.str(7, 10))}%</col> of their <sprite=18851><nbsp><col=ED705A>Maximum<nbsp>Life<nbsp>Points</col> every <col=ffffff>${R.s(R.call(14945, [6, 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.strLoc(R.scale(300, 100, R.call(2916, [])), 1))}</col> (<col=ffffff>${R.s(R.str(300, 10))}%</col> <sprite=18851><nbsp><col=ED705A>Maximum<nbsp>Life<nbsp>Points</col>) heal capacity.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [36]))}.`;
    return string0;
  };
  V[18593] = { vb: [], vp: [], vc: [], calls: [14945] };
  S[18593] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- Steals <col=ffffff>${R.s(R.str(R.idiv(100, 10), 10))}%</col> <sprite=14907><nbsp><col=DEAC18>Adrenaline</col> from the target.`;
    string0 = `${R.s(string0)}<br>- Grants immunity to <sprite=18764><nbsp>${R.s(R.structParam(28429, 2794))} for <col=ffffff>${R.s(R.call(14945, [10, 1]))}</col>.`;
    return string0;
  };
  V[18594] = { vb: [], vp: [], vc: [], calls: [17720] };
  S[18594] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 75, ((75) + (10) | 0), 0, int1]))}.`;
    string0 = `${R.s(string0)}<br>- If the target is your slayer assignment the attack is empowered and deals <col=ffffff>${R.s(R.strLoc(R.scale(R.stat(18), 100, 10000), 1))}</col> (<col=ffffff>${R.s(R.strLoc(10000, 1))}%</col><sprite=216>) damage.`;
    return string0;
  };
  V[18595] = { vb: [], vp: [], vc: [], calls: [17721] };
  S[18595] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- Applies <col=ffffff>${R.s(R.str(1, 10))} <sprite=19227><nbsp>${R.s(R.structParam(14675, 2794))}</col> stack to the target for ${R.s(R.call(17721, [int0, (-1 | 0), 80, ((80) + (10) | 0), 0, 1]))}.`;
    return string0;
  };
  V[18596] = { vb: [], vp: [], vc: [], calls: [] };
  S[18596] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- Consumes all <col=ffffff><sprite=19227><nbsp>${R.s(R.structParam(14675, 2794))}</col> stacks on the target and deals <col=ffffff>100%</col> of the total damage applied.`;
    return string0;
  };
  V[18597] = { vb: [], vp: [], vc: [], calls: [15973, 17720, 18556] };
  S[18597] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 100, ((100) + (20) | 0), 0, int1]))} per hit every <col=ffffff>${R.s(R.call(15973, [2, 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(((25) + (1) | 0), 10))}</col> hits.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>Channeled</col>.`;
    string0 = `${R.s(string0)}<br>- Deals an additional ${R.s(R.call(17720, [int0, 18, ((18) + (4) | 0), 0, int1]))} with each hit.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(18556, [250]))} per hit.`;
    string0 = `${R.s(string0)}<br>- If you run out of <sprite=14907><nbsp><col=DEAC18>Adrenaline</col>, <col=ffffff>${R.s(R.str(25, 10))}%</col> of the damage dealt will be dealt to self per hit.`;
    string0 = `${R.s(string0)}<br>- After <col=ffffff>${R.s(R.str(10, 10))}</col> hits, an additional <col=ffffff>${R.s(R.strLoc(1000, 1))}</col> damage is dealt to self per hit.`;
    return string0;
  };
  V[18598] = { vb: [], vp: [], vc: [], calls: [15973] };
  S[18598] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- Stores <col=ffffff>${R.s(R.str(100, 10))}%</col> of damage taken over <col=ffffff>${R.s(R.call(15973, [10, 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>- After the duration, deals <col=ffffff>${R.s(R.str(100, 10))}%</col> of the damage stored.`;
    string0 = `${R.s(string0)}<br>- Can be <col=ffffff>recast</col> within its duration.`;
    return string0;
  };
  V[18599] = { vb: [], vp: [], vc: [], calls: [7653, 17710] };
  S[18599] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- Your next ability or spell will have <col=ffffff>${R.s(R.call(7653, [1000, 1, 1, 0, 1]))}%</col> <col=ffffff>Hit Chance</col>.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [10]))}.`;
    return string0;
  };
  V[18600] = { vb: [], vp: [], vc: [], calls: [17710] };
  S[18600] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- Increases your damage against demons by <col=ffffff>${R.s(R.str(15, 10))}%</col>.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [17]))}.`;
    return string0;
  };
  V[18601] = { vb: [], vp: [], vc: [], calls: [17710] };
  S[18601] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- Increases your damage against dragons by <col=ffffff>${R.s(R.str(15, 10))}%</col>.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [17]))}.`;
    return string0;
  };
  V[18602] = { vb: [], vp: [], vc: [], calls: [17710] };
  S[18602] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- Increases your damage against undead by <col=ffffff>${R.s(R.str(15, 10))}%</col>.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [17]))}.`;
    return string0;
  };
  V[18603] = { vb: [], vp: [], vc: [], calls: [7653, 17710] };
  S[18603] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- <col=ffffff>Threshold</col> abilities no longer require <col=ffffff>${R.s(R.call(7653, [500, 1, 1, 0, 1]))}%</col> <sprite=14907><nbsp><col=DEAC18>Adrenaline</col> to cast.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [10]))}.`;
    return string0;
  };
  V[18604] = { vb: [], vp: [], vc: [], calls: [17710] };
  S[18604] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- Increases your <sprite=216><nbsp><col=ffffff>Slayer XP</col> gained by <col=ffffff>${R.s(R.str(10, 10))}%</col>.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [50]))}.`;
    return string0;
  };
  V[18605] = { vb: [], vp: [], vc: [], calls: [17710] };
  S[18605] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- Enemies killed in <col=ffffff>Kuradal's Dungeon</col> will instantly respawn.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [50]))}.`;
    return string0;
  };
  V[18606] = { vb: [], vp: [], vc: [], calls: [17710] };
  S[18606] = function (int0, int1, string0) {
    switch (R.key(int0)) {
      default:
      {
      }
    }
    var string0 = `${R.s(string0)}<br>- Applies <sprite=23725><nbsp><col=ffffff>${R.s(R.structParam(52778, 2794))}</col> to self.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [25]))}.`;
    return string0;
  };
  V[18607] = { vb: [60464], vp: [], vc: [], calls: [17720, 17724, 18571] };
  S[18607] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 65, ((65) + (10) | 0), 0, int1]))}.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17724, [3, 3]))}.`;
    if (R.eq(R.vb(60464), 1)) {
      string0 = `${R.s(string0)}<br>- ${R.s(R.call(18571, [1]))}.`;
    }
    return string0;
  };
  V[18608] = { vb: [], vp: [], vc: [], calls: [17720, 17724] };
  S[18608] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 120, ((120) + (20) | 0), 0, int1]))}.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17724, [6, 6]))}.`;
    return string0;
  };
  V[18609] = { vb: [30980], vp: [], vc: [], calls: [15973, 17707, 17709, 17720, 18561] };
  S[18609] = function (int0, int1, string0) {
    var int2 = 2;
    var int3 = 70;
    var int4 = 20;
    var int5 = 30;
    var int6 = 5;
    if (R.eq(int0, 45046)) {
      int2 = 6;
      int3 = 80;
      int4 = 20;
      int5 = 50;
      int6 = 5;
    }
    if ((R.vb(30980) > 0)) {
      int5 = ((int5) + (((5) + (Math.imul(5, R.vb(30980))) | 0)) | 0);
    }
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int3, ((int3) + (int4) | 0), 0, int1]))} to the target and up to ${R.s(R.call(18561, [int2]))} within ${R.s(R.call(17709, [int6]))} of the target.`;
    string0 = `${R.s(string0)}<br>- Your next <col=ffffff>${R.s(R.call(17707, [1, 1]))}</col> <col=3366FF>Magic ability</col> against the target within <col=ffffff>${R.s(R.call(15973, [10, 1]))}</col> will also target the additional enemies, dealing <col=ffffff>${R.s(R.str(int5, 10))}%</col> damage.`;
    return string0;
  };
  V[18610] = { vb: [30982], vp: [], vc: [], calls: [15973, 17720] };
  S[18610] = function (int0, int1, string0) {
    var int2 = 27;
    var int3 = 6;
    if ((R.vb(30982) > 0)) {
      int2 = ((int2) + (R.scale(int2, 100, ((10) + (Math.imul(R.vb(30982), 3)) | 0))) | 0);
      int3 = ((int3) + (R.scale(int3, 100, ((10) + (Math.imul(R.vb(30982), 3)) | 0))) | 0);
    }
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int2, ((int2) + (int3) | 0), 0, int1]))} per hit every <col=ffffff>${R.s(R.call(15973, [3, 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(10, 10))}</col> hits.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>Damage over time</col>.`;
    return string0;
  };
  V[18611] = { vb: [], vp: [], vc: [], calls: [17717, 17720, 18561] };
  S[18611] = function (int0, int1, string0) {
    var int2 = 110;
    var int3 = 20;
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int2, ((int2) + (int3) | 0), 0, int1]))} to the target and up to ${R.s(R.call(18561, [4]))} in a <col=ffffff>cone</col> in the attack direction.`;
    string0 = `${R.s(string0)}<br>- Deals ${R.s(R.call(17717, [((100) + (25) | 0)]))} to <sprite=23401><nbsp><col=ffffff>${R.s(R.structParam(52780, 2794))}</col> enemies.`;
    return string0;
  };
  V[18612] = { vb: [58286], vp: [], vc: [], calls: [15973, 17720, 17724] };
  S[18612] = function (int0, int1, string0) {
    var int2 = 2;
    var int3 = 3;
    var int4 = 120;
    var int5 = 20;
    var int6 = 6;
    if ((R.vb(58286) >= 4)) {
      int2 = 1;
      int3 = 7;
      int4 = R.scale(int4, 100, ((100) - (40) | 0));
      int5 = R.scale(int5, 100, ((100) - (40) | 0));
    }
    if ((R.vb(58286) >= 5)) {
      int6 = 15;
    }
    var string0 = `${R.s(string0)}<br>- Attack <col=ffffff>${R.s(R.str(((int3) + (1) | 0), 10))}</col> times over <col=ffffff>${R.s(R.call(15973, [((Math.imul(int2, int3)) + (1) | 0), 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int4, ((int4) + (int5) | 0), 0, int1]))} per hit.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>Channelled</col>.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17724, [6, 6]))}.`;
    string0 = `${R.s(string0)}<br>- Completing the channel applies <sprite=17606><nbsp><col=ffffff>${R.s(R.structParam(45563, 2794))}</col> to self for <col=ffffff>${R.s(R.call(15973, [int6, 1]))}</col>.`;
    return string0;
  };
  V[18613] = { vb: [], vp: [], vc: [], calls: [17720] };
  S[18613] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 125, ((125) + (30) | 0), 0, int1]))} per hit.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(2, 10))}</col> hits.`;
    string0 = `${R.s(string0)}<br>- Each hit has <col=ffffff>+${R.s(R.str(R.idiv(100, 10), 10))}% Critical Strike Chance</col> and <col=ffffff>+${R.s(R.str(R.idiv(200, 10), 10))}% Critical Strike Damage</col>.`;
    return string0;
  };
  V[18614] = { vb: [], vp: [], vc: [], calls: [17720, 17724, 18571] };
  S[18614] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 65, ((65) + (10) | 0), 0, int1]))}.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17724, [2, 2]))}.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(18571, [1]))}.`;
    return string0;
  };
  V[18615] = { vb: [], vp: [], vc: [], calls: [17720, 17724, 18571] };
  S[18615] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 120, ((120) + (20) | 0), 0, int1]))}.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17724, [6, 6]))}.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(18571, [1]))}.`;
    return string0;
  };
  V[18616] = { vb: [], vp: [], vc: [], calls: [17709, 17720, 18559, 18561] };
  S[18616] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 225, ((225) + (50) | 0), 0, int1]))} to the target and up to ${R.s(R.call(18561, [8]))} within ${R.s(R.call(17709, [4]))} in the attack direction.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(18559, [80, 50]))}.`;
    return string0;
  };
  V[18617] = { vb: [], vp: [], vc: [], calls: [9681, 17720] };
  S[18617] = function (int0, int1, string0) {
    var int2 = 420;
    var int3 = 80;
    var int4 = 1;
    if (R.eq(R.call(9681, [3]), 1)) {
      int2 = 120;
      int3 = 30;
      int4 = 4;
    }
    if ((int4 > 1)) {
      var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int2, ((int2) + (int3) | 0), 0, int1]))} with each hit.`;
      string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(int4, 10))}</col> hits.`;
    } else {
      string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int2, ((int2) + (int3) | 0), 0, int1]))}.`;
    }
    return string0;
  };
  V[18618] = { vb: [30983, 58286], vp: [], vc: [], calls: [7653, 15973, 17444, 17710, 17717, 17720, 18554] };
  S[18618] = function (int0, int1, string0) {
    var int2 = R.call(17444, [int0]);
    if ((R.eq(int0, 19254) && (R.vb(30983) > 0))) {
      int2 = ((int2) + (R.scale(int2, 100, 26)) | 0);
    }
    var string0 = `${R.s(string0)}<br>- Creates a ${R.s(R.call(18554, [7]))} at your location.`;
    if ((R.vb(58286) >= 3)) {
      string0 = `${R.s(string0)}<br>- <col=3366FF>Magic attacks</col> deal ${R.s(R.call(17717, [150]))} and gain <col=ffffff>${R.s(R.call(7653, [Math.imul(R.vb(58286), 15), 1, 1, 0, 1]))}% Critical Strike Chance</col> while inside the area.`;
    } else {
      string0 = `${R.s(string0)}<br>- <col=3366FF>Magic attacks</col> deal ${R.s(R.call(17717, [150]))} while inside the area.`;
    }
    if (((!R.eq(int0, 19254)) || (R.vb(30983) <= 0))) {
      string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 10, ((10) + (10) | 0), 0, int1]))} per hit every <col=ffffff>${R.s(R.call(15973, [3, 1]))}</col> to the target while it is inside the area.`;
    }
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [int2]))}.`;
    return string0;
  };
  V[18619] = { vb: [], vp: [], vc: [], calls: [15973, 17709, 17720] };
  S[18619] = function (int0, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [31985, 90, ((90) + (20) | 0), 0, int0]))} per hit every <col=ffffff>${R.s(R.call(15973, [2, 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(5, 10))}</col> hits.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>Damage over time</col>.`;
    string0 = `${R.s(string0)}<br>- Damage is reduced by <col=ffffff>${R.s(R.str(20, 10))}%</col> of initial damage with each hit and will spread to enemies within ${R.s(R.call(17709, [2]))}.`;
    return string0;
  };
  V[18620] = { vb: [], vp: [], vc: [], calls: [17444, 17710, 17717, 18562] };
  S[18620] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- <col=3366FF>Magic attacks</col> deal ${R.s(R.call(17717, [166]))} (<col=ffffff>PvP</col>: ${R.s(R.call(18562, [133]))}).`;
    string0 = `${R.s(string0)}<br>- Combat spells consume no runes.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [R.call(17444, [int0])]))}.`;
    return string0;
  };
  V[18621] = { vb: [], vp: [], vc: [], calls: [17709] };
  S[18621] = function (int0, string0) {
    var string0 = `${R.s(string0)}<br>- Move forwards ${R.s(R.call(17709, [10]))}.`;
    return string0;
  };
  V[18622] = { vb: [], vp: [], vc: [], calls: [15973, 17720] };
  S[18622] = function (int0, int1, string0) {
    var int2 = 0;
    var int3 = 0;
    var int4 = 1;
    var int5 = 2;
    var int6 = 5;
    switch (R.key(int0)) {
      case 19343:
      {
        int2 = 30;
        int3 = 10;
        break;
      }
      case 45450:
      {
        int2 = 40;
        int3 = 10;
        int6 = 7;
        break;
      }
    }
    var string0 = `${R.s(string0)}<br>- Attack <col=ffffff>${R.s(R.str(((int5) + (1) | 0), 10))}</col> times over <col=ffffff>${R.s(R.call(15973, [((Math.imul(int4, int5)) + (1) | 0), 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int2, ((int2) + (int3) | 0), 0, int1]))} per hit.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>Channelled</col>.`;
    string0 = `${R.s(string0)}<br>- Each attack increases the <col=ffffff>Critical Strike Chance</col> of your next <col=3366FF>Magic attack</col> by <col=ffffff>${R.s(R.str(int6, 10))}%</col>, stacking to a maximum of <col=ffffff>${R.s(R.str(Math.imul(int6, ((int5) + (1) | 0)), 10))}%</col>.`;
    return string0;
  };
  V[18623] = { vb: [], vp: [], vc: [], calls: [17710, 17720] };
  S[18623] = function (int0, int1, string0) {
    var int2 = 0;
    var int3 = 0;
    var int4 = 0;
    var int5 = 0;
    var int6 = (-1 | 0);
    switch (R.key(int0)) {
      case 19342:
      {
        int2 = 90;
        int3 = 20;
        int5 = 15;
        int6 = 46308;
        break;
      }
      case 47221:
      {
        int2 = 115;
        int3 = 20;
        int5 = 15;
        int6 = 46309;
        break;
      }
    }
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int2, ((int2) + (int3) | 0), 0, int1]))}.`;
    string0 = `${R.s(string0)}<br>- Applies <col=ffffff> <sprite=27430><nbsp>${R.s(R.structParam(int6, 2794))}</col> to self.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [int5]))}.`;
    return string0;
  };
  V[18624] = { vb: [], vp: [], vc: [], calls: [] };
  S[18624] = function (int0, string0) {
    var int1 = 100;
    if (R.eq(int0, 46309)) {
      int1 = 200;
    }
    var string0 = `<br>- Your next <col=3366FF>Magic ability</col> costs <col=ffffff>${R.s(R.str(R.idiv(int1, 10), 10))}%</col> less <sprite=14907><nbsp><col=DEAC18>Adrenaline</col>.`;
    return string0;
  };
  V[18625] = { vb: [], vp: [], vc: [], calls: [15973, 17720, 17721] };
  S[18625] = function (int0, int1, string0) {
    var int2 = 55;
    var int3 = 10;
    var int4 = 2;
    var int5 = 3;
    var string0 = `${R.s(string0)}<br>- Attack <col=ffffff>${R.s(R.str(((int5) + (1) | 0), 10))}</col> times over <col=ffffff>${R.s(R.call(15973, [((Math.imul(int4, int5)) + (1) | 0), 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int2, ((int2) + (int3) | 0), 0, int1]))} per hit.`;
    string0 = `${R.s(string0)}<br>- Deals an additional ${R.s(R.call(17721, [int0, (-1 | 0), 10, ((10) + (5) | 0), 0, int1]))} per hit.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17721, [int0, (-1 | 0), 35, ((35) + (5) | 0), 0, int1]))} to self per hit.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>Channelled</col>.`;
    return string0;
  };
  V[18626] = { vb: [], vp: [], vc: [], calls: [15973, 17709, 17720, 18561] };
  S[18626] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- Charge ${R.s(R.call(17720, [int0, 45, ((45) + (10) | 0), 0, int1]))} every <col=ffffff>${R.s(R.call(15973, [1, 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>- Maximum <col=ffffff>${R.s(R.str(5, 10))}</col> charges over <col=ffffff>${R.s(R.call(15973, [Math.imul(5, 1), 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>- Recast to damage the target and up to ${R.s(R.call(18561, [9]))} within ${R.s(R.call(17709, [1]))} of the target.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>Fully Charged:</col> Instead hit up to ${R.s(R.call(18561, [25]))} within ${R.s(R.call(17709, [2]))} of the target.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>Channelled</col>.`;
    return string0;
  };
  V[18627] = { vb: [], vp: [], vc: [], calls: [15973, 17720, 18554, 18560] };
  S[18627] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- Creates a ${R.s(R.call(18554, [5]))} at the target location.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 35, ((35) + (10) | 0), 0, int1]))} per hit every <col=ffffff>${R.s(R.call(15973, [2, 1]))}</col> to up to ${R.s(R.call(18560, [25]))} inside the area.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(8, 10))}</col> hits.`;
    return string0;
  };
  V[18628] = { vb: [], vp: [], vc: [], calls: [17720] };
  S[18628] = function (int0, int1, string0) {
    var int2 = 95;
    var int3 = 20;
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int2, ((int2) + (int3) | 0), 0, int1]))}.`;
    string0 = `${R.s(string0)}<br>- Damage is increased by <col=ffffff>${R.s(R.str(40, 10))}%</col> if the target is <col=ffffff>Stunned</col> or <col=ffffff>Bound</col>.`;
    return string0;
  };
  V[18629] = { vb: [], vp: [], vc: [], calls: [7495, 15734, 15973, 17709, 17720, 17725] };
  S[18629] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- Move up to ${R.s(R.call(17709, [10]))} towards the target.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 75, ((75) + (20) | 0), 0, int1]))}.`;
    string0 = `${R.s(string0)}<br>- Clears <col=ffffff>Bound</col> debuff.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17725, [11]))}.`;
    if (R.eq(int0, 40935)) {
      string0 = `${R.s(string0)}<br>- Deals an additional ${R.s(R.call(17720, [int0, 5, ((5) + (2) | 0), 0, int1]))} for every <col=ffffff>${R.s(R.call(15973, [1, 1]))}</col> since your last attack.`;
      string0 = `${R.s(string0)}<br>- After <col=ffffff>${R.s(R.call(15973, [8, 1]))}</col> since your last attack, your next <col=ffffff>Channelled ability</col> cast within <col=ffffff>${R.s(R.call(15973, [10, 1]))}</col> is dealt as <col=ffffff>Damage over time</col>.`;
    }
    if (R.eq(R.call(7495, []), 1)) {
      string0 = `${R.s(string0)}<br>- ${R.s(R.call(15734, [1]))}`;
    }
    return string0;
  };
  V[18630] = { vb: [], vp: [], vc: [], calls: [7460, 7495, 15734, 17720, 18561] };
  S[18630] = function (int0, int1, string0) {
    var int2 = 120;
    var int3 = 20;
    var int4 = R.invObj(94, 3);
    var int5 = 0;
    if (R.eq(R.itemParam(int4, 2825), 1)) {
      int5 = R.call(7460, [int4]);
    }
    switch (R.key(int5)) {
      case 2:
      {
        var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int2, ((int2) + (int3) | 0), 0, int1]))} to the target and up to ${R.s(R.call(18561, [8]))} in a <col=ffffff>cone</col> in the attack direction.`;
        break;
      }
      case 1:
      {
        string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 60, ((60) + (15) | 0), 0, int1]))} per hit.`;
        string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(2, 10))}</col> hits.`;
        break;
      }
      default:
      {
        string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int2, ((int2) + (int3) | 0), 0, int1]))}.`;
        break;
      }
    }
    if (R.eq(R.call(7495, []), 1)) {
      string0 = `${R.s(string0)}<br>- ${R.s(R.call(15734, [1]))}`;
    }
    return string0;
  };
  V[18631] = { vb: [], vp: [], vc: [], calls: [17720, 18575] };
  S[18631] = function (int0, int1, string0) {
    var int2 = 50;
    var int3 = 10;
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int2, ((int2) + (int3) | 0), 0, int1]))} per hit.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(2, 10))}</col> hits.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>PvP:</col> ${R.s(R.call(18575, [9]))}.`;
    return string0;
  };
  V[18632] = { vb: [], vp: [], vc: [], calls: [17720, 18575] };
  S[18632] = function (int0, int1, string0) {
    var int2 = 115;
    var int3 = 20;
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int2, ((int2) + (int3) | 0), 0, int1]))}.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>PvP:</col> ${R.s(R.call(18575, [9]))}.`;
    return string0;
  };
  V[18633] = { vb: [60466], vp: [], vc: [], calls: [7495, 15734, 17720, 17724, 18571] };
  S[18633] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 95, ((95) + (10) | 0), 0, int1]))}.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17724, [3, 3]))}.`;
    if (R.eq(R.vb(60466), 1)) {
      string0 = `${R.s(string0)}<br>- ${R.s(R.call(18571, [1]))}.`;
    }
    if (R.eq(R.call(7495, []), 1)) {
      string0 = `${R.s(string0)}<br>- ${R.s(R.call(15734, [1]))}`;
    }
    return string0;
  };
  V[18634] = { vb: [], vp: [], vc: [], calls: [16738, 17709, 17717, 18561, 18574] };
  S[18634] = function (int0, int1, string0) {
    if (R.eq(R.structParam(int0, 2842), 1)) {
      var string0 = `${R.s(string0)}<br>- <sprite=23399><nbsp><col=ffffff>${R.s(R.structParam(52777, 2794))}</col>: Hits up to ${R.s(R.call(18561, [9]))} within ${R.s(R.call(17709, [1]))} of the target, dealing ${R.s(R.call(17717, [70]))}.`;
      string0 = `${R.s(string0)}<br>- <sprite=23399><nbsp><col=ffffff>${R.s(R.structParam(52777, 2794))}</col>: ${R.s(R.call(16738, [5, 16]))}.`;
    } else {
      string0 = `${R.s(string0)}<br>- <sprite=23399><nbsp><col=ffffff>${R.s(R.structParam(52777, 2794))}</col>: ${R.s(R.call(18574, [5, 16]))}.`;
    }
    return string0;
  };
  V[18635] = { vb: [], vp: [], vc: [], calls: [15973, 17717, 17720] };
  S[18635] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 30, ((30) + (10) | 0), 0, int1]))} per hit every <col=ffffff>${R.s(R.call(15973, [2, 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(5, 10))}</col> hits.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>Damage over time</col>.`;
    string0 = `${R.s(string0)}<br>- Deals ${R.s(R.call(17717, [300]))} if the target moves.`;
    return string0;
  };
  V[18636] = { vb: [], vp: [], vc: [], calls: [15735, 15973, 17709, 17720, 17724, 18560] };
  S[18636] = function (int0, int1, string0) {
    var int2 = 60;
    var int3 = 10;
    var int4 = 1;
    var int5 = 7;
    switch (R.key(int0)) {
      case 40936:
      case 52785:
      {
        int2 = 60;
        int3 = 10;
        break;
      }
    }
    var string0 = `${R.s(string0)}<br>- Attack <col=ffffff>${R.s(R.str(((int5) + (1) | 0), 10))}</col> times over <col=ffffff>${R.s(R.call(15973, [((Math.imul(int4, int5)) + (1) | 0), 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int2, ((int2) + (int3) | 0), 0, int1]))} per hit to up to ${R.s(R.call(18560, [8]))} within ${R.s(R.call(17709, [1]))} of you.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>Channelled</col>.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17724, [6, 6]))}.`;
    switch (R.key(int0)) {
      case 40936:
      case 52785:
      {
        string0 = `${R.s(string0)}<br>- Each attack extends the duration of <sprite=14264><nbsp><col=ffffff>${R.s(R.structParam(52793, 2794))}</col> by <col=ffffff>${R.s(R.call(15973, [1, 1]))}</col>.`;
        break;
      }
    }
    string0 = R.call(15735, [string0]);
    return string0;
  };
  V[18637] = { vb: [], vp: [], vc: [], calls: [15736, 15973, 17709, 17720, 18561] };
  S[18637] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 135, ((135) + (30) | 0), 0, int1]))}.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 155, ((155) + (30) | 0), 0, int1]))} to the target and up to ${R.s(R.call(18561, [9]))} within ${R.s(R.call(17709, [1]))} of you.`;
    string0 = `${R.s(string0)}<br>- Reduces the cooldown of <sprite=14215><nbsp><col=ffffff>${R.s(R.structParam(14685, 2794))}</col> by <col=ffffff>${R.s(R.call(15973, [5, 1]))} for each enemy hit.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(2, 10))}</col> hits.`;
    string0 = R.call(15736, [int0, int1, string0]);
    return string0;
  };
  V[18638] = { vb: [30982], vp: [], vc: [], calls: [14945, 15411, 15973, 17718, 17720] };
  S[18638] = function (int0, int1, string0) {
    var int2 = 25;
    var int3 = 10;
    if ((R.vb(30982) > 0)) {
      int2 = ((int2) + (R.scale(int2, 100, ((10) + (Math.imul(R.vb(30982), 3)) | 0))) | 0);
      int3 = ((int3) + (R.scale(int3, 100, ((10) + (Math.imul(R.vb(30982), 3)) | 0))) | 0);
    }
    switch (R.key(int0)) {
      case 44244:
      {
        var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int2, ((int2) + (int3) | 0), 0, int1]))} per hit every <col=ffffff>${R.s(R.call(15973, [2, 1]))}</col>.`;
        string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(8, 10))}</col> hits.`;
        string0 = `${R.s(string0)}<br>- <col=ffffff>Damage over time</col>.`;
        string0 = `${R.s(string0)}<br>- Heals you for <col=ffffff>${R.s(R.str(4, 10))}%</col> of the damage dealt.`;
        if (R.eq(R.call(15411, [52788]), 1)) {
          string0 = `${R.s(string0)}<br>- Can be <col=ffffff>recast</col> within <col=ffffff>${R.s(R.call(14945, [40, 1]))}</col> of the previous cast.`;
          string0 = `${R.s(string0)}<br><br><col=ffffff>Second Cast:</col> ${R.s(R.structParam(52788, 2795))}`;
        }
        if (R.eq(R.call(15411, [52789]), 1)) {
          string0 = `${R.s(string0)}<br><col=ffffff>Third Cast:</col> ${R.s(R.structParam(52789, 2795))}`;
        }
        break;
      }
      case 52788:
      {
        string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 80, ((80) + (20) | 0), 0, int1]))} per hit every <col=ffffff>${R.s(R.call(15973, [3, 1]))}</col>.`;
        string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(6, 10))}</col> hits.`;
        string0 = `${R.s(string0)}<br>- <col=ffffff>Damage over time</col>.`;
        string0 = `${R.s(string0)}<br>- Heals you for <col=ffffff>${R.s(R.str(6, 10))}%</col> of the damage dealt.`;
        if (R.eq(R.call(15411, [52789]), 1)) {
          string0 = `${R.s(string0)}<br><br><col=ffffff>Third Cast:</col> ${R.s(R.structParam(52789, 2795))}`;
        }
        break;
      }
      case 52789:
      {
        string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 110, ((110) + (20) | 0), 0, int1]))} on first hit.`;
        string0 = `${R.s(string0)}<br>- ${R.s(R.call(17718, [int0, 100, 0, int1]))} per hit every <col=ffffff>${R.s(R.call(15973, [4, 1]))}</col>.`;
        string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(((6) + (1) | 0), 10))}</col> hits.`;
        string0 = `${R.s(string0)}<br>- <col=ffffff>Damage over time</col>.`;
        string0 = `${R.s(string0)}<br>- Heals you for <col=ffffff>${R.s(R.str(12, 10))}%</col> of the damage dealt.`;
        break;
      }
    }
    return string0;
  };
  V[18639] = { vb: [], vp: [], vc: [], calls: [9681, 17720] };
  S[18639] = function (int0, int1, string0) {
    var int2 = 520;
    var int3 = 50;
    var int4 = 1;
    if (R.eq(R.call(9681, [1]), 1)) {
      int2 = 280;
      int3 = 60;
      int4 = 2;
    }
    if ((int4 > 1)) {
      var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int2, ((int2) + (int3) | 0), 0, int1]))} with each hit.`;
      string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(int4, 10))}</col> hits.`;
    } else {
      string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int2, ((int2) + (int3) | 0), 0, int1]))}.`;
    }
    return string0;
  };
  V[18640] = { vb: [], vp: [], vc: [], calls: [15973, 17444, 17709, 17710, 17720, 18555, 18561, 18562] };
  S[18640] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 220, ((220) + (30) | 0), 0, int1]))} to the target and up to ${R.s(R.call(18561, [8]))} within ${R.s(R.call(17709, [1]))} of you.`;
    string0 = `${R.s(string0)}<br>- <col=FFA11A>Melee basic abilities</col> generate ${R.s(R.call(18562, [150]))} <sprite=14907><nbsp><col=DEAC18>Adrenaline</col>.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(18555, [45]))} every <col=ffffff>${R.s(R.call(15973, [1, 1]))}</col> while you have a <col=FFA11A>Melee weapon</col> equipped.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [R.call(17444, [int0])]))}.`;
    return string0;
  };
  V[18641] = { vb: [], vp: [], vc: [], calls: [15973, 17718, 17720] };
  S[18641] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 100, ((100) + (20) | 0), 0, int1]))} on first hit.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17718, [int0, 65, 0, int1]))} per hit every <col=ffffff>${R.s(R.call(15973, [2, 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(((5) + (1) | 0), 10))}</col> hits.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>Damage over time</col>.`;
    return string0;
  };
  V[18642] = { vb: [], vp: [], vc: [], calls: [15973, 17720] };
  S[18642] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, R.scale(50, 100, 200), ((R.scale(50, 100, 200)) + (R.scale(10, 100, 200)) | 0), 0, int1]))} on first hit.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 50, ((50) + (10) | 0), 0, int1]))} per hit every <col=ffffff>${R.s(R.call(15973, [2, 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(27, 10))}%</col> of the damage dealt after the first hit is dealt to self.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(5, 10))}</col> hits.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>Damage over time</col>.`;
    return string0;
  };
  V[18643] = { vb: [], vp: [], vc: [], calls: [] };
  S[18643] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>Target has higher life points percentage:`;
    string0 = `${R.s(string0)}<br>- Deals damage to the target equal to <col=ffffff>${R.s(R.str(100, 10))}%</col> of the percentage difference multiplied by your maximum life points.`;
    string0 = `${R.s(string0)}<br>- Heals you for <col=ffffff>${R.s(R.str(50, 10))}%</col> of the damage dealt.`;
    string0 = `${R.s(string0)}<br>Caster has higher life points percentage:`;
    string0 = `${R.s(string0)}<br>- Deals damage to self equal to <col=ffffff>${R.s(R.str(100, 10))}%</col> of the percentage difference multiplied by your maximum life points.`;
    string0 = `${R.s(string0)}<br>- Heals the target for <col=ffffff>${R.s(R.str(50, 10))}%</col> of the damage dealt.`;
    return string0;
  };
  V[18644] = { vb: [], vp: [], vc: [], calls: [15973, 17709, 17720, 18561] };
  S[18644] = function (int0, int1, string0) {
    switch (R.key(int0)) {
      case 47129:
      {
        var string0 = `${R.s(string0)}<br>- Move up to ${R.s(R.call(17709, [10]))} towards tile.`;
        break;
      }
      case 1488:
      {
        string0 = `${R.s(string0)}<br>- Move up to ${R.s(R.call(17709, [10]))} towards enemy or tile.`;
        string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 75, ((75) + (20) | 0), 0, int1]))} to the target and up to ${R.s(R.call(18561, [8]))} within ${R.s(R.call(17709, [1]))} of you.`;
        string0 = `${R.s(string0)}<br>- Enemies hit will reset the cooldown of <sprite=30331><nbsp><col=ffffff>${R.s(R.structParam(1488, 2794))}</col> if they die within <col=ffffff>${R.s(R.call(15973, [10, 1]))}</col>.`;
        break;
      }
    }
    return string0;
  };
  V[18645] = { vb: [], vp: [], vc: [], calls: [16291, 17709, 17717, 18561, 18573] };
  S[18645] = function (int0, int1, string0) {
    if (R.eq(R.structParam(int0, 2842), 1)) {
      var string0 = `${R.s(string0)}<br>- <sprite=23399><nbsp><col=ffffff>${R.s(R.structParam(52777, 2794))}</col>: Hits up to ${R.s(R.call(18561, [9]))} within ${R.s(R.call(17709, [1]))} of the target, dealing ${R.s(R.call(17717, [70]))}.`;
      string0 = `${R.s(string0)}<br>- <sprite=23399><nbsp><col=ffffff>${R.s(R.structParam(52777, 2794))}</col>: ${R.s(R.call(16291, [5, 16]))}.`;
    } else {
      string0 = `${R.s(string0)}<br>- <sprite=23399><nbsp><col=ffffff>${R.s(R.structParam(52777, 2794))}</col>: ${R.s(R.call(18573, [5, 16]))}.`;
    }
    return string0;
  };
  V[18647] = { vb: [], vp: [], vc: [], calls: [7495, 15734, 17717, 17720] };
  S[18647] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 110, ((110) + (20) | 0), 0, int1]))}.`;
    if (R.eq(R.call(7495, []), 1)) {
      string0 = `${R.s(string0)}<br>- ${R.s(R.call(15734, [1]))}`;
    }
    string0 = `${R.s(string0)}<br>- Deals ${R.s(R.call(17717, [250]))} if the target's <sprite=18851><nbsp><col=ED705A>Life<nbsp>Points</col> are below <col=ffffff>${R.s(R.strLoc(50, 1))}%</col>.`;
    return string0;
  };
  V[18648] = { vb: [], vp: [], vc: [], calls: [7495, 15734, 15973, 17720] };
  S[18648] = function (int0, int1, string0) {
    switch (R.key(int0)) {
      case 14701:
      {
        var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 110, ((110) + (20) | 0), 0, int1]))}.`;
        string0 = `${R.s(string0)}<br>- Increases the <col=ffffff>Critical Strike Chance</col> of your next <col=FFA11A>Melee attack</col> by <col=ffffff>${R.s(R.str(25, 10))}%</col>.`;
        break;
      }
      case 40941:
      {
        string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 120, ((120) + (20) | 0), 0, int1]))}.`;
        string0 = `${R.s(string0)}<br>- Your next <col=FFA11A>Melee attack</col> within <col=ffffff>${R.s(R.call(15973, [25, 1]))}</col> is guaranteed to <col=ffffff>Critically Strike</col>.`;
        break;
      }
    }
    if (R.eq(R.call(7495, []), 1)) {
      string0 = `${R.s(string0)}<br>- ${R.s(R.call(15734, [1]))}`;
    }
    return string0;
  };
  V[18649] = { vb: [], vp: [], vc: [], calls: [17720] };
  S[18649] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 55, ((55) + (10) | 0), 0, int1]))} per hit.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(2, 10))}</col> hits.`;
    string0 = `${R.s(string0)}<br>- Damage is increased by <col=ffffff>${R.s(R.str(5, 10))}%</col> if the target is <col=ffffff>Bleeding</col>.`;
    return string0;
  };
  V[18650] = { vb: [], vp: [], vc: [], calls: [17720, 18561] };
  S[18650] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 110, ((110) + (20) | 0), 0, int1]))} to the target and up to ${R.s(R.call(18561, [8]))} in a <col=ffffff>cone</col> in the attack direction.`;
    return string0;
  };
  V[18651] = { vb: [], vp: [], vc: [], calls: [15737, 15973, 17720] };
  S[18651] = function (int0, int1, string0) {
    var int2 = 2;
    var int3 = 3;
    var int4 = 130;
    var int5 = 20;
    var string0 = `${R.s(string0)}<br>- Attack <col=ffffff>${R.s(R.str(((int3) + (1) | 0), 10))}</col> times over <col=ffffff>${R.s(R.call(15973, [((Math.imul(int2, int3)) + (1) | 0), 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int4, ((int4) + (int5) | 0), 0, int1]))} per hit.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>Channelled</col>.`;
    string0 = R.call(15737, [int0, int1, string0]);
    return string0;
  };
  V[18652] = { vb: [], vp: [], vc: [], calls: [15973, 17709, 17720, 18561, 18577] };
  S[18652] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 200, ((200) + (30) | 0), 0, int1]))} to the target and up to ${R.s(R.call(18561, [8]))} within ${R.s(R.call(17709, [1]))} of you.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(18577, [5, 0]))}.`;
    string0 = `${R.s(string0)}<br>- Increases the <sprite=9228><nbsp><col=ffffff>Base Hit Chance</col> against all enemies hit by <col=ffffff>${R.s(R.str(2, 10))}</col> for <col=ffffff>${R.s(R.call(15973, [100, 1]))}</col>.`;
    return string0;
  };
  V[18653] = { vb: [], vp: [], vc: [], calls: [15973, 17720, 17724, 17725] };
  S[18653] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 60, ((60) + (10) | 0), 0, int1]))} per hit, twice every <col=ffffff>${R.s(R.call(15973, [2, 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(Math.imul(((3) + (1) | 0), 2), 10))}</col> hits.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>Channelled</col>.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17724, [6, 0]))}.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17725, [10]))}.`;
    return string0;
  };
  V[18654] = { vb: [], vp: [], vc: [], calls: [7495, 15734, 17710, 17711, 17717, 18562] };
  S[18654] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- <col=FFA11A>Melee attacks</col> deal ${R.s(R.call(17717, [175]))}.`;
    string0 = `${R.s(string0)}<br>- Increases damage taken by <col=ffffff>${R.s(R.str(25, 10))}%</col>.`;
    if (R.eq(R.call(7495, []), 1)) {
      string0 = `${R.s(string0)}<br>- ${R.s(R.call(15734, [4]))}`;
      string0 = `${R.s(string0)}<br>- Basic attacks and basic abilities generate ${R.s(R.call(18562, [200]))} <sprite=23875><nbsp><col=ffffff>${R.s(R.structParam(52791, 2794))}</col> stack.`;
      string0 = `${R.s(string0)}<br>- Maximum number of <sprite=23875><nbsp><col=ffffff>${R.s(R.structParam(52791, 2794))}</col> stacks are increased by <col=ffffff>4</col>. `;
    }
    string0 = `${R.s(string0)}<br>- <sprite=14216><nbsp><col=ffffff>${R.s(R.structParam(14686, 2794))}:</col> ${R.s(R.call(17711, [15, 1]))}.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [33]))}.`;
    return string0;
  };
  V[18655] = { vb: [], vp: [], vc: [], calls: [15973, 17720, 17721] };
  S[18655] = function (int0, int1, string0) {
    var int2 = 1;
    var int3 = 7;
    var int4 = 65;
    var int5 = 30;
    var string0 = `${R.s(string0)}<br>- Attack <col=ffffff>${R.s(R.str(((int3) + (1) | 0), 10))}</col> times over <col=ffffff>${R.s(R.call(15973, [((Math.imul(int2, int3)) + (1) | 0), 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int4, ((int4) + (int5) | 0), 0, int1]))} per hit.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>Channelled</col>.`;
    string0 = `${R.s(string0)}<br>- Deals an additional ${R.s(R.call(17721, [int0, (-1 | 0), 10, ((10) + (10) | 0), 0, int1]))} with each hit.`;
    return string0;
  };
  V[18656] = { vb: [], vp: [], vc: [], calls: [15973, 17720, 18555] };
  S[18656] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 300, ((300) + (40) | 0), 0, int1]))}.`;
    string0 = `${R.s(string0)}<br>- Applies <sprite=14266><nbsp><col=ffffff>${R.s(R.structParam(51665, 2794))}</col> to the target for <col=ffffff>${R.s(R.call(15973, [50, 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>On killing blow:</col> ${R.s(R.call(18555, [500]))}</col>.`;
    return string0;
  };
  V[18657] = { vb: [], vp: [], vc: [], calls: [7495, 15734, 15973, 17720, 18562] };
  S[18657] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 100, ((100) + (20) | 0), 0, int1]))}.`;
    string0 = `${R.s(string0)}<br>- Your next <col=FFA11A>Melee ability</col> within <col=ffffff>${R.s(R.call(15973, [12, 1]))}</col> deals ${R.s(R.call(18562, [175]))} (<col=ffffff>PvP:</col> ${R.s(R.call(18562, [125]))}) base damage.`;
    if (R.eq(R.call(7495, []), 1)) {
      string0 = `${R.s(string0)}<br>- ${R.s(R.call(15734, [1]))}`;
    }
    return string0;
  };
  V[18658] = { vb: [], vp: [], vc: [], calls: [17720] };
  S[18658] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 90, ((90) + (20) | 0), 0, int1]))}.`;
    string0 = `${R.s(string0)}<br>- Generates <col=ffffff>${R.s(R.str(4, 10))}<nbsp><sprite=30101><nbsp>${R.s(R.structParam(48333, 2794))}</col> stacks.`;
    return string0;
  };
  V[18659] = { vb: [], vp: [], vc: [], calls: [17712, 17720] };
  S[18659] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 270, ((270) + (60) | 0), 0, int1]))}.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17712, [100, 0]))} for each <sprite=30101><nbsp><col=ffffff>${R.s(R.structParam(48333, 2794))}</col> stack.`;
    string0 = `${R.s(string0)}<br>- Consumes up to <col=ffffff>${R.s(R.str(6, 10))} <sprite=30101><nbsp>${R.s(R.structParam(48333, 2794))}</col> stacks.`;
    return string0;
  };
  V[18660] = { vb: [], vp: [], vc: [], calls: [17720] };
  S[18660] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 90, ((90) + (20) | 0), 0, int1]))}.`;
    string0 = `${R.s(string0)}<br>- Generates <col=ffffff>${R.s(R.str(1, 10))} <sprite=30123><nbsp>${R.s(R.structParam(48334, 2794))}</col> stack with each hit.`;
    return string0;
  };
  V[18661] = { vb: [], vp: [], vc: [], calls: [17709, 17720, 17724, 18560] };
  S[18661] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 135, ((135) + (30) | 0), 0, int1]))}.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17724, [5, 5]))}.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 90, ((90) + (20) | 0), 0, int1]))} to up to ${R.s(R.call(18560, [9]))} within ${R.s(R.call(17709, [1]))} of the target.`;
    return string0;
  };
  V[18662] = { vb: [], vp: [], vc: [], calls: [17720] };
  S[18662] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 135, ((135) + (30) | 0), 0, int1]))} for each <sprite=30123><nbsp><col=ffffff>${R.s(R.structParam(48334, 2794))}</col> stack.`;
    return string0;
  };
  V[18663] = { vb: [], vp: [], vc: [], calls: [15973, 17454, 17710, 17721, 17727] };
  S[18663] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17721, [int0, 38, 22, ((22) + (6) | 0), 0, int1]))} every <col=ffffff>${R.s(R.call(15973, [5, 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [R.call(17454, [int0])]))}.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>Spirit</col>.`;
    string0 = `${R.s(string0)}<br><br><col=ffffff>${R.s(R.call(17727, [48335, 0, int1]))}`;
    return string0;
  };
  V[18664] = { vb: [], vp: [], vc: [], calls: [15973, 17710, 17721] };
  S[18664] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17721, [int0, 38, 22, ((22) + (6) | 0), 0, int1]))} per hit every <col=ffffff>${R.s(R.call(15973, [2, 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(2, 10))}</col> hits.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [10]))}.`;
    return string0;
  };
  V[18665] = { vb: [], vp: [], vc: [], calls: [15973, 17454, 17710, 17721, 17727] };
  S[18665] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17721, [int0, 38, 18, ((18) + (4) | 0), 0, int1]))} every <col=ffffff>${R.s(R.call(15973, [6, 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [R.call(17454, [int0])]))}.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>Spirit</col>.`;
    string0 = `${R.s(string0)}<br><br><col=ffffff>${R.s(R.call(17727, [48336, 0, int1]))}`;
    return string0;
  };
  V[18666] = { vb: [], vp: [], vc: [], calls: [17709, 17721] };
  S[18666] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17721, [int0, 38, 360, ((360) + (80) | 0), 0, int1]))} to enemies within ${R.s(R.call(17709, [2]))}.`;
    return string0;
  };
  V[18667] = { vb: [], vp: [], vc: [], calls: [15973, 17454, 17710, 17721, 17727] };
  S[18667] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17721, [int0, 38, 18, ((18) + (4) | 0), 0, int1]))} every <col=ffffff>${R.s(R.call(15973, [7, 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [R.call(17454, [int0])]))}.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>Spirit</col>.`;
    string0 = `${R.s(string0)}<br><br><col=ffffff>${R.s(R.call(17727, [48337, 0, int1]))}`;
    return string0;
  };
  V[18668] = { vb: [], vp: [], vc: [], calls: [15973] };
  S[18668] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- Applies <sprite=30212><nbsp><col=ffffff>${R.s(R.structParam(48344, 2794))}</col> to the target with each attack for <col=ffffff>${R.s(R.call(15973, [8, 1]))}</col>.`;
    return string0;
  };
  V[18669] = { vb: [], vp: [], vc: [], calls: [17720] };
  S[18669] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, 135, ((135) + (30) | 0), 0, int1]))}.`;
    string0 = `${R.s(string0)}<br>- Applies <sprite=30098><nbsp><col=ffffff>${R.s(R.structParam(48338, 2794))}</col> to the target for <col=ffffff>${R.s(R.str(10, 10))}</col> hits.`;
    return string0;
  };
  V[18670] = { vb: [], vp: [], vc: [], calls: [15973, 17709, 17720, 18560] };
  S[18670] = function (int0, int1, string0) {
    var int2 = 22;
    var int3 = 6;
    var int4 = 2;
    var int5 = 4;
    var string0 = `${R.s(string0)}<br>- Attack <col=ffffff>${R.s(R.str(((int5) + (1) | 0), 10))}</col> times over <col=ffffff>${R.s(R.call(15973, [((Math.imul(int4, int5)) + (1) | 0), 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int2, ((int2) + (int3) | 0), 0, int1]))} per hit to up to ${R.s(R.call(18560, [25]))} within ${R.s(R.call(17709, [2]))} of you.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>Channelled</col>.`;
    string0 = `${R.s(string0)}<br>- Heals you for <col=ffffff>${R.s(R.str(70, 10))}%</col> of the damage dealt.`;
    string0 = `${R.s(string0)}<br>- The final attack instead deals ${R.s(R.call(17720, [48309, 117, ((117) + (26) | 0), 0, int1]))} to the target plus <col=ffffff>${R.s(R.str(100, 10))}%</col> of the total heal value.`;
    return string0;
  };
  V[18671] = { vb: [], vp: [], vc: [], calls: [17444, 17710, 17711, 17717, 18558] };
  S[18671] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- <sprite=30015><nbsp><col=ffffff>${R.s(R.structParam(48293, 2794))}:</col> Generates <col=ffffff>${R.s(R.str(2, 10))}<nbsp><sprite=30101><nbsp>${R.s(R.structParam(48333, 2794))}</col> stacks.`;
    string0 = `${R.s(string0)}<br>- <sprite=30076><nbsp><col=ffffff>${R.s(R.structParam(48296, 2794))}:</col> ${R.s(R.call(18558, [60]))}</col>.`;
    string0 = `${R.s(string0)}<br>- <sprite=30077><nbsp><col=ffffff>${R.s(R.structParam(48297, 2794))}:</col> Deals ${R.s(R.call(17717, [150]))}.`;
    string0 = `${R.s(string0)}<br>- <sprite=30074><nbsp><col=ffffff>${R.s(R.structParam(48314, 2794))}:</col> ${R.s(R.call(17711, [17, 1]))}.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [R.call(17444, [int0])]))}.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>On cast:</col> Resets the cooldown of <sprite=30076><nbsp><col=ffffff>${R.s(R.structParam(48296, 2794))}</col> and <sprite=30074><nbsp><col=ffffff>${R.s(R.structParam(48314, 2794))}</col>.`;
    return string0;
  };
  V[18672] = { vb: [], vp: [], vc: [], calls: [15973, 16858] };
  S[18672] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.strLoc(R.scale(50, 100, R.call(16858, [])), 1))}</col> (<col=ffffff>50%</col> <sprite=18851><nbsp><col=ED705A>Base<nbsp>Life<nbsp>Points</col>) damage to self.`;
    string0 = `${R.s(string0)}<br>- Extends the duration of each active <col=ffffff>Spirit</col> by <col=ffffff>${R.s(R.call(15973, [35, 1]))}</col>.`;
    return string0;
  };
  V[18673] = { vb: [], vp: [], vc: [], calls: [17444, 17707, 17709, 17710, 18561] };
  S[18673] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.call(17707, [1, 1]))}</col> <col=A788DD>Necromancy attacks</col> will also be cast on up to ${R.s(R.call(18561, [4]))} within ${R.s(R.call(17709, [4]))} of the target.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [R.call(17444, [int0])]))}.`;
    return string0;
  };
  V[18674] = { vb: [], vp: [], vc: [], calls: [17444, 17710] };
  S[18674] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- Your next <col=A788DD>Necromancy attack</col> applies <sprite=30100><nbsp><col=ffffff>${R.s(R.structParam(48345, 2794))}</col> to the target.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [R.call(17444, [int0])]))}.`;
    return string0;
  };
  V[18675] = { vb: [], vp: [], vc: [], calls: [15973, 17444, 17710] };
  S[18675] = function (int0, int1, string0) {
    var int2 = R.call(17444, [int0]);
    var string0 = `${R.s(string0)}<br>- Applies <sprite=30122><nbsp><col=ffffff>${R.s(R.structParam(48343, 2794))}</col> to self.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [int2]))}.`;
    string0 = `${R.s(string0)}<br>- Can be recast to extend duration by <col=ffffff>${R.s(R.call(15973, [int2, 1]))}</col> per cast to a maximum of <col=ffffff>${R.s(R.call(15973, [6000, 1]))}</col>.`;
    return string0;
  };
  V[18676] = { vb: [], vp: [], vc: [], calls: [17444, 17710] };
  S[18676] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(400, 10))}%</col> of the life points you would have gained from the <col=ffffff><sprite=26033><nbsp>Soul Split</col> curse is instead dealt to your target.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [R.call(17444, [int0])]))}.`;
    return string0;
  };
  V[18677] = { vb: [], vp: [], vc: [], calls: [15973, 17720] };
  S[18677] = function (int0, string0) {
    var int1 = 45;
    var int2 = 10;
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [14663, int1, ((int1) + (int2) | 0), 0, int0]))} per hit.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(2, 10))}</col> hits.`;
    string0 = `${R.s(string0)}<br>- Reduces the cooldown of <sprite=23729><nbsp><col=ffffff>${R.s(R.structParam(14666, 2794))}</col> by <col=ffffff>${R.s(R.call(15973, [4, 1]))}</col> with each hit.`;
    return string0;
  };
  V[18678] = { vb: [60465], vp: [], vc: [], calls: [17720, 17724, 17725, 18571] };
  S[18678] = function (int0, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [14664, 65, ((65) + (10) | 0), 0, int0]))}.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17724, [2, 16]))}.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17725, [16]))}.`;
    if (R.eq(R.vb(60465), 1)) {
      string0 = `${R.s(string0)}<br>- ${R.s(R.call(18571, [1]))}.`;
    }
    return string0;
  };
  V[18680] = { vb: [], vp: [], vc: [], calls: [17720, 17724, 18571] };
  S[18680] = function (int0, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [39532, 65, ((65) + (10) | 0), 0, int0]))}.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17724, [2, 2]))}.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(18571, [1]))}.`;
    return string0;
  };
  V[18681] = { vb: [], vp: [], vc: [], calls: [17720, 17724, 18571] };
  S[18681] = function (int0, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [39533, 120, ((120) + (20) | 0), 0, int0]))}.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17724, [6, 6]))}.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(18571, [1]))}.`;
    return string0;
  };
  V[18682] = { vb: [], vp: [], vc: [], calls: [17709, 18544] };
  S[18682] = function (int0, string0) {
    var string0 = `${R.s(string0)}<br>- Move backwards ${R.s(R.call(17709, [R.call(18544, [])]))}.`;
    return string0;
  };
  V[18683] = { vb: [], vp: [], vc: [], calls: [17720] };
  S[18683] = function (int0, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [14669, 135, ((135) + (20) | 0), 0, int0]))} per hit.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(2, 10))}</col> hits.`;
    return string0;
  };
  V[18684] = { vb: [], vp: [], vc: [], calls: [15973, 17720, 17725] };
  S[18684] = function (int0, int1, string0) {
    var int2 = 1;
    var int3 = 7;
    var int4 = 75;
    var int5 = 10;
    var string0 = `${R.s(string0)}<br>- Attack <col=ffffff>${R.s(R.str(((int3) + (1) | 0), 10))}</col> times over <col=ffffff>${R.s(R.call(15973, [((Math.imul(int2, int3)) + (1) | 0), 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int4, ((int4) + (int5) | 0), 0, int1]))} per hit.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>Channelled</col>.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17725, [10]))}.`;
    string0 = `${R.s(string0)}<br>- Each attack extends the duration of <sprite=23877><nbsp><col=ffffff>${R.s(R.structParam(52801, 2794))}</col> by <col=ffffff>${R.s(R.call(15973, [1, 1]))}</col>.`;
    return string0;
  };
  V[18685] = { vb: [], vp: [], vc: [], calls: [17709, 17720, 18561] };
  S[18685] = function (int0, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [14671, 220, ((220) + (40) | 0), 0, int0]))} to the target and up to ${R.s(R.call(18561, [9]))} within ${R.s(R.call(17709, [2]))} of the target.`;
    return string0;
  };
  V[18686] = { vb: [], vp: [], vc: [], calls: [15973, 17709, 17720, 18561] };
  S[18686] = function (int0, string0) {
    var int1 = 5;
    var int2 = 5;
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [31986, 90, ((90) + (20) | 0), 0, int0]))} per hit every <col=ffffff>${R.s(R.call(15973, [2, 1]))}</col> to the target and up to ${R.s(R.call(18561, [int1]))} within ${R.s(R.call(17709, [int2]))} of the target.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(5, 10))}</col> hits.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>Damage over time</col>.`;
    string0 = `${R.s(string0)}<br>- Damage is reduced by <col=ffffff>${R.s(R.str(20, 10))}%</col> of initial damage with each hit.`;
    return string0;
  };
  V[18687] = { vb: [], vp: [], vc: [], calls: [15973, 17709, 17720, 18559, 18561] };
  S[18687] = function (int0, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [14672, 270, ((270) + (60) | 0), 0, int0]))} after <col=ffffff>${R.s(R.call(15973, [((4) + (1) | 0), 1]))}</col> to the target and up to ${R.s(R.call(18561, [9]))} within ${R.s(R.call(17709, [1]))} of the target.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(18559, [80, 50]))}.`;
    return string0;
  };
  V[18688] = { vb: [], vp: [], vc: [], calls: [15973, 17719, 17720] };
  S[18688] = function (int0, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [14673, 130, ((130) + (20) | 0), 0, int0]))} per hit every <col=ffffff>${R.s(R.call(15973, [2, 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(((3) + (1) | 0), 10))}</col> hits.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>Channelled</col>.`;
    string0 = `${R.s(string0)}<br>- Deals an additional ${R.s(R.call(17719, [14673, (-1 | 0), 20, 0, int0]))} with each hit.`;
    return string0;
  };
  V[18689] = { vb: [], vp: [], vc: [], calls: [15973, 17720, 17721] };
  S[18689] = function (int0, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [28177, 200, ((200) + (40) | 0), 0, int0]))}.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17721, [28177, (-1 | 0), 100, ((100) + (35) | 0), 0, int0]))} to self.`;
    string0 = `${R.s(string0)}<br>- Extends the duration of <sprite=31925><nbsp><col=ffffff>${R.s(R.structParam(52802, 2794))}</col> by <col=ffffff>${R.s(R.call(15973, [6, 1]))}</col>.`;
    return string0;
  };
  V[18690] = { vb: [], vp: [], vc: [], calls: [17720] };
  S[18690] = function (int0, string0) {
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [1491, 110, ((110) + (20) | 0), 0, int0]))}.`;
    string0 = `${R.s(string0)}<br>- Deals an additional ${R.s(R.call(17720, [1491, 10, ((10) + (5) | 0), 0, 1]))} for each <sprite=30335><nbsp><col=ffffff>${R.s(R.structParam(1489, 2794))}</col> stack on the target.`;
    string0 = `${R.s(string0)}<br>- Refreshes <sprite=30335><nbsp><col=ffffff>${R.s(R.structParam(1489, 2794))}</col>.`;
    return string0;
  };
  V[18691] = { vb: [], vp: [], vc: [], calls: [17720, 18563] };
  S[18691] = function (int0, int1, string0) {
    var int2 = 300;
    var int3 = 60;
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int2, ((int2) + (int3) | 0), 0, int1]))} after ${R.s(R.call(18563, [2, 1]))}.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>Channelled</col>.`;
    return string0;
  };
  V[18692] = { vb: [], vp: [], vc: [], calls: [15973, 17717, 17720] };
  S[18692] = function (int0, int1, string0) {
    var int2 = 25;
    var int3 = 6;
    var int4 = 200;
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int2, ((int2) + (int3) | 0), 0, int1]))} per hit every <col=ffffff>${R.s(R.call(15973, [2, 1]))}</col>.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(5, 10))}</col> hits.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>Damage over time</col>.`;
    string0 = `${R.s(string0)}<br>- Deals ${R.s(R.call(17717, [int4]))} if the target moves.`;
    return string0;
  };
  V[18693] = { vb: [], vp: [], vc: [], calls: [17709, 17720, 18561] };
  S[18693] = function (int0, int1, string0) {
    var int2 = 2;
    var int3 = 75;
    var int4 = 10;
    if (R.eq(int0, 45048)) {
      int2 = 6;
      int3 = 75;
      int4 = 10;
    }
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int3, ((int3) + (int4) | 0), 0, int1]))} to the target and up to ${R.s(R.call(18561, [int2]))} within ${R.s(R.call(17709, [5]))} of the target.`;
    if ((int2 > 2)) {
      string0 = `${R.s(string0)}<br>- The target will be hit for an additional ${R.s(R.call(17720, [int0, 15, ((15) + (5) | 0), 0, int1]))} (${R.s(R.call(17720, [int0, 4, ((4) + (2) | 0), 0, int1]))} after <col=ffffff>${R.s(R.str(2, 10))}</col> hits) for each enemy that cannot be found.`;
    } else {
      string0 = `${R.s(string0)}<br>- The target will be hit for an additional ${R.s(R.call(17720, [int0, 15, ((15) + (5) | 0), 0, int1]))} for each enemy that cannot be found.`;
    }
    return string0;
  };
  V[18694] = { vb: [], vp: [], vc: [], calls: [9681, 17720] };
  S[18694] = function (int0, string0) {
    var int1 = 105;
    var int2 = 20;
    var int3 = 4;
    if (R.eq(R.call(9681, [2]), 1)) {
      int1 = 55;
      int2 = 20;
      int3 = 8;
    }
    var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [14674, int1, ((int1) + (int2) | 0), 0, int0]))} per hit.`;
    string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(int3, 10))}</col> hits.`;
    return string0;
  };
  V[18697] = { vb: [30983], vp: [], vc: [], calls: [17444, 17710, 17717] };
  S[18697] = function (int0, int1, string0) {
    var int2 = R.call(17444, [int0]);
    if ((R.eq(int0, 19251) && (R.vb(30983) > 0))) {
      int2 = ((int2) + (R.scale(int2, 100, 26)) | 0);
    }
    var string0 = `${R.s(string0)}<br>- <col=25AD37>Ranged attacks</col> deal ${R.s(R.call(17717, [150]))}.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [int2]))}.`;
    return string0;
  };
  V[18710] = { vb: [], vp: [], vc: [], calls: [17710] };
  S[18710] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.str(400, 10))}%</col> of the life points you would have gained from the <col=ffffff><sprite=26033><nbsp>Soul Split</col> curse is instead dealt to your target.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [25]))}.`;
    return string0;
  };
  V[18713] = { vb: [], vp: [], vc: [], calls: [17707, 17709, 17710, 18561] };
  S[18713] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.call(17707, [1, 1]))}</col> <col=25AD37>Ranged attacks</col> will also be cast on up to ${R.s(R.call(18561, [5]))} within ${R.s(R.call(17709, [3]))} of the target.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [18]))}.`;
    return string0;
  };
  V[18825] = { vb: [], vp: [], vc: [], calls: [18576] };
  S[18825] = function (string0) {
    var string0 = `${R.s(string0)}- Attacks have a <col=ffffff>${R.s(R.str(25, 10))}%</col> chance to increase your ${R.s(R.call(18576, [0, 1, 1]))} and ${R.s(R.call(18576, [2, 1, 1]))} by <col=ffffff>${R.s(R.str(1, 10))}</col>, up to a maximum of <col=ffffff>${R.s(R.str(10, 10))}%</col>.`;
    return string0;
  };
  V[18826] = { vb: [], vp: [], vc: [], calls: [15973] };
  S[18826] = function (string0) {
    var string0 = `${R.s(string0)}- Attacks have a <col=ffffff>${R.s(R.str(25, 10))}%</col> chance to <col=ffffff>bind</col> the target for <col=ffffff>${R.s(R.call(15973, [10, 1]))}</col>.`;
    return string0;
  };
  V[18842] = { vb: [], vp: [], vc: [], calls: [12478] };
  S[18842] = function (int0) {
    var int1 = R.enumValue(0, 3, 2252, int0);
    if (R.eq(int1, (-1 | 0))) {
      switch (R.key(int0)) {
        case 164:
        {
          int1 = 354;
          break;
        }
        default:
        {
          R.call(12478, [`Failed to find quest #${R.s(R.str(int0, 10))} in [ql4_quests] enum.`]);
          break;
        }
      }
    }
    return int1;
  };
  V[18986] = { vb: [55661], vp: [], vc: [], calls: [17520, 17522] };
  S[18986] = function () {
    if (R.eq(R.vb(55661), 0)) {
      return "Current alteration: None";
    }
    return `Current alteration: ${R.s(R.call(17522, [R.call(17520, [R.vb(55661)])]))}`;
  };
  V[19021] = { vb: [], vp: [], vc: [], calls: [15973] };
  S[19021] = function (int0) {
    var string0 = "";
    switch (R.key(int0)) {
      case 1:
      {
        string0 = `Damage over time abilities generate <sprite=33967><nbsp>${R.s(R.structParam(50083, 2794))}, lasting ${R.s(R.call(15973, [50, 1]))}. At ${R.s(R.str(1, 10))}, ${R.s(R.str(10, 10))} and ${R.s(R.str(25, 10))} stacks, gain additional effects.`;
        break;
      }
      case 2:
      {
        string0 = `Damage over time abilities deal ${R.s(R.str(30, 10))}% increased damage.`;
        break;
      }
    }
    return string0;
  };
  V[19131] = { vb: [], vp: [], vc: [], calls: [12478] };
  S[19131] = function (int0) {
    R.dbFind(1200128, int0, 0);
    var int1 = R.dbNext();
    if (R.eq(int1, (-1 | 0))) {
      R.call(12478, [`Unable to locate runecrafting_altars dbrow with id = ${R.s(R.str(int0, 10))}`]);
      return "Unknown";
    }
    return R.dbField(int1, 1200144, 0);
  };
  V[19132] = { vb: [55991, 55992, 55993], vp: [], vc: [], calls: [19131] };
  S[19132] = function () {
    var string0 = "";
    if (R.eq(R.vb(55993), 1)) {
      string0 = "Teleport: <col=00ff00>Charged";
    } else {
      string0 = "Teleport: <col=969696>Not yet charged";
    }
    return `Absorbed energy: <col=00ff00>${R.s(R.strLoc(R.vb(55992), 1))}<br>${R.s(string0)}<br>Current attunement: <col=00ff00>${R.s(R.call(19131, [R.vb(55991)]))}`;
  };
  V[19316] = { vb: [56612], vp: [], vc: [], calls: [] };
  S[19316] = function () {
    if (R.eq(R.vb(56612), 1)) {
      return 1;
    }
    return 0;
  };
  V[19318] = { vb: [56613], vp: [], vc: [], calls: [] };
  S[19318] = function () {
    if (R.eq(R.vb(56613), 1)) {
      return 1;
    }
    return 0;
  };
  V[19638] = { vb: [36288, 56740], vp: [], vc: [], calls: [7653] };
  S[19638] = function (int0, string0) {
    var int1 = 75;
    var int2 = 125;
    var int3 = R.vb(56740);
    var int4 = R.vb(36288);
    if (R.eq(R.itemParam(int0, 4079), 1)) {
      int1 = 80;
      int2 = 130;
    }
    if (R.eq(R.itemParam(int0, 4080), 1)) {
      int1 = 85;
      int2 = 135;
    }
    if (R.eq(R.itemParam(int0, 4081), 1)) {
      int1 = 90;
      int2 = 140;
    }
    if (R.eq(R.itemParam(int0, 6599), 1)) {
      int1 = 95;
      int2 = 145;
    }
    if (R.eq(int3, 0)) {
      var string0 = `${R.s(string0)}- <col=FFA11A>Melee</col>, <col=25AD37>Ranged</col> and <col=3366FF>Magic</col> attacks against your active Slayer Assignment have ${R.s(R.call(7653, [int2, 1, 1, 1, 1]))}% increased accuracy, and deal ${R.s(R.call(7653, [int1, 1, 1, 1, 1]))}% increased damage.`;
    } else {
      string0 = `${R.s(string0)}- <col=FFA11A>Melee</col>, <col=25AD37>Ranged</col>, <col=3366FF>Magic</col> and <col=8257CF>Necromancy</col> attacks against your active Slayer Assignment have ${R.s(R.call(7653, [int2, 1, 1, 1, 1]))}% increased accuracy, and deal ${R.s(R.call(7653, [int1, 1, 1, 1, 1]))}% increased damage.`;
    }
    if ((int4 > 0)) {
      string0 = `${R.s(string0)}<br>- +${R.s(R.str(int4, 10))}% Slayer XP.`;
    }
    return string0;
  };
  V[19639] = { vb: [], vp: [], vc: [], calls: [7653] };
  S[19639] = function (string0) {
    var int0 = 75;
    var int1 = 125;
    var string0 = `${R.s(string0)}- <col=FFA11A>Melee attacks</col> against your active Slayer Assignment have ${R.s(R.call(7653, [int1, 1, 1, 1, 1]))}% increased accuracy, and deal ${R.s(R.call(7653, [int0, 1, 1, 1, 1]))}% increased damage.`;
    return string0;
  };
  V[19640] = { vb: [], vp: [], vc: [], calls: [7653] };
  S[19640] = function (string0) {
    var int0 = 75;
    var int1 = 125;
    var string0 = `${R.s(string0)}- <col=3366FF>Magic attacks</col> against your active Slayer Assignment have ${R.s(R.call(7653, [int1, 1, 1, 1, 1]))}% increased accuracy, and deal ${R.s(R.call(7653, [int0, 1, 1, 1, 1]))}% increased damage.`;
    return string0;
  };
  V[19641] = { vb: [], vp: [], vc: [], calls: [7653] };
  S[19641] = function (string0) {
    var int0 = 75;
    var int1 = 125;
    var string0 = `${R.s(string0)}- <col=25AD37>Ranged attacks</col> against your active Slayer Assignment have ${R.s(R.call(7653, [int1, 1, 1, 1, 1]))}% increased accuracy, and deal ${R.s(R.call(7653, [int0, 1, 1, 1, 1]))}% increased damage.`;
    return string0;
  };
  V[19642] = { vb: [], vp: [], vc: [], calls: [7653] };
  S[19642] = function (string0) {
    var int0 = 75;
    var int1 = 125;
    var string0 = `${R.s(string0)}- <col=8257CF>Necromancy attacks</col> against your active Slayer Assignment have ${R.s(R.call(7653, [int1, 1, 1, 1, 1]))}% increased accuracy, and deal ${R.s(R.call(7653, [int0, 1, 1, 1, 1]))}% increased damage.`;
    return string0;
  };
  V[19652] = { vb: [], vp: [], vc: [], calls: [19653] };
  S[19652] = function (int0) {
    var int1 = ((R.invSize(int0)) - (1) | 0);
    var int2 = 0;
    while ((int1 > (-1 | 0))) {
      int2 = ((int2) + (R.call(19653, [R.invObj(int0, int1)])) | 0);
      int1 = ((int1) - (1) | 0);
    }
    return int2;
  };
  V[19653] = { vb: [], vp: [], vc: [], calls: [17465] };
  S[19653] = function (int0) {
    if (R.eq(int0, (-1 | 0))) {
      return 0;
    }
    var int1 = R.itemParam(int0, 1326);
    if ((int1 > 0)) {
      return int1;
    }
    if (R.eq(R.call(17465, [int0]), 0)) {
      return 0;
    }
    var int2 = R.itemParam(int0, 4596);
    if (R.eq(int2, 0)) {
      int2 = R.itemParam(int0, 750);
    }
    var int3 = 0;
    switch (R.key(R.itemWearpos(int0))) {
      case 0:
      {
        int3 = 10;
        break;
      }
      case 4:
      {
        int3 = 15;
        break;
      }
      case 7:
      {
        int3 = 15;
        break;
      }
      case 9:
      {
        int3 = 5;
        break;
      }
      case 10:
      {
        int3 = 5;
        break;
      }
      default:
      {
        return 0;
      }
    }
    int1 = Math.imul(int2, int3);
    return int1;
  };
  V[19675] = { vb: [], vp: [], vc: [], calls: [] };
  S[19675] = function () {
    var int0 = 5;
    var int1 = 3;
    var int2 = 1;
    var int3 = 10;
    if ((R.statBase(8) >= R.structParam(28968, 2212))) {
      int0 = ((int0) + (1) | 0);
      int1 = ((int1) + (1) | 0);
      int2 = ((int2) + (1) | 0);
      int3 = ((int3) + (1) | 0);
    }
    return `- ${R.s(R.str(int0, 10))}% XP when a log is obtained (for only 1 of the logs if multiple are obtained).<br>- ${R.s(R.str(int1, 10))}% chance to obtain Bird's Nests.<br>- ${R.s(R.str(int2, 10))}% chance to obtain Enchanted Bird's Nests.<br>- ${R.s(R.str(int3, 10))}% chance to perform a 'Perfect Cut'.`;
  };
  V[19680] = { vb: [], vp: [], vc: [], calls: [12477, 18362, 18364] };
  S[19680] = function () {
    if (((R.call(12477, []) < R.call(18362, [])) || (R.call(12477, []) >= R.call(18364, [])))) {
      return 0;
    }
    return 1;
  };
  V[19769] = { vb: [], vp: [], vc: [], calls: [14614] };
  S[19769] = function (int0, int1) {
    switch (R.key(int0)) {
      case 0:
      {
        if (R.eq(R.call(14614, [1]), int1)) {
          return 1;
        }
        if (R.eq(R.call(14614, [2]), int1)) {
          return 2;
        }
        if (R.eq(R.call(14614, [3]), int1)) {
          return 3;
        }
        break;
      }
      case 1:
      {
        if (R.eq(R.call(14614, [4]), int1)) {
          return 4;
        }
        if (R.eq(R.call(14614, [5]), int1)) {
          return 5;
        }
        if (R.eq(R.call(14614, [6]), int1)) {
          return 6;
        }
        break;
      }
      case 2:
      {
        if (R.eq(R.call(14614, [7]), int1)) {
          return 7;
        }
        if (R.eq(R.call(14614, [8]), int1)) {
          return 8;
        }
        if (R.eq(R.call(14614, [9]), int1)) {
          return 9;
        }
        break;
      }
      case 3:
      {
        if (R.eq(R.call(14614, [10]), int1)) {
          return 10;
        }
        if (R.eq(R.call(14614, [11]), int1)) {
          return 11;
        }
        if (R.eq(R.call(14614, [12]), int1)) {
          return 12;
        }
        break;
      }
    }
    return (-1 | 0);
  };
  V[19771] = { vb: [], vp: [], vc: [], calls: [] };
  S[19771] = function (int0, int1) {
    switch (R.key(int0)) {
      case 0:
      {
        if ((int1 > 3)) {
          return 1;
        }
        break;
      }
      case 1:
      {
        if (((int1 < 4) || (int1 > 6))) {
          return 1;
        }
        break;
      }
      case 2:
      {
        if (((int1 < 7) || (int1 > 9))) {
          return 1;
        }
        break;
      }
      case 3:
      {
        if ((int1 < 10)) {
          return 1;
        }
        break;
      }
    }
    if ((!R.eq(int1, (-1 | 0)))) {
      return 2;
    }
    return 1;
  };
  V[19865] = { vb: [], vp: [], vc: [], calls: [7241, 17172] };
  S[19865] = function () {
    var int0 = 0;
    var int1 = R.invSize(94);
    var int2 = (-1 | 0);
    var int3 = (-1 | 0);
    var int4 = (-1 | 0);
    var int5 = (-1 | 0);
    while (((int5 = (int5 + (1)) | 0) < int1)) {
      int2 = R.invObj(94, int5);
      if ((!R.eq(int2, (-1 | 0)))) {
        int3 = R.call(7241, [int2]);
        if (R.eq(int3, 7)) {
          int4 = R.call(17172, [int2, int3]);
          if (R.eq(int4, 2)) {
            int0 = ((int0) + (R.itemParam(int2, 965)) | 0);
          }
        }
      }
    }
    return R.min(200, R.scale(int0, 1000, 125));
  };
  V[19866] = { vb: [], vp: [], vc: [], calls: [15973, 17444, 17710] };
  S[19866] = function (int0, int1, string0) {
    var int2 = R.call(17444, [int0]);
    var string0 = `${R.s(string0)}<br>- Applies <sprite=1368><nbsp><col=ffffff>${R.s(R.structParam(51272, 2794))}</col> to self.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [int2]))}.`;
    string0 = `${R.s(string0)}<br>- Can be recast to extend duration by <col=ffffff>${R.s(R.call(15973, [int2, 1]))}</col> per cast to a maximum of <col=ffffff>${R.s(R.call(15973, [6000, 1]))}</col>.`;
    return string0;
  };
  V[19867] = { vb: [], vp: [], vc: [], calls: [7653, 19865] };
  S[19867] = function (string0) {
    var int0 = R.call(19865, []);
    if (R.eq(int0, 0)) {
      var string0 = `${R.s(string0)}<br>- Gain <col=ffffff>${R.s(R.call(7653, [125, 1, 1, 0, 1]))}%</col> of magic power armour damage bonus as chance to reset the cooldown of a magic ability when cast against a target.`;
    } else {
      string0 = `${R.s(string0)}<br>- <col=ffffff>${R.s(R.call(7653, [R.call(19865, []), 1, 1, 0, 1]))}%</col> (<col=ffffff>${R.s(R.call(7653, [125, 1, 1, 0, 1]))}%</col> of magic power armour damage bonus) chance to reset the cooldown of a magic ability when cast against a target.`;
    }
    return string0;
  };
  V[19868] = { vb: [], vp: [], vc: [], calls: [] };
  S[19868] = function (int0, int1, string0) {
    var string0 = `${R.s(string0)}<br>- Base damage is increased by <col=ffffff>${R.s(R.str(30, 10))}%</col> against undead.`;
    return string0;
  };
  V[19869] = { vb: [57684], vp: [], vc: [], calls: [20160] };
  S[19869] = function (string0) {
    if (R.eq(R.call(20160, []), 1)) {
      var string0 = `${R.s(string0)}<br><br>Casts remaining: <col=ffffff>Unlimited</col>`;
    } else {
      string0 = `${R.s(string0)}<br><br>Casts remaining: <col=ffffff>${R.s(R.strLoc(R.vb(57684), 1))}</col>`;
    }
    return string0;
  };
  V[19887] = { vb: [], vp: [], vc: [], calls: [12477] };
  S[19887] = function (int0, int1) {
    if ((R.call(12477, []) < int0)) {
      return 1;
    }
    if ((R.eq(int0, R.call(12477, [])) && (R.mod(R.dateMinutes(), 1440) < int1))) {
      return 1;
    }
    return 0;
  };
  V[19973] = { vb: [], vp: [], vc: [], calls: [] };
  S[19973] = function (string0) {
    var string0 = `${R.s(string0)}- Grants <col=ffffff>${R.s(R.str(R.idiv(150, 10), 10))}%-${R.s(R.str(((R.idiv(150, 10)) + (R.idiv(100, 10)) | 0), 10))}%</col> <col=ffffff>Critical Strike Damage</col>.`;
    return string0;
  };
  V[19974] = { vb: [], vp: [], vc: [], calls: [] };
  S[19974] = function (string0) {
    var string0 = `${R.s(string0)}- Your <col=FFA11A>Melee attack</col> damage is increased by ${R.s(R.str(12, 10))}%</col> against your <sprite=34998><nbsp><col=ffffff><col=ffffff>Flamebound Rival</col>.`;
    string0 = `${R.s(string0)}<br>- You take <col=ffffff>${R.s(R.str(12, 10))}%</col> reduced damage from your <sprite=34998><nbsp><col=ffffff>Flamebound Rival</col>.`;
    return string0;
  };
  V[19975] = { vb: [], vp: [], vc: [], calls: [7653] };
  S[19975] = function (string0) {
    var int0 = 20;
    var string0 = `${R.s(string0)}- Attacks heal you for <col=ffffff>${R.s(R.call(7653, [int0, 1, 1, 1, 1]))}%</col> of the damage dealt.`;
    return string0;
  };
  V[19979] = { vb: [], vp: [], vc: [], calls: [] };
  S[19979] = function (int0, string0) {
    var int1 = 25;
    var string0 = `${R.s(string0)}<br>- Reduces damage dealt by <col=ffffff>${R.s(R.str(int1, 10))}%</col>.`;
    return string0;
  };
  V[20004] = { vb: [], vp: [], vc: [], calls: [] };
  S[20004] = function (int0, int1) {
    if (R.eq(R.itemCategory(int1), 5568)) {
      return R.scale(int0, 100, 30);
    }
    return int0;
  };
  V[20005] = { vb: [], vp: [], vc: [], calls: [20004] };
  S[20005] = function (int0, int1) {
    return R.call(20004, [R.itemParam(int1, 9298), int0]);
  };
  V[20006] = { vb: [], vp: [], vc: [], calls: [] };
  S[20006] = function (int0) {
    var int1 = 5;
    if (R.eq(R.itemCategory(int0), 5571)) {
      int1 = 3;
    }
    if (((R.itemParam(int0, 2645) >= 100) || (R.itemParam(int0, 2645) >= 100))) {
      if ((R.statBase(12) >= R.structParam(52876, 2212))) {
        int1 = ((int1) + (2) | 0);
      } else {
        if ((R.statBase(12) >= R.structParam(52875, 2212))) {
          int1 = ((int1) + (1) | 0);
        }
      }
    }
    return int1;
  };
  V[20007] = { vb: [], vp: [], vc: [], calls: [] };
  S[20007] = function (int0) {
    var int1 = R.itemParam(int0, 2645);
    if (R.eq(R.mod(int1, 2), 0)) {
      int1 = ((int1) + (9) | 0);
    }
    return int1;
  };
  V[20058] = { vb: [], vp: [12251, 12252, 12253, 12254, 12255, 12256], vc: [], calls: [] };
  S[20058] = function (int0) {
    switch (R.key(int0)) {
      case 51829:
      {
        return R.vp(12251);
      }
      case 51830:
      {
        return R.vp(12252);
      }
      case 51831:
      {
        return R.vp(12253);
      }
      case 51832:
      {
        return R.vp(12254);
      }
      case 51833:
      {
        return R.vp(12255);
      }
      case 51834:
      {
        return R.vp(12256);
      }
    }
    return 0;
  };
  V[20059] = { vb: [], vp: [], vc: [], calls: [20058] };
  S[20059] = function (int0) {
    if ((R.dateMinutes() > R.call(20058, [int0]))) {
      return 0;
    }
    return 1;
  };
  V[20066] = { vb: [], vp: [12289], vc: [], calls: [] };
  S[20066] = function () {
    var int0 = 10;
    if (R.eq(R.vp(12289), 1)) {
      int0 = ((int0) + (5) | 0);
    }
    return int0;
  };
  V[20085] = { vb: [], vp: [12289], vc: [], calls: [] };
  S[20085] = function () {
    var int0 = 20;
    if (R.eq(R.vp(12289), 1)) {
      int0 = ((int0) + (10) | 0);
    }
    return int0;
  };
  V[20086] = { vb: [], vp: [], vc: [], calls: [] };
  S[20086] = function () {
    return 4;
  };
  V[20090] = { vb: [], vp: [], vc: [], calls: [17709, 17720, 18561] };
  S[20090] = function (string0) {
    var string0 = `${R.s(string0)}- <col=ffffff>On killing blow:</col> ${R.s(R.call(17720, [49532, 65, ((65) + (10) | 0), 0, 0]))}</col> to up to ${R.s(R.call(18561, [4]))} within ${R.s(R.call(17709, [6]))} of the target.`;
    return string0;
  };
  V[20091] = { vb: [], vp: [], vc: [], calls: [20086] };
  S[20091] = function (string0) {
    var string0 = `${R.s(string0)}- <sprite=30015><nbsp><col=ffffff>${R.s(R.structParam(48293, 2794))}:</col> Generates <col=ffffff>1 <sprite=35069><nbsp>${R.s(R.structParam(52064, 2794))}</col> stack.`;
    string0 = `${R.s(string0)}<br>- At <col=ffffff>${R.s(R.str(R.call(20086, []), 10))}</col> stacks, your <sprite=30015><nbsp><col=ffffff>${R.s(R.structParam(48293, 2794))}</col> is empowered and generates <col=ffffff>${R.s(R.str(1, 10))} <sprite=30123><nbsp>${R.s(R.structParam(48334, 2794))}</col>.`;
    return string0;
  };
  V[20092] = { vb: [], vp: [], vc: [], calls: [] };
  S[20092] = function (string0) {
    var string0 = `${R.s(string0)}- <sprite=34171><nbsp>Vengeful Ghost no longer heals you.`;
    string0 = `${R.s(string0)}<br>- Your <sprite=30212><nbsp><col=ffffff>${R.s(R.structParam(48344, 2794))}</col> damage bonus is increased by <col=ffffff>${R.s(R.str(5, 10))}%</col>.`;
    string0 = `${R.s(string0)}<br>- Your <sprite=30212><nbsp><col=ffffff>${R.s(R.structParam(48344, 2794))}</col> cap is increased by <col=ffffff>${R.s(R.str(10, 10))}%</col>.`;
    return string0;
  };
  V[20096] = { vb: [], vp: [], vc: [], calls: [] };
  S[20096] = function (int0) {
    var string0 = "";
    string0 = `${R.s(string0)}<nbsp><col=ffffff>Item Set:</col> ${R.s(R.structParam(int0, 2794))}`;
    return string0;
  };
  V[20099] = { vb: [], vp: [], vc: [], calls: [17709, 17720, 18561] };
  S[20099] = function (string0) {
    var string0 = `${R.s(string0)}<br><col=ffffff>${R.s(R.structParam(52068, 2794))}</col>`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [49532, 45, ((45) + (10) | 0), 0, 0]))}</col> to up to ${R.s(R.call(18561, [2]))} within ${R.s(R.call(17709, [4]))} of the target.`;
    return string0;
  };
  V[20152] = { vb: [58456], vp: [12314], vc: [], calls: [] };
  S[20152] = function () {
    if (((R.vp(12314) > 0) && (R.vb(58456) > 0))) {
      return 1;
    }
    return 0;
  };
  V[20157] = { vb: [58472], vp: [12314], vc: [], calls: [] };
  S[20157] = function () {
    if (((R.vp(12314) > 0) && (R.vb(58472) > 0))) {
      return 1;
    }
    return 0;
  };
  V[20160] = { vb: [58526], vp: [12314], vc: [], calls: [] };
  S[20160] = function () {
    if (((R.vp(12314) > 0) && (R.vb(58526) > 0))) {
      return 1;
    }
    return 0;
  };
  V[20167] = { vb: [58536], vp: [12314], vc: [], calls: [] };
  S[20167] = function () {
    if (((R.vp(12314) > 0) && R.eq(R.vb(58536), 1))) {
      return 1;
    }
    return 0;
  };
  V[20174] = { vb: [], vp: [], vc: [], calls: [20167] };
  S[20174] = function () {
    if (R.eq(R.call(20167, []), 1)) {
      return 1;
    }
    return R.questFinished(79);
  };
  V[20176] = { vb: [29077], vp: [], vc: [], calls: [20956] };
  S[20176] = function () {
    if (R.eq(R.call(20956, []), 1)) {
      return 1;
    }
    if (R.eq(R.vb(29077), 120)) {
      return 1;
    }
    return 0;
  };
  V[20178] = { vb: [], vp: [], vc: [], calls: [] };
  S[20178] = function (int0) {
    if (R.eq(int0, (-1 | 0))) {
      return 0;
    }
    return R.dbField(int0, 32896, 0);
  };
  V[20395] = { vb: [], vp: [], vc: [], calls: [] };
  S[20395] = function (string0) {
    var string0 = `${R.s(string0)}- Deal <col=ffffff>${R.s(R.str(5, 10))}%</col> additional damage with <col=ffffff>obsidian melee weapons</col>.`;
    return string0;
  };
  V[20396] = { vb: [], vp: [], vc: [], calls: [8944] };
  S[20396] = function (string0) {
    var string0 = `${R.s(string0)}- Deal <col=ffffff>${R.s(R.str(R.call(8944, []), 10))}</col>% additional damage (<col=ffffff>${R.s(R.str(5, 10))}%</col> of your <col=ffffff>Strength</col> level).`;
    return string0;
  };
  V[20397] = { vb: [], vp: [], vc: [], calls: [8945] };
  S[20397] = function (string0) {
    var string0 = `${R.s(string0)}- Deal <col=ffffff>${R.s(R.str(R.call(8945, []), 10))} bonus damage (<col=ffffff>${R.s(R.str(135, 10))}%</col> of your <col=ffffff>Attack</col> level).`;
    return string0;
  };
  V[20451] = { vb: [60446], vp: [], vc: [], calls: [] };
  S[20451] = function (int0) {
    if ((R.eq(int0, 21278) && R.eq(R.vb(60446), 1))) {
      return 0;
    }
    return R.structParam(int0, 3527);
  };
  V[20487] = { vb: [], vp: [], vc: [], calls: [] };
  S[20487] = function (int0, int1, int2) {
    var int3 = 0;
    if (((int2 > 0) && R.eq(R.ifNone(int1, ((int2) - (1) | 0)), 1))) {
      int3 = ((R.ifZero()) + (R.ifZero()) | 0);
    }
    R.ifNoop(96797560, 5, R.ifZero(96797560));
    R.ifNoop(0, 2, 1, 0);
    R.ifNoop(0, ((int3) + (5) | 0), 1, 0);
    R.ifNoop(35516);
    R.ifNoop(int1, 4, ((int2 = (int2 + (1)) | 0) - (1)));
    R.ifNoop(0, 8, 1, 0);
    R.ifNoop(0, int3, 1, 0);
    return int2;
  };
  V[20699] = { vb: [60830], vp: [], vc: [], calls: [] };
  S[20699] = function () {
    var int0 = 0;
    if ((R.vb(60830) > 0)) {
      int0 = 1;
    }
    int0 = ((int0) + (Math.imul(1, R.idiv(R.vb(60830), 10))) | 0);
    int0 = R.max(0, R.min(int0, Math.imul(1, 10)));
    return int0;
  };
  V[20700] = { vb: [60830], vp: [], vc: [], calls: [] };
  S[20700] = function () {
    var int0 = 20;
    if ((R.vb(60830) > 0)) {
      int0 = ((int0) + (3) | 0);
    }
    int0 = ((int0) + (Math.imul(3, R.idiv(R.vb(60830), 10))) | 0);
    int0 = R.max(20, R.min(int0, ((20) + (Math.imul(3, 10)) | 0)));
    return int0;
  };
  V[20701] = { vb: [], vp: [], vc: [], calls: [20699, 20700] };
  S[20701] = function () {
    var int0 = R.call(20699, []);
    var int1 = R.call(20700, []);
    var string0 = `Spirit Plane Connection:<br><br>+${R.s(R.str(int1, 10))}% Summoning XP at obelisks that have a strong connection to the spirit plane.`;
    var string1 = `${R.s(R.str(int0, 10))}% chance to save materials when infusing pouches at these obelisks.`;
    if ((int0 > 0)) {
      string0 = `${R.s(string0)}<br><br>${R.s(string1)}`;
    }
    return string0;
  };
  V[20917] = { vb: [], vp: [12314], vc: [], calls: [] };
  S[20917] = function () {
    if ((R.vp(12314) > 0)) {
      return 1;
    }
    return 0;
  };
  V[20956] = { vb: [58535, 61618], vp: [12314], vc: [], calls: [] };
  S[20956] = function () {
    if ((R.vp(12314) > 0)) {
      if (R.eq(R.vb(58535), 1)) {
        return 1;
      }
      if (R.eq(R.vb(61618), 1)) {
        return 1;
      }
    }
    return 0;
  };
  V[20958] = { vb: [61629], vp: [12314], vc: [], calls: [] };
  S[20958] = function (int0) {
    if (((((R.vp(12314) > 0) && R.eq(R.vb(61629), 1)) && R.eq(R.enumHas(0, 1482, int0), 1)) && (R.statBase(R.enumReverse(0, 17, 1482, int0, 0)) >= 99))) {
      return 1;
    }
    return 0;
  };
  V[20963] = { vb: [61584], vp: [12314], vc: [], calls: [12517] };
  S[20963] = function () {
    if ((((R.vp(12314) > 0) && R.eq(R.vb(61584), 1)) && ((!R.eq(R.call(12517, [93, 63589, 0]), (-1 | 0))) || R.eq(R.invObj(94, 17), 63589)))) {
      return 1;
    }
    return 0;
  };
  V[20966] = { vb: [61593], vp: [12314], vc: [], calls: [] };
  S[20966] = function () {
    if (((R.vp(12314) > 0) && R.eq(R.vb(61593), 1))) {
      return 1;
    }
    return 0;
  };
  V[20967] = { vb: [61596], vp: [12314], vc: [], calls: [] };
  S[20967] = function () {
    if (((R.vp(12314) > 0) && R.eq(R.vb(61596), 1))) {
      return 1;
    }
    return 0;
  };
  V[20971] = { vb: [61604], vp: [12314], vc: [], calls: [] };
  S[20971] = function (int0) {
    if (((R.vp(12314) > 0) && R.eq(R.vb(61604), 1))) {
      var int0 = R.scale(int0, 100, ((100) - (90) | 0));
    }
    return int0;
  };
  V[20977] = { vb: [61621], vp: [12314], vc: [], calls: [12517] };
  S[20977] = function () {
    if ((((R.vp(12314) > 0) && R.eq(R.vb(61621), 1)) && (R.eq(R.invObj(94, 17), 63596) || (!R.eq(R.call(12517, [93, 63596, 0]), (-1 | 0)))))) {
      return 1;
    }
    return 0;
  };
  V[20982] = { vb: [61652], vp: [12314], vc: [], calls: [] };
  S[20982] = function () {
    if ((!R.eq(R.vp(12314), 2))) {
      return 0;
    }
    if (R.eq(R.vb(61652), 1)) {
      return 1;
    }
    return 0;
  };
  V[20986] = { vb: [61648], vp: [12314], vc: [], calls: [] };
  S[20986] = function () {
    if ((!R.eq(R.vp(12314), 2))) {
      return 0;
    }
    if ((R.vb(61648) > 0)) {
      return 1;
    }
    return 0;
  };
  V[20987] = { vb: [61649], vp: [12314], vc: [], calls: [] };
  S[20987] = function () {
    if ((!R.eq(R.vp(12314), 2))) {
      return 0;
    }
    if ((R.vb(61649) > 0)) {
      return 1;
    }
    return 0;
  };
  V[20988] = { vb: [61684], vp: [12314], vc: [], calls: [] };
  S[20988] = function () {
    if ((!R.eq(R.vp(12314), 2))) {
      return 0;
    }
    if (R.eq(R.vb(61684), 1)) {
      return 1;
    }
    return 0;
  };
  V[20992] = { vb: [61670], vp: [12314], vc: [], calls: [] };
  S[20992] = function () {
    if ((R.eq(R.vp(12314), 2) && R.eq(R.vb(61670), 1))) {
      return 1;
    }
    return 0;
  };
  V[20995] = { vb: [], vp: [], vc: [], calls: [20999, 21000] };
  S[20995] = function () {
    if (R.eq(R.call(20999, []), 0)) {
      return 0;
    }
    return Math.imul(500, R.call(21000, []));
  };
  V[20999] = { vb: [61676], vp: [12314], vc: [], calls: [] };
  S[20999] = function () {
    if ((!R.eq(R.vp(12314), 2))) {
      return 0;
    }
    if (R.eq(R.vb(61676), 0)) {
      return 0;
    }
    return 1;
  };
  V[21000] = { vb: [61678, 61679, 61680], vp: [], vc: [], calls: [] };
  S[21000] = function () {
    var int0 = 0;
    if ((R.vb(61678) > 0)) {
      int0 = ((int0) + (1) | 0);
    }
    if ((R.vb(61679) > 0)) {
      int0 = ((int0) + (1) | 0);
    }
    if ((R.vb(61680) > 0)) {
      int0 = ((int0) + (1) | 0);
    }
    return int0;
  };
  V[21002] = { vb: [], vp: [], vc: [], calls: [] };
  S[21002] = function (int0) {
    var int1 = 0;
    if (R.eq(R.enumHas(33, 13250, int0), 1)) {
      int1 = R.enumReverse(33, 0, 13250, int0, 0);
      if (((int1 > (-1 | 0)) && (int1 < 23))) {
        return R.invNum(1011, int1);
      }
    }
    return 0;
  };
  V[21003] = { vb: [], vp: [], vc: [], calls: [21002] };
  S[21003] = function () {
    var int0 = 0;
    var int1 = R.enumCount(13250);
    var int2 = (-1 | 0);
    while ((int0 < int1)) {
      int2 = R.enumValue(0, 33, 13250, int0);
      if ((R.call(21002, [int2]) > 0)) {
        return 1;
      }
      int0 = ((int0) + (1) | 0);
    }
    return 0;
  };
  V[21027] = { vb: [], vp: [711, 3563], vc: [], calls: [] };
  S[21027] = function () {
    return ((R.vp(711)) + (R.vp(3563)) | 0);
  };
  V[21028] = { vb: [], vp: [], vc: [], calls: [20992] };
  S[21028] = function () {
    if (R.eq(R.call(20992, []), 1)) {
      return 0;
    }
    return 30;
  };
  V[21105] = { vb: [], vp: [], vc: [], calls: [7235, 15936] };
  S[21105] = function (int0, int1, int2, int3, int4, int5, int6, string0) {
    if (((!R.eq(R.invObj(int1, int2), int0)) || R.eq(R.invNum(int1, int2), 0))) {
      return int6;
    }
    var int7 = 1010;
    var int8 = ((R.invSize(int7)) - (R.invFree(int7)) | 0);
    var int9 = 0;
    var int10 = 0;
    R.arrDef(0, 84);
    R.arrDef(65536, 84);
    var int11 = 0;
    var int12 = 0;
    var int13 = 0;
    while ((int13 < 84)) {
      int13 = ((int13) + (1) | 0);
      int11 = 0;
      while ((int11 < int8)) {
        int9 = R.invVar(int7, int11, 30215);
        int10 = R.invVar(int7, int11, 30216);
        if (R.eq(int9, int13)) {
          R.arrSet(0, int13, int9);
          R.arrSet(1, int13, R.max(R.arrGet(1, int13), int10));
          int12 = ((int12) + (1) | 0);
        }
        int9 = R.invVar(int7, int11, 30217);
        int10 = R.invVar(int7, int11, 30218);
        if (R.eq(int9, int13)) {
          R.arrSet(0, int13, int9);
          R.arrSet(1, int13, R.max(R.arrGet(1, int13), int10));
          int12 = ((int12) + (1) | 0);
        }
        int9 = R.invVar(int7, int11, 30219);
        int10 = R.invVar(int7, int11, 30220);
        if (R.eq(int9, int13)) {
          R.arrSet(0, int13, int9);
          R.arrSet(1, int13, R.max(R.arrGet(1, int13), int10));
          int12 = ((int12) + (1) | 0);
        }
        int9 = R.invVar(int7, int11, 30221);
        int10 = R.invVar(int7, int11, 30222);
        if (R.eq(int9, int13)) {
          R.arrSet(0, int13, int9);
          R.arrSet(1, int13, R.max(R.arrGet(1, int13), int10));
          int12 = ((int12) + (1) | 0);
        }
        int11 = ((int11) + (1) | 0);
      }
    }
    var int14 = 0;
    var string1 = "";
    var int6 = R.call(7235, [int3, int4, int5, int6, "Stored Perks:", string0]);
    if (R.eq(int12, 0)) {
      int6 = R.call(7235, [int3, int4, int5, int6, "None!", string0]);
      return int6;
    }
    while ((int14 < 84)) {
      if (R.eq(R.enumHas(0, 13420, R.arrGet(0, int14)), 0)) {
        R.arrSet(1, int14, Math.imul(((R.arrGet(1, int14)) + (0) | 0), 2));
      }
      string1 = R.call(15936, [R.arrGet(0, int14), R.arrGet(1, int14)]);
      if ((!R.eq(R.strcmp(string1, ""), 0))) {
        int6 = R.call(7235, [int3, int4, int5, int6, string1, string0]);
      }
      int14 = ((int14) + (1) | 0);
    }
    return int6;
  };
  V[21111] = { vb: [], vp: [4499], vc: [], calls: [17722, 18567] };
  S[21111] = function (int0, int1, int2, int3, int4, int5) {
    var int6 = R.vp(4499);
    if (R.eq(int1, 0)) {
      var int1 = R.call(18567, [int0]);
    }
    var string0 = R.call(17722, [int1, 1]);
    if (R.eq(int4, 1)) {
      string0 = `bonus ${R.s(string0)}`;
    }
    if ((R.eq(int5, 1) && (int6 > 0))) {
      return `<col=ffffff>${R.s(R.strLoc(R.scale(int2, 100, int6), 1))}-${R.s(R.strLoc(((R.scale(int2, 100, int6)) + (R.scale(((int3) - (int2) | 0), 100, int6)) | 0), 1))}</col> (<col=ffffff>${R.s(R.str(int2, 10))}%-${R.s(R.str(int3, 10))}%</col><sprite=14902>) ${R.s(string0)}`;
    }
    return `<col=ffffff>${R.s(R.str(int2, 10))}%-${R.s(R.str(int3, 10))}%</col><sprite=14902> ${R.s(string0)}`;
  };
  V[21112] = { vb: [], vp: [], vc: [], calls: [17722, 18567, 21027] };
  S[21112] = function (int0, int1, int2, int3, int4, int5) {
    var int6 = R.call(21027, []);
    if (R.eq(int1, 0)) {
      var int1 = R.call(18567, [int0]);
    }
    var string0 = R.call(17722, [int1, 1]);
    if (R.eq(int4, 1)) {
      string0 = `bonus ${R.s(string0)}`;
    }
    if ((R.eq(int5, 1) && (int6 > 0))) {
      return `<col=ffffff>${R.s(R.strLoc(R.scale(int2, 100, int6), 1))}-${R.s(R.strLoc(((R.scale(int2, 100, int6)) + (R.scale(((int3) - (int2) | 0), 100, int6)) | 0), 1))}</col> (<col=ffffff>${R.s(R.str(int2, 10))}%-${R.s(R.str(int3, 10))}%</col><sprite=25227>) ${R.s(string0)}`;
    }
    return `<col=ffffff>${R.s(R.str(int2, 10))}%-${R.s(R.str(int3, 10))}%</col><sprite=25227> ${R.s(string0)}`;
  };
  V[21113] = { vb: [], vp: [], vc: [], calls: [17720, 20986, 21111, 21112] };
  S[21113] = function (int0, int1, string0) {
    var int2 = 20;
    var int3 = 80;
    var int4 = 20;
    var int5 = 80;
    if (R.eq(R.call(20986, []), 0)) {
      var string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int2, ((int2) + (int3) | 0), 0, int1]))}.`;
      string0 = `${R.s(string0)}<br>- Deals an additional ${R.s(R.call(21111, [int0, 0, int4, ((int4) + (int5) | 0), 0, int1]))}.`;
    } else {
      int2 = 60;
      int3 = 20;
      int4 = 350;
      int5 = 100;
      string0 = `${R.s(string0)}<br>- ${R.s(R.call(17720, [int0, int2, ((int2) + (int3) | 0), 0, int1]))}.`;
      string0 = `${R.s(string0)}<br>- Deals an additional ${R.s(R.call(21112, [int0, 0, int4, ((int4) + (int5) | 0), 0, int1]))}.`;
    }
    return string0;
  };
  V[21114] = { vb: [], vp: [], vc: [], calls: [17709, 17710, 18561, 20986, 21112] };
  S[21114] = function (int0, int1, string0) {
    var int2 = 50;
    var int3 = 100;
    var int4 = 16;
    var int5 = 8;
    var int6 = 3;
    var int7 = 10;
    var int8 = 5;
    if (R.eq(R.call(20986, []), 0)) {
      var string0 = `${R.s(string0)}<br>- Reduce incoming damage by <col=ffffff>${R.s(R.str(int2, 10))}%</col>.`;
      string0 = `${R.s(string0)}<br>- Reflect <col=ffffff>${R.s(R.str(int3, 10))}%</col> of damage taken back at the attacker.`;
      string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [int4]))}.`;
    } else {
      string0 = `${R.s(string0)}<br>- Reduce incoming damage by <col=ffffff>${R.s(R.str(int2, 10))}%</col>.`;
      string0 = `${R.s(string0)}<br>- Reflect <col=ffffff>${R.s(R.str(int3, 10))}%</col> of incoming damage, plus an additional ${R.s(R.call(21112, [int0, 0, int7, ((int7) + (int8) | 0), 0, int1]))} at the attacker and up to ${R.s(R.call(18561, [int5]))} within ${R.s(R.call(17709, [int6]))} of you.`;
      string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [int4]))}.`;
    }
    return string0;
  };
  V[21115] = { vb: [], vp: [], vc: [], calls: [15973, 17710, 20986] };
  S[21115] = function (int0, int1, string0) {
    var int2 = 16;
    var int3 = 5;
    var int4 = 20;
    if (R.eq(R.call(20986, []), 0)) {
      var string0 = `${R.s(string0)}<br>-\tReduce the cooldown of <sprite=14222><nbsp><col=ffffff>${R.s(R.structParam(14713, 2794))}</col> and <sprite=15035><nbsp><col=ffffff>${R.s(R.structParam(45045, 2794))}</col> by <col=ffffff>${R.s(R.call(15973, [int3, 1]))}</col> each time you are hit by an attack.`;
      string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [int2]))}.`;
    } else {
      string0 = `${R.s(string0)}<br>-\tReduce the cooldown of all other abilities by <col=ffffff>${R.s(R.call(15973, [int4, 1]))}</col>.`;
      string0 = `${R.s(string0)}<br>-\tReduce the cooldown of <sprite=14222><nbsp><col=ffffff>${R.s(R.structParam(14713, 2794))}</col> and <sprite=15035><nbsp><col=ffffff>${R.s(R.structParam(45045, 2794))}</col> by <col=ffffff>${R.s(R.call(15973, [int3, 1]))}</col> each time you are hit by an attack.`;
      string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [int2]))}.`;
    }
    return string0;
  };
  V[21116] = { vb: [], vp: [], vc: [], calls: [17710, 20986] };
  S[21116] = function (int0, int1, string0) {
    var int2 = 32;
    if (R.eq(R.call(20986, []), 1)) {
      int2 = 64;
    }
    var string0 = `${R.s(string0)}<br>-\tGain a stack of <sprite=14227><nbsp><col=ffffff>${R.s(R.structParam(14718, 2794))}</col> each time you are attacked.`;
    string0 = `${R.s(string0)}<br>- ${R.s(R.call(17710, [int2]))}.`;
    return string0;
  };
  V[21117] = { vb: [], vp: [], vc: [], calls: [20986] };
  S[21117] = function () {
    var int0 = 10;
    if (R.eq(R.call(20986, []), 1)) {
      int0 = 20;
    }
    return int0;
  };
  V[21118] = { vb: [16463, 29064, 45364, 46758, 58266], vp: [6524], vc: [], calls: [5803, 16843, 20995] };
  S[21118] = function () {
    var int0 = 0;
    int0 = ((int0) + (R.call(5803, [94])) | 0);
    int0 = ((int0) + (R.vb(16463)) | 0);
    int0 = ((int0) + (R.vb(29064)) | 0);
    int0 = ((int0) + (R.vp(6524)) | 0);
    int0 = ((int0) + (R.vb(45364)) | 0);
    int0 = ((int0) + (R.vb(46758)) | 0);
    int0 = ((int0) + (R.call(16843, [0])) | 0);
    int0 = ((int0) + (R.vb(58266)) | 0);
    int0 = ((int0) + (R.call(20995, [])) | 0);
    return int0;
  };
  V[21120] = { vb: [61657], vp: [12314], vc: [], calls: [20982] };
  S[21120] = function () {
    var int0 = 1000;
    if (R.eq(R.call(20982, []), 1)) {
      int0 = ((int0) + (500) | 0);
    }
    if (R.eq(R.vp(12314), 2)) {
      int0 = ((int0) - (R.vb(61657)) | 0);
    }
    return int0;
  };
  const PARAMS = [3, 4, 13, 21, 258, 277, 368, 369, 396, 485, 641, 642, 643, 667, 686, 741, 742, 743, 744, 745, 746, 747, 748, 749, 750, 751, 752, 753, 754, 755, 756, 757, 758, 759, 760, 761, 770, 771, 772, 773, 774, 775, 776, 777, 778, 779, 780, 781, 823, 963, 965, 972, 1047, 1079, 1324, 1326, 2212, 2231, 2281, 2500, 2524, 2533, 2640, 2645, 2655, 2656, 2657, 2658, 2659, 2660, 2661, 2662, 2663, 2664, 2665, 2666, 2667, 2668, 2669, 2670, 2671, 2672, 2673, 2674, 2675, 2676, 2793, 2794, 2795, 2799, 2800, 2806, 2807, 2821, 2822, 2823, 2824, 2825, 2826, 2827, 2832, 2842, 2853, 2869, 2870, 2879, 2880, 2881, 2946, 2970, 2972, 2976, 3109, 3112, 3200, 3203, 3205, 3206, 3207, 3208, 3209, 3210, 3211, 3212, 3213, 3214, 3215, 3216, 3217, 3218, 3219, 3220, 3221, 3222, 3223, 3224, 3225, 3226, 3227, 3228, 3229, 3248, 3267, 3382, 3384, 3385, 3527, 3674, 3675, 3683, 3697, 3698, 3699, 3702, 3740, 3793, 3824, 3825, 3826, 3827, 3828, 3829, 3830, 3831, 3832, 3999, 4029, 4079, 4080, 4081, 4085, 4205, 4244, 4245, 4328, 4329, 4333, 4338, 4342, 4347, 4351, 4363, 4367, 4371, 4397, 4535, 4552, 4563, 4596, 4653, 4688, 4689, 4690, 4744, 4797, 4798, 4910, 4911, 4943, 4944, 4945, 4946, 4947, 4949, 5094, 5095, 5117, 5118, 5119, 5124, 5125, 5126, 5127, 5128, 5129, 5130, 5131, 5132, 5133, 5134, 5135, 5136, 5137, 5138, 5155, 5196, 5385, 5386, 5387, 5416, 5439, 5451, 5452, 5453, 5454, 5455, 5456, 5457, 5458, 5459, 5460, 5461, 5462, 5463, 5464, 5465, 5466, 5467, 5468, 5469, 5470, 5471, 5472, 5473, 5474, 5475, 5505, 5506, 5507, 5508, 5509, 5510, 5511, 5512, 5513, 5514, 5515, 5524, 5527, 5541, 5542, 5550, 5551, 5553, 5559, 5560, 5561, 5562, 5563, 5564, 5565, 5566, 5567, 5568, 5569, 5570, 5571, 5572, 5573, 5683, 5684, 5685, 5722, 5772, 5777, 5778, 5814, 5815, 5816, 5817, 5818, 5819, 5820, 5821, 5822, 5823, 5824, 5825, 5826, 6046, 6047, 6048, 6049, 6050, 6051, 6052, 6053, 6054, 6055, 6170, 6186, 6295, 6296, 6390, 6527, 6528, 6599, 6600, 6608, 6616, 6620, 6621, 6622, 6623, 6624, 6625, 6626, 6627, 6663, 6810, 6833, 6834, 6835, 6845, 6846, 6847, 6848, 6880, 6894, 6924, 7176, 7226, 7382, 7393, 7452, 7456, 7487, 7489, 7595, 7596, 7601, 7602, 7603, 7604, 7605, 7606, 7607, 7608, 7609, 7610, 7611, 7612, 7613, 7614, 7615, 7616, 7617, 7618, 7619, 7620, 7621, 7622, 7623, 7624, 7625, 7626, 7628, 7629, 7630, 7631, 7632, 7633, 7634, 7635, 7636, 7637, 7638, 7639, 7640, 7641, 7642, 7643, 7644, 7645, 7646, 7647, 7648, 7649, 7650, 7651, 7652, 7653, 7654, 7656, 7657, 7658, 7659, 7660, 7661, 7662, 7663, 7664, 7665, 7666, 7667, 7668, 7669, 7670, 7671, 7672, 7673, 7674, 7675, 7676, 7677, 7678, 7679, 7680, 7681, 7682, 7683, 7684, 7685, 7686, 7706, 7710, 7711, 7712, 7713, 7714, 7715, 7716, 7717, 7718, 7719, 7720, 7721, 7722, 7723, 7724, 7725, 7726, 7727, 7728, 7729, 7730, 7731, 7732, 7733, 7735, 7736, 7796, 7801, 7802, 7804, 7806, 7807, 7828, 7863, 7864, 7897, 7998, 8116, 8119, 8161, 8162, 8163, 8229, 8284, 8367, 8368, 8369, 8370, 8371, 8372, 8373, 8374, 8375, 8376, 8377, 8378, 8379, 8380, 8381, 8382, 8383, 8384, 8385, 8386, 8387, 8388, 8389, 8390, 8391, 8392, 8393, 8394, 8395, 8396, 8397, 8398, 8399, 8400, 8401, 8402, 8403, 8404, 8405, 8406, 8407, 8408, 8409, 8410, 8411, 8412, 8413, 8414, 8415, 8416, 8417, 8418, 8419, 8420, 8421, 8422, 8423, 8424, 8425, 8426, 8427, 8428, 8429, 8430, 8431, 8432, 8433, 8434, 8435, 8436, 8437, 8438, 8439, 8440, 8441, 8442, 8443, 8444, 8445, 8446, 8447, 8448, 8449, 8450, 8451, 8452, 8453, 8454, 8455, 8456, 8457, 8458, 8459, 8460, 8461, 8462, 8463, 8464, 8465, 8466, 8467, 8468, 8469, 8470, 8471, 8472, 8473, 8474, 8475, 8476, 8477, 8478, 8479, 8480, 8481, 8482, 8483, 8484, 8485, 8486, 8487, 8488, 8489, 8490, 8491, 8492, 8493, 8494, 8495, 8496, 8497, 8498, 8499, 8500, 8501, 8502, 8503, 8504, 8505, 8506, 8507, 8508, 8509, 8510, 8511, 8512, 8513, 8514, 8515, 8516, 8517, 8518, 8563, 8569, 8571, 8572, 8591, 8592, 8605, 8684, 8695, 8696, 8697, 8727, 8739, 8773, 8774, 8839, 8840, 8879, 8881, 8897, 8898, 8899, 8902, 8906, 8910, 8917, 8928, 8929, 8936, 8937, 8938, 8940, 8941, 8942, 8943, 9001, 9003, 9087, 9089, 9090, 9091, 9092, 9118, 9119, 9122, 9123, 9218, 9241, 9242, 9298, 9300, 9304, 9308, 9345, 9405, 9407, 9432, 9433, 9473];
  window.GAME_TEXT = { scripts: S, vars: V, roots: { buff: 11088, item: 5828 }, params: PARAMS, unsupported: { buff: {}, item: {"script 10761": "needs script 13099", "script 12197": "needs script 11024"} }, bind: function (rt) { R = rt; } };
})();
