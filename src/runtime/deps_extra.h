#pragma once

// Optional third-party deps for the native runtime.
//
// IMPORTANT: Do NOT define implementation macros here (SOKOL_IMPL, stb impls, ...).
// Those are compiled exactly once in `src/runtime/vendor_impl.cc`.

#include "runtime/deps_sokol.h"

// Image loading (for <img> nodes / RmlUI textures) + optional PNG writing for capture.
#include "stb_image.h"
#if defined(COI_NATIVE_CAPTURE)
#include "stb_image_write.h"
#endif

// Fontstash (native text rendering).
#if defined(COI_NATIVE_FONTSTASH)
#include "fontstash.h"
#include "sokol_fontstash.h"
#endif

// (Additional layout backends are added in follow-up commits.)
