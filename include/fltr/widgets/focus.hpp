#pragma once

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <vector>

#include "fltr/focus/manager.hpp"
#include "fltr/widgets/inherited.hpp"

namespace fltr {

class ScrollPosition;

// ---------------------------------------------------------------------------
// The ambient node
// ---------------------------------------------------------------------------

/// The nearest enclosing focus node, which is what a `Focus` below attaches to.
///
/// Published only where there is a focus tree at all: a subtree with no
/// `FocusScope` above it holds no marker, no manager and no key handler, and its
/// components are pointer-only with nothing configured to make them so. The
/// accepted consequence is the mirror image of that property -- introducing a
/// scope *above* an already-mounted subtree changes its shape, so the subtree is
/// inflated afresh.
class FocusMarker final : public Configure<FocusMarker, InheritedWidget> {
public:
  struct Args {
    Key key;
    FocusNode* node = nullptr;
    WidgetRef child;
  };

  explicit FocusMarker(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "FocusMarker"; }
  WidgetRef child() const noexcept { return args_.child; }
  FocusNode* node() const noexcept { return args_.node; }

  bool updateShouldNotify(const FocusMarker& previous) const noexcept {
    return args_.node != previous.args_.node;
  }

  /// Null when nothing above introduced a focus tree.
  static FocusNode* of(BuildContext& context) {
    InheritedElementBase* found = context.element().dependOnInherited(widgetTypeOf<FocusMarker>());
    return found == nullptr ? nullptr
                            : static_cast<InheritedElement<FocusMarker>*>(found)->config().node();
  }

private:
  Args args_;
};

/// A focus node a State owns, or one the consumer supplied.
///
/// The subscription is both how focus changes arrive and how this knows a
/// consumer's node is still alive: a `Subscription` unhooks itself when its
/// `Listenable` is destroyed, which is the same liveness token M10's scrollbar
/// uses rather than a second pointer to keep in step.
template <class Node>
class OwnedFocusNode {
public:
  template <class T, void (T::*M)()>
  void bind(Node* supplied, T* observer) {
    Node* next = supplied ? supplied : &own_;
    if (next == node_ && changes_.attached()) return;
    if (Node* previous = get()) previous->detach();
    node_ = next;
    subscribeMember<T, M>(*node_, changes_, observer);
  }

  /// Null once a consumer-supplied node has been destroyed.
  Node* get() const noexcept { return changes_.attached() ? node_ : nullptr; }

  /// Detached explicitly rather than left to the destructor, and the
  /// subscription first: a subtree unmounts entirely before any of it is
  /// destroyed, and a node detaching moves the focus, which must not come back
  /// as a callback on a State whose element is already gone.
  void release() {
    Node* node = get();
    changes_.detach();
    if (node) node->detach();
    node_ = nullptr;
  }

private:
  Node own_;
  Node* node_ = nullptr;
  Subscription changes_;
};

// ---------------------------------------------------------------------------
// FocusScope
// ---------------------------------------------------------------------------

class FocusScope;

class FocusScopeState final : public State<FocusScope> {
public:
  void dispose() override;
  WidgetRef build(BuildContext& context) override;

  /// A scope has nothing to do when the focus moves inside it; the subscription
  /// exists to tell this State that a consumer's node is still alive.
  void didChangeFocus() {}

  FocusScopeNode* node() const noexcept { return host_.get(); }

private:
  OwnedFocusNode<FocusScopeNode> host_;
  std::unique_ptr<FocusManager> manager_;
};

/// Introduces a focus tree, and with it the one key handler the whole subsystem
/// registers. A tree with no `FocusScope` in it is pointer-only by construction.
///
/// The scope is also the traversal group: Tab wraps within the innermost one
/// enclosing the focused node, and the arrows search inside it. A dialog that
/// should keep the keyboard to itself is therefore a scope, and needs nothing
/// else to trap traversal.
class FocusScope final : public Configure<FocusScope, StatefulWidget> {
public:
  struct Args {
    Key key;
    /// Consumer-owned, and the only way to move the focus from outside the tree.
    /// It must outlive the widget, as every other object referred to by pointer
    /// from a widget must.
    FocusScopeNode* node = nullptr;
    bool tabTraversal = true;
    /// Off for a scope whose arrows belong to the game rather than to the menu.
    bool directionalTraversal = true;
    WidgetRef child;
  };

  explicit FocusScope(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "FocusScope"; }
  WidgetRef child() const noexcept { return args_.child; }
  FocusScopeNode* node() const noexcept { return args_.node; }
  bool tabTraversal() const noexcept { return args_.tabTraversal; }
  bool directionalTraversal() const noexcept { return args_.directionalTraversal; }

  std::unique_ptr<State<FocusScope>> createState() const {
    return std::make_unique<FocusScopeState>();
  }

private:
  Args args_;
};

// ---------------------------------------------------------------------------
// Focus
// ---------------------------------------------------------------------------

class Focus;

class FocusState final : public State<Focus> {
public:
  void dispose() override;
  WidgetRef build(BuildContext& context) override;

