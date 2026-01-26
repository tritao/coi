// Clay backend implementation.

#include "runtime/prelude.h"

#include "runtime/backends/clay_backend.h"

#include "runtime/debug_text.h"
#include "runtime/clay/engine.h"
#include "runtime/util.h"

namespace coi::native {

#if defined(COI_NATIVE_CLAY)
class ClayBackend final : public UiBackend {
  public:
    const char* name() const override { return "clay"; }

    void init_gfx() override {
#if defined(COI_NATIVE_FONTSTASH)
#if defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
        sclay_setup();
#else
        sfons_desc_t fs_desc{};
        fs_desc.width = 512;
        fs_desc.height = 512;
        fons_ctx = sfons_create(&fs_desc);
        if (!fons_ctx) {
            std::cerr << "[font] sfons_create failed\n";
        }
#endif

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
#if defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
            clay_fonts[0] = sclay_add_font_mem(font_bytes.data(), (int)font_bytes.size());
            if (clay_fonts[0] == FONS_INVALID) {
                std::cerr << "[font] failed to load ttf from " << font_path_s << "\n";
            }
#else
            if (fons_ctx) {
                fons_font = fonsAddFontMem(fons_ctx, "coi-default", font_bytes.data(), (int)font_bytes.size(), 0);
                if (fons_font == FONS_INVALID) {
                    std::cerr << "[font] failed to load ttf from " << font_path_s << "\n";
                }
            }
#endif
        } else {
            const char* shown = (font_path_env && *font_path_env) ? font_path_env
                                                                  : "deps/clay/examples/sokol-video-demo/resources/Roboto-Regular.ttf";
            std::cerr << "[font] failed to read ttf file: " << shown << "\n";
        }
#endif // COI_NATIVE_FONTSTASH
    }

    void shutdown_gfx() override {
#if defined(COI_NATIVE_FONTSTASH)
#if defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
        sclay_shutdown();
        clay_fonts[0] = FONS_INVALID;
#else
        if (fons_ctx) {
            sfons_destroy(fons_ctx);
            fons_ctx = nullptr;
        }
        fons_font = FONS_INVALID;
#endif
        font_bytes.clear();
#endif

#if defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
        DesktopImageCache::shutdown();
#endif

        ClayEngine::shutdown();
        ok = false;
        render_commands = Clay_RenderCommandArray{};
    }

    void set_input(const InputState& input, float dt, float dpi) override {
        float mx = input.mouse_x;
        float my = input.mouse_y;
#if defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
        if (dpi > 0.0f) {
            mx /= dpi;
            my /= dpi;
        }
#endif
        ClayEngine::set_input(mx, my, input.mouse_down, input.scroll_x, input.scroll_y, dt);
    }

    void layout(float w, float h, float dpi) override {
        render_commands = ClayEngine::layout(w, h, dpi);
        ok = (ClayEngine::ctx() != nullptr);
    }

