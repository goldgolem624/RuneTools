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
struct ImageInfo { VkImageUsageFlags usage; VkSampleCountFlagBits samples; VkImageCreateFlags flags; unsigned w, h, mips, layers; };
bool LookupImage(VkImage img, ImageInfo* out);   // create info recorded by the vkCreateImage hook
void SetHideScene(bool on);
// Drawing inside the game's frame. Between the pass that finishes the scene and the pass that
// starts the interface, `recorder` is handed the game's command buffer and a render pass over the
// finished scene image; what it draws ends up under the whole interface. Everything the game had
// bound in that command buffer is put back afterwards.
void SetInFrameTrial(bool on);
void SetSceneRecorder(bool (*recorder)(VkCommandBuffer cmd, VkRenderPass rp, VkFramebuffer fb, std::uint32_t w, std::uint32_t h, unsigned look));
// The other place: inside the game's interface pass itself, after the draws that copy the scene in
// and before its first interface batch. `borrow` runs before the game begins that pass and `giveBack`
// after it ends, since no image can change layout in between.
struct PassRecorder {
    bool (*borrow)(VkCommandBuffer cmd, VkImage passDepth);
    void (*giveBack)(VkCommandBuffer cmd);
    bool (*record)(VkCommandBuffer cmd, VkRenderPass gameRp, std::uint32_t w, std::uint32_t h, bool borrowed, unsigned look);
    void (*trial)(int where, unsigned look);
};
void SetPassRecorder(const PassRecorder& r);
void SetTimingEnabled(bool on);
bool HideSceneAvailable();
void RequestProbe();

}  // namespace rtx::vkprobe
