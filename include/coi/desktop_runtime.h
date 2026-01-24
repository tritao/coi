#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
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

#if defined(COI_DESKTOP_CLAY)
#ifndef COI_DESKTOP_RUNTIME_CLAY_INCLUDED
#define COI_DESKTOP_RUNTIME_CLAY_INCLUDED
    #define CLAY_IMPLEMENTATION
    #include "clay.h"
#endif
#endif

namespace coi::desktop {
struct Rect {
    float x, y, w, h;
};

// Desktop event dispatch hooks (set by generated app code).
// The dispatcher is expected to return true when the event was handled.
inline webcc::function<bool(webcc::handle)> g_click_dispatcher;
inline void set_click_dispatcher(webcc::function<bool(webcc::handle)> cb) {
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
inline void flush();

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

#if defined(COI_DESKTOP_CLAY)
struct DesktopClassStyle {
    bool has_dir = false;
    Clay_LayoutDirection dir = CLAY_TOP_TO_BOTTOM;

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

    bool bg_none = false;

    bool has_align_x = false;
    Clay_LayoutAlignmentX align_x = CLAY_ALIGN_X_LEFT;

    bool has_align_y = false;
    Clay_LayoutAlignmentY align_y = CLAY_ALIGN_Y_TOP;
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
        if (parse_f32_suffix("w-", st.w, st.w_fixed)) return;
        if (parse_f32_suffix("h-", st.h, st.h_fixed)) return;
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
            Clay_SetLayoutDimensions(Clay_Dimensions{w, h});
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
            .textColor = {20, 20, 22, 255},
            .fontSize = 8,
            .letterSpacing = 0,
            .lineHeight = 8,
            .wrapMode = CLAY_TEXT_WRAP_WORDS,
            .textAlignment = CLAY_TEXT_ALIGN_LEFT,
        });
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
        const uint16_t default_pad = is_root ? 0 : 12;
        const uint16_t default_gap = is_root ? 0 : 10;
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

        Clay_ElementDeclaration decl{};
        decl.layout = Clay_LayoutConfig{
            .sizing = Clay_Sizing{.width = sx, .height = sy},
            .padding = Clay_Padding{pad, pad, pad, pad},
            .childGap = gap,
            .childAlignment = Clay_ChildAlignment{ax, ay},
            .layoutDirection = dir,
        };
        if (is_root || st.bg_none) {
            decl.backgroundColor = Clay_Color{0, 0, 0, 0};
        } else {
            const webcc::string* cls = attr(n, "class");
            uint32_t h = hash_u32(cls ? cls->c_str() : n.tag.c_str());
            float cr, cg, cb;
            color_from_hash(h, cr, cg, cb);
            decl.backgroundColor = Clay_Color{cr * 255.0f, cg * 255.0f, cb * 255.0f, 46.0f};
        }
        return decl;
    }

    static void build_node(int32_t id, bool is_root) {
        auto it = coi::ui::g_nodes.find(id);
        if (it == coi::ui::g_nodes.end()) return;
        const auto& n = it->second;
        if (n.tag == "comment") return;

        Clay_ElementDeclaration decl = declaration_for_node(n, is_root);
        Clay_ElementId eid = element_id(id);
        CLAY(eid, decl) {
            if (!n.text.empty()) {
                Clay_String t = clay_string(n.text);
                CLAY_TEXT(t, text_cfg);
            }
            for (int32_t c : n.children) build_node(c, false);
        }
    }

    static Clay_RenderCommandArray layout(float w, float h) {
        ensure(w, h);
        if (!ctx) return Clay_RenderCommandArray{};
        Clay_SetCurrentContext(ctx);
        Clay_SetLayoutDimensions(Clay_Dimensions{w, h});
        Clay_BeginLayout();
        build_node(0, true);
        return Clay_EndLayout();
    }
};
#endif // COI_DESKTOP_CLAY

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

#if defined(COI_DESKTOP_CLAY)
        ClayEngine::ensure((float)sapp_width(), (float)sapp_height());
#endif
    }

    static inline float mouse_x = 0.0f;
    static inline float mouse_y = 0.0f;
    static inline bool mouse_down = false;
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
        double dt = sapp_frame_duration();
        if (dt > 0.1) dt = 0.1;
        if constexpr (requires(AppT* a, double d) { a->tick(d); }) {
            if (app) app->tick(dt);
        }

        coi::desktop::flush();

#if defined(COI_DESKTOP_CLAY)
        Clay_RenderCommandArray render_commands = ClayEngine::layout((float)sapp_width(), (float)sapp_height());
        const bool clay_ok = (ClayEngine::ctx != nullptr);
        if (!clay_ok) layout_tree((float)sapp_width(), (float)sapp_height());
