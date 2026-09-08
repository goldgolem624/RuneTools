#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Persistent per-character container cache (bank 95 exists in memory only while open).
// Stored encrypted at %USERPROFILE%\RuneToolsX\bankcache\<character>.bnk (format in BankCache.cpp).

namespace rtx::reader {

struct BankSlot {
    std::int32_t item_id = 0;   // 0/-1 = empty
    std::int32_t stack   = 0;
};

struct BankCacheData {
    std::vector<BankSlot> slots;       // full array; slot number = index
    long long             cached_at = 0;
};

// `kind` selects the cache subdir ("bank" -> bankcache, "metalbank" -> metalbankcache, ...).
bool          WriteContainerCache(const std::string& kind, const std::string& character,
                                  const std::vector<BankSlot>& slots);
BankCacheData ReadContainerCache(const std::string& kind, const std::string& character);

// Bank wrappers (kind = "bank").
bool          WriteBankCache(const std::string& character,
                             const std::vector<BankSlot>& slots);
BankCacheData ReadBankCache(const std::string& character);

}  // namespace rtx::reader
