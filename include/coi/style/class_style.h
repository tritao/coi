#pragma once

#include <cstdint>

#include "webcc/core/string_view.h"

namespace coi::style {

// NOTE: Avoid `None` because X11 defines a `None` macro.
enum class Direction : uint8_t { NoneDir = 0, Row = 1, Column = 2 };

// Parsed from COI's token-driven "class" strings (used by both web + native demo scenes).
struct ClassStyle {
    Direction dir = Direction::NoneDir;
    bool grow_x = false;
    bool grow_y = false;
    bool has_fill = false;
    bool bg_none = false;

    bool clip_x = false;
    bool clip_y = false;
    bool scroll_x = false;
    bool scroll_y = false;

    bool has_border = false;
    uint16_t border = 0;
    bool has_border_between = false;
    uint16_t border_between = 0;

    bool has_radius = false;
    uint16_t r_all = 0;
    uint16_t r_tl = 0;
    uint16_t r_tr = 0;
    uint16_t r_bl = 0;
    uint16_t r_br = 0;

    bool has_fx = false;
    bool has_fy = false;
    float fx = 0.0f;
    float fy = 0.0f;

    bool has_z = false;
    int32_t z = 0;

    bool float_root = false;
    bool float_parent = false;
    bool float_pass = false;
    bool float_clip = false;

    bool has_w = false;
    bool has_h = false;
    bool has_min_w = false;
    bool has_max_w = false;
    bool has_min_h = false;
    bool has_max_h = false;
    float w = 0.0f;
    float h = 0.0f;
    float min_w = 0.0f;
    float max_w = 0.0f;
    float min_h = 0.0f;
    float max_h = 0.0f;

    bool has_pad = false;
    bool has_gap = false;
    float pad = 0.0f;
    float gap = 0.0f;

    bool has_opacity = false;
    float opacity = 1.0f; // 0..1

    bool text_left = false;
    bool text_center = false;
    bool text_right = false;

    bool has_fs = false;
    bool has_lh = false;
    bool has_ls = false;
    float fs = 0.0f;
    float lh = 0.0f;
    float ls = 0.0f;

