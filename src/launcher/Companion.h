#pragma once
#include <cstdint>

namespace rtx::launcher::companion {

// Ensure the scene-data companion is present in the given client so the launcher can read the
// runtime scene-object list it publishes. Idempotent and cheap to call on every UI poll (fast
// liveness check first); one-time setup is throttled per pid. True once the companion is live.
bool EnsureLoaded(std::uint32_t pid);

}  // namespace rtx::launcher::companion
