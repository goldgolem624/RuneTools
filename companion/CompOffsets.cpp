#include "CompOffsets.h"
#include "MainDataOffsets.h"
#include "SceneOffsets.h"
#include "../src/reader/Calibrate.h"
#include "PacketRefTable.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

namespace rtx::coff {

std::uint32_t status     = rtx::md::kStatus;
std::uint32_t account    = rtx::md::kAccount;
std::uint32_t players    = rtx::md::kPlayers;
std::uint32_t sceneViews = rtx::md::kSceneViews;
std::uint32_t varpMgr    = rtx::md::kVarpMgr;
std::uint32_t language   = rtx::md::kLanguage;
std::uint32_t soundCtx   = rtx::md::kSoundCtx;
std::uint32_t strStack   = 0x10A8;
std::uint32_t strSp      = 0x8DA8;
std::uint32_t entityRef  = 0xC3B0;
std::uint32_t entityObj  = 0xC3B8;

namespace {
std::atomic<bool> g_loaded{ false };
std::string g_why, g_missing, g_summary, g_packets;

struct Slot { const char* rule; std::uint32_t* at; };
const Slot kSlots[] = {
    { "kOffStatus",      &status },
    { "kOffAccount",     &account },
    { "md::kPlayers",    &players },
    { "md::kSceneViews", &sceneViews },
    { "md::kVarpMgr",    &varpMgr },
    { "md::kLanguage",   &language },
    { "md::kSoundCtx",   &soundCtx },
    { "stringStack",     &strStack },
    { "stringSp",        &strSp },
    { "stateEntityRef",  &entityRef },
    { "stateEntityObj",  &entityObj },
};
}  // namespace

bool Load() {
    if (g_loaded.load()) return true;
    wchar_t exe[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) { g_why = "the game's exe path is not readable"; return false; }
    std::wstring opcodes;
    wchar_t up[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH)) opcodes = std::wstring(up) + L"\\RuneToolsX\\cs2\\opcodes.json";
    const auto& found = rtx::calib::Run(exe, opcodes);
    g_why = rtx::calib::Why();
    std::string moved;
    for (const Slot& s : kSlots) {
        const std::uint32_t compiled = *s.at;
        bool proved = false;
        for (const auto& f : found) if (std::string(f.name) == s.rule && f.found) proved = true;
        *s.at = rtx::calib::Use(s.rule, compiled);
        if (!proved) g_missing += std::string(g_missing.empty() ? "" : ", ") + s.rule;
        else if (*s.at != compiled) {
            char b[96]; std::snprintf(b, sizeof(b), "%s%s 0x%X", moved.empty() ? "" : ", ", s.rule, *s.at);
            moved += b;
        }
    }
    // server packets by identity (PacketIds.h), from the same exe file
    {
        std::vector<std::uint8_t> file;
        HANDLE h = CreateFileW(exe, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER sz{};
            if (GetFileSizeEx(h, &sz) && sz.QuadPart > 0 && sz.QuadPart < (256ll << 20)) {
                file.resize((std::size_t)sz.QuadPart);
                std::size_t at = 0; DWORD got = 0;
                while (at < file.size() && ReadFile(h, file.data() + at, (DWORD)(file.size() - at), &got, nullptr) && got) at += got;
                if (at != file.size()) file.clear();
            }
            CloseHandle(h);
        }
        // an update can replace the file under a running game: only this exe's own file counts
        const auto* img = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(img + reinterpret_cast<const IMAGE_DOS_HEADER*>(img)->e_lfanew);
        std::uint32_t lf = 0, st = 0;
        if (file.size() > 0x40) std::memcpy(&lf, file.data() + 0x3C, 4);
        if (lf && file.size() > (std::size_t)lf + 12) std::memcpy(&st, file.data() + lf + 8, 4);
        if (st != nt->FileHeader.TimeDateStamp) file.clear();
        if (!file.empty()) rtx::pktid::SetCurrent(file.data(), file.size(), rtx::pktid::kRef, rtx::pktid::kRefCount);
        const auto& P = rtx::pktid::Current();
        char pl[200];
        if (P.ok) std::snprintf(pl, sizeof(pl), "packets: %d in this exe, %d of %d reference packets found (%d by name, %d by order, %d by length)",
                                P.liveCount, P.c.byName + P.c.byOrder + P.c.byLength, P.c.refCount, P.c.byName, P.c.byOrder, P.c.byLength);
        else std::snprintf(pl, sizeof(pl), "packets: not identified (%s), opcodes pass through", file.empty() ? "exe file not readable" : P.why.c_str());
        g_packets = pl;
    }
    char line[160];
    std::snprintf(line, sizeof(line), "offsets from the client's code: %d rules, %s", (int)found.size(), found.empty() ? "none ran" : "applied");
    g_summary = line;
    if (!moved.empty()) g_summary += "; moved: " + moved;
    if (!g_missing.empty()) g_summary += "; compiled (no rule proved them): " + g_missing;
    if (!g_why.empty()) g_summary += "; " + g_why;
    g_summary += "; " + g_packets;
    g_loaded.store(true);
    return !found.empty();
}

bool Loaded() { return g_loaded.load(); }
std::string Why() { return g_why; }
std::string Missing() { return g_missing; }
std::string Summary() { return g_summary; }

}  // namespace rtx::coff
