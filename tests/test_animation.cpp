#include "testing.hpp"

#include <algorithm>
#include <memory>

#include "fltr/animation/tween.hpp"
#include "fltr/widgets/binding.hpp"
#include "fltr/widgets/reactive.hpp"
#include "widget_harness.hpp"

using namespace fltr;
using namespace fltrtest;

namespace {

/// A driver and the clock that feeds it, for the tests that exercise the driver
/// itself with no tree around it. Time is explicit here, as it is everywhere
/// else: nothing in this framework reads a real one.
class Clock {
public:
  explicit Clock(AnimationDriver::Config config = {}) : driver_(config) {
    driver_.attach(registry_);
  }

  void advance(float seconds) { registry_.tick(seconds); }

  AnimationDriver& driver() noexcept { return driver_; }
  TickerRegistry& registry() noexcept { return registry_; }

private:
  // The registry outlives the driver, whose ticker is subscribed to it.
  TickerRegistry registry_;
  AnimationDriver driver_;
};

/// Counts how many times a channel fired, which is how "value and status are
/// notified independently" is measured rather than argued.
class Counter {
public:
  explicit Counter(Listenable& source) {
    source.subscribe(
        subscription_, [](void* p) { ++static_cast<Counter*>(p)->count_; }, this);
  }

  int count() const noexcept { return count_; }

private:
  int count_ = 0;
  Subscription subscription_;
};

class Fading;

/// A State that owns an animation, as game code would: the driver is attached
/// when the element mounts and released when it unmounts, and what goes into the
/// tree is the animated value itself rather than a sample of it.
class FadingState final : public State<Fading> {
public:
  void initState() override {
    driver_.attach(context().tickers());
    driver_.setConfig({.duration = 0.2f});
  }

  void dispose() override { driver_.detach(); }

  WidgetRef build(BuildContext&) override {
    return Opacity::make({.animation = &fade_, .child = SizedBox::make({.size = {10, 10}})});
  }

  AnimationDriver& driver() noexcept { return driver_; }
  const AnimatedValue<float>& fade() const noexcept { return fade_; }

private:
  AnimationDriver driver_;
  AnimatedValue<float> fade_{driver_, {1.0f, 0.0f}};
};

class Fading final : public Configure<Fading, StatefulWidget> {
public:
  struct Args {
    Key key;
  };

  explicit Fading(const Args& args) : Configure(args.key) {}

  const char* name() const noexcept override { return "Fading"; }

  std::unique_ptr<State<Fading>> createState() const { return std::make_unique<FadingState>(); }
};

FadingState& fadingState(Element& root) {
  return static_cast<FadingState&>(
      static_cast<StatefulElement<Fading>&>(elementFor(root, widgetTypeOf<Fading>())).state());
}

RenderOpacity& opacityOf(Harness& h) {
  return static_cast<RenderOpacity&>(
      *elementFor(h.rootElement(), widgetTypeOf<Opacity>()).renderObject());
}

}  // namespace

// ---------------------------------------------------------------------------
// The driver
// ---------------------------------------------------------------------------

TEST(animation_a_driver_arrives_exactly_and_reports_that_it_settled) {
  Clock clock({.duration = 0.2f});
  CHECK_EQ(clock.driver().status(), AnimationStatus::Dismissed);
  CHECK(!clock.driver().ticking());

  clock.driver().forward();
  CHECK_EQ(clock.driver().status(), AnimationStatus::Forward);
  CHECK(clock.driver().ticking());

  clock.advance(0.05f);
  CHECK_NEAR(clock.driver().value(), 0.25f, 1e-5);
  clock.advance(0.05f);
  CHECK_NEAR(clock.driver().value(), 0.5f, 1e-5);

  // Overshooting the remaining time arrives exactly, rather than beyond.
  clock.advance(0.5f);
  CHECK_EQ(clock.driver().value(), 1.0f);
  CHECK_EQ(clock.driver().status(), AnimationStatus::Completed);
  CHECK(!clock.driver().isAnimating());
  CHECK(!clock.driver().ticking());

  clock.advance(0.1f);
  CHECK_EQ(clock.driver().value(), 1.0f);
}

