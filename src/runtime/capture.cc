#include "runtime/capture.h"

#include "runtime/backends/backend.h"
#include "runtime/deps_stb.h"

#if defined(__linux__) || defined(__unix__)
#include <GL/gl.h>
#endif

namespace coi::native {

#if defined(COI_NATIVE_CAPTURE)

static bool g_capture_enabled = false;
enum class CaptureMode { Offscreen, X11 };
static CaptureMode g_capture_mode = CaptureMode::Offscreen;
static int g_capture_every = 60;
static int g_capture_max = -1;
static int g_capture_index = 0;
static int g_capture_tolerance = 8; // max Hamming distance for dHash
static bool g_capture_overlay = false;
static bool g_capture_fail_on_mismatch = false;
static bool g_capture_debug = false;
static std::filesystem::path g_capture_dir;
static std::filesystem::path g_capture_baseline_dir;
static int g_capture_w = 0;
static int g_capture_h = 0;
static int g_exit_code = 0;

static sg_image g_capture_img{};
static sg_image g_capture_ds_img{};
static sg_view g_capture_view{};
static sg_view g_capture_ds_view{};
static std::vector<uint8_t> g_capture_pixels;
static std::vector<uint8_t> g_capture_pixels_flipped;

static bool parse_wh(const char* s, int& w, int& h) {
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

static uint64_t dhash_rgba8(const uint8_t* rgba, int w, int h) {
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
            const uint8_t a = g[y * 9 + x];
            const uint8_t b = g[y * 9 + x + 1];
            if (a > b) out |= (1ull << bit);
            bit++;
        }
    }
    return out;
}

