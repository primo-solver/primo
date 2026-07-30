#include "smtlib/ast.hpp"

#include <utility>
#include <vector>

namespace primo::smtlib {

namespace {

// Iterative (explicit-stack) deep copy of an SExpr tree, used by SExpr's
// copy constructor/assignment instead of the compiler-generated recursive
// version. One Frame per list node currently being rebuilt, mirroring
// smtlib::parser.cpp's iterative parse_all().
SExpr deep_copy(const SExpr& root) {
  if (root.kind == SExpr::Kind::Atom) {
    return SExpr::make_atom(root.atom);
  }
  struct Frame {
    const SExpr* source;
    std::vector<SExpr> built;
    std::size_t next_child = 0;
  };
  std::vector<Frame> stack;
  stack.push_back(Frame{&root, {}, 0});
  while (true) {
    Frame& frame = stack.back();
    if (frame.next_child < frame.source->list.size()) {
      const SExpr& child = frame.source->list[frame.next_child];
      ++frame.next_child;
      if (child.kind == SExpr::Kind::Atom) {
        frame.built.push_back(SExpr::make_atom(child.atom));
      } else {
        stack.push_back(Frame{&child, {}, 0});
      }
      continue;
    }
    SExpr built_list = SExpr::make_list(std::move(frame.built));
    stack.pop_back();
    if (stack.empty()) {
      return built_list;
    }
    stack.back().built.push_back(std::move(built_list));
  }
}

}  // namespace

SExpr::SExpr(const SExpr& other) : SExpr(deep_copy(other)) {}

SExpr& SExpr::operator=(const SExpr& other) {
  if (this != &other) {
    *this = deep_copy(other);
  }
  return *this;
}

// Iterative (explicit-stack) destruction: drains this node's children into
// a flat work queue up front (leaving `list` empty, so the compiler's
// member-wise teardown after this body runs is trivial), then repeatedly
// pops one node, moves *its* children into the same queue before it goes
// out of scope, and lets it destruct with an already-empty `list` -- no
// element's destructor ever has non-empty children left to recurse into.
SExpr::~SExpr() {
  if (list.empty()) {
    return;
  }
  std::vector<SExpr> queue;
  queue.swap(list);
  while (!queue.empty()) {
    SExpr node = std::move(queue.back());
    queue.pop_back();
    if (!node.list.empty()) {
      for (SExpr& child : node.list) {
        queue.push_back(std::move(child));
      }
      node.list.clear();
    }
  }
}

SExpr SExpr::make_atom(std::string value) {
  SExpr expr;
  expr.kind = Kind::Atom;
  expr.atom = std::move(value);
  return expr;
}

SExpr SExpr::make_list(std::vector<SExpr> elements) {
  SExpr expr;
  expr.kind = Kind::List;
  expr.list = std::move(elements);
  return expr;
}

bool SExpr::is_atom(const std::string& value) const { return kind == Kind::Atom && atom == value; }

}  // namespace primo::smtlib
