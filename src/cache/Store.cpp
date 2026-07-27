#include "Store.h"

#include <filesystem>

namespace rtx::cache {

bool Store::Add(int index_id, int default_files_per_archive) {
    std::string path = cache_root_ + "/js5-" + std::to_string(index_id) + ".jcache";
    if (!std::filesystem::exists(path)) return false;
    // Register even if the reference table didn't parse -- raw-archive
    // reads (e.g. sprites) don't need it. ReadFile callers still get empty
    // results when the table is absent, which is the correct degradation.
    indexes_[index_id] = std::make_unique<SqliteIndexFile>(
        index_id, path, default_files_per_archive);
    return true;
}

SqliteIndexFile* Store::Get(int index_id) const {
    auto it = indexes_.find(index_id);
    return (it == indexes_.end()) ? nullptr : it->second.get();
}

}  // namespace rtx::cache
