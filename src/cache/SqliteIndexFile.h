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

// One .jcache file (e.g. `js5-19.jcache` for items). The jcache is a
// SQLite database with two interesting tables:
//   cache_index  KEY=1 -> reference-table blob (zlib-wrapped manifest)
//   cache        KEY=<archive_id> -> archive blob (zlib-wrapped files)
// One READONLY connection is kept per index and dropped on any BUSY/LOCKED/
// IOERR so the official launcher in another client can still take exclusive
// access; the next call simply reopens.

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

    // Returns the decompressed bytes of one file inside one archive, or
    // empty if the file/archive doesn't exist or decompression fails.
    // Decoded archives are cached under a byte budget with LRU eviction;
    // archives that failed to decode are remembered and not retried.
    std::vector<std::uint8_t> ReadFile(int archive_id, int file_id);

    // Raw archive blob straight from the SQLite `cache` table (no
    // decompression, no reference-table validation). The sprite index
    // uses the standard container, so callers decompress it themselves
    // with DecompressStandard.
    std::vector<std::uint8_t> ReadRawArchive(int archive_id);

    // Archive ids present in the SQLite table, ascending from `from_key`, at most `limit`.
    // Read straight from the table rather than the reference table, so it works for indexes
    // whose ref table this build does not parse.
    std::vector<int> ArchiveIdsFrom(int from_key, int limit) const;

    // Health reporting: bytes of decoded files currently cached, and archives
    // whose decode failed (not retried this process).
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
    bool ArchiveHasFile(int archive_id, int file_id) const;

    // Connection handling (const so ArchiveIdsFrom can stay const).
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

    // All access is already serialised by the launcher's g_mu; this mutex is
    // a cheap guard in case that assumption ever changes.
    mutable std::mutex             db_mu_;
    mutable sqlite3*               db_ = nullptr;
    mutable sqlite3_stmt*          stmt_ref_table_ = nullptr;
    mutable sqlite3_stmt*          stmt_archive_   = nullptr;
    mutable sqlite3_stmt*          stmt_keys_      = nullptr;

    mutable std::mutex             archive_cache_mu_;
    std::vector<Slot>              archive_cache_;
    std::size_t                    byte_budget_;
    std::size_t                    cached_bytes_  = 0;
    int                            failed_count_  = 0;
    std::uint64_t                  use_counter_   = 0;
};

}  // namespace rtx::cache
