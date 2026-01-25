#include "coi/native/runtime_api.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#if defined(_WIN32)
#include <windows.h>
#endif

#include "webcc/core/handle.h"
#include "webcc/core/string_view.h"
#include "webcc/core/string.h"
#include "webcc/core/function.h"
#include "webcc/core/allocator.h"
#include "webcc/core/new.h"
#include "webcc/core/array.h"
#include "webcc/core/vector.h"
#include "webcc/core/random.h"

// Desktop backend runtime for Coi.
// - Provides a retained-tree "UI" in namespace coi::ui (used by codegen).
// - Optionally provides a Sokol window renderer under COI_NATIVE_SOKOL.

namespace coi::ui {
struct Attr {
    webcc::string key;
    webcc::string value;
};
struct Node {
    webcc::string tag;
    webcc::string text;
    webcc::handle parent;
    std::vector<int32_t> children;
    std::vector<Attr> attrs;
};

std::unordered_map<int32_t, Node> g_nodes;
int32_t g_next_handle = 0x100000;
bool g_dumped = false;

static Node& ensure_node(webcc::handle h) {
    int32_t id = (int32_t)h;
    auto it = g_nodes.find(id);
    if (it == g_nodes.end()) {
        Node n;
        n.parent = webcc::handle();
        it = g_nodes.emplace(id, std::move(n)).first;
    }
    return it->second;
}

webcc::handle next_deferred_handle() {
    return webcc::handle(g_next_handle++);
}

webcc::handle get_body() {
    auto& b = ensure_node(webcc::handle(0));
    if (b.tag.empty()) b.tag = "body";
    return webcc::handle(0);
}

void flush() {
    const char* env = std::getenv("COI_NATIVE_DUMP");
    if (!env || !*env) return;
    if (env[0] == '0' && env[1] == '\0') return;
    if (g_dumped && std::string(env) != std::string("always")) return;
    g_dumped = true;
    std::cout << "--- COI_NATIVE_DUMP ---\n";

    // Dump a simple tree snapshot to stdout.
    auto dump = [&](auto&& self, int32_t id, int depth) -> void {
        auto it = g_nodes.find(id);
        if (it == g_nodes.end()) return;
        const Node& n = it->second;
        for (int i = 0; i < depth; i++) std::cout << "  ";
        std::cout << "<" << n.tag.c_str();
        for (const auto& a : n.attrs) {
            std::cout << " " << a.key.c_str() << "=\"" << a.value.c_str() << "\"";
        }
        std::cout << ">";
        if (!n.text.empty()) std::cout << n.text.c_str();
        std::cout << "</" << n.tag.c_str() << ">\n";
        for (int32_t c : n.children) self(self, c, depth + 1);
    };
    dump(dump, 0, 0);
    std::cout << std::flush;
}

void create_element_deferred(webcc::handle h, webcc::string_view tag) {
    auto& n = ensure_node(h);
    n.tag = webcc::string(tag.data(), tag.length());
}
void create_comment_deferred(webcc::handle h, webcc::string_view text) {
    auto& n = ensure_node(h);
    n.tag = "comment";
    n.text = webcc::string(text.data(), text.length());
}
void set_attribute(webcc::handle h, webcc::string_view name, webcc::string_view value) {
    auto& n = ensure_node(h);
    webcc::string k(name.data(), name.length());
    webcc::string v(value.data(), value.length());
    for (auto& a : n.attrs) {
        if (a.key == k) {
            a.value = v;
            return;
        }
    }
    n.attrs.push_back(Attr{std::move(k), std::move(v)});
}
void set_property(webcc::handle h, webcc::string_view name, webcc::string_view value) {
    set_attribute(h, name, value);
}
void set_inner_html(webcc::handle h, webcc::string_view html) {
    auto& n = ensure_node(h);
    n.children.clear();
    n.text = webcc::string(html.data(), html.length());
}
void set_inner_text(webcc::handle h, webcc::string_view text) {
    auto& n = ensure_node(h);
    n.children.clear();
    n.text = webcc::string(text.data(), text.length());
}
void append_child(webcc::handle parent, webcc::handle child) {
    auto& p = ensure_node(parent);
    auto& c = ensure_node(child);
    c.parent = parent;
    p.children.push_back((int32_t)child);
}
void insert_before(webcc::handle parent, webcc::handle child, webcc::handle ref) {
    auto& p = ensure_node(parent);
    auto& c = ensure_node(child);
    c.parent = parent;
    int32_t ref_id = (int32_t)ref;
    if (ref_id == 0) {
        p.children.push_back((int32_t)child);
        return;
    }
    auto it = std::find(p.children.begin(), p.children.end(), ref_id);
    if (it == p.children.end()) {
        p.children.push_back((int32_t)child);
        return;
    }
    p.children.insert(it, (int32_t)child);
}
void remove_element(webcc::handle h) {
    auto& n = ensure_node(h);
    if (!n.parent.is_valid()) return;
    auto& p = ensure_node(n.parent);
    int32_t id = (int32_t)h;
    p.children.erase(std::remove(p.children.begin(), p.children.end(), id), p.children.end());
    n.parent = webcc::handle();
}
void move_before(webcc::handle parent, webcc::handle node, webcc::handle ref) {
    auto& p = ensure_node(parent);
    int32_t node_id = (int32_t)node;
    int32_t ref_id = (int32_t)ref;
    p.children.erase(std::remove(p.children.begin(), p.children.end(), node_id), p.children.end());
    if (ref_id == 0) {
        p.children.push_back(node_id);
        return;
    }
    auto it = std::find(p.children.begin(), p.children.end(), ref_id);
    if (it == p.children.end()) {
        p.children.push_back(node_id);
        return;
    }
    p.children.insert(it, node_id);
}

void add_click_listener(webcc::handle) {}
void add_input_listener(webcc::handle) {}
void add_change_listener(webcc::handle) {}
void add_keydown_listener(webcc::handle) {}
void scroll_to_top() {}
} // namespace coi::ui

#if defined(COI_NATIVE_SOKOL)
#ifndef COI_NATIVE_RUNTIME_SOKOL_INCLUDED
#define COI_NATIVE_RUNTIME_SOKOL_INCLUDED
    #define SOKOL_NO_ENTRY
    #define SOKOL_GLCORE
    #define SOKOL_IMPL
    #define SOKOL_GL_IMPL
    #define SOKOL_DEBUGTEXT_IMPL
    #if defined(COI_NATIVE_FONTSTASH)
        #define FONTSTASH_IMPLEMENTATION
    #endif
    #if defined(COI_NATIVE_CAPTURE)
        #define STB_IMAGE_WRITE_STATIC
        #define STB_IMAGE_WRITE_IMPLEMENTATION
    #endif
    #include "sokol_app.h"
    #include "sokol_gfx.h"
    #include "sokol_gl.h"
    #include "sokol_glue.h"
    #include "sokol_time.h"
    #include "sokol_debugtext.h"
	    #if defined(COI_NATIVE_FONTSTASH)
	        #include "fontstash.h"
	        #include "sokol_fontstash.h"
	    #endif
	    #ifndef COI_NATIVE_RUNTIME_STB_IMAGE_INCLUDED
	    #define COI_NATIVE_RUNTIME_STB_IMAGE_INCLUDED
	        #define STB_IMAGE_STATIC
	        #define STB_IMAGE_IMPLEMENTATION
	        #include "stb_image.h"
	    #endif
	    #if defined(COI_NATIVE_CAPTURE)
	        #include "stb_image_write.h"
	    #endif
#endif

#if defined(COI_NATIVE_CLAY)
#ifndef COI_NATIVE_RUNTIME_CLAY_INCLUDED
#define COI_NATIVE_RUNTIME_CLAY_INCLUDED
    #define CLAY_IMPLEMENTATION
    #include "clay.h"
#endif
#endif

#if defined(COI_NATIVE_SOKOL) && defined(COI_NATIVE_CLAY) && defined(COI_NATIVE_FONTSTASH)
#ifndef COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED
#define COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED
    // Clay's reference sokol renderer + fontstash-based text measurement.
    #define SOKOL_CLAY_IMPL
    #include "renderers/sokol/sokol_clay.h"
#endif
#endif

namespace coi::native {
struct Rect {
    float x, y, w, h;
};

// Used to avoid running desktop scripts/simulated input in the "pre-run" flush
// that happens before sapp_run() starts when window/capture is enabled.
inline bool g_sokol_frame_started = false;

	inline bool asset_debug_enabled() {
	    const char* e = std::getenv("COI_NATIVE_ASSET_DEBUG");
	    if (!e || !*e) return false;
	    return !(e[0] == '0' && e[1] == '\0');
	}

	inline std::optional<std::filesystem::path> try_get_executable_path() {
#if defined(__linux__)
	    std::array<char, 4096> buf{};
	    ssize_t n = ::readlink("/proc/self/exe", buf.data(), buf.size() - 1);
	    if (n > 0) {
	        buf[(size_t)n] = '\0';
	        return std::filesystem::path(buf.data());
	    }
#elif defined(__APPLE__)
	    std::array<char, 4096> buf{};
	    uint32_t size = (uint32_t)buf.size();
	    if (_NSGetExecutablePath(buf.data(), &size) == 0) {
	        return std::filesystem::path(buf.data());
	    }
#elif defined(_WIN32)
	    std::array<char, 4096> buf{};
	    DWORD n = GetModuleFileNameA(nullptr, buf.data(), (DWORD)buf.size());
	    if (n > 0 && n < buf.size()) {
	        buf[(size_t)n] = '\0';
	        return std::filesystem::path(buf.data());
	    }
#endif
	    return std::nullopt;
	}

	inline std::filesystem::path resolve_existing_path(const std::filesystem::path& in) {
	    std::filesystem::path p = in.lexically_normal();
	    std::error_code ec;
	    if (p.is_absolute()) {
	        if (std::filesystem::exists(p, ec)) return p;
	        return {};
	    }

	    if (std::filesystem::exists(p, ec)) return p;

	    if (const char* root = std::getenv("COI_NATIVE_ASSET_ROOT"); root && *root) {
	        std::filesystem::path cand = std::filesystem::path(root) / p;
	        cand = cand.lexically_normal();
	        if (std::filesystem::exists(cand, ec)) {
	            if (asset_debug_enabled()) {
	                std::cerr << "[asset] resolved " << p.string() << " via COI_NATIVE_ASSET_ROOT to " << cand.string() << "\n";
	            }
	            return cand;
	        }
	    }

	    auto search_up_from = [&](std::filesystem::path base) -> std::filesystem::path {
	        for (int i = 0; i < 14; i++) {
	            std::filesystem::path cand = (base / p).lexically_normal();
	            if (std::filesystem::exists(cand, ec)) {
	                if (asset_debug_enabled()) {
	                    std::cerr << "[asset] resolved " << p.string() << " via search-up from " << base.string() << " to " << cand.string() << "\n";
	                }
	                return cand;
	            }
	            if (!base.has_parent_path()) break;
	            std::filesystem::path parent = base.parent_path();
	            if (parent == base) break;
	            base = parent;
	        }
	        return {};
	    };

	    if (auto exe = try_get_executable_path()) {
	        std::filesystem::path found = search_up_from(exe->parent_path());
	        if (!found.empty()) return found;
	    }

	    std::filesystem::path cwd = std::filesystem::current_path(ec);
	    if (!ec && !cwd.empty()) {
	        std::filesystem::path found = search_up_from(cwd);
	        if (!found.empty()) return found;
	    }

	    return {};
	}

	inline bool read_file_bytes(const char* path, std::vector<unsigned char>& out) {
	    out.clear();
	    if (!path || !*path) return false;
	    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streampos size = f.tellg();
    if (size <= 0) return false;
    out.resize((size_t)size);
    f.seekg(0, std::ios::beg);
    f.read((char*)out.data(), size);
	    return (bool)f;
	}

#if defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
	struct DesktopImage {
	    int w = 0;
	    int h = 0;
	    sg_image img{};
	    sg_view view{};
	    sclay_image scl{};
	};

	struct DesktopImageCache {
	    static inline std::unordered_map<std::string, std::unique_ptr<DesktopImage>> images;

	    static const DesktopImage* get_or_load(const char* src) {
	        if (!src || !*src) return nullptr;
	        std::error_code ec;
	        std::filesystem::path p(src);
	        p = p.lexically_normal();
	        std::filesystem::path resolved = resolve_existing_path(p);
	        const std::filesystem::path& load_path = resolved.empty() ? p : resolved;
	        const std::string key = load_path.string();

	        auto it = images.find(key);
	        if (it != images.end()) return it->second.get();

	        int w = 0, h = 0, n = 0;
	        unsigned char* rgba = stbi_load(key.c_str(), &w, &h, &n, 4);
	        if (!rgba || w <= 0 || h <= 0) {
	            std::cerr << "[img] failed to load " << key << "\n";
	            if (rgba) stbi_image_free(rgba);
	            images.emplace(key, nullptr);
	            return nullptr;
	        }

	        auto img = std::make_unique<DesktopImage>();
	        img->w = w;
	        img->h = h;

	        sg_image_desc img_desc{};
	        img_desc.width = w;
	        img_desc.height = h;
	        img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
	        img_desc.data.mip_levels[0].ptr = rgba;
	        img_desc.data.mip_levels[0].size = (size_t)w * (size_t)h * 4u;
	        img_desc.label = "coi-image";
	        img->img = sg_make_image(&img_desc);
	        stbi_image_free(rgba);

	        if (img->img.id == SG_INVALID_ID) {
	            std::cerr << "[img] sg_make_image failed for " << key << "\n";
	            images.emplace(key, nullptr);
	            return nullptr;
	        }

	        sg_view_desc view_desc{};
	        view_desc.texture.image = img->img;
	        view_desc.label = "coi-image-view";
	        img->view = sg_make_view(&view_desc);
	        if (img->view.id == SG_INVALID_ID) {
	            std::cerr << "[img] sg_make_view failed for " << key << "\n";
	            sg_destroy_image(img->img);
	            images.emplace(key, nullptr);
	            return nullptr;
	        }

	        img->scl.view = img->view;
	        img->scl.sampler = sg_sampler{};
	        img->scl.uv.u0 = 0.0f;
	        img->scl.uv.v0 = 0.0f;
	        img->scl.uv.u1 = 1.0f;
	        img->scl.uv.v1 = 1.0f;

	        const DesktopImage* out = img.get();
	        images.emplace(key, std::move(img));
	        return out;
	    }

	    static void shutdown() {
	        for (auto& kv : images) {
	            if (!kv.second) continue;
	            if (kv.second->view.id != SG_INVALID_ID) {
	                sg_destroy_view(kv.second->view);
	                kv.second->view.id = SG_INVALID_ID;
	            }
	            if (kv.second->img.id != SG_INVALID_ID) {
	                sg_destroy_image(kv.second->img);
	                kv.second->img.id = SG_INVALID_ID;
	            }
	        }
	        images.clear();
	    }
	};
#endif

