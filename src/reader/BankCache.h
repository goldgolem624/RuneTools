#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Persistent per-character bank cache. The RS3 bank (container 95) only exists in
// client memory while the bank is open, so snapshot it while open and keep it on disk
// to display when closed / next session. Stored encrypted at
// %USERPROFILE%\RuneToolsX\bankcache\<character>.bnk (format in BankCache.cpp).

namespace rtx::reader {

struct BankSlot {
    std::int32_t item_id = 0;   // 0/-1 = empty
    std::int32_t stack   = 0;
};

struct BankCacheData {
    std::vector<BankSlot> slots;       // full array; slot number = index
    long long             cached_at = 0;
};

// Generic per-character container cache (same encrypted format). `kind` selects the cache
// subdir/file ("bank" -> bankcache, "metalbank" -> metalbankcache, ...) so multiple
// on-demand containers (bank 95, metal bank 858) each persist independently.
bool          WriteContainerCache(const std::string& kind, const std::string& character,
                                  const std::vector<BankSlot>& slots);
BankCacheData ReadContainerCache(const std::string& kind, const std::string& character);

// Bank wrappers (kind = "bank").
bool          WriteBankCache(const std::string& character,
                             const std::vector<BankSlot>& slots);
BankCacheData ReadBankCache(const std::string& character);

}  // namespace rtx::reader
