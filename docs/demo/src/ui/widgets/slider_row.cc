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

Transform2D scaleX(float fraction, float) noexcept {
  return Transform2D::scaling(std::clamp(fraction, 0.0f, 1.0f), 1.0f);
}

Transform2D slideX(float fraction, float travel) noexcept {
  return Transform2D::translation({std::clamp(fraction, 0.0f, 1.0f) * travel, 0.0f});
}

WidgetRef bar(fltr::Color color, float thickness) {
  return DecoratedBox::make({
      .decoration = {.color = color, .radius = BorderRadius::all(thickness * 0.5f)},
      .child = SizedBox::make({.size = {fltr::kInf, thickness}}),
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
      .size = {args.width, args.thumbSize * 1.4f},
      .child = Stack::make({
          .fit = StackFit::Expand,
          .children =
              {
                  Align::make({.child = bar(args.track, args.thickness)}),
                  Align::make({
                      .child = ClipRect::make({
                          .radius = BorderRadius::all(args.thickness * 0.5f),
                          .child = fltr::Transform::make({
                              .animation = &fill_,
                              .origin = Alignment::centerLeft(),
                              .child = bar(args.fill, args.thickness),
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

  // The slider is the track and nothing else. A value read off where the pointer
  // is has to be read across the thing the player is aiming at: wrapping the
  // whole row would spread the range over the label and the readout as well, so
  // the visible track would only be worth the last third of it.
  WidgetRef track = RawSlider::make({
      .key = spec.key,
      .value = spec.value,
      .min = spec.min,
      .max = spec.max,
      .divisions = spec.divisions,
      .onChanged = spec.onChanged,
      // The value maps across the track less the thumb, so both ends stay
      // reachable -- the same number the visual uses for its travel.
      .thumbExtent = theme.px(14.0f),
      .enabled = spec.enabled,
      .child = SliderTrack::make({
          .width = theme.px(180.0f),
          .thumbSize = theme.px(14.0f),
          .thickness = theme.px(4.0f),
          .fill = spec.enabled ? theme.accent : theme.border,
          .track = theme.surfaceRaised,
          .thumb = spec.enabled ? theme.text : theme.textFaint,
      }),
  });

  return Padding::make({
      .padding = EdgeInsets::symmetric(theme.unit * 1.75f, theme.unit * 1.25f),
      .child = Row::make({
          .crossAxisAlignment = CrossAxisAlignment::Center,
          .children =
              {
                  body(theme, spec.label),
                  spacer(),
                  Text::make({.text = spec.valueText, .style = value}),
                  gap(theme.unit * 1.5f),
                  track,
              },
      }),
  });
}

}  // namespace demo
