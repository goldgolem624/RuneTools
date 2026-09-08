#pragma once

#include <Windows.h>
#include <bcrypt.h>
#include <cstdio>
#include <mutex>
#include <string>

#pragma comment(lib, "bcrypt.lib")

// SHA-256(volumeSerial(C:) | MachineGuid | computerName), 64 hex chars.

namespace rtx::shared {

namespace detail {

inline std::string VolumeSerialDecimal() {
    DWORD serial = 0;
    if (!GetVolumeInformationW(L"C:\\", nullptr, 0, &serial,
                               nullptr, nullptr, nullptr, 0)) {
        serial = 0;
    }
    char buf[16]{};
    std::snprintf(buf, sizeof(buf), "%lu",
                  static_cast<unsigned long>(serial));
    return std::string(buf);
}

inline std::string RegStringUtf8(HKEY root, const wchar_t* subkey, const wchar_t* value) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ | KEY_WOW64_64KEY, &hKey)
        != ERROR_SUCCESS || !hKey) {
        return {};
    }
    wchar_t buf[256]{};
    DWORD size = sizeof(buf), type = 0;
    LONG q = RegQueryValueExW(hKey, value, nullptr, &type,
                              reinterpret_cast<LPBYTE>(buf), &size);
    RegCloseKey(hKey);
    if (q != ERROR_SUCCESS || type != REG_SZ) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, buf, -1,
                                     nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, out.data(), needed,
                        nullptr, nullptr);
    if (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

inline std::string ComputerNameUtf8() {
    wchar_t buf[256]{};
    DWORD size = static_cast<DWORD>(std::size(buf));
    if (!GetComputerNameW(buf, &size)) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, buf, -1,
                                     nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, out.data(), needed,
                        nullptr, nullptr);
    if (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

inline std::string Sha256Hex(const std::string& data) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM,
                                    nullptr, 0) != 0) {
        return {};
    }
    UCHAR digest[32]{};
    NTSTATUS st = BCryptHash(
        hAlg, nullptr, 0,
        reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
        static_cast<ULONG>(data.size()),
        digest, sizeof(digest));
    BCryptCloseAlgorithmProvider(hAlg, 0);
    if (st != 0) return {};

    char hex[65]{};
    for (int i = 0; i < 32; ++i) std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
    return std::string(hex);
}

}  // namespace detail

inline const std::string& GetMachineFingerprint() {
    static std::once_flag once;
    static std::string cached;
    std::call_once(once, []() {
        std::string raw;
        raw.reserve(256);
        raw += detail::VolumeSerialDecimal();
        raw += '|';
        raw += detail::RegStringUtf8(HKEY_LOCAL_MACHINE,
                                     L"SOFTWARE\\Microsoft\\Cryptography",
                                     L"MachineGuid");
        raw += '|';
        raw += detail::ComputerNameUtf8();
        cached = detail::Sha256Hex(raw);
        if (cached.empty()) {
            // Sentinel: never pin a key to an all-zeros fingerprint.
            cached.assign(64, 'f');
        }
    });
    return cached;
}

}  // namespace rtx::shared
