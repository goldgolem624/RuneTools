#pragma once

#include <string>
#include <vector>

// RS3 item icons: offline-rendered %USERPROFILE%\RuneToolsX\icons\<clientVersion>\<id>.png,
// falling back to the bundled items.pack next to the exe. No network; results memoized.

namespace rtx::launcher::icons {

// `data:image/gif|png;base64,...` URL, or empty when unavailable.
std::string ItemIconDataUrl(int item_id);

// Same, from modelicons.pack: pre-rendered interface type-6 model comps keyed by model id.
std::string ModelIconDataUrl(int model_id);

// items.pack has an icon blob for the id. Index only.
bool IconPackHas(int item_id);

// 2 = offline-rendered PNG for this client version, 1 = bundled items.pack, 0 = none.
int IconSource(int item_id);

// PNGs in the active rendered-icon directory, counted once per process.
int RenderedIconCount();

// Health-panel diagnostics: {"packIds":N,"packIcons":present,"misses":[[id,count],...],
// "missCount":K,"rendered":N}. Misses are capped at 10,000 distinct ids.
std::string IconMissesJson();

// Asset file staged next to the exe, base64-encoded (cached per name); empty if missing.
std::string AssetFileBase64(const std::wstring& filename);

}  // namespace rtx::launcher::icons
