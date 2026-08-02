#include "fltr/focus/node.hpp"

#include <algorithm>

#include "fltr/focus/manager.hpp"
#include "fltr/render/box.hpp"
#include "fltr/widgets/framework.hpp"

namespace fltr {

FocusNode::~FocusNode() {
  // A consumer's scope node may well die before the tree that named it, so the
  // manager loses its root here rather than dereferencing one that is gone.
  if (manager_) manager_->forgetRoot(*this);
  detach();
  // Orphan whatever is still below, so a child destroyed after its parent finds
  // no dangling link back. Each detach unlinks itself from `children_`.
  while (!children_.empty()) children_.back()->detach();
}

void FocusNode::attach(FocusNode& parent) {
  FLTR_EXPECTS(this != &parent, "a focus node cannot be its own parent");
  if (parent_ == &parent) return;
  detach();
  parent_ = &parent;
  parent.children_.push_back(this);
  setManager(parent.manager_);

  // An autofocus claim made while this node was somewhere else, or before it had
  // a manager at all, is met here rather than dropped.
  if (manager_ == nullptr) return;
  if (FocusScopeNode* scope = enclosingScope(); scope && scope->pendingAutofocus() == this) {
    manager_->grantPendingAutofocus(*scope);
  }
}

void FocusNode::detach() {
  if (!parent_) return;
  if (manager_) manager_->willDetach(*this);
  std::erase(parent_->children_, this);
  parent_ = nullptr;
  setManager(nullptr);
}

void FocusNode::setManager(FocusManager* manager) {
  if (manager_ == manager) return;
  manager_ = manager;
  for (FocusNode* child : children_) child->setManager(manager);
}

void FocusNode::setCanRequestFocus(bool value) {
  if (value == canRequestFocus_) return;
  canRequestFocus_ = value;
  if (!value && manager_) manager_->didChangePolicy(*this);
}

void FocusNode::setSkipTraversal(bool value) { skipTraversal_ = value; }

void FocusNode::setDescendantsAreFocusable(bool value) {
  if (value == descendantsAreFocusable_) return;
  descendantsAreFocusable_ = value;
  if (!value && manager_) manager_->didChangePolicy(*this);
}

bool FocusNode::hasPrimaryFocus() const noexcept {
  return manager_ != nullptr && manager_->primaryFocus() == this;
}

bool FocusNode::hasFocus() const noexcept {
  if (!manager_) return false;
  const FocusNode* primary = manager_->primaryFocus();
  return primary != nullptr && (primary == this || primary->isDescendantOf(*this));
}

bool FocusNode::isFocusable() const noexcept {
  if (!manager_ || !canRequestFocus_) return false;
  for (const FocusNode* node = parent_; node; node = node->parent_) {
    if (!node->descendantsAreFocusable_) return false;
  }
  return true;
}

bool FocusNode::isDescendantOf(const FocusNode& ancestor) const noexcept {
  for (const FocusNode* node = parent_; node; node = node->parent_) {
    if (node == &ancestor) return true;
  }
  return false;
}

bool FocusNode::requestFocus() { return manager_ != nullptr && manager_->requestFocus(*this); }

void FocusNode::autofocus() {
  if (manager_) manager_->autofocus(*this);
}

void FocusNode::unfocus() {
  if (manager_) manager_->unfocus(*this);
}

FocusScopeNode* FocusNode::enclosingScope() const noexcept {
  for (FocusNode* node = parent_; node; node = node->parent_) {
    if (FocusScopeNode* scope = node->asScope()) return scope;
  }
  return nullptr;
}

Rect FocusNode::rect() const {
  // The element is only known to be alive while this node is part of a tree: a
  // consumer may hold a node past the unmount of the widget that named it, and
  // the pointer left behind would be to an element that is gone.
  if (!attached() || !element_) return Rect::zero();
  const RenderBox* box = element_->renderObject();
  if (!box || !box->hasSize()) return Rect::zero();
  return box->localToGlobalRect(box->paintBounds());
}

}  // namespace fltr
