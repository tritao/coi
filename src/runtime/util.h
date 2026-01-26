	inline bool asset_debug_enabled() {
	    const char* e = std::getenv("COI_NATIVE_ASSET_DEBUG");
	    if (!e || !*e) return false;
	    return !(e[0] == '0' && e[1] == '\0');
	}

	inline std::optional<std::filesystem::path> try_get_executable_path() {
#if defined(__linux__)
	    std::array<char, 4096> buf{};
	    ssize_t n = ::readlink("/proc/self/exe", buf.data(), buf.size() - 1);
	    if (n > 0) {
	        buf[(size_t)n] = '\0';
	        return std::filesystem::path(buf.data());
	    }
#elif defined(__APPLE__)
	    std::array<char, 4096> buf{};
	    uint32_t size = (uint32_t)buf.size();
	    if (_NSGetExecutablePath(buf.data(), &size) == 0) {
	        return std::filesystem::path(buf.data());
	    }
#elif defined(_WIN32)
	    std::array<char, 4096> buf{};
	    DWORD n = GetModuleFileNameA(nullptr, buf.data(), (DWORD)buf.size());
	    if (n > 0 && n < buf.size()) {
	        buf[(size_t)n] = '\0';
	        return std::filesystem::path(buf.data());
	    }
#endif
	    return std::nullopt;
	}

	inline std::filesystem::path resolve_existing_path(const std::filesystem::path& in) {
	    std::filesystem::path p = in.lexically_normal();
	    std::error_code ec;
	    if (p.is_absolute()) {
	        if (std::filesystem::exists(p, ec)) return p;
	        return {};
	    }

	    if (std::filesystem::exists(p, ec)) return p;

	    if (const char* root = std::getenv("COI_NATIVE_ASSET_ROOT"); root && *root) {
	        std::filesystem::path cand = std::filesystem::path(root) / p;
	        cand = cand.lexically_normal();
	        if (std::filesystem::exists(cand, ec)) {
	            if (asset_debug_enabled()) {
	                std::cerr << "[asset] resolved " << p.string() << " via COI_NATIVE_ASSET_ROOT to " << cand.string() << "\n";
	            }
	            return cand;
	        }
	    }

	    auto search_up_from = [&](std::filesystem::path base) -> std::filesystem::path {
	        for (int i = 0; i < 14; i++) {
	            std::filesystem::path cand = (base / p).lexically_normal();
	            if (std::filesystem::exists(cand, ec)) {
	                if (asset_debug_enabled()) {
	                    std::cerr << "[asset] resolved " << p.string() << " via search-up from " << base.string() << " to " << cand.string() << "\n";
	                }
	                return cand;
	            }
	            if (!base.has_parent_path()) break;
	            std::filesystem::path parent = base.parent_path();
	            if (parent == base) break;
	            base = parent;
	        }
	        return {};
	    };

	    if (auto exe = try_get_executable_path()) {
	        std::filesystem::path found = search_up_from(exe->parent_path());
	        if (!found.empty()) return found;
	    }

	    std::filesystem::path cwd = std::filesystem::current_path(ec);
	    if (!ec && !cwd.empty()) {
	        std::filesystem::path found = search_up_from(cwd);
	        if (!found.empty()) return found;
	    }

	    return {};
	}

	inline bool read_file_bytes(const char* path, std::vector<unsigned char>& out) {
	    out.clear();
	    if (!path || !*path) return false;
	    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streampos size = f.tellg();
    if (size <= 0) return false;
    out.resize((size_t)size);
    f.seekg(0, std::ios::beg);
    f.read((char*)out.data(), size);
	    return (bool)f;
	}

