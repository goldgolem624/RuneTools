#pragma once
// Vulkan present path: captures the game's VkDevice through the loader exports it imports, then
// detours the device-level present and swapchain entry points it fetched by name.

namespace rtx::vkpresent {

bool Install();    // detour the vulkan-1 exports; false when the loader is not loaded
void Poll();       // worker tick: arm on a captured device, bootstrap the swapchain
bool Active();
void SetHideScene(bool on);   // skip the client's depth-tested draws
void SetInFrameTrial(bool on);   // see VkProbe.h
bool HideSceneAvailable();

}  // namespace rtx::vkpresent
