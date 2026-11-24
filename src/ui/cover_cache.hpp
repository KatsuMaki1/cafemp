#ifndef UI_COVER_CACHE_HPP
#define UI_COVER_CACHE_HPP

// Async HTTP image downloader -> GX2Texture cache for ImGui poster cards.

#include <imgui/imgui.h>
#include <string>

namespace ui_covers {

// Returns an ImTextureID for `url` if already downloaded & uploaded to GX2,
// or 0 while queued/downloading/failed. Must be called from the UI thread.
ImTextureID get(const std::string &url);

// Clears all cached GX2 textures and stops the background loader.
void shutdown();

} // namespace ui_covers

#endif

