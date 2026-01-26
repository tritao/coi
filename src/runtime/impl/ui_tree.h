#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "webcc/core/handle.h"
#include "webcc/core/string.h"
#include "webcc/core/string_view.h"

namespace coi::ui {

struct Attr {
    webcc::string key;
    webcc::string value;
};

struct Node {
    webcc::string tag;
    webcc::string text;
    webcc::handle parent;
    std::vector<int32_t> children;
    std::vector<Attr> attrs;
};

extern std::unordered_map<int32_t, Node> g_nodes;
extern int32_t g_next_handle;
extern bool g_dumped;
extern uint64_t g_rev;

} // namespace coi::ui

