// Native runtime implementation for the Sokol backend.
// This TU intentionally owns the bulk of the runtime logic (Clay/RmlUI backends,
// capture, debug tools) but depends on `third_party/impl.cc` for header-only impls.

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

// Retained UI tree declarations (implementation lives in core/ui_tree.cc).
#include "coi/native/impl/ui_tree.h"

#if defined(COI_NATIVE_SOKOL)

#include "coi/native/internal/state.h"
#include "coi/native/internal/clay_internal.h"
#include "coi/native/third_party/deps.h"

#if defined(COI_NATIVE_RMLUI)
#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>
#endif

namespace coi::native {

bool g_sokol_frame_started = false;

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
