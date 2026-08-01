#pragma once

#include <memory>

#include "fltr/animation/driver.hpp"
#include "fltr/components/states.hpp"

namespace fltr {

class RawProgress;

class RawProgressState final : public State<RawProgress> {
public:
  void initState() override;
  void didUpdateWidget(const RawProgress& previous) override;
  void dispose() override;
  WidgetRef build(BuildContext& context) override;

private:
  void adoptConfiguration();
  void sample() { fraction_.set(driver_.progress()); }

  Observable<float> fraction_;
  AnimationDriver driver_;
  Subscription tick_;
};

/// How far along something is, published and not drawn.
///
/// The only component with no interaction at all: no focus node, no gestures, no
/// states. What it has is the one thing its visual cannot work out for itself --
/// a 0..1 fraction, either the value the consumer pushed or a sweep that runs on
/// its own when there is no value to report.
class RawProgress final : public Configure<RawProgress, StatefulWidget> {
public:
  struct Args {
    Key key;
    float value = 0.0f;
    float min = 0.0f;
    float max = 1.0f;
    /// Sweeps 0 to 1 and wraps, for work whose extent is unknown. `value` is
    /// ignored while this is set.
    bool indeterminate = false;
    /// Seconds for one sweep. Only the indeterminate case has a clock.
    float period = 1.5f;
    WidgetRef child;
  };

  explicit RawProgress(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "RawProgress"; }
  const Args& args() const noexcept { return args_; }

  std::unique_ptr<State<RawProgress>> createState() const {
    return std::make_unique<RawProgressState>();
  }

private:
  Args args_;
};

}  // namespace fltr
