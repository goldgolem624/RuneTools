#pragma once
// The script export against the cache as it is now: the scripts it holds versus the cache's script
// count, and what the cache stores for the indexes it reads (reference table and archive rows) versus
// what the launcher recorded when the export started (cache_state.json beside meta.json). The game
// client rewrites index files without changing them, so file write times stand in only for an export
// made before the state was recorded.

#include "../cache/CacheReader.h"

#include <windows.h>
#include <cstdio>
#include <ctime>
#include <string>

namespace rtx::cs2fresh {

// configs, interfaces, scripts, locs, enums, npcs, items, structs, achievements
inline constexpr int kRead[] = { 2, 3, 12, 16, 17, 18, 19, 22, 57 };

struct Fresh {
    int cacheScripts = -1;     // index 12 archives, -1 when the cache is not readable
    std::string newer;         // indexes whose stored state is not the one recorded at the export's start, "2, 12"
    std::string written;       // indexes with no recorded state written after the export started
    bool recorded = false;     // cache_state.json belongs to this export
};

// Each read index as the cache stores it now, one "i":{...} per line; an index that cannot be read
// is left out.
inline std::string StateNow() {
    std::string out;
    for (int i : kRead) {
        const rtx::cache::IndexStored s = rtx::cache::IndexStoredNow(i);
        if (!s.ok) continue;
        out += std::string(out.empty() ? "" : ",\n") + "\"" + std::to_string(i) + "\":{\"version\":" + std::to_string(s.refVersion) +
               ",\"crc\":" + std::to_string(s.refCrc) + ",\"rows\":" + std::to_string(s.rows) + ",\"hash\":" + std::to_string(s.hash) + "}";
    }
    return out;
}

// cache_state.json for the export whose meta.json date is `date`, from StateNow() at its start.
inline std::string StateFile(const std::string& date, const std::string& state) {
    return "{\"date\":\"" + date + "\",\"indexes\":{\n" + state + "\n}}\n";
}

// The first "date" string in a JSON text (meta.json, cache_state.json), empty when there is none.
inline std::string DateOf(const std::string& json) {
    std::size_t at = json.find("\"date\"");
    if (at == std::string::npos) return {};
    at = json.find(':', at + 6);
    if (at == std::string::npos) return {};
    const std::size_t q = json.find('"', at + 1);
    if (q == std::string::npos) return {};
    const std::size_t e = json.find('"', q + 1);
    return e == std::string::npos ? std::string() : json.substr(q + 1, e - q - 1);
}

struct IndexState { bool known = false; long long version = 0, crc = 0; int rows = 0; unsigned long long hash = 0; };

inline IndexState StateOf(const std::string& state, int index) {
    IndexState s;
    const std::string k = "\"" + std::to_string(index) + "\":{";
    const std::size_t at = state.find(k);
    if (at == std::string::npos) return s;
    int end = -1;   // set only when the entry is whole, up to its closing brace
    s.known = std::sscanf(state.c_str() + at + k.size(), "\"version\":%lld,\"crc\":%lld,\"rows\":%d,\"hash\":%llu}%n",
                          &s.version, &s.crc, &s.rows, &s.hash, &end) == 4 && end > 0;
    return s;
}

// date = the export's start as meta.json gives it (2026-10-06T02:38:01.504Z); state = cache_state.json's
// text, empty when there is none. An index the state holds is newer when its reference table or
// archive rows differ now (one the cache cannot read now is left out: a busy file is no change); any
// other index falls back to its file's write time.
inline Fresh Check(const std::string& date, const std::string& state) {
    Fresh f;
    const rtx::cache::IndexFacts ix = rtx::cache::IndexInfo(12);
    if (ix.open) f.cacheScripts = ix.archives;
    f.recorded = !date.empty() && DateOf(state) == date;
    long long exported = 0;
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (std::sscanf(date.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) == 6) {
        std::tm tm{}; tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d; tm.tm_hour = h; tm.tm_min = mi; tm.tm_sec = s;
        exported = (long long)_mkgmtime(&tm);
    }
    const std::string root = rtx::cache::CacheRoot();
    std::wstring wroot;
    if (!root.empty()) {
        const int n = MultiByteToWideChar(CP_UTF8, 0, root.data(), (int)root.size(), nullptr, 0);
        wroot.assign(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, root.data(), (int)root.size(), wroot.data(), n);
    }
    auto writtenAfter = [&](int i) {
        if (exported <= 0 || wroot.empty()) return false;
        WIN32_FILE_ATTRIBUTE_DATA a{};
        const std::wstring p = wroot + L"\\js5-" + std::to_wstring(i) + L".jcache";
        if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a)) return false;
        const unsigned long long t = ((unsigned long long)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime;
        return (long long)(t / 10000000ULL) - 11644473600LL > exported;
    };
    auto add = [](std::string& list, int i) { list += (list.empty() ? "" : ", ") + std::to_string(i); };
    for (int i : kRead) {
        const IndexState was = f.recorded ? StateOf(state, i) : IndexState{};
        if (was.known) {
            const rtx::cache::IndexStored now = rtx::cache::IndexStoredNow(i);
            if (now.ok && (now.refVersion != was.version || now.refCrc != was.crc || now.rows != was.rows || now.hash != was.hash))
                add(f.newer, i);
        } else if (writtenAfter(i)) {
            add(f.written, i);
        }
    }
    return f;
}

// Why the export is behind the cache, empty when it is not.
inline std::string Why(const Fresh& f, int exportedScripts) {
    std::string why;
    if (f.cacheScripts >= 0 && f.cacheScripts != exportedScripts)
        why = "the cache has " + std::to_string(f.cacheScripts) + " scripts, the export " + std::to_string(exportedScripts);
    auto indexes = [](const std::string& list) { return std::string(list.find(',') == std::string::npos ? "cache index " : "cache indexes ") + list; };
    if (!f.newer.empty()) why += (why.empty() ? "" : ", ") + indexes(f.newer) + " changed since it";
    if (!f.written.empty()) why += (why.empty() ? "" : ", ") + indexes(f.written) + " written after it";
    return why;
}

}  // namespace rtx::cs2fresh
