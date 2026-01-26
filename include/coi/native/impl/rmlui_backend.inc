#if defined(COI_NATIVE_RMLUI)

namespace detail {

class SokolRmlRenderInterface final : public Rml::RenderInterface {
  public:
    void init() {
        if (premult_pip.id != 0) return;

        sg_pipeline_desc pip{};
        pip.colors[0].blend.enabled = true;
        pip.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_ONE;
        pip.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        pip.colors[0].blend.op_rgb = SG_BLENDOP_ADD;
        pip.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
        pip.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        pip.colors[0].blend.op_alpha = SG_BLENDOP_ADD;
        premult_pip = sgl_make_pipeline(&pip);

        sg_sampler_desc smp{};
        smp.min_filter = SG_FILTER_LINEAR;
        smp.mag_filter = SG_FILTER_LINEAR;
        smp.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
        smp.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
        default_sampler = sg_make_sampler(&smp);
    }

    void shutdown() {
        for (auto& kv : textures) {
            destroy_texture(kv.second);
        }
        textures.clear();
        next_texture = 1;

        geometries.clear();
        next_geometry = 1;

        if (default_sampler.id != SG_INVALID_ID) {
            sg_destroy_sampler(default_sampler);
            default_sampler.id = SG_INVALID_ID;
        }
        if (premult_pip.id != 0) {
            sgl_destroy_pipeline(premult_pip);
            premult_pip.id = 0;
        }
    }

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override {
        const uintptr_t id = next_geometry++;
        Geometry g;
        g.vertices.assign(vertices.begin(), vertices.end());
        g.indices.assign(indices.begin(), indices.end());
        geometries.emplace(id, std::move(g));
        return (Rml::CompiledGeometryHandle)id;
    }

    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override {
        auto it = geometries.find((uintptr_t)geometry);
        if (it == geometries.end()) return;

        sgl_push_pipeline();
        if (premult_pip.id != 0) {
            sgl_load_pipeline(premult_pip);
        }

        if (texture != 0) {
            auto itt = textures.find((uintptr_t)texture);
            if (itt != textures.end() && itt->second.view.id != SG_INVALID_ID) {
                sgl_enable_texture();
                sgl_texture(itt->second.view, itt->second.sampler.id != SG_INVALID_ID ? itt->second.sampler : default_sampler);
            } else {
                sgl_disable_texture();
            }
        } else {
            sgl_disable_texture();
        }

        // Apply active transform if present.
        sgl_matrix_mode_modelview();
        sgl_load_identity();
        if (transform_active) {
            sgl_load_matrix(transform_m);
        }

        sgl_begin_triangles();
        const Geometry& g = it->second;
        for (int idx : g.indices) {
            if (idx < 0 || (size_t)idx >= g.vertices.size()) continue;
            const Rml::Vertex& v = g.vertices[(size_t)idx];
            const Rml::ColourbPremultiplied c = v.colour;
            sgl_c4f((float)c.red / 255.0f, (float)c.green / 255.0f, (float)c.blue / 255.0f, (float)c.alpha / 255.0f);
            sgl_t2f(v.tex_coord.x, v.tex_coord.y);
            sgl_v2f(v.position.x + translation.x, v.position.y + translation.y);
        }
        sgl_end();

        sgl_pop_pipeline();
    }

    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override {
        geometries.erase((uintptr_t)geometry);
    }

    Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override {
        std::string path = source.c_str();
        int w = 0, h = 0, n = 0;
        unsigned char* rgba = stbi_load(path.c_str(), &w, &h, &n, 4);
        if (!rgba || w <= 0 || h <= 0) {
            if (rgba) stbi_image_free(rgba);
            texture_dimensions = Rml::Vector2i(0, 0);
            return 0;
        }

        // RmlUi expects premultiplied alpha pixel data.
        const size_t px_count = (size_t)w * (size_t)h;
        for (size_t i = 0; i < px_count; i++) {
            const uint8_t a = rgba[i * 4 + 3];
            rgba[i * 4 + 0] = (uint8_t)((uint32_t)rgba[i * 4 + 0] * (uint32_t)a / 255u);
            rgba[i * 4 + 1] = (uint8_t)((uint32_t)rgba[i * 4 + 1] * (uint32_t)a / 255u);
            rgba[i * 4 + 2] = (uint8_t)((uint32_t)rgba[i * 4 + 2] * (uint32_t)a / 255u);
        }

        Rml::Vector2i dims(w, h);
        Rml::TextureHandle hnd = GenerateTexture(Rml::Span<const Rml::byte>((const Rml::byte*)rgba, (size_t)w * (size_t)h * 4u), dims);
        stbi_image_free(rgba);
        texture_dimensions = dims;
        return hnd;
    }

    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) override {
        if (source_dimensions.x <= 0 || source_dimensions.y <= 0) return 0;
        if (source.size() < (size_t)source_dimensions.x * (size_t)source_dimensions.y * 4u) return 0;

        Texture t;
        t.w = source_dimensions.x;
        t.h = source_dimensions.y;

        sg_image_desc img{};
        img.width = t.w;
        img.height = t.h;
        img.pixel_format = SG_PIXELFORMAT_RGBA8;
        img.data.mip_levels[0].ptr = source.data();
        img.data.mip_levels[0].size = source.size();
        img.label = "rmlui-texture";
        t.img = sg_make_image(&img);
        if (t.img.id == SG_INVALID_ID) return 0;

        sg_view_desc view{};
        view.texture.image = t.img;
        view.label = "rmlui-texture-view";
        t.view = sg_make_view(&view);
        if (t.view.id == SG_INVALID_ID) {
            sg_destroy_image(t.img);
            return 0;
        }

        t.sampler = sg_sampler{};
        const uintptr_t id = next_texture++;
        textures.emplace(id, std::move(t));
        return (Rml::TextureHandle)id;
    }

