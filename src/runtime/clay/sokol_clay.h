#ifndef COI_SOKOL_CLAY_INCLUDED
#define COI_SOKOL_CLAY_INCLUDED (1)
/*
    C++-safe Clay renderer for sokol_gl.h (vendored and adapted from Clay's
    `renderers/sokol/sokol_clay.h`).

    Key differences from upstream:
    - Compiles as C++ (no C compound literals, no designated initializers).
    - Uses `fonsAddFontMem` only (Sokol's bundled fontstash doesn't provide
      `fonsAddFont`).

    Usage:
      #define SOKOL_CLAY_IMPL
      before including this file in exactly one translation unit.

    Required includes before including this header:
      - sokol_gl.h
      - sokol_fontstash.h
      - sokol_app.h (unless SOKOL_CLAY_NO_SOKOL_APP)
      - clay.h
*/

#if !defined(SOKOL_CLAY_NO_SOKOL_APP) && !defined(SOKOL_APP_INCLUDED)
#error "Please include sokol_app.h before sokol_clay.h (or define SOKOL_CLAY_NO_SOKOL_APP)"
#endif

typedef int sclay_font_t;

typedef struct sclay_image {
    sg_view view;
    sg_sampler sampler;
    struct {
        float u0, v0, u1, v1;
    } uv;
} sclay_image;

void sclay_setup();
void sclay_shutdown();

// Loads a font from disk (reads file into memory and calls fonsAddFontMem with freeData=1).
sclay_font_t sclay_add_font(const char* filename);
// Adds a font from memory (caller owns `data`).
sclay_font_t sclay_add_font_mem(unsigned char* data, int dataLen);
Clay_Dimensions sclay_measure_text(Clay_StringSlice text, Clay_TextElementConfig* config, void* userData);

#ifndef SOKOL_CLAY_NO_SOKOL_APP
void sclay_new_frame();
void sclay_handle_event(const sapp_event* ev);
#endif /* SOKOL_CLAY_NO_SOKOL_APP */

// Use this if you don't call sclay_new_frame. `size` is the "virtual" size which your layout is relative to
// (ie. the actual framebuffer size divided by dpi_scale.) Set dpi_scale to 1 if you're not using high-dpi support.
void sclay_set_layout_dimensions(Clay_Dimensions size, float dpi_scale);

void sclay_render(Clay_RenderCommandArray renderCommands, sclay_font_t* fonts);

#endif /* COI_SOKOL_CLAY_INCLUDED */

#ifdef SOKOL_CLAY_IMPL

#ifndef SOKOL_GL_INCLUDED
#error "Please include sokol_gl.h before sokol_clay.h"
#endif
#ifndef SOKOL_FONTSTASH_INCLUDED
#error "Please include sokol_fontstash.h before sokol_clay.h"
#endif
#ifndef CLAY_HEADER
#error "Please include clay.h before sokol_clay.h"
#endif

#include <cstdio>
#include <cstdlib>

typedef struct {
    sgl_pipeline pip;
#ifndef SOKOL_CLAY_NO_SOKOL_APP
    Clay_Vector2 mouse_pos, scroll;
    bool mouse_down;
#endif
    Clay_Dimensions size;
    float dpi_scale;
    FONScontext* fonts;
} _sclay_state_t;
static _sclay_state_t _sclay;

void sclay_setup() {
    sg_pipeline_desc pip_desc{};
    pip_desc.colors[0].blend.enabled = true;
    pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    _sclay.pip = sgl_make_pipeline(&pip_desc);
#ifndef SOKOL_CLAY_NO_SOKOL_APP
    _sclay.mouse_pos = Clay_Vector2{0, 0};
    _sclay.scroll = Clay_Vector2{0, 0};
    _sclay.mouse_down = false;
#endif
    _sclay.size = Clay_Dimensions{1, 1};
    _sclay.dpi_scale = 1.0f;
    sfons_desc_t fs_desc{};
    fs_desc.width = 512;
    fs_desc.height = 512;
    _sclay.fonts = sfons_create(&fs_desc);
}

void sclay_shutdown() {
    sgl_destroy_pipeline(_sclay.pip);
    sfons_destroy(_sclay.fonts);
}

