class TreeBackend final : public UiBackend {
  public:
    const char* name() const override { return "tree"; }

    void set_input(const InputState&, float, float) override {}

    void layout(float w, float h, float) override {
        layout_map.clear();
        draw_list.clear();
        (void)layout_node(0, 0.0f, 0.0f, w, h);
    }

    void render(float, float, float) override {
        sgl_begin_quads();
        for (int32_t id : draw_list) {
            if (id == 0) continue;
            auto itn = coi::ui::g_nodes.find(id);
            if (itn == coi::ui::g_nodes.end()) continue;
            const auto& n = itn->second;
            if (n.tag == "comment") continue;
            auto itr = layout_map.find(id);
            if (itr == layout_map.end()) continue;
            const Rect& r = itr->second;
            const webcc::string* cls = attr(n, "class");
            uint32_t h = hash_u32(cls ? cls->c_str() : n.tag.c_str());
            float cr, cg, cb;
            color_from_hash(h, cr, cg, cb);
            sgl_c4f(cr, cg, cb, 0.18f);
            float x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;
            sgl_v2f(x0, y0);
            sgl_v2f(x1, y0);
            sgl_v2f(x1, y1);
            sgl_v2f(x0, y1);
        }
        sgl_end();
        sgl_draw();
    }

    bool hit_test(float x, float y, float, webcc::handle& out) override {
        out = webcc::handle();
        for (auto it = draw_list.rbegin(); it != draw_list.rend(); ++it) {
            int32_t id = *it;
            auto itr = layout_map.find(id);
            if (itr == layout_map.end()) continue;
            const Rect& r = itr->second;
            if (x >= r.x && x <= (r.x + r.w) && y >= r.y && y <= (r.y + r.h)) {
                out = webcc::handle(id);
                return true;
            }
        }
        return false;
    }

    void scroll_by(float, float, float, float, float, float) override {}

  private:
    std::unordered_map<int32_t, Rect> layout_map;
    std::vector<int32_t> draw_list;

    float layout_node(int32_t id, float x, float y, float w, float max_h) {
        auto it = coi::ui::g_nodes.find(id);
        if (it == coi::ui::g_nodes.end()) return 0.0f;
        const auto& n = it->second;
        const bool is_root = (id == 0);
        const float pad = is_root ? 16.0f : 12.0f;
        const float gap = 10.0f;
        float content_x = x + pad;
        float content_y = y + pad;
        float content_w = std::max(1.0f, w - 2.0f * pad);
        float used_h = pad * 2.0f;

        if (!n.text.empty() && !n.children.empty()) {
            float th = measure_text_h(n, content_w);
            used_h += th + gap;
            content_y += th + gap;
        }

        float cur_y = content_y;
        for (int32_t c : n.children) {
            float child_h = layout_node(c, content_x, cur_y, content_w, max_h);
            if (child_h <= 0.0f) continue;
            cur_y += child_h + gap;
            used_h += child_h + gap;
        }
        if (!n.children.empty()) used_h -= gap;

        if (n.children.empty()) {
            float th = measure_text_h(n, content_w);
            used_h = std::max(used_h, pad * 2.0f + th);
        }

        if (is_root) {
            used_h = max_h;
        } else {
            used_h = std::min(used_h, max_h);
            used_h = std::max(used_h, 32.0f);
        }

        layout_map[id] = Rect{x, y, w, used_h};
        draw_list.push_back(id);
        return used_h;
    }
};

inline TreeBackend& tree_backend() {
    static TreeBackend b;
    return b;
}

