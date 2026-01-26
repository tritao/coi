#include "coi/native/internal/state.h"

inline bool g_layout_dumped = false;
inline bool g_click_done = false;
inline bool g_render_dumped = false;
inline bool g_scroll_done = false;

inline bool parse_xy(const char* s, float& x, float& y) {
    if (!s || !*s) return false;
    int ix = 0, iy = 0;
    if (std::sscanf(s, "%d,%d", &ix, &iy) == 2 || std::sscanf(s, "%d %d", &ix, &iy) == 2) {
        x = (float)ix;
        y = (float)iy;
        return true;
    }
    return false;
}

inline bool parse_viewport(float& w, float& h) {
    const char* vw = std::getenv("COI_NATIVE_VIEWPORT");
    if (!vw || !*vw) {
        w = 960.0f;
        h = 540.0f;
        return true;
    }
    int iw = 0;
    int ih = 0;
    if (std::sscanf(vw, "%dx%d", &iw, &ih) == 2 || std::sscanf(vw, "%d,%d", &iw, &ih) == 2) {
        if (iw <= 0) iw = 1;
        if (ih <= 0) ih = 1;
        w = (float)iw;
        h = (float)ih;
        return true;
    }
    w = 960.0f;
    h = 540.0f;
    return true;
}

inline void dump_layout(float w, float h);
inline void dump_layout() {
#if defined(COI_NATIVE_CLAY)
    float w = 0.0f, h = 0.0f;
    parse_viewport(w, h);
    dump_layout(w, h);
#endif
}

inline void dump_layout(float w, float h) {
#if defined(COI_NATIVE_CLAY)
    (void)ClayEngine::layout(w, h);
    if (!ClayEngine::ctx) return;

    std::cout << "--- COI_NATIVE_LAYOUT ---\n";
    auto dump = [&](auto&& self, int32_t id, int depth) -> void {
        auto it = coi::ui::g_nodes.find(id);
        if (it == coi::ui::g_nodes.end()) return;
        const auto& n = it->second;
        if (n.tag == "comment") return;
        Clay_ElementData d = Clay_GetElementData(ClayEngine::element_id(id));
        if (!d.found) return;
        for (int i = 0; i < depth; i++) std::cout << "  ";
        std::cout << "<" << n.tag.c_str();
        for (const auto& a : n.attrs) {
            if (a.key == "class") continue;
            std::cout << " " << a.key.c_str() << "=\"" << a.value.c_str() << "\"";
        }
        if (const webcc::string* cls = attr(n, "class")) {
            std::cout << " class=\"" << cls->c_str() << "\"";
        }
        std::cout << ">";
        if (!n.text.empty()) std::cout << n.text.c_str();
        std::cout << "</" << n.tag.c_str() << ">";
        std::cout << " x=" << (int)std::lround(d.boundingBox.x) << " y=" << (int)std::lround(d.boundingBox.y)
                  << " w=" << (int)std::lround(d.boundingBox.width) << " h=" << (int)std::lround(d.boundingBox.height)
                  << "\n";
        for (int32_t c : n.children) self(self, c, depth + 1);
    };
    dump(dump, 0, 0);
    std::cout << std::flush;
#endif
}

inline void dump_render(float w, float h);
inline void dump_render() {
#if defined(COI_NATIVE_CLAY)
    float w = 0.0f, h = 0.0f;
    parse_viewport(w, h);
    dump_render(w, h);
#endif
}

