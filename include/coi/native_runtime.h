#pragma once

#include "coi/native/runtime_api.h"

namespace coi::native {
#if defined(COI_NATIVE_SOKOL)
// Compatibility wrapper: generated code uses `coi::native::SokolRunner<App>::run(app, frames)`.
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
} // namespace coi::native

