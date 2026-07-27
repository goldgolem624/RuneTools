#pragma once
//
// Launcher <-> companion contract for live client-variable values (varc / varbit /
// client-string ints). The engine resolves these through a per-script VM context
// whose storage pointer comes from a vtable call, so they are only resolvable
// in-process: the companion observes script var-pushes to learn each var's storage
// pointer and publishes {id, scope, value} snapshots here. Plain C-layout POD.

#include <cstdint>

namespace rtx::varc {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXVarcData_v1_";
inline constexpr std::uint32_t kMagic   = 0x52545643;   // 'RTVC'
inline constexpr std::uint32_t kVersion = 5;            // entries keyed by (scope,id): varp is scope 4, varc-int scope 5
inline constexpr int kMaxVars           = 32768;        // headroom for ~12.6k varps + the varc-int set
inline constexpr int kMaxStrVars        = 256;          // distinct string varcs observed
inline constexpr int kStrLen            = 192;          // per string-var, incl. NUL

// One resolved int client variable. `scope` = raw VM scope byte (vm_ctx+0x20);
// launcher groups/labels varc vs varbit vs other from it.
struct Entry {
    std::uint16_t id;
    std::uint8_t  scope;
    std::uint8_t  _pad;
    std::int32_t  value;
};

// One resolved string client variable (varc-string, scope 2): last value a script
// read onto the VM string stack. NUL-terminated.
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
    // Launcher -> companion: 1 while the Vars watcher is open. Resolving a var's
    // storage runs a vtable call per newly-seen id, so the companion only resolves
    // and publishes while this is on; otherwise the observer is a near-noop.
    volatile std::uint32_t enable;
    std::uint32_t flags;            // reserved + bit0 = observer installed
    std::uint32_t strCount;         // valid strEntries[0..strCount)
    // Diagnostic counters (companion-written, racy/best-effort), placed BEFORE
    // entries so they sit at a fixed small offset. [0]=varp captures, [1]=varc
    // captures, [2]=panel-varc resolve attempts, [3]=resolve successes,
    // [4]=ReadVarContext ok, [5]=FindEntry ok, [6..7]=spare.
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
