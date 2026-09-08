#pragma once

#include "SqliteIndexFile.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace rtx::cache {

// Registry of opened jcache indexes, keyed by numeric index id.
class Store {
public:
    explicit Store(std::string cache_root)
        : cache_root_(std::move(cache_root)) {}

    // Opens `<cache_root>/js5-<index_id>.jcache` and parses the reference table; false on failure.
    bool Add(int index_id, int default_files_per_archive);

    SqliteIndexFile* Get(int index_id) const;

private:
    std::string cache_root_;
    std::unordered_map<int, std::unique_ptr<SqliteIndexFile>> indexes_;
};

}  // namespace rtx::cache
