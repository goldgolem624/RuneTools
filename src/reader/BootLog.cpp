#include "BootLog.h"

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace rtx::bootlog {
namespace {

std::string ReadTail(const std::wstring& path, std::uint64_t maxBytes) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); return {}; }
    const std::uint64_t size = (std::uint64_t)sz.QuadPart;
    const std::uint64_t from = size > maxBytes ? size - maxBytes : 0;
    LARGE_INTEGER pos; pos.QuadPart = (LONGLONG)from;
    SetFilePointerEx(h, pos, nullptr, FILE_BEGIN);
    std::string out((std::size_t)(size - from), '\0');
    DWORD got = 0; std::size_t at = 0;
    while (at < out.size()) {
        if (!::ReadFile(h, out.data() + at, (DWORD)std::min<std::size_t>(out.size() - at, 1u << 24), &got, nullptr) || !got) break;
        at += got;
    }
    out.resize(at);
    CloseHandle(h);
    return out;
}

// One hook line in the boot grammar: "hook: <name> ATTACHED rva=0x..", "hook: <name> NOT FOUND",
// "hook: <name> AMBIGUOUS hits=n", "hook: <name> REFUSED <why>", "hook: <name> attach FAILED".
bool ParseHook(const std::string& body, Hook& out) {
    if (body.rfind("hook: ", 0) != 0) return false;
    const std::string rest = body.substr(6);
    const std::size_t sp = rest.find(' ');
    if (sp == std::string::npos) return false;
    out.name = rest.substr(0, sp);
    const std::string tail = rest.substr(sp + 1);
    static const char* const kStates[] = { "ATTACHED", "NOT FOUND", "AMBIGUOUS", "REFUSED", "attach FAILED" };
    for (const char* s : kStates) {
        if (tail.rfind(s, 0) != 0) continue;
        out.state = std::strcmp(s, "attach FAILED") == 0 ? "FAILED" : s;
        const std::string more = tail.substr(std::strlen(s));
        const std::size_t rv = more.find("rva=");
        if (rv != std::string::npos) out.rva = (std::uint32_t)std::strtoul(more.c_str() + rv + 4, nullptr, 0);
        out.note = more;
        while (!out.note.empty() && out.note[0] == ' ') out.note.erase(0, 1);
        return true;
    }
    return false;
}

// Start-up lines from companions that predate the hook grammar, mapped onto it.
bool ParseLegacy(const std::string& body, Hook& out) {
    struct Map { const char* prefix; const char* name; const char* state; };
    static const Map kMap[] = {
        { "netprobe: framer hook ATTACHED", "framer", "ATTACHED" },
        { "netprobe: framer NOT FOUND", "framer", "NOT FOUND" },
        { "netprobe: framer hook attach FAILED", "framer", "FAILED" },
        { "sound: mix hook ATTACHED", "sound-synth", "ATTACHED" },
        { "sound: mix fn not found", "sound-synth", "NOT FOUND" },
        { "chat: message store hook attached", "chat-notify", "ATTACHED" },
        { "chat: message store routine not recognised", "chat-notify", "NOT FOUND" },
        { "menu: probe installed", "menu-init", "ATTACHED" },
        { "menu: string-init pattern not found", "menu-init", "NOT FOUND" },
        { "tooltip: text hook installed", "tooltip-stub", "ATTACHED" },
        { "tooltip: hover entry op not recognised", "tooltip-stub", "NOT FOUND" },
        { "markers: game arrow and tile routines found", "arrow-frame", "ATTACHED" },
        { "markers: game arrow and tile routines not recognised", "arrow-frame", "NOT FOUND" },
    };
    for (const auto& m : kMap) {
        if (body.rfind(m.prefix, 0) != 0) continue;
        out.name = m.name; out.state = m.state;
        const std::size_t rv = body.find("rva=");
        if (rv != std::string::npos) out.rva = (std::uint32_t)std::strtoul(body.c_str() + rv + 4, nullptr, 0);
        return true;
    }
    return false;
}

}  // namespace

Boot Read(std::uint32_t pid) {
    Boot b;
    wchar_t up[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH)) return b;
    const std::wstring path = std::wstring(up) + L"\\rtx_ring.log";
    char tag[48];
    std::snprintf(tag, sizeof(tag), " pid=%u] ", pid);
    const std::string bootMark = std::string(tag) + "=== BOOT";
    std::string text = ReadTail(path, 4ull << 20);
    std::size_t at = text.rfind(bootMark);
    if (at == std::string::npos) {
        // the log rolls to rtx_ring.log.1 at 16 MB: the boot record can sit in the older half
        text = ReadTail(path + L".1", 64ull << 20) + ReadTail(path, 64ull << 20);
        at = text.rfind(bootMark);
        if (at == std::string::npos) return b;
    }
    const std::size_t lineStart = text.rfind('\n', at);
    std::size_t p = lineStart == std::string::npos ? 0 : lineStart + 1;
    b.found = true;
    {
        const std::size_t sp = text.find(' ', p);
        if (sp != std::string::npos && text[p] == '[') b.at = text.substr(p + 1, sp - p - 1);
    }
    bool first = true;
    while (p < text.size()) {
        std::size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(p, e - p);
        p = e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t tp = line.find(tag);
        if (tp == std::string::npos) continue;              // another client's line
        const std::string body = line.substr(tp + std::strlen(tag));
        if (body.rfind("=== BOOT", 0) == 0) { if (!first) break; first = false; continue; }
        Hook hk;
        if (ParseHook(body, hk) || ParseLegacy(body, hk)) {
            bool merged = false;
            for (auto& x : b.hooks) if (x.name == hk.name) { x = hk; merged = true; break; }
            if (!merged) b.hooks.push_back(hk);
            continue;
        }
        if (body.rfind("hooks: ", 0) == 0) {
            std::sscanf(body.c_str(), "hooks: %d attached, %d missing", &b.attached, &b.missing);
            continue;
        }
        if (body.rfind("diag: ", 0) == 0) {
            b.beats.push_back(body);
            if (b.beats.size() > 8) b.beats.erase(b.beats.begin());
            continue;
        }
        if (b.notes.size() < 40 &&
            (body.rfind("engine ops:", 0) == 0 || body.rfind("cc:", 0) == 0 || body.rfind("compositor:", 0) == 0 ||
             body.rfind("outline:", 0) == 0 || body.rfind("scene root:", 0) == 0 || body.rfind("menu:", 0) == 0))
            b.notes.push_back(body);
    }
    return b;
}

}  // namespace rtx::bootlog
