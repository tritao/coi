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

#include "runtime/deps_extra.h"
