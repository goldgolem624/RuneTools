#pragma once
// The client offsets the companion reads, found from the client's own code the way the launcher
// finds them (src/reader/Calibrate.cpp, run here on the exe the game was started from). Each value is
// the compiled one until Load() has run, then the client's where a rule proved it. A rule that proves
// nothing leaves the compiled value and Missing() names it, for the health rows.
#include <cstdint>
#include <string>

namespace rtx::coff {

// Root fields
extern std::uint32_t status;       // i32 game state, 30 in the world
extern std::uint32_t account;      // account and local player data
extern std::uint32_t players;      // player registry by world index
extern std::uint32_t sceneViews;   // game world list
extern std::uint32_t varpMgr;      // player var store, embedded
extern std::uint32_t language;     // language index
extern std::uint32_t soundCtx;     // sound context
// Script state fields
extern std::uint32_t strStack, strSp, entityRef, entityObj;

// Runs the rules once (the worker thread calls it before any hook reads an offset). False when no
// rule could run; Why() says why.
bool Load();
bool Loaded();
std::string Why();
std::string Missing();   // names whose rule found nothing, comma separated
std::string Summary();   // one line for the log

}  // namespace rtx::coff
