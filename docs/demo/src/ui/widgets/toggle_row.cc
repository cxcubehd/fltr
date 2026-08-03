#include "ui/widgets/toggle_row.hh"

#include "fltr/components/states.hpp"
#include "fltr/widgets/basic.hpp"
#include "ui/widgets/panel.hh"
#include "ui/widgets/styled_surface.hh"

namespace demo {

using fltr::Align;
using fltr::Alignment;
using fltr::BorderRadius;
using fltr::BuildContext;
using fltr::ComponentScope;
using fltr::CrossAxisAlignment;
using fltr::DecoratedBox;
using fltr::EdgeInsets;
using fltr::Padding;
using fltr::RawToggle;
using fltr::Row;
using fltr::SizedBox;
using fltr::ToggleValue;
using fltr::Transform2D;
using fltr::WidgetRef;

namespace {

Transform2D slideX(float fraction, float travel) noexcept {
  return Transform2D::translation({fraction * travel, 0.0f});
}

/// The switch, in unscaled units. Everything here goes through `Theme::px` at
/// the point of use so the whole control follows the interface scale.
constexpr float kTrackWidth = 40.0f;
constexpr float kTrackHeight = 22.0f;
constexpr float kThumbInset = 3.0f;

}  // namespace

void SwitchThumbState::dispose() { travel_.release(); }

WidgetRef SwitchThumbState::build(BuildContext& context) {
  travel_.bind(ComponentScope::fractionOf(context), &slideX, widget().args().travel);

  return fltr::Transform::make({
      .animation = &travel_,
      .origin = Alignment::centerLeft(),
      .child = DecoratedBox::make({
          .decoration = {.color = widget().args().color,
                         .radius = BorderRadius::all(widget().args().diameter * 0.5f)},
          .child = SizedBox::make({.size = fltr::Size::square(widget().args().diameter)}),
      }),
  });
}

WidgetRef toggleRow(const Theme& theme, const ToggleRowSpec& spec) {
  const float trackWidth = theme.px(kTrackWidth);
  const float trackHeight = theme.px(kTrackHeight);
  const float inset = theme.px(kThumbInset);
  const float diameter = trackHeight - inset * 2.0f;
  const float travel = trackWidth - inset * 2.0f - diameter;

  SurfaceStyle track = trackStyle(theme);
  const BorderRadius pill = BorderRadius::all(trackHeight * 0.5f);
  track.rest.radius = pill;
  track.hovered.radius = pill;
  track.pressed.radius = pill;
  track.selected.radius = pill;
  track.disabled.radius = pill;
  if (spec.value && spec.enabled) {
    track.rest.color = theme.accentSoft;
    track.rest.borderColor = theme.accent;
    track.hovered.color = theme.accentSoft;
    track.hovered.borderColor = theme.accent;
  }

  fltr::Color thumb = theme.textDim;
  if (!spec.enabled) {
    thumb = theme.textFaint;
  } else if (spec.value) {
    thumb = theme.accent;
  }

  return RawToggle::make({
      .key = spec.key,
      .value = spec.value ? ToggleValue::On : ToggleValue::Off,
      .onChanged = spec.onChanged,
      // A non-zero drag extent is what makes this a switch rather than a
      // checkbox: it creates the drag recognizer, and the thumb follows the
      // finger instead of jumping when it is let go.
      .dragExtent = travel,
      .enabled = spec.enabled,
      .autofocus = spec.autofocus,
      .child = Padding::make({
          .padding = EdgeInsets::symmetric(theme.unit * 1.75f, theme.unit * 1.25f),
          .child = Row::make({
              .crossAxisAlignment = CrossAxisAlignment::Center,
              .children =
                  {
                      body(theme, spec.label),
                      spacer(),
                      label(theme, spec.hint),
                      gap(theme.unit * 1.5f),
                      StyledSurface::make({
                          .style = track,
                          .child = SizedBox::make({
                              .size = {trackWidth, trackHeight},
                              .child = Align::make({
                                  .alignment = Alignment::centerLeft(),
                                  .child = Padding::make({
                                      .padding = EdgeInsets::all(inset),
                                      .child = SwitchThumb::make({
                                          .travel = travel,
                                          .diameter = diameter,
                                          .color = thumb,
                                      }),
                                  }),
                              }),
                          }),
                      }),
                  },
          }),
      }),
  });
}

}  // namespace demo
