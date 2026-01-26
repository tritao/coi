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

#endif // COI_NATIVE_CLAY
