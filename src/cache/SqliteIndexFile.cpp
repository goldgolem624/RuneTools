#include "SqliteIndexFile.h"

#include "FileContainer.h"
#include "JagexContainer.h"
#include "vendor/sqlite/sqlite3.h"

#include <algorithm>

namespace rtx::cache {

// ---- connection handling ---------------------------------------------------

bool SqliteIndexFile::EnsureDb() const {
    if (db_) return true;
    // NOMUTEX: we serialise ourselves (db_mu_ and the launcher's g_mu).
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(jcache_path_.c_str(), &db,
                        SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return false;
    }
    db_ = db;
    return true;
}

void SqliteIndexFile::DropDb() const {
    if (stmt_ref_table_) { sqlite3_finalize(stmt_ref_table_); stmt_ref_table_ = nullptr; }
    if (stmt_archive_)   { sqlite3_finalize(stmt_archive_);   stmt_archive_   = nullptr; }
    if (stmt_keys_)      { sqlite3_finalize(stmt_keys_);      stmt_keys_      = nullptr; }
    if (db_)             { sqlite3_close(db_);                db_             = nullptr; }
}

void SqliteIndexFile::NoteResult(int rc) const {
    // Another process (the official launcher) may want exclusive access; let go
    // of the file so it can, and reopen on the next call.
    const int base = rc & 0xff;
    if (base == SQLITE_BUSY || base == SQLITE_LOCKED || base == SQLITE_IOERR) DropDb();
}

sqlite3_stmt* SqliteIndexFile::Prepare(const char* sql, sqlite3_stmt*& cached) const {
    if (!EnsureDb()) return nullptr;
    if (cached) { sqlite3_reset(cached); sqlite3_clear_bindings(cached); return cached; }
    int rc = sqlite3_prepare_v2(db_, sql, -1, &cached, nullptr);
    if (rc != SQLITE_OK) { cached = nullptr; NoteResult(rc); return nullptr; }
    return cached;
}

std::vector<std::uint8_t> SqliteIndexFile::FetchBlob(const char* sql, sqlite3_stmt*& cached,
                                                     int key) const {
    std::vector<std::uint8_t> out;
    std::lock_guard<std::mutex> lk(db_mu_);
    sqlite3_stmt* stmt = Prepare(sql, cached);
    if (!stmt) return out;
    if (key >= 0) sqlite3_bind_int(stmt, 1, key);
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(stmt, 0);
        int         size = sqlite3_column_bytes(stmt, 0);
        if (blob && size > 0) {
            out.assign((const std::uint8_t*)blob, (const std::uint8_t*)blob + size);
        }
    }
    sqlite3_reset(stmt);   // release the read lock between calls
    NoteResult(rc);
    return out;
}

// Archive ids present in the SQLite table itself, independent of the reference table. The
// audio indexes are browsed this way: the ref table is the authority for archive CONTENTS, but
// listing what exists only needs the keys, and it works even where the ref table is absent.
std::vector<int> SqliteIndexFile::ArchiveIdsFrom(int from_key, int limit) const {
    std::vector<int> out;
    if (limit < 1) return out;
    if (from_key < 0) from_key = 0;

    std::lock_guard<std::mutex> lk(db_mu_);
    sqlite3_stmt* stmt = Prepare(
        "SELECT `KEY` FROM `cache` WHERE `KEY` >= ? ORDER BY `KEY` LIMIT ?;", stmt_keys_);
    if (!stmt) return out;
    sqlite3_bind_int(stmt, 1, from_key);
    sqlite3_bind_int(stmt, 2, limit);
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) out.push_back(sqlite3_column_int(stmt, 0));
    sqlite3_reset(stmt);
    NoteResult(rc);
    return out;
}

// ---- lifecycle --------------------------------------------------------------

SqliteIndexFile::SqliteIndexFile(int index_id, std::string jcache_path,
                                 int default_files_per_archive,
                                 std::size_t byte_budget)
    : index_id_(index_id),
      jcache_path_(std::move(jcache_path)),
      default_files_per_archive_(default_files_per_archive),
      byte_budget_(byte_budget) {
    auto blob = FetchReferenceTableBlob();
    if (blob.empty()) return;
    ref_table_ = std::make_unique<ReferenceTable>(index_id, blob,
                                                  default_files_per_archive);
    archive_cache_.resize(ref_table_->entries().size());
}

SqliteIndexFile::~SqliteIndexFile() {
    std::lock_guard<std::mutex> lk(db_mu_);
    DropDb();
}

std::vector<std::uint8_t> SqliteIndexFile::FetchReferenceTableBlob() {
    return FetchBlob("SELECT `DATA` FROM `cache_index` WHERE `KEY` = 1;", stmt_ref_table_, -1);
}

std::vector<std::uint8_t> SqliteIndexFile::FetchArchiveBlob(int archive_id) {
    return FetchBlob("SELECT `DATA` FROM `cache` WHERE `KEY` = ?;", stmt_archive_, archive_id);
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

// ---- decoded-archive cache --------------------------------------------------

void SqliteIndexFile::EvictToBudget(std::size_t incoming) {
    // Evict least-recently-used loaded slots until the newcomer fits. An archive
    // bigger than the whole budget is still cached (alone) so lookups work.
    while (cached_bytes_ > 0 && cached_bytes_ + incoming > byte_budget_) {
        Slot* victim = nullptr;
        for (auto& s : archive_cache_) {
            if (s.state != SlotState::Loaded) continue;
            if (!victim || s.last_use < victim->last_use) victim = &s;
        }
        if (!victim) break;
        cached_bytes_ -= victim->bytes;
        victim->bytes = 0;
        victim->files.clear();
        victim->files.shrink_to_fit();
        victim->state = SlotState::NotLoaded;
    }
}

std::vector<std::uint8_t>
SqliteIndexFile::ReadFile(int archive_id, int file_id) {
    if (!ArchiveHasFile(archive_id, file_id)) return {};

    std::lock_guard<std::mutex> lk(archive_cache_mu_);
    auto& slot = archive_cache_[archive_id];
    if (slot.state == SlotState::Failed) return {};
    if (slot.state == SlotState::NotLoaded) {
        // Failed decodes are sticky: the blob does not change under us this process.
        auto compressed = FetchArchiveBlob(archive_id);
        if (compressed.empty()) { slot.state = SlotState::Failed; ++failed_count_; return {}; }
        auto decompressed = Decompress(compressed);
        if (decompressed.empty()) { slot.state = SlotState::Failed; ++failed_count_; return {}; }
        const auto& a = ref_table_->entries()[archive_id];
        auto files = SplitArchive(decompressed, a.valid_file_ids, a.largest_file_id);
        std::size_t bytes = 0;
        for (const auto& f : files) bytes += f.size();
        EvictToBudget(bytes);
        slot.files = std::move(files);
        slot.bytes = bytes;
        slot.state = SlotState::Loaded;
        cached_bytes_ += bytes;
    }
    slot.last_use = ++use_counter_;
    if (file_id < 0 || (std::size_t)file_id >= slot.files.size()) return {};
    return slot.files[file_id];
}

std::size_t SqliteIndexFile::CachedBytes() const {
    std::lock_guard<std::mutex> lk(archive_cache_mu_);
    return cached_bytes_;
}

int SqliteIndexFile::FailedArchives() const {
    std::lock_guard<std::mutex> lk(archive_cache_mu_);
    return failed_count_;
}

}  // namespace rtx::cache