	// Desktop event dispatch hooks (set by generated app code).
	// The dispatcher is expected to return true when the event was handled.
webcc::function<bool(webcc::handle)> g_click_dispatcher;
void set_click_dispatcher(webcc::function<bool(webcc::handle)> cb) {
    g_click_dispatcher = std::move(cb);
}

inline bool dispatch_click_bubble(webcc::handle start) {
    if (!g_click_dispatcher) return false;
    webcc::handle h = start;
    while (h.is_valid()) {
        if (g_click_dispatcher(h)) return true;
        auto it = coi::ui::g_nodes.find((int32_t)h);
        if (it == coi::ui::g_nodes.end()) break;
        h = it->second.parent;
    }
    return false;
}

// Flush hooks for headless runtime (tree dump + optional layout dump/click simulation).
void flush();

inline const webcc::string* attr(const coi::ui::Node& n, const char* key) {
    for (const auto& a : n.attrs) {
        if (a.key == key) return &a.value;
    }
    return nullptr;
}

inline uint32_t hash_u32(const char* s) {
    uint32_t h = 2166136261u;
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        h ^= *p;
        h *= 16777619u;
    }
    return h;
}

inline void color_from_hash(uint32_t h, float& r, float& g, float& b) {
    r = 0.25f + ((h & 0xFF) / 255.0f) * 0.65f;
    g = 0.25f + (((h >> 8) & 0xFF) / 255.0f) * 0.65f;
    b = 0.25f + (((h >> 16) & 0xFF) / 255.0f) * 0.65f;
}

#if defined(COI_NATIVE_CLAY)
	struct DesktopClassStyle {
	    bool has_dir = false;
	    Clay_LayoutDirection dir = CLAY_TOP_TO_BOTTOM;

	    bool has_opacity = false;
	    float opacity = 1.0f; // 0..1

	    bool has_floating = false;
	    Clay_FloatingAttachToElement floating_attach_to = CLAY_ATTACH_TO_NONE;
	    Clay_FloatingClipToElement floating_clip_to = CLAY_CLIP_TO_NONE;
	    Clay_PointerCaptureMode floating_pointer_mode = CLAY_POINTER_CAPTURE_MODE_CAPTURE;
	    Clay_FloatingAttachPoints floating_attach_points = Clay_FloatingAttachPoints{CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_TOP};
	    bool has_float_x = false;
	    float float_x = 0.0f;
	    bool has_float_y = false;
	    float float_y = 0.0f;
	    bool has_z_index = false;
	    int16_t z_index = 0;

	    bool has_pad = false;
	    uint16_t pad = 0;

    bool has_gap = false;
    uint16_t gap = 0;

    bool w_fixed = false;
    float w = 0.0f;

    bool h_fixed = false;
    float h = 0.0f;

    bool w_grow = false;
    bool h_grow = false;

    bool w_has_min = false;
    float w_min = 0.0f;
    bool w_has_max = false;
    float w_max = 0.0f;

    bool h_has_min = false;
    float h_min = 0.0f;
    bool h_has_max = false;
    float h_max = 0.0f;

    bool bg_none = false;

    bool has_clip = false;
    // These map directly to Clay_ClipElementConfig horizontal/vertical clip flags.
    // scroll_x/scroll_y are a COI runtime convention: only when one of these is true do we
    // apply Clay's scroll offset to decl.clip.childOffset (enabling scrolling).
    bool clip_x = false;
    bool clip_y = false;
    bool scroll_x = false;
    bool scroll_y = false;

    bool has_border = false;
    Clay_BorderWidth border_width = Clay_BorderWidth{0, 0, 0, 0, 0};

    bool has_corner_radius = false;
    Clay_CornerRadius corner_radius = Clay_CornerRadius{0, 0, 0, 0};

    bool has_align_x = false;
    Clay_LayoutAlignmentX align_x = CLAY_ALIGN_X_LEFT;

    bool has_align_y = false;
    Clay_LayoutAlignmentY align_y = CLAY_ALIGN_Y_TOP;

    // Text styling (applies when the node contains a text payload).
    bool has_font_size = false;
    int32_t font_size = 8;

    bool has_letter_spacing = false;
    int32_t letter_spacing = 0;

    bool has_line_height = false;
    int32_t line_height = 8;

    bool has_text_align = false;
    Clay_TextAlignment text_align = CLAY_TEXT_ALIGN_LEFT;
};

inline bool _parse_u16(const char* s, uint16_t& out) {
    if (!s || !*s) return false;
    int v = 0;
    for (const char* p = s; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        v = v * 10 + (*p - '0');
        if (v > 65535) return false;
    }
    out = (uint16_t)v;
    return true;
}

inline bool _parse_f32(const char* s, float& out) {
    if (!s || !*s) return false;
    char* end = nullptr;
    out = std::strtof(s, &end);
    return end && end != s;
}

inline void _for_each_class_token(const webcc::string& s, const webcc::function<void(const char* tok, int len)>& fn) {
    const char* p = s.c_str();
    int n = (int)std::strlen(p);
    int i = 0;
    while (i < n) {
        while (i < n && (p[i] == ' ' || p[i] == '\t' || p[i] == '\n' || p[i] == '\r')) i++;
        if (i >= n) break;
        int j = i;
        while (j < n && p[j] != ' ' && p[j] != '\t' && p[j] != '\n' && p[j] != '\r') j++;
        fn(p + i, j - i);
        i = j;
    }
}

	inline DesktopClassStyle parse_desktop_class_style(const coi::ui::Node& n, bool is_root) {
	    DesktopClassStyle st{};
	    const webcc::string* cls = attr(n, "class");
	    if (!cls) return st;

	    _for_each_class_token(*cls, webcc::function<void(const char*, int)>([&](const char* tok, int len) {
	        std::string t(tok, (size_t)len);
	        {
	            uint16_t ov = 0;
	            if (t.rfind("op-", 0) == 0 && _parse_u16(t.c_str() + 3, ov)) {
	                st.has_opacity = true;
	                st.opacity = std::clamp((float)ov / 255.0f, 0.0f, 1.0f);
	                return;
	            }
	            if (t.rfind("opacity-", 0) == 0 && _parse_u16(t.c_str() + 8, ov)) {
	                st.has_opacity = true;
	                st.opacity = std::clamp((float)ov / 100.0f, 0.0f, 1.0f);
	                return;
	            }
	        }
	        if (t == "float" || t == "floating") {
	            st.has_floating = true;
	            st.floating_attach_to = CLAY_ATTACH_TO_ROOT;
	            return;
	        }
	        if (t == "float-parent") {
	            st.has_floating = true;
	            st.floating_attach_to = CLAY_ATTACH_TO_PARENT;
	            return;
	        }
	        if (t == "float-pass") {
	            st.has_floating = true;
	            st.floating_pointer_mode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH;
	            return;
	        }
	        if (t == "float-clip") {
	            st.has_floating = true;
	            st.floating_clip_to = CLAY_CLIP_TO_ATTACHED_PARENT;
	            return;
	        }
	        if (t == "text-left") {
	            st.has_text_align = true;
	            st.text_align = CLAY_TEXT_ALIGN_LEFT;
	            return;
	        }
        if (t == "text-center") {
            st.has_text_align = true;
            st.text_align = CLAY_TEXT_ALIGN_CENTER;
            return;
        }
        if (t == "text-right") {
            st.has_text_align = true;
            st.text_align = CLAY_TEXT_ALIGN_RIGHT;
            return;
        }
        if (t == "row") {
            st.has_dir = true;
            st.dir = CLAY_LEFT_TO_RIGHT;
            return;
        }
        if (t == "col") {
            st.has_dir = true;
            st.dir = CLAY_TOP_TO_BOTTOM;
            return;
        }
        if (t == "bg-none") {
            st.bg_none = true;
            return;
        }
        if (t == "clip") {
            st.has_clip = true;
            st.clip_x = true;
            st.clip_y = true;
            return;
        }
        if (t == "clip-x") {
            st.has_clip = true;
            st.clip_x = true;
            return;
        }
        if (t == "clip-y") {
            st.has_clip = true;
            st.clip_y = true;
            return;
        }
        if (t == "scroll") {
            st.has_clip = true;
            st.scroll_x = true;
            st.scroll_y = true;
            st.clip_x = true;
            st.clip_y = true;
            return;
        }
        if (t == "scroll-x") {
            st.has_clip = true;
            st.scroll_x = true;
            // Scroll-x implies the other axis is clipped (overflow hidden).
            st.clip_x = true;
            st.clip_y = true;
            return;
        }
        if (t == "scroll-y") {
            st.has_clip = true;
            st.scroll_y = true;
            // Scroll-y implies the other axis is clipped (overflow hidden).
            st.clip_x = true;
            st.clip_y = true;
            return;
        }
        if (t == "border") {
            st.has_border = true;
            st.border_width = Clay_BorderWidth{1, 1, 1, 1, st.border_width.betweenChildren};
            return;
        }
        if (t == "border-none") {
            st.has_border = false;
            st.border_width = Clay_BorderWidth{0, 0, 0, 0, 0};
            return;
        }
        if (t == "rounded") {
            st.has_corner_radius = true;
            st.corner_radius = CLAY_CORNER_RADIUS(8);
            return;
        }
        if (t == "grow") {
            st.w_grow = true;
            return;
        }
        if (t == "grow-x") {
            st.w_grow = true;
            return;
        }
        if (t == "grow-y") {
            st.h_grow = true;
            return;
        }
        if (t == "fill") {
            st.w_grow = true;
            st.h_grow = true;
            return;
        }
        if (t == "center") {
            st.has_align_x = true;
            st.has_align_y = true;
            st.align_x = CLAY_ALIGN_X_CENTER;
            st.align_y = CLAY_ALIGN_Y_CENTER;
            return;
        }
        if (t == "x-center") {
            st.has_align_x = true;
            st.align_x = CLAY_ALIGN_X_CENTER;
            return;
        }
        if (t == "x-right") {
            st.has_align_x = true;
            st.align_x = CLAY_ALIGN_X_RIGHT;
            return;
        }
        if (t == "y-center") {
            st.has_align_y = true;
            st.align_y = CLAY_ALIGN_Y_CENTER;
            return;
        }
        if (t == "y-bottom") {
            st.has_align_y = true;
            st.align_y = CLAY_ALIGN_Y_BOTTOM;
            return;
        }

        auto parse_u16_suffix = [&](const char* prefix, uint16_t& out, bool& flag) {
            size_t plen = std::strlen(prefix);
            if (t.size() <= plen) return false;
            if (t.compare(0, plen, prefix) != 0) return false;
            uint16_t v = 0;
            if (!_parse_u16(t.c_str() + plen, v)) return false;
            out = v;
            flag = true;
            return true;
        };
	        auto parse_i32_from_u16_suffix = [&](const char* prefix, int32_t& out, bool& flag) {
	            uint16_t v = 0;
	            if (!parse_u16_suffix(prefix, v, flag)) return false;
	            out = (int32_t)v;
	            return true;
	        };
	        auto parse_i16_from_u16_suffix = [&](const char* prefix, int16_t& out, bool& flag) {
	            uint16_t v = 0;
	            if (!parse_u16_suffix(prefix, v, flag)) return false;
	            out = (int16_t)std::min<uint16_t>(v, 32767);
	            flag = true;
	            return true;
	        };
	        auto parse_f32_suffix = [&](const char* prefix, float& out, bool& flag) {
	            size_t plen = std::strlen(prefix);
	            if (t.size() <= plen) return false;
	            if (t.compare(0, plen, prefix) != 0) return false;
            float v = 0.0f;
            if (!_parse_f32(t.c_str() + plen, v)) return false;
            out = v;
            flag = true;
            return true;
        };

        if (parse_u16_suffix("pad-", st.pad, st.has_pad)) return;
        if (parse_u16_suffix("p-", st.pad, st.has_pad)) return;
        if (parse_u16_suffix("gap-", st.gap, st.has_gap)) return;
        if (parse_u16_suffix("g-", st.gap, st.has_gap)) return;
        if (parse_i32_from_u16_suffix("fs-", st.font_size, st.has_font_size)) return;
        if (parse_i32_from_u16_suffix("ls-", st.letter_spacing, st.has_letter_spacing)) return;
        if (parse_i32_from_u16_suffix("lh-", st.line_height, st.has_line_height)) return;
        {
            uint16_t rv = 0;
            if (parse_u16_suffix("r-", rv, st.has_corner_radius)) {
                st.corner_radius = CLAY_CORNER_RADIUS((float)rv);
                return;
            }
            if (parse_u16_suffix("r-tl-", rv, st.has_corner_radius)) {
                st.corner_radius.topLeft = (float)rv;
                return;
            }
            if (parse_u16_suffix("r-tr-", rv, st.has_corner_radius)) {
                st.corner_radius.topRight = (float)rv;
                return;
            }
            if (parse_u16_suffix("r-bl-", rv, st.has_corner_radius)) {
                st.corner_radius.bottomLeft = (float)rv;
                return;
            }
            if (parse_u16_suffix("r-br-", rv, st.has_corner_radius)) {
                st.corner_radius.bottomRight = (float)rv;
                return;
            }
        }
	        if (parse_f32_suffix("w-", st.w, st.w_fixed)) return;
	        if (parse_f32_suffix("h-", st.h, st.h_fixed)) return;
	        if (parse_f32_suffix("min-w-", st.w_min, st.w_has_min)) return;
	        if (parse_f32_suffix("max-w-", st.w_max, st.w_has_max)) return;
	        if (parse_f32_suffix("min-h-", st.h_min, st.h_has_min)) return;
	        if (parse_f32_suffix("max-h-", st.h_max, st.h_has_max)) return;
	        if (parse_f32_suffix("fx-", st.float_x, st.has_float_x)) return;
	        if (parse_f32_suffix("fy-", st.float_y, st.has_float_y)) return;
	        if (parse_i16_from_u16_suffix("z-", st.z_index, st.has_z_index)) return;

	        uint16_t bw = 0;
	        if (parse_u16_suffix("border-", bw, st.has_border)) {
	            st.border_width.left = bw;
            st.border_width.right = bw;
            st.border_width.top = bw;
            st.border_width.bottom = bw;
            return;
        }
        if (parse_u16_suffix("border-between-", st.border_width.betweenChildren, st.has_border)) return;
    }));

    if (is_root) {
        // If root asked for grow, treat it as fill.
        if (st.w_grow) st.h_grow = true;
    }
    return st;
}

struct ClayEngine {
    static inline Clay_Context* ctx = nullptr;
    static inline void* mem = nullptr;
    static inline size_t mem_size = 0;
    static inline Clay_TextElementConfig* text_cfg = nullptr;
    static inline Clay_Vector2 pointer_pos = Clay_Vector2{0, 0};
    static inline bool pointer_down = false;
    // Clay_UpdateScrollContainers expects scroll deltas where negative Y scrolls "down" (because scrollPosition is clamped <= 0).
    static inline Clay_Vector2 scroll_delta = Clay_Vector2{0, 0};
    static inline float frame_dt = 1.0f / 60.0f;
    static inline bool enable_drag_scroll = true;

    static void error_handler(Clay_ErrorData data) {
        std::cerr << "[Clay] error " << (int)data.errorType << ": ";
        if (data.errorText.chars && data.errorText.length > 0) {
            std::cerr.write(data.errorText.chars, data.errorText.length);
        } else {
            std::cerr << "(no message)";
        }
        std::cerr << std::endl;
    }

    static Clay_Dimensions measure_text(Clay_StringSlice text, Clay_TextElementConfig* config, void*) {
        const float font_size = config ? (float)config->fontSize : 8.0f;
        const float letter_spacing = config ? (float)config->letterSpacing : 0.0f;
        const float line_h = (config && config->lineHeight) ? (float)config->lineHeight : font_size;
        const float char_w = font_size + letter_spacing;
        return Clay_Dimensions{(float)text.length * char_w, line_h};
    }

    static uint32_t id_from_handle(int32_t h) {
        return 0xC01D0000u ^ (uint32_t)h;
    }

