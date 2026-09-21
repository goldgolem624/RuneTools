#pragma once
// Unified-window host: one host window per game client; all entry points run on the AppCore main thread.

#include <cstdint>
#include <string>
#include <vector>

namespace ultralight { class App; }

namespace rtx::launcher::dock {

void Init(ultralight::App* app, std::string client_html_path, bool uiDevWatch = false);

std::string BuildClientHtml();

void EnsureClient(std::uint32_t pid);

void RemoveClient(std::uint32_t pid);

bool IsOpen(std::uint32_t pid);

void* GameWindowHandle(std::uint32_t pid);

bool GameFocused(std::uint32_t pid);

double GameSpaceFactor(void* gameHwnd);
double GameSpaceFactor(void* gameHwnd, std::uint32_t pid);

void PublishGameClientSize(std::uint32_t pid, int w, int h);

void SetHostFullscreen(std::uint32_t pid, bool on);
void SetUiScaleMultiplier(std::uint32_t pid, double mul);   // Preferences UI scale (1.0 = DPI only)
bool IsHostFullscreen(std::uint32_t pid);

void SetKeepFocused(std::uint32_t pid, bool on);

std::string ReadUiAsset(const std::string& name);

// Every file the client page is assembled from, relative to the install folder.
std::vector<std::string> UiAssetFiles();

void Tick();

void Shutdown();

}  // namespace rtx::launcher::dock
