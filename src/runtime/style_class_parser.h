#pragma once

#include "runtime/prelude.h"
#include "runtime/util.h"

namespace coi::native {

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

	inline DesktopClassStyle parse_desktop_class_style(const coi::ui::Node& n, bool is_root) {
	    DesktopClassStyle st{};
	    const webcc::string* cls = attr(n, "class");
	    if (!cls) return st;

	    const webcc::string_view cls_sv(cls->c_str(), cls->length());
	    const coi::style::ClassStyle cs = coi::style::parse_class_style(cls_sv);

	    if (cs.dir != coi::style::Direction::NoneDir) {
	        st.has_dir = true;
	        st.dir = (cs.dir == coi::style::Direction::Row) ? CLAY_LEFT_TO_RIGHT : CLAY_TOP_TO_BOTTOM;
	    }

	    st.w_grow = cs.grow_x;
	    st.h_grow = cs.grow_y;

	    if (cs.has_opacity) {
	        st.has_opacity = true;
	        st.opacity = cs.opacity;
	    }

	    if (cs.float_root || cs.float_parent || cs.float_pass || cs.float_clip) {
	        st.has_floating = true;
	        if (cs.float_root) st.floating_attach_to = CLAY_ATTACH_TO_ROOT;
	        if (cs.float_parent) st.floating_attach_to = CLAY_ATTACH_TO_PARENT;
	        if (cs.float_pass) st.floating_pointer_mode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH;
	        if (cs.float_clip) st.floating_clip_to = CLAY_CLIP_TO_ATTACHED_PARENT;
	    }

	    if (cs.has_fx) {
	        st.has_float_x = true;
	        st.float_x = cs.fx;
	    }
	    if (cs.has_fy) {
	        st.has_float_y = true;
	        st.float_y = cs.fy;
	    }
	    if (cs.has_z) {
	        st.has_z_index = true;
	        st.z_index = (int16_t)std::clamp<int32_t>(cs.z, 0, 32767);
	    }

	    st.bg_none = cs.bg_none;

	    if (cs.clip_x || cs.clip_y || cs.scroll_x || cs.scroll_y) {
	        st.has_clip = true;
	        st.clip_x = cs.clip_x;
	        st.clip_y = cs.clip_y;
	        st.scroll_x = cs.scroll_x;
	        st.scroll_y = cs.scroll_y;
	    }

	    if (cs.has_border || cs.has_border_between) {
	        st.has_border = true;
	        const uint16_t bw = cs.has_border ? cs.border : 0;
	        st.border_width.left = bw;
	        st.border_width.right = bw;
	        st.border_width.top = bw;
	        st.border_width.bottom = bw;
	        if (cs.has_border_between) st.border_width.betweenChildren = cs.border_between;
	    }

	    if (cs.has_radius) {
	        st.has_corner_radius = true;
	        const float all = (float)cs.r_all;
	        st.corner_radius = CLAY_CORNER_RADIUS(all);
	        if (cs.r_tl) st.corner_radius.topLeft = (float)cs.r_tl;
	        if (cs.r_tr) st.corner_radius.topRight = (float)cs.r_tr;
	        if (cs.r_bl) st.corner_radius.bottomLeft = (float)cs.r_bl;
	        if (cs.r_br) st.corner_radius.bottomRight = (float)cs.r_br;
	    }

	    if (cs.has_w) {
	        st.w_fixed = true;
	        st.w = cs.w;
	    }
	    if (cs.has_h) {
	        st.h_fixed = true;
	        st.h = cs.h;
	    }
	    if (cs.has_min_w) {
	        st.w_has_min = true;
	        st.w_min = cs.min_w;
	    }
	    if (cs.has_max_w) {
	        st.w_has_max = true;
	        st.w_max = cs.max_w;
	    }
	    if (cs.has_min_h) {
	        st.h_has_min = true;
	        st.h_min = cs.min_h;
	    }
	    if (cs.has_max_h) {
	        st.h_has_max = true;
	        st.h_max = cs.max_h;
	    }

	    if (cs.has_pad) {
	        st.has_pad = true;
	        st.pad = (uint16_t)std::clamp<int>( (int)std::lround(cs.pad), 0, 65535);
	    }
	    if (cs.has_gap) {
	        st.has_gap = true;
	        st.gap = (uint16_t)std::clamp<int>( (int)std::lround(cs.gap), 0, 65535);
	    }

	    if (cs.text_left) {
	        st.has_text_align = true;
	        st.text_align = CLAY_TEXT_ALIGN_LEFT;
	    } else if (cs.text_center) {
	        st.has_text_align = true;
	        st.text_align = CLAY_TEXT_ALIGN_CENTER;
	    } else if (cs.text_right) {
	        st.has_text_align = true;
	        st.text_align = CLAY_TEXT_ALIGN_RIGHT;
	    }

	    if (cs.has_fs) {
	        st.has_font_size = true;
	        st.font_size = (int32_t)std::lround(cs.fs);
	    }
	    if (cs.has_ls) {
	        st.has_letter_spacing = true;
	        st.letter_spacing = (int32_t)std::lround(cs.ls);
	    }
	    if (cs.has_lh) {
	        st.has_line_height = true;
	        st.line_height = (int32_t)std::lround(cs.lh);
	    }

	    if (cs.has_align_x) {
	        st.has_align_x = true;
	        st.align_x = (cs.align_x == 1) ? CLAY_ALIGN_X_CENTER : (cs.align_x == 2 ? CLAY_ALIGN_X_RIGHT : CLAY_ALIGN_X_LEFT);
	    }
	    if (cs.has_align_y) {
	        st.has_align_y = true;
	        st.align_y = (cs.align_y == 1) ? CLAY_ALIGN_Y_CENTER : (cs.align_y == 2 ? CLAY_ALIGN_Y_BOTTOM : CLAY_ALIGN_Y_TOP);
	    }

	    if (is_root) {
	        // If root asked for grow, treat it as fill.
	        if (st.w_grow) st.h_grow = true;
	    }
	    return st;
	}

#endif // COI_NATIVE_CLAY

} // namespace coi::native
