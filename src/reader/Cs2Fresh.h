#pragma once
// The script export against the cache as it is now: the scripts it holds versus the cache's script
// count, and the write times of the index files it reads versus the time it started.

#include "../cache/CacheReader.h"

#include <windows.h>
#include <cstdio>
#include <ctime>
#include <string>

namespace rtx::cs2fresh {

struct Fresh {
    int cacheScripts = -1;     // index 12 archives, -1 when the cache is not readable
    std::string newer;         // indexes written after the export started, "2, 12"
};

// date = the export's start as meta.json gives it (2026-10-06T02:38:01.504Z)
inline Fresh Check(const std::string& date) {
    Fresh f;
    const rtx::cache::IndexFacts ix = rtx::cache::IndexInfo(12);
    if (ix.open) f.cacheScripts = ix.archives;
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (std::sscanf(date.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6) return f;
    std::tm tm{}; tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d; tm.tm_hour = h; tm.tm_min = mi; tm.tm_sec = s;
    const long long exported = (long long)_mkgmtime(&tm);
    const std::string root = rtx::cache::CacheRoot();
    if (exported <= 0 || root.empty()) return f;
    const int n = MultiByteToWideChar(CP_UTF8, 0, root.data(), (int)root.size(), nullptr, 0);
    std::wstring wroot(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, root.data(), (int)root.size(), wroot.data(), n);
    static const int kRead[] = { 2, 3, 12, 16, 17, 18, 19, 22, 57 };   // configs, interfaces, scripts, locs, enums, npcs, items, structs, achievements
    for (int i : kRead) {
        WIN32_FILE_ATTRIBUTE_DATA a{};
        const std::wstring p = wroot + L"\\js5-" + std::to_wstring(i) + L".jcache";
        if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a)) continue;
        const unsigned long long t = ((unsigned long long)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime;
        if ((long long)(t / 10000000ULL) - 11644473600LL > exported) f.newer += (f.newer.empty() ? "" : ", ") + std::to_string(i);
    }
    return f;
}

// Why the export is behind the cache, empty when it is not.
inline std::string Why(const Fresh& f, int exportedScripts) {
    std::string why;
    if (f.cacheScripts >= 0 && f.cacheScripts != exportedScripts)
        why = "the cache has " + std::to_string(f.cacheScripts) + " scripts, the export " + std::to_string(exportedScripts);
    if (!f.newer.empty()) why += (why.empty() ? "" : ", ") + std::string("cache indexes ") + f.newer + " written after it";
    return why;
}

}  // namespace rtx::cs2fresh
