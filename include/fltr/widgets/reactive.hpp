#pragma once

#include <concepts>
#include <memory>

#include "fltr/core/callback.hpp"
#include "fltr/core/observable.hpp"
#include "fltr/widgets/inherited.hpp"

namespace fltr {

// ---------------------------------------------------------------------------
// Watch
// ---------------------------------------------------------------------------

template <class T>
class Watch;

template <class T>
class WatchState final : public State<Watch<T>> {
public:
  void initState() override { listen(); }

  void didUpdateWidget(const Watch<T>& previous) override {
    if (&previous.observable() != &this->widget().observable()) listen();
  }

  /// Not left to the Subscription's destructor: a subtree unmounts entirely
  /// before any of it is destroyed, so a sibling pushing a value from its own
  /// dispose would reach a State whose element no longer has a tree.
  void dispose() override { subscription_.detach(); }

  WidgetRef build(BuildContext& context) override {
    return this->widget().builder()(context, this->widget().observable().value());
  }

private:
  void listen() {
    subscribeMember<WatchState, &WatchState::onChanged>(this->widget().observable(), subscription_,
                                                        this);
  }

  /// The value changed underneath, so there is nothing of our own to mutate --
  /// only the build to redo.
  void onChanged() {
    this->setState([] {});
  }

  Subscription subscription_;
};

/// Rebuilds its own subtree, and nothing else, when the value it watches
/// changes. A push of an equal value never reaches here at all.
///
/// The observable belongs to the game and must outlive the widget, which is the
/// same rule the pointer callbacks already follow.
///
/// ```cpp
/// Watch<int>::make({
///   .value = &shield,
///   .builder = [](BuildContext&, const int& hp) { return Bar::make({.value = hp}); },
/// })
/// ```
template <class T>
class Watch final : public Configure<Watch<T>, StatefulWidget> {
public:
  using Builder = Callback<WidgetRef(BuildContext&, const T&)>;

  struct Args {
    Key key;
    Observable<T>* value = nullptr;
    Builder builder;
  };

  explicit Watch(const Args& args) : Configure<Watch<T>, StatefulWidget>(args.key), args_(args) {
    FLTR_EXPECTS(args.value != nullptr, "Watch needs a value to watch");
    FLTR_EXPECTS(static_cast<bool>(args.builder), "Watch needs a builder");
  }

  const char* name() const noexcept override { return "Watch"; }
  Observable<T>& observable() const noexcept { return *args_.value; }
  const Builder& builder() const noexcept { return args_.builder; }

  std::unique_ptr<State<Watch<T>>> createState() const {
    return std::make_unique<WatchState<T>>();
  }

private:
  Args args_;
};

// ---------------------------------------------------------------------------
// Ambient
// ---------------------------------------------------------------------------

/// Ambient data for a subtree -- a theme, a layout scale -- read with `of` in
/// O(1) rather than by walking ancestors.
///
/// Changing it rebuilds exactly the elements whose last build read it. A widget
/// between the value and its readers is not disturbed, so a theme change does
/// not walk the tree it applies to.
///
/// `T` is copied into the build arena, so it must be trivially destructible --
/// the rule every widget field already follows.
template <class T>
class Ambient final : public Configure<Ambient<T>, InheritedWidget> {
public:
  struct Args {
    Key key;
    T value{};
    WidgetRef child;
  };

  explicit Ambient(const Args& args)
      : Configure<Ambient<T>, InheritedWidget>(args.key), args_(args) {}

  const char* name() const noexcept override { return "Ambient"; }
  WidgetRef child() const noexcept { return args_.child; }
  const T& value() const noexcept { return args_.value; }

  bool updateShouldNotify(const Ambient& previous) const noexcept {
    return !(args_.value == previous.args_.value);
  }

  /// Reads the nearest `T` above `context` and subscribes that element to it.
  static const T& of(BuildContext& context) {
    InheritedElementBase* node = context.element().dependOnInherited(widgetTypeOf<Ambient>());
    FLTR_EXPECTS(node != nullptr, "Ambient<T>::of found no Ambient<T> above this widget");
    return static_cast<InheritedElement<Ambient>*>(node)->config().value();
  }

private:
  static_assert(std::equality_comparable<T>,
                "an ambient value must be equality-comparable: an unchanged one notifies nobody");

  Args args_;
};

}  // namespace fltr
