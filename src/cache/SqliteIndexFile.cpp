#include "SqliteIndexFile.h"

#include "FileContainer.h"
#include "JagexContainer.h"
#include "vendor/sqlite/sqlite3.h"

#include <algorithm>

namespace rtx::cache {

namespace {

std::vector<std::uint8_t> FetchBlob(const std::string& jcache_path,
                                    const char* sql, int key) {
    std::vector<std::uint8_t> out;
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(jcache_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return out;
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (key >= 0) sqlite3_bind_int(stmt, 1, key);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const void* blob = sqlite3_column_blob(stmt, 0);
            int         size = sqlite3_column_bytes(stmt, 0);
            if (blob && size > 0) {
                out.assign((const std::uint8_t*)blob,
                           (const std::uint8_t*)blob + size);
            }
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return out;
}

}  // namespace

SqliteIndexFile::SqliteIndexFile(int index_id, std::string jcache_path,
                                 int default_files_per_archive)
    : index_id_(index_id),
      jcache_path_(std::move(jcache_path)),
      default_files_per_archive_(default_files_per_archive) {
    auto blob = FetchReferenceTableBlob();
    if (blob.empty()) return;
    ref_table_ = std::make_unique<ReferenceTable>(index_id, blob,
                                                  default_files_per_archive);
    archive_cache_.resize(ref_table_->entries().size());
}

std::vector<std::uint8_t> SqliteIndexFile::FetchReferenceTableBlob() {
    return FetchBlob(jcache_path_,
                     "SELECT `DATA` FROM `cache_index` WHERE `KEY` = 1;", -1);
}

std::vector<std::uint8_t> SqliteIndexFile::FetchArchiveBlob(int archive_id) {
    return FetchBlob(jcache_path_,
                     "SELECT `DATA` FROM `cache` WHERE `KEY` = ?;", archive_id);
}

std::vector<std::uint8_t> SqliteIndexFile::ReadRawArchive(int archive_id) {
    return FetchArchiveBlob(archive_id);
}

bool SqliteIndexFile::ArchiveHasFile(int archive_id, int file_id) const {
    if (!ref_table_) return false;
    const auto& entries = ref_table_->entries();
    if (archive_id < 0 || (std::size_t)archive_id >= entries.size()) return false;
    const auto& a = entries[archive_id];
    return std::find(a.valid_file_ids.begin(), a.valid_file_ids.end(), file_id)
           != a.valid_file_ids.end();
}

std::vector<std::uint8_t>
SqliteIndexFile::ReadFile(int archive_id, int file_id) {
    if (!ArchiveHasFile(archive_id, file_id)) return {};

    std::lock_guard<std::mutex> lk(archive_cache_mu_);
    auto& slot = archive_cache_[archive_id];
    if (slot.empty()) {
        auto compressed = FetchArchiveBlob(archive_id);
        if (compressed.empty()) return {};
        auto decompressed = Decompress(compressed);
        if (decompressed.empty()) return {};
        const auto& a = ref_table_->entries()[archive_id];
        slot = SplitArchive(decompressed, a.valid_file_ids, a.largest_file_id);
    }
    if (file_id < 0 || (std::size_t)file_id >= slot.size()) return {};
    return slot[file_id];
}

}  // namespace rtx::cache
