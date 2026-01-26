#pragma once

namespace coi::native {
// Used to avoid running native scripts/simulated input in the "pre-run" flush that
// happens before sapp_run() starts when window/capture is enabled.
extern bool g_sokol_frame_started;
} // namespace coi::native