TEST(animation_frame_durations_are_the_consumers_and_need_not_be_uniform) {
  Clock clock({.duration = 0.1f});
  clock.driver().forward();

  // A stutter, a fast frame, and a long one. What matters is the total.
  clock.advance(0.031f);
  clock.advance(0.004f);
  clock.advance(0.02f);
  CHECK_NEAR(clock.driver().progress(), 0.55f, 1e-5);

  clock.advance(0.045f);
  CHECK_EQ(clock.driver().progress(), 1.0f);
  CHECK_EQ(clock.driver().status(), AnimationStatus::Completed);
}

TEST(animation_value_and_status_are_notified_independently) {
  Clock clock({.duration = 0.1f});
  Counter values(clock.driver());
  Counter statuses(clock.driver().statusChanges());

  clock.driver().forward();
  CHECK_EQ(values.count(), 0);
  CHECK_EQ(statuses.count(), 1);

  for (int i = 0; i < 4; ++i) clock.advance(0.025f);
  CHECK_EQ(values.count(), 4);
  CHECK_EQ(statuses.count(), 2);

  // Settled: neither channel fires again however much time passes.
  clock.advance(0.1f);
  CHECK_EQ(values.count(), 4);
  CHECK_EQ(statuses.count(), 2);
}

TEST(animation_reversing_mid_flight_is_continuous) {
  Clock clock({.duration = 0.2f});
  clock.driver().forward();
  clock.advance(0.1f);

  const float atReversal = clock.driver().value();
  clock.driver().reverse();
  CHECK_EQ(clock.driver().value(), atReversal);
  CHECK_EQ(clock.driver().status(), AnimationStatus::Reverse);

  // Reversing from halfway takes half the duration, so the value never leaves
  // the range and never doubles back on itself.
  clock.advance(0.05f);
  CHECK_NEAR(clock.driver().value(), 0.25f, 1e-5);
  clock.advance(0.05f);
  CHECK_EQ(clock.driver().value(), 0.0f);
  CHECK_EQ(clock.driver().status(), AnimationStatus::Dismissed);
}

TEST(animation_repeated_interruption_accumulates_no_error) {
  Clock clock({.duration = 0.2f});
  float lowest = 1.0f;
  float highest = 0.0f;

  // The pointer entering and leaving over and over, which is the ordinary case
  // rather than the exotic one.
  for (int i = 0; i < 40; ++i) {
    clock.driver().forward();
    clock.advance(0.017f);
    clock.advance(0.013f);
    lowest = std::min(lowest, clock.driver().progress());
    highest = std::max(highest, clock.driver().progress());

    clock.driver().reverse();
    clock.advance(0.019f);
    clock.advance(0.011f);
    lowest = std::min(lowest, clock.driver().progress());
    highest = std::max(highest, clock.driver().progress());
  }

  CHECK(lowest >= 0.0f);
  CHECK(highest <= 1.0f);

  // Whatever the interruptions did, one uninterrupted run still ends exactly at
  // the end -- no drift to clamp away.
  clock.driver().forward();
  clock.advance(1.0f);
  CHECK_EQ(clock.driver().progress(), 1.0f);
  CHECK_EQ(clock.driver().status(), AnimationStatus::Completed);
}

TEST(animation_retargeting_proceeds_from_the_current_value) {
  Clock clock({.duration = 0.2f});
  clock.driver().forward();
  clock.advance(0.12f);
  CHECK_NEAR(clock.driver().progress(), 0.6f, 1e-5);

  clock.driver().animateTo(0.25f);
  CHECK_NEAR(clock.driver().progress(), 0.6f, 1e-5);
  CHECK_EQ(clock.driver().status(), AnimationStatus::Reverse);

  clock.advance(0.04f);
  CHECK_NEAR(clock.driver().progress(), 0.4f, 1e-5);
  clock.advance(0.04f);
  CHECK_NEAR(clock.driver().progress(), 0.25f, 1e-5);
  CHECK(!clock.driver().isAnimating());
  CHECK_EQ(clock.driver().status(), AnimationStatus::Dismissed);
}

