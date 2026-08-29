// RuneToolsX client companion module.
//
// Runs inside the game client, reads the client's own memory, and publishes the
// live scene-object list (runtime-placed locs the launcher cannot read from
// outside) into a shared section the launcher maps.
//
// Approach: locate the scene-object container by structure (a list of scene
// entities whose object-data type byte marks scenery), validate strictly to avoid
// false matches, then each tick copy {config id, tile, plane} for every scenery
// entry. Names/actions are resolved launcher-side from the loc cache.

#include <winsock2.h>   // MUST precede windows.h: windows.h pulls in the old winsock.h
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <mutex>
#include <atomic>
#include <utility>
#include <vector>
#include <string>
#include <fstream>

#include "SceneOffsets.h"   // client memory offsets shared with the launcher's Reader.cpp
#include <cstdio>
#include <cstdarg>
#include <unordered_map>
#include <unordered_set>
#include <detours.h>
#include "SceneShare.h"
#include "VarcShare.h"
#include "RenderShare.h"
#include "SpecialShare.h"   // transient render-pass highlights (the clue-scan ring)
#include "GroundShare.h"    // dropped ground items (scene entity type 3)
#include "NetShare.h"       // raw inbound socket bytes (recv/WSARecv), for wire capture
#include "NetProbeShare.h"  // DECODED server->client packets, captured at the inbound framer
#include "Present.h"     // in-frame compositor (draws the launcher UI into the game frame)
#include "SoundFilter.h" // cache-sound observation + muting (audio-chunk mix hook)
#include "MenuProbe.h"  // right-click menu entry-layout probe (diagnostic, opt-in)

namespace {

using rtx::scene::Share;
using rtx::scene::Object;

// Distinct scene-object containers to track near the player. Many objects sit in
// their OWN single-entity container (isolated decor: plaques, fountains, ...), so in
// the worst case there are as many containers as objects -- size this to the object
// cap so the container limit is never the bottleneck. (g_mgrs is ~32KB.)
constexpr int kMaxMgrs = rtx::scene::kMaxObjects;
std::uint64_t g_base = 0;     // game module base
std::uint64_t g_size = 0;

// Diagnostic file log -> %USERPROFILE%\rtx_ring.log (ms-since-boot timestamp).
// fopen per line (cheap).
std::mutex g_ringLogMu;
void RingLog(const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_ringLogMu);
    char path[MAX_PATH] = "C:\\rtx_ring.log";
    char home[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("USERPROFILE", home, sizeof(home));
    if (n > 0 && n < sizeof(home)) std::snprintf(path, sizeof(path), "%s\\rtx_ring.log", home);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "a") != 0 || !f) return;
    char buf[512];
    va_list ap; va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    std::fprintf(f, "[%llu pid=%lu] %s\n", (unsigned long long)GetTickCount64(), (unsigned long)GetCurrentProcessId(), buf);
    std::fclose(f);
}

std::uint64_t g_mgrs[kMaxMgrs] = {0};   // distinct scene-object containers near the player
int           g_mgrCount = 0;

