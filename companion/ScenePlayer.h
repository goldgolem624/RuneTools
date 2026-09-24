#pragma once
// The local player's tile as the scene worker last read it from game memory, for code that must
// not take a position from the launcher on trust.

namespace rtx::sceneplayer {

// False until the worker has found the player, and again once it has not for a few seconds.
bool Tile(int& x, int& y);

}  // namespace rtx::sceneplayer