    void ReleaseTexture(Rml::TextureHandle texture) override {
        auto it = textures.find((uintptr_t)texture);
        if (it == textures.end()) return;
        destroy_texture(it->second);
        textures.erase(it);
    }

    void EnableScissorRegion(bool enable) override {
        scissor_enabled = enable;
        if (!enable) {
            // Reset to a full scissor, required because scissor state persists across passes.
            sgl_scissor_rect(0, 0, viewport_w, viewport_h, true /* origin_top_left */);
        }
    }

    void SetScissorRegion(Rml::Rectanglei region) override {
        if (!scissor_enabled) return;
        sgl_scissor_rect(region.Left(), region.Top(), region.Width(), region.Height(), true /* origin_top_left */);
    }

    void SetTransform(const Rml::Matrix4f* transform) override {
        transform_active = (transform != nullptr);
        if (transform) {
            const float* m = transform->data();
            for (int i = 0; i < 16; i++) transform_m[i] = m[i];
        }
    }

    void begin_frame(int w, int h) {
        viewport_w = std::max(1, w);
        viewport_h = std::max(1, h);
        if (!scissor_enabled) {
            sgl_scissor_rect(0, 0, viewport_w, viewport_h, true /* origin_top_left */);
        }
        // Keep texture matrix defaulted.
        sgl_matrix_mode_texture();
        sgl_load_identity();
    }

  private:
    struct Geometry {
        std::vector<Rml::Vertex> vertices;
        std::vector<int> indices;
    };
    struct Texture {
        int w = 0;
        int h = 0;
        sg_image img{};
        sg_view view{};
        sg_sampler sampler{};
    };

    static void destroy_texture(Texture& t) {
        if (t.view.id != SG_INVALID_ID) {
            sg_destroy_view(t.view);
            t.view.id = SG_INVALID_ID;
        }
        if (t.img.id != SG_INVALID_ID) {
            sg_destroy_image(t.img);
            t.img.id = SG_INVALID_ID;
        }
        if (t.sampler.id != SG_INVALID_ID) {
            sg_destroy_sampler(t.sampler);
            t.sampler.id = SG_INVALID_ID;
        }
    }

    std::unordered_map<uintptr_t, Geometry> geometries;
    uintptr_t next_geometry = 1;
    std::unordered_map<uintptr_t, Texture> textures;
    uintptr_t next_texture = 1;

    bool scissor_enabled = false;
    int viewport_w = 1;
    int viewport_h = 1;

    bool transform_active = false;
    float transform_m[16] = {0};

    sgl_pipeline premult_pip{};
    sg_sampler default_sampler{};
};

class SokolRmlSystemInterface final : public Rml::SystemInterface {
  public:
    double GetElapsedTime() override { return elapsed; }
    void set_elapsed(double t) { elapsed = t; }

  private:
    double elapsed = 0.0;
};

class RmlUiBackend final : public UiBackend {
  public:
    const char* name() const override { return "rmlui"; }

