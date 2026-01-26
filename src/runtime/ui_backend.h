struct InputState {
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    bool mouse_down = false;
    float scroll_x = 0.0f;
    float scroll_y = 0.0f;
};

// Shared geometry helper for the tree backend's retained layout map.
// (Kept here because TreeBackend stores rectangles and this type is used
// in method bodies defined in this header.)
struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

class UiBackend {
  public:
    virtual ~UiBackend() = default;
    virtual const char* name() const = 0;

    virtual void init_gfx() {}
    virtual void shutdown_gfx() {}

    virtual void set_input(const InputState& input, float dt, float dpi) = 0;
    virtual void layout(float w, float h, float dpi) = 0;
    virtual void render(float w, float h, float dpi) = 0;
    virtual bool hit_test(float x, float y, float dpi, webcc::handle& out) = 0;
    virtual void scroll_by(float pointer_x, float pointer_y, float dx, float dy, float w, float h) = 0;

    // Backend health / optional capabilities.
    virtual bool is_ok() const { return true; }
    virtual bool font_ok() const { return true; }
    virtual void* measure_userdata() { return nullptr; }
};

class TreeBackend final : public UiBackend {
  public:
    const char* name() const override { return "tree"; }

    void set_input(const InputState&, float, float) override {}

    void layout(float w, float h, float) override {
        layout_map.clear();
        draw_list.clear();
        (void)layout_node(0, 0.0f, 0.0f, w, h);
    }

    void render(float, float, float) override {
        sgl_begin_quads();
        for (int32_t id : draw_list) {
            if (id == 0) continue;
            auto itn = coi::ui::g_nodes.find(id);
            if (itn == coi::ui::g_nodes.end()) continue;
            const auto& n = itn->second;
            if (n.tag == "comment") continue;
            auto itr = layout_map.find(id);
            if (itr == layout_map.end()) continue;
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
    }

    bool hit_test(float x, float y, float, webcc::handle& out) override {
        out = webcc::handle();
        for (auto it = draw_list.rbegin(); it != draw_list.rend(); ++it) {
            int32_t id = *it;
            auto itr = layout_map.find(id);
            if (itr == layout_map.end()) continue;
            const Rect& r = itr->second;
            if (x >= r.x && x <= (r.x + r.w) && y >= r.y && y <= (r.y + r.h)) {
                out = webcc::handle(id);
                return true;
            }
        }
        return false;
    }

    void scroll_by(float, float, float, float, float, float) override {}

  private:
    std::unordered_map<int32_t, Rect> layout_map;
    std::vector<int32_t> draw_list;

    float layout_node(int32_t id, float x, float y, float w, float max_h) {
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

        layout_map[id] = Rect{x, y, w, used_h};
        draw_list.push_back(id);
        return used_h;
    }
};

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
        ok = (ClayEngine::ctx != nullptr);
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
        struct IRect {
            int x = 0;
            int y = 0;
            int w = 0;
            int h = 0;
        };
        auto intersect = [](const IRect& a, const IRect& b) -> IRect {
            int x0 = std::max(a.x, b.x);
            int y0 = std::max(a.y, b.y);
            int x1 = std::min(a.x + a.w, b.x + b.w);
            int y1 = std::min(a.y + a.h, b.y + b.h);
            IRect out;
            out.x = x0;
            out.y = y0;
            out.w = std::max(0, x1 - x0);
            out.h = std::max(0, y1 - y0);
            return out;
        };
        auto apply_scissor = [&](const IRect& r) { sgl_scissor_rect(r.x, r.y, r.w, r.h, true /* origin_top_left */); };

        const IRect full{0, 0, iround(w), iround(h)};
        std::vector<IRect> scissor_stack;
        apply_scissor(full);

        bool quads_open = false;
        auto begin_quads = [&]() {
            if (!quads_open) {
                sgl_begin_quads();
                quads_open = true;
            }
        };
        auto flush_quads = [&]() {
            if (quads_open) {
                sgl_end();
                sgl_draw();
                quads_open = false;
            }
        };
        auto quad = [&](float x, float y, float ww, float hh) {
            float x0 = x, y0 = y, x1 = x + ww, y1 = y + hh;
            sgl_v2f(x0, y0);
            sgl_v2f(x1, y0);
            sgl_v2f(x1, y1);
            sgl_v2f(x0, y1);
        };

        begin_quads();
        for (int32_t i = 0; i < render_commands.length; i++) {
            Clay_RenderCommand* cmd = Clay_RenderCommandArray_Get(&render_commands, i);
            if (!cmd) continue;
            if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_SCISSOR_START) {
                flush_quads();
                const auto& bb = cmd->boundingBox;
                IRect r{iround(bb.x), iround(bb.y), std::max(0, iround(bb.width)), std::max(0, iround(bb.height))};
                if (!scissor_stack.empty()) r = intersect(scissor_stack.back(), r);
                scissor_stack.push_back(r);
                apply_scissor(r);
                begin_quads();
                continue;
            }
            if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_SCISSOR_END) {
                flush_quads();
                if (!scissor_stack.empty()) scissor_stack.pop_back();
                apply_scissor(scissor_stack.empty() ? full : scissor_stack.back());
                begin_quads();
                continue;
            }
            const auto& bb = cmd->boundingBox;
            if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_RECTANGLE) {
                const auto& c = cmd->renderData.rectangle.backgroundColor;
                sgl_c4f(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
                quad(bb.x, bb.y, bb.width, bb.height);
            } else if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_BORDER) {
                const auto& b = cmd->renderData.border;
                const auto& c = b.color;
                if (c.a <= 0) continue;
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
        Clay_SetCurrentContext(ClayEngine::ctx);
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
#endif // COI_NATIVE_CLAY

inline TreeBackend& tree_backend() {
    static TreeBackend b;
    return b;
}

#if defined(COI_NATIVE_CLAY)
inline ClayBackend& clay_backend() {
    static ClayBackend b;
    return b;
}
#endif
