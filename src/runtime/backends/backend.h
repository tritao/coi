#pragma once

#include "webcc/core/handle.h"

namespace coi::native {

struct InputState {
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    bool mouse_down = false;
    float scroll_x = 0.0f;
    float scroll_y = 0.0f;
};

// Shared geometry helper for the retained layout map used by debug backends.
struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

class UiBackend {
  public:
    virtual ~UiBackend() = default;
    virtual const char* name() const = 0;

    virtual void init_gfx() {}
    virtual void shutdown_gfx() {}

    virtual void set_input(const InputState& input, float dt, float dpi) = 0;
    virtual void layout(float w, float h, float dpi) = 0;
    virtual void render(float w, float h, float dpi) = 0;
    virtual bool hit_test(float x, float y, float dpi, webcc::handle& out) = 0;
    virtual void scroll_by(float pointer_x, float pointer_y, float dx, float dy, float w, float h) = 0;

    // Backend health / optional capabilities.
    virtual bool is_ok() const { return true; }
    virtual bool font_ok() const { return true; }
    virtual void* measure_userdata() { return nullptr; }
};

} // namespace coi::native
