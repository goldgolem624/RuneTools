#pragma once
// The health check run by itself. A client whose exe or cache fingerprint builds.txt has not
// reviewed, and a cache update under a running client, queue one run; it starts once the client
// is in world with the companion booted and has settled, or after three minutes as an incomplete
// run. The panel's manual run goes through the same start and poll, so one run at a time is
// shared and a page picks up a run it did not start.
#include <cstdint>
#include <string>

namespace rtx::launcher::healthauto {

void ClientAttached(std::uint32_t pid);        // Dock::EnsureClient
void ClientGone(std::uint32_t pid);            // Dock::RemoveClient, dead client
void CacheChanged(std::uint64_t generation);   // Dock::Tick after the UI layers reloaded
void Tick();                                   // Dock::Tick; throttled inside

// Starts a run unless one is running; returns the seq the finished run will not equal (the
// panel's protocol). `trigger`: manual, new exe, cache update.
std::uint64_t Start(std::uint32_t pid, const std::string& pinsJson, const std::string& trigger);
// {"running":bool,"seq":n,"run":<the last finished run or null>}
std::string PollJson();
// {"seq":n,"running":bool,"trigger":"..","pending":{pid,trigger,why,version,stamp}|null,
//  "run":<the last finished run when seq != since, else null>}
std::string LatestJson(std::uint64_t since);
// The readable form of a history run by name, or of the last finished run when the name is empty.
std::string SummaryText(const std::string& name);

}  // namespace rtx::launcher::healthauto
