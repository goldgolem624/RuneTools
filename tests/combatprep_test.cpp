// Upload body preparation (src/launcher/CombatPrep.h) on a built-in synthetic log.
// Optional: combatprep_test <in.json> <out.json> [keep] writes the prepared form of an inflated log;
// combatprep_test --synthetic <out.json> writes the built-in log.
#include "../src/launcher/CombatPrep.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace cp = rtx::launcher::combatprep;

static int fails = 0;
static void check(bool c, const char* what) {
    std::printf("%-66s %s\n", what, c ? "ok" : "FAIL");
    if (!c) ++fails;
}
static bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

static const char* kSchema =
    "{\"0\":[\"hit\",\"actor\",\"hm\",\"value\",\"hm2\",\"value2\",\"delay\"],\"1\":[\"cast\",\"struct\",\"ready\",\"src\"],"
    "\"2\":[\"anim\",\"actor\",\"seq\"],\"3\":[\"target\",\"actor\",\"target\"],\"4\":[\"lp\",\"actor\",\"lp\",\"lpMax\"],\"5\":[\"adren\",\"value\"],"
    "\"6\":[\"prayer\",\"points\",\"level\"],\"7\":[\"buff\",\"struct\",\"on\",\"start\",\"end\",\"stacks\"],\"8\":[\"channel\",\"side\",\"ticks\",\"name\"],"
    "\"9\":[\"tracker\",\"group\",\"row\",\"col\",\"value\"],\"10\":[\"death\",\"actor\",\"how\"],\"11\":[\"actor\",\"actor\",\"present\"],"
    "\"12\":[\"encounter\",\"struct\"],\"13\":[\"gfx\",\"actor\",\"gfx\"],\"14\":[\"proj\",\"from\",\"to\",\"gfx\"],\"15\":[\"xp\",\"skill\",\"xp\"],"
    "\"16\":[\"mark\",\"kind\",\"text\"],\"17\":[\"bar\",\"actor\",\"slot\",\"fill\"],\"18\":[\"stat\",\"actor\",\"idx\",\"cur\",\"base\"],"
    "\"19\":[\"sound\",\"id\",\"area\"],\"mech\":[\"mech\",\"boss\",\"key\",\"kind\",\"id\",\"actor\"]}";

