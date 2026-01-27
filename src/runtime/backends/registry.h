#pragma once

#include "runtime/backends/backend.h"

namespace coi::native {

enum class BackendPref { Auto, Tree, RmlUi };

BackendPref backend_pref_from_env();
const char* backend_pref_name(BackendPref pref);

// Called from the Sokol init callback (after sg/sgl/sdtx are ready).
void init_sokol_backends(float fbw, float fbh, float dpi);

// Selects and layouts the backend for this frame.
UiBackend& select_backend(const InputState& input, float dt, float dpi, float fbw, float fbh);

// Called from the Sokol cleanup callback (before sg_shutdown()).
void shutdown_sokol_backends();

} // namespace coi::native
