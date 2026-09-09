#pragma once
// Present callback: composites launcher UI into the game frame before the swap or present.

namespace rtx::present {

// Drawing surface a renderer backend provides; see Composite.h for the contract.
struct Backend {
    void (*Begin)();
    void (*End)();
    void (*DrawSolidRect)(int, int, int, int, float, float, float, float, int, int);
    void (*DrawLine)(float, float, float, float, float, float, float, float, float, int, int);
    void (*DrawFillQuad)(float, float, float, float, float, float, float, float, float, float, float, float, int, int);
    void (*DrawGlyph)(int, float, float, float, float, float, float, float, float, int, int);
    void (*DrawLabel)(const char*, float, float, float, float, float, float, float, int, int);
    void (*DrawPlainText)(const char*, float, float, float, int, float, float, float, float, int, int);
    void (*DrawRoundRect)(float, float, float, float, float, float, float, float, float, int, int);
    void (*UploadUiLayer)(const void*, int, int, int, int, int, int, int);
    void (*DrawUiLayer)(int, int, int, int);
    void (*UploadHud)(const void*, int, int);
    void (*DrawHud)(int, int, int, int, int, int);
};

const Backend& GlBackend();
const Backend& VkBackend();

// Draw one frame of overlay content into the current target. hwnd may be null.
void RenderOverlay(const Backend& b, void* hwnd, int fbw, int fbh);

bool Install();
void Poll();       // worker tick; late renderer switch and Vulkan arming
const char* Mode();
void Uninstall();

}  // namespace rtx::present
