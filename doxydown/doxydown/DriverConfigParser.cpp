#include <doxydown/DriverConfigParser.hpp>

#include <doxydown/FileUtils.hpp>
#include <doxydown/Log.hpp>
#include <doxydown/YAMLParser.hpp>

#include <glob/glob.hpp>

#include <filesystem>
#include <format>
#include <initializer_list>
#include <stdexcept>
#include <system_error>

std::string_view DriverConfigParser::get_title() const noexcept {
  return get_or_default<std::string_view>(title, "");
}

std::string_view DriverConfigParser::get_description() const noexcept {
  return get_or_default<std::string_view>(description, "");
}

const std::filesystem::path& DriverConfigParser::get_output() const noexcept {
  return get_or_default<const std::filesystem::path&>(output, DEF_ROOT_PATH);
}

StyleOption DriverConfigParser::get_style() const {
  if (not style and style_str) {
    std::optional<StyleOption> parsed = str_to_style_option(style_str.value());
    if (not parsed) {
      throw std::runtime_error(std::format(
        "value \"{}\" from command line does not correspond to any style option",
        style_str.value()));
    }
  }
  return get_or_default(style, StyleOption::MKDOCS);
}

void DriverConfigParser::parse_config_yaml(const std::string_view& _yaml_path) {
  yaml_path.emplace(_yaml_path);
  root = YAMLParser().parse(_yaml_path);
  const YAMLNode& _root = root;
  std::string buffer;
  auto parse_value = [&_root, &buffer](const std::string_view& value_name) -> bool {
    if (not _root.has(value_name)) {
      return false;
    }
    if (_root.isValue(value_name)) {
      buffer = _root.value(value_name);
      return true;
    } else {
      warn("'" << value_name << "' must be a value in configuration.");
    }
    return false;
  };
  if (parse_value("style")) {
    std::optional<StyleOption> parsed = str_to_style_option(buffer);
    if (not parsed) {
      throw std::runtime_error(std::format(
        "value \"{}\" from configuration file \"{}\" does not correspond to any style option",
        buffer, _yaml_path));
    } else {
      style = parsed;
    }
  }
  if (parse_value("title")) {
    title = buffer;
  }
  if (parse_value("description")) {
    description = buffer;
  }

  std::initializer_list<
    std::pair<std::string_view, std::optional<std::filesystem::path>&>
  > file_members{{"output", output}, {"coverage", coverage}};
  for(auto& [value, member]: file_members) {
    if (not parse_value(value)) {
      continue;
    }
    try {
      member.emplace(buffer);
    } catch (...) {
      warn("cannot construct file from configuration file \"" << _yaml_path
        << "\", field \"" << value << "\": \"" << buffer << "\": got an internal exception."
      );
      throw;
    }
    std::error_code ec;
    member = std::filesystem::weakly_canonical(member.value(), ec);
    if (ec) {
      throw std::runtime_error(std::format(
        "from configuration file \"{}\": field \"{}\", value \"{}\" is not a valid path",
        _yaml_path, value, buffer));
    }
  }
}

std::unordered_map<std::string,std::string>
  DriverConfigParser::get_defines() const {
  if (not root.has("defines")) {
    return {};
  }
  if (not root.isMapping("defines")) {
    warn("'defines' must be a mapping in configuration.");
    return {};
  }
  std::unordered_map<std::string,std::string> defines;
  const auto& map = root.mapping("defines");
  for (auto& [key, value] : map) {
    if (value->isValue()) {
      defines[key] = value->value();
    }
  }
  return defines;
}

std::pair<std::unordered_set<std::filesystem::path>, std::vector<std::string>>
  DriverConfigParser::get_filenames_and_patterns() const {
  if (not root.isSequence("files")) {
    return std::pair<std::unordered_set<std::filesystem::path>, std::vector<std::string>>();
  }
  std::unordered_set<std::filesystem::path> filenames;
  std::vector<std::string> files_patterns;
  for (auto& node : root.sequence("files")) {
    if (node->isValue()) {
      auto& pattern = node->value();
      files_patterns.push_back(pattern);
      auto paths = glob::rglob(pattern);
      if (paths.empty()) {
        /* print warning if pattern does not contain a wildcard '*' */
        if (pattern.find('*') == std::string::npos) {
          warn("no file matching '" << pattern << "' in configuration.");
        }
      } else for (auto path : paths) {
        auto result = filenames.insert(path.string());
        if (!result.second) {
          warn("file " << path << " appears more than once in configuration.");
        }
      }
    }
  }
  return std::make_pair(filenames, files_patterns);
}
