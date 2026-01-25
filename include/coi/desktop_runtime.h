#pragma once

#include "coi/desktop/runtime_api.h"

namespace coi::desktop {
#if defined(COI_DESKTOP_SOKOL)
// Compatibility wrapper: generated code uses `coi::desktop::SokolRunner<App>::run(app, frames)`.
template <typename AppT>
struct SokolRunner {
    static int run(AppT* app, int frames) {
        tick_fn tick = nullptr;
        if constexpr (requires(AppT* a, double d) { a->tick(d); }) {
            tick = [](void* p, double dt) { static_cast<AppT*>(p)->tick(dt); };
        }
        return run_sokol(static_cast<void*>(app), frames, tick);
    }
};
#endif
} // namespace coi::desktop

