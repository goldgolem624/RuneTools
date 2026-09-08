#pragma once
#include <cstdint>

namespace rtx::launcher::companion {

// Ensure the scene-data companion is live in the client. Idempotent, cheap to poll; true once live.
bool EnsureLoaded(std::uint32_t pid);

}  // namespace rtx::launcher::companion
