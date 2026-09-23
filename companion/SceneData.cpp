// RuneToolsX client companion module: in-process scene reader publishing to shared sections.

#include <winsock2.h>   // must precede windows.h
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

#include "SceneOffsets.h"
#include "SceneHover.h"
#include "TooltipHook.h"
#include "EngineMarkers.h"
#include "EngineComponents.h"
#include "EngineOps.h"
#include "EngineHighlight.h"
#include <cstdio>
#include <cstdarg>
#include <unordered_map>
#include <unordered_set>
#include <detours.h>
#include "SceneShare.h"
#include "VarcShare.h"
#include "RenderShare.h"
#include "SpecialShare.h"
#include "GroundShare.h"
#include "NetShare.h"       // raw inbound socket bytes
#include "NetProbeShare.h"  // decoded server->client packets from the inbound framer
#include "EventShare.h"
#include "ServerOps.h"
#include "Present.h"
#include "VkPresent.h"
#include "SoundFilter.h"
#include "MenuProbe.h"

namespace {

using rtx::scene::Share;
using rtx::scene::Object;

constexpr int kMaxMgrs = rtx::scene::kMaxObjects;
std::uint64_t g_base = 0;     // game module base
std::uint64_t g_size = 0;

std::mutex g_ringLogMu;
constexpr std::uint64_t kRingLogMaxBytes = 16ull * 1024 * 1024;


void RingLog(const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_ringLogMu);
    char path[MAX_PATH] = "C:\\rtx_ring.log";
    char home[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("USERPROFILE", home, sizeof(home));
    if (n > 0 && n < sizeof(home)) std::snprintf(path, sizeof(path), "%s\\rtx_ring.log", home);
    // Bounded: past kRingLogMaxBytes the file is rolled to <name>.1 and a new
    // one starts, so a long session cannot fill the profile while the previous
    // run stays readable. Every client writes to this one file, so the roll is
    // best effort: if another process has it open the rename fails and the next
    // call tries again.
    {
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (GetFileAttributesExA(path, GetFileExInfoStandard, &fa) &&
            (((std::uint64_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow) >= kRingLogMaxBytes) {
            char prev[MAX_PATH];
            std::snprintf(prev, sizeof(prev), "%s.1", path);
            MoveFileExA(path, prev, MOVEFILE_REPLACE_EXISTING);
        }
    }

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

std::uint64_t g_rootGlobal = 0;   // address of the global; deref for the root
std::uint32_t g_rootMethod = 0;   // 0 = unresolved, 1 = byte anchor, 2 = structural scan

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
        if (scenery >= 4) return true;
    }
    return false;
}

std::uint64_t ScanRootGlobal() {
    const std::uint8_t* img = reinterpret_cast<const std::uint8_t*>(g_base);
    std::uint64_t n = (g_size ? g_size : 0x2000000);
    if (n < 0x1300) return 0;
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
                    break;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
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
    static ULONGLONG s_nextCheck = 0;
    const ULONGLONG now = GetTickCount64();
    if (g_rootGlobal && now >= s_nextCheck) {
        s_nextCheck = now + 2000;
        if (!RootGlobalValid(g_rootGlobal)) { g_rootGlobal = 0; g_rootMethod = 0; }
    }
    if (!g_rootGlobal) g_rootGlobal = ScanRootGlobal();
    return g_rootGlobal ? R64(g_rootGlobal) : 0;
}

constexpr std::uint64_t kSecPtr   = rtx::scn::kSecPtr;    // entity -> object-data (sub)
constexpr std::uint64_t kType     = rtx::scn::kType;      // sub: type byte (0/12 = scenery)
constexpr std::uint64_t kFloor    = rtx::scn::kPlane;     // sub: plane
constexpr std::uint64_t kEntPosX  = 0x30;                 // entity: float X (fine, companion-only)
constexpr std::uint64_t kEntPosY  = 0x38;                 // entity: float Y (fine, companion-only)
constexpr std::uint64_t kVecBegin = rtx::scn::kVecBegin;  // worker: entity vector begin
constexpr std::uint64_t kVecEnd   = rtx::scn::kVecEnd;    // worker: entity vector end

// worldView -> scene-worker offset, runtime-resolved (0x10150 -> 0x10170 on 949); worker thread only.
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
// sub -> config id (0xa8 through 949-5): type 0 stores the loc id inline, type 12 a loc-def pointer (id at def+0x28).
constexpr std::uint64_t kConfigId = 0xb0;

constexpr std::uint64_t kDefLocId = 0x28;

constexpr std::uint64_t kModelLo = 0x28, kModelMid = 0x30, kModelHi = 0x40;   // live-model block, zero unless rendered this frame; 950-1: was 0x18/0x20/0x30

std::int32_t ReadConfig(std::uint64_t sub) {
    std::uint64_t v = R64(sub + kConfigId);
    if (IsHeap(v)) {                                   // def-pointer form
        std::int32_t id = 0;
        if (TryRead(v + kDefLocId, id) && id >= 1 && id <= 200000) return id;
    }
    std::int32_t lo = R32(sub + kConfigId);            // inline form
    return (lo >= 1 && lo <= 200000) ? lo : 0;
}

// Entity world AABB, stored (X=east, Y=up, Z=north).
constexpr std::uint64_t kBoxMinX = 0x40, kBoxMinY = 0x44, kBoxMinZ = 0x48;
constexpr std::uint64_t kBoxMaxX = 0x50, kBoxMaxY = 0x54, kBoxMaxZ = 0x58;

bool IsFin(float v) { return v == v && v > -3.0e38f && v < 3.0e38f; }

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
        o.bmax[0] = o.bmax[1] = o.bmax[2] = 0.f;
    }
}

bool IsScenery(int t) { return t == 0 || t == 12; }

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

constexpr std::uint64_t kEntMgr = 0x130;

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

float g_lastPlayerX = 0.f, g_lastPlayerY = 0.f;
bool  g_havePlayerPos = false;
int   g_posSource = 0;             // 0 = none, 1 = live, 2 = remembered (diag[11])

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

