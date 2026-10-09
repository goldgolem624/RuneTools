#include "EngineHighlight.h"
#include "Signatures.h"
#include "EngineOps.h"
#include "CodeFind.h"

#include <windows.h>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

namespace rtx::enginehl {
namespace {

constexpr int kAny = -1;

// Where the loc pass decides whether a definition's opt-out applies:
//   cmp byte ptr [rip+disp], 0      80 3D <disp32> 00      <- the byte this module sets
//   movaps [rsp+50h], xmm6          0F 29 74 24 50
//   jne                             75 ..
//   cmp byte ptr [rcx], 0           80 39 00               (the loc's own "important" flag)
//   jne                             75 ..
//   test rdx, rdx ; je              48 85 D2 74 ..
//   cmp byte ptr [rdx+off], 0       80 BA <off32> 00       (the definition's opt-out)
// Offsets and displacements are wildcards, so a build that moves them still matches.
constexpr const auto& kSwitchSig = rtx::sig::kOutlineSwitch;
constexpr std::size_t kSwitchDispAt = 2, kSwitchInsnEnd = 7;

// The script op that sets a category's mode, which is where the category table is named:
//   cmp eax, 7 ; ja                 83 F8 07 77 ..
//   mov r8d, [rcx+off]              44 8B 81 <off32>
//   cmp r8d, 3 ; ja                 41 83 F8 03 77 ..
//   add rax, rax                    48 03 C0
//   lea rcx, [rip+disp]             48 8D 0D <disp32>      <- the table
//   mov [rcx+rax*8], r8b            44 88 04 C1
constexpr const auto& kTableSig = rtx::sig::kOutlineTable;
constexpr std::size_t kTableDispAt = 24, kTableInsnEnd = 28;

// One category: mode byte, the outline width as a byte, then the colour as three floats 0..1.
constexpr std::size_t kCategoryStride = 16, kCategoryWidth = 1, kCategoryColour = 4;
// The mode the game draws an outline in. HIGHLIGHT_SET_CATEGORY_MODE takes 0 to 3, and the renderer
// returns no outline strength at all for 3, so 3 alone means "off" and 0, 1 and 2 all draw.
constexpr std::uint8_t kModeOutline = 0, kModeOff = 3, kModeNone = 0xFF;

std::mutex g_logMu; char g_log[600] = {};
char g_check[600] = {};                 // the highlight check line, taken before the log (under g_logMu)
void Say(const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_logMu);
    std::size_t used = std::strlen(g_log);
    if (used && used + 4 < sizeof(g_log)) { std::memcpy(g_log + used, " | ", 4); used += 3; }
    va_list ap; va_start(ap, fmt);
    std::vsnprintf(g_log + used, sizeof(g_log) - used, fmt, ap);
    va_end(ap);
}
// Boot record check line (grammar in Signatures.h).
void SayCheck(const char* status, const char* kind, const char* exp, const char* got, const char* detail) {
    std::lock_guard<std::mutex> lk(g_logMu);
    std::snprintf(g_check, sizeof(g_check), "check: highlight %s kind=%s exp=%s got=%s features=%s need=- ; %s", status,
                  kind && kind[0] ? kind : "-", exp && exp[0] ? exp : "-", got && got[0] ? got : "-",
                  "Native outlines|Native outlines: colours and modes", detail);
}

Status        g_status = kUnknown;
std::uint8_t* g_byte = nullptr;
std::uint8_t  g_original = 0;
bool          g_touched = false;

std::uint8_t* g_table = nullptr;           // null when only the table was not recognised
float         g_gameColour[kCategories][3] = {};
std::uint32_t g_appliedRgb[kCategories] = {};   // per category; 0 = the game's own colour is in place
std::uint8_t  g_gameWidth[kCategories] = {};
std::uint8_t  g_gameMode[kCategories] = {};
std::uint8_t  g_appliedMode[kCategories] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };   // 0xFF = the game's own mode is in place
bool          g_haveMode[kCategories] = {};
std::uint8_t  g_appliedWidth[kCategories] = {};    // what this last wrote, while g_haveWidth says it did
bool          g_haveWidth[kCategories] = {};

struct Image {
    const std::uint8_t* base = nullptr;
    const IMAGE_NT_HEADERS64* nt = nullptr;
};

