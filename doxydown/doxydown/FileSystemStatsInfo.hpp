#pragma once

#include <doxydown/FileUtils.hpp>
#include <doxydown/entities/FileEntity.hpp>
#include <doxydown/entities/FwdRef.hpp>
#include <doxydown/entities/LineStats.hpp>

#include <filesystem>
#include <format>
#include <functional>
#include <optional>
#include <unordered_set>
#include <stdexcept>
#include <tuple>
#include <type_traits>

class FileSystemStatsInfo {
public:

  FileSystemStatsInfo() = default;

  FileSystemStatsInfo(const FileSystemStatsInfo&) = delete;

  FileSystemStatsInfo& operator=(const FileSystemStatsInfo&) = delete;

  struct DirInfo {
    std::unordered_set<path_ref> subdirs;
    std::unordered_set<FwdRef<FileEntity>> files;
    LineStats stats;

    DirInfo() = default;
  };

  std::optional<FwdRef<FileEntity>> lookup_file(path_str_view path) {
    return do_lookup_file(path);
  }

  std::optional<FwdRef<FileEntity>> lookup_file(const std::filesystem::path& path) {
    return do_lookup_file(path.native());
  }

  std::optional<std::reference_wrapper<DirInfo>> lookup_dir(path_str_view path);

  std::optional<std::reference_wrapper<const DirInfo>> lookup_dir(path_str_view path) const;

  inline std::optional<std::reference_wrapper<DirInfo>> lookup_dir(const std::filesystem::path& path) {
    path_str_view v{path.native()};
    return lookup_dir(v);
  }

  inline std::optional<std::reference_wrapper<const DirInfo>> lookup_dir(const std::filesystem::path& path) const {
    path_str_view v{path.native()};
    return lookup_dir(v);
  }

  void add_file(const std::filesystem::path& path, FwdRef<FileEntity> file);

  void explore() const;

  bool contains(const std::filesystem::path& p) const {
    return files_map.contains(p);
  }

  void remove_file(const std::filesystem::path& filepath);

  void clear() {
    dir_info_map.clear();
    files_map.clear();
  }

private:
  path_unordered_map<std::filesystem::path, DirInfo> dir_info_map;
  path_unordered_map<path_str_view, FwdRef<FileEntity>> files_map;

  std::optional<FwdRef<FileEntity>> do_lookup_file(path_str_view path);
};

template<bool IS_CONST> struct _DirStackController {

  using _dirinfo_t = std::conditional_t<IS_CONST,
    const FileSystemStatsInfo::DirInfo, FileSystemStatsInfo::DirInfo>;
  using _fsinfo_t = std::conditional_t<IS_CONST,
    const FileSystemStatsInfo, FileSystemStatsInfo>;

  using iter_t = std::conditional_t<IS_CONST,
    std::unordered_set<path_ref>::const_iterator,
    std::unordered_set<path_ref>::iterator>;
  using stack_value = std::tuple<path_ref, _dirinfo_t&, iter_t>;
  using yield_value = std::tuple<const std::filesystem::path&, _dirinfo_t&>;

  _fsinfo_t& fsinfo;
  std::filesystem::path root_dir = DEF_ROOT_PATH;

  stack_value make_init() {
    auto opt = fsinfo.lookup_dir(root_dir);
    if (not opt) {
      throw std::runtime_error(std::format("cannot initialize from {}", root_dir));
    }
    _dirinfo_t& d = opt->get();
    return stack_value{root_dir, d, d.subdirs.begin()};
  }

  yield_value make_yield_pre(const stack_value& top) {
    return yield_value{std::get<0>(top), std::get<1>(top)};
  }

  yield_value make_yield_post(const stack_value& top) {
    return make_yield_pre(top);
  }

  bool has_next(const stack_value& top) const {
    return std::get<2>(top) != std::get<1>(top).subdirs.end();
  }

  stack_value make_next(stack_value& top) {
    iter_t& it = std::get<2>(top);
    const path_ref& next_path = *it;
    ++it;
    auto opt = fsinfo.lookup_dir(next_path);
    if (not opt) {
      throw std::runtime_error(std::format("cannot make next from {}", root_dir));
    }
    _dirinfo_t& d = opt->get();
    return stack_value{next_path, d, d.subdirs.begin()};
  }

  void after_pop(const stack_value&) {}
};

using DirStackController = _DirStackController<false>;
using ConstDirStackController = _DirStackController<true>;
