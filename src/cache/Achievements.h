// Achievement definitions from the live cache. Index 57: archive = id >> 7, file = id & 0x7f.
#pragma once

#include <string>
#include <vector>

namespace rtx::cache {

// All achievements as a JSON array, built once and cached. Complete when every op-14 req
// (live varbit >= value) is satisfied. "[]" if the cache index is unavailable.
const std::string& AchievementsJson();

// Names of the Quest Cape's (achievement id 1) sub-achievements = the current quest set.
// Caller MUST hold AchievementsMutex; does not lock.
void QuestCapeQuestNames(std::vector<std::string>& out);

// Parse-health sweep: clean-end count and most common stopping opcode (-1 if none).
// Caller MUST hold AchievementsMutex; does not lock.
void AchievementsParseHealth(int& ok, int& total, int& stop_op, int& stop_n);
// Unknown-opcode probe (Probe.h); appends a report to `log`. Caller holds the cache mutex.
void AchievementsProbeUnknown(std::string& log);

}  // namespace rtx::cache
