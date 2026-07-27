#include "MonitorFix.h"

#include <Windows.h>
#include <cstring>

namespace rtx::launcher {

namespace {

using GetMonitorInfoW_t = BOOL(WINAPI*)(HMONITOR, LPMONITORINFO);
GetMonitorInfoW_t g_real_get_monitor_info = nullptr;

// Replacement for user32!GetMonitorInfoW as seen by AppCore.dll. On the expected
// path it forwards. When AppCore's cached handle has gone stale the real call
// returns FALSE; rather than let AppCore show its modal box, the wrapper re-resolves the
// current primary monitor and retries so window sizing still gets real geometry.
BOOL WINAPI Hooked_GetMonitorInfoW(HMONITOR monitor, LPMONITORINFO info) {
    GetMonitorInfoW_t real = g_real_get_monitor_info;
    if (!real) return FALSE;
    if (real(monitor, info)) return TRUE;

    HMONITOR primary = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    if (primary && real(primary, info)) return TRUE;

    // No monitor answered (extremely rare -- e.g. all displays asleep mid call). Hand back sane
    // primary-ish geometry so AppCore proceeds silently; the window is resizable and gets
    // corrected on the next resize event.
    if (info && info->cbSize >= sizeof(MONITORINFO)) {
        info->rcMonitor = RECT{0, 0, 1920, 1080};
        info->rcWork    = RECT{0, 0, 1920, 1040};
        info->dwFlags   = MONITORINFOF_PRIMARY;
        return TRUE;
    }
    return FALSE;
}

// Locate the IAT slot for an imported function by name across every import
// descriptor (matching the descriptor's module name is unreliable -- the loader
// may record an api-set alias rather than "user32.dll"). Returns the address
// of the writable function pointer, or nullptr if not imported.
FARPROC* find_iat_slot(HMODULE module, const char* func) {
    auto* base = reinterpret_cast<BYTE*>(module);
    auto* dos  = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

    const auto& dir =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return nullptr;

    auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
    for (; imp->Name; ++imp) {
        // OriginalFirstThunk holds the import names; FirstThunk is the live IAT.
        // Some images omit the former, in which case FirstThunk still carries
        // the name thunks pre-binding.
        DWORD names_rva = imp->OriginalFirstThunk ? imp->OriginalFirstThunk
                                                  : imp->FirstThunk;
        if (!names_rva) continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + names_rva);
        auto* iat   = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++iat) {
            if (names->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;  // by ordinal
            auto* by_name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(by_name->Name), func) == 0)
                return reinterpret_cast<FARPROC*>(&iat->u1.Function);
        }
    }
    return nullptr;
}

}  // namespace

bool InstallMonitorInfoFix() {
    if (g_real_get_monitor_info) return true;  // already installed

    HMODULE appcore = GetModuleHandleW(L"AppCore.dll");
    if (!appcore) return false;

    FARPROC* slot = find_iat_slot(appcore, "GetMonitorInfoW");
    if (!slot) return false;

    g_real_get_monitor_info = reinterpret_cast<GetMonitorInfoW_t>(*slot);

    DWORD old_protect = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old_protect)) {
        g_real_get_monitor_info = nullptr;
        return false;
    }
    *slot = reinterpret_cast<FARPROC>(&Hooked_GetMonitorInfoW);
    VirtualProtect(slot, sizeof(*slot), old_protect, &old_protect);
    return true;
}

}  // namespace rtx::launcher
