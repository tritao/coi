	#if defined(COI_NATIVE_SOKOL)
	struct SokolRunnerImpl {
	    static inline void* app = nullptr;
	    static inline tick_fn tick = nullptr;
	    static inline int frames_limit = -1;
	    static inline int frames = 0;
	    static inline bool render_swapchain = true;

	    static bool window_requested() {
	        const char* e = std::getenv("COI_NATIVE_WINDOW");
	        if (!e || !*e) return false;
	        return !(e[0] == '0' && e[1] == '\0');
	    }

	    static void sg_log(const char* tag, uint32_t log_level, uint32_t log_item_id, const char* message_or_null, uint32_t line_nr,
	                       const char* filename_or_null, void*) {
	        const char* e = std::getenv("COI_NATIVE_SG_LOG");
	        const bool verbose = (e && *e && std::string(e) != "0");
	        if (!verbose && log_level > 1) return; // default: only panic+error
        std::cerr << "[sokol_gfx] " << (tag ? tag : "sg") << " lvl=" << log_level << " item=" << log_item_id;
        if (filename_or_null) std::cerr << " at " << filename_or_null << ":" << line_nr;
        if (message_or_null) std::cerr << " " << message_or_null;
        std::cerr << "\n";
    }

	    static bool overlay_enabled() {
        const char* e = std::getenv("COI_NATIVE_OVERLAY");
        if (e && *e) {
            return std::string(e) != std::string("0");
        }
#if defined(COI_NATIVE_CAPTURE)
        if (capture_enabled) return false;
#endif
        return true;
	    }

	    enum class BackendPref { Auto, Tree, Clay, RmlUi };
	    static BackendPref backend_pref() {
	        const char* e = std::getenv("COI_NATIVE_UI_BACKEND");
	        if (!e || !*e) return BackendPref::Auto;
	        std::string v(e);
	        if (v == "tree") return BackendPref::Tree;
	        if (v == "clay") return BackendPref::Clay;
	        if (v == "rmlui") return BackendPref::RmlUi;
	        return BackendPref::Auto;
	    }

	    static void init(void) {
        stm_setup();
        sg_desc desc{};
        desc.logger.func = sg_log;
        desc.environment = sglue_environment();
        sg_setup(&desc);
	        const sg_swapchain sc = sglue_swapchain();
	        sgl_desc_t gld{};
	        gld.color_format = sc.color_format;
	        gld.depth_format = sc.depth_format;
	        gld.sample_count = sc.sample_count;
	        sgl_setup(&gld);

        sdtx_desc_t ddesc{};
        ddesc.fonts[0] = sdtx_font_kc853();
        ddesc.fonts[1] = sdtx_font_kc854();
        ddesc.fonts[2] = sdtx_font_z1013();
	        ddesc.fonts[3] = sdtx_font_cpc();
	        ddesc.fonts[4] = sdtx_font_c64();
	        ddesc.fonts[5] = sdtx_font_oric();
	        sdtx_setup(&ddesc);

#if defined(COI_NATIVE_CLAY)
	        detail::clay_backend().init_gfx();
#endif

#if defined(COI_NATIVE_CLAY)
#if defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
	        const float dpi = (sapp_dpi_scale() > 0.0f) ? sapp_dpi_scale() : 1.0f;
	        ClayEngine::ensure((float)sapp_width() / dpi, (float)sapp_height() / dpi);
	        if (ClayEngine::ctx && detail::clay_backend().font_ok()) {
	            Clay_SetCurrentContext(ClayEngine::ctx);
	            Clay_SetMeasureTextFunction(sclay_measure_text, detail::clay_backend().measure_userdata());
	            Clay_ResetMeasureTextCache();
	        }
#else
	        ClayEngine::ensure((float)sapp_width(), (float)sapp_height());
#endif
#endif

#if defined(COI_NATIVE_CAPTURE)
	        capture_init();
#endif

#if defined(__linux__) || defined(__unix__)
	        // Offscreen capture-only mode doesn't need a visible window; we only use sokol_app
	        // to get a GL context. Hide the X11 window to avoid popups during visual runs.
	        if (!render_swapchain) {
	            Display* dpy = (Display*)sapp_x11_get_display();
	            Window win = (Window)(uintptr_t)sapp_x11_get_window();
	            if (dpy && win) {
	                XUnmapWindow(dpy, win);
	                XFlush(dpy);
	            }
	        }
#endif
	    }

#if defined(COI_NATIVE_CAPTURE)
    static inline bool capture_enabled = false;
    enum class CaptureMode { Offscreen, X11 };
    static inline CaptureMode capture_mode = CaptureMode::Offscreen;
    static inline int capture_every = 60;
    static inline int capture_max = -1;
    static inline int capture_index = 0;
    static inline int capture_tolerance = 8; // max Hamming distance for dHash
    static inline bool capture_overlay = false;
    static inline bool capture_fail_on_mismatch = false;
    static inline std::filesystem::path capture_dir;
    static inline std::filesystem::path capture_baseline_dir;
    static inline int capture_w = 0;
    static inline int capture_h = 0;
    static inline sg_image capture_img{};
    static inline sg_image capture_ds_img{};
    static inline sg_view capture_view{};
    static inline sg_view capture_ds_view{};
    static inline std::vector<uint8_t> capture_pixels;
    static inline std::vector<uint8_t> capture_pixels_flipped;
    static inline int exit_code = 0;
    static inline bool capture_debug = false;

    static inline bool parse_wh(const char* s, int& w, int& h) {
        if (!s || !*s) return false;
        int iw = 0, ih = 0;
        if (std::sscanf(s, "%dx%d", &iw, &ih) == 2 || std::sscanf(s, "%d,%d", &iw, &ih) == 2 || std::sscanf(s, "%d %d", &iw, &ih) == 2) {
            if (iw <= 0) iw = 1;
            if (ih <= 0) ih = 1;
            w = iw;
            h = ih;
            return true;
        }
        return false;
    }

    static inline uint64_t dhash_rgba8(const uint8_t* rgba, int w, int h) {
        if (!rgba || w <= 0 || h <= 0) return 0;
        uint8_t g[9 * 8]{};
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 9; x++) {
                int sx = (int)(((x + 0.5) * (double)w) / 9.0);
                int sy = (int)(((y + 0.5) * (double)h) / 8.0);
                if (sx < 0) sx = 0;
                if (sy < 0) sy = 0;
                if (sx >= w) sx = w - 1;
                if (sy >= h) sy = h - 1;
                const uint8_t* p = rgba + (size_t)(sy * w + sx) * 4u;
                const uint32_t r = p[0], gg = p[1], b = p[2];
                const uint32_t gray = (r * 77u + gg * 150u + b * 29u) >> 8;
                g[y * 9 + x] = (uint8_t)gray;
            }
        }
        uint64_t out = 0;
        int bit = 0;
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                uint8_t a = g[y * 9 + x];
                uint8_t b = g[y * 9 + x + 1];
                if (a > b) out |= (1ull << bit);
                bit++;
            }
        }
        return out;
    }

    static inline int popcount64(uint64_t v) {
#if defined(__GNUC__) || defined(__clang__)
        return __builtin_popcountll(v);
#else
        int c = 0;
        while (v) {
            v &= (v - 1);
            c++;
        }
        return c;
#endif
    }

    static inline bool read_hex_u64(const std::filesystem::path& p, uint64_t& out) {
        std::ifstream f(p);
        if (!f) return false;
        std::string s;
        f >> s;
        if (s.empty()) return false;
        try {
            size_t idx = 0;
            out = std::stoull(s, &idx, 16);
            return idx > 0;
        } catch (...) {
            return false;
        }
    }

	    static void capture_init() {
	        const char* dir = std::getenv("COI_NATIVE_CAPTURE_DIR");
	        if (!dir || !*dir) return;
	        capture_enabled = true;
	        capture_dir = std::filesystem::path(dir);

        if (const char* e = std::getenv("COI_NATIVE_CAPTURE_DEBUG")) {
            capture_debug = (std::string(e) != "0");
        }

	        // Default capture mode:
	        // - windowed runs: prefer X11 on Linux (robust across drivers)
	        // - capture-only (no COI_NATIVE_WINDOW): prefer offscreen
	        // Offscreen/X11 can be forced via COI_NATIVE_CAPTURE_MODE.
	        const bool want_window = window_requested();
#if defined(__linux__) || defined(__unix__)
	        capture_mode = want_window ? CaptureMode::X11 : CaptureMode::Offscreen;
#else
	        capture_mode = CaptureMode::Offscreen;
#endif
	        if (const char* m = std::getenv("COI_NATIVE_CAPTURE_MODE")) {
	            if (m && *m) {
	                const std::string mm(m);
                if (mm == "x11") capture_mode = CaptureMode::X11;
                if (mm == "offscreen") capture_mode = CaptureMode::Offscreen;
            }
        }

        capture_w = sapp_width();
        capture_h = sapp_height();
        const char* size = std::getenv("COI_NATIVE_CAPTURE_SIZE");
        (void)parse_wh(size, capture_w, capture_h);

        if (const char* e = std::getenv("COI_NATIVE_CAPTURE_EVERY")) {
            capture_every = std::atoi(e);
            if (capture_every <= 0) capture_every = 1;
        }
        if (const char* e = std::getenv("COI_NATIVE_CAPTURE_MAX")) {
            capture_max = std::atoi(e);
        }
        if (const char* e = std::getenv("COI_NATIVE_CAPTURE_TOLERANCE")) {
            capture_tolerance = std::atoi(e);
            if (capture_tolerance < 0) capture_tolerance = 0;
        }
        if (const char* e = std::getenv("COI_NATIVE_CAPTURE_OVERLAY")) {
            capture_overlay = (std::string(e) != "0");
        }
        if (const char* e = std::getenv("COI_NATIVE_CAPTURE_FAIL_ON_MISMATCH")) {
            capture_fail_on_mismatch = (std::string(e) != "0");
        }
        if (const char* base = std::getenv("COI_NATIVE_CAPTURE_BASELINE")) {
            if (*base) capture_baseline_dir = std::filesystem::path(base);
        }

        std::error_code ec;
        std::filesystem::create_directories(capture_dir, ec);

        capture_img = sg_image{};
        capture_ds_img = sg_image{};
        capture_view = sg_view{};
        capture_ds_view = sg_view{};
        capture_pixels.clear();
        capture_pixels_flipped.clear();
        capture_index = 0;

        if (capture_debug) {
            std::cerr << "[capture] enabled dir=" << capture_dir.string()
                      << " size=" << capture_w << "x" << capture_h
                      << " mode=" << (capture_mode == CaptureMode::Offscreen ? "offscreen" : "x11")
                      << " every=" << capture_every
                      << " max=" << capture_max
                      << " baseline=" << capture_baseline_dir.string()
                      << " tol=" << capture_tolerance
                      << " overlay=" << (capture_overlay ? 1 : 0)
                      << " fail=" << (capture_fail_on_mismatch ? 1 : 0)
                      << std::endl;
        }
    }

    static void capture_shutdown() {
        if (capture_ds_view.id != SG_INVALID_ID) {
            sg_destroy_view(capture_ds_view);
            capture_ds_view.id = SG_INVALID_ID;
        }
        if (capture_ds_img.id != SG_INVALID_ID) {
            sg_destroy_image(capture_ds_img);
            capture_ds_img.id = SG_INVALID_ID;
        }
        if (capture_view.id != SG_INVALID_ID) {
            sg_destroy_view(capture_view);
            capture_view.id = SG_INVALID_ID;
        }
        if (capture_img.id != SG_INVALID_ID) {
            sg_destroy_image(capture_img);
            capture_img.id = SG_INVALID_ID;
        }
        capture_pixels.clear();
        capture_pixels_flipped.clear();
    }

    static void capture_ensure_target() {
        if (!capture_enabled) return;
        if (capture_mode != CaptureMode::Offscreen) return;
        if (capture_w <= 0) capture_w = 1;
        if (capture_h <= 0) capture_h = 1;

        if (capture_img.id != SG_INVALID_ID) {
            sg_image_desc d = sg_query_image_desc(capture_img);
            if ((int)d.width == capture_w && (int)d.height == capture_h) return;
            capture_shutdown();
        }

        if (capture_debug) {
            std::cerr << "[capture] ensure_target " << capture_w << "x" << capture_h << std::endl;
        }

        const sg_swapchain sc = sglue_swapchain();

        sg_image_desc img_desc{};
        img_desc.type = SG_IMAGETYPE_2D;
        img_desc.width = capture_w;
        img_desc.height = capture_h;
        img_desc.num_mipmaps = 1;
        img_desc.sample_count = (sc.sample_count > 0) ? sc.sample_count : 1;
        img_desc.pixel_format = (sc.color_format != SG_PIXELFORMAT_NONE) ? sc.color_format : SG_PIXELFORMAT_RGBA8;
        img_desc.usage.color_attachment = true;
        img_desc.label = "coi-capture-color";
        if (capture_debug) {
            std::cerr << "[capture] sg_make_image..." << std::endl;
        }
        capture_img = sg_make_image(&img_desc);
        if (capture_debug) {
            std::cerr << "[capture] sg_make_image id=" << capture_img.id << std::endl;
        }
        if (capture_img.id == SG_INVALID_ID) {
            std::cerr << "[capture] failed to create capture image; disabling capture\n";
            capture_enabled = false;
            return;
        }

        sg_view_desc view_desc{};
        view_desc.color_attachment.image = capture_img;
        view_desc.label = "coi-capture-view";
        if (capture_debug) {
            std::cerr << "[capture] sg_make_view..." << std::endl;
        }
        capture_view = sg_make_view(&view_desc);
        if (capture_debug) {
            std::cerr << "[capture] sg_make_view id=" << capture_view.id << std::endl;
        }
        if (capture_view.id == SG_INVALID_ID) {
            std::cerr << "[capture] failed to create capture view; disabling capture\n";
            capture_enabled = false;
            return;
        }

        if (sc.depth_format != SG_PIXELFORMAT_NONE) {
            sg_image_desc ds_desc{};
            ds_desc.type = SG_IMAGETYPE_2D;
            ds_desc.width = capture_w;
            ds_desc.height = capture_h;
            ds_desc.num_mipmaps = 1;
            ds_desc.sample_count = (sc.sample_count > 0) ? sc.sample_count : 1;
            ds_desc.pixel_format = sc.depth_format;
            ds_desc.usage.depth_stencil_attachment = true;
            ds_desc.label = "coi-capture-depth";
            if (capture_debug) {
                std::cerr << "[capture] sg_make_depth_image..." << std::endl;
            }
            capture_ds_img = sg_make_image(&ds_desc);
            if (capture_debug) {
                std::cerr << "[capture] sg_make_depth_image id=" << capture_ds_img.id << std::endl;
            }
            if (capture_ds_img.id == SG_INVALID_ID) {
                std::cerr << "[capture] failed to create depth image; disabling capture\n";
                capture_enabled = false;
                return;
            }
            sg_view_desc ds_view_desc{};
            ds_view_desc.depth_stencil_attachment.image = capture_ds_img;
            ds_view_desc.label = "coi-capture-depth-view";
            if (capture_debug) {
                std::cerr << "[capture] sg_make_depth_view..." << std::endl;
            }
            capture_ds_view = sg_make_view(&ds_view_desc);
            if (capture_debug) {
                std::cerr << "[capture] sg_make_depth_view id=" << capture_ds_view.id << std::endl;
            }
            if (capture_ds_view.id == SG_INVALID_ID) {
                std::cerr << "[capture] failed to create depth view; disabling capture\n";
                capture_enabled = false;
                return;
            }
        }

        capture_pixels.resize((size_t)capture_w * (size_t)capture_h * 4u);
        capture_pixels_flipped.resize(capture_pixels.size());
    }

    static void capture_maybe(detail::UiBackend& backend, const sg_pass_action& action, double dt) {
        (void)dt;
        if (!capture_enabled) return;
        if (capture_mode != CaptureMode::Offscreen) return;
        if (capture_max >= 0 && capture_index >= capture_max) return;
        if (capture_every > 1 && (frames % capture_every) != 0) return;

        if (capture_debug) {
            std::cerr << "[capture] capture_maybe frame=" << frames << " idx=" << capture_index << std::endl;
        }

        capture_ensure_target();
        if (capture_view.id == SG_INVALID_ID) return;

        // Render to an offscreen pass.
        sg_pass cp{};
        cp.action = action;
        cp.attachments.colors[0] = capture_view;
        if (capture_ds_view.id != SG_INVALID_ID) {
            cp.attachments.depth_stencil = capture_ds_view;
        }
        if (capture_debug) {
            std::cerr << "[capture] sg_begin_pass..." << std::endl;
        }
        sg_begin_pass(&cp);

        sgl_defaults();
        sgl_viewport(0, 0, capture_w, capture_h, true);
        sgl_matrix_mode_projection();
        sgl_load_identity();
        sgl_ortho(0.0f, (float)capture_w, (float)capture_h, 0.0f, -1.0f, 1.0f);
        sgl_matrix_mode_modelview();
        sgl_load_identity();

        backend.layout((float)capture_w, (float)capture_h, 1.0f);
        backend.render((float)capture_w, (float)capture_h, 1.0f);

        if (capture_overlay) {
            sdtx_canvas((float)capture_w, (float)capture_h);
            sdtx_font(0);
            sdtx_origin(1.0f, 1.0f);
            sdtx_home();
            sdtx_color3f(1.0f, 1.0f, 1.0f);
            sdtx_puts("CAPTURE\n");
            sdtx_draw();
        }

        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, capture_w, capture_h, GL_RGBA, GL_UNSIGNED_BYTE, capture_pixels.data());
        sg_end_pass();

        const size_t stride = (size_t)capture_w * 4u;
        for (int y = 0; y < capture_h; y++) {
            const uint8_t* src = capture_pixels.data() + (size_t)(capture_h - 1 - y) * stride;
            uint8_t* dst = capture_pixels_flipped.data() + (size_t)y * stride;
            std::memcpy(dst, src, stride);
        }

        capture_write_outputs(capture_pixels_flipped.data(), capture_w, capture_h, (int)stride);
    }

    static void capture_write_outputs(const uint8_t* rgba_topdown, int w, int h, int stride_bytes) {
        if (!rgba_topdown || w <= 0 || h <= 0) return;
        char name[64];
        std::snprintf(name, sizeof(name), "frame_%06d.png", capture_index);
        std::filesystem::path png_path = capture_dir / name;
        const int ok_png = stbi_write_png(png_path.string().c_str(), w, h, 4, rgba_topdown, stride_bytes);
        if (!ok_png) {
            std::cerr << "[capture] failed to write png: " << png_path.string() << "\n";
        }

        const uint64_t hsh = dhash_rgba8(rgba_topdown, w, h);
        std::snprintf(name, sizeof(name), "frame_%06d.dhash", capture_index);
        std::filesystem::path hash_path = capture_dir / name;
        {
            std::ofstream hf(hash_path);
            hf << std::hex << hsh << "\n";
        }

        if (!capture_baseline_dir.empty()) {
            std::filesystem::path base_hash = capture_baseline_dir / hash_path.filename();
            uint64_t base = 0;
            if (read_hex_u64(base_hash, base)) {
                const int dist = popcount64(hsh ^ base);
                std::cout << "[capture] " << hash_path.filename().string() << " dhash=" << std::hex << hsh << std::dec
                          << " baseline_dist=" << dist << " tol=" << capture_tolerance << "\n";
                if (dist > capture_tolerance) {
                    exit_code = 1;
                    if (capture_fail_on_mismatch) sapp_request_quit();
                }
            } else {
                std::cout << "[capture] " << hash_path.filename().string() << " dhash=" << std::hex << hsh << std::dec << " (no baseline)\n";
            }
        } else {
            std::cout << "[capture] " << hash_path.filename().string() << " dhash=" << std::hex << hsh << std::dec << "\n";
        }

        capture_index++;
    }

    static void capture_maybe_swapchain(double dt) {
        (void)dt;
        if (!capture_enabled) return;
        if (capture_mode != CaptureMode::X11) return;
        if (capture_max >= 0 && capture_index >= capture_max) return;
        if (capture_every > 1 && (frames % capture_every) != 0) return;

        int w = capture_w > 0 ? capture_w : sapp_width();
        int h = capture_h > 0 ? capture_h : sapp_height();
        const int sw = sapp_width();
        const int sh = sapp_height();
        if (w > sw) w = sw;
        if (h > sh) h = sh;
        if (w <= 0) w = 1;
        if (h <= 0) h = 1;

        if (capture_debug) {
            std::cerr << "[capture] swapchain read " << w << "x" << h << " (win " << sw << "x" << sh << ")" << std::endl;
        }

        capture_pixels.resize((size_t)w * (size_t)h * 4u);
        capture_pixels_flipped.resize(capture_pixels.size());

        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, capture_pixels.data());

        const size_t stride = (size_t)w * 4u;
        for (int y = 0; y < h; y++) {
            const uint8_t* src = capture_pixels.data() + (size_t)(h - 1 - y) * stride;
            uint8_t* dst = capture_pixels_flipped.data() + (size_t)y * stride;
            std::memcpy(dst, src, stride);
        }

        capture_write_outputs(capture_pixels_flipped.data(), w, h, (int)stride);
    }
