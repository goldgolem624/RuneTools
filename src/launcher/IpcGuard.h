#pragma once
// Creation helpers for the launcher's named sections and events.
//
// The launcher owns creation of every one of these names; the module side only
// opens them. These wrappers apply a fixed security descriptor and report
// whether the name was already present, so callers can log an unexpected state
// rather than proceed silently.

#include <Windows.h>
#include <sddl.h>
#include <cstdint>
#include <string>

namespace rtx::ipc {

// Owner-only descriptor, built once. Returns nullptr if it could not be built,
// in which case the caller still gets an object with the process default.
inline SECURITY_ATTRIBUTES* OwnerOnlySa() {
    static bool                 initialised = false;
    static SECURITY_ATTRIBUTES  sa{};
    static PSECURITY_DESCRIPTOR sd = nullptr;
    static bool                 ok = false;

    if (!initialised) {
        initialised = true;
        // D:P            protected, no inheritance
        // (A;;GA;;;OW)   object owner, that is the creating user
        // (A;;GA;;;SY)   SYSTEM
        const wchar_t* kSddl = L"D:P(A;;GA;;;OW)(A;;GA;;;SY)";
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(kSddl, SDDL_REVISION_1, &sd, nullptr)) {
            sa.nLength              = sizeof(sa);
            sa.lpSecurityDescriptor = sd;
            sa.bInheritHandle       = FALSE;
            ok = true;
        }
    }
    return ok ? &sa : nullptr;
}

// preexisting reports that the name was already present when we created it.
inline HANDLE CreateSection(const wchar_t* name, std::uint32_t bytes, bool& preexisting) {
    preexisting = false;
    SetLastError(0);
    HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, OwnerOnlySa(), PAGE_READWRITE,
                                  0, (DWORD)bytes, name);
    if (h && GetLastError() == ERROR_ALREADY_EXISTS) preexisting = true;
    return h;
}

inline HANDLE CreateEvent(const wchar_t* name, BOOL manualReset, BOOL initialState, bool& preexisting) {
    preexisting = false;
    SetLastError(0);
    HANDLE h = CreateEventW(OwnerOnlySa(), manualReset, initialState, name);
    if (h && GetLastError() == ERROR_ALREADY_EXISTS) preexisting = true;
    return h;
}

}   // namespace rtx::ipc