inline void dump_render(float w, float h) {
#if defined(COI_NATIVE_CLAY)
    Clay_RenderCommandArray render_commands = ClayEngine::layout(w, h);
    if (!ClayEngine::ctx) return;

    std::cout << "--- COI_NATIVE_RENDER_DUMP ---\n";
    auto iround = [](float v) -> int { return (int)std::lround((double)v); };
    auto escape = [](const char* s, int32_t len) -> std::string {
        std::string out;
        out.reserve((size_t)len);
        for (int32_t i = 0; i < len; i++) {
            char c = s[i];
            if (c == '\\') {
                out += "\\\\";
            } else if (c == '"') {
                out += "\\\"";
            } else if (c == '\n') {
                out += "\\n";
            } else if (c == '\r') {
                out += "\\r";
            } else if (c == '\t') {
                out += "\\t";
            } else {
                out.push_back(c);
            }
        }
        return out;
    };
    for (int32_t i = 0; i < render_commands.length; i++) {
        Clay_RenderCommand* cmd = Clay_RenderCommandArray_Get(&render_commands, i);
        if (!cmd) continue;
        const auto& bb = cmd->boundingBox;

        if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_SCISSOR_START) {
            std::cout << "SCISSOR_START x=" << iround(bb.x) << " y=" << iround(bb.y) << " w=" << iround(bb.width) << " h=" << iround(bb.height) << "\n";
        } else if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_SCISSOR_END) {
            std::cout << "SCISSOR_END\n";
        } else if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_BORDER) {
            const auto& b = cmd->renderData.border;
            const auto& c = b.color;
            if (c.a <= 0.0f) continue;
            const auto& w = b.width;
            if (!(w.left || w.right || w.top || w.bottom || w.betweenChildren)) continue;

            const int32_t owner = cmd->userData ? (int32_t)(intptr_t)cmd->userData : 0;
            const auto itn = (owner != 0) ? coi::ui::g_nodes.find(owner) : coi::ui::g_nodes.end();

            std::cout << "BORDER";
            if (itn != coi::ui::g_nodes.end()) {
                const auto& n = itn->second;
                const webcc::string* cls = attr(n, "class");
                std::cout << " owner=" << owner << " tag=" << n.tag.c_str();
                if (cls && !cls->empty()) std::cout << " class=\"" << cls->c_str() << "\"";
            } else if (owner != 0) {
                std::cout << " owner=" << owner;
            }
            std::cout << " x=" << iround(bb.x) << " y=" << iround(bb.y) << " w=" << iround(bb.width) << " h=" << iround(bb.height);
            std::cout << " l=" << (int)w.left << " r=" << (int)w.right << " t=" << (int)w.top << " b=" << (int)w.bottom
                      << " between=" << (int)w.betweenChildren;
            std::cout << " color=" << (int)c.r << "," << (int)c.g << "," << (int)c.b << "," << (int)c.a << "\n";
        } else if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_RECTANGLE) {
            const auto& c = cmd->renderData.rectangle.backgroundColor;
            if (c.a <= 0.0f) continue;

            int32_t hid = (int32_t)(cmd->id ^ 0xC01D0000u);
            auto itn = coi::ui::g_nodes.find(hid);
            if (itn != coi::ui::g_nodes.end()) {
                const auto& n = itn->second;
                const webcc::string* cls = attr(n, "class");

                std::cout << "RECT id=" << hid << " tag=" << n.tag.c_str();
                if (cls && !cls->empty()) std::cout << " class=\"" << cls->c_str() << "\"";
                std::cout << " x=" << iround(bb.x) << " y=" << iround(bb.y) << " w=" << iround(bb.width) << " h=" << iround(bb.height) << "\n";
                continue;
            }

            // Clay emits "betweenChildren" borders as anonymous RECTANGLE commands. Expose those in the dump.
            const int32_t owner = cmd->userData ? (int32_t)(intptr_t)cmd->userData : 0;
            if (owner != 0) {
                auto ito = coi::ui::g_nodes.find(owner);
                if (ito != coi::ui::g_nodes.end()) {
                    const auto& n = ito->second;
                    DesktopClassStyle st = parse_desktop_class_style(n, owner == 0);
                    if (st.has_border && st.border_width.betweenChildren > 0) {
                        const webcc::string* cls = attr(n, "class");
                        std::cout << "RECT_BETWEEN owner=" << owner << " tag=" << n.tag.c_str();
                        if (cls && !cls->empty()) std::cout << " class=\"" << cls->c_str() << "\"";
                        std::cout << " x=" << iround(bb.x) << " y=" << iround(bb.y) << " w=" << iround(bb.width) << " h=" << iround(bb.height);
                        std::cout << " color=" << (int)c.r << "," << (int)c.g << "," << (int)c.b << "," << (int)c.a << "\n";
                    }
                }
            }
        } else if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_TEXT) {
            const auto& t = cmd->renderData.text;
            const int32_t owner = cmd->userData ? (int32_t)(intptr_t)cmd->userData : 0;
            const auto itn = (owner != 0) ? coi::ui::g_nodes.find(owner) : coi::ui::g_nodes.end();

            std::cout << "TEXT";
            if (itn != coi::ui::g_nodes.end()) {
                const auto& n = itn->second;
                const webcc::string* cls = attr(n, "class");
                std::cout << " owner=" << owner << " tag=" << n.tag.c_str();
                if (cls && !cls->empty()) std::cout << " class=\"" << cls->c_str() << "\"";
            } else if (owner != 0) {
                std::cout << " owner=" << owner;
            }
            std::cout << " x=" << iround(bb.x) << " y=" << iround(bb.y) << " w=" << iround(bb.width) << " h=" << iround(bb.height);
            std::cout << " color=" << (int)t.textColor.r << "," << (int)t.textColor.g << "," << (int)t.textColor.b << "," << (int)t.textColor.a;
            std::cout << " font=" << (int)t.fontId << " size=" << (int)t.fontSize << " ls=" << (int)t.letterSpacing << " lh=" << (int)t.lineHeight;
            std::cout << " text=\"" << escape(t.stringContents.chars, t.stringContents.length) << "\"\n";
        }
    }
    std::cout << std::flush;