static std::string synthetic() {
    std::string o;
    o += "{\"format\":1,\"log\":{\"id\":\"AbCdEfGhIjKlMnOpQrSt_-\",\"character\":\"Hero One\",\"launcher\":\"3.6.0\",\"client\":\"950-1\","
         "\"startedAt\":1791572054026,\"endedAt\":1791572165854,\"endBy\":\"stop\",\"anonymised\":false,\"companion\":false,\"readFails\":0,\"gaps\":0},";
    o += "\"clock\":{\"c0\":1000,\"wall0\":1791572054026,\"phase\":5,\"tick0\":-1},\"actors\":[";
    const char* actors[] = {
        "{\"i\":0,\"type\":\"player\",\"uid\":10,\"id\":-1,\"name\":\"Zed Alpha\",\"first\":1000,\"last\":1100,\"lpMax\":-1,\"vis\":-1}",
        "{\"i\":1,\"type\":\"self\",\"uid\":11,\"id\":-1,\"name\":\"Hero One\",\"first\":1000,\"last\":1100,\"lpMax\":9900,\"vis\":-1}",
        "{\"i\":2,\"type\":\"player\",\"uid\":12,\"id\":-1,\"name\":\"Bo_Beta\",\"first\":1000,\"last\":1100,\"lpMax\":-1,\"vis\":-1}",
        "{\"i\":3,\"type\":\"npc\",\"uid\":20,\"id\":30265,\"name\":\"combatv2_spirit_skeleton\",\"first\":1000,\"last\":1100,\"lpMax\":-1,\"vis\":-1}",
        "{\"i\":4,\"type\":\"npc\",\"uid\":21,\"id\":30265,\"name\":\"Skeleton \\u0057arrior\",\"first\":1000,\"last\":1100,\"lpMax\":-1,\"vis\":-1}",
        "{\"i\":5,\"type\":\"npc\",\"uid\":22,\"id\":4242,\"name\":\"boss_spawn_thing\",\"first\":1000,\"last\":1100,\"lpMax\":-1,\"vis\":-1}",
        "{\"i\":6,\"type\":\"npc\",\"uid\":23,\"id\":5000,\"name\":\"Zed Alpha's pet\",\"first\":1000,\"last\":1100,\"lpMax\":-1,\"vis\":-1}",
        "{\"i\":7,\"type\":\"npc\",\"uid\":24,\"id\":6000,\"name\":\"SOME_DEV_NAME\",\"first\":1000,\"last\":1100,\"lpMax\":-1,\"vis\":-1}",
        "{\"i\":8,\"type\":\"npc\",\"uid\":25,\"id\":30266,\"name\":\"x_y\",\"first\":1000,\"last\":1100,\"lpMax\":-1,\"vis\":-1}",
        "{\"i\":9,\"type\":\"npc\",\"uid\":26,\"id\":-1,\"name\":\"lone_spawn\",\"first\":1000,\"last\":1100,\"lpMax\":-1,\"vis\":-1}",
        "{\"i\":10,\"type\":\"npc\",\"uid\":27,\"id\":7000,\"name\":\"Zed Alphabet\",\"first\":1000,\"last\":1100,\"lpMax\":-1,\"vis\":-1}",
        "{\"i\":11,\"type\":\"player\",\"uid\":13,\"id\":-1,\"name\":\"Player 7\",\"first\":1000,\"last\":1100,\"lpMax\":-1,\"vis\":-1}" };
    for (std::size_t i = 0; i < sizeof(actors) / sizeof(actors[0]); ++i) { if (i) o += ","; o += actors[i]; }
    o += "],\"dict\":{\"abilities\":{\"14881\":{\"name\":\"Global cooldown\",\"icon\":0,\"style\":\"\",\"cd\":0,\"varc\":0},"
         "\"48298\":{\"name\":\"Soul Sap\",\"icon\":30080,\"style\":\"necromancy\",\"cd\":0,\"varc\":0,\"anim\":35461},"
         "\"20000\":{\"name\":\"Greater  Flurry (melee)\",\"icon\":1,\"style\":\"melee\",\"cd\":20,\"varc\":0},"
         "\"20001\":{\"name\":\"Wild Magic\",\"icon\":2,\"style\":\"magic\",\"cd\":20,\"varc\":0},"
         "\"20002\":{\"name\":\"\",\"icon\":0,\"style\":\"\",\"cd\":0,\"varc\":0}},"
         "\"buffs\":{\"100\":{\"name\":\"Overload\",\"type\":0,\"icon\":1},\"101\":{\"name\":\"BUFF_DEV_THING\",\"type\":0,\"icon\":2}},"
         "\"hitmarks\":{\"142\":{\"kind\":\"poison\",\"other\":false,\"crit\":false,\"name\":\"HM_GREEN_SPLAT\"},"
         "\"477\":{\"kind\":\"necromancy\",\"other\":false,\"crit\":false,\"name\":\"HM_GREY_SPLAT\"},"
         "\"500\":{\"kind\":\"magic\",\"other\":false,\"crit\":true}},"
         "\"seqs\":{\"100\":\"SEQ_FLURRY_ATTACK\",\"101\":\"NPC_THING_DEATH\",\"102\":\"WILD_MAGIC_CAST\",\"103\":\"SOUL_SAPPER\","
         "\"104\":\"X_SOUL_SAP\",\"105\":\"DEATHLY_ATTACKER\",\"106\":\"COMMAND_GHOST\",\"107\":\"GLOBAL_COOLDOWN_X\",\"35461\":\"\"},"
         "\"encounters\":{\"8849\":\"Arch-Glacor\",\"8850\":\"ENC_DEV_NAME\"},"
         "\"trackers\":{\"0\":{\"name\":\"Combat\",\"cols\":{\"0\":\"Damage\",\"1\":\"COL_DEV_NAME\"}}},"
         "\"mechs\":{\"30000\":{\"frost_beam\":{\"label\":\"Frost beam\",\"tactic\":1,\"kind\":1},\"x_y\":{\"label\":\"MECH_DEV\",\"tactic\":0,\"kind\":2}}}},";
    o += "\"fights\":[{\"n\":0,\"start\":1000,\"end\":1100,\"startMs\":1791572054026,\"kind\":\"kills\",\"boss\":null,\"targets\":[3,5],\"kills\":1,"
         "\"deaths\":0,\"startBy\":\"cast\",\"endBy\":\"stop\",\"summary\":{\"durMs\":2000,\"dealt\":4935,\"dps\":2467.50}}],";
    o += "\"schema\":"; o += kSchema; o += ",";
    o += "\"events\":[[11,1000,0,1],\n[0,1001,3,477,4925,-1,-1,12],\n[16,1002,0,\"cast\"],\n[16,1003,2,\"Zed Alpha joined\"],\n"
         "[8,1004,1,4,\"Bo Beta's channel\"],\n[15,1005,3,123456],\n[16,1006,1,\"MARK_DEV_TEXT\"],\n[\"mech\",1007,30000,\"frost_beam\",1,1234,5],\n"
         "[0, 1008,  3,477,10,-1,-1,0],\n[16,1009,3,\"Zed Alphabet soup\"],\n[8,1010,1,4,\"Soul Sap\"]]}\n";
    return o;
}

