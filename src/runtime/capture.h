#pragma once

#include "runtime/prelude.h"

namespace coi::native {

struct CaptureStartup {
    bool enabled = false;
    bool render_swapchain = true;
    int win_w = 960;
    int win_h = 540;
    const char* window_title = nullptr;
};

// Parse capture-related env vars and decide how the sokol app should be started
// (windowed vs capture-only offscreen, window size, etc).
CaptureStartup capture_startup(bool want_window, int default_win_w, int default_win_h);

// Called after sg/sgl/sdtx are initialized.
void capture_runtime_init();

// Called before sg_shutdown().
void capture_runtime_shutdown();

bool capture_is_enabled();
bool capture_render_swapchain();

// In X11 capture mode, read back the swapchain before sg_end_pass().
void capture_maybe_swapchain(int frame_index);

// In offscreen capture mode, render to an offscreen target after ending the swapchain pass.
class UiBackend;
void capture_maybe_offscreen(UiBackend& backend, const sg_pass_action& action, int frame_index);

int capture_exit_code();

} // namespace coi::native

