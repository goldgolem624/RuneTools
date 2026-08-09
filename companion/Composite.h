#pragma once
//
// Frame composite: draws the launcher-published UI into the game's GL frame from
// inside the present callback, saving and restoring the game's GL state around every
// draw. Coordinates are client pixels (origin top-left); colours are straight
// (non-premultiplied) RGBA, composited with premultiplied blend.
//
// All entry points must run on the thread that owns the GL context.

namespace rtx::composite {

// Brackets one frame of compositing; every Draw*/Upload* call must sit between the
// pair. Begin() saves the game's GL state before creating or binding anything.
void Begin();
void End();

void DrawSolidRect(int x, int y, int w, int h,
                   float r, float g, float b, float a,
                   int fb_w, int fb_h);

void DrawLine(float x0, float y0, float x1, float y1, float thickness,
              float r, float g, float b, float a, int fb_w, int fb_h);

// Corners in perimeter order p0->p1->p2->p3.
void DrawFillQuad(float x0, float y0, float x1, float y1,
                  float x2, float y2, float x3, float y3,
                  float r, float g, float b, float a, int fb_w, int fb_h);

// cell = ASCII - kGlyphFirst. The atlas is rasterised once on first use.
void DrawGlyph(int cell, float x, float y, float w, float h,
               float r, float g, float b, float a, int fb_w, int fb_h);

// In-world label centred at (cx,cy) on a dark rounded pill with a 1px accent border.
// ASCII 32..126 only. (Named DrawLabel to dodge the <windows.h> DrawText macro.)
void DrawLabel(const char* s, float cx, float cy, float text_px,
               float ar, float ag, float ab, float aa, int fb_w, int fb_h);

// One line of text, no pill. y = the line's vertical centre.
// align: 0 = x is the left edge, 1 = centre, 2 = right edge.
void DrawPlainText(const char* s, float x, float y, float text_px, int align,
                   float r, float g, float b, float a, int fb_w, int fb_h);

// rad is clamped to half the smaller extent, so a large radius yields a capsule.
void DrawRoundRect(float x, float y, float w, float h, float rad,
                   float r, float g, float b, float a, int fb_w, int fb_h);

// UI-layer texture (the launcher's off-screen window UI). Pixels are
// premultiplied BGRA, `stride` bytes per source row, `bgra` = the FULL surface
// base pointer. On a size change the whole surface is (re)uploaded; otherwise
// only the dirty sub-rect (dx,dy,dw,dh) is pushed with glTexSubImage2D. Call
// only on an actual content change -- this is the only per-frame upload.
void UploadUiLayer(const void* bgra, int w, int h, int stride,
                   int dx, int dy, int dw, int dh);

// Draws the uploaded layer at ITS OWN pixel size (1:1, top-left anchored) --
// never scaled, so a mid-resize stale texture stays anchored instead of swimming.
void DrawUiLayer(int dst_x, int dst_y, int fb_w, int fb_h);

void UploadHud(const void* rgba, int w, int h);
void DrawHud(int dst_x, int dst_y, int dst_w, int dst_h, int fb_w, int fb_h);

// Release GL resources; render thread only.
void Shutdown();

}  // namespace rtx::composite
