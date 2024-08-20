#include "diff.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include "bee/file_reader.hpp"
#include "bee/filesystem.hpp"
#include "bee/or_error.hpp"
#include "bee/string_util.hpp"
#include "bee/util.hpp"

using std::deque;
using std::make_pair;
using std::optional;
using std::reverse;
using std::string;
using std::vector;

namespace diffo {
namespace {

struct StringView {
 public:
  StringView(const char* begin) : _begin(begin) {}

  inline char operator[](ssize_t idx) const { return _begin[idx]; }

  inline bool operator==(const StringView& other) const
  {
    for (ssize_t i = 0;; i++) {
      if (_begin[i] != other._begin[i]) { return false; }
      if (is_end(_begin[i])) { return true; }
    }
  }

  inline operator std::string() const
  {
    for (const char* end = _begin;; end++) {
      if (is_end(*end)) { return std::string(_begin, end); }
    }
  }

  inline std::string to_string() const
  {
    for (const char* end = _begin;; end++) {
      if (is_end(*end)) { return std::string(_begin, end); }
    }
  }

 private:
  static inline bool is_end(char c) { return c == '\n' || c == 0; }
  const char* _begin;
};

struct DiffLineView {
  DiffLineView(const StringView& line, Action action, ssize_t line_number)
      : line(line), action(action), line_number(line_number)
  {}
  StringView line;
  Action action;
  ssize_t line_number;

  DiffLine to_diff_line() const { return {line, action, line_number}; }
};

vector<StringView> split_lines(const std::string& str)
{
  std::vector<StringView> output;
  size_t pos = 0;
  while (pos < str.size()) {
    output.emplace_back(&str[pos]);
    pos = str.find('\n', pos);
    if (pos == string::npos) { break; }
    pos++;
  }
  return output;
}

template <class T> struct DenseMap {
 public:
  DenseMap() {}

  inline T& get(ssize_t idx) { return _maybe_resize(idx); }

  ssize_t size() const { return _neg.size() + _pos.size(); }

  ssize_t begin_idx() const { return _idx_offset - _neg.size(); }
  ssize_t end_idx() const { return _idx_offset + _pos.size(); }

 private:
  T& _maybe_resize(ssize_t idx)
  {
    if (_pos.empty()) {
      _idx_offset = idx;
      _pos.emplace_back();
      return _pos.front();
    }
    idx -= _idx_offset;
    if (idx < 0) {
      idx = -idx - 1;
      if (idx >= std::ssize(_neg)) { _neg.resize(idx + 1); }
      return _neg[idx];
    } else {
      if (idx >= std::ssize(_pos)) { _pos.resize(idx + 1); }
      return _pos[idx];
    }
  }

  vector<T> _neg;
  vector<T> _pos;

  ssize_t _idx_offset = 0;
  const T _def;
};

template <class T> struct SimpleQueue {
 public:
  void push(const T& value) { _elements.push_back(value); }
  T pop()
  {
    auto ret = _elements.front();
    _elements.pop_front();
    return ret;
  }
  bool empty() const { return _elements.empty(); }

  ssize_t size() const { return _elements.size(); }

 private:
  deque<T> _elements;
};

struct NodeKey {
 public:
  NodeKey(ssize_t left, ssize_t right) : left(left), right(right) {}

  bool operator==(const NodeKey& other) const = default;

  inline NodeKey equal_action() const { return NodeKey(left + 1, right + 1); }

  inline NodeKey walk(Action action) const
  {
    switch (action) {
    case Action::Equal:
      return equal_action();
    case Action::RemoveLeft:
      return NodeKey(left + 1, right);
    case Action::AddRight:
      return NodeKey(left, right + 1);
    case Action::Undefined:
      assert(false);
    }
    assert(false);
  }

  NodeKey backout(Action action) const
  {
    switch (action) {
    case Action::Equal:
      return NodeKey(left - 1, right - 1);
    case Action::RemoveLeft:
      return NodeKey(left - 1, right);
    case Action::AddRight:
      return NodeKey(left, right - 1);
    case Action::Undefined:
      assert(false);
    }
    assert(false);
  }

  ssize_t left;
  ssize_t right;
};

struct PathStep {
 public:
  Action action;
  NodeKey key;
};

template <class T> struct BucketPriorityQueue {
  std::pair<T, ssize_t> pop()
  {
    while (!_queue.empty() && _queue.front().empty()) {
      _queue.pop_front();
      _queue_head++;
    }
    return make_pair(_queue.front().pop(), _queue_head);
  }

  void push(ssize_t dist, NodeKey key)
  {
    ssize_t idx = dist - _queue_head;
    if (idx >= std::ssize(_queue)) { _queue.resize(idx + 1); }
    _queue.at(idx).push(key);
  }

 private:
  deque<SimpleQueue<NodeKey>> _queue;
  ssize_t _queue_head = 0;
};

struct DenseActionMap {
  DenseActionMap() {}

  Action get(size_t idx) { return _map.get(idx / 32).get(idx % 32); }

  void set(size_t idx, Action action)
  {
    _map.get(idx / 32).set(idx % 32, action);
  }

 private:
  struct __attribute__((packed)) ActionBucket {
   public:
    void set(int idx, Action action)
    {
      _bucket |= uint64_t(action) << (idx * 2);
    }
    Action get(int idx) const { return Action((_bucket >> (idx * 2)) & 3); }

   private:
    uint64_t _bucket = 0;
  };

  DenseMap<ActionBucket> _map;
};

struct StateTable {
  StateTable() {}

  Action get(NodeKey key)
  {
    ssize_t idx1 = key.right - key.left;
    ssize_t idx2 = key.right;
    return _state_table.get(idx1).get(idx2);
  }

