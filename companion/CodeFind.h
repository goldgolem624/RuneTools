#pragma once
// The function holding an address of the loaded client, from its exception table: the entry whose
// range holds it, followed through chained entries to the function's own start. 0 when no entry
// holds it. For signatures taken from inside a body (kInFunction in Signatures.h).
#include <windows.h>
#include <cstdint>

namespace rtx::codefind {

inline std::uint64_t FunctionStart(std::uint64_t base, std::uint64_t va) {
    if (!base || va < base) return 0;
    __try {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        if (!dir.VirtualAddress || !dir.Size) return 0;
        const auto* rf = reinterpret_cast<const RUNTIME_FUNCTION*>(base + dir.VirtualAddress);
        const std::uint32_t n = dir.Size / sizeof(RUNTIME_FUNCTION);
        const std::uint32_t rva = (std::uint32_t)(va - base);
        std::uint32_t lo = 0, hi = n;   // sorted by start
        while (lo < hi) {
            const std::uint32_t mid = lo + (hi - lo) / 2;
            if (rf[mid].BeginAddress <= rva) lo = mid + 1; else hi = mid;
        }
        if (!lo || rva >= rf[lo - 1].EndAddress) return 0;
        const RUNTIME_FUNCTION* e = &rf[lo - 1];
        for (int guard = 0; guard < 8; ++guard) {
            std::uint32_t info = e->UnwindData;
            if (info & 1) { e = reinterpret_cast<const RUNTIME_FUNCTION*>(base + (info & ~1u)); continue; }
            const std::uint8_t* u = reinterpret_cast<const std::uint8_t*>(base + info);
            if (!((u[0] >> 3) & UNW_FLAG_CHAININFO)) break;
            const std::uint32_t codes = (u[2] + 1u) & ~1u;
            e = reinterpret_cast<const RUNTIME_FUNCTION*>(u + 4 + codes * 2);
        }
        return base + e->BeginAddress;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

}  // namespace rtx::codefind
