#pragma once

// Sokol + optional Clay/Fontstash/STB dependencies.
// This header must be included by any TU that uses the native Sokol runtime types.
//
// IMPORTANT: Do NOT define implementation macros here (SOKOL_IMPL, CLAY_IMPLEMENTATION, stb impls, ...).
// Those are compiled exactly once in `src/runtime/third_party/impl.cc`.

#ifndef COI_NATIVE_RUNTIME_SOKOL_INCLUDED
#define COI_NATIVE_RUNTIME_SOKOL_INCLUDED (1)
#endif

#ifndef SOKOL_NO_ENTRY
#define SOKOL_NO_ENTRY
#endif

// Default to GLCORE on desktop platforms.
#if !defined(SOKOL_GLCORE) && (defined(__linux__) || defined(__unix__) || defined(__APPLE__) || defined(_WIN32))
#define SOKOL_GLCORE
#endif

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_gl.h"
#include "sokol_glue.h"
#include "sokol_time.h"
#include "sokol_debugtext.h"

#if defined(COI_NATIVE_FONTSTASH)
#include "fontstash.h"
#include "sokol_fontstash.h"
#endif

// Image loading is used by native demo scenes (img tag) when Clay is enabled.
#ifndef COI_NATIVE_RUNTIME_STB_IMAGE_INCLUDED
#define COI_NATIVE_RUNTIME_STB_IMAGE_INCLUDED (1)
#endif
#include "stb_image.h"

#if defined(COI_NATIVE_CAPTURE)
#include "stb_image_write.h"
#endif

#if defined(COI_NATIVE_CLAY)
#ifndef COI_NATIVE_RUNTIME_CLAY_INCLUDED
#define COI_NATIVE_RUNTIME_CLAY_INCLUDED (1)
#endif
#include "clay.h"
#endif

#if defined(COI_NATIVE_CLAY) && defined(COI_NATIVE_FONTSTASH)
// Used by the runtime to toggle Clay's Sokol renderer path.
#ifndef COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED
#define COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED (1)
#endif
// Clay's reference sokol renderer + fontstash-based text measurement.
// (No impl macro here; compiled in third_party/impl.cc.)
#include "renderers/sokol/sokol_clay.h"
#endif
