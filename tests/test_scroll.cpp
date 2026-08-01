#include "testing.hpp"

#include <memory>

#include "fltr/gestures/pointer_region.hpp"
#include "fltr/harness.hpp"
#include "fltr/widgets/scroll.hpp"
#include "widget_harness.hpp"

using namespace fltr;
using namespace fltrtest;

namespace {

PointerEvent touch(PointerPhase phase, Offset position, PointerId pointer = 0) {
  return {phase, pointer, PointerDeviceKind::Touch, position};
}

PointerEvent mouse(PointerPhase phase, Offset position, PointerId pointer = 0) {
  return {phase, pointer, PointerDeviceKind::Mouse, position};
}

/// The harness surface, and therefore every viewport below, is 200x100.
constexpr Size kSurface{200, 100};

WidgetRef content(float height) { return SizedBox::make({.size = {kSurface.width, height}}); }

WidgetRef listOf(ScrollController* controller, float height = 400.0f,
                 const ScrollPhysics* physics = nullptr) {
  return Scrollable::make({
      .controller = controller,
      .physics = physics,
      .child = content(height),
  });
}

/// Ten stacked boxes, keyed by index, so a test can name one and ask to see it.
WidgetRef rows(float height) {
  const auto row = [height](int index) {
    return SizedBox::make({.key = Key::of(index), .size = {kSurface.width, height}});
  };
  return Column::make({
      .mainAxisSize = MainAxisSize::Min,
      .children = {row(0), row(1), row(2), row(3), row(4), row(5), row(6), row(7), row(8), row(9)},
  });
}

/// Reports the ambient scroll position at its own place in the tree.
class Probe final : public Configure<Probe, StatelessWidget> {
public:
  struct Args {
    Key key;
    ScrollPosition** found = nullptr;
    float height = 400.0f;
  };

