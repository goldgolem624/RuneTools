#pragma once
// Draw-stream hooks on the game's Vulkan device: scene hiding by skipping depth-tested draws,
// per-pass GPU timing through timestamp queries, the frame's scene depth attachment, and a
// one-shot frame probe that logs passes, enabled entry points and resource usage.

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
void SetHideScene(bool on);
void SetTimingEnabled(bool on);
bool HideSceneAvailable();
void RequestProbe();

}  // namespace rtx::vkprobe