#endif

    static inline float mouse_x = 0.0f;
    static inline float mouse_y = 0.0f;
    static inline bool mouse_down = false;
    static inline float scroll_x = 0.0f;
    static inline float scroll_y = 0.0f;
    static inline bool click_pending = false;
    static inline float click_x = 0.0f;
    static inline float click_y = 0.0f;

    static void event_cb(const sapp_event* ev) {
        if (!ev) return;
        switch (ev->type) {
        case SAPP_EVENTTYPE_MOUSE_MOVE:
            mouse_x = ev->mouse_x;
            mouse_y = ev->mouse_y;
            break;
        case SAPP_EVENTTYPE_MOUSE_SCROLL:
            // sokol_app scroll deltas typically follow browser semantics (positive Y == scroll down),
            // but Clay expects negative scrollDelta.y to move content down (scrollPosition is clamped <= 0).
            scroll_x -= ev->scroll_x;
            scroll_y -= ev->scroll_y;
            break;
        case SAPP_EVENTTYPE_MOUSE_DOWN:
            mouse_down = true;
            mouse_x = ev->mouse_x;
            mouse_y = ev->mouse_y;
            break;
        case SAPP_EVENTTYPE_MOUSE_UP:
            mouse_down = false;
            mouse_x = ev->mouse_x;
            mouse_y = ev->mouse_y;
            click_pending = true;
            click_x = mouse_x;
            click_y = mouse_y;
            break;
        default:
            break;
        }
    }

	    static void frame_cb(void) {
        g_sokol_frame_started = true;
        double dt = sapp_frame_duration();
        if (dt > 0.1) dt = 0.1;
        if (tick && app) tick(app, dt);

        coi::native::flush();

        const float dpi = (sapp_dpi_scale() > 0.0f) ? sapp_dpi_scale() : 1.0f;
        detail::InputState input;
        input.mouse_x = mouse_x;
        input.mouse_y = mouse_y;
        input.mouse_down = mouse_down;
        input.scroll_x = scroll_x;
        input.scroll_y = scroll_y;
        scroll_x = 0.0f;
        scroll_y = 0.0f;

	        detail::UiBackend* backend = &detail::tree_backend();
	        const BackendPref pref = backend_pref();
#if defined(COI_NATIVE_CLAY)
	        const float fbw = (float)sapp_width();
	        const float fbh = (float)sapp_height();
	        auto try_clay = [&]() -> bool {
	            detail::clay_backend().set_input(input, (float)dt, dpi);
	            detail::clay_backend().layout(fbw, fbh, dpi);
	            if (detail::clay_backend().is_ok()) {
	                backend = &detail::clay_backend();
	                return true;
	            }
	            return false;
	        };
#else
	        const float fbw = (float)sapp_width();
	        const float fbh = (float)sapp_height();
#endif

#if defined(COI_NATIVE_RMLUI)
	        auto try_rmlui = [&]() -> bool {
	            detail::rmlui_backend().set_input(input, (float)dt, dpi);
	            detail::rmlui_backend().layout(fbw, fbh, dpi);
	            if (detail::rmlui_backend().is_ok()) {
	                backend = &detail::rmlui_backend();
	                return true;
	            }
	            return false;
	        };
#endif

	        bool selected = false;
	        if (pref == BackendPref::Tree) {
	            detail::tree_backend().layout(fbw, fbh, dpi);
	            selected = true;
	        } else if (pref == BackendPref::Clay) {
#if defined(COI_NATIVE_CLAY)
	            selected = try_clay();
#endif
	        } else if (pref == BackendPref::RmlUi) {
#if defined(COI_NATIVE_RMLUI)
	            selected = try_rmlui();
#endif
	        } else {
	            // Auto: prefer Clay, then RmlUI, then fallback tree.
#if defined(COI_NATIVE_CLAY)
	            selected = try_clay();
#endif
#if defined(COI_NATIVE_RMLUI)
	            if (!selected) selected = try_rmlui();
#endif
	            if (!selected) {
	                detail::tree_backend().layout(fbw, fbh, dpi);
	                selected = true;
	            }
	        }

	        if (!selected) {
	            detail::tree_backend().layout(fbw, fbh, dpi);
	            backend = &detail::tree_backend();
	        }

        if (click_pending && g_click_dispatcher) {
            click_pending = false;
            webcc::handle target;
            if (backend->hit_test(click_x, click_y, dpi, target) && target.is_valid()) {
                dispatch_click_bubble(target);
            }
        }

	        sg_pass_action pass{};
	        pass.colors[0].load_action = SG_LOADACTION_CLEAR;
	        pass.colors[0].clear_value = {0.08f, 0.08f, 0.10f, 1.0f};

	        if (render_swapchain) {
	        sg_pass p{};
	        p.action = pass;
	        p.swapchain = sglue_swapchain();
	        sg_begin_pass(&p);

        sgl_defaults();
        sgl_viewport(0, 0, sapp_width(), sapp_height(), true);
        sgl_matrix_mode_projection();
        sgl_load_identity();
        sgl_ortho(0.0f, (float)sapp_width(), (float)sapp_height(), 0.0f, -1.0f, 1.0f);
        sgl_matrix_mode_modelview();
        sgl_load_identity();
        backend->render((float)sapp_width(), (float)sapp_height(), dpi);

        const bool draw_overlay = overlay_enabled();
        if (draw_overlay) {
            sdtx_canvas((float)sapp_width(), (float)sapp_height());
            sdtx_font(0);
            sdtx_origin(1.0f, 1.0f);
            sdtx_home();
            sdtx_color3f(1.0f, 1.0f, 1.0f);
            sdtx_puts("COI native runtime (sokol)\n");
	        sdtx_printf("dt: %.3f\n\n", dt);
#if defined(COI_NATIVE_FONTSTASH)
#if defined(COI_NATIVE_CLAY)
	            sdtx_printf("fontstash: %s\n\n", detail::clay_backend().font_ok() ? "on" : "off");
#else
	            sdtx_printf("fontstash: off\n\n");
#endif
#endif
	            sdtx_printf("backend: %s\n\n", backend ? backend->name() : "unknown");

            const char* show_dump = std::getenv("COI_NATIVE_SHOW_DUMP");
            if (show_dump && *show_dump && std::string(show_dump) != std::string("0")) {
                sdtx_origin(1.0f, 6.0f);
                sdtx_home();
                sdtx_color3f(1.0f, 1.0f, 1.0f);
                sdtx_puts("\nUI tree (dump):\n");
                sdtx_dump_node(0, 0);
            }
            sdtx_draw();
        }

#if defined(COI_NATIVE_CAPTURE)
	        // In X11 capture mode, read back the swapchain framebuffer before ending the pass.
	        capture_maybe_swapchain(dt);
#endif
	        sg_end_pass();
	        }
#if defined(COI_NATIVE_CAPTURE)
	        // In offscreen capture mode, render to a separate target after ending the swapchain pass.
	        capture_maybe(*backend, pass, dt);
#endif
	        sg_commit();

        frames++;
        if (frames_limit > 0 && frames >= frames_limit) {
            sapp_request_quit();
        }
    }

	    static void cleanup(void) {
#if defined(COI_NATIVE_CAPTURE)
		        capture_shutdown();
#endif
#if defined(COI_NATIVE_CLAY)
		        // Backend resources (textures, font atlases, etc) must be destroyed before sg_shutdown().
		        detail::clay_backend().shutdown_gfx();
#endif
#if defined(COI_NATIVE_RMLUI)
		        detail::rmlui_backend().shutdown_gfx();
#endif
		        sdtx_shutdown();
		        sgl_shutdown();

		        sg_shutdown();
		    }

	    static int run(void* app_in, int frames_limit_in, tick_fn tick_in) {
	        app = app_in;
	        tick = tick_in;
	        frames_limit = frames_limit_in;
	        frames = 0;

	        const bool want_window = window_requested();

	        sapp_desc desc{};
	        int win_w = 960;
	        int win_h = 540;
	        desc.window_title = "COI (Desktop)";

#if defined(COI_NATIVE_CAPTURE)
	        const char* dir = std::getenv("COI_NATIVE_CAPTURE_DIR");
	        const bool capture_requested = (dir && *dir);
	        CaptureMode mode = CaptureMode::Offscreen;
	        if (capture_requested) {
#if defined(__linux__) || defined(__unix__)
	            mode = want_window ? CaptureMode::X11 : CaptureMode::Offscreen;
#endif
	            if (const char* m = std::getenv("COI_NATIVE_CAPTURE_MODE")) {
	                if (m && *m) {
	                    const std::string mm(m);
	                    if (mm == "x11") mode = CaptureMode::X11;
	                    if (mm == "offscreen") mode = CaptureMode::Offscreen;
	                }
	            }
	        }

	        if (!want_window && capture_requested && mode == CaptureMode::Offscreen) {
	            // Capture-only offscreen mode: no need to render the swapchain at all.
	            // Keep the context window tiny and hide it in init().
	            render_swapchain = false;
	            win_w = 32;
	            win_h = 32;
	            desc.window_title = "COI (Native, capture)";
	        } else {
	            render_swapchain = true;
	        }

	        // If X11 capture is selected and a size is provided, prefer it as the window size
	        // so that X11-based capture is deterministic.
	        if (capture_requested && mode == CaptureMode::X11) {
	            const char* size = std::getenv("COI_NATIVE_CAPTURE_SIZE");
	            int cw = 0, ch = 0;
	            if (parse_wh(size, cw, ch)) {
	                win_w = cw;
	                win_h = ch;
	            }
	        }
#endif
	        desc.width = win_w;
	        desc.height = win_h;
	        desc.init_cb = init;
	        desc.frame_cb = frame_cb;
	        desc.event_cb = event_cb;
	        desc.cleanup_cb = cleanup;
	        sapp_run(&desc);
#if defined(COI_NATIVE_CAPTURE)
        return exit_code;
#else
        return 0;
#endif
    }

};

#endif // defined(COI_NATIVE_SOKOL)

int run_sokol(void* app, int frames, tick_fn tick) {
#if defined(COI_NATIVE_SOKOL)
    return SokolRunnerImpl::run(app, frames, tick);
#else
    (void)app;
    (void)frames;
    (void)tick;
    std::cerr << "COI native runtime built without Sokol support\n";
    return 1;
#endif
}
