#pragma once
//
// Launcher <-> companion contract: the companion publishes the live scene-object
// list (runtime-placed locs the launcher cannot read on its own) into a named
// shared section. Keep it a plain C-layout POD (no STL/virtuals).

#include <cstdint>

namespace rtx::scene {

// Named shared section, per client pid; version in the name isolates layout changes.
inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXSceneData_v1_";
inline constexpr std::uint32_t kMagic   = 0x52545853;   // 'RTXS'
inline constexpr std::uint32_t kVersion = 2;
inline constexpr int kMaxObjects        = 4000;

// Build "Local\RuneToolsXSceneData_v1_<pid>" into `out` (size >= 64).
inline void MakeSectionName(std::uint32_t pid, wchar_t* out) {
    int i = 0;
    for (const wchar_t* s = kSectionPrefix; *s; ++s) out[i++] = *s;
    wchar_t tmp[16]; int n = 0;
    if (pid == 0) tmp[n++] = L'0';
    while (pid) { tmp[n++] = (wchar_t)(L'0' + pid % 10); pid /= 10; }
    while (n) out[i++] = tmp[--n];
    out[i] = 0;
}

// One runtime scene object. Names/actions are resolved launcher-side from the loc
// cache by `config_id`, so the companion only publishes ids + position + bounds.
struct Object {
    std::int32_t config_id;   // loc config id (resolve name/actions from the cache)
    std::int32_t x;           // world tile X
    std::int32_t y;           // world tile Y
    std::int16_t plane;       // 0..3
    std::int16_t kind;        // sub type byte (0 / 12 = scenery)
    // Live model bounding box in world-FINE coords, ordered (east, north, up) to
    // match the overlay projection. bmax.x <= bmin.x signals "no valid box".
    float        bmin[3];     // AABB min (east, north, up)
    float        bmax[3];     // AABB max (east, north, up)
};

// Header + inline object array at the start of the section. `seq` goes odd before
// a write and even after; reader retries while odd or if it changed across the copy.
struct Share {
    std::uint32_t magic;      // kMagic once the companion has initialised it
    std::uint32_t version;    // kVersion
    volatile std::uint32_t seq;   // write seqlock (odd = mid-update)
    std::uint32_t count;      // valid entries in objects[0..count)
    std::uint32_t pid;        // target client pid (sanity)
    std::uint32_t flags;      // reserved
    // Diagnostic channel (first-run offset confirmation): a raw copy of one
    // known-good scenery object's sub-struct, so launcher-side tooling can pin the
    // exact field offsets for this game build. diag_len 0 = none.
    std::uint32_t diag_len;
    std::uint8_t  diag[0x400];
    Object        objects[kMaxObjects];
};

}  // namespace rtx::scene
