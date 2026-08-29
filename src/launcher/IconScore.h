#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Similarity between a live atlas crop and a bundled pack icon, used to tell the real
// item atlas from the client's other 1536x1536 textures (docs/icon-capture.md).
// Header-only so tools/icon-score-test.cpp can exercise it without the launcher.

namespace rtx::launcher::iconscore {

// Both images are reduced to an 8x8 grid of average RGB over their opaque bounding box,
// so the 36x32 atlas cell (icon centred with padding) and the pack's tighter crop line up.
struct Grid {
    float rgb[64][3] = {};
    bool  opaque[64] = {};
    bool  any = false;
};

inline Grid make_grid(const std::uint8_t* rgba, int w, int h, int stride) {
    Grid g;
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (rgba[(size_t)y * stride + (size_t)x * 4 + 3] >= 128) {
                x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y);
            }
    if (x1 < 0) return g;
    g.any = true;
    const int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
    for (int cy = 0; cy < 8; ++cy) {
        for (int cx = 0; cx < 8; ++cx) {
            int px0 = x0 + cx * bw / 8, px1 = x0 + (cx + 1) * bw / 8;
            int py0 = y0 + cy * bh / 8, py1 = y0 + (cy + 1) * bh / 8;
            if (px1 <= px0) px1 = px0 + 1;
            if (py1 <= py0) py1 = py0 + 1;
            double r = 0, gg = 0, b = 0; int n = 0;
            for (int y = py0; y < py1 && y <= y1; ++y)
                for (int x = px0; x < px1 && x <= x1; ++x) {
                    const std::uint8_t* p = rgba + (size_t)y * stride + (size_t)x * 4;
                    if (p[3] < 128) continue;
                    r += p[0]; gg += p[1]; b += p[2]; ++n;
                }
            const int i = cy * 8 + cx;
            if (n) { g.opaque[i] = true; g.rgb[i][0] = (float)(r / n); g.rgb[i][1] = (float)(gg / n); g.rgb[i][2] = (float)(b / n); }
        }
    }
    return g;
}

// 1 - mean |diff| / 255 over cells opaque in at least one image; a cell opaque in only
// one counts as a full-scale difference. Cells transparent in both are ignored.
inline double grid_similarity(const Grid& a, const Grid& b) {
    if (!a.any || !b.any) return 0.0;
    double sum = 0; int n = 0;
    for (int i = 0; i < 64; ++i) {
        if (!a.opaque[i] && !b.opaque[i]) continue;
        ++n;
        if (a.opaque[i] != b.opaque[i]) { sum += 255.0; continue; }
        sum += (std::fabs(a.rgb[i][0] - b.rgb[i][0]) + std::fabs(a.rgb[i][1] - b.rgb[i][1]) + std::fabs(a.rgb[i][2] - b.rgb[i][2])) / 3.0;
    }
    return n ? 1.0 - (sum / n) / 255.0 : 0.0;
}

}  // namespace rtx::launcher::iconscore
