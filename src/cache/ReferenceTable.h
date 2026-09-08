#pragma once

#include <cstdint>
#include <vector>

namespace rtx::cache {

struct ArchiveEntry {
    int identifier = -1;
    int crc        = 0;
    int version    = 0;
    int hash       = 0;
    std::vector<int> valid_file_ids;     // sparse list of file ids that exist
    int largest_file_id = -1;            // max element of valid_file_ids
};

// Per-index manifest: which archives exist, files per archive, optional names.
// Stored zlib-wrapped in the SQLite `cache_index` table at KEY=1.

class ReferenceTable {
public:
    // default_file_count: assumed per-archive file count when not encoded (0 = highest id + 1).
    ReferenceTable(int index_id,
                   const std::vector<std::uint8_t>& zlib_wrapped_blob,
                   int default_file_count);

    int               protocol() const { return protocol_; }
    int               version()  const { return version_; }
    const std::vector<int>& valid_archive_ids() const { return valid_archive_ids_; }
    const std::vector<ArchiveEntry>& entries() const { return entries_; }

private:
    void Decode(const std::vector<std::uint8_t>& payload);

    int protocol_ = 0;
    int version_  = 0;
    int default_file_count_;
    std::vector<int>          valid_archive_ids_;   // sparse archive id list
    std::vector<ArchiveEntry> entries_;             // dense; index = archive id
};

}  // namespace rtx::cache
