// Achievement definitions from the live cache (js5-57). Completion is varbit-driven: a
// requirement is satisfied when the live varbit value >= its target. The
// panel/SDK read the varbits live and compare. Index 57: archive = id >> 7, file = id & 0x7f.
#pragma once

#include <string>
#include <vector>

namespace rtx::cache {

// All achievements as a JSON array, built once and cached:
//   [{ "id":N, "name":"..", "desc":"..", "reward":"..", "cat":C, "subcat":S, "sprite":SP,
//      "points":P, "hidden":H, "members":bool, "subach":[ids],
//      "reqs":[{ "value":V, "desc":"..", "varbits":[vb,..] }] }, ..]
// `reqs` are the op-14 completion requirements; an achievement is complete when every req is
// satisfied. Names omitted / partial when a def hits an unknown trailing opcode (kept, not
// dropped -- matches the other cache decoders). "[]" if the cache index is unavailable.
const std::string& AchievementsJson();

// Raw names of the quests required for the Quest Cape (achievement id 1, category 4745) -- the
// authoritative set of CURRENT quests. Decodes the Quest Cape's sub-achievements (op15) and appends
// each one's name to `out`. The quest list uses this to drop legacy/removed quest configs. Caller
// MUST hold the shared cache mutex (AchievementsMutex); does not lock. `out` empty on failure.
void QuestCapeQuestNames(std::vector<std::string>& out);

// Parse-health sweep: decode EVERY achievement def, reporting how many parse to a
// clean end and the most common stopping opcode (stop_op = -1 when none stopped).
// Caller MUST hold the shared cache mutex (AchievementsMutex); does not lock.
void AchievementsParseHealth(int& ok, int& total, int& stop_op, int& stop_n);

}  // namespace rtx::cache
