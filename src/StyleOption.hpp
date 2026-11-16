#pragma once

#include <optional>
#include <string_view>

enum class StyleOption {
  MKDOCS,
  PANDOCS,
  PLAIN
};

std::optional<StyleOption> str_to_style_option(const std::string_view&);

std::string_view style_option_to_str(StyleOption);
