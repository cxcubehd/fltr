#include "ui/widgets/slider_row.hh"

#include <algorithm>

#include "fltr/components/states.hpp"
#include "fltr/widgets/basic.hpp"
#include "ui/widgets/panel.hh"

namespace demo {

using fltr::Align;
using fltr::Alignment;
using fltr::BorderRadius;
using fltr::BuildContext;
using fltr::ClipRect;
using fltr::ComponentScope;
using fltr::CrossAxisAlignment;
using fltr::DecoratedBox;
using fltr::EdgeInsets;
using fltr::Padding;
using fltr::RawSlider;
using fltr::Row;
using fltr::SizedBox;
using fltr::Stack;
using fltr::StackFit;
using fltr::Text;
using fltr::Transform2D;
using fltr::WidgetRef;

namespace {

constexpr float kTrackThickness = 4.0f;

Transform2D scaleX(float fraction, float) noexcept {
  return Transform2D::scaling(std::clamp(fraction, 0.0f, 1.0f), 1.0f);
}

Transform2D slideX(float fraction, float travel) noexcept {
  return Transform2D::translation({std::clamp(fraction, 0.0f, 1.0f) * travel, 0.0f});
}

WidgetRef bar(fltr::Color color) {
  return DecoratedBox::make({
      .decoration = {.color = color, .radius = BorderRadius::all(kTrackThickness * 0.5f)},
      .child = SizedBox::make({.size = {fltr::kInf, kTrackThickness}}),
  });
}

}  // namespace

void SliderTrackState::dispose() {
  fill_.release();
  thumb_.release();
}

WidgetRef SliderTrackState::build(BuildContext& context) {
  const SliderTrack::Args& args = widget().args();
  const float travel = std::max(0.0f, args.width - args.thumbSize);

  fltr::ValueListenable<float>* fraction = ComponentScope::fractionOf(context);
  fill_.bind(fraction, &scaleX, 0.0f);
  thumb_.bind(fraction, &slideX, travel);

  return SizedBox::make({
      .size = {args.width, args.thumbSize + 6.0f},
      .child = Stack::make({
          .fit = StackFit::Expand,
          .children =
              {
                  Align::make({.child = bar(args.track)}),
                  Align::make({
                      .child = ClipRect::make({
                          .radius = BorderRadius::all(kTrackThickness * 0.5f),
                          .child = fltr::Transform::make({
                              .animation = &fill_,
                              .origin = Alignment::centerLeft(),
                              .child = bar(args.fill),
                          }),
                      }),
                  }),
                  Align::make({
                      .alignment = Alignment::centerLeft(),
                      .child = fltr::Transform::make({
                          .animation = &thumb_,
                          .origin = Alignment::centerLeft(),
                          .child = DecoratedBox::make({
                              .decoration = {.color = args.thumb,
                                             .radius = BorderRadius::all(args.thumbSize * 0.5f)},
                              .child = SizedBox::make({.size = fltr::Size::square(args.thumbSize)}),
                          }),
                      }),
                  }),
              },
      }),
  });
}

WidgetRef sliderRow(const Theme& theme, const SliderRowSpec& spec) {
  fltr::TextStyle value = monoStyle(theme, spec.enabled ? theme.text : theme.textFaint);

  return RawSlider::make({
      .key = spec.key,
      .value = spec.value,
      .min = spec.min,
      .max = spec.max,
      .divisions = spec.divisions,
      .onChanged = spec.onChanged,
      // The value maps across the track less the thumb, so both ends stay
      // reachable -- the same number the visual uses for its travel.
      .thumbExtent = 14.0f,
      .enabled = spec.enabled,
      .child = Padding::make({
          .padding = EdgeInsets::symmetric(theme.unit * 1.75f, theme.unit * 1.25f),
          .child = Row::make({
              .crossAxisAlignment = CrossAxisAlignment::Center,
              .children =
                  {
                      body(theme, spec.label),
                      spacer(),
                      Text::make({.text = spec.valueText, .style = value}),
                      gap(theme.unit * 1.5f),
                      SliderTrack::make({
                          .fill = spec.enabled ? theme.accent : theme.border,
                          .track = theme.surfaceRaised,
                          .thumb = spec.enabled ? theme.text : theme.textFaint,
                      }),
                  },
          }),
      }),
  });
}

}  // namespace demo