    static Clay_ElementId element_id(int32_t h) {
        return Clay_ElementId{.id = id_from_handle(h)};
    }

    static Clay_String clay_string(const webcc::string& s) {
        return Clay_String{false, (int32_t)std::strlen(s.c_str()), s.c_str()};
    }

    static void ensure(float w, float h) {
        if (ctx) {
            Clay_SetCurrentContext(ctx);
            return;
        }
        uint32_t min_bytes = Clay_MinMemorySize();
        mem_size = (size_t)min_bytes + (size_t)min_bytes / 2 + 64 * 1024;
        mem = std::malloc(mem_size);
        if (!mem) {
            std::cerr << "[Clay] failed to allocate " << mem_size << " bytes\n";
            return;
        }
        Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(mem_size, mem);
        ctx = Clay_Initialize(arena, Clay_Dimensions{w, h}, Clay_ErrorHandler{error_handler, nullptr});
        Clay_SetCurrentContext(ctx);
        Clay_SetMeasureTextFunction(measure_text, nullptr);
        text_cfg = CLAY_TEXT_CONFIG({
            .textColor = {235, 235, 240, 255},
            .fontId = 0,
            .fontSize = 8,
            .letterSpacing = 0,
            .lineHeight = 8,
            .wrapMode = CLAY_TEXT_WRAP_WORDS,
            .textAlignment = CLAY_TEXT_ALIGN_LEFT,
        });
    }

    static void set_input(float x, float y, bool down, float scroll_x, float scroll_y, float dt) {
        pointer_pos = Clay_Vector2{x, y};
        pointer_down = down;
        scroll_delta.x += scroll_x;
        scroll_delta.y += scroll_y;
        if (dt > 0.0f) frame_dt = dt;
    }

    static void pre_layout_update() {
        Clay_SetPointerState(pointer_pos, pointer_down);
        Clay_UpdateScrollContainers(enable_drag_scroll, scroll_delta, frame_dt);
        scroll_delta = Clay_Vector2{0, 0};
    }

    static void shutdown() {
        // clay has no explicit shutdown; free the backing arena memory
        if (mem) {
            std::free(mem);
            mem = nullptr;
            mem_size = 0;
        }
        ctx = nullptr;
        text_cfg = nullptr;
    }

	    static Clay_ElementDeclaration declaration_for_node(const coi::ui::Node& n, bool is_root) {
	        DesktopClassStyle st = parse_desktop_class_style(n, is_root);
	        const bool is_img = (n.tag == "img");
	        const uint16_t default_pad = (is_root || is_img) ? 0 : 12;
	        const uint16_t default_gap = (is_root || is_img) ? 0 : 10;
	        const uint16_t pad = st.has_pad ? st.pad : default_pad;
	        const uint16_t gap = st.has_gap ? st.gap : default_gap;
	        const Clay_LayoutDirection dir = st.has_dir ? st.dir : CLAY_TOP_TO_BOTTOM;
	        const Clay_LayoutAlignmentX ax = st.has_align_x ? st.align_x : CLAY_ALIGN_X_LEFT;
	        const Clay_LayoutAlignmentY ay = st.has_align_y ? st.align_y : CLAY_ALIGN_Y_TOP;

	        Clay_SizingAxis sx = CLAY_SIZING_GROW(0);
	        Clay_SizingAxis sy = is_root ? CLAY_SIZING_GROW(0) : CLAY_SIZING_FIT(0);
	        if (st.w_fixed) sx = CLAY_SIZING_FIXED(st.w);
	        if (st.h_fixed) sy = CLAY_SIZING_FIXED(st.h);
	        if (st.w_grow) sx = CLAY_SIZING_GROW(0);
	        if (st.h_grow) sy = CLAY_SIZING_GROW(0);

	        const DesktopImage* img = nullptr;
#if defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
	        if (is_img) {
	            const webcc::string* src = attr(n, "src");
	            img = DesktopImageCache::get_or_load(src ? src->c_str() : nullptr);
	            if (img) {
	                if (!st.w_fixed && !st.w_grow) {
	                    sx = CLAY_SIZING_FIXED((float)img->w);
	                }
	                if (!st.h_fixed && !st.h_grow) {
	                    sy = CLAY_SIZING_FIXED((float)img->h);
	                }
	                if (st.w_fixed && !st.h_fixed && img->w > 0) {
	                    sy = CLAY_SIZING_FIXED(st.w * ((float)img->h / (float)img->w));
	                }
	                if (st.h_fixed && !st.w_fixed && img->h > 0) {
	                    sx = CLAY_SIZING_FIXED(st.h * ((float)img->w / (float)img->h));
	                }
	            }
	        }
#endif

	        // Apply optional min/max constraints (Clay uses minMax even for FIT/GROW/FIXED).
	        if (sx.type != CLAY__SIZING_TYPE_PERCENT) {
	            if (st.w_has_min) sx.size.minMax.min = std::max(0.0f, st.w_min);
	            if (st.w_has_max) sx.size.minMax.max = std::max(0.0f, st.w_max);
        }
        if (sy.type != CLAY__SIZING_TYPE_PERCENT) {
            if (st.h_has_min) sy.size.minMax.min = std::max(0.0f, st.h_min);
            if (st.h_has_max) sy.size.minMax.max = std::max(0.0f, st.h_max);
        }

        Clay_ElementDeclaration decl{};
        decl.layout = Clay_LayoutConfig{
            .sizing = Clay_Sizing{.width = sx, .height = sy},
            .padding = Clay_Padding{pad, pad, pad, pad},
            .childGap = gap,
            .childAlignment = Clay_ChildAlignment{ax, ay},
            .layoutDirection = dir,
        };
        if (st.has_clip) {
            decl.clip = Clay_ClipElementConfig{
                .horizontal = st.clip_x,
                .vertical = st.clip_y,
                // Note: for scroll containers, childOffset must be queried from inside the
                // CLAY() macro (after the element is opened). We'll patch it up in
                // declaration_for_node_open().
                .childOffset = Clay_Vector2{0, 0},
            };
        }
	        if (st.has_border && (st.border_width.left || st.border_width.right || st.border_width.top || st.border_width.bottom ||
	                              st.border_width.betweenChildren)) {
	            uint32_t h = hash_u32((attr(n, "class") ? attr(n, "class")->c_str() : n.tag.c_str()));
	            float cr, cg, cb;
	            color_from_hash(h, cr, cg, cb);
	            decl.border = Clay_BorderElementConfig{
	                .color = Clay_Color{cr * 255.0f, cg * 255.0f, cb * 255.0f, 180.0f},
	                .width = st.border_width,
	            };
	        }
	        if (is_img) {
	            if (st.has_opacity && st.opacity < 1.0f) {
	                decl.backgroundColor = Clay_Color{255, 255, 255, 255.0f * st.opacity};
	            } else {
	                decl.backgroundColor = Clay_Color{0, 0, 0, 0};
	            }
	        } else if (is_root || st.bg_none) {
	            decl.backgroundColor = Clay_Color{0, 0, 0, 0};
	        } else {
	            const webcc::string* cls = attr(n, "class");
	            uint32_t h = hash_u32(cls ? cls->c_str() : n.tag.c_str());
	            float cr, cg, cb;
	            color_from_hash(h, cr, cg, cb);
	            decl.backgroundColor = Clay_Color{cr * 255.0f, cg * 255.0f, cb * 255.0f, 46.0f};
	        }
	        if (!is_img && st.has_opacity && st.opacity < 1.0f) {
	            decl.backgroundColor.a *= st.opacity;
	            if (decl.border.width.left || decl.border.width.right || decl.border.width.top || decl.border.width.bottom ||
	                decl.border.width.betweenChildren) {
	                decl.border.color.a *= st.opacity;
	            }
	        }
	        if (st.has_corner_radius) {
	            decl.cornerRadius = st.corner_radius;
	        }
#if defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
	        if (is_img && img) {
	            decl.image = Clay_ImageElementConfig{.imageData = (void*)&img->scl};
	        }
#endif
	        if (st.has_floating && st.floating_attach_to != CLAY_ATTACH_TO_NONE) {
	            const float ox = st.has_float_x ? st.float_x : 0.0f;
	            const float oy = st.has_float_y ? st.float_y : 0.0f;
	            const int16_t zi = st.has_z_index ? st.z_index : 1;
	            decl.floating = Clay_FloatingElementConfig{
	                .offset = Clay_Vector2{ox, oy},
	                .expand = Clay_Dimensions{0, 0},
	                .parentId = 0,
	                .zIndex = zi,
	                .attachPoints = st.floating_attach_points,
	                .pointerCaptureMode = st.floating_pointer_mode,
	                .attachTo = st.floating_attach_to,
	                .clipTo = st.floating_clip_to,
	            };
	        }
	        return decl;
	    }

    static Clay_ElementDeclaration declaration_for_node(const coi::ui::Node& n, bool is_root, int32_t id) {
        Clay_ElementDeclaration decl = declaration_for_node(n, is_root);
        decl.userData = (void*)(intptr_t)id;
        return decl;
    }

    // Must be called from inside CLAY(...) so that Clay_GetScrollOffset() refers to the opened element.
    static Clay_ElementDeclaration declaration_for_node_open(const coi::ui::Node& n, bool is_root, int32_t id) {
        Clay_ElementDeclaration decl = declaration_for_node(n, is_root, id);
        DesktopClassStyle st = parse_desktop_class_style(n, is_root);
        if (st.has_clip && (st.scroll_x || st.scroll_y)) {
            // Clay_GetScrollOffset() returns the scroll offset by matching the current open
            // layout element pointer. At this point (between OpenElement and ConfigureOpenElement),
            // the cached scroll container mapping hasn't been updated to point at the new
            // per-frame layout element yet. Query by elementId instead.
            Clay_Context* c = Clay_GetCurrentContext();
            Clay_LayoutElement* open = Clay__GetOpenLayoutElement();
            Clay_Vector2 off{0, 0};
            if (c && open) {
                for (int32_t i = 0; i < c->scrollContainerDatas.length; i++) {
                    Clay__ScrollContainerDataInternal* mapping = Clay__ScrollContainerDataInternalArray_Get(&c->scrollContainerDatas, i);
                    if (mapping && mapping->elementId == open->id) {
                        off = mapping->scrollPosition;
                        break;
                    }
                }
            }
            decl.clip.childOffset = off;
        }
        return decl;
    }

    static void build_node(int32_t id, bool is_root) {
        auto it = coi::ui::g_nodes.find(id);
        if (it == coi::ui::g_nodes.end()) return;
        const auto& n = it->second;
        if (n.tag == "comment") return;

        Clay_ElementId eid = element_id(id);
        CLAY(eid, (ClayEngine::declaration_for_node_open(n, is_root, id))) {
	            if (!n.text.empty()) {
	                Clay_String t = clay_string(n.text);
	                Clay_TextElementConfig* cfgp = text_cfg;
	                if (cfgp) {
	                    Clay_TextElementConfig cfg = *cfgp;
	                    DesktopClassStyle st = parse_desktop_class_style(n, is_root);
	                    if (st.has_font_size) cfg.fontSize = (uint16_t)st.font_size;
	                    if (st.has_letter_spacing) cfg.letterSpacing = (uint16_t)st.letter_spacing;
	                    if (st.has_line_height) cfg.lineHeight = (uint16_t)st.line_height;
	                    if (st.has_text_align) cfg.textAlignment = st.text_align;
	                    if (st.has_opacity && st.opacity < 1.0f) cfg.textColor.a *= st.opacity;
	                    cfg.userData = (void*)(intptr_t)id;
	                    cfgp = Clay__StoreTextElementConfig(cfg);
	                }
	                CLAY_TEXT(t, cfgp);
	            }
            for (int32_t c : n.children) build_node(c, false);
        }
    }

    static Clay_RenderCommandArray layout(float w_px, float h_px, float dpi_scale = 1.0f) {
#if defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
        const float safe_dpi = (dpi_scale > 0.0f) ? dpi_scale : 1.0f;
        ensure(w_px / safe_dpi, h_px / safe_dpi);
#else
        (void)dpi_scale;
        ensure(w_px, h_px);
#endif
        if (!ctx) return Clay_RenderCommandArray{};
        Clay_SetCurrentContext(ctx);
#if defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
        sclay_set_layout_dimensions(Clay_Dimensions{w_px, h_px}, (dpi_scale > 0.0f) ? dpi_scale : 1.0f);
#else
        Clay_SetLayoutDimensions(Clay_Dimensions{w_px, h_px});
#endif
        pre_layout_update();
        Clay_BeginLayout();
        build_node(0, true);
        return Clay_EndLayout();
    }
};
#endif // COI_NATIVE_CLAY

inline void sdtx_put_wrapped(const char* text, int cols) {
    if (!text || !*text) return;
    if (cols < 1) cols = 1;
    int len = (int)std::strlen(text);
    int i = 0;
    while (i < len) {
        int nl = i;
        while (nl < len && text[nl] != '\n') nl++;
        int max_take = std::min(cols, nl - i);
        int take = max_take;
        for (int j = 0; j < max_take; j++) {
            if (text[i + j] == ' ') take = j;
        }
        if (take == 0) take = max_take;
        sdtx_putr(text + i, take);
        sdtx_crlf();
        i += take;
        while (i < len && text[i] == ' ') i++;
        if (i < len && text[i] == '\n') i++;
    }
}

inline void sdtx_dump_node(int32_t id, int depth) {
    auto it = coi::ui::g_nodes.find(id);
    if (it == coi::ui::g_nodes.end()) return;
    const auto& n = it->second;
    std::string s;
    s.append((size_t)depth * 2, ' ');
    s.push_back('<');
    s += n.tag.c_str();
    for (const auto& a : n.attrs) {
        s.push_back(' ');
        s += a.key.c_str();
        s += "=\"";
        s += a.value.c_str();
        s += "\"";
    }
    s.push_back('>');
    if (!n.text.empty()) s += n.text.c_str();
    s += "</";
    s += n.tag.c_str();
    s.push_back('>');
    sdtx_printf("%s\n", s.c_str());
    for (int32_t c : n.children) sdtx_dump_node(c, depth + 1);
}

