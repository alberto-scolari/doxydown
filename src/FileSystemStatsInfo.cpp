#include "FileSystemStatsInfo.hpp"

#include <cstddef>
#include <format>
#include <stdexcept>
#include <utility>

#ifdef DXM_DEBUG
#include <iostream>
#endif //DXM_DEBUG

std::optional<FwdRef<FileEntity>> FileSystemStatsInfo::do_lookup_file(path_str_view path) {
  auto it = files_map.find(path);
  if (it == files_map.end()) {
    return {};
  }
  return it->second;
}

std::optional<std::reference_wrapper<FileSystemStatsInfo::DirInfo>> FileSystemStatsInfo::lookup_dir(path_str_view path) {
  auto it = dir_info_map.find(path);
  if (it == dir_info_map.end()) {
    return {};
  }
  return it->second;
}

std::optional<std::reference_wrapper<const FileSystemStatsInfo::DirInfo>> FileSystemStatsInfo::lookup_dir(path_str_view path) const {
  auto it = dir_info_map.find(path);
  if (it == dir_info_map.cend()) {
    return {};
  }
  return it->second;
}

void FileSystemStatsInfo::add_file(const std::filesystem::path& path, FwdRef<FileEntity> file) {
  namespace fs = std::filesystem;

  if (path.empty() or not path.has_filename()) {
    throw std::runtime_error(std::format("empty file path or no filename"));
  }

  fs::path normalized = path.lexically_normal();
  auto [_, newly_created] = files_map.try_emplace(file->get_path().native(), file);
  if (not newly_created) {
    throw std::runtime_error(std::format("file '{}' was already added", normalized));
  }

  auto get_or_create_path = [&](fs::path&& path) {
    auto it = dir_info_map.find(path);
    if (it == dir_info_map.end()) {
      auto [pit, _] = dir_info_map.try_emplace(path);
      it = pit;
    }
    return it;
  };

  fs::path parent = normalized.parent_path();
  const bool no_parent = parent.empty();
  auto it = get_or_create_path(not no_parent ?
    std::move(parent) : fs::path{DEF_ROOT_PATH});
  it->second.files.insert(file);
  if (no_parent) {
    return;
  }

  parent = parent.parent_path();
  while(not parent.empty()) {
    auto pit = get_or_create_path(std::move(parent));
    pit->second.subdirs.insert(it->first);
    it = pit;
    parent = parent.parent_path();
  }

  auto pit = get_or_create_path(fs::path{DEF_ROOT_PATH});
  pit->second.subdirs.insert(it->first);
}

#ifdef DXM_DEBUG
void FileSystemStatsInfo::explore() const {
  std::cout << "DIRS\n";
  for(const auto& [k, v]: dir_info_map) {
    std::cout << "----- " << k << ":\n";
    const auto& stats = lookup_dir(k)->get().stats;
    std::cout << "    - covered_lines " << stats.covered_lines << std::endl;
    std::cout << "    - included_lines " << stats.included_lines << std::endl;
    for(const auto& f: v.subdirs) {
      std::cout << "  dir: " << f << "\n";
      const auto& stats = lookup_dir(f)->get().stats;
      std::cout << "    - covered_lines " << stats.covered_lines << std::endl;
      std::cout << "    - included_lines " << stats.included_lines << std::endl;
    }
    for(const auto& f: v.files) {
      std::cout << "  file: " << f->get_name() << "\n";
      std::cout << "    - covered_lines " << f->get_covered_lines() << std::endl;
      std::cout << "    - included_lines " << f->get_included_lines() << std::endl;
    }
    std::cout << "\n";
  }
  std::cout << "FILES\n";
  for(const auto& [k, _]: files_map) {
    std::cout << "----- " << k << "\n";
  }
}
#endif //DXM_DEBUG

void FileSystemStatsInfo::remove_file(const std::filesystem::path& filepath) {
  namespace fs = std::filesystem;
  fs::path normalized = filepath.lexically_normal();
  auto file_it = files_map.find(normalized.native());
  if (file_it == files_map.end()) {
    throw std::runtime_error(std::format("cannot find file name \"{}\"", filepath));
  }
  FwdRef<FileEntity> fe = file_it->second;
  files_map.erase(file_it);

  auto get_opt_path = [&](const std::filesystem::path& path) {
    auto it = dir_info_map.find(path);
    if (it == dir_info_map.end()) {
      return std::optional<decltype(it)>();
    }
    return std::optional(it);
  };

  fs::path parent = normalized.parent_path();
  bool is_root = parent.empty();
  auto parent_it_opt = get_opt_path(parent);
  if (parent_it_opt) {
    (*parent_it_opt)->second.files.erase(fe);
  }
  while(parent_it_opt and (*parent_it_opt)->second.files.empty()
    and (*parent_it_opt)->second.subdirs.empty() and not is_root) {

    parent = (*parent_it_opt)->first.parent_path();
    is_root = parent.empty() or parent == SEPARATOR_PATH;
    fs::path p = is_root ? DEF_ROOT_PATH : parent;
    auto current_it = *parent_it_opt;
    parent_it_opt = get_opt_path(p);
    if (parent_it_opt) {
      (*parent_it_opt)->second.subdirs.erase(current_it->first);
    }
    dir_info_map.erase(current_it);
  }
}