#else
        layout_tree((float)sapp_width(), (float)sapp_height());
#endif

#if defined(COI_DESKTOP_CLAY)
        if (clay_ok && click_pending && g_click_dispatcher) {
            click_pending = false;
            Clay_SetCurrentContext(ClayEngine::ctx);
            Clay_SetPointerState(Clay_Vector2{click_x, click_y}, false);
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

        sgl_begin_quads();
#if defined(COI_DESKTOP_CLAY)
        if (clay_ok) {
            for (int32_t i = 0; i < render_commands.length; i++) {
                Clay_RenderCommand* cmd = Clay_RenderCommandArray_Get(&render_commands, i);
                if (!cmd) continue;
                if (cmd->commandType != CLAY_RENDER_COMMAND_TYPE_RECTANGLE) continue;
                const auto& bb = cmd->boundingBox;
                const auto& c = cmd->renderData.rectangle.backgroundColor;
                sgl_c4f(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
                float x0 = bb.x, y0 = bb.y, x1 = bb.x + bb.width, y1 = bb.y + bb.height;
                sgl_v2f(x0, y0);
                sgl_v2f(x1, y0);
                sgl_v2f(x1, y1);
                sgl_v2f(x0, y1);
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
#endif
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

#if defined(COI_DESKTOP_CLAY)
        if (clay_ok) {
            // Render clay text commands using sokol_debugtext (monospace-ish).
            for (int32_t i = 0; i < render_commands.length; i++) {
                Clay_RenderCommand* cmd = Clay_RenderCommandArray_Get(&render_commands, i);
                if (!cmd) continue;
                if (cmd->commandType != CLAY_RENDER_COMMAND_TYPE_TEXT) continue;
                const auto& bb = cmd->boundingBox;
                const auto& t = cmd->renderData.text;
                const auto& col = t.textColor;
                sdtx_origin(bb.x / cell, bb.y / cell);
                sdtx_home();
                sdtx_color3f(col.r / 255.0f, col.g / 255.0f, col.b / 255.0f);
                sdtx_putr(t.stringContents.chars, t.stringContents.length);
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

#if defined(COI_DESKTOP_CLAY)
        ClayEngine::shutdown();
#endif
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
        desc.event_cb = event_cb;
        desc.cleanup_cb = cleanup;
        sapp_run(&desc);
        return 0;
    }
};

inline bool g_layout_dumped = false;
inline bool g_click_done = false;

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
    const char* vw = std::getenv("COI_DESKTOP_VIEWPORT");
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

inline void dump_layout() {
#if defined(COI_DESKTOP_CLAY)
    float w = 0.0f, h = 0.0f;
    parse_viewport(w, h);
    (void)ClayEngine::layout(w, h);
    if (!ClayEngine::ctx) return;

    std::cout << "--- COI_DESKTOP_LAYOUT ---\n";
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

inline void maybe_simulate_click() {
    const char* c = std::getenv("COI_DESKTOP_CLICK");
    if (!c || !*c) return;
    if (g_click_done && std::string(c) != std::string("always")) return;

    float x = 0.0f, y = 0.0f;
    if (!parse_xy(c, x, y)) return;
    if (!g_click_dispatcher) return;

#if defined(COI_DESKTOP_CLAY)
    float w = 0.0f, h = 0.0f;
    parse_viewport(w, h);
    (void)ClayEngine::layout(w, h);
    if (!ClayEngine::ctx) return;
    Clay_SetCurrentContext(ClayEngine::ctx);
    Clay_SetPointerState(Clay_Vector2{x, y}, false);
    Clay_ElementIdArray ids = Clay_GetPointerOverIds();
    // ids are ordered from root->leaf, so traverse backwards to find a COI handle node.
    for (int32_t i = ids.length - 1; i >= 0; --i) {
        Clay_ElementId* eid = Clay_ElementIdArray_Get(&ids, i);
        if (!eid) continue;
        int32_t hid = (int32_t)(eid->id ^ 0xC01D0000u);
        if (coi::ui::g_nodes.find(hid) == coi::ui::g_nodes.end()) continue;
        dispatch_click_bubble(webcc::handle(hid));
        g_click_done = true;
        return;
    }
#else
    (void)x;
    (void)y;
#endif
    g_click_done = true;
}

inline void flush() {
    maybe_simulate_click();
    coi::ui::flush();
    const char* env = std::getenv("COI_DESKTOP_LAYOUT_DUMP");
    if (!env || !*env) return;
    if (g_layout_dumped && std::string(env) != std::string("always")) return;
    g_layout_dumped = true;
    dump_layout();
}
} // namespace coi::desktop
#endif // COI_DESKTOP_SOKOL