bool OpenImage(Image& im) {
    im.base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
    if (!im.base) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(im.base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    im.nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(im.base + dos->e_lfanew);
    return im.nt->Signature == IMAGE_NT_SIGNATURE;
}

// The single place in the code that matches, or null when there is none or more than one.
const std::uint8_t* FindUnique(const Image& im, const int* sig, std::size_t len) {
    const std::uint8_t* found = nullptr;
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(im.nt);
    for (unsigned i = 0; i < im.nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const std::uint8_t* p = im.base + sec->VirtualAddress;
        const std::size_t n = sec->Misc.VirtualSize;
        if (n < len) continue;
        for (std::size_t o = 0; o + len <= n; ++o) {
            if (p[o] != (std::uint8_t)sig[0] || p[o + 1] != (std::uint8_t)sig[1]) continue;
            std::size_t k = 2;
            while (k < len && (sig[k] == kAny || p[o + k] == (std::uint8_t)sig[k])) ++k;
            if (k != len) continue;
            if (found) return nullptr;               // ambiguous: touch nothing
            found = p + o;
        }
    }
    return found;
}

// `count` bytes of this module's own writable data.
bool InWritableData(const Image& im, const std::uint8_t* p, std::size_t count) {
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(im.nt);
    for (unsigned i = 0; i < im.nt->FileHeader.NumberOfSections; ++i, ++sec) {
        const std::uint8_t* lo = im.base + sec->VirtualAddress;
        const std::uint8_t* hi = lo + sec->Misc.VirtualSize;
        if (p >= lo && p + count <= hi)
            return (sec->Characteristics & IMAGE_SCN_MEM_WRITE) && !(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE);
    }
    return false;
}

std::uint8_t* RipTarget(const std::uint8_t* match, std::size_t dispAt, std::size_t insnEnd) {
    std::int32_t disp;
    std::memcpy(&disp, match + dispAt, 4);
    return const_cast<std::uint8_t*>(match) + insnEnd + disp;
}

// The eight category records read as the table: modes 0..3, colours three floats in 0..1.
std::uint8_t* g_tableRefused = nullptr;   // a hit that did not read as the table
bool TableReads(const std::uint8_t* table) {
    for (int c = 0; c < 8; ++c) {
        if (table[c * kCategoryStride] > 3) return false;
        float col[3];
        std::memcpy(col, table + c * kCategoryStride + kCategoryColour, sizeof(col));
        for (float v : col) if (!(v >= 0.0f && v <= 1.0f)) return false;
    }
    return true;
}

void Resolve() {
    g_status = kNotFound;
    Image im;
    if (!OpenImage(im)) return;

    const std::uint8_t* sw = FindUnique(im, kSwitchSig, sizeof(kSwitchSig) / sizeof(kSwitchSig[0]));
    if (!sw) { Say("outline: the switch was not recognised in this build"); return; }
    std::uint8_t* target = RipTarget(sw, kSwitchDispAt, kSwitchInsnEnd);
    if (!InWritableData(im, target, 1) || *target > 1) {
        Say("outline: switch at exe+0x%llx holds %u, which is not a flag",
            (unsigned long long)(target - im.base), InWritableData(im, target, 1) ? *target : 0u);
        return;
    }
    g_byte = target;
    g_original = *target;
    g_status = kActive;
    Say("outline: switch at exe+0x%llx, the game had it %u", (unsigned long long)(target - im.base), (unsigned)g_original);

    // the colours are optional: without the table the outline still works, in the game's colours
    const std::uint8_t* tb = FindUnique(im, kTableSig, sizeof(kTableSig) / sizeof(kTableSig[0]));
    if (!tb) { Say("outline: no colour table in this build, the game's own colours and widths stay"); return; }
    std::uint8_t* table = RipTarget(tb, kTableDispAt, kTableInsnEnd);
    if (!InWritableData(im, table, 8 * kCategoryStride)) {
        Say("outline: colour table at exe+0x%llx is not where writable data is", (unsigned long long)(table - im.base));
        return;
    }
    if (!TableReads(table)) {
        Say("outline: colour table at exe+0x%llx refused, modes %u %u %u %u %u %u %u %u (0 to 3 and colours 0..1 expected)",
            (unsigned long long)(table - im.base),
            table[0], table[kCategoryStride], table[2 * kCategoryStride], table[3 * kCategoryStride],
            table[4 * kCategoryStride], table[5 * kCategoryStride], table[6 * kCategoryStride], table[7 * kCategoryStride]);
        g_tableRefused = table;
        return;
    }
    g_table = table;
    Say("outline: colour table at exe+0x%llx accepted", (unsigned long long)(table - im.base));
}

// The anchor behind the table: the handler of the op that sets a category's mode names the table
// with its first lea into writable data. Compared once the op table is ready (or given up after a
// minute), and the check line written then; a table the signature missed is adopted from the op.
bool g_checkDone = false;
ULONGLONG g_checkFrom = 0;
void AnchorCheck() {
    if (g_checkDone) return;
    if (!g_checkFrom) g_checkFrom = GetTickCount64();
    const bool ready = rtx::engineops::Ready();
    if (!ready && GetTickCount64() - g_checkFrom < 60000) return;
    g_checkDone = true;
    Image im;
    if (!OpenImage(im)) return;
    char exp[24], got[24], detail[400];
    std::snprintf(exp, sizeof(exp), "0x%llx", (unsigned long long)(g_table ? g_table - im.base : 0));
    if (g_status != kActive) {
        std::snprintf(got, sizeof(got), "-");
        SayCheck("FAIL", "gone", exp, got, "GONE: the loc pass outline switch was not recognised in this exe; native outlines off");
        return;
    }
    const void* h = ready ? rtx::engineops::Handler("HIGHLIGHT_SET_CATEGORY_MODE") : nullptr;
    std::uint64_t opTable = 0;
    if (h) {
        rtx::codefind::Image cim;
        if (rtx::codefind::OpenImage(reinterpret_cast<std::uint64_t>(im.base), reinterpret_cast<std::uint64_t>(im.base), cim))
            opTable = rtx::codefind::LeaIntoData(cim, reinterpret_cast<std::uint64_t>(h), 0x80);
    }
    std::snprintf(got, sizeof(got), "0x%llx", (unsigned long long)(opTable ? opTable - reinterpret_cast<std::uint64_t>(im.base) : (g_table ? g_table - im.base : 0)));
    if (!g_table && opTable) {
        std::uint8_t* table = reinterpret_cast<std::uint8_t*>(opTable);
        if (InWritableData(im, table, 8 * kCategoryStride) && TableReads(table)) {
            g_table = table;
            std::snprintf(detail, sizeof(detail), "MOVED: colour table at 0x%llx named by op HIGHLIGHT_SET_CATEGORY_MODE, the signature missed%s; adopted",
                          (unsigned long long)(opTable - reinterpret_cast<std::uint64_t>(im.base)), g_tableRefused ? " (its hit was refused)" : "");
            SayCheck("OK", "moved", exp, got, detail);
        } else {
            std::snprintf(detail, sizeof(detail), "FORMAT: op HIGHLIGHT_SET_CATEGORY_MODE names 0x%llx, which does not read as the category table; colours and modes off, the switch stays",
                          (unsigned long long)(opTable - reinterpret_cast<std::uint64_t>(im.base)));
            SayCheck("FAIL", "format", exp, got, detail);
        }
        return;
    }
    if (g_table && opTable && opTable != reinterpret_cast<std::uint64_t>(g_table)) {
        std::snprintf(detail, sizeof(detail), "MOVED: op HIGHLIGHT_SET_CATEGORY_MODE names the table at 0x%llx, the signature 0x%llx; colours and modes off, the switch stays",
                      (unsigned long long)(opTable - reinterpret_cast<std::uint64_t>(im.base)), (unsigned long long)(g_table - im.base));
        g_table = nullptr;
        SayCheck("FAIL", "moved", exp, got, detail);
        return;
    }
    std::snprintf(detail, sizeof(detail), "switch at 0x%llx (the game had it %u)%s%s", (unsigned long long)(g_byte - im.base), (unsigned)g_original,
                  g_table ? ", colour table: 8 categories, modes 0..3, colours 0..1" : (g_tableRefused ? ", colour table refused: the game's own colours stay" : ", no colour table: the game's own colours stay"),
                  opTable ? "; op HIGHLIGHT_SET_CATEGORY_MODE names the same table" : ready ? "; the op name is not in this build's table, anchor not compared" : "; op table not ready within a minute, anchor not compared");
    SayCheck(g_table ? "OK" : "FAIL", g_table ? "" : "format", exp, got, g_table ? detail : (std::string("FORMAT: ") + detail).c_str());
}

float* Colour(int category) {
    return reinterpret_cast<float*>(g_table + (std::size_t)category * kCategoryStride + kCategoryColour);
}

void Unpack(std::uint32_t rgb, float out[3]) {
    out[0] = (float)((rgb >> 16) & 0xFF) / 255.0f;
    out[1] = (float)((rgb >> 8) & 0xFF) / 255.0f;
    out[2] = (float)(rgb & 0xFF) / 255.0f;
}

void ApplyColour(int category, std::uint32_t rgb) {
    if (!g_table) return;
    float* cur = Colour(category);
    if (rgb == 0) {
        if (g_appliedRgb[category] != 0) std::memcpy(cur, g_gameColour[category], sizeof(float) * 3);
        g_appliedRgb[category] = 0;
        return;
    }
    float want[3], was[3];
    Unpack(rgb, want);
    Unpack(g_appliedRgb[category], was);
    if (std::memcmp(cur, want, sizeof(want)) != 0) {
        // Anything other than the colour this put there came from the game (it sends its colours
        // again when its settings change): that is what gets restored later.
        if (g_appliedRgb[category] == 0 || std::memcmp(cur, was, sizeof(was)) != 0)
            std::memcpy(g_gameColour[category], cur, sizeof(want));
        std::memcpy(cur, want, sizeof(want));
    }
    g_appliedRgb[category] = rgb;
}

// The mode decides whether the category is drawn as an outline at all, and the game sets it from
// its own highlight setting. A client with that setting off leaves it at a value that draws
// nothing, so a colour and a width alone are not enough: on a client like that the outline was
// applied exactly as asked and stayed invisible. While the feature is on, the categories it drives
// are put into the outline mode, and the game's own mode goes back when it is turned off.
// Mode: when the game highlights the category. What the launcher asks for is written; kModeKeep
// leaves the player's own choice in place, except that a category the player switched off is lifted
// to mouseover while the feature is on, since otherwise the feature looks broken through no fault of
// its own. Whatever the game had is remembered and put back.
void ApplyMode(int category, std::int32_t mode, bool on) {
    if (!g_table) return;
    std::uint8_t* cur = g_table + (std::size_t)category * kCategoryStride;
    const bool mine = g_appliedMode[category] != kModeNone;
    // Give the category back: only over the value this left there, so a mode the player changed
    // underneath us is kept rather than clobbered.
    auto release = [&] {
        if (mine && g_haveMode[category] && *cur == g_appliedMode[category]) *cur = g_gameMode[category];
        g_appliedMode[category] = kModeNone;
    };
    if (!on) { release(); return; }

    std::int32_t want = mode;
    if (want == kModeKeep) {
        // nothing of ours belongs here unless the player's own choice draws nothing
        const std::uint8_t player = mine && g_haveMode[category] ? g_gameMode[category] : *cur;
        if (player != kModeOff) { release(); return; }
        want = kModeMouseover;
    }
    if (want < 0) want = kModeMouseover;
    if (want > kModeOff) want = kModeOff;
    const std::uint8_t put = (std::uint8_t)want;
    if (mine && *cur == g_appliedMode[category] && *cur == put) return;      // already ours
    if (!mine || *cur != g_appliedMode[category]) {                          // the game wrote its own
        g_gameMode[category] = *cur; g_haveMode[category] = true;
    }
    if (*cur != put)
        Say("highlight: category %d mode %u -> %u (the player's own is %u)", category,
            (unsigned)*cur, (unsigned)put, (unsigned)g_gameMode[category]);
    *cur = put;
    g_appliedMode[category] = put;
}

void ApplyScale(int category, std::int32_t scale) {
    if (!g_table) return;
    std::uint8_t* cur = g_table + (std::size_t)category * kCategoryStride + kCategoryWidth;
    const bool mine = g_haveWidth[category];
    if (scale == kScaleKeep) {
        if (mine && *cur == g_appliedWidth[category]) *cur = g_gameWidth[category];
        g_haveWidth[category] = false;
        return;
    }
    std::uint8_t want = (std::uint8_t)(scale < 0 ? 0 : scale > kMaxScale ? kMaxScale : scale);
    // Whether the scale is zero is not a width, it is which path the entity takes. The highlight
    // vertex shader scales the mesh by uVertexScale, which the game sets to 1 only when the scale is
    // zero: that draws the mesh at true size and fills it, the silhouette. Any other scale collapses
    // the mesh out of that pass and the entity is outlined afterwards instead, at this width. The
    // game's own Entity Highlight Type is what decides which, and it sets up the frame accordingly,
    // so changing the zero-ness from here produces an entity that is drawn for one path and
    // finished by the other, which shows nothing or the wrong shape. The width is ours to set; the
    // choice is not.
    const std::uint8_t theirs = g_haveWidth[category] ? g_gameWidth[category] : *cur;
    if ((want == 0) != (theirs == 0)) {
        if (mine && *cur == g_appliedWidth[category]) *cur = g_gameWidth[category];
        g_haveWidth[category] = false;
        static bool s_said = false;
        if (!s_said) {
            s_said = true;
            Say("highlight: the game is drawing %s, which its own Entity Highlight Type chooses; leaving that alone",
                theirs == 0 ? "silhouettes" : "borders");
        }
        return;
    }
    if (mine && *cur == want) return;                       // already ours
    if (!mine || *cur != g_appliedWidth[category]) g_gameWidth[category] = *cur;   // the game's, to go back
    if (*cur != want)
        Say("highlight: category %d scale %u -> %u (%s; the game's own is %u)", category,
            (unsigned)*cur, (unsigned)want, want ? "border" : "silhouette", (unsigned)g_gameWidth[category]);
    *cur = want;
    g_appliedWidth[category] = want;
    g_haveWidth[category] = true;
}

}  // namespace

