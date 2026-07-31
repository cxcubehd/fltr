#include "testing.hpp"

#include "fltr/widgets/animated.hpp"
#include "fltr/widgets/binding.hpp"
#include "widget_harness.hpp"

using namespace fltr;
using namespace fltrtest;

namespace {

RenderOpacity& opacityOf(Harness& h) {
  return static_cast<RenderOpacity&>(
      *elementFor(h.rootElement(), widgetTypeOf<Opacity>()).renderObject());
}

RenderPositionedBox& alignOf(Harness& h) {
  return static_cast<RenderPositionedBox&>(
      *elementFor(h.rootElement(), widgetTypeOf<Align>()).renderObject());
}

RenderDecoratedBox& decorationOf(Harness& h) {
  return static_cast<RenderDecoratedBox&>(
      *elementFor(h.rootElement(), widgetTypeOf<DecoratedBox>()).renderObject());
}

RenderTransform& transformOf(Harness& h) {
  return static_cast<RenderTransform&>(
      *elementFor(h.rootElement(), widgetTypeOf<Transform>()).renderObject());
}

WidgetRef leaf() { return SizedBox::make({.size = {10, 10}}); }

}  // namespace

// ---------------------------------------------------------------------------
// Re-targeting on configuration change
// ---------------------------------------------------------------------------

TEST(implicit_a_configuration_change_animates_to_the_new_value) {
  Harness h;
  float opacity = 1.0f;

  ScriptedRoot root(h, [&] {
    return AnimatedOpacity::make({
        .opacity = opacity,
        .animation = {.duration = 0.2f},
        .child = leaf(),
    });
  });
  h.frame();
  CHECK_EQ(opacityOf(h).opacity(), 1.0f);

  // Nothing animates on its own: the first configuration is where it starts.
  h.frame(0.1f);
  CHECK_EQ(opacityOf(h).opacity(), 1.0f);

  // The change lands in the frame it happened, showing the value it starts
  // from; it advances from the next one, because the time this frame reports
  // elapsed before the animation existed.
  opacity = 0.0f;
  root.rebuild();
  h.frame();
  CHECK_EQ(opacityOf(h).opacity(), 1.0f);

  h.frame(0.1f);
  CHECK_NEAR(opacityOf(h).opacity(), 0.5f, 1e-5);
  h.frame(0.1f);
  CHECK_EQ(opacityOf(h).opacity(), 0.0f);
  CHECK(!h.binding().needsFrame());
}

TEST(implicit_an_interruption_continues_from_the_value_on_screen) {
  Harness h;
  float opacity = 1.0f;

  ScriptedRoot root(h, [&] {
    return AnimatedOpacity::make({
        .opacity = opacity,
        .animation = {.duration = 0.2f},
        .child = leaf(),
    });
  });
  h.frame();

  opacity = 0.0f;
  root.rebuild();
  h.frame();
  h.frame(0.05f);
  CHECK_NEAR(opacityOf(h).opacity(), 0.75f, 1e-5);

  // Back the other way, mid-flight. The value on screen does not move at the
  // instant the target changes -- it is where the new interval begins.
  opacity = 1.0f;
  root.rebuild();
  h.frame();
  CHECK_NEAR(opacityOf(h).opacity(), 0.75f, 1e-5);

  h.frame(0.05f);
  CHECK_NEAR(opacityOf(h).opacity(), 0.8125f, 1e-5);
  h.frame(0.2f);
  CHECK_EQ(opacityOf(h).opacity(), 1.0f);
}

TEST(implicit_repeated_interruption_accumulates_no_error) {
  Harness h;
  float opacity = 1.0f;

  ScriptedRoot root(h, [&] {
    return AnimatedOpacity::make({
        .opacity = opacity,
        .animation = {.duration = 0.15f, .curve = Curves::easeOut},
        .child = leaf(),
    });
  });
  h.frame();

  // The pointer entering and leaving a button over and over, never letting the
  // animation finish.
  for (int i = 0; i < 30; ++i) {
    opacity = (i % 2 == 0) ? 0.2f : 1.0f;
    root.rebuild();
    h.frame(0.011f);
    h.frame(0.023f);
    CHECK(opacityOf(h).opacity() >= 0.2f);
    CHECK(opacityOf(h).opacity() <= 1.0f);
  }

  opacity = 1.0f;
  root.rebuild();
  for (int i = 0; i < 20 && h.binding().needsFrame(); ++i) h.frame(0.02f);
  CHECK_EQ(opacityOf(h).opacity(), 1.0f);
}

