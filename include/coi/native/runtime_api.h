#pragma once

#include <cstdint>

#include "webcc/core/function.h"
#include "webcc/core/handle.h"
#include "webcc/core/string_view.h"

namespace coi::ui {
webcc::handle next_deferred_handle();
webcc::handle get_body();

void flush();

void create_element_deferred(webcc::handle h, webcc::string_view tag);
void create_comment_deferred(webcc::handle h, webcc::string_view text);

void set_attribute(webcc::handle h, webcc::string_view name, webcc::string_view value);
void set_property(webcc::handle h, webcc::string_view name, webcc::string_view value);
void set_inner_html(webcc::handle h, webcc::string_view html);
void set_inner_text(webcc::handle h, webcc::string_view text);

void append_child(webcc::handle parent, webcc::handle child);
void insert_before(webcc::handle parent, webcc::handle child, webcc::handle ref);
void move_before(webcc::handle parent, webcc::handle node, webcc::handle ref);
void remove_element(webcc::handle h);

void add_click_listener(webcc::handle h);
void add_input_listener(webcc::handle h);
void add_change_listener(webcc::handle h);
void add_keydown_listener(webcc::handle h);

void scroll_to_top();
} // namespace coi::ui

namespace coi::native {
using tick_fn = void (*)(void*, double);

void set_click_dispatcher(webcc::function<bool(webcc::handle)> cb);

// Native-only flush (scripts, dumps, etc). Generated code calls this in the native main loop.
void flush();

// Run the Sokol+Clay native runtime. Only valid when compiled with `COI_NATIVE_SOKOL`.
int run_sokol(void* app, int frames, tick_fn tick);
} // namespace coi::native
