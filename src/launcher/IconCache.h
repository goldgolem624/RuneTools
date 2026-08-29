#pragma once

#include <string>
#include <vector>

// Resolves RS3 item icons from the bundled `items.pack` staged next to the exe
// (noted items are composited as item-on-note PNGs). No network. Hot path is an
// in-memory map of base64 data URLs so repeated JS polls don't re-read the pack.

namespace rtx::launcher::icons {

// Returns a `data:image/gif|png;base64,...` URL for the item icon, or an empty
// string when the id isn't in the pack (or the pack is missing). Memoized.
std::string ItemIconDataUrl(int item_id);

// Same, from modelicons.pack: pre-rendered interface type-6 MODEL comps keyed by
// MODEL id (the live widget only holds a runtime render handle; the model id comes
// from the js5-3 def, so the UI joins live group:comp -> cache def -> this pack).
std::string ModelIconDataUrl(int model_id);

// True when items.pack has an icon blob for the id (index len > 0). Cheap: index only.
bool IconPackHas(int item_id);

// Health-panel diagnostics: {"packIds":N,"packIcons":present,"misses":[[id,count],...],
// "missCount":K}. `misses` are ids asked for through ItemIconDataUrl this session and not
// in the pack (capped at 10,000 distinct ids), so a missing icon shows up as data rather
// than a blank cell somebody has to notice.
std::string IconMissesJson();

// Drop ids from the miss log (a capture just produced their PNGs).
void ForgetMisses(const std::vector<int>& ids);

// Returns an asset file staged next to the exe, base64-encoded (cached per name),
// or an empty string if missing. Used to ship the puzzle-solver tables
// (wd_table.bin, pdb_5554.bin); the JS solver decodes them once on puzzle open.
std::string AssetFileBase64(const std::wstring& filename);

}  // namespace rtx::launcher::icons