    void init_gfx() override {
        if (initialized) return;

        renderer.init();
        Rml::SetRenderInterface(&renderer);
        Rml::SetSystemInterface(&system);
        initialized = Rml::Initialise();
        if (!initialized) {
            std::cerr << "[rmlui] Rml::Initialise failed\n";
            return;
        }

        // Load a default font face if possible.
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
        if (!font_path.empty()) {
            (void)Rml::LoadFontFace(Rml::String(font_path.string().c_str()));
        }
    }

    void shutdown_gfx() override {
        if (!initialized) return;
        destroy_context();
        Rml::Shutdown();
        renderer.shutdown();
        initialized = false;
    }

    void set_input(const InputState& input, float dt, float dpi) override {
        (void)dpi;
        pending_input = input;
        pending_dt = dt;
    }

    void layout(float w, float h, float dpi) override {
        ensure_context((int)std::lround(w), (int)std::lround(h), dpi);
        if (!ctx) return;

        elapsed += pending_dt;
        system.set_elapsed(elapsed);

        // Feed input.
        ctx->ProcessMouseMove((int)std::lround(pending_input.mouse_x), (int)std::lround(pending_input.mouse_y), 0);
        if (pending_input.mouse_down && !prev_mouse_down) {
            ctx->ProcessMouseButtonDown(0, 0);
        } else if (!pending_input.mouse_down && prev_mouse_down) {
            ctx->ProcessMouseButtonUp(0, 0);
        }
        prev_mouse_down = pending_input.mouse_down;
        if (pending_input.scroll_x != 0.0f || pending_input.scroll_y != 0.0f) {
            // Convert Clay's scroll convention back to browser-like wheel deltas: positive is right/down.
            ctx->ProcessMouseWheel(Rml::Vector2f{-pending_input.scroll_x, -pending_input.scroll_y}, 0);
        }

        const uint64_t tree_rev = get_tree_rev();
        if (!doc || last_tree_rev != tree_rev) {
            rebuild_document();
            last_tree_rev = tree_rev;
        }

        ctx->Update();
    }

    void render(float w, float h, float) override {
        if (!ctx) return;
        renderer.begin_frame((int)std::lround(w), (int)std::lround(h));
        ctx->Render();
        sgl_draw();
    }

    bool hit_test(float x, float y, float, webcc::handle& out) override {
        out = webcc::handle();
        if (!ctx) return false;

        Rml::Element* e = ctx->GetElementAtPoint(Rml::Vector2f{x, y});
        while (e) {
            Rml::Variant* v = e->GetAttribute("data-coi-id");
            if (v) {
                const Rml::String s = v->Get<Rml::String>();
                int id = 0;
                if (std::sscanf(s.c_str(), "%d", &id) == 1 && id != 0) {
                    out = webcc::handle(id);
                    return true;
                }
            }
            e = e->GetParentNode();
        }
        return false;
    }

    void scroll_by(float pointer_x, float pointer_y, float dx, float dy, float, float) override {
        if (!ctx) return;
        ctx->ProcessMouseMove((int)std::lround(pointer_x), (int)std::lround(pointer_y), 0);
        ctx->ProcessMouseWheel(Rml::Vector2f{dx, dy}, 0);
    }

    bool is_ok() const { return ctx != nullptr; }

  private:
    static uint64_t get_tree_rev() {
        // Conservative: rebuild on any structural or content changes signaled by the UI tree.
        return coi::ui::g_rev;
    }

    void ensure_context(int w, int h, float dpi) {
        (void)dpi;
        if (!initialized) init_gfx();
        if (!initialized) return;
        if (!ctx) {
            ctx = Rml::CreateContext("coi", Rml::Vector2i{w, h});
            if (!ctx) {
                std::cerr << "[rmlui] CreateContext failed\n";
                return;
            }
        }
        ctx->SetDimensions(Rml::Vector2i{w, h});
    }

    void destroy_context() {
        if (doc) {
            doc->Close();
            doc = nullptr;
        }
        if (ctx) {
            Rml::RemoveContext("coi");
            ctx = nullptr;
        }
        element_by_node.clear();
        text_by_node.clear();
    }

