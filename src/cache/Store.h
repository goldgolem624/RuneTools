#pragma once

#include "SqliteIndexFile.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace rtx::cache {

// Registry of the jcache indexes we open. Each index is identified by its
// numeric id (e.g. 19 for items); add new ones by calling `Add` at boot.
class Store {
public:
    explicit Store(std::string cache_root)
        : cache_root_(std::move(cache_root)) {}

    // Opens `<cache_root>/js5-<index_id>.jcache` if it exists, parses the
    // reference table. Returns false on any failure (missing file, bad
    // schema, decompression error).
    bool Add(int index_id, int default_files_per_archive);

    SqliteIndexFile* Get(int index_id) const;

private:
    std::string cache_root_;
    std::unordered_map<int, std::unique_ptr<SqliteIndexFile>> indexes_;
};

}  // namespace rtx::cache