inline float measure_text_h(const coi::ui::Node& n, float w) {
    if (n.text.empty()) return 0.0f;
    const float char_w = 8.0f;
    const float char_h = 8.0f;
    float inner_w = std::max(1.0f, w);
    int cols = (int)std::floor(inner_w / char_w);
    if (cols < 1) cols = 1;
    int len = (int)std::strlen(n.text.c_str());
    int lines = (len + cols - 1) / cols;
    return lines * char_h;
}

	#if defined(COI_NATIVE_SOKOL)
	struct SokolRunnerImpl {
	    static inline void* app = nullptr;
	    static inline tick_fn tick = nullptr;
	    static inline int frames_limit = -1;
	    static inline int frames = 0;
	    static inline std::unordered_map<int32_t, Rect> layout;
	    static inline std::vector<int32_t> draw_list;

	    static bool window_requested() {
	        const char* e = std::getenv("COI_NATIVE_WINDOW");
	        if (!e || !*e) return false;
	        return !(e[0] == '0' && e[1] == '\0');
	    }

	    static void sg_log(const char* tag, uint32_t log_level, uint32_t log_item_id, const char* message_or_null, uint32_t line_nr,
	                       const char* filename_or_null, void*) {
	        const char* e = std::getenv("COI_NATIVE_SG_LOG");
	        const bool verbose = (e && *e && std::string(e) != "0");
	        if (!verbose && log_level > 1) return; // default: only panic+error
        std::cerr << "[sokol_gfx] " << (tag ? tag : "sg") << " lvl=" << log_level << " item=" << log_item_id;
        if (filename_or_null) std::cerr << " at " << filename_or_null << ":" << line_nr;
        if (message_or_null) std::cerr << " " << message_or_null;
        std::cerr << "\n";
    }

    static bool overlay_enabled() {
        const char* e = std::getenv("COI_NATIVE_OVERLAY");
        if (e && *e) {
            return std::string(e) != std::string("0");
        }
#if defined(COI_NATIVE_CAPTURE)
        if (capture_enabled) return false;
#endif
        return true;
    }

    static float layout_node(int32_t id, float x, float y, float w, float max_h) {
        auto it = coi::ui::g_nodes.find(id);
        if (it == coi::ui::g_nodes.end()) return 0.0f;
        const auto& n = it->second;
        const bool is_root = (id == 0);
        const float pad = is_root ? 16.0f : 12.0f;
        const float gap = 10.0f;
        float content_x = x + pad;
        float content_y = y + pad;
        float content_w = std::max(1.0f, w - 2.0f * pad);
        float used_h = pad * 2.0f;

        if (!n.text.empty() && !n.children.empty()) {
            float th = measure_text_h(n, content_w);
            used_h += th + gap;
            content_y += th + gap;
        }

        float cur_y = content_y;
        for (int32_t c : n.children) {
            float child_h = layout_node(c, content_x, cur_y, content_w, max_h);
            if (child_h <= 0.0f) continue;
            cur_y += child_h + gap;
            used_h += child_h + gap;
        }
        if (!n.children.empty()) used_h -= gap;

        if (n.children.empty()) {
            float th = measure_text_h(n, content_w);
            used_h = std::max(used_h, pad * 2.0f + th);
        }

        if (is_root) {
            used_h = max_h;
        } else {
            used_h = std::min(used_h, max_h);
            used_h = std::max(used_h, 32.0f);
        }

        layout[id] = Rect{x, y, w, used_h};
        draw_list.push_back(id);
        return used_h;
    }

    static void layout_tree(float w, float h) {
        layout.clear();
        draw_list.clear();
        layout_node(0, 0.0f, 0.0f, w, h);
    }

    static void init(void) {
        stm_setup();
        sg_desc desc{};
        desc.logger.func = sg_log;
        desc.environment = sglue_environment();
        sg_setup(&desc);
	        const sg_swapchain sc = sglue_swapchain();
	        sgl_desc_t gld{};
	        gld.color_format = sc.color_format;
	        gld.depth_format = sc.depth_format;
	        gld.sample_count = sc.sample_count;
	        sgl_setup(&gld);

        sdtx_desc_t ddesc{};
        ddesc.fonts[0] = sdtx_font_kc853();
        ddesc.fonts[1] = sdtx_font_kc854();
        ddesc.fonts[2] = sdtx_font_z1013();
        ddesc.fonts[3] = sdtx_font_cpc();
        ddesc.fonts[4] = sdtx_font_c64();
        ddesc.fonts[5] = sdtx_font_oric();
	        sdtx_setup(&ddesc);

#if defined(COI_NATIVE_FONTSTASH)
#if defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
	        sclay_setup();
	        const char* font_path_env = std::getenv("COI_NATIVE_FONT");
	        std::filesystem::path font_path = resolve_existing_path(
	            (font_path_env && *font_path_env) ? std::filesystem::path(font_path_env)
	                                              : std::filesystem::path("deps/clay/examples/sokol-video-demo/resources/Roboto-Regular.ttf"));
	        if (font_path.empty()) {
	            // Fallbacks for "run from anywhere" on Linux/macOS.
	            static const char* kFallbacks[] = {
	                "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
	                "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
	                "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
	                "/Library/Fonts/Arial.ttf",
	            };
	            for (const char* fp : kFallbacks) {
	                std::filesystem::path cand(fp);
	                std::error_code e2;
	                if (std::filesystem::exists(cand, e2)) {
	                    font_path = cand;
	                    break;
	                }
	            }
	        }
	        const std::string font_path_s = font_path.empty() ? std::string() : font_path.string();
	        if (!font_path_s.empty() && read_file_bytes(font_path_s.c_str(), font_bytes)) {
	            clay_fonts[0] = sclay_add_font_mem(font_bytes.data(), (int)font_bytes.size());
	            if (clay_fonts[0] == FONS_INVALID) {
	                std::cerr << "[font] failed to load ttf from " << font_path_s << "\n";
	            }
	        } else {
	            const char* shown = (font_path_env && *font_path_env) ? font_path_env
	                                                                  : "deps/clay/examples/sokol-video-demo/resources/Roboto-Regular.ttf";
	            std::cerr << "[font] failed to read ttf file: " << shown << "\n";
	        }
#else
	        sfons_desc_t fs_desc{};
	        fs_desc.width = 512;
	        fs_desc.height = 512;
	        fons_ctx = sfons_create(&fs_desc);
	        if (!fons_ctx) {
	            std::cerr << "[font] sfons_create failed\n";
	        }
	        if (fons_ctx) {
	            const char* font_path_env = std::getenv("COI_NATIVE_FONT");
	            std::filesystem::path font_path = resolve_existing_path(
	                (font_path_env && *font_path_env) ? std::filesystem::path(font_path_env)
	                                                  : std::filesystem::path("deps/clay/examples/sokol-video-demo/resources/Roboto-Regular.ttf"));
	            if (font_path.empty()) {
	                static const char* kFallbacks[] = {
	                    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
	                    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
	                    "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
	                    "/Library/Fonts/Arial.ttf",
	                };
	                for (const char* fp : kFallbacks) {
	                    std::filesystem::path cand(fp);
	                    std::error_code e2;
	                    if (std::filesystem::exists(cand, e2)) {
	                        font_path = cand;
	                        break;
	                    }
	                }
	            }
	            const std::string font_path_s = font_path.empty() ? std::string() : font_path.string();
	            if (!font_path_s.empty() && read_file_bytes(font_path_s.c_str(), font_bytes)) {
	                fons_font = fonsAddFontMem(fons_ctx, "coi-default", font_bytes.data(), (int)font_bytes.size(), 0);
	                if (fons_font == FONS_INVALID) {
	                    std::cerr << "[font] failed to load ttf from " << font_path_s << "\n";
	                }
	            } else {
	                const char* shown = (font_path_env && *font_path_env) ? font_path_env
	                                                                      : "deps/clay/examples/sokol-video-demo/resources/Roboto-Regular.ttf";
	                std::cerr << "[font] failed to read ttf file: " << shown << "\n";
	            }
	        }
#endif
#endif

#if defined(COI_NATIVE_CLAY)
#if defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
	        const float dpi = (sapp_dpi_scale() > 0.0f) ? sapp_dpi_scale() : 1.0f;
	        ClayEngine::ensure((float)sapp_width() / dpi, (float)sapp_height() / dpi);
	        if (ClayEngine::ctx && clay_fonts[0] != FONS_INVALID) {
	            Clay_SetCurrentContext(ClayEngine::ctx);
	            Clay_SetMeasureTextFunction(sclay_measure_text, clay_fonts);
	            Clay_ResetMeasureTextCache();
	        }
#else
	        ClayEngine::ensure((float)sapp_width(), (float)sapp_height());
#endif
#endif

#if defined(COI_NATIVE_CAPTURE)
	        capture_init();
#endif
	    }

#if defined(COI_NATIVE_CAPTURE)
    static inline bool capture_enabled = false;
    enum class CaptureMode { Offscreen, X11 };
    static inline CaptureMode capture_mode = CaptureMode::Offscreen;
    static inline int capture_every = 60;
    static inline int capture_max = -1;
    static inline int capture_index = 0;
    static inline int capture_tolerance = 8; // max Hamming distance for dHash
    static inline bool capture_overlay = false;
    static inline bool capture_fail_on_mismatch = false;
    static inline std::filesystem::path capture_dir;
    static inline std::filesystem::path capture_baseline_dir;
    static inline int capture_w = 0;
    static inline int capture_h = 0;
    static inline sg_image capture_img{};
    static inline sg_image capture_ds_img{};
    static inline sg_view capture_view{};
    static inline sg_view capture_ds_view{};
    static inline std::vector<uint8_t> capture_pixels;
    static inline std::vector<uint8_t> capture_pixels_flipped;
    static inline int exit_code = 0;
    static inline bool capture_debug = false;

    static inline bool parse_wh(const char* s, int& w, int& h) {
        if (!s || !*s) return false;
        int iw = 0, ih = 0;
        if (std::sscanf(s, "%dx%d", &iw, &ih) == 2 || std::sscanf(s, "%d,%d", &iw, &ih) == 2 || std::sscanf(s, "%d %d", &iw, &ih) == 2) {
            if (iw <= 0) iw = 1;
            if (ih <= 0) ih = 1;
            w = iw;
            h = ih;
            return true;
        }
        return false;
    }

    static inline uint64_t dhash_rgba8(const uint8_t* rgba, int w, int h) {
        if (!rgba || w <= 0 || h <= 0) return 0;
        uint8_t g[9 * 8]{};
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 9; x++) {
                int sx = (int)(((x + 0.5) * (double)w) / 9.0);
                int sy = (int)(((y + 0.5) * (double)h) / 8.0);
                if (sx < 0) sx = 0;
                if (sy < 0) sy = 0;
                if (sx >= w) sx = w - 1;
                if (sy >= h) sy = h - 1;
                const uint8_t* p = rgba + (size_t)(sy * w + sx) * 4u;
                const uint32_t r = p[0], gg = p[1], b = p[2];
                const uint32_t gray = (r * 77u + gg * 150u + b * 29u) >> 8;
                g[y * 9 + x] = (uint8_t)gray;
            }
        }
        uint64_t out = 0;
        int bit = 0;
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                uint8_t a = g[y * 9 + x];
                uint8_t b = g[y * 9 + x + 1];
                if (a > b) out |= (1ull << bit);
                bit++;
            }
        }
        return out;
    }

    static inline int popcount64(uint64_t v) {
#if defined(__GNUC__) || defined(__clang__)
        return __builtin_popcountll(v);
#else
        int c = 0;
        while (v) {
            v &= (v - 1);
            c++;
        }
        return c;
#endif
    }

    static inline bool read_hex_u64(const std::filesystem::path& p, uint64_t& out) {
        std::ifstream f(p);
        if (!f) return false;
        std::string s;
        f >> s;
        if (s.empty()) return false;
        try {
            size_t idx = 0;
            out = std::stoull(s, &idx, 16);
            return idx > 0;
        } catch (...) {
            return false;
        }
    }

	    static void capture_init() {
	        const char* dir = std::getenv("COI_NATIVE_CAPTURE_DIR");
	        if (!dir || !*dir) return;
	        capture_enabled = true;
	        capture_dir = std::filesystem::path(dir);

        if (const char* e = std::getenv("COI_NATIVE_CAPTURE_DEBUG")) {
            capture_debug = (std::string(e) != "0");
        }

	        // Default capture mode:
	        // - windowed runs: prefer X11 on Linux (robust across drivers)
	        // - capture-only (no COI_NATIVE_WINDOW): prefer offscreen
	        // Offscreen/X11 can be forced via COI_NATIVE_CAPTURE_MODE.
	        const bool want_window = window_requested();
#if defined(__linux__) || defined(__unix__)
	        capture_mode = want_window ? CaptureMode::X11 : CaptureMode::Offscreen;
#else
	        capture_mode = CaptureMode::Offscreen;
#endif
	        if (const char* m = std::getenv("COI_NATIVE_CAPTURE_MODE")) {
	            if (m && *m) {
	                const std::string mm(m);
                if (mm == "x11") capture_mode = CaptureMode::X11;
                if (mm == "offscreen") capture_mode = CaptureMode::Offscreen;
            }
        }

        capture_w = sapp_width();
        capture_h = sapp_height();
        const char* size = std::getenv("COI_NATIVE_CAPTURE_SIZE");
        (void)parse_wh(size, capture_w, capture_h);

        if (const char* e = std::getenv("COI_NATIVE_CAPTURE_EVERY")) {
            capture_every = std::atoi(e);
            if (capture_every <= 0) capture_every = 1;
        }
        if (const char* e = std::getenv("COI_NATIVE_CAPTURE_MAX")) {
            capture_max = std::atoi(e);
        }
        if (const char* e = std::getenv("COI_NATIVE_CAPTURE_TOLERANCE")) {
            capture_tolerance = std::atoi(e);
            if (capture_tolerance < 0) capture_tolerance = 0;
        }
        if (const char* e = std::getenv("COI_NATIVE_CAPTURE_OVERLAY")) {
            capture_overlay = (std::string(e) != "0");
        }
        if (const char* e = std::getenv("COI_NATIVE_CAPTURE_FAIL_ON_MISMATCH")) {
            capture_fail_on_mismatch = (std::string(e) != "0");
        }
        if (const char* base = std::getenv("COI_NATIVE_CAPTURE_BASELINE")) {
            if (*base) capture_baseline_dir = std::filesystem::path(base);
        }

        std::error_code ec;
        std::filesystem::create_directories(capture_dir, ec);

        capture_img = sg_image{};
        capture_ds_img = sg_image{};
        capture_view = sg_view{};
        capture_ds_view = sg_view{};
        capture_pixels.clear();
        capture_pixels_flipped.clear();
        capture_index = 0;

        if (capture_debug) {
            std::cerr << "[capture] enabled dir=" << capture_dir.string()
                      << " size=" << capture_w << "x" << capture_h
                      << " mode=" << (capture_mode == CaptureMode::Offscreen ? "offscreen" : "x11")
                      << " every=" << capture_every
                      << " max=" << capture_max
                      << " baseline=" << capture_baseline_dir.string()
                      << " tol=" << capture_tolerance
                      << " overlay=" << (capture_overlay ? 1 : 0)
                      << " fail=" << (capture_fail_on_mismatch ? 1 : 0)
                      << std::endl;
        }
    }

    static void capture_shutdown() {
        if (capture_ds_view.id != SG_INVALID_ID) {
            sg_destroy_view(capture_ds_view);
            capture_ds_view.id = SG_INVALID_ID;
        }
        if (capture_ds_img.id != SG_INVALID_ID) {
            sg_destroy_image(capture_ds_img);
            capture_ds_img.id = SG_INVALID_ID;
        }
        if (capture_view.id != SG_INVALID_ID) {
            sg_destroy_view(capture_view);
            capture_view.id = SG_INVALID_ID;
        }
        if (capture_img.id != SG_INVALID_ID) {
            sg_destroy_image(capture_img);
            capture_img.id = SG_INVALID_ID;
        }
        capture_pixels.clear();
        capture_pixels_flipped.clear();
    }

    static void capture_ensure_target() {
        if (!capture_enabled) return;
        if (capture_mode != CaptureMode::Offscreen) return;
        if (capture_w <= 0) capture_w = 1;
        if (capture_h <= 0) capture_h = 1;

        if (capture_img.id != SG_INVALID_ID) {
            sg_image_desc d = sg_query_image_desc(capture_img);
            if ((int)d.width == capture_w && (int)d.height == capture_h) return;
            capture_shutdown();
        }

        if (capture_debug) {
            std::cerr << "[capture] ensure_target " << capture_w << "x" << capture_h << std::endl;
        }

        const sg_swapchain sc = sglue_swapchain();

        sg_image_desc img_desc{};
        img_desc.type = SG_IMAGETYPE_2D;
        img_desc.width = capture_w;
        img_desc.height = capture_h;
        img_desc.num_mipmaps = 1;
        img_desc.sample_count = (sc.sample_count > 0) ? sc.sample_count : 1;
        img_desc.pixel_format = (sc.color_format != SG_PIXELFORMAT_NONE) ? sc.color_format : SG_PIXELFORMAT_RGBA8;
        img_desc.usage.color_attachment = true;
        img_desc.label = "coi-capture-color";
        if (capture_debug) {
            std::cerr << "[capture] sg_make_image..." << std::endl;
        }
        capture_img = sg_make_image(&img_desc);
        if (capture_debug) {
            std::cerr << "[capture] sg_make_image id=" << capture_img.id << std::endl;
        }
        if (capture_img.id == SG_INVALID_ID) {
            std::cerr << "[capture] failed to create capture image; disabling capture\n";
            capture_enabled = false;
            return;
        }

        sg_view_desc view_desc{};
        view_desc.color_attachment.image = capture_img;
        view_desc.label = "coi-capture-view";
        if (capture_debug) {
            std::cerr << "[capture] sg_make_view..." << std::endl;
        }
        capture_view = sg_make_view(&view_desc);
        if (capture_debug) {
            std::cerr << "[capture] sg_make_view id=" << capture_view.id << std::endl;
        }
        if (capture_view.id == SG_INVALID_ID) {
            std::cerr << "[capture] failed to create capture view; disabling capture\n";
            capture_enabled = false;
            return;
        }

        if (sc.depth_format != SG_PIXELFORMAT_NONE) {
            sg_image_desc ds_desc{};
            ds_desc.type = SG_IMAGETYPE_2D;
            ds_desc.width = capture_w;
            ds_desc.height = capture_h;
            ds_desc.num_mipmaps = 1;
            ds_desc.sample_count = (sc.sample_count > 0) ? sc.sample_count : 1;
            ds_desc.pixel_format = sc.depth_format;
            ds_desc.usage.depth_stencil_attachment = true;
            ds_desc.label = "coi-capture-depth";
            if (capture_debug) {
                std::cerr << "[capture] sg_make_depth_image..." << std::endl;
            }
            capture_ds_img = sg_make_image(&ds_desc);
            if (capture_debug) {
                std::cerr << "[capture] sg_make_depth_image id=" << capture_ds_img.id << std::endl;
            }
            if (capture_ds_img.id == SG_INVALID_ID) {
                std::cerr << "[capture] failed to create depth image; disabling capture\n";
                capture_enabled = false;
                return;
            }
            sg_view_desc ds_view_desc{};
            ds_view_desc.depth_stencil_attachment.image = capture_ds_img;
            ds_view_desc.label = "coi-capture-depth-view";
            if (capture_debug) {
                std::cerr << "[capture] sg_make_depth_view..." << std::endl;
            }
            capture_ds_view = sg_make_view(&ds_view_desc);
            if (capture_debug) {
                std::cerr << "[capture] sg_make_depth_view id=" << capture_ds_view.id << std::endl;
            }
            if (capture_ds_view.id == SG_INVALID_ID) {
                std::cerr << "[capture] failed to create depth view; disabling capture\n";
                capture_enabled = false;
                return;
            }
        }

        capture_pixels.resize((size_t)capture_w * (size_t)capture_h * 4u);
        capture_pixels_flipped.resize(capture_pixels.size());
    }

    static void capture_maybe(const sg_pass_action& action, double dt) {
        (void)dt;
        if (!capture_enabled) return;
        if (capture_mode != CaptureMode::Offscreen) return;
        if (capture_max >= 0 && capture_index >= capture_max) return;
        if (capture_every > 1 && (frames % capture_every) != 0) return;

        if (capture_debug) {
            std::cerr << "[capture] capture_maybe frame=" << frames << " idx=" << capture_index << std::endl;
        }

        capture_ensure_target();
        if (capture_view.id == SG_INVALID_ID) return;

        // Render to an offscreen pass.
        sg_pass cp{};
        cp.action = action;
        cp.attachments.colors[0] = capture_view;
        if (capture_ds_view.id != SG_INVALID_ID) {
            cp.attachments.depth_stencil = capture_ds_view;
        }
        if (capture_debug) {
            std::cerr << "[capture] sg_begin_pass..." << std::endl;
        }
        sg_begin_pass(&cp);

        sgl_defaults();
        sgl_viewport(0, 0, capture_w, capture_h, true);
        sgl_matrix_mode_projection();
        sgl_load_identity();
        sgl_ortho(0.0f, (float)capture_w, (float)capture_h, 0.0f, -1.0f, 1.0f);
        sgl_matrix_mode_modelview();
        sgl_load_identity();

#if defined(COI_NATIVE_CLAY)
	        // Re-layout for capture size (uses the existing Clay context).
	        Clay_RenderCommandArray cmds = ClayEngine::layout((float)capture_w, (float)capture_h, 1.0f);
	        const bool ok = (ClayEngine::ctx != nullptr);
	        if (ok) {
#if defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
	            // Important: sg scissor state persists across passes. Ensure a full-target
	            // scissor before rendering (Clay emits its own scissor commands only when
	            // clip elements are present).
	            sg_apply_scissor_rect(0, 0, capture_w, capture_h, true /* origin_top_left */);
	            sgl_matrix_mode_projection();
	            sgl_load_identity();
	            sgl_matrix_mode_modelview();
	            sgl_load_identity();
	            sclay_render(cmds, clay_fonts);
	            sgl_draw();
#else
            auto iround = [](float v) -> int { return (int)std::lround((double)v); };
            struct IRect {
                int x = 0;
                int y = 0;
                int w = 0;
                int h = 0;
            };
            auto intersect = [](const IRect& a, const IRect& b) -> IRect {
                int x0 = std::max(a.x, b.x);
                int y0 = std::max(a.y, b.y);
                int x1 = std::min(a.x + a.w, b.x + b.w);
                int y1 = std::min(a.y + a.h, b.y + b.h);
                IRect out;
                out.x = x0;
                out.y = y0;
                out.w = std::max(0, x1 - x0);
                out.h = std::max(0, y1 - y0);
                return out;
            };
            auto apply_scissor = [&](const IRect& r) {
                sg_apply_scissor_rect(r.x, r.y, r.w, r.h, true /* origin_top_left */);
            };

            const IRect full{0, 0, capture_w, capture_h};
            std::vector<IRect> scissor_stack;
            apply_scissor(full);

            bool quads_open = false;
            auto begin_quads = [&]() {
                if (!quads_open) {
                    sgl_begin_quads();
                    quads_open = true;
                }
            };
            auto flush_quads = [&]() {
                if (quads_open) {
                    sgl_end();
                    sgl_draw();
                    quads_open = false;
                }
            };
            auto quad = [&](float x, float y, float w, float h) {
                float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
                sgl_v2f(x0, y0);
                sgl_v2f(x1, y0);
                sgl_v2f(x1, y1);
                sgl_v2f(x0, y1);
            };

            begin_quads();
            for (int32_t i = 0; i < cmds.length; i++) {
                Clay_RenderCommand* cmd = Clay_RenderCommandArray_Get(&cmds, i);
                if (!cmd) continue;
                if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_SCISSOR_START) {
                    flush_quads();
                    const auto& bb = cmd->boundingBox;
                    IRect r{iround(bb.x), iround(bb.y), std::max(0, iround(bb.width)), std::max(0, iround(bb.height))};
                    if (!scissor_stack.empty()) r = intersect(scissor_stack.back(), r);
                    scissor_stack.push_back(r);
                    apply_scissor(r);
                    begin_quads();
                    continue;
                }
                if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_SCISSOR_END) {
                    flush_quads();
                    if (!scissor_stack.empty()) scissor_stack.pop_back();
                    apply_scissor(scissor_stack.empty() ? full : scissor_stack.back());
                    begin_quads();
                    continue;
                }
                const auto& bb = cmd->boundingBox;
                if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_RECTANGLE) {
                    const auto& c = cmd->renderData.rectangle.backgroundColor;
                    sgl_c4f(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
                    quad(bb.x, bb.y, bb.width, bb.height);
                } else if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_BORDER) {
                    const auto& b = cmd->renderData.border;
                    const auto& c = b.color;
                    if (c.a <= 0) continue;
                    float ww = std::max(0.0f, bb.width);
                    float hh = std::max(0.0f, bb.height);
                    float l = std::min<float>((float)b.width.left, ww);
                    float r = std::min<float>((float)b.width.right, ww);
                    float t = std::min<float>((float)b.width.top, hh);
                    float bo = std::min<float>((float)b.width.bottom, hh);
                    if ((l + r + t + bo) <= 0.0f) continue;
                    sgl_c4f(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
                    if (t > 0.0f) quad(bb.x, bb.y, ww, t);
                    if (bo > 0.0f) quad(bb.x, bb.y + hh - bo, ww, bo);
                    if (l > 0.0f) quad(bb.x, bb.y, l, hh);
                    if (r > 0.0f) quad(bb.x + ww - r, bb.y, r, hh);
                }
            }
            flush_quads();