#endif
}

inline void dump_tree_force() {
    std::cout << "--- COI_NATIVE_DUMP ---\n";
    auto dump = [&](auto&& self, int32_t id, int depth) -> void {
        auto it = coi::ui::g_nodes.find(id);
        if (it == coi::ui::g_nodes.end()) return;
        const coi::ui::Node& n = it->second;
        for (int i = 0; i < depth; i++) std::cout << "  ";
        std::cout << "<" << n.tag.c_str();
        for (const auto& a : n.attrs) {
            std::cout << " " << a.key.c_str() << "=\"" << a.value.c_str() << "\"";
        }
        std::cout << ">";
        if (!n.text.empty()) std::cout << n.text.c_str();
        std::cout << "</" << n.tag.c_str() << ">\n";
        for (int32_t c : n.children) self(self, c, depth + 1);
    };
    dump(dump, 0, 0);
    std::cout << std::flush;
}

inline void simulate_click_at(float x, float y, float w, float h) {
    if (!g_click_dispatcher) return;
    webcc::handle target;
#if defined(COI_NATIVE_RMLUI)
    const char* pref = std::getenv("COI_NATIVE_UI_BACKEND");
    if (pref && std::string(pref) == "rmlui") {
        detail::rmlui_backend().layout(w, h, 1.0f);
        if (detail::rmlui_backend().is_ok() && detail::rmlui_backend().hit_test(x, y, 1.0f, target) && target.is_valid()) {
            dispatch_click_bubble(target);
            return;
        }
    }
#endif
#if defined(COI_NATIVE_CLAY)
    detail::clay_backend().layout(w, h, 1.0f);
    if (detail::clay_backend().is_ok() && detail::clay_backend().hit_test(x, y, 1.0f, target) && target.is_valid()) {
        dispatch_click_bubble(target);
        return;
    }
#endif
    detail::tree_backend().layout(w, h, 1.0f);
    if (detail::tree_backend().hit_test(x, y, 1.0f, target) && target.is_valid()) {
        dispatch_click_bubble(target);
    }
}

inline void simulate_scroll_at(float pointer_x, float pointer_y, float dx, float dy, float w, float h) {
    (void)pointer_x;
    (void)pointer_y;
    (void)dx;
    (void)dy;
    (void)w;
    (void)h;
#if defined(COI_NATIVE_RMLUI)
    const char* pref = std::getenv("COI_NATIVE_UI_BACKEND");
    if (pref && std::string(pref) == "rmlui") {
        detail::rmlui_backend().layout(w, h, 1.0f);
        if (detail::rmlui_backend().is_ok()) {
            detail::rmlui_backend().scroll_by(pointer_x, pointer_y, dx, dy, w, h);
            return;
        }
    }
#endif
#if defined(COI_NATIVE_CLAY)
    detail::clay_backend().scroll_by(pointer_x, pointer_y, dx, dy, w, h);
#endif
}

inline void maybe_simulate_click() {
    const char* c = std::getenv("COI_NATIVE_CLICK");
    if (!c || !*c) return;
    if (g_click_done && std::string(c) != std::string("always")) return;

    float x = 0.0f, y = 0.0f;
    if (!parse_xy(c, x, y)) return;
    if (!g_click_dispatcher) return;

    float w = 0.0f, h = 0.0f;
    parse_viewport(w, h);
    simulate_click_at(x, y, w, h);
    g_click_done = true;
}

inline void maybe_simulate_scroll() {
    const char* s = std::getenv("COI_NATIVE_SCROLL");
    if (!s || !*s) return;
    if (g_scroll_done && std::string(s) != std::string("always")) return;

    float dx = 0.0f, dy = 0.0f;
    if (!parse_xy(s, dx, dy)) return;

    float px = 1.0f, py = 1.0f;
    const char* p = std::getenv("COI_NATIVE_POINTER");
    if (p && *p) (void)parse_xy(p, px, py);

    float w = 0.0f, h = 0.0f;
    parse_viewport(w, h);
    simulate_scroll_at(px, py, dx, dy, w, h);
    g_scroll_done = true;
}

