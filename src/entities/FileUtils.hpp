#pragma once

#include <filesystem>
#include <string_view>

using path_str = std::filesystem::path::string_type;
using path_str_view = std::basic_string_view<typename path_str::value_type>;

namespace file_utils {

  constexpr static path_str_view get_filename(const path_str_view& path) {
      path_str_view vpath = path;
      size_t last_slash = vpath.find_last_of("/\\");
      if (last_slash == path_str_view::npos) {
          return vpath; // No slash found, the entire path is the filename
      }
      return vpath.substr(last_slash + 1);
  }

  constexpr static path_str_view relative_path(const path_str_view& path,
    const path_str_view& base) {
    if (base.empty() or base.empty()) {
      return path;
    }
    if (base.size() >= path.size()) {
      return "";
    }
    return path.substr(base.size() + 1);
  }

  constexpr static path_str_view parent_path(const path_str_view& path) {
    size_t last_slash = path.find_last_of("/\\");
    if (last_slash == path_str_view::npos) {
        return ""; // No slash found, the entire path is the filename
    }
    return path.substr(0, last_slash);
  }

}
