#pragma once
// Frame composite: client-pixel coords, straight RGBA in, premultiplied blend; GL thread only, between Begin() and End().

namespace rtx::composite {

void Begin();
void End();

void DrawSolidRect(int x, int y, int w, int h,
                   float r, float g, float b, float a,
                   int fb_w, int fb_h);

void DrawLine(float x0, float y0, float x1, float y1, float thickness,
              float r, float g, float b, float a, int fb_w, int fb_h);

void DrawFillQuad(float x0, float y0, float x1, float y1,
                  float x2, float y2, float x3, float y3,
                  float r, float g, float b, float a, int fb_w, int fb_h);

void DrawGlyph(int cell, float x, float y, float w, float h,
               float r, float g, float b, float a, int fb_w, int fb_h);

void DrawLabel(const char* s, float cx, float cy, float text_px,
               float ar, float ag, float ab, float aa, int fb_w, int fb_h);

void DrawPlainText(const char* s, float x, float y, float text_px, int align,
                   float r, float g, float b, float a, int fb_w, int fb_h);

void DrawRoundRect(float x, float y, float w, float h, float rad,
                   float r, float g, float b, float a, int fb_w, int fb_h);

void UploadUiLayer(const void* bgra, int w, int h, int stride,
                   int dx, int dy, int dw, int dh);

void DrawUiLayer(int dst_x, int dst_y, int fb_w, int fb_h);

void UploadHud(const void* rgba, int w, int h);
void DrawHud(int dst_x, int dst_y, int dst_w, int dst_h, int fb_w, int fb_h);

void Shutdown();

}  // namespace rtx::composite
