#pragma once

#if defined(COI_NATIVE_CLAY)
#include "runtime/deps_clay.h"
#include "webcc/core/handle.h"

namespace coi::native {

struct ClayEngine {
    static Clay_Context* ctx();

    static uint32_t id_from_handle(int32_t h);
    static Clay_ElementId element_id(int32_t h);

    static void ensure(float w, float h);
    static void set_input(float x, float y, bool down, float scroll_x, float scroll_y, float dt);
    static Clay_RenderCommandArray layout(float w_px, float h_px, float dpi_scale = 1.0f);
    static void shutdown();
};

} // namespace coi::native

#endif // COI_NATIVE_CLAY
