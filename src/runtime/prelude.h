#pragma once

// Common includes for the native runtime implementation.
// Internal runtime headers should include this instead of relying on indirect includes
// from a specific translation unit.

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
#include <system_error>
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

#include "webcc/core/function.h"
#include "webcc/core/handle.h"
#include "webcc/core/string.h"
#include "webcc/core/string_view.h"

// Shared class-token parsing + CSS emitters used by multiple native UI backends.
#include "coi/style/css.h"

// Retained UI tree declarations.
#include "runtime/ui_tree.h"

// Native graphics deps (Sokol + optional Clay/RmlUI helpers).
#if defined(COI_NATIVE_SOKOL)
#include "runtime/deps_sokol.h"
#endif
