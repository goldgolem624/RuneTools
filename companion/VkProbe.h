#pragma once
// Draw-stream hooks on the game's Vulkan device: scene hiding by skipping depth-tested draws,
// per-pass GPU timing through timestamp queries, the frame's scene depth attachment, and a
// one-shot frame probe that logs passes, enabled entry points and resource usage.

#include <cstdint>
#define VK_NO_PROTOTYPES
#include "vk/vulkan_core.h"

namespace rtx::vkprobe {

bool Attach(VkDevice dev, PFN_vkGetDeviceProcAddr gdpa, float timestampPeriodNs,
            void (*log)(const char*, ...), void (*imageDestroyed)(VkImage));
void Detach();
void SetTargetExtent(unsigned w, unsigned h);   // swapchain size: which depth pass counts as the scene
void FrameBegin();                              // present-time boundary, before the overlay is recorded
void OnOverlayCmd(VkCommandBuffer cmd);         // start of the overlay command buffer
bool SceneDepth(VkImage* img, VkFormat* fmt, VkImageLayout* layout);
// The characters' depth: the game writes it in a depth-only pass of its own and never into the
// scenery's depth image (characters are tested against the scenery, not added to it).
bool ActorDepth(VkImage* img, VkFormat* fmt, VkImageLayout* layout);
// Whether the characters' depth image was drawn into in the last frame. The image itself is kept
// between frames (ActorDepth), so a frame without it does not tear the compositor's view down.
bool ActorDepthActive();
struct ImageInfo { VkImageUsageFlags usage; VkSampleCountFlagBits samples; VkImageCreateFlags flags; unsigned w, h, mips, layers; };
bool LookupImage(VkImage img, ImageInfo* out);   // create info recorded by the vkCreateImage hook
// The sample count of a depth image, from the render pass it was last a depth attachment of: what is
// known of an image created before the image hook was in place. False when no pass described it.
bool DepthAttachmentSamples(VkImage img, VkSampleCountFlagBits* out);
void SetHideScene(bool on);
// Drawing inside the game's frame. Between the pass that finishes the scene and the pass that
// starts the interface, `recorder` is handed the game's command buffer and a render pass over the
// finished scene image; what it draws ends up under the whole interface. Everything the game had
// bound in that command buffer is put back afterwards.
void SetInFrameTrial(bool on);
void SetSceneRecorder(bool (*recorder)(VkCommandBuffer cmd, VkRenderPass rp, VkFramebuffer fb, std::uint32_t w, std::uint32_t h, unsigned look));
// The other place: where the game's interface starts, after the draws that copy the scene in and
// before its first interface batch (or, when the game draws its interface into a target of its own,
// over the scene image before anything else begins). `borrow` runs before the game begins that pass
// and `giveBack` after it ends, since no image can change layout in between. `sceneBegins` runs when
// the game begins a frame's scene pass: the camera that frame is drawn with is taken there.
struct PassRecorder {
    bool (*borrow)(VkCommandBuffer cmd, VkImage passDepth);
    void (*giveBack)(VkCommandBuffer cmd);
    bool (*record)(VkCommandBuffer cmd, VkRenderPass gameRp, std::uint32_t w, std::uint32_t h, bool borrowed, unsigned look);
    void (*trial)(int where, unsigned look);
    void (*sceneBegins)();
};
void SetPassRecorder(const PassRecorder& r);
void SetTimingEnabled(bool on);
bool HideSceneAvailable();
void RequestProbe();

}  // namespace rtx::vkprobe
