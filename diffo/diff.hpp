#pragma once

#include "bee/error.hpp"

#include <string>
#include <vector>

namespace diffo {

enum class Action : uint8_t {
  Undefined = 0,
  AddRight = 1,
  RemoveLeft = 2,
  Equal = 3,
};

struct DiffLine {
  std::string line;
  Action action;
  ssize_t line_number;
};

struct Diff {
  static std::string action_prefix(Action action);

  static bee::OrError<std::vector<DiffLine>> diff_files(
    const std::string& file_left, const std::string& file_right);

  static std::vector<DiffLine> diff_strings(
    const std::string& doc_left, const std::string& doc_right);
};

} // namespace diffo
