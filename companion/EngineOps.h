#pragma once
// Any scripting-engine operation of the client, called by name from the game thread.
//
// The client registers every operation in one routine with the same two instructions each, so
// all handlers are found from the image. Their numbers change per build; the cache export the
// launcher keeps (cs2\opcodes.json) names them. A handler takes the client's root and a script
// state and reads its arguments from the state's stacks, so a call is: push the arguments, call,
// pop the results. Only the game thread may call, and only while the client is in a frame.
#include <cstddef>
#include <cstdint>

namespace rtx::marker { struct Anchor; }

namespace rtx::engineops {

bool Ready();                                   // handlers found and the name table read
// Calls `name` with the given ints (and optional strings) and returns the ints it left on the
// stack in `out` (up to `cap`); -1 when the operation is unknown or the call faulted.
int Call(std::uint8_t* root, const char* name, const std::int32_t* ints, int nInts,
         const char* const* strs, int nStrs, std::int32_t* out, int cap);
// Handler address for `name`, or null; for callers that need a routine rather than a call.
const void* Handler(const char* name);
bool TakeLog(char* out, std::size_t cap);
// The client's own world to screen answer, the one it uses for its own overlays: a position in the
// game's fine units (512 to a tile) with a height offset above it, giving screen x, screen y and the
// view depth inside the game view. With `onGround` the height is taken from the terrain instead of
// the height given. False when the operation is not recognised in this build or the call faulted;
// game thread only, inside a frame.
struct Point { int x, y, depth; };
bool Project(std::uint8_t* root, int plane, float x, float height, float y, int heightOffset,
             bool onGround, Point& out);
// The same answer for the local player, with the position taken from the game as well, so nothing
// about the projection depends on our own reading of the world. Used to check the two against
// each other before the markers move over to it.
bool ProjectSelf(std::uint8_t* root, int heightOffset, Point& out);
// How high above a character the game hangs its own overheads, in the units a position's height is
// in. The NPC form goes through the scripts' own lookup; the player form fills the state's character
// slot from the registry, which is the only way a player can be reached. False when the index names
// no character, or the operations are not recognised in this build.
bool NpcOverheadHeight(std::uint8_t* root, int index, std::int32_t& lift);
bool PlayerOverheadHeight(std::uint8_t* root, int index, std::int32_t& lift);
// Reports that answer once a second while the check file is present; no cost otherwise.
void DevProject(std::uint8_t* root);
// The launcher's one-shot requests: queued from any thread, carried out by Pump on the game thread.
void Queue(std::int32_t sound, std::int32_t zoom, std::int32_t fov);
void Pump(std::uint8_t* root);
// Answers the launcher's world points once per frame, on the game thread, and hands the answers to
// the frame share. Does nothing when there are none to answer.
void PumpAnchors(std::uint8_t* root);
// Why the answers could not be handed over, for the check only.
void SayFrameState(bool mapped, std::uint32_t magic, std::uint32_t version,
                   std::uint32_t wantMagic, std::uint32_t wantVersion);
// The points to answer, taken from the launcher's list every time it changes.
void WantAnchors(const struct rtx::marker::Anchor* a, int n);

}  // namespace rtx::engineops
