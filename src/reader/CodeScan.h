#pragma once
// Static check of a client exe on disk: every code signature the companion attaches to, the
// reader's anchors, the engine ops it calls by number or name, the MainData displacement set the
// op handlers read, and the build-specific addresses. Read only; cached per exe file.
#include "HealthRun.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rtx::codescan {

// The client code group of a health run. `pid` 0 for an exe that is not running.
void Check(const std::wstring& exePath, rtx::health::Run& run);

// Build facts of an exe for the Build group: stamp, image size, flavour from its imports.
struct ExeFacts { bool ok = false; std::uint32_t stamp = 0, sizeOfImage = 0; std::string flavour, version; std::uint32_t optableRva = 0; int opMax = -1; };
ExeFacts Facts(const std::wstring& exePath);

// Every call of the sound play routine in the exe: the return address (an RVA) and the origin
// its place gives it (Signatures.h kSoundPacketOrigins; "other" when no rule holds it), in
// address order. The launcher labels a sound's origin by which of these called it.
struct SoundSite { std::uint32_t ret; const char* origin; };
std::vector<SoundSite> SoundCallSites(const std::wstring& exePath, std::uint32_t playRva);

// --sigs-record: the per-build manifest lines for this exe (build, sig, disp, rva, imports).
std::string Record(const std::wstring& exePath);
// --sigs-check: the client code group alone, as text.
std::string CheckText(const std::wstring& exePath);

}  // namespace rtx::codescan
