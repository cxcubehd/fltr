#include "ui/widgets/readout.hh"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "fltr/widgets/basic.hpp"
#include "fltr/widgets/reactive.hpp"
#include "ui/widgets/panel.hh"

namespace demo {

using fltr::Alignment;
using fltr::BorderRadius;
using fltr::BuildContext;
using fltr::ClipRect;
using fltr::Column;
using fltr::CrossAxisAlignment;
using fltr::DecoratedBox;
using fltr::MainAxisSize;
using fltr::SizedBox;
using fltr::Text;
using fltr::Transform2D;
using fltr::Watch;
using fltr::WidgetRef;

namespace {

Transform2D horizontalScale(float fraction, float) noexcept {
  // Clamped rather than trusted: a game pushing 1.02 should not overflow the
  // track, and the framework has no opinion about the range of a float.
  const float clamped = std::clamp(fraction, 0.0f, 1.0f);
  return Transform2D::scaling(clamped, 1.0f);
}

}  // namespace

// ---------------------------------------------------------------------------
// Meter
// ---------------------------------------------------------------------------

void MeterState::initState() { scale_.bind(widget().args().fraction, &horizontalScale, 0.0f); }

void MeterState::didUpdateWidget(const Meter& previous) {
  if (widget().args().fraction != previous.args().fraction) {
    scale_.bind(widget().args().fraction, &horizontalScale, 0.0f);
  }
}

void MeterState::dispose() { scale_.release(); }

WidgetRef MeterState::build(BuildContext&) {
  const Meter::Args& args = widget().args();

  return DecoratedBox::make({
      .decoration = {.color = args.track, .radius = BorderRadius::all(args.height * 0.5f)},
      .child = ClipRect::make({
          .radius = BorderRadius::all(args.height * 0.5f),
          .child = fltr::Transform::make({
              .animation = &scale_,
              // Scaling about the left edge is what makes this a fill rather
              // than a zoom -- and it is a paint operation, so no layout runs.
              .origin = Alignment::centerLeft(),
              .child = DecoratedBox::make({
                  .decoration = {.color = args.fill},
                  .child = SizedBox::make({.size = {fltr::kInf, args.height}}),
              }),
          }),
      }),
  });
}

// ---------------------------------------------------------------------------
// CountUp
// ---------------------------------------------------------------------------

void CountUpState::initState() {
  driver_.attach(context().tickers());
  driver_.setConfig({.duration = widget().args().duration, .curve = fltr::Curves::easeOutCubic});

  const float settled =
      widget().args().value != nullptr ? static_cast<float>(widget().args().value->value()) : 0.0f;
  shown_.setTween({settled, settled});

  if (widget().args().value != nullptr) {
    fltr::subscribeMember<CountUpState, &CountUpState::onTargetChanged>(*widget().args().value,
                                                                       target_, this);
  }
  fltr::subscribeMember<CountUpState, &CountUpState::onTick>(shown_, tick_, this);
}

void CountUpState::didUpdateWidget(const CountUp& previous) {
  driver_.setConfig({.duration = widget().args().duration, .curve = fltr::Curves::easeOutCubic});
  if (widget().args().value == previous.args().value) return;

  target_.detach();
  if (widget().args().value != nullptr) {
    fltr::subscribeMember<CountUpState, &CountUpState::onTargetChanged>(*widget().args().value,
                                                                       target_, this);
    onTargetChanged();
  }
}

void CountUpState::dispose() {
  tick_.detach();
  target_.detach();
  driver_.detach();
}

void CountUpState::onTargetChanged() {
  if (widget().args().value == nullptr) return;
  shown_.retarget(static_cast<float>(widget().args().value->value()));
}

void CountUpState::onTick() {
  // This is the rebuild. It happens once per frame while the number is moving
  // and not at all once it has settled, because the driver stops notifying.
  setState([] {});
}

WidgetRef CountUpState::build(BuildContext&) {
  const int rounded = static_cast<int>(std::lround(shown_.value()));
  const int written = std::snprintf(buffer_, sizeof(buffer_), "%d", rounded);
  text_ = written > 0 ? std::string_view{buffer_, static_cast<std::size_t>(written)}
                      : std::string_view{buffer_, 0};

  // The view points at this State's own buffer, which outlives every build it
  // is handed to -- `Text` copies nothing.
  return Text::make({.text = text_, .style = widget().args().style});
}

// ---------------------------------------------------------------------------
// Compositions
// ---------------------------------------------------------------------------

WidgetRef gauge(const Theme& theme, std::string_view name, fltr::ValueListenable<float>* fraction,
                fltr::Color fill) {
  return Column::make({
      .crossAxisAlignment = CrossAxisAlignment::Stretch,
      .mainAxisSize = MainAxisSize::Min,
      .spacing = theme.unit * 0.75f,
      .children =
          {
              label(theme, name),
              Meter::make({
                  .fraction = fraction,
                  .fill = fill,
                  .track = theme.surfaceRaised,
                  .height = theme.px(6.0f),
              }),
          },
  });
}

WidgetRef statistic(const Theme& theme, std::string_view name, fltr::ValueListenable<int>* value,
                    TextSource source, const void* owner) {
  return Column::make({
      .crossAxisAlignment = CrossAxisAlignment::Start,
      .mainAxisSize = MainAxisSize::Min,
      .spacing = theme.unit * 0.75f,
      .children =
          {
              label(theme, name),
              // `Watch` rebuilds this one subtree when the value moves, and does
              // nothing at all when the game pushes the same number again. The
              // style is read from the ambient theme inside the builder rather
              // than captured, which keeps the closure inside `Callback`'s
              // 32-byte inline budget.
              Watch<int>::make({
                  .value = value,
                  .builder = [source, owner](BuildContext& context, const int&) -> WidgetRef {
                    const Theme& current = ThemeScope::of(context);
                    return Text::make(
                        {.text = source(owner), .style = monoStyle(current, current.text)});
                  },
              }),
          },
  });
}

}  // namespace demo