  explicit Probe(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Probe"; }

  WidgetRef build(BuildContext& context) const {
    *args_.found = ScrollScope::of(context);
    return content(args_.height);
  }

private:
  Args args_;
};

/// A drag whose samples land on separate frames, so the velocity tracker sees a
/// speed rather than one instant with everything in it.
void flingUp(Harness& h, float from, float step, int steps) {
  float y = from;
  h.binding().dispatchPointer(touch(PointerPhase::Down, {100, y}));
  for (int i = 0; i < steps; ++i) {
    h.frame(0.016f);
    y -= step;
    h.binding().dispatchPointer(touch(PointerPhase::Move, {100, y}));
  }
  h.binding().dispatchPointer(touch(PointerPhase::Up, {100, y}));
}

template <class W>
typename W::Render& renderFor(Harness& h, Key key = Key::none()) {
  Element& element = elementFor(h.rootElement(), widgetTypeOf<W>(), key);
  return static_cast<typename W::Render&>(*element.renderObject());
}

}  // namespace

// ---------------------------------------------------------------------------
// Simulations
// ---------------------------------------------------------------------------

TEST(scroll_friction_decays_toward_a_rest_it_only_reaches_in_the_limit) {
  const FrictionSimulation friction(0.135f, 0.0f, 1000.0f);
  const float rest = friction.finalX();

  CHECK(friction.x(0.0f) == 0.0f);
  CHECK(friction.x(0.1f) < friction.x(0.2f));
  CHECK(friction.x(0.5f) < rest);
  CHECK(friction.dx(0.5f) < friction.dx(0.0f));
  CHECK(!friction.isDone(0.5f));
  CHECK(friction.isDone(8.0f));
  CHECK_NEAR(friction.x(8.0f), rest, 0.01f);

  // The point it passes on the way is where the bouncing simulation hands over.
  CHECK_NEAR(friction.x(friction.timeAtX(rest * 0.5f)), rest * 0.5f, 0.01f);
  CHECK_EQ(friction.timeAtX(rest * 2.0f), kInf);
}

TEST(scroll_a_critically_damped_spring_arrives_without_passing_its_target) {
  const SpringSimulation spring(SpringDescription::withDampingRatio(1.0f, 100.0f, 1.0f), 0.0f,
                                100.0f, 0.0f);
  for (float t = 0.0f; t < 2.0f; t += 0.01f) CHECK(spring.x(t) <= 100.0f);
  CHECK(spring.isDone(2.0f));
  CHECK_NEAR(spring.x(2.0f), 100.0f, 0.1f);
}

TEST(scroll_an_underdamped_spring_overshoots_and_rings_back) {
  const SpringSimulation spring(SpringDescription::withDampingRatio(1.0f, 100.0f, 0.4f), 0.0f,
                                100.0f, 0.0f);
  float highest = 0.0f;
  for (float t = 0.0f; t < 3.0f; t += 0.01f) highest = std::max(highest, spring.x(t));
  CHECK(highest > 100.0f);
  CHECK(spring.isDone(3.0f));
  CHECK_NEAR(spring.x(3.0f), 100.0f, 0.1f);
}

TEST(scroll_the_clamping_fling_stops_at_a_finite_time_rather_than_crawling) {
  const ClampingScrollSimulation fling(0.0f, 1000.0f);
  CHECK(!fling.isDone(0.1f));

  float settled = 0.1f;
  while (!fling.isDone(settled) && settled < 10.0f) settled += 0.01f;
  CHECK(settled < 2.0f);
  // Past its own duration it neither moves nor claims to.
  CHECK_EQ(fling.x(settled + 1.0f), fling.x(settled));
  CHECK_EQ(fling.dx(settled + 1.0f), 0.0f);
}

TEST(scroll_a_bouncing_fling_carries_past_the_edge_and_is_pulled_back) {
  const BouncingScrollSimulation fling(0.0f, 2000.0f, 0.0f, 100.0f,
                                       SpringDescription::withDampingRatio(0.5f, 100.0f, 1.1f));
  float highest = 0.0f;
  for (float t = 0.0f; t < 3.0f; t += 0.005f) highest = std::max(highest, fling.x(t));
  CHECK(highest > 100.0f);
  CHECK(fling.isDone(3.0f));
  CHECK_NEAR(fling.x(3.0f), 100.0f, 0.5f);
}

// ---------------------------------------------------------------------------
// The viewport and its dimensions
// ---------------------------------------------------------------------------

TEST(scroll_a_viewport_reports_the_extents_its_content_implies) {
  Harness h;
  ScrollController controller;
  ScriptedRoot root(h, [&] { return listOf(&controller, 400.0f); });
  h.frame();

  const ScrollMetrics& metrics = controller.position().metrics();
  CHECK_EQ(metrics.viewportDimension, 100.0f);
  CHECK_EQ(metrics.maxScrollExtent, 300.0f);
  CHECK_EQ(metrics.pixels, 0.0f);
  CHECK(metrics.canScroll());

  // Content that fits leaves nothing to scroll.
  CHECK_EQ(renderFor<Viewport>(h).size(), kSurface);
}

TEST(scroll_content_shorter_than_its_viewport_cannot_scroll_at_all) {
  Harness h;
  ScrollController controller;
  ScriptedRoot root(h, [&] { return listOf(&controller, 60.0f); });
  h.frame();

  CHECK_EQ(controller.position().maxScrollExtent(), 0.0f);
  CHECK(!controller.position().metrics().canScroll());
  CHECK(!h.binding().dispatchSignal({.position = {100, 50}, .delta = {0, 40}}));
}

TEST(scroll_moving_the_offset_paints_and_never_lays_out) {
  Harness h;
  ScrollController controller;
  ScriptedRoot root(h, [&] { return listOf(&controller); });
  h.frame();

  controller.jumpTo(120.0f);
  h.frame();

  CHECK_EQ(controller.offset(), 120.0f);
  CHECK_EQ(h.pipeline().stats().layouts, 0);
  CHECK_EQ(h.pipeline().stats().boundariesRepainted, 1);
  CHECK_EQ(h.buildOwner().buildCount(), 0);
}

TEST(scroll_a_frame_of_scrolling_re_records_a_clip_and_one_reference) {
  Harness h;
  ScrollController controller;
  ScriptedRoot root(h, [&] { return listOf(&controller); });
  h.frame();

  const DisplayList* viewportList = renderFor<Viewport>(h).boundaryListIfAny();
  CHECK(viewportList != nullptr);
  const std::uint64_t before = viewportList->revision();

  controller.jumpTo(50.0f);
  h.frame();

  CHECK_NE(viewportList->revision(), before);
  CHECK_EQ(viewportList->size(), std::size_t{3});  // clip, the content's list, pop
}

TEST(scroll_content_that_shrank_under_the_offset_is_settled_back_into_range) {
  Harness h;
  ScrollController controller;
  float height = 400.0f;
  ScriptedRoot root(h, [&] { return listOf(&controller, height); });
  h.frame();

  controller.jumpTo(300.0f);
  h.frame();
  CHECK_EQ(controller.offset(), 300.0f);

  height = 150.0f;
  root.rebuild();
  h.frame();
  CHECK_EQ(controller.position().maxScrollExtent(), 50.0f);

  // A spring, not a clamp, so it is briefly still outside and then arrives.
  CHECK(controller.offset() > 50.0f);
  for (int i = 0; i < 60 && controller.offset() > 50.0f; ++i) h.frame(0.016f);
  CHECK_NEAR(controller.offset(), 50.0f, 0.2f);
}

// ---------------------------------------------------------------------------
// Dragging
// ---------------------------------------------------------------------------

TEST(scroll_dragging_moves_the_content_with_the_pointer) {
  Harness h;
  ScrollController controller;
  ScriptedRoot root(h, [&] { return listOf(&controller); });
  h.frame();

  h.binding().dispatchPointer(touch(PointerPhase::Down, {100, 80}));
  h.binding().dispatchPointer(touch(PointerPhase::Move, {100, 60}));
  CHECK_EQ(controller.offset(), 20.0f);

  h.binding().dispatchPointer(touch(PointerPhase::Move, {100, 70}));
  CHECK_EQ(controller.offset(), 10.0f);

  h.binding().dispatchPointer(touch(PointerPhase::Up, {100, 70}));
}

TEST(scroll_a_release_with_speed_keeps_going_and_then_settles) {
  Harness h;
  ScrollController controller;
  ScriptedRoot root(h, [&] { return listOf(&controller); });
  h.frame();

  flingUp(h, 90.0f, 20.0f, 4);
  const float atRelease = controller.offset();
  CHECK_EQ(controller.position().activity(), ScrollActivityKind::Ballistic);

  for (int i = 0; i < 120 && controller.position().isScrolling(); ++i) h.frame(0.016f);
  CHECK(controller.offset() > atRelease);
  CHECK_EQ(controller.position().activity(), ScrollActivityKind::Idle);
  CHECK(controller.offset() <= controller.position().maxScrollExtent());
}

TEST(scroll_a_slow_release_stops_where_it_was_let_go) {
  Harness h;
  ScrollController controller;
  ScriptedRoot root(h, [&] { return listOf(&controller); });
  h.frame();

  h.binding().dispatchPointer(touch(PointerPhase::Down, {100, 80}));
  h.frame(0.5f);
  h.binding().dispatchPointer(touch(PointerPhase::Move, {100, 60}));
  h.frame(0.5f);
  h.binding().dispatchPointer(touch(PointerPhase::Up, {100, 60}));

  const float released = controller.offset();
  CHECK_EQ(controller.position().activity(), ScrollActivityKind::Idle);
  h.frame(0.1f);
  CHECK_EQ(controller.offset(), released);
}

TEST(scroll_the_boundary_refuses_what_is_pushed_past_it_and_reports_how_much) {
  Harness h;
  ScrollController controller;
  ScriptedRoot root(h, [&] { return listOf(&controller); });
  h.frame();

  h.binding().dispatchPointer(touch(PointerPhase::Down, {100, 20}));
  h.binding().dispatchPointer(touch(PointerPhase::Move, {100, 60}));

  CHECK_EQ(controller.offset(), 0.0f);
  CHECK(controller.position().overscroll() < 0.0f);

  // Letting go lets it go.
  h.binding().dispatchPointer(touch(PointerPhase::Up, {100, 60}));
  for (int i = 0; i < 60 && controller.position().overscroll() != 0.0f; ++i) h.frame(0.016f);
  CHECK_EQ(controller.position().overscroll(), 0.0f);
}

TEST(scroll_bouncing_physics_leaves_its_range_instead_of_refusing) {
  Harness h;
  ScrollController controller;
  const BouncingScrollPhysics physics;
  ScriptedRoot root(h, [&] { return listOf(&controller, 400.0f, &physics); });
  h.frame();

  // The first push across the edge is at full strength, as Flutter's is: the
  // resistance is a function of how far outside we already are.
  h.binding().dispatchPointer(touch(PointerPhase::Down, {100, 20}));
  h.binding().dispatchPointer(touch(PointerPhase::Move, {100, 40}));
  CHECK_EQ(controller.offset(), -20.0f);
  CHECK_EQ(controller.position().overscroll(), 0.0f);

  h.binding().dispatchPointer(touch(PointerPhase::Move, {100, 60}));
  CHECK(controller.offset() > -40.0f);

  h.binding().dispatchPointer(touch(PointerPhase::Up, {100, 60}));
  for (int i = 0; i < 200 && controller.position().isScrolling(); ++i) h.frame(0.016f);
  CHECK_NEAR(controller.offset(), 0.0f, 0.2f);
}

TEST(scroll_physics_that_refuse_every_offset_keep_the_view_where_it_is) {
  Harness h;
  ScrollController controller;
  const NeverScrollPhysics physics;
  ScriptedRoot root(h, [&] { return listOf(&controller, 400.0f, &physics); });
  h.frame();

  h.binding().dispatchPointer(touch(PointerPhase::Down, {100, 80}));
  h.binding().dispatchPointer(touch(PointerPhase::Move, {100, 40}));
  CHECK_EQ(controller.offset(), 0.0f);
  CHECK(!h.binding().dispatchSignal({.position = {100, 50}, .delta = {0, 40}}));
}

// ---------------------------------------------------------------------------
// Wheel and trackpad
// ---------------------------------------------------------------------------

TEST(scroll_a_wheel_notch_eases_toward_its_target_rather_than_jumping) {
  Harness h;
  ScrollController controller;
  ScriptedRoot root(h, [&] { return listOf(&controller); });
  h.frame();

  CHECK(h.binding().dispatchSignal({.position = {100, 50}, .delta = {0, 60}}));
  CHECK_EQ(controller.offset(), 0.0f);
  CHECK_EQ(controller.position().activity(), ScrollActivityKind::Wheel);

  h.frame(0.016f);
  CHECK(controller.offset() > 0.0f);
  CHECK(controller.offset() < 60.0f);

  for (int i = 0; i < 60 && controller.position().isScrolling(); ++i) h.frame(0.016f);
  CHECK_EQ(controller.offset(), 60.0f);
  CHECK_EQ(controller.position().activity(), ScrollActivityKind::Idle);
}

TEST(scroll_notches_arriving_mid_flight_add_to_the_target_rather_than_restart_it) {
  Harness h;
  ScrollController controller;
  ScriptedRoot root(h, [&] { return listOf(&controller); });
  h.frame();

  const PointerSignalEvent notch{.position = {100, 50}, .delta = {0, 60}};
  h.binding().dispatchSignal(notch);
  h.frame(0.016f);
  h.binding().dispatchSignal(notch);

  for (int i = 0; i < 60 && controller.position().isScrolling(); ++i) h.frame(0.016f);
  CHECK_EQ(controller.offset(), 120.0f);
}

TEST(scroll_a_wheel_at_the_end_declines_so_the_signal_chains_outward) {
  Harness h;
  ScrollController controller;
  ScriptedRoot root(h, [&] { return listOf(&controller); });
  h.frame();

  controller.jumpTo(300.0f);
  CHECK(!h.binding().dispatchSignal({.position = {100, 50}, .delta = {0, 40}}));
  CHECK(h.binding().dispatchSignal({.position = {100, 50}, .delta = {0, -40}}));
}

TEST(scroll_a_trackpad_pan_is_a_displacement_and_is_applied_as_one) {
  Harness h;
  ScrollController controller;
  ScriptedRoot root(h, [&] { return listOf(&controller); });
  h.frame();

  CHECK(h.binding().dispatchSignal(
      {.kind = PointerSignalKind::Pan, .position = {100, 50}, .delta = {0, 40}}));
  CHECK_EQ(controller.offset(), 40.0f);
  CHECK_EQ(controller.position().activity(), ScrollActivityKind::Idle);
}

// ---------------------------------------------------------------------------
// Controller, scope, and ensureVisible
// ---------------------------------------------------------------------------

TEST(scroll_a_controller_holds_where_it_was_before_it_had_a_scrollable) {
  Harness h;
  ScrollController controller(120.0f);
  CHECK(!controller.hasClient());
  CHECK_EQ(controller.offset(), 120.0f);

  ScriptedRoot root(h, [&] { return listOf(&controller); });
  h.frame();

  CHECK(controller.hasClient());
  CHECK_EQ(controller.offset(), 120.0f);
}

/// The other order -- a controller destroyed while the tree is still up -- is
/// what every test here does, since the harness is declared first and torn down
/// last. Both links clear themselves, and the sanitizer build is the assertion.
TEST(scroll_a_controller_outliving_its_scrollable_is_inert_rather_than_stale) {
  ScrollController controller;
  {
    Harness h;
    ScriptedRoot root(h, [&] { return listOf(&controller); });
    h.frame();
    controller.jumpTo(100.0f);
    CHECK(controller.hasClient());
    CHECK_EQ(controller.offset(), 100.0f);
  }

  CHECK(!controller.hasClient());
  controller.jumpTo(50.0f);
  CHECK_EQ(controller.offset(), 0.0f);
}

/// A position remembers one controller, so swapping has to be a hand-off rather
/// than an overwrite -- otherwise the one let go of still believes it drives
/// this scroll view, and still tries to unhook from it on the way out.
TEST(scroll_swapping_the_controller_lets_go_of_the_old_one) {
  Harness h;
  ScrollController first;
  ScrollController second;
  bool swapped = false;
  ScriptedRoot root(h, [&] { return listOf(swapped ? &second : &first); });
  h.frame();

  first.jumpTo(40.0f);
  CHECK(first.hasClient());
  CHECK(!second.hasClient());

  swapped = true;
  root.rebuild();
  h.frame();

  CHECK(!first.hasClient());
  CHECK_EQ(first.offset(), 0.0f);
  CHECK(second.hasClient());
  CHECK_EQ(second.offset(), 40.0f);
}

TEST(scroll_the_ambient_scope_hands_the_position_to_whatever_is_inside) {
  Harness h;
  ScrollController controller;
  ScrollPosition* seen = nullptr;
  ScriptedRoot root(h, [&] {
    return Scrollable::make({.controller = &controller, .child = Probe::make({.found = &seen})});
  });
  h.frame();

  CHECK_EQ(seen, &controller.position());
}

TEST(scroll_a_widget_outside_any_scrollable_finds_no_scope) {
  Harness h;
  ScrollPosition* seen = nullptr;
  ScriptedRoot root(h, [&] { return Probe::make({.found = &seen, .height = 50.0f}); });
  h.frame();

  CHECK_EQ(seen, static_cast<ScrollPosition*>(nullptr));
}

TEST(scroll_ensure_visible_brings_a_descendant_to_the_edge_it_was_asked_for) {
  Harness h;
  ScrollController controller;
  ScriptedRoot root(h, [&] {
    return Scrollable::make({.controller = &controller, .child = rows(40.0f)});
  });
  h.frame();

  Element& target = elementFor(h.rootElement(), widgetTypeOf<SizedBox>(), Key::of(5));
  CHECK_EQ(controller.position().maxScrollExtent(), 300.0f);

  controller.position().ensureVisible(*target.renderObject());
  CHECK_EQ(controller.offset(), 200.0f);

  // Against the trailing edge instead, which is where a focus walk downward
  // wants the thing it just reached.
  controller.jumpTo(0.0f);
  controller.position().ensureVisible(*target.renderObject(), 1.0f);
  CHECK_EQ(controller.offset(), 140.0f);
}

TEST(scroll_ensure_visible_says_the_same_thing_wherever_the_view_already_is) {
  Harness h;
  ScrollController controller;
  ScriptedRoot root(h, [&] {
    return Scrollable::make({.controller = &controller, .child = rows(40.0f)});
  });
  h.frame();

  Element& target = elementFor(h.rootElement(), widgetTypeOf<SizedBox>(), Key::of(5));
  controller.jumpTo(280.0f);
  h.frame();
  controller.position().ensureVisible(*target.renderObject());
  CHECK_EQ(controller.offset(), 200.0f);
}

TEST(scroll_an_animated_scroll_arrives_over_the_time_it_was_given) {
  Harness h;
  ScrollController controller;
  ScriptedRoot root(h, [&] { return listOf(&controller); });
  h.frame();

  controller.animateTo(200.0f, 0.4f, Curves::linear);
  CHECK_EQ(controller.position().activity(), ScrollActivityKind::Driven);

  h.frame(0.2f);
  CHECK_NEAR(controller.offset(), 100.0f, 0.01f);

  h.frame(0.2f);
  CHECK_EQ(controller.offset(), 200.0f);
  CHECK_EQ(controller.position().activity(), ScrollActivityKind::Idle);
}

// ---------------------------------------------------------------------------
// Scrollbar
// ---------------------------------------------------------------------------

namespace {

WidgetRef barred(ScrollController* controller, float height = 400.0f) {
  return Scrollbar::make({
      .controller = controller,
      .child = Scrollable::make({.controller = controller, .child = content(height)}),
  });
}

}  // namespace

TEST(scroll_the_thumb_is_sized_by_how_much_of_the_content_is_visible) {
  Harness h;
  ScrollController controller;
  ScriptedRoot root(h, [&] { return barred(&controller); });
  h.frame();

  RenderScrollbarThumb& thumb = renderFor<ScrollbarThumb>(h);
  CHECK_EQ(thumb.thumbRect(), Rect::fromLTWH(0, 0, 6, 25));

  controller.jumpTo(150.0f);
  h.frame();
  CHECK_EQ(thumb.thumbRect(), Rect::fromLTWH(0, 37.5f, 6, 25));

  controller.jumpTo(300.0f);
  h.frame();
  CHECK_EQ(thumb.thumbRect(), Rect::fromLTWH(0, 75, 6, 25));
}

TEST(scroll_a_thumb_never_shrinks_below_the_size_a_pointer_can_hold) {
  Harness h;
  ScrollController controller;
  ScriptedRoot root(h, [&] { return barred(&controller, 4000.0f); });
  h.frame();

  CHECK_EQ(renderFor<ScrollbarThumb>(h).thumbRect().height(), 24.0f);
}

TEST(scroll_dragging_the_thumb_moves_the_content_further_than_the_thumb) {
  Harness h;
  ScrollController controller;
  ScriptedRoot root(h, [&] { return barred(&controller); });
  h.frame();

  h.binding().dispatchPointer(touch(PointerPhase::Down, {197, 10}));
  h.binding().dispatchPointer(touch(PointerPhase::Move, {197, 25}));
  h.binding().dispatchPointer(touch(PointerPhase::Up, {197, 25}));

  // Fifteen pixels of track over a 75-pixel travel is a fifth of 300.
  CHECK_EQ(controller.offset(), 60.0f);
}

TEST(scroll_a_press_on_the_track_away_from_the_thumb_is_not_a_grab) {
  Harness h;
  ScrollController controller;
  ScriptedRoot root(h, [&] { return barred(&controller); });
  h.frame();

  h.binding().dispatchPointer(touch(PointerPhase::Down, {197, 80}));
  h.binding().dispatchPointer(touch(PointerPhase::Move, {197, 90}));
  h.binding().dispatchPointer(touch(PointerPhase::Up, {197, 90}));

  CHECK_EQ(controller.offset(), 0.0f);
}

TEST(scroll_the_thumb_appears_when_something_moves_and_fades_once_nothing_does) {
  Harness h;
  ScrollController controller;
  ScriptedRoot root(h, [&] { return barred(&controller); });
  h.frame();

  RenderScrollbarThumb& thumb = renderFor<ScrollbarThumb>(h);
  CHECK_EQ(thumb.opacity(), 0.0f);

  controller.jumpTo(50.0f);
  for (int i = 0; i < 20; ++i) h.frame(0.016f);
  CHECK_EQ(thumb.opacity(), 1.0f);

  for (int i = 0; i < 80; ++i) h.frame(0.016f);
  CHECK_EQ(thumb.opacity(), 0.0f);
}

TEST(scroll_a_pointer_resting_on_the_bar_keeps_it_up_and_widens_it) {
  Harness h;
  ScrollController controller;
  ScriptedRoot root(h, [&] { return barred(&controller); });
  h.frame();

  h.binding().dispatchPointer(mouse(PointerPhase::Hover, {197, 10}));
  for (int i = 0; i < 80; ++i) h.frame(0.016f);

  RenderScrollbarThumb& thumb = renderFor<ScrollbarThumb>(h);
  CHECK_EQ(thumb.opacity(), 1.0f);
  CHECK_EQ(thumb.size().width, 10.0f);

  h.binding().dispatchPointer(mouse(PointerPhase::Hover, {100, 50}));
  h.frame();
  CHECK_EQ(renderFor<ScrollbarThumb>(h).size().width, 6.0f);
}

// ---------------------------------------------------------------------------
// Overscroll
// ---------------------------------------------------------------------------

TEST(scroll_a_refused_push_stretches_the_view_and_lays_nothing_out) {
  Harness h;
  ScrollController controller;
  ScriptedRoot root(h, [&] {
    return Overscroll::make({.controller = &controller, .child = listOf(&controller)});
  });
  h.frame();

  RenderTransform& stretch = renderFor<Transform>(h);
  CHECK(stretch.transform().isIdentity());

  h.binding().dispatchPointer(touch(PointerPhase::Down, {100, 20}));
  h.binding().dispatchPointer(touch(PointerPhase::Move, {100, 60}));
  h.frame();

  CHECK(!stretch.transform().isIdentity());
  CHECK(stretch.transform().d > 1.0f);
  CHECK_EQ(stretch.transform().a, 1.0f);
  CHECK_EQ(h.pipeline().stats().layouts, 0);

  h.binding().dispatchPointer(touch(PointerPhase::Up, {100, 60}));
  for (int i = 0; i < 60 && !stretch.transform().isIdentity(); ++i) h.frame(0.016f);
  CHECK(stretch.transform().isIdentity());
}

TEST(scroll_a_view_that_bounces_never_stretches_as_well) {
  Harness h;
  ScrollController controller;
  const BouncingScrollPhysics physics;
  ScriptedRoot root(h, [&] {
    return Overscroll::make({.controller = &controller,
                             .child = listOf(&controller, 400.0f, &physics)});
  });
  h.frame();

  h.binding().dispatchPointer(touch(PointerPhase::Down, {100, 20}));
  h.binding().dispatchPointer(touch(PointerPhase::Move, {100, 60}));
  h.frame();

  CHECK(controller.offset() < 0.0f);
  CHECK(renderFor<Transform>(h).transform().isIdentity());
}

// ---------------------------------------------------------------------------
// Cost
// ---------------------------------------------------------------------------

TEST(scroll_a_frame_of_fling_allocates_nothing) {
  Harness h;
  ScrollController controller;
  ScriptedRoot root(h, [&] { return listOf(&controller); });
  h.frame();

  // One whole fling first, so every buffer a scrolling frame touches has
  // reached the size it will keep.
  flingUp(h, 90.0f, 20.0f, 4);
  for (int i = 0; i < 120 && controller.position().isScrolling(); ++i) h.frame(0.016f);
  controller.jumpTo(0.0f);

  flingUp(h, 90.0f, 20.0f, 4);
  CHECK_EQ(controller.position().activity(), ScrollActivityKind::Ballistic);

  const std::size_t before = allocationCount();
  for (int i = 0; i < 8; ++i) h.frame(0.016f);
  CHECK_EQ(allocationCount(), before);
  CHECK(controller.position().isScrolling());
}

TEST(scroll_a_settled_view_asks_for_no_frames) {
  Harness h;
  ScrollController controller;
  ScriptedRoot root(h, [&] { return listOf(&controller); });
  h.frame();
  CHECK(!h.binding().needsFrame());

  h.binding().dispatchSignal({.position = {100, 50}, .delta = {0, 60}});
  CHECK(h.binding().needsFrame());

  for (int i = 0; i < 60 && controller.position().isScrolling(); ++i) h.frame(0.016f);
  h.frame();
  CHECK(!h.binding().needsFrame());
}
