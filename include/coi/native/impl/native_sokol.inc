// Native runtime implementation (single TU). Included by `include/coi/native/runtime.cc`.
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

#include "webcc/core/handle.h"
#include "webcc/core/string_view.h"
#include "webcc/core/string.h"
#include "webcc/core/function.h"
#include "webcc/core/allocator.h"
#include "webcc/core/new.h"
#include "webcc/core/array.h"
#include "webcc/core/vector.h"
#include "webcc/core/random.h"

// Desktop backend runtime for Coi.
// - Provides a retained-tree "UI" in namespace coi::ui (used by codegen).
// - Optionally provides a Sokol window renderer under COI_NATIVE_SOKOL.

#include "coi/native/impl/ui_tree.inc"

#if defined(COI_NATIVE_SOKOL)
#include "coi/native/impl/sokol_impl.inc"

namespace coi::native {
struct Rect {
    float x, y, w, h;
};

// Used to avoid running desktop scripts/simulated input in the "pre-run" flush
// that happens before sapp_run() starts when window/capture is enabled.
inline bool g_sokol_frame_started = false;

    #include "coi/native/impl/util.inc"
    #include "coi/native/impl/style_class_parser.inc"
    #include "coi/native/impl/clay_engine.inc"
    #include "coi/native/impl/debug_text.inc"
    #include "coi/native/impl/ui_backend.inc"
    #include "coi/native/impl/sokol_runner.inc"
    #include "coi/native/impl/debug_tools.inc"

} // namespace coi::native
#endif // COI_NATIVE_SOKOL
