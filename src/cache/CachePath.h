#pragma once
// Locates the RS3 cache. Each install's <launcher dir>\preferences.cfg records cache_folder=<dir>;
// the cache is <dir>\RuneScape. src/cache must not depend on src/launcher.

#include <string>
#include <vector>

namespace rtx::cache {

// One place the cache might be, and what was found there.
struct CacheCandidate {
    std::string path;        // the cache directory itself (the one holding js5-*.jcache), UTF-8
    std::string source;      // how it was found, for diagnostics
    int         archives = 0;    // js5-*.jcache files present
    long long   newest   = 0;    // newest js5 write time; opaque units, compare only
    bool        usable   = false;
    int         rank     = 1;    // trust, lower first: 0 RTX_CACHE_DIR, 1 named by an install
                                 // (preferences.cfg, Steam, default folders), 2 drive scan
};

// Every candidate probed, in discovery order (not preference order). For diagnostics.
std::vector<CacheCandidate> CacheCandidates();

// The cache directory to read (UTF-8), memoized; never throws. RTX_CACHE_DIR wins, else the most
// recently written usable candidate of the best rank, else kDefaultCacheRoot.
const std::string& ResolveCacheRoot();

}  // namespace rtx::cache
