#if defined(COI_NATIVE_RMLUI)

	namespace detail {

class SokolRmlRenderInterface final : public Rml::RenderInterface {
  public:
    void init() {
        if (premult_pip.id != 0) return;

        sg_pipeline_desc pip{};
        pip.cull_mode = SG_CULLMODE_NONE;
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
        render_geometry_calls++;
        auto it = geometries.find((uintptr_t)geometry);
        if (it == geometries.end()) return;

        const char* dbg = std::getenv("COI_NATIVE_RMLUI_DEBUG");
        if (dbg && *dbg && std::string(dbg) != "0" && render_geometry_calls == 1) {
            const Geometry& g = it->second;
            std::cerr << "[rmlui] RenderGeometry first: idx=" << g.indices.size() << " vtx=" << g.vertices.size() << " tx=" << texture
                      << " translate=" << translation.x << "," << translation.y << "\n";
            for (int k = 0; k < 3 && k < (int)g.indices.size(); k++) {
                int vi = g.indices[(size_t)k];
                if (vi < 0 || (size_t)vi >= g.vertices.size()) continue;
                const Rml::Vertex& v = g.vertices[(size_t)vi];
                const auto c = v.colour;
                std::cerr << "[rmlui]  v" << k << " pos=" << v.position.x << "," << v.position.y << " uv=" << v.tex_coord.x << "," << v.tex_coord.y
                          << " rgba=" << (int)c.red << "," << (int)c.green << "," << (int)c.blue << "," << (int)c.alpha << "\n";
            }
        }

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
        scissor_has_region = false;
        if (!enable) {
            // Reset to a full scissor, required because scissor state persists across passes.
            sgl_scissor_rect(0, 0, viewport_w, viewport_h, true /* origin_top_left */);
        }
    }

    void SetScissorRegion(Rml::Rectanglei region) override {
        if (!scissor_enabled) return;
        scissor_has_region = true;
        sgl_scissor_rect(region.Left(), region.Top(), region.Width(), region.Height(), true /* origin_top_left */);
    }

    void EnableClipMask(bool enable) override {
        clip_enabled = enable;
        clip_enable_calls++;
        (void)enable;
    }

    void RenderToClipMask(Rml::ClipMaskOperation, Rml::CompiledGeometryHandle, Rml::Vector2f) override {
        clip_render_calls++;
    }

    Rml::LayerHandle PushLayer() override {
        layer_push_calls++;
        return {};
    }
    void PopLayer() override { layer_pop_calls++; }
    void CompositeLayers(Rml::LayerHandle, Rml::LayerHandle, Rml::BlendMode, Rml::Span<const Rml::CompiledFilterHandle>) override {
        layer_composite_calls++;
    }

    void SetTransform(const Rml::Matrix4f* transform) override {
        transform_active = (transform != nullptr);
        if (transform) {
            const float* m = transform->data();
            for (int i = 0; i < 16; i++) transform_m[i] = m[i];
        }
    }

    void begin_frame(int w, int h) {
        render_geometry_calls = 0;
        clip_enable_calls = 0;
        clip_render_calls = 0;
        layer_push_calls = 0;
        layer_pop_calls = 0;
        layer_composite_calls = 0;
        viewport_w = std::max(1, w);
        viewport_h = std::max(1, h);
        // Always start with a full-target scissor to avoid leaking prior state.
        sgl_scissor_rect(0, 0, viewport_w, viewport_h, true /* origin_top_left */);
        scissor_has_region = false;
        // Keep texture matrix defaulted.
        sgl_matrix_mode_texture();
        sgl_load_identity();
    }

    int get_and_clear_render_geometry_calls() const { return render_geometry_calls; }
    int get_clip_enable_calls() const { return clip_enable_calls; }
    int get_clip_render_calls() const { return clip_render_calls; }
    int get_layer_push_calls() const { return layer_push_calls; }
    int get_layer_pop_calls() const { return layer_pop_calls; }
    int get_layer_composite_calls() const { return layer_composite_calls; }

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
    bool scissor_has_region = false;
    int viewport_w = 1;
    int viewport_h = 1;

    bool clip_enabled = false;
    bool transform_active = false;
    float transform_m[16] = {0};

    sgl_pipeline premult_pip{};
    sg_sampler default_sampler{};

    mutable int render_geometry_calls = 0;
    mutable int clip_enable_calls = 0;
    mutable int clip_render_calls = 0;
    mutable int layer_push_calls = 0;
    mutable int layer_pop_calls = 0;
    mutable int layer_composite_calls = 0;
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
        const char* dbg = std::getenv("COI_NATIVE_RMLUI_DEBUG");
        if (dbg && *dbg && std::string(dbg) != "0") {
            std::cerr << "[rmlui] global render interface=" << (void*)Rml::GetRenderInterface() << " local=" << (void*)&renderer << "\n";
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

        const bool ok = ctx->Update();
        const char* dbg = std::getenv("COI_NATIVE_RMLUI_DEBUG");
            if (dbg && *dbg && std::string(dbg) != "0") {
                if (!ok) std::cerr << "[rmlui] Context::Update returned false\n";
            if (doc) {
                std::cerr << "[rmlui] doc children=" << doc->GetNumChildren() << " size=" << doc->GetOffsetWidth() << "x" << doc->GetOffsetHeight() << "\n";
                if (doc->GetNumChildren() > 0) {
                    Rml::Element* c0 = doc->GetChild(0);
                    if (c0) {
                        std::cerr << "[rmlui] child0 <" << c0->GetTagName().c_str() << "> size=" << c0->GetOffsetWidth() << "x" << c0->GetOffsetHeight()
                                  << " pos=" << c0->GetAbsoluteLeft() << "," << c0->GetAbsoluteTop() << "\n";
                        std::cerr << "[rmlui] child0 children=" << c0->GetNumChildren() << "\n";
                        const int nchild = std::min(3, c0->GetNumChildren());
                        for (int i = 0; i < nchild; i++) {
                            Rml::Element* c = c0->GetChild(i);
                            if (!c) continue;
                            std::cerr << "[rmlui] child0[" << i << "] <" << c->GetTagName().c_str() << "> size=" << c->GetOffsetWidth() << "x"
                                      << c->GetOffsetHeight() << " pos=" << c->GetAbsoluteLeft() << "," << c->GetAbsoluteTop()
                                      << " children=" << c->GetNumChildren() << "\n";
                        }
                    }
                }
            }
            }
    }

    void render(float w, float h, float) override {
        if (!ctx) return;
        renderer.begin_frame((int)std::lround(w), (int)std::lround(h));
        const bool ok = ctx->Render();
        const char* dbg = std::getenv("COI_NATIVE_RMLUI_DEBUG");
        if (dbg && *dbg && std::string(dbg) != "0") {
            if (!ok) std::cerr << "[rmlui] Context::Render returned false\n";
            const int calls = renderer.get_and_clear_render_geometry_calls();
            std::cerr << "[rmlui] RenderGeometry calls: " << calls << "\n";
            std::cerr << "[rmlui] ClipMask enable=" << renderer.get_clip_enable_calls() << " render=" << renderer.get_clip_render_calls() << "\n";
            std::cerr << "[rmlui] Layers push=" << renderer.get_layer_push_calls() << " pop=" << renderer.get_layer_pop_calls()
                      << " composite=" << renderer.get_layer_composite_calls() << "\n";
            std::cerr << "[rmlui] sgl cmds=" << sgl_num_commands() << " verts=" << sgl_num_vertices() << "\n";
        }
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

    bool is_ok() const override { return ctx != nullptr; }

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
            // Pass explicit render interface to avoid relying on the global default.
            ctx = Rml::CreateContext("coi", Rml::Vector2i{w, h}, &renderer);
            if (!ctx) {
                std::cerr << "[rmlui] CreateContext failed\n";
                return;
            }
            const char* dbg = std::getenv("COI_NATIVE_RMLUI_DEBUG");
            if (dbg && *dbg && std::string(dbg) != "0") {
                std::cerr << "[rmlui] context created, docs=" << ctx->GetNumDocuments() << "\n";
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
    }

	    static std::string escape_attr(const std::string& s) {
	        std::string out;
	        out.reserve(s.size());
	        for (char c : s) {
            switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            default:
                out.push_back(c);
                break;
            }
        }
        return out;
    }

    static std::string escape_text(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            default:
                out.push_back(c);
                break;
            }
        }
        return out;
    }

	    static void append_node_rml(std::string& out, int32_t id, bool is_root) {
	        auto it = coi::ui::g_nodes.find(id);
	        if (it == coi::ui::g_nodes.end()) return;
	        const coi::ui::Node& n = it->second;
        if (n.tag == "comment") return;

        const char* tag = "div";
        bool self_close = false;
        const webcc::string* src = attr(n, "src");
        if (n.tag == "img" && src && n.children.empty() && n.text.empty()) {
            tag = "img";
            self_close = true;
        }

	        const webcc::string* cls = attr(n, "class");
	        const webcc::string_view cls_sv = cls ? webcc::string_view(cls->c_str(), cls->length()) : webcc::string_view();
	        const webcc::string_view tag_sv(n.tag.c_str(), n.tag.length());
	        const webcc::string css = coi::style::css_style_attr_for_node_rmlui(cls_sv, tag_sv, is_root);

	        out += "<";
	        out += tag;
	        out += " data-coi-id=\"";
        out += std::to_string(id);
        out += "\"";

	        if (!css.empty()) {
	            out += " style=\"";
	            out += escape_attr(std::string(css.c_str()));
	            out += "\"";
	        }

        if (tag == std::string("img") && src) {
            out += " src=\"";
            out += escape_attr(std::string(src->c_str()));
            out += "\"";
        }

        for (const auto& a : n.attrs) {
            if (a.key == "class") continue;
            if (tag == std::string("img") && a.key == "src") continue;
            out += " ";
            out += a.key.c_str();
            out += "=\"";
            out += escape_attr(std::string(a.value.c_str()));
            out += "\"";
        }

        if (self_close) {
            out += " />";
            return;
        }

        out += ">";
        if (!n.text.empty()) out += escape_text(std::string(n.text.c_str()));
        for (int32_t c : n.children) append_node_rml(out, c, false);
        out += "</";
        out += tag;
        out += ">";
    }

		    static std::string build_document_rml() {
	        const char* smoke = std::getenv("COI_NATIVE_RMLUI_SMOKE");
	        if (smoke && *smoke && std::string(smoke) != "0") {
	            return "<rml><body style=\"margin:0; width:100%; height:100%; background-color: rgb(20,20,26);\">"
	                   "<div style=\"display:block; position:absolute; left:50px; top:50px; width:300px; height:180px; background-color:#ff0000; opacity:0.7;\"></div>"
	                   "</body></rml>";
	        }

        std::string out;
        out.reserve(4096);

	        auto it = coi::ui::g_nodes.find(0);
	        if (it == coi::ui::g_nodes.end()) return "<rml><body></body></rml>";
	        const coi::ui::Node& root = it->second;

	        const webcc::string* cls = attr(root, "class");
	        const webcc::string_view cls_sv = cls ? webcc::string_view(cls->c_str(), cls->length()) : webcc::string_view();
	        const webcc::string_view tag_sv(root.tag.c_str(), root.tag.length());
	        const webcc::string css = coi::style::css_style_attr_for_node_rmlui(cls_sv, tag_sv, true);

	        out += "<rml><head><style>"
	               "body{margin:0; font-family: Roboto; font-size:16px; color: rgba(255,255,255,235);} "
	               "*,*:before,*:after{box-sizing:border-box;}"
	               "</style></head><body data-coi-id=\"0\"";
	        if (!css.empty()) {
	            out += " style=\"";
	            out += escape_attr(std::string(css.c_str()));
	            out += "\"";
	        }
        out += ">";
        for (int32_t c : root.children) append_node_rml(out, c, false);
        out += "</body></rml>";
        return out;
    }

    void rebuild_document() {
        if (!ctx) return;
        if (doc) {
            doc->Close();
            doc = nullptr;
        }

        // Using LoadDocumentFromMemory ensures the document is properly initialized and attached
        // to the context's document stack for rendering.
        const std::string rml = build_document_rml();
        const char* dbg = std::getenv("COI_NATIVE_RMLUI_DEBUG");
        if (dbg && *dbg && std::string(dbg) != "0") {
            std::cerr << "[rmlui] rml: " << rml.substr(0, 400) << (rml.size() > 400 ? "..." : "") << "\n";
        }
        doc = ctx->LoadDocumentFromMemory(Rml::String(rml.c_str()), "[coi native rmlui]");
        if (!doc) {
            std::cerr << "[rmlui] LoadDocumentFromMemory failed\n";
            return;
        }
        doc->Show();

        if (dbg && *dbg && std::string(dbg) != "0") {
            std::cerr << "[rmlui] documents: " << ctx->GetNumDocuments() << "\n";
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
};

inline RmlUiBackend& rmlui_backend() {
    static RmlUiBackend b;
    return b;
}

} // namespace detail
#endif // COI_NATIVE_RMLUI
