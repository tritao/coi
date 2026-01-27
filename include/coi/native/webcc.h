#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>

// Native target shim for the subset of WebCC types that COI-generated code expects.
//
// The real WebCC headers are WASM-only (they rely on __builtin_wasm_memory_*), so the
// native runtime and native-generated apps must not include them. For native builds,
// we provide a minimal compatible surface:
//   - webcc::handle       -> int32_t
//   - webcc::string       -> std::string
//   - webcc::string_view  -> std::string_view
//   - webcc::function     -> std::function
namespace webcc {
struct handle {
    int32_t id = 0;

    constexpr handle() = default;
    constexpr handle(int32_t v) : id(v) {}

    constexpr bool is_valid() const { return id != 0; }

    constexpr explicit operator bool() const { return is_valid(); }
    constexpr operator int32_t() const { return id; }
};

constexpr inline bool operator==(handle a, handle b) { return a.id == b.id; }
constexpr inline bool operator!=(handle a, handle b) { return a.id != b.id; }

using string = std::string;
using string_view = std::string_view;

template <typename Sig>
using function = std::function<Sig>;

// Simple formatting helper used by generated code for string interpolation.
// This intentionally does not try to match WebCC's exact formatting behavior.
template <size_t N>
class hybrid_formatter {
  public:
    hybrid_formatter() { buf.reserve(N); }

    const char* c_str() const { return buf.c_str(); }
    const std::string& str() const { return buf; }

    hybrid_formatter& operator<<(const char* s) {
        if (s) buf += s;
        return *this;
    }

    hybrid_formatter& operator<<(const std::string& s) {
        buf += s;
        return *this;
    }

    hybrid_formatter& operator<<(std::string_view s) {
        buf.append(s.data(), s.size());
        return *this;
    }

    hybrid_formatter& operator<<(char c) {
        buf.push_back(c);
        return *this;
    }

    template <typename T, typename = std::enable_if_t<std::is_arithmetic_v<T>>>
    hybrid_formatter& operator<<(T v) {
        buf += std::to_string(v);
        return *this;
    }

  private:
    std::string buf;
};
} // namespace webcc
