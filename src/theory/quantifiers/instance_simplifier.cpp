#include "theory/quantifiers/instance_simplifier.hpp"

#include <algorithm>
#include <utility>

#include "core/binders.hpp"
#include "core/term_manager.hpp"

namespace primo::theory {

namespace {

bool opaque(const core::Term& node) {
  return node.children.empty() || node.kind == core::TermKind::Forall ||
         node.kind == core::TermKind::Exists || node.kind == core::TermKind::Pattern;
}

}  // namespace

InstanceSimplifier::InstanceSimplifier(core::TermManager& terms) : terms_(terms) {}

core::TermId InstanceSimplifier::simplify(core::TermId root) {
  if (const auto found = memo_.find(root); found != memo_.end()) {
    return found->second;
  }
  struct Frame {
    core::TermId term = core::kInvalidTermId;
    bool expanded = false;
  };
  std::vector<Frame> pending{{root, false}};
  while (!pending.empty()) {
    const Frame frame = pending.back();
    if (memo_.contains(frame.term)) {
      pending.pop_back();
      continue;
    }
    const core::Term& node = terms_.get(frame.term);
    if (opaque(node)) {
      memo_.emplace(frame.term, frame.term);
      pending.pop_back();
      continue;
    }
    if (!frame.expanded) {
      pending.back().expanded = true;
      const std::vector<core::TermId> children = node.children;
      for (auto child = children.rbegin(); child != children.rend(); ++child) {
        if (!memo_.contains(*child)) {
          pending.push_back({*child, false});
        }
      }
      continue;
    }
    pending.pop_back();
    const core::TermKind kind = node.kind;
    std::vector<core::TermId> children;
    children.reserve(node.children.size());
    for (const core::TermId child : node.children) {
      children.push_back(memo_.at(child));
    }
    const core::TermId result = rebuild(frame.term, kind, std::move(children));
    memo_.emplace(frame.term, result);
    // Results are fixed points, so a later occurrence needs no second pass.
    memo_.emplace(result, result);
  }
  return memo_.at(root);
}

core::TermId InstanceSimplifier::rebuild(core::TermId original, core::TermKind kind,
                                         std::vector<core::TermId> children) {
  switch (kind) {
    case core::TermKind::Not:
      return terms_.mk_not(children[0]);
    case core::TermKind::Implies:
      return connective(core::TermKind::Or, {terms_.mk_not(children[0]), children[1]});
    case core::TermKind::And:
    case core::TermKind::Or:
      return connective(kind, std::move(children));
    case core::TermKind::Equality:
      return equality(children[0], children[1]);
    case core::TermKind::Ite:
      return ite(children[0], children[1], children[2]);
    default:
      break;
  }
  const core::Term& node = terms_.get(original);
  if (std::equal(children.begin(), children.end(), node.children.begin(), node.children.end())) {
    return original;
  }
  return core::rebuild_operator(terms_, original, std::move(children));
}

core::TermId InstanceSimplifier::connective(core::TermKind kind,
                                            std::vector<core::TermId> operands) {
  // `true` absorbs a disjunction and `false` a conjunction; the other constant
  // is neutral.
  const bool absorbing = kind == core::TermKind::Or;
  std::vector<core::TermId> flat;
  std::vector<core::TermId> stack(operands.rbegin(), operands.rend());
  while (!stack.empty()) {
    const core::TermId current = stack.back();
    stack.pop_back();
    const core::Term& node = terms_.get(current);
    if (node.kind == core::TermKind::BoolValue) {
      if (node.bool_value == absorbing) {
        return terms_.mk_bool(absorbing);
      }
      continue;
    }
    if (node.kind == kind) {
      stack.insert(stack.end(), node.children.rbegin(), node.children.rend());
      continue;
    }
    flat.push_back(current);
  }
  std::sort(flat.begin(), flat.end());
  flat.erase(std::unique(flat.begin(), flat.end()), flat.end());
  for (const core::TermId operand : flat) {
    const core::Term& node = terms_.get(operand);
    if (node.kind == core::TermKind::Not &&
        std::binary_search(flat.begin(), flat.end(), node.children[0])) {
      return terms_.mk_bool(absorbing);
    }
  }
  if (flat.empty()) {
    return terms_.mk_bool(!absorbing);
  }
  if (flat.size() == 1) {
    return flat.front();
  }
  return kind == core::TermKind::And ? terms_.mk_and(std::move(flat))
                                     : terms_.mk_or(std::move(flat));
}

core::TermId InstanceSimplifier::equality(core::TermId lhs, core::TermId rhs) {
  if (lhs == rhs) {
    return terms_.mk_bool(true);
  }
  const core::Term& left = terms_.get(lhs);
  const core::Term& right = terms_.get(rhs);
  if (left.sort == terms_.symbols().bool_sort()) {
    if (left.kind == core::TermKind::BoolValue && right.kind == core::TermKind::BoolValue) {
      return terms_.mk_bool(false);  // distinct TermIds, so distinct values
    }
    if (left.kind == core::TermKind::BoolValue) {
      return left.bool_value ? rhs : terms_.mk_not(rhs);
    }
    if (right.kind == core::TermKind::BoolValue) {
      return right.bool_value ? lhs : terms_.mk_not(lhs);
    }
    if ((left.kind == core::TermKind::Not && left.children[0] == rhs) ||
        (right.kind == core::TermKind::Not && right.children[0] == lhs)) {
      return terms_.mk_bool(false);
    }
  }
  return lhs < rhs ? terms_.mk_eq(lhs, rhs) : terms_.mk_eq(rhs, lhs);
}

core::TermId InstanceSimplifier::ite(core::TermId condition, core::TermId then_term,
                                     core::TermId else_term) {
  // Canonical conditions are never doubly negated, so one step suffices.
  if (const core::Term& node = terms_.get(condition); node.kind == core::TermKind::Not) {
    condition = node.children[0];
    std::swap(then_term, else_term);
  }
  if (const core::Term& node = terms_.get(condition); node.kind == core::TermKind::BoolValue) {
    return node.bool_value ? then_term : else_term;
  }
  if (then_term == else_term) {
    return then_term;
  }
  const core::Term& then_node = terms_.get(then_term);
  const core::Term& else_node = terms_.get(else_term);
  if (then_node.sort == terms_.symbols().bool_sort()) {
    const bool then_constant = then_node.kind == core::TermKind::BoolValue;
    const bool else_constant = else_node.kind == core::TermKind::BoolValue;
    const bool then_value = then_constant && then_node.bool_value;
    const bool else_value = else_constant && else_node.bool_value;
    if (then_constant && else_constant) {
      return then_value ? condition : terms_.mk_not(condition);  // branches differ
    }
    if (then_constant) {
      return then_value ? connective(core::TermKind::Or, {condition, else_term})
                        : connective(core::TermKind::And, {terms_.mk_not(condition), else_term});
    }
    if (else_constant) {
      return else_value ? connective(core::TermKind::Or, {terms_.mk_not(condition), then_term})
                        : connective(core::TermKind::And, {condition, then_term});
    }
  }
  return terms_.mk_ite(condition, then_term, else_term);
}

}  // namespace primo::theory
