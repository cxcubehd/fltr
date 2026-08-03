#include "ui/widgets/styled_surface.hh"

#include <algorithm>

#include "fltr/widgets/basic.hpp"

namespace demo {

using fltr::BorderRadius;
using fltr::BoxDecoration;
using fltr::BuildContext;
using fltr::ComponentScope;
using fltr::DecoratedBox;
using fltr::WidgetRef;
using fltr::WidgetStatesController;
using fltr::WidgetState;
using fltr::WidgetStates;

bool focusVisible(const SurfaceStyle& style) noexcept {
  return style.keyboardMode == nullptr || style.keyboardMode->value();
}

BoxDecoration resolveSurface(const SurfaceStyle& style, WidgetStates states) noexcept {
  BoxDecoration decoration = style.rest;
  if (states.has(WidgetState::Selected)) decoration = style.selected;
  if (states.has(WidgetState::Hovered)) decoration = style.hovered;
  if (states.has(WidgetState::Pressed)) decoration = style.pressed;
  if (states.has(WidgetState::Disabled)) decoration = style.disabled;

  if (states.has(WidgetState::Focused) && !states.has(WidgetState::Disabled) &&
      style.focusRing.a > 0 && focusVisible(style)) {
    decoration.borderColor = style.focusRing;
    decoration.borderWidth = std::max(decoration.borderWidth, 1.0f);
  }
  return decoration;
}

SurfaceStyle buttonStyle(const Theme& theme) {
  const BorderRadius radius = BorderRadius::all(theme.radius);
  return SurfaceStyle{
      .rest = {theme.surface, radius, theme.border, theme.hairline},
      .hovered = {theme.surfaceRaised, radius, theme.borderStrong, theme.hairline},
      .pressed = {theme.accentSoft, radius, theme.accent, theme.hairline},
      .selected = {theme.accentSoft, radius, theme.accent, theme.hairline},
      .disabled = {theme.background, radius, theme.border, theme.hairline},
      .focusRing = theme.accent,
      .keyboardMode = theme.keyboardMode,
  };
}

SurfaceStyle listItemStyle(const Theme& theme) {
  const BorderRadius radius = BorderRadius::all(theme.radius);
  return SurfaceStyle{
      .rest = {theme.background, radius, theme.border, theme.hairline},
      .hovered = {theme.surface, radius, theme.borderStrong, theme.hairline},
      .pressed = {theme.surfaceRaised, radius, theme.accent, theme.hairline},
      .selected = {theme.accentSoft, radius, theme.accent, theme.hairline},
      .disabled = {theme.background, radius, theme.border, theme.hairline},
      .focusRing = theme.accent,
      .keyboardMode = theme.keyboardMode,
  };
}

SurfaceStyle trackStyle(const Theme& theme) {
  const BorderRadius radius = BorderRadius::all(3.0f);
  return SurfaceStyle{
      .rest = {theme.background, radius, theme.border, theme.hairline},
      .hovered = {theme.background, radius, theme.borderStrong, theme.hairline},
      .pressed = {theme.background, radius, theme.accent, theme.hairline},
      .selected = {theme.background, radius, theme.borderStrong, theme.hairline},
      .disabled = {theme.background, radius, theme.border, theme.hairline},
      .focusRing = theme.accent,
      .keyboardMode = theme.keyboardMode,
  };
}

void StyledSurfaceState::initState() {
  driver_.attach(context().tickers());
  driver_.setConfig({.duration = widget().style().duration, .curve = fltr::Curves::easeOut});
  const BoxDecoration settled = resolveSurface(widget().style(), {});
  decoration_.setTween({settled, settled});
  scale_.setTween({fltr::Transform2D::identity(), fltr::Transform2D::identity()});
}

void StyledSurfaceState::didUpdateWidget(const StyledSurface& previous) {
  driver_.setConfig({.duration = widget().style().duration, .curve = fltr::Curves::easeOut});
  if (!(widget().style() == previous.style())) retarget();
}

void StyledSurfaceState::dispose() {
  states_.detach();
  mode_.detach();
  driver_.detach();
}

void StyledSurfaceState::retarget() {
  const WidgetStates states = controller_ != nullptr ? controller_->value() : WidgetStates{};

  const BoxDecoration decoration = resolveSurface(widget().style(), states);
  if (!(decoration == decoration_.value())) decoration_.retarget(decoration);

  const float scale = states.has(WidgetState::Pressed) ? widget().style().pressScale : 1.0f;
  const fltr::Transform2D transform = fltr::Transform2D::scaling(scale, scale);
  if (!(transform == scale_.value())) scale_.retarget(transform);
}

void StyledSurfaceState::onStatesChanged() { retarget(); }

WidgetRef StyledSurfaceState::build(BuildContext& context) {
  // Reading the component's states subscribes this element to the *scope*, not
  // to the controller: the scope only changes when a different component wraps
  // us, which is almost never.
  if (!mode_.attached() && widget().style().keyboardMode != nullptr) {
    fltr::subscribeMember<StyledSurfaceState, &StyledSurfaceState::onStatesChanged>(
        *widget().style().keyboardMode, mode_, this);
  }

  WidgetStatesController* controller = ComponentScope::statesOf(context);
  if (controller != controller_) {
    controller_ = controller;
    states_.detach();
    if (controller_ != nullptr) {
      fltr::subscribeMember<StyledSurfaceState, &StyledSurfaceState::onStatesChanged>(
          *controller_, states_, this);
    }
    retarget();
  }

  WidgetRef surface = DecoratedBox::make({.animation = &decoration_, .child = widget().child()});

  // The transform is a paint-only property, so a press animates by repainting
  // one render object -- the same bargain the decoration makes. It is omitted
  // entirely when the style does not ask for it, rather than left as an
  // identity node nobody needs.
  if (widget().style().pressScale != 1.0f) {
    surface = fltr::Transform::make({
        .animation = &scale_,
        .origin = fltr::Alignment::center(),
        .child = surface,
    });
  }
  return surface;
}

}  // namespace demo
