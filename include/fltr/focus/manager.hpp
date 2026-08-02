#pragma once

#include <cstddef>
#include <vector>

#include "fltr/focus/node.hpp"
#include "fltr/gestures/keyboard.hpp"

namespace fltr {

/// Where the focus is, and the one key handler the whole subsystem registers.
///
/// It is owned by the outermost `FocusScope` in a tree, not by the binding, so a
/// tree with no scope in it has no manager, no handler on `KeyboardBinding` and
/// no subscriptions -- the property that lets a HUD pay nothing for the fact
/// that this machinery exists.
///
/// DIVERGENCE: Flutter routes a key through `HardwareKeyboard`, then the focus
/// chain, then `Shortcuts` and `Actions` as separate layers, with `Intent` in
/// between so an ancestor can re-target what a descendant means. There is one
/// walk here: every node on the chain from the focused node to the root is
/// offered the event, and traversal is what is left when none of them took it.
/// The cost is that a shortcut is bound to its callback where it is declared,
/// rather than to an intent something else can re-bind.
class FocusManager final : public KeyHandler {
public:
  FocusManager(KeyboardBinding& keyboard, FocusScopeNode& root);
  ~FocusManager();

  FocusManager(const FocusManager&) = delete;
  FocusManager& operator=(const FocusManager&) = delete;

  /// Null once the scope node this manager was built around has been destroyed,
  /// which a consumer's own node may well be before the tree that named it. The
  /// two clear each other's pointer, as `ScrollController` and `ScrollPosition`
  /// do and for the same reason.
  FocusScopeNode* rootScope() const noexcept { return root_; }
  FocusNode* primaryFocus() const noexcept { return primary_; }

  /// Focuses `node`, or the node a scope remembers when `node` is one. Refused,
  /// and reported as such, when the node is not focusable.
  bool requestFocus(FocusNode& node);
  /// `requestFocus` when the enclosing scope has no focused child, and a claim
  /// on that scope for when it next has none otherwise.
  void autofocus(FocusNode& node);
  /// Moves the focus to the enclosing scope, if this subtree holds it.
  void unfocus(FocusNode& node);

  /// Tab order: the traversable nodes of the innermost enclosing scope, in the
  /// order they were built, wrapping at either end.
  ///
  /// DIVERGENCE: Flutter's default policy sorts geometrically into reading
  /// order. This is Flutter's other policy, `WidgetOrderTraversalPolicy`, chosen
  /// because source order is what the author of a menu already controls and
  /// because it needs no rects, so it works before the first layout. The cost is
  /// that a two-column form tabs down the columns only if it was built that way.
  bool nextFocus();
  bool previousFocus();

  /// D-pad, stick and arrow keys: the nearest traversable node beyond this one's
  /// centre in that direction, preferring one whose extent overlaps the current
  /// node's on the other axis.
  bool moveFocusInDirection(TraversalDirection direction);

  bool handleKey(const KeyEvent& event) override;

  /// Every attached node below the root scope. The claim that focus costs
  /// nothing where it is not used is a count, not a comment.
  std::size_t nodeCount() const noexcept;

private:
  friend class FocusNode;

  void setPrimary(FocusNode* node);
  void grantPendingAutofocus(FocusScopeNode& scope);
  void willDetach(FocusNode& node);
  void didChangePolicy(FocusNode& node);
  void forgetRoot(const FocusNode& node) noexcept;

  /// The traversable descendants of `node` in build order, into `out`.
  void gatherTraversable(const FocusNode& node, std::vector<FocusNode*>& out) const;
  FocusScopeNode* scopeForTraversal() const noexcept;
  bool moveTab(int step);
  bool handleTraversalKey(const KeyEvent& event);

  KeyboardBinding* keyboard_;
  FocusScopeNode* root_;
  FocusNode* primary_ = nullptr;
  /// The chain the current key is walking, and the nodes whose focus state a
  /// change is about to move. Members rather than locals, so neither a key nor a
  /// focus move allocates once the high-water mark is reached -- and so a node
  /// destroyed from its own callback can blank itself out of the walk, which is
  /// what `KeyboardBinding` does with its handler list for the same reason.
  std::vector<FocusNode*> dispatching_;
  std::vector<FocusNode*> before_;
  std::vector<FocusNode*> after_;
  mutable std::vector<FocusNode*> candidates_;
  /// Bumped by every focus change. A listener that moves the focus again from
  /// its own callback therefore ends the outer walk rather than notifying from a
  /// set that no longer describes anything.
  std::uint32_t notifyGeneration_ = 0;
};

}  // namespace fltr
