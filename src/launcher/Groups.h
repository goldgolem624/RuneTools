#pragma once
// Group Finder: the launcher's side of the runetools.io lobby. Calls go out on worker threads and
// come back through Take() together with the live event stream; the plugin page polls that. The
// player snapshot a listing or application carries is composed here from the live game, never by
// the page, and leaves with this PC's account token.
#include <cstdint>
#include <string>

namespace rtx::launcher::groups {

// Starts a website call. method is GET or POST, path begins with /api/groups/. Returns the request id
// the answer carries: {"kind":"result","id":N,"status":S,"ok":bool,"body":<json or null>,"text":...}
std::uint32_t Call(const std::string& method, const std::string& path, const std::string& body);

// Everything that arrived since the last call, as a JSON array; results and
// {"kind":"event","event":"...","data":<json>} entries from the live stream.
std::string Take();

// Keeps a live event stream open to the lobby while on.
void Subscribe(bool on);

// This client's verified card: name, world, combat, levels, worn items with perks, kill counts.
std::string SnapshotJson(std::uint32_t pid);

void Shutdown();

}  // namespace rtx::launcher::groups
