#pragma once
#include <cstdint>
// Present callback: composites launcher UI into the game frame before the swap or present.

namespace rtx::frame { struct Share; }

namespace rtx::present {
// Hands the game's own screen points for the launcher's world points to the frame share; a null
// list clears them.
bool PublishAnchors(const void* points, int count);
// Hands over the game's own answers to the launcher's questions about the account; `askSeq` names
// the list they belong to and `ready` says every question in it has been answered.
bool PublishAnswers(const void* answers, int count, std::uint32_t askSeq, bool ready);
// The width of each glyph of the label font, as the compositor measured it; `px` is the font size
// those widths belong to. Sent once, when the atlas is built.
void PublishGlyphWidths(const int* adv, int count, int px);
void FrameChannelState(bool& mapped, std::uint32_t& magic, std::uint32_t& version,
                       std::uint32_t& wantMagic, std::uint32_t& wantVersion);

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
    void (*SetDepth)(const float*);                       // per-command clip depths, nullptr = none
    void (*SetDepthMode)(unsigned, const float*);          // marker share flags + calibration {x,y,z, a,b, x2,y2,z2}
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
