// Native runtime implementation (single TU).
// Built as a normal project source (not via .inc includes in public headers).
//
// This file provides:
// - A retained-tree "UI" in namespace `coi::ui` (used by codegen).
// - A Sokol-based renderer/event loop under COI_NATIVE_SOKOL.
//
// NOTE: This is intentionally a single translation unit today to keep integration simple
// (and to ensure 3rd-party header-impl sections are compiled exactly once).

#include "coi/native/runtime_api.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#if defined(_WIN32)
#include <windows.h>
#endif

#include "webcc/core/allocator.h"
#include "webcc/core/array.h"
#include "webcc/core/function.h"
#include "webcc/core/handle.h"
#include "webcc/core/new.h"
#include "webcc/core/random.h"
#include "webcc/core/string.h"
#include "webcc/core/string_view.h"
#include "webcc/core/vector.h"

// Shared class-token parsing + CSS emitters used by multiple native UI backends.
#include "coi/style/css.h"

// Retained UI tree and DOM-like operations.
#include "coi/native/impl/ui_tree.h"

#if defined(COI_NATIVE_SOKOL)
#if defined(COI_NATIVE_RMLUI)
#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>
#endif

// 3rd-party implementations + Sokol/Clay integration (compiled once in this TU).
#include "coi/native/impl/sokol_impl.h"

namespace coi::native {
struct Rect {
    float x, y, w, h;
};

// Used to avoid running desktop scripts/simulated input in the "pre-run" flush
// that happens before sapp_run() starts when window/capture is enabled.
inline bool g_sokol_frame_started = false;

#include "coi/native/impl/util.h"
#include "coi/native/impl/style_class_parser.h"
#include "coi/native/impl/clay_engine.h"
#include "coi/native/impl/debug_text.h"
#include "coi/native/impl/ui_backend.h"
#include "coi/native/impl/rmlui_backend.h"
#include "coi/native/impl/sokol_runner.h"
#include "coi/native/impl/debug_tools.h"

} // namespace coi::native
#endif // COI_NATIVE_SOKOL

