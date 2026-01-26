#include "runtime/clay/engine.h"

#if defined(COI_NATIVE_CLAY)

#include "runtime/prelude.h"

#include "runtime/clay_internal.h"
#include "runtime/clay/style_map.h"
#include "runtime/util.h"

namespace coi::native {
namespace {

Clay_Context* s_ctx = nullptr;
void* s_mem = nullptr;
size_t s_mem_size = 0;
Clay_TextElementConfig* s_text_cfg = nullptr;
Clay_Vector2 s_pointer_pos = Clay_Vector2{0, 0};
bool s_pointer_down = false;
// Clay_UpdateScrollContainers expects scroll deltas where negative Y scrolls "down" (because scrollPosition is clamped <= 0).
Clay_Vector2 s_scroll_delta = Clay_Vector2{0, 0};
float s_frame_dt = 1.0f / 60.0f;
bool s_enable_drag_scroll = true;

void error_handler(Clay_ErrorData data) {
    std::cerr << "[Clay] error " << (int)data.errorType << ": ";
    if (data.errorText.chars && data.errorText.length > 0) {
        std::cerr.write(data.errorText.chars, data.errorText.length);
    } else {
        std::cerr << "(no message)";
    }
    std::cerr << std::endl;
}

Clay_Dimensions measure_text(Clay_StringSlice text, Clay_TextElementConfig* config, void*) {
    const float font_size = config ? (float)config->fontSize : 8.0f;
    const float letter_spacing = config ? (float)config->letterSpacing : 0.0f;
    const float line_h = (config && config->lineHeight) ? (float)config->lineHeight : font_size;
    const float char_w = font_size + letter_spacing;
    return Clay_Dimensions{(float)text.length * char_w, line_h};
}

Clay_String clay_string(const webcc::string& s) { return Clay_String{false, (int32_t)std::strlen(s.c_str()), s.c_str()}; }

void pre_layout_update() {
    Clay_SetPointerState(s_pointer_pos, s_pointer_down);
    Clay_UpdateScrollContainers(s_enable_drag_scroll, s_scroll_delta, s_frame_dt);
    s_scroll_delta = Clay_Vector2{0, 0};
}

Clay_ElementDeclaration declaration_for_node(const coi::ui::Node& n, bool is_root) {
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

Clay_ElementDeclaration declaration_for_node(const coi::ui::Node& n, bool is_root, int32_t id) {
    Clay_ElementDeclaration decl = declaration_for_node(n, is_root);
    decl.userData = (void*)(intptr_t)id;
    return decl;
}

Clay_ElementDeclaration declaration_for_node_open(const coi::ui::Node& n, bool is_root, int32_t id) {
    Clay_ElementDeclaration decl = declaration_for_node(n, is_root, id);
    DesktopClassStyle st = parse_desktop_class_style(n, is_root);
    if (st.has_clip && (st.scroll_x || st.scroll_y)) {
        decl.clip.childOffset = ::coi_native_clay_scroll_offset_for_open_element();
    }
    return decl;
}

void build_node(int32_t id, bool is_root) {
    auto it = coi::ui::g_nodes.find(id);
    if (it == coi::ui::g_nodes.end()) return;
    const auto& n = it->second;
    if (n.tag == "comment") return;

    Clay_ElementId eid = ClayEngine::element_id(id);
    CLAY(eid, (declaration_for_node_open(n, is_root, id))) {
        if (!n.text.empty()) {
            Clay_String t = clay_string(n.text);
            Clay_TextElementConfig* cfgp = s_text_cfg;
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

} // namespace

Clay_Context* ClayEngine::ctx() { return s_ctx; }

uint32_t ClayEngine::id_from_handle(int32_t h) { return 0xC01D0000u ^ (uint32_t)h; }

Clay_ElementId ClayEngine::element_id(int32_t h) { return Clay_ElementId{.id = id_from_handle(h)}; }

void ClayEngine::ensure(float w, float h) {
    if (s_ctx) {
        Clay_SetCurrentContext(s_ctx);
        return;
    }
    uint32_t min_bytes = Clay_MinMemorySize();
    s_mem_size = (size_t)min_bytes + (size_t)min_bytes / 2 + 64 * 1024;
    s_mem = std::malloc(s_mem_size);
    if (!s_mem) {
        std::cerr << "[Clay] failed to allocate " << s_mem_size << " bytes\n";
        return;
    }
    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(s_mem_size, s_mem);
    s_ctx = Clay_Initialize(arena, Clay_Dimensions{w, h}, Clay_ErrorHandler{error_handler, nullptr});
    Clay_SetCurrentContext(s_ctx);
    Clay_SetMeasureTextFunction(measure_text, nullptr);
    s_text_cfg = CLAY_TEXT_CONFIG({
        .textColor = {235, 235, 240, 255},
        .fontId = 0,
        // Match typical browser defaults: 16px text with ~1.125 line-height.
        .fontSize = 16,
        .letterSpacing = 0,
        .lineHeight = 18,
        .wrapMode = CLAY_TEXT_WRAP_WORDS,
        .textAlignment = CLAY_TEXT_ALIGN_LEFT,
    });
}

void ClayEngine::set_input(float x, float y, bool down, float scroll_x, float scroll_y, float dt) {
    s_pointer_pos = Clay_Vector2{x, y};
    s_pointer_down = down;
    s_scroll_delta.x += scroll_x;
    s_scroll_delta.y += scroll_y;
    if (dt > 0.0f) s_frame_dt = dt;
}

Clay_RenderCommandArray ClayEngine::layout(float w_px, float h_px, float dpi_scale) {
#if defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
    const float safe_dpi = (dpi_scale > 0.0f) ? dpi_scale : 1.0f;
    ensure(w_px / safe_dpi, h_px / safe_dpi);
#else
    (void)dpi_scale;
    ensure(w_px, h_px);
#endif
    if (!s_ctx) return Clay_RenderCommandArray{};
    Clay_SetCurrentContext(s_ctx);
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

void ClayEngine::shutdown() {
    if (s_mem) {
        std::free(s_mem);
        s_mem = nullptr;
        s_mem_size = 0;
    }
    s_ctx = nullptr;
    s_text_cfg = nullptr;
}

} // namespace coi::native

#endif // COI_NATIVE_CLAY

