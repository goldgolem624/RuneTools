#pragma once

#include <string>
#include <vector>

// Resolves RS3 item icons from icons rendered offline out of the game cache into
// %USERPROFILE%\RuneToolsX\icons\<clientVersion>\<id>.png, falling back to the bundled
// `items.pack` staged next to the exe (noted items are composited as item-on-note PNGs).
// No network. Hot path is an in-memory map of base64 data URLs so repeated JS polls
// don't re-read the pack.

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

// Where an item's icon comes from: 2 = a PNG rendered offline from the cache for this
// client version, 1 = bundled items.pack, 0 = nowhere (the UI shows the name only).
int IconSource(int item_id);

// PNGs in the active rendered-icon directory, 0 when there is none. Counted once per
// process (a directory walk per Health-panel poll is not free).
int RenderedIconCount();

// Health-panel diagnostics: {"packIds":N,"packIcons":present,"misses":[[id,count],...],
// "missCount":K,"rendered":N}. `misses` are ids asked for through ItemIconDataUrl this
// session and served by neither source (capped at 10,000 distinct ids), so a missing icon
// shows up as data rather than a blank cell somebody has to notice: that list is what the
// offline renderer still owes us.
std::string IconMissesJson();

// Returns an asset file staged next to the exe, base64-encoded (cached per name),
// or an empty string if missing. Used to ship the puzzle-solver tables
// (wd_table.bin, pdb_5554.bin); the JS solver decodes them once on puzzle open.
std::string AssetFileBase64(const std::wstring& filename);

}  // namespace rtx::launcher::icons