  void set(NodeKey key, Action action)
  {
    ssize_t idx1 = key.right - key.left;
    ssize_t idx2 = key.right;
    _state_table.get(idx1).set(idx2, action);
  }

 private:
  DenseMap<DenseActionMap> _state_table;
};

vector<Action> find_best_diff(
  const vector<StringView>& doc_left,
  const vector<StringView>& doc_right,
  const std::optional<ssize_t>& agg)
{
  ssize_t size_left = doc_left.size();
  ssize_t size_right = doc_right.size();

  StateTable states;

  BucketPriorityQueue<NodeKey> queue;

  auto is_equal = [&](const NodeKey& key) {
    return key.left < size_left && key.right < size_right &&
           doc_left.at(key.left) == doc_right.at(key.right);
  };
  ssize_t furthest_key = 0;

  auto maybe_enqueue =
    [&](NodeKey key, const ssize_t dist, const Action action) {
      if (states.get(key) != Action::Undefined) return;
      states.set(key, action);

      while (is_equal(key)) {
        key = key.equal_action();
        if (states.get(key) != Action::Undefined) return;
        states.set(key, Action::Equal);
      }

      ssize_t key_dist = key.left + key.right;
      if (key_dist > furthest_key) {
        furthest_key = key_dist;
      } else if (agg && furthest_key - key_dist > *agg) {
        return;
      }

      queue.push(dist, key);
    };

  const NodeKey origin_key(0, 0);
  const NodeKey goal_key(size_left, size_right);
  maybe_enqueue(origin_key, 0, Action::Undefined);

  ssize_t final_edit_dist;
  while (true) {
    auto el = queue.pop();
    auto key = el.first;
    auto dist = el.second;
    if (key == goal_key) {
      final_edit_dist = dist;
      break;
    }

    auto take_action = [&](Action action, ssize_t dist) {
      auto neighbor_key = key.walk(action);
      return maybe_enqueue(neighbor_key, dist, action);
    };

    if (key.left < size_left) { take_action(Action::RemoveLeft, dist + 1); }
    if (key.right < size_right) { take_action(Action::AddRight, dist + 1); }
  }

  vector<Action> path;
  path.reserve(final_edit_dist + doc_left.size());
  NodeKey key = goal_key;
  while (key != origin_key) {
    auto action = states.get(key);
    key = key.backout(action);
    path.push_back(action);
  }
  reverse(path.begin(), path.end());

  return path;
}

vector<Chunk> slow_diff(
  const vector<StringView>& doc_left,
  const vector<StringView>& doc_right,
  const Diff::Options& options)
{
  auto min_path = find_best_diff(doc_left, doc_right, options.agg);

  std::vector<Chunk> output;
  NodeKey key(0, 0);
  bool in_chunk = false;
  int context_count = 0;
  std::deque<DiffLineView> chunk_buffer;

  auto make_chunk = [&]() {
    output.emplace_back();
    auto& chunk = output.back();
    chunk.lines.reserve(chunk_buffer.size());
    for (auto& dlv : chunk_buffer) {
      chunk.lines.emplace_back(dlv.line, dlv.action, dlv.line_number);
    }
    chunk_buffer.clear();
    context_count = 0;
    in_chunk = false;
  };

  for (auto action : min_path) {
    ssize_t line_number = 0;
    optional<StringView> line;
    switch (action) {
    case Action::Equal:
      line_number = key.left + 1;
      line = doc_left[key.left];
      break;
    case Action::RemoveLeft:
      line_number = key.left + 1;
      line = doc_left[key.left];
      break;
    case Action::AddRight:
      line_number = key.left + 1;
      line = doc_right[key.right];
      break;
    case Action::Undefined:
      assert(false && "This shouldn't happen");
    };
    key = key.walk(action);

    if (
      action == Action::Equal && in_chunk &&
      context_count >= options.context_lines) {
      make_chunk();
    }

    chunk_buffer.emplace_back(*line, action, line_number);
    if (action != Action::Equal) {
      in_chunk = true;
      context_count = 0;
    } else if (in_chunk) {
      context_count++;
    } else if (std::ssize(chunk_buffer) > options.context_lines) {
      chunk_buffer.pop_front();
    }
  }

  if (in_chunk) { make_chunk(); }

  return output;
}

bee::OrError<std::string> read_file(
  const bee::FilePath& file_path, bool treat_missing_files_as_empty)
{
  if (treat_missing_files_as_empty && !bee::FileSystem::exists(file_path)) {
    return "";
  }
  bail(content, bee::FileReader::read_file(file_path));
  if (content.empty() || content.back() != '\n') { content += '\n'; }
  return std::move(content);
}

} // namespace

const char* Diff::action_prefix(Action action)
{
  switch (action) {
  case Action::AddRight:
    return "+";
  case Action::RemoveLeft:
    return "-";
  case Action::Equal:
    return " ";
  case Action::Undefined:
    return "?";
  }
  assert(false);
}

vector<Chunk> Diff::diff_strings(
  const string& doc_left, const string& doc_right, const Diff::Options& options)
{
  if (doc_left == doc_right) { return {}; }
  auto left = split_lines(doc_left);
  auto right = split_lines(doc_right);
  return slow_diff(left, right, options);
}

bee::OrError<vector<Chunk>> Diff::diff_files(
  const bee::FilePath& file1,
  const bee::FilePath& file2,
  const Options& options)
{
  bail(doc_left, read_file(file1, options.treat_missing_files_as_empty));
  bail(doc_right, read_file(file2, options.treat_missing_files_as_empty));
  return Diff::diff_strings(doc_left, doc_right, options);
}

} // namespace diffo
