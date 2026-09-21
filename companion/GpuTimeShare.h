// Per-pass GPU timing, module -> launcher. Written once per presented frame from timestamp queries.
#pragma once
#include <cstdint>
#include "ShareName.h"

namespace rtx::gputime {

inline constexpr wchar_t kSectionPrefix[] = L"Local\\RuneToolsXGpuTime_v1_";
inline constexpr std::uint32_t kMagic   = 0x52545847;   // 'RTXG'
inline constexpr std::uint32_t kVersion = 1;
inline constexpr std::uint32_t kMaxPasses = 64;
inline constexpr int kDescMax = 80;

struct Pass {
    char          desc[kDescMax];
    std::uint32_t draws;
    std::uint32_t us;          // GPU time of the pass in microseconds
};

struct Share {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t pid;
    volatile std::uint32_t seq;      // write seqlock (odd = mid-update)
    std::uint32_t frame;             // frame the timings belong to
    std::uint32_t count;             // valid passes[0..count)
    std::uint32_t total_us;          // first pass begin to last pass end
    std::uint32_t frame_us;          // wall time between presents
    Pass          passes[kMaxPasses];
};

template <std::size_t N>
inline void MakeSectionName(std::uint32_t pid, wchar_t (&out)[N]) {
    rtx::ipc::BuildName(out, kSectionPrefix, pid);
}

}  // namespace rtx::gputime