inline bool parse_wh(const std::string& s, float& w, float& h) {
    int iw = 0;
    int ih = 0;
    if (std::sscanf(s.c_str(), "%dx%d", &iw, &ih) == 2 || std::sscanf(s.c_str(), "%d,%d", &iw, &ih) == 2 ||
        std::sscanf(s.c_str(), "%d %d", &iw, &ih) == 2) {
        if (iw <= 0) iw = 1;
        if (ih <= 0) ih = 1;
        w = (float)iw;
        h = (float)ih;
        return true;
    }
    return false;
}

inline std::string trim_copy(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
    size_t j = s.size();
    while (j > i && (s[j - 1] == ' ' || s[j - 1] == '\t' || s[j - 1] == '\r' || s[j - 1] == '\n')) j--;
    return s.substr(i, j - i);
}

inline std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
        if (i >= s.size()) break;
        size_t j = i;
        while (j < s.size() && (s[j] != ' ' && s[j] != '\t')) j++;
        out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

struct DesktopScriptRunner {
    bool loaded = false;
    bool ran = false;
    std::string path;
    std::vector<std::string> lines;

    float viewport_w = 0.0f;
    float viewport_h = 0.0f;
    float pointer_x = 1.0f;
    float pointer_y = 1.0f;

    bool dump_tree = true;
    bool dump_layout = false;
    bool dump_render = false;

    size_t next_line = 0;
    int step = 0;
    int wait_frames = 0;
};

inline DesktopScriptRunner g_script;

inline void load_script_if_any() {
    if (g_script.loaded) return;
    const char* p = std::getenv("COI_NATIVE_SCRIPT");
    if (!p || !*p) return;
    g_script.loaded = true;
    g_script.path = p;

    parse_viewport(g_script.viewport_w, g_script.viewport_h);
    const char* ptr = std::getenv("COI_NATIVE_POINTER");
    if (ptr && *ptr) (void)parse_xy(ptr, g_script.pointer_x, g_script.pointer_y);

    const char* dumps = std::getenv("COI_NATIVE_SCRIPT_DUMPS");
    if (dumps && *dumps) {
        g_script.dump_tree = false;
        g_script.dump_layout = false;
        g_script.dump_render = false;
        std::string raw = trim_copy(std::string(dumps));
        if (raw == "0" || raw == "off" || raw == "none") {
            // Explicitly disable all dumps.
        } else {
        std::string s = dumps;
        for (char& c : s) c = (c == ';') ? ',' : c;
        auto parts = split_ws(s);
        // also accept comma-separated in a single token
        std::vector<std::string> expanded;
        for (const auto& tok : parts) {
            size_t start = 0;
            while (start < tok.size()) {
                size_t end = tok.find(',', start);
                if (end == std::string::npos) end = tok.size();
                if (end > start) expanded.push_back(tok.substr(start, end - start));
                start = end + 1;
            }
        }
        for (auto t : expanded) {
            for (char& c : t) c = (char)std::tolower((unsigned char)c);
            if (t == "all") {
                g_script.dump_tree = true;
                g_script.dump_layout = true;
                g_script.dump_render = true;
            } else if (t == "dump" || t == "tree") {
                g_script.dump_tree = true;
            } else if (t == "layout") {
                g_script.dump_layout = true;
            } else if (t == "render" || t == "render_dump") {
                g_script.dump_render = true;
            }
        }
        // default if parsed nothing
        if (!g_script.dump_tree && !g_script.dump_layout && !g_script.dump_render) g_script.dump_tree = true;
        }
    }

    std::ifstream f(g_script.path);
    if (!f) {
        std::cerr << "[native-script] failed to open: " << g_script.path << "\n";
        g_script.ran = true;
        return;
    }
    std::string line;
    while (std::getline(f, line)) g_script.lines.push_back(line);
}

inline void script_snapshot(int step, const std::string& line) {
    std::cout << "--- COI_NATIVE_SCRIPT_STEP " << step << ": " << line << " ---\n";
    if (g_script.dump_tree) dump_tree_force();
    if (g_script.dump_layout) dump_layout(g_script.viewport_w, g_script.viewport_h);
    if (g_script.dump_render) dump_render(g_script.viewport_w, g_script.viewport_h);
    std::cout << std::flush;
}

