#include "diff.hpp"

#include "bee/print.hpp"
#include "bee/string_util.hpp"
#include "bee/util.hpp"
#include "command/command_builder.hpp"
#include "command/file_path.hpp"
#include "command/group_builder.hpp"

using std::string;
using std::vector;

namespace diffo {
namespace {

void print_chunks_interleaved(const vector<Chunk>& chunks)
{
  string sep(80, '=');
  for (const auto& chunk : chunks) {
    P(sep);
    P("$:", chunk.lines.at(0).line_number);
    for (const auto& line : chunk.lines) {
      P("$ $", diffo::Diff::action_prefix(line.action), line.line);
    }
  }
}

string replace_tab_with_spaces(const string& input)
{
  string output;
  for (char c : input) {
    if (c == '\t') {
      int spaces = 8 - ((output.size() + 8) % 8);
      output += string(spaces, ' ');
    } else {
      output += c;
    }
  }
  return output;
}

string no_color = "\e[0m";

string action_color(Action action)
{
  switch (action) {
  case Action::RemoveLeft:
    return "\e[31m";
  case Action::AddRight:
    return "\e[32m";
  case Action::Equal:
    return no_color;
  case Action::Undefined:
    assert(false);
  }
  assert(false);
}

void print_chunks_sxs(const vector<Chunk>& chunks)
{
  const ssize_t column_width = 50;

  auto format_line = [&](Action action, string line) {
    string prefix = Diff::action_prefix(action);
    string color = action_color(action);
    line = replace_tab_with_spaces(line);
    vector<string> output;
    auto format_one = [&](const string& str) {
      string l;
      l += color;
      l += prefix;
      l += ' ';
      l += bee::right_pad_string(str, column_width - 2);
      l += no_color;
      return l;
    };
    if (line.empty()) {
      output.push_back(format_one(""));
    } else {
      for (ssize_t i = 0; i < std::ssize(line);) {
        auto size = std::min(column_width - 4, std::ssize(line) - i);
        output.push_back(format_one(line.substr(i, size)));
        i += size;
      }
    }
    return output;
  };

  auto equalize_one = [&](vector<string>& lines, const vector<string>& with) {
    while (lines.size() < with.size())
      bee::concat(lines, format_line(Action::Equal, ""));
  };

  auto equalize = [&](vector<string>& left, vector<string>& right) {
    equalize_one(left, right);
    equalize_one(right, left);
  };

  string sep(column_width * 2 + 1, '=');
  for (const auto& chunk : chunks) {
    P(sep);
    P("$:", chunk.lines.at(0).line_number);
    vector<string> left_lines;
    vector<string> right_lines;
    for (const auto& line : chunk.lines) {
      vector<string> lines = format_line(line.action, line.line);
      switch (line.action) {
      case Action::AddRight:
        bee::concat(right_lines, lines);
        break;
      case Action::RemoveLeft:
        bee::concat(left_lines, lines);
        break;
      case Action::Equal:
        equalize(left_lines, right_lines);
        bee::concat(right_lines, lines);
        bee::concat(left_lines, lines);
        break;
      case Action::Undefined:
        assert(false);
      }
    }
    equalize(left_lines, right_lines);
    assert(left_lines.size() == right_lines.size());
    for (ssize_t i = 0; i < std::ssize(right_lines); i++) {
      P("$|$", left_lines.at(i), right_lines.at(i));
    }
  }
}

bee::OrError<> run_diff(
  const bee::FilePath& left_file,
  const bee::FilePath& right_file,
  bool interleaved,
  const std::optional<ssize_t>& agg)
{
  bail(chunks, diffo::Diff::diff_files(left_file, right_file, {.agg = agg}));
  size_t diff_size = 0;
  for (auto&& chunk : chunks) {
    for (auto&& line : chunk.lines) {
      if (line.action != Action::Equal) { diff_size++; }
    }
  }
  if (diff_size > 0) { P("Diff size: {,}", diff_size); }
  if (interleaved) {
    print_chunks_interleaved(chunks);
  } else {
    print_chunks_sxs(chunks);
  }
  return bee::ok();
}

command::Cmd diff_command()
{
  using namespace command;
  auto builder = CommandBuilder("Print the diff of two files");
  auto interleaved = builder.no_arg("--interleaved");
  auto left_file = builder.required_anon(flags::FilePath, "left-file");
  auto right_file = builder.required_anon(flags::FilePath, "right-file");
  auto agg = builder.optional_with_default("--agg", flags::Int, 1000);
  return builder.run(
    [=]() { return run_diff(*left_file, *right_file, *interleaved, *agg); });
}

command::Cmd command()
{
  return command::GroupBuilder("Mellow").cmd("diff", diff_command()).build();
}

} // namespace
} // namespace diffo

int main(int argc, char* argv[]) { return diffo::command().main(argc, argv); }
