#pragma once

#include <doxydown/StyleOption.hpp>
#include <doxydown/YAMLNode.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <utility>

class DriverConfigParser {
public:
  std::optional<std::string> style_str;
  std::optional<std::string> title;
  std::optional<std::string> description;
  std::optional<std::filesystem::path> coverage;
  std::optional<std::filesystem::path> output;

  DriverConfigParser() = default;

  DriverConfigParser(const DriverConfigParser&) = delete;

  DriverConfigParser& operator=(const DriverConfigParser&) = delete;

  std::string_view get_title() const noexcept;

  std::string_view get_description() const noexcept;

  const std::filesystem::path& get_output() const noexcept;

  StyleOption get_style() const;

  inline std::optional<std::filesystem::path> get_coverage() const noexcept {
    return coverage;
  }

  inline const std::optional<std::filesystem::path>& get_config_file() const noexcept { return yaml_path; }

  const YAMLNode& get_root() const noexcept { return root; }

  void parse_config_yaml(const std::string_view& info);

  std::unordered_map<std::string, std::string> get_defines() const;

  std::pair<std::unordered_set<std::filesystem::path>, std::vector<std::string>>
    get_filenames_and_patterns() const;
private:
  std::optional<StyleOption> style;

  std::optional<std::filesystem::path> yaml_path;
  YAMLNode root;

  template<typename OutT, typename OptT> OutT
    get_or_default(const std::optional<OptT>& opt, OutT&& def_value) const noexcept {
      if (opt) {
        return opt.value();
      }
      return def_value;
  }
};
