#include "FileContainer.h"

namespace rtx::cache {

std::vector<std::vector<std::uint8_t>>
SplitArchive(const std::vector<std::uint8_t>& data,
             const std::vector<int>& valid_file_ids,
             int largest_file_id) {
    std::vector<std::vector<std::uint8_t>> files;
    if (largest_file_id < 0 || valid_file_ids.empty()) return files;
    files.resize((std::size_t)largest_file_id + 1);

    const int n = (int)valid_file_ids.size();
    if (n == 1) {
        // Same guard as the multi-file loop: the id comes from the reference table unchecked.
        if (valid_file_ids[0] < 0 || valid_file_ids[0] > largest_file_id) return files;
        files[valid_file_ids[0]] = data;
        return files;
    }

    const std::size_t table_start = 1;
    const std::size_t table_end   = table_start + (std::size_t)(n + 1) * 4;
    if (data.size() < table_end) return files;

    std::vector<std::int32_t> offsets((std::size_t)n + 1);
    for (std::size_t i = 0; i <= (std::size_t)n; ++i) {
        std::size_t p = table_start + i * 4;
        offsets[i] = ((std::int32_t)data[p    ] << 24) |
                     ((std::int32_t)data[p + 1] << 16) |
                     ((std::int32_t)data[p + 2] <<  8) |
                      (std::int32_t)data[p + 3];
    }

    for (int i = 0; i < n; ++i) {
        int start = offsets[i];
        int end   = offsets[i + 1];
        if (start < 0 || end < start || (std::size_t)end > data.size()) continue;
        int fid = valid_file_ids[i];
        if (fid < 0 || fid > largest_file_id) continue;
        files[fid].assign(data.begin() + start, data.begin() + end);
    }
    return files;
}

}  // namespace rtx::cache
