#pragma once

#include "Constants.h"

namespace rtx::cache {

// How the readers turn an id into (archive, file): archive = id >> bits, file = id & ((1 << bits) - 1).
// 0 = no split: the id is the archive (sprites, scripts, map squares, models, audio) or the file inside
// a fixed archive (configs by type, interface components by group, world-map areas by kind).
constexpr int IdSplitBits(int index) {
    switch (index) {
    case kIndexLocations: case kIndexEnums: case kIndexItems: case 21: return 8;   // 21 = spot animations
    case kIndexNpcs: case kIndexAchievements: return 7;
    case kIndexStructs: return 5;
    default: return 0;
    }
}

}  // namespace rtx::cache
