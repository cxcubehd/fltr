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
  /// Applied on top of whichever of the above won, as a border only.
  fltr::Color focusRing = fltr::Color::transparent();
  /// How far the surface shrinks while pressed. 1 adds no transform at all.
  float pressScale = 1.0f;
  float duration = 0.12f;

  friend constexpr bool operator==(const SurfaceStyle&, const SurfaceStyle&) noexcept = default;
};

/// Precedence, most specific first: disabled, pressed, hovered, selected, rest.
/// Focus is not a fifth surface -- it recolours the border of whichever won, so
/// a focused *and* hovered control still looks hovered.
fltr::BoxDecoration resolveSurface(const SurfaceStyle& style, fltr::WidgetStates states) noexcept;

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

  /// One driver for both properties: they are retargeted from the same callback
  /// at the same instant, so sharing it keeps them exactly in step.
  fltr::AnimationDriver driver_;
  fltr::AnimatedValue<fltr::BoxDecoration> decoration_{driver_, {}};
  fltr::AnimatedValue<fltr::Transform2D> scale_{driver_, {}};
  fltr::Subscription states_;
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
