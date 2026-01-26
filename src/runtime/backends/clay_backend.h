#pragma once

#include "runtime/backends/backend.h"

namespace coi::native {
#if defined(COI_NATIVE_CLAY)
UiBackend& clay_backend();
#endif
} // namespace coi::native

