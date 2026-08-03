#pragma once

#include <memory>

#include "fltr/animation/tween.hpp"
#include "fltr/components/states.hpp"
#include "fltr/render/boxes.hpp"
#include "fltr/widgets/framework.hpp"
#include "ui/theme.hh"

namespace demo {

/// One decoration per interaction state.
///
/// fltr components publish state and own nothing about how they look, so this is
/// the consumer's half of that bargain: the mapping from `WidgetStates` to
/// pixels, written once and reused by every control in the demo.
struct SurfaceStyle {
  fltr::BoxDecoration rest;
  fltr::BoxDecoration hovered;
  fltr::BoxDecoration pressed;
  fltr::BoxDecoration selected;
  fltr::BoxDecoration disabled;
  /// Applied on top of whichever of the above won, as a border only, and only
  /// while `keyboardMode` says the focus is being driven by the keyboard.
  fltr::Color focusRing = fltr::Color::transparent();
  fltr::ValueListenable<bool>* keyboardMode = nullptr;
  /// How far the surface shrinks while pressed. 1 adds no transform at all.
  float pressScale = 1.0f;
  float duration = 0.12f;

  friend constexpr bool operator==(const SurfaceStyle&, const SurfaceStyle&) noexcept = default;
};

/// Precedence, most specific first: disabled, pressed, hovered, selected, rest.
/// Focus is not a fifth surface -- it recolours the border of whichever won, so
/// a focused *and* hovered control still looks hovered.
fltr::BoxDecoration resolveSurface(const SurfaceStyle& style, fltr::WidgetStates states) noexcept;

/// Which of the two highlights the current modality allows. Exactly one of them
/// answers "where is the player", so the cursor's does not linger on one control
/// while the keyboard rings another.
bool focusVisible(const SurfaceStyle& style) noexcept;
bool hoverVisible(const SurfaceStyle& style) noexcept;

SurfaceStyle buttonStyle(const Theme& theme);
SurfaceStyle listItemStyle(const Theme& theme);
SurfaceStyle trackStyle(const Theme& theme);

class StyledSurface;

class StyledSurfaceState final : public fltr::State<StyledSurface> {
public:
  void initState() override;
  void didUpdateWidget(const StyledSurface& previous) override;
  void dispose() override;
  fltr::WidgetRef build(fltr::BuildContext& context) override;

private:
  /// The whole point of this class: a state change retargets an interpolation
  /// that a render object is already observing. No `setState`, so hovering a
  /// button rebuilds nothing at all -- it repaints one node.
  void onStatesChanged();
  void retarget();
  void adoptTiming();

  /// A driver each. Retargeting restarts the driver it runs on, and a restart
  /// replays whatever else is on that driver from the start of an interval it had
  /// already left -- which is how a press that ends off the button leaves the
  /// surface holding the shrink it was told to let go of.
  fltr::AnimationDriver decorationDriver_;
  fltr::AnimationDriver pressDriver_;
  fltr::AnimatedValue<fltr::BoxDecoration> decoration_{decorationDriver_, {}};
  fltr::AnimatedValue<fltr::Transform2D> scale_{pressDriver_, {}};
  fltr::Subscription states_;
  /// The surface repaints when the input modality flips, so a ring appears on
  /// whatever is already focused the moment a key is pressed.
  fltr::Subscription mode_;
  fltr::WidgetStatesController* controller_ = nullptr;
};

/// Paints the surface of the component around it, and animates between the
/// states that component publishes.
///
/// Outside a component it simply paints `rest`, which is what lets the same
/// widget be used for a static panel.
class StyledSurface final : public fltr::Configure<StyledSurface, fltr::StatefulWidget> {
public:
  struct Args {
    fltr::Key key;
    SurfaceStyle style;
    fltr::WidgetRef child;
  };

  explicit StyledSurface(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "StyledSurface"; }
  const SurfaceStyle& style() const noexcept { return args_.style; }
  fltr::WidgetRef child() const noexcept { return args_.child; }

  std::unique_ptr<fltr::State<StyledSurface>> createState() const {
    return std::make_unique<StyledSurfaceState>();
  }

private:
  Args args_;
};

}  // namespace demo