#if defined(COI_NATIVE_FONTSTASH)
            if (fons_ctx && fons_font != FONS_INVALID) {
                // sgl_draw() rewinds the recorded command stream. Ensure viewport/matrices
                // are set again before recording fontstash draw calls.
                sgl_defaults();
                sgl_viewport(0, 0, capture_w, capture_h, true);
                sgl_matrix_mode_projection();
                sgl_load_identity();
                sgl_ortho(0.0f, (float)capture_w, (float)capture_h, 0.0f, -1.0f, 1.0f);
                sgl_matrix_mode_modelview();
                sgl_load_identity();

                sg_apply_scissor_rect(0, 0, capture_w, capture_h, true /* origin_top_left */);
                fonsClearState(fons_ctx);
                fonsSetFont(fons_ctx, fons_font);
                for (int32_t i = 0; i < cmds.length; i++) {
                    Clay_RenderCommand* cmd = Clay_RenderCommandArray_Get(&cmds, i);
                    if (!cmd) continue;
                    if (cmd->commandType != CLAY_RENDER_COMMAND_TYPE_TEXT) continue;
                    const auto& bb = cmd->boundingBox;
                    const auto& t = cmd->renderData.text;
                    fonsSetAlign(fons_ctx, FONS_ALIGN_LEFT | FONS_ALIGN_TOP);
                    fonsSetSize(fons_ctx, (float)t.fontSize);
                    fonsSetSpacing(fons_ctx, (float)t.letterSpacing);
                    fonsSetColor(fons_ctx, sfons_rgba(t.textColor.r, t.textColor.g, t.textColor.b, t.textColor.a));
                    const char* start = t.stringContents.chars;
                    const char* end = start ? (start + t.stringContents.length) : nullptr;
                    if (start && end && t.stringContents.length > 0) {
                        (void)fonsDrawText(fons_ctx, bb.x, bb.y, start, end);
                    }
                }
                sfons_flush(fons_ctx);
                sgl_draw();
            }
#endif
#endif
        }
#endif

        if (capture_overlay) {
            sdtx_canvas((float)capture_w, (float)capture_h);
            sdtx_font(0);
            sdtx_origin(1.0f, 1.0f);
            sdtx_home();
            sdtx_color3f(1.0f, 1.0f, 1.0f);
            sdtx_puts("CAPTURE\n");
            sdtx_draw();
        }

        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, capture_w, capture_h, GL_RGBA, GL_UNSIGNED_BYTE, capture_pixels.data());
        sg_end_pass();

        const size_t stride = (size_t)capture_w * 4u;
        for (int y = 0; y < capture_h; y++) {
            const uint8_t* src = capture_pixels.data() + (size_t)(capture_h - 1 - y) * stride;
            uint8_t* dst = capture_pixels_flipped.data() + (size_t)y * stride;
            std::memcpy(dst, src, stride);
        }

        capture_write_outputs(capture_pixels_flipped.data(), capture_w, capture_h, (int)stride);
    }

    static void capture_write_outputs(const uint8_t* rgba_topdown, int w, int h, int stride_bytes) {
        if (!rgba_topdown || w <= 0 || h <= 0) return;
        char name[64];
        std::snprintf(name, sizeof(name), "frame_%06d.png", capture_index);
        std::filesystem::path png_path = capture_dir / name;
        const int ok_png = stbi_write_png(png_path.string().c_str(), w, h, 4, rgba_topdown, stride_bytes);
        if (!ok_png) {
            std::cerr << "[capture] failed to write png: " << png_path.string() << "\n";
        }

        const uint64_t hsh = dhash_rgba8(rgba_topdown, w, h);
        std::snprintf(name, sizeof(name), "frame_%06d.dhash", capture_index);
        std::filesystem::path hash_path = capture_dir / name;
        {
            std::ofstream hf(hash_path);
            hf << std::hex << hsh << "\n";
        }

        if (!capture_baseline_dir.empty()) {
            std::filesystem::path base_hash = capture_baseline_dir / hash_path.filename();
            uint64_t base = 0;
            if (read_hex_u64(base_hash, base)) {
                const int dist = popcount64(hsh ^ base);
                std::cout << "[capture] " << hash_path.filename().string() << " dhash=" << std::hex << hsh << std::dec
                          << " baseline_dist=" << dist << " tol=" << capture_tolerance << "\n";
                if (dist > capture_tolerance) {
                    exit_code = 1;
                    if (capture_fail_on_mismatch) sapp_request_quit();
                }
            } else {
                std::cout << "[capture] " << hash_path.filename().string() << " dhash=" << std::hex << hsh << std::dec << " (no baseline)\n";
            }
        } else {
            std::cout << "[capture] " << hash_path.filename().string() << " dhash=" << std::hex << hsh << std::dec << "\n";
        }

        capture_index++;
    }

    static void capture_maybe_swapchain(double dt) {
        (void)dt;
        if (!capture_enabled) return;
        if (capture_mode != CaptureMode::X11) return;
        if (capture_max >= 0 && capture_index >= capture_max) return;
        if (capture_every > 1 && (frames % capture_every) != 0) return;

        int w = capture_w > 0 ? capture_w : sapp_width();
        int h = capture_h > 0 ? capture_h : sapp_height();
        const int sw = sapp_width();
        const int sh = sapp_height();
        if (w > sw) w = sw;
        if (h > sh) h = sh;
        if (w <= 0) w = 1;
        if (h <= 0) h = 1;

        if (capture_debug) {
            std::cerr << "[capture] swapchain read " << w << "x" << h << " (win " << sw << "x" << sh << ")" << std::endl;
        }

        capture_pixels.resize((size_t)w * (size_t)h * 4u);
        capture_pixels_flipped.resize(capture_pixels.size());

        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, capture_pixels.data());

        const size_t stride = (size_t)w * 4u;
        for (int y = 0; y < h; y++) {
            const uint8_t* src = capture_pixels.data() + (size_t)(h - 1 - y) * stride;
            uint8_t* dst = capture_pixels_flipped.data() + (size_t)y * stride;
            std::memcpy(dst, src, stride);
        }

        capture_write_outputs(capture_pixels_flipped.data(), w, h, (int)stride);
    }
