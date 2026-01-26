#pragma once

#include <array>
#include <cstdint>

#include "webcc/core/handle.h"

namespace coi::native {

enum class InputEventType : uint8_t {
    KeyDown = 0,
    KeyUp = 1,
    Char = 2,
};

struct InputEvent {
    InputEventType type = InputEventType::KeyDown;
    int key_code = 0;           // sapp_keycode
    uint32_t char_code = 0;     // UTF-32 codepoint
    uint32_t modifiers = 0;     // sapp_modifier bitmask
    bool repeat = false;
};

struct InputState {
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    bool mouse_down = false;
    float scroll_x = 0.0f;
    float scroll_y = 0.0f;

    std::array<InputEvent, 64> events{};
    uint32_t event_count = 0;
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
