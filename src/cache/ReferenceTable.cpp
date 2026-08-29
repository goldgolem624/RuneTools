#include "ReferenceTable.h"

#include "JagexContainer.h"

#include <optional>

namespace rtx::cache {

namespace {

class Walk {
public:
    explicit Walk(const std::vector<std::uint8_t>& d) : d_(d) {}

    bool   in_bounds(std::size_t need) const { return p_ + need <= d_.size(); }

    std::uint8_t  U8()  { return in_bounds(1) ? d_[p_++] : 0; }
    std::uint16_t U16() {
        if (!in_bounds(2)) return 0;
        std::uint16_t v = ((std::uint16_t)d_[p_] << 8) | d_[p_ + 1];
        p_ += 2; return v;
    }
    std::uint32_t U32() {
        if (!in_bounds(4)) return 0;
        std::uint32_t v = ((std::uint32_t)d_[p_    ] << 24) |
                          ((std::uint32_t)d_[p_ + 1] << 16) |
                          ((std::uint32_t)d_[p_ + 2] <<  8) |
                           (std::uint32_t)d_[p_ + 3];
        p_ += 4; return v;
    }
    void Skip(std::size_t n) { p_ += n; }

    std::optional<std::uint32_t> Smart() {
        if (!in_bounds(1)) return std::nullopt;
        if (d_[p_] & 0x80) return (std::uint32_t)(U32() & 0x7FFFFFFFu);
        std::uint16_t v = U16();
        return (v == 0x7FFF) ? std::nullopt : std::optional<std::uint32_t>(v);
    }

private:
    const std::vector<std::uint8_t>& d_;
    std::size_t p_ = 0;
};

}  // namespace

ReferenceTable::ReferenceTable(int /*index_id*/,
                               const std::vector<std::uint8_t>& zlib_blob,
                               int default_file_count)
    : default_file_count_(default_file_count) {
    auto payload = Decompress(zlib_blob);
    if (payload.empty()) return;
    Decode(payload);
}

void ReferenceTable::Decode(const std::vector<std::uint8_t>& d) {
    Walk w(d);
    protocol_ = w.U8();
    if (protocol_ >= 6) version_ = (int)w.U32();

    const int flags         = w.U8();
    const bool has_names    = (flags & 0x1) != 0;
    const bool has_digests  = (flags & 0x2) != 0;
    const bool has_lengths  = (flags & 0x4) != 0;
    const bool has_hash_alt = (flags & 0x8) != 0;

    // Untrusted blob: reject absurd counts/ids before any resize or indexed write.
    constexpr int kMaxId = 1000000;
    int archive_count = (protocol_ >= 7) ? (int)w.Smart().value_or(0) : (int)w.U16();
    if (archive_count < 0 || archive_count > kMaxId) return;
    valid_archive_ids_.resize(archive_count);

    int running = 0;
    int biggest_archive_id = -1;
    for (int i = 0; i < archive_count; ++i) {
        running += (protocol_ >= 7) ? (int)w.Smart().value_or(0) : (int)w.U16();
        if (running < 0 || running > kMaxId) { valid_archive_ids_.clear(); return; }
        valid_archive_ids_[i] = running;
        if (running > biggest_archive_id) biggest_archive_id = running;
    }
    if (biggest_archive_id < 0) return;
    entries_.resize((std::size_t)biggest_archive_id + 1);

    if (has_names) {
        for (int i = 0; i < archive_count; ++i)
            entries_[valid_archive_ids_[i]].identifier = (int)w.U32();
    }
    for (int i = 0; i < archive_count; ++i)
        entries_[valid_archive_ids_[i]].crc = (int)w.U32();
    if (has_hash_alt) {
        for (int i = 0; i < archive_count; ++i)
            entries_[valid_archive_ids_[i]].hash = (int)w.U32();
    }
    if (has_digests) {
        w.Skip((std::size_t)archive_count * 64);
    }
    if (has_lengths) {
        for (int i = 0; i < archive_count; ++i) {
            w.U32();  // compressed length
            w.U32();  // uncompressed length
        }
    }
    for (int i = 0; i < archive_count; ++i)
        entries_[valid_archive_ids_[i]].version = (int)w.U32();

    std::vector<int> file_counts(archive_count);
    for (int i = 0; i < archive_count; ++i) {
        file_counts[i] = (protocol_ >= 7) ? (int)w.Smart().value_or(0) : (int)w.U16();
        if (file_counts[i] < 0 || file_counts[i] > kMaxId) { entries_.clear(); return; }
    }

    for (int i = 0; i < archive_count; ++i) {
        ArchiveEntry& a = entries_[valid_archive_ids_[i]];
        const int n = file_counts[i];
        a.valid_file_ids.reserve(n);
        int rolling = 0;
        int biggest = -1;
        for (int j = 0; j < n; ++j) {
            int delta = (protocol_ >= 7) ? (int)w.Smart().value_or(0) : (int)w.U16();
            rolling += delta;
            if (rolling < 0 || rolling > kMaxId) { entries_.clear(); return; }
            if (rolling > biggest) biggest = rolling;
            a.valid_file_ids.push_back(rolling);
        }
        a.largest_file_id = (default_file_count_ > 0)
            ? default_file_count_ - 1
            : biggest;
    }

    if (has_names) {
        for (int i = 0; i < archive_count; ++i) {
            const auto& vfids = entries_[valid_archive_ids_[i]].valid_file_ids;
            w.Skip(vfids.size() * 4);   // per-file name hashes, unused here
        }
    }
}

}  // namespace rtx::cache
