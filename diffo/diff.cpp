#include "diff.hpp"

#include <algorithm>
#include <deque>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "bee/error.hpp"
#include "bee/file_reader.hpp"
#include "bee/format.hpp"
#include "bee/format_vector.hpp"
#include "bee/string_util.hpp"
#include "bee/util.hpp"

using bee::FilePath;
using std::deque;
using std::make_pair;
using std::optional;
using std::reverse;
using std::string;
using std::unordered_set;
using std::vector;

namespace diffo {

namespace {

template <class T> struct DenseMap {
 public:
  DenseMap(T def = T()) : _def(def) {}
  inline void set(ssize_t idx, const T& value)
  {
    _values[_maybe_resize(idx)] = value;
  }

  inline T& get(ssize_t idx) { return _values[_maybe_resize(idx)]; }

  auto begin() const { return _values.begin(); }
  auto end() const { return _values.end(); }

  ssize_t size() const { return _values.size(); }

 private:
  ssize_t _maybe_resize(ssize_t idx)
  {
    if (_values.empty()) {
      _idx_offset = idx;
      _values.push_back(_def);
      return 0;
    }
    idx -= _idx_offset;
    while (idx < 0) {
      _values.push_front(_def);
      idx++;
      _idx_offset--;
    }
    while (idx >= std::ssize(_values)) { _values.push_back(_def); }
    return idx;
  }

  deque<T> _values;
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

vector<ssize_t> line_cost(const vector<string>& on, const vector<string>& other)
{
  unordered_set<string> exclude_set(other.begin(), other.end());
  vector<ssize_t> output;
  for (const auto& s : on) {
    ssize_t cost = 1;
    if (!exclude_set.contains(s)) { cost = 0; }
    output.push_back(cost);
  }
  return output;
}

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

struct StateTable {
  StateTable(ssize_t max_key) : _state_table(max_key / 32 + 1) {}

  Action get(NodeKey key)
  {
    return _state_table.at(key.left / 32).get(key.right).get(key.left % 32);
  }

  void set(NodeKey key, Action action)
  {
    _state_table.at(key.left / 32).get(key.right).set(key.left % 32, action);
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

  vector<DenseMap<ActionBucket>> _state_table;
};

vector<PathStep> find_best_diff(
  const vector<string>& doc_left, const vector<string>& doc_right)
{
  ssize_t size_left = doc_left.size();
  ssize_t size_right = doc_right.size();

  auto line_cost_left = line_cost(doc_left, doc_right);
  auto line_cost_right = line_cost(doc_right, doc_left);

  StateTable states(size_left);

  BucketPriorityQueue<NodeKey> queue;

  auto is_equal = [&](const NodeKey& key) {
    return key.left < size_left && key.right < size_right &&
           doc_left.at(key.left) == doc_right.at(key.right);
  };
  auto maybe_enqueue =
    [&](NodeKey key, const ssize_t dist, const Action action) {
      if (states.get(key) != Action::Undefined) return;
      states.set(key, action);

      while (is_equal(key)) {
        key = key.equal_action();
        if (states.get(key) != Action::Undefined) return;
        states.set(key, Action::Equal);
      }

      queue.push(dist, key);
    };

  const NodeKey origin_key(0, 0);
  const NodeKey goal_key(size_left, size_right);
  maybe_enqueue(origin_key, 0, Action::Undefined);

  while (true) {
    auto el = queue.pop();
    auto key = el.first;
    auto dist = el.second;
    if (key == goal_key) { break; }

    auto take_action = [&](Action action, ssize_t dist) {
      auto neighbor_key = key.walk(action);
      return maybe_enqueue(neighbor_key, dist, action);
    };

    if (key.left < size_left) {
      take_action(Action::RemoveLeft, dist + line_cost_left[key.left]);
    }
    if (key.right < size_right) {
      take_action(Action::AddRight, dist + line_cost_right[key.right]);
    }
  }

  vector<PathStep> path;
  NodeKey key = goal_key;
  while (key != origin_key) {
    auto action = states.get(key);
    key = key.backout(action);
    path.push_back(PathStep{.action = action, .key = key});
  }
  reverse(path.begin(), path.end());

  return path;
}

vector<DiffLine> slow_diff(
  const vector<string>& doc_left, const vector<string>& doc_right)
{
  vector<DiffLine> output;

  auto min_path = find_best_diff(doc_left, doc_right);

  for (auto& step : min_path) {
    ssize_t line_number = 0;
    optional<string> line;
    Action action = step.action;
    NodeKey key = step.key;
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
      line_number = key.left;
      line = doc_right[key.right];
      break;
    case Action::Undefined:
      assert(false && "This shouldn't happen");
    };
    output.push_back(DiffLine{
      .line = std::move(*line), .action = action, .line_number = line_number});
  }

  return output;
}

} // namespace

string Diff::action_prefix(Action action)
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

vector<DiffLine> Diff::diff_strings(
  const string& doc_left, const string& doc_right)
{
  if (doc_left == doc_right) { return {}; }
  return slow_diff(bee::split(doc_left, "\n"), bee::split(doc_right, "\n"));
}

bee::OrError<vector<DiffLine>> Diff::diff_files(
  const string& file1, const string& file2)
{
  bail(doc_left, bee::FileReader::read_file(FilePath::of_string(file1)));
  bail(doc_right, bee::FileReader::read_file(FilePath::of_string(file2)));
  return Diff::diff_strings(doc_left, doc_right);
}

} // namespace diffo