TEST(animation_a_repeating_driver_wraps_and_a_ping_pong_one_reflects) {
  Clock wrapping({.duration = 0.1f});
  wrapping.driver().repeat();
  wrapping.advance(0.08f);
  CHECK_NEAR(wrapping.driver().progress(), 0.8f, 1e-5);
  wrapping.advance(0.04f);
  CHECK_NEAR(wrapping.driver().progress(), 0.2f, 1e-5);
  CHECK_EQ(wrapping.driver().status(), AnimationStatus::Forward);

  Clock reflecting({.duration = 0.1f});
  reflecting.driver().repeat(true);
  reflecting.advance(0.08f);
  CHECK_NEAR(reflecting.driver().progress(), 0.8f, 1e-5);
  reflecting.advance(0.04f);
  CHECK_NEAR(reflecting.driver().progress(), 0.8f, 1e-5);
  CHECK_EQ(reflecting.driver().status(), AnimationStatus::Reverse);
  reflecting.advance(0.1f);
  CHECK_NEAR(reflecting.driver().progress(), 0.2f, 1e-5);
  CHECK_EQ(reflecting.driver().status(), AnimationStatus::Forward);
}

TEST(animation_a_stopped_or_settled_driver_holds_no_subscription) {
  Clock clock({.duration = 0.1f});
  CHECK_EQ(clock.registry().activeTickerCount(), std::size_t{0});

  clock.driver().forward();
  CHECK_EQ(clock.registry().activeTickerCount(), std::size_t{1});

  clock.advance(0.05f);
  clock.driver().stop();
  CHECK_EQ(clock.registry().activeTickerCount(), std::size_t{0});

  clock.driver().forward();
  clock.advance(0.5f);
  CHECK_EQ(clock.registry().activeTickerCount(), std::size_t{0});
}

TEST(animation_a_muted_driver_takes_no_time_and_resumes_where_it_stopped) {
  Clock clock({.duration = 0.2f});
  clock.driver().forward();
  clock.advance(0.1f);

  clock.driver().setMuted(true);
  CHECK_EQ(clock.registry().activeTickerCount(), std::size_t{0});
  CHECK(clock.driver().isAnimating());

  for (int i = 0; i < 10; ++i) clock.advance(0.1f);
  CHECK_NEAR(clock.driver().progress(), 0.5f, 1e-5);

  // A per-frame delta rather than an absolute clock is what makes this exact:
  // there is no elapsed time to reconcile on the way back.
  clock.driver().setMuted(false);
  clock.advance(0.1f);
  CHECK_EQ(clock.driver().progress(), 1.0f);
}

TEST(animation_a_driver_started_before_it_has_a_clock_begins_when_it_gets_one) {
  TickerRegistry registry;
  AnimationDriver driver({.duration = 0.1f});

  driver.forward();
  CHECK(driver.isAnimating());
  CHECK(!driver.ticking());

  driver.attach(registry);
  CHECK(driver.ticking());
  registry.tick(0.05f);
  CHECK_NEAR(driver.progress(), 0.5f, 1e-5);
}

TEST(animation_time_may_not_run_backwards) {
  TickerRegistry registry;
  CHECK_THROWS(registry.tick(-0.01f));
}

// ---------------------------------------------------------------------------
// Curves and interpolation
// ---------------------------------------------------------------------------

TEST(animation_curves_are_pinned_at_their_ends_and_stay_in_range) {
  const Curve curves[] = {Curves::linear,      Curves::easeIn,       Curves::easeOut,
                          Curves::easeInOut,   Curves::easeInCubic,  Curves::easeOutCubic,
                          Curves::easeInOutCubic};

  for (const Curve& curve : curves) {
    CHECK_EQ(curve(0.0f), 0.0f);
    CHECK_EQ(curve(1.0f), 1.0f);

    float previous = 0.0f;
    for (int i = 1; i <= 20; ++i) {
      const float v = curve(static_cast<float>(i) / 20.0f);
      CHECK(v >= previous - 1e-6f);
      CHECK(v >= 0.0f && v <= 1.0f);
      previous = v;
    }
  }

  // Ease-in leaves slowly and ease-out arrives slowly, which is the whole point
  // of having both.
  CHECK(Curves::easeIn(0.25f) < 0.25f);
  CHECK(Curves::easeOut(0.25f) > 0.25f);
}

