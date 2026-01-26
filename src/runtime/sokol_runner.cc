#include "coi/native/runtime_api.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

#include "runtime/state.h"

#include "runtime/backends/registry.h"
#include "runtime/capture.h"
#include "runtime/debug_text.h"
#include "runtime/deps_sokol.h"
#include "runtime/util.h"

#if defined(__linux__) || defined(__unix__)
#include <X11/Xlib.h>
#endif

namespace coi::native {

#if defined(COI_NATIVE_SOKOL)

bool g_sokol_frame_started = false;

webcc::function<bool(webcc::handle)> g_click_dispatcher;
void set_click_dispatcher(webcc::function<bool(webcc::handle)> cb) { g_click_dispatcher = std::move(cb); }

static inline std::array<InputEvent, 64> g_injected_events{};
static inline uint32_t g_injected_event_count = 0;

static inline bool g_injected_mouse_pos_valid = false;
static inline float g_injected_mouse_x = 0.0f;
static inline float g_injected_mouse_y = 0.0f;
static inline bool g_injected_mouse_down_valid = false;
static inline bool g_injected_mouse_down = false;

static inline void inject_event(InputEventType type, int key_code, uint32_t char_code, uint32_t modifiers, bool repeat) {
    if (g_injected_event_count >= g_injected_events.size()) return;
    InputEvent& e = g_injected_events[g_injected_event_count++];
    e.type = type;
    e.key_code = key_code;
    e.char_code = char_code;
    e.modifiers = modifiers;
    e.repeat = repeat;
}

void inject_key_down(int key_code, uint32_t modifiers, bool repeat) { inject_event(InputEventType::KeyDown, key_code, 0, modifiers, repeat); }
void inject_key_up(int key_code, uint32_t modifiers) { inject_event(InputEventType::KeyUp, key_code, 0, modifiers, false); }
void inject_char(uint32_t char_code, uint32_t modifiers, bool repeat) { inject_event(InputEventType::Char, 0, char_code, modifiers, repeat); }

void inject_mouse_move(float x, float y) {
    g_injected_mouse_pos_valid = true;
    g_injected_mouse_x = x;
    g_injected_mouse_y = y;
}

void inject_mouse_down(float x, float y) {
    inject_mouse_move(x, y);
    g_injected_mouse_down_valid = true;
    g_injected_mouse_down = true;
}

void inject_mouse_up(float x, float y) {
    inject_mouse_move(x, y);
    g_injected_mouse_down_valid = true;
    g_injected_mouse_down = false;
}

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
        if (capture_is_enabled()) return false;
#endif
        return true;
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

        const float dpi = (sapp_dpi_scale() > 0.0f) ? sapp_dpi_scale() : 1.0f;
        init_sokol_backends((float)sapp_width(), (float)sapp_height(), dpi);

#if defined(COI_NATIVE_CAPTURE)
        capture_runtime_init();
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

    static inline float mouse_x = 0.0f;
    static inline float mouse_y = 0.0f;
    static inline bool mouse_down = false;
    static inline float scroll_x = 0.0f;
    static inline float scroll_y = 0.0f;
    static inline bool click_pending = false;
    static inline float click_x = 0.0f;
    static inline float click_y = 0.0f;

    static inline std::array<InputEvent, 64> pending_events{};
    static inline uint32_t pending_event_count = 0;

    static void push_event(InputEventType type, const sapp_event* ev) {
        if (!ev) return;
        if (pending_event_count >= pending_events.size()) return;
        InputEvent& e = pending_events[pending_event_count++];
        e.type = type;
        e.key_code = (int)ev->key_code;
        e.char_code = ev->char_code;
        e.modifiers = ev->modifiers;
        e.repeat = ev->key_repeat;
    }

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
        case SAPP_EVENTTYPE_KEY_DOWN:
            push_event(InputEventType::KeyDown, ev);
            break;
        case SAPP_EVENTTYPE_KEY_UP:
            push_event(InputEventType::KeyUp, ev);
            break;
        case SAPP_EVENTTYPE_CHAR:
            push_event(InputEventType::Char, ev);
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
        InputState input;
        if (g_injected_mouse_pos_valid) {
            mouse_x = g_injected_mouse_x;
            mouse_y = g_injected_mouse_y;
            g_injected_mouse_pos_valid = false;
        }
        if (g_injected_mouse_down_valid) {
            mouse_down = g_injected_mouse_down;
            g_injected_mouse_down_valid = false;
        }
        input.mouse_x = mouse_x;
        input.mouse_y = mouse_y;
        input.mouse_down = mouse_down;
        input.scroll_x = scroll_x;
        input.scroll_y = scroll_y;
        scroll_x = 0.0f;
        scroll_y = 0.0f;

        uint32_t out_event_count = 0;
        for (uint32_t i = 0; i < pending_event_count && out_event_count < input.events.size(); i++) input.events[out_event_count++] = pending_events[i];
        for (uint32_t i = 0; i < g_injected_event_count && out_event_count < input.events.size(); i++) input.events[out_event_count++] = g_injected_events[i];
        input.event_count = out_event_count;
        pending_event_count = 0;
        g_injected_event_count = 0;

        const float fbw = (float)sapp_width();
        const float fbh = (float)sapp_height();
        UiBackend& backend_ref = select_backend(input, (float)dt, dpi, fbw, fbh);
        UiBackend* backend = &backend_ref;

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
            capture_maybe_swapchain(frames);
#endif
            sg_end_pass();
        }

#if defined(COI_NATIVE_CAPTURE)
        // In offscreen capture mode, render to a separate target after ending the swapchain pass.
        capture_maybe_offscreen(*backend, pass, frames);
#endif
        sg_commit();

        frames++;
        if (frames_limit > 0 && frames >= frames_limit) {
            sapp_request_quit();
        }
    }

    static void cleanup(void) {
#if defined(COI_NATIVE_CAPTURE)
        capture_runtime_shutdown();
#endif
        // Backend resources (textures, font atlases, etc) must be destroyed before sg_shutdown().
        shutdown_sokol_backends();
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
        const CaptureStartup startup = capture_startup(want_window, win_w, win_h);
        if (startup.enabled) {
            render_swapchain = startup.render_swapchain;
            win_w = startup.win_w;
            win_h = startup.win_h;
            if (startup.window_title) desc.window_title = startup.window_title;
        } else {
            render_swapchain = true;
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
        return capture_exit_code();
#else
        return 0;
#endif
    }
};

#else // defined(COI_NATIVE_SOKOL)

void inject_mouse_move(float, float) {}
void inject_mouse_down(float, float) {}
void inject_mouse_up(float, float) {}
void inject_key_down(int, uint32_t, bool) {}
void inject_key_up(int, uint32_t) {}
void inject_char(uint32_t, uint32_t, bool) {}

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

} // namespace coi::native