#ifndef SOKOL_CLAY_NO_SOKOL_APP
void sclay_handle_event(const sapp_event* ev) {
    switch (ev->type) {
    case SAPP_EVENTTYPE_MOUSE_MOVE:
        _sclay.mouse_pos.x = ev->mouse_x / _sclay.dpi_scale;
        _sclay.mouse_pos.y = ev->mouse_y / _sclay.dpi_scale;
        break;
    case SAPP_EVENTTYPE_MOUSE_DOWN:
        _sclay.mouse_down = true;
        break;
    case SAPP_EVENTTYPE_MOUSE_UP:
        _sclay.mouse_down = false;
        break;
    case SAPP_EVENTTYPE_MOUSE_SCROLL:
        _sclay.scroll.x += ev->scroll_x;
        _sclay.scroll.y += ev->scroll_y;
        break;
    default:
        break;
    }
}

void sclay_new_frame() {
    Clay_Dimensions dims{(float)sapp_width(), (float)sapp_height()};
    sclay_set_layout_dimensions(dims, sapp_dpi_scale());
    Clay_SetPointerState(_sclay.mouse_pos, _sclay.mouse_down);
    Clay_UpdateScrollContainers(true, _sclay.scroll, sapp_frame_duration());
    _sclay.scroll = Clay_Vector2{0, 0};
}
#endif /* SOKOL_CLAY_NO_SOKOL_APP */

void sclay_set_layout_dimensions(Clay_Dimensions size, float dpi_scale) {
    size.width /= dpi_scale;
    size.height /= dpi_scale;
    _sclay.size = size;
    if (_sclay.dpi_scale != dpi_scale) {
        _sclay.dpi_scale = dpi_scale;
        Clay_ResetMeasureTextCache();
    }
    Clay_SetLayoutDimensions(size);
}

sclay_font_t sclay_add_font(const char* filename) {
    if (!filename || !*filename) return FONS_INVALID;
    FILE* f = std::fopen(filename, "rb");
    if (!f) return FONS_INVALID;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(f);
        return FONS_INVALID;
    }
    unsigned char* data = (unsigned char*)std::malloc((size_t)size);
    if (!data) {
        std::fclose(f);
        return FONS_INVALID;
    }
    size_t n = std::fread(data, 1, (size_t)size, f);
    std::fclose(f);
    if (n != (size_t)size) {
        std::free(data);
        return FONS_INVALID;
    }
    // Let fontstash free the buffer on shutdown.
    return fonsAddFontMem(_sclay.fonts, "", data, (int)size, 1);
}

sclay_font_t sclay_add_font_mem(unsigned char* data, int dataLen) {
    if (!data || dataLen <= 0) return FONS_INVALID;
    return fonsAddFontMem(_sclay.fonts, "", data, dataLen, 0);
}

Clay_Dimensions sclay_measure_text(Clay_StringSlice text, Clay_TextElementConfig* config, void* userData) {
    sclay_font_t* fonts = (sclay_font_t*)userData;
    if (!fonts) return Clay_Dimensions{0, 0};
    fonsSetFont(_sclay.fonts, fonts[config->fontId]);
    fonsSetSize(_sclay.fonts, config->fontSize * _sclay.dpi_scale);
    fonsSetSpacing(_sclay.fonts, config->letterSpacing * _sclay.dpi_scale);
    fonsSetAlign(_sclay.fonts, FONS_ALIGN_LEFT | FONS_ALIGN_TOP);
    float ascent = 0.0f, descent = 0.0f, lineh = 0.0f;
    fonsVertMetrics(_sclay.fonts, &ascent, &descent, &lineh);
    Clay_Dimensions out{};
    out.width = fonsTextBounds(_sclay.fonts, 0, 0, text.chars, text.chars + text.length, nullptr) / _sclay.dpi_scale;
    out.height = (ascent - descent) / _sclay.dpi_scale;
    return out;
}

static void _draw_rect(float x, float y, float w, float h) {
    sgl_v2f(x, y);
    sgl_v2f(x, y);
    sgl_v2f(x + w, y);
    sgl_v2f(x, y + h);
    sgl_v2f(x + w, y + h);
    sgl_v2f(x + w, y + h);
}

