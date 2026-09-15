#pragma once
// Rune Caches: while this PC is linked to a RuneTools account and the user has opted in, a sampler
// watches every in-game character for XP drops (any skill's XP rising) and boss kills (the kill-log
// varps the Bosses panel reads), and one heartbeat a minute reports the counts to runetools.io. The
// website rolls every cache (Mastery Caches per XP drop, Melee Caches per boss kill, by boss) and
// answers with what dropped; this side only counts, reports and relays the answer to the in-game UI.
#include <cstdint>
#include <string>

namespace rtx::launcher::loot {

void Start();   // starts the sampler and heartbeat threads once (cheap no-ops while not linked or not opted in)
// The launcher is closing: tells the site the characters still in the game world have left.
// Also sent automatically whenever a character goes to the lobby or its client closes.
void Shutdown();

// Opt-in, default OFF and remembered on this PC (%USERPROFILE%\RuneToolsX\rune_caches.txt). No
// heartbeat leaves this PC while it is off, so nothing is reported and nothing can drop.
bool Enabled();
void SetEnabled(bool on);

// In-game alerts for a cache earned: the message card and its sound, each on by default and remembered
// on this PC (%USERPROFILE%\RuneToolsX\rune_caches_alerts.txt). Caches are still earned with both off.
bool AlertMessage();
bool AlertSound();
void SetAlerts(bool message, bool sound);

// The play screen's "Rune Caches" notice (shown while the feature is off) was closed with its X; remembered on
// this PC (%USERPROFILE%\RuneToolsX\rune_caches_notice.txt).
bool NoticeDismissed();
void DismissNotice();

// Per-client poll from the in-game UI. Returns JSON:
//   {"linked":bool,"enabled":bool,"name":"...","xp":n,"kills":n,"caches":n,"unopened":n,
//    "capped":bool,"drop":bool,"dropName":"Mastery Cache","alertMessage":bool,"alertSound":bool}
// xp / kills / caches are this character's totals since the launcher started; "drop" is true exactly
// once per cache earned by that character (cleared by this call), with the cache's name.
std::string PollJson(std::uint32_t pid);

}  // namespace rtx::launcher::loot
