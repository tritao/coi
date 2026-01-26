// Native runtime glue TU.
// This file only provides small runtime globals; the implementation lives in
// dedicated translation units (runner, debug tools, vendor impls).

#include "coi/native/runtime_api.h"

#if defined(COI_NATIVE_SOKOL)
#include "runtime/util.h"
#include "runtime/state.h"

namespace coi::native {
bool g_sokol_frame_started = false;

webcc::function<bool(webcc::handle)> g_click_dispatcher;
void set_click_dispatcher(webcc::function<bool(webcc::handle)> cb) { g_click_dispatcher = std::move(cb); }
} // namespace coi::native
#endif
