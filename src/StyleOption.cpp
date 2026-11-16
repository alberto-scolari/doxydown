#include "StyleOption.hpp"

#include "Log.hpp"

std::optional<StyleOption> str_to_style_option(const std::string_view& style) {
  if (style == "mkdocs") {
    return StyleOption::MKDOCS;
  } else if (style == "pandoc") {
    return StyleOption::PANDOCS;
  } else if (style == "plain") {
    return StyleOption::PLAIN;
  }
  warn("unknown style '" << style << "', setting 'mkdocs'.");
  return {};
}

std::string_view style_option_to_str(StyleOption style) {
  // verbose implementation to silence MSVC
  std::string_view ret;
  switch (style) {
    case StyleOption::MKDOCS: {
      ret = "mkdocs";
      break;
    }
    case StyleOption::PANDOCS: {
      ret = "pandoc";
      break;
    }
    case StyleOption::PLAIN: {
      ret = "plain";
      break;
    }
  }
  return ret;
}