Status Set(bool on, const std::uint32_t* rgb, const std::int32_t* scale, const std::int32_t* mode) {
    if (g_status == kUnknown) Resolve();
    AnchorCheck();
    if (g_status != kActive) return g_status;
    const std::uint8_t want = on ? 1 : g_original;
    const std::uint8_t had = *g_byte;
    if (*g_byte != want) { *g_byte = want; g_touched = true; }
    // What came in and what the switch did with it, once per change. The read back is the point:
    // the game writes this byte from its own state too, so ours can be overwritten straight away.
    {
        static bool s_first = true; static bool s_on = false;
        static std::uint32_t s_rgb[kCategories] = {};
        static std::int32_t  s_scale[kCategories] = {}, s_mode[kCategories] = {};
        bool same = !s_first && s_on == on;
        for (int c = 0; same && c < kCategories; ++c)
            if (s_rgb[c] != (rgb ? rgb[c] : 0u) || s_scale[c] != (scale ? scale[c] : kScaleKeep) ||
                s_mode[c] != (mode ? mode[c] : kModeKeep)) same = false;
        if (!same) {
            s_first = false; s_on = on;
            for (int c = 0; c < kCategories; ++c) {
                s_rgb[c] = rgb ? rgb[c] : 0u;
                s_scale[c] = scale ? scale[c] : kScaleKeep;
                s_mode[c] = mode ? mode[c] : kModeKeep;
            }
            Say("highlight: asked %s, switch was %u now %u, table %s; interactables %06x scale %d mode %d, npcs %06x/%d/%d, enemies %06x/%d/%d",
                on ? "on" : "off", (unsigned)had, (unsigned)*g_byte, g_table ? "in use" : "not in use",
                s_rgb[kCatScenery], s_scale[kCatScenery], s_mode[kCatScenery],
                s_rgb[kCatNpcs], s_scale[kCatNpcs], s_mode[kCatNpcs],
                s_rgb[kCatAttackable], s_scale[kCatAttackable], s_mode[kCatAttackable]);
        }
    }
    for (int c = 0; c < kCategories; ++c) {
        const std::uint32_t colour = (on && rgb) ? (rgb[c] & 0xFFFFFFu) : 0u;
        if (colour != 0 || g_appliedRgb[c] != 0) ApplyColour(c, colour);
    }
    for (int c = 0; c < kCategories; ++c)
        ApplyScale(c, on && scale ? scale[c] : kScaleKeep);
    for (int c = 0; c < kCategories; ++c)
        ApplyMode(c, mode ? mode[c] : kModeKeep, on);
    return g_status;
}

bool TakeLog(char* out, std::size_t cap) {
    std::lock_guard<std::mutex> lk(g_logMu);
    if (cap == 0) return false;
    if (g_check[0]) { std::snprintf(out, cap, "%s", g_check); g_check[0] = 0; return true; }   // one check line per take
    if (!g_log[0]) return false;
    std::snprintf(out, cap, "%s", g_log);
    g_log[0] = 0;
    return true;
}

void Restore() {
    if (g_status != kActive) return;
    for (int c = 0; c < kCategories; ++c) { ApplyColour(c, 0); ApplyScale(c, kScaleKeep); ApplyMode(c, kModeKeep, false); }
    if (g_touched) *g_byte = g_original;
    g_touched = false;
}

}  // namespace rtx::enginehl