inline void run_script_if_any() {
    load_script_if_any();
    if (!g_script.loaded || g_script.ran) return;

    // Handle waits across frames.
    if (g_script.wait_frames > 0) {
        g_script.wait_frames--;
        if (g_script.wait_frames > 0) return;
        // If wait reached zero, continue executing remaining script lines below.
    }

    for (; g_script.next_line < g_script.lines.size(); g_script.next_line++) {
        const auto& raw = g_script.lines[g_script.next_line];
        std::string line = trim_copy(raw);
        if (line.empty()) continue;
        if (line[0] == '#') continue;

        auto toks = split_ws(line);
        if (toks.empty()) continue;
        std::string cmd = toks[0];
        for (char& c : cmd) c = (char)std::tolower((unsigned char)c);

        bool ok = true;
        if (cmd == "viewport") {
            if (toks.size() < 2) {
                ok = false;
            } else {
                ok = parse_wh(toks[1], g_script.viewport_w, g_script.viewport_h);
                if (!ok && toks.size() >= 3) {
                    ok = parse_wh(toks[1] + " " + toks[2], g_script.viewport_w, g_script.viewport_h);
                }
            }
        } else if (cmd == "wait" || cmd == "frames") {
            if (toks.size() < 2) {
                ok = false;
            } else {
                int n = std::atoi(toks[1].c_str());
                if (n < 0) n = 0;
                g_script.wait_frames = n;
            }
        } else if (cmd == "pointer" || cmd == "move") {
            if (toks.size() < 3) {
                ok = false;
            } else {
                float x = 0.0f, y = 0.0f;
                ok = parse_xy((toks[1] + " " + toks[2]).c_str(), x, y);
                if (ok) {
                    g_script.pointer_x = x;
                    g_script.pointer_y = y;
                }
            }
        } else if (cmd == "click") {
            float x = g_script.pointer_x;
            float y = g_script.pointer_y;
            if (toks.size() >= 3) {
                ok = parse_xy((toks[1] + " " + toks[2]).c_str(), x, y);
            }
            if (ok) simulate_click_at(x, y, g_script.viewport_w, g_script.viewport_h);
        } else if (cmd == "scroll") {
            if (toks.size() < 3) {
                ok = false;
            } else {
                float dx = 0.0f, dy = 0.0f;
                ok = parse_xy((toks[1] + " " + toks[2]).c_str(), dx, dy);
                if (ok) {
                    simulate_scroll_at(g_script.pointer_x, g_script.pointer_y, dx, dy, g_script.viewport_w, g_script.viewport_h);
                }
            }
        } else if (cmd == "snapshot" || cmd == "dump") {
            // no-op; snapshot happens below
        } else {
            ok = false;
        }

        g_script.step++;
        if (!ok) {
            std::cerr << "[native-script] parse error in: " << g_script.path << ": " << line << "\n";
        }
        script_snapshot(g_script.step, line);

        // Yield to the next frame if we just scheduled a wait.
        if ((cmd == "wait" || cmd == "frames") && g_script.wait_frames > 0) {
            g_script.next_line++;
            return;
        }
    }

    g_script.ran = true;
}

void flush() {
    const bool has_window_env = []() -> bool {
        const char* e = std::getenv("COI_NATIVE_WINDOW");
        return (e && *e && std::string(e) != std::string("0"));
    }();
    const bool has_capture_env = []() -> bool {
        const char* e = std::getenv("COI_NATIVE_CAPTURE_DIR");
        return (e && *e);
    }();
    const bool pre_sokol = (has_window_env || has_capture_env) && !g_sokol_frame_started;

    // Desktop scripts and simulated input should run during the actual main loop.
    // When window/capture is enabled, the generated main() calls coi::native::flush()
    // once before starting the Sokol loop; running scripts there would consume steps
    // before the first captured frame.
    if (!pre_sokol) {
        run_script_if_any();
        maybe_simulate_scroll();
        maybe_simulate_click();
    }
    coi::ui::flush();

    const char* render_env = std::getenv("COI_NATIVE_RENDER_DUMP");
    if (render_env && *render_env && !(render_env[0] == '0' && render_env[1] == '\0')) {
        if (!g_render_dumped || std::string(render_env) == std::string("always")) {
            dump_render();
            g_render_dumped = true;
        }
    }

    const char* env = std::getenv("COI_NATIVE_LAYOUT_DUMP");
    if (!env || !*env) return;
    if (env[0] == '0' && env[1] == '\0') return;
    if (g_layout_dumped && std::string(env) != std::string("always")) return;
    g_layout_dumped = true;
    dump_layout();
}
