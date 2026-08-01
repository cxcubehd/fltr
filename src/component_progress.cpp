#include "fltr/components/progress.hpp"

#include <algorithm>

namespace fltr {

void RawProgressState::initState() {
  driver_.attach(context().tickers());
  subscribeMember<RawProgressState, &RawProgressState::sample>(driver_, tick_, this);
  adoptConfiguration();
}

void RawProgressState::didUpdateWidget(const RawProgress&) { adoptConfiguration(); }

void RawProgressState::dispose() {
  tick_.detach();
  driver_.detach();
}

void RawProgressState::adoptConfiguration() {
  const RawProgress::Args& args = widget().args();
  FLTR_EXPECTS(args.max >= args.min, "a progress range must not run backwards");
  driver_.setConfig({.duration = args.period});
  if (!args.indeterminate) {
    // Stopped rather than merely ignored: a determinate bar holds no ticker, so
    // a screen full of them is a screen with nothing running on it.
    driver_.stop();
    const float span = args.max - args.min;
    fraction_.set(span <= 0.0f ? 0.0f : std::clamp((args.value - args.min) / span, 0.0f, 1.0f));
    return;
  }
  if (!driver_.isAnimating()) driver_.repeat();
  // Published now rather than on the first tick: the sweep starts where the
  // driver already is, and a bar that showed its old determinate fill for a
  // frame after being switched over would be showing a number that means
  // nothing.
  sample();
}

WidgetRef RawProgressState::build(BuildContext&) {
  return ComponentScope::make({.fraction = &fraction_, .child = widget().args().child});
}

}  // namespace fltr