// ---- crash-safe reads of this process's own memory ------------------------
template <class T>
bool TryRead(std::uint64_t addr, T& out) {
    if (addr < 0x10000) return false;
    __try { out = *reinterpret_cast<const T*>(addr); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
std::uint64_t R64(std::uint64_t a) { std::uint64_t v = 0; return TryRead(a, v) ? v : 0; }
std::int32_t  R32(std::uint64_t a) { std::int32_t  v = 0; return TryRead(a, v) ? v : 0; }
float         RF (std::uint64_t a) { float         v = 0; return TryRead(a, v) ? v : 0.f; }
std::uint8_t  R8 (std::uint64_t a) { std::uint8_t  v = 0; return TryRead(a, v) ? v : 0; }

bool InModule(std::uint64_t a) { return a >= g_base && a < g_base + (g_size ? g_size : 0x2000000); }
bool IsHeap(std::uint64_t a)   { return a > 0x10000 && a < 0x7FFFFFFFFFFFull && !InModule(a); }

// MainData root global; the whole scene side hangs off it, so resolution is the most
// update-critical step in the companion. Two-stage and self-checking:
//   1. a byte anchor as the fast path, whose result is validated before use;
//   2. otherwise a structural search: walk the image's static qwords and take the
//      first whose chain resolves. It tests the data shape, never instruction bytes.
// g_rootMethod records which path won, so a silent downgrade to the slow path shows.
std::uint64_t g_rootGlobal = 0;   // address OF the global; deref for the root
std::uint32_t g_rootMethod = 0;   // 0 = unresolved, 1 = byte anchor, 2 = structural scan

// STRUCTURAL validation: does the qword at `globalAddr` lead to a live worldView?
// root -> container -> entry array -> worldView -> scene worker -> entity vector holding
// real scenery. No code bytes are involved.
bool RootGlobalValid(std::uint64_t globalAddr) {
    std::uint64_t root = R64(globalAddr);
    if (!IsHeap(root)) return false;
    std::uint64_t cont = R64(root + rtx::scn::kContainer);
    if (!IsHeap(cont)) return false;
    std::int32_t idx = R32(cont + rtx::scn::kActiveIdx);
    std::uint64_t arr = R64(cont + rtx::scn::kEntryArr);
    if (idx < 0 || idx > 64 || !IsHeap(arr)) return false;
    std::uint64_t W = R64(arr + (std::uint64_t)idx * 0x10 + rtx::scn::kEntryWv);
    if (!IsHeap(W)) return false;
    // The worker offset is itself runtime-resolved elsewhere; sweep the window rather
    // than depend on a constant that may also have moved.
    for (std::uint32_t wo = 0x10000; wo < 0x10400; wo += 8) {
        std::uint64_t wk = W + wo;
        std::uint64_t vb = R64(wk + rtx::scn::kVecBegin), ve = R64(wk + rtx::scn::kVecEnd);
        if (!IsHeap(vb) || ve <= vb || ((ve - vb) & 7)) continue;
        std::uint64_t n = (ve - vb) / 8;
        if (!n || n > 30000) continue;
        int scenery = 0;
        for (std::uint64_t i = 0; i < n && i < 512 && scenery < 4; ++i) {
            std::uint64_t ep = R64(vb + i * 8);
            if (!IsHeap(ep)) continue;
            std::uint64_t sub = R64(ep + rtx::scn::kSecPtr);
            if (!IsHeap(sub)) continue;
            std::uint8_t t = R8(sub + rtx::scn::kType);
            if (t == 0 || t == 12) ++scenery;
        }
        if (scenery >= 4) return true;    // a real scene, not a coincidental shape
    }
    return false;
}

std::uint64_t ScanRootGlobal() {
    const std::uint8_t* img = reinterpret_cast<const std::uint8_t*>(g_base);
    std::uint64_t n = (g_size ? g_size : 0x2000000);
    if (n < 0x1300) return 0;
    // --- 1. fast path: the code anchor, VALIDATED before it is trusted -------------
    static const std::uint8_t kAnchor[] = { 0x48, 0x2D, 0xA8, 0x00, 0x00, 0x00, 0x48, 0x83 };
    __try {
        for (std::uint64_t i = 0x1000; i + 0x220 < n; ++i) {
            if (std::memcmp(img + i, kAnchor, sizeof(kAnchor)) != 0) continue;
            const std::uint8_t* fn = img + i - 32;   // anchor sits 32 bytes into the fn
            for (int j = 0; j + 7 <= 0x200; ++j) {
                if (fn[j] == 0x48 && fn[j + 1] == 0x89 && fn[j + 2] == 0x05) {
                    std::int32_t disp = 0;
                    std::memcpy(&disp, fn + j + 3, 4);
                    std::uint64_t cand = (std::uint64_t)(fn + j + 7) + (std::int64_t)disp;
                    if (RootGlobalValid(cand)) { g_rootMethod = 1; return cand; }
                    break;                            // wrong global -> try the next anchor
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    // --- 2. fallback: structural search over the image's static qwords -------------
    // Every candidate is proven by RootGlobalValid, so a hit is correct by construction
    __try {
        for (std::uint64_t o = 0; o + 8 <= n; o += 8) {
            std::uint64_t cand = g_base + o;
            std::uint64_t v = R64(cand);
            if (!IsHeap(v)) continue;
            if (RootGlobalValid(cand)) { g_rootMethod = 2; return cand; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_rootMethod = 0;
    return 0;
}
std::uint64_t Root() {
    // Re-resolve if the cached global goes stale, throttled to once every 2s:
    // RootGlobalValid sweeps up to 128 worker offsets and reads hundreds of entities,
    // and Root() runs on every pass of the publish loop.
    static ULONGLONG s_nextCheck = 0;
    const ULONGLONG now = GetTickCount64();
    if (g_rootGlobal && now >= s_nextCheck) {
        s_nextCheck = now + 2000;
        if (!RootGlobalValid(g_rootGlobal)) { g_rootGlobal = 0; g_rootMethod = 0; }
    }
    if (!g_rootGlobal) g_rootGlobal = ScanRootGlobal();
    return g_rootGlobal ? R64(g_rootGlobal) : 0;
}

// Chain/entity offsets come from SceneOffsets.h, shared with the launcher's
// Reader.cpp -- edit them there.
constexpr std::uint64_t kSecPtr   = rtx::scn::kSecPtr;    // entity -> object-data (sub)
constexpr std::uint64_t kType     = rtx::scn::kType;      // sub: type byte (0/12 = scenery)
constexpr std::uint64_t kFloor    = rtx::scn::kPlane;     // sub: plane
constexpr std::uint64_t kEntPosX  = 0x30;                 // entity: float X (fine, companion-only)
constexpr std::uint64_t kEntPosY  = 0x38;                 // entity: float Y (fine, companion-only)
constexpr std::uint64_t kVecBegin = rtx::scn::kVecBegin;  // worker: entity vector begin
constexpr std::uint64_t kVecEnd   = rtx::scn::kVecEnd;    // worker: entity vector end

// worldView -> scene-worker offset, resolved at RUNTIME: it moves when a game
// update grows the worldView class (0x10150 -> 0x10170 on build 949). The cached
// offset is cheap-validated on use and the worldView block rescanned (throttled)
// when it stops walking like an entity vector. Worker-thread only (no locking).
std::uint32_t g_workerOff = rtx::scn::kWorkerOffDefault;
std::uint64_t g_workerScanMs = 0;
bool WorkerWalks(std::uint64_t wk) {
    if (!IsHeap(wk)) return false;
    std::uint64_t vb = R64(wk + kVecBegin), ve = R64(wk + kVecEnd);
    if (!IsHeap(vb) || ve < vb || (ve - vb) % 8) return false;
    std::uint64_t n = (ve - vb) / 8;
    if (n < 4 || n > 100000) return false;
    int typed = 0;
    for (std::uint64_t i = 0; i < n && i < 48 && typed < 4; ++i) {
        std::uint64_t ep = R64(vb + i * 8);
        if (!IsHeap(ep)) continue;
        std::uint64_t sub = R64(ep + kSecPtr);
        if (!IsHeap(sub)) continue;
        int t = R8(sub + kType);
        if (t <= 5 || (t >= 10 && t <= 13)) ++typed;
    }
    return typed >= 4;
}
std::uint64_t SceneWorker(std::uint64_t W) {
    if (!IsHeap(W)) return 0;
    std::uint64_t wk = R64(W + g_workerOff);
    if (WorkerWalks(wk)) return wk;
    std::uint64_t now = GetTickCount64();
    if (now - g_workerScanMs < 3000) return 0;
    g_workerScanMs = now;
    for (std::uint32_t o = 0; o < 0x14000; o += 8) {
        wk = R64(W + o);
        if (WorkerWalks(wk)) {
            g_workerOff = o;
            RingLog("scene worker offset moved: worldView+0x%X", o);
            return wk;
        }
    }
    return 0;
}
// Config-id offset on the sub struct. Walls/decor keep a pointer at this offset
// instead, so reads are range-gated (see ReadConfig) and fall back to unnamed.
constexpr std::uint64_t kConfigId = 0xa8;

// Loc id within the loc-def struct that sub+0xa8 points at for the def-pointer form.
constexpr std::uint64_t kDefLocId = 0x28;

// Live-model instance block on the sec (sub). This 0x18..0x30 region is populated only
// when the loc has a rendered model this frame; a loc sitting in the worldview with a
// valid config, action and cached loc-def AABB but never drawn (phantom locs, cleared
// or uninstantiated locs) has the whole block zeroed. Gates the published AABB.
constexpr std::uint64_t kModelLo = 0x18, kModelMid = 0x20, kModelHi = 0x30;

// sub+0xa8 has two forms: type-0 scenery stores the loc id inline as an int;
// type-12 (walls/decor) stores a pointer to the loaded loc def, id at def+0x28.
// Distinguish by whether the 64-bit value is a readable heap pointer.
// Returns 0 when neither yields a sane id (launcher leaves it unnamed).
std::int32_t ReadConfig(std::uint64_t sub) {
    std::uint64_t v = R64(sub + kConfigId);
    if (IsHeap(v)) {                                   // def-pointer form
        std::int32_t id = 0;
        if (TryRead(v + kDefLocId, id) && id >= 1 && id <= 200000) return id;
    }
    std::int32_t lo = R32(sub + kConfigId);            // inline form
    return (lo >= 1 && lo <= 200000) ? lo : 0;
}

// Live model world AABB on the entity: min XYZ at +0x40, max XYZ at +0x50, each
// interleaved as (X=east, Y=up, Z=north). Published as (east,north,up).
constexpr std::uint64_t kBoxMinX = 0x40, kBoxMinY = 0x44, kBoxMinZ = 0x48;
constexpr std::uint64_t kBoxMaxX = 0x50, kBoxMaxY = 0x54, kBoxMaxZ = 0x58;

bool IsFin(float v) { return v == v && v > -3.0e38f && v < 3.0e38f; }

// Read + validate the entity's world AABB into o (east,north,up order). On any
// problem, mark it absent (bmax.x == bmin.x) so the launcher uses the footprint.
void ReadBox(std::uint64_t ep, Object& o) {
    float mnx = RF(ep + kBoxMinX), mny = RF(ep + kBoxMinY), mnz = RF(ep + kBoxMinZ);
    float mxx = RF(ep + kBoxMaxX), mxy = RF(ep + kBoxMaxY), mxz = RF(ep + kBoxMaxZ);
    const float kLim = 60.f * 512.f;
    bool ok = IsFin(mnx) && IsFin(mny) && IsFin(mnz) && IsFin(mxx) && IsFin(mxy) && IsFin(mxz) &&
              mxx > mnx && mxz > mnz && mxy >= mny &&
              (mxx - mnx) < kLim && (mxz - mnz) < kLim && (mxy - mny) < kLim;
    if (ok) {
        o.bmin[0] = mnx; o.bmin[1] = mnz; o.bmin[2] = mny;   // east, north, up
        o.bmax[0] = mxx; o.bmax[1] = mxz; o.bmax[2] = mxy;
    } else {
        o.bmin[0] = o.bmin[1] = o.bmin[2] = 0.f;
        o.bmax[0] = o.bmax[1] = o.bmax[2] = 0.f;             // bmax.x == bmin.x -> absent
    }
}

bool IsScenery(int t) { return t == 0 || t == 12; }

// Is `ep` a plausible scene entity with a scenery sub at a sane tile?
bool ValidSceneryEntity(std::uint64_t ep, int& typeOut, int& txOut, int& tyOut) {
    if (!IsHeap(ep)) return false;
    std::uint64_t sub = R64(ep + kSecPtr);
    if (!IsHeap(sub)) return false;
    int t = R8(sub + kType);
    if (!IsScenery(t)) return false;
    float fx = RF(ep + kEntPosX), fy = RF(ep + kEntPosY);
    int tx = (int)(fx / 512.f), ty = (int)(fy / 512.f);
    if (tx <= 0 || ty <= 0 || tx > 16384 || ty > 16384) return false;
    typeOut = t; txOut = tx; tyOut = ty;
    return true;
}

// Count scenery entries in a candidate container's vector (sampled). Strict: needs
// several valid scenery entities so random heap doesn't match.
int ScoreContainer(std::uint64_t mgr, int& total) {
    total = 0;
    std::uint64_t vb = R64(mgr + kVecBegin), ve = R64(mgr + kVecEnd);
    if (!IsHeap(vb) || ve <= vb) return 0;
    std::uint64_t n = (ve - vb) / 8;
    if (n == 0 || n > 30000) return 0;
    total = (int)n;
    int scen = 0, sampled = 0;
    for (std::uint64_t i = 0; i < n && sampled < 80; ++i) {
        std::uint64_t ep = R64(vb + i * 8);
        if (ep == 0) continue;
        ++sampled;
        int t, tx, ty;
        if (ValidSceneryEntity(ep, t, tx, ty)) ++scen;
    }
    return scen;
}

// Each scenery entity stores a pointer to its container at entity+0x130.
constexpr std::uint64_t kEntMgr = 0x130;

// Local-player fine position, via the worldView chain (same the launcher uses).
bool PlayerFine(float& px, float& py) {
    std::uint64_t root = Root();
    std::uint64_t cont = R64(root + rtx::scn::kContainer);
    int idx = R32(cont + rtx::scn::kActiveIdx);
    std::uint64_t arr = R64(cont + rtx::scn::kEntryArr);
    if (!IsHeap(arr) || idx < 0) return false;
    std::uint64_t W = R64(arr + (std::uint64_t)idx * 0x10 + rtx::scn::kEntryWv);
    std::uint64_t wk = SceneWorker(W);
    std::uint64_t vb = R64(wk + kVecBegin), ve = R64(wk + kVecEnd);
    if (!IsHeap(vb) || ve <= vb) return false;
    std::uint64_t pdata = R64(root + rtx::scn::kPlayerData);
    int luid = IsHeap(pdata) ? R32(pdata + rtx::scn::kLocalUid) : -1;
    std::uint64_t n = (ve - vb) / 8; if (n > 30000) n = 30000;
    for (std::uint64_t i = 0; i < n; ++i) {
        std::uint64_t ep = R64(vb + i * 8);
        if (!IsHeap(ep)) continue;
        std::uint64_t sub = R64(ep + kSecPtr);
        if (!IsHeap(sub)) continue;
        if (R8(sub + kType) == 2 && R32(sub + rtx::scn::kUid) == luid) {
            px = RF(sub + rtx::scn::kPosX); py = RF(sub + rtx::scn::kPosY);
            return px != 0.f && py != 0.f;
        }
    }
    return false;
}

// Last known good player position. The position only bounds the container scan and
// scene objects do not move, so a remembered position is a good enough bound: a
// position failure degrades scan tightness instead of disabling capture entirely.
float g_lastPlayerX = 0.f, g_lastPlayerY = 0.f;
bool  g_havePlayerPos = false;
int   g_posSource = 0;             // 0 = none, 1 = live, 2 = remembered (diag[11])

// Live position when available, otherwise the last one seen. Returns false only when
// no position has ever been read -- there is nothing to bound a scan with.
bool PlayerFineOrLast(float& px, float& py) {
    if (PlayerFine(px, py)) {
        g_lastPlayerX = px; g_lastPlayerY = py; g_havePlayerPos = true; g_posSource = 1;
        return true;
    }
    if (g_havePlayerPos) { px = g_lastPlayerX; py = g_lastPlayerY; g_posSource = 2; return true; }
    g_posSource = 0;
    return false;
}

void AddMgr(std::uint64_t mgr) {
    for (int i = 0; i < g_mgrCount; ++i) if (g_mgrs[i] == mgr) return;
    if (g_mgrCount < kMaxMgrs) g_mgrs[g_mgrCount++] = mgr;
}

// Float-pair scan of one region for scenery entities near (px,py): an entity has
// float X@+0x30 and Y@+0x38. For each hit, validate the sub type and record its
// container (entity+0x130). One SEH guard per region keeps faults contained.
void ScanRegionForManagers(std::uint64_t rbase, std::uint64_t rsize, float px, float py, float rng) {
    __try {
        const float* f = reinterpret_cast<const float*>(rbase);
        std::uint64_t fn = rsize / 4;
        for (std::uint64_t i = 0; i + 3 < fn && g_mgrCount < kMaxMgrs; ++i) {
            float fx = f[i];
            if (fx <= px - rng || fx >= px + rng) continue;
            float fy = f[i + 2];                         // entity+0x38 = +8 bytes = +2 floats
            if (fy <= py - rng || fy >= py + rng) continue;
            std::uint64_t ent = rbase + i * 4 - kEntPosX;
            if (!IsHeap(ent)) continue;
            std::uint64_t sub = R64(ent + kSecPtr);
            if (!IsHeap(sub) || !IsScenery(R8(sub + kType))) continue;
            // Confirm the entity<->sub link (sub+0x8 points back to the entity). This is a
            // near-perfect filter for the float-scan's coincidental hits, so even a
            // single-entity container below is safe to accept.
            if (R64(sub + 0x8) != ent) continue;
            std::uint64_t mgr = R64(ent + kEntMgr);
            int total = 0;
            // Accept score >= 1: many real objects (plaques, fountains, isolated decor)
            // live alone in their own container. The backptr check above already
            // rejects bogus seeds, so a real seed's container is always legit.
            if (IsHeap(mgr) && ScoreContainer(mgr, total) >= 1) AddMgr(mgr);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Discover scene-object containers near the player (walls + scenery live in separate
// containers) by locating scenery entities near the local player and following their
// container back-pointers. ACCUMULATES into g_mgrs (AddMgr dedupes) -- it never clears,
// so existing captures persist; PruneInvalid drops only dead ones. Idempotent to call
// repeatedly as the player moves to pick up newly in-range containers.
// Defined further down, beside the tracked-entity set it reads.
int FindContainersFromTracked();

// `deep` = also run the full heap sweep. See the cost note on the fast path below.
void FindContainers(bool deep) {
    float px = 0, py = 0;
    if (!PlayerFineOrLast(px, py)) return;   // never had a position -> nothing to bound with
    // Fast path first: the spawn hook already holds every entity it saw spawn, and each
    // one knows its own container at ent+kEntMgr, so following those is a few hundred
    // pointer reads. The deep sweep below walks every committed R/W region in the
    // process -- thousands of them, gigabytes -- hunting float pairs, and takes seconds.
    if (FindContainersFromTracked() > 0 && !deep) return;
    const float rng = 64.f * 512.f;                     // within ~64 tiles (>= the panel's max range)
    MEMORY_BASIC_INFORMATION mbi;
    std::uint64_t addr = 0x10000;
    while (addr < 0x7FFFFFFFFFFFull && g_mgrCount < kMaxMgrs) {
        if (!VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi))) break;
        std::uint64_t rbase = (std::uint64_t)mbi.BaseAddress, rsize = (std::uint64_t)mbi.RegionSize;
        if (mbi.State == MEM_COMMIT &&
            (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE) &&
            rsize <= 64ull * 1024 * 1024) {
            ScanRegionForManagers(rbase, rsize, px, py, rng);
        }
        addr = rbase + rsize;
        if (addr <= rbase) break;
    }
}

// Drop only containers that are genuinely gone (chunk unloaded -> vector freed /
// reused), keeping every still-structurally-valid one. This is conservative on
// purpose: a live container is never removed just because it's momentarily empty,
// so captured objects never flicker out. Compacts g_mgrs in place.
void PruneInvalid() {
    int w = 0;
    for (int i = 0; i < g_mgrCount; ++i) {
        std::uint64_t mgr = g_mgrs[i];
        std::uint64_t vb = R64(mgr + kVecBegin), ve = R64(mgr + kVecEnd);
        if (IsHeap(vb) && ve >= vb && ((ve - vb) & 7) == 0 && (ve - vb) / 8 <= 30000)
            g_mgrs[w++] = mgr;                          // vector still well-formed -> keep
    }
    g_mgrCount = w;
}

// ---- client-variable values (varp / varbit / varc) -------------------------
// The CS2 VM reads a var through per-TYPE op handlers (type byte at
// registry-entry+0x20: 4 = varp, 5 = varc-int). int-stack base = vm_ctx+0x100,
// element count = vm_ctx+0x10a0, current op's var id = vm_ctx+0x24 (u16). The two
// int-pushing handlers (varp + varc-int) are captured and each value is tagged
// with its type byte.

constexpr std::uint64_t kVmVarId = 0x24;     // current op's var id (u16), at vm_ctx+0x24

// ===== Registry-walk var reader. The flat varp hashmap can't see appearance varps
// (172/2017, behind a getStorage storage-object), but the engine resolves EVERY
// type-4 var (varp/varbit) through a per-scope registry whose
// holder.vtable.getStorage(holder, entry+8) yields the int storage.
//   (1) getStorage NEVER runs on the game thread: the push capture only SNAPSHOTS
//       scope candidates; resolving/reading happens on the publish thread @250ms;
//   (2) each var's storage pointer is resolved ONCE and cached, re-read per tick;
//   (3) the registry is re-ENUMERATED only every ~2s (the var set is near-static);
//   (4) the hot path is lock-free for the common case (same registry repeats);
//       only a registry-pointer CHANGE triggers a snapshot.
// Every raw deref is SEH-guarded; pointers are canonical-range gated so a stale
// chain bails silently instead of storming first-chance exceptions.

rtx::varc::Share* g_varcShare = nullptr;
typedef void* (*VarOp_t)(std::uint64_t, std::uint64_t);
VarOp_t g_origVarp = nullptr;     // type 4 handler (player var); captured to snapshot vm_ctx
VarOp_t g_origVarc = nullptr;     // type 5 handler (client var); a second scope source

struct VarContext {
    std::uint64_t registry, bucket_table; std::uint32_t bucket_count;
    std::uint64_t inst_table_0; std::uint32_t inst_count_0;
    std::uint64_t inst_table_1; std::uint32_t inst_count_1; std::uint8_t flag20;
    std::uint8_t  scope_type;   // 4 = varp/varbit handler, 5 = varc-int handler (which capture recorded it)
};
typedef void* (__fastcall *GetStorage_t)(void*, void*);

std::mutex g_scopeMu;
std::vector<VarContext> g_verifiedScopes;                  // distinct real registries discovered
std::vector<std::pair<VarContext, std::uint16_t>> g_candidates;  // hot-path snapshots awaiting verify
std::atomic<std::uint64_t> g_lastReg{0};         // last registry the hot path snapshotted (lock-free repeat skip)

// Throttle for ResolvePanelGroupsFromLive (see VarOpObserve). In combat the registry
// ping-pongs on every push, so an unthrottled resolve runs ~18k/sec on the game
// thread, each a getStorage vtable call plus a VirtualQuery syscall per panel group.
// Panels move slowly and the worker publishes at ~250ms, so ~4/sec is plenty.
std::atomic<std::uint64_t> g_lastPanelResolveMs{0};
constexpr std::uint64_t    kPanelResolveMinMs = 250;

std::mutex g_varMu;
std::unordered_map<std::uint32_t, std::int32_t>  g_varVal;        // (type<<16)|id -> value (published)
std::unordered_map<std::uint32_t, std::uint64_t> g_storageCache;  // (scope<<16)|id -> storage ptr (resolved once; scope-keyed so varp 9102 and varc 9102 coexist)
std::unordered_map<std::uint32_t, int>           g_walkAttempts;  // (scope<<16)|id -> failed getStorage attempts (give up after a few; v948 some vars fault)
static constexpr int kWalkMaxAttempts = 6;

static inline bool CanonPtr(std::uint64_t p) { return p >= 0x10000ull && p <= 0x00007FFFFFFFFFFFull; }

// Snapshot the resolved engine pointers from a live vm_ctx (POD + SEH; no C++ objects -> no C2712).
bool ReadVarContext(std::uint64_t vm_ctx, VarContext& out) {
    VarContext c{};
    __try {
        c.registry = *(std::uint64_t*)(vm_ctx + 0x10);
        if (!c.registry) return false;
        c.bucket_table = *(std::uint64_t*)(c.registry + 0x58);
        c.bucket_count = *(std::uint32_t*)(c.registry + 0x60);
        c.inst_table_0 = *(std::uint64_t*)(vm_ctx + 0xc010);
        c.inst_count_0 = *(std::uint32_t*)(vm_ctx + 0xc018);
        c.inst_table_1 = *(std::uint64_t*)(vm_ctx + 0xc1e0);
        c.inst_count_1 = *(std::uint32_t*)(vm_ctx + 0xc1e8);
        c.flag20       = *(std::uint8_t*)(vm_ctx + 0x20);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    out = c;
    return c.bucket_table != 0 && c.bucket_count != 0;
}

std::uint64_t LookupInstEntry(std::uint64_t inst_table, std::uint32_t inst_count, std::uint32_t inst_id) {
    if (!inst_table || !inst_count) return 0;
    __try {
        std::uint64_t e = *(std::uint64_t*)(inst_table + (inst_id % inst_count) * 8);
        for (int s = 0; e && s < 256; ++s) {
            if (*(std::uint32_t*)(e + 0) == inst_id) return e;
            e = *(std::uint64_t*)(e + 0x10);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

// Resolve the int-storage pointer for an already-located registry ENTRY within a scope: holder chain
// -> vtable getStorage(holder, entry+8). 0 on any fault/garbage. Split out from the context walk
// so enumeration can resolve a known entry without re-hashing. Background thread only.
std::uint64_t ResolveStorage(std::uint64_t entry, const VarContext& ctx) {
    __try {
        if (*(std::uint8_t*)(entry + 0x20) != 4) return 0;          // type 4 (varp/varbit) only
        std::uint64_t type_holder = *(std::uint64_t*)(entry + 0x10);
        if (!type_holder) return 0;
        std::uint64_t sub_holder = *(std::uint64_t*)(type_holder + 0x38);
        if (!sub_holder) return 0;
        std::uint32_t inst_id = *(std::uint32_t*)(sub_holder + 8);
        std::uint64_t inst_entry = 0;
        if (ctx.flag20 == 0) {
            inst_entry = LookupInstEntry(ctx.inst_table_0, ctx.inst_count_0, inst_id);
            if (!inst_entry) inst_entry = LookupInstEntry(ctx.inst_table_1, ctx.inst_count_1, inst_id);
        } else {
            inst_entry = LookupInstEntry(ctx.inst_table_1, ctx.inst_count_1, inst_id);
            if (!inst_entry) inst_entry = LookupInstEntry(ctx.inst_table_0, ctx.inst_count_0, inst_id);
        }
        if (!inst_entry) return 0;
        std::uint64_t holder = *(std::uint64_t*)(inst_entry + 8);
        if (!CanonPtr(holder)) return 0;
        std::uint64_t vtable = *(std::uint64_t*)holder;
        if (!CanonPtr(vtable)) return 0;
        GetStorage_t getStorage = (GetStorage_t)*(std::uint64_t*)(vtable + 8);
        if (!getStorage) return 0;
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery((LPCVOID)getStorage, &mbi, sizeof(mbi)) == 0) return 0;
        const DWORD execMask = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if (mbi.State != MEM_COMMIT || !(mbi.Protect & execMask) || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return 0;
        return (std::uint64_t)getStorage((void*)holder, (void*)(entry + 8));
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// Locate a var by id within a scope (bucket hash + chain) -> its entry ptr. POD + SEH.
std::uint64_t FindEntry(const VarContext& ctx, std::uint16_t var_id) {
    if (!ctx.bucket_table || !ctx.bucket_count || !CanonPtr(ctx.bucket_table)) return 0;
    __try {
        std::uint64_t e = *(std::uint64_t*)(ctx.bucket_table + (var_id % ctx.bucket_count) * 8);
        for (int s = 0; e && s < 256; ++s) {
            if (!CanonPtr(e)) return 0;
            if (*(std::uint16_t*)(e + 0) == var_id) return e;
            e = *(std::uint64_t*)(e + 0x28);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

// A snapshot scope can go stale on v948 (registry freed/reused). Re-validate by VirtualQuery-ing the
// page committed+readable, then confirming reg+0x58/+0x60 still match. Background thread only.
bool ScopeStillLive(const VarContext& sc) {
    if (!CanonPtr(sc.registry) || !sc.bucket_table || !sc.bucket_count) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery((LPCVOID)(sc.registry + 0x58), &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY;
    if (!(mbi.Protect & readable) || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    __try {
        return *(std::uint64_t*)(sc.registry + 0x58) == sc.bucket_table && *(std::uint32_t*)(sc.registry + 0x60) == sc.bucket_count;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Hot-path candidate push: cheap snapshot only (no getStorage). Locked, deduped by registry, capped.
__declspec(noinline) void PushCandidate(const VarContext& ctx, std::uint16_t var_id) {
    std::lock_guard<std::mutex> lk(g_scopeMu);
    for (auto& e : g_verifiedScopes) if (e.registry == ctx.registry) return;
    for (auto& c : g_candidates) if (c.first.registry == ctx.registry) return;
    if (g_candidates.size() < 64) g_candidates.emplace_back(ctx, var_id);
}

// Movable-panel position-var groups (X, Y, W, H; W/H = 0 when the panel has none).
// Each group is the panel interface's own var scope, resolved from a live push of
// that scope (the panel's scripts push from it on open). These scopes are
// script-local and freed right after the script runs, so the deferred worker-thread
// walk finds them dead -- hence resolution at the push_var capture point. Panels
// sharing a var group (all dialogues use 9102/9103/9104/9105) appear once; the
// launcher maps interface id -> group.
struct PanelGroup { std::uint16_t x, y, w, h; };
static const PanelGroup kPanelGroups[] = {
    { 9102, 9103, 9104, 9105 },  // dialogues (Choose/NPC/Player/Clue/Server/Input)
    { 9121, 9122, 0, 0 },           // bank pin / material storage / select item / teleports / tool / pause
    { 9596, 9597, 9598, 9599 },     // bank
    { 10128, 10129, 10130, 10131 }, // quest
    { 10394, 10395, 0, 0 },         // lodestone / smithing
    { 10071, 10072, 0, 0 },         // dxp timer
    { 8969, 8970, 8971, 8972 },     // all chat
    { 9292, 9293, 9294, 9295 },     // emote
    { 9387, 9388, 9389, 9390 },     // notes
    { 9368, 9369, 9370, 9371 },     // music
    { 8988, 8989, 8990, 8991 },     // inventory
    { 9083, 9084, 9085, 9086 },     // equipment
    { 9311, 9312, 9313, 9314 },     // skills
    { 10318, 10319, 10320, 10321 }, // achievement paths
    { 10166, 10167, 10168, 10169 }, // activity tracker
    { 8931, 8932, 8933, 8934 },     // minimap
    { 9539, 9540, 9541, 9542 },     // friends chat list
    { 9026, 9027, 9028, 9029 },     // private chat
    { 9406, 9407, 9408, 9409 },     // friends list
    { 9045, 9046, 9047, 9048 },     // friends chat
    { 9064, 9065, 9066, 9067 },     // clan chat
    { 9425, 9426, 9427, 9428 },     // clan chat list
    { 9349, 9350, 9351, 9352 },     // guest clan chat
    { 9653, 9654, 9655, 9656 },     // requests
    { 9748, 9749, 9750, 9751 },     // group chat
    { 9786, 9787, 9788, 9789 },     // group chat list
    { 9900, 9901, 9902, 9903 },     // loot panel
    { 9444, 9445, 9446, 9447 },     // buff bar
    { 10147, 10148, 10149, 10150 }, // debuff bar
};

// Opportunistic per-var resolution from a LIVE vm_ctx. getStorage must run while the
// script's instance tables (vm_ctx+0xc010/+0xc1e0) are still alive; the deferred
// worker-thread walk finds them freed. So resolve THIS var here, with live tables,
// and cache the permanent heap storage pointer. Once cached, the worker re-reads the
// int. Resolve once per id; give up after a few faults. Kept out of the __try
// function (std::lock_guard would be MSVC C2712).
__declspec(noinline) void ResolveCurrentVarLive(std::uint64_t vm_ctx, std::uint16_t var_id, std::uint8_t scope_type) {
    std::uint32_t key = ((std::uint32_t)scope_type << 16) | var_id;
    {
        std::lock_guard<std::mutex> lk(g_varMu);
        if (g_storageCache.find(key) != g_storageCache.end()) return;     // already resolved
        if (g_walkAttempts[key] >= kWalkMaxAttempts) return;              // gave up (faulting var)
    }
    VarContext ctx{};
    if (!ReadVarContext(vm_ctx, ctx)) return;
    ctx.scope_type = scope_type;
    std::uint64_t e = FindEntry(ctx, var_id);
    std::uint64_t storage = e ? ResolveStorage(e, ctx) : 0;               // LIVE inst tables -> works
    std::lock_guard<std::mutex> lk(g_varMu);
    if (storage) { g_storageCache[key] = storage; g_walkAttempts.erase(key); if (g_varcShare) g_varcShare->diag[3]++; }
    else         { g_walkAttempts[key]++; }
}

std::int32_t ReadIntSafe(std::uint64_t storage);   // defined below (worker-side int read)

// Resolve panel position groups from a live push's registry. A panel interface has
// its own var scope (registry @ vm_ctx+0x10), live while the panel is open, and its
// scripts push from it, so on a new registry every unresolved group is tried here.
// Disambiguation: the registry may be a decoy whose ids are unrelated vars all
// reading -1, so require a sensible on-screen X/Y, and for groups with W/H a
// sensible panel size too. A reject does not cache, so it never blocks the real
// scope. Resolved storage pointers are permanent heap; per-tick reads track moves.
__declspec(noinline) void ResolvePanelGroupsFromLive(std::uint64_t vm_ctx) {
    VarContext ctx{};
    if (!ReadVarContext(vm_ctx, ctx)) return;
    for (const auto& grp : kPanelGroups) {
        // No skip-if-already-cached: a panel reopened or re-laid-out at a new position gets
        // a FRESH var scope, so re-resolve every time the var appears in a live registry and
        // overwrite the cached pointer, else the origin stays frozen at the first scope seen.
        // The X/Y (+ W/H) validation below still rejects decoy scopes, so only a real panel
        // instance updates the cache.
        std::uint64_t ex = FindEntry(ctx, grp.x), ey = FindEntry(ctx, grp.y);
        if (!ex || !ey) continue;                                        // X/Y not in this registry
        std::uint64_t sx = ResolveStorage(ex, ctx), sy = ResolveStorage(ey, ctx);
        if (!sx || !sy) continue;
        int X = ReadIntSafe(sx), Y = ReadIntSafe(sy);
        if (X < 0 || X >= 6000 || Y < 0 || Y >= 6000) continue;          // decoy/garbage (e.g. -1)
        std::uint64_t sw = 0, sh = 0;
        if (grp.w || grp.h) {                                            // groups WITH W/H: require sensible size too
            std::uint64_t ew = grp.w ? FindEntry(ctx, grp.w) : 0, eh = grp.h ? FindEntry(ctx, grp.h) : 0;
            if ((grp.w && !ew) || (grp.h && !eh)) continue;
            if (ew) { std::uint64_t s = ResolveStorage(ew, ctx); int v = s ? ReadIntSafe(s) : 0; if (v <= 0 || v >= 5000) continue; sw = s; }
            if (eh) { std::uint64_t s = ResolveStorage(eh, ctx); int v = s ? ReadIntSafe(s) : 0; if (v <= 0 || v >= 5000) continue; sh = s; }
        }
        std::lock_guard<std::mutex> lk(g_varMu);
        g_storageCache[(4u << 16) | grp.x] = sx; g_storageCache[(4u << 16) | grp.y] = sy;
        if (sw) g_storageCache[(4u << 16) | grp.w] = sw;
        if (sh) g_storageCache[(4u << 16) | grp.h] = sh;
        if (g_varcShare) g_varcShare->diag[3]++;
    }
}

// push_var capture (game thread, fires thousands/sec): resolve storage from the LIVE
// vm_ctx (the only time the script's instance tables are alive). On a NEW registry,
// try the dialogue-panel resolver (ALWAYS-on, so the position is ready the instant a
// dialogue opens if its onload pushes any var). The full per-var capture (Vars
// watcher) stays gated.
inline void* VarOpObserve(VarOp_t orig, std::uint64_t a, std::uint64_t vm_ctx, std::uint8_t scope_type) {
    if (g_varcShare) {
        std::uint64_t reg = 0; std::uint16_t var_id = 0; bool ok = false;
        __try { var_id = *(std::uint16_t*)(vm_ctx + kVmVarId); reg = *(std::uint64_t*)(vm_ctx + 0x10); ok = true; }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        if (ok && reg && reg != g_lastReg.load(std::memory_order_relaxed)) {
            g_lastReg.store(reg, std::memory_order_relaxed);
            // Throttled: combat's registry churn makes this branch fire ~18k/sec, each a
            // getStorage vtable call plus a VirtualQuery syscall per panel group, on the game
            // thread. One resolve per ~250ms is enough. GetTickCount64 is a cheap
            // KUSER_SHARED_DATA read, no syscall.
            std::uint64_t now = GetTickCount64();
            if (now - g_lastPanelResolveMs.load(std::memory_order_relaxed) >= kPanelResolveMinMs) {
                g_lastPanelResolveMs.store(now, std::memory_order_relaxed);
                ResolvePanelGroupsFromLive(vm_ctx);      // catches each panel scope on open (no drag)
            }
        }
        if (ok && g_varcShare->enable) ResolveCurrentVarLive(vm_ctx, var_id, scope_type); // full capture (id 0 is a real var)
    }
    return orig(a, vm_ctx);
}
void* Detour_Varp(std::uint64_t a, std::uint64_t vm_ctx) { if (g_varcShare) g_varcShare->diag[0]++; return VarOpObserve(g_origVarp, a, vm_ctx, 4); }
void* Detour_Varc(std::uint64_t a, std::uint64_t vm_ctx) { if (g_varcShare) g_varcShare->diag[1]++; return VarOpObserve(g_origVarc, a, vm_ctx, 5); }

// cc_if_setdraggable handler: CS2 op that BINDS a panel to its draggable anchor +
// position vars. Fires when a dialogue/panel OPENS (unlike push_var, which fires for
// the position var only on a DRAG). Its 4th arg (r9) is the vm_ctx whose script scope
// holds the panel's position vars, so resolving the whitelist from it catches
// 9102/9103/9104/9105 on open, with live instance tables.
typedef void* (*CcOp_t)(std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t);
CcOp_t g_origCcDrag = nullptr;

void* Detour_CcIfSetDraggable(std::uint64_t p1, std::uint64_t p2, std::uint64_t p3, std::uint64_t p4) {
    void* r = g_origCcDrag(p1, p2, p3, p4);
    // Diag only: cc's scope does NOT contain the panel vars (binding args, not scope
    // vars). The panel position resolves from the push_var path's registry instead.
    if (g_varcShare) g_varcShare->diag[2]++;              // cc_if_setdraggable fired (count)
    return r;
}

// Find a var op handler by its var-lookup BODY (mov r9,[rdx+0x10]; mov rbx,rdx;
// movzx r8d,[rdx+0x24]; the registry bucket lookup) -- unique per handler in .text and
// survives prologue churn. The body sits a few bytes PAST the prologue, and the true
// function entry is required (the epilogue `add rsp; pop rbx; ret` must run against a
// frame that was set up). Resolve the body's containing function via the exception
// (.pdata) table; BeginAddress is the real callable entry. The varp (type 4) and
// varc-int (type 5) handlers share this body, differing only in the final bucket-load
// reg (...49 8B 0C C2 vs ...49 8B 04 C2). Re-derive bytes per game build.
std::uint64_t FindVarOp(const unsigned char* body, std::size_t blen) {
    auto dos = (const IMAGE_DOS_HEADER*)g_base;
    auto nt  = (const IMAGE_NT_HEADERS*)(g_base + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    std::uint64_t tb = 0, ts = 0;
    for (int s = 0; s < nt->FileHeader.NumberOfSections; ++s)
        if (sec[s].Characteristics & IMAGE_SCN_MEM_EXECUTE) { tb = g_base + sec[s].VirtualAddress; ts = sec[s].Misc.VirtualSize; break; }
    if (!tb || !ts) return 0;
    __try {
        const unsigned char* b = (const unsigned char*)tb;
        std::uint64_t body_va = 0;
        for (std::uint64_t i = 0; i + blen < ts; ++i) {
            bool ok = true;
            for (std::uint64_t j = 0; j < blen; ++j) if (b[i + j] != body[j]) { ok = false; break; }
            if (ok) { body_va = tb + i; break; }
        }
        if (!body_va) return 0;
        // Map the body back to its function's true entry via the exception unwind table.
        std::uint32_t body_rva = (std::uint32_t)(body_va - g_base);
        auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        if (dir.VirtualAddress && dir.Size) {
            auto rf = (const RUNTIME_FUNCTION*)(g_base + dir.VirtualAddress);
            std::uint32_t n = dir.Size / sizeof(RUNTIME_FUNCTION);
            for (std::uint32_t i = 0; i < n; ++i)
                if (rf[i].BeginAddress <= body_rva && body_rva < rf[i].EndAddress)
                    return g_base + rf[i].BeginAddress;
        }
        return body_va;   // no .pdata match (shouldn't happen): fall back to the body
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

// As FindVarOp, but mask[j]==0 means "wildcard this byte" (e.g. a relative CALL/JMP operand
// that differs per build), so a pattern whose only varying bytes are an embedded rel32 stays
// usable -- which is what disambiguates the object-submit fn from look-alikes.
std::uint64_t FindVarOpWild(const unsigned char* body, const unsigned char* mask, std::size_t blen) {
    auto dos = (const IMAGE_DOS_HEADER*)g_base;
    auto nt  = (const IMAGE_NT_HEADERS*)(g_base + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    std::uint64_t tb = 0, ts = 0;
    for (int s = 0; s < nt->FileHeader.NumberOfSections; ++s)
        if (sec[s].Characteristics & IMAGE_SCN_MEM_EXECUTE) { tb = g_base + sec[s].VirtualAddress; ts = sec[s].Misc.VirtualSize; break; }
    if (!tb || !ts) return 0;
    __try {
        const unsigned char* b = (const unsigned char*)tb;
        std::uint64_t body_va = 0;
        for (std::uint64_t i = 0; i + blen < ts; ++i) {
            bool ok = true;
            for (std::uint64_t j = 0; j < blen; ++j) if (mask[j] && b[i + j] != body[j]) { ok = false; break; }
            if (ok) { body_va = tb + i; break; }
        }
        if (!body_va) return 0;
        std::uint32_t body_rva = (std::uint32_t)(body_va - g_base);
        auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        if (dir.VirtualAddress && dir.Size) {
            auto rf = (const RUNTIME_FUNCTION*)(g_base + dir.VirtualAddress);
            std::uint32_t n = dir.Size / sizeof(RUNTIME_FUNCTION);
            for (std::uint32_t i = 0; i < n; ++i)
                if (rf[i].BeginAddress <= body_rva && body_rva < rf[i].EndAddress)
                    return g_base + rf[i].BeginAddress;
        }
        return body_va;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

rtx::varc::Share* MapVarcShare() {
    wchar_t name[64];
    rtx::varc::MakeSectionName(GetCurrentProcessId(), name);
    HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        (DWORD)(sizeof(rtx::varc::Share) >> 32), (DWORD)(sizeof(rtx::varc::Share) & 0xFFFFFFFF), name);
    if (!h) return nullptr;
    return (rtx::varc::Share*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(rtx::varc::Share));
}

rtx::varc::Entry g_varcbuf[rtx::varc::kMaxVars];

struct EnumEntry { std::uint64_t entry; std::uint16_t id; std::uint8_t type; };

// Enumerate one scope's bucket table -> entries. v948-staleness-guarded (probe the LAST bucket
// first -- a truncated/stale table faults there; bail after a run of faults).
// Cheap: pointer walks only, no getStorage. Background thread.
void EnumerateScope(const VarContext& sc, std::vector<EnumEntry>& out) {
    std::uint64_t bt = sc.bucket_table; std::uint32_t bc = sc.bucket_count;
    if (!bt || !bc || bc > 1000000 || !CanonPtr(bt)) return;
    { volatile std::uint64_t probe = 0; (void)probe; bool f = false;
      __try { probe = *(std::uint64_t*)(bt + (std::uint64_t)(bc - 1) * 8); }
      __except (EXCEPTION_EXECUTE_HANDLER) { f = true; }
      if (f) return; }
    int consec = 0;
    for (std::uint32_t b = 0; b < bc; ++b) {
        std::uint64_t entry = 0; bool f = false;
        __try { entry = *(std::uint64_t*)(bt + (std::uint64_t)b * 8); }
        __except (EXCEPTION_EXECUTE_HANDLER) { f = true; }
        if (f) { if (++consec >= 8) return; continue; }
        consec = 0;
        if (out.size() > 200000) return;
        for (int chain = 0; entry && chain < 256; ++chain) {
            if (!CanonPtr(entry)) break;
            EnumEntry e{}; e.entry = entry;
            __try { e.id = *(std::uint16_t*)(entry + 0); e.type = *(std::uint8_t*)(entry + 0x20); }
            __except (EXCEPTION_EXECUTE_HANDLER) { break; }
            out.push_back(e);
            __try { entry = *(std::uint64_t*)(entry + 0x28); }
            __except (EXCEPTION_EXECUTE_HANDLER) { break; }
        }
    }
}

std::int32_t ReadIntSafe(std::uint64_t storage) {
    __try { return *(std::int32_t*)storage; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// Resolve+cache storage for newly-seen type-4 vars. Worker thread, ~every 2s (the var SET is
// near-static): enumerate live scopes, getStorage each new var. Storage pointers are permanent
// heap (stable for the engine's life), so per-tick reads never re-walk. getStorage runs HERE only.
__declspec(noinline) void DiscoverStorage() {
    std::vector<VarContext> scopes;
    { std::lock_guard<std::mutex> lk(g_scopeMu); scopes = g_verifiedScopes; }
    int live = 0;
    std::vector<EnumEntry> ents;
    for (auto& sc : scopes) {
        if (!ScopeStillLive(sc)) continue;
        ++live;
        ents.clear(); EnumerateScope(sc, ents);
        // Key by (scope<<16)|id: a varp scope (scope_type 4) and the varc-int scope
        // (scope_type 5) both hold type-4 int entries, often with the SAME numeric id
        // (e.g. 9102 is both a varp and the dialogue-panel-X varc). Scope-keying keeps
        // them distinct so the launcher can ask for the varc specifically.
        const std::uint32_t scopeKey = (std::uint32_t)sc.scope_type << 16;
        std::lock_guard<std::mutex> lk(g_varMu);
        for (auto& e : ents) {
            if (e.type != 4) continue;
            std::uint32_t key = scopeKey | e.id;
            if (g_storageCache.find(key) != g_storageCache.end()) continue;
            std::uint64_t storage = ResolveStorage(e.entry, sc);
            if (storage) g_storageCache[key] = storage;
        }
    }
    if (live == 0 && !scopes.empty()) {            // all scopes dead (relog / world hop) -> re-discover
        { std::lock_guard<std::mutex> lk(g_scopeMu); g_verifiedScopes.clear(); }
        { std::lock_guard<std::mutex> lk(g_varMu);   g_storageCache.clear(); }
        g_lastReg.store(0, std::memory_order_relaxed);   // force the hot path to re-snapshot new registries
    }
}

// Worker tick (@250ms while the Vars tab is enabled): verify candidate scopes snapshotted by the hot
// path (getStorage off the game thread), occasionally re-discover the var set, then read all cached
// storage and publish under the seqlock. Light per tick -- only cached int reads.
int g_pubTick = 0;
__declspec(noinline) void VerifyCandidates() {
    std::vector<std::pair<VarContext, std::uint16_t>> cands;
    { std::lock_guard<std::mutex> lk(g_scopeMu); cands.swap(g_candidates); }
    for (auto& c : cands) {
        if (!ScopeStillLive(c.first)) continue;
        std::uint64_t entry = FindEntry(c.first, c.second);
        if (!entry) continue;
        if (!ResolveStorage(entry, c.first)) continue;                 // getStorage -- the offset-fragile step
        std::lock_guard<std::mutex> lk(g_scopeMu);
        bool dup = false;
        for (auto& e : g_verifiedScopes) if (e.registry == c.first.registry && e.bucket_table == c.first.bucket_table) { dup = true; break; }
        if (!dup && g_verifiedScopes.size() < 64) g_verifiedScopes.push_back(c.first);
    }
}
void PublishVarcs(rtx::varc::Share* vsh) {
    // Resolved storage pointers are permanent heap for the session, so a resolved panel
    // position stays readable until the engine recycles its var heap (relog / world hop).
    // Every ~2s, validate each cached pointer and drop only the individually-dead ones
    // (a freed pointer is no longer committed). Per-entry eviction, never a wholesale
    // clear: the cache is flooded with transient script-local vars, and clearing on a
    // sample would take live panel positions with it. It also self-heals on relog, since
    // all entries die individually and re-resolve in the new scopes. VirtualQuery is
    // ~1us and the map is small.
    if ((g_pubTick++ % 8) == 0) {
        std::lock_guard<std::mutex> lk(g_varMu);
        for (auto it = g_storageCache.begin(); it != g_storageCache.end(); ) {
            MEMORY_BASIC_INFORMATION mbi{};
            bool live = VirtualQuery((LPCVOID)it->second, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT &&
                        !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS));
            if (live) { ++it; }
            else { g_walkAttempts.erase(it->first); it = g_storageCache.erase(it); }
        }
    }
    std::uint32_t c = 0;
    { std::lock_guard<std::mutex> lk(g_varMu);
      for (auto& kv : g_storageCache) {
          if (c >= (std::uint32_t)rtx::varc::kMaxVars) break;
          g_varcbuf[c].id    = (std::uint16_t)(kv.first & 0xffff);
          g_varcbuf[c].scope = (std::uint8_t)((kv.first >> 16) & 0xff);  // 4 = varp/varbit, 5 = varc-int
          g_varcbuf[c]._pad  = 0;
          g_varcbuf[c].value = ReadIntSafe(kv.second);
          ++c;
      }
    }
    vsh->seq++;                                   // odd: mid-update
    MemoryBarrier();                              // data must not move above the odd seq
    std::memcpy(vsh->entries, g_varcbuf, (std::size_t)c * sizeof(rtx::varc::Entry));
    vsh->count = c;
    vsh->strCount = 0;
    MemoryBarrier();                              // data must land before the even seq
    vsh->seq++;                                   // even: complete
}

// ---- shared section --------------------------------------------------------
Share* MapShare() {
    wchar_t name[64];
    rtx::scene::MakeSectionName(GetCurrentProcessId(), name);
    HANDLE hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                     (DWORD)(sizeof(Share) >> 32), (DWORD)(sizeof(Share) & 0xFFFFFFFF),
                                     name);
    if (!hMap) return nullptr;
    auto* p = reinterpret_cast<Share*>(MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Share)));
    return p;
}

// Staging buffer so the slow per-object game-memory reads happen OUTSIDE the seqlock.
// Holding the seqlock only for the final memcpy keeps the launcher's torn-read window
// tiny (microseconds) instead of milliseconds -> no full-frame flicker on the reader.
Object g_pubbuf[rtx::scene::kMaxObjects];

// Copy of the despawned-entity set; defined beside the capture points further down
// (same forward-declaration pattern as FindContainersFromTracked).
std::unordered_set<std::uint64_t> DeadSnapshot();

void Publish(Share* sh, bool wantDiag) {
    std::uint32_t c = 0;
    bool got0 = false, got12 = false;            // dumped a type-0 / type-12 sub yet
    // One snapshot per tick, then lock-free lookups: taking the capture-point mutex per
    // entity would contend with the render thread on every spawn/despawn.
    const std::unordered_set<std::uint64_t> dead = DeadSnapshot();
    for (int mi = 0; mi < g_mgrCount && c < rtx::scene::kMaxObjects; ++mi) {
        std::uint64_t mgr = g_mgrs[mi];
        std::uint64_t vb = R64(mgr + kVecBegin), ve = R64(mgr + kVecEnd);
        std::uint64_t n = (IsHeap(vb) && ve > vb) ? (ve - vb) / 8 : 0;
        if (n > 30000) n = 30000;
        for (std::uint64_t i = 0; i < n && c < rtx::scene::kMaxObjects; ++i) {
            std::uint64_t ep = R64(vb + i * 8);
            // The del capture saw this entity despawn and nothing has re-spawned into
            // its slot: it is not in the scene, however intact its memory still reads.
            // A despawned loc passes every structural check below and can even keep a
            // non-zero model block.
            if (dead.count(ep)) continue;
            int t, tx, ty;
            if (!ValidSceneryEntity(ep, t, tx, ty)) continue;
            std::uint64_t sub = R64(ep + kSecPtr);
            Object& o = g_pubbuf[c++];            // build into the staging buffer (no lock held)
            o.config_id = ReadConfig(sub);
            o.x = tx; o.y = ty;
            o.plane = (std::int16_t)R32(sub + kFloor);
            o.kind = (std::int16_t)t;
            ReadBox(ep, o);                          // live model world AABB (true size + height)
            // PHANTOM-LOC FILTER: the AABB is loc-def-derived, so it is present and valid
            // even for a loc that is never actually drawn (phantom Rubble on empty floor).
            // The sec's live-model block (kModelLo/Mid/Hi) is the true "is rendered" signal:
            // empty => report the AABB as absent so the launcher's existing `vis`
            // (bmax.x <= bmin.x) reads not-drawn and the panel's shown() gate drops it.
            if ((R32(sub + kModelLo) | R32(sub + kModelMid) | R32(sub + kModelHi)) == 0) {
                o.bmin[0] = o.bmin[1] = o.bmin[2] = 0.f;
                o.bmax[0] = o.bmax[1] = o.bmax[2] = 0.f;   // bmax.x == bmin.x -> absent
            }
            if (wantDiag) {                       // dump one sub per type for offset pinning
                int slot = (t == 0 && !got0) ? 0 : (t == 12 && !got12) ? 1 : -1;
                if (slot == 0) got0 = true; else if (slot == 1) got12 = true;
                if (slot >= 0) {
                    for (int b = 0; b < 0x130; ++b) sh->diag[slot * 0x130 + b] = R8(sub + b);
                    std::uint32_t want = (std::uint32_t)((slot + 1) * 0x130);
                    if (want > sh->diag_len) sh->diag_len = want;
                }
            }
        }
    }
    // Commit atomically: short odd-seq window = just the copy of `c` objects.
    sh->seq++;                                    // odd: mid-update
    MemoryBarrier();                              // data must not move above the odd seq
    std::memcpy(sh->objects, g_pubbuf, (std::size_t)c * sizeof(Object));
    sh->count = c;
    MemoryBarrier();                              // data must land before the even seq
    sh->seq++;                                    // even: complete
}

// ===== Render toggles (launcher-driven): hide NPCs / hide other players / blank the
// whole scene. All gated on the launcher-written render share; default off = the two
// entity capture points just call through, scene-blank untouched. The capture points
// skip a spawn/visibility call so the engine never draws that entity; scene-blank
// flips ONE conditional-jump byte in the render thread (guarded: only a real Jcc is
// ever touched). FindVarOp() maps a distinctive in-function byte body to the true
// function entry via the .pdata unwind table. ==========================================
rtx::render::Share* g_renderShare = nullptr;
std::atomic<std::uint64_t> g_localPlayerSub{ 0 };

typedef void* (*PlDis_t)(std::uint64_t a1, void* a2, void* a3, void* a4, void* a5, void* a6);
typedef void  (*NpcDis_t)(std::uint64_t a1, std::uint64_t a2, std::uint64_t a3, std::uint64_t a4, std::uint64_t a5, std::uint64_t a6);
PlDis_t  g_origPlDis  = nullptr;
NpcDis_t g_origNpcDis = nullptr;

// Hide NPCs: the NPC display method -- the per-actor draw wrapper, sibling of the
// player one Detour_PlDis covers. Both are vtable display methods calling one shared
// display worker; this is the NPC actor class's, invoked once per NPC per frame.
// Skipping it for a type-1 actor means the engine doesn't draw that NPC this frame, so
// NPCs already in the scene AND new ones vanish immediately. Purely visual: the NPC
// stays in the world data (no entity-list mutation), so the Scene tab / other reads
// are unaffected, no game-state or threading risk.
void Detour_NpcDis(std::uint64_t a1, std::uint64_t a2, std::uint64_t a3, std::uint64_t a4, std::uint64_t a5, std::uint64_t a6) {
    __try {
        // a1 = actor object-data (sub); type at +0x10, 1 = NPC.
        if (g_renderShare && g_renderShare->hideNpcs && IsHeap(a1) && R8(a1 + kType) == 1) return;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_origNpcDis(a1, a2, a3, a4, a5, a6);
}
void* Detour_PlDis(std::uint64_t a1, void* a2, void* a3, void* a4, void* a5, void* a6) {
    __try {
        // a1 = player object-data (sub); type at +0x10, 2 = player. Keep the LOCAL player.
        if (g_renderShare && g_renderShare->hidePlayers && IsHeap(a1)) {
            std::uint64_t self = g_localPlayerSub.load(std::memory_order_relaxed);
            if (R8(a1 + kType) == 2 && a1 != self) return nullptr;     // other player -> not rendered
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return g_origPlDis(a1, a2, a3, a4, a5, a6);
}

// scene-blank: one conditional-jump byte in the render thread, toggled from the worker.
std::uint64_t g_sceneBlankAddr = 0;     // address of the Jcc byte (0 = unavailable)
std::uint8_t  g_sceneBlankOrig = 0;     // original Jcc opcode
bool          g_sceneBlankPatched = false;
void SceneBlankSet(bool on) {
    if (!g_sceneBlankAddr) return;
    DWORD oldp = 0;
    if (!VirtualProtect((void*)g_sceneBlankAddr, 1, PAGE_EXECUTE_READWRITE, &oldp)) return;
    *(volatile std::uint8_t*)g_sceneBlankAddr = on ? (std::uint8_t)0x74 : g_sceneBlankOrig;
    FlushInstructionCache(GetCurrentProcess(), (void*)g_sceneBlankAddr, 1);
    VirtualProtect((void*)g_sceneBlankAddr, 1, oldp, &oldp);
    g_sceneBlankPatched = on;
}

rtx::render::Share* MapRenderShare() {
    wchar_t name[64]; rtx::render::MakeSectionName(GetCurrentProcessId(), name);
    HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                  sizeof(rtx::render::Share), name);
    if (!h) return nullptr;
    return (rtx::render::Share*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(rtx::render::Share));
}

void ResolveRenderHooks() {
    g_renderShare = MapRenderShare();
    if (g_renderShare) {
        g_renderShare->magic = rtx::render::kMagic; g_renderShare->version = rtx::render::kVersion;
        g_renderShare->pid = GetCurrentProcessId();
        g_renderShare->hideNpcs = 0; g_renderShare->hidePlayers = 0; g_renderShare->hideAll = 0;
        g_renderShare->keepFocused = 0;   // launcher re-pushes after load if the toggle is on
        g_renderShare->installed = 0;
    }
    // distinctive exact bodies (no wildcards):
    //   npc-display: mov rax,[rcx]; mov rdi,r9; mov rsi,r8; mov rbp,rdx; mov rbx,rcx; call [rax+0x110]
    //   player-vis : mov rax,[rcx+0x1078]; mov rbp,r9
    //   render-thr : mov rax,[rcx+8]; mov r15,rcx; mov r14,[rip+..]
    static const unsigned char kNpcDisBody[] = {0x48,0x8B,0x01,0x49,0x8B,0xF9,0x49,0x8B,0xF0,0x48,0x8B,0xEA,0x48,0x8B,0xD9,0xFF,0x90,0x10,0x01,0x00,0x00};
    static const unsigned char kPlDisBody[]  = {0x48,0x8B,0x81,0x78,0x10,0x00,0x00,0x49,0x8B,0xE9};
    static const unsigned char kRenderBody[] = {0x48,0x8B,0x41,0x08,0x4C,0x8B,0xF9,0x4C,0x8B,0x35};
    std::uint64_t pNpc = FindVarOp(kNpcDisBody, sizeof(kNpcDisBody));
    std::uint64_t pDis = FindVarOp(kPlDisBody,  sizeof(kPlDisBody));
    if (pNpc || pDis) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        if (pNpc) { g_origNpcDis = (NpcDis_t)pNpc; DetourAttach(&(PVOID&)g_origNpcDis, (PVOID)Detour_NpcDis); }
        if (pDis) { g_origPlDis  = (PlDis_t)pDis;  DetourAttach(&(PVOID&)g_origPlDis,  (PVOID)Detour_PlDis); }
        if (DetourTransactionCommit() == NO_ERROR && g_renderShare) {
            if (pNpc) g_renderShare->installed |= 1;   // bit0: hide-NPCs (per-frame NPC display)
            if (pDis) g_renderShare->installed |= 2;   // bit1: hide other players
        }
    }
    std::uint64_t pRender = FindVarOp(kRenderBody, sizeof(kRenderBody));
    if (pRender) {
        std::uint64_t spot = pRender + 0x266;
        std::uint8_t op = R8(spot);
        if (op == 0x74 || op == 0x75) {     // guard: only ever touch a real conditional jump
            g_sceneBlankAddr = spot; g_sceneBlankOrig = op;
            if (g_renderShare) g_renderShare->installed |= 4;
        }
    }
}

// ===== Render-pass highlight observer: transient type-4 "highlight" graphics (the
// clue-scan proximity ring, gfx at sub+0x74) are submitted to the renderer each frame
// but NOT kept in the persistent worldview vector, so the launcher cannot read them
// externally. Observe the object-submit path, record each frame's type-4 highlights
// into a fixed ring; the worker publishes the recent, deduped set. Gated on the
// launcher's enable flag, so render-thread cost is ~zero when nothing is reading.
// Pure POD, allocation-free, lock-light, SEH-wrapped. ===================================
rtx::special::Share* g_specialShare = nullptr;
constexpr int kHiRingCap = 128;
rtx::special::Highlight g_hiRing[kHiRingCap] = {};
std::atomic<std::uint32_t> g_hiIdx{ 0 };
std::atomic<bool> g_specialOn{ false };   // worker decays this from the launcher's enable stamp
// The submit fn runs with DIFFERENT a1 workers across render passes; the ring lives in
// only one. Persistent set of every entity ptr seen at the spawn capture point
// (single-writer render thread; read by the worker thread). The scan ring REUSES a
// slot tracked earlier as some OTHER entity, so it never arrives here as a type-4 a2;
// keep every ptr and re-read its type LIVE on the worker thread. Spawn capture records
// EVERY a2, del capture removes it on despawn, so this set only ever holds LIVE
// entities. Mutex-guarded.
std::mutex g_renderMu;
std::unordered_set<std::uint64_t> g_renderSet;
// Entities the del capture saw despawn and that have not re-spawned since. Publish
// consults this: a despawned loc can stay bit-perfect in a captured container, pool
// slot intact and model block still non-zero, so neither the container walk nor the
// phantom-loc render-block gate can tell it is gone. The del capture is the
// authoritative signal; the spawn capture revives a re-used slot.
std::unordered_set<std::uint64_t> g_deadSet;
constexpr std::size_t kDeadSetCap = 100000;   // runaway backstop; clearing only re-admits stale locs
// Scene worker root: the FIRST a1 the spawn capture sees (via compare_exchange).
std::atomic<std::uint64_t> g_SceneWorkerRoot{ 0 };
std::uint64_t g_hookInstallMs = 0;   // GetTickCount64 when the spawn capture attached (proves early presence)

// NB the capture points are __try functions, so everything with a destructor (the
// lock_guard) must live in helpers like these, never inline in the detour body (C2712).
static inline void RenderInsert(std::uint64_t p) { std::lock_guard<std::mutex> lk(g_renderMu); g_renderSet.insert(p); }

// Spawn: the slot is a live entity again -- track it and revive it for Publish. ONLY
// the spawn capture may revive (WalkVecTrack must not: soft-removed locs can linger in
// the vectors it walks).
static inline void SpawnMark(std::uint64_t p) {
    std::lock_guard<std::mutex> lk(g_renderMu);
    g_renderSet.insert(p);
    g_deadSet.erase(p);
}
// Despawn: stop tracking and remember the slot as dead until something re-spawns there.
static inline void DeadMark(std::uint64_t p) {
    std::lock_guard<std::mutex> lk(g_renderMu);
    g_renderSet.erase(p);
    if (g_deadSet.size() >= kDeadSetCap) g_deadSet.clear();
    g_deadSet.insert(p);
}

std::unordered_set<std::uint64_t> DeadSnapshot() {   // forward-declared above Publish
    std::lock_guard<std::mutex> lk(g_renderMu);
    return g_deadSet;
}

// Containers from the spawn hook's tracked entities -- the cheap way to discover them.
// Same acceptance chain as the heap scan (scenery sub, entity<->sub back-pointer, scored
// container), just seeded from pointers already held instead of from a memory sweep.
//
// NOT a full replacement: the hook only knows entities it watched SPAWN, so anything
// already in the scene when it attached is invisible here. That is what the periodic
// deep sweep is for -- this makes the common case instant, the sweep keeps it complete.
int FindContainersFromTracked() {
    std::vector<std::uint64_t> snap;
    { std::lock_guard<std::mutex> lk(g_renderMu); snap.assign(g_renderSet.begin(), g_renderSet.end()); }
    int added = 0;
    for (std::uint64_t ent : snap) {
        if (!IsHeap(ent)) continue;
        std::uint64_t sub = R64(ent + kSecPtr);
        if (!IsHeap(sub) || !IsScenery(R8(sub + kType))) continue;
        if (R64(sub + 0x8) != ent) continue;              // the back-pointer filter
        std::uint64_t mgr = R64(ent + kEntMgr);
        int total = 0;
        if (IsHeap(mgr) && ScoreContainer(mgr, total) >= 1) {
            const int before = g_mgrCount;
            AddMgr(mgr);
            if (g_mgrCount > before) ++added;
        }
    }
    return added;
}

typedef void* (*ObjSubmit_t)(std::uint64_t a1, std::uint64_t a2);   // a1 = scene worker, a2 = entity
ObjSubmit_t g_origObjSubmit = nullptr;
typedef void* (*ObjDel_t)(std::uint64_t a1, std::uint64_t a2);
ObjDel_t g_origObjDel = nullptr;

// Object-submit capture (rs2client+0x50A8B0): capture the first a1 and record EVERY
// a2 (even with a null sub -- the ring arrives here once, at allocation).
void* Detour_ObjSubmit(std::uint64_t a1, std::uint64_t a2) {
    __try {
        if (a1 > 0xfffff && a1 < g_base) { std::uint64_t e = 0; g_SceneWorkerRoot.compare_exchange_strong(e, a1); }
        if (a2 > 0xfffff && a2 < g_base) SpawnMark(a2);
        if (g_specialOn.load(std::memory_order_relaxed) && g_specialShare) g_specialShare->diag[0]++;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return g_origObjSubmit(a1, a2);
}

// ===== Per-type DISPLAY hooks ===========================================================
// The spawn capture above is indirect: it fires while the scene NODE exists but BEFORE the
// Entity is allocated and linked (ctor order is alloc-node -> [capture] -> alloc entity ->
// mov [node+0x1A0],entity), so an entity can reach it with no sub attached -- which is why
// the scan ring never arrived there as a type-4 and why a worker thread has to re-read every
// tracked pointer's type. The display methods have no such problem: by the time one runs,
// the object is fully built, and each is reached ONLY through its own type's vtable, so
// anything arriving IS that type. No type test, no re-read, no race.
//
//   type 4  -> vtable 0xB73A50 slot 7 -> 0x326690   (graphic highlights)
//   type 13 -> vtable 0xB5E878 slot 7 -> 0x1ABBB0   (world markers)
//
// `rcx` is the SUB (the object carrying the vtable): plane at +0x40 and uid at +0x88 are
// both read by the real function, and sub+0x8 is the back-pointer to the scene node, which
// is where the float fine position lives.
//
// COST. These run on the render thread, per matching entity per frame, so the body is
// gated on the launcher's enable flag first and is allocation-free and lock-free after it:
// the ring index is atomic and the slot is written in place. Both types are rare on screen
// (a handful at most), so the gated path is a load and a branch in the common case.
// ===== Per-type DISPLAY hooks ===========================================================
// The spawn capture is indirect: it fires while the scene NODE exists but BEFORE the Entity
// is built and linked, so entities reach it with no sub attached -- which is why the scan
// ring never arrived there as a type-4 and why a worker thread had to re-read every tracked
// pointer's type. A display method has no such problem: it runs on a finished object and is
// reached only through its own type's vtable, so anything arriving IS that type.
//
//   type 4  -> vtable 0xB73A50 slot 7 -> 0x326690   (graphic highlights)
//   type 13 -> vtable 0xB5E878 slot 7 -> 0x1ABBB0   (world markers)
//
// THE ARGUMENT COUNT IS LOAD-BEARING. A first version of this declared four parameters and
// crashed the game (0xC0000005, reproducible around Lumbridge). Both functions read stack
// arguments beyond the four register slots:
//     type 13: mov rdi,[rsp+0x110] (arg5) @0x1ABBE4 ; mov rax,[rsp+0x118] (arg6) @0x1ABCC3
//     type 4 : mov rsi,[rsp+0x70]  (arg5) @0x3266FB ; mov r15,[rsp+0x78]  (arg6) @0x32675F
// Forwarding with only four built a fresh 4-arg frame, so args 5/6 were whatever happened to
// be on our stack; the callee then dereferenced that garbage (test byte [rdi+0x39c]) and
// faulted. We declare EIGHT and pass all eight through: passing more than the callee reads is
// harmless under caller-cleanup, passing fewer is fatal, and "at least 6" is a lower bound
// (a slot being read proves it exists; a slot not being read proves nothing).
//
// xmm is NOT a hazard here, checked rather than assumed: type-4 reads r9 (`mov rbx,r9`), so
// its arg4 is an integer, and type-13 only WRITES xmm1 from a constant. The xmm0 store at
// 0x1ABC43 is the return value of the call at 0x1ABC32, not an incoming float.
//
// COST: render thread, per matching entity per frame. Gated on the launcher's enable flag
// first, then allocation-free and lock-free -- the ring index is atomic and the slot is
// written in place. Both types are rare on screen.
typedef void* (*Display_t)(std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t,
                           std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t);
Display_t g_origT4Display = nullptr;
Display_t g_origT13Display = nullptr;

// Record one fully-built entity into the highlight ring. `type` is known from WHICH hook
// called, not sniffed from memory.
static inline void RecordDisplay(std::uint64_t sub, int type) {
    if (sub <= 0xfffff || sub >= g_base) return;
    auto& s = g_hiRing[g_hiIdx.fetch_add(1, std::memory_order_relaxed) % kHiRingCap];
    s.gfx   = (type == 4) ? R32(sub + 0x74) : -1;    // +0x74 is a gfx id for type 4 ONLY
    s.uid   = R32(sub + 0x88);
    s.plane = (std::int16_t)R32(sub + kFloor);       // dword: the real fn compares it as one
    s.type  = (std::int16_t)type;
    std::uint64_t ent = R64(sub + 0x8);              // sub -> node back-pointer
    float nx = 0.f, ny = 0.f;
    if (ent > 0xfffff && ent < g_base) { nx = RF(ent + kEntPosX); ny = RF(ent + kEntPosY); }
    s.x = (nx > 0.f && nx < 1e9f) ? (std::int32_t)(nx / 512.f) : 0;
    s.y = (ny > 0.f && ny < 1e9f) ? (std::int32_t)(ny / 512.f) : 0;
    s.kind = rtx::special::kKindUnknown;
    if (type == 13) {
        // Same data-shape test the worldview walk uses (Reader.cpp): the clue-scan marker stores
        // its OWN tile as a fine destination at sub+0x74/+0x7C; the walk marker leaves 0.0/NaN
        // there. NaN fails every comparison, so it falls through to kKindDest.
        float dx = RF(sub + 0x74), dy = RF(sub + 0x7C);
        bool hasDest = dx > 0.f && dx < 1e9f && dy > 0.f && dy < 1e9f;
        s.kind = (hasDest && (std::int32_t)(dx / 512.f) == s.x && (std::int32_t)(dy / 512.f) == s.y)
                 ? rtx::special::kKindScan : rtx::special::kKindDest;
    } else if (type == 4) {
        // A health bar / hitsplat / overhead icon hangs off the ACTOR's node, so the node's own
        // section reports type 1 or 2. A standalone world effect (scan ring, rockertunity, Time
        // Sprite) points back at its own type-4 section. Structural, so a new adornment gfx id
        // classifies correctly without anyone adding it to a list.
        std::uint64_t osec = (ent > 0xfffff && ent < g_base) ? R64(ent + kSecPtr) : 0;
        std::uint8_t  ot   = (osec > 0xfffff && osec < g_base) ? R8(osec + kType) : 0xff;
        s.kind = (ot == 1 || ot == 2) ? rtx::special::kKindAdorn : rtx::special::kKindEffect;
    }
    s.stamp = (std::uint32_t)GetTickCount64();
    if (g_specialShare) {
        g_specialShare->diag[1]++;
        std::uint32_t prev = g_specialShare->diag[2];
        if (type == 4 && ((s.gfx >= 6841 && s.gfx <= 6843) || !(prev >= 6841 && prev <= 6843)))
            g_specialShare->diag[2] = (std::uint32_t)s.gfx;   // prefer a ring gfx
    }
}

void* Detour_T4Display(std::uint64_t a1, std::uint64_t a2, std::uint64_t a3, std::uint64_t a4,
                       std::uint64_t a5, std::uint64_t a6, std::uint64_t a7, std::uint64_t a8) {
    __try {
        if (g_specialOn.load(std::memory_order_relaxed)) RecordDisplay(a1, 4);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return g_origT4Display(a1, a2, a3, a4, a5, a6, a7, a8);
}
void* Detour_T13Display(std::uint64_t a1, std::uint64_t a2, std::uint64_t a3, std::uint64_t a4,
                        std::uint64_t a5, std::uint64_t a6, std::uint64_t a7, std::uint64_t a8) {
    __try {
        if (g_specialOn.load(std::memory_order_relaxed)) RecordDisplay(a1, 13);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return g_origT13Display(a1, a2, a3, a4, a5, a6, a7, a8);
}

// Object-delete capture (rs2client+0x50AA40): remove the entity on despawn so a stale
// ptr whose memory was reused is never re-read (the shared-sub flicker was exactly this).
void* Detour_ObjDel(std::uint64_t a1, std::uint64_t a2) {
    __try {
        if (a2 > 0xfffff && a2 < g_base) DeadMark(a2);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return g_origObjDel(a1, a2);
}

// Enumerate scene entities: add every entity ptr in a scene worker's vector
// (wk+0x138 begin .. wk+0x140 end) into the live set. Returns ptrs walked (diag).
static int WalkVecTrack(std::uint64_t wk) {
    if (!IsHeap(wk)) return 0;
    int cnt = 0;
    __try {
        std::uint64_t vb = R64(wk + rtx::scn::kVecBegin), ve = R64(wk + rtx::scn::kVecEnd);
        if (!IsHeap(vb) || ve < vb) return 0;
        std::uint64_t n = (ve - vb) / 8;
        if (n > 20000) n = 20000;
        for (std::uint64_t i = 0; i < n; ++i) {
            std::uint64_t ent = R64(vb + i * 8);
            if (IsHeap(ent)) { RenderInsert(ent); cnt++; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return cnt;
}

// Re-read ONE tracked entity ptr LIVE; if its slot currently holds the scan ring (type-4 with gfx
// 6841/6842/6843), record it. Type read live from sub+0x10, gfx from sub+0x74. Per-entity SEH is
// zero-cost on x64 unless a stale ptr faults.
static void ScanOneEnt(std::uint64_t ent) {
    __try {
        std::uint64_t sub = R64(ent + kSecPtr);
        if (!IsHeap(sub)) return;
        int type = R8(sub + kType);
        if (g_specialShare) g_specialShare->diag[4] |= (1u << (type & 31));
        // The scan dig-destination is a type-13 scene entity that spawns when the ring
        // goes red. Tile positions are floats in fine units (tile*512) at a type-specific
        // offset (obj 0x16C, decor 0x9C, npc 0x270). The diagnostic below dumps each field
        // as int / float / float-over-512; the offset whose f/512 is the dig tile is tileX/Y.
        if (type == 13) {
            static std::uint64_t s_lastT13 = 0;
            std::uint64_t now = GetTickCount64();
            if (now - s_lastT13 > 1200) { s_lastT13 = now;
                auto dump = [&](const char* tag, std::uint64_t base, int last) {
                    for (int off = 0; off <= last; off += 4) {
                        int v = R32(base + off);
                        union { int i; float f; } u; u.i = v;
                        float tile = u.f / 512.0f;
                        if ((tile > 200.0f && tile < 16000.0f) || (u.f > 200.0f && u.f < 16000.0f) ||
                            (v > 100000 && v < 8400000))
                            RingLog("  T13 %s+0x%x: i=%d f=%.1f f/512=%.2f", tag, off, v, u.f, tile);
                    }
                };
                RingLog("TYPE13 ent=0x%llx sub=0x%llx -- tile candidates (f/512 = world tile):", (unsigned long long)ent, (unsigned long long)sub);
                // A type-13 object is 0xF8 bytes (ctor 0x19F6F0: mov ecx,0xF8). Dumping 0x200
                // ran 0x108 bytes past the end into neighbouring heap, which is where the
                // bogus "tile candidates" in this log were coming from.
                dump("sub", sub, rtx::scn::kT13Size - 4);
                dump("ent", ent, 0x100);
            }
            return;
        }
        if (type != 4) return;
        int gfx = R32(sub + 0x74);                                 // proj_otherId
        {   // rate-limited: a type-4 entity is live (rare). Single worker-thread caller, static OK.
            static std::uint64_t s_lastT4Ms = 0;
            std::uint64_t now = GetTickCount64();
            if (now - s_lastT4Ms > 1500) { s_lastT4Ms = now;
                RingLog("TYPE4 LIVE: ent=0x%llx sub=0x%llx gfx=%d ring=%d", (unsigned long long)ent, (unsigned long long)sub, gfx, (gfx >= 6841 && gfx <= 6843) ? 1 : 0); }
        }
        if (g_specialShare) {
            g_specialShare->diag[1]++;                             // type-4 entities seen this pass
            std::uint32_t prev = g_specialShare->diag[2];
            if ((gfx >= 6841 && gfx <= 6843) || !(prev >= 6841 && prev <= 6843))
                g_specialShare->diag[2] = (std::uint32_t)gfx;      // prefer a ring gfx
        }
        if (gfx < 6841 || gfx > 6843) return;                      // only the scan ring -> g_hiRing
        auto& s = g_hiRing[g_hiIdx.fetch_add(1, std::memory_order_relaxed) % kHiRingCap];
        s.gfx   = gfx;
        s.uid   = R32(sub + 0x88);
        // Position from the scene NODE (float fine coords at ent+0x30/+0x38), which is the
        // same source Reader.cpp uses for every type-4 and type-13 it resolves correctly.
        // This previously read ints from sub+0x7c/+0x80: a type-4 object is only 0x1B0 bytes
        // so those are in bounds, but they are the entity's own projection fields, not a
        // world tile -- and Highlight documents x/y as "world tile". The panel does not
        // currently place the ring by tile (it renders at distance 0 and lets colour carry
        // the meaning), so this makes the field honest without changing what is drawn.
        {
            float nx = RF(ent + kEntPosX), ny = RF(ent + kEntPosY);
            s.x = (nx > 0.f && nx < 1e9f) ? (std::int32_t)(nx / 512.f) : 0;
            s.y = (ny > 0.f && ny < 1e9f) ? (std::int32_t)(ny / 512.f) : 0;
        }
        s.plane = (std::int16_t)R8(sub + kFloor);
        s.stamp = (std::uint32_t)GetTickCount64();
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Worker thread: re-read EVERY tracked entity ptr live for the ring.
// diag[1]/diag[4] reset each pass (current scene); diag[2] holds the live ring gfx if present.
void ScanTrackedForRing() {
    if (g_specialShare) { g_specialShare->diag[1] = 0; g_specialShare->diag[4] = 0; }
    // Snapshot the live set under the lock, then read each
    // entity's type live outside the lock.
    std::vector<std::uint64_t> snap;
    { std::lock_guard<std::mutex> lk(g_renderMu); snap.assign(g_renderSet.begin(), g_renderSet.end()); }
    for (std::uint64_t ent : snap) if (IsHeap(ent)) ScanOneEnt(ent);
    if (g_specialShare) g_specialShare->diag[6] = (std::uint32_t)snap.size();
}

rtx::special::Share* MapSpecialShare() {
    wchar_t name[64]; rtx::special::MakeSectionName(GetCurrentProcessId(), name);
    HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                  sizeof(rtx::special::Share), name);
    if (!h) return nullptr;
    return (rtx::special::Share*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(rtx::special::Share));
}

// ===== Ground items (scene entity type 3): dropped item stacks lying on a tile =====
// These come through the same spawn/despawn captures as everything else (into
// g_renderSet), NOT the scenery managers (g_mgrs). Each type-3 entity holds an OBJG
// stack sub-array: sub+0x70/0x78 = [begin,end), 0x90 stride, item id at element+0; tile
// from the entity's model-box corners (average of min@0x40/0x48 and max@0x50/0x58,
// /512). Published so the launcher can answer "which item is on the ground at (x,y)".
rtx::ground::Share* g_groundShare = nullptr;

rtx::ground::Share* MapGroundShare() {
    wchar_t name[64]; rtx::ground::MakeSectionName(GetCurrentProcessId(), name);
    HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                  sizeof(rtx::ground::Share), name);
    if (!h) return nullptr;
    return (rtx::ground::Share*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(rtx::ground::Share));
}

static rtx::ground::Item g_groundBuf[rtx::ground::kMaxItems];

// Per-entity type-3 read. Kept in its OWN function so the __try has no C++ objects that need
// unwinding (MSVC C2712 forbids mixing __try with such objects -- same reason ScanOneEnt is split
// out). Appends any item stacks on this entity's tile to g_groundBuf; returns the new count.
static std::uint32_t ScanGroundEnt(std::uint64_t ent, std::uint32_t c) {
    __try {
        if (!IsHeap(ent)) return c;
        std::uint64_t sub = R64(ent + kSecPtr);
        if (!IsHeap(sub)) return c;
        if (R8(sub + kType) != 3) return c;                       // ground items only
        float ex = (RF(ent + kBoxMinX) + RF(ent + kBoxMaxX)) * 0.5f;   // east centre
        float nz = (RF(ent + kBoxMinZ) + RF(ent + kBoxMaxZ)) * 0.5f;   // north centre
        int tx = (int)(ex / 512.f), ty = (int)(nz / 512.f);
        if (tx <= 0 || ty <= 0 || tx > 16384 || ty > 16384) {         // fall back to the fine pos
            float fx = RF(ent + kEntPosX), fy = RF(ent + kEntPosY);
            tx = (int)(fx / 512.f); ty = (int)(fy / 512.f);
            if (tx <= 0 || ty <= 0 || tx > 16384 || ty > 16384) return c;
        }
        std::int32_t plane = R32(sub + kFloor);
        std::uint64_t beg = R64(sub + 0x70), end = R64(sub + 0x78);   // OBJG stack sub-array
        if (!IsHeap(beg) || end <= beg) return c;
        std::uint64_t stacks = (end - beg) / 0x90;
        if (stacks == 0 || stacks > 256) return c;
        for (std::uint64_t s = 0; s < stacks && c < (std::uint32_t)rtx::ground::kMaxItems; ++s) {
            std::int32_t id = R32(beg + s * 0x90);                    // item id at element+0
            if (id <= 0 || id > 300000) continue;
            g_groundBuf[c].id = id; g_groundBuf[c].x = tx; g_groundBuf[c].y = ty; g_groundBuf[c].plane = plane;
            ++c;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return c;
}

void PublishGround(rtx::ground::Share* sh) {
    if (!sh) return;
    std::vector<std::uint64_t> snap;
    { std::lock_guard<std::mutex> lk(g_renderMu); snap.assign(g_renderSet.begin(), g_renderSet.end()); }
    std::uint32_t c = 0;
    for (std::uint64_t ent : snap) {
        if (c >= (std::uint32_t)rtx::ground::kMaxItems) break;
        c = ScanGroundEnt(ent, c);
    }
    sh->seq++;                                    // odd: mid-update
    MemoryBarrier();                              // data must not move above the odd seq
    std::memcpy(sh->items, g_groundBuf, (std::size_t)c * sizeof(rtx::ground::Item));
    sh->count = c;
    MemoryBarrier();                              // data must land before the even seq
    sh->seq++;                                    // even: complete
}

// ---- raw inbound socket capture -----------------------------------------------
// Hooked at ws2_32, below any decoding, so records hold whatever the wire carried.
// Opcodes are enciphered at this level; NetProbe below captures decoded messages.
rtx::net::Share* g_netShare = nullptr;

typedef int (WSAAPI* Recv_t)(SOCKET, char*, int, int);
typedef int (WSAAPI* WSARecv_t)(SOCKET, LPWSABUF, DWORD, LPDWORD, LPDWORD,
                                LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
static Recv_t    g_origRecv    = nullptr;
static WSARecv_t g_origWsaRecv = nullptr;

// Publishes `written` LAST so a reader observing N can trust records [0, N).
// A reader can still race the writer for the oldest slot, which is acceptable
// for a diagnostic ring.
static void NetRecord(SOCKET s, const char* buf, int n, std::uint32_t api) {
    auto* sh = g_netShare;
    if (!sh || !sh->enable || n <= 0 || !buf) return;
    rtx::net::Record& r = sh->recs[sh->written % rtx::net::kMaxRecords];
    r.tick = GetTickCount64();
    r.sock = (std::uint64_t)s;
    r.ret  = n;
    r.api  = api;
    r.kept = (std::uint32_t)(n < rtx::net::kSnip ? n : rtx::net::kSnip);
    std::memcpy(r.data, buf, r.kept);
    sh->bytesTotal += (std::uint64_t)n;
    ++sh->written;
}

static int WSAAPI Detour_Recv(SOCKET s, char* buf, int len, int flags) {
    const int n = g_origRecv(s, buf, len, flags);
    NetRecord(s, buf, n, 1);
    return n;
}

// Records only a SYNCHRONOUS completion: with lpOverlapped set the buffer is not
// filled at return, so recording there would capture stale bytes.
static int WSAAPI Detour_WSARecv(SOCKET s, LPWSABUF bufs, DWORD count, LPDWORD got,
                                 LPDWORD flags, LPWSAOVERLAPPED ov,
                                 LPWSAOVERLAPPED_COMPLETION_ROUTINE cr) {
    const int rc = g_origWsaRecv(s, bufs, count, got, flags, ov, cr);
    if (rc == 0 && !ov && !cr && got && bufs && count) {
        const DWORD n = (*got < bufs[0].len) ? *got : bufs[0].len;
        NetRecord(s, bufs[0].buf, (int)n, 2);
    }
    return rc;
}

static rtx::net::Share* MapNetShare() {
    wchar_t name[128];
    swprintf_s(name, L"%s%lu", rtx::net::kSectionPrefix,
               (unsigned long)GetCurrentProcessId());
    HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                  sizeof(rtx::net::Share), name);
    if (!h) return nullptr;
    return (rtx::net::Share*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0,
                                           sizeof(rtx::net::Share));
}

void ResolveNetCapture() {
    g_netShare = MapNetShare();
    if (!g_netShare) { RingLog("net: share map FAILED"); return; }
    g_netShare->magic   = rtx::net::kMagic;
    g_netShare->version = rtx::net::kVersion;
    g_netShare->pid     = GetCurrentProcessId();
    g_netShare->written = 0;
    g_netShare->bytesTotal = 0;
    g_netShare->flags   = 0;
    g_netShare->enable  = 1;   // on by default: cost is one <=512B memcpy per recv
    // LoadLibrary, not GetModuleHandle: at companion init the client may not have
    // pulled winsock in yet, and a null handle here would silently skip the hook.
    HMODULE ws2 = LoadLibraryW(L"ws2_32.dll");
    if (!ws2) { RingLog("net: ws2_32 unavailable"); return; }
    auto pRecv = (Recv_t)GetProcAddress(ws2, "recv");
    auto pWsa  = (WSARecv_t)GetProcAddress(ws2, "WSARecv");
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    if (pRecv) { g_origRecv = pRecv; DetourAttach(&(PVOID&)g_origRecv, (PVOID)Detour_Recv); }
    if (pWsa)  { g_origWsaRecv = pWsa; DetourAttach(&(PVOID&)g_origWsaRecv, (PVOID)Detour_WSARecv); }
    if (DetourTransactionCommit() == NO_ERROR) {
        if (pRecv) g_netShare->flags |= 1;
        if (pWsa)  g_netShare->flags |= 2;
        RingLog("net: inbound capture ATTACHED recv=%d wsarecv=%d",
                pRecv ? 1 : 0, pWsa ? 1 : 0);
    } else {
        RingLog("net: inbound capture attach FAILED");
    }
}

// ---- decoded inbound packet capture -------------------------------------------
// Hooked at the GAME's inbound framer (FUN_1400ff0c0), one level above the socket:
// the function that reads one server message, ISAAC-deciphers its opcode, looks the
// opcode up in the packet table (base held at rs2client+0xC6A158, entries 0..0xE5),
// and reads the declared-length payload into the connection buffer. At its return the
// connection object carries the plaintext opcode + length + payload -- exactly what
// the client itself will hand to the packet's handler. Zero cost while the panel is
// closed: the detour calls through and returns unless `enable` was stamped recently.
//
// Connection-object field offsets, from the framer decompile (re-derive if a game
// update shifts them):
//   +0x2C  int  opcode  (deciphered; 0xFFFFFFFF = none / invalid, framer set it so)
//   +0x30  int  length  (resolved payload length, after any 1/2-byte size prefix)
//   +0x2D0 ptr  payload buffer (the framer read `length` bytes to *(conn+0x2D0))
//   +0x2E8 int  cumulative inbound byte counter (monotonic; de-dups a re-observed msg)
rtx::netprobe::Share* g_netProbeShare = nullptr;
typedef std::uint64_t* (*Framer_t)(std::uint64_t conn, std::uint64_t* out);
static Framer_t g_origFramer = nullptr;

static void NetProbeRecord(std::uint64_t conn, std::uint64_t* out) {
    auto* sh = g_netProbeShare;
    if (!sh) return;
    if (!out || out[0] == 0) return;                          // no message completed this call
    const std::uint32_t now = (std::uint32_t)GetTickCount64();
    // Diag ring only records while the netprobe panel keeps this stamp fresh; the chat
    // ring below is always-on so the chat log never depends on a panel being open.
    const bool armed = sh->enable != 0 && (std::uint32_t)(now - sh->enable) <= 3000;
    __try {
        const std::int32_t op = *(const std::int32_t*)(conn + 0x2c);
        if (op < 0 || op > 0xE5) { if (armed) sh->diag[2]++; return; }   // invalid/none
        const std::int32_t  len = *(const std::int32_t*)(conn + 0x30);
        const std::uint32_t rx  = *(const std::uint32_t*)(conn + 0x2e8);
        // The framer is re-entered on the same pending message until the dispatcher
        // consumes it; a genuinely new packet advanced the byte counter.
        static std::uint32_t s_lastRx = 0xFFFFFFFFu; static std::int32_t s_lastOp = -1;
        if (rx == s_lastRx && op == s_lastOp) { if (armed) sh->diag[3]++; return; }
        s_lastRx = rx; s_lastOp = op;
        const std::uint8_t* p =
            len > 0 ? *(const std::uint8_t* const*)(conn + 0x2d0) : nullptr;

        if (op == 0x15) {                                     // message_game -> chat ring
            rtx::netprobe::ChatRecord& c =
                sh->chat[sh->chatWritten % rtx::netprobe::kChatRecords];
            c.tick   = GetTickCount64();
            c.seq    = sh->chatWritten + 1;
            c.length = len;
            std::uint32_t ck = 0;
            if (p) {
                ck = (std::uint32_t)(len < rtx::netprobe::kChatSnip
                                     ? len : rtx::netprobe::kChatSnip);
                std::memcpy(c.data, p, ck);
            }
            c.kept = ck;
            sh->chatSeen++;
            ++sh->chatWritten;                                // publish LAST
        }

        if (!armed) return;
        sh->diag[0]++;                                        // framer messages seen while armed
        rtx::netprobe::Record& r = sh->recs[sh->written % rtx::netprobe::kMaxRecords];
        r.tick   = GetTickCount64();
        r.seq    = sh->written + 1;
        r.opcode = op;
        r.length = len;
        r.gtick  = 0;
        std::uint32_t kept = 0;
        if (p) {
            kept = (std::uint32_t)(len < rtx::netprobe::kSnip ? len : rtx::netprobe::kSnip);
            std::memcpy(r.data, p, kept);
        }
        r.kept = kept;
        sh->seen++;
        ++sh->written;                                        // publish LAST (record body is filled)
        sh->diag[1]++;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static std::uint64_t* Detour_Framer(std::uint64_t conn, std::uint64_t* out) {
    std::uint64_t* r = g_origFramer(conn, out);
    NetProbeRecord(conn, out);
    return r;
}

static rtx::netprobe::Share* MapNetProbeShare() {
    wchar_t name[128];
    swprintf_s(name, L"%s%lu", rtx::netprobe::kSectionPrefix,
               (unsigned long)GetCurrentProcessId());
    HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                  sizeof(rtx::netprobe::Share), name);
    if (!h) return nullptr;
    return (rtx::netprobe::Share*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0,
                                                sizeof(rtx::netprobe::Share));
}

void ResolveNetProbe() {
    g_netProbeShare = MapNetProbeShare();
    if (!g_netProbeShare) { RingLog("netprobe: share map FAILED"); return; }
    auto* sh = g_netProbeShare;
    sh->magic = rtx::netprobe::kMagic; sh->version = rtx::netprobe::kVersion;
    sh->pid = GetCurrentProcessId(); sh->enable = 0; sh->written = 0; sh->seen = 0;
    sh->flags = 0; sh->framerRva = 0;
    sh->chatWritten = 0; sh->chatSeen = 0;
    for (int i = 0; i < 8; ++i) sh->diag[i] = 0;
    // Framer entry prologue:
    //   push rbx; push rsi; push r14; push r15; sub rsp,0x28; xor esi,esi; mov rbx,rcx;
    //   mov rcx,[rcx+8]; mov r14,rdx; mov r15d,esi; test rcx,rcx; jz <rel32>; cmp dword[rcx],2
    // The `test rcx,rcx; jz; cmp [rcx],2` tail (the connection-state guard) makes it unique;
    // the jz's rel32 is build-specific -> wildcarded. .pdata maps the match to the true entry.
    static const unsigned char body[] = {
        0x40,0x53,0x56,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x28,0x33,0xF6,0x48,0x8B,0xD9,
        0x48,0x8B,0x49,0x08,0x4C,0x8B,0xF2,0x44,0x8B,0xFE,0x48,0x85,0xC9,0x0F,0x84,
        0x00,0x00,0x00,0x00,                                            // jz rel32 (wildcard)
        0x83,0x39,0x02 };
    static const unsigned char mask[] = {
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        0,0,0,0,
        1,1,1 };
    std::uint64_t p = FindVarOpWild(body, mask, sizeof(body));
    if (!p) { RingLog("netprobe: framer NOT FOUND (re-derive signature)"); return; }
    sh->framerRva = (std::uint32_t)(p - g_base);
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    g_origFramer = (Framer_t)p;
    DetourAttach(&(PVOID&)g_origFramer, (PVOID)Detour_Framer);
    if (DetourTransactionCommit() == NO_ERROR) {
        sh->flags |= 1;
        RingLog("netprobe: framer hook ATTACHED rva=0x%llx", (unsigned long long)(p - g_base));
    } else {
        RingLog("netprobe: framer hook attach FAILED");
    }
}

void ResolveSpecialObserver() {
    g_specialShare = MapSpecialShare();
    if (g_specialShare) {
        g_specialShare->magic = rtx::special::kMagic; g_specialShare->version = rtx::special::kVersion;
        g_specialShare->pid = GetCurrentProcessId();
        g_specialShare->enable = 0; g_specialShare->count = 0; g_specialShare->seq = 0; g_specialShare->flags = 0;
        g_specialShare->diag[0] = g_specialShare->diag[1] = g_specialShare->diag[2] = 0;
        g_specialShare->diag[3] = g_specialShare->diag[4] = g_specialShare->diag[5] = 0;
        g_specialShare->diag[6] = g_specialShare->diag[7] = 0;
        g_specialShare->diag[8] = g_specialShare->diag[9] = g_specialShare->diag[10] = g_specialShare->diag[11] = 0;
    }
    // Object-submit fn resolution, built to survive game updates:
    //   1. Anchored on the fn's post-call SCENE-ARRAY WALK, not a generic prologue:
    //        mov rcx,rdx; mov r8d,0x47; mov r14,rdx; call <X>; mov rcx,[rbx+0x140]; cmp rcx,[rbx+0x148]
    //      [rbx+0x140]/[rbx+0x148] are the worker-vec end/cap -- the SAME offsets the
    //      reader walks; they uniquely separate this fn from other "mov r8d,0x47" sites.
    //   2. The 4 bytes after the CALL are its rel32 (build-specific) -> WILDCARDED via the mask.
    //   3. .pdata maps the in-fn match to the true entry.
    // Self-validating: if an update changes the body so this misresolves, the type-4-seen
    // diag (diag[1]) stays 0 while fires (diag[0]) climbs. Mask 0 == wildcard.
    static const unsigned char body[] = {
        0x00,0x00,0x48,0x8B,0xCA,0x41,0xB8,0x47,0x00,0x00,0x00,0x4C,0x8B,0xF2,0xE8,
        0x00,0x00,0x00,0x00,                                          // CALL rel32 (wildcard)
        0x48,0x8B,0x8B,0x40,0x01,0x00,0x00,0x48,0x3B,0x8B,0x48,0x01,0x00 };
    static const unsigned char mask[] = {
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        0,0,0,0,
        1,1,1,1,1,1,1,1,1,1,1,1,1 };
    std::uint64_t p = FindVarOpWild(body, mask, sizeof(body));   // maps the in-fn match -> true entry via .pdata
    if (p) {
        if (g_specialShare) g_specialShare->diag[5] = (std::uint32_t)(p - g_base);   // diag: resolved fn RVA
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        g_origObjSubmit = (ObjSubmit_t)p;
        DetourAttach(&(PVOID&)g_origObjSubmit, (PVOID)Detour_ObjSubmit);
        if (DetourTransactionCommit() == NO_ERROR && g_specialShare) { g_specialShare->flags |= 1; g_specialShare->diag[3] = 1; g_hookInstallMs = GetTickCount64(); RingLog("hook: spawn-hook ATTACHED rva=0x%llx", (unsigned long long)(p - g_base)); }
        else RingLog("hook: spawn-hook attach FAILED");
    }

    // Object-delete fn (rs2client+0x50AA40): resolve via its entry body (prologue + the
    // rcx+0x140 / rdi+0x138 worker-vec compare, which uniquely separates it). Removing
    // entities on despawn keeps the set LIVE-ONLY; else reused stale ptrs flicker as
    // false type-4s.
    static const unsigned char delBody[] = {
        0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF9,0x48,0x8B,0xDA,
        0x48,0x8B,0x89,0x40,0x01,0x00,0x00,0x48,0x8B,0x87,0x38,0x01,0x00,0x00,0x48,0x3B,0xC1 };
    std::uint64_t pd = FindVarOp(delBody, sizeof(delBody));
    if (pd) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        g_origObjDel = (ObjDel_t)pd;
        DetourAttach(&(PVOID&)g_origObjDel, (PVOID)Detour_ObjDel);
        if (DetourTransactionCommit() == NO_ERROR) RingLog("hook: del-hook ATTACHED rva=0x%llx", (unsigned long long)(pd - g_base));
        else RingLog("hook: del-hook attach FAILED");
    } else RingLog("hook: del-hook NOT FOUND");

    // ---- per-type display hooks ----
    // Resolved by body pattern, not by RVA, so a rebuild that moves the function still finds
    // it; a build that changes the body simply does not attach and says so, rather than
    // attaching to the wrong function. Each pattern matches EXACTLY ONCE in .text on build
    // 940, and each maps to the vtable slot it belongs to:
    //   0x326690 == *(0xB73A50 + 7*8)      0x1ABBB0 == *(0xB5E878 + 7*8)
    // The type-4 pattern starts mid-function (0x32669D); FindVarOp maps a body match back to
    // the true entry through .pdata, which is what makes that one safe to detour.
    static const unsigned char t4Body[] = {
        0x80,0xB9,0xAD,0x01,0x00,0x00,0x00,
        0x49,0x8B,0xD9, 0x49,0x8B,0xE8, 0x4C,0x8B,0xF2, 0x48,0x8B,0xF9 };
    std::uint64_t p4 = FindVarOp(t4Body, sizeof(t4Body));
    if (p4) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        g_origT4Display = (Display_t)p4;
        DetourAttach(&(PVOID&)g_origT4Display, (PVOID)Detour_T4Display);
        if (DetourTransactionCommit() == NO_ERROR) RingLog("hook: t4-display ATTACHED rva=0x%llx", (unsigned long long)(p4 - g_base));
        else RingLog("hook: t4-display attach FAILED");
    } else RingLog("hook: t4-display NOT FOUND");

    static const unsigned char t13Body[] = {
        0x48,0x89,0x5C,0x24,0x10, 0x48,0x89,0x6C,0x24,0x18,
        0x56, 0x48,0x81,0xEC,0xE0,0x00,0x00,0x00 };
    std::uint64_t p13 = FindVarOp(t13Body, sizeof(t13Body));
    if (p13) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        g_origT13Display = (Display_t)p13;
        DetourAttach(&(PVOID&)g_origT13Display, (PVOID)Detour_T13Display);
        if (DetourTransactionCommit() == NO_ERROR) RingLog("hook: t13-display ATTACHED rva=0x%llx", (unsigned long long)(p13 - g_base));
        else RingLog("hook: t13-display attach FAILED");
    } else RingLog("hook: t13-display NOT FOUND");

}

void PublishSpecials(rtx::special::Share* sh) {
    if (!sh) return;
    std::uint32_t now = (std::uint32_t)GetTickCount64();
    // Arm the observer only while the launcher has refreshed enable (a stamp) recently; otherwise it
    // idles (cost ~zero on the render thread). 3s window > the launcher's poll cadence.
    g_specialOn.store(sh->enable != 0 && (std::uint32_t)(now - sh->enable) < 3000, std::memory_order_relaxed);
    if (!g_specialOn.load(std::memory_order_relaxed)) { sh->seq++; MemoryBarrier(); sh->count = 0; MemoryBarrier(); sh->seq++; return; }
    rtx::special::Highlight out[rtx::special::kMaxHighlights];
    std::uint32_t c = 0;
    // Two passes so per-actor adornments cannot crowd out the captures this hook exists for. A
    // busy fight renders far more health bars and hitsplats than there are publish slots, and a
    // single-pass ring scan would evict the scan ring behind them.
    for (int pass = 0; pass < 2; ++pass)
    for (int i = 0; i < kHiRingCap && c < (std::uint32_t)rtx::special::kMaxHighlights; ++i) {
        rtx::special::Highlight hh = g_hiRing[i];                 // best-effort POD copy (racy, range-guarded reader)
        if (hh.stamp == 0 || (std::uint32_t)(now - hh.stamp) > rtx::special::kStaleMs) continue;   // empty or stale
        if ((hh.kind == rtx::special::kKindAdorn) != (pass == 1)) continue;   // effects first, adornments after
        bool dup = false;
        for (std::uint32_t j = 0; j < c; ++j) {
            // uid is the dedupe key ONLY when the entity has one. Markers and effects frequently
            // read uid 0, and keying those on uid alone merged every one of them into a single
            // slot whose stamp any later capture refreshed -- so the entry could never age out
            // and appeared to linger on screen forever. Fall back to identity-by-placement.
            bool same = hh.uid ? (out[j].uid == hh.uid)
                              : (out[j].uid == 0 && out[j].type == hh.type && out[j].gfx == hh.gfx &&
                                 out[j].x == hh.x && out[j].y == hh.y && out[j].plane == hh.plane);
            if (same) { if (hh.stamp > out[j].stamp) out[j] = hh; dup = true; break; }
        }
        if (!dup) out[c++] = hh;
    }
    sh->seq++;                                    // odd: mid-update
    MemoryBarrier();                              // data must not move above the odd seq
    std::memcpy(sh->items, out, (std::size_t)c * sizeof(rtx::special::Highlight));
    sh->count = c;
    MemoryBarrier();                              // data must land before the even seq
    sh->seq++;                                    // even: complete
}

std::uint64_t FindLocalPlayerSub() {
    std::uint64_t root = Root();
    std::uint64_t cont = R64(root + rtx::scn::kContainer);
    int idx = R32(cont + rtx::scn::kActiveIdx);
    std::uint64_t arr = R64(cont + rtx::scn::kEntryArr);
    if (!IsHeap(arr) || idx < 0) return 0;
    std::uint64_t W = R64(arr + (std::uint64_t)idx * 0x10 + rtx::scn::kEntryWv);
    std::uint64_t wk = SceneWorker(W);
    std::uint64_t vb = R64(wk + kVecBegin), ve = R64(wk + kVecEnd);
    if (!IsHeap(vb) || ve <= vb) return 0;
    std::uint64_t pdata = R64(root + rtx::scn::kPlayerData);
    int luid = IsHeap(pdata) ? R32(pdata + rtx::scn::kLocalUid) : -1;
    std::uint64_t n = (ve - vb) / 8; if (n > 30000) n = 30000;
    for (std::uint64_t i = 0; i < n; ++i) {
        std::uint64_t ep = R64(vb + i * 8);
        if (!IsHeap(ep)) continue;
        std::uint64_t sub = R64(ep + kSecPtr);
        if (IsHeap(sub) && R8(sub + kType) == 2 && R32(sub + rtx::scn::kUid) == luid) return sub;
    }
    return 0;
}

DWORD WINAPI Worker(LPVOID) {
    HMODULE gm = GetModuleHandleW(L"rs2client.exe");
    if (!gm) return 0;
    g_base = (std::uint64_t)gm;
    RingLog("=== BOOT: companion worker thread running (DLL loaded), base=0x%llx ===", (unsigned long long)g_base);
    {   // image size from the PE headers (in-module, safe to read)
        auto dos = (const IMAGE_DOS_HEADER*)g_base;
        auto nt  = (const IMAGE_NT_HEADERS*)(g_base + dos->e_lfanew);
        g_size = nt->OptionalHeader.SizeOfImage;
    }

    Share* sh = MapShare();
    if (!sh) return 0;
    sh->magic = rtx::scene::kMagic;
    sh->version = rtx::scene::kVersion;
    sh->pid = GetCurrentProcessId();
    sh->count = 0; sh->diag_len = 0; sh->seq = 0;

    // Ground items (type-3 dropped stacks): own section, published each worker round from g_renderSet.
    g_groundShare = MapGroundShare();
    if (g_groundShare) {
        g_groundShare->magic = rtx::ground::kMagic; g_groundShare->version = rtx::ground::kVersion;
        g_groundShare->pid = GetCurrentProcessId();
        g_groundShare->count = 0; g_groundShare->seq = 0; g_groundShare->flags = 0;
    }

    // Live client-variable values: map the section and observe the two int-pushing var
    // op handlers (varp + varc-int). Read-only; each observer records a value only while
    // the launcher enables it (Vars watcher open). flags bit0 = observers installed.
    g_varcShare = MapVarcShare();
    if (g_varcShare) {
        g_varcShare->magic = rtx::varc::kMagic; g_varcShare->version = rtx::varc::kVersion;
        g_varcShare->pid = GetCurrentProcessId(); g_varcShare->count = 0; g_varcShare->strCount = 0;
        g_varcShare->enable = 0; g_varcShare->flags = 0; g_varcShare->seq = 0;
        for (int i = 0; i < 8; ++i) g_varcShare->diag[i] = 0;
        // Same body up to the final bucket-load register: varp ends ...49 8B 0C C2, varc ...49 8B 04 C2.
        static const unsigned char kVarpBody[] = {0x4C,0x8B,0x4A,0x10,0x48,0x8B,0xDA,0x44,0x0F,0xB7,0x42,0x24,
            0x33,0xD2,0x41,0x8B,0xC0,0x41,0x8B,0x49,0x60,0x4D,0x8B,0x51,0x58,0x48,0xF7,0xF1,0x8B,0xC2,0x49,0x8B,0x0C,0xC2};
        static const unsigned char kVarcBody[] = {0x4C,0x8B,0x4A,0x10,0x48,0x8B,0xDA,0x44,0x0F,0xB7,0x42,0x24,
            0x33,0xD2,0x41,0x8B,0xC0,0x41,0x8B,0x49,0x60,0x4D,0x8B,0x51,0x58,0x48,0xF7,0xF1,0x8B,0xC2,0x49,0x8B,0x04,0xC2};
        // Observe the two int-pushing handlers only to SNAPSHOT the live vm_ctx (scope
        // source for the registry walk); no operand-stack read. Capturing the scope
        // lets the worker thread walk getStorage off the game thread.
        std::uint64_t pVarp = FindVarOp(kVarpBody, sizeof(kVarpBody));
        std::uint64_t pVarc = FindVarOp(kVarcBody, sizeof(kVarcBody));
        if (pVarp || pVarc) {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            if (pVarp) { g_origVarp = (VarOp_t)pVarp; DetourAttach(&(PVOID&)g_origVarp, (PVOID)Detour_Varp); }
            if (pVarc) { g_origVarc = (VarOp_t)pVarc; DetourAttach(&(PVOID&)g_origVarc, (PVOID)Detour_Varc); }
            if (DetourTransactionCommit() == NO_ERROR) g_varcShare->flags = 1;
        }
        // cc_if_setdraggable: distinctive body = `add dword [r9+0x10A0], -2; mov rbp,rcx;
        // mov eax,[r9+..]` (the op's stack-pointer pop). FindVarOp maps it to the function
        // entry via .pdata. Observed so a panel's position vars resolve when it OPENS
        // (they aren't pushed until a drag). r9 = the 4th arg = vm_ctx.
        static const unsigned char kCcDragBody[] = {0x41,0x83,0x81,0xA0,0x10,0x00,0x00,0xFE,0x48,0x8B,0xE9,0x41,0x8B,0x81};
        std::uint64_t pCc = FindVarOp(kCcDragBody, sizeof(kCcDragBody));
        if (pCc) {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            g_origCcDrag = (CcOp_t)pCc; DetourAttach(&(PVOID&)g_origCcDrag, (PVOID)Detour_CcIfSetDraggable);
            if (DetourTransactionCommit() == NO_ERROR) g_varcShare->flags |= 2;   // bit1 = cc_if_setdraggable observed
        }
    }

    // Render toggles: map the launcher->module flag channel, install the entity
    // capture points + resolve the scene-blank point (all no-ops until a flag is set).
    ResolveRenderHooks();

    // Render-pass highlight observer (the clue-scan ring etc.): no-op until the launcher arms it.
    ResolveSpecialObserver();

    // Decoded server->client packet capture at the inbound framer. Own Detour transaction,
    // installed last; the hook is a pass-through until the Server Packets panel arms it
    // (stamps `enable`), so it costs nothing while closed. Unlike the ws2_32 raw capture
    // above, this is a game-function detour in the same class as the spawn/varc hooks.
    ResolveNetProbe();

    // Raw inbound socket capture: disabled. It installs its own Detours transaction next
    // to the scene hooks during init and forces winsock2.h ahead of windows.h here.
    // Re-enable only to run another wire capture, and check the scene share still fills.
    // ResolveNetCapture();

    // In-frame compositor: draws into the game frame just before each buffer-swap. The
    // sidebar is a docked window, but the WORLD-MARKER layer (NPC/scene/object highlights
    // over the 3D view) is drawn in-frame, so the present callback is active.
    OutputDebugStringA(rtx::present::Install()
                           ? "RuneToolsX: in-frame compositor active"
                           : "RuneToolsX: buffer-swap entry not found (compositor off)");

    // Cache-sound channel: publishes the sound id behind each mixed chunk so the Sounds panel
    // can name what just played, and silences ids the launcher has muted. Idle until the panel
    // sets `enable`, so a failure here costs nothing but the feature.
    RingLog(rtx::soundfilter::Install() ? "sound: mix hook ATTACHED"
                                        : "sound: mix fn not found (observation/mute off)");

    // Right-click menu probe. Attaches one detour that records the manager pointer and does
    // nothing else; the dump only runs with RTX_MENU_PROBE=1 set.
    RingLog(rtx::menuprobe::Install() ? "menu: probe installed"
                                      : "menu: string-init pattern not found");

    // Stable capture: prune only dead containers each tick (no flicker), and discover
    // NEW ones (accumulating) when first empty, after the player has walked far enough
    // that fresh containers can be in range, OR periodically -- objects can spawn into
    // BRAND-NEW containers right next to a stationary player (portables, player-placed
    // scenery), which a movement-only trigger never sees. Accumulation means a rescan
    // can only add; captured objects never flicker or revert.
    // Deep-sweep cadence: the sweep walks every committed R/W region in the process
    // ("gigabytes... takes seconds" per FindContainers), so re-running it on a fixed
    // 12s timer forever saturated memory bandwidth inside the game for the whole
    // session. Its only remaining job is entities that PRE-DATE the spawn hook (the
    // ungated tracked pass right below catches every post-hook spawn, portables and
    // player-placed scenery included), so: back off exponentially while sweeps find
    // nothing new, reset to the base period the moment one adds a container, and
    // re-arm immediately when the player moves far enough for pre-hook entities to
    // have entered scan range. Accumulation semantics make rarer sweeps safe.
    constexpr ULONGLONG kRescanMs = 12000;               // base deep-sweep period
    constexpr ULONGLONG kRescanMaxMs = 300000;           // backoff ceiling (5 min)
    ULONGLONG lastScanMs = 0, rescanMs = kRescanMs;
    float scanPx = 0, scanPy = 0; bool haveScanPos = false;
    int emptyTicks = 0;
    for (;;) {
        float cpx = 0, cpy = 0;
        bool havePos = PlayerFineOrLast(cpx, cpy);
        PruneInvalid();                                  // drop only freed containers; keep live ones
        rtx::menuprobe::Poll();                          // no-op unless RTX_MENU_PROBE=1
        // Moved more than half the scan range since the last deep sweep: pre-hook
        // entities can now be in range, so sweep NOW regardless of backoff.
        const float kMoveArm = 32.f * 512.f;
        const bool moved = havePos && haveScanPos &&
            (std::fabs(cpx - scanPx) > kMoveArm || std::fabs(cpy - scanPy) > kMoveArm);
        bool stale = moved || GetTickCount64() - lastScanMs > rescanMs;
        // The fast path is ungated: a few hundred pointer reads, so a container is picked up
        // on the very next pass after the hook sees an entity in it.
        if (havePos) FindContainersFromTracked();
        // The DEEP sweep stays gated -- it walks every committed region in the process and
        // is the genuinely expensive one. It exists to catch entities that were already in
        // the scene before the hook attached, which is not a latency-critical case.
        if (havePos && (g_mgrCount == 0 || stale)) {
            const int before = g_mgrCount;
            FindContainers(true);                        // accumulate newly in-range containers
            lastScanMs = GetTickCount64();
            scanPx = cpx; scanPy = cpy; haveScanPos = true;
            if (g_mgrCount > before || moved) rescanMs = kRescanMs;                 // productive: stay eager
            else rescanMs = rescanMs >= kRescanMaxMs ? kRescanMaxMs : rescanMs * 2; // fruitless: back off
        }
        if (g_mgrCount > 0) {
            Publish(sh, sh->diag_len < 0x260);           // keep dumping until a sub of each type captured
            emptyTicks = 0;
        } else if (++emptyTicks >= 8) {
            // Genuinely empty for ~2s -> publish the empty set. A transient empty (a
            // scene/region reload frees all containers for a tick or two before they
            // re-discover) keeps the LAST good publish, so boxes don't flash off/on.
            sh->seq++; MemoryBarrier(); sh->count = 0; MemoryBarrier(); sh->seq++;
        }
        // Publish ALWAYS (not gated on the Vars watcher): panel-position vars are resolved
        // always-on (ResolvePanelGroupsFromLive), so they must be readable by the launcher without
        // any test/watcher enabled. When the watcher is off, g_storageCache holds only the panel
        // whitelist, so this is cheap; when on, it also carries the full per-var capture.
        if (g_varcShare) PublishVarcs(g_varcShare);
        std::uint64_t root = g_SceneWorkerRoot.load(std::memory_order_relaxed);
        int entN = WalkVecTrack(root);
        if (g_specialShare) {
            g_specialShare->diag[7] = (std::uint32_t)entN;
            // diag[8] = seconds the spawn capture has been installed (proves early presence).
            g_specialShare->diag[8] = g_hookInstallMs ? (std::uint32_t)((GetTickCount64() - g_hookInstallMs) / 1000) : 0;
            // [9..11] name the object pipeline's failure stage: every other diag can read
            // healthy while root resolution is the stage that has broken.
            g_specialShare->diag[9]  = g_rootMethod;                  // 0 none / 1 anchor / 2 structural
            g_specialShare->diag[10] = (std::uint32_t)g_mgrCount;     // containers discovered
            g_specialShare->diag[11] = (std::uint32_t)g_posSource;    // 0 none / 1 live / 2 remembered
        }
        if (g_specialOn.load(std::memory_order_relaxed)) ScanTrackedForRing();   // re-read every tracked ptr live
        if (g_specialShare) PublishSpecials(g_specialShare);
        if (g_groundShare) PublishGround(g_groundShare);   // dropped ground items (type 3) from g_renderSet
        {   // periodic timeline log (~every 5s) recording the capture/ring state over time
            static int s_logCnt = 0;
            if (g_specialShare && (++s_logCnt % 20) == 0)
                RingLog("diag: uptime=%us armed=%d fires=%u tracked=%u type4=%u gfx=%u workervec=%u root=0x%llx",
                        g_specialShare->diag[8], g_specialOn.load(std::memory_order_relaxed) ? 1 : 0,
                        g_specialShare->diag[0], g_specialShare->diag[6], g_specialShare->diag[1],
                        g_specialShare->diag[2], g_specialShare->diag[7], (unsigned long long)root);
        }

        // Render toggles: keep the local-player sub current (for the player-hide path)
        // and flip the scene-blank byte when the master toggle changes.
        if (g_renderShare) {
            std::uint64_t lps = FindLocalPlayerSub();
            if (lps) g_localPlayerSub.store(lps, std::memory_order_relaxed);
            bool wantBlank = g_renderShare->hideAll != 0;
            if (wantBlank != g_sceneBlankPatched) SceneBlankSet(wantBlank);
        }
        Sleep(250);
    }
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    }
    return TRUE;
}
