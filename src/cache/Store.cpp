#include "Store.h"

#include <filesystem>
#include <system_error>

namespace rtx::cache {

bool Store::Add(int index_id, int default_files_per_archive) {
    std::string path = cache_root_ + "/js5-" + std::to_string(index_id) + ".jcache";
    // The root is UTF-8 (as SQLite takes it); a plain std::string path would be read as ANSI.
    std::error_code ec;
    if (!std::filesystem::exists(std::filesystem::u8path(path), ec)) return false;
    // Register even without a parsed reference table: raw-archive reads (sprites) don't need it.
    indexes_[index_id] = std::make_unique<SqliteIndexFile>(
        index_id, path, default_files_per_archive);
    return true;
}

SqliteIndexFile* Store::Get(int index_id) const {
    auto it = indexes_.find(index_id);
    return (it == indexes_.end()) ? nullptr : it->second.get();
}

}  // namespace rtx::cache