static void _draw_rect_textured(float x, float y, float w, float h, float u0, float v0, float u1, float v1) {
    sgl_v2f_t2f(x, y, u0, v0);
    sgl_v2f_t2f(x, y, u0, v0);
    sgl_v2f_t2f(x + w, y, u1, v0);
    sgl_v2f_t2f(x, y + h, u0, v1);
    sgl_v2f_t2f(x + w, y + h, u1, v1);
    sgl_v2f_t2f(x + w, y + h, u1, v1);
}

static float _SIN[16] = {
    0.000000f, 0.104528f, 0.207912f, 0.309017f,
    0.406737f, 0.500000f, 0.587785f, 0.669131f,
    0.743145f, 0.809017f, 0.866025f, 0.913545f,
    0.951057f, 0.978148f, 0.994522f, 1.000000f,
};

/* rx,ry = radius */
static void _draw_corner(float x, float y, float rx, float ry) {
    x -= rx;
    y -= ry;
    sgl_v2f(x, y);
    for (int i = 0; i < 16; ++i) {
        sgl_v2f(x, y);
        sgl_v2f(x + (rx * _SIN[15 - i]), y + (ry * _SIN[i]));
    }
    sgl_v2f(x + (rx * _SIN[0]), y + (ry * _SIN[15]));
}

static void _draw_corner_textured(float x, float y, float rx, float ry, float bx, float by, float bw, float bh, float u0, float v0, float u1, float v1) {
    x -= rx;
    y -= ry;
#define MAP_U(x) (u0 + (((x) - bx) / bw) * (u1 - u0))
#define MAP_V(y) (v0 + (((y) - by) / bh) * (v1 - v0))
    sgl_v2f_t2f(x, y, MAP_U(x), MAP_V(y));
    for (int i = 0; i < 16; ++i) {
        sgl_v2f_t2f(x, y, MAP_U(x), MAP_V(y));
        float px = x + (rx * _SIN[15 - i]);
        float py = y + (ry * _SIN[i]);
        sgl_v2f_t2f(px, py, MAP_U(px), MAP_V(py));
    }
    sgl_v2f_t2f(x + (rx * _SIN[0]), y + (ry * _SIN[15]), MAP_U(x + (rx * _SIN[0])), MAP_V(y + (ry * _SIN[15])));
#undef MAP_U
#undef MAP_V
}

/* rx,ry = radius   ix,iy = inner radius */
static void _draw_corner_border(float x, float y, float rx, float ry, float ix, float iy) {
    x -= rx;
    y -= ry;
    sgl_v2f(x + (ix * _SIN[15]), y + (iy * _SIN[0]));
    for (int i = 0; i < 16; ++i) {
        sgl_v2f(x + (ix * _SIN[15 - i]), y + (iy * _SIN[i]));
        sgl_v2f(x + (rx * _SIN[15 - i]), y + (ry * _SIN[i]));
    }
    sgl_v2f(x + (rx * _SIN[0]), y + (ry * _SIN[15]));
}