static int popcount64(uint64_t v) {
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

static bool read_hex_u64(const std::filesystem::path& p, uint64_t& out) {
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

static void capture_shutdown_images() {
    if (g_capture_ds_view.id != SG_INVALID_ID) {
        sg_destroy_view(g_capture_ds_view);
        g_capture_ds_view.id = SG_INVALID_ID;
    }
    if (g_capture_ds_img.id != SG_INVALID_ID) {
        sg_destroy_image(g_capture_ds_img);
        g_capture_ds_img.id = SG_INVALID_ID;
    }
    if (g_capture_view.id != SG_INVALID_ID) {
        sg_destroy_view(g_capture_view);
        g_capture_view.id = SG_INVALID_ID;
    }
    if (g_capture_img.id != SG_INVALID_ID) {
        sg_destroy_image(g_capture_img);
        g_capture_img.id = SG_INVALID_ID;
    }
    g_capture_pixels.clear();
    g_capture_pixels_flipped.clear();
}

static void capture_ensure_target() {
    if (!g_capture_enabled) return;
    if (g_capture_mode != CaptureMode::Offscreen) return;
    if (g_capture_w <= 0) g_capture_w = 1;
    if (g_capture_h <= 0) g_capture_h = 1;

    if (g_capture_img.id != SG_INVALID_ID) {
        const sg_image_desc d = sg_query_image_desc(g_capture_img);
        if ((int)d.width == g_capture_w && (int)d.height == g_capture_h) return;
        capture_shutdown_images();
    }

    if (g_capture_debug) {
        std::cerr << "[capture] ensure_target " << g_capture_w << "x" << g_capture_h << std::endl;
    }

    const sg_swapchain sc = sglue_swapchain();

    sg_image_desc img_desc{};
    img_desc.type = SG_IMAGETYPE_2D;
    img_desc.width = g_capture_w;
    img_desc.height = g_capture_h;
    img_desc.num_mipmaps = 1;
    img_desc.sample_count = (sc.sample_count > 0) ? sc.sample_count : 1;
    img_desc.pixel_format = (sc.color_format != SG_PIXELFORMAT_NONE) ? sc.color_format : SG_PIXELFORMAT_RGBA8;
    img_desc.usage.color_attachment = true;
    img_desc.label = "coi-capture-color";
    g_capture_img = sg_make_image(&img_desc);
    if (g_capture_img.id == SG_INVALID_ID) {
        std::cerr << "[capture] failed to create capture image; disabling capture\n";
        g_capture_enabled = false;
        return;
    }

    sg_view_desc view_desc{};
    view_desc.color_attachment.image = g_capture_img;
    view_desc.label = "coi-capture-view";
    g_capture_view = sg_make_view(&view_desc);
    if (g_capture_view.id == SG_INVALID_ID) {
        std::cerr << "[capture] failed to create capture view; disabling capture\n";
        g_capture_enabled = false;
        capture_shutdown_images();
        return;
    }

    sg_image_desc ds_desc{};
    ds_desc.type = SG_IMAGETYPE_2D;
    ds_desc.width = g_capture_w;
    ds_desc.height = g_capture_h;
    ds_desc.num_mipmaps = 1;
    ds_desc.sample_count = img_desc.sample_count;
    ds_desc.pixel_format = (sc.depth_format != SG_PIXELFORMAT_NONE) ? sc.depth_format : SG_PIXELFORMAT_DEPTH_STENCIL;
    ds_desc.usage.depth_stencil_attachment = true;
    ds_desc.label = "coi-capture-depth";
    g_capture_ds_img = sg_make_image(&ds_desc);
    if (g_capture_ds_img.id != SG_INVALID_ID) {
        sg_view_desc ds_view_desc{};
        ds_view_desc.depth_stencil_attachment.image = g_capture_ds_img;
        ds_view_desc.label = "coi-capture-depth-view";
        g_capture_ds_view = sg_make_view(&ds_view_desc);
        if (g_capture_ds_view.id == SG_INVALID_ID) {
            sg_destroy_image(g_capture_ds_img);
            g_capture_ds_img.id = SG_INVALID_ID;
        }
    }

    g_capture_pixels.resize((size_t)g_capture_w * (size_t)g_capture_h * 4u);
    g_capture_pixels_flipped.resize(g_capture_pixels.size());
}

static void capture_write_outputs(const uint8_t* rgba_topdown, int w, int h, int stride_bytes) {
    if (!rgba_topdown || w <= 0 || h <= 0) return;
    char name[64];
    std::snprintf(name, sizeof(name), "frame_%06d.png", g_capture_index);
    const std::filesystem::path png_path = g_capture_dir / name;
    const int ok_png = stbi_write_png(png_path.string().c_str(), w, h, 4, rgba_topdown, stride_bytes);
    if (!ok_png) {
        std::cerr << "[capture] failed to write png: " << png_path.string() << "\n";
    }

    const uint64_t hsh = dhash_rgba8(rgba_topdown, w, h);
    std::snprintf(name, sizeof(name), "frame_%06d.dhash", g_capture_index);
    const std::filesystem::path hash_path = g_capture_dir / name;
    {
        std::ofstream hf(hash_path);
        hf << std::hex << hsh << "\n";
    }

    if (!g_capture_baseline_dir.empty()) {
        const std::filesystem::path base_hash = g_capture_baseline_dir / hash_path.filename();
        uint64_t base = 0;
        if (read_hex_u64(base_hash, base)) {
            const int dist = popcount64(hsh ^ base);
            std::cout << "[capture] " << hash_path.filename().string() << " dhash=" << std::hex << hsh << std::dec << " baseline_dist=" << dist
                      << " tol=" << g_capture_tolerance << "\n";
            if (dist > g_capture_tolerance) {
                g_exit_code = 1;
                if (g_capture_fail_on_mismatch) sapp_request_quit();
            }
        } else {
            std::cout << "[capture] " << hash_path.filename().string() << " dhash=" << std::hex << hsh << std::dec << " (no baseline)\n";
        }
    } else {
        std::cout << "[capture] " << hash_path.filename().string() << " dhash=" << std::hex << hsh << std::dec << "\n";
    }

    g_capture_index++;
}

CaptureStartup capture_startup(bool want_window, int default_win_w, int default_win_h) {
    CaptureStartup out{};
    out.enabled = false;
    out.render_swapchain = true;
    out.win_w = default_win_w;
    out.win_h = default_win_h;
    out.window_title = nullptr;

    const char* dir = std::getenv("COI_NATIVE_CAPTURE_DIR");
    if (!dir || !*dir) return out;

    out.enabled = true;

    CaptureMode mode = CaptureMode::Offscreen;
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

    if (!want_window && mode == CaptureMode::Offscreen) {
        out.render_swapchain = false;
        out.win_w = 32;
        out.win_h = 32;
        out.window_title = "COI (Native, capture)";
        return out;
    }

    out.render_swapchain = true;
    if (mode == CaptureMode::X11) {
        const char* size = std::getenv("COI_NATIVE_CAPTURE_SIZE");
        int cw = 0, ch = 0;
        if (parse_wh(size, cw, ch)) {
            out.win_w = cw;
            out.win_h = ch;
        }
    }
    return out;
}

void capture_runtime_init() {
    const char* dir = std::getenv("COI_NATIVE_CAPTURE_DIR");
    if (!dir || !*dir) return;

    g_capture_enabled = true;
    g_capture_dir = std::filesystem::path(dir);

    if (const char* e = std::getenv("COI_NATIVE_CAPTURE_DEBUG")) {
        g_capture_debug = (std::string(e) != "0");
    }

    // Default capture mode:
    // - windowed runs: prefer X11 on Linux (robust across drivers)
    // - capture-only (no COI_NATIVE_WINDOW): prefer offscreen
    // Offscreen/X11 can be forced via COI_NATIVE_CAPTURE_MODE.
    const char* win = std::getenv("COI_NATIVE_WINDOW");
    const bool want_window = (win && *win && !(win[0] == '0' && win[1] == '\0'));

#if defined(__linux__) || defined(__unix__)
    g_capture_mode = want_window ? CaptureMode::X11 : CaptureMode::Offscreen;
#else
    g_capture_mode = CaptureMode::Offscreen;
#endif
    if (const char* m = std::getenv("COI_NATIVE_CAPTURE_MODE")) {
        if (m && *m) {
            const std::string mm(m);
            if (mm == "x11") g_capture_mode = CaptureMode::X11;
            if (mm == "offscreen") g_capture_mode = CaptureMode::Offscreen;
        }
    }

    g_capture_w = sapp_width();
    g_capture_h = sapp_height();
    (void)parse_wh(std::getenv("COI_NATIVE_CAPTURE_SIZE"), g_capture_w, g_capture_h);

    if (const char* e = std::getenv("COI_NATIVE_CAPTURE_EVERY")) {
        g_capture_every = std::atoi(e);
        if (g_capture_every <= 0) g_capture_every = 1;
    }
    if (const char* e = std::getenv("COI_NATIVE_CAPTURE_MAX")) {
        g_capture_max = std::atoi(e);
    }
    if (const char* e = std::getenv("COI_NATIVE_CAPTURE_TOLERANCE")) {
        g_capture_tolerance = std::atoi(e);
        if (g_capture_tolerance < 0) g_capture_tolerance = 0;
    }
    if (const char* e = std::getenv("COI_NATIVE_CAPTURE_OVERLAY")) {
        g_capture_overlay = (std::string(e) != "0");
    }
    if (const char* e = std::getenv("COI_NATIVE_CAPTURE_FAIL_ON_MISMATCH")) {
        g_capture_fail_on_mismatch = (std::string(e) != "0");
    }
    if (const char* base = std::getenv("COI_NATIVE_CAPTURE_BASELINE")) {
        if (*base) g_capture_baseline_dir = std::filesystem::path(base);
    }

    std::error_code ec;
    std::filesystem::create_directories(g_capture_dir, ec);

    g_capture_img = sg_image{};
    g_capture_ds_img = sg_image{};
    g_capture_view = sg_view{};
    g_capture_ds_view = sg_view{};
    g_capture_pixels.clear();
    g_capture_pixels_flipped.clear();
    g_capture_index = 0;
    g_exit_code = 0;

    if (g_capture_debug) {
        std::cerr << "[capture] enabled dir=" << g_capture_dir.string() << " size=" << g_capture_w << "x" << g_capture_h
                  << " mode=" << (g_capture_mode == CaptureMode::Offscreen ? "offscreen" : "x11") << " every=" << g_capture_every
                  << " max=" << g_capture_max << " baseline=" << g_capture_baseline_dir.string() << " tol=" << g_capture_tolerance
                  << " overlay=" << (g_capture_overlay ? 1 : 0) << " fail=" << (g_capture_fail_on_mismatch ? 1 : 0) << std::endl;
    }
}

void capture_runtime_shutdown() {
    if (!g_capture_enabled) return;
    capture_shutdown_images();
}

bool capture_is_enabled() { return g_capture_enabled; }
bool capture_render_swapchain() { return g_capture_enabled ? (g_capture_mode == CaptureMode::X11 ? true : false) : true; }

void capture_maybe_swapchain(int frame_index) {
    (void)frame_index;
    if (!g_capture_enabled) return;
    if (g_capture_mode != CaptureMode::X11) return;
    if (g_capture_max >= 0 && g_capture_index >= g_capture_max) return;
    if (g_capture_every > 1 && (frame_index % g_capture_every) != 0) return;

    int w = g_capture_w > 0 ? g_capture_w : sapp_width();
    int h = g_capture_h > 0 ? g_capture_h : sapp_height();
    const int sw = sapp_width();
    const int sh = sapp_height();
    if (w > sw) w = sw;
    if (h > sh) h = sh;
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;

    g_capture_pixels.resize((size_t)w * (size_t)h * 4u);
    g_capture_pixels_flipped.resize(g_capture_pixels.size());

#if defined(__linux__) || defined(__unix__)
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, g_capture_pixels.data());
#endif

    const size_t stride = (size_t)w * 4u;
    for (int y = 0; y < h; y++) {
        const uint8_t* src = g_capture_pixels.data() + (size_t)(h - 1 - y) * stride;
        uint8_t* dst = g_capture_pixels_flipped.data() + (size_t)y * stride;
        std::memcpy(dst, src, stride);
    }

    capture_write_outputs(g_capture_pixels_flipped.data(), w, h, (int)stride);
}

void capture_maybe_offscreen(UiBackend& backend, const sg_pass_action& action, int frame_index) {
    (void)backend;
    (void)action;
    (void)frame_index;
    if (!g_capture_enabled) return;
    if (g_capture_mode != CaptureMode::Offscreen) return;
    if (g_capture_max >= 0 && g_capture_index >= g_capture_max) return;
    if (g_capture_every > 1 && (frame_index % g_capture_every) != 0) return;

    capture_ensure_target();
    if (g_capture_view.id == SG_INVALID_ID) return;

    sg_pass cp{};
    cp.action = action;
    cp.attachments.colors[0] = g_capture_view;
    if (g_capture_ds_view.id != SG_INVALID_ID) {
        cp.attachments.depth_stencil = g_capture_ds_view;
    }
    sg_begin_pass(&cp);

    sgl_defaults();
    sgl_viewport(0, 0, g_capture_w, g_capture_h, true);
    sgl_matrix_mode_projection();
    sgl_load_identity();
    sgl_ortho(0.0f, (float)g_capture_w, (float)g_capture_h, 0.0f, -1.0f, 1.0f);
    sgl_matrix_mode_modelview();
    sgl_load_identity();

    backend.layout((float)g_capture_w, (float)g_capture_h, 1.0f);
    backend.render((float)g_capture_w, (float)g_capture_h, 1.0f);

    if (g_capture_overlay) {
        sdtx_canvas((float)g_capture_w, (float)g_capture_h);
        sdtx_font(0);
        sdtx_origin(1.0f, 1.0f);
        sdtx_home();
        sdtx_color3f(1.0f, 1.0f, 1.0f);
        sdtx_puts("CAPTURE\n");
        sdtx_draw();
    }

#if defined(__linux__) || defined(__unix__)
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, g_capture_w, g_capture_h, GL_RGBA, GL_UNSIGNED_BYTE, g_capture_pixels.data());
#endif
    sg_end_pass();

    const size_t stride = (size_t)g_capture_w * 4u;
    for (int y = 0; y < g_capture_h; y++) {
        const uint8_t* src = g_capture_pixels.data() + (size_t)(g_capture_h - 1 - y) * stride;
        uint8_t* dst = g_capture_pixels_flipped.data() + (size_t)y * stride;
        std::memcpy(dst, src, stride);
    }

    capture_write_outputs(g_capture_pixels_flipped.data(), g_capture_w, g_capture_h, (int)stride);
}

int capture_exit_code() { return g_exit_code; }

#else // !defined(COI_NATIVE_CAPTURE)

CaptureStartup capture_startup(bool, int default_win_w, int default_win_h) {
    CaptureStartup out{};
    out.enabled = false;
    out.render_swapchain = true;
    out.win_w = default_win_w;
    out.win_h = default_win_h;
    out.window_title = nullptr;
    return out;
}

void capture_runtime_init() {}
void capture_runtime_shutdown() {}
bool capture_is_enabled() { return false; }
bool capture_render_swapchain() { return true; }
void capture_maybe_swapchain(int) {}
void capture_maybe_offscreen(UiBackend&, const sg_pass_action&, int) {}
int capture_exit_code() { return 0; }

#endif

} // namespace coi::native
