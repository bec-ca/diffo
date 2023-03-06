#include "diff.hpp"

#include "bee/string_util.hpp"
#include "bee/util.hpp"
#include "command/command_builder.hpp"
#include "command/group_builder.hpp"

using bee::print_line;
using command::Cmd;
using command::CommandBuilder;
using command::GroupBuilder;
using std::string;
using std::vector;

namespace diffo {
namespace {

struct Chunk {
  vector<DiffLine> lines;
};

vector<Chunk> chunkify(const vector<DiffLine>& diff)
{
  const ssize_t context = 5;
  std::vector<Chunk> chunks;

  bool in_chunk = false;
  ssize_t first_non_equal = 0;
  ssize_t last_non_equal;

  auto make_chunk = [&]() {
    ssize_t begin = std::max<ssize_t>(first_non_equal - context, 0);
    ssize_t end = std::min<ssize_t>(last_non_equal + context, diff.size());
    chunks.push_back({vector(diff.begin() + begin, diff.begin() + end)});
  };

  for (ssize_t i = 0; i <= std::ssize(diff); i++) {
    auto action = diff[i].action;
    if (!in_chunk) {
      if (action != Action::Equal) {
        first_non_equal = i;
        last_non_equal = i;
        in_chunk = true;
      }
    } else {
      if (action != Action::Equal) {
        last_non_equal = i;
      } else if (i >= last_non_equal + context) {
        make_chunk();
        in_chunk = false;
      }
    }
  }

  if (in_chunk) { make_chunk(); }

  return chunks;
}

void print_chunks_interleaved(const vector<Chunk>& chunks)
{
  string sep(80, '=');
  for (const auto& chunk : chunks) {
    print_line(sep);
    print_line("$:", chunk.lines.at(0).line_number);
    for (const auto& line : chunk.lines) {
      print_line("$ $", diffo::Diff::action_prefix(line.action), line.line);
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
}

void print_chunks_sxs(const vector<Chunk>& chunks)
{
  const ssize_t column_width = 100;

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
    print_line(sep);
    print_line("$:", chunk.lines.at(0).line_number);
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
      print_line("$|$", left_lines.at(i), right_lines.at(i));
    }
  }
}

bee::OrError<bee::Unit> run_diff(
  const string& left_file, const string& right_file, bool interleaved)
{
  bail(d, diffo::Diff::diff_files(left_file, right_file));
  auto chunks = chunkify(d);
  if (interleaved) {
    print_chunks_interleaved(chunkify(d));
  } else {
    print_chunks_sxs(chunkify(d));
  }
  return bee::unit;
}

Cmd diff_command()
{
  using namespace command::flags;
  auto builder = CommandBuilder("Print the diff of two files");
  auto interleaved = builder.no_arg("--interleaved");
  auto left_file = builder.required_anon(string_flag, "left-file");
  auto right_file = builder.required_anon(string_flag, "right-file");
  return builder.run(
    [=]() { return run_diff(*left_file, *right_file, *interleaved); });
}

Cmd command()
{
  return GroupBuilder("Mellow").cmd("diff", diff_command()).build();
}

} // namespace
} // namespace diffo

int main(int argc, char* argv[]) { return diffo::command().main(argc, argv); }
