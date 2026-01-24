#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

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
// - Optionally provides a Sokol window renderer under COI_DESKTOP_SOKOL.

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

inline std::unordered_map<int32_t, Node> g_nodes;
inline int32_t g_next_handle = 0x100000;
inline bool g_dumped = false;

inline Node& ensure_node(webcc::handle h) {
    int32_t id = (int32_t)h;
    auto it = g_nodes.find(id);
    if (it == g_nodes.end()) {
        Node n;
        n.parent = webcc::handle();
        it = g_nodes.emplace(id, std::move(n)).first;
    }
    return it->second;
}

inline webcc::handle next_deferred_handle() {
    return webcc::handle(g_next_handle++);
}

inline webcc::handle get_body() {
    auto& b = ensure_node(webcc::handle(0));
    if (b.tag.empty()) b.tag = "body";
    return webcc::handle(0);
}

inline void flush() {
    const char* env = std::getenv("COI_DESKTOP_DUMP");
    if (!env || !*env) return;
    if (g_dumped && std::string(env) != std::string("always")) return;
    g_dumped = true;
    std::cout << "--- COI_DESKTOP_DUMP ---\n";

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

inline void create_element_deferred(webcc::handle h, webcc::string_view tag) {
    auto& n = ensure_node(h);
    n.tag = webcc::string(tag.data(), tag.length());
}
inline void create_comment_deferred(webcc::handle h, webcc::string_view text) {
    auto& n = ensure_node(h);
    n.tag = "comment";
    n.text = webcc::string(text.data(), text.length());
}
inline void set_attribute(webcc::handle h, webcc::string_view name, webcc::string_view value) {
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
inline void set_property(webcc::handle h, webcc::string_view name, webcc::string_view value) {
    set_attribute(h, name, value);
}
inline void set_inner_html(webcc::handle h, webcc::string_view html) {
    auto& n = ensure_node(h);
    n.children.clear();
    n.text = webcc::string(html.data(), html.length());
}
inline void set_inner_text(webcc::handle h, webcc::string_view text) {
    auto& n = ensure_node(h);
    n.children.clear();
    n.text = webcc::string(text.data(), text.length());
}
inline void append_child(webcc::handle parent, webcc::handle child) {
    auto& p = ensure_node(parent);
    auto& c = ensure_node(child);
    c.parent = parent;
    p.children.push_back((int32_t)child);
}
inline void insert_before(webcc::handle parent, webcc::handle child, webcc::handle ref) {
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
inline void remove_element(webcc::handle h) {
    auto& n = ensure_node(h);
    if (!n.parent.is_valid()) return;
    auto& p = ensure_node(n.parent);
    int32_t id = (int32_t)h;
    p.children.erase(std::remove(p.children.begin(), p.children.end(), id), p.children.end());
    n.parent = webcc::handle();
}
inline void move_before(webcc::handle parent, webcc::handle node, webcc::handle ref) {
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

inline void add_click_listener(webcc::handle) {}
inline void add_input_listener(webcc::handle) {}
inline void add_change_listener(webcc::handle) {}
inline void add_keydown_listener(webcc::handle) {}
inline void scroll_to_top() {}
} // namespace coi::ui

#if defined(COI_DESKTOP_SOKOL)
#ifndef COI_DESKTOP_RUNTIME_SOKOL_INCLUDED
#define COI_DESKTOP_RUNTIME_SOKOL_INCLUDED
    #define SOKOL_NO_ENTRY
    #define SOKOL_GLCORE
    #define SOKOL_IMPL
    #define SOKOL_GL_IMPL
    #define SOKOL_DEBUGTEXT_IMPL
    #include "sokol_app.h"
    #include "sokol_gfx.h"
    #include "sokol_gl.h"
    #include "sokol_glue.h"
    #include "sokol_time.h"
    #include "sokol_debugtext.h"
#endif

namespace coi::desktop {
struct Rect {
    float x, y, w, h;
};

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

template <typename AppT>
struct SokolRunner {
    static inline AppT* app = nullptr;
    static inline int frames_limit = -1;
    static inline int frames = 0;
    static inline std::unordered_map<int32_t, Rect> layout;
    static inline std::vector<int32_t> draw_list;

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
        desc.environment = sglue_environment();
        sg_setup(&desc);
        sgl_desc_t gld{};
        sgl_setup(&gld);

        sdtx_desc_t ddesc{};
        ddesc.fonts[0] = sdtx_font_kc853();
        ddesc.fonts[1] = sdtx_font_kc854();
        ddesc.fonts[2] = sdtx_font_z1013();
        ddesc.fonts[3] = sdtx_font_cpc();
        ddesc.fonts[4] = sdtx_font_c64();
        ddesc.fonts[5] = sdtx_font_oric();
        sdtx_setup(&ddesc);
    }

    static void frame_cb(void) {
        double dt = sapp_frame_duration();
        if (dt > 0.1) dt = 0.1;
        if constexpr (requires(AppT* a, double d) { a->tick(d); }) {
            if (app) app->tick(dt);
        }

        coi::ui::flush();
        layout_tree((float)sapp_width(), (float)sapp_height());

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

        const float cell = 8.0f;
        sdtx_canvas((float)sapp_width(), (float)sapp_height());
        sdtx_font(0);
        sdtx_origin(1.0f, 1.0f);
        sdtx_home();
        sdtx_color3f(1.0f, 1.0f, 1.0f);
        sdtx_puts("COI desktop runtime (sokol)\n");
        sdtx_printf("dt: %.3f\n\n", dt);

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

        const char* show_dump = std::getenv("COI_DESKTOP_SHOW_DUMP");
        if (show_dump && *show_dump && std::string(show_dump) != std::string("0")) {
            sdtx_origin(1.0f, 6.0f);
            sdtx_home();
            sdtx_color3f(1.0f, 1.0f, 1.0f);
            sdtx_puts("\nUI tree (dump):\n");
            sdtx_dump_node(0, 0);
        }
        sdtx_draw();

        sg_end_pass();
        sg_commit();

        if (frames_limit > 0) {
            frames++;
            if (frames >= frames_limit) sapp_request_quit();
        }
    }

    static void cleanup(void) {
        sdtx_shutdown();
        sgl_shutdown();
        sg_shutdown();
    }

    static int run(AppT* app_in, int frames_limit_in) {
        app = app_in;
        frames_limit = frames_limit_in;
        frames = 0;

        sapp_desc desc{};
        desc.width = 960;
        desc.height = 540;
        desc.window_title = "COI (Desktop)";
        desc.init_cb = init;
        desc.frame_cb = frame_cb;
        desc.cleanup_cb = cleanup;
        sapp_run(&desc);
        return 0;
    }
};
} // namespace coi::desktop
#endif // COI_DESKTOP_SOKOL

