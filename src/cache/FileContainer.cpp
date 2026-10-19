#include "FileContainer.h"

namespace rtx::cache {

std::vector<std::vector<std::uint8_t>>
SplitArchive(const std::vector<std::uint8_t>& data,
             const std::vector<int>& valid_file_ids,
             int largest_file_id,
             std::string* why) {
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
    auto be32 = [&](std::size_t p) {
        return ((std::int32_t)data[p] << 24) | ((std::int32_t)data[p + 1] << 16) |
               ((std::int32_t)data[p + 2] << 8) | (std::int32_t)data[p + 3];
    };
    // The first offset is where the table ends, so the blob itself says how many files it holds: a
    // table that is whole for another count is an archive stored against another file list (an
    // earlier download), not another layout.
    int held = -1;
    if (data.size() >= table_start + 4) {
        const std::int32_t first = be32(table_start);
        if (first > (std::int32_t)table_start && (first - (std::int32_t)table_start) % 4 == 0 && (std::size_t)first <= data.size()) {
            const int m = (first - (int)table_start) / 4 - 1;
            std::int32_t prev = first; bool mono = m >= 1;
            for (int i = 1; mono && i <= m; ++i) { const std::int32_t o = be32(table_start + (std::size_t)i * 4); mono = o >= prev && (std::size_t)o <= data.size(); prev = o; }
            if (mono) held = m;
        }
    }
    auto layout = [&](const std::string& detail) {
        if (!why) return;
        *why = (held >= 0 && held != n ? "file table holds " + std::to_string(held) + " files, the reference table lists " + std::to_string(n)
                                       : std::string("file table layout changed")) + " (" + detail + ")";
    };
    if (data.size() < table_end) {
        layout(std::to_string(data.size()) + " bytes for " + std::to_string(n) + " files");
        return {};
    }

    std::vector<std::int32_t> offsets((std::size_t)n + 1);
    for (std::size_t i = 0; i <= (std::size_t)n; ++i) offsets[i] = be32(table_start + i * 4);
    // The first file starts right after the table and the last ends inside the blob; anything else
    // is another container layout, not files.
    if (offsets[0] != (std::int32_t)table_end || offsets[n] < 0 || (std::size_t)offsets[n] > data.size()) {
        layout("first file at " + std::to_string(offsets[0]) + ", table ends at " + std::to_string(table_end) +
               ", last file ends at " + std::to_string(offsets[n]) + " of " + std::to_string(data.size()));
        return {};
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
