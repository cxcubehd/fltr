#pragma once

#include <cstddef>
#include <vector>

#include "fltr/core/listenable.hpp"
#include "fltr/widgets/framework.hpp"

namespace fltr {

/// The type-erased half of an inherited element: what an ambient scope stores,
/// and what the elements reading its value subscribe to. The scope keys it by
/// widget type, so whoever finds one already knows the concrete element type.
class InheritedElementBase : public Listenable {
protected:
  ~InheritedElementBase() = default;
};

/// The ambient values in effect at one point in the tree, as a snapshot rather
/// than a path to walk.
///
/// An element that introduces a value copies its parent's entries and adds its
/// own; every other element points at its parent's scope. A scope holds no link
/// to the one it was extended from, so a lookup structurally cannot become an
/// ancestor walk: it is one hash probe whatever the depth.
class InheritedScope {
public:
  /// Snapshot of `parent` with `type` mapped to `node`, shadowing any entry the
  /// parent had for that type.
  void extend(const InheritedScope* parent, WidgetType type, InheritedElementBase& node);

  InheritedElementBase* find(WidgetType type) const noexcept;

  std::size_t size() const noexcept { return count_; }

private:
  struct Entry {
    WidgetType type = nullptr;
    InheritedElementBase* node = nullptr;
  };

  void insert(WidgetType type, InheritedElementBase& node) noexcept;

  /// Open-addressed, power-of-two sized and never more than half full, so a
  /// probe always terminates on an empty slot.
  std::vector<Entry> table_;
  std::size_t count_ = 0;
};

/// Introduces one ambient value for its subtree. Its widget supplies the value,
/// the child, and `updateShouldNotify`, which decides whether a new
/// configuration is worth waking the readers for.
template <class W>
class InheritedElement final : public ConfiguredElement<W, Element>, public InheritedElementBase {
public:
  using ConfiguredElement<W, Element>::ConfiguredElement;

  const W& config() const noexcept { return this->config_; }

  void visitChildren(FunctionRef<void(Element&)> visitor) const override {
    if (child_) visitor(*child_);
  }

protected:
  void extendInheritedScope() override {
    scope_.extend(this->inheritedScope_, widgetTypeOf<W>(), *this);
    this->inheritedScope_ = &scope_;
  }

  /// Readers are marked needing build here, before the subtree below is
  /// reconciled. A reader deeper down is therefore built by this same frame,
  /// even when the subtree between is unchanged and reconciliation skips it.
  void adopt(const W& next) override {
    const bool notify = next.updateShouldNotify(this->config_);
    this->config_ = next;
    if (notify) this->notifyListeners();
  }

  void performRebuild() override {
    child_ = this->updateChild(std::move(child_), this->config_.child());
  }

private:
  InheritedScope scope_;
  ElementPtr child_;
};

class InheritedWidget : public Widget {
public:
  template <class W>
  using ElementFor = InheritedElement<W>;

protected:
  using Widget::Widget;
  ~InheritedWidget() = default;
};

}  // namespace fltr