TEST(animation_a_reverse_curve_is_the_one_case_reversing_is_not_continuous) {
  Clock clock({.duration = 0.2f, .curve = Curves::easeOut, .reverseCurve = Curves::easeIn});
  clock.driver().forward();
  clock.advance(0.1f);
  CHECK_NEAR(clock.driver().value(), 0.75f, 1e-5);

  // Documented rather than hidden: a distinct reverse curve changes the value at
  // the instant of the reversal. One curve in both directions does not.
  clock.driver().reverse();
  CHECK_NEAR(clock.driver().value(), 0.25f, 1e-5);
}

TEST(animation_interpolation_covers_the_types_the_widget_layer_uses) {
  CHECK_EQ(lerp(2.0f, 4.0f, 0.5f), 3.0f);
  CHECK_EQ(lerp(Offset{0, 10}, Offset{10, 20}, 0.5f), (Offset{5, 15}));
  CHECK_EQ(lerp(Size{0, 0}, Size{10, 20}, 0.25f), (Size{2.5f, 5}));
  CHECK_EQ(lerp(Rect::fromLTWH(0, 0, 10, 10), Rect::fromLTWH(10, 10, 10, 10), 0.5f),
           Rect::fromLTWH(5, 5, 10, 10));
  CHECK_EQ(lerp(EdgeInsets::all(0), EdgeInsets::all(8), 0.5f), EdgeInsets::all(4));
  CHECK_EQ(lerp(Alignment::topLeft(), Alignment::bottomRight(), 0.5f), Alignment::center());
  CHECK_EQ(lerp(BorderRadius::all(0), BorderRadius::all(8), 0.5f), BorderRadius::all(4));
  CHECK_EQ(lerp(Color{0, 0, 0, 0}, Color{200, 100, 50, 255}, 0.5f), (Color{100, 50, 25, 128}));

  const BoxDecoration faded{.color = Color{0, 0, 0, 0}, .radius = BorderRadius::all(0)};
  const BoxDecoration solid{.color = Color{100, 100, 100, 200}, .radius = BorderRadius::all(4)};
  const BoxDecoration half = lerp(faded, solid, 0.5f);
  CHECK_EQ(half.color, (Color{50, 50, 50, 100}));
  CHECK_EQ(half.radius, BorderRadius::all(2));

  CHECK_EQ(lerp(Transform2D::identity(), Transform2D::scaling(2, 2), 0.5f),
           Transform2D::scaling(1.5f, 1.5f));

  static_assert(Interpolatable<float>);
  static_assert(Interpolatable<Color>);
  static_assert(Interpolatable<BoxDecoration>);
  static_assert(!Interpolatable<const char*>);
}

TEST(animation_an_animated_value_composes_interpolation_with_easing) {
  Clock clock({.duration = 0.2f, .curve = Curves::easeOut});
  AnimatedValue<Color> tint{clock.driver(), {Color{0, 0, 0, 255}, Color{100, 200, 40, 255}}};
  CHECK_EQ(tint.value(), (Color{0, 0, 0, 255}));

  clock.driver().forward();
  clock.advance(0.1f);

  // The driver applies the curve and the tween the interpolation; there is
  // nothing in between them to configure.
  CHECK_NEAR(clock.driver().value(), 0.75f, 1e-5);
  CHECK_EQ(tint.value(), lerp(Color{0, 0, 0, 255}, Color{100, 200, 40, 255}, 0.75f));

  clock.advance(0.5f);
  CHECK_EQ(tint.value(), (Color{100, 200, 40, 255}));
}