    void render(float w, float h, float) override {
        if (!ok) return;
#if defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
        // Ensure a full-target scissor before rendering; Clay emits scissor only for clip elements.
        sgl_scissor_rect(0, 0, (int)w, (int)h, true /* origin_top_left */);
        // Clay's reference sokol renderer expects identity matrices.
        sgl_matrix_mode_projection();
        sgl_load_identity();
        sgl_matrix_mode_modelview();
        sgl_load_identity();
        sclay_render(render_commands, clay_fonts);
        sgl_draw();
#else
        auto iround = [](float v) -> int { return (int)std::lround((double)v); };
        auto quad = [&](float x, float y, float w, float h) {
            float x0 = x;
            float y0 = y;
            float x1 = x + w;
            float y1 = y + h;
            sgl_v2f(x0, y0);
            sgl_v2f(x1, y0);
            sgl_v2f(x1, y1);
            sgl_v2f(x0, y1);
        };

        sgl_scissor_rect(0, 0, (int)w, (int)h, true /* origin_top_left */);
        sgl_begin_quads();

        auto flush_quads = [&]() {
            sgl_end();
            sgl_draw();
            sgl_begin_quads();
        };

        for (int32_t i = 0; i < render_commands.length; i++) {
            Clay_RenderCommand* cmd = Clay_RenderCommandArray_Get(&render_commands, i);
            if (!cmd) continue;
            const auto& bb = cmd->boundingBox;
            const auto clip = cmd->clip;
            const bool clipped = (cmd->clip.width > 0.0f && cmd->clip.height > 0.0f);
            if (clipped) {
                flush_quads();
                sgl_scissor_rect(iround(clip.x), iround(clip.y), iround(clip.width), iround(clip.height), true /* origin_top_left */);
            }

            if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_RECTANGLE) {
                const auto& r = cmd->renderData.rectangle;
                const Clay_Color c = r.backgroundColor;
                sgl_c4f(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
                quad(bb.x, bb.y, bb.width, bb.height);
            } else if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_BORDER) {
                const auto& b = cmd->renderData.border;
                const Clay_Color c = b.color;
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
            // sgl_draw() rewinds the recorded command stream. Re-emit viewport/matrices before fontstash draw calls.
            sgl_defaults();
            sgl_viewport(0, 0, (int)w, (int)h, true);
            sgl_matrix_mode_projection();
            sgl_load_identity();
            sgl_ortho(0.0f, w, h, 0.0f, -1.0f, 1.0f);
            sgl_matrix_mode_modelview();
            sgl_load_identity();

            sgl_scissor_rect(0, 0, (int)w, (int)h, true /* origin_top_left */);
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
            sfons_flush(fons_ctx);
            sgl_draw();
        }
#endif
#endif // COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED
    }

    bool hit_test(float x, float y, float dpi, webcc::handle& out) override {
        out = webcc::handle();
        if (!ok) return false;
        Clay_SetCurrentContext(ClayEngine::ctx());
        float cx = x;
        float cy = y;
#if defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
        if (dpi > 0.0f) {
            cx /= dpi;
            cy /= dpi;
        }
#endif
        Clay_SetPointerState(Clay_Vector2{cx, cy}, false);
        Clay_ElementIdArray ids = Clay_GetPointerOverIds();
        for (int32_t i = ids.length - 1; i >= 0; --i) {
            Clay_ElementId* eid = (ids.internalArray && i >= 0) ? (ids.internalArray + i) : nullptr;
            if (!eid) continue;
            int32_t hid = (int32_t)(eid->id ^ 0xC01D0000u);
            if (coi::ui::g_nodes.find(hid) == coi::ui::g_nodes.end()) continue;
            out = webcc::handle(hid);
            return true;
        }
        return false;
    }

    void scroll_by(float pointer_x, float pointer_y, float dx, float dy, float w, float h) override {
        // Prime scroll container mappings and pointer-over state.
        ClayEngine::set_input(pointer_x, pointer_y, false, 0.0f, 0.0f, 1.0f / 60.0f);
        (void)ClayEngine::layout(w, h, 1.0f);
        // Apply scroll. Deltas follow browser-like semantics.
        ClayEngine::set_input(pointer_x, pointer_y, false, -dx, -dy, 1.0f / 60.0f);
        (void)ClayEngine::layout(w, h, 1.0f);
    }

    bool is_ok() const override { return ok; }

    bool font_ok() const override {
#if defined(COI_NATIVE_FONTSTASH) && defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
        return clay_fonts[0] != FONS_INVALID;
#elif defined(COI_NATIVE_FONTSTASH)
        return fons_ctx && fons_font != FONS_INVALID;
#else
        return false;
#endif
    }

    void* measure_userdata() override {
#if defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
        return (void*)clay_fonts;
#else
        return nullptr;
#endif
    }

  private:
    Clay_RenderCommandArray render_commands{};
    bool ok = false;

#if defined(COI_NATIVE_FONTSTASH)
#if defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
    sclay_font_t clay_fonts[1] = {FONS_INVALID};
    std::vector<unsigned char> font_bytes;
#else
    FONScontext* fons_ctx = nullptr;
    int fons_font = FONS_INVALID;
    std::vector<unsigned char> font_bytes;
#endif
#endif
};

UiBackend& clay_backend() {
    static ClayBackend b;
    return b;
}
#endif // COI_NATIVE_CLAY

} // namespace coi::native
