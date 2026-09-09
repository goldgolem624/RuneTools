#pragma once
// Draw-stream hooks on the game's Vulkan device: scene hiding by skipping depth-tested draws, and a
// one-shot frame probe that logs render passes, draw counts, enabled extensions and resource usage.

#define VK_NO_PROTOTYPES
#include "vk/vulkan_core.h"

namespace rtx::vkprobe {

bool Attach(VkDevice dev, PFN_vkGetDeviceProcAddr gdpa, void (*log)(const char*, ...));
void Detach();
void FrameEnd();                    // present-time boundary
void SetHideScene(bool on);
bool HideSceneAvailable();
void RequestProbe();                // log the next frame's pass list

}  // namespace rtx::vkprobe
