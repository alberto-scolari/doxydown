#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <filesystem>
#include <format>
#include <functional>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>

using path_str_view = typename std::basic_string_view<typename std::filesystem::path::value_type>;

std::string to_string(const path_str_view& view);

extern const std::filesystem::path DEF_ROOT_PATH;

extern const std::filesystem::path SEPARATOR_PATH;

class path_ref {
public:
  path_ref() = delete;

  path_ref(const std::filesystem::path& _p): p(_p) {}

  path_ref(const path_ref& _p) = default;

  operator const std::filesystem::path& () const noexcept {
    return p;
  }

  const std::filesystem::path& path() const noexcept {
    return p;
  }

  bool operator==(const path_ref& other) const {
    return p == other.p;
  }

private:
  const std::filesystem::path& p;
};

inline std::ostream& operator<<(std::ostream& s, const path_ref& pr) {
  return s << pr.path();
}

namespace std {
    template<> struct hash<path_ref> {
        std::size_t operator()(const path_ref& pr) const noexcept {
            return std::hash<std::filesystem::path>{}(pr.path());
        }
    };
}

struct path_het_string_hash {
  using is_transparent = void; // Enables heterogeneous lookup

  inline std::size_t operator()(const std::filesystem::path::string_type& sv) const noexcept {
    return std::hash<std::filesystem::path::string_type>{}(sv);
  }

  inline std::size_t operator()(path_str_view sv) const noexcept {
    return std::hash<path_str_view>{}(sv);
  }

  inline std::size_t operator()(const std::filesystem::path& p) const noexcept {
    return std::hash<std::filesystem::path::string_type>{}(p.native());
  }

  inline std::size_t operator()(const path_ref& p) const noexcept {
    return std::hash<path_ref>{}(p);
  }
};

template<typename K, typename V>
  requires (
    std::same_as<K, std::filesystem::path>
    or std::same_as<K, std::filesystem::path::string_type>
    or std::same_as<K, path_str_view>
  )
  using path_unordered_map = std::unordered_map<K, V,
    path_het_string_hash, std::equal_to<>>;

#if __cplusplus < 202603L
template<> struct std::formatter<std::filesystem::path, char> {

  template<class ParseContext>
  constexpr ParseContext::iterator parse(ParseContext& ctx) {
    auto it = ctx.begin();
    if (it != ctx.end() && *it != '}') {
      throw std::format_error("Invalid format specifier for std::filesystem::path");
    }
    return it;
  }

#ifdef _WIN32
  inline decltype(auto) get_range(const std::filesystem::path& p) const {
    return p.string();
  }
#else
  inline decltype(auto) get_range(const std::filesystem::path& p) const {
    return p.native();
  }
#endif

  template<class FmtContext> FmtContext::iterator format(
    const std::filesystem::path& p, FmtContext& ctx) const {
    return std::ranges::copy(get_range(p), ctx.out()).out;
  }

};
#endif