#endif

    static inline float mouse_x = 0.0f;
    static inline float mouse_y = 0.0f;
    static inline bool mouse_down = false;
    static inline float scroll_x = 0.0f;
    static inline float scroll_y = 0.0f;
    static inline bool click_pending = false;
    static inline float click_x = 0.0f;
    static inline float click_y = 0.0f;

    static void event_cb(const sapp_event* ev) {
        if (!ev) return;
        switch (ev->type) {
        case SAPP_EVENTTYPE_MOUSE_MOVE:
            mouse_x = ev->mouse_x;
            mouse_y = ev->mouse_y;
            break;
        case SAPP_EVENTTYPE_MOUSE_SCROLL:
            // sokol_app scroll deltas typically follow browser semantics (positive Y == scroll down),
            // but Clay expects negative scrollDelta.y to move content down (scrollPosition is clamped <= 0).
            scroll_x -= ev->scroll_x;
            scroll_y -= ev->scroll_y;
            break;
        case SAPP_EVENTTYPE_MOUSE_DOWN:
            mouse_down = true;
            mouse_x = ev->mouse_x;
            mouse_y = ev->mouse_y;
            break;
        case SAPP_EVENTTYPE_MOUSE_UP:
            mouse_down = false;
            mouse_x = ev->mouse_x;
            mouse_y = ev->mouse_y;
            click_pending = true;
            click_x = mouse_x;
            click_y = mouse_y;
            break;
        default:
            break;
        }
    }

    static void frame_cb(void) {
        g_sokol_frame_started = true;
        double dt = sapp_frame_duration();
        if (dt > 0.1) dt = 0.1;
        if (tick && app) tick(app, dt);

        coi::native::flush();

#if defined(COI_NATIVE_CLAY)
        const float dpi = (sapp_dpi_scale() > 0.0f) ? sapp_dpi_scale() : 1.0f;
        float mx = mouse_x;
        float my = mouse_y;
#if defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
        mx /= dpi;
        my /= dpi;
#endif
        ClayEngine::set_input(mx, my, mouse_down, scroll_x, scroll_y, (float)dt);
        scroll_x = 0.0f;
        scroll_y = 0.0f;
        Clay_RenderCommandArray render_commands = ClayEngine::layout((float)sapp_width(), (float)sapp_height(), dpi);
        const bool clay_ok = (ClayEngine::ctx != nullptr);
        if (!clay_ok) layout_tree((float)sapp_width(), (float)sapp_height());
#else
        layout_tree((float)sapp_width(), (float)sapp_height());
#endif

#if defined(COI_NATIVE_CLAY)
        if (clay_ok && click_pending && g_click_dispatcher) {
            click_pending = false;
            Clay_SetCurrentContext(ClayEngine::ctx);
            float cx = click_x;
            float cy = click_y;
#if defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
            cx /= dpi;
            cy /= dpi;
#endif
            Clay_SetPointerState(Clay_Vector2{cx, cy}, false);
            Clay_ElementIdArray ids = Clay_GetPointerOverIds();
            for (int32_t i = ids.length - 1; i >= 0; --i) {
                Clay_ElementId* eid = Clay_ElementIdArray_Get(&ids, i);
                if (!eid) continue;
                int32_t hid = (int32_t)(eid->id ^ 0xC01D0000u);
                if (coi::ui::g_nodes.find(hid) == coi::ui::g_nodes.end()) continue;
                dispatch_click_bubble(webcc::handle(hid));
                break;
            }
        }
#else
        if (click_pending && g_click_dispatcher) {
            click_pending = false;
            // Fallback: pick last drawn rect containing the click point.
            webcc::handle target;
            for (auto it = draw_list.rbegin(); it != draw_list.rend(); ++it) {
                int32_t id = *it;
                auto itr = layout.find(id);
                if (itr == layout.end()) continue;
                const Rect& r = itr->second;
                if (click_x >= r.x && click_x <= (r.x + r.w) && click_y >= r.y && click_y <= (r.y + r.h)) {
                    target = webcc::handle(id);
                    break;
                }
            }
            if (target.is_valid()) dispatch_click_bubble(target);
        }
#endif

        sg_pass_action pass{};
        pass.colors[0].load_action = SG_LOADACTION_CLEAR;
        pass.colors[0].clear_value = {0.08f, 0.08f, 0.10f, 1.0f};

        sg_pass p{};
        p.action = pass;
        p.swapchain = sglue_swapchain();
        sg_begin_pass(&p);

        sgl_defaults();
        sgl_viewport(0, 0, sapp_width(), sapp_height(), true);
        sgl_matrix_mode_projection();
        sgl_load_identity();
        sgl_ortho(0.0f, (float)sapp_width(), (float)sapp_height(), 0.0f, -1.0f, 1.0f);
        sgl_matrix_mode_modelview();
        sgl_load_identity();

#if defined(COI_NATIVE_CLAY)
	        if (clay_ok) {
#if defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
	            // Clay's reference sokol renderer expects an identity projection matrix.
	            // Also reset sg scissor so we don't inherit state from earlier passes.
	            sg_apply_scissor_rect(0, 0, sapp_width(), sapp_height(), true /* origin_top_left */);
	            sgl_matrix_mode_projection();
	            sgl_load_identity();
	            sgl_matrix_mode_modelview();
	            sgl_load_identity();
	            sclay_render(render_commands, clay_fonts);
	            sgl_draw();
#else
            auto iround = [](float v) -> int { return (int)std::lround((double)v); };
            struct IRect {
                int x = 0;
                int y = 0;
                int w = 0;
                int h = 0;
            };
            auto intersect = [](const IRect& a, const IRect& b) -> IRect {
                int x0 = std::max(a.x, b.x);
                int y0 = std::max(a.y, b.y);
                int x1 = std::min(a.x + a.w, b.x + b.w);
                int y1 = std::min(a.y + a.h, b.y + b.h);
                IRect out;
                out.x = x0;
                out.y = y0;
                out.w = std::max(0, x1 - x0);
                out.h = std::max(0, y1 - y0);
                return out;
            };
            auto apply_scissor = [&](const IRect& r) {
                sg_apply_scissor_rect(r.x, r.y, r.w, r.h, true /* origin_top_left */);
            };

            const IRect full{0, 0, sapp_width(), sapp_height()};
            std::vector<IRect> scissor_stack;
            apply_scissor(full);

            bool quads_open = false;
            auto begin_quads = [&]() {
                if (!quads_open) {
                    sgl_begin_quads();
                    quads_open = true;
                }
            };
            auto flush_quads = [&]() {
                if (quads_open) {
                    sgl_end();
                    sgl_draw();
                    quads_open = false;
                }
            };

            begin_quads();
            for (int32_t i = 0; i < render_commands.length; i++) {
                Clay_RenderCommand* cmd = Clay_RenderCommandArray_Get(&render_commands, i);
                if (!cmd) continue;
                if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_SCISSOR_START) {
                    flush_quads();
                    const auto& bb = cmd->boundingBox;
                    IRect r{iround(bb.x), iround(bb.y), std::max(0, iround(bb.width)), std::max(0, iround(bb.height))};
                    if (!scissor_stack.empty()) r = intersect(scissor_stack.back(), r);
                    scissor_stack.push_back(r);
                    apply_scissor(r);
                    begin_quads();
                    continue;
                }
                if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_SCISSOR_END) {
                    flush_quads();
                    if (!scissor_stack.empty()) scissor_stack.pop_back();
                    apply_scissor(scissor_stack.empty() ? full : scissor_stack.back());
                    begin_quads();
                    continue;
                }
                const auto& bb = cmd->boundingBox;
                auto quad = [&](float x, float y, float w, float h) {
                    float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
                    sgl_v2f(x0, y0);
                    sgl_v2f(x1, y0);
                    sgl_v2f(x1, y1);
                    sgl_v2f(x0, y1);
                };

                if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_RECTANGLE) {
                    const auto& c = cmd->renderData.rectangle.backgroundColor;
                    sgl_c4f(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
                    quad(bb.x, bb.y, bb.width, bb.height);
                } else if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_BORDER) {
                    const auto& b = cmd->renderData.border;
                    const auto& c = b.color;
                    if (c.a <= 0) continue;
                    float w = std::max(0.0f, bb.width);
                    float h = std::max(0.0f, bb.height);
                    float l = std::min<float>((float)b.width.left, w);
                    float r = std::min<float>((float)b.width.right, w);
                    float t = std::min<float>((float)b.width.top, h);
                    float bo = std::min<float>((float)b.width.bottom, h);
                    if ((l + r + t + bo) <= 0.0f) continue;
                    sgl_c4f(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
                    if (t > 0.0f) quad(bb.x, bb.y, w, t);
                    if (bo > 0.0f) quad(bb.x, bb.y + h - bo, w, bo);
                    if (l > 0.0f) quad(bb.x, bb.y, l, h);
                    if (r > 0.0f) quad(bb.x + w - r, bb.y, r, h);
                }
            }
            flush_quads();
        } else {
            sgl_begin_quads();
            for (int32_t id : draw_list) {
                if (id == 0) continue;
                auto itn = coi::ui::g_nodes.find(id);
                if (itn == coi::ui::g_nodes.end()) continue;
                const auto& n = itn->second;
                if (n.tag == "comment") continue;
                auto itr = layout.find(id);
                if (itr == layout.end()) continue;
                const Rect& r = itr->second;
                const webcc::string* cls = attr(n, "class");
                uint32_t h = hash_u32(cls ? cls->c_str() : n.tag.c_str());
                float cr, cg, cb;
                color_from_hash(h, cr, cg, cb);
                sgl_c4f(cr, cg, cb, 0.18f);
                float x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;
                sgl_v2f(x0, y0);
                sgl_v2f(x1, y0);
                sgl_v2f(x1, y1);
                sgl_v2f(x0, y1);
            }
            sgl_end();
            sgl_draw();
#endif
        }
#else
        sgl_begin_quads();
        for (int32_t id : draw_list) {
            if (id == 0) continue;
            auto itn = coi::ui::g_nodes.find(id);
            if (itn == coi::ui::g_nodes.end()) continue;
            const auto& n = itn->second;
            if (n.tag == "comment") continue;
            auto itr = layout.find(id);
            if (itr == layout.end()) continue;
            const Rect& r = itr->second;
            const webcc::string* cls = attr(n, "class");
            uint32_t h = hash_u32(cls ? cls->c_str() : n.tag.c_str());
            float cr, cg, cb;
            color_from_hash(h, cr, cg, cb);
            sgl_c4f(cr, cg, cb, 0.18f);
            float x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;
            sgl_v2f(x0, y0);
            sgl_v2f(x1, y0);
            sgl_v2f(x1, y1);
            sgl_v2f(x0, y1);
        }
        sgl_end();
        sgl_draw();
#endif

        const float cell = 8.0f;

#if defined(COI_NATIVE_CLAY) && defined(COI_NATIVE_FONTSTASH) && !defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
        if (clay_ok && fons_ctx && fons_font != FONS_INVALID) {
            // Ensure font rendering isn't accidentally clipped by a stale scissor rect.
            // Also re-emit viewport/matrices because we may have already called sgl_draw()
            // while rendering rectangles/borders.
            sgl_defaults();
            sgl_viewport(0, 0, sapp_width(), sapp_height(), true);
            sgl_matrix_mode_projection();
            sgl_load_identity();
            sgl_ortho(0.0f, (float)sapp_width(), (float)sapp_height(), 0.0f, -1.0f, 1.0f);
            sgl_matrix_mode_modelview();
            sgl_load_identity();

            sg_apply_scissor_rect(0, 0, sapp_width(), sapp_height(), true /* origin_top_left */);
            fonsClearState(fons_ctx);
            fonsSetFont(fons_ctx, fons_font);
            for (int32_t i = 0; i < render_commands.length; i++) {
                Clay_RenderCommand* cmd = Clay_RenderCommandArray_Get(&render_commands, i);
                if (!cmd) continue;
                if (cmd->commandType != CLAY_RENDER_COMMAND_TYPE_TEXT) continue;
                const auto& bb = cmd->boundingBox;
                const auto& t = cmd->renderData.text;
                fonsSetAlign(fons_ctx, FONS_ALIGN_LEFT | FONS_ALIGN_TOP);
                fonsSetSize(fons_ctx, (float)t.fontSize);
                fonsSetSpacing(fons_ctx, (float)t.letterSpacing);
                fonsSetColor(fons_ctx, sfons_rgba(t.textColor.r, t.textColor.g, t.textColor.b, t.textColor.a));
                const char* start = t.stringContents.chars;
                const char* end = start ? (start + t.stringContents.length) : nullptr;
                if (start && end && t.stringContents.length > 0) {
                    (void)fonsDrawText(fons_ctx, bb.x, bb.y, start, end);
                }
            }
            const char* ttf_test = std::getenv("COI_NATIVE_TTF_TEST");
            if (ttf_test && *ttf_test && std::string(ttf_test) != std::string("0")) {
                fonsSetSize(fons_ctx, 28.0f);
                fonsSetSpacing(fons_ctx, 0.0f);
                fonsSetColor(fons_ctx, sfons_rgba(255, 80, 80, 255));
                (void)fonsDrawText(fons_ctx, 20.0f, 52.0f, "TTF OK", nullptr);
            }
            sfons_flush(fons_ctx);
            sgl_draw();
        }
#endif

        const bool draw_overlay = overlay_enabled();
        if (draw_overlay) {
            sdtx_canvas((float)sapp_width(), (float)sapp_height());
            sdtx_font(0);
            sdtx_origin(1.0f, 1.0f);
            sdtx_home();
            sdtx_color3f(1.0f, 1.0f, 1.0f);
            sdtx_puts("COI native runtime (sokol)\n");
            sdtx_printf("dt: %.3f\n\n", dt);
#if defined(COI_NATIVE_FONTSTASH)
#if defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
	            sdtx_printf("fontstash: %s\n\n", (clay_fonts[0] != FONS_INVALID) ? "on" : "off");
#else
	            sdtx_printf("fontstash: %s\n\n", (fons_ctx && fons_font != FONS_INVALID) ? "on" : "off");
#endif
#endif

#if defined(COI_NATIVE_CLAY)
	        if (clay_ok) {
	            bool fontstash_ok = false;
#if defined(COI_NATIVE_FONTSTASH)
#if defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
	            fontstash_ok = (clay_fonts[0] != FONS_INVALID);
#else
	            fontstash_ok = (fons_ctx && fons_font != FONS_INVALID);
#endif
#endif
	            if (!fontstash_ok) {
	                auto iround = [](float v) -> int { return (int)std::lround((double)v); };
	                struct IRect {
	                    int x = 0;
	                    int y = 0;
	                    int w = 0;
	                    int h = 0;
	                };
	                auto intersects = [](const IRect& a, const IRect& b) -> bool {
	                    int x0 = std::max(a.x, b.x);
	                    int y0 = std::max(a.y, b.y);
	                    int x1 = std::min(a.x + a.w, b.x + b.w);
	                    int y1 = std::min(a.y + a.h, b.y + b.h);
	                    return (x1 > x0) && (y1 > y0);
	                };
	                const IRect full{0, 0, sapp_width(), sapp_height()};
	                std::vector<IRect> scissor_stack;
	                IRect active = full;

	                // Render clay text commands using sokol_debugtext (fallback).
	                for (int32_t i = 0; i < render_commands.length; i++) {
	                    Clay_RenderCommand* cmd = Clay_RenderCommandArray_Get(&render_commands, i);
	                    if (!cmd) continue;
	                    if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_SCISSOR_START) {
	                        const auto& bb = cmd->boundingBox;
	                        IRect r{iround(bb.x), iround(bb.y), std::max(0, iround(bb.width)), std::max(0, iround(bb.height))};
	                        if (!scissor_stack.empty()) {
	                            const IRect& prev = scissor_stack.back();
	                            int x0 = std::max(prev.x, r.x);
	                            int y0 = std::max(prev.y, r.y);
	                            int x1 = std::min(prev.x + prev.w, r.x + r.w);
	                            int y1 = std::min(prev.y + prev.h, r.y + r.h);
	                            r.x = x0;
	                            r.y = y0;
	                            r.w = std::max(0, x1 - x0);
	                            r.h = std::max(0, y1 - y0);
	                        }
	                        scissor_stack.push_back(r);
	                        active = r;
	                        continue;
	                    }
	                    if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_SCISSOR_END) {
	                        if (!scissor_stack.empty()) scissor_stack.pop_back();
	                        active = scissor_stack.empty() ? full : scissor_stack.back();
	                        continue;
	                    }
	                    if (cmd->commandType != CLAY_RENDER_COMMAND_TYPE_TEXT) continue;
	                    const auto& bb = cmd->boundingBox;
	                    IRect tbb{iround(bb.x), iround(bb.y), std::max(0, iround(bb.width)), std::max(0, iround(bb.height))};
	                    if (!intersects(active, tbb)) continue;
	                    const auto& t = cmd->renderData.text;
	                    const auto& col = t.textColor;
	                    sdtx_origin(bb.x / cell, bb.y / cell);
	                    sdtx_home();
	                    sdtx_color3f(col.r / 255.0f, col.g / 255.0f, col.b / 255.0f);
	                    sdtx_putr(t.stringContents.chars, t.stringContents.length);
	                }
	            }
	        } else {
	            for (int32_t id : draw_list) {
	                if (id == 0) continue;
	                auto itn = coi::ui::g_nodes.find(id);
                if (itn == coi::ui::g_nodes.end()) continue;
                const auto& n = itn->second;
                if (n.tag == "comment") continue;
                auto itr = layout.find(id);
                if (itr == layout.end()) continue;
                const Rect& r = itr->second;
                const float pad = 12.0f;
                const float x = r.x + pad;
                const float y = r.y + pad;
                const float w = std::max(1.0f, r.w - pad * 2.0f);
                int cols = (int)std::floor(w / cell);
                if (cols < 1) cols = 1;
                if (!n.text.empty()) {
                    sdtx_origin(x / cell, y / cell);
                    sdtx_home();
                    sdtx_color3f(0.05f, 0.05f, 0.06f);
                    sdtx_put_wrapped(n.text.c_str(), cols);
                } else {
                    const webcc::string* cls = attr(n, "class");
                    std::string label = n.tag.c_str();
                    if (cls && !cls->empty()) {
                        label += ".";
                        label += cls->c_str();
                    }
                    sdtx_origin((r.x + 6.0f) / cell, (r.y + 6.0f) / cell);
                    sdtx_home();
                    sdtx_color3f(0.85f, 0.85f, 0.90f);
                    sdtx_put_wrapped(label.c_str(), std::max(1, cols));
                }
            }
        }
