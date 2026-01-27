#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include "runtime/ui_tree.h"
#include "coi/native/webcc.h"

namespace coi::native {

bool asset_debug_enabled();

std::optional<std::filesystem::path> try_get_executable_path();
std::filesystem::path resolve_existing_path(const std::filesystem::path& in);
bool read_file_bytes(const char* path, std::vector<unsigned char>& out);

struct NativeImageInfo {
    int w = 0;
    int h = 0;
    // Optional backend-defined payload.
    const void* image_data = nullptr;
};

const NativeImageInfo* native_image_get_or_load(const char* src);
void native_image_cache_shutdown();

// Native event dispatch hooks (set by generated app code).
// The dispatcher is expected to return true when the event was handled.
extern webcc::function<bool(webcc::handle)> g_click_dispatcher;
void set_click_dispatcher(webcc::function<bool(webcc::handle)> cb);

bool dispatch_click_bubble(webcc::handle start);

// Flush hooks for headless runtime (tree dump + optional layout dump/click simulation).
void flush();

const webcc::string* attr(const coi::ui::Node& n, const char* key);

uint32_t hash_u32(const char* s);
void color_from_hash(uint32_t h, float& r, float& g, float& b);

} // namespace coi::native
