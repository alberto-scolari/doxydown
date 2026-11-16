#include "FileUtils.hpp"

#ifdef _WIN32
#include <locale>
#include <string>
#include <vector>

std::string to_string(const path_str_view& view) {
  if (view.empty()) {
    return "";
  }

  std::locale loc("en_US.UTF-8");
  using Facet = std::codecvt<wchar_t, char, std::mbstate_t>;
  const Facet& facet = std::use_facet<Facet>(loc);

  std::mbstate_t state = std::mbstate_t();
  std::vector<char> buffer(view.size() * facet.max_length());

  const wchar_t* from_next = nullptr;
  char* to_next = nullptr;

  auto result = facet.out(
      state,
      view.data(), view.data() + view.size(), from_next,
      buffer.data(), buffer.data() + buffer.size(), to_next
  );

  if (result != Facet::ok) {
      return ""; // Conversion error
  }

  return std::string(buffer.data(), to_next);
}
#else
std::string to_string(const path_str_view& view) {
  return std::string(view);
}
#endif

const std::filesystem::path DEF_ROOT_PATH = std::filesystem::path{"."} / "" ;

const std::filesystem::path SEPARATOR_PATH = std::filesystem::path{"/"}.make_preferred();
