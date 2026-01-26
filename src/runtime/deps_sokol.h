#pragma once

// Core Sokol dependencies for the native runtime.
//
// IMPORTANT: Do NOT define implementation macros here (SOKOL_IMPL, stb impls, ...).
// Those are compiled exactly once in `src/runtime/vendor_impl.cc`.

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
