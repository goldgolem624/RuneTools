#pragma once
#include <cstdint>

namespace rtx::launcher::companion {

// Ensure the scene-data companion is live in the client. Idempotent, cheap to poll; true once live.
bool EnsureLoaded(std::uint32_t pid);

// Drop everything remembered about a client that has gone away: its session
// slot, its load throttle, and any note that its module could not be handshaken.
void Forget(std::uint32_t pid);

}  // namespace rtx::launcher::companion
