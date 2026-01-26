// Third-party single-implementation TU for the native runtime.
// Keeps heavy header-only libraries from being compiled into every runtime module.

#if defined(COI_NATIVE_SOKOL)
#define SOKOL_IMPL
#define SOKOL_GL_IMPL
#define SOKOL_DEBUGTEXT_IMPL
#endif

#if defined(COI_NATIVE_FONTSTASH)
#define FONTSTASH_IMPLEMENTATION
#endif

// stb_image is used for loading images for <img> nodes.
#define STB_IMAGE_IMPLEMENTATION

#if defined(COI_NATIVE_CAPTURE)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#endif

#if defined(COI_NATIVE_CLAY)
#define CLAY_IMPLEMENTATION
#endif

#if defined(COI_NATIVE_CLAY) && defined(COI_NATIVE_FONTSTASH)
#define SOKOL_CLAY_IMPL
#endif

#include "coi/native/third_party/deps.h"

#if defined(COI_NATIVE_CLAY)
#include "coi/native/internal/clay_internal.h"

Clay_Vector2 coi_native_clay_scroll_offset_for_open_element(void) {
    Clay_Context* c = Clay_GetCurrentContext();
    Clay_LayoutElement* open = Clay__GetOpenLayoutElement();
    Clay_Vector2 off{0, 0};
    if (!c || !open) return off;
    for (int32_t i = 0; i < c->scrollContainerDatas.length; i++) {
        Clay__ScrollContainerDataInternal* mapping = Clay__ScrollContainerDataInternalArray_Get(&c->scrollContainerDatas, i);
        if (mapping && mapping->elementId == open->id) {
            off = mapping->scrollPosition;
            break;
        }
    }
    return off;
}
#endif