  void didChangeFocus();

  FocusNode* node() const noexcept { return host_.get(); }

private:
  OwnedFocusNode<FocusNode> host_;
  ScrollPosition* scroll_ = nullptr;
  bool focused_ = false;
  bool autofocused_ = false;
};

/// One focusable place, and the keys that reach it.
///
/// With no focus tree above, none of this exists: no node is attached, no key
/// arrives, and the subtree behaves exactly as it would have without the widget.
class Focus final : public Configure<Focus, StatefulWidget> {
public:
  struct Args {
    Key key;
    /// Consumer-owned; must outlive the widget. Null means the State owns one.
    FocusNode* node = nullptr;
    /// Never focusable at all -- pointer interaction only.
    bool canRequestFocus = true;
    /// Focusable by a click or programmatically, never reached by Tab or a
    /// D-pad. A HUD element that is clickable but has no business in a menu's
    /// order is the common case, not an exotic one.
    bool skipTraversal = false;
    /// Excludes this whole subtree, which is Flutter's `ExcludeFocus`.
    bool descendantsAreFocusable = true;
    /// Takes the focus on its first build, if its scope has none.
    bool autofocus = false;
    /// Scrolls this into view when it takes the focus, moving as little as
    /// possible. What makes a menu taller than its viewport navigable at all.
    bool ensureVisible = true;
    float ensureVisibleDuration = 0.15f;
    /// Offered every key that reaches this node on the way up the focus chain.
    Callback<bool(const KeyEvent&)> onKey;
    Callback<void(bool)> onFocusChange;
    WidgetRef child;
  };

  explicit Focus(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Focus"; }
  const Args& args() const noexcept { return args_; }
  WidgetRef child() const noexcept { return args_.child; }

  std::unique_ptr<State<Focus>> createState() const { return std::make_unique<FocusState>(); }

private:
  Args args_;
};

// ---------------------------------------------------------------------------
// Shortcuts
// ---------------------------------------------------------------------------

/// A key and the modifiers that must be held with it, matched exactly -- so
/// Ctrl-S is not Ctrl-Shift-S, which is what `SingleActivator` does too.
struct KeyStroke {
  LogicalKey key = LogicalKey::None;
  KeyModifiers modifiers;

  friend constexpr bool operator==(KeyStroke, KeyStroke) noexcept = default;
};

struct Shortcut {
  KeyStroke stroke;
  Callback<void()> onInvoke;
};

/// The shortcuts of one `Shortcuts` widget. The braced list at the call site is
/// a temporary, so entries are copied into the build arena -- the same bargain
/// `WidgetList` makes, for the same reason.
class ShortcutList {
public:
  ShortcutList() = default;
  ShortcutList(std::initializer_list<Shortcut> shortcuts);

  std::size_t size() const noexcept { return size_; }
  const Shortcut* begin() const noexcept { return items_; }
  const Shortcut* end() const noexcept { return items_ + size_; }

private:
  const Shortcut* items_ = nullptr;
  std::size_t size_ = 0;
};

static_assert(std::is_trivially_destructible_v<ShortcutList>);

class Shortcuts;

class ShortcutsState final : public State<Shortcuts> {
public:
  void initState() override;
  void didUpdateWidget(const Shortcuts& previous) override;
  WidgetRef build(BuildContext& context) override;

private:
  void adoptShortcuts();
  bool invoke(const KeyEvent& event) const;

  /// Copied out of the arena when the configuration arrives, not when the widget
  /// builds: the entries belong to the build that made them, and a key arrives
  /// long after that arena was released. Assignment reuses the capacity, so a
  /// rebuild with the same number of shortcuts allocates nothing.
  std::vector<Shortcut> shortcuts_;
};

/// Key combinations that fire while the focus is anywhere inside this subtree.
///
/// It is a focus node that never takes the focus, so keys reach it on their way
/// up the chain from whatever is focused below -- which is exactly Flutter's
/// `Shortcuts`, minus the `Intent`/`Action` indirection between the stroke and
/// what it does. The cost of collapsing those: a shortcut is bound to its
/// callback where it is declared, so an ancestor cannot re-target what a
/// descendant means by "activate".
class Shortcuts final : public Configure<Shortcuts, StatefulWidget> {
public:
  struct Args {
    Key key;
    ShortcutList shortcuts;
    WidgetRef child;
  };

  explicit Shortcuts(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Shortcuts"; }
  ShortcutList shortcuts() const noexcept { return args_.shortcuts; }
  WidgetRef child() const noexcept { return args_.child; }

  std::unique_ptr<State<Shortcuts>> createState() const {
    return std::make_unique<ShortcutsState>();
  }

private:
  Args args_;
};

}  // namespace fltr
