#pragma once

#include <cstdint>

#include "coi/style/class_style.h"
#if defined(COI_NATIVE)
#include "coi/native/webcc.h"
#else
#include "webcc/core/string.h"
#endif

namespace coi::style {

enum class CssDialect : uint8_t { Web = 0, RmlUi = 1 };

struct Rgb8 {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

static inline uint32_t fnv1a(webcc::string_view s) {
    uint32_t h = 2166136261u;
    const char* p = s.data();
    for (uint32_t i = 0; i < s.length(); i++) {
        h ^= (uint8_t)p[i];
        h *= 16777619u;
    }
    return h;
}

static inline Rgb8 pick_color(webcc::string_view key) {
    const uint32_t h = fnv1a(key);
    uint32_t rv = 64u + (((h >> 0) & 0xFFu) * 166u) / 255u;
    uint32_t gv = 64u + (((h >> 8) & 0xFFu) * 166u) / 255u;
    uint32_t bv = 64u + (((h >> 16) & 0xFFu) * 166u) / 255u;
    if (rv > 255u) rv = 255u;
    if (gv > 255u) gv = 255u;
    if (bv > 255u) bv = 255u;
    return Rgb8{(uint8_t)rv, (uint8_t)gv, (uint8_t)bv};
}

static inline void append_rgba_value(webcc::string& out, CssDialect dialect, Rgb8 c, float alpha01) {
    out += "rgba(";
    out += (int)c.r;
    out += ",";
    out += (int)c.g;
    out += ",";
    out += (int)c.b;
    out += ",";
    if (dialect == CssDialect::Web) {
        // Web CSS expects alpha as 0..1.
        out += (double)alpha01;
    } else {
        // RmlUI expects alpha as 0..255.
        int a = (alpha01 >= 0.0f) ? (int)(alpha01 * 255.0f + 0.5f) : (int)(alpha01 * 255.0f - 0.5f);
        if (a < 0) a = 0;
        if (a > 255) a = 255;
        out += a;
    }
    out += ")";
}

static inline void append_rgba_prop(webcc::string& out, CssDialect dialect, const char* prop, Rgb8 c, float alpha01) {
    out += prop;
    out += ":";
    append_rgba_value(out, dialect, c, alpha01);
    out += ";";
}

static inline void append_px(webcc::string& out, const char* prop, float v) {
    out += prop;
    out += ":";
    out += (v >= 0.0f) ? (int)(v + 0.5f) : (int)(v - 0.5f);
    out += "px;";
}

static inline void append_dp(webcc::string& out, const char* prop, float v) {
    out += prop;
    out += ":";
    out += (v >= 0.0f) ? (int)(v + 0.5f) : (int)(v - 0.5f);
    out += "dp;";
}

static inline void append_clip_path_inset(webcc::string& out, const char* prop, int top, int right, int bottom, int left, uint16_t tl, uint16_t tr,
                                         uint16_t br, uint16_t bl) {
    out += prop;
    out += ":inset(";
    out += top;
    out += "px ";
    out += right;
    out += "px ";
    out += bottom;
    out += "px ";
    out += left;
    out += "px";
    if (tl || tr || bl || br) {
        out += " round ";
        out += (int)tl;
        out += "px ";
        out += (int)tr;
        out += "px ";
        out += (int)br;
        out += "px ";
        out += (int)bl;
        out += "px";
    }
    out += ");";
}

// Web: matches historical codegen behavior (no tag fallback, empty class => empty style).
inline webcc::string css_style_attr_from_class_web(webcc::string_view cls) {
    if (cls.length() == 0) return webcc::string();
    const ClassStyle st = parse_class_style(cls);
    const bool has_grow = (st.grow_x || st.grow_y);
    webcc::string out;

    out += "box-sizing:border-box;";

    // Layout
    if (st.dir != Direction::NoneDir) {
        out += "display:flex;";
        out += "flex-direction:";
        out += (st.dir == Direction::Column) ? "column;" : "row;";
    }
    // Web flexbox shrinks items by default (flex-shrink:1), which differs from Clay's behavior.
    // Prefer fixed-size overflow unless explicitly grow.
    if (has_grow) {
        out += "flex:1 1 0px;min-width:0;min-height:0;";
    } else {
        out += "flex-shrink:0;";
    }

    if (st.has_fill) out += "width:100%;height:100%;min-width:100vw;min-height:100vh;";
    if (st.has_w) append_px(out, "width", st.w);
    if (st.has_h) append_px(out, "height", st.h);
    if (st.has_min_w) append_px(out, "min-width", st.min_w);
    if (st.has_max_w) append_px(out, "max-width", st.max_w);
    if (st.has_min_h) append_px(out, "min-height", st.min_h);
    if (st.has_max_h) append_px(out, "max-height", st.max_h);
    if (st.has_pad) append_px(out, "padding", st.pad);
    if (st.has_gap) append_px(out, "gap", st.gap);

    // Alignment (maps Clay's axis-alignment roughly onto flexbox)
    if (st.dir != Direction::NoneDir) {
        auto jc_for = [&](uint8_t v) -> const char* { return v == 1 ? "center" : (v == 2 ? "flex-end" : "flex-start"); };
        auto ai_for = [&](uint8_t v) -> const char* { return v == 1 ? "center" : (v == 2 ? "flex-end" : "flex-start"); };
        if (st.dir == Direction::Row) {
            if (st.has_align_x) {
                out += "justify-content:";
                out += jc_for(st.align_x);
                out += ";";
            }
            if (st.has_align_y) {
                out += "align-items:";
                out += ai_for(st.align_y);
                out += ";";
            }
        } else {
            if (st.has_align_x) {
                out += "align-items:";
                out += ai_for(st.align_x);
                out += ";";
            }
            if (st.has_align_y) {
                out += "justify-content:";
                out += jc_for(st.align_y);
                out += ";";
            }
        }
    }

    // Positioning (floating)
    if (st.float_root || st.float_parent || st.has_fx || st.has_fy || st.has_z) {
        out += (st.float_root ? "position:fixed;" : "position:absolute;");
        if (st.has_fx) append_px(out, "left", st.fx);
        if (st.has_fy) append_px(out, "top", st.fy);
        if (st.has_z) {
            out += "z-index:";
            out += (int)st.z;
            out += ";";
        }
    }
    if (st.float_pass) {
        out += "pointer-events:none;";
    }

    // Clip/scroll
    const bool has_scroll = (st.scroll_x || st.scroll_y);
    const bool has_clip = (st.clip_x || st.clip_y);
    if (has_scroll) {
        if (st.scroll_x) out += "overflow-x:auto;";
        else if (st.clip_x) out += "overflow-x:hidden;";
        if (st.scroll_y) out += "overflow-y:auto;";
        else if (st.clip_y) out += "overflow-y:hidden;";
    } else if (has_clip) {
        // 'overflow-x:visible' can't be combined with 'overflow-y:hidden' (visible is coerced).
        // Use a large clip-path inset to emulate axis-only clipping.
        if (st.clip_x && st.clip_y) {
            out += "overflow:hidden;";
        } else {
            const int inset = 99999;
            const int top = st.clip_y ? 0 : -inset;
            const int right = st.clip_x ? 0 : -inset;
            const int bottom = st.clip_y ? 0 : -inset;
            const int left = st.clip_x ? 0 : -inset;
            uint16_t tl = 0, tr = 0, bl = 0, br = 0;
            if (st.has_radius) {
                tl = st.r_tl ? st.r_tl : st.r_all;
                tr = st.r_tr ? st.r_tr : st.r_all;
                bl = st.r_bl ? st.r_bl : st.r_all;
                br = st.r_br ? st.r_br : st.r_all;
            }
            append_clip_path_inset(out, "-webkit-clip-path", top, right, bottom, left, tl, tr, br, bl);
            append_clip_path_inset(out, "clip-path", top, right, bottom, left, tl, tr, br, bl);
        }
    }

    // Text
    if (st.text_left) out += "text-align:left;";
    else if (st.text_center) out += "text-align:center;";
    else if (st.text_right) out += "text-align:right;";
    if (st.has_fs) append_px(out, "font-size", st.fs);
    if (st.has_lh) append_px(out, "line-height", st.lh);
    if (st.has_ls) append_px(out, "letter-spacing", st.ls);

    // Visual defaults for token-driven scenes: deterministic colors if not bg-none.
    const Rgb8 col = pick_color(cls);
    if (!st.bg_none) {
        append_rgba_prop(out, CssDialect::Web, "background-color", col, 0.18f);
    }
    if (st.has_border && st.border > 0) {
        out += "border:";
        out += (int)st.border;
        out += "px solid ";
        append_rgba_value(out, CssDialect::Web, col, 0.71f);
        out += ";";
    }
    if (st.has_radius) {
        const uint16_t tl = st.r_tl ? st.r_tl : st.r_all;
        const uint16_t tr = st.r_tr ? st.r_tr : st.r_all;
        const uint16_t bl = st.r_bl ? st.r_bl : st.r_all;
        const uint16_t br = st.r_br ? st.r_br : st.r_all;
        out += "border-radius:";
        out += (int)tl;
        out += "px ";
        out += (int)tr;
        out += "px ";
        out += (int)br;
        out += "px ";
        out += (int)bl;
        out += "px;";
    }
    if (st.has_opacity) {
        out += "opacity:";
        out += (double)st.opacity;
        out += ";";
    }

    return out;
}

// Native RmlUI: includes tag fallback for deterministic colors; applies root defaults for sizing.
inline webcc::string css_style_attr_for_node_rmlui(webcc::string_view cls, webcc::string_view tag, bool is_root) {
    const ClassStyle st = parse_class_style(cls);
    const bool has_grow = (st.grow_x || st.grow_y);
    const webcc::string_view key = (cls.length() > 0) ? cls : tag;
    webcc::string out;

    if (is_root) {
        out += "position:relative;width:100%;height:100%;";
    }
    if (st.dir != Direction::NoneDir) {
        out += "display:flex;flex-direction:";
        out += (st.dir == Direction::Column) ? "column;" : "row;";
    } else {
        // Match the previous RmlUI path: force block layout (important for <img> which is inline by default).
        out += "display:block;";
    }
    if (has_grow) out += "flex:1 1 0px;min-width:0;min-height:0;";
    else out += "flex-shrink:0;";

    if (st.has_fill) out += "width:100%;height:100%;";
    if (st.has_w) append_dp(out, "width", st.w);
    if (st.has_h) append_dp(out, "height", st.h);
    if (st.has_min_w) append_dp(out, "min-width", st.min_w);
    if (st.has_max_w) append_dp(out, "max-width", st.max_w);
    if (st.has_min_h) append_dp(out, "min-height", st.min_h);
    if (st.has_max_h) append_dp(out, "max-height", st.max_h);
    if (st.has_pad) append_dp(out, "padding", st.pad);
    if (st.has_gap) append_dp(out, "gap", st.gap);

    if (st.dir != Direction::NoneDir) {
        auto jc_for = [&](uint8_t v) -> const char* { return v == 1 ? "center" : (v == 2 ? "flex-end" : "flex-start"); };
        auto ai_for = [&](uint8_t v) -> const char* { return v == 1 ? "center" : (v == 2 ? "flex-end" : "flex-start"); };
        if (st.dir == Direction::Row) {
            if (st.has_align_x) {
                out += "justify-content:";
                out += jc_for(st.align_x);
                out += ";";
            }
            if (st.has_align_y) {
                out += "align-items:";
                out += ai_for(st.align_y);
                out += ";";
            }
        } else {
            if (st.has_align_x) {
                out += "align-items:";
                out += ai_for(st.align_x);
                out += ";";
            }
            if (st.has_align_y) {
                out += "justify-content:";
                out += jc_for(st.align_y);
                out += ";";
            }
        }
    }

    if (st.float_root || st.float_parent || st.has_fx || st.has_fy || st.has_z) {
        out += (st.float_root ? "position:fixed;" : "position:absolute;");
        if (st.has_fx) append_dp(out, "left", st.fx);
        if (st.has_fy) append_dp(out, "top", st.fy);
        if (st.has_z) {
            out += "z-index:";
            out += (int)st.z;
            out += ";";
        }
    }

    const bool has_scroll = (st.scroll_x || st.scroll_y);
    const bool has_clip = (st.clip_x || st.clip_y);
    if (has_scroll) {
        if (st.scroll_x) out += "overflow-x:auto;";
        else if (st.clip_x) out += "overflow-x:hidden;";
        if (st.scroll_y) out += "overflow-y:auto;";
        else if (st.clip_y) out += "overflow-y:hidden;";
    } else if (has_clip) {
        out += "overflow-x:";
        out += st.clip_x ? "hidden;" : "visible;";
        out += "overflow-y:";
        out += st.clip_y ? "hidden;" : "visible;";
    }

    if (st.text_left) out += "text-align:left;";
    else if (st.text_center) out += "text-align:center;";
    else if (st.text_right) out += "text-align:right;";
    if (st.has_fs) append_dp(out, "font-size", st.fs);
    if (st.has_lh) append_dp(out, "line-height", st.lh);
    if (st.has_ls) append_dp(out, "letter-spacing", st.ls);

    const Rgb8 col = pick_color(key);
    if (!st.bg_none && !is_root) {
        append_rgba_prop(out, CssDialect::RmlUi, "background-color", col, 0.18f);
    }
    if (st.has_border && st.border > 0) {
        append_dp(out, "border-width", (float)st.border);
        append_rgba_prop(out, CssDialect::RmlUi, "border-color", col, 0.71f);
    }
    if (st.has_radius) {
        const uint16_t tl = st.r_tl ? st.r_tl : st.r_all;
        const uint16_t tr = st.r_tr ? st.r_tr : st.r_all;
        const uint16_t bl = st.r_bl ? st.r_bl : st.r_all;
        const uint16_t br = st.r_br ? st.r_br : st.r_all;
        out += "border-radius:";
        out += (int)tl;
        out += "dp ";
        out += (int)tr;
        out += "dp ";
        out += (int)br;
        out += "dp ";
        out += (int)bl;
        out += "dp;";
    }
    if (st.has_opacity) {
        out += "opacity:";
        out += (double)st.opacity;
        out += ";";
    }

    return out;
}

} // namespace coi::style
