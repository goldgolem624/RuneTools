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

// Stored zlib-wrapped in the SQLite `cache_index` table at KEY=1.

class ReferenceTable {
public:
    ReferenceTable(int index_id,
                   const std::vector<std::uint8_t>& zlib_wrapped_blob,
                   int default_file_count);

    int               protocol() const { return protocol_; }
    int               version()  const { return version_; }
    // The flags byte (bit 0 names, 1 digests, 2 lengths, 3 hash); -1 when the blob did not decode.
    int               flags()    const { return flags_; }
    // True when the decode read the payload exactly to its end: the layout is the one this code
    // expects. False after a bad count, a new protocol or a trailing section.
    bool              consumedExactly() const { return exact_; }
    int               payloadBytes()    const { return payload_bytes_; }
    int               leftoverBytes()   const { return leftover_; }
    const std::vector<int>& valid_archive_ids() const { return valid_archive_ids_; }
    const std::vector<ArchiveEntry>& entries() const { return entries_; }

private:
    void Decode(const std::vector<std::uint8_t>& payload);

    int protocol_ = 0;
    int version_  = 0;
    int flags_    = -1;
    bool exact_   = false;
    int payload_bytes_ = 0;
    int leftover_ = 0;
    int default_file_count_;
    std::vector<int>          valid_archive_ids_;   // sparse archive id list
    std::vector<ArchiveEntry> entries_;             // dense; index = archive id
};

}  // namespace rtx::cache
