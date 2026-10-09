// Achievement definitions from the live cache. Index 57: archive = id >> 7, file = id & 0x7f.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rtx::cache {

// All achievements as a JSON array, built once and cached. Complete when every op-14 req
// (live varbit >= value) is satisfied. "[]" if the cache index is unavailable.
const std::string& AchievementsJson();

// Names of the Quest Cape's (achievement id 1) sub-achievements = the current quest set.
// Caller MUST hold AchievementsMutex; does not lock.
void QuestCapeQuestNames(std::vector<std::string>& out);

// Parse-health decode of one record: 0 = clean end, 1..255 = that opcode is unknown, kStop*
// (InputStream.h) = the record did not end the way a clean one does. `last` receives the last
// opcode read before the stop (-1 when none). Pure: reads only `bytes`, no lock needed.
int AchievementDecodeStop(const std::vector<std::uint8_t>& bytes, int& last);
// Unknown-opcode probe (Probe.h); appends a report to `log`. Caller holds the cache mutex.
void AchievementsProbeUnknown(std::string& log);

}  // namespace rtx::cache