#if defined(COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED)
	struct DesktopImage {
	    int w = 0;
	    int h = 0;
	    sg_image img{};
	    sg_view view{};
	    sclay_image scl{};
	};

	struct DesktopImageCache {
	    static inline std::unordered_map<std::string, std::unique_ptr<DesktopImage>> images;

	    static const DesktopImage* get_or_load(const char* src) {
	        if (!src || !*src) return nullptr;
	        std::error_code ec;
	        std::filesystem::path p(src);
	        p = p.lexically_normal();
	        std::filesystem::path resolved = resolve_existing_path(p);
	        const std::filesystem::path& load_path = resolved.empty() ? p : resolved;
	        const std::string key = load_path.string();

	        auto it = images.find(key);
	        if (it != images.end()) return it->second.get();

	        int w = 0, h = 0, n = 0;
	        unsigned char* rgba = stbi_load(key.c_str(), &w, &h, &n, 4);
	        if (!rgba || w <= 0 || h <= 0) {
	            std::cerr << "[img] failed to load " << key << "\n";
	            if (rgba) stbi_image_free(rgba);
	            images.emplace(key, nullptr);
	            return nullptr;
	        }

	        auto img = std::make_unique<DesktopImage>();
	        img->w = w;
	        img->h = h;

	        sg_image_desc img_desc{};
	        img_desc.width = w;
	        img_desc.height = h;
	        img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
	        img_desc.data.mip_levels[0].ptr = rgba;
	        img_desc.data.mip_levels[0].size = (size_t)w * (size_t)h * 4u;
	        img_desc.label = "coi-image";
	        img->img = sg_make_image(&img_desc);
	        stbi_image_free(rgba);

	        if (img->img.id == SG_INVALID_ID) {
	            std::cerr << "[img] sg_make_image failed for " << key << "\n";
	            images.emplace(key, nullptr);
	            return nullptr;
	        }

	        sg_view_desc view_desc{};
	        view_desc.texture.image = img->img;
	        view_desc.label = "coi-image-view";
	        img->view = sg_make_view(&view_desc);
	        if (img->view.id == SG_INVALID_ID) {
	            std::cerr << "[img] sg_make_view failed for " << key << "\n";
	            sg_destroy_image(img->img);
	            images.emplace(key, nullptr);
	            return nullptr;
	        }

	        img->scl.view = img->view;
	        img->scl.sampler = sg_sampler{};
	        img->scl.uv.u0 = 0.0f;
	        img->scl.uv.v0 = 0.0f;
	        img->scl.uv.u1 = 1.0f;
	        img->scl.uv.v1 = 1.0f;

	        const DesktopImage* out = img.get();
	        images.emplace(key, std::move(img));
	        return out;
	    }

	    static void shutdown() {
	        for (auto& kv : images) {
	            if (!kv.second) continue;
	            if (kv.second->view.id != SG_INVALID_ID) {
	                sg_destroy_view(kv.second->view);
	                kv.second->view.id = SG_INVALID_ID;
	            }
	            if (kv.second->img.id != SG_INVALID_ID) {
	                sg_destroy_image(kv.second->img);
	                kv.second->img.id = SG_INVALID_ID;
	            }
	        }
	        images.clear();
	    }
	};
#endif

	// Desktop event dispatch hooks (set by generated app code).
	// The dispatcher is expected to return true when the event was handled.
inline webcc::function<bool(webcc::handle)> g_click_dispatcher;
void set_click_dispatcher(webcc::function<bool(webcc::handle)> cb) {
    g_click_dispatcher = std::move(cb);
}

inline bool dispatch_click_bubble(webcc::handle start) {
    if (!g_click_dispatcher) return false;
    webcc::handle h = start;
    while (h.is_valid()) {
        if (g_click_dispatcher(h)) return true;
        auto it = coi::ui::g_nodes.find((int32_t)h);
        if (it == coi::ui::g_nodes.end()) break;
        h = it->second.parent;
    }
    return false;
}

// Flush hooks for headless runtime (tree dump + optional layout dump/click simulation).
void flush();

inline const webcc::string* attr(const coi::ui::Node& n, const char* key) {
    for (const auto& a : n.attrs) {
        if (a.key == key) return &a.value;
    }
    return nullptr;
}

inline uint32_t hash_u32(const char* s) {
    uint32_t h = 2166136261u;
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        h ^= *p;
        h *= 16777619u;
    }
    return h;
}

inline void color_from_hash(uint32_t h, float& r, float& g, float& b) {
    r = 0.25f + ((h & 0xFF) / 255.0f) * 0.65f;
    g = 0.25f + (((h >> 8) & 0xFF) / 255.0f) * 0.65f;
    b = 0.25f + (((h >> 16) & 0xFF) / 255.0f) * 0.65f;
}
