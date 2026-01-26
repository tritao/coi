#pragma once

// Clay's reference Sokol renderer + fontstash text helpers.
// (No implementation macro here; see vendor_impl.cc.)

#if defined(COI_NATIVE_CLAY) && defined(COI_NATIVE_FONTSTASH)
#include "runtime/deps_clay.h"
#ifndef COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED
#define COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED (1)
#endif
#include "renderers/sokol/sokol_clay.h"
#endif
