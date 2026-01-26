#pragma once

#include "runtime/backends/backend.h"

namespace coi::native {
#if defined(COI_NATIVE_RMLUI)
UiBackend& rmlui_backend();
#endif
} // namespace coi::native