void ScanRegionForManagers(std::uint64_t rbase, std::uint64_t rsize, float px, float py, float rng) {
    __try {
        const float* f = reinterpret_cast<const float*>(rbase);
        std::uint64_t fn = rsize / 4;
        for (std::uint64_t i = 0; i + 3 < fn && g_mgrCount < kMaxMgrs; ++i) {
            float fx = f[i];
            if (fx <= px - rng || fx >= px + rng) continue;
            float fy = f[i + 2];
            if (fy <= py - rng || fy >= py + rng) continue;
            std::uint64_t ent = rbase + i * 4 - kEntPosX;
            if (!IsHeap(ent)) continue;
            std::uint64_t sub = R64(ent + kSecPtr);
            if (!IsHeap(sub) || !IsScenery(R8(sub + kType))) continue;
            if (R64(sub + rtx::scn::kBack) != ent) continue;
            std::uint64_t mgr = R64(ent + kEntMgr);
            int total = 0;
            if (IsHeap(mgr) && ScoreContainer(mgr, total) >= 1) AddMgr(mgr);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

int FindContainersFromTracked();

void FindContainers(bool deep) {
    float px = 0, py = 0;
    if (!PlayerFineOrLast(px, py)) return;
    if (FindContainersFromTracked() > 0 && !deep) return;
    const float rng = 64.f * 512.f;                     // ~64 tiles (>= the panel's max range)
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

void PruneInvalid() {
    int w = 0;
    for (int i = 0; i < g_mgrCount; ++i) {
        std::uint64_t mgr = g_mgrs[i];
        std::uint64_t vb = R64(mgr + kVecBegin), ve = R64(mgr + kVecEnd);
        if (IsHeap(vb) && ve >= vb && ((ve - vb) & 7) == 0 && (ve - vb) / 8 <= 30000)
            g_mgrs[w++] = mgr;
    }
    g_mgrCount = w;
}

// CS2 var ops: registry-entry+0x20 4 = varp, 5 = varc-int. vm_ctx: int stack +0x100, count +0x10a0, var id +0x24 (u16), registry +0x10.

constexpr std::uint64_t kVmVarId = 0x24;

rtx::varc::Share* g_varcShare = nullptr;
typedef void* (*VarOp_t)(std::uint64_t, std::uint64_t);
VarOp_t g_origVarp = nullptr;     // type 4 handler (player var)
VarOp_t g_origVarc = nullptr;     // type 5 handler (client var)

struct VarContext {
    std::uint64_t registry, bucket_table; std::uint32_t bucket_count;
    std::uint64_t inst_table_0; std::uint32_t inst_count_0;
    std::uint64_t inst_table_1; std::uint32_t inst_count_1; std::uint8_t flag20;
    std::uint8_t  scope_type;   // 4 = varp/varbit handler, 5 = varc-int handler
};
typedef void* (__fastcall *GetStorage_t)(void*, void*);

std::mutex g_scopeMu;
std::vector<VarContext> g_verifiedScopes;
std::vector<std::pair<VarContext, std::uint16_t>> g_candidates;  // hot-path snapshots awaiting verify
std::atomic<std::uint64_t> g_lastReg{0};         // last registry the hot path snapshotted

std::atomic<std::uint64_t> g_lastPanelResolveMs{0};
constexpr std::uint64_t    kPanelResolveMinMs = 250;

std::mutex g_varMu;
std::unordered_map<std::uint32_t, std::int32_t>  g_varVal;        // (type<<16)|id -> value (published)
std::unordered_map<std::uint32_t, std::uint64_t> g_storageCache;  // (scope<<16)|id -> storage ptr
std::unordered_map<std::uint32_t, int>           g_walkAttempts;  // (scope<<16)|id -> failed getStorage attempts
static constexpr int kWalkMaxAttempts = 6;

static inline bool CanonPtr(std::uint64_t p) { return p >= 0x10000ull && p <= 0x00007FFFFFFFFFFFull; }

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

// entry -> holder chain -> vtable getStorage(holder, entry+8); 0 on any fault.
std::uint64_t ResolveStorage(std::uint64_t entry, const VarContext& ctx) {
    __try {
        if (*(std::uint8_t*)(entry + 0x20) != 4) return 0;          // type 4 only
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

// var id -> registry entry (bucket hash + chain at entry+0x28).
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

__declspec(noinline) void PushCandidate(const VarContext& ctx, std::uint16_t var_id) {
    std::lock_guard<std::mutex> lk(g_scopeMu);
    for (auto& e : g_verifiedScopes) if (e.registry == ctx.registry) return;
    for (auto& c : g_candidates) if (c.first.registry == ctx.registry) return;
    if (g_candidates.size() < 64) g_candidates.emplace_back(ctx, var_id);
}

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

__declspec(noinline) void ResolveCurrentVarLive(std::uint64_t vm_ctx, std::uint16_t var_id, std::uint8_t scope_type) {
    std::uint32_t key = ((std::uint32_t)scope_type << 16) | var_id;
    {
        std::lock_guard<std::mutex> lk(g_varMu);
        if (g_storageCache.find(key) != g_storageCache.end()) return;
        if (g_walkAttempts[key] >= kWalkMaxAttempts) return;
    }
    VarContext ctx{};
    if (!ReadVarContext(vm_ctx, ctx)) return;
    ctx.scope_type = scope_type;
    std::uint64_t e = FindEntry(ctx, var_id);
    std::uint64_t storage = e ? ResolveStorage(e, ctx) : 0;
    std::lock_guard<std::mutex> lk(g_varMu);
    if (storage) { g_storageCache[key] = storage; g_walkAttempts.erase(key); if (g_varcShare) g_varcShare->diag[3]++; }
    else         { g_walkAttempts[key]++; }
}

std::int32_t ReadIntSafe(std::uint64_t storage);

__declspec(noinline) void ResolvePanelGroupsFromLive(std::uint64_t vm_ctx) {
    VarContext ctx{};
    if (!ReadVarContext(vm_ctx, ctx)) return;
    for (const auto& grp : kPanelGroups) {
        std::uint64_t ex = FindEntry(ctx, grp.x), ey = FindEntry(ctx, grp.y);
        if (!ex || !ey) continue;
        std::uint64_t sx = ResolveStorage(ex, ctx), sy = ResolveStorage(ey, ctx);
        if (!sx || !sy) continue;
        int X = ReadIntSafe(sx), Y = ReadIntSafe(sy);
        if (X < 0 || X >= 6000 || Y < 0 || Y >= 6000) continue;
        std::uint64_t sw = 0, sh = 0;
        if (grp.w || grp.h) {
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

// push_var capture (game thread, thousands/sec). Panel resolver is always on; per-var capture is gated.
inline void* VarOpObserve(VarOp_t orig, std::uint64_t a, std::uint64_t vm_ctx, std::uint8_t scope_type) {
    if (g_varcShare) {
        std::uint64_t reg = 0; std::uint16_t var_id = 0; bool ok = false;
        __try { var_id = *(std::uint16_t*)(vm_ctx + kVmVarId); reg = *(std::uint64_t*)(vm_ctx + 0x10); ok = true; }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        if (ok && reg && reg != g_lastReg.load(std::memory_order_relaxed)) {
            g_lastReg.store(reg, std::memory_order_relaxed);
            std::uint64_t now = GetTickCount64();
            if (now - g_lastPanelResolveMs.load(std::memory_order_relaxed) >= kPanelResolveMinMs) {
                g_lastPanelResolveMs.store(now, std::memory_order_relaxed);
                ResolvePanelGroupsFromLive(vm_ctx);
            }
        }
        if (ok && g_varcShare->enable) ResolveCurrentVarLive(vm_ctx, var_id, scope_type); // id 0 is a real var
    }
    return orig(a, vm_ctx);
}
void* Detour_Varp(std::uint64_t a, std::uint64_t vm_ctx) { if (g_varcShare) g_varcShare->diag[0]++; return VarOpObserve(g_origVarp, a, vm_ctx, 4); }
void* Detour_Varc(std::uint64_t a, std::uint64_t vm_ctx) { if (g_varcShare) g_varcShare->diag[1]++; return VarOpObserve(g_origVarc, a, vm_ctx, 5); }

typedef void* (*CcOp_t)(std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t);
CcOp_t g_origCcDrag = nullptr;

void* Detour_CcIfSetDraggable(std::uint64_t p1, std::uint64_t p2, std::uint64_t p3, std::uint64_t p4) {
    void* r = g_origCcDrag(p1, p2, p3, p4);
    if (g_varcShare) g_varcShare->diag[2]++;
    return r;
}

// Unique body-byte match in .text, mapped back to the true entry via .pdata. Re-derive bytes per build.
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
            if (ok) { if (body_va) return 0; body_va = tb + i; }   // must match exactly once
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

// As FindVarOp; mask[j]==0 wildcards that byte (e.g. a rel32 operand).
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
            if (ok) { if (body_va) return 0; body_va = tb + i; }   // must match exactly once
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
    wchar_t name[rtx::ipc::kNameChars];
    rtx::varc::MakeSectionName(GetCurrentProcessId(), name);
    HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        (DWORD)(sizeof(rtx::varc::Share) >> 32), (DWORD)(sizeof(rtx::varc::Share) & 0xFFFFFFFF), name);
    if (!h) return nullptr;
    return (rtx::varc::Share*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(rtx::varc::Share));
}

rtx::varc::Entry g_varcbuf[rtx::varc::kMaxVars];

struct EnumEntry { std::uint64_t entry; std::uint16_t id; std::uint8_t type; };

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

__declspec(noinline) void DiscoverStorage() {
    std::vector<VarContext> scopes;
    { std::lock_guard<std::mutex> lk(g_scopeMu); scopes = g_verifiedScopes; }
    int live = 0;
    std::vector<EnumEntry> ents;
    for (auto& sc : scopes) {
        if (!ScopeStillLive(sc)) continue;
        ++live;
        ents.clear(); EnumerateScope(sc, ents);
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
    if (live == 0 && !scopes.empty()) {            // all scopes dead (relog / world hop)
        { std::lock_guard<std::mutex> lk(g_scopeMu); g_verifiedScopes.clear(); }
        { std::lock_guard<std::mutex> lk(g_varMu);   g_storageCache.clear(); }
        g_lastReg.store(0, std::memory_order_relaxed);
    }
}

int g_pubTick = 0;
__declspec(noinline) void VerifyCandidates() {
    std::vector<std::pair<VarContext, std::uint16_t>> cands;
    { std::lock_guard<std::mutex> lk(g_scopeMu); cands.swap(g_candidates); }
    for (auto& c : cands) {
        if (!ScopeStillLive(c.first)) continue;
        std::uint64_t entry = FindEntry(c.first, c.second);
        if (!entry) continue;
        if (!ResolveStorage(entry, c.first)) continue;
        std::lock_guard<std::mutex> lk(g_scopeMu);
        bool dup = false;
        for (auto& e : g_verifiedScopes) if (e.registry == c.first.registry && e.bucket_table == c.first.bucket_table) { dup = true; break; }
        if (!dup && g_verifiedScopes.size() < 64) g_verifiedScopes.push_back(c.first);
    }
}
void PublishVarcs(rtx::varc::Share* vsh) {
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
    MemoryBarrier();
    std::memcpy(vsh->entries, g_varcbuf, (std::size_t)c * sizeof(rtx::varc::Entry));
    vsh->count = c;
    vsh->strCount = 0;
    MemoryBarrier();
    vsh->seq++;                                   // even: complete
}

Share* MapShare() {
    wchar_t name[rtx::ipc::kNameChars];
    rtx::scene::MakeSectionName(GetCurrentProcessId(), name);
    HANDLE hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                     (DWORD)(sizeof(Share) >> 32), (DWORD)(sizeof(Share) & 0xFFFFFFFF),
                                     name);
    if (!hMap) return nullptr;
    auto* p = reinterpret_cast<Share*>(MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Share)));
    return p;
}

// Staging buffer: game-memory reads happen outside the seqlock, which is held only for the memcpy.
Object g_pubbuf[rtx::scene::kMaxObjects];

// Where each published object lives, for the hover mark. Written by the walk, read by the render
// thread; the sequence is odd while it is being rewritten.
struct HoverRef { std::uint64_t sub; std::int32_t x, y, id; };
HoverRef g_hoverRefs[rtx::scene::kMaxObjects];
HoverRef g_hoverStage[rtx::scene::kMaxObjects];
std::atomic<std::uint32_t> g_hoverSeq{ 0 };
std::atomic<std::uint32_t> g_hoverCount{ 0 };

std::unordered_set<std::uint64_t> DeadSnapshot();

void Publish(Share* sh, bool wantDiag) {
    std::uint32_t c = 0;
    bool got0 = false, got12 = false;            // dumped a type-0 / type-12 sub yet
    const std::unordered_set<std::uint64_t> dead = DeadSnapshot();
    for (int mi = 0; mi < g_mgrCount && c < rtx::scene::kMaxObjects; ++mi) {
        std::uint64_t mgr = g_mgrs[mi];
        std::uint64_t vb = R64(mgr + kVecBegin), ve = R64(mgr + kVecEnd);
        std::uint64_t n = (IsHeap(vb) && ve > vb) ? (ve - vb) / 8 : 0;
        if (n > 30000) n = 30000;
        for (std::uint64_t i = 0; i < n && c < rtx::scene::kMaxObjects; ++i) {
            std::uint64_t ep = R64(vb + i * 8);
            if (dead.count(ep)) continue;
            int t, tx, ty;
            if (!ValidSceneryEntity(ep, t, tx, ty)) continue;
            std::uint64_t sub = R64(ep + kSecPtr);
            Object& o = g_pubbuf[c++];
            o.config_id = ReadConfig(sub);
            o.x = tx; o.y = ty;
            g_hoverStage[c - 1] = HoverRef{ sub, tx, ty, o.config_id };
            o.plane = (std::int16_t)R32(sub + kFloor);
            o.kind = (std::int16_t)t;
            if (R32(sub + rtx::scn::kLocFlags) & rtx::scn::kLocHidden) o.kind |= rtx::scene::kHiddenBit;   // depleted tree / dormant stump
            ReadBox(ep, o);
            if ((R32(sub + kModelLo) | R32(sub + kModelMid) | R32(sub + kModelHi)) == 0) {
                o.bmin[0] = o.bmin[1] = o.bmin[2] = 0.f;
                o.bmax[0] = o.bmax[1] = o.bmax[2] = 0.f;
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
    sh->seq++;                                    // odd: mid-update
    MemoryBarrier();
    std::memcpy(sh->objects, g_pubbuf, (std::size_t)c * sizeof(Object));
    sh->count = c;
    MemoryBarrier();
    sh->seq++;                                    // even: complete

    g_hoverSeq.fetch_add(1);                      // odd: mid-update
    std::memcpy(g_hoverRefs, g_hoverStage, (std::size_t)c * sizeof(HoverRef));
    g_hoverCount.store(c);
    g_hoverSeq.fetch_add(1);
}

// Display detours skip the draw call; scene blank flips one Jcc byte in the render thread.
rtx::render::Share* g_renderShare = nullptr;
std::atomic<std::uint64_t> g_localPlayerSub{ 0 };

typedef void* (*PlDis_t)(std::uint64_t a1, void* a2, void* a3, void* a4, void* a5, void* a6);
typedef void  (*NpcDis_t)(std::uint64_t a1, std::uint64_t a2, std::uint64_t a3, std::uint64_t a4, std::uint64_t a5, std::uint64_t a6);
PlDis_t  g_origPlDis  = nullptr;
NpcDis_t g_origNpcDis = nullptr;

// NPC actor display vtable method, once per NPC per frame; a1 = actor sub. Visual only.
void Detour_NpcDis(std::uint64_t a1, std::uint64_t a2, std::uint64_t a3, std::uint64_t a4, std::uint64_t a5, std::uint64_t a6) {
    __try {
        if (g_renderShare && g_renderShare->hideNpcs && IsHeap(a1) && R8(a1 + kType) == 1) return;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_origNpcDis(a1, a2, a3, a4, a5, a6);
}
void* Detour_PlDis(std::uint64_t a1, void* a2, void* a3, void* a4, void* a5, void* a6) {
    __try {
        // a1 = player sub; keep the local player.
        if (g_renderShare && g_renderShare->hidePlayers && IsHeap(a1)) {
            std::uint64_t self = g_localPlayerSub.load(std::memory_order_relaxed);
            if (R8(a1 + kType) == 2 && a1 != self) return nullptr;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return g_origPlDis(a1, a2, a3, a4, a5, a6);
}

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
    wchar_t name[rtx::ipc::kNameChars]; rtx::render::MakeSectionName(GetCurrentProcessId(), name);
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
        g_renderShare->keepFocused = 0;
        g_renderShare->installed = 0;
    }
    // npc-display: mov rax,[rcx]; mov rdi,r9; mov rsi,r8; mov rbp,rdx; mov rbx,rcx; call [rax+0x110]
    // player-vis : mov rax,[rcx+0x1078]; mov rbp,r9
    // render-thr : mov rax,[rcx+8]; mov r15,rcx; mov r14,[rip+..]
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
            if (pNpc) g_renderShare->installed |= 1;   // bit0: hide NPCs
            if (pDis) g_renderShare->installed |= 2;   // bit1: hide other players
        }
    }
    std::uint64_t pRender = rtx::scn::KnownBuild(g_base) ? FindVarOp(kRenderBody, sizeof(kRenderBody)) : 0;
    if (pRender) {
        std::uint64_t spot = pRender + 0x266;
        std::uint8_t op = R8(spot);
        if (op == 0x74 || op == 0x75) {     // only ever patch a real Jcc
            g_sceneBlankAddr = spot; g_sceneBlankOrig = op;
            if (g_renderShare) g_renderShare->installed |= 4;
        }
    }
}

rtx::special::Share* g_specialShare = nullptr;
constexpr int kHiRingCap = 128;
rtx::special::Highlight g_hiRing[kHiRingCap] = {};
std::atomic<std::uint32_t> g_hiIdx{ 0 };
std::atomic<bool> g_specialOn{ false };   // worker decays this from the launcher's enable stamp
std::mutex g_renderMu;
std::unordered_set<std::uint64_t> g_renderSet;
std::unordered_set<std::uint64_t> g_deadSet;
constexpr std::size_t kDeadSetCap = 100000;
std::atomic<std::uint64_t> g_SceneWorkerRoot{ 0 };
std::uint64_t g_hookInstallMs = 0;

// Capture points are __try functions: anything with a destructor lives in these helpers (C2712).
static inline void RenderInsert(std::uint64_t p) { std::lock_guard<std::mutex> lk(g_renderMu); g_renderSet.insert(p); }

static inline void SpawnMark(std::uint64_t p) {
    std::lock_guard<std::mutex> lk(g_renderMu);
    g_renderSet.insert(p);
    g_deadSet.erase(p);
}
static inline void DeadMark(std::uint64_t p) {
    std::lock_guard<std::mutex> lk(g_renderMu);
    g_renderSet.erase(p);
    if (g_deadSet.size() >= kDeadSetCap) g_deadSet.clear();
    g_deadSet.insert(p);
}

std::unordered_set<std::uint64_t> DeadSnapshot() {
    std::lock_guard<std::mutex> lk(g_renderMu);
    return g_deadSet;
}

int FindContainersFromTracked() {
    std::vector<std::uint64_t> snap;
    { std::lock_guard<std::mutex> lk(g_renderMu); snap.assign(g_renderSet.begin(), g_renderSet.end()); }
    int added = 0;
    for (std::uint64_t ent : snap) {
        if (!IsHeap(ent)) continue;
        std::uint64_t sub = R64(ent + kSecPtr);
        if (!IsHeap(sub) || !IsScenery(R8(sub + kType))) continue;
        if (R64(sub + rtx::scn::kBack) != ent) continue;
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

// Object-submit capture (rs2client+0x50A8B0); fires before the entity's sub is linked, so record every a2.
void* Detour_ObjSubmit(std::uint64_t a1, std::uint64_t a2) {
    __try {
        if (a1 > 0xfffff && a1 < g_base) { std::uint64_t e = 0; g_SceneWorkerRoot.compare_exchange_strong(e, a1); }
        if (a2 > 0xfffff && a2 < g_base) SpawnMark(a2);
        if (g_specialOn.load(std::memory_order_relaxed) && g_specialShare) g_specialShare->diag[0]++;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return g_origObjSubmit(a1, a2);
}

// Per-type display hooks, reached only via each type's vtable, so the arrival IS that type: type 4 -> vtable 0xB73A50 slot 7 -> 0x326690 (graphic highlights), type 13 -> vtable 0xB5E878 slot 7 -> 0x1ABBB0 (world markers).
// rcx = sub. Callees read stack args 5 and 6 (type 13 @0x1ABBE4/0x1ABCC3, type 4 @0x3266FB/0x32675F), so the detour forwards >= 6 args; 8 are declared. No xmm args. Render thread, per entity per frame.
typedef void* (*Display_t)(std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t,
                           std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t);
Display_t g_origT4Display = nullptr;
Display_t g_origT13Display = nullptr;

static inline void RecordDisplay(std::uint64_t sub, int type) {
    if (sub <= 0xfffff || sub >= g_base) return;
    auto& s = g_hiRing[g_hiIdx.fetch_add(1, std::memory_order_relaxed) % kHiRingCap];
    s.gfx   = (type == 4) ? R32(sub + 0x74) : -1;    // +0x74 is a gfx id for type 4 only
    s.uid   = R32(sub + 0x88);
    s.plane = (std::int16_t)R32(sub + kFloor);
    s.type  = (std::int16_t)type;
    std::uint64_t ent = R64(sub + rtx::scn::kBack);
    float nx = 0.f, ny = 0.f;
    if (ent > 0xfffff && ent < g_base) { nx = RF(ent + kEntPosX); ny = RF(ent + kEntPosY); }
    s.x = (nx > 0.f && nx < 1e9f) ? (std::int32_t)(nx / 512.f) : 0;
    s.y = (ny > 0.f && ny < 1e9f) ? (std::int32_t)(ny / 512.f) : 0;
    s.kind = rtx::special::kKindUnknown;
    if (type == 13) {
        // Clue-scan marker stores its own tile as a fine destination at sub+0x74/+0x7C; walk marker leaves 0/NaN.
        float dx = RF(sub + 0x74), dy = RF(sub + 0x7C);
        bool hasDest = dx > 0.f && dx < 1e9f && dy > 0.f && dy < 1e9f;
        s.kind = (hasDest && (std::int32_t)(dx / 512.f) == s.x && (std::int32_t)(dy / 512.f) == s.y)
                 ? rtx::special::kKindScan : rtx::special::kKindDest;
    } else if (type == 4) {
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

// Object-delete capture (rs2client+0x50AA40).
void* Detour_ObjDel(std::uint64_t a1, std::uint64_t a2) {
    __try {
        if (a2 > 0xfffff && a2 < g_base) DeadMark(a2);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return g_origObjDel(a1, a2);
}

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

// Re-read one tracked entity live; record it if it holds the scan ring (type 4, gfx 6841..6843).
static void ScanOneEnt(std::uint64_t ent) {
    __try {
        std::uint64_t sub = R64(ent + kSecPtr);
        if (!IsHeap(sub)) return;
        int type = R8(sub + kType);
        if (g_specialShare) g_specialShare->diag[4] |= (1u << (type & 31));
        if (type != 4) return;
        int gfx = R32(sub + 0x74);                                 // proj_otherId
        if (g_specialShare) {
            g_specialShare->diag[1]++;
            std::uint32_t prev = g_specialShare->diag[2];
            if ((gfx >= 6841 && gfx <= 6843) || !(prev >= 6841 && prev <= 6843))
                g_specialShare->diag[2] = (std::uint32_t)gfx;      // prefer a ring gfx
        }
        if (gfx < 6841 || gfx > 6843) return;
        auto& s = g_hiRing[g_hiIdx.fetch_add(1, std::memory_order_relaxed) % kHiRingCap];
        s.gfx   = gfx;
        s.uid   = R32(sub + 0x88);
        // Position from the scene node floats, not sub+0x7c/+0x80 (those are projection fields).
        {
            float nx = RF(ent + kEntPosX), ny = RF(ent + kEntPosY);
            s.x = (nx > 0.f && nx < 1e9f) ? (std::int32_t)(nx / 512.f) : 0;
            s.y = (ny > 0.f && ny < 1e9f) ? (std::int32_t)(ny / 512.f) : 0;
        }
        s.plane = (std::int16_t)R8(sub + kFloor);
        s.stamp = (std::uint32_t)GetTickCount64();
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// diag[1]/diag[4] reset each pass; diag[2] holds the live ring gfx if present.
void ScanTrackedForRing() {
    if (g_specialShare) { g_specialShare->diag[1] = 0; g_specialShare->diag[4] = 0; }
    std::vector<std::uint64_t> snap;
    { std::lock_guard<std::mutex> lk(g_renderMu); snap.assign(g_renderSet.begin(), g_renderSet.end()); }
    for (std::uint64_t ent : snap) if (IsHeap(ent)) ScanOneEnt(ent);
    if (g_specialShare) g_specialShare->diag[6] = (std::uint32_t)snap.size();
}

rtx::special::Share* MapSpecialShare() {
    wchar_t name[rtx::ipc::kNameChars]; rtx::special::MakeSectionName(GetCurrentProcessId(), name);
    HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                  sizeof(rtx::special::Share), name);
    if (!h) return nullptr;
    return (rtx::special::Share*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(rtx::special::Share));
}

// ===== Ground items (scene entity type 3), sourced from g_renderSet, not g_mgrs.
// OBJG stack sub-array: sub+0x70/0x78 = [begin,end), 0x90 stride, item id at element+0.
rtx::ground::Share* g_groundShare = nullptr;

rtx::ground::Share* MapGroundShare() {
    wchar_t name[rtx::ipc::kNameChars]; rtx::ground::MakeSectionName(GetCurrentProcessId(), name);
    HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                  sizeof(rtx::ground::Share), name);
    if (!h) return nullptr;
    return (rtx::ground::Share*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(rtx::ground::Share));
}

static rtx::ground::Item g_groundBuf[rtx::ground::kMaxItems];

// Appends this entity's item stacks to g_groundBuf; returns the new count. Own function for C2712.
static std::uint32_t ScanGroundEnt(std::uint64_t ent, std::uint32_t c) {
    __try {
        if (!IsHeap(ent)) return c;
        std::uint64_t sub = R64(ent + kSecPtr);
        if (!IsHeap(sub)) return c;
        if (R8(sub + kType) != 3) return c;
        float ex = (RF(ent + kBoxMinX) + RF(ent + kBoxMaxX)) * 0.5f;   // east centre
        float nz = (RF(ent + kBoxMinZ) + RF(ent + kBoxMaxZ)) * 0.5f;   // north centre
        int tx = (int)(ex / 512.f), ty = (int)(nz / 512.f);
        if (tx <= 0 || ty <= 0 || tx > 16384 || ty > 16384) {
            float fx = RF(ent + kEntPosX), fy = RF(ent + kEntPosY);
            tx = (int)(fx / 512.f); ty = (int)(fy / 512.f);
            if (tx <= 0 || ty <= 0 || tx > 16384 || ty > 16384) return c;
        }
        std::int32_t plane = R32(sub + kFloor);
        std::uint64_t beg = R64(sub + 0x70), end = R64(sub + 0x78);
        if (!IsHeap(beg) || end <= beg) return c;
        std::uint64_t stacks = (end - beg) / 0x90;
        if (stacks == 0 || stacks > 256) return c;
        for (std::uint64_t s = 0; s < stacks && c < (std::uint32_t)rtx::ground::kMaxItems; ++s) {
            std::int32_t id = R32(beg + s * 0x90);
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
    MemoryBarrier();
    std::memcpy(sh->items, g_groundBuf, (std::size_t)c * sizeof(rtx::ground::Item));
    sh->count = c;
    MemoryBarrier();
    sh->seq++;                                    // even: complete
}

rtx::net::Share* g_netShare = nullptr;

typedef int (WSAAPI* Recv_t)(SOCKET, char*, int, int);
typedef int (WSAAPI* WSARecv_t)(SOCKET, LPWSABUF, DWORD, LPDWORD, LPDWORD,
                                LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
static Recv_t    g_origRecv    = nullptr;
static WSARecv_t g_origWsaRecv = nullptr;

// written is published last so a reader observing N can trust records [0, N).
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
    rtx::net::MakeSectionName(GetCurrentProcessId(), name);
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
    g_netShare->enable  = 1;
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

// Hooked at the inbound framer FUN_1400ff0c0, which ISAAC-deciphers the opcode and looks it up in the packet table (rs2client+0xC70BB0 on 950-1, entries 0..0xDE).
// Connection object: +0x2C int opcode (-1 = none), +0x30 int length, +0x2D0 payload ptr, +0x2E8 cumulative inbound byte counter.
constexpr int kOpMessageGame = rtx::sops::kMessageGame;
rtx::netprobe::Share* g_netProbeShare = nullptr;
rtx::events::Share*   g_eventShare    = nullptr;
typedef std::uint64_t* (*Framer_t)(std::uint64_t conn, std::uint64_t* out);
static Framer_t g_origFramer = nullptr;

static void EventRecord(std::int32_t op, std::int32_t len, const std::uint8_t* p) {
    auto* ev = g_eventShare;
    if (!ev) return;
    ev->inbound++;
    if (op == kOpMessageGame) return;                         // chat ring owns message_game
    if (!((ev->mask[(op >> 5) & 7] >> (op & 31)) & 1u)) return;
    const std::uint64_t i = ev->written;
    rtx::events::Record& r = ev->recs[i % rtx::events::kMaxRecords];
    const std::uint32_t pub = (std::uint32_t)((i + 1) * 2);
    r.seq = pub | 1u;                                         // odd: body in flux
    MemoryBarrier();
    r.tick   = GetTickCount();
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    const std::uint64_t ft64 = ((std::uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    r.wallMs = (std::uint32_t)((ft64 - 116444736000000000ULL) / 10000ULL);
    r.opcode = op;
    r.length = len;
    if (p && len > 0) {
        const std::uint32_t kept =
            (std::uint32_t)(len < rtx::events::kPayload ? len : rtx::events::kPayload);
        std::memcpy(r.payload, p, kept);
    }
    if (len > rtx::events::kPayload) ev->truncated++;
    MemoryBarrier();
    r.seq = pub;                                              // even: published
    MemoryBarrier();
    ev->written = i + 1;
}

static void NetProbeRecord(std::uint64_t conn, std::uint64_t* out) {
    auto* sh = g_netProbeShare;
    if (!sh) return;
    if (!out || out[0] == 0) return;
    const std::uint32_t now = (std::uint32_t)GetTickCount64();
    const bool armed = sh->enable != 0 && (std::uint32_t)(now - sh->enable) <= 3000;
    __try {
        const std::int32_t op = *(const std::int32_t*)(conn + 0x2c);
        if (op < 0 || op > rtx::sops::kOpMax) { if (armed) sh->diag[2]++; return; }
        const std::int32_t  len = *(const std::int32_t*)(conn + 0x30);
        const std::uint32_t rx  = *(const std::uint32_t*)(conn + 0x2e8);
        static std::uint32_t s_lastRx = 0xFFFFFFFFu; static std::int32_t s_lastOp = -1;
        if (rx == s_lastRx && op == s_lastOp) { if (armed) sh->diag[3]++; return; }
        s_lastRx = rx; s_lastOp = op;
        const std::uint8_t* p =
            len > 0 ? *(const std::uint8_t* const*)(conn + 0x2d0) : nullptr;

        EventRecord(op, len, p);

        if (op == kOpMessageGame) {
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
            ++sh->chatWritten;                                // publish last
        }

        if (!armed) return;
        sh->diag[0]++;
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
        ++sh->written;                                        // publish last
        sh->diag[1]++;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static std::uint64_t* Detour_Framer(std::uint64_t conn, std::uint64_t* out) {
    std::uint64_t* r = g_origFramer(conn, out);
    NetProbeRecord(conn, out);
    return r;
}

static rtx::events::Share* MapEventShare() {
    wchar_t name[128];
    rtx::events::MakeSectionName(GetCurrentProcessId(), name);
    HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                  sizeof(rtx::events::Share), name);
    if (!h) return nullptr;
    return (rtx::events::Share*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0,
                                              sizeof(rtx::events::Share));
}

static rtx::netprobe::Share* MapNetProbeShare() {
    wchar_t name[rtx::ipc::kNameChars];
    rtx::netprobe::MakeSectionName(GetCurrentProcessId(), name);
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
    g_eventShare = MapEventShare();
    if (g_eventShare) {
        auto* ev = g_eventShare;
        if (ev->magic != rtx::events::kMagic || ev->version != rtx::events::kVersion || !ev->maskSet)
            for (int i = 0; i < 8; ++i) ev->mask[i] = rtx::events::kDefaultMask[i];
        ev->magic = rtx::events::kMagic; ev->version = rtx::events::kVersion;
        ev->pid = GetCurrentProcessId(); ev->flags = 0;
        ev->written = 0; ev->truncated = 0; ev->inbound = 0;
    } else {
        RingLog("events: share map FAILED");
    }
    // Framer entry prologue; the `test rcx,rcx; jz; cmp dword[rcx],2` tail makes it unique, jz rel32 wildcarded.
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
        if (g_eventShare) g_eventShare->flags |= 1;
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
    // Object-submit fn: mov rcx,rdx; mov r8d,0x47; mov r14,rdx; call <rel32>; mov rcx,[rbx+0x140]; cmp rcx,[rbx+0x148]
    static const unsigned char body[] = {
        0x00,0x00,0x48,0x8B,0xCA,0x41,0xB8,0x47,0x00,0x00,0x00,0x4C,0x8B,0xF2,0xE8,
        0x00,0x00,0x00,0x00,                                          // CALL rel32 (wildcard)
        0x48,0x8B,0x8B,0x40,0x01,0x00,0x00,0x48,0x3B,0x8B,0x48,0x01,0x00 };
    static const unsigned char mask[] = {
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        0,0,0,0,
        1,1,1,1,1,1,1,1,1,1,1,1,1 };
    std::uint64_t p = FindVarOpWild(body, mask, sizeof(body));
    if (p) {
        if (g_specialShare) g_specialShare->diag[5] = (std::uint32_t)(p - g_base);   // resolved fn RVA
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        g_origObjSubmit = (ObjSubmit_t)p;
        DetourAttach(&(PVOID&)g_origObjSubmit, (PVOID)Detour_ObjSubmit);
        if (DetourTransactionCommit() == NO_ERROR && g_specialShare) { g_specialShare->flags |= 1; g_specialShare->diag[3] = 1; g_hookInstallMs = GetTickCount64(); RingLog("hook: spawn-hook ATTACHED rva=0x%llx", (unsigned long long)(p - g_base)); }
        else RingLog("hook: spawn-hook attach FAILED");
    }

    // Object-delete fn (rs2client+0x50AA40): prologue + rcx+0x140 / rdi+0x138 worker-vec compare.
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

    // Per-type display hooks (vtable slot 7). Build 940: type-4 0x326690 == *(0xB73A50 + 7*8),
    // type-13 0x1ABBB0 == *(0xB5E878 + 7*8). The type-4 pattern starts mid-function (0x32669D).
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
    g_specialOn.store(sh->enable != 0 && (std::uint32_t)(now - sh->enable) < 3000, std::memory_order_relaxed);
    if (!g_specialOn.load(std::memory_order_relaxed)) { sh->seq++; MemoryBarrier(); sh->count = 0; MemoryBarrier(); sh->seq++; return; }
    rtx::special::Highlight out[rtx::special::kMaxHighlights];
    std::uint32_t c = 0;
    for (int pass = 0; pass < 2; ++pass)
    for (int i = 0; i < kHiRingCap && c < (std::uint32_t)rtx::special::kMaxHighlights; ++i) {
        rtx::special::Highlight hh = g_hiRing[i];                 // racy POD copy, range-guarded
        if (hh.stamp == 0 || (std::uint32_t)(now - hh.stamp) > rtx::special::kStaleMs) continue;
        if ((hh.kind == rtx::special::kKindAdorn) != (pass == 1)) continue;
        bool dup = false;
        for (std::uint32_t j = 0; j < c; ++j) {
            bool same = hh.uid ? (out[j].uid == hh.uid)
                              : (out[j].uid == 0 && out[j].type == hh.type && out[j].gfx == hh.gfx &&
                                 out[j].x == hh.x && out[j].y == hh.y && out[j].plane == hh.plane);
            if (same) { if (hh.stamp > out[j].stamp) out[j] = hh; dup = true; break; }
        }
        if (!dup) out[c++] = hh;
    }
    sh->seq++;                                    // odd: mid-update
    MemoryBarrier();
    std::memcpy(sh->items, out, (std::size_t)c * sizeof(rtx::special::Highlight));
    sh->count = c;
    MemoryBarrier();
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

// ---- Session rebind --------------------------------------------------
// The channels this module publishes are created once while the hooks go in.
// When the launcher issues a new session the names change, so each one is
// re-created here and its header re-initialised. Called from the worker loop,
// where it costs a pointer test per tick until a session actually changes.
//
// Old views are left mapped. Detours on other threads write through these
// pointers, and a rebind cannot know when they are between instructions, so
// the previous view stays valid for the life of the process. The cost is a few
// kilobytes per rebind, and a rebind only happens when the launcher restarts.
//
// State that came from installing the hooks is carried across, because the
// hooks are installed once and are deliberately not touched here.
std::uint32_t g_varcFlagsSticky      = 0;
std::uint32_t g_renderInstalledStick = 0;
std::uint32_t g_eventFlagsSticky     = 0;
std::uint32_t g_netProbeFlagsSticky  = 0;
std::uint32_t g_netProbeFramerRva    = 0;

void EnsureProducers(Share*& sh) {
    static std::uint32_t s_gen = 0;
    if (rtx::ipc::SessionChanged(s_gen)) {
        if (g_varcShare)   g_varcFlagsSticky      = g_varcShare->flags;
        if (g_renderShare) g_renderInstalledStick = g_renderShare->installed;
        if (g_eventShare)  g_eventFlagsSticky     = g_eventShare->flags;
        if (g_netProbeShare) {
            g_netProbeFlagsSticky = g_netProbeShare->flags;
            g_netProbeFramerRva   = g_netProbeShare->framerRva;
        }

        sh                = nullptr;
        g_groundShare     = nullptr;
        g_varcShare       = nullptr;
        g_renderShare     = nullptr;
        g_specialShare    = nullptr;
        g_eventShare      = nullptr;
        g_netProbeShare   = nullptr;
        RingLog("session: producer channels rebinding");
    }

    const std::uint32_t pid = GetCurrentProcessId();

    if (!sh) {
        sh = MapShare();
        if (sh) {
            sh->magic = rtx::scene::kMagic;
            sh->version = rtx::scene::kVersion;
            sh->pid = pid;
            sh->count = 0; sh->diag_len = 0; sh->seq = 0;
        }
    }

    if (!g_groundShare) {
        g_groundShare = MapGroundShare();
        if (g_groundShare) {
            g_groundShare->magic = rtx::ground::kMagic; g_groundShare->version = rtx::ground::kVersion;
            g_groundShare->pid = pid;
            g_groundShare->count = 0; g_groundShare->seq = 0; g_groundShare->flags = 0;
        }
    }

    if (!g_varcShare) {
        g_varcShare = MapVarcShare();
        if (g_varcShare) {
            g_varcShare->magic = rtx::varc::kMagic; g_varcShare->version = rtx::varc::kVersion;
            g_varcShare->pid = pid; g_varcShare->count = 0; g_varcShare->strCount = 0;
            g_varcShare->enable = 0; g_varcShare->seq = 0;
            g_varcShare->flags = g_varcFlagsSticky;      // observers are still attached
            for (int i = 0; i < 8; ++i) g_varcShare->diag[i] = 0;
        }
    }

    if (!g_renderShare) {
        g_renderShare = MapRenderShare();
        if (g_renderShare) {
            g_renderShare->magic = rtx::render::kMagic; g_renderShare->version = rtx::render::kVersion;
            g_renderShare->pid = pid;
            g_renderShare->hideNpcs = 0; g_renderShare->hidePlayers = 0; g_renderShare->hideAll = 0;
            g_renderShare->keepFocused = 0;
            g_renderShare->installed = g_renderInstalledStick;   // hooks are still attached
        }
    }

    if (!g_specialShare) {
        g_specialShare = MapSpecialShare();
        if (g_specialShare) {
            g_specialShare->magic = rtx::special::kMagic; g_specialShare->version = rtx::special::kVersion;
            g_specialShare->pid = pid;
            g_specialShare->enable = 0; g_specialShare->count = 0; g_specialShare->seq = 0; g_specialShare->flags = 0;
            for (int i = 0; i < 12; ++i) g_specialShare->diag[i] = 0;
        }
    }

    if (!g_eventShare) {
        g_eventShare = MapEventShare();
        if (g_eventShare) {
            auto* ev = g_eventShare;
            if (ev->magic != rtx::events::kMagic || ev->version != rtx::events::kVersion || !ev->maskSet)
                for (int i = 0; i < 8; ++i) ev->mask[i] = rtx::events::kDefaultMask[i];
            ev->magic = rtx::events::kMagic; ev->version = rtx::events::kVersion;
            ev->pid = pid;
            ev->flags = g_eventFlagsSticky;      // framer hook is still attached
            ev->written = 0; ev->truncated = 0; ev->inbound = 0;
        }
    }

    if (!g_netProbeShare) {
        g_netProbeShare = MapNetProbeShare();
        if (g_netProbeShare) {
            auto* np = g_netProbeShare;
            np->magic = rtx::netprobe::kMagic; np->version = rtx::netprobe::kVersion;
            np->pid = pid; np->enable = 0; np->written = 0; np->seen = 0;
            np->flags = g_netProbeFlagsSticky;   // framer hook is still attached
            np->framerRva = g_netProbeFramerRva;
            np->chatWritten = 0; np->chatSeen = 0;
            for (int i = 0; i < 8; ++i) np->diag[i] = 0;
        }
    }

    rtx::menuprobe::Rebind();
    rtx::soundfilter::Rebind();
}

DWORD WINAPI Worker(LPVOID) {
    HMODULE gm = GetModuleHandleW(L"rs2client.exe");
    if (!gm) return 0;
    g_base = (std::uint64_t)gm;
    RingLog("=== BOOT: companion worker thread running (DLL loaded), base=0x%llx ===", (unsigned long long)g_base);
    {
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

    g_groundShare = MapGroundShare();
    if (g_groundShare) {
        g_groundShare->magic = rtx::ground::kMagic; g_groundShare->version = rtx::ground::kVersion;
        g_groundShare->pid = GetCurrentProcessId();
        g_groundShare->count = 0; g_groundShare->seq = 0; g_groundShare->flags = 0;
    }

    // Client-variable values: observe the varp and varc-int op handlers. flags bit0 = observers installed.
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
        std::uint64_t pVarp = FindVarOp(kVarpBody, sizeof(kVarpBody));
        std::uint64_t pVarc = FindVarOp(kVarcBody, sizeof(kVarcBody));
        if (pVarp || pVarc) {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            if (pVarp) { g_origVarp = (VarOp_t)pVarp; DetourAttach(&(PVOID&)g_origVarp, (PVOID)Detour_Varp); }
            if (pVarc) { g_origVarc = (VarOp_t)pVarc; DetourAttach(&(PVOID&)g_origVarc, (PVOID)Detour_Varc); }
            if (DetourTransactionCommit() == NO_ERROR) g_varcShare->flags = 1;
        }
        // cc_if_setdraggable body: add dword [r9+0x10A0],-2; mov rbp,rcx; mov eax,[r9+..]
        static const unsigned char kCcDragBody[] = {0x41,0x83,0x81,0xA0,0x10,0x00,0x00,0xFE,0x48,0x8B,0xE9,0x41,0x8B,0x81};
        std::uint64_t pCc = FindVarOp(kCcDragBody, sizeof(kCcDragBody));
        if (pCc) {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            g_origCcDrag = (CcOp_t)pCc; DetourAttach(&(PVOID&)g_origCcDrag, (PVOID)Detour_CcIfSetDraggable);
            if (DetourTransactionCommit() == NO_ERROR) g_varcShare->flags |= 2;   // bit1 = cc_if_setdraggable observed
        }
    }

    ResolveRenderHooks();

    ResolveSpecialObserver();

    ResolveNetProbe();

    // ResolveNetCapture();

    {
        // installed first, described second: as two arguments of one call the order was not fixed
        const bool up = rtx::present::Install();
        RingLog(up ? "compositor: %s" : "compositor: present entry not found (off)", rtx::present::Mode());
    }

    RingLog(rtx::soundfilter::Install() ? "sound: mix hook ATTACHED"
                                        : "sound: mix fn not found (observation/mute off)");

    // Menu probe dump only runs with RTX_MENU_PROBE=1 set.
    RingLog(rtx::menuprobe::Install() ? "menu: probe installed"
                                      : "menu: string-init pattern not found");
    RingLog(rtx::tooltip::Install() ? "tooltip: text hook installed"
                                    : "tooltip: hover entry op not recognised (off)");
    RingLog(rtx::enginemark::Install() ? "markers: game arrow and tile routines found"
                                       : "markers: game arrow and tile routines not recognised (off)");

    constexpr ULONGLONG kRescanMs = 12000;               // base deep-sweep period
    constexpr ULONGLONG kRescanMaxMs = 300000;           // backoff ceiling (5 min)
    ULONGLONG lastScanMs = 0, rescanMs = kRescanMs;
    float scanPx = 0, scanPy = 0; bool haveScanPos = false;
    int emptyTicks = 0;
    for (;;) {
        { char said[400]; if (rtx::enginemark::TakeLog(said, sizeof(said))) RingLog("%s", said); }
        { char said[400]; if (rtx::enginecc::TakeLog(said, sizeof(said))) RingLog("%s", said); }
        { char said[400]; if (rtx::engineops::TakeLog(said, sizeof(said))) RingLog("%s", said); }
        { char said[600]; if (rtx::enginehl::TakeLog(said, sizeof(said))) RingLog("%s", said); }
        EnsureProducers(sh);
        if (!sh) { Sleep(250); continue; }
        float cpx = 0, cpy = 0;
        bool havePos = PlayerFineOrLast(cpx, cpy);
        PruneInvalid();
        rtx::menuprobe::Poll();
        rtx::present::Poll();
        const float kMoveArm = 32.f * 512.f;
        const bool moved = havePos && haveScanPos &&
            (std::fabs(cpx - scanPx) > kMoveArm || std::fabs(cpy - scanPy) > kMoveArm);
        bool stale = moved || GetTickCount64() - lastScanMs > rescanMs;
        if (havePos) FindContainersFromTracked();
        if (havePos && (g_mgrCount == 0 || stale)) {
            const int before = g_mgrCount;
            FindContainers(true);
            lastScanMs = GetTickCount64();
            scanPx = cpx; scanPy = cpy; haveScanPos = true;
            if (g_mgrCount > before || moved) rescanMs = kRescanMs;
            else rescanMs = rescanMs >= kRescanMaxMs ? kRescanMaxMs : rescanMs * 2;
        }
        if (g_mgrCount > 0) {
            Publish(sh, sh->diag_len < 0x260);           // keep dumping until a sub of each type captured
            emptyTicks = 0;
        } else if (++emptyTicks >= 8) {
            sh->seq++; MemoryBarrier(); sh->count = 0; MemoryBarrier(); sh->seq++;
        }
        if (g_varcShare) PublishVarcs(g_varcShare);
        std::uint64_t root = g_SceneWorkerRoot.load(std::memory_order_relaxed);
        int entN = WalkVecTrack(root);
        if (g_specialShare) {
            g_specialShare->diag[7] = (std::uint32_t)entN;
            g_specialShare->diag[8] = g_hookInstallMs ? (std::uint32_t)((GetTickCount64() - g_hookInstallMs) / 1000) : 0;
            g_specialShare->diag[9]  = g_rootMethod;                  // 0 none / 1 anchor / 2 structural
            g_specialShare->diag[10] = (std::uint32_t)g_mgrCount;
            g_specialShare->diag[11] = (std::uint32_t)g_posSource;    // 0 none / 1 live / 2 remembered
        }
        if (g_specialOn.load(std::memory_order_relaxed)) ScanTrackedForRing();
        if (g_specialShare) PublishSpecials(g_specialShare);
        if (g_groundShare) PublishGround(g_groundShare);
        {   // heartbeat
            static int s_logCnt = 0;
            if (g_specialShare && (++s_logCnt % 120) == 0)      // ~30 s
                RingLog("diag: uptime=%us armed=%d fires=%u tracked=%u type4=%u gfx=%u workervec=%u root=0x%llx",
                        g_specialShare->diag[8], g_specialOn.load(std::memory_order_relaxed) ? 1 : 0,
                        g_specialShare->diag[0], g_specialShare->diag[6], g_specialShare->diag[1],
                        g_specialShare->diag[2], g_specialShare->diag[7], (unsigned long long)root);
        }

        if (g_renderShare) {
            std::uint64_t lps = FindLocalPlayerSub();
            if (lps) g_localPlayerSub.store(lps, std::memory_order_relaxed);
            bool wantBlank = g_renderShare->hideAll != 0;
            if (wantBlank != g_sceneBlankPatched) SceneBlankSet(wantBlank);
            rtx::vkpresent::SetHideScene(wantBlank);
            if (rtx::vkpresent::HideSceneAvailable()) g_renderShare->installed |= 4;
        }
        Sleep(250);
    }
}

}  // namespace

// The game keeps the same small interface on everything it can outline: slot 31 of the object's
// method table is "hovered this frame", taking the frame number the highlight settings count in.
// The game calls it for what is under the cursor, except for scenery whose definition opts out,
// and that is the call made here. The method is checked by how it starts before it is trusted:
//   mov rax, [rcx+18h] ; test rax, rax ; je ; mov rax, [rax+130h]
namespace {
constexpr std::uint64_t kHoverSlot = 0xF8;
constexpr std::uint64_t kHlOwner = 0x60, kHlSettings = 0x20, kHlFrame = 0xBC;
constexpr std::uint8_t  kHoverProlog[] = { 0x48, 0x8B, 0x41, 0x18, 0x48, 0x85, 0xC0, 0x74, 0x1F,
                                           0x48, 0x8B, 0x80, 0x30, 0x01, 0x00, 0x00 };

// The game's own sequence for something under the cursor: "hovered" once, which also starts the
// short pulse a fresh hover gets, then the plain setter every frame to keep it lit. Repeating
// "hovered" instead holds the pulse at its start, and it then plays as a flash when the cursor
// leaves. Both methods begin the same way, which is what is checked before either is called.
constexpr std::uint64_t kKeepSlot = 0x100;
constexpr std::uint64_t kHlId = 0x10C;

bool LooksLikeHoverMethod(std::uint64_t fn) {
    if (!InModule(fn)) return false;
    for (std::size_t i = 0; i < sizeof(kHoverProlog); ++i)
        if (R8(fn + i) != kHoverProlog[i]) return false;
    return true;
}

// The "hovered" method does two things: it stamps the object with the frame number, which is what
// keeps the outline lit, and it restarts the outline's pulse (a time stamp at +0x110, the bright
// start of the highlight). The plain setter only stamps the frame. The game calls "hovered" every
// frame while a right-click menu is open on the object, which holds the pulse at its bright start
// for as long as the menu is up, and lets it play out as a flash once the menu closes. Here a
// "hovered" call on an object that was lit in the previous frame is turned into the plain setter,
// so a highlight that merely continues keeps its pulse where it is. A fresh hover still pulses.
using HoverFn = void (*)(std::uint64_t, std::int32_t);
HoverFn g_realHovered = nullptr;
std::atomic<bool> g_pulseHold{ false };
void HookHovered(std::uint64_t obj, std::int32_t frame) {
    if (g_pulseHold.load(std::memory_order_relaxed)) {
        __try {
            const std::int32_t id = R32(obj + kHlId);
            if (id == frame || id == frame - 1) {
                const std::uint64_t vt = R64(obj);
                const std::uint64_t keep = InModule(vt) ? R64(vt + kKeepSlot) : 0;
                if (LooksLikeHoverMethod(keep)) { reinterpret_cast<HoverFn>(keep)(obj, frame); return; }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    g_realHovered(obj, frame);
}
void InstallPulseHold(std::uint64_t hover) {
    static bool s_tried = false;
    if (s_tried) return;
    s_tried = true;
    g_realHovered = reinterpret_cast<HoverFn>(hover);
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)g_realHovered, (PVOID)HookHovered);
    const LONG rc = DetourTransactionCommit();
    RingLog("hover pulse: %s (method at exe+%llx)", rc == NO_ERROR ? "a continuing highlight keeps its pulse" : "hook failed", (unsigned long long)(hover - g_base));
    if (rc != NO_ERROR) g_realHovered = nullptr;
}

bool MarkSub(std::uint64_t sub) {
    static std::uint64_t s_sub = 0;        // what this marked last, and with which frame number
    static std::int32_t  s_frame = 0;
    static std::uint32_t s_said = 0;
    __try {
        if (!IsScenery(R8(sub + kType))) return false;            // freed and reused since the walk
        const std::uint64_t vt = R64(sub);
        if (!InModule(vt)) return false;
        const std::uint64_t hover = R64(vt + kHoverSlot), keep = R64(vt + kKeepSlot);
        if (!LooksLikeHoverMethod(hover) || !LooksLikeHoverMethod(keep)) return false;
        InstallPulseHold(hover);
        const std::uint64_t owner = R64(sub + kHlOwner);
        const std::uint64_t settings = IsHeap(owner) ? R64(owner + kHlSettings) : 0;
        if (!IsHeap(settings)) return false;
        const std::int32_t frame = R32(settings + kHlFrame);
        if (frame <= 0) return false;

        const std::int32_t id = R32(sub + kHlId);
        const bool fresh = id < frame - 1 || id > frame;             // nobody marked it lately
        // this did, last time round. A few frames may have gone by without a mark (a present that
        // was skipped, a stall): that is still the same hover, and saying "hovered" again would
        // start the pulse over, which shows as the outline flashing white.
        const bool ours = sub == s_sub && id == s_frame && frame - id <= 30;
        int path;
        if (id == frame && ours) {
            path = 0;                                                // already done for this frame
        } else if (ours) {
            reinterpret_cast<void (*)(std::uint64_t, std::int32_t)>(keep)(sub, frame);
            path = 1;
        } else if (fresh) {
            reinterpret_cast<void (*)(std::uint64_t, std::int32_t)>(hover)(sub, frame);
            path = 2;
        } else {
            path = 3;                                                // the game is marking it itself: leave it be
        }
        if (path == 1 || path == 2) { s_sub = sub; s_frame = frame; }
        if (path >= 2 && s_said < 60) {
            ++s_said;
            RingLog("hover mark: obj %llx frame %d id %d -> %s", (unsigned long long)sub, frame, id,
                    path == 2 ? "started" : "left to the game");
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
}  // namespace

namespace {
std::uint64_t g_traceSub = 0, g_traceNode = 0; ULONGLONG g_traceUntil = 0; int g_traceOff = -1;
std::uint8_t g_traceLast[64]; bool g_traceHave = false;
// The object's render node: the field that points at a heap object holding the outline colour
// (four floats 0..1 at +0x100, the alpha near 0.85 or 0) and a width float at +0x110.
// The object's render node, looked for while the outline is on: the heap object, one or two pointers
// down from the scene object, that holds the outline colour at +0x100 (rgb not all zero, alpha
// above a half) and a width at +0x110. The owner at +0x60 has zeros there and is passed over.
bool NodeLike(std::uint64_t p) {
    if (!IsHeap(p) || !InModule(R64(p))) return false;
    float c[5];
    for (int i = 0; i < 5; ++i) { std::uint32_t u = R32(p + 0x100 + 4 * i); std::memcpy(&c[i], &u, 4); if (!(c[i] >= 0.f && c[i] <= 64.f)) return false; }
    if (c[0] > 1.f || c[1] > 1.f || c[2] > 1.f || c[3] > 1.f) return false;
    return c[3] > 0.5f && (c[0] > 0.f || c[1] > 0.f || c[2] > 0.f);
}
std::uint64_t FindNode(std::uint64_t sub) {
    for (std::uint64_t off = 0; off < 0x1800; off += 8) {
        const std::uint64_t p = R64(sub + off);
        if (NodeLike(p)) { g_traceOff = (int)off; return p; }
    }
    for (std::uint64_t off = 0; off < 0x1800; off += 8) {
        const std::uint64_t p = R64(sub + off);
        if (!IsHeap(p) || !InModule(R64(p))) continue;
        for (std::uint64_t o2 = 0; o2 < 0x800; o2 += 8) {
            const std::uint64_t q = R64(p + o2);
            if (NodeLike(q)) { g_traceOff = (int)(off | (o2 << 16)); return q; }
        }
    }
    return 0;
}
}  // namespace

void rtx::scenehover::Trace() {
    // Whatever the game has lit right now, whoever asked for it: the scene object whose highlight
    // id is the current frame. Looked for among the objects the scene walk knows.
    if (!g_traceSub || GetTickCount64() > g_traceUntil) {
        __try {
            const std::uint32_t n = g_hoverCount.load();
            std::int32_t frame = -1;
            for (std::uint32_t i = 0; i < n && i < (std::uint32_t)rtx::scene::kMaxObjects && i < 4096; ++i) {
                const std::uint64_t sub = g_hoverRefs[i].sub;
                if (!IsHeap(sub)) continue;
                if (frame < 0) {
                    const std::uint64_t owner = R64(sub + kHlOwner);
                    const std::uint64_t settings = IsHeap(owner) ? R64(owner + kHlSettings) : 0;
                    if (IsHeap(settings)) frame = R32(settings + kHlFrame);
                    if (frame <= 0) return;
                }
                const std::int32_t id = R32(sub + kHlId);
                if (id == frame || id == frame - 1) {
                    if (sub != g_traceSub) { g_traceSub = sub; g_traceNode = 0; g_traceHave = false; RingLog("trace: lit obj %llx (frame %d)", (unsigned long long)sub, frame); }
                    g_traceUntil = GetTickCount64() + 4000;
                    break;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
        if (!g_traceSub || GetTickCount64() > g_traceUntil) return;
    }
    __try {
        if (!IsScenery(R8(g_traceSub + kType))) { g_traceSub = 0; return; }
        if (!g_traceNode) { g_traceNode = FindNode(g_traceSub); if (!g_traceNode) return; RingLog("trace: obj %llx node %llx at +0x%x (two levels when above 0xffff)", (unsigned long long)g_traceSub, (unsigned long long)g_traceNode, g_traceOff); }
        std::uint8_t cur[64];
        for (int i = 0; i < 64; i += 4) { std::uint32_t u = R32(g_traceNode + 0x100 + i); std::memcpy(cur + i, &u, 4); }
        if (g_traceHave && std::memcmp(cur, g_traceLast, 64) == 0) return;
        std::memcpy(g_traceLast, cur, 64); g_traceHave = true;
        float f[6]; std::memcpy(f, cur, 24);
        const std::uint64_t owner = R64(g_traceSub + kHlOwner);
        const std::uint64_t settings = IsHeap(owner) ? R64(owner + kHlSettings) : 0;
        RingLog("trace: frame %d id %d rgba %.3f %.3f %.3f %.3f width %.2f +114 %.3f +134 %02x +118 %08x %08x %08x",
                IsHeap(settings) ? R32(settings + kHlFrame) : -1, R32(g_traceSub + kHlId), f[0], f[1], f[2], f[3], f[4], f[5], cur[0x34],
                *(std::uint32_t*)(cur + 0x18), *(std::uint32_t*)(cur + 0x1C), *(std::uint32_t*)(cur + 0x20));
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_traceSub = 0; }
}

void rtx::scenehover::SetPulseHold(bool on) { g_pulseHold.store(on, std::memory_order_relaxed); }

bool rtx::scenehover::Mark(int x, int y, int id) {
    std::uint64_t sub = 0;
    for (int attempt = 0; attempt < 4; ++attempt) {
        const std::uint32_t s1 = g_hoverSeq.load();
        if (s1 & 1u) continue;
        const std::uint32_t n = g_hoverCount.load();
        std::uint64_t exact = 0, tile = 0;
        for (std::uint32_t i = 0; i < n && i < (std::uint32_t)rtx::scene::kMaxObjects; ++i) {
            const HoverRef& r = g_hoverRefs[i];
            if (r.x != x || r.y != y) continue;
            if (r.id == id) { exact = r.sub; break; }
            if (!tile) tile = r.sub;
        }
        if (g_hoverSeq.load() != s1) continue;
        sub = exact ? exact : tile;
        break;
    }
    if (IsHeap(sub)) { if (sub != g_traceSub) { g_traceSub = sub; g_traceNode = 0; g_traceHave = false; } g_traceUntil = GetTickCount64() + 4000; }
    return IsHeap(sub) && MarkSub(sub);
}

// Entry point the launcher calls through a remote thread to hand this module
// the session it should use. Called once after load and again whenever the
// launcher opens a new session for this client, so a launcher restart while
// the game keeps running is an ordinary rebind rather than a special case.
//
// param points at a SessionBlob the launcher allocated in this process. It is
// read once and not retained.
extern "C" __declspec(dllexport) DWORD WINAPI RtxSetSession(LPVOID param) {
    if (!param) return 1;

    rtx::ipc::SessionBlob blob{};
    __try {
        blob = *reinterpret_cast<const rtx::ipc::SessionBlob*>(param);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 2;
    }

    if (blob.version != rtx::ipc::kSessionBlobVersion) return 3;
    if (blob.pid != GetCurrentProcessId()) return 4;

    rtx::ipc::SetSessionKey(GetCurrentProcessId(), blob.key, sizeof(blob.key));
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    }
    return TRUE;
}