TEST(implicit_an_animation_to_where_it_already_is_does_not_run) {
  Harness h;
  float opacity = 1.0f;
  BoxDecoration decoration{.color = Color::argb(0xFF204060)};

  ScriptedRoot root(h, [&] {
    return AnimatedOpacity::make({
        .opacity = opacity,
        .animation = {.duration = 0.2f},
        .child = AnimatedDecoration::make({
            .decoration = decoration,
            .animation = {.duration = 0.2f},
            .child = leaf(),
        }),
    });
  });
  h.frame();

  // A rebuild that changes nothing, and one that changes something else.
  root.rebuild();
  h.frame();
  CHECK_EQ(h.binding().tickers().activeTickerCount(), std::size_t{0});

  decoration.color = Color::argb(0xFF000000);
  root.rebuild();
  h.frame();
  CHECK_EQ(h.binding().tickers().activeTickerCount(), std::size_t{1});
}

TEST(implicit_a_target_that_changes_and_changes_back_leaves_nothing_running) {
  Harness h;
  float opacity = 1.0f;

  ScriptedRoot root(h, [&] {
    return AnimatedOpacity::make({
        .opacity = opacity,
        .animation = {.duration = 0.2f},
        .child = leaf(),
    });
  });
  h.frame();

  opacity = 0.0f;
  root.rebuild();
  h.frame();
  CHECK_EQ(h.binding().tickers().activeTickerCount(), std::size_t{1});

  // Back before any time passed, so the value never left where it started. An
  // interval from a value to itself is not an animation.
  opacity = 1.0f;
  root.rebuild();
  h.frame();
  CHECK_EQ(h.binding().tickers().activeTickerCount(), std::size_t{0});
  CHECK_EQ(opacityOf(h).opacity(), 1.0f);
}

// ---------------------------------------------------------------------------
// What a frame of it costs
// ---------------------------------------------------------------------------

TEST(implicit_ticking_rebuilds_nothing_and_relayouts_nothing) {
  Harness h;
  float opacity = 1.0f;

  ScriptedRoot root(h, [&] {
    return AnimatedOpacity::make({
        .opacity = opacity,
        .animation = {.duration = 0.2f},
        .child = leaf(),
    });
  });
  h.frame();

  opacity = 0.0f;
  root.rebuild();
  h.frame();

  // The configuration change was the last build. What the widget handed down is
  // the animated value itself, so every frame after it is one repaint.
  for (int i = 0; i < 8; ++i) {
    h.frame(0.02f);
    CHECK_EQ(h.buildOwner().buildCount(), 0);
    CHECK_EQ(h.pipeline().stats().layouts, 0);
    CHECK_EQ(h.pipeline().stats().boundariesRepainted, 1);
  }
  CHECK_NEAR(opacityOf(h).opacity(), 0.2f, 1e-5);
}

TEST(implicit_an_animated_alignment_lays_out_every_frame_and_says_so) {
  Harness h({100, 40});
  Alignment alignment = Alignment::centerLeft();

  ScriptedRoot root(h, [&] {
    return SizedBox::make({
        .size = {100, 40},
        .child = AnimatedAlign::make({
            .alignment = alignment,
            .animation = {.duration = 0.2f},
            .child = leaf(),
        }),
    });
  });
  h.frame();
  CHECK_EQ(alignOf(h).childOffset(), (Offset{0, 15}));

  alignment = Alignment::centerRight();
  root.rebuild();
  h.frame();
  h.frame(0.1f);

  // The honest cost of animating a property resolved during layout: this is the
  // one of the four that is not free, and it is measurable rather than implied.
  CHECK(h.pipeline().stats().layouts > 0);
  CHECK_EQ(alignOf(h).childOffset(), (Offset{45, 15}));

  h.frame(0.1f);
  CHECK_EQ(alignOf(h).childOffset(), (Offset{90, 15}));
}

TEST(implicit_decoration_and_transform_take_the_paint_only_path) {
  Harness h;
  BoxDecoration decoration{.color = Color{0, 0, 0, 0}, .radius = BorderRadius::all(0)};
  Transform2D transform = Transform2D::identity();

  ScriptedRoot root(h, [&] {
    return AnimatedDecoration::make({
        .decoration = decoration,
        .animation = {.duration = 0.2f},
        .child = AnimatedTransform::make({
            .transform = transform,
            .animation = {.duration = 0.2f},
            .child = leaf(),
        }),
    });
  });
  h.frame();

  decoration = {.color = Color{200, 100, 0, 255}, .radius = BorderRadius::all(8)};
  transform = Transform2D::scaling(2.0f, 2.0f);
  root.rebuild();
  h.frame();

  h.frame(0.1f);
  CHECK_EQ(h.buildOwner().buildCount(), 0);
  CHECK_EQ(h.pipeline().stats().layouts, 0);
  CHECK_EQ(decorationOf(h).decoration().color, (Color{100, 50, 0, 128}));
  CHECK_EQ(decorationOf(h).decoration().radius, BorderRadius::all(4));
  CHECK_EQ(transformOf(h).transform(), Transform2D::scaling(1.5f, 1.5f));

  h.frame(0.1f);
  CHECK_EQ(decorationOf(h).decoration().color, (Color{200, 100, 0, 255}));
  CHECK_EQ(transformOf(h).transform(), Transform2D::scaling(2.0f, 2.0f));
}

