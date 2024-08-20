#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "bee/file_path.hpp"
#include "bee/or_error.hpp"

namespace diffo {

enum class Action : uint8_t {
  Undefined = 0,
  AddRight = 1,
  RemoveLeft = 2,
  Equal = 3,
};

struct DiffLine {
  template <class T>
  DiffLine(T&& line, Action action, ssize_t line_number)
      : line(std::forward<T>(line)), action(action), line_number(line_number)
  {}
  std::string line;
  Action action;
  ssize_t line_number;
};

struct Chunk {
  std::vector<DiffLine> lines;
};

struct Diff {
  struct Options {
    bool treat_missing_files_as_empty = false;
    int context_lines = 3;
    std::optional<ssize_t> agg = std::nullopt;
  };

  static constexpr Options DefaultOptions{
    .treat_missing_files_as_empty = false,
    .context_lines = 3,
    .agg = std::nullopt,
  };

  static const char* action_prefix(Action action);

  static std::vector<Chunk> diff_strings(
    const std::string& doc_left,
    const std::string& doc_right,
    const Options& options = DefaultOptions);

  static bee::OrError<std::vector<Chunk>> diff_files(
    const bee::FilePath& file_left,
    const bee::FilePath& file_right,
    const Options& options = DefaultOptions);
};

} // namespace diffo
