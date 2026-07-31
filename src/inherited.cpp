#include "fltr/widgets/inherited.hpp"

#include <cstdint>

namespace fltr {
namespace {

/// A type tag is the address of a per-type object, so the low bits carry no
/// information; multiply and take the high ones.
std::size_t probeStart(WidgetType type, std::size_t mask) noexcept {
  const auto bits = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(type));
  return static_cast<std::size_t>((bits * 0x9E3779B97F4A7C15ull) >> 32) & mask;
}

}  // namespace

void InheritedScope::extend(const InheritedScope* parent, WidgetType type,
                            InheritedElementBase& node) {
  const std::size_t entries = (parent ? parent->count_ : 0) + 1;
  std::size_t capacity = 4;
  while (capacity < entries * 2) capacity *= 2;

  table_.assign(capacity, Entry{});
  count_ = 0;
  if (parent) {
    for (const Entry& entry : parent->table_) {
      if (entry.type) insert(entry.type, *entry.node);
    }
  }
  insert(type, node);
}

void InheritedScope::insert(WidgetType type, InheritedElementBase& node) noexcept {
  const std::size_t mask = table_.size() - 1;
  std::size_t i = probeStart(type, mask);
  while (table_[i].type != nullptr && table_[i].type != type) i = (i + 1) & mask;
  if (table_[i].type == nullptr) ++count_;
  table_[i] = {type, &node};
}

InheritedElementBase* InheritedScope::find(WidgetType type) const noexcept {
  if (table_.empty()) return nullptr;
  const std::size_t mask = table_.size() - 1;
  for (std::size_t i = probeStart(type, mask); table_[i].type != nullptr; i = (i + 1) & mask) {
    if (table_[i].type == type) return table_[i].node;
  }
  return nullptr;
}

InheritedElementBase* Element::dependOnInherited(WidgetType type) {
  FLTR_EXPECTS(mounted(),
               "an ambient value may only be read once the element is mounted, so not from "
               "initState");
  InheritedElementBase* node = inheritedScope_ ? inheritedScope_->find(type) : nullptr;
  if (node == nullptr) return nullptr;

  for (const Subscription& dependency : dependencies_) {
    if (dependency.source() == node) return node;
  }
  subscribeMember<Element, &Element::markNeedsBuild>(*node, dependencies_.emplace_back(), this);
  return node;
}

}  // namespace fltr
