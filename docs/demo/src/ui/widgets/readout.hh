#pragma once

#include <memory>
#include <string_view>

#include "fltr/animation/tween.hpp"
#include "fltr/core/observable.hpp"
#include "fltr/paint/text.hpp"
#include "fltr/widgets/framework.hpp"
#include "ui/theme.hh"
#include "ui/widgets/mapped_value.hh"

namespace demo {

// ---------------------------------------------------------------------------
// Meter -- the cheap path
// ---------------------------------------------------------------------------

class Meter;

class MeterState final : public fltr::State<Meter> {
public:
  void initState() override;
  void didUpdateWidget(const Meter& previous) override;
  void dispose() override;
  fltr::WidgetRef build(fltr::BuildContext& context) override;

private:
  /// A 0..1 fraction turned into a horizontal scale about the left edge. The
  /// `Transform` render object observes this and repaints itself; the meter is
  /// never rebuilt, however fast the value moves.
  MappedValue<fltr::Transform2D> scale_;
};

/// A hairline bar that fills from the left. Takes a `ValueListenable<float>`,
/// which is what both `Observable<float>` and `RawProgress` publish, so the same
/// widget serves a pushed game value and a component's fraction.
class Meter final : public fltr::Configure<Meter, fltr::StatefulWidget> {
public:
  struct Args {
    fltr::Key key;
    /// Consumer-owned and must outlive this widget, like every pointer a widget
    /// holds.
    fltr::ValueListenable<float>* fraction = nullptr;
    fltr::Color fill;
    fltr::Color track;
    float height = 6.0f;
  };

  explicit Meter(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Meter"; }
  const Args& args() const noexcept { return args_; }

  std::unique_ptr<fltr::State<Meter>> createState() const {
    return std::make_unique<MeterState>();
  }

private:
  Args args_;
};

// ---------------------------------------------------------------------------
// CountUp -- the expensive path, on purpose
// ---------------------------------------------------------------------------

class CountUp;

class CountUpState final : public fltr::State<CountUp> {
public:
  void initState() override;
  void didUpdateWidget(const CountUp& previous) override;
  void dispose() override;
  fltr::WidgetRef build(fltr::BuildContext& context) override;

private:
  void onTargetChanged();
  void onTick();

  fltr::AnimationDriver driver_;
  fltr::AnimatedValue<float> shown_{driver_, {}};
  fltr::Subscription target_;
  fltr::Subscription tick_;
  char buffer_[16] = "0";
  std::string_view text_{buffer_, 1};
};

/// A number that rolls up to its new value.
///
/// This one *does* rebuild every frame it animates, and it has to: the animated
/// quantity is a string, and there is no render-attached animated text in fltr.
/// It is here as the deliberate counterexample to `Meter` -- one screen, both
/// paths, and the cost of each visible in the frame stats.
class CountUp final : public fltr::Configure<CountUp, fltr::StatefulWidget> {
public:
  struct Args {
    fltr::Key key;
    fltr::ValueListenable<int>* value = nullptr;
    fltr::TextStyle style;
    float duration = 0.45f;
  };

  explicit CountUp(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "CountUp"; }
  const Args& args() const noexcept { return args_; }

  std::unique_ptr<fltr::State<CountUp>> createState() const {
    return std::make_unique<CountUpState>();
  }

private:
  Args args_;
};

// ---------------------------------------------------------------------------
// Compositions
// ---------------------------------------------------------------------------

/// A label above a meter, which is what the hull and shield gauges are.
fltr::WidgetRef gauge(const Theme& theme, std::string_view name,
                      fltr::ValueListenable<float>* fraction, fltr::Color fill);

/// Where a statistic's displayed text comes from. A function pointer and an
/// owner rather than a closure: `Callback` stores 32 bytes inline and static-
/// asserts on more, which is a real constraint the call sites have to respect.
using TextSource = std::string_view (*)(const void* owner);

/// A label above a value that is re-read whenever the value changes -- and only
/// then, because pushing an unchanged value notifies nobody.
fltr::WidgetRef statistic(const Theme& theme, std::string_view name,
                          fltr::ValueListenable<int>* value, TextSource source,
                          const void* owner);

}  // namespace demo