// ---------------------------------------------------------------------------
// The two consumption paths
// ---------------------------------------------------------------------------

TEST(animation_advancing_a_paint_only_animation_rebuilds_nothing_and_relayouts_nothing) {
  Harness h;
  AnimationDriver driver({.duration = 0.2f});
  AnimatedValue<float> fade{driver, {1.0f, 0.0f}};

  h.attach([&] {
    return Opacity::make({.animation = &fade, .child = SizedBox::make({.size = {10, 10}})});
  });
  driver.attach(h.binding().tickers());
  h.frame();
  const Scene before = h.frame();

  driver.forward();
  const Scene after = h.frame(0.05f);

  CHECK_EQ(h.buildOwner().buildCount(), 0);
  CHECK_EQ(h.pipeline().stats().layouts, 0);
  CHECK_EQ(h.pipeline().stats().boundariesRepainted, 1);
  CHECK_NEAR(opacityOf(h).opacity(), 0.75f, 1e-5);
  CHECK_NE(after.revision, before.revision);

  for (int i = 0; i < 8; ++i) h.frame(0.02f);
  CHECK_EQ(h.buildOwner().buildCount(), 0);
  CHECK_EQ(opacityOf(h).opacity(), 0.0f);
}

TEST(animation_the_same_value_drives_a_rebuild_through_watch) {
  Harness h;
  AnimationDriver driver({.duration = 0.1f});
  AnimatedValue<float> fade{driver, {0.0f, 20.0f}};
  int builds = 0;
  float seen = -1.0f;

  h.attach([&] {
    return Watch<float>::make({
        .value = &fade,
        .builder =
            [&](BuildContext&, const float& size) {
              ++builds;
              seen = size;
              return SizedBox::make({.size = Size::square(size)});
            },
    });
  });
  driver.attach(h.binding().tickers());
  h.frame();
  CHECK_EQ(builds, 1);

  driver.forward();
  h.frame(0.05f);
  CHECK_EQ(builds, 2);
  CHECK_NEAR(seen, 10.0f, 1e-5);
  // The general path: structure follows the value, so this one does relayout.
  CHECK(h.pipeline().stats().layouts > 0);

  h.frame(0.05f);
  CHECK_EQ(builds, 3);
  CHECK_EQ(seen, 20.0f);
}

TEST(animation_a_driven_property_ignores_the_constant_beside_it) {
  Harness h;
  AnimationDriver driver({.duration = 0.2f});
  AnimatedValue<float> fade{driver, {1.0f, 0.0f}};
  float constant = 1.0f;

  ScriptedRoot root(h, [&] {
    return Opacity::make(
        {.opacity = constant, .animation = &fade, .child = SizedBox::make({.size = {10, 10}})});
  });
  driver.attach(h.binding().tickers());
  h.frame();
  h.frame();

  // One widget covers both a constant and a driven value, so the render object
  // has to settle which wins -- and not invalidate itself over the one that
  // does not.
  constant = 0.25f;
  root.rebuild();
  h.frame();

  CHECK_EQ(opacityOf(h).opacity(), 1.0f);
  CHECK_EQ(h.pipeline().stats().boundariesRepainted, 0);
}

TEST(animation_a_tick_that_did_not_move_the_value_wakes_nobody) {
  Harness h;
  AnimationDriver driver({.duration = 1.0f});
  AnimatedValue<Color> tint{driver, {Color{0, 0, 0, 255}, Color{8, 0, 0, 255}}};
  int builds = 0;

  h.attach([&] {
    return Watch<Color>::make({
        .value = &tint,
        .builder =
            [&](BuildContext&, const Color&) {
              ++builds;
              return SizedBox::make({.size = {5, 5}});
            },
    });
  });
  driver.attach(h.binding().tickers());
  h.frame();
  builds = 0;

  // Eight steps of colour spread over a second: at sixty frames a second, most
  // frames interpolate to the byte the frame before already had, and an
  // animated value that did not move notifies nobody.
  driver.forward();
  for (int i = 0; i < 30; ++i) h.frame(1.0f / 60.0f);
  CHECK_NEAR(driver.progress(), 0.5f, 1e-4);
  CHECK_EQ(tint.value(), (Color{4, 0, 0, 255}));
  CHECK_EQ(builds, 4);
}

