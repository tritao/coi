inline void sdtx_put_wrapped(const char* text, int cols) {
    if (!text || !*text) return;
    if (cols < 1) cols = 1;
    int len = (int)std::strlen(text);
    int i = 0;
    while (i < len) {
        int nl = i;
        while (nl < len && text[nl] != '\n') nl++;
        int max_take = std::min(cols, nl - i);
        int take = max_take;
        for (int j = 0; j < max_take; j++) {
            if (text[i + j] == ' ') take = j;
        }
        if (take == 0) take = max_take;
        sdtx_putr(text + i, take);
        sdtx_crlf();
        i += take;
        while (i < len && text[i] == ' ') i++;
        if (i < len && text[i] == '\n') i++;
    }
}

inline void sdtx_dump_node(int32_t id, int depth) {
    auto it = coi::ui::g_nodes.find(id);
    if (it == coi::ui::g_nodes.end()) return;
    const auto& n = it->second;
    std::string s;
    s.append((size_t)depth * 2, ' ');
    s.push_back('<');
    s += n.tag.c_str();
    for (const auto& a : n.attrs) {
        s.push_back(' ');
        s += a.key.c_str();
        s += "=\"";
        s += a.value.c_str();
        s += "\"";
    }
    s.push_back('>');
    if (!n.text.empty()) s += n.text.c_str();
    s += "</";
    s += n.tag.c_str();
    s.push_back('>');
    sdtx_printf("%s\n", s.c_str());
    for (int32_t c : n.children) sdtx_dump_node(c, depth + 1);
}

inline float measure_text_h(const coi::ui::Node& n, float w) {
    if (n.text.empty()) return 0.0f;
    const float char_w = 8.0f;
    const float char_h = 8.0f;
    float inner_w = std::max(1.0f, w);
    int cols = (int)std::floor(inner_w / char_w);
    if (cols < 1) cols = 1;
    int len = (int)std::strlen(n.text.c_str());
    int lines = (len + cols - 1) / cols;
    return lines * char_h;
}

