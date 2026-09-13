#pragma once
// RuneTools account linking for this PC (device authorization flow against runetools.io).
//
// Start() asks the website for a short user code and a long device secret, shows the code,
// and polls until a signed-in browser approves it. The bearer token that comes back is the only
// credential this PC ever holds; it is sealed with DPAPI for the current Windows user
// (%USERPROFILE%\RuneToolsX\link.bin) and never leaves the launcher except as an
// Authorization header to runetools.io. The website can revoke it at any time; Verify() notices.
#include <string>

namespace rtx::launcher::link {

// Current state as JSON for the settings page:
//   {"state":"idle"|"starting"|"pending"|"linked"|"error",
//    "userCode","verifyUrl","expiresAt","interval",            (pending)
//    "username","displayName","deviceId","deviceName",         (linked)
//    "linkedCount","verified":bool,                            (linked; verified = confirmed with the site this run)
//    "error","note"}
std::string StatusJson();

void Start();          // begin linking (no-op while a code is pending or when already linked)
void Cancel();         // drop a pending code
void Unlink();         // revoke this PC's token on the site (best effort) and forget it locally
void Verify();         // confirm the stored token with the site; clears it if the site revoked it

// Bearer token for account calls, "" when this PC is not linked.
std::string Token();
// Authorization header value ("Bearer ...") or "" when not linked.
std::string AuthHeader();

}  // namespace rtx::launcher::link