TEST(animation_a_frame_in_which_no_time_passed_does_nothing) {
  Harness h;
  AnimationDriver driver({.duration = 0.2f});
  AnimatedValue<float> fade{driver, {1.0f, 0.0f}};

  h.attach([&] {
    return Opacity::make({.animation = &fade, .child = SizedBox::make({.size = {10, 10}})});
  });
  driver.attach(h.binding().tickers());
  h.frame();
  const Scene settled = h.frame();

  driver.forward();
  for (int i = 0; i < 3; ++i) {
    CHECK_EQ(h.frame().revision, settled.revision);
  }
  CHECK_EQ(opacityOf(h).opacity(), 1.0f);
}

TEST(animation_a_tree_with_nothing_animating_asks_for_no_frames) {
  Harness h;
  AnimationDriver driver({.duration = 0.2f});
  AnimatedValue<float> fade{driver, {1.0f, 0.0f}};

  h.attach([&] {
    return Opacity::make({.animation = &fade, .child = SizedBox::make({.size = {10, 10}})});
  });
  driver.attach(h.binding().tickers());
  h.frame();
  CHECK(!h.binding().needsFrame());

  driver.forward();
  CHECK(h.binding().needsFrame());

  for (int i = 0; i < 20 && h.binding().needsFrame(); ++i) h.frame(0.02f);
  CHECK(!h.binding().needsFrame());
  CHECK_EQ(opacityOf(h).opacity(), 0.0f);
}

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

TEST(animation_an_animation_in_an_unmounted_subtree_takes_no_time_and_holds_no_ticker) {
  Harness h;
  bool visible = true;

  ScriptedRoot root(h, [&] {
    return Column::make({.children = {visible ? Fading::make({}) : WidgetRef{}}});
  });
  h.frame();

  fadingState(h.rootElement()).driver().forward();
  h.frame(0.05f);
  CHECK_EQ(h.binding().tickers().activeTickerCount(), std::size_t{1});
  CHECK_NEAR(opacityOf(h).opacity(), 0.75f, 1e-5);

  visible = false;
  root.rebuild();
  h.frame();

  CHECK_EQ(h.binding().tickers().activeTickerCount(), std::size_t{0});
  CHECK(!h.binding().needsFrame());

  // Nothing is left to reach: the frames go on, the animation does not.
  for (int i = 0; i < 5; ++i) h.frame(0.1f);
  CHECK_EQ(h.buildOwner().buildCount(), 0);
}

TEST(animation_a_state_that_disposes_releases_its_ticker_before_it_is_destroyed) {
  Harness h;
  h.attach([&] { return Fading::make({}); });
  h.frame();

  fadingState(h.rootElement()).driver().forward();
  CHECK_EQ(h.binding().tickers().activeTickerCount(), std::size_t{1});

  Element& fading = elementFor(h.rootElement(), widgetTypeOf<Fading>());
  fading.detachRenderObject();
  fading.unmount();
  CHECK_EQ(h.binding().tickers().activeTickerCount(), std::size_t{0});
}

TEST(animation_a_frame_of_animation_allocates_nothing) {
  Harness h;
  AnimationDriver driver({.duration = 1.0f});
  AnimatedValue<float> fade{driver, {1.0f, 0.0f}};

  h.attach([&] {
    return Opacity::make({.animation = &fade, .child = SizedBox::make({.size = {10, 10}})});
  });
  driver.attach(h.binding().tickers());

  // Warm the display lists, the dirty lists and the arena.
  driver.forward();
  for (int i = 0; i < 4; ++i) h.frame(0.02f);

  const std::size_t before = allocationCount();
  for (int i = 0; i < 8; ++i) h.frame(0.02f);
  CHECK_EQ(allocationCount() - before, std::size_t{0});
  CHECK(driver.isAnimating());
}