    bool has_align_x = false;
    bool has_align_y = false;
    uint8_t align_x = 0; // 0=start,1=center,2=end
    uint8_t align_y = 0; // 0=start,1=center,2=end
};

static inline bool sv_eq(webcc::string_view a, const char* b) {
    if (!b) return false;
    uint32_t bl = 0;
    while (b[bl]) bl++;
    if (a.length() != bl) return false;
    const char* ap = a.data();
    for (uint32_t i = 0; i < bl; i++) {
        if (ap[i] != b[i]) return false;
    }
    return true;
}

static inline bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

static inline bool sv_starts(webcc::string_view s, const char* prefix) {
    uint32_t pl = 0;
    while (prefix[pl]) pl++;
    if (s.length() < pl) return false;
    const char* p = s.data();
    for (uint32_t i = 0; i < pl; i++) {
        if (p[i] != prefix[i]) return false;
    }
    return true;
}

static inline bool parse_u16(webcc::string_view s, uint16_t& out) {
    if (s.length() == 0) return false;
    uint32_t v = 0;
    const char* p = s.data();
    for (uint32_t i = 0; i < s.length(); i++) {
        const char c = p[i];
        if (c < '0' || c > '9') return false;
        v = v * 10u + (uint32_t)(c - '0');
        if (v > 65535u) return false;
    }
    out = (uint16_t)v;
    return true;
}

static inline bool parse_f32(webcc::string_view s, float& out) {
    if (s.length() == 0) return false;
    // Accept plain integers and decimals (eg "12", "-12", "12.5", "-12.5").
    const char* p = s.data();
    const uint32_t n = s.length();
    uint32_t i = 0;
    bool neg = false;
    if (p[i] == '-') {
        neg = true;
        i++;
        if (i >= n) return false;
    }
    bool has_digit = false;
    uint32_t int_part = 0;
    while (i < n) {
        const char c = p[i];
        if (c < '0' || c > '9') break;
        has_digit = true;
        int_part = int_part * 10u + (uint32_t)(c - '0');
        i++;
    }
    float v = (float)int_part;
    if (i < n && p[i] == '.') {
        i++;
        float frac = 0.0f;
        float scale = 1.0f;
        while (i < n) {
            const char c = p[i];
            if (c < '0' || c > '9') break;
            has_digit = true;
            frac = frac * 10.0f + (float)(c - '0');
            scale *= 10.0f;
            i++;
        }
        if (scale > 1.0f) v += frac / scale;
    }
    if (!has_digit) return false;
    if (i != n) return false;
    out = neg ? -v : v;
    return true;
}

static inline float clamp_f32(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

inline ClassStyle parse_class_style(webcc::string_view cls) {
    ClassStyle st{};
    const char* p = cls.data();
    const uint32_t n = cls.length();
    uint32_t i = 0;
    while (i < n) {
        while (i < n && is_ws(p[i])) i++;
        if (i >= n) break;
        uint32_t j = i;
        while (j < n && !is_ws(p[j])) j++;
        const webcc::string_view t(p + i, j - i);

        if (sv_eq(t, "row")) {
            st.dir = Direction::Row;
        } else if (sv_eq(t, "col")) {
            st.dir = Direction::Column;
        } else if (sv_eq(t, "grow") || sv_eq(t, "grow-x")) {
            st.grow_x = true;
        } else if (sv_eq(t, "grow-y")) {
            st.grow_y = true;
        } else if (sv_eq(t, "fill")) {
            st.has_fill = true;
            st.grow_x = true;
            st.grow_y = true;
        } else if (sv_eq(t, "bg-none")) {
            st.bg_none = true;
        } else if (sv_eq(t, "clip")) {
            st.clip_x = true;
            st.clip_y = true;
        } else if (sv_eq(t, "clip-x")) {
            st.clip_x = true;
        } else if (sv_eq(t, "clip-y")) {
            st.clip_y = true;
        } else if (sv_eq(t, "scroll")) {
            st.scroll_x = true;
            st.scroll_y = true;
            st.clip_x = true;
            st.clip_y = true;
        } else if (sv_eq(t, "scroll-x")) {
            st.scroll_x = true;
            st.clip_x = true;
            st.clip_y = true;
        } else if (sv_eq(t, "scroll-y")) {
            st.scroll_y = true;
            st.clip_x = true;
            st.clip_y = true;
        } else if (sv_eq(t, "border")) {
            st.has_border = true;
            st.border = 1;
        } else if (sv_eq(t, "border-none")) {
            st.has_border = false;
            st.border = 0;
        } else if (sv_eq(t, "rounded")) {
            st.has_radius = true;
            st.r_all = 8;
        } else if (sv_eq(t, "float") || sv_eq(t, "floating")) {
            st.float_root = true;
        } else if (sv_eq(t, "float-parent")) {
            st.float_parent = true;
        } else if (sv_eq(t, "float-pass")) {
            st.float_pass = true;
        } else if (sv_eq(t, "float-clip")) {
            st.float_clip = true;
        } else if (sv_eq(t, "center")) {
            st.has_align_x = true;
            st.align_x = 1;
            st.has_align_y = true;
            st.align_y = 1;
        } else if (sv_eq(t, "x-center")) {
            st.has_align_x = true;
            st.align_x = 1;
        } else if (sv_eq(t, "x-right")) {
            st.has_align_x = true;
            st.align_x = 2;
        } else if (sv_eq(t, "y-center")) {
            st.has_align_y = true;
            st.align_y = 1;
        } else if (sv_eq(t, "y-bottom")) {
            st.has_align_y = true;
            st.align_y = 2;
        } else if (sv_eq(t, "text-left")) {
            st.text_left = true;
        } else if (sv_eq(t, "text-center")) {
            st.text_center = true;
        } else if (sv_eq(t, "text-right")) {
            st.text_right = true;
        } else {
            uint16_t v16 = 0;
            float vf = 0.0f;

            auto u16_suffix = [&](const char* prefix, uint16_t& out) -> bool {
                if (!sv_starts(t, prefix)) return false;
                uint32_t pl = 0;
                while (prefix[pl]) pl++;
                if (t.length() <= pl) return false;
                return parse_u16(webcc::string_view(t.data() + pl, t.length() - pl), out);
            };

            auto f32_suffix = [&](const char* prefix, float& out) -> bool {
                if (!sv_starts(t, prefix)) return false;
                uint32_t pl = 0;
                while (prefix[pl]) pl++;
                if (t.length() <= pl) return false;
                return parse_f32(webcc::string_view(t.data() + pl, t.length() - pl), out);
            };

            if (u16_suffix("w-", v16)) {
                st.has_w = true;
                st.w = (float)v16;
            } else if (u16_suffix("h-", v16)) {
                st.has_h = true;
                st.h = (float)v16;
            } else if (u16_suffix("min-w-", v16)) {
                st.has_min_w = true;
                st.min_w = (float)v16;
            } else if (u16_suffix("max-w-", v16)) {
                st.has_max_w = true;
                st.max_w = (float)v16;
            } else if (u16_suffix("min-h-", v16)) {
                st.has_min_h = true;
                st.min_h = (float)v16;
            } else if (u16_suffix("max-h-", v16)) {
                st.has_max_h = true;
                st.max_h = (float)v16;
            } else if (u16_suffix("pad-", v16) || u16_suffix("p-", v16)) {
                st.has_pad = true;
                st.pad = (float)v16;
            } else if (u16_suffix("gap-", v16) || u16_suffix("g-", v16)) {
                st.has_gap = true;
                st.gap = (float)v16;
            } else if (u16_suffix("border-", v16)) {
                st.has_border = true;
                st.border = v16;
            } else if (u16_suffix("border-between-", v16)) {
                st.has_border_between = true;
                st.border_between = v16;
            } else if (u16_suffix("r-tl-", v16)) {
                st.has_radius = true;
                st.r_tl = v16;
            } else if (u16_suffix("r-tr-", v16)) {
                st.has_radius = true;
                st.r_tr = v16;
            } else if (u16_suffix("r-bl-", v16)) {
                st.has_radius = true;
                st.r_bl = v16;
            } else if (u16_suffix("r-br-", v16)) {
                st.has_radius = true;
                st.r_br = v16;
            } else if (u16_suffix("r-", v16)) {
                st.has_radius = true;
                st.r_all = v16;
            } else if (u16_suffix("z-", v16)) {
                st.has_z = true;
                st.z = (int32_t)((v16 < 32767u) ? v16 : 32767u);
            } else if (u16_suffix("op-", v16)) {
                st.has_opacity = true;
                st.opacity = clamp_f32((float)v16 / 255.0f, 0.0f, 1.0f);
            } else if (u16_suffix("opacity-", v16)) {
                st.has_opacity = true;
                const float pct = (float)((v16 < 100u) ? v16 : 100u);
                st.opacity = clamp_f32(pct / 100.0f, 0.0f, 1.0f);
            } else if (u16_suffix("fs-", v16)) {
                st.has_fs = true;
                st.fs = (float)v16;
            } else if (u16_suffix("lh-", v16)) {
                st.has_lh = true;
                st.lh = (float)v16;
            } else if (u16_suffix("ls-", v16)) {
                st.has_ls = true;
                st.ls = (float)v16;
            } else if (f32_suffix("fx-", vf)) {
                st.has_fx = true;
                st.fx = vf;
            } else if (f32_suffix("fy-", vf)) {
                st.has_fy = true;
                st.fy = vf;
            }
        }

        i = j;
    }

    return st;
}

} // namespace coi::style