void sclay_render(Clay_RenderCommandArray renderCommands, sclay_font_t* fonts) {
    sgl_matrix_mode_modelview();
    sgl_translate(-1.0f, 1.0f, 0.0f);
    sgl_scale(2.0f / _sclay.size.width, -2.0f / _sclay.size.height, 1.0f);
    sgl_disable_texture();
    sgl_push_pipeline();
    sgl_load_pipeline(_sclay.pip);
    for (uint32_t i = 0; i < renderCommands.length; i++) {
        Clay_RenderCommand* renderCommand = Clay_RenderCommandArray_Get(&renderCommands, i);
        Clay_BoundingBox bbox = renderCommand->boundingBox;
        switch (renderCommand->commandType) {
        case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
            Clay_RectangleRenderData* config = &renderCommand->renderData.rectangle;
            sgl_c4f(config->backgroundColor.r / 255.0f,
                    config->backgroundColor.g / 255.0f,
                    config->backgroundColor.b / 255.0f,
                    config->backgroundColor.a / 255.0f);
            Clay_CornerRadius r = config->cornerRadius;
            sgl_begin_triangle_strip();
            if (r.topLeft > 0 || r.topRight > 0) {
                _draw_corner(bbox.x, bbox.y, -r.topLeft, -r.topLeft);
                _draw_corner(bbox.x + bbox.width, bbox.y, r.topRight, -r.topRight);
                _draw_rect(bbox.x + r.topLeft, bbox.y,
                           bbox.width - r.topLeft - r.topRight, CLAY__MAX(r.topLeft, r.topRight));
            }
            if (r.bottomLeft > 0 || r.bottomRight > 0) {
                _draw_corner(bbox.x, bbox.y + bbox.height, -r.bottomLeft, r.bottomLeft);
                _draw_corner(bbox.x + bbox.width, bbox.y + bbox.height, r.bottomRight, r.bottomRight);
                _draw_rect(bbox.x + r.bottomLeft,
                           bbox.y + bbox.height - CLAY__MAX(r.bottomLeft, r.bottomRight),
                           bbox.width - r.bottomLeft - r.bottomRight, CLAY__MAX(r.bottomLeft, r.bottomRight));
            }
            if (r.topLeft < r.bottomLeft) {
                if (r.topLeft < r.topRight) {
                    _draw_rect(bbox.x, bbox.y + r.topLeft, r.topLeft, bbox.height - r.topLeft - r.bottomLeft);
                    _draw_rect(bbox.x + r.topLeft, bbox.y + r.topRight,
                               r.bottomLeft - r.topLeft, bbox.height - r.topRight - r.bottomLeft);
                } else {
                    _draw_rect(bbox.x, bbox.y + r.topLeft, r.bottomLeft, bbox.height - r.topLeft - r.bottomLeft);
                }
            } else {
                if (r.bottomLeft < r.bottomRight) {
                    _draw_rect(bbox.x, bbox.y + r.topLeft, r.bottomLeft, bbox.height - r.topLeft - r.bottomLeft);
                    _draw_rect(bbox.x + r.bottomLeft, bbox.y + r.topLeft,
                               r.topLeft - r.bottomLeft, bbox.height - r.topLeft - r.bottomRight);
                } else {
                    _draw_rect(bbox.x, bbox.y + r.topLeft, r.topLeft, bbox.height - r.topLeft - r.bottomLeft);
                }
            }
            if (r.topRight < r.bottomRight) {
                if (r.topRight < r.topLeft) {
                    _draw_rect(bbox.x + bbox.width - r.bottomRight, bbox.y + r.topLeft,
                               r.bottomRight - r.topRight, bbox.height - r.topLeft - r.bottomRight);
                    _draw_rect(bbox.x + bbox.width - r.topRight, bbox.y + r.topRight,
                               r.topRight, bbox.height - r.topRight - r.bottomRight);
                } else {
                    _draw_rect(bbox.x + bbox.width - r.bottomRight, bbox.y + r.topRight,
                               r.bottomRight, bbox.height - r.topRight - r.bottomRight);
                }
            } else {
                if (r.bottomRight < r.bottomLeft) {
                    _draw_rect(bbox.x + bbox.width - r.topRight, bbox.y + r.topRight,
                               r.topRight - r.bottomRight, bbox.height - r.topRight - r.bottomLeft);
                    _draw_rect(bbox.x + bbox.width - r.bottomRight, bbox.y + r.topRight,
                               r.bottomRight, bbox.height - r.topRight - r.bottomRight);
                } else {
                    _draw_rect(bbox.x + bbox.width - r.topRight, bbox.y + r.topRight,
                               r.topRight, bbox.height - r.topRight - r.bottomRight);
                }
            }
            _draw_rect(bbox.x + CLAY__MAX(r.topLeft, r.bottomLeft),
                       bbox.y + CLAY__MAX(r.topLeft, r.topRight),
                       bbox.width - CLAY__MAX(r.topLeft, r.bottomLeft) - CLAY__MAX(r.topRight, r.bottomRight),
                       bbox.height - CLAY__MAX(r.topLeft, r.topRight) - CLAY__MAX(r.bottomLeft, r.bottomRight));
            sgl_end();
            break;
        }
        case CLAY_RENDER_COMMAND_TYPE_TEXT: {
            if (!fonts) break;
            Clay_TextRenderData* config = &renderCommand->renderData.text;
            Clay_StringSlice text = config->stringContents;
            fonsSetFont(_sclay.fonts, fonts[config->fontId]);
            uint32_t color = sfons_rgba(
                config->textColor.r,
                config->textColor.g,
                config->textColor.b,
                config->textColor.a);
            fonsSetColor(_sclay.fonts, color);
            fonsSetSpacing(_sclay.fonts, config->letterSpacing * _sclay.dpi_scale);
            fonsSetAlign(_sclay.fonts, FONS_ALIGN_LEFT | FONS_ALIGN_TOP);
            fonsSetSize(_sclay.fonts, config->fontSize * _sclay.dpi_scale);
            sgl_matrix_mode_modelview();
            sgl_push_matrix();
            sgl_scale(1.0f / _sclay.dpi_scale, 1.0f / _sclay.dpi_scale, 1.0f);
            fonsDrawText(_sclay.fonts, bbox.x * _sclay.dpi_scale, bbox.y * _sclay.dpi_scale,
                         text.chars, text.chars + text.length);
            sgl_pop_matrix();
            break;
        }
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
            Clay_BoundingBox bb = renderCommand->boundingBox;
            const int sx = (int)(bb.x * _sclay.dpi_scale);
            const int sy = (int)(bb.y * _sclay.dpi_scale);
            const int sw = (int)(bb.width * _sclay.dpi_scale);
            const int sh = (int)(bb.height * _sclay.dpi_scale);
            sgl_scissor_rect(sx, sy, sw, sh, true);
            break;
        }
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
            sgl_scissor_rect(0, 0, (int)(_sclay.size.width * _sclay.dpi_scale), (int)(_sclay.size.height * _sclay.dpi_scale), true);
            break;
        case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
            Clay_ImageRenderData* config = &renderCommand->renderData.image;
            const sclay_image* img = (const sclay_image*)config->imageData;
            if (!img) break;
            sgl_enable_texture();
            // sampler.id == 0 uses sokol-gl's default sampler.
            sgl_texture(img->view, img->sampler);
            sgl_begin_triangle_strip();
            _draw_rect_textured(bbox.x, bbox.y, bbox.width, bbox.height, img->uv.u0, img->uv.v0, img->uv.u1, img->uv.v1);
            sgl_end();
            sgl_disable_texture();
            break;
        }
        case CLAY_RENDER_COMMAND_TYPE_BORDER: {
            Clay_BorderRenderData* config = &renderCommand->renderData.border;
            Clay_CornerRadius r = config->cornerRadius;
            const float lw = (float)config->width.top;
            sgl_c4f(config->color.r / 255.0f,
                    config->color.g / 255.0f,
                    config->color.b / 255.0f,
                    config->color.a / 255.0f);
            sgl_begin_triangle_strip();
            if (r.topLeft > 0 || r.topRight > 0) {
                _draw_corner_border(bbox.x + lw, bbox.y + lw, -r.topLeft, -r.topLeft, -r.topLeft + lw, -r.topLeft + lw);
                _draw_corner_border(bbox.x + bbox.width - lw, bbox.y + lw, r.topRight, -r.topRight, r.topRight - lw, -r.topRight + lw);
                _draw_rect(bbox.x + r.topLeft, bbox.y, bbox.width - r.topLeft - r.topRight, lw);
            } else {
                _draw_rect(bbox.x, bbox.y, bbox.width, lw);
            }
            if (r.bottomLeft > 0 || r.bottomRight > 0) {
                _draw_corner_border(bbox.x + lw, bbox.y + bbox.height - lw, -r.bottomLeft, r.bottomLeft, -r.bottomLeft + lw, r.bottomLeft - lw);
                _draw_corner_border(bbox.x + bbox.width - lw, bbox.y + bbox.height - lw, r.bottomRight, r.bottomRight, r.bottomRight - lw, r.bottomRight - lw);
                _draw_rect(bbox.x + r.bottomLeft, bbox.y + bbox.height - lw, bbox.width - r.bottomLeft - r.bottomRight, lw);
            } else {
                _draw_rect(bbox.x, bbox.y + bbox.height - lw, bbox.width, lw);
            }
            _draw_rect(bbox.x, bbox.y + r.topLeft, lw, bbox.height - r.topLeft - r.bottomLeft);
            _draw_rect(bbox.x + bbox.width - lw, bbox.y + r.topRight, lw, bbox.height - r.topRight - r.bottomRight);
            sgl_end();
            break;
        }
        default:
            break;
        }
    }
    sgl_pop_pipeline();
    // Ensure any queued fontstash draws are flushed for this frame.
    sfons_flush(_sclay.fonts);
}

#endif /* SOKOL_CLAY_IMPL */