    static void set_px(Rml::Element* e, const char* prop, float v) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.0fpx", (double)v);
        (void)e->SetProperty(prop, buf);
    }
    static void set_color_rgba(Rml::Element* e, const char* prop, float r, float g, float b, float a) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "rgba(%d,%d,%d,%.3f)", (int)std::lround(r * 255.0), (int)std::lround(g * 255.0), (int)std::lround(b * 255.0), (double)a);
        (void)e->SetProperty(prop, buf);
    }

    static void apply_style(Rml::Element* e, const coi::ui::Node& n, const DesktopClassStyle& st, bool is_root) {
        const webcc::string* cls = attr(n, "class");
        uint32_t h = hash_u32(cls ? cls->c_str() : n.tag.c_str());
        float cr = 0.2f, cg = 0.2f, cb = 0.2f;
        color_from_hash(h, cr, cg, cb);

        if (is_root) {
            (void)e->SetProperty("position", "relative");
            (void)e->SetProperty("width", "100%");
            (void)e->SetProperty("height", "100%");
        }

        if (st.has_dir) {
            (void)e->SetProperty("display", "flex");
            (void)e->SetProperty("flex-direction", (st.dir == CLAY_LEFT_TO_RIGHT) ? "row" : "column");
        }

        if (st.has_pad) set_px(e, "padding", (float)st.pad);
        if (st.has_gap) set_px(e, "gap", (float)st.gap);

        if (st.w_fixed) set_px(e, "width", st.w);
        if (st.h_fixed) set_px(e, "height", st.h);

        if (st.w_has_min) set_px(e, "min-width", st.w_min);
        if (st.w_has_max) set_px(e, "max-width", st.w_max);
        if (st.h_has_min) set_px(e, "min-height", st.h_min);
        if (st.h_has_max) set_px(e, "max-height", st.h_max);

        if (st.w_grow || st.h_grow) {
            (void)e->SetProperty("flex-grow", "1");
            (void)e->SetProperty("flex-shrink", "1");
            (void)e->SetProperty("flex-basis", "auto");
        }

        if (!st.bg_none) {
            set_color_rgba(e, "background-color", cr, cg, cb, 0.18f);
        } else {
            (void)e->SetProperty("background-color", "transparent");
        }

        if (st.has_border) {
            if (st.border_width.top) set_px(e, "border-top-width", (float)st.border_width.top);
            if (st.border_width.right) set_px(e, "border-right-width", (float)st.border_width.right);
            if (st.border_width.bottom) set_px(e, "border-bottom-width", (float)st.border_width.bottom);
            if (st.border_width.left) set_px(e, "border-left-width", (float)st.border_width.left);
            if (st.border_width.top || st.border_width.right || st.border_width.bottom || st.border_width.left) {
                set_color_rgba(e, "border-color", cr, cg, cb, 0.60f);
            }
        }

        if (st.has_corner_radius) {
            set_px(e, "border-top-left-radius", (float)st.corner_radius.topLeft);
            set_px(e, "border-top-right-radius", (float)st.corner_radius.topRight);
            set_px(e, "border-bottom-right-radius", (float)st.corner_radius.bottomRight);
            set_px(e, "border-bottom-left-radius", (float)st.corner_radius.bottomLeft);
        }

        if (st.has_opacity) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.3f", (double)st.opacity);
            (void)e->SetProperty("opacity", buf);
        }

        if (st.has_clip) {
            const char* ox = st.scroll_x ? "scroll" : (st.clip_x ? "hidden" : "visible");
            const char* oy = st.scroll_y ? "scroll" : (st.clip_y ? "hidden" : "visible");
            (void)e->SetProperty("overflow-x", ox);
            (void)e->SetProperty("overflow-y", oy);
        }

        if (st.has_align_x || st.has_align_y) {
            const bool is_row = (!st.has_dir) ? true : (st.dir == CLAY_LEFT_TO_RIGHT);
            auto ax = [&](Clay_LayoutAlignmentX a) -> const char* {
                switch (a) {
                case CLAY_ALIGN_X_CENTER:
                    return "center";
                case CLAY_ALIGN_X_RIGHT:
                    return "flex-end";
                default:
                    return "flex-start";
                }
            };
            auto ay = [&](Clay_LayoutAlignmentY a) -> const char* {
                switch (a) {
                case CLAY_ALIGN_Y_CENTER:
                    return "center";
                case CLAY_ALIGN_Y_BOTTOM:
                    return "flex-end";
                default:
                    return "flex-start";
                }
            };
            if (is_row) {
                if (st.has_align_x) (void)e->SetProperty("justify-content", ax(st.align_x));
                if (st.has_align_y) (void)e->SetProperty("align-items", ay(st.align_y));
            } else {
                if (st.has_align_y) (void)e->SetProperty("justify-content", ay(st.align_y));
                if (st.has_align_x) (void)e->SetProperty("align-items", ax(st.align_x));
            }
        }

        // Text styling
        if (!n.text.empty()) {
            if (st.has_font_size) set_px(e, "font-size", (float)st.font_size);
            if (st.has_letter_spacing) set_px(e, "letter-spacing", (float)st.letter_spacing);
            if (st.has_line_height) set_px(e, "line-height", (float)st.line_height);
            if (st.has_text_align) {
                const char* ta = "left";
                if (st.text_align == CLAY_TEXT_ALIGN_CENTER) ta = "center";
                else if (st.text_align == CLAY_TEXT_ALIGN_RIGHT) ta = "right";
                (void)e->SetProperty("text-align", ta);
            }
            // Ensure visible text color.
            (void)e->SetProperty("color", "rgba(255,255,255,0.92)");
        }

        if (st.has_floating || st.has_float_x || st.has_float_y || st.has_z_index) {
            (void)e->SetProperty("position", "absolute");
            if (st.has_float_x) set_px(e, "left", st.float_x);
            if (st.has_float_y) set_px(e, "top", st.float_y);
            if (st.has_z_index) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%d", (int)st.z_index);
                (void)e->SetProperty("z-index", buf);
            }
        }
    }

    void rebuild_document() {
        if (!ctx) return;
        element_by_node.clear();
        text_by_node.clear();

        if (doc) {
            doc->Close();
            doc = nullptr;
        }

        doc = ctx->CreateDocument("body");
        if (!doc) {
            std::cerr << "[rmlui] CreateDocument failed\n";
            return;
        }
        doc->Show();

        auto root_it = coi::ui::g_nodes.find(0);
        if (root_it == coi::ui::g_nodes.end()) return;
        // Apply styles directly to the document root (node 0 is "body").
        const coi::ui::Node& root = root_it->second;
        doc->SetAttribute("data-coi-id", Rml::String("0"));
#if defined(COI_NATIVE_CLAY)
        DesktopClassStyle st = parse_desktop_class_style(root, true);
        apply_style(doc, root, st, true);
#endif
        element_by_node[0] = doc;
        for (int32_t c : root.children) {
            build_node(doc, c, false);
        }
    }

    void build_node(Rml::Element* parent, int32_t id, bool is_root) {
        auto it = coi::ui::g_nodes.find(id);
        if (it == coi::ui::g_nodes.end()) return;
        const coi::ui::Node& n = it->second;
        if (n.tag == "comment") return;

        Rml::ElementPtr el = doc->CreateElement(n.tag.c_str());
        if (!el) {
            el = doc->CreateElement("div");
            if (!el) return;
        }
        Rml::Element* e = el.get();
        parent->AppendChild(std::move(el));

        // Copy attributes (best-effort).
        for (const auto& a : n.attrs) {
            e->SetAttribute(a.key.c_str(), Rml::String(a.value.c_str()));
        }

        // Map back to COI handles for hit-testing / dispatch.
        char id_buf[32];
        std::snprintf(id_buf, sizeof(id_buf), "%d", id);
        e->SetAttribute("data-coi-id", Rml::String(id_buf));

#if defined(COI_NATIVE_CLAY)
        DesktopClassStyle st = parse_desktop_class_style(n, is_root);
        apply_style(e, n, st, is_root);
#endif

        element_by_node[id] = e;

        if (!n.text.empty()) {
            Rml::ElementPtr t = doc->CreateTextNode(Rml::String(n.text.c_str()));
            if (t) {
                Rml::Element* tp = t.get();
                e->AppendChild(std::move(t));
                if (auto* et = dynamic_cast<Rml::ElementText*>(tp)) {
                    text_by_node[id] = et;
                }
            }
        }

        for (int32_t c : n.children) {
            build_node(e, c, false);
        }
    }

    SokolRmlRenderInterface renderer;
    SokolRmlSystemInterface system;
    bool initialized = false;

    Rml::Context* ctx = nullptr;
    Rml::ElementDocument* doc = nullptr;

    InputState pending_input;
    float pending_dt = 0.0f;
    bool prev_mouse_down = false;
    double elapsed = 0.0;

    uint64_t last_tree_rev = 0;
    std::unordered_map<int32_t, Rml::Element*> element_by_node;
    std::unordered_map<int32_t, Rml::ElementText*> text_by_node;
};

inline RmlUiBackend& rmlui_backend() {
    static RmlUiBackend b;
    return b;
}

} // namespace detail
#endif // COI_NATIVE_RMLUI