#else
        for (int32_t id : draw_list) {
            if (id == 0) continue;
            auto itn = coi::ui::g_nodes.find(id);
            if (itn == coi::ui::g_nodes.end()) continue;
            const auto& n = itn->second;
            if (n.tag == "comment") continue;
            auto itr = layout.find(id);
            if (itr == layout.end()) continue;
            const Rect& r = itr->second;
            const float pad = 12.0f;
            const float x = r.x + pad;
            const float y = r.y + pad;
            const float w = std::max(1.0f, r.w - pad * 2.0f);
            int cols = (int)std::floor(w / cell);
            if (cols < 1) cols = 1;
            if (!n.text.empty()) {
                sdtx_origin(x / cell, y / cell);
                sdtx_home();
                sdtx_color3f(0.05f, 0.05f, 0.06f);
                sdtx_put_wrapped(n.text.c_str(), cols);
            } else {
                const webcc::string* cls = attr(n, "class");
                std::string label = n.tag.c_str();
                if (cls && !cls->empty()) {
                    label += ".";
                    label += cls->c_str();
                }
                sdtx_origin((r.x + 6.0f) / cell, (r.y + 6.0f) / cell);
                sdtx_home();
                sdtx_color3f(0.85f, 0.85f, 0.90f);
                sdtx_put_wrapped(label.c_str(), std::max(1, cols));
            }
        }
#endif

            const char* show_dump = std::getenv("COI_NATIVE_SHOW_DUMP");
            if (show_dump && *show_dump && std::string(show_dump) != std::string("0")) {
                sdtx_origin(1.0f, 6.0f);
                sdtx_home();
                sdtx_color3f(1.0f, 1.0f, 1.0f);
                sdtx_puts("\nUI tree (dump):\n");
                sdtx_dump_node(0, 0);
            }
            sdtx_draw();
        }

#if defined(COI_NATIVE_CAPTURE)
        // In X11 capture mode, read back the swapchain framebuffer before ending the pass.
        capture_maybe_swapchain(dt);
#endif
        sg_end_pass();
#if defined(COI_NATIVE_CAPTURE)
        // In offscreen capture mode, render to a separate target after ending the swapchain pass.
        capture_maybe(pass, dt);
#endif
        sg_commit();

        frames++;
        if (frames_limit > 0 && frames >= frames_limit) {
            sapp_request_quit();
        }
    }

	    static void cleanup(void) {
#if defined(COI_NATIVE_FONTSTASH)
#if defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
        sclay_shutdown();
        clay_fonts[0] = FONS_INVALID;
        font_bytes.clear();
#else
        if (fons_ctx) {
            sfons_destroy(fons_ctx);
            fons_ctx = nullptr;
        }
        fons_font = FONS_INVALID;
        font_bytes.clear();
#endif
#endif
#if defined(COI_NATIVE_CAPTURE)
		        capture_shutdown();
#endif
		        sdtx_shutdown();
		        sgl_shutdown();

#if defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
		        DesktopImageCache::shutdown();
#endif

		        sg_shutdown();

#if defined(COI_NATIVE_CLAY)
		        ClayEngine::shutdown();
#endif
		    }

	    static int run(void* app_in, int frames_limit_in, tick_fn tick_in) {
	        app = app_in;
	        tick = tick_in;
	        frames_limit = frames_limit_in;
	        frames = 0;

	        const bool want_window = window_requested();

	        sapp_desc desc{};
	        int win_w = 960;
	        int win_h = 540;
	        desc.window_title = "COI (Desktop)";

#if defined(COI_NATIVE_CAPTURE)
	        const char* dir = std::getenv("COI_NATIVE_CAPTURE_DIR");
	        const bool capture_requested = (dir && *dir);
	        if (!want_window && capture_requested) {
	            // "Headless" capture still needs a GL context. For offscreen capture we prefer
	            // using the capture size as the GL backbuffer size to avoid backend bugs where
	            // origin_top_left viewport/scissor conversions use the swapchain height.
	            const char* size = std::getenv("COI_NATIVE_CAPTURE_SIZE");
	            if (!parse_wh(size, win_w, win_h)) {
	                // Default to a deterministic size matching the visual tests.
	                win_w = 960;
	                win_h = 540;
	            }
	            desc.window_title = "COI (Desktop, headless capture)";
	        }

	        // If X11 capture is selected and a size is provided, prefer it as the window size
	        // so that X11-based capture is deterministic.
	        if (capture_requested) {
	            CaptureMode mode = CaptureMode::Offscreen;
#if defined(__linux__) || defined(__unix__)
	            mode = want_window ? CaptureMode::X11 : CaptureMode::Offscreen;
#endif
	            if (const char* m = std::getenv("COI_NATIVE_CAPTURE_MODE")) {
	                if (m && *m) {
	                    const std::string mm(m);
	                    if (mm == "x11") mode = CaptureMode::X11;
	                    if (mm == "offscreen") mode = CaptureMode::Offscreen;
	                }
	            }
	            if (mode == CaptureMode::X11) {
	                const char* size = std::getenv("COI_NATIVE_CAPTURE_SIZE");
	                int cw = 0, ch = 0;
	                if (parse_wh(size, cw, ch)) {
	                    win_w = cw;
	                    win_h = ch;
	                }
	            }
	        }
#endif
	        desc.width = win_w;
	        desc.height = win_h;
	        desc.init_cb = init;
	        desc.frame_cb = frame_cb;
	        desc.event_cb = event_cb;
	        desc.cleanup_cb = cleanup;
	        sapp_run(&desc);
#if defined(COI_NATIVE_CAPTURE)
        return exit_code;
#else
        return 0;
#endif
    }

#if defined(COI_NATIVE_FONTSTASH)
#if defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
    static inline sclay_font_t clay_fonts[1] = {FONS_INVALID};
    static inline std::vector<unsigned char> font_bytes;
#else
    static inline FONScontext* fons_ctx = nullptr;
    static inline int fons_font = FONS_INVALID;
    static inline std::vector<unsigned char> font_bytes;
#endif
#endif
};

#endif // defined(COI_NATIVE_SOKOL)

int run_sokol(void* app, int frames, tick_fn tick) {
#if defined(COI_NATIVE_SOKOL)
    return SokolRunnerImpl::run(app, frames, tick);
#else
    (void)app;
    (void)frames;
    (void)tick;
    std::cerr << "COI native runtime built without Sokol support\n";
    return 1;
#endif
}

inline bool g_layout_dumped = false;
inline bool g_click_done = false;
inline bool g_render_dumped = false;
inline bool g_scroll_done = false;

inline bool parse_xy(const char* s, float& x, float& y) {
    if (!s || !*s) return false;
    int ix = 0, iy = 0;
    if (std::sscanf(s, "%d,%d", &ix, &iy) == 2 || std::sscanf(s, "%d %d", &ix, &iy) == 2) {
        x = (float)ix;
        y = (float)iy;
        return true;
    }
    return false;
}

inline bool parse_viewport(float& w, float& h) {
    const char* vw = std::getenv("COI_NATIVE_VIEWPORT");
    if (!vw || !*vw) {
        w = 960.0f;
        h = 540.0f;
        return true;
    }
    int iw = 0;
    int ih = 0;
    if (std::sscanf(vw, "%dx%d", &iw, &ih) == 2 || std::sscanf(vw, "%d,%d", &iw, &ih) == 2) {
        if (iw <= 0) iw = 1;
        if (ih <= 0) ih = 1;
        w = (float)iw;
        h = (float)ih;
        return true;
    }
    w = 960.0f;
    h = 540.0f;
    return true;
}

inline void dump_layout(float w, float h);
inline void dump_layout() {
#if defined(COI_NATIVE_CLAY)
    float w = 0.0f, h = 0.0f;
    parse_viewport(w, h);
    dump_layout(w, h);
#endif
}

inline void dump_layout(float w, float h) {
#if defined(COI_NATIVE_CLAY)
    (void)ClayEngine::layout(w, h);
    if (!ClayEngine::ctx) return;

    std::cout << "--- COI_NATIVE_LAYOUT ---\n";
    auto dump = [&](auto&& self, int32_t id, int depth) -> void {
        auto it = coi::ui::g_nodes.find(id);
        if (it == coi::ui::g_nodes.end()) return;
        const auto& n = it->second;
        if (n.tag == "comment") return;
        Clay_ElementData d = Clay_GetElementData(ClayEngine::element_id(id));
        if (!d.found) return;
        for (int i = 0; i < depth; i++) std::cout << "  ";
        std::cout << "<" << n.tag.c_str();
        for (const auto& a : n.attrs) {
            if (a.key == "class") continue;
            std::cout << " " << a.key.c_str() << "=\"" << a.value.c_str() << "\"";
        }
        if (const webcc::string* cls = attr(n, "class")) {
            std::cout << " class=\"" << cls->c_str() << "\"";
        }
        std::cout << ">";
        if (!n.text.empty()) std::cout << n.text.c_str();
        std::cout << "</" << n.tag.c_str() << ">";
        std::cout << " x=" << (int)std::lround(d.boundingBox.x) << " y=" << (int)std::lround(d.boundingBox.y)
                  << " w=" << (int)std::lround(d.boundingBox.width) << " h=" << (int)std::lround(d.boundingBox.height)
                  << "\n";
        for (int32_t c : n.children) self(self, c, depth + 1);
    };
    dump(dump, 0, 0);
    std::cout << std::flush;
#endif
}

inline void dump_render(float w, float h);
inline void dump_render() {
#if defined(COI_NATIVE_CLAY)
    float w = 0.0f, h = 0.0f;
    parse_viewport(w, h);
    dump_render(w, h);
#endif
}

inline void dump_render(float w, float h) {
#if defined(COI_NATIVE_CLAY)
    Clay_RenderCommandArray render_commands = ClayEngine::layout(w, h);
    if (!ClayEngine::ctx) return;

    std::cout << "--- COI_NATIVE_RENDER_DUMP ---\n";
    auto iround = [](float v) -> int { return (int)std::lround((double)v); };
    auto escape = [](const char* s, int32_t len) -> std::string {
        std::string out;
        out.reserve((size_t)len);
        for (int32_t i = 0; i < len; i++) {
            char c = s[i];
            if (c == '\\') {
                out += "\\\\";
            } else if (c == '"') {
                out += "\\\"";
            } else if (c == '\n') {
                out += "\\n";
            } else if (c == '\r') {
                out += "\\r";
            } else if (c == '\t') {
                out += "\\t";
            } else {
                out.push_back(c);
            }
        }
        return out;
    };
    for (int32_t i = 0; i < render_commands.length; i++) {
        Clay_RenderCommand* cmd = Clay_RenderCommandArray_Get(&render_commands, i);
        if (!cmd) continue;
        const auto& bb = cmd->boundingBox;

        if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_SCISSOR_START) {
            std::cout << "SCISSOR_START x=" << iround(bb.x) << " y=" << iround(bb.y) << " w=" << iround(bb.width) << " h=" << iround(bb.height) << "\n";
        } else if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_SCISSOR_END) {
            std::cout << "SCISSOR_END\n";
        } else if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_BORDER) {
            const auto& b = cmd->renderData.border;
            const auto& c = b.color;
            if (c.a <= 0.0f) continue;
            const auto& w = b.width;
            if (!(w.left || w.right || w.top || w.bottom || w.betweenChildren)) continue;

            const int32_t owner = cmd->userData ? (int32_t)(intptr_t)cmd->userData : 0;
            const auto itn = (owner != 0) ? coi::ui::g_nodes.find(owner) : coi::ui::g_nodes.end();

            std::cout << "BORDER";
            if (itn != coi::ui::g_nodes.end()) {
                const auto& n = itn->second;
                const webcc::string* cls = attr(n, "class");
                std::cout << " owner=" << owner << " tag=" << n.tag.c_str();
                if (cls && !cls->empty()) std::cout << " class=\"" << cls->c_str() << "\"";
            } else if (owner != 0) {
                std::cout << " owner=" << owner;
            }
            std::cout << " x=" << iround(bb.x) << " y=" << iround(bb.y) << " w=" << iround(bb.width) << " h=" << iround(bb.height);
            std::cout << " l=" << (int)w.left << " r=" << (int)w.right << " t=" << (int)w.top << " b=" << (int)w.bottom
                      << " between=" << (int)w.betweenChildren;
            std::cout << " color=" << (int)c.r << "," << (int)c.g << "," << (int)c.b << "," << (int)c.a << "\n";
        } else if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_RECTANGLE) {
            const auto& c = cmd->renderData.rectangle.backgroundColor;
            if (c.a <= 0.0f) continue;

            int32_t hid = (int32_t)(cmd->id ^ 0xC01D0000u);
            auto itn = coi::ui::g_nodes.find(hid);
            if (itn != coi::ui::g_nodes.end()) {
                const auto& n = itn->second;
                const webcc::string* cls = attr(n, "class");

                std::cout << "RECT id=" << hid << " tag=" << n.tag.c_str();
                if (cls && !cls->empty()) std::cout << " class=\"" << cls->c_str() << "\"";
                std::cout << " x=" << iround(bb.x) << " y=" << iround(bb.y) << " w=" << iround(bb.width) << " h=" << iround(bb.height) << "\n";
                continue;
            }

            // Clay emits "betweenChildren" borders as anonymous RECTANGLE commands. Expose those in the dump.
            const int32_t owner = cmd->userData ? (int32_t)(intptr_t)cmd->userData : 0;
            if (owner != 0) {
                auto ito = coi::ui::g_nodes.find(owner);
                if (ito != coi::ui::g_nodes.end()) {
                    const auto& n = ito->second;
                    DesktopClassStyle st = parse_desktop_class_style(n, owner == 0);
                    if (st.has_border && st.border_width.betweenChildren > 0) {
                        const webcc::string* cls = attr(n, "class");
                        std::cout << "RECT_BETWEEN owner=" << owner << " tag=" << n.tag.c_str();
                        if (cls && !cls->empty()) std::cout << " class=\"" << cls->c_str() << "\"";
                        std::cout << " x=" << iround(bb.x) << " y=" << iround(bb.y) << " w=" << iround(bb.width) << " h=" << iround(bb.height);
                        std::cout << " color=" << (int)c.r << "," << (int)c.g << "," << (int)c.b << "," << (int)c.a << "\n";
                    }
                }
            }
        } else if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_TEXT) {
            const auto& t = cmd->renderData.text;
            const int32_t owner = cmd->userData ? (int32_t)(intptr_t)cmd->userData : 0;
            const auto itn = (owner != 0) ? coi::ui::g_nodes.find(owner) : coi::ui::g_nodes.end();

            std::cout << "TEXT";
            if (itn != coi::ui::g_nodes.end()) {
                const auto& n = itn->second;
                const webcc::string* cls = attr(n, "class");
                std::cout << " owner=" << owner << " tag=" << n.tag.c_str();
                if (cls && !cls->empty()) std::cout << " class=\"" << cls->c_str() << "\"";
            } else if (owner != 0) {
                std::cout << " owner=" << owner;
            }
            std::cout << " x=" << iround(bb.x) << " y=" << iround(bb.y) << " w=" << iround(bb.width) << " h=" << iround(bb.height);
            std::cout << " color=" << (int)t.textColor.r << "," << (int)t.textColor.g << "," << (int)t.textColor.b << "," << (int)t.textColor.a;
            std::cout << " font=" << (int)t.fontId << " size=" << (int)t.fontSize << " ls=" << (int)t.letterSpacing << " lh=" << (int)t.lineHeight;
            std::cout << " text=\"" << escape(t.stringContents.chars, t.stringContents.length) << "\"\n";
        }
    }
    std::cout << std::flush;
