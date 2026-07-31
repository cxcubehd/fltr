#pragma once

#include <memory>

#include "fltr/animation/tween.hpp"
#include "fltr/widgets/basic.hpp"
#include "fltr/widgets/inherited.hpp"

namespace fltr {

// ---------------------------------------------------------------------------
// TickerMode
// ---------------------------------------------------------------------------

/// Mutes every ticker in its subtree.
///
/// A panel that is hidden rather than removed keeps its state and its
/// subscriptions but costs no time at all, and resumes exactly where it stopped:
/// tickers carry a per-frame delta, so a muted one has no elapsed time to
/// reconcile when it comes back.
class TickerMode final : public Configure<TickerMode, InheritedWidget> {
public:
  struct Args {
    Key key;
    bool enabled = true;
    WidgetRef child;
  };

  explicit TickerMode(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "TickerMode"; }
  WidgetRef child() const noexcept { return args_.child; }
  bool enabled() const noexcept { return args_.enabled; }

  bool updateShouldNotify(const TickerMode& previous) const noexcept {
    return args_.enabled != previous.args_.enabled;
  }

  /// Enabled unless something above says otherwise, so a tree with no TickerMode
  /// in it pays nothing for the mechanism.
  static bool of(BuildContext& context) {
    InheritedElementBase* node = context.element().dependOnInherited(widgetTypeOf<TickerMode>());
    return node == nullptr || static_cast<InheritedElement<TickerMode>*>(node)->config().enabled();
  }

private:
  Args args_;
};

// ---------------------------------------------------------------------------
// The implicit-animation layer
// ---------------------------------------------------------------------------

/// Animates one property to whatever its widget is configured with.
///
/// A configuration change re-targets the interpolation *from the value on
/// screen* and runs the driver from zero, so an interruption is continuous and
/// repeated interruption accumulates no error -- every new interval begins where
/// the last one had actually reached.
///
/// Only the configuration change rebuilds. Each tick after it invalidates
/// exactly one render object, because what the built widget hands down is the
/// animated value itself rather than a snapshot of it.
template <class W>
class ImplicitlyAnimatedState final : public State<W> {
public:
  using Value = typename W::Value;

  void initState() override {
    driver_.attach(this->context().tickers());
    driver_.setConfig(this->widget().animation());
    const Value settled = this->widget().target();
    value_.setTween({settled, settled});
  }

  void didUpdateWidget(const W& previous) override {
    driver_.setConfig(this->widget().animation());
    if (this->widget().target() != previous.target()) value_.retarget(this->widget().target());
  }

  void dispose() override { driver_.detach(); }

  WidgetRef build(BuildContext& context) override {
    driver_.setMuted(!TickerMode::of(context));
    return W::compose(this->widget(), value_);
  }

private:
  AnimationDriver driver_;
  AnimatedValue<Value> value_{driver_, {}};
};

/// The element every implicitly animated widget needs. A widget joins the layer
/// by naming its value type, its target, its timing, and how to compose the
/// widget it wraps around the animated value.
template <class Derived>
class ImplicitlyAnimatedWidget : public StatefulWidget {
public:
  std::unique_ptr<State<Derived>> createState() const {
    return std::make_unique<ImplicitlyAnimatedState<Derived>>();
  }

protected:
  using StatefulWidget::StatefulWidget;
  ~ImplicitlyAnimatedWidget() = default;
};

/// ```cpp
/// AnimatedOpacity::make({
///   .opacity = hovered ? 1.0f : 0.35f,
///   .animation = {.duration = 0.12f, .curve = Curves::easeOut},
///   .child = badge(),
/// })
/// ```
class AnimatedOpacity final
    : public Configure<AnimatedOpacity, ImplicitlyAnimatedWidget<AnimatedOpacity>> {
public:
  using Value = float;

  struct Args {
    Key key;
    float opacity = 1.0f;
    AnimationDriver::Config animation;
    WidgetRef child;
  };

  explicit AnimatedOpacity(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "AnimatedOpacity"; }
  float target() const noexcept { return args_.opacity; }
  const AnimationDriver::Config& animation() const noexcept { return args_.animation; }

  static WidgetRef compose(const AnimatedOpacity& self, ValueListenable<float>& value) {
    return Opacity::make({.animation = &value, .child = self.args_.child});
  }

private:
  Args args_;
};

class AnimatedTransform final
    : public Configure<AnimatedTransform, ImplicitlyAnimatedWidget<AnimatedTransform>> {
public:
  using Value = Transform2D;

  struct Args {
    Key key;
    Transform2D transform = Transform2D::identity();
    Alignment origin = Alignment::center();
    AnimationDriver::Config animation;
    WidgetRef child;
  };

  explicit AnimatedTransform(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "AnimatedTransform"; }
  const Transform2D& target() const noexcept { return args_.transform; }
  const AnimationDriver::Config& animation() const noexcept { return args_.animation; }

  static WidgetRef compose(const AnimatedTransform& self, ValueListenable<Transform2D>& value) {
    return Transform::make(
        {.animation = &value, .origin = self.args_.origin, .child = self.args_.child});
  }

private:
  Args args_;
};

class AnimatedDecoration final
    : public Configure<AnimatedDecoration, ImplicitlyAnimatedWidget<AnimatedDecoration>> {
public:
  using Value = BoxDecoration;

  struct Args {
    Key key;
    BoxDecoration decoration;
    AnimationDriver::Config animation;
    WidgetRef child;
  };

  explicit AnimatedDecoration(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "AnimatedDecoration"; }
  const BoxDecoration& target() const noexcept { return args_.decoration; }
  const AnimationDriver::Config& animation() const noexcept { return args_.animation; }

  static WidgetRef compose(const AnimatedDecoration& self, ValueListenable<BoxDecoration>& value) {
    return DecoratedBox::make({.animation = &value, .child = self.args_.child});
  }

private:
  Args args_;
};

/// The one whose frames are not free: alignment resolves during layout, so every
/// frame of this lays out the subtree below it. Correct, invalidated properly,
/// and the reason the other three exist.
class AnimatedAlign final
    : public Configure<AnimatedAlign, ImplicitlyAnimatedWidget<AnimatedAlign>> {
public:
  using Value = Alignment;

  struct Args {
    Key key;
    Alignment alignment = Alignment::center();
    float widthFactor = -1.0f;
    float heightFactor = -1.0f;
    AnimationDriver::Config animation;
    WidgetRef child;
  };

  explicit AnimatedAlign(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "AnimatedAlign"; }
  const Alignment& target() const noexcept { return args_.alignment; }
  const AnimationDriver::Config& animation() const noexcept { return args_.animation; }

  static WidgetRef compose(const AnimatedAlign& self, ValueListenable<Alignment>& value) {
    return Align::make({
        .animation = &value,
        .widthFactor = self.args_.widthFactor,
        .heightFactor = self.args_.heightFactor,
        .child = self.args_.child,
    });
  }

private:
  Args args_;
};

}  // namespace fltr
