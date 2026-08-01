#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "fltr/core/callback.hpp"
#include "fltr/core/geometry.hpp"
#include "fltr/core/listenable.hpp"
#include "fltr/gestures/keys.hpp"

namespace fltr {

class Element;
class FocusManager;
class FocusScopeNode;

/// Which way a D-pad, a stick or an arrow key is asking the focus to move.
enum class TraversalDirection : std::uint8_t { Up, Down, Left, Right };

/// One focusable place in the tree.
///
/// Owned by exactly one State -- the component's own, or the consumer's when it
/// supplied one -- and referred to non-owningly by everything else, which is the
/// rule every controller in this framework follows.
///
/// A node is inert until it is attached under a scope, and that is what keeps a
/// pointer-only tree free: a component holds its node either way, but with no
/// `FocusScope` above it there is no parent to attach to, no manager, no key
/// handler and no place in any traversal order.
///
/// It is a `Listenable`, and notifies whenever this subtree gains or loses the
/// focus -- so a component publishes its focused state without rebuilding.
class FocusNode : public Listenable {
public:
  FocusNode() = default;
  ~FocusNode() override;

  FocusNode(const FocusNode&) = delete;
  FocusNode& operator=(const FocusNode&) = delete;

  /// Re-parents this node, which a `Focus` widget does from its build -- the one
  /// phase in which the ambient scope is known. Re-attaching to the same parent
  /// does nothing, so an ordinary rebuild costs nothing here.
  void attach(FocusNode& parent);
  void detach();
  /// Part of a live focus tree, which is the question every component asks: a
  /// node with no manager is pointer-only and every focus operation on it is a
  /// no-op rather than an error.
  bool attached() const noexcept { return manager_ != nullptr; }

  // --- policy: Flutter's own three axes, not invented ones ------------------

  /// Never focusable at all. A HUD element that answers to the pointer and has
  /// no business in a menu's tab order says so here.
  void setCanRequestFocus(bool value);
  bool canRequestFocus() const noexcept { return canRequestFocus_; }

  /// Focusable by a click or programmatically, never reached by tab or a D-pad.
  void setSkipTraversal(bool value);
  bool skipTraversal() const noexcept { return skipTraversal_; }

  /// Excludes this whole subtree, which is Flutter's `ExcludeFocus`.
  void setDescendantsAreFocusable(bool value);
  bool descendantsAreFocusable() const noexcept { return descendantsAreFocusable_; }

  // --- state ---------------------------------------------------------------

  bool hasPrimaryFocus() const noexcept;
  /// This node or something below it holds the focus.
  bool hasFocus() const noexcept;
  /// Attached, willing, and with no ancestor excluding its descendants.
  bool isFocusable() const noexcept;
  bool isTraversable() const noexcept { return isFocusable() && !skipTraversal_; }

  bool requestFocus();
  /// Hands the focus back to the enclosing scope. Does nothing when this subtree
  /// does not hold it.
  void unfocus();

  FocusNode* parent() const noexcept { return parent_; }
  FocusManager* manager() const noexcept { return manager_; }
  std::span<FocusNode* const> children() const noexcept { return children_; }
  FocusScopeNode* enclosingScope() const noexcept;
  bool isDescendantOf(const FocusNode& ancestor) const noexcept;

  /// The one virtual, for the reason `asPointerRegion` is: a scope is told apart
  /// from an ordinary node without RTTI.
  virtual FocusScopeNode* asScope() noexcept { return nullptr; }

  /// Where this node is, in the render tree's root space. Empty while detached,
  /// and until the element it names has been laid out. Directional traversal and
  /// scrolling a focused node into view both read it, and both are what surgery
  /// item 1 of this phase paid for.
  Rect rect() const;
  void setElement(const Element* element) noexcept { element_ = element; }

  /// Offered every key that reaches this node on its way up the focus chain.
  /// Returning true consumes it.
  Callback<bool(const KeyEvent&)> onKey;

private:
  friend class FocusManager;

  void setManager(FocusManager* manager);
  void notifyFocusChanged() { notifyListeners(); }

  FocusNode* parent_ = nullptr;
  FocusManager* manager_ = nullptr;
  const Element* element_ = nullptr;
  std::vector<FocusNode*> children_;
  bool canRequestFocus_ = true;
  bool skipTraversal_ = false;
  bool descendantsAreFocusable_ = true;
};

/// A node that remembers which of its descendants last held the focus, so a
/// scope regaining it lands where it left off rather than at the top.
///
/// DIVERGENCE: Flutter separates `FocusScope` from `FocusTraversalGroup`, so one
/// scope may contain several independent tab orders. A scope here is also the
/// traversal group: tab wraps within the innermost enclosing scope, and the
/// arrows search inside it. The cost is that two independent orders need two
/// nested scopes rather than two groups in one -- which is how it would be
/// written anyway.
class FocusScopeNode final : public FocusNode {
public:
  FocusScopeNode() { setSkipTraversal(true); }

  FocusScopeNode* asScope() noexcept override { return this; }

  /// The descendant that most recently held the focus within this scope.
  FocusNode* focusedChild() const noexcept { return focusedChild_; }

  /// Whether Tab and the arrow keys move the focus while it is inside this
  /// scope. Both off makes the scope a pure container: keys still reach the
  /// focused node and the shortcuts above it, and nothing moves on its own.
  void setTabTraversal(bool value) noexcept { tabTraversal_ = value; }
  bool tabTraversal() const noexcept { return tabTraversal_; }
  void setDirectionalTraversal(bool value) noexcept { directionalTraversal_ = value; }
  bool directionalTraversal() const noexcept { return directionalTraversal_; }

private:
  friend class FocusManager;

  FocusNode* focusedChild_ = nullptr;
  bool tabTraversal_ = true;
  bool directionalTraversal_ = true;
};

}  // namespace fltr
