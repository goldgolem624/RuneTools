#pragma once
// Launcher <-> companion shared section for the live scene-object list. Plain C-layout POD only.

#include <cstdint>

namespace rtx::scene {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXSceneData_v2_";
inline constexpr std::uint32_t kMagic   = 0x52545853;   // 'RTXS'
inline constexpr std::uint32_t kVersion = 2;
inline constexpr int kMaxObjects        = 4000;

inline void MakeSectionName(std::uint32_t pid, wchar_t* out) {
    int i = 0;
    for (const wchar_t* s = kSectionPrefix; *s; ++s) out[i++] = *s;
    wchar_t tmp[16]; int n = 0;
    if (pid == 0) tmp[n++] = L'0';
    while (pid) { tmp[n++] = (wchar_t)(L'0' + pid % 10); pid /= 10; }
    while (n) out[i++] = tmp[--n];
    out[i] = 0;
}

inline constexpr std::int16_t kHiddenBit = 0x100;   // Object::kind flag: loc hidden (sub+0xF8 bit 16),
                                                     // how a depleted tree or a stump is switched off

struct Object {
    std::int32_t config_id;   // loc config id (resolve name/actions from the cache)
    std::int32_t x;           // world tile X
    std::int32_t y;           // world tile Y
    std::int16_t plane;       // 0..3
    std::int16_t kind;        // sub type byte (0 / 12 = scenery) | kHiddenBit when the game hides it
    float        bmin[3];     // AABB min, world-fine (east, north, up)
    float        bmax[3];     // AABB max; bmax.x <= bmin.x = no valid box
};

struct Share {
    std::uint32_t magic;      // kMagic once the companion has initialised it
    std::uint32_t version;    // kVersion
    volatile std::uint32_t seq;   // write seqlock (odd = mid-update)
    std::uint32_t count;      // valid entries in objects[0..count)
    std::uint32_t pid;        // target client pid (sanity)
    std::uint32_t flags;      // reserved
    std::uint32_t diag_len;   // raw scenery sub-struct copy for offset checks; 0 = none
    std::uint8_t  diag[0x400];
    Object        objects[kMaxObjects];
};

}  // namespace rtx::scene
