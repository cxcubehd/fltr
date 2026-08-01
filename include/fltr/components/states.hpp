#pragma once

#include <cstdint>
#include <memory>

#include "fltr/core/callback.hpp"
#include "fltr/core/flags.hpp"
#include "fltr/core/observable.hpp"
#include "fltr/widgets/inherited.hpp"

namespace fltr {

// ---------------------------------------------------------------------------
// What a component says about itself
// ---------------------------------------------------------------------------

/// The whole vocabulary a component has for describing itself.
///
/// Flutter's `WidgetState`, promoted from an implementation detail of
/// `material/` to this layer's public contract: a component owns its behaviour
/// and publishes this, and owns nothing about how any of it looks.
enum class WidgetState : std::uint8_t {
  Hovered = 1 << 0,
  Pressed = 1 << 1,
  Focused = 1 << 2,
  Disabled = 1 << 3,
  Selected = 1 << 4,
  Dragged = 1 << 5,
};

template <>
inline constexpr bool kIsFlags<WidgetState> = true;

using WidgetStates = Flags<WidgetState>;

/// The observable a component publishes, and which a consumer may own instead so
/// that several visuals -- or the game -- can read one component's state.
class WidgetStatesController final : public ValueListenable<WidgetStates> {
public:
  WidgetStatesController() = default;
  explicit WidgetStatesController(WidgetStates initial) noexcept : states_(initial) {}

  const WidgetStates& value() const noexcept override { return states_; }
  bool has(WidgetState state) const noexcept { return states_.has(state); }

  void update(WidgetState state, bool present) {
    if (states_.set(state, present)) notifyListeners();
  }

private:
  WidgetStates states_;
};

/// A states controller a State owns, or one the consumer supplied.
///
/// The subscription carries no payload; it is the liveness token, exactly as
/// `OwnedFocusNode`'s is. A `Subscription` unhooks itself when its `Listenable`
/// dies, so a consumer's controller destroyed while the component is still
/// mounted reads as gone rather than being written into.
class OwnedStates {
public:
  void bind(WidgetStatesController* supplied) {
    WidgetStatesController* next = supplied ? supplied : &own_;
    // A detached subscription with a controller still named means that
    // controller was destroyed, because there is no live-but-unsubscribed state.
    // Returning rather than re-subscribing is what keeps a consumer who went on
    // naming it out of freed memory.
    FLTR_EXPECTS(next != bound_ || liveness_.attached(),
                 "a widget still names a states controller that has been destroyed");
    if (next == bound_) return;
    bound_ = next;
    bound_->subscribe(liveness_, [](void*) {}, nullptr);
  }

  /// Null once a consumer-supplied controller has been destroyed.
  WidgetStatesController* get() const noexcept { return liveness_.attached() ? bound_ : nullptr; }

  void release() {
    liveness_.detach();
    bound_ = nullptr;
  }

  void update(WidgetState state, bool present) {
    if (WidgetStatesController* controller = get()) controller->update(state, present);
  }

private:
  WidgetStatesController own_;
  WidgetStatesController* bound_ = nullptr;
  Subscription liveness_;
};

// ---------------------------------------------------------------------------
// What a component publishes to the visuals it does not own
// ---------------------------------------------------------------------------

/// The two things a visual subtree may want from the component around it: the
/// state it is in, and -- for the components that have one -- the 0..1 position
/// its shape is drawn from. One lookup carries both, and both are pointers to
/// objects the component's State owns.
///
/// A visual built outside any component finds nothing here, which is what lets
/// the same subtree be used inside a button and on its own.
class ComponentScope final : public Configure<ComponentScope, InheritedWidget> {
public:
  struct Args {
    Key key;
    WidgetStatesController* states = nullptr;
    /// How far along the component is: a toggle's on-ness, a slider's value as a
    /// fraction of its range, a progress bar's fill. Null for a button, which
    /// has no such quantity.
    ValueListenable<float>* fraction = nullptr;
    WidgetRef child;
  };

  explicit ComponentScope(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "ComponentScope"; }
  WidgetRef child() const noexcept { return args_.child; }
  WidgetStatesController* states() const noexcept { return args_.states; }
  ValueListenable<float>* fraction() const noexcept { return args_.fraction; }

  bool updateShouldNotify(const ComponentScope& previous) const noexcept {
    return args_.states != previous.args_.states || args_.fraction != previous.args_.fraction;
  }

  /// Both null outside a component. What they hand back belongs to the
  /// component's State and stays good for as long as the component is mounted --
  /// which the scope config carrying them does not, so it is never handed out.
  static WidgetStatesController* statesOf(BuildContext& context) {
    const ComponentScope* scope = of(context);
    return scope == nullptr ? nullptr : scope->states();
  }

  static ValueListenable<float>* fractionOf(BuildContext& context) {
    const ComponentScope* scope = of(context);
    return scope == nullptr ? nullptr : scope->fraction();
  }

private:
  static const ComponentScope* of(BuildContext& context) {
    InheritedElementBase* found =
        context.element().dependOnInherited(widgetTypeOf<ComponentScope>());
    return found == nullptr ? nullptr
                            : &static_cast<InheritedElement<ComponentScope>*>(found)->config();
  }

  Args args_;
};

// ---------------------------------------------------------------------------
// StatesBuilder -- the builder half
// ---------------------------------------------------------------------------

class StatesBuilder;

class StatesBuilderState final : public State<StatesBuilder> {
public:
  void dispose() override { changes_.detach(); }
  WidgetRef build(BuildContext& context) override;

private:
  /// The states moved underneath, so there is nothing of our own to mutate --
  /// only the build to redo. Same shape as `WatchState`, and for the same reason.
  void onChanged() {
    setState([] {});
  }

  Subscription changes_;
};

/// Rebuilds its subtree whenever the enclosing component's state changes.
///
/// This is the thin half of the call-site question, and deliberately thin: a
/// component publishes an observable, and this turns it into a builder for a
/// consumer who would rather write one. A visual whose *painting* is all that
/// changes with the state should skip this and hand
/// `ComponentScope::statesOf(context)` to a render object instead -- that is the
/// path M7 and M8 paid for, and it costs no rebuild at all.
///
/// Outside a component it builds once, with no states set, and subscribes to
/// nothing.
class StatesBuilder final : public Configure<StatesBuilder, StatefulWidget> {
public:
  using Builder = Callback<WidgetRef(BuildContext&, WidgetStates)>;

  struct Args {
    Key key;
    Builder builder;
  };

  explicit StatesBuilder(const Args& args) : Configure(args.key), args_(args) {
    FLTR_EXPECTS(static_cast<bool>(args.builder), "StatesBuilder needs a builder");
  }

  const char* name() const noexcept override { return "StatesBuilder"; }
  const Builder& builder() const noexcept { return args_.builder; }

  std::unique_ptr<State<StatesBuilder>> createState() const {
    return std::make_unique<StatesBuilderState>();
  }

private:
  Args args_;
};

inline WidgetRef StatesBuilderState::build(BuildContext& context) {
  WidgetStatesController* states = ComponentScope::statesOf(context);
  if (states) {
    subscribeMember<StatesBuilderState, &StatesBuilderState::onChanged>(*states, changes_, this);
  } else {
    changes_.detach();
  }
  return widget().builder()(context, states ? states->value() : WidgetStates{});
}

}  // namespace fltr
