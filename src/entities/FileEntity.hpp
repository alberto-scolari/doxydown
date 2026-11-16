#pragma once

#include "Documentable.hpp"
#include "EntityFormatter.hpp"
#include "FwdRef.hpp"
#include "LineStats.hpp"

#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

struct FileStorage {
  std::filesystem::path file_path;
  std::string file_path_str;

  FileStorage(const std::filesystem::path& path):
    file_path{path}, file_path_str{path.string()} {}
};

class FileEntity: protected FileStorage, public Documentable, public LineStats {
public:
  constexpr FileEntity() = delete;

  FileEntity(const std::filesystem::path& path, const std::string& content, uint32_t num_lines):
    FileStorage{path}, Documentable(this->file_path_str, Doc(), true), content(content), num_lines(num_lines) {}

  FileEntity(const std::filesystem::path& path, std::string&& content, uint32_t num_lines):
    FileStorage{path}, Documentable(this->file_path_str, Doc(), true), content(std::move(content)), num_lines(num_lines) {}

  const std::filesystem::path& get_path() const noexcept { return file_path; }

  std::string_view get_path_str() const noexcept { return file_path_str; }

  std::filesystem::path get_filename() const noexcept { return file_path.filename(); }

  const std::string_view get_content() const noexcept { return content; }

  uint32_t get_num_lines() const noexcept { return num_lines; }

  auto& get_line_counts() noexcept {
    if (line_counts.size() < get_num_lines()) {
      line_counts.resize(get_num_lines(), -1);
    }
    return line_counts;
  }

  const auto& get_line_counts() const noexcept { return line_counts; }

  auto& get_included_lines() noexcept { return this->included_lines; }

  auto get_included_lines() const noexcept { return this->included_lines; }

  void increment_covered_lines() noexcept { ++this->covered_lines; }

  auto get_covered_lines() const noexcept { return this->covered_lines; }

private:
  std::string content;
  uint32_t num_lines;

  /**
   * Execution counts for lines. -1 for a line indicates that
   * it is excluded.
   */
  std::vector<int> line_counts;
};

template<> struct std::formatter<FwdRef<FileEntity>, char>: StatelessEntityFormatter {
  template<class FmtContext> constexpr FmtContext::iterator format(const FwdRef<FileEntity>& f, FmtContext& ctx) const {
    return std::format_to(ctx.out(), "FileEntity[{}]", f->get_path_str());
  }
};
