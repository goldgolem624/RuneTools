// Presented-frame capture, launcher <-> module. The launcher creates the section and bumps
// `request`; the module copies the next presented swapchain image (overlay included) into
// `pixels` and sets `done` to the request it served.
#pragma once
#include <cstdint>
#include "ShareName.h"

namespace rtx::capture {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXCapture_v1_";
inline constexpr std::uint32_t kMagic   = 0x52545843;   // 'RTXC'
inline constexpr std::uint32_t kVersion = 1;
inline constexpr std::uint32_t kMaxWidth  = 3840;
inline constexpr std::uint32_t kMaxHeight = 2160;
inline constexpr std::uint32_t kMaxBytes  = kMaxWidth * kMaxHeight * 4;

struct Share {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t pid;
    volatile std::uint32_t request;   // launcher -> module: capture serial wanted
    volatile std::uint32_t done;      // module -> launcher: serial served (pixels valid)
    std::uint32_t width, height, stride;   // BGRA8, top-down rows
    std::uint32_t format;             // VkFormat of the swapchain the pixels came from
    std::uint8_t  pixels[kMaxBytes];
};

template <std::size_t N>
inline void MakeSectionName(std::uint32_t pid, wchar_t (&out)[N]) {
    rtx::ipc::BuildName(out, kSectionPrefix, pid);
}

}  // namespace rtx::capture
