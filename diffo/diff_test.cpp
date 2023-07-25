#include "diff.hpp"

#include "bee/string_util.hpp"
#include "bee/testing.hpp"
#include "bee/util.hpp"

using std::string;
using std::vector;

namespace diffo {
namespace {

void print_diff(const vector<DiffLine>& diff)
{
  if (diff.empty()) {
    P("No diff");
    return;
  }
  for (const auto& line : diff) {
    if (line.action == Action::Equal) { continue; }
    P("$:$ $", line.line_number, Diff::action_prefix(line.action), line.line);
  }
}

void diff_docs(const vector<string>& doc1, const vector<string>& doc2)
{
  string d1 = bee::join(doc1, "\n");
  string d2 = bee::join(doc2, "\n");
  print_diff(Diff::diff_strings(d1, d2));
}

TEST(basic) { diff_docs({"foo", "bar", "foobar"}, {"bar", "barfoo"}); }

TEST(larger)
{
  diff_docs(
    {
      "#include <something>",
      "int main() {",
      "int v = 5;",
      "printf(stuff);",
      "return 0;",
      "}",
    },
    {
      "#include <something>",
      "int main(int argc, char[][] argv) {",
      "int v = 5;",
      "printf(other_stuff);",
      "return 0;",
      "}",
    });
}

TEST(equal)
{
  diff_docs(
    {
      "#include <something>",
      "int main(int argc, char[][] argv) {",
      "int v = 5;",
      "printf(other_stuff);",
      "return 0;",
      "}",
    },
    {
      "#include <something>",
      "int main(int argc, char[][] argv) {",
      "int v = 5;",
      "printf(other_stuff);",
      "return 0;",
      "}",
    });
}

TEST(giant)
{
  vector<string> doc1;
  for (int i = 0; i < 20000; i++) { doc1.push_back(F(i)); }
  vector<string> doc2;
  bee::concat_many(doc2, "bye", "bye", "bye", doc1, "EOF", "EOF", "EOF");

  diff_docs(doc1, doc2);
}

TEST(giant_repeated)
{
  vector<string> doc1(20000, "hello");
  vector<string> doc2;
  bee::concat_many(doc2, "bye", doc1, "EOF", "EOF", "EOF");

  diff_docs(doc1, doc2);
}

} // namespace
} // namespace diffo
