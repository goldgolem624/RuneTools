#pragma once

#include "ReferenceTable.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace rtx::cache {


class SqliteIndexFile {
public:
    static constexpr std::size_t kDefaultByteBudget = 64u * 1024u * 1024u;

    SqliteIndexFile(int index_id, std::string jcache_path,
                    int default_files_per_archive,
                    std::size_t byte_budget = kDefaultByteBudget);
    ~SqliteIndexFile();

    int                 index_id() const     { return index_id_; }
    bool                ready()    const     { return ref_table_ != nullptr; }
    const ReferenceTable& ref()   const     { return *ref_table_; }

    std::vector<std::uint8_t> ReadFile(int archive_id, int file_id);

    // True once the reference table stored in the jcache differs from the one this object was built
    // from, which happens when the game client downloads a cache update while we are running. The
    // file list per archive then no longer matches the archive data, so every read from this object
    // is suspect: the owner must rebuild it. Cheap when nothing changed (file mtime gate).
    bool RefTableChanged();

    std::vector<std::uint8_t> ReadRawArchive(int archive_id);

    std::vector<int> ArchiveIdsFrom(int from_key, int limit) const;

    std::size_t CachedBytes()     const;
    int         FailedArchives()  const;

private:
    enum class SlotState : std::uint8_t { NotLoaded, Loaded, Failed };
    struct Slot {
        SlotState                              state = SlotState::NotLoaded;
        std::uint64_t                          last_use = 0;   // LRU touch counter
        std::size_t                            bytes = 0;
        std::vector<std::vector<std::uint8_t>> files;
    };

    std::vector<std::uint8_t> FetchReferenceTableBlob();
    std::vector<std::uint8_t> FetchArchiveBlob(int archive_id);
    // True when the last FetchBlob could not read the db at all (locked/busy/io error, or the
    // connection would not open) as opposed to the key simply not being present. Transient: the
    // game client holds js5-2.jcache open and writes it while it downloads cache updates.
    bool          LastReadFailed() const { return db_error_; }
    bool ArchiveHasFile(int archive_id, int file_id) const;

    bool          EnsureDb() const;          // lazy open; false if it cannot open
    sqlite3_stmt* Prepare(const char* sql, sqlite3_stmt*& cached) const;
    void          DropDb() const;            // close + forget statements
    void          NoteResult(int rc) const;  // drop the connection on BUSY/LOCKED/IOERR
    std::vector<std::uint8_t> FetchBlob(const char* sql, sqlite3_stmt*& cached, int key) const;

    void EvictToBudget(std::size_t incoming);   // caller holds archive_cache_mu_

    int                            index_id_;
    std::string                    jcache_path_;
    int                            default_files_per_archive_;
    std::unique_ptr<ReferenceTable> ref_table_;
    std::uint64_t                  ref_fp_ = 0;       // FNV-1a of the reference table blob it was built from
    long long                      ref_mtime_ = 0;    // jcache last-write time when last compared

    // Access is already serialised by the launcher's g_mu; this is a cheap extra guard.
    mutable std::mutex             db_mu_;
    mutable sqlite3*               db_ = nullptr;
    mutable sqlite3_stmt*          stmt_ref_table_ = nullptr;
    mutable sqlite3_stmt*          stmt_archive_   = nullptr;
    mutable sqlite3_stmt*          stmt_keys_      = nullptr;
    mutable bool                   db_error_       = false;

    mutable std::mutex             archive_cache_mu_;
    std::vector<Slot>              archive_cache_;
    std::size_t                    byte_budget_;
    std::size_t                    cached_bytes_  = 0;
    int                            failed_count_  = 0;
    std::uint64_t                  use_counter_   = 0;
};

}  // namespace rtx::cache
