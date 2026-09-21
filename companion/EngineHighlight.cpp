#include "EngineHighlight.h"

#include <windows.h>
#include <cstring>

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
constexpr int kSwitchSig[] = {
    0x80, 0x3D, kAny, kAny, kAny, kAny, 0x00,
    0x0F, 0x29, 0x74, 0x24, kAny,
    0x75, kAny,
    0x80, 0x39, 0x00,
    0x75, kAny,
    0x48, 0x85, 0xD2, 0x74, kAny,
    0x80, 0xBA, kAny, kAny, 0x00, 0x00, 0x00,
};
constexpr std::size_t kSwitchDispAt = 2, kSwitchInsnEnd = 7;

// The script op that sets a category's mode, which is where the category table is named:
//   cmp eax, 7 ; ja                 83 F8 07 77 ..
//   mov r8d, [rcx+off]              44 8B 81 <off32>
//   cmp r8d, 3 ; ja                 41 83 F8 03 77 ..
//   add rax, rax                    48 03 C0
//   lea rcx, [rip+disp]             48 8D 0D <disp32>      <- the table
//   mov [rcx+rax*8], r8b            44 88 04 C1
constexpr int kTableSig[] = {
    0x83, 0xF8, 0x07, 0x77, kAny,
    0x44, 0x8B, 0x81, kAny, kAny, 0x00, 0x00,
    0x41, 0x83, 0xF8, 0x03, 0x77, kAny,
    0x48, 0x03, 0xC0,
    0x48, 0x8D, 0x0D, kAny, kAny, kAny, kAny,
    0x44, 0x88, 0x04, 0xC1,
};
constexpr std::size_t kTableDispAt = 24, kTableInsnEnd = 28;

// One category: mode byte, the outline width as a byte, then the colour as three floats 0..1.
constexpr std::size_t kCategoryStride = 16, kCategoryWidth = 1, kCategoryColour = 4;

Status        g_status = kUnknown;
std::uint8_t* g_byte = nullptr;
std::uint8_t  g_original = 0;
bool          g_touched = false;

std::uint8_t* g_table = nullptr;           // null when only the table was not recognised
float         g_gameColour[kCategories][3] = {};
std::uint32_t g_appliedRgb[kCategories] = {};   // per category; 0 = the game's own colour is in place
std::uint8_t  g_gameWidth[kCategories] = {};
std::uint8_t  g_appliedWidth[kCategories] = {};    // per category; 0 = the game's own width is in place

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

void Resolve() {
    g_status = kNotFound;
    Image im;
    if (!OpenImage(im)) return;

    const std::uint8_t* sw = FindUnique(im, kSwitchSig, sizeof(kSwitchSig) / sizeof(kSwitchSig[0]));
    if (!sw) return;
    std::uint8_t* target = RipTarget(sw, kSwitchDispAt, kSwitchInsnEnd);
    if (!InWritableData(im, target, 1) || *target > 1) return;   // a flag is 0 or 1
    g_byte = target;
    g_original = *target;
    g_status = kActive;

    // the colours are optional: without the table the outline still works, in the game's colours
    const std::uint8_t* tb = FindUnique(im, kTableSig, sizeof(kTableSig) / sizeof(kTableSig[0]));
    if (!tb) return;
    std::uint8_t* table = RipTarget(tb, kTableDispAt, kTableInsnEnd);
    if (!InWritableData(im, table, 8 * kCategoryStride)) return;
    for (int c = 0; c < 8; ++c)
        if (table[c * kCategoryStride] > 3) return;              // modes are 0..3
    g_table = table;
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

void ApplyWidth(int category, std::uint8_t width) {
    if (!g_table) return;
    std::uint8_t* cur = g_table + (std::size_t)category * kCategoryStride + kCategoryWidth;
    if (width == 0) {
        if (g_appliedWidth[category] != 0) *cur = g_gameWidth[category];
    } else if (*cur != width) {
        // anything other than the width this put there is the game's own
        if (g_appliedWidth[category] == 0 || *cur != g_appliedWidth[category]) g_gameWidth[category] = *cur;
        *cur = width;
    }
    g_appliedWidth[category] = width;
}

}  // namespace

Status Set(bool on, const std::uint32_t* rgb, const std::uint32_t* thickness) {
    if (g_status == kUnknown) Resolve();
    if (g_status != kActive) return g_status;
    const std::uint8_t want = on ? 1 : g_original;
    if (*g_byte != want) { *g_byte = want; g_touched = true; }
    for (int c = 0; c < kCategories; ++c) {
        const std::uint32_t colour = (on && rgb) ? (rgb[c] & 0xFFFFFFu) : 0u;
        if (colour != 0 || g_appliedRgb[c] != 0) ApplyColour(c, colour);
    }
    for (int c = 0; c < kCategories; ++c) {
        const std::uint32_t asked = (on && thickness) ? thickness[c] : 0u;
        const std::uint8_t width = (std::uint8_t)(asked > kMaxThickness ? kMaxThickness : asked);
        if (width != 0 || g_appliedWidth[c] != 0) ApplyWidth(c, width);
    }
    return g_status;
}

void Restore() {
    if (g_status != kActive) return;
    for (int c = 0; c < kCategories; ++c) { ApplyColour(c, 0); ApplyWidth(c, 0); }
    if (g_touched) *g_byte = g_original;
    g_touched = false;
}

}  // namespace rtx::enginehl