#endif
}

inline void dump_tree_force() {
    std::cout << "--- COI_NATIVE_DUMP ---\n";
    auto dump = [&](auto&& self, int32_t id, int depth) -> void {
        auto it = coi::ui::g_nodes.find(id);
        if (it == coi::ui::g_nodes.end()) return;
        const coi::ui::Node& n = it->second;
        for (int i = 0; i < depth; i++) std::cout << "  ";
        std::cout << "<" << n.tag.c_str();
        for (const auto& a : n.attrs) {
            std::cout << " " << a.key.c_str() << "=\"" << a.value.c_str() << "\"";
        }
        std::cout << ">";
        if (!n.text.empty()) std::cout << n.text.c_str();
        std::cout << "</" << n.tag.c_str() << ">\n";
        for (int32_t c : n.children) self(self, c, depth + 1);
    };
    dump(dump, 0, 0);
    std::cout << std::flush;
}

inline void simulate_click_at(float x, float y, float w, float h) {
    if (!g_click_dispatcher) return;
#if defined(COI_NATIVE_CLAY)
    (void)ClayEngine::layout(w, h);
    if (!ClayEngine::ctx) return;
    Clay_SetCurrentContext(ClayEngine::ctx);
    Clay_SetPointerState(Clay_Vector2{x, y}, false);
    Clay_ElementIdArray ids = Clay_GetPointerOverIds();
    for (int32_t i = ids.length - 1; i >= 0; --i) {
        Clay_ElementId* eid = Clay_ElementIdArray_Get(&ids, i);
        if (!eid) continue;
        int32_t hid = (int32_t)(eid->id ^ 0xC01D0000u);
        if (coi::ui::g_nodes.find(hid) == coi::ui::g_nodes.end()) continue;
        dispatch_click_bubble(webcc::handle(hid));
        return;
    }
#else
    (void)x;
    (void)y;
    (void)w;
    (void)h;
#endif
}

inline void simulate_scroll_at(float pointer_x, float pointer_y, float dx, float dy, float w, float h) {
#if defined(COI_NATIVE_CLAY)
    // Prime scroll container mappings and pointer-over state.
    ClayEngine::set_input(pointer_x, pointer_y, false, 0.0f, 0.0f, 1.0f / 60.0f);
    (void)ClayEngine::layout(w, h);
    // Apply scroll. Deltas follow browser-like semantics.
    ClayEngine::set_input(pointer_x, pointer_y, false, -dx, -dy, 1.0f / 60.0f);
    (void)ClayEngine::layout(w, h);
#else
    (void)pointer_x;
    (void)pointer_y;
    (void)dx;
    (void)dy;
    (void)w;
    (void)h;
#endif
}

inline void maybe_simulate_click() {
    const char* c = std::getenv("COI_NATIVE_CLICK");
    if (!c || !*c) return;
    if (g_click_done && std::string(c) != std::string("always")) return;

    float x = 0.0f, y = 0.0f;
    if (!parse_xy(c, x, y)) return;
    if (!g_click_dispatcher) return;

#if defined(COI_NATIVE_CLAY)
    float w = 0.0f, h = 0.0f;
    parse_viewport(w, h);
    simulate_click_at(x, y, w, h);
#else
    (void)x;
    (void)y;
#endif
    g_click_done = true;
}

inline void maybe_simulate_scroll() {
    const char* s = std::getenv("COI_NATIVE_SCROLL");
    if (!s || !*s) return;
    if (g_scroll_done && std::string(s) != std::string("always")) return;

    float dx = 0.0f, dy = 0.0f;
    if (!parse_xy(s, dx, dy)) return;

    float px = 1.0f, py = 1.0f;
    const char* p = std::getenv("COI_NATIVE_POINTER");
    if (p && *p) (void)parse_xy(p, px, py);

#if defined(COI_NATIVE_CLAY)
    float w = 0.0f, h = 0.0f;
    parse_viewport(w, h);
    simulate_scroll_at(px, py, dx, dy, w, h);
#else
    (void)dx;
    (void)dy;
    (void)px;
    (void)py;
#endif
    g_scroll_done = true;
}

inline bool parse_wh(const std::string& s, float& w, float& h) {
    int iw = 0;
    int ih = 0;
    if (std::sscanf(s.c_str(), "%dx%d", &iw, &ih) == 2 || std::sscanf(s.c_str(), "%d,%d", &iw, &ih) == 2 ||
        std::sscanf(s.c_str(), "%d %d", &iw, &ih) == 2) {
        if (iw <= 0) iw = 1;
        if (ih <= 0) ih = 1;
        w = (float)iw;
        h = (float)ih;
        return true;
    }
    return false;
}

inline std::string trim_copy(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
    size_t j = s.size();
    while (j > i && (s[j - 1] == ' ' || s[j - 1] == '\t' || s[j - 1] == '\r' || s[j - 1] == '\n')) j--;
    return s.substr(i, j - i);
}

inline std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
        if (i >= s.size()) break;
        size_t j = i;
        while (j < s.size() && (s[j] != ' ' && s[j] != '\t')) j++;
        out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

struct DesktopScriptRunner {
    bool loaded = false;
    bool ran = false;
    std::string path;
    std::vector<std::string> lines;

    float viewport_w = 0.0f;
    float viewport_h = 0.0f;
    float pointer_x = 1.0f;
    float pointer_y = 1.0f;

    bool dump_tree = true;
    bool dump_layout = false;
    bool dump_render = false;

    size_t next_line = 0;
    int step = 0;
    int wait_frames = 0;
};

inline DesktopScriptRunner g_script;

inline void load_script_if_any() {
    if (g_script.loaded) return;
    const char* p = std::getenv("COI_NATIVE_SCRIPT");
    if (!p || !*p) return;
    g_script.loaded = true;
    g_script.path = p;

    parse_viewport(g_script.viewport_w, g_script.viewport_h);
    const char* ptr = std::getenv("COI_NATIVE_POINTER");
    if (ptr && *ptr) (void)parse_xy(ptr, g_script.pointer_x, g_script.pointer_y);

    const char* dumps = std::getenv("COI_NATIVE_SCRIPT_DUMPS");
    if (dumps && *dumps) {
        g_script.dump_tree = false;
        g_script.dump_layout = false;
        g_script.dump_render = false;
        std::string raw = trim_copy(std::string(dumps));
        if (raw == "0" || raw == "off" || raw == "none") {
            // Explicitly disable all dumps.
        } else {
        std::string s = dumps;
        for (char& c : s) c = (c == ';') ? ',' : c;
        auto parts = split_ws(s);
        // also accept comma-separated in a single token
        std::vector<std::string> expanded;
        for (const auto& tok : parts) {
            size_t start = 0;
            while (start < tok.size()) {
                size_t end = tok.find(',', start);
                if (end == std::string::npos) end = tok.size();
                if (end > start) expanded.push_back(tok.substr(start, end - start));
                start = end + 1;
            }
        }
        for (auto t : expanded) {
            for (char& c : t) c = (char)std::tolower((unsigned char)c);
            if (t == "all") {
                g_script.dump_tree = true;
                g_script.dump_layout = true;
                g_script.dump_render = true;
            } else if (t == "dump" || t == "tree") {
                g_script.dump_tree = true;
            } else if (t == "layout") {
                g_script.dump_layout = true;
            } else if (t == "render" || t == "render_dump") {
                g_script.dump_render = true;
            }
        }
        // default if parsed nothing
        if (!g_script.dump_tree && !g_script.dump_layout && !g_script.dump_render) g_script.dump_tree = true;
        }
    }

    std::ifstream f(g_script.path);
    if (!f) {
        std::cerr << "[native-script] failed to open: " << g_script.path << "\n";
        g_script.ran = true;
        return;
    }
    std::string line;
    while (std::getline(f, line)) g_script.lines.push_back(line);
}

inline void script_snapshot(int step, const std::string& line) {
    std::cout << "--- COI_NATIVE_SCRIPT_STEP " << step << ": " << line << " ---\n";
    if (g_script.dump_tree) dump_tree_force();
    if (g_script.dump_layout) dump_layout(g_script.viewport_w, g_script.viewport_h);
    if (g_script.dump_render) dump_render(g_script.viewport_w, g_script.viewport_h);
    std::cout << std::flush;
}

inline void run_script_if_any() {
    load_script_if_any();
    if (!g_script.loaded || g_script.ran) return;

    // Handle waits across frames.
    if (g_script.wait_frames > 0) {
        g_script.wait_frames--;
        if (g_script.wait_frames > 0) return;
        // If wait reached zero, continue executing remaining script lines below.
    }

    for (; g_script.next_line < g_script.lines.size(); g_script.next_line++) {
        const auto& raw = g_script.lines[g_script.next_line];
        std::string line = trim_copy(raw);
        if (line.empty()) continue;
        if (line[0] == '#') continue;

        auto toks = split_ws(line);
        if (toks.empty()) continue;
        std::string cmd = toks[0];
        for (char& c : cmd) c = (char)std::tolower((unsigned char)c);

        bool ok = true;
        if (cmd == "viewport") {
            if (toks.size() < 2) {
                ok = false;
            } else {
                ok = parse_wh(toks[1], g_script.viewport_w, g_script.viewport_h);
                if (!ok && toks.size() >= 3) {
                    ok = parse_wh(toks[1] + " " + toks[2], g_script.viewport_w, g_script.viewport_h);
                }
            }
        } else if (cmd == "wait" || cmd == "frames") {
            if (toks.size() < 2) {
                ok = false;
            } else {
                int n = std::atoi(toks[1].c_str());
                if (n < 0) n = 0;
                g_script.wait_frames = n;
            }
        } else if (cmd == "pointer" || cmd == "move") {
            if (toks.size() < 3) {
                ok = false;
            } else {
                float x = 0.0f, y = 0.0f;
                ok = parse_xy((toks[1] + " " + toks[2]).c_str(), x, y);
                if (ok) {
                    g_script.pointer_x = x;
                    g_script.pointer_y = y;
                }
            }
        } else if (cmd == "click") {
            float x = g_script.pointer_x;
            float y = g_script.pointer_y;
            if (toks.size() >= 3) {
                ok = parse_xy((toks[1] + " " + toks[2]).c_str(), x, y);
            }
            if (ok) simulate_click_at(x, y, g_script.viewport_w, g_script.viewport_h);
        } else if (cmd == "scroll") {
            if (toks.size() < 3) {
                ok = false;
            } else {
                float dx = 0.0f, dy = 0.0f;
                ok = parse_xy((toks[1] + " " + toks[2]).c_str(), dx, dy);
                if (ok) {
                    simulate_scroll_at(g_script.pointer_x, g_script.pointer_y, dx, dy, g_script.viewport_w, g_script.viewport_h);
                }
            }
        } else if (cmd == "snapshot" || cmd == "dump") {
            // no-op; snapshot happens below
        } else {
            ok = false;
        }

        g_script.step++;
        if (!ok) {
            std::cerr << "[native-script] parse error in: " << g_script.path << ": " << line << "\n";
        }
        script_snapshot(g_script.step, line);

        // Yield to the next frame if we just scheduled a wait.
        if ((cmd == "wait" || cmd == "frames") && g_script.wait_frames > 0) {
            g_script.next_line++;
            return;
        }
    }

    g_script.ran = true;
}

void flush() {
    const bool has_window_env = []() -> bool {
        const char* e = std::getenv("COI_NATIVE_WINDOW");
        return (e && *e && std::string(e) != std::string("0"));
    }();
    const bool has_capture_env = []() -> bool {
        const char* e = std::getenv("COI_NATIVE_CAPTURE_DIR");
        return (e && *e);
    }();
    const bool pre_sokol = (has_window_env || has_capture_env) && !g_sokol_frame_started;

    // Desktop scripts and simulated input should run during the actual main loop.
    // When window/capture is enabled, the generated main() calls coi::native::flush()
    // once before starting the Sokol loop; running scripts there would consume steps
    // before the first captured frame.
    if (!pre_sokol) {
        run_script_if_any();
        maybe_simulate_scroll();
        maybe_simulate_click();
    }
    coi::ui::flush();

    const char* render_env = std::getenv("COI_NATIVE_RENDER_DUMP");
    if (render_env && *render_env && !(render_env[0] == '0' && render_env[1] == '\0')) {
        if (!g_render_dumped || std::string(render_env) == std::string("always")) {
            dump_render();
            g_render_dumped = true;
        }
    }

    const char* env = std::getenv("COI_NATIVE_LAYOUT_DUMP");
    if (!env || !*env) return;
    if (env[0] == '0' && env[1] == '\0') return;
    if (g_layout_dumped && std::string(env) != std::string("always")) return;
    g_layout_dumped = true;
    dump_layout();
}
} // namespace coi::native
#endif // COI_NATIVE_SOKOL
