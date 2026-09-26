#pragma once
#include <cstdint>
#include <string>

namespace rtx::launcher::companion {

// Bring the scene-data companion up in the client and keep it there. Idempotent and cheap to poll:
// true once the module is live under this client's session. A call that finds it not live makes at
// most one attempt, and attempts back off, so polling from a render path costs nothing between them.
bool EnsureLoaded(std::uint32_t pid);

// Where a client stands, in words, for the logs: live, what is being waited on, or why it can no
// longer be served until the game restarts.
std::string Describe(std::uint32_t pid);

// Drop everything remembered about a client that has gone away: its session
// slot and the state of the attempts to bring its module up.
void Forget(std::uint32_t pid);

}  // namespace rtx::launcher::companion
