#pragma once
//
// Launcher <-> companion contract for ITEM ICON ATLAS readback. The client keeps item
// icons only as cells of a 1536x1536 GPU texture (see docs/icon-capture.md); the
// launcher asks the companion (which owns the GL context in the wglSwapBuffers hook)
// to read that texture back with glGetTexImage into this section, then slices it
// with the cell rectangles it reads itself out of client memory.
//
// Protocol: launcher writes `hintTexOwner` (address of the client's texture-owner
// object, optional) then `request = 1`. The hook, at the next present, finds the atlas
// texture, copies it into `pixels` (RGBA, row 0 = TOP after the flip), sets `status`,
// bumps `seq`, clears `request`. When `request` is 0 the hook path is one load.

#include <cstdint>

namespace rtx::iconatlas {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXIconAtlas_v1_";   // moves with kVersion
inline constexpr std::uint32_t kMagic   = 0x54414349;   // 'ICAT'
inline constexpr std::uint32_t kVersion = 1;
inline constexpr std::uint32_t kAtlasW  = 1536;
inline constexpr std::uint32_t kAtlasH  = 1536;
inline constexpr std::uint32_t kMaxBytes = kAtlasW * kAtlasH * 4;

// `status` values written by the hook.
inline constexpr std::uint32_t kStatusOk        = 0;
inline constexpr std::uint32_t kStatusNoTexture = 1;   // no 1536x1536 texture found (hint or probe)
inline constexpr std::uint32_t kStatusGlError   = 2;   // glGetTexImage raised; `glError` holds it

struct Share {
    std::uint32_t magic;               // kMagic once the launcher has initialised it
    std::uint32_t version;             // kVersion
    std::uint32_t pid;                 // target client pid (sanity)
    volatile std::uint32_t request;    // launcher -> hook: 1 = capture at next present; hook clears
    volatile std::uint32_t seq;        // hook -> launcher: bumped after every completed attempt
    std::uint32_t width;               // captured texture size (== kAtlasW/H on success)
    std::uint32_t height;
    std::uint32_t glName;              // GL texture name that was read (diagnostic, cached across captures)
    std::uint32_t status;              // kStatus*
    std::uint32_t glError;             // glGetError() after the readback (0 when clean)
    std::uint64_t capturedAtMs;        // GetTickCount64() at capture
    std::uint64_t hintTexOwner;        // launcher -> hook: client texture-owner object (0 = probe only)
    std::uint32_t hintHit;             // hook -> launcher: 1 = name came from the hint, 2 = from the probe
    std::uint32_t reserved;
    std::uint8_t  pixels[kMaxBytes];   // RGBA8, top-down
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

}  // namespace rtx::iconatlas
