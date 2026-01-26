#pragma once

#include "runtime/ui_tree.h"

#if defined(COI_NATIVE_CLAY)
#include "runtime/deps.h"

namespace coi::native {

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

DesktopClassStyle parse_desktop_class_style(const coi::ui::Node& n, bool is_root);

} // namespace coi::native

#endif // COI_NATIVE_CLAY

