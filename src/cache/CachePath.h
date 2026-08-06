#pragma once
//
// Where is the RS3 cache?
//
// There is no single answer. The Jagex Launcher keeps it under ProgramData, the Steam build
// keeps it inside its own install, and the user can move it. The client itself records the
// answer, so that is what gets read rather than guessed:
//
//     <launcher dir>\preferences.cfg  ->  cache_folder=<dir>      and the cache is <dir>\RuneScape
//
// Each install has its OWN preferences.cfg, so both are consulted and the one actually in use
// wins. Observed on a machine with both:
//     Jagex : cache_folder=C:\ProgramData\Jagex                       -> C:\ProgramData\Jagex\RuneScape
//     Steam : cache_folder=...\steamapps\common\RuneScape             -> ...\RuneScape\RuneScape
//
// (Loader.cpp has its own Steam-library walk for finding RuneScape.exe. It is deliberately not
// shared: that one is a launcher concern and this is a cache concern, and src/cache must not
// depend upward on src/launcher.)

#include <string>
#include <vector>

namespace rtx::cache {

// One place the cache might be, and what was found there.
struct CacheCandidate {
    std::string path;        // the cache directory itself (the one holding js5-*.jcache)
    std::string source;      // how it was found, for diagnostics
    int         archives = 0;    // js5-*.jcache files present
    long long   newest   = 0;    // newest js5 write time; opaque units, only
                                 // meaningful compared against other candidates
    bool        usable   = false;
};

// Every candidate considered, in discovery order, each probed. Ordering here is NOT the
// preference order - see ResolveCacheRoot. Intended for diagnostics ("which cache am I on?").
std::vector<CacheCandidate> CacheCandidates();

// The cache directory to read, memoized after the first call.
//
// Prefers, among the usable candidates, the most RECENTLY written one: with two installs on a
// machine only one is being played, and that is the one whose archives the client is updating.
// An explicit RTX_CACHE_DIR always wins. Returns kDefaultCacheRoot if nothing usable is found,
// so behaviour never gets worse than before this existed.
const std::string& ResolveCacheRoot();

}  // namespace rtx::cache
