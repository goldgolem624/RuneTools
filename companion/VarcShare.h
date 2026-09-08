#pragma once
// Live client-variable values, learned from script var-pushes; `scope` = raw VM scope byte (vm_ctx+0x20).

#include <cstdint>

namespace rtx::varc {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXVarcData_v1_";
inline constexpr std::uint32_t kMagic   = 0x52545643;   // 'RTVC'
inline constexpr std::uint32_t kVersion = 5;            // entries keyed by (scope,id): varp is scope 4, varc-int scope 5
inline constexpr int kMaxVars           = 32768;        // headroom for ~12.6k varps + the varc-int set
inline constexpr int kMaxStrVars        = 256;          // distinct string varcs observed
inline constexpr int kStrLen            = 192;          // per string-var, incl. NUL

struct Entry {
    std::uint16_t id;
    std::uint8_t  scope;
    std::uint8_t  _pad;
    std::int32_t  value;
};

struct StrEntry {
    std::uint16_t id;
    char          text[kStrLen];
};

struct Share {
    std::uint32_t magic;            // kMagic once initialised
    std::uint32_t version;          // kVersion
    volatile std::uint32_t seq;     // write seqlock (odd = mid-update)
    std::uint32_t count;            // valid entries[0..count)
    std::uint32_t pid;              // target client pid (sanity)
    volatile std::uint32_t enable;  // launcher -> companion; resolving costs a vtable call per new id, so gated
    std::uint32_t flags;            // bit0 = observer installed
    std::uint32_t strCount;         // valid strEntries[0..strCount)
    std::uint32_t diag[8];
    Entry         entries[kMaxVars];
    StrEntry      strEntries[kMaxStrVars];
};

inline void MakeSectionName(std::uint32_t pid, wchar_t* out) {
    int i = 0;
    for (const wchar_t* s = kSectionPrefix; *s; ++s) out[i++] = *s;
    wchar_t tmp[16]; int n = 0;
    if (pid == 0) tmp[n++] = L'0';
    while (pid) { tmp[n++] = (wchar_t)(L'0' + pid % 10); pid /= 10; }
    while (n) out[i++] = tmp[--n];
    out[i] = 0;
}

}  // namespace rtx::varc
