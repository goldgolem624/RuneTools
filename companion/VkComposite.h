#pragma once
// Vulkan frame composite: same drawing contract as Composite.h, rendered into the swapchain image
// the game is about to present. Present thread only, between BeginTarget() and Submit().

#include <cstdint>
#define VK_NO_PROTOTYPES
#include "vk/vulkan_core.h"

namespace rtx::vkcomposite {

// Device-level entry points, resolved by the caller through the real vkGetDeviceProcAddr.
struct DeviceFns {
    PFN_vkCreateRenderPass            CreateRenderPass;
    PFN_vkDestroyRenderPass           DestroyRenderPass;
    PFN_vkCreateImageView             CreateImageView;
    PFN_vkDestroyImageView            DestroyImageView;
    PFN_vkCreateFramebuffer           CreateFramebuffer;
    PFN_vkDestroyFramebuffer          DestroyFramebuffer;
    PFN_vkCreateShaderModule          CreateShaderModule;
    PFN_vkDestroyShaderModule         DestroyShaderModule;
    PFN_vkCreatePipelineLayout        CreatePipelineLayout;
    PFN_vkDestroyPipelineLayout       DestroyPipelineLayout;
    PFN_vkCreateGraphicsPipelines     CreateGraphicsPipelines;
    PFN_vkDestroyPipeline             DestroyPipeline;
    PFN_vkCreateDescriptorSetLayout   CreateDescriptorSetLayout;
    PFN_vkDestroyDescriptorSetLayout  DestroyDescriptorSetLayout;
    PFN_vkCreateDescriptorPool        CreateDescriptorPool;
    PFN_vkDestroyDescriptorPool       DestroyDescriptorPool;
    PFN_vkAllocateDescriptorSets      AllocateDescriptorSets;
    PFN_vkUpdateDescriptorSets        UpdateDescriptorSets;
    PFN_vkCreateSampler               CreateSampler;
    PFN_vkDestroySampler              DestroySampler;
    PFN_vkCreateImage                 CreateImage;
    PFN_vkDestroyImage                DestroyImage;
    PFN_vkCreateBuffer                CreateBuffer;
    PFN_vkDestroyBuffer               DestroyBuffer;
    PFN_vkGetImageMemoryRequirements  GetImageMemoryRequirements;
    PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements;
    PFN_vkAllocateMemory              AllocateMemory;
    PFN_vkFreeMemory                  FreeMemory;
    PFN_vkBindImageMemory             BindImageMemory;
    PFN_vkBindBufferMemory            BindBufferMemory;
    PFN_vkMapMemory                   MapMemory;
    PFN_vkUnmapMemory                 UnmapMemory;
    PFN_vkCreateCommandPool           CreateCommandPool;
    PFN_vkDestroyCommandPool          DestroyCommandPool;
    PFN_vkAllocateCommandBuffers      AllocateCommandBuffers;
    PFN_vkFreeCommandBuffers          FreeCommandBuffers;
    PFN_vkBeginCommandBuffer          BeginCommandBuffer;
    PFN_vkEndCommandBuffer            EndCommandBuffer;
    PFN_vkResetCommandBuffer          ResetCommandBuffer;
    PFN_vkCmdPipelineBarrier          CmdPipelineBarrier;
    PFN_vkCmdCopyBufferToImage        CmdCopyBufferToImage;
    PFN_vkCmdCopyImageToBuffer        CmdCopyImageToBuffer;
    PFN_vkCmdBeginRenderPass          CmdBeginRenderPass;
    PFN_vkCmdEndRenderPass            CmdEndRenderPass;
    PFN_vkCmdBindPipeline             CmdBindPipeline;
    PFN_vkCmdBindDescriptorSets       CmdBindDescriptorSets;
    PFN_vkCmdBindVertexBuffers        CmdBindVertexBuffers;
    PFN_vkCmdPushConstants            CmdPushConstants;
    PFN_vkCmdSetViewport              CmdSetViewport;
    PFN_vkCmdSetScissor               CmdSetScissor;
    PFN_vkCmdDraw                     CmdDraw;
    PFN_vkCreateFence                 CreateFence;
    PFN_vkDestroyFence                DestroyFence;
    PFN_vkWaitForFences               WaitForFences;
    PFN_vkResetFences                 ResetFences;
    PFN_vkCreateSemaphore             CreateSemaphore;
    PFN_vkDestroySemaphore            DestroySemaphore;
    PFN_vkQueueSubmit                 QueueSubmit;
    PFN_vkGetSwapchainImagesKHR       GetSwapchainImagesKHR;
    PFN_vkDeviceWaitIdle              DeviceWaitIdle;
};

bool Init(VkDevice dev, const DeviceFns& fns,
          const VkPhysicalDeviceMemoryProperties& mem, std::uint32_t queueFamily);
void Shutdown();
bool Ready();

void SetLog(void (*log)(const char*, ...));
void SetCmdHook(void (*hook)(VkCommandBuffer));   // start of every recorded overlay command buffer
void SetAlwaysRecord(bool on);                    // record and submit even when nothing is drawn
void SetFeatures(bool depth, bool capture);       // runtime switches (vk-features.txt)

bool RegisterSwapchain(VkSwapchainKHR sc, VkFormat fmt, std::uint32_t w, std::uint32_t h);
// A swapchain that existed before injection: size from the window, format tried in the order
// the client has been seen to use. Returns the format that took, or VK_FORMAT_UNDEFINED.
VkFormat RegisterSwapchainLate(VkSwapchainKHR sc, std::uint32_t w, std::uint32_t h);
bool KnownSwapchain(VkSwapchainKHR sc);
void UnregisterSwapchain(VkSwapchainKHR sc);
int  SwapchainCount();

// Scene depth attachment of the frame (the game's image) and the layout it is left in.
void SetSceneDepth(VkImage img, VkFormat fmt, VkImageLayout layout, VkImageUsageFlags usage, VkSampleCountFlagBits samples, VkImageCreateFlags flags);

// Drawing the world markers inside the game's frame instead of over the presented one.
// SetInScene(true) asks for it. While it is on and RecordScene is being called, the markers of each
// presented frame are kept back from the overlay pass and handed to the next RecordScene instead.
// RecordScene records them into `cmd`, which is the game's own command buffer between two of its
// render passes: `rp` and `fb` are a pass over the game's finished scene image (16 bit float,
// contents kept, single sampled). Returns false when it drew nothing. Any thread.
void SetInScene(bool on);
// `look`: kLookDepth shows the scene depth as bands over the view, to check the lookup by eye;
// kLookTurned draws for a target the other way up than the one expected, kLookDepthTurned reads
// the scene depth the other way up than expected.
enum : unsigned { kLookDepth = 1, kLookTurned = 2, kLookDepthTurned = 4 };
void SetTrial(int where, unsigned look);   // where 2: at present time as always, with `look` applied there
bool RecordScene(VkCommandBuffer cmd, VkRenderPass rp, VkFramebuffer fb, std::uint32_t w, std::uint32_t h, unsigned look);
// The same markers, recorded INSIDE a render pass the game has open: its interface pass, after it
// has copied the finished scene in and before its first interface batch. Colours land exactly as
// given, since the game's tone mapping is already behind. `gameRp` is that pass (8 bit colour plus
// depth, single sampled). A render pass cannot change an image's layout, so the scene depth is
// borrowed before the game begins that pass and returned after it ends; `passDepth` is that
// pass's own depth image, which cannot be the one sampled.
bool SceneDepthBorrow(VkCommandBuffer cmd, VkImage passDepth);
void SceneDepthReturn(VkCommandBuffer cmd);
bool RecordInPass(VkCommandBuffer cmd, VkRenderPass gameRp, std::uint32_t w, std::uint32_t h, bool depthBorrowed, unsigned look);
const char* LastSubmit();                         // one-line description of the last recorded frame
void OnImageDestroyed(VkImage img);
// Marker share depth flags and the player's projected reference point (calibration log).
void SetDepthMode(std::uint32_t flags, const float* ref);   // {x,y,z, a,b, x2,y2,z2}
// The matrix this frame's markers were projected with, where the game keeps it, the game view
// {x, y, w, h} and the client size they were projected for. See Reproject in the .cpp.
// What is drawn next marks the game's interface: it stays in the pass at present time, over that
// interface, when the world markers go into the game's frame under it.
void SetTopLayer(bool on);
void SetViewInfo(const float* m16, std::uint64_t addr, const std::int32_t* gv, int cw, int ch);
void SetDepth(const float* z4);                   // per-command clip-space depths; nullptr = none
// Capture channel (rtx::capture::Share*); nullptr detaches.
void SetCaptureShare(void* share);

// Select the image about to be presented. False when the swapchain is unknown or its fence is stuck.
bool BeginTarget(VkSwapchainKHR sc, std::uint32_t imageIndex, std::uint32_t* w, std::uint32_t* h);
// Record and submit whatever was drawn since BeginTarget(). Returns the semaphore the present must
// wait on, or VK_NULL_HANDLE when nothing was recorded (the caller then leaves the present untouched).
VkSemaphore Submit(VkQueue queue, std::uint32_t waitCount, const VkSemaphore* waits);

void Begin();
void End();

void DrawSolidRect(int x, int y, int w, int h,
                   float r, float g, float b, float a, int fb_w, int fb_h);
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

}  // namespace rtx::vkcomposite
