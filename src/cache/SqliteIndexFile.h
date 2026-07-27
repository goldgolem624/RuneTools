#pragma once

#include "ReferenceTable.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace rtx::cache {

// One .jcache file (e.g. `js5-19.jcache` for items). The jcache is a
// SQLite database with two interesting tables:
//   cache_index  KEY=1 -> reference-table blob (zlib-wrapped manifest)
//   cache        KEY=<archive_id> -> archive blob (zlib-wrapped files)
// Connections are opened per-query (READONLY) so the official launcher
// in another client can still take exclusive access if it needs to.

class SqliteIndexFile {
public:
    SqliteIndexFile(int index_id, std::string jcache_path,
                    int default_files_per_archive);

    int                 index_id() const     { return index_id_; }
    bool                ready()    const     { return ref_table_ != nullptr; }
    const ReferenceTable& ref()   const     { return *ref_table_; }

    // Returns the decompressed bytes of one file inside one archive, or
    // empty if the file/archive doesn't exist or decompression fails.
    // Thread-safe; each call opens its own transient SQLite connection.
    std::vector<std::uint8_t> ReadFile(int archive_id, int file_id);

    // Raw archive blob straight from the SQLite `cache` table (no
    // decompression, no reference-table validation). The sprite index
    // uses the standard container, so callers decompress it themselves
    // with DecompressStandard.
    std::vector<std::uint8_t> ReadRawArchive(int archive_id);

private:
    std::vector<std::uint8_t> FetchReferenceTableBlob();
    std::vector<std::uint8_t> FetchArchiveBlob(int archive_id);
    bool ArchiveHasFile(int archive_id, int file_id) const;

    int                            index_id_;
    std::string                    jcache_path_;
    int                            default_files_per_archive_;
    std::unique_ptr<ReferenceTable> ref_table_;
    std::mutex                     archive_cache_mu_;
    std::vector<std::vector<std::vector<std::uint8_t>>> archive_cache_;
};

}  // namespace rtx::cache