TEST(implicit_a_steady_frame_of_animation_allocates_nothing) {
  Harness h;
  float opacity = 1.0f;

  ScriptedRoot root(h, [&] {
    return AnimatedOpacity::make({
        .opacity = opacity,
        .animation = {.duration = 1.0f},
        .child = leaf(),
    });
  });
  h.frame();

  opacity = 0.0f;
  root.rebuild();
  for (int i = 0; i < 4; ++i) h.frame(0.02f);

  const std::size_t before = allocationCount();
  for (int i = 0; i < 8; ++i) h.frame(0.02f);
  CHECK_EQ(allocationCount() - before, std::size_t{0});
}

// ---------------------------------------------------------------------------
// Ticker mode
// ---------------------------------------------------------------------------

TEST(implicit_a_hidden_subtree_costs_no_time_and_resumes_where_it_stopped) {
  Harness h;
  float opacity = 1.0f;
  bool visible = true;

  ScriptedRoot root(h, [&] {
    return TickerMode::make({
        .enabled = visible,
        .child = AnimatedOpacity::make({
            .opacity = opacity,
            .animation = {.duration = 0.2f},
            .child = leaf(),
        }),
    });
  });
  h.frame();

  opacity = 0.0f;
  root.rebuild();
  h.frame();
  h.frame(0.05f);
  CHECK_NEAR(opacityOf(h).opacity(), 0.75f, 1e-5);
  CHECK_EQ(h.binding().tickers().activeTickerCount(), std::size_t{1});

  visible = false;
  root.rebuild();
  h.frame();
  CHECK_EQ(h.binding().tickers().activeTickerCount(), std::size_t{0});
  CHECK(!h.binding().needsFrame());

  for (int i = 0; i < 10; ++i) h.frame(0.1f);
  CHECK_NEAR(opacityOf(h).opacity(), 0.75f, 1e-5);

  visible = true;
  root.rebuild();
  h.frame();
  CHECK_EQ(h.binding().tickers().activeTickerCount(), std::size_t{1});

  h.frame(0.05f);
  CHECK_NEAR(opacityOf(h).opacity(), 0.5f, 1e-5);
  h.frame(0.15f);
  CHECK_EQ(opacityOf(h).opacity(), 0.0f);
}

TEST(implicit_a_tree_with_no_ticker_mode_in_it_pays_nothing_for_the_mechanism) {
  Harness h;
  float opacity = 1.0f;

  ScriptedRoot root(h, [&] {
    return AnimatedOpacity::make({
        .opacity = opacity,
        .animation = {.duration = 0.2f},
        .child = leaf(),
    });
  });
  h.frame();

  opacity = 0.0f;
  root.rebuild();
  h.frame();
  h.frame(0.1f);
  CHECK_NEAR(opacityOf(h).opacity(), 0.5f, 1e-5);
}

TEST(implicit_an_unmounted_state_releases_its_ticker_before_it_is_destroyed) {
  Harness h;
  float opacity = 1.0f;

  ScriptedRoot root(h, [&] {
    return AnimatedOpacity::make({
        .opacity = opacity,
        .animation = {.duration = 0.2f},
        .child = leaf(),
    });
  });
  h.frame();

  opacity = 0.0f;
  root.rebuild();
  h.frame();
  CHECK_EQ(h.binding().tickers().activeTickerCount(), std::size_t{1});

  // Unmounted but not yet destroyed, which is the window a subtree teardown
  // actually spends: every element in it unmounts before any of them is
  // destroyed, so the destructor is too late to be the only release.
  Element& animated = elementFor(h.rootElement(), widgetTypeOf<AnimatedOpacity>());
  animated.detachRenderObject();
  animated.unmount();
  CHECK_EQ(h.binding().tickers().activeTickerCount(), std::size_t{0});
}

TEST(implicit_an_unmounted_animation_releases_its_ticker) {
  Harness h;
  float opacity = 1.0f;
  bool present = true;

  ScriptedRoot root(h, [&] {
    return Column::make({.children = {present ? AnimatedOpacity::make({
                                                    .opacity = opacity,
                                                    .animation = {.duration = 0.2f},
                                                    .child = leaf(),
                                                })
                                              : WidgetRef{}}});
  });
  h.frame();

  opacity = 0.0f;
  root.rebuild();
  h.frame(0.02f);
  CHECK_EQ(h.binding().tickers().activeTickerCount(), std::size_t{1});

  present = false;
  root.rebuild();
  h.frame();
  CHECK_EQ(h.binding().tickers().activeTickerCount(), std::size_t{0});
  CHECK(!h.binding().needsFrame());
}