static std::string slurp(const char* p) { std::ifstream f(p, std::ios::binary); std::ostringstream ss; ss << f.rdbuf(); return ss.str(); }

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--synthetic") == 0) {   // the built-in log, for checks in other tools
        std::ofstream(argv[2], std::ios::binary | std::ios::trunc) << synthetic();
        return 0;
    }
    if (argc >= 3) {
        const std::string in = slurp(argv[1]);
        cp::Options opt; opt.keepNames = argc >= 4 && std::strcmp(argv[3], "keep") == 0;
        std::string out, err; cp::Stats st;
        if (!cp::Prepare(in, opt, out, st, err)) { std::printf("prepare failed: %s\n", err.c_str()); return 1; }
        std::ofstream(argv[2], std::ios::binary | std::ios::trunc) << out;
        std::printf("players=%d npcFixed=%d seqDropped=%d hitmarkNamesDropped=%d seqinfo=%d devNames=%d nameStrings=%d xpDropped=%d events=%lld\n",
                    st.playersRenamed, st.npcNamesFixed, st.seqNamesDropped, st.hitmarkNamesDropped, st.seqinfo, st.devNamesDropped,
                    st.nameStringsCleared, st.xpDropped, st.events);
        return 0;
    }

    // the token rule
    check(cp::Token("Greater Flurry") == "FLURRY", "token drops a leading GREATER");
    check(cp::Token("Greater  Flurry (melee)") == "FLURRY", "token drops (...) groups and trims");
    check(cp::Token("Greaterflurry") == "GREATERFLURRY", "GREATER needs a space after it");
    check(cp::Token("Touch of Death") == "TOUCH_OF_DEATH", "token joins words with _");
    check(cp::Token("a (b) c (d") == "A_C_D", "an unclosed ( stays text");
    check(cp::Token("Kal'gerion  demon!") == "KAL_GERION_DEMON", "runs of other characters are one _");
    check(cp::Token("(x)").empty() && cp::Token("").empty(), "empty tokens");

    const std::string src = synthetic();
    std::string out, err; cp::Stats st;
    check(cp::Prepare(src, cp::Options{}, out, st, err), "prepares the synthetic log");
    check(st.playersRenamed == 3, "three players renamed");
    check(has(out, "\"name\":\"Player 1\"") && has(out, "\"name\":\"Player 2\"") && has(out, "\"name\":\"Player 3\""), "players named Player 1..3 in index order");
    check(has(out, "{\"i\":0,\"type\":\"player\",\"uid\":10,\"id\":-1,\"name\":\"Player 1\""), "actor 0 is Player 1");
    check(has(out, "{\"i\":2,\"type\":\"player\",\"uid\":12,\"id\":-1,\"name\":\"Player 2\""), "actor 2 is Player 2");
    check(has(out, "{\"i\":11,\"type\":\"player\",\"uid\":13,\"id\":-1,\"name\":\"Player 3\""), "actor 11 (was Player 7) is Player 3");
    check(has(out, "\"name\":\"Hero One\",\"first\"") && has(out, "\"character\":\"Hero One\""), "self and log.character untouched");
    check(!has(out, "Zed Alpha\"") && !has(out, "Bo_Beta"), "no other player name left as a name");
    check(has(out, "\"id\":30265,\"name\":\"Skeleton \\u0057arrior\",\"first\":1000,\"last\":1100,\"lpMax\":-1,\"vis\":-1},{\"i\":4"), "spawn name takes the sibling's name, as written");
    check(has(out, "\"id\":4242,\"name\":\"NPC 4242\""), "spawn name without sibling is NPC <id>");
    check(has(out, "\"id\":30266,\"name\":\"Putrid Zombie\""), "conjure spawn without sibling gets the conjure name");
    check(has(out, "\"id\":-1,\"name\":\"NPC\""), "spawn name with id -1 is NPC");
    check(has(out, "\"id\":5000,\"name\":\"NPC 5000\""), "NPC name holding a player name is NPC <id>");
    check(has(out, "\"id\":7000,\"name\":\"Zed Alphabet\""), "a longer word is not a player name");
    check(has(out, "\"id\":6000,\"name\":\"\""), "developer-shaped NPC name blanked");
    check(st.npcNamesFixed == 5, "five NPC names fixed");
    check(has(out, "\"seqs\":{}"), "seqs emptied");
    check(st.seqNamesDropped == 9, "nine seq keys dropped");
    check(!has(out, "SEQ_FLURRY") && !has(out, "_SPLAT") && !has(out, "NPC_THING"), "no seq or hitmark name left");
    check(st.hitmarkNamesDropped == 2, "two hitmark names dropped");
    check(has(out, "\"142\":{\"kind\":\"poison\",\"other\":false,\"crit\":false}"), "hitmark keeps kind, other, crit");
    check(has(out, "\"500\":{\"kind\":\"magic\",\"other\":false,\"crit\":true}"), "hitmark without a name unchanged");
    const char* seqinfo = "\"seqinfo\":{\"100\":{\"ab\":[20000],\"tag\":1},\"101\":{\"ab\":[],\"tag\":2},\"102\":{\"ab\":[20001],\"tag\":0},"
                          "\"104\":{\"ab\":[48298],\"tag\":0},\"105\":{\"ab\":[],\"tag\":1},\"106\":{\"ab\":[-3],\"tag\":0},"
                          "\"107\":{\"ab\":[14881,14882],\"tag\":0},\"35461\":{\"ab\":[48298],\"tag\":0}}}";
    check(has(out, seqinfo), "seqinfo exactly as the rule, last in dict");
    check(st.seqinfo == 8, "eight seqinfo entries");
    check(has(out, "\"101\":{\"name\":\"\",\"type\":0,\"icon\":2}"), "developer-shaped buff name blanked");
    check(has(out, "\"8850\":\"\""), "developer-shaped encounter blanked");
    check(has(out, "\"cols\":{\"0\":\"Damage\",\"1\":\"\"}"), "developer-shaped tracker col blanked");
    check(has(out, "\"x_y\":{\"label\":\"\",\"tactic\":0,\"kind\":2}"), "developer-shaped mech label blanked");
    check(has(out, "\"frost_beam\":{\"label\":\"Frost beam\",\"tactic\":1,\"kind\":1}"), "plain mech label kept");
    check(st.devNamesDropped == 6, "six developer-shaped names blanked");
    {
        const std::string from = "\"kind\":\"kills\",\"boss\":null";
        const std::size_t at = src.find(from);
        check(at != std::string::npos, "synthetic fight found");
        const char* cases[][2] = { { "BOSS_DEV_NAME_X", "null" }, { "Arch-Glacor & SOME_DEV|Hard mode", "null" },
                                   { "Arch-Glacor|HARD_MODE", "null" }, { "", "null" }, { "Arch-Glacor|Hard mode", "\"Arch-Glacor|Hard mode\"" } };
        for (const auto& c : cases) {
            std::string in = src;
            in.replace(at, from.size(), std::string("\"kind\":\"encounter\",\"boss\":\"") + c[0] + "\"");
            std::string o2, e2; cp::Stats s2;
            const bool ok = cp::Prepare(in, cp::Options{}, o2, s2, e2);
            check(ok && has(o2, (std::string("\"kind\":\"encounter\",\"boss\":") + c[1]).c_str()), (std::string("fight boss ") + c[0]).c_str());
            check(s2.devNamesDropped == 6 + ((c[0][0] && std::strcmp(c[1], "null") == 0) ? 1 : 0), (std::string("dev count for boss ") + c[0]).c_str());
        }
    }
    check(!has(out, "[15,"), "xp row removed");
    check(st.xpDropped == 1, "one xp row counted");
    check(has(out, "[16,1003,2,\"\"]"), "mark text holding a player name cleared");
    check(has(out, "[8,1004,1,4,\"\"]"), "channel name holding a player name (folded) cleared");
    check(has(out, "[16,1006,1,\"\"]"), "developer-shaped mark text cleared");
    check(has(out, "[16,1009,3,\"Zed Alphabet soup\"]"), "mark text with a longer word kept");
    check(st.nameStringsCleared == 2, "two texts cleared for names");
    check(has(out, "[0, 1008,  3,477,10,-1,-1,0]"), "other events copied byte for byte");
    check(has(out, "[\"mech\",1007,30000,\"frost_beam\",1,1234,5]"), "mech rows copied");
    check(st.events == 10, "ten events written");
    check(has(out, "\"anonymised\":true"), "log.anonymised true");
    check(has(out, "\"summary\":{\"durMs\":2000,\"dealt\":4935,\"dps\":2467.50}"), "fight summary kept as written");
    check(out.find("{\"format\":1,\"log\":") == 0 && has(out, "},\"clock\":{\"c0\":1000") && has(out, "],\"dict\":{\"abilities\":") &&
          has(out, "}}],\"schema\":") && has(out, ",\"events\":["), "key order kept");
    check(has(out, std::string("\"schema\":") + kSchema + ","), "schema copied");

    // keep names
    std::string kept; cp::Stats ks; cp::Options keep; keep.keepNames = true;
    check(cp::Prepare(src, keep, kept, ks, err), "prepares with keepNames");
    check(ks.playersRenamed == 0 && has(kept, "\"name\":\"Zed Alpha\"") && has(kept, "\"name\":\"Bo_Beta\"") && has(kept, "\"name\":\"Player 7\""), "keepNames keeps player names");
    check(has(kept, "\"name\":\"Zed Alpha's pet\""), "keepNames keeps an NPC name holding a player name");
    check(has(kept, "[16,1003,2,\"Zed Alpha joined\"]") && has(kept, "[8,1004,1,4,\"Bo Beta's channel\"]"), "keepNames keeps mark and channel texts");
    check(has(kept, "[16,1006,1,\"\"]") && has(kept, "\"id\":4242,\"name\":\"NPC 4242\""), "keepNames still applies the shape and spawn rules");
    check(has(kept, "\"anonymised\":false") && !has(kept, "[15,") && has(kept, seqinfo), "keepNames: anonymised false, xp gone, same seqinfo");
    check(ks.npcNamesFixed == 4 && ks.nameStringsCleared == 0, "keepNames counts");

    // a prepared log prepares to itself
    std::string again; cp::Stats as;
    check(cp::Prepare(out, cp::Options{}, again, as, err) && again == out, "preparing twice changes nothing");
    check(as.playersRenamed == 0 && as.seqinfo == 8, "second pass keeps the seqinfo");

    // rejects
    std::string bad = src; bad.replace(bad.find("\"format\":1"), 10, "\"format\":2");
    check(!cp::Prepare(bad, cp::Options{}, out, st, err) && err == "not format 1", "rejects format 2");
    bad = src; bad.replace(bad.find("AbCdEfGhIjKlMnOpQrSt_-"), 22, "AbCdEfGhIjKlMnOpQrSt.-");
    check(!cp::Prepare(bad, cp::Options{}, out, st, err) && err == "bad log id", "rejects a bad log id");
    check(!cp::Prepare(src.substr(0, src.size() / 2), cp::Options{}, out, st, err), "rejects cut json");

    // a live head (no events) and chunk lines
    std::string head = src.substr(0, src.find(",\"events\":[")) + ",\"events\":[]}";
    std::string hp; cp::Stats hs;
    check(cp::Prepare(head, cp::Options{}, hp, hs, err) && has(hp, "\"events\":[]") && hs.events == 0, "prepares a head without events");
    const auto names = cp::NamesForFilter(head);
    check(names.size() == 3 && names[0] == "Zed Alpha" && names[1] == "Bo_Beta" && names[2] == "Hero One", "names for the chunk filter");
    std::vector<std::string> lines = { "[16,2000,0,\"cast\"]", "[15,2001,3,999]", "[16,2002,2,\"zed-alpha waves\"]", "[8,2003,1,4,\"Hero One\"]",
                                       "[0,2004,3,477,5,-1,-1,1]", "[16,2005,2,\"SOME_MARK\"]", "[\"mech\",2006,30000,\"frost_beam\",1,1,5]" };
    std::vector<std::string> fl; cp::Stats fs;
    cp::FilterEventLines(lines, names, cp::Options{}, fl, fs);
    check(fl.size() == 6 && fl[0] == lines[0] && fl[1] == "[16,2002,2,\"\"]" && fl[2] == "[8,2003,1,4,\"\"]" && fl[3] == lines[4] &&
          fl[4] == "[16,2005,2,\"\"]" && fl[5] == lines[6], "chunk lines filtered");
    check(fs.xpDropped == 1 && fs.nameStringsCleared == 2 && fs.devNamesDropped == 1 && fs.events == 6, "chunk filter counts");
    std::vector<std::string> fk; cp::Stats fks;
    cp::FilterEventLines(lines, names, keep, fk, fks);
    check(fk.size() == 6 && fk[1] == lines[2] && fk[2] == lines[3] && fk[4] == "[16,2005,2,\"\"]", "chunk lines with keepNames");

    std::printf(fails ? "\n%d check(s) failed\n" : "\nall checks passed\n", fails);
    return fails ? 1 : 0;
}
