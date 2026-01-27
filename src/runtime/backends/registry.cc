#include "runtime/backends/registry.h"

#include <cstdlib>
#include <string>

#include "runtime/backends/rmlui_backend.h"
#include "runtime/backends/tree_backend.h"

#include "runtime/deps_sokol.h"
#include "runtime/util.h"

namespace coi::native {

BackendPref backend_pref_from_env() {
    const char* e = std::getenv("COI_NATIVE_UI_BACKEND");
    if (!e || !*e) return BackendPref::Auto;
    const std::string v(e);
    if (v == "tree") return BackendPref::Tree;
    if (v == "rmlui") return BackendPref::RmlUi;
    return BackendPref::Auto;
}

const char* backend_pref_name(BackendPref pref) {
    switch (pref) {
    case BackendPref::Tree:
        return "tree";
    case BackendPref::RmlUi:
        return "rmlui";
    case BackendPref::Auto:
    default:
        return "auto";
    }
}

void init_sokol_backends(float fbw, float fbh, float dpi) {
    (void)fbw;
    (void)fbh;
    (void)dpi;

#if defined(COI_NATIVE_RMLUI)
    rmlui_backend().init_gfx();
#endif
}

UiBackend& select_backend(const InputState& input, float dt, float dpi, float fbw, float fbh) {
    UiBackend& tree = tree_backend();
    UiBackend* backend = &tree;

    const BackendPref pref = backend_pref_from_env();

    auto use_tree = [&]() -> UiBackend& {
        tree.set_input(input, dt, dpi);
        tree.layout(fbw, fbh, dpi);
        backend = &tree;
        return *backend;
    };

#if defined(COI_NATIVE_RMLUI)
    UiBackend& rmlui = rmlui_backend();
    auto try_rmlui = [&]() -> bool {
        rmlui.set_input(input, dt, dpi);
        rmlui.layout(fbw, fbh, dpi);
        if (rmlui.is_ok()) {
            backend = &rmlui;
            return true;
        }
        return false;
    };
#endif

    if (pref == BackendPref::Tree) {
        return use_tree();
    }
    if (pref == BackendPref::RmlUi) {
#if defined(COI_NATIVE_RMLUI)
        if (try_rmlui()) return *backend;
#endif
        return use_tree();
    }

    // Auto: prefer RmlUI (if built), else fallback tree.
#if defined(COI_NATIVE_RMLUI)
    if (try_rmlui()) return *backend;
#endif
    return use_tree();
}

void shutdown_sokol_backends() {
#if defined(COI_NATIVE_RMLUI)
    rmlui_backend().shutdown_gfx();
#endif
}

} // namespace coi::native
